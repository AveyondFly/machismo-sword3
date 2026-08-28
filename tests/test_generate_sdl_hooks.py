#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(
    os.environ.get(
        "SWORD3_BINARY", "/tmp/sword3-ipa-extract/Payload/SWD3.app/SWD3"
    )
)


class GeneratedSdlHooksTest(unittest.TestCase):
    def test_checked_hook_set(self) -> None:
        report = json.loads(
            (ROOT / "configs/sword3/sdl-hook-report.json").read_text()
        )
        self.assertEqual(report["coverage"], "partial")
        self.assertGreaterEqual(report["hook_count"], 30)
        symbols = {hook["symbol"] for hook in report["hooks"]}
        self.assertIn("SDL_InitSubSystem", symbols)
        self.assertIn("SDL_VideoInit", symbols)
        self.assertIn("SDL_VideoQuit", symbols)
        self.assertIn("SDL_AudioInit", symbols)
        self.assertIn("SDL_AudioQuit", symbols)
        self.assertIn("SDL_RWFromFile", symbols)
        self.assertIn("SDL_CreateWindow", symbols)
        self.assertIn("SDL_CreateRenderer", symbols)
        self.assertIn("SDL_DestroyWindow", symbols)
        self.assertIn("SDL_DestroyRenderer", symbols)
        self.assertIn("SDL_RenderPresent", symbols)
        self.assertIn("SDL_RenderCopyF", symbols)
        self.assertIn("SDL_RenderCopyExF", symbols)
        self.assertIn("SDL_PeepEvents", symbols)
        self.assertIn("SDL_PumpEvents", symbols)
        self.assertIn("SDL_PollEvent", symbols)
        self.assertIn("SDL_WaitEventTimeout", symbols)
        self.assertIn("IMG_Load", symbols)
        self.assertIn("IMG_Load_RW", symbols)
        self.assertIn("IMG_LoadTyped_RW", symbols)
        self.assertIn("SDL_OpenAudioDevice", symbols)
        self.assertIn("SDL_PauseAudioDevice", symbols)
        self.assertIn("SDL_LockAudioDevice", symbols)
        self.assertIn("SDL_UnlockAudioDevice", symbols)
        self.assertIn("SDL_CloseAudioDevice", symbols)
        self.assertIn("SDL_IsGameController", symbols)
        self.assertNotIn("UIGamePad_Update", symbols)
        self.assertEqual(report["schema_version"], 2)

    @unittest.skipUnless(BINARY.is_file(), "Sword3 binary is unavailable")
    def test_generation_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "hooks.conf"
            report = Path(temporary) / "report.json"
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools/generate_sdl_address_hooks.py"),
                    str(BINARY),
                    "--output",
                    str(output),
                    "--report",
                    str(report),
                ],
                cwd=ROOT,
                check=True,
            )
            self.assertEqual(
                output.read_bytes(),
                (ROOT / "configs/sword3/address-hooks.conf").read_bytes(),
            )
            self.assertEqual(
                report.read_bytes(),
                (ROOT / "configs/sword3/sdl-hook-report.json").read_bytes(),
            )


if __name__ == "__main__":
    unittest.main()
