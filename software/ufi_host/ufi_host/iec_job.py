"""Checkpointed IEC block reads. Only complete images receive the requested name.

The caller must keep the same physical disk inserted when resuming: IEC provides
no immutable media identity. Hashes verify the checkpoint files, not the disk.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path

from . import cbm


def write_image(dev, source: str, checkpoint: str, device: int = 0,
                retries: int = 2, resume: bool = False, cancelled=None) -> dict:
    """Checkpointed U2 writes with mandatory U1 readback. Resume rechecks all sectors.

    Source hash/address/geometry are stable job identity; completed sectors also
    have to match the inserted medium. This is not a physical disk serial number.
    """
    if not 0 <= retries <= 20:
        raise ValueError("retries must be 0..20")
    raw = Path(source).read_bytes()
    kind, tracks = cbm.geometry(len(raw))
    size = cbm.image_size(kind, tracks)
    if any(x != 1 for x in raw[size:]):
        raise ValueError("source image contains unreadable sectors")
    journal = Path(checkpoint)
    identity = dict(schema=1, operation="write", source_sha256=hashlib.sha256(raw).hexdigest(),
                    device=device, kind=kind, tracks=tracks)
    if resume:
        state = json.loads(journal.read_text(encoding="utf-8"))
        if any(state.get(k) != v for k, v in identity.items()):
            raise ValueError("write checkpoint does not match source/device/geometry")
    else:
        if journal.exists():
            raise FileExistsError("write checkpoint exists; use --resume")
        state = dict(identity, complete=False, sectors={})
        _save(journal, state)
    blocks = list(cbm.blocks(kind, tracks))
    # Validate every acknowledged sector before performing any resumed writes.
    if resume:
        for i, (track, sector) in enumerate(blocks):
            if state["sectors"].get(f"{track}/{sector}", {}).get("verified"):
                code, data = dev.iec_block_result(track, sector, device)
                if code != 0 or data != raw[i*256:(i+1)*256]:
                    raise ValueError("inserted disk no longer matches verified checkpoint sectors")
    for i, (track, sector) in enumerate(blocks):
        if cancelled and cancelled():
            state["cancelled"] = True
            _save(journal, state)
            return state
        key = f"{track}/{sector}"
        if state["sectors"].get(key, {}).get("verified"):
            continue
        data = raw[i*256:(i+1)*256]
        entry = state["sectors"].setdefault(key, dict(verified=False, attempts=[]))
        for _ in range(retries+1):
            entry["pending"] = True
            _save(journal, state)  # preserve uncertainty before a potentially interrupted write
            code = dev.iec_block_write(track, sector, data, device)
            read_code, actual = dev.iec_block_result(track, sector, device) if code == 0 else (None, b"")
            entry["attempts"].append(dict(write_dos=code, read_dos=read_code,
                                           match=actual == data))
            entry["pending"] = False
            entry["verified"] = code == 0 and read_code == 0 and actual == data
            _save(journal, state)
            if entry["verified"] or code == 26:
                break
        if not entry["verified"]:
            raise IOError(f"IEC write/readback failed at {track}/{sector}; checkpoint retained")
    state["complete"] = True
    state["cancelled"] = False
    _save(journal, state)
    return state


def _save(path: Path, state: dict) -> None:
    tmp = path.with_name(path.name + ".tmp")
    with tmp.open("w", encoding="utf-8") as f:
        json.dump(state, f, sort_keys=True, indent=2)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def read_image(dev, output: str, kind: str, tracks: int, device: int = 0,
               retries: int = 2, resume: bool = False) -> dict:
    if not 0 <= retries <= 20:
        raise ValueError("retries must be 0..20")
    target = Path(output)
    partial = target.with_name(target.name + ".partial")
    journal = target.with_name(target.name + ".ufi-job.json")
    identity = dict(schema=1, kind=kind, tracks=tracks, device=device)
    blocks = list(cbm.blocks(kind, tracks))
    if resume:
        state = json.loads(journal.read_text(encoding="utf-8"))
        if any(state.get(k) != v for k, v in identity.items()):
            raise ValueError("checkpoint geometry/device does not match this job")
        source = target if state.get("complete") else partial
        # Handle interruption between the final rename and the final checkpoint.
        if not source.exists() and target.exists() and len(state["sectors"]) == len(blocks):
            source = target
        with source.open("rb") as f:
            if os.fstat(f.fileno()).st_size != len(blocks) * cbm.BLOCK:
                raise ValueError("checkpoint image size changed")
            for i, (t, s) in enumerate(blocks):
                entry = state["sectors"].get(f"{t}/{s}")
                if entry and entry["dos"] == 0:
                    f.seek(i * cbm.BLOCK)
                    if hashlib.sha256(f.read(cbm.BLOCK)).hexdigest() != entry["sha256"]:
                        raise ValueError(f"checkpoint data changed at {t}/{s}")
        if source == target:
            if any(state["sectors"].get(f"{t}/{s}", {}).get("dos") != 0 for t, s in blocks):
                raise ValueError("incomplete checkpoint has no partial image")
            state["complete"] = True
            _save(journal, state)
            return state
    else:
        if any(p.exists() for p in (target, partial, journal)):
            raise FileExistsError("output/job already exists; use --resume with the same disk")
        state = dict(identity, complete=False, sectors={})
        with partial.open("xb") as f:
            f.truncate(len(blocks) * cbm.BLOCK)
        _save(journal, state)
    with partial.open("r+b") as f:
        for i, (t, s) in enumerate(blocks):
            key = f"{t}/{s}"
            previous = state["sectors"].get(key, {})
            if previous.get("dos") == 0:
                continue
            history = list(previous.get("attempts", []))
            data = b""
            for _ in range(retries + 1):
                # Bus/protocol errors propagate; all earlier sectors remain checkpointed.
                try:
                    dos, data = dev.iec_block_result(t, s, device)
                except Exception as exc:
                    state["sectors"][key] = dict(dos=None, attempts=history,
                                                  transport_error=f"{type(exc).__name__}: {exc}")
                    _save(journal, state)
                    raise
                history.append(dos)
                if dos == 0:
                    if len(data) != cbm.BLOCK:
                        raise ValueError(f"short successful block at {key}")
                    break
            entry = dict(dos=dos, attempts=history)
            if dos == 0:
                f.seek(i * cbm.BLOCK)
                f.write(data)
                f.flush()
                os.fsync(f.fileno())  # data is durable before its successful checkpoint
                entry["sha256"] = hashlib.sha256(data).hexdigest()
            state["sectors"][key] = entry
            _save(journal, state)
    state["complete"] = all(e["dos"] == 0 for e in state["sectors"].values())
    if state["complete"]:
        os.replace(partial, target)
    _save(journal, state)
    return state
