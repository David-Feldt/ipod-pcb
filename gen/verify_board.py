"""Electrical/layout regression checks beyond KiCad ERC/DRC.

Run with KiCad's Python after routing. Fail if the router changes the fixed
USB/boost routes, if sense isolation changes, or local decoupling moves away.
"""
import math
import os
import pcbnew as P

from critical_routes import route_critical

PCB = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "kicad", "eink-pod.kicad_pcb"))


def point(p):
    return (P.ToMM(p.x), P.ToMM(p.y))


def verify():
    board = P.LoadBoard(PCB)
    fps = {f.GetReference(): f for f in board.GetFootprints()}

    def pad(ref, number):
        return next(p for p in fps[ref].Pads() if p.GetNumber() == str(number))

    # Body-diode orientation and domain boundaries are intentional.
    expected_nets = {
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
    for ref, pins in expected_nets.items():
        for num, net in pins.items():
            assert pad(ref, num).GetNetname() == net, (ref, num, net)
    for ref, value in {"R6": "1M", "R7": "1M", "R8": "100k", "R9": "1M",
                       "R11": "1M", "R12": "22", "R13": "22", "C5": "100nF"}.items():
        assert fps[ref].GetValue() == value, (ref, value)
    for ref, limit in [("C7", 2.0), ("C6", 5.0), ("C9", 5.0)]:
        distance = math.dist(point(pad("U1", 3).GetPosition()), point(pad(ref, 1).GetPosition()))
        assert distance <= limit, (ref, distance, limit)
        print("%s to MCU supply pad: %.2f mm" % (ref, distance))

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
    # No extra branches on the USB data nets; two layer changes per wire.
    for net in ["USB_DP", "USB_DN", "USB_MCU_P", "USB_MCU_N"]:
        actual_len = sum(t["length"] for t in actual_tracks if t["net"] == net)
        expected_len = sum(math.dist(t["start"], t["end"]) for t in expected if "start" in t and t["net"] == net)
        assert abs(actual_len - expected_len) < 0.05, (net, "extra/missing route", actual_len, expected_len)
        nv = sum(v["net"] == net for v in actual_vias)
        assert nv == (2 if net in ("USB_DP", "USB_DN") else 0), (net, nv)
        print("%s: %.2f mm total copper, %d vias" % (net, actual_len, nv))
    # The USB trunk references an unbroken ground plane, not inner signal tracks.
    assert not any(t["layer"] == P.In1_Cu for t in actual_tracks), "Signal routing in ground plane"
    assert any(z.GetLayer() == P.In1_Cu and z.GetNetname() == "GND" for z in board.Zones())
    print("PASS: sense isolation, capacitor placement, fixed routes, USB branches/vias, ground reference")


if __name__ == "__main__":
    verify()
