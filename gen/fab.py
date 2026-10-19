"""Generate JLCPCB fabrication + assembly files into fab/."""
import csv
import glob
import json
import os
import shutil
import subprocess
import zipfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
KDIR = os.path.join(ROOT, "kicad")
FAB = os.path.join(ROOT, "fab")
PCB = os.path.join(KDIR, "eink-pod.kicad_pcb")
APP = os.path.expanduser("~/Applications/KiCad/KiCad.app/Contents")
if not os.path.isdir(APP):
    APP = "/Applications/KiCad/KiCad.app/Contents"
KC = os.path.join(APP, "MacOS", "kicad-cli")

LAYERS = ["F.Cu", "In1.Cu", "In2.Cu", "B.Cu", "F.Paste", "B.Paste", "F.Silkscreen",
          "B.Silkscreen", "F.Mask", "B.Mask", "Edge.Cuts"]


def run(*args):
    subprocess.run([KC, *args], check=True, stdout=subprocess.DEVNULL)


def main():
    gdir = os.path.join(FAB, "gerbers")
    shutil.rmtree(gdir, ignore_errors=True)
    os.makedirs(gdir)
    run("pcb", "export", "gerbers", "--layers", ",".join(LAYERS), "--subtract-soldermask",
        "--check-zones", "-o", gdir + "/", PCB)
    run("pcb", "export", "drill", "--format", "excellon", "--excellon-units", "mm",
        "--generate-map", "--map-format", "pdf", "-o", gdir + "/", PCB)
    zpath = os.path.join(FAB, "eink-pod-gerbers.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(glob.glob(os.path.join(gdir, "*"))):
            if not f.endswith(".pdf"):
                z.write(f, os.path.basename(f))

    # placement (CPL) in JLC column format
    raw = os.path.join(FAB, "pos-raw.csv")
    run("pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both",
        "--exclude-dnp", "-o", raw, PCB)
    with open(raw) as f, open(os.path.join(FAB, "eink-pod-cpl.csv"), "w", newline="") as g:
        w = csv.writer(g, lineterminator="\n")
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for r in csv.DictReader(f):
            if r["Ref"].startswith(("TP", "H")):
                continue
            w.writerow([r["Ref"], r["PosX"] + "mm", r["PosY"] + "mm",
                        "Top" if r["Side"] == "top" else "Bottom", r["Rot"]])
    os.remove(raw)

    # BOM grouped by LCSC part
    design = json.load(open(os.path.join(KDIR, "design.json")))
    groups = {}
    for c in design["components"]:
        if c["ref"].startswith(("TP", "H")) or c.get("dnp"):
            continue
        key = (c["footprint"].split(":")[1], c.get("lcsc") or c["value"])
        g = groups.setdefault(key, {"refs": [], "values": []})
        g["refs"].append(c["ref"])
        if c["value"] not in g["values"]:
            g["values"].append(c["value"])

    def refkey(r):
        p = r.rstrip("0123456789")
        return (p, int(r[len(p):]))
    with open(os.path.join(FAB, "eink-pod-bom.csv"), "w", newline="") as g:
        w = csv.writer(g, lineterminator="\n")
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #", "Qty"])
        for (fp, lcsc), grp in sorted(groups.items(), key=lambda kv: refkey(sorted(kv[1]["refs"], key=refkey)[0])):
            refs = sorted(grp["refs"], key=refkey)
            w.writerow(["/".join(grp["values"]), ",".join(refs), fp, lcsc, len(refs)])
    missing = [k for k in groups if not k[1].startswith("C")]
    run("sch", "export", "pdf", "-o", os.path.join(FAB, "eink-pod-schematic.pdf"),
        os.path.join(KDIR, "eink-pod.kicad_sch"))
    render_dir = os.path.join(ROOT, "renders")
    os.makedirs(render_dir, exist_ok=True)
    for name, side in [("front", "top"), ("back", "bottom")]:
        run("pcb", "render", "--side", side, "--width", "1000", "--height", "1800",
            "--background", "transparent", "-o", os.path.join(render_dir, name + ".png"), PCB)
    print("fab outputs written to", FAB, "| BOM lines:", len(groups),
          "| parts without LCSC:", missing or "none")


if __name__ == "__main__":
    main()
