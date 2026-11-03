# Changelog

All notable changes for players. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions
follow [Semantic Versioning](https://semver.org/). The game shows this version's section once after an update and in
Options > System > What's New.

## [1.1.0] - 2026-11-02

### Added
- A new setup: Install to (any folder you pick; not Program Files) > Game files > Options > Install > Play.
- Game files: drop a zip, a folder or a CD image (.iso, .bin / .cue) on "Arcade ROMs" and "PC game", or click to
  choose. Each is checked at once: green (good), yellow (accepted, with a note) or red (and why, e.g. "This is the
  Saturn version", "This is the PC demo", "SPRITE\SPR.RBH is missing"). A zip holding a CD image works too, and so do .7z / .rar archives. For a CD image, choose its .cue file.
- CD images: the disc's music tracks are copied too (as Music\TrackNN.wav), so the CD music plays without ripping it
  yourself.
- The European CD's BIGGER.VMC (5 bits differ) is turned into the US file when installed.
- Install copies the game and only the files it needs (the 29 arcade ROM files, the PC game's files, the CD music if
  you want) into its folder, so it no longer needs your originals. Desktop shortcut, Start menu entry and Add to Steam
  are options.
- Uninstall from Windows' Apps & features (no administrator needed). It asks before deleting your settings and
  high scores.
- Playing without installing still works: unzip and start, then "Play from here".
- Auto-update: when a new version is out, the game shows what changed and offers Update / Not now. Update downloads
  it, checks it against the release's SHA-256 sums and installs it; your settings and game files are kept.
- What's New: shown once after an update, and in Options > System > What's New.
- Options > System > Repair / Change Files: the setup's Game files page.
- Options > Gameplay > Edge Reload: as The House of the Dead 2, moving the mouse crosshair onto the screen's edge
  reloads, no shot needed (off by default).
- Video > Hit Sparks / Gun Muzzle Flash / Blood Particles: On / Off (for clean recordings).
- Gallery > Cut-scenes: the Hangedman's, the Hermit's and the Magician's defeats; Free Cam has a slow mode
  (C / Back), hotkeys 1 / 2 / 3 for the switches and H to hide the key hints; a scroll bar on the list.

### Changed
- The setup window replaces the small "Auto Run" style window of v1.0.0, in the 1998 PC version's grey Windows style.
- Your own files are only read: the setup always builds a new copy, never installs into a folder that already has
  files in it, and the uninstaller removes only what the setup copied.
- The package has a CHANGELOG.txt (this file).

### Fixed
- "155 of 156" PC files: a patched THOTD.EXE (music fix, no-CD, Windows fixes) is accepted. An unpatched 1998 PC copy
  is still recommended. A backup of the original next to it (THOTD.EXE.orig or THOTD.EXE.bak) is used too.
- The setup names the file that is missing or wrong (all of them are in settings\hotd.log).
- The CD music is optional and never part of the check; without it the main menu plays the arcade's own song.
- "BIGGER.VMC is wrong", scrambled menu text, a black blob for a cursor: PC files of another version (the Japanese
  release, many "RIP" copies) are accepted with a short note and read correctly. A damaged file no longer draws
  garbage: the simple menu is used instead.
- A missing sound file (SOUND\O_*.RBH) is only a note: that one sound stays silent.
- A PC game one or more folders down: the main menu shows again (it went straight into the game before).
- The setup tells a SEGA Saturn disc apart from the PC game.
- PC soundtrack: the title and boss songs were swapped with rips of the US CD (its tracks 2 and 6 are the other way
  round); they are put back in the game's order by themselves.
- Borderless: pushing the mouse against the screen's bottom edge reloads again (off-screen reload).

### Known issues
- Free Cam's slow mode is awkward: pressing E while rotated, and looking straight down.
- The mouse crosshair can get stuck after a restart with it off the screen: being looked into.
- Windows only for now: Linux / SteamOS and macOS builds come in an update.

## [1.0.0] - 2026-10-31

### Added
- The whole game, Chapters 1-4 and the ending, from your own arcade ROMs (MAME's "hotd", Revision A) and 1998 PC game.
- Arcade mode for 1-4 players with the arcade's (1996) or the PC's (1998) models.
- PC Mode and Boss Mode with the 1998 PC's rules; Chapter Select; High Scores with name entry.
- Two menu styles: the 1998 PC's menu and a widescreen "Remake" menu.
- Online co-op for up to 4 PCs (LAN or internet, a share code, encrypted).
- Mouse, keyboard, gamepads (gyro and touchpad aim), light guns and touch screens.
- Widescreen 16:9 / 16:10 or 4:3, any monitor's refresh rate, the arcade's 57.5 Hz or 60 Hz.
- CRT filter, crosshairs (built-in or your own), blood colour, Sonic Mode, mods, Add to Steam, a Model Viewer.
- Languages: English, Korean, Japanese, Arabic, Hebrew (machine translations).
- Update check: the game tells you when a new version is out.

[1.1.0]: https://github.com/TawhidI-602/The-House-of-the-Dead-ReWrithe/releases/tag/v1.1.0
[1.0.0]: https://github.com/TawhidI-602/The-House-of-the-Dead-ReWrithe/releases/tag/v1.0.0
