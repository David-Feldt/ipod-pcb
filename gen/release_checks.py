"""Release checks shared by export and pre-order validation (standard Python)."""
import csv
import hashlib
import math
import re
import zipfile
from collections import Counter


def require(condition, message):
    if not condition:
        raise ValueError(message)


def release_paths(root):
    """Include rules, generator inputs and all manufacturing reference files."""
    paths = [
        "build.sh", "check-order.sh", "gen/release.py",
        "kicad/eink-pod.kicad_pcb", "kicad/eink-pod.kicad_sch",
        "kicad/eink-pod.kicad_pro", "kicad/design.json", "kicad/fp-lib-table",
        "kicad/erc.rpt", "kicad/drc.rpt", "kicad/drc-all.rpt",
        "kicad/verification.txt",
        "fab/eink-pod-gerbers.zip", "fab/eink-pod-bom.csv", "fab/eink-pod-cpl.csv",
        "fab/eink-pod-schematic.pdf", "fab/assembly-pin1.csv",
        "fab/assembly-front.svg", "fab/assembly-back.svg", "fab/mechanical.svg",
        "fab/debug-access.csv", "fab/debug-map.svg",
        "fab/README.md", "README.md", "VALIDATION.md", "ORDER-READINESS.md",
        "DESIGN.md", "MECHANICAL.md", "BRINGUP.md", "DEBUG.md",
    ]
    paths += [str(p.relative_to(root)) for p in (root / "gen").glob("*.py")]
    paths += [str(p.relative_to(root)) for p in (root / "kicad/eink-pod.pretty").glob("*.kicad_mod")]
    return sorted(set(paths))


def manifest_text(root):
    return "".join(
        hashlib.sha256((root / p).read_bytes()).hexdigest() + "  " + p + "\n"
        for p in release_paths(root)
    )


def verify_manifest(root):
    actual = {}
    for line in (root / "fab/manifest-sha256.txt").read_text().splitlines():
        digest, path = line.split("  ", 1)
        require(path not in actual, "Duplicate manifest path: " + path)
        actual[path] = digest
    require(set(actual) == set(release_paths(root)),
            "Release manifest coverage changed; validate and regenerate the release")
    for path, digest in actual.items():
        require(hashlib.sha256((root / path).read_bytes()).hexdigest() == digest,
                "Stale release file: " + path)


def read_csv(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def verify_assembly(design, bom, cpl, footprints):
    """footprints maps references to independently read PCB values/placements."""
    components = design["components"]
    require(len({c["ref"] for c in components}) == len(components),
            "Duplicate reference in design")
    expected = {c["ref"]: c for c in components
                if not c.get("dnp") and not c["ref"].startswith(("TP", "H"))}
    bom_refs = [ref for row in bom for ref in row["Designator"].split(",")]
    require(Counter(bom_refs) == Counter({ref: 1 for ref in expected}),
            "BOM missing, duplicate or unexpected references")
    require(Counter(row["Designator"] for row in cpl)
            == Counter({ref: 1 for ref in expected}),
            "CPL missing, duplicate or unexpected references")
    for row in bom:
        refs = row["Designator"].split(",")
        require(int(row["Qty"]) == len(refs), "BOM quantity mismatch")
        for ref in refs:
            c = expected[ref]
            require(re.fullmatch(r"C[0-9]+", c.get("lcsc") or "") is not None,
                    ref + ": missing valid LCSC ID")
            require(row["LCSC Part #"] == c["lcsc"], ref + ": wrong BOM LCSC ID")
            require(row["Footprint"] == c["footprint"].split(":")[-1],
                    ref + ": wrong BOM footprint")
            require(c["value"] in row["Comment"].split("/"), ref + ": wrong BOM value")
            require(ref in footprints, ref + ": missing PCB footprint")
            fp = footprints[ref]
            require(not fp["dnp"], ref + ": assembled part is DNP on PCB")
            require(not fp["exclude_pos"], ref + ": assembled part excluded from placement")
            require(fp["lcsc"] == c["lcsc"], ref + ": PCB LCSC ID mismatch")
            require(fp["footprint"] == c["footprint"], ref + ": PCB footprint mismatch")
            require(fp["value"] == c["value"], ref + ": PCB value mismatch")
    for row in cpl:
        ref = row["Designator"]
        fp = footprints[ref]
        require(row["Layer"] == fp["side"], ref + ": wrong placement side")
        for column, key in (("Mid X", "x"), ("Mid Y", "y")):
            require(row[column].endswith("mm"), ref + ": placement units must be mm")
            value = float(row[column][:-2])
            require(math.isfinite(value) and abs(value - fp[key]) <= 0.001,
                    ref + ": wrong placement " + column)
        angle = float(row["Rotation"])
        require(math.isfinite(angle) and abs((angle - fp["angle"] + 180) % 360 - 180) <= 0.001,
                ref + ": wrong placement rotation")
    return len(expected)


def verify_archive(path):
    required = {
        "eink-pod-F_Cu.gtl", "eink-pod-In1_GND.g1", "eink-pod-In2_3V3.g2",
        "eink-pod-B_Cu.gbl", "eink-pod-F_Mask.gts", "eink-pod-B_Mask.gbs",
        "eink-pod-F_Paste.gtp", "eink-pod-B_Paste.gbp",
        "eink-pod-F_Silkscreen.gto", "eink-pod-B_Silkscreen.gbo",
        "eink-pod-Edge_Cuts.gm1", "eink-pod.drl", "eink-pod-job.gbrjob",
    }
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        require(len(names) == len(set(names)), "Duplicate fabrication archive member")
        require(set(names) == required, "Unexpected or missing fabrication archive layers/drills")
        require(archive.testzip() is None, "Corrupt fabrication archive")
        require(all(archive.getinfo(name).file_size > 0 for name in names),
                "Empty fabrication archive member")


def verify_drc(report):
    allowed_ignored = {
        "missing_courtyard", "track_not_centered_on_via", "tuning_profile_track_geometries",
        "footprint_filters_mismatch", "footprint_type_mismatch",
    }
    require({c["key"] for c in report["ignored_checks"]} <= allowed_ignored,
            "New ignored DRC categories need review")
    require(not report["schematic_parity"], "Schematic/PCB parity failed")
    require(not report["unconnected_items"], "Unconnected PCB items")
    seen = set()
    for violation in report["violations"]:
        refs = tuple(item["description"] for item in violation["items"])
        key = (violation["type"], violation["severity"], refs)
        allowed = {("lib_footprint_mismatch", "warning", ("Footprint U1",)),
                   ("lib_footprint_mismatch", "warning", ("Footprint J1",))}
        require(key in allowed and key not in seen, "Unreviewed DRC finding: " + str(key))
        seen.add(key)


REVIEW_ITEMS = {
    "fabricator": "Supplier confirms stackup, 90-ohm USB target, filled/capped vias and manufacturing preview",
    "assembly": "Supplier preview checked against pin-1 maps; exact parts available; assembly side plan confirmed",
    "display_fit": "Actual display/flex fit checked, or explicitly accepted as a first-prototype experiment",
    "battery_fit": "Exact protected pack selected; charge rating, lead polarity and insulated placement checked",
    "prototype_plan": "Small prototype quantity chosen and BRINGUP.md test equipment/firmware plan prepared",
}


def pending_reviews(review, release_digest):
    pending = []
    if review.get("release_sha256") != release_digest:
        pending.append("Supplier/physical review is not tied to this release")
    for key, description in REVIEW_ITEMS.items():
        item = review.get("checks", {}).get(key, {})
        if not (item.get("status") == "pass" and
                all(isinstance(item.get(field), str) and item[field].strip()
                    for field in ("evidence", "reviewer", "date"))):
            pending.append(description)
    return pending
