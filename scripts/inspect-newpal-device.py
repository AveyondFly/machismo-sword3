#!/usr/bin/env python3
"""Read-only inventory of the newpal-ios files on a ROCKNIX device."""

from __future__ import annotations

import argparse
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", default="root@192.168.31.224")
    parser.add_argument("--remote-dir", default="/roms/ports/newpal-ios")
    parser.add_argument("--binary-output", type=Path,
                        help="copy the NewPAL executable to this local path")
    args = parser.parse_args()

    remote = shlex.quote(args.remote_dir)
    binary = f"{args.remote_dir.rstrip('/')}/game/Payload/NewPAL.app/NewPAL"
    quoted_binary = shlex.quote(binary)
    script = f"""
set -eu
remote={remote}
if [ ! -d "$remote" ]; then
    echo "Directory not found: $remote" >&2
    exit 2
fi
echo '=== Top level ==='
ls -la "$remote"
echo '=== Executable ==='
ls -lh {quoted_binary}
sha256sum {quoted_binary}
if command -v file >/dev/null 2>&1; then file {quoted_binary}; fi
echo '=== App bundles ==='
find "$remote" -maxdepth 5 -type d -name '*.app' | head -30
echo '=== gptokeyb ==='
for gptk in /storage/.config/gptokeyb/gptokeyb /storage/.config/gptokeyb/gptokeyb2; do
    if [ -x "$gptk" ]; then ls -l "$gptk"; fi
done
if command -v gptokeyb >/dev/null 2>&1; then command -v gptokeyb; fi
if command -v gptokeyb2 >/dev/null 2>&1; then command -v gptokeyb2; fi
if [ -f /storage/.config/gptokeyb/control.ini ]; then
    sed -n '1,60p' /storage/.config/gptokeyb/control.ini
fi
"""
    command = [
        "ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10",
        "-o", "StrictHostKeyChecking=accept-new", args.target, "sh", "-s",
    ]
    try:
        result = subprocess.run(command, input=script, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f"device inspection failed: {error}", file=sys.stderr)
        return 2
    if result.returncode or args.binary_output is None:
        return result.returncode

    output = args.binary_output
    output.parent.mkdir(parents=True, exist_ok=True)
    temp_path = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as stream:
            temp_path = Path(stream.name)
            copy = subprocess.run(
                command[:-2] + [f"cat {quoted_binary}"],
                stdout=stream, timeout=120,
            )
        if copy.returncode:
            print(f"binary copy failed (exit {copy.returncode})", file=sys.stderr)
            return 2
        os.replace(temp_path, output)
        print(f"Copied executable to {output}")
        return 0
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f"binary copy failed: {error}", file=sys.stderr)
        return 2
    finally:
        if temp_path is not None:
            temp_path.unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
