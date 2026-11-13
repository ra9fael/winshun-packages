# Win顺 Packages（需求草案）

[English](README.en.md) · [设计](plugin-design.md) · [界面规格](UI-SPEC.md) · [测试计划](TEST-PLAN.md)

`ra9fael.packages` 拟提供 Scoop / WinGet 一站式软件包查询、安装、卸载、重装和升级界面。当前仅需求阶段：没有插件实现、清单、脚本、配置、构建产物或可用安装包。本文中的行为都是拟议验收要求，不能当作已提供的功能。

## 范围与入口

拟从 Win顺 关键词 `packages` 打开独立窗口，默认进入 Installed。主导航为 Installed / Discover / Updates / Tasks，可筛选 All / Scoop / WinGet。查询只产生结果；安装等变更必须选择明确包、查看刷新后的计划、确认并进入队列。缺一个管理器时仍可使用另一个；不自动安装管理器、PowerShell 模块或其他可选依赖。

P0 优先完成正常用户权限下的 installed / search / details / update-list，以及 selected install / uninstall / reinstall / upgrade 和任务队列。P1 包含 source / bucket 只读诊断、失败恢复辅助及显式元数据维护入口；P2 才考虑 source / bucket / pin / hold 的完整增删改管理。P0 必须读出并尊重已有 pin / hold，不能以“后续支持管理”为由忽略它们。P0 不提供隐含 upgrade-all、跨管理器迁移、自动修复、自动同意协议、默认 purge 或自动权限提升。

## 稳定功能需求

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
| PKG-F17 | P1 | normal + resident + lazy，声明 ui/processes/network/elevated/files:read/files:write 的实际用途；子进程仍受 Job 限制。`elevated` 让插件跟随 Win顺 的管理员状态（需用户在宿主设置里确认一次），machine/global scope 才可执行；未提权时这些 scope 在预检阻止并说明原因。UI 标题栏的权限 chip 反映运行时真实状态。 |
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
