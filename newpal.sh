#!/bin/sh
set -eu

PORTSDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
GAMEDIR=${NEWPAL_GAME_DIR:-"$PORTSDIR/newpal-ios"}
APPDIR="$GAMEDIR/game/Payload/NewPAL.app"
BINARY="$APPDIR/NewPAL"

fail()
{
    echo "newpal-ios: $*" >&2
    exit 1
}

[ -f "$BINARY" ] || fail "game executable not found: $BINARY"
mkdir -p "$GAMEDIR/saves" "$GAMEDIR/logs"

export LD_LIBRARY_PATH="$GAMEDIR/libs.aarch64:$GAMEDIR/libs:$GAMEDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export MACHISMO_CONFIG="${MACHISMO_CONFIG:-$GAMEDIR/machismo-partial-sdl.conf}"
export MACHISMO_STRICT_BINDS=1
export SWORD3_BUNDLE_DIR="$APPDIR"
export SWORD3_DATA_DIR="$GAMEDIR/saves"
export SWORD3_PORT=newpal
export SDL_GAMECONTROLLERCONFIG_FILE=/storage/.config/gptokeyb/gamecontrollerdb.txt

cd "$GAMEDIR"
status=0
"$GAMEDIR/machismo" "$BINARY" "$@" \
    >>"$GAMEDIR/logs/runtime.log" 2>&1 || status=$?
exit "$status"
