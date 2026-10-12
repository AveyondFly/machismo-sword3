#!/bin/bash
# Paladin2 (Pal2_AppStore iOS port, machismo loader) PortMaster launcher.
# Shape follows the portmaster-standard-sh skeleton; every line outside it cites
# the on-device evidence that put it there.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

# ── official standard header, copied verbatim ──
if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
source $controlfolder/device_info.txt

[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

get_controls

GAMEDIR=/$directory/ports/paladin2-ios

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

CONFDIR="$GAMEDIR/conf/"
$ESUDO mkdir -p "$CONFDIR"

export XDG_CONFIG_HOME="$CONFDIR"
export XDG_DATA_HOME="$CONFDIR"

# The bundled runtime libs have to be visible to the game process, so this is
# exported -- and the order is what makes it work:
#   * /usr/lib first: the firmware libstdc++ (GLIBCXX_3.4.29) is newer than the
#     bundled gcc 9.4 one (3.4.28); letting the bundle shadow it kills
#     PortMaster's gptokeyb (EE47 log.txt: "gptokeyb: libs.aarch64/libstdc++.so.6:
#     version GLIBCXX_3.4.29 not found"), i.e. pad mapping and SELECT+START quit.
#   * port dirs before the inherited value: control.txt appends the 32-bit
#     /usr/lib32, and libc++.so.1 needs a 64-bit libatomic.so.1 that only the port
#     ships (EE47: "libatomic.so.1: wrong ELF class: ELFCLASS32").
export LD_LIBRARY_PATH="/usr/lib:$GAMEDIR/libs.aarch64:$GAMEDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# ── video backend: chosen per firmware, never left to SDL to guess ──
# ROCKNIX (Sway/Wayland): sourcing /storage/env.txt gives SDL_VIDEODRIVER=wayland,
# WAYLAND_DISPLAY=wayland-1 and an opengles2 renderer (device logs/runtime.log);
# the playthrough that cleared this port ran on that backend.
if [ -f /storage/env.txt ]; then
  # shellcheck disable=SC1091
  . /storage/env.txt
else
  # Pure kmsdrm firmware (EE47 and friends): no Wayland service runs, yet some
  # frontends leave WAYLAND_DISPLAY exported, which makes the host SDL pick the
  # Wayland backend and end up with an invalid window handle. Do not copy the
  # newpal launcher's [ -d /usr/lib/libmali ] test: that path does not exist on
  # the RF45H unit, so the condition is never true and the driver stays unset.
  export SDL_VIDEODRIVER=kmsdrm
  unset WAYLAND_DISPLAY
fi

# ── port tuning: only exports that differ from the engine's own defaults ──
# Left unset, the loader reads machismo.conf from cwd, which has no partial-SDL hooks.
export MACHISMO_CONFIG="$GAMEDIR/machismo-partial-sdl.conf"
# The engine is fail-soft by default (src/resolver.c:1168 aborts only when this is
# set). Keeping it fail-closed exposes a missing lib immediately instead of booting
# a sick game: both ROCKNIX and EE47 report 929 binds resolved / 0 failed with it.
export MACHISMO_STRICT_BINDS=1
# src/ports/sword3/paths.c:248: sword3_paths_init() returns ENOENT unless
# SWORD3_BUNDLE_DIR is set, so neither directory has a usable built-in default.
export SWORD3_BUNDLE_DIR="$GAMEDIR/game/Payload/Pal2_AppStore.app"
export SWORD3_DATA_DIR="$GAMEDIR/saves"

BINARY="$SWORD3_BUNDLE_DIR/Pal2_AppStore"
# port.json ships no game/: the user drops their own paladin2.ipa in and the first
# run extracts it. Staging into game.new keeps an interrupted unzip from leaving a
# half-populated game/ behind.
if [ ! -f "$BINARY" ]; then
  IPA="$GAMEDIR/paladin2.ipa"
  command -v unzip >/dev/null 2>&1 || { echo "paladin2-ios: unzip is required" >&2; exit 1; }
  [ -f "$IPA" ] || { echo "paladin2-ios: put your owned paladin2.ipa into $GAMEDIR" >&2; exit 1; }
  rm -rf "$GAMEDIR/game.new"
  mkdir -p "$GAMEDIR/game.new"
  unzip -q "$IPA" 'Payload/Pal2_AppStore.app/*' -d "$GAMEDIR/game.new"
  [ -f "$GAMEDIR/game.new/Payload/Pal2_AppStore.app/Pal2_AppStore" ] || {
    echo "paladin2-ios: $IPA lacks Payload/Pal2_AppStore.app/Pal2_AppStore" >&2
    rm -rf "$GAMEDIR/game.new"
    exit 1
  }
  rm -rf "$GAMEDIR/game"
  mv "$GAMEDIR/game.new" "$GAMEDIR/game"
fi

$ESUDO mkdir -p "$GAMEDIR/saves"

cd "$GAMEDIR"

# gptokeyb maps the pad onto a held keyboard; Pal2's SDL joystick is switched off by
# address-hooks.conf, so nothing here reads SDL_GAMECONTROLLERCONFIG. The positional
# argument is the name SELECT+START kills: machismo calls the guest entry in place
# (src/machismo.c:1093, no execv/fork), so comm stays "machismo". $GPTOKEYB may carry
# its own flags (EE47: "... gptokeyb -1"), so it must stay unquoted to word-split.
$GPTOKEYB "machismo" -c "$GAMEDIR/paladin2.gptk" &

"$GAMEDIR/machismo" "$BINARY" "$@"

pm_finish
