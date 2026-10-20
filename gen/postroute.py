"""Correct Freerouting power via escapes beside the protected USB wiring.

The router can put the VBUS via between C1's ground pad and the protected USB
pair with less than the required 0.20 mm clearance. Move that escape above C1,
updating every attached segment. The normal final KiCad DRC validates the move.
The connector's left VBUS escape can similarly crowd the fixed D+ neckdown;
move that via toward the VBUS pad, away from the data trace.
"""
import pcbnew as P


def repair_power_escape(board):
    moved = 0
    for via in list(board.GetTracks()):
        if not isinstance(via, P.PCB_VIA) or via.IsLocked() or via.GetNetname() != 'VBUS':
            continue
        old = via.GetPosition()
        x, y = P.ToMM(old.x)-100, P.ToMM(old.y)-100
        if 15.7 < x < 16.05 and 81.35 < y < 82.55:
            new = P.VECTOR2I_MM(115.85, 181.0)
        elif 15.4 < x < 15.8 and 88.7 < y < 89.4:
            new = P.VECTOR2I_MM(115.3, 100 + y)
        else:
            continue
        for track in board.GetTracks():
            if isinstance(track, P.PCB_VIA) or track.GetNetCode() != via.GetNetCode():
                continue
            if track.GetStart() == old:
                assert not track.IsLocked(), 'Do not move a protected track'
                track.SetStart(new)
            if track.GetEnd() == old:
                assert not track.IsLocked(), 'Do not move a protected track'
                track.SetEnd(new)
        via.SetPosition(new)
        moved += 1
    return moved
