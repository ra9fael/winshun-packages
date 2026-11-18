# Packages: editable settings and search actions plan

[中文](sdk-upgrade-plan.md)

## Why change

The host already delivers settings to the Controller, but the window's settings popup is a read-only summary. Users must leave the package window to change behavior that directly affects it, such as scanning policy, information fetching or cache lifetime.

Search queries currently return one Open row. The plugin already implements normal opening, cache-only opening and forced refresh through separate commands. Exposing those existing modes on the row makes them discoverable without requiring additional keywords.

## User-facing scheme

Keep all seven settings on the host card because the set is small:

- UI language.
- Show observed-only rows.
- Cache refresh interval.
- Fetch package information on selection.
- Scan-on-open policy.
- Scan when the lazy plugin starts.
- Detailed diagnostics.

The existing settings popup becomes editable and shows the same settings. Cache/probe summaries and explicit cache maintenance actions remain beside them. Reuse this content for the host settings entry; it should open without triggering an inventory refresh.

The search Open row offers these actions:

| Action | Behavior |
|---|---|
| Open | Follow the saved scan-on-open policy |
| Open cached list | Open without backend queries |
| Open and refresh | Force a fresh backend query |
| Settings | Open configuration without scanning |

Existing keywords remain available. Package installation, removal and upgrades continue through the current identity and plan-review flow.

## Implementation

1. Update the SDK and add Controller-to-plugin notifications for changed settings.
2. Replace summary-only fields with choices, switches and a numeric interval control. Commit individual user changes through `updateSettings()`.
3. Apply incoming settings without echoing them back. Preserve unrelated settings when one control changes.
4. Determine and document the current valid refresh interval semantics before adding bounds. If zero or another sentinel currently has a supported meaning, expose it explicitly; do not accidentally normalize it away.
5. Route item action IDs to existing `OpenMode` values, preserving any text query passed to the window.
6. Add `settingsWindow: true` and a queued `showSettings()` entry that opens the existing window and settings UI through a no-scan path. Keep search Settings as the cold-start entry for the lazy process.
7. Preserve the meaning of scan-on-startup: it applies when the plugin process starts, not necessarily when WinShun starts. Explicit Settings does not itself schedule an extra scan.

All seven preferences remain host-owned. Inventory, search, information and probe caches remain plugin data.

## Acceptance

- Editing each setting in the window updates the host and applies back to the Controller; restart restores the value.
- Editing one setting preserves the other six.
- Cache-only and Settings actions cause no backend query, except any independently configured process-start scan whose semantics must remain explicit.
- Force refresh bypasses cache freshness; ordinary Open follows saved policy.
- Search text still reaches the existing Discover flow.
- Settings updates do not automatically install, uninstall or upgrade anything.
- Relevant Controller/protocol tests and fake-data UI checks pass.

## Documentation

Document editable in-window settings, the four result actions, cache lifetime semantics and the distinction between plugin-start scanning and WinShun-start scanning.
