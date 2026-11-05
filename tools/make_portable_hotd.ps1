# Builds the game (`hotd`, the Chapter 1 teaser) in Release and copies the program into one self-contained folder.
# NO GAME DATA goes in: the player points the launcher at their own arcade ROM zip and PC install (the files check
# verifies them). The originals are only read.
#
#   powershell -ExecutionPolicy Bypass -File tools\make_portable_hotd.ps1 [-Build <build folder>] [-Out <folder>] [-NoBuild] [-Release]
#   -NoBuild: copy the exe the build folder already has (no cmake --build, nothing is rebuilt or locked).
#   -Release: a public release package (docs/porting.md "Experimental modes"): built with HOTD_EXPERIMENTAL_MODES=OFF (no
#     Typing of the Dead 0 or Horde; Survival Mode in, HOTD_SURVIVAL_MODE=ON) in its own build folder, build-release (configured the first time with
#     the dev build's generator and dependency folders), into private\portable\HOTD ReWrithe Release; the exe is checked
#     (--totd must be refused; Survival Mode stays in, labelled Very Experimental). Without -Release: the user's own copy from build\ (the modes ON, as build\Release).
#
# Default output: <repo>\private\portable\HOTD Teaser (inside private\, never committed).
# The game needs nothing else: the work RAM is built from the ROM at the start and the textures / colours come from
# the ROM regions (docs/teaser_status/s1.md); no MAME snapshot or dump is used (the old -WithDevData is gone).
param(
    [string]$Build = "",
    [string]$Out = "",
    [switch]$NoBuild,
    [switch]$NoZip,
    [switch]$Release
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$devBuild = Join-Path $repo "build"
if ($Build -eq "") { $Build = if ($Release) { Join-Path $repo "build-release" } else { $devBuild } }
$priv = Join-Path $repo "private"
if (-not (Test-Path -LiteralPath $priv)) {
    # a git worktree: private\ is in the main checkout
    $common = (& git -C $repo rev-parse --git-common-dir)
    if ($common) {
        if (-not [IO.Path]::IsPathRooted($common)) { $common = Join-Path $repo $common }
        $priv = Join-Path (Split-Path (Resolve-Path -LiteralPath $common).Path) "private"
    }
}

if ($Out -eq "") { $Out = if ($Release) { Join-Path $priv "portable\HOTD ReWrithe Release" } else { Join-Path $priv "portable\HOTD ReWrithe" } }

# 1. Release build
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = "C:\Program Files\CMake\bin\cmake.exe" }
if ($Release -and -not $NoBuild) {
    if ((Resolve-Path -LiteralPath $Build -ErrorAction SilentlyContinue).Path -eq (Resolve-Path -LiteralPath $devBuild -ErrorAction SilentlyContinue).Path) {
        throw "-Release builds in its own folder (build-release): not the dev build ($devBuild), which keeps the modes ON"
    }
    # the release package: HOTD_EXPERIMENTAL_MODES=OFF in its own build folder (the dev build keeps the modes)
    $cfgArgs = @('-S', $repo, '-B', $Build, '-DHOTD_EXPERIMENTAL_MODES=OFF', '-DHOTD_SURVIVAL_MODE=ON', '-DHOTD_LOCAL_ICON=OFF')
    if (-not (Test-Path -LiteralPath (Join-Path $Build "CMakeCache.txt"))) {
        $devCache = Get-Content -LiteralPath (Join-Path $devBuild "CMakeCache.txt") -ErrorAction SilentlyContinue
        $cfgArgs += @('-G', 'Visual Studio 17 2022', '-A', 'x64')
        foreach ($k in 'HOTD_FETCH_SDL3', 'FETCHCONTENT_FULLY_DISCONNECTED', 'FETCHCONTENT_SOURCE_DIR_SDL3', 'HOTD_ANGLE_DIR',
                       'HOTD_DR_LIBS_DIR', 'HOTD_MUSASHI_DIR', 'HOTD_SCSP_DIR', 'HOTD_STB_DIR', 'HOTD_ARCADE_SOUND', 'OpenXR_DIR') {
            $line = $devCache | Where-Object { $_ -like "${k}:*" } | Select-Object -First 1
            $v = if ($line) { $line -replace '^[^=]*=', '' } else { '' }
            if ($v -and $v -notlike '*NOTFOUND') { $cfgArgs += "-D$k=$v" }
        }
        # the dev build fetched SDL3 once into its _deps (its cache keeps no source path): a disconnected release
        # configure uses that copy
        $sdlSrc = Join-Path $devBuild "_deps\sdl3-src"
        if (-not ($cfgArgs -like '-DFETCHCONTENT_SOURCE_DIR_SDL3=*') -and (Test-Path -LiteralPath $sdlSrc)) {
            $cfgArgs += "-DFETCHCONTENT_SOURCE_DIR_SDL3=$($sdlSrc -replace '\\', '/')"
        }
    }
    & $cmake @cfgArgs
    if ($LASTEXITCODE -ne 0) { throw "configure ($Build) failed" }
}
if (-not $NoBuild) {
    $targets = @('hotd', 'hotd_viewer')
    if ($Release) { $targets += 'hotd_launch' }   # (its own build folder: the stub is built there too)
    & $cmake --build $Build --config Release --target @targets -- /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}
# the experimental modes (docs/porting.md): a release package must have them OFF; the exe must refuse --totd (Survival Mode stays: HOTD_SURVIVAL_MODE)
$expLine = Get-Content -LiteralPath (Join-Path $Build "CMakeCache.txt") -ErrorAction SilentlyContinue |
    Where-Object { $_ -like "HOTD_EXPERIMENTAL_MODES:*" } | Select-Object -First 1
$expOn = -not ($expLine -match '=(OFF|0|FALSE|NO)$')
if ($Release) {
    if ($expOn) { throw "$Build has HOTD_EXPERIMENTAL_MODES on: a release package must be built with it OFF" }
    $said = (& (Join-Path $Build "Release\hotd.exe") --totd 2>&1 | Out-String)
    if ($said -notmatch 'not in this version') { throw "the release exe did not refuse --totd: $said" }
    Write-Host "experimental modes: OFF (Typing of the Dead 0 and Horde left out, Survival in; checked)"
} else {
    Write-Host ("experimental modes: " + $(if ($expOn) { "ON (the user's own copy)" } else { "OFF" }))
}
# robust-2: a build folder configured without stb_vorbis makes a package without music (the game only logs "built
# without stb_vorbis"); say so before copying
$cache = Get-Content -LiteralPath (Join-Path $Build "CMakeCache.txt") -ErrorAction SilentlyContinue
$stb = ($cache | Where-Object { $_ -like "HOTD_STB_DIR:*" }) -replace '^[^=]*=', ''
if (-not $stb -or -not (Test-Path -LiteralPath (Join-Path $stb "stb_vorbis.c"))) {
    Write-Warning "this build has no stb_vorbis (HOTD_STB_DIR): the package will play NO music (configure with -DHOTD_STB_DIR=<folder with stb_vorbis.c>)"
}

if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out -Recurse -Force }
New-Item -ItemType Directory -Force $Out | Out-Null

function Copy-One([string]$src, [string]$dstRel) {
    if (-not (Test-Path -LiteralPath $src)) { throw "missing: $src" }
    $dst = Join-Path $Out $dstRel
    New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
    Copy-Item -LiteralPath $src -Destination $dst
}

# 2. Program: exe, SDL3.dll, MSVC runtime (shaders and the files check's hash tables are built into the exe)
$rel = Join-Path $Build "Release"
Copy-One (Join-Path $rel "hotd.exe") "bin\hotd.exe"
Copy-One (Join-Path $rel "hotd_launch.exe") "bin\hotd_launch.exe"   # the game exe stub (util/game_exe.h)
Copy-One (Join-Path $rel "hotd_viewer.exe") "bin\hotd_viewer.exe"   # the Model Viewer (src/viewer/main.cpp)
# Light Gun Edition: the quick settings (src/presets/presets.cpp): one program under each setting's name, beside the
# folders whose settings lines the Display ones put into settings\launcher.ini (add lines there to change them only in
# that mode); the Hook of the Reaper game file (no LightgunHID.ini: the game asks for the guns at its first start and writes it)
foreach ($n in "2 - Display - 4x3", "2 - Display - 16x9", "2 - Crosshairs - On", "2 - Crosshairs - Off") {
    Copy-One (Join-Path $rel "hotd_presets.exe") "$n.exe"
}
New-Item -ItemType Directory -Force (Join-Path $Out "4x3"), (Join-Path $Out "16x9") | Out-Null
"; 2 - Display - 4x3.exe puts these lines into settings\launcher.ini (add more to change them only in 4:3)`r`n; The bezel: put it in this folder as bezel.png (1920 x 1080, art in the outer 240 px each side, the middle transparent)`r`naspect=1" |
    Set-Content -LiteralPath (Join-Path $Out "4x3\launcher.ini") -Encoding ascii
"; 2 - Display - 16x9.exe puts these lines into settings\launcher.ini (add more to change them only in 16:9)`r`naspect=0" |
    Set-Content -LiteralPath (Join-Path $Out "16x9\launcher.ini") -Encoding ascii
Copy-One (Join-Path $repo "setup\Hook of the Reaper\defaultLG\hotdrw.txt") "Hook of the Reaper\defaultLG\hotdrw.txt"
Copy-One (Join-Path $repo "setup\Launch HOTD ReWrithe.ahk") "Launch HOTD ReWrithe.ahk"   # the AutoHotkey 1.1 launcher (Esc quits; 1 is the game's pause)
foreach ($n in "Link Play Host", "Link Play Join") {   # online / LAN co-op in one click: the launcher with LinkPlay set
    Copy-One (Join-Path $repo "setup\Launch HOTD ReWrithe - $n.ahk") "Launch HOTD ReWrithe - $n.ahk"
}
# (no LightgunHID.ini: the game writes it after its set-up, so an update never replaces the player's guns)
Copy-One (Join-Path $rel "SDL3.dll") "bin\SDL3.dll"
# angle (docs/renderer_backends.md): ANGLE (BSD; built by vcpkg's "angle" port from Google's sources) beside the exe:
# Video > Renderer's DirectX for graphics without OpenGL 3.3. d3dcompiler_47.dll is part of Windows 8.1 / 10 / 11 (not
# bundled; this ANGLE needs Windows 8 or later anyway). A build without them: OpenGL only, said here
if (Test-Path -LiteralPath (Join-Path $rel "libGLESv2.dll")) {
    foreach ($d in "libEGL.dll", "libGLESv2.dll", "z.dll") { Copy-One (Join-Path $rel $d) "bin\$d" }
} else {
    Write-Warning "no ANGLE DLLs in $rel (HOTD_ANGLE_DIR): the package has no DirectX renderer (OpenGL only)"
}
# openxr (docs/vr.md): the Khronos OpenXR loader (Apache 2.0) beside the exe for VR headsets (--vr openxr), with its
# licence text in licenses\ (NOTICE.md). A build without the SDK (HOTD_OPENXR off) has none: the preview only
if (Test-Path -LiteralPath (Join-Path $rel "openxr_loader.dll")) {
    Copy-One (Join-Path $rel "openxr_loader.dll") "bin\openxr_loader.dll"
    Copy-One (Join-Path $repo "docs\release\LICENSE-OpenXR-Apache-2.0.txt") "licenses\OpenXR-LICENSE.txt"
} else {
    Write-Warning "no openxr_loader.dll in $rel (OpenXR SDK not found): the package has no VR headset support"
}
# relprep: the Build Tools' folder, else any Visual Studio 2022 vswhere finds (a GitHub runner has Enterprise)
$vsRoots = @("C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools")
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path -LiteralPath $vswhere) { $vsRoots += @(& $vswhere -products * -version "[17.0,18.0)" -property installationPath) }
$crt = $vsRoots | ForEach-Object { Join-Path $_ "VC\Redist\MSVC" } | Where-Object { Test-Path -LiteralPath $_ } |
    ForEach-Object { Get-ChildItem -LiteralPath $_ -Directory | Sort-Object Name -Descending } |
    ForEach-Object { Join-Path $_.FullName "x64\Microsoft.VC143.CRT" } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $crt) { throw "MSVC runtime (VC143.CRT) not found under $($vsRoots -join ', ')" }
foreach ($d in "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "vcruntime140.dll", "vcruntime140_1.dll") {
    Copy-One (Join-Path $crt $d) "bin\$d"
}
# the marker the exe looks for: this folder is the package's root (src/main.cpp)
"The House of the Dead: ReWrithe, portable package (no game data)." | Set-Content -LiteralPath (Join-Path $Out "hotd_package.txt") -Encoding ascii

# 3. Start file and README
@'
@echo off
rem The House of the Dead: ReWrithe's setup: it always opens the setup window (install, or change the game files),
rem also when it ran before. To play, use the shortcut / "The House of the Dead ReWrithe.exe" it makes, or bin\hotd.exe.
rem Only paths relative to this file: works from any drive.
rem Personal portable copy: put your own arcade ROM zip in data\arcade (or at data\hotd.zip) and your PC game files,
rem in data\pc (a .zip of them, or unpacked: THOTD.EXE there or one folder down); the game finds them by itself, also when
rem started straight from bin\hotd.exe (Steam, a shortcut), else pick them in the setup.
rem Music: the PC release played CD audio; put your TrackNN.ogg files in data\music (or inside the PC zip's Music folder).
rem .mp3 works too, any name ending in the track number ("House of the Dead 02.mp3"), or a .zip of them.
setlocal
cd /d "%~dp0"
if not exist "%~dp0bin\hotd.exe" (
  echo bin\hotd.exe is missing. If Windows Defender removed it, restore it in Windows Security ^> Protection history
  echo ^(a false positive^), or unzip the whole package again.
  pause
  exit /b 1
)
start "" "%~dp0bin\hotd.exe" --setup %*
'@ | Set-Content -LiteralPath (Join-Path $Out "HOTDReWritheSetup.bat") -Encoding ascii
@'
@echo off
rem Safe Mode: for a PC that has trouble with the game (a black or flickering picture, a crash at the start or on a
rem resolution change, the menu very slow). A 1280 x 720 window, no live attract behind the menu, the menus at 60 fps,
rem no CRT filter and no texture packs. A setting you change in Options keeps these values; HOTDReWritheSetup.bat starts normally.
setlocal
cd /d "%~dp0"
start "" "%~dp0bin\hotd.exe" --safe-mode %*
'@ | Set-Content -LiteralPath (Join-Path $Out "Play (Safe Mode).bat") -Encoding ascii
@'
@echo off
rem Starts the ReWrithe Model Viewer: browse the game's models and stages from your own files (see README.txt).
rem Only paths relative to this file: works from any drive. It finds your files in data\arcade and data\pc by itself,
rem like HOTDReWritheSetup.bat's game does (the first start reads your ROM zip once). Extra arguments are passed on, e.g.
rem "Model Viewer.bat" --chapter 2 --source pc   (--windowed for a window instead of fullscreen).
setlocal
cd /d "%~dp0"
start "" "%~dp0bin\hotd_viewer.exe" %*
'@ | Set-Content -LiteralPath (Join-Path $Out "Model Viewer.bat") -Encoding ascii
# empty: for the player's own files only
New-Item -ItemType Directory -Force (Join-Path $Out "data\pc"), (Join-Path $Out "data\arcade"), (Join-Path $Out "data\music"), (Join-Path $Out "data\steam") | Out-Null
# firstrun: a note in each (a zip keeps no empty folder, and the player sees what goes where; .txt notes are ignored)
"Put your own arcade ROM zip here (MAME's 'hotd' set, The House of the Dead Revision A), e.g. hotd.zip. Loose ROM files work too." |
    Set-Content -LiteralPath (Join-Path $Out "data\arcade\put the arcade ROM zip here.txt") -Encoding ascii
"Put your own 1998 PC game here: the installed folder's contents (THOTD.EXE here or one folder down), or a .zip of it." |
    Set-Content -LiteralPath (Join-Path $Out "data\pc\put the PC game here.txt") -Encoding ascii
"Optional: the PC CD's music, Track02.ogg ... Track12.ogg (or .mp3 with the track number at the end of the name)." |
    Set-Content -LiteralPath (Join-Path $Out "data\music\put the music here.txt") -Encoding ascii
# the player's own Steam pictures, one folder per kind (empty: nothing of anyone else's is shipped)
foreach ($k in "covers", "wide covers", "heros", "logos", "icons") { New-Item -ItemType Directory -Force (Join-Path $Out "data\steam\$k") | Out-Null }
# optional: links to SteamGridDB pictures (not shipped; the player downloads them if they want; links only)
@'
Optional Steam pictures from SteamGridDB (not part of this game)
================================================================
These are links only: nothing below is included. SteamGridDB (https://www.steamgriddb.com, search "The House of
the Dead") has fan-made pictures, mostly from Sega's art. If you want one for your own Steam library:
1. Download it and put it in the matching folder here (covers, wide covers, heros, logos, icons).
2. Start its name with "SteamGridDB - " (e.g. "SteamGridDB - 9982d3ef.png"): the game then shows it as
   "SteamGridDB 1, 2..." so you can tell it apart from your own. Never share or publish these pictures.
3. In the game: Options -> System -> Add to Steam, pick it, Add (Steam closed).
Thanks to SteamGridDB and the artists who uploaded these.

  icons:  https://cdn2.steamgriddb.com/icon/80af02f360a352b23cd28711ee5e2578.png
  icons:  https://cdn2.steamgriddb.com/icon/d71477e8a7cc698620c5f390e485966e.ico
  icons:  https://cdn2.steamgriddb.com/icon/ead79bebb88514b1e270b8856af6fce1.png
  icons:  https://cdn2.steamgriddb.com/icon/ba1ccf0271ba8fb4766db5c608f1012a.ico
  logos:  https://cdn2.steamgriddb.com/logo/b2b9efa258316b09890cf697c00be8a6.png
  logos:  https://cdn2.steamgriddb.com/logo/653e7cac0ffda01adbe54b15c8ed9ecd.png
  logos:  https://cdn2.steamgriddb.com/logo/39acb542878b92997468cd17aa72399e.png
  heros:  https://cdn2.steamgriddb.com/hero/bf3db48ad49a6cbe21fa3ec8b97c8b83.jpg
  heros:  https://cdn2.steamgriddb.com/hero/2680d306830f7fe3bf8cf37f4d128a19.jpg
  heros:  https://cdn2.steamgriddb.com/hero/c2f2c5bea08415228284d7e51461a137.png
  heros:  https://cdn2.steamgriddb.com/hero/f8db36f4eb09c494a1619ebe4d8db028.png
  covers:  https://cdn2.steamgriddb.com/grid/9504d73a7be7f3974143681328c6fc6d.png
  covers:  https://cdn2.steamgriddb.com/grid/f63dee3d1d26309a507b99deaa4990ad.png
  covers:  https://cdn2.steamgriddb.com/grid/ded40f09fc03968e3c847a7b258d01e1.png
  covers:  https://cdn2.steamgriddb.com/grid/7931217424ef04c1bdcb0a60c4319e40.png
  covers:  https://cdn2.steamgriddb.com/grid/9982d3efd6dcab4387fc1817ef660f08.png
'@ | Set-Content -LiteralPath (Join-Path $Out "data\steam\Optional downloads (SteamGridDB).txt") -Encoding utf8

@'
The House of the Dead: ReWrithe @VERSION@ (Chapters 1-4 and the ending)
A C++ rewrite of the arcade game and its PC port, combined.
================================================================
Health warning: the game has flashing lights. A few people may have seizures from flashing lights; stop playing
if you feel unwell.

Quick start
-----------
1. Unzip this folder anywhere you like (any drive, any folder name).
2. Put your own arcade ROM zip (MAME's "hotd" set) in data\arcade.
3. Put your own 1998 PC game (its installed folder, or a .zip of it) in data\pc; music (TrackNN.ogg) in data\music.
4. Double-click HOTDReWritheSetup.bat once. After the files check it makes "The House of the Dead ReWrithe.exe" in this
   folder (your PC game's icon): start the game with that from then on. Mouse = gun.
5. Problem? The setup window says what is missing and where it goes; details are in settings\hotd.log.

Something went wrong?
---------------------
- Windows SmartScreen "protected your PC": the program is not signed; "More info" -> "Run anyway" (SHA-256 sums are
  in the release notes).
- The setup window shows CANNOT START: read the line starting with "->": it tells you what to choose or where to put it
  (the arcade zip and the PC game swapped, a .7z / .rar or CD image, a zip inside a zip, files not found).
- "OpenGL 3.3" message: update your graphics driver (from NVIDIA, AMD or Intel).
- A black, flickering or very slow picture, or a crash when the game starts or changes resolution: start it with
  "Play (Safe Mode).bat" (a 1280 x 720 window, the simplest drawing). Please report it with settings\hotd.log.
- The game closed with a message: a crash report (crash-<date>.txt) and hotd.log are in this folder's settings\.
  Please attach both to a bug report (they contain no game files). hotd.prev.log is the run before.
- Settings look wrong: delete settings\launcher.ini (a damaged one is set aside as launcher.ini.damaged by itself).
- No sound: the PC files are needed for sound (and a working audio device; the game also runs without one).

An unofficial rewrite. This package contains NO game data: you need your own copies of
  - the arcade ROMs: MAME's "hotd" set (The House of the Dead, Revision A), as a zip or loose files,
    any file or folder names (they are recognised by content);
  - the 1998 PC release, installed (sound and the PC models come from it): its folder, or a .zip of that folder
    (no need to unpack it; the game may be one folder down inside the zip). A .rar / .7z is not read: convert
    it to a .zip (or unpack it).

How to run
----------
Double-click HOTDReWritheSetup.bat. In the setup window, drop (or click to choose) your Arcade ROMs and your PC game: a zip, a .7z, a folder or a CD image (choose its .cue); any names. Then Install (or Play from here). The PC game's CD music is optional: without it the arcade's music plays. HOTDReWritheSetup.bat always opens the setup again (to change the files).
If your PC copy does not pass, try another one: copies of the 1998 PC game differ, and the US and European releases work best. When the setup says a file is missing or damaged, it is missing or damaged in that copy itself, wherever it came from: not a problem of the setup or the game. Keeping a second copy (US or European) at hand is recommended.
Your own files only: put your arcade ROM zip in data\arcade and your PC game files (a .zip of them, or unpacked)
in data\pc for a copy you carry to another PC of yours; the game then finds them by itself (also when started from bin\hotd.exe, e.g. by Steam). Never share a package
with game files in it.
Music: the PC release played CD audio; put your TrackNN.ogg files in data\music (or inside the PC zip's Music
folder). A Music folder next to your PC folder / zip, or the tracks beside the PC zip, are found as well.
.ogg or .mp3; any name whose last number is the PC CD's track number ("Track02.ogg", "House of the Dead 02.mp3");
a .zip of them in data\music works too. Numbered as the Sega Saturn disc instead of the PC CD? Options -> Advanced -> Music
Numbering (auto by default: such a set is recognised by itself) maps them to the right songs.
The setup checks the files (CRC-32 / SHA-1 against the official releases) and tells you which file is
missing or different. Results and settings are kept in this folder's settings\ (paths inside the folder are
saved relative to it, so the folder can be moved or copied to another PC of yours).

Menu: the PC version's own menu. Main menu: Arcade, Chapter Select, Boss Mode, PC Mode, Gallery, Co-op (local 2P,
4 players, online), Sonic Mode, High Scores, Options, Quit. Options: the PC's screen plus the tabs Video, Audio,
Controls, Gameplay, Mods and System; the line at the bottom explains each item.
  Boss Mode        the four bosses or all in a row, with a clock and best-times tables (a new best time asks for your name).
  PC Mode          pick a character per player (Rogan, G, Rogan 2, G 2, Sophie, Researcher) and play by the PC's rules;
                   Options -> Continues (0-9, default 5) is the PC's shared credit pool.
  Gallery          the characters and creatures in 3D, arcade or PC models.
  Secrets          hold Ctrl on the menu and type SKIDMARX (Cheat, Edit Char Stats) or CREATURE (Creature Test).
                   Thanks to RUNESA for the tip.

Steam: close Steam, then in the game press Options -> System -> Add to Steam (artwork from your own PC files).
Start Steam: the game is in your Library. Pressing it again updates the entry; Options -> System -> Add to Steam can also remove it.
Your own Steam pictures (pick and choose, mix and match): put .png / .jpg pictures in data\steam\covers (600 x 900),
"wide covers" (920 x 430), heros (the background, 1920 x 620), logos and icons (.ico too); SteamGridDB's "grids" folder works
as well. In the game: Options -> System -> Add to Steam: each picture cycles through "Game files" and yours,
then Add (Steam closed). Default.png in each folder is the game's own picture (made from your PC files when you
open that page). Pictures you did not make (e.g. from SteamGridDB): start their names with "SteamGridDB - " so the
game shows them as "SteamGridDB 1, 2..." and you can tell them apart; never publish or share them. Optional: data\steam\Optional downloads (SteamGridDB).txt lists
SteamGridDB pictures you can download yourself.

Crosshairs: Options -> Video -> Crosshair cycles the built-in sights (the arcade-style one, the PC's, a simple
cross, ring + dot, corner brackets, gap cross), then your own; Crosshair Size and Crosshair
Thickness change how big and how thick the drawn ones are. Your own crosshair pictures: put .png files in
data\crosshairs (the settings\crosshairs folder works too):
  crosshair.png      one picture, tinted per player (draw it white or grey: player 1 red, player 2 blue), or
  p1.png and p2.png  one picture per player, used exactly as they are (only p1.png: both players use it);
either directly in data\crosshairs or in a sub-folder (the folder's name is the choice's name: "Custom: <name>").
Any other .png directly in data\crosshairs is a tinted picture named after its file. Transparent PNGs (not interlaced,
up to 1024 x 1024); the picture's middle is the aim; 1 picture pixel = 1 screen pixel at 1080 lines (scaled with the
window; Crosshair Size scales it too). Command line: --crosshair custom:<name>.

Controls: player 1 = the mouse (left trigger, right reload). Player 2 = a second mouse or the keyboard
(arrows aim, Space trigger, Enter reload). Keys 1 / 2 (or a mouse's middle button) = start / join / continue.
Gamepads and light guns: Options -> Controls -> Player 1 Input / Player 2 Input (Mouse, Keyboard, Gamepad, Light Gun), and
Controller: Player 1 / 2 for a pad's control style, each with its own settings:
  Classic Pad      a stick aims (linear, small dead zone), the other stick fine-adjusts
  Motion Only      the gyro aims like a pointer (DualSense, DualShock 4, Switch Pro, Joy-Con)
  Motion + Stick   the gyro aims, a stick fine-adjusts (the default for pads with a gyro)
  Sideways         held sideways like a gun (right- or left-handed); the shoulder trigger under your finger shoots
  Touchpad         the touchpad aims: Pointer (where you touch) or Trackpad (DualSense, DualShock 4)
  Pad buttons: R2 / R1 / A (Cross) shoot, L2 / L1 / B / X reload, Y (Triangle) held = ratchet, tapped = recentre,
  Start = start / join / pause. Vibration on hits / enemy hits / reloads, the light bar in the player's colour, the
  DualSense's adaptive trigger (all per player). Unplugging a pad pauses the game until it is back.
  Light guns that act as an absolute mouse (Sinden, AimTrak, GunCon 3 with a mouse driver, Wii Remote + DolphinBar in
  mouse mode): set Light Gun, then F9 in the game to calibrate (shoot the two targets). Trigger shoots, right button
  reloads, pointing off the screen and shooting reloads.
F1 developer lines, P pause, F11 / Alt+Enter fullscreen (starts fullscreen; "Start Fullscreen" in Options -> Video, or fullscreen=0 in launcher.ini, or --windowed; remembers how you left it), F12 screenshot, Esc the pause menu.
Models: arcade by default (the arcade's models and textures); switch to the PC models in Options.

More:
  Online Co-op     (main menu: Co-op) up to four PCs over the internet or the LAN: Host a Game (name, password; send the
                   share code; Play Alone starts before anyone joins) / Join a Game (LAN list or share code); a running
                   game can be joined or rejoined. Four players: "Up to 4 PCs" (one each) or two PCs with two seats each.
                   Windows Firewall asks once: allow Private networks. Step by step (port forwarding, the firewall, what
                   each message means): ONLINE HOWTO.txt in this folder.
  Mods             (Options -> Mods) your own packs in data\mods (switch on, order); visual only.
  Sonic Mode       Off / Frenzy x1.2 / Overkill x1.5 / Nightmare (bosses faster, the Magician most): faster play,
                   cut-scenes normal, own top 10.
  Game Speed       (Options -> Video) Auto: 57.52 Hz like the arcade, 60 Hz in PC Mode; or force Arcade / 60 Hz. Frame
                   rate, vsync and Smooth Motion set how the picture is drawn.
  CRT Filter       (Options -> Video) Off / Arcade Monitor / Consumer TV / Sharp Scanlines / Custom.
  Players 3 / 4    the experimental 4-player mode (gamepads), empty seats can be CPU teammates.
Settings, the log (hotd.log) and crash reports are in this folder's settings\ (delete launcher.ini there for the defaults).

Model Viewer
------------
Double-click "Model Viewer.bat" to look around the game's worlds and creatures with a free camera, from your own files
(the same data\arcade and data\pc as the game; the first start reads the ROM zip once). It shows the four chapters'
stages and their models, from the arcade or the PC release, side by side or switched one at a time. The controls are
listed on screen (H hides them). It starts fullscreen (F11 / Alt+Enter toggles; --windowed on the command line).
  WASD move, Q / E down / up, Shift faster; hold the right mouse button (or M) and move the mouse to look around
  1-4 or PgUp / PgDn chapter;  Tab everything arcade <-> PC;  F1-F5 source of the world / zombies / bosses / effects / humans
  P source of the model at the screen centre;  R clear those choices;  N next recorded creature view
  U / Shift+U next / previous motion of the creature in the middle;  J animation on / off;  O creatures;  K sky
  L lighting;  F arcade texture filtering;  G arcade gamma;  Y ride the Chapter 1 camera rail
  F12 screenshot (the package's screenshots\ folder);  C print the camera;  Esc quit

Teaser limits: Chapters 1-4 and the ending play, but parts are not ported yet and are skipped, and some scenes are rough. Everything the game shows
and plays comes from your own ROMs and PC files.

Windows 10/11, 64-bit, OpenGL 3.3. Made by tools\make_portable_hotd.ps1 in the rewrite's repository.
'@ | Set-Content -LiteralPath (Join-Path $Out "README.txt") -Encoding ascii
# relprep: the version in its title (CMakeLists.txt's project VERSION, from the build's cache)
$readme = Join-Path $Out "README.txt"
$pv = ((Get-Content -LiteralPath (Join-Path $Build "CMakeCache.txt") -ErrorAction SilentlyContinue) | Where-Object { $_ -like "CMAKE_PROJECT_VERSION:*" }) -replace '^[^=]*=', ''
(Get-Content -LiteralPath $readme -Raw).Replace("@VERSION@", $(if ($pv) { "v$pv" } else { "" })) | Set-Content -LiteralPath $readme -Encoding ascii -NoNewline
# the repo's shipped data (our own, no game data): data\ui (logo2: the Remake menu's "ReWrithe" wordmark, made with
# Special Elite, Apache 2.0: NOTICE.md), data\fonts (Noto Sans for Korean / Japanese / Chinese / Arabic / Hebrew /
# Cyrillic, OFL.txt), data\lang (the language files: en / ko / ja / zh / ru / ar / he ...), data\mods (the ragdoll mod and the example mod); every file
# the repo has there, so a new one comes along by itself
foreach ($d in "ui", "fonts", "lang", "mods", "vr") {   # (vr: vr.ini, the VR settings: docs/vr.md)
    $src = Join-Path $repo "data\$d"
    if (-not (Test-Path -LiteralPath $src)) { continue }
    Get-ChildItem -LiteralPath $src -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($src.Length).TrimStart('\')
        $dst = Join-Path $Out "data\$d\$rel"
        New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $dst -Force
    }
}
# -Release: the modes left out of the release builds (Typing of the Dead 0, Horde) are not shown anywhere: their menu
# texts leave the language files too (the user, 2026-11-03: "don't even show them"); their data folders are not copied
if ($Release) {
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    Get-ChildItem -LiteralPath (Join-Path $Out "data\lang") -Filter *.txt -File | ForEach-Object {
        $lines = [IO.File]::ReadAllLines($_.FullName, $utf8)
        $keep = $lines | Where-Object { $_ -notmatch '^\s*#' -and $_ -match ' = ' -and ($_ -split ' = ', 2)[0] -match '(?i)typing of the dead|totd|horde' }
        if ($keep) { [IO.File]::WriteAllLines($_.FullName, ($lines | Where-Object { $keep -notcontains $_ }), $utf8) }
    }
}
# the license and the notice (what the license covers; no game data)
Copy-Item -LiteralPath (Join-Path $repo "LICENSE") -Destination (Join-Path $Out "LICENSE.txt")
Copy-Item -LiteralPath (Join-Path $repo "NOTICE.md") -Destination (Join-Path $Out "NOTICE.txt")
Copy-Item -LiteralPath (Join-Path $repo "docs/release/CREDITS.txt") -Destination (Join-Path $Out "CREDITS.txt")
Copy-Item -LiteralPath (Join-Path $repo "CHANGELOG.md") -Destination (Join-Path $Out "CHANGELOG.txt")   # setup2: What's New reads it
# online co-op step by step (hosting, the share code, the LAN, port forwarding, the firewall, the messages)
Copy-Item -LiteralPath (Join-Path $repo "docs\online_howto.md") -Destination (Join-Path $Out "ONLINE HOWTO.txt")

$bytes = (Get-ChildItem -LiteralPath $Out -Recurse -File | Measure-Object Length -Sum).Sum
$files = (Get-ChildItem -LiteralPath $Out -Recurse -File).Count
"Portable folder: $Out"
"{0} files, {1:N1} MB" -f $files, ($bytes / 1MB)

# 4. firstrun: the release checks (docs/release/build_release.md), then the zip and its SHA-256 sums
$exe = Join-Path $Out "bin\hotd.exe"
$vi = (Get-Item -LiteralPath $exe).VersionInfo
if ($vi.ProductName -notlike "*ReWrithe*") { throw "hotd.exe has no VERSIONINFO product name (got '$($vi.ProductName)')" }
$exeText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($exe))
if ($exeText -notmatch 'level="asInvoker"') { throw "hotd.exe: no asInvoker manifest" }
foreach ($bad in "iqbal", "C:\Users\", "C:/Users/", "M2 RECOMPILE") {   # (the relative /private/... developer fallbacks are expected)
    if ($exeText.Contains($bad)) { Write-Warning "hotd.exe contains the text '$bad' (a build path?)" }
}
$cacheText = Get-Content -LiteralPath (Join-Path $Build "CMakeCache.txt") -ErrorAction SilentlyContinue
$icon = ($cacheText | Where-Object { $_ -like "HOTD_LOCAL_ICON:*" }) -replace '^[^=]*=', ''
if ($icon -ne "OFF" -and $icon -ne "NO") {
    Write-Warning "this build was not configured with -DHOTD_LOCAL_ICON=OFF: it may carry the PC game's icon (not for release)"
}
$game = Get-ChildItem -LiteralPath $Out -Recurse -File | Where-Object {
    $_.Extension -match '^\.(zip|7z|rar|ogg|mp3|wav|rbh|vmc|bin|rom|iso)$' -or $_.Name -match '^(epr|mpr)-' -or $_.Name -ieq "THOTD.EXE" }
if ($game) { throw "game data in the package: $($game.FullName -join ', ')" }
"checks: VERSIONINFO '$($vi.ProductName)' $($vi.FileVersion), asInvoker, no game data, icon setting '$icon'"
if (-not $NoZip) {
    # relprep: named after the version (CMakeLists.txt's project VERSION, in the build's cache)
    $ver = ($cacheText | Where-Object { $_ -like "CMAKE_PROJECT_VERSION:*" }) -replace '^[^=]*=', ''
    $zipName = if ($ver) { "hotd-rewrithe-v$ver-win64.zip" } else { "hotd-rewrithe-{0}-win64.zip" -f (Get-Date -Format "yyyy-MM-dd") }
    $zip = Join-Path (Split-Path $Out) $zipName
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    # relprep: Windows' bsdtar (-a: a zip from the extension), not Compress-Archive: Windows PowerShell 5.1's
    # Compress-Archive stores "bin\hotd.exe" with backslashes, which Linux / macOS unzip tools warn about or mangle
    & (Join-Path $env:SystemRoot "System32\tar.exe") -a -c -f $zip -C (Split-Path $Out) (Split-Path $Out -Leaf)
    if ($LASTEXITCODE -ne 0) { throw "zip failed" }
    $sums = Join-Path (Split-Path $Out) "SHA256SUMS.txt"
    & (Join-Path $PSScriptRoot "package_hashes.ps1") -Package $Out -Zip $zip | Set-Content -LiteralPath $sums -Encoding ascii
    "Zip: $zip ({0:N2} MB)" -f ((Get-Item -LiteralPath $zip).Length / 1MB)
    "SHA-256 sums: $sums"
    Get-Content -LiteralPath $sums | Select-Object -First 1
}
