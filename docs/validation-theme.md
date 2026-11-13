# Packages 插件验证 — 界面风格一致性（2026-11-10）

[English](validation-theme.en.md)

## 1. 问题是什么

用户要求插件界面与 Win顺 的界面风格保持一致。Win顺 有四种界面风格（fluent / brand / tactile / liquid），各有深浅色，插件窗口需要全部跟随，而不是自己发明一套配色。

环境：Windows 11、Win顺 0.22.0（源码核实）、Qt 6.12.0（msvc2022_64）、MSVC 14.51。

## 2. 为什么（根因分析）

之前的 `Theme.qml` 只接了约 20 个 token，且有三类问题：

1. **半透明 token 直接用**。宿主开着透明效果时 `layer`、`hover`、`pressed`、`tagFill`、`divider` 等是半透明色（如 fluent 深色下 `layer = rgba(255,255,255,0.052)`）。插件窗口没有宿主那份 DWM backdrop，直接绘制会透出桌面。这是 `docs/plugins.md`「外观」明确要求合成的原因。
2. **字体与行高硬编码**。`font.pixelSize: 10/11/12/13`、`implicitHeight: 50` 等写死，宿主的 界面大小（dense/compact/standard/roomy）无法生效。
3. **`uiSize` 没接**。它不在 `nativeLook` 里，而在 appearance 顶层。

## 3. 改了什么

| 改动 | 说明 |
|---|---|
| `src/core/theme_tokens.h/.cpp`（新增，纯逻辑） | 解析 `nativeLook` 全部 27 个颜色 token + 7 个圆角 token；对可能半透明的 token 按 src-over 合成到 `background`（结果恒不透明）；`success`/`caution` 不在 `nativeLook`，按 `dark` 自取（与 fluent 同值）；`uiSize` 四档照宿主 `UiScale.qml` 数值表；未知风格不分支、缺字段回落 fluent 固定值；异常输入（非法十六进制、负数/超大圆角）钳制或回落。 |
| `src/ui/theme_resolver.h/.cpp`（新增） | 把 QVariantMap 展平喂给纯核心，返回 `#AARRGGBB` 形式给 QML。 |
| `src/ui/qml/Theme.qml`（重写） | 全 token 集成、`uiSize` 四档字号/行高、无 `themeResolver` 时回落、`look.resolvedUiFont` 作字体族。 |
| `src/main.cpp::appearanceLook()` | 同时传 `uiStyle` `uiSize` `themeSetting` `corners` `transparency` `renderer` `uiFont` `resolvedUiFont` `locale` `theme` + 嵌套 `nativeLook`。 |
| `src/ui/window.cpp::applyAppearance` | 接收完整 map，`themeObject->setProperty("look", …)` 推给 Theme。 |
| QML 全部去硬编码 | `RowDelegate/Main/DetailPane/ReviewDialog/TasksPage` 的字号全部改读 `Theme.tinyFont/metaFont/smallFont/bodyFont/titleFont`；行高/条高改读 `Theme.rowHeight/tabHeight/headerHeight` 等；等宽字体统一 `Theme.monoFamily`。 |
| `tools/grab_window.cpp` | 支持 `packages_grab_window.exe <out.png> <list\|tasks\|review> <style> <light\|dark>`，内置四种风格 × 深浅色的真实 `nativeLook` 值（取自宿主各 `Look.qml`）。 |
| `tests/theme_tokens_tests.cpp`（新增，70 检查） | 颜色解析、合成（含 78% 白 over 深底 = 合成后不透明且不过亮）、fluent 回落、brand 全量、半透明合成、`rounded=false` 全零、未知风格、success/caution 派生、uiSize 四档、畸形输入。 |
| `tests/validate.py`（新增） | L0：manifest 形状（含“必须 qt:host”“不得 elevated”）、双语文档互链、x64 PE、包内容与 SHA256。 |

## 4. 实测

### 4.1 单元测试

```
ctest --test-dir build/grab -C Release
  1/5 packages_core      Passed  (身份/版本/计划/队列/脱敏)
  2/5 packages_theme     Passed  (70 检查，0 失败)
  3/5 packages_fixtures  Passed  (WinGet 真实 fixture，2755 检查)
  4/5 packages_scoop     Passed  (Scoop fixture + argv 安全)
  5/5 packages_static    Passed  (manifest/双语/x64)
```

关键断言（theme_tokens_tests）：
- 合成后 `a == 255`（半透明 token 一律不可穿透）。
- fluent 深色 `layer = #C7FFFFFF` 合成到 `#FF202020` 底后，通道值落在浅色区间但非纯白。
- `rounded=false` 时 7 个圆角全部为 0。
- `nativeLook` 完全缺失时返回 fluent 固定值（旧宿主兼容）。

### 4.2 渲染验证（8 张截图）

`packages_grab_window.exe build\shots\<style>-<theme>.png list <style> <theme>`，全部 980×720：

| 文件 | 核对点 | 结果 |
|---|---|---|
| fluent-light.png | 中性浅灰底、深字、蓝 accent | 符合 |
| fluent-dark.png | `#202020` 底、浅字（非硬编码深字） | 符合 |
| brand-light.png | `#A8402C` terracotta accentFill 出现在主按钮/勾选框 | 符合 |
| brand-dark.png | 深底 + terracotta | 符合 |
| tactile-light.png | 暖纸色面板底 `#F3EFE9`、橙色 accent | 符合 |
| tactile-dark.png | `#2A2A2D` 暖深底 | 符合 |
| liquid-light.png | 半透明 `layer` 合成后不透明、无穿帮 | 符合 |
| liquid-dark.png | 同上，深色 | 符合 |

8 张图均在真实 Windows 平台（`QT_QPA_PLATFORM=windows`）离屏 grab，字号 12–16 px 中文正常渲染，行高不重叠。

### 4.3 协议往返（真实进程）

```
stdin:  initialize → query(packages) → action(open) → shutdown
stdout: initialize 应答 protocolVersion=1
        query 返回一条 "打开软件包窗口"
        action 返回 {effects:[{type:"hide"}]} 并触发窗口创建
        shutdown 应答 {}
```

- 窗口按宿主 appearance 创建并显示（`windowShouldShow` → `initializeUi` → QML 场景加载成功）。
- 截图即该真实场景。

## 5. 没测的

- **未在真实宿主（Win顺 进程内）验证**。appearance 数据链路（宿主 → initialize/appearance → SDK → controller → Theme.qml）只在本工具里用宿主同源值模拟；真实宿主发来的字段命名差异（如 `corners` 值域、`uiSize` 缺省）未实测。挂接宿主后需补一轮。
- **liquid 的 Acrylic 背景未复刻**（按计划不做）：插件窗口用合成后的不透明色，与宿主 Mica/Acrylic 的质感有差异。已记录为已知限制。
- **窗口进程退出在 `app.exec()` 返回后仍挂起**（最小 Qt 复现亦复现，疑似本机 Qt 6.12.0/offscreen 或驱动环境问题）。宿主以 Job 结束插件进程，不影响宿主集成；已列入待办。
- 安装/升级/重装路径未真实执行（按用户选择，mutation 一律标"未执行"）。
- **`uiSize` 四档只验证了 standard**（截图工具固定 standard）；dense/compact/roomy 的行高/字号变化未截图核对，仅有单测覆盖数值表。
- 键盘可达性（Tab 顺序、读屏）未在本轮验证。

## 6. 收尾清理

- 删除了调试用临时文件（`src/core/dbgc.cpp`、`build/l1/dbg*.cpp/exe`、`tools/dbg_scene.cpp`、`build/smoke/crop*.ps1/png`）。
- 保留 `build/shots/*.png`（8 张风格验证图）作为证据素材；正式证据归档应在挂接宿主验证后迁入 `WinShun/docs/validation/packages-<日期>/`，届时按证据体积纪律压缩转存。
