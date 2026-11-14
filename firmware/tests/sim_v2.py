"""Build and drive the firmware simulator sim_v2.c: the production v2 dispatcher, stream
encoder and READ completion as a native process, for host-in-the-loop tests.

build() compiles it like the v2 fuzz target (run_fuzz.build_stubbed) but without libFuzzer
and sanitizer runtime, and returns the executable.  SimStream is a byte stream for
ufi_host.protocol2.Device2: what the host writes arrives at the firmware (optionally cut into
fragments), reading lets simulated time pass and returns what the firmware transmitted.

usage: python sim_v2.py        builds and runs a smoke check (PING, INFO)
"""
from pathlib import Path
import struct
import subprocess
import sys

from run_tests import BUILD, FW, compiler
import fuzz_stubs
from run_fuzz import COMMON, STUB_FLAGS, EXE, LINKER_SYMBOLS, LIBC, LIBC_PREFIXES

UNITS = ('ufi_v2', 'ufi_stream', 'ufi_analysis_job')
# ufi_usb.c needs the USB device library and is not built natively; the one function of it on
# the READ path is taken from the source verbatim, so the simulator still runs production code.
EXTRACT_HEAD = r'''
#include "ufi_firmware.h"
#include "ufi_read_info.h"
#include "ufi_evidence.h"
static int reply(uint8_t cmd, uint8_t status, const void* p, uint16_t n) { (void)cmd; (void)status; (void)p; (void)n; return UFI_OK; }
static uint8_t st(int e) { return (uint8_t)(e < 0 ? -e : e); }
'''
REAL = ('ufi_job', 'ufi_resources', 'ufi_cancel', 'ufi_capabilities', 'ufi_media', 'ufi_metrics',
        'ufi_write_codec', 'ufi_quality', 'ufi_protection', 'ufi_lan_status', 'ufi_lan_cfg', 'ufi_read_info')
EXTRA = {'g_capture': ('ufi_firmware.h', 'capture_context_t g_capture;'),
         'hUsbDevice': ('usbd_def.h', 'USBD_HandleTypeDef hUsbDevice;'), **LINKER_SYMBOLS}


def build():
    objdir = BUILD / 'sim_v2_obj'
    objdir.mkdir(parents=True, exist_ok=True)
    flags = [*COMMON, *STUB_FLAGS]
    objects, harness = [], objdir / 'harness.o'
    for unit in (*UNITS, *REAL):
        o = objdir / (unit + '.o')
        subprocess.run([compiler(), *flags, '-w', '-c', str(FW / 'src' / (unit + '.c')), '-o', str(o)], check=True)
        objects.append(o)
    from test_regressions import function
    extract = objdir / 'extract.c'
    extract.write_text(EXTRACT_HEAD + function('ufi_usb.c', 'ufi_usb_send_read_done'), encoding='utf-8')
    o = objdir / 'extract.o'
    subprocess.run([compiler(), *flags, '-w', '-c', str(extract), '-o', str(o)], check=True)
    objects.append(o)
    subprocess.run([compiler(), *flags, '-Wall', '-Wextra', '-Werror', '-c', str(FW / 'tests' / 'sim_v2.c'),
                    '-o', str(harness)], check=True)
    stubs = objdir / 'stubs.c'
    stubbed, skipped = fuzz_stubs.generate(objects, stubs, harness_objects=[harness], extra=EXTRA)
    libc = LIBC | {'_fileno', '_setmode', 'fflush', 'fread', 'stdin', 'stdout'}
    unknown = [s for s in skipped if s not in libc and not s.startswith(LIBC_PREFIXES)]
    if unknown:
        raise SystemExit('sim_v2: no prototype found for ' + ', '.join(unknown))
    exe = BUILD / ('sim_v2' + EXE)
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
        self._cmd(b'S' + struct.pack('<III', rev_ticks, cell_ticks, lead_ms))

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
        owner, route, capture, write_pending, opened = struct.unpack('<BBBBI', self._cmd(b'Z'))
        return dict(owner=owner, route=route, capture=capture, write_pending=bool(write_pending), write_enables=opened)

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
    s.close()
    print(f'sim_v2: {stubs} generated stubs behind {", ".join(UNITS)}; PING and INFO answered: {str(info)[:120]}')
