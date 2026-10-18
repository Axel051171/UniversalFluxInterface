"""Native 0x0f operations 22 (capability matrix), 23 (configuration report), 24 and 26 (analysis
start bound to the configuration generation, op 26 also to the media generation); software
admission is not hardware qualification."""
import struct

CAPABILITY_UNCONFIRMED = 1
WRITE_MODE_UNQUALIFIED = 2
PROFILE_CHANGED = 4
NO_FLUX_PORT = 8
BUSY = 16
LINE_CONFLICT = 32
RATES = (250000, 300000, 500000, 1000000, 125000, 150000, 266667,
         285714, 307692, 380228, 346021, 312500, 280112, 251256)


def decode_capabilities(payload: bytes) -> dict:
    if len(payload) != 224 or payload[:4] != bytes((1, 6, 32, 32)):
        raise ValueError('unsupported capability matrix')
    generation, flags, supported_flags, timer_hz = struct.unpack_from('<4I', payload, 4)
    rows = []
    for offset in range(32, 224, 32):
        p = payload[offset:offset+32]
        rate_mask, classic_min, classic_max, extended_min, extended_max, row_flags = struct.unpack_from('<6I', p, 8)
        rows.append(dict(format=p[0], implemented=p[1], eligible=p[2],
                         offline_tested=p[3], hardware_qualified=p[4],
                         backends=p[5], eligible_backends=p[6], prerequisites=p[7],
                         rate_mask=rate_mask, rates=[r for i, r in enumerate(RATES) if rate_mask & (1 << i)],
                         classic_limits=(classic_min, classic_max),
                         extended_limits=(extended_min, extended_max), flags=row_flags))
    return dict(generation=generation, flags=flags, supported_flags=supported_flags,
                timer_hz=timer_hz, drive=payload[20], write_guard_discovery=payload[23],
                media_guarded_analysis=payload[24], multitrack_create=payload[25],
                multitrack_status=payload[26], multitrack_control=payload[27], rows=rows)


def decode_configuration(payload: bytes) -> dict:
    if len(payload) != 24 or payload[:3] != bytes((1, 24, 1)):
        raise ValueError('unsupported configuration report')
    generation, expected, flags, media_generation, supported_flags = struct.unpack_from('<5I', payload, 4)
    return dict(generation=generation, expected=expected, flags=flags,
                media_generation=media_generation, supported_flags=supported_flags)


def guarded_analysis(generation: int, start_payload: bytes, media_generation: int | None = None) -> bytes:
    """Wrap an existing Op1 payload; submit with Device2.request(0x0f, result)."""
    if not 0 < generation <= 0xffffffff or len(start_payload) < 28 or start_payload[0] != 1:
        raise ValueError('invalid generation or analysis start')
    if media_generation is not None:
        if not 0 < media_generation <= 0xffffffff:
            raise ValueError('invalid media generation')
        return bytes([26]) + struct.pack('<II', generation, media_generation) + start_payload
    return b'\x18' + struct.pack('<I', generation) + start_payload
