# Weapons (ours, 2026-11-04)

Survival Mode's guns (the user's plan after v1.2.0: new weapons, a switch "with the mouse scroll wheel (and pad
shoulder buttons)", the Magician's Hand instead of a grenade launcher). Code: `src/game/weapons.h` / `weapons.cpp`; the logic's hooks in `src/port/shooting.cpp`
(`weapon_extra_shots`, `weapon_blast_hit`, `weapon_damage`), `damage.cpp` (`enemy_part_hits`), `hud_state.cpp`
(`hud_ammo_draw`); the numbers in `data/weapons.ini`. Normal play: `Weapons::mode` Off and `Game::weaponHooks` false,
none of it runs (the input test, the chapters' end frames unchanged).

## The weapons

(survhand, 2026-11-10, players: "I thought Magician's balls were powerful but they made me lose in Survival mode": 8000 points bought 12 shots, a 2 s reload in the middle, gone once used up, and a light gun's auto best fired it only after every other weapon. The balance runs, `HOTD_SURVIVAL_BUYER=hand|smg|dual|shotgun`, 18 rounds, 8 seeds, lives lost a run (lives kept) in the courtyard / arena 2 / arena 3: the old Hand 90 / 97 / 135, the SMG 74 / 94 / 133, the Double Pistols 47 / 63 / 82, the shotgun 65 / 95 / 108, the default buyer 31 / 48 / 51; the new Hand 25 / 39 / 36, its late rounds still costing lives.)

| | Magazine | Reserve | Reload | Fire | Damage | Special | Survival price | Drop weight |
|---|---|---|---|---|---|---|---|---|
| Handgun | 6 | never runs out | 36 frames (`Weapons::Drops`: at once, the arcade's) | a pull | 100 % | the game's own | - | - |
| SMG | 30 | 4 magazines | 72 | held: every 5 frames; pads / guns: bursts of 3 | 100 % | | 1000 | 4 |
| Assault Rifle | 30 | 4 | 84 | held: every 6 frames; bursts of 4 | 140 % | | 2500 | 3 |
| Double Pistols | 16 | 5 | 60 | a pull | 200 % | a second gun: a second ray 14 units right of the aim, its own flash (both on one creature: one hit, hence the 200 %) | 1500 | 3 |
| Shotgun | 6 | 5 | 90 | a pull | 130 % | 6 pellets (rays) up to 24 units round the aim, hitting only within 170 units | 3000 | 3 |
| Magician's Hand | 6 | 5 | 75 | a pull | 550 % | the Magician's own fireball flies along the aim to the first creature, item or wall / ground it meets and explodes: every creature within 50 takes a hit (550 % near, 40 % at the edge), items in it are shot; used up it stays held, empty (`keep_empty`), until Ammo / Max Ammo fill it | 8000 | 1 |

Damage is part_hit's own (the part's damage table, the difficulty bonus) times the percent, per seat
(`Game::weapon_damage`, after the 4P scaling). A weapon whose magazine and reserve are empty is gone: back to the best
weapon held (the handgun at last); the Magician's Hand (`keep_empty = 1`) stays held, empty. The light guns' auto best (`auto_rank`): the Hand 6, the rifle 5, the SMG 4, the shotgun 3, the double pistols 2. Survival's perks: Quick Reload halves every reload, Big Mag makes every magazine
x 1.5.

**How the shot works.** The game's own shot stays: the input's trigger fires `player_shoot` (one round, the ray, the
flash, the gunshot cue). For each seat the runtime sets `Game::weaponShot` before its frame: the extra rays (offsets
from the aim), whether they flash, their range, the explosion's radius and the damage percent. `shot_resolve`, after
the seat's own shot, runs `weapon_extra_shots`: each extra ray is built (`shot_ray_setup`), every depth-list object
tested again (no scene quads), the nearest within range hit as `shot_hit_nearest` hits (the same flags: blood, damage,
points as any shot); the double pistols' second gun also gets `shot_effect` at its aim. The Magician's Hand (survfix, user 2026-11-04: "it shoots the Magician's own projectiles" that explode on impact):
`projectile = 1` (`WeaponShot::projectile`) makes the shot hit nothing itself; `shot_resolve` takes what its aim ray
meets first as the target (`weapon_aim_target`, survfix3): the nearer of the shot test's nearest hit (creatures, props,
scene items) and **the stage** (`weapon_world_ray` -> `Weapons::worldRay`: the view's triangles drawn this frame, the
ragdolls' walls and ground; headless: the floor height of the creature nearest the ray); neither within 300 units:
there. (survfix3, user 2026-11-05: the Hand did no damage: the shot test holds no walls or ground, so every
shot that missed a creature's small spheres, and every shot at the ground or a wall near one, flew 300 units on and
burst out of everyone's reach.) The target, and `weapon_extra_shots` launches a bolt
(`Game::WeaponBolt`, world space) from 30 units ahead of the camera along the aim to it, 24 units a frame (`kWeaponBoltSpeed`, at most 12 frames). In flight
it is the Magician's fireball: `magician_fireball`'s looping models 0x1CAB..0x1CB9 (a model a frame, growing to his
1.75; textures from his room's area 12 as his hand's), drawn by the weapons' overlay. On arrival
(`weapon_bolts_update`, each frame in `shot_resolve`) the explosion (`weapon_blast_at`): the creatures within the
radius (45) take the seat's hit in their own `enemy_part_hits` (`weapon_blast_hit`, this frame or the next; the
shot's own percent (kept in the bolt) at full within 30 % of the radius of the part's sphere, falling to 40 % at the
radius (`blastPct` per seat, used by `weapon_damage`; up to 8 explosions at once in `weaponBlast[]`, summed per seat on a creature); an item the bolt flew to (a prop, a crate, a scene item) is hit on
arrival as `shot_hit_nearest` hits it, and every other shootable object without a body within the radius gets the
seat's hit flags (`weapon_blast_items`: they break / drop / are shot down as by a shot); once a
creature, at its part nearest the point that takes damage: never the body's root, which `part_hit` never reaches, nor
a held weapon / armour, classes 16 / 17 (survfix2: a creature right in front of the camera had its root nearest and
never died); the full 350 %, no falloff), five of the game's hit flashes (`shot_flash`,
models 0x74..0x88, 3-4.5x their size), the fireball's own burst (`magician_fireball_frame`: the same model 1.5x,
growing, 14 frames) and its burst cue 0xA01464. The test line counts `blasts=made/hit/hit 2+ hits=creatures`
(a 3000-frame round-8 run with the Hand forced: 12 explosions, 11 hit, 6 hit two or more, 21 creatures). PC models:
the fireball is not drawn (the view's actor areas are arcade only); the flashes and the damage are the same.

**Sounds** (`Weapons::cue`, sound_cue's first look): the gunshot cues 0xA01208 / 0xA01207 of a seat that fired with a
weapon other than the handgun, and the reload cue 0xA01200, play the weapon's own: `data/weapons/<fire_sound>.wav`
when the user put one there (the SMG's also in `data/survival/`), else the stand-in: the PC's cue 30 (the handgun's
shot) sped up / slowed down and cut (`fire_standin_*`), the reload (cue 28) at `reload_standin_speed`; the Magician's
Hand the game's own Magician's projectile (0xA01463, magician_fireball's throw; the PC's cue 249).

**Models** (the game's own only; `HOTD_DUMP_MODEL_NAMES=<file>` lists a stage's models): **no weapon is drawn in a
hand outside VR** (survfix, user 2026-11-04: the arcade shows no gun on the screen: the light gun is the player's; the
players' gun hand 0x719 `ter00a` only appears in the cut-scenes). VR has its own hands (vr_scene.cpp). The shop's and
the dropped pickups are the guns without the hand (`kGunOnlyKey` 0xF1000719: 0x719 without the polygons on the empty hand
0x36B's texture rectangles, arcade; with PC models 0x719 without its page-560 corner u < 40, v >= 225, the hand's
skin and cuff: pc_loader.cpp), the Hand the Magician's right hand 0x1C6C `boss4_ter01` (the weapon is his hand; survfix3: turned about its middle,
its origin is the wrist, and half round so its fingertips point where the guns' muzzles point at every moment), its
textures in his room's area 12 (drawn from Chapter 4's stage by the view's `actorAreas`). Every floating model (the
shop's nine, the dropped pickups) turns on one clock: the same angle and bob at the same frame (`Weapons::overlay`).
`model =` in weapons.ini is kept for the files but not drawn.

## Switching

- The mouse wheel (that mouse's player), the number keys 1-6 (player 1; a key bound to a player's action keeps it).
- A pad's shoulder buttons L1 / R1 and the d-pad's left / right: previous / next (in these modes only; the triggers
  keep firing / reloading).
- Light guns: `auto_best_light_gun = 1` in `data/weapons.ini` ([options]): at each pull the best weapon with ammo
  (`auto_rank`).
- In co-op each seat has its own weapons, magazines and reserves.
- No delay: the weapon's own magazine comes into the hand (the one left in the old gun stays there).

## Survival

- Weapons are **kept after a round** (the user's answer 2026-11-04); Max Ammo (after a special round) fills every
  weapon held; AMMO in the shop fills the reserves.
- **The shop:** nine items in three rows (SMG, Assault Rifle, Double Pistols / Shotgun, Magician's Hand, Ammo / Life,
  Quick Reload, Big Mag), each name and price in the game's font and **an in-game model turning over it**: the guns
  the players' handgun without the hand (`kGunOnlyKey`), the Hand the Magician's hand, Ammo and Quick Reload the
  game's gold coin 0x3B5 `kinka` (the props' hidden bonus), Life the game's life item 0x312 `item_life` (the props'
  drop), Big Mag the gun. A shot at the name or the model buys it.
- **The HUD** (survfix, user 2026-11-04: the sprite letters were "horrible and unreadable"; "the same font as the
  debug score"): every line in the font of the game's score line (Gameplay > Score Display, `SCORE:00000`:
  hud_lives_draw's text through text_printf, the debug font sprite_init puts in character slot 0: the arcade's font ROM
  0x2EF3870, palette 0x168A0, 8 x 8 cells, white with a dark edge; with PC models too: the PC's text sprites 0x19D + c
  - 0x20 are 1.6 times the cell and ran into each other over the shop, `HOTD_SURVIVAL_PC_FONT=1` shows them), each glyph with a one-pixel shadow, integer scales only (1 or 2 cells). Laid out
  on the arcade's 64 x 48 cell grid as its own HUD: **one title block at the top centre for 1-4 players** (survfix2,
  user 2026-11-04: with four players the round line fell between the RELOAD prompts and the shop's title sat
  mid-screen): the round two cells high from row 1 (in the break the next round and its countdown, `ROUND n IN s`; at
  the end `REACHED ROUND n`), under it one cell high the status (`LEFT k`; in the shop `SHOOT AN ITEM TO BUY`, or the
  moment's `1P BOUGHT SMG` in the seat's colour / `NOT ENOUGH POINTS` in red; at the end `POINTS p   KILLS k` in gold;
  hidden over the end's route map, its labels are there). Rows 1-4 lie between players 3 / 4's bullets (row 1, to
  cell 12 / from 0x33) and their torches (row 5): the widest title fits in between at 4:3. Per player the weapon and `rounds/reserve` (red when a quarter or less is left;
  RELOADING blinking as the game's RELOAD!) and `PTS:00000` in gold in the score line's own row 0x24 above the
  game's bullets (a line higher with Score Display on), past 6 rounds `X N` beside the one bullet; players 3 / 4 under
  their bands at the top corners (rows 7.9 / 9: clear of their torches and of their RELOAD prompt from row 10.3; their
  notice takes the weapon's line). An empty 3P / 4P seat's PRESS START BUTTON sits on its band's row (row 2 ran into
  the title). **The shop** (survfix2: the Hand's model covered its name): each sign a stack, nothing overlapping
  (`SurvivalMode::kSigns`, `kModelBox` ..): the model in a 28-unit box (each scaled to it and lifted to its middle:
  the gun 26 units a unit of scale, its middle 5.5 over its origin; the life item 18.4; the coin 13; the Hand 25), 5
  units of air, the name (a cell, white), the price (two cells, **gold as the points**), with two players or more the
  seats that can buy it (`1P 2P ..`, each in its colour); **grey when nobody can buy it** (one player: grey = you
  cannot). Columns x -135 / 0 / 135, rows 72 units apart under the title block and players 3 / 4's points, over
  players 1 / 2's at 4:3; a shot anywhere on the stack buys. The moment's banner (a round's start in red, cleared in
  gold, the arena's name in white) two cells high in rows 0x12-0x13: the gap between players 3 / 4's RELOAD prompts
  and players 1 / 2's, the same place for 1-4 players. Checked at 640 x 480 4:3, 1080p and 4K, 1-4 players.
- Self-test: `hotd --survival-test N --survival-weapons-wave 1 --survival-cycle-weapons` (every weapon from round 1,
  each seat switching to the next once the one in its hand has reloaded: each fires, reloads and switches); its line
  ends `kept=ok|FAIL` (every weapon held at a round's clear still held at the next start) and
  `weapons(shots/reloads/kills)=pistol=..,smg=..,rifle=..,dual=..,shotgun=..,hand=.. switches=..`.

## Drops (`Weapons::Drops`)

The weapons' second mode (Horde Mode: docs/horde.md "Weapons"):
- A killed creature drops a **pickup** one time in 9 (at most 6 lying): a weapon by its `drop_weight`, its model turning
  where the creature was (about 30 pixels across at any distance), for 7 s (blinking at the end). A shot at it takes it
  (the shot is not fired; the life item's pick-up sound 0xA01351): a new weapon goes into the hand full; one already
  held gets **ammo only** (hordepub, user 2026-11-09: "picking up ammo must not switch you to that gun"): half its full
  reserve (a magazine at least) added to its reserve, the gun in the hand stays (it used to switch from the handgun).
- Weapons and ammo carry over to the next chapter and **through a continue** (hordepub, user 2026-11-09: the seat is no
  longer put back to the handgun when it is out of lives; its weapon in the hand gets its own magazine back when it
  plays again: `weapons: 1P plays again with the rifle (2 weapons kept)`).
- The handgun reloads at once as in the arcade; the others take their reload time.
- **Bosses** (hordepub, user 2026-11-09: "the Magician's Hand does not damage the Magician"): the Hand's explosions hit
  creatures in `enemy_part_hits`; the bosses with their own hit test (the Magician's `magician_hit`, the Chariot without
  its armour: `chariot_part_damage`) now take an explosion near them as a shot at their nearest damageable part
  (`weapon_blast_boss`: the Magician's parts 4 / 6 / 11 / 13, any part while he guards so it sparks; the Chariot's parts
  with a plate left), with their own damage. The Hangedman and the Hermit use `enemy_part_hits` (hit before too).

## Testing helpers

- `HOTD_WEAPONS_FORCE=<0-5>`: that weapon in every hand that holds it (pictures of each).
- `HOTD_WEAPONS_LEAVE=1`: the autofire leaves the dropped pickups lying (pictures).
- `HOTD_WEAPONS_TRACE=1`: each pickup's camera-space place as drawn.
- `HOTD_WEAPONS_DROP=<1-5>`: every kill drops that weapon (`Weapons::Drops`; pictures of a pickup).
- `HOTD_BLAST_TRACE=1`: each Hand bolt's target (a creature / an item / the stage / nothing), each explosion, the
  objects near it, each creature it hits (distance, percent) and kills. Test lines: `blasts=made/hit/hit 2+
  hits=creatures stage=on the stage items=items hit blastkills=kills`.
- `HOTD_HAND_TEST=<frames>` (a window run, e.g. with HOTD_REC on the second monitor): player 1 plays through SDL's
  event queue as a mouse (motion, left button; right button to reload), every <frames> a pull at the nearest creature
  on the screen, in turn at it, at the ground in front of it, and 90 screen units beside it (`HANDTEST` lines).

## Open

- Online co-op: Survival is a local game (its state is not in the rollback snapshot).
- Players 3 / 4 (4P): their weapons work; their HUD shows the game's bullets only (no name / "x N" line yet).
- No rifle / shotgun model exists in the game: they are drawn as the handgun; a user's model could come later.
- VR dual pistols: see the weapons worker's STATUS.md (what can be reused).
