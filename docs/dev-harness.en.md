# Development tool: packages_dev.exe

[中文](dev-harness.md)

The plugin is a resident process driven by WinShun over stdio, so every check otherwise costs a build, a package, an install and a host restart — one data point per round. And every bug found so far was a **runtime fact**:

| Bug that happened | Could the package flow catch it? |
|---|---|
| `USERPROFILE` is `C:\Users\Default` under elevation | No — only a real run shows it |
| `winget list` prints no scope, yet scope gated execution | No — needs real output |
| A hidden item kept `Layout.fillWidth`, pushing buttons 137 px | No — needs geometry |
| `theme.controlFill` does not exist, unreadable in dark mode | No — not even by eye |
| Basic-style buttons ignore the theme, light blobs in dark mode | No |

`packages_dev.exe` is the same window with no host. It links **exactly** the same Controller, models, adapters, platform layer and QML scene, so what it shows is what the plugin shows.

## 1. Usage

```powershell
.\run-dev.ps1                            # open the window, real backends
.\run-dev.ps1 -Fake                      # deterministic rows, no winget wait
.\run-dev.ps1 -Fake -Watch               # restart on save, edit QML without relaunching
.\run-dev.ps1 -SimulateElevated          # reproduce the elevated environment
.\run-dev.ps1 -Lint                      # layout check, exit 1 on a defect
.\run-dev.ps1 -Plan isOfficeAI.OfficeCLI # print the argv, run nothing
.\run-dev.ps1 -Review 4:uninstall        # open row 4's uninstall review page
.\run-dev.ps1 -Shot out.png -Fake        # write a PNG and exit
.\run-dev.ps1 -RealDataDir -ExpectRows 373  # assert the cache painted 373 rows
.\check-layout.ps1                       # 6 pages × both themes, full layout check
```

`check-layout.ps1` runs `--lint` over every surface the plugin can show (installed / discover / updates / tasks / review-upgrade / review-uninstall) × light and dark: 12 cases, exit 1 if any fails. It uses `--fake` because the subject is geometry, not the backend.

It starts `packages_dev.exe` **directly** instead of going through `run-dev.ps1`: nesting one PowerShell script inside another per case made the check hang after a few iterations, and "hung" is indistinguishable from "slow build" in CI. Starting the executable directly also gives every case a hard timeout (`-TimeoutSec`, default 30), so a stuck window is recorded as a failure rather than blocking forever.

`--quit-after` has one contract when combined with a capture switch: the capture takes its own exit path, so if `--quit-after` fires first the capture never ran — usually because `--shot-delay` was set longer than `--quit-after`. That case exits 1 and says why, instead of quietly reporting success with no file on disk.

The script sets `vcvars64` and then calls cmake in one process (it is the only place that does both; `-NoBuild` skips the build).

## 2. Switches

| Switch | Effect |
|---|---|
| `--style` `--theme` `--size` `--locale` | Inject the full `nativeLook` (4 styles × light/dark × 4 size tiers) |
| `--tab` | Open on installed / discover / updates / tasks |
| `--qml DIR` | Load QML from DIR (defaults to `src/ui/qml`, which is why a QML edit needs no rebuild) |
| `--fake` | Deterministic snapshot instead of the backends; UI work is not gated on `winget list`. It also short-circuits every backend call (both the scan and the search), so a capture is decided entirely by the injected data and never says "Querying…" |
| `--watch` | Watch `src/ui/qml/*.qml` and restart on change (see §5) |
| `--simulate-elevated` | Set `USERPROFILE` to `C:\Users\Default` and drop scoop from PATH |
| `--data-dir real` | Default writes `ra9fael.packages.dev.data`; only this switch touches the real cache |
| `--dump-plan ID` | Print the uninstall/upgrade/install argv for that id, then exit |
| `--lint` | Layout check, exit 1 on a defect |
| `--refresh` | Scan even when the cache is fresh. `openWindow()` deliberately skips the query within `refreshMinutes`, which is right for the plugin and useless for testing what a new scan produces (whether the update listing reached the disk, above all) |
| `--review N[:uninstall]` | Select row N and open the review page (with `:uninstall` also pick the uninstall action); the only way to check that page's layout |
| `--search TEXT` | Type TEXT into the Discover box and run the query |
| `--installed-filter TEXT` | Type TEXT into the Installed tab's filter box (local filter, starts no process) |
| `--detail-action ROW:ACT` | Press the details pane's action for row ROW the way a user would: **select** it (no checkbox), then act (`upgrade` / `uninstall` / `install`). This switch reproduced the "select then upgrade" crash |
| `--tab-cycle-check` | With `--fake --search TEXT`: verify that an installed Neovim result (ID casing differs) is recognized and that Discover's query and rows survive Installed → Discover switching |
| `--discover-filter new\|all\|installed` | Discover's 结果/Result filter |
| `--select 0,2,5` | Check the given rows, for checking the selection bar |
| `--expect-rows N` | Exit 1 unless the model holds exactly N rows when the window opens, before any backend reply |
| `--dump-tree` | Print the item tree geometry |
| `--shot FILE` | Write a PNG and exit |
| `--shot-delay MS` | Wait MS before the capture (real backends need seconds; default 700) |
| `--log-level quiet\|info\|debug` | Structured output on stderr |
| `--quit-after SEC` | Exit after N seconds (smoke checks) |

## 3. What `--lint` caught

It is not "does it look right" — it encodes the defects that already happened as rules:

**Rule 1: a container with no painted ancestor.** A container that claims area but has no `Rectangle` anywhere up its parent chain lets the window's default background through — white in a dark theme. This found that both `SplitView` and the `TasksPage` root never painted.

**Rule 2: a child wider than its parent.** Over 4 px means clipped or overlapping.

**Rule 3: the gap between 管理器 and the segmented buttons.** Over 40 px is reported. It caught **137 px** immediately:

```
LINT filter-gap: 管理器 ends at 54 but the manager buttons start at 191 (gap 137 px)
```

The cause was not the hidden search field but `RowLayout` mismeasuring a Rectangle whose inner Row is centred on it. Switching to `Row` brought the gap back to 8 px.

> That rule's value is precisely this: I looked at the screenshot twice earlier and concluded "no problem".

## 4. `--dump-plan`: see the command without running it

```
> .\run-dev.ps1 -Plan isOfficeAI.OfficeCLI
action uninstall state draft
  winget.exe uninstall --id isOfficeAI.OfficeCLI --exact --disable-interactivity
action upgrade   state blocked  blocked: target version is unknown or not orderable
action install   state blocked  blocked: identity is not exactly resolvable (scope must be known)
```

That proves both "an unknown scope no longer blocks an uninstall" and "an install still requires a scope". No mutating process is started.

## 5. Why `--watch` restarts instead of hot-reloading

Destroying and rebuilding the scene in-process crashes (`0xC0000005`): `initializeUi` parents the engine to the controller and the scene's context owns the models, so deleting the window leaves several dangling references.

Rewriting that lifetime handling for a reload would trade a verified path for a fragile one. So `--watch` exits with code **75** on a change and `run-dev.ps1` restarts the process. A launch is about two seconds and the plugin's lifetime model is untouched.

## 6. What it cannot replace

| Only the host can provide | Note |
|---|---|
| The real `nativeLook` | The tool's values come from the host's `Look.qml`, but real field naming/defaults still need a live check |
| A real `elevated` token | The tool simulates the environment, not the privilege |
| Protocol round-trips | initialize / appearance / settingsChanged / shuttingDown |
| Host permission gating and settings page | Absent from the tool |

So the milestone is "harness green → one host round-trip", not "harness green = shippable".

## 7. `--expect-rows`: whether the cache is actually working

This is the second defect the tool caught that could not be seen by eye. The cache is supposed to make the window open instantly within `refreshMinutes`, but a cold start showed an empty list and rows only came back after pressing refresh.

Root cause: `loadCache()` runs in the constructor, and its final `rebuildRows()` returns immediately when `m_packages == nullptr` — the model is not attached yet. The cache was read and then never turned into rows.

`--expect-rows` checks the model's row count **before the event loop starts**, when no backend reply can possibly have arrived, so the rows can only have come from the disk cache:

```
> .\packages_dev.exe --data-dir real --expect-rows 373 --quit-after 2
rows  373 rows from the cache, before any backend reply
```

A wrong count exits 1, so it is both an assertion and self-checking — `--expect-rows 99999` prints `wanted 99999 rows ... found 373` and exits 1.

## 8. Not shipped

`packages_dev.exe`, `tools/look_fixture.*`, `tools/dev_main.cpp`, `run-dev.ps1` and `check-layout.ps1` are **not** in `tools/package.py`'s whitelist. They are development tools, not part of the plugin; `packages.exe` has no `--dry-run`-style switch either.

The `qmlSourceDir` parameter added to `initializeUi` is non-empty only in the development tool; the plugin passes an empty string and loads from the qrc resource exactly as before.
