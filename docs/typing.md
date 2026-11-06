# The Typing of the Dead 0

ReWrithe's own mode (worker totd0, 2026-11-03; reworked by totd1 the same day after the user's first play: "decent
but pretty buggy", and by totd2 after the second: bats / projectiles, kill grades, rescues, bosses, word lengths): The House of the Dead played by typing, after SEGA's The Typing of the Dead (2000, on HOTD2),
with a few ideas from Typing of the Dead 3 and Typing of the Dead: Overkill (2013). The target is TotD 2000's feel.
Code: `src/game/typing.h` / `typing.cpp` (the layer), `src/game/typing_hud.*` (its HUD), data: `data/typing/`.

## How it works

The arcade logic runs as it is; the layer turns typed letters into player 1's light-gun input (`Runtime::gun[0]`),
between ticks, from the game's own state, so it is deterministic and normal play is untouched when the mode is off.
Three small hooks in `src/port` (all null outside a Typing of the Dead 0 tick, so the arcade / PC Mode / the proofs
never see them): `port::gAttackHold` (an attack waits) and `port::gHitHold` (a hit waits) and `port::gPreyHold` (a captor's bite on a researcher waits), see "Attacks" and
"Rescues".

| What | Rule | From |
|---|---|---|
| Prompts | every enemy the CPU teammates would take as a target (`Bot::targetKind` / `targetThreat`: on the screen, damageable, in the fight) gets a prompt in a box over it | all three |
| Word mix | most enemies one **word** (from all of `word_lists`, by length); tougher ones (hit points >= `tough_hp`, 200, not near) a **short phrase** (`tough_letters`); the toughest (>= `two_prompts_hp`, 400, not near) two prompts in a row; bosses **sentences** (`boss_lists`, `boss_letters`); bats, Murrers, frogs, dogs (`small_names`) **1-3 letters** (`small_letters`), thrown axes / barrels / knives 1-2 (`projectile_letters`) | TotD (Overkill: two prompts) |
| Length by threat (totd2) | a word's letters by threat band: **near** (spawned within `near_depth` 30 of the camera, or at you within `near_seconds` 2 at the speed it comes closer: one jumping out next to you), **far** (beyond `far_depth` 70, later than `far_seconds` 5, no attack place taken: a slow walker far off), **mid** the rest; `word_letters_near / mid / far` per difficulty (Normal 3-4 / 4-5 / 5-7). The depth: the camera-space z at first sight; the speed: the CPU teammates' tracking of the enemy (`Bot::approach`, 8 frames before the prompt). Captors and enemies in a rescue scene (a researcher on the screen) take the near band's shortest | user 2026-11-03 |
| Difficulty | Options > Difficulty picks the lengths and lists (one value per difficulty). Very Easy: 3-5 letter words, 14-20 letter boss sentences; Normal: 3-7 letter words, 7-11 letter phrases, 16-25 letter sentences; Very Hard: 4-10 letter words, phrases and sentences with capitals and punctuation, typed exactly as shown (`case_from_difficulty`, 3) | TotD |
| Chapter | from `longer_from_chapter` (5: never, totd2) enemies take the next difficulty's lists | TotD (longer later) |
| First letters | no two prompts held (on the screen or briefly off it) start with the same letter; one coming back into view with a taken letter gets another word | TotD |
| Lock-on | the first letter locks onto the prompt starting with it; the box turns yellow | all three |
| Letters | each correct letter is one shot at the enemy (hit, blood, part damage, points); its hit points are held up (30000) from the first letter so the letters cannot kill; the last letter's shot kills (hit points 1 first) | TotD (each key a shot) |
| Interrupts (totd2) | bats, small creatures and projectiles are urgent: a letter that is not the locked word's next one but starts an urgent prompt breaks in; that one is typed, then the lock goes back to the word, its letters kept. An urgent prompt never starts with a letter still to type in the locked word | TotD |
| Thrown things (totd2) | while a thrown axe / barrel / knife carries a prompt it flies at `projectile_speed` of its speed (Normal 0.4: velocity x k, gravity x k^2: the same arc, k times slower) | TotD (thrown things drift) |
| Kill grade (totd2) | each kill gets S / A / B / C / D / F from its accuracy and speed over all its prompts, timed from when it could be typed (shown, or the last prompt done if later): `kill_grade_S..D` = accuracy %, WPM (S 100 % 50 WPM, A 100 % 36, B 85 % 27, C 70 % 19, D 50 % 11). Shown over the enemy for 1.25 s (gold S / A, white B / C, grey D / F; a boss sentence's under its panel), counted in the results' "Kills" row | TotD |
| Small targets | bats / Murrers / frogs / dogs and projectiles get no letter shots (a bat dies at any hit: its first letter killed it); the last letter fires once and waits 10 frames for the hit to show (its flags' hit bit); still not hit after 6 tries, the hit is given (flags bit 8 + player 1's bit, as a landed shot) so the word always kills or knocks the thing away | TotD |
| Misses | a wrong letter (or one that matches nothing) is a miss: accuracy, combo broken; the lock stays; nothing is entered, so there is nothing to Backspace | all three |
| Drop the lock | `drop_lock_keys`: 0 nothing, 1 Escape (default; Escape without a lock is the pause), 2 Escape and Backspace. Backspace alone does nothing (totd0 dropped the word on it: a typo's habitual Backspace threw progress away) | ours |
| Items | pickups by routine name (`item_names`: the gold frog ...) get a word; totd2: also the locks / route spots / signs (`route_names`), props with an item inside (flags bits 5-7), and in a rescue scene with no creature about the props by the researcher (Chapter 2's crate) | all three |
| Civilians | researchers / hostages / scene people never get a prompt; no shot is fired where one covers the aim point | all three |
| Off the screen | a prompt is drawn only while its enemy is a target on the screen; it keeps its word 5 s off the screen (a duck, a pillar) | ours |
| Combo | prompts in a row without a miss; the score gets `combo_bonus` x the combo (up to `combo_cap`) per prompt | Overkill |
| Reload | none: the gun is refilled every tick in play | TotD |
| Punctuation | typed as shown; `auto_skip_punctuation = 1` passes it over; spaces are always typed; case ignored below `case_from_difficulty` | Overkill / TotD |
| Long prompts | wrap onto two lines at the space nearest the middle | Overkill |
| Results | per chapter, over the clear's results: letters, misses, accuracy, WPM, prompts, best combo, the fastest prompt, a grade S-E | all three |
| Sonic Mode | the ticks run faster; windows and timers are in ticks: less real time to type; its own top 10s | ours |
| High scores | the game's own ranking (name entry as Arcade) in `ranking_totd.bin`, `ranking_totd_<preset>.bin` | ours |

## Round 3 (totd3: the user's play in a real window, "still many issues")

Verified by real play this time: `--totd-play wpm,accuracy,seed` runs the scripted typist in the real game window
(second monitor, no focus); its keys are pushed into SDL's queue and taken by the window's own event loop the next
frame, as a keyboard's; `HOTD_TOTD_REC=<dir>` writes the window's picture every `HOTD_TOTD_REC_EVERY` ticks (half
size) for a video. The run ends with the chapter or a game over.

- **Bosses** (root cause): a sentence's three shots only counted when they landed, x `boss_damage_x`. Sentences typed
  while the Hangedman flew off the screen or the Chariot turned did nothing; hit points per shot vary by boss, so the
  Hangedman took 17 sentences (77 s) at 60 WPM and a slower typist lost first. Now a boss part with hit points loses
  1 / `boss_sentences` (Normal 7) of them per sentence at once, on or off the screen; one shot follows for its own hit
  reaction (its damage taken back); the last sentence sets 1 hit point and its shots kill it the game's own way
  (repeated up to 20 s until one lands). The Chariot's armour part (no hit points) keeps the old shots. Sentences are
  shorter (`boss_short` list, Normal 12-17 letters). After a boss dies its other parts get no sentences for 10 s
  (the Chariot kept one up over its death). A boss's hit still lands only after a sentence's window ran out.
- **Projectiles** (root cause): only the axes / barrels were slowed (their velocity). The Magician's fireballs,
  Drake's knives and the spit reached the player in 0.5-0.7 s. Now every thrown thing with a prompt keeps
  `projectile_speed` (Normal 0.3) of each tick's step (its position put back part of the way after the tick); Normal
  projectiles get one letter; a thrown thing gets its prompt after 3 frames on the screen (the Magician's fading
  rings flashed prompts for 3 frames).
- **Pace** (`attack_hold`): 0 = the arcade's own timing: tried first, a 60 WPM typist lost every life by Chapter 1's
  second scene (creatures appear at arm's length and strike in 0.9 s, before a word can be read). 2 (the default) = a
  short grace from the prompt shown: `attack_wait` + `attack_per_letter` x letters (Normal 1.2 s + 0.24 s a letter),
  counted while the creature walks up, so most creatures attack on arrival on their own timing, and only one that
  appears next to you waits a moment; no bar; after an attack the others wait `attack_gap` (Normal 0.8 s). 1 = totd2's
  window (seconds from the arrival, the creature standing with a bar). The whole game runs at `game_speed` of the
  arcade's tick rate (0.85 / 0.92 on Very Easy / Easy, 1 from Normal).
- **Frogs and hanging Ebitans** (`auto_names`: kind 69 frogs, kind 135 Ebitan B that hang from the ceiling and drop, kind 70 set-up 11 hanging): no word; the layer shoots them itself `auto_delay` (0.4 s) after they
  show (a frog dies at a shot; a hanging Ebitan falls dead), as a gun player would in passing. Taran (Chapter 3's
  leapers, 1 hit point) count as small creatures (1-2 letters, they break into a word).
- **Locks**: the cell locks (kind 77), padlocks (kind 106), the locked door's lock (kind 189), route spots and signs
  had prompts already (totd2); `route_names` adds the old lock (`furukagi`). Chapter 1 by the cell route
  (`--route 1:3`): all 7 rescued.

Results (totd3, real window, Normal, 60 WPM 95 %, each chapter from its start with 3 lives; recorded):

| Chapter | Bats typed / killed | Projectiles shown / typed / killed | Small | Rescued | Boss | Hits |
|---|---|---|---|---|---|---|
| 1 | - | 5 / 5 / 5 | 14 / 12 / 12 | 5 of 7 | the Chariot beaten (armour, body) | 0 |
| 2 | 27 / 27 | 5 / 5 / 5 | 8 / 7 / 7 | 5 of 5 | the Hangedman beaten in 7 sentences | 0 |
| 3 | - | 33 / 30 / 30 | 11 / 7 / 7 | 3 of 3 | the Hermit beaten | 2 |
| 4 | 14 / 14 | 8 / 8 / 8 | - | - | Chariot, Hangedman rematches, the Magician (7 sentences) | 2 |

With no hold at all (`attack_hold 0`) the same typist had a game over in Chapter 1 (frame 4500) and in Chapter 3.
Chapter 1's two lost researchers: the run took the event 6 route (route by timing), where a researcher hangs from a
balcony with two Ebitans on him: he covers them, the shots wait for a clear part, he falls (open).

## Round 4 (totd4: the user's reference video, the sounds)

The user's reference: The Typing of the Dead on the PC (SEGA 2000), a full playthrough (worker notes: what its boxes,
ranks, HUD, bosses and results look like). The biggest differences in feel were the boxes, the kills' ranks, the
HUD and the silence of the typing; the pace (HOTD's own, a short grace) and the shorter words (the user's choice in
totd2) are kept.

- **Boxes** (the game's 8 x 8 font, drawn boxes as before): a prompt that fits a line shows **two rows**, the prompt
  and under it the letters still to type in their places (typed ones gone), as TotD; a longer one wraps as before.
  Border **green**, **yellow** once its creature is at you (an attack place, within 45 units, or its grace over), for
  a thrown thing, and for a boss sentence in its last 40 %. The word being typed: **red corner marks**. Each correct
  letter: **red spikes** out of the box for 5 ticks. The box sits **on the body** (centred on the aim point, the
  chest), not above the head; one size larger from 1000 window lines.
- **Ranks**: "RANK A" where the kill was, no frame: "RANK" white, the letter coloured (S gold, A light blue, B green,
  C white, D grey, F red).
- **HUD**: the COMBO / ACCURACY / WPM line shows only in a fight (until 3 s after the last prompt), as TotD's HUD.

### Sounds

Each from `data/typing/<name>.wav` when the file is there (RIFF PCM 8 / 16-bit, mono / stereo, any rate; a mod's
folder first), else the PC game's own sound (a cue of THOTD.EXE's table 0x5CBDA8, typing.ini `key_cue` /
`miss_cue` / `ding_cue`; `sounds = 0` turns them off). Played straight into the mixer (never through the game's sound
queue: the game's memory and frames are the same with or without them), over the arcade board too.

| Sound | File | Default | What it is |
|---|---|---|---|
| a correct letter | `key.wav` | cue 28, GAMESNDS sample 13 (0.15 s) | the 1998 PC's name entry (records screen, screen 15) key sound: its frontend (0x420A60, from frontend_input 0x41DA18) plays cue 28 on Enter / Space / a click; the letter keys of 0x425188 play none. The same sample is the arcade's reload click (0xA01200) |
| a wrong letter | `miss.wav` | cue 253, sample 60 (0.18 s) | a short low buzz from the PC bank (arcade code 0xA01467) |
| a word done | `ding.wav` | cue 140, `SOUND\O_LIFEUP.RBH` | the coin / life item chime (0xA01351); since 2026-11-05 `data/typing/ding.wav` is a real typewriter bell: "Typewriter, bell #2" (a Hermes Precisa 305) by Joseph SARDIN, BigSoundBank.com sound 2845, CC0 (public domain), trimmed to the ding (1.3 s) and mixed half clean / half an 8-bit 22 kHz copy for a mid-90s sound (the user's pick). Credit it in CREDITS.txt / NOTICE.md when TotD0 ships |

The run prints once where each came from (`TOTD sound key: PC cue 28`, or the file). `HOTD_TOTD_NOSOUND=1`
(testing) leaves them out: a headless run with and without them gives WAVs that differ only by these sounds (totd4,
Chapter 1, 60 WPM: keys 0.21 s apart, then the ding after each word; correlation 0.99 with samples 13 / O_LIFEUP).
The kill itself keeps the game's sounds (the shot, the creature's voice). The arcade name entry's letter sound
(0xA01363 = cue 155, sample 37) is a 1.5 s whoosh (the letter flying to its slot), not a key sound.

## Round 5 (totd5: the user's play, "still not playable"; the feel report 2026-11-05)

| User's point | Root cause | Now |
|---|---|---|
| Typed axes / barrels still hurt | a typed thing was only shot; a barrel takes 2-3 hits (its kit's hit points), a shot that missed was retried, and its flight went on to the screen | a thing typed to its end is **harmless**: `Typing::neutral_` (15 s); its hit is held by the current task in `player_hit` (`port::gHitHold`) and in the new `Runtime::projectile_hit_player` hook (`gTotdProjectileHold`: returns 0, the kits end the flight); a kit object (axe, barrel, crate) is broken at once (hits left 0: its own shot-down routine), others get the hit bits |
| Frogs get a barrage | the killing shot was fired every frame until one landed (up to 40, the self-test log: "39 killing shots"); frogs had no word (auto-shot) | **one shot per word**: the killing shot fires once; if it does not land its hit is given directly (`partHit` in bot.cpp: player 1's hit bits on the biggest damaging body part, as the weapons' blast; small ones the object's hit bits), every 10 frames, no shot; frogs get a one-letter word; small words one letter up to Normal (as the PC TotD) |
| Keypad / locked researcher | Chapter 3's cell padlock (kind 106) got its word 200 units off, before the game lets it be shot (flag 0x51E970, then 6 s): typed for nothing, given up, the new word late | the padlock and Chapter 1's cell locks (kind 77) get their word only at step +0x60 = 2 (shootable); an item / lock's hit is given from the second try; the game's own rescue follows |
| The Chariot cannot be fought | boss sentences sat in a fixed panel low in the middle, over the fight; a sentence typed while the armour part could not be hit did nothing | the sentence sits on the weak point; after each sentence `boss_rest` (Normal 3.5 s) of boss action, its hits held; the damage rule of totd3 (1 / `boss_sentences` of the hit points per sentence) |
| Cut-scenes: hold Space | Space was no binding of player 1's skip; the arcade's own SKIP parts were left out of the hold | in TotD 0 Space counts as a skip button; the hold (2 s, the red ring) also runs the arcade's SKIP parts fast |
| No P2 Start | the arcade HUD's PRESS START (sprite 0x40) | `Runtime::noStartPrompt`: `hud_prompt_anim` clears its place; players 3 / 4's corner prompt off |
| UI covers things | the COMBO line and the boss panel low in the middle | the line in the top left (TotD's PERFECT place), one step smaller; boss sentences on the boss |
| Distant zombies | every target got a word | a creature gets its word once it is shootable (flags bit 0; kind 70's +0x324 bit 3: the route runner) and in the fight: an attack place, coming closer (tracked speed > 0.05), within `far_depth`, a captor, a rescue scene; else only after `idle_seconds` (4) with nothing else up while enemies are counted alive |
| Items | (checked) | words on pickups, props with an item (flags bits 5-7), locks; props / signs wait while a creature's word is up; `TOTD ITEMS chapter N: shown/typed/broken or taken` |
| Sounds barely audible | the bank's click played at its own level: typing sounds' 50 ms peaks -15.1 dB against the game's -13.3 dB | each sound brought to `sound_peak` (95 %) and a gain (`key_volume` / `miss_volume` 2.5, `ding_volume` 2): -8.8 dB (the PC TotD's key shots: -7.4 dB window peaks); the ding's O_LIFEUP (file cue 140) through `playExtra` too |

Feel report (2026-11-05): boxes laid out so they never overlap (the locked one first, the others pushed up / down),
one scale step larger, 2-pixel borders, a lock-on burst (corner blades shrinking over 10 frames); a far creature's
box above it; RANK pop-ups 0.75 s; shots with a small flash and no muzzle; wind-ups play (`holds` only stops an
attack with `attack_hold 1`; with 2 only the attacker's own hit waits on its grace and `attack_gap`); a one-letter
word grades on 3 letters' time.

`HOTD_TOTD_SKIP=1` (testing, with `--totd-play`): the typist holds Space through every cut-scene.

## Round 6 (totd6: difficulty, the PC TotD's HUD, Smooth Motion, the iron ball and the barrage, bosses)

| User's point | Root cause / measure | Now |
|---|---|---|
| 40 WPM on Normal: GAME OVER in Chapters 2-4 | the headless 40 WPM 95 % typist (round 5's last build): hits 3 / 2 / 15 / 2; Chapter 3's 15: three tough ones shown at once ("you", "fresh air", "cup of tea") each with its own grace from the same moment, the third struck while the first two were typed; the Hermit's hits (4) came through while no sentence was seen (off the screen, on a wall); the iron ball reached the screen 0.4 s after its word showed | **the graces queue** as a typist types: a creature's grace starts no sooner than the latest running grace ends (+ `attack_queue`), then `attack_wait` to read it and `attack_per_letter` a letter: the difficulty's typing speed (12 / WPM: Very Easy 20, Easy 25, Normal 40, Hard 60, Very Hard 75); the small ones (`small_wait`) neither queue nor hold the queue; after a hit the creatures whose grace ran out wait `hit_reset` again (TotD: the zombie steps back); a boss's own hit (its routine, a barrage shot) lands only after a missed sentence window wherever the boss is |
| HUD like the PC TotD | the reference: top left a gauge and "PERFECT nn" (gold, the words in a row without a wrong letter; "MISS!!" in its place on a typo, then 00), the lives bottom left, no bullets; top centre a time / "Rest n" counter only in its missions ("Defeat 10 zombies in 30 seconds") | "PERFECT nn" top left in the game's own 8 x 8 font (gold, dark edge, about 4 % of the height), "MISS!!" (light blue) for half a second on a wrong letter; accuracy / WPM only in the chapter's results; player 1's bullet row not drawn (`Game::noAmmoRow`, hud_ammo_draw); the lives as the game's |
| Boxes drift with Smooth Motion | the picture is blended between the last two ticks (alpha), the boxes sat at the aim point before the tick | each box at its aim point before the tick + alpha x its step over the tick (`Label::dx`, after()); a boss's weak point (it changes by turns) and jumps over 120 units are not blended |
| The iron ball gets a word | the ball is a decoy to the CPU teammates (shots spark); its throw (iron_ball_pos_attack: owner motion 0x279, ball step 0x21) hits through projectile_hit_player from the ball's own task | in its throw the ball is a thrown thing: a one-letter word from its first frame, flying at `ball_speed` (Normal 0.15) of its speed; typed: its spark, its hit held for that throw |
| The Magician's barrage gets words | 0x74150 had none (500-700 a fight) | a barrage shot gets a one-letter word once it leaves its circle round him (step +0x60 >= 3), at most `barrage_words` (Normal 3) at a time, slowed as the other thrown things; typed: shot down (its hit points), harmless |
| Bosses like the reference's Judgment (7:05-7:45) | the reference: a sentence of 12-17 letters about every 8 s, up 2-3 s (an 80 WPM player), then 5-6 s of boss action; the box on the flying weak point, green, yellow near its end; the damage on the boss's life bar at the top (about 1 / 8 a sentence) | `boss_rest` 5 s on Normal (round 5: 3.5), the sentence on the weak point (round 5), its window bar, yellow in its last 40 %, the arcade's own boss life bar at the top taking 1 / `boss_sentences` a sentence |

Measured in the reference (the PC TotD, $6,132,600 run): the lives (torch count, 2 samples a second over the whole
video, `tools/torches.py` in the worker's scratch) never drop to a plain zombie in Chapters 1-2 (a strong player);
the hits found are boss attacks: a sentence not finished in its window (the Hierophant-like countdown boss: 18
letters up 2.8 s; the last boss: 24 letters up 2.0 s), the Hydra's quiz answers up 3.8 s. A plain zombie's grace in
the reference could not be measured from hits; the per-letter times above are the difficulty's typing speed instead.

Difficulty (headless `--totd-test`, the scripted typist 95 % accuracy, each chapter from its start; hits = lives
lost):

| Difficulty, typist | Ch1 | Ch2 | Ch3 | Ch4 | all 4 | bosses |
|---|---|---|---|---|---|---|
| round 5 (before), Normal, 40 WPM | 3 | 2 | 15 | 2 | 22 | beaten |
| Normal, 40 WPM (seed 1) | 2 | 0 | 3 | 0 | 5 | beaten |
| Normal, 40 WPM (seed 2, 93-95 %) | 0 | 1 | 4 | 1 | 6 | beaten |
| Easy, 30 WPM | 3 | 3 | 2 | 1 | 9 | beaten |
| Hard, 60 WPM | 2 | 4 | 4 | 0 | 10 | beaten |
| Very Easy, 20 WPM | 0 | 6 | 4 | 1 | 11 | beaten (Ch2's chapter end waited with the test's kept life at 0 in one seed; seed 3 cleared) |

Hits by source (Normal 40): Ch1 a Murrer's leap, an agile kind 70; Ch3 two zombies, a thrown axe; Ch2 / Ch4 none
(round 5: Ch3 9 zombies, 4 Hermit, an axe, the iron ball).

Real window (second monitor, no focus, muted, Space held in cut-scenes, Normal 40 WPM 95 %): all four chapters cleared, lives lost 2 / 0 / 3 / 1
(Ch1 a Murrer's leap and an agile kind 70; Ch3 two zombies and an axe; Ch4 a zombie), all bosses beaten; round 5's
build: GAME OVER in Chapters 2-4 (before its attack_wait + 0.6 s; with it, Ch2 1 hit).

Smooth Motion (the real window, `HOTD_TOTD_RECTS=1`, all 4 chapters, 63715 boxes drawn, alpha median 0.5, a 990 x 780
window on the second monitor): the box's old place (before the tick) against the body as the blended picture
draws it (the aim point blended with the same alpha): median 0.9 px, 90 % 5.6 px, 99 % 24 px, worst 213 px, 1.4 % of
boxes more than 20 px off (at 1080p about 1.4 x that); now the box takes the picture's own blend (0 by construction).

### QA pass (qa1, 2026-11-05: real windows on the second monitor, 40 WPM Normal / 25 WPM Easy, 16:9 and 4:3)

- RANK pop-ups go through the boxes' layout pass (pushed up off the boxes and each other; up to here two RANKs and a
  box could sit on each other).
- Props, signs, route spots and crates / barrels with an item get no word while creatures are in the fight (shootable,
  word or not); an untouched prop word is taken back when they come. Pickups themselves (a life, a coin) and locks keep
  theirs. Chapter 1 at 40 WPM: items typed 14 -> 8 (shown 23 -> 26: the deferred ones come again), hits 2 -> 1. The
  route can differ (a sign / route spot typed later or not at all).
- One-letter words in one row (was "e" over "e").
- The chapter's typing results in the game's font, capitals, above the rescue picture (was the system font on a dark
  panel over its top).
- Very Easy / Easy: `small_wait` 2.4 / 1.9, `projectile_speed` 0.14 / 0.2 (the Murrers' leaps cost a 20 WPM typist 2
  lives a Chapter 2). Very Easy Chapter 2 at 20 WPM still loses 4-5 lives (bats, zombies, an axe).
- Very Easy Chapter 2 seed 3 "stall" (evt_WAIT_ITEM at 0xF3A04 after the Hangedman): only in runs whose kept life
  hit 0 in the boss fight (`--lives-kept`); a real game is at its continue screen there. Left as a test-harness case.
- The "u on a sign" (Chapter 3, 7800): a small creature's word (the faint green wire spider crawling down the DANGER
  sign), not the sign's.

## Round 7 (totd7: the user's first play with a real keyboard, 2026-11-05: "better", but)

### Why the tests passed what real play failed

The user's play (the portable's log and three screenshots each, 09:16-09:21): Chapter 1 took the event 6 route (the
keypad cell with two researchers; the screenshots show the keypad and the creature inside with no box at all), a
game over and a continue, then the Chariot with its intro skipped and no sentence for the 4.5 s until the user quit.
The scripted typist passed both every time. The differences between its keys and a keyboard's:

| Real keyboard | The typist (totd2-6) | Effect |
|---|---|---|
| a key-down carries its scancode (the game's key bindings are by scancode) | key code only, scancode 0 | its letters could never reach a binding |
| a person types when nothing is up (cut-scenes, the card, empty corridors, the rescue scenes) | typed only when a box was shown | outside play every letter went to the arcade's bindings: **P (player 1's pause key) paused the game mid-scene** (the real-event test stalled in a pause menu at its first cut-scene); with nothing up each key was a miss: PERFECT 00 |
| holds Space a moment after a scene starts | held Space from the scene's first frame | the hold-to-skip takes a key held from before as stale: the tests never skipped the Chariot's intro (the user did) |
| plays whichever route the play leads to | the window tests took the cell route (event 3's branch typed / shot), never event 6 | the keypad cell was never met in a real-window test |
| loses lives, continues | never lost all lives | the continue was never met before a boss |

Reproduced through the real-event path (below): the pause stall (stray letters in a cut-scene). Not reproduced: a boss
with no sentence (the Chariot after a skipped intro, held with Space or a letter; after a game over and a continue
(`--lives-out`, `HOTD_TOTD_CONTINUE`); with stray letters: its sentence is up on the first frame of the fight every
time), and the keypad's missing box (the keypad route at 24 and 40 WPM: the keypad's key is up and taken, both
researchers rescued). The keypad's lock and the creature in its cell are what the user named; both now go by the quick
key (one press) and the cell's creature waits while the keypad's key is up (below).

### What changed

| User's point | Now |
|---|---|
| A. locked researchers / the Chariot | the real-event path for every acceptance test (docs/testing.md section 5); letters outside play never reach the arcade's key bindings (only digits, Space, Escape, Enter, F-keys do); the locks / keypads take one key (B); a captor with no word of its own (the keypad cell's, behind the bars) waits while a lock's key is up (`holdsPrey`) |
| B. quick attacks by screen side | thrown things (axes, barrels, knives, the spit, the iron ball, the barrage), the small quick creatures (bats, Murrers, frogs, dogs, taran, the hanging Ebitans: no longer shot by themselves), crates / barrels / props and the locks / keypads get no word but **G** (left half of the screen) or **H** (right half), drawn as a small key cap with a yellow border on the target (the PC TotD's one-key prompt). G / H takes the most urgent one on that side at once (a thrown thing first); the word being typed stays locked. With none on that side the key does nothing (no miss, the streak kept). No word starts with G or H; the locked word's next letter G / H is that word's. `quick_keys`, `quick_left`, `quick_right` |
| C. gunfire | every correct letter sounds the gun: a letter whose own shot fires (a creature's letter) has the game's shot; any other (a boss sentence, an item, a tick's second letter) plays the arcade gun's shot by itself (`shot_sound` 0xA01208, player 1's sound code, straight to the sound: the game's memory never sees it). The small spark stays, no big flash |
| D. typing sounds louder | the key / miss / ding go after the PC sound's limiter (they went into it with the music and the effects and were squeezed by them), with a soft knee only where they sound; `key_volume` / `miss_volume` 2.5 -> 4. They scale with the effects volume as the gun does; the music volume never touches them |
| E. the red corner burst | gone, and every red mark with it (the corner triangles, the letters' red spikes): the locked box has one solid border in the game's accent red, drawn over the others; borders one game pixel (were 2 / 3); an optional one-pixel drop shadow (`box_shadow`, `HOTD_TOTD_SHADOW=1`; off until the user picks) |
| F. typing with nothing up | a key with no box up (an empty screen, between waves) is ignored: no miss, no streak reset, no shot; in cut-scenes letters are nobody's |
| (coordinator) any key skips | in TotD 0 any key held 2 s fills the hold-to-skip ring (letters too); a tap is let go long before: hammering keys as a scene starts never skips |
| G. groups | a creature about to get a word joins one with a word of the same routine within `group_dx` 34 / `group_dy` 44 screen units (the arcade's 496 x 384 screen) and `group_dz` 10 of depth (up to `group_max` 4): one word, its box over the group's middle, takes them all down (each member's own killing shot or hit); a member strikes on its leader's grace |
| H. crates side by side | the same for crates / barrels / props (`prop_group_*` 70 / 40 / 16): one key breaks them all |
| I. auto pickup | a pickup (`item_names`: a coin, the gold frog, a life, also from a broken crate) is shot by the layer `pickup_delay` (0.25 s) after it shows: the game's own pickup, sound and effect |
| (coordinator) strike vs damage | round 5-6 let a creature begin its attack inside its grace and only held the hit: it visibly struck and nothing happened. Now its attack choice (`gAttackHold`) is refused until its grace is over (and `attack_gap` after the last attack began): it walks up and stands at you; then it attacks on its own timing and the hit lands with the strike. Typed first, it goes down before any strike. The few that never ask (the agile kind 70, the Ebitans' vomit, the taran) strike on their own timing and now hit too. `HOTD_TOTD_HOLD6=1`: round 6's rule (the comparison) |
| (coordinator) "Normal is too slow and easy" | the difficulties move up one typist: Very Easy for ~25 WPM (round 6's Easy), Easy for ~40 (round 6's Normal), Normal for 55-60 (round 6's Hard), Hard 70-80 (round 6's Very Hard), Very Hard 90+: `attack_wait`, `attack_per_letter`, `small_wait`, `attack_queue`, `attack_gap`, `hit_reset`, `projectile_speed` (Normal 0.3 -> 0.4), `ball_speed` (0.15 -> 0.2), `barrage_words`, the boss windows; `boss_rest` Normal 5 -> 3.5 s (the reference: a sentence about every 8 s, up 2-3 s); `game_speed` 1 from Easy |
| (coordinator) "scripted events at the same speed" | the only thing that slowed a scripted scene was the rescue timer: a captor at its researcher (the opening's two creatures pushing the researchers) was held still until `rescue_wait` + its word's letters ran out. `rescue_wait` 2.5 / 1.5 / 0 / 0 / 0: from Normal no rescue timer at all (the arcade's own pace; a captor whose word is typed is held: it is going down); the keypad cell's creature still waits while the keypad's key is up |

Measured (headless `--totd-test`, Normal, 40 WPM 95 %, seed 1, each chapter from its start; "held strikes" = a
creature's strike that hit nothing while its word was up):

| Chapter | held strikes round 6 -> 7 | hits round 6 -> 7 | hits by |
|---|---|---|---|
| 1 | 7 -> 0 | 1 -> 0 | |
| 2 | 19 -> 0 | 1 -> 3 | kind 70 x2, vomit (round 6: the Hangedman) |
| 3 | 26 -> 0 | 3 -> 3 | 2 zombies, a taran |
| 4 | 4 -> 0 | 0 -> 0 | |

Bosses keep their rule (a hit lands only after a sentence window ran out): their own strikes in the rest between
sentences still play without damage (Chapter 3: the Hermit's, 6 held). Open: hold a boss's strike animation itself.

Loudness (headless `--audio-wav`, Chapter 1's first 6000 frames, 40 WPM; 50 ms window peaks, dBFS; the typing sounds
alone = the run minus the same run with `HOTD_TOTD_NOSOUND=1`; the gun shots alone likewise with `HOTD_TOTD_NOSHOT=1`):

| | median | 90 % | max |
|---|---|---|---|
| the game (all its sounds, no typing sounds) | -13.7 | -7.8 | -3.2 |
| the gun's letter shots | -10.6 | -5.7 | 0.0 |
| typing sounds, round 6 (inside the limiter, volume 2.5) | -9.0 | -4.1 | -0.9 |
| typing sounds, round 7 (after the limiter, volume 4) | -8.8 | -1.5 | 0.0 |

Flicker (the real-event path, Normal, 40 WPM 95 %, stray keys, cut-scenes held with K; `TOTD FLICKER`: boxes gone and
back within 60 frames + quick keys changing letter and back): round 6's boxes (`HOTD_TOTD_NOLATCH=1`) Chapter 1 4 + 2,
Chapter 2 17 + 3, Chapter 3 15 + 0 (a game over), Chapter 4 7 + 0; round 7: 0 in every chapter. Box edges (`TOTD BOX
EDGES`): 0 boxes past the picture's edge in every run (16:9 and a 4:3 picture in a 16:9 window); 40-143 a chapter were
kept inside (their creature's aim point past the edge). The causes: a target out of the game's shootable list for 1-55
frames (the agile kind 70 in its leaps, zombies dropping / jumping down, the bosses flying off the screen: their box
went and came back); a word taken from one that could not be shot for a frame; a quick key's letter following its
target over the middle; a crate typed off the screen whose key came back when its shots ran out.

Difficulty (the real-event path, 95 % accuracy, stray keys, each chapter from its start with 3 lives; hits = lives lost):

| Difficulty, typist | Ch1 | Ch2 | Ch3 | Ch4 | bosses |
|---|---|---|---|---|---|
| Very Easy, 25 WPM | 1 | game over | 2 | 0 | beaten (Ch2: not reached) |
| Easy, 40 WPM | 0 | 2 | 3 | 0 | beaten |
| Normal, 55 WPM | 1 | game over | 2 | 1 | beaten (Ch2: not reached) |
| Normal, 60 WPM | 0 | 3 | 3 | 0 | beaten |
| Hard, 75 WPM | 1 | 4 | 4 | 1 | beaten |
| Very Hard, 95 WPM | 0 | 4 | 3 | 1 | beaten |

Chapter 2 is the hard one at every setting (the bats, the agile kind 70s).

## Round 8 (totd8: QA through the real-event path; boss strikes and pace, Normal's fairness, the boxes, the bosses' length, the ding)

| Point | Root cause / measure | Now |
|---|---|---|
| Boss strikes play between sentences with no damage (round 7's open item) | the bosses' own attack choice was never asked: round 7 held only the hit (headless, Normal 60 WPM: 9 / 8 / 7 / 31 boss strikes a chapter that hit nothing) | **the strike itself is asked** (`port::gBossHold`, where each boss begins one): the Chariot's attack actions 8 / 9 / 11 / 18 wait in its idle (action 17, as the arcade's own "combat not active"), the Hermit's claws 11-15 and lunge 21 in a pose (33), the Hangedman's claw run (state 7) where he is, his hover's attack (state 6) and the dive's swoops (state 12: the dive flies on), the Magician's lunge (state 5 step 10: one more swoop). A strike goes (a) when a sentence's window ran out: its hits land (`boss_strike_seconds` 6, or until one did), owed until it lands; (b) **at the boss's own pace** in the rest after a sentence (`boss_strike_gap` 9 / 8 / 7 / 6 / 5 s apart at most): the next sentence comes `boss_block_warn` (1.5 / 1.2 / 0.8 / 0.6 / 0.4 s) before the strike and its first `boss_block_letters` (1 / 2 / 2 / 3 / 3) letters **block** it (the strike is seen, the boss staggers 12 frames into it: one shot, its damage taken back), else it lands. Not held: a boss whose sentence was not seen for 4 s (nothing to type) and a boss whose last sentence was typed (Chapter 4's Hangedman, at 0 over the floor, lives on until a hit in his claw run). `HOTD_TOTD_BOSS7=1`: round 7's rule |
| "The Magician is just flying around and not even trying to attack" (user) | his fight is mostly swoop rounds (state 5); round 7 let his lunge play with no damage; this round's first hold (a) alone made every boss passive at 60 WPM (strikes 0 / 1 / 0 / 0 a chapter) | (b) above: his lunge comes every 7 s or so in the rests, blockable; his fireballs (state 4) and the barrage (state 6) keep their G / H keys (a fireball not typed hits); the barrage's keys now really capped at `barrage_words` (11 came at once). The reference's Magician (46:54-49:18): orbs round him, launched 3-4 at a time each with a 1-3 word phrase (typed: shattered; missed: a life), a sword lunge blocked by a phrase, then a long sentence on him (1 / 16 of his bar); ours: G / H keys on the fireballs (round 7's rule for thrown things), the lunge blocked by the sentence's first letters |
| Bats in the Hangedman's fight hit with no damage | every hit in a boss fight waited on a missed window | the creatures there go by their own grace, as anywhere else |
| Normal 55 WPM: game over in Chapter 2 | hits before the grace ran out: the circling bats' dive (`bat_st3_circle` action 3: `pick_target_player_any`, never asked), the hanging Ebitan's spit / stream and its rise's spit, the agile kind 70's leap from the wall; the Hangedman's swarm keyed at once, each bat with `small_wait` from the same moment (the fourth struck while the first three were typed) | those attacks ask `gAttackHold` too (the rise's spit inside the grace: no hit); the small ones' graces queue, `small_queue` (0.6 / 0.45 / 0.35 / 0.3 / 0.25 s) a key. Headless Chapter 2 at 55 WPM: hits 5 -> 3 |
| Boxes block the zombies (user) | the PC TotD reference measured (15 moments, 7:47-25:00): a box is a fixed size (not scaled with distance), centred on the body about 53 % down from the top of the head (the belly: head and shoulders always clear); a far / small creature's box just above its head. Ours: on the object's centre (the chest: arms and their swing hidden), far ones (z > 70) above | a creature's box centre `box_anchor` (0.53) down its body (its parts' hit spheres); a creature whose body on the screen is under 2.2 box heights gets its box just above its head. `HOTD_TOTD_COVER=1` reports the body / head each box hides |
| Bosses too easy (user) | the reference's boss bars (2 samples a second, 9 fights): a sentence takes about 1 / 9-1 / 10 of the bar, one every 8.5-9.6 s, 55-96 s a fight; its last bosses about 16 sentences, 145-180 s. Ours (round 7, Normal): 5 (the Chariot: its armour 3 hits x3, then 2) to 7 sentences, 31-48 s, no window missed at 40-75 WPM | `boss_sentences` 5, 7, 9, 11, 13; per boss `boss_x_<name>` (the user's pairs: the Hangedman = the reference's first boss, 9; the Chariot = its second, x1.15 = 10; the Hermit the general rule; the Magician `boss_final_x` 1.8 = 16); the Chariot's armour on boss_sentences too (its bar / n a sentence, was its one hit x3); `boss_rest` normal 3.5 -> 4.5 s (hard 4, very hard 3.5); sentence windows normal 3 + 0.3 -> 2 + 0.25 s a letter (easy 3 + 0.35, hard 1.5 + 0.2) |
| The ding: "arcadey or PC depending on the audio" (user) | the board's own effects (`HOTD_SCSP_KEYS=1` prints every slot keyed on: 57 key-ons over 316 effect codes, all 8-bit PCM, at 11025 / 15988 / 22050 Hz through a RAM ring at 0x66000-0x6E000; the gun shot 0xA01208: 8-bit 22050 Hz); the PC's set-up 8-bit 11025 Hz mono (docs/formats.md) | a typing sound's own WAV is converted on first use for the board in use: resampled (windowed sinc, low-pass at 0.45 of the new rate) to 22050 Hz (arcade) or 11025 Hz (PC), its peak to `sound_peak`, rounded to 8-bit (no dither: the PC's own rounding is not known); played as any 8-bit sample (linear interpolation, the SCSP's too). One copy per board; the WAV on disk untouched. `HOTD_TOTD_RAWWAV=1`: as before. Loudness as round 7 (the same peak normalisation) |

Boss fights (the user's pairs; reference: the PC TotD's bars, sentences = steps of the bar; ours headless, 95 % accuracy):

| Ours / reference | reference (time, sentences, length) | round 7, Normal 60 | now Easy 40 | now Normal 60 | now Hard 75 |
|---|---|---|---|---|---|
| Hangedman / first boss (flying) | 6:57-8:13, 9, 76 s | 7, 48 s, 8 strikes no damage | 7, 72 s | 9, 67 s | 11, 85 s |
| Chariot / second boss | 14:44-16:20, 10, 96 s | 5, 31 s, 9 strikes no damage | 8, 65 s | 10, 54 s | 13, 90 s |
| Hermit / (general) | (others: 8-11, 65-93 s) | 7, 48 s, 7 strikes no damage | 7, 56 s | 9, 65 s | 10, 64 s |
| Magician / the Magician (the reference's 7th fight) | 36:20-39:21, ~18 steps of his bar, 181 s with a continue; every fireball carries a word or short phrase (3 at a time), a miss costs a life | 7, 42 s | 12 | 15, ~160 s (real-event: ~170 s, 2 lives: a fireball, a lunge) | 19 |

(totd8 correction: 46:54-49:18 is the reference's last boss, the orb-summoning Emperor, not the Magician. The Magician is the caped figure at 36:20-39:21; the Hydra quiz at 43:07-44:02 comes between. Ours now: `boss_final_x` 1.7, his fireballs a near zombie's word (`fireball_words`), no free strike while a thrown thing's word is up.)

Boss attacks (headless, Normal 60 WPM, chapters 1 / 2 / 3 / 4): round 7: 9 / 8 / 7 / 31 strikes, all without damage; this round's first hold: 0 / 1 / 0 / 0; now 5 / 4 / 3 / 14 strikes (24 of them free: 21 blocked by the typist, the rest landed or owed after a missed window).

Box cover (headless, Chapter 1, 40 WPM, 640 x 480, `HOTD_TOTD_COVER=1`; the share of the creature's body, its hit spheres on the screen, its own box hides; the reference estimated by eye from 6 frames):

| | reference | before (the object's centre) | now |
|---|---|---|---|
| near (z < 30) | ~15-25 %, head 0 | 11.8 %, head 0 % | 11.8 %, head 0 % |
| mid | ~22-29 %, head 0 | 24.4 %, head 2.1 % | 19.5 %, head 0.2 % |
| far (z > 70) | 0-20 % (above the head) | 65.2 %, head 15.2 % | 10.4 %, head 0 % |

In real play (the real-event path, Normal 60, all four chapters, a 16:9 window): near 11.6 % body / 3.1 % head, mid 20.6 % / 1.2 %, far 19.5 % / 3.8 % (26,000 boxes; the head share is its own box pushed up there by the no-overlap rule or kept inside the picture).

Lives lost per chapter (the real-event path, 95 % accuracy, stray letters, K held to skip, each chapter from its start with 3 lives; round 7's table in brackets): no game over at any setting; every boss beaten; 0 blinking boxes, 0 boxes past the picture's edge, 0 creature strikes with no damage in every run.

| Difficulty, typist | Ch1 | Ch2 | Ch3 | Ch4 | boss strikes (free / blocked) |
|---|---|---|---|---|---|
| Easy, 40 WPM | 0 (0) | 3 (2) | 3 (3) | 1 (0) | 20 / 20 |
| Normal, 55 WPM | 1 (1) | 2 (game over) | 2 (2) | 1 (1) | 21 / 22 |
| Normal, 60 WPM | 0 (0) | 2 (3) | 3 (3) | 2 (0) | 22 / 22 |
| Hard, 75 WPM | 1 (1) | 2 (4) | 4 (4) | 2 (1) | 28 / 28 |

QA notes (the real-event path, a window on the second monitor, no focus, muted; stray letters, K held 2 s to skip, the continue key):
- The end of the game: Chapter 4 at 60 WPM to the ending (`HOTD_TOTD_FULL=1`), the name entry typed by the stray letters ("LYA", place 1), `ranking_totd.bin` written, back to the menu.
- Chapter 1's keypad route (event 6, `--route 1:3=0,1:4=0`) at 60 WPM: cleared, 6 of 7 rescued, 1 hit; a continue after `--lives-out 14000`: continued and cleared.
- (open) The Hangedman's owed strike can come late (21 s after the miss once: he was in his bat phase); Chapter 4's Hangedman after his last sentence still claws 2-3 times with no damage before he falls (the arcade's "killed over the floor" rule).
- (open) A blocked strike's stagger does not always stop its motion (its hit then held: the strike reads as parried).
- (open) The key / miss sounds are the PC bank's samples in both sound modes (only WAV files are converted). The reference's orbs carry phrases; ours G / H keys.

## Attacks (totd1: "enemies attack too fast, even on Normal")

TotD: a zombie walks up, then gives a clear window before it hits; its attack waits on a timer you can see.

- **Timer.** A creature "arrives" when it takes an attack place (+0x2E7), comes within 45 units, or (bats, the agile
  ones) once its prompt has been up `attack_wait` seconds. It may attack `attack_wait` + `attack_per_letter` x its
  prompt's letters later (small ones: half the wait). The bar under its word shows the time left (green, yellow, red).
- **Gap.** No attack on player 1 within `attack_gap` seconds of the last one (an attack beginning: player 1's
  "targeted" flag set; a hit; a throw). After an attack the creatures whose timer had run out wait `attack_wait` again.
- **Defaults** (totd6, very easy .. very hard): wait 2.4 / 2.1 / 1.6 / 1.3 / 1.1 s, per letter 0.6 / 0.48 / 0.3 /
  0.2 / 0.16 s, queued (`attack_queue` 0.5 / 0.45 / 0.4 / 0.3 / 0.2 s), small ones 1.8 / 1.6 / 1 / 0.75 / 0.6 s, gap
  1.5 / 1.2 / 0.8 / 0.5 / 0.3 s, after a hit 3 / 2.5 / 2 / 1.5 / 1 s (totd1's: wait 6 / 5 / 3.5 / 2.5 / 1.5 s).
- **Where it is asked** (`port::gAttackHold(obj)`): the standard enemy's attack choice (`enemy_ai` states 5 / 6: no
  free player for it), `pick_target_player` (bats, the agile kind 70, frogs, scene zombies, the Hangedman's helpers),
  the dogs' bite (a held bite snaps at the air), and from the layer the kind 32 throwers' countdowns (+0x2EC held at 2).
  A creature without a prompt (off the screen) waits too. Murrers' leaps are not held (their own queue).
- **Bosses** (`port::gHitHold(player)` in `player_hit`): in a boss fight a hit on player 1 lands only after a sentence's
  window ran out (TotD: the boss strikes when its sentence was not typed in time); a finished sentence holds them again.
  Boss sentence windows: `boss_window_seconds` 5 / 4 / 3 / 3 / 2 + `boss_window_per_letter` 0.6 / 0.5 / 0.4 / 0.3 /
  0.25 s a letter (Normal: 30 WPM makes it). A sentence's attack that did no damage (the boss turning, in its own hit
  reaction) goes on with a shot every `boss_shot_gap` until one does (3 s at most).

Why the hit points are held: a shot's damage lands a frame or two after the trigger and HOTD's zombies need several
hits, so "one key = one shot" would kill most enemies half-way through a word, or never at its end. Holding the hit
points until the word is done gives the SEGA games' rule (the word kills) while every letter still visibly hits. A
prompt dropped (Escape, the enemy gone for 2 s, a cut-scene) puts the real hit points back; a killing shot that never
lands (40 tries, the body parts tried in turn: `partAim`) also puts them back and gives a new prompt.

Why bosses take `boss_damage_x`: a boss staggers at the first hit and takes no more for a while (the Chariot: 40 a
hit), so a sentence is about one hit; x3 makes the Chariot about six sentences. The extra is taken from the hit points
after the boss's own hit routine ran, never below 1, so the boss's own checks (armour off, death) come with its own
hits.

## Rescues (totd2: "the trapped / grabbed researchers can't be saved")

Root causes: a researcher in front of the captor's aim point stopped every shot (`Bot::hostageCovers`), so the
killing shot never fired and the prompt was given up; the captor's bite came at the game's own pace, before a word
could be typed; the locked doors' padlocks had no prompt. Now:

- The killing shot tries the captor's damageable body parts (`partAim`) for one the researcher does not cover.
- **Rescue timer** (`port::gPreyHold(obj)`, in the run-after / attack-researcher routines' arrival tests in
  `enemy_actions.cpp`): a creature that reached its researcher bites only after `rescue_wait` (Normal 4 s) +
  `attack_per_letter` x its prompt's letters from the prompt shown (one with no prompt yet: `rescue_wait`). While held
  it is put back where it stood after the tick (walking on into him pushed Chapter 1's first researcher off his path,
  and the event never went on). Typed in time, the game's own rescue follows its death (thanks, item, bonus); too
  late, the game's own bite.
- Locked-door researchers: the padlocks and route props get a word (Items above).

## Bosses (totd2: "bosses don't work")

- One sentence at a time: a boss of several parts (the Chariot's armour and body) had a sentence each, drawn over each
  other in the one boss panel. The part shown is the one locked or begun, else the most dangerous; the others wait
  (their windows held).
- The Magician's barrage (homing shots, routine 0x74150, 500-700 a fight) gets no prompts; its hits are held by the
  boss rule anyway.
- Hits outside a boss fight (`gHitHold`): only while a creature whose attack timer ran out is there (the agile kind 70,
  the vomit, the taran never ask `gAttackHold`; the self-test's 40 WPM typist took 16-18 hits a chapter from them).
- Chapter 4 ends in the ending, not a clear: its results are taken there (`Typing::flush`).

## Text

The prompts are drawn in the game's own 8 x 8 text font: the arcade's SCORE-line font (the boot's debug font, 128
ASCII glyphs at ROM 0x2EF3870, 4-bit pens; palette 0 at ROM 0x168A0 for the pens' brightness, through MAME's gamma
as `sprites_arcade.cpp`). It has every printable ASCII character (letters, digits, punctuation); anything else falls
back to SDL's 8 x 8 font. No new art. Typed letters red, the next one yellow and underlined, the rest white.
`HOTD_TOTD_FONT=<chars>` (developer) prints which glyphs the game has and draws the given ones as text.

## Input

totd2, the root cause of "bats and projectiles still don't work in real play" (totd1's self-test: 19/19): its typist
called `Typing::typed` itself and always finished its locked word first, so it never typed a thrown axe while a word
was locked; in real play the axe's letters were misses against the locked word, and the axe hit 0.6 s after the throw.
A frog / bat still falling after its word also got a new prompt. Now the self-test's keys go through SDL's event queue
(a key-down, Shift held for a capital, the text event, a key-up) into the window's own handler (`totdEvent` in
main.cpp); the typist reads a prompt a reaction time after it appears (0.38 s at 70 WPM, 0.48 s at 40), types urgent
ones as soon as read, slips onto a neighbouring key, and presses Escape when its locked prompt is gone for a second.

SDL text events while player 1 is in play (`Typing::inPlay`: play, shots on, no cut-scene / card / results / pause);
every printable key is kept from the key bindings then (a letter never also fires or aims). Ctrl / Alt chords,
F-keys, Enter and Escape without a lock reach the game as usual. One typist: player 1. Players 2-4 can play along with
the mouse / pads / CPU teammates. Offline only (an online game turns the mode off).

## Data (`data/typing/`, mods can replace any file)

- `typing.ini`: the tuning above (each data folder's in turn, a later one overriding). Values "a, b, c, d, e" are per
  difficulty (fewer: the last repeats).
- `<dictionary>/<list>.txt`: one prompt per line, `#` comments; the lists named in typing.ini. The default set
  (`default/`): `short` (1-3 letters), `easy` (3-5), `words` (5-8), `long` (8-13), `phrases` (everyday, lower case),
  `hard_phrases` (capitals, punctuation), `boss_easy`, `boss`, `boss_hard`. Plain everyday English, written for
  ReWrithe, a little spooky flavour; no song, book or SEGA text. A list missing from a dictionary borrows the largest
  one it has. `dictionary = <name>` (or `--totd-dict <name>`) picks the folder: a player's own word lists.
- HUD words go through the language system (`lang::tr`); prompts are not translated (a language's own dictionary
  folder is the way).

## Running it

- Window: main menu > More Modes > Typing of the Dead 0, or `hotd --totd` (`--difficulty 0-4` for a quick try).
- Headless self-test: `hotd --totd-test [--difficulty 0-4] [--totd-typist wpm,accuracy,seed] [--sonic-mode nightmare]
  [--chapter N]`: a scripted typist (70 WPM, 95 %; capitals with Shift from Hard; its keys as SDL events) plays the chapter to its end and
  prints `TOTD chapter N: ...`, `TOTD lists (...)` (prompts and mean letters per list), `TOTD attacks: ...` (attacks
  begun, hits taken, the closest two), `TOTD small targets / projectiles: ...` (typed, gone at the last letter, kill
  shots that never landed, letter kills) and `TOTD TEST: PASS|FAIL` (fails on attacks closer than `attack_gap`, small
  targets not killed, an enemy killed by a letter). totd2: `TOTD REPORT chapter N` (bats / projectiles / small ones
  shown / typed / killed as the game saw it afterwards, rescued of those who appeared, the boss, the grade spread,
  hits) and `TOTD LENGTHS chapter N` (mean letters by enemy class); fails on typed-but-not-killed (< 90 %) and on a
  boss not beaten; the attack gap is reported only. `--totd-verbose` a line per prompt; `HOTD_TOTD_TRACE=1`
  prompts given / dropped, shots, attacks, player 1's targeted flag; `HOTD_TOTD_BOSSHP=1`, `HOTD_TOTD_OBJ=<hex>`.
- Pictures: `--totd-test --render --shot-at F --shot-dir D` draws the prompts too.
- Real window (totd3): `hotd --no-launcher --windowed --mute --chapter N --totd-play 60,0.95,1 [--totd-verbose]` (a
  window on the second monitor, no focus; `HOTD_TOTD_REC=<dir>` its pictures for a video): prints the same report.

Results (totd1, 2026-11-03, Chapter 1 unless said; player hits include the boss):

| Run | Lists (mean letters) | Hits | Small typed / killed |
|---|---|---|---|
| totd0, Normal, 70 WPM | words / phrases / sentences mixed | 19 | bats died at the first letter, frogs never |
| Very Easy, 70 WPM | easy 4.4, short 1.0, boss_easy 20 | 4 | 20 / 20 |
| Normal, 70 WPM | words 6.4, phrases 11, short 2.8, boss 37 | 4 | 19 / 19 |
| Normal, 30 WPM 90 % | the same | 16 (chapter cleared) | 19 / 19 |
| Very Hard, 70 WPM | long 9.3, hard_phrases 18, short 3.0, boss_hard 42 | 11 | 18 / 18 |
| Chapter 2, Normal, 70 WPM | | 4 (totd0: 44) | 20 / 20 |
| Sonic Nightmare, 100 WPM | | 4 | 20 / 20 |

Results (totd2, 2026-11-03, Normal, typist keys through SDL events; 70 WPM 95 % / 40 WPM 92 %):

| Chapter | Bats typed / killed | Projectiles typed / killed | Rescued | Boss | Grades S A B C D F | Hits |
|---|---|---|---|---|---|---|
| 1 at 70 / 40 | 1/1, 1/1 (+ small 16/16, 13/13) | 3/3, 2/2 | 6 of 6, 5 of 6 | beaten, beaten | 28 25 17 7 3 0 / 1 6 42 21 5 0 | 0 / 1 |
| 2 | 34/34, 31/31 | 4/4, 2/2 | 5 of 5, 6 of 6 | beaten, beaten | 29 40 23 11 7 0 / 1 12 59 32 11 4 | 1 / 6 |
| 3 | - | 20/20, 25/25 | 3 of 3, 3 of 3 | beaten, beaten | 51 36 18 11 3 0 / 6 19 50 32 9 1 | 2 / 3 |
| 4 | 20/20, 18/18 | 28/28, 25/25 | (none) | beaten (the ending), beaten | 30 25 27 6 5 0 / 2 11 37 30 7 1 | 1 / 3 |

Mean letters (70 WPM, all chapters): zombie near 3.5, mid 4.3, far 6.2, captor 3.0, tough 9.8, item 4.3, bat 2.0,
projectile 1.4, boss 19.9 (totd1 Normal: words 6.4, phrases 11, boss 37).

## Open

- (totd2) Murrers' leaps are held (they regroup); the Magician's / Hermit's own attack timing not looked at (their hits
  are held by the boss rule). Some projectiles are seen but never typed (Chapter 4: 45 shown, 28 typed): they reach the
  player or leave the screen before the typist reads them; their hits are not counted as a failure.
- Boss "quiz" segments (TotD's answer prompts).
- A dictionary chooser in the menu; a per-language dictionary picked with the language.
- Box frames / typewriter art from the user (the font is now the game's own).
- (totd6) Not like the PC TotD yet: its top-left gauge (what it measures is not known from the video) and the
  missions' top-centre time / "Rest n" counter (we have no missions); the boss countdown ("3, 2, 1, 0") and quiz
  phases; Chapter 1 of the PC TotD ("for absolute beginners") uses 2-letter words, ours 3-5. A plain zombie's grace
  in the PC TotD was not measured (the reference player is never hit by one); ours is the difficulty's typing speed.
- (totd6) The scripted typist finishes a locked item / prop word before turning to creatures that appeared meanwhile
  (Easy Ch1: two agile kind 70s hit while it typed a coin); a person might drop it (Escape).
