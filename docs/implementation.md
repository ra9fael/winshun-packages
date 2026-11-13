# 实现说明

[English](implementation.en.md)

面向读者的实现地图。需求见 README，设计裁决与失败矩阵见仓库根的插件设计文档（winshun-dev 工作台）。

## 目录

| 路径 | 职责 |
|---|---|
| `plugin.json` | 唯一权威清单：runtime（resident/lazy/normal/qt:host）、权限白名单、命令 |
| `src/core/` | 纯逻辑，无 OS 头文件，任意平台可单测：identity（身份）、version（版本比较）、model（领域模型）、plan（计划、确认失效、动作 id 往返）、queue（每 manager 串行 lane、按 id 干预、暂停、逐阶段日志）、redact（脱敏）、json_lite（Scoop export 解析）、cache（磁盘缓存序列化）、theme_tokens（宿主外观解析与合成） |
| `src/adapters/` | WinGet/Scoop 适配器：检测、fixture 门控解析、argv 构造 |
| `src/platform/` | CreateProcessW + 子 Job、管道捕获、取消 |
| `src/ui/` | Qt Quick 窗口：controller（window.h）、模型（models.h）、theme_resolver、qml_window（场景装载） |
| `src/ui/qml/` | Main / Theme / RowDelegate / DetailPane / ReviewDialog / TasksPage / RowMenu，以及三个主题化控件 ThemedButton / ThemedField / ThemedCheckBox。DetailPane 里精确 ID 是独立可复制框（`key: "exactId"`），并带 `winget show` 信息块；`fieldAt()` 的键名是封闭集合，写错会静默返回空串 |
| `tests/` | L1 纯逻辑（core/theme）、fixture 测试（winget/scoop）、L2 真实子进程（process）、L0 静态校验（validate.py） |
| `tools/` | package.py（确定性打包）、capture_fixtures.ps1（fixture 采集）、grab_window（离屏渲染验证）、probe_scoop（Scoop 根定位与真实 export 诊断） |

## 线程模型

- 主线程 = Qt GUI（窗口与场景）。
- 协议线程 = `winshun::Plugin::serve()`，独占 stdio；经 `QMetaObject::invokeMethod(QueuedConnection)` 与 GUI 通信。
- 查询线程 = 每次刷新起一个 detached worker（`startBackendRefresh`），内部再按 backend 分两条线程并行（WinGet 已安装+更新 / Scoop 已安装），结果经队列投递回 GUI；`m_refreshInFlight` 防重入。
- 执行线程 = `drainQueue` 挑出可运行任务后在 detached worker 上跑 `runOneTask`，逐阶段 `platform::run`；`logLine` 跨线程时重投回 GUI 线程再写队列，`m_runCancel` 由执行器与 `run()` 共同轮询。
- 搜索线程 = 发现页搜索单飞行，结果过期即丢弃。

## 安全边界（实现位置）

| 规则 | 实现位置 |
|---|---|
| 不拼接 shell、argv 逐个校验 | `platform/process.cpp::quoteArgument` + 各 adapter 的白名单 |
| unknown 不当通配、scope 必须精确 | `core/identity.cpp::isResolvable` |
| 协议永不自动同意 | `winget_adapter.cpp::buildMutation`（仅显式传入时加 `--accept-*-agreements`） |
| 确认失效 | `core/plan.cpp::invalidateIfChanged`（出队前 facts 对比） |
| 每 manager 串行、重装两阶段占同一 lane | `core/queue.cpp` |
| 日志脱敏、不无限缓冲 | `core/redact.cpp` + `ui/window.cpp::logLine`（唯一写入点）+ `Queue::appendLog`（上限 200、保留尾部） |
| 运行中的任务不能被移除 | `core/queue.cpp::canRemove` / `removeFinished` |
| 未知动作字符串不能变成变更 | `core/plan.cpp::actionFromId`（未知一律 `Action::None`） |

## 构建

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=<Qt>/6.12.0/msvc2022_64
cmake --build build --parallel
ctest --test-dir build -C Release
```

共享 Qt 契约要求 MSVC、Release、动态 CRT；包内不含 Qt DLL。
