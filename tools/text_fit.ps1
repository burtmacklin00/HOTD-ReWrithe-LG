# text_fit.ps1: "does every text fit" over all languages and two window sizes (docs/testing.md). Per language
# (data\lang\*.txt) and size (640x480, 1920x1080 by default):
#   - the menus: --lang-check <code> with HOTD_LANG_SIZE=WxH (every menu screen of both menu styles, Classic and Remake;
#     OVERFLOW = wider than its box or the screen, BROKEN = cut through a letter)
#   - the in-mode HUD: --render --text-fit runs of Survival and Typing of the Dead 0 (TEXTFIT = a HUD text
#     drawn past the screen's / the 2D canvas's edge; TEXTFIT-PICTURE = a ready picture such as a TotD word box: listed,
#     not counted)
# A problem English has too at the same size (the PC Mode table's sample names: the PC's own font) is listed as
# "also in English" and not counted for the other languages. Headless and muted, each run with its own HOTD_PREF_DIR,
# a timeout per run (killed by its own PID). Writes <Out>\summary.md (a table language x size: the new problems per
# mode, the menus' untranslated keys; then every problem line) and <Out>\<run>.log; exit code = the number of runs
# with a new problem (or that failed / timed out). -ReportOnly: the summary again from the logs in <Out>.
#
#   powershell -ExecutionPolicy Bypass -File tools\text_fit.ps1 -Root "<main checkout>" [-Langs en,de,ko] [-Sizes 640x480,1920x1080]
#       [-Modes menus,survival,totd] [-Frames 3000] [-Jobs 4] [-Timeout 600] [-Out build\textfit] [-ReportOnly]
param(
    [string]$Root = "",
    [string]$Exe = "",
    [string]$Out = "",
    [string[]]$Langs = @(),
    [string[]]$Sizes = @("640x480", "1920x1080"),
    [string[]]$Modes = @("menus", "survival", "totd"),
    [int]$Frames = 3000,
    [int]$Jobs = 4,
    [int]$Timeout = 600,
    [string]$PcDir = "C:\Games\HOTD",
    [switch]$ReportOnly
)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $here "build\Release\hotd.exe" }
if (-not $Out) { $Out = Join-Path $here "build\textfit" }
if (-not $Root) { $Root = $here }
if ($Langs.Count -eq 1 -and $Langs[0] -match ",") { $Langs = $Langs[0].Split(",") }
if ($Sizes.Count -eq 1 -and $Sizes[0] -match ",") { $Sizes = $Sizes[0].Split(",") }
if ($Modes.Count -eq 1 -and $Modes[0] -match ",") { $Modes = $Modes[0].Split(",") }
if ($Langs.Count -eq 0) { $Langs = @(Get-ChildItem (Join-Path $here "data\lang") -Filter *.txt | ForEach-Object { $_.BaseName }) }
New-Item -ItemType Directory -Force $Out | Out-Null
$files = "--arcade `"$(Join-Path $Root 'private\hotd.zip')`" --pc `"$PcDir`""
$modeArgs = @{
    "survival" = "--survival courtyard --autofire --lives-kept 1"
    "totd"     = "--totd-test"
}
$runs = New-Object System.Collections.ArrayList
foreach ($l in $Langs) {
    foreach ($s in $Sizes) {
        foreach ($m in $Modes) {
            $name = "$l-$s-$m"
            if ($m -eq "menus") { $a = "--pc `"$PcDir`" --lang-check $l" }
            else { $a = "$files --render --mute --size $s --lang $l --text-fit --frames $Frames $($modeArgs[$m])" }
            [void]$runs.Add([pscustomobject]@{ Name = $name; Lang = $l; Size = $s; Mode = $m; Args = $a })
        }
    }
}
$results = @{}
# a run's log: its problems, pictures, untranslated keys, status
function Analyze($r, [string[]]$text, [bool]$timedOut, $exitCode, [int]$secs) {
    $problems = @($text | Where-Object { $_ -match '^(OVERFLOW|BROKEN|TEXTFIT) ' })
    $pics = @($text | Where-Object { $_ -match '^TEXTFIT-PICTURE ' })
    $missing = ""
    $done = $false
    if ($r.Mode -eq "menus") {
        $lc = $text | Where-Object { $_ -match '^lang-check .*, (\d+) keys not translated' } | Select-Object -First 1
        if ($lc -match ', (\d+) keys not translated') { $missing = $Matches[1]; $done = $true }
    } else { $done = [bool]($text | Where-Object { $_ -match '^text-fit: ' }) }
    $status = if ($timedOut) { "TIMEOUT" } elseif (-not $done) { if ($null -ne $exitCode -and $exitCode -ne 0) { "exit $exitCode" } else { "no report" } } else { "ok" }
    $results[$r.Name] = [pscustomobject]@{ R = $r; Status = $status; Problems = $problems; Pics = $pics.Count; Missing = $missing; Secs = $secs; New = @() }
}
function Key([string]$line) { return ($line -replace '\s+-?\d+ px', '') -replace '\s+', ' ' }   # (the overrun's size left out)

if ($ReportOnly) {
    foreach ($r in $runs) {
        $log = Join-Path $Out "$($r.Name).log"
        if (Test-Path -LiteralPath $log) { Analyze $r @(Get-Content -Encoding UTF8 -LiteralPath $log) $false $null 0 }
        else { $results[$r.Name] = [pscustomobject]@{ R = $r; Status = "not run"; Problems = @(); Pics = 0; Missing = ""; Secs = 0; New = @() } }
    }
} else {
    $queue = [System.Collections.Queue]::new($runs)
    $live = @()
    function Start-Run($r) {
        $pref = Join-Path $Out "pref\$($r.Name)"
        New-Item -ItemType Directory -Force $pref | Out-Null
        $env:HOTD_PREF_DIR = $pref
        $env:HOTD_LANG_SIZE = $r.Size
        $log = Join-Path $Out "$($r.Name).log"
        $p = Start-Process -FilePath $Exe -ArgumentList $r.Args -NoNewWindow -PassThru -RedirectStandardOutput "$log.out" -RedirectStandardError "$log.err"
        $null = $p.Handle   # (keeps the exit code readable after HasExited)
        return [pscustomobject]@{ R = $r; P = $p; T0 = Get-Date; Log = $log }
    }
    while ($queue.Count -gt 0 -or $live.Count -gt 0) {
        while ($live.Count -lt $Jobs -and $queue.Count -gt 0) { $live += Start-Run $queue.Dequeue() }
        Start-Sleep -Milliseconds 500
        $still = @()
        foreach ($x in $live) {
            $timedOut = ((Get-Date) - $x.T0).TotalSeconds -gt $Timeout
            if (-not $x.P.HasExited -and -not $timedOut) { $still += $x; continue }
            if (-not $x.P.HasExited) { try { $x.P.Kill() } catch {}; try { $x.P.WaitForExit(10000) | Out-Null } catch {} }
            $x.P.WaitForExit()
            $text = @()
            if (Test-Path "$($x.Log).out") { $text += Get-Content -Encoding UTF8 "$($x.Log).out" }
            if (Test-Path "$($x.Log).err") { $text += Get-Content -Encoding UTF8 "$($x.Log).err" }
            Set-Content -Encoding UTF8 -LiteralPath $x.Log -Value (@("hotd.exe $($x.R.Args)", "HOTD_LANG_SIZE=$($x.R.Size)") + $text)
            Remove-Item -LiteralPath "$($x.Log).out", "$($x.Log).err" -ErrorAction SilentlyContinue
            Analyze $x.R $text $timedOut $x.P.ExitCode ([int]((Get-Date) - $x.T0).TotalSeconds)
            $res = $results[$x.R.Name]
            Write-Output ("{0,-34} {1,-9} {2,3} problems {3,4} s" -f $x.R.Name, $res.Status, $res.Problems.Count, $res.Secs)
        }
        $live = $still
    }
}
# the English baseline: a problem English has at the same size / mode is not new
foreach ($res in $results.Values) {
    $en = $results["en-$($res.R.Size)-$($res.R.Mode)"]
    $known = @{}
    if ($en -and $res.R.Lang -ne "en") { foreach ($p in $en.Problems) { $known[(Key $p)] = 1 } }
    $res.New = @($res.Problems | Where-Object { -not $known.ContainsKey((Key $_)) })
}
$md = @("# Text fit ($(Get-Date -Format 'yyyy-MM-dd HH:mm'))", "",
        "New problems per run (English's own at the same size not counted for the other languages); menus: + keys not translated.", "",
        ("| Language | Size | " + ($Modes -join " | ") + " |"), ("|---|---|" + ("---|" * $Modes.Count)))
$bad = 0
foreach ($l in $Langs) {
    foreach ($s in $Sizes) {
        $cells = @()
        foreach ($m in $Modes) {
            $r = $results["$l-$s-$m"]
            if ($r.Status -ne "ok" -or $r.New.Count -gt 0) { $bad++ }
            $c = if ($r.Status -ne "ok") { "**$($r.Status)**" } elseif ($r.New.Count) { "**$($r.New.Count)**" } else { "0" }
            if ($r.Problems.Count -gt $r.New.Count) { $c += " ($($r.Problems.Count - $r.New.Count) as en)" }
            if ($m -eq "menus" -and $r.Missing -ne "") { $c += ", untranslated $($r.Missing)" }
            $cells += $c
        }
        $md += "| $l | $s | " + ($cells -join " | ") + " |"
    }
}
$md += @("", "## New problems", "")
foreach ($r in ($results.Values | Sort-Object { $_.R.Name })) {
    foreach ($p in $r.New) { $md += "- ``$($r.R.Name)``: ``$p``" }
}
$md += @("", "## Also in English (not counted for the other languages)", "")
foreach ($s in $Sizes) { foreach ($m in $Modes) { $en = $results["en-$s-$m"]; if ($en) { foreach ($p in $en.Problems) { $md += "- ``en-$s-$m``: ``$p``" } } } }
Set-Content -LiteralPath (Join-Path $Out "summary.md") -Value $md -Encoding utf8
Write-Output "text-fit: $($runs.Count) runs, $bad with a new problem ($(Join-Path $Out 'summary.md'))"
exit $bad
