# run-dev.ps1 — build and open the development harness.
#
# The plugin is a resident process driven by WinShun over stdio, so every check
# otherwise costs a build, a package, an install and a host restart. The harness
# opens the same window with no host, which makes a runtime fact one command away.
#
#   .\run-dev.ps1                            open the window (real backends)
#   .\run-dev.ps1 -Fake                      deterministic rows, no winget wait
#   .\run-dev.ps1 -Fake -Watch               ... and reload on every .qml save
#   .\run-dev.ps1 -SimulateElevated          reproduce the elevated environment
#   .\run-dev.ps1 -Lint                      check the layout, exit 1 on a defect
#   .\run-dev.ps1 -Plan isOfficeAI.OfficeCLI print the argv, run nothing
#   .\run-dev.ps1 -Shot out.png -Fake        write a PNG and exit
#   .\run-dev.ps1 -Review 4:uninstall        open the review page for row 4
#   .\run-dev.ps1 -ExpectRows 373            fail unless the cache painted 373 rows
#
# vcvars must be set in the same process as the build, so this script does both.
param(
    [string]$Style = 'fluent',
    [string]$Theme = 'system',
    [string]$Size = 'standard',
    [string]$Lang = 'zh',
    [string]$Tab = '',
    [switch]$Fake,
    [switch]$Watch,
    [switch]$Lint,
    [switch]$DumpTree,
    [switch]$SimulateElevated,
    [switch]$RealDataDir,
    [string]$Plan = '',
    [string]$Shot = '',
    [string]$Review = '',
    [int]$ShotDelay = 0,
    [int]$ExpectRows = -1,
    [string]$LogLevel = 'info',
    [int]$QuitAfter = 0,
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$buildDir = Join-Path $root 'build\dev2'
$exe = Join-Path $root 'packages_dev.exe'

$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$qtDir = 'D:\tools\Qt\6.12.0\msvc2022_64'

if (-not $NoBuild) {
    if (-not (Test-Path $buildDir)) {
        & $env:ComSpec /c "call `"$vcvars`" >nul && cd /d $root && cmake -S . -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$qtDir -DPACKAGES_BUILD_DEV_TOOL=ON"
        if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
    }
    & $env:ComSpec /c "call `"$vcvars`" >nul && cd /d $root && cmake --build $buildDir --target packages_dev"
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
}

if (-not (Test-Path $exe)) { throw "missing $exe" }

# Qt's DLLs come from the host in production; here they must be on PATH.
$env:PATH = "$qtDir\bin;$env:PATH"

$argv = @('--style', $Style, '--theme', $Theme, '--size', $Size, '--locale', $Lang, '--log-level', $LogLevel)
if ($Tab) { $argv += @('--tab', $Tab) }
if ($Fake) { $argv += '--fake' }
if ($Watch) { $argv += '--watch' }
if ($Lint) { $argv += '--lint' }
if ($DumpTree) { $argv += '--dump-tree' }
if ($SimulateElevated) { $argv += '--simulate-elevated' }
if ($RealDataDir) { $argv += @('--data-dir', 'real') }
if ($Plan) { $argv += @('--dump-plan', $Plan) }
if ($Shot) { $argv += @('--shot', $Shot) }
if ($Review) { $argv += @('--review', $Review) }
if ($ShotDelay -gt 0) { $argv += @('--shot-delay', $ShotDelay) }
if ($ExpectRows -ge 0) { $argv += @('--expect-rows', $ExpectRows) }
if ($QuitAfter -gt 0) { $argv += @('--quit-after', $QuitAfter) }

# The harness exits when a .qml file changes (in-process scene rebuild is not safe:
# the scene owns the models and the engine is parented to the controller, so a
# rebuild leaves dangling references). Restarting the process takes about two
# seconds and keeps the plugin's own lifetime model untouched.
if ($Watch) {
    $first = $true
    while ($true) {
        if (-not $first) {
            # Rebuild so a C++ edit is picked up too; a .qml edit needs no build.
            if (-not $NoBuild) {
                & $env:ComSpec /c "call `"$vcvars`" >nul && cd /d $root && cmake --build $buildDir --target packages_dev" | Out-Null
            }
            Write-Host '--- .qml changed, restarting harness ---' -ForegroundColor Cyan
        }
        $first = $false
        & $exe @argv
        # 75 is the harness's "a .qml file changed" code: restart. Anything else
        # (0 = window closed, non-zero = failure) ends the session.
        if ($LASTEXITCODE -ne 75) { exit $LASTEXITCODE }
    }
}

& $exe @argv
exit $LASTEXITCODE