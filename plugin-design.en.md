# Packages plugin design (requirements stage)

[中文](plugin-design.md) · [Requirements/FRs](README.en.md) · [UI](UI-SPEC.en.md) · [Tests](TEST-PLAN.en.md)

## 1. Verdict

Unify queries and operation plans without erasing backend identity, permissions or command semantics. Every mutation requires a verifiable exact identity, current state and user confirmation. Unproven versions and installer paths remain disabled. This is documentation only; implementation requires design approval.

PKG-F01–PKG-F30 in README define scope and priority. P0 covers everyday normal-user queries and selected operations; full source/bucket/pin management is deferred. Architecture, file layout and protocol wiring below are proposals, not existing code.

## 2. Grounded facts

This task performed document/source inspection only, with no local runtime measurements.

| Evidence | Established content | Strength/limits |
|---|---|---|
| [Workspace rules](../DEVELOPMENT.md) | Eleven design sections, bilingual documents, L0–L5 and the L3 gate | Local documentation fact |
| [Host permissions](../WinShun/docs/plugins.md) | normal means medium integrity with administrator token removal by default; processes permits children but keeps them in the Job; host exit ends the Job; network/files:read are declarative, as is files:write under normal | Documentation facts, not proof that any installer works |
| Same source | elevated follows an administrator host and confirmed copy; it does not raise UAC for a plugin | Documentation fact; this design does not request elevated |
| [WinGet overview](https://learn.microsoft.com/en-us/windows/package-manager/winget/) | search/list/show/install/upgrade/uninstall/repair; reinstall is not listed | Official documentation read during this session; no universal JSON claim follows |
| Official Scoop help source | Help extracts Usage/Help from command_path source | Retrieved with serial read-only gh api at a fixed revision |
| Official Scoop update source | No-argument update calls Sync-Scoop and Sync-Bucket; app operations may first synchronize Scoop/buckets; update app upgrades, held apps are skipped | Source fact; Git/config/metadata writes are possible, so this is not read-only |
| Official Scoop install source | Help supports app@version, no force; installed versions are skipped; self-update may happen by default, with a --no-update-scoop option | Version selection does not guarantee historical installation material exists |
| Official Scoop update/reset source | Force update resolves a target from current manifests and @version pins can resolve to bucket HEAD; reset restores current, shims, shortcuts, environment and persist links | Force is not guaranteed original-version reinstall; reset is not reinstall |

Scoop evidence is pinned to `e6aa3b366bdee8ed138c1e0f7b85192ebdd35d0f`. Only the revision and four necessary files were read, without cloning the repository:

- [scoop-help.ps1](https://github.com/ScoopInstaller/Scoop/blob/e6aa3b366bdee8ed138c1e0f7b85192ebdd35d0f/libexec/scoop-help.ps1)
- [scoop-update.ps1](https://github.com/ScoopInstaller/Scoop/blob/e6aa3b366bdee8ed138c1e0f7b85192ebdd35d0f/libexec/scoop-update.ps1)
- [scoop-install.ps1](https://github.com/ScoopInstaller/Scoop/blob/e6aa3b366bdee8ed138c1e0f7b85192ebdd35d0f/libexec/scoop-install.ps1)
- [scoop-reset.ps1](https://github.com/ScoopInstaller/Scoop/blob/e6aa3b366bdee8ed138c1e0f7b85192ebdd35d0f/libexec/scoop-reset.ps1)

### Reinstall capability table: decide per backend/version

Capability records include backend, exact version/revision, OS, command/option help summary, invocation kind, scope, language, installer type and L3/L4 evidence. Candidates below are not supported execution capabilities. “Latest” cannot stand for all versions.

| Backend/version | Original-version force install | Reinstall candidate and current decision |
|---|---|---|
| Scoop at the pinned revision | Install help has no force; disable that path. Update force does not guarantee the original version; reset is inapplicable | Consider only explicit uninstall then install. First verify acquisition/staging and integrity of original-version material; @version help is insufficient. Not runtime-tested; execution disabled. |
| Other Scoop versions/revisions | Obtain that version's help and verify same-version behavior; disable without evidence | Do not inherit implementation details. Build a new capability record; use a preflighted two-stage plan where necessary. |
| WinGet, supported version not yet selected | The overview does not establish original-version reinstall via force install; disable that path | Select a concrete version and verify install version/force/exact-target behavior and material acquisition. Otherwise consider only preflighted explicit uninstall/install. No winget reinstall; repair is separate and cannot substitute. |
| Every future added WinGet version | Enable only when that version's help, fixtures and L3/L4 demonstrate original-version behavior | Missing evidence, ambiguous targeting or unavailable material disables reinstall while retaining read-only queries. |

Two-stage reinstall is non-atomic. Stop if uninstall fails. If removal succeeds and installation fails, show removed/install failed, retain target version/material references and diagnostics, refresh actual state and provide manual recovery information. Unavailable original versions require stopping or explicit alternative selection, never silent upgrading. No default purge or promise of data retention/rollback. Even an approved future force path must disclose installer side effects and respect hold/pin.

## 3. Architecture

Proposed runtime: a bundled executable with an independent compact UI, resident mode, normal sandbox and lazy first-use startup. The UI thread only renders; workers/asynchronous processes handle queries and operations without blocking stdio JSON-RPC. Runtime/UI toolkit choice awaits dependency and packaging review.

Proposed components: Host bridge → UI model → Query coordinator / Plan service → Scoop adapter and WinGet adapter → Process runner. A mutation scheduler accepts immutable plans; an evidence store retains redacted events. The pure core consumes data/events; the OS layer handles processes, Jobs, files and optional APIs.

Proposed responsibilities: future plugin.json for metadata/permissions/commands, src/core/ for identity/plans/state machines, src/adapters/ for version adapters, src/platform/ for Windows process handling, src/ui/ for views, tests/fixtures/ for redacted output. None are created now. Manifest intent is prose only: id ra9fael.packages, resident, lazy, normal, command packages, permissions in section 6; no configuration is authored.

Host initialize supplies locale, appearance and data directory; appearance notifications update UI. Keyword queries return an “Open Packages” entry or read-only results. Actions only open the window/details; mutation confirmation happens inside that window. Stdout carries protocol only; diagnostics use stderr or redacted records. Actual SDK wiring/minWinShun must be checked before implementation; do not invent host RPCs. Disable, exit, timeout and late responses invalidate old action tokens.

## 4. Core logic

### Identity and provenance

PackageKey = manager + source/bucket + exact id + scope. Scope uses backend-supported values such as user/machine/global/unknown and retains its evidence. Missing source/scope is explicit unknown, never a wildcard. Unresolved identities remain read-only until precisely resolved; unsupported scope selection cannot trigger an automatic retry in another scope. Version, architecture and installer-instance locators are snapshot/execution conditions; display names never replace IDs.

Inventory provenance is observed (enumeration only), matched (catalog match), or backend-recorded (backend installation-record evidence). Show managed only with adequate evidence and explain that this does not guarantee original installation by that manager. WinGet list can include installations made outside WinGet; matching a source package does not prove ownership. Possible Scoop/WinGet duplicates are advisory, never merged or automatically uninstalled. Observed uninstall additionally requires a unique instance, verified scope and supported uninstall locator.

### Queries and adapters

Installed / Discover / details / update-list are read-only intents, but each version adapter must review real side effects. Argument-free Scoop update is not update-list and cannot run automatically for search/update checks. Mark old metadata stale. If an upgrade command self-updates, disclose this and obtain separate approval; suppress it using verified options where possible, otherwise block an operation that cannot meet this requirement.

Measure debounce and query concurrency rather than inventing values. Each input creates a generation, cancels its predecessor and rejects late results even on success. Query cancellation does not cancel confirmed mutations. Avoid lock contention when queries compete with mutations; show waiting or an older snapshot.

Adapters produce domain models with original versions, error categories and observation times. WinGet CLI parsing uses a version × command × locale × encoding fixture allowlist, including localized headers, Unicode, truncation, control sequences and errors. Do not guess columns. Missing fixtures mean unsupported. Structured APIs/modules are optional: explain dependencies/capabilities and let the user choose; never install automatically. Candidate Scoop search/list/info/status queries still need per-version output/side-effect review and are not marked verified by this task.

Scoop may be a .ps1/PowerShell shim rather than an executable. Use a trusted fixed PowerShell entry, noninteractive invocation and typed parameter binding, never concatenated user text. Serialization, paths, special characters and option-like IDs require L1/L3 verification. Disable unstable/unsafe invocation. WinGet uses a verified path and correct Windows argv encoding, with no shell fallback. Require exact IDs and explicit source/scope; never construct mutations from display names, arbitrary user URLs, scripts or extra flags.

### Plans, queues and recovery

A Plan contains PackageKey, installed/target version, scope/architecture, dependencies, material availability/integrity, policies, agreements, permission path, metadata time, capability evidence version and digest. Flow: refresh → resolve → preflight → review → confirm → queue. Refresh/compare again before execution; a changed digest enters needs review and cannot reuse approval. Confirmation names the operation/version, not an opaque script.

One serial mutation lane per manager. Both reinstall stages occupy the same lane with no interleaving. Cross-manager queries may proceed independently; suspected mutations of the same installation pause for review. External process/record changes are signals, not complete race detection. Wait or fail on lock conflict; never remove/seize locks. Deduplication cannot merge different scopes. External changes invalidate affected caches/plans.

Task states include queued, preflight, needs review, blocked, running (acquire/uninstall/install/verify), interactive pending, cancel requested, reboot required, succeeded, failed, partial failure, cancelled and interrupted/unknown. Cancellation after reinstall removal can leave the package absent. Request cancellation, record its reach and reconcile actual state; killed does not mean rolled back. A waiting installer window with no output is pending, with a switch-to-installer action; silence/timeouts cannot imply success. Exit codes plus installed-version/instance verification determine outcomes. Pending restarts do not count as fully effective completion.

## 5. Interaction

See the [UI specification](UI-SPEC.en.md). The packages entry has no execution syntax; display names are not stable IDs. Installed defaults to read-only, Enter in Discover opens details, and every mutation has its own action and confirmation. Updates has no default select-all and cannot bypass hold/pin or unknown-version review. Tasks shows blockers, phases and the estimated nature of progress.

## 6. Permissions, data and privacy

| Proposed declaration | Actual purpose and limits |
|---|---|
| ui | Independent window, confirmation, details and installer-wait prompts; does not remove Job/process mitigation restrictions. |
| processes | Start managers, necessary PowerShell and manager-launched installers; children remain in the host Job. This is not administrator authorization. |
| network | Source searches, metadata/agreements, installation materials and manager-originated traffic; the host cannot enforce denial. Do not claim local queries never use the network. |
| files:read | Declare actual paths for private cache, manager installation records/manifests and selected material; minimize concrete paths after adapter review. Declarative, not a read sandbox. |
| files:write | Settings, redacted tasks and cache in WS_DATA_DIR. Managers/installers also write software directories, user settings, persist, source metadata and potentially registry/environment state; disclose these in plans. Declarative under normal. |

Do not request elevated, network:listen, clipboard, host content index or system:settings. Normal is not file-write isolation. Restricting the plugin's own data directory does not confine third-party installer writes there.

Probe installer children, COM/service proxies, UAC brokers, nested Jobs and host exit separately in isolated L3/L4 VMs. Any permitted per-task UAC path still requires explicit user confirmation; the host does not raise UAC for the plugin. Refusal returns blocked/denied. If unsupported by the host model, disable the path and offer user-directed manual instructions; never elevate the whole plugin, break away/remove the Job or change host permission policy. Administrator scopes stay disabled until probes pass.

Minimize logs: no full environment dumps, tokens, source credentials or full personal paths. Retain redacted IDs, versions, phases, exit codes and necessary error excerpts. Persist no executable shell text; restoration data is untrusted. Export is user-triggered with redaction preview. Retention/capacity follows L5 measurement and includes clear controls. No telemetry.

## 7. Failure matrix

| Trigger | Presentation | Recovery/degradation |
|---|---|---|
| Missing backend, untrusted path or unknown version | missing/unsupported | Keep the other backend, provide read-only guidance, do not auto-install. |
| Permission denial, Job constraint or UAC refusal | denied/blocked with error source | No whole-plugin elevation or Job escape; record unsupported path. |
| Fixture mismatch or truncated output | unsupported/incomplete | No executable target and no guessed parsing. |
| Network failure or unaccepted source agreement | stale/error/agreement required | Keep cache read-only; refusal stops related work. |
| Ambiguous scope/source/ID or non-native record | needs review | Disable mutation until unique exact resolution. |
| Hold/pin or unknown version | blocked/manual review | No automatic removal/override; review individual policy. |
| External manager change/lock | stale/conflict | Refresh, invalidate approval, wait or user retry. |
| Reinstall material missing/hash failure | preflight blocked | Stop before uninstall; no hash bypass; reconfirm another version. |
| Reinstall uninstall failure | failed | Do not start installation; reconcile state. |
| Removed then install failed/cancelled | partial failure | Explain absence/data risks, retain recovery information, user replans. |
| Interactive installer waiting | interactive pending | Offer window switching; silence/timeouts are not success; no blind relaunch. |
| Restart needed or version mismatch after exit | reboot required/unknown | Explain later reconciliation; no automatic reboot. |
| Host exit/crash/disable | interrupted/unknown | Job may kill children while external proxies remain uncontrolled; reconcile on recovery, never replay. |
| Excessive output/disk exhaustion | incomplete/failed | Mark diagnostic truncation and stop parsing; capacity derives from L5, not unbounded buffers. |

## 8. Deliverables

This task delivers only README.md/.en.md, plugin-design.md/.en.md, UI-SPEC.md/.en.md and TEST-PLAN.md/.en.md: eight documents. Implementation approval separately covers manifest, adapters, UI, fixtures, packaging, bilingual CHANGELOG and evidence. None are created now; no commits or software install/uninstall operations.

## 9. Test plan (L0–L5)

The [row-level test plan](TEST-PLAN.en.md) contains 36 cases, each with ID / FR / layer / preconditions / steps / expected / evidence / not executed. L0 checks documents and future manifest/packaging (currently absent); L1 covers portable pure logic; L2 fake host/backends; L3 real-host round trips in isolated VMs; L4 unlocked VM desktop, real input and disposable packages; L5 calibration/release. L3 gates implementation acceptance; specific installer/UAC paths need both L3/L4. All cases are currently unexecuted.

## 10. Honest notes

Measured: no package-manager, installer, UI, host round-trip or performance runtime tests. Inspected: section 2 local documents and pinned official Scoop source; WinGet command scope follows the official documentation read during this session.

Inferred: unified models, serial lanes and conservative capability gating can reduce mistakes; Job/proxy interactions may block some installers. These are not tested compatibility conclusions.

Unverified/open: concrete backend version matrices/localized fixtures; Scoop query side effects; optional WinGet API/module selection and deployment licensing; acquisition/staging; original-version reinstall safety; unknown scope/observed uninstall targeting; Job, UAC, interactive installers, restart/cancellation recovery; external activity detection reliability; capacity/performance. Untested paths remain unsupported.

## 11. Delivery report (fill after implementation)

Current delivery is requirements documentation with no implementation version, runtime evidence or release acceptance. Append an addendum for each future version: implementation version/commit, adopted backend/version matrix, deviations, test IDs/evidence, measured/inferred/untested boundaries, outstanding blockers and cleanup. Document completion must never be described as plugin completion.
