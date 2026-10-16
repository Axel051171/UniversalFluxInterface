"""Format-aware host sector readback, not a raw-flux identity claim."""
from binascii import crc_hqx
from .hfe import Pll

SYNC = bytes(int(c) for c in f"{0x4489:016b}"*3)
RATES = (250_000, 300_000, 500_000, 1_000_000)


class AmigaReference(dict):
    """Full trackdisk reference including labels and sectors-to-gap metadata."""
    format = 'Amiga MFM (data, labels and header metadata)'

    def __init__(self, sectors, track, spt):
        super().__init__(sectors)
        self.track, self.spt = track, spt


def decode_ibm(deltas, rate):
    pll = Pll(rate)
    bits = bytearray()
    for delta in deltas:
        if not 0 < delta <= 0xffffffff:
            raise ValueError("invalid flux interval for readback")
        # A long silent gap cannot contain a valid mark. Preserve a separating
        # gap without allocating millions of cells for a no-flux interval.
        cells = min(pll.cells(delta), 128)
        bits.extend(bytes(cells-1));bits.append(1)
        if len(bits)>8_000_000:
            raise ValueError("track too large for bounded host verification")

    def field(pos, length):
        if pos+length*16 > len(bits):
            return None
        out = bytearray()
        for start in range(pos,pos+length*16,16):
            value=0
            for k in range(start+1,start+16,2):value=(value<<1)|bits[k]
            out.append(value)
        return bytes(out)

    sectors, pending, cursor = {}, None, 0
    while True:
        at=bits.find(SYNC,cursor)
        if at<0:break
        cursor=at+len(SYNC)
        marker=field(cursor,1)
        if marker==b'\xfe':
            header=field(cursor,7)
            pending=None
            if header and crc_hqx(b'\xa1'*3+header,0xffff)==0 and header[4]<=6:
                pending=tuple(header[1:5])
                cursor+=7*16
        elif marker in (b'\xfb',b'\xf8') and pending is not None:
            length=128<<pending[3]
            data=field(cursor,length+3)
            if data and crc_hqx(b'\xa1'*3+data,0xffff)==0:
                value=(data[0],data[1:-2])
                if pending in sectors and sectors[pending]!=value:
                    raise ValueError("conflicting duplicate IBM sector; verification is ambiguous")
                sectors[pending]=value
                cursor+=(length+3)*16
            pending=None
    return sectors


def reference(deltas, *, index_ticks=None):
    best, best_rate = {}, 0
    for rate in RATES:
        sectors=decode_ibm(deltas,rate)
        if len(sectors)>len(best):best,best_rate=sectors,rate
    if best:
        return best_rate,best
    from .amiga import decode, unique
    prefix = None
    if index_ticks is not None:
        if not isinstance(index_ticks, int) or index_ticks < sum(deltas) or not deltas:
            raise ValueError('invalid source revolution duration; no disk was written')
        prefix = len(deltas)
        deltas = deltas + [index_ticks-sum(deltas)+deltas[0]] + deltas[1:]
    for rate in RATES[:3]:
        result = decode(deltas, rate, prefix_intervals=prefix)
        sectors = unique(result)
        if not sectors:
            continue
        tracks = {key[0] for key in sectors}
        spt = 22 if any(key[1] >= 11 or value.to_gap > 11 for key,value in sectors.items()) else 11
        if len(tracks) != 1 or len(sectors) != spt:
            raise ValueError('incomplete or mixed Amiga source track; no disk was written')
        track = next(iter(tracks))
        if set(sectors) != {(track, s) for s in range(spt)}:
            raise ValueError('invalid Amiga source sector set; no disk was written')
        return rate, AmigaReference(sectors, track, spt)
    raise ValueError("automatic extended readback requires valid IBM or Amiga MFM sectors; no disk was written")


def verify_capture(expected, rate, capture, *, quality=None):
    if capture.status or not capture.revolutions:
        raise ValueError("readback failed or incomplete")
    if isinstance(expected, AmigaReference):
        from .amiga import analyze_capture
        report, found = analyze_capture(capture, rate, expected.track, expected.spt)
        if quality is not None:
            quality.update(report)
        if report['capture_overflow']:
            raise ValueError('readback capture overflow')
        if report['conflicting_sectors']:
            raise ValueError('conflicting Amiga readback variants')
        if report['unexpected_sectors']:
            raise ValueError('readback contains unexpected Amiga sectors')
        for key, value in found.items():
            if key in expected and value != expected[key]:
                raise ValueError(f'readback content/label/header mismatch for Amiga sector {key}')
        missing = set(expected) - set(found)
        if missing:
            raise ValueError(f'readback missing {len(missing)} checksum-valid Amiga sector(s)')
        return len(expected)
    found={}
    for rev in capture.revolutions:
        prev=0;deltas=[]
        for timestamp in rev.samples:
            deltas.append(timestamp-prev);prev=timestamp
        sectors=decode_ibm(deltas,rate)
        for key,value in sectors.items():
            if key in expected and value!=expected[key]:
                raise ValueError(f"readback content mismatch for IBM sector {key}")
        found.update(sectors)
    missing=set(expected)-set(found)
    if missing:
        raise ValueError(f"readback missing {len(missing)} CRC-valid IBM sector(s)")
    return len(expected)
