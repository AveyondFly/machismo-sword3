# iOS 系统菜单：开闭检测 vs 状态机

适用样本（地址只对这个版本有效）：

```text
SWD3 Mach-O SHA-256:
268d6f40eac47718ee2cac6acd34912421546eb8a58056d310c0b42d2655e36b

LC_UUID:
7C40879F-22A3-3F14-8486-F37FA0AA96F3
```

宿主代码：`src/ports/sword3/sdl_bridge.c`。
Android 对照：`sword3/src/main.c` 的 `SysPage` / `SysLevel` / `inMenuSystem`。

---

## 1. 直接结论

**检测开闭可以靠状态位；关菜单不能靠宿主去改状态机。**

触摸打开这条路径上：

| 做法 | 行不行 |
|---|---|
| 读 `draw_gate`（`0x10030f48c`）判断开/关 | **行。** 打开 0→1，根层返回 1→0 |
| 点 widget `0x2495` 关闭 | **行。** 菜单开着时 `layer==1`，场地帧函数会把它当返回 |
| 点 `0x2495` 再打开 | **场地不行。** 关完 `layer=0`、click=`RET`，控件还停在返回位置 `(596,47)`，没人走 hit-test |
| 读档/标题上点 `0x2495` | **行一次。** 加载 walker `0x100027834` 还在，所以第一次 SELECT 看起来能开 |
| 场地 SELECT 调 `0x10002b2b0`（`MenuWallPaper_SystemACT`） | **开菜单用这个**，不要再点返回键残留 |
| 读 `SysPage` / `SysLevel` 判断这一层在哪 | **不行。** 触摸打开从不装那张 handler 表 |
| 宿主写 `layer`、调 `installer(0xea60)` | **不行。** 标题表会毁掉对话 |

关菜单仍然是 **再点一次 `0x2495`**。再打开不要点它，改调 `0x10002b2b0`。宿主：

1. `draw==1` 时认为菜单开着（SELECT 吞掉，B 点返回）。
2. 见过 `draw==1` 之后变成 `draw==0`，约 250ms 后结束宿主会话。
3. **不要写** SysPage / SysLevel / layer，**不要调用** `installer(0xea60)`。

---

## 2. 实机上真正会变的量

`scripts/menu_trace.py`：场地快照 → 点菜单 → 右 → 左 → 点菜单（关）→ 再点（开）。

| 地址 | 宿主宏 | 场地 | 触摸打开后 | 根层 B 之后 |
|---|---|---|---|---|
| `0x10030f48c` | `GUEST_DRAW_GATE` | 0 | **1** | **0** |
| `0x1002a99cc` | `GUEST_SYS_LAYER` | 标题残留 `0xea60`，进场地后常为 **1** | 1 | 游戏自己减成 **0**（不会写回 `0xea60`） |
| `0x1002a86a8` | `GUEST_FEXECUTE` click | 场地 `0x10005b87c`（4 字节 RET） | 仍是 RET | 仍是 RET |
| `0x1002aa46c` / `470` | `GUEST_SYS_PAGE` / `LEVEL` | 不变 | **不变** | **不变** |
| `0x1002aa488` | `GUEST_IN_MENU_SYSTEM` | -1 | **-1** | -1 |

`in=2` 出现过，那是地图 42，不是菜单开着。**不要用 `in < 0` 当关闭。**

`fex==RET`（`0x10005b87c`）是 **installer(1) 之后的正常场地表**：click 被 stub 掉，避免误点地图；confirm 槽是 `0x10005bbac`（对话）。**不是损坏。**

---

## 3. installer 表（`0x100023d8c` → `0x1002a86a8`）

`w0` 选哪一张 handler 表：

| `w0` | 含义 | click `+0x00` | confirm `+0x48` |
|---|---|---|---|
| **`0xea60` (60000)** | 标题 / MetaBar | `0x1000831a8` | `0x1000830ec`（标题确认，**不是 NPC 对话**） |
| **1** | Continue 之后的场地 | **`0x10005b87c`（RET）** | `0x10005bbac` |
| **0** | 根层返回后的递减 | 空 `ret` `0x1000240ec`，**不恢复任何表** |
| 其它 | 部分地图 / 键盘 SysPage 菜单 | 例如 `0x10002d018` | SysPage 那一套 |

`installer(0xea60)` 还会调 `0x100082f7c`：加载 MetaBar ACT、写 `inMenuSystem=-1`、清标题选择 BSS。 **场地行走时绝对不能调。**

SysPage 那套 handler（`0x10002d018` 点击、`0x10002d69c` page++、`0x10002d4f4` page--、`0x10002da28` 返回、`0x10002dad8` 确认）是真的，但只在 installer 走到特定分支时才装上。 **点 `0x2495` 打开的 iOS 系统菜单不会装它们。** 所以文档早期把 SysPage/SysLevel 当成触摸菜单的层状态，是找错簇了。

---

## 4. 打开 / 关闭（触摸，不是 Return）

真实按钮：widget **id `0x2495`（9365）**。

- 逻辑 640×480：场地残留约 **(592, 20)**；菜单出现过之后约 **(596, 47)**，约 44×48。
- 游戏用 **`x == -1` 隐藏控件**。
- 打开后 **同一个 widget 变成返回**。没有单独的 reopen API：再开还是再点它。
- **不是** `UIGamePad` ConfigIcon（`pad+0x1c0`）。那是左边 HUD / 虚拟十字开关。

宿主：

| 键 | 行为 |
|---|---|
| SELECT | **只打开**：标题/读档 tap `0x2495`；场地走 `0x10002b2b0`。`draw==1` 时吞掉，**从不拿它来关** |
| B | **只返回 / 关闭**：仅当 `draw==1` 时 tap `0x2495`。场地吞掉，不开菜单 |
| A | **仅菜单里确认**。`draw==1` 时放行（一级栏没有页签 widget 也要放行）。场地吞掉，避免走路 A 开门。NPC 对话走场地 confirm 槽 / 原生手柄读键，不是宿主 Return |
| 左右 | 若页签 `0x2496+` 在屏上则假点；否则键盘左右。一级栏页签坐标是 -1，键盘也 **不会** 改 SysPage（那张表没装） |

会话：

- SELECT 按下发出 tap；抬起不清会话。
- `draw` 变成 1 才 `menu_enter_session`。
- 见过 `draw==1` 再变 `draw==0`，约 250ms 后 `menu_leave_session`，输入模式改回 4。
- 日志不应再出现 `restored field`。

---

## 5. 曾经过的坑（不要再做）

1. 用 `g_menu_session` 单独当“在菜单里”：SELECT 抬起时 `draw` 还是 0，会话卡住，第二次 SELECT 被吞，场地 A 也被吞。
2. 用页签 chrome 判断关菜单：一级栏页签永远是 `(-1,-1)`，会误判已关。
3. `draw==0 && fex==RET` 时自动 `installer(0xea60)`：这是场地表，不是坏表。强制装标题表之后，**第一次进场地就不能对话**，SELECT 开再关也进不去。
4. 根层 B 之后宿主再写 `layer=0xea60`：游戏关菜单只是把 layer 减到 0，本来就不会回到标题哨兵。

---

## 6. 一级栏左右 / 进子项（未完成）

触摸打开后第一屏默认停在 **物品**，还没进子菜单。实机：widget **10015** 出现在 `(471,371) 80×80`；左右既不移动它，也不改 SysPage，只看到 `layer+0x28` 像计时器一样加。

下一步应沿着 **installer(1) 的真实点击命中**（`0x10001f708` 一类），去点卡片/命中矩形，而不是调用未安装的 `0x10002d018` / `SysPage++`。

嵌套深度目前也不是 SysLevel。`layer` 为 1 和更深之间的对应还没画完。B 在任意层继续点 `0x2495`，让游戏自己 pop。

---

## 7. 部署后怎么验

必须 **完全退出再进游戏**（热替换宿主不够）。

1. 进场地就能对 NPC 对话（A）。日志 **没有** `restored field`。`fex` 应是 `0x10005b87c`，不是标题 `0x1000831a8`。
2. SELECT 打开，`menust` 里 `draw=1`。
3. B 关闭，`draw=0`，出现 `menu closed draw=0` / `menu session end`。
4. 还能对话；再按 SELECT 还能打开。
5. 开着的时候 SELECT 不应关掉。
