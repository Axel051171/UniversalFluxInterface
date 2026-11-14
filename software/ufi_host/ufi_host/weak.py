"""Instability of a READ capture, computed on the host from the revolutions the READ delivered.

Reference for docs/USB_Protokoll.md 4.13a.  The numbers are defined exactly like the firmware's
op 42 (4.11) and op 44 (4.13) in firmware/src/ufi_analyze.c (weak_windows, weak_positions,
ufi_analyze_weak_positions); firmware/tests/test_weak_reference.py feeds identical revolutions to
both and requires byte-identical results.

Why on the host: ops 42/44 belong to the last ANALYZE, which reads the track again.  A weak area
gives different flux on every pass, so a second read does not describe the stream a READ already
delivered.  Nothing is read from the drive here and the flux stream is not changed.

Input: revolutions with `index_ticks` (revolution length in flux-timer ticks) and `samples`
(transition times in ticks since that revolution's index), as protocol.Revolution carries them.

Definition, T = index_ticks of the revolution, all divisions are integer divisions:
- coarse window w (0..63) covers [T*w/64, T*(w+1)/64); fine window w (0..1023) covers
  [T*w/1024, T*(w+1)/1024), the last fine window ends at T.
- a transition at or after T is not counted; revolutions are aligned by their own index only
  (each scaled by its own T), not by content.
- per window: count the transitions of each revolution, then maximum - minimum over the revolutions.
  Coarse differences cap at 255 after the subtraction; fine counts cap at 255 before it.
- fewer than two revolutions: nothing to compare (revs 0, all zero, no entries).
- a revolution that is empty or has T = 0 counts 0 in every window; it is not skipped.

A difference is a hint that the flux was unstable there.  It is not a bit position (one fine window
at 500 kbit/s MFM holds about 25 bit cells) and by itself no proof of copy protection: a marginal
medium, dirt or drive speed variation produce the same picture.
"""
from __future__ import annotations

from bisect import bisect_left
from typing import Any, Iterable, Sequence

COARSE_WINDOWS = 64         # WEAK_WINDOWS in ufi_analyze.c
FINE_WINDOWS = 1024         # WEAK_FINE
FINE_LIST = 64              # WEAK_LIST: entries of one op 44 report
COARSE_THRESHOLD = 3        # fixed threshold of weak_mask / weak_count in the ANALYZE reply
U32 = 0xFFFFFFFF


def _revs(revolutions: Iterable[Any]) -> list[tuple[int, Sequence[int]]]:
    out = []
    for r in revolutions:
        t, s = int(r.index_ticks), r.samples
        if not 0 <= t <= U32 or any(not 0 <= x <= U32 for x in s):
            raise ValueError("index_ticks and samples must be unsigned 32-bit tick values")
        out.append((t, s))
    if len(out) > 255:
        raise ValueError("at most 255 revolutions (the firmware counts them in a byte)")
    return out


def _count(samples: Sequence[int], ascending: bool, a: int, b: int) -> int:
    """Transitions in [a, b), as the firmware scans them: from the first sample, stop at the first
    one at or after b.  For ascending samples that is the plain interval count."""
    if ascending:
        return bisect_left(samples, b) - bisect_left(samples, a)
    cnt = 0
    for t in samples:
        if a <= t < b:
            cnt += 1
        if t >= b:
            break
    return cnt


def weak_windows(revolutions: Iterable[Any]) -> dict:
    """Op 42 numbers for these revolutions: {'revs', 'counts': 64 x (max - min), 'mask', 'weak_count'}.
    mask bit w and weak_count use the firmware's fixed threshold 3 (ANALYZE weak_mask / weak_count)."""
    revs = _revs(revolutions)
    counts = [0] * COARSE_WINDOWS
    if len(revs) < 2:
        return {"revs": 0, "counts": counts, "mask": 0, "weak_count": 0}
    order = [all(s[i] <= s[i + 1] for i in range(len(s) - 1)) for _, s in revs]
    mask = 0
    for w in range(COARSE_WINDOWS):
        per_rev = [_count(s, asc, t * w // COARSE_WINDOWS, t * (w + 1) // COARSE_WINDOWS)
                   for (t, s), asc in zip(revs, order, strict=True)]
        diff = max(per_rev) - min(per_rev)
        counts[w] = min(diff, 255)
        if diff >= COARSE_THRESHOLD:
            mask |= 1 << w
    return {"revs": len(revs), "counts": counts, "mask": mask, "weak_count": bin(mask).count("1")}


def fine_min_max(revolutions: Iterable[Any]) -> tuple[list[int], list[int]] | None:
    """Per 1/1024 revolution: (minimum, maximum) transition count over the revolutions, counts capped
    at 255; None with fewer than two revolutions.  One sweep per revolution with a window cursor that
    only moves forward, exactly like weak_positions() in the firmware."""
    revs = _revs(revolutions)
    if len(revs) < 2:
        return None
    lo, hi = [255] * FINE_WINDOWS, [0] * FINE_WINDOWS
    for t_index, samples in revs:
        w = cnt = 0
        b = t_index // FINE_WINDOWS
        for i in range(len(samples) + 1):
            t = samples[i] if i < len(samples) else U32        # the sentinel closes the open windows
            while t >= b and w < FINE_WINDOWS:
                c = min(cnt, 255)
                lo[w] = min(lo[w], c)
                hi[w] = max(hi[w], c)
                cnt = 0
                w += 1
                b = t_index * (w + 1) // FINE_WINDOWS if w + 1 < FINE_WINDOWS else t_index
            if w >= FINE_WINDOWS:
                break
            cnt += 1
    return lo, hi


def weak_positions(revolutions: Iterable[Any], threshold: int = 2, limit: int | None = FINE_LIST) -> dict:
    """Op 44 numbers for these revolutions: the fine windows whose max - min reaches `threshold`
    (1..255), ascending, as {'window', 'diff', 'min'}.  limit = 64 reproduces one firmware report
    ('truncated' when more exist); limit=None lists all of them, which the device cannot."""
    if not 1 <= threshold <= 255:
        raise ValueError("threshold 1..255")
    revs = list(revolutions)
    mm = fine_min_max(revs)
    entries: list[dict] = []
    truncated = False
    if mm:
        for w, (lo, hi) in enumerate(zip(*mm, strict=True)):
            if hi - lo < threshold:
                continue
            if limit is not None and len(entries) == limit:
                truncated = True
                break
            entries.append({"window": w, "diff": hi - lo, "min": lo})
    return {"revs": len(revs) if mm else 0, "windows": FINE_WINDOWS, "threshold": threshold,
            "truncated": truncated, "entries": entries}


def window_ticks(index_ticks: int, window: int, windows: int = FINE_WINDOWS) -> tuple[int, int]:
    """[start, end) of a window in ticks after the index of a revolution of length index_ticks."""
    if not 0 <= window < windows:
        raise ValueError("window out of range")
    end = index_ticks if window + 1 == windows else index_ticks * (window + 1) // windows
    return index_ticks * window // windows, end


def capture_instability(cap: Any, threshold: int = 2, limit: int | None = None) -> dict:
    """Both reports for one READ capture (protocol.Capture), tied to that capture and kept apart
    from its flux: the capture itself is not modified.

    capture_id / media_generation: from the END generation block, None on older firmware.
    complete: False when the capture ended with a status, a revolution overflowed or the read
        context lists a loss window; the numbers then describe what arrived, not the track.
    index_physical: False when the revolutions were cut by the index simulator or in time slices
        (ReadInfo.index_physical); windows are then no disk position and revolutions are not aligned,
        so differences mean nothing.  None when the read context was not requested.
    """
    revs = list(cap.revolutions)
    gen = getattr(cap, "generations", None) or {}
    ri = getattr(cap, "read_info", None)
    complete = not getattr(cap, "status", 0) and not any(getattr(r, "overflow", False) for r in revs) \
        and not (ri is not None and (ri.loss_total or ri.losses))
    return {
        "capture_id": gen.get("capture_id"),
        "media_generation": gen.get("media_generation_end"),
        "track": revs[0].track if revs else None,
        "side": revs[0].side if revs else None,
        "index_ticks": [int(r.index_ticks) for r in revs],
        "complete": bool(complete),
        "index_physical": None if ri is None else ri.index_physical,
        "coarse": weak_windows(revs),
        "fine": weak_positions(revs, threshold, limit),
    }
