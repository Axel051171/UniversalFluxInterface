# Extracted verbatim from Greaseweazle usb.py (public domain).
# Commit 26690f89967d519e0106ab9566019a026b920bb4
# https://github.com/keirf/greaseweazle/blob/26690f89967d519e0106ab9566019a026b920bb4/src/greaseweazle/usb.py
import itertools as it
from typing import List, Tuple
class FluxOp:
    Index = 1
    Space = 2
    Astable = 3

def _encode_flux(self, flux: List[int]) -> bytes:
    nfa_thresh = round(150e-6 * self.sample_freq)  # 150us
    nfa_period = round(1.25e-6 * self.sample_freq) # 1.25us
    dat = bytearray()
    def _write_28bit(x):
        dat.append(1 | (x<<1) & 255)
        dat.append(1 | (x>>6) & 255)
        dat.append(1 | (x>>13) & 255)
        dat.append(1 | (x>>20) & 255)
    # Emit a dummy final flux value. This is never written to disk because
    # the write is aborted immediately the final flux is loaded into the
    # WDATA timer. The dummy flux is sacrificial, ensuring that the real
    # final flux gets written in full.
    dummy_flux = round(100e-6 * self.sample_freq)
    for val in it.chain(flux, [dummy_flux]):
        if val == 0:
            pass
        elif val < 250:
            dat.append(val)
        elif val > nfa_thresh:
            dat.append(255)
            dat.append(FluxOp.Space)
            _write_28bit(val)
            dat.append(255)
            dat.append(FluxOp.Astable)
            _write_28bit(nfa_period)
        else:
            high = (val-250) // 255
            if high < 5:
                dat.append(250 + high)
                dat.append(1 + (val-250) % 255)
            else:
                dat.append(255)
                dat.append(FluxOp.Space)
                _write_28bit(val - 249)
                dat.append(249)
    dat.append(0) # End of Stream
    return dat

class error:
    Fatal = ValueError

def _decode_flux(self, dat: bytes) -> Tuple[List[float], List[float]]:
    flux: List[float] = []
    index: List[float] = []
    assert dat[-1] == 0
    dat_i = it.islice(dat, 0, len(dat)-1)
    ticks, ticks_since_index = 0, 0
    def _read_28bit():
        val =  (next(dat_i) & 254) >>  1
        val += (next(dat_i) & 254) <<  6
        val += (next(dat_i) & 254) << 13
        val += (next(dat_i) & 254) << 20
        return val
    try:
        while True:
            i = next(dat_i)
            if i == 255:
                opcode = next(dat_i)
                if opcode == FluxOp.Index:
                    val = _read_28bit()
                    index.append(ticks_since_index + ticks + val)
                    ticks_since_index = -(ticks + val)
                elif opcode == FluxOp.Space:
                    ticks += _read_28bit()
                else:
                    raise error.Fatal("Bad opcode in flux stream (%d)"
                                      % opcode)
            else:
                if i < 250:
                    val = i
                else:
                    val = 250 + (i - 250) * 255
                    val += next(dat_i) - 1
                ticks += val
                flux.append(ticks)
                ticks_since_index += ticks
                ticks = 0
    except StopIteration:
        pass
    return flux, index
