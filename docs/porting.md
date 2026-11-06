# Porting: Linux, Steam Deck (SteamOS), macOS

Workers "linuxport" and "linux2", 2026-10-20. Goal: Linux and the Steam Deck (important), macOS (later), mobile maybe
one day. Windows behaviour is unchanged (all checks give the same numbers).

## Where it stands

- **Release-ready Linux package, CI without a release (worker "linux2", second round, 2026-11-03):** main (with
  OpenXR, Typing of the Dead 0, Survival, 13 languages, cosmetic mods) built in the **Steam Runtime 3 "sniper"
  SDK** (podman in WSL; Debian 11, glibc 2.31, gcc-14 + its static libstdc++, SDL 3.2.10 from source,
  `HOTD_EXPERIMENTAL_MODES=OFF`, `HOTD_SURVIVAL_MODE=ON`). One fix: `game/cosmods.h` lacked `<cstdint>` (GCC). With the built SDL 3.2.10:
  `--input-test` 2277 checks, 0 failed (Wayland); Chapter 1 16350 (arcade and PC models); Chapter 4's ending 34967.
  The package (`tools/make_linux_package.sh` in the same SDK, as the CI): **needs GLIBC_2.29 at most** (runs on glibc
  2.31+: SteamOS 3, Ubuntu 20.04+, Mint 20+, Debian 11+); unpacked into a fresh folder its `hotd.sh` plays Chapter 1 to
  16350 both in WSL (glibc 2.43) and inside the sniper container itself (glibc 2.31); `--setup-test` installs 773
  files + 29 ROMs + 193 PC files + 12 tracks + 2 `.desktop` entries in a sandbox, the installed copy runs,
  `--uninstall --yes` removes it; `--survival` is refused. A **clang 21 + libc++** build (the macOS compiler family
  and C++ library, in WSL) compiles with no errors and gives the same Chapter 1 (16350) and Chapter 4 (34967) and
  `hotd_nettest` ALL PASSED. CI: section "CI without a release". Note: with Ubuntu 26.04's **system SDL 3.4.2** the
  input test fails 52 checks (the Remake menu's dropdown at width 640, the pad / light gun input task frames 565-610);
  the packages bundle 3.2.10 (rpath `$ORIGIN`), so this only matters for a later SDL upgrade.
- **Android / Meta Quest 3 (worker "quest", 2026-11-03):** a standalone VR APK (arm64-v8a, OpenGL ES 3, OpenXR) built
  with the NDK, no Gradle (`tools/make_quest_apk.ps1`); not run on a headset yet. Android-only changes: `crash_log.cpp`
  (no execinfo before API 33: the unwinder), `files_check.cpp` (libc++'s file clock), `util/android_app.*` (the start-up),
  the ES context in `render/gl_backend.cpp`. docs/quest.md.
- **v1.1.0 (worker "linux", 2026-11-03):** builds on Linux again (two GCC fixes) with the setup2 installer, the update
  notice and the .7z / .rar unpacking made portable (the inventory's last rows). WSL, package-style build (SDL 3.2.10
  from source: its `libSDL3.so.0` needs only glibc; libstdc++ linked in): `--input-test` 2277 checks, 0 failed (with
  `SDL_VIDEODRIVER=wayland`: under WSLg's XWayland a hidden window misses about 12 window-size checks; Windows 2277 /
  0), Chapter 1 ended at 16350 (arcade and PC models; Windows 16350), Chapter 4's ending at 34967 (Windows 34967),
  `hotd_nettest` all passed. The package's `hotd.sh` plays Chapter 1 to 16350 with its own SDL; `--setup-test`
  installs a copy with its two `.desktop` entries and `--uninstall --yes` removes them.

- **The game runs on Linux** (worker "linux2", WSL Ubuntu 26.04, GCC 15.2, cmake 4.2.3 + Ninja, the system's SDL
  3.4.2): `hotd`, `hotd_viewer`, `hotd_nettest`, `hotd_proof` build and run. Headless on Linux, the same numbers as
  Windows:
  | check | Linux | Windows |
  |---|---|---|
  | `--input-test` | 1577 checks, 0 failed | 1577 checks, 0 failed |
  | Chapter 1, `--frames 40000 --autofire --mute --lives-kept 1` (arcade / `--models pc`) | ended at 16695 / 16695 | 16695 / 16695 |
  | Chapter 4, `--chapter 4 --strict --autofire --lives-kept 1 --mute --frames 60000 --default-route` (both models) | the ending at 34967 / 34967 | 34967 / 34967 |
  | `hotd_nettest` | all passed | all passed |
  | `hotd_proof pmmaths` | 0 differ (102 routines, 2,065,600 cases) | 0 differ |
  | `--render --shot-at 2500` (WSLg's OpenGL) | the picture, 0 GL errors, ~10 ms a frame | |
- **Online play on every system with one protocol (cross-play)**: protocol 10 uses Monocypher on Windows, Linux and
  macOS (section "Crypto"). A Linux host and a Windows joiner played Chapter 1 + 2 (40,000 frames, 2 players): both
  `result=ok`, 40,000 hashes compared, the same final hash as two Windows PCs.
- **A Linux package** (`tools/make_linux_package.sh`, section "SteamOS package"): `bin/hotd` with its own
  `libSDL3.so.0` (rpath `$ORIGIN`), `hotd.sh`. Checked: the packaged game loads the bundled SDL and plays Chapter 1
  to 16695.
- Earlier (worker "linuxport"): everything compiles with `-Wall -Wextra`; `hotd_proof` on the 4 MAME captures is
  identical on Linux and Windows; the C library maths the game uses are bit-identical (section "Cross-play").
- **Not tested yet:** a real Steam Deck / desktop Linux PC (the window, pads, gyro, light guns, sound devices), a
  build in the Steam Runtime (the WSL build needs glibc 2.43), UPnP on Linux, macOS.

## Inventory of the Windows-only parts

| File | What is Windows-only | Portable replacement | State | Effort left |
|---|---|---|---|---|
| `src/net/socket.cpp` | Winsock UDP | BSD sockets behind the same class | **done before** (both in the file) | none |
| `src/net/upnp.cpp` | UPnP port mapping through Windows' COM (`NATUPnP`) | our own SSDP + SOAP client (UDP multicast + a small HTTP POST), no library | Linux / macOS: "forward the port yourself" message | medium (1 session) |
| `src/net/crypto_win.cpp` (gone) | CNG: ECDH P-256, CPace, HMAC-SHA256, AES-256-GCM, SHA-256, random | Monocypher on every system (section "Crypto") | **done (linux2)**: `crypto.cpp`, protocol 10, cross-play checked | none |
| `src/util/crash_log.cpp` | SEH filter + DbgHelp stack walk, `MessageBox`, `_wfopen` | **POSIX signal handler on its own stack** (SIGSEGV / SIGBUS / SIGILL / SIGFPE), `backtrace` + `dladdr` (module + offset), SDL's message box | **done (this branch)** | none |
| `src/game/bot.cpp` | the robustness runs' SEH crash line | the same `ROBUST: result=crash` line from a signal handler | **done (this branch)** | none |
| `src/util/win_shell.cpp` | desktop / Start menu `.lnk` (COM `IShellLink`), `GetModuleFileName` | **Linux: a `.desktop` file** on the Desktop and in `~/.local/share/applications`, icon = your THOTD.EXE's icon as a PNG in `~/.local/share/icons`; **own path: `/proc/self/exe` (Linux), `_NSGetExecutablePath` (macOS)** | **done (this branch)**; macOS shortcut: says "not supported" | macOS: small |
| `src/util/steam_shortcut.cpp` | Steam folder from the registry; "is Steam running" via Toolhelp | **Linux: `~/.steam/steam`, `~/.local/share/Steam`, Flatpak, Snap; macOS: `~/Library/Application Support/Steam`; running: `/proc/*/comm` = `steam` (Linux), `libproc` `steam_osx` (macOS)**. The `shortcuts.vdf` / grid code was already portable | **done (this branch)** | none |
| `src/main.cpp` | raw input (`WM_INPUT`) to tell mice / light guns apart; process memory report | SDL3 already tells mice apart on Linux (X11 XInput2 / Wayland): the events decide; memory report says "unknown" | compiles; mice / guns untested on Linux | test on real hardware |
| `src/game/lightguns.cpp` | light guns through raw input (absolute mice) | Linux: guns are absolute evdev devices; SDL3 shows them as mice with absolute motion. Needs testing with a real gun (Sinden / GUN4IR) | Windows-only path kept | medium |
| `src/assets/archive.cpp`, `pc_files.cpp`, `game/files_check.cpp`, `proof/main.cpp` | `_fseeki64` / `_ftelli64` (MSVC) | `util/file64.h`: `fseeko` / `ftello` elsewhere | **done (this branch)** | none |
| `src/assets/pc_files.cpp` | file names match whatever their case on Windows | **Linux: a missing PC file path is looked up a part at a time ignoring case** (`ART/STAGE1/...` finds `art/stage1/...`) | **done (this branch)**, tested in WSL | none |
| `src/proof/main.cpp` | memory-mapped captures (`CreateFileMapping`) | `mmap` | **done (this branch)** | none |
| `src/main.cpp`, `src/viewer/main.cpp` | `localtime_s` | `localtime_r` | **done** | none |
| `src/game/audio.cpp` | (all builds) a build without the arcade sound board did not compile (`unique_ptr` of an incomplete type) | an empty stand-in class | **done** | none |
| `src/game/pads.cpp` | `std::max(uint64_t, unsigned long long)` (the same type on Windows only) | `std::max<uint64_t>` | **done** | none |
| `src/res/*.rc`, `hotd.ico`, manifest | the exe icon, version info | Linux: the `.desktop` file's icon; macOS: an `.app` bundle's `.icns` | CMake already adds the `.rc` on Windows only | macOS: small |
| `CMakeLists.txt` | `ws2_32 bcrypt ole32 shell32 uuid`, `/MAP`, the `.rc`, SDL3.dll copy | already inside `if(WIN32)` / `if(MSVC)`; **added: Threads, `${CMAKE_DL_LIBS}`, rpath `$ORIGIN` (`@loader_path` on macOS), `-Wno-unused-parameter`** | **done** | none |
| settings / cache folder | `SDL_GetPrefPath` | SDL already uses `~/.local/share/hotd-rewrite/hotd/` on Linux (XDG_DATA_HOME) and `~/Library/Application Support/hotd-rewrite/hotd/` on macOS; a portable package keeps them in its own `settings/` (unchanged) | nothing to do | none |
| file dialogs (setup "Folder... / Zip...") | `SDL_ShowOpenFolderDialog` | works on Linux desktops through xdg-desktop-portal or zenity; **not in the Steam Deck's Game Mode**: there the files go in the package's `data/` folders (the game finds them itself) | nothing to do | none |
| `tools/*.ps1`, `*.bat` (HOTDReWritheSetup.bat, `make_portable*.ps1`, `netplay*_test.ps1`, `package_hashes.ps1`, ...) | PowerShell | the developer tools stay Windows-only; a Linux package needs a `make_linux_package.sh` and `hotd.sh` launch script (section "SteamOS package") | **`tools/make_linux_package.sh` done (linux2)**; the netplay test scripts stay PowerShell | none |
| `HOTDLauncher.exe` / HOTDReWritheSetup.bat busy checks (Get-Process) | PowerShell | the game itself needs none on Linux | n/a | none |
| `src/util/update_check.cpp` (v1.1.0 auto-updater) | WinHTTP download, BCrypt SHA-256, hand-over to `hotd_launch.exe --update` | **the check runs on Linux / macOS through the system's `curl`** (GitHub's API, a constant address); the Update page shows the version and notes with **Download page** only (`util::kSelfUpdate` false: no self-install) | **done (linux, v1.1.x)** | a self-update for the tarball: later, if wanted |
| `src/game/installer.cpp` (setup2) | Apps & features (registry), `.lnk` shortcuts, `bin\hotd.exe` | **a folder install** (`~/Games/HOTD ReWrithe` by default) with `.desktop` entries (Desktop + `~/.local/share/applications`, icon = THOTD.EXE's icon as `hotd-rewrithe.png` in the install), `bin/hotd`; uninstall removes them (`--uninstall`); no registry. macOS: the shortcut boxes are greyed out | **done (linux)** | macOS: an .app per install |
| `src/game/launcher.cpp` (setup .7z / .rar) | Windows' own `tar.exe` (libarchive) | macOS: `/usr/bin/tar` (bsdtar); Linux: `bsdtar` (libarchive-tools), else `7zz` / `7z` / `7za`; none: a clear line ("install bsdtar or 7-Zip, or unpack it yourself") | **done (linux)** | none |
| `src/util/win_shell.cpp` `launch` / `runHidden` | `CreateProcessW` | POSIX `fork` + `execv` (no shell; Windows-style quoted arguments split), detached / with a time limit; `findProgram` (PATH) | **done (linux)** | none |
| `src/util/game_exe.cpp`, `src/launch/launch_main.cpp` (`hotd_launch`) | the game exe stub with the PC icon, the updater | not needed: the `.desktop` entry has the icon; `hotd_launch` is built on Windows only | n/a | none |
| `src/util/disc_image.cpp`, `util/file64.h` (CD images, `\\?\` long paths) | `_wfopen`, long paths | `fopen` / `fseeko` (no length limit on Linux / macOS) | compiles and runs (the file64 layer) | none |

## Cross-play: the game logic gives the same results on Linux

Online play is lockstep: both PCs must compute the same game. Checked on this branch:

- `hotd_proof` on 4 MAME captures: Linux (GCC 15, `-ffp-contract=off -fno-fast-math`) and Windows (MSVC, `/fp:precise`)
  print identical results (0 differ on both).
- The only C library maths the game logic uses: the sine / cosine and arctangent tables (`port/pm_maths.h`) and the
  dogs' `log` (`port/enemy_dogs.cpp`). A test program hashed the tables and `log` for **all 222,313,874 floats from
  0.001 to 100,000** (both forms the dogs use): MSVC's UCRT and Linux's glibc give **bit-identical** results.
- **Checked (linux2):** `hotd` on Linux ends Chapter 1 at 16695 (both models) and Chapter 4's run at 34967, the
  Windows numbers; a Linux host + Windows joiner game (40,000 frames, 2 players) never desynced and ended with the same
  hash as a Windows + Windows game (3ddee74c4007301f).
- **Cross-play over WSL, which direction:** the Linux host / Windows joiner direction was run. The other one (a
  Windows host, a WSL joiner) was not: WSL 2's NAT puts Linux behind a virtual switch, so the joiner's packets arrive at
  the Windows host from outside, and Windows Firewall has no rule for the worktree's new hotd.exe: binding a
  non-loopback UDP port would pop up the "allow access" dialog on the desktop (NotifyOnListen is on), which a test must
  not do, and adding a rule is a security setting. The handshake and the packets are the same in both directions (the
  host / joiner roles only swap who sends HELLO), so this direction adds little; on two real PCs both work alike.

## Crypto (online play)

**Done (linux2, protocol 10): Monocypher 4.0.3 on every system, Windows too** (`src/net/crypto.cpp`,
`src/third_party/monocypher/`). The design, the primitives and the tests: docs/netplay.md "Security". The CNG code
(`crypto_win.cpp`, `p256.cpp`) and the stand-in that refused online play (`crypto_none.cpp`) are gone.

The choice that was made (linuxport's comparison):

| Option | Primitives | Licence | Notes |
|---|---|---|---|
| **Monocypher 4.0.2** (recommended) | X25519, Elligator 2 (hash to curve: exactly what CPace on X25519 needs), XChaCha20-Poly1305 AEAD, BLAKE2b, SHA-512 / HMAC-SHA-512 | CC0 or BSD-2-Clause (your choice) | 2 files (`monocypher.c/.h`, about 3,000 lines), audited by Cure53 (2020), used widely; can be committed in `src/third_party/monocypher/` |
| libsodium 1.0.20 | X25519, XChaCha20-Poly1305, BLAKE2b, SHA-512 (+ AES-GCM only on CPUs with AES-NI) | ISC | bigger (a library to build per system), also well audited |
| keep P-256 + AES-GCM | needs a portable P-256 + AES-GCM library (e.g. BearSSL, MIT; Mbed TLS, Apache-2.0) | | bigger, more to get wrong; no reason to keep P-256 |

As built (all systems, Windows too): protocol 10; CPace-X25519 (generator = Elligator 2 map of BLAKE2b of the
password and the protocol's label; no session id: the nonces in the transcript make every session's keys fresh, and
the host makes a new key pair for every HELLO); keys = BLAKE2b-512 over a label, the shared secret and the transcript,
one half per direction; every packet XChaCha20-Poly1305 with the header as associated data and the same 64-bit
sequence numbers and replay window as before. Random bytes from the OS (Windows `BCryptGenRandom`, Linux `getrandom`,
macOS `arc4random_buf`). Passed: `hotd_nettest` (Windows and Linux, with the known-answer tests and the fuzz),
`netplay_test.ps1 -HostArgs "--players 2"`, `netplay4_test.ps1`, and a Linux <-> Windows game.

## Steam Deck (SteamOS)

- **Screen 1280 x 800 (16:10):** rendered on Windows at 1280 x 800: the main menu, Options, all six settings tabs, a
  Controller page, High Scores, Chapter Select, Co-op, and the HUD in a 2-player game (frames 2500 / 6000 / 8800).
  Everything fits: the menus are 4:3 with side bars, the 3D view simply shows a little more at the top and bottom,
  the HUD (bullets, lives, 1P / 2P) sits in the corners as at 16:9. Nothing needed fixing.
- **Controls:** the Deck's controls reach the game as an SDL gamepad (through Steam Input: an Xbox-style pad). The
  existing pad support applies (stick aim, fine aim, trigger = R2). Gyro: Steam Input passes the Deck's gyro to games
  as a mouse or a pad's motion sensor, depending on the controller layout; the game's gyro aim needs the motion
  sensor (Steam's "Gamepad with gyro" layout) - to test on a Deck. The trackpads work as a mouse.
- **Touch screen as a light gun:** already works through SDL's touch-to-mouse events (a tap = a shot where you touch;
  the code treats touches as absolute pointer positions). Reloading needs a shot off the screen, which a touch can't
  do: turn on Options -> Auto Reload (or use a back button mapped to the right mouse button in Steam Input).
- **Suspend / resume:** the game loop drops a long stall instead of speeding up afterwards (no fast-forward after
  waking). A netplay game will time out during a suspend (as with a network loss). Idea for later: on a long stall,
  clamp the pad aim's time step (today a stick held while suspending moves the aim once by stick speed x the gap;
  harmless, it is clamped to the screen).
- **Files:** in Game Mode there is no file dialog. A package finds the files in its `data/arcade` and `data/pc`
  folders by itself (as on Windows); PC file names in any case work (`art/stage1/...` too).
- **Add to Steam:** works with the Linux Steam folders, but Steam must not be running (it rewrites the list when it
  exits): on a Deck, switch to Desktop Mode, exit Steam, press Add to Steam, start Game Mode again. Adding it by hand
  in Desktop Mode (Steam -> Add a Non-Steam Game) works too.
- **Fullscreen:** the game starts borderless fullscreen (Gamescope gives it the whole screen).
- **The setup window with a pad** (linux, 2026-11-03): A / Start = Enter (the files known: straight to the check and
  the menu), B / Back = Esc, d-pad Up / Down scroll the Update / What's New text; the rest is the touch screen or the
  trackpad (the mouse). The Update notice offers the download page (no self-update on Linux).
- **Adding it as a non-Steam game:** Desktop Mode -> Steam -> Games -> Add a Non-Steam Game to My Library -> Browse
  -> the package's `hotd.sh` (File type: All files) or `bin/hotd` -> Add Selected Programs; back in Game Mode it is
  under Non-Steam. Or the game's own Options -> System -> Add to Steam with Steam closed (artwork included).

### SteamOS package (`tools/make_linux_package.sh`, linux2)

```
HOTD ReWrithe Linux/
  hotd.sh, hotd_viewer.sh launch scripts: cd "$(dirname "$0")"; exec ./bin/hotd "$@"
  hotd_package.txt        (as the Windows package: the game keeps settings/ and finds data/ itself)
  bin/hotd, bin/hotd_viewer   built with rpath $ORIGIN (stripped)
  bin/libSDL3.so.0        the SDL3 the build linked (copied from where the loader finds it)
  data/arcade  data/pc  data/music  data/mods  data/steam/<kinds>   (empty, a note in each: the player's own files)
  README.txt (the Steam Deck steps), LICENSE.txt, NOTICE.txt, ONLINE HOWTO.txt
```

`tools/make_linux_package.sh [-b build-linux] [-o <folder>] [--no-build] [--no-tar]`: default output
`<main checkout>/private/portable/HOTD ReWrithe Linux` plus `hotd-teaser-<date>-linux-x86_64.tar.gz` and
`SHA256SUMS-linux.txt` beside it. Checks: no game data, the packaged `bin/hotd` loads the bundled `libSDL3.so.0`
(not the system's), no build paths in the binaries; prints the newest glibc the binaries need. Tested in WSL: the
package's `hotd.sh` loaded its own SDL and played Chapter 1 to 16695.

- **Minimum: glibc 2.31** (the package needs GLIBC_2.29 symbols at most; tested on 2.31 and 2.43). Built in the
  **Steam Runtime 3 "sniper" SDK** (Debian 11, glibc 2.31; the CI's Linux job and, locally,
  `podman run ... registry.gitlab.steamos.cloud/steamrt/sniper/sdk` with the newest `g++-N` it has: gcc-14 since
  2026; it no longer ships gcc-12) with SDL 3.2.10 built from source. So it runs on SteamOS 3, Ubuntu 20.04 / 22.04 /
  24.04, Linux Mint 20 / 21 / 22, Debian 11+, current Fedora and Arch. A build made directly on a newer distribution
  (WSL's Ubuntu 26.04) needs that distribution's glibc (2.43): never release one.
- The script takes the build's own `_deps/sdl3-build/libSDL3.so.0` (HOTD_FETCH_SDL3) before what `ldd` finds: on a
  machine with a system libSDL3 (e.g. 3.4 from the distribution) ldd would pick that one and the package would carry
  the wrong SDL.
- The exe's RUNPATH is `$ORIGIN:<build>/_deps/sdl3-build` (CMake's build rpath; `$ORIGIN` first, the second entry does
  not exist on a player's machine): harmless.
- SDL's own Linux backends (X11, Wayland, PipeWire / PulseAudio / ALSA, evdev, HIDAPI for pads) load at run time;
  the bundled SDL avoids SteamOS's varying system version.
- OpenGL 3.3: SteamOS's Mesa (RADV / radeonsi) supports it.

## macOS (supplementary)

- Compiles in principle (the POSIX paths are shared with Linux; `_NSGetExecutablePath`, `libproc` for Steam, rpath
  `@loader_path`; random bytes from `arc4random_buf`). No Objective-C / Objective-C++ of our own (SDL has its own):
  plain C++20 with Apple clang and libc++. Checked without a Mac (linux2, 2026-11-03): clang 21 + libc++ in WSL build
  the game with no errors and give the Linux / Windows numbers; the first real compile is the CI's macOS job
  (`macos-latest`, universal arm64 + x86_64, deployment target 11.0). Not run on a Mac yet.
- OpenGL on macOS stops at 4.1 core (deprecated but working): our renderer asks for 3.3 core, which SDL's Cocoa backend
  turns into a forward-compatible core profile: should be fine (to see on a Mac).
- The CI package: `bin/hotd` + `libSDL3.0.dylib` (ad-hoc signed), `hotd.command`, `hotd-setup.command`, a small
  `HOTD ReWrithe.app` whose launcher starts `../bin/hotd`, `HOW TO OPEN.txt`. Unsigned (no Developer ID): a downloaded
  copy is quarantined and the .app would run from a random read-only copy (App Translocation) without its `bin/`; the
  launcher then shows an alert with the fix (`xattr -dr com.apple.quarantine <folder>`), README "Linux, Steam Deck and
  macOS" has the steps (right-click > Open; macOS 15+: Privacy & Security > Open Anyway).
- Missing: an `.icns` icon, code signing / notarisation, the desktop shortcut (says "not supported"), self-update.

## Experimental modes

The user (2026-11-03): **the unreleased modes stay out of the public releases until they are polished.** Survival Mode ships
(v1.2.0 labelled it "Very Experimental / Proof of Concept"; removed since, user 2026-11-05). **The Typing of the Dead 0
ships since v1.3.0** (user 2026-11-05, rel130), labelled "Experimental / Proof of Concept": its More Modes item's tag
line, its help line, a notice at a game's start (`Hud::drawTotdNote`). Three CMake options:

- **`HOTD_EXPERIMENTAL_MODES`** (default **ON**: every development build, `build\Release` and the private portable):
  the unreleased modes (and The Typing of the Dead 0, Survival and all Survival arenas with them).
- **`HOTD_TOTD_MODE`** (default **ON**): The Typing of the Dead 0 in a build with the experimental modes OFF (the
  release packages). OFF as well: it is left out.
- **`HOTD_SURVIVAL_MODE`** (default **ON**): Survival Mode (Chapter 1's courtyard only) in a build with the experimental
  modes OFF. OFF as well: Survival is left out.

| | ON (development) | OFF + `HOTD_TOTD_MODE=ON` + `HOTD_SURVIVAL_MODE=ON` (every release package) |
|---|---|---|
| More Modes | The Typing of the Dead 0, Survival, Survival Arena (all arenas), the unreleased modes | Chapter Select, Co-op, Sonic Mode, The Typing of the Dead 0 (labelled), Survival, Survival Arena (the courtyard) |
| High Scores | the Typing of the Dead 0 tables, Survival, the unreleased modes' | the Typing of the Dead 0 tables, Survival |
| Command line | `--totd*`, `--typing`, `--survival*`, the unreleased modes' | `--totd*`, `--typing`, `--survival*` |

The unreleased modes' code is not compiled at all with the experimental modes OFF (user 2026-11-05): it lives in
`src/unreleased/` and every use of it is inside `#if HOTD_UNRELEASED` (CMake: ON only with the experimental modes ON
and that folder present); their options are unknown options there. Their language lines are each file's
`# [unreleased]` .. `# [/unreleased]` section; `tools\export_public.ps1` leaves out the folder, those blocks and
sections (the public source builds without them). Built OFF (TotD 0 and Survival ON) by:
`tools\make_portable_hotd.ps1 -Release` (its own `build-release`; it checks that nothing of `src/unreleased` is
compiled and `--totd-test` is accepted), `.github/workflows/release.yml` (Windows, Linux, macOS),
`tools/make_linux_package.sh` (stops on a build folder configured ON unless `--experimental`),
`tools\make_quest_apk.ps1` (`-Experimental` for a testing APK). The packages strip the `[unreleased]` language
sections; `data\typing` ships. The release checklist:
[release/build_release.md](release/build_release.md) section 0.

## What needs your approval (downloads / installs)

Done (approved 2026-10-20): WSL got `build-essential cmake ninja-build libsdl3-dev` from Ubuntu's archive; Monocypher
4.0.3 was downloaded from monocypher.org (SHA-512 and BLAKE2b checked against the site) and is committed in
`src/third_party/monocypher/`. Still open:

1. Done: the Steam Runtime sniper SDK image is in WSL's podman (used by linux2); clang 21 + libc++ from Ubuntu's
   archive in WSL (linux2, 2026-11-03: the macOS-like compile check).
2. A Mac for running the macOS build (the CI's runner compiles and packages it).
3. Optional, for the Windows-host direction of the WSL cross-play test: a Windows Firewall rule for the test exe
   (or WSL's mirrored networking). Two real PCs need neither.

## Linux build

```
D=<main checkout>/private/deps
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DHOTD_ARCADE_SOUND=ON \
      -DHOTD_EXPERIMENTAL_MODES=OFF -DHOTD_SURVIVAL_MODE=ON \
      -DHOTD_DR_LIBS_DIR=$D/dr_libs -DHOTD_MUSASHI_DIR=$D/musashi -DHOTD_SCSP_DIR=$D/mame_scsp -DHOTD_STB_DIR=$D/stb
cmake --build build-linux
HOTD_PREF_DIR=<a test folder> ./build-linux/hotd --root <checkout> --arcade <hotd.zip> --pc <PC folder> \
      --frames 40000 --autofire --mute --lives-kept 1        # says: [runtime 16695] chapter 1 ended
./build-linux/hotd_nettest                                   # ALL PASSED (0 failed)
```

Headless runs need no display; `--render` and window runs in WSL use WSLg (Windows 11). A fresh `HOTD_PREF_DIR`
needs `--root`, `--arcade` and `--pc` (else the input test waits in the launcher). Cross-play test: host in WSL with
`--host <port> --code X --players 2`, join from Windows with `--join <WSL's eth0 address>:<port> --code X`.

Release-style build in the sniper SDK (as the CI; podman in WSL, the sources mounted read-only where possible):
```
podman run --rm -v "$PWD:/work/src" -v "$D:/work/deps:ro" -v "$HOME/hotd-sniper:/work/build" -w /work/src \
  registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest bash -c '
  cmake -S . -B /work/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
    -DHOTD_FETCH_SDL3=ON -DHOTD_EXPERIMENTAL_MODES=OFF -DHOTD_SURVIVAL_MODE=ON -DHOTD_ARCADE_SOUND=ON -DHOTD_STB_DIR=/work/deps/stb \
    -DHOTD_DR_LIBS_DIR=/work/deps/dr_libs -DHOTD_MUSASHI_DIR=/work/deps/musashi -DHOTD_SCSP_DIR=/work/deps/mame_scsp &&
  cmake --build /work/build --target hotd hotd_viewer hotd_nettest &&
  tools/make_linux_package.sh -b /work/build -o "/work/build/pkg/HOTD ReWrithe Linux" --no-build'
```
Tests of that build on the WSL host: `LD_LIBRARY_PATH=$HOME/hotd-sniper/_deps/sdl3-build` (the built SDL 3.2.10;
without it the loader takes the system's libSDL3, see "Where it stands").

## CI without a release

`.github/workflows/release.yml` builds and packages Windows, Linux / SteamOS and macOS on GitHub's machines and uploads
the packages as **artifacts** of the run; it never makes a GitHub Release. It runs:
- on a version tag (`v*`): the release builds;
- **by hand**: GitHub -> the repository -> **Actions** -> **release** (left) -> **Run workflow** (right) -> Branch:
  `main` (or any) -> "Which packages to build": `all`, `linux-macos`, `linux`, `macos` or `windows` -> **Run workflow**;
- on a **push to a branch named `ci`** (all three): in GitHub Desktop: Branch -> New branch `ci` from main -> Publish
  branch (later: switch to `ci`, Branch -> Update from main, Push origin).

A newer run on the same branch cancels the older one. When it is done (Linux ~10 min, macOS ~15 min, Windows ~25 min):
open the run -> **Summary** -> **Artifacts** at the bottom: `linux-x86_64` (the `.tar.gz` + `SHA256SUMS-linux.txt`),
`macos-universal` (the `.zip` + `SHA256SUMS-macos.txt`), `windows-x64`. GitHub wraps each in a `.zip` (unzip once);
artifacts are kept 90 days. A red job: click it, the failing step shows the compiler / test output.

Every job builds with `-DHOTD_EXPERIMENTAL_MODES=OFF -DHOTD_SURVIVAL_MODE=ON` and checks that `--totd` is refused. The Linux job also
unpacks its own tarball into a fresh folder and runs `--version` / `--mods-test` from it with only the bundled SDL; the
macOS job checks the `@rpath` SDL and runs the packaged exe and the .app's launcher.
