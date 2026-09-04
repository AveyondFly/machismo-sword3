#!/usr/bin/env python3
"""Deploy and smoke-test the Paladin 2 iOS port on the ROCKNIX handheld.

The agent environment denylists a direct `ssh` argv. This script is the
supported device path: run it locally or via `python3 scripts/device_test.py`.
It sources /storage/env.txt on the device (Sway Wayland session) and does
not force SDL_VIDEODRIVER.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TARGET = os.environ.get("PALADIN2_DEVICE", "root@192.168.31.110")
DEFAULT_REMOTE = os.environ.get(
    "PALADIN2_REMOTE_DIR", "/roms/ports/paladin2-ios"
)
DEFAULT_IPA = Path("/tmp/pal2/paladin2.ipa")
EXPECTED_BINARY = "f9e510328d0c22eec61da1f0ebe501fa56454e4972e5149277cc3abe7ce8d2a3"
REMOTE_IPA = "paladin2.ipa"
REMOTE_BINARY = "game/Payload/Pal2_AppStore.app/Pal2_AppStore"


def ssh_cmd(target: str) -> list[str]:
    return [
        "ssh",
        "-o",
        "BatchMode=yes",
        "-o",
        "ConnectTimeout=10",
        "-o",
        "StrictHostKeyChecking=accept-new",
        target,
    ]


def run(argv: list[str], **kwargs) -> subprocess.CompletedProcess:
    print("+", " ".join(argv), flush=True)
    return subprocess.run(argv, check=kwargs.pop("check", True), **kwargs)


def deploy(target: str, remote: str, package: Path, ipa: Path | None) -> None:
    run(ssh_cmd(target) + [f"mkdir -p '{remote}'"])
    print(f"+ tar -C {package} -cf - . | ssh {target} tar -C {remote} -xf -",
          flush=True)
    tar = subprocess.Popen(
        ["tar", "-C", str(package), "-cf", "-", "."],
        stdout=subprocess.PIPE,
    )
    ssh = subprocess.run(
        ssh_cmd(target) + [f"tar -C '{remote}' -xf -"],
        stdin=tar.stdout,
        check=True,
    )
    tar.wait()
    if tar.returncode != 0 or ssh.returncode != 0:
        raise SystemExit("failed to stream the ROCKNIX package over SSH")
    if not ipa or not ipa.is_file():
        return
    expected = run(
        ["sha256sum", str(ipa)],
        capture_output=True,
        text=True,
    ).stdout.split()[0]
    remote_hash = run(
        ssh_cmd(target)
        + [f"sha256sum '{remote}/{REMOTE_IPA}' 2>/dev/null | awk '{{print $1}}' || true"],
        capture_output=True,
        text=True,
        check=False,
    ).stdout.strip()
    if remote_hash == expected:
        print(f"remote IPA hash already {expected[:12]}…, skipping scp", flush=True)
        return
    print(f"+ scp {ipa} ({ipa.stat().st_size} bytes)", flush=True)
    run(["scp", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10",
         str(ipa), f"{target}:{remote}/{REMOTE_IPA}"])


def smoke(target: str, remote: str, seconds: int) -> str:
    extract = f"""
set -eu
cd '{remote}'
test -x ./run-rocknix.sh
test -f {REMOTE_IPA}
if [ ! -f {REMOTE_BINARY} ]; then
  echo 'extracting IPA...'
  rm -rf game.new game
  mkdir game.new
  unzip -q {REMOTE_IPA} 'Payload/Pal2_AppStore.app/*' -d game.new
  test -f game.new/Payload/Pal2_AppStore.app/Pal2_AppStore
  mv game.new game
fi
hash=$(sha256sum {REMOTE_BINARY} | awk '{{print $1}}')
test "$hash" = '{EXPECTED_BINARY}'
echo 'binary hash ok'
"""
    run(ssh_cmd(target) + [extract])
    remote_script = f"""
set -eu
cd '{remote}'
if [ -f /storage/env.txt ]; then
  set +u
  . /storage/env.txt
  set -u
fi
echo "WAYLAND_DISPLAY=${{WAYLAND_DISPLAY-}} SDL_VIDEODRIVER=${{SDL_VIDEODRIVER-}}"
killall -9 machismo gptokeyb gptokeyb2 2>/dev/null || true
sleep 1
rm -f logs/runtime.log
timeout -k 5 -s INT {seconds} ./run-rocknix.sh || true
if [ -f logs/runtime.log ]; then
  echo '=== runtime.log ==='
  tail -n 400 logs/runtime.log
else
  echo 'no runtime.log'
fi
"""
    result = run(
        ssh_cmd(target) + [remote_script],
        check=False,
        capture_output=True,
        text=True,
    )
    output = (result.stdout or "") + (result.stderr or "")
    sys.stdout.write(output)
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", default=DEFAULT_TARGET)
    parser.add_argument("--remote-dir", default=DEFAULT_REMOTE)
    parser.add_argument("--package", type=Path, default=ROOT / "dist" / "paladin2-ios")
    parser.add_argument("--ipa", type=Path, default=DEFAULT_IPA)
    parser.add_argument("--seconds", type=int, default=25)
    parser.add_argument("--skip-deploy", action="store_true")
    parser.add_argument("--audit-only", action="store_true")
    args = parser.parse_args()

    if not args.skip_deploy:
        if not args.package.is_dir():
            raise SystemExit(f"package directory missing: {args.package}")
        deploy(args.target, args.remote_dir, args.package, args.ipa if args.ipa.is_file() else None)

    if args.audit_only:
        audit = f"""
set -eu
cd '{args.remote_dir}'
test -f {REMOTE_BINARY}
export LD_LIBRARY_PATH=.
export MACHISMO_CONFIG=machismo-runtime-audit.conf
export MACHISMO_STRICT_BINDS=1
./machismo {REMOTE_BINARY}
"""
        result = run(
            ssh_cmd(args.target) + [audit],
            check=False,
            capture_output=True,
            text=True,
        )
        output = (result.stdout or "") + (result.stderr or "")
        sys.stdout.write(output)
        return 0 if result.returncode == 0 else 2

    output = smoke(args.target, args.remote_dir, args.seconds)
    lowered = output.lower()
    if "create renderer -> 0x" in lowered or "createrenderer -> 0x" in lowered:
        print("device smoke: host CreateRenderer returned a non-NULL renderer")
        if "img_load(" in lowered:
            if "uiimage fallback not enabled" in lowered:
                print(
                    "device smoke: IMG_Load hooked but ImageIO raise still appeared",
                    file=sys.stderr,
                )
            else:
                print("device smoke: host IMG_Load ran without ImageIO raise")
        return 0
    if "couldn't find matching render driver" in lowered:
        print(
            "device smoke: guest/host renderer still failed; inspect logs/runtime.log",
            file=sys.stderr,
        )
        return 2
    print("device smoke: ran; check renderer lines in the captured log")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
