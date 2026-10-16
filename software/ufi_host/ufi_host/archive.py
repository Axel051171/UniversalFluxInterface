"""UFI native tick archive v1 (SQLite, application_id 'UFI1').

Each CRC-validated v2 frame is committed immediately, before decoding/conversion.
Malformed wire traffic rejected by FrameParser is not represented. Revolutions
contain little-endian uint32 timestamps at the recorded sample_hz, without SCP
quantisation. Incomplete attempts survive interruption and are never labelled OK.
"""
from __future__ import annotations

import hashlib
import json
import sqlite3
import struct
from datetime import datetime, timezone
from pathlib import Path


class NativeArchive:
    def __init__(self, path: str, metadata: dict):
        # Never silently overwrite a preservation master.
        with Path(path).open("xb"):
            pass
        self.db = sqlite3.connect(path)
        self.db.executescript("""
            PRAGMA application_id = 1430661425;
            PRAGMA user_version = 1;
            PRAGMA synchronous = FULL;
            CREATE TABLE metadata (json TEXT NOT NULL);
            CREATE TABLE captures (id INTEGER PRIMARY KEY, track INTEGER, side INTEGER,
                requested INTEGER, status INTEGER, error TEXT, started TEXT NOT NULL);
            CREATE TABLE frames (capture INTEGER, ordinal INTEGER, data BLOB NOT NULL,
                sha256 TEXT NOT NULL, PRIMARY KEY(capture, ordinal));
            CREATE TABLE revolutions (capture INTEGER, ordinal INTEGER, index_ticks INTEGER,
                flags INTEGER, samples BLOB NOT NULL, sha256 TEXT NOT NULL,
                PRIMARY KEY(capture, ordinal));
        """)
        with self.db:
            self.db.execute("INSERT INTO metadata VALUES (?)", (json.dumps(metadata, sort_keys=True),))

    def begin(self, track: int, side: int, requested: int) -> int:
        with self.db:
            cur = self.db.execute("INSERT INTO captures(track,side,requested,started) VALUES (?,?,?,?)",
                                  (track, side, requested, datetime.now(timezone.utc).isoformat()))
        return cur.lastrowid

    def frame(self, capture: int, data: bytes) -> None:
        with self.db:
            self.db.execute("INSERT INTO frames SELECT ?,COUNT(*),?,? FROM frames WHERE capture=?",
                            (capture, data, hashlib.sha256(data).hexdigest(), capture))

    def finish(self, capture: int, result) -> None:
        with self.db:
            for i, rev in enumerate(result.revolutions):
                data = struct.pack(f"<{len(rev.samples)}I", *rev.samples)
                self.db.execute("INSERT INTO revolutions VALUES (?,?,?,?,?,?)",
                                (capture, i, rev.index_ticks, rev.flags, data, hashlib.sha256(data).hexdigest()))
            self.db.execute("UPDATE captures SET status=? WHERE id=?", (result.status, capture))

    def fail(self, capture: int, error: str) -> None:
        with self.db:
            self.db.execute("UPDATE captures SET error=? WHERE id=?", (error, capture))

    def close(self) -> None:
        self.db.close()


def verify_archive(path: str) -> dict:
    """Read-only structural/hash check, also reporting unfinished captures."""
    db = sqlite3.connect(Path(path).resolve().as_uri() + "?mode=ro", uri=True)
    try:
        if db.execute("PRAGMA application_id").fetchone()[0] != 1430661425 or db.execute("PRAGMA user_version").fetchone()[0] != 1:
            raise ValueError("not a supported UFI archive")
        if db.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
            raise ValueError("damaged archive database")
        metadata = json.loads(db.execute("SELECT json FROM metadata").fetchone()[0])
        if not isinstance(metadata.get("sample_hz"), int) or metadata["sample_hz"] <= 0:
            raise ValueError("archive clock missing or invalid")
        counts = {}
        for table, column in (("frames", "data"), ("revolutions", "samples")):
            count = 0
            for data, digest in db.execute(f"SELECT {column},sha256 FROM {table}"):
                if hashlib.sha256(data).hexdigest() != digest:
                    raise ValueError(f"{table} checksum mismatch")
                if table == "revolutions" and len(data) % 4:
                    raise ValueError("invalid native timestamp array")
                count += 1
            counts[table] = count
        captures, unfinished, failed = db.execute("SELECT COUNT(*),COALESCE(SUM(status IS NULL),0),"
            "COALESCE(SUM(status != 0 OR error IS NOT NULL),0) FROM captures").fetchone()
        return dict(schema=1, metadata=metadata, captures=captures, unfinished=unfinished,
                    failed=failed, **counts)
    finally:
        db.close()
