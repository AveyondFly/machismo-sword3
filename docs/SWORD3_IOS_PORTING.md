# Sword3 iOS → ROCKNIX 移植与调试记录

本文记录当前基于 Machismo 的实验性移植状态、已验证事实、构建方法、
调试命令和下一步工作。首帧渲染已在实机跑起来，但画面曾因 `RenderCopyF`
被误接成整数 `RenderCopy` 而只显示黄底。

## 1. 目标环境

- 目标系统：ROCKNIX AArch64
- 目标 SoC：RK3326
- 测试设备：`root@192.168.31.110`
- 交叉工具链：

  ```text
  /home/ubuntu/distribution/build.ROCKNIX-RK3326.aarch64/toolchain
  ```

- 工具链文件：
  [`cmake/rocknix-rk3326.cmake`](../cmake/rocknix-rk3326.cmake)
- 原始 IPA：

  ```text
  /home/ubuntu/sword3/ipa/sword3.ipa
  ```

当前执行环境禁止发起 `ssh`，因此本地可以完成交叉构建和 QEMU
验证，但实机部署命令需要在外部终端执行。

## 2. 固定样本

地址 hook 只适用于下面这个版本。`run-rocknix.sh` 默认**不再**在启动时
校验 IPA/SWD3 hash（1.4G IPA 的 sha256 会拖慢每次启动）。需要恢复旧闸门时：

```bash
SWORD3_REQUIRE_HASH=1 ./run-rocknix.sh
```

错误版本仍会被 address hook 的原始字节检查拒绝（全有或全无，不会部分 patch）。

```text
IPA SHA-256:
68d03cd8266d21044e25dcab362b148618da5d3ffc51ee3d260c8d7e90e4dab7

SWD3 Mach-O SHA-256:
268d6f40eac47718ee2cac6acd34912421546eb8a58056d310c0b42d2655e36b

LC_UUID:
7C40879F-22A3-3F14-8486-F37FA0AA96F3
```

主程序是未加密的 ARM64 PIE Mach-O：

- `cryptid=0`
- `LC_MAIN=0x1000f17cc`
- 20 个动态库依赖
- 565 个 nlist symbol
- 536 个 undefined symbol
- 6293 个 `LC_FUNCTION_STARTS` 函数入口
- 47 个 `__mod_init_func`
- SDL2 2.0.10、SDL_image、SDL_mixer、SDL_ttf、FreeType 和 Lua 5.2
  静态编入主程序

完整审计结果：
[`configs/sword3/binary-manifest.json`](../configs/sword3/binary-manifest.json)。

重新生成：

```bash
python3 tools/sword3_audit.py \
  --audit-only \
  /tmp/sword3-ipa-extract/Payload/SWD3.app/SWD3 \
  --output configs/sword3/binary-manifest.json
```

## 3. 当前架构

```text
SWD3 ARM64 Mach-O
  │
  ├─ Machismo loader
  │    ├─ 映射 Mach-O segments
  │    ├─ LC_DYLD_INFO_ONLY rebase/bind
  │    ├─ Apple ARM64 variadic ABI adapter
  │    ├─ compact unwind → DWARF
  │    └─ stripped address trampoline
  │
  ├─ libsystem_shim.so       Darwin libc/pthread/dyld → glibc
  ├─ libc++.so.1             Apple ABI 兼容 libc++
  ├─ libsword3_objc_shim.so  最小 ObjC2 runtime/msgSend
  ├─ libsword3_ios_shim.so   Foundation/UIKit/CoreGraphics 最小代理
  ├─ libsword3_gl_bridge.so  OpenGLES → GLES1/GLES2/EGL
  └─ libsword3_host.so       SDL、路径、输入、音频、FFmpeg 视频服务
```

## 4. 已完成的关键工作

### 4.1 Mach-O loader

- 支持该程序使用的 `LC_DYLD_INFO_ONLY`。
- 加入 `audit_only`，可完成映射和重定位但不执行任何 guest 指令。
- 加入 strict bind 模式；strong import 未解析时拒绝继续。
- 修复 Objective-C 符号名中的 `$` 被错误当成 Darwin ABI 后缀截断的问题。
- 修复 mixed C++/Objective-C compact-unwind personality：每个 personality
  使用独立 CIE。
- 增加安全 `entry_override`：
  - 候选 `SDL_main=0x10002c3b0`
  - 必须同时匹配前 16 字节
    `ff0302d1f65705a9f44f06a9fd7b07a9`
- 地址 hook 在写入前检查：
  - `LC_FUNCTION_STARTS`
  - 可执行 section
  - 原始字节
  - host DSO 和 symbol
  - branch island 范围
- 任意 hook 校验失败时不产生部分 patch。

### 4.2 入口验证

[`tools/recover_sdl.py`](../tools/recover_sdl.py) 已证明：

```text
LC_MAIN 0x1000f17cc
  adr x2, 0x10002c3b0
  ...
  调用 UIApplicationMain stub
```

`0x10002c3b0` 是 `LC_FUNCTION_STARTS` 中的独立函数，大小 584 字节，
符合 `int SDL_main(int argc, char **argv)` 行为。当前 loader 绕过
`UIApplicationMain`，直接调用这个函数。

报告：
[`configs/sword3/recovery-report.json`](../configs/sword3/recovery-report.json)。

### 4.3 动态绑定

使用真实 ROCKNIX runtime DSO 的 audit 结果：

```text
907 binds resolved
0 stubbed
0 failed
5594 rebases
7 ctor/dtor ABI adapters
10 variadic thunks
```

动态导入已经全部有明确接收方，不再存在 unresolved import。

### 4.4 Apple ABI libc++

[`scripts/build-libcxx-rocknix.sh`](../scripts/build-libcxx-rocknix.sh)
会构建：

```text
build-libcxx-rocknix/lib/libc++.so.1
build-libcxx-rocknix/lib/libc++abi.so.1
```

它启用了：

- `_LIBCPP_ABI_ALTERNATE_STRING_LAYOUT`
- `_LIBCPP_ABI_DARWIN_MBSTATE_COMPAT`
- Darwin pthread 对象布局补丁

GCC 14 会生成 `__cxa_call_terminate`，而当前 Machismo libc++abi
版本不导出它，因此使用
[`src/shim/gcc_cxa_compat.c`](../src/shim/gcc_cxa_compat.c)
提供安全 noreturn 实现，避免链接进 ABI 不兼容的 libstdc++。

### 4.5 ObjC 和最小 iOS 服务

已实现：

- ARM64 `objc_msgSend` / `objc_msgSendSuper2`
- x0-x7、q0-q7 参数保存和 IMP 尾跳；nil 消息同时清零
  x0/x1 与 v0-v3，覆盖整数、HFA 和小型聚合返回
- selector intern
- 普通和 relative/small method list 安全查找及稳定 Method 缓存
- retain/release/autorelease/weak/storeStrong 最小语义
- 符合 Apple 原型的 `objc_setProperty_atomic(self,_cmd,value,offset)`
- autorelease pool
- NSObject `init`、`respondsToSelector:`
- NSBundle：
  - `mainBundle`
  - `resourcePath`
  - `bundlePath`
  - `pathForResource:ofType:`
- NSFileManager：
  - `defaultManager`
  - `fileExistsAtPath:`
  - `contentsOfDirectoryAtPath:error:`
- NSArray 最小访问和 fast enumeration
- NSString 最小路径、切分、比较和数值转换
- NSLocale `preferredLanguages` 固定返回 `zh-Hans`
- UIDevice 最小设备信息
- UIScreen `mainScreen`、bounds、scale
- `NSSearchPathForDirectoriesInDomains`
- CGRect 几何辅助函数
- NSLog 安全 no-op

47 个 Mach-O 静态构造函数现在可以全部执行完成。

### 4.6 ROCKNIX host services

[`src/ports/sword3/`](../src/ports/sword3/) 已包含：

- `paths.c`：bundle/data 路径、目录 fd 锚定、`openat/renameat` 原子写入，
  拒绝 `..` 和中间 symlink 逃逸
- `input.c`：SDL controller/keyboard → 游戏 scancode，事务式状态更新、
  轴滞回、重复事件去重和 focus/device 移除防卡键
- `audio_bridge.c`：SDL_mixer 文件/内存播放及所有权管理，安全停止同一
  chunk 占用的全部 channel
- `video_bridge.c`：FFmpeg 6 MP4 解码到 RGBA frame，包含 EAGAIN/EOF
  drain、单调 PTS 和缺失时间戳合成
- `gl_bridge.c`：GLES1/GLES2/EGL 联合符号域和 OES/EXT bridge
- `host_prepare.c`：验证 SDL2 runtime 并执行 `SDL_SetMainReady`

FFmpeg bridge 已在 ROCKNIX 目标库和 QEMU 下实际解码 IPA 的 `ch.mp4`：
490/490 帧，最终 PTS 16.3163 秒。

## 5. 静态 SDL hook 现状

因为 SDL 被 strip，不能使用 Machismo 原本按 `_SDL_*` symbol 名匹配的
方式。当前通过“唯一错误字符串引用 + 父函数有序 BL + `LC_FUNCTION_STARTS`”
恢复了 41 个高置信函数，其中包括：

```text
0x1000abef8 SDL_InitSubSystem
0x1000e132c SDL_VideoInit
0x1000e15d8 SDL_VideoQuit
0x1000a03cc SDL_AudioInit
0x1000a06d8 SDL_AudioQuit
0x1000e2960 SDL_CreateWindow
0x1000f52cc SDL_CreateRenderer
```

配置：
[`configs/sword3/address-hooks.conf`](../configs/sword3/address-hooks.conf)。

这只是部分覆盖，不能作为最终运行配置。SDL object 不能在内嵌 SDL 和
ROCKNIX SDL 之间混用，尤其是：

- `SDL_Window`
- `SDL_Renderer`
- `SDL_Texture`
- `SDL_Surface`
- `SDL_RWops`
- 分配/释放函数

## 6. API 留空策略

不能把所有未实现 API 都简单返回 0。当前按三类处理：

### REQUIRED

必须有真实或兼容实现：

- ObjC 消息分派和 class/method metadata
- bundle/resource 路径
- 文件和存档
- SDL window/renderer/texture/surface/RWops 生命周期
- EGL/GLES
- 游戏需要的音频和视频结束回调

### FALLBACK

返回最小但有效结果：

- 系统语言：`zh-Hans`
- 屏幕：640×480、scale 1
- 设备型号：ROCKNIX
- Documents：映射到本地持久目录
- iCloud：固定不可用并走本地存档

### NOOP

只有确认调用方不依赖返回值/副作用时才留空：

- NSLog
- GL debug label
- framebuffer discard optimization
- 剪贴板
- 传感器
- idle timer
- iCloud 通知
- UIKit 提示框
- Metal 路径（最终固定使用 GLES）

返回 ObjC object、指针、尺寸或状态的 API 不可盲目 no-op。

## 7. 构建

```bash
cd /home/ubuntu/sword3/sword3-ios-linux

# Machismo 和所有 Sword3 shim
./scripts/build-rocknix.sh

# Apple ABI libc++
./scripts/build-libcxx-rocknix.sh

# 生成部署目录
./scripts/package-rocknix.sh
```

产物：

```text
dist/sword3-ios/
```

发行目录不包含 IPA 或游戏资源。用户需要自行放入合法拥有的
`sword3.ipa`。

## 8. 本地测试

### Python/host 测试

```bash
python3 -m unittest \
  tests/test_sword3_audit.py \
  tests/test_recover_sdl.py \
  tests/test_sword3_ios_shim.py \
  tests/test_objc_shim.py

bash tests/test_trampoline_addresses.sh
bash tests/test_objc_shim_cross.sh
bash tests/test_sword3_paths.sh
bash tests/test_sword3_ffmpeg.sh /home/ubuntu/sword3/ipa/sword3.ipa
```

### ROCKNIX 二进制的 QEMU resolve-only 测试

```bash
bash tests/test_sword3_resolve_rocknix.sh
```

该测试会执行两轮：

1. 全 STUB mapping，验证 Mach-O rebase/bind walker。
2. 真实 runtime mapping，要求 `907 resolved / 0 stubbed / 0 failed`。

两轮都不会执行 guest constructor 或入口。

## 9. 当前执行进度和最新阻塞

实机（Sway/Wayland，`source /storage/env.txt`）已达到：

```text
47 address hooks patched
907 binds / 0 stubbed / 47 constructors
SDL_VideoInit(NULL) -> 0 driver=wayland
CreateWindow 800x600 flags=0x2016 -> non-NULL, wayland
CreateRenderer -> non-NULL, driver=opengl
IMG_Init(0xf) -> 0xf runtime=2.8.2
IMG_Load(Resource/Background.png) -> 2436x1125
IMG_Load_RW -> 640x480 / 128x128
SDL_AudioInit(coreaudio) -> 0 driver=pulseaudio
OpenAudioDevice -> 2
SDL_InitSubSystem(0x3200) -> 0
```

游戏不调用 `SDL_Init(SDL_INIT_VIDEO)`，而是 `SDL_VideoInit(NULL)`。
`SDL_Init` 没有独立函数入口。

内嵌 iOS SDL_image 的 `IMG_Load`（`0x100116d98`）会走 ImageIO/UIImage 并
`raise:format:`。这组入口已 hook 到宿主 SDL_image 2.8.2：

| 符号 | 地址 | 锚点 |
|---|---|---|
| IMG_Load | `0x100116d98` | `SS2D_LoadIconFile` bl[0] |
| IMG_Load_RW | `0x10011963c` | `LoadImageFile` bl[3] |
| IMG_LoadTyped_RW | `0x100119644` | `Passed a NULL data source` |

未登记的 guest `SDL_RWops`（来自未 hook 的 `RWFromMem`）会先按 vtable
拷进宿主内存再解码，避免混用。实机已加载 Background、LoadingCyc、dpad
等 PNG，没有再出现 UIImage/ImageIO 异常。

`NSBundle infoDictionary`、事件泵（`PumpEvents`/`PollEvent`/`WaitEventTimeout`）
和 `IMG_Load` 已接上。实机 grim 曾看到 960×720 里居中一块冻结的 800×600
赭黄矩形，不是 `Background.png`。原因是 `0x1000f82ec` 是 iOS SDL 2.0.10 的
**`SDL_RenderCopyF`**（`SDL_FRect*`），却被当成整数 `SDL_RenderCopy`：
`dst={800.0f,600.0f}` 被读成几十亿的 `SDL_Rect`，第二张图等于没画上，只剩
一张被拉满窗口的 400×300 黄底纹理。现已改接到宿主 `SDL_RenderCopyF` /
`SDL_RenderCopyExF`。整数版 `RenderCopy`（`0x1000f82b8`）会 `scvtf` 后 BL
进 F 实现，不必单独 hook。

窗口和输入对齐 Android 口 `sword3/`：

- `CreateWindow` 强制 `FULLSCREEN_DESKTOP`，逻辑分辨率仍是游戏请求的 800×600，
  `SDL_RenderSetLogicalSize` 按比例铺满，比例不合则 letterbox。
- iOS 官方输入是触摸屏：`UIGamePad::Update(SDL_Event*)` 把 `SDL_FINGER*` 转成
  `InputKeyDown` / `InputClick`，同时预留了鼠标 / 键盘 / 摇杆 / 手柄。不要 stub
  这个函数。掌机映射：摇杆移光标，A 在光标处发 `FINGERDOWN/UP`，B 发右键，
  方向键走 `setting.lua` 的键盘 scancode。`GetWindowSize` 返回逻辑尺寸，避免
  guest 对 host `SDL_Window*` 做 magic check 失败、视口一直为 0。

## 10. 下一次继续的明确步骤

1. 实机验证：真机触摸（若有）以及摇杆挪光标后 A 点主菜单。
2. 视情况把 Android 口的 `commButtonClass` / `SDLINPUT::UpdateKeyStatus` /
   `BACK_KEY_CLICK` 战斗菜单补丁迁过来（iOS 二进制 strip 了 C++ 符号，要靠字符串锚点）。
3. 视情况补 `CreateRGBSurface` / `FillRect` / `ConvertSurface`，避免 guest
   表面混进 host blit。
4. 实机依次验证进游戏、音频、存档、ANI、MP4。
5. 不要用 QEMU dummy 结论代替 RK3326 实机 GPU 结论。启动前必须
   `source /storage/env.txt`。

## 11. 实机部署

当前环境对直接 `ssh` argv 有限制。用 Python 包装部署并测试：

```bash
cd /home/ubuntu/sword3/sword3-ios-linux
python3 scripts/device_test.py
```

`run-rocknix.sh` 会在启动前 `source /storage/env.txt`，以继承 Sway
会话里的 `WAYLAND_DISPLAY` 和 `XDG_RUNTIME_DIR`。

手动部署：

```bash
cd /home/ubuntu/sword3/sword3-ios-linux
./scripts/deploy-rocknix.sh
```

默认目标：

```text
root@192.168.31.110:/storage/roms/ports/sword3-ios
```

然后把用户自有 IPA 放到设备：

```bash
scp /home/ubuntu/sword3/ipa/sword3.ipa \
  root@192.168.31.110:/storage/roms/ports/sword3-ios/
```

为 resolve-only audit 先只解出 app，不调用游戏入口：

```bash
ssh root@192.168.31.110 '
  cd /storage/roms/ports/sword3-ios &&
  rm -rf game &&
  mkdir game &&
  unzip -q sword3.ipa "Payload/SWD3.app/*" -d game &&
  test "$(sha256sum game/Payload/SWD3.app/SWD3 | awk "{print \$1}")" = \
    268d6f40eac47718ee2cac6acd34912421546eb8a58056d310c0b42d2655e36b
'
```

首次只运行无 guest 执行的真实 mapping audit：

```bash
ssh root@192.168.31.110 '
  cd /storage/roms/ports/sword3-ios &&
  MACHISMO_CONFIG=machismo-runtime-audit.conf \
  MACHISMO_STRICT_BINDS=1 \
  LD_LIBRARY_PATH=. \
  ./machismo game/Payload/SWD3.app/SWD3
'
```

正常启动脚本：

```bash
ssh root@192.168.31.110 \
  /storage/roms/ports/sword3-ios/run-rocknix.sh
```

日志位置：

```text
/storage/roms/ports/sword3-ios/logs/runtime.log
```

注意：当前默认 `machismo.conf` 没有启用部分 SDL hook；用于研发的
`machismo-partial-sdl.conf` 也尚未达到可玩状态。继续开发前不要把当前
产物当作正式端口发布。
