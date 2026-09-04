#!/bin/sh
set -eu

GAMEDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
IPA=${PALADIN2_IPA:-${SWORD3_IPA:-"$GAMEDIR/paladin2.ipa"}}
APPDIR="$GAMEDIR/game/Payload/Pal2_AppStore.app"
BINARY="$APPDIR/Pal2_AppStore"
EXPECTED_IPA=8f5abda81c4efe47619850a268905a1eb24149fdde741bd950e972639ce20dcc
EXPECTED_BINARY=f9e510328d0c22eec61da1f0ebe501fa56454e4972e5149277cc3abe7ce8d2a3

fail()
{
    echo "paladin2-ios: $*" >&2
    exit 1
}

hash_file()
{
    sha256sum "$1" | awk '{print $1}'
}

require_hash=0
case "${PALADIN2_REQUIRE_HASH-${SWORD3_REQUIRE_HASH-}}" in
    1|yes|true|TRUE|Yes)
        require_hash=1
        ;;
esac

[ -f "$IPA" ] || fail "place the owned IPA at $IPA"
if [ "$require_hash" -eq 1 ]; then
    [ "$(hash_file "$IPA")" = "$EXPECTED_IPA" ] ||
        fail "unsupported IPA hash"
fi

mkdir -p "$GAMEDIR/saves" "$GAMEDIR/logs"

need_extract=0
if [ ! -f "$BINARY" ]; then
    need_extract=1
elif [ "$require_hash" -eq 1 ] &&
     [ "$(hash_file "$BINARY")" != "$EXPECTED_BINARY" ]; then
    need_extract=1
fi

if [ "$need_extract" -eq 1 ]; then
    command -v unzip >/dev/null 2>&1 || fail "unzip is required"
    rm -rf "$GAMEDIR/game.new"
    mkdir -p "$GAMEDIR/game.new"
    unzip -q "$IPA" 'Payload/Pal2_AppStore.app/*' -d "$GAMEDIR/game.new"
    [ -f "$GAMEDIR/game.new/Payload/Pal2_AppStore.app/Pal2_AppStore" ] ||
        fail "IPA did not contain Payload/Pal2_AppStore.app/Pal2_AppStore"
    if [ "$require_hash" -eq 1 ]; then
        [ "$(hash_file "$GAMEDIR/game.new/Payload/Pal2_AppStore.app/Pal2_AppStore")" = \
          "$EXPECTED_BINARY" ] || fail "extracted Mach-O hash mismatch"
    fi
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

gptk_bin=${GPTOKEYB:-}
if [ -z "$gptk_bin" ]; then
    if command -v gptokeyb >/dev/null 2>&1; then
        gptk_bin=gptokeyb
    elif command -v gptokeyb2 >/dev/null 2>&1; then
        gptk_bin=gptokeyb2
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

cd "$GAMEDIR"
if [ -n "$gptk_bin" ]; then
    echo "paladin2-ios: $gptk_bin -k machismo -c paladin2.gptk" >&2
    "$gptk_bin" -k machismo -c "$GAMEDIR/paladin2.gptk" \
        >>"$GAMEDIR/logs/runtime.log" 2>&1 &
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
