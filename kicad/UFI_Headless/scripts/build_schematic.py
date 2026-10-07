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
    ("2.2k", R0603): "C4190", ("4.7k", R0603): "C23162", ("330", R0603): "C23138", ("1k", RPACK4_FP): "C20197",
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
    "SWD": "C22438120", "FDD_PWR": "C32713270", "DBG_UART": "C49257", "IEC_HDR": "C37208",
    # v0.2 additions (looked up 2026-10-06): QSPI PSRAM, ESD arrays, drive-power polyfuses
    "APS6404L-3SQR-SN": "C5333729", "ESDA6V1-5SC6": "C6650",
    "1.1A hold 6V": "C20801", "1.1A hold 24V": "C20999",
    # v0.5 additions (looked up 2026-10-07): drive supply switch / current sense, write lock, microSD
    "AO3401A": "C15127", "2N7002": "C8545", "INA180A1": "C122228", "74LVC1G32GW": "C12516",
    ("0.1R 1%", "Resistor_SMD:R_1206_3216Metric"): "C25334", ("100k", R0603): "C25803",
    ("47k", R0603): "C25819", "BTN_A": "C2886898", "BTN_B": "C2886898",
    # v0.6 (looked up 2026-10-07): SD NAND replaces the microSD slot
    "CSNP32GCR01-AOW": "C2841139",
    # v0.7 Apple Disk II port
    "SN74AHCT244PWR": "C484743", "74LVC2G17GW": "C19829576", "ICL7662EBA+T": "C28595",
    "DISK_II": "C2977593", ("10uF/25V", C1206): "C14860", ("10k", RPACK4_FP): "C29718",
    "PSU_IN": "C32713270", "5V_SEL": "C49257", "SYNC_SENSOR": "C49257",   # v0.7: same headers as J3 / J5
    # no LCSC match (hand-sourced): AMIGA_FDD 2x12 shrouded header, 12V barrel jack
    # test points are bare pads (TestPoint_Pad_D1.5mm), nothing to place
}

# Parts added in v0.2 get fixed references above the v0.1 maxima, so the existing
# references (and the PCB links keyed on them) stay untouched.
TP_FP = "TestPoint:TestPoint_Pad_D1.5mm"
SOT23_6 = "Package_TO_SOT_SMD:SOT-23-6"


def testpoint(sh, refdes, label, net, x, y):
    sh.part("Connector", "TestPoint", refdes, label, x, y, {"1": net}, TP_FP, in_bom=False)  # bare pad


def esd5(sh, refdes, nets, x, y):
    """ESDA6V1-5SC6: 5 unidirectional TVS lines to GND, unused lines left open."""
    io = list(nets) + ["NC"] * (5 - len(nets))
    sh.part("Power_Protection", "ESDA6V1-5SC6", refdes, "ESDA6V1-5SC6", x, y, {
        "1": io[0], "3": io[1], "4": io[2], "5": io[3], "6": io[4], "2": "GND"},
        SOT23_6, {"MPN": "ESDA6V1-5SC6"})


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
        "A5": "G:CC1", "B5": "G:CC2",
        "A6": "G:USB_DP", "B6": "G:USB_DP", "A7": "G:USB_DM", "B7": "G:USB_DM",
        "A8": "NC", "B8": "NC",
    }, "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
        {"MPN": "HRO TYPE-C-31-M-12"})
    R(sh, "5.1k", 80, 75, "G:CC1", "GND")
    R(sh, "5.1k", 90, 75, "G:CC2", "GND")
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

    # Power mux: +5V_PRI (priority, JP selection below) / VBUS_F (USB fallback) -> +5V
    sh.part("Power_Management", "TPS2116DRL", ref("U"), "TPS2116DRL", 200, 90, {
        "3": "+5V_PRI", "5": "+5V_PRI", "4": "MUX_PR1", "6": "VBUS_F",
        "2": "+5V", "7": "+5V", "1": "GND", "8": "G:PWR_SRC"}, None, {"MPN": "TPS2116DRLR"})
    R(sh, "33k 1%", 165, 70, "+5V_PRI", "MUX_PR1")
    R(sh, "10k 1%", 165, 105, "MUX_PR1", "GND")
    R(sh, "10k", 235, 70, "+3V3", "G:PWR_SRC")
    cap_row(sh, ["1uF", "1uF"], 165, 125, "+5V_PRI", "GND")
    C(sh, "1uF", 185, 125, "VBUS_F", "GND")
    cap_row(sh, ["22uF/10V", "100nF"], 250, 95, "+5V", "GND", C0805)
    CP(sh, "100uF/25V", 270, 95, "+5V", "GND")

    # v0.7: board inside a PC case - internal USB 2.0 header J16 (port 1 pinout of a mainboard
    # header, pin 9 key) parallel to USB-C (one host at a time), PC PSU input J17 (Berg:
    # 1 +5V, 2/3 GND, 4 +12V into +12V_IN like the barrel jack, one 12 V source at a time),
    # J18 jumper = priority 5 V source of the mux: 1-2 12V buck (default), 2-3 PSU 5 V;
    # USB stays the automatic fallback either way
    sh.text("v0.7: J16 internal USB header (1 +5V, 3 D-, 5 D+, 7/8 GND, pin 9 not fitted = key) parallel to USB-C: one host only.\n"
            "J17 PC PSU (Berg 1 +5V, 2/3 GND, 4 +12V -> +12V_IN): J2 or J17, not both.\n"
            "J18 5V priority source: 1-2 = 12V buck (default), 2-3 = PSU +5V; USB VBUS is the fallback.",
            20, 230, 1.27)
    sh.part("Connector_Generic", "Conn_02x05_Odd_Even", "J16", "USB_HDR", 40, 250, {
        "1": "VBUS", "3": "G:USB_DM", "5": "G:USB_DP", "7": "GND", "8": "GND",
        "2": "NC", "4": "NC", "6": "NC", "9": "NC", "10": "NC"},
        "Connector_PinHeader_2.54mm:PinHeader_2x05_P2.54mm_Vertical",
        {"Note": "mainboard USB 2.0 header pinout (port 1), 1:1 9-pin cable: do not fit pin 9 (key)"})
    sh.part("Connector_Generic", "Conn_01x04", "J17", "PSU_IN", 90, 250,
            {"1": "+5V_PSU", "2": "GND", "3": "GND", "4": "+12V_IN"},
            "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
            {"Note": "PC PSU floppy (Berg) plug: 1 +5V, 2/3 GND, 4 +12V"})
    sh.part("Device", "Polyfuse", "F4", "1.5A hold", 120, 250, {"1": "+5V_PSU", "2": "+5V_PSU_F"},
            "Fuse:Fuse_1812_4532Metric", {"MPN": "SMD1812P150TF/24 (1.5A hold)"})
    sh.part("Device", "D_TVS", "D17", "SMF5.0CA", 140, 260, {"1": "+5V_PSU_F", "2": "GND"},
            "Diode_SMD:D_SMF", {"MPN": "SMF5.0CA"})
    sh.part("Connector_Generic", "Conn_01x03", "J18", "5V_SEL", 170, 250,
            {"1": "+5V_DRV", "2": "+5V_PRI", "3": "+5V_PSU_F"},
            "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
            {"Note": "jumper 1-2 = 12V buck (default), 2-3 = PC PSU 5V"})
    sh.pwr_flag("+5V_PRI", 190, 240)
    sh.pwr_flag("+5V_PSU", 100, 240)

    # 3.3V LDO
    sh.part("Regulator_Linear", "AP7361C-33E", ref("U"), "AP7361C-33E", 320, 95,
            {"1": "+5V", "2": "GND", "3": "+3V3"}, None, {"MPN": "AP7361C-33E-13"})
    C(sh, "10uF", 305, 115, "+5V", "GND", C0805)
    cap_row(sh, ["10uF", "100nF"], 340, 115, "+3V3", "GND", C0805)

    # Floppy drive power out (3.5in Berg / 5.25in Molex adapter cable), polyfused (v0.2)
    sh.part("Connector_Generic", "Conn_01x04", ref("J"), "FDD_PWR", 340, 170,
            {"1": "G:FDD_5V", "2": "GND", "3": "GND", "4": "G:FDD_12V"},
            "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
            {"Note": "Pin1 +5V, 2/3 GND, 4 +12V (Berg/Molex FDD order)"})

    # v0.2: drive power polyfuses (a shorted drive cable must not take down +5V/+3V3)
    sh.text("v0.2: drive supplies (J3, Amiga J7) via polyfuses; test points TP1-TP4.", 300, 200, 1.27)
    sh.part("Device", "Polyfuse", "F2", "1.1A hold 6V", 300, 215, {"1": "+5V", "2": "FDD5_F"},
            "Fuse:Fuse_1206_3216Metric", {"MPN": "SMD1206P110TF (1.1A hold, 6V)"})
    sh.part("Device", "Polyfuse", "F3", "1.1A hold 24V", 320, 215, {"1": "+12V", "2": "FDD12_F"},
            "Fuse:Fuse_1812_4532Metric", {"MPN": "SMD1812P110TF/24 (1.1A hold, 24V)"})

    # v0.5: drive supplies switched (P-FET high side, off while the MCU is in reset) and
    # measured (0.1R shunt + INA180A1, 20 V/V -> 2 V/A on the ADC), TVS at the outputs
    sh.text("v0.5: FDD_5V / FDD_12V: polyfuse -> 0.1R shunt (INA180A1 -> I_FDD5/I_FDD12, 2 V/A)\n"
            "-> AO3401A high-side switch (FDD5_EN/FDD12_EN high = on, 100k pull-downs = off in reset)\n"
            "-> TVS. 12V gate divider 10k/10k keeps Vgs at -6 V (AO3401A max +-12 V).", 300, 255, 1.27)
    for k, (rail, fuse_out, tvs, y) in enumerate([("5", "FDD5_F", "SMF5.0CA", 270),
                                                  ("12", "FDD12_F", "SMAJ15CA", 310)]):
        s, gate, en, out = f"FDD{rail}_S", f"FDD{rail}_G", f"G:FDD{rail}_EN", f"G:FDD_{rail}V"
        r0 = 26 + 3 * k                     # R26-R28 (5V), R29-R31 (12V), R32 12V gate series
        sh.part("Device", "R", f"R{r0}", "0.1R 1%", 300, y, {"1": fuse_out, "2": s},
                "Resistor_SMD:R_1206_3216Metric", {"MPN": "1206W4F100LT5E (0.1R 1% 1/4W)"})
        sh.part("Amplifier_Current", "INA180A1", f"U{12 + k}", "INA180A1", 300, y + 15, {
            "1": f"G:I_FDD{rail}", "2": "GND", "3": fuse_out, "4": s, "5": "+3V3"},
            "Package_TO_SOT_SMD:SOT-23-5", {"MPN": "INA180A1IDBVR"})
        sh.part("Device", "C", f"C{49 + k}", "100nF", 280, y + 15, {"1": "+3V3", "2": "GND"}, C0603)
        sh.part("Transistor_FET", "AO3401A", f"Q{1 + 2 * k}", "AO3401A", 330, y,
                {"1": gate, "2": s, "3": out}, "Package_TO_SOT_SMD:SOT-23")
        sh.part("Transistor_FET", "2N7002", f"Q{2 + 2 * k}", "2N7002", 350, y + 15,
                {"1": en, "2": "GND", "3": gate if rail == "5" else "FDD12_GD"},
                "Package_TO_SOT_SMD:SOT-23")
        sh.part("Device", "R", f"R{r0 + 1}", "100k" if rail == "5" else "10k", 330, y + 15,
                {"1": s, "2": gate}, R0603)
        sh.part("Device", "R", f"R{r0 + 2}", "100k", 370, y + 15, {"1": en, "2": "GND"}, R0603)
        if rail == "12":
            sh.part("Device", "R", "R32", "10k", 345, y + 25, {"1": gate, "2": "FDD12_GD"}, R0603)
        sh.part("Device", "D_TVS", f"D{13 + k}", tvs, 360, y, {"1": out, "2": "GND"},
                "Diode_SMD:D_SMF" if rail == "5" else "Diode_SMD:D_SMA", {"MPN": tvs})
    for i, (label, net) in enumerate([("TP_3V3", "+3V3"), ("TP_5V", "+5V"), ("TP_12V", "+12V"),
                                      ("TP_GND", "GND")]):
        testpoint(sh, f"TP{i + 1}", label, net, 300 + i * 12.7, 240)
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
    # v0.2: 8 MB QSPI PSRAM on OCTOSPIM port 1 (AF9; PB13 = IO2 is AF4), memory-mapped flux store
    "PB2": "PSRAM_CLK", "PB10": "PSRAM_CS",
    "PD11": "PSRAM_IO0", "PD12": "PSRAM_IO1", "PB13": "PSRAM_IO2", "PD13": "PSRAM_IO3",
    # v0.3: USB-C CC voltage (across Rd 5.1k) on ADC1 INP16 / INP15 -> source current 0.5/1.5/3 A
    "PA0": "G:CC1", "PA3": "G:CC2",
    "PF11": "G:FDD_DRATE",  # v0.4: DRATE for 3-mode drives (J6 pin 6 via solder jumper JP1)
    # v0.5: switched + measured drive supplies, write lock, board ID
    "PE2": "G:FDD5_EN", "PE3": "G:FDD12_EN",           # high = drive supply on (reset: off)
    "PC0": "G:I_FDD5", "PC1": "G:I_FDD12",             # ADC1 INP10/INP11, INA180A1: 2 V/A
    "PE4": "G:WLOCK",                                  # high = write lock jumper set
    "PA4": "BOARD_ID",                                 # ADC1 INP18, divider = board revision
    # v0.5: expansion header J9 (I2C1 AF4, 2 GPIO), front buttons SW3/SW4 (active low)
    "PB6": "I2C_SCL", "PB7": "I2C_SDA", "PE0": "EXP_IO1", "PE1": "EXP_IO2",
    "PB8": "BTN_A", "PB9": "BTN_B",
    # SDMMC2, 4 bit (PD6/PD7/PG9/PG10 AF11, PG11/PG12 AF10): v0.5 microSD, v0.6 soldered SD NAND
    # (PG13 card detect dropped with the slot)
    "PD6": "SD_CLK", "PD7": "SD_CMD", "PG9": "SD_D0", "PG10": "SD_D1", "PG11": "SD_D2",
    "PG12": "SD_D3",
    # v0.7: Apple Disk II port J14 (Apple_Port sheet).  RDDATA on TIM2_CH3 (same timebase as
    # the flux capture), WRDATA on TIM3_CH2 (toggle output: one level change per flux transition)
    "PF12": "G:APL_PH0", "PF13": "G:APL_PH1", "PG6": "G:APL_PH2", "PF15": "G:APL_PH3",
    "PG5": "G:APL_EN1", "PB12": "G:APL_EN2", "PB14": "G:APL_WRREQ", "PB15": "G:APL_WRPROT",   # PG5/PG6: PB11/PF14 not reachable in the layout
    "PA7": "G:APL_WRDATA",    # TIM3_CH2 (AF2)
    "PA2": "G:APL_RDDATA",    # TIM2_CH3 (AF1)
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
    # v0.6: front panel LEDs of the enclosure (one row per LED: + / -), parallel to the board
    # LEDs, 330R each (~4 mA at Vf 2 V)
    sh.text("v0.6: J12 front panel LEDs, 2 pins per LED: 1/2 PWR (green), 3/4 ACT, 5/6 FDD, 7/8 ERR\n"
            "(odd pin = anode via 330R, even pin = GND).", 290, 225, 1.27)
    for i, (src, net) in enumerate([("+3V3", "FRONT_PWR"), ("LED_ACT", "FRONT_ACT"),
                                    ("LED_FDD", "FRONT_FDD"), ("LED_ERR", "FRONT_ERR")]):
        sh.part("Device", "R", f"R{34 + i}", "330", 300 + i * 8, 240, {"1": src, "2": net}, R0603)
    sh.part("Connector_Generic", "Conn_02x04_Odd_Even", "J12", "FRONT_LED", 345, 245, {
        "1": "FRONT_PWR", "2": "GND", "3": "FRONT_ACT", "4": "GND",
        "5": "FRONT_FDD", "6": "GND", "7": "FRONT_ERR", "8": "GND"},
        "Connector_PinHeader_2.54mm:PinHeader_2x04_P2.54mm_Vertical",
        {"Note": "front LEDs: 1+ 2- PWR, 3+ 4- ACT, 5+ 6- FDD, 7+ 8- ERR"})

    # v0.2: QSPI PSRAM, 8 MB (OCTOSPI1 quad mode, memory-mapped at 0x90000000)
    sh.text("v0.2: APS6404L 8 MB QSPI PSRAM on OCTOSPIM P1 (PB2 CLK, PB10 NCS, PD11/PD12/PB13/PD13 IO0-3).\n"
            "CE# pulled up so the RAM stays deselected while the MCU is in reset.", 20, 235, 1.27)
    sh.part("Memory_RAM", "APS6404L-3SQRx-SN", "U11", "APS6404L-3SQR-SN", 120, 250, {
        "1": "PSRAM_CS", "2": "PSRAM_IO1", "3": "PSRAM_IO2", "4": "GND",
        "5": "PSRAM_IO0", "6": "PSRAM_CLK", "7": "PSRAM_IO3", "8": "+3V3"},
        "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", {"MPN": "APS6404L-3SQR-SN"})
    sh.part("Device", "C", "C46", "100nF", 150, 250, {"1": "+3V3", "2": "GND"}, C0603)
    sh.part("Device", "R", "R16", "10k", 90, 250, {"1": "+3V3", "2": "PSRAM_CS"}, R0603)

    # v0.5: board ID divider (10k/10k = 1.65 V = rev v0.5), expansion header, buttons, microSD
    sh.text("BOARD_ID divider on PA4 (10k/10k -> v0.5, 10k/4.7k -> v0.6, 10k/2.2k -> v0.7). J9 expansion: I2C1 (2.2k pull-ups), 2 GPIO,\n"
            "button lines, 3V3/5V. SW3/SW4 front buttons (active low, MCU pull-ups). v0.6: SD NAND U15 on\n"
            "SDMMC2 (soldered, 4 GB), 4 bit, 47k pull-ups on CMD/DAT.", 20, 280, 1.27)
    sh.part("Device", "R", "R17", "10k", 30, 300, {"1": "+3V3", "2": "BOARD_ID"}, R0603)
    # board revision divider: 10k/10k = 1650 mV (v0.5), 10k/4.7k = 1055 mV (v0.6, SD NAND),
    # 10k/2.2k = 595 mV (v0.7, Apple Disk II port)
    sh.part("Device", "R", "R18", "2.2k", 30, 315, {"1": "BOARD_ID", "2": "GND"}, R0603)
    sh.part("Device", "R", "R19", "2.2k", 60, 300, {"1": "+3V3", "2": "I2C_SCL"}, R0603)
    sh.part("Device", "R", "R20", "2.2k", 60, 315, {"1": "+3V3", "2": "I2C_SDA"}, R0603)
    sh.part("Connector_Generic", "Conn_02x05_Odd_Even", "J9", "EXPANSION", 100, 300, {
        "1": "+3V3", "2": "+5V", "3": "I2C_SCL", "4": "I2C_SDA", "5": "EXP_IO1", "6": "EXP_IO2",
        "7": "BTN_A", "8": "BTN_B", "9": "GND", "10": "GND"},
        "Connector_PinHeader_2.54mm:PinHeader_2x05_P2.54mm_Vertical",
        {"Note": "1 3V3, 2 5V, 3 SCL, 4 SDA, 5/6 GPIO PE0/PE1, 7/8 buttons (low = pressed), 9/10 GND"})
    # v0.6: external 3-position mode switch (ON-OFF-ON) in the enclosure; pin order = switch
    # pin order (common in the middle).  Same MCU inputs as J9 pins 5/6 (PE0/PE1, pull-downs).
    sh.part("Connector_Generic", "Conn_01x03", "J13", "MODE_SW", 100, 330, {
        "1": "EXP_IO1", "2": "+3V3", "3": "EXP_IO2"},
        "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
        {"Note": "mode switch: 1 = USB floppy, 2 = common (3V3), 3 = SD drive; open = flux"})
    sh.part("Switch", "SW_Push", "SW3", "BTN_A", 140, 300, {"1": "BTN_A", "2": "GND"}, BTN)
    sh.part("Switch", "SW_Push", "SW4", "BTN_B", 140, 315, {"1": "BTN_B", "2": "GND"}, BTN)
    # v0.6: soldered SD NAND instead of the microSD slot (board lives in a closed case);
    # same land pattern fits MKDV8GIL-AST (1 GB, C26159627) as a cheaper alternative
    sh.part("UFI_Headless", "SD_NAND_LGA8", "U15", "CSNP32GCR01-AOW", 220, 300, {
        "1": "SD_D2", "2": "SD_D3", "3": "SD_CLK", "4": "GND",
        "5": "SD_CMD", "6": "SD_D0", "7": "SD_D1", "8": "+3V3"},
        "UFI_Headless:SD_NAND_LGA-8_6x8mm_P1.27mm",
        {"MPN": "CSNP32GCR01-AOW", "Note": "4 GB; alt. MKDV8GIL-AST 1 GB (C26159627), same footprint"})
    for i, net in enumerate(["SD_CMD", "SD_D0", "SD_D1", "SD_D2", "SD_D3"]):
        sh.part("Device", "R", f"R{21 + i}", "47k", 180 + i * 8, 330, {"1": "+3V3", "2": net}, R0603)
    sh.part("Device", "C", "C47", "100nF", 250, 300, {"1": "+3V3", "2": "GND"}, C0603)
    sh.part("Device", "C", "C48", "10uF", 260, 300, {"1": "+3V3", "2": "GND"}, C0805)
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
    ("FDD_WGATE_G", "FD_WGATE"), ("FDD_WDATA", "FD_WDATA"), ("FDD_DENSITY", "FD_DENSITY"),
    ("FDD_DRATE", "FD_DRATE")]  # v0.4: spare LS07 gate, reaches J6 pin 6 only via JP1
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
    # v0.2: scope test points on the MCU side of the flux inputs
    testpoint(sh, "TP5", "TP_RDATA", "G:FDD_RDATA", 360, 60)
    testpoint(sh, "TP6", "TP_INDEX", "G:FDD_INDEX", 360, 75)

    # v0.5: write lock - jumper J11 forces the WGATE driver input high (released) in hardware,
    # whatever the firmware does; the MCU reads the jumper on WLOCK (PE4)
    sh.text("v0.5: WRITE LOCK jumper J11 set -> WLOCK high -> 74LVC1G32 output high -> WGATE released.\n"
            "Open (default, 100k pull-down) -> WGATE follows the MCU. TP7: WDATA (MCU), TP8: WGATE after the gate.",
            20, 280, 1.27)
    sh.part("74xGxx", "74LVC1G32", "U14", "74LVC1G32GW", 120, 300, {
        "1": "G:FDD_WGATE", "2": "G:WLOCK", "3": "GND", "4": "G:FDD_WGATE_G", "5": "+3V3"},
        "Package_TO_SOT_SMD:SOT-353_SC-70-5", {"MPN": "74LVC1G32GW,125"})
    sh.part("Device", "C", "C51", "100nF", 150, 300, {"1": "+3V3", "2": "GND"}, C0603)
    sh.part("Connector_Generic", "Conn_01x02", "J11", "WRITE_LOCK", 80, 300,
            {"1": "+3V3", "2": "G:WLOCK"}, "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
            {"Note": "jumper set = no writes possible"})
    sh.part("Device", "R", "R33", "100k", 80, 320, {"1": "G:WLOCK", "2": "GND"}, R0603)
    testpoint(sh, "TP7", "TP_WDATA", "G:FDD_WDATA", 360, 90)
    testpoint(sh, "TP8", "TP_WGATE", "G:FDD_WGATE_G", 360, 105)   # after the lock gate
    return sh


# ----------------------------------------------------------------------------
# FDD connectors: 34-pin Shugart/PC and Amiga 2x12 header (pin n = DB23 pin n)
# ----------------------------------------------------------------------------
def build_fdd_conn() -> Sheet:
    sh = Sheet("UFI Headless - FDD Connectors", PROJECT)
    sh.text("FDD CONNECTORS\n"
            "J: 34-pin IBM PC / Shugart. Odd pins GND. Pins 10/12/14/16 = MOTEA/DRVSB/DRVSA/MOTEB (PC)\n"
            "   resp. DS0/DS1/DS2/MOTOR (Shugart, DS3 = pin 6 via JP1); the drive type selects the bus.\n"
            "   Pin 34 = DSKCHG (PC) / READY (Shugart); Shugart /DCD on pin 2 is not readable (DENSITY output).\n"
            "J: Amiga external drive, 2x12 header, pin n = DB23 pin n (Amiga HRM pinout), pin 24 GND.\n"
            "   Adapter cable to DB23. SEL1B shares DRVSEL_B, SEL2B = DRVSEL_A via JP3 (DF2),\n"
            "   MTRXD shares MOTOR_B (each drive latches it on its select edge). Only one drive at a time.\n"
            "   CHECK: docs/Amiga_DB23_Adapter_Cable.md uses a different (inconsistent) pinout.", 20, 20, 1.5)
    pc = {str(n): "GND" for n in range(1, 34, 2)}
    pc.update({"2": "FD_DENSITY", "3": "FD_PIN3", "4": "NC", "6": "FD_PIN6", "8": "FD_INDEX", "10": "FD_MOTOR_A",
               "12": "FD_DRVSEL_B", "14": "FD_DRVSEL_A", "16": "FD_MOTOR_B", "18": "FD_DIR",
               "20": "FD_STEP", "22": "FD_WDATA", "24": "FD_WGATE", "26": "FD_TRK0",
               "28": "FD_WPROT", "30": "FD_RDATA", "32": "FD_SIDE", "34": "FD_DSKCHG"})
    pc = {k: (v if v in ("GND", "NC", "FD_PIN3", "FD_PIN6") else f"G:{v}") for k, v in pc.items()}
    sh.part("Connector_Generic", "Conn_02x17_Odd_Even", ref("J"), "FDD_34PIN", 90, 110, pc,
            "Connector_IDC:IDC-Header_2x17_P2.54mm_Vertical", {"Note": "IBM PC / Shugart floppy bus"})

    amiga = {"1": "FD_READY", "2": "FD_RDATA", "3": "GND", "4": "GND", "5": "GND", "6": "GND",
             "7": "GND", "8": "FD_MOTOR_B", "9": "AMI_SEL2", "10": "AMI_DRES", "11": "FD_DSKCHG",
             "12": "FDD_5V", "13": "FD_SIDE", "14": "FD_WPROT", "15": "FD_TRK0", "16": "FD_WGATE",
             "17": "FD_WDATA", "18": "FD_STEP", "19": "FD_DIR", "20": "AMI_SEL3",
             "21": "FD_DRVSEL_B", "22": "FD_INDEX", "23": "FDD_12V", "24": "GND"}
    amiga = {k: (f"G:{v}" if v.startswith(("FD_", "FDD_")) else v) for k, v in amiga.items()}
    sh.part("Connector_Generic", "Conn_02x12_Odd_Even", ref("J"), "AMIGA_FDD", 260, 110, amiga,
            "Connector_IDC:IDC-Header_2x12_P2.54mm_Vertical",
            {"Note": "Pin n = Amiga DB23 pin n; RDY,DKRD,GND x5,MTRXD,SEL2B,DRESB,CHNG,+5V,SIDEB,"
                     "WPRO,TK0,DKWEB,DKWDB,STEPB,DIRB,SEL3B,SEL1B,INDEX,+12V"})
    # unused Amiga selects and drive reset held inactive
    pullups(sh, 260, 190, ["AMI_SEL2", "AMI_SEL3", "AMI_DRES"])

    # v0.2: ESD on every bus line that reaches the external Amiga port (+ J6 selects)
    sh.text("v0.2: ESDA6V1-5SC6 TVS arrays on all Amiga J7 bus lines + MOTOR_A (J7 is an external port).",
            20, 230, 1.27)
    esd_nets = [f"G:FD_{n}" for n in ("READY", "RDATA", "MOTOR_B", "DSKCHG", "SIDE", "WPROT", "TRK0",
                                      "WGATE", "WDATA", "STEP", "DIR", "DRVSEL_B", "INDEX",
                                      "MOTOR_A", "DRVSEL_A")]  # v0.7: DRVSEL_A reaches J7 via JP3
    for i in range(3):
        esd5(sh, f"D{9 + i}", esd_nets[i * 5:(i + 1) * 5], 120 + i * 40, 250)

    # v0.4 (ideas from Monster FDC, concept only): J6 pin 6 = DRATE for 3-mode drives via
    # open jumper JP1; J6 pin 3 = GND (JP2 bridged 1-2) or +5V for PS/2 drives (cut, bridge 2-3)
    sh.text("v0.4: JP1 open = J6 pin 6 unused, bridge = DRATE (3-mode drives).\n"
            "JP2 default 1-2 = pin 3 GND; 2-3 = +5V (fused FDD_5V) ONLY for PS/2 drives that take power on pin 3.",
            20, 280, 1.27)
    sh.part("Jumper", "SolderJumper_2_Open", "JP1", "DRATE", 120, 300,
            {"1": "G:FD_DRATE", "2": "FD_PIN6"}, "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm",
            in_bom=False)
    sh.part("Jumper", "SolderJumper_3_Bridged12", "JP2", "PIN3_GND/5V", 160, 300,
            {"1": "GND", "2": "FD_PIN3", "3": "G:FDD_5V"},
            "Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm", in_bom=False)
    # v0.7: second external Amiga drive (DF2) on J7 pin 9 SEL2B, selected by DRVSEL_A
    sh.text("v0.7: JP3 open = J7 pin 9 SEL2B idle (pull-up); bridge = SEL2B driven by DRVSEL_A (second\n"
            "Amiga drive DF2, drive type amiga2). DRVSEL_A also selects J6 drive A / DS2: one bus user at a time.",
            20, 320, 1.27)
    sh.part("Jumper", "SolderJumper_2_Open", "JP3", "AMI_SEL2", 200, 300,
            {"1": "G:FD_DRVSEL_A", "2": "AMI_SEL2"}, "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm",
            in_bom=False)
    return sh


# ----------------------------------------------------------------------------
# IEC bus (Commodore serial)
# ----------------------------------------------------------------------------
IEC_LINES = ["ATN", "CLK", "DATA", "SRQ", "RESET"]


def build_iec() -> Sheet:
    sh = Sheet("UFI Headless - IEC Bus", PROJECT)
    sh.text("IEC BUS (Commodore serial) - 1x6 header, pin n = DIN-6 pin n, DIN socket external\n"
            "UFI acts as host: 1k pull-ups to +5V on all lines.\n"
            "IEC_x_OUT low = line pulled low (SN74LS07 open collector).\n"
            "IEC_x_IN high = line is low (74LVC14A inverter).", 20, 20, 1.5)
    hex_buffer(sh, "oc", 80, 60, [(f"G:IEC_{n}_OUT", f"IEC_{n}") for n in IEC_LINES], (140, 60))
    hex_buffer(sh, "st", 220, 60, [(f"IEC_{n}", f"G:IEC_{n}_IN") for n in IEC_LINES], (280, 60))
    pullups(sh, 80, 160, [f"IEC_{n}" for n in IEC_LINES])
    sh.part("Connector_Generic", "Conn_01x06", ref("J"), "IEC_HDR", 300, 170, {
        "1": "IEC_SRQ", "2": "GND", "3": "IEC_ATN", "4": "IEC_CLK", "5": "IEC_DATA", "6": "IEC_RESET"},
        "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical",
        {"Note": "Pin n = DIN-6 pin n (1 SRQ, 2 GND, 3 ATN, 4 CLK, 5 DATA, 6 RESET); DIN socket external"})
    # v0.2: ESD on the external IEC lines
    esd5(sh, "D12", [f"IEC_{n}" for n in IEC_LINES], 300, 210)
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


# ----------------------------------------------------------------------------
# v0.7: Apple II Disk II port (20-pin, as on the Disk II controller card)
# ----------------------------------------------------------------------------
TSSOP20 = "Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm"
SOT363 = "Package_TO_SOT_SMD:SOT-363_SC-70-6"
SOIC8 = "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm"
APPLE_OUT = ["PH0", "PH1", "PH2", "PH3", "EN1", "EN2", "WRREQ", "WRDATA"]


def build_apple() -> Sheet:
    sh = Sheet("UFI Headless - Apple Disk II Port", PROJECT)
    sh.text("APPLE II DISK II PORT (v0.7) - J14 2x10, pinout of the Disk II controller card drive connector:\n"
            "1/3/5/7 GND, 2/4/6/8 PH0-PH3, 9 -12V, 10 /WRREQ, 11/12 +5V, 13/15/17/19 +12V, 14 /ENABLE,\n"
            "16 RDDATA, 18 WRDATA, 20 WRPROT.  J15: /ENABLE of a second drive (DB19 pin 9 /DRIVE2 or a\n"
            "second Disk II header via adapter cable).  DB19 drives (Apple 5.25 Drive) via adapter cable;\n"
            "Apple 3.5 / UniDisk 3.5 (/EN3.5, SmartPort) are not supported.\n"
            "Outputs: SN74AHCT244 at FDD_5V (TTL levels, unpowered with the drive supply; inputs 5.5 V\n"
            "tolerant at any VCC).  MCU side 10k: phases pulled low, enables and /WRREQ pulled high (reset safe).\n"
            "PH0-3 active high, /ENABLE and /WRREQ active low, WRDATA toggles once per flux transition.\n"
            "Inputs: RDDATA, WRPROT 10k to FDD_5V -> 74LVC2G17 at 3V3 (5 V tolerant) -> MCU.\n"
            "-12V: ICL7662 inverter from FDD_12V (~100 R source, Disk II analog card load only).",
            20, 20, 1.27)

    # output buffer, both halves always enabled
    conn = {"1": "GND", "19": "GND", "10": "GND", "20": "G:FDD_5V"}
    a_pins = ["2", "4", "6", "8", "17", "15", "13", "11"]   # 1A0..1A3, 2A0..2A3
    y_pins = ["18", "16", "14", "12", "3", "5", "7", "9"]   # 1Y0..1Y3, 2Y0..2Y3
    for s, a, y in zip(APPLE_OUT, a_pins, y_pins):
        conn[a] = f"G:APL_{s}"
        conn[y] = f"AP_{s}"
    sh.part("74xx", "74AHCT244", "U16", "SN74AHCT244PWR", 80, 90, conn, TSSOP20,
            {"MPN": "SN74AHCT244PWR"})
    sh.part("Device", "C", "C52", "100nF", 120, 70, {"1": "G:FDD_5V", "2": "GND"}, C0603)
    # FDD_5V / FDD_12V come from the shunts (passive) on the power sheet
    sh.pwr_flag("G:FDD_5V", 140, 60)
    sh.pwr_flag("G:FDD_12V", 150, 60)
    # reset-safe levels on the MCU side (floating GPIOs at power-up)
    sh.part("Device", "R_Pack04", "RN9", "10k", 40, 150, {
        "1": "G:APL_PH0", "8": "GND", "2": "G:APL_PH1", "7": "GND",
        "3": "G:APL_PH2", "6": "GND", "4": "G:APL_PH3", "5": "GND"}, RPACK4)
    sh.part("Device", "R_Pack04", "RN10", "10k", 60, 150, {
        "1": "G:APL_EN1", "8": "+3V3", "2": "G:APL_EN2", "7": "+3V3",
        "3": "G:APL_WRREQ", "6": "+3V3", "4": "NC", "5": "NC"}, RPACK4)

    # input buffer
    props = {"MPN": "74LVC2G17GW,125"}
    sh.part("74xGxx", "74LVC2G17", "U17", "74LVC2G17GW", 180, 90, {"1": "AP_RDDATA", "6": "G:APL_RDDATA"},
            SOT363, props, unit=1)
    sh.part("74xGxx", "74LVC2G17", "U17", "74LVC2G17GW", 180, 110, {"3": "AP_WRPROT", "4": "G:APL_WRPROT"},
            SOT363, props, unit=2)
    sh.part("74xGxx", "74LVC2G17", "U17", "74LVC2G17GW", 210, 100, {"5": "+3V3", "2": "GND"},
            SOT363, props, unit=3)
    sh.part("Device", "C", "C53", "100nF", 230, 100, {"1": "+3V3", "2": "GND"}, C0603)
    sh.part("Device", "R", "R38", "10k", 160, 80, {"1": "G:FDD_5V", "2": "AP_RDDATA"}, R0603)
    sh.part("Device", "R", "R39", "10k", 160, 120, {"1": "G:FDD_5V", "2": "AP_WRPROT"}, R0603)

    # -12V charge pump (ICL7662: up to 20 V in, LV and OSC open)
    sh.part("Regulator_SwitchedCapacitor", "ICL7660", "U18", "ICL7662EBA+T", 80, 220, {
        "1": "NC", "2": "M12_CP", "3": "GND", "4": "M12_CN", "5": "AP_M12V", "6": "NC", "7": "NC",
        "8": "G:FDD_12V"}, SOIC8, {"MPN": "ICL7662EBA+T"})
    sh.part("Device", "C", "C54", "100nF", 40, 220, {"1": "G:FDD_12V", "2": "GND"}, C0603)
    sh.part("Device", "C", "C55", "10uF/25V", 120, 210, {"1": "M12_CP", "2": "M12_CN"}, C1206)
    sh.part("Device", "C", "C56", "10uF/25V", 120, 235, {"1": "GND", "2": "AP_M12V"}, C1206)

    # connectors
    hdr = {"1": "GND", "3": "GND", "5": "GND", "7": "GND",
           "2": "AP_PH0", "4": "AP_PH1", "6": "AP_PH2", "8": "AP_PH3", "9": "AP_M12V", "10": "AP_WRREQ",
           "11": "G:FDD_5V", "12": "G:FDD_5V", "13": "G:FDD_12V", "15": "G:FDD_12V", "17": "G:FDD_12V",
           "19": "G:FDD_12V", "14": "AP_EN1", "16": "AP_RDDATA", "18": "AP_WRDATA", "20": "AP_WRPROT"}
    sh.part("Connector_Generic", "Conn_02x10_Odd_Even", "J14", "DISK_II", 300, 120, hdr,
            "UFI_Headless:IDC-Header_2x10_P2.54mm_Vertical_UFI",  # silk edge clears the J7 pin-1 mark
            {"Note": "Apple Disk II drive connector (controller card pinout)"})
    sh.part("Connector_Generic", "Conn_01x02", "J15", "APPLE_EN2", 300, 190,
            {"1": "AP_EN2", "2": "GND"}, "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
            {"Note": "/ENABLE second drive (DB19 pin 9 /DRIVE2)"})
    # v0.7: sync sensor input for Disk II drives (Applesauce-style: magnet on the spindle +
    # A3144 hall sensor, open collector with 10k pull-up, active low, >= 500 us per turn).
    # Same electrical behaviour as a drive's INDEX output, so it joins the 34-pin FD_INDEX
    # line directly (wired OR, 1k pull-up, ESD D11, LVC14 -> TIM2_CH2 index capture)
    sh.text("J19 SYNC sensor (hall, open collector, active low): 1 FDD_5V, 2 GND, 3 SYNC -> FD_INDEX directly\n"
            "(wired OR with the 34-pin drives, which only drive INDEX while selected). Firmware: UFI.CFG apple_sync=1.",
            20, 280, 1.27)
    sh.part("Connector_Generic", "Conn_01x03", "J19", "SYNC_SENSOR", 300, 280,
            {"1": "G:FDD_5V", "2": "GND", "3": "G:FD_INDEX"},
            "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
            {"Note": "Disk II sync sensor: 1 +5V, 2 GND, 3 SYNC (open collector, active low)"})

    # ESD on everything that leaves the board
    esd5(sh, "D15", ["AP_PH0", "AP_PH1", "AP_PH2", "AP_PH3", "AP_EN1"], 250, 230)
    esd5(sh, "D16", ["AP_EN2", "AP_WRREQ", "AP_WRDATA", "AP_RDDATA", "AP_WRPROT"], 290, 230)
    return sh


def main():
    root = Sheet("UFI Headless", PROJECT)
    root.text("UFI Headless - STM32H723 flux engine without CM5.\n"
              "Sheets: Power, MCU_Core, Flux_Interface, FDD_Connectors, IEC_Bus, Apple_Port.",
              20, 20, 2)
    blocks = []
    subs = [("Power", "Power.kicad_sch", build_power()),
            ("MCU_Core", "MCU_Core.kicad_sch", build_core()),
            ("Flux_Interface", "Flux_Interface.kicad_sch", build_flux()),
            ("FDD_Connectors", "FDD_Connectors.kicad_sch", build_fdd_conn()),
            ("IEC_Bus", "IEC_Bus.kicad_sch", build_iec()),
            ("Apple_Port", "Apple_Port.kicad_sch", build_apple())]
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
