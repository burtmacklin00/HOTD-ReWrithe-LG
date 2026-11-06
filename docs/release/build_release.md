# Building the release package (exact commands)

Dry run of 2026-10-16 (branch `claude/package`, main at c7ac870): Visual Studio 17 2022 Build Tools, CMake, Windows 11.
Run from the repo root in PowerShell. The package is made OUTSIDE the repo; nothing from `private\` goes in.

## 0. Experimental modes OFF in every public package (user 2026-11-03)

The Typing of the Dead 0 ships since v1.3.0, labelled "Experimental / Proof of Concept"; the unreleased modes are **not**
in a public release until they are polished; Survival Mode **is** (user 2026-11-03; courtyard only). Every
release build is configured with **`-DHOTD_EXPERIMENTAL_MODES=OFF -DHOTD_TOTD_MODE=ON -DHOTD_SURVIVAL_MODE=ON`** ([porting.md](../porting.md#experimental-modes)):

- [ ] Windows: `tools\make_portable_hotd.ps1 -Release` (its own `build-release` folder, configured OFF; it stops if the
      cache says ON and checks that nothing of `src/unreleased` is compiled and the exe accepts `--totd-test`). A hand-made build folder: add the flag to its configure.
- [ ] GitHub release workflow (`.github/workflows/release.yml`): all three configures carry the flag; its tests check the
      TotD 0 option on Windows, Linux and macOS.
- [ ] Linux / SteamOS: configure `build-linux` with the flag; `tools/make_linux_package.sh` stops on a build without it.
- [ ] Quest APK: `tools\make_quest_apk.ps1` configures OFF (`-Experimental` only for a testing APK).
- [ ] From the package: `bin\hotd.exe --survival courtyard` prints "... not in this version ..." (exit 2); More Modes
      shows The Typing of the Dead 0 (with its Experimental / Proof of Concept line) and Survival and nothing else; High Scores has no other mode's table.

The user's own builds (`build\Release`, the private portable from `make_portable_hotd.ps1` without `-Release`) keep them ON.

## 1. Configure a clean Release build with music

SDL3 and stb_vorbis are not downloaded by the build unless asked. Two ways:

```
# a) offline (what the dry run used): SDL3 source and stb already on disk
cmake -S . -B build-package -G "Visual Studio 17 2022" -A x64 -DHOTD_FETCH_SDL3=ON -DHOTD_EXPERIMENTAL_MODES=OFF -DHOTD_TOTD_MODE=ON -DHOTD_SURVIVAL_MODE=ON ^
  "-DFETCHCONTENT_SOURCE_DIR_SDL3=<folder of the SDL3 release-3.2.10 source>" "-DHOTD_STB_DIR=<folder with stb_vorbis.c>"

# b) on a machine with internet: SDL3 (tag release-3.2.10) and nothings/stb are fetched from GitHub
cmake -S . -B build-package -G "Visual Studio 17 2022" -A x64 -DHOTD_FETCH_SDL3=ON -DHOTD_FETCH_STB=ON -DHOTD_EXPERIMENTAL_MODES=OFF -DHOTD_TOTD_MODE=ON -DHOTD_SURVIVAL_MODE=ON
```

The configure output must contain `OGG music: stb_vorbis from <folder>`. If it says `OGG music: off` the package has no music.
(Without `-DHOTD_STB_DIR` / `-DHOTD_FETCH_STB=ON` a fresh configure builds WITHOUT music; make_portable_hotd.ps1 warns.)

## 2. Build and package

```
powershell -ExecutionPolicy Bypass -File tools\make_portable_hotd.ps1 -Release -Build build-package -Out "<scratch folder>"
```

(`-Release` refuses a build folder whose cache has `HOTD_EXPERIMENTAL_MODES` on, and checks that nothing of `src/unreleased` is compiled and the exe accepts `--totd-test`.)

Since 2026-10-20 (firstrun) the script also checks the exe (VERSIONINFO, asInvoker manifest, no build paths such as
`C:\Users`, a warning when the build was not configured with `-DHOTD_LOCAL_ICON=OFF`, no game-data files in the folder)
and writes `hotd-teaser-<date>-win64.zip` and `SHA256SUMS.txt` (tools\package_hashes.ps1) next to the `-Out` folder
(`-NoZip` skips them). Use a dated parent folder, e.g. `-Out "<...>\private\release\<date>\HOTD ReWrithe"`.

It builds `hotd` in Release, copies the program and the MSVC runtime, writes `HOTDReWritheSetup.bat`, `README.txt`, `hotd_package.txt`.
Dry run: 10 files, 5.31 MB (hotd.exe 2.1 MB, SDL3.dll 2.4 MB, MSVC runtime 1.0 MB). Files: `bin\hotd.exe`, `bin\SDL3.dll`,
`bin\msvcp140.dll`, `msvcp140_1.dll`, `msvcp140_2.dll`, `vcruntime140.dll`, `vcruntime140_1.dll`, `HOTDReWritheSetup.bat`, `README.txt`,
`hotd_package.txt`. No ROM, zip, ogg, vmc, rbh or any other game data; no `private\` path.

## 3. Checks from the package (needs the ROM zip and PC game remembered in launcher.ini, or --arcade / --pc)

```
bin\hotd.exe --check-files                                   # exit 0; arcade 29/29, PC 156/156 + 12 music tracks
bin\hotd.exe --frames 20000 --autofire --mute --lives-kept 1 --models arcade   # exit 0, chapter clear at runtime frame 16228
bin\hotd.exe --frames 20000 --autofire --mute --lives-kept 1 --models pc       # exit 0, same
bin\hotd.exe --frames 3000 --autofire --audio-wav <temp>.wav                    # exit 0; no "music:" failure notes
bin\hotd.exe --input-test <results.txt>                                         # hidden window; "408 checks, 0 failed"
```

Music: the 3000-frame wav (4.3 M samples, 44.1 kHz stereo, no audio device) peaks between 12,600 and 32,700 in every 5 s
window, i.e. the looping music track is decoded and mixed all the time; no "built without stb_vorbis" or "cannot open" note.

## Distribution and bug reports (decided 2026-10-16)

- **Source:** the public copy made by `tools\export_public.ps1 -Out <folder>` (one commit, no journal) is pushed as a
  new GitHub repository. Publishing (creating the repository, pushing, making it public) is done by the user.
- **Program:** the portable package from step 2, zipped (`hotd-teaser-<date>-win64.zip`, ~2.5 MB zipped), attached
  to a GitHub Release of that repository, with docs/release/teaser_release_notes.md as the release text. No game data.
- **Bug reports:** GitHub Issues of that repository, template `.github/ISSUE_TEMPLATE/bug_report.md` (no ROMs or game
  files in reports).
- **Clean-machine check (2026-10-16):** no second PC / Windows Sandbox here, so the package was run from a scratch
  folder with an empty profile (APPDATA / LOCALAPPDATA / USERPROFILE / TEMP in a new folder), PATH = System32 only, and
  the ROM zip renamed into a folder with spaces: `--check-files` OK (arcade, PC), headless Chapter 1 cleared with
  arcade and PC models (frame 16,710), blood red by default. Still worth one run on a second PC (GPU drivers, no
  MSVC runtime installed system-wide: the package carries its own).

## Icon, resources and the packaging check (2026-10-16)

- **Icons.** The public build embeds only our own neutral icon (`src/res/hotd.ico`, written by `tools/make_neutral_icon.py`;
  not derived from any game art). A local build from source whose CMake finds a PC folder (`private/pcharness/game/THOTD.EXE`,
  `%HOTD_PC_DIR%\THOTD.EXE`, or `-DHOTD_LOCAL_ICON=<your THOTD.EXE>`) extracts that icon into the build folder and links it;
  that exe is for the player's own machine only. **Build the release package with `-DHOTD_LOCAL_ICON=OFF`** (or from a checkout
  with no `private\` folder, as the export does). At run time the window icon is read from the player's THOTD.EXE; the
  shortcut (`hotd --create-shortcut`, or Options -> System -> Desktop Shortcut) points its IconLocation at THOTD.EXE. No exe is ever modified.
- **Resources.** hotd.exe and hotd_viewer.exe carry a VERSIONINFO (ProductName "The House of the Dead: ReWrithe", no Sega branding) and an
  `asInvoker` manifest (no administrator rights).
- **Packaging check** (run it on the scratch package before zipping):
  - no hook DLLs, no process injection in the package (the PC harness's `hotd_hook` is dev-only and not in `bin\`);
  - no packer / UPX, no runtime downloads (the program only reads local files; SDL3 and stb are fetched at build time only);
  - settings and remembered file-check results live in `%APPDATA%\hotd-rewrite\hotd\` (SDL pref path), not the program folder;
  - the package icon check: `(Get-Item bin\hotd.exe).VersionInfo` shows the product name, and the exe contains no THOTD icon.
- **Signing: decided, none.** The release stays unsigned, so Windows SmartScreen may warn on first launch (README, "First launch").
  Instead the release notes list the SHA-256 of the zip and of every package file: print them with
  `powershell -ExecutionPolicy Bypass -File tools\package_hashes.ps1 -Package "<package folder>" -Zip "<zip>"`.

## v1.0.0: version, package and CI (relprep, 2026-10-27)

- **The version** lives in one place: `project(hotd_m2 VERSION 1.0.0 ...)` in CMakeLists.txt. It feeds the exes'
  VERSIONINFO, `generated/hotd_version.h` (`HOTD_VERSION_TEXT` = "v1.0.0": the main menu's corner and `hotd --version`),
  the package README's title and the zip / tar names (`hotd-rewrithe-v1.0.0-win64.zip`, `...-linux-x86_64.tar.gz`).
- **Windows dry run** (build folder configured with `-DHOTD_LOCAL_ICON=OFF`, `make_portable_hotd.ps1 -NoBuild`):
  761 files, 21.8 MB, zip 13.5 MB (now made with Windows' tar.exe: forward slashes, so Linux / macOS unzip tools read it;
  Compress-Archive wrote backslashes). bin\ (hotd.exe 7.4 MB, hotd_viewer.exe, SDL3.dll, 5 MSVC runtime DLLs), data\ui
  (prompts + both CC0 licences, wordmark), data\fonts (4 Noto fonts + OFL.txt), data\lang (5), data\mods (2 mod.ini),
  the empty data\arcade / pc / music / steam folders with notes, HOTDReWritheSetup.bat, Play (Safe Mode).bat, Model Viewer.bat, README.txt, LICENSE.txt,
  NOTICE.txt, ONLINE HOWTO.txt. No game data, nothing from private\. The CRT DLLs are now found through vswhere too
  (any Visual Studio 2022, e.g. a GitHub runner's Enterprise), not only the Build Tools folder.
- **Linux package**: `make_linux_package.sh` now copies data\ui / fonts / lang / mods like the Windows script (before,
  the Linux package had no fonts, translations or prompt icons).
- **CI** (`.github/workflows/release.yml`, not run yet): on a tag `v*` (`git tag v1.0.0`, `git push origin v1.0.0`) or by
  hand. Three jobs: Windows x64 (windows-2022, VS 2022), Linux x64 = the SteamOS build (in Valve's Steam Runtime
  "sniper" SDK container, glibc 2.31, gcc-12), macOS universal (macos-14, arm64 + x86_64, deployment target 11.0,
  ad-hoc signed, a folder with `bin/` + a small `HOTD ReWrithe.app` that starts `bin/hotd`). Each fetches the pinned
  third-party sources (SDL3 release-3.2.10, stb, dr_mp3, Musashi without its FPU / MMU files, MAME mame0289's SCSP),
  runs `hotd --version`, `hotd --mods-test` and `hotd_nettest`, then `hotd --gl-check` on Mesa's software driver
  (compat, 2026-10-29: Windows downloads mesa-dist-win 26.2.4 and puts its `opengl32.dll` next to a copy of hotd.exe,
  also as a plain OpenGL 3.3 driver; Linux installs the distribution's Mesa and Xvfb, that step `continue-on-error`),
  packages, and uploads the zip / tar.gz and SHA-256
  sums as artifacts. Nothing is published: make the GitHub Release by hand from the artifacts.
- **What will probably break the first time** (the YAML was checked by eye; no YAML parser here):
  - macOS was never compiled: expect compile errors in Windows / Linux-only spots (win_shell, steam_shortcut, lightguns,
    crash_log's signal code, main.cpp's raw input) and possibly OpenGL context flags (macOS core profiles may need
    `SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG`). Online cross-play between Apple Silicon and x86 has not been checked
    for float determinism (`-ffp-contract=off` is set; NEON vs SSE maths libraries could still differ).
  - The sniper container: gcc-12 / ninja / SDL3's build dependencies (X11, Wayland, PipeWire headers) are assumed to
    be in the SDK image; if not, add an `apt-get install` step or use `ubuntu-22.04` (then glibc 2.35: no SteamOS 3.4-).
    (linux2, 2026-11-03: checked locally in the same image: the headers are there, but it ships gcc-10 and gcc-14,
    no gcc-12 any more: the job now takes the newest `g++-N`. The workflow also runs by hand with a choice of
    platforms and on a push to the branch `ci`, and the macOS job uses `macos-latest`: docs/porting.md "CI without a
    release".)
  - stb is fetched at `master` (not pinned): pin a commit for reproducible builds.
  - The Windows job's version check compares the tag with `hotd --version`: a tag that differs from CMakeLists.txt fails.

## v1.1.0: setup, auto-updater, changelog (setup2, 2026-11-02)

- **CHANGELOG.md** (Keep a Changelog) at the repository root; the packages carry it as `CHANGELOG.txt`
  (make_portable_hotd.ps1, make_linux_package.sh, release.yml's macOS job). The game shows the running version's
  section as What's New (once after a new version starts: launcher.ini `lastversion=`; Options > System > What's New).
  Every release: add its section at the top (`## [x.y.z] - date`) before tagging.
- **The release on GitHub must have** `hotd-rewrithe-v<version>-win64.zip` and `SHA256SUMS.txt` as assets (the CI
  uploads both): the auto-updater (src/util/update_check.h) downloads the zip, checks it against SHA256SUMS.txt and
  refuses a mismatch. The release's description (its "body") is the changelog the Update page shows: paste the
  version's CHANGELOG section (or docs/release/notes_vX.md) there.
- The updater unpacks with bin\hotd_launch.exe (copied to settings\update\hotd_update.exe, so bin\ can be replaced);
  it keeps settings\, screenshots\ and data\arcade / pc / music / steam, and writes nothing outside the game's folder.
- Test without the internet: `HOTD_UPDATE_URL=<a local release.json>` (asset URLs may be local paths),
  `HOTD_UPDATE_AUTO=1` (presses Update, hidden window), `HOTD_UPDATE_RESTART_ARGS="--frames 200 --mute"`.
- The setup's install: `hotd.exe --setup-test <folder> --arcade <zip> --pc <folder>` (headless; with
  `HOTD_SETUP_SANDBOX=<folder>` the shortcuts and the Apps & features entry go to a test place);
  `hotd.exe --uninstall --yes --remove-settings` removes it again. Pictures of every page:
  `HOTD_SETUP_SHOTS=<folder>` (+ HOTD_SHOT_ARCADE / HOTD_SHOT_PC / HOTD_SHOT_PC_NOTE / HOTD_SHOT_PC_BAD /
  HOTD_SHOT_PC_SATURN), run from a package's bin\hotd.exe.
