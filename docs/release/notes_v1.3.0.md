# The House of the Dead: ReWrithe v1.3.0

> A fan-made rewrite, **not affiliated with, endorsed by or sponsored by SEGA**. **No game data is included in any
> download**: you need your own arcade ROMs (MAME's `hotd`, Revision A) and your own 1998 PC game.

> **AI disclaimer:** ReWrithe was made with AI assistance: AI tools helped reverse engineer the original game and combine its code to bring the arcade and PC versions together. No generative AI has been or ever will be used to create artistic assets. Everything you see and hear comes from the original games or from human-made work. Any new model, texture or sound effect will always be made by humans.

A new experimental mode, **The Typing of the Dead 0**, Survival Mode grown up (new weapons, its own HUD and results,
a fairer co-op rule), the controller sensitivity rework, VRR and Black Level, and a round of fixes. Coming from v1.2.0:
the game offers the update itself (Update / Not now); your settings and game files are kept.

## New

- **The Typing of the Dead 0** (More Modes), **Experimental / Proof of Concept**: The House of the Dead played by
  typing, after SEGA's The Typing of the Dead (2000).
  - Type the word over a creature to shoot it: the first letter locks on, each letter is a shot. Bats and thrown axes /
    knives get a letter or two that break into your word; bosses take sentences on their weak points between attacks.
  - Grades per kill, results per chapter, its own top 10s (one per Sonic Mode preset). Options > Difficulty sets the
    word lengths; Escape drops the lock.
  - Keyboard only, offline only, a placeholder look; tuning in `data\typing\typing.ini` (see the README). Expect rough
    edges, and please report what feels wrong.
- **Survival Mode weapons**: the rifle, double pistols, the shotgun and the Magician's Hand (a fireball that explodes
  where it hits, damaging everything around it) next to the SMG, bought between rounds; the shop's items float over
  their signs.
- **VRR (G-Sync / FreeSync)** (#20), Options > Video, default Off.
- **Black Level** (#23), Options > Video: the arcade picture's black point, to bring back the detail in dark areas.
- **Widen 2D Screens**, Options > Video: the title, chapter cards and other full-screen pictures up to 16:9, or 4:3.
- **VR Dual Pistols**: a gun in each hand.

## Changed

- **Controller sensitivity** (#36): every pad input (stick, gyro, touchpad, mouse) has its own whole sensitivity, no
  shared multipliers; the menu cursor has its own touchpad / gyro sliders. Your old settings are converted.
- **Survival Mode**: its HUD in the arcade's own score font (one title block for 1-4 players), its own results card at
  the game over, a new co-op rule (a player who is out comes back next round with the starting lives and half their
  points; the game ends only when everyone is out), Chapter 1's courtyard with the original's own spawn places, a
  balance pass. It is no longer labelled experimental.
- The 2D layer is drawn sharp at 768 lines and more (#43); the lives "x N" is smaller (#25).

## Fixed

- A crash at exit while the next chapter was still loading.
- CPU teammates now recognise every creature, thrown thing and item in the packaged game (they missed some before).
- The Magician's Hand exploded out of reach when its shot missed a creature.
- Setup: files 5-6 folders down passed the check but were not installed.
- Gallery: Neopetit's arcade look has its texture again.
- "Play from here" now also makes The House of the Dead ReWrithe.exe when the PC game's menu pictures cannot be read
  (#13).
- Frame pacing with V-Sync, and the main menu's background with Smooth Motion (#27).
- More Modes and Extras open with SKIDMARX on (#44); the touchpad / gyro cursor keeps the controller's prompts (#40).
- Online, 4 players on two PCs: Edge Reload for each PC's second player.
- The mouse's relative mode is reset when a game starts (#31 follow-up); the Language page fits in every language.

## Known issues

- **The Typing of the Dead 0 is experimental / a proof of concept**: placeholder HUD, keyboard only, not online; the
  word timing and difficulty are still being tuned.
- macOS and the Steam Deck have only been tested by their automatic builds, not on real hardware yet: reports are
  very welcome.
- **Windows Defender may flag `hotd.exe`** (a false positive for a new, unsigned program; the full source is public).
  If it was quarantined: Windows Security > Virus & threat protection > **Protection history**, open the entry for
  `hotd.exe`, **Actions > Restore** (or Allow on device). Check the zip against `SHA256SUMS.txt` first if unsure.
- Everything else as in [v1.2.0](notes_v1.2.0.md#known-issues).

## Downloads

| File | |
|---|---|
| hotd-rewrithe-v1.3.0-win64.zip | Windows 10 / 11 (64-bit) |
| hotd-rewrithe-v1.3.0-linux-x86_64.tar.gz | Linux, Steam Deck |
| hotd-rewrithe-v1.3.0-macos-universal.zip | macOS 11+ (Apple silicon and Intel) |
| SHA256SUMS.txt | the SHA-256 of each file |

**For the auto-updater:** the release must carry `hotd-rewrithe-v1.3.0-win64.zip` and `SHA256SUMS.txt` as assets; the
release's description is the changelog the game shows.

Report bugs on the GitHub Issues page with `settings\hotd.log` (never attach game files or ROMs).
