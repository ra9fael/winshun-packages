# 开发工具：packages_dev.exe

[English](dev-harness.en.md)

插件是常驻进程，由 Win顺 通过 stdio 驱动。因此每验证一个改动都要走「编译 → 打包 → 安装 → 重启宿主 → 开窗口」，一轮只给一个数据点。而已经暴露的问题全是**运行时才成立的事实**：

| 已发生的缺陷 | 打包流程能提前发现吗 |
|---|---|
| 提权环境里 `USERPROFILE` 是 `C:\Users\Default` | 不能，只有真机跑才暴露 |
| `winget list` 不打印 scope，却按 scope 判定可执行 | 不能，要靠真实输出 |
| 隐藏控件仍占 `Layout.fillWidth`，按钮被推开 137px | 不能，只能几何核对 |
| `theme.controlFill` 不存在，深色下不可读 | 不能，肉眼也难看准 |
| Basic 样式按钮不跟随主题，深色下是亮块 | 不能 |

`packages_dev.exe` 是同一个窗口，不带宿主。它链接**完全相同**的 Controller、模型、适配器、platform 层与 QML 场景，所以它显示什么，插件就显示什么。

## 1. 用法

```powershell
.\run-dev.ps1                            # 打开窗口，真实后端
.\run-dev.ps1 -Fake                      # 确定性假数据，不等 winget
.\run-dev.ps1 -Fake -Watch               # 存盘即重启，改 QML 不用手动启动
.\run-dev.ps1 -SimulateElevated          # 复现提权环境
.\run-dev.ps1 -Lint                      # 布局体检，有缺陷退出 1
.\run-dev.ps1 -Plan isOfficeAI.OfficeCLI # 只打印 argv，什么都不跑
.\run-dev.ps1 -Review 4:uninstall        # 打开第 4 行的卸载审阅页
.\run-dev.ps1 -Shot out.png -Fake        # 出图后退出
.\run-dev.ps1 -RealDataDir -ExpectRows 373  # 断言缓存画出了 373 行
.\check-layout.ps1                       # 6 个页面 × 深浅两色，全量布局体检
```

`check-layout.ps1` 把上表的 `--lint` 铺满所有能显示的界面（已安装 / 发现 / 更新 / 任务 / 审阅-升级 / 审阅-卸载）× 深浅两色，共 12 组，任一组有缺陷即退出 1。它用 `--fake`，因为被测的是几何，不是后端。

它**直接**启动 `packages_dev.exe`，不套 `run-dev.ps1`：逐例嵌套调用 PowerShell 脚本时会在若干轮之后挂住，而"挂住"和"构建慢"在 CI 里无法区分。直接启动还让每一例都有硬超时（`-TimeoutSec`，默认 30 秒），卡住会记为失败而不是永久阻塞。

`--quit-after` 与捕捉类开关同时使用时有一个契约：捕捉走自己的退出路径，所以如果 `--quit-after` 先到，说明捕捉根本没跑（通常是把 `--shot-delay` 设得比 `--quit-after` 还长）。这种情况退出 1 并说明原因，而不是安静地报成功却没有文件。

不需要每次手工 build：脚本会先用 `vcvars64` 设好环境再调用 cmake（`run-dev.ps1` 是唯一同时做这两件事的地方，`-NoBuild` 可跳过）。

## 2. 开关

| 开关 | 作用 |
|---|---|
| `--style` `--theme` `--size` `--locale` | 注入完整 `nativeLook`（4 风格 × 深浅 × 4 档大小） |
| `--tab` | 打开时停在 已安装 / 发现 / 更新 / 任务 |
| `--qml DIR` | 从 DIR 读取 QML（默认即 `src/ui/qml`，这是改 QML 不必重编的原因） |
| `--fake` | 用确定性快照代替后端；UI 迭代不受 `winget list` 1.6s 影响。它同时关掉所有后端调用（扫描与搜索都被短路），所以截图完全由注入数据决定，不会出现"正在查询…" |
| `--watch` | 监视 `src/ui/qml/*.qml`，改动即重启（见 §5） |
| `--simulate-elevated` | 把 `USERPROFILE` 设为 `C:\Users\Default` 并从 PATH 去掉 scoop |
| `--data-dir real` | 默认写 `ra9fael.packages.dev.data`，只有这个开关才碰真实缓存 |
| `--dump-plan ID` | 打印该 id 的 uninstall/upgrade/install 会构建的 argv 后退出 |
| `--lint` | 布局体检，有缺陷退出 1 |
| `--refresh` | 缓存新鲜时也强制扫描。`openWindow()` 在 `refreshMinutes` 内不查询（对插件正确、对测试无用），而"一次新扫描产出了什么"（尤其是更新清单是否落盘）只有强制扫描才能验 |
| `--review N[:uninstall]` | 选中第 N 行并打开审阅页（附加 `:uninstall` 再切到卸载），审阅页的布局只有这样才能被检查 |
| `--search TEXT` | 在发现页输入 TEXT 并执行查询 |
| `--installed-filter TEXT` | 在已安装页的筛选框输入 TEXT（纯本地过滤，不跑进程） |
| `--detail-action ROW:ACT` | 像用户那样点详情面板的动作：**只选中**第 ROW 行（不勾选）再执行 ACT（`upgrade` / `uninstall` / `install`）。这条开关复现过"选中后点升级崩溃" |
| `--tab-cycle-check` | 与 `--fake --search TEXT` 联用：验证发现结果里的已安装 Neovim（不同大小写 ID）被识别，并且发现页搜索词与结果在已安装→发现切换后仍保留 |
| `--discover-filter new\|all\|installed` | 发现页的「结果」筛选 |
| `--select 0,2,5` | 勾选指定行，用于检查选择工具条 |
| `--expect-rows N` | 窗口刚打开、后端尚未回应时模型必须正好 N 行，否则退出 1 || `--dump-tree` | 打印 item 树几何 |
| `--shot FILE` | 出图后退出 |
| `--shot-delay MS` | 出图前等待（真实后端要十几秒才回来；默认 700ms） |
| `--log-level quiet\|info\|debug` | stderr 结构化输出 |
| `--quit-after SEC` | N 秒后退出（冒烟） |

## 3. `--lint` 抓到了什么

它不是「看起来对不对」，而是把已发生的缺陷编码成规则：

**规则 1：容器没有绘制祖先。** 一个声明了大小的容器若祖先链上没有 `Rectangle`，它的空隙会透出窗口默认底色——深色主题下就是白块。这条规则发现 `SplitView` 与 `TasksPage` 根节点都没填色。

**规则 2：子项比父项宽。** 超过 4px 即报，说明被裁剪或重叠。

**规则 3：`管理器` 与分段按钮的间距。** 超过 40px 即报。这条规则当场抓到 **137px**：

```
LINT filter-gap: 管理器 ends at 54 but the manager buttons start at 191 (gap 137 px)
```

根因不是隐藏的搜索框，而是 `RowLayout` 对「内层 Row 居中于自身的 Rectangle」测量错误。换成 `Row` 后间距回到 8px。

> 这条规则的价值就在于：我先前用肉眼看截图，两次都判定「没问题」。

## 4. `--dump-plan`：安全地看会发什么命令

```
> .\run-dev.ps1 -Plan isOfficeAI.OfficeCLI
action uninstall state draft
  winget.exe uninstall --id isOfficeAI.OfficeCLI --exact --disable-interactivity
action upgrade   state blocked  blocked: target version is unknown or not orderable
action install   state blocked  blocked: identity is not exactly resolvable (scope must be known)
```

这同时证明了「unknown scope 不再阻止卸载」和「install 仍要求 scope」。它不启动任何变更进程。

## 5. `--watch` 为什么是重启而不是热重载

进程内销毁并重建场景会崩溃（`0xC0000005`）：`initializeUi` 把 engine parent 到 controller，而 scene 的 context 拥有模型，销毁窗口会留下多处悬空引用。

为热重载重写这套生命周期，是拿一条已验证的路径换一条脆弱的路径。所以 `--watch` 检测到改动后以退出码 **75** 退出，由 `run-dev.ps1` 重启进程。一次启动约 2 秒，插件的生命周期模型一点没被改动。

## 6. 它不能替代的

必须说清，否则会误以为可以完全抛开 wsplugin：

| 只有宿主能给 | 说明 |
|---|---|
| 真实 `nativeLook` | 工具里的值取自宿主 `Look.qml`，但真实字段命名/缺省仍需实测 |
| `elevated` 真实提权 | 工具只能模拟环境变量，不是真的提权令牌 |
| 协议往返 | initialize / appearance / settingsChanged / shuttingDown |
| 宿主权限门禁与设置页 | 工具里没有这些 |

所以里程碑是「harness 全绿 → 做一次宿主往返」，不是「harness 绿 = 可发布」。

## 7. `--expect-rows`：缓存是否真的在起作用

这是工具第二次抓到「肉眼看不见」的缺陷。缓存本应在 `refreshMinutes` 内让窗口秒开，但实测冷启动后列表全空、必须点刷新才有行。

根因：`loadCache()` 在构造函数里调用，而它末尾的 `rebuildRows()` 在 `m_packages == nullptr` 时直接返回——那一刻模型还没挂上。缓存读进来了，却没人把它变成行。

`--expect-rows` 在**事件循环启动之前**检查模型行数，此时任何后端回应都不可能到达，所以行数只可能来自磁盘缓存：

```
> .\packages_dev.exe --data-dir real --expect-rows 373 --quit-after 2
rows  373 rows from the cache, before any backend reply
```

把 `expect-rows` 写错就退出 1，所以它既是断言，也是自检——`--expect-rows 99999` 会打印 `wanted 99999 rows ... found 373` 并退出 1。

## 8. 不随包分发
`packages_dev.exe`、`tools/look_fixture.*`、`tools/dev_main.cpp`、`run-dev.ps1`、`check-layout.ps1` 都**不在** `tools/package.py` 的白名单里。它们是开发工具，不是插件的一部分；`packages.exe` 也没有 `--dry-run` 这类开关。

`initializeUi` 新增的 `qmlSourceDir` 参数只在开发工具里非空；插件传空串，走 qrc 资源，与之前完全一致。
