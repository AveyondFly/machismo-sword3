#!/usr/bin/env python3
"""Deploy and smoke-test the extracted NewPAL iOS game on ROCKNIX."""

from __future__ import annotations

import argparse
import hashlib
import posixpath
import shlex
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_BINARY = "2056c5b0a921088f96302ce7562884fcb2b7383652912995c3946c104893a0d8"
REMOTE_BINARY = "game/Payload/NewPAL.app/NewPAL"
RUNTIME_FILES = (
    "machismo",
    "libsystem_shim.so",
    "libsword3_ios_shim.so",
    "libsword3_objc_shim.so",
    "libsword3_gl_bridge.so",
    "libsword3_host.so",
    "libc++.so.1",
    "libc++abi.so.1",
)


def ssh_cmd(target: str) -> list[str]:
    return [
        "ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10",
        "-o", "StrictHostKeyChecking=accept-new", target,
    ]


def run(argv: list[str], **kwargs: object) -> subprocess.CompletedProcess:
    print("+", " ".join(argv), flush=True)
    return subprocess.run(argv, check=True, **kwargs)


def deploy(target: str, remote: str, package: Path) -> None:
    launcher = package.parent / "newpal.sh"
    remote_launcher = posixpath.join(posixpath.dirname(remote), "newpal.sh")
    if not package.is_dir() or not launcher.is_file():
        raise SystemExit("run scripts/package-newpal-rocknix.sh first")
    run(ssh_cmd(target) + [f"mkdir -p '{remote}'"])
    print(f"+ tar {package} | ssh {target}:{remote}", flush=True)
    tar = subprocess.Popen(
        ["tar", "-C", str(package), "-cf", "-", "."],
        stdout=subprocess.PIPE,
    )
    assert tar.stdout is not None
    received = subprocess.run(
        ssh_cmd(target) + [f"tar -C '{remote}' -xf -"],
        stdin=tar.stdout,
        check=False,
    )
    tar.stdout.close()
    tar.wait()
    if tar.returncode or received.returncode:
        raise SystemExit("runtime transfer failed")
    run(["scp", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10",
         str(launcher), f"{target}:{remote_launcher}.new"])
    run(ssh_cmd(target) + [
        f"chmod 755 '{remote_launcher}.new' && "
        f"mv -f '{remote_launcher}.new' '{remote_launcher}'"
    ])


def verify_runtime(target: str, remote: str, package: Path) -> None:
    """Confirm deployed libraries came from this branch's build package."""
    local_hashes = {}
    for name in RUNTIME_FILES:
        path = package / name
        if not path.is_file():
            raise SystemExit(f"missing packaged runtime: {path}")
        local_hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    for name in ("dylib_map.conf", "address-hooks.conf",
                 "machismo-partial-sdl.conf"):
        contents = (package / name).read_text()
        if "/ref/" in contents or "libnewpal_" in contents:
            raise SystemExit(f"reference library in packaged config: {name}")
    paths = " ".join(shlex.quote(posixpath.join(remote, name))
                     for name in RUNTIME_FILES)
    result = run(ssh_cmd(target) + [f"sha256sum {paths}"],
                 capture_output=True, text=True)
    remote_hashes = {
        posixpath.basename(path): digest
        for digest, path in (line.split(maxsplit=1)
                             for line in result.stdout.splitlines())
    }
    if remote_hashes != local_hashes:
        raise SystemExit("device runtime differs from this branch's package")
    print("NewPAL runtime hashes match this branch's package", flush=True)


def inspect(target: str, remote: str, audit_only: bool, seconds: int) -> int:
    launcher = posixpath.join(posixpath.dirname(remote), "newpal.sh")
    prefix = f"""
set -eu
cd '{remote}'
test -f '{REMOTE_BINARY}'
hash=$(sha256sum '{REMOTE_BINARY}' | awk '{{print $1}}')
test "$hash" = '{EXPECTED_BINARY}'
echo 'NewPAL binary hash ok'
"""
    if audit_only:
        script = prefix + f"""
export LD_LIBRARY_PATH=.
export MACHISMO_CONFIG=machismo-runtime-audit.conf
export MACHISMO_STRICT_BINDS=1
./machismo '{REMOTE_BINARY}'
"""
    else:
        script = prefix + f"""
# The launcher inherits the ROCKNIX Sway environment from this caller.
if [ -f /storage/env.txt ]; then
    set +u
    . /storage/env.txt
    set -u
fi
echo "WAYLAND_DISPLAY=${{WAYLAND_DISPLAY-}} XDG_RUNTIME_DIR=${{XDG_RUNTIME_DIR-}}"
log_size=0
if [ -f logs/runtime.log ]; then log_size=$(wc -c < logs/runtime.log); fi
set +e
NEWPAL_GAME_DIR='{remote}' timeout -k 5 -s INT {seconds} '{launcher}'
status=$?
set -e
echo "NewPAL launcher exit: $status"
if [ -f logs/runtime.log ]; then
    tail -c +$((log_size + 1)) logs/runtime.log | tail -n 100
fi
if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then exit "$status"; fi
"""
    result = subprocess.run(
        ssh_cmd(target) + [script],
        capture_output=True, text=True, check=False, timeout=seconds + 45,
    )
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", default="root@192.168.31.224")
    parser.add_argument("--remote-dir", default="/roms/ports/newpal-ios")
    parser.add_argument("--package", type=Path, default=ROOT / "dist/newpal-ios")
    parser.add_argument("--seconds", type=int, default=25)
    parser.add_argument("--skip-deploy", action="store_true")
    parser.add_argument("--audit-only", action="store_true")
    args = parser.parse_args()
    if not args.skip_deploy:
        deploy(args.target, args.remote_dir, args.package)
    verify_runtime(args.target, args.remote_dir, args.package)
    return inspect(args.target, args.remote_dir, args.audit_only, args.seconds)


if __name__ == "__main__":
    raise SystemExit(main())
