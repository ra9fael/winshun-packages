# Packages 插件验证 — 任务干预与卸载入口（2026-11-12）

[English](validation-queue.en.md)

环境：Windows 11、WinShun 0.22.0 源码、Qt 6.12.0（msvc2022_64）、MSVC 14.51、winget v1.29.380 zh-CN。

## 1. 报告的两个缺陷

| # | 报告 | 读码确认的根因 |
|---|---|---|
| A | 任务列表只能看，不能干预 | 三个操作都不带 task id：`removeLastPendingTask()` 从后往前找第一个可移除的，`requestCancelLastRunning()` 找第一个运行中的，`retryAction()` 不接受 id。点第 1 行可能删掉第 5 行。 |
| B | 找不到卸载操作 | `openReview()` 用一条隐式规则推断唯一动作：已装且远程有新版 ⇒ 永远是 `Upgrade`。升级与卸载是两个独立意图，不该由隐式规则替用户决定。 |

## 2. 改了什么

| 改动 | 说明 |
|---|---|
| `src/core/queue.h/.cpp` | 新增 `removeFinished(id)` / `canRemove(id)` / `canCancel(id)` / `pause()` / `resume()` / `paused()` / `appendLog` / `clearFinished()`；`nextRunnable()` 在暂停时不下发新任务。`TaskLogEntry{at,phase,kind,text,exitCode}` 挂在 `Task` 上，上限 200 条、保留尾部。 |
| `src/core/plan.h/.cpp` | 新增 `actionFromId()`：未知字符串一律返回 `Action::None`，坏字符串不可能变成一次变更。 |
| `src/ui/window.h/.cpp` | `cancelTask/removeTask/retryTask/startTask/clearFinishedTasks/setQueuePaused/toggleQueuePaused` 全部按 id；`logLine()` 是所有日志写入的唯一入口（跨线程重投到 GUI 线程后经 `redactBlock` 脱敏）；`buildReview()` 与 `openReview()` 共用一条路径；`setReviewAction()` 只接受本次审阅的合法动作；执行器按阶段写日志；`statusLine()` 在任务页报告队列；`setTab()` 补发 `statusChanged`。 |
| `src/ui/models.h/.cpp` | 新增 `canRetry` / `canStart` / `logCount` / `logText` 角色；`canRemove` 改为"除运行中以外都可移除"。 |
| `src/adapters/winget_adapter.h/.cpp` | `pinsCached()`：`winget pin list` 一次带互斥的缓存，被已安装与更新两个并行查询共用。 |
| `src/adapters/scoop_adapter.cpp` | `locateScoop()` 改为 `resolveScoopRoot()` 纯逻辑 + Windows 侧事实采集：根的顺序是 `%SCOOP%` → 注册表 `ProfileList\<SID>`（不读 `USERPROFILE`）→ `PATH` 上的 `scoop.ps1` → `%ProgramData%\scoop`。原因码写进失败文本。 |
| `src/ui/qml/TasksPage.qml` | 页头队列控制（暂停/继续、清空已完成、重新扫描）；每行按状态显示 开始/取消/重试/移除 与日志开关；行内可展开逐阶段日志。 |
| `src/ui/qml/ReviewDialog.qml` | 顶部动作选择器；卸载时协议文案改为"理解影响"。 |
| `src/ui/qml/Main.qml` | 隐藏的搜索框收回 `Layout.fillWidth`（间距根因）；任务页隐藏"已选 N / 审阅所选操作"；日志面板改用已存在的 token。 |

## 3. 实测

### 3.1 单元测试

```
ctest --test-dir build/dev
  1/6 packages_core      Passed   (220 检查，0 失败)
  2/6 packages_theme     Passed
  3/6 packages_process   Passed
  4/6 packages_fixtures  Passed
  5/6 packages_scoop     Passed
  6/6 packages_static    Passed
```

本轮新增 `queue intervention (T21)` 段（44 检查）：

- `removeFinished(id)` 只删指定 id，另一条记录不受影响；未知 id 是 no-op。
- 运行中的任务 `canRemove=false` / `canCancel=true`，`removeFinished` 与 `removePending` 都返回 false，记录仍在。
- 终结态可移除；移除后 `canRemove` 变 false。
- `pause()` 后 `nextRunnable()` 不返回任何任务且给出原因，运行中的任务仍是 `Running`（暂停不是取消）；`resume()` 后立刻给出排队头。
- `clearFinished()` 只清终结态，返回清除数量，再清一次返回 0。
- 日志超过上限时丢最旧的、保留尾部，`at` 为 8 字符时间、`phase` 正确。

`plan confirmation (T06)` 新增 `actionFromId` 往返断言：四个动作 id 双向一致，`"none"` / `""` / `"--force"` 都归到 `Action::None`。

### 3.2 渲染验证

`packages_grab_window.exe <out.png> <list|tasks|review|settings> <style> <light|dark>`：

| 文件 | 核对点 | 结果 |
|---|---|---|
| build/shots/v5-tasks.png | 每行按钮组：Partial failure → 重试/移除/查看日志(7)；Queued → 开始/移除/查看日志(1) | 符合 |
| build/shots/v5-tasks.png | 行内日志面板：7 行，时间/阶段/文本/退出码，`error` 红、`warning` 黄、`ok` 绿 | 符合 |
| build/shots/v5-tasks.png | 页脚改为"队列运行中 · 0 个进行中 · 退出码 0 不等于成功"，选择控件消失 | 符合 |
| build/shots/v5-tasks-dark-fluent.png | 深色下日志面板为深底浅字（此前用了不存在的 token，落到白底不可读） | 符合 |
| build/shots/v5-review.png | 审阅页顶部"动作"分段：升级（选中）/ 卸载，右侧给出"检测到更新可用，可升级到 14.1.2" | 符合 |
| build/shots/v5-review.png | 切到卸载后：方式变 `winget uninstall --id … --exact`、材料变"无需下载材料"、结果核对变"仍在列表中即视为卸载失败"、源协议消失、勾选项文案变"理解影响" | 符合 |

四种风格 × 深浅色的任务页均渲染正常（`build/shots/v5-tasks-{brand,tactile,liquid}.png` 与 `build/shots/v5-tasks-dark-fluent.png`）。

### 3.3 几何核对

隐藏搜索框的间距根因用 `PACKAGES_GRAB_DEBUG=1` 的 item 树 dump 确认：

```
QQuickText 42x18 @0,10 vis=1          // "管理器"
QQuickRectangle 191x24 @179,7 vis=1   // managerSeg
TextField_QMLTYPE_3 200x40 @0,0 vis=0 // 隐藏的搜索框，仍有 200 宽
```

修复（`Layout.fillWidth: visible` + `Layout.preferredWidth: visible ? -1 : 0`）后，mockup 中同一行的实际间距为 8 px（浏览器内测得 `segLeft - lblRight = 8`）。

### 3.4 静态校验

`python tests/validate.py` 在重新打包前报 `packaged binary differs`（二进制已重编、包未更新），这是预期；重新打包后通过。

### 3.5 Scoop 定位（本轮新增，真实证据）

报告：插件显示 `Scoop 未找到 (scoop.ps1 was not found at C:\Users\Default\scoop\shims)`，而 `scoop list` 在本机正常。

| 检查 | 结果 |
|---|---|
| 真实 Scoop | `C:\Users\renfei\scoop\shims\scoop.ps1` 存在 |
| 注册表真实 profile | `ProfileList\S-1-5-21-…-1001 → C:\Users\renfei` |
| `C:\Users\Default\scoop` | 不存在 |
| 宿主提权环境 | `elevatedEnvironment()` 用 `CreateEnvironmentBlock(nullptr)` 取系统变量，再用 `FOLDERID_Profile` 填 `USERPROFILE`；未加载用户 profile 时该已知文件夹即 `C:\Users\Default` |

所以插件不能再信任 `USERPROFILE`。新的解析顺序与实测（`tools/probe_scoop.cpp`，真实跑 `scoop export` 并过解析器）：

| 场景 | 根 | 来源 | export | 解析出的包 |
|---|---|---|---|---|
| 正常用户环境 | `C:\Users\renfei\scoop` | `profile`（注册表） | exit 0，3945 字节 | 6 |
| 模拟提权：`USERPROFILE=C:\Users\Default` | `C:\Users\renfei\scoop` | `profile`（注册表） | exit 0，3945 字节 | 6 |
| 模拟提权 + `PATH` 去掉 scoop | `C:\Users\renfei\scoop` | `profile`（注册表） | exit 0，3945 字节 | 6 |

L1 `scoop root resolution (PKG-F19)` 覆盖 8 个分支（`SCOOP` 优先、profile、PATH 上的 shim、正斜杠 shim、`ProgramData`、全无、无 `\shims\` 的假 shim、大小写保留），共 203 检查。

补充修正：`openWindow()` 现在只要任一后端不是 `Ready` 就重新扫描，避免磁盘缓存里的 `scoop: missing` 在 `refreshMinutes` 内继续显示。

### 3.6 已安装页无法操作（本轮新增，真实证据）

报告三点：发现/已安装无法勾选、没法卸载、计划都失败。其实是同一条链上的两个独立 bug。

| 层 | 缺陷 | 后果 |
|---|---|---|
| `WingetAdapter::buildMutation` | `isResolvable(plan.key)` 用了默认参数（要求已知 scope），而 `winget list` 不打印 scope | 每次执行都被拒 `identity is not exactly resolvable` → 任务页「计划都失败」 |
| `WingetAdapter::fromListText` | `canUninstall = false` 一律写死；`canUpgrade` 依赖 `winget list` 不提供的 available | 三个标志全 false → `actionable=false` → 勾选框 `enabled:false`、35% 透明 → 整页点不动 |
| 更新行（WinGet upgrade / Scoop manifest） | 没有置 `canUninstall` | 更新页同样不可操作 |

修复后的判定规则：

| 动作 | 需要的身份信息 | 理由 |
|---|---|---|
| install | manager + source + id + **scope** | 必须传 `--scope` |
| upgrade | manager + source + id | `--id X --exact` 自己解析实例 |
| uninstall | manager + source + id | 同上 |

另外，刷新时把更新清单的目标版本按精确身份合并回已安装行（`winget list` 本身不提供 available），否则已安装行永远没有升级可用。

证据：

- `tests/fixture_tests.cpp` 的 `winget command building` 新增断言：未知 scope 的 install 被拒；未知 scope 的 uninstall/upgrade **可以**构建 argv，且 argv 不含 `--scope`。
- 真实 `winget list` fixture 的逐行断言改为：ARP 行 `canUninstall=false`，其余已安装行 `canUninstall=true`（并至少存在一个），检查数 2755 → 2843。
- 渲染：`build/shots/v6-review-unknown.png` 是 `isOfficeAI.OfficeCLI`（scope `unknown`）的卸载审阅页——可以勾选、动作选择器可用、**没有红色阻塞**，只等勾「理解影响」；而修复前同一行是 `scope is not reported by WinGet; a mutation needs an exact scope` + 全部灰色。

## 4. 语义边界（本轮明确下来的）

- **暂停 ≠ 取消**：暂停只阻止队列取下一个任务，正在跑的继续跑。取消是另一个显式动作。
- **重试 ≠ 重放**：失败任务点重试不会重跑旧记录，而是用同一身份重新打开审阅页让用户再确认一次（事实可能已变）。
- **移除 ≠ 卸载**：移除只删 UI 记录，不碰机器状态；运行中的任务不能移除，因为那条记录是"仍有进程在动这台机器"的唯一证据。
- **卸载没有源协议**：卸载不读远程来源，也不安装任何东西，所以只要求勾选"理解影响"。
- **退出码 0 不等于成功**：执行后仍要重新读取已装状态；卸载后"仍在列表中"即判失败。

## 5. 已知限制

- 执行路径的证据是 fixture/L1 与渲染验证，**没有在真机上跑过一次真实的安装/卸载**（按约定不在开发机执行真实变更）。
- 日志只在内存，进程重启即丢；不写盘（日志可能含本地路径）。
- 队列仍是一条全局串行通道（每个管理器一条 lane 的规则保留在核心，执行器当前一次只跑一个任务），没有做多任务并发执行。
- 并行刷新与任务日志的结构已在 `mockup/layout-v4.html` 中演示并从本轮的 `pinsCached()` 开始落地，但刷新尚未做到设计的 5 域全并行。
