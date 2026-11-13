# 第三方组件

[English](THIRD_PARTY.en.md)

本插件在运行时链接的第三方组件及其许可。

| 组件 | 版本 | 许可 | 用途 |
|---|---|---|---|
| Qt 6 | 6.12.0（宿主共享） | LGPL-3.0 | 通过 Win顺 的共享 Qt 契约加载（`runtime.qt: "host"`）；插件包内不含 Qt DLL，由宿主部署。详见 [Qt licensing](https://www.qt.io/licensing/)。 |
| Microsoft Windows | WinGet v1.29.380 | 系统组件 | 作为外部子进程调用（`winget.exe`），不随包分发、不嵌入。 |
| Scoop | 本机安装版 | MIT（Unlicense） | 作为外部 PowerShell 脚本调用（`scoop.ps1`），不随包分发、不嵌入。 |
| Cascadia Mono / Microsoft YaHei UI | 系统字体 | 系统组件 | 通过 `font.family` 引用，不分发字体文件。 |

## 构建期（不随包分发）

| 组件 | 用途 |
|---|---|
| CMake 3.21+ / Ninja | 构建系统 |
| MSVC 14.51 (VS 18) | 编译器 |
| Python 3.12（仅测试） | `tests/validate.py`、打包脚本 |

## 无第三方源码嵌入

插件源码不包含、不修改、不再分发任何第三方源码文件；上述组件均以系统安装或宿主部署的形态使用。
