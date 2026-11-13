# Packages plugin validation — overview (2026-11-12)

[中文](validation.md)

This file is the overview of every validation record. Two focused records cover UI style consistency and task intervention / the uninstall entry.

Environment: Windows 11, WinShun 0.22.0 source, Qt 6.12.0 (msvc2022_64), MSVC 14.51, winget v1.29.380 zh-CN, Python 3.12 (used only by the L0 script).

## 1. Validation layers

| Layer | Content | Command | Status |
|---|---|---|---|
| L0 static | manifest shape, bilingual doc links, x64 PE, package contents and SHA256 | `python tests/validate.py` | Pass |
| L1 pure logic | identity, version, plan, queue, queue intervention, redaction, theme tokens, disk cache | `ctest -R packages_core` etc. | 220 + 70 checks pass |
| L1 fixtures | WinGet real-output parsing (2755 checks), Scoop fixtures and argv safety (188 checks) | `ctest -R packages_fixtures` etc. | Pass |
| L2 real child processes | `run()` output capture, timeout, cancel, exit code | `ctest -R packages_process` | 7 cases pass |
| Rendering | four styles × light/dark, list/tasks/review/settings states | `packages_grab_window.exe` | captured and checked per state |

All at once:

```
ctest --test-dir build/dev --output-on-failure
  1/6 packages_core      Passed
  2/6 packages_theme     Passed
  3/6 packages_process   Passed
  4/6 packages_fixtures  Passed
  5/6 packages_scoop     Passed
  6/6 packages_static    Passed
```

## 2. Focused records

| Record | Covers |
|---|---|
| [validation-theme.md](validation-theme.md) | Style consistency with WinShun: token compositing, the four `uiSize` tiers, removing hardcoded sizes, 8 screenshot checks |
| [validation-queue.md](validation-queue.md) | Per-task intervention, queue pause/clear, per-step logs and redaction, the review action picker (uninstall entry) |

## 3. Cross-cutting assertions

- **Exit code 0 is not success**: the installed state is re-read after every change and only `verifiedAfterRun` decides the result; after an uninstall, still being listed means failure.
- **Elevation only follows the host**: `elevated` reports the process's real administrator state and the plugin never raises UAC; without it, machine/global scopes are blocked at preflight with the reason stated.
- **No agreement is ever auto-accepted**: source and installer agreements are checked one by one and reset when the action changes.
- **No shell command is concatenated**: argv is built in exactly one place (`buildMutation`) and package ids pass a whitelist (option-like strings, URLs, scripts and control characters are refused).
- **Anything that cannot change machine state is not treated as a change**: removing a task record deletes a UI record only; pausing only stops the queue picking up new work.

## 4. No real change was executed

No real install, uninstall or upgrade was performed on a developer machine during development (by agreement). The evidence for the run path is fixtures, L1 and rendering checks, not a live install. Real-machine integration should happen after host attachment, with evidence archived according to the host's evidence discipline.

## 5. Known limitations

- The appearance pipeline is simulated in the grab tool with host-identical values and **has not been verified inside the real host process**; a round is still needed after integration.
- liquid's Acrylic/Mica backdrop is not replicated (the plugin cannot obtain the host's DWM backdrop), so the composited opaque palette is used.
- The process hangs after `app.exec()` returns (a minimal Qt repro does too; suspected local Qt environment issue — the host ends plugin processes via its job).
- Only the standard `uiSize` tier was checked by screenshot; dense/compact/roomy are covered by unit tests on the value table only.
- Keyboard accessibility (tab order, screen readers) is unverified.
- Refresh is not yet the fully parallel five-domain design; `winget pin list` and `scoop export` now each have one cache per scan.
- Scoop root discovery was verified with a real `scoop export` in **both the normal and a simulated elevated environment**, but **not inside a real elevated host process** (that needs a run with WinShun as administrator and the plugin granted `elevated`).
