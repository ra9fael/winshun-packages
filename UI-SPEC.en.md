# Packages UI specification (not implemented)

[中文](UI-SPEC.md) · [Requirements](README.en.md) · [Design](plugin-design.en.md) · [Tests](TEST-PLAN.en.md)

## Entry and information architecture

`packages` opens an independent normal-user window. First use only detects backends and queries Installed; it does not install dependencies or update managers. Follow WinShun appearance/locale notifications with a compact, modern, simple interface. Dimensions, density, virtualization thresholds and pagination capacity follow measurement/accessibility review, not arbitrary requirement-stage caps. Navigation remains Installed / Discover / Updates / Tasks; Chinese labels are 已安装 / 发现 / 更新 / 任务. English names identify stable areas in these documents.

## Main list and details ASCII

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

Rows are synthetic. An upgrade arrow appears only with backend update evidence. All keeps identities separate rather than merging names. Missing scope/source displays unknown, never “any scope.” Managed requires evidence with an expandable explanation; plain WinGet enumeration is observed and catalog matching is matched. Cross-manager duplicate advice has no “automatically remove the other” action.

Installed search filters inventory or explicitly starts a read-only query; Discover searches catalogs locally/remotely. Explain the distinction and possible networking. Updates shows hold/pin, target and scan time, without default select-all. Enter, double-click and selection in Discover only open details. Checkboxes select candidates; an ambiguous Execute button cannot mix operations implicitly. Review selected first names an operation and shows each plan.

Details always expose the complete PackageKey, versions/architecture, scope/source evidence, dependencies, metadata time, material status and capability limits. Explain missing/unsupported actions in text; offer guidance when no package is selected. Install requires an exactly resolved target; Uninstall requires one installed instance. Reinstall names its backend/version strategy and stays disabled if original-version preflight fails; alternative versions require explicit selection. Explain hold/pin blockers without a one-click override. Unknown versions require individual review.

## Action confirmation ASCII

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

Confirmation is disabled until refresh/preflight completes. Preflight downloads/staging writes/source refresh require their own disclosed side-effecting plan and confirmation; opening a review page must not secretly start downloads or updates. Agreement boxes default unchecked and do not imply acceptance of all sources/installers. Show prior backend consent only when its scope is verifiable. Omit irrelevant boxes when no agreement is pending. Refusing source terms produces a restricted-query explanation; refusing installer terms prevents installation.

Refresh again before execution. Identity, target, scope, policy, agreement or dependency changes enter needs review and show a diff for renewed confirmation. Action labels name the operation; version changes cannot be disguised as original-version reinstall. No default purge; explicitly state that avoiding purge still cannot guarantee data retention. Selected batches list individual outcomes and never invoke `*` or upgrade-all.

## Tasks ASCII and states

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

Phases are queue, preflight, acquire, uninstall/install and verify; not every task has every phase. Unknown totals use indeterminate progress and elapsed time. Verified byte totals may show download progress only, never an overall percentage. Duration predictions must say estimated and await L5 calibration. Interactive pending, permission denied, restart required, partial failure and unknown each have distinct text and recovery advice.

Remove pending removes only unstarted work. Request cancel explains its reach: it may stop waiting without terminating an installer/proxy or rolling back. Failed/cancelled installation after reinstall removal explicitly warns that the package may be absent, rather than showing an uncomplicated cancelled outcome. Replan reconciles state and returns to review without replay. Restart is advisory only. Restored work after host exit/disable is interrupted/unknown.

## States, errors and recovery

| State | Required presentation | Recovery |
|---|---|---|
| First load/refresh | Name the backend being queried, offer query cancellation, do not clear into a successful empty state | Cancel query; retain timestamped old inventory |
| Truly empty/no matches | Successful query scope and filters | Clear filters, switch backend |
| Missing backend | Name missing backend and independent status of the other | Manual preparation guidance; no automatic installation |
| Unsupported | Unsupported version/command/locale or invocation path | Diagnostics; keep the other backend |
| Network/source failure | Error origin, last successful time and stale marker | Explicit retry; no automatic source consent |
| Unaccepted agreement | Source/installer distinction, scope and terms | View / Accept / Decline; decline prevents related execution |
| Lock/external change | Waiting cause and invalidated snapshot | Refresh / Replan; no lock seizure |
| Permission/UAC/Job blocked | Denied/unsupported path and real error | Diagnostics/user-directed instructions, not administrator plugin restart |
| Interactive waiting | Identify installer and unknown duration | Switch to verified window or manual lookup guidance; request cancel |
| Partial failure/unknown | Completed stages and potentially affected state | Redacted logs, refresh, replan |
| Restart required | Installation outcome and pending effect | Reconcile later; no automatic reboot |

Settings proposes read-only backend paths/versions/adapter status, optional API dependency information, privacy and record clearing. Read-only source/bucket diagnostics are P1; full edits and pin/hold management are P2. Do not introduce dependency auto-install flows through empty states or settings.

## Keyboard, themes, DPI and screen readers

Tab order follows navigation, backend, search, list, detail actions and footer/queue. Arrow keys move within lists, Enter only opens details, Space selects candidates, and Esc closes the current overlay/returns rather than implicitly cancelling a running operation. Confirmation initially focuses its heading/summary; the default button must not execute a risky operation. Restore focus to the originating row, or a sensible list position if removed. Queue states are keyboard-accessible; disabled-action reasons remain readable.

Use WinShun theme/appearance notifications and system high-contrast semantic colors. Pair icons with text and do not use red alone for errors. DPI/text scaling and mixed-DPI monitor changes must preserve actions. Narrow widths may separate details with a back-to-list action; never truncate essential identity irretrievably. Full IDs are selectable/readable without requesting clipboard permissions. Screen readers expose navigation, selection, identity, scope, risks, agreements, disabled reasons and phases. Announce asynchronous results/status appropriately without moving focus for every log line. Reduced-motion users must not depend on animation for state.

Related FRs: PKG-F01–F24 and F27–F30; PKG-F25/F26 and the test plan cover performance/accessibility verification. Every ASCII drawing is a requirements wireframe, not a screenshot or implementation evidence.
