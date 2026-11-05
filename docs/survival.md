# Survival Mode (ours, 2026-11-03)

Endless rounds of creatures in an arena taken from a stage, the game's cut creatures brought back among them, played
like the old **Call of Duty Zombies** (World at War / Black Ops; survival2, the user's 2026-11-03 brief: the first
version was too easy): rounds that keep getting harder, points for hits and kills spent between the rounds (ammo, the
SMG, a life, two perks), reloads by hand, three lives. Code: `src/game/survival.h` / `survival.cpp`; the hooks listed
under "Normal play".

**Very Experimental / Proof of Concept** (user 2026-11-03): in the release packages too (CMake `HOTD_SURVIVAL_MODE`,
default ON), with that line on its More Modes item and its help line and a notice when a game starts. In a release
package (`HOTD_EXPERIMENTAL_MODES` OFF) only `--survival*` works: the old `--horde*` names are refused there (they are
the new Horde Mode's, which stays out) ([porting.md](porting.md#experimental-modes)).

Named **Horde Mode** until 2026-11-03 (the user: it is survival). The old names still work: `--horde <arena>`, `--horde-test`, `--horde-seed`, `--horde-smg-wave`,
`--horde-cap`, `--horde-no-cut` (= the `--survival*` ones), `HOTD_HORDE_*` (= `HOTD_SURVIVAL_*`), `data/horde/` (the
sound folder when there is no `data/survival/`); `horde_scores.txt` is moved to `survival_scores.txt` once.

## How to play / test

- Menu: **More Modes > Survival** (the players are asked first); **Survival Arena: <name>** below it picks the arena (click:
  the next one). Sonic Mode's preset (More Modes) applies to Survival as to every game.
- Command line: `hotd --survival <arena>` (`courtyard`, `bedroom`, `lab`, `furnace`, or 1-4), with `--players 1..4`.
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
  (n+1)-th creature wait; each wait is logged with the camera: how the arenas were found), `HOTD_SURVIVAL_NAME=X` (a new
  table row filed under that name, also headless).

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

**Spawn places:** the creatures the lead-in captured at that wait, plus those of earlier sections within 450 units of
the camera (the same room), at most 16; a place nearer than 110 units is moved out to 110 along its own direction (the
original's creatures there are scripted to step up close: born in the player's face they could not be shot in time);
with fewer than 4, each is also turned 30 degrees either way about the camera (more sides to come from). Each spawn
is spread +-20 units.

**Rounds (round r; Call of Duty Zombies' curve, `SurvivalMode::roundCount` / `roundHealth`):**

| | Rule |
|---|---|
| Creatures | 6, 8, 13, 18 (rounds 1-4), then `24 + 2(r - 5)` to round 9, from round 10 `24 x 0.15 x r`; + 25 % for each other player; at most 150 |
| At once | `min(cap, 1 + r)` in rounds 1-3 (2, 3, 4), then `2 + r`; the runners' round `4 + r` |
| Spawn gap | `max(12, 80 x 0.93^(r - 1)) + rand(20)` frames |
| Hit points | the type's own to round 3, then + 12 % a round to round 9 (x 1.72), then x 1.1 a round (CoD: + 100 to 9, then x 1.1) |
| Runners | none in rounds 1-3; from round 4 a share `min(8(r - 3), 60) %` start in their dash (start sub-state 2: the type's motion entry 2, `walk_variant` 1, so they dash again after each stop); `min(10(r - 3), 70) %` with the AI at once (flag bit 22) |
| Aggression | the game's own rank (difficulty_level's 0x51F492, 0-15: how often creatures attack) gets a floor of `(r - 1) x 2 / 3` |
| Mix | the arena chapter's own standard types, the early rounds from the weaker end of the list; from round 2 a cut creature with chance `min(5 + 2r, 25) %` |
| Special rounds | every 5th: 5, 15, 25 .. **runners** (every creature dashing, AI at once, the lighter half of the types, 3/4 the count, more at once); 10, 20 .. **brutes** (the heavier half and the cut creatures, half the count, x 1.25 hit points). Cleared: **Max Ammo** (every gun full, the SMG's reserve too) |
| Break | 8 s ("ROUND n CLEARED"; the shop is open, a countdown at the top); **no free life** |

**Lives:** at most **3** when a seat joins (fewer when the Lives setting is lower), up to 5 with bought ones. No
continue: when every player is out, game over (as Boss Mode); its results line ("REACHED ROUND n POINTS p KILLS k")
stays over the game over scene, then the menus: the name entry for a new row ("ROUND n p PTS"), then High Scores on
the **Survival** table (`survival_scores.txt` in the settings folder: points, round, arena, name; ten rows; shown as
"Round n" and the points).

**Points** (each seat its own; `Game::survivalScoring`: the game's own `add_score` does nothing in Survival, and
`enemy_part_hits` reports each frame a creature was hit, `Game::survival_hits`): **10** for each frame a seat's shots hit
a creature, **50** for the kill (the game's own killer), **100** for a kill with a head hit (the game's eye / head hit,
part class 1: `G::eye_hits`). The score in the table is the points earned (spending does not lower it).

**The shop (between rounds):** five signs in a row across the lower part of the picture; a shot at a sign buys it
(the shot is not fired): **AMMO 500** (the SMG's reserve full again: four magazines; the handgun never runs dry),
**SMG 1000** (the SMG, a full magazine and its reserve; lost when it and its reserve are empty: back to the handgun),
**LIFE 1500** (+1, at most 5), **QUICK RELOAD 2000** (reloads take half: 18 / 36 frames), **BIG MAG 2500** (magazines
x 1.5: 9 / 45). "NOT ENOUGH POINTS" / "NOT NOW" when it cannot be bought.

**Reload by hand:** Survival ignores the Auto Reload setting (`autoReloadSeats` / `autoReloadWanted` in main.cpp: off
while `survival.active()`, the setting itself untouched, every other game as set). A pull off the screen starts the
reload: the handgun's takes 36 frames, the SMG's 72 (from its reserve); the gun does nothing meanwhile ("RELOADING").

**HUD:** the round line at the top ("ROUND n LEFT k"; in the break "ROUND n IN s - SHOOT A SIGN TO BUY"), a line per
player ("1P 1230 PTS 6/6 QUICK BIG MAG", "SMG 23+120"), the shop's signs with their prices (lit when affordable) in
the break, the moment's banner in the middle (the arena's name, ROUND n - RUNNERS, 1P BOUGHT THE SMG, ROUND n CLEARED -
MAX AMMO). Drawn by `Hud::drawSurvival`.

## Arenas

Found with `HOTD_SURVIVAL_WAIT` (every creature wait of each chapter's lead-in logged with the camera) and pictures of
each (`--render --shot-at`); the ones where the camera stops in an open place with several ways in were kept.

| Key | Where | Start | Wait | Types (the chapter's kind 0 records) |
|---|---|---|---|---|
| courtyard | Chapter 1, the courtyard in front of the mansion | chapter 1 event 1 | 1st | 11, 28, 24, 16, 57, 29, 30 |
| bedroom | Chapter 2, the green bedroom | chapter 2 event 1 | 6th (event 4) | 11, 28, 16, 29, 51, 33, 45, 30, 32, 58 |
| lab | Chapter 3, the laboratory floor | chapter 3 event 1 | 1st | 28, 61, 86, 62, 73, 83, 3, 29, 53, 65, 72, 63, 84 |
| furnace | Chapter 3, the bridge over the furnace | chapter 3 event 1 | 7th (event 6) | as lab |

Dropped: Chapter 1's mansion steps (event 3's wait is inside a cut-scene: shots off, letterbox; the creatures could not
be shot), Chapter 2's waits 7 / 8 (the camera in a dark corner), Chapter 3's waits 3 / 8 (too dark / too close).

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

## The SMG

Bought in the shop (1000 points; `--survival-smg-wave N` gives it free from round N, testing), for each player on
their own: a full magazine at once, **30 rounds** a magazine (45 with Big Mag) and a reserve of four magazines
(`Game::survivalRounds[seat]`: the port's `pm_full_rounds`; the HUD's bullets show the first 6, the player's line the
count and reserve). The reload takes from the reserve; with the magazine and the reserve empty the player is back to
the handgun (buy it again, or AMMO before that). Holding the trigger on the screen fires every 5 frames (12 shots a
second, `SurvivalMode::gunInput`, called by `Runtime::input_task_stand_in`): the mouse's left button and the fire keys
report held; an input that cannot (pads, light guns: their pulls arrive as single presses) fires a burst of 3 a pull.
Damage per shot: the handgun's (the game's own shot path, `player_shoot`).

**Sounds** (`survivalSoundDir()`: the first data folder with `data/survival/`):

| File | Played | Stand-in until the file is there |
|---|---|---|
| `data/survival/smg_fire.wav` | each SMG shot (instead of the handgun's 0xA01208 / 0xA01207) | the handgun's shot (PC cue 30) 1.4 x faster, its first 40 %, 90 % volume |
| `data/survival/smg_reload.wav` | the reload with the SMG (instead of 0xA01200) | the reload (PC cue 28) at 0.8 x |
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
`actorAreaLook` (empty). Checked: the input test, Chapter 1 (arcade and PC
models) and Chapter 4 `--strict` give the same results as before (see the worker's STATUS.md).

## Open

- MrG (19 joints, 4 own motions), Lubin (16 joints) and Hiru (9 joints, 6 own motions) need behaviour of their own.
- Online co-op: Survival is local only (its state is not in the rollback snapshot; the menu's Survival starts a local game).
- The rounds' "speed": the creatures' own walk speeds are the motions' (not changed); faster rounds come from the
  runners (the dash motion), shorter gaps, the AI-at-once flag, the aggression floor and Sonic Mode.
- Not checked in a window: the shop's signs' place on a wide screen and with the PC HUD, buying with a pad / light gun.
- The tuning was done with bots (below); a human's round may differ: the numbers are in `roundCount` / `roundHealth` /
  the `k*` constants of survival.h.
