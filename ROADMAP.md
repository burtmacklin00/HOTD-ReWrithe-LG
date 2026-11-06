# The House of the Dead: ReWrithe - roadmap

## Public roadmap

- [x] Fullscreen
- [x] Switch between arcade (1996) and PC (1998) models
- [x] Chapter 1 demo (plays start to finish; teaser, October 2026)
- [ ] Halloween release (~2026-10-28): Chapters 1 and 2, Chapter 3 if possible, chapter select in the launcher
- [ ] Chapter 4 (after Halloween)
- [ ] Online co-op
- [ ] 4-player mode (new: the original supports two players)
- [ ] Windows, Linux, macOS, Android, iOS (and similar)

### Platforms (decided 2026-10-16)

The game must run on Windows, Linux, macOS, Android and iOS; the graphics API itself does not matter. The Chapter 1
teaser stays on the current OpenGL 3.3 renderer (Windows). After the teaser, the renderer moves to **SDL3's GPU API
(SDL_GPU: Direct3D 12 / Vulkan / Metal)** as the one cross-platform renderer (OpenGL is missing on iOS, deprecated
on macOS and ES-only on Android). Follow-ups per platform:
- input: touch aiming and gyro aiming as the light gun on phones / tablets; controllers;
- screen sizes and aspect ratios (phones, tablets, ultrawide), UI scaling;
- how players supply their own PC and arcade files on Android / iOS (sandboxed storage, the system file picker,
  copying into the app's folder), the official-files check unchanged;
- packaging per platform (signing / store rules where needed; the Windows release stays unsigned).

The detailed plan and its history follow.


Goal: a C++ rewrite of the 1998 PC port of The House of the Dead. Its game logic takes the best of both versions
(PC port and arcade, system by system; PC-only features such as PC Mode, boss select and the settings are kept) and it
draws the models and textures of the arcade version (Sega Model 2C, Revision A), which are more detailed, or the PC's.

Basis for this plan: Chapter 1 compared side by side. Levels, camera path and enemy spawns are identical in both versions;
only the PC models are less detailed.

Reference material (never committed):
- **PC install:** the user's patched, working copy (`C:\Games\HOTD` here). Read-only. Its `PATCHES.md`
  and `_project` folder hold the earlier findings, summarised in [docs/formats.md](docs/formats.md).
- **Arcade dump:** `private\hotd.zip`, Revision A. Extracted data goes to `private\arcade_exports\`.
  Nothing in `private\` is ever committed or shared.
- **MAME** `src/mame/sega/model2.cpp` (BSD-3-Clause): Model 2 hardware and the "hotd" ROM layout.

## Design principle: switchable model sources

The long-term goal is switchable model sources: PC, arcade, and later custom/modded models. Asset loading is built around
**one interface from the start**. Each source is a separate loader behind it (PC loader, arcade loader, later a mod loader), and the
engine only talks to the interface, never to a format. The engine asks for "model N of the original game" (the PC object ID is the
natural key, since the game logic comes from the PC port) and gets back the same in-memory model/texture types from whichever source
is active. The PC-to-arcade mapping table (phase 3) is what lets the arcade loader answer requests for PC IDs.

## Design requirement: PC copy, arcade dump, or both (user, 2026-10-08)

Players may own the PC copy, the arcade dump, or both.
- **PC only must work fully.** The arcade data is an optional upgrade, never a requirement.
  (Arcade only is a goal as far as the PC logic allows; the game logic, camera paths and event scripts come from the PC files.)
- **With both**, the player chooses the source **per category**: world, zombies, bosses, weapons/effects. Optionally also
  **per model** (by canonical object ID), e.g. PC zombies in an arcade world, or one arcade boss in an otherwise PC game.
  Order of precedence: per-model choice, then the category's choice, then whichever source has the model at all.
- **Animation differences:** PC and arcade creatures may not share skeletons, keyframes or animation timing. The creature
  interface must let each source supply its own animation data (and drive it from the same game-logic state, e.g. "walk",
  "hit left arm", "die"), rather than assuming one set of animations fits both models.
  Found 2026-10-11: the PC port carries the arcade's skeletons and motion data (672 of 674 motions byte-identical, same
  motion numbers), so PC and arcade models animate from one data set; the per-source interface stays for mods.
- **Per-source shading:** each source keeps its own look. PC: textured ARGB1555 pages as the PC port draws them. Arcade:
  Model 2 shading (4-bit texel shades → luma table → palette base color → color translation table, per-polygon lighting).
  The renderer supports both shading models side by side in one frame.
- **Textures always follow their model's source.** A PC model is never drawn with arcade textures or the reverse;
  a texture set belongs to the source (and, for the arcade, to the scene) that loaded it.
- **Before release: arcade textures and colors from the ROM alone.** Players will have ROM dumps, not MAME memory dumps.
  The texture RAM contents (per scene), the polygon palette and the color translation tables must be rebuilt from the ROM
  (by following what the game program does), and the MAME-dump path removed or kept only as a developer check.

## Design principle: best of both versions (user, 2026-10-11)

The game logic is decompiled from **both** versions: the PC port (THOTD.EXE, x86, Watcom calling convention, in Ghidra)
and the arcade program (i960, maincpu ROM), system by system (event script interpreter, camera, enemies and behaviours,
shooting / hit detection, damage, scoring, menus, UI, PC Mode, boss select, ...). For each system:
- **only one version has it** (e.g. PC Mode and boss select on the PC): keep it;
- **both have it:** take the superior one and write down why; when the better choice is a matter of taste, the user decides.
Progress, the choice per system and the reasons: [docs/decompilation.md](docs/decompilation.md).

## Design principle: switchable UI (user, 2026-10-11)

The **whole UI** is switchable between the PC port's and the arcade's: HUD, menus, title and attract screens, text and
fonts, results and name entry. It is its own **source category**, like world / zombies / bosses / weapons-effects, with
the same per-category choice. So when the UI code is decompiled, **both versions are kept** (not merged into one), each
behind the same game-state interface, and the UI's assets (sprites, fonts, layouts) follow their source like textures do.

## Design principle: switchable sound (user, 2026-10-12)

Sound is its own **source category** too (sound effects and music, same cue numbers in both versions,
docs/decompilation.md). First source: the PC port's sound files plus OGG music (`Music\TrackNN.ogg`, already working
in the PC mod). Later, as the "authentic" option: the arcade's sound board emulated (68000 sound program + SCSP and the
sample ROMs from the player's arcade dump), driven by the same cue queue the arcade program fills.

## Phase 1: Project setup

- Folders `src`, `tools`, `docs` (`private` already exists).
- Git repository, `private/` in `.gitignore`, checked to be ignored before the first commit.
- CMake build for C++ with SDL3 that opens an empty window.
- `docs/formats.md`: everything already learned from the PC install.
- The project's working journal (private; left out of the public copy): what is done and what the next phase needs.

## Phase 2: Coordinate check

Find out whether the two versions share a coordinate system before building anything on that assumption.
- Crack the Chapter 1 world geometry of both versions: PC `ART\STAGE1\WORLD\WORLD1.RBH`, and the arcade models in the ROMs.
- Export both to .OBJ.
- Load both in Blender and check that they line up (position, scale, axes, handedness). Record any transform needed.

## Phase 3: Full asset loading

- All PC .RBH types: world, creature, camera (`CAMALL.RBH`), event scripts (`EVTALL.RBH`), sprites, sounds.
- PC .VMC textures.
- All arcade models and textures, including the color tables.
- A PC-to-arcade model mapping table (which arcade model replaces which PC model, with its placement).
- One asset interface with a PC loader and an arcade loader behind it (see the design principle above).

## Phase 4: Engine

- Renderer imitating Model 2 shading.
- Input.
- Audio: sound effects from the PC files, music as OGG like the PC mod (CD-track numbering, ogg-winmm style `Music\TrackNN.ogg`).

## Milestone: playable slice (user, 2026-10-09; paused 2026-10-11 for the decompilation)

The first minute of Chapter 1, playable in the rewrite (arcade or PC models, as selected). Pulls the first parts of phases
4-6 forward; the rest of those phases continues afterwards.

1. Camera path playback on the real rail (arcade path records at main_data 0xEF0000, formats.md §22, or the PC's
   CAMALL.RBH), with the game's speed and stops.
2. Zombie spawns from the event scripts (arcade script stream at work RAM 0x52092C, or the PC's EVTALL.RBH, whichever is
   quicker), placed and animated with the creature animation (§23). PC Mode / boss select later.
3. Mouse aiming and shooting; hit detection on creature parts (ray against the posed part meshes); hit and death motions
   (which motion number means what, per creature type).
4. Basic health, reload (shoot off-screen), a simple HUD (bullets, lives).

Estimate (2026-10-09): about **5-7 sessions**: camera path 1, event scripts and spawns 2-3 (the riskiest: the script
format is not decoded yet), shooting / hits / hit and death motions 1-2, health / reload / HUD 1.

## Phase 5: Camera path playback and event script interpreter

- Play back the camera paths.
- Interpret the event scripts (starting from the known "play music" instruction, opcode 90).

## Phase 6: Game logic

- Decompile both versions system by system and port the better one of each (design principle "best of both versions",
  [docs/decompilation.md](docs/decompilation.md)); PC-only and arcade-only systems are all kept.
- Logging tools that run the original and the rewrite side by side and compare their state.

## Phase 7: PC features

- Any resolution, widescreen, resizable window.
- Mouse, controller.
- Settings (including the PC port's own: blood color, PC Mode, boss select), plus the arcade operator menu's game
  assignments offered as PC options (user, 2026-10-12): difficulty 0-4, lives setting (12 start / max pairs), blood
  color, attract sound, and region (JAPAN / USA / EXPORT, 0x51F791); docs/decompilation.md "Settings".
- In-game switch between PC and arcade models (switches the active loader behind the asset interface).
- Crosshair: a custom one, or the PC port's crosshair.
- Option to turn off the screen flash on each shot.
- Reload by shooting off-screen (like the HOTD2/HOTD3 PC ports).
- Hide the flashing player 2 "insert coin" text in single-player games.
- The PC port's pause feature.
- UI as its own source category (next to world, zombies, bosses, weapons/effects): players choose the PC port's or the
  arcade's HUD, menus, title / attract screens, fonts, results and name entry (design principle above).
- Proper fullscreen.

## Milestone: Halloween release (user, 2026-10-17; target ~2026-10-28): Chapters 1-3 and a chapter select

Replaces the teaser's "Chapter 1 only" scope (the teaser, below, is done: Chapter 1 plays start to finish). Goal:
**Chapters 1 and 2 definitely, Chapter 3 if possible, Chapter 4 after Halloween**, plus a **chapter select in the
launcher**: a chapter is selectable once it plays through (headless bot games without a stall, both model sources, one
and two players); the others are greyed out, never broken. Work from Monday 2026-10-19, 4 PM, with a new weekly budget,
parallel workers per chapter (Chapter 2: the Hangedman boss and Chapter 2's own kinds, props, scene objects and world;
Chapter 3: the Hermit and Chapter 3's own; the chapter select early). What is missing, with sizes, and the worker
split: [docs/chapter2_3_plan.md](docs/chapter2_3_plan.md) (Chapter 2 about 31 KB of arcade code, Chapter 3 about 23 KB,
the two bosses 14.4 KB and 8.9 KB); dates and gates: [docs/teaser_plan.md](docs/teaser_plan.md).

**Status 2026-10-19 evening: all four chapters and the ending are playable and selectable** (both model sources; rough
in places). **Update 2026-10-27 (ch4proof):** the whole-game differential (docs/proof.md) replays the recorded MAME game
frame by frame in the runtime: Chapter 4 with the Magician, the ending and the name entry 28,454 frames all equal;
Chapters 1-3 equal but 8 frames of test-harness / stale-entry artefacts. **Next (user, 2026-10-19):
polish this week / before Halloween** (the user's play-test findings, accuracy against MAME and the real PC), then,
if the polish is done in time, in this order of the user's list:
1. **Secure, easy online co-op** (see "co-op, local and online" below).
2. **Boss battle mode** (the PC port's Boss Mode: fight the bosses in a row; greyed in our PC menu today).
3. **A chapter select menu in the game's own front end** (today: the launcher's chapter line and More Options).
4. **Save / resume games** (a chapter's start state; never mid-event unless the arcade's own state can be restored).
5. **The PC port's PC Mode with its character pick** (PM behaviour: own rounds, reload timer, characters).
6. Players keep choosing arcade or PC models (the PC models have extra styles).

## Milestone: Halloween teaser (user, 2026-10-15; target ~2026-10-28; Chapter 1 done 2026-10-16)


A small playable proof of content: Chapter 1 start to finish on Windows, arcade or PC models, local co-op (mouse +
second mouse / keyboard). Needs the engine side (rendering, event / camera playback, game loop, input) and Chapter 1's
remaining logic; unproven routines are marked unverified. Public repo with docs and tools.

## Milestone: co-op, local and online (user, 2026-10-15; **top priority after the game logic**)

Goal: two players exactly like the arcade cabinet, either on one PC or on two PCs, with player 2 able to press start
and join in the middle of a game.

- **Local co-op (priority 1):** two players on one copy. Inputs per player, assignable in a menu: mouse 1, a second
  mouse (SDL3 reports separate mouse IDs on Windows through raw input), keyboard (arrows / numpad move a crosshair,
  keys for trigger / reload / start), any controller. Each player has their own crosshair.
- **Online co-op (priority 1; phase 1 merged 2026-10-19, docs/netplay.md).** User 2026-10-19: safe and secure, works
  like the SM64 PC port's multiplayer (sm64coopdx: browse a lobby list, host name / password, direct IP fallback),
  handles lag; test bench: the user's 4 home PCs. Order: LAN lobby (no server) -> menu UI + waiting room -> ping,
  adaptive input delay, rollback -> UPnP + PAKE. **No internet lobby or relay (user decision): peer-to-peer between
  friends only** (the LAN list at home; over the internet the host shares its address + code, e.g. one copyable share
  string; UPnP or a port forward).
- (original plan) Two PCs, no dedicated server. The game logic is deterministic and is being proven to
  be exact (the proof harness), so the PCs exchange only the inputs each frame (gun x / y, trigger, reload, start) and
  run the same simulation: lockstep, with rollback (GGPO-style) to hide latency. Connection by direct IP / port
  (UPnP to open the port automatically); [superseded: no relay / lobby] a small optional public relay / lobby is only for NAT cases that a
  direct connection can't handle. Mid-game join: the host sends a snapshot of the game state (the object and global
  areas the port already models), then both continue in lockstep, like player 2 inserting a coin. Desync check: a hash
  of the game state every few seconds. Requires strict float settings (no fused multiply-add, the same build on both
  PCs). **Phase 1 done 2026-10-19** (docs/netplay.md): `--host` / `--join` / `--code`, lockstep with input delay,
  ECDH + AES-GCM on Windows' CNG, per-frame state hashes; tested on one PC over the loopback. Next: launcher rows,
  a test on two real PCs, rollback, mid-game join, UPnP.
- **Controllers:** SDL3 gamepads (Xbox, PlayStation, Switch Pro, Joy-Con); stick aiming, and **gyro aiming** where the
  controller has one (DualShock 4 / DualSense, Switch Pro / Joy-Con; Xbox pads have none). Wii Remote: as a mouse
  through a Dolphin Bar first, native IR pointer later.
- **Future: VR** (after release): the game in stereo with a motion controller as the gun (aim ray instead of a
  screen point); the arcade's fixed camera paths need care (comfort settings).
- Rough size: local co-op 2-3 sessions, online co-op 5-8 (networking, rollback, join-in, desync tests), gyro 1-2,
  VR 8+.

## Future milestone: 4-player co-op, VR, more platforms (user, 2026-10-15; not now)

- **4-player co-op** (an optional mode next to the exact 2-player game; **also in PC Mode once PC Mode is ported**, user
  2026-10-20: today `Seats<P>::max` is 2 for the PC logic): the original logic is built for two players
  (player records, per-player hit bits on body parts, kill credit, attack-slot targets, the dogs' target alternation,
  scores, lives). Mode 4P widens those to four; the exact 2-player game stays as proven. Players 3 / 4 are Rogan and G
  in alternative colours, made at load time as recolours of the player's own textures (like the blood colour option;
  no textures shipped). Cut-scenes: each player sees their own pair (players 1 / 2: the original pair; 3 / 4: the
  recoloured pair); online that is per-PC rendering; local on one screen shows pair 1 (or an option). Rough size 6-10
  sessions.
  **User additions (2026-10-19, experimental mode, allowed to start now next to the polish):** P3 = green crosshair +
  green fire lamps (HUD torches), P4 = yellow crosshair + yellow fire lamps; P3 / P4 models = the PC port's
  alternative skins of Rogan and G (from the player's PC files; PC Mode's character choices). Cut-scenes: try all four
  in the scenes where it works; otherwise two sets (P1 / P2 one cut-scene set, P3 / P4 another). **Gameplay rules
  (agreed with the user 2026-10-19):** enemy attacks rotate over the players in play; enemy counts as the arcade,
  bosses +50 % health per extra player; own lives / continues per player, join / leave any time; score per player,
  hostage rescues to whoever shot the attacker; HUD: P1 / P2 their corners, P3 / P4 the top corners (green / yellow).
  Details: docs/coop4.md. Priority below online co-op and controllers (user).
- **Controllers and light guns (user, 2026-10-19, started):** PS5 / PS4 / PS3, Xbox One, Nintendo Joy-Con (and Switch
  Pro) through SDL3's gamepad API: stick aiming, trigger = shoot, a button = reload (or off-screen); **motion aiming**
  with the gyro on the DualSense / DualShock 4 and the Switch controllers (recentre button, sensitivity, smoothing);
  **light guns** (the user has none: Sinden, GunCon 3 / GunCon 2 adapters, AimTrak, Wii Remote via Dolphin Bar) appear
  as an absolute mouse / pointer: support absolute-pointer devices per player with calibration (tools\calibrate_gun.ps1
  exists) and off-screen reload. **User (2026-10-19): controller support must be flawless, unlike the console remake;
  the PS5 DualSense uses everything it has**: gyro aiming as the headline mode (low latency, recentre / ratchet, tilt
  correction), the touchpad as an absolute pointer (light-gun-like) or a trackpad, sticks with linear response and a
  small dead zone (fine-adjust on top of gyro), light trigger pulls, adaptive-trigger gun resistance + haptics, the light
  bar in the player's colour; per-player profiles, per-type defaults, hot-plug, mixed devices. **Control styles per
  player, each with its own settings and combinable (user):** classic pad (sticks), motion only, motion + stick,
  sideways "gun grip" (built, then removed 2026-10-30 after the user's test: a Switch controller works better),
  touchpad (pointer or trackpad, alone or with gyro); shared: adaptive-trigger resistance (strength), vibration on
  hitting / on getting hit (separate), light bar = player colour.
- **VR** (OpenXR): stereo rendering from our own renderer, the head free inside the camera's frame along the rails, the
  motion controller as the gun (the game's shot code already works with a ray, so the controller's ray replaces the
  screen point), comfort options for the camera moves. Someone has made a VR mod for HOTD2; ours is easier because the
  renderer is our own. Rough size 8-12 sessions.
- **Platforms:** Windows, macOS, Linux natively (SDL3; rendering through SDL3's GPU API, which uses Direct3D 12 / Vulkan
  / Metal; case-insensitive file lookup for the player's game files). Later Android / iOS: controller, mouse, or touch
  (tap to shoot, tap off-screen or a button to reload); the player copies their own game files onto the phone (iOS
  probably by sideloading). Rough size: desktop trio 2-4 sessions if kept in mind from the start, mobile 4-6.
- **Design rules from now on** so these stay cheap: keep the game simulation separate from rendering and input (it
  already is: the proven logic only sees inputs per frame), no platform-specific code in the game logic, player count
  never hard-wired outside the game logic.

## Phase 8: Release

- Code only. No game data, no ROM data, no music.
- Players supply their own PC copy and their own arcade dump.

## "Add to Steam" (user, 2026-10-17; done 2026-10-17: util/steam_shortcut.*, More Options / launcher / --add-to-steam)

Make adding The House of the Dead: ReWrithe to Steam as a non-Steam game one button press: an "Add to Steam" button in the launcher /
PC menu and `--add-to-steam`. Only on the player's press, never automatic; it edits Steam's user config only, with
consent, and never an executable (antivirus rule).
- Find the Steam install (registry SteamPath / InstallPath) and every `userdata\<id>\config\` account folder. Refuse
  while Steam runs (ask the player to close it).
- Back up `shortcuts.vdf`, then add, or update with no duplicates, a "The House of the Dead: ReWrithe" entry in the binary VDF: exe =
  hotd.exe (or the package's Play.bat), StartDir, icon = THOTD.EXE's or ours.
- Compute the shortcut app id (CRC32 of exe + name, | 0x80000000; the 64-bit grid id) and write the grid art into
  `config\grid\`: `<id>p.png` 600x900 cover, `<id>.png` 920x430 capsule, `<id>_hero.png` 1920x620, `<id>_logo.png`
  (transparent) and `<id>_icon.png`. Compose all of it at that moment from the player's own files (PC splash / title
  logo / mansion backdrop, the arcade logo as an alternative); never ship it in the package.
- Log what it wrote; an "undo" restores the backup.

## Future: Steam art rendered by the game (user, 2026-10-18)

Recreate the original promotional / launcher pictures (first: THOTDRUN.EXE's two clown-like zombies in the sewers
under the logo) and new scenes (the Chariot, the mansion gate, ...) with our own renderer, in high resolution and
composed for each Steam size (tall cover, wide cover, background, logo). They are rendered on the player's PC from
the player's own files when the Steam page opens (like today's Default pictures), never shipped: a screenshot of
Sega's models / textures / logo is still Sega's art. Offered as extra choices on the Steam page.
## "Sonic Mode" (the user's experimental fast mode, 2026-10-19; started)

The user's idea: the camera moves faster, the bosses are faster (especially the Magician), normal enemies attack
slightly faster; cut-scenes and dialogue stay at normal speed. Plan (proposed): the deterministic logic runs more
ticks per second during play and the normal rate while a cut-scene / dialogue / title card / results / menu runs (the
game's own flags), the music not sped up; presets "Hasty" x1.2, "Frenzy" x1.5, "Nightmare" (bosses x1.75); later
separate dials (camera / rail, normal enemies, bosses, projectiles) through hooks in the movement and attack timers;
optionally the arcade's dynamic difficulty locked at its top; a score multiplier and separate rankings per preset;
works with local / online co-op (the host's setting) and 4P boss scaling. Opt-in; the exact arcade game unchanged.

## Future: "Typing of the Dead 0" mode (user idea, 2026-10-19; not soon)

Name: **Typing of the Dead 0** (preferred by the user: 0 comes before 1, as HOTD 1 comes before the others);
alternative: "Typing of the Dead: ReWrithe".

A typing mode for HOTD 1 **inside the rewrite** (a game mode chosen in the menu, like Arcade / Boss Mode; same exe,
same files; user 2026-10-19), inspired by The Typing of the Dead (HOTD2), Typing of the Dead 3 / Overkill. Not a rewrite:
the ported shot code takes an aim point, so a completed word "fires" at its enemy's damaging part (as the bot's
weak-part aim already does). New parts: word lists (per enemy / boss phase, difficulty tiers, players' own lists),
word-to-enemy assignment (the nearest / most urgent attacker first, no two on-screen words with the same first
letter), the typing HUD (words over enemies in the 2D layer, typed letters highlighted), typos / combos / accuracy in
the results, keyboard input per player (2 players = 2 keyboards or a split), hostages and bosses (weak points as word
sequences). The name: "Typing of the Dead" is Sega's; a published mod may need a neutral name.

## Future: gallery mode

- View each model on its own (PC and arcade, side by side or switchable), from the player's own copies, inspired by the gallery of
  the Remake. Our own implementation: no assets or code from the Remake.
- **First version in (2026-10-20, src/game/gallery.\*, Frontend::screenGallery):** main menu > Gallery (the Play list):
  35 entries (Rogan, G, their PC Mode second looks, Sophie, researchers, Curien, the zombie / creature types, the four
  bosses and the Chariot without armour), each from the chapter stage it appears in, arcade or PC models (Model / Tab),
  turning (drag, arrows, stick; turns by itself until touched), zoom (wheel, + / -), its motions (the arcade skeleton's
  motions by developer-name prefix, sorted idle / walk / other / attack / hit / death), name and a line of our own text
  in the PC font. All entries available. Left: unlock by chapter reached (a setting), the generated belly skins and
  shadows (the viewer has them), side-by-side arcade / PC, more types (neopetit, cut content), a per-entry check of the
  chosen chapter's texture set. Test pictures: `HOTD_GALLERY_SHOT=<entry>,<arcade|pc>,<motion>,<frames>,<png>[,<yaw>]`.
- The 1998 PC release has its own hidden viewer: Creature Test (Ctrl held, type CREATURE at the main menu; next / last
  body object and motion, THOTD.EXE 0x422324, docs/decompilation.md; tip from YouTube commenter RUNESA). Its 1:1 port
  is the worker "cheats"' job; the Gallery is our separate, larger mode.

## Future (after release): mod support

- Custom models and textures in an open format (glTF or OBJ, plus PNG textures).
- A simple manifest per mod that says which original model (by PC object ID) each file replaces.
- Implemented as a third loader behind the same asset interface, falling back to the PC or arcade model for anything a mod doesn't replace.
- **Texture swaps first (user 2026-10-20):** a `data\mods\<name>\textures\` folder: a PNG named by texture ID / page
  replaces that texture (arcade or PC source, as chosen), plus recolour rules (hue / tint per texture group, like the
  existing blood colour option and the PC port's swappable blood) so players can recolour zombie blood, fire lamps,
  clothes without drawing; an export command dumps the player's own textures as PNGs to start from (from their files,
  never shipped). The 4-player green / yellow fire lamps use the same recolour path in code (no edited art shipped).

## Future (after release): restored cut content (optional mod, user 2026-10-14)

- An optional "restored cut content" mod, **off by default** and clearly labeled as not part of the original game
  (in the settings and on screen while it is active).
- Candidates, from what exists in the player's own arcade dump ([docs/cut_content.md](docs/cut_content.md)): the cut
  creatures (baba, syndy, MrG, disiprin, lubin, hiru, neozom, debu: models, skeletons, some motions), the Hermit's second
  head (`boss31_kao`), the Chariot room with the fireplace passage (`room_6bbb`) and the bad-ending room
  (`room_6bb_zsof`), the two unused subtitle lines; the unused music once the sound board is emulated.
- Arcade data only (the PC files lack the meshes); nothing restored is shipped, it is built from the player's dump.

## Future: translations / language packs (user request 2026-10-21: someone wants Korean)

**Built 2026-10-26 (worker "lang"):** steps below except the 2D sprite pictures (RELOAD ...: per-language pack folders
exist, but packs do not cover 2D sprites yet). How to: docs/translating.md. Left: full ko / ja translations (the files
are samples), shaping scripts (Arabic, Indic, Thai), the "All Settings" text launcher page (English only).

Make it as easy as possible for translators / modders (the plan):
- **One text file per language**, plain UTF-8: `data/lang/<code>.txt` (e.g. `ko.txt`), `key = text` lines with comments,
  next to an English `en.txt` that is generated by the game (`--export-lang en`) and always complete; a missing key
  falls back to English, so a half-done translation still works. Hot reload (F5 in the menu) for quick iteration.
- **Every string goes through a lookup**: menus, Options rows and help lines, prompts, Gallery / Jukebox info, error
  and online messages, the launcher text. Placeholders like `{player}` / `{n}` instead of string gluing.
- **The game's own texts** (cut-scene subtitles / captions, RELOAD / HOLD YOUR FIRE / CONTINUE?, ranking texts) are
  art or ROM text cells: subtitles get a text table keyed by event / line (rendered with our font instead of the ROM
  cells when a translation exists); sprite texts are replaced through the existing texture packs (`data\mods`), with a
  per-language folder.
- **Fonts**: the PC bitmap fonts have no Hangul / CJK glyphs, so a language file names a TTF / OTF font (a free one
  such as Noto Sans KR, SIL OFL; downloading or bundling it needs the user's OK); glyphs rendered to an atlas at the
  menu's size, with the outline / shadow styles; text measuring / wrapping that works for Korean (wrap at spaces, CJK
  per character).
- A "Language" row in Options (both styles; the Remake style gets its Language tab), auto-picked from Windows' UI
  language on first start.
- docs/translating.md: a short guide (copy en.txt, translate, set the font, test with --lang ko, check the
  overflow report `--lang-check ko` that renders every screen and lists texts that don't fit).
- **Japanese** (user, 2026-10-21): the original game already has it. The arcade's region setting (0x51F791: JAPAN /
  USA / EXPORT) switches its own subtitles / voice-message sprites and the attract's tutorial text to Japanese, and the
  PC port has `language=JAPANESE` (THOTD.CFG; 0x5CE8C4 / 0xDA0CEE). So step 1 is cheap (**done 2026-10-21**, gametext: text-only, the logic keeps the USA region; online the host's): a "Game Text: English /
  Japanese" option that sets the game's own region / language (both model sources), no translation needed. Step 2:
  ja.txt for our own menus (needs a Japanese-capable font, same as Korean).
