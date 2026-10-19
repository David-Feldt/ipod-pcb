"""E-ink Pod schematic definition. Run: python3 gen/build_sch.py"""
import os

from schbuilder import Lib, Schematic

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "kicad")

R0402 = "Resistor_SMD:R_0402_1005Metric"
C0402 = "Capacitor_SMD:C_0402_1005Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"

LCSC_R = {"22": "C25092", "10k": "C25744", "5.1k": "C25905", "1M": "C26083", "100k": "C25741",
          "1k": "C11702", "2k": "C4109", "2.2": "C25158", "470": "C25117"}
# (value, footprint) -> LCSC
LCSC_C = {("100nF", C0402): "C1525", ("1uF", C0402): "C52923",
          ("2.2uF", C0402): "C12530", ("2.2nF", C0402): "C1531",
          ("10uF", C0603): "C19702", ("10uF", C0805): "C15850", ("22uF", C0603): "C59461",
          ("1uF 50V", C0603): "C15849", ("4.7uF 25V", C0805): "C1779",
          ("10nF", C0402): "C15195"}

sch = Schematic("eink-pod", "E-ink Pod - ESP32-S3 e-paper Spotify remote", Lib())


def R(ref, val, a, b, x, y):
    sch.add(ref, "Device:R", val, R0402, x, y, {"1": a, "2": b}, lcsc=LCSC_R[val])


def C(ref, val, a, b, x, y, fp=C0402):
    sch.add(ref, "Device:C", val, fp, x, y, {"1": a, "2": b}, lcsc=LCSC_C[(val, fp)])


def row(x, y, parts, dx=10.16):
    """Place a row of two-terminal passives: (kind, ref, val, net1, net2[, fp])."""
    for i, (kind, ref, val, a, b, *fp) in enumerate(parts):
        if kind == "R":
            R(ref, val, a, b, x + i * dx, y)
        else:
            C(ref, val, a, b, x + i * dx, y, *fp)


# ---------------------------------------------------------------- USB + power
sch.text("USB-C, charger, power path, 3.3 V", 20, 20)
sch.add("J1", "Connector:USB_C_Receptacle_USB2.0_16P", "USB-C",
        "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12", 40, 60,
        {"A1": "GND", "B1": "GND", "A12": "GND", "B12": "GND", "SH": "GND",
         "A4": "VBUS", "A9": "VBUS", "B4": "VBUS", "B9": "VBUS",
         "A5": "CC1", "B5": "CC2", "A6": "USB_DP", "B6": "USB_DP",
         "A7": "USB_DN", "B7": "USB_DN"},
        lcsc="C165948", mpn="TYPE-C-31-M-12")
row(75, 95, [("R", "R1", "5.1k", "CC1", "GND"), ("R", "R2", "5.1k", "CC2", "GND")])
# ESD array taps the data lines (pins 1/6 and 3/4 are joined internally)
sch.add("U6", "Power_Protection:USBLC6-2SC6", "USBLC6-2SC6",
        "Package_TO_SOT_SMD:SOT-23-6", 90, 60,
        {"1": "USB_DP", "6": "USB_DP", "3": "USB_DN", "4": "USB_DN",
         "2": "GND", "5": "VBUS"}, lcsc="C7519")

# Li-ion charger, 500 mA (PROG = 2k)
sch.add("U4", "Battery_Management:MCP73831-2-OT", "MCP73831-2-OT",
        "Package_TO_SOT_SMD:SOT-23-5", 140, 50,
        {"1": "CHG_STAT", "2": "GND", "3": "VBAT", "4": "VBUS", "5": "CHG_PROG"},
        lcsc="C424093")
row(120, 75, [("R", "R3", "2k", "CHG_PROG", "GND"), ("C", "C1", "10uF", "VBUS", "GND", C0805),
              ("C", "C2", "10uF", "VBAT", "GND", C0805), ("R", "R4", "1k", "VBUS", "LED_A")])
sch.add("D5", "Device:LED", "red", "LED_SMD:LED_0603_1608Metric", 170, 75,
        {"2": "LED_A", "1": "CHG_STAT"}, lcsc="C2286")
sch.add("J2", "Connector_Generic_MountingPin:Conn_01x02_MountingPin", "LiPo 1S",
        "Connector_JST:JST_PH_S2B-PH-SM4-TB_1x02-1MP_P2.00mm_Horizontal", 190, 50,
        {"1": "VBAT", "2": "GND", "MP": "GND"}, lcsc="C295747", mpn="S2B-PH-SM4-TB")

# power path: USB present -> VSYS from VBUS via Schottky, battery FET off
sch.text("Power path", 20, 110)
sch.add("D1", "Device:D_Schottky", "B5819W", "Diode_SMD:D_SOD-123", 40, 125,
        {"2": "VBUS", "1": "VSYS"}, lcsc="C8598")
sch.add("Q1", "Transistor_FET:AO3401A", "AO3401A", "Package_TO_SOT_SMD:SOT-23",
        70, 130, {"1": "VBUS", "3": "VBAT", "2": "VSYS"}, lcsc="C15127")
row(90, 130, [("R", "R5", "100k", "VBUS", "GND")])

# 3.3 V LDO, slide switch drives EN (hold / off)
sch.add("U5", "Regulator_Linear:AP2112K-3.3", "AP2112K-3.3",
        "Package_TO_SOT_SMD:SOT-23-5", 130, 125,
        {"1": "VSYS", "2": "GND", "3": "LDO_EN", "5": "+3V3"}, lcsc="C51118")
sch.add("SW8", "Switch:SW_SPDT", "POWER", "Button_Switch_SMD:SW_SPDT_Shouhan_MSK12C02",
        105, 160, {"2": "LDO_EN", "1": "VSYS", "3": "GND"}, lcsc="C431540")
row(150, 125, [("C", "C3", "10uF", "VSYS", "GND", C0603), ("C", "C4", "10uF", "+3V3", "GND", C0603)])

# Battery divider is disconnected from VBAT whenever +3V3 is off.
# Q4 source goes to VBAT: its body diode must NOT feed the divider when off.
sch.text("Isolated battery sense / active-low USB detection", 20, 170)
sch.add("Q4", "Transistor_FET:AO3401A", "AO3401A", "Package_TO_SOT_SMD:SOT-23",
        30, 190, {"1": "BAT_SENSE_GATE", "2": "VBAT", "3": "VBAT_MEAS"}, lcsc="C15127")
sch.add("Q5", "Transistor_FET:AO3400A", "AO3400A", "Package_TO_SOT_SMD:SOT-23",
        60, 190, {"1": "+3V3", "2": "GND", "3": "BAT_SENSE_GATE"}, lcsc="C20917")
R("R9", "1M", "VBAT", "BAT_SENSE_GATE", 85, 190)
R("R11", "1M", "+3V3", "GND", 100, 190)
row(120, 190, [("R", "R6", "1M", "VBAT_MEAS", "VBAT_SENSE"), ("R", "R7", "1M", "VBAT_SENSE", "GND"),
              ("C", "C5", "100nF", "VBAT_SENSE", "GND"),
              ("R", "R8", "100k", "+3V3", "USB_PRESENT_N")])
# VBUS drives only an insulated MOSFET gate. R5 discharges VBUS on unplug.
sch.add("Q3", "Transistor_FET:AO3400A", "AO3400A", "Package_TO_SOT_SMD:SOT-23",
        185, 190, {"1": "VBUS", "2": "GND", "3": "USB_PRESENT_N"}, lcsc="C20917")

for i, n in enumerate(["GND", "VBUS", "VSYS"]):
    sch.pwr_flag(n, 30 + i * 15, 215)

# ---------------------------------------------------------------- MCU
sch.text("ESP32-S3-MINI-1", 230, 20)
sch.add("U1", "RF_Module:ESP32-S3-MINI-1", "ESP32-S3-MINI-1-N8",
        "eink-pod:ESP32-S3-MINI-1", 260, 90,
        dict({"1": "GND", "3": "+3V3", "45": "EN", "4": "BOOT",
              "5": "VBAT_SENSE", "6": "USB_PRESENT_N", "8": "VOL_SENSE", "9": "VOL_EN",
              "13": "BTN_MENU", "14": "BTN_PLAY", "15": "BTN_PREV", "16": "BTN_NEXT",
              "17": "BTN_SEL", "20": "EPD_SCK",
              "21": "EPD_MOSI", "22": "EPD_CS", "23": "USB_MCU_N", "24": "USB_MCU_P",
              "25": "EPD_DC", "39": "TXD0", "40": "RXD0",
              "27": "EPD_RST", "30": "EPD_BUSY"},
             **{str(n): "GND" for n in [2] + list(range(42, 44)) + list(range(46, 66))}),
        lcsc="C2913206")
R("R12", "22", "USB_DP", "USB_MCU_P", 300, 165)
R("R13", "22", "USB_DN", "USB_MCU_N", 315, 165)
# extra bulk for Wi-Fi TX bursts (~350 mA peaks)
row(230, 140, [("C", "C9", "22uF", "+3V3", "GND", C0603),
               ("C", "C6", "10uF", "+3V3", "GND", C0603), ("C", "C7", "100nF", "+3V3", "GND"),
               ("R", "R10", "10k", "+3V3", "EN"), ("C", "C8", "1uF", "EN", "GND")])
sch.add("SW6", "Switch:SW_Push", "BOOT", "Button_Switch_SMD:SW_SPST_TS-1088-xR020",
        240, 165, {"1": "BOOT", "2": "GND"}, lcsc="C720477")
sch.add("SW7", "Switch:SW_Push", "RESET", "Button_Switch_SMD:SW_SPST_TS-1088-xR020",
        265, 165, {"1": "EN", "2": "GND"}, lcsc="C720477")
for i, n in enumerate(["TXD0", "RXD0", "GND", "+3V3", "VBAT"]):
    sch.add("TP%d" % (i + 1), "Connector:TestPoint", n, "TestPoint:TestPoint_Pad_D1.0mm",
            230 + i * 10, 185, {"1": n})

# ---------------------------------------------------------------- buttons
sch.text("Navigation buttons (iPod cross)", 230, 200)
for i, (ref, n) in enumerate([("SW1", "BTN_MENU"), ("SW2", "BTN_PLAY"), ("SW3", "BTN_PREV"),
                              ("SW4", "BTN_NEXT"), ("SW5", "BTN_SEL")]):
    sch.add(ref, "Switch:SW_Push", n.split("_")[1], "Button_Switch_SMD:SW_SPST_TS-1088-xR020",
            240, 215 + i * 10, {"1": n, "2": "GND"}, lcsc="C720477")

# ---------------------------------------------------------------- volume wheel
# Thumbwheel pot is powered from a GPIO only while it is read, so it draws nothing in sleep.
sch.text("Volume thumbwheel (powered by VOL_EN only while reading)", 230, 265)
sch.add("RV1", "Device:R_Potentiometer_MountingPin", "10k", "eink-pod:Alps_RK10J1xR_Thumbwheel",
        250, 285, {"1": "GND", "2": "VOL_SENSE", "3": "VOL_EN", "MP": "GND"},
        lcsc="C470766", mpn="RK10J11R0A0H")
C("C36", "10nF", "VOL_SENSE", "GND", 270, 285)

# ---------------------------------------------------------------- mounting holes
for i in range(4):
    sch.add("H%d" % (i + 1), "Mechanical:MountingHole", "M2.5", "MountingHole:MountingHole_2.7mm_M2.5",
            300 + i * 10, 285, {})

# ---------------------------------------------------------------- e-paper
sch.text("2.13in e-paper GDEY0213B74 (SSD1680) + gate-voltage boost", 20, 240)
sch.add("J4", "Connector_Generic_MountingPin:Conn_01x24_MountingPin", "GDEY0213B74",
        "eink-pod:FPC-05FB-24PH20", 60, 320,
        {"2": "EPD_GDR", "3": "EPD_RESE", "5": "EPD_VSH2", "8": "GND", "9": "EPD_BUSY",
         "10": "EPD_RST", "11": "EPD_DC", "12": "EPD_CS", "13": "EPD_SCK", "14": "EPD_MOSI",
         "15": "+3V3", "16": "+3V3", "17": "GND", "18": "EPD_VDD", "20": "EPD_VSH1",
         "21": "EPD_PREVGH", "22": "EPD_VSL", "23": "EPD_PREVGL", "24": "EPD_VCOM",
         "MP": "GND"}, lcsc="C2856831", mpn="FPC-05FB-24PH20")
sch.add("L1", "Device:L", "10uH", "Inductor_SMD:L_Sunlord_SWPA4030S", 120, 270,
        {"1": "+3V3", "2": "EPD_SW"}, lcsc="C38117", mpn="SWPA4030S100MT")
sch.add("Q2", "Transistor_FET:AO3400A", "AO3400A", "Package_TO_SOT_SMD:SOT-23", 150, 290,
        {"1": "EPD_GDR", "3": "EPD_SW", "2": "EPD_RESE"}, lcsc="C20917")
sch.add("D2", "Device:D_Schottky", "B5819W", "Diode_SMD:D_SOD-123", 180, 270,
        {"2": "EPD_SW", "1": "EPD_PREVGH"}, lcsc="C8598")
sch.add("D3", "Device:D_Schottky", "B5819W", "Diode_SMD:D_SOD-123", 180, 300,
        {"2": "EPD_X", "1": "GND"}, lcsc="C8598")
sch.add("D4", "Device:D_Schottky", "B5819W", "Diode_SMD:D_SOD-123", 180, 320,
        {"2": "EPD_PREVGL", "1": "EPD_X"}, lcsc="C8598")
row(110, 345, [("R", "R21", "1M", "EPD_GDR", "GND"), ("R", "R22", "2.2", "EPD_RESE", "GND"),
               ("C", "C26", "4.7uF 25V", "EPD_SW", "EPD_X", C0805),
               ("C", "C27", "10uF", "+3V3", "GND", C0603), ("C", "C28", "1uF", "+3V3", "GND"),
               ("C", "C29", "1uF", "EPD_VDD", "GND")])
row(110, 375, [("C", "C30", "1uF 50V", "EPD_PREVGH", "GND", C0603),
               ("C", "C31", "1uF 50V", "EPD_PREVGL", "GND", C0603),
               ("C", "C32", "1uF 50V", "EPD_VSH1", "GND", C0603),
               ("C", "C33", "1uF 50V", "EPD_VSH2", "GND", C0603),
               ("C", "C34", "1uF 50V", "EPD_VSL", "GND", C0603),
               ("C", "C35", "1uF 50V", "EPD_VCOM", "GND", C0603)])

os.makedirs(OUT, exist_ok=True)
sch.save(os.path.join(OUT, "eink-pod.kicad_sch"))
sch.save_design(os.path.join(OUT, "design.json"))
print("components:", len(sch.components), "nets:", len(sch.nets))
for n, pins in sorted(sch.nets.items()):
    if len(pins) < 2:
        print("WARNING single-pin net", n, pins)
