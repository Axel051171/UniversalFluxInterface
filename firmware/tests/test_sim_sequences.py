"""Random command sequences over USB and LAN against the production dispatcher and write path
(sim_v2.c), with the state checked after every step (review item 3 of 2026-11-14).

Steps, each from a random transport: queries, drive commands and setting changes; READs that
finish, are aborted while waiting for the index or mid-stream (by the owner, or tried by the other
transport), and connections that drop mid-read with an immediate retry; WRITEs (classic backend,
no verify, sent as WRITE request, DATA frames in random pieces, END) that finish, that are longer
than a revolution, that are aborted during the upload or while waiting for the index, whose
connection drops during the upload or while the write waits or runs, and that the other transport
tries to disturb (DATA with the same sequence number, ABORT, a WRITE of its own).  In every third
seed the session write lock (ANALYSIS_JOB op 37) is engaged at some point, alone or in the middle
of an upload.  Disk writes over LAN are allowed in about two thirds of the seeds.

What a WRITE request must be answered with is derived from the device's own state, in the order
the dispatcher checks: session lock engaged (asked with op 37) -> 22; from the LAN without
lan_write -> 12; from the transport that does not own the drive -> 1; otherwise 0.

Checked after every step:
- every request gets exactly one reply with its command and sequence number, a WRITE at most one
  END, and no frame is left over that the step did not expect;
- the dispatcher's decision fits its own ownership: a changing request from the transport that
  does not own the drive is refused with status 1 and changes nothing; an accepted one leaves
  the drive with its sender; a query never changes the owner;
- a READ that completes decodes to exactly what the simulated drive delivered, and no DATA
  frame follows its END;
- a WRITE that completes put exactly the intervals sent on the disk (the periods the timer / DMA
  model recorded while WGATE was asserted and the write enable open); a track longer than the
  revolution ends with status 19 at the index and only a prefix of it was written; a refused or
  aborted-before-the-index WRITE never raised WGATE;
- write enable and WGATE follow the write state of ufi_write.c: both off while it is idle,
  receiving, complete or in error; enable open and WGATE low while it waits for the index (the
  firmware opens the enable in ufi_write_start(), before the index wait); both on while it is
  active.  No write is pending and the write state is idle at the end of every step;
- WGATE never rose while the write enable was closed, and the enable never closed under an
  asserted WGATE (counter of the model);
- after an abort or a dropped connection the capture is idle, no write is pending, and no END or
  DATA of the ended operation arrives later;
- after a closed LAN connection the LAN does not own the drive.
At the end of every sequence, after both sessions ended, a plain READ works, and a plain WRITE
works with the written periods equal to the sent ones - or, where the session lock was engaged,
is refused with status 22 while the enable stays closed.

Not covered: the read-back of a verifying WRITE (not modelled), the extended backend and
GUARDED_WRITE in sequences (test_sim_host.py writes one extended track), the integrity job around
a WRITE, media change, write protect, DMA underrun (the model always serves the request in time),
interrupt latency and every other hardware timing.
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
PING, SEEK, ANALYSIS, READ, ABORT, WRITE = 0x00, 0x12, 0x0F, 0x20, 0x21, 0x22
USB, LAN = 1, 2
NAME = {USB: 'USB', LAN: 'LAN'}
IDLE, RECEIVING, WAITING_INDEX, ACTIVE = 0, 1, 2, 3     # write_state_t
REV_TICKS = 5_500_000                                   # revolution of the simulated drive (20 ms)
STEPS = 40


class Bench:
    def __init__(self, exe, rng, lan_write):
        self.s = sim_v2.SimStream(exe)
        self.rng = rng
        self.parser = P2.FrameParser()
        self.frames = []
        self.seq = {USB: 0, LAN: 0}
        self.trace = []
        self.reading = None                 # (transport, seq) of the READ that is open
        self.seen = {}
        self.lan_write = lan_write
        self.s.lan_write(lan_write)

    def count(self, what):
        self.seen[what] = self.seen.get(what, 0) + 1

    def pump(self, ticks=0):
        if ticks:
            self.s.tick(ticks)
        data = bytes(self.s.buf)
        self.s.buf.clear()
        self.frames += self.parser.feed(data)

    def frame(self, t, ftype, cmd, seq, payload=b''):
        if t == USB:
            self.s.write(P2.encode_frame(ftype, cmd, seq, 0, bytes(payload)))
        else:
            self.s.lan_frame(ftype, cmd, seq, payload)

    def send(self, t, cmd, payload=b''):
        self.seq[t] = self.seq[t] % 120 + 1
        seq = self.seq[t] + (0 if t == USB else 128)
        self.trace.append((NAME[t], hex(cmd), seq, bytes(payload[:24]).hex()))
        self.frame(t, REQUEST, cmd, seq, payload)
        return seq

    def reply_frame(self, cmd, seq):
        for _ in range(40):
            self.pump()
            got = [f for f in self.frames if f.type == REPLY and f.cmd == cmd and f.seq == seq]
            if got:
                assert len(got) == 1, ('two replies', self.trace[-3:])
                self.frames.remove(got[0])
                return got[0]
            self.pump(1)
        raise AssertionError(('no reply', hex(cmd), seq, self.trace[-5:]))

    def reply(self, cmd, seq):
        return self.reply_frame(cmd, seq).status

    def state(self, write=(IDLE,)):
        """State of the simulator with the invariants that hold at any time; `write`: the write
        states the caller expects at this point."""
        st = self.s.state()
        ws = st['write_state']
        where = (st, self.trace[-5:])
        assert st['owner'] in (0, USB, LAN) and st['enable_violations'] == 0, where
        assert ws in write, where
        assert st['write_enable'] == (ws in (WAITING_INDEX, ACTIVE)) and st['wgate'] == (ws == ACTIVE), where
        assert st['write_pending'] == (ws in (RECEIVING, WAITING_INDEX, ACTIVE)), where
        return st

    def locked(self, t):
        """Session write lock as the device reports it (op 37 read form, a query)."""
        f = self.reply_frame(ANALYSIS, self.send(t, ANALYSIS, bytes([37])))
        assert f.status == 0 and len(f.payload) == 2 and f.payload[0] == 1 and not f.payload[1] & 2, f
        return bool(f.payload[1] & 1)

    def take(self, seq, kind, cmd=READ):
        got = [f for f in self.frames if f.type == kind and f.cmd == cmd and f.seq == seq]
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


# ---- WRITE ---------------------------------------------------------------------------------

class Write:
    """One WRITE: the track, its stream code and what was sent of it."""

    def __init__(self, b, t, long=False, big=False):
        rng = b.rng
        if long:                            # longer than the revolution: 2900 x 1900 ticks and more
            self.deltas = [rng.randrange(1900, 2001) for _ in range(rng.randrange(2900, 3001))]
        elif big:                           # long enough to be caught while it is being written
            self.deltas = [rng.randrange(440, 2001) for _ in range(rng.randrange(1500, 2500))]
        elif rng.random() < 0.2:            # more than the ring of 2 x 2048 periods: the DMA wraps
            self.deltas = [rng.randrange(440, 1201) for _ in range(rng.randrange(4100, 4501))]
        else:
            self.deltas = [rng.randrange(440, 2001) for _ in range(rng.randrange(3, 2500))]
        self.code = P2.encode_flux(self.deltas)
        self.b, self.t, self.sent, self.seq = b, t, 0, None
        self.rises = b.s.written()['gate_rises']

    def request(self, t=None):
        """Send the WRITE request -> status.  The expectation comes from the device's state."""
        b, t = self.b, t or self.t
        before = b.state(write=(IDLE, RECEIVING))
        want = 22 if b.locked(t) else 12 if t == LAN and not b.lan_write else 1 if before['owner'] not in (0, t) else 0
        payload = P2.WRITE_REQ.pack(b.rng.randrange(80), b.rng.randrange(2), 0, len(self.deltas), len(self.code))
        seq = b.send(t, WRITE, payload)
        status = b.reply(WRITE, seq)
        assert status == want, ('WRITE answered', status, 'expected', want, before, b.trace[-4:])
        b.count({0: 'write_accepted', 1: 'write_non_owner', 12: 'write_lan_denied', 22: 'write_locked'}[status])
        if status:
            b.pump(3)
            after = b.state(write=(before['write_state'],))
            assert after['owner'] == before['owner'] and b.s.written()['gate_rises'] == self.rises, (status, after)
            assert not b.take(seq, END, WRITE) and not b.take(seq, DATA, WRITE)
        else:
            assert b.state(write=(RECEIVING,))['owner'] == t
            self.seq = seq
        return status

    def data(self, part=1.0):
        """Send DATA frames in random pieces up to `part` of the stream code."""
        b, stop = self.b, int(len(self.code) * part)
        while self.sent < stop:
            n = min(stop - self.sent, b.rng.choice((1, 7, 64, 500, 1024, 4096)))
            b.frame(self.t, DATA, WRITE, self.seq, self.code[self.sent:self.sent + n])
            self.sent += n

    def end(self, tries=120):
        """Wait for the END frame of this WRITE -> status."""
        b = self.b
        for _ in range(tries):
            b.pump(2)
            ends = b.take(self.seq, END, WRITE)
            if ends:
                assert len(ends) == 1, ('two ENDs', b.trace[-4:])
                return ends[0].status
        raise AssertionError(('no END for the WRITE', b.s.state(), b.trace[-5:]))

    def nothing_more(self):
        """The WRITE is over: idle, nothing pending, and no frame of it arrives later."""
        b = self.b
        b.pump(8)
        b.state()
        assert not b.take(self.seq, END, WRITE) and not b.take(self.seq, DATA, WRITE), ('frames after the end of a WRITE', b.trace[-4:])

    def on_disk(self):
        """-> (periods on the disk as ticks, record); pulse width and mode are checked here."""
        w = self.b.s.written()
        if w['gate_rises'] == self.rises:
            return None, w
        assert w['gate_rises'] == self.rises + 1 and w['elapsed'] == len(w['periods']), (w['gate_rises'], w['elapsed'])
        assert all(p[1:] == (109, 6) for p in w['periods']), 'pulse width / PWM1'
        return [p[0] for p in w['periods']], w

    def finish(self):
        """Send the rest, expect END 0 and the track on the disk exactly as sent."""
        self.data()
        status = self.end()
        assert status == 0, ('WRITE ended with', status, self.b.trace[-4:])
        periods, w = self.on_disk()
        assert periods == self.deltas, ('written periods differ from the intervals sent', len(self.deltas),
                                        None if periods is None else len(periods), self.b.trace[-4:])
        assert (w['gate_off'] - w['gate_on']) & 0xFFFFFFFF == sum(self.deltas)
        self.nothing_more()

    def never_gated(self):
        assert self.b.s.written()['gate_rises'] == self.rises, ('WGATE rose for a WRITE that never started', self.b.trace[-4:])


def drop(b, t):
    b.s.lan_closed() if t == LAN else b.s.usb_dropped()


def lock(b, t, owner):
    """Engage the session write lock (op 37): a setting change, so the drive must be the sender's."""
    was = b.locked(t)
    status = b.reply(ANALYSIS, b.send(t, ANALYSIS, bytes([37, 1])))
    if owner not in (0, t):
        assert status == 1 and b.locked(t) == was and b.state(write=(IDLE, RECEIVING))['owner'] == owner, ('lock from the non-owner', status)
        return False
    assert status == 0 and b.locked(t) and b.state(write=(IDLE, RECEIVING))['owner'] == t, ('lock refused', status)
    b.count('lock_engaged')
    return True


def write_step(b, kind, t, other, owner):
    rng = b.rng
    w = Write(b, t, long=kind == 'write_long', big=kind == 'write_drop_active')
    if w.request():
        return                              # refused as the device's state demands; checked in request()
    if kind == 'write':
        if rng.random() < 0.3:              # requests arriving during the upload
            w.data(rng.random())
            assert b.reply(PING, b.send(t, PING)) == 0
            assert b.reply(SEEK, b.send(t, SEEK, bytes([3]))) == 1          # the drive is in use
            assert b.reply(PING, b.send(other, PING)) == 0
            b.state(write=(RECEIVING,))
        w.finish()
        assert b.state()['owner'] == t
    elif kind == 'write_long':              # the index comes before the last interval
        w.data()
        assert w.end() == 19, 'track longer than the revolution'
        periods, rec = w.on_disk()
        assert periods is not None and 0 < len(periods) < len(w.deltas) and periods == w.deltas[:len(periods)]
        assert (rec['gate_off'] - rec['gate_on']) & 0xFFFFFFFF == REV_TICKS  # index to index
        assert sum(periods) <= REV_TICKS < sum(w.deltas[:len(periods) + 1])
        w.nothing_more()
    elif kind == 'write_abort_upload':
        w.data(rng.random() * 0.9)
        b.pump(rng.randrange(0, 5))
        b.state(write=(RECEIVING,))
        assert b.reply(ABORT, b.send(t, ABORT)) == 0
        assert w.end(10) == 4               # the aborted operation ends with status 4, once
        w.never_gated()
        w.nothing_more()
        b.frame(t, DATA, WRITE, w.seq, w.code[w.sent:w.sent + 64] or b'\x01')   # late DATA: ignored
        w.nothing_more()
        w.never_gated()
    elif kind == 'write_abort_index':
        b.s.index(False)                    # no index pulse: the write stays armed
        w.data()
        b.pump(rng.randrange(6, 40))
        b.state(write=(WAITING_INDEX,))
        assert b.reply(ABORT, b.send(t, ABORT)) == 0
        assert w.end(10) == 4
        b.s.index(True)
        w.never_gated()
        w.nothing_more()
        b.pump(25)                          # an index passes: nothing starts
        w.never_gated()
        w.nothing_more()
    elif kind == 'write_drop_upload':
        w.data(rng.random() * 0.9)
        b.pump(rng.randrange(0, 5))
        b.state(write=(RECEIVING,))
        drop(b, t)
        b.pump(6)
        b.frames.clear()                    # the END of the cancelled WRITE goes to a session that is gone
        w.never_gated()
        w.nothing_more()
        if t == LAN:
            assert b.state()['owner'] != LAN
        b.pump(25)
        w.never_gated()
        w.nothing_more()
    elif kind == 'write_drop_active':
        w.data()
        b.pump(rng.randrange(1, 30))        # waiting for the index, writing, or already done
        before = b.state(write=(IDLE, RECEIVING, WAITING_INDEX, ACTIVE))['write_state']
        b.count(('drop_after_write', 'drop_write_armed', 'drop_write_armed', 'drop_write_active')[before])
        drop(b, t)
        b.pump(6)
        b.frames.clear()
        b.state()
        periods, _ = w.on_disk()
        if before in (RECEIVING, WAITING_INDEX):
            assert periods is None, 'dropped before the index, yet WGATE rose'
        elif before == IDLE:
            assert periods == w.deltas
        else:
            assert periods is not None and periods == w.deltas[:len(periods)]
        if t == LAN:
            assert b.state()['owner'] != LAN
        rises = b.s.written()['gate_rises']
        b.pump(25)
        w.nothing_more()
        assert b.s.written()['gate_rises'] == rises, 'WGATE rose after the connection dropped'
    else:                                   # write_foreign: the other transport cannot disturb the upload
        w.data(rng.random() * 0.9)
        b.frame(other, DATA, WRITE, w.seq, b'\xFE\x00\x10\x00\x00' * 8)     # same sequence number, other transport
        b.pump(2)
        assert b.reply(ABORT, b.send(other, ABORT)) == 1
        assert Write(b, other).request() in (1, 12)
        assert b.state(write=(RECEIVING,))['owner'] == t
        w.finish()


def lock_step(b, t, owner):
    if b.rng.random() < 0.5 or b.locked(t):
        lock(b, t, owner)
        return
    # the lock is engaged between acceptance and start: the upload completes, the write does not start
    w = Write(b, t)
    if w.request():
        return
    w.data(b.rng.random() * 0.9)
    assert lock(b, t, t)
    w.data()
    assert w.end() == 22, 'WRITE started although the session lock was engaged during its upload'
    w.never_gated()
    w.nothing_more()
    b.count('lock_mid_upload')


READ_KINDS = ('ping', 'query', 'seek', 'set', 'read', 'read', 'abort_wait', 'abort_mid', 'foreign_abort',
              'drop_mid', 'drop_idle')
WRITE_KINDS = ('write', 'write', 'write_long', 'write_abort_upload', 'write_abort_index', 'write_drop_upload',
               'write_drop_active', 'write_foreign')


def step(b, may_lock):
    rng, t = b.rng, b.rng.choice((USB, LAN))
    other = LAN if t == USB else USB
    owner = b.state()['owner']
    kind = rng.choice(READ_KINDS + WRITE_KINDS + (('lock',) if may_lock else ()))
    b.count(kind)
    if owner not in (0, t):
        b.count('non-owner')
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
            drop(b, t)
            b.frames.clear()
            b.idle_after('drop_mid')
            if t == LAN:
                assert b.state()['owner'] != LAN
            b.pump(8)
            b.frames.clear()                            # whatever was already on its way before the drop
            seq = start_read(b, t, 1)
            if seq is not None:
                b.finish_read(seq, 1)
    elif kind == 'drop_idle':
        drop(b, t)
        b.pump(2)
        b.frames.clear()
        if t == LAN:
            assert b.state()['owner'] != LAN
    elif kind == 'lock':
        lock_step(b, t, owner)
    else:
        write_step(b, kind, t, other, owner)
    b.pump()
    assert not b.frames, ('frames nobody asked for', [(f.type, hex(f.cmd), f.seq, f.status) for f in b.frames], b.trace[-4:])
    b.state()


def main():
    exe, _ = sim_v2.build()
    steps = 0
    seen = {}
    for seed in range(60):
        rng = random.Random(seed)
        b = Bench(exe, rng, lan_write=seed % 3 != 1)
        try:
            for _ in range(STEPS):
                step(b, may_lock=seed % 3 == 0)
                steps += 1
            # whatever happened: after both sessions ended, a plain READ and a plain WRITE work
            b.s.lan_closed()
            b.s.usb_dropped()
            b.pump(8)
            b.frames.clear()
            seq = start_read(b, USB, 2)
            assert seq is not None, ('final READ refused', seed)
            b.finish_read(seq, 2)
            st = b.state()
            assert st['owner'] == USB and st['capture'] not in (1, 2)
            locked = b.locked(USB)
            w = Write(b, USB)
            status = w.request()
            assert status == (22 if locked else 0), ('final WRITE', status, locked)
            if not locked:
                w.finish()
            b.count('final_locked' if locked else 'final_write')
            for k, v in b.seen.items():
                seen[k] = seen.get(k, 0) + v
        except AssertionError:
            print('seed', seed, 'last steps:', b.trace[-8:])
            raise
        finally:
            b.s.close()
    # the sequences did reach every kind of step and every answer a WRITE can get, often
    need = {k: 60 for k in READ_KINDS + WRITE_KINDS}
    need.update({'non-owner': 300, 'write_accepted': 300, 'write_non_owner': 60, 'write_lan_denied': 40,
                 'write_locked': 40, 'lock': 20, 'lock_engaged': 15, 'lock_mid_upload': 4,
                 'drop_write_armed': 10, 'drop_write_active': 10, 'final_write': 30, 'final_locked': 10})
    short = {k: (seen.get(k, 0), n) for k, n in need.items() if seen.get(k, 0) < n}
    assert not short, ('step kinds reached too rarely (count, needed)', short, seen)
    print(f'command sequences: 60 seeds, {steps} random steps over USB and LAN; ownership, replies, aborted and '
          f'dropped READs and WRITEs, capture contents, {seen["write_accepted"]} accepted WRITEs with the written '
          f'periods equal to the intervals sent, {seen["write_non_owner"]} from the non-owner, '
          f'{seen["write_locked"]} under the session lock, write enable and WGATE following the write state '
          'after every step')


if __name__ == '__main__':
    main()
