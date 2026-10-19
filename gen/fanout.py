"""Plane fanout: give every GND / +3V3 pad its own via to the inner plane.

Freerouting does not reliably drop vias into power planes, so we do it here
before autorouting. Big exposed pads get vias in the pad; everything else gets
a short stub to the nearest via site that clears all other copper.
"""
import math

import pcbnew as P

PLANE_NETS = ("GND", "+3V3")
VIA_D, VIA_DRILL = 0.5, 0.3
STUB_W = 0.25
CLR = 0.2          # via / stub clearance to other copper
HOLE2HOLE = 0.3
EDGE_INSET = 0.65  # via centre to board edge


def _mm(v):
    return P.ToMM(v)


class Rect:
    __slots__ = ("x0", "y0", "x1", "y1", "net", "layers", "pad")

    def __init__(self, bb, net, layers, pad=None):
        self.x0, self.y0 = _mm(bb.GetLeft()), _mm(bb.GetTop())
        self.x1, self.y1 = _mm(bb.GetRight()), _mm(bb.GetBottom())
        self.net, self.layers, self.pad = net, layers, pad

    def dist(self, x, y):
        dx = max(self.x0 - x, 0, x - self.x1)
        dy = max(self.y0 - y, 0, y - self.y1)
        return math.hypot(dx, dy)


def _seg_point_dist(ax, ay, bx, by, px, py):
    vx, vy = bx - ax, by - ay
    L = vx * vx + vy * vy
    t = 0 if L == 0 else max(0, min(1, ((px - ax) * vx + (py - ay) * vy) / L))
    return math.hypot(ax + t * vx - px, ay + t * vy - py)


def _inside(poly, x, y):
    c = False
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % n]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            c = not c
    return c


def fanout(board, outline_pts):
    rects = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            layers = {l for l in (P.F_Cu, P.B_Cu) if pad.IsOnLayer(l)}
            if pad.GetDrillSizeX() > 0:
                layers = {P.F_Cu, P.B_Cu, "hole"}
            rects.append(Rect(pad.GetBoundingBox(), pad.GetNetname(), layers, pad))
    keepouts = [z for z in board.Zones() if z.GetIsRuleArea() and z.GetDoNotAllowVias()]
    keepouts += [z for fp in board.GetFootprints() for z in fp.Zones()
                 if z.GetIsRuleArea() and z.GetDoNotAllowVias()]
    vias = []    # (x, y, net)
    stubs = []   # (ax, ay, bx, by, layer, net, width)
    # Respect the fixed signal / power wiring placed before plane fanout.
    for t in board.GetTracks():
        if isinstance(t, P.PCB_VIA):
            pos = t.GetPosition()
            vias.append((_mm(pos.x), _mm(pos.y), t.GetNetname()))
        else:
            a, b = t.GetStart(), t.GetEnd()
            stubs.append((_mm(a.x), _mm(a.y), _mm(b.x), _mm(b.y),
                          t.GetLayer(), t.GetNetname(), _mm(t.GetWidth())))
    added = skipped = 0

    def via_ok(x, y, net, own):
        if not _inside(outline_pts, x, y):
            return False
        for z in keepouts:
            for dx, dy in ((0, 0), (0.3, 0), (-0.3, 0), (0, 0.3), (0, -0.3)):
                if z.Outline().Contains(P.VECTOR2I_MM(x + dx, y + dy)):
                    return False
        r = VIA_D / 2
        for rc in rects:
            if rc.pad is own:
                continue
            need = CLR + r if rc.net != net else 0.1 + r
            if "hole" in rc.layers:
                need = max(need, HOLE2HOLE + VIA_DRILL / 2)
            if rc.dist(x, y) < need:
                return False
        for vx, vy, _ in vias:
            if math.hypot(vx - x, vy - y) < VIA_D + CLR:
                return False
        for ax, ay, bx, by, _, snet, sw in stubs:
            if snet != net and _seg_point_dist(ax, ay, bx, by, x, y) < CLR + r + sw / 2:
                return False
        return True

    def stub_ok(ax, ay, bx, by, layer, net, own):
        steps = max(2, int(math.hypot(bx - ax, by - ay) / 0.05))
        for i in range(steps + 1):
            t = i / steps
            px, py = ax + (bx - ax) * t, ay + (by - ay) * t
            for rc in rects:
                if rc.pad is own or rc.net == net or layer not in rc.layers:
                    continue
                if rc.dist(px, py) < CLR + STUB_W / 2:
                    return False
            for vx, vy, vnet in vias:
                if vnet != net and math.hypot(vx - px, vy - py) < CLR + VIA_D / 2 + STUB_W / 2:
                    return False
            for sx, sy, ex, ey, sl, snet, sw in stubs:
                if sl == layer and snet != net and _seg_point_dist(sx, sy, ex, ey, px, py) < CLR + (sw + STUB_W) / 2:
                    return False
        return True

    def add_via(x, y, net_item):
        v = P.PCB_VIA(board)
        v.SetPosition(P.VECTOR2I_MM(x, y))
        v.SetWidth(P.FromMM(VIA_D))
        v.SetDrill(P.FromMM(VIA_DRILL))
        v.SetNet(net_item)
        v.SetLocked(True)
        board.Add(v)
        vias.append((x, y, net_item.GetNetname()))

    def add_stub(ax, ay, bx, by, layer, net_item):
        t = P.PCB_TRACK(board)
        t.SetStart(P.VECTOR2I_MM(ax, ay))
        t.SetEnd(P.VECTOR2I_MM(bx, by))
        t.SetWidth(P.FromMM(STUB_W))
        t.SetLayer(layer)
        t.SetNet(net_item)
        t.SetLocked(True)
        board.Add(t)
        stubs.append((ax, ay, bx, by, layer, net_item.GetNetname(), STUB_W))

    for fp in board.GetFootprints():
        fc = fp.GetPosition()
        fcx, fcy = _mm(fc.x), _mm(fc.y)
        for pad in fp.Pads():
            net = pad.GetNetname()
            if net not in PLANE_NETS or pad.GetDrillSizeX() > 0:
                continue  # through-hole pads already reach the planes
            layer = P.F_Cu if pad.IsOnLayer(P.F_Cu) else P.B_Cu
            c = pad.GetPosition()
            cx, cy = _mm(c.x), _mm(c.y)
            sz = pad.GetSize(layer)
            w, h = _mm(sz.x), _mm(sz.y)
            if min(w, h) >= 1.15 and via_ok(cx, cy, net, pad):
                # exposed / thermal pad: via in pad
                add_via(cx, cy, pad.GetNet())
                added += 1
                continue
            out = math.atan2(cy - fcy, cx - fcx) if (cx, cy) != (fcx, fcy) else 0.0
            cands = []
            for r in (0.7, 0.9, 1.1, 1.35, 1.6, 1.9, 2.3, 2.8):
                for k in range(16):
                    a = k * math.pi / 8
                    dev = abs((a - out + math.pi) % (2 * math.pi) - math.pi)
                    cands.append((r + dev * 0.25, r, a))
            cands.sort()
            done = False
            for _, r, a in cands:
                x, y = cx + r * math.cos(a), cy + r * math.sin(a)
                if via_ok(x, y, net, pad) and stub_ok(cx, cy, x, y, layer, net, pad):
                    add_via(x, y, pad.GetNet())
                    add_stub(cx, cy, x, y, layer, pad.GetNet())
                    added += 1
                    done = True
                    break
            if not done:
                skipped += 1
    return added, skipped
