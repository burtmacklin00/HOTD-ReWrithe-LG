# export_public.ps1 - makes the public copy of the repository: the committed tree of HEAD without the private working
# journal and the developer-only files, as a NEW git repository with one commit (no history: the history holds the
# journal). Nothing is pushed; publishing is a separate, manual step.
#
#   powershell -ExecutionPolicy Bypass -File tools\export_public.ps1 -Out <empty folder outside the repo> [-Rev HEAD]
#
# Left out (the working journal and what needs private\ data): TODO-NEXT-SESSION.md, docs/history.md,
# docs/session-history.md, "Run Viewer.bat" (the viewer needs MAME dumps from private\); the unreleased modes (user
# 2026-11-05: not in the public source at all): the folders src/unreleased and docs/unreleased, every C / C++ block
# "#if HOTD_UNRELEASED" .. "#endif" (an "#else" part stays) and every "# [unreleased]" .. "# [/unreleased]" section
# (language files, tool scripts): the copy builds without them (CMake: HOTD_UNRELEASED OFF). The script then checks the
# copy: no private\ path, no ROM / EXE / archive / image / audio files, no file over 1 MB, and lists links into the
# left-out files.
param([Parameter(Mandatory)] [string]$Out, [string]$Rev = "HEAD")
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$outFull = [System.IO.Path]::GetFullPath($Out)
if ($outFull.StartsWith($repo, [System.StringComparison]::OrdinalIgnoreCase)) { throw "-Out must be outside the repository" }
if ((Test-Path -LiteralPath $outFull) -and (Get-ChildItem -LiteralPath $outFull -Force | Select-Object -First 1)) {
    throw "-Out must be empty or not exist: $outFull"
}
New-Item -ItemType Directory -Force $outFull | Out-Null

$exclude = @("TODO-NEXT-SESSION.md", "docs/history.md", "docs/session-history.md", "Run Viewer.bat")
$excludeDirs = @("src/unreleased", "docs/unreleased")   # (the unreleased modes: not in the public repo, the user 2026-11-03 / 11-05)
$sha = (git -C $repo rev-parse --short $Rev).Trim()

# 1. the committed tree (git archive: only tracked files, never private\ or build folders)
$tar = Join-Path $env:TEMP "hotd_public_$sha.tar"
git -C $repo archive --format=tar -o $tar $Rev
if ($LASTEXITCODE -ne 0) { throw "git archive failed" }
& (Join-Path $env:SystemRoot "System32\tar.exe") -xf $tar -C $outFull   # Windows tar (a Git-Bash tar reads "C:" as a host)
if ($LASTEXITCODE -ne 0) { throw "tar failed" }
Remove-Item -LiteralPath $tar
foreach ($f in $exclude) {
    $p = Join-Path $outFull $f
    if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
}
foreach ($d in $excludeDirs) {
    $p = Join-Path $outFull $d
    if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Recurse -Force }
}

# 1b. the unreleased modes' blocks and sections (UTF-8 kept as it was: a BOM only where there was one; CRLF / LF kept)
function Remove-Unreleased([string[]]$lines, [bool]$cpp) {
    $res = New-Object System.Collections.Generic.List[string]
    $stack = New-Object System.Collections.Generic.List[string]   # per #if: "U" dropped, "E" its #else (kept), "O" other
    $section = $false
    foreach ($l in $lines) {
        if ($cpp) {
            if ($l -match '^\s*#\s*if\s+HOTD_UNRELEASED\b') { $stack.Add("U"); continue }
            if ($l -match '^\s*#\s*if') { $stack.Add("O") }
            elseif ($l -match '^\s*#\s*else\b' -and $stack.Count -and $stack[$stack.Count - 1] -eq "U") { $stack[$stack.Count - 1] = "E"; continue }
            elseif ($l -match '^\s*#\s*endif\b' -and $stack.Count) {
                $k = $stack[$stack.Count - 1]; $stack.RemoveAt($stack.Count - 1)
                if ($k -ne "O") { continue }
            }
            if ($stack.Contains("U")) { continue }
        } else {
            if ($l -match '^\s*#\s*\[unreleased\]') { $section = $true; continue }
            if ($l -match '^\s*#\s*\[/unreleased\]') { $section = $false; continue }
            if ($section) { continue }
        }
        $res.Add($l)
    }
    return ,$res
}
$utf8 = New-Object System.Text.UTF8Encoding($false)
foreach ($f in Get-ChildItem -LiteralPath $outFull -Recurse -File) {
    $cpp = $f.Extension -in @(".cpp", ".h", ".c", ".hpp", ".inc", ".mm")
    if (-not $cpp -and $f.Extension -notin @(".txt", ".ps1", ".sh", ".py", ".ini", ".cmake")) { continue }
    $bytes = [IO.File]::ReadAllBytes($f.FullName)
    $bom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    $text = $utf8.GetString($bytes, $(if ($bom) { 3 } else { 0 }), $bytes.Length - $(if ($bom) { 3 } else { 0 }))
    if ($cpp) { if ($text -notmatch 'HOTD_UNRELEASED') { continue } } elseif ($text -notmatch '\[unreleased\]') { continue }
    $nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $kept = Remove-Unreleased ($text -split "`r?`n") $cpp
    $new = [string]::Join($nl, $kept)
    $enc = New-Object System.Text.UTF8Encoding($bom)
    [IO.File]::WriteAllText($f.FullName, $new, $enc)
}

# 2. checks
$problems = @()
$files = Get-ChildItem -LiteralPath $outFull -Recurse -File
$badExt = '\.(zip|7z|rar|exe|dll|bin|rom|rbh|vmc|ogg|wav|png|jpg|bmp|iso|chd)$'
# relprep: our own / third-party shipped files with their licences in NOTICE.md (the prompt icons CC0, the wordmark ours,
# the Noto fonts OFL: the CJK fonts are ~4.7 MB each, SC 8.5 MB; TotD 0's bell CC0); everything else of these types is still a problem
$allowed = '^data[\\/](typing[\\/]ding\.wav|ui[\\/]prompts[\\/](xelu|kenney)[\\/].*\.png|ui[\\/]rewrithe_wordmark\.png|fonts[\\/]NotoSans[A-Za-z]*-Bold\.(otf|ttf))$'
foreach ($f in $files) {
    $rel = $f.FullName.Substring($outFull.Length + 1)
    if ($rel -match '^private[\\/]') { $problems += "private file: $rel" }
    if ($rel -match $allowed) { continue }
    if ($f.Name -match $badExt) { $problems += "binary / game-data type: $rel" }
    if ($f.Length -gt 1MB) { $problems += ("over 1 MB ({0:N1} MB): $rel" -f ($f.Length / 1MB)) }
    if ($f.Extension -in @(".cpp", ".h", ".c", ".hpp", ".inc", ".mm", ".txt", ".ps1", ".sh", ".py", ".ini") -and
        (Select-String -LiteralPath $f.FullName -Pattern '^\s*#\s*if\s+HOTD_UNRELEASED|^\s*#\s*\[/?unreleased\]' -Quiet)) {
        $problems += "an unreleased block / section left: $rel"
    }
}
$links = @()
foreach ($f in $files | Where-Object { $_.Extension -eq ".md" }) {
    $text = Get-Content -LiteralPath $f.FullName -Raw
    foreach ($x in $exclude) {
        $leaf = Split-Path $x -Leaf
        if ($text -match [regex]::Escape($leaf)) { $links += "$($f.FullName.Substring($outFull.Length + 1)) mentions $leaf" }
    }
}

# 3. a new repository with one commit
git -C $outFull init -q -b main
git -C $outFull add -A
git -C $outFull commit -q -m "Public snapshot of $sha"
if ($LASTEXITCODE -ne 0) { throw "commit failed" }

"Public copy: $outFull ({0} files) from $sha" -f $files.Count
if ($problems) { "PROBLEMS:"; $problems | ForEach-Object { "  $_" } } else { "checks: no private / binary / large files" }
if ($links) { "mentions of left-out files (fix before publishing):"; $links | ForEach-Object { "  $_" } }
