"""Amiga trackdisk decoding and bounded, non-destructive track quality analysis.

Own implementation from the documented disk layout, not ADF-Drive source.
Times are native UFI ticks. A checksum-valid sector is not proof of stable media.
"""
from dataclasses import dataclass, field
from functools import reduce
from operator import xor

from .hfe import Pll

MASK = 0x55555555
SYNC = bytes(int(c) for c in f'{0x44894489:032b}')
MAX_CELLS = 8_000_000
SEEDS = (1000, 960, 1040)  # bounded initial PLL periods, permille
GAINS = {1000: (16, 2), 960: (8, 2), 1040: (32, 4)}


@dataclass(frozen=True)
class Sector:
    label: bytes
    data: bytes
    to_gap: int


@dataclass
class Decode:
    sectors: dict = field(default_factory=dict)  # (track, zero-based sector) -> set[Sector]
    header_errors: int = 0
    data_errors: int = 0
    invalid_headers: int = 0
    truncated: int = 0
    bad_sectors: set = field(default_factory=set)


def cells(deltas, rate, seed=1000):
    if rate not in (250_000, 300_000, 500_000):
        raise ValueError('unsupported Amiga bit rate')
    if seed not in GAINS:
        raise ValueError('unsupported Amiga PLL profile')
    period_divisor, phase_divisor = GAINS[seed]
    pll = Pll(rate, period_divisor=period_divisor, phase_divisor=phase_divisor)
    pll.period = pll.nominal * seed // 1000
    bits = bytearray()
    for delta in deltas:
        if not isinstance(delta, int) or not 0 < delta <= 0xffffffff:
            raise ValueError('invalid flux interval for Amiga decoding')
        count = min(pll.cells(delta), 128)
        if len(bits) + count > MAX_CELLS:
            raise ValueError('track too large for bounded Amiga analysis')
        bits.extend(bytes(count - 1))
        bits.append(1)
    return bits


def decode(deltas, rate, seed=1000, *, prefix_intervals=None):
    bits = cells(deltas, rate, seed)
    # When extending a revolution with its actual successor, count only syncs
    # starting in the first revolution. Never wrap the last capture to the first.
    limit = len(bits) if prefix_intervals is None else len(cells(deltas[:prefix_intervals], rate, seed))
    out = Decode()
    cursor = 0
    def join(a, b):
        return ((a & MASK) << 1) | (b & MASK)
    while True:
        start = bits.find(SYNC, cursor)
        if start < 0 or start >= limit:
            break
        cursor = start + len(SYNC)
        if cursor + 270 * 32 > len(bits):
            out.truncated += 1
            continue
        raw = []
        for pos in range(cursor, cursor + 270 * 32, 32):
            word = 0
            for bit in bits[pos:pos + 32]:
                word = (word << 1) | bit
            raw.append(word)
        if (reduce(xor, raw[:10], 0) & MASK) != join(raw[10], raw[11]):
            out.header_errors += 1
            continue
        info = join(raw[0], raw[1])
        track, sector, to_gap = (info >> 16) & 255, (info >> 8) & 255, info & 255
        if info >> 24 != 255 or sector >= 22 or not 1 <= to_gap <= 22:
            out.invalid_headers += 1
            continue
        key = (track, sector)
        if (reduce(xor, raw[14:], 0) & MASK) != join(raw[12], raw[13]):
            out.data_errors += 1
            out.bad_sectors.add(key)
            continue
        label = b''.join(join(raw[2+i], raw[6+i]).to_bytes(4, 'big') for i in range(4))
        data = b''.join(join(raw[14+i], raw[142+i]).to_bytes(4, 'big') for i in range(128))
        out.sectors.setdefault(key, set()).add(Sector(label, data, to_gap))
        cursor += 270 * 32
    return out


def unique(result):
    if any(len(values) != 1 for values in result.sectors.values()):
        raise ValueError('conflicting duplicate Amiga sector; verification is ambiguous')
    return {key: next(iter(values)) for key, values in result.sectors.items()}


def deltas_of(rev):
    previous = 0
    out = []
    duration = getattr(rev, 'index_ticks', 0)
    for timestamp in rev.samples:
        if not isinstance(timestamp, int) or timestamp <= previous or (duration and timestamp > duration):
            raise ValueError('invalid capture timestamps')
        out.append(timestamp - previous)
        previous = timestamp
    return out


def analyze_capture(capture, rate, track, spt=11, *, adaptive=True):
    """Return JSON-safe evidence plus unambiguous sectors; never rewrite raw samples.

    Three PLL seeds maximum per revolution. Conflicts from any seed/revolution
    remain conflicts, even if a majority agrees. 'stable' requires >=2 observations.
    """
    if spt not in (11, 22) or not 0 <= track <= 255:
        raise ValueError('invalid Amiga geometry')
    if not 1 <= len(capture.revolutions) <= 200:
        raise ValueError('Amiga analysis requires 1..200 revolutions')
    revs = capture.revolutions
    intervals = [deltas_of(rev) for rev in revs]
    variants, seen, nominal_seen, bad = {}, {}, {}, set()
    attempts = []
    hist = [0] * 128  # 0.125 us bins; last bin includes all longer intervals
    for seq, base in enumerate(intervals):
        for delta in base:
            hist[min(127, delta * 8_000_000 // 275_000_000)] += 1
        extended = base
        prefix = None
        duration = getattr(revs[seq], 'index_ticks', 0)
        if base and duration and seq + 1 < len(revs) and intervals[seq + 1]:
            bridge = duration - revs[seq].samples[-1] + intervals[seq + 1][0]
            extended = base + [bridge] + intervals[seq + 1][1:]
            prefix = len(base)
        row = {'revolution': seq, 'attempts': []}
        for seed in SEEDS if adaptive else SEEDS[:1]:
            result = decode(extended, rate, seed, prefix_intervals=prefix)
            row['attempts'].append({'pll_seed_permille': seed,
                                    'period_divisor': GAINS[seed][0],
                                    'phase_divisor': GAINS[seed][1],
                                    'valid_sectors': len(result.sectors),
                                    'header_errors': result.header_errors,
                                    'data_errors': result.data_errors,
                                    'invalid_headers': result.invalid_headers,
                                    'truncated': result.truncated})
            bad.update(result.bad_sectors)
            for key, values in result.sectors.items():
                variants.setdefault(key, set()).update(values)
                seen.setdefault(key, set()).add(seq)
                if seed == 1000:
                    nominal_seen.setdefault(key, set()).add(seq)
        attempts.append(row)
    entries = []
    for sector in range(spt):
        key = (track, sector)
        values = variants.get(key, set())
        observed = len(seen.get(key, ()))
        nominal = len(nominal_seen.get(key, ()))
        state = ('conflicting' if len(values) > 1 else 'missing' if not values
                 else 'recovered' if nominal < observed
                 else 'intermittent' if key in bad or observed < len(revs)
                 else 'stable' if observed >= 2 else 'observed_once')
        entries.append({'sector': sector, 'state': state, 'valid_variants': len(values),
                        'observed_revolutions': observed, 'nominal_revolutions': nominal,
                        'data_checksum_error': key in bad})
    expected_keys = {(track, s) for s in range(spt)}
    conflicting = [list(key) for key, values in sorted(variants.items()) if len(values) > 1]
    unambiguous = {key: next(iter(values)) for key, values in variants.items() if len(values) == 1}
    overflow = any(getattr(rev, 'flags', 0) & 2 for rev in revs)
    complete = (not capture.status and not overflow and not conflicting
                and expected_keys == unambiguous.keys())
    report = {'schema': 1, 'format': 'amiga', 'rate_bps': rate, 'track': track,
              'sectors_per_track': spt, 'capture_status': capture.status,
              'capture_overflow': overflow,
              'complete': complete, 'conflicting_sectors': conflicting,
              'unexpected_sectors': [list(k) for k in sorted(variants.keys() - expected_keys)],
              'sectors': entries, 'revolutions': attempts,
              'histogram': {'bin_ns': 125, 'last_bin_overflow': True, 'counts': hist},
              'interpretation': 'Observed decode reliability, not proof of intentional weak bits.'}
    return report, unambiguous
