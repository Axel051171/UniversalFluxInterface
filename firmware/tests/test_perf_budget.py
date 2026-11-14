"""Budgets of the read path as regressions (review item 4 of 2026-11-14), on the firmware
simulator and the host decoder.  The simulator is deterministic, so its numbers are exact and a
change shows as a change, not as noise; host times are compared with the old two-pass decoder
on the same machine, never with a wall-clock limit.

1. Stream size: bytes of flux code per transition for the fixed capture.
2. Latency: simulated milliseconds from the last index pulse of the capture to the END frame.
3. Framing: DATA frames used for the capture.
4. Dropped events: with 16 revolutions far shorter than a main-loop pass the 3-event queue must
   overflow; every revolution's progress event is either delivered or counted as dropped with
   the cause "queue full", none vanishes and none is counted as "too long".
The host decoder's memory and time against its two-pass reference are budgeted in the host
suite (software/ufi_host/tests/test_decode_stream.py).

Not measured here: anything on the board (DIAGNOSTICS 0x0D sub 01/02 hold the run-time and
buffer high-water marks there; they need a drive and a host under load).
"""
import sys

from run_tests import FW
import sim_v2

sys.path.insert(0, str(FW.parent / 'software' / 'ufi_host'))
from ufi_host import protocol2 as P2  # noqa: E402

# Budgets.  The first three are what the code does today plus a small margin; lowering a number
# after an improvement is welcome, raising one needs a reason in the commit.
MAX_BYTES_PER_TRANSITION = 3.20     # measured 3.143: a third of the simulator's intervals need the 5-byte code
MAX_END_LATENCY_MS = 6              # measured 4
MAX_DATA_FRAMES = 3                 # measured 2


def run_read(exe, revs, **scenario):
    s = sim_v2.SimStream(exe, ticks_per_read=1)
    dev = P2.Device2(s)
    status = dev.enable_events()
    assert status and status['queue_events'] == 3 and status['max_payload'] >= 9, status
    if scenario:
        s.scenario(**scenario)
    events = []
    cap = dev.read_track(5, 1, revs, progress=lambda r, t, ms: events.append((r, t, ms)))
    for ev in list(dev.events):                         # progress events that arrived around the END
        if ev.code == P2.EV_READ_PROGRESS:
            events.append((ev.payload[0],))
    after = dev.events_status()
    idx, flux = s.truth()
    raw = bytes(s.sent)
    s.close()
    return cap, events, status, after, idx, flux, raw


def main():
    exe, _ = sim_v2.build()

    # 1-3: the fixed capture of the simulator, three revolutions of 20 ms
    cap, events, before, after, idx, flux, raw = run_read(exe, 3)
    frames = P2.FrameParser().feed(raw)
    data = [f for f in frames if f.type == 4 and f.cmd == P2.READ]
    stream_bytes = sum(len(f.payload) for f in data)
    transitions = sum(len(r.samples) for r in cap.revolutions)
    per = stream_bytes / transitions
    latency = events[-1][2] - (idx[3] - idx[0]) // 275000 if len(events) == 3 else None
    measured = f'{per:.3f} bytes/transition, {len(data)} DATA frames, stream lag {latency} ms'
    assert len(events) == 3 and after['dropped_queue_full'] == before['dropped_queue_full'] == 0, measured
    assert after['dropped_too_long'] == 0
    assert per <= MAX_BYTES_PER_TRANSITION, measured
    assert len(data) <= MAX_DATA_FRAMES, measured
    # the last progress event is stamped when the streamer handled the last index: its distance
    # to the capture's own length is how far the stream lags behind the drive
    assert latency is not None and 0 <= latency <= MAX_END_LATENCY_MS, measured

    # 4: 16 revolutions of 0.36 ms (the simulator models up to 16): several indexes per main-loop
    # pass, while one event leaves per pass
    revs = 16
    cap2, events2, _, after2, _, _, raw2 = run_read(exe, revs, rev_ticks=100_000, cell_ticks=1100, lead_ms=2)
    assert len(cap2.revolutions) == revs
    delivered = raw2.count(b'\x55\xAA\x03\x85')
    assert after2['dropped_too_long'] == 0
    assert after2['dropped_queue_full'] > 0, 'the scenario no longer overflows the queue'
    assert delivered + after2['dropped_queue_full'] == revs, (delivered, after2)
    # what was delivered is in order and carries the right revolution numbers
    got = [e[0] for e in events2]
    assert got == sorted(got) and len(set(got)) == len(got) and set(got) <= set(range(1, revs + 1))

    print(f'read path budgets: {per:.3f} bytes/transition (<= {MAX_BYTES_PER_TRANSITION}), {len(data)} DATA frames '
          f'(<= {MAX_DATA_FRAMES}), stream lag {latency} ms (<= {MAX_END_LATENCY_MS}); queue overflow: {delivered} '
          f'delivered + {after2["dropped_queue_full"]} counted as dropped = {revs} passed')


if __name__ == '__main__':
    main()
