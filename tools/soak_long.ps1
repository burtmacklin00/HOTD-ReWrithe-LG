# soak_long.ps1: the long soak (docs/testing.md): each mode played for a long time headless, muted, both models
# (arcade / pc): Survival to round 16 (--survival-test), Typing of the Dead 0 chapters 1-4 (Normal),
# the arcade game Chapter 1 to the ending in one run. Each run has its own HOTD_PREF_DIR and a wall-clock timeout
# (killed by its own PID, never by name); HOTD_PROGRESS_EVERY / HOTD_STUCK_FRAMES make the game print PROGRESS / STUCK
# lines; this script samples the process's working set every -Sample seconds.
# A run fails on: a non-zero exit (a crash: 0xC0000005 ...), the timeout, bad_access=1 / "access outside the mapped
# memory", STUCK (nothing changed for -Stuck frames: the mode, the event script's place, the score, Survival's round /
# kills), memory growth (the working set's last quarter more than -GrowMb above its second quarter), or
# its expected line missing. Writes <Out>\summary.md, <Out>\<run>.log and <Out>\<run>.mem.csv; exit code = failed runs.
#
#   powershell -ExecutionPolicy Bypass -File tools\soak_long.ps1 -Root "<main checkout>" [-Only <regex>] [-Jobs 4]
#       [-Models arcade,pc] [-Rounds 16] [-Timeout 3600] [-Stuck 7200] [-GrowMb 150] [-Render] [-Out build\soak_long]
param(
    [string]$Root = "",
    [string]$Exe = "",
    [string]$Out = "",
    [string]$Only = "",
    [int]$Jobs = 4,
    [string[]]$Models = @("arcade", "pc"),
    [int]$Timeout = 3600,
    [int]$Stuck = 7200,
    [int]$GrowMb = 150,
    [int]$Sample = 5,
    [int]$Rounds = 16,   # Survival's rounds
    [switch]$Render,
    [string]$PcDir = "C:\Games\HOTD"
)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $here "build\Release\hotd.exe" }
if (-not $Out) { $Out = Join-Path $here "build\soak_long" }
if (-not $Root) { $Root = $here }
if ($Models.Count -eq 1 -and $Models[0] -match ",") { $Models = $Models[0].Split(",") }
New-Item -ItemType Directory -Force $Out | Out-Null
$files = "--arcade `"$(Join-Path $Root 'private\hotd.zip')`" --pc `"$PcDir`" --mute"
if ($Render) { $files += " --render --size 640x360" }
$cases = New-Object System.Collections.ArrayList
function Add-Case([string]$name, [string]$argLine, [string]$expect, [string]$aimSkip = "") {
    if ($Only -and $name -notmatch $Only) { return }
    [void]$cases.Add([pscustomobject]@{ Name = $name; Args = "$files $argLine"; Expect = $expect; AimSkip = $aimSkip })
}
foreach ($m in $Models) {
    Add-Case "survival-r$Rounds-$m" "--survival-test $Rounds --frames $(40000 * $Rounds) --models $m" "SURVIVAL TEST: result=ok"
    foreach ($ch in 1, 2, 3, 4) { Add-Case "totd-c$ch-$m" "--totd-test --chapter $ch --difficulty 2 --frames 90000 --models $m" "TOTD TEST: PASS" }
    Add-Case "arcade-all-$m" "--strict --default-route --autofire --lives-kept 1 --frames 160000 --models $m" "the ending is over"
}
$queue = [System.Collections.Queue]::new($cases)
$live = @()
$rows = @()
$failed = 0
function Start-Run($c) {
    $pref = Join-Path $Out "pref\$($c.Name)"
    New-Item -ItemType Directory -Force $pref | Out-Null
    $env:HOTD_PREF_DIR = $pref
    $env:HOTD_PROGRESS_EVERY = "3600"
    $env:HOTD_STUCK_FRAMES = "$Stuck"
    $env:HOTD_AIM_SKIP = $c.AimSkip   # ("" removes it)
    $log = Join-Path $Out "$($c.Name).log"
    $p = Start-Process -FilePath $Exe -ArgumentList $c.Args -NoNewWindow -PassThru -RedirectStandardOutput "$log.out" -RedirectStandardError "$log.err"
    $null = $p.Handle   # (keeps the exit code readable after HasExited)
    $mem = New-Object System.Collections.ArrayList
    return [pscustomobject]@{ C = $c; P = $p; T0 = Get-Date; Log = $log; Mem = $mem; Next = (Get-Date) }
}
while ($queue.Count -gt 0 -or $live.Count -gt 0) {
    while ($live.Count -lt $Jobs -and $queue.Count -gt 0) { $live += Start-Run $queue.Dequeue() }
    Start-Sleep -Milliseconds 500
    $still = @()
    foreach ($x in $live) {
        $secs = ((Get-Date) - $x.T0).TotalSeconds
        if (-not $x.P.HasExited -and (Get-Date) -ge $x.Next) {
            try { $x.P.Refresh(); [void]$x.Mem.Add([pscustomobject]@{ S = [int]$secs; Mb = [math]::Round($x.P.WorkingSet64 / 1MB, 1) }) } catch {}
            $x.Next = (Get-Date).AddSeconds($Sample)
        }
        $timedOut = $secs -gt $Timeout
        if (-not $x.P.HasExited -and -not $timedOut) { $still += $x; continue }
        if (-not $x.P.HasExited) { try { $x.P.Kill() } catch {}; try { $x.P.WaitForExit(10000) | Out-Null } catch {} }
        $x.P.WaitForExit()
        $text = @()
        if (Test-Path "$($x.Log).out") { $text += Get-Content "$($x.Log).out" }
        if (Test-Path "$($x.Log).err") { $text += Get-Content "$($x.Log).err" }
        Set-Content -LiteralPath $x.Log -Value (@("hotd.exe $($x.C.Args)") + $text)
        Remove-Item -LiteralPath "$($x.Log).out", "$($x.Log).err" -ErrorAction SilentlyContinue
        $x.Mem | ForEach-Object { "$($_.S),$($_.Mb)" } | Set-Content -LiteralPath "$($x.Log -replace '\.log$', '.mem.csv')"
        $why = @()
        if ($timedOut) { $why += "TIMEOUT ($Timeout s)" }
        elseif ($x.P.ExitCode -ne 0) { $why += ("exit 0x{0:X8}" -f $x.P.ExitCode) }
        if ($text | Where-Object { $_ -match 'bad_access=1|access outside the mapped memory' }) { $why += "bad_access" }
        $st = @($text | Where-Object { $_ -match '^STUCK:' })
        if ($st.Count) { $why += $st[0] }
        if ($x.C.Expect -and -not ($text | Where-Object { $_ -match [regex]::Escape($x.C.Expect) })) { $why += "missing '$($x.C.Expect)'" }
        # memory: the working set's 2nd quarter (after the load) against its last quarter; the game's own PROGRESS
        # ws_mb (every 3600 frames) when it has more points than the samples (a fast headless run)
        $prog = New-Object System.Collections.ArrayList
        foreach ($t in $text) { if ($t -match '^PROGRESS frame (\d+) .* ws_mb=(\d+)') { [void]$prog.Add([pscustomobject]@{ S = [int]$Matches[1]; Mb = [double]$Matches[2] }) } }
        if ($prog.Count -gt $x.Mem.Count) { $x.Mem = $prog }
        $n = $x.Mem.Count
        $grow = 0; $peak = 0
        if ($n -ge 8) {
            $q2 = ($x.Mem[[int]($n / 4)..([int]($n / 2) - 1)] | Measure-Object Mb -Average).Average
            $q4 = ($x.Mem[[int]($n * 3 / 4)..($n - 1)] | Measure-Object Mb -Average).Average
            $grow = [math]::Round($q4 - $q2, 1)
            if ($grow -gt $GrowMb) { $why += "memory +$grow MB" }
        }
        if ($n) { $peak = ($x.Mem | Measure-Object Mb -Maximum).Maximum }
        $frames = ($text | Where-Object { $_ -match '^PROGRESS frame (\d+)' } | Select-Object -Last 1)
        $lastF = if ($frames -match '^PROGRESS frame (\d+)') { $Matches[1] } else { "?" }
        $res = if ($why.Count) { "**FAIL** " + ($why -join "; ") } else { "ok" }
        if ($why.Count) { $failed++ }
        $rows += "| $($x.C.Name) | $res | $([int]$secs) | $lastF | $peak | $grow |"
        Write-Output ("{0,-22} {1} ({2} s, peak {3} MB, growth {4} MB)" -f $x.C.Name, $res, [int]$secs, $peak, $grow)
    }
    $live = $still
}
$md = @("# Long soak ($(Get-Date -Format 'yyyy-MM-dd HH:mm'))", "", "| Run | Result | s | last PROGRESS frame | peak MB | growth MB |", "|---|---|---|---|---|---|") + $rows
Set-Content -LiteralPath (Join-Path $Out "summary.md") -Value $md -Encoding utf8
Write-Output "soak_long: $($cases.Count) runs, $failed failed ($(Join-Path $Out 'summary.md'))"
exit $failed
