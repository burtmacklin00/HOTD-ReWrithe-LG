# Survival Mode (ours, 2026-11-03)

Endless rounds of creatures in an arena taken from a stage, the game's cut creatures brought back among them, played
like the old **Call of Duty Zombies** (World at War / Black Ops; survival2, the user's 2026-11-03 brief: the first
version was too easy): rounds that keep getting harder, points for hits and kills spent between the rounds (ammo, the
SMG, a life, two perks), reloads by hand, three lives. Code: `src/game/survival.h` / `survival.cpp`; the hooks listed
under "Normal play".

In the release packages too (user 2026-11-03; CMake `HOTD_SURVIVAL_MODE`, default ON). Its "Very Experimental / Proof of
Concept" labels (the More Modes item's tag line, its help line, the 6 s notice at a game's start: v1.2.0) are gone
(survfix3, user 2026-11-05: "remove very experimental / proof of concept from survival"). Its options are
`--survival*` ([porting.md](porting.md#experimental-modes)).

## How to play / test

- Menu: **More Modes > Survival** (the players are asked first); **Survival Arena: <name>** below it picks the arena (click:
  the next one). Sonic Mode's preset (More Modes) applies to Survival as to every game.
- Command line: `hotd --survival <arena>` (`courtyard`, `dining`, `lab`, `octagon`, or 1-4; the old `bedroom` /
  `furnace` give the dining hall / the laboratory), with `--players 1..4`.
  Options: `--survival-seed N` (the director's random stream: the same seed plays the same rounds), `--survival-smg-wave N`
  (testing: the SMG free from round N; default 0 = only bought), `--survival-cap N` (creatures at once, 1-64; default
  24), `--survival-no-cut` (no cut creatures).
- Self-test (headless, muted, no window): `hotd --survival-test N [--survival <arena>]`: the autofire plays N rounds (lives
  kept, buying by the test rule below), then prints a line per round (creatures, cut creatures, peak at once, frames) and
  `SURVIVAL TEST: result=ok|FAIL arena=.. round=.. score=.. points=.. spent=.. hits=.. headkills=.. buys=ammo..,smg..,life..,quick..,bigmag..
  spawned=.. cut=.. runners=.. killed=.. alive=.. peak=.. bodies=.. frame=.. smg=.. bad_access=.. sfx: ...`. Exit code 6
  when the frames ran out first. Deterministic: two runs with the same arguments print the same lines.
- Balance runs (headless, real lives, game over at the end): `hotd --survival <arena> --mute --frames 200000` with
  `--autofire [--autofire-every N]` (a perfect aim, a handgun pull every N frames) or `--bot S --bot-skill K` (the
  launcher's bot; `--bot` in Survival also shops by the test rule); the last line `SURVIVAL: result=gameover arena=..
  round=.. score=..`. The test rule (`SurvivalMode::testBuy`): the SMG first, ammo when its reserve is under two
  magazines, a life when one was lost, Quick Reload, Big Mag, then lives. Results under "Balance".
- Testing helpers: `HOTD_SURVIVAL_AIM_UP=n` (the autofire aims n arcade pixels above the target's centre: head kills),
  `HOTD_SURVIVAL_TYPE=t` (every spawn that type: a cut creature's look and behaviour),
  `HOTD_SURVIVAL_START_WAVE=n` (the first wave is n: the limits' stress run), `HOTD_SURVIVAL_WAIT=n` (the lead-in takes its
  (n+1)-th creature wait; each wait is logged with the camera: how the arenas were found), `HOTD_SURVIVAL_START=c:e`
(the lead-in from chapter c's event e: any wait of any chapter, with the arena's types), `HOTD_SURVIVAL_DUMP=<file>`
(with `--render`: the camera, the spawn places and the stage's walls / floors near it, then the creatures' places every
4 frames: "Arenas"), `HOTD_SURVIVAL_NAME=X` (a new
  table row filed under that name, also headless), `HOTD_SURVIVAL_POINTS=n` (seat p gets n x (4 - p) / 4 points at
  each round's clear: the shop's pictures), `HOTD_SURVIVAL_BUYER=saver|upgrades` (the test buyer saves for the
  Magician's Hand after the SMG / after the SMG, ammo, Quick Reload and Big Mag), `HOTD_WEAPONS_FORCE=k` +
  `HOTD_WEAPONS_FORCE_KEEP=1` (only weapon k, given back when used up: the time to kill). The round lines of the
  test end `earned E banked B lives lost L` (every seat's points earned so far / unspent at the clear, the lives lost
  in the round) and the result line `ttk=<weapon>=kills/frames,..` (per the killer's weapon: the kills and the mean
  frames from a creature's first hit to its death).

## How it works

Layered on the arcade logic like Boss Mode. A normal game of the arena's chapter starts at the arena's event (its
section 0, as Boss Mode's start: the room's draw zone and colours). The **lead-in** (the script up to the arena) runs
at once, undrawn and silent (Runtime::frame, as Boss Mode's walk-up); every creature the spawn opcodes ENTB / ENTB3 /
ENTBE make is captured (`Game::evt_spawned`: its place, facing and kind are kept; the task is unlinked before it first
runs). Waits that only those creatures would have ended are passed after 2 s (a WAIT_ITEM's flag set, a creature
wait's camera stop marked done, after 5 s the wait itself as the teaser's guard does). At the arena's creature wait
(the n-th WAIT_ENNUM / WAIT_ENALV after captures) the **event script is frozen**: from then on Survival's director runs
in the event task's slot each frame (Runtime::run, `evt_run`), so its creatures are made in the game's task group with
ENTB3's fields (`Runtime::survival_spawn`: kind 0, the standard enemy). Everything else is the game's own logic: the
creatures walk to the attack places, attack, take hits, die, score; the camera turns toward the attackers and back
(the original's game camera), so the arena holds or pans slowly.

**Spawn places (arenas, 2026-11-05):** each arena lists its **doors** (`SurvivalArena::doors`: the room's entrances,
world x / floor y / z, chosen and checked as under "Arenas"): the creatures come from these, facing the camera, with the
flags of the arena's own captured creatures, spread +-8 units (a doorway is narrow). An arena without doors (and the
finding helpers `HOTD_SURVIVAL_START` / `HOTD_SURVIVAL_WAIT`) keeps the old rule: the creatures the lead-in captured at
that wait, plus those of earlier sections within 450 units of the camera, at most 16; a place nearer than 110 units
moved out to 110 along its own direction; with fewer than 4, each also turned 30 degrees either way about the camera;
spread +-20. (That rule is why the old arenas felt wrong: the captured places are where the original's creatures stand
up close, so most were born 110 units away in a ring; the earlier sections' places were often behind a wall in the
next room or on another floor, and the turned copies stood in walls or in the furnace's lava.)

**Rounds (round r; Call of Duty Zombies' curve, `SurvivalMode::roundCount` / `roundHealth`):**

| | Rule |
|---|---|
| Creatures | 6, 9, 13, 17 (rounds 1-4), then `21 + 3(r - 5)` to round 9 (24 ... 33; survfix2: CoD's 18 -> 24 step at round 5 smoothed), from round 10 `24 x 0.15 x r` (36, 40, 43 ..); + 25 % for each other player; at most 150 |
| At once | `min(cap, 1 + r)` in rounds 1-3 (2, 3, 4), then `2 + r`; the runners' round `4 + r` |
| Spawn gap | `max(12, 80 x 0.93^(r - 1)) + rand(20)` frames |
| Hit points | the type's own to round 3, then + 12 % a round to round 9 (x 1.72), then x 1.1 a round (CoD: + 100 to 9, then x 1.1) |
| Runners | none in rounds 1-3; from round 4 a share `min(6(r - 3), 60) %` start in their dash (start sub-state 2: the type's motion entry 2, `walk_variant` 1, so they dash again after each stop); `min(8(r - 3), 70) %` with the AI at once (survfix2: were 8 / 10 % a round) (flag bit 22) |
| Aggression | the game's own rank (difficulty_level's 0x51F492, 0-15: how often creatures attack) gets a floor of `(r - 1) x 2 / 3` |
| Mix | the arena chapter's own standard types, the early rounds from the weaker end of the list; from round 2 a cut creature with chance `min(5 + 2r, 25) %` |
| Special rounds | every 5th: 5, 15, 25 .. **runners** (every creature dashing, AI at once, the lighter half of the types, 3/4 the count, more at once); 10, 20 .. **brutes** (the heavier half and the cut creatures, half the count, x 1.25 hit points). Cleared: **Max Ammo** (every gun full, the SMG's reserve too) |
| Break | 8 s ("ROUND n CLEARED"; the shop is open, a countdown at the top); **no free life** |

**Lives:** at most **3** when a seat joins (fewer when the Lives setting is lower), up to 5 with bought ones. No
continue: when every player is out, game over (as Boss Mode); its results line ("REACHED ROUND n POINTS p KILLS k")
stays over the game over scene, then the menus: the name entry for a new row ("ROUND n p PTS"), then High Scores on
the **Survival** table (`survival_scores.txt` in the settings folder: points, round, arena, name; ten rows; shown as
"Round n" and the points).

**Out until the next round (2-4 players; survrevive, user 2026-11-05: "if a player loses all their lives, they come
back in the next wave where they get half their points back"):**
- A player who loses the last life while at least one other player is still in play is **out for the rest of the
  round**: no CONTINUE?, no countdown, no continue at all; the start button does nothing (`Game::seatsHeldOut`:
  `player_start_check`, `coop4_seat_frame`) and no PRESS START is shown. The seat's line reads **BACK NEXT ROUND** in
  the score font in the seat's colour, with its points (frozen) under it; its gun is off (no aim, no shots, no shop),
  its torches and bullets taken off. Checked after each frame (`SurvivalMode::tick`), so the CONTINUE? never shows,
  not even for a frame: the seat goes from the game's state 4 straight to waiting (9).
- When the next round starts (with its ROUND n banner) every out player **comes back**: through the game's own join
  (players 1 / 2: `player_st3_join` / PlStartInit's join row; players 3 / 4: `coop4_seat_start`), with Survival's
  start lives (the Lives setting's start lives, at most 3: the same as a game's start), **half the points it had when
  it went out, rounded down to the points' step of 10** (it loses half), its weapons as they were (Survival keeps
  weapons) and the magazine it had in the hand (no free refill). Its corner shows **"nP IS BACK"** in its colour over
  its weapon line and its new points for 3 s.
- Every player out at the same time (or the last one in play losing the last life): game over as before (the results,
  then the Survival table). The points counted for the table are the points earned (unchanged by the halving).
- 1 player: unchanged (game over at 0 lives). CPU teammates follow the same rule (their start button is held like a
  player's). Deterministic: it uses only game state (no input, no random numbers), so it is the same on every PC; Survival
  itself is still local only (see Open).
- Test: `HOTD_SURVIVAL_DRAIN=<seat>@<round>[+frames],..` sets that seat's lives to 0 that many frames (default 300) into
  that round, as a creature's last hit would (`--lives-kept` leaves the seat alone until it is back). The log has
  `survival: nP is out ..` / `nP is back .. lives L, points P (half of Q), weapons W (had W), .., ok`, the result line
  `out=N back=N revive=ok|FAIL`. E.g. `HOTD_SURVIVAL_DRAIN=3@2,4@2+200,2@3+100,1@4+100 hotd --survival-test 6 --players 4`.

**Points** (each seat its own; `Game::survivalScoring`: the game's own `add_score` does nothing in Survival, and
`enemy_part_hits` reports each frame a creature was hit, `Game::survival_hits`): **10** for each frame a seat's shots hit
a creature, **50** for the kill (the game's own killer), **100** for a kill with a head hit (the game's eye / head hit,
part class 1: `G::eye_hits`). The score in the table is the points earned (spending does not lower it).

**The shop (between rounds, weapons 2026-11-04: [weapons.md](weapons.md#survival)):** nine items in three rows, each
with an in-game model turning over it; a shot at its name or model buys it (the shot is not fired): the weapons
**SMG 1000**, **ASSAULT RIFLE 2500**, **DOUBLE PISTOLS 1500**, **SHOTGUN 3000**, **MAGICIAN'S HAND 8000** (prices
from `data/weapons.ini`; into the hand at once, full; lost when its magazine and reserve are empty), **AMMO 500** (the
reserves of every weapon held full again; the handgun never runs dry), **LIFE 1500** (+1, at most 5), **QUICK RELOAD
2000** (reloads take half), **BIG MAG 2500** (magazines x 1.5). "NOT ENOUGH POINTS" / "NOT NOW" when it cannot be
bought. The weapons are **kept after a round** (the user's answer).

**Reload by hand:** Survival ignores the Auto Reload setting (`autoReloadSeats` / `autoReloadWanted` in main.cpp: off
while `survival.active()`, the setting itself untouched, every other game as set). A pull off the screen starts the
reload: each weapon's own time (the handgun 36 frames, the SMG 72 ...: weapons.md), from its reserve; the gun does
nothing meanwhile ("RELOADING").

**HUD** (redone 2026-11-04, survfix; one title block for 1-4 players and the shop's stacks, survfix2:
[weapons.md](weapons.md#survival)): in the font of the game's own score line (Score Display's `SCORE:00000`), on the
arcade HUD's cell grid with shadows: one title block at the top centre for 1-4 players, rounds and shop alike ("ROUND
n" over "LEFT k"; in the break "ROUND n IN s" over "SHOOT AN ITEM TO BUY" or the moment's buy / NOT ENOUGH POINTS;
at the end "REACHED ROUND n" over "POINTS p   KILLS k" over the fallen players and the GAME OVER card), per player
the weapon with `rounds/reserve` and `PTS:00000` over the arcade's bullets (past 6: one bullet and "X N"), the shop's
signs (model, name, gold price, the seats that can buy it; grey when nobody can), the moment's banner in the gap
between the RELOAD prompts (the arena's name, ROUND n RUNNERS, ROUND n CLEARED MAX AMMO). Drawn by
`Hud::drawSurvival`. The name entry ("ROUND n  p PTS") and the Survival table are the menus' own screens.

## Creatures that never attack (survfix, 2026-11-04)

The user: "creatures far away that never attack the player must not matter: they must not hold up a round's end / wave
clear, and must not count as needed kills", but those behind cover, hiding, popping out, waiting at a door or scripted
to come later do matter. Decided from each creature's AI, never from distance or visibility alone
(`game/engage.h` / `engage.cpp`, `EngageWatch`), every frame for Survival's own creatures (the
script's own creatures stay as the arcade has them):

| The creature | Rule | Evidence (the port of the arcade's code) |
|---|---|---|
| any routine but `enemy_main` (0x77410): `enemy_wait_camera_stop`, the scripted kinds (1 waking, 2 kicking, 8 rising into view, the door / corner / hanging / dropping kinds, bosses' helpers), dying, corpses | **matters** | their own routines decide when they come or leave (enemy_kinds.cpp, enemy_scripted.cpp) |
| `enemy_main` in AI states 2-9 | **matters** | enemy_ai.cpp: a waiting or front attack position is held; attack_slots.cpp `attack_slots_update` re-orders the waiting positions by depth every frame, so the queue always moves on to a front position |
| `enemy_main` state 1 with its delay (+0x2EC) running | **matters** ("comes later") | enemy_ai.cpp: the delay counts down, then the turn and the step |
| `enemy_main` state 1 within 70 units of its target, or still getting 2 units nearer within 300 frames | **matters** (on its approach path) | enemy_ai.cpp state 1: it moves on to 2 / 3 within 70 |
| `enemy_main` state 1, beyond 70 units and no nearer for 300 frames | **never attacks** | state 1 leaves only within 70 units: a creature that has stopped getting nearer (walking on the spot, a standing motion, a stale target) never will |
| `enemy_main` with flags bit 0 clear for 300 frames (not dying) | **never attacks** | enemy.cpp `enemy_main`: `enemy_ai` runs only with flags bit 0 |
| `enemy_main` in state 0 or above 9 for 300 frames | **never attacks** | enemy_ai.cpp: its switch acts on states 1-9 only |

A creature that never attacks is sent away by the game's own leave (its routine set to `kind10_remove` 0x76FC0 with
flags bit 21 cleared: `enemy_leave` frees its attack position and target, takes it off `enemy_count` /
`enemies_alive` and ends its task), so the counts every wait reads (Survival's round, the script's WAIT_ENNUM /
WAIT_ENALV, the camera stops) let go. Survival logs `survival: a type N creature will never attack (why): sent away`
and counts them in the test line (`retired=`), not as kills. In the runs so far (Survival 12 rounds x 4 arenas with
the autofire, with the launcher's bot and without shooting) no creature met a "never attacks" rule by itself:
every standard creature either approached or waited in the queue of waiting positions (with one player only position
0 attacks; up to ten stand about 70 units off, some behind the camera in the furnace arena: they matter and come in
turn).

Test (`HOTD_SURVIVAL_TEST_IDLE=1 hotd --survival courtyard --frames 6000 --lives-kept 1 --mute`, no shooter): round 1
gets A (put back on its place each frame of its approach: walks on the spot, never attacks) and B (a 1500-frame delay:
comes later) 250 units out of view; the test kills whoever attacks. Result: A sent away at frame 442, B attacked at
4571, round 1 cleared at 4717 (after B). With `HOTD_SURVIVAL_KEEP_IDLE=1` (the old rule) the round never ends.
`HOTD_SURVIVAL_TRACE_ALIVE=n`: every n frames each creature's routine, AI state, delay,
attack position, distance and depth.

## Arenas

**Chosen again (arenas, user 2026-11-05: "the chapter locations are weird, especially Chapter 2: it does not make
sense, is not open enough"; the old set was the courtyard, the green bedroom, the laboratory and the furnace bridge: two
from Chapter 3, none from Chapter 4).** One arena a chapter, the most open place where the camera stops:

| # | Key | Menu name | Start | Wait | Doors | Types (the chapter's kind 0 records) |
|---|---|---|---|---|---|---|
| 1 | courtyard | Chapter 1: the courtyard | chapter 1 event 1 | 1st | 5: the mansion gate, three across the yard's far side (between the flower beds' kerbs), the west side | 11, 28, 24, 16, 57, 29, 30 |
| 2 | dining | Chapter 2: the dining hall | chapter 2 event 12 | 3rd | 4: the north door, three across the far end (a step up) | 11, 28, 16, 29, 51, 33, 45, 30, 32, 58 |
| 3 | lab | Chapter 3: the laboratory | chapter 3 event 1 | 1st | 4: the west door, the north corridor, the two far corners | 28, 61, 86, 62, 73, 83, 3, 29, 53, 65, 72, 63, 84 |
| 4 | octagon | Chapter 4: the octagon hall | chapter 4 event 0 | 2nd | 4: three at the machine's end of the hall, the east corridor | 11, 53, 72 |

(The dining hall is on Chapter 2's other route, events 10-13: the arena starts at its event 12 directly, as Boss Mode
starts a boss. Chapter 4 has only three standard types: kind 0 records of 11, 53, 72.)

**How they were found.** Every creature wait of all four chapters on every route (the lead-in from each chapter's
event 0 / 1 and from each branch's first event, `HOTD_SURVIVAL_START=c:e` + `HOTD_SURVIVAL_WAIT=99`: 96 camera spots,
44 in Chapter 1, 24 in Chapter 2, 24 in Chapter 3, 4 in Chapter 4), each pictured headless (`--render`, the space 60
frames after the arena is taken and round 1 at +700) and measured from a dump of the stage (`HOTD_SURVIVAL_DUMP=<file>`:
the camera, its view matrix, the spawn places and the stage's walls / floors near it: the Ragdoll mod's triangles, the
same the dead bodies collide with; then each creature's place every 4 frames). The numbers (scratch tools `arenas.py`,
`floor.py`, `check.py`, `find.py`):

- **walkable floor in view**: the floor triangles at the camera's floor height (+-8, steps of 6 joined) connected to
  the one under the camera, the part within the view (+-36 degrees), in 100 x 100 units;
- **nearest wall / median depth**: rays from the camera over the view at body height (12 and 30 units up) to the first
  wall triangle;
- a door: on that floor, a straight walk to the camera with no wall at foot height (5 units: Chapter 1's flower-bed
  kerbs are 10 high), room around it; then checked in play: the creatures' walks against the walls, and pictures every
  600 frames (a door was dropped when the camera, turning after its creatures as the original's does, ended up facing a
  wall close up: the courtyard's east side and gate behind (a hut), the dining hall's doorway behind (a curtain), the
  octagon hall's corridor behind (a step lower: the camera looked at the floor) and west corridor (rock)).

| Spot | Walkable floor in view | Nearest wall | Median depth | Verdict |
|---|---|---|---|---|
| Ch1 courtyard (kept) | **52.7** (129 in all) | 258 | 652 | the most open place of the game |
| Ch1 mansion bridge, event 4 | 0.0 (the wide view is of the yard below) | 366 | 1200 | the camera on a narrow bridge; "HOLD YOUR FIRE": a hostage scene |
| Ch2 green bedroom (old) | 0.2 | 57 | 60 | small, the creatures born in a ring at 110 |
| Ch2 dining hall (new) | **2.3** | 35 (the table's end) | 174 | the largest checked room of Chapter 2: 275 x 100, the table in the middle, doors at both ends |
| Ch2 gallery hall before the boss, event 9 | 0.5 | 23 | 99 | looks open, but a 35-wide gallery around a stairwell |
| Ch2 pillar hall, event 0 | ? | (166) | (1200) | its walls / floors are not in the dump (a world the Ragdoll mod's walls do not cover): could not be checked; pillars close up |
| Ch3 laboratory (kept) | **3.4** | 129 | 281 | a real floor, three ways in |
| Ch3 furnace bridge (old) | 2.1 | 122 | 515 | a 20-wide walkway over lava (the lava 15 lower): one way in, the turned spawn copies stood in the lava |
| Ch4 octagon hall (new) | **16.0** | 127 | 229 | the hall at Chapter 4's start: a wide floor round the machine, the corridors |
| Ch4 fire corridor, event 3 | 11.7 | 20 | 87 | a 50-wide corridor |

The rest (corridors, stairs, the dark Chapter 1 halls and Chapter 3's corners) had less than 13 in view or a wall
within 40. Contact sheets of every spot and the before / after pictures: the worker's scratch folder (`arenas/sheets`).

**Checked:** the creatures' walks in long runs with every door (the autofire every 40 frames, `--frames 12000` and
`30000`, lives kept): courtyard 75 + 141 creatures, dining 93 + 192, lab 111 + 262, octagon 36 + 164 (the earlier door
sets included), 0 steps crossing a wall at foot height; no creature sent away (`will never attack`: 0).
`--survival-test 10`: ok in all four (see the worker's STATUS.md).

The balance tables below were measured on the old arenas (courtyard / bedroom / lab / furnace).

## Cut creatures

Sources: docs/cut_content.md (the inventory), the ROM's per-type tables read for each type (maincpu: BO_ names
0xC8A60, joints 0xC73C0, scale 0x118060, hit radius 0x1181E0, motion lists `enemy_motion_lists` 0xC6B60, attacks
0xC6F10, attack choice 0xC6D30, death motions 0x762C0), gallery_cut.cpp (their texture areas), the event lists (no
record spawns any of them; scratch tool recs.py: 1037 records of the 4 chapters).

| Type | Name | Skeleton | Motion data | Standard-enemy tables in the ROM | Brought back as |
|---|---|---|---|---|---|
| 52 | Disiprin (laced body suit, gas mask) | 18 joints | none of its own | **all set**: motion list 0xC6B00 (the zombies'), attacks 0xC6EB0, choice 0xC6CE0, death 1 | its own tables as they are: a standard creature (kind 0), 160 hit points |
| 12 | Debu (an earlier Bentley) | 18 | none | **all set**: list 0xC6B30 (Bentley's / the chainsaw's), attacks 0xC6EE0, choice 0xC6D10 | its own tables, 220 hp |
| 56 | Neozom (Sam's parts, own clawed chest) | 18 | none | **all set**: list 0xC6B10 (Zom's), attacks 0xC6EC0, choice 0xC6CF0 | its own tables, 120 hp |
| 10 | Syndy (leather, mask, stick) | 18 | MO_synd_demo only (a demo pose) | all 0 | borrows type 28's (BO_neil) motion list, attacks, attack choice and death motion; 180 hp |
| 66 | Baba (skull face, coat) | 18 | none | all 0 | borrows type 29's (BO_siriru); 140 hp |
| 5 | MrG (masked wrestler) | **19** | 4 own: MO_mrg_aruki / kiai / naguri / taore | all 0 | **not yet** (open question): 19 joints, so no 18-joint creature's motions fit (the frame records are read with the body's joint count); a list of his own four motions would cover walk / shout / punch / fall, not the hit reactions |
| 39 | Lubin (officer, tattoos) | **16** | none | all 0 | **not yet**: the 16-joint creatures (Drake, Kageo, Moody) have no standard tables either (their own kinds' code) |
| 75 | Hiru (leech, 9 segments) | **9** | 6 own: MO_hiru_a / blur / blurb / walk / y / yy | all 0 | **not yet**: no behaviour to borrow (a crawler) |

So three of them (Disiprin, Debu, Neozom) were left in the ROM as complete standard enemies: the game only never
spawned them; Syndy and Baba have bodies and textures but no tables, and move with their relatives' (same joint count).
The borrowed words are written into the program ROM image at the game's start (`survivalPatches_`; a new game reloads
the image) and only in a Survival game.

**Their look:** their textures are in areas the chapters do not load (gallery_cut.cpp: Disiprin area 2 (the
attract's), Debu 5, Neozom 6, Syndy 7, Baba 3); their part models are drawn with that area's texture set through the
cut-scene viewer's `actorAreas` (the chapter's own set when it is that area, else Chapter 4's stage's, which has
every area, loaded ahead in the background), with the colour rows of their Gallery look (`actorAreaLook`). **PC
models:** the PC files have no meshes for them, so with the PC models Survival leaves them out (the regular types only).

Checked headless (`HOTD_SURVIVAL_TYPE=t --survival-test 4`, every spawn that type, 50 each): all five walk in, attack,
take hits and die; no access outside the mapped memory (Syndy without the borrowed attack table went to NaN positions
on her first attack: motion 0, a boss's, at the attack slot). Pictures: scratch `cut\sheet.png`, `cut\sheet2.png`.

**Their damage (fixed 2026-11-03, the user's report).** The part tables `part_hit` reads by type (damage.cpp), checked
for every type Survival spawns (scratch `audit.py`: the 23 arena types and the five cut ones; only these two failed):

| Creature | Seen | Cause | Fix (written with the other borrowed words, set back at the game's end) |
|---|---|---|---|
| Syndy (10), the tall woman with the mallet | could not be hurt | her `part_damage_table` row (0x12F9F0 + 10 x 0x100) is all 0, and every part role has armour 0xFF: a hit did only the difficulty bonus (0x14AAF0: +20..30 at rank 0, 0 or less from rank 6-7), so she died in the early test waves (rank 0) and never once the rank rose | Neil's (28) damage row |
| Baba (66) | the body "glitched out" when shot | its roles 2-4, 6, 7, 10, 11, 14, 15 (head, arms, legs) have armour 1 and sever 1, but its `part_replacement` rows (0x11DFF0) are empty: no stump or damage models, so the first hit on each part cut it off with nothing in its place (the skinned body stretched to the lost joints). Siriru's (29) replacement models are another creature's meshes, so they are not borrowed | Siriru's damage row; every role armour 0xFF and sever 0 (as Syndy's and Disiprin's are): it takes hits and dies whole |

Disiprin (52) has the same armour-0xFF roles with a complete damage row (no part loss, by design); Debu (12) and Neozom
(56) have full replacement rows (their own damage models). Checked: `HOTD_SURVIVAL_TYPE=10|66 HOTD_SURVIVAL_START_WAVE=10
--survival-test 10`: Syndy's 35 cleared at frame 2608 (was 4299), Baba's at 1471 (was 5055); renders of Baba
before / after: scratch `workers\survival\shots\baba.png`.

**Their shadows (fixed 2026-11-05, the user's report: "the hammer lady's shadow is just a squished-down Y level of
her").** `part_draw` (0x36240) draws a creature's shadow, when body +0x6C bit 1 is set, by drawing each part again
under the shadow projection (stored matrix 3, `shadow_setup`: onto the floor along the shadow light). The model it
draws is the part role's entry of the type's **shadow table** (`0x14BA80[type]`, then `[role]`): low-polygon dark
copies, `<part>_DD` / `semusikage_*` ...; a type whose word is 0 gets the part's **own textured model** squashed flat
instead. The words, read from the ROM: 0 for MrG (5), Syndy (10), the Hangedman (50, 55, 69) and the Hermit (64);
every other type has a table, and the 17-role human-shaped creatures share the zombies' 0x14A730 (`ff_*_DD`: Sam,
Neil, Cyril, Samson, Harris, Gilmore, Bentley, Kageo, Bourbon, Robert, Burner, the other cut ones, ...). So Syndy cast
a flat silvery copy of herself where every other creature casts a dark shadow. That is the original code's own path
(the arcade would draw her so too, but never spawns her: nothing to compare in MAME). Her part tree has the same 17
roles in the same order as theirs, so Survival now gives her her donor Neil's word (the zombies' table) with the other
borrowed words: the same shadow models, opacity and floor projection as the others, posed by her own skeleton. Her
mallet (drawn by her part hook, not a part) has no shadow, as Gilmore's hammer has none. Of the other types without a
table, only the Hangedman's Chapter 1 copy (55, carrying Sophie off) draws a shadow pass (a trace of every shadow
model drawn, per type: Chapters 1-4 with `--strict --default-route`, and `--survival-test 10`): the original's own
flat copy, while he is high in the sky in that cut-scene (not on screen); the ROM has no shadow models for him, so it
is left as the arcade draws it. MrG and the Hermit draw no creature shadow anywhere the game runs. Pictures: scratch
`workers\shadow\syndy_shadow_before_after.png`.

Not found: cut music in the arcade sound ROMs (the sound test's 8 / 10 / 13 are still undecoded: docs/cut_content.md);
GitHub issue #4 could not be read (no network in this session).

## Limits (and what Survival raises)

The original's fixed tables that bound how many creatures can be in play, read from the port:

| Limit | Where | Size | In the original | Survival |
|---|---|---|---|---|
| Body slots | `player_bodies` 0x51F440 (16 words: creatures 0-13, the players' bodies 14 / 15); a body's slot (+0x2E6) also picks its 0x348-byte place in the three skin buffers (0x51FDC8.., 0x3480 bytes each = 16 places) | **14 creatures** | a 15th body got no slot: +0x2E6 stayed 0, so it shared slot 0's skin place and its table entry (freeing either cleared the other's) | **raised**: when 0-13 are taken, slots 16.. in Survival's own table (0x7E0000, outside the original map; `Game::extraBodySlots` = cap + 8 - 14, 18 by default), the skin buffers 0x348 bytes larger each per extra slot (`skin_buffers_alloc` at the chapter's heap reset). Normal play: `extraBodySlots` 0, every path as before |
| Attack places | `attack_slots` 0x51F9F0, 14 x 0x14 (0 / 1 front, 2-13 waiting) | 14 | the 15th creature waits with place 14 ("none") until one is free | unchanged: the extra creatures queue and come in as places free (the original's own rule) |
| Attackers list | `attack_users` 0x53E1C0 | 13 (checked: `attacker_list_add` stops at 13) | | unchanged |
| Depth list | `depth_list` 0x541470..0x542070, 12-byte entries | 256 | not checked when added | unchanged (24 creatures + the room's props are far below 256) |
| Shot candidates | `shot_candidates` 0x53F070..0x541470, 0x24 bytes | 256 | not checked | unchanged (only parts the ray crosses are added: 24 creatures in one line would be needed) |
| Task / object memory | the heap 0x543B00..0x5FFFF8 (0x2F13B words); each creature 0x3C8 + its parts (0xD8 each) and pose buffers | ~750 KB | "memory allocation error" and a hang when full | unchanged; 24 creatures + corpses fit (the stress run below) |
| Creatures at once | Survival's own cap | | the scripts send 2-6 | **24** by default (`--survival-cap`, at most 64; the body slots follow it) |

Stress run (`HOTD_SURVIVAL_START_WAVE=20 --survival-test 23`, and 25 waves from wave 1 in all four arenas): 24 creatures
alive at once, **24 bodies at once** (10 past the original's 14), no access outside the mapped memory, every creature
killed (1100 in 25 waves).

## Balance (headless bot runs, 1 player, Normal, 2026-11-03)

The round reached at game over (3 lives, buying by the test rule; `--frames 200000`, the four arenas):

| Player | courtyard | bedroom | lab | furnace |
|---|---|---|---|---|
| autofire, a pull every 15 frames (perfect aim) | 13 | 11 | 11 | 12 |
| autofire every 25 | 11 | 8 | 11 | 11 |
| autofire every 40 | 11 | 4 | 9 | 9 |
| bot skill 0.9 (seed 1 / 2) | 7 / 8 | 6 / 6 | 7 / 7 | 7 / 8 |
| bot skill 0.75 | 7 | 4 | 5 | 5 |
| bot skill 0.6 (seed 1 / 2) | 4 / 5 | 4 / 4 | 4 / 4 | 5 / 4 |
| bot skill 0.3 | 4 | 3 | 4 | 4 |

So a sure, quick shot reaches rounds 9-13, a good one 6-8, a weak one 3-5 (the target: a decent player round 8-12).
The bedroom is the hardest (its creatures come close fast). (The first Survival had +1 life every 5th wave, the free
SMG from wave 3 and Auto Reload: the user found it too easy.) Logs: the survival2 worker's `logs\bal_final\`.

### The balance pass (survfix2, 2026-11-04)

Targets (the user's): the first buy in round 2-3, the Magician's Hand in round 8-10, each weapon worth its price,
rounds harder steadily without spikes. Runs: `--survival-test 15 --survival <arena> --bot <1|2> --bot-skill <0.9|0.75>`
(lives kept, the four arenas, 8 runs a row), the buyers `HOTD_SURVIVAL_BUYER=saver|upgrades` with the autofire, the
time to kill `HOTD_WEAPONS_FORCE=k HOTD_WEAPONS_FORCE_KEEP=1 --survival-test 10 --survival-weapons-wave 1` (perfect aim,
a pull every 15 frames).

Per round, bot skill 0.9 (the means of 8 runs; before = the curve of 2026-11-03, after = this pass):

| Round | Points earned (all so far) | Earned in the round | Lives lost before | Lives lost after | Creatures before / after |
|---|---|---|---|---|---|
| 1 | 495 | 495 | 0.0 | 0.0 | 6 / 6 |
| 2 | 1268 | 772 | 0.1 | 0.1 | 8 / 9 |
| 3 | 2458 | 1190 | 0.1 | 0.1 | 13 / 13 |
| 4 | 4015 | 1558 | 1.1 | 0.4 | 18 / 17 |
| 5 (runners) | 5345 | 1330 | 0.8 | 0.8 | 18 / 16 |
| 6 | 7560 | 2215 | 0.8 | 0.5 | 26 / 24 |
| 7 | 10120 | 2560 | 2.8 | 1.5 | 28 / 27 |
| 8 | 13172 | 3052 | 4.2 | 4.0 | 30 / 30 |
| 9 | 16824 | 3651 | 6.2 | 5.4 | 32 / 33 |
| 10 (brutes) | 19188 | 2364 | 5.2 | 3.2 | 18 / 18 |
| 11 | 23585 | 4398 | 9.2 | 10.1 | 40 / 40 |
| 12 | 28242 | 4658 | 13.9 | 9.6 | 43 / 43 |
| 13 | 33270 | 5028 | 15.0 | 13.4 | 47 / 47 |
| 14 | 39066 | 5796 | 18.1 | 14.5 | 50 / 50 |
| 15 (runners) | 43232 | 4166 | 11.0 | 7.5 | 40 / 40 |

(Skill 0.75: the same points within 5 %, lives lost after 0 / 0.6 / 0.6 / 1.1 / 2.8 / 1.9 / 3.1 / 6.8 / 9.0 / 6.2 /
17.0 ...) Before, round 4 cost more lives than rounds 5-6 and round 7 three times round 6 (the 18 -> 24 count step and
the runners' 8 / 10 % a round); after, the losses rise round by round, the special rounds (5, 10, 15) a breather.

Buying (the round whose break it was bought in; the autofire, a pull every 15 / 25 frames, the four arenas): the
**SMG (1000) in round 2** in every run (rounds 1-2 earn ~1200); the **Magician's Hand (8000)**: a saver (the SMG, then
everything for the Hand) **in round 8**, a buyer of the SMG, ammo, Quick Reload (round 4) and Big Mag (round 6) **in
round 9** (all 16 runs). At 5000 a saver had it after round 5.

Time to kill (frames from a creature's first hit to its death, by the killer's weapon; rounds 1-10, perfect aim):

| Weapon (price) | courtyard | bedroom | lab | furnace | before (courtyard / lab) |
|---|---|---|---|---|---|
| Handgun (-) | 175 | 274 | 198 | 269 | 178 / 174 |
| SMG (1000) | 33 | 109 | 51 | 99 | 29 / 53 |
| Double Pistols (1500), 100 -> 200 % | 33 | 141 | 53 | 120 | 161 / 149 |
| Assault Rifle (2500) | 24 | 93 | 46 | 92 | 25 / 33 |
| Shotgun (3000), 100 -> 130 % | 21 | 153 | 40 | 174 | 51 / 50 |
| Magician's Hand (8000), 250 -> 350 % + its burst | 35 | 109 | 33 | 101 | 66 / 52 (with the root fix) |

The Double Pistols were the handgun: both guns' hits on one creature in a frame count once, so the second gun only
helped against a second creature; at 200 % a pull of both is worth two handgun shots. The shotgun's pellets on one
creature count once too (130 %; still weak at the long range of the bedroom and the furnace: its 170-unit range). The
Hand's burst marked the body's root when the creature stood right at the camera (never damaged: the round never
ended; fixed). Rounds reached at game over (real lives, before -> after; courtyard / bedroom / lab / furnace): bot 0.9
7/6/8/7 -> 9/5/8/7, bot 0.6 6/4/4/5 -> 5/4/4/4, autofire every 25 frames 8/8/8/10 -> 9/8/9/10, every 40 6/4/6/7 ->
9/7/9/9 (the stronger Double Pistols).

## The SMG and the other weapons

Since 2026-11-04 the SMG is one of the six weapons of [weapons.md](weapons.md) (data-driven:
`data/weapons.ini`), as survival2 tuned it: 30 rounds (45 with Big Mag), four magazines of reserve, a held trigger
fires every 5 frames, a pad's / light gun's pull a burst of 3, the handgun's damage. `--survival-smg-wave N` still
gives it free from round N; `--survival-weapons-wave N` every weapon (testing).

**Sounds** (the weapons' in `data/weapons/`, the SMG's also in `survivalSoundDir()`: the first data folder with
`data/survival/`):

| File | Played | Stand-in until the file is there |
|---|---|---|
| `data/weapons/smg_fire.wav` (or `data/survival/`) | each SMG shot (instead of the handgun's 0xA01208 / 0xA01207) | the handgun's shot (PC cue 30) 1.4 x faster, its first 40 %, 90 % volume |
| `data/weapons/smg_reload.wav` (or `data/survival/`) | the reload with the SMG (instead of 0xA01200) | the reload (PC cue 28) at 0.8 x |
| the other weapons' (weapons.md) | | |
| `data/survival/wave_start.wav` | a wave's start | none (silent until the file is there) |
| `data/survival/wave_clear.wav` | a wave cleared | none (silent until the file is there) |

Any RIFF PCM WAV (8 / 16-bit, mono / stereo, any rate) works (`Audio::playExtra`); the self-test's line says which
source each one used (`file` / `stand-in` / `unused`).

The stand-ins play in both sound modes (with the arcade sound board they are mixed with the PC voices, as the pause's
cues). Without a file or the PC sound bank the SMG's shot / reload is the handgun's own (the board's or the PC's),
never silence. Fixed for v1.2.0 (relfix2, user 2026-11-03: "the SMG sound is missing"): every game start loads the
PC sound bank again (`Audio::init`), which dropped the stand-ins' samples while `playExtra` kept their old numbers, so
in every game after the one where the SMG was first heard (the user's log: the next Survival game from the menu) the
SMG was silent; `loadBank` now forgets them and they are made again. Test helpers: `HOTD_TRACE_SOUND=1` logs each SMG
shot's source, `HOTD_AUDIO_RESTART_AT=F` sets the sound up again at frame F (as a new game does),
`HOTD_SURVIVAL_SMG_SILENT=1` mutes the SMG's shots (a WAV of the same run without them: the difference is the SMG alone).

## Normal play

Every Survival hook does nothing while Survival Mode is off: `Game::evt_spawned` (an empty virtual), `Game::survivalRounds` /
`extraBodySlots` (0), `Game::survivalScoring` (false: `add_score` as always, no `survival_hits`; `survivalHitSeats` is
only written), the runtime's event task case and lead-in loop (`survival.active()`), `sound_cue`'s first look, the
input's gun (`survival.gunInput` only while active), the Auto Reload override (only while active), the view's
`actorAreaLook` (empty), `Game::seatsHeldOut` / `Runtime::livesKeptSkip` (0: the start check, the 4P start, the PRESS
START prompt and `--lives-kept` as before; `coop4_seat_start` is the 4P start button's own code moved out). Checked: the input test, Chapter 1 (arcade and PC
models) and Chapter 4 `--strict` give the same results as before (see the worker's STATUS.md).

## Open

- MrG (19 joints, 4 own motions), Lubin (16 joints) and Hiru (9 joints, 6 own motions) need behaviour of their own.
- Online co-op: Survival is local only (its state is not in the rollback snapshot; the menu's Survival starts a local game).
- The rounds' "speed": the creatures' own walk speeds are the motions' (not changed); faster rounds come from the
  runners (the dash motion), shorter gaps, the AI-at-once flag, the aggression floor and Sonic Mode.
- Not checked in a window: the shop's signs' place on a wide screen and with the PC HUD, buying with a pad / light gun.
- The tuning was done with bots (below); a human's round may differ: the numbers are in `roundCount` / `roundHealth` /
  the `k*` constants of survival.h.
