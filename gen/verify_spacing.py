"""Check mechanical spacing that the padless display's missing courtyard omits."""
import math

import pcbnew as P


def xy(p):
    return P.ToMM(p.x), P.ToMM(p.y)


def bounds(items):
    points = [xy(p) for item in items for p in (item.GetStart(), item.GetEnd())]
    assert points, "Missing mechanical outline"
    return (min(p[0] for p in points), min(p[1] for p in points),
            max(p[0] for p in points), max(p[1] for p in points))


def gap(a, b):
    """Distance between axis-aligned envelopes; zero means touching/overlap."""
    return math.hypot(max(a[0] - b[2], b[0] - a[2], 0),
                      max(a[1] - b[3], b[1] - a[3], 0))


def courtyard(fp):
    # The checked front switches and wheel use rectangular/orthogonal outlines.
    return bounds(g for g in fp.GraphicalItems()
                  if isinstance(g, P.PCB_SHAPE) and
                  g.GetLayer() in (P.F_CrtYd, P.B_CrtYd))


def verify_spacing(board):
    fps = {f.GetReference(): f for f in board.GetFootprints()}
    panel = fps['DS1']
    glass = bounds(g for g in panel.GraphicalItems()
                   if isinstance(g, P.PCB_SHAPE) and g.GetLayer() == P.F_Fab
                   and g.GetShape() == P.SHAPE_T_RECT)
    assert panel.GetLayer() == P.F_Cu and abs(panel.GetOrientationDegrees()) < 0.001
    assert abs(glass[2] - glass[0] - 29.2) < 0.001
    assert abs(glass[3] - glass[1] - 59.2) < 0.001

    # Front assembly envelopes and all through-hole lands must clear the glass.
    front_gaps, pad_gaps = [], []
    for ref, fp in fps.items():
        if ref != 'DS1' and fp.GetLayer() == P.F_Cu:
            distance = gap(glass, courtyard(fp))
            assert distance >= 0.25, (ref, 'front courtyard/glass gap', distance)
            front_gaps.append(distance)
        for pad in fp.Pads():
            if pad.IsOnLayer(P.F_Cu):
                bb = pad.GetBoundingBox()
                envelope = tuple(P.ToMM(v) for v in
                                 (bb.GetLeft(), bb.GetTop(), bb.GetRight(), bb.GetBottom()))
                distance = gap(glass, envelope)
                assert distance >= 0.25, (ref, pad.GetNumber(), 'front land/glass gap', distance)
                pad_gaps.append(distance)

    head_gaps = []
    for ref in ('H1', 'H2', 'H3', 'H4'):
        x, y = xy(fps[ref].GetPosition())
        distance = gap(glass, (x, y, x, y)) - 2.25
        assert distance >= 0.65 - 0.001, (ref, '4.5 mm head/glass gap', distance)
        head_gaps.append(distance)

    cx, cy = xy(fps['SW5'].GetPosition())
    for ref, offset in {'SW1': (0, -10), 'SW2': (0, 10),
                        'SW3': (-10, 0), 'SW4': (10, 0)}.items():
        assert math.dist(xy(fps[ref].GetPosition()),
                         (cx + offset[0], cy + offset[1])) < 0.001, (ref, 'button pitch')

    antenna = [z for z in fps['U1'].Zones() if z.GetIsRuleArea()]
    assert antenna, 'Missing antenna keepout'
    for zone in antenna:
        bb = zone.GetBoundingBox()
        envelope = tuple(P.ToMM(v) for v in
                         (bb.GetLeft(), bb.GetTop(), bb.GetRight(), bb.GetBottom()))
        assert gap(glass, envelope) >= 1.85 - 0.001, 'Glass enters antenna margin'
        assert gap(courtyard(fps['RV1']), envelope) >= 1.5 - 0.001, 'Wheel enters antenna margin'

    print('PASS: 10 mm button pitch; glass gaps: %.2f mm to front courtyards, '
          '%.2f mm to front lands, %.2f mm to 4.5 mm mounting heads' %
          (min(front_gaps), min(pad_gaps), min(head_gaps)))
    print('PASS: glass and wheel courtyard clear antenna keepout by at least 1.85 / 1.50 mm')


if __name__ == '__main__':
    from pathlib import Path
    verify_spacing(P.LoadBoard(str(Path(__file__).resolve().parent.parent /
                                 'kicad/eink-pod.kicad_pcb')))
