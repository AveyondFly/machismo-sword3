#!/bin/bash
# NewPAL（新仙剑奇侠传 iOS 移植）PortMaster 启动脚本。
# 形态依据 portmaster-standard-sh 标准骨架；调优区每行来自 machismo 引擎的
# 硬性要求或 EE47 实机回归结论，非标准行均有注释说明。

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

# ── 官方标准头：逐行照抄，不自创 ──
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

GAMEDIR=/$directory/ports/newpal-ios

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

CONFDIR="$GAMEDIR/conf/"
$ESUDO mkdir -p "$CONFDIR"

export XDG_CONFIG_HOME="$CONFDIR"
export XDG_DATA_HOME="$CONFDIR"
export LD_LIBRARY_PATH="$GAMEDIR/libs.aarch64:$LD_LIBRARY_PATH"

# ── EE47 (EmuELEC 4.7-Nexus / KMS-DRM) 视频后端 ──
# 该固件没有 wayland 服务，但环境里仍残留 WAYLAND_DISPLAY，会让 host SDL 误选
# wayland 后端拿到无效窗口句柄；/usr/lib/libmali 存在即 EE47，显式 kmsdrm。
if [ -d /usr/lib/libmali ]; then
  export SDL_VIDEODRIVER=kmsdrm
  export LD_LIBRARY_PATH="/usr/lib/libmali:$LD_LIBRARY_PATH"
  unset WAYLAND_DISPLAY
fi

# ── 端口调优区：machismo 引擎要求的环境（引擎无内部默认值可依赖）──
export MACHISMO_CONFIG="$GAMEDIR/machismo-partial-sdl.conf"
# 不要开 MACHISMO_STRICT_BINDS：NewPAL 会导入 4 个 sword3 shim 未提供的符号
# （OBJC_CLASS_$_UIAlertView / CACurrentMediaTime / atexit / objc_autorelease），
# strict 模式会在 dylib load 阶段直接 abort（EE47 实机回归结论）。
export SWORD3_PORT=newpal

APPDIR="$GAMEDIR/game/Payload/NewPAL.app"
BINARY="$APPDIR/NewPAL"
[ -f "$BINARY" ] || {
  echo "newpal: game executable missing: $BINARY" >&2
  echo "newpal: put your owned newpal.ipa into $GAMEDIR and extract game/ from it" >&2
  exit 1
}
mkdir -p "$GAMEDIR/saves"
export SWORD3_BUNDLE_DIR="$APPDIR"
export SWORD3_DATA_DIR="$GAMEDIR/saves"

cd "$GAMEDIR"

$ESUDO chmod +x "$GAMEDIR/machismo"

SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig" \
  "$GAMEDIR/machismo" "$BINARY" "$@"

pm_finish
