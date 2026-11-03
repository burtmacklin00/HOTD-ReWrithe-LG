# The House of the Dead: ReWrithe v1.1.0

> A fan-made rewrite, **not affiliated with, endorsed by or sponsored by SEGA**. **No game data is included in any
> download**: you need your own arcade ROMs (MAME's `hotd`, Revision A) and your own 1998 PC game.

The first update after v1.0.0: a real setup, an auto-updater, and the v1.0.0 bug fixes. Coming from v1.0.0: unzip
this version anywhere and start `HOTDReWritheSetup.bat` (it can install itself), or unzip it over your old folder
(your settings are kept). From now on the game updates itself.

## New

- **Setup**: Install to (any folder; not Program Files) > Game files > Options > Install > Play.
  - Game files: drop a zip, a folder or a CD image (`.iso`, `.bin` / `.cue`) on "Arcade ROMs" and "PC game", or click
    to choose. Each is checked at once: green, yellow (accepted with a note), or red with the reason ("This is the
    Saturn version", "This is the PC demo", "SOUND\GAMESNDS.RBH is missing"). A zip holding a CD image works too, and so do .7z / .rar archives. For a CD image, choose its .cue file.
  - A CD image's music tracks are copied too, so the CD music plays without ripping it yourself.
  - Your own files are only read: the setup always builds a new copy, never installs into a folder that already
    has files in it, and the uninstaller removes only what the setup copied.
  - Install copies the game and only the files it needs (the 29 ROM files, the PC game's files, the CD music if you
    want) into its folder: your originals are not needed afterwards. Desktop shortcut and Start menu entry (with your
    PC game's icon), Add to Steam if you like.
  - Uninstall from Windows' Apps & features (no administrator). It asks before deleting your settings and high scores.
  - Or **Play from here** without installing, as before.
- **Auto-update**: a new version's changelog with Update / Not now. Update downloads the Windows zip, checks it against
  the release's `SHA256SUMS.txt` (refused on a mismatch), installs it and restarts the game. Settings and game files
  are kept. Off: Options > System > Check for Updates.
- **What's New**: once after an update, and in Options > System > What's New (`CHANGELOG.txt` in the package).
- Options > System > **Repair / Change Files**: the setup's Game files page.
- Options > Gameplay > **Edge Reload**: as The House of the Dead 2, moving the mouse crosshair onto the screen's
  edge reloads, no shot needed (off by default).
- Video > Hit Sparks / Gun Muzzle Flash / Blood Particles: On / Off (for clean recordings).
- Gallery > Cut-scenes: the Hangedman's, the Hermit's and the Magician's defeats; Free Cam has a slow mode
  (C / Back), hotkeys 1 / 2 / 3 for the switches and H to hide the key hints; a scroll bar on the list.

## Fixed

- **"155 of 156" PC files**: a patched THOTD.EXE (music fix, no-CD, Windows fixes) is accepted, with a note. An
  **unpatched** 1998 PC copy is still recommended. A backup of the original next to it (THOTD.EXE.orig or .bak) is
  used too.
- The setup names the file that is missing or wrong (all of them in `settings\hotd.log`).
- The CD music is optional and never part of the check; without it the main menu plays the arcade's own song.
- **"BIGGER.VMC is wrong" / scrambled menu text / a black blob for a cursor:** PC files of another version are
  accepted, with a short yellow note per file; only THOTD.EXE or the main data files missing stop Play. The Japanese
  PC release's files (also inside many "RIP" copies) are read with their own numbering, so the menu text, the cursor
  and the PC models look right. A damaged file no longer draws garbage: the simple menu is used instead.
- A missing sound file (SOUND\O_*.RBH) is a note: that one sound stays silent.
- A PC game one or more folders down (for example `data\pc\<some folder>\Game Files\`): the main menu shows again.
- The setup tells a SEGA Saturn disc image apart from the PC game.
- The European CD's BIGGER.VMC (5 bits differ from the US file) is turned into the US file when installed.
- **PC soundtrack: the title and boss songs swapped** (#12): rips of the US CD, which has tracks 2 and 6 the other
  way round, are put back in the game's order by themselves.
- **Off-screen reload in Borderless** (#15): pushing the mouse against the screen's bottom edge reloads again.

## If your PC copy does not pass

Copies of the 1998 PC game differ. The **US and European** releases work best; for a CD image, choose its `.cue`. If
the setup says a file is missing or damaged, that file is missing or damaged **in the copy itself**, wherever it came
from: it is not a problem of the setup or the game. Try another copy (US or European); keeping a second one at hand is
recommended.

## Known issues

- Free Cam's slow mode is awkward (pressing E while rotated; looking straight down).
- CD images: ISO 9660 only; the music comes from a `.bin` / `.cue` image's audio tracks (an `.iso` has none).
- Windows Defender / browsers may warn about the download (for example "Wacatac.H!ml"): a false positive for a new,
  unsigned program. The full source is here; check the zip against `SHA256SUMS.txt`.
- The mouse crosshair can get stuck after a restart with it off the screen (#31): being looked into.
- Windows only for now: Linux / SteamOS and macOS builds come in an update.
- Everything else as in [v1.0.0](notes_v1.0.0.md#known-issues).

## Downloads

| File | SHA-256 |
|---|---|
| hotd-rewrithe-v1.1.0-win64.zip | (see SHA256SUMS.txt) |

**Important for the auto-updater:** every release must carry `hotd-rewrithe-v<version>-win64.zip` and
`SHA256SUMS.txt` (made by `tools\make_portable_hotd.ps1`) as assets; the release's description is the changelog the
game shows.

Report bugs on the GitHub Issues page with `settings\hotd.log` (never attach game files or ROMs).
