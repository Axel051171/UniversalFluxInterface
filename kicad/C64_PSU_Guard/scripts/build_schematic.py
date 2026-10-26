"""Generate the C64 PSU Guard schematic (one sheet).

Two identical protection channels (5 V and 12 V) between a PC power supply and the
4-pin DIN power input of a Commodore 1541-II / 1581 drive:
  P-MOSFET reverse-polarity blocking -> TVS + input capacitors -> TPS25921A eFuse
  (current limit, fast trip, OVP/UVLO dividers, soft start, auto retry) -> output.
LEDs: green = rail present at the output, red = channel switched off (FLT),
one red "reverse polarity" LED fed from whichever input rail is negative.
J3 links to the UFI board J20 GUARD (OFF input, ERR outputs), v0.2: keyed JST XH.

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
XH4 = "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical"   # keyed, v0.2 (was a 2.54 mm pin header)
XH4_MPN = "JST B4B-XH-A(LF)(SN) (XH 2.5 mm, 3 A, keyed)"
TERM3 = "TerminalBlock:TerminalBlock_MaiXu_MX126-5.0-03P_1x03_P5.00mm"

# JLCPCB/LCSC part numbers, looked up 2026-10-11 via the JLCPCB parts API, re-checked 2026-10-26
# (v0.2: SMAJ13A C110519 and JST B4B-XH-A C144395 new, all in stock).
LCSC = {
    "TPS25921ADR": "C189199", "AO3401A": "C15127", "2N7002": "C8545",
    "BZT52C10": "C2103", "1N4148W": "C81598", "SMF5.0CA": "C2980402", "SMAJ13A": "C110519",
    "green": "C12624", "red": "C2286",
    ("100k", R0603): "C25803", ("49.9k", R0603): "C23184", ("30k", R0603): "C22984",
    ("15.4k", R0603): "C22804", ("11k", R0603): "C25950", ("115k", R0603): "C22783",
    ("143k", R0603): "C22877", ("1k", R0603): "C21190", ("3.3k", R0603): "C22978",
    ("2.2k", R0603): "C4190",
    ("22nF", C0603): "C21122", ("100nF", C0603): "C14663", ("1uF", C0603): "C15849",
    ("10uF/35V", C1206): "C454102",
    "PSU_IN": "C144395", "UFI_LINK": "C144395", "DRIVE": "C474882", "EXT_ERR": "C54620389",
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
    if tvs.startswith("SMAJ"):
        # v0.2: unidirectional SMAJ13A (cathode = pin 1 = rail, footprint pad 1 = cathode band);
        # VRWM 13 V above 12.6 V (12 V + 5 %), VBR 14.4-15.9 V, VC 21.5 V at 18.6 A - the
        # SMAJ15CA of v0.1 clamped at 24.4 V, above the TPS25921A IN rating (20 V, 22 V <= 10 ms)
        sh.part("Device", "D_Zener", ref("D"), tvs, 105, y0 + 15, {"1": vp, "2": "GND"}, "Diode_SMD:D_SMA",
                {"MPN": "Diodes SMAJ13A-13-F (unidirectional TVS, VRWM 13 V, VC 21.5 V @ 18.6 A)",
                 "Description": "Unidirectional TVS, cathode (pin 1) to the 12 V rail"})
    else:
        sh.part("Device", "D_TVS", ref("D"), tvs, 105, y0 + 15, {"1": vp, "2": "GND"}, "Diode_SMD:D_SMF")
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
            "v0.2 2026-10-26: J1 PSU in = JST XH B4B-XH-A, keyed (1 +5V, 2/3 GND, 4 +12V; Berg <-> XH adapter cable)\n"
            "->  J2 drive out (DIN pin 1 = 5V, 2 = GND, 4 = 12V).\n"
            "Channel 5 V: limit 1.2 A, OVP 6.0 V, UVLO 4.2 V.  Channel 12 V: limit 1.5 A, OVP 14.0 V, UVLO 10.4 V.\n"
            "TPS25921A = auto retry: red LED on while the channel is off (thermal shutdown), off again when the fault is gone.\n"
            "J3 UFI link = JST XH B4B-XH-A, 1:1 XH cable to UFI J20 GUARD: 1 GND, 2 OFF (high = both channels off), 3 ERR5, 4 ERR12.",
            20, 15, 1.5)
    sh.part("Connector_Generic", "Conn_01x04", ref("J"), "PSU_IN", 35, 60,
            {"1": "G:+5V_IN", "2": "GND", "3": "GND", "4": "G:+12V_IN"}, XH4,
            {"MPN": XH4_MPN,
             "Note": "keyed XH housing on the adapter cable, Berg pin order 1:1: 1 +5V red, 2/3 GND black, 4 +12V yellow"})
    sh.pwr_flag("G:+5V_IN", 50, 40)
    sh.pwr_flag("G:+12V_IN", 60, 40)
    sh.pwr_flag("GND", 40, 90)

    channel(sh, "5", 70, "SMF5.0CA", "49.9k", "30k", "115k", "1k")
    channel(sh, "12", 170, "SMAJ13A", "15.4k", "11k", "143k", "3.3k")

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
    # The FLT nodes sit at the rail voltage (through the red LED chains) while no fault is
    # present, so they must not reach a 3.3 V input directly: N-MOSFET inverters make
    # ERR5/ERR12 open-drain outputs, high (pull-up on the UFI side) = fault or board unpowered.
    sh.text("UFI link: OFF high = both channels off. ERR5/ERR12 = open drain, released (high) when the\n"
            "channel reports a fault or the guard is unpowered, pulled low while the channel is on.\n"
            "UFI Headless v1.0 J20 GUARD (1:1 XH cable): 1 GND, 2 OFF <- PG7 (220R), 3 ERR5 -> PG8, 4 ERR12 -> PC9\n"
            "(10k pull-ups to 3.3 V, 220R series resistors and PESD3V3L1BA on the UFI side).",
            230, 200, 1.27)
    sh.part("Connector_Generic", "Conn_01x04", ref("J"), "UFI_LINK", 300, 220,
            {"1": "GND", "2": "G:OFF", "3": "ERR5", "4": "ERR12"}, XH4,
            {"MPN": XH4_MPN,
             "Note": "1:1 XH cable to UFI J20: 1 GND, 2 OFF (high = both channels off, 100k pull-down), "
                     "3 ERR5, 4 ERR12 (open drain, high = fault)"})
    R(sh, "100k", 280, 240, "G:OFF", "GND")
    sh.part("Transistor_FET", "2N7002", ref("Q"), "2N7002", 265, 215, {"1": "G:FLT5", "2": "GND", "3": "ERR5"}, SOT23)
    sh.part("Transistor_FET", "2N7002", ref("Q"), "2N7002", 265, 235, {"1": "G:FLT12", "2": "GND", "3": "ERR12"}, SOT23)

    # one external fault LED (front panel): fed from the protected 5 V input, cathode through a
    # diode OR into both FLT open drains -> lights while either channel is switched off.
    # (A reversed input is shown by the on-board REV POL LED only: the eFuses are unpowered then.)
    sh.text("J4 external fault LED: 1 = anode (+), 2 = cathode (-); on while the 5 V or the 12 V channel is off.",
            20, 285, 1.27)
    R(sh, "1k", 40, 300, "V5_P", "XA")
    sh.part("Connector_Generic", "Conn_01x02", ref("J"), "EXT_ERR", 60, 300, {"1": "XA", "2": "XK"},
            "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
            {"Note": "external fault LED, 1 = + (anode), 2 = - (cathode), ~2.5 mA"})
    sh.part("Device", "D_Small", ref("D"), "1N4148W", 80, 295, {"2": "XK", "1": "G:FLT5"}, SOD123)
    sh.part("Device", "D_Small", ref("D"), "1N4148W", 80, 305, {"2": "XK", "1": "G:FLT12"}, SOD123)

    # v0.2: defined FLT level - 100k from each FLT to its protected rail, parallel to the red LED
    # chain (the LED alone leaves FLT floating below the rail, Q5/Q6 gates high-impedance).
    # Numbered last so the v0.1 references stay unchanged.
    sh.text("FLT pull-ups (v0.2): 100k FLT5 -> V5_P, 100k FLT12 -> V12_P (TPS25921A FLT: open drain, 20 V max).",
            110, 285, 1.27)
    R(sh, "100k", 130, 300, "V5_P", "G:FLT5")
    R(sh, "100k", 150, 300, "V12_P", "G:FLT12")

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
