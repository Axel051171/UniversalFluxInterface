"""Host reference ufi_host/weak.py against the production weak-window code of ufi_analyze.c.

Identical revolutions go into both: the extracted firmware functions (weak_windows,
ufi_analyze_weak_report, weak_positions, ufi_analyze_weak_positions) compiled natively, and the
Python functions.  Every op 42 byte, the ANALYZE weak_mask / weak_count and every op 44 report
(several thresholds) must be identical.  Two separate drive captures could not show this: a weak
area differs from pass to pass.
"""
import os
import random
import re
import subprocess
import sys
from types import SimpleNamespace

from test_regressions import ERRORS, FW, function, run
from run_tests import BUILD

sys.path.insert(0, str(FW.parent / 'software' / 'ufi_host'))
from ufi_host import weak  # noqa: E402

THRESHOLDS = (1, 2, 3, 40, 255)


def rev(index_ticks, samples):
    return SimpleNamespace(index_ticks=index_ticks, samples=samples)


BASE_TICKS = 55_000_000


def pattern(rng, index_ticks=BASE_TICKS, cell=550):
    """The recorded track: transitions every 2..4 cells."""
    out, t = [], rng.randrange(cell)
    while t < index_ticks:
        out.append(t)
        t += cell * rng.choice((2, 3, 4))
    return out


def track(rng, base, index_ticks, jitter=20, weak_from=None, weak_to=None, drop=0.0, base_ticks=BASE_TICKS):
    """One revolution of the recorded track `base` at this revolution's speed, with read jitter;
    inside [weak_from, weak_to) each transition is dropped with probability `drop` (an unstable
    area differs per revolution, the rest of the track does not)."""
    out = []
    for t in base:
        if weak_from is not None and weak_from <= t < weak_to and rng.random() < drop:
            continue
        v = t * index_ticks // base_ticks + rng.randrange(-jitter, jitter + 1)
        if 0 <= v < index_ticks:
            out.append(v)
    return sorted(out)


def cases():
    rng = random.Random(0x5EED)
    c = []
    # 1: stable track, three revolutions of slightly different length (speed variation only)
    base = pattern(rng)
    c.append(('stable', [rev(t, track(rng, base, t)) for t in (55_000_000, 55_011_000, 54_989_500)]))
    # 2: unstable area in the middle, five revolutions
    c.append(('weak area', [rev(t, track(rng, base, t, 20, 20_000_000, 23_000_000, 0.4))
                            for t in (55_000_000, 55_004_000, 54_996_000, 55_001_000, 55_000_500)]))
    # 3: a single revolution: nothing to compare
    c.append(('one revolution', [rev(55_000_000, track(rng, base, 55_000_000))]))
    # 4: no revolution at all
    c.append(('no revolution', []))
    # 5: transitions at and after the index, one empty revolution, one of length 0
    t5 = pattern(rng, 2_000_000)
    c.append(('edges', [rev(2_000_000, t5 + [2_000_000, 2_000_001, 2_500_000]), rev(2_000_000, []),
                        rev(0, [0, 5, 9]), rev(2_000_000, t5[: len(t5) // 2])]))
    # 6: more than 255 transitions in a fine window and a coarse difference above 255
    dense = list(range(0, 1_024_000, 3))
    c.append(('dense', [rev(1_024_000, dense), rev(1_024_000, dense[::400]), rev(1_024_000, dense[: 200_000])]))
    # 7: revolution length that is no multiple of the window counts, tiny lengths
    c.append(('odd lengths', [rev(1023, list(range(0, 1023, 2))), rev(1025, list(range(0, 1025, 3))),
                              rev(63, list(range(63))), rev(65, [0, 64])]))
    # 8: non-monotonic timestamps (never produced by a capture; the scan order is still defined)
    wild = pattern(rng, 3_000_000)
    rng.shuffle(wild)
    c.append(('unsorted', [rev(3_000_000, wild), rev(3_000_000, sorted(wild)), rev(3_000_000, wild[::-1])]))
    # 9: more differing fine windows than one report holds (truncation flag)
    c.append(('truncated', [rev(1_024_000, list(range(0, 1_024_000, 500))), rev(1_024_000, [])]))
    # 10: largest tick values
    big = 0xFFFFFFFF
    c.append(('u32 limits', [rev(big, [0, big // 2, big - 1]), rev(big, [1, big - 2]), rev(big - 1, [big - 1, big])]))
    return c


def expected(revs):
    """What the firmware must print for one case, from the host reference."""
    coarse = weak.weak_windows(revs)
    lines = ['C %d %016x %d %s' % (coarse['revs'], coarse['mask'], coarse['weak_count'],
                                   ''.join('%02x' % v for v in coarse['counts']))]
    for th in THRESHOLDS:
        fine = weak.weak_positions(revs, th)
        lines.append('F %d %d %d %d %d %d %s' % (th, fine['revs'], fine['windows'], fine['threshold'],
                                                  int(fine['truncated']), len(fine['entries']),
                                                  ''.join('%04x%02x%02x' % (e['window'], e['diff'], e['min'])
                                                          for e in fine['entries'])))
    return lines


def main():
    source = (FW / 'src' / 'ufi_analyze.c').read_text(encoding='utf-8')
    consts = {n: int(re.search(r'#define\s+' + n + r'\s+(\d+)u', source).group(1))
              for n in ('WEAK_WINDOWS', 'WEAK_FINE', 'WEAK_LIST')}
    assert (consts['WEAK_WINDOWS'], consts['WEAK_FINE'], consts['WEAK_LIST']) == \
        (weak.COARSE_WINDOWS, weak.FINE_WINDOWS, weak.FINE_LIST), consts
    assert re.search(r'diff >= 3u', source) and weak.COARSE_THRESHOLD == 3

    data = cases()
    code = '#include <stdint.h>\n#include <stdbool.h>\n#include <stdio.h>\n#include <string.h>\n' + ERRORS
    code += ''.join('#define %s %du\n' % kv for kv in consts.items()) + r'''
typedef struct { uint32_t timestamp; } flux_sample_t;
typedef struct { const flux_sample_t* samples; uint32_t count, index_time; } flux_revolution_t;
typedef struct { uint8_t weak_count; uint64_t weak_mask; } analyze_result_t;
static uint8_t weak_diff[WEAK_WINDOWS], weak_revs, weak_track, weak_side;
static uint8_t fine_min[WEAK_FINE], fine_max[WEAK_FINE];
static bool fine_valid;
static const flux_revolution_t* current;
static const flux_revolution_t* ufi_capture_get_data(uint8_t k) { return &current[k]; }
static int analysis_progress(uint32_t since) { (void)since; return UFI_OK; }
'''
    for name in ('weak_windows', 'ufi_analyze_weak_report', 'weak_positions', 'ufi_analyze_weak_positions'):
        code += function('ufi_analyze.c', name)
    table = []
    for ci, (_, revs) in enumerate(data):
        for ri, r in enumerate(revs):
            body = ','.join('{%uu}' % t for t in r.samples) or '{0}'
            code += 'static const flux_sample_t s_%d_%d[] = {%s};\n' % (ci, ri, body)
        rows = ','.join('{s_%d_%d,%du,%uu}' % (ci, ri, len(r.samples), r.index_ticks) for ri, r in enumerate(revs))
        code += 'static const flux_revolution_t c_%d[] = {%s};\n' % (ci, rows or '{0,0,0}')
        table.append('{c_%d,%d}' % (ci, len(revs)))
    code += 'static const struct { const flux_revolution_t* revs; uint8_t n; } all[] = {%s};\n' % ','.join(table)
    code += 'static const uint8_t thresholds[] = {%s};\n' % ','.join(map(str, THRESHOLDS))
    code += r'''
#define P(...) do { if (argc > 1) printf(__VA_ARGS__); } while (0)
int main(int argc, char** argv)
{
    (void)argv;                         /* any argument: print the results for the comparison */
    static uint8_t out[10 + 4 * WEAK_LIST + 8];
    for (unsigned c = 0; c < sizeof all / sizeof all[0]; c++) {
        current = all[c].revs;
        analyze_result_t r;
        memset(&r, 0, sizeof r);
        weak_track = 0; weak_side = 0;
        if (weak_windows(all[c].n, 0, &r) != UFI_OK || weak_positions(all[c].n, 0) != UFI_OK) return 2;
        if (ufi_analyze_weak_report(out) != 4u + WEAK_WINDOWS || out[0] != 1) return 3;
        P("C %u %016llx %u ", out[1], (unsigned long long)r.weak_mask, r.weak_count);
        for (unsigned w = 0; w < WEAK_WINDOWS; w++) P("%02x", out[4 + w]);
        P("\n");
        for (unsigned t = 0; t < sizeof thresholds; t++) {
            memset(out, 0xA5, sizeof out);
            const uint16_t len = ufi_analyze_weak_positions(thresholds[t], out);
            const unsigned n = out[8] | out[9] << 8;
            if (len != 10u + 4u * n || n > WEAK_LIST || out[len] != 0xA5 || out[0] != 1) return 4;
            P("F %u %u %u %u %u %u ", thresholds[t], out[1], out[4] | out[5] << 8, out[6], out[7] & 1u, n);
            for (unsigned i = 0; i < n; i++) {
                const uint8_t* e = out + 10 + 4 * i;
                P("%04x%02x%02x", e[0] | e[1] << 8, e[2], e[3]);
            }
            P("\n");
        }
    }
    return 0;
}
'''
    run('regression_weak_reference', code)
    exe = BUILD / ('regression_weak_reference' + ('.exe' if os.name == 'nt' else ''))
    got = subprocess.run([str(exe), 'print'], check=True, capture_output=True, text=True, timeout=120).stdout.split('\n')
    got = [g.rstrip() for g in got if g.strip()]
    per = 1 + len(THRESHOLDS)
    assert len(got) == per * len(data), (len(got), per * len(data))
    seen = {'weak': False, 'truncated': False, 'capped': False, 'none': False}
    for ci, (name, revs) in enumerate(data):
        want = expected(revs)
        have = got[ci * per:(ci + 1) * per]
        for w, h in zip(want, have):
            assert w.rstrip() == h, 'case %r differs:\n host     %s\n firmware %s' % (name, w[:200], h[:200])
        coarse = weak.weak_windows(revs)
        seen['weak'] |= coarse['weak_count'] > 0
        seen['capped'] |= 255 in coarse['counts']
        seen['none'] |= coarse['revs'] == 0
        seen['truncated'] |= weak.weak_positions(revs, 1)['truncated']
    # the comparison is only worth something if the data reached every branch
    assert all(seen.values()), seen
    # ... and if it can fail: changed input must change the host result against the firmware lines
    name, revs = data[1]
    changed = [rev(r.index_ticks, list(r.samples)) for r in revs]
    w0 = next(e['window'] for e in weak.weak_positions(revs, 1, None)['entries'] if e['min'] > 0)
    a, b = weak.window_ticks(changed[0].index_ticks, w0)
    for r in changed:                   # empty that fine window in every revolution: its entry must go
        r.samples[:] = [t for t in r.samples if not a - 40 <= t < b + 40]
    assert expected(changed) != got[per:2 * per], 'comparison cannot fail'
    stable = weak.weak_windows(data[0][1])
    area = weak.weak_windows(data[1][1])
    assert stable['weak_count'] == 0 and area['weak_count'] > 0
    hit = [w for w in range(64) if area['mask'] >> w & 1]
    assert min(hit) >= 20_000_000 * 64 // 55_004_000 - 1 and max(hit) <= 23_000_000 * 64 // 54_996_000 + 1, hit
    print('weak reference: host == firmware on %d cases, op 42 bytes, weak_mask and op 44 at thresholds %s passed'
          % (len(data), list(THRESHOLDS)))


if __name__ == '__main__':
    main()
