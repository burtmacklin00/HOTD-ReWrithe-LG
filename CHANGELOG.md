# Changelog

All notable changes for players. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions
follow [Semantic Versioning](https://semver.org/). The game shows this version's section once after an update and in
Options > System > What's New.

## [1.2.0] - 2026-11-03

### Added
- Linux, Steam Deck (SteamOS) and macOS packages next to the Windows one. The Linux package runs on glibc 2.31 and
  newer (SteamOS 3, Ubuntu 20.04+, Mint 20+); the macOS one is a universal app (Apple silicon and Intel, unsigned:
  right-click > Open the first time). The install steps are in each package's README.
- Survival Mode (More Modes > Survival), **Very Experimental / Proof of Concept**: endless rounds that keep getting
  harder, points for hits and kills to spend between rounds (ammo, the SMG, a life, perks), reloading by hand, three
  lives, its own high scores. Expect rough edges.
- Two built-in mods (Options > Mods, off by default): Rainbow Blood (#8: all the blood, the wounds and the blood marks
  cycle through the colours, with speed and style settings, optionally the HUD and crosshairs too) and Big Head (#10:
  big heads on the creatures, the bosses and, by default, the people too; also in the Gallery).
- Light guns: Options > Video > Show Crosshairs Off (or Off per player) to play without a crosshair, and Shot Flash
  Size (Small / Arcade / Large; Arcade is the original size at any resolution). CPU teammates' shot flashes follow
  Teammate Crosshair Flashes.
- Settings during play: the pause menu opens the settings.
- Options > Video > Game Gamma and Menu Gamma (#23): brighter (or darker) dark tones for the game and for the menus
  separately; 1.00 is the original picture.
- Options > Video > Transparency (Auto, Arcade Dots, Smooth, Smooth with Dark Edges) for blood splats and puddles, and
  Hit Marks (Arcade Dots or Smooth, #35), with pictures in the menu that show the difference.
- CPU teammates: computer players in the other seats (Easy, Normal, Hard, Expert) that shoot the creatures, the bats
  and the bosses' weak points.
- 16 languages: English plus Arabic, Chinese (Simplified), French, German, Hebrew, Indonesian, Italian, Japanese,
  Korean, Polish, Portuguese (Brazil), Russian, Spanish, Turkish and Vietnamese (most are machine-translated:
  corrections are welcome).
- Lives (#30, #25): Starting Lives and Max Lives as two settings, up to 9 lives (more than 5: one torch and "x N").
- A 1-player game in player 2's seat (#29), as the arcade's 2P start alone.
- Chapter Retry (#5): retry the chapter from the pause menu and after a game over.
- Separate stick, gyro, trackpad and mouse sensitivities (#36).
- Gallery: the blood options (Blood Color, a custom colour, Rainbow Blood, Blood Particles) and each body part's
  damage and part-loss models, on their own panels.

### Changed
- The menus' text stays where it belongs: long labels in More Modes fit their buttons (smaller or on wider buttons),
  the classic menu's long lists no longer run into the help line, the Gallery's top hint wraps above the stage, its
  Viewer / Blood / Damage tabs are bigger and easier to point at, and the bosses' weak-point picture no longer covers
  the settings under it or cuts the description short.

### Fixed
- Gallery (#4): the creatures' names corrected and the missing ones added (Ebitan A / B / C, Neopetit 2 / 3, Parlor,
  Devilon and others; the fish is an item). With Rainbow Blood the bosses now change colour too (the Chariot without
  its armour, the Hermit), and the pale pink wound stripes (Harris's chest and arms) cycle with the rest.
- The hit screen shake is drawn again (#35).
- The ending (#34, #7): the cut-scene bars and the ranking card's black bands go away when the ending starts; the
  staff roll is centred, and in widescreen only its right side is black.
- The shot flash stayed glued to the screen with Smooth Motion (#22).
- The mouse stayed locked in the middle of the window after a restart from the game over (#31).
- Red specks in some arcade colours (#28).
- PC models: the punch's hit mark is the PC's own splat, not claw marks (#9).
- PC sound: the chainsaw and electric sounds popped (#3).
- Setup: importing loose arcade ROM files (a folder or an unpacked archive) found only 4 of the 29 files.
- Attract demo: fully uncensored as on the 1998 PC with Attract Gore (the default): the sewer researcher and the
  blood pool in the prologue are back, and the prologue's hostage close-up uses the PC's camera. Off plays Revision A.
- Gameplay > Edge Reload works in PC Mode too (#42), reloading to the character's full load.

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
