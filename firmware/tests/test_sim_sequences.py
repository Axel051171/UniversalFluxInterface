"""Random command sequences over USB and LAN against the production dispatcher (sim_v2.c),
with the state checked after every step (review item 3 of 2026-11-14).

Steps: queries, drive commands and setting changes from either transport, READs that finish,
are aborted while waiting for the index or mid-stream (by the owner, or tried by the other
transport), and connections that drop mid-read with an immediate retry.

Checked after every step:
- every request gets exactly one reply with its command and sequence number;
- the dispatcher's decision fits its own ownership: a changing request from the transport that
  does not own the drive is refused with status 1 and changes nothing; an accepted one leaves
  the drive with its sender; a query never changes the owner;
- a READ that completes decodes to exactly what the simulated drive delivered, and no DATA
  frame follows its END;
- after an abort or a dropped connection the capture is idle;
- the write enable never opened and no write is pending (read-only sequences);
- after a closed LAN connection the LAN does not own the drive.
And at the end of every sequence a plain READ still works.

Not covered: the write path (ufi_write.c is not part of the simulator), media change, timing.
"""
import random
import sys

from run_tests import FW
import sim_v2
from test_sim_host import expected

sys.path.insert(0, str(FW.parent / 'software' / 'ufi_host'))
from ufi_host import protocol as P  # noqa: E402
from ufi_host import protocol2 as P2  # noqa: E402

REQUEST, REPLY, DATA, END = 1, 2, 4, 5
PING, SEEK, ANALYSIS, READ, ABORT = 0x00, 0x12, 0x0F, 0x20, 0x21
USB, LAN = 1, 2
NAME = {USB: 'USB', LAN: 'LAN'}


class Bench:
    def __init__(self, exe, rng):
        self.s = sim_v2.SimStream(exe)
        self.rng = rng
        self.parser = P2.FrameParser()
        self.frames = []
        self.seq = {USB: 0, LAN: 0}
        self.trace = []
        self.reading = None                 # (transport, seq) of the READ that is open
        self.seen = {}

    def pump(self, ticks=0):
        if ticks:
            self.s.tick(ticks)
        data = bytes(self.s.buf)
        self.s.buf.clear()
        self.frames += self.parser.feed(data)

    def send(self, t, cmd, payload=b''):
        self.seq[t] = self.seq[t] % 120 + 1
        seq = self.seq[t] + (0 if t == USB else 128)
        self.trace.append((NAME[t], hex(cmd), seq, bytes(payload).hex()))
        if t == USB:
            self.s.write(P2.encode_frame(REQUEST, cmd, seq, 0, bytes(payload)))
        else:
            self.s.lan_frame(REQUEST, cmd, seq, payload)
        return seq

    def reply(self, cmd, seq):
        for _ in range(40):
            self.pump()
            got = [f for f in self.frames if f.type == REPLY and f.cmd == cmd and f.seq == seq]
            if got:
                assert len(got) == 1, ('two replies', self.trace[-3:])
                self.frames.remove(got[0])
                return got[0].status
            self.pump(1)
        raise AssertionError(('no reply', hex(cmd), seq, self.trace[-5:]))

    def state(self):
        st = self.s.state()
        assert st['owner'] in (0, USB, LAN) and st['write_enables'] == 0 and not st['write_pending'], (st, self.trace[-5:])
        return st

    def take(self, seq, kind):
        got = [f for f in self.frames if f.type == kind and f.cmd == READ and f.seq == seq]
        for f in got:
            self.frames.remove(f)
        return got

    def finish_read(self, seq, revs):
        """Let the open READ run to its END and compare with the simulated drive."""
        data, end = bytearray(), None
        for _ in range(400):
            self.pump(2)
            for f in self.take(seq, DATA):
                assert end is None, 'DATA after END'
                data += f.payload
            ends = self.take(seq, END)
            if ends:
                assert len(ends) == 1 and end is None
                end = ends[0]
                break
        assert end is not None and end.status == 0, (end, self.trace[-5:])
        self.pump(5)
        assert not self.take(seq, DATA) and not self.take(seq, END), 'frames after END'
        idx, flux = self.s.truth()
        got = [(r.index_ticks, r.samples) for r in P.decode_stream_lead(bytes(data), 5, 1)[1]]
        assert got == expected(idx, flux, revs), ('capture differs', self.trace[-5:])
        self.reading = None

    def idle_after(self, what):
        self.pump(6)
        st = self.state()
        assert st['capture'] not in (1, 2), (what, st, self.trace[-5:])     # not waiting, not running
        self.reading = None


def start_read(b, t, revs=1, lead_ms=3):
    b.s.scenario(lead_ms=lead_ms)
    owner = b.state()['owner']
    seq = b.send(t, READ, bytes([5, 1, revs]))
    status = b.reply(READ, seq)
    if owner not in (0, t):
        assert status == 1 and b.state()['owner'] == owner, ('READ from the non-owner', status, b.trace[-3:])
        return None
    assert status == 0 and b.state()['owner'] == t, ('READ refused', status, b.trace[-3:])
    b.reading = (t, seq)
    return seq


def step(b):
    rng, t = b.rng, b.rng.choice((USB, LAN))
    other = LAN if t == USB else USB
    owner = b.state()['owner']
    kind = rng.choice(('ping', 'query', 'seek', 'set', 'read', 'read', 'abort_wait', 'abort_mid',
                       'foreign_abort', 'drop_mid', 'drop_idle'))
    b.seen[kind] = b.seen.get(kind, 0) + 1
    if owner not in (0, t):
        b.seen['non-owner'] = b.seen.get('non-owner', 0) + 1
    if kind == 'ping':
        assert b.reply(PING, b.send(t, PING)) == 0 and b.state()['owner'] == owner
    elif kind == 'query':                               # read form of a setting: never needs the drive
        assert b.reply(ANALYSIS, b.send(t, ANALYSIS, bytes([41]))) == 0 and b.state()['owner'] == owner
    elif kind in ('seek', 'set'):
        cmd, payload = (SEEK, bytes([rng.randrange(80)])) if kind == 'seek' else (ANALYSIS, bytes([41, 1, 30, 0]))
        status = b.reply(cmd, b.send(t, cmd, payload))
        if owner not in (0, t):
            assert status == 1 and b.state()['owner'] == owner, (kind, 'from the non-owner', status, b.trace[-3:])
        else:
            assert status == 0 and b.state()['owner'] == t, (kind, status, b.trace[-3:])
    elif kind == 'read':
        revs = rng.choice((1, 2))
        seq = start_read(b, t, revs)
        if seq is not None:
            if rng.random() < 0.5:                      # commands arriving while the READ runs
                assert b.reply(PING, b.send(t, PING)) == 0
                assert b.reply(SEEK, b.send(t, SEEK, bytes([3]))) == 1          # the drive is in use
                assert b.reply(ANALYSIS, b.send(t, ANALYSIS, bytes([41, 1, 20, 0]))) == 1
                assert b.reply(PING, b.send(other, PING)) == 0                  # the other transport may ask
                assert b.reply(SEEK, b.send(other, SEEK, bytes([3]))) == 1
            b.finish_read(seq, revs)
            assert b.state()['owner'] == t
    elif kind in ('abort_wait', 'abort_mid'):
        seq = start_read(b, t, 2, lead_ms=40 if kind == 'abort_wait' else 3)
        if seq is not None:
            b.pump(5 if kind == 'abort_wait' else 3 + rng.randrange(1, 30))
            assert b.reply(ABORT, b.send(t, ABORT)) == 0
            b.idle_after(kind)
            ends = b.take(seq, END)
            assert len(ends) <= 1                       # at most one END for the aborted READ
            b.take(seq, DATA)
            b.pump(10)
            assert not b.take(seq, DATA) and not b.take(seq, END), 'frames after an aborted READ'
    elif kind == 'foreign_abort':                       # the other transport cannot stop a READ it does not own
        seq = start_read(b, t, 1)
        if seq is not None:
            assert b.reply(ABORT, b.send(other, ABORT)) == 1
            b.finish_read(seq, 1)
    elif kind == 'drop_mid':                            # connection lost mid-read, immediate retry
        seq = start_read(b, t, 2)
        if seq is not None:
            b.pump(rng.randrange(1, 25))
            b.s.lan_closed() if t == LAN else b.s.usb_dropped()
            b.frames.clear()
            b.idle_after('drop_mid')
            if t == LAN:
                assert b.state()['owner'] != LAN
            b.pump(8)
            b.frames.clear()                            # whatever was already on its way before the drop
            seq = start_read(b, t, 1)
            if seq is not None:
                b.finish_read(seq, 1)
    else:                                               # drop_idle
        b.s.lan_closed() if t == LAN else b.s.usb_dropped()
        b.pump(2)
        b.frames.clear()
        if t == LAN:
            assert b.state()['owner'] != LAN
    b.state()


def main():
    exe, _ = sim_v2.build()
    steps = kinds = 0
    seen = {}
    for seed in range(60):
        b = Bench(exe, random.Random(seed))
        try:
            for _ in range(25):
                step(b)
                steps += 1
            # whatever happened: after both sessions ended, a plain READ works
            b.s.lan_closed()
            b.s.usb_dropped()
            b.pump(8)
            b.frames.clear()
            seq = start_read(b, USB, 2)
            assert seq is not None, ('final READ refused', seed)
            b.finish_read(seq, 2)
            st = b.state()
            assert st['owner'] == USB and st['capture'] not in (1, 2)
            for k, v in b.seen.items():
                seen[k] = seen.get(k, 0) + v
        except AssertionError:
            print('seed', seed, 'last steps:', b.trace[-8:])
            raise
        finally:
            b.s.close()
    # the sequences did reach every kind of step, and the non-owner case often
    assert all(seen.get(k, 0) >= 60 for k in ('ping', 'query', 'seek', 'set', 'read', 'abort_wait', 'abort_mid',
                                               'foreign_abort', 'drop_mid', 'drop_idle', 'non-owner')), seen
    print(f'command sequences: 60 seeds, {steps} random steps over USB and LAN; ownership, replies, aborted and '
          'dropped READs, capture contents and the closed write enable held after every step')


if __name__ == '__main__':
    main()
