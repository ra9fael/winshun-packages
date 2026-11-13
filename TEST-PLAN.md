# Packages 测试计划（全部未执行）
[English](TEST-PLAN.en.md) · [FR 需求](README.md) · [设计](plugin-design.md) · [UI](UI-SPEC.md)

## 范围与隔离。
36 项稳定测试 ID，覆盖 30 个 PKG-Fxx。当前只有文档，所有行状态均为“未执行”；文档/官方源码阅读不算动态测试通过。本轮不运行包管理器、探针、软件安装卸载，也不编写测试脚本。
未来 L0 为静态清单、打包与文档检查；L1 为无 OS 头文件的纯逻辑；L2 使用 fake host/backend；L3 为真实宿主往返，是实现验收门槛；L4 为解锁 VM 桌面、真实输入与安装器交互；L5 为性能标定和发布检查。所有动态测试只在隔离、可还原 VM 中进行，包括正常查询与性能采样；使用无用户凭据、无共享用户目录的独立账户、独立源/数据目录及可丢弃包。安装器/UAC 专项另用 VM 快照，不得接触用户现有软件、配置或资料。真实官方源条款若需接受，由测试人员在 VM 内明确同意，不自动代签。
测试矩阵需记录 Windows/宿主版本、管理员/普通宿主、backend 精确版本、Scoop revision、调用类型、locale/encoding、scope、安装器种类和可选API 依赖。仅通过某个版本/语言/安装器不扩大为全量支持。管理员 scope 在 L3/L4 探针前保持禁用；探针不通过不通过提权整插件或移除 Job“修好”。
## 逐项用例

Evidence 列给出未来证据名与内容要求，并非已生成文件。证据放在工作台规范指定的`packages-</日期>/`，脱敏精选摘要留库，原始大文件按规范归档。所有行分别关联设计节号，步骤需要实现后才执行。
| ID | FR | Layer | Preconditions | Steps | Expected | Evidence（拟议）| 状态 |
|---|---|---|---|---|---|---|---|
| PKG-T01 | PKG-F30 | L0 | 八份文档 | 对照 FR、链接、双语事实和状态 | 30 FR 同义、互链有效、没有实现完成宣称；设计 §8/10/11 一致 | T01-doc-review：数据链接/双语审阅 | 未执行 |
| PKG-T02 | PKG-F17, PKG-F21, PKG-F26 | L0 | 未来清单与候选包，当前不存在 | 校验 schema、权限、运行模式和包文档| resident/lazy/normal；权限用途准确，无elevated，双语产物完整；设计 §3/6/8 | T02-package-static：校验输入文件摘要 | 未执行 |
| PKG-T03 | PKG-F06, PKG-F04 | L1 | 同名、同 ID 异源/多scope、unknown 数据 | 生成键、选择、查详情 | 不误合并，unknown 非通配；跨 backend 仅提示；设计 §4 | T03-identity：输入断言 | 未执行 |
| PKG-T04 | PKG-F02, PKG-F08 | L1 | WinGet 枚举、WinGet 安装、匹配记录三类 fixture | 映射归属并请求卸载| observed/matched 不误标managed，定位不唯一禁卸载；设计 §4 | T04-provenance：fixture/断言 | 未执行 |
| PKG-T05 | PKG-F03 | L1 | 可控时钟与乱序响应| 连续输入，防抖触发，取消并返回旧结果 | 仅当新generation 可见，无 mutation；设计 §4/5 | T05-query：时序事件| 未执行 |
| PKG-T06 | PKG-F12 | L1 | 已确认计划及可变快照 | 分别修改版本/scope/策略/协议/依赖后出现| 原确认失效，needs review，不执行；设计 §4 | T06-plan：差异与状态转移| 未执行 |
| PKG-T07 | PKG-F05, PKG-F10, PKG-F29 | L1 | hold/pin、unknown、非标准版本 fixture | 构造更新列表和选中升级计划 | 尊重策略，不按字符串猜升级；unknown 单项审阅；设计 §4 | T07-policy：决策断言 | 未执行 |
| PKG-T08 | PKG-F09 | L1 | 每版本能力与材料可用/缺失/损坏 fixture | 请求原版重装、force 路径和替代版本| 无证据禁 force；缺材料在卸载前阻塞；替代版需新确认；设计 §2/4 | T08-reinstall-plan：能力断言 | 未执行 |
| PKG-T09 | PKG-F11 | L1 | 多任务、两 manager、疑似共同实例| 排队/移除/去重/运行两阶段重装 | 两 manager 串行、重装不穿插、不同scope 不误去重、共同实例暂停；设计 §4 | T09-queue：调度事件| 未执行 |
| PKG-T10 | PKG-F13, PKG-F15, PKG-F24 | L1 | 各任务阶段与恢复快照 | 取消、退出、重装卸载后失败、重启事件| 部分失败/unknown 独立，无自动回滚或重放；设计 §4/7 | T10-state：状态机断言 | 未执行 |
| PKG-T11 | PKG-F18 | L1 | 空格/引号/Unicode/元字符option-like ID 等恶意输入| 编码 WinGett argv 与 Scoop 参数绑定；拒绝 URL/脚本 | round-trip 保持单一参数，无 shell 注入/附加参数；设计 §4 | T11-arguments：安全用例输入| 未执行 |
| PKG-T12 | PKG-F19 | L1 | 各支持版本、命令/语言/编码及未知输入fixture | 解析正常、截断、错误、控制序列与未支持组合 | 正确或明确incomplete/unsupported，绝不猜测 JSON；设计 §4 | T12-parsers：fixture 矩阵/断言 | 未执行 |
| PKG-T13 | PKG-F01, PKG-F19 | L2 | fake host；backend 缺失/拒绝/未知版本；API 缺失 | 初始化与选择 backend/API | 局部降级、显式可选依赖、无依赖安装；设计 §3/4 | T13-detection：fake 调用记录 | 未执行 |
| PKG-T14 | PKG-F07, PKG-F12, PKG-F16 | L2 | fake install、源/安装器两种协议| 搜索、选中、取消、分别拒绝协议、确认、改计划 | 搜索零安装；两种协议独立，无同意不变更，改计划再审；设计 §4/5 | T14-install：调用与同意记录 | 未执行 |
| PKG-T15 | PKG-F08, PKG-F09, PKG-F15 | L2 | fake 卸载失败、安装失败及 hash 失败 | 重装依次注入每阶段失败| 材料失败不卸载；卸载失败不安装；已卸载失败明标partial；无 purge；设计 §2/7 | T15-reinstall-fail：阶段记录| 未执行 |
| PKG-T16 | PKG-F20, PKG-F27, PKG-F28 | L2 | fake 锁、外部操作、源变化/新bucket | 触发刷新、出队和源诊断| stale/重新审阅，不抢锁、不自加源；元数据维护独立计划；设计 §4/7 | T16-external：缓存锁事件| 未执行 |
| PKG-T17 | PKG-F14, PKG-F22 | L2 | fake 无输出、进度、错误、离线和空结果| 切换查询/任务状态 | 空态不同于失败；无总量不定进度；交互等待不假成功；设计 §5/7 | T17-presentation：状态快照| 未执行 |
| PKG-T18 | PKG-F21, PKG-F24 | L2 | fake initialize/appearance、敏感日志、损坏持久记录| 改语言/外观、导出、恢复禁用 | 协议 stdout 纯净、脱敏、写私有目录、恢复不执行文本/不重放；设计 §3/6 | T18-host-data：消息脱敏快照 | 未执行 |
| PKG-T19 | PKG-F01, PKG-F17, PKG-F26 | L3 | 隔离 VM、真实宿主，普通与管理员宿主矩阵| lazy 首用、query/action 往返、禁用退出| 普通token、Job 保留，权限按声明；无整插件elevated；设计 §3/6 | T19-host-job：token/Job/协议摘要 | 未执行 |
| PKG-T20 | PKG-F18, PKG-F19 | L3 | VM 中真实Scoop PowerShell/shim 与各 WinGet 支持版本 | 用安全特殊字符查询定位并比对fixtures | 调用正确、shell fallback；不匹配禁用；可选API 缺失不安装；设计 §4 | T20-invocation：版本help/argv 摘要 | 未执行 |
| PKG-T21 | PKG-F02, PKG-F04, PKG-F05, PKG-F27 | L3 | VM 可丢弃包、非 WinGet 安装包、不同scope/源、hold/pin | Installed/search/details/update-list 往返，观察文件/网络变化 | 身份/归属准确、更新策略正确；无参数Scoop update 不充当查询；实际副作用有记录；设计 §2/4/6 | T21-queries：快照与副作用摘要| 未执行 |
| PKG-T22 | PKG-F07, PKG-F08, PKG-F10, PKG-F12 | L3 | VM 中每 backend/version 的正常user-scope 可丢弃包 | 选中安装/升级/卸载，执行前外部改变一次计数| 精确定位，正常成功有状态核对；变更后要求重确认；无其他 scope 重试；设计 §4 | T22-mutations：前置inventory/退出码 | 未执行 |
| PKG-T23 | PKG-F09, PKG-F26 | L3 | VM 快照、固定版本材料和失效下载案例 | 验证材料预取/暂存；重装；模拟原版不可选安装中断 | 仅证实版本开放；不可得不卸载；partial 明确；force 必须有help+同版运行证据；设计 §2/4 | T23-reinstall：版本help/材料 hash/前后状态 | 未执行 |
| PKG-T24 | PKG-F15, PKG-F17, PKG-F26 | L3 | 独立 VM 快照，各安装器COM/服务/UAC broker/nested Job | 探测 user/admin scope、拒绝UAC、宿主退出 | 记录路径/代理生命周期，失败禁用；不解 Job、不 elevated 整插件；设计 §6/7 | T24-installer-probe：进程树/Job/UAC 结果 | 未执行 |
| PKG-T25 | PKG-F13, PKG-F20, PKG-F24 | L3 | VM 中真实外部manager、锁和运行任务| 外部并行操作、取消、宿主退出再恢复 | 无抢占重放；实际状态刷新；未能可靠检测的窗口明确记载；设计 §4/7 | T25-concurrency-recovery：时间线实际状态 | 未执行 |
| PKG-T26 | PKG-F03, PKG-F07, PKG-F08, PKG-F10 | L4 | 解锁 VM 桌面、真实输入、可丢弃 VM| 键入/Enter/双击搜索，再明确确认各选中操作 | 搜索不安装；所选动作仅确认后执行；显示身份/版本/scope；设计 §5 | T26-user-flow：脱敏截图操作记录 | 未执行 |
| PKG-T27 | PKG-F09, PKG-F13, PKG-F15, PKG-F17 | L4 | 解锁 VM、已过L3 的安装器路径、可还原快照 | 交互等待、UAC 接受/拒绝、重启需求、重装中取消/失败 | 各状态可区分，取消不承诺回滚，partial 明示；无自动重启/整体提权；设计 §6/7 | T27-interaction：窗口代理结果/恢复状态 | 未执行 |
| PKG-T28 | PKG-F16, PKG-F12 | L4 | VM 真实待同意条款与变更的计划| 分别阅读/拒绝/接受 source 或 installer 条款；修改计划| 无预勾同意，拒绝阻塞相关动作，计划变化重新确认；设计 §4/5 | T28-consent：界面与执行记录 | 未执行 |
| PKG-T29 | PKG-F23 | L4 | VM 键盘、读屏、高对比度、深浅主题、多 DPI/文字放大 | 全键盘查询确认/队列、切换缩放、读出错误与风险 | 焦点合理、无关键裁切、禁用原因可读、状态播报不抢焦点；设计 §5 | T29-accessibility：检查表/精选截图| 未执行 |
| PKG-T30 | PKG-F14, PKG-F22, PKG-F20 | L4 | VM 离线、空结果、缺 backend、unsupported、锁与错误 | 遍历加载/stale/部分失败/任务估计 | 状态分离，时间与估计准确标注，恢复动作不隐藏更新安装；设计 §5/7 | T30-ui-states：状态截图矩阵| 未执行 |
| PKG-T31 | PKG-F25, PKG-F03 | L5 | 隔离 VM，记录硬件版本/语言，递增真实和合成数据| 测冷/热启动、查询详情、输入防抖取消、UI 延迟与峰值内存输出 | 得到可复现分布和峰值；据此定性能目标和有依据容量余量，不预设包数上限；设计 §3/4/10 | T31-calibration：分量峰值余量理由 | 未执行 |
| PKG-T32 | PKG-F25, PKG-F11, PKG-F21 | L5 | VM 测得代表性峰值与磁盘/队列压力 | 长时排队、输入缓存/日志增长及空间不足| 决定容量、保留期、背压截断政策并验证UI 可响应；无无限缓存敏感泄露；设计 §4/6/7 | T32-capacity：增长曲线容量决策 | 未执行 |
| PKG-T33 | PKG-F26, PKG-F30 | L5 | 所有候选支持能力的 L3/L4 证据 | 审核版本表、未测项、双语文档和发布检查| 证据缺失能力不开放；未通过 L3 不验收，无发布阻塞才发布；设计 §8–111 | T33-release-gate：能力矩阵验收记录 | 未执行 |
| PKG-T34 | PKG-F28, PKG-F27 | L2 | 未来 P1/P2 设计、fake source/bucket 后端 | 只读诊断，再模拟新增/删除/信任改变 | P0 无写入口；P2 需独立确认/信任/缓存失效；源错误不自动修复；设计 §1/4/5 | T34-source-policy：范围确认事件 | 未执行 |
| PKG-T35 | PKG-F29, PKG-F10 | L2 | 未来 P2 设计、fake pin/hold 状态 | 读取策略、尝试绕过、显式修改后排队升级 | P0 不能修改/忽略；P2 修改单独确认审计，旧计划失效；设计 §1/4 | T35-pin-policy：策略审计事件 | 未执行 |
| PKG-T36 | PKG-F06, PKG-F08, PKG-F11 | L4 | VM 同软件双 manager 匹配、相同ID 多源/scope、observed 等| 查看提示、选择单项卸载、制造共同实例冲突| 不自动互卸、不误合并；卸载只针对验证唯一实例；共同实例任务暂停；设计 §4/5 | T36-identity-e2e：身份前后状态 | 未执行 |

## 执行与证据门槛
每项未来记录实际步骤、预期实际差异、版本矩阵、时间与证据文件，不用“看起来正常”代替断言。L0/T01 的人工检查也尚未作为正式测试执行；本轮交付检查不冒充验收证据。L1/L2 的 fake 成功不证明真实安装器兼容性。L3 与 L4 都通过的路径才能进入能力白名单；未覆盖的installer/scope/version 仍为 unsupported。P1/P2 用例是范围与未来能力门槛，不代表本轮实现它们。
材料预检涉及网络下载/暂存时也属于有副作用操作，需先确认获取计划；测试必须证明没有“打开详情即下载”或“先卸载再找包”。L5 实测峰值加余量后把依据回填设计、UI 与能力记录，若宿主预算不满足则降级禁用，不擅改宿主配置。任何安装后清理、还原快照和删除可丢弃包只在测试 VM 内由测试流程负责；当前不执行这些动作。
未决：具体支持版本 fixture、材料预取机制、Job/UAC/代理可运行性、unknown scope/observed 精确卸载、外部操作检测及性能容量。测试计划不是兼容性承诺。
