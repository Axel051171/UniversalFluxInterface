"""UFI Flux Engine USB protocol (firmware/include/ufi_firmware.h, src/ufi_usb.c).

Host -> device: one packet per command, <= 64 bytes, byte 0 = command code, arguments
follow directly.  Device -> host: a byte stream where every message starts with
ufi_response_header_t {u8 command, u8 status, u16 length LE} + `length` payload bytes.
READ_TRACK streams {FLUX_STREAM, 0, len} + encoded bytes while the disk turns
(decode_stream), READ_TRACK_RAW sends {FLUX, 0, 12} + flux header + u32 samples per
revolution after the capture; both end with {READ_DONE, status, 1} + [revolutions].
"""
from __future__ import annotations

import struct
import time
from dataclasses import dataclass, field
from typing import Protocol

FLUX_CLOCK_HZ = 275_000_000          # TIM2/TIM3 tick
USB_VID, USB_PID = 0x1209, 0x4F54
MAX_REVOLUTIONS = 20                 # firmware REVOLUTIONS_BUFFER

# command codes (ufi_command_t)
NOP, GET_INFO, GET_STATUS = 0x00, 0x01, 0x02
SELECT_DRIVE, MOTOR_ON, MOTOR_OFF, SEEK, RECALIBRATE, SELECT_SIDE = 0x10, 0x11, 0x12, 0x13, 0x14, 0x15
CHECK_DISK, DRIVE_TIMING, AMIGA_ID, USB_POWER = 0x16, 0x17, 0x18, 0x19
TIMING_FIELDS = ("step_pulse_us", "step_rate_us", "settle_us", "dir_change_us",
                 "side_settle_us", "spinup_ms", "select_settle_us",
                 "motor_off_s", "double_step", "precomp_ns")   # precomp 0xFFFF = by data rate
TIMING = struct.Struct("<10H")
AMIGA_IDS = {0xFFFFFFFF: '3.5" DD', 0xAAAAAAAA: '3.5" HD (HD media)',
             0x55555555: '5.25" 40 track', 0x00000000: "no drive"}
READ_TRACK, READ_TRACK_RAW, EVT_READ_DONE, EVT_FLUX, ABORT_READ = 0x20, 0x21, 0x2D, 0x2E, 0x2F
EVT_FLUX_STREAM = 0x2C
WRITE_TRACK, ERASE_TRACK, WRITE_TRACK_VERIFY = 0x30, 0x31, 0x32
IEC_RESET, IEC_SEND, IEC_RECEIVE = 0x40, 0x41, 0x42
DEBUG_GPIO, DEBUG_TIMER, RESET, BOOTLOADER = 0xD0, 0xD1, 0xF0, 0xFF

DRIVES = {"none": 0, "a": 1, "b": 2, "apple": 3, "amiga": 4, "iec": 5,
          "ds0": 6, "ds1": 7, "ds2": 8, "ds3": 9}   # ds*: Shugart bus (straight cable)

# status byte = -ufi_error_t (firmware/include/ufi_fixes.h)
ERRORS = {
    1: "busy", 2: "no drive selected", 3: "seek failed", 4: "no index pulse",
    5: "timeout", 6: "DMA / verify error", 7: "USB error", 8: "IEC: no device (NRFD)",
    9: "IEC: no acknowledge", 10: "buffer full / overflow", 11: "not implemented",
    12: "write protected", 13: "storage (SD NAND) error", 0xFE: "reply too long",
    0xFF: "unknown command",
}

HDR = struct.Struct("<BBH")
FLUX_HDR = struct.Struct("<BBBBII")
STATUS = struct.Struct("<BBBBBBBBH")


class DeviceError(RuntimeError):
    def __init__(self, command: int, status: int):
        self.command, self.status = command, status
        super().__init__(f"command 0x{command:02X}: {ERRORS.get(status, f'error {status}')}")


class Stream(Protocol):
    def write(self, data: bytes) -> int | None: ...
    def read(self, size: int) -> bytes: ...


@dataclass
class Revolution:
    track: int
    side: int
    index: int
    flags: int
    index_ticks: int                 # revolution duration in 275 MHz ticks
    samples: list[int]               # flux timestamps, ticks since this revolution's index

    @property
    def rpm(self) -> float:
        return 60.0 * FLUX_CLOCK_HZ / self.index_ticks if self.index_ticks else 0.0

    @property
    def overflow(self) -> bool:
        return bool(self.flags & 0x02)


@dataclass
class Capture:
    revolutions: list[Revolution] = field(default_factory=list)
    status: int = 0                  # READ_DONE status (10 = store full, fewer revs)


def decode_stream(data: bytes, track: int = 0, side: int = 0, flags: int = 0x01) -> list[Revolution]:
    """Decode a READ_TRACK stream (format: firmware/src/ufi_stream.c) into revolutions.

    Time 0 is the first index pulse; a transition at or after index k belongs to
    revolution k, as in the firmware's RAW finalisation.
    """
    t, flux, index = 0, [], [0]
    i, n = 0, len(data)
    while i < n:
        b = data[i]
        if 0x01 <= b <= 0xEF:
            t += b
            flux.append(t)
            i += 1
        elif 0xF0 <= b <= 0xFC:
            t += 240 + ((b - 0xF0) << 8) + data[i + 1]
            flux.append(t)
            i += 2
        elif b in (0xFD, 0xFE):
            v = int.from_bytes(data[i + 1:i + 5], "little")
            if b == 0xFD:
                index.append(t + v)
            else:
                t += v
                flux.append(t)
            i += 5
        else:
            raise ValueError(f"bad stream byte 0x{b:02X} at {i}")
    revs, j = [], 0
    for r in range(len(index) - 1):
        start, end = index[r], index[r + 1]
        samples = []
        while j < len(flux) and flux[j] < end:
            samples.append(flux[j] - start)
            j += 1
        revs.append(Revolution(track, side, r, flags, end - start, samples))
    return revs


@dataclass
class DriveStatus:
    drive: int
    motor_on: bool
    write_protected: bool
    track0: bool
    disk_changed: bool
    ready: bool
    track: int
    side: int
    rpm: int


class Device:
    """Command layer over any byte stream (pyserial port or a test double)."""

    def __init__(self, stream: Stream, timeout: float = 5.0):
        self.s = stream
        self.timeout = timeout

    # -- framing ------------------------------------------------------------
    def _read_exact(self, n: int, timeout: float | None = None) -> bytes:
        deadline = time.monotonic() + (timeout if timeout is not None else self.timeout)
        buf = bytearray()
        while len(buf) < n:
            chunk = self.s.read(n - len(buf))
            if chunk:
                buf += chunk
            elif time.monotonic() > deadline:
                raise TimeoutError(f"device sent {len(buf)} of {n} bytes")
        return bytes(buf)

    def read_message(self, timeout: float | None = None) -> tuple[int, int, bytes]:
        cmd, status, length = HDR.unpack(self._read_exact(HDR.size, timeout))
        return cmd, status, self._read_exact(length, timeout) if length else b""

    def command(self, cmd: int, *args: int, timeout: float | None = None) -> bytes:
        pkt = bytes([cmd, *args])
        if len(pkt) > 64:
            raise ValueError("command packet longer than 64 bytes")
        self.s.write(pkt)
        rcmd, status, payload = self.read_message(timeout)
        if rcmd != cmd:
            raise DeviceError(rcmd, 0xFF) if status == 0 else DeviceError(rcmd, status)
        if status:
            raise DeviceError(cmd, status)
        return payload

    # -- commands -----------------------------------------------------------
    def info(self) -> list[str]:
        return [s.decode(errors="replace") for s in self.command(GET_INFO).split(b"\0") if s]

    def status(self) -> DriveStatus:
        v = STATUS.unpack(self.command(GET_STATUS))
        return DriveStatus(v[0], *(bool(x) for x in v[1:6]), v[6], v[7], v[8])

    def select(self, drive: str) -> None:
        self.command(SELECT_DRIVE, DRIVES[drive])

    def motor(self, on: bool) -> None:
        self.command(MOTOR_ON if on else MOTOR_OFF, timeout=3.0)

    def seek(self, track: int) -> None:
        self.command(SEEK, track, timeout=3.0)

    def recalibrate(self) -> None:
        self.command(RECALIBRATE, timeout=3.0)

    def side(self, side: int) -> None:
        self.command(SELECT_SIDE, side)

    def check_disk(self) -> tuple[bool, bool]:
        """-> (disk changed since last check, disk present); steps once to re-arm DSKCHG."""
        changed, present = self.command(CHECK_DISK, timeout=3.0)
        return bool(changed), bool(present)

    def timing(self, **changes: int) -> dict[str, int]:
        """Read drive timings; keyword arguments (TIMING_FIELDS) change them."""
        cur = dict(zip(TIMING_FIELDS, TIMING.unpack(self.command(DRIVE_TIMING))))
        if changes:
            unknown = set(changes) - set(TIMING_FIELDS)
            if unknown:
                raise ValueError(f"unknown timing field(s): {', '.join(sorted(unknown))}")
            cur.update(changes)
            cur = dict(zip(TIMING_FIELDS, TIMING.unpack(
                self.command(DRIVE_TIMING, *TIMING.pack(*(cur[f] for f in TIMING_FIELDS))))))
        return cur

    def usb_power(self) -> tuple[int, int, int]:
        """-> (CC1 mV, CC2 mV, allowed USB-C source current in mA; 0 = no Type-C source)."""
        return struct.unpack("<3H", self.command(USB_POWER))

    def amiga_id(self) -> tuple[int, str]:
        (v,) = struct.unpack("<I", self.command(AMIGA_ID))
        return v, AMIGA_IDS.get(v, "unknown")

    def read_track(self, track: int, side: int, revolutions: int = 3) -> Capture:
        """Streamed capture: the device sends while the disk turns (firmware ufi_stream.c)."""
        revolutions = max(1, min(revolutions, MAX_REVOLUTIONS))
        self.command(READ_TRACK, track, side, revolutions, timeout=5.0)
        data = bytearray()
        while True:
            cmd, status, payload = self.read_message(timeout=2.0 + 0.4 * revolutions)
            if cmd == EVT_FLUX_STREAM:
                data += payload
            elif cmd == EVT_READ_DONE:
                if status and status != 10:
                    raise DeviceError(READ_TRACK, status)
                flags = 0x01 | (0x02 if status == 10 else 0)
                return Capture(decode_stream(bytes(data), track, side, flags), status)
            else:
                raise DeviceError(cmd, status or 0xFF)

    def read_track_raw(self, track: int, side: int, revolutions: int = 3) -> Capture:
        """Capture first, then 4 bytes per transition (older firmware, fallback)."""
        revolutions = max(1, min(revolutions, MAX_REVOLUTIONS))
        self.command(READ_TRACK_RAW, track, side, revolutions, timeout=5.0)
        cap = Capture()
        while True:
            cmd, status, payload = self.read_message(timeout=2.0 + 0.4 * revolutions)
            if cmd == EVT_FLUX:
                t, sd, idx, flags, ticks, n = FLUX_HDR.unpack(payload)
                raw = self._read_exact(4 * n)
                cap.revolutions.append(Revolution(t, sd, idx, flags, ticks,
                                                  list(struct.unpack(f"<{n}I", raw))))
            elif cmd == EVT_READ_DONE:
                cap.status = status
                if status and status != 10:
                    raise DeviceError(READ_TRACK_RAW, status)
                return cap
            else:
                raise DeviceError(cmd, status or 0xFF)

    def write_track(self, track: int, side: int, deltas: list[int], verify: bool = False) -> None:
        """deltas: flux intervals in 275 MHz ticks (first one measured from the index)."""
        cmd = WRITE_TRACK_VERIFY if verify else WRITE_TRACK
        self.command(cmd, track, side, *struct.pack("<I", len(deltas)), timeout=5.0)
        data = struct.pack(f"<{len(deltas)}I", *deltas)
        for off in range(0, len(data), 64):
            self.s.write(data[off:off + 64])
        rcmd, status, _ = self.read_message(timeout=10.0)
        if status or rcmd != cmd:
            raise DeviceError(rcmd, status or 0xFF)

    def erase(self, track: int, side: int) -> None:
        self.command(ERASE_TRACK, track, side, timeout=3.0)

    def abort(self) -> None:
        self.command(ABORT_READ)

    def iec_reset(self) -> None:
        self.command(IEC_RESET, timeout=3.0)

    def iec_send(self, byte: int, eoi: bool = False) -> None:
        self.command(IEC_SEND, byte, int(eoi))

    def iec_receive(self) -> tuple[int, bool]:
        b, eoi = self.command(IEC_RECEIVE, timeout=1.0)
        return b, bool(eoi)

    def selftest(self) -> int:
        return self.command(DEBUG_GPIO, 3)[0]

    def rpm(self) -> int:
        return struct.unpack("<H", self.command(DEBUG_TIMER, 2, timeout=3.0))[0]

    def reset(self) -> None:
        self.command(RESET)

    def bootloader(self) -> None:
        self.command(BOOTLOADER)


def open_serial(port: str | None = None):
    """Open the UFI CDC port (auto-detected by VID/PID when port is None)."""
    import serial  # pyserial, imported lazily so tests run without it
    from serial.tools import list_ports

    if port is None:
        found = [p.device for p in list_ports.comports() if p.vid == USB_VID and p.pid == USB_PID]
        if not found:
            raise SystemExit("no UFI device found (VID 1209 / PID 4F54); use --port")
        port = found[0]
    return serial.Serial(port, timeout=0.2, write_timeout=5)
