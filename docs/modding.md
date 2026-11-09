# Making mods

A mod is a folder with a small text file in it. You switch mods on and off, set their order and change their settings
in the game: **Options > Mods** (both menu styles). No programming, no rebuild. This page explains the folder, the
`mod.ini` file, the kinds of mod there are today (texture packs and the built-in Ragdoll, Rainbow Blood and Big Head
mods), how to make your own
and how to share it.

Mods only change **how the game looks**. They never change how it plays: the scores, the high scores, online co-op and
the game's own checks are the same with any mod on or off, and in online co-op each PC can use its own mods.

## Quick start

1. Copy the example: `data\mods\example\` to `data\mods\my-mod\`.
2. Open `data\mods\my-mod\mod.ini` in any text editor (Notepad is fine) and change `name`, `author`, `description`.
3. Start the game: Options > **Mods**. Your mod is in the list (Off: the example says `default = off`). Switch it On.
4. Start a chapter. A texture pack is used from the next chapter start.

## Where mods go

- `data\mods\<mod>\` in the game's folder, or
- `mods\<mod>\` in the settings folder (next to `launcher.ini`), or
- a folder you name with `--mods-dir <folder>`.

The folder's name is the mod's ID (what `launcher.ini` and `--mod` use). When two folders have the same name, the
first one in the list above wins.

```
data\mods\my-mod\
    mod.ini                 the manifest (below)
    textures\               PNGs, named by texture ID (texture packs, docs/mods.md)
    models\                 OBJ + MTL + PNG, named by object ID (custom models, docs/mods.md)
```

## mod.ini

A plain text file (UTF-8). `;` or `#` starts a comment line. Every line is optional.

```ini
[mod]
name = My Mod                 ; the name in the Mods list (up to ~22 letters show)
author = you
version = 1
description = One line: what it does (the help line under the list)
type = textures               ; textures (a texture pack: the default), or a built-in mod: ragdoll, rainbowblood, bighead
default = on                  ; on / off: whether a new mod starts switched on (until the player switches it)
models = both                 ; texture packs: arcade / pc / both, the model source the pictures are for
depth = auto                  ; auto / sort / buffer: how this mod's models and pictures hide each other (below)

[settings]
; key = label | default | choice, choice, ... | help line
strength = Strength | Normal | Low, Normal, High | How strong it is

[recolour]
; texture packs: colour rules (docs/mods.md "Recolour rules")
blood = #30C040
```

A `;` after a value (with a space before it) starts a comment too.

| Key | Meaning |
|---|---|
| `name`, `author`, `version` | Shown in the Mods list and its help line |
| `description` | The help line when the mod's row is lit |
| `type` | `textures`: a texture pack (pictures, models, colour rules). `ragdoll`, `rainbowblood`, `bighead`: the built-in Ragdoll, Rainbow Blood and Big Head mods (their code is in the game; the folder gives them their texts and settings) |
| `default` | `off`: the mod starts switched off (good for a mod that changes a lot). Default `on` |
| `models` | Texture packs: which model source the pictures are for |
| `depth` | How the models this mod draws hide each other and the rest. `auto` (default): your custom models (OBJ) per pixel, models that only get your pictures as the game draws them (Arcade Sort). `sort`: the arcade's polygon sort (whole polygons in order; good for flat, layered arcade-style art). `buffer`: a per-pixel depth buffer (good for new, detailed shapes that cross other polygons). Only the models your mod changes are affected |
| `[settings]` | The mod's own settings, shown on its **Settings** page (below) |

### Settings

Each line of `[settings]` is one setting: `key = label | default | choices | help`.

- `key`: a short name (letters, digits, `_`). The value chosen is saved per PC in `launcher.ini` as
  `modset.<mod>.<key>=<value>`.
- `label`: the row's name on the mod's Settings page.
- `default`: one of the choices (the first one if it is not in the list).
- `choices`: the values, comma-separated, in order. Left / Right (or a click) steps through them; the Remake menu style
  also opens them as a list.
- `help`: the help line.

A texture pack has no code to read settings today, so settings matter for the built-in mods (Ragdoll). They are kept
here so a mod's settings page needs no programming once more built-in mod types arrive.

## The Mods page

Options > **Mods** (the Mods tab of both menu styles):

- One row per mod: its name, **On / Off** (a click or Enter switches it). Under it, grey: what it is (texture pack or
  built-in mod), who made it and its version. The help line shows its description.
- **Settings** (under a mod that has any): its own page: the mod On / Off again at the top, then its settings;
  **Defaults** puts them back.
- **Move Up** (texture packs, when there are two or more): the load order. Mods that are On come first, by priority; when two texture packs replace the same
  picture, the higher one wins.
- Everything is saved in `launcher.ini` (`mods=` the On mods in order, `modsoff=` the Off ones, `modset.` the
  settings). Mods are per PC: nothing is sent online.

Command line: `--mod <mod>` (repeatable) uses only those mods for this run (`--mod ragdoll`); `--mod-set
<mod>.<key>=<value>` sets one setting for this run (`--mod-set ragdoll.friction=Rough`); `--no-mods` uses none.

### Depth (advanced)

The arcade board has no depth buffer: it sorts whole polygons. The game draws the arcade models the same way
(**Arcade Sort**), which is what the arcade shows; a per-pixel depth buffer only adds small glitches there (clothes or a
door showing through), so there is no menu row for it. PC models always use a depth buffer, and so does the cut-scene
viewer's Free Cam. For testing, the whole game can still be switched: `drawdepth=sort` / `drawdepth=buffer` in
launcher.ini, or `--draw-depth sort|buffer` on the command line (default `sort`).

A mod's own items follow its `depth =` instead, whatever that setting says (not in the Free Cam: per pixel there). A
"buffer" item drawn among Arcade Sort models takes its depth from its own shape, in the same order as the arcade
polygons around it; a "sort" item among Depth Buffer models takes its polygons' arcade sort keys whole. Custom models have
no arcade sort keys, so `sort` makes no difference to them: their depth always comes from their shape.

## Texture packs

A texture pack (`type = textures`, the default) replaces pictures and models and recolours textures: the full guide is
[docs/mods.md](mods.md) (exporting your own textures, texture IDs, HD pictures, colour rules, custom OBJ models).

## The Ragdoll mod (built in)

Like the HOTD Remake: when a creature dies, its body falls as a **ragdoll** instead of playing its death motion, and you
can keep shooting the body to push it along the floor until it comes to rest. Off by default:
Options > Mods > **Ragdoll: On**.

### What it does, step by step

1. **A death.** When any creature starts dying, its drawn body becomes a ragdoll built from its own body parts:
   - the zombies (plain, axe, kit / drum, iron-ball, chainsaw, the ones that drop or jump down, `Zombie_Dead`'s
     second death style), the dogs (Kenfis, 20 parts), Ebitan (ceiling / sliding, 10 parts), the frogs (Bouere),
     the agile ones (kind 70: the monkeys, kage, Rubin, Drake, Moody, kageta), Taran, the small 11-part creatures
     (types 41-43), the axe zombie of the researcher scenes;
   - creatures the game draws without a body, from their own models: the **Murrers** (16-18 models, each one a
     part hanging off the nearest one drawn before it), the **bats** (also the Hangedman's swarm when it falls),
     rats, the slugs, the insects, the falling frogs (props) and the **severed heads** the game throws: small ones (up
     to 3 models) are one rigid body that tumbles.
   The bosses keep their own deaths; the players, Sophie and the researchers / hostages are never ragdolls.
   A creature that dies in the air, on a wall or a ceiling (bats, Moody, Ebitan, Taran) lands where the game's own
   falling body lands (the lowest the game draws it), or on the room's floor if the game removes it in mid-fall.
2. **The body.** Each body part (about 17: hips, chest, head, upper and lower arms and legs, hands, feet) becomes four
   small points: its joint and three points along its own axes. The four keep their distances (the part stays rigid).
   A part's joint keeps its place on its parent part (a ball joint), and a limb cannot fold back onto its parent
   further than a 50 degree bend (a simple angle limit).
3. **Moving.** Each frame every point keeps its speed (Verlet integration), falls by the arcade's own gravity, then
   the distances are put right 12 times over (position-based dynamics). It starts with the speed of the death motion
   at that moment, so a zombie shot while lunging falls forward.
4. **The floor.** The floor is the world's floor, step or slope under each point (with a window; else the
   creature's own height, where the game keeps it; never more than 8 units below that). A point on the floor stops
   falling and loses a little of its sliding speed each frame (the **Friction** setting); a slow one stops (no
   creeping), and while most of the body lies, its other points slow down with it.
   **Walls:** the scene's standing surfaces (walls, pillars, railings, kerbs: the world's triangles that are more
   upright than about 37 degrees, of the place the game draws now) stop the body's points a little off them; the
   speed into the wall stops, along it is kept, so a body pushed against a wall slides along it instead of into it
   (with a window: the view keeps them; the headless runs have none). `HOTD_RAGDOLL_NO_WALLS=1`: as before (testing).
   **Moving props:** the door-sized models the game draws outside any creature (doors, gates, the opening's car ...)
   are walls and ground too, where they are drawn each frame (an opening door pushes a body along). The walls are
   built at every chapter's load whether the mod is on or not, so switching it on mid-game works at once.
   **Other bodies:** each bone (a part's joint to its parent's) is a capsule as thick as the body's size gives; two
   dead bodies that meet are pushed apart (half each, at most 0.4 units a frame; a resting body stays put and the
   other one lies on it), so bodies pile up instead of sinking into each other.
5. **Shots.** Each shot is a line from your eye through the crosshair. The nearest dead body's bone on that line is
   pushed along the shot: the whole body a little, the bone hit more, and a little upward so it slides instead of
   digging in. Keep shooting and it keeps sliding.
6. **Resting.** A body that has (nearly) stopped moving for half a second rests (no creeping, no jitter) until a
   shot wakes it.
7. **Going.** The game removes a dead body after its death motion and 40 frames of blinking. The ragdoll draws the
   body all the time (no blinking) and stays for **Bodies Stay** after that, then fades out over half a second.
8. **Drawing.** Each part's place is rebuilt from its four points, and every model the game drew for that part (and its
   shadow, and the arcade's generated belly skins) is drawn there instead.

**Visual only.** The ragdoll runs after the game's frame and changes only what is drawn. The dead creature's own logic
goes on exactly as before (its death motion, blood, score, the hit test), so the scores, the high scores, the game's
checks and online co-op are the same with the mod on or off. A body you push is not "really" there for the game: a
shot at it does to the game exactly what it did without the mod.

### Its settings

| Setting | Choices | What it does |
|---|---|---|
| Friction | Rough, Normal, Smooth | How quickly a pushed body stops sliding (Rough: soon, Smooth: further) |
| Bodies Stay | Arcade, 4 s, 8 s, 15 s, 30 s | How long a body stays after the game would remove it (Arcade: gone with the game's own) |
| Gravity | Low, Normal, Heavy | How fast bodies fall (Normal: the arcade's own) |
| Dismemberment | On, Off | On: as the arcade (heads / limbs come off). Off: the pieces the game throws are not drawn and the severed parts stay on the body (dead or alive), so the whole ragdoll shows. A mod setting: only what is drawn changes (the game still counts the hit as before) |

Since 10-28 (ragrevert, the user's choice) the mod is v2 again (the 10-27 night's version): the v3 tuning, the round /
soft shadows, the cut-scene freeze / fade, the researcher ragdolls, the joint limits / self-collision and the
ragdolls' own per-pixel depth are gone. Only the Dismemberment setting stayed. A mod folder from an older version
(its mod.ini without Dismemberment) gets the built-in's newer settings.

The mod's texts and settings come from `data\mods\ragdoll\mod.ini`; you may reword them, change the defaults or leave
choices out. Without that folder the game uses the same built-in values.

## The Rainbow Blood mod (built in)

GitHub #8. The blood cycles through the colours of the rainbow. Off by default: Options > Mods > **Rainbow Blood: On**
(or `--mod rainbow-blood`).

- **Arcade models:** everything the game draws in the blood colour (polygon palette entry 1023: the blood bursts, the
  drips, the gore, the splats on the floor and walls, the blood marks, the blood on the creatures' parts). It takes
  effect at once.
- **PC models:** the blood texels of the PC's texture pages (those that differ between `B_RED.VMC` and `B_GREEN.VMC`, the
  same texels Blood Color: Custom tints): the bursts, the gore and the wounds and faces painted on the creatures. The
  pages are made at the chapter's load, so switching the mod on or off takes effect at the next chapter start (switched
  off mid-chapter, the blood is plain red until then).
- The colour moves with the game's frames (the same picture at the same frame); in the grey (sepia) scenes the blood
  stays grey, as the game draws it.
- **The colours are the game's own rainbow** (user 2026-11-03): the 90-colour table 0xAB120 that the palette animation
  (palette_animation 0x25310) cycles on the computer screens behind Curien in Chapter 4, blended between its steps so
  any speed stays smooth (a plain hue if the table is missing). The blood entry's colour is drawn directly at the
  polygon's brightness (each channel between its colour rows 0 and 31), not as 5-bit values through the colour rows:
  the rows between are other colours and made the colour jump (the user's clip). A body's parts cycle as one colour.
- **The painted wounds (arcade, user 2026-11-03: all the blood):** they are not in the blood colour entry; they come
  from the colour rows the event scripts set by the blood colour (GGSET: a row whose red and green lists differ,
  `Runtime::bloodRows`), the rows the game's own green blood recolours. Every shade of those rows (the pale pink inside
  the wounds too) takes the rainbow per pixel in the renderer, as Blood Color: Custom recolours them (below): each
  creature's parts in one colour, each splash its own. That covers the people's and the bosses' wounds too, wherever
  they are blood rows. Their own colours that are not blood rows stay (Sophie's red jacket, the researchers' painted
  blood pools). **The Chariot** is the exception: its whole body is drawn in the blood rows (the game's own green blood
  turns all of it green), so it keeps its red (types 35 / 36). A clearly-red-pixel test (saturation 55 %+, green and
  blue alike) is the fallback for the enemies' pixels the rows do not colour.
- **Creature Blood** (the user's ideas, 2026-11-03): by default (Rainbow Cycle) the enemies' wounds cycle with the rest.
  With **Own Colour Each**, when an enemy appears it gets a colour of its own, kept for its whole life: the wounds painted on its skin, its blood
  texels (PC) and the blood on its parts. The bursts, drips, splats and gore around it cycle as above. A creature
  "appears" the first frame its body is drawn (or drawn again after 2 s away, or with another type in the same slot);
  its colour comes from its body slot and that frame, so every run gives the same colours. Enemies only: the people and
  the bosses cycle. **Off**: the wounds as the game paints them.
- **The 2D blood** (rainbow2, 2026-11-03): the stippled red hit marks over the play area when a player is hit (the
  arcade's sprites 0x28..0x2D, the PC's 0x47E..0x483: these are the "translucent red splashes" in front of an attacking
  creature: 2D, not models) by their red share (the grey under it kept), their hue moving across the screen with Each
  Splash; the PC's GAME OVER screen's blood (the sprite texels that differ between the blood sets, found once when
  first drawn with the mod on: about a second's pause). Not in the grey (sepia) scenes.
- **HUD & Crosshair** (user 2026-11-03): Crosshair: the players' sights cycle through the same rainbow, player 2 half a
  cycle after player 1, players 3 / 4 a quarter between, also with Colours: Together, so they stay apart. HUD: the
  player HUD (the bullets, the 1P / 2P frames, the torches, the score line, RELOAD, PRESS START, CONTINUE?, GAME OVER;
  not HOLD YOUR FIRE!) by each pixel's brightest channel (its shading kept), the screen's left half in player 1's
  colour, the right half in player 2's. Both: the two. The players 3 / 4 HUD of the 4P mode keeps its colours.

| Setting | Choices | What it does |
|---|---|---|
| Colours | Each Splash, Together | Each Splash: every splash, drip and body part its own colour (by where it is drawn), all cycling. Together: all blood one colour at a time |
| Speed | Still, Slow, Normal, Fast | How fast the colours change (Normal: one cycle in 5 s, like an RGB PC's rainbow wave; Slow 10 s; Fast 2.25 s, the Chapter 4 screens' own speed) |
| Creature Blood | Rainbow Cycle, Own Colour Each, Off | Each enemy's painted wounds and blood: cycling like the splashes (default), its own colour for its whole life, or as the game paints them (the blood on its parts then cycles like the splashes) |
| HUD & Crosshair | Off, Crosshair, HUD, Both | The players' crosshairs (each player its own phase) and / or their HUD in the rainbow too (default Off) |

While it is on it replaces the Blood Color setting's colour (also a texture pack's `blood =`); the game logic keeps the
player's setting. **Not covered:** blood painted into the textures in colours that are not blood rows (arcade: the dead
researchers' pools in Chapter 1, the people's bloody faces); the Chariot's body (kept red on purpose, above; on the PC
models its body is blood texels and cycles); Survival Mode's cut creatures drawn in another chapter's texture set (their
rows are not GGSET's: a row the chapter marks as blood may cycle there). `HOTD_CREATURE_BLOOD_TRACE=1` logs each new creature and its hue.

**Blood Color: Custom** on the arcade models (user 2026-11-03), with the mod off: the creatures' painted wounds take
the custom colour too, not only the bursts and the gore (entry 1023). The rows of the colour translation the event
scripts set by the blood colour (GGSET: a row whose red and green lists differ, `Runtime::bloodRows`) are recoloured
to it, every shade (the pale pink inside the wounds too), as the game's own green blood does; the red-pixel test above
covers what is left. The PC models' custom colour is the load-time tint of their blood texels, as before.

## The Big Head mod (built in)

GitHub #10. The classic cheat: the creatures' heads drawn big. Off by default: Options > Mods > **Big Head: On** (or
`--mod big-head`). Works the same with the arcade and the PC models, at once.

- The head is the body part the game turns to look at things (part class 1 in the creatures' part role table). It and
  everything under it in the part tree (jaw, hair, hats) are scaled about the neck joint; their shadows too.
- **Hit detection is the game's own and does not change:** a big head is hit where the normal head would be (bigger
  hit areas would change the game's logic, the scores and online co-op; mods are visual only).
- **Bosses** (setting Bosses, On by default): each boss's head part grows like the others': the Chariot (its skull, with
  and without the armour), the Hangedman's fly-by and fight, the Hermit, the Magician. Their weak points stay where they
  are. Off: the bosses keep their size.
- **People** (setting Heads: Everyone, the default): Rogan and G in the cut-scenes, Sophie, the researchers, Curien.
  Heads: Enemies: the creatures only.
- Not scaled: the heads the game throws when it blows one off (they are their own models, no longer on the body), the
  dead researchers lying in the opening scenes (scene models, not bodies), creatures drawn without a body (bats,
  Murrers, rats).

| Setting | Choices | What it does |
|---|---|---|
| Head Size | 1.5x, 2x, 3x | How big the heads are |
| Heads | Enemies, Everyone (default) | Everyone: the people too |
| Bosses | On, Off | The bosses' heads too |

Both mods' texts and settings come from `data\mods\rainbow-blood\mod.ini` and `data\mods\big-head\mod.ini` (the game
has the same built-in values without them). Pictures for checking: `tools\cosmods_shots.ps1` (Chapter 1 offscreen, no
mods / each mod / both, arcade and PC models).

### Rainbow Blood: the Magician's afterimages

Rainbow Blood also colours the Magician's afterimages (Chapter 4's boss; v1.3.1, GitHub #54), one setting per phase:
**Magician Afterimages 1** (as he flies around) and **2** (his swoops and lunges). The arcade game draws them in one
polygon palette colour (entry 583) that it sets itself: blue (0xFEC0), then red (0x801F); the PC game keeps them blue
(its own textured model).

- **Rainbow** (the default): each afterimage its own colour, the next one the next, from the Chapter 4 computer screens'
  colours (palette_animation's 90-colour table 0xAB120), kept as it fades, as the creatures' own colours. Speed sets
  how far apart two afterimages' colours are.
- **Original** (the game's), Red, Orange, Yellow, Green, Cyan, Blue, Purple, Pink, White, or **any colour** as `#RRGGBB`
  (`launcher.ini`: `modset.rainbow-blood.afterimage1=#FF00FF`, or `--mod-set rainbow-blood.afterimage1=#FF00FF`).

Arcade models: the palette entry per afterimage; PC models: their texels in that colour, at their own brightness.

## Custom crosshairs

Your own crosshair pictures: put PNG files in the **crosshairs** folder in the settings folder (next to `launcher.ini`;
the portable copy: `settings\crosshairs\`), or in `data\crosshairs\` in the game's folder. Options > Video >
Crosshairs (the Firing Range): the **Crosshair** row's help line shows the settings folder's one (made on the first visit).
Then pick yours on the Crosshair row ("Custom: <name>").

- A loose PNG (`skull.png`): one choice named after the file, tinted with the player's colour.
- A folder with `crosshair.png`: one tinted choice named after the folder; with `p1.png` (and `p2.png`) instead: drawn
  as they are (`p2.png` for player 2).
- White on transparent works best for the tinted ones.

## Limits (today)

- **No code mods yet.** A mod is pictures, models, colour rules and the settings of the built-in mods. You cannot add
  new behaviour with a script.
- **Visual only.** Nothing a mod does may change the game's logic, so mods never need to match between online players.
- The Ragdoll mod: every creature that dies except the bosses (the Chariot, the Hangedman, the Hermit, the Magician)
  and the gold frog (it leaps away, it does not die); the loose creatures (bats, Murrers ...) have no joints from the
  game, so their parts are joined to the nearest model (a Murrer can look a little stiff); bodies do not push living creatures (or the players' partners); a prop
  counts when it is door-sized (10+ units high, 6+ wide); the floor is
  the drawn world's (a body sliding off a ledge drops at most 8 units; without a window: flat at the creature's
  height).
- Texture packs: see the limits in [docs/mods.md](mods.md) (2D sprites not covered, the atlas size).

## Sharing a mod

A mod is its folder. Zip it with its folder (`my-mod\mod.ini`, `my-mod\textures\...`) and tell players to unzip it into
the game's `data\mods\` (or the settings folder's `mods\`). **Share only what you made**: never pictures exported from
the game's files (`--export-textures`) as they are, nor the game's files themselves.

## For developers

- `src/assets/mods.*`: `findModPacks` reads every folder's `mod.ini` (`type`, `default`, `depth` with
  `parseModDepth`, `[settings]` with `parseModSetting`); `findMods` adds the built-in Ragdoll manifest when no folder
  has `type = ragdoll`. `TexturePacks::build` fills `StageReplacements::modelDepth` (model key -> depth mode, by
  `modDepthFor`: a custom model's pack, or the pack whose picture / colour rule a model's polygons draw); the renderer
  copies it to `GpuStage::modelDepth` and `Renderer::draw` sets each such item's `uKeyless` / `uHybrid` (render/renderer.cpp).
  `hotd --mods-test` checks it (`modDepthTest`: a small stage with a custom model, depth auto / buffer / sort).
- `src/main.cpp` `applyMods`: the On list (launcher.ini / `--mod`), texture packs to the renderer (`texturePacks()`), the
  Ragdoll mod's on / off and settings to `game::ragdollConfigure`. `modsRows` / `modSettingsPage` build the Mods page
  and each mod's Settings page (`game::Frontend::Extra` rows: both menu styles draw them, text through the lang lookup).
- cosmods: `findMods` also adds the Rainbow Blood / Big Head manifests (`builtinCosmetic`) when no folder has `type =
  rainbowblood` / `bighead`; `applyMods` hands them to `game::rainbowBloodConfigure` / `game::bigHeadConfigure`
  (src/game/cosmods.*). Rainbow Blood is a renderer colour: `RenderOptions::rainbow` / `rainbowPhase` (set by
  `GameView` each frame), the vertex shader's `uRainbow` (arcade: `pal == 1023` gets the hue; per item from `uOffset`
  or one for all), the fragment shader's PC branch (texels of alpha `kRainbowAlpha` 250 times the hue). The PC pages
  are marked at the load: `GameView::prepare` gives the PC source `kBloodCustom | kBloodRainbow | 0xFFFFFF` (the custom
  path in white, `pc_loader.cpp` sets the marked texels' alpha). Big Head: `bigHeadFrame` at the end of
  `Runtime::frame` (after the Ragdoll mod and the actor swap): each draw with `DrawModel::part` whose part or an
  ancestor (BodyPart +0x20) has class 1 (the part role table 0x1184E0, byte 3) is scaled about that head's pivot (the
  head draw's translation; shadows their own). The Chariot and the Hermit draw their parts through their own hooks
  (0x9A040 / 0x9A280, 0x87560), never body_part_draw_hook: `DrawModel::bossPart` holds those parts (`part` stays 0, so
  the Ragdoll mod leaves the bosses alone). The Gallery (`Gallery::draw`, `bigHeadTypeOn`) scales its placements the
  same way. With both off nothing changes (no draw-list pass, `uRainbow` 0).
  Creature Blood: `creatureBloodFrame` (after `bigHeadFrame`) keeps a hue per enemy body (first drawn frame, a hash of
  the body address and the frame); `GameView` puts `creatureBloodHue(body)` in `DrawItem::bloodHue` -> the per-item
  `uBodyHue` (vertex: the item's hue instead of the cycling one; fragment, arcade: `woundTint` on the colour after the
  monitor curve). rainbow2: `RenderOptions::bloodRowMask` (the view, from `Runtime::bloodRows`) -> the fragment
  shader's `uBloodRows`: a pixel whose base colour row is a blood row gets that row's grey (lowest channel at this
  brightness) plus its colour share in `vRainbow`; `woundTint` only where not all three channels were blood rows.
  `uBodyHue` 4..5 (people, bosses): blood rows, no `woundTint`; 6..7 (Creature Blood Off, the Chariot:
  `creatureBloodKeepsRows`): neither. The 2D: `Runtime::cellHud` (sprites.cpp: 1 a per-player HUD sprite, 2 a hit
  mark), `Hud::rainbow2D` (arcade: those cells composed alone, the picture's matching pixels recoloured),
  `PcSprites::setTint` / `setBloodTexelTint` (PC); the sights: the HUD shader's `uTint`; the colour on the CPU:
  `game::rainbowColour` (the same table and phase).
- `src/game/ragdoll.*`: the Ragdoll mod. `Runtime::ragdoll_frame` runs at the end of `Runtime::frame` (after the
  camera is known) and only reads the address space; it changes `drawList` / `skinDraws`. `DrawModel::part` (set by
  `body_part_draw_hook` and kept for the part's shadow) tells it which part drew each model. A netplay rollback's
  re-run of frames already stepped only draws (no second step). `HOTD_RAGDOLL_TRACE=1` logs the ragdolls and the
  shots that hit them; `HOTD_RAGDOLL_AIM=N` (pictures only: it changes the play) makes `--autofire` shoot the dead
  bodies on the screen. ragall: `HOTD_RAGDOLL_KINDS=1` logs every drawn creature's routine / state changes (type,
  routine, parts) to find a kind's death; `Ragdolls::dying` lists the death routines and the death states (kind 70,
  frogs, bats, kind 65 through their state tables), `looseCreature` the routines of the creatures drawn by their task.
- ipolish: `GameView` (game/view.cpp) builds `RagdollWalls` at every chapter load (the world's placements) and keeps
  the door-sized models' triangles (`propWalls_`); each drawn frame those the game drew with `body == 0` are added past
  the world's objects (`staticWalls_`) as active walls. `Ragdolls::collideBodies` pushes bodies apart before the steps;
  the trace's `bodies N` counts the frames a ragdoll was pushed.
