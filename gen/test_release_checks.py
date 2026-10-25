"""Fault-injection tests for order checks; no edits to actual release files."""
import copy
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from release_checks import (REVIEW_ITEMS, pending_reviews, verify_archive,
                            verify_assembly, verify_drc, verify_manifest)


class AssemblyChecks(unittest.TestCase):
    def setUp(self):
        self.design = {"components": [
            {"ref": "R1", "value": "10k", "footprint": "Resistor:R_0402",
             "lcsc": "C123", "dnp": False},
            {"ref": "J3", "value": "DEBUG", "footprint": "Header:J3", "dnp": True},
        ]}
        self.bom = [{"Designator": "R1", "Comment": "10k", "Footprint": "R_0402",
                     "LCSC Part #": "C123", "Qty": "1"}]
        self.cpl = [{"Designator": "R1", "Mid X": "101mm", "Mid Y": "-102mm",
                     "Layer": "Bottom", "Rotation": "-90"}]
        self.fps = {"R1": {"value": "10k", "footprint": "Resistor:R_0402",
                           "lcsc": "C123", "dnp": False, "exclude_pos": False,
                           "x": 101, "y": -102, "side": "Bottom", "angle": -90}}

    def check(self):
        return verify_assembly(self.design, self.bom, self.cpl, self.fps)

    def test_good_release_and_equivalent_angle(self):
        self.assertEqual(self.check(), 1)
        self.cpl[0]["Rotation"] = "270"
        self.assertEqual(self.check(), 1)

    def test_wrong_part_or_value_or_footprint(self):
        for column in ("LCSC Part #", "Comment", "Footprint"):
            with self.subTest(column=column):
                original = self.bom[0][column]
                self.bom[0][column] = "wrong"
                with self.assertRaises(ValueError):
                    self.check()
                self.bom[0][column] = original

    def test_placement_errors(self):
        for column, value in [("Mid X", "102mm"), ("Mid Y", "102mm"),
                              ("Mid X", "nanmm"), ("Mid X", "101"),
                              ("Layer", "Top"), ("Rotation", "90"),
                              ("Rotation", "nan")]:
            with self.subTest(column=column, value=value):
                original = self.cpl[0][column]
                self.cpl[0][column] = value
                with self.assertRaises(ValueError):
                    self.check()
                self.cpl[0][column] = original

    def test_duplicate_bom_reference_even_with_matching_quantity(self):
        self.bom[0].update(Designator="R1,R1", Qty="2")
        with self.assertRaises(ValueError):
            self.check()

    def test_missing_or_duplicate_placement(self):
        self.cpl.append(copy.deepcopy(self.cpl[0]))
        with self.assertRaises(ValueError):
            self.check()
        self.cpl.clear()
        with self.assertRaises(ValueError):
            self.check()

    def test_dnp_leak(self):
        self.cpl.append(dict(self.cpl[0], Designator="J3"))
        with self.assertRaises(ValueError):
            self.check()

    def test_missing_catalogue_id_does_not_silently_drop_part(self):
        self.design["components"][0]["lcsc"] = None
        with self.assertRaises(ValueError):
            self.check()

    def test_changed_board_fields(self):
        for key, value in [("lcsc", "C456"), ("value", "1k"), ("dnp", True),
                           ("exclude_pos", True), ("footprint", "Resistor:R_0805")]:
            with self.subTest(key=key):
                original = self.fps["R1"][key]
                self.fps["R1"][key] = value
                with self.assertRaises(ValueError):
                    self.check()
                self.fps["R1"][key] = original


class ReleaseChecks(unittest.TestCase):
    def test_new_drc_findings_and_ignored_categories_fail(self):
        base = {"ignored_checks": [], "schematic_parity": [],
                "unconnected_items": [], "violations": []}
        verify_drc(base)
        warning = {"type": "lib_footprint_mismatch", "severity": "warning",
                   "items": [{"description": "Footprint U1"}]}
        verify_drc(dict(base, violations=[warning]))
        for field, value in [
            ("ignored_checks", [{"key": "clearance"}]),
            ("schematic_parity", [{}]), ("unconnected_items", [{}]),
            ("violations", [dict(warning, severity="error")]),
            ("violations", [dict(warning, items=[{"description": "Footprint U5"}])]),
            ("violations", [dict(warning, type="silk_overlap")]),
        ]:
            with self.subTest(field=field):
                with self.assertRaises(ValueError):
                    verify_drc(dict(base, **{field: value}))

    def test_missing_or_changed_manifest_file(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "fab").mkdir()
            (root / "board").write_bytes(b"released")
            digest = hashlib.sha256(b"released").hexdigest()
            manifest = root / "fab/manifest-sha256.txt"
            manifest.write_text(digest + "  board\n")
            with patch("release_checks.release_paths", return_value=["board"]):
                verify_manifest(root)
                (root / "board").write_bytes(b"edited")
                with self.assertRaisesRegex(ValueError, "Stale release"):
                    verify_manifest(root)
                manifest.write_text("")
                with self.assertRaisesRegex(ValueError, "coverage"):
                    verify_manifest(root)

    def test_incomplete_archive_fails(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "gerbers.zip"
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("eink-pod-F_Cu.gtl", "copper")
            with self.assertRaises(ValueError):
                verify_archive(path)

    def test_manual_review_requires_evidence_and_matching_release(self):
        self.assertTrue(pending_reviews({}, "abc"))
        review = {"release_sha256": "abc", "checks": {
            key: {"status": "pass", "evidence": "Checked order preview",
                  "reviewer": "Engineer", "date": "2026-10-19"}
            for key in REVIEW_ITEMS}}
        self.assertFalse(pending_reviews(review, "abc"))
        self.assertTrue(pending_reviews(review, "changed"))
        review["checks"]["assembly"]["evidence"] = " "
        self.assertTrue(pending_reviews(review, "abc"))


if __name__ == "__main__":
    unittest.main()
