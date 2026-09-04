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
        self.assertIn('__asm__("CGRectZero")', generated)
        self.assertIn('__asm__("AVAudioSessionCategoryAmbient")', generated)

        self.assertNotIn("OBJC_CLASS_$_NSException", generated)
        self.assertNotIn("OBJC_CLASS_$_NSDictionary", generated)
        self.assertNotIn("OBJC_CLASS_$_NSString", generated)
        self.assertNotIn("OBJC_CLASS_$_NSArray", generated)
        self.assertNotIn("OBJC_CLASS_$_NSMutableArray", generated)
        self.assertNotIn("OBJC_CLASS_$_NSNumber", generated)
        self.assertNotIn("OBJC_CLASS_$_NSData", generated)
        self.assertNotIn("OBJC_CLASS_$_NSDate", generated)
        self.assertNotIn("OBJC_CLASS_$_NSDateFormatter", generated)
        self.assertNotIn("OBJC_CLASS_$_NSCalendar", generated)
        self.assertNotIn("NSFileModificationDate", generated)
        self.assertNotIn("OBJC_METACLASS_$_UIWindow", generated)
        self.assertNotIn("OBJC_CLASS_$_UIView", generated)
        self.assertNotIn("OBJC_CLASS_$_NSURL", generated)
        self.assertNotIn("OBJC_CLASS_$_NSNotificationCenter", generated)
        self.assertNotIn("OBJC_CLASS_$_AVPlayerLayer", generated)
        self.assertNotIn("OBJC_CLASS_$_AVAudioPlayer", generated)
        self.assertNotIn("OBJC_CLASS_$_CADisplayLink", generated)
        self.assertNotIn("OBJC_CLASS_$_NSRunLoop", generated)
        self.assertNotIn("kCFRunLoopDefaultMode", generated)
        self.assertNotIn("NSDefaultRunLoopMode", generated)
        self.assertNotIn('__asm__("CFRunLoopGetCurrent")', generated)
        self.assertNotIn('__asm__("CFRunLoopRunInMode")', generated)
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
        self.assertIn(" OBJC_CLASS_$_NSArray\n", symbols)
        self.assertIn(" OBJC_CLASS_$_NSMutableArray\n", symbols)
        self.assertIn(" OBJC_CLASS_$_NSNumber\n", symbols)
        self.assertIn(" OBJC_CLASS_$_NSData\n", symbols)
        self.assertIn(" OBJC_CLASS_$_NSDate\n", symbols)
        self.assertIn(" OBJC_CLASS_$_NSDateFormatter\n", symbols)
        self.assertIn(" OBJC_CLASS_$_NSCalendar\n", symbols)
        self.assertIn(" OBJC_CLASS_$_UIApplication\n", symbols)
        self.assertIn(" OBJC_CLASS_$_UIWindow\n", symbols)
        self.assertIn(" OBJC_METACLASS_$_UIWindow\n", symbols)
        self.assertIn(" OBJC_CLASS_$_UIView\n", symbols)
        self.assertIn(" OBJC_CLASS_$_NSURL\n", symbols)
        self.assertIn(" OBJC_CLASS_$_NSNotificationCenter\n", symbols)
        self.assertIn(" OBJC_CLASS_$_AVPlayer\n", symbols)
        self.assertIn(" OBJC_CLASS_$_AVPlayerLayer\n", symbols)
        self.assertIn(" OBJC_CLASS_$_AVAudioPlayer\n", symbols)
        self.assertIn(" OBJC_CLASS_$_CADisplayLink\n", symbols)
        self.assertIn(" OBJC_CLASS_$_NSRunLoop\n", symbols)
        self.assertIn(" CFRunLoopGetCurrent\n", symbols)
        self.assertIn(" CFRunLoopRunInMode\n", symbols)
        self.assertIn(" kCFRunLoopDefaultMode\n", symbols)
        self.assertIn(" NSDefaultRunLoopMode\n", symbols)
        self.assertIn(" NSFileModificationDate\n", symbols)
        self.assertIn(" sword3_ios_tick_display_links\n", symbols)
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
        library.CFRunLoopGetCurrent.restype = ctypes.c_void_p
        self.assertTrue(library.CFRunLoopGetCurrent())
        library.CFRunLoopRunInMode.restype = ctypes.c_int32
        library.CFRunLoopRunInMode.argtypes = [
            ctypes.c_void_p,
            ctypes.c_double,
            ctypes.c_ubyte,
        ]
        self.assertEqual(library.CFRunLoopRunInMode(None, 0.0, 0), 3)
        self.assertTrue(
            ctypes.c_void_p.in_dll(library, "kCFRunLoopDefaultMode").value
        )
        self.assertNotEqual(
            ctypes.c_uint64.in_dll(library, "OBJC_CLASS_$_CADisplayLink").value,
            0,
        )
        self.assertNotEqual(
            ctypes.c_uint64.in_dll(library, "OBJC_CLASS_$_NSRunLoop").value,
            0,
        )
        self.assertNotEqual(
            ctypes.c_uint64.in_dll(library, "OBJC_CLASS_$_NSDate").value,
            0,
        )
        self.assertNotEqual(
            ctypes.c_uint64.in_dll(library, "OBJC_CLASS_$_NSCalendar").value,
            0,
        )
        self.assertNotEqual(
            ctypes.c_uint64.in_dll(library, "OBJC_CLASS_$_NSData").value,
            0,
        )
        self.assertTrue(
            ctypes.c_void_p.in_dll(library, "NSFileModificationDate").value
        )

    def test_document_and_cache_search_paths_are_split(self) -> None:
        data_dir = Path(self.temporary_directory.name) / "saves"
        tmpdir = Path(self.temporary_directory.name) / "tmp"
        data_dir.mkdir()
        tmpdir.mkdir()
        os.environ["SWORD3_DATA_DIR"] = str(data_dir)
        os.environ["TMPDIR"] = str(tmpdir)
        self.addCleanup(os.environ.pop, "SWORD3_DATA_DIR", None)
        self.addCleanup(os.environ.pop, "TMPDIR", None)

        library = ctypes.CDLL(str(self.shared_object))
        library.sword3_test_search_path.argtypes = [
            ctypes.c_ulong,
            ctypes.c_char_p,
            ctypes.c_uint32,
        ]
        library.sword3_test_search_path.restype = ctypes.c_int
        library.sword3_test_keep_save_path.argtypes = [ctypes.c_char_p]
        library.sword3_test_keep_save_path.restype = ctypes.c_int
        library.sword3_test_remove_path.argtypes = [ctypes.c_char_p]
        library.sword3_test_remove_path.restype = ctypes.c_int

        buf = ctypes.create_string_buffer(4096)
        self.assertEqual(library.sword3_test_search_path(9, buf, 4096), 0)
        self.assertEqual(buf.value.decode(), str(data_dir))
        self.assertEqual(library.sword3_test_search_path(13, buf, 4096), 0)
        cache_dir = tmpdir / "ns-search-13"
        self.assertEqual(buf.value.decode(), str(cache_dir))
        self.assertTrue(cache_dir.is_dir())

        save = data_dir / "PAL2_001.sav"
        junk = cache_dir / "stringdb.tmp"
        save.write_bytes(b"PAL2")
        junk.write_bytes(b"tmp")
        self.assertEqual(
            library.sword3_test_keep_save_path(str(save).encode()), 1
        )
        self.assertEqual(
            library.sword3_test_keep_save_path(str(junk).encode()), 0
        )
        self.assertEqual(library.sword3_test_remove_path(str(save).encode()), 1)
        self.assertEqual(library.sword3_test_remove_path(str(junk).encode()), 1)
        self.assertTrue(save.is_file())
        self.assertFalse(junk.exists())

    def test_nsdata_loads_url_bytes_from_file_and_bundle(self) -> None:
        bundle = Path(self.temporary_directory.name) / "bundle"
        music = bundle / "MusicFile"
        music.mkdir(parents=True)
        payload = b"OggS\x00pal2-audio-test"
        direct = Path(self.temporary_directory.name) / "direct.mp3"
        planted = music / "Empty.mp3"
        direct.write_bytes(payload)
        planted.write_bytes(payload)
        os.environ["SWORD3_BUNDLE_DIR"] = str(bundle)
        self.addCleanup(os.environ.pop, "SWORD3_BUNDLE_DIR", None)

        library = ctypes.CDLL(str(self.shared_object))
        library.sword3_test_nsdata_url_bytes.argtypes = [
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_uint32,
        ]
        library.sword3_test_nsdata_url_bytes.restype = ctypes.c_int
        buf = ctypes.create_string_buffer(64)
        self.assertEqual(
            library.sword3_test_nsdata_url_bytes(str(direct).encode(), buf, 64),
            len(payload),
        )
        self.assertEqual(buf.raw[: len(payload)], payload)
        self.assertEqual(
            library.sword3_test_nsdata_url_bytes(b"file://" + str(direct).encode(), buf, 64),
            len(payload),
        )
        self.assertEqual(buf.raw[: len(payload)], payload)
        self.assertEqual(
            library.sword3_test_nsdata_url_bytes(b"Empty.mp3", buf, 64),
            len(payload),
        )
        self.assertEqual(buf.raw[: len(payload)], payload)

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
