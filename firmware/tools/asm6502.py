"""Minimal two-pass 6502 assembler for the UFI 1541 drive code (no external tools needed).

Syntax: one instruction or directive per line, ';' comments, labels end with ':'.
Directives: .org ADDR, .byte v, v, ..., NAME = value.  Operands: #imm, abs, abs,X, abs,Y,
(zp),Y, zp,X; labels and 'label+1' / 'label-1'; '<label' / '>label' low/high byte.
usage: asm6502.py source.s out.bin [listing.txt]
"""
import re
import sys

OPS = {
    # mnemonic: {mode: opcode}; modes: imp, imm, zp, zpx, abs, abx, aby, izy, rel
    "ADC": {"imm": 0x69, "zp": 0x65, "abs": 0x6D},
    "AND": {"imm": 0x29, "zp": 0x25, "abs": 0x2D},
    "ASL": {"imp": 0x0A, "zp": 0x06},
    "BCC": {"rel": 0x90}, "BCS": {"rel": 0xB0}, "BEQ": {"rel": 0xF0}, "BNE": {"rel": 0xD0},
    "BMI": {"rel": 0x30}, "BPL": {"rel": 0x10}, "BVC": {"rel": 0x50}, "BVS": {"rel": 0x70},
    "BIT": {"zp": 0x24, "abs": 0x2C},
    "CLC": {"imp": 0x18}, "CLI": {"imp": 0x58}, "CLV": {"imp": 0xB8}, "SEC": {"imp": 0x38},
    "SEI": {"imp": 0x78},
    "CMP": {"imm": 0xC9, "zp": 0xC5, "abs": 0xCD},
    "CPX": {"imm": 0xE0, "zp": 0xE4, "abs": 0xEC},
    "CPY": {"imm": 0xC0, "zp": 0xC4, "abs": 0xCC},
    "DEC": {"zp": 0xC6, "abs": 0xCE, "abx": 0xDE},
    "DEX": {"imp": 0xCA}, "DEY": {"imp": 0x88},
    "EOR": {"imm": 0x49, "zp": 0x45, "abs": 0x4D},
    "INC": {"zp": 0xE6, "abs": 0xEE, "abx": 0xFE},
    "INX": {"imp": 0xE8}, "INY": {"imp": 0xC8},
    "JMP": {"abs": 0x4C}, "JSR": {"abs": 0x20},
    "LDA": {"imm": 0xA9, "zp": 0xA5, "zpx": 0xB5, "abs": 0xAD, "abx": 0xBD, "aby": 0xB9, "izy": 0xB1},
    "LDX": {"imm": 0xA2, "zp": 0xA6, "abs": 0xAE, "aby": 0xBE},
    "LDY": {"imm": 0xA0, "zp": 0xA4, "abs": 0xAC, "abx": 0xBC},
    "LSR": {"imp": 0x4A, "zp": 0x46},
    "NOP": {"imp": 0xEA},
    "ORA": {"imm": 0x09, "zp": 0x05, "abs": 0x0D},
    "PHA": {"imp": 0x48}, "PLA": {"imp": 0x68}, "PHP": {"imp": 0x08}, "PLP": {"imp": 0x28},
    "ROL": {"imp": 0x2A, "zp": 0x26}, "ROR": {"imp": 0x6A, "zp": 0x66},
    "RTS": {"imp": 0x60},
    "SBC": {"imm": 0xE9, "zp": 0xE5, "abs": 0xED},
    "STA": {"zp": 0x85, "zpx": 0x95, "abs": 0x8D, "abx": 0x9D, "aby": 0x99, "izy": 0x91},
    "STX": {"zp": 0x86, "abs": 0x8E}, "STY": {"zp": 0x84, "abs": 0x8C},
    "TAX": {"imp": 0xAA}, "TAY": {"imp": 0xA8}, "TXA": {"imp": 0x8A}, "TYA": {"imp": 0x98},
}
SIZE = {"imp": 1, "imm": 2, "zp": 2, "zpx": 2, "izy": 2, "rel": 2, "abs": 3, "abx": 3, "aby": 3}


def assemble(text):
    syms = {}
    lines = []
    for raw in text.splitlines():
        line = raw.split(";", 1)[0].strip()
        if line:
            lines.append(line)

    def value(expr, pc):
        expr = expr.strip()
        if expr.startswith("<"):
            return value(expr[1:], pc) & 0xFF
        if expr.startswith(">"):
            return (value(expr[1:], pc) >> 8) & 0xFF
        m = re.match(r"^(.+?)([+-])(\d+)$", expr)
        if m and not expr.startswith("$"):
            base = value(m.group(1), pc)
            return base + int(m.group(3)) if m.group(2) == "+" else base - int(m.group(3))
        if expr == "*":
            return pc
        if expr.startswith("$"):
            return int(expr[1:], 16)
        if expr.isdigit():
            return int(expr)
        if expr in syms:
            return syms[expr]
        raise KeyError(expr)

    for passno in (1, 2):
        pc = 0
        out = bytearray()
        listing = []
        for line in lines:
            m = re.match(r"^([A-Za-z_]\w*):\s*(.*)$", line)
            if m:                           # "LABEL:" or "LABEL: instruction"
                syms[m.group(1)] = pc
                line = m.group(2).strip()
                if not line:
                    continue
            if "=" in line and not line.upper().startswith(".BYTE"):
                k, v = line.split("=", 1)
                syms[k.strip()] = value(v, pc) if passno == 2 or v.strip()[0] in "$0123456789" else 0
                continue
            parts = line.split(None, 1)
            mn = parts[0].upper()
            arg = parts[1].strip() if len(parts) > 1 else ""
            if mn == ".ORG":
                pc = value(arg, pc)
                continue
            if mn == ".BYTE":
                vals = [value(v, pc) & 0xFF for v in arg.split(",")]
                if passno == 2:
                    out += bytes(vals)
                    listing.append(f"{pc:04X}  {' '.join('%02X' % v for v in vals):12} {line}")
                pc += len(vals)
                continue
            if mn not in OPS:
                raise SyntaxError(line)
            modes = OPS[mn]
            if not arg:
                mode, opnd = "imp", None
            elif arg.startswith("#"):
                mode, opnd = "imm", arg[1:]
            elif arg.startswith("(") and arg.upper().endswith("),Y"):
                mode, opnd = "izy", arg[1:-3]
            elif arg.upper().endswith(",X"):
                opnd = arg[:-2]
                v = value(opnd, pc) if passno == 2 else 0x100
                mode = "zpx" if ("zpx" in modes and passno == 2 and v < 0x100) else "abx"
            elif arg.upper().endswith(",Y"):
                mode, opnd = "aby", arg[:-2]
            elif "rel" in modes:
                mode, opnd = "rel", arg
            else:
                opnd = arg
                v = value(opnd, pc) if passno == 2 else 0x100
                mode = "zp" if ("zp" in modes and passno == 2 and v < 0x100 and "abs" in modes
                                and not opnd.startswith("$0")) else ("abs" if "abs" in modes else "zp")
                if passno == 2 and mode == "zp" and "zp" in modes and v >= 0x100:
                    mode = "abs"
            if mode not in modes:
                raise SyntaxError(f"{line}: mode {mode}")
            code = [modes[mode]]
            if passno == 2:
                if mode == "imm":
                    code.append(value(opnd, pc) & 0xFF)
                elif mode == "rel":
                    off = value(opnd, pc) - (pc + 2)
                    if not -128 <= off <= 127:
                        raise ValueError(f"branch out of range: {line}")
                    code.append(off & 0xFF)
                elif mode in ("zp", "zpx", "izy"):
                    code.append(value(opnd, pc) & 0xFF)
                elif mode in ("abs", "abx", "aby"):
                    v = value(opnd, pc)
                    code += [v & 0xFF, (v >> 8) & 0xFF]
                out += bytes(code)
                listing.append(f"{pc:04X}  {' '.join('%02X' % v for v in code):12} {line}")
            pc += SIZE[mode]
    return bytes(out), syms, listing


if __name__ == "__main__":
    src = open(sys.argv[1], encoding="utf-8").read()
    code, syms, listing = assemble(src)
    if sys.argv[2].endswith(".h"):          # C header: byte array for the firmware
        name = sys.argv[2].rsplit("/", 1)[-1].rsplit("\\", 1)[-1].split(".")[0]
        rows = [", ".join("0x%02X" % b for b in code[i:i + 12]) for i in range(0, len(code), 12)]
        with open(sys.argv[2], "w", encoding="utf-8") as f:
            f.write(f"/* generated by tools/asm6502.py from tools/{name}.s - do not edit */\n")
            f.write(f"#define {name.upper()}_ORG 0x{syms.get('ENTRY', 0):04X}u\n")
            f.write(f"#define {name.upper()}_LEN {len(code)}u\n")
            f.write(f"static const uint8_t {name}_code[{len(code)}] = {{\n    " + ",\n    ".join(rows) + "\n};\n")
    else:
        open(sys.argv[2], "wb").write(code)
    if len(sys.argv) > 3:
        with open(sys.argv[3], "w", encoding="utf-8") as f:
            f.write("\n".join(listing) + "\n\n" + "\n".join(f"{k:16} ${v:04X}" for k, v in sorted(syms.items(), key=lambda kv: kv[1])) + "\n")
    print(f"{len(code)} bytes")
