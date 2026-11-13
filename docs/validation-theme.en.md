# Packages plugin validation — UI style consistency (2026-11-10)

[中文](validation-theme.md)

## 1. What was wrong

The plugin window was asked to match Win顺's UI style. Win顺 has four UI styles (fluent / brand / tactile / liquid), each with light and dark; the window had to follow all of them instead of inventing its own palette.

Environment: Windows 11, Win顺 0.22.0 (source-verified), Qt 6.12.0 (msvc2022_64), MSVC 14.51.

## 2. Why (root cause)

The old `Theme.qml` wired only about 20 tokens and had three classes of problems:

1. **Translucent tokens used directly.** With the host's 透明效果 on, `layer`, `hover`, `pressed`, `tagFill`, `divider` and friends are translucent (for example fluent dark `layer = rgba(255,255,255,0.052)`). A plugin window has no DWM backdrop of its own, so drawing them straight shows the desktop through. This is exactly why `docs/plugins.md`「外观」asks for compositing.
2. **Hard-coded fonts and heights.** `font.pixelSize: 10/11/12/13`, `implicitHeight: 50` and friends ignored the host's 界面大小 (dense/compact/standard/roomy).
3. **`uiSize` not wired.** It is not inside `nativeLook`; it arrives at the top level of `appearance`.

## 3. What changed

| Change | Notes |
|---|---|
| `src/core/theme_tokens.h/.cpp` (new, pure logic) | Parses all 27 colour tokens + 7 radii tokens of `nativeLook`; composites possibly-translucent tokens over `background` (src-over, result always opaque); `success`/`caution` are not in `nativeLook` and are derived from `dark` (same values as fluent); `uiSize` four tiers follow the host's `UiScale.qml` table verbatim; an unknown style never branches, missing fields fall back to fluent; malformed input (bad hex, negative/oversized radii) is clamped or rejected. |
| `src/ui/theme_resolver.h/.cpp` (new) | Flattens the QVariantMap for the pure core and returns `#AARRGGBB` strings to QML. |
| `src/ui/qml/Theme.qml` (rewritten) | Full token set, `uiSize` tiers for type and heights, fallback without a resolver, `look.resolvedUiFont` as the font family. |
| `src/main.cpp::appearanceLook()` | Now also passes `uiStyle` `uiSize` `themeSetting` `corners` `transparency` `renderer` `uiFont` `resolvedUiFont` `locale` `theme` plus the nested `nativeLook`. |
| `src/ui/window.cpp::applyAppearance` | Takes the full map and pushes it via `themeObject->setProperty("look", …)`. |
| All QML de-hard-coded | `RowDelegate/Main/DetailPane/ReviewDialog/TasksPage` read `Theme.tinyFont/metaFont/smallFont/bodyFont/titleFont` for every font size; bars read `Theme.rowHeight/tabHeight/headerHeight`; monospace is `Theme.monoFamily`. |
| `tools/grab_window.cpp` | `packages_grab_window.exe <out.png> <list\|tasks\|review> <style> <light\|dark>`, with real `nativeLook` values per style (taken from the host's own `Look.qml` files). |
| `tests/theme_tokens_tests.cpp` (new, 70 checks) | Colour parsing, compositing (78 % white over a dark base lands mid-light and opaque), fluent fallback, full brand set, translucent compositing, `rounded=false` zeroes everything, unknown style, success/caution derivation, uiSize tiers, malformed input. |
| `tests/validate.py` (new) | L0: manifest shape (must be `qt:host`, must not be `elevated`), bilingual doc cross-links, x64 PE, package contents and SHA256. |

## 4. Verification

### 4.1 Unit tests

```
ctest --test-dir build/grab -C Release
  1/5 packages_core      Passed  (identity / version / plan / queue / redaction)
  2/5 packages_theme     Passed  (70 checks, 0 failures)
  3/5 packages_fixtures  Passed  (real WinGet fixtures, 2755 checks)
  4/5 packages_scoop     Passed  (Scoop fixtures + argv safety)
  5/5 packages_static    Passed  (manifest / bilingual / x64)
```

Key assertions (theme_tokens_tests):
- Composited `a == 255` (a translucent token can never see through).
- fluent dark `layer = #C7FFFFFF` composited over `#FF202020` lands in the light range but is not pure white.
- `rounded=false` zeroes all seven radii.
- A completely absent `nativeLook` returns the fluent constants (older-host compatibility).

### 4.2 Rendered verification (8 screenshots)

`packages_grab_window.exe build\shots\<style>-<theme>.png list <style> <theme>`, all 980×720:

| File | Check | Result |
|---|---|---|
| fluent-light.png | neutral light-grey base, dark text, blue accent | pass |
| fluent-dark.png | `#202020` base, light text (not hard-coded dark text) | pass |
| brand-light.png | `#A8402C` terracotta accentFill on the primary button and checkbox | pass |
| brand-dark.png | dark base + terracotta | pass |
| tactile-light.png | warm paper base `#F3EFE9`, orange accent | pass |
| tactile-dark.png | `#2A2A2D` warm dark base | pass |
| liquid-light.png | translucent `layer` composited opaque, no bleed-through | pass |
| liquid-dark.png | same, dark | pass |

All 8 were grabbed offscreen on the real Windows platform (`QT_QPA_PLATFORM=windows`); 12–16 px Chinese text renders correctly and rows do not overlap.

### 4.3 Protocol round trip (real process)

```
stdin:  initialize → query(packages) → action(open) → shutdown
stdout: initialize answers protocolVersion=1
        query returns one "Open Packages" row
        action answers {effects:[{type:"hide"}]} and triggers window creation
        shutdown answers {}
```

- The window is created from the host appearance and shown (`windowShouldShow` → `initializeUi` → QML scene loads).
- The screenshots above are that real scene.

## 5. Not tested

- **Not verified inside the real host process.** The appearance pipeline (host → initialize/appearance → SDK → controller → Theme.qml) was exercised with host-sourced values in this tool; differences in how the real host names fields (the `corners` value domain, a missing `uiSize`) are unmeasured. A round with the real host is still needed.
- **liquid's Acrylic backdrop is not replicated** (out of scope by plan): the window uses the composited opaque colour, which differs from the host's Mica/Acrylic material. Recorded as a known limitation.
- **The process still hangs after `app.exec()` returns** (a minimal Qt repro hangs too; suspected local Qt 6.12.0/offscreen or driver environment issue). The host ends plugin processes via its job, so host integration is unaffected; tracked as a todo.
- Install/upgrade/reinstall paths were never executed for real (per the user's choice, every mutation is marked "not executed").
- **Only the `standard` uiSize tier was screenshotted** (the capture tool fixes it); dense/compact/roomy height and font changes are covered by the unit test of the table but not visually.
- Keyboard accessibility (tab order, screen readers) is unverified this round.

## 6. Cleanup

- Removed debug scratch files (`src/core/dbgc.cpp`, `build/l1/dbg*.cpp/exe`, `tools/dbg_scene.cpp`, `build/smoke/crop*.ps1/png`).
- Kept `build/shots/*.png` (the 8 style-verification renders) as evidence material; the formal evidence archive moves into `WinShun/docs/validation/packages-<date>/` once host integration is verified, compressed per the evidence-size rules then.
