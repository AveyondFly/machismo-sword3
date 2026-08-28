#!/usr/bin/env python3
"""Host-side tests for the generated Sword3 iOS import boundary."""

from __future__ import annotations

import ctypes
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "tools" / "generate_sword3_shim.py"
MANIFEST = ROOT / "configs" / "sword3" / "binary-manifest.json"
SHIM_C = ROOT / "src" / "shim" / "sword3_ios_shim.c"
SHIM_INCLUDE = ROOT / "src" / "shim"


class Sword3IosShimTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.compiler = shutil.which(os.environ.get("CC", "cc"))
        if cls.compiler is None:
            raise unittest.SkipTest("host C compiler is unavailable")

        cls.temporary_directory = tempfile.TemporaryDirectory()
        temporary_path = Path(cls.temporary_directory.name)
        cls.generated = temporary_path / "sword3_ios_imports.c"
        cls.shared_object = temporary_path / "libsword3_ios_shim.so"

        subprocess.run(
            [
                sys.executable,
                str(GENERATOR),
                "--manifest",
                str(MANIFEST),
                "--output",
                str(cls.generated),
            ],
            cwd=ROOT,
            check=True,
        )
        cls.generated_text = cls.generated.read_text(encoding="utf-8")

        subprocess.run(
            [
                cls.compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fPIC",
                "-shared",
                "-DSWORD3_SHIM_ENABLE_TEST_API",
                "-I",
                str(SHIM_INCLUDE),
                str(cls.generated),
                str(SHIM_C),
                "-o",
                str(cls.shared_object),
            ],
            cwd=ROOT,
            check=True,
        )

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary_directory.cleanup()

    def test_generated_import_classes_and_exclusions(self) -> None:
        generated = self.generated_text
        self.assertIn('__asm__("UIApplicationMain")', generated)
        self.assertIn('__asm__("OBJC_CLASS_$_UIApplication")', generated)
        self.assertIn('__asm__("OBJC_METACLASS_$_UIWindow")', generated)
        self.assertIn('__asm__("CGRectZero")', generated)
        self.assertIn('__asm__("AVAudioSessionCategoryAmbient")', generated)

        self.assertNotIn("OBJC_CLASS_$_NSException", generated)
        self.assertNotIn("OBJC_CLASS_$_NSDictionary", generated)
        self.assertNotIn("OBJC_CLASS_$_NSString", generated)
        self.assertNotIn("glActiveTexture", generated)
        self.assertNotIn("OBJC_CLASS_$_EAGLContext", generated)
        self.assertNotIn("malloc", generated)
        self.assertNotIn("objc_msgSend", generated)
        self.assertNotIn("inflate", generated)

    def test_generation_is_deterministic(self) -> None:
        second_output = Path(self.temporary_directory.name) / "second.c"
        subprocess.run(
            [
                sys.executable,
                str(GENERATOR),
                "--manifest",
                str(MANIFEST),
                "--output",
                str(second_output),
            ],
            cwd=ROOT,
            check=True,
        )
        self.assertEqual(self.generated.read_bytes(), second_output.read_bytes())

    def test_shared_object_loads_and_exports_exact_elf_names(self) -> None:
        symbols = subprocess.run(
            ["nm", "-D", "--defined-only", str(self.shared_object)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        self.assertIn(" UIApplicationMain\n", symbols)
        self.assertIn(" OBJC_CLASS_$_NSException\n", symbols)
        self.assertIn(" OBJC_CLASS_$_NSDictionary\n", symbols)
        self.assertIn(" OBJC_CLASS_$_NSString\n", symbols)
        self.assertIn(" OBJC_CLASS_$_UIApplication\n", symbols)
        self.assertIn(" OBJC_METACLASS_$_UIWindow\n", symbols)
        self.assertIn(" CGRectZero\n", symbols)
        self.assertNotIn(" glActiveTexture\n", symbols)

        library = ctypes.CDLL(str(self.shared_object))
        self.assertEqual(library.sword3_unsupported_call_count(), 0)
        self.assertEqual(
            ctypes.c_ubyte.in_dll(
                library, "OBJC_CLASS_$_UIApplication"
            ).value,
            0,
        )
        self.assertEqual(ctypes.c_ubyte.in_dll(library, "CGRectZero").value, 0)

    def test_function_wrapper_logs_and_aborts(self) -> None:
        script = (
            "import ctypes, resource;"
            "resource.setrlimit(resource.RLIMIT_CORE, (0, 0));"
            f"library=ctypes.CDLL({str(self.shared_object)!r});"
            "library.UIApplicationMain()"
        )
        completed = subprocess.run(
            [sys.executable, "-c", script],
            capture_output=True,
            text=True,
        )
        self.assertEqual(completed.returncode, -signal.SIGABRT)
        self.assertIn(
            "[sword3-ios-shim] unsupported Apple symbol: UIApplicationMain",
            completed.stderr,
        )


if __name__ == "__main__":
    unittest.main()
