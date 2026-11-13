# Packages 插件设计（需求阶段）

[English](plugin-design.en.md) · [需求/FR](README.md) · [UI](UI-SPEC.md) · [测试](TEST-PLAN.md)

## 1. 关键裁决（Verdict）

统一的是查询和操作计划，不能抹平 backend 的身份、权限及命令语义。每次变更都必须基于可验证的精确身份与当前状态，由用户确认；不支持的版本或安装器路径保持禁用。当前仅编写文档，未经批准不进入实现。

范围与优先级见 README 中 PKG-F01–PKG-F30。P0 是正常用户权限的日常查询和所选包操作；全量 source/bucket/pin 管理延后。这里的架构、文件布局和协议接线均是拟议方案，不是已有代码。

## 2. 依据的实测事实（Grounded facts）

本次只有文档/源码阅读核实，没有本机运行实测。

| 证据 | 可确认内容 | 证据强度/限制 |
|---|---|---|
| [工作台规范](../DEVELOPMENT.md) | 设计 11 节、双语、L0–L5 和 L3 门槛 | 本地文档事实 |
| [宿主权限](../WinShun/docs/plugins.md) | normal 中完整性、默认去管理员令牌；processes 允许子进程但仍在 Job；宿主退出结束作业；network/files:read 只是声明，normal 的 files:write 也只是声明 | 文档事实，不代表任意安装器可运行 |
| 同上 | elevated 仅随管理员宿主及已确认副本，不为插件弹 UAC；不等于“允许安装器提权” | 文档事实；本设计不请求 elevated |
| [WinGet 官方总览](https://learn.microsoft.com/en-us/windows/package-manager/winget/) | search/list/show/install/upgrade/uninstall/repair；未列 reinstall | 本轮读取的官方文档依据；没有推导全命令 JSON 能力 |
| Scoop 官方 help 源码 | help 从 command_path 对应源码提取 Usage/Help | 通过串行只读 gh api 取得固定修订 |
| Scoop 官方 update 源码 | 无参数调用 Sync-Scoop 与 Sync-Bucket；app 分支可能先同步 Scoop/buckets；`update <app>` 是升级，hold 项跳过 | 源码事实；可能涉及 Git/配置/元数据写入，不能标只读 |
| Scoop 官方 install 源码 | 帮助支持 app@version，无 force；已装版本会被跳过；默认可能自更新，存在 `--no-update-scoop` | 版本选择帮助不保证任意历史安装材料存在 |
| Scoop 官方 update/reset 源码 | force update 从当前 manifest 取目标版本，@version pin 可能转 bucket HEAD；reset 恢复 current、shims、快捷方式、环境与 persist 链接 | force 不保证原版，reset 非重装 |

Scoop 证据固定为 `e6aa3b366bdee8ed138c1e0f7b85192ebdd35d0f`，只读取提交号与必要四个文件，不克隆全仓：

- [scoop-help.ps1](https://github.com/ScoopInstaller/Scoop/blob/e6aa3b366bdee8ed138c1e0f7b85192ebdd35d0f/libexec/scoop-help.ps1)
- [scoop-update.ps1](https://github.com/ScoopInstaller/Scoop/blob/e6aa3b366bdee8ed138c1e0f7b85192ebdd35d0f/libexec/scoop-update.ps1)
- [scoop-install.ps1](https://github.com/ScoopInstaller/Scoop/blob/e6aa3b366bdee8ed138c1e0f7b85192ebdd35d0f/libexec/scoop-install.ps1)
- [scoop-reset.ps1](https://github.com/ScoopInstaller/Scoop/blob/e6aa3b366bdee8ed138c1e0f7b85192ebdd35d0f/libexec/scoop-reset.ps1)

### 重装能力表：按 backend/version 判定

能力记录键至少含 backend、精确版本/修订、OS、命令/选项 help 摘要、调用形式、scope、语言、安装器类型和 L3/L4 证据。表中候选不是已支持能力；不能以“最新版本”作为覆盖所有版本的证据。

| backend/version | 原版本 force install | 重装候选与当前裁决 |
|---|---|---|
| Scoop，上述固定修订 | install help 无 force，禁用该路径；update force 不保证原版，reset 不适用 | 仅考虑显式 uninstall 后 install 的两阶段计划。先验证原版本材料可取得/暂存与完整性；help 有 @version 不代表材料可取得。尚未运行，禁用执行。 |
| Scoop，其他版本/修订 | 必须取得该版本 help 并验证同版本行为，缺证据禁用 | 不继承上述实现细节；重新建立能力记录，必要时仍采用经预检的两阶段计划。 |
| WinGet，当前尚未选定支持版本 | 官方总览不足以证明 force install 会重装原版本，禁用该路径 | 选定具体版本后核实 install 的版本/force/精确定位行为及材料获取机制。否则只考虑经预检的显式 uninstall/install；没有 `winget reinstall`；repair 独立且不替代重装。 |
| WinGet，未来每个新增版本 | 必须由该版本 help + fixture + L3/L4 证明原版本行为才可开放 | 证据缺失、定位歧义或材料不可取得时，禁用重装，保留只读查询。 |

两阶段重装不可原子执行：卸载失败则停止；卸载成功但安装失败标 removed/install failed，保留目标版本、已用材料及诊断，刷新实际状态，给出手工恢复信息。原版本不可取得只能停止或显式改选目标版本，不能默默升级。无默认 purge，无数据保留/回滚承诺。force 路径即便未来获准，也需说明安装器副作用，不允许绕过 hold/pin。

## 3. 架构

拟采用自带可执行运行时、独立紧凑 UI 的 resident 插件，normal sandbox、lazy 首用启动。UI 主线程只渲染；工作线程/异步进程负责查询与操作，互不阻塞 stdio JSON-RPC。运行时与 UI 工具包选择尚待依赖/打包评审。

拟议组件：Host bridge → UI model → Query coordinator / Plan service → Scoop adapter、WinGet adapter → Process runner。Mutation scheduler 统一接收不可变计划，Evidence store 保存脱敏事件。纯核心只接收数据和事件；OS 层处理进程、Job、文件和可选 API。

拟议文件职责：未来 `plugin.json` 声明元数据/权限/命令，`src/core/` 放身份/计划/状态机，`src/adapters/` 放版本适配，`src/platform/` 放 Windows 进程层，`src/ui/` 放视图，`tests/fixtures/` 放脱敏输出；本轮不创建这些文件。manifest 意向仅以文字说明：id `ra9fael.packages`、mode resident、start lazy、sandbox normal、command `packages`，权限见第 6 节；不编写配置。

宿主 initialize 用于获得语言、外观、数据目录；appearance 通知更新 UI。关键词 query 只返回“打开 Packages”或只读结果，action 只打开窗口/详情，变更确认必须在独立窗口完成。stdout 仅协议，日志写 stderr 或脱敏记录。具体 SDK 接线/minWinShun 尚待实现前核实，不能虚构新的宿主 RPC。禁用、退出、超时、迟到响应都要使旧动作 token 失效。

## 4. 核心逻辑

### 身份与归属

PackageKey = manager + source/bucket + exact id + scope。scope 为 user/machine/global/unknown 等 backend 支持值，并保留其证据；缺源或 scope 时用显式 unknown，不能当通配符匹配其他项。未知身份只能只读，直到精确解析；scope 不支持指定时不能自动重试其他 scope。版本、架构、安装器实例定位信息属于快照/执行定位条件，不以显示名称替代 ID。

Inventory 的 provenance 分为 observed（仅枚举）、matched（有目录匹配）、backend-recorded（有 backend 安装记录证据）；UI 只有证据足够才允许显示 managed，并说明不保证由该管理器首次安装。WinGet list 可包含非 WinGet 安装项目，匹配源包也不证明安装归属。跨 Scoop/WinGet 的疑似同软件仅提供提示与用户核对，不合并 PackageKey，不连带卸载。observed 卸载额外要求唯一实例、scope 和受支持卸载定位都验证通过。

### 查询与适配

Installed / Discover / details / update-list 分别形成只读意图，版本适配器必须审查实际副作用。Scoop 无参数 update 不是 update-list；不能为搜索/检查更新自动调用它。元数据过期时标 stale；升级命令若会自更新，计划必须披露并获得独立许可，能以已验证选项抑制则抑制，无法满足则阻塞该操作。

防抖间隔和查询并发从测量定；每次输入生成 generation，取消先前请求，迟到结果即使成功也不覆盖新请求。查询取消不影响已确认的变更。查询和正在变更的 manager 竞争时优先避免锁冲突，展示等待/旧快照。

适配器输出统一领域模型并保留原始版本、错误类别与观测时间。WinGet CLI 为 version × command × locale × encoding 的 fixture 白名单；包括本地化列标题、Unicode、截断、控制序列和错误输出，不能按猜测切列。无匹配 fixture 则 unsupported。API/模块路径可选，先说明依赖和能力再由用户选，不自动安装。Scoop 的 search/list/info/status 等候选查询仍需逐版本核实输出与副作用；本次未将它们标为已验证命令。

Scoop 可能是 `.ps1`/PowerShell shim，不能当普通 exe 直接传 argv。未来采用固定可信 PowerShell 入口、非交互调用及类型化参数绑定，不拼接用户文本；参数序列化、脚本路径、特殊字符和 option-like ID 必须经过 L1/L3。不能稳定、安全调用则禁用。WinGet 采用可验证路径与 Windows argv 编码；禁 shell fallback。ID 精确匹配，scope/source 明示；不从名称、任意用户 URL、脚本或附加参数构造变更。

### 计划、队列与恢复

Plan 包含 PackageKey、installed/target version、scope/arch、依赖、材料可用性/完整性、策略、协议、权限路径、元数据时间、能力证据版本及摘要。先 refresh → resolve → preflight → review → confirm → queue；出队再 refresh/compare，摘要不同进入 needs review，不能沿用旧授权。确认有明确操作和目标版本，不把整段未知脚本当授权。

每个 manager 一条串行 mutation lane；重装两个阶段占用同一 lane，不能插入其他任务。跨 manager 可独立查询；可能操作同一实例的变更暂停复核。外部进程检测/记录变化只是信号，不能保证捕获所有竞争；锁冲突则等待或失败提示，绝不删除/抢锁。排队可去重但不得把不同 scope 合并。外部状态改变后失效相关缓存及计划。

Task 状态至少包括 queued、preflight、needs review、blocked、running（acquire/uninstall/install/verify）、interactive pending、cancel requested、reboot required、succeeded、failed、partial failure、cancelled、interrupted/unknown。重装卸载成功后的取消也可能留下未安装状态。运行取消先请求停止并记录范围，随后查实际状态；不能以 killed=rolled back。交互安装器有窗口/提示但无输出时显示 pending，并让用户切换安装器；超时不直接判成功。退出码和安装后版本/实例核对共同决定结果；重启未完成时不宣称完全生效。

## 5. 交互

见 [UI 规格](UI-SPEC.md)。主入口 `packages` 不携带执行语法；显示名不作稳定 ID。Installed 默认只读，Discover 的 Enter 进入详情，所有改变必须单独点操作并确认。Updates 不默认全选，不绕过 hold/pin 或 unknown version；Tasks 显示阻塞、阶段与估计性质。

## 6. 权限、数据、隐私

| 拟声明 | 实际用途和限制 |
|---|---|
| ui | 独立窗口、确认、详情和安装器等待提示；不取消 Job/其他进程缓解限制。 |
| processes | 启动 manager、必要 PowerShell 和由其发起的安装器；子进程仍在宿主 Job。不能当作管理员许可。 |
| network | 搜索源、元数据/协议、安装材料和 manager 可能发起的访问；宿主不能强制阻止，不能声称本地查询绝不联网。 |
| files:read | 声明读取插件私有缓存、manager 安装记录/清单与所选包材料的实际路径；具体路径待 adapter 核实后最小化列明。此权限仅声明，不是读沙箱。 |
| files:write | 插件设置、脱敏任务记录、缓存在 WS_DATA_DIR；manager/安装器另会写软件目录、用户配置、persist、源元数据和可能的注册表/环境，按计划告知。normal 下只是声明。 |

不请求 elevated、network:listen、剪贴板、宿主内容索引或 system:settings。normal 不等于隔离文件写入，插件私有数据目录约束不代表能把第三方安装器全部写入限制在该目录。

安装器子进程、COM/服务代理、UAC broker、嵌套 Job 和宿主退出必须在 L3/L4 隔离 VM 分别探测。允许 UAC 的单次任务路径也需显式用户确认，宿主不会替插件弹 UAC；UAC 拒绝返回 blocked/denied。若原始宿主模型不支持，禁用该路径并给出用户自主操作指引，不整体提权插件、不 breakaway/解除 Job、不修改宿主权限策略。管理员 scope 在未通过探针前禁用。

日志最小化：不存环境全量、令牌、源凭据、完整个人路径；保留脱敏 ID、版本、阶段、退出码和必要错误片段。持久化不存可自动执行的 shell 文本；恢复数据视为不可信。日志导出由用户主动触发并预览脱敏；保留期/容量在 L5 测量后确定，提供清除。无遥测。

## 7. 失败矩阵

| 触发 | 表现 | 降级/恢复 |
|---|---|---|
| backend 缺失/路径不可信/版本未知 | missing/unsupported | 保留另一 backend，提供只读说明，不自动安装。 |
| 权限拒绝、Job 限制或 UAC 拒绝 | denied/blocked，保留错误来源 | 不提权整插件、不脱离 Job；记录未支持路径。 |
| 解析 fixture 不匹配/输出截断 | unsupported/incomplete | 不生成可执行目标，不猜解析。 |
| 网络失败或源协议未同意 | stale/error/agreement required | 缓存只读可见，拒绝协议则停止对应请求。 |
| scope/源/ID 歧义、非 backend 原生记录 | needs review | 唯一精确定位前禁变更。 |
| hold/pin 或 unknown version | blocked/manual review | 不自动解除/忽略；单项政策复核。 |
| 外部 manager 变更或锁占用 | stale/conflict | 刷新、使确认失效，等待或由用户重试。 |
| 重装原版本材料缺失/校验失败 | preflight blocked | 卸载前停止，不跳过 hash；另选版本须重确认。 |
| 重装卸载失败 | failed | 不开始安装，重新查状态。 |
| 重装已卸载但安装失败/取消 | partial failure | 明示可能已无软件/数据受损，保留恢复资料，用户重新规划。 |
| 交互安装器等待 | interactive pending | 提示切换窗口；超时/无输出不判成功，不盲目重复启动。 |
| 重启需求或退出后版本不符 | reboot required/unknown | 提示后续核对，不自动重启。 |
| 宿主退出/崩溃/禁用 | interrupted/unknown | Job 可能杀子进程，外部代理状态未必可控；恢复后核对，不重放。 |
| 输出过大/空间不足 | incomplete/failed | 截断诊断须标识、停止解析；容量基于 L5，不无限缓冲。 |

## 8. 交付清单

本轮仅交付 README.md/.en.md、plugin-design.md/.en.md、UI-SPEC.md/.en.md、TEST-PLAN.md/.en.md，共八份文档。实现阶段另行审批清单、适配器、UI、fixture、打包、双语 CHANGELOG 与验证证据；本轮不创建它们，不提交、不安装卸载软件。

## 9. 测试计划（L0–L5）

[逐行测试计划](TEST-PLAN.md) 含 36 项，逐项写 ID / FR / layer / preconditions / steps / expected / evidence / 未执行。L0 静态文档与清单/打包（后两者尚不存在）；L1 跨平台纯逻辑；L2 fake 宿主/backend；L3 真实宿主往返（隔离 VM）；L4 解锁 VM 桌面真实输入与可丢弃包；L5 标定/发布。L3 是实现验收门槛，特定安装器/UAC 必须 L3/L4 都通过；当前全部未执行。

## 10. 诚实笔记（Honest notes）

实测：无包管理器、安装器、UI、宿主往返或性能运行实测。已读核实：第 2 节的本地文档及官方 Scoop 固定源码；WinGet 命令范围依据本轮读取的官方文档。

推断：统一模型、每 manager 串行及保守能力门控可减少误操作；Job/代理交互可能阻塞某些安装器。它们不是已验证兼容性结论。

未验证/待决：具体 backend 版本矩阵与本地化 fixture；Scoop 查询命令副作用；WinGet 结构化 API/模块选择与部署许可；材料预取/暂存能力；原版本重装安全性；未知 scope/observed 卸载定位；Job、UAC、交互安装器、重启/取消恢复；外部操作检测可靠性；容量和性能。未测路径保持不支持。

## 11. 交付报告（实施后回填）

当前交付为需求文档，尚无实施版本、运行证据或发布验收。未来每版本追加 addendum：实现版本/提交、采用的 backend/version 矩阵、设计偏差、测试 ID 与证据、实测/推断/未测边界、未解决阻塞和清理记录。不能把文档完成写成插件已完成。
