"""Fixed USB, local decoupling, and display boost routes, before autorouting.

Coordinates are board-local millimetres. Locked tracks are exported to
Freerouting as protected wiring. DRC and verify_board.py check the result.
USB trunk: F.Cu over In1.GND, 0.25 mm width / 0.15 mm edge gap.
Nominal stackup target: JLC04161H-7628; confirm 90 ohm with the fabricator.
"""
import pcbnew as P
from debug_features import POINTS, PROBE_ANCHORS

USB_WIDTH = 0.25
USB_GAP = 0.15


def route_critical(board, collect=None):
    fps = {f.GetReference(): f for f in board.GetFootprints()}

    def pad(ref, number):
        return next(p for p in fps[ref].Pads() if p.GetNumber() == str(number))

    def xy(ref, number):
        pos = pad(ref, number).GetPosition()
        return (P.ToMM(pos.x) - 100, P.ToMM(pos.y) - 100)

    def wire(net, points, layer=P.B_Cu, width=0.3):
        for a, b in zip(points, points[1:]):
            if a == b:
                continue
            if collect is not None:
                collect.append(dict(net=net, layer=layer, width=P.FromMM(width),
                                    start=(a[0] + 100, a[1] + 100),
                                    end=(b[0] + 100, b[1] + 100)))
                continue
            t = P.PCB_TRACK(board)
            t.SetStart(P.VECTOR2I_MM(a[0] + 100, a[1] + 100))
            t.SetEnd(P.VECTOR2I_MM(b[0] + 100, b[1] + 100))
            t.SetWidth(P.FromMM(width))
            t.SetLayer(layer)
            t.SetNet(board.FindNet(net))
            t.SetLocked(True)
            board.Add(t)

    def via(net, point):
        if collect is not None:
            collect.append(dict(net=net, pos=(point[0] + 100, point[1] + 100)))
            return
        v = P.PCB_VIA(board)
        v.SetPosition(P.VECTOR2I_MM(point[0] + 100, point[1] + 100))
        v.SetWidth(P.FromMM(0.5))
        v.SetDrill(P.FromMM(0.3))
        v.SetNet(board.FindNet(net))
        v.SetLocked(True)
        board.Add(v)

    # Reversible USB-C contacts join locally, then flow through the ESD array.
    wire("USB_DP", [xy("J1", "A6"), (17.25, 91.6), (16.25, 91.6), xy("J1", "B6")], width=0.15)
    wire("USB_DP", [xy("J1", "B6"), (16.25, 89.4), (16.05, 89.2), xy("U6", 6)], width=0.15)
    wire("USB_DN", [xy("J1", "A7"), (16.75, 88.9), (17.75, 88.9), xy("J1", "B7")], width=0.15)
    wire("USB_DN", [(17.75, 88.9), (17.95, 88.7), xy("U6", 4)], width=0.15)
    # Explicit copper through the flow-through ESD pads, not long external stubs.
    wire("USB_DP", [xy("U6", 6), xy("U6", 1)], width=USB_WIDTH)
    wire("USB_DN", [xy("U6", 4), xy("U6", 3)], width=USB_WIDTH)
    # Escape the ESD rail between its two data channels, then reach the USB
    # supply on F.Cu. The data routes otherwise enclose this pad on B.Cu.
    wire("VBUS", [xy("U6", 5), (17, 88.1)], width=0.3)
    via("VBUS", (17, 88.1))
    via("VBUS", (19.45, 88.8))
    wire("VBUS", [(17, 88.1), (18.75, 88.1), (19.45, 88.8)], P.F_Cu, 0.4)
    wire("VBUS", [(19.45, 88.8), xy("J1", "A4")], width=0.3)
    wire("USB_DP", [xy("U6", 1), (16.05, 84.1), (16.45, 83.8)], width=USB_WIDTH)
    wire("USB_DN", [xy("U6", 3), (17.95, 84.1), (17.65, 83.8), (17.15, 83.8)], width=USB_WIDTH)
    for n, bottom, top in [("USB_DP", (16.45, 83.8), (18.8, 28.5)),
                           ("USB_DN", (17.15, 83.8), (18.8, 30.5))]:
        via(n, bottom)
        via(n, top)
    wire("USB_DP", [(16.45, 83.8), (16.45, 29.6), (17.55, 28.5), (18.8, 28.5)], P.F_Cu, USB_WIDTH)
    wire("USB_DN", [(17.15, 83.8), (17.15, 83.1), (16.85, 82.8), (16.85, 31.6), (17.95, 30.5), (18.8, 30.5)], P.F_Cu, USB_WIDTH)
    wire("USB_DP", [(18.8, 28.5), xy("R12", 1)], width=USB_WIDTH)
    wire("USB_DN", [(18.8, 30.5), xy("R13", 1)], width=USB_WIDTH)
    wire("USB_MCU_P", [xy("R12", 2), (20.75, 28.5), (21.4, 29.15), xy("U1", 24)], width=0.2)
    wire("USB_MCU_N", [xy("R13", 2), (20.8, 30.5), (21.3, 30), xy("U1", 23)], width=0.2)

    # Supply pin -> local ceramic -> bulk capacitors, with local ground vias.
    wire("+3V3", [xy("U1", 3), xy("C7", 1)], width=0.3)
    wire("+3V3", [xy("C7", 1), (32.5, 38.62), (32.5, 40.525), xy("C6", 1)], width=0.4)
    wire("+3V3", [xy("C7", 1), (35.025, 38.62), xy("C9", 1)], width=0.4)

    # Filled/capped thermal vias connect both power IC exposed pads to In1.GND.
    for x, y in [(13.6,76.6),(14.4,76.6),(13.6,77.4),(14.4,77.4),(28,77.6),(28,78.4)]:
        via("GND", (x,y))

    # Buck-boost: switch loops stay on the component layer; feedback avoids L2.
    wire("PWR_L1", [xy("U5", 4), (30.4, 78.5), (31.4, 79.5), xy("L2", 1)], width=0.3)
    wire("PWR_L2", [xy("U5", 2), (30.4, 77.5), (31.4, 76.5), xy("L2", 2)], width=0.3)
    wire("VSYS", [xy("U5", 5), (29.5, 79.3), xy("C3", 1)], width=0.3)
    wire("REG_3V3", [xy("U5", 1), (29.5, 76.7), xy("C4", 1)], width=0.3)
    wire("REG_3V3", [xy("U5", 10), (26.5, 76.7), xy("C37", 1), xy("C4", 1)], width=0.25)
    wire("VSYS", [xy("U5", 8), (25.8, 78), xy("C38", 1)], width=0.2)
    wire("VSYS", [xy("C3", 1), (25,80.55), xy("C38", 1)], width=0.4)
    # Direct copper into PGND and exposed pad; the thermal pad reaches In1.GND.
    wire("GND", [xy("U5", 3), xy("U5", 11), (27.5, 77.5), xy("U5", 9)], width=0.25)
    wire("GND", [xy("U5", 11), (27.5, 78.5), xy("U5", 7)], width=0.25)
    # Charger local input, battery, and system output capacitors.
    wire("VBUS", [xy("U4", 13), (14.75, 79.4), xy("C1", 1)], width=0.3)
    wire("VBAT", [xy("U4", 2), xy("U4", 3), (11.8, 76.75), xy("C2", 1)], width=0.3)
    wire("VSYS", [xy("U4", 10), xy("U4", 11), (16.3, 77.25), xy("C39", 1)], width=0.3)

    wire("VSYS", [xy("C39", 1), (18.35,78.8), (24.68,78.8), xy("C38", 1)], width=0.4)

    # Complete VSYS explicitly. Leaving branches of this protected power network
    # to Freerouting can trigger exponential cycle cleanup in version 2.4.1.
    wire("VSYS", [xy("C3", 1), (31,80.55), (31,81.5), (36,81.5), (36,75.75), xy("SW8", 1)], width=0.4)
    wire("VSYS", [(31,81.5), (31,83.5), (30.65,83.85), (30.65,89.5), (29,91.5)], width=0.4)
    via("VSYS", (29,91.5))
    wire("VSYS", [(29,91.5), xy("R4", 1)], P.F_Cu, 0.4)

    # Keep switching nodes and sense path short and on B.Cu.
    wire("EPD_SW", [xy("L1", 2), xy("Q2", 3), (13.35, 12), xy("D2", 2)], width=0.3)
    wire("EPD_SW", [xy("Q2", 3), (12.15, 12.5625), (12.15, 14.8), (8.75, 14.8), xy("C26", 1)], width=0.3)
    wire("EPD_RESE", [xy("Q2", 2), (9.7125, 9.2225), xy("R22", 1)], width=0.25)
    wire("EPD_BOOST_IN", [xy("L1", 1), (2.8, 13.9), xy("C27", 1)], width=0.4)
    wire("EPD_X", [xy("C26", 2), (10.65, 17.3), (8.95, 19), xy("D3", 2), xy("D4", 1)], width=0.3)
    wire("EPD_PREVGH", [xy("D2", 1), (15.3, 16.15), (15.425, 16.275), xy("C30", 1)], width=0.3)
    wire("EPD_PREVGL", [xy("D4", 2), (14.7, 19), (15.2, 19.5), xy("C31", 1)], width=0.3)

    wire("VSYS", [(36,81.5),xy("TP8",1)], width=0.4)

    # Links keep the charger/regulator capacitors on their own source side.
    wire("VBAT_RAW", [xy("J2",1),xy("R23",1)], width=0.4)
    wire("VBAT_RAW", [xy("R23",1),xy("TP6",1)], width=0.4)
    wire("VBAT", [xy("R23",2),xy("TP5",1)], width=0.4)
    wire("REG_3V3", [xy("C37",1),xy("R24",1)], width=0.4)
    wire("REG_3V3", [xy("R24",1),(24.5,75.45),(24.5,73),xy("TP10",1)], width=0.3)
    wire("EPD_BOOST_IN", [xy("R25",2),(2.8,12),xy("L1",1)], width=0.4)
    for ref,(component,pin) in PROBE_ANCHORS.items():
        wire(POINTS[ref][0],[xy(component,pin),xy(ref,1)],width=0.15)
    return len(board.GetTracks())
