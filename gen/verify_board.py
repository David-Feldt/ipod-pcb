"""Electrical/layout regression checks beyond KiCad ERC/DRC.

Run with KiCad's Python after routing. Fail if the router changes the fixed
USB/boost routes, if sense isolation changes, or local decoupling moves away.
"""
import math
import os
import pcbnew as P
from debug_features import POINTS, PROGRAM, DISPLAY, PROBE_ANCHORS

from critical_routes import route_critical
from verify_spacing import verify_spacing

PCB = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "kicad", "eink-pod.kicad_pcb"))


def point(p):
    return (P.ToMM(p.x), P.ToMM(p.y))


def verify_uart_labels(board):
    fps = {f.GetReference(): f for f in board.GetFootprints()}
    labels = [t for t in board.GetDrawings() if isinstance(t, P.PCB_TEXT)]
    assert not any(t.GetText() == "TX RX G 3V3" for t in labels), "Reversed combined UART label"
    for ref, net, label in [("TP1", "TXD0", "TX"), ("TP2", "RXD0", "RX"),
                            ("TP3", "GND", "G"), ("TP4", "+3V3", "3V3")]:
        p = next(iter(fps[ref].Pads()))
        assert p.GetNetname() == net, (ref, "UART pad net")
        x, y = point(p.GetPosition())
        matches = [t for t in labels if t.GetLayer() == P.B_SilkS
                   and math.dist(point(t.GetPosition()), (x, y - 1.3)) < 0.001]
        assert len(matches) == 1 and matches[0].GetText() == label and matches[0].IsMirrored(), \
            (ref, "missing, incorrect or unmirrored UART label")
    print("PASS: TP1–TP4 have individual mirrored TX/RX/G/3V3 labels aligned to their pads")


def verify_usb_reference(board):
    """Sample filled In1 ground beneath the USB trace center and both edges.

    Resolution is 0.05 mm along each segment. Only the immediate 0.60 mm
    radius around each signal's own transition vias is exempt (antipads).
    This checks copper coverage, not impedance or high-frequency return loss.
    """
    tracks = list(board.GetTracks())
    assert not any(not isinstance(t, P.PCB_VIA) and t.GetLayer() == P.In1_Cu
                   for t in tracks), "Signal routing in ground plane"
    fills = [z.GetFilledPolysList(P.In1_Cu) for z in board.Zones()
             if not z.GetIsRuleArea() and z.GetLayer() == P.In1_Cu and z.GetNetname() == "GND"]
    assert fills and any(f.OutlineCount() for f in fills), "Missing filled ground plane"
    checked = 0
    for net in ("USB_DP", "USB_DN"):
        vias = [point(t.GetPosition()) for t in tracks
                if isinstance(t, P.PCB_VIA) and t.GetNetname() == net]
        segments = [t for t in tracks if not isinstance(t, P.PCB_VIA)
                    and t.GetLayer() == P.F_Cu and t.GetNetname() == net]
        assert segments, (net, "missing front USB trunk")
        for t in segments:
            a, b = point(t.GetStart()), point(t.GetEnd())
            length = math.dist(a, b)
            if length == 0:
                continue
            nx, ny = -(b[1] - a[1]) / length, (b[0] - a[0]) / length
            steps = max(1, math.ceil(length / 0.05))
            for i in range(steps + 1):
                for offset in (-P.ToMM(t.GetWidth()) / 2, 0, P.ToMM(t.GetWidth()) / 2):
                    q = (a[0] + (b[0] - a[0]) * i / steps + nx * offset,
                         a[1] + (b[1] - a[1]) * i / steps + ny * offset)
                    if any(math.dist(q, v) <= 0.60 for v in vias):
                        continue
                    assert any(f.Contains(P.VECTOR2I_MM(*q)) for f in fills), \
                        (net, "ground-plane void beneath USB", q)
                    checked += 1
    print("PASS: filled In1.GND covers USB centers/edges at %d samples, excluding local via antipads" % checked)


def verify(board=None):
    if board is None:
        board = P.LoadBoard(PCB)
    fps = {f.GetReference(): f for f in board.GetFootprints()}
    verify_spacing(board)
    verify_uart_labels(board)

    def pad(ref, number):
        return next(p for p in fps[ref].Pads() if p.GetNumber() == str(number))

    # Body-diode orientation and domain boundaries are intentional.
    expected_nets = {
        "C40": {1: "VBUS", 2: "GND"},
        "Q3": {1: "VBUS", 2: "GND", 3: "USB_PRESENT_N"},
        "Q4": {1: "BAT_SENSE_GATE", 2: "VBAT", 3: "VBAT_MEAS"},
        "Q5": {1: "+3V3", 2: "GND", 3: "BAT_SENSE_GATE"},
        "R6": {1: "VBAT_MEAS", 2: "VBAT_SENSE"},
        "R7": {1: "VBAT_SENSE", 2: "GND"},
        "R8": {1: "+3V3", 2: "USB_PRESENT_N"},
        "R9": {1: "VBAT", 2: "BAT_SENSE_GATE"},
        "R11": {1: "+3V3", 2: "GND"},
        "U1": {3: "+3V3", 5: "VBAT_SENSE", 6: "USB_PRESENT_N",
               23: "USB_MCU_N", 24: "USB_MCU_P"},
        "R12": {1: "USB_DP", 2: "USB_MCU_P"},
        "R13": {1: "USB_DN", 2: "USB_MCU_N"},
    }
    expected_nets.update({
        "U4": {1: "BAT_TS", 2: "VBAT", 3: "VBAT", 4: "GND", 5: "CHG_EN2", 6: "CHG_EN1",
               8: "GND", 9: "CHG_STAT", 10: "VSYS", 11: "VSYS", 12: "CHG_ILIM", 13: "VBUS",
               16: "CHG_PROG", 17: "GND"},
        "U5": {1: "REG_3V3", 2: "PWR_L2", 3: "GND", 4: "PWR_L1", 5: "VSYS", 6: "PWR_EN",
               7: "GND", 8: "VSYS", 9: "GND", 10: "REG_3V3", 11: "GND"},
        "L2": {1: "PWR_L1", 2: "PWR_L2"},
        "R3": {1: "CHG_PROG", 2: "CHG_PROG_MID"}, "R14": {1: "CHG_PROG_MID", 2: "GND"},
        "R15": {1: "CHG_ILIM", 2: "GND"}, "R16": {1: "BAT_TS", 2: "GND"},
        "R17": {1: "CHG_EN1", 2: "GND"}, "R18": {1: "CHG_EN2", 2: "GND"},
        "R4": {1: "VSYS", 2: "LED_A"}, "R19": {1: "VOL_EN", 2: "VOL_SUPPLY"}, "R20": {1: "PWR_EN", 2: "GND"},
        "RV1": {1: "GND", 2: "VOL_SENSE", 3: "VOL_SUPPLY"},
        "J2": {1: "VBAT_RAW", 2: "GND"}, "SW8": {1: "VSYS", 2: "PWR_EN", 3: "GND"},
        "J4": {2: "EPD_GDR", 3: "EPD_RESE", 5: "EPD_VSH2", 8: "GND", 9: "EPD_BUSY",
               10: "EPD_RST", 11: "EPD_DC", 12: "EPD_CS", 13: "EPD_SCK", 14: "EPD_MOSI",
               15: "+3V3", 16: "+3V3", 17: "GND", 18: "EPD_VDD", 20: "EPD_VSH1",
               21: "EPD_PREVGH", 22: "EPD_VSL", 23: "EPD_PREVGL", 24: "EPD_VCOM"},
    })
    expected_nets["U1"].update({8: "VOL_SENSE", 9: "VOL_EN", 10: "CHG_EN1", 11: "CHG_EN2"})
    expected_nets.update({
        "R23": {1:"VBAT_RAW",2:"VBAT"}, "R24": {1:"REG_3V3",2:"+3V3"},
        "R25": {1:"+3V3",2:"EPD_BOOST_IN"}, "L1": {1:"EPD_BOOST_IN",2:"EPD_SW"},
        "C27": {1:"EPD_BOOST_IN",2:"GND"}, "C4": {1:"REG_3V3",2:"GND"},
        "C37": {1:"REG_3V3",2:"GND"},
        "J3": dict(enumerate(PROGRAM,1)), "J5": dict(enumerate(DISPLAY,1)),
    })
    expected_nets["U1"].update({12:"SPARE_GPIO8",18:"SPARE_GPIO14",19:"SPARE_GPIO15"})
    expected_nets.update({ref:{1:data[0]} for ref,data in POINTS.items()})
    for ref in ("R23","R24","R25"):
        assert fps[ref].GetValue()=="0R"
    for ref in ("J3","J5"):
        assert fps[ref].GetAttributes() & P.FP_DNP, ref
        assert fps[ref].GetAttributes() & P.FP_EXCLUDE_FROM_POS_FILES, ref
    # There must be no parallel path bypassing a removable power link.
    for net,refs in {
        "VBAT_RAW":{"J2","R23","TP6"},
        "REG_3V3":{"U5","C4","C37","R24","TP10"},
        "EPD_BOOST_IN":{"R25","L1","C27"},
    }.items():
        actual={r for r,f in fps.items() if any(p.GetNetname()==net for p in f.Pads())}
        assert actual==refs,(net,actual,refs)
    for ref,(anchor,pin) in PROBE_ANCHORS.items():
        assert math.dist(point(pad(ref,1).GetPosition()),point(pad(anchor,pin).GetPosition()))<2.5,ref
    print("PASS: power isolation boundaries, manual UART/header pinouts, DNP headers, local boost probes and spare GPIOs")
    for ref, val in {"L1": "68uH", "U4": "BQ24074RGTR", "U5": "TPS63031DSKR", "R3": "2k", "R14": "2k",
                     "R15": "5.1k", "R16": "10k", "R17": "100k", "R18": "100k", "R19": "2k", "L2": "2.2uH"}.items():
        assert fps[ref].GetValue() == val, (ref, val)
    # Panel-reference boost pair: keep values, assembly selection and sense wiring together.
    for ref, value, footprint, lcsc, mpn in (
        ("L1", "68uH", "L_Sunlord_SWPA4030S", "C83473", "SWPA4030S680MT"),
        ("R22", "3", "R_0402_1005Metric", "C423196", "0402WGF300KTCE"),
    ):
        assert fps[ref].GetValue() == value, ref
        assert fps[ref].GetFPID().GetLibItemName() == footprint, ref
        assert fps[ref].GetFieldText("LCSC") == lcsc, ref
        assert fps[ref].GetFieldText("MPN") == mpn, ref
    for num, net in ((1, "EPD_RESE"), (2, "GND")):
        assert pad("R22", num).GetNetname() == net, ("R22", num, net)
    print("PASS: L1 68 uH / R22 3 ohm reference pair, exact assembly parts and sense wiring")
    # Panel-reference gate pull-down, retaining the existing 0402 land pattern.
    assert fps["R21"].GetValue() == "10k"
    assert fps["R21"].GetFPID().GetLibItemName() == "R_0402_1005Metric"
    assert fps["R21"].GetFieldText("LCSC") == "C25744"
    for num, net in ((1, "EPD_GDR"), (2, "GND")):
        assert pad("R21", num).GetNetname() == net, ("R21", num, net)
    print("PASS: R21 10 kohm gate pull-down, exact assembly part and pin mapping")
    # MBR0530 package pin 1 is cathode; retain each boost diode's polarity.
    for ref, cathode, anode in (("D2", "EPD_PREVGH", "EPD_SW"),
                                ("D3", "GND", "EPD_X"),
                                ("D4", "EPD_X", "EPD_PREVGL")):
        assert fps[ref].GetValue() == "MBR0530T1G", ref
        assert fps[ref].GetFPID().GetLibItemName() == "D_SOD-123", ref
        assert fps[ref].GetFieldText("LCSC") == "C82046", ref
        assert fps[ref].GetFieldText("MPN") == "MBR0530T1G", ref
        assert pad(ref, 1).GetNetname() == cathode, ref
        assert pad(ref, 2).GetNetname() == anode, ref
    print("PASS: D2-D4 MBR0530T1G exact assembly parts, SOD-123 and cathode/anode mapping")
    # Q2 must use the low-charge reference MOSFET and its smaller land pattern.
    assert fps["Q2"].GetValue() == "Si1308EDL-T1-GE3"
    assert fps["Q2"].GetFPID().GetLibItemName() == "SOT-323_SC-70"
    assert fps["Q2"].GetFieldText("LCSC") == "C469327"
    for num, net in {1: "EPD_GDR", 2: "EPD_RESE", 3: "EPD_SW"}.items():
        assert pad("Q2", num).GetNetname() == net, ("Q2", num, net)
    print("PASS: Q2 reference Si1308EDL, SOT-323 footprint, assembly ID and G/S/D pin mapping")
    # VCOM capacitance must retain margin at high bias; do not substitute C15849.
    assert fps["C35"].GetValue() == "1uF 50V X7R"
    assert fps["C35"].GetFPID().GetLibItemName() == "C_0805_2012Metric"
    assert fps["C35"].GetFieldText("LCSC") == "C28323"
    assert fps["C35"].GetFieldText("MPN") == "CL21B105KBFNNNE"
    for num, net in {1: "EPD_VCOM", 2: "GND"}.items():
        assert pad("C35", num).GetNetname() == net, ("C35", num, net)
    print("PASS: C35 X7R 0805 VCOM capacitor, exact assembly part and pin mapping")
    for ref, net in (("C30", "EPD_PREVGH"), ("C31", "EPD_PREVGL"),
                     ("C32", "EPD_VSH1"), ("C33", "EPD_VSH2"), ("C34", "EPD_VSL")):
        assert fps[ref].GetValue() == "1uF 50V X7R", ref
        assert fps[ref].GetFPID().GetLibItemName() == "C_0805_2012Metric", ref
        assert fps[ref].GetFieldText("LCSC") == "C28323", ref
        assert fps[ref].GetFieldText("MPN") == "CL21B105KBFNNNE", ref
        assert pad(ref, "1").GetNetname() == net, ref
        assert pad(ref, "2").GetNetname() == "GND", ref
    print("PASS: C30-C34 X7R 0805 rail capacitors, exact assembly parts and pin mapping")

    # Preserve the approved 50 V X7R flying capacitor and switching-node mapping.
    assert fps["C26"].GetValue() == "4.7uF 50V X7R"
    assert fps["C26"].GetFPID().GetLibItemName() == "C_1206_3216Metric"
    assert fps["C26"].GetFieldText("LCSC") == "C51205"
    assert fps["C26"].GetFieldText("MPN") == "CL31B475KBHNNNE"
    for num, net in (("1", "EPD_SW"), ("2", "EPD_X")):
        assert pad("C26", num).GetNetname() == net, ("C26", num, net)
    print("PASS: C26 50 V X7R 1206 flying capacitor, exact assembly part and pin mapping")

    assert "Q1" not in fps and "D1" not in fps, "Obsolete power path present"
    for pin in (7, 14, 15):
        assert pad("U4", pin).GetNetname().startswith("unconnected-"), ("U4", pin)
    for pin in (1, 4, 6, 7, 19):
        assert pad("J4", pin).GetNetname().startswith("unconnected-"), ("J4", pin)
    for ref, pins in expected_nets.items():
        for num, net in pins.items():
            assert pad(ref, num).GetNetname() == net, (ref, num, net)
    for ref, value in {"R6": "1M", "R7": "1M", "R8": "100k", "R9": "1M",
                       "R11": "1M", "R12": "22", "R13": "22", "C5": "100nF", "C40": "100nF"}.items():
        assert fps[ref].GetValue() == value, (ref, value)
    assert math.dist(point(pad("U6", 5).GetPosition()), point(pad("C40", 1).GetPosition())) < 1.0
    assert fps["C40"].GetLayer() == P.F_Cu, "ESD bypass must stay opposite U6"
    print("PASS: 100 nF C40 bypass sits opposite U6; local supply/ground routes are protected")
    for ref, limit in [("C7", 2.0), ("C6", 5.0), ("C9", 5.0)]:
        distance = math.dist(point(pad("U1", 3).GetPosition()), point(pad(ref, 1).GetPosition()))
        assert distance <= limit, (ref, distance, limit)
        print("%s to MCU supply pad: %.2f mm" % (ref, distance))

    # Capacitors must remain close to the switching and charger supply pins.
    for ic, pin, cap, limit in [("U4", 13, "C1", 2.0), ("U4", 2, "C2", 3.0),
                               ("U4", 10, "C39", 3.0), ("U5", 5, "C3", 2.0),
                               ("U5", 1, "C4", 2.0), ("U5", 8, "C38", 2.0)]:
        d = math.dist(point(pad(ic, pin).GetPosition()), point(pad(cap, 1).GetPosition()))
        assert d <= limit, (ic, pin, cap, d)
    # Front-view coordinates after rotating the vendor panel drawing 90 degrees.
    assert abs(point(pad("J4", 1).GetPosition())[0] - 126.95) < 0.01
    assert abs(point(pad("J4", 24).GetPosition())[0] - 115.45) < 0.01
    assert point(pad("J2", 1).GetPosition())[0] < point(pad("J2", 2).GetPosition())[0]
    # Glass 29.2 x 59.2; screw/boss OD 4.5 mm max leaves 0.65 mm nominal clearance.
    for ref, x, y in [("H1",103.5,103.5),("H2",138.5,103.5),("H3",103.5,194.5),("H4",138.5,194.5)]:
        assert math.dist(point(fps[ref].GetPosition()), (x,y)) < 0.01
    assert abs(fps["RV1"].GetOrientationDegrees() - 90) < 0.01
    assert math.dist(point(fps["RV1"].GetPosition()), (134.1,161.5)) < 0.01
    # Alps drawing No.2: knob center is 1.8 below the lower edge of the MP lands.
    # MP center -4.9 + half-height .7 + 1.8 = -2.4; board-right edge is x=142.
    assert abs((134.1 + 2.4 + 7) - 142 - 1.5) < 0.001
    print("PASS: power-path topology, default USB100 straps, buck-boost, wheel range, panel pinout and geometry")

    # Snapshot native objects; checks do not mutate the board or reload it.
    actual_tracks, actual_vias = [], []
    for t in board.GetTracks():
        if isinstance(t, P.PCB_VIA):
            actual_vias.append(dict(net=t.GetNetname(), pos=point(t.GetPosition())))
        else:
            actual_tracks.append(dict(net=t.GetNetname(), layer=t.GetLayer(), width=t.GetWidth(),
                                      start=point(t.GetStart()), end=point(t.GetEnd()),
                                      length=P.ToMM(t.GetLength())))
    expected = []
    route_critical(board, collect=expected)

    def covers(segment):
        a, b = segment["start"], segment["end"]
        length = math.dist(a, b)
        ux, uy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
        intervals = []
        for t in actual_tracks:
            if (t["net"], t["layer"], t["width"]) != (segment["net"], segment["layer"], segment["width"]):
                continue
            projections = []
            for p in [t["start"], t["end"]]:
                dx, dy = p[0] - a[0], p[1] - a[1]
                if abs(dx * uy - dy * ux) > 0.003:  # DSN exchange rounds to um
                    break
                projections.append(dx * ux + dy * uy)
            if len(projections) == 2:
                intervals.append(sorted(projections))
        end = 0
        for lo, hi in sorted(intervals):
            if lo > end + 0.003:
                break
            end = max(end, hi)
        return end >= length - 0.003

    for t in expected:
        if "pos" in t:
            assert any(v["net"] == t["net"] and math.dist(v["pos"], t["pos"]) < 0.003
                       for v in actual_vias), "Fixed via lost: " + t["net"]
        else:
            assert covers(t), "Fixed route lost: %s %s -> %s" % (t["net"], t["start"], t["end"])
    # Screw heads/bosses must not touch signal copper on either outer surface.
    def seg_distance(p, a, b):
        dx, dy = b[0]-a[0], b[1]-a[1]
        den = dx*dx + dy*dy
        u = 0 if not den else max(0, min(1, ((p[0]-a[0])*dx+(p[1]-a[1])*dy)/den))
        return math.dist(p, (a[0]+u*dx, a[1]+u*dy))
    for ref in ("H1", "H2", "H3", "H4"):
        center = point(fps[ref].GetPosition())
        for t in actual_tracks:
            if t["net"] != "GND" and t["layer"] in (P.F_Cu, P.B_Cu):
                assert seg_distance(center, t["start"], t["end"]) >= 2.25 + P.ToMM(t["width"])/2, (ref, t["net"], "signal under screw head")
        for f in fps.values():
            for p in f.Pads():
                if p.GetNetname() and p.GetNetname() != "GND" and (p.IsOnLayer(P.F_Cu) or p.IsOnLayer(P.B_Cu)):
                    bb=p.GetBoundingBox()
                    radius=math.hypot(P.ToMM(bb.GetWidth()),P.ToMM(bb.GetHeight()))/2
                    assert math.dist(center,point(p.GetPosition()))>=2.25+radius,(ref,f.GetReference(),"pad under screw head")
        for v in actual_vias:
            if v["net"] != "GND":
                assert math.dist(center,v["pos"])>=2.55,(ref,v["net"],"via under screw head")
    print("PASS: no signal copper under 4.5 mm mounting heads")
    # Hand-soldered debug headers: a bridge must not reach battery/system power.
    power = {"VBUS", "VSYS", "VBAT", "VBAT_RAW", "REG_3V3", "EPD_BOOST_IN"}
    def rect_distance(p, r):
        return math.hypot(max(r[0]-p[0], 0, p[0]-r[2]), max(r[1]-p[1], 0, p[1]-r[3]))
    header_gaps = []
    for ref in ("J3", "J5"):
        for p in fps[ref].Pads():
            bb = p.GetBoundingBox()
            r = (P.ToMM(bb.GetLeft()), P.ToMM(bb.GetTop()), P.ToMM(bb.GetRight()), P.ToMM(bb.GetBottom()))
            for t in actual_tracks:
                if t["net"] in power:
                    a, b = t["start"], t["end"]
                    n = max(1, int(math.dist(a, b) / 0.02))
                    d = min(rect_distance((a[0]+(b[0]-a[0])*i/n, a[1]+(b[1]-a[1])*i/n), r)
                            for i in range(n+1)) - P.ToMM(t["width"])/2
                    assert d >= 0.5, (ref, p.GetNumber(), t["net"], "power track beside header", d)
                    header_gaps.append(d)
            for v in actual_vias:
                if v["net"] in power:
                    assert rect_distance(v["pos"], r) - 0.3 >= 0.5, (ref, p.GetNumber(), v["net"], "power via beside header")
    print("PASS: power nets clear J3/J5 header pads by %.2f mm" % min(header_gaps))
    # No extra branches on the USB data nets; two layer changes per wire.
    for net in ["USB_DP", "USB_DN", "USB_MCU_P", "USB_MCU_N"]:
        actual_len = sum(t["length"] for t in actual_tracks if t["net"] == net)
        expected_len = sum(math.dist(t["start"], t["end"]) for t in expected if "start" in t and t["net"] == net)
        assert abs(actual_len - expected_len) < 0.05, (net, "extra/missing route", actual_len, expected_len)
        nv = sum(v["net"] == net for v in actual_vias)
        assert nv == (2 if net in ("USB_DP", "USB_DN") else 0), (net, nv)
        print("%s: %.2f mm total copper, %d vias" % (net, actual_len, nv))
    verify_usb_reference(board)
    print("PASS: sense isolation, capacitor placement, fixed routes, USB branches/vias, ground reference")


if __name__ == "__main__":
    verify()
