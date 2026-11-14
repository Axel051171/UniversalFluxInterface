"""The Python host against the production firmware code (sim_v2.c): request -> dispatcher ->
stream encoder -> event queue -> frames -> host parser, with nothing faked in between.

Why: the host's own fake device produces the replies the host expects.  Both sides passed their
tests while the read progress event 0x85 never left the firmware (9 bytes into an 8-byte queue)
and carried wrong values (found 2026-11-14).  Here the firmware's real output is what the host
decodes, and the decoded capture is compared with what the simulated drive delivered.
"""
import sys

from run_tests import FW
import sim_v2

sys.path.insert(0, str(FW.parent / 'software' / 'ufi_host'))
from ufi_host import protocol2 as P2  # noqa: E402


def expected(idx, flux, revs):
    """Revolutions the host must decode: transitions in [index k, index k+1), relative to index k."""
    out = []
    for k in range(revs):
        length = (idx[k + 1] - idx[k]) & 0xFFFFFFFF
        samples = [(t - idx[k]) & 0xFFFFFFFF for t in flux if (t - idx[k]) & 0xFFFFFFFF < length]
        out.append((length, samples))
    return out


def read(exe, revs=3, fragment=0, ticks=2, slow=False, **kw):
    s = sim_v2.SimStream(exe, fragment=fragment, ticks_per_read=ticks)
    try:
        dev = P2.Device2(s)
        dev.enable_events()
        events = []
        if slow:                            # the host does not take data for 30 ms in the middle of the read
            def progress(rev, ticks_, ms):
                events.append((rev, ticks_, ms))
                if rev == 1:
                    s.busy(True)
                    s.tick(30)
                    s.busy(False)
        else:
            def progress(rev, ticks_, ms):
                events.append((rev, ticks_, ms))
        cap = dev.read_track(5, 1, revs, progress=progress, **kw)
        idx, flux = s.truth()
        return cap, events, idx, flux, bytes(s.sent), dev
    finally:
        s.close()


def check(cap, events, idx, flux, revs):
    want = expected(idx, flux, revs)
    assert len(cap.revolutions) == revs, len(cap.revolutions)
    for k, (r, (length, samples)) in enumerate(zip(cap.revolutions, want)):
        assert (r.track, r.side, r.index) == (5, 1, k)
        assert r.index_ticks == length, (k, r.index_ticks, length)
        assert r.samples == samples, (k, len(r.samples), len(samples))
    # the transition the simulated drive put exactly on index 1 opens revolution 1
    assert cap.revolutions[1].samples[0] == 0
    assert sum(len(r.samples) for r in cap.revolutions) > 300 * len(cap.revolutions)
    # one progress event per completed revolution, with that revolution's index-to-index length
    assert [e[0] for e in events] == list(range(1, revs + 1)), events
    assert [e[1] for e in events] == [w[0] for w in want], events
    ms = [e[2] for e in events]
    assert ms == sorted(ms) and ms[0] >= 20 and ms[-1] >= 20 * revs, ms      # 20 ms revolutions, since the start
    return want


def write(exe, deltas, rev_ticks=None, index=True, **kw):
    """One WRITE through Device2 -> (result or DeviceError, written(), state(), wire bytes, rises before)."""
    s = sim_v2.SimStream(exe)
    try:
        dev = P2.Device2(s)
        if rev_ticks:
            s.scenario(rev_ticks=rev_ticks)
        s.index(index)
        before = s.written()['gate_rises']
        try:
            result = dev.write_track(3, 0, deltas, integrity=False, **kw)   # raises on a REPLY or END status != 0
        except P2.DeviceError as e:
            result = e
        s.tick(5)
        return result, s.written(), s.state(), bytes(s.sent), before
    finally:
        s.close()


def check_write(exe):
    """WRITE 0x22 through the production write path (ufi_write.c) against the timer / DMA model."""
    end = bytes([0x55, 0xAA, P2.T_END, P2.WRITE])
    # classic backend, 5 intervals: no ring refill; 4200: the DMA wraps the ring of 2 x 2048 once
    # (HT, TC, then the tail); 9000: two wraps, in a 200 ms revolution (default: 20 ms)
    for n, rev in ((5, None), (4200, None), (9000, 55_000_000)):
        deltas = [1000 + (i * 37) % 200 for i in range(n)]
        result, w, st, raw, before = write(exe, deltas, rev_ticks=rev, extended=False)
        assert isinstance(result, P2.WriteResult) and result.verify_level == 1 and result.clamped == 0, (n, result)
        assert raw.count(end) == 1, n
        # Exact, no tolerance: the firmware packs interval d as ARR = d - 1, the timer period is
        # ARR + 1 ticks, track 3 gets no precompensation, and the model starts the timer at the
        # index time and drops WGATE at the last update without latency.
        assert w['gate_rises'] == before + 1 and w['elapsed'] == n, (n, w['gate_rises'], w['elapsed'])
        # The update interrupt is switched on late (in the DMA handler of the half before the last
        # one).  A pending UIF at that moment calls the handler at once, and on the board every
        # later call then waits one flux period for the DMA counter (found 2026-11-14).
        assert w['timer_irqs'] and w['timer_irqs_stale'] == 0, (n, w['timer_irqs'], w['timer_irqs_stale'])
        # the handler runs for the tail only: at most two ring halves (2 x 2048) plus the closing updates
        assert w['timer_irqs'] <= min(n, 2 * 2048) + 4, (n, w['timer_irqs'])
        assert [p[0] for p in w['periods']] == deltas, n
        assert all(p[1:] == (109, 6) for p in w['periods']), n          # 110-tick pulse, PWM1
        assert (w['gate_off'] - w['gate_on']) & 0xFFFFFFFF == sum(deltas), n
        assert st['write_safe'] and st['write_state'] == 0 and not st['write_pending'], (n, st)
    # extended backend (three-halfword DMA burst through DMAR, ring of 2 x 512 segments): the
    # transition of a period is CCR1 ticks after its start; the gate stays on for the pulse
    deltas = [1000 + (i * 37) % 200 for i in range(1500)]
    result, w, st, raw, before = write(exe, deltas, extended=True)
    assert isinstance(result, P2.WriteResult) and raw.count(end) == 1, result
    assert w['timer_irqs'] and w['timer_irqs_stale'] == 0, (w['timer_irqs'], w['timer_irqs_stale'])
    t, transitions = 0, []
    for period, ccr, mode in w['periods']:
        assert mode == 7, mode
        if ccr < period:
            transitions.append(t + ccr)
        t += period
    want = [sum(deltas[:i + 1]) for i in range(len(deltas))]
    assert transitions == want and w['elapsed'] == len(w['periods']), (len(transitions), transitions[:4], want[:4])
    assert st['write_safe'] and st['write_state'] == 0, st
    # no index pulse (no disk): the firmware gives up after its own 1000 ms, the gate never rose
    result, w, st, raw, before = write(exe, deltas[:50], index=False, extended=False)
    assert isinstance(result, P2.DeviceError) and result.status == 4 and raw.count(end) == 1, result
    assert w['gate_rises'] == before and st['write_safe'] and st['write_state'] == 0, (w['gate_rises'], st)


def main():
    exe, stubs = sim_v2.build()
    check_write(exe)

    # what the host learns about the firmware comes from the firmware's own constants
    s = sim_v2.SimStream(exe)
    dev = P2.Device2(s)
    assert dev.ping()
    masks = dev.flag_masks()
    assert masks['read_flags'] == 0xFF and masks['read_flags2'] == P2.READ2_DOUBLE_STEP, masks
    s.close()

    # READ with progress events, three revolutions
    cap, events, idx, flux, raw, _ = read(exe)
    want = check(cap, events, idx, flux, 3)
    assert raw.count(b'\x55\xAA\x03\x85') >= 3                   # the events really crossed the wire

    # the same request arriving byte by byte and in 7-byte pieces: same capture, same events
    for fragment in (1, 7):
        cap2, events2, idx2, flux2, _, _ = read(exe, fragment=fragment)
        check(cap2, events2, idx2, flux2, 3)
        assert [(r.index_ticks, r.samples) for r in cap2.revolutions] == want

    # host reads rarely (large steps of simulated time between reads): nothing lost
    cap3, events3, idx3, flux3, _, _ = read(exe, ticks=25)
    check(cap3, events3, idx3, flux3, 3)
    # host stops taking data for a while in the middle: the stream waits, nothing lost or doubled
    cap4, events4, idx4, flux4, _, _ = read(exe, slow=True)
    check(cap4, events4, idx4, flux4, 3)

    # combined flags: progress (bit 4) with the extension byte (bit 7, double step) and one revolution
    cap5, events5, idx5, flux5, raw5, _ = read(exe, revs=1, double_step=True)
    assert len(cap5.revolutions) == 1 and cap5.revolutions[0].samples == expected(idx5, flux5, 1)[0][1]
    assert [e[0] for e in events5] == [1]
    # without the flag no event is produced although the mask allows it
    s = sim_v2.SimStream(exe)
    dev = P2.Device2(s)
    dev.enable_events()
    cap6 = dev.read_track(5, 1, 2)
    assert len(cap6.revolutions) == 2 and b'\x55\xAA\x03\x85' not in bytes(s.sent)
    # a refused request: status and the reason travel through the same path
    try:
        dev.read_track(17, 0, 1, quarter=1)      # the simulated drive takes no quarter tracks
        raise AssertionError('quarter track accepted')
    except P2.DeviceError as e:
        assert e.status == 16, e.status
    refusal = dev.last_refusal()
    assert refusal['command'] == P2.READ and refusal['status'] == 16 and refusal['field_name'] == 'track/quarter', refusal
    # and the firmware is ready for the next request
    assert dev.ping() and len(dev.read_track(5, 1, 1).revolutions) == 1
    s.close()
    print(f'host against firmware code: {stubs} stubs; READ decoded = simulated drive ({sum(len(w[1]) for w in want)} '
          'transitions, 3 revolutions), progress events, fragmented requests, slow host, combined flags, refusal passed; '
          'WRITE: intervals on the disk = intervals sent (5, 4200, 9000 classic, 1500 extended), no index = status 4')


if __name__ == '__main__':
    main()
