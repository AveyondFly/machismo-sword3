#!/bin/sh
set -eu

PORTSDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
GAMEDIR=${PALADIN2_GAME_DIR:-"$PORTSDIR/paladin2-ios"}
IPA=${PALADIN2_IPA:-${SWORD3_IPA:-"$GAMEDIR/paladin2.ipa"}}
APPDIR="$GAMEDIR/game/Payload/Pal2_AppStore.app"
BINARY="$APPDIR/Pal2_AppStore"

fail()
{
    echo "paladin2-ios: $*" >&2
    exit 1
}

[ -d "$GAMEDIR" ] || fail "runtime directory not found: $GAMEDIR"
mkdir -p "$GAMEDIR/saves" "$GAMEDIR/logs"

if [ ! -f "$BINARY" ]; then
    [ -f "$IPA" ] || fail "place the owned IPA at $IPA"
    command -v unzip >/dev/null 2>&1 || fail "unzip is required"
    rm -rf "$GAMEDIR/game.new"
    mkdir -p "$GAMEDIR/game.new"
    unzip -q "$IPA" 'Payload/Pal2_AppStore.app/*' -d "$GAMEDIR/game.new"
    [ -f "$GAMEDIR/game.new/Payload/Pal2_AppStore.app/Pal2_AppStore" ] ||
        fail "IPA did not contain Payload/Pal2_AppStore.app/Pal2_AppStore"
    rm -rf "$GAMEDIR/game"
    mv "$GAMEDIR/game.new" "$GAMEDIR/game"
fi

# ROCKNIX Sway session exports WAYLAND_DISPLAY, XDG_RUNTIME_DIR, and related
# compositor variables here. Load them before SDL starts so the host backend
# can attach to the running Wayland display instead of trying a dummy/KMS grab.
if [ -f /storage/env.txt ]; then
    set +u
    # shellcheck disable=SC1091
    . /storage/env.txt
    set -u
fi

export LD_LIBRARY_PATH="$GAMEDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export MACHISMO_CONFIG="${MACHISMO_CONFIG:-$GAMEDIR/machismo-partial-sdl.conf}"
export MACHISMO_STRICT_BINDS=1
export SWORD3_BUNDLE_DIR="$APPDIR"
export SWORD3_DATA_DIR="$GAMEDIR/saves"

# gptokeyb turns the handheld pad into a held keyboard. Pal2's SDL
# joystick is address-hooked off so it does not steal the same device.
# SELECT+START kills machismo. Do not exec: we must reap gptokeyb.
if [ -f /storage/.config/gptokeyb/control.ini ]; then
    set +u
    # shellcheck disable=SC1091
    . /storage/.config/gptokeyb/control.ini
    set -u
fi

gptk_cmd=${GPTOKEYB:-}
if [ -z "$gptk_cmd" ]; then
    if command -v gptokeyb >/dev/null 2>&1; then
        gptk_cmd=gptokeyb
    elif command -v gptokeyb2 >/dev/null 2>&1; then
        gptk_cmd=gptokeyb2
    fi
fi

gptk_pid=
cleanup_gptk()
{
    if [ -n "$gptk_pid" ]; then
        kill -9 "$gptk_pid" 2>/dev/null || true
    fi
    killall -9 gptokeyb gptokeyb2 2>/dev/null || true
}
trap cleanup_gptk EXIT INT TERM

start_gptk()
{
    # ROCKNIX may include options in GPTOKEYB, for example
    # "/storage/.config/gptokeyb/gptokeyb -sudokill". Split those command
    # words in this helper so the game's original positional arguments stay
    # intact. Pathname expansion is disabled and eval is deliberately avoided.
    set -f
    # shellcheck disable=SC2086
    set -- $gptk_cmd
    set +f
    exec "$@" -k machismo -c "$GAMEDIR/paladin2.gptk"
}

cd "$GAMEDIR"
if [ -n "$gptk_cmd" ]; then
    echo "paladin2-ios: $gptk_cmd -k machismo -c paladin2.gptk" >&2
    start_gptk >>"$GAMEDIR/logs/runtime.log" 2>&1 &
    gptk_pid=$!
else
    echo "paladin2-ios: gptokeyb not found; pad will not be mapped" >&2
fi

"$GAMEDIR/machismo" "$BINARY" "$@" \
    >>"$GAMEDIR/logs/runtime.log" 2>&1
status=$?
trap - EXIT INT TERM
cleanup_gptk
exit "$status"
