#!/bin/sh
set -eu

PORTSDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
GAMEDIR=${NEWPAL_GAME_DIR:-"$PORTSDIR/newpal-ios"}
APPDIR="$GAMEDIR/game/Payload/NewPAL.app"
BINARY="$APPDIR/NewPAL"
GPTOKEYB_BIN=/storage/.config/gptokeyb/gptokeyb

fail()
{
    echo "newpal-ios: $*" >&2
    exit 1
}

[ -f "$BINARY" ] || fail "game executable not found: $BINARY"
[ -x "$GPTOKEYB_BIN" ] || fail "gptokeyb not found: $GPTOKEYB_BIN"
[ -f "$GAMEDIR/newpal.gptk" ] || fail "key map not found: $GAMEDIR/newpal.gptk"
mkdir -p "$GAMEDIR/saves" "$GAMEDIR/logs"

export LD_LIBRARY_PATH="$GAMEDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export MACHISMO_CONFIG="${MACHISMO_CONFIG:-$GAMEDIR/machismo-partial-sdl.conf}"
export MACHISMO_STRICT_BINDS=1
export SWORD3_BUNDLE_DIR="$APPDIR"
export SWORD3_DATA_DIR="$GAMEDIR/saves"
export SWORD3_PORT=newpal
export SDL_GAMECONTROLLERCONFIG_FILE=/storage/.config/gptokeyb/gamecontrollerdb.txt

gptk_pid=
cleanup_gptk()
{
    if [ -n "$gptk_pid" ]; then
        kill "$gptk_pid" 2>/dev/null || true
        wait "$gptk_pid" 2>/dev/null || true
    fi
}
trap cleanup_gptk EXIT HUP INT TERM

cd "$GAMEDIR"
"$GPTOKEYB_BIN" -sudokill -k machismo -c "$GAMEDIR/newpal.gptk" \
    >>"$GAMEDIR/logs/runtime.log" 2>&1 &
gptk_pid=$!

status=0
"$GAMEDIR/machismo" "$BINARY" "$@" \
    >>"$GAMEDIR/logs/runtime.log" 2>&1 || status=$?
exit "$status"
