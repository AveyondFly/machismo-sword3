# Docker 本地编译规划

状态：**已执行**（2026-10-11，入口脚本 `scripts/docker-build.sh`，产物在 `build-docker/`，见文末第 8 节执行记录）。  
日期：2026-09-03  
目标：在本机 Windows + Docker 环境下，用已有 aarch64 镜像完成 Machismo Sword3 相关二进制编译。

## 1. 现状盘点

| 项 | 结果 |
|---|---|
| 宿主 | Windows x86_64 + Docker 29.7.2 |
| 可用镜像 | `ghcr.io/monkeyx-net/portmaster-build-templates/portmaster-builder:aarch64-latest`（Ubuntu 20.04 aarch64，QEMU user 模拟） |
| 备选镜像 | `portmaster-builder:aarch64-armhf-zen2`、`qemu-user-static:local` |
| 已有依赖 | cmake 3.31、gcc 9.4、ninja、make、python3、SDL2/mixer/image/ttf、GLES/EGL、libdrm 运行库 |
| 缺失依赖 | FFmpeg dev（`libavformat` / `libavcodec` / `libavutil` / `libswscale`）、`libdrm-dev` 头文件 |
| 容器权限 | root，可 `apt install` |
| 官方脚本 | `scripts/build-rocknix.sh` / `scripts/build-linaro.sh` 依赖**本机不存在**的 ROCKNIX/Linaro 交叉工具链，不能直接用 |

结论：第一轮走 **Docker 内 aarch64 原生编译**（QEMU user），不走 ROCKNIX/Linaro 交叉。

## 2. 编译流程

```text
① 启动 aarch64 容器（挂载源码 + 可写 build 目录）
        │
② apt 安装缺失包
   libavformat-dev libavcodec-dev libavutil-dev libswscale-dev
   libdrm-dev
        │
③ CMake configure（原生 aarch64，无 toolchain file）
   cmake -S . -B build-docker \
     -DCMAKE_BUILD_TYPE=RelWithDebInfo \
     -DBUILD_WRAPGEN=OFF -DBUILD_TESTS=OFF
        │
④ 编译产物（与 CMakeLists 对应）
   machismo
   libsystem_shim.so
   libsword3_ios_shim.so
   libsword3_objc_shim.so
   libsword3_gl_bridge.so
   libsword3_host.so
        │
⑤ 校验：file / readelf 确认 aarch64、动态依赖齐全
        │
⑥ 拷回宿主 build-docker/（Docker Desktop 卷挂载自动同步）
```

## 3. 命令骨架

在项目根目录执行：

```powershell
$IMG = "ghcr.io/monkeyx-net/portmaster-build-templates/portmaster-builder:aarch64-latest"
$SRC = "D:\ProgramData\machismo-sword3"

docker run --rm --platform linux/arm64 `
  -v "${SRC}:/src" `
  -w /src `
  $IMG bash -lc @'
    set -euo pipefail
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libdrm-dev
    cmake -S /src -B /src/build-docker \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DBUILD_WRAPGEN=OFF -DBUILD_TESTS=OFF
    cmake --build /src/build-docker --parallel $(nproc)
    file /src/build-docker/machismo /src/build-docker/*.so
'@
```

也可拆成两步（先 configure，再 build），便于失败后只重跑 build。

## 4. 注意点

1. **QEMU 模拟很慢**：全量编译预计 10–30 分钟级。`--parallel` 建议用 `nproc`，不宜盲目拉满。
2. **glibc 2.31**：Ubuntu 20.04 产物的 `GLIBC_` 需求约 2.31，比 Linaro 路径的 2.17 新。仅验证编译/链接够用；若最终要跑老 glibc 设备，需另备旧 sysroot。
3. **不要用** `cmake/rocknix-rk3326.cmake` / `cmake/linaro-aarch64.cmake`：里面写死了 `/home/ubuntu/...` 路径，容器里不存在。
4. **`libc++.so.1` 不在第一轮**：`scripts/package-rocknix.sh` 还要 Apple-ABI libc++（见 `scripts/build-libcxx-rocknix.sh` / `build-libcxx-linaro.sh`）。先跑通主 loader + sword3 host，再补 libc++。
5. **产物出口**：`build-docker/` 挂在仓库根下，Windows 侧可直接看到。
6. **不改动默认配置**：第一轮不修改 `configs/sword3/machismo.conf` 等运行配置；只做编译。

## 5. 期望产物

第一轮成功后，至少应有：

| 文件 | 说明 |
|------|------|
| `build-docker/machismo` | Mach-O loader 可执行文件 |
| `build-docker/libsystem_shim.so` | Darwin libc/pthread 兼容层 |
| `build-docker/libsword3_ios_shim.so` | Foundation/UIKit 最小代理 |
| `build-docker/libsword3_objc_shim.so` | ObjC2 runtime/msgSend |
| `build-docker/libsword3_gl_bridge.so` | OpenGLES → GLES1/GLES2/EGL |
| `build-docker/libsword3_host.so` | SDL / 路径 / 输入 / 音频 / FFmpeg |

校验命令：

```bash
file build-docker/machismo build-docker/*.so
readelf -d build-docker/libsword3_host.so | grep NEEDED
```

期望：架构为 aarch64；`libsword3_host.so` 能看到 SDL2、libavformat 等依赖。

## 6. 可选后续（不在第一轮）

| 步骤 | 何时做 |
|------|--------|
| Apple-ABI libc++ 构建 | 主产物链接通过后，为了上实机打包 |
| `scripts/package-rocknix.sh` 打包 | 需要 `dist/sword3-ios` 完整分发包时 |
| 部署到 RK3326 | 编译校验通过后，走 `scripts/deploy-rocknix.sh` |

## 7. 下次开工入口

1. 在项目根目录执行第 3 节命令。
2. 若 `apt` 网络失败：换镜像源，或改用 `portmaster-builder:aarch64-armhf-zen2` 再探测依赖。
3. 若链接阶段缺 FFmpeg 符号：确认容器内 `pkg-config --exists libavformat`。
4. 编译通过后，再决定是否进入 libc++ / 打包 / 实机部署阶段。

## 8. 执行记录（2026-10-11）

第一轮按第 3 节方案跑通，入口固化为 `scripts/docker-build.sh`（自动补装 FFmpeg dev → CMake configure → ninja 构建 → file/objdump/readelf 自检）。

实际结果与规划的差异：

| 项 | 规划预期 | 实际 |
|---|---|---|
| glibc 地板 | 2.31（Ubuntu 20.04 产物上限） | **2.17**（多数产物）/ 2.29（libsystem_shim.so），仅由实际用到的符号决定 |
| FFmpeg | 未明确版本 | 4.2（soname 58/56/5），比 ROCKNIX 产物低，正好匹配老固件 |
| 源码改动 | 不改源码 | 需要少量兼容修正（见下） |

为兼容容器内 FFmpeg 4.2 / Python 3.8 做的修正：

- 新增 `src/ports/sword3/ffmpeg_compat.h`：`SWORD3_AV_CHANNELS` / `SWORD3_AV_FRAME_DURATION` 按 libavutil 版本分发新（5.1+ `ch_layout`/`duration`）旧（`channels`/`pkt_duration`）字段；`sdl_bridge.c`、`video_bridge.c` 改经宏读取。
- `tools/generate_sword3_shim.py`、`tools/generate_objc_shim.py`：`write_text(newline=)` 是 Python 3.10 才有的参数，改为 `open(..., newline=)` 写入。

遗留注意：

1. `libsword3_host.so` 的 NEEDED 是 FFmpeg 4.x soname；老固件直接解析，ROCKNIX 等新固件上缺失——新固件继续用 ROCKNIX 工具链产物，或按 `port-runtime-lib-bundling` 手法捆绑。
2. 产物未 strip（调试期保留符号），打包发布前再瘦身。
3. `build-docker/` 已加入 `.gitignore`。
