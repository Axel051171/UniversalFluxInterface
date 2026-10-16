"""Check the linked ELF, not estimates of C structure sizes. Standard library only."""
import argparse
import json
import re
import subprocess
from pathlib import Path


def inspect(elf, nm, objdump):
    sections = {}
    for line in subprocess.check_output([objdump, "-h", str(elf)], text=True).splitlines():
        m = re.match(r"\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s", line)
        if m:
            sections[m[1]] = (int(m[3], 16), int(m[2], 16))
    symbols = {}
    for line in subprocess.check_output([nm, "-S", str(elf)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 4:
            symbols[fields[3]] = (int(fields[0], 16), int(fields[1], 16))
        elif len(fields) == 3 and re.fullmatch(r"[0-9a-fA-F]+", fields[0]):
            symbols[fields[2]] = (int(fields[0], 16), 0)
    return sections, symbols


def validate(sections, symbols):
    def require(condition, message):
        if not condition:
            raise ValueError(message)
    def addr(name):
        return symbols[name][0]
    def inside(address, size, start, end):
        return size >= 0 and start <= address <= address + size <= end

    for name, start, end in ((".dtcm", 0x20000000, 0x20020000),
                              (".axi_sram", 0x24000000, 0x24050000),
                              (".sram2", 0x30004000, 0x30008000)):
        require(inside(*sections[name], start, end), name + " outside its RAM bank")
    heap_start, heap_end = addr("__heap_start"), addr("__heap_end")
    require(inside(heap_start, heap_end-heap_start, 0x24000000, 0x24050000), "heap outside AXI")
    require(heap_end-heap_start == 8192, "expected bounded 8 KiB heap")
    require(heap_start % 8 == 0, "heap alignment")
    require(0x24050000-heap_end >= 32768, "AXI reserve below 32 KiB")
    require(addr("_estack") == 0x20020000, "stack top")
    require(addr("_estack")-addr("_edtcm") >= 16384, "stack reserve below 16 KiB")
    rx, rx_end = addr("__usb_rx_start"), addr("__usb_rx_end")
    require(rx_end-rx == 16384 and inside(rx, rx_end-rx, 0x20000000, addr("_edtcm")), "USB rings outside DTCM")
    fallback, size = symbols["flux_fallback"]
    require(size == 224*1024 and inside(fallback, size, 0x24000000, 0x24050000), "flux fallback changed or not DMA reachable")
    ring, size = symbols["write_ring"]
    require(size == 8192 and inside(ring, size, 0x30004000, 0x30008000), "write ring outside SRAM2")
    require(fallback % 32 == 0 and ring % 32 == 0, "DMA cache-line alignment")
    require("_sbrk" in symbols, "bounded heap implementation missing")
    # Reject future sections placed after the checked heap or across bank boundaries.
    banks = ((0x20000000,0x20020000),(0x24000000,0x24050000),
             (0x30000000,0x30004000),(0x30004000,0x30008000),(0x38000000,0x38004000))
    for name, (base, length) in sections.items():
        for start,end in banks:
            if length and start <= base < end:
                require(base+length <= end, name+" crosses RAM boundary")
                if start == 0x24000000:
                    require(base+length <= heap_end, name+" exceeds checked AXI budget")
                if start == 0x20000000:
                    require(base+length <= addr("_edtcm"), name+" overlaps stack reserve")
    return dict(axi_used=heap_end-0x24000000, axi_free=0x24050000-heap_end,
                dtcm_static=addr("_edtcm")-0x20000000,
                stack_space=addr("_estack")-addr("_edtcm"), heap_bytes=8192,
                fallback_bytes=224*1024, usb_rx_bytes=16384)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("elf", type=Path)
    p.add_argument("--nm", default="arm-none-eabi-nm")
    p.add_argument("--objdump", default="arm-none-eabi-objdump")
    p.add_argument("--report", type=Path)
    a = p.parse_args()
    report = validate(*inspect(a.elf, a.nm, a.objdump))
    if a.report:
        a.report.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print("Memory budget OK: AXI free {axi_free} B; stack space {stack_space} B; heap {heap_bytes} B".format(**report))


if __name__ == "__main__":
    main()
