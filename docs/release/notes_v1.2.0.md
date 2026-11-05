# The House of the Dead: ReWrithe v1.2.0

> A fan-made rewrite, **not affiliated with, endorsed by or sponsored by SEGA**. **No game data is included in any
> download**: you need your own arcade ROMs (MAME's `hotd`, Revision A) and your own 1998 PC game.

The first release for **Linux, Steam Deck and macOS**, a pile of fixes from your reports, two cosmetic mods, settings
during play, and a very experimental Survival mode. Coming from v1.1.0: the game offers the update itself (Update /
Not now); your settings and game files are kept.

## New

- **Linux, Steam Deck (SteamOS) and macOS** packages next to the Windows one.
  - Linux: glibc 2.31 and newer (SteamOS 3, Ubuntu 20.04+, Linux Mint 20+). Unpack, run the setup script; steps in
    the package's README and in the main README ("Linux, Steam Deck and macOS").
  - Steam Deck: Desktop Mode to install, then Add to Steam and play in Game Mode.
  - macOS: a universal app (Apple silicon and Intel), unsigned: right-click > Open the first time (on macOS 15:
    System Settings > Privacy & Security > Open Anyway). See `HOW TO OPEN.txt` in the zip.
- **Survival Mode** (More Modes > Survival), **Very Experimental / Proof of Concept**: endless rounds that keep getting
  harder, points for hits and kills to spend between rounds (ammo, the SMG, a life, perks), reloading by hand, three
  lives, its own high scores. Expect rough edges.
- **Mods** (Options > Mods, off by default):
  - **Rainbow Blood** (#8): all the blood, the wounds and the blood marks cycle through the game's own rainbow (the
    colours of the computer screens behind Curien in Chapter 4). Speed, "each splash / together", creature colours,
    and optionally the HUD and crosshairs.
  - **Big Head** (#10): the classic cheat, 2x by default, everyone (creatures, people, bosses) by default.
- **Light-gun options**: Video > Show Crosshairs Off (or Off per player) for playing without a crosshair, and Shot
  Flash Size (Small / Arcade / Large; Arcade is the original size at any resolution).
- **Settings during play**: Pause > Settings opens the options over the paused game. Rows that only take effect in
  the next game say so.
- **Game Gamma and Menu Gamma** (#23), Options > Video: brighten the dark tones of the game and of the menus
  separately (1.00 is the original picture).
- **Transparency** (Auto, Arcade Dots, Smooth, Smooth with Dark Edges) for blood splats and puddles, and **Hit Marks**
  (Arcade Dots or Smooth, #35), with pictures in the menu that show each choice.
- **CPU teammates**: computer players in the free seats (Easy, Normal, Hard, Expert) that shoot creatures, bats and
  the bosses' weak points.
- **16 languages**: English plus Arabic, Chinese (Simplified), French, German, Hebrew, Indonesian, Italian, Japanese,
  Korean, Polish, Portuguese (Brazil), Russian, Spanish, Turkish and Vietnamese. Most are machine-translated:
  corrections are welcome (`docs/translating.md`).
- **Lives** (#30, #25): Starting Lives and Max Lives as two settings, up to 9 lives.
- **Player 2 alone** (#29), as the arcade's 2P start.
- **Chapter Retry** (#5, an option): retry the chapter from the pause menu and after a game over.
- **Separate sensitivities** (#36) for stick, gyro, trackpad and mouse.
- **Gallery**: the blood options (colour, custom colour, Rainbow Blood, particles) and every body part's damage and
  part-loss models, to explore the creatures' variety.

## Fixed

- **Setup**: loose arcade ROM files (a folder or an unpacked .7z / .rar) were "Revision A, verified" but the install
  found only 4 of the 29. A zip always worked; now everything does.
- Gallery names and missing creatures (#4): Ebitan A / B / C, Neopetit 2 / 3, Parlor, Devilon and others; the fish is an
  item, not cut content.
- The screen shakes again when you are hit (#35).
- The ending (#34, #7): the bars and the black boxes go away when the ending starts; the staff roll is centred and
  uses the widescreen.
- The gun flash jumped away from the crosshair with Smooth Motion (#22).
- The mouse stayed stuck in the middle after a restart from the game over (#31).
- Red specks on bright highlights, e.g. the axes on Chapter 1's walls (#28).
- PC models: the punch shows the PC's own blood splat (#9).
- PC sound: the chainsaw and the electric sounds popped (#3).
- Attract demo: fully uncensored as on the 1998 PC (Attract Gore, on by default): the dead researcher and his blood
  pool in the sewer are back, and the prologue's hostage close-up uses the PC's camera. Off plays the arcade's
  Revision A.
- Edge Reload works in PC Mode too (#42).
- The menus' text stays inside its boxes (More Modes, the Gallery, the classic menu).

## If your PC copy does not pass

Copies of the 1998 PC game differ. The **US and European** releases work best; for a CD image, choose its `.cue`. If
the setup says a file is missing or damaged, that file is missing or damaged **in the copy itself**. Try another copy.

If your **arcade ROMs** are refused: the game needs MAME's `hotd` set, **Revision A**. Many downloads labelled "Rev A"
are the original release (hotdo) or a prototype, which the setup names as such. Send `settings\hotd.log` in a GitHub
issue if you are unsure.

## Known issues

- #16: the camera tilts while tracking the Hermit in Arcade Mode. This is the arcade's own camera (MAME shows the
  same); Boss Mode and PC Mode use the PC's camera, which does not.
- For about 48 frames after you are hit, the shot flash lands slightly off the crosshair: the hit's screen shake moves
  the picture under it. This was always in the arcade's code (the shake is the original's), so it is kept.
- #26: the arcade FM music: no change yet (needs a recording from real hardware to compare against).
- #27: frame pacing / micro-stutter on some systems: send your `settings\hotd.log`.
- macOS and the Steam Deck have only been tested by their automatic builds, not on real hardware yet: reports are
  very welcome.
- Windows Defender / browsers may warn about the download: a false positive for a new, unsigned program. The full
  source is here; check the zip against `SHA256SUMS.txt`.
- Everything else as in [v1.1.0](notes_v1.1.0.md#known-issues).

## Downloads

| File | |
|---|---|
| hotd-rewrithe-v1.2.0-win64.zip | Windows 10 / 11 (64-bit) |
| hotd-rewrithe-v1.2.0-linux-x86_64.tar.gz | Linux, Steam Deck |
| hotd-rewrithe-v1.2.0-macos-universal.zip | macOS 11+ (Apple silicon and Intel) |
| SHA256SUMS.txt | the SHA-256 of each file |

**For the auto-updater:** the release must carry `hotd-rewrithe-v1.2.0-win64.zip` and `SHA256SUMS.txt` as assets; the
release's description is the changelog the game shows.

Report bugs on the GitHub Issues page with `settings\hotd.log` (never attach game files or ROMs).
