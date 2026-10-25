"""Read-only pre-order checks. Run using check-order.sh with KiCad Python."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

import pcbnew as P
import wx
from release_checks import (pending_reviews, read_csv, require, verify_archive,
                            verify_assembly, verify_drc, verify_manifest)
from verify_board import verify

ROOT = Path(__file__).resolve().parent.parent


def board_assembly(board):
    # Matches KiCad absolute, non-negated-bottom-X position export: X, -Y.
    return {
        f.GetReference(): {
            "value": f.GetValue(),
            "footprint": str(f.GetFPID().GetLibNickname()) + ":" + str(f.GetFPID().GetLibItemName()),
            "lcsc": next((field.GetText() for field in f.GetFields()
                          if field.GetName() == "LCSC"), ""),
            "x": P.ToMM(f.GetPosition().x), "y": -P.ToMM(f.GetPosition().y),
            "angle": f.GetOrientationDegrees(),
            "side": "Top" if f.GetLayer() == P.F_Cu else "Bottom",
            "dnp": bool(f.GetAttributes() & P.FP_DNP),
            "exclude_pos": bool(f.GetAttributes() & P.FP_EXCLUDE_FROM_POS_FILES),
        }
        for f in board.GetFootprints()
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--automated-only", action="store_true",
                        help="exit 0 on automated checks; manual readiness is still reported")
    args = parser.parse_args()
    os.chdir(ROOT)
    require(__debug__, "Python optimization disables layout assertions; unset PYTHONOPTIMIZE")
    verify_manifest(ROOT)
    print("PASS: release hashes cover design, rules, sources and manufacturing references", flush=True)
    kc = os.environ["KICAD_CLI"]
    with tempfile.TemporaryDirectory(prefix="ipod-order-") as temp:
        erc = Path(temp) / "erc.rpt"
        drc = Path(temp) / "drc.json"
        for command in [
            [kc, "sch", "erc", "--severity-all", "--exit-code-violations",
             "-o", str(erc), "kicad/eink-pod.kicad_sch"],
            [kc, "pcb", "drc", "--severity-all", "--schematic-parity",
             "--refill-zones", "--format", "json", "-o", str(drc),
             "kicad/eink-pod.kicad_pcb"],
        ]:
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                # Preserve diagnostics on failure, suppress successful CLI/fontconfig chatter.
                print(result.stdout + result.stderr, file=sys.stderr)
                if erc.exists():
                    print(erc.read_text(), file=sys.stderr)
                result.check_returncode()
        verify_drc(json.loads(drc.read_text()))
    print("PASS: fresh ERC and DRC/parity; only the two documented footprint warnings are allowed")
    # Supply wx traits without opening a GUI. pcbnew installs its own image
    # handlers on LoadBoard, so clear wxPython's initial registrations first.
    app = wx.GetApp() or wx.AppConsole()
    wx.Image.CleanUpHandlers()
    board = P.LoadBoard(str(ROOT / "kicad/eink-pod.kicad_pcb"))
    verify(board)
    count = verify_assembly(json.loads((ROOT / "kicad/design.json").read_text()),
                            read_csv(ROOT / "fab/eink-pod-bom.csv"),
                            read_csv(ROOT / "fab/eink-pod-cpl.csv"), board_assembly(board))
    verify_archive(ROOT / "fab/eink-pod-gerbers.zip")
    # Detect files changing while the check ran.
    verify_manifest(ROOT)
    print(f"PASS: {count} exact BOM assignments and PCB placements; complete fabrication archive")
    digest = hashlib.sha256((ROOT / "fab/manifest-sha256.txt").read_bytes()).hexdigest()
    review_path = ROOT / "order-review.json"
    review = json.loads(review_path.read_text()) if review_path.exists() else {}
    pending = pending_reviews(review, digest)
    print("Release SHA256: " + digest)
    print("AUTOMATED CHECKS: PASS")
    if pending:
        print("PROTOTYPE ORDER: HOLD — external checks remain in order-review.json")
        for item in pending:
            print("  PENDING: " + item)
    else:
        print("PROTOTYPE ORDER: recorded reviews complete for this release")
    print("BATCH ORDER: requires separate measured hardware acceptance per BRINGUP.md")
    return 2 if pending and not args.automated_only else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, AssertionError, OSError, KeyError, zipfile.BadZipFile,
            subprocess.CalledProcessError) as error:
        print("PRE-ORDER CHECK FAILED: " + str(error), file=sys.stderr)
        sys.exit(1)
