# Cut content in the arcade ROM and the PC files (inventory, 2026-10-14)

Checklist: the fan wiki's "List of unused content in The House of the Dead" (The Wiki of the Dead, read 2026-10-14).
Nothing is restored in the game; this is only what exists (the Gallery shows the cut creatures with their textures:
"Textures of the cut creatures" at the end). Restoring is a later, optional mod (ROADMAP.md, "Future (after release):
restored cut content").

**Survival Mode (2026-11-03, [survival.md](survival.md) "Cut creatures"):** five of the cut creatures fight in Survival Mode
(never in the normal game): Disiprin, Debu and Neozom with their own standard-enemy tables, which the ROM has complete
(motion list, attacks, attack choice, death motion: the game only never spawned them); Syndy and Baba with their
relatives' (types 28 BO_neil and 29 BO_siriru; same joint count). MrG, Lubin and Hiru are still out (other joint counts).

Sources checked: arcade program ROM (maincpu), main_data (models, names, motions), the geometry ROMs (polygons), the
event scripts (PC EVTALL.RBH; the arcade runs the same scripts), the PC files (ART\STAGEn\*\*.RBH object tables,
THOTD.EXE). Object IDs are shared by both versions. Part names: main_data pointer table 0xEE4EB0 ([names.md](names.md)).
"Drawn in the replay" = seen in the recorded full-game MAME run (`private\arcade_texsets.txt` / `arcade_states.txt`).

## Summary

| Item (wiki) | Arcade ROM | PC files | Code / data that uses it |
|---|---|---|---|
| baba (BO_pbaba, type 66) | skeleton 18 joints, 11 part models (0xCD2-0xCDC `pbaba_a*`, all with polygons) | no meshes | no motions of its own; no stance / walk list; never spawned |
| syndy (BO_syndy, type 10) | 18 joints, 17 parts (0x14C8-0x14D8 `syn_*`, plus 43 `bagbag01a..` models 0x14DA.., probably her mallet / bag pieces) | no meshes | one motion **MO_synd_demo**; never spawned |
| MrG (BO_mr_g, type 5) | 19 joints, 18 parts (0xAB1-0xAC1 `mrg_*`) | no meshes | 4 motions **MO_mrg_aruki / kiai / naguri / taore** (walk, shout, punch, fall); never spawned |
| disiprin (BO_disiprin, type 52) | 18 joints, 17 parts (0xA87-0xA97 `dis_*`) | no meshes | stance / walk list in table 0xC6B60 (plain zombie set); never spawned |
| lubin (BO_lubin, type 39) | 16 joints, 15 parts (0xA98-0xAAF `lubin_*`, 25 names with damage variants `_dam00a`..`_dam02a`) | no meshes | no motions of its own (it would use Drake's MO_dr_*?); never spawned |
| hiru (BO_hiru_b, type 75) | 9 joints, 8 segment models (0x2B6-0x2C5 `hiru_body*_CT000a`, each with a `_DD` shadow model, plus `hiru_bodyey`) | no meshes | **6 motions MO_hiru_a / blur / blurb / walk / y / yy** (the wiki's "6 animations"); never spawned |
| neozom (BO_neozom, type 56) | 18 joints; 16 of 17 parts are Sam's (`zonbichana_*`), own chest `sam_mune` | Sam's parts only | stance / walk list (Sam's); never spawned |
| debu (BO_debu, type 12) | 18 joints, 17 parts (0x1249-0x1263 `zonbisan_*`, 33 names with damage variants) | no meshes | stance / walk lists (Bentley's z_ben_* and the chainsaw z_chn_* set); never spawned |
| Hermit fly head | **`boss31_kao` 0x1AC6** (86 polygon links): a second Hermit head next to the used `boss3_kao` 0x1AD9; in no skeleton, never drawn in the replay | not in the PC files | none found. **Rendered (Gallery, 2026-10-21):** with the Hermit's texture area (11) it is a textured head with two large red bulging eyes and a scaly top, so most likely the fly-eyed head |**Built-in mod (2026-11-09):** Hermit Fly Head Mod (`--mod hermit-head`, docs/modding.md) draws it on the Hermit in the fight (arcade models) |
| room_6bbb (Chariot room with the fireplace passage) | **0x111E `room_6bbb`** (216 links), placed in the Chapter 1 table (0xBEF00) at (248.5, 26.1, 565.1) together with 0x111C `room_6b` and 0x111D `room_6bb` (an alternate-state group, `arcade_states.txt` group 0); the replay only ever drew 0x111D | not in the PC files (the PC has only 0x111C, WORLD1) | the game picks one member of the group; no code found yet that would pick 0x111E. No ladder model or ladder motion for the Chariot found (Thomas / Sophie have ladder motions MO_k_hasigo*, MO_p_hasigo, used elsewhere) |
| PN_room_6bb_zsof (bad-ending room) | **0xAC4 `room_6bb_zsof`** (231 links), not placed anywhere | not in the PC files | none. Its counterpart **0xAC3 `room_1b_zsof`** (the foyer version, 740 links) is placed at the origin of the Chapter 1 table and is in PC WORLD6 (the ending scenes): the final game uses the foyer |
| Environment mapping (not on the wiki; found 2026-10-15) | a complete run-time environment-mapping system, maincpu cluster 0xA07A0 (`envmap_init`, `envmap_task`, `envmap_request`, strings "EnvMapReq", "TexDataSize Limit Over(envmap)!!"): it would turn an object's normals into texture coordinates every frame (reflective / shiny surfaces). Its only reserved object is **0x1352 `jinmengyo_01`**, the first of the 40 human-faced fish models; plus a developers' test object (`envmap_test_object`, buttons step spin / mode / object) | not in the PC (no code, no strings) | **nothing calls it**: the fish are drawn plainly in Chapter 2 (probably the fish were meant to look wet / shiny). Details: [decompilation.md](decompilation.md) "World drawing" |
| Unused music (arcade sound test 8, 10, 13) | in the sound board's data (68000 sound program + sample ROMs), not checked: we have not decoded the sound board yet | the PC CD has 11 tracks (Music\README.txt); none is unused | the arcade's sound test list is in the operator test menu code (not read) |
| Unused subtitles | both lines present: arcade maincpu **0xCBD21** ("This is the warning from Dr.Curien." / "Stay away from this house! Or else....", the first entries of the subtitle block before "Nobody leaves here alive!") | **THOTD.EXE file offset 0x1DB7FB**, the same two lines first in its subtitle block | not looked up yet which subtitle numbers they are and whether any script asks for them |

Other items on the wiki page (cabinet concepts, character concept art, game titles, the prototype's continue font and game
over screen) are not in this ROM (Revision A) or are concept art only.

## Cut-content mod list

What an optional "restored cut content" mod (ROADMAP, after release) would have to work with. Under the merged plan
these are ported and proven only when the mod is made. "Code" = routines exist in the ROM; "data" = models / motions /
text only (new code would be needed, which is then a mod, not a restoration).

| Item | Code | Data | Notes |
|---|---|---|---|
| Environment mapping (`envmap_init` / `envmap_task` / `envmap_request`, cluster 0xA07A0; test object `envmap_test_object`) | yes (complete system, nothing calls it) | reserved object 0x1352 `jinmengyo_01` (human-faced fish) | the only cut feature with working code |
| Cut creatures MrG (5), syndy (10), debu (12), lubin (39), disiprin (52), neozom (56), baba (66), hiru (75) | none (no set-up, no behaviour; [dead_code.md](dead_code.md): no dead routine refers to them) | skeletons, part models, some motions (MrG 4, syndy 1, hiru 6), stance / walk lists for disiprin, neozom, debu | would need new behaviour code |
| Hermit fly head `boss31_kao` 0x1AC6 | none found | model (86 links) | to confirm by rendering |
| room_6bbb 0x111E (Chariot room with the fireplace passage), room_6bb_zsof 0xAC4 (bad-ending room) | none found that picks them | models | room_6bbb is in the Chapter 1 alternate-state group 0 |
| Unused subtitles (Dr. Curien's warning) | not looked up | text in both versions | |
| Unused music (arcade sound test 8, 10, 13) | sound board not decoded | sample ROMs | |
| Greyscale palette effect (`unused_19c80`) | yes (dead) | - | possibly a cut screen effect (e.g. game over / continue); listed as a leftover in dead_code.md until it is known |

## Details

- **Never spawned:** no enemy record of a creature kind (record kinds 0 / 96 / 98 / 99 / 100 / 103, where the ID's low
  byte is the creature type) uses types 5, 10, 12, 39, 52, 56, 66 or 75, in any of the 7 event lists (Chapters 1-4, the
  two short lists and the 8 ending events). Records of other kinds (e.g. 110, 116, 120, 121, 161, 179) also have small
  numbers there, but those are not creature records (their "positions" are not floats).
- **PC:** THOTD.EXE keeps the full BO_ / MO_ name tables and the arcade's skeletons and motion data (formats.md §23, 672 of
  674 motions identical), so the cut creatures' skeletons and motions (MrG, syndy, hiru) are on the PC too; only their
  meshes are missing (none of their part IDs is in any PC .RBH file).
- **Textures:** each part's model record (main_data 0xE73530 + 16 x ID) has texture point / header addresses, so their
  textures are in the arcade texture ROMs; not checked one by one (needs the texture-set work of formats.md §19-21).
  **Gallery check (2026-10-21, gallery2):** the creatures looked untextured-worthy in every set tried, and the Gallery
  drew them in one palette colour. **Corrected the same day (cutrestore): their textures are there**, see "Textures of
  the cut creatures" below (the attract's area 2 was not tried; the four in chapter areas were missed when judged from
  whole renders). **hiru** is textured coherently in every set (its textures are loaded with the shared ones); only its
  colours change with the stage (greenish brown in Chapter 1, blue in the others), so its real colours are open.
- The other boss parts outside every skeleton (`boss3_syo01-50`, `boss3_kosi_01-10`, the second leg sets) are drawn by the
  Hermit's own code (most were drawn in the replay), not cut content.

## Textures of the cut creatures (cutrestore, 2026-10-21)

**Result: all seven have their textures in the arcade ROM; the Gallery now draws them** (`src/game/gallery_cut.cpp`:
area and colour rows per type; **U** on the Gallery page switches back to the untextured look). Nothing was invented:
every texel comes from the ROM at the place the creature's own texture headers point to; only the colour rows are
chosen (see Colours).

**How it was found.** Each part's model record (main_data 0xE73530 + 16 x ID) gives its texture point and header
lists in the texture ROM (formats.md §13). Every textured polygon of every cut part points into **sheet 1** (the area
sheet; sheet select bit 1), e.g. MrG's head `mrg_matama` 0xAB3: 38 polygons on a 128 x 128 texture at (1568, 96),
chest `mrg_mmune` 0xABB on (128, 384) and (0, 384). Sheet 1 is texture RAM 1, which the area loader fills from one of
main_data blocks 1-12 (formats.md §17; `ArcadeSource::buildTextureRam`, ported to Python for this). For each creature
the polygons' texture coordinates were drawn over the same rectangles of all 12 area sheets and looked at: in one area
the outlines land on a face (eyes, mouth where the face polygons are), a chest, sleeves, boots; in the others on walls,
doors, signs. Then each was rendered in the Gallery with that area's set (pictures below).

Sources searched besides the 11 chapter areas: the attract's area 2 (**this is where three of them are**), block 0
(the shared sheet, sheet 0), the 34 extra 128 x 128 tiles (main_data 0xE00000-0xE43FFF; the per-area tile table at
maincpu 0xC1610 uses 33 of them, the unused one, 0xE04000, is a brick wall), main_data blocks 13-31 (block 13 empty,
0xE00000+ tiles and tables, 0x1000000+ motion data: no texture sheets). The program's texture lists (maincpu 0xA99E0,
13 block addresses; 0xC1610 tiles) are plain copies, there is no packed texture data. **PC:** none of the parts has a
mesh in CREAT1 / CREAT2.RBH (Neozom's 16 Sam parts excepted), and none of BIGGER.VMC's 421 pages (all looked at:
1-337 menus / sprites / credits, 512-546 world, 547-583 creature atlases, 584-595 effects and world) shows any of
these textures (the masked faces, Lubin's lettering, the laced suit): the PC port never had them.

| Creature | Texture area | What the textures show | Shared with used models? | Confidence |
|---|---|---|---|---|
| MrG (BO_mr_g, 5) | **2** (the attract's sheet) | a masked head (black hood, eye holes, stitched mouth), a muscular bare chest, gloved hands, dark trousers: a masked wrestler | 59 % of its polygons sit on rectangles other models use in other areas (Kageo, Moody, Bentley variants); in area 2 those places hold his pictures | high |
| Lubin (BO_lubin, 39) | **2** | an officer's cap and a face with lettering tattooed on the cheek, a chest covered in lettering, claws | (as MrG; 11 % unique places) | high |
| Disiprin (BO_disiprin, 52) | **2** | a woman in a black laced body suit (laced head mask with a gas-mask mouth, laced sleeves and boots): "discipline" | 41 % unique places | high |
| Syndy (BO_syndy, 10) | **7** (Chapter 2) | a woman in black leather (bikini top, masked face with straps), with a stick or mallet (her `bagbag` models) | 62 % unique places | high |
| Debu (BO_debu, 12) | **5** (Chapter 2) | Bentley's own textures (fat bloody belly, bald head): `zonbisan_*` uses Bentley's (`bentry_*`) texture places for 98 % of its polygons; only (448, 576) is its own | yes, Bentley's | high (it is an earlier / alternative Bentley) |
| Neozom chest (BO_neozom, 56; `sam_mune` 0x139A) | **6** (Chapter 2) | a pale chest with three claw slashes; the other 16 parts are Sam's (sheet 0, as in the game) | 31 % unique places | medium (one part; area 7 also has a plausible chest at the same place, area 6 fits the outlines better) |
| Baba (BO_pbaba, 66) | **3** (Chapter 1) | a skull-like screaming face, a brown dress / coat, bony arms and legs | its face place (448, 800) is also `haguuu`'s (a face used in area 3); its body places (384, 384) / (320-416, 800) are its own and hold a matching torso and limbs only in area 3 | medium |
| Hiru (75), the Hermit's head (0x1AC6) | (unchanged) | already textured (sheet 0 / area 11) | | |

**Colours.** The texels and the palette entries come with the area; which colour **rows** the event scripts would
have swapped in for these creatures is not known (no spawn record). The Gallery uses the rows of a creature the game
draws in the same area (Bentley's for Debu, Rubin's area-7 rows for Syndy, Saruzou's for Baba, a researcher's area-6
rows for Neozom; the area-2 trio: the zombies' usual "7:81 9:28"). The rows only change the tint of some colours
(skin, cloth), not the pictures. These are the guessed part.

**Why the game never shows them:** no event record spawns these types (see Details), and the attract's area 2 is only
loaded for the attract intro / prologue, where none of them appears. The pictures in area 2 at their texture places are
leftovers: the attract block was built while MrG, Lubin and Disiprin were still in the game.

Pictures (private): `private\changelog\img\2026-10-21_cut_<name>_before.png` / `_after.png` for mrg, syndy, debu,
lubin, disiprin, neozom, baba, hiru, head (arcade at 0 / 120 / 240 degrees and the PC source).
