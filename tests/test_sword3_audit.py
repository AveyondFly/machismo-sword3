#!/usr/bin/env python3
"""Regression tests for the read-only Sword3 Mach-O audit."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "sword3_audit.py"
MANIFEST = ROOT / "configs" / "sword3" / "binary-manifest.json"
DEFAULT_BINARY = Path("/tmp/sword3-ipa-extract/Payload/SWD3.app/SWD3")
EXPECTED_SHA256 = "268d6f40eac47718ee2cac6acd34912421546eb8a58056d310c0b42d2655e36b"
EXPECTED_UUID = "7C40879F-22A3-3F14-8486-F37FA0AA96F3"


def assert_stable_facts(test: unittest.TestCase, audit: dict) -> None:
    test.assertEqual(audit["schema_version"], 1)
    test.assertEqual(audit["mode"], "audit-only")
    test.assertEqual(audit["file"]["sha256"], EXPECTED_SHA256)
    test.assertEqual(audit["uuid"], EXPECTED_UUID)

    macho = audit["macho"]
    test.assertEqual(macho["magic"], "0xfeedfacf")
    test.assertEqual(macho["bits"], 64)
    test.assertEqual(macho["endianness"], "little")
    test.assertEqual(macho["cpu"]["name"], "arm64")
    test.assertEqual(macho["cpu"]["type"], 0x0100000C)
    test.assertEqual(macho["filetype"]["name"], "MH_EXECUTE")

    test.assertEqual(audit["encryption_info_64"]["cryptid"], 0)
    test.assertEqual(
        audit["lc_main"], {"entryoff": 989132, "stacksize": 0}
    )
    test.assertEqual(len(audit["dylibs"]), 20)

    symtab = audit["symtab"]
    test.assertEqual(symtab["nsyms"], 565)
    test.assertEqual(symtab["undefined_count"], 536)
    test.assertEqual(len(symtab["undefined_symbols"]), 536)
    test.assertEqual(symtab["defined_count"], 28)
    test.assertEqual(len(symtab["defined_symbols"]), 28)
    test.assertGreater(audit["function_starts"]["count"], 0)
    test.assertTrue(audit["function_starts"]["terminated"])
    test.assertEqual(
        audit["function_starts"]["count"],
        len(audit["function_starts"]["entries"]),
    )

    test.assertTrue(audit["segments"])
    test.assertTrue(any(segment["sections"] for segment in audit["segments"]))
    dyld = audit["dyld_info_only"]
    test.assertGreater(dyld["rebase"]["rebase_count"], 0)
    for kind in ("regular", "weak", "lazy"):
        test.assertGreater(dyld["bind"][kind]["bind_count"], 0)
        test.assertTrue(dyld["bind"][kind]["by_dylib"])
        test.assertTrue(dyld["bind"][kind]["by_symbol"])
    test.assertGreater(audit["mod_init_func"]["count"], 0)
    test.assertGreater(audit["objc"]["metadata_counts"]["__objc_classlist"], 0)
    test.assertGreater(audit["unwind"]["personality_count"], 0)


class CheckedManifestTest(unittest.TestCase):
    def test_manifest_stable_facts(self) -> None:
        with MANIFEST.open(encoding="utf-8") as manifest_file:
            assert_stable_facts(self, json.load(manifest_file))


class LiveAuditTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.binary = Path(os.environ.get("SWORD3_BINARY", DEFAULT_BINARY))
        if not cls.binary.is_file():
            raise unittest.SkipTest(f"Sword3 binary not found: {cls.binary}")

    def test_audit_only_output_matches_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / "audit.json"
            completed = subprocess.run(
                [
                    sys.executable,
                    str(TOOL),
                    "--audit-only",
                    str(self.binary),
                    "--output",
                    str(output),
                ],
                cwd=ROOT,
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertEqual(completed.stdout, "")
            self.assertEqual(completed.stderr, "")
            with output.open(encoding="utf-8") as output_file:
                generated = json.load(output_file)
            with MANIFEST.open(encoding="utf-8") as manifest_file:
                checked_in = json.load(manifest_file)
            assert_stable_facts(self, generated)
            self.assertEqual(generated, checked_in)


if __name__ == "__main__":
    unittest.main()
