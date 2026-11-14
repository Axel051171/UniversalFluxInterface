"""Minimal UFI v2 PING/INFO probe; no drive or media commands.

Install software/ufi_host first. Run with --port COM5 (native 1209:4F54).
The serial entry point resets the CHANNEL before probing; close other clients.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
import json
import secrets

from ufi_host import protocol2 as P


def probe(stream, timeout: float = 2.0) -> dict:
    """Probe an already prepared byte stream; also usable with protocol doubles."""
    device = P.Device2(stream, timeout=timeout)
    if not device.ping(secrets.token_bytes(16)):
        raise ValueError('PING echo mismatch')
    info = device.device_info()
    if info.proto != 2:
        raise ValueError(f'unsupported protocol {info.proto}')
    if not 1 <= info.max_payload <= P.MAX_PAYLOAD:
        raise ValueError(f'invalid advertised payload limit {info.max_payload}')
    if info.sample_hz <= 0:
        raise ValueError('missing native sample clock')
    return asdict(info)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='native UFI CDC port, e.g. COM5')
    args = parser.parse_args()
    import serial
    from serial.tools import list_ports
    candidates = [p for p in list_ports.comports() if p.device.casefold() == args.port.casefold()]
    if not any((p.vid, p.pid) == (0x1209, 0x4F54) for p in candidates):
        parser.error('select the native UFI CDC port (1209:4F54); GW and composite need their own transports')
    with serial.Serial(args.port, baudrate=115200, timeout=0.1, write_timeout=2.0) as stream:
        P.channel_reset(stream)
        print(json.dumps(probe(stream), indent=2, ensure_ascii=False))


if __name__ == '__main__':
    main()
