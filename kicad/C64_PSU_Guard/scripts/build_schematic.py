"""Generate the C64 PSU Guard schematic (one sheet).

Two identical protection channels (5 V and 12 V) between a PC power supply and the
4-pin DIN power input of a Commodore 1541-II / 1581 drive:
  P-MOSFET reverse-polarity blocking -> TVS + input capacitors -> TPS25921A eFuse
  (current limit, fast trip, OVP/UVLO dividers, soft start, auto retry) -> output.
LEDs: green = rail present at the output, red = channel switched off (FLT),
one red "reverse polarity" LED fed from whichever input rail is negative.
J3 is a reserve header for the UFI board (OFF input, FLT outputs).

Run with KiCad's bundled Python:
    "C:/Program Files/KiCad/10.0/bin/python.exe" build_schematic.py
"""
from __future__ import annotations

import json
from pathlib import Path

from kisch import Sheet

OUT = Path(__file__).resolve().parent.parent
PROJECT = "C64_PSU_Guard"

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"
LED0603 = "LED_SMD:LED_0603_1608Metric"
SOT23 = "Package_TO_SOT_SMD:SOT-23"
SOD123 = "Diode_SMD:D_SOD-123"
SOIC8 = "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm"
HDR4 = "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical"
TERM3 = "TerminalBlock:TerminalBlock_MaiXu_MX126-5.0-03P_1x03_P5.00mm"

# JLCPCB/LCSC part numbers, looked up 2026-10-11 via the JLCPCB parts API (all in stock).
LCSC = {
    "TPS25921ADR": "C189199", "AO3401A": "C15127", "2N7002": "C8545",
    "BZT52C10": "C2103", "1N4148W": "C81598", "SMF5.0CA": "C2980402", "SMAJ15CA": "C110044",
    "green": "C12624", "red": "C2286",
    ("100k", R0603): "C25803", ("49.9k", R0603): "C23184", ("30k", R0603): "C22984",
    ("15.4k", R0603): "C22804", ("11k", R0603): "C25950", ("115k", R0603): "C22783",
    ("143k", R0603): "C22877", ("1k", R0603): "C21190", ("3.3k", R0603): "C22978",
    ("2.2k", R0603): "C4190",
    ("22nF", C0603): "C21122", ("100nF", C0603): "C14663", ("1uF", C0603): "C15849",
    ("10uF/35V", C1206): "C454102",
    "PSU_IN": "C32713270", "UFI_LINK": "C32713270", "DRIVE": "C474882",
}


def lcsc_field(value: str, fp: str | None) -> dict:
    code = LCSC.get((value, fp)) or LCSC.get(value)
    return {"LCSC": code} if code else {}


Sheet.field_hook = staticmethod(lcsc_field)


class Refs:
    def __init__(self):
        self.n: dict[str, int] = {}

    def __call__(self, prefix: str) -> str:
        self.n[prefix] = self.n.get(prefix, 0) + 1
        return f"{prefix}{self.n[prefix]}"


ref = Refs()


def R(sh, value, x, y, a, b):
    sh.part("Device", "R", ref("R"), value, x, y, {"1": a, "2": b}, R0603)


def C(sh, value, x, y, a, b, fp=C0603):
    sh.part("Device", "C", ref("C"), value, x, y, {"1": a, "2": b}, fp)


def LED(sh, colour, x, y, anode, cathode):
    sh.part("Device", "LED", ref("D"), colour, x, y, {"2": anode, "1": cathode}, LED0603)


def channel(sh, v: str, y0: float, tvs: str, r_uv: str, r_ov: str, r_ilim: str, r_led: str):
    """One protection channel; `v` = '5' or '12' names the nets."""
    vin, vp, vout = f"G:+{v}V_IN", f"V{v}_P", f"G:+{v}V_OUT"
    gate, ss, en, ov, il, flt = f"G{v}", f"SS{v}", f"EN{v}", f"OV{v}", f"IL{v}", f"G:FLT{v}"
    sh.text(f"{v} V channel: reverse blocking P-MOSFET (body diode conducts first, then Vgs = -Vin, "
            f"zener clamps the gate), TVS + input capacitors, TPS25921A eFuse.\n"
            f"UVLO divider 100k/{r_uv}, OVP divider 100k/{r_ov}, R_ILIM {r_ilim} "
            f"(I_LIMIT ~ 0.0105 A/kOhm, fast trip 0.0142 A/kOhm + 0.36 A), C_SS 22 nF.",
            20, y0 - 30, 1.27)
    # reverse-polarity blocking: D = connector side, S = protected rail, G pulled to GND
    sh.part("Transistor_FET", "AO3401A", ref("Q"), "AO3401A", 70, y0, {"3": vin, "2": vp, "1": gate}, SOT23)
    R(sh, "100k", 55, y0 + 15, gate, "GND")
    sh.part("Device", "D_Zener", ref("D"), "BZT52C10", 85, y0 + 15, {"1": vp, "2": gate}, SOD123)
    sh.part("Device", "D_TVS", ref("D"), tvs, 105, y0 + 15, {"1": vp, "2": "GND"},
            "Diode_SMD:D_SMA" if tvs.startswith("SMAJ") else "Diode_SMD:D_SMF")
    C(sh, "10uF/35V", 120, y0 + 15, vp, "GND", C1206)
    C(sh, "100nF", 130, y0 + 15, vp, "GND")
    sh.pwr_flag(vp, 95, y0 - 10)

    # eFuse
    u = ref("U")
    sh.part("C64_PSU_Guard", "TPS25921x", u, "TPS25921ADR", 180, y0, {
        "4": vp, "5": vout, "1": "GND", "2": ss, "3": en, "7": il, "8": ov, "6": flt}, SOIC8,
        {"MPN": "TPS25921ADR"})
    R(sh, "100k", 150, y0 - 15, vp, en)
    R(sh, r_uv, 150, y0 + 15, en, "GND")
    R(sh, "100k", 140, y0 - 15, vp, ov)
    R(sh, r_ov, 140, y0 + 15, ov, "GND")
    C(sh, "22nF", 160, y0 + 20, ss, "GND")
    R(sh, r_ilim, 205, y0 + 15, il, "GND")
    C(sh, "1uF", 215, y0 + 15, vout, "GND")
    # LEDs: green on the output, red on FLT (anode from the protected input, lights when OUT is off)
    R(sh, r_led, 235, y0 - 15, vout, f"LG{v}")
    LED(sh, "green", 250, y0 - 15, f"LG{v}", "GND")
    R(sh, r_led, 235, y0 + 5, vp, f"LR{v}")
    LED(sh, "red", 250, y0 + 5, f"LR{v}", flt)
    # OFF from the UFI link header: N-MOSFET pulls ENUV below its threshold
    sh.part("Transistor_FET", "2N7002", ref("Q"), "2N7002", 165, y0 + 35, {"1": "G:OFF", "2": "GND", "3": en}, SOT23)


def main():
    sh = Sheet("C64 PSU Guard", PROJECT)
    sh.text("C64 PSU GUARD - protected 5 V / 12 V feed for a 1541-II / 1581 drive from a PC power supply.\n"
            "J1 PSU in (Berg: 1 +5V, 2/3 GND, 4 +12V)  ->  J2 drive out (DIN pin 1 = 5V, 2 = GND, 4 = 12V).\n"
            "Channel 5 V: limit 1.2 A, OVP 6.0 V, UVLO 4.2 V.  Channel 12 V: limit 1.5 A, OVP 14.0 V, UVLO 10.4 V.\n"
            "TPS25921A = auto retry: red LED on while the channel is off, off again when the fault is gone.\n"
            "J3 UFI link (reserve): 1 GND, 2 OFF (high = both channels off), 3 FLT5, 4 FLT12 (open drain).",
            20, 15, 1.5)
    sh.part("Connector_Generic", "Conn_01x04", ref("J"), "PSU_IN", 35, 60,
            {"1": "G:+5V_IN", "2": "GND", "3": "GND", "4": "G:+12V_IN"}, HDR4,
            {"Note": "PC PSU floppy (Berg) plug: 1 +5V red, 2/3 GND black, 4 +12V yellow"})
    sh.pwr_flag("G:+5V_IN", 50, 40)
    sh.pwr_flag("G:+12V_IN", 60, 40)
    sh.pwr_flag("GND", 40, 90)

    channel(sh, "5", 70, "SMF5.0CA", "49.9k", "30k", "115k", "1k")
    channel(sh, "12", 170, "SMAJ15CA", "15.4k", "11k", "143k", "3.3k")

    # reverse-polarity indicator: GND -> R -> LED -> diode -> whichever input rail is negative
    sh.text("Reverse polarity LED: conducts only when +5V_IN or +12V_IN is negative against GND\n"
            "(1N4148W blocks the rail voltage in normal operation).", 20, 240, 1.27)
    R(sh, "2.2k", 40, 260, "GND", "REVA")
    LED(sh, "red", 55, 260, "REVA", "REV")
    sh.part("Device", "D_Small", ref("D"), "1N4148W", 75, 255, {"2": "REV", "1": "G:+5V_IN"}, SOD123)
    sh.part("Device", "D_Small", ref("D"), "1N4148W", 75, 265, {"2": "REV", "1": "G:+12V_IN"}, SOD123)

    # outputs and UFI link
    sh.part("Connector_Generic", "Conn_01x03", ref("J"), "DRIVE", 300, 120,
            {"1": "G:+5V_OUT", "2": "GND", "3": "G:+12V_OUT"}, TERM3,
            {"Note": "to the drive's DIN cable: DIN 1 = 5V, 2 = GND, 4 = 12V, 3 n.c."})
    sh.part("Connector_Generic", "Conn_01x04", ref("J"), "UFI_LINK", 300, 220,
            {"1": "GND", "2": "G:OFF", "3": "G:FLT5", "4": "G:FLT12"}, HDR4,
            {"Note": "reserve: OFF high = both channels off (100k pull-down), FLT open drain"})
    R(sh, "100k", 280, 240, "G:OFF", "GND")

    (OUT / f"{PROJECT}.kicad_sch").write_text(sh.render(PROJECT, f"/{sh.uuid}", is_root=True), encoding="utf8")
    pro = OUT / f"{PROJECT}.kicad_pro"
    if not pro.exists():
        pro.write_text(json.dumps({"meta": {"filename": pro.name, "version": 1}}, indent=2), encoding="utf8")
    with open(OUT / "netlist_summary.txt", "w", encoding="utf8") as f:
        for net, pins in sorted(sh.netlist.items()):
            f.write(f"{net:12s} {' '.join(sorted(pins))}\n")
    print("written to", OUT, "refs", dict(ref.n))


if __name__ == "__main__":
    main()
