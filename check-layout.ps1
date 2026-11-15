# check-layout.ps1 — one command that lints every page the plugin can show.
#
# The layout regressions this has caught (a 137 px gap from RowLayout
# mis-measuring, an unpainted SplitView, unthemed controls) share one property:
# they are invisible to a code review and easy to misjudge in a screenshot. A
# check that a script can run is worth more than a careful look, so this lints
# all six surfaces in both themes and fails on the first defect.
#
#   .\check-layout.ps1                 lint everything (fast: fake data)
#   .\check-layout.ps1 -NoBuild        skip the build
#   .\check-layout.ps1 -TimeoutSec 40  per-case hard timeout
#
# Every case runs with --fake: the pages it checks are populated by the model, and
# a real backend would add tens of seconds of waiting without changing the
# geometry being measured.
#
# The harness is started directly rather than through run-dev.ps1. Nesting one
# PowerShell script inside another per case made this check hang after a few
# iterations, and a hang is indistinguishable from a slow build in CI. A direct
# Start-Process also gives each case a hard timeout, so a stuck window reports a
# failure instead of blocking forever.
param(
    [switch]$NoBuild,
    [int]$TimeoutSec = 30
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$buildDir = Join-Path $root 'build\dev2'
$exe = Join-Path $root 'packages_dev.exe'
$qtDir = 'D:\tools\Qt\6.12.0\msvc2022_64'
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'

if (-not $NoBuild) {
    if (-not (Test-Path $buildDir)) {
        & $env:ComSpec /c "call `"$vcvars`" >nul && cd /d $root && cmake -S . -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$qtDir -DPACKAGES_BUILD_DEV_TOOL=ON" | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
    }
    & $env:ComSpec /c "call `"$vcvars`" >nul && cd /d $root && cmake --build $buildDir --target packages_dev" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
}

if (-not (Test-Path $exe)) { throw "missing $exe (run without -NoBuild)" }

# Qt's DLLs come from the host in production; here they must be on PATH.
$env:PATH = "$qtDir\bin;$env:PATH"

# label, and the selector that shows the surface under test
$cases = @(
    @('installed', '--tab', 'installed'),
    @('discover', '--tab', 'discover'),
    @('updates', '--tab', 'updates'),
    @('tasks', '--tab', 'tasks'),
    @('review', '--review', '4'),
    @('review-uninstall', '--review', '4:uninstall')
)

$logDir = Join-Path $buildDir 'layout-check'
New-Item -ItemType Directory -Force $logDir | Out-Null

$failures = @()
foreach ($theme in 'dark', 'light') {
    foreach ($case in $cases) {
        $label = "$theme/$($case[0])"
        $outFile = Join-Path $logDir "$theme-$($case[0]).log"
        $errFile = Join-Path $logDir "$theme-$($case[0]).err"
        $argv = @('--style', 'fluent', '--theme', $theme, '--size', 'standard',
                  '--locale', 'zh', '--fake', '--lint', '--quit-after', '4')
        $argv += $case[1..($case.Count - 1)]

        $p = Start-Process -FilePath $exe -ArgumentList $argv -PassThru -NoNewWindow `
                           -RedirectStandardOutput $outFile -RedirectStandardError $errFile
        # Start-Process -PassThru does not keep the process handle, so ExitCode reads
        # as $null and every case looks like a failure even when the lint passed.
        # Touching Handle caches it before the exit is observed.
        $null = $p.Handle
        $timeoutMs = $TimeoutSec * 1000
        if (-not $p.WaitForExit($timeoutMs)) {
            try { $p.Kill() } catch { }
            $failures += $label
            Write-Host ("FAIL {0}: no exit within {1}s (hung)" -f $label, $TimeoutSec) -ForegroundColor Red
            continue
        }
        $code = $p.ExitCode
        $detail = (Get-Content $errFile -ErrorAction SilentlyContinue | Select-String 'LINT ' | Select-Object -Last 1).Line
        if ($code -ne 0) {
            $failures += $label
            Write-Host ("FAIL {0}: exit {1} {2}" -f $label, $code, $detail) -ForegroundColor Red
        } else {
            Write-Host ("ok   {0}" -f $label) -ForegroundColor Green
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host ("`n{0} layout check(s) failed: {1}" -f $failures.Count, ($failures -join ', ')) -ForegroundColor Red
    Write-Host "logs: $logDir"
    exit 1
}
Write-Host "`nall layout checks passed (6 surfaces x 2 themes)" -ForegroundColor Green
exit 0
