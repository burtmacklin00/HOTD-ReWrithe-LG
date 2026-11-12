# Horde Mode (ours, 2026-11-03; Experimental / Proof of Concept)

The arcade game as it is (the chapters, the story, the rescues, the bosses, the routes) with many more creatures at
once, as HOTD1 Remake's Horde mode. Code: `src/game/horde.h` / `horde.cpp` / `horde_places.inc`. **In the release
builds since 2026-11-09** (user: "public build, labelled Experimental / Proof of Concept", as The Typing of the Dead 0):
CMake `HOTD_HORDE_MODE` (default ON; with `HOTD_EXPERIMENTAL_MODES` ON it is always in) sets `HOTD_HORDE`, which builds
`horde.cpp` and its `#if HOTD_HORDE` blocks; More Modes' **Horde** item has the red "Experimental / Proof of Concept" tag
and help line, and the first 6 s of a Horde game show TotD 0's "EXPERIMENTAL / PROOF OF CONCEPT" notice
(`Hud::drawTotdNote`). Local games only (online co-op starts without it: no rollback state). (Until then it lived in
`src/unreleased/`, built only with HOTD_UNRELEASED.) (The wave / arena mode that was called
Horde until 2026-11-03 is now Survival Mode: [survival.md](survival.md); its old `--horde <arena>` option names stay
development-only.)

## How to play / test

- Menu: **More Modes > Horde** asks the players ("Horde - Normal") and starts the arcade game from Chapter 1, as
  Arcade does (user 2026-11-03: "the normal arcade game with more enemies, not a chapter select"): the same start,
  chapter flow, continues, game over, name entry and rankings (the Horde table). **Horde Size: Low / Normal / High**
  below it (click: the next size; Normal by default). Chapter Select is the arcade's own (no Horde). Sonic Mode's
  preset applies as to every game. With Gameplay > Character Select on (the default), the cards come first, as
  before Arcade (fix140b, user 2026-11-10: "the more modes does not have the character select thing"; Default
  Characters: the same game as without them). `--auto-start --horde-mode` (testing) starts it as the menu's Horde.
- Command line: `--horde-mode [low|normal|high]` (Normal without a word) or `--horde-size low|normal|high`, with the
  arcade game's own options (`--chapter N`, `--players`, ...). Local games only (online co-op starts without it).
  (`--horde <arena>` / `--horde-test` are Survival Mode's old names and still start Survival.)
- High Scores: the **Horde** table, the arcade's own top 10 in its own file (`ranking_horde.bin`, with Sonic Mode
  `ranking_horde_<preset>.bin`): the game's name entry after a game writes there, never into the arcade's table.
- Headless runs print `HORDE: size=.. originals=.. copies=.. escorts=.. skipped=.. peak=.. bodies=.. waiting=.. held=..
  chain=.. multi=.. bonus=.. bad_access=.. frame=..` (waiting: the most copies waiting at once; held: appearances put off
  by the burst limit; chain / multi: the longest kill chain and multi-kill; bonus: the chains' points).
- Test trace (horde2): `HOTD_HORDE_TRACE=N` prints every Nth frame `HTRACE` (creatures counted, body slots, the
  copies waiting) and for each spawn / copy / appearance `HSPAWN` / `HCOPY` / `HAPPEAR` (delay, distance to the camera,
  distance behind its creature); `HOTD_HORDE_DUMP=F` lists the followed creatures' camera-space places at frame F. (It
  also runs in the plain game: its spawn lines and counts, nothing else.) horde3: `HCOPY` / `HAPPEAR` name the lane
  and gap, `HTYPE` the type choice, `HMOVE` a copy sent to another place, `HFREE` a copy at its trail's end, `HDROP`
  the dropped ones; `HOTD_HORDE_FOLLOW=A:B` prints the followed walkers' world place, state, delay and heading every
  10th frame from A to B. The `HORDE:` line adds `dropped=` (into the reserve), `other_type=`, `trail=` (copies on a
  trail / handed to another trail / freed at its end) and `moved=`. horde4 (2026-11-09): `pioneers=` (copies that walked
  their creature's way because no way was known, below), `HTRACE` adds `pile N` (the most followed walkers within 10
  units of one of them, among those nearer the camera than 75), `HAPPEAR` the task and `pioneer`.
- **Wall test (horde3)**: `HOTD_HORDE_WALLS=1` with `--render` (the stage's walls exist only with a view): every
  creature the spawn opcodes made (any kind, in play) and every copy is followed frame by frame; its step (last frame's
  place to this one's, at 6 and 14 units over its feet) is tested against the standing triangles the ragdolls use
  (view.cpp "the ragdolls' walls": the world's, then the frame's moving props). A crossing prints `HWALL` (original /
  copy, kind, routine, `walking` for a living walker's enemy_main / wait, `free` for a copy past its trail, the place,
  `world|prop obj:triangle`, the running totals); `HWALLS` every 3000 frames. Works in the plain game too (the
  arcade's own crossings: the reference).

## Weapons (hordepub, 2026-11-09)

Survival's weapons in Drops mode (`docs/weapons.md` "Drops"). The user after playing (2026-11-09): "Horde is alright but
needs more improvements":
- **Kept after a continue** (no longer back to the handgun) and **ammo does not switch the gun** (a weapon already held
  gets ammo in its reserve; only a new one goes in the hand): docs/weapons.md.
- **Pickups from other places** (`HordeMode::propBroken` / `giftsTick`; the `HORDE:` line's `gifts=crates/researchers/bonus`):
  - a **crate / breakable prop** broken for good (the port's new `prop_broken` hook at its last break step, where its life
    item drops; nothing in a normal game): one in 3 a weapon pickup in front of it, 6 units up (over its life item);
  - each **researcher rescued** (hostages_rescued_chapter): a weapon pickup in front of the camera at its next stop
    (the rescue's scene goes on with the shots on, the researcher walking to the camera; the camera then moves on);
  - Chapter 4's **bonus room** (event 3, the route `kind191_route_by_rescues` takes when every researcher was saved;
    `--route 4:1` in tests): three different weapons floating across the room's floor 230 frames into its play (the
    camera turns about the room without a stop), 20 s.
  The pickups are the weapons' own pickup models (the players' gun without the hand, the Magician's hand), as the kills'.
  Measured (bot, Normal, Ch1 --strict --default-route): 20 crate drops at one in 2 (now one in 3), 1 researcher's;
  Ch4 --route 4:1: 3 in the bonus room (pictures: scratch `hordepub\shots\g4s`, `g1`).
- **Upgrades** (Survival's shop perks; `Weapons::givePerk` in the shop's order: QUICK RELOAD, then BIG MAG, then MAX AMMO):
  one broken crate in 6 drops an upgrade pickup (the gold coin, Survival's QUICK RELOAD sign's model); at a chapter's
  clear each seat that played it gets one when every researcher of the chapter was saved (hostages_rescued_chapter =
  the researchers that appeared since it began) and one when it lost no life, announced in the score line's font under
  the multi-kill banner's row for 4 s of play ("NO DAMAGE" / "ALL RESEARCHERS SAVED" gold, "1P BIG MAG" white; 16
  languages). The `HORDE:` line: `upgrades=crates/saved/no damage`. Measured (bot, Normal, --lives-kept): Ch1 7 crate
  upgrades (one in 6), the bot lost lives (no award); with HOTD_CHEATS=invincible: "chapter 1 clear: researchers 1 of 8,
  no damage: 1P MAX AMMO" (picture: scratch `hordepub\shots\ann`). Saving every researcher of a chapter was not reached
  by the bot (1 of 8, 2 of 5): that award is untested in a run.

## Standing: creatures already there when a stop starts (hordepub, 2026-11-09)

The user: "some creatures should already be there instead of all arriving late: when a stop starts, part of its copies
already standing in place" (horde4's placement table stays off: `HOTD_HORDE_PLACES=1`), "a safe version: copies placed
only on walkable ground the original creatures use, never in walls". `src/game/horde_stands.inc` (`HordeMode::standAtStop`):
- **The table** (`tools/horde_stands.py`): plain-game runs (Chapters 1-4, `--strict --default-route`, arcade models,
  `HOTD_HORDE_TRACE=10 HOTD_HORDE_FOLLOW=0:999999`; `HFOLLOW` now prints y and the type): for each camera stop (`HSTOP`),
  one point per arcade walker that walked while the camera was there, the middle of its steps that are past its door
  (8 units), 30-120 units from the camera and in front of it (cos 0.6); points 15 apart, 4 a stop at most: 42 points at
  20 stops. **Kept only the points whose straight way to the camera (and 12 units either side) is clear of the stage's
  walls and that have a floor under them** (`placeCheck`, `HOTD_HORDE_WALLS=1 --render` Horde runs of Chapters 1-4: every
  point of a stop checked): 20 of 42 (Chapter 1: 7, 2: 4, 3: 5, 4: 4).
- **In play:** at a table stop (the camera's place within 12 units and look within cos 0.95 of the table's, once a
  chapter) Low / Normal / High put 1 / 2 / 3 of its points' creatures there at once, the nearest first, 6 frames apart,
  facing the camera and walking in (start sub-state 2); the walker's own type when it is one of the chapter's four, else
  one of them; counted (the script's waits wait for them) and limited as the placement table's (the burst limit, the
  cap, dropped if the camera leaves the stop first). `HSTAND` trace lines; the `HORDE:` line's `stood=`.
  `HOTD_HORDE_STANDS=0` turns it off (the before runs).
- **Measured** (bot, Normal, arcade models, 45000 frames from the chapter's start; before = `HOTD_HORDE_STANDS=0`; in
  play mean / share of time with 4+ / chapter length): Ch1 1.62 / 7.8 % / 22472 -> 1.64 / 9.7 % / 23476 (11 stood), Ch2
  1.25 / 6.6 % / 20799 -> 1.26 / 7.3 % / 20776 (6), Ch3 1.17 / 2.8 % -> 1.20 / 3.4 % (2; the bot does not finish Chapter 3
  in 45000 frames either way: the lab's iron ball), Ch4 1.09 / 5.1 % / 22050 -> 1.07 / 5.7 % / 21809 (2). Walls: **no
  new walking crossing** by a copy in Chapters 1-4 against the plain game's own (`walls.sh`); the unpruned table had one
  (Chapter 3, a stand creature's 0.4-unit step through world object 6 at 681 / 297), so the pruned one is what ships.
  Pictures (before / after): scratch `hordepub\shots\stands_before_after.png`.
- They appear on the stop's first frame (no fade): the arcade's own creatures also step in in view; a real player
  should judge it.

## Cut creatures among the copies (hordepub, 2026-11-09)

The user: "Horde has none of the cut creatures (Syndy, Baba, Debu, MrG ...): add them to Horde's copies too, where the
original area's creatures fit (each only with its own model / motions, as Survival does)". `HordeMode::pickType`: with
the arcade models (the PC files have no cut meshes), one copy in 6 (`kCutCopy`) of a creature that has a cut relative is
that cut creature, set up as Survival's (`survivalCuts`: its hit points; `survivalCutTables` / `survival_areas` at Horde's
start: the borrowed motion lists, shadows, damage rows, texture areas): Neil (28) -> **Syndy** (a tall one as he; she walks with Cyril's axe set: docs/survival.md "Syndy's walk"),
Cyril (29, walking) -> **Baba** (his), Sam (11) -> **Neozom** (Sam's parts and list) or **Disiprin** (the plain zombie
set), Bentley and his variants (33, 45, 51, 65, 72, 89) -> **Debu** (Bentley's textures and lists). It walks its lane's
trail as every copy (its own speed). MrG, Lubin and Hiru stay out (other joint counts, no stance / walk list:
docs/cut_content.md). `HCUT` trace lines; the `HORDE:` line's `cutcopies=`; `HOTD_HORDE_CUT=0` off.
Measured (bot, Normal, 45000 frames a chapter): Ch1 6 (3 Debu, 2 Neozom, 1 Disiprin), Ch2 7, Ch3 3, Ch4 2; every chapter
ends as before, bad_access=0, no new wall crossing by a copy. Pictures: scratch `hordepub\shots\cut_sheet.png`.

## Copies that come, without waiting or piling (horde4, 2026-11-09)

The user's complaints after playing (2026-11-05): creatures piled up at arm's length on Chapter 1's bridge, "Horde
does not make sense at all", copies waiting, walls. The fix that evening (eeaa785a) turned the placements below off
and let an entrance near the camera (within 150 units) send one copy at a time and 1 (Normal) / 2 (High) in all. The
test runs of 2026-11-09 (the bot, Normal, arcade models, `--strict --default-route`) found Horde **thinner than the
plain game** after it: every arcade walker of Chapter 1 steps out within 150 units of the camera (33 of 42 within 75),
so Horde was one copy a creature at most; a copy whose creature was shot before it walked off its place (most of them:
the players shoot a creature as it steps out) waited 2 s, counted, for another creature's way, then was dropped, and
the script's creature waits waited for it: **38 s of Chapter 1 with nothing to shoot** (Chapter 2: 50 s, Chapter 3:
23 s). Chapter 3's copies were also handed to the places of the stop before (an event spans several stops there) and
dropped the next frame. Now (horde.cpp `activate`):

- **A pioneer:** a copy with no way known (its creature shot at its place) and no other place of the camera's own stop
  within 60 units whose creature walked is its creature again (its type, its speed; no other type) and walks the
  record's own way, as the arcade's creature does; its steps are the place's new trail for the next copies. It walks
  its record's heading for its first 16 units (its creature did that while the camera came: the spawn routine's
  walk-in; with the camera already there, the first pioneers turned to the player at once and went through the wall
  beside Chapter 1's side passage).
- **Near entrances:** 2 / 4 / 6 copies in all (Low / Normal / High) from an entrance within 150 units, one at a time:
  the next once the last one from there is 15 units on its way (or gone), and only while fewer than 3 / 4 / 5
  creatures are within 75 units of the camera (no pile at arm's length). (A held copy no longer counts as sent.)
  **None from a place within 30 units of the camera** (the creatures that start beside you: Chapter 2's, Chapter 3's
  lab tables; a copy there is one at arm's length out of nowhere): they come with the next creatures (the reserve).
- A copy sent to another place goes only to one of the camera's stop within 60 units (not to another room).

Before -> after (mean creatures in play while shots are on, the waiting copies not; seconds with only waiting copies;
the chapter's length in frames; plain game in brackets):

| Chapter | In play | Nothing to shoot, copies waiting | Length | Copies made |
|---|---|---|---|---|
| 1 | 1.16 -> 1.72 (1.40) | 37.9 s -> 0.3 s | 21451 -> 23996 (16738) | 20 -> 127 |
| 2 | 0.82 -> 1.30 (0.96) | 50.3 s -> 0.2 s | 22172 -> 20416 (16550) | 12 -> 88 |
| 3 | 1.44 -> 1.64 (1.56) | 22.6 s -> 0.6 s | 24309 -> 26049 (22824) | 12 -> 51 |
| 4 | 0.99 -> 1.09 (1.04) | 0 -> 0 | 26922 -> 21780 (28083) | 4 -> 12 |

(Copies: the `HORDE:` line at the run's end, 45000-50000 frames from the chapter's start.) Chapter 1 High 1.82 in play,
26805 frames, 197 copies; Low 1.63, 19874, 65; Normal with the PC models 1.88, 23087. Piles (3 or more within 10 units,
near the camera): 1-16 s a chapter (the crowd at the attack places in front: `HOTD_HORDE_DUMP` / pictures: a crowd
being shot, not a stack at a door; the plain game: 0). Walls (`HOTD_HORDE_WALLS=1 --render`): no walking crossing by a
copy where no creature of the plain game crosses the same wall object in chapters 1-4 Normal, Chapter 1 Low / High and
Normal with the PC models, but one: High, Chapter 2's first stop, a copy on its creature's trail steps 0.4 units
through world object 0 at -3 / -49.6 (Normal runs: copies and the arcade's creatures cross there too). The bot's density
rises little with more copies (it kills a creature as it steps out); real players were not measured.

**The Magician's Hand in Horde** (the user's "no damage", 2026-11-05): checked in a real window on the second monitor
(`HOTD_HAND_TEST`: SDL mouse events, no focus; `HOTD_WEAPONS_FORCE=5`), Horde Normal Chapter 1, arcade and PC models:
43 pulls, 35 explosions hit creatures (PC models: 16 bolts, 11 hit, 9 kills); the misses are the 90-pixels-aside shots
at walls, a shot past the creatures (300 units) and the man at the gate (a hostage: never hurt). The Hand did no damage
before survfix3 (10:13 that day: the fireball burst 300 units away on every shot that missed a creature's spheres);
the user's build may have been older. Nothing found to fix; the user should try it again.

## Placements: where the Remake puts its extra creatures (horde4, 2026-11-05)

**Off since 2026-11-05 evening** (eeaa785a, user: "Horde does not make sense at all"): `HOTD_HORDE_PLACES=1` brings the
table back for testing.

The user (2026-11-05): "go back to the video I showed you and just copy where the Remake places its additional
creatures, and add the cut creatures for some of the new creatures". The reference: the user's HOTD Remake Horde
playthrough (`Videos\references for typing of the dead and horde`, arcade difficulty), read at 1 frame a second
(scratch `workers\horde3\remake\c1\d01-d10.png`). The Remake keeps the arcade's route and stops; at the open stops it
adds groups across the space (rows from the back of the gate's field, a group down the courtyard's path, the square
filled from the left, the middle and the right); indoors its extras come through the same doors as the arcade's (our
lanes, below). The table (`src/game/horde_places.inc`) holds them stop by stop: a stop is the camera's place and look
when it stops (camera_stopped 2), each line a creature on the floor there (world space, picked from the stop's own
picture with `HOTD_HORDE_PICK`), its delay after the stop, its type (-1: one of the chapter's walkers; a cut creature
with the arcade models). It faces the camera and walks in (start sub-state 2); it is made at the stop (counted, so the
script's waits wait for it; unseen until its moment; the burst limit; dropped if the camera leaves the stop first).
Low takes every other one. The `HORDE:` line: `placed=` / `cut=`; the trace: `HSTOPPLACE` / `HPLACE`.

Chapter 1 (our default route; the video 1:15-6:15):

| Video | Stop (camera x / z, look) | The Remake's extras | Ours (count, delay frames, kind) |
|---|---|---|---|
| 1:20-1:27 | gate field (0 / -1167, ahead) | three more across the middle while the arcade's two go for the man | 3 at 20-80, one Syndy (cut) |
| 1:37-1:42 | gate field, turned left (-0.55 / 0.84) | a row of five walking from the back of the field | 5 in a row at 10-90 |
| 1:45-1:59 | courtyard path (-0.6 / -976) | a group down the path, then four from the benches on the left | 4 at 20-95 inside the path's fence, 2 at 300-330 |
| 2:13-2:17 | the fountain's square (-2.5 / -798) | from the left and the middle | 4 at 30-120, one Disiprin (cut) |
| 2:15-2:20 | further in the square (0 / -744) | more from the sides | 3 at 30-90 |
| 2:37-2:41 | the square after the Hangedman (16.9 / -643) | five from the right and the front | 5 at 10-130, one Neozom (cut) |
| 3:00-3:04 | the mansion's steps (0 / -334) | two from the sides, one from the top | 3 at 30-90 |
| 3:58-4:02 | the long corridor upstairs (0 / 42.7) | a file from its far end | 3 at 30-150, one Debu (cut) |

The other stops (the canal, the side yard, the hall, the rooms after the corridor, where the video's player took
another branch) have their extras from the arcade's own spawn places (the lanes below), as the Remake's indoor extras
come through the arcade's doors. Picture sheet (the Remake's frame and ours at each stop): scratch
`workers\horde3\shots\sheet_c1.png`.

Chapter 2 (the video 8:00-11:45 takes the cellar / kitchen / dining hall / bar branch; our default route the entrance
hall, the paintings' corridor, the gallery, the balconies and the ruins' bridges: mostly rooms, whose extras come
through the arcade's doors, the lanes). Placed where our route opens up as the Remake's open stops do:

| Stop (camera x / z, look) | Ours |
|---|---|
| the walkway round the tower (734 / 420, +x) | 2 from its far end at 30 / 100, one Baba (cut) |
| the ruins' second bridge (1045 / 186, +x) | a file of 3 along its middle at 30-110, one Disiprin (cut) |

Left out after the wall check (`HPLACECHECK`, below, and the runs): the pillared gallery (its pillars stand between its
far side and the camera), the outer balcony (its railing's bend), the ruins' first bridge (its gates). Checked: 0
walking crossings at new places (Normal, High, Low, PC models); mean alive Normal 1.42 (1.5x the plain game's 0.96),
High 1.67; Chapter 2 ends at 25784 (Normal). The test tool: with `HOTD_HORDE_WALLS=1 --render` each placement prints
`HPLACECHECK` (its straight way to the camera's foot and 12 units either side clear, a floor under it; blocked: the
nearest clear place within 60 units).

Chapter 3 (the video 16:05-: the labs, then its sewers branch; ours the labs, the cells and the furnace). The Remake's
lab rooms (17:10, 17:20) fill from the back of the room in groups of three or four:

| Stop (camera x / z, look) | Ours |
|---|---|
| the first lab room (96.5 / 206.7, +z) | 3 from its back at 30-90, one Syndy (cut) |
| the shutter room (384 / 114, +x) | 3 from its far side at 30-90 |
| the hazard-floor hall (470 / 103) | 4 across its far side at 30-120, one Neozom (cut) |
| the furnace hall's last stand (577 / 2087, -x) | 2 where the arcade's three stand (its far side is behind the railing), one Disiprin |

Left out after the wall check: the furnace's walkway (its railing). Checked: 0 walking crossings by copies or placed
creatures at new places (Normal, High, Low, PC models); the one script creature of the cells (type 83, 27 units from
the camera at 533 / 1172) still steps 0.2-0.5 units through a wall at 540 / 1158 in some runs (its own walk to another
attack place). Mean alive Normal 2.52 (1.6x the plain game's 1.56), High 3.59 (2.3x); Chapter 3 ends at 32529
(Normal).

Chapter 4 (the video 24:00-24:20 before the boss rush: the cave, then a hall filling with a crowd; ours the cave, the
pillar's hall and the long corridor of doors):

| Stop (camera x / z, look) | Ours |
|---|---|
| the cave's first stop (5 / 121.5) | 2 from its far side at 40 / 80, one Syndy (cut) |
| the long corridor's four stops (-126 / 1407 .. 1720, +z) | one more at each from its far end beside the middle's lamps, two of them cut creatures (Neozom, Debu) |

Left out after the wall check: the pillar's hall (its railings ring the floor in front). Checked: 0 walking crossings
at new places (Normal, High, Low, PC models); mean alive Normal 1.82 (1.8x the plain game's 1.04), High 1.96; Chapter
4 ends at 32672 (Normal), the ending at 39556. (The cave's two come only when the camera stays there long enough: in the
test runs it moves on first.)

Not mapped: the Remake video's other branches (Chapter 1's canal / basement, Chapter 2's cellar / kitchen / bar,
Chapter 3's sewers), which our default route does not take: there the lanes bring the extras.

Checked (Chapter 1, `--render` wall test, autofire): Normal 28 placed, Low 17, High 28; walking crossings by Horde's
creatures at places the plain game's creatures never cross: 0 in Normal, High, Low and Normal with the PC models (the
first table had three of the path's group behind its fence and two of the square's behind a wall: moved). Mean alive
Normal 3.03 (2.2x the plain game's 1.40), High 3.78 (2.7x), Low 1.96; Chapter 1 ends at 28341 (Normal).

## How it works

The spawn opcodes ENTB / ENTB3 / ENTBE tell the runtime of each object they make (`Game::evt_spawned`, before it first
runs). For a **standard walker** (record kind 0, `kind000_enemy_spawn`, or kind 9, `kind009_enemy_spawn_b`), outside
a cut-scene (shots on, no letterbox), Horde sends 3 / 6 / 10 copies (Low / Normal / High). horde3 (2026-11-05, the user
played horde2: "where the creatures come from does not make sense and they walk through walls and objects"; horde2's
copies appeared 40+ units behind their creature, a few units to a side, at one of the event's places, and walked from
there as if they stood at their record's place):

- **Where from: exactly its creature's place.** A copy is its creature's spawn record again (the work area as the
  opcode set it: the place, the heading, the record delay, the action, the start sub-state): the door, the corridor's
  end, the corner or window the level design starts that creature at, no offset. Each script walker is a *lane*:
  its copies queue there, the next one 0.5-1 s after the last one from that place (a place within 110 units of the
  camera: 0.7-1.5 s) and only once that one has walked 6 units off it (or is gone).
- **Its creature's way: the trail.** The standard walker walks along its heading for its delay, then turns to its
  player while walking (`enemy_ai`); the turn while walking is what takes it round the door post. A copy with another
  attack place, speed or wait would cut the corner (Chapter 4's start: 24 crossings of one wall). So each lane records
  where its original walked (a point a unit apart, until it dies or attacks: the *trail*), and a copy walks the trail:
  kept within 0.3 units of it, its heading along it, to its end; then the game's own walk to its player from where
  its creature was. A trail ending where its creature was shot hands the copy on to another of the event's trails
  that passes there (a creature from the same door) when one goes on. A copy comes only when its lane's trail is 8
  points long (or its creature still walks it); a lane whose creature was shot before it walked sends its copies from
  the nearest place of the event whose creature did (else they wait up to 2 s, then go to the reserve, below).
- **Standing, the game's way.** A walker within 70 units of its player with no front attack place stands (state 2)
  and turns to its player; a copy does not stand on its trail (state 3: it walks on along it, and at 40 units waits
  for a place as the game's walkers do, state 4), and while one stands at its place its heading stays the record's.
  A place still taken after 0.75 s: the copy comes from another free place of the event.
- **Not clones.** 70 % of the time a copy is another type of the same set-up (the chapter's walkers the script has
  sent: spawn routine, action, start sub-state, weight; a walk speed within 0.75-1.33x of its creature's, measured
  from the walkers' steps), so every model / motion it takes is loaded and valid; its record delay is scaled to its
  speed (its delay walk ends where its creature's does). The animation phase is the spawn routine's own.
- **Paced.** At most 4 creatures (the script's own included) appear within half a second, at most 3 of them within 75
  units of the camera; the rest a few frames later.
- **Waiting unseen, counted.** Two copies a place at a time are made (a task of the scheduler's ret routine) and
  counted in `enemy_count` / `enemies_alive`, so the script's creature waits (WAIT_ENNUM / WAIT_ENALV) wait for them;
  the rest are the place's credit, made as those come (uncounted: the waits never wait for copies many seconds
  away). The camera leaving the stop the lane's creatures walk to (25 units: it sways a little at a stop), a change
  of event or chapter, a cut-scene or the end of play drops the waiting ones and the credit: up to 6 / 12 / 18 of
  them come with the next creatures (the *reserve*).
- **Kinds 32 / 70.** The kit zombies (kind 32: the axe throwers, drum carriers) and the agile ones (kind 70: the
  monkeys, kage, ...) are not copied themselves: their records drive event flags (`kind32_event_flags`, kind 70's
  flags 3 / 4 / 5 / 6 / 8), `route_choice`, the agile ones' shared slots (`kind70_slot_state`) and attack turn; a copy
  would set them twice or early. Each gets 1 / 2 / 3 standard walkers as escorts, queued at one of the event's walker
  places (a far one in front of the camera; none when the event has had no walker yet).

Every other kind is left as the script has it: the bosses (their own kinds), hostages and their scripted attackers
(kinds 96-101 ..), the scene and event creatures (kind 162 ...), props.

**Crowd feedback (horde2, Horde only; game/hud.cpp `Hud::drawHorde`, in the score line's font as Survival's and the
weapons' lines, survfix's `scoreText`):** a kill (`Game::weapon_kill`) pops its points where the creature was ("+100"
white, with a multiplier "+300" gold and twice the size; rising, gone in 0.8 s); each seat's chain (the next kill
within 2.5 s, no life lost) shows in its points row (unused in Horde: players 1 / 2 under their weapon line in the
bottom corners, players 3 / 4 under their bands in the top corners) as "12 KILLS X2" (the multiplier from 5 kills: x3
at 10, x4 at 15, x5 at 20); kills within 0.4 s of each other make "N KILL" at the top centre (over the sky / ceiling,
never the middle; with players 3 / 4 a row lower, between their bands). A kill is worth 100 x the multiplier more
(`add_score`: Horde's own table only).

**Creatures that never attack (survfix, game/engage.h):** every copy is in `made` once it appears (`copy` true), so
the never-attack rule sends it away as for any copy; a copy still waiting unseen (its routine the scheduler's ret) is
never in `made` and never checked. `retired=` in the line.

**Limits:** the extra body slots of Survival Mode (`Game::extraBodySlots`: slots 16.. in a table outside the original
map, 0x7E1000 here, the three skin buffers grown at each chapter's heap reset): 26 more than the original's 14. A copy
is not made when 24 creatures would be counted (`enemy_count`, the waiting copies included) or fewer than 4 body slots
would be left for the script's own creatures (`skipped` in the line). The other tables (attack places 14, attackers
13, depth list 256, shot candidates 256, the heap) as docs/survival.md "Limits": the extra creatures queue for attack
places as the original's 15th does.

## Checked (horde3, 2026-11-05: `--render --size 320x240`, autofire, `--strict --default-route --lives-kept 1`, `HOTD_HORDE_WALLS=1 HOTD_HORDE_TRACE=1`; each chapter from its start, to its end; Chapter 3 with `HOTD_AIM_SKIP=47C70`)

**Walls.** Walking crossings (a living walker's step through a standing wall triangle) in each chapter, horde2 (before)
and horde3 (after). "Arcade places": within 4 units of a place where a creature of the plain game crosses the same
wall object (the plain game's own: doorways the arcade's walkers walk through: Chapter 1's 5).

| Chapter | Plain (originals) | Normal before: copies (creatures) | Normal after | High before | High after |
|---|---|---|---|---|---|
| 1 | 5 | 68 (38), 65 new | 15 (14), all at arcade places | 116 (55), 108 new | 58 (47), all at arcade places |
| 2 | 1 | 46 (28), 45 new | 24 (12), all at arcade places | 88 (44), 87 new | 37 (20), all at arcade places |
| 3 | 1 | 73 (39), 72 new | 0 | 90 (56), 90 new | 0 |
| 4 | 0 | 15 (9), 15 new | 0 | 43 (18), 43 new | 0 |

New crossings by copies: 0 in all 8 runs (horde2: 653 in 8 runs). The PC models' runs: the same numbers. One script
creature (not a copy) of Chapter 3 (type 83, spawned 27 units from the camera at 533 / 1172) steps 0.2-0.5 units
through a wall at 540 / 1158 in Horde Normal (and High with the PC models): its own AI walking to another attack place
(the crowd took the front ones); the plain game's never goes there.

**Density** (mean creatures alive while shots are on, the waiting copies not; x the plain game) and **chapter
lengths** (frames):

| Chapter | Plain | Normal before / after | High before / after |
|---|---|---|---|
| 1 | 1.40, 16738 | 3.00 (2.1x), 23100 / 2.38 (1.7x), 26046 | 4.26 (3.0x), 24707 / 3.47 (2.5x), 34716 |
| 2 | 0.96, 16550 | 1.75 (1.8x), 20793 / 1.34 (1.4x), 24752 | 2.57 (2.7x), 22725 / 1.69 (1.8x), 30072 |
| 3 | 1.56, 22824 | 3.63 (2.3x), 28419 / 2.99 (1.9x), 29064 | 4.66 (3.0x), 31119 / 3.58 (2.3x), 40029 |
| 4 | 1.04, 28083 | 1.38 (1.3x), 34519 / 2.12 (2.0x), 24149 | 1.76 (1.7x), 34382 / 2.37 (2.3x), 23443 |

Pop-ins (5+ creatures appearing within 0.5 s): 0 before and after (the burst limit). Copies of another type: 19-37 %
(Chapter 4's walkers have no other type of their set-up). The density is lower than horde2's: a copy comes only from
its creature's place, after the last one walked off it, on its creature's way, and the camera leaving a stop drops the
copies still waiting (they come with the next creatures, the reserve); Chapter 2 sends few standard walkers.

Whole runs (horde3 build): Horde Normal from Chapter 1 to the ending (114227), Chapter 3 Normal to the ending (60430),
High from Chapter 1 (chapters 1-3 end by 97464 of 120000 frames) and from Chapter 3 to the ending (72027), arcade and PC
models alike; `bad_access=0`, `retired=0` in every run.

Recorded window clips (`HOTD_REC`, second monitor, muted, Horde Normal, the test bot): Chapter 1 outdoors (frames
1800-3100), Chapter 1's hall (6950-8250) and Chapter 2's corridor (3850-4600), before and after.

## Checked (horde2, headless, autofire, `--strict --default-route --lives-kept 1`)

Chapter 1 from the trace (`HOTD_HORDE_TRACE=1`; mean creatures counted while shots are on, the waiting copies not;
pop-ins: 5 or more more creatures within 0.5 s):

| Run | Chapter 1 ends | Mean alive | >= 4 alive | Pop-ins | Notes |
|---|---|---|---|---|---|
| Plain | 16738 | 1.40 | 4 % | 0 | |
| Horde Low, before / after | 18518 / 20326 | 1.80 / 2.00 | 14 / 15 % | 4 / 0 | |
| Horde Normal, before / after | 19307 / 22949 | 2.36 / 2.84 | 19 / 30 % | 8 / 0 | copies appear 71 / 140 / 227 frames after (p10 / 50 / 90), 27 / 46 / 87 units behind their creature |
| Horde High, before / after | 20625 / 25050 | 3.08 / 4.17 | 33 / 41 % | 8 / 0 | |

Faster bot (`--autofire-every 6`): plain 15224, Normal before 17691, after 20032 (mean 2.67). Other type than its
creature: 62-66 % of the copies.

Whole runs (horde2 build, 90000 frames at most):

| Run | Copies / escorts (originals) | Peak alive | Result |
|---|---|---|---|
| Chapter 1, Normal | 328 / 24 (76) | 15 | chapter 1 ended 22949, chapter 2 43685, chapter 3 72069 |
| Chapter 2, Normal | 210 / 10 (50) | 15 | chapter 2 ended 21695, chapter 3 52404, chapter 4 75076, the ending is over 81960 |
| Chapter 4, Normal | 30 / 2 (7) | 10 | chapter 4 ended 26238, the ending is over 33122 |
| Chapters 1 / 2 / 4, High | 468 / 315 / 45 | 21 / 21 / 14 | ch 1: chapters 1-3 end (25050, 48224, 85269); ch 2: 22939, 66669; ch 4: 34217, the ending is over 41101 |
| Chapter 1, Low, 2 players | 172 / 12 (80) | 9 | chapters 1-3 end (20265, 39635, 71529) |
| Chapter 1, Normal, PC models | 158 / 14 | 15 | chapter 1 ended 22949 (as the arcade models) |

After merging main (survfix, totd5): Normal from Chapter 2 to the ending (83307), Chapter 4 Normal / High / PC models
/ 2 players to the ending (33701 / 41101 / 33701 / 39025), Chapter 1 2 players and 4 players end; `retired=0` in every
run. bad_access=0 in every run. Plain game unchanged: the input test 2613 checks, 0 failed; Chapter 1 ends at 16350 (arcade
and PC models); `--chapter 4 --strict`: the ending is over at 34967.

## Open

- horde4 (2026-11-09): with the bot, Normal is only 1.2-1.4x the plain game's creatures in play (Chapter 4: 1.05x,
  it has 7 walkers): the arcade's walkers step out near the camera and the bot shoots each as it does, so the copies,
  one at a time from such a door, mostly make the stops longer. More at once needs more from the same door at once,
  which is what the user saw piled up on the bridge: the next step is the user's feel pass in a real game (Normal /
  High), then maybe kNearCrowd / kNearCopies (horde.cpp) up, or the Remake placements back where the stop is open.
- horde4: one bot run of an intermediate build (Normal from Chapter 1) stopped at Chapter 4's first WAIT_ITEM (the
  bot stopped finding the Hangedman's rematch target after 77055); the final build's Normal and High runs from Chapter
  1 reach the ending (102316 / 106487; main's High: 104868). Not reproduced; watch the soak.
- The weapons' explosion is one slot (`Game::weaponBlast`): two Hand bursts landing in the same frame (two players)
  keep only the second one's hits (docs/weapons.md; Survival too).
- Density vs length: with the test bot Normal is 2.0x the plain game's creatures alive at once (the brief's 3x is
  High: 3.0x) and Chapter 1 is 37 % longer (High 50 %): every extra creature takes the bot's shots and the copies come
  after their creature has walked on. Real players with Horde's weapons kill faster than the bot (it fires every 15
  frames); not measured with people.
- `--chapter 3` stops at Chapter 3's first creature wait (frame 838; Horde and, by chance, the plain game: the same
  on main): the test bot, not the game. It shoots the iron-ball swinger's ball (kind 32 kit 4, `iron_ball_follow`
  0x47C70), which no shot kills and which, thrown at the camera, stays the nearest target, for ever, never its
  swinger. Runs from Chapter 3 pass with `HOTD_AIM_SKIP=47C70` (Horde Normal: chapter 3 ended 28569, the ending is over
  58889; High: 32925, 64892). The bot is left as it is: leaving the balls alone (always, or only near the camera) moves the
  Chapter 4 reference run (the ending at 38269 / 29697 instead of 34967). The long soak's "Horde Chapter 3 stalls
  ~33000 frames at 0xF4094" (checks1) is this (fix5 2026-11-05: Horde has nothing waiting there; with the skip no
  stall, chapter 3 ended 28209); `tools\soak_long.ps1` sets `HOTD_AIM_SKIP=47C70` for its Chapter 3 Horde runs.
- Online co-op: not with Horde (its state is the game's own, but the menu's Horde starts a local game).
- 4P chains: placed in the seats' points rows (as Survival's points). Seen (qa1, a real 4:3 window, `--players 4
  --seat-bot 2:3 --seat-bot 3:3 --seat-bot 4:3 --autofire`): "2 KILLS" under players 3 / 4's weapon line; the computer
  teammates rarely chain (14 of 25 kills were theirs in 7000 frames, two chains of 2). `HOTD_HORDE_TRACE` prints `HKILL
  <frame> seat <s> chain <n> x<mult>` per kill.

## The first idea (moved from ROADMAP.md, 2026-11-05)

### Future: Horde Mode (user idea, 2026-10-21; next week or a later update, not now)

Like the Remake's Horde mode, as a toggle similar to Sonic Mode (Off / On, maybe intensity presets), usable in Arcade,
Chapter Select, PC Mode and co-op: many more enemies per wave / scene than the original. Ideas to look into:
- How the spawn system works (the event scripts' spawn records, attack slots, the kinds' limits) and how far the
  object / enemy tables can grow without breaking the 1:1 logic (Horde Mode is a mode: the arcade default stays exact).
- **Bring in the cut creatures** (MrG, Lubin, Disiprin, Syndy, Debu, Baba, Neozom, Hiru; textures found in the arcade
  ROM, docs/cut_content.md): give them motions / behaviour borrowed from similar used kinds so they can appear in hordes.
- Scoring / its own top 10 (like Sonic Mode's), balance for 1-4 players, performance on low-end PCs.
Ordered with Typing of the Dead 0 after the release work.
