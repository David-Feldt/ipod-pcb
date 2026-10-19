"""Build the E-ink Pod PCB from design.json (run with KiCad's bundled python).

Creates the board, places every footprint, assigns nets, draws the outline,
planes and silkscreen, and saves kicad/eink-pod.kicad_pcb (unrouted).
"""
import json
import os

import pcbnew as P

HERE = os.path.dirname(os.path.abspath(__file__))
KDIR = os.path.normpath(os.path.join(HERE, "..", "kicad"))
FPDIR = os.path.expanduser("~/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints")
PCB = os.path.join(KDIR, "eink-pod.kicad_pcb")

W, H = 42.0, 98.0          # board size (mm)
OX, OY = 100.0, 100.0      # board origin on the page
R_CORNER = 3.0

# GDEY0213B74 on the front: 29.2 x 59.2 mm glass, FPC tail edge flush with the top edge
PANEL_W, PANEL_H, PANEL_CX = 29.2, 59.2, W / 2
# active area 23.70 x 48.55 mm, centred across, 7.95 mm below the tail edge (datasheet drawing)
PANEL_AA = ((PANEL_CX - 11.85, 7.95), (PANEL_CX + 11.85, 56.50))


def V(x, y):
    return P.VECTOR2I_MM(OX + x, OY + y)


# ref: (x, y, rot, side)   side "F" = front (display side), "B" = back
PLACE = {
    # front: buttons in an iPod cross + charge LED
    "SW1": (21, 65, 0, "F"), "SW5": (21, 75, 0, "F"), "SW2": (21, 85, 0, "F"),
    "SW3": (11, 75, 0, "F"), "SW4": (31, 75, 0, "F"),
    "D5": (31, 95.5, 0, "F"), "R4": (31, 93, 0, "F"),

    # back, top band: e-paper connector + boost
    "J4": (21.2, 9.4, 0, "B"), "L1": (6.2, 12, 0, "B"),
    "Q2": (10.65, 12, 0, "B"), "D2": (14.2, 14.5, 90, "B"), "D3": (6.2, 19, 0, "B"), "D4": (11.7, 19, 0, "B"),
    "C26": (9.7, 16, 0, "B"),
    "R21": (6.2, 8.5, 0, "B"), "R22": (10.2, 9.2, 0, "B"), "C27": (2.8, 17, 270, "B"),
    "C28": (16, 18.5, 0, "B"), "C29": (18.1, 18.5, 0, "B"), "C30": (17.2, 15.5, 90, "B"),
    "C31": (15.2, 21.5, 270, "B"), "C32": (25.6, 18.5, 90, "B"), "C33": (27.6, 18.5, 90, "B"),
    "C34": (29.6, 18.5, 90, "B"), "C35": (31.6, 18.5, 90, "B"),

    # back, middle: MCU (antenna at right edge, full keepout)
    "U1": (32.2, 30, 90, "B"),
    "C9": (35.8, 41, 0, "B"), "C6": (32.8, 41, 0, "B"), "C7": (33.9, 39.1, 270, "B"),
    "R10": (19.3, 35.3, 90, "B"), "C8": (16.8, 36.5, 0, "B"),
    "SW6": (26, 43, 0, "B"), "SW7": (33, 45.5, 0, "B"),
    "R6": (5.5, 39.5, 90, "B"), "R7": (7.5, 39.5, 90, "B"), "C5": (9.5, 39.5, 90, "B"),
    "R8": (13.5, 39.5, 90, "B"), "R9": (5.5, 35.5, 90, "B"),
    "Q3": (12.5, 35.5, 0, "B"), "Q4": (5.5, 31.5, 0, "B"), "Q5": (9.5, 31.5, 0, "B"),
    "R11": (9.5, 35.5, 90, "B"),
    "R12": (20, 28.5, 0, "B"), "R13": (20, 30.5, 0, "B"),
    "SW8": (40, 78, 270, "B"),

    # back, right edge below the antenna keepout: volume thumbwheel, knob overhangs the edge
    "RV1": (34.1, 61.5, 90, "B"), "C36": (25, 63.5, 90, "B"),

    # M2.5 mounting holes
    "H1": (3.5, 3.5, 0, "B"), "H2": (W - 3.5, 3.5, 0, "B"),
    "H3": (3.5, H - 3.5, 0, "B"), "H4": (W - 3.5, H - 3.5, 0, "B"),

    # back, bottom: power + USB
    "J2": (6.8, 84, 0, "B"),
    "U4": (14, 77, 0, "B"), "U5": (28, 78, 180, "B"),
    "L2": (33, 78, 90, "B"),
    "C1": (14.75, 81, 270, "B"), "C2": (10.5, 77, 270, "B"),
    "C39": (17.5, 77, 90, "B"),
    "R3": (12.5, 81, 90, "B"), "R14": (12.5, 83, 90, "B"),
    "R15": (19.5, 77, 90, "B"), "R16": (10, 73, 90, "B"),
    "R17": (14, 73, 90, "B"), "R18": (12, 73, 90, "B"),
    "R5": (19.5, 81, 90, "B"), "C3": (29.5, 81.5, 270, "B"),
    "C4": (29.5, 74.5, 90, "B"), "C37": (26.5, 74.5, 90, "B"),
    "R20": (22.5, 80.5, 90, "B"), "C38": (25, 78, 90, "B"), "R19": (25, 60, 90, "B"),
    "J1": (17, 94.3, 180, "B"), "U6": (17, 86, 270, "B"), "R1": (13, 86.3, 90, "B"), "R2": (21, 86.3, 90, "B"),
    "TP1": (7, 46, 0, "B"), "TP2": (9.5, 46, 0, "B"), "TP3": (12, 46, 0, "B"),
    "TP4": (14.5, 46, 0, "B"), "TP5": (17, 46, 0, "B"),
}

CLASSES = {
    # name: (track, clearance, via_d, via_drill, nets)
    "Power": (0.4, 0.2, 0.6, 0.3, ["VBUS", "VSYS", "VBAT"]),
    "Supply": (0.25, 0.15, 0.5, 0.3, ["GND", "+3V3"]),
    "HV": (0.25, 0.2, 0.5, 0.3, ["EPD_SW", "EPD_PREVGH", "EPD_PREVGL", "EPD_X"]),
}


# footprints whose stock 3D model isn't shipped -> our simple VRML from gen/models.py
MODEL_OVERRIDE = {
    "ESP32-S3-MINI-1": "ESP32-S3-MINI-1",
    "USB_C_Receptacle_HRO_TYPE-C-31-M-12": "USB_C_HRO_TYPE-C-31-M-12",
    "JST_PH_S2B-PH-SM4-TB_1x02-1MP_P2.00mm_Horizontal": "JST_PH_S2B-PH-SM4-TB",
    "SW_SPDT_Shouhan_MSK12C02": "SW_SPDT_MSK12C02",
    "FPC-05FB-24PH20": "FPC-05FB-24PH20",
}


def set_model(fp, name):
    m = P.FP_3DMODEL()
    m.m_Filename = "${KIPRJMOD}/3d/%s.wrl" % name
    m.m_Scale = P.VECTOR3D(1 / 2.54, 1 / 2.54, 1 / 2.54)
    fp.Models().clear()
    fp.Models().push_back(m)


def load_fp(fpid):
    lib, name = fpid.split(":")
    path = os.path.join(KDIR, lib + ".pretty") if lib == "eink-pod" else os.path.join(FPDIR, lib + ".pretty")
    fp = P.FootprintLoad(path, name)
    if fp is None:
        raise RuntimeError("footprint not found: " + fpid)
    fp.SetFPID(P.LIB_ID(lib, name))
    if name in MODEL_OVERRIDE:
        set_model(fp, MODEL_OVERRIDE[name])
    return fp


def seg(board, layer, x1, y1, x2, y2, w=0.1):
    s = P.PCB_SHAPE(board)
    s.SetShape(P.SHAPE_T_SEGMENT)
    s.SetStart(V(x1, y1)); s.SetEnd(V(x2, y2))
    s.SetLayer(layer); s.SetWidth(P.FromMM(w))
    board.Add(s)


def arc(board, layer, cx, cy, sx, sy, ang, w=0.1):
    s = P.PCB_SHAPE(board)
    s.SetShape(P.SHAPE_T_ARC)
    s.SetCenter(V(cx, cy)); s.SetStart(V(sx, sy))
    s.SetArcAngleAndEnd(P.EDA_ANGLE(ang, P.DEGREES_T), True)
    s.SetLayer(layer); s.SetWidth(P.FromMM(w))
    board.Add(s)


def text(board, layer, s, x, y, size=1.0, mirror=False, bold=False):
    t = P.PCB_TEXT(board)
    t.SetText(s); t.SetPosition(V(x, y)); t.SetLayer(layer)
    t.SetTextSize(P.VECTOR2I_MM(size, size)); t.SetTextThickness(P.FromMM(size * (0.2 if bold else 0.15)))
    t.SetMirrored(mirror)
    board.Add(t)


def outline_poly(inset=0.0):
    pts = []
    r = R_CORNER
    import math
    corners = [(W - r, r, -90), (W - r, H - r, 0), (r, H - r, 90), (r, r, 180)]
    for cx, cy, a0 in corners:
        for k in range(7):
            a = math.radians(a0 + k * 15)
            pts.append((cx + (r - inset) * math.cos(a), cy + (r - inset) * math.sin(a)))
    return pts


def zone(board, net, layer, inset=0.3, priority=0):
    z = P.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(net)
    z.SetAssignedPriority(priority)
    z.SetLocalClearance(P.FromMM(0.25))
    z.SetMinThickness(P.FromMM(0.2))
    z.SetPadConnection(P.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(P.FromMM(0.25))
    z.SetThermalReliefSpokeWidth(P.FromMM(0.3))
    ol = z.Outline()
    ol.NewOutline()
    for x, y in outline_poly(inset):
        ol.Append(P.FromMM(OX + x), P.FromMM(OY + y))
    board.Add(z)
    return z


def netlist_pads():
    """Pad -> net map from KiCad's exported netlist (the source of truth)."""
    import sys
    sys.path.insert(0, HERE)
    from sexpr import parse, find, findall
    root = parse(open(os.path.join(KDIR, "eink-pod.net")).read())[0]
    pads = {}
    for n in findall(find(root, "nets"), "net"):
        name = find(n, "name")[1]
        for nd in findall(n, "node"):
            pads[(find(nd, "ref")[1], find(nd, "pin")[1])] = name
    return pads


def write_stackup(path):
    """Record the nominal JLC04161H-7628 construction used for USB geometry."""
    stack = '''
        (stackup
            (layer "F.SilkS" (type "Top Silk Screen"))
            (layer "F.Paste" (type "Top Solder Paste"))
            (layer "F.Mask" (type "Top Solder Mask") (thickness 0.01) (epsilon_r 3.3) (loss_tangent 0))
            (layer "F.Cu" (type "copper") (thickness 0.035))
            (layer "dielectric 1" (type "prepreg") (thickness 0.2104) (material "FR4") (epsilon_r 4.6) (loss_tangent 0.02))
            (layer "In1.Cu" (type "copper") (thickness 0.0152))
            (layer "dielectric 2" (type "core") (thickness 1.065) (material "FR4") (epsilon_r 4.6) (loss_tangent 0.02))
            (layer "In2.Cu" (type "copper") (thickness 0.0152))
            (layer "dielectric 3" (type "prepreg") (thickness 0.2104) (material "FR4") (epsilon_r 4.6) (loss_tangent 0.02))
            (layer "B.Cu" (type "copper") (thickness 0.035))
            (layer "B.Mask" (type "Bottom Solder Mask") (thickness 0.01) (epsilon_r 3.3) (loss_tangent 0))
            (layer "B.Paste" (type "Bottom Solder Paste"))
            (layer "B.SilkS" (type "Bottom Silk Screen"))
            (copper_finish "ENIG")
            (dielectric_constraints no)
        )'''
    # NewBoard does not emit a stackup. Routing subsequently preserves it.
    content = open(path).read()
    assert "(stackup" not in content
    content = content.replace("(setup", "(setup" + stack, 1)
    with open(path, "w") as f:
        f.write(content)


def finish_silkscreen(board):
    """Keep overhanging component outlines on fabrication drawings only."""
    for fp in board.GetFootprints():
        if fp.GetReference() in ('U1', 'J1'):
            for item in fp.GraphicalItems():
                if item.GetLayer() != P.B_SilkS:
                    continue
                box = item.GetBoundingBox()
                if (P.ToMM(box.GetRight()) > OX + W - 0.2 or
                        P.ToMM(box.GetBottom()) > OY + H - 0.2):
                    item.SetLayer(P.B_Fab)
        if fp.GetReference() == 'U4':
            fp.Reference().SetPosition(V(17, 73))


def main():
    design = json.load(open(os.path.join(KDIR, "design.json")))
    netmap = netlist_pads()
    design["nets"] = sorted(set(netmap.values()))
    for c in design["components"]:
        c["pads"] = {pin: net for (ref, pin), net in netmap.items() if ref == c["ref"]}
    board = P.NewBoard(PCB)
    board.SetCopperLayerCount(4)
    board.SetLayerType(P.In1_Cu, P.LT_POWER)
    board.SetLayerType(P.In2_Cu, P.LT_POWER)
    board.SetLayerName(P.In1_Cu, "In1.GND")
    board.SetLayerName(P.In2_Cu, "In2.3V3")
    ds = board.GetDesignSettings()
    ds.m_MinClearance = P.FromMM(0.15)
    ds.m_TrackMinWidth = P.FromMM(0.12)
    ds.m_ViasMinSize = P.FromMM(0.45)
    ds.m_MinThroughDrill = P.FromMM(0.25)
    ds.m_CopperEdgeClearance = P.FromMM(0.3)
    ds.m_HoleToHoleMin = P.FromMM(0.25)

    ns = ds.m_NetSettings
    dflt = ns.GetDefaultNetclass()
    dflt.SetTrackWidth(P.FromMM(0.2)); dflt.SetClearance(P.FromMM(0.15))
    dflt.SetViaDiameter(P.FromMM(0.5)); dflt.SetViaDrill(P.FromMM(0.3))
    for name, (tw, cl, vd, vdr, nets) in CLASSES.items():
        nc = P.NETCLASS(name)
        nc.SetTrackWidth(P.FromMM(tw)); nc.SetClearance(P.FromMM(cl))
        nc.SetViaDiameter(P.FromMM(vd)); nc.SetViaDrill(P.FromMM(vdr))
        ns.SetNetclass(name, nc)
        for n in nets:
            ns.SetNetclassPatternAssignment(n, name)

    nets = {}
    for name in sorted(design["nets"]):
        ni = P.NETINFO_ITEM(board, name)
        board.Add(ni)
        nets[name] = ni

    missing = []
    for c in design["components"]:
        ref = c["ref"]
        if ref not in PLACE:
            missing.append(ref)
            continue
        fp = load_fp(c["footprint"])
        fp.SetReference(ref)
        fp.SetValue(c["value"])
        fp.SetPath(P.KIID_PATH("/" + c["uuid"]))
        fp.SetSheetname("Root")
        fp.SetSheetfile("eink-pod.kicad_sch")
        if c.get("lcsc"):
            fp.SetField("LCSC", c["lcsc"])
        if c.get("mpn"):
            fp.SetField("MPN", c["mpn"])
        for f in fp.GetFields():
            if f.GetName() not in ("Reference",):
                f.SetVisible(False)
        x, y, rot, side = PLACE[ref]
        board.Add(fp)
        if side == "B":
            fp.Flip(fp.GetPosition(), P.FLIP_DIRECTION_LEFT_RIGHT)
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(V(x, y))
        for pad in fp.Pads():
            n = c["pads"].get(pad.GetNumber())
            if n:
                pad.SetNet(nets[n])
            if pad.GetNumber() in ("SH", "MP"):
                pad.SetLocalZoneConnection(P.ZONE_CONNECTION_FULL)
        ref_t = fp.Reference()
        ref_t.SetTextSize(P.VECTOR2I_MM(0.8, 0.8))
        ref_t.SetTextThickness(P.FromMM(0.12))
        if ref.rstrip("0123456789") not in ("U", "J") and ref != "SW8":
            ref_t.SetLayer(P.B_Fab if side == "B" else P.F_Fab)
    if missing:
        raise SystemExit("no placement for: " + ", ".join(missing))

    # board-only placeholder carrying the e-paper panel's 3D model (no pads, not in BOM/CPL)
    ds = load_fp("eink-pod:EPD_GDEY0213B74_3D")
    ds.SetReference("DS1")
    ds.SetValue("GDEY0213B74")
    board.Add(ds)
    ds.SetPosition(V(PANEL_CX, PANEL_H / 2))

    # board outline
    E = P.Edge_Cuts
    r = R_CORNER
    seg(board, E, r, 0, W - r, 0); seg(board, E, W, r, W, H - r)
    seg(board, E, W - r, H, r, H); seg(board, E, 0, H - r, 0, r)
    arc(board, E, W - r, r, W - r, 0, 90); arc(board, E, W - r, H - r, W, H - r, 90)
    arc(board, E, r, H - r, r, H, 90); arc(board, E, r, r, 0, r, 90)

    # planes: In1 = GND, In2 = +3V3; outer GND pours added after routing
    zone(board, nets["GND"], P.In1_Cu)
    zone(board, nets["+3V3"], P.In2_Cu)

    # display outline (front): GDEY0213B74 29.2 x 59.2, tail edge at top
    px0, px1, py1 = PANEL_CX - PANEL_W / 2, PANEL_CX + PANEL_W / 2, PANEL_H
    for layer in (P.Dwgs_User,):
        seg(board, layer, px0, 0, px1, 0); seg(board, layer, px1, 0, px1, py1)
        seg(board, layer, px1, py1, px0, py1); seg(board, layer, px0, py1, px0, 0)
    (ax0, ay0), (ax1, ay1) = PANEL_AA
    seg(board, P.Dwgs_User, ax0, ay0, ax1, ay0); seg(board, P.Dwgs_User, ax1, ay0, ax1, ay1)
    seg(board, P.Dwgs_User, ax1, ay1, ax0, ay1); seg(board, P.Dwgs_User, ax0, ay1, ax0, ay0)
    text(board, P.Dwgs_User, "E-PAPER 2.13in (tail wraps over top edge)", PANEL_CX, 30, 1.0)
    # silkscreen corner marks for panel alignment
    for cx, cy, dx, dy in [(px0, 0.6, 1, 1), (px1, 0.6, -1, 1), (px0, py1, 1, -1), (px1, py1, -1, -1)]:
        seg(board, P.F_SilkS, cx, cy, cx + 2 * dx, cy, 0.15)
        seg(board, P.F_SilkS, cx, cy, cx, cy + 2 * dy, 0.15)

    # front silkscreen
    text(board, P.F_SilkS, "MENU", 21, 61.6, 0.8)
    text(board, P.F_SilkS, "PLAY", 24.5, 86.7, 0.8)
    text(board, P.F_SilkS, "<<", 11, 78.2, 0.8)
    text(board, P.F_SilkS, ">>", 31, 78.2, 0.8)
    text(board, P.F_SilkS, "CHG", 27.5, 95.5, 0.8)
    # back silkscreen (mirrored)
    text(board, P.B_SilkS, "E-INK REMOTE rev D", 21, 50.5, 1.0, mirror=True, bold=True)
    text(board, P.B_SilkS, "BOOT", 26, 45.3, 0.8, mirror=True)
    text(board, P.B_SilkS, "RST", 33, 47.8, 0.8, mirror=True)
    text(board, P.B_SilkS, "+", 5.8, 90.4, 1.0, mirror=True, bold=True)
    text(board, P.B_SilkS, "-", 7.8, 90.4, 1.0, mirror=True, bold=True)
    text(board, P.B_SilkS, "BAT", 6.8, 92.0, 0.8, mirror=True)
    text(board, P.B_SilkS, "TX RX G 3V3 BAT", 12, 48, 0.8, mirror=True)
    text(board, P.B_SilkS, "ON/OFF", 37.5, 72.8, 0.8, mirror=True)
    text(board, P.B_SilkS, "VOL", 31, 53.5, 0.8, mirror=True)

    finish_silkscreen(board)
    P.SaveBoard(PCB, board)
    write_stackup(PCB)
    print("saved", PCB, "footprints:", len(board.GetFootprints()))


if __name__ == "__main__":
    main()
