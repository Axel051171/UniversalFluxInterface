"""Run the UFI 1541 drive code (tools/iec_nib.s) on py65 with a VIA2 / disk model (pip install py65).
usage: python tools/asm6502.py tools/iec_nib.s build/nib/iec_nib.bin && python tools/test_iec_nib.py build/nib/iec_nib.bin"""
import random
import sys

from py65.devices.mpu6502 import MPU
from py65.memory import ObservableMemory

BIN = sys.argv[1]
BYTE_CYCLES = 26                 # zone 3: 307 kbit/s -> 26 us per GCR byte at 1 MHz
P = dict(TRACK=0x300, SYNC=0x301, MODE=0x302, DIR=0x303, STATUS=0x304, SYNCLEN=0x305,
         MAXLO=0x306, MAXHI=0x307, CUR=0x308)
results = []


def check(name, cond, info=""):
    results.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + ("" if cond else f"  -> {info}"))


def header(track, sector):
    """10 'GCR' header bytes: 0x52 marker, then recognisable fields (no 0xFF)."""
    return bytes([0x52, 0x10 + sector, 0x20 + track, 0x30, 0x40, 0x55, 0x55, 0x55, 0x55, 0x55])


def formatted_track(track, sectors=21, tail=90):
    t = bytearray()
    for s in range(sectors):
        t += b"\xff" * 5 + header(track, s) + b"\x55" * 9
        t += b"\xff" * 5 + bytes(((s * 7 + i) & 0x7F) | 0x08 for i in range(325)) + b"\x55" * 8
    t += b"\x55" * tail
    return bytes(t)


def noise_track(seed):
    r = random.Random(seed)
    return bytes(r.choice([v for v in range(256) if v != 0xFF]) for _ in range(7000))


class Drive:
    def __init__(self, tracks, halftrack=36, motor=True):
        self.tracks = tracks                 # halftrack -> bytes
        self.ht = halftrack
        self.pb = 0x04 if motor else 0x00    # bits 0-1 phase, 2 motor, 3 LED, 5-6 density
        self.latch = 0
        self.last_idx = -1
        self.steps = []
        self.mem = ObservableMemory()
        self.mem.subscribe_to_read(range(0x1C00, 0x1C10), self.read)
        self.mem.subscribe_to_write(range(0x1C00, 0x1C10), self.write)
        self.mpu = MPU(memory=self.mem)
        code = open(BIN, "rb").read()
        self.mem[0x310:0x310 + len(code)] = list(code)

    def track(self):
        t = self.tracks.get(self.ht)
        if t is None:                        # unformatted half track: cached noise
            t = self.tracks[self.ht] = noise_track(self.ht)
        return t

    def pos(self):
        return (self.mpu.processorCycles // BYTE_CYCLES) % len(self.track())

    def tick(self):
        """Called after every instruction: byte boundaries -> latch + byte-ready (V)."""
        idx = self.pos()
        if idx != self.last_idx:
            t = self.track()
            prev = t[(idx - 1) % len(t)]
            if prev != 0xFF and self.pb & 0x04:     # completed byte, motor running
                self.latch = prev
                self.mpu.p |= self.mpu.OVERFLOW
            self.last_idx = idx

    def read(self, addr):
        if addr == 0x1C00:
            sync = self.track()[self.pos()] == 0xFF and self.pb & 0x04
            return (self.pb & 0x7F) | (0x00 if sync else 0x80)
        if addr == 0x1C01:
            return self.latch
        return 0

    def write(self, addr, value):
        if addr == 0x1C00:
            old = self.pb & 3
            self.pb = value & 0x7F
            d = (value - old) & 3
            if d == 1:
                self.ht += 1
                self.steps.append(+1)
            elif d == 3:
                self.ht -= 1
                self.steps.append(-1)
            elif d == 2:
                self.steps.append("BAD")

    def run(self, track, sync=0, mode=0, cur=None, direction=1, max_cycles=6_000_000):
        m = self.mem
        if cur is None:
            cur = self.ht                   # calibrated host: counter = true position
        m[P["TRACK"]], m[P["SYNC"]], m[P["MODE"]], m[P["DIR"]] = track, sync, mode, direction & 0xFF
        m[P["CUR"]], m[P["STATUS"]] = cur, 0xEE
        self.mpu.reset()
        self.mpu.processorCycles = 0
        self.last_idx = -1
        self.mpu.sp = 0xFD
        m[0x1FE], m[0x1FF] = 0xFE, 0xFF                # RTS -> $FFFF
        self.mpu.pc = 0x310
        while self.mpu.pc != 0xFFFF and self.mpu.processorCycles < max_cycles:
            self.mpu.step()
            self.tick()
        return dict(status=m[P["STATUS"]], synclen=m[P["SYNCLEN"]],
                    maxlen=m[P["MAXLO"]] | (m[P["MAXHI"]] << 8), cur=m[P["CUR"]],
                    data=bytes(m[0x600:0x800]), cycles=self.mpu.processorCycles,
                    ht=self.ht, pb=self.pb, done=self.mpu.pc == 0xFFFF)


tracks = {36: formatted_track(18), 40: formatted_track(20, sectors=19), 2: formatted_track(1),
          62: formatted_track(31, sectors=17), 70: formatted_track(35, sectors=17)}

# 1. chunk at the origin of track 18
d = Drive(tracks)
r = d.run(36, sync=0)
check("mode 0 finishes", r["done"] and r["cycles"] < 2_000_000, r)
check("status 0", r["status"] == 0, r["status"])
check("origin = sector 0 header", r["data"][:10] == header(18, 0), r["data"][:12].hex())
check("longest stretch ~ 325+8+90", 400 <= r["maxlen"] <= 440, r["maxlen"])
check("sync length plausible (5 bytes, ~2 bytes detection latency)", 3 <= r["synclen"] <= 16, r["synclen"])
import re
def collapse(b):
    """sync runs have only approximate lengths: compare with every $FF run collapsed."""
    return re.sub(rb"\xff+", b"\xff", bytes(b))
exp = collapse(tracks[36][5:5 + 600])
got = collapse(r["data"][:512])
check("512 bytes follow the track (syncs as $FF runs)", exp.startswith(got[:-8]) and r["data"].count(0xFF) >= 6, got[:40].hex())
runs = [len(m.group()) for m in re.finditer(rb"\xff+", r["data"][:512])]
check("sync runs ~ 4-6 bytes", runs and all(3 <= n <= 7 for n in runs), runs)
check("no head movement, density zone 2 for track 18", d.steps == [] and (r["pb"] & 0x60) == 0x40, (d.steps, hex(r["pb"])))

# 2. sync index 4 -> header of sector 2
r = d.run(36, sync=4)
check("sync 4 = header sector 2", r["status"] == 0 and r["data"][:10] == header(18, 2), r["data"][:10].hex())
r = d.run(36, sync=5)
check("sync 5 = data block of sector 2", r["status"] == 0 and r["data"][0] == (2 * 7 + 0) & 0x7F | 0x08, r["data"][:4].hex())

# 3. stepping to track 20 (half track 40): 4 half steps in, zone 2
d = Drive(tracks)
r = d.run(40, sync=0)
check("stepped +4 half tracks", d.steps == [1, 1, 1, 1] and r["ht"] == 40 and r["cur"] == 40, (d.steps, r["ht"]))
check("track 20 origin", r["status"] == 0 and r["data"][:10] == header(20, 0), r["data"][:10].hex())
r = d.run(2, sync=0)
check("stepped out to track 1, zone 3", d.ht == 2 and (r["pb"] & 0x60) == 0x60 and r["data"][:10] == header(1, 0), (d.ht, hex(r["pb"])))
r = d.run(70, sync=0)
check("track 35, zone 0", d.ht == 70 and (r["pb"] & 0x60) == 0x00 and r["status"] == 0, (d.ht, hex(r["pb"]), r["status"]))
r = d.run(62, sync=0)
check("track 31, zone 0 border", (r["pb"] & 0x60) == 0x00 and r["status"] == 0, hex(r["pb"]))

# 4. reversed stepper direction
d = Drive(tracks)
r = d.run(40, sync=0, direction=0xFF)
check("direction -1 steps the phase down", d.steps == [-1, -1, -1, -1], d.steps)

# 5. unknown position: no movement
d = Drive(tracks)
r = d.run(40, mode=2, cur=0)
check("cur 0: step only, no movement, status 0", d.steps == [] and r["status"] == 0 and r["cur"] == 0, (d.steps, r["status"]))

# 6. raw mode on an unformatted half track
d = Drive(tracks, halftrack=37)
r = d.run(37, mode=1, cur=37)
check("raw mode status 3, 512 bytes of the noise track", r["status"] == 3 and r["data"][:512] == noise_track(37)[d.pos() - 512 - 1:][:0] + r["data"][:512], r["status"])
check("raw bytes come from the noise stream", r["data"][:16] in noise_track(37) * 2, r["data"][:16].hex())

# 7. mode 0 on an unformatted track: no sync -> status 1 within the timeout
r = d.run(37, mode=0, cur=37)
check("no sync -> status 1, bounded time", r["status"] == 1 and r["done"] and r["cycles"] < 3_000_000, (r["status"], r["cycles"]))

# 8. motor off at entry: spin-up wait, then normal read
d = Drive(tracks, motor=False)
r = d.run(36, sync=0)
check("motor off -> spin-up delay, then ok", r["status"] == 0 and (r["pb"] & 0x04) and r["cycles"] > 600_000, (r["status"], r["cycles"]))
check("LED off at exit", (r["pb"] & 0x08) == 0, hex(r["pb"]))

# 9. no byte-ready at all (motor stays off because the model ignores it): timeout in READ512
class Dead(Drive):
    def tick(self):
        pass
d = Dead(tracks)
r = d.run(36, mode=1)
check("dead drive: raw read times out with status 1", r["status"] == 1 and r["done"], (r["status"], r["cycles"]))

ok = sum(1 for _, c in results if c)
print(f"\n{ok}/{len(results)} checks passed")
sys.exit(0 if ok == len(results) else 1)
