"""Build and drive the firmware simulator sim_v2.c: the production v2 dispatcher, stream
encoder, write path (ufi_write.c) and the READ / WRITE completions as a native process, for
host-in-the-loop tests.  sim_v2.c lists what is production code and what it models (time, GPIO,
write enable, index pulses, TIM3 and the write DMA, the synthetic capture).

build() compiles it like the v2 fuzz target (run_fuzz.build_stubbed) but without libFuzzer
and sanitizer runtime, and returns the executable.  SimStream is a byte stream for
ufi_host.protocol2.Device2: what the host writes arrives at the firmware (optionally cut into
fragments), reading lets simulated time pass and returns what the firmware transmitted.

usage: python sim_v2.py        builds and runs a smoke check (PING, INFO, one WRITE)
"""
from pathlib import Path
import struct
import subprocess
import sys

from run_tests import BUILD, FW, compiler
import fuzz_stubs
from run_fuzz import COMMON, STUB_FLAGS, EXE, LINKER_SYMBOLS, LIBC, LIBC_PREFIXES

UNITS = ('ufi_v2', 'ufi_stream', 'ufi_analysis_job', 'ufi_write')
# Units that need the real HAL or the USB device library are not built natively.  What the
# simulated paths use of them is taken from the source verbatim, so it is still production code:
# the READ and WRITE completions of ufi_usb.c (reply() is their v1 fallback, not reached while a
# v2 operation is open), the session write lock of ufi_board.c (session_lock is its one variable),
# ufi_capture_abort() of ufi_main.c and the pin table of board_headless.c.
EXTRACT_HEAD = r'''
#include "ufi_firmware.h"
#include "ufi_read_info.h"
#include "ufi_evidence.h"
#include <string.h>
static int reply(uint8_t cmd, uint8_t status, const void* p, uint16_t n) { (void)cmd; (void)status; (void)p; (void)n; return UFI_OK; }
static uint8_t st(int e) { return (uint8_t)(e < 0 ? -e : e); }
static bool session_lock;
'''
EXTRACT = (('ufi_usb.c', ('ufi_usb_send_read_done', 'ufi_usb_send_event')),
           ('ufi_board.c', ('ufi_board_soft_lock', 'ufi_board_soft_locked', 'ufi_board_write_locked')),
           ('ufi_main.c', ('ufi_capture_abort',)))
REAL = ('ufi_job', 'ufi_resources', 'ufi_cancel', 'ufi_capabilities', 'ufi_media', 'ufi_metrics',
        'ufi_write_codec', 'ufi_quality', 'ufi_protection', 'ufi_lan_status', 'ufi_lan_cfg', 'ufi_read_info',
        'ufi_write_stream', 'ufi_write_wave', 'ufi_verify', 'ufi_raw_verify', 'ufi_mfm')
EXTRA = {'g_capture': ('ufi_firmware.h', 'capture_context_t g_capture;'),
         'hUsbDevice': ('usbd_def.h', 'USBD_HandleTypeDef hUsbDevice;'), **LINKER_SYMBOLS}
# sim_v2.c is told where ufi_write.c writes TIM_EGR_UG (see the HAL stub header)
SIM_FLAGS = ['-DUFI_STUB_TIM_UG_HOOK=sim_tim_ug']


def extracted():
    """Production text for extract.c: functions of EXTRACT and the pin table."""
    import re
    from test_regressions import function
    pins = re.findall(r'^const gpio_pin_t \w+\s*=\s*\{[^}]*\};', (FW / 'src' / 'board_headless.c').read_text(encoding='utf-8'), re.M)
    if len(pins) < 20:
        raise RuntimeError('cannot extract the pin table of board_headless.c')
    return (EXTRACT_HEAD + ''.join(function(file, name) for file, names in EXTRACT for name in names)
            + '\n'.join(pins) + '\n')


def build():
    # one directory per process: the gate runs several simulator tests in parallel
    import os
    objdir = BUILD / f'sim_v2_obj_{os.getpid()}'
    objdir.mkdir(parents=True, exist_ok=True)
    flags = [*COMMON, *STUB_FLAGS, *SIM_FLAGS]
    objects, harness = [], objdir / 'harness.o'
    for unit in (*UNITS, *REAL):
        o = objdir / (unit + '.o')
        subprocess.run([compiler(), *flags, '-w', '-c', str(FW / 'src' / (unit + '.c')), '-o', str(o)], check=True)
        objects.append(o)
    extract = objdir / 'extract.c'
    extract.write_text(extracted(), encoding='utf-8')
    o = objdir / 'extract.o'
    subprocess.run([compiler(), *flags, '-w', '-c', str(extract), '-o', str(o)], check=True)
    objects.append(o)
    subprocess.run([compiler(), *flags, '-Wall', '-Wextra', '-Werror', '-c', str(FW / 'tests' / 'sim_v2.c'),
                    '-o', str(harness)], check=True)
    stubs = objdir / 'stubs.c'
    stubbed, skipped = fuzz_stubs.generate(objects, stubs, harness_objects=[harness], extra=EXTRA)
    # bcmp: clang on Linux turns memcmp(...) == 0 into it (the first CI run on Linux failed here)
    libc = LIBC | {'_fileno', '_setmode', 'fileno', 'fflush', 'fread', 'stdin', 'stdout', 'bcmp', 'memchr'}
    unknown = [s for s in skipped if s not in libc and not s.startswith(LIBC_PREFIXES)]
    if unknown:
        raise SystemExit('sim_v2: no prototype found for ' + ', '.join(unknown))
    exe = objdir / ('sim_v2' + EXE)
    subprocess.run([compiler(), *flags, '-std=c23', '-w', str(stubs), str(harness), *map(str, objects),
                    '-o', str(exe)], check=True)
    return exe, len(stubbed)


class SimStream:
    """Stream for Device2 on top of the simulator process."""

    def __init__(self, exe, fragment=0, ticks_per_read=2):
        self.p = subprocess.Popen([str(exe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.fragment = fragment            # > 0: host bytes arrive in pieces of this size
        self.ticks_per_read = ticks_per_read
        self.buf = bytearray()
        self.sent = bytearray()             # everything the firmware transmitted, for the test
        self.idle_reads = 0

    def _cmd(self, data):
        self.p.stdin.write(data)
        self.p.stdin.flush()
        head = self.p.stdout.read(4)
        if len(head) != 4:
            raise RuntimeError(f'simulator ended (exit {self.p.poll()})')
        return self.p.stdout.read(struct.unpack('<I', head)[0])

    def _take(self, data):
        self.buf += data
        self.sent += data

    def write(self, data):
        data = bytes(data)
        step = self.fragment or len(data) or 1
        for off in range(0, len(data), step):
            piece = data[off:off + step]
            self._take(self._cmd(b'W' + struct.pack('<H', len(piece)) + piece))
            if self.fragment:
                self.tick(1)                # the main loop runs between the fragments
        return len(data)

    def tick(self, n=1):
        self._take(self._cmd(b'T' + struct.pack('<H', n)))

    @property
    def in_waiting(self):
        if not self.buf:
            self.tick(self.ticks_per_read)
        return len(self.buf)

    def read(self, n):
        if not self.buf:
            self.tick(self.ticks_per_read)
        out = bytes(self.buf[:n])
        del self.buf[:n]
        return out

    def scenario(self, rev_ticks=5_500_000, cell_ticks=1100, lead_ms=3):
        """Capture model of the next READ; rev_ticks is also the period of the free-running index
        that starts a WRITE."""
        self._cmd(b'S' + struct.pack('<III', rev_ticks, cell_ticks, lead_ms))

    def index(self, on):
        """Index pulses of the free-running index on / off (off: a drive without a disk)."""
        self._cmd(b'I' + bytes([1 if on else 0]))

    def lan_write(self, on):
        """Disk writes over LAN allowed (lan_write=1 in LAN.CFG with the lock switch open); off at the start."""
        self._cmd(b'A' + bytes([1 if on else 0]))

    def written(self):
        """What reached the disk since WGATE last rose: every write-timer period that ended while
        WGATE was asserted and the write enable open.  periods: (ticks, CCR1, OC1M mode) with mode
        6 = PWM1 (classic backend: a pulse ends the period), 7 = PWM2 (extended backend: the
        transition is CCR1 ticks into the period, none if CCR1 > period - 1), 4 = forced inactive
        (no pulse).  elapsed counts all periods, the model stores the first 131072."""
        d = self._cmd(b'R')
        rises, on, off, elapsed, n = struct.unpack_from('<5I', d)
        return dict(gate_rises=rises, gate_on=on, gate_off=off, elapsed=elapsed,
                    periods=[struct.unpack_from('<IHBx', d, 20 + 8 * i)[:3] for i in range(n)])

    def busy(self, on):
        self._cmd(b'B' + bytes([1 if on else 0]))

    def truth(self):
        """Ground truth of the last capture: (index times, transition times), flux-timer values."""
        d = self._cmd(b'G')
        revs = d[0]
        idx = list(struct.unpack_from(f'<{revs + 1}I', d, 1))
        n, = struct.unpack_from('<I', d, 1 + 4 * (revs + 1))
        return idx, list(struct.unpack_from(f'<{n}I', d, 5 + 4 * (revs + 1)))

    def lan_frame(self, ftype, cmd, seq, payload=b''):
        """One frame from the LAN session; the firmware's answer lands in the same output."""
        self._take(self._cmd(b'L' + bytes([ftype, cmd, seq]) + struct.pack('<H', len(payload)) + bytes(payload)))

    def lan_closed(self):
        self._take(self._cmd(b'C'))

    def usb_dropped(self):
        self._take(self._cmd(b'X'))

    def state(self):
        """write_enable: firmware write enable (WLOCK_FW) open; wgate: WGATE asserted; write_state:
        ufi_write_get_state() (0 = idle); enable_violations: WGATE rose while the enable was closed,
        or the enable closed under an asserted WGATE.  write_safe: enable closed, gate low and no
        violation so far - what holds whenever no write is running."""
        owner, route, capture, write_pending, enable, gate, wstate, violations = struct.unpack('<BBBBBBBI', self._cmd(b'Z'))
        return dict(owner=owner, route=route, capture=capture, write_pending=bool(write_pending),
                    write_enable=bool(enable), wgate=bool(gate), write_state=wstate, enable_violations=violations,
                    write_safe=not enable and not gate and violations == 0)

    def reset_input_buffer(self):
        self.buf.clear()

    def close(self):
        if self.p.poll() is None:
            try:
                self.p.stdin.write(b'Q')
                self.p.stdin.flush()
            except OSError:
                pass
            self.p.wait(timeout=5)


if __name__ == '__main__':
    exe, stubs = build()
    sys.path.insert(0, str(FW.parent / 'software' / 'ufi_host'))
    from ufi_host import protocol2 as P2
    s = SimStream(exe)
    dev = P2.Device2(s)
    assert dev.ping()
    info = dev.info()
    deltas = [1000 + 7 * (i % 16) for i in range(64)]
    dev.write_track(3, 0, deltas, extended=False, integrity=False)
    assert [p[0] for p in s.written()['periods']] == deltas and s.state()['write_safe']
    s.close()
    print(f'sim_v2: {stubs} generated stubs behind {", ".join(UNITS)}; PING and INFO answered, '
          f'WRITE of {len(deltas)} intervals reached the disk as sent: {str(info)[:120]}')
