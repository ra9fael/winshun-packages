# capture_fixtures.ps1 — re-capture the WinGet/Scoop output fixtures.
#
# These fixtures are evidence of OUTPUT SHAPE only. Capturing them proves nothing
# about install/uninstall behaviour, and a passing parse only proves the parser
# reads that shape. Run this only when adding a new backend version or locale, and
# commit the result so a reviewer can see exactly what the parser was built on.
#
# Nothing here changes the machine: every command is a read-only listing.
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $PSScriptRoot '..\tests\fixtures\winget'),
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$env:WINGET_DISABLE_INTERACTIVITY = '1'

$winget = Get-Command winget.exe -ErrorAction SilentlyContinue
if (-not $winget) { throw 'winget.exe was not found; nothing to capture.' }

$version = (& winget --version) -replace '^v', '' -replace '\s.*$', ''
$locale = (Get-Culture).Name
$prefix = "$($locale).v$($version)"
Write-Host "winget $version / $locale"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$captures = @(
    @{ Name = "list.$prefix.txt";            Args = @('list') },
    @{ Name = "list-upgrade-available.$prefix.txt"; Args = @('list', '--upgrade-available') },
    @{ Name = "pin-list.$prefix.txt";        Args = @('pin', 'list') },
    @{ Name = "search-jq.$prefix.txt";       Args = @('search', '--query', 'jq') },
    @{ Name = "help.root.$prefix.txt";       Args = @('--help') },
    @{ Name = "help.list.$prefix.txt";       Args = @('list', '--help') },
    @{ Name = "help.install.$prefix.txt";    Args = @('install', '--help') },
    @{ Name = "help.uninstall.$prefix.txt";  Args = @('uninstall', '--help') },
    @{ Name = "help.upgrade.$prefix.txt";    Args = @('upgrade', '--help') },
    @{ Name = "help.pin.$prefix.txt";        Args = @('pin', '--help') }
)

foreach ($capture in $captures) {
    $target = Join-Path $OutDir $capture.Name
    if ((Test-Path $target) -and -not $Force) {
        Write-Host "skip $($capture.Name) (exists; use -Force to overwrite)"
        continue
    }
    Write-Host "capture $($capture.Name)"
    & winget @($capture.Args + '--disable-interactivity') 2>&1 |
        Out-File -FilePath $target -Encoding utf8
}

# Record what the capture proves, next to it, so a reviewer can tell fixtures from
# verified behaviour.
$note = @"
# WinGet fixtures

Captured with tools/capture_fixtures.ps1 on this machine.

- WinGet: $version
- Culture: $locale
- Purpose: output shape only. A passing parse proves the parser reads this
  output; it proves nothing about installing, uninstalling or upgrading.

Verified separately against this capture:
- ``--output`` is NOT accepted by list/search/upgrade (the tool answers
  "Argument name was not recognized for the current command: '--output'").
- ``list`` enumerates uninstall-registry entries as ``ARP\hive\arch\name``.
  Those are installed software WinGet does not manage: Observed, never managed.
- ``list`` does not print the install scope, so a mutation cannot be planned
  from a list row without resolving the scope first.
- ``--upgrade-available`` prints a second table for packages needing explicit
  targeting whose columns are single-space separated; only the first table is
  parsed.

Re-capture with ``-Force`` when adding a version or locale.
"@
Set-Content -Path (Join-Path $OutDir 'README.md') -Value $note -Encoding utf8

Get-ChildItem $OutDir | Select-Object Name, Length
