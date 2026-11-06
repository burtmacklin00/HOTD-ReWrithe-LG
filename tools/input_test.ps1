# input_test.ps1: runs hotd.exe --input-test with a wall-clock timeout and always ends its results file with a
# summary line (docs/testing.md). The test itself writes "N checks, M failed" at its end, a "progress" line every
# 30 s, and stops itself past HOTD_INPUT_TEST_LIMIT seconds; this wrapper covers what the game cannot: a crash (a
# non-zero exit) and a run killed from outside (past -Timeout: killed by its own PID, never by name). Then it adds
#   FAIL input-test: <why> (last line: ...)
#   N checks, M failed
# so the file's last line is always the summary. Exit code: 0 = all checks passed, 1 = a failed check / no clean end.
#
#   powershell -ExecutionPolicy Bypass -File tools\input_test.ps1 -Root "<main checkout>" -Results <file>
#       [-Exe build\Release\hotd.exe] [-Pref <settings folder>] [-Timeout 2400] [-Limit 1800]
param(
    [string]$Root = "",
    [Parameter(Mandatory = $true)][string]$Results,
    [string]$Exe = "",
    [string]$Pref = "",
    [int]$Timeout = 2400,
    [int]$Limit = 1800
)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $here "build\Release\hotd.exe" }
if (-not $Root) { $Root = $here }
if (-not $Pref) { $Pref = Join-Path (Split-Path -Parent $Results) "pref_input" }
New-Item -ItemType Directory -Force $Pref | Out-Null
$env:HOTD_PREF_DIR = $Pref
$env:HOTD_INPUT_TEST_LIMIT = "$Limit"
$arcade = Join-Path $Root "private\hotd.zip"
$pc = Join-Path $Root "private\pcharness\game"
$log = "$Results.log"
if (Test-Path -LiteralPath $Results) { Remove-Item -LiteralPath $Results }
$argLine = "--arcade `"$arcade`" --pc `"$pc`" --input-test `"$Results`""
$t0 = Get-Date
$p = Start-Process -FilePath $Exe -ArgumentList $argLine -NoNewWindow -PassThru -RedirectStandardOutput "$log.out" -RedirectStandardError "$log.err"
$null = $p.Handle   # (keeps the exit code readable after HasExited)
$timedOut = -not $p.WaitForExit($Timeout * 1000)
if ($timedOut) {
    try { $p.Kill() } catch {}
    try { $p.WaitForExit(10000) | Out-Null } catch {}
}
$secs = [int]((Get-Date) - $t0).TotalSeconds
$lines = @()
if (Test-Path -LiteralPath $Results) { $lines = @(Get-Content -LiteralPath $Results) }
$last = if ($lines.Count) { $lines[-1] } else { "" }
$summary = $last -match '^(\d+) checks, (\d+) failed$'
if (-not $summary) {
    $why = if ($timedOut) { "killed after $secs s (-Timeout $Timeout)" }
           elseif ($p.ExitCode -ne 0) { "the game exited with 0x{0:X8} after $secs s (a crash?)" -f $p.ExitCode }
           else { "the game ended after $secs s without the summary" }
    $checks = @($lines | Where-Object { $_ -match '^(ok  |FAIL) frame' }).Count
    $fails = @($lines | Where-Object { $_ -match '^FAIL' }).Count + 1
    Add-Content -LiteralPath $Results "FAIL input-test: $why (last line: $last)"
    Add-Content -LiteralPath $Results "$($checks + 1) checks, $fails failed"
    Write-Output "input-test: NO CLEAN END: $why; $($checks + 1) checks, $fails failed ($Results)"
    exit 1
}
Write-Output "input-test: $last in $secs s ($Results)"
if ([int]$Matches[2] -ne 0) { exit 1 }
exit 0
