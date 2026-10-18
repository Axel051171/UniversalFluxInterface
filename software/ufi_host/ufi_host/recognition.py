"""Native analysis sections 15/16. Geometry is never inferred from one track."""
import struct

PROFILE_PRESENT = 1
FORMAT_OBSERVED = 2
SECTOR_SET_CONFIRMED = 4
GEOMETRY_UNCONFIRMED = 8
AMBIGUOUS = 16
HEADER_POSITION_MISMATCH = 32
CONTENT_CONFIRMED = 64
INCOMPLETE = 128
POSITION_CHECKED = 256
PROFILE_DECLARED = 512

FLAG_NAMES = {PROFILE_PRESENT: 'PROFILE_PRESENT', FORMAT_OBSERVED: 'FORMAT_OBSERVED',
              SECTOR_SET_CONFIRMED: 'SECTOR_SET_CONFIRMED', GEOMETRY_UNCONFIRMED: 'GEOMETRY_UNCONFIRMED',
              AMBIGUOUS: 'AMBIGUOUS', HEADER_POSITION_MISMATCH: 'HEADER_POSITION_MISMATCH',
              CONTENT_CONFIRMED: 'CONTENT_CONFIRMED', INCOMPLETE: 'INCOMPLETE',
              POSITION_CHECKED: 'POSITION_CHECKED', PROFILE_DECLARED: 'PROFILE_DECLARED'}
FORMAT_NAMES = {0: 'none', 1: 'IBM MFM', 2: 'Amiga', 3: 'IBM FM', 4: 'HP M2FM', 5: 'CBM GCR',
                6: 'Mac GCR', 7: 'Apple II GCR'}
DECISION_NAMES = {0: 'no retry', 1: 'capture again', 2: 'content confirmed', 3: 'stagnation stop',
                  4: 'attempt limit'}


def flag_names(flags: int) -> list[str]:
    """Names of the recognition flags set in `flags`, in bit order."""
    return [name for bit, name in FLAG_NAMES.items() if flags & bit]


CAUSE_NAMES = ('missing_sectors', 'header_errors', 'data_errors', 'coding_errors',
               'content_conflicts', 'transport_dma', 'buffer_full', 'position',
               'no_index', 'media_changed', 'timeout', 'cancelled',
               'confirmation_pending', 'incomplete_evidence', 'no_valid_format')


def _assessment(payload: bytes) -> dict:
    flags, causes, valid, mismatches = struct.unpack_from('<4I', payload)
    return dict(flags=flags, causes=causes, valid_sector_records=valid,
                position_mismatches=mismatches,
                cause_names=[name for i, name in enumerate(CAUSE_NAMES) if causes & (1 << i)])


def decode_recognition(payload: bytes) -> dict:
    if len(payload) != 64 or payload[0] != 1:
        raise ValueError('unsupported or truncated recognition report')
    result = _assessment(payload[4:20])
    expected, seen = struct.unpack_from('<QQ', payload, 20)
    generation, capture_id = struct.unpack_from('<II', payload, 52)
    result.update(format=payload[1], policy=payload[2], confirmations=payload[3],
                  expected=expected, seen=seen, media_profile=payload[36:48],
                  expected_c=payload[48], expected_h=payload[49], expected_n=payload[50],
                  expected_set_supplied=bool(payload[51]), generation=generation,
                  capture_id=capture_id, attempt=payload[60], phase=payload[61],
                  decision=payload[62], status=payload[63])
    return result


def decode_attempt_assessments(payload: bytes) -> list[dict]:
    if not payload or len(payload) % 20 or len(payload) > 160:
        raise ValueError('invalid attempt assessment size')
    records = []
    for start in range(0, len(payload), 20):
        row = payload[start:start+20]
        record = _assessment(row)
        record.update(phase=row[16], decision=row[17])
        records.append(record)
    return records
