# The House of the Dead: ReWrithe v1.4.1

A fan-made rewrite, **not affiliated with, endorsed by or sponsored by SEGA**. **No game data is included in any
download**: you need your own arcade ROMs (MAME's `hotd`, Revision A) and your own 1998 PC game.

**AI disclaimer:** ReWrithe was made with AI assistance: AI tools helped reverse engineer the original game and combine its code to bring the arcade and PC versions together. No generative AI has been or ever will be used to create artistic assets. Everything you see and hear comes from the original games or from human-made work. Any new model, texture or sound effect will always be made by humans.

**About the sound:** the sound is still not "accurate" everywhere. If something sounds off compared with the real
cabinet, please tell me which track or effect and where (GitHub #26).

Wii Remote support, Mixed Models, a Direct3D renderer, a new main menu, custom splash screens, more VR options and a
round of fixes. Coming from v1.4.0: the game offers the update itself (Update / Not now); your settings and game files
are kept.

## New

- **Wii Remote support** (Experimental; Windows, Linux, macOS): use a Wii Remote as a pointer gun. It is off until you
  enable it (Options > Controls > Wii Remotes). Added by **zedhqx4** (GitHub PR #85); the protocol and calibration
  notes come from **GitFist**'s hotd2-re. Thank you both.
- **Mixed Models** (Options > Video > Models: Arcade / PC / Mixed): choose the models per group on the Mixed Models
  page: Players and humans, Creatures, Bosses, World, Effects, HUD and Arcade Extras. Where the PC files lack a model
  (the cut creatures too), the arcade model fills in. Not on the Quest (#49).
- **PC Renderer** (Options > Video): Software or Direct3D (#79).
- **A new main menu**: Story Mode, More Modes, Extras, Options and Quit, with Survival and Horde sub-menus (#89).
- **Custom splash screens**: mods of the type `splash` (PNG / JPG) (#83).

## VR (Experimental)

VR is still experimental, and the standalone Quest build is still extremely buggy: PC VR (Quest Link / Air Link /
SteamVR) is the recommended way to play.

- **Gun Hand (VR)**: Right / Left (#86).
- Creatures that appear outside the flat view now fade in, and the scenery at the sides shows (#78).
- **Cut-scene Crosshairs**: Hide / Subtle / Show (default Hide; flat game and VR, #80).

## Changed

- **Sound (Accurate)**: the lead melody of the boss, Chapter 2, Chapter 3, chapter clear and Magician songs now matches
  the arcade sound, checked against a recording of an arcade board (#87).
- Remake menu: with the keyboard or a pad, a page opens on its first row, and Back returns to the group's item.

## Fixed

- G's voice as Player 1 and the Solo Player Seat crosshair (#88).
- The attract's third demo (#84).
- Sophie's body with PC models (#81).
- Survival, Chapter 4's roof: the glitched creature textures.
- PC Mode: the Researcher holds her gun in the cut-scenes, as on the 1998 PC (#90).
- PC Mode with the arcade models: the worms' shadows on Chapter 1's bridge sit under them (#91).
- The good ending: Sophie's "Thank you" plays again (#92).
- Steam Deck / Linux: the main-menu music falls back to the arcade's song when the PC files give none, and CD images' files are found whatever their case; hotd.log says why (#94).
- PC Mode: no Character Select after PC Mode's own character screen.
- Mixed Models: the players in the opening car follow Players and humans.
- Linux and macOS: build fixes.

## Known issues

- **VR is experimental**, and the Quest build is extremely buggy.
- **The tempo of the arcade music is about 4 % fast** compared with a board recording (#26).
- Horde Mode and The Typing of the Dead 0 are still experimental / proofs of concept.
- macOS and the Steam Deck: tested by their automatic builds only.
- Windows Defender may flag `hotd.exe` (a false positive; see v1.3.0's notes for Restore / Allow).
- Everything else as in v1.4.0.


## Downloads

| File | |
|---|---|
| hotd-rewrithe-v1.4.1-win64.zip | Windows 10 / 11 (64-bit) |
| hotd-rewrithe-v1.4.1-linux-x86_64.tar.gz | Linux, Steam Deck |
| hotd-rewrithe-v1.4.1-macos-universal.zip | macOS 11+ (Apple silicon and Intel) |
| hotd-rewrithe-v1.4.1-quest3-experimental.apk | Meta Quest 3 standalone: **extremely buggy, experimental** (PC VR preferred) |
| SHA256SUMS.txt | the SHA-256 of each file |

**For the auto-updater:** the release must carry `hotd-rewrithe-v1.4.1-win64.zip` and `SHA256SUMS.txt` as assets; the
release's description is the changelog the game shows.

Report bugs on the GitHub Issues page with `settings\hotd.log` (never attach game files or ROMs).
