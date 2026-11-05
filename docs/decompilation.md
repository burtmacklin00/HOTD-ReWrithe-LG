# Decompilation: three behaviours, exactly

**Standard (user, correction after the 2026-10-21 session; replaces "best of both" as the bar for the decompilation).**
There are **three behaviours** to reproduce exactly:
1. **A** = the arcade version (Model 2 ROM code);
2. **PA** = the PC port's Arcade Mode (PC code; not the same as the arcade: e.g. no shot flash, its own reload input);
3. **PM** = the PC port's PC Mode (PC code: characters, own rounds, reload timer, damage, hit size ...).

The rewrite must be as faithful as the developers' source (which we don't have): behaviourally exact to whichever of
the three a behaviour comes from (same states, constants, random-number call order, frame timing, quirks), keeping
their structure (one routine per original function, the same data layouts and tables, their names). Byte-matching
recompilation is not required. A constant or branch is reproduced exactly; English explanations only where versions
are mixed, where platform code or rendering changes, or where a port needs them. Proof is by comparison: a
**differential checker** (same inputs, game state compared per frame) for A against MAME and for PA / PM against
the PC port (plan: "Differential checker" below). Where the rewrite still mixes versions (ROADMAP "best of both":
switchable UI and sound, the arcade world renderer with PC logic ...), the "Chosen version" column says which, and
each of the three behaviours must stay reproducible on its own.

## Sources and tools

- **PC:** `C:\Games\HOTD\THOTD.EXE.orig` (unpatched, Watcom C/C++, formats.md §2). Ghidra project `private\ghidra\hotd_pc`
  with the Watcom register convention (`tools\ghidra\x86watcom.cspec`, compiler spec id `watcom`). Export:
  `tools\ghidra\run_pc.ps1` -> `private\decomp\pc\thotd.c` (all functions) + `thotd_index.tsv` (callers, callees, strings).
  Function discovery (`FindFunctionsPc.java`): Ghidra's own analysis finds only 1,426 functions (300 KB); code pointers
  in the data section (from the relocation table) and the functions packed back to back after each one (Watcom pads
  with `mov eax,eax` / `lea eax,[eax+0]`) bring it to **8,095 functions, 1.16 MB of the 1.6 MB BEGTEXT** (the rest
  is mostly data: float tables, zero blocks). All get `__watcall`, except Windows callbacks (`name@N`). Example of the
  output quality: the "play music" opcode handler 0x4C17D8 decompiles to
  `FUN_00412000(*(int *)(DAT_00da0ab8 + 4)); DAT_00da0ab8 += 8;` (cue to the sound dispatcher, script PC += 8).
- **Arcade:** the i960KB program (`private\arcade_exports\regions\maincpu.bin`, formats.md §8). Ghidra has no i960
  module, so `tools\ghidra\i960\` is our own SLEIGH module (written from the i960 KA/KB reference and
  `tools\i960dis.py`): all CTRL / COBR / REG / MEM instructions incl. the KB's real-number ops; the condition code is
  three flags (l / e / g) so compare-and-branch decompiles to plain comparisons; `call` keeps the caller's r0-r15 and fp
  (register windows, expressed in the calling convention), arguments g0-g11, result g0, g14 = 0 at calls, stack grows
  upward (`fp = sp - 64` at entry). `tools\ghidra\run_arcade.ps1`: maincpu at 0, work RAM at 0x500000, main_data at
  0x02000000; functions from every `call` / absolute `callx` target plus the back-to-back walk: **2,201 functions,
  512 KB** -> `private\decomp\arcade\maincpu.c` + `maincpu_index.tsv`. Example: the set-motion routine 0x39180
  decompiles to `old = obj[0xd0]; obj[0xd0] = motion; obj[0xf8] = 0; obj[0xf4] = 0; obj[0xf6] = 0; obj[0xd2] = old;`
  plus three flag bits cleared. `tools\i960dis.py` stays the quick text disassembler.
- `tools\ghidra\install_specs.ps1` installs both into the private Ghidra copy (rerun after changing them).
- `tools\name_routines.py` (2026-10-13) gives **structural names** to arcade routines reachable from hand-named
  code: state-table entries (`<owner>_st<i>`, `<owner>_act<i>` when indexed by the record's action code), routines
  installed as an object's per-frame routine (`_n`, or `_case<N>` inside a switch), other code pointers (`_p`),
  helpers called only by one function (`_sub`). They live in a generated block of names.tsv and say where a routine
  sits, not what it does; hand names replace them as systems are read (`decomp_progress.py` counts them apart).
- Decompiled output stays in `private\decomp\` (derived from the user's copies, never committed); what is learned goes
  into this file and [formats.md](formats.md).

**Types and function boundaries in Ghidra (2026-10-21).** Both run scripts (`-Reexport`) now run, in order:
`FixNoReturn.java`, `FixSplits.java` (folds Ghidra "split" functions back into the function whose code runs into
them: 71 arcade, 1 PC; log `private\decomp\<version>\splits.tsv`), `ApplyNames.java` (a name on an address inside a
function becomes a label), `ApplyTypes.java` (structures, field names and parameter types from
[tools/ghidra/types.tsv](../tools/ghidra/types.tsv)). The object structure `Obj` (task header + the work area, size
0x3C8; the PC layout is the same with 8 more bytes at +0x10) is applied to every object parameter that
`tools/gen_param_types.py` finds (by the fields used, by object routines, and through calls: about 1000 arcade
functions and their PC pairs), so the C reads `obj->state`, `obj->motion`, `obj->flags` instead of offsets. Fields
named `wNNN` are per-kind work slots (their meaning per kind is in the comments and in the notes below).

Status values: **-** not started, **found** (entry points / data located), **partial** (main paths understood),
**done** (fully understood, ready to port), **n/a** (the version doesn't have it).

**Depth column (the exactness standard):** **N** named (every function of the system has the developers' or a
descriptive name); **T** typed (structs / globals in types.tsv, every offset used has a field); **E** exact (every
branch, constant, table entry, random-number call and frame count read, for each of A / PA / PM where they differ:
enough to write the routine 1:1); **D** differential check passed (A vs MAME, PA / PM vs the PC port, same inputs,
state equal every frame). "Spot checks" are the earlier single-value checks (replay dumps, viewer vs MAME): useful,
but not proof under this standard. The A / PA / PM differences per system: section "Three behaviours per system".

## Progress

| System | PC status | Arcade status | Exclusive to | Chosen version | Reason | Depth (N named / T typed / E exact / D differential check) |
|--------|-----------|---------------|--------------|----------------|--------|-------|
| Event script interpreter | partial (all 0x60 handlers paired) | partial (all 0x60 handlers paired) | - | **PC** (identical logic) + arcade GGCOL / GGSET / TEXCHG | same code in both; the PC turned GGCOL, GGSET, TEXCHG into skips (renderer-specific: arcade color rows / texture swaps), so those 3 come from the arcade when the arcade world is drawn; scripts from EVTALL.RBH (= the arcade's ROM scripts) | N yes / T no / E partial (handlers named, operands of most opcodes read; GGCOL / GGSET / TEXCHG details open) / D no (spot checks: spawn records and camera ranges vs the replay) |
| Camera (rail playback, CAMDR / CCTB, look offsets) | partial (CAMDR mode table 0x5D3888; camera-job table 0x5D8418) | partial (CAMDR mode table 0xAB484; CCTB instructions queue in a 16-entry ring, 0x2B180 starts the next job: routine = table 0xB00E0[sub-operation], 16 operand bytes to 0x51EB70; six job slots, routines at 0x51EAFC + 0x154 * n; 2026-10-13: CCTB 16 player flag bit, 17 re-run a setup of the current camera kind, **18 the players' own bodies act out cut-scene action n (table 0xC0C60)** (18 scenes, 2026-10-19 `player_sceneNN_*`: 1 photo, 2 three shots, 3 ladder, 4 fall on the backside, 5 / 13 knocked flying, 6 / 12 G reloads while Rogan shoots / kicks, 7 / 10 / 11 / 15 jumping down, 8 finish pose, 9 stances, 14 rattling a door, 16 run and jump, 17 dash; in 2-player games G (gman_*) gets his own motions), 19 camera preset (0xAB420), 21 sets 0x51F620, 32 camera offset for n frames (0xAB550); camera setups 0xAB460 = 3 kinds x 9: only CAMDR 1-3 and CCTB 33 modes 4-7 exist (`camdr_mode1..3`, `cctb33_mode4..7`: they set the players' routines, the camera routine 0x5209B0 and the bodies' walk motion), every other slot is the developers' "ChgCamMode not defined!!" trap; per-frame routines for modes 4, 6, 7) | - | open | job types 16-21, 32, 33 (switch camera mode m: setup 0xAB4A8[m], per-frame routine 0xAB3F0[m]; so `CCTB 33 4 0` is a mode change, not a look offset), 64 (play a path segment; start = end holds) exist in both | N yes / T partial (CameraRecord: eye, angles, look-at, job, operands, path frame) / E partial / D no (spot checks: rail = CAMALL.RBH exactly) |
| World drawing (draw zones, animated and fading world models) | n/a? (the PC draws its own world; zones not looked at yet) | partial (task 0x4B300, zone lists 0xC0BB0, BGTB sets the zone) | - | arcade (for the arcade world) | in the viewer since 2026-10-12 | N yes / T no / E partial / D no (spot checks: viewer vs MAME) |
| Enemies: spawning (ENTB* records) | partial (ENTB 0x46E5C8; object kind table 0x5D8118) | partial (ENTB 0x315C0; kind table 0xAFDE0) | - | open | same code; PC objects are 8 bytes larger at the front (arcade +0x58 = PC +0x60) | N yes / T yes (SpawnRecord, AttackSlot, enemy_count / enemies_alive) / E partial (evt_ENTB read whole; record +0x22 = hit points per the code) / D no (spot checks: positions and types vs the replay; hit points +0x22 verified in the dumps) |
| Enemies: behaviours and motions | partial (enemy_guard 0x4A5180: action code -> routine + motion; kind table 0x5D8118; set-motion 0x46A91C / 0x478F08) | partial (enemy_guard 0x550A0, same cases and motions; kind table 0xAFDE0; set-motion 0x39180) | - | open (identical so far) | kinds 71-74 exist only on the PC | N yes / T partial (Obj, Enemy, MotionStep, EnemyAttack) / E partial (enemy_ai, kind000_enemy_spawn, enemy_check_death, enemy_part_hits exact for A and PA incl. random numbers (A / PA differences: distances, shared kill credit); enemy_main, enemy_ai_motion, hit reaction read whole; the guard actions and the other kinds not yet) / **D for A, partly** (C++ port proven on captures: enemy_main, enemy_ai, enemy_ai_motion, hit reaction, check death, voice, footsteps, enemy_guard and most guard-action routines; docs/proof.md), PA / PM no |
| Bosses (Chariot, Hangedman, Hermit, Magician) | found (Chariot kind 37 0x515400, Hermit kind 40 0x4ED0D8, Hangedman kind 18 0x498F84, Magician kind 20 0x4D1484) | partial (Chariot kind 37 0x97880: type 35, armour-off type 36 via 0x99450; Hermit kind 40 0x85430: type 64; Hangedman kind 18 0x4EE70: 15 states, all read; Magician kind 20 0x70AB0: intro + 7 states, all named; Chariot and Hermit actions read) | - | open | all four located; the rest of the state routines still to read (notes below) | N yes / T partial (Obj; script blocks +0x37C untyped) / E partial / D for A (the whole-game differential, docs/proof.md: the Chariot, Hangedman, Hermit and Magician fights of the recorded game, every frame equal to MAME, 2026-10-27; a two-player Chapter 4 run with the Magician's fireballs hitting both players too, arcfix) |
| Shooting / hit detection | partial (hit spheres: table 0x53ECE4, copied to 0xD81798 by 0x47148C; radius scaled per creature type 0x5235F0, 0x476F0C; player_shoot 0x4365C4, shot_resolve 0x489C84, ray 0x414810) | partial (same table at maincpu 0x1359B0, built by 0x34410; gun board, trigger lockout, flash, ray, nearest hit: notes "Light gun and the shot test") | - | open | identical data; the PC shot test skips the arcade's scene-item lists | N yes / T yes (DepthEntry, ShotCandidate, ShotResult, BodyPart, Player flags) / **E for A** (gun cycle, player_shoot, shot_resolve, shot_hit_nearest, shot_ray_setup, ray_sign_test, shot_test_object / part / body, shot_targets_sort, shot_test_list_a / b exact; every coprocessor function used bit-exact: docs/coprocessor.md); **PA / PM: E** (the x87 runs at 24-bit precision: control word 0x107F, read with the PC harness) / **D for A** (C++ port proven on captures: player_shoot, shot_ray_setup, shot_resolve and the shot test; docs/proof.md), PA / PM no |
| Damage (enemies, body parts, dismemberment) | found (part hit handler 0x4C3D58; tables per creature kind: part class 0x523A58, replacement part 0x527344, part hit points 0x532F44, values 0x538D44; per-kind callbacks 0x5EEDD0; part blown off 0x4C4C60) | found (part_hit 0x68010 = PC 0x4C3D58; class variant 0x68680) | - | either (identical) | data tables identical (see notes) | N yes / T partial (BodyPart, PartRole; damage tables named) / E partial (part_hit, part_blow_off exact for A, PA and PM (PA / PM: falling-piece speed from the PC's own sin / cos); **E**: part_hit, part_blow_off, bleeding, the 11 per-type callbacks, part_hit_class (dead) exact for A, PA, PM (PA / PM: 9-task blood cap, PC sin / cos for falling pieces) / **D for A** (enemy_part_hits, part_hit; 330 captures; effects / blow-off / bleeding / callbacks still boundaries), PA / PM no |
| Player: lives, health, reload | found (player hit 0x436FE4 / variant 0x4370D4 with custom invulnerability; player blocks 0xD9F05C, 0xCC each: state +0, score +8, lives +0xC) | found (player hit 0x6410, add score 0x69B0; blocks 0x51F284: state +0, score +8, lives +0xC) | - | open | PC hit: score -100 directly (floored at 0); arcade the same through 0x69B0, plus a total-hits counter 0x51F618 | N yes / T yes (Player, PlayerTask, GunInput) / E partial (player_hit, invulnerability, lives floor, shake read whole; player states not all) / D no |
| Scientists / hostages, rescue bonuses, items | found (Sophie kind 16 0x50E318; rescue-scene humans kinds 24 / 25) | partial (Sophie kind 16 0x95EE0, type 34; kinds 24 / 25 = types 60 / 78, the 20-joint rescue-scene humans; **hostage scenes = kind 100** `hostage` 0x56460 (and 103): scenario = the record's action code, table 0xC1580 (26 scenarios), each a small state machine: the zombie threatens, the scientist is freed, thanks motion 0x113 / 0x116, **400 points** to the rescuer, counters 0x51F424 / 0x520796, HUD prompt state 0x51F4B4) | - | open | | N yes / T partial (Obj) / E partial / D no |
| Branching routes | partial (NEXTEV 0x4C1948; branch tables per chapter 0x5D3420; route choice 0xDA0CF2) | partial (NEXTEV 0x67A90; branch tables 0xAAF80; route choice 0x51F270) | - | either (identical) | branch tables identical in all four chapters (16 bytes per event: type 0 jump, 1 = pick by the route choice (up to 3 targets), 2 = next); the route choice is set by shootable scene objects (arcade 0x3A40 / 0x40B0 family: scene-object flags 0x51E940, e.g. objects in state 0x318 / 0x319 pick a target) | N yes / T no / E partial / D no |
| Scoring and ranking | partial (score = player block +8; ranking rewritten (mode 6/7 0x44D0C4); high scores scores.cfg per mode incl. boss modes 2-6) | partial (add_score 0x69B0 floors at 0; awards by caller (2026-10-13): **80** an enemy killed (`enemy_check_death` and the other enemy kinds' death routines), **120** a part hit that counts (`enemy_part_hits`), **400** a hostage rescued (kind 100 scenarios), 100 to both players at a hostage scene start, 300 / 1000 special objects (0x1E200, 0x3DA10), **-100** when the player is hit (`player_hit` and 0x6520 / 0x6640); ranking: mode 6/7 and the name entry at the end of the game, names in backup RAM 0x1D00104) | - | open | | N yes / T no / E partial / D no |
| UI: HUD | partial (lives / torches 0x50B5B0; sprite draw 0x4434B0 / 0x443810; sprites from sprite.spr.rbh) | partial (0x94740, same code; sprite draw 0x17E40 / 0x180B0; sprites from ROM) | - | **both** (switchable UI) | same layout code and positions (text-cell grid); the PC's sprite numbers are mostly the arcade's + 0x3B8 (0xBE -> 0x476, 0xAF -> 0x467, 0xB7 -> 0x46F), so one HUD code can draw either sprite set | N yes / T no / E partial / D no |
| UI: menus, options | found (front end in the main loop 0x419168, state 0x5CED98; menu 0x41DD3C; options / controls 0x427494) | found (operator test menu 0x84C0 and its pages, see the operator row) | - | **both** | | N partial / T no / E partial / D no |
| UI: title and attract screens | partial (mode 2 title 0x439280; modes 0 and 1 removed; demo prompt 0x41B3F8) | partial (modes 0 boot, 1 attract, 2 title, 3 demo) | - | **both** | | N yes / T no / E partial / D no |
| UI: text and fonts | partial (proportional sprite font: draw string 0x41AA54 (glyph sprite per character via 0x428494 / 0x4430A4, scaled to the window), sprintf 0x5943DE; ASCIISET.txt 0x412A84) | partial (fixed text grid: cursor 0x17660 (column, row), printf 0xA4EC0 -> 0xA4F50 onto the 2D layer; font in ROM) | - | **both** (switchable UI) | the arcade prints on a character grid (e.g. SCORE:%05d at column player*26+14, row 4); the PC draws free-positioned sprite text | N yes / T no / E partial / D no |
| UI: results and name entry | found (mode 6/7: own routine 0x44D0C4; high scores scores.cfg 0x425748 ...) | partial (2026-10-20: name entry read whole (3D letter grid, 20 s timer, banned names, backup RAM 0x1D00104); ranking screen mode 6/7 0x21DB0: two pages of 5, name / chapter / score) | - | **both** | | N yes / T no / E partial / D no |
| PC Mode | partial (characters with hit points, rounds, damage, reload time, hit size: table 0x5CE348, cstats.cfg, editor 0x426190; see notes) | n/a | PC | PC | only the PC has it | N partial / T no / E partial / D no |
| Boss select | partial (boss mode = front-end state 0x5CED98 = 4; boss 0xC723B8: 2-5 = one boss, 6 = all four in a row; game start 0x458588 takes chapter / event / route from table 0x5CD384 [boss 0xC723BC]: Chariot = chapter 1 event 19, Hangedman = 2 / 15, Hermit = 3 / 7, Magician = 4 / 4; timer via timeGetTime 0x412B14; per-boss high scores modes 2-6) | n/a | PC | PC | only the PC has it; it simply starts the event script at the boss's event, so it works with our interpreter | N partial / T no / E partial / D no |
| Settings (difficulty, lives, blood color, language, THOTD.CFG) | partial (same 5 game settings, block 0xD9F5A1, defaults 0x5D2A3D = 0, 2, 1, 9, 1, applied by 0x43BA54; options menu: Difficulty, Life, Continues, Blood Color, language; THOTD.CFG) | partial (operator game assignments, EEPROM copy 0x51F729, applied by 0xDC20: region 0-2 (JAPAN / USA / EXPORT) -> 0x51F791, difficulty 0-4 -> 0x51F490, attract sound, lives 0-11, blood color 0-3 -> 0x51F9C0; plus gun calibration) | - | **PC** menu, with the arcade's game assignments offered as PC options (user 2026-10-12) | same settings and ranges in both; lives setting n -> (start, max) from the same 12-entry table (arcade 0xD780, PC 0x4396BB): (1,3) (2,3) (3,3) (1,4) (2,4) (3,4) (4,4) (1,5) (2,5) (3,5) (4,5) (5,5), default 9 = 3 lives, up to 5 | N yes / T no / E partial / D no |
| Sound effects and music cues | partial (sound_cue 0x412000: cue table 0x5CBDA8, 12 bytes per cue: type 1 play sample, 2 stop, 3 CD track, 4 load bank `sound\O_%s.rbh` + play, 5 command) | partial (sound_cue 0x154D0: 16-entry queue to the 68000 sound board; outside modes 4 / 5 only if the attract-sound setting 0x51F9C1 is on) | - | **both** (switchable sound, user 2026-10-12) | same cue numbers in both; first the PC sound files + OGG music, later the arcade sound board emulated (68000 + SCSP) as the authentic option | N yes / T no / E partial / D no |
| Palette / color effects | n/a? | done (formats.md §24) | arcade? | open | | N yes / T no / E partial / D no (spot checks: 2630 of 2656 dumps) |
| Creature animation playback | done (the PC carries the arcade's tables and motion data: pointers 0x5EFBD0, frame counts 0x5F0658, joint counts 0x5EFA58, data from 0x5FD4F4) | done (formats.md §23) | - | arcade data (= PC data) | 672 of 674 motions byte-identical, frame counts identical; motions 3 (joints 2-16, up to 180°: maybe equivalent angles) and 213 (joint 19 up to 70°) differ on the PC: compare in the viewer; same motion numbers in both | N yes / T partial (Obj motion fields) / E partial / D no (spot checks: m2_anim.py check) |
| Random numbers | done (LCG state 0xDA10F0; helpers 0x46F5DC/0x46F618/0x46F664 pick 1 of 2/3/4 values; seed 0x471820) | done (same LCG, state 0x53E380, rand 0x34A50 returns bits 16-31, seed 0x34A40) | - | either (identical) | x = x * 0x41C64E6D + 0x3039; both seed 0 in SCNINIT when the attract demo runs, so demos replay exactly | N yes / T no / E yes / D no (spot checks: checked) |
| Main loop, timing (frame rate) | partial (frame tick 0x43B6E0 = mode table 0x5D19A0; called from 0x419168 at a fixed 60 ticks/s with catch-up, rendering decoupled) | partial (0xD900: vblank wait, counters, mode table 0xA9600; one tick per vblank, 57.524 Hz: teaser_status/rate.md) | - | **PC** (proposed) | same 60 Hz logic; the PC's fixed-step loop with decoupled rendering is what a PC rewrite needs | N yes / T no / E partial / D no |
| Attract demo (recorded play) | found (mode 3 0x45378C; "Press SPACE to skip Demo" 0x41B3F8) | partial (mode 3 0x23B40: `demo_start` / `demo_run`; demo list 0xAB0E0 = {chapter, event, frames}: Chapter 1 event 5 (1092 frames), Chapter 1 event 15 (1023), Chapter 2 event 9 (1389), then from the start) | - | open | both seed the random numbers with 0 for the demo, and the demo runs the ordinary event scripts from that event (players get 1 life); gun input: recorded streams at maincpu 0x100000 (notes below) | N yes / T no / E partial / D no |
| Continue / game over | partial (the arcade's credit logic kept, credits = the "Continues" setting: credits 0xD9F790 per player or shared (0xDA0B98), start cost 0xDA0B7C, continue cost 0xDA0B88, free play 0xDA0B8C; can-continue 0x45B8F0, use credits 0x448934; "Continues N" text 0x41B304) | partial (coins / credits with the operator coin settings 0xA140-0xAB70; continue countdown and game over read: game flow notes under "Game modes") | - | **PC** | no coins on a PC: continues as a setting (same code underneath) | N yes / T no / E partial / D no |
| Operator test menu (input / gun / sound tests, game and coin assignments, bookkeeping) | n/a | partial (2026-10-20: GUN SETTING and COIN ASSIGNMENTS read whole, EEPROM layout and checks, boot; TEST MENU 0x84C0, input test 0x9170, coin settings 0xA140-0xAB70, gun adjustment 0x16AB0 / 0x27780, play-time counters 0xCC40, EEPROM 0xD040) | arcade | arcade (keep) | only the arcade has it; its game assignments (difficulty, lives, ...) could become PC options | N yes / T no / E partial / D no |
| Title trigger codes (LRRLLR, RLRRLL, LRLLRR, LRLRLR, RRLRR, LLRLL, LLLRR) | found (0x43937C) | yes (0xD2C0, scoredisp 2026-10-26: the P1 / P2 **gun triggers** (the left / right gun: bit 0 of the pressed edges 0x51F518 / 0x51F534) at the title build the string; a code counts only when the whole code is typed (`str_starts_with` 0x5E480 (code, buffer)); code n = 6..1, LLLRR = 7 -> 0x51F42C. MAME proof: `tools/run_mame_title_code.ps1` (L/LL/LLL/LLLR 0, LLLRR 7 kept into the game, LLRLL 1) | - | either (same) | the code picks the player characters' creature types (P1 / P2 pair per code, table 0xCA690, seen in the cut-scenes; pair 7 = pair 0); code 7 prints SCORE:%05d (format 0x94730, row 4, column 14 / 40; cleared by 0x94CB0's 11 spaces) in play: the **score display** RUNESA described ("insert coins, Left three times, Right twice, then Start"). Ported: text_cursor 0x17660 / text_printf -> text_putc 0x17710 (debug font, palette 0 from palette_init 0x16AB0's ROM 0x168A0); Gameplay > Score Display sets 7 offline | N yes / T yes / E yes / D no |
| Region / version text | - | found ("AUSTRALIAN VERSION" 0xD550) | arcade | open | | N partial / T no / E partial / D no |

## Three behaviours per system (A / PA / PM)

Only systems where they differ; everything else is the same code in all three (the PC is a direct port). "Open"
= not yet compared to the E standard. PA and PM run the **same PC code**; they part only where it reads the mode
switch (next section).

| System | A (arcade) | PA (PC Arcade Mode) | PM (PC Mode) |
|--------|-----------|---------------------|--------------|
| Gun input and shot timing | flash cycle: a pull starts `gun_flash_phase` 4, the shot fires at phase 2 (2 frames later); 4-frame lockout per gun; pulls in phases 1-2 buffered | no flash; mouse / gun read directly (0x4358CC), shot the same frame; GunInput +0x16 = the reload input (A: the lockout byte) | as PA |
| Reload | trigger with the gun off screen: 6 rounds, sound 0xA01200 | own reload input: 6 rounds; PC cue 0x1C | own reload input: the character's rounds (+0x24) and **reload timer** (+0x2C ms, timeGetTime: wall-clock, not frames) |
| Rounds, ammo | 6; `ammo_enabled` always 1 | 6; PC-only infinite-ammo flag 0xDA09E8 | rounds from the character (`PlStartInit`); HUD ammo by the character (`hud_ammo_draw`) |
| Gunshot sounds | cues 0xA01208 (P1) / 0xA01207 (P2) | PC cues 0x1E / 0x1D | as PA |
| Player hit | `player_hit` 0x6410: -100 through `add_score` (floored), `hits_counter`, 90-frame invulnerability | 0x436FE4: -100 directly; own invulnerability variant 0x4370D4 | as PA, lives = the character's hit points (+0x20) |
| Lives at start | lives setting table 0xD780 | same table (0xD5B1D8 at run time; not in state 6) | the character's hit points |
| Bullet damage | part_hit steps as read; model changes, blow-offs and hit effects only in game mode 4 (not in the attract demo) | no game-mode test (the demo's hits change parts too); otherwise as A | as PA, and the damage x the character's damage (+0x28), truncated |
| Hit size | hit spheres x per-type factor | as A | x the character's hit size (+0x30) in 0x414AAC |
| Researcher's life gift (researcher_gift_spawn) | effect always; life to the player chosen by 0x5207B0 (0 / 1, 2 = rand() & 1) | life to the last killer (0xDA0AE8), no random number; effect only if a life was added | as PA |
| Kill credit when both players' damage counts (enemy_part_hits) | rand() & 1 (a random number is used) | the only player in play, else the previous killer (0xDA0AE8); no random number | as PA |
| Distances (enemy AI 70 / 40 / 25, attack positions, ...) | coprocessor function 0x1F: float32 sum, FRSQ + 2 Newton steps (1-2 ULP from sqrtf; tools/tgp_model.py) | table square root (3 copies: 0x4FC76C, 0x41347C, 0x4D7934; 256 entries, up to 0.39 % error; tools/pc_model.py) | as PA |
| Shot ray and sphere test maths | coprocessor: normalize / distance with FRSQ + 2 Newton steps (bit-exact models in tools/tgp_model.py), sin / cos from its interpolated table (cos 0 = 0.99999988), float32 | x87 at **24-bit precision** (control word 0x107F, read with the PC harness: every operation rounds to a float32 mantissa), table square root 0x41347C, sin / cos table 0x5697B0 (4096 steps, no interpolation) | as PA (plus the hit size) |
| Bleeding | part_bleed / blood_drip without limit | at most 9 live bleed + drip tasks together (`pc_blood_task_count`) | as PA |
| Shots and scenery | scene quads (lists a / b, coprocessor 0x62) are shot candidates: a nearer wall stops the shot; result code = the quad's attribute, flash unless 0 / 4 | no scene-quad test: shots pass scenery | as PA |
| Standard enemy turn (enemy_ai) | turn by delta / (1 + slow), slow = 60 (1 - cos) (60 when facing away): almost facing the target the enemy turns all the way at once | the same formula (PC matrix helpers, table square roots) but **slow is at least 4**: an enemy never turns more than a fifth of the way in one frame (proven with the PC captures, 2026-10-14) | as PA |
| Standard enemy step forward | enemy_ai ends with the step at the motion's speed (state 7: the lunge speed); every speed in the tables (0xC6B60 +4, 0xC6F10 +8) is 0.0, so it never moves anything; the enemy moves by its motion's root step (body_draw) | not in enemy_ai or enemy_ai_motion (dead code dropped); the same root step in body_draw 0x477B00 (plus: no root step for type 0x22 in chapter slot 5) | as PA (docs/pcmode.md row 12) |
| Enemy sounds | footsteps 0xA01225, hit / death voices from 0xC6880, headless 0xA01356 / 0xA01466 | PC numbers 0x32, table 0x5EEF98 (no padding word), 0x87, 0x90 / 0xFC | as PA |
| Event opcodes GGCOL / GGSET / TEXCHG | colour rows / texture area swaps | skip their operands | as PA |
| Continue | coins / credits, countdown 0x9FFF -0x2D per frame | credits = the Continues setting; no continue without credits; restart at 9 on a change; "Continues N" | as PA |
| Ending scenes | players' characters by the title code (0xCA690) | the characters 0x5CE4EC[p] (Rogan / G) through table 0x5CE358, no title code (also the cut-scene bodies, player_char_models_set 0x50C114) | the PC Mode character's type (`ending_p1_a` .. `ending_p2_b`; ReWrithe: docs/pcmode.md row 15) |
| After the game | 3D name entry (sub-modes 9 / 10), backup RAM 0x1D00104 | `mode67_ranking_pc`, scores.cfg | own PC Mode high-score table (screen 9) |
| Attract demo | recorded gun streams 0x100000, seed 0 | open (front-end state 6 looks like the demo: `evt_NEXTEV`, `PlStartInit`, `player_shoot`) | n/a |
| Main loop | one tick per vblank | fixed 60 ticks/s from timeGetTime with catch-up (resync after 500 ms / 30 ticks), a key toggles one tick per drawn frame | as PA |
| Region, operator settings, test menu | region 0-2, EEPROM, test menu | THOTD.CFG; no region / test menu | as PA |

Boss mode (front-end state 4) is a fourth PC path with its own branches (start position from table 0x5CD384, no
continue, Sophie removed, Magician variant 0x4D1EE4, game over): it is PC port behaviour, reproduced exactly too
(open whether it runs on PA rules: it does not set state 5). Ported on the arcade logic (pmrest): every state-4
branch below. Side find: the PC's ground_height 0x48A254 is only `fld [floor_y]; ret` (no floor search; its 18
callers get floor_y, where the arcade's 0x461D0 searches the floor quads): used for PC Mode's tumble ground test
0x4C5A10 (docs/pcmode.md).

### The PC port's mode switch (`frontend_state` 0x5CED98)

Values (confirmed with the PC harness, 2026-10-13): 0 menus, **2 = Arcade Mode game, 4 = boss mode, 5 = PC Mode
game, 6 = attract (with its demos), 7 = back to the menus**. 5 is written only by the PC Mode screen's start button
(0x425990, `mov edx, 5 ... mov [0x5CED98], edx`: no immediate store, which is why it was not found in the C). PC Mode data: character table 0x5CE348 (0x34 bytes: hit points +0x20
= 0x5CE368, rounds +0x24 = 0x5CE36C, damage +0x28, reload ms +0x2C, hit size +0x30 = 0x5CE378), character per player
0x5CE4E4[p] (0x5CE4EC[p] the non-PC-Mode one), 0x5CE4DC[p] used by the HUD.
Functions that read it (PC export 2026-10-21; each is a place where PA / PM / boss mode can differ):
- **5 (PC Mode):** 0x414AAC (hit size), `PlStartInit` 0x43557C (lives, rounds), 0x435518, `player_shoot` 0x4365C4
  (rounds, reload timer), `player_waiting_frame` 0x436B60, `mode67_ranking_pc` 0x44D0C4, 0x496F2C (character per
  player), `hangedman_death_fall` 0x49A03C, `ending_p1_a` / `_p1_b` / `_p2_a` / `_p2_b` 0x4AA52C-0x4AB148, `part_hit`
  0x4C3D58 (damage), `hud_ammo_draw` 0x50B7E4.
- **4 (boss mode):** 0x412BC4, 0x412C08, 0x4185BC, 0x418CC8, `pc_main_loop` 0x419168, `hud_continues_text` 0x41B304,
  0x436430, `game_start_position` 0x458588, `game_continue_begin` 0x458808, `game_continue_countdown` 0x4588A0,
  0x4D1EE4, 0x50BBF0, `sophie` 0x50E318, `game_over_start` 0x512FC8, `game_over_scene` 0x5130C4.
  **E notes (pmrest, 2026-10-27; all ported on the arcade logic as `Game::boss_on()` / `boss_event`):** 0x412BC4 the
  entrance (pc_boss_next = 0, clock shown, cue 12 / 19 by boss_index); 0x412C08 the win (result 0xC723C0 = 1, clock
  hidden); 0x4185BC the window's close message (state 4: the pause's timer stop / reset); 0x418CC8 the clock text
  "%02d:%02d:%02d" at (500, 440); 0x419168 the result: 1 = next boss (all four, index < 3) or IsHighScore -> name
  entry, 7 = the table, `while (pc_boss_next) frame_tick()` (the undrawn walk-up), the clock reset only for a single
  boss or the first of four; 0x41B304 no "Continues N"; 0x436430 the start button is not read (nobody joins);
  0x458588 chapter / event / route from boss_starts; 0x458808 / 0x4588A0 no CONTINUE, game over at once; 0x4D1EE4 the
  Magician's death: step 30, 120 frames, win (no flight home); 0x50BBF0 is really `hud_continue_sprite` 0x50BC24's
  test (no CONTINUE? sprite); 0x50E318 Sophie exits at once; 0x512FC8 the clock stopped / hidden; 0x5130C4 result 7
  at the 90-frame mark. Not in the export (found by scanning THOTD.EXE for the four clock routines' 38 call sites and
  `cmp [0x5CED98], 4`): `hangedman_death_fall` 0x49A03C (270 blink frames in state 4, else 180), the Chariot's death
  0x515DBD and the Hermit's 0x4EE9D9 (state 4: step 3, 200 frames, win), and every boss's entrance / clock start /
  stop (table in docs/teaser_status/bossmode.md). pc_boss_next's other readers: sound_cue 0x412000 (silent),
  tile_text_draw 0x41B1C0, evt_ENTB3 0x46E7FC (no spawn), cam_job64_play_start 0x461158 (path from end - 5, not in
  Chapter 4 after flag 213), the Chariot's landing 0x51564C (placed at 260 / 542), magician_fireball_frame 0x4D53A0
  (drawing only).
- **6 / 7 / other:** 0x4185BC, 0x41AD34, `frontend_input` 0x41DA18, 0x420A60, 0x420F70, 0x4358CC (gun read),
  `player_shoot` (6 with 0x5D7FB8), `player_waiting_frame`, `evt_NEXTEV` 0x4C1948 (6: 0x5CE654 = 4), 0x50C114.
Global reads only; a function that gets the value through a parameter or a copy (0x5CE340 = a saved state) is not
listed yet. Each of these needs its E-level note before PA / PM count as exact.

## Differential checker (plan)

Proof that a system is exact = the rewrite and the original reach the same game state from the same inputs, frame by
frame.
1. **State map.** One table (tools, generated from types.tsv) of what to compare per frame: the player blocks, game
   mode / sub-mode, event script PC / chapter / event, camera record, random seed (A 0x53E380, PC 0xDA10F0), the task
   list's objects (kind, routine, state, position, motion / frame, life, flags), attack slots, depth list, score.
   Addresses for A and the PC, with the PC's +8 object shift. Float fields compared exactly (the rewrite must use the
   same operations in the same order; the PC's x87 80-bit intermediates are a known risk: note per routine).
2. **Arcade side (A).** Start from what exists: `replay_hotd.ps1` replays a recorded `.inp` headless from a fixed
   NVRAM and dumps work RAM (0x500000, 1 MB); `fullgame_us` covers a whole game every 0.4 s, `ch1_run1` / `ch1_run2`,
   `ch3_us` part runs. Step 1 compares the rewrite at those sampled frames (same inputs taken from the `.inp`, first
   divergence found by bisecting with denser dumps); step 2 a Lua hook that writes only the state map **every frame**
   (small enough for a full game) plus the per-frame inputs as the game read them (`buttons_live`, gun_inputs). The
   rewrite runs headless from the same inputs and writes the same record; a diff tool names the first frame and
   field that differ.
3. **PC side (PA, PM).** Needs a way to drive the PC port with recorded inputs and dump its state per tick: a
   debugger-style harness or a DLL hooked into the tick 0x43B6E0 (input injection for mouse / keys, state map dump
   after each tick), run on a copy (C:\Games\HOTD stays read-only), with the timeGetTime-driven parts (main loop
   catch-up, PC Mode reload timer) replaced by a tick-based clock so runs repeat. To decide with the user (it means
   running a modified copy of the PC executable).
4. **Order.** Random numbers and the main tick first (seed equal every frame), then event interpreter + camera +
   spawns (the attract demos are ideal: fixed inputs, seed 0), then player / shooting / enemies in the recorded runs,
   then bosses, then PA / PM once the PC harness exists.

## Notes per system

(Filled in as the systems are worked through: entry addresses in both versions, data structures, differences, choice.)

### How the two versions relate (2026-10-11)

The PC port is a **direct port of the arcade program**: the same functions in the same order, the same globals, the
same tables, rewritten for x86 (the arcade's geometry-coprocessor commands, writes to 0x880000-0x884000, became
software math). So most systems can be read in either version and checked against the other.
- `tools\match_functions.py` pairs PC and arcade functions: seeds are the tables known to correspond (event opcodes
  PC 0x5EEAC0 / arcade 0xC6510, camera modes 0x5D3888 / 0xAB484, object kinds 0x5D8118 / 0xAFDE0) plus every other
  function-pointer table with the same length and zero pattern; then call sequences are aligned and globals paired
  by votes, and functions that use the same paired globals or the same numeric constants (arcade object offsets
  8 lower allowed) are matched (mutual best). **1,287 pairs** so far (of 2,408
  arcade functions); output `private\decomp\match_functions.tsv` / `match_globals.tsv`.
- Names: `tools\ghidra\names.tsv` (both addresses per name) is applied to both projects by `ApplyNames.java`
  (`run_pc.ps1 -Reexport`, `run_arcade.ps1 -Reexport`); `tools\decomp_query.py` searches the exports.
- Objects: PC objects are the arcade's with 8 more bytes at the front (arcade +0x58 = PC +0x60, +0xCC = +0xD4).
- Globals (arcade = PC): event script PC 0x52092C = 0xDA0AB8; chapter 0x51F9D1 = 0xDA0CF4; event index 0x51F9D0 =
  0xDA0CF3; player 1 block 0x51F284 = 0xD9F05C (state, 5 = playing), lives 0x51F290 = 0xD9F068; player 2 block
  +0xCC in both (0x51F350 = 0xD9F128); event skip flag 0x51F9E0 = 0xDA0ACC.

### Event script interpreter

- Dispatcher: PC `evt_run` 0x4C0DB0, arcade 0x66AD0: identical (first works out whether a player is in play, lives > 0
  and state 5, or the attract demo runs; then runs `handler[opcode]` until a handler sets the yield flag). Chapter
  start (PC 0x4C0CF0): script PC = event table[chapter][event].
- Opcode tables: PC 0x5EEAC0, arcade 0xC6510, 0x60 entries; every opcode has the same handler in both, field for field
  (names in `tools\ghidra\names.tsv`). Differences: **GGCOL** (0x4C) and **GGSET** (0x4D, color rows chosen by the
  blood-color setting from the lists at 0xC5A40) and **TEXCHG** (0x4F, texture swap via 0x34FF0) only skip their
  operands on the PC; the PC adds a debug print when events are skipped; PLTB, BRTB, EBLFNSH are no-ops in both.
- NEXTEV (PC 0x4C1948, arcade 0x67A90): next event of the chapter; at the end of a chapter's list the branch table
  (arcade 0xAAF80 per chapter: 16-byte entries, type 0 = jump, 1 = choose by the route flag 0x51F270, 2 = next) picks
  the next event list: this is where the **route branches** are decided.
- Choice: the PC's code (same as the arcade's) with EVTALL.RBH; GGCOL / GGSET / TEXCHG as in the arcade, used by the
  arcade renderer (the viewer's "rows" already emulate their effect from replays; now they can come from the scripts).
- **Ported** (2026-10-15, `src/port/event.cpp`, both versions; proven on A, docs/proof.md). What the code does, beyond
  the above:
  - Each instruction is an int32 opcode + int32 operands; the handler moves evt_pc itself. Lengths from the code
    (they correct three of evt_dis.py's guesses): 0 operands NEXTEV, SCNINIT, WAIT_CAMJOB, WAIT, CLRGFST; 2 SHDWLT,
    GGCOL, RNWCJ (state, camera jobs to add), EV3JP1 (flag, jump address); lists ended by -1: ET*, ENTB*, CLTB, KGTB;
    CCTB 1 + sub / 16 operands; SKPETO / SKPETU n, then a block ended by -1 (skipped unless evt_gmt < n / >= n);
    every other opcode 1. nop (opcode 0 and the unused ones, also PLTB / BRTB / EBLFNSH) does not move evt_pc: a
    script using one would hang.
  - The WAIT_* opcodes (except WAIT_CAMJOB) set evt_yield on their first call and test their condition on the
    following frames; they move on only while `evt_players_active` (set by evt_run: a player in play, or the attract
    demo) is set, so the script stops while nobody plays. WAIT_ENNUM / WAIT_ENALV also need `enemy_left_route`.
    WAIT_FRM counts in evt_wait_frames; WAIT_CAMFRM n waits until the camera frame (float) is past n, n = 0 until
    camera_frames_left is 0. WAIT stops the script for good. With evt_skip set (a skipped scene) the waits and CCTB
    pass at once.
  - CCTB only queues its instruction's address in a 16-slot ring (evt_cctb_ring, read by the camera task) and counts a
    camera job; CAMDR / CAMDR2 call the drive mode's set-up (camdr_modes), put the path job to nop and count the job
    done; CAMDR2 first waits while a player is out (not in play by player_state_routines +7 and no lives).
  - SCNINIT (each section's start): camera record 0 from scninit_camera at frame 0 and drive mode 3; resets counters
    and the shadow / light set-up; picks the input source (live, the attract demo's recording, mode 8's); appends the
    event to scene_history (16 bytes per chapter, 0xFF ends).
  - ENTB placement mode (record +0x20 & 3): 0 the record's position, 1 through stored matrix 1 (the camera), 2 / 3
    the record's x / z through it, then along the camera's up vector (matrix applied to (0, 1, 0) without
    translation) to the record's height: the enemy stands on a plane relative to the camera. ENTB3 adds record bytes
    +0x26 / +0x27 (object +0x320 / +0x324); ENTBE hands the object the rest of its record (+0x35C).
  - NEXTEV at the end of a chapter's list: chapter_ended, game_submode 5 (game mode 4), the next chapter's first
    event from the table entry before the current one (route_branches + event * 16 - 12), and the script goes on in
    event_lists[4] event 1 (shared code).
  - PC differences: event lists from EVTALL.RBH (pc_event_lists); the chapter start loads the PC chapter files and
    fades the music (no attract-mode section 0 there); CCTB writes the ring one slot further on (index pos + 1);
    WAIT_CAMFRM clears evt_yield when skipped; CHGSM / NEXTEV / skipped opcodes print debug lines; SCNINIT starts no
    sprite-clear task; NEXTEV sends no sound-board commands at a chapter's end; ENTB2 / ENTB3 / ENTBE skip kinds
    with no routine (debug line) and ENTB3 spawns nothing while boss mode moves on (pc_boss_next); ENTB places
    through the software matrices (pc_point_transform, the maths of pc_matrix_transform).

### Random numbers: every caller (arcade, 2026-10-13)

`rand` (0x34A50 / PC 0x471828, LCG on rand_state 0x53E380 / 0xDA10F0) is called from 153 functions; the order of these
calls within a frame (task run order, then shot_resolve, attack_slots_update ...) decides every later value, so each
system's E note lists its calls. Recorded runs give the calls per frame: `py tools/state_diff.py rand <state.rec>`
(LCG steps between frames; e.g. demo 1: about 30 calls, at most 2 a frame). Callers by system (calls in the function):
- **Shot path:** none (player_shoot, shot_resolve, shot_test_*, shot_hit_nearest). **Hits / effects:**
  `enemy_part_hits` (2), `part_blow_off` (1 per falling piece), `hit_effect_at`, `floor_blood_splat`, `eyeball_pop`,
  `eyeball_pop_spawn`, `muzzle_flash_spawn` (2), `body_drips_draw`, `kill_score_award`, `hud_popup_update` (2).
- **Enemies:** `kind000_enemy_spawn` (3), `enemy_ai` (7), `enemy_ai_motion`, `enemy_check_death` (2),
  `enemy_wait_camera_stop` (2), `enemy_voice` (0-1 per call, see "Enemies: deep pass"), `pick_target_player` / `_any`, `attack_queue_role`, kinds 6, 8, 9 (3), 10, 11, 12 (2),
  32 (`kind32_act2_hidden`, `kind32_throw_wait`, flying axe / held objects 5), 34-36 / 39 Murrers (`molb_leap_target_pick`
  3, `area_heading_pick` 11 ...), 65, 69 frogs (10), 70 (9), 96, 97 dogs (4), 132 / 141 / 142 / 153, bats 66-68 (17).
- **Bosses:** Chariot (7), Hangedman (2), Hermit (13 incl. `hermit_spit_ball` 3, `hermit_attack_hit`), Magician (14).
- **Hostages / researchers:** `hostage_s03_*`, `hostage_shot`, `hostage_gift_task`, `researcher_scream_penalty` (4),
  `researcher_gift_spawn`, `researcher_rescue_bonus`, `researcher_shot_by_player` (shooter by rand when both hit; was mis-named ZombieWalk2Tom).
- **Props and scenery:** ~40 prop routines (breaking pieces: `prop_break_pieces` 22, `prop_bookshelf_collapse` 9,
  `prop_piece_random` 8, `kind136_prop_group` 8, `kind145_wall_chips_spawn` 7, `prop_tumble_copy` 7 ...), `kind164_wing_wait` (6).
- **Player scenes:** `player_scene00_kousya_run_p_p1_p`, `player_scene16_run_jump`, `player_body_at_scene_pos`.
- Dead code: `unused_27e20` (16), `unused_30400`, `unused_30b50`.
Not the game LCG: the C library's own `crand_seed` 0x543AF0.

### Main loop and timing

- Arcade 0xD900: forever { wait for vblank (0xDA00); frame counter 0x51F428 += 1 (and 0x51F420, 0x51F794); call the
  game-mode routine `0xA9600[mode 0x51F494]` }. PC 0x43B6E0 is the same tick (frame counter 0xDA0B08, mode
  `0x5D19A0[mode]`, mode = the 16-bit value at 0xDA0CDC: frame_tick reads the dword at 0xDA0CDA and shifts it right 16; corrected 2026-10-13 with the PC harness).
- The PC main loop 0x419168 runs the tick at a **fixed 60 per second**: ticks due = (now - start) * 60 / 1000
  (timeGetTime), runs the missing ticks before drawing (resync after 500 ms or 30 ticks behind); a key toggles a
  "one tick per drawn frame" mode. So both versions have the same game speed; the PC only decouples drawing.

### Arcade object structure (from the developers' debug page, 2026-10-12)

The arcade program has a debug page for one object (maincpu 0x9C8B0; labels at 0x9C750) that names its fields. With
set_motion (0x39180) and formats.md §22-23. PC objects: the same fields 8 bytes further on.

| Offset | Name (debug label) | Notes |
|--------|--------------------|-------|-------|
| +0x00 | | per-frame routine (code pointer) |
| +0x20, +0x24, +0x28 | World.xp, World.yp, World.zp | position (float) |
| +0x2C, +0x30, +0x34 | World.xa, World.ya, World.za | rotation (16-bit angles) |
| +0xCC | | creature type (BO_ name table 0xC8A60) |
| +0xD0 / +0xD2 | | motion / previous motion (MO_ name table 0xC8BE0) |
| +0xF4 | FrameNum | motion frame; set_motion clears +0xF4, +0xF6, +0xF8 |
| +0x14C | Direction | 16-bit angle |
| +0x150 | Height | float |
| +0x3C5 bits 3-7 | NoBias, MotFinish, MotFinishUp, SplFinish, SplFinishUp | motion / spline finished flags (set_motion clears MotFinish) |
| +0x3C6 bits 0, 1, 2, 5, 6 | FixMode, Supplement, SupplementU, **WMotion**, DevideEnd | WMotion = the second motion (formats.md §23) |
| +0x3C7 bits 0-2 | DevideNoRep, Stay, StayUp | |

### Settings labels (maincpu 0xA9130)

Difficulty VERY EASY, MEDIUM EASY, NORMAL, MEDIUM HARD, VERY HARD; blood color RED, GREEN, BLUE, PURPLE; attract sound
OFF / ON; the 0-2 setting (0x51F791) is the **region**: JAPAN, USA, EXPORT. The in-game subtitle texts are at
0xCB000-0xCD000 ("Sophie!?", "Run! Run!", "Unlocked.", "Thank you!! Take this." ...).

### World drawing and draw zones (2026-10-12)

- World task maincpu **0x4B300** (created by 0x4B650 at stage start), each frame: per-zone light / fog settings
  (0xC0C10 entry chosen by `0xC0BF0[chapter][zone]`), then draws **only the placements listed for the current zone**:
  `0xC0BB0[chapter]` points to 0x32-byte lists of placement indices (0xFF ends a list) into the placement table
  `0xC0BD0[chapter]` (= formats.md §12's tables). Zone = work RAM **0x51FDD4**, set by the event opcode **BGTB n**
  (handler 0x66C60); zone 0 is an empty list.
- Placement +0x14 (pointer): a list of model IDs drawn one per frame (-1 loops): animated world models. Placement
  +0x10 / +0x12 (short): fade-in state and speed (state 0xFF = opaque), per-placement state in 0x543210.
- Chapter 1 special cases in the task: placements 0x1C-0x1E also draw 0x916 / 0x91A, 0x917 / 0x91B, 0x918 / 0x91C;
  placements 0x37 and 0x47 are turned 90 degrees about y after the translation (copro 0x15, angle 0x4000); Chapter 2
  placement 0x1B about z (copro 0x16, 0x4000). The arcade loader sets Placement::rot for them (visual, 2026-10-16).
- This is what swaps the mansion entrance (0x9E5 in zones 1-8 and 15, 0x9E4 in 9-11 and 14, 0x9E3 in 12-13) and hides
  everything not visible from the rail. **Viewer:** draws the arcade world by these zone lists (key Z, shot token
  `nozones`): during the script ride the runner's BGTB zone, with the free camera the zone of the nearest rail point
  facing the same way (zone spans from the scripts: every CCTB 64 segment with the BGTB zone before it). It replaces
  the replay-derived alternate-state groups for the arcade world and removes the floating pieces around the mansion.
- **Texture areas:** TEXCHG n (handler 0x67450 -> 0x34FF0) loads texture area n when it differs from the current one
  (work RAM 0x51F9B4): upload list `0xA99E0[n]`, colors `0xA9960[n]`, lights `0xA99A0[n]`; the areas are the ROM area
  blocks the viewer rebuilds (`rom:n`, formats.md §17). Chapter 1's scripts: event 4 TEXCHG 3 (entering the mansion),
  event 11 TEXCHG 3, event 12 TEXCHG 1, event 19 TEXCHG 4 (the Chariot); Chapter 2: 6, 7, 6; Chapter 3: 10, 8, 9, 11,
  11; Chapter 4: 10, 9, 9, 12; a chapter starts in its first set's area. **Viewer:** the texture set comes from the
  last TEXCHG along the rail (same zone spans) instead of the replay's positions, so distant models get the set the
  game has loaded there. The second argument of 0x34FF0 (-1 = none) is a cover sprite placed first, for the area
  already loaded too: `sprite_place(0x1004000, 1, 0, 0, 0, cover)`: 0xA1 (black) from chapter_load and the ending's
  kind111, 0x8D (white) from the prologue's kind107; kind104 / kind110 lift it. The runtime draws it since arcfix
  (2026-10-27): the prologue's area changes now flash white as in MAME instead of showing one frame of the old scene
  with the new textures.
- Chapter 1 uses zones 0-48 (event 19 ends with BGTB 0: nothing drawn).
- **Environment mapping (cluster 0xA07A0, 2026-10-15): present but unused.** Developer strings "EnvMapReq",
  "TexDataSize Limit Over(envmap)!!". `envmap_init` creates `envmap_task`, which each frame sends every requested
  object's normals through the geometry coprocessor (function port 0x880000, FIFO 0x884000), turns the results into
  texture coordinates scaled by the texture size and copies them to geometry address 0x808000 (texture data the
  display lists then use). Only one object is reserved for it (list 0x9F8B0): 0x1352 `jinmengyo_01`, the first of 40
  "human-faced fish" models (drawn plainly in Chapter 2). Nothing calls `envmap_init` or `envmap_request`; a
  developers' test object (`envmap_test_object`, buttons step spin / mode / object) has no caller either. Not in the
  PC (no strings). So run-time texture coordinates are **not** the cause of the Magician's vein animation (solved
  2026-10-16: model cycles, see "Part model swaps").
- **Scrolling textures (2026-10-16, `uv_scroll_model` 0x1E290):** 9 models (table 0x1B290: 0x915-0x91C
  `hashi02_07a..10a` / `hashi03_07a..10a`, the bridge-area models of the placements 0x1C-0x1E above, and 0x8A4
  `ge00a_wa01a`) are drawn from a copy of their texture points in a polygon-RAM buffer (0x51E190) whose v coordinate
  is shifted by `frame_counter % 1024` each frame (0x8A4 the other way): probably flowing water. `uv_scroll_flush`
  sends the buffer from `world_draw_task`. Units: the table 0x1B2C0 holds 32-bit points (value >> 5 = 13.3 fixed
  point), so the scroll is 1/8 texel a frame, 128 texels a cycle. **Viewer (2026-10-17):** the renderer has a per-model
  UV scroll (`Renderer::setUVScroll`, set in updateAnimation), but none of these 9 models is in a chapter's placement
  table (the bridge placements 0x1B-0x1E are 0x911-0x914), so the viewer doesn't draw them yet: next, find which code
  draws 0x915-0x91C / 0x8A4 and where (world_draw_task callers of uv_scroll_model), add them as placements.
- **Generated skins: the missing bellies (2026-10-16, `skin_draw` 0x214F0).** When object +0x3C4 bit 4 is set,
  `body_draw` builds a skin of polygons stretched between two body parts every frame: `skin_build` transforms the
  skin's corner points (main_data 0xF8B2A0, 0x90 bytes a skin) with two part matrices (+0x184 and +0x1D4), writes
  0x28-byte polygons into the object's slot buffer (0x51FDC8 + slot x 0x348), `geo_polygon_ram_write` (geometry
  function 5) puts them in **polygon RAM** at a running address (0x51E8CC), and an object header from 0x20F00 (by
  creature type) draws them. Skin number per creature type: 0x20E40 (28 skins; -1 none); polygon counts 0xF8D220
  (12-20). Type 16 (the bridge zombie) uses header 0x9F9CA / 0x9FA2A and types 17 / 19-22 / 49 (scientists) 0x410A2 /
  0x41102: **exactly the unknown "belly" objects of the 2026-10-10 investigation**, so this is the missing belly
  Most creature types have a skin (the Chariot, type 37 / skin 16, switches it with 0x543A28 in `chariot_draw_sub`);
  which object init sets bit 4 for the others is not traced yet.
  Sophie (type 34) gets two more strips (`sophie_skin_draw`, 2 + 4 polygons): her waist. Ported and proven 2026-10-21
  (src/port/blow_off.cpp; corners from the +0x2A4 part into the +0x274 part, drawn with +0x274).
  **Viewer:** to do: rebuild the skins from these tables (corner points + the two part matrices it already has).
  Read 2026-10-17 (not implemented): the polygon template per skin is main_data **0xF803A0 + skin x 0x640**, copied
  into the slot buffer at init (0x211F0): count x 10 + 7 words, i.e. a polygon-RAM model in the polygon ROM layout
  (0x28-byte polygons; textures and headers stay); `skin_build` only overwrites one vertex (+0xC..+0x14 of polygon
  map[corner], map 0xF8C260 / 0xF8CCE0, shared corners 0xF8D290) per corner with a coprocessor transform. Open: which
  of the two part matrices each corner uses and the result's space (Ghidra shows the geometry FIFO reads as
  0xd001a1a placeholders: read the disassembly of 0x21280, or dump polygon RAM in a MAME run at t01060 and compare).
  Sophie's strips: templates at 0x21640 (27 words) and 0x216BC (0x2F words), `body_init_sub1` 0x21800.
  **Solved 2026-10-18 (work RAM dump t01060, no MAME run needed):** the slot buffer base is the *pointer* at 0x51FDC8;
  the model starts at +0xC (6 words p0 / p1, then 10-word polygons; vertex index k = word 10k of the model: k = 0 is
  p0, else polygon k-1's first new vertex). The part-draw routines save node matrices into the object by the byte at
  maincpu **0x1184E3 + type x 0xF8 + node x 8** (node = pre-order index): **8 -> +0x184 (A)**, **9 -> +0x1B4 (B)**.
  The skin is drawn with B; corner i (main_data 0xF8B2A0 + skin x 0x90, n/2 corners) becomes **B^-1 x A x c_i**
  (exact to 1e-15 for the scientist, type 21: A = node 12, B = node 1; and the bridge zombie, type 16: A = node 1
  drawn as damage variant 0x7AF, B = node 9). Most types: A = 1 / B = 9; scientists and Sophie: A = 12 / B = 1.
- **The scrolling textures' work RAM (arcfix2, 2026-10-27, from the disassembly):** `uv_scroll_begin` 0x1E280 zeroes
  the buffer's fill count 0x51E800; `uv_scroll_model` writes the model's scrolled points at 0x51E190 + count x 8, its
  polygon-RAM address 0x808000 + count into 0x51E7D0 + 4 x (its index in table 0x1B290) and the new count. Drawing
  only (nothing in the game logic reads them); the runtime draws the water itself, so these stay 0 there (the
  whole-RAM differential's 0x51E190..0x51E52D / 0x51E7D9.. from the canal on).
- **Kept since arcpal (2026-10-27):** `Runtime::uv_scroll_model` (src/game/chapter.cpp) writes that buffer from
  world_draw_task (the offset is count x 4 bytes: the count is in words, 8 a polygon). The disassembly (0x4B434..
  0x4B488) shows what the decompiled C hides: before a placement is drawn (and its uv_scroll_model calls made) the
  game sends its position (+4, +8, +0xC) and its model's radius (0x2EEC390[model] +0x18) to **coprocessor function
  0x7B** (sphere visible) and skips the placement for a result <= 0. Without that test the runtime filled the buffer
  while MAME, the placement off screen, did not (Chapter 1 dump 7668: count 0x50 against 0). Each point: word 0 =
  frame_counter % 1024 + ((p0 & 0x1FFFFFF) >> 5), word 1 = (p1 & 0x1FFFFFF) >> 5 (0x8A4: word 0 unscrolled, word 1
  + 0x3FF - frame % 1024). Table 0x1B290 keeps each placement's pair together (0x1C: indices 2 / 3, 0x1D: 4 / 5,
  0x1E: 6 / 7).

### Palette RAM bank 2 and the grey look (arcfix2, 2026-10-27)

Palette RAM 0x1800000: banks 0 / 1 (0x1800000..0x1801FFF) are the 2D layer's (palette_flush 0x1AC70 uploads the
shadow 0x515CA0 there), **bank 2 (0x1802000..0x18027FF, 1024 words) is the polygon palette**: the base colour of
every Model 2 polygon (the "2D layer's" in older notes is wrong). Its writers, found with a MAME write tap on the bank
from power-on into Chapter 1 (and the disassembly):
- `palette_init` 0x16AB0 (the boot): area 0's texture palette list (*0xA9960: count, then RGB555 words) at entry 0,
  area 1's (*0xA9964) at entry 500.
- `texture_palette_upload_vblank` 0x1F70 (queued by texture_area_load 0x34FF0 through texture_palette_upload 0x2010):
  the area's list at entry 500 (lists are 116..434 long, so an area keeps the tail of a longer earlier one), then
  `vblank_words_upload` 0x24D70(0xA35C8): the 100-colour effect record downward from entry 1023, the palette
  animation's start values into work RAM (0x520662 = ROM 0xA35CC, 0x520664..0x52069F = 0xA35CE..: the same words as
  entries 1021 down) and entries 1023 / 984 = table 0xAB110[byte 0x51F9C0].
- `palette_anim_copy` 0x24E90 (every vblank: entries 984..1021, 959, 211, 689).
- `palette_grey_build` 0x1A400 saves entries 0-7 and 24-1023 into work RAM **0x51CCB0** and their grey
  ((r + g + b) / 3, even, all three channels, bit 15 kept) into **0x51D4B0**; `palette_bank2_upload` 0x1A300 (the next
  vblank) writes the grey copy to the bank, `palette_restore_vblank` 0x1A710 the saved one. So in the arcade's sepia
  scenes the polygons' base colours are greyed (and the colour tables rewritten to the sepia ramp), but the animated
  entries stay in colour: palette_anim_copy writes them every vblank on top.
- The Magician's colour word 0x180248E (entry 583: 0xFEC0 / 0x801F, a 16-bit `stos`).

The runtime keeps the bank (src/game/palette.cpp: `palette_ram_map`, `palette_init_bank2`, `palette_list_upload`,
`vblank_words_upload`) so that palette_grey_build's work RAM copies are the arcade's. The renderer still draws from
the asset palettes; since 2026-10-27 its grey look greys the base colour of entries 0-7 / 24-1023 like
palette_bank2_upload (src/render/renderer.cpp's vertex shader; the animated entries stay in colour). Chapter 1's
opening against MAME's sepia shot (private\colors\mame_grey1.png): the scientist's coat 120,76,52 = MAME's
120,76,52 (was 120,107,84, a grey-white coat). Entry 1023 (the blood of the bursts, drips and gore) is drawn from the
runtime's bank since arcpal (2026-10-27, src/game/view.cpp): the area load's 0xAB110 colour, and in the sepia scenes
its grey (palette_grey_build covers entries 24-1023; palette_anim_copy does not write 1023 back). MAME with the blood
colour held from power-on by a Lua script (the setting's EEPROM image byte 0x51F72D, and blood_color 0x51F9C0:
apply_settings reads the byte once at boot): entry 1023 = 0x801F / 0xB340 / 0xFC12 / 0xE01C for settings 0-3 from
the first area load on, 0xA94A (the grey of 0x801F) at 43.1 s, Chapter 1's sepia opening. The custom colour
(experimental) replaces the table's and is greyed the same way; a setting changed in the menus after the last area
load is drawn at once (the arcade cannot change it then).

### Game modes (top-level state machine)

Mode = arcade 0x51F494 (sub-mode 0x51F4C8), PC the u16 at 0xDA0CDC (corrected 2026-10-13; the sub-mode address 0xDA0CD6 to recheck the same way); per-frame routine
`modes[mode]` (arcade 0xA9600, PC 0x5D19A0), each a dispatcher over a sub-mode table. Arcade sequence in the
full-game replay (work RAM 0x51F494 over time): 0 at power-on, 1 attract, 9 after the coin (tutorial: it loads a
stage), 4 in game (sub-modes 2 -> 8 -> 10), 7 after the game (name entry), then 1 again.

| Mode | Arcade (routine, sub-table) | PC | Meaning |
|-----:|-----------------------------|----|---------|
| 0 | 0x16E80, 0xA9B48 | stub (jumps to 0x43BBBC) | power-on / boot (arcade only) |
| 1 | 0x23980, 0xAAFA0 | stub (0x43BBBC) | attract loop (arcade only; the PC has its own front end) |
| 2 | 0xD0E0, 0xA95D0 | 0x439280, 0x5D197C | title (trigger codes LRRLLR, RLRRLL, ... in the PC's) |
| 3 | 0x23B40, 0xAB0D0 | 0x45378C | attract demo play |
| 4 | 0x25C20, 0xAB1E0 | 0x458578, 0x5D363C | game (sub-modes below) |
| 5 | 0x971E0 | 0x512FB8, 0x5F2EFC | **game over** (corrected 2026-10-13: entered when the continue countdown runs out) |
| 6, 7 | 0x21D90, 0xAA460 | 0x44D0C4 (own 450-byte routine) | after the game: ranking / name entry (the PC rewrote it) |
| 8 | 0x26590, 0xAB280 | 0x45AC54 | ? (SCNINIT special-cases it) |
| 9 | 0x15C70, 0xA9A80 | 0x4409F0, 0x5D1ECC | tutorial (how to play) |

The PC's front end (menus, boss mode, options) runs outside this table, in the main loop 0x419168 with its own state
0x5CED98.

**Game flow (mode 4 sub-modes, 0x51F4C8; arcade, 2026-10-13):** 0 `game_new` (chapter / event 0, sound reset) ->
1 `game_chapter_start` (`chapter_load`: texture area per chapter / event from 0xAB0B0, the chapter's tasks from
0xAB210) -> 2 `game_play` (both players out -> 3) -> 3 `game_continue_begin` -> 4 `game_continue_countdown`: counter
0x52071C starts at 0x9FFF and drops 0x2D per frame, shown as the digit 9..0 (about 1.5 s per digit, ~15 s in all);
**pulling the trigger skips to the next digit**; a player who comes back in returns to 2; at 0 `set_mode(5)` =
game over. Chapter end: 5 -> 6 -> next chapter (1), or after Chapter 4 -> 7 `game_ending_start` (chapter 6 = the
ending's event list, event chosen by player count) -> 8 `game_ending` -> 9 / 10 name entry if a player ranks (names
stored at 0x1D00104 + rank * 12, the backup RAM) -> `next_mode` (table 0xA9630[region][mode]: what follows depends
on the region setting). Mode 5 (`mode5_game_over`): scene (360 frames, music 0xA01003), GAME OVER sprite 0x106 for
180 frames, `next_mode`.
**Player states (arcade, 2026-10-20):** the player block's state (+0) picks a start routine from table 0xA8790: 0 new
game (`player_st0_start`: character body from the title code, full lives, score 0), 1 continued (`player_st1_continue`:
**score + 1** - the last digit counts the continues -, the dynamic difficulty 0x51F492 - 2, a continue booked in backup
RAM), 2 / 3 rejoin / join, 4 out of lives (the player's own continue countdown while the other plays), 6 game over
(collapse motion, 120 frames, GAME OVER at the corner, then 9), 7 idle, 8 shoot only, 9 waiting (start check; demo /
how-to-play set-ups 4 / 5), 10 paused, 11 / 12 operator gun test. Set-up records 0xA8700 (`PlStartInit`).

**The game-over scene is a route map (2026-10-20):** a map of the mansion scrolls along the route the players took
(0x520720: per chapter 16 route entries; path records 0xC1810 + chapter * 0xF00 + route * 0xC0) while the players'
characters walk on it; at the chapter reached they collapse (motion table 0xA86E8). Names `game_over_*`.

**Name entry (arcade sub-modes 9 / 10, 2026-10-20):** a 3D screen, not sprites: 33 letter models in 4 rows (A-Z
0xADB-0xAF4, `. - ? !`, back 0xACE, forward 0xACF, END 0xAD0; table 0xC54A0) and per ranking player a gun cursor,
"YOUR RANK" with 1ST / 2ND / 3RD (else digit + TH) and three underscores. Pointing the gun at a letter (within 20 x
15 units) shows a highlight; a shot enters it and a copy flies to the slot (x = player * 280 + slot * 60 - 205);
back drops the last letter off the screen; at three letters only back / END count. A 20 s countdown (one digit per
120 frames) or END finishes; names go straight into backup RAM 0x1D00104 + rank * 12, then `name_is_banned`
replaces "SEX", any name of only X and spaces, or an empty one with three spaces. Per-player state 0x520AF0
(12 bytes: gun x / y, letters, rank, lock). The PC has its own ranking screen instead (sub-modes 9 / 10 empty).
PC (`game_continue_countdown` = PC 0x4588A0, 669 bytes): the same counter (0x9FFF, -0x2D per frame, a trigger pull
skips a digit), plus: in **boss mode** there is no continue (straight to game over); with **no continues left**
(can-continue 0x45B8F0) game over at once; the countdown restarts at 9 when a player's continue count changes; the
"Continues N" text per player (0x41B3EC); the chapter reached is stored per player (0xC72388 / 0xC723A4, for the
high-score table). Choice: PC logic (it is the arcade's plus the PC options); the arcade's coin path stays for the
"arcade" UI category.

**Mode 4 sub-modes side by side (2026-10-14):** PC table 0x5D363C vs arcade 0xAB1E0, same order:

| Sub | Arcade | PC | Difference |
|----:|--------|----|------------|
| 0 | `game_new` 0x25C40 | `game_start_position` 0x458588 | PC: chapter / event / route from the boss-mode table or the chapter select (0xD5B350), not always 0 |
| 1 | `game_chapter_start` | 0x45867C | same |
| 2 | `game_play` | 0x458720 | same; both run `game_frame` (arcade 0x262A0, PC 0x458B7C) |
| 3 | `game_continue_begin` | 0x458808 | PC: no CONTINUE prompt in boss mode or when no player `can_continue`; arcade always shows sprite 0x108 |
| 4 | `game_continue_countdown` | 0x4588A0 | PC rewrite (above): boss mode, continue limit, restart at 9, "Continues N" |
| 5, 6 | `game_chapter_end(_wait)` | 0x45870C / 0x458794 | PC also loads the next chapter's PC files (`pc_chapter_files_load`) and fades the music (`pc_music_fade_*`) |
| 7 | `game_ending_start` | 0x473CD8 | same choice of the ending event (2 = two players, else `solo_player_is_p2`); PC sets up its continue state (`pc_continues_init`) and starts the PC event task (`evt_chapter_start`) |
| 8 | `game_ending` | 0x473E04 | same; PC goes straight to `set_mode` (no region table) |
| 9, 10 | name entry 0x5D500 / 0x5D6D0 | 0x4B3DE4 (jumps to a `ret`) / 0x4B3DE0 (`ret`) | **PC: empty**: the PC enters names in its own ranking mode |
| 11 | (0 = end of table) | 0x4C0CF0 | no PC code sets sub-mode 11: just the next word after the table |

**Chapter / ending task lists (2026-10-19):** `chapter_load` starts the tasks of arcade table 0xAB210 (PC 0x5D3668,
right after the sub-mode table), `game_ending_start` those of 0xAC280 (PC 0x5D464C), same order on both: 0
`evt_chapter_start` (event task, routine evt_run), 1 `camera_task_start`, 2 `sky_task_start` (the sky dome; also a ranking task; the ending
uses `sky_task_ending_start`), 3 `input_task_start`, 4 `InitSetPlayerTask` (the PC's debug-string name), 5 `shake_task_start` (the hit screen shake: 48 frames after a player hit, the camera walk pauses meanwhile), 6
`world_init`; the chapter list adds 7 `attack_slots_task_start` (attack positions follow the camera), 8 `letterbox_task_start` (cut-scene bars), 9 `depth_lists_task_start`, 10 `demo_timer_start` (mode 3
only). Until 2026-10-19 the matcher had paired the PC chapter list with the arcade **test menu** (0xA8C50, same
length and zero pattern), so 11 PC functions carried test-menu names; now `match_functions.py` never pairs test-menu
code (66 arcade functions: the menu, its 12 items and what only they reach) and takes the hand pairs of names.tsv as
seeds (the weak global / constant votes had moved 57 reviewed pairs when the early pairs changed).

Both keep the arcade's play-time bookkeeping in "backup RAM" 0x1D00000 (`bookkeeping_play_end` /
`bookkeeping_game_end`; the PC emulates the address range).

**Attract demo input (2026-10-14):** the demos replay **recorded gun input**. `evt_SCNINIT` in mode 3 sets
`input_source` (0x51F5E8) = 1 and `demo_input_ptr` (0x51F600) = `demo_inputs`[demo_index] (pointer table at maincpu
0x100000: 0x100010, 0x104810, 0x109010, 0x4800 bytes = 1152 frames each); `player_input_update` (0x5DF0) then takes
16 bytes a frame, 8 per player: shot, on screen, fire enable, gun read phase (4 / 3 / 2, as the live gun board
does), x, y (screen, centred; 1000 = off screen). Demo 3 is a two-player demo (player 2 shoots too). Mode 8 does the
same with the stream 0xC5BC0 (chapter slot 4, event 0, 3600 frames): **mode 8 is the how-to-play demo** (2026-10-20): the lesson zombie (kind 130,
`kind130_lesson`) throws an axe at the demo player, who shoots, reloads off screen and kills it, with instruction sprites. The random
numbers are seeded 0 in the demo, so the run repeats exactly. **PC:** the code is kept (PC `evt_SCNINIT` 0x4C15B4,
mode 8 stream copied to 0x5EE3C4), and the demo pointer is still read from the arcade address 0x100000; THOTD.EXE
has no such table in its image, so either the PC maps demo data there at run time or its mode 3 demo is never
entered (to check: who calls set_mode(3) on the PC). The PC treats modes 2, 3, 8 and 9 as idle screens ("Press SPACE
to skip Demo", 0x41B3F8, not in mode 8; a key returns to the front end, 0x419168). Best of both: the remake can run
the arcade's demos (data from the ROM) in either version's front end.

### Shooting, hit spheres, damage (first look)

- Hit spheres: per creature type (94 types, 900 bytes each) up to 45 entries of {part ID, x, y, z offset, radius}
  (PC 0x53ECE4 in the code section, arcade maincpu 0x1359B0, **identical**; main_data 0xFDCF90 holds an older copy
  that differs in 491 words). 0x47148C / 0x34410 copy them into a per-part table (PC 0xD81798, 16 bytes per part ID).
  Radius x per-type factor (PC 0x5235F0) when a body is set up (PC 0x476F0C).
- Part hit handler PC 0x4C3D58 = arcade **0x68010** (corrected 2026-10-13; arcade 0x68680 is a variant that only
  handles one hit class): bit 1 / 2 = hit by player 1 / 2; per creature kind (0xDA0AD4):
  part class (0x523A58), the part model that replaces a hit part (0x527344: damage variants / dismemberment), part
  hit points (0x532F44), per-part values (0x538D44), a per-kind hit callback (0x5EEDD0), 0x4C4C60 = part blown off.
- **The damage data is identical in both** (checked 2026-10-11): part classes (arcade 0x1184E0, 8-byte records, 0xF8
  per creature kind; the PC keeps bytes 0-4 of each record, stride 0x9B: all 2,914 records equal), replacement parts,
  part hit points and per-part values (every 16-byte row of the PC tables 0x527344-0x53EB44 occurs in the arcade
  ROM), hit spheres. The arcade reaches them through per-kind pointer tables (0x135940, 0x1359A0, 0x14BC00), the PC
  through flattened arrays. `tools\diff_pairs.py` lists paired functions whose constants differ (noisy: the PC's
  structure offsets shift by varying amounts).
- **`part_hit_class` exact and dead (A 0x68680 / PC 0x4C4378, 2026-10-13):** part_hit restricted to the parts whose
  class equals the argument (same armour / replacement model / blow-off / damage / bleed logic and tables as
  part_hit, recursive over the children), but **without** the arcade's game-mode-4 test and without the PC Mode
  damage factor. Its only caller (`part_hit_class_all` A 0x68990 / PC 0x4C4608) is called by nothing in either
  version, so it never runs: nothing to reproduce beyond keeping the routine.
- **Per-type damage callbacks exact (`part_damage_type*`, table 0xC66A0, 11 routines, 2026-10-13):** each tests the
  part's new model number and (a) marks the body's `parts_lost` bits, flags the part (byte +0x28 |= 1) and starts a
  blood drip at its child part, or (b) (types 11, 16, 31, 32, 12, 24, 57 for their head models) parts_lost |= 7,
  every child of role 2 flagged + a blood drip at it, roles 3 / 6 flagged, the body's motion_flags3 bit 3 cleared;
  type 0 also swaps the child part's model on the last stage (by hit_type: 0 -> 0x84F, 8 -> 0x86C, 0x28 -> 0xB8A,
  0x4F -> 0x881, 0x50 -> 0x88C, 0x51 -> 0xB9F; child +0x79 = 1, +0x78 = 1). **The model numbers are the same in
  A and the PC** (all 11 compared; Ghidra shows some of them as `&DAT_0000xxxx` pointers). No random numbers.
  Differences: the PC moves body (b) into one helper (`pc_part_damage_flag_children` 0x4C4A70; the arcade repeats it
  inline), and its blood drips go through `blood_drip_at` = PC 0x486378 (capped by `pc_blood_task_count`, drip
  position through the PC's inverse-camera copy; the arcade's `blood_drip_at` 0x43510 transforms by the current
  coprocessor matrix and pops one level: the caller's push, to check). The old pairing blood_drip_at = PC 0x4C4A70
  was wrong (corrected in names.tsv).
- **Bleeding exact (A / PC, 2026-10-13):** `part_bleed_spawn` (A 0x43740 / PC 0x4864DC, (part)) and
  `part_bleed_start` (A 0x43770 / PC 0x486520, (hit record): the part at +0x20) create a `part_bleed` task (0x60,
  PC 0x68): timer +0x50 = 0, part +0x5C, the part's owner object (+0x40) at +0x58. `part_bleed` (A 0x435A0 / PC
  0x4863FC) each frame: ends when the owner's +0xC is 0 or the timer > 99; timer + 1; when timer % 20 == 1 (frames
  1, 21, 41, 61, 81): the part's view-space centre (+0x5C..+0x64) back to world space (A: coprocessor 0x74 slot 1
  (the inverse camera), 0x1A, 0x06; PC: its software matrix with the copy at 0xD52EA0), visibility test of a
  sphere of radius 3 there (A: coprocessor 0x7B; PC 0x4854D8), and if visible a `blood_drip` (A 0x44E70 / PC
  0x4862AC, 0x58 / 0x60 bytes, model 0x46A) at that world position. No random numbers. **A / PA difference: the PC
  counts live bleed + drip tasks (`pc_blood_task_count` 0x5D7F6C, +1 when started, -1 when they end) and starts
  no new one at 9; the arcade has no limit** (A / PA / PM row "Bleeding").
- **`part_blow_off` exact (A 0x69550 / PC 0x4C4C60, 2026-10-13, A from the disassembly):** (player p, part).
  New model for the part: damage stage byte +0x78 (signed) <= 1 -> the hit-sphere table entry of the part's role
  (+0x38): word at 0x13599C + hit_type x 900 + role x 0x14 (PC: same table, written as (role - 1) x 0x14 from its
  start); else word at 0x11E008 + hit_type x 0x200 + stage x 4 (PC 0x52735C). Then the node's byte at +0x28 + index
  (+0x30) of the body (+0x20) |= 1, `part_tree_detach_severed`, and for each detached subtree in the list 0x520AA0
  (PC 0xD9EC68), zero-terminated: part class (part_roles[hit_type x 31 + role] byte 3) == 1 -> a falling piece:
  `task_create(severed_part_fall, 0x3C8)` (PC 0x3D0: the +8 shift), +0x18 = body_current, `severed_part_init`,
  +0x94 = part_init_p[hit_type]; horizontal speed: **A** coprocessor push + identity (0x21), rotate Y (0x15) by the
  player's body yaw (+0x30 of the body at player +0xBC) + 0x8000, transform (0, 0, -0.2) (0x1A), pop (0x06):
  +0x78 = -x, +0x80 = -z; **PA / PM** the same with the PC's software matrix (its 4096-step sin / cos table, x87):
  the low bits can differ. +0x88 = 0xBCDF0123 (-0.02724, gravity), **one rand(): +0x7C = (rand() % 5) / 100 + 0.25**
  (A: single-precision divide then add; PC: x 0.01f + 0.25 on x87: **equal for all five values**, checked), +0x50 = 0.
  Part class != 1 -> `part_subtree_free`. No mode switch in it.
- **Severed parts (2026-10-16, cluster 0x69550):** `part_blow_off` 0x69550 (called by part_hit / part_hit_class
  when the hit class lies in the kind's blow-off range, table 0x135940) flags the hit node, cuts every flagged
  subtree out of the body's part tree (list 0x520AA0) and, where the part-class record's byte 3 (0x1184E3) is 1,
  turns it into a falling piece: `severed_part_fall` 0x69730 (gravity, drag 0.8, up to 5 bounces on the floor height
  0x51EA40 with a blood splat each, model 0x2C7 for 120 frames via `floor_blood_splat` 0x43920), drawn with the
  owner's current motion (`severed_part_draw` 0x38B10). Other cut subtrees are simply freed (they vanish).

### Light gun and the shot test (arcade, 2026-10-13)

- Gun board: serial-style I/O at 0x1C00012 (command) / 0x1C00014 (data) / 0x1C00018 (result); `gun_board_init`
  uploads the board's own program (0x1400 bytes from 0xA3A10). `gun_read` (0xA38D0) gives 10-bit raw positions and
  on-screen bits per player; `gun_to_screen` applies the operator calibration (offset + scale per axis).
- Per frame `gun_input_update` (0x46A90): trigger -> shot flag (0x5430A0 + player) with a **4-frame lockout**; the aim
  goes into the player block (+0x34 x, +0x36 y; 1000 = off screen). A shot starts the **screen flash**
  (`gun_flash`, counter 0x51F5EC = 4; the gun is read during the flash: that's how a CRT light gun finds the beam).
  A PC port needs neither: the mouse position is the aim (the PC's own input code replaced this).
- `player_shoot` (0x5A80): if rounds are left (+0x3D, 6 per load), sets "fired" (player +4 bit 1), builds the shot
  ray, impact effect, gunshot sound 0xA01208 (P1) / 0xA01207 (P2); trigger with the gun off screen = reload
  (rounds = 6, sound 0xA01200).
- `shot_ray_setup` (0x32CF0): ray from the camera through the aim point (focal length **496**, the projection used
  everywhere), stored per player at 0x53E2B0 + player * 0x68.
- `shot_resolve` (0x454E0), once per frame for each player who fired: candidates from the **depth list** (0x541470,
  filled by `depth_list_add` while objects are drawn: objects with hit spheres test their parts, others as a whole;
  only z < 0 is left out, so a NaN z joins with key 0: a thrown axe's first frame, found by the whole-game differential),
  plus scene items (`shot_test_list_a` / `_b`, shootable quads via `shot_in_quad`); `shot_hit_nearest` sorts them and
  sets bit 1 << (player + 1) on the nearest part (+0x3C) and its object (+0x58). The next frame `enemy_part_hits`
  turns those bits into damage (part_hit tables). The sphere centres are transformed by the geometry engine while
  the parts are drawn (part +0x5C..+0x64), so hits use exactly the drawn pose.
- PC, read and confirmed: `player_shoot` = 0x4365C4 (rounds and reload time from the PC Mode character),
  `shot_resolve` = 0x489C84 (tests only the depth-list objects), `shot_ray_setup` = 0x414810, `shot_effect` =
  0x50BF10 (matcher seeds). Matcher pairs, not read: `gun_flash` 0x436F94, `shot_test_object` 0x489D0C,
  `shot_hit_nearest` 0x489FBC. The PC's mouse path replaces gun_read / gun_to_screen.

- **`shot_test_list_a` / `_b` exact (A, read from the disassembly 2026-10-13; the two are identical except the
  list):** scene = *(0x520650); n = scene byte +0x60 (list a, ids at 0x51FE50) / +0x61 (list b, ids at 0x520250).
  For each id k: coprocessor **0x62** (k, ray origin, ray direction from `shot_rays[player]` +0x18 / +0x24) = ray
  against the quad list k of the coprocessor's data ROM (docs/coprocessor.md). Result <= 0: nothing. Hit: push +
  load the camera matrix (0x74, slot 0), transform the hit point (0x1A) -> view (vx, vy, vz), pop (0x06); append a
  ShotCandidate: +0 / +4 / +8 = vx, vy, vz, +0xC..+0x14 = the quad's plane normal, +0x18 target = 0, flags +0x1C
  |= 0x10 (scene item), +0x20 code = the quad's attribute word; sort key `shot_sort[2c]` = trunc(float32(vz x 10))
  (i960 `mulr` then `cvtzril`), `shot_sort[2c + 1]` = c; count + 1. No random numbers. `shot_hit_nearest` then
  gives a scene item the shot like any target: result code = the attribute, and the shot flash effect unless the
  code is 0 or 4. **So in the arcade a wall or scene quad nearer than an enemy stops the shot.**
  **PA / PM:** the PC's `shot_resolve` (0x489C84) tests only the depth-list objects: no scene quads, so a shot is
  never stopped by scenery (A / PA / PM row "Shot ray and sphere test maths").
  `floor_probe` (0x45FE0) uses the same function 0x62 on list b's first id with the ray straight down from y = the
  position's y (direction (0, -1, 0)); with no list b it returns the flat `floor_y`.

### Geometry coprocessor maths (arcade A behaviour, 2026-10-13)

The arcade does almost no game maths on the i960: distances, the shot ray, camera-space positions (obj view_pos),
matrices and even sin / cos go through the geometry coprocessor (Model 2C: Fujitsu MB86235 "TGP"). For **A** to be
exact, the rewrite needs each coprocessor function the game reads results from, computed the coprocessor's way.
- **The program is the game's own:** `copro_program_load` (0xA36A0, from `hw_init_3d`) sends 0x2000 words from ROM
  0x2F48000 (main_data +0xF48000) to the FIFO while the control word 0x980000 has bit 31 set (MAME
  `model2c_state::copro_fifo_w`: two words per 64-bit instruction). MAME's `unidasm -arch mb86235` disassembles it
  (4096 instructions; `private/copro/tgp_program.dis`, ROM-derived, not committed).
- **Calls:** the i960 writes n | n << 8 to the function port 0x880000 + n x 0x10 (or pushes the encoded word
  n << 23 | n << 8 | n straight into the FIFO 0x884000, e.g. 0x0D001A1A = function 0x1A), then the inputs to the
  FIFO, then reads the results from 0x884000. The dispatcher (0x198) checks the three copies of n and calls the
  routine stored at A-RAM [n] (table filled by immediates at 0x250..: `private/copro/tgp_functions.txt`; 0x90 slots,
  unused ones point to 0x196). At start-up the program also builds its sine table (step 2 pi / 65536) with its own
  series routines (0x1F0 / 0x1F6), so angle -> sin / cos is coprocessor arithmetic too.
- Functions used by the game code (call sites, i960): 0x05 (240), 0x06 (404), 0x0F (119), 0x10 / 0x11 (12 / 33),
  0x1F (82), 0x20 (29), 0x21 (88), 0x0C / 0x1A / 0x1B / 0x1C / 0x74 / 0x88 (pushed as FIFO words: shot_ray_setup and
  others), 0x52 / 0x6F / 0x71 / 0x7A / 0x7F / 0x83 / 0x84 / 0x87 (1-11 each).
- **Port status (user, 2026-10-13: option (a), each function as C++ in the same float32 order, bit-exact against
  the MAME probe).** Full description, algorithms, state, tools and obstacles: **[coprocessor.md](coprocessor.md)**.

  | Fn | Meaning | Modelled (tools/tgp_model.py, tgp_trace.py) |
  |----|---------|-------------------------------|
  | 0x05 / 0x06 / 0x21 | push / pop / push + identity | **yes** (state; proven through later reads) |
  | 0x07 / 0x10 / 0x87 / 0x88 / 0x7F | load matrix / identity / rotation / translation / push + load | **yes** |
  | 0x08 | reset the matrix stack | **yes** |
  | 0x09 / 0x76 / 0x77 | matrix products | **yes** |
  | 0x0A | 2D vector -> angle | **yes** (1273 / 1273) |
  | 0x0C | normalize | **yes** (1308 / 1308) |
  | 0x0F | vector -> yaw, pitch | **yes** (5919 / 5919; zero vector: MA0 quirk) |
  | 0x11 / 0x20 | read matrix / translation | **yes** (275258 / 80484) |
  | 0x12 / 0x13 / 0x14 / 0x15 / 0x16 / 0x86 | translate / scale / rotate X, Y, Z / Z-Y-X | **yes** |
  | 0x1A | transform a point | **yes** (60495 / 60495) |
  | 0x1B / 0x1C | sin / cos | **yes** (604 / 604 each) |
  | 0x1D / 0x1E | r sin / r cos | **yes** (0x1E 141 / 141; 0x1D not seen in the logs yet) |
  | 0x1F | distance | **yes** (6609 / 6609) |
  | 0x6F / 0x71 | matrix -> yaw, pitch, roll | **yes** (4045 / 14) |
  | 0x73 / 0x74 / 0x75 | stored matrices | **yes** |
  | 0x7A | inverse | **yes** |
  | 0x7B / 0x7D | sphere visibility / projection constants | **yes** (5180 / 5180) |
  | 0x89 | cosine between vectors | **yes** (1348 / 1348) |
  | 0x62 | ray vs quad list in the coprocessor's data ROM (shot_test_list_a / b, floor_probe) | **yes** (5077 / 5077, 4390 hits) |
  | 0x6E | 8 keyframed Hermite path curves (data ROM) | **yes** (5703 / 5703) |
  | division 0x1CD, arctangent 0x31A, sqrt 0x1D9 | internal routines | **yes** |

  **All 33 functions the game was seen to use are modelled bit-exactly** (attract traces from boot and over demo 1,
  a recorded game: 4.1 M calls, every output word equal; 0x1D modelled but not yet seen).
- **Function 0x1F = distance, modelled bit-exactly** (`tools/tgp_model.py`, checked on 5000 calls logged in the
  attract loop with `tools/mame_copro_probe.lua`: 0 differ): inputs x1, y1, z1, x2, y2, z2 (the game passes y = 0:
  a floor-plane distance); s = (dx^2 + dy^2) + dz^2 in float32 (d = second - first); root by routine 0x1D9: 0 -> 0,
  else y = FRSQ(s), two Newton steps y = (3 - s*(y*y)) * (y*0.5), result s*y. FRSQ is emulated by MAME as
  1.0f / sqrtf(s); real hardware may differ in the last bits (A here = the arcade as MAME runs it). The result is
  1-2 ULP away from sqrtf in 46% of the calls, so an IEEE sqrt is **not** exact.
  **PA / PM** use the PC's table square root `pc_fast_sqrt` (0x4FC76C) where the PC computes distances: a third
  variant, to model the same way.
- **Function 0x0C = normalize, modelled bit-exactly** (1272 of 1272 calls): s = (x*x + y*y) + z*z, r = FRSQ(s) + the
  same two Newton steps, result (x*r, y*r, z*r). Meaning (from the code and the probe): 0x1B = **sin**, 0x1C = **cos**
  of a 16-bit angle, from the coprocessor's own table with interpolation: **cos(0) = 0.99999988, not 1.0** in the
  arcade; 0x1A = point x current matrix (rows ((m0*x + m3*y) + m6*z) + m9), 0x06 = pop the matrix stack, 0x07 = load
  a matrix from the FIFO, 0x74 = load stored matrix n, 0x88 = set the translation, 0x0F = vector -> two angles
  (arctangent routine 0x31A, scale 65536 / 2 pi). The PC instead uses a 4096-entry sin / cos table (0x5697B0, angle
  >> 4, no interpolation) and x87 maths.
- **sin / cos (0x1B / 0x1C) modelled bit-exactly** (532 / 532 calls each; `tools/tgp_model.py` `tgp_sincos`): at boot
  the program fills three 128-entry tables with its own series (sin: n = trunc(|x| / pi) (the CFI conversion
  truncates), Cody-Waite reduction with pi in two parts, odd polynomial of degree 13; cos = the same shifted by
  pi / 2): coarse sin(j x 128 units), fine sin(i) and cos(i) units; a call splits the 16-bit angle into quadrant,
  coarse j and fine i and uses the angle-addition formula in float32 (sin' = cF*sC + sF*cC, cos' = cF*cC - sF*sC),
  then the quadrant's signs. That is why cos(0) = cos_series(0)^2 = 0.99999988.
- **Function 0x0F = direction -> (yaw, pitch), modelled bit-exactly** (3000 of 3000 attract-loop calls;
  `f0f_angles`): yaw = CFI(atan2(-x, z) x 65536 / 2 pi) (CFI truncates); h = z / cos(yaw) when (yaw + 0x2000) &
  0x4000 = 0, else -x / sin(yaw) (sin / cos of the integer yaw by the 0x1B / 0x1C routine); pitch = CFI(atan2(y, h)
  x 65536 / 2 pi); if bit 31 xor bit 30 of pitch is set: yaw + 0x8000, pitch = 0x8000 - pitch (never seen). Two
  int32 words out: yaw, pitch.
  **Arctangent (routine 0x31A)**, Cody & Waite: |den| below 9.7e-24 -> +-pi/2; q = |num / den|; q > 1 -> q = 1 / q
  (FRCP + three Newton steps), n = 2; q >= tan 15 deg -> f = (q sqrt3 - 1) / (q + sqrt3), n + 1; |f| > 2^-12 ->
  f + f g (p1 g + p0) / ((g + q1) g + q0), g = f^2 (p1 = -0.7205, p0 = -1.4400, q1 = 4.7522, q0 = 4.3202, float32
  constants in the program); n >= 2 -> negate; + {0, pi/6, pi/2, pi/3}[n]; den <= 0 -> pi - a; num < 0 -> -a.
  The routine also exits early on the multiplier's divide-by-zero / overflow / underflow flags after the first
  division (0 / +-pi/2 / 0): not seen in the logs, not modelled yet.
  **Quirk (A only): atan2(0, 0) returns the multiplier register MA0 unchanged**, so the yaw of a zero vector is
  whatever the previous coprocessor call left in MA0 (in the attract loop: 0, or 496.0 -> yaw 0x4EF03D), and its
  pitch is 0x28BE (MA0 = 1.0 left by the division z / cos). A bit-exact port has to carry MA0 across calls for
  this case (or prove the game never uses that yaw).
- **Division (routine 0x1CD) modelled** (`tgp_div`): r = FRCP(b) (= 1.0f / b in MAME, confirmed through 0x0F),
  t = b r, e1 = 2 - t, q = (r a) e1, e2 = 2 - e1 t, result e2 q (this exact order; 0x1D3 is the same into MA0).
- Next: model the functions whose results the game keeps (0x06 transform -> view_pos, 0x0C / 0x1A / 0x1B / 0x1C /
  0x74 / 0x88 in the shot ray, 0x20 / 0x21 / 0x10 / 0x11, sin / cos), each checked with the probe.

### Bosses: object kinds (arcade, 2026-10-13)

From the bosses' own events (PC boss-mode starts): the spawn records' first word is the object kind; for bosses the
record's word 9 looks like 0xA0000 | hit points (Hangedman 800, Magician 600 / 9999 for the intro).
- **Chariot** kind 37 (`boss_chariot` 0x97880), Chapter 1 event 19.
- **Hangedman** kind 18 (`boss_hangedman` 0x4EE70, PC 0x498F84), Chapter 2 event 15: set-up states (table 0xC1190),
  then the per-frame routine 0x4F000: bobs up and down (y +- 0.1 per frame), runs one of **15 attack / flight states**
  (table 0xC1150, state +0x5C; structural names `boss_hangedman_st0_n_st0..14`), draws, advances both motions.
  Kind 19 in the same event: `billboard_model` (model 0x14AC turned to the camera).
- **Hermit** kind 40 (`boss_hermit` 0x85430), Chapter 3 event 7.
- **Magician** kind 20 (`boss_magician` 0x70AB0, PC 0x4D1484), Chapter 4 event 4: state +0x70 (table 0xC69B0): 0 the
  intro, 1 the fight, whose per-frame routine 0x71010 runs **7 states** (`boss_magician_st1_n_st0..6`; fireballs and
  teleports still to read: sounds 0xA01463-0xA01475). Kind 22 in that event: `event_model_fade` (model 0x765).
- Read so far (2026-10-13): Hangedman state 4 `hangedman_flap` (MO_b2_bat_a / bat_c), 7 `hangedman_claw_attack`
  (MO_b2_hikkaki, hits the player), 12 `hangedman_dive_attack` (MO_b2_tobidasi + b2_glider, hits the player);
  Magician `magician_frame` (velocity + acceleration physics), 1 `magician_dying` (one of MO_boss4_sini1..4),
  2 `magician_descend` (entrance with the two title sprites), 3 `magician_next_pattern` (steps through an attack
  pattern table), 5 `magician_attack` (MO_boss4_cyopp1).
- **Chariot:** `chariot_frame` (0x97D20) runs an **action script**: sequences of 8-byte steps {action, a, b, c}
  (-1 = back to the sequence start; action 15 = pick the next sequence by its hit points against a, 16 = jump to
  sequence a); attack actions (3, 4, 6) wait until `combat_active` (camera stopped and someone playing). Part
  damage per part class: `chariot_part_damage` (table 0xCAC40). The action meanings and the script data (pointer
  at object +0x37C) are still to read.
- **Hermit:** `hermit_frame` (0x85700) uses the same action-script engine (sequence table at script +0x1A4); its
  branch actions 25 / 26 choose the next sequence by the distance at +0xA0 (100 / 150 units). One engine for both
  bosses: worth porting once.
- **Hangedman, all 15 states read (2026-10-14).** Set-up by the spawn record's action (table 0xC1190): 0 =
  `hangedman_flyby_init` (a fly-by earlier in Chapter 2, not a fight: states 1-2 fly between points around the camera,
  then up and away, and the object removes itself); 1 = `hangedman_fight_init` (Chapter 2 event 15: state 3, plus
  `hangedman_captives`, a woman (type 26) and a researcher (type 17) hanging from him who fall, MO_s_ochi);
  2 = `hangedman_rematch_init` (Chapter 4: state 14). Fight frame `hangedman_fight_frame`: aggression level
  (`boss_aggression_update`: +0x66 rises every 1200 frames divided by the number of players, falls by one each time a
  player loses a life), bobbing, `hangedman_hit` (damage x0.8 with two players; at 0 hit points state 13), the state,
  velocity + acceleration, draw. States:

  | State | Name | What it does |
  |---|---|---|
  | 0 | idle | nothing |
  | 1, 2 | flyby_start, flyby | fly-by only |
  | 3 | intro | Chapter 2 entrance: voice messages 0x1F / 0x20, title sprites, life bar, then 4 |
  | 4 | flap | wing beats (MO_b2_bat_a / bat_c), then 5 |
  | 5 | rejoin | retakes its enemy slot, then 7 |
  | 6 | hover | waits ahead of the camera (time from table 0xC11A0 by aggression), picks a player -> 7; in Chapter 2 below a share of its hit points -> 9 |
  | 7 | claw_attack | MO_b2_hikkaki, hits the player, then 8 |
  | 8 | return | back to the hover point, then 6 |
  | 9 | flee | Chapter 2: climbs away to a fixed point, then 10 |
  | 10 | wait_reappear | hidden until event flags (the camera moves on), reappears, then 11 |
  | 11 | dive_approach | flies to the start of a dive, then 12 |
  | 12 | dive_attack | MO_b2_tobidasi + b2_glider at the player, then 11 |
  | 13 | death_fall | MO_b2_oti, falls to the ground (`ground_height`), kill score, blinks 180 frames, removed |
  | 14 | rematch_intro | Chapter 4: 180 frames, life bar, then 4 |

  So the Chapter 2 fight has two phases: hover / claw (4-8) at the first stop, then after he flees the camera moves
  and he dives at the players (11 / 12) until he dies. In Chapter 4 a kill near the ground goes to state 8 first.
  Shared helpers found on the way: `voice_message` (n) / `voice_message_task` (voice line + subtitle: Japanese sprite
  in region 0, else text from 0xCB300), `boss_life_bar_task`, `pick_target_player`, `enemy_slot_take` (slots
  0x51F9F0), `obj_stop`, `player_in_play`, `players_in_game` (0x51F4A0), `player2_lives` (0x51F35C).
- **Magician, all 7 states named (2026-10-14):** set-up 0 `magician_intro` (the scene with Dr. Curien,
  MO_boss4_kyurian1; 9999 hit points), set-up 1 `magician_fight_init`. States: 0 idle, 1 dying, 2 descend,
  3 next_pattern, 4 `magician_fireballs` (flies to one of 4 points around the camera, table 0xC69F0, leaving
  `magician_afterimage`s, then throws `magician_fireball`s right / left / down, MO_boss4_hinotama r1 / l1 / d1, 2 per
  throw or 4 with two players; fireballs can be shot down and are slower with two players), 5 attack,
  6 `magician_barrage` (MO_boss4_saigoppe4s / saigoppeloop: a stream of shots from his body for 300 frames). Hits:
  `magician_hit` (x0.8 with two players), `magician_hit_reaction` (MO_boss4_daml_uder1). The pattern steps (object
  +0x35C) hold the per-step speeds, hit points and counts.
- **Chariot actions (2026-10-14).** The script comes with the spawn record (object +0x37C -> a 0x28-byte block
  made by `boss_chariot` from the record: +0x12 armour threshold, +0x18 distance table, +0x1C sequences, +0x20 point
  table). Actions: 2 land (entrance), 3 / 4 / 6 walk forward / back / either (`chariot_act_walk`, until the distance
  +0xA0 reaches the step's distance), 5 armour motion then walk back, 7 retreat (MO_b1_koutai), 8 / 9 / 18 / 11
  attack a / b / r / random (`chariot_attack_*` hit the player at the swing frame), 12 turn to a script point,
  14 face the players, 15 branch on hit points, 16 jump, 17 held attack (waits for combat_active), 19-26 hit
  reactions by part (MO_b1_hit_m / a / m / kr / kl / h / lr / ll, pushed back to the arena edge). Two phases:
  armoured (`chariot_armoured_hits`: only the weapon hand takes damage, 40 a shot; other shots spark) until the
  threshold, then `chariot_armour_break` (MO_b1_yoroi_1..3, the armour pieces fall as type 36 objects) and script
  sequence 7: unarmoured (`chariot_unarmoured_hits`: damage per part class, hit reaction by part, death MO_b1_dead).
- **Hermit actions (2026-10-14)**, same engine (script block +0x37C: +0x19C distance table, +0x1A4 sequences,
  +0xC hit-point threshold for the drop): entrance `hermit_entrance` (MO_b3_tojo_pose, out of the hole tojo_ana);
  1 pose; 2-7 walks (walk_a / walk_2 / random / back / by distance / guarded gwalk); 8-10 steps (backstep,
  backstep moving, frontstep); 11-15 claw attacks (atk_l / atk_r / atk_r2 / random / g2atk, the hit at a frame
  from table 0xCA0D0); 16-19 turns (rot_r45 / r135 / l45); 20 spit (MO_b3_eki); 21 lunge; 22-24 guard
  (guard_1..3); 25-30 and 35 branches (25 distance > 100, 26 three ways at 100 / 150, 27 the step's distance,
  28 / 29 random of 2 / 3, 30 hit points, 35 jump); 31 the drop (forced once below the threshold: MO_b3_oti);
  32 hit reaction (dam_a / dam_b); 33 / 34 held attacks waiting for combat_active. `hermit_hits_and_draw` runs in
  every action (damage, death, hurt -> action 32).
- **The bosses' PC forms (pmboss, 2026-10-21 / 10-27; docs/pcmode.md "The bosses").** Each boss file has its own
  copy of the PC's matrix / maths helpers block (transform at the base, +0xE8 pc_vec_angles, +0x1E4 translate,
  +0x404 / +0x50C / +0x698 rotate y / z / x, +0x83C table square root, +0xA54 identity, +0xAB0 mul, +0xC54
  mul_stored, +0x1418 matrix angles, +0x16B8 view test): Chariot 0x5134EC, Hangedman 0x497070, Magician 0x4CF570,
  Hermit 0x4EB224, kind 17 0x50C230. Ghidra's names in the Magician's file are shifted (`hangedman_claw_attack`
  0x4D2E78 is a Magician routine; `FUN_004d3dec` = the Magician's fireball aim: kind +0x334, count +0x33C, out
  +0x3A8 in the PC layout, stored matrix 1, x 0.0020161290; `FUN_004d3f9c` = its hit, `magician_fireball_hit`).
  Hermit: action 20 `hermit_act20_spit` 0x4EE4C0 (stored 6 = 0xD52F90, speed word 0x4EB1C0 = 1.0, scale x 0.3
  at 0x5A7074), view point 0x4EEBA3 (stored 5), mouth 0x4EF315 (offset 0x5F26C0). Chariot: act 14 0x516780, act 12
  0x5167CC, hit reaction 0x5169CC (push-back limits), flag 0xDA67FC. Hangedman: hover point 0x49CAD0;
  camera_path_eval 0x461980 chops the frame. The bosses' rand is the game's 0x471828 (the arcade's LCG); the C
  library rand 0x578A6B is the dogs' only. Disassembly of a range: a Ghidra headless post-script printing
  `getInstructions` over an address set, run read-only on a copy of `private\ghidra\hotd_pc` (the paths have
  spaces: call analyzeHeadless.bat from PowerShell, not Git Bash).
- Porting note: the Chariot and the Hermit differ only in their action tables; one engine with per-boss action
  tables would cover both (and the script data comes with the spawn record, so it is already in the event files).

### The Chariot (kind 37), as ported (arcade 0x97880..0x9AFB0, teaser L7, 2026-10-15)

`src/port/boss_chariot.cpp` (one member per routine, 56 routines); Sophie's scene and kinds 78 / 170 in
`src/port/sophie.cpp`. Facts read on the way (arcade addresses):

- **Spawn** (`boss_chariot`): type 35, motion 5; the part index 0x543A50 (the part of each of the 18 roles); draw hook
  +0x11C = 0x9A040; block +0x37C (40 bytes, mem_alloc): +0 frame-advance flag (`chariot_draw` adds it to +0xF8),
  +4 bit 0 hit box alive / bit 1 life bar alive, +8 = 3 when dead, +0xC unarmoured, +0xD / +0xE step / sequence,
  +0x10 / +0x11 the armour action's flags, +0x12 armour threshold (record +0), +0x14 the record's hit points (record
  +2), +0x18 distances (record +4), +0x1C sequences (record +12), +0x20 points (record +8), +0x24 the held action.
  Three tasks: the **hit box** 0x993A0 (a separate shot target at the weapon hand, scale +0x90 = 1.3; its +0x18 group
  head is the Chariot), the **life bar** 0x9AA30, and in Chapter 1 the **title card** 0x9A8C0 (+ sprite bank 0x3F,
  sprite 0xDC). The 17 armour counters 0x543A00 start at 0x8000 ("not set"). Counted as an enemy (0x51FB30,
  0x520792).
- **Entrance** (Chapter 1; later chapters start the fight at once): falls from above (placed at x 260, z 542 at
  camera frame 290; gravity -0.0273), landing dust 0xD39..0xD57 and sounds 0xA0125B / 0xA00002; MO 4 (sound
  0xA0125C) whose frame 38 sets **event flag 0x9C** (Sophie struck) with 0xA01344; MO 16 held 282 frames (0xA0131F at
  0x87 and 0xB5 left); then `chariot_combat_start` (attack slots, a random target player, **event flag 0x9F**, both
  shot targets live).
- **Script engine** (`chariot_frame`): when the action is done (+0x60 = 0) the next 8-byte step {action, a, b, c};
  -1 = back to step 1; 15 = to sequence (b if hit points <= a, else c; low byte), 16 = to sequence a (both loop to
  the next step at once). While `combat_active` is 0, walk actions 3 / 4 / 6 are held as 17 (MO 17 the pose) and
  restored when it turns 1.
- **Hits, armoured phase**: `chariot_armour_hit_test` finds the hit part (part +0x3C bits 1 / 2) -> hit position
  0x5439F0 (camera space) and effect class 0x5439E0 (`0x1357F0[0x11A6CB[8 x role]]`); a hit on the **hit box**
  (the weapon hand) does **40** (hit glow 0x209..0x263, hit reaction 19); any other armour hit only sparks (task
  0x9AE20, models 0x8A..0xA0, sound 0xA0120A). At hit points <= the threshold: `chariot_armour_break` (MO 19 / 20 /
  21; at MO 20's frame 3 every part with a model drops a falling piece (0x9ADD0 -> 0x99570: shown 80 frames, gone at
  118; model / keep from 0xCA7B0 by role, placed from the part's last matrix 0x543650 through stored matrix 1), the
  body is rebuilt as **type 36** (`chariot_armour_off`, draw dispatch 0x9A280 by role, table 0xCABB0), sounds
  0xA0131A / 0xA01230 / 0xA01231 / 0xA0131F), then script sequence 7.
- **Hits, unarmoured phase**: `chariot_part_damage`: per hit on a part, one plate less (part +0x79, its starting
  count from 0x97782 + 8 x role), the class's damage (0xCAC40 + 16 x class: 45 / 22 / 27 / 31 / 18 ...) added to
  hit_damage, blood; the 17 group counters recomputed (groups 0 and 9 share their part's plates with the next
  counter, one at a time, at random when both are above 0; 13 and 16 even out a left / right pair; parts with no
  plate left get hit radius 0). `chariot_unarmoured_hits`: once per reaction (flags bit 28), hit points down; at 0:
  **1500 points** to the shooter, MO 6, `chariot_death`; else reaction 20-26 by the effect class (sounds 0xA01345 /
  0xA01322). Reaction end (`chariot_hit_reaction_end`): sequence 15 (unarmoured) / 14 (armoured).
- **Actions**: walk (table 0xCAD60: motion, speed 0.1 / -0.2, footstep frames; until +0xA0, its camera distance,
  passes the step's distance), retreat (table 0xCAD78: **the speed is the table's first word read as a float,
  2.2e-12**, so the retreat does not move: an original slip), attacks (table 0xCAD80: motion, hit frame, side;
  `player_hit(player, 1, side)`, the player at random with two players, else 0x51F700), turn to a point (+ half a
  turn), face the camera, land (gravity 0xCAD40), hit reaction (pushed back from the camera to z 80; 60 after
  Chapter 1).
- **Drawing**: `chariot_draw` (shadow at the floor, the skin flag = counter 10 > 0, object_draw, camera-space
  position via stored matrix 5 into +0x98 / +0x9C / +0xA0, depth list). The part hooks keep the weapon hands'
  matrices (+0x184 / +0x1B4), place the **hit box** at (2.5, -2.1, -0.8) in the hand (class 8), keep every part's
  matrix (0x543650, for the falling pieces) and, unarmoured, pick each plate's model by the counters (0xCA920:
  chest with the beating heart 0x109A + frame % 30, waist, pairs with neighbour counters 0xCA8D0, extra pieces
  0xCAA00 / 0xCAA70 drawn as 0x10B8 + timer % 30).
- **Death**: sounds 0xA00002 (Chapter 1 only) / 0xA0131C, 0xA01223 / 0xA0123A at MO frames 0xCE / 0xFA; at the
  motion's end the attack slot is freed, the enemy counts go down, block +8 = 3 (the hit box ends), the title sprite
  bank 0x3F is freed; drawn until it leaves the view (coprocessor 0x7B, radius 20) and the life bar has ended.
- **Life bar** (`chariot_life_bar_task`): once camera_stopped (0x51F628) is 2, 20 frames, `life_bar_init(11, 1, hit
  points)`, `life_bar_update(hp, 0)` per frame, at 0 `life_bar_update(0, 1)` until it returns -1. **Title card**
  (`chariot_title_card`, Chapter 1): at camera frame 390 the camera's eye / look-at are saved, the game camera held
  (0x51FE24 = 1), sprites 0xDC / 0xDF placed (layer 0x1004000 at (33, 5) / (7, 5)); 180 frames on camera path 32;
  then everything restored and the sprites cleared.
- **Sophie** (kind 16, `sophie` 0x95EE0): type 34, MO 0x184 at (243.4, -0.5, 562.2); a chain of routines waiting for
  event flags 0x99 (scene), camera frames 90 (MO 0x18F) and 220 (MO 0x191), flag 0x9C (struck by the Chariot: MO
  0x182, 60 frames, then lying at (208.65, 579.97), MO 0x187), 0x9D, 0x9E (MO 0x188 / 0x18A / 0x18C), her counter 37,
  flag 0xFD (gone); every one ends at once when evt_skip_end is set. **Kind 78** (0x310B0): record x 61 frames, sound
  0xA01352 every 60. **Kind 170** (0x3520): the breaking glass at (248.5, 1.06, 565.08), models 0xD7B..0xDDD, sound
  0xA01101.

### Hostage scenes and cut-scene people (kind 100 / 103, arcade, 2026-10-19)

`hostage` 0x56460: the spawn record's action code picks one of 26 scenarios (table 0xC1580); kind 103 `hostage_b`
uses the same table without the counters. Each scenario = `body_init` with a start motion + a scene type (+0x5C),
then per-frame routines; the developers' motion names say what the researcher does.
- **0-15, held researchers.** The wait routine runs `hostage_shot` / `hostage_struck` and `hostage_captor_gone`
  (the captor = +0x390; when it dies the researcher is free and its killer (+0x2EB) is the rescuer). Rescue: run up
  (k_run_a), within 15 units (`hostage_camera_near`, or 30 for some), talk (head models swapped per frame for
  lip-sync, `body_role_model_set` role 1), **400 points** to the rescuer, counters `hostages_rescued` / 0x520796,
  HUD prompt `scene_state` 5 -> 6, then leave. Gifts: scenarios 8, 11, 13, 15 (and the dropped researcher of 3) hold
  out model 0x313 and start `hostage_gift_task`: **an extra life** (`player_life_add`), or **+300 points** at full
  lives. Messages: scenarios 4 / 14 hold item 0x71C and play voice message 12 / 36; scenarios 1 and 14 set
  `route_choice` = 1 (the rescue changes the route). Start poses: 0 backing away (no captor, 100 points to each
  player), 1 hands up, 2 / 9 fleeing, 3 carried (thrown at the camera by the carrier's motion 0x216: `player_hit` if
  he arrives; shot: -100 and he dies; carrier killed: dropped, gets up, rescued), 4 / 10 / 14 / 15 backing away, 5
  held and walked (his zombie walks him to Thomas Rogan: ZombieWalk2Tom = guard action 20), 6 hanging (climbs up when freed), 7 crawling, 8 curled up (gone at camera frame
  221 if nobody reached him), 11 reluctant ("iyaiya"), 12 in a cell, 13 riding a platform down.
  Voices by sex (`hostage_is_female`: male researchers are types 17, 19-22).
- **16-25, cut-scene people** (removed when the scene / event number changes): 16 a person playing the record's
  motion (Sophie in the ending), 17 Thomas Rogan's scene (`Tom_Adv_00_0`), **18 / 19 and 21 / 22 the players'
  characters in the ending** (types from 0xCA690 / 0xCA694 indexed by 0x51F42C, timed to camera paths 0x75-0x7C and
  frames, lip-sync heads 0x748.. / 0x283..), 20 motion once, 23 talking head (models 0x1CEF..), 24 a walking zombie,
  25 drawn until frame 160.
- Porting note: the scenario code is mostly the same few steps with different motions / thresholds: a data table
  (start motion, scene type, free condition, rescue steps, gift / message / route flags) would cover 0-15.

### Agile creatures (kind 70, arcade, 2026-10-19)

One engine for Saruzou (monkey types 1 / 9) and kage (38), Rubin (18), Drake (37), Moody (59, 92, 93) and kageta
(68, 87). `kind70_saru_saruzou` reads the spawn record (type, action code, route byte) and runs one of 13 set-ups
(table 0xCA5C0: jump in, jump across, eating, waiting, wall attack, fade in, hanging upside down ...); every frame
`kind70_frame`: `kind70_hits` (life +0x70; death cry, hurt cry), hit reaction (ground: motion per hit part from
0x14B900; airborne: slips and falls), the state routine (table 0xCA540, 30 slots: approach, think, wall moves, bite
on the player, leap, stance, Drake's knife throw with a flying knife object, roll, Moody's wall / ceiling moves and
throws), velocity integration, draw. `kind70_st03_think` picks the AI per type (monkey / kage, Rubin, Drake, Moody,
kageta). Chapter 1 event 4: a creature's route byte sets `route_choice` when the last hostage is pending.

### Kenfis dogs (kind 97) and attack positions (arcade, 2026-10-19)

Dogs come in packs: a 0x2C-byte record per pack at 0x51FB44 (index +0x2E6) holds the pack's step, target player
(alternating between the players in 2-player games) and motion choices. Entrances (table 0xC9730): pack AI, run in,
jump in, look around. `dog_frame` per frame: dust on landing frames, knock-back, `dog_hits` (reaction by the side
hit; death motion by the camera angle), state routine (table 0xC9750: 0 bite (`player_hit` at frame 42 of each
bite loop, then switch target and jump off), 2 leap in, 3 circle on a waiting position until the camera is within
50). **Attack positions:** table 0x51F9F0, 16 entries of 0x14 bytes (flag, object): slots 0 / 1 in front of the
player, 2 / 3 waiting; objects keep their slot in +0x2E7 and free it with `attack_slot_free` (chapter task 7 clears
the table).

**The dog helpers (2026-10-15, src/port/enemy_dogs.cpp).** The pack "record" is per dog in practice (+0x2E6 is the
enemy slot), and a dog's life is 1 (dog_full2: every hit killed): `dog_knockback` (pushed `step` away from the camera
for 4 frames after a hit) only runs after a hit that leaves the dog alive. `dog_st2_move` (state 2 with two players):
the point (+-8, 0, 5) in the camera's frame (camera_records + 0x10 = record 0's yaw), turned toward at 0x100.
`dog_angle_to_target` / `dog_st3_steer` (state 3): the point in the dog's own frame / steering toward it at a turn
rate of 6553.6 / ln(d)^2 (angle_turn_toward written out). `dog_set4_path`: rate 36 / ln(d)^2 x 65536 / 360.
`dog_set3_fall`: the jump speed sqrt(-0.0544 d^2 / (2 cos (cos (ty - y) - sin d))), the arcade's only use of
coprocessor 0x2D (square root). **A / PC differences:** the arcade takes ln with the i960's `logrl` (MAME: double,
t2 x log(t1) / log 2 with t2 = ln 2), rounds it to a float and squares it as floats; the turn rates of set-up 4 stay
in double until `cvtzri`; the PC uses x87 `fyl2x` (not rounded by the 24-bit precision control), squares at 24 bits
and divides by the double constants at 24 bits, then `pc_float_chop` + `fistp`. The PC computes the jump speed as
d (d x -0.0544) / (A (2 cos)) with the table square root written out inline, and the steer's step through
move_along_heading (the arcade turns (0, 0, speed) through the coprocessor itself) after clearing the heading's
upper 16 bits. The PC versions use per-file copies of the matrix helpers (shared helpers file 0x445xxx, suffix 11;
file 0x4C1xxx, suffix 12; byte-for-byte the generic routines).

### Enemies: deep pass (2026-10-13)

- **`enemy_main` exact (A 0x77410 / PC 0x4D9A90):** (1) flags bit 21 and route_choice == 1: the enemy leaves:
  0x51F64D = 1, flags bit 0 cleared, its target player's `player_targeted` cleared, target = -1, attack slot freed,
  enemies_alive - 1, enemy_count - 1, `body_free_parts`, `enemy_free_extras`, task_exit (PC: the same body in a
  helper 0x4D9694). (2) `enemy_footsteps`. (3) action 1 with parts_lost bit 1 (head gone): types 32, 58, 63, 74, 85
  -> action = 0 unless flags bit 31; types 30 and 84 first clear flags bit 19 and count 0x51FE34 (PC 0xDA0BBC) down,
  at 0 a sound (A 0xA01356 / 0xA01466, PC cues 0x90 / 0xFC: the PC's own cue numbers), then the same. (4) hit_type =
  type, hit_class = 0, hit_damage = 0; `enemy_part_hits`, `enemy_hit_reaction`, `enemy_check_death`; flags bit 0 ->
  `enemy_ai`. (5) flags bit 23: pos y = camera eye y - 15 (PC: + (-15.0), the same value); `shadow_setup` (floor_y
  otherwise). (6) `object_draw`, anim_time + 1, motion2_time + 1 when motion_flags2 bit 5. (7) type 3 in motion 499
  with 9 < anim_time < 100: an extra effect model (anim_time + 0x200) drawn through the coprocessor (draw only).
  (8) attack_slot > 1 -> `attacker_list_add`; flags bit 0 -> `enemy_ai_motion`, `depth_list_add`. Random numbers:
  none of its own (in order: enemy_part_hits, enemy_check_death, enemy_ai, enemy_ai_motion, all exact). No mode
  switch: A = PA = PM apart from the sound cue numbers.

- **`enemy_footsteps` exact (A 0x78F50 / PC 0x4DB344):** unless motion_flags3 bit 1: motion 0x1E3 at frames 10 / 40,
  motion 0x20C at frames 20 / 40 -> footstep sound (A 0xA01225, PC cue 0x32). No rand.
- **`enemy_hit_reaction` exact (A 0x777F0 / PC 0x4D9D28):** when hit_class != 0, flags bit 31 clear and state not 7 /
  8: motion = enemy_hit_motions[type][action][hit_class % 9] (A 0xC70C0, PC 0x5EF768), blended (`set_motion_blend_hold`
  (obj, motion, 0, 1, 20)) when hit_class % 9 < 5, else `set_motion`; flags bit 28 set; `enemy_voice(obj, 0)`.
- **`enemy_voice` exact (A 0x6BF30 / PC 0x4C631C, newly paired):** (obj, kind 0..4); creature types 3, 4, 12, 16,
  30-33, 45-47, 51, 53, 58, 63, 65, 71, 72, 74, 77, 84, 85, 89 use the second voice set (same list in both).
  **Random numbers:** kind 0 (hit): one rand(), voice = table[rand() & 4] (A 0xC6880 / PC 0x5EEF98: only entries
  0 and 4 can be chosen: a quirk of the original, kept) plus a second sound; kind 1 (death): one rand() (% 2) for
  the first set, none for the second; kind 2: none; kind 3: one rand() (% 2); kind 4: none. A = PA = PM (sound
  numbers are each platform's own).

- **`enemy_guard` (A 0x550A0 / PC 0x4A5180) compared case by case (2026-10-13):** the same action -> routine table
  in both versions (actions 1-20). The comparison fixed four pairings: actions 14 and 17 / 18 had their PC routines
  swapped (PC 0x4A4980 = action 14: roar 0x23C, attack 0x24A; PC 0x4A4B10 = actions 17 / 18: 0x26C / 0x25A / 0x25B),
  action 12 = PC 0x4A507C, action 15 = PC 0x4A4CF0; and actions 13 and 16 are the PC routines with the developers'
  names `ENGuardZombieRunAfterResearcher2` (A 0x54090) and `ENGuardZombieRunAfterResearcher_St3_00` (A 0x54290),
  now used for both versions. The per-case motion numbers and the actions' own routines: next.

### Enemy behaviours (first look)

- **Bats (kinds 66 / 67 / 68, 2026-10-20):** not skeletal creatures: one model per wing frame (`PN_bat00a`.., 0x54 +
  frame), hanging bats shake through `PN_bayure00a-19a` (0xA72-0xA85), a shot bat falls as `PN_bat_dam00a` (0x68). Kind
  66 = Chapter 1 event 9, kind 68 = Chapter 2 event 2, kind 67 = the Hangedman's swarms (20 bats in formation around
  him, 30 in Chapter 2 event 15; all fall when he is beaten; when the last bat of a wave is shot his attack pattern
  advances, table 0xC1124). States: hanging, take-off, circling, dive at a player's face (`player_hit`), shot (80
  points), falling. At most **3 bats dive at once** (0x5207A0). Names: `bat_*` in names.tsv.
- Enemy spawn records (ENTB*) create a task of the record's **kind** (object-kind table PC 0x5D8118 / arcade 0xAFDE0;
  named `obj_kindNN` in names.tsv). Ordinary enemies are kind 99 (`enemy_guard`, PC 0x4A5180 / arcade 0x550A0; the
  debug strings call it ENGuard) and 98.
- `enemy_guard` stores the creature type, resets per-enemy slots, then **switches on the record's action code**
  (record +0x22, object +0x64 arcade / +0x6A PC) and sets the enemy's per-frame routine (object +0) and its first
  motion, often per creature type: action 1 = walk along (routine 0x55FA0 / 0x4A3364, motion from the record), 2 =
  attack a target (type 0x49 -> motion 0x289, type 3 -> 0x22F, others 0x284), 3 = timer 0xB4 frames, 4 = idle,
  5, 6 = chase (motion 0x288), 7 = 0x228 ..., 8 ...: **the same cases and the same motion numbers in both versions**
  (the PC keeps some motion numbers in data words, e.g. 0x5E9E64). This is the behaviour -> motion mapping the
  playable slice needs (formats.md §23 motions).
- **The Murrer (kinds 34-39, creature type 6 `molb`; 2026-10-16, cluster 0x65050):** crawlers that follow a quadratic
  B-spline path (`spline_segment_init` / `spline_eval` / `spline_advance`) and take places in an **attack queue**
  (0x51F9F0, 0x14-byte entries; `attack_queue_push_front`, `attack_queue_role`): the front one lunges at a chosen
  player (`kind35_molb_lunge_start`, motion 0x132; `pick_target_player`). Shot: kill score 80, falls, its part model is
  swapped (`body_part_set_model`), then it fades and is removed. Floor shadows of all creatures: `shadow_update` probes
  the floor (`floor_probe`) and builds a projection matrix onto that plane (`shadow_projection`).
- **The frog Bouere (kind 69, creature type 7; 2026-10-14, arcade 0x88200-0x8A3F0, PC 0x4F6D34-0x4F8960):** the
  spawner runs a set-up chosen by the event record (in EVTALL.RBH the frog entries are `0x45, 0xFFFF0000 | set-up`):
  chapter 1 event 4 set-ups 0 (8 swimming frogs, scenery) and 2 (3 per player leap in, at most 3 in the air:
  `frog_count`), event 6 set-up 4 and event 9 set-up 3 (swimmers, the first three leap at a player and swim back);
  set-up 1 (a frog that sits, then hops) is in no event script. States (`kind69_states`): 1 hit, 2 death (80 points),
  3 / 4 hop toward the camera (3 with a floor probe, unreached in every recording; both versions read a stale stack
  word in its step 4), 5 the leap at a player (`player_hit`). Chapter 1 reaches events 6 / 9 only off the default
  route (branch table 0xAABE0: event 3 -> 4 / 9, event 5 -> 6 / 12). **PC differences:** sounds by PC number (0x83
  death, 0x57 landing, 0x5A pause, 0x1B splash); the leap-in arc takes 60 frames (arcade 29); hop targets in floats
  with the table square root (the arcade normalises in double precision); the leap's screen point x 10 x (1/496)
  (the arcade divides); stored matrix 1 copied onto the software matrix stack; the swimmers' draw-state calls are an
  empty routine (0x42CE70), nothing drawn with `pc_no_extra_model`; set-up 3's swimmers leave at x > -100 (arcade
  -125); set-ups 3 / 4 free a swimmer's body when it leaves the water's edge instead of after the leap back. The frog
  file has its own copies of the PC maths helpers (`pc_matrix_transform5`, `pc_vec_angles5`, `pc_fast_sqrt6`,
  `pc_matrix_translate5`, `pc_matrix_rotate_y5 / _b2`, plus `pc_matrix_rotate_x`).
- **The researchers of the scripted scenes (kind 64; 2026-10-15, arcade 0x6DAC0-0x70190, PC 0x4CCE44-0x4CEBC8;
  src/port/enemy_researchers.cpp):** nine set-ups by the record action (`kind64_setups`), which are also states of
  `kind64_states` (30 entries): 0 hanging (falls when struck, else climbs up when the enemies are gone), 1 / 6 "sno"
  (crouching, rescued with a gift run to the camera), 2 hanging by a ladder, 3 / 8 / 4 standing and crawling ones
  that get up at event flags 0x12 / 0x15, 5 the one who flees to `kind64_hold_up_point` and raises his hands
  (chapter 1 event 9; struck or shot sets `route_choice` 1), 7 in a cell (event flags 0x3F / 0x39 / 0x40). Event
  flags (`event_flags` + n) start and end each scene; the end states clear `camera_hold_hostage` and set scene_state
  1. Struck (state 29): by an enemy a hit effect at the body part point +0x1A8 and a scream (in chapter 1 events 6 /
  14 men fall with MO 0xE9), by a player `researcher_hit_by_player`; then back to the state saved in +0x32C. Each
  set-up counts `hostages_appeared`; a rescue counts `hostages_rescued` and `hostages_rescued_chapter` and gives
  `researcher_rescue_bonus`. Researcher records in EVTALL.RBH: chapter 1 events 5, 6, 7, 9, 11, 14, chapter 2 event 9
  (kind 65's scene zombies share events 6, 7, 9, 14). **PC differences:** sound numbers (0x30 fall, 0x79 thanks, 0x73
  / 0x70 / 0x7B cries); the set-ups' common start is a routine (`kind64_researcher_init`, inline in the arcade) and
  also makes creature type 49 a man; `researcher_reached_camera` is inline (a second turn by 0xC0, then the floor
  distance by the file's own copy `pc_floor_distance2` < 15); the file's copies `pc_vec_angles6`, `pc_fast_sqrt7`,
  `pc_matrix_transform6`; state 22 compares the camera frame's float bits as integers (45, 80); state 29's effect
  goes through stored matrix 1 on the software matrix stack. Nine of the PC pairs were wrong (four pointed into
  other files); the PC state table 0x5EEFDC gives them.
- **The bats (kinds 66-68; 2026-10-15, arcade 0x832C0-0x85430, PC 0x4E9688-0x4EB1C2; src/port/enemy_bats.cpp):**
  kind 66 picks a set-up (`bat_setups`) by the record action, kind 67 (the Hangedman's swarm) by +0x70, kind 68
  (chapter 2 event 2) always set-up 0 with the owner's record. Set-ups: 0 / 2 / 3 one flying bat (circle 7 x 6
  around its spawn point), 1 five bats (`bat_group5_points`), 4 a flatter circle (7 x 2), 5 the swarm of 20 (30 in
  event 15) in formation around the Hangedman (`bat_swarm_offsets`). States (`bat_states`): 1 hanging (shaking now
  and then), 2 take-off, 3 circling (sine / cosine of +0x44, wing frames 0x54-0x67, bobbing by +0x88), 4 the dive at
  a point 3 in front of the target player's view (through stored matrix 1), a hit (`player_hit`), then back to the
  circle or (actions 2-4) on past the player, 5 shot (80 points; a hanging bat wakes one or two of `hanging_bats`),
  6 falling dead, 9 / 10 the swarm's circle and dive (dive speeds from the Hangedman's table +0x35C, how many dive
  at once from `hangedman_bat_waves`). At most three bats dive at once (`bats_attacking`). **PC differences:** no
  bob while circling or diving; the direction's unit vector by the file's table square root and a reciprocal (the
  arcade uses coprocessor 0x0C); the dive point x 1 / 496 (the arcade divides); sine / cosine from `pc_trig_table`;
  the frame drawn through the software matrix stack with the file's helper copies, the model itself only with the
  option `pc_draw_bats`; sounds 0x8A / 0x88 (hit), 0x89 (death), 0x59 (the swarm's hit). The PC's
  `pc_matrix_position_draw` (0x43D870, the arcade's geo_matrix_position plus an extra part) had been paired with
  envmap_test_object.
- **Kind 32, the zombies with a kit (2026-10-15, arcade 0x7EEB0-0x80600 / 0x82F00 / 0x83270, PC 0x4E3988-0x4E5090;
  src/port/enemy_kit_zombie.cpp; the iron-ball kit 0x46F60-0x47AF0 and the held objects 0x805A0-0x82E80 not ported
  yet):** the spawn record (owner +0x35C) picks a kit (`kind32_kits`: start motion, start routine, after-hit
  routine): 2 the axe thrower, 3 the drum carrier, 4 the iron-ball swinger. Each per-frame routine is the object's
  +0 routine and ends in `kind32_hits` (part hits, hit reaction, death check, the event flag of
  `kind32_event_flags` at no hit points, shadow, draw, view position). The axe thrower enters by record +3 bits 1-3
  (walk in to a line, hide until combat, rise, or ready at once), throws by its current motion's row
  (`kind32_axe_throw_row`, `kind32_throw_rows`: the release frame for `kind32_axe_release`) while a hand still holds
  an axe (Cyril's bare-hand models 0x5DE / 0x5DD at nodes 5 / 8), then by record +0xC bits 6-8 becomes a standard
  enemy (`kind32_deactivate`: enemy_wait_camera_stop), stands, walks off along a spline, leaves, or jumps down. The
  drum carrier carries its held object (a separate task, `held_object_start`) until the front attack positions are
  free (`attack_front_busy`), throws it, and recovers from hits (its drum hit: `kind32_hit_react`). **PC
  differences:** the walk-in's start point and step through the software matrix stack (the identity
  `pc_identity_matrix` with its position, the file's copy of pc_matrix_rotate_y), the line test by the table square
  root; the view position through stored matrix 5; `attack_front_busy` inline; the walk-off's end test compares the
  float's bits as a signed integer; an unexpected motion in `kind32_axe_throw_row` only prints a debug message (the
  arcade hangs); the axe thrower's set-up stores 6 in `pc_wide_slot_kind`. Fourteen PC pairs were missing or wrong
  (e.g. carry_wait_start / carry_resume swapped; the PC has a second copy of carry_resume).
- **Scripted enemies' shared helpers (2026-10-16):** `enemy_scripted_hit` 0x535F0 runs in every scripted guard
  action: hit reaction (motion table 0xC14A0 by hit kind, voice), damage off +0x70, and at 0 HP a death motion
  chosen by the angle to the camera (0x233 front, 0x236 / 0x237 side / back); `enemy_scripted_to_ai` 0x54EB0
  hands the enemy back to `enemy_main`; `turn_toward_camera` 0x1CF90 (with 2+ players each enemy aims 3 units
  left or right), `angle_to_camera` 0x69A80, `move_along_heading` 0x69DD0. Clusters 0x53FD4 / 0x54168 were tails
  of two guard actions that Ghidra split off.
- **enemy_guard action codes** (record +0x22 -> object +0x64; routine = structural name `enemy_guard_caseN`, start
  motion with its developer name; "target" = the case takes a target object from 0x520830, e.g. a researcher):
  1 walk (motion from the record), 2 go to a target (649 z_walk_g; type 3: 559 z_dash_b), 3 axe zombie walk with a
  180-frame timer (336 onozonbia_2aruki), 4 stand (no motion change), 5 dash (484 z_a_dash2a), 6 chase a target (648
  z_walk_f), 7 chainsaw zombie roars at a target (552 z_chn_otake), 8 `ENGuardOpeningDummy` (518 z_a_walk6a), 9-11
  `ENGuardZombieRunAfterResearcher` (11: 644 z_walk_b), 12 bentry carrying (535 z_ben_motialu), 13 / 16 target +
  walk, 14 roar (584 z_han_otake), 15 holds a prop object it creates (597 z_kamae_a), 17 / 18 stance blended in
  (597), 19 `enemy_main` starting with a dash, 20 goes after Thomas Rogan (the object of creature type 2, BO_tom),
  anything else `enemy_main` with a walk. Read so far (hand names `enemy_actN_*`): 1 = **scenery** (plays its
  motion until the scene number 0x520704 changes, then disappears: not a fighting enemy), 2 = walk to the target,
  then run after it, 3 / 5 = start `enemy_main` (5 waits until the camera stops, 0x51F628 = 2), 4 = inert,
  6 / 7 / 14 / 17 / 18 = **scripted attacks on a researcher** (`walk_to` the victim, attack motion, then
  `ENGuardZombieBiteAgent`; the hit frame per attack motion from table 0xC14D0), 20 = follow Thomas Rogan until the
  scene moves on; 12 walks to a spot carrying something; 13, 15, 16 more researcher attacks (15 with a prop it
  holds). 8-11 keep the developers' names (`ENGuardOpeningDummy`, `ENGuardZombieRunAfterResearcher`).
- **Ordinary enemy (2026-10-13):** action 19 and every action code without a case start `enemy_main` (arcade 0x77410,
  PC 0x4D9A90), the per-frame routine of a plain zombie. Each frame: footsteps (`enemy_footsteps`), this frame's part
  hits (`enemy_part_hits` -> hit class 0x51FDB0 and damage 0x51FDB2), `enemy_hit_reaction`, `enemy_check_death`, the
  AI when the enemy is active (`enemy_ai`), shadow and draw (`object_draw`), motion frame +1.
  - Motion lists per creature type: table **0xC6B60**[type] -> [variant = +0x64] -> 8-byte entries {motion, 0}:
    entry 0 = stance (`MO_z_*_kamae`), 1 = walk, 2 = dash, 3 = step back (`MO_z_*_back`). Nine lists cover all plain
    types, e.g. BO_zom / BO_neozom: 497 kamae2a, 645 walk_c, 483 a_dash, 621 sam_back; the axe zombie (siriru variant
    1): 605 ono_kamae, 614 ono_walk, 483, 604 ono_back; chainsaw (samson / burner variant 1): 552, 542, 554, 550.
    The start motion is entry 1 (walk), or entry 2 for action 19.
  - Hit motions: table **0xC70C0**[type][variant][hit class % 9] (classes below 5 blend in, `set_motion_blend`).
    The hit class is the body region: e.g. BO_zom: 1 face 500 z_a_kao2hit, 2 chest 505 z_a_mune3hit, 3 right
    shoulder 504 z_a_katar4hit, 4 left shoulder 502 z_a_katal4hit, 5 belly 494 z_a_hala4hit, 6 right leg 474
    z_a_asiryolo, 7 left leg 472 z_a_asilyolo, 8 chest again.
  - Death motion table 0x762C0[type]: 1 = pick one of several at random (most zombies), else a fixed motion:
    560 z_dead_a (haris, gilmoa families), 561 z_dead_b (samson, bentry, burner families), 509 z_a_roll (ebita).
  - Death: hit points +0x70 minus the frame's damage; at 0 (or when the demo / a flag ends it) the shooter gets
    **80 points** (`add_score(player, 0x50)`), the death motion is 0x762C0[type] (or 0x27C, or random variants by the
    +0x6E flags), the routine becomes `Zombie_Dead` or `enemy_dying` (death motion, then `enemy_corpse`: blinks for
    40 frames, or `enemy_corpse_sink`).
  - `enemy_ai` (0x77B20) is a state machine in +0x5C, target distance +0x3A8: 1-2 approach until within 70 units, 3-4
    walk / dash until within 40, 6 ready within 25, 7 attack (calls `player_hit` on the attack frames), 8 recover
    (back to 6 when the target is 25+ away). Only a few enemies may attack one player at once: attack slots
    0x51F9F0 (`enemy_attack_slot_take` / `_free`). `enemy_ai_motion` picks the next motion from the lists above when
    the current one ends.
  - `set_motion_blend` (0x381D0, 198 callers): (object, motion, start frame, blend frames): the game **blends** from
    the current pose into the new motion; the viewer doesn't yet (formats.md §23).
- Behaviour routines named in the PC's debug strings: ENGuardZombieBiteAgent, ENGuardOpeningDummy,
  ENGuardZombieAttackAgent, ENGuardZombieRunAfterResearcher(2), ...AttackResearcher_St3_00, ZombieChainSawAttack,
  ZombieWalk2Tom, Tom_Adv_00_0 (Thomas Rogan's scripted moves), Zombie_Dead.
- **Held and thrown objects (arcade, 2026-10-14):** carriers (kind 32 kit 3, the drum carrier, and others) keep an
  index at object +0x32C into **kit records at 0xC9930** (0x58 bytes, `held_object_kits`): three sound cues,
  carry-walk / throw / hit-while-holding motions (MO_z_ben_motialu, mononage + release frame 46, monohit; the barrel
  uses talunage, frame 45), the held object's model list, throw speed (5.0, barrel 2.0) and 8 routines. Records: 0 Cyril's
  axe (0x5BB `siriru_ono`), 1-4 the drum (0x3B7-0x3BA `kkkdolam*`, 4 pieces down to 1, so shooting it takes pieces
  off), 5 the barrel (0x68A `taru_yoko_01a`, damaged 0x6D0). Routines: `held_object_carried` (drawn and shootable in
  the hand), `held_kit_drop1` / `drop2` (the carrier lets go), `held_object_release` + `held_object_flight` (the
  thrown object: shootable, sound on impact), `held_object_draw`. **+0x32C is set from the spawn data (2026-10-16):** `kind32_act3_drum` copies byte 2 of the carrier's script record (object +0x368) into +0x32C of the carrier and of the held object it creates, so each ENTB record chooses its kit (1-4 drum states, 5 barrel); `kind32_axe_release` sets 0 (the axe); `held_kit_drop1` / `drop2` and 0x82F90 / 0x82FE0 pass it on to the objects they create. (+0x32C is a general work slot: the Hangedman, the Magician, the Murrer and the props use it for other things.)
  **Cyril's axe throw (2026-10-15, cluster 0x7F5A0):** `kind32_axe_throw_row` picks a row of `axe_throw_rows` (0xC9B40,
  6 x 0x18 bytes) from the current throw motion (MO_z_a_onomage / _l, MO_z_ono_rnage(b) / lnage(b)): hand flags,
  release frame (30-72), spin direction; `kind32_axe_throw_frame` calls `kind32_axe_release` (creates the flying axe)
  at that frame. At the motion's end Cyril returns to his stance if a hand shows the bare-hand model (`siriru_ter` /
  `siriru_tel`), otherwise the spawn record decides: deactivate, stance, walk off along a path, or walk.
- **Props, shared helpers (kind 128 `prop_object`, 2026-10-15):** `prop_shootable` (depth list), `prop_draw`,
  `prop_bullet_impact` (impact mark `chaku00a`, or `chakuhi00a` on metal / stone sub-types), `prop_hit_drop` (a loose
  part falls, e.g. from the pot `nabe01a`, sub-type 64), `prop_break_pieces` (fragments with random velocities: chair
  pieces `isu_hahen*`, `chiza_dam*`, ...). Sub-type 68 (`prop_type068`, model `powa00a`) wobbles when shot and bursts
  after 226 frames. Ghidra starts that routine 0x1C bytes late (real entry 0x3E630): other table slots may have the
  same problem (check unnamed functions that begin right after a `ret`).
  **Landing (2026-10-15):** a thrown object that reaches the floor tumbles: `tumble_corners` transforms its 8 box
  corners through the coprocessor, `tumble_update` tests them against `ground_height`, `tumble_start` picks an axis
  (+-x / y / z) and 3 bounces, `tumble_bounce` lifts it above the floor and rotates it, halving the speed each bounce.
  Used by `held_object_flight` and object kind 38. Cluster 0x81B20 (`thrown_tumble_old*`) is an unreferenced older
  copy of the flight (fly, land, rest, then lies still).
- **Part model swaps (2026-10-14):** `body_init` sets object +0x11C = `body_part_draw_hook` (0x9CE60), called per part
  while a body is drawn; its switch fragment `body_part_swap_models` (0x9CFD4) draws, by creature type, motion and frame,
  another model for a part: hand poses (0x71B, 0x36B, 0x2B3, 0x631), a hand holding a magazine (0x716, human types 2 /
  13-15, motions 0xC2-0xC3), Sophie's other heads (0x60F, 0x61F). The viewer's "self-drawn parts" (fix 1) are these.
- **The Magician's veins are model cycles, not texture writes (2026-10-16).** In `body_part_draw_hook`, creature type
  76 (case 0x4A): parts 3, 4, 11 and 12 draw `base + (0x51F420 / 2) % 25` (0x51F420 counts vblanks, so a new model
  every 2 frames, a 25-frame loop of 50 frames): bases 0x1C2A `boss4_kls01`, 0x1C75 `boss4_urs01`, 0x1C47
  `boss4_mls01`, 0x1C07 `boss4_hrs01`; the 25 members of each family (01, 03 ... 49) are the frames. The skeleton
  names member 17 (0x1C32, 0x1C7D, 0x1C4F, 0x1C0F: the "four arcade-only vein parts"), which is why the viewer shows
  them frozen. While the Magician is hit (+0x58 bit 8) and a blink timer (+0x2F4) is odd, the same models are drawn
  with the header variant 0x802400 (hit flash; part 8 sets +0x3C4 bit 0 for the other parts). Same mechanism: type 78
  (Dr. Curien's second model, case 0x4C) head `kyubatama_CT00a` + (motion frame / 3) % 15, type 84 (the Burner,
  case 0x52) flame `burn_kaen000a` + frame % 15. **Viewer (2026-10-17):** the Magician's four vein parts now cycle
  (`kModelCycles` in src/viewer/main.cpp; J off shows the recorded member 17); the face veins visibly change. Still
  different from MAME: the large dark-red shapes on the Magician's legs at t13540 (another part or the hit flash?).
  Curien's head and the Burner's flame: not done.
  The PC has no meshes for these families (only the four frozen parts were missing), so it cannot animate them.
- Official names of the creature types and kinds: [names.md](names.md).

### Player, light gun and shooting: deep pass (2026-10-21)

Types: `Player` (blocks `players[2]`, arcade 0x51F284 / PC 0xD9F05C, 0xCC each, same layout), `PlayerTask` (the
0x24-byte task each player runs; +0x20 = player number), `GunInput` (arcade `gun_inputs[2]` 0x51F574, 0x1C each),
all in tools/ghidra/types.tsv with every field used.

- **Input sources** (`input_source`): 0 live (buttons word `buttons_live` 0x51F514; the gun via gun_read /
  gun_to_screen), 1 recorded (attract demo stream: per frame 16 bytes, per player trigger / on-screen / shot / gun
  phase / aim x / aim y into Player +0x56..+0x5C; mode 8, the how-to-play demo: its own stream into +0x38 / +0x44),
  2 none. `player_input_update` keeps buttons / previous / pressed / released (+0x44..+0x4A).
- **Arcade gun cycle** (`gun_flash` 0x46D50, `gun_input_update` 0x46A90): a trigger pull (4-frame lockout per gun)
  starts `gun_flash_phase` 0x51F5EC = 4: the screen flashes white and the gun position is read into Player
  +0x34 / +0x36 (1000 = off screen) and +0x3C (on screen); the phase counts down one per frame; **player_shoot fires
  at phase 2**; pulls during phases 1-2 are buffered (`gun_shot_buffered`). Outside play the flash only runs with
  `gun_test_mode` 0x51E8D8 set.
- **A shot** (`player_shoot` 0x5A80): needs phase 2, a trigger edge, the gun on screen and rounds left (+0x3D);
  `ammo_enabled` (always 1) counts the round down, the last one sets +0x3E (empty). With `shot_enable` 0x51F5F0 the
  shot sets Player +4 bit 1, builds the ray (`shot_ray_setup`, focal 496.0), stores the shot position (+0x30), plays
  the flash effect and gunshot sound 0xA01208 (player 1) / 0xA01207 (player 2) unless `gun_test_mode`; without
  `shot_enable` a pull only sets `skip_request` 0x51F5F4 (skips the chapter card and skippable scenes).
  **Reload:** trigger with the gun off screen = 6 rounds, +0x3E cleared, sound 0xA01200 (not in the attract demo).
- **PC differences** (`player_shoot` 0x4365C4): no flash cycle (the mouse / gun position is read directly by
  0x4358CC and the shot fires the same frame: the arcade fires 2 frames after the pull); reload is its own input
  (GunInput +0x16 on the PC; the arcade uses that byte as the trigger lockout); in PC Mode (front-end state 5) the
  reload refills the character's rounds (table 0x5CE348 + 0x24) and starts its reload timer (timeGetTime); a PC-only
  flag 0xDA09E8 stops the round count (infinite ammo); the PC's own sound cues (0x1E / 0x1D gunshot, 0x1C reload: so
  the "same cue numbers" note in the table holds for the game sounds, not for these); the shot position comes from a
  float helper the matcher mis-named `kind32_act2_stance_sub1_n` on the PC (stale name, to fix).
  Best of both: the PC path for mouse / PC guns (no flash needed), the arcade cycle only for an authentic light-gun
  mode.
- **Hits on bodies** (types `BodyPart` 0xD8 per node, `PartRole` 8 bytes): the shot test sets BodyPart +0x3C bits 1 / 2;
  `part_hit` (0x68010) per hit part: armour counts down (+0x79, from the role's armour); at 0 the part's damage step
  advances: replacement model `part_replacement` [type][class][step] (0 = destroyed, 0x7B0 = keep the model), new hit
  points `part_hit_points`, `hit_class` = `part_class_hit`[class] (the enemy's hit reaction), `parts_lost` (obj
  +0x2E4) bit `part_class_lost_bit`[class]; parts that may be severed get +0x3C bit 31; damage collects in
  `hit_damage` 0x51FDB2 for the creature's own routine. Unverified on screen: which classes are which body regions.
- **Shot test** (types `DepthEntry`, `ShotCandidate`, `ShotResult`): every shootable object adds itself each frame
  with `depth_list_add` (only if flags bit 0 and its camera-space z, obj +0x38.., is >= 0; depth = z x 10; up to 256).
  `shot_resolve` (0x454E0), per player whose Player +4 bit 1 is set: tests every depth-list object
  (`shot_test_object`), the scene pairs and the two scene-item lists, sorts the candidates (`shot_targets_sort`) and
  `shot_hit_nearest` gives the hit to the nearest one only: object +0x58 |= bit (player + 1) | 0x100, or for a body
  part part +0x3C the same and its owner's +0x58 too; a scene item gets no bit but its code in `shot_results`[p] +0x24
  (and a flash unless the code is 4). Player 1 is resolved before player 2 in the same frame.
  `shot_test_object` (0x45620): the player's ray against the object's sphere (`ray_sign_test`: view_pos, hit_radius);
  inside it, an object with a body and **flags2 (+0x6C) bit 0** has each body part tested (`shot_test_body`), else the
  object itself becomes a candidate (hit point = view_pos, sort depth z x 10 into `shot_sort`). `shot_test_pairs`
  is an empty loop in this build (its body was removed: nothing is tested there).
- **Exact level (2026-10-13), shot ray and sphere test** (types `ShotRay` 0x53E2B0 / PC 0xC723E0, `BodyPart` now with
  its PC layout = the arcade's): `shot_ray_setup(player, aim x, aim y, 496.0)`: cam_dir = 0x0C normalize(x, y, 496),
  cam_origin = 0; (a0, a1) = 0x0F(cam_dir), and when a1 & 0xFFFF leaves -0x4000..0x3FFF (bits 15 and 14 differ):
  a1 = 0x8000 - a1, a0 += 0x8000; matrix 1 (0x74) -> origin = 0x1A(cam_origin), translation 0 (0x88) -> dir =
  0x1A(cam_dir), pop (0x06); (b0, b1) = 0x0F(dir) with the same fix; cam_ang_neg = (-a1, -a0), cam_trig = sin(-a1),
  cos(-a1), sin(-a0), cos(-a0); world_ang_neg / world_trig the same from (b0, b1); aim stored at +0x60.
  `ray_sign_test(player, x, y, z, radius)` (camera-space point): c0, c1, s0, s1 = cam_trig[0..3]; u = x*s1 - z*s0;
  v = ((x*c0)*s0 + y*c1) + (z*c0)*s1 (float32, this order); hit when 0x1F distance(u, 0, v) <= radius.
  `shot_test_object(entry, player)`: ray_sign_test(view_pos, hit_radius); hit and the object has a part tree with
  children and flags2 bit 0 -> `shot_test_body` (every child of the root through `shot_test_part`); else a candidate
  {view_pos, 0, 0, 0, obj, obj flags, 0} with sort key (int)(view_pos z * 10.0) rounded toward zero.
  `shot_test_part(part, player)`: tested when part_id != 0 and hit_radius != 0.0: ray_sign_test(part view_pos,
  hit_radius) -> candidate {view_pos, 0, 0, 0, part, part flags, 0}, key (int)(z * 10.0); then all children,
  whether hit or not (recursion). No random numbers anywhere in the shot path.
  **PA / PM differences:** the PC builds the same record with x87 maths: length by its table square root
  `pc_fast_sqrt2` (0x41347C), dir = v * (1 / length) in 80-bit then rounded to float, angles by its own routine
  (0x412D28; its pc_atan2 0x568F10 reads the 1025-int arctangent table 0xC653B8, initialised data in the EXE =
  round(atan(i / 1024) x 2048 / pi)), sin / cos from the 4096-entry table 0x5697B0 at index (angle >> 4) & 0xFFF (no
  interpolation; round(x 65536) / 65536 except +-43562 -> +-43563 in 8 entries: port/pm_maths.h); its
  `ray_sign_test` (0x414AAC) measures the distance with the same table square root and, **in PC Mode only, multiplies
  the radius by the character's hit size** (0x5CE378 + 0x34 x character); shot_test_body is inlined; the sort key is
  truncated too (`pc_float_chop`, Watcom __CHP). So A, PA and PM agree on the logic but not on the last bits of the
  distances: a hit on the very edge of a sphere can differ between A and PA (to measure with the checker).
  `shot_targets_sort` (arcade 0x466D0, PC 0x48A44C, same algorithm): stable LSD radix sort of `shot_sort` {key,
  index} in two 8-bit passes (bits 0-7, then 8-15; scratch 0x542090 / PC 0xDA15F0): only the key's low 16 bits
  count, so a candidate farther than z = 6553.6 sorts by key mod 65536 (a quirk of both); equal keys keep the
  candidate order (depth-list order, parts in tree order).
- **`part_hit` exact level (2026-10-13; arcade 0x68010, PC 0x4C3D58):** per part with part_id != 0 and flags bits
  1 / 2, for player 0 then 1 (bit player + 1): class = part_roles[type][role].part_class; class 1 (eyes) adds
  player + 1 to the counter 0x543524; armour - 1 (if > 0); at 0: next model = part_replacement[type][class][step]
  (step = +0x78); model 0 -> parts with sever mode 1 / 2 mark the creature (w308 = one-hit kill), get flags bit 31
  unless motion flag 1 is set, hit_class and parts_lost bit; armour = -1; else (**arcade only in game mode 4**):
  hit_class, parts_lost bit, the new model unless 0x7B0 (keep) or (sever mode 1 / 2 with motion flag 1), step + 1,
  hit radius = creature scale x part radius table 0x520B1C, armour = part_hit_points[type][class][step], flags
  bit 31 when the next step destroys a severable part (radius 0 for model 1), the per-type damage callback (table
  0xC66A0) unless motion flag 1; class 1 at step 2 spawns the popping eyeball; class 1 inside the type's blow-off
  range (table 0x135940) -> part_blow_off. Damage: class 0 = role byte 1, class 0x10 = 0 (sparks), class 0x11 = 0
  (class-17 effect), else table 0x12F9F0[type][class][step]; plus the difficulty bonus 0x14BC00[type][role x 16 +
  difficulty_get()] when the type has that table; a positive total goes to hit_damage and the per-player tally
  0x543520[p]. Then (arcade: game mode 4 only) sparks + sound 0xA0122A / class-17 effect + 0xA0120A / blood burst;
  when the creature's life minus hit_damage < 1: bleeding (part_bleed_start when severed / one-hit, else
  part_bleed_spawn). Then the children, recursively. Random numbers: none in part_hit itself (part_blow_off and the
  effects call rand).
  `enemy_part_hits` (arcade 0x684B0, PC 0x4C4168; only while the camera is at a stop and flags bit 0, or in chapter
  index 5): resets the per-player tallies (0x543520 / 0x543522) and the eye counter 0x543524 = -1, part_hit on every
  child of the root; when life - hit_damage < 1 the killer is the player whose own tally kills while the other's
  does not; **otherwise A draws it: rand() & 1; PA / PM take player 1 if only player 1 is playing, player 2 if only
  player 2, else the previous killer (`pc_last_killer` 0xDA0AE8, PC only) - no random number** (so in a 2-player
  game with a shared kill the arcade's random sequence moves on and the PC's does not). Not dead: killer = -1. Eye
  hits (counter set by part_hit): 120 points to that player, or with both (2) to the killer, or with no killer to a
  random player (rand() & 1, in both versions). Then body_free_cut_parts.
  **A / PA / PM:** the PC has **no game-mode-4 test**: in the arcade, outside play (the attract demo, mode 3) hit
  parts never change model, step, armour refill or blow off and no hit effect plays; the PC does all of it (to check
  on screen: shoot-outs in the arcade's attract demo show no lost limbs). **Checked on screen (attdoor, 2026-10-31):**
  MAME Rev A's attract, 345 s (three demos: Chapter 1 event 5, event 15, Chapter 2 event 9), a picture every 0.25 s:
  no wound models, no blow-offs, no lost limbs in any demo; ours over the same two Chapter 1 demos: 25 part hits, all
  at step 0 with a replacement model (none severed), the same pictures. (Only a part whose step-0 replacement is 0 with
  sever mode 1 / 2 would still come off outside mode 4; none is hit in these demos.) Gore in an attract video is
  therefore another version: the PC release (no game-mode test, above) most likely. PM: the damage is multiplied by the
  character's damage factor and truncated (`pc_float_chop`) before it is added. PC sound cues 0x37 (sparks) / 0x20
  (class 17) for the arcade's 0xA0122A / 0xA0120A.
- **The prologue's sewer corpses (Revision A data edit; demo, 2026-11-03).** The "1998.12.18" prologue (mode 9,
  chapter slot 5, event 0; PC EVTALL.RBH list 5 event 0) scene 6 is the sewer: the PC's script there is
  `BGTB 16, GGCOL 6 110, GGCOL 8 94, GGCOL 9 106, ENTB 0xFE310 0xFE340, GGSET 24, GGSET 57, ENTB3 ...` (PC offset 668);
  Revision A's ROM script (0xFDC3C..) has the same instructions **without the ENTB** (GGCOL 9 at 0xFDC54 is followed
  by GGSET 24 at 0xFDC64). The two records are still in the ROM (0xFE310: kind 118, type 17, action 1, at
  (-72, -75.5, 219), heading 0x6800; 0xFE340: action 0, (-63, -75.5, 140), 0x8800), referenced by nothing, as are two
  more kind-118 records 0xF0624 / 0xF0654 (not in the PC data at all). Kind 118 (`kind118_corpse` 0x5C0A0 /
  PC 0x4AD744, frame 0x5BF80 / 0x4AD678): kind 101's lying researcher (the `sitai_ken` models 0x5E8 / 0x5E6, with the
  blood pool; 0x5E9 / 0x5E7 when blood_color != 0), kept until scene_number changes; in Revision A nothing spawns it.
  The scene's camera (path 110) still ends on the empty floor where the first lies. No code test is involved (the PC
  routines match the arcade's, plus the PC's draw-skip flag 0x5CD35C). ReWrithe: under **Attract Gore** (on) the
  event interpreter runs that ENTB when the script reaches 0xFDC64 in mode 9 (`evt_attract_restore`, event.cpp); off
  = Revision A (no spawn; frames identical to before).
- **The attract, Revision A against the PC, everything compared (2026-11-04).** Every event of all 7 lists (ROM
  0xE0000 against EVTALL.RBH.orig), instruction by instruction, with the records behind ENTB / ENTB2 / ENTB3 / ENTBE /
  ET* / WLTB pointers compared by content (the PC packs its records tighter; only the packing differs), and all 142
  camera paths (copro data against CAMALL.RBH, keys within rounding). Differences: (1) the prologue's sewer ENTB
  (above); (2) **camera path 107** (the prologue's scene 7, the hostage held by two zombies, frames 0-94): the PC's
  last key is higher (frame 100: eye y 13.79 / look-at y 18; ROM 10.79 / 16, copro words 0xC803 / 0xC7CC / 0xC7D0),
  so its close-up ends with the raised hand in the picture; (3) the gore's game-mode-4 test (bullet damage row).
  Outside the attract, not censorship: Chapter 2 event 10 SKYTB 1 (PC 0), Chapter 4 event 4 one more SNDON in the
  PC, the ending list 6 (event 3 ENTB2 kind 0x6F, PC 0x6B; events 4-6 the PC waits 240 / 120 frames where the ROM
  repeats a camera hold). The recorded demos (Chapter 1 events 5 / 15, Chapter 2 event 9) run the same scripts and
  records in both. Two more kind-118 records in Chapter 2 event 9's data (0xF0624 / 0xF0654) are in neither script
  (and not in the PC data): left out. ReWrithe, Attract Gore on: (1), (2) (`camera_path_eval` reads the PC's keys,
  `Tgp::set_patch`), (3); off = Revision A.
- **Player hits** (`player_hit` 0x6410 (player, shake, side), `player_hit_lose_life` 0x6520 (player, shake, side,
  ignore invulnerability, invulnerable frames)): a hit counts only when the player is in play (in game mode 4 the
  player state must be 1, 4 or 5; unverified which states those are on screen), not invulnerable
  (`player_invulnerable`[p], set to **90 frames** after each hit) and not in the attract demo: lives - 1, **score -
  100** (add_score floors at 0), `hits_counter` - 1, and with `shake` the hit side and the **48-frame screen shake**
  (`hit_screen_shake`, roll = timer x 0x1800). **A player cannot lose the last life while the camera is moving**: if
  the camera is not at a stop (`camera_stopped` != 2) and the camera kind is not 2, lives stay at least 1.
  The PC's player_hit (0x436FE4) takes off 100 points directly and has its own invulnerability variant (0x4370D4).

### Enemy spawn records and the standard enemy (deep pass, 2026-10-21)

- `evt_ENTB` (0x315C0) walks a -1-terminated list of 40-byte `SpawnRecord`s (types.tsv): per record a task of the
  record's kind (kind table 0xAFDE0), flags = record +4 | 1, type = byte +0x24, action = byte +0x25, **+0x70 / +0x72 =
  short +0x22**, position +8 (placement mode +0x20 & 3: 0 as given, 1 / 2 relative to the camera through the
  coprocessor), angles +0x14 / +0x18 / +0x1C. For the standard enemies +0x70 is the life: `enemy_check_death`
  subtracts `hit_damage` and kills at <= 0 (80 points to `killer`), so **record +0x22 is the hit points**
  (**verified** with the replay dumps private\mame_dumps\ch1_run1: +0x72 keeps 100 / 120 / 160 by type while +0x70
  drops 120 -> 85 -> 20 with the hits and goes below 0 at death); the 2026-10-10 note (formats.md §29) read it as an
  appear frame: corrected.
- `Enemy` struct (types.tsv, extends Obj): death style +0x6E (& 3: fixed motion 0x27C + `Zombie_Dead`, one of three, or
  the type's: table 0x762C0, 1 = one of five), one-hit kill +0x308 (any hit kills), AI start flags +0x304 / +0x330,
  players counted at targeting +0x334, attack position wanted +0x338, walk speed +0x348. Group roars: types 30 / 84
  with flags bit 19 count in 0x51FE34; the last one dying plays 0xA01356 / 0xA01466. `return_false` (called in the
  death test) always returns 0 (a hook left in by the developers).
- **`enemy_main`** (0x77410) per frame: (flags bit 21 and `route_choice` == 1: the enemy is removed at once, see
  below); footsteps; action 1 with `parts_lost` bit 1 ends the action (types 30 / 84 also leave their roar group);
  part hits -> `enemy_hit_reaction` -> `enemy_check_death`; while alive `enemy_ai`; shadow (flags bit 23: floor 15
  under the camera eye); draw; motion time + 1; type 3 in motion 499 draws model 0x200 + time for frames 10-99;
  attack positions >= 2 join the attacker list; motion step (`enemy_ai_motion`) and the depth list.
- **`enemy_ai`** (0x77B20; its switch runs on state - 1), states (+0x5C) with the target's z (`target_pos`,
  refreshed from the target player's body: the solo player, or player `slot_wanted` with 2 players):
  1 approach: after `delay` frames, at z <= 70 -> 3 (attack position < 4) or 2 (with the walk motion of the type /
  action list `enemy_motion_lists`, or the random walk set when `walk_variant` >= 2);
  2 walk on: once its attack position is < 4 -> 3 (walk sub-state 1 or 2, random start frame);
  3 near: at z <= 40 take an attack position -> 6, else 4 (stance);
  4 wait for a position (enemy_target_distance) -> 6;
  5 close in: at z <= 25, if its player is not already targeted (`player_targeted`[p]), no screen shake and someone
  is playing: pick the attack (`attack` = `enemy_attack_choice` by type / action / lost parts / rand; `ai_at_once`
  forces 7) from `enemy_attacks`[type][action] (EnemyAttack: attack motion, lunge motion, lunge speed, reach, hit
  frame, hit side, parts it needs): beyond the reach -> 7 (lunge), else -> 8 (attack, voice 3); not possible -> 6;
  6 at an attack position: moves to the wanted position when it frees up; each frame a 1 in 32 chance to attack (as in
  5; with one player not from position 1 while `attack_side` is 0 and more than one enemy is alive);
  7 lunge until within reach -> 8;
  8 attack motion: at the hit frame, unless one of the needed parts is lost (`parts_lost` & needs_parts),
  `player_hit` (target, shake, side). **Enemies with flags bit 21 hit once and vanish, and their hit sets
  `route_choice` = 1** (then every other bit-21 enemy is removed by enemy_main): a route-deciding attacker (which
  ones, unverified); when the attack motion ends (`enemy_ai_motion`) -> 9: frees the target, steps back to its attack
  position (motion list entry 3, walking backwards);
  9 back off: once the target is 25 or more away again -> 6.
  **Exact (2026-10-13):** the thresholds 70 / 40 / 25 are compared with the coprocessor's floor-plane distance
  (function 0x1F of (x, 0, z) of the enemy and of target_pos, bit-exact model in tools/tgp_model.py), not with z.
  Random numbers, in this order: 2 -> 3 and 4 -> 6: one call each, the walk motion's start frame = rand % 10;
  5: one call when it may attack: choice = enemy_attack_choice[type][action][(rand >> 4) % 10 + (parts_lost & 7) x
  10] (none with ai_at_once); **6: one call every frame** ((rand >> 4) % 32 == 1 = the attack chance; it is drawn
  before the other conditions are tested), and a second call for the choice when it attacks. So each enemy waiting
  at an attack position consumes one random number per frame, in task order.
  **PA / PM:** the PC enemy_ai (0x4DA030) makes the same random-number calls in the same places, but measures the
  distance with its table square root (`pc_fast_sqrt3` 0x4D7934 = the same code as 0x4FC76C / 0x41347C: 256-entry
  table 0x5CCEFC, **up to 0.39 % error**, e.g. sqrt(4900) = 69.9714): an enemy passes the 70 / 25 tests up to about
  0.27 units (a frame or so) earlier on the PC than in the arcade (thresholds 70.0 / 40.0 / 25.0 at 0x5A41BD.. are
  the same). Model: `tools/pc_model.py`.
  **`kind000_enemy_spawn`** (arcade 0x77110 = PC 0x4D974C, the same in both incl. the random numbers): the walk-kind
  word (+0x324, PC +0x32C) 6 -> one rand: walk set 0x757C0[(rand >> 4) % 6], walk_variant 2-7, word = 1; 7 -> one
  rand: 0x757D0[(rand >> 4) % 2], walk_variant 6 / 7, word = 1; 8 -> MO 0x287 (MO_z_walk_e), walk_variant 8; else the
  type / action motion list entry; then always one more rand: anim_time = rand % 10 (the start frame).
  **`enemy_check_death`** (0x778C0 = PC 0x4D9DFC, same): life -= hit_damage (16-bit); dead when life <= 0, or hit
  (flags bit 8) with one_hit_kill; 80 points to the killer (if any), the targeted flag freed, second motion off;
  death motion: death_style & 3 = 1 -> 0x27C and Zombie_Dead; 2 -> table 0x76460[rand % 3]; else the type's entry of
  0x762C0, where 1 means one of five (0x76440[rand % 5]): at most one random number per death; types 30 / 84 in a
  roaring group (flags bit 19) count 0x51FE34 down (sound 0xA01356 / 0xA01466 at 0); with more than one enemy alive
  flags bit 20.
  **`enemy_ai_motion`** (0x78B70 = PC 0x4DAE90, same random use): one rand only when a motion has just ended (motion
  flag MotFinish) while the enemy is in a hit reaction (flags bit 28): walk on when rand % 100 < table
  0x765F0[type x 4 + chapter].
  Turning toward the target each frame (rate 65536 / 61636.88 of the difference; sub-state 3 = backwards, slower)
  once `delay` has run out; movement = the current motion's speed (MotionStep.speed, or the lunge speed in state 7)
  along the heading.
- **`enemy_ai_motion`** (0x78B70): when a motion ends, replays the state's motion (the list entry of the sub-state);
  **a hit can make it walk on**: while hit (flags bit 28) at a motion's end, with a chance per type and chapter
  (table 0x765F0[type x 4 + chapter], percent) states 1 / 3 / 5 switch to the walk (sub-state 2).
  `enemy_hit_reaction` (0x777F0): a hit (hit_class != 0) outside the attack states 7 / 8 plays the hit motion
  0xC70C0[type][action][class % 9] (blended and held for classes 0-4, else at once), sets flags bit 28, voice 0.
  `enemy_target_distance`: 1 player -> the solo player, slot_wanted 0 / 1 by `attack_side`; nobody playing -> the
  camera position; with 2 players the decompiled C always shows player 2 (a coprocessor result is lost in Ghidra's
  output: unverified). `enemy_dying`: plays the death motion (motion 0x1FD at frame 53: in water (floor_probe kind 5)
  a splash (kind160_splash) and the corpse sinks, else a floor splat), then `enemy_corpse` at the last frame.
  `enemy_footsteps`: sound 0xA01225 at frames 10 / 40 of dash 0x1E3 and 20 / 40 of 0x20C.

### Props (kind 128 `prop_object`, arcade, 2026-10-21)

- **Sub-type record** 0xAC2C0 + 0x4C x sub-type (124 sub-types; PC 0x5D46AC.., same layout): +0x00..+0x14 floats
  (+0x04 + 4 x step = height of the bullet-impact mark per damage step), +0x18 sound cue when shot, +0x1C
  flag, **+0x20.. model per damage step** (0 after the last step = the prop is destroyed, -1 = no further damage),
  +0x34.. hit radius per step, +0x48 scale. Per-frame routine per sub-type: table 0xAF960 (most use `prop_default`).
  Model names give the sub-types: 3 chair isu, 5 the grandfather clock tokei, 8 / 43 bookshelves, 9 chest tantansu,
  10 deer head, 13 aquarium suiso, 14 the human-faced fish, 17 / 22 round tables, 23 falling leaves, 27-29 corpses (the
  blood version when blood_color != 0), 30 / 31 glass cases, 36 lamp, 39 / 40 sofas, 46-50 barrel pieces, 52 / 59 stone
  statues, 56 / 57 red balls, 62 crates (stacked), 63 frog, 65 / 66 / 72 pots nabe, 67 floor lid (route 1), 69 bubbles,
  77 Chapter 2 wall, 78 / 79 Chapter 2 doors, 83 giant insect, 84 meat, 85 fruit, 87-95 Chapter 3 machines, 97-101
  Chapter 4 mechanism, 109-117 containers / machines, 118 barrel, **122 life item, 123 gold coin, 124 gold frog**.
- **Common fields:** `prop_init` (0x42740) sets the step-0 model / radius / scale; `prop_schedule_update` (0x3FCB0)
  ends the prop after +0x70 scene changes and toggles visibility (+0x2F0) at camera frames from schedule +0x64
  (table 0xAE840, 8 shorts per schedule, -1 ends). `prop_default`: when shot (flags bits 1 / 2) plays the hit sound,
  places the impact mark, runs the break effect of its sub-type (switch on sub-type - 3: pieces, skull burst, plate
  shards, bookshelf collapse, statue break, tumble copy, lid fly, spurt ...), steps the damage model; after the last
  step drops its item (flags bit 5 life item, bit 6 coin, bit 7 gold frog) and ends. Bit 31 = one of a **lottery
  group**: sub-type 107 (no model) draws a random member index (0x53E3B1 = rand % +0x70) and only that member drops
  the item (0x53E3B0 counts the destroyed members).
- **Items:** life item (`prop_drop_life_item`, model item_life); gold coin (`prop_coin_run`: spins, shot = sparkle +
  add_score to the shooter); **gold frog** (creature type 91 BO_gfrog, `prop_gold_frog_run`: shot = **1000 points**,
  leaps away). In Chapter 4 none of the three appears (chapter 3 ends them). Unverified on screen.
- The item sparkle (`item_sparkle` 0x42C20) pulses **palette entry 985** (0x5206AC) with a 32-frame triangle wave:
  the open "entry 985" of the palette notes (written by 0x42C78).
- Arcade vs PC: the PC pairs of `prop_object`, `prop_init`, `prop_default` and the shared helpers use the same record
  table and the same steps (checked: prop_init 0x483AD0, the PC writes the flags as a 16-bit word); the sub-type
  routines not yet compared one by one.

### UI inventory (for the switchable UI)

Every 2D UI element goes through one sprite routine pair (arcade 0x17E40 / 0x180B0 = PC 0x4434B0 / 0x443810: draw
sprite N at a text-cell position, optionally animated). Callers: **71 arcade, 61 PC functions; 33 paired** (25 of
those still draw on the PC). Both sets are kept (ROADMAP: switchable UI).
- Arcade-only UI functions: 0x5784, 0xB050, 0xB5F0, 0xBFE0, 0xD550 (operator menu / version screens), 0x16F20 (boot),
  0x1E480, 0x1E740, 0x20B40, 0x20B70, 0x224A0 (attract / ranking), 0x26260-0x27300 (mode 8), 0x351D4, 0x35FD0,
  0x44540, 0x44680, 0x503E0, 0x5D074, 0x5D7D0, 0x5E4F0, 0x6BAF0-0x6BDB0, 0x94430, 0x949F0, 0x94FD0-0x95110 (HUD
  parts), 0x9B0E0-0x9B4C0.
- PC-only UI functions: 0x43956C, 0x43B5BC, 0x442EA8, 0x44861C-0x4487C0, 0x44D8C8 (ranking rewrite), 0x458B40,
  0x45AD90, 0x45B510-0x45B748, 0x473E90, 0x4B4B80, 0x4C5FAC-0x4C61D4, 0x50B358, 0x50BBF0-0x50BD3C (HUD parts),
  0x51A03C-0x51AD20 (sprite / text helpers).
- The PC's front-end menus (main menu, options, boss mode, PC Mode character stats, high scores) are Windows-era code
  outside this sprite path (main loop 0x419168, menu 0x41DD3C, 0x420570 score strings).
- **Developers' text-window library (arcade 0x281A0-0x2AB50, 2026-10-15):** text windows with a border, a highlight
  cursor over a grid of cells (`window_grid_cursor`, written straight into the text tilemap 0x1000000) and a debug-pad
  reader with auto-repeat (`debug_pad_repeat`, 0x51F514 / 0x51F518). Nothing outside the library calls it except
  `window_printf` (from the "CC%.3d" debug display 0x4ADDC): a debug-menu toolkit left in the ROM, not the operator
  menu. `camera_matrices_update` (0x2ABD0) right after it is ordinary engine code (camera matrices).
  2026-10-16: the remaining ~30 library functions named (`window_*`, `text_tile_put_*`, `text_font_upload`): two
  tilemap layers (A 0x1000000, B 0x1004000), a window record (+0x64 x, +0x68 y, +0x6C w, +0x70 h, cursor +0x74 /
  +0x78, item count +0x80, columns +0x88, scroll / wrap flags +0x98) with save / restore of the cells underneath.
- **Operator test menu (arcade, 2026-10-16):** `test_menu_mode` 0x84C0 runs the routine pointer 0x512100;
  `test_menu_open` / `test_menu_select` show the 12 items of 0xA8800 with routines 0xA8C50: MEMORY TEST 0x8B10,
  T.G.P. TEST 0x9090, INPUT TEST 0x9170, OUTPUT TEST 0x93D0, SOUND TEST 0x96C0, C.R.T. TEST 0x98D0, GAME ASSIGNMENTS
  0x9A30, COIN ASSIGNMENTS 0x9ED0, GUN SETTING 0xAD30 (screens `test_gun_adjust` 0xB5F0, `test_gun_manual_adjust`
  0xBFE0: ADXMIN / ADYMIN / ADXMAX / ADYMAX per player; `gun_calibration_apply` 0xC720), BOOKKEEPING 0xC850, BACKUP
  DATA CLEAR 0xCD60, EXIT 0x8750. (0x98D0 was named InitSetPlayerTask from a PC pair; the PC has no test menu, so
  that pair is probably wrong; the PC name stays on the PC function only.)
- **EEPROM (arcade, 2026-10-16, cluster 0x22910):** a serial EEPROM bit-banged through port 0x1C00000
  (`eeprom_write_word` / `eeprom_read_word`, enable / disable commands). `eeprom_save` bumps a save counter
  (0x51F71C), checksums the 0x26-byte settings block (CRC seed 0xDEBDEB00, `eeprom_checksum`), keeps a second copy
  at 0x51F740, writes the 44 words from 0x51F710 and reads them back. Called from the operator menu's save paths
  and the frame tick. The PC keeps its settings in THOTD.CFG instead.
- **Boot (arcade, 2026-10-20, `boot_start` family):** `boot_start` clears work RAM, runs `boot_hw_init`, prints
  "SOUND INITIALIZE...", resets the game globals (`boot_globals_reset`), picks the first mode by region, seeds the C
  library rand with 123456 (`srand_leaf`; the game's own `rand` 0x34A50 is separate) and starts `frame_tick`.
  `boot_hw_init`: input defaults, `eeprom_load` (header "S32A", version 0x6655, two copies; a "SEGA" header keeps
  only the save counter), `apply_settings`, backup RAM checks (`backup_bookkeeping_check` 0x1D00008 and
  `backup_ranking_check` 0x1D00100: each block has a checksum and a copy; a bad block is restored from the good one,
  both bad = defaults), palette queue, `sound_init`, `coin_settings_init` (max credits 24 in the USA, else 9; the
  credits kept in backup RAM are clamped), sprites, palette, `hw_init_3d` (Model 2 coprocessor / geometrizer
  programs on a cold start, polygon RAM, log table, colour curves, texture and luma RAM), interrupts and timers.
  Nothing here matters for a port beyond the order and the settings / backup layouts.

### PC-only systems seen so far (strings)

- Chapter select: outside boss mode a game starts at chapter / event / route 0xD5B350 / 0xD5B354 / 0xD5B358 (checked
  against the event lists by 0x422808), normally 0 / 0 / 0.
- Boss mode in the main loop 0x419168 (game mode 0x5CED98 = 4, boss 0xC723B8 = 2..6, retry counter 0xC723BC,
  per-boss high-score modes 2-6), keyboard shortcuts there (F12 = `SNAP%04d.bmp` screenshot 0x412420, debug keys 1-4).
- **PC Mode characters** (`cstats.cfg`, load 0x421DF4 / save 0x422194, editor 0x426190): table 0x5CE348, 0x34 bytes
  each (name[0x20], hit points +0x20, rounds +0x24, bullet damage x +0x28, reload time ms +0x2C, hit size x +0x30),
  selected character 0x5CECA4. Defaults:

  | Character | Hit points | Rounds | Damage | Reload ms | Hit size |
  |-----------|-----------:|-------:|-------:|----------:|---------:|
  | Rogan (= arcade) | 3 | 6 | 1.0 | 0 | 1.0 |
  | G | 3 | 2 | 4.0 | 250 | 1.2 |
  | Rogan 2 | 2 | 12 | 1.0 | 0 | 0.8 |
  | G 2 | 5 | 6 | 0.5 | 250 | 1.2 |
  | Sophie | 4 | 12 | 0.5 | 500 | 0.8 |
  | Researcher | 2 | 10 | 1.5 | 500 | 1.0 |
  | (NULL PLAYER) | 5 | 8 | 1.0 | 250 | 1.0 |
- **Front end (2026-10-16, 41 PC names):** `frontend_run` 0x41DD3C (menu.pal) builds each screen as a tree of
  0x44-byte widgets (`widget_new` / `widget_add_child` / `widget_set_rect` / `_label` / `_text` / `_image` /
  `_handlers`, freed by `widget_free_tree`; tree 0x5CED88, screen id 0x5CE8A8); `frontend_input` routes keys to the
  focused widget's handlers. Screens: 1 debug menu, 2 video options (320x240 / 640x480), 3 controls (devices, key
  assignment, `controls_status_text`), 5 Creature Test, 8 high scores (default name SIMON), 9 PC Mode high scores,
  10-14 boss mode best times (mm:ss.cc, `score_time_format`), 15 and 17 not identified, plus the PC Mode character
  editor (`screen_pc_mode_edit`). Text labels are ids into the menu art (not decoded). For the switchable UI this
  is the PC side of every menu; the arcade has none of it (its only menus are the operator test menu).
- **Front-end input (2026-10-14):** the menus read the DirectInput keyboard as two 256-byte states (current
  `[0x12FDBE0]`, previous `[0x12FDBE4]`) and act on press edges (`cur & (cur ^ prev) & 0x80`). `frontend_input`:
  Escape (DIK 0x01) runs the handler (+0x28) of widget 1, else widget 3; Y (0x15) widget 2; N (0x31) widget 3; F12
  with Ctrl a screenshot; SKIDMARX / CREATURE typed with Ctrl held toggle the debug menu / Creature Test. 0x420A60 (called first)
  owns the pointer `0xD5A97C / 0xD5A980` (screen pixels): the arrow keys (main or keypad) move it with acceleration
  (x1.5 per frame up to 11 px), mouse motion (`0x12FDB98 / 0x12FDB9C`) sets it, scaled to the screen; Enter / keypad Enter / Space / a mouse-button edge (`0x12FDB84` bit 0) activates the widget under it
  (0x4277B0); 30 s idle (10 s with the debug flag) on the main menu starts the attract demo (`frontend_state` 6).
  0x42AD5C (GetCursorPos offset from 320, 240 into direction flags, SetCursorPos back) and its reader 0x42A26C have
  no callers: an unused mouse-as-joystick reader.
- Debug menus left in: Cheat / Edit Char Stats / Creature Test (0x4210FC, 0x422324: next/last body object and motion).
  **The secret codes (2026-10-20, tip: RUNESA; [teaser_status/pc_cheats.md](teaser_status/pc_cheats.md)):**
  `frontend_input` 0x41DA18 (0x41DC3C..): with Ctrl held, letter edges (DIK list 0x5CE868, ASCII table 0x5CED9C)
  shift into the buffer 0x5CE89C (reset to "00000000" 0x5A1198 while Ctrl is up); `strcmp` 0x571C79 with "SKIDMARX"
  0x5A118C toggles the debug flag 0x5CD374, "CREATURE" 0x5A11A4 toggles 0x5CE898; both rebuild the main menu. Not
  saved. 0x5CD374 adds Cheat (0x422784 -> screen 6 0x4228CC: Stage 0xD5B350 (next 0x422790), Scene 0xD5B354 (next
  0x4227CC; names 0x5CE918 / 0x5CEBA4), Free-Play 0xD5B35C (0x4228C0; `pc_continues_init` 0x4488E4 -> 0xDA0B8C: no
  continue limit), Start 0x420F70 (`game_new` 0x458588 starts there)) and Edit Char Stats (0x426188 ->
  `screen_pc_mode_edit` 0x426190) under Quit; with 0x5CE898 too, Creature Test (0x422230 -> 0x422324; BO names
  0x5F10E0, MO names 0x5F1258, yaw 0x5CEBF8 +- 0xB4 by 0x422274 / 0x422290, BO 0xD5A944 by 0x4222AC / 0x4222DC; next /
  last MO are no-ops). In play with 0x5CD374 and Ctrl (`pc_main_loop` 0x419168): 1 / 2 infinite rounds and lives for
  P1 / P2 (bits of 0x5CCEF8, applied in `pc_gun_input` 0x4358CC), 3 / 4 kill P1 / P2 (`player_in_game_frame`
  0x434C9C), R random autofire (0x5CE680: gun input mode 7), V frame pacing (0x5CE688); the HUD (0x418CC8) prints
  "%d fps [%d:%d:%d]" and "[1] %d [2] %d" (scores) at y 0x168; Ctrl + B blood colour files (0x4270A4), Ctrl + T
  0x5CD378; idle to the attract after 10 s instead of 30.
- High scores `scores.cfg` (0x425748 IsHighScore, 0x425798 InsertScore, 0x425868 / 0x4258D4 load / save), options
  `thotd.cfg`, controls (0x427494), "Continues" (0x41B304), attract demo with "Press SPACE to skip Demo" (0x41B3F8).
