# Sword3 输入重构计划

## 目标

把当前集中在 `sdl_bridge.c` 的触屏、键盘和手柄适配拆成有明确生命周期与写入边界的模块，避免一个 UI 的按键适配污染其他 UI。

本计划只支持固定的 Sword3 iOS Mach-O：

```text
SHA-256 268d6f40eac47718ee2cac6acd34912421546eb8a58056d310c0b42d2655e36b
```

目标平台为 ROCKNIX RK3326/aarch64。构建只使用：

```text
/home/ubuntu/distribution/build.ROCKNIX-RK3326.aarch64/toolchain
```

对应 sysroot：

```text
/home/ubuntu/distribution/build.ROCKNIX-RK3326.aarch64/toolchain/aarch64-rocknix-linux-gnu/sysroot
```

## 已确认的原版输入结构

原版存在统一的输入归一化入口，但不存在维护全部 UI 焦点的单一分发器：

```text
SDL_Event
  -> UIGamePad::Update (0x1001abcf4)
  -> keyboard/controller/dpad/click/coordinate slots
  -> shared action router (0x10002380c, coverage is partial)
  -> per-scene/per-widget state machines
```

关键地址：

| 名称 | 地址/偏移 |
|---|---:|
| UIGamePad object | `0x100304e28` |
| UIGamePad::Update | `0x1001abcf4` |
| input transition | `0x1001c15fc` |
| action router | `0x10002380c` |
| action table installer | `0x100023d8c` |
| GetDirState | `0x1001c1df4` |
| PlayerMove | `0x100072a28` |
| save/load list | `0x100027834` |
| system menu | `0x10002d018` |
| shop | `0x10003992c` |
| battle input | `0x10003cbf8` |
| Caption/Dialog poll | `0x1001f24cc` |

## 设计原则

### 生命周期唯一来源

每帧读取一次 guest 权威状态，生成不可变的 `InputContext`。输入处理期间不重新路由；如果处理导致页面切换，新 context 从下一个事件/帧开始生效。

Context 至少包含：

- base scene：field/title/battle
- modal overlay：caption/save/shop/system/host UI
- page/layer/sub-state
- route generation
- 同时 active 的候选集合

`SYS_PAGE/SYS_LEVEL/SYS_SUB` 第一轮只作为诊断值输出。它们尚不能可靠
表示 iOS 触摸路径中的 item list/action row 等真实子状态，因此在找到稳定的
handler/widget identity 前不参与 generation。

### 模态优先级

输入只交给最高优先级的 active route。初始优先级：

1. host cheat
2. Caption/blocking dialog
3. host menu
4. host battle
5. native battle
6. save/load
7. shop
8. menu opening transition
9. title
10. native system menu
11. pointer mode
12. field

优先级需要通过 shadow 日志和实机状态转换验证，不能仅凭现有 `rewrite_event()` 的条件顺序认定。

### 写入必须有 owner

Adapter 不得直接写 guest input slot、selection 或 input mode。最终所有写入通过 `InjectionBroker`：

```text
owner + generation + capability + operation
```

Broker 负责：

- held key/button lease
- physical down/up capture
- route 离开时自动 release
- stale generation 拒绝写入
- debug 构建中的 capability 越权检查

### 使用最窄的注入通道

优先级从安全到危险：

1. UI 自己的 native action
2. UI 自己的 selection
3. UI 专用 input slot
4. synthetic tap
5. process-wide keyboard state

### Adapter 粒度

Adapter 对应“交互状态/控件组”，不是顶层 UI。例如：

```text
system.tabs
system.item.list
system.item.actions
system.item.drop_confirm
battle.command
battle.target
caption.choice
field.movement
```

## 分阶段实施

### 第一轮：只观察的生命周期骨架

- 新增独立、无 SDL/guest 内存依赖的 context resolver。
- Resolver 输出唯一 route、候选 mask 和 generation。
- 在 `sdl_bridge.c` 中以 shadow mode 接入。
- 不改变 `rewrite_event()` 的现有事件消费和注入行为。
- 环境变量 `SWORD3_INPUT_SHADOW=1` 开启 transition/overlap 日志。
- 为 resolver 增加纯 C 单元测试。

验收：

- ROCKNIX 交叉构建通过。
- shadow 关闭时没有新增运行时行为。
- shadow 开启时只在 route/context 变化或候选冲突时记录。
- generation 只随生命周期相关字段变化，不随普通焦点移动变化。

### 第二轮：InjectionBroker

- 包装现有 key/dpad/controller/finger/input-mode 写入。
- 为每次写入附加 owner 和 generation。
- 先保持所有现有调用语义不变。
- 集中 route leave 清理，逐步替换散落的 release 函数。

### 第三轮：低风险 Adapter

按顺序迁移：

1. Caption choice
2. field movement
3. title selection
4. save/load
5. shop

每次只迁移一个 interaction state，并验证进入、退出、按住方向和手柄断开。

### 原生系统菜单第一切片

原生触摸菜单先只建立两个 host 焦点层，不尝试一次覆盖所有子页面：

```text
TABS
  左右 -> 只移动 host tab focus
  A    -> 在选中 tab 上合成一次 tap，进入 CONTENT
  B    -> 原生 back widget 关闭菜单

CONTENT
  方向 -> 现有 native page action/key 路径
  A    -> 现有 native confirm 路径
  B    -> 原生 back widget pop；native layer > 2 时保留 CONTENT，
          从 layer 2 回到根层时才把 host focus 还给 TABS
```

`TABS` 状态不得写 guest keyboard direction slot，避免 PlayerMove、Caption
或页面 handler 同帧观察到全局方向键。选中框是 host 叠加层，不替换 native
菜单绘制。天书 action widget id `3..7` 会复用顶部区域，不能作为 tab identity。

### 第四轮：复杂 UI

- system menu 按 tabs/item-list/item-actions/confirm 等子状态拆分。
- battle 按 command/list/target/result 拆分。
- 优先恢复 native action/selection；无法稳定适配时保留 host replacement。

## 实机验证矩阵

每个迁移必须覆盖：

- field -> system menu（方向仍按住）
- item list -> action row
- action row -> Caption confirm
- system menu -> field
- battle command -> target -> result
- title -> load list
- shop -> field
- 任意状态下 controller disconnect

共同断言：

- 旧 owner 无 held input
- 无遗留 finger-down
- stale generation 不能写 guest
- 一个物理事件只被一个 route 消费
- input mode 在退出时恢复

## 部署

设备入口：

```text
root@192.168.31.110:/roms/ports/sword3-ios.sh
```

实际运行目录：

```text
root@192.168.31.110:/roms/ports/sword3-ios
```

部署前必须确认入口脚本的真实指向；不得再默认使用旧的 `/storage/roms/ports/sword3-ios`。
