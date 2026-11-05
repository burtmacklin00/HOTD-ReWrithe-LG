# The runtime host (`hotd`)

The game executable runs the ported arcade logic (`Game<A>`, [port.md](port.md)) as the game itself: the original
address space is mapped from the player's arcade files, the arcade's task system and heap run on that memory with the
original layouts, and each 1/60 s one game frame runs the frame's task group. Built in round 1 of the Halloween teaser
plan ([teaser_plan.md](teaser_plan.md), R1; the chapter start, events, camera and two players in R2); state of
2026-10-15.

```
hotd [--root <repo>] [--regions <arcade regions>] [--snapshot <work RAM dump>] [--players 1|2|3|4 (3 / 4: the experimental 4P mode, docs/coop4.md)] [--event E]
     [--no-chapter-start] [--frames N] [--autofire] [--shot-at F]
```

Input: player 1 = the mouse (left trigger, right reload); player 2 = a second mouse (SDL3 mouse IDs: the first mouse
that reports its own ID is player 1's, the next one player 2's, moved by its relative motion) or the keyboard (arrows
aim, Right Ctrl / Space trigger, Right Shift / Enter reload); 1 / 2 = a player joins (the teaser's join: in play with
the other player's lives; the original's join / continue flow is R4). Both go into `Runtime::gun[p]`, which the input
task writes into the player records `player_shoot` reads. Crosshairs are placeholders until the HUD (R3).

## The chapter start (R2)

`Runtime::chapter_start` (src/game/chapter.cpp) does `chapter_load`'s (0x25DA0) logic on top of the snapshot (default
now `fullgame_us\t00220`: Chapter 1, event 0, one player): `heap_reset` (0x14640, + `skin_buffers_alloc`), the
players' body blocks (cleared; `body_init` is L3's), `task_group_create(0xAB210)` run at once, so the start table runs
as in the original: `evt_chapter_start` and `camera_task_start` (the port), then sky, input, `InitSetPlayerTask` (a
player in state 5 gets `player_in_game_frame`: the table has no routine for state 5), shake, `world_init` (task +
zone; its tables are the engine's), attack slots, letterbox, depth lists, demo timer (since worker chstart the port's,
src/port/chapter_tasks.cpp); then `camera_reset`, `shot_lists_clear`, `chapter_reset_53e1b0`, `chapter_counters_reset`.
(Worker chstart: chapter_load itself is now the port's, src/port/game_flow.cpp, with the 2D screen, sprite banks,
texture area and `hud_mode_init` in it; the runtime's `Runtime::chapter_load` adds its own set-up around it, and the
chapter change game_chapter_start 0x25D40 is the port's too.) From there the ported event interpreter (L1) runs the chapter's scripts and the ported camera
task (L2) the camera: the input task runs `camera_mode_run` (CAMDR 3: the view from camera record 2), the view is read
back from `camera_matrices` (0x51EA50 + slot x 0x154) after each frame and the zoom from the geometrizer writes
(0x800090, `geo_write`). The CAMDR set-ups (0x2D690 / 0x2D6F0 / 0x2D770), called by the interpreter through
`call_routine`, are dispatched to the port by the runtime (they are not in the proof registry).

**Teaser guards (logged, docs/teaser_plan.md cut 1):** `WAIT_ITEM n` still waiting after 4 s: the runtime sets event
flag n (the scene objects that set them: chapter card 177, opening title 172, doors ..., round 3); `WAIT_ENNUM` /
`WAIT_ENALV` still waiting after 20 s while unported routines ran: passed. A spawn that reaches the missing `body_init`
/ `creature_type_setup` boundary ends its task (it would run on an uninitialised body; gone by itself when L3's port
replaces those boundaries). A routine chain deeper than 64 ends its task. With these, `--frames 12000` plays the
default route **0, 1, 3, 4, 5, 6, 19** to `chapter_ended` (game sub-mode 5) with no creatures yet.

## Memory and start

| Address | What |
|---|---|
| 0x000000 | program ROM (`maincpu.bin`) |
| 0x500000..0x5FFFFF | work RAM, loaded from a snapshot of a game in progress (MAME work RAM dump) |
| 0x2000000 | `main_data.bin` (32 MB) |
| 0x6000000 | main_data's upper 16 MB again (the motion data pointers 0xC7540 point there) |
| 0x700000 | the port's stack locals (`Game::local_alloc`), outside the original map |

There is no boot yet: the runtime starts from a snapshot (default: the full-game replay `fullgame_us\t00660`, Chapter 1
event 1, two zombies attacking and two walkers) and runs `game_play`'s frame (`game_frame(0x53E080)`) from there.

## Tasks: no fibers needed

`task_group_run` (0x14D40) calls each task's routine (+0) in the group's ring; `task_unwind_point` / `task_unwind_jump`
(0x23B00 / 0x23B20) are a setjmp / longjmp pair returning 1 (the task exited: go on with the next), 2 (unlinked: run
the task now current) or 3 (the ring's head, `task_group_idle`, was reached: the run is over). The only callers of
`task_unwind_jump` are `task_group_idle`, `task_exit`, `task_unlink` and the unused 0x15150 (the unwind 1 at 0x14E80
inside `task_group_idle`'s block has no caller either): **tasks never yield in the middle of a routine**. So the
runtime unwinds with a C++ exception back to `task_group_run`, and every routine is plain C++ (no fibers, no state
splitting). `task_exit` runs the task's exit routine (+4, normally 0x14E90 = `ret`), clears +8 / +0xC and makes the
task's routine `task_unlink` (freed on its next turn), like the original.

The heap (0x14640..0x14C10): word-indexed blocks from 0x543B00 (index 0 is the free list's sentinel); header = size in
words (negative when free), +4 / +8 = the free list (word indices) or, in a used block, its own payload address (the
ring of blocks freed together, header bit 30 = linked into another block's ring), footer = the size again; first fit,
split when more than 4 words remain, neighbours merged on free, a merged block bigger than the list's first moves to the
front.

## Routines: port, stand-ins, logged gaps

Each task routine address goes, in this order, to: a runtime routine (task system, stand-ins, the routines below), the
port through the proof registries (`src/proof/cases/*_arcade.inc`), or, if neither, a log line ("routine not ported")
and nothing. The port's boundaries the runtime does not implement are overridden by `tools/gen_runtime_bounds.py`
(generated at build time from `src/port/decl/*.h`) to log their first call. `--frames N` runs headless and lists both.

Read from the arcade disassembly and run by the runtime (`src/game/`, all **UNVERIFIED (teaser)**, written in the port's
style so they can move into `src/port` with capture proofs):

- **Creature core** (`creature.cpp`): `object_draw`, `object_draw_model`, `body_draw`, `part_draw`, `part_hit_mark`,
  the per-part hook `body_part_draw_hook` (common path), `body_hook_angle_ease`, `body_pose`, `body_pose_copy`,
  `body_pose_set_rec`, `body_motion2_frame_flags`, `body_motion_advance`, the end flags 0x36B80, `set_motion`,
  `set_motion_blend`, `set_motion_at_frame`, `turn_motion_set_speed`, `set_motion2`, `set_motion2_at_frame`,
  `set_motion_blend_hold`, `body_second_motion`, `set_motion2_blend`.
- **Enemy support** (`enemy_support.cpp`): `depth_list_add`, `attacker_list_add`, `enemy_target_distance`,
  `enemy_attack_slot_take` / `_free`, `difficulty_level`, `player_hit`, `angle_to_camera`, `enemy_dying`,
  `enemy_corpse`, `body_free_parts`, `part_tree_free_children`, `enemy_free_extras`; kind 96 (set-up 0x7EDE0, walk
  0x7EC60, fall 0x7EC30: unanalysed code after `kind096_zombie_walker`'s entry in Ghidra).
- **Frame and system tasks** (`runtime.cpp`, `stand_ins.cpp`): `game_frame`, `attack_slots_task`, `depth_lists_task`.

Stand-ins (not the original logic): the camera task keeps the snapshot's camera (it loads the current slot's stored
matrices 0x51EA50 / 0x51EA80 into the coprocessor exactly as `camera_matrices_update` ends); the input task writes the
mouse into the player / gun records `player_shoot` reads; the player frame runs the scene routine, the lives clamp and
`player_shoot` (no HUD, no out-of-lives; `--lives-kept` gives a life back).

What the creature core taught (for L3, docs to fold into decompilation.md when proven):

- `body_draw` builds T(pos) R(z, y, x) S on the camera matrix, adds the root's step for this frame (x / z from the last
  frame's record, y as recorded; none with NoBias, frame 0, a blend, or Stay), keeps that matrix at +0x1E4, turns by the
  root joint and multiplies by stored matrix 1 (the camera's inverse) to read back the world position (pos, height),
  unless FixMode; the direction is the matrix's yaw (coprocessor 0x6F). The frame is anim_time % frames
  (`body_motion2_frame_flags`), MotFinish = the next frame starts another loop (0x36B80).
- `part_draw` keeps the parent's matrix at part +0x7C, translates by the part's offset (z negated), turns by the joint's
  angles of this frame (or the second motion's), calls the body's hook, and writes the hit sphere's centre in camera
  space at +0x5C (the shot test). Its own model draw is the **shadow** pass (stored matrix 3 = the shadow projection,
  set by `shadow_setup`): the creature's models are drawn by the hook (0x9CE60), which by part class applies the
  head / torso look angles (+0x120.., +0x12C.., +0x138..), saves the anchor matrices (+0x154 head, +0x184 / +0x1B4 the
  skin's two anchors, +0x214, +0x244, Sophie's +0x274 / +0x2A4), marks the type's home part (table 0x14A430: its world
  position becomes the object's home, whose camera-space position is the object's shot sphere) and draws `part_id`.
- Blends interpolate the shortest way per angle; a joint whose angles differ by 0x7000 or more is blended toward the
  equivalent Euler triple (a0 + 0x8000, -0x8000 - a1, a2 + 0x8000).

## Drawing

Every model the game sends through `geo_draw_model` goes to a draw list with the coprocessor's current matrix (camera
space); the engine (`src/game/view.*`) turns it into a world placement with the view's inverse and draws it with the
chapter's arcade world and sky, from the game's camera with the arcade projection (focal length 496, 496 x 384). The
generated skins (`skin_draw`, the zombies' bellies) are rebuilt per frame from the two anchors with the loader's
template. Damage variants of part models (`..._dam00a`) are loaded with their base models.

## Sound (R3)

`Runtime::sound_cue` (the arcade's 0x154D0 boundary) hands the arcade's sound-board code to `game::Audio`
(src/game/audio.*), which maps it to the PC cue (src/game/cue_map.inc, made by `tools/cue_pairs.py`; formats.md §30)
and does what the PC's `sound_cue` does with its cue table: bank samples from `SOUND\GAMESNDS.RBH`, one-shots from
`SOUND\O_<name>.RBH`, stop cues, music as `Music\TrackNN.ogg`, the "all off" commands. The PC files are only read:
`--pc <folder>` (default `private\pcharness\game`, else `C:\Games\HOTD`); `--mute` opens no audio device (the cues are
still decoded and counted); `--audio-wav <file>` mixes into a WAV file instead, one game frame (735 samples) per frame,
also headless. The mixer is our own: an SDL3 audio stream at 44.1 kHz stereo, 16 voices, linear interpolation, the
entry's volume; no panning; a sample re-sent within 1/15 s restarts its voice; a soft limiter.
Arcade codes without a PC cue are logged once and listed by the headless report; `HOTD_SOUND_TABLE=1` adds every code
with its calls and PC cue, `HOTD_TRACE_SOUND=1` prints each cue with its frame.
`--board-wav <out.wav> <arcade zip> <code@seconds,...> <seconds>` (sound tests) plays arcade codes (hex) on the sound
board alone, headless, at the given times into a WAV at the game's board gain (e.g. the chainsaw: `A01355@0.25,A01356@8`).

The cue map: the arcade program ROM holds the list of every sound code at 0xA8CDC (261 words: 0, 0xA00002, the music
0xA01000.., the effects, the voices ..0xA01475; the sound test's order) and the PC cue table keeps that order: list
entry k = PC cue k + 6 (PC cues 1-7 are the commands); every routine pair agrees. PC cues of type 0 are sounds the PC
dropped (silent on the PC too). `tools/mame_sound_probe.lua` logs the arcade's commands (port 0x1C80000) in a MAME
play-through for comparison.

OGG music needs stb_vorbis, which is not in the repository: configure with `-DHOTD_STB_DIR=<folder with
stb_vorbis.c>` (`private\deps\stb`) or `-DHOTD_FETCH_STB=ON` (downloads github.com/nothings/stb). Without it the music
cues are logged.

### The Jukebox (ours; src/game/jukebox.*, frontend_jukebox.cpp)

The main menu's Jukebox (screen 20) lists, per source: **Arcade** = the arcade sound list's codes from the cue map
(0xA01000-0xA0100E music, the rest effects / voices; the PC's stop cues (type 2) and commands left out, codes whose PC
cue is empty shown as "Arcade only"), sent with `Audio::cue` to the sound board after a board reset (0.25 s for its
program to start; the board is started for the page when the sound setting is PC); **PC** = the CD tracks 2-12 (13 when
patch 7's file is there) through `Audio::playMusicTrack`, and the cue table's sample / file cues (types 1 and 4) through
`pcCue`. Voices: PC cues 112-126 (the people's cries) and 158-240 (the VOC files). Song places come from the cue table
(formats.md section 4), the effects' names from the O_ file names. Volume: the board uses the music volume for the
music list (`Audio::setBoardVolume`).

**Arcade lengths** (jukelen, src/game/jukebox_measure.cpp). The board says nothing about its sounds, so every arcade
code is measured once on the player's PC from their own ROM set: the first visit of the Jukebox (per session) starts 4
hidden copies of the exe (`hotd --jukebox-lengths <settings folder> <regions> <ROMs> <part 0-3>`, below normal
priority, one board per process; each takes every 4th code so the songs spread out), which play each code on a board
of their own as fast as it runs (about 28x real time) with the output thrown away and write
`jukebox_arcade_lengths_1..4.txt` in the settings folder as they go (the page reads them every second: "length: being
measured" in the info line until then). The files hold a hash of the sound ROMs: other ROMs, measured again; complete
files: the processes stop at once. Nothing derived from the game is in the repository. About 55 s for all 259 codes on
this PC (the songs first). Per code, in 10 ms steps: **the end** = the output below the quiet level (1e-4 peak) for 4 s
(music) / 2 s (others), the length up to the last sound; **a loop** = the level (RMS of each side over 50 ms) repeats
itself: the smallest lag whose repeat holds (1 s chunks with 85 of 100 steps alike within 25 %) for a whole round + 8 s
and at least 150 s (a phrase played over and over inside a song held 45 s), the best of the next 15 steps, and the
smallest multiple that fits (almost) every step when the lag fits under 99 % (Name entry: 6.85 s fits 96 %, 13.68 s
99.9 %); where the repeat starts back from the end is the intro's end. The samples themselves never repeat exactly (the
SCSP's free-running LFOs); the level does (99 %+ of the steps). Measured with the MAME-dumped hotd.zip (2026-10-21):
music 0xA01000 Title 59.39 s once, 01 Chapter 1 intro 8.59 + loop 143.71 s, 02 Chapter 2 1.98 + 102.41, 03 Game over
6.60 once, 04 Boss 0.55 + 49.71, 05 Chapter clear 5.30 once, 06 Ending 80.78 once, 07 Chapter 3 first cue 7.81 + 98.84,
08 Chapter 3 7.66 + 93.07, 09 Chapter 4 9.20 + 103.68, 0A Chapter 4 second cue 55.83 once, 0B Magician 5.19 + 70.51,
0C Magician second cue 6.26 + 57.65, 0D Name entry 0.30 + 13.70, 0E Chapter 4 menu 0.51 + 49.49; effects / voices: 217
end (0.42-9.49 s), 14 held sounds loop (a 0.1-1.7 s loop: no bar, "loops" in the info line; their stop codes, not
listed, are silent).

The page then shows arcade entries like the PC's: time / total and the bar (a looping song: intro + one loop; once it
went round the time stays in the loop, "loops m:ss" after the name); its time is the board's own clock
(`Audio::boardSamples` since the command). Loop all / Once / Shuffle move on after intro + one loop (a held sound 4 s),
Loop one lets the board go round. **Seeking** an arcade sound (click the bar): the code is sent again after a board
reset and the board run muted (`Audio::boardSkip`, up to 25 ms of each frame, "Seeking:" meanwhile) up to the place:
Chapter 1 to 60 % (91 s) takes about 3 s. A sound without a length (not measured yet, a held sound) shows the time only.

Test: `hotd --jukebox-test <folder>` (or `HOTD_JUKEBOX_TEST=<folder>`; with `--arcade` / `--pc`): no device, every item
of every list played for 0.6-8 s, its RMS (all must be above 1e-5), WAVs of each song and of all effects / voices per
source, then the loop points: each looping PC track's last 2 s and 2 s more (it must wrap to 2.0 s and sound), the
jingles (Track05 / 07) must end, looping samples still sound after 1.5 x their length. Then the arcade lengths: the
measuring processes run first (into the settings folder, waited for), every arcade entry's length is listed
("length Arcade ..."), each sound that ends must go quiet within 0.3 s of its length (the last 10 ms above the quiet
level), each looping song's RMS envelope (100 ms windows, 20 s from the middle of the first round) must fit best (of
the lags from half a loop to one and a half, 10 ms steps) within 0.05 s of the measured loop, and a seek of Chapter 1 to
60 % must land there (within 0.1 s) and its next 20 s match the same place played through (envelope correlation > 0.9;
WAVs Arcade_music_02_whole / _seek60). Result 2026-10-21: 475 items, 0 silent (PC 12 songs, 119 effects, 98 voices;
arcade 15 songs, 133 effects, 98 voices), every loop check passed, 233 arcade length checks 0 bad (the loops' best lags
0.00-0.02 s from the measured ones, correlations 0.965-0.996; seek at 91.38 s of 91.38 s in 3.1-3.4 s, 0.972).
Picture: `HOTD_JUKEBOX_SHOT=<arcade|pc>,<list 0-2>,<row>,<frames>,<png>` (hidden window).

## Continue and game over (R3)

`Runtime::frame` -> `game_mode_frame` (src/game/flow.cpp) runs mode 4's sub-modes as `mode4_game` does: 2 play
(both players "out" in the state table 0xA8790 +4 -> sub-mode 3), 3 `game_continue_begin` (shared countdown
0x52071C = 0x9FFF, i.e. 9), 4 `game_continue_countdown` (-0x2D a frame, a trigger pull drops to the next digit, a
change of the credit counters 0x51F4B8 restarts it; a player back in -> sub-mode 2, the other player's own countdown
`player_continue_frame` takes over; 0 -> `set_mode(5)`). The player side: `player_in_game_frame`'s out-of-lives path
(players_in_game - 1, state 4) -> `player_st4_out_of_lives` -> `player_continue_frame`; at 0 state 6 (game over,
players_in_game_b - 1) -> 120 frames -> state 9. Mode 5: game over music (0xA01003), 360 frames down to 90, GAME OVER
180 frames, then `gameOverDone`: the window loads the snapshot and starts the chapter again (the original's
`next_mode` goes to the attract mode, which the teaser does not have).

relplay (2026-10-30): a game from the menus (Arcade, Chapter Select; not online, not PC Mode or Boss Mode, which have
their own ends) goes from `gameOverDone` to the menus' attract instead (the one the boot and the idle main menu show:
a key, a click or a pad button goes to the main menu, nothing starts a game), or with Title / Attract off to the main
menu, as the 1998 PC's game over goes back to its front end. Before, the window started a new game there: the arcade's
title loop for Chapter 1 (its start button or a trigger began Arcade again), a later chapter at once. The scripted runs
(`--frames`, `--input-test`, `--bot`, checkpoints) and online games restart as before.

Score Display (Gameplay, or the title code's 0x51F42C = 7): the moved score line (row 0x24) is cleared outside play
(cut-scenes, the continue screen) by `Runtime::score_line_hide`; `hud_lives_draw` prints it again in play. Draws only.

Teaser forms (UNVERIFIED): `player_start_check` is free play: the start key (1 / 2) continues at once with 3 lives
and the score kept (the credit check, lock-out and the state-1 start routine are R4's); the trigger is bit 0 of the
player's `pressed`; `player_st6_game_over` has no collapse motion; mode 5 shows the last view instead of the map
scene (0x5CB50..). `--lives-kept 1` restores the old testing behaviour, `--lives-out F` empties every player's lives
at frame F (headless: the report prints each game mode / sub-mode change and stops at the game over's end).

## HUD and the 2D layer (R3, hud 2026-10-16)

The original's 2D routines run as they are (src/game/sprites.cpp, formats.md §31): the runtime maps tile RAM
0x1000000 and character RAM 0x1080000 into the arcade address space on first use, runs the boot's and chapter_load's
2D set-up (the fixed banks, chapter_screen_reset, hud_sprites_load) and implements the sprite routines, the HUD
routines player_in_game_frame calls (torches, bullets, RELOAD and the off-screen hint, HOLD YOUR FIRE 0x107), the
start prompt, CONTINUE / the digits / GAME OVER (from the flow's player states and sub-modes), the boss life bar,
the subtitles and the window masks. `game::Hud` composes the result each frame:
- **arcade models**: the priority cells of the four tilemaps (src/assets/sprites_arcade.h), over the 3D picture where
  the 496 x 384 screen lies in the window (height fitted, centred);
- **PC models**: the runtime also keeps the PC's sprite-ID layers (Runtime::pcCells, the PC's own semantics); each
  arcade sprite is drawn as the PC sprite with the same picture (src/assets/sprite_map_pc.inc) from SPR.RBH +
  BIGGER.VMC pages, the subtitles in the PC's font; a labelled text placeholder only when those files are missing.
Both: the crosshairs (the mouse stand-in; player 1 red, player 2 blue), R4's letterbox bars and chapter clear text.
F1 toggles the developer lines. Debugging: `HOTD_TRACE_SPRITES=1` prints each sprite newly shown and each subtitle
with its frame; `HOTD_DUMP_2D=<frame>` writes tile RAM, character RAM, the palette shadow and the bank records to
dump2d.bin. Low-priority cells (behind the 3D in the original: kind 176's area picture) are composed under the priority ones, over
the 3D. Not drawn: the shot flash (0x46D50 not ported), the coin / credit prompts (free play), the game over map.

## Not done yet (round 3 on)

`body_init` / `creature_type_setup` (spawning: L3, merge pending), `attack_slots_update` (the waiting positions
2..13), the scene objects the scripts wait on (177, 172, 178, 173, doors 162-167 ...: L5), props, Hangedman fly-by,
kind 70 (L4), the Chariot (L7), severed parts, effects, the game over map scene, the original join / credit flow,
texture area changes (`texture_area_load`, TEXCHG), camera roll in the view, the PC models, boot.

## Icons and the desktop shortcut

No game icon is shipped or committed. At start-up `hotd` and `hotd_viewer` read the largest uncompressed icon image of the
player's own `THOTD.EXE` (their PC folder; own PE resource parser in `src/util/pe_icon.cpp`, read only) and set it as the
window icon (log line `window icon: THOTD.EXE, 32x32`); no THOTD.EXE or no icon means no icon, silently.
The exes themselves carry our neutral icon, a version resource and an asInvoker manifest (`src/res/`). A local build
whose CMake finds a THOTD.EXE (or `-DHOTD_LOCAL_ICON=<path>`; `OFF` to disable) embeds that icon in the exes it builds
(build folder only). The launcher's `[ DESKTOP SHORTCUT ]` button and `hotd --create-shortcut [folder] [--yes]` write a
`The House of the Dead - ReWrithe.lnk` (desktop and Start menu, or only `folder`) whose icon is `<PC folder>\THOTD.EXE,0`; an existing
shortcut is replaced only after a second click / a `y` (or `--yes`).
