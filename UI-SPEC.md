# Packages UI 规格（未实现）

[English](UI-SPEC.en.md) · [需求](README.md) · [设计](plugin-design.md) · [测试](TEST-PLAN.md)

## 入口与信息架构

`packages` 打开普通权限独立窗口。首次只检测 backend 和查询 Installed，不安装依赖或更新管理器。窗口遵循 Win顺 appearance/语言通知，紧凑、现代、简洁；具体尺寸、密度、虚拟列表阈值与分页容量从测量/可访问性评审确定，不在需求阶段拍固定上限。主导航保持 Installed / Discover / Updates / Tasks；中文界面对应“已安装 / 发现 / 更新 / 任务”，文档用英文标识稳定区域。

## 主列表与详情 ASCII

```text
+-----------------------------------------------------------------------+
| Packages                                      [Refresh] [Settings]    |
| [Installed] [Discover] [Updates] [Tasks: 2]                             |
| Backend: [All v]   Search packages [________________________]           |
| Scan: time | source metadata: stale | Scoop available | WinGet available|
+------------------------------------------+----------------------------+
| [ ] Name / exact ID                      | Details                    |
|     Manager | source/bucket | scope      | manager / source / ID      |
|     Installed -> Available | provenance  | scope + evidence / arch    |
|------------------------------------------| installed / target / time  |
| [ ] Example A                            | provenance / restrictions  |
|     Scoop | main | user                  | hold/pin / agreements      |
|     1.0 -> 1.1 | backend-recorded         | possible duplicate: review |
| [ ] Example A                            |                            |
|     WinGet | source-X | machine           | [Install] [Uninstall]      |
|     unknown | observed                   | [Reinstall] [Upgrade]      |
+------------------------------------------+----------------------------+
| Status: read-only query complete | selected: 1 | [Review selected...]   |
+-----------------------------------------------------------------------+
```

示例行是假数据，箭头只有 backend 给出更新证据才显示。All 保留不同身份的独立行，不按名称合并。scope/source 缺失显示 unknown；unknown 不代表任意 scope。managed 只有证据支持才出现，并可展开证据说明；WinGet list 中普通枚举项显示 observed，目录匹配显示 matched。跨管理器重复提示没有“自动移除另一个”动作。

Installed 的搜索框过滤当前列表或显式触发只读查询，Discover 搜索远程/本地目录；区分两者和可能联网。Updates 显示 hold/pin、目标版本和最后扫描时间，无默认全选。Discover 中 Enter、双击与选中均只开详情，不安装。复选框只选择候选项；不同操作不可被一个含糊“执行”按钮隐含混合，Review selected 先明确选择操作并逐项显示计划。

详情始终展示完整 PackageKey、版本/架构、scope/来源证据、依赖、元数据时间、材料获取状态及能力限制。动作不存在/unsupported 用文字原因解释；尚未选包时显示指引。Install 只对精确可解析目标开放；Uninstall 要求唯一已装实例。Reinstall 显示所采用的 backend/version 策略，原版本预检不满足则禁用；替换版本只能显式另选。Upgrade 被 hold/pin 阻塞时解释策略，不提供一键强行忽略。unknown version 需要单项人工审阅。

## 操作确认 ASCII

```text
+-----------------------------------------------------------------------+
| Review action: Reinstall                                              |
| Manager: Scoop  Version/revision: ...                                  |
| Source/bucket: main  Exact ID: example  Scope: user  Arch: ...          |
| Installed: 1.0  -> Target: 1.0  Plan refreshed: time                    |
| Method: explicit uninstall, then install (NOT ATOMIC)                  |
| Material: verified/staged ... | Dependencies: ...                      |
| Risk: uninstall/installer may delete data; recovery is not guaranteed.  |
| Policy: hold/pin ... | Manager/metadata side effects: ...              |
| [ ] Source agreement ... [View]      (only when applicable)            |
| [ ] Installer agreement ... [View]   (separate consent)                 |
| Scope/UAC: verified path or BLOCKED reason                             |
| [Cancel]                                     [Confirm and queue]      |
+-----------------------------------------------------------------------+
```

刷新和预检结束前不允许确认；预检若需下载/写暂存或刷新源，先展示独立有副作用计划，由用户确认后才执行，不能让“打开确认页”偷偷启动下载或更新。协议勾选默认未选，不自动记作所有源/安装器的同意。已存在的 backend 同意状态只有可验证时才展示其范围；没有待同意条款不生成无意义勾选。拒绝 source agreements 后查询可显示受限原因；拒绝 installer agreements 则不执行安装。

队列执行前再次刷新；任一身份、目标版本、scope、策略、协议、依赖变化均变为 needs review，返回差异页再次确认。按钮文案带操作，不把重装改版隐藏为原版重装。卸载不默认 purge，风险说明明确“不 purge 也不能保证保留数据”。批量所选逐项列出独立结果，不能用 `*` 或 upgrade-all 执行。

## Tasks ASCII 与状态

```text
+-----------------------------------------------------------------------+
| Tasks  Backend [All v]                                                |
| # | Manager | Exact target / scope | Action    | State                 |
| 1 | Scoop   | main/example / user  | Reinstall | install failed        |
|   | Removed successfully; package may be absent. [Details] [Replan]     |
| 2 | Scoop   | main/other / user    | Upgrade   | queued / lane waiting |
|   | [Remove pending]                                                  |
| 3 | WinGet  | source-X/id / user   | Install   | interactive pending   |
|   | Stage: installer running (duration unknown) [Switch to installer]  |
|   | [Request cancel]                                                  |
+-----------------------------------------------------------------------+
| Log: phase + timestamp + redacted error | [Preview export] [Clear...]   |
+-----------------------------------------------------------------------+
```

阶段顺序为排队、预检、获取、卸载/安装、核对，并非每项都有所有阶段。无可靠总量显示不定进度及耗时；若 backend 有可验证字节总量，只能标“下载进度”，不能冒充整体百分比。估计耗时必须标“估计”，依据待 L5 标定。interactive pending、权限拒绝、需要重启、partial failure、结果未知分别有文本和恢复建议。

Remove pending 只删除未开始任务；Request cancel 解释当前能取消到哪里，可能只停止等待，安装器/代理未必结束，也不能保证回滚。重装卸载后失败/取消明确软件可能已不存在，不能直接显示 cancelled 一切正常。Replan 重新核对状态并回到确认页，不自动重跑。重启只提示，不触发系统重启。宿主退出/禁用后恢复任务显示 interrupted/unknown。

## 状态、错误与恢复

| 状态 | 必须显示 | 可用恢复动作 |
|---|---|---|
| 初次加载/刷新 | 正在查询哪个 backend，取消查询，不把列表先清成成功空态 | Cancel query；保留有时间戳旧列表 |
| 真正空列表/无匹配 | 成功查询的范围和筛选条件 | 清筛选、切换 backend |
| backend 缺失 | 缺哪个、另一个的独立状态 | 查看手工准备说明；不自动安装 |
| unsupported | version/command/locale 或调用路径不支持 | 查看诊断，保留另一 backend |
| 网络/源错误 | 错误来源、最近成功时间、stale | 显式重试；不自动同意源协议 |
| 协议未同意 | source/installer、适用范围、条款入口 | View / Accept / Decline；Decline 不执行 |
| 锁冲突/外部变更 | 等待原因、快照失效 | Refresh / Replan，不抢锁 |
| 权限/UAC/Job 阻塞 | denied/unsupported path 及实际错误 | 诊断/用户自主操作指引，不“以管理员重启插件” |
| 交互等待 | 等哪个安装器、时长未知 | 切换到经验证窗口或说明手工查找；请求取消 |
| 部分失败/未知 | 已完成阶段、可能受影响状态 | 查看脱敏日志、刷新、重新规划 |
| 更新需重启 | 安装结果与尚未生效部分 | 后续刷新核对；不自动重启 |

Settings 仅拟提供 backend 只读路径/版本/适配状态、可选 API 依赖说明、隐私/清除记录；source/bucket 只读诊断 P1，全量增删和 pin/hold 管理 P2。不得在空态或设置里暗加自动安装依赖按钮流程。

## 键盘、主题、DPI 与读屏

Tab 顺序依次为导航、backend、搜索、列表、详情动作、底部状态/队列；列表方向键移动选择，Enter 只开详情，Space 选择候选，Esc 关闭当前弹层/返回，不能作为隐含运行取消。确认框焦点先落标题/摘要，默认按钮不执行危险动作；关闭后回到来源行，行消失则回列表合理位置。队列状态可键盘访问，禁用动作也能读取原因。

使用 Win顺 主题与外观通知、系统高对比度语义色；图标配文字，错误不只靠红色。DPI/文字放大、多显示器缩放切换时保持操作可见；窄宽度允许详情独立面板/返回列表，不截断关键身份，完整 ID 可选择/读出但不擅自申请剪贴板权限。屏幕阅读器可读导航、选择、包身份、scope、风险、协议、禁用理由和任务阶段；异步结果/状态以合适 live 通知播报，不因每段日志抢焦点。Reduced motion 下不依赖动画表达状态。

关联 FR：PKG-F01–F24、F27–F30；性能与可访问性验证另由 PKG-F25/F26 和测试计划覆盖。所有 ASCII 为需求线框，不是截图或实现证据。
