#!/bin/bash
# Linux / Steam Deck package of the game (docs/porting.md "SteamOS package"): bin/hotd + bin/hotd_viewer with the
# libSDL3.so.0 they were built against (rpath $ORIGIN: the bundled one is used, not the system's), hotd.sh /
# hotd_viewer.sh launchers, the same data/ layout as the Windows package (tools/make_portable_hotd.ps1).
# NO GAME DATA goes in: the player puts their own arcade ROM zip and PC game in data/arcade and data/pc.
#
#   tools/make_linux_package.sh [-b <build folder>] [-o <output folder>] [--no-build] [--no-tar] [--experimental]
#
# Default build folder: build-linux (cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
# -DHOTD_EXPERIMENTAL_MODES=OFF ..., docs/porting.md). A release package needs HOTD_EXPERIMENTAL_MODES=OFF (no Typing of
# the Dead 0 or Horde, Survival Mode in: docs/porting.md "Experimental modes"): the script stops on a build folder with them on,
# unless --experimental (a package for testing only).
# Default output: <main checkout>/private/portable/HOTD ReWrithe Linux (inside private/, never committed).
# For a binary that runs on every SteamOS / older desktop Linux, build in Valve's Steam Runtime "sniper" SDK (glibc
# 2.31); a build made on a newer distribution needs that distribution's glibc or newer (the script prints the
# newest glibc symbol the binary needs).
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$repo/build-linux"
out=""
dobuild=1
dotar=1
experimental=0
while [ $# -gt 0 ]; do
    case "$1" in
        -b) build="$2"; shift 2 ;;
        -o) out="$2"; shift 2 ;;
        --no-build) dobuild=0; shift ;;
        --no-tar) dotar=0; shift ;;
        --experimental) experimental=1; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
priv="$repo/private"
if [ ! -d "$priv" ]; then   # a git worktree: private/ is in the main checkout
    common="$(git -C "$repo" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
    [ -n "$common" ] && priv="$(dirname "$common")/private"
fi
[ -z "$out" ] && out="$priv/portable/HOTD ReWrithe Linux"

# 0. the experimental modes off (a release package; docs/porting.md "Experimental modes")
if [ "$experimental" = 0 ] && ! grep -Eq '^HOTD_EXPERIMENTAL_MODES:BOOL=(OFF|0|FALSE|NO)$' "$build/CMakeCache.txt" 2>/dev/null; then
    echo "error: $build is not configured with -DHOTD_EXPERIMENTAL_MODES=OFF (a release package leaves out Typing of the Dead 0," >&2
    echo "       and Horde; Survival Mode stays in); reconfigure it, or pass --experimental for a testing package" >&2
    exit 2
fi

# 1. Release build
if [ "$dobuild" = 1 ]; then
    cmake --build "$build" --target hotd hotd_viewer
fi
if ! grep -q '^HOTD_STB_DIR:.*=.\+' "$build/CMakeCache.txt" 2>/dev/null; then
    echo "warning: this build has no stb_vorbis (HOTD_STB_DIR): the package will play NO music" >&2
fi

rm -rf "$out"
mkdir -p "$out/bin"

# 2. Program: the exes and the SDL3 they link (libSDL3.so.0, found the way the loader finds it)
cp "$build/hotd" "$build/hotd_viewer" "$out/bin/"
strip --strip-unneeded "$out/bin/hotd" "$out/bin/hotd_viewer" 2>/dev/null || true
sdlOf() { ldd "$1" | sed -n 's/^.*libSDL3\.so\.0 => \(.*\) (0x[0-9a-f]*)$/\1/p'; }   # (paths may have spaces)
# linux2 (2026-11-03): an SDL built by the build itself (HOTD_FETCH_SDL3) first: ldd would pick a system libSDL3 (e.g.
# a distribution's 3.4 with X11 / Wayland linked in) over it, as the exe's rpath is only $ORIGIN
sdl="$build/_deps/sdl3-build/libSDL3.so.0"
[ -f "$sdl" ] || sdl="$(sdlOf "$build/hotd")"
[ -f "$sdl" ] || { echo "libSDL3.so.0 not found (ldd $build/hotd)" >&2; exit 1; }
cp -L "$sdl" "$out/bin/libSDL3.so.0"
readelf -d "$out/bin/hotd" | grep -q 'R.*PATH.*\$ORIGIN' || { echo "bin/hotd has no rpath \$ORIGIN (CMakeLists.txt)" >&2; exit 1; }
echo "The House of the Dead: ReWrithe, portable package (no game data)." > "$out/hotd_package.txt"

# 3. Launchers (only paths relative to themselves: the folder can live anywhere)
cat > "$out/hotd.sh" <<'EOF'
#!/bin/sh
# Starts The House of the Dead: ReWrithe (the launcher). Your own files: data/arcade (the arcade ROM zip), data/pc
# (the 1998 PC game, a .zip of it or unpacked), data/music (TrackNN.ogg / .mp3). Extra arguments are passed on.
cd "$(dirname "$0")" || exit 1
exec ./bin/hotd "$@"
EOF
cat > "$out/hotd_viewer.sh" <<'EOF'
#!/bin/sh
# Starts the ReWrithe Model Viewer (the same data/arcade and data/pc as the game). Extra arguments are passed on.
cd "$(dirname "$0")" || exit 1
exec ./bin/hotd_viewer "$@"
EOF
# linux (2026-11-03): the setup (as HOTDReWritheSetup.bat on Windows): the game files check, Install (a copy in a
# folder of your choice with desktop / applications menu entries) or Play from here
cat > "$out/hotd-setup.sh" <<'EOF'
#!/bin/sh
# Opens the setup of The House of the Dead: ReWrithe: checks your game files; Install copies the game to a folder of
# your choice (default ~/Games/HOTD ReWrithe) with a desktop and an applications menu entry; or Play from here.
cd "$(dirname "$0")" || exit 1
exec ./bin/hotd --setup "$@"
EOF
chmod +x "$out/hotd.sh" "$out/hotd_viewer.sh" "$out/hotd-setup.sh" "$out/bin/hotd" "$out/bin/hotd_viewer"

# empty: for the player's own files only (a note in each: a tar keeps empty folders, but the player sees what goes where)
mkdir -p "$out/data/pc" "$out/data/arcade" "$out/data/music" "$out/data/mods"
for k in covers "wide covers" heros logos icons; do mkdir -p "$out/data/steam/$k"; done
echo "Put your own arcade ROM zip here (MAME's 'hotd' set, The House of the Dead Revision A), e.g. hotd.zip. Loose ROM files work too." \
    > "$out/data/arcade/put the arcade ROM zip here.txt"
echo "Put your own 1998 PC game here: the installed folder's contents (THOTD.EXE here or one folder down), or a .zip of it. File names in any case work." \
    > "$out/data/pc/put the PC game here.txt"
echo "Optional: the PC CD's music, Track02.ogg ... Track12.ogg (or .mp3 with the track number at the end of the name)." \
    > "$out/data/music/put the music here.txt"

cat > "$out/README.txt" <<'EOF'
The House of the Dead: ReWrithe - Linux / Steam Deck package
A C++ rewrite of the arcade game and its PC port, combined.
================================================================

An unofficial rewrite. This package contains NO game data: you need your own copies of the arcade ROMs (MAME's
"hotd" set) and the 1998 PC release (its installed folder, or a .zip of it).

Quick start (desktop Linux)
---------------------------
1. Unpack this folder anywhere (tar -xzf hotd-...-linux-x86_64.tar.gz).
2. Put your arcade ROM zip in data/arcade, your PC game (folder contents or a .zip) in data/pc, music in data/music.
3. Run ./hotd.sh (or double-click it in your file manager: "Run as a program"). The files are checked once, then
   the main menu comes up. Mouse = gun.
Settings, the log (hotd.log) and crash reports are in this folder's settings/. Everything else works as in the
Windows package's README (menus, controls, Options, Online Co-op).
Or run ./hotd-setup.sh: drop or pick your ROM zip and PC game (a folder, .zip, CD image .cue / .bin / .iso; .7z / .rar
need bsdtar or 7-Zip installed), then Install (a copy in ~/Games/HOTD ReWrithe or a folder you choose, with a desktop
icon and an applications menu entry; nothing outside that folder and those entries is written; uninstall:
bin/hotd --uninstall in the installed copy) or Play from here.
Updates: the game says when a new version is out and opens the download page (Linux does not update itself).

Steam Deck (SteamOS)
--------------------
1. Desktop Mode (Steam button -> Power -> Switch to Desktop). Copy this folder to the Deck (e.g. ~/Games/HOTD ReWrithe)
   and your own files into its data/arcade, data/pc and data/music (Game Mode has no file picker: the game finds them
   there by itself).
2. Add it to Steam, either way:
   - in the game: run ./hotd.sh once, Options -> System -> Add to Steam, with Steam CLOSED (it rewrites its list when
     it exits): right-click Steam in the task bar -> Exit first, then press Add, then start Steam again; or
   - by hand: Steam -> Games -> "Add a Non-Steam Game to My Library" -> Browse -> pick hotd.sh (set "File type: All
     files" to see it) -> Add Selected Programs (bin/hotd itself works too: it finds its folder by itself). Steam's
     Launch Options are passed on, e.g. --windowed.
3. Back to Game Mode: the game is in the Library under Non-Steam. It starts fullscreen at 1280 x 800.
Controls on the Deck: Steam Input gives the game an Xbox-style pad: a stick aims, R2 shoots, L2 reloads, Start
pauses. Gyro aim: pick a Steam Input layout that passes the gyro to the game as a motion sensor (a "Gamepad with
Gyro" layout), then Motion + Stick in Options -> Controls. The touch screen works as a light gun (tap = shot where
you touch); turn on Options -> Auto Reload for it (a touch cannot shoot off the screen to reload). The trackpads
move the mouse.
Suspend: a game resumes after waking; an online game will have timed out.

Online co-op
------------
Works with Windows and other Linux PCs (one protocol for every system: cross-play). Hosting needs UDP port 47800 (or
the one you choose) open: Linux distributions usually have no firewall in the way; over the internet the router must
forward the port (UPnP is not used on Linux yet). Step by step: ONLINE HOWTO.txt (written for Windows; skip the
Windows Firewall part).

Requirements
------------
64-bit x86 Linux with OpenGL 3.3 (Mesa / the GPU driver) and glibc 2.31 or newer (the release is built in the Steam
Runtime 3 "sniper" SDK: SteamOS 3, Linux Mint 20+, Ubuntu 20.04+, Debian 11+, current Fedora / Arch).
The bundled bin/libSDL3.so.0 is used (not the system's). Audio through PipeWire / PulseAudio / ALSA (SDL's own).
Made by tools/make_linux_package.sh in the rewrite's repository.
EOF
# relprep: the repo's shipped data, as the Windows package (data/ui with the prompts and their licences, data/fonts with
# OFL.txt, data/lang, data/mods): our own or third-party files with their licences in NOTICE.md, no game data
for d in ui fonts lang mods; do
    [ -d "$repo/data/$d" ] && mkdir -p "$out/data/$d" && cp -R "$repo/data/$d/." "$out/data/$d/"
done
# a release package: the modes left out (Typing of the Dead 0, Horde) leave the language files too, as the Windows
# package (make_portable_hotd.ps1 -Release; the user, 2026-11-03: "don't even show them")
if [ "$experimental" = 0 ]; then
    for f in "$out"/data/lang/*.txt; do
        awk '{ if ($0 !~ /^[ \t]*#/ && index($0, " = ")) { k = tolower(substr($0, 1, index($0, " = ") - 1));
               if (k ~ /typing of the dead|totd|horde/) next } print }' "$f" > "$f.tmp" && mv "$f.tmp" "$f"
    done
fi
cp "$repo/LICENSE" "$out/LICENSE.txt"
cp "$repo/NOTICE.md" "$out/NOTICE.txt"
cp "$repo/docs/release/CREDITS.txt" "$out/CREDITS.txt"
cp "$repo/CHANGELOG.md" "$out/CHANGELOG.txt"   # setup2: What's New reads it
cp "$repo/docs/online_howto.md" "$out/ONLINE HOWTO.txt"

# 4. Checks: no game data, the bundled SDL is the one the exe loads, the newest glibc it needs
bad="$(find "$out" -type f \( -iname '*.zip' -o -iname '*.7z' -o -iname '*.rar' -o -iname '*.ogg' -o -iname '*.mp3' -o -iname '*.wav' \
      -o -iname '*.rbh' -o -iname '*.vmc' -o -iname '*.bin' -o -iname '*.rom' -o -iname '*.iso' -o -iname 'epr-*' -o -iname 'mpr-*' -o -iname 'thotd.exe' \))"
[ -z "$bad" ] || { echo "game data in the package: $bad" >&2; exit 1; }
loaded="$(LD_LIBRARY_PATH= sdlOf "$out/bin/hotd")"
case "$loaded" in "$out/bin/libSDL3.so.0") ;; *) echo "bin/hotd loads $loaded, not the bundled libSDL3.so.0" >&2; exit 1 ;; esac
glibc="$(objdump -T "$out/bin/hotd" "$out/bin/libSDL3.so.0" | grep -o 'GLIBC_[0-9.]*' | sort -t. -k2,2n -k3,3n -u | tail -1)"
for f in "$out/bin/hotd" "$out/bin/hotd_viewer" "$out/bin/libSDL3.so.0"; do
    for bad in iqbal "/mnt/c/" "M2 RECOMPILE"; do
        if grep -q -a -F "$bad" "$f"; then echo "warning: $(basename "$f") contains the text '$bad' (a build path?)" >&2; fi
    done
done
echo "Package: $out"
echo "$(find "$out" -type f | wc -l) files, $(du -sh "$out" | cut -f1); needs $glibc or newer; SDL $(basename "$(readlink -f "$sdl")")"
if [ "$dotar" = 1 ]; then
    ver="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build/CMakeCache.txt" 2>/dev/null)"   # (CMakeLists.txt project VERSION)
    [ -n "$ver" ] && tag="v$ver" || tag="$(date +%Y-%m-%d)"   # (was "v1.1.01.1.0": both expansions applied)
    tarball="$(dirname "$out")/hotd-rewrithe-$tag-linux-x86_64.tar.gz"
    tar -C "$(dirname "$out")" -czf "$tarball" "$(basename "$out")"
    (cd "$(dirname "$tarball")" && sha256sum "$(basename "$tarball")") > "$(dirname "$out")/SHA256SUMS-linux.txt"
    echo "Tarball: $tarball ($(du -h "$tarball" | cut -f1))"
    cat "$(dirname "$out")/SHA256SUMS-linux.txt"
fi
