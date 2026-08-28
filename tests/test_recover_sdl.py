#!/usr/bin/env python3
"""Regression tests for the deterministic Sword3 static-recovery report."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "recover_sdl.py"
MANIFEST = ROOT / "configs" / "sword3" / "binary-manifest.json"
REPORT = ROOT / "configs" / "sword3" / "recovery-report.json"
DEFAULT_BINARY = Path("/tmp/sword3-ipa-extract/Payload/SWD3.app/SWD3")

EXPECTED_SHA256 = "268d6f40eac47718ee2cac6acd34912421546eb8a58056d310c0b42d2655e36b"
EXPECTED_ENTRY = "0x1000f17cc"
EXPECTED_CALLBACK = "0x10002c3b0"
EXPECTED_CALLBACK_END = "0x10002c5f8"
EXPECTED_CALLBACK_HEX = "ff0302d1f65705a9f44f06a9fd7b07a9"


def load_json(path: Path) -> dict:
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


class CheckedRecoveryReportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.report = load_json(REPORT)

    def test_manifest_and_function_index(self) -> None:
        report = self.report
        self.assertEqual(report["schema_version"], 1)
        self.assertEqual(report["mode"], "static-recovery")
        self.assertEqual(report["inputs"]["binary_sha256"], EXPECTED_SHA256)
        self.assertTrue(report["manifest_validation"]["matched"])
        self.assertTrue(all(report["manifest_validation"]["checks"].values()))
        self.assertEqual(report["function_index"]["source"], "LC_FUNCTION_STARTS")
        self.assertEqual(report["function_index"]["count"], 6293)

        manifest = load_json(MANIFEST)
        self.assertEqual(manifest["function_starts"]["count"], 6293)
        self.assertEqual(
            report["function_index"]["count"],
            len(manifest["function_starts"]["entries"]),
        )

    def test_fixed_entry_flow_reaches_uiapplicationmain(self) -> None:
        validation = self.report["entry_validation"]
        self.assertTrue(validation["passed"])
        self.assertEqual(validation["lc_main"]["address"], EXPECTED_ENTRY)
        self.assertEqual(validation["lc_main"]["entryoff"], "0xf17cc")

        first = validation["first_instruction"]
        self.assertTrue(first["passed"])
        self.assertEqual(first["address"], EXPECTED_ENTRY)
        self.assertEqual(first["mnemonic"], "adr")
        self.assertEqual(first["destination_register"], "x2")
        self.assertEqual(first["resolved_address"], EXPECTED_CALLBACK)

        ui_main = validation["uiapplicationmain"]
        self.assertTrue(ui_main["reached"])
        self.assertEqual(ui_main["symbol"], "_UIApplicationMain")
        self.assertEqual(
            ui_main["control_flow_path"],
            ["0x1000f17cc", "0x1000f17d8"],
        )
        self.assertEqual(ui_main["call_site"], "0x1000f18d8")
        self.assertEqual(ui_main["stub_address"], "0x10023f6dc")
        proof = ui_main["stub_proof"]
        self.assertEqual(
            proof["indirect_symbol"]["symbol"], "_UIApplicationMain"
        )
        self.assertEqual(proof["lazy_pointer_address"], "0x1002947b0")
        self.assertEqual(proof["lazy_bind"]["address"], "0x1002947b0")
        self.assertEqual(proof["lazy_bind"]["symbol"], "_UIApplicationMain")
        self.assertGreaterEqual(len(proof["evidence"]), 3)

    def test_callback_candidate_facts(self) -> None:
        candidates = self.report["candidates"]
        self.assertEqual(len(candidates), 1)
        candidate = candidates[0]
        self.assertEqual(candidate["address"], EXPECTED_CALLBACK)
        self.assertEqual(candidate["end_address"], EXPECTED_CALLBACK_END)
        self.assertEqual(candidate["size"], 584)
        self.assertEqual(candidate["expected_hex"], EXPECTED_CALLBACK_HEX)
        self.assertEqual(candidate["role"], "entry-callback-candidate")
        self.assertEqual(candidate["confidence"], "high")
        self.assertNotIn("name", candidate)
        self.assertTrue(candidate["evidence"])

        calls = candidate["call_targets"]
        self.assertTrue(
            any(call.get("symbol") == "_strcmp" for call in calls)
        )
        self.assertTrue(
            any(call["target"] == "0x10010ed7c" for call in calls)
        )
        strings = {entry["string"] for entry in candidate["string_references"]}
        self.assertTrue({"/F", "/WC", "/WM", "/w", "count is %d", "9198.png", "SWD3"} <= strings)

        fingerprint = candidate["fingerprint"]
        self.assertEqual(
            fingerprint["algorithm"], "sha256(normalized-arm64-v1)"
        )
        self.assertEqual(fingerprint["instruction_count"], 146)
        self.assertIn("adrp x8, <pcrel>", fingerprint["normalized_instructions"])
        self.assertIn("bl <branch>", fingerprint["normalized_instructions"])
        self.assertEqual(len(fingerprint["sha256"]), 64)
        self.assertGreater(candidate["cfg_call_summary"]["basic_block_count"], 1)


class LiveRecoveryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.binary = Path(os.environ.get("SWORD3_BINARY", DEFAULT_BINARY))
        if not cls.binary.is_file():
            raise unittest.SkipTest(f"Sword3 binary not found: {cls.binary}")
        try:
            __import__("capstone")
        except ModuleNotFoundError:
            raise unittest.SkipTest("Python capstone package is not installed")

    def test_output_is_deterministic_and_matches_checked_report(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            first = Path(temporary_directory) / "first.json"
            second = Path(temporary_directory) / "second.json"
            command = [
                sys.executable,
                str(TOOL),
                str(self.binary),
                "--manifest",
                str(MANIFEST),
            ]
            for output in (first, second):
                completed = subprocess.run(
                    command + ["--output", str(output)],
                    cwd=ROOT,
                    check=True,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(completed.stdout, "")
                self.assertEqual(completed.stderr, "")
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(first.read_bytes(), REPORT.read_bytes())

    def test_anchor_regex_reports_xref_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / "anchored.json"
            subprocess.run(
                [
                    sys.executable,
                    str(TOOL),
                    str(self.binary),
                    "--manifest",
                    str(MANIFEST),
                    "--anchor",
                    r"^count is %d$",
                    "--output",
                    str(output),
                ],
                cwd=ROOT,
                check=True,
                capture_output=True,
                text=True,
            )
            anchor = load_json(output)["anchor_search"]
            self.assertEqual(anchor["patterns"], [r"^count is %d$"])
            self.assertEqual(
                anchor["matched_strings"],
                [{"address": "0x100273f6f", "string": "count is %d"}],
            )
            candidate = next(
                item
                for item in anchor["candidates"]
                if item["address"] == EXPECTED_CALLBACK
            )
            self.assertEqual(candidate["confidence"], "high")
            self.assertEqual(
                candidate["evidence"],
                [
                    {
                        "instruction": "0x10002c48c",
                        "string": "count is %d",
                        "string_address": "0x100273f6f",
                        "type": "anchor-string-xref",
                    }
                ],
            )


if __name__ == "__main__":
    unittest.main()
