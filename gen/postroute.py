"""Correct a Freerouting via escape in the narrow C1 / USB-trunk corridor.

The router can put the VBUS via between C1's ground pad and the protected USB
pair with less than the required 0.20 mm clearance. Move that escape above C1,
updating every attached segment. The normal final KiCad DRC validates the move.
"""
import pcbnew as P


def repair_power_escape(board):
    moved = 0
    for via in list(board.GetTracks()):
        if not isinstance(via, P.PCB_VIA) or via.IsLocked() or via.GetNetname() != 'VBUS':
            continue
        old = via.GetPosition()
        x, y = P.ToMM(old.x)-100, P.ToMM(old.y)-100
        if not (15.7 < x < 16.05 and 81.35 < y < 82.55):
            continue
        new = P.VECTOR2I_MM(115.85, 181.0)
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
