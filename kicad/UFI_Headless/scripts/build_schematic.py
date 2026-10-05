"""Generate the UFI Headless schematic (root + Power + MCU_Core sheets).

Run with KiCad's bundled Python (no pcbnew needed):
    "C:/Program Files/KiCad/10.0/bin/python.exe" build_schematic.py
"""
from __future__ import annotations

import json
import re
from pathlib import Path

from kisch import Sheet, fmt, lib_symbol, symbol_pins, uid

OUT = Path(__file__).resolve().parent.parent
PROJECT = "UFI_Headless"

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"
LED0603 = "LED_SMD:LED_0603_1608Metric"
BTN = "Button_Switch_SMD:SW_SPST_TL3342"


# JLCPCB/LCSC part numbers, looked up 2026-10-05 via the JLCPCB parts API (in stock;
# "basic" = no feeder fee).  Key: (value, footprint) or value alone.
RPACK4_FP = "Resistor_SMD:R_Array_Convex_4x0603"
CP_FP = "Capacitor_SMD:CP_Elec_8x10.5"
LCSC = {
    # resistors 0603 (all basic except 13.3k)
    ("5.1k", R0603): "C23186", ("100k 1%", R0603): "C25803", ("13.3k 1%", R0603): "C25952",
    ("33k 1%", R0603): "C4216", ("33k", R0603): "C4216", ("10k 1%", R0603): "C25804",
    ("10k", R0603): "C25804", ("22k", R0603): "C31850", ("1k", R0603): "C21190",
    ("2.2k", R0603): "C4190", ("1k", RPACK4_FP): "C20197",
    # capacitors
    ("100nF", C0603): "C14663", ("100nF/50V", C0603): "C14663", ("1uF", C0603): "C15849",
    ("18pF C0G", C0603): "C1647", ("1uF", C0805): "C28323", ("2.2uF", C0805): "C87994",
    ("4.7uF", C0805): "C1779", ("10uF", C0805): "C15850", ("22uF/10V", C0805): "C45783",
    ("10uF/35V", C1206): "C454102", ("22uF/16V", C1206): "C90146", ("100uF/25V", CP_FP): "C5337554",
    # semiconductors / ICs
    "STM32H723ZGT6": "C730146", "USBLC6-2SC6": "C7519", "TPS54202DDC": "C191884",
    "TPS2116DRL": "C3235557", "AP7361C-33E": "C500795", "SN74LS07D": "C371970",
    "74LVC14AD": "C133541", "SS54": "C16103", "SMF5.0CA": "C2980402", "SMAJ15CA": "C110044",
    "green": "C12624", "green PWR": "C12624", "red": "C2286",
    # passives with specific parts
    "1.5A hold": "C32404", "15uH 3A": "C1330797", "600R@100MHz": "C1002", "25MHz CL=12pF": "C9006",
    # electromechanical
    "USB-C": "C165948", "RESET": "C2886898", "BOOT": "C2886898", "FDD_34PIN": "C20920",
    "SWD": "C22438120", "FDD_PWR": "C32713270", "DBG_UART": "C49257",
    # no LCSC match (hand-sourced): AMIGA_FDD 2x12 shrouded header, 12V barrel jack, IEC DIN-6
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


def R(sh, value, x, y, a, b, fp=R0603):
    sh.part("Device", "R", ref("R"), value, x, y, {"1": a, "2": b}, fp)


def C(sh, value, x, y, a, b, fp=C0603):
    if value.startswith("100nF"):
        fp = C0603  # small decoupling caps always 0603, even inside a bulk-cap row
    sh.part("Device", "C", ref("C"), value, x, y, {"1": a, "2": b}, fp)


def CP(sh, value, x, y, a, b, fp="Capacitor_SMD:CP_Elec_8x10.5"):
    sh.part("Device", "C_Polarized", ref("C"), value, x, y, {"1": a, "2": b}, fp)


def cap_row(sh, values, x0, y, a, b, fp=C0603, step=10.16):
    for i, v in enumerate(values):
        C(sh, v, x0 + i * step, y, a, b, fp)


# ----------------------------------------------------------------------------
# Power sheet
# ----------------------------------------------------------------------------
def build_power() -> Sheet:
    sh = Sheet("UFI Headless - Power", PROJECT)
    sh.text("POWER\n"
            "USB-C 5V (VBUS) and 12V barrel jack. 12V -> TPS54202 buck -> +5V_DRV.\n"
            "TPS2116 priority mux: +5V_DRV preferred (PR1 threshold ~4.3V), VBUS fallback -> +5V.\n"
            "+5V feeds floppy drive power (J3) and AP7361C 3.3V LDO.\n"
            "+12V only present with barrel jack (needed for 5.25in drives).", 20, 20, 1.5)

    # USB-C receptacle (power + data), ESD, fuse
    sh.part("Connector", "USB_C_Receptacle_USB2.0_16P", ref("J"), "USB-C", 40, 90, {
        "A4": "VBUS", "A9": "VBUS", "B4": "VBUS", "B9": "VBUS",
        "A1": "GND", "A12": "GND", "B1": "GND", "B12": "GND", "SH": "GND",
        "A5": "CC1", "B5": "CC2",
        "A6": "G:USB_DP", "B6": "G:USB_DP", "A7": "G:USB_DM", "B7": "G:USB_DM",
        "A8": "NC", "B8": "NC",
    }, "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
        {"MPN": "HRO TYPE-C-31-M-12"})
    R(sh, "5.1k", 80, 75, "CC1", "GND")
    R(sh, "5.1k", 90, 75, "CC2", "GND")
    sh.part("Power_Protection", "USBLC6-2SC6", ref("U"), "USBLC6-2SC6", 80, 110, {
        "1": "G:USB_DP", "6": "G:USB_DP", "3": "G:USB_DM", "4": "G:USB_DM",
        "5": "VBUS", "2": "GND"}, None, {"MPN": "USBLC6-2SC6"})
    sh.part("Device", "D_TVS", ref("D"), "SMF5.0CA", 110, 75, {"1": "VBUS", "2": "GND"},
            "Diode_SMD:D_SMF", {"MPN": "SMF5.0CA"})
    sh.part("Device", "Polyfuse", ref("F"), "1.5A hold", 110, 100, {"1": "VBUS", "2": "VBUS_F"},
            "Fuse:Fuse_1812_4532Metric", {"MPN": "SMD1812P150TF/24 (1.5A hold)"})
    sh.pwr_flag("VBUS", 130, 70)
    sh.pwr_flag("VBUS_F", 140, 70)

    # 12V input: barrel jack, reverse polarity Schottky, TVS, bulk
    sh.part("Connector", "Barrel_Jack", ref("J"), "12V DC 5.5/2.1", 40, 170,
            {"1": "+12V_IN", "2": "GND"}, "Connector_BarrelJack:BarrelJack_Horizontal")
    sh.part("Device", "D_Schottky", ref("D"), "SS54", 70, 150, {"2": "+12V_IN", "1": "+12V"},
            "Diode_SMD:D_SMC", {"MPN": "SS54C (40V/5A)"})
    sh.part("Device", "D_TVS", ref("D"), "SMAJ15CA", 90, 165, {"1": "+12V", "2": "GND"},
            "Diode_SMD:D_SMA", {"MPN": "SMAJ15CA"})
    CP(sh, "100uF/25V", 105, 165, "+12V", "GND")
    C(sh, "10uF/35V", 115, 165, "+12V", "GND", C1206)
    C(sh, "100nF/50V", 125, 165, "+12V", "GND")
    sh.pwr_flag("+12V_IN", 60, 135)
    sh.pwr_flag("+12V", 80, 135)
    sh.pwr_flag("GND", 150, 70)

    # 12V -> 5V buck (TPS54202, datasheet 7.2 reference design values)
    sh.part("Regulator_Switching", "TPS54202DDC", ref("U"), "TPS54202DDC", 170, 170, {
        "3": "+12V", "1": "GND", "5": "NC", "6": "BUCK_BST", "2": "BUCK_SW", "4": "BUCK_FB"},
        None, {"MPN": "TPS54202DDCR"})
    C(sh, "100nF", 200, 150, "BUCK_BST", "BUCK_SW")
    sh.part("Device", "L", ref("L"), "15uH 3A", 215, 170, {"1": "BUCK_SW", "2": "+5V_DRV"},
            "Inductor_SMD:L_Bourns_SRN6045TA", {"MPN": "Bourns SRN6045TA-150M"})
    R(sh, "100k 1%", 235, 160, "+5V_DRV", "BUCK_FB")
    R(sh, "13.3k 1%", 235, 180, "BUCK_FB", "GND")
    cap_row(sh, ["22uF/16V", "22uF/16V", "100nF"], 250, 170, "+5V_DRV", "GND", C1206)
    sh.pwr_flag("+5V_DRV", 280, 150)

    # Power mux: +5V_DRV (priority) / VBUS_F (fallback) -> +5V
    sh.part("Power_Management", "TPS2116DRL", ref("U"), "TPS2116DRL", 200, 90, {
        "3": "+5V_DRV", "5": "+5V_DRV", "4": "MUX_PR1", "6": "VBUS_F",
        "2": "+5V", "7": "+5V", "1": "GND", "8": "G:PWR_SRC"}, None, {"MPN": "TPS2116DRLR"})
    R(sh, "33k 1%", 165, 70, "+5V_DRV", "MUX_PR1")
    R(sh, "10k 1%", 165, 105, "MUX_PR1", "GND")
    R(sh, "10k", 235, 70, "+3V3", "G:PWR_SRC")
    cap_row(sh, ["1uF", "1uF"], 165, 125, "+5V_DRV", "GND")
    C(sh, "1uF", 185, 125, "VBUS_F", "GND")
    cap_row(sh, ["22uF/10V", "100nF"], 250, 95, "+5V", "GND", C0805)
    CP(sh, "100uF/25V", 270, 95, "+5V", "GND")

    # 3.3V LDO
    sh.part("Regulator_Linear", "AP7361C-33E", ref("U"), "AP7361C-33E", 320, 95,
            {"1": "+5V", "2": "GND", "3": "+3V3"}, None, {"MPN": "AP7361C-33E-13"})
    C(sh, "10uF", 305, 115, "+5V", "GND", C0805)
    cap_row(sh, ["10uF", "100nF"], 340, 115, "+3V3", "GND", C0805)

    # Floppy drive power out (3.5in Berg / 5.25in Molex adapter cable)
    sh.part("Connector_Generic", "Conn_01x04", ref("J"), "FDD_PWR", 340, 170,
            {"1": "+5V", "2": "GND", "3": "GND", "4": "+12V"},
            "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
            {"Note": "Pin1 +5V, 2/3 GND, 4 +12V (Berg/Molex FDD order)"})
    return sh


# ----------------------------------------------------------------------------
# MCU core sheet
# ----------------------------------------------------------------------------
# GPIO assignment (hardware is the reference; firmware pin table must follow)
GPIO = {
    # Floppy - timer pins
    "PA5": "G:FDD_RDATA",     # TIM2_CH1 (AF1) input capture, 32-bit
    "PA1": "G:FDD_INDEX",     # TIM2_CH2 (AF1) input capture, same timebase
    "PA6": "G:FDD_WDATA",     # TIM3_CH1 (AF2) output
    # Floppy - outputs (to open-collector drivers)
    "PE7": "G:FDD_MOTOR_A", "PE8": "G:FDD_MOTOR_B",
    "PE9": "G:FDD_DRVSEL_A", "PE10": "G:FDD_DRVSEL_B",
    "PE11": "G:FDD_STEP", "PE12": "G:FDD_DIR", "PE13": "G:FDD_SIDE",
    "PE14": "G:FDD_WGATE", "PE15": "G:FDD_DENSITY",
    # Floppy - inputs
    "PF0": "G:FDD_TRK0", "PF1": "G:FDD_WPROT", "PF2": "G:FDD_DSKCHG", "PF3": "G:FDD_READY",
    # IEC bus: separate drive (to open-collector driver) and sense lines
    "PD0": "G:IEC_ATN_OUT", "PD1": "G:IEC_CLK_OUT", "PD2": "G:IEC_DATA_OUT",
    "PD3": "G:IEC_SRQ_OUT", "PD4": "G:IEC_RESET_OUT",
    "PF4": "G:IEC_ATN_IN", "PF5": "G:IEC_CLK_IN", "PF6": "G:IEC_DATA_IN",
    "PF7": "G:IEC_SRQ_IN", "PF8": "G:IEC_RESET_IN",
    # USB OTG_HS with internal FS PHY
    "PA11": "G:USB_DM", "PA12": "G:USB_DP", "PA9": "VBUS_SENSE",
    # Debug
    "PA13": "SWDIO", "PA14": "SWCLK", "PB3": "SWO",
    "PD8": "DBG_TX", "PD9": "DBG_RX",   # USART3 AF7
    # Status
    "PG0": "LED_ACT", "PG1": "LED_FDD", "PG2": "LED_USB", "PG3": "LED_ERR",
    "PG4": "G:PWR_SRC",
    # Clock
    "PH0": "HSE_IN", "PH1": "HSE_OUT",
}


def build_core() -> Sheet:
    sh = Sheet("UFI Headless - STM32H723 Core", PROJECT)
    sh.text("STM32H723ZGT6 CORE (LQFP144)\n"
            "Decoupling per ST AN5419: 100nF per VDD pin + 4.7uF bulk, VCAP 2x2.2uF (LDO mode),\n"
            "VDDA via ferrite with 1uF+100nF, VREF+ = VDDA. HSE 25 MHz, CL 12pF -> 2x 18pF (firmware PLL M=5 N=220 -> 550 MHz).\n"
            "FDD flux: RDATA=PA5 TIM2_CH1, INDEX=PA1 TIM2_CH2, WDATA=PA6 TIM3_CH1.", 20, 20, 1.5)

    mcu = lib_symbol("MCU_ST_STM32H7", "STM32H723ZGTx")
    conn: dict[str, str] = {}
    by_port: dict[str, str] = {}
    for p in symbol_pins(mcu):
        m = re.match(r"(P[A-K]\d+)", p["name"])
        if m:
            by_port[m.group(1)] = p["number"]
            conn[p["number"]] = "NC"
        else:
            conn[p["number"]] = {
                "VDD": "+3V3", "VDD33USB": "+3V3", "VBAT": "+3V3", "PDR_ON": "+3V3",
                "VSS": "GND", "VSSA": "GND", "VDDA": "VDDA", "VREF+": "VDDA", "VCAP": "VCAP",
                "NRST": "NRST", "BOOT0": "BOOT0"}[p["name"]]
    # VCAP pins (71, 106) each get their own 2.2uF; both are internally tied to VCORE
    conn["71"], conn["106"] = "VCAP1", "VCAP2"
    for port, net in GPIO.items():
        conn[by_port[port]] = net
    sh.part("MCU_ST_STM32H7", "STM32H723ZGTx", ref("U"), "STM32H723ZGT6", 210, 150, conn,
            "Package_QFP:LQFP-144_20x20mm_P0.5mm", {"MPN": "STM32H723ZGT6"})

    # Decoupling: 12x VDD + VDD33USB + VBAT -> 14x 100nF, 4.7uF bulk, 1uF USB
    cap_row(sh, ["100nF"] * 14, 20, 45, "+3V3", "GND", step=7.62)
    cap_row(sh, ["4.7uF", "1uF"], 20, 65, "+3V3", "GND", C0805)
    # VDDA filter
    sh.part("Device", "FerriteBead_Small", ref("FB"), "600R@100MHz", 50, 65,
            {"1": "+3V3", "2": "VDDA"}, "Inductor_SMD:L_0603_1608Metric")
    cap_row(sh, ["1uF", "100nF"], 60, 65, "VDDA", "GND")
    sh.pwr_flag("VDDA", 85, 60)
    # VCAP (internal LDO output)
    C(sh, "2.2uF", 100, 65, "VCAP1", "GND", C0805)
    C(sh, "2.2uF", 110, 65, "VCAP2", "GND", C0805)

    # Reset and boot
    C(sh, "100nF", 30, 95, "NRST", "GND")
    sh.part("Switch", "SW_Push", ref("SW"), "RESET", 50, 95, {"1": "NRST", "2": "GND"}, BTN)
    R(sh, "10k", 30, 120, "BOOT0", "GND")
    sh.part("Switch", "SW_Push", ref("SW"), "BOOT", 50, 120, {"1": "+3V3", "2": "BOOT0"}, BTN)

    # HSE 25 MHz
    sh.part("Device", "Crystal_GND24", ref("Y"), "25MHz CL=12pF", 50, 150,
            {"1": "HSE_IN", "3": "HSE_OUT", "2": "GND", "4": "GND"},
            "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm", {"MPN": "YXC X322525MOB4SI (25MHz, CL 12pF, +-10ppm)"})
    C(sh, "18pF C0G", 30, 165, "HSE_IN", "GND")
    C(sh, "18pF C0G", 70, 165, "HSE_OUT", "GND")

    # VBUS sense divider (5V -> 3.0V on PA9)
    R(sh, "22k", 30, 195, "VBUS", "VBUS_SENSE")
    R(sh, "33k", 30, 215, "VBUS_SENSE", "GND")

    # SWD (ARM Cortex 10-pin 1.27 mm)
    sh.part("Connector", "Conn_ARM_JTAG_SWD_10", ref("J"), "SWD", 330, 60, {
        "1": "+3V3", "2": "SWDIO", "3": "GND", "4": "SWCLK", "5": "GND", "6": "SWO",
        "7": "NC", "8": "NC", "9": "GND", "10": "NRST"},
        "Connector_PinHeader_1.27mm:PinHeader_2x05_P1.27mm_Vertical")
    # Debug UART
    sh.part("Connector_Generic", "Conn_01x03", ref("J"), "DBG_UART", 330, 100,
            {"1": "DBG_TX", "2": "DBG_RX", "3": "GND"},
            "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
            {"Note": "Pin1 = MCU TX (PD8), Pin2 = MCU RX (PD9), 3V3 levels"})

    # Status LEDs (GPIO high = on)
    for i, name in enumerate(["LED_ACT", "LED_FDD", "LED_USB", "LED_ERR"]):
        y = 140 + i * 15
        R(sh, "1k", 310, y, name, f"{name}_A")
        sh.part("Device", "LED", ref("D"), {"LED_ERR": "red"}.get(name, "green"), 330, y,
                {"2": f"{name}_A", "1": "GND"}, LED0603)
    R(sh, "2.2k", 310, 205, "+3V3", "LED_PWR_A")
    sh.part("Device", "LED", ref("D"), "green PWR", 330, 205, {"2": "LED_PWR_A", "1": "GND"}, LED0603)
    return sh


# ----------------------------------------------------------------------------
# Bus buffers (shared by Flux_Interface and IEC_Bus)
# ----------------------------------------------------------------------------
SOIC14 = "Package_SO:SOIC-14_3.9x8.7mm_P1.27mm"
RPACK4 = "Resistor_SMD:R_Array_Convex_4x0603"
# hex gate pin numbers per unit (74xx standard pinout): (input, output)
HEX_GATES = [("1", "2"), ("3", "4"), ("5", "6"), ("9", "8"), ("11", "10"), ("13", "12")]


def hex_buffer(sh, kind, x, y, lines, cap_xy):
    """Place one hex chip; `lines` = [(in_net, out_net), ...] up to 6 gates.

    kind 'oc':  SN74LS07 open-collector driver, 5V supply, 40 mA sink (MCU -> bus)
    kind 'st':  74LVC14A Schmitt inverter, 3V3 supply, 5V-tolerant inputs (bus -> MCU)
    """
    if kind == "oc":
        lib_name, value, vcc, spare_in = "74LS07", "SN74LS07D", "+5V", "+5V"
        props = {"MPN": "SN74LS07DR (alt. SN7407DR)"}
    else:
        lib_name, value, vcc, spare_in = "74HC14", "74LVC14AD", "+3V3", "GND"
        props = {"MPN": "SN74LVC14ADR"}
    r = ref("U")
    for i, (gin, gout) in enumerate(HEX_GATES):
        a, b = lines[i] if i < len(lines) else (spare_in, "NC")
        sh.part("74xx", lib_name, r, value, x, y + i * 12.7, {gin: a, gout: b}, SOIC14, props, unit=i + 1)
    sh.part("74xx", lib_name, r, value, x + 25.4, y + 25.4, {"14": vcc, "7": "GND"}, SOIC14, props, unit=7)
    C(sh, "100nF", cap_xy[0], cap_xy[1], vcc, "GND")


def pullups(sh, x, y, nets, rail="+5V", value="1k"):
    """1k pull-ups as 4x0603 arrays; unused elements left open."""
    for k in range(0, len(nets), 4):
        grp = list(nets[k:k + 4]) + ["NC"] * (4 - len(nets[k:k + 4]))
        conn = {}
        for i, net in enumerate(grp):  # element i: pin i+1 (bottom) / pin 8-i (top)
            conn[str(i + 1)] = net
            conn[str(8 - i)] = rail if net != "NC" else "NC"
        sh.part("Device", "R_Pack04", ref("RN"), value, x + (k // 4) * 20.32, y, conn, RPACK4)


# ----------------------------------------------------------------------------
# Flux interface: MCU <-> Shugart/PC bus buffers
# ----------------------------------------------------------------------------
FDD_OUT = [  # MCU net -> bus net (active low on bus, MCU low = asserted)
    ("FDD_MOTOR_A", "FD_MOTOR_A"), ("FDD_MOTOR_B", "FD_MOTOR_B"),
    ("FDD_DRVSEL_A", "FD_DRVSEL_A"), ("FDD_DRVSEL_B", "FD_DRVSEL_B"),
    ("FDD_STEP", "FD_STEP"), ("FDD_DIR", "FD_DIR"), ("FDD_SIDE", "FD_SIDE"),
    ("FDD_WGATE", "FD_WGATE"), ("FDD_WDATA", "FD_WDATA"), ("FDD_DENSITY", "FD_DENSITY")]
FDD_IN = [  # bus net -> MCU net (inverted: MCU high = asserted)
    ("FD_INDEX", "FDD_INDEX"), ("FD_TRK0", "FDD_TRK0"), ("FD_WPROT", "FDD_WPROT"),
    ("FD_RDATA", "FDD_RDATA"), ("FD_DSKCHG", "FDD_DSKCHG"), ("FD_READY", "FDD_READY")]


def g(lines):
    return [(f"G:{a}", f"G:{b}") for a, b in lines]


def build_flux() -> Sheet:
    sh = Sheet("UFI Headless - Flux Interface", PROJECT)
    sh.text("FLUX INTERFACE\n"
            "Outputs: SN74LS07 open-collector, 40 mA sink (drives 150R terminators), 1k pull-ups to +5V.\n"
            "  MCU low = bus line asserted (active low), MCU high/reset = released.\n"
            "Inputs: 1k pull-ups to +5V, 74LVC14A Schmitt inverter at 3V3 (5V-tolerant inputs).\n"
            "  MCU sees INVERTED level: high = drive asserts line. RDATA/INDEX capture on rising edge.", 20, 20, 1.5)
    hex_buffer(sh, "oc", 80, 60, g(FDD_OUT[:6]), (140, 60))
    hex_buffer(sh, "oc", 80, 150, g(FDD_OUT[6:]), (140, 150))
    hex_buffer(sh, "st", 260, 60, g(FDD_IN), (320, 60))
    pullups(sh, 170, 240, [f"G:{b}" for _, b in FDD_OUT])
    pullups(sh, 260, 160, [f"G:{a}" for a, _ in FDD_IN])
    C(sh, "10uF", 340, 160, "+5V", "GND", C0805)
    return sh


# ----------------------------------------------------------------------------
# FDD connectors: 34-pin Shugart/PC and Amiga 2x12 header (pin n = DB23 pin n)
# ----------------------------------------------------------------------------
def build_fdd_conn() -> Sheet:
    sh = Sheet("UFI Headless - FDD Connectors", PROJECT)
    sh.text("FDD CONNECTORS\n"
            "J: 34-pin IBM PC / Shugart. Odd pins GND. Pins 10/12/14/16 = MOTEA/DRVSB/DRVSA/MOTEB (PC)\n"
            "   resp. DS0/DS1/DS2/MOTOR (Shugart) - firmware selects bus type. Pin 34 = DSKCHG (PC) / READY (Shugart).\n"
            "J: Amiga external drive, 2x12 header, pin n = DB23 pin n (Amiga HRM pinout), pin 24 GND.\n"
            "   Adapter cable to DB23. SEL1B shares DRVSEL_B, MTRXD shares MOTOR_B. Only one drive at a time.\n"
            "   CHECK: docs/Amiga_DB23_Adapter_Cable.md uses a different (inconsistent) pinout.", 20, 20, 1.5)
    pc = {str(n): "GND" for n in range(1, 34, 2)}
    pc.update({"2": "FD_DENSITY", "4": "NC", "6": "NC", "8": "FD_INDEX", "10": "FD_MOTOR_A",
               "12": "FD_DRVSEL_B", "14": "FD_DRVSEL_A", "16": "FD_MOTOR_B", "18": "FD_DIR",
               "20": "FD_STEP", "22": "FD_WDATA", "24": "FD_WGATE", "26": "FD_TRK0",
               "28": "FD_WPROT", "30": "FD_RDATA", "32": "FD_SIDE", "34": "FD_DSKCHG"})
    pc = {k: (v if v in ("GND", "NC") else f"G:{v}") for k, v in pc.items()}
    sh.part("Connector_Generic", "Conn_02x17_Odd_Even", ref("J"), "FDD_34PIN", 90, 110, pc,
            "Connector_IDC:IDC-Header_2x17_P2.54mm_Vertical", {"Note": "IBM PC / Shugart floppy bus"})

    amiga = {"1": "FD_READY", "2": "FD_RDATA", "3": "GND", "4": "GND", "5": "GND", "6": "GND",
             "7": "GND", "8": "FD_MOTOR_B", "9": "AMI_SEL2", "10": "AMI_DRES", "11": "FD_DSKCHG",
             "12": "+5V", "13": "FD_SIDE", "14": "FD_WPROT", "15": "FD_TRK0", "16": "FD_WGATE",
             "17": "FD_WDATA", "18": "FD_STEP", "19": "FD_DIR", "20": "AMI_SEL3",
             "21": "FD_DRVSEL_B", "22": "FD_INDEX", "23": "+12V", "24": "GND"}
    amiga = {k: (f"G:{v}" if v.startswith("FD_") else v) for k, v in amiga.items()}
    sh.part("Connector_Generic", "Conn_02x12_Odd_Even", ref("J"), "AMIGA_FDD", 260, 110, amiga,
            "Connector_IDC:IDC-Header_2x12_P2.54mm_Vertical",
            {"Note": "Pin n = Amiga DB23 pin n; RDY,DKRD,GND x5,MTRXD,SEL2B,DRESB,CHNG,+5V,SIDEB,"
                     "WPRO,TK0,DKWEB,DKWDB,STEPB,DIRB,SEL3B,SEL1B,INDEX,+12V"})
    # unused Amiga selects and drive reset held inactive
    pullups(sh, 260, 190, ["AMI_SEL2", "AMI_SEL3", "AMI_DRES"])
    return sh


# ----------------------------------------------------------------------------
# IEC bus (Commodore serial)
# ----------------------------------------------------------------------------
IEC_LINES = ["ATN", "CLK", "DATA", "SRQ", "RESET"]


def build_iec() -> Sheet:
    sh = Sheet("UFI Headless - IEC Bus", PROJECT)
    sh.text("IEC BUS (Commodore serial, DIN-6 240deg)\n"
            "UFI acts as host: 1k pull-ups to +5V on all lines.\n"
            "IEC_x_OUT low = line pulled low (SN74LS07 open collector).\n"
            "IEC_x_IN high = line is low (74LVC14A inverter).", 20, 20, 1.5)
    hex_buffer(sh, "oc", 80, 60, [(f"G:IEC_{n}_OUT", f"IEC_{n}") for n in IEC_LINES], (140, 60))
    hex_buffer(sh, "st", 220, 60, [(f"IEC_{n}", f"G:IEC_{n}_IN") for n in IEC_LINES], (280, 60))
    pullups(sh, 80, 160, [f"IEC_{n}" for n in IEC_LINES])
    sh.part("Connector", "DIN-6", ref("J"), "IEC", 300, 170, {
        "1": "IEC_SRQ", "2": "GND", "3": "IEC_ATN", "4": "IEC_CLK", "5": "IEC_DATA", "6": "IEC_RESET"},
        "UFI:DIN-6_Female_PCB", {"Note": "1 SRQ, 2 GND, 3 ATN, 4 CLK, 5 DATA, 6 RESET; footprint to verify"})
    return sh


# ----------------------------------------------------------------------------
def sheet_block(name, file, x, y, w, h, page, root_uuid):
    u = uid(f"sheetblock|{name}")
    return u, (
        f'(sheet (at {fmt(x)} {fmt(y)}) (size {fmt(w)} {fmt(h)}) (fields_autoplaced yes) '
        f'(stroke (width 0.1524) (type solid)) (fill (color 0 0 0 0.0000)) (uuid "{u}") '
        f'(property "Sheetname" "{name}" (at {fmt(x)} {fmt(y - 0.7)} 0) '
        f'(effects (font (size 1.27 1.27)) (justify left bottom))) '
        f'(property "Sheetfile" "{file}" (at {fmt(x)} {fmt(y + h + 0.6)} 0) '
        f'(effects (font (size 1.27 1.27)) (justify left top))) '
        f'(instances (project "{PROJECT}" (path "/{root_uuid}" (page "{page}")))))')


def main():
    root = Sheet("UFI Headless", PROJECT)
    root.text("UFI Headless - STM32H723 flux engine without CM5.\n"
              "Sheets: Power, MCU_Core, Flux_Interface, FDD_Connectors, IEC_Bus.",
              20, 20, 2)
    blocks = []
    subs = [("Power", "Power.kicad_sch", build_power()),
            ("MCU_Core", "MCU_Core.kicad_sch", build_core()),
            ("Flux_Interface", "Flux_Interface.kicad_sch", build_flux()),
            ("FDD_Connectors", "FDD_Connectors.kicad_sch", build_fdd_conn()),
            ("IEC_Bus", "IEC_Bus.kicad_sch", build_iec())]
    for i, (name, file, sh) in enumerate(subs):
        u, blk = sheet_block(name, file, 30 + i * 70, 50, 50, 30, str(i + 2), root.uuid)
        blocks.append(blk)
        (OUT / file).write_text(sh.render(PROJECT, f"/{root.uuid}/{u}"), encoding="utf8")
    (OUT / f"{PROJECT}.kicad_sch").write_text(
        root.render(PROJECT, f"/{root.uuid}", "\n".join(blocks), is_root=True), encoding="utf8")

    pro = OUT / f"{PROJECT}.kicad_pro"
    if not pro.exists():
        pro.write_text(json.dumps({"meta": {"filename": pro.name, "version": 1}}, indent=2), encoding="utf8")

    # Net summary for review
    with open(OUT / "netlist_summary.txt", "w", encoding="utf8") as f:
        for name, _, sh in subs:
            f.write(f"== {name}\n")
            for net, pins in sorted(sh.netlist.items()):
                f.write(f"{net:16s} {' '.join(sorted(pins))}\n")
    print("written to", OUT)


if __name__ == "__main__":
    main()
