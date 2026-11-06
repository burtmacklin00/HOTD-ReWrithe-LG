# hud_cover.ps1: "the HUD stays out of the action" (docs/testing.md). --render --hud-cover runs of the arcade game,
# Survival and Typing of the Dead 0 (and the unreleased modes in a development tree), arcade and PC models: every 30th frame in play the share of the play
# area's middle 70 % that the HUD / 2D layer changed; frames above -Pct listed, the worst frame of each run saved with
# the HUD's pixels in magenta (<Out>\<run>\hudcover_worst_f<F>.png). Headless, muted, own HOTD_PREF_DIR per run, a
# timeout per run (killed by its own PID). Writes <Out>\summary.md; exit code = the runs with a frame above -Pct.
#
#   powershell -ExecutionPolicy Bypass -File tools\hud_cover.ps1 -Root "<main checkout>" [-Pct 8] [-Frames 12000]
#       [-Size 1280x720] [-Models arcade,pc] [-Jobs 4] [-Out build\hudcover]
param(
    [string]$Root = "",
    [string]$Exe = "",
    [string]$Out = "",
    [double]$Pct = 8,
    [int]$Frames = 12000,
    [string]$Size = "1280x720",
    [string[]]$Models = @("arcade", "pc"),
    [int]$Jobs = 4,
    [int]$Timeout = 900,
    [string]$PcDir = "C:\Games\HOTD"
)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $here "build\Release\hotd.exe" }
if (-not $Out) { $Out = Join-Path $here "build\hudcover" }
if (-not $Root) { $Root = $here }
if ($Models.Count -eq 1 -and $Models[0] -match ",") { $Models = $Models[0].Split(",") }
New-Item -ItemType Directory -Force $Out | Out-Null
$modes = [ordered]@{
    "arcade"   = "--autofire --lives-kept 1"
    "survival" = "--survival courtyard --autofire --lives-kept 1"
    "totd"     = "--totd-test"
}
$runs = @()
foreach ($m in $Models) {
    foreach ($k in $modes.Keys) {
        $name = "$k-$m"
        $dir = Join-Path $Out $name
        $a = "--arcade `"$(Join-Path $Root 'private\hotd.zip')`" --pc `"$PcDir`" --render --mute --size $Size --frames $Frames --models $m --hud-cover $Pct --shot-dir `"$dir`" $($modes[$k])"
        $runs += [pscustomobject]@{ Name = $name; Args = $a }
    }
}
$queue = [System.Collections.Queue]::new($runs)
$live = @()
$rows = @()
$bad = 0
while ($queue.Count -gt 0 -or $live.Count -gt 0) {
    while ($live.Count -lt $Jobs -and $queue.Count -gt 0) {
        $r = $queue.Dequeue()
        $env:HOTD_PREF_DIR = Join-Path $Out "pref\$($r.Name)"
        New-Item -ItemType Directory -Force $env:HOTD_PREF_DIR | Out-Null
        $log = Join-Path $Out "$($r.Name).log"
        $p = Start-Process -FilePath $Exe -ArgumentList $r.Args -NoNewWindow -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
        $null = $p.Handle
        $live += [pscustomobject]@{ R = $r; P = $p; T0 = Get-Date; Log = $log }
    }
    Start-Sleep -Milliseconds 500
    $still = @()
    foreach ($x in $live) {
        $timedOut = ((Get-Date) - $x.T0).TotalSeconds -gt $Timeout
        if (-not $x.P.HasExited -and -not $timedOut) { $still += $x; continue }
        if (-not $x.P.HasExited) { try { $x.P.Kill() } catch {}; try { $x.P.WaitForExit(10000) | Out-Null } catch {} }
        $line = Get-Content -LiteralPath $x.Log | Where-Object { $_ -match '^hud-cover: ' } | Select-Object -Last 1
        if (-not $line) { $line = if ($timedOut) { "TIMEOUT" } else { "no report (exit $($x.P.ExitCode))" } }
        $above = if ($line -match ', (\d+) above ') { [int]$Matches[1] } else { -1 }
        if ($above -ne 0) { $bad++ }
        $pic = Get-ChildItem (Join-Path $Out $x.R.Name) -Filter "hudcover_worst_*.png" -ErrorAction SilentlyContinue | Select-Object -First 1
        $rows += "| $($x.R.Name) | $line | $(if ($pic) { "$($x.R.Name)\$($pic.Name)" }) |"
        Write-Output "$($x.R.Name): $line"
    }
    $live = $still
}
$md = @("# HUD cover ($(Get-Date -Format 'yyyy-MM-dd HH:mm'), threshold $Pct %, $Size)", "", "| Run | Result | Worst frame |", "|---|---|---|") + $rows
Set-Content -LiteralPath (Join-Path $Out "summary.md") -Value $md -Encoding utf8
Write-Output "hud-cover: $($runs.Count) runs, $bad with frames above $Pct % ($(Join-Path $Out 'summary.md'))"
exit $bad
