"""Bounded coverage-guided tests of production code with libFuzzer.  Corpus and crash
artifacts stay in build/.

Targets:
  protection  flag configuration, bounded evidence collection, raw scanner and report paging
  parsers  HFEv3, HFE header, fragmented compact write codec, raw-track profiles,
           job and resource transitions (fuzz_parsers.c)
  v2       the whole UFI v2 dispatcher behind generated driver stubs (fuzz_v2.c)
  gw       the whole Greaseweazle-compatible protocol behind generated stubs (fuzz_gw.c)
  sd       the stand-alone SD module: image loaders, dump writers, UFI.CFG and the file
           API over an in-memory FatFs fed by the input (fuzz_sd.c)
  lan      the HAL-free LAN parsers: LAN.CFG text, DHCP replies, the address state
           machine against the W5500 fake, the TCP v2 session, discovery (fuzz_lan.c)
Stubs: fuzz_stubs.py from the header prototypes, HAL/FatFs declarations from hal_stub/.

usage: python run_fuzz.py [--target parsers|v2|gw|sd|all] [--seconds N] [--replay]
--replay only executes the stored corpus once (used by coverage.py).
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

from run_tests import BUILD, FW, compiler, extra_cflags
import fuzz_stubs

COMMON = ['-std=c11', '-O1', '-g', '-fno-omit-frame-pointer', '-D_CRT_SECURE_NO_WARNINGS',
          '-I', str(FW / 'include'), '-I', str(FW / 'tests')]
STUB_FLAGS = ['-I', str(FW / 'tests/hal_stub'), '-D__packed=__attribute__((packed))',
              '-DUFI_FW_VERSION="fuzz"', '-DUFI_GIT_REV="fuzz"']
EXE = '.exe' if sys.platform == 'win32' else ''
LINKER_SYMBOLS = {s: (None, f'uint32_t {s};') for s in ('_estack', '_edtcm', '__heap_start', '__heap_end')}
LIBC = {'memcpy', 'memmove', 'memset', 'memcmp', 'strlen', 'strncpy', 'strcmp', 'strncmp', 'snprintf',
        'strchr', 'strrchr', 'strstr', 'strcpy', 'strcat', 'strncat', 'abs', 'rand', 'malloc', 'free',
        '__chkstk', '__stack_chk_fail', '__stack_chk_guard', 'assert', '_assert', '__assert_fail',
        '_wassert', 'abort', 'puts', 'printf', 'fprintf', 'vsnprintf', 'getenv', 'exit', 'fputs', 'fwrite',
        'fputc', 'stderr', '__security_cookie', '__security_check_cookie', '__GSHandlerCheck',
        '__C_specific_handler'}
LIBC_PREFIXES = ('___', '__acrt', '__stdio', '__local_stdio', '_errno')


def bits(values):
    return bytes(int(f'{v:08b}'[::-1], 2) for v in values)


def record(flags, cmd, seq, payload=b''):
    return bytes([flags, cmd, seq, len(payload) & 255, len(payload) >> 8]) + payload


def gw(cmd, *args):
    return bytes([cmd, 2 + len(args), *args])


PARSER_SEEDS = (bits([0xf2, 72, 0x80, 0xf1, 0xf2, 36, 0x40, 0xf3, 7, 1]),
                bits([0xf2, 72, 0xf4, 0xf3, 5, 0xf4]), bits([0xf2, 72, 0xf1, 0x80, 0xf1, 0x80]))

V2_SEEDS = (
    b'\x00' + record(4,0x0d,35,b'\x02'),  # real bounded cycle/loss diagnostic
    b'\x00' + record(4,0x0d,34,b'\x01'),  # real bounded performance provider, not an unconstrained generated length
    b'\x00' + record(4, 0x00, 1, b'ping') + record(4, 0x01, 2) + record(4, 0x02, 3) + record(4, 0x09, 4),
    b'\x00' + record(4, 0x0F, 1, b'\x00') + record(4, 0x0F, 2, b'\x14') + record(4, 0x0C, 3) + record(4, 0x0D, 4),
    b'\x00' + record(4, 0x20, 1, bytes([0, 0, 2, 0, 0, 0])) + record(0x08, 0, 0) + record(4, 0x0E, 2),
    b'\x00' + record(4, 0x22, 1, bytes([0, 0, 0, 3, 0, 0, 0, 6, 0, 0, 0])) + record(5, 0x22, 1, b'\xf2\x48\xf1\x80\xf1\x80')
    + record(0x18, 0, 0),
    b'\x00' + record(4, 0x50, 1, b'\x00\x00') + record(4, 0x51, 2, b'\x00\x00\x00\x00\x10\x00UFI.CFG')
    + record(4, 0x33, 3, b'\x01\x00\x03\x05UI') + record(4, 0x36, 4, b'\x01\x08\x01\x00\x03\x05'),
    b'\x00' + record(4, 0x60, 1, b'\x01\x50\x02\x03\x04') + record(4, 0x61, 2) + record(4, 0x62, 3)
    + record(0x24, 0x82, 0, b'\x01\x02') + record(4, 0x06, 4, b'\xff\xff\x00\x00'),
    b'\x01\x55\xaa\x01\x00\x07\x00\x00\x00\x12\x34\x55\xaa',
    b'\x00' + record(4, 0x48, 1) + record(4, 0x48, 2, b'x') + record(4, 0x49, 3),   # L5 NET, L4 AUTH over USB
    # integrity job around a READ: open (kind 1, id 0x1234), READ, end, status
    b'\x00' + record(4, 0x0B, 1, bytes([1, 1, 0x34, 0x12, 0, 0, 0, 0, 0, 0])) + record(4, 0x20, 2, bytes([5, 1, 3, 0, 0, 0]))
    + record(0x0C, 0, 0) + record(4, 0x0B, 3),
    # integrity job around a WRITE: open (kind 2), WRITE, DATA with 16-byte job header, commit, end
    b'\x00' + record(4, 0x0B, 1, bytes([1, 2, 0x78, 0x56, 0, 0, 0, 0, 0, 0])) + record(4, 0x22, 2, bytes([0, 0, 0, 3, 0, 0, 0, 6, 0, 0, 0]))
    + record(5, 0x22, 2, bytes([0x78, 0x56, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0]) + b'\xf2\x48\xf1\x80\xf1\x80')
    + record(4, 0x0B, 3, bytes([2, 0, 0, 0, 0])) + record(0x1C, 0, 0),
    # guarded write after one configuration change (generation 1), IEC block write with 256 data bytes, clear comms
    b'\x00' + record(4, 0x10, 9, b'\x01') + record(4, 0x27, 1, bytes([1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 6, 0, 0, 0]))
    + record(4, 0x35, 2, bytes([1, 18, 0, 8]) + bytes(256)) + record(0x40, 0, 0) + record(4, 0x00, 3),
    b'\x02' + record(4, 0x10, 1, b'\x01') + record(4, 0x20, 2, bytes([0, 0, 1, 0, 0, 0])) + record(4, 0x0A, 3, b'\x01'),
    # L4: LAN (odd seq) selects and reads, USB (even seq) is busy, then the LAN session ends
    b'\x04' + record(4, 0x10, 1, b'\x01') + record(4, 0x20, 3, bytes([0, 0, 1, 0, 0, 0])) + record(4, 0x12, 2, b'\x05')
    + record(4, 0x51, 5, b'\x00\x00\x00\x00\x10\x00LAN.CFG') + record(4, 0x04, 7) + record(0x44, 0, 9) + record(4, 0x12, 4, b'\x05'),
)

# Greaseweazle: GET_INFO 0, SEEK 2, HEAD 3, SET_PARAMS 4, GET_PARAMS 5, MOTOR 6, READ_FLUX 7,
# WRITE_FLUX 8, GET_FLUX_STATUS 9, SELECT 12, DESELECT 13, SET_BUS_TYPE 14, SET_PIN 15, RESET 16,
# ERASE_FLUX 17, SOURCE_BYTES 18, SINK_BYTES 19, GET_PIN 20, NOCLICK_STEP 22
GW_SEEDS = (
    b'\x00' + gw(0, 0) + gw(0, 1) + gw(14, 1) + gw(12, 0) + gw(6, 0, 1) + gw(2, 5) + gw(3, 1) + gw(5, 0, 16),
    b'\x00' + gw(14, 1) + gw(12, 0) + gw(7, 0, 0, 0, 0, 2, 0) + gw(9) + gw(13) + gw(16),
    b'\x00' + gw(14, 2) + gw(12, 1) + gw(8, 1, 1) + bytes([40, 60, 80, 0xFF, 2, 1, 1, 1, 1, 250, 3, 100, 0]) + gw(9),
    b'\x00' + gw(14, 1) + gw(12, 0) + gw(17, 0x40, 0x42, 0x0F, 0) + gw(22) + gw(15, 2, 1) + gw(20, 26),
    b'\x00' + gw(18, 0, 2, 0, 0, 1, 2, 3, 4) + gw(19, 3, 0, 0, 0, 1, 2, 3, 4) + bytes([0x10, 0x20, 0x30]) + gw(0, 7),
    b'\x00' + gw(4, 0, 100, 0, 200, 0, 15, 0, 250, 0, 0x10, 0x27, 0, 0, 0, 0, 0, 0) + gw(12, 0) + gw(6, 0, 1),
    b'\x01' + gw(14, 1) + gw(12, 0) + gw(2, 1),
)

STUBBED = {
    'v2': dict(harness='fuzz_v2.c', units=('ufi_v2',),
               real=('ufi_job', 'ufi_resources', 'ufi_cancel', 'ufi_capabilities', 'ufi_media', 'ufi_metrics',
                     'ufi_write_codec', 'ufi_quality','ufi_protection', 'ufi_lan_status', 'ufi_lan_cfg'),
               extra={'g_capture': ('ufi_firmware.h', 'capture_context_t g_capture;'), **LINKER_SYMBOLS}),
    'gw': dict(harness='fuzz_gw.c', units=('ufi_gw',), real=('ufi_resources', 'ufi_cancel'),
               extra={'g_capture': ('ufi_firmware.h', 'capture_context_t g_capture;'),
                      'hUsbDevice': ('usbd_def.h', 'USBD_HandleTypeDef hUsbDevice;'), **LINKER_SYMBOLS}),
    'sd': dict(harness='fuzz_sd.c', units=('ufi_dump',),
               real=('ufi_image', 'ufi_hfe_v3', 'ufi_mfm', 'ufi_verify', 'ufi_quality','ufi_protection', 'ufi_compare',
                     'ufi_write_codec', 'ufi_resources', 'ufi_cancel', 'ufi_capabilities', 'ufi_media'),
               extra=dict(LINKER_SYMBOLS)),
    'drive': dict(harness='fuzz_drive.c', units=('ufi_drive',),
                  real=('ufi_drive_extra', 'ufi_media', 'ufi_capabilities', 'ufi_verify', 'ufi_mac'),
                  extra=dict(LINKER_SYMBOLS)),
}

# drive scripts: byte 0 = first drive type, then one operation per byte (see fuzz_drive.c)
DRIVE_SEEDS = (bytes([1, 4, 2, 2, 3, 3, 2, 4, 5, 1]), bytes([6, 1, 2, 2, 2, 6, 2, 4]),
               bytes([2, 8, 9, 15, 10, 11, 0, 11]), bytes([3, 20, 21, 4, 2, 16]),
               bytes([7, 12, 19, 13, 14, 22, 23, 17, 18]))

# LAN parsers: selector byte, then the input (fuzz_lan.c)
LAN_SEEDS = (b'\x00lan=on\r\nip=dhcp\r\nmask=255.255.255.0\r\ngw=\r\ndns=\r\nhostname=\r\nmac=\r\nport=55100\r\n'
             b'lan_write=0\r\nlan_key=\r\n',
             b'\x00ip=192.168.1.50\nmask=255.255.0.0 # x\nGW=192.168.1.1\nmac=02:ab:cd:01:02:03\nlan_key=k#c\n',
             b'\x00hostname=Lab-1\nport=65535\nlan_write=1\nlan=off\n=5\n# c\n')


def dhcp_seeds():
    """DHCP OFFER/ACK for the first transaction of fuzz_lan.c's state machine: MAC
    02:20:64:02:ca:b3, xid = first xorshift32 step of FNV-1a(MAC) ^ now (1), as ufi_net.c."""
    mac = bytes([0x02, 0x20, 0x64, 0x02, 0xca, 0xb3])
    x = 2166136261
    for b in mac:
        x = ((x ^ b) * 16777619) & 0xFFFFFFFF
    x ^= (x << 13) & 0xFFFFFFFF
    x ^= x >> 17
    x ^= (x << 5) & 0xFFFFFFFF
    xid = x ^ 1

    def reply(kind):
        m = bytearray(300)
        m[0:3] = b'\x02\x01\x06'
        m[4:8] = xid.to_bytes(4, 'big')
        m[16:20] = bytes([192, 168, 1, 77])
        m[28:34] = mac
        m[236:240] = bytes([0x63, 0x82, 0x53, 0x63])
        o = bytes([53, 1, kind, 54, 4, 192, 168, 1, 1, 1, 4, 255, 255, 255, 0, 3, 4, 192, 168, 1, 1,
                   51, 4, 0, 0, 0x0e, 0x10, 255])
        m[240:240 + len(o)] = o
        return bytes(m)
    offer, ack = reply(2), reply(5)
    return (b'\x01' + xid.to_bytes(4, 'big') + ack,
            b'\x02' + bytes([0x10, 44]) + offer + bytes([1, 10, 0x10, 44]) + ack + bytes([1, 50, 1, 255, 1, 255]))


LAN_SEEDS += dhcp_seeds() + (b'\x02' + bytes([1, 255]) * 3 + bytes([2, 0, 1, 255, 3, 0, 1, 100]),)


def v2_frame(ftype, cmd, seq, payload=b''):
    """55 AA type cmd seq status len16 payload crc16 (CRC-16/CCITT-FALSE over type..payload)."""
    body = bytes([ftype, cmd, seq, 0, len(payload) & 255, len(payload) >> 8]) + payload
    crc = 0xFFFF
    for b in body:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return b'\x55\xaa' + body + bytes([crc & 255, crc >> 8])


def session_seed(flags, stream):
    """selector 3, flags, then the stream in chunks of up to 64 bytes"""
    out = bytes([3, flags])
    for i in range(0, len(stream), 64):
        chunk = stream[i:i + 64]
        out += bytes([len(chunk) - 1]) + chunk
    return out


LAN_SEEDS += (session_seed(0xFE, v2_frame(1, 0, 1, b'UFI2') + b'\x01\x55' + v2_frame(4, 0x22, 2, b'\xf2\x48')),
              session_seed(0xFF, v2_frame(1, 0x49, 1) + v2_frame(1, 0x49, 2, bytes(32)) + v2_frame(1, 0, 3)),
              b'\x04' + bytes([8]) + b'UFI?\x01\x00\x00\x00' + bytes([0x88]) + b'UFI?\x01\x00\x00\x00' + bytes([3]) + b'UFI')


def scp_seed():
    """SCP with one track of eight 2 us cells; flags bit 4 = mutable image without checksum."""
    hdr = b'SCP' + bytes([0x19, 0x80, 1, 0, 1, 0x10, 0, 0, 0]) + bytes(4)
    table = (688).to_bytes(4, 'little') + bytes(4 * 167)
    trk = b'TRK' + bytes([0]) + (8000000).to_bytes(4, 'little') + (8).to_bytes(4, 'little') + (16).to_bytes(4, 'little')
    return b'\x00' + hdr + table + trk + bytes([0, 80]) * 8


def hfe_seed():
    """HFE v1, one track, one side, 250 kbit/s, LUT in block 1, data in block 2."""
    hdr = b'HXCPICFE' + bytes([0, 1, 1, 0]) + (250).to_bytes(2, 'little') + (300).to_bytes(2, 'little') \
        + bytes([7, 1]) + (1).to_bytes(2, 'little') + bytes([0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF])
    lut = (2).to_bytes(2, 'little') + (512).to_bytes(2, 'little')
    return b'\x01' + hdr.ljust(512, b'\xff') + lut.ljust(512, b'\xff') + bytes([0x49, 0x24, 0x92] * 170 + [0x49, 0x24])


def hfe_v3_seed():
    """HFE v3 signature, one track; the stream starts with SETBITRATE then data bytes."""
    s = hfe_seed()
    return s[:1] + b'HXCHFEV3' + s[9:1025] + bytes([0xF2, 0x36]) + s[1027:]


SD_SEEDS = (scp_seed(), hfe_seed(), hfe_v3_seed(), b'\x02' + bytes(1024), b'\x03' + bytes(512), b'\x04' + bytes(256),
            b'\x05' + bytes(256),
            b'\x06drive=a\ntracks=80\nsides=2\nrevs=3\nformat=hfe\nbutton_a=write\nwrite_file=GAME.ADF\n# x\nrpm_line=density\n',
            b'\x06drive=b\ntracks=0\ntrack_search=1\ndouble_step=auto\nauto_dump=1\ncopy_from=a\ncopy_to=b\nformat=img\n'
            b'index_sim=300\nprotocol=gw\ntg43=1\niec_device=9\n',
            b'\x07' + bytes(range(256)) * 4)


def fuzzer_env():
    env = os.environ.copy()
    if sys.platform == 'win32':
        resource = subprocess.check_output([compiler(), '-print-resource-dir'], text=True).strip()
        env['PATH'] = str(Path(resource) / 'lib/windows') + os.pathsep + env.get('PATH', '')
    return env


def sanitizer_flags():
    sanitize = 'fuzzer,address' if sys.platform == 'win32' else 'fuzzer,address,undefined'
    return [f'-fsanitize={sanitize}'] + [f for f in extra_cflags() if not f.startswith('-fsanitize')]


def build_parsers():
    exe = BUILD / ('fuzz_parsers' + EXE)
    subprocess.run([compiler(), *COMMON, *sanitizer_flags(), str(FW / 'tests/fuzz_parsers.c'),
                    *[str(FW / 'src' / f) for f in ('ufi_hfe_v3.c', 'ufi_write_codec.c', 'ufi_image.c',
                                                      'ufi_raw_verify.c', 'ufi_job.c', 'ufi_resources.c')],
                    '-o', str(exe)], check=True)
    return exe


def build_protection():
    exe=BUILD/('fuzz_protection'+EXE)
    subprocess.run([compiler(),*COMMON,*sanitizer_flags(),str(FW/'tests/fuzz_protection.c'),
                    *[str(FW/'src'/f) for f in ('ufi_protection.c','ufi_quality.c','ufi_mfm.c','ufi_verify.c','ufi_compare.c','ufi_metrics.c')],
                    '-o',str(exe)],check=True)
    return exe


LAN_SOURCES = ('ufi_lan_cfg.c', 'ufi_net.c', 'ufi_w5500.c', 'ufi_sha256.c', 'ufi_lan_session.c', 'ufi_lan_status.c')


def build_lan():
    exe = BUILD / ('fuzz_lan' + EXE)
    subprocess.run([compiler(), *COMMON, *sanitizer_flags(), str(FW / 'tests/fuzz_lan.c'),
                    *[str(FW / 'src' / f) for f in LAN_SOURCES], '-o', str(exe)], check=True)
    return exe


def build_stubbed(name):
    """Whole production units + real HAL-free modules + generated stubs for the rest."""
    spec = STUBBED[name]
    objdir = BUILD / f'fuzz_{name}_obj'
    objdir.mkdir(parents=True, exist_ok=True)
    flags = [*COMMON, *sanitizer_flags(), *STUB_FLAGS]
    objects, harness = [], objdir / 'harness.o'
    for unit in (*spec['units'], *spec['real']):
        o = objdir / (unit + '.o')
        subprocess.run([compiler(), *flags, '-w', '-c', str(FW / 'src' / (unit + '.c')), '-o', str(o)], check=True)
        objects.append(o)
    subprocess.run([compiler(), *flags, '-Wall', '-Wextra', '-Werror', '-c', str(FW / 'tests' / spec['harness']),
                    '-o', str(harness)], check=True)
    stubs = objdir / 'stubs.c'
    stubbed, skipped = fuzz_stubs.generate(objects, stubs, harness_objects=[harness], extra=spec['extra'])
    unknown = [s for s in skipped if s not in LIBC and not s.startswith(LIBC_PREFIXES)]
    if unknown:
        raise SystemExit(f'fuzz_{name}: no prototype found for ' + ', '.join(unknown))
    print(f'fuzz_{name}: {len(stubbed)} generated stubs, {len(spec["real"])} real modules behind '
          + ', '.join(spec['units']))
    exe = BUILD / (f'fuzz_{name}' + EXE)
    subprocess.run([compiler(), *flags, '-std=c23', '-w', str(stubs), str(harness), *map(str, objects),
                    '-o', str(exe)], check=True)
    return exe


TARGETS = {'parsers': (build_parsers, PARSER_SEEDS, 'fuzz_corpus', 32767),
           'protection': (build_protection,(bytes([1,63,1,0,30,2,0,0])+bytes(range(256)),
                                           bytes([55,1,63,0,3,2,30,0])), 'fuzz_protection_corpus',2048),
           'v2': (lambda: build_stubbed('v2'), V2_SEEDS, 'fuzz_v2_corpus', 8192),
           'gw': (lambda: build_stubbed('gw'), GW_SEEDS, 'fuzz_gw_corpus', 4096),
           'sd': (lambda: build_stubbed('sd'), SD_SEEDS, 'fuzz_sd_corpus', 8192),
           'drive': (lambda: build_stubbed('drive'), DRIVE_SEEDS, 'fuzz_drive_corpus', 1024),
           'lan': (build_lan, LAN_SEEDS, 'fuzz_lan_corpus', 4096)}


def run_target(name, seconds, replay):
    build, seeds, corpus_name, max_len = TARGETS[name]
    corpus = BUILD / corpus_name
    corpus.mkdir(parents=True, exist_ok=True)
    for i, seed in enumerate(seeds):
        (corpus / f'seed{i}').write_bytes(seed)
    exe = build()
    log = BUILD / f'{name}_fuzz_result.log'
    args = [str(exe), str(corpus), f'-max_len={max_len}', '-timeout=5', '-seed=12345',
            f'-artifact_prefix={BUILD}/{name}-']
    args += ['-runs=0'] if replay else [f'-max_total_time={seconds}']
    with log.open('w') as out:
        result = subprocess.run(args, stdout=out, stderr=out, timeout=seconds + 120, env=fuzzer_env())
    print('\n'.join(log.read_text(errors='replace').splitlines()[-12:]))
    result.check_returncode()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--target', choices=('parsers','protection', 'v2', 'gw', 'sd', 'drive', 'lan', 'all'), default='all')
    parser.add_argument('--seconds', type=int, default=30)
    parser.add_argument('--replay', action='store_true', help='execute the stored corpus once, no mutation')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600:
        parser.error('seconds must be 1..3600')
    for name in (tuple(TARGETS) if args.target == 'all' else (args.target,)):
        run_target(name, args.seconds, args.replay)


if __name__ == '__main__':
    main()
