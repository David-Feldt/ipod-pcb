"""Autoroute kicad/eink-pod.kicad_pcb with Freerouting (run with KiCad's python).

usage: route.py <freerouting.jar> [passes]
"""
import os
import subprocess
import sys
import tempfile
import hashlib

import pcbnew as P

HERE = os.path.dirname(os.path.abspath(__file__))
KDIR = os.path.normpath(os.path.join(HERE, "..", "kicad"))
PCB = os.path.join(KDIR, "eink-pod.kicad_pcb")
sys.path.insert(0, HERE)
from build_pcb import outline_poly, OX, OY  # noqa: E402
from fanout import fanout, EDGE_INSET  # noqa: E402
from critical_routes import route_critical  # noqa: E402
from postroute import repair_power_escape  # noqa: E402


def stitch(board, gnd, step=2.5, keep=0.55):
    """Drop GND vias wherever both outer GND pours fully cover the via + margin."""
    import math
    from build_pcb import W, H
    fills = {}
    for z in board.Zones():
        if z.GetNetname() == "GND" and z.GetLayer() in (P.F_Cu, P.B_Cu) and not z.GetIsRuleArea():
            fills[z.GetLayer()] = z.GetFilledPolysList(z.GetLayer())
    keepouts = [z for z in board.Zones() if z.GetIsRuleArea() and z.GetDoNotAllowVias()]
    keepouts += [z for fp in board.GetFootprints() for z in fp.Zones()
                 if z.GetIsRuleArea() and z.GetDoNotAllowVias()]
    vias = [(P.ToMM(v.GetPosition().x), P.ToMM(v.GetPosition().y))
            for v in board.GetTracks() if v.GetClass() == "PCB_VIA"]
    holes = [(P.ToMM(pd.GetPosition().x), P.ToMM(pd.GetPosition().y))
             for fp in board.GetFootprints() for pd in fp.Pads() if pd.GetDrillSizeX() > 0]
    n = 0
    y = 1.5
    while y < H - 1.0:
        x = 1.5
        while x < W - 1.0:
            ax, ay = OX + x, OY + y
            ring = [(ax, ay)] + [(ax + keep * math.cos(k * math.pi / 4), ay + keep * math.sin(k * math.pi / 4))
                                 for k in range(8)]
            ok = all(f.Contains(P.VECTOR2I_MM(px, py)) for f in fills.values() for px, py in ring)
            ok = ok and all(math.hypot(vx - ax, vy - ay) > 1.0 for vx, vy in vias + holes)
            ok = ok and not any(z.Outline().Contains(P.VECTOR2I_MM(ax, ay)) for z in keepouts)
            if ok:
                v = P.PCB_VIA(board)
                v.SetPosition(P.VECTOR2I_MM(ax, ay))
                v.SetWidth(P.FromMM(0.5)); v.SetDrill(P.FromMM(0.3))
                v.SetNet(gnd)
                board.Add(v)
                vias.append((ax, ay))
                n += 1
            x += step
        y += step
    return n


def route_once(jar, passes, dsn, ses):
    board = P.LoadBoard(PCB)
    for t in list(board.GetTracks()):
        board.Remove(t)
    for z in list(board.Zones()):
        if z.GetLayer() in (P.F_Cu, P.B_Cu) and not z.GetIsRuleArea():
            board.Remove(z)
    print("critical routes:", route_critical(board), flush=True)
    pts = [(OX + x, OY + y) for x, y in outline_poly(EDGE_INSET)]
    added, skipped = fanout(board, pts)
    print("plane fanout: %d vias, %d pads left to the router" % (added, skipped), flush=True)
    if not P.ExportSpecctraDSN(board, dsn):
        raise SystemExit("DSN export failed")
    print("exported", dsn, flush=True)

    if os.path.exists(ses):
        os.remove(ses)
    # No GUI or writes to the user's application settings during a board build.
    runtime = os.path.join(tempfile.gettempdir(), "eink-pod-freerouting")
    os.makedirs(runtime, exist_ok=True)
    cmd = ["java", "-Djava.awt.headless=true", "-Duser.home=" + runtime,
           "-jar", jar, "-de", dsn, "-do", ses, "-mp", passes, "--gui.enabled=false",
           "--usage_and_diagnostic_data.disable_analytics=true"]
    print(" ".join(cmd), flush=True)
    subprocess.run(cmd, check=True)
    if not os.path.exists(ses):
        raise SystemExit("freerouting produced no session file")

    if not P.ImportSpecctraSES(board, ses):
        raise SystemExit("SES import failed")
    print("power via escapes corrected:", repair_power_escape(board), flush=True)

    # outer-layer GND pours
    gnd = board.FindNet("GND")
    for layer in (P.F_Cu, P.B_Cu):
        z = P.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(gnd)
        z.SetAssignedPriority(0)
        z.SetLocalClearance(P.FromMM(0.25))
        z.SetMinThickness(P.FromMM(0.2))
        z.SetPadConnection(P.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(P.FromMM(0.25))
        z.SetThermalReliefSpokeWidth(P.FromMM(0.3))
        ol = z.Outline()
        ol.NewOutline()
        for x, y in outline_poly(0.3):
            ol.Append(P.FromMM(OX + x), P.FromMM(OY + y))
        board.Add(z)
    P.ZONE_FILLER(board).Fill(board.Zones())
    print("stitching vias:", stitch(board, gnd), flush=True)
    P.ZONE_FILLER(board).Fill(board.Zones())
    board.BuildConnectivity()
    return board, board.GetConnectivity().GetUnconnectedCount(False)


def main():
    jar = sys.argv[1]
    passes = sys.argv[2] if len(sys.argv) > 2 else "40"
    # Each run owns its exchange files. Concurrent builds must not import each
    # other's SES (which also contains footprint placements).
    routing_dir = os.path.join(KDIR, "routing")
    os.makedirs(routing_dir, exist_ok=True)
    work = tempfile.mkdtemp(prefix="run-", dir=routing_dir)
    original_hash = hashlib.sha256(open(PCB, "rb").read()).digest()
    dsn, ses = os.path.join(work, "eink-pod.dsn"), os.path.join(work, "eink-pod.ses")

    best = None
    for attempt in range(1, 6):
        board, unrouted = route_once(jar, passes, dsn, ses)
        print("attempt %d: %d unrouted connections" % (attempt, unrouted), flush=True)
        if best is None or unrouted < best[1]:
            best = (board, unrouted)
        if unrouted == 0:
            break
    board = best[0]
    if hashlib.sha256(open(PCB, "rb").read()).digest() != original_hash:
        raise SystemExit("PCB changed during routing; refusing to overwrite it. Rerun against the current board.")
    P.SaveBoard(PCB, board)
    print("routed + saved", PCB, "tracks:", len(board.GetTracks()), flush=True)


if __name__ == "__main__":
    main()
