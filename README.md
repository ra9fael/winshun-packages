# Win顺 Packages

[English](README.en.md) · [设计](plugin-design.md) · [界面规格](UI-SPEC.md) · [测试计划](TEST-PLAN.md)

`ra9fael.packages` 是 Win顺 的软件包管理插件：在一个窗口里查看、安装、卸载、重装和升级 Scoop 与 WinGet 软件包。当前版本 **0.4.12**。

插件以**普通用户权限**运行（清单不含 `elevated`），查询只产生结果，任何变更都要先选中精确身份、审阅计划、显式确认后入队执行。WinGet／安装器需要管理员权限时由它们自己请求 UAC；插件不自行提权、降权或换权限重试。

## 已实现

- **已安装**：WinGet / Scoop 两个来源合并展示，同名不合并；区分后端记录项与被枚举项（ARP/MSIX）；可本地筛选。
- **发现**：异步搜索，丢弃迟到结果；搜索结果作用域未知时进入审阅后必须先选「当前用户 / 所有用户」，否则不能确认。
- **更新**：按后端证据列出目标版本，尊重 `winget pin list` 的 pin/hold。
- **任务**：每个变更独立授权；按管理器串行；阶段显示 `install/uninstall › verify`，结束后核对状态而不是只看退出码；失败、部分成功、结果未知分别呈现。
- **诊断**：完整命令行与后端输出写入任务日志（脱敏路径与令牌）；`0x8A15007D`（管理员进程处理用户作用域包）、`0x87AF0008`（WinGet 用户数据库不可写）都有明确说明。
- **设置与动作**：窗口设置弹层可编辑七项宿主偏好并即时同步；`packages` 搜索动作支持打开（遵循打开策略）、仅打开缓存、强制刷新和设置。设置与仅打开缓存不因打开动作查询后端。
- **缓存**：清单、更新、搜索结果、包详情、pin 列表、后端探针结论分别落盘，按 `refreshMinutes` 与各自失效条件复用；0 分钟表示缓存始终过期；任一后端故障时只重查该后端。插件启动扫描指插件被按需启动后的后台查询，不代表 Win顺 启动时预加载插件。

## 截图

<details>
<summary>已安装列表</summary>

<img width="982" height="586" alt="installed" src="https://github.com/user-attachments/assets/410d0cd2-9877-4735-a0fe-d23d0c1900dc" />

</details>

<details>
<summary>发现页搜索</summary>

<img width="982" height="586" alt="search" src="https://github.com/user-attachments/assets/19829160-3ff9-4c17-af59-18fdf1a6a4e9" />

</details>

<details>
<summary>更新列表</summary>

<img width="982" height="586" alt="update" src="https://github.com/user-attachments/assets/65bbe915-965c-450c-a15d-d91ef542ee26" />

</details>

<details>
<summary>安装审阅</summary>

<img width="982" height="586" alt="Request" src="https://github.com/user-attachments/assets/c309867c-4959-46fa-8a73-17dc87596f11" />

</details>

## 权限

| 权限 | 用途 |
|---|---|
| `ui` | 插件窗口、主题与语言 |
| `processes` | 调用 `winget` / `scoop`（PowerShell）并读取其输出 |
| `network` | 由 WinGet／Scoop 自身下载软件包与源数据；插件不发起额外网络请求 |
| `clipboard:write` | 通过宿主 `clipboard_copy` 复制包 ID、日志、路径 |
| `files:read` / `files:write` | 只读写插件自己的数据目录（`%LOCALAPPDATA%\WinShun\plugins\ra9fael.packages.data`）保存缓存 |

## 构建与发布

需要 Windows x64、MSVC 与 Qt 6.12（使用宿主共享 Qt，插件不打包 Qt DLL）：

```powershell
cmake -S . -B build\dev2
cmake --build build\dev2 --target packages
python tools\package.py --out dist\release-0.4.12   # 生成 <id>-<version>.wsplugin
```

正式包以 `<id>-<version>.wsplugin` 命名，附在对应 tag 的 GitHub Release 上；插件库条目由 `WinShun-plugins` 仓库的 `tools/entry.py` 从该附件生成。本插件是 C++ + 宿主共享 Qt，构建在本地以固定提交完成，不使用只打包脚本的通用发布工作流。

## 验收需求（规格附录）

以下 PKG-Fxx 是最初的需求与验收条目，仍然作为行为规范；实现进度以「已实现」一节和 [更改日志](CHANGELOG.md) 为准。

### 范围与入口

拟从 Win顺 关键词 `packages` 打开独立窗口，默认进入 Installed。主导航为 Installed / Discover / Updates / Tasks，可筛选 All / Scoop / WinGet。查询只产生结果；安装等变更必须选择明确包、查看刷新后的计划、确认并进入队列。缺一个管理器时仍可使用另一个；不自动安装管理器、PowerShell 模块或其他可选依赖。

P0 优先完成正常用户权限下的 installed / search / details / update-list，以及 selected install / uninstall / reinstall / upgrade 和任务队列。P1 包含 source / bucket 只读诊断、失败恢复辅助及显式元数据维护入口；P2 才考虑 source / bucket / pin / hold 的完整增删改管理。P0 必须读出并尊重已有 pin / hold，不能以“后续支持管理”为由忽略它们。P0 不提供隐含 upgrade-all、跨管理器迁移、自动修复、自动同意协议、默认 purge 或自动权限提升。

### 功能条目

PKG-Fxx 是 FR 标识。优先级不改变安全约束；后续拆分时保留原 ID，以新增 ID 扩展。下列 30 项由 [测试计划](TEST-PLAN.md) 逐项追踪。

| FR | 优先级 | 需求与可观察验收 |
|---|---|---|
| PKG-F01 | P0 | 检测 backend 路径、版本、调用类型及能力；缺失、拒绝、unsupported 独立呈现，不下载依赖；一个失败不阻塞另一个。 |
| PKG-F02 | P0 | Installed 展示已安装项及证据来源；WinGet list 中仅被枚举/匹配的项目标为 observed 或 matched，不能仅凭出现就标 managed。 |
| PKG-F03 | P0 | Discover 异步搜索，防抖、取消旧请求并丢弃迟到结果；键入、选中、按 Enter 查看详情均不安装。 |
| PKG-F04 | P0 | Details 展示 manager、source/bucket、精确 ID、scope、版本、架构、信任/归属证据和能力限制；未知字段明确写 unknown。 |
| PKG-F05 | P0 | Updates 按 backend 的可用更新证据列出目标版本、unknown、hold/pin 和扫描时间；不得把字符串大小比较当版本升级判断。 |
| PKG-F06 | P0 | 稳定身份为 manager + source/bucket + id + scope；同名、同 ID 不同源/作用域不能合并。跨管理器疑似同软件仅提示，不自动互卸。 |
| PKG-F07 | P0 | 选中安装只针对刷新并精确解析的一个身份；展示依赖、目标版本/作用域及可知副作用，确认后才入队；歧义或目标不明则阻塞。 |
| PKG-F08 | P0 | 选中卸载显示精确已安装实例、归属证据、数据风险及 scope；非 WinGet 安装的 observed 项仅在卸载目标经验证唯一并明确确认时开放；默认不 purge。 |
| PKG-F09 | P0 | 选中重装按 backend/version 能力表执行；先核实原版本或明确另选版本的可取得性，未经验证不 force install；显式卸载后安装须提示非原子、数据风险和中间失败。 |
| PKG-F10 | P0 | 选中升级遵守 hold/pin、scope 和 unknown-version 策略；未知版本必须人工检查并单项再确认，不能隐式包含在批量操作。 |
| PKG-F11 | P0 | 每个 manager 的变更严格串行，同一安装实例跨 backend 的疑似冲突也需暂停复核；可排队、移除未开始项、查看顺序和阻塞原因，不用 manager 的通配全量更新代替所选任务。 |
| PKG-F12 | P0 | 确认前和出队执行前 refresh plan；源、版本、scope、hold/pin、协议、归属或依赖发生变化，原确认失效并要求重新审阅。 |
| PKG-F13 | P0 | 区分排队取消、查询取消、运行中取消；运行取消可能只停止等待或子进程，不能保证安装器停止或系统回滚；必须刷新实际状态。 |
| PKG-F14 | P0 | 任务显示阶段、估计进度、退出码、结果核对及可脱敏诊断；没有可靠总量时用不定进度，不捏造百分比。 |
| PKG-F15 | P0 | 权限拒绝、interactive installer pending、需要重启、结果未知、重装已卸载但安装失败均为独立状态；进程退出不等于操作成功。 |
| PKG-F16 | P0 | source agreements 与 installer agreements 分开列明并显式同意，拒绝则阻塞相关操作；搜索遇协议不能静默同意，且不得全局记作同意所有协议。 |
| PKG-F17 | P1 | normal + resident + lazy，声明 ui/processes/network/clipboard:write/files:read/files:write；不声明 elevated，插件通过宿主普通用户执行通道运行，不继承管理员权限。WinGet / 安装器按需请求 UAC，拒绝或不支持时停止，不自动换权限重试；Scoop global 在无管理员执行通道时阻塞。标题栏显示实际权限。 |
| PKG-F18 | P0 | Scoop 识别 PowerShell 脚本/shim 调用，使用可信固定入口与结构化参数传递；WinGet 用安全 argv。用户文字不得拼入 shell，不接受任意脚本、URL manifest 或额外 CLI 参数作为包 ID。 |
| PKG-F19 | P0 | 解析按 backend/version/命令/语言/编码 fixture 适配；无证据的组合标 unsupported，不猜列。WinGet 结构化 API/模块是显式可选依赖，不自动安装，不假定所有 CLI 命令支持 JSON。 |
| PKG-F20 | P0 | 缓存带观测时间与 stale 标记；检测外部 manager 操作/锁冲突并失效刷新。无法可靠检测时展示不确定，执行前重查，不强抢锁。 |
| PKG-F21 | P0 | 网络、管理器自身进程及插件文件读写都按真实副作用告知；日志脱敏，不收集遥测，不默认读任意用户文档；设置与任务记录只写插件数据目录。 |
| PKG-F22 | P0 | 空态、加载、错误、部分成功、离线/stale、unsupported 各有可读提示和恢复动作，不把失败展示成“无软件”。 |
| PKG-F23 | P0 | 紧凑窗口支持键盘全流程、主题、DPI/缩放与屏幕阅读器；操作前不只靠颜色/图标表达风险，焦点返回来源项。 |
| PKG-F24 | P0 | 宿主退出/插件禁用/崩溃后不自动重跑变更；恢复时任务标 interrupted/unknown，并对照实际安装状态后由用户重新规划。 |
| PKG-F25 | P0 | 性能目标与内存、输出、队列/缓存容量由隔离环境实测峰值加有依据余量决定；保留测量条件与取消响应数据，不能凭空承诺固定上限。 |
| PKG-F26 | P0 | 所有动态验证仅在隔离 VM 和可丢弃包执行；L0–L5 证据可追溯，未执行明标；L3 不过不称实现完成，L4 权限/安装器路径未过不开放相关能力。 |
| PKG-F27 | P1 | 提供 source/bucket 只读状态、源身份与失败诊断；源刷新/管理器自更新视作独立有副作用的计划，不能藏在搜索按钮里。 |
| PKG-F28 | P2 | source/bucket 完整管理须独立设计确认、信任与刷新失效流程；P0 不自动添加缺失源或 bucket。 |
| PKG-F29 | P2 | pin/hold 全量管理须独立确认和审计；P0 只读并尊重已有策略，禁止用 force/忽略 pin 绕过。 |
| PKG-F30 | P0 | 中英文 README、设计、界面和测试实质一致互链；设计事实/推断/未验证分开，发布前补能力版本表和真实证据。 |

## 管理器语义与重装边界

官方 WinGet 文档已列出 search / list / show / install / upgrade / uninstall / repair；没有列出 reinstall。不得生成 `winget reinstall`，repair 不能作为重装。不能宣称全命令均支持 `--output json`。可选结构化途径需单独验证版本与部署条件。

Scoop 官方源码在修订 `e6aa3b366bdee8ed138c1e0f7b85192ebdd35d0f` 中表明：`update` 无参数更新管理器和 buckets；`update <app>` 升级指定应用，可能先同步管理器/元数据；`install` 没有 force 选项；`update --force` 不保证原版本，尤其 @version pin 可能被重新解析到 bucket HEAD；`reset` 重建链接、shims、环境等，不重新下载安装包。详见 [设计事实和能力表](plugin-design.md)。源码阅读不是本机运行验证。

重装计划先确认精确身份和目标版本；原版本不可取得时停止，或由用户显式另选版本并重新确认。卸载前需验证受支持的获取/暂存机制、manifest、架构、依赖、安装材料完整性与许可；若 backend 无法做到，禁用重装，不能先卸载再找包。即使完成预检也不能保证后续安装成功、数据保留或自动回滚。默认不 purge；安装器/卸载器自身仍可能删数据。

## 文档与验证状态

依据：[工作台规范](../DEVELOPMENT.md)、[宿主权限与 Job](../WinShun/docs/plugins.md)、[WinGet 官方文档](https://learn.microsoft.com/en-us/windows/package-manager/winget/)、[Scoop 官方源码](https://github.com/ScoopInstaller/Scoop/tree/e6aa3b366bdee8ed138c1e0f7b85192ebdd35d0f/libexec)。已完成文档及必要官方源码只读核实；没有运行包管理器或安装卸载，没有执行 L0–L5 动态测试。

未决：宿主 Job 下各安装器/UAC broker 的可运行性；支持的具体管理器版本/语言 fixture；可选 WinGet API 的依赖策略；原版本安装材料的可靠预取能力；unknown/observed 项的精确 scope 与卸载定位；性能与容量标定。当前所有可执行能力均未验收，进入实现前需完成设计审批。
