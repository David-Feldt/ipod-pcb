import sys, re
from schbuilder import Lib, symbol_pins, KICAD_SYMBOLS
from sexpr import find
import os
lib = Lib()
for q in sys.argv[1:]:
    if ":" in q and not q.endswith("*"):
        s = lib.get(q)
        fp = [p for p in s if isinstance(p, list) and p[0]=="property" and p[1]=="Footprint"]
        print("##", q, "FP=", fp[0][2] if fp else "")
        for p in sorted(symbol_pins(s), key=lambda p: (len(p["number"]), p["number"])):
            print("  %4s %-14s %-13s (%g,%g,%g)%s" % (p["number"], p["name"], p["type"], p["x"], p["y"], p["ang"], " H" if p["hidden"] else ""))
    else:
        l, pat = q.rstrip("*").split(":")
        print("##", l, [n for n in lib._lib(l) if n.startswith(pat)])
