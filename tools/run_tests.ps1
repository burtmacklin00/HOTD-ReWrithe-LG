# run_tests.ps1 (worker "tests", 2026-11-09; docs/testing.md "CTest"): builds hotd + hotd_unit and runs the CTest suite
# (the unit layer: no game data; the module layer: the headless runs, skipped without the game files), then prints a
# summary table. Exit code: 0 when nothing failed (skipped tests are not failures).
#
#   powershell -ExecutionPolicy Bypass -File tools\run_tests.ps1 [-Build build] [-Config Release] [-NoBuild]
#       [-Layer all|unit|module|translation] [-Only <ctest -R regex>] [-Jobs 4]
#       [-Root <folder with private\: hotd.zip, game_copies\pc_spanish>] [-Arcade <zip>] [-Pc <PC game folder>]
#       [-Es <Spanish PC copy>] [-Harness <PC folder for the input test>]
#
# The game files come from HOTD_TEST_ARCADE / HOTD_TEST_PC / HOTD_TEST_ES / HOTD_TEST_PC_HARNESS, else from the
# parameters, else from -Root's private\ (hotd.zip, pcharness\game, game_copies\pc_spanish\extracted\installed).
param(
    [string]$Build = "",
    [string]$Config = "Release",
    [switch]$NoBuild,
    [string]$Layer = "all",
    [string]$Only = "",
    [int]$Jobs = 4,
    [string]$Root = "",
    [string]$Arcade = "",
    [string]$Pc = "",
    [string]$Es = "",
    [string]$Harness = ""
)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $PSScriptRoot
if (-not $Build) { $Build = Join-Path $here "build" }
if (-not $Root) { $Root = $here }

function Set-TestPath([string]$var, [string]$param, [string]$fallback) {
    $cur = [Environment]::GetEnvironmentVariable($var)
    if ($cur) { return }
    $v = if ($param) { $param } elseif ($fallback -and (Test-Path $fallback)) { $fallback } else { "" }
    if ($v) { [Environment]::SetEnvironmentVariable($var, (Resolve-Path $v).Path) }
}
$priv = Join-Path $Root "private"
Set-TestPath "HOTD_TEST_ARCADE" $Arcade (Join-Path $priv "hotd.zip")
Set-TestPath "HOTD_TEST_PC" $Pc (Join-Path $priv "pcharness\game")
Set-TestPath "HOTD_TEST_ES" $Es (Join-Path $priv "game_copies\pc_spanish\extracted\installed")
Set-TestPath "HOTD_TEST_PC_HARNESS" $Harness (Join-Path $priv "pcharness\game")
foreach ($v in "HOTD_TEST_ARCADE", "HOTD_TEST_PC", "HOTD_TEST_ES", "HOTD_TEST_PC_HARNESS") {
    $x = [Environment]::GetEnvironmentVariable($v)
    "{0,-22} {1}" -f $v, $(if ($x) { $x } else { "(not set: its module tests are skipped)" })
}

$t0 = Get-Date
if (-not $NoBuild) {
    $targets = @(if ($Layer -eq "unit" -or $Layer -eq "translation") { "hotd_unit" } else { "hotd"; "hotd_unit" })
    & cmake --build $Build --config $Config --parallel --target @targets
    if ($LASTEXITCODE -ne 0) { "BUILD FAILED"; exit 1 }
}
$junit = Join-Path $Build "ctest_results.xml"
$ctestArgs = @("--test-dir", $Build, "-C", $Config, "-j", "$Jobs", "--output-on-failure", "--output-junit", $junit)
if ($Layer -ne "all") { $ctestArgs += @("-L", "^$Layer$") } else { $ctestArgs += @("-LE", "^translation$") }   # (all: unit + module)
if ($Only) { $ctestArgs += @("-R", $Only) }
& ctest @ctestArgs
$code = $LASTEXITCODE

# the summary: each test's layer, result and time; the unit checks counted by hotd_unit itself
$rows = @()
if (Test-Path $junit) {
    [xml]$x = Get-Content $junit
    foreach ($tc in $x.testsuite.testcase) {
        $st = if ($tc.status -eq "run") { "passed" } elseif ($tc.status -eq "notrun" -or $tc.skipped) { "skipped" } else { "FAILED" }
        if ($tc.failure) { $st = "FAILED" }
        $layer = ($tc.name -split '\.')[0]
        $rows += [pscustomobject]@{ Test = $tc.name; Layer = $layer; Result = $st; Seconds = [math]::Round([double]$tc.time, 1) }
    }
}
""
$rows | Sort-Object Layer, Test | Format-Table -AutoSize | Out-String -Width 200
foreach ($l in ($rows | Group-Object Layer | Sort-Object Name)) {
    $p = @($l.Group | Where-Object Result -eq "passed").Count
    $f = @($l.Group | Where-Object Result -eq "FAILED").Count
    $s = @($l.Group | Where-Object Result -eq "skipped").Count
    "{0,-12} {1,3} passed, {2,3} failed, {3,3} skipped" -f $l.Name, $p, $f, $s
}
$unitExe = Join-Path $Build "$Config\hotd_unit.exe"
if (-not (Test-Path $unitExe)) { $unitExe = Join-Path $Build "hotd_unit" }
if ((Test-Path $unitExe) -and ($Layer -eq "all" -or $Layer -eq "unit")) {
    $line = & $unitExe util launcher lang mods files vr net rollback typing weapons survival evdev | Select-Object -Last 1
    "unit checks: $line"
}
"total: {0:N0} s" -f ((Get-Date) - $t0).TotalSeconds
exit $code
