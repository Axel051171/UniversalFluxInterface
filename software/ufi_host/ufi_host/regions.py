"""Native analysis section 14. Flags describe observations, not physical weak bits."""
import struct

SUFFICIENT_TIMING = 1
RATE_CHANGE_CANDIDATE = 2
DECODE_ERRORS = 4
CONTENT_VARIATION = 8
MISSING_EDGES = 16
EXTRA_EDGES = 32
ALIGNMENT_UNCERTAIN = 64
INCOMPLETE = 128


def decode_regions(payload: bytes) -> dict:
    if len(payload) != 1312 or payload[:3] != bytes([1, 32, 40]):
        raise ValueError('unsupported or truncated local analysis report')
    generation, rate = struct.unpack_from('<II', payload, 4)
    minimum, threshold, baseline = struct.unpack_from('<III', payload, 16)
    names = ('flags', 'samples', 'mean_cell_q8', 'min_cell_q8', 'max_cell_q8',
             'rejected', 'error_events', 'content_conflicts', 'missing_edges', 'extra_edges')
    return dict(generation=generation, rate=rate, format=payload[12],
                completed_revolutions=payload[3], selected_flags=payload[13],
                compared_revolutions=payload[14], ambiguous=bool(payload[15]), minimum_samples=minimum,
                change_threshold_permille=threshold, baseline_cell_q8=baseline,
                windows=[dict(zip(names, struct.unpack_from('<10I', payload, 32+40*k)))
                         for k in range(32)])
