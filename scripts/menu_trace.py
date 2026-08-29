#!/usr/bin/env python3
"""Inject menu taps/keys on the running ROCKNIX game and snapshot guest state.

The host already shares the guest address space. This drops commands into
/tmp/sword3-menu-trace; libsword3_host.so taps widget 0x2495 or sends
keys, waits, then diffs BSS + on-screen widgets.

The game must already be on the field (map 30). Restart after deploying
a host that contains menu_trace_poll.

Examples:
  python3 scripts/menu_trace.py run
  python3 scripts/menu_trace.py snap field
  python3 scripts/menu_trace.py cmd tap-menu wait 500 snap open
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import device_test

TRACE = "/tmp/sword3-menu-trace"
LOG = "/storage/roms/ports/sword3-ios/logs/runtime.log"

SEQUENCE = """\
snap field
tap-menu
wait 500
snap open
right
wait 250
snap right
left
wait 250
snap left
tap-menu
wait 500
snap back
tap-menu
wait 500
snap reopen
"""


def ssh(cmd: str, check: bool = True) -> subprocess.CompletedProcess:
    return device_test.run(
        device_test.ssh_cmd(device_test.DEFAULT_TARGET) + [cmd],
        check=check,
        capture_output=True,
        text=True,
    )


def game_running() -> bool:
    r = ssh("pidof machismo || true", check=False)
    return bool((r.stdout or "").strip())


def send(text: str) -> None:
    payload = text if text.endswith("\n") else text + "\n"
    # Atomic replace so the host never reads a partial file.
    script = f"""
set -e
cat > {TRACE}.tmp <<'EOF'
{payload}EOF
mv -f {TRACE}.tmp {TRACE}
"""
    ssh(script)


def dump_trace_log(lines: int = 220) -> None:
    r = ssh(
        f"grep 'menu-trace\\|SELECT -> menu\\|menust ' {LOG} | tail -n {lines}",
        check=False,
    )
    sys.stdout.write(r.stdout or "")
    if r.stderr:
        sys.stdout.write(r.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", nargs="?", default="run",
                        help="run | snap | cmd")
    parser.add_argument("rest", nargs="*", help="snap label, or raw commands")
    args = parser.parse_args()

    if not game_running():
        print("machismo is not running; start the game and enter the field",
              file=sys.stderr)
        return 2

    if args.action == "run":
        print("writing open / left-right / back / reopen sequence")
        send(SEQUENCE)
        time.sleep(4.5)
        dump_trace_log()
        return 0
    if args.action == "snap":
        label = args.rest[0] if args.rest else "snap"
        send(f"snap {label}\n")
        time.sleep(0.8)
        dump_trace_log(80)
        return 0
    if args.action == "cmd":
        if not args.rest:
            print("usage: menu_trace.py cmd tap-menu wait 500 snap open",
                  file=sys.stderr)
            return 2
        lines = []
        it = iter(args.rest)
        for tok in it:
            if tok == "wait":
                ms = next(it, "200")
                lines.append(f"wait {ms}")
            elif tok == "snap":
                lines.append("snap " + next(it, "snap"))
            elif tok == "tap":
                x = next(it, "0")
                y = next(it, "0")
                lines.append(f"tap {x} {y}")
            else:
                lines.append(tok)
        send("\n".join(lines) + "\n")
        time.sleep(2.5)
        dump_trace_log(120)
        return 0

    print(f"unknown action {args.action}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
