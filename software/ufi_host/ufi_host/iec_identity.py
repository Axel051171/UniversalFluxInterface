"""Read-only ROM signatures; unknown firmware never implies a drive geometry.

Signature addresses/values: OpenCBM opencbm/lib/detect.c (cbm_identify).
https://github.com/OpenCBM/OpenCBM/blob/master/opencbm/lib/detect.c
ROM contents themselves are not bundled.
"""
import hashlib

SIGNATURES = {
    0xaaaa: ("1541", "1540/1541 family"),
    0xf00f: ("1541-II", "1541-II ROM"),
    0xcd18: ("1541C", "1541C ROM"),
    0x02ac: ("1571", "1571 ROM"),
    0x8085: ("1541", "JiffyDOS 1541 ROM"),
    0x10ca: ("1541", "DolphinDOS 1541 ROM"),
    0x6f10: ("1541", "SpeedDOS 1541 ROM"),
}

def identify(dev, device=0, fingerprint=False):
    evidence = {}
    def word(address):
        data = dev.iec_mem_read(address, 2, device)
        if len(data) != 2:
            raise ValueError("truncated IEC ROM signature")
        evidence[f"{address:04x}"] = data.hex()
        return int.from_bytes(data, "little")
    signature = word(0xff40)
    if signature == 0xaaaa:
        irq = word(0xfffe)
        if irq != 0xfe67:
            signature = irq
    model, label = SIGNATURES.get(signature, (None, "unknown ROM"))
    if signature == 0x01ba:
        model, label = ((None, "CMD FD2000/FD4000") if word(0x8008) == 0x4446
                        else ("1581", "1581 ROM family"))
    result = dict(device=device, model=model, rom=label, signatures=evidence,
                  identification="ROM signature, not physical board identity")
    if fingerprint:
        if model is None:
            raise ValueError("unknown ROM mapping; refusing automatic ROM fingerprint")
        start = 0xc000 if model.startswith("1541") else 0x8000
        digest = hashlib.sha256()
        for address in range(start, 0x10000, 32):
            data = dev.iec_mem_read(address, 32, device)
            if len(data) != 32:
                raise ValueError("incomplete ROM fingerprint")
            digest.update(data)
        result.update(rom_start=start, rom_bytes=0x10000-start, rom_sha256=digest.hexdigest())
    return result
