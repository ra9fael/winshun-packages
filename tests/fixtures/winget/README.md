# WinGet fixtures

Captured with tools/capture_fixtures.ps1 on this machine.

- WinGet: 1.29.380
- Culture: zh-CN
- Purpose: output shape only. A passing parse proves the parser reads this
  output; it proves nothing about installing, uninstalling or upgrading.

Verified separately against this capture:
- `--output` is NOT accepted by list/search/upgrade (the tool answers
  "Argument name was not recognized for the current command: '--output'").
- `list` enumerates uninstall-registry entries as `ARP\hive\arch\name`.
  Those are installed software WinGet does not manage: Observed, never managed.
- `list` does not print the install scope, so a mutation cannot be planned
  from a list row without resolving the scope first.
- `--upgrade-available` prints a second table for packages needing explicit
  targeting whose columns are single-space separated; only the first table is
  parsed.

Re-capture with `-Force` when adding a version or locale.
