#!/bin/sh
set -eu

GAMEDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
IPA=${SWORD3_IPA:-"$GAMEDIR/sword3.ipa"}
APPDIR="$GAMEDIR/game/Payload/SWD3.app"
BINARY="$APPDIR/SWD3"
EXPECTED_IPA=68d03cd8266d21044e25dcab362b148618da5d3ffc51ee3d260c8d7e90e4dab7
EXPECTED_BINARY=268d6f40eac47718ee2cac6acd34912421546eb8a58056d310c0b42d2655e36b

fail()
{
    echo "sword3-ios: $*" >&2
    exit 1
}

hash_file()
{
    sha256sum "$1" | awk '{print $1}'
}

require_hash=0
case "${SWORD3_REQUIRE_HASH-}" in
    1|yes|true|TRUE|Yes)
        require_hash=1
        ;;
esac

mkdir -p "$GAMEDIR/saves" "$GAMEDIR/logs"

need_extract=0
if [ ! -f "$BINARY" ]; then
    need_extract=1
elif [ "$require_hash" -eq 1 ] &&
     [ "$(hash_file "$BINARY")" != "$EXPECTED_BINARY" ]; then
    need_extract=1
fi

if [ "$need_extract" -eq 1 ]; then
    [ -f "$IPA" ] || fail "place the owned IPA at $IPA (needed only to extract game/)"
    if [ "$require_hash" -eq 1 ]; then
        [ "$(hash_file "$IPA")" = "$EXPECTED_IPA" ] ||
            fail "unsupported IPA hash"
    fi
fi

if [ "$need_extract" -eq 1 ]; then
    command -v unzip >/dev/null 2>&1 || fail "unzip is required"
    rm -rf "$GAMEDIR/game.new"
    mkdir -p "$GAMEDIR/game.new"
    unzip -q "$IPA" 'Payload/SWD3.app/*' -d "$GAMEDIR/game.new"
    [ -f "$GAMEDIR/game.new/Payload/SWD3.app/SWD3" ] ||
        fail "IPA did not contain Payload/SWD3.app/SWD3"
    if [ "$require_hash" -eq 1 ]; then
        [ "$(hash_file "$GAMEDIR/game.new/Payload/SWD3.app/SWD3")" = \
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

cd "$GAMEDIR"
exec "$GAMEDIR/machismo" "$BINARY" "$@" \
    >>"$GAMEDIR/logs/runtime.log" 2>&1
