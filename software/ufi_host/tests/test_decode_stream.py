"""decode_stream_lead() builds the revolutions while decoding (2026-11-14, peak memory).  It must
return exactly what the two-pass decoder returned - results and error texts - and it must use
less memory."""
from __future__ import annotations

import random
import tracemalloc

import pytest

from ufi_host import protocol as P


def reference(data: bytes, track: int = 0, side: int = 0, flags: int = 0x01):
    """The decoder as it was until 2026-11-14: all absolute times first, then cut at the indexes."""
    t, flux, index = 0, [], [0]
    lead = None
    in_lead = False
    i, n = 0, len(data)
    if n >= 1 and data[0] == 0xFF:
        if n < 6 or data[1] != 0x01:
            raise ValueError("bad or truncated lead-in header")
        ticks = int.from_bytes(data[2:6], "little")
        lead, in_lead, t, i = P.LeadIn(ticks, []), True, -ticks, 6
    while i < n:
        b = data[i]
        sink = lead.samples if in_lead else flux
        if 0x01 <= b <= 0xEF:
            t += b
            sink.append(t)
            i += 1
        elif 0xF0 <= b <= 0xFC:
            if i + 2 > n:
                raise ValueError("truncated two-byte flux interval")
            t += 240 + ((b - 0xF0) << 8) + data[i + 1]
            sink.append(t)
            i += 2
        elif b in (0xFD, 0xFE):
            if i + 5 > n:
                raise ValueError("truncated flux timestamp")
            v = int.from_bytes(data[i + 1:i + 5], "little")
            if b == 0xFD:
                if in_lead:
                    raise ValueError("index mark inside the lead-in")
                if t + v <= index[-1]:
                    raise ValueError("non-increasing index timestamp")
                index.append(t + v)
            else:
                if v == 0:
                    raise ValueError("zero flux interval")
                t += v
                sink.append(t)
            i += 5
        elif b == 0xFF and in_lead:
            if i + 6 > n or data[i + 1] != 0x02:
                raise ValueError(f"bad or truncated lead-in end at {i}")
            if t + int.from_bytes(data[i + 2:i + 6], "little") != 0:
                raise ValueError("lead-in end does not meet the announced length")
            in_lead = False
            i += 6
        else:
            raise ValueError(f"bad stream byte 0x{b:02X} at {i}")
        if in_lead and t >= 0:
            raise ValueError("lead-in transition at or after index 0")
    if in_lead:
        raise ValueError("lead-in without index 0")
    revs, j = [], 0
    for r in range(len(index) - 1):
        start, end = index[r], index[r + 1]
        samples = []
        while j < len(flux) and flux[j] < end:
            samples.append(flux[j] - start)
            j += 1
        revs.append(P.Revolution(track, side, r, flags, end - start, samples))
    return lead, revs


def delta(d: int) -> bytes:
    if d <= 0xEF:
        return bytes([d])
    if d < 240 + 13 * 256:
        return bytes([0xF0 + ((d - 240) >> 8), (d - 240) & 0xFF])
    return b"\xFE" + d.to_bytes(4, "little")


def stream(rng: random.Random, transitions: int, revolutions: int, late_marks: bool, lead: int = 0) -> bytes:
    """Random stream: transitions with all three interval encodings; index marks at random
    places, with late_marks sometimes only after later transitions were already sent."""
    out = bytearray()
    t = 0
    cursor = 0                                      # time of the last transition sent: intervals count from it
    if lead:
        out += b"\xFF\x01" + lead.to_bytes(4, "little")
        t = -lead
        while True:
            d = rng.choice((rng.randint(1, 239), rng.randint(240, 3567), rng.randint(3568, 20000)))
            if t + d >= 0:
                break
            t += d
            out += delta(d)
        out += b"\xFF\x02" + (-t).to_bytes(4, "little")
        cursor = t                                  # the index 0 mark does not move the cursor
        t = 0
    times = []
    for _ in range(transitions):
        d = rng.choice((rng.randint(1, 239), rng.randint(240, 3567), rng.randint(3568, 90000)))
        t += d
        times.append(t)
    marks = sorted(rng.sample(range(times[0], times[-1] + 500), revolutions))
    marks = [m for k, m in enumerate(marks) if m > 0 and (not k or m > marks[k - 1])]
    last, mi, sent = cursor, 0, 0
    for at in times:
        # an index at or before this transition goes out first - unless it comes late
        while mi < len(marks) and marks[mi] <= at and not (late_marks and rng.random() < 0.3 and marks[mi] > last):
            if marks[mi] > last or sent == 0:
                out += b"\xFD" + (marks[mi] - last).to_bytes(4, "little") if marks[mi] >= last else b""
            mi += 1
        out += delta(at - last)
        last = at
        sent += 1
        while late_marks and mi < len(marks) and marks[mi] <= last - 1 and rng.random() < 0.5:
            mi += 1                                 # dropped: cannot be expressed behind the cursor
    while mi < len(marks):
        if marks[mi] > last:
            out += b"\xFD" + (marks[mi] - last).to_bytes(4, "little")
        mi += 1
    return bytes(out)


def same(data: bytes):
    want = reference(data, 5, 1, 3)
    got = P.decode_stream_lead(data, 5, 1, 3)
    assert got == want
    return got


def test_same_result_as_the_two_pass_decoder():
    rng = random.Random(20261114)
    for case in range(300):
        late = case % 3 == 2
        data = stream(rng, rng.randint(1, 400), rng.randint(1, 6), late, lead=rng.choice((0, 0, 5000, 5_500_000)))
        lead, revs = same(data)
        assert [r.index for r in revs] == list(range(len(revs)))
        assert all(0 <= s < r.index_ticks for r in revs for s in r.samples)
    # An index mark lies at the cursor or behind it (offset >= 0), so the only transition that can
    # precede its own index in the stream is one exactly on it: mark with offset 0 after the
    # transition.  It belongs to the next revolution, as with the mark sent first.
    data = delta(100) + delta(100) + delta(100)                  # times 100, 200, 300, cursor at 300
    _, revs = same(data + b"\xFD" + (0).to_bytes(4, "little") + delta(40) + b"\xFD" + (60).to_bytes(4, "little"))
    assert [r.samples for r in revs] == [[100, 200], [0, 40]] and [r.index_ticks for r in revs] == [300, 100]
    _, revs = same(data + b"\xFD" + (50).to_bytes(4, "little") + delta(100) + b"\xFD" + (300).to_bytes(4, "little"))
    assert [r.samples for r in revs] == [[100, 200, 300], [50]] and [r.index_ticks for r in revs] == [350, 350]
    # exactly on the index: next revolution; transitions after the last index are dropped
    _, revs = same(delta(100) + b"\xFD" + (100).to_bytes(4, "little") + delta(100) + delta(50) +
                   b"\xFD" + (10).to_bytes(4, "little") + delta(70))
    assert [r.samples for r in revs] == [[100], [0, 50]] and [r.index_ticks for r in revs] == [200, 60]
    assert same(b"") == (None, []) and same(delta(5)) == (None, [])


def test_index_mark_ahead_of_later_transitions():
    """A mark gives a time at or ahead of the cursor and does not move it: transitions that follow
    it in the stream but lie before that index still belong to the open revolution.  The first
    one-pass version got this wrong; the existing fuzz test in test_protocol.py caught it."""
    data = delta(100) + b"\xFD" + (400).to_bytes(4, "little") + delta(100) + delta(100) + delta(300) + delta(10) + \
        b"\xFD" + (90).to_bytes(4, "little")
    _, revs = same(data)                     # times 100, [index 500], 200, 300, 600, 610, [index 700]
    assert [r.samples for r in revs] == [[100, 200, 300], [100, 110]] and [r.index_ticks for r in revs] == [500, 200]
    # two marks ahead, the second never reached by a transition: closed at the end, second revolution empty
    data = delta(100) + b"\xFD" + (50).to_bytes(4, "little") + b"\xFD" + (900).to_bytes(4, "little") + delta(20)
    _, revs = same(data)
    assert [r.samples for r in revs] == [[100, 120], []] and [r.index_ticks for r in revs] == [150, 850]
    # one transition jumps over two indexes: an empty revolution in between
    data = b"\xFD" + (10).to_bytes(4, "little") + b"\xFD" + (20).to_bytes(4, "little") + delta(25) + \
        b"\xFD" + (5).to_bytes(4, "little")
    _, revs = same(data)
    assert [r.samples for r in revs] == [[], [], [5]] and [r.index_ticks for r in revs] == [10, 10, 10]


def test_mutated_streams_match_the_two_pass_decoder():
    """Byte-level mutations of valid streams and random byte strings: the same result or the same
    error text, 20 000 inputs."""
    from test_protocol import encode_lead_stream, encode_stream
    rng = random.Random(4250)
    alphabet = [0x00, 0x01, 0x7F, 0xEF, 0xF0, 0xFC, 0xFD, 0xFE, 0xFF, 0x02, 0x03]
    seeds = [encode_lead_stream(1000, [-999, -1], [(5000, [0, 4999])]), encode_stream([(5000, [1, 2])]),
             encode_stream([(5000, [1, 2, 4000]), (5100, [0, 7, 5099]), (4900, [])]),
             stream(random.Random(1), 60, 4, False), stream(random.Random(2), 60, 3, False, lead=5000)]

    def run(fn, data):
        try:
            return fn(data)
        except ValueError as e:
            return "error: " + str(e)

    decoded = errors = 0
    for n in range(20_000):
        if n % 3:
            data = bytearray(rng.choice(seeds))
            for _ in range(rng.randint(1, 4)):
                data[rng.randrange(len(data))] = rng.choice(alphabet) if rng.random() < 0.7 else rng.randrange(256)
        else:
            data = bytearray(rng.choice(alphabet) for _ in range(rng.randint(0, 24)))
        want, got = run(reference, bytes(data)), run(P.decode_stream_lead, bytes(data))
        assert got == want, bytes(data).hex()
        decoded += not isinstance(want, str)
        errors += isinstance(want, str)
    assert decoded > 1000 and errors > 1000         # both outcomes are well covered


def test_same_errors():
    bad = [b"\x00", b"\xF0", b"\xFE\x01\x02", b"\xFE\x00\x00\x00\x00", b"\xFD\x00\x00\x00\x00",
           b"\xFF\x02\x00\x00\x00\x00", b"\xFF\x01\x10\x00\x00\x00\x05", b"\xFF\x01\x10\x00\x00\x00\x05\xFD\x01\x00\x00\x00",
           b"\xFF\x01\x10\x00\x00\x00\x20", b"\xFF\x01\x10\x00\x00\x00\x05\xFF\x02\x01\x00\x00\x00",
           delta(10) + b"\xFD\x05\x00\x00\x00" + b"\xFF"]
    for data in bad:
        with pytest.raises(ValueError) as old:
            reference(data)
        with pytest.raises(ValueError) as new:
            P.decode_stream_lead(data)
        assert str(new.value) == str(old.value), data.hex()


def test_peak_memory_is_lower():
    """200 000 transitions in three revolutions, as in the review's measurement.  The bound is
    relative to the old decoder on the same input, so it does not depend on the interpreter."""
    rng = random.Random(7)
    out, last, n = bytearray(), 0, 200_000
    for k in range(n):
        d = rng.randint(400, 1200)
        if k and k % 66_000 == 0:
            out += b"\xFD" + (d // 2).to_bytes(4, "little")
        out += delta(d)
        last += d
    out += b"\xFD" + (5).to_bytes(4, "little")
    data = bytes(out)

    def peak(fn):
        tracemalloc.start()
        result = fn(data)
        _, top = tracemalloc.get_traced_memory()
        tracemalloc.stop()
        return top, result

    old_peak, old = peak(reference)
    new_peak, new = peak(P.decode_stream_lead)
    assert new == old and sum(len(r.samples) for r in new[1]) == n
    print(f"decode peak: {old_peak / 2**20:.1f} MiB before, {new_peak / 2**20:.1f} MiB now, input {len(data)} bytes")
    assert new_peak < 0.7 * old_peak, (old_peak, new_peak)

    # time budget, relative to the old decoder on this machine (best of five, no wall-clock limit)
    import time

    def best(fn):
        t = 9.0
        for _ in range(5):
            t0 = time.perf_counter()
            fn(data)
            t = min(t, time.perf_counter() - t0)
        return t

    ratio = best(P.decode_stream_lead) / best(reference)
    print(f"decode time: {ratio:.2f} x the two-pass decoder")
    assert ratio < 1.25, ratio
