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


def read_trainer_log(target: str, remote: str) -> int:
    log = shlex.quote(posixpath.join(remote, "logs/runtime.log"))
    command = (f"grep -E 'NewPAL trainer|runtime hooks installed|hook mismatch' "
               f"{log} | tail -n 200")
    result = subprocess.run(
        ssh_cmd(target) + [command],
        capture_output=True, text=True, check=False, timeout=20,
    )
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    return result.returncode


def process_status(target: str) -> int:
    result = subprocess.run(
        ssh_cmd(target) + ["pgrep -af machismo || true"],
        capture_output=True, text=True, check=False, timeout=20,
    )
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    return result.returncode


def snapshot_actors(target: str) -> int:
    script = '''
import os, struct, subprocess
pids = subprocess.check_output(
    ['pgrep', '-f', '^(/storage)?/roms/ports/newpal-ios/machismo '], text=True).split()
if not pids:
    raise SystemExit('NewPAL is not running')
pid = int(pids[0])
fd = os.open(f'/proc/{pid}/mem', os.O_RDONLY)
def read(address, size):
    return os.pread(fd, size, address)
def u64(address):
    return struct.unpack('<Q', read(address, 8))[0]
def u32(address):
    return struct.unpack('<I', read(address, 4))[0]
print('pid', pid, 'scene', u32(0x1001d000c), 'request', u32(0x1001d0010),
      'event', u32(0x1001f3f30), 'tile-x', u32(0x1001f3f34))
script_data = u64(0x1001f5928)
print('script data', hex(script_data))
if script_data:
    data = read(script_data, 1 << 20)
    for offset in range(0, len(data) - 6, 2):
        opcode, request = struct.unpack_from('<HH', data, offset)
        if opcode == 0x309 and request in (2001, 2002, 2003, 2343):
            start = max(0, offset - 24)
            end = min(len(data), offset + 28)
            print(' battle script offset', hex(offset), 'request', request,
                  'near', data[start:end].hex())
map_count = u32(0x1001d0528)
print('map objects', map_count)
for index in range(min(map_count, 32)):
    address = 0x1001f8810 + index * 0x68
    data = read(address, 0x68)
    words = struct.unpack('<24i', data[:96])
    matches = [(offset, value) for offset, value in
               enumerate(struct.unpack('<52H', data))
               if value in (2001, 2002, 2003, 2343)]
    print(' map', index, words[:16], 'request matches', matches)
    if 16 <= index <= 27:
        print('  halfwords', struct.unpack('<52H', data))
for category in range(10):
    listing = u64(0x10023a000 + category * 8)
    if not listing:
        continue
    count = u32(listing + 8)
    if not 0 < count < 100:
        continue
    print('category', category, 'count', count, 'list', hex(listing))
    node = u64(listing)
    for _ in range(min(count, 24)):
        if not node:
            break
        role = u64(node + 8)
        if role:
            words = struct.unpack('<32i', read(role, 128))
            print(' role', words[0], 'ptr', hex(role), 'words', words[:24])
        node = u64(node + 0x20)
os.close(fd)
'''
    result = subprocess.run(
        ssh_cmd(target) + ["python3 -"], input=script,
        capture_output=True, text=True, check=False, timeout=20,
    )
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    return result.returncode


def sample_script(target: str) -> int:
    script = '''
import os, struct, subprocess, time
pid = int(subprocess.check_output(
    ['pgrep', '-f', '^(/storage)?/roms/ports/newpal-ios/machismo '], text=True).split()[0])
fd = os.open(f'/proc/{pid}/mem', os.O_RDONLY)
last = None
deadline = time.monotonic() + 5
samples = 0
while time.monotonic() < deadline:
    words = struct.unpack('<12i', os.pread(fd, 48, 0x10022c810))
    request = struct.unpack('<i', os.pread(fd, 4, 0x1001d0010))[0]
    marker = struct.unpack('<2i', os.pread(fd, 8, 0x1001f3f30))
    state = (words, request, marker)
    if state != last and (request != -1 or words[0] != -1 or
                          marker != (10, 16)) and samples < 30:
        print('script', words, 'request', request, 'marker', marker,
              flush=True)
        samples += 1
    last = state
os.close(fd)
'''
    result = subprocess.run(
        ssh_cmd(target) + ["python3 -"], input=script,
        capture_output=True, text=True, check=False, timeout=20,
    )
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    return result.returncode


def trace_mode(target: str) -> int:
    """Read scene mode and field script transitions from a running game."""
    script = '''
import os, struct, subprocess, time
pid = int(subprocess.check_output(
    ['pgrep', '-f', '^(/storage)?/roms/ports/newpal-ios/machismo '], text=True).split()[0])
fd = os.open(f'/proc/{pid}/mem', os.O_RDONLY)
def read(address, size):
    return os.pread(fd, size, address)
def u32(address):
    return struct.unpack('<I', read(address, 4))[0]
last = None
deadline = time.monotonic() + 40
while time.monotonic() < deadline:
    source = struct.unpack('<H', read(0x10022c810 + 0x2c, 2))[0]
    offset = struct.unpack('<H', read(0x10022c810 + 0x28, 2))[0]
    mode = u32(0x10026d214)
    request = u32(0x1001d0010)
    state = (source, offset, mode, request)
    if state != last:
        print(f'{time.monotonic():.3f} source={source} offset={offset} '
              f'mode={mode} request={request}', flush=True)
        last = state
    time.sleep(0.002)
os.close(fd)
'''
    result = subprocess.run(
        ssh_cmd(target) + ["python3 -"], input=script,
        capture_output=True, text=True, check=False, timeout=55,
    )
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    return result.returncode


def attach_mode_watch(target: str) -> int:
    """Attach GDB to log scene-mode writes while the user plays."""
    script = r'''
import pathlib, subprocess, time
pid = int(subprocess.check_output(
    ['pgrep', '-f', '^(/storage)?/roms/ports/newpal-ios/machismo '], text=True).split()[0])
commands = r''' + '"""' + r'''
set pagination off
set confirm off
watch *(int*)0x10026d214
set $hits = 0
commands 1
silent
set $hits = $hits + 1
printf "MODE_TRACE hit=%d mode=%d source=%u offset=%u request=%d pc=%p\n", $hits, *(int*)0x10026d214, *(unsigned short*)0x10022c83c, *(unsigned short*)0x10022c838, *(int*)0x1001d0010, $pc
bt 5
if $hits >= 80
  detach
  quit
end
continue
end
continue
''' + '"""' + r'''
path = pathlib.Path('/tmp/newpal-mode-watch.gdb')
log = pathlib.Path('/tmp/newpal-mode-watch.log')
path.write_text(commands)
with log.open('w') as output:
    process = subprocess.Popen(
        ['timeout', '-s', 'INT', '-k', '5', '120',
         'gdb', '-q', '-nx', '-batch', '-p', str(pid), '-x', str(path)],
        stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
deadline = time.monotonic() + 12
while time.monotonic() < deadline:
    contents = log.read_text()
    if 'Hardware watchpoint 1' in contents:
        print('GDB watchpoint armed for pid', pid, 'log', log)
        break
    if process.poll() is not None:
        print(contents)
        raise SystemExit(process.returncode or 1)
    time.sleep(.2)
else:
    print(log.read_text())
    raise SystemExit('GDB watchpoint did not arm')
'''
    result = subprocess.run(
        ssh_cmd(target) + ["python3 -"], input=script,
        capture_output=True, text=True, check=False, timeout=20,
    )
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    return result.returncode


def read_mode_watch(target: str) -> int:
    script = ("cat /tmp/newpal-mode-watch.log; "
              "printf '\\n--- process ---\\n'; "
              "awk '/^State:|^TracerPid:/{print}' /proc/$(pgrep -f '^(/storage)?/roms/ports/newpal-ios/machismo ' | head -1)/status; "
              "pgrep -af 'gdb.*newpal-mode-watch' || true")
    result = subprocess.run(
        ssh_cmd(target) + [script], capture_output=True, text=True,
        check=False, timeout=20,
    )
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    return result.returncode


def stop_mode_watch(target: str) -> int:
    script = '''
import os, signal, subprocess, time
result = subprocess.run(['pgrep', '-f', '^gdb -q -nx -batch -p .*newpal-mode-watch.gdb$'],
                        capture_output=True, text=True)
for value in result.stdout.split():
    os.kill(int(value), signal.SIGINT)
time.sleep(1)
print('GDB interrupt sent to', result.stdout.split())
'''
    result = subprocess.run(
        ssh_cmd(target) + ["python3 -"], input=script,
        capture_output=True, text=True, check=False, timeout=15,
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
    parser.add_argument("--read-trainer-log", action="store_true",
                        help="read recent trainer diagnostics without deploying")
    parser.add_argument("--process-status", action="store_true",
                        help="check whether NewPAL is running without deploying")
    parser.add_argument("--snapshot-actors", action="store_true",
                        help="read live scene actors without changing the game")
    parser.add_argument("--sample-script", action="store_true",
                        help="sample field script context for 10 seconds")
    parser.add_argument("--trace-mode", action="store_true",
                        help="trace scene mode and script context for 40 seconds")
    parser.add_argument("--attach-mode-watch", action="store_true",
                        help="attach GDB and watch scene-mode writes for two minutes")
    parser.add_argument("--read-mode-watch", action="store_true",
                        help="read the active GDB scene-mode trace")
    parser.add_argument("--stop-mode-watch", action="store_true",
                        help="interrupt the GDB scene-mode trace")
    args = parser.parse_args()
    if args.read_trainer_log:
        return read_trainer_log(args.target, args.remote_dir)
    if args.process_status:
        return process_status(args.target)
    if args.snapshot_actors:
        return snapshot_actors(args.target)
    if args.sample_script:
        return sample_script(args.target)
    if args.trace_mode:
        return trace_mode(args.target)
    if args.attach_mode_watch:
        return attach_mode_watch(args.target)
    if args.read_mode_watch:
        return read_mode_watch(args.target)
    if args.stop_mode_watch:
        return stop_mode_watch(args.target)
    if not args.skip_deploy:
        deploy(args.target, args.remote_dir, args.package)
    verify_runtime(args.target, args.remote_dir, args.package)
    return inspect(args.target, args.remote_dir, args.audit_only, args.seconds)


if __name__ == "__main__":
    raise SystemExit(main())
