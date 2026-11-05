# export_public.ps1 - makes the public copy of the repository: the committed tree of HEAD without the private working
# journal and the developer-only files, as a NEW git repository with one commit (no history: the history holds the
# journal). Nothing is pushed; publishing is a separate, manual step.
#
#   powershell -ExecutionPolicy Bypass -File tools\export_public.ps1 -Out <empty folder outside the repo> [-Rev HEAD]
#
# Left out (the working journal and what needs private\ data): TODO-NEXT-SESSION.md, docs/history.md,
# docs/session-history.md, "Run Viewer.bat" (the viewer needs MAME dumps from private\). The script then checks the
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

$exclude = @("TODO-NEXT-SESSION.md", "docs/history.md", "docs/session-history.md", "Run Viewer.bat",
              "docs/typing.md", "docs/horde.md")   # (the unreleased modes: not shown in the public repo, the user 2026-11-03)
$excludeDirs = @("data/typing", "data/horde")
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

# 2. checks
$problems = @()
$files = Get-ChildItem -LiteralPath $outFull -Recurse -File
$badExt = '\.(zip|7z|rar|exe|dll|bin|rom|rbh|vmc|ogg|wav|png|jpg|bmp|iso|chd)$'
# relprep: our own / third-party shipped files with their licences in NOTICE.md (the prompt icons CC0, the wordmark ours,
# the Noto fonts OFL: the CJK fonts are ~4.7 MB each, SC 8.5 MB); everything else of these types is still a problem
$allowed = '^data[\\/](ui[\\/]prompts[\\/](xelu|kenney)[\\/].*\.png|ui[\\/]rewrithe_wordmark\.png|fonts[\\/]NotoSans[A-Za-z]*-Bold\.(otf|ttf))$'
foreach ($f in $files) {
    $rel = $f.FullName.Substring($outFull.Length + 1)
    if ($rel -match '^private[\\/]') { $problems += "private file: $rel" }
    if ($rel -match $allowed) { continue }
    if ($f.Name -match $badExt) { $problems += "binary / game-data type: $rel" }
    if ($f.Length -gt 1MB) { $problems += ("over 1 MB ({0:N1} MB): $rel" -f ($f.Length / 1MB)) }
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
