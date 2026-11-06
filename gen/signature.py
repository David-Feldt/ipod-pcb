"""Add the script-font maker's signature to the routed board (run after route.py)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcbnew as P  # noqa: E402
from build_pcb import PCB, add_signature, write_signature_font  # noqa: E402

board = P.LoadBoard(PCB)
add_signature(board)
P.SaveBoard(PCB, board)
write_signature_font(PCB)
print("signature added", PCB)
