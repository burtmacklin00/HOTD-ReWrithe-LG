# 4-player co-op (experimental mode)

Status (2026-10-20): **phases 0-6 done** (sections 7-10, 12 and 13); phase 7 (online): section 11. ROADMAP: "Future milestone: 4-player co-op"
and its "User additions (2026-10-19)".

**Hard rule:** 4P is a separate mode. With 1 or 2 players (and in every proof) the game is the proven arcade / PC code,
byte for byte the same behaviour: no 4P branch may run, read or write anything unless the 4P mode is on.

## 1. Decisions

### Decided by the user

| Topic | Decision |
|---|---|
| Colours | P1 red, P2 blue (unchanged); **P3 green** crosshair + green fire lamps (HUD torches); **P4 yellow** crosshair + yellow fire lamps |
| Bodies | P3 / P4 = the alternative skins of Rogan and G that the games already have (below) |
| Cut-scenes | all four players where it works; elsewhere two sets (P1 / P2 one set, P3 / P4 the other) |
| Enemy targeting (DECIDED) | enemy attacks rotate over the players in play: the arcade's 2-player alternation widened to n players |
| Enemy counts (DECIDED) | as the arcade (no extra enemies) |
| Bosses (DECIDED, user confirmed 2026-10-19) | +50 % hit points per player beyond two (3 players x1.5, 4 players x2.0) as the starting point; 1-2 players unchanged. **Balance requirement (user): fair, not too hard, also for a team where one or two of the four are weak players.** Tune with bot games of mixed skill (e.g. two strong + two weak bots: low fire rate / poor aim) so mixed 3P / 4P teams win about as often as a good 2P team; lower the factor (e.g. x1.35 / x1.7) or count only players who actually hit the boss if mixed teams struggle. |
| Lives / continues (DECIDED) | each player has their own lives and continues; any player can join or leave at any time (like inserting a coin) |
| Score / hostages (DECIDED) | score per player; a hostage rescue goes to whoever shot the attacker (the arcade's own rule, `+0x2EB`) |
| HUD (DECIDED) | P1 / P2 keep their corners; P3 / P4 at the top corners (P3 top left, P4 top right) with green / yellow fire lamps and crosshairs |

### The alternative skins (found 2026-10-19)

Both games carry them, so no recolouring of textures is needed (the ROADMAP's first idea is dropped):

- **PC** `THOTD.EXE`, PC Mode character table 0x5CE348 (0x34 bytes each; +0x10 creature type, +0x14 an id not yet
  identified, +0x18 / +0x1C the big / small portrait sprites of the character select, +0x20 hit points, +0x24 rounds,
  +0x28 damage):

  | # | Name | Creature type | +0x14 | Portraits | HP / rounds / damage |
  |--:|---|--:|--:|---|---|
  | 0 | NULL PLAYER | 2 | 0x188 | 0xEF / 0xF5 | 5 / 8 / 1.0 |
  | 1 | Rogan | 2 (BO_tom) | 0x188 | 0xEF / 0xF5 | 3 / 6 / 1.0 |
  | 2 | G | 13 (BO_gman_kihon) | 0x189 | 0xED / 0xF3 | 3 / 2 / 4.0 |
  | **3** | **Rogan 2** | **14 (BO_haride)** | 0x1D38 | 0xF0 / 0xF6 | 2 / 12 / 1.0 |
  | **4** | **G 2** | **15 (BO_staje)** | 0x1D39 | 0xEE / 0xF4 | 5 / 6 / 0.5 |
  | 5 | Sophie | 34 | 0x1D3A | 0xF1 / 0xF7 | 4 / 12 / 0.5 |
  | 6 | Researcher | 26 | 0x1D3B | 0xF2 / 0xF8 | 2 / 10 / 1.5 |

  The player's character per player is 0x5CE4E4[p] (PC Mode) / 0x5CE4EC[p]; `player_char_models_set` (PC 0x50C114)
  takes the type through 0x5CE358 (= table +0x10).
- **Arcade** `maincpu` 0xCA690, the title-code pairs (`player_char_models_set` 0x95530, index 0x51F42C):
  (2,13) (14,13) (2,15) **(14,15)** (2,34) (34,13) (34,34) (2,13): the title codes already put types 14 / 15 into the
  players' cut-scene roles, so both skins run with the players' motions on the same human skeleton.

So: **P3 = creature type 14 ("Rogan 2"), P4 = creature type 15 ("G 2")**, arcade models from the arcade set, PC models
from the PC's files, same as the others. (docs/names.md listed 14 / 15 as "human, not identified": they are these.)
Option (off by default): P3 / P4 use the PC Mode stats of Rogan 2 / G 2 (rounds 12 / 6 etc.) in the PC-Mode variant of 4P.

## 2. Inventory: every 2-player assumption

The port works on the original memory image (`mem`, the arcade addresses) and indexes the player records as
`G::players + p * 0xCC`. The records of P1 / P2 (0x51F284..0x51F41C) are followed directly by other globals
(`frame_counter` 0x51F428, `char_pair` 0x51F42C, `body_types` 0x51F438, `player_bodies` 0x51F440, `players_in_game`
0x51F4A0): **P3 / P4 cannot live in place**. Every per-player array below has the same problem.

Counts in src/ (2026-10-19): `* 0xCC` player indexing about 60 sites in 23 files; `players_in_game(_b)` about 140 uses
in 32 files; `p < 2` loops 52 in 25 files; `player_targeted` 10; the hit mask `& 6` in 13 files; `rand() % 2` /
`rand() & 1` player picks about 6.

| Area | Where (file / routine) | 2-player form | For 4P |
|---|---|---|---|
| Player records | types.h `Player` (0xCC), `G::players` 0x51F284; player.cpp, player_join.cpp (`PlStartInit`), player_start.cpp, player_support.cpp, game_flow.cpp (rows 0x51F280 + p x 0xCC), game_over.cpp, body_hooks.cpp, blow_off.cpp (per-player camera 0x51F340), t1_bounds.cpp, enemy_support.cpp | 2 records, raw address math | `player_rec(p)`: p < 2 the original address, p >= 2 an extension block (below) |
| Player counts | `players_in_game` 0x51F4A0, `players_in_game_b` 0x51F4A4, last counts 0x51F4A2 / 0x51F4A6; written by player_join.cpp (join), player.cpp (out), game_flow.cpp, chapter.cpp, attract.cpp | 0 / 1 / 2, used as a table index ([count][player] tables) and in `== 2` / `== 1` tests | the real count 1..4 in a 4P-only field; the original fields see `min(n, 2)` (keeps every table in range and the arcade curve); `== 2` tests read "two or more" in the 4P build |
| Solo player | `solo_player_is_p2` 0x51F700; camera.cpp (mode 4 gun pan), boss_chariot / hermit / hangedman, ch3_kinds, enemy_dogs | which of two is alone | "the one in play" = lowest seat in play (0..3) |
| Gun / input | runtime.h `GunInput gun[2]`, `shotsIn[2]` / `reloadsIn[2]` / `startsIn[2]` / `startRequest[2]`; `G::gun_inputs` 0x51F574 (2 x 0x1C); `--input-test` record; bot.h `profile[2]` / `pl_[2]` | 2 guns | 4 guns in the runtime; the extension block holds GunInput 2 / 3; the input record gets a 4-player version (netplay's frame is designed for 4 already) |
| Shots | shooting.cpp `shot_resolve` (`for p < 2`, P1 before P2), `shot_hit_nearest` (`bit = 1 << (p + 1)`), `G::shot_results` [2] | 2 passes, hit bits 1 / 2 | 4 passes in seat order; results [4] in the extension |
| Hit bits on objects / parts | types.h `Obj.flags` +0x58 and `BodyPart.flags` +0x3C bits 1 / 2; read as `& 6` in creature.cpp, damage.cpp, hostages.cpp, props*.cpp, prop_groups.cpp, prop_kinds.cpp, enemy_actions.cpp, enemy_agile.cpp, ch3_kinds.cpp, boss_chariot.cpp | 2 bits | P3 / P4 need two bits more. Bits 3-7 are unverified in both words: first prove them free (grep + a capture of every write), else a per-object side mask in the extension; the 4P build tests `P::hit_mask` (0x1E) |
| Kill / rescue credit | `Obj.killer` +0x2EB (int8, -1 none); combat_support.cpp `kill_score_award(_shooter)`, ch2 / ch3 kinds `add_score(+0x2EB)`, hostages*.cpp | 0 / 1 | int8 holds 0..3: works once `add_score` uses `player_rec` |
| Hostage shot | hostages.cpp `hostage_shot` (0x2EB = shooter; both bits set: `rand() % 2` picks who loses the life) | 2 shooters | random among the shooters whose bit is set |
| Targets | `player_targeted` 0x5209A0 (1 byte a player); combat_support.cpp `pick_target_player(_any)` (random when both), `pick_target_player_rand` (`rand() & 1`); enemy.cpp, enemy_ai.cpp (`freePlayer`, `setTarget`), enemy_agile.cpp, boss_hangedman.cpp, ch2_kinds.cpp, camera.cpp (clears both) | 2 bytes, 50 / 50 | 4 bytes (extension); the decided rotation: a 4P turn counter picks the next free player in play |
| Attack slots | attack_slots.cpp `attack_slot_front_take`, `enemy_attack_slot_take` (`G::attack_slots` 0x51F9F0: front 0 / 1, waiting 2 / 3; `attack_side` 0x51FE30), enemy_ai.cpp (`slot_wanted`), `Enemy.players_seen` +0x334 | front slot = player | keep the 4 slots (screen places, not players) and the 2-player rules (count clamped to 2); the attack's *target* comes from the rotation |
| Dogs and packs | enemy_dogs.cpp (target = dog index % players: rec +0x24 % n), kind 70 `kind70_attack_turn` 0x5207B4, `bats_attacking` | % 2 alternation | % n over the players in play (the decided rotation) |
| Dynamic difficulty | enemy_helpers.cpp `difficulty_level` 0x264F0: +4 per player joined, +1 per 3000 frames, + hits, 0..15 | count 0..2 | sees `min(n, 2)` (decided: enemy counts as the arcade) |
| Bosses | boss_hangedman.cpp `boss_aggression_init / _update` (P1 + P2 lives total), boss_magician.cpp (7 `players_in_game` tests), boss_hermit.cpp, boss_chariot.cpp (`rand() & 1` target), life bars (`life_bar_init`, kLifeBar 0x543530) | 2 players | lives total over 4; tests "two or more"; hit points x1.5 / x2.0 at the fight's start (late joiners add their 50 % to the remaining hit points) |
| Lives / continues | player.cpp, game_flow.cpp (`game_continue_*`, continue rows, 0x9FFF countdown at +0xA0), game_over.cpp (`player_st4_out_of_lives`, state 6 both / one), `Player.continue_save` | 2 | per seat; game over when no seat is in play and no countdown runs |
| HUD | hud_state.cpp `hud_lives_draw` (torches 0x51F5C0 + p x 10), popups 0x51F480, reload prompt (+0x40), `hud_player_*` in sprites.cpp (display2 per-player cells, 4:3 corners); hud.cpp (`for p < 2` sights, `cross_[2]`, `crossTex_[2]`, `sightTex_[3][2]`, colours red / blue), hud.h `drawCross[2]` | 2 corners | P3 / P4 cells at the top corners, lamps tinted green / yellow, sights green / yellow; torches / popups for seats 2-3 in the extension |
| Results | chapter_clear.cpp (kind 176: `for p < 2` scores 0x51F28C, lives bonus 0x51F290) | 2 columns | 4 rows (or 2 x 2) |
| Ranking / name entry | name_entry.cpp (`name_entry_players` 0x520AF0, two halves), `ranking_start / _run`, backup_ranking_* (EEPROM image) | 2 entrants | two rounds (P1 / P2, then P3 / P4) or four quadrants; 4P scores in a separate 4P table (the arcade table untouched) |
| Cut-scene bodies | player_body.cpp `player_char_models_set` (2 bodies, slots 14 / 15 in `player_bodies` 0x51F440, types by `char_pair`), all scene routines (tables [players_in_game][player] x 0xC, "player 2 of two stands aside" branches), body_hooks.cpp, `Player.body` +0xBC / `scene_routine` +0xB0 | 2 bodies | P3 / P4 bodies in the extension (the slot table is packed among other globals); see section 3 |
| Camera | camera.cpp (mode 4: the solo player's gun pans the view; shake on a hit: `shake_timer`, `Player.hit`), body_hooks.cpp (hit -> shake per player) | one view, 2 shakers | one view; any player's hit shakes it (as now) |
| Ending / attract / demo | ending.cpp, attract.cpp (2 bodies, the demo streams in `Player.demo_*`) | 2 | unchanged (attract stays 2P); the ending: section 3 |

## 3. Cut-scenes

The player cut-scenes (player_body.cpp) all follow one pattern: the lead body (P1's slot) does the action, "player 2 of
two" stands aside / reloads / waits at its own table place; one player alone gets the lead. Bodies are placed from
[players_in_game][player] tables relative to the scene camera.

| Scene (routine) | Action | Four? |
|---|---|---|
| 00 kousya run (0x4BAB0..) | the school-yard opening run | likely: P3 / P4 run behind P1 / P2 with an offset |
| 02 walk and shoot (0x4C0D0..0x4C320) | lead walks and fires three shots, P2 aside | likely: P3 / P4 stand aside further out |
| 03 ladder (0x4C3A0) | Rogan climbs, P2 looks up | tight framing: two sets |
| 04 fall backside (0x4C550) | lead sits down hard | two sets |
| 05 / 13 knocked flying (0x4C700, 0x4D710) | both fly back (P1 0x156, P2 0xBD) | two sets (close camera) |
| 06 reload shoot / 12 reload kick (0x4C980, 0x4D5C0) | one shoots, one reloads | likely: P3 reloads, P4 stands |
| 07 / 10 / 11 jump down a / b / c (0x4CC00, 0x4D2F0, 0x4D450) | lead jumps down, P2 waits | maybe (P3 / P4 wait beside P2) |
| 09 stance shoot (0x4D0B0) | stance and three shots | likely |
| 14 rattle door (0x4D870) | rattle a door | two sets |
| Ending (kinds 18 / 19, 21 / 22, lip-sync heads) | the two players' characters talk | two sets |

"Likely" means: free room in the camera's frame; each must be checked by eye before it is switched on (a per-scene
table `coop4_scene_mode[scene] = four / sets`, default "sets").

**Four bodies:** P3 / P4 get extra bodies (types 14 / 15) driven by the P2 "aside" routines with their own place
table (new 4P data, not the original's tables), so the original routines keep driving P1 / P2 unchanged.

**Two sets (fallback):** the simulation runs the scene exactly as in 2P (two bodies, the original routines); only the
drawn models change. Set A draws types 2 / 13 (or the title code's pair), set B draws 14 / 15 in the same roles, a
render-time model swap (no simulation change: the cut-scene bodies are never shot, and the swap is on the draw side,
so lockstep stays safe).
- One screen (local): the set of the "lead pair": set A if P1 or P2 is in play, else set B; option "alternate"
  (each cut-scene the other set) or "always A".
- Online: every PC draws its own set (players 3 / 4 see their own characters in the roles): per-PC rendering of the
  same simulation, so it costs nothing in netplay.

## 4. Gameplay rules

Decided defaults are in section 1. Alternatives kept as options (menu "4P rules", off by default):

1. **Targeting:** rotation (DECIDED) / random among free players (the arcade's 2P random widened) / nearest-screen
   side (P1 / P3 left, P2 / P4 right, matching the attack slots' sides).
2. **Difficulty:** arcade counts and the arcade curve with the count clamped to 2 (DECIDED) / "rising": the dynamic
   level +2 per player beyond two (capped at 15) / more enemies per wave (needs new spawn data: not planned).
3. **Bosses:** +50 % per player beyond two (DECIDED) / +25 % / none.
4. **Lives:** own lives and continues, join / leave any time (DECIDED) / shared pool (the team's lives = 2 x the
   setting, any player's hit takes one) / no continues ("hardcore").
5. **Score / hostages:** per player, rescuer = who shot the captor (DECIDED) / team score as well on the results screen.
6. **Friendly fire:** none (as the arcade). A hostage shot costs only the shooter's life (as the arcade).
7. **HUD:** P3 / P4 top corners (DECIDED). 4:3: the top corners must clear the boss life bar and the chapter title
   cards; fallback: P3 / P4 stacked above P1 / P2 in the bottom corners. 16:9: the side bars hold P3 / P4 outside the
   4:3 picture (option).
8. **Join / leave:** a player joins with Start at any time (as the arcade's coin join: `player_st3_join`); a player
   who leaves (option: a "leave" key, or a disconnected netplay peer) goes to the game-over state; the seat is free
   again.

## 5. Architecture

- **Extension block:** P3 / P4 data (two `Player` records, two `GunInput`, two `ShotResult`, targeted bytes 2 / 3,
  torches, popups, the two extra bodies, the real player count, the target rotation counter) in a reserved area of
  `mem` that the original never uses (checked: nothing reads or writes it in 2P), so the port's existing `mem.at<>`
  code works on it.
- **A 4-seat build of the logic:** a third layout trait next to A / Pc (`A4`: the arcade layout, `P::players = 4`,
  `P::hit_mask = 0x1E`), so `Game<A4>` is a separate instantiation used only by the 4P mode. In the shared routines the
  changes are `if constexpr (P::players == 4)` blocks or `P::players` loop bounds and the `player_rec(p)` accessor,
  which compile to the same code for `Game<A>`. Every proof (`hotd_proof` capture sets) is rerun on `Game<A>` after
  each change to show 2P is unchanged.
- **Runtime:** 4 guns / sights / HUD cells, 4-player input record, `--players 4` (until the logic exists: refuse
  with a clear message).
- **Netplay:** lockstep with 4 input slots (the netplay worker's frame format); the cut-scene set choice is per PC
  (render only).
- The PC logic (`Game<Pc>`) gets no 4P form; 4P runs the arcade logic with either model source.

## 6. Plan (sizes in sessions)

| Phase | What | Touches proven code? | Size |
|---|---|---|---|
| 0 | Groundwork: P3 / P4 colours in hud.cpp (sights, tinted lamps), types 14 / 15 loadable as player models behind a flag, `--players 4` stub that refuses | no (runtime only) | 0.5 |
| 1 | Extension block + `player_rec(p)` + the `A4` trait instantiated; `* 0xCC` sites moved to the accessor; proofs rerun (0 differ) | yes, behaviour-neutral for A | 1-2 |
| 2 | Shots and hit bits for 4 (prove bits 3-7 free or side mask), kill credit, hostage shot | yes, behind `P::players` | 1 |
| 3 | Counts (real n vs clamped 2), targeting rotation, dogs / kind 70 / bats, bosses (life scaling, lives totals, magician / hermit / chariot tests) | yes, behind `P::players` | 2 |
| 4 | Lives / continues / join / leave per seat, game over, results, 4P ranking and name entry | yes, behind `P::players` | 1-2 |
| 5 | HUD layout (4:3 / 16:9), 4 guns in the runtime, 4-player input record, local play with 4 pads / guns | runtime | 1 |
| 6 | Cut-scenes: two-set render swap (all scenes), then four bodies where the framing allows | 4P data + runtime | 1-2 |
| 7 | Online 4 (with the netplay worker), soak tests | runtime / net | 1 |

Total about 8-11 sessions. Checks for every phase that touches shared routines: `hotd --input-test` passes, Chapter 1
ends at 16695 (both model sources), the proof capture sets 0 differ.

## 7. Done (phases 0 and 1, 2026-10-20) and what changed in the plan

### Using it

`hotd --players 3|4` (or More Options / the launcher: "Players 3 / 4 (experimental)"); the 4P mode is on for 3 or 4
players (players 3 / 4 can also join later with their start button: `--start-at 3:F`). Local only: an online game keeps
two players (netplay's frame carries four slots already; the runtime turns 4P off while `gNet` is set). Players 3 / 4's
input: More Options "Player 3 / 4 Input (4P)" = Gamepad (default; light bar green / yellow, `pads.cpp kColour`),
Light Gun, or a mouse of their own (both through raw input, as the light guns), launcher.ini `p3input` / `p4input`
(0 mouse, 2 pad, 3 gun). The keyboard stays player 2's. Pad profiles / gun calibrations of players 3 / 4: the defaults
(not saved yet).

### The reserved memory area: work RAM 0x50C000..0x50FFFF (port/coop4.h)

The top half of the arcade's **main stack**: the boot code sets `fp = 0x500400` (0x6DC / 0x740) and the i960 stack
grows upward to the interrupt stack at 0x510400 (PRCB 0xB0 word +0x18). Why nothing of the original touches it:
- no 4-aligned word of the program ROM is an address in 0x508560..0x5103FF (every hit of a scan is an odd-aligned piece
  of an instruction), so no routine addresses it directly;
- it is zero in **all 3,591** MAME work RAM dumps in private\ (fullgame_us / fullgame_bufram every 0.4 s of a whole
  game, the Chapter 3 runs, the hangedman fight, scans, boot, attract); the stack is never cleared, so any frame that
  ever went there would have left words: the deepest word seen is 0x50855F (boot);
- the boot-to-Chapter-1 write map (private\boot_writers\writers.txt, every write tapped) has no write in
  0x508580..0x5103FF;
- the port never uses the original stack (locals at 0x700000), and the runtime's work RAM starts from a MAME image.
So 16 KB stay free above the deepest frame and 0x400 below the interrupt stack. Layout: magic "C4P4", the real player
count, seats 2 / 3's `Player` records (+0x104, laid out as the originals' pair, so `record - 4` works), GunInput,
gun_shot, invulnerability, targeted bytes, start switch words, torches, popups, part tallies, ShotResult / ShotRay,
the hit table. Netplay's rollback snapshots the whole work RAM, so the block is saved with it.

### Architecture as built (differs from section 5)

- **No separate `Game<A4>` instantiation.** The runtime is `class Runtime : RuntimeBounds : port::Game<A>` with about
  200 members in 20 files and the per-kind dispatch registries of `Game<A>` member pointers; a second instantiation
  would need all of it templated. Instead: the trait **`Seats<P>::max`** (port/coop4.h: 4 for A, 2 for Pc) compiles the
  seat-3 / 4 code for the arcade logic only, and the runtime switch **`Game::seats4`** (0 in every 1-2 player game and
  in every proof: the harness never sets it) turns it on. `coop4_on()`, `seat_count()` (2 / 4), `player_rec(p)`
  (seats 0 / 1: `G::players + p x 0xCC`, 2 / 3: the block) and `seat_at(base, ext, stride, p)` (`gun_input_rec`,
  `gun_shot_at`, `shot_ray_rec`, `shot_result_rec`, `part_tally_at`) give the same addresses as before for p < 2.
- **Hit bits 3-7 are not free:** bit 4 (0x10) of a shot candidate's flags is "scene item" and the candidates copy it
  from the object's own +0x58 flags (shot_test_object), so an object carrying bit 4 would turn later shots at it into
  scene-item hits. So seats 2 / 3 set **player 1's / 2's bit** (every `& 6` test sees their hits unchanged) and a
  **per-frame hit table** in the block (`coop4_hit_add / _seats`: target object or part -> seats) names the real
  shooters: `part_hit` loops over the table's seats (damage per seat, tallies of seats 2 / 3 in the block),
  `enemy_part_hits` credits the seat whose own damage kills (else a random one of those that did damage).
- Seats 2 / 3 run their own task (routine word `kSeatRoutine` 0x7FC04000, made after both players' by
  InitSetPlayerTask): `coop4_seat_frame` = the gun (`player_shoot` on the seat's records), lives -> state 4, the
  invulnerability countdown, the start button (join / continue, free play). No camera drive, no cut-scene action.
- Counts: `players_in_game(_b)` see min(n, 2) (the arcade's tables and curve); the real count is the block's
  `kPlayersReal`; the game goes to its continue only when all four seats are out.
- The runtime: `gun[4]`, input counters [4], `startRequest[4]`, the live input stand-in over `seat_count()`, the
  autofire takes turns over the seats in play, the headless summary prints each seat ("4P seats: ...").
- Colours (user rule: recoloured in code from the players' own art, never edited art): `game/player_colours.h` and the
  shared `util/recolour.h` (also the texture packs'): every crosshair style is built for players 1 / 2 and tinted
  for 3 / 4 (P3 from P1's art, P4 from P2's); players 3 / 4's HUD (`Hud::drawSeatHud`): their rounds and lives (torch
  base + flickering fire lamp, the arcade's own sprite numbers and phases) at the top corners, composed from the
  banks the HUD loaded (arcade: a private tile layer through `Arcade2D::compose`; PC: the matching PC sprites), lamps
  tinted, rounds as they are; widescreen: at the window's edges like players 1 / 2.
- Bodies: both sources draw types 14 / 15 in the players' cut-scene roles (checked with `HOTD_CHAR_PAIR=3`, arcade
  and PC renders of the opening). `coop4_body_set` (each chapter load): when neither player 1 nor 2 is in play but
  3 / 4 are, the cut-scenes use set B (title code pair 3 = types 14 / 15); set A again when 1 or 2 is back.

### Results

`--players 4 --autofire --lives-kept 1`: Chapter 1 ends at 17656 (arcade logic; 2P / 1P: 16695 unchanged), Chapter 2
at 39057, Chapter 3 at 58944, Chapter 4 at 81000, the ending over at 88366 (both model sources: Chapter 1 at 17656);
seats 3 / 4 fired 275 / 276 shots in Chapter 1 and scored 1600 / 1680 (kills credited through the hit table).
`--players 3 --start-at 4:3000`: player 4 joins, Chapter 1 ends at 16230. Renders (arcade and PC models): green /
yellow lamps and rounds at the top corners, yellow / green sights.

### Not yet after phase 1 (see section 8)

Phases 2-4 are in section 8.

## 8. Phases 2, 3 and 4 (2026-10-20, second session)

### Phase 3: the enemies attack all four

- **Rotation (decided):** an enemy still aims at side 0 / 1 exactly as the arcade does (its target pick, attack
  slots, motions, the dogs' / kind 70's alternation, the bosses' `rand() & 1`, a projectile's screen half). Only the
  hit itself moves: `player_hit` and `projectile_hit_player` give it to `coop4_victim(side)`, the next seat in a
  rotation (block `kTargetTurn`) that is in play and not invulnerable (none: the side asked for). So every attack of
  the arcade's 2-player alternation lands on the four in turn, and nothing of the enemies' own logic changes.
- Seats 2 / 3 take damage and lose lives like players 1 / 2: `player_hit_check / _take / _keep_life`,
  `player_life_add`, the invulnerability (block `kInvulnerable`), the screen shake (`hit_screen_shake` over the four).
- **Sides:** `player_in_play(0 / 1)` = seat 0 or 2 / seat 1 or 3 in play; each frame of play `players_in_game` = the
  sides in play and `solo_player_is_p2` = the empty side, so the enemies keep attacking while any of the four plays
  (with players 1 and 2 out, 3 / 4 still fight).

### Bosses: hit points for 3 / 4 players, and the balance runs

`coop4_boss_scale` (just before each fight's `life_bar_init`: the Chariot's +0x70, the Hanged Man's and the
Magician's +0x70 / +0x72, the Hermit's +0x70 / block +0xA) multiplies the hit points and their maximum by
1 + step x (players - 2); `coop4_boss_frame` adds base x step per player who joins during the fight (life bar and
its maximum too). 1-2 players: unchanged. `--4p-boss-step S` sets the step.

Tuning runs (`hotd --chapter C --event E --bot SEED --bot-gameovers 1 --frames 15000`, `--bot-skill`: a strong bot
0.85, a weak one 0.3 = slow fire (gap 26 frames), aim error 4 + 0.7 x 40, more shots at nothing; win = the chapter
ended before the team's game over; Chariot 1 / 19, Hanged Man 2 / 15, Hermit 3 / 7, Magician 4 / 4):

| Team | Chariot | Hanged Man | Hermit | Magician |
|---|---|---|---|---|
| good 2P (0.85, 0.85) | 8 / 8 | 8 / 8 | 8 / 8 | 32 / 44 (73 %) |
| mixed 2P (0.85, 0.3) | | | | 3 / 12 (25 %) |
| mixed 4P (0.85, 0.85, 0.3, 0.3), step 0.5 (x2.0) | 8 / 8 (frames +16 %) | 8 / 8 (+7 %) | 8 / 8 (+30 %) | 24 / 44 (55 %) |
| mixed 4P, step 0.35 (x1.7) | 8 / 8 | 8 / 8 | 8 / 8 | 24 / 44 (55 %) |
| mixed 4P, step 0.25 (x1.5) | | | | 25 / 36 (69 %) |
| mixed 4P, step 0 (x1.0) | | | | 8 / 12 (67 %) |
| strong 4P (all 0.85), step 0.5 | 8 / 8 | 8 / 8 | 8 / 8 | 6 / 8 |

The first three bosses are won by every team at every factor (the fights only last longer). The Magician decides
it: with x2.0 / x1.7 a mixed four wins clearly less often than a good pair (55 % vs 73 %); with **x1.5 for four
players (step 0.25: x1.25 for three)** they are level (69 %), and going lower gains nothing (x1.0: 67 %). So the
default is **step 0.25** (3 players x1.25, 4 players x1.5) instead of the starting x1.5 / x2.0. The runs' outputs:
scratchpad `coop4b\bal\`. Worth repeating with more seeds and with real players.

### Phase 2: credit for players 3 / 4

Through the hit table (`coop4_shooter(target, proxy)`: the side's seat that alone hit it): hostage shots
(`hostage_shot_penalty` / the Chapter 1 kinds' copy: the real shooter loses the life), rescues and gifts (seats 2 / 3
as rescuers, `hostage_gift_task` / `_fly` for 3 / 4), item gifts, `kill_score_shooter` (one of the real shooters in
the game), `enemy_kill_score` (killer 2 / 3 = seat 3 / 4, not the arcade's "either"), the scripted enemies' 80
points, the scream penalty for all four, eye-hit points by seat (`kEyeSeats`).

### Phase 4: continue, game over, results, ranking

- Players 3 / 4 out of lives: their own CONTINUE? countdown (9 .. 0, -0x2D a frame, a trigger pull skips a digit,
  paused during the team's countdown), start = continue (score + 1, the lives setting's lives), at 0 their game over
  (2 s, then waiting: start joins again). The team's continue / game over comes when all four are out; one of
  3 / 4 continuing in the team's countdown ends it too. HUD: CONTINUE? and the digit at their corner (the arcade's
  sprites when the bank is loaded, else the line in their colour; PC: its sprites, tinted).
- Results (kind 176): players 3 / 4's SCORE and score at the top corners; the life bonus for all four.
- Ranking: a 4P top 10 of its own (`<ranking file>4p`, from the ROM's defaults; the arcade's table is untouched) and
  two rounds of the original name entry: players 1 / 2, then players 3 / 4 in their places (their records swapped
  for the round). Checked with `--chapter 4 --name-entry --name-keys ABC#` and a 4P table of zero scores: both rounds
  run, the 4P file holds the four names, the arcade's is not written.

`--bot` plays all four seats in the 4P mode (players 3 / 4's habits from their own generator, so 1-2 player bot runs
are unchanged); `--bot-skill a,b,c,d` sets each player's skill.

Results: the whole game with `--players 4 --autofire --lives-kept 1`: Chapters 1-4 end at 17785 / 39736 / 64434 /
92642, the ending at 100008. Without kept lives the four lose lives in turn, continue and run out as the arcade's two.

### Not yet

- **Phase 5:** players 3 / 4's RELOAD prompt and hit-notice pop-ups; the top HUD vs the boss life bar / title cards;
  the keyboard for 3 / 4; saved pad profiles / gun calibrations for 3 / 4; a 4-player input record; showing the 4P
  ranking (the ranking screen shows the arcade's table).
- **Phase 2 leftovers:** the Chapter 3 kinds that pay by the `& 6` bits directly (ch3_kinds 0x843 / 0x1149) and the
  bosses' own hit tests still credit player 1 / 2 for a proxy hit; a hit by player 1 and player 3 on the same
  object in one frame is one hit for the `& 6` readers.
- **Phase 6:** four bodies in the scenes that have room; the per-PC set choice online.
- **Phase 7:** online 4: two PCs with two seats each done (section 11); four PCs open.

## 9. Phase 5, the play-test fixes and computer teammates (2026-10-20, third session)

- **Lamps / labels / sights (user: "proper shades"):** players 3 / 4's fire lamps are player 2's flame lamp (its blue
  flame separates cleanly from the brass) through hand-tuned colour ramps (`Recolour::Ramp`, player_colours.h
  `kPlayerRamp`: shadow, deep, mid, hot, white-hot core; green for P3, amber / yellow for P4), the flicker kept; their
  "3P" / "4P" labels are the countdown's digit + player 2's "P", ramped; the sights the same ramps. The 4P HUD draws
  through a private 2D layer straight from the sheets (no game banks or palettes touched). Before / after renders:
  main checkout `private\coop4\lamps\`.
- **HUD placement:** the P3 / P4 band moves down past anything the game drew there (boss life bar, title cards,
  prompts). An empty seat shows PRESS START BUTTON at its corner; HOLD YOUR FIRE! in cut-scenes (scene_state 4) over
  the top bar; CONTINUE? and its digit in their colour; hit notices (the arcade shows them for shaking hits only).
- **Shot spark / marks:** players 3 / 4's shot spark (tamahi), MISS SHOT mark and extra-life gift through their ramp
  in the renderer (`DrawItem::ramp`, both model sources). Gun sounds: P3 sends player 1's command / cue, P4 player 2's
  (checked by the cue counts).
- **Settings:** players 3 / 4 follow the lives setting (checked 0 / 5 / 11), join / continue with PlStartInit's
  invulnerability; their pad profiles and gun calibrations are saved (pad3. / pad4., gun3cal / gun4cal) with Controller
  pages; High Scores' table name toggles the 4P top 10.
- **Credit leftovers:** the Chapter 3 rams and the Chariot's hand by the real shooter (players 1 + 3 in one frame:
  two hand hits).
- **Input test:** a 4-seat section (frames 601-625: players 3 / 4's virtual pads assigned, light bars green / yellow,
  Start joins, triggers fire, a left trigger reloads): 911 checks (866 + 5 + the per-frame check over 40 more frames).
- **Computer teammates (user request):** player 2 (any game) and players 3 / 4 (4P) can be CPU Easy / Normal / Hard
  (More Options, launcher.ini `p2bot` .. `p4bot`, `--seat-bot P:L`); online since 2026-10-20 through the lockstep
  (docs/netplay.md "Input determinism"; seeded from the seats / levels, no longer the clock). The bot (game/bot.h `teammates`)
  plays only its seats with its own generator: joins at once, always continues, holds its fire in cut-scenes, leaves
  hostages alone (Easy rarely not), takes the nearest threat first (Normal / Hard), Normal fires a little slower so the
  human keeps kills; its crosshair in its seat colour with a CPU tag. Headless: with player 1 idle, three CPU partners
  clear Chapter 1 (17074); one CPU player 2 alone clears it (22763). (Replaced by the play below.)
- **Smarter CPU teammates (worker "cpusmart", 2026-10-20; user: "they shoot all the time and at everything, even at
  creatures in the distance").** game/bot.cpp `teamPlay` (the testing bot `--bot` is unchanged):
  - *Targets by threat* (`threat`, recomputed every frame): enemies close by / in an attack position (+0x2E7) / coming
    closer first (about 1000 - 3.5 x depth, +150 engaged, +100 approaching, +300 within 35 units), thrown things next
    (`flying_axe_frame`, `held_object_flight`, fireballs, the Magician's barrage 0x74150, knives, spit: 950 - 3 x depth,
    1250 within 40), bosses 700. Left alone: enemies deeper than 260, or deeper than 130 and not yet engaged; bodies
    with their hit points used up (for 2.5 s: kind 70 fights on at -30 hit points, so one still there later counts
    again); routines named death / dead / dying; scenery (props, padlocks, signs, items, kind 146's billiard balls, a
    carried barrel); creatures while the camera moves (`camera_stopped` != 2: `enemy_part_hits` ignores those shots,
    and enemies attack only at a stop) are aimed at but not shot. The current target keeps a +200 bonus (no flicking).
  - *Hostages / researchers* (names hostage* / researcher*, kind 64, kind 17 researchers): never targets; no shot while
    the crosshair is within 12 units (+ the hand's shake) of any of their hit spheres (`sphereGap`: the body parts'
    spheres +0x5C / +0x74 as the shot test sees them).
  - *Aim and fire like a person* (`kTeamLevel`; reworked in section 21): the crosshair moves `ease` of the way each frame, at most `speed`
    units; a new target gets a reaction delay (less when it is near the crosshair) and an aim error that shrinks with
    each shot; it fires only once the crosshair is on the aim point, in bursts with gaps, then a pause. Easy / Normal /
    Hard: reaction 24-36 / 15-23 / 9-14 frames, speed 7 / 11 / 16 units a frame, error 13 / 7 / 4, bursts 2-3 / 2-4 / 3-5
    shots 10-14 / 8-10 / 6-8 frames apart, pauses 30-50 / 14-26 / 8-16. With nothing to shoot its sight drifts to a rest
    point on its side of the screen.
  - *Reload* off screen (as at the arcade), 18-36 / 10-18 / 5-10 frames after the last round, or with rounds gone after
    70 / 45 / 28 frames without a threat; never in a cut-scene or while shots are off (an off-screen pull is a skip
    there).
  - *Spread*: with two or more threats a target another CPU partner holds counts 400 less, the one nearest the human's
    sight (fired in the last 45 frames, within 50 units) 450 less. 4P Chapter 1: frames on a partner's target 58 % with
    the spread, 99 % without (`HOTD_BOT_NO_SPREAD=1`), and the chapter cleared ~1350 frames sooner.
  - *Bosses*: the Magician's weak parts (4 / 6 / 11 / 13, `aimPoint`), the one nearest the crosshair; the Chariot's
    hand while the armour is on (`isDecoy`).
  - *Determinism*: only the game state and the bot's seeded generator; local runs repeat (checked twice, same output);
    online the seat's input goes through the lockstep as before.
  - *Measuring*: every headless run with `--bot` or CPU seats prints `TEAM (clear / end, frame N): Pn shots / hits /
    hostages / lives_lost / score` (shots: rounds fired; hits: shots that hit an object, `Game::stat_hits`; hostages:
    hostages / researchers shot, `stat_hostages`; testing counters only, outside the game state, so rollback re-runs count
    twice). `HOTD_TRACE_BOT=1`: each target choice, each shot and every 120 frames what the bot sees.
  - Results, headless, Chapters 1-4 x human-bot seeds 1-3 (`--bot S --lives-kept 1`, 12 runs per row), CPU seats only:

    | CPU seats | shots before -> after | hit ratio | hostages shot | CPU lives lost | P1 lives lost | mean chapter 1-4 end |
    |---|---|---|---|---|---|---|
    | P2 Easy | 4192 -> 1994 | 38 % -> 85 % | 4 -> 0 | 168 -> 132 | 201 -> 132 | 21119 -> 19953 |
    | P2 Normal | 6072 -> 3102 | 48 % -> 89 % | 6 -> 0 | 118 -> 66 | 130 -> 75 | 19460 -> 17811 |
    | P2 Hard | 7953 -> 4652 | 62 % -> 90 % | 7 -> 0 | 67 -> 27 | 80 -> 46 | 18179 -> 16976 |
    | P2-P4 Normal | 15689 -> 7662 | 44 % -> 91 % | 8 -> 0 | 113 -> 61 | 39 -> 22 | 18237 -> 16610 |
    | P2 Easy, P3 Normal, P4 Hard | 16760 -> 7934 | 49 % -> 93 % | 16 -> 0 | 78 -> 58 | 24 -> 22 | 17477 -> 15896 |

    Every run clears its chapter. Player 1 idle (never shoots), CPU only, `--lives-kept 1`: all 16 runs clear
    (Chapters 1-4 x P2 Easy / Normal / Hard / P2-P4 Normal); Chapter 1 ends at 22775 / 19093 / 18332 / 16821, Chapter 4
    at 39806 / 24740 / 18972 / 20569; CPU hostages 0 in all. Online (127.0.0.1, `tools/netplay_test.ps1`, 4P on two PCs,
    host P3 CPU Normal, client P4 CPU Hard; and Easy / Normal with `--net-sim 10,20 --net-delay 8`): result ok, same
    hash, 40000 frames; four PCs (`tools/netplay4_test.ps1`, host seats 3 / 4 CPU, the fourth PC joining late): ok.
    Crosshair renders (`--render --shot-at`, not in git): the CPU sights glide onto the bats / the barrel zombies and
    split over them.
- **Everything shootable, a harder Hard and CPU Expert (worker "team", 2026-11-03; trailer comments: "they don't shoot
  the bats or the worms, and not the Chariot's weak point").** game/bot.cpp:
  - Creatures without a body (the bats, kinds 66-68) run their own hit check (`bat_hit_check`: the shot flag, any
    time), not `enemy_part_hits`' camera stop: the teammates now shoot them while the camera moves, and count them in
    the fight within 200 units (they take no attack position).
  - The teammates shoot what the player sees: `Bot::setHalfW` (the window's game area each frame, `gameHalfW`; headless
    the Aspect on a 16:9 screen), so the widescreen sides too (the clamps at +-246 went with it).
  - Hard: reaction 15 -> 11 frames, a 18-unit flick, settle 6 -> 3.5, shake 2.5 -> 1.6, the hand following a moving
    spot 95 % (was 85 %: the Chariot's small, moving hit box was trailed and missed). New level 4 Expert (Options >
    Players "CPU Expert", `--seat-bot P:4`, launcher.ini `p2bot=4`): reaction 8, flick 24, shake 1.0, no lapses.
  - `TEAM` line: `KILLS bats k/n small k/n projectiles k/n enemies k/n boss_hits h boss_damage d` (a kill: a CPU hit,
    `Game::stat_last_hit`, then the thing down or gone within 60 frames; n: how many appeared; boss damage: the boss's
    hit points lost within 2 frames of a CPU hit). 1 player idle + CPU P2, Chapters 1-4: STATUS of the worker.
- **Not done:** 3P / 4P on the results and name-entry screens; four bodies in the cut-scenes (phase 6); online 4
  (phase 7). (The keyboard for players 3 / 4: section 12.)

## 10. Phase 6, four bodies in the cut-scenes (2026-10-20)

- **Draw side only** (game/creature.cpp `coop4_scene_bodies`, after each frame's logic): with 3-4 players in play, in
  the scenes marked "four" (`kSceneFour`: 00 opening run, 02 walk and shoot, 06 reload shoot, 07 jump down a, 09
  stance shoot, 12 reload kick), players 1 / 2's body draws of the frame (DrawModel / SkinDraw `body`) are copied for players 3 / 4 in
  play with types 14 / 15's models (part map from walking both part trees of `creature_type_headers` in step, shadow
  models by role; Rogan's separate coat models dropped) and moved in camera space beside their pair body (out across
  the picture on the pair's side, deeper). Same motions, same frame. Nothing is written to the address space: Chapter
  1 16695 (both sources), input test 1480, 4P Chapter 1 clear unchanged (17785).
- The scene comes from player 1's (else 2's) `scene_routine` address range; all other scenes (03 / 04 / 05 / 08 / 10 /
  11 / 13 / 14) keep the two-set rule. First shots: main checkout `private\coop4f\` (frames 650 / 780 opening, 18200
  stance shoot, 25080 reload shoot). Testing: `HOTD_COOP4_TWO_BODIES=1` draws the two-body picture,
  `HOTD_TRACE_COOP4_SCENE=1` logs the scenes and the copies' places.
- **Retuned (worker "coop4g", 2026-10-20):** camera space is +x = screen right. 09: player 3 right of player 1,
  player 4 between player 2 (at the left door) and player 1; 06 / 12: player 3 out beside player 1, player 4 deeper
  between players 1 and 2 (visible in the second half of the scene); 07 (jump down a) on: four fit. Shots, both
  sources, 16:9 and 4:3 (`--render --size 1280x720 / 1024x768`): main checkout `private\coop4g\` (before / after;
  frames 700, 18200 (09), 19062 / 19072 (07), 25040 / 25080 (06)). Scene 02's bodies are never drawn in Chapter 1
  (first-person door shot), so nothing to place. Testing: `HOTD_COOP4_SCENE_TUNE="S=out3,out4,away3,away4;..."`
  overrides (and turns on) scene S's offsets without a rebuild. Chapter 1 16695 (both sources), input test 1480, 4P
  Chapter 1 clear 17785.
- ~~Not done: 10 / 11 / 12 not reached~~: they were, but seen as 07 / 06 (section 12): done there.

## 11. Phase 7, online 4 players: two PCs with two seats each (2026-10-20)

The first step chosen: **two PCs, two seats each** (no change to the one-peer session, its crypto, join and rejoin);
four PCs (the host relaying three peers) is the next step. Code: `src/net/` (protocol 7), `src/main.cpp`.

- **The settings** carry `seats` (2 / 4; the host's `--players 3-4` or the launcher's 3-4 players = 4) and `players`
  up to 4. Online, `Game::seats4` comes from the host's settings on both PCs (no longer off while `gNet` is set).
- **Seats per PC:** the host owns seats 0 / 2 (P1, P3), the client seats 1 / 3 (P2, P4): the sides stay as the 4P
  mode pairs them (P1 / P3 left, P2 / P4 right). `net::Input` holds the PC's two seats (`flags2 / x2 / y2` = its
  second seat); the INPUT packet carries 18 bytes per frame in the 4-seat game (9 otherwise: 2-player online is
  byte-for-byte as before apart from the protocol number and the settings' last byte), at most 40 frames per packet.
  `applyInputs` sets `Runtime::gun[2 / 3]` and the start buttons of seats 2 / 3. Rollback predicts both seats (aims
  kept, no trigger / start).
- **Rollback snapshot:** the work RAM (the reserved block 0x50C000..0x50FFFF in it) and now the runtime's 4-seat
  fields: `gun[4]`, `shotsIn / reloadsIn / startsIn[4]`, `startRequest[4]`, `seats4`, `seatNotice[2]`.
- **Desync hash** (`NetGame::gameHash`): the work RAM (the block in it) + in the 4-seat game the start requests and
  input counters of all four seats.
- **Join / rejoin:** the log ("H4": two seats per PC and frame) replays all four seats; a joiner (or a rejoining PC)
  gets its two seats back, each pressing start for itself (a mid-game join: like inserting coins). Play Alone with
  4 seats: the host's P1 starts, its P3 joins with its start, the client's P2 / P4 join later.
- **Inputs on the window:** the PC's input slot 0 (Player 1 input) plays its first seat, slot 1 (Player 2 input:
  a second mouse, pad, gun or the keyboard) its second seat (`pads.gamePlayer[1]` = that seat; slot 1's start =
  the seat's start). Both seats' crosshairs of the other PC are drawn from the frame's inputs.
- **CPU teammates** stay local-only (never online): an empty online seat stays empty until that PC presses start.
- **The menu:** the waiting room's row says "Players 2 + 4" for a 4-seat game; the LAN list shows players / 4.

Verified (127.0.0.1, headless, each its own HOTD_PREF_DIR):

| Test | Result |
|---|---|
| 4-seat online, `--players 4`, rollback 8, 40000 frames | chapter 1 ended at 16577 on both, chapter 2 at 35352, all four seats in play, final hashes equal |
| the same at `--net-sim 10,30` both ways | 1463 rollbacks, 11233 frames re-simulated, the same final hash as without loss |
| a joiner mid-game (`netplay_join_test.ps1 -Mode join -Players 4`: host alone with P1, P3 at 500, P2 / P4 at 9000) | joined at frame 3954 (log 4.4 KB, 88 ms), chapter 1 ended at 16531 on both, hashes equal |
| rejoin (`-Mode rejoin -Players 4`, the client killed after 7 s) | back at frame 3545, caught up in 82 ms, chapter 1 at 16577 (as without the drop), hashes equal |
| 2-player online (`netplay_test.ps1`) | chapter 1 at 15810, chapter 2 at 35268, hashes equal (as before) |
| hotd_nettest; `--input-test`; Chapter 1 arcade / `--models pc` | all passed; 1480 / 1480; 16695 / 16695 |

~~Not yet: four PCs~~ Done: **up to four PCs** (protocol 8, 2026-10-20: the host takes three joiners, one seat each,
and relays every frame's four seats; a session per joiner, each its own CPace handshake; docs/netplay.md "Four PCs"),
and since **protocol 15 (2026-10-27, worker "net4pc") one or two seats per PC**: 1+1+1+1, 2+1+1 (in any order), 2+2,
1+1+2 ... The host's "Your Seats 2" (`--net-seats 2`) makes it P1 + P3; a joiner's (`--net-seats 2`) asks for two and
gets its seat and the one across (P2 + P4) or any free one (one only when one is free). The waiting rooms name each seat
and its PC ("Rogan, P1's PC"). A PC dropping out frees its seat(s), the others play on; it joins the running game again
(the log replay) and gets its seat(s) back. Tests: docs/netplay.md "One or two seats per PC". CPU teammates: the host
fills the seats no PC plays (docs/netplay.md "CPU teammates"); nothing new. Still not yet: the window's two-seat input
not play-tested by hand (slot 1's devices).

## 12. Phase 5 / 6 leftovers (worker "coop4h", 2026-10-20)

- **Keyboard for players 3 / 4** (main.cpp `kSeatKeys`, local 4P games only; by key position, i.e. scancodes): player
  3 W A S D aim, F trigger, R reload, 3 start; player 4 I J K L (or keypad 8 4 5 6) aim, H / keypad 0 trigger, Y /
  keypad . reload, 4 start. The keys always work (as player 2's arrows / Space / Enter do), whatever "Player 3 / 4
  Input" says; that setting has a new value **Keyboard** (launcher.ini `p3input=1` / `p4input=1`: no pad, gun or mouse
  claimed). Aim speed as player 2's (a screen width in 1.5 s); held keys are not applied while a name is typed. The
  name entry still gets the letters first. No key rebinding (the game has no rebinding page for any player).
  Input test: 2 new checks (622: D moves player 3, F / keypad 0 fire players 3 / 4; 623: R reloads player 3, I moves
  player 4): 1482 checks, 0 failed.
- **Settings of players 3 / 4:** pad profiles (`pad3.` / `pad4.`) were already saved and loaded; a light-gun
  calibration (F9) of players 3 / 4 was not kept (main.cpp copied only two players into launcher.ini): now all four.
- **RELOAD for players 3 / 4** (`Hud::drawSeatHud`): the arcade's RELOAD sprite (0x36) blinking 45 of 60 frames while
  the gun is empty (hud_ammo_draw's rule: empty, lives, scene state not 3 / 5, shots on), after 120 frames the region's
  "shoot outside the screen" line (0xCA700) under it; in the seat's colour, right under the seat's top band. When the
  same side's player 1 / 2 shows its RELOAD too and they would meet, player 3's / 4's goes under that one (y 0x1F, no
  line). The frame count is the HUD's own (no game state).
- **Hit notices of players 3 / 4** (t1_bounds.cpp `hit_screen_shake`): were drawn already; now one at a time as
  hud_player_notice_show (a new hit while one shows is ignored), with its sound (0xCA660[kind]) and the hit voice 5
  frames later (player 3 player 1's two voices, player 4 player 2's; which of the two by the frame's parity instead of
  `rand()`, so no game state is touched).
- **The top band vs the boss life bar:** the band already moved down past the life bar / title cards (tile layers 0 /
  2), but players 1 / 2's hit notices (the blood splash on layer 2, up to 60 frames) also pushed it down, up to 16
  rows (Chapter 2's boss: player 4's band in the middle of the screen). The notices' cells (kPopups 0x51F480, place
  0x94368, size from the sprite's layout) no longer count. Renders (arcade 4:3, PC 16:9): the bands under the life bar
  in the Chariot and Chapter 2 boss fights.
- **The 4P top 10 in High Scores** was already there (the table arrows: Arcade, the Sonic presets, then "4 Players" and
  its presets; file `<ranking>4p`): checked with `--frontend-shot --frontend-screen 8` and `HOTD_FE_PAGE=4`.
- **Cut-scenes 06 / 07 / 10 / 11 / 12:** the scene was taken from player 1's `scene_routine` address range, but a
  scene's later routines are shared: 10 / 11 run 07's (0x4CD60 / 0x4CED0), 12 runs 06's (0x4CB70 / 0x4CBF0), 13 05's.
  So 10 / 11 / 12 were drawn with 07's / 06's places, and coop4g's "07" / "06" shots (whole-game frames 19062 / 25080)
  were Chapter 2's 11 / 12. Now `cam_job18_player_scene` (the CCTB job that starts every player cut-scene,
  `player_scene_routines[n]`) records n in `Game::coop4_scene` (a C++ field, only while the 4P mode is on), and
  `coop4_scene_bodies` uses it when the address range is 07 / 06 / 05. Where they are (4P, autofire, default route):
  Chapter 2 frames 1313 (11), 7462 (12), 11980 (10); Chapter 1 `--route 1:3=1,1:4=1` 12488 (06), 12772 (07).
  Retuned by eye (renders, arcade 4:3 and PC 16:9; `HOTD_COOP4_SCENE_TUNE`): 06 {4, -6} {2, 10} (all four at the
  start; player 4 behind player 1 at the end), 07 {9, 6} {6, 4}, 10 {6, -3} {10, 8} (player 4 behind the rail
  between players 1 and 2: the frame is tight), 11 {9, -4} {10, 12}, 12 unchanged {8, -2} {14, 26}. All of 00 / 02 /
  06 / 07 / 09 / 10 / 11 / 12 now draw four bodies.
- **Checks:** input test 1482 / 0 failed; Chapter 1 16695 (arcade and PC models); Chapter 4 `--strict --default-route`
  ending over at 34967 (both); 4P Chapter 1 clear 17785 (unchanged).
- **Left:** a key rebinding page (no player has one).

## 13. Play-test fixes: HUD rows, label colours, prompts, teammate flashes (worker "hud4p", 2026-10-20)

- **P3 / P4 band height** (user: "slightly too low"): the band moves down past the game's cells under it (boss life
  bar, title cards) a row at a time instead of 4 rows at a time, so in a boss fight it now sits right under the life
  bar (2 rows higher than before, 16 pixels at 1024 x 768); with nothing there it stays at the top rows as before
  (rounds row 1, label / lamps rows 5-6, mirroring players 1 / 2's rows 0x26 / 0x2A-0x2B).
- **"3P" / "4P" digits** (user: "colors wrong, like the number 3 and 4"): the digit comes from the countdown's digit
  art, which is pale (nearly white), so it came out white-green / white-yellow next to a properly coloured "P". Each of
  its texels now takes the colour of player 2's own "2" texel of the same brightness rank (outline .. core), then the
  whole label goes through the seat's ramp like the "P": the digit has the same shades as the "P", from the players'
  art (no flat tint). Player 4's ramp is a little less orange (mid {236, 180, 16}, player_colours.h): reads yellow.
- **Prompts in the arcade's own colours** (user: "the HOLD YOUR FIRE is supposed to be red"): the arcade draws
  HOLD YOUR FIRE! and RELOAD red and CONTINUE? / PRESS START BUTTON white for players 1 and 2 alike (checked in renders),
  so players 3 / 4's are drawn as they are too (no seat ramp). The seat colour stays on their label, lamps, sights,
  sparks / flashes and hit notices. (Section 9 / 12 had them in the seat's colour.)
- **Players 3 / 4's shot flash was missing:** shot_effect takes the flash model from table 0xCA738[player], which has
  two words; for players 3 / 4 it read the next two (0x958C0 / 0x958D0: code, no model), so their shots drew no flash.
  In the 4P mode they now draw player 2's flash (0x200) through their ramp, like their spark, noted in a ring of four
  in the reserved block (`kFlashes` 0x50CD00, `kFlashNext` +0x1D; the block's end is 0x50CD20).
- **Teammate Flashes** (user: "seeing 4 gun flashes makes it hard to see what we are shooting at"): Options -> Gameplay -> Advanced
  "Teammate Flashes" Auto / Full / Reduced / Off (launcher.ini `teamflash=` 0-3, `--teammate-flashes
  auto|full|reduced|off`). Auto = Full with 1-2 players (the arcade's look, unchanged) and Reduced in the 4P mode.
  This PC's own player (local: player 1; online: the PC's first seat) is always drawn in full. Reduced: the others'
  flash at 0.45 x its size for its first 4 frames (of 10), their spark 0.6 x for 20 frames (of 60), and a flash /
  spark at the same spot as another one started in the same frame is drawn once (the own one wins). Off: none of the
  others'. Draw list only (`Runtime::teamFlashDraw`): no game state, so netplay / lockstep are not affected and each PC
  picks its own. `HOTD_TRACE_TEAM_FLASH=1` logs every flash / spark draw and the decision (a 4-bot 8000-frame run: 244
  same-spot draws merged).
- Renders (arcade and PC models, 4:3 and 16:9; HOLD YOUR FIRE, RELOAD, CONTINUE?, PRESS START BUTTON, the Chariot fight,
  a player 4 flash full / reduced): worktree `build\shots\n3_*`, `tf_full` / `tf_reduced`.
- Checks: input test 1562 / 0 failed; Chapter 1 16695 (arcade and PC models); Chapter 4 `--strict --default-route`
  ending over at 34967 (both); 4P Chapter 1 17785 (unchanged).

## 14. The cut-scene viewer (worker "sceneview", 2026-10-20)

To look at the four bodies without playing up to each scene: Gallery -> **Cut-scenes** (screen 19,
game/scene_view.h), or `--scene-view C:S` (with `--players`, `--models`). It runs the game's own scene code:

- **Where each scene is** (4 players, `--autofire`; `HOTD_TRACE_SCENES=1` prints every start / end of a run):
  Chapter 1: 00 opening run ~244, 02 walk and shoot ~5844, 08 finish pose ~16308; with `--route 1:3=1`: 03 ladder
  ~9018, 06 reload shoot ~12489, 07 jump down a ~12773; with `--route 1:5=1`: 04 fall backside ~7722. Chapter 2 (the
  autofire's own route, not `--default-route`): 09 ~183, 11 ~1314, 12 ~7463, 10 ~11981 (forced with `--route 2:5=0`: 2 players miss it otherwise), 14 rattle door ~21715.
  Chapter 3: 15 jump down ~283. Chapter 4: 17 dash ~183, 18 ~11836. Not reached in any route tried: 01, 05 / 13
  (knocked flying), 16 (run jump). `--default-route` skips 11 / 12 in Chapter 2.
- **How:** the chapter starts (route forced where listed, lives kept, Sonic Mode and CPU teammates off), runs headless
  with the autofire player (no picture, no sound) until cam_job18 starts that scene (`Game::scene_started` /
  `scene_starts`: written by cam_job18 in every game, read only by the viewer), then the netplay rollback save-state
  (`net::RuntimeState`, plus the scene fields and `Runtime::shootable`, which the autofire aims from) from 90 frames
  before is restored and played live. The lead-in keeps the autofire input (so the play goes the same way), the scene
  gets no input. The save-state is kept for that game: Replay is instant; a new Play seeks again (a save-state is never restored into a new game: the rollback state holds raw pointers into its runtime, Runtime::cardLatch_; that crashed in sprites_frame). A seek
  takes 0.0-0.5 s. The end: the bodies' routines done (or the shooting on again) + 45 frames, at most 40 s.
- **Checks:** `--scene-view C:S --frames 1` (headless) seeks, watches, replays, starts a new game and seeks again, and
  compares the work RAM at the end of the three: all 15 scenes `result=ok` (4 players arcade; 1 / 2 / 3 players and PC
  models on 2:12 and others). (With `--render` the restart differs at 0x702000: the render harness's view is set up
  once for the first game; the window sets it up per game.) `HOTD_SCENE_TEST=<dir>` with `--scene-view` in a hidden
  window: pictures, pause holds the frame, slow motion, the end, Replay, Esc back to the page (all passed on 2:12 PC).
  Renders (4 players, 1024 x 768, arcade and PC): 1-00, 1-06, 2-09, 2-10, 2-12 show the four bodies (worktree
  `build\shots\r_*`).
- **Clean View** (worker "cliprec", for recording clips; the page's "Clean View", On by default, key C there and while
  watching): while watching, `Hud::clean` leaves out the 2D layer over the 3D (lamps, ammo, prompts), players 3 / 4's
  HUD, the boss clock and the sights; the cut-scene bars and the fades stay. No mouse cursor. The viewer's top-left
  lines are replaced by a small hint at the bottom (the keys) for 2 s after the start or any key, fading over its last
  0.5 s; at the end nothing until a key. Off: the old look.
- **Music:** the seek runs with `Runtime::audio` null, so its sound codes were lost (the scene played without the
  chapter's music, the menu's track could go on). The seek now keeps them (`Runtime::silentCues`); the save-state keeps
  the last music code before it (`Audio::isMusicCode`: a PC cue that plays a track or turns the music off). At each
  watch start (first and Replay) the sound is silenced (board reset), 0xBE5000 and that code are sent: 2-12 starts
  Chapter 2's 0xA01002 from the beginning; 1-00 0xA00002 (music off, the chapter's comes later live). The board's first
  sound comes about 0.25 s after the start (its reset). Checks (`HOTD_SCENE_TEST`, `--audio-wav`): 2-12 arcade / PC,
  1-00, 4-17: no HUD / sight / text in `scene_a`, `scene_over`; `scene_hint` shows the hint; the WAV has sound all
  through both watches (before: silent gaps, no music).
- **Actors (worker "actorswap", 2026-10-26; like Metal Gear Solid 2's demo theater):** who plays each role while a
  scene plays in the viewer. Roles: Rogan's, G's, players 3 / 4's part; actors: Rogan (type 2), G (13), Player 3 (14,
  "Rogan 2"), Player 4 (15, "G 2"), Sophie (34), Researcher (26) - the bodies with the players' skeleton part for part
  (PC Mode's character table). Page: a row per role (Remake: Left / Right or a click; Classic: a click; both: keys 1-4,
  Shift back). Watching: A (a pad's LB) opens the panel, Up / Down the role, Left / Right the actor; on a paused picture
  players 1 / 2's roles change at once. Draws only (`Runtime::actorSwap`, set by `SceneView::applyCast` while the viewer
  is open, all -1 otherwise): after the frame's logic `Runtime::actor_swap` gives players 1 / 2's body draws (the
  ending's own body tasks 0x59120 / 0x59B10 too) the part models of the picked type through `coop4_part_map` (Rogan's
  arcade coat dropped); players 3 / 4's copies take the picked type in `coop4_scene_type`. A type whose part tree
  differs keeps the role's own actor. Testing: `--cast g,rogan,sophie,researcher` with `--scene-view` (by role; `-`
  keeps one); `HOTD_SCENE_TEST` also opens the panel while paused (`scene_actors.png`).
- **Actors, part 2 (worker "actors2", 2026-10-26):**
  - *More actors:* the ten researchers (types 17, 19-22, 49 on the man's body, 23, 25-27 on the woman's; keys `r1`-`r6`,
    `w1`-`w4`, `researcher` = `w3`). Every human body has the players' 20-joint tree (maincpu 0xC7240), so no bone mapping
    was needed. Left out (shots of every actor in 1-06, 2-12, 3-15, 4-17, 4-19, both models): Dr. Curien (60, 78) and the
    zombie Sophie (67): arcade, their textures are in none of the texture areas those scenes load (wrong pages); PC,
    Curien's head lies on its side and a hand draws a long stick (his parts are not modelled in the players' axes) and
    the zombie Sophie is Sophie's model. A part the actor has no model for (model 0, "space") is not drawn.
  - *Men's / women's motions:* the game's only pair is the jump down (player_body, scenes 07 / 10 / 11): MO_p_tobio_a
    0x178 for the men, MO_s_tobio_a 400 for Sophie (97 frames each). A woman actor (Sophie, the woman researchers) in a
    man's body playing 0x178 (or a man in Sophie's body playing 400) is re-posed with the other one at the same frame
    (`Runtime::actor_pose`: each joint angle moved by the difference of the two records, so blends stay blends; the
    root height too; mirrored motions not). Rebuilding the drawn pose from the records matches it exactly
    (`HOTD_TRACE_ACTOR_POSE=1`: "rebuild off by 0.0000"). Every other motion: the part's own.
  - *The coat follows Rogan:* `Runtime::actor_dress` draws a body as an actor: models, pose, Rogan's arcade coat only on
    Rogan (also when he plays G / P3 / P4: body_part_draw_hook's model from that body's motion and frame; not with PC
    models, whose Rogan has it built in), Sophie's strip only on Sophie. Players 3 / 4's copies use it while the viewer
    is open; outside the viewer nothing changes.
  - *Saved:* the picks go to launcher.ini `scenecast=` (e.g. `sophie,r2,p3,p4`) when changed on the page or the panel
    (not with `--cast`), and are read at start.

## 15. Prompts follow the model source (worker "promptcol", 2026-10-20)

User decision: the prompts' look depends on the models / textures picked (Arcade / PC), not on the seat.
- **What the PC draws** (THOTD.EXE, read from the decompilation): one sprite for both players, no per-player colour:
  RELOAD 0x421 (`hud_ammo_draw` 0x50B7E4, at x player x 0x1F + 4, row 0x14, as the arcade), HOLD YOUR FIRE! 0x445
  (`hud_sprite107_blink` 0x50BB64, red), CONTINUE? 0x51B + its digit 0x51C.. (0x50BC24 / 0x50BCAC, white; real PC
  capture `pcpolish2\gameover` t02264). Unlike the arcade it draws **no "shoot outside of the screen" line** under
  RELOAD: its hint sprites 0x527 / 0x529 are loaded (bank 0x38) and sit in a table at 0x5F2E94 that no code reads.
  And in a game it draws **no PRESS START BUTTON** on an empty seat (hud_start_prompt is empty; drawPc2D already
  skipped it for players 1 / 2).
- **Arcade models:** unchanged (section 13): the arcade's own sprites for all four players (RELOAD red with its hint
  line, HOLD YOUR FIRE! red, CONTINUE? / PRESS START BUTTON white). 1P / 2P byte-identical.
- **PC models:** players 1 / 2 lose the hint line too (drawPc2D skips the region's 0xCA700 sprite in a game), so they
  match the PC. Players 3 / 4 get the same as the PC's players 1 / 2: the same PC sprites as they are (no seat ramp,
  as the PC has no per-player prompt colours), RELOAD alone, no PRESS START BUTTON on an empty seat.
- **Band fix:** with every player out (the game's own CONTINUE? screen: the rope and the big CONTINUE? fill the top 16
  rows) the P3 / P4 band slid 16 rows down onto the big CONTINUE?; now, when no free row is found, it stays at the top.
- Testing aids: `--lives-out F` also empties players 3 / 4 in the 4P mode; `HOTD_NO_RELOAD=1` keeps `--autofire`'s
  empty guns empty (RELOAD stays up for renders).
- Renders (arcade / PC models, 4:3 1024 x 768 and 16:9 1280 x 720; HOLD YOUR FIRE! with P4 empty, RELOAD, all four
  CONTINUE?): worktree `build\shots\p_*`.
- Checks: input test 1562 / 0 failed; Chapter 1 16695 (arcade and PC models); Chapter 4 `--strict --default-route`
  ending over at 34967 (both); 4P Chapter 1 17785 (unchanged).

## 16. Cut-scene variations for players 3 / 4 (worker "scenevar", 2026-10-20)

User: "some scenes might not make sense, so make multiple possible variations ... so I can compare and pick".

- **Setting:** launcher.ini `scene4p=00:B,02:D,...` (scene number : letter), `--scene4p` on the command line,
  `HOTD_SCENE4P` (testing / rendering, over both). A scene not named keeps **A**, the look of sections 10-12 (the
  default is unchanged). No Options row yet: the user picks from the clips first.
- **How** (game/creature.cpp `coop4_scene_variant`, draws only like `coop4_scene_bodies`): every frame of the scene,
  players 1 / 2's body draws are kept in world space (with that frame's camera). A variation draws player 3 / 4 from
  a kept frame: live, `delay:N` (a beat later), `hold:F` (the pose of scene frame F, kept from then on: it stays at
  its spot while the camera moves), with `src:1|2` (whose body), `out / up / away` (camera-space place, as kSceneFour),
  `yaw:D` (turned about the body's up axis), `from / until`, `off`. Only the game's own motions and the type 14 / 15
  models. Scenes 15-18 are told apart by cam_job18's scene number (`coop4_variant_scene`).
- **The variations** (`kVariants`): 00 B a beat later, C they stay at the car; 02 (the mansion door) B both wait by
  the door with G, C player 3 shoots too, D all three shoot in turn; four-body scenes 06 / 07 / 09 / 10 / 11 / 12:
  B wait as player 2 does, C the same turned round (guarding the rear), D A a beat later; two-set scenes 03 / 04 /
  08 / 14 / 15 / 17 / 18: B all four, C players 3 / 4 stand still at the side, D B a beat later. 08 shows the bodies
  only in its first second, 18 hardly at all.
- **Clips:** `HOTD_SCENE_CLIP=<file.mp4>` with `--scene-view C:S --render --size 1280x720 --frames 1` pipes the scene
  (from 15 frames before its start, 30 fps, no sights) to the local `ffmpeg` (H.264, `HOTD_SCENE_CLIP_CRF`, default
  26). `HOTD_SCENE4P_DEF="02E=spec p3 | spec p4;..."` tries a new variation without a rebuild. All 59 variations, 4P and
  3P (PC models for 00 / 02 / 09), contact sheets and a page to pick from: main checkout `private\scene4p\index.html`.
- Checks: input test 1562 / 0 failed; Chapter 1 16695 (arcade and PC models); Chapter 4 `--strict --default-route`
  ending over at 34967 (both); 4P Chapter 1 17785 (unchanged, also with variations picked: draws only).

## 17. Viewer fixes, the Magician's farewell, Free Cam (worker "cutfix", 2026-10-22)

- **Pause stops the sound.** Space / P / left click / Start in the viewer now holds the music and every sound where
  they are (the arcade sound board too), as the game's own pause does (`Audio::pause`); unpausing goes on from that
  point. Replay unholds and starts the sound cleanly (as before: silence, board reset, the scene's music again).
  Slow motion: the sound keeps its normal pitch and speed (the music runs on its own clock, so it gets ahead of the
  picture; effects play at normal pitch when the half-speed game cues them).
- **"Goodbye Curien" (4-19) added.** It is not one of cam_job18's player scenes (that list is complete: 01, 05 / 13
  and 16 are never reached), so it was missing: it is the ending's event 2 after the Magician's defeat (mode 4
  sub-mode 8, chapter slot 6; `ending_p1_b` / `ending_p2_b` talk: "I guess everything's over", "Good bye, Curien",
  "Farewell Sophie"). `Runtime::kSceneEnding` = 19 (ours), `--scene-view 4:19`, `scene4p=19:X`. Found ~26580 frames into
  Chapter 4 (seek 0.6 s); over when the event ends (the staff roll). 1 player sees the ending's event 0 (Rogan alone).
  Its lead-in is short (20-49 frames: the 1.5 s before were the chapter's last sky shot); clips start at its start.
  Players 3 / 4 in the ending: game_ending_start puts players 1 / 2 to state 7, so the four-body code counts 5 or 7
  there; their bodies are found by the ending tasks' routines (0x59120 / 0x59B10). Their HUD is off in the ending
  (their HOLD YOUR FIRE! blinked through it; also in a real game).
- **Variation E: mirror player 2.** Players 3 / 4 copy player 2's motions, 8 / 16 frames later, spaced out beside /
  behind player 2 (`src:2,delay:8,out:8,away:9 | src:2,delay:16,out:15,away:18`). **Default for 4-19** (`kDefaultPick`;
  the old two-body look is `19:A`). E is a choice on every other scene too (`S:E`); their defaults are unchanged (the
  tight two-set shots, the ladder from above, Sophie, were not checked one by one). Delayed / held copies now use the
  copied body's first drawn frame until the delayed one exists (no pop-in at a scene's start; also B / D).
- Clips: main checkout `private\scene4p\magician_{1,2,3,4}p_{arcade,pc}.mp4`, and `magician_4p_arcade_oldA.mp4`.
- **Free Cam** (user: trailer footage): key **F** (pad: a stick click) while watching or at the end. The picture is seen
  from our own camera (`GameView::camOverride`, draws only), starting where the scene's camera is; the scene, its
  camera and the game go on underneath; pause + Free Cam = photo mode. W A S D / arrows move, Q / E down / up, Shift x4,
  Ctrl x0.25, right-drag looks, the wheel zooms (field of view 5-110 degrees); pad: left stick move, right stick look,
  triggers or LB / RB down / up, left-stick click x4 speed, right-stick click off (ipolish: any pad, not only player
  1's; the line in the pad's names). While it is on, S is a movement key: slow motion is M (M works always). F again: the scene's
  camera at once. Works with Clean View, Subtitles off, slow motion. Seen from other angles the world is drawn as the
  draw zone has it (`draw_zone`): what the scene never shows can be missing (not changed). Testing:
  `HOTD_SCENE_FREECAM="F,right,up,forward,yaw,pitch[,zoom]"` with a clip / render; `HOTD_SCENE_TEST` now also turns it
  on with a right-drag and the wheel ("free cam on, yaw +0.78 rad, fov x0.81"). The Cut-scenes page's own key line
  still lists the old keys (that page is being restyled by another worker).
- Checks: input test 1622 / 0 failed; Chapter 1 16695 (arcade and PC); Chapter 4 `--strict --default-route` ending
  over at 34967 (both); `--scene-view 4:19 --frames 1` result=ok; `HOTD_SCENE_TEST` on 4:19 passes.

## 18. The 4-player cut-scenes fixed (worker "scene4fix", 2026-10-22)

User: "a lot of the 4-player cut-scenes do not make sense, or there are clipping issues and unnaturalness". All 16
scenes were rendered (3 / 4 players, arcade and PC; 24-frame strips) and fixed one by one. New variation **F**, the
**default of every scene** (`kDefaultPick`); A .. E stay selectable (`scene4p=19:E`, `09:A`, ...). Draws only.

- **New words** for a variation seat (game/creature.cpp `coop4_scene_variant`): `fix` places the copy once per
  camera shot (from the shot's first camera: a delayed copy no longer slides or jumps while the camera moves), with
  `out` / `away` laid flat on the floor (the world's up axis) so the copy keeps its source body's floor height
  (`up` along the up axis), and nudges it each frame to at least `gap` (default 7) across the floor from players 1 / 2
  (as they stand now) and from the other copy. `cam:N` takes the place from the camera of the scene's frame N instead
  (for a shot whose camera swings far: 3-15, 4-17, 4-19). `hide:1+3` leaves a seat out in camera shots 1 and 3 (a cut =
  the camera moving > 6 units or turning > 10 degrees in one frame; `HOTD_TRACE_SCENE4P=1` logs the cuts and places).
- Main checkout `private\scene4p\fixed\index.html`: before (main's default) / after per scene, 4 players arcade, PC
  for 00 / 02 / 09 / 12 / 17 / 19, 3 players after.

| Scene | F (the new default) |
|---|---|
| 1-00 opening run | as E (G's motions a beat later), placed per shot, kept apart |
| 1-02 mansion door | P3 / P4 wait against the wall with G while Rogan shoots, then follow |
| 1-03 ladder | new: P3 / P4 wait at the ladder's foot (before: 2 bodies) |
| 1-04 fall through the floor | new: all four drop, P3 / P4 behind (P4 copies Rogan) |
| 1-06 reload shoot | P3 / P4 behind G by the shelves, apart from Rogan |
| 1-07 jump down a | a row of four on the walkway, then they jump after G |
| 1-08 finish pose | new: a row of four (P3 copies G, P4 Rogan) |
| 2-09 stance shoot | on the balcony right of Rogan, inside the door frame |
| 2-10 jump down b | off: seen from below, no room (A drew P3 into Rogan) |
| 2-11 jump down c | beside / behind G, apart |
| 2-12 reload kick | P3 beside G, P4 behind between Rogan and G |
| 2-14 rattle door | new: a row behind G |
| 3-15 jump down | new: P3 jumps in left and crouches by G (P4 just out of the shot) |
| 4-17 dash | new: P3 / P4 land on the bridge ahead of Rogan (E put them off the bridge) |
| 4-18 Curien's talk | off: the camera is at the players' backs (E filled the picture with a back) |
| 4-19 goodbye Curien | a spaced row beside G in both shots (E: a line hidden behind G) |

- Checks: input test 1951 / 0 failed; Chapter 1 16695 (arcade and PC); Chapter 4 `--strict --default-route` ending
  over at 34967 (both); 4P Chapter 1 17785 (unchanged, also with `--render`); `--scene-view 1:00` / `4:19 --frames 1`
  result=ok.
- **Fix 2 (worker "scene19", 2026-10-22).** User on 4-19: "P3 / P4 all rotate to face the camera when the camera
  rotates", and all stood on the left of Rogan and G. Cause: `cam:N` took the place from the camera of the scene's
  frame N, but before frame N (`min(N, now)`) that was the camera of every frame, so the offset turned with the
  orbiting camera (4-19 used `cam:700`: the whole scene; 3-15 / 4-17 their first 60 / 90 frames, where 3-15's dive
  swung P3 / P4 some 40 units). The bodies' own facing was always the copied body's world facing. New words
  `wx:X,wz:Z`: the offset from the copied body in world units on the floor (x, z), no camera in it at all (sets
  `fix`). 4-19: P3 on Rogan's other side a step back, P4 beside G on the far side (Rogan stands at 0, 0, G at
  14.3, -8), both copying G 8 / 16 frames later; 3-15 / 4-17 keep the places frame 60 / 90's camera gave, now from
  the start. `HOTD_TRACE_SCENE4P` logs each seat's camera offset as `wx / wz` words (at a shot's start and frame N).
  Clips: main checkout `private\scene4p\fixed\{19,15,17}_after2_*.mp4` (index.html, last section).

## 19. User notes 2026-10-22 on the fixed cut-scenes (worker "scene4b")

The user watched `private\scene4p\fixed\index.html` (4 players) and gave notes per scene. New variation **G** (the
new default of the scenes below, `kDefaultPick`); F stays selectable (`scene4p=06:F`), 2-11 goes back to **A**.
Draws only. Clips (4 and 3 players, PC models for 1-06 / 2-09 / 4-19), one line per scene, before / after: main
checkout `private\scene4p\fixed2\index.html`.

- **Pop-in at a cut (code, every `fix` seat):** at a camera cut the game moves the bodies to the new shot's spot a
  frame or two after the camera. A delayed copy (`delay:N`) drew a frame from before that move, at the old spot (out of
  the picture), until the delay caught up: P3 / P4 popped in 8 / 16 frames into 1-00's shot in front of Rogan. Now,
  when the copied body jumps (more than 6 units in a frame) within the shot's first 4 frames, the delayed copy never
  uses a frame from before the jump: it holds the shot's first pose for the delay, then moves.
- `HOTD_TRACE_SCENE4P=2` logs every frame (`=1`: every 20th); the lines now carry the camera's world position too.

| Scene | User note | Now |
|---|---|---|
| 1-00 opening run | P3 / P4 pop in while the camera is in front of Rogan | there from the shot's first frame (the cut fix above) |
| 1-03 ladder | good, but P3 / P4 move at the same time | P3 10 frames after G, P4 24 |
| 1-04 fall through the floor | same | P3 8 frames, P4 22 |
| 1-06 reload shoot | P3's feet in the wall, P4 nowhere | both behind G, a little to his left, in front of the bookshelf (wx / wz) |
| 1-07 jump down | all look right at once; P4 left of Rogan | 10 / 20 frames; P4 left of Rogan, clear of his jump |
| 1-08 finish pose | heads go through the wall when the camera turns | a row left of G on the floor (wx / wz), off the wall and the camera's path |
| 2-09 stance shoot | the old one (all four seen), P3 / P4 as G | A's places in world space, both copy G 6 / 12 frames later |
| 2-10 jump down b | the old one was fine | A again; P3 waits on the stairs (`hold:0`: it jumped into Rogan in A) |
| 2-11 jump down c | old 4-player look; 3 players: Rogan hits P3 | A (P3 left of Rogan) for 3 and 4 players |
| 2-12 reload kick | perfect | unchanged (F) |
| 2-14 rattle door | try P4 right of Rogan | P4 right of Rogan (fits, no clipping), P3 behind G |
| 3-15 jump down | P3 more forward, P4 nowhere | P3 left by G, more of it seen; P4 lands right of Rogan |
| 4-17 dash | G seems to hit the rail | the 2-player game is the same (the original): unchanged |
| 4-19 goodbye Curien | P3 / P4 turned with the camera, all on the left | checked after scene19's fix: they keep their spots, P3 on Rogan's other side: unchanged |

- Checks: input test 1951 / 0 failed; Chapter 1 16695 (arcade and PC); Chapter 4 `--strict --default-route` ending
  over at 34967 (both); 4P Chapter 1 17785.

## 20. User notes 2026-10-26 (worker "hudfix4")

- **Heads (4-19):** G's head draws its talking-mouth models (0x282.. / 0x28E..) while he speaks; they are not in the
  part tree, so `coop4_part_map` had no entry and the copies drew G's own head (no cap / beard / glasses: "pop in and
  out"). `coop4_copy_model`: a head (role class 1) draw the map does not know becomes the copy type's model of that
  joint (`coop4JointMaps_`, by the part data's role); `DrawModel::joint` carries the joint from body_part_draw_hook.
  No detail levels are involved (the game has none for the bodies). `HOTD_TRACE_SCENE4P` logs unmapped models.
- **4-19 variation H** (default): `src:2,delay:8,wx:-7.15,wz:4 | src:2,delay:16,wx:7.15,wz:-4,mirror` (Rogan 0,0, G
  14.3,-8: a row P4 | G | P3 | Rogan, steps of 8.2). New seat word **`mirror`**: the copy is reflected about the copied
  body's middle plane (its x axis); the left / right draws are paired once per scene (from the first drawn frame, by
  the mirrored place in the body's frame) and each part is drawn as `S M_pair X` (X = the pair's first-frame link), so
  the limbs swap and the gun stays in the right hand. F stays selectable (`scene4p=19:F`).
- **Ending:** no sights in mode 4 sub-modes 7 / 8 (players 3 / 4 stay state 5 there). The staff roll is viewer entry
  `4-20` (`Runtime::kSceneCredits`, ending event 3).
- **HUD:** P3 / P4 HOLD YOUR FIRE! at row 0 (the top bar; P1 / P2's at 0x2B); PRESS START by coin_update's counter
  0x51F560 (as hud_prompt_anim); P2's hit notice (kPopups record 1) with 3-4 players is taken out of the composed tile
  layer and drawn again through a blue ramp (`kPlayer2BloodRamp`), P3 / P4's notices now also show kind 9 (claws).

## 21. CPU teammates like people, enemy pressure for 3-4 players (worker "cpuai", 2026-10-26)

User: "still robotic: aim-bot, no randomness, always aim at the same exact place; sometimes ignore the boss's weak
spots; still shoot researchers with no remorse; CPU Normal too easy; 3P / 4P should be harder." Numbers:
docs/changelog/2026-10-26.md.

- **The hand** (game/bot.cpp `teamPlay`, `kTeamLevel`): a new target gets a reaction time `react` x exp(N(0,
  `reactSd`)) (+12..40 frames with chance `hesitate`); the sight flicks with an over / undershoot N(0, `gainSd`) of the
  move and N(0, `perpSd`) sideways; once it rests, a micro-correction every `corrMin..corrMax` frames keeps 10-40 % of
  the error plus N(0, `settle`). It pulls when the sight is within 0.9 x the spot's radius, or at rest with an error
  under 2 x `settle` (it cannot see that small an error: some shots miss), or now and then (`hasty`) within 2.5 x.
  The shot lands at the sight + N(0, `tremor`), clamped at 2.5 sd. Bursts / gaps / pauses as before, a pause now and
  then 30-90 frames longer (`lapse`), reloads now and then 30-90 frames late (`lateReload`). Only the bot's seeded
  generator and the game state: local runs repeat (checked), online through the lockstep (hashes equal).
- **The spot** (`pickSpot` / `spotPoint`): a body part (+0x5C sphere, +0x74 radius) and a place on it (N(0, `spot`)
  x radius, at most 1.1); others: drawn by radius x (0.5 + damage / the creature's most), parts that do no damage
  0.1; after a burst 40 % take another spot. The damage of a part (`partDamage`) is part_hit's: the type's role class
  (part_roles 0x1184E0 + (type x 31 + role) x 8, +3) and the class's damage at the part's step (0x12F9F0); the
  Chariot without armour (type 36): its class table (0x11A6CB / 0xCAC40); the Magician: roles 4 / 6 / 11 / 13.
- **Bosses**: the spot is always a part with the most damage, the one nearest the sight (big parts first when all
  are alike: the Hanged Man), re-checked every 15 frames; tables seen: Chariot plates chest 45 / others 18-31, the
  Hermit only part 27 (10), the Hanged Man all 10; the armoured Chariot: its hand (`chariot_hitbox_task`); its
  armour falling off (`chariot_armour_break`, still type 35): no shots (all sparks).
- **Hostages**: `hangedman_captives` were taken as the boss (name with "hangedman"): now people ("captive"). A spot
  is never within its radius + 10 of a hostage sphere; no pull while any hostage sphere is within 2.5 x `tremor` + 6
  of the sight (the shake's reach; the pull then may move to another spot).
- **Levels** (Easy / Normal / Hard): react 32 / 23 / 15, speed 7 / 10 / 14, settle 14 / 10 / 6, tremor 5 / 3.5 / 2.5,
  hasty 4.5 / 3.5 / 2 %, bursts 1-3 / 2-3 / 2-4, pauses 34-64 / 20-40 / 10-22, late reloads 40 / 25 / 10 %. Hit
  ratios 69 / 77 / 83 % (were 87 / 89 / 90 %).
- **Measuring**: the TEAM line adds `cpu_boss_shots`, `weak_aimed`, `weak` (landed on the weak part's sphere),
  `held_for_hostage`, `cpu_shots_on_hostage` (a CPU shot whose point is inside a hostage's sphere: must be 0). The
  game's `stat_hostages` for kind-100 hostages reads +0x2EB (the rescuer), so in 4P it can name the wrong seat.
- **Enemy pressure** (3 / 4 players in play, 4P mode): `coop4_enemy_damage` (after enemy_part_hits' parts) divides
  this hit's damage and the seats' tallies by 1 + `coop4EnemyStep` x (n - 2), rounded, at least 1; not while a boss
  life bar is up (0x543530: the bosses have `coop4BossStep`). Part armour counts, blow-offs and eyes are the
  arcade's. Options > Gameplay "4P Enemy Toughness" (launcher.ini `enemy4p=`, default 50, `--4p-enemy-step S`);
  "4P Boss Hit Points" default 50 (an ini written before `enemy4p=` existed with `boss4p=25` reads as 50). Both are
  each PC's own setting, like boss4p before: online the PCs must match. 1-2 players: unchanged.
- **4P researcher penalty**: `researcher_hit_by_player` called player_hit(side), which in 4P passes the hit round the
  players (coop4_victim); now the shooter's seat (coop4_shooter) loses the life (player_hit's own steps).
- **Checked, unchanged**: a player out of lives (2P / 4P headless with `--lives-out`, enemies' camera-space depth
  per 120 frames): no creatures behind the camera beyond normal; targets come from the attack slots by side as the
  arcade's. Creatures with parts shot off still attacking: part blow-offs count hits per part, death goes by hit
  points (part_hit / enemy_part_hits, arcade pairs); kind 70 fights on below 0 hit points while +0x324 bit 4 (arcade).

## 22. Follow-ups (worker "followup", 2026-10-27)

- **The zombie that walked round the players after a continue** (user: "when all players died waiting for continue,
  the first zombie walks around the player even when they all came back"). Compared with MAME (2 players, Chapter 1's
  first fight, nobody shoots, both wait 4 s at CONTINUE, both continue; a Lua tap logs every frame): in the arcade
  `players_in_game_b` stays 2 while both are out (it drops only when a continue runs out, at game over), attack
  positions 0 / 1 stay beside the camera (-78.8 -1286.1 / -77.1 -1276.2), and the zombie (type 28) steps back to its
  position, waits (state 6) and attacks again within a second of the continue. Our 2P run does the same. **4P differed:**
  the per-frame side count set `players_in_game_b` from the players in play only, so with everyone out it was 0, the
  attack positions came from table row 0 (on the camera), and the zombie's retreat point (state 8 -> 9: its attack
  position) was the players' own spot: it walked round them (state 9 keeps walking while within 25 of the target)
  and went on after the continue. Now `players_in_game_b` counts a side while a seat of it is in the game (states
  0-5, 10: also out of lives waiting at CONTINUE), as the arcade's. After: 4P all out at frame 3573, the zombie
  waits at -79 -1283 (state 6) and attacks after the continue at 4302, as in MAME. Logic change in 4P only; 1-2
  players unchanged.
- **Players 3 / 4's hit notice with the PC models** was a solid picture in the seat's colour (player 4: a yellow
  block); the arcade's splash / claw marks are stippled (every other pixel pen 0). With the PC models the seats'
  notice now takes a checkerboard, as the arcade's (pictures: worktree `wt-followup\build\shots\hit_pc\render_f1530.png`
  before, `hit_pc2\render_f1530.png` after, `hit_arcade\render_f1530.png` the arcade). Players 1 / 2's stay the PC's
  own solid ones. Draw-only.
- **4-19: P3 floated for a split second after "Shall we go now?"** (the previous worker could not see it). At that cut
  (frame 32133 in `--scene-view 4:19`) the game puts Rogan and G 0.2 lower and turns them round; P3 / P4 copy G 8 / 16
  frames later (variation H, `delay`), and the cut rule (copies hold the shot's first pose after a cut) only looked for
  a body moved more than 6 units, so P3 drew G's pose from before the cut, 0.2 above the new floor and in the old
  posture, for 8 frames, then dropped (P4 the same for 16, at the picture's edge). The same in every 4P set-up that uses
  a delayed copy (arcade / PC models, PC Mode characters, the viewer or the game; any frame rate: interpolation is off
  across a cut, the drop came 8 frames later). Now a re-seat at the cut counts too: the copied body set more than 0.05
  higher / lower or turned more than 60 degrees in one frame where it stood still the frame before (a fall across a cut,
  1-04, is not one). Trace (`HOTD_TRACE_SCENE4P=2`): P3 / P4 at y 9.8 from the cut on (before: 10.0 until 32141 /
  32149). All 16 scenes compared old / new (P3 / P4 places per frame): only 4-19 changes, plus 1-00 by 0.2 units on one
  frame and 1-04's first 8 frames of the fall (the held pose is the fall's second frame instead of its first).
  Pictures: worktree `wt-followup\build\shots\s19pc\render_f32136.png` (before) / `s19pc2\render_f32136.png` (after).
  Draw-only.
- **Viewer actors: Dr. Curien, Dr. Curien (Dead), Sophie (zombie)** (`--cast` words `curien`, `curien2`, `zsophie`; types
  60, 78, 67). Arcade models: their textures are in one texture area only (Curien 60: area 11, 78: 12, the zombie
  Sophie: 2 with row 13:92 as the ending's event 6 draws her (galfix3; area 2 is in Chapter 4's sets now), the
  Gallery's looks), so `Runtime::actorAreas` lists their part models while one is picked and GameView
  draws those with that area's set: the scene's own when the scene is in that area, else from Chapter 4's stage (it has
  every area; loaded once, ~0.2 s, as the prologue's other world) with the Gallery's colour rows. Both models: Curien's
  motions hold the head joint about 0x4000 further round than the players' (his head model lies on its side) and his
  left hand holds his cane, so on the players' motions the head lay on its side and the cane stuck out
  (`Runtime::actorPartFix`: the head turned 0x4000 back, the cane hand given his own rest angles relative to the
  forearm: the cane down along his side). PC models: the zombie Sophie is Sophie's model (the PC files have no other).
  Left: a mirrored copy (variation H's player 4) still holds the cane out (the mirror pairs the hands); the belly skin
  between his chest and hips (the players' skeleton spacing) is grey, not his white shirt.
  galact (2026-10-28): players 1 / 2's Curien keeps his cane arm as in his idle (MO_kyu_b frame 0): `Runtime::actor_dress`
  re-builds the cane hand's upper arm, forearm and hand from the drawn chest with those angles (mirrored motions too),
  so the cane stands upright in front of him whatever the role's motion. The page's Actors rows open a list of every
  actor (both menu styles; `onScenes(-(1000 + 100 r + k))`).
  polish28 (2026-10-28): players 3 / 4's Curien copies get the same cane arm (`Runtime::actor_cane_arm` at the
  recorded frame, `actor_cane_apply` on the copy's drawn chest; were: the cane hand turned only). PC models, 4-19:
  Curien's coat (and other light colours: the trousers, Sophie's legs) were pure white: the PC's screen fade went to
  white at the Magician's end (`Runtime::pcFadeStart(1, 2)`, every colour doubled) and was held over the whole
  farewell; it now comes back with the arcade's fade-in from white (fade id 2, 363 frames). Checked: `--scene-view 4:19
  --cast -,-,curien2,curien` both models, `--cast -,curien2 --models pc`; players 1 / 2's copies unchanged (pictures
  byte for byte). Pictures: main checkout `private\changelog\img\polish28\`.
- **The zombie Sophie's gun hand (rellook, 2026-10-30):** galfix4 kept the role's gun hand (0x719 `ter00a`, Rogan's
  skin) on actors without a gun; on the zombie Sophie that was a healthy hand. Arcade models: her own right hand
  (0xB05 `zsofter00a`, area 2) for the gun hand and for the hook's empty hand 0x36B, plus the gun alone as a second
  draw (`kGunOnlyKey`: ArcadeSource::loadStage copies 0x719 without the polygons on 0x36B's texture rectangles: the
  31 skin / cuff polygons go, the 19 gun polygons on rectangle 416, 736 stay; `Runtime::actor_gun_model`, all three
  copy paths). Other actors unchanged; PC models unchanged.
- **Viewer actors in the variations (B and later):** players 3 / 4's copies are made from recorded frames, so
  `actor_dress` was never applied to them (only variation A): now each frame records, per seat with an actor picked,
  the re-pose (the woman's / man's jump down: `actor_pose`) and Rogan's coat on a body he plays (model and chest draw),
  and the copy draws the recorded frame with them (a mirrored copy too; the belly skin re-posed). Checked: 1-07
  (variation G) with `--cast -,-,sophie,rogan`: P3 jumps with Sophie's motion (`HOTD_TRACE_ACTOR_POSE`: 0x178 -> 0x190,
  rebuild off by 0.0000), P4 wears Rogan's coat. Scene states with and without a cast equal (1-06, 2-12, 4-19): draws only.
  Pictures: worktree `wt-followup\build\shots\a107\` (1-07), `c419_arcade\`, `c419_pc\` (4-19, frames 31800 / 32150),
  `cur_a3\` (1-06 arcade).

## 23. CPU teammates: researchers, the sewer slugs, open shots (worker "cpu3", 2026-10-28)

User: "they still shoot at researchers but less often ... confused with the brown slugs in the sewer ... when they
are not visible they know the general location but won't shoot until they are reasonably open and visible." Numbers:
docs/changelog/2026-10-28.md "cpu3". game/bot.cpp `teamPlay` only (the testing bot is unchanged).

- **People**: also by body type (`isHuman`: hostage_list_humans' types 14, 15, 17, 19-23, 25-27). Safety reach 2.5 x
  `tremor` + 12 (was + 6), tested where the person is and where its screen motion takes it in 5 / 10 frames
  (`hostageGap`); spots within radius + 18 of a person are never picked. A target holding a hostage (the kind 100 / 103
  person's +0x390 captor) or within 40 of one is `held`: 600 less, and fired at only with the sight within 0.6 of a
  damaging part's radius and the people a further 16 away (`held_captor` counts the pulls held back).
- **Targets**: `kind142_slug_spitter_frame` is a creature (its name has "spit"); `kind142_spit` with +0x58 bit 28 (shot,
  tumbling down), `kind142_slug_fall`, `kind141_slug_burst` are done. The hand follows the spot's own move (85 %, at
  most `speed`): crawling slugs (enemy_main type 46) were chased for ever. A switch within 2 s of the last target
  waits 0.4 of a reaction. Shots also with the bars up while shot_enable is 1 (letterbox_task's scene_state 0).
- **Visibility**: a creature up to a screen beyond the edge (depth < 130) is a known target: the sight waits at the
  edge nearest it, no shots; when it comes into view a short look (0.4-0.7 of a reaction) first. No pull at a spot
  past 238 / 182 or with an iron-ball trail / a prop nearer in front of it (`held_hidden`).
- **Kinds audit** (`HOTD_BOT_AUDIT=1`, CPU-only Chapters 1-4, 2P / 4P, Easy / Normal / Hard + the sewer route):
  targeted and shot: enemy_main (types 11, 16, 29, 46, 53, 63, 65, 71, 72, 77), the scripted guards / researcher
  attackers (enguard, ENGuardZombie*, enemy_act2 / 7 / 12 / 15 / 17), the axe / barrel / iron-ball zombies (kind32_*),
  kind3 / 5 / 7 / 65 zombies, dogs, bats and swarms, frogs (kind69), kind70 (types 18, 59, 68), Taran, molb (type 6),
  Ebitan / Ebitb (kind11 / 132 / 135 / 150), the slugs (kind141, kind142 spitters); thrown things: axes, held objects,
  kind70 knives, Moody's shots, the Hermit's spit, the Magician's fireballs, kind142 spit blobs; bosses: the Hanged Man,
  the Chariot (its hand while armoured), the Hermit, the Magician. Left alone: people (hostage_s*, kind64, kind17,
  the captives), iron-ball trails, scenery (props, padlocks, signs, items, route spots), dying / dead routines. No
  shootable kind is ignored now (`kind32_act2_hidden_frame` is a candidate but never on the screen).
- **Results** (CPU-only, player 1 idle, 50,000 frames): researcher hits 0 / 0; shots within 20 of a researcher 15 -> 0;
  Chapters 1-3 cleared 16 -> 18 of 18; Chapter 4 Normal / Hard finish, Easy alone does not (the Magician; as before).
- **Found, not changed**: with `--route 1:9=1` forcing the sewer after the hold-up researcher was rescued (Chapter 1
  event 9), kind64 state 22 never sets scene_state 1 (its camera frame was past 80 already): the bars stay up, shots
  off, event 11 waits for ever. Only seen with the forced route (the real route is 1 only when the researcher is hurt).
