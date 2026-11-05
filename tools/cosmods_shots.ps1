# cosmods_shots.ps1 - the cosmetic mods' headless check (docs/modding.md: Rainbow Blood, Big Head): Chapter 1 with
# --autofire, rendered offscreen (no window), muted, a few screenshots per model source with no mods, with Rainbow
# Blood, with Big Head (3x, Everyone) and with both. Look at the pictures side by side: blood bursts / drips in other
# colours (f2800, f4300), big heads on the zombie and the researcher (f800), on Rogan (f600), the Hangedman (f3200)
# and the Chariot (f14600). Output under private\ by default.
#   .\tools\cosmods_shots.ps1 [-Build build] [-Out <folder>] [-Sources arcade,pc] [-Frames 600,800,2800,3200,4300]
param(
    [string]$Build = "build",
    [string]$Out = "",
    [string[]]$Sources = @("arcade", "pc"),
    [int[]]$Frames = @(600, 800, 2800, 3200, 4300, 14600)
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$priv = Join-Path $repo "private"
if (-not (Test-Path -LiteralPath $priv)) {
    # a git worktree: private\ is in the main checkout
    $common = (& git -C $repo rev-parse --git-common-dir)
    if (-not [IO.Path]::IsPathRooted($common)) { $common = Join-Path $repo $common }
    $priv = Join-Path (Split-Path (Resolve-Path -LiteralPath $common).Path) "private"
}
if ($Out -eq "") { $Out = Join-Path $priv ("cosmods\" + (Get-Date -Format "yyyyMMdd_HHmm")) }
if (-not [IO.Path]::IsPathRooted($Build)) { $Build = Join-Path $repo $Build }
$exe = Join-Path $Build "Release\hotd.exe"
$arcade = Join-Path $priv "hotd.zip"
$pc = if ($env:HOTD_PC_DIR) { $env:HOTD_PC_DIR } else { "C:\Games\HOTD" }
$pref = Join-Path $Out "pref"
New-Item -ItemType Directory -Force $pref | Out-Null
$env:HOTD_PREF_DIR = $pref   # (never the player's settings)
$last = ($Frames | Measure-Object -Maximum).Maximum + 20
$shots = @(); foreach ($f in $Frames) { $shots += @("--shot-at", "$f") }
$sets = [ordered]@{
    "none"    = @("--no-mods")
    "rainbow" = @("--mod", "rainbow-blood")
    "bighead" = @("--mod", "big-head", "--mod-set", "big-head.size=3x", "--mod-set", "big-head.who=Everyone")
    "both"    = @("--mod", "rainbow-blood", "--mod", "big-head")
}
foreach ($src in $Sources) {
    foreach ($name in $sets.Keys) {
        $dir = Join-Path $Out "$src`_$name"
        New-Item -ItemType Directory -Force $dir | Out-Null
        $cmd = @("--arcade", $arcade, "--pc", $pc, "--models", $src, "--frames", "$last", "--autofire", "--mute",
                  "--lives-kept", "1", "--render") + $shots + @("--shot-dir", $dir) + $sets[$name]
        & $exe @cmd 2>&1 | Select-String -Pattern "\[mods\]|screenshot" | ForEach-Object { $_.Line }
    }
}
Write-Host "pictures: $Out"
