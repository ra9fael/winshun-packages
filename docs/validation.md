# Packages 插件验证 — 总览（2026-11-12）

[English](validation.en.md)

本文件是所有验证记录的总览。两个专项记录分别覆盖界面风格一致性与任务干预/卸载入口。

环境：Windows 11、WinShun 0.22.0 源码、Qt 6.12.0（msvc2022_64）、MSVC 14.51、
winget v1.29.380 zh-CN、Python 3.12（仅用于 L0 脚本）。

## 1. 验证分层

| 层 | 内容 | 命令 | 现状 |
|---|---|---|---|
| L0 静态 | manifest 形状、双语文档互链、x64 PE、包内容与 SHA256 | `python tests/validate.py` | 通过 |
| L1 纯逻辑 | 身份、版本、计划、队列、队列干预、脱敏、主题 token、磁盘缓存 | `ctest -R packages_core` 等 | 220 + 70 检查通过 |
| L1 fixture | WinGet 真实输出解析（2755 检查）、Scoop fixture 与 argv 安全（188 检查） | `ctest -R packages_fixtures` 等 | 通过 |
| L2 真实子进程 | `run()` 的输出捕获、超时、取消、退出码 | `ctest -R packages_process` | 7 例通过 |
| 渲染 | 四种风格 × 深浅色、列表/任务/审阅/设置四个状态 | `packages_grab_window.exe` | 按状态留图核对 |

一次跑完：

```
ctest --test-dir build/dev --output-on-failure
  1/6 packages_core      Passed
  2/6 packages_theme     Passed
  3/6 packages_process   Passed
  4/6 packages_fixtures  Passed
  5/6 packages_scoop     Passed
  6/6 packages_static    Passed
```

## 2. 专项记录

| 记录 | 覆盖 |
|---|---|
| [validation-theme.md](validation-theme.md) | 与 WinShun 的四风格一致性：token 合成、`uiSize` 四档、去硬编码、8 张截图核对 |
| [validation-queue.md](validation-queue.md) | 任务行级干预、队列暂停/清空、逐阶段日志与脱敏、审阅页动作选择器（卸载入口） |

## 3. 关键断言（跨专项）

- **退出码 0 不等于成功**：每条变更执行后都重新读取已装状态，`verifiedAfterRun` 才判定结果；卸载后仍在列表中即判失败。
- **提权只跟随宿主**：`elevated` 只反映进程真实的管理员状态，插件不弹 UAC；未提权时 machine/global scope 在预检阻塞并说明原因。
- **不自动同意协议**：source/installer 协议逐项勾选，切换动作后重新归零。
- **不拼接 shell 命令**：argv 只在 `buildMutation` 一处构建，包 ID 走白名单校验（拒绝 option-like、URL、脚本、控制字符）。
- **不改机器状态的操作不当作变更**：移除任务记录只删 UI 记录；暂停只阻止取新任务。

## 4. 未做真实变更

开发过程**没有在真机上执行过一次真实的安装/卸载/升级**（按约定）。执行路径的证据来自 fixture、L1 与渲染核对，而不是一次真实安装。真机联调应在挂接宿主后单独进行，并按宿主证据纪律归档。

## 5. 已知限制

- 外观数据链路只在 grab 工具里用宿主同源值模拟，**未在真实宿主进程内验证**；挂接后需补一轮。
- liquid 的 Acrylic/Mica 背景未复刻（插件拿不到宿主的 DWM backdrop），用合成后的不透明色。
- 窗口进程在 `app.exec()` 返回后仍挂起（最小 Qt 复现同样复现，疑似本机 Qt 环境问题；宿主以 Job 结束插件进程）。
- `uiSize` 四档只截图核对过 standard，dense/compact/roomy 仅单测覆盖数值表。
- 键盘可达性（Tab 顺序、读屏）未验证。
- 刷新尚未做到设计的五域全并行；`winget pin list` 与 `scoop export` 已改为每次刷新一次的共享缓存。
- Scoop 的根解析在**正常环境与模拟提权环境下**都用真实 `scoop export` 验证过，但**没有在真实宿主提权进程内跑过**（需要在 Win顺 以管理员身份运行、且插件已获 `elevated` 时实测一次）。
