# Packages plugin validation — task intervention and the uninstall entry (2026-11-12)

[中文](validation-queue.md)

Environment: Windows 11, WinShun 0.22.0 source, Qt 6.12.0 (msvc2022_64), MSVC 14.51, winget v1.29.380 zh-CN.

## 1. The two reported defects

| # | Report | Root cause confirmed by reading the code |
|---|---|---|
| A | The task list could be watched but not managed | None of the three operations took a task id: `removeLastPendingTask()` scanned backwards for the first removable task, `requestCancelLastRunning()` for the first running one, and `retryAction()` accepted no id at all. Clicking row 1 could delete row 5. |
| B | There was no way to reach an uninstall | `openReview()` inferred a single action by rule: installed with a newer version available ⇒ always `Upgrade`. Upgrade and uninstall are independent intents and must not be decided by an implicit rule. |

## 2. What changed

| Change | Notes |
|---|---|
| `src/core/queue.h/.cpp` | Added `removeFinished(id)` / `canRemove(id)` / `canCancel(id)` / `pause()` / `resume()` / `paused()` / `appendLog` / `clearFinished()`; `nextRunnable()` hands out nothing while paused. `TaskLogEntry{at,phase,kind,text,exitCode}` lives on `Task`, capped at 200 entries keeping the tail. |
| `src/core/plan.h/.cpp` | Added `actionFromId()`: an unknown string always maps to `Action::None`, so a bad string can never become a mutation. |
| `src/ui/window.h/.cpp` | `cancelTask/removeTask/retryTask/startTask/clearFinishedTasks/setQueuePaused/toggleQueuePaused` are all by id; `logLine()` is the single write point for logs (re-posted to the GUI thread, then redacted through `redactBlock`); `buildReview()` and `openReview()` share one path; `setReviewAction()` accepts only the actions legal for this review; the executor logs each phase; `statusLine()` reports the queue on the Tasks page; `setTab()` re-emits `statusChanged`. |
| `src/ui/models.h/.cpp` | Added `canRetry` / `canStart` / `logCount` / `logText` roles; `canRemove` now means "anything except a running task". |
| `src/adapters/winget_adapter.h/.cpp` | `pinsCached()`: one mutex-guarded `winget pin list` cache shared by the installed and updates queries, which now run in parallel. |
| `src/adapters/scoop_adapter.cpp` | `locateScoop()` became `resolveScoopRoot()` (pure logic) plus Windows fact gathering: the root order is `%SCOOP%` → the registry `ProfileList\<SID>` for this token (never `USERPROFILE`) → `scoop.ps1` on `PATH` → `%ProgramData%\scoop`. The chosen source is named in the failure text. |
| `src/ui/qml/TasksPage.qml` | Queue controls in the header (pause/resume, clear finished, rescan); each row shows Start/Cancel/Retry/Remove and a log toggle for its own state; the per-step log expands inside the row. |
| `src/ui/qml/ReviewDialog.qml` | Action picker at the top; on uninstall the agreement text becomes an impact acknowledgement. |
| `src/ui/qml/Main.qml` | The hidden search field gives back its `Layout.fillWidth` (the gap root cause); the Tasks page hides "Selected N / Review selected"; the log panel uses an existing token. |

## 3. Evidence

### 3.1 Unit tests

```
ctest --test-dir build/dev
  1/6 packages_core      Passed   (220 checks, 0 failures)
  2/6 packages_theme     Passed
  3/6 packages_process   Passed
  4/6 packages_fixtures  Passed
  5/6 packages_scoop     Passed
  6/6 packages_static    Passed
```

New `queue intervention (T21)` section (44 checks):

- `removeFinished(id)` removes only that id; the other record is untouched; an unknown id is a no-op.
- A running task has `canRemove=false` / `canCancel=true`, and both `removeFinished` and `removePending` return false; the record stays.
- A terminal record can be removed, and `canRemove` becomes false afterwards.
- After `pause()`, `nextRunnable()` returns nothing and says why, while the running task is still `Running` — pause is not cancel. `resume()` yields the queued head immediately.
- `clearFinished()` removes only terminal records, reports how many, and a second call returns 0.
- Over the cap the oldest log entries are dropped and the tail survives; `at` is an 8-character clock and `phase` is correct.

`plan confirmation (T06)` gained `actionFromId` round-trip assertions: all four action ids map both ways, and `"none"` / `""` / `"--force"` all become `Action::None`.

### 3.2 Render evidence

`packages_grab_window.exe <out.png> <list|tasks|review|settings> <style> <light|dark>`:

| File | Checked | Result |
|---|---|---|
| build/shots/v5-tasks.png | Per-row buttons: Partial failure → Retry/Remove/Show log (7); Queued → Start/Remove/Show log (1) | Pass |
| build/shots/v5-tasks.png | In-row log panel: 7 lines with time/phase/text/exit code; errors red, warnings amber, ok green | Pass |
| build/shots/v5-tasks.png | Footer switched to "queue running · 0 active · exit code 0 is not success"; the selection controls are gone | Pass |
| build/shots/v5-tasks-dark-fluent.png | Dark mode: the log panel is dark-on-dark (it previously used a non-existent token and landed on white) | Pass |
| build/shots/v5-review.png | Review page action segment: Upgrade (selected) / Uninstall, with "an update is available; upgrade to 14.1.2" beside it | Pass |
| build/shots/v5-review.png | After switching to uninstall: method becomes `winget uninstall --id … --exact`, material becomes "no material is downloaded", verification becomes "still being listed means the uninstall failed", the source agreement disappears and the checkbox becomes "impact acknowledgement" | Pass |

The Tasks page renders correctly in all four styles × light/dark (`build/shots/v5-tasks-{brand,tactile,liquid}.png` and `build/shots/v5-tasks-dark-fluent.png`).

### 3.3 Geometry

The hidden search field's gap was confirmed with an item-tree dump under `PACKAGES_GRAB_DEBUG=1`:

```
QQuickText 42x18 @0,10 vis=1          // "管理器"
QQuickRectangle 191x24 @179,7 vis=1   // managerSeg
TextField_QMLTYPE_3 200x40 @0,0 vis=0 // hidden search field, still 200 wide
```

After the fix (`Layout.fillWidth: visible` plus `Layout.preferredWidth: visible ? -1 : 0`) the same row measures an 8 px gap in the mockup (`segLeft - lblRight = 8`, measured in the browser).

### 3.4 Static validation

`python tests/validate.py` reported `packaged binary differs` before repackaging (the binary had been rebuilt, the package had not) — expected; it passes after `tools/package.py --dev`.

### 3.5 Scoop discovery (new this round, real evidence)

Reported: the plugin showed `Scoop 未找到 (scoop.ps1 was not found at C:\Users\Default\scoop\shims)` while `scoop list` works on this machine.

| Check | Result |
|---|---|
| Real Scoop | `C:\Users\renfei\scoop\shims\scoop.ps1` exists |
| Registry profile | `ProfileList\S-1-5-21-…-1001 → C:\Users\renfei` |
| `C:\Users\Default\scoop` | does not exist |
| Host elevated environment | `elevatedEnvironment()` takes the system block via `CreateEnvironmentBlock(nullptr)` and fills `USERPROFILE` from `FOLDERID_Profile`; with no user profile loaded, that known folder is `C:\Users\Default` |

So the plugin must not trust `USERPROFILE`. The new order, measured with `tools/probe_scoop.cpp` (which runs a real `scoop export` and feeds it through the parser):

| Scenario | Root | Source | export | parsed packages |
|---|---|---|---|---|
| Normal user environment | `C:\Users\renfei\scoop` | `profile` (registry) | exit 0, 3945 bytes | 6 |
| Simulated elevated: `USERPROFILE=C:\Users\Default` | `C:\Users\renfei\scoop` | `profile` (registry) | exit 0, 3945 bytes | 6 |
| Simulated elevated + scoop removed from `PATH` | `C:\Users\renfei\scoop` | `profile` (registry) | exit 0, 3945 bytes | 6 |

The L1 `scoop root resolution (PKG-F19)` section covers eight branches (`SCOOP` first, profile, shim on `PATH`, forward-slash shim, `ProgramData`, nothing at all, a shim path without `\shims\`, and case preservation) — 203 checks.

Also fixed: `openWindow()` now re-scans whenever any backend is not `Ready`, so a stored `scoop: missing` cannot keep being shown for `refreshMinutes`.

### 3.6 The Installed page could not be acted on (new this round, real evidence)

Three reports — Discover/Installed rows cannot be selected, no uninstall, every plan fails — were one cause chain from two independent bugs.

| Layer | Defect | Consequence |
|---|---|---|
| `WingetAdapter::buildMutation` | called `isResolvable(plan.key)` with the default argument (scope required), while `winget list` prints no scope | every run refused with `identity is not exactly resolvable` → the Tasks page's "every plan failed" |
| `WingetAdapter::fromListText` | `canUninstall = false` unconditionally, and `canUpgrade` relied on an available version `winget list` never provides | all three flags false → `actionable=false` → checkbox `enabled:false` at 35% opacity → the whole page inert |
| Update rows (WinGet upgrade / Scoop manifest) | never set `canUninstall` | the Updates tab was equally inert |

The rule after the fix:

| Action | Identity it needs | Why |
|---|---|---|
| install | manager + source + id + **scope** | it must pass `--scope` |
| upgrade | manager + source + id | `--id X --exact` resolves the instance itself |
| uninstall | manager + source + id | same |

A refresh also merges each update's target version onto the matching installed row by exact identity (`winget list` carries no available version), otherwise an installed row could never offer an upgrade.

Evidence:

- `tests/fixture_tests.cpp`'s `winget command building` gained assertions: an unknown-scope install is refused; an unknown-scope uninstall/upgrade **can** build argv, and that argv contains no `--scope`.
- The real `winget list` fixture's per-row assertion became: ARP rows `canUninstall=false`, every other installed row `canUninstall=true` (and at least one exists) — 2755 → 2843 checks.
- Rendering: `build/shots/v6-review-unknown.png` is the uninstall review for `isOfficeAI.OfficeCLI` (scope `unknown`) — selectable, the action picker works, and **there is no red block**, only the impact acknowledgement to tick. Before the fix the same row showed `scope is not reported by WinGet; a mutation needs an exact scope` in red with everything greyed out.

## 4. Semantics settled this round

- **Pause ≠ cancel**: pausing only stops the queue picking up the next task; the running one keeps running. Cancelling is a separate, explicit action.
- **Retry ≠ replay**: retrying a failed task never re-runs the old record; it reopens the review for the same identity so the user confirms again, because the facts may have changed.
- **Remove ≠ uninstall**: removing deletes the UI record only and never touches machine state. A running task cannot be removed, because that record is the only evidence a process is still touching the machine.
- **Uninstall has no source agreement**: it reads no remote source and installs nothing, so it only asks for an impact acknowledgement.
- **Exit code 0 is not success**: the installed state is re-read after every run; after an uninstall, still being listed means failure.

## 5. Known limitations

- The run path is evidenced by fixtures/L1 and by rendering, **not by a real install or uninstall** — by agreement no real change is executed on the development machine.
- Logs live in memory only and are lost when the process restarts; they are never written to disk (they can contain local paths).
- The executor still runs one task at a time (the core keeps one lane per manager), so there is no concurrent multi-task execution yet.
- Parallel refresh and the task-log structure were demonstrated in `mockup/layout-v4.html` and landed partially this round (`pinsCached()`); the refresh is not yet the fully parallel five-domain design.
