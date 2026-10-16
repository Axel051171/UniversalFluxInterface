"""Commodore disk image geometry for the IEC block commands (firmware 1.16).

D64 (1541, 35 or 40 tracks), D71 (1571, 70 tracks, both sides) and D81 (1581, 80 tracks of
40 sectors).  Blocks are 256 bytes, tracks count from 1, sectors from 0; the images store
them track by track in sector order.
"""
from __future__ import annotations

from typing import Iterator

BLOCK = 256
# image size (without the optional error-info tail) -> (kind, tracks)
SIZES = {
    174848: ("d64", 35),
    196608: ("d64", 40),
    349696: ("d71", 70),
    819200: ("d81", 80),
}
# D64 images may carry one error byte per block after the data
ERROR_INFO_SIZES = {174848 + 683: ("d64", 35), 196608 + 768: ("d64", 40), 349696 + 1366: ("d71", 70)}
DOS_ERRORS = {
    0: "ok", 1: "files scratched", 20: "read error (header not found)", 21: "read error (no sync)",
    22: "read error (data block not present)", 23: "read error (checksum)", 24: "read error (decode)",
    25: "write error (verify)", 26: "write protect on", 27: "read error (header checksum)",
    28: "write error (long data block)", 29: "disk id mismatch", 30: "syntax error",
    31: "syntax error (invalid command)", 32: "syntax error (long line)", 33: "syntax error (filename)",
    34: "syntax error (no file given)", 39: "file not found", 50: "record not present",
    60: "write file open", 61: "file not open", 62: "file not found", 63: "file exists",
    64: "file type mismatch", 65: "no block", 66: "illegal track or sector", 67: "illegal system t or s",
    70: "no channel", 71: "directory error", 72: "disk full", 73: "dos version", 74: "drive not ready",
}


def sectors_per_track(kind: str, track: int) -> int:
    """1541 zones (tracks 36-40 of a 40-track D64: 17), D71 tracks 36-70 repeat the first
    side, 1581 = 40 throughout."""
    if kind == "d81":
        return 40
    t = track - 35 if (kind == "d71" and track > 35) else track
    return 21 if t <= 17 else 19 if t <= 24 else 18 if t <= 30 else 17


def geometry(size: int) -> tuple[str, int]:
    """(kind, tracks) for an image file size; ValueError for unknown sizes."""
    if size in SIZES:
        return SIZES[size]
    if size in ERROR_INFO_SIZES:
        return ERROR_INFO_SIZES[size]
    raise ValueError(f"unknown Commodore image size {size} (D64 174848/196608, D71 349696, D81 819200)")


def image_size(kind: str, tracks: int) -> int:
    return sum(sectors_per_track(kind, t) for t in range(1, tracks + 1)) * BLOCK


def blocks(kind: str, tracks: int) -> Iterator[tuple[int, int]]:
    """(track, sector) in image order."""
    for t in range(1, tracks + 1):
        for s in range(sectors_per_track(kind, t)):
            yield t, s


def kind_for(type_name: str, tracks: int) -> tuple[str, int]:
    """--type 1541|1571|1581 and --tracks -> (kind, tracks) for reading."""
    if type_name == "1581" and tracks in (0, 80):
        return "d81", 80
    if type_name == "1571":
        if tracks in (0, 70):
            return "d71", 70
        if tracks in (35, 40):
            return "d64", tracks
    if type_name in ("1541", "1541-II", "1541C") and tracks in (0, 35, 40):
        return "d64", tracks or 35
    raise ValueError(f"unsupported geometry: {type_name}, {tracks} tracks")
