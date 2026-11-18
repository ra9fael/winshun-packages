# Implementation notes

[中文](implementation.md)

A reader's map of the implementation. Requirements live in the README; the design verdicts and failure matrix live in the plugin design document at the winshun-dev workbench root.

## Layout

| Path | Responsibility |
|---|---|
| `plugin.json` | The single authoritative manifest: runtime (resident/lazy/normal/qt:host), permission allow-list, commands |
| `src/core/` | Pure logic, no OS headers, unit-testable anywhere: identity, version (comparison), model, plan (confirmation invalidation, action-id round-trip), queue (one serial lane per manager, per-id intervention, pause, per-step log), redact, json_lite (Scoop export parsing), cache (disk-cache serialisation), theme_tokens (host appearance resolution and compositing) |
| `src/adapters/` | WinGet/Scoop adapters: detection, fixture-gated parsing, argv construction |
| `src/platform/` | CreateProcessW + child job, pipe capture, cancellation |
| `src/ui/` | Qt Quick window: controller (window.h), models, theme_resolver, qml_window (scene loading) |
| `src/ui/qml/` | Main / Theme / RowDelegate / DetailPane / ReviewDialog / TasksPage / RowMenu, plus three themed controls: ThemedButton / ThemedField / ThemedCheckBox. In DetailPane the exact ID is its own copyable box (`key: "exactId"`), beside a `winget show` info block; `fieldAt()`'s key names are a closed set and a wrong one returns an empty string silently |
| `tests/` | L1 pure logic (core/theme), fixture tests (winget/scoop), L2 real child processes (process), L0 static validation (validate.py) |
| `tools/` | package.py (deterministic packaging), capture_fixtures.ps1 (fixture capture), grab_window (offscreen render verification), probe_scoop (Scoop root discovery and real-export diagnostic) |

## Threading

- Main thread = Qt GUI (window and scene).
- Protocol thread = `winshun::Plugin::serve()`, owns stdio; talks to the GUI via `QMetaObject::invokeMethod(QueuedConnection)`.
- Query thread = one detached worker per refresh (`startBackendRefresh`) that forks two more threads (WinGet installed+updates / Scoop installed), results posted back to the GUI; `m_refreshInFlight` prevents re-entry.
- Executor thread = `drainQueue` picks a runnable task and `runOneTask` drives it on a detached worker, one `platform::run` per phase; `logLine` re-posts to the GUI thread before writing, and `m_runCancel` is polled by both the executor and `run()`.
- Search thread = single-flight for the Discover query; a stale result is dropped.
- Settings sync = QML calls the Controller on the GUI thread with one changed key; the Controller passes that map to the SDK's `updateSettings()` notification. After persistence, the host sends the merged values back through `settingsChanged`. Incoming host values are applied without being sent back.

The window's settings popup edits the seven host-managed manifest preferences. Caches remain in the plugin data folder; `refreshMinutes` of 0 keeps the cache perpetually stale. Search actions map to policy-based open, no-scan open, forced refresh and a no-scan Settings entry. `scanOnStartup` means one background scan when this lazy plugin starts; it does not preload the plugin when WinShun starts.

## Safety boundaries (where they live)

| Rule | Implementation |
|---|---|
| No shell concatenation, per-argument validation | `platform/process.cpp::quoteArgument` + each adapter's whitelist |
| unknown is not a wildcard, scope must be exact | `core/identity.cpp::isResolvable` |
| Agreements are never auto-accepted | `winget_adapter.cpp::buildMutation` (`--accept-*-agreements` only when explicitly passed) |
| Confirmation invalidation | `core/plan.cpp::invalidateIfChanged` (facts compared before dequeue) |
| Serial per manager, reinstall occupies its lane for both phases | `core/queue.cpp` |
| Redacted logs, bounded buffering | `core/redact.cpp` + `ui/window.cpp::logLine` (the single write point) + `Queue::appendLog` (cap 200, tail kept) |
| A running task cannot be removed | `core/queue.cpp::canRemove` / `removeFinished` |
| An unknown action string can never become a mutation | `core/plan.cpp::actionFromId` (anything unknown is `Action::None`) |

## Build

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=<Qt>/6.12.0/msvc2022_64
cmake --build build --parallel
ctest --test-dir build -C Release
```

The shared Qt contract requires MSVC, Release and the dynamic CRT; the package ships no Qt DLLs.
