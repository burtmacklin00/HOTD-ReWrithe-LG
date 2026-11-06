# Formats and reverse-engineering notes

What is already known about the 1998 PC port, collected from the reference install's notes
(`PATCHES.md`, `README-MODERN-PC.txt`, `ROADMAP.md`, `Music\README.txt`,
`_project\launcher\HOTDLauncher.cs`, `_project\tools\cuelog.ps1` and `cuelog.log`) as of 2026-10-07.
Everything here was found while patching the PC game; nothing here comes from the arcade ROMs yet.

Labels: **confirmed** = verified in-game or with a debugger. **observed** = seen in logs or files, not fully explained.
**unknown** = not looked at yet.

## 1. PC install layout

Sizes and SHA-256 hashes are from the reference copy (`C:\Games\HOTD`). Other PC releases may differ.

| File | Size | SHA-256 | What it is |
|------|-----:|---------|------------|
| `THOTD.EXE` (original, `THOTD.EXE.orig`) | 8,855,552 | `9FA13C200737E690ACFE52497422DAFF4E254844805A8DD199A1926304D94613` | The game, dated 22 May 1998. All addresses below refer to this build. |
| `ART\STAGE1\WORLD\WORLD1.RBH` | 830,560 | `98A9FB1C…E15286` | Chapter 1 world (by name; contents unknown) |
| `ART\STAGE2\WORLD\WORLD2.RBH` | 670,568 | `A13A8EA7…CC462D` | Chapter 2 world |
| `ART\STAGE3\WORLD\WORLD3.RBH` | 473,240 | `014276FB…88CE63` | Chapter 3 world |
| `ART\STAGE4\WORLD\WORLD4.RBH` | 224,708 | `88896E64…444504` | Chapter 4 world |
| `ART\STAGE5\WORLD\WORLD5.RBH` | 551,964 | `50D39D97…38AD7A` | Stage 5 (stage index 4/5, see §5) |
| `ART\STAGE6\WORLD\WORLD6.RBH` | 1,096,252 | `FFBD0F6C…95698F` | Stage 6 (stage index 6) |
| `ART\STAGE1\CREATURE\CREAT1.RBH` | 1,144,084 | `4676E737…63CB9C` | Creature models (by name) |
| `ART\STAGE1\CREATURE\CREAT2.RBH` | 2,218,764 | `FA1AAF0A…484B68` | Creature models (by name) |
| `ART\STAGE1\CREATURE\BIGGER.VMC` | 55,211,636 | `56FDC1AD…1968B2` | Creature texture source (see §3) |
| `ART\STAGE1\CREATURE\B_RED.VMC` | 7,508,229 | `488A5D7D…7AB4BC` | Pre-built packed texture set, red blood |
| `ART\STAGE1\CREATURE\B_GREEN.VMC` | 7,509,852 | `324008CB…81550F` | … green |
| `ART\STAGE1\CREATURE\B_BLUE.VMC` | 7,509,599 | `77AA0BA5…C0C4C3` | … blue |
| `ART\STAGE1\CREATURE\B_PURPL.VMC` | 7,510,462 | `4FE4D318…EBB52E` | … purple |
| `ART\STAGE1\CREATURE\B_YELLO.VMC` | 7,509,160 | `4AE92921…4BD297` | … yellow |
| `ART\STAGE1\CREATURE\TEXTURE.VMC` | 55,211,636 | (varies) | Texture cache the game builds itself (§3) |
| `CAM\ALLSTAGE\CAMALL.RBH` | 202,840 | `9C082663…E329650E` | Camera paths, all stages (by name) |
| `EVT\ALLSTAGE\EVTALL.RBH` (original) | 127,536 | `37567B7F…6C726D` | Event scripts, all stages (§4) |
| `SPRITE\SPR.RBH` | 78,372 | `4F3E248B…8A6535` | 2D sprites (by name) |
| `SOUND\GAMESNDS.RBH` | 644,836 | `827603A6…6AA4A7` | Sound effects bank (by name) |
| `SOUND\O_*.RBH` | 150 files, 0.6–100 KB | | One-shot sounds and voices (`O_VOC02` … `O_VOC94`, `O_HELP`, `O_THUNDE`, …) |
| `*.PAL` / `*.TBL` | | | Palette and table pairs: `HOTD`, `MENU`, `TITLE`, `BROWN`, `GREY`, `STAGE1`–`STAGE4`. Contents unknown. |
| `THOTD.CFG` | 396 | | Saved options (§6) |
| `SCORES.CFG`, `CSTATS.CFG`, `DEVS.CFG` | | | Scores, stats(?), Direct3D device choice (`devs.cfg`). Contents unknown. |
| `GAMEBIOS.DLL` | | | Driver for Sega's 1998 PC light gun (game-port adapter, direct port I/O). |
| `WAIT.BMP` | | | Loading picture (by name) |

The game's file loader looks in its own folder first and only then on the CD.

**RBH container:** same PIFF style as VMC (§3): `"PIFF" <size> "RBHF"`, an `RBHH` header chunk, `BODY` chunks and `RELC`
relocation chunks. The world files are worked out (§9). The other RBH types are still unknown, except the "play music" instruction
in `EVTALL.RBH` (§4).

## 2. THOTD.EXE

- Compiler: Watcom C/C++ (code section `BEGTEXT`, data section `DGROUP`).
- Image base `0x400000`.
- Code section `BEGTEXT`: file offset = VA − `0x410000` + `0x400`.
- Data section `DGROUP`: file offset = VA − `0x5A0000` + `0x187400`.
- Calling convention: Watcom register convention. Arguments go in EAX, EDX, EBX, ECX (then the stack), result in EAX.
  Seen in practice: the music play routine takes the track in EAX and the loop flag in EDX; stage load takes the stage index in EAX;
  the sound dispatcher's cue arrives in EBX at its CD-music branch. Windows API calls (for example `mciSendStringA` through the
  import table at `0x13901C4`) use stdcall as usual.
- Command-line switches it parses: `-highres`, `-lowres` (texture set), `-d3d_zbuffer`, `-d3d_nozbuffer`, `-d3d_doublebuffer`,
  `-d3d_triplebuffer`, `-d3d_setup` (parsed, but its dialog is never reachable), `-nfullscreen` (fails: "Couldn't find a valid draw buffer
  to use"), `-cd_nocheck` (hidden), `-nocda` (no CD audio).
- Display: 640×480×16 exclusive fullscreen through DirectDraw / Direct3D (Immediate Mode, `IDirect3D2` era, to be confirmed by logging).
  The window is created 0×0 for exclusive mode.
- Single instance: global atom `TheHouseOfTheDead`, removed only on a clean exit.
- Settings in the registry: `SOFTWARE\Sega\The House Of The Dead`, values `bloodcolor` and `language`
  (HKLM originally, HKCU after patch 5). Only used before `THOTD.CFG` exists.

### Known addresses (VA)

Code:

| VA | What |
|----|------|
| `0x41194F` | Startup CD check (`je fail`; looks for `ART\STAGE1\CREATURE\bigger.vmc` on every CD drive) |
| `0x412000` | Sound dispatcher. Pushes 6 registers and reserves 0x80 bytes (its return address is at `[esp+0x98]`). |
| `0x4120AA` | Dispatcher's CD-music branch, cue number in EBX |
| `0x412C05` | Sends cue 19 (final boss music) |
| `0x4186FB` | Startup check of `TEXTURE.VMC` (exists and same size as `BIGGER.VMC`) |
| `0x4187B5` | Call to `gamebios.dll!gbInitDeviceDriver` (light-gun probe) |
| `0x418AA0` | Stage load, 0-based stage index in EAX |
| `0x41DFCC` | Main menu / ranking: hard-coded music cue 22 (call returns to `0x41DFD1`) |
| `0x410E68` | Unpacks `B_<color>.VMC` into the texture cache (called from `0x4273F0`) |
| `0x4273F0` | Texture cache rebuild |
| `0x427177` | `push 0x80000002` (HKLM) for `RegOpenKeyExA` of the settings key |
| `0x4435C3` | Crash site seen when the window is closed during play |
| `0x4585C9` | New game: `mov [0xDA0CF4],al`, stores the starting chapter (AL = 0 for Chapter 1) |
| `0x45B00F` | Reads the table at `0x5D36D0` (indexed by the mode value `0xDA0CEE`) |
| `0x4C17D8` | Event-script handler for opcode 90 "play music"; at `0x4C17DD` EAX = script PC. Sends the cue from `0x4C17E5`. |
| `0x5130B5` | Sends cue 11 (game over) |
| `0x565BAE` | CD resume: originally `mciSendStringA(saved command)` (patch 8) |
| `0x565C22` | CD pause routine (pause menu and focus loss) (patch 6) |
| `0x565D60` | Play CD track: EAX = track, EDX = loop flag |
| `0x565D88` | In the play routine: last track used "from 12 to 12:mm:ss:ff" (patch 4) |
| `0x565E10` | "CD is playing": non-zero when `status cdaudio mode` contains `playing` |
| `0x575190` | Original D3D device dialog, reachable only through the renderer function table at `0xC66EFC`, never called |
| `0x57A605` | "CD Must be in to run" check |
| `0x57CE5F` | Crash site on window close: reads NULL `[0xDA8980]` (DirectDraw object) |

Data:

| VA | Size | What |
|----|------|------|
| `0x5CBDA8` | 12 × 23 | Music cue table (§4) |
| `0x5CE8C0` | int32 | Language index (written, never read) |
| `0x5CE8C4` | | Set to 0 by `language=JAPANESE` |
| `0x5D36D0` | | Table indexed by the mode value at `0xDA0CEE` |
| `0xC64C00` | int32 | Current CD track (−1 = none) |
| `0xC64C1C` | string | Saved MCI resume command (`play cdaudio from <pos> to <next>`) |
| `0xC64C9C` | int32 | Current track's loop flag |
| `0xC64CA0` | int32 | CD paused flag |
| `0xC66EFC` | | Renderer function table |
| `0xD5B1D4` | 0x18C | Options block, loaded from / saved to `THOTD.CFG` |
| `0xD5B1FC` | int32 | Blood color index (options + 0x28) |
| `0xDA0AB8` | ptr | Event-script program counter |
| `0xDA0CEE` | | Mode value: 4 normally, 0 with `language=JAPANESE`. Read in about 25 places, effect unknown. |
| `0xDA0CF4` | byte | Starting chapter of a new game (0-based) |
| `0xDA7DE0` | 0x80 | MCI return-string buffer |
| `0xDA8980` | ptr | DirectDraw object |

## 3. VMC textures

### File layout (confirmed)

Cracked in a separate session; tool: [tools/vmc_tool.py](../tools/vmc_tool.py) (`export` to PNG pages, `import` back).
All values little-endian.

```
"PIFF" <u32 size> "VVTP"        size = file size − 8
then chunks, each "<tag>" <u32 size> <data>:
  "BODY"  size = 64 + width*height*2: 64-byte page header, then the pixels
  "PACK"  compressed page (B_<color>.VMC only; format unknown)
```

Page header (64 bytes):

| Offset | Type | Value |
|-------:|------|-------|
| 0 | u32 | header size (64) |
| 4 | u32 | page number |
| 8 | u16 | width |
| 10 | u16 | height |
| 12 | u16 | 3 on 337 pages, 0 on 84 (the 0 pages are page numbers 512 and up; meaning unknown) |
| 14 | u16 | bits per pixel (16) |
| 16 | u32 × 4 | channel masks: A `0x8000`, R `0x7C00`, G `0x03E0`, B `0x001F` |
| 32 | u32 | 64 on every page (meaning unknown) |
| 36 | u32 | 0 on 337 pages, 24 on the 84 others (goes with the 0 at offset 12) |
| 40 | 24 bytes | zero on 265 pages; on 156 pages it holds six 4-byte values that look like RGB colors (unknown, kept as-is) |

Page numbers in BIGGER.VMC run 1–595 in ascending order with gaps (421 pages), so they are IDs, not indexes.

Pixels: 16-bit ARGB1555. Bit 15 = opaque flag, then 5-5-5 RGB. `0x0000` = transparent.
5-bit to 8-bit: `(v << 3) | (v >> 2)`. Some pixels have the opaque flag clear but a nonzero color (meaning unknown; the tool reports
how many).

- `BIGGER.VMC`: 421 pages, all 256×256. Export then import gives a byte-identical file (tested).
- `B_<color>.VMC`: `PACK` chunks, compression not cracked yet.

### Release versions (2026-11-02)
The Japanese PC release has an older BIGGER.VMC (54,293,628 bytes, 415 chunks) and SPRITE\SPR.RBH (1479 sprites,
71,868 bytes); its B_<colour>.VMC sets are smaller too. Its pages 1..330 are the US / European release's pages 8..337,
pixel for pixel; the world pages 512..595 are the same. The US / European files added pages 1-7 and sprites 0..160 at
the front (a 640 x 480 picture, a 147 x 54 sprite and the grey 24-pixel font, 2..160); the Japanese sprite i is their
i + 161 (1453 of 1479 records equal). The CREAT / WORLD RBH files are the same in both, so the ReWrithe loaders read the
Japanese files with the US numbering (pc_loader.h pcVmcPageShift, sprites_pc.cpp). The European disc's BIGGER.VMC
differs from the US one in 5 single bits (texels); the French repack and the US disc match the manifest.

### How the game uses the files

- `BIGGER.VMC` (55 MB) is the creature texture source. `TEXTURE.VMC` is a cache of the same size with the blood color baked in.
- At startup (`0x4186FB`) the game only checks that `TEXTURE.VMC` exists and is as big as `BIGGER.VMC`. It never checks its color.
- When the cache is missing, the game rebuilds it in about a second (`0x4273F0` → `0x410E68`) by unpacking `B_<color>.VMC`
  (about 7.5 MB each, so the B_ files are compressed), or by recoloring `BIGGER.VMC` if that file is missing.
  The in-game Options menu forces this rebuild.
- The rebuilt cache is byte-identical whether the launcher or the Options menu triggered it.
- Blood color index order: 0 red, 1 green, 2 blue, 3 purple, 4 yellow (file names `B_RED`, `B_GREEN`, `B_BLUE`, `B_PURPL`, `B_YELLO`).
- The `-highres` / `-lowres` switches select a texture set; how this maps onto the files is unknown.
- For the rewrite: blood color only recolors textures, so a loader can apply it at load time instead of keeping a 55 MB cache.

## 4. Music and the event scripts

### Music cue table

At VA `0x5CBDA8` (file offset `0x1B31A8`), 12 bytes per cue: `{int32 type, char* name, int32 value}` (all types: §30). The music cues are type 3, value = CD track (−1 = no track).
The cue names come from the game. The track column lists what was seen in logs or read from the table.

| Cue | Name | CD track | Plays |
|----:|------|---------:|-------|
| 0 | NULL | | |
| 1 | ALL_CLEAR | | |
| 2 | MUSIC_CLEAR | | |
| 3 | SE_CLEAR | | |
| 4 | BGM_PAUSE | | |
| 5 | BGM_VOLUME | | |
| 6 | BGM_FADE | | |
| 7 | SE_CLEAR_C | | |
| 8 | MUSIC_ADV1 | 2 | Attract / title demo, loops |
| 9 | MUSIC_STG1 | 3 | Chapter 1, loops |
| 10 | MUSIC_STG2 | 4 | Chapter 2, loops |
| 11 | MUSIC_OVER | 5 | Game over, once |
| 12 | MUSIC_BOSS1 | 6 | Boss, loops |
| 13 | MUSIC_STGCLEAR | 7 | Stage clear, once |
| 14 | MUSIC_ENDING | 8 | Ending / credits, loops |
| 15 | MUSIC_STG3 | ? | |
| 16 | MUSIC_STG3_2 | 9 | Chapter 3, loops |
| 17 | MUSIC_STG4 | −1 (13 with patch 7a) | Unused on the original PC release |
| 18 | MUSIC_STG4_2 | ? | |
| 19 | MUSIC_STG4BOSS | 10 | Final boss (Magician), loops |
| 20 | MUSIC_STG4BOSS_V2 | ? | |
| 21 | MUSIC_NAMEENTRY | 11 | Name entry, loops; also the main menu / ranking with the user's patch 9 and in the rewrite |
| 22 | MUSIC_STG4BOSS_3 | 12 | Chapter 4 (the arcade's Chapter 4 cue is 0xA0100E = this one), loops; the original PC release also sent it for the main menu / ranking (0x41DFC7, 0x425D44, 0x425E09), i.e. played the Chapter 4 song there; patch 9 changes those to 21 |

The original disc had the music as CD audio tracks 2–12, played through MCI (`play cdaudio from N to N+1`).
The PC mod plays `Music\Track02.ogg` … `Track13.ogg` through ogg-winmm. The rewrite should keep that file naming so players can reuse
the mod's music folder. Saturn soundtrack mapping (Saturn track → PC track):
02→02, 03→03, 04→04, 10→05, 07→06, 09→07, 11→08, 05→09, 08→10, 12→11, 06→12, and 06→13 (patch 7's Chapter 4 copy). Both discs hold the same 11 songs; the
PC track 12 is the Chapter 4 song (corrected 2026-10-19: it used to read 12→12, taken from the menu, which played track 12).

### EVTALL.RBH "play music" instruction (confirmed)

- 8 bytes: `5A 00 00 00 <cue> 00 00 00`, i.e. int32 opcode 90, int32 cue. Handled at `0x4C17D8`, script PC at `[0xDA0AB8]`.
- Chapter 1 = cue 9, Chapter 2 = cue 10, Chapter 3 = cue 16, Chapter 4 = cue 22.
- Each chapter repeats its instruction at several points (probably continues/checkpoints).
- Chapter 4's three instructions are at file offsets `0x196A0`, `0x19F2C`, `0x1A1CC` (patch 7b changes the cue byte at +4 of each to 17).
  They follow the last Chapter 3 one at `0x18E90`.
- Observed, not explained: during a Chapter 4 load the same breakpoint (`0x4C17DD`) logged opcode-90 instructions with operands
  outside the music range (2, 6, 8, 19, 24, 39, 66–70, 74, 75, 152, 157, 245, 247, 253). Only cue 19 reached the CD-music branch.
  So opcode 90 is probably a general sound request whose operand is a music cue only for small values. Needs checking in phase 5.
- In memory the script data was loaded around `0x488CD9C`–`0x49D3D9C` (varies per run; heap).

## 5. Stages

Stage index passed to the stage loader (`0x418AA0`, EAX): 0–3 = Chapters 1–4, 4 and 5 = stage 5, 6 = stage 6.
Observed: at startup the game loads index 0, then index 5, before the menu (probably the attract demo; not confirmed). The starting chapter of a new game is the byte at
`0xDA0CF4` (written at `0x4585C9`); forcing it in memory starts the game at another chapter.

## 6. THOTD.CFG (options)

396 bytes (`0x18C`), written on every exit and loaded to `0xD5B1D4`. Known field: blood color index, int32 at `+0x28`.
The rest of the layout is unknown.

## 7. Gaps to fill

- VMC: the `PACK` compression in `B_<color>.VMC`, the unknown page-header fields, and which page belongs to which model.
- RBH world: the meaning of the group flag bits, the u16 after the indices of untextured polygons, the texture word
  (probably a VMC page number), the `RBHH` fields, BODY 2.
- Every other RBH layout: creature, camera, sprites, sounds, and the rest of the event-script opcodes.
- `.PAL` / `.TBL` files, `SCORES.CFG`, `CSTATS.CFG`, `DEVS.CFG`, the rest of `THOTD.CFG`.
- Arcade: placement flags and the extra-data pointer (§12), the tables for the other stages, the texture point/header data and the
  color tables (phase 3).

## 8. Arcade dump: ROM layout (Model 2C, Revision A)

From the "hotd" entry in MAME's `src/mame/sega/model2.cpp` (BSD-3-Clause). [tools/m2_roms.py](../tools/m2_roms.py) checks every ROM
against MAME's CRC32 and SHA-1 and rebuilds the regions (verified 2026-10-07: all 29 ROMs of the user's dump match).

| Region | Size | ROMs | Contents |
|--------|-----:|------|----------|
| `maincpu` | 2 MB | epr-19696a, 19697a, 19694, 19695 | i960 program |
| `main_data` | 32 MB | mpr-19704, 19705, 19702, 19703, 19700, 19701, epr-19698, 19699 | i960 data (CPU address 0x02000000); 0x1800000–0x18FFFFF is mirrored up to 0x1FFFFFF |
| `copro_data` | 8 MB (1 MB used) | epr-19707, 19706 | TGPx4 (MB86235 geometry DSP) data |
| `polygons` | 32 MB | mpr-19715, 19711, 19714, 19710, 19713, 19709, 19712, 19708 | models (§10) |
| `textures` | 16 MB | mpr-19718, 19716, 19719, 19717 | texture ROM: per-polygon UVs ("texture point") and texture headers |
| `audiocpu` | 512 KB | epr-19720 | 68000 sound program (byte-swapped) |
| `samples` | 8 MB | mpr-19721, 19722 | sound samples (byte-swapped) |

Pairs are interleaved 16 bits at a time (`ROM_LOAD32_WORD`): the first ROM gives bytes 0–1 of every 32-bit word, the second bytes 2–3.

## 9. PC world files (`ART\STAGEn\WORLD\WORLDn.RBH`)

Worked out 2026-10-07; reader: [tools/pc_rbh.py](../tools/pc_rbh.py) (full layout in its header comment). All six files parse,
every polygon count matches its header, and every vertex index stays inside its object.

- Chunks: `RBHH`, `BODY` 0 (object table, object headers, polygon groups), `BODY` 1 (vertices, float x y z), `BODY` 2 (empty in WORLD1),
  three `RELC` (u32 0, u32 target body, then offsets of the pointer fields in BODY 0 that point into that body).
- Object table at the start of BODY 0: 12 bytes per entry, `u16 id, u16 0xFFFF, u32 header offset, u32 0`. An entry with offset 0
  (WORLD1: id 0xCFD) isn't an object.
- Object header (0x2C bytes): +0x00 offset of its polygon groups, +0x04 offset of its vertices in BODY 1, +0x08 own offset,
  +0x10 vertex count, +0x14 polygon count, +0x18 radius?, +0x20 center?. The groups stored right after a header belong to the
  *next* object.
- Polygon groups: `u32 head` (count in bits 0–15, kind in bits 16–22, flags above), `u32 sizes` (bits 16–31 = record length in
  16-bit words), records; the list ends with a head of `0xFFFF0000`. Kinds: 0 untextured triangle (8 bytes), 1 untextured quad (10),
  2 textured triangle (36: 3 indices, texture word, 0, 0xFFFF, 3 UV pairs as floats in texels), 4 textured quad (44).
- Coordinates: world space, Y up (floor of the first room at y = −25), Chapter 1 spans about x −1060…580, y −242…108, z −1748…782.
- Totals: WORLD1 18,688 vertices / 65 objects / 13,456 polygons; WORLD2 15,808 / 27 / 11,364; WORLD3 10,248 / 16 / 8,138;
  WORLD4 4,671 / 9 / 3,909; WORLD5 12,490 / 37 / 9,401; WORLD6 24,945 / 64 / 18,724.

## 10. Arcade models (polygon ROM)

Format from MAME's `model2_v.cpp` (`geo_parse_np_ns`, `model2_3d_process_polygon`); reader: [tools/m2_models.py](../tools/m2_models.py).
Little-endian 32-bit words, IEEE floats.

```
P0 (x y z), P1 (x y z)                  first edge
link × n (10 words each):
  attr                                  bits 0-1: 0 end, 1 quad, 2 triangle; 8-9: which edge the next polygon reuses
                                        (link type 0 = hidden polygon: never drawn, only moves the edge to start a new strip);
                                        10-11: z-sort; 12-16: signed texture header step; 17: double-sided;
                                        18-22: lighting parameter index; 27-31: LOD coefficient index
  normal (x y z)                        unit length, or 0 on some polygons
  Pa (x y z), Pb (x y z)                Pb is present but unused for triangles
end: one word with (attr & 3) == 0
polygon = P1(prev), P0(prev), Pa, [Pb]
next edge: link 0/2 → (Pa, Pb); 1 → P1 = Pa; 3 → P0 = Pb      (a triangle counts as Pb = Pa)
```

Models are stored back to back. A scan from address 0 finds 7,385 models with 453,811 polygons (374,743 visible, the rest hidden
link-type-0 polygons) and covers 100% of the non-empty (non-0xFF) words, so there is no other data in this region.
Exporting the hidden polygons produces long "spike" triangles between strips, so exporters must skip them. Texture coordinates aren't in the model: the polygon ROM only holds
geometry; UVs and texture headers come from the texture ROM, via addresses the program passes with each object.

## 11. Coordinate check: PC Chapter 1 vs arcade (phase 2 result)

Tool: [tools/compare_world.py](../tools/compare_world.py). It matches every PC object of WORLD1.RBH to arcade models of nearly the same
bounding-box size, aligns them **by translation only**, and exports both into PC coordinates for Blender.

Result:
- **Same axes, same handedness, same scale.** 21 of the 64 PC objects line up with an arcade model with translation alone
  (median distance ≤ 2 units; 13 of them at a median of 0.000, i.e. vertices identical). No rotation, mirroring or scaling is needed.
  Remaining mismatches are expected: the PC models are simplified, and some PC objects merge or split arcade pieces differently.
- **Different origins.** The arcade stores each piece in its own local coordinates, and the program places it with an offset.
  The PC version baked those offsets into world coordinates. The offsets come in groups that look like area origins (z = 300, 580, 747.6,
  1117.5, 1181.4, 1360.6 for the first area; y ≈ +24 to +26 for most pieces of the first area). One area (arcade models 0x23579A…)
  needs no offset at all.
- **Instancing.** The arcade reuses a model at several places (0x137FDB is used three times: PC objects 0x906, 0x90B, 0x90D at x ≈ +1.6, −198, +201);
  the PC has a separate copy of each.
- Chapter 1 arcade world models sit at polygon ROM word addresses 0x137FDB–0x13E350, 0x15A76E, 0x16F774–0x172FBC, 0x1890C5 and
  0x23579A–0x24D01B, in roughly the same order as the PC object IDs.
- Blender check (2026-10-07, `tools/blender_overlay.py`, images and `overlay.blend` in `private\compare\ch1\`): with the offsets
  applied, the arcade pieces sit exactly on the PC geometry (z-fighting where both have the same surface); red shows only where the
  arcade has extra detail.
- Limits of this check: the offsets were found by aligning shapes, not read from arcade data, so they prove the axes and scale match,
  not where the arcade program actually puts each piece. 43 of the 64 PC objects have no good match yet (no size-matched candidate, or
  the PC object combines/splits arcade pieces differently).
- Superseded by §12: the arcade's own placement data confirms all of this and gives a complete mapping.

## 12. Arcade level data: model table and placement tables (found 2026-10-07)

Tool: [tools/m2_level.py](../tools/m2_level.py). **The arcade and the PC use the same object IDs** (the RBH table IDs of §9).

**Model table** in `main_data` at 0xE73530 (CPU address 0x02E73530), indexed directly by object ID, 16 bytes per ID. Each record is
exactly the 4 parameters of the geometry engine's "object data" command, in its order (MAME `geo_object_data`):

| Offset | Field |
|-------:|-------|
| 0 | texture point address (UV list in the texture ROM, 16-bit word index) |
| 4 | texture header address (texture ROM, 16-bit word index) |
| 8 | `0x800000 | word address` of the model in the polygon ROM (bit 23 = polygon ROM) |
| 12 | link count (polygons including hidden ones) |

(A first reading put the base 8 bytes later, which paired each model with the next ID's texture addresses. Proof of the layout above:
for all 73 Chapter 1 models, header address − point address = 2 × the number of vertices over all links.)
The table covers IDs up to about 0x1D33 (main_data to 0xE90878); some slots are empty or untextured (point = header address).

**Placement tables** in `maincpu`, 24 bytes per record, each list ended by `0xFFFFFFFF`:

| Offset | Field |
|-------:|-------|
| 0 | object ID |
| 4, 8, 12 | f32 x, y, z: world position = model vertex + (x, y, z); no rotation or scale |
| 16 | flag: 0xFF for most; 0x000A0001, 0x00050001, 0x00140001, 0x00080001 on a few (meaning unknown; maybe trigger/visibility) |
| 20 | 0, or a maincpu offset (only ID 0xCE8 → 0xBE440 in Chapter 1; probably extra data such as animation) |

| maincpu offset | Records | IDs | Stage |
|---------------:|--------:|-----|-------|
| 0xBEF00 | 73 | 0x906–0x111E | Chapter 1 (checked against WORLD1.RBH) |
| 0xBFEA0 | 28 | 0xCDE–0x14BF | not checked yet (probably Chapter 2) |
| 0xC0640 | 18 | 0x16C1–0x1B7B | not checked yet |
| 0xC0A90 | 10 | 0x1116–0x1D1F | not checked yet |

Stages 5/6 and the remaining chapters may use further tables or another mechanism; not looked at yet.

**Chapter 1 check** (`m2_level.py compare`, surface distance = PC vertex to the nearest arcade triangle and the reverse):
- All 64 PC objects with polygons match an arcade placement: 63 by the same ID, all at a median surface distance ≤ 0.3
  (61 at ≤ 0.01), and PC **0x1D4B is the second placement of arcade 0xA27** (the PC gave that instance its own ID).
- PC 0x9DE is shifted by 0.295 in z compared with the arcade, and lacks the arcade model's lower 55 units.
- 9 arcade placements have no PC geometry: 0x91F, 0xCED, 0x111D, 0x111E (each at the same position as 0x91E, 0xCEC, 0x111C, so
  probably alternate states such as damaged versions), 0xCFD (the PC table has this ID with no object), 0xCDE (twice; 1 link),
  0xAC3 (740 links at the origin), and 0xCE8 (the PC object exists but has no polygons; the arcade record carries the 0xBE440 pointer).
- The arcade-only level (`m2_level.py obj`) rendered over the PC level in Blender lines up everywhere
  (`private\compare\ch1\level_top.png`, `level_persp.png`, `level.blend`).
- Mapping table: `private\compare\ch1\mapping.csv` (arcade ID, placement, PC ID, distances). For Chapter 1 the PC→arcade mapping is
  "same ID", plus the one exception above.
- Orientation: rendered from +Y the level shows the courtyard ground from above, so +Y is up (as assumed in §9).
  Confirmed 2026-10-08 with the arcade's own camera (§14): the camera's up axis is +Y.
- **Handedness: the game world is left-handed** (Y up). The arcade camera's axes satisfy right × up = forward, and MAME projects
  screen x = +camera x, screen up = +camera y, depth = +camera z. In a right-handed tool (Blender, OpenGL conventions) the scene
  therefore appears mirrored left-right unless one axis is flipped. The engine must flip one axis (e.g. negate z) or use a
  left-handed projection. PC and arcade share this convention.
- Other stages (2026-10-08, surface distance): placement table 0xBFEA0 = **Chapter 2** (27 of 27 PC objects match by ID),
  0xC0640 = **Chapter 3** (15 of 16; 0x191C matches at median 1.01, just over the 1.0 threshold; 0x1B7B differs, median 61,
  possibly a moving object), 0xC0A90 = **Chapter 4** (9 of 9). Cross-checks against the wrong world files match nothing.
  Stages 5/6 are still to be found. Tables: `private\compare\stage_*.csv`.

## 14. MAME dumps (2026-10-08)

MAME 0.289 (official x64 release, SHA-256 matches GitHub's published digest) is unpacked in `private\mame0289\` and runs
`private\hotd.zip` ("romset hotd is good"; driver status "preliminary", graphics fine).
- [tools/run_mame_dump.ps1](../tools/run_mame_dump.ps1) runs it headless (`-video none -sound none -nothrottle`), with all inputs
  from [tools/mame_dump.lua](../tools/mame_dump.lua) (coin, start, trigger and gun through MAME's input ports; nothing goes
  through Windows). Safety limits: MAME's `-str` and a wall-clock timeout.
- `-video none` also skips MAME's "THIS SYSTEM DOESN'T WORK" warning (MAME `ui.cpp`: warnings are off with no video,
  with `-str` under 300 s, or with the debugger). `skip_warnings` in ui.ini does not help. Frame notifiers don't fire with
  `-video none`, so the script runs as a coroutine on `emu.wait` (emulated time).
- Each dump: palette RAM, color translation RAM, luma RAM, 1 MB work RAM, optionally texture RAM, plus a screenshot.
  About 3.5× real time: 360 emulated seconds take about 2 minutes.
- Inputs: ports `:IN0` (Coin 1, 1 Player Start), `:IN1` (P1 Trigger), `:P1_X`/`:P1_Y` (light gun, range 87–380).
  The machine's settings: 2 credits per game, Japanese tutorial text in the attract demo. Credits are kept in MAME's NVRAM.
- **Life counter:** work RAM 0x51F290 (u32), 3 at the start of a game, −1 per hit, continue screen at 0. Writing 3 there
  continuously (`HOTD_INFLIVES=1`) gives infinite lives; tested over 1,200 s.
- **Region, blood color, free play** are operator settings (test menu, saved in the 93C46 EEPROM, two copies of the settings
  block): GAME ASSIGNMENTS has difficulty, lives (initial 3 / max 5), BLOOD COLOR (GREEN → BLUE → PURPLE → RED), advertise sound,
  COUNTRY (JAPAN → USA → EXPORT); COIN ASSIGNMENTS setting 27 = FREEPLAY. The dump as found was set to JAPAN / GREEN / 2 credits.
  `tools/mame_testmenu.lua` + `run_testmenu.ps1` drive the menu headless (TEST = "Service Mode", SERVICE = "Service 1"); main menu
  cursor starts on EXIT, GAME ASSIGNMENTS is 7 SERVICE presses down. The US / RED / FREEPLAY settings are saved as
  `private\mame_play\nvram_base_us` (checked: FBI boot screen, "FREE PLAY", English subtitles, red blood). Recordings must be
  replayed with the base they were made with; the region changes the game from boot on.
- **Full-game recording** (user, 2026-10-07): `private\mame_play\inp\play_us_20261007_2238.inp`, base `nvram_base_us`, 1,620 emulated s,
  all four chapters and the ending. Replayed with dumps every 2 s and texture RAM every 10 s into `private\mame_dumps\fullgame_us`
  (810 dumps); the replay's work RAM equals the live session's at all 54 moments both dumped.
- **Coverage** ([tools/coverage.py](../tools/coverage.py)): every placed model of Chapters 1, 2 and 4 was on screen at some point
  (within 600 units and 35° of the view direction). Chapter 3: 0x16C1 (a stone ledge along a brick wall, near the red room) and
  0x1AAE (pipes and catwalks, near the yellow/blue shaft) never were: they belong to routes this run didn't take.
  Limits: alternate routes that pass through rooms this run already saw (other camera paths, other enemies) can't be detected from
  geometry; stages 5/6 aren't in the four placement tables found so far.
- **Stage index:** work RAM 0x51F9D1 (byte): 0–3 = Chapters 1–4, 5 = attract/boot, 6 = ending (same numbering as the PC
  version's stage loader). Writing 2 from 1 s before Start until 8 s after makes a new game start in Chapter 3 ("The Third
  Chapter — Truth"); writing only after Start leaves Chapter 1's setup half-applied (Chapter 1 sky with Chapter 3 objects).
  `HOTD_START_STAGE` in mame_dump.lua, `-Chapter` in play/replay scripts, `tools\play_hotd_chapter.bat`; record/replay checked exact.
- **Gun calibration** (operator menu GUN SETTING > PLAYER1 GUN ADJUSTMENT): shoot target 1 at picture pixel (31.5, 31.5), then
  target 2 at the center (247.5, 191.5) of 496 × 384; "NOW CALCULATING", then "GUN MARK CHECKING": TEST = memorize, SERVICE =
  cancel. The game stores the two raw gun values as P1 calibration in the EEPROM at 0x20 (u16 x1, y1, x2, y2; P2 at 0x28) and
  updates the checksum at 0x08 itself (not a standard CRC-16; editing the EEPROM directly isn't possible without it). The dump's
  values equal MAME's port ranges (P1 X 173–596, Y 87–380).
  `tools/calibrate_gun.bat` measures cursor vs gun values (9 clicks), computes the values the cursor produces at both targets
  and shoots them through the menu headless; the result is `private\mame_play\nvram_base_us_cal`. Recordings note their
  base in `<name>.base.txt`.
- **Ammo counter:** work RAM 0x51F2C1 (byte), 6 = full, −1 per shot. Writing 6 continuously (`HOTD_INFAMMO=1`) gives infinite ammo.
- **MAME 0.289 quirks:** `-offscreen_reload` is listed in the bundled manual but rejected ("unknown option"). The Win32 light-gun
  provider (`-lightgun -lightgun_device lightgun -lightgunprovider win32`) works in a window. If MAME stops while a Lua coroutine is
  waiting in `emu.wait`, MAME resumes it during shutdown and crashes when it touches the machine (access violation in
  `lua_engine::on_machine_stop`), losing the `-record` file; `mame_dump.lua` returns as soon as a stop notifier fires.
- **Progress needs kills:** with infinite lives and a random gun sweep, the game reached a dark room with a red creature and a
  yellow swarm at about 240 s and stayed there until the end of the run (1,200 s). The camera only moves on when an area's enemies
  are dead.
- **Record/replay:** `-record` / `-playback` replay exactly (work RAM byte-identical at all 12 dumps of a 120 s test), provided the
  replay starts from the same NVRAM and repeats whatever the Lua script injects (`set_value` inputs and RAM writes are *not* in the
  recording). `tools/play_hotd.ps1` (user plays, mouse as light gun) and `tools/replay_hotd.ps1` do this.
- **Camera:** work RAM 0x51EA50 holds the view matrix in the geometry engine's layout (world → camera, +z forward, +y up);
  0x51EA80 holds its inverse. [tools/mame_camera.py](../tools/mame_camera.py) reads it.
- Dumps so far (`private\mame_dumps\`): `ch1_run1` (90 dumps over 360 s: attract loop, a game start at 40 s, Chapter 1 courtyard
  until the player dies at about 64 s, then attract demos of Chapter 1), `ch1_texram` (9 dumps with texture RAM).

## 13. Arcade textures (found 2026-10-07; colors from MAME dumps 2026-10-08)

Tool: [tools/m2_textures.py](../tools/m2_textures.py) (exports a level as .OBJ with one image per texture);
[tools/blender_textured.py](../tools/blender_textured.py) renders it.

**Texture sheets** are raw in `main_data` blocks 0–12: 1 MB each at CPU 0x02000000 + n × 0x100000. The program lists the 13 block
addresses at maincpu 0xA99E0 (passed to the routine at maincpu 0x3505C). Blocks 13 and up aren't textures. The game copies blocks into
the two texture RAMs (0x11000000 and 0x11200000); the code at maincpu 0xCDC–0x1000 then writes mip levels into the RAMs' second halves.
- Layout of a block (= MAME texture RAM): 1024 × 2048 texels, 4 bits each, one 16-bit word = 2 × 2 texels
  (bits 12–15 x even/y even, 8–11 x odd/y even, 4–7 x even/y odd, 0–3 x odd/y odd).
  The rasterizer sees a 2048 × 1024 sheet: x < 1024 from RAM rows 0–1023, x ≥ 1024 from rows 1024–2047.
- Chapter 1: all world polygons use sheet select bit = 1, and that sheet is **block 3** (chosen by eye: only block 3 has one coherent
  texture in each of the rectangles the Chapter 1 headers point to). Block 0 holds characters, enemies and HUD text
  ("LIFE UP", "MISS SHOT"), so it is probably the shared sheet 0. Which block each stage loads is not read from the program yet.

**Per polygon** (in link order, hidden link-type-0 polygons included):
- Texture points from the model's point address: (v, u) per vertex, u16 each, 13.3 fixed point in texels. The list holds
  2 × vertices words for every link, and the header list starts right after it.
- Texture header: 4 u16 at the header address, which moves by the signed attr bits 12–16 × 4 after each polygon.
  h0: bits 0–2 width = 32 << n, 3–5 height, 6/7 wrap x/y, 8/9 mirror x/y, 13 translucent, 14 textured, 15 checker;
  h1 bits 0–7: luma table base; h2: bits 0–5 x / 32, 6–10 y / 32, 12 sheet select, 13–15 microtexture;
  h3 bits 6–15: color index (palette). Chapter 1: 96% of visible polygons are textured; 253 distinct textures.
- UVs often run past the texture size (tiling with wrap), so textures must be exported one image per texture with repeat,
  not as UVs into the shared sheet. Mirror flags are not applied by the exporter yet.

**Colors (partly found):** texels are 4-bit shades. MAME's rasterizer (`draw_scanline_tex`) colors them like this:
1. shade (filtered to 8 bits, then >> 1) → **luma RAM** [h1 × 128 + shade] → 0–63, scaled by the polygon's lighting (`luma / 256`), capped at 63.
2. **Palette RAM** entry 0x1000 + h3 color index (CPU 0x01800000, 16-bit words) → 5-5-5 base color (red in the low bits).
3. Per channel: **color translation RAM** (CPU 0x01810000; red, green, blue tables 0x4000 bytes apart) [base value × 256 + brightness],
   low byte = output, then MAME's gamma table.
4. Translucent textures (h0 bit 13) discard texels under 50% alpha; checker (bit 15) draws every other pixel.

Sources:
- Luma RAM: **found**. maincpu 0x15F0 copies `main_data` 0xE9AAC0–0xE9EABF (16 KB = 128 curves × 128 entries, values 0–63) into
  luma RAM 0x11400000 (every other byte). Confirmed by MAME dumps: identical, and constant all the time.
- Color translation RAM: **computed at run time** (copy loops at maincpu 0x1AE94, 0x1B0C4, 0x1B1C8 from work RAM 0x512CA0).
  MAME dumps: one version for all Chapter 1 courtyard gameplay and its attract demo; other versions for cut-scenes, rankings and fades.
- Palette RAM: the bulk source isn't found in ROM; **taken from MAME dumps instead** (§14). Polygon entries 0x1000–0x13FF:
  the coin-started game and the attract demo use different sets (e.g. entries 532+, probably character colors), and within
  gameplay only entries 928+ change (animated effects). Entries 0x0000–0x0FFF belong to the 2D layer (text, HUD).
- maincpu 0x87D0–0x8AC0 is the self-test's list of memory ranges (name, address, size, mask), not a load list.
`m2_textures.py` applies these tables when given a dump (`level ... <dump prefix>`): luma at full lighting, palette, color translation.

**What the texture RAM really contains** (MAME dump of 0x11000000–0x113FFFFF, compared 64 KB at a time with `main_data`):
- Sheet 0 (RAM 0, first MB) = block 0, except 0xC0000–0xDFFFF, which comes from the stage block.
- Sheet 1 (RAM 1, first MB) = the stage block, except 0xC0000–0xDFFFF, which comes from block 0.
- The stage block changes with the scene: 2 in the attract intro, **3 in Chapter 1 gameplay**, 1 in a later attract segment.
- RAM 0's first 256 KB and both sheets' last 128 KB match no block: the program generates or unpacks them.
- The second MB of each RAM holds one repeated chunk (unused).
So "block 3" (chosen by eye earlier) was right for most of sheet 1 but not all of it. The exporter can use a texture RAM dump
directly (`<texram.bin>:0` / `:1`), which is exact for the dumped moment.

**Texture sets change per scene (solved 2026-10-08).** The "wrong textures" (trees showing a staircase, etc.) came from using
texture RAM from the wrong moment: at t = 40 s (coin inserted) the RAM still held the attract set; from 48 s the gameplay set is
loaded, and the tree is exactly where the header points (sheet 1, 128 × 256 at (1344, 0)). With each view's own texture RAM,
palette and color tables, the textures match the MAME screenshots (`tools/render_views.ps1`, `private\compare\ch1\views2\`).
So the header and sheet decoding above is right; **the texture RAM content must be taken from the same scene**. For the engine this
means: the arcade loader needs per-scene texture sets (which block parts the game loads when), not one sheet per stage.
Also confirmed by logging buffer RAM (0x00900000, where the i960 builds the geometry DSP's display lists): the object command is
`00800101, texture point, texture header, 0x800000 | model, count`, with exactly the model table's values
(`tools/mame_fifo_log.lua`; the CPU doesn't write the DSP FIFO ports directly in gameplay).
Remaining differences to the game: per-polygon lighting isn't applied (renders are brighter and flatter), the sky backdrop is drawn
separately, and in the alley view (t01840) a roof-like model stands where the game shows hills.

## 15. Display lists, lighting and output curve (2026-10-08, evening)

From MAME's `model2_v.cpp` / `model2rd.ipp` / `model2.cpp` (in `private\mame\`) and buffer RAM dumps of the full-game replay
(`replay_hotd.ps1 -BufRam`, `private\mame_dumps\fullgame_bufram`; parser [tools/m2_displaylist.py](../tools/m2_displaylist.py)).
- **Buffer RAM** is 128 KB on Model 2C (CPU 0x00900000–0x0091FFFF, mirrored up to 0x0097FFFF). The i960 writes display lists
  there; the dumps show several lists at 64 KB steps (one per frame). Opcode words seen (bits 23–27 = command):
  `0x00800101` object (texture point, texture header, 0x800000 | model, link count), `0x05800B0B` matrix (12 floats),
  `0x05000A0A` light vector (3 floats, camera space), `0x03000606` lighting parameters (start index × 4, count, then count ×
  (param, f32 LOD coefficient)), `0x04800909` (focal distance?), `0x01800303` (window), `0x0100011A`, `0x07800F0F` (fill).
- **Most world models are not in these lists**: Chapter 1's 0x906 (bridge) never appears in any object command or anywhere in
  buffer RAM, even with the camera on it; some world IDs do (0x91D–0x931, 0x9F2–0xA05). How the rest reaches the geometry DSP
  is open (the TGPx4 copro program and `copro_data` are the next place to look). So display lists can't tell which models are drawn.
- **Polygon luma** (MAME `geo_parse_*`): normal and light in camera space; `l = |n·L|`, or 0 when `(n·L)(n·P) < 0` (light and
  viewer on different sides); `luma = clamp(l × diffuse[i] + ambient[i], 0, 255)`, `i` = polygon attr bits 18–22.
  Parameter word: bits 0–7 diffuse, 8–15 ambient, 16–23 specular scale, 24–31 specular control (specular only in some
  geometry variants).
- **HOTD's values:** the light for world objects, converted to world space with the object's matrix, is exactly **(0, −1, 0)**
  (straight down) in every frame of Chapters 1–4 (max deviation 0.000 over thousands of objects). Characters and effects get other
  light vectors. Parameter table (index: diffuse/ambient), same in all chapters: 0: 0/255, 1: 80/224, 2: 112/192, 3: 144/160,
  4: 176/128, 5: 0/255, 6: 72/224, 7: 96/208, 8: 128/192, 9: 255/180, 10: 255/180, 11: 128/192, 12: 159/96, 13: 152/62,
  14: 255/255, 15: 255/255, 16–30 = 0–14 again; 31 not set.
- **Rasterizer** (`model2rd.ipp`): textured: `lumaRAM[luma base × 128 + (filtered texel >> 1)] × luma / 256`, capped at 63;
  untextured: `luma >> 2`. Then color translation, then MAME's output curve `max((v − 64) × 255 / 191, 0)` (MAME's comment:
  "works OK for most games"; real monitors were calibrated per game). Bilinear filtering of the 4-bit texels; a transparent
  texel (15) takes its neighbour's value before filtering; discard below 50% alpha.
- All of this is implemented in the viewer's shader (src/render/renderer.cpp).

### Why the picture is darker than the cabinets (gamma, GitHub issue #23, 2026-11-03)

People who played on the real cabinets say the game is too dark here, and darker than they remember in MAME too. What
the renderer does (src/render/renderer.cpp, `uM2Gamma`, on by default: `RenderOptions::gamma`):
1. the polygon's luma picks a brightness 0-63, the colour translation table (§19, the game's own, built at boot by
   maincpu 0x1180: `y = ½(√x + x²)`) gives each channel's 8-bit value;
2. then MAME's output curve `max((v − 64) × 255 / 191, 0)` (model2_v.cpp). The 2D layer goes through the same curve
   (hud.cpp's bar colours do it on the CPU). The PC models (THOTD.EXE's colours) have no such curve.

What is known:
- **The game's table rises steeply off 0.** Its `√x` part lifts the dark end: base 1 at brightness 16 is 19, base 4
  is 28, base 16 at brightness 32 is 71; brightness 63 of base 16 (a middle colour) is 120. So the game was written for a display that takes roughly the bottom quarter of the signal as black: the
  monitor's black level (its brightness knob) does that, and MAME's `− 64` stands for it.
- **MAME's curve is a guess.** Its own comment says it "works OK for most games"; it is a fixed straight line, not
  read from the hardware. Every table value up to 64 is black, and the rest is stretched: base 16 at full brightness
  120 -> 74, base 24 at half brightness 96 -> 42. If the cabinet's black sat a little lower than 64 (an operator's
  brightness knob, an aged tube), all of those shades become visible dark greys and the middle tones are brighter:
  that is the "brighter cabinet".
- **Real cabinets were not calibrated alike** (MAME's note: per game, per operator), so there is no single right
  value; there is no measurement of a real cabinet's output in this project.
- How much brighter, as a power curve: the gamma that brings MAME's value back to the table's own value is about
  1.4 (bright colours) to 1.6 (middle) to 2+ (dark ones), e.g. base 16 at full brightness 74 -> 120 needs 1.64.
  A power curve cannot bring back the shades MAME's curve made 0 (they stay black: blacks stay black is the point of
  a gamma slider); a "black level" control (the `64`) would, and is the more hardware-like knob. Done (bugs4,
  2026-11-04): Options -> Video -> **Black Level** (render/gamma.h `blackLevel()`, launcher.ini `blacklevel=`,
  `--black-level`): the curve is `max(v − b, 0) × 255 / (255 − b)`, b = 64 (default: the shader's original line, every
  frame bit-identical) down to 0, per pixel before the 8-bit output in the shader (`uM2Gamma` 2, `uM2Black`), in the
  2D layer's table (sprites_arcade.cpp) and the cut-scene bars' flash (hud.cpp). Not yet matched to the C-CRX
  real-hardware video of issue #23 (no preset).

Options -> Video -> **Game Gamma** / **Menu Gamma** (src/render/gamma.h; launcher.ini `gamma=` / `menugamma=`, `--gamma` /
`--menu-gamma`: the play / the menus, the Gallery and the front-end screens): `out = in ^ (1 / gamma)` on the
final picture (both model sets, the 2D layer, the HUD, the menus, the VR eyes, screenshots and `--render`), 0.60 -
2.00 in 0.05 steps, 1.00 = the picture unchanged (no pass at all). No "Arcade cabinet" preset: there is no measured
cabinet footage to match (above), only "undo MAME's guess", which is not one number.

## 16. Texture sets in the viewer (2026-10-08, evening)

The game reloads parts of texture RAM per area (§13): over the full-game replay, Chapter 1 has 3 texture RAM versions, Chapter 2: 3,
Chapter 3: 3, Chapter 4: 4 (`tools/texset_assign.py`). Per-model choices (first time near, closest, weighted vote) were each wrong
somewhere. What works: **choose the set by camera position**, like the game: the viewer uses the set that was loaded at the nearest
camera position recorded in the replay (`zone` lines in `private\arcade_texsets.txt`, 662 positions). With that, viewer renders
match MAME screenshots of the same moments in all four chapters (`tools/viewer_compare.py`, `private\screenshots\compare\`),
except: no sky backdrop, no characters/effects/HUD, and some animated colors (palette entries 928+ change during play; one palette
per set is used, e.g. the Chapter 2 pool at t05200 comes out yellow).
Off the recorded route the nearest position may pick a set that doesn't hold a model's textures. For release all of this must come
from the ROM (ROADMAP), i.e. from the game's own per-area texture load lists.

## 17. Texture RAM rebuilt from the ROM alone (2026-10-08, evening)

Every texture RAM version seen in the full-game replay (13 of them: attract, and 3-4 per chapter) is fixed by one **area block**
`n` (main_data block 1-12; see §13 for the block layout):
- RAM 0 (sheet select 0) = block 0, except 0xC0000-0xFFFFF = block n's 0xC0000-0xFFFFF.
- RAM 1 (sheet select 1) = block n, except 0xC0000-0xDFFFF = block 0's 0xC0000-0xDFFFF.
- RAM 0, texels x 0-255, y 0-511 (the first 256 bytes of each of the first 256 RAM rows of 1 KB): 8 tiles of 128 x 128 texels,
  2 wide x 4 high, row-major. Each tile is 8 KB stored linearly (128 bytes per RAM row) in main_data 0xE00000-0xE43FFF (CPU
  0x02E00000+). The 8 tile addresses per area block are a table at **maincpu 0xC1610**: 12 rows of 8 u32 CPU addresses,
  row = area block - 1. (Probably the enemy/character textures of the area.)
- The right half of each RAM's last 128 KB (sheet x 1536-2047, y 768-1023) is generated by the program (mip levels?). No world
  polygon of Chapters 1-4 uses it.
Checked byte for byte: all 12 area blocks' rebuilds equal the dumps everywhere except that generated corner. In the viewer
(`rom:<n>` in `arcade_texsets.txt`, ArcadeSource::buildTextureRam) the renders of 13 test moments are pixel-identical to the
renders from dumped texture RAM.

**Current area block: work RAM 0x51F9B4** (byte, 1-12; equals the texture RAM's area block in all 162 texture RAM dumps of the
replay). `texset_assign.py` now reads it from every 2-second dump. What sets it is i960 code: maincpu 0xC1580 is a 26-entry jump
table into handlers at 0x56540-0x59F80 (they read work RAM 0x51FDB4); needs an i960 disassembler. For the rewrite it may be simpler to
map the PC camera path / event scripts to area blocks (a small table of numbers, not ROM data).
Area blocks per chapter (from the replay): Chapter 1: 1, 3, 4; Chapter 2: 5, 6, 7; Chapter 3: 8, 9, 11; Chapter 4: 4, 10, 9, 12;
attract: 2. Still needed for ROM-only: **which area block is loaded where** (the game's trigger; now taken from recorded camera
positions), the **polygon palette** and the **color translation tables** (both still from MAME dumps), the sky, and the
characters' textures (probably the tiles above).
The table just before the tile table (maincpu 0xC1580, 26 u32 values 0x56540-0x59F80) looks like code or script pointers and
may be the per-area load triggers; not checked.

## 18. Polygon palette and color translation sources (2026-10-08, evening)

**Polygon palette (palette RAM 0x1000-0x13FF): found in the ROM.** maincpu **0xA9960** holds 13 pointers to palette records
(record = u16 count, then count 5-5-5 colors; all start with the same 32 standard colors 0x8000, 0xFC00, 0x83E0, ...):
record 0 (0xA1D48, 213 colors) is loaded at entry 0; record n (n = the area block of §17, 1-12) at **entry 500**. Entries past
the end of the current area's record keep the previous area's values (seen in the dumps), and entries 920-1023 are animated by
the program (water, fire, flashes). The viewer applies the records for `rom:<n>` sets (`--dump-palette` turns it off); renders are
pixel-identical to the dumped palettes at all 13 test moments. The table right after (0xA99A4, 12 pointers to 0xA9660-0xA9920,
64 bytes apart) is not decoded yet; 0xA99E0 is the 13 texture block addresses (§13).

**Color translation: built by the program, not stored.** The tables are generated in work RAM (row 0 at 0x512CA0, u16 per
entry, laid out like the hardware tables) and copied to the color translation RAM (maincpu 0x1AE94, 0x1B0C4, 0x1B1C8). Structure
(per channel, 32 base values x 256 brightness, only brightness 0-63 used):
- Even base values and base 25, 27, 31: one fixed family of brightness curves, identical for red, green and blue and constant
  during gameplay (all but 9 fade frames of 718 gameplay dumps). Not a simple power law (best fit off by 26) and not a function
  of base x brightness alone.
- Odd base values (except 25, 27, 31): per-channel gradients between two 5-bit colors (end values are multiples of 255/31), which
  change per scene and animate (flashes, tints).
Next step for ROM-only colors: disassemble the generator (i960 code that writes 0x512CA0) or record the constant part as a
formula once the code is understood. Until then the viewer takes the color translation from a dump.

## 19. Color translation from the ROM (2026-10-08, night)

Read with [tools/i960dis.py](../tools/i960dis.py) (a small i960 disassembler written for this). The game keeps a work copy of the
tables in work RAM: 3 channels x 32 base values x 64 brightness, u16 (red 0x512CA0, green 0x513CA0, blue 0x514CA0; 0x80 bytes
per base value) and copies it to the color translation RAM.
- **Startup generator, maincpu 0x1180** (called at 0xAF0), for every base value b (5 bit) and brightness l (0-63):
  `c = (8b + b/4) / 255`, `M = ½(√c + c²)`, `x = (l/63)·M`, `y = ½(√x + x²)`, entry = `min(⌊255·y⌋, 255)` for all three
  channels; brightness 64-255 (written straight to the hardware table) = `min(⌊255·M⌋, 255)`. Square roots and products in
  single precision, the sums in double (i960 `sqrtr`/`mulr` vs `mulrl`/`addrl`). Matches every even row of every dump exactly.
- **Area loader, maincpu 0x34FF0..0x35084** (area n in g0): stores n at work RAM 0x51F9B4, loads texture block `[0xA99E0 + 4n]`
  (call 0xEB0), the tiles (0x5CA30), palette record `[0xA9960 + 4n]` (0x2010) and the odd color rows from the row list
  `[0xA99A0 + 4n]` (0x1330). At startup 0x1330 is called with list 0xA9660 (the area-1 list).
- **Row lists:** 14 pointers per list (rows 1, 3, ..., 27); each row is 64 x (R, G, B) bytes, multiplied by the float at
  maincpu 0xA831C (1.0) and capped at 255.
- **Row library:** maincpu 0xE211 + 192 x entry (about 125 entries). Routine 0x13F20 (g0 = slot k -> row 1 + 2k, g1 = entry)
  copies one entry into one row; scene events call it: 0x67380 loads rows 5, 7, 9, 11 from per-event lists at maincpu 0xC5A40
  (indexed by the event's value and work RAM byte 0x51F9C0; 0xFFFF = keep), driven by the command stream at work RAM 0x52092C;
  other callers at 0x21E74, 0x5D66C.. (rows 25-29, effects), 0x71BB4 (rows 1 and 13).
- Rows 19/21 also have their own animation routines (0x1440, 0x1500), and 0x187B4.. are fades (whole table times a factor / 32).
**In the viewer:** `rom:<n>` sets build the tables from the ROM (ArcadeSource::buildColorXlat). Which library entries the scene
events put into which rows is, for now, the most common state per area in the replay (`rows` lines written by
`texset_assign.py`: small index numbers, no color data). With them, all 13 comparison moments are pixel-identical to renders with
the dumped tables. `--dump-colors` switches back to the dumps. Remaining for ROM-only: the event side (which area and which row
entries when), i.e. the arcade's own event/camera scripts or a mapping from the PC port's.

**Palette, completed:** the record loader (maincpu 0x2010 -> task 0x1F70) writes the area record from entry 500, then calls
0x24D70, which writes the record at maincpu **0xA35C8** (100 colors) *downward* from entry 1023 (so entries 924-1023) and copies a
palette animation script (from 0xA35CC on) to work RAM 0x520662. About 30 of those 100 entries are animated (alarm lights,
glowing floors), and their initial values differ a lot from what's on screen. The game keeps a shadow copy of the palette in work
RAM at 0x51CCB0. Entries past an area's record keep earlier areas' values (records applied in route order, attract area 2 first).
With all of this, the ROM palette equals the dumps everywhere except the animated entries (and entry 211). The viewer takes those
from a dump when there is one, until the animation script is emulated. With them, 11 of 13 comparison moments are pixel-identical
and the other 2 differ in a few hundred pixels (row-swap states that change within an area).

## 20. Sky (2026-10-08, night)

The sky is ordinary 3D models, the first objects of each display list, drawn around the camera (their matrix translation is
camera + (0, -9, 0) in world space, i.e. only the camera's rotation applies). Per area (from `m2_displaylist.sky_objects`,
replay buffer RAM): Chapter 1 area 1: **0xA29** (dome, 3600 x 1130 x 3600) + **0x608** (flat ring, 3350 wide); Chapter 2 area 5:
**0x1238** + 0x608; area 6: **0x137C** + 0x608 at camera + (0, -200, 0) (the snowy landscape seen through the windows); area 7:
**0x14B9** + 0x608 (also in the placement table; the game only draws it around the camera); the moon there is a separate flat
model 0x14AC far away. Chapter 4 area 12: **0x1D20** + 0x608. The other indoor areas (3, 4, 8, 9, 10, 11) have none. They use the area's texture set like the world. The viewer draws them first without depth writes (K toggles);
`texset_assign.py` writes them as `sky` lines. The PC version's sky isn't known yet (the PC world files have no such object).

## 21. Creatures (2026-10-08, night)

**PC:** `ART\STAGE1\CREATURE\CREAT1.RBH` and `CREAT2.RBH` have the world files' layout (§9) except the object table: 8-byte
entries (u16 ID, 0xFFFF, u32 mesh header offset); each entry is one creature *part* (head, upper arm, ...) as an ordinary mesh.
CREAT1: 1,398 entries (916 distinct meshes, all polygon counts match), CREAT2: 2,660 IDs, no overlap. Entries 0x5BC-0x5D9 of
CREAT1 point to a different structure at the end of BODY 0 (not decoded; maybe animation). Textures: BIGGER.VMC pages below 512.
**The part IDs are the arcade's object IDs, and the local coordinates are the same** (bounds equal to 0.1 units for the parts
checked: 0x46B, 0x46C, 0x489, 0x4DF, 0x33A, 0x361, 0x1AF, 0x7C4), so PC and arcade parts are interchangeable per ID.
**Arcade:** each part is a separate object command in the display list with its own model-to-camera matrix. With the camera
recovered from a placed world model in the same list (`eye = placement - R^T t`), every part's world transform follows
(`tools/creature_poses.py`). Display lists end at the END command 0x07800F0F; data after it in the 64 KB block is older (and
MAME fills empty buffer RAM with END words), which earlier produced stray objects.
Some part IDs: Chariot (Chapter 1 boss) 0xE02-0xE85, 0xEC3, 0xF03-0xF84, 0xFD7-0xFE1, 0x101F; zombies 0x46B-0x4A3, 0x4DF-0x50C,
0x334-0x36D; scientist 0x5E4/0x5E6; blood/effects 0x7C4, 0x660, 0x1AF-0x1BF.
**Not done:** animation (how the game computes the part matrices), and which parts form which creature. The viewer shows the
poses of 8 recorded frames (a zombie and a boss frame per chapter; N jumps to them), each part from the source its category picks.

## 22. Object names, shadows, data shared with the PC executable (2026-10-09)

- **Name table (corrected 2026-10-15):** a **pointer table at main_data 0xEE4EB0**, one u32 per object ID (7,477 IDs): the CPU
  address (0x02xxxxxx = main_data + 0x2000000) of a zero-terminated "PN_..." string, or 0 for the 12 IDs without a name. The
  strings lie at main_data **0xE4B180** up to the model table (0xE73530), each padded with zeros to a multiple of 16 bytes,
  but **not one per ID**: IDs without a name have no string, so counting strings drifts by up to 6+ IDs past 0x7C3 (the
  viewer, arcade_props.py and notes before 2026-10-15 did that; names quoted in older notes may be off). Examples: 0x906
  `hashi01_01a` (hashi = bridge), 0x608 `skyuv02a`, 0x5E4 `sitai_ken_a`, 0x9E4 `niwa01_03b`, 0xFD9 `hyum_sakir` (the
  Chariot's right foot), 0x13FF `devilon_asil`, 0x1B02 `boss3_syo21` (Hermit), 0x660 `tamahi`. The PC executable has the same
  strings (THOTD.EXE file offset 0x18E7xx). Readers: `arcadeObjectNames` (src/assets/arcade_loader.cpp; Model::name, P prints
  it), `tools/arcade_props.py` names().
- **Shadows:** names ending in **`_DD`** (e.g. 0xE03 `hyum_hizal_DD`, 0x2A2 `hara00a_DD`) are shadow models: a low-polygon copy
  of a part, drawn with a transform whose vertical row is zero (squashed onto the floor). The PC creature files have no `_DD`
  models; the viewer draws them only when the creature's category comes from the arcade.
- **Arcade parts without a PC model** (besides shadows): 0x1B02 (Hermit), 0x1C32, 0x1C4F, 0x1C7D, 0x1D1C (Magician fight).
- **PC detail:** one detail level per part in CREAT1/CREAT2.RBH (no LOD copies); the PC meshes simply have fewer polygons
  (e.g. 0x5E4: 191 vs 353, 0x33A: 59 vs 125).
- **CREAT2.RBH polygon kinds 64-68:** same record layouts as kinds 0-4 with bit 22 of the group head set (a flag, maybe
  translucency; not used yet). The reader didn't know them before, so the PC data of Chapters 2-4 failed to load whenever
  creature parts were requested (the viewer then fell back to the arcade parts).
- **Data shared with THOTD.EXE** (32-byte matches): main_data 0xE4B180-0xE73530 (the names), 0xEF0000-0xF16000 (a table of
  518 pointers, as arcade CPU addresses, to 28-byte records of 7 floats at 0xF12740 that change smoothly from record to record:
  looks like a camera path, eye + target + 1), 0xF80000 (index tables), 0xFD0000. The PC port carried these blocks over as they
  are (with the arcade's addresses). The placement tables are in the executable too (Chapter 1's at file offset 0x1CEDD4).
- **Creature animation:** not found yet. Part matrices are built by the geometry coprocessor (the i960 sends operations and reads
  the results back from 0x884000; object command helpers at maincpu 0x13FC0-0x14700). Game objects are records in work RAM
  (e.g. 0x5D08F4: code pointer 0x3B160, position, model ID 0x5E6) with behaviour code pointers; the animation state is reached
  from there.

## 23. Creature animation (2026-10-09; decoded and verified)

Everything is in the ROM; tool: `tools/m2_anim.py`, engine: `src/assets/arcade_anim.*`.

- **Address space:** the i960 sees main_data at 0x02000000 (all 32 MB) and its upper 16 MB again at **0x06000000**
  (MAME `map(0x06000000, 0x06ffffff) ... region("main_data", 0x1000000)`). Pointers 0x06xxxxxx = main_data 0x1000000 + offset.
- **Tables in maincpu:** 0xC7240 root skeleton node per creature type (94 types), 0xC73C0 joint count per type (int32),
  **0xC7540** first frame record per motion (674 motions; the last 12 point into maincpu 0x150000-0x183AE0, the rest into
  main_data), **0xC7FD0** frame count per motion (int32). Every motion's size (frames x record size, padded to 16 bytes) fits
  exactly one joint count, so a motion's joint count follows from the gap to the next pointer. The counts come in blocks
  (motions 0-21: 18 joints, 22-37: 19, 38-65: 28, ...): creature types with the same joint count share one pool of motions.
- **Skeleton node** (maincpu): +0x00 model ID (0 = none), +0x04 offset x, y, z (floats), +0x10 flag, +0x14 joint index,
  +0x18 child count, +0x1C child node pointers. The root node (joint 0) has no model. Example type 20 (bridge zombie, "ken*"
  parts): root -> 0x35E torso (joint 1: head 0x334/0x335, arms 0x361-0x368-0x336-0x365 and 0x362-0x369-0x337-0x366) and
  0x363 hips (joint 12: 0x33A, legs 0x35F-0x36C-0x36A and 0x360-0x36D-0x36B).
- **Frame record:** 3 floats (root position x, y, z), then one int16 triple (a0, a1, a2) per joint (65536 = 360 degrees),
  padded to 4 bytes: (12 + 6 x joints + 3) & ~3 bytes (20 joints: 132).
- **Drawing** (maincpu 0x37720 per object, 0x36240 per part, recursive; the i960 drives the geometry coprocessor's matrix stack,
  TGPx4 on Model 2C, through the function port 0x880000 + 4 x function and the FIFO 0x884000; a command word in the FIFO is
  `fn << 23 | fn << 8 | fn`). Functions used: 0x05 push, 0x06 pop, 0x11 read the 12-float matrix (3 columns, then translation),
  0x12 translate, 0x13 scale, 0x14/0x15/0x16 rotate X/Y/Z by one angle, **0x86 rotate by three angles**:
  R(g1, g2, g3) = Rz(-g1) Ry(-g2) Rx(-g3) (usual right-handed matrices).
  - object: T(pos +0x20) R(+0x34, +0x30, +0x2C) scale(+0x94) T(root) R(joint 0)
  - part: parent x T(x, y, -z) x R(-a2, a1, a0) = parent x T(x, y, -z) x **Rz(a2) Ry(-a1) Rx(-a0)**; the part's model is drawn
    with that matrix and its children start from it.
  - root translation: (x - x1, y, -(z - z1)) with x1/z1 from frame 1 (moving motions are drawn in place; the object moves
    instead), or (x, y, -z) when object byte +0x3C6 bit 0 is set.
  - Verified: the work RAM part matrices (part +0x7C, entry matrix) of 30+ objects in 25+ creature types (11-21 joints) are
    rebuilt exactly from the ROM (`m2_anim.py check`).
- **Object record** (work RAM, e.g. 0x5E410C in `anim_bridge` t01060): +0xC8 runtime part tree (part: +0x00 model,
  +0x04 offset, +0x24 children, +0x34 child count, +0x38 joint, +0x7C entry matrix), +0xCC creature type, +0xD0 motion,
  +0xD8 current frame record, +0xF4 frame (0-based), +0xF8 frame counter (frame = counter mod length), +0x11C per-part hook
  (0x9CE60: saves the matrices of a few parts, e.g. for aiming), +0x1E4 matrix after T(root).
  - **Second motion (corrected 2026-10-20, viewerfix):** +0xFE / +0x110 is a second motion and frame; while object byte
    +0x3C6 bit 5 is set, the parts whose side flags bit 5 is clear take their angles from it (part_init 0x36750: a
    first-level part gets bit 5 = ((role == 1 ? b : ~b) & 1) with b = byte 0x118000 + type, deeper parts inherit). The
    node flag (+0x10) bit 0 is a different thing, the **mirror** flag (side flags bit 4): in a mirrored motion (byte table
    0x7C6F0 per motion) part_draw 0x36240 places such a part at its usual position but with the body's matrix (+0x1E4,
    before the root joint's turn) instead of its parent's rotation (the walkers' feet). `CreatureAnimations::pose` does both.
  - **What a part draws** (body_part_draw_hook 0x9CE60 and the boss hooks): by the part's class (role table 0x1184E0 +
    type x 0xF8 + role x 8, byte 3) another model in its place or extra models with its matrix: the Hangedman's wings
    (role 1: 0x1401 + counter % 100), the Magician's veins (classes 3 / 4 / 11 / 12), the Hermit's face (role byte 27:
    0x1AEE + blink % 50), Sophie's eyes / hand, Samson's saw (role 5: 0x557 / 0x558), the players' gun / hands, Rogan's
    arcade coat (class 8: 0xB5 + frame / 2 % 100). Shared as `partDrawModels` (src/assets/creature_pose.*).
  - **Colours:** the creatures are re-coloured per spawn group by the colour rows the event scripts swap in (GGCOL /
    GGSET -> color_row_set 0x13F20), and lit by light record 1 (object_draw 0x67DF0: straight up in world space). The
    Gallery takes each type's area and rows from game runs (`galleryLook`, HOTD_TRACE_TYPE_ROWS), the viewer the rows
    each area has most of the time (`arcadeAreaColorRows`).
  - **Blending** (0x36CA0): when a motion changes, the joints move from the old pose (buffer +0xE0) to the new motion along
    the shorter way over +0x106 frames; +0xD8 then points to the blended record (+0xDC, work RAM).
  - Motion selection: 0x39180 sets the motion (+0xD0) and clears the frame; 0x38BD0(object, type, motion) builds a creature.
- **Not known yet:** which motion each creature uses for what (walk, hit, die: chosen by the behaviour code), the parts the
  per-part hook saves, the PC port's own animation data.

- **PC port:** THOTD.EXE carries the same tables and data (2026-10-11): motion pointers at VA 0x5EFBD0, frame counts
  0x5F0658 (equal to 0xC7FD0), joint counts 0x5EFA58, motion data from 0x5FD4F4; 672 of the 674 motions are
  byte-identical (motions 3 and 213 differ). The PC's creature parts (CREAT1/2.RBH) share the part IDs, so one set of
  skeletons and motions drives both sources.

- **Names** (2026-10-12): the program keeps the developers' names: maincpu **0xC8A60** = 94 pointers to the creature
  type names (`BO_hyum` 35 Chariot, `BO_hyumhada` 36, `BO_devilon` 50 Hangedman, `BO_boss3` 64 Hermit, `BO_boss4` 76
  Magician, `BO_sophi` 34, `BO_zonbiman` 16, `BO_kenkyu*` scientists ...), **0xC8BE0** = 674 pointers to the motion
  names in motion-number order (alphabetical: `MO_b1_*` Chariot 0-21, ..., `MO_tomaruku` 0x1CD Rogan walking,
  `MO_z_walk_g` 0x289, `MO_z_dead_*` ...). THOTD.EXE has identical tables. Tool `tools/m2_names.py`; the viewer prints
  them with every animated creature and when U cycles the motions.

## 24. Palette animation (2026-10-09; decoded and verified)

- maincpu 0x24D70 (after every area palette load) writes the effect record 0xA35C8 downward from entry 1023 and keeps a copy of
  entries 1..30 of it in work RAM 0x520662-0x52069E.
- maincpu **0x25310** (called from 0x1DE4 once per frame) computes the animated colors from the frame counter (work RAM
  **0x51F428**), the stage byte **0x51F9D1** (0-3 = Chapters 1-4; 5 = no effects; 6 clears two entries) and four event flags
  (0x51E982: stage 2 skips the routine; 0x51E9A1, 0x51E9B2, 0x51E9B3: stage 3 special cases). Color helpers: 0x24F40 splits
  RGB555, 0x24F80 joins it again (components other than 31 lose their lowest bit); "scale by k" = component x (k mod 32) / 32.
  - 0x52066C + 2i (i = 0..8): `[0xAB120 + 2 * (int((frame + 3i) / 1.5) mod 90)]` (a 90-color cycle).
  - 0x52067E..0x520682: `[0xA35E8 + 2i]` scaled by int(tri(frame, 96) / 1.5), tri(t, p) = t mod p mirrored above p/2;
    0x520684 (0 in stage 6) and 0x52068C: `[0xA35EE]` scaled by the same.
  - 0x520690 + 2i (i = 0..13, except 0x520698 which only stage 6 clears): `[0xA35FA + 2i]` scaled by
    int(tri(frame + 0x150 + 14i, 100) / 2.5 + 12) (12-31: light strips pulsing out of phase).
  - k = tri(frame, 64): 0x52068E = `[0xA35F8]` scaled by k; 0x5206AE = 0x801F blended toward 0x800C by k/32;
    0x5206E0 = 0x800E (stage 3 with flag 0x51E9A1: 0x8012) scaled by k.
- maincpu **0x24E90** copies work RAM 0x520664 + 2n to entry **1021 - n** (n = 0..37, skipping 0x520668, 0x520688,
  0x52068A), stores **0xEBDF in entry 211**, 0x9854 in entry 689 when the area (0x51F9B4) is 9, and 0x5206E0 in entry **959**.
- maincpu **0x24E5C** stores `[0xAB110 + 2 * state]` in entries 1023 and 984 (0x5206AE wins for 984), state = work RAM
  byte **0x51F9C0**, copied once at setup (0xDC70) from 0x51F72D (values above 3 -> the ROM default at 0xAA545): an operator
  setting next to 0x51F72C (probably difficulty), most likely the **blood color** (0xAB110: 0x801F, 0xB340, 0xFC12, 0xE01C).
  It is 0 in all our runs. The row-event lists of §19 are indexed by the same byte, so they are per-setting variants.
- Verified: the palette RAM equals the routine's output for frame - 1 in 2630 of 2656 MAME dumps (the rest are attract mode,
  where it doesn't run). Tool: `tools/m2_palette_fx.py check`; engine: `src/assets/arcade_palette_fx.*`; the viewer animates
  the entries per frame (vertex attribute = palette entry, 1024-entry override texture).
- Not covered: entry 985 (work RAM 0x5206AC, written by 0x3D3D4 / 0x42C78, probably a gun or hit flash), the flag events.

## 25. Character categories (2026-10-09)

Table in `src/assets/arcade_categories.cpp` (`CreatureCategories`): one entry per arcade creature type (the 94 skeletons of
§23), applied to every part ID of that type's skeleton; the PC shares the IDs, so it serves both sources. Categories:
**zombies** (every enemy, including dogs, frogs, leeches, crawlers), **bosses**, **humans** (scientists, agents, Sophie;
viewer F5), **weapons/effects** (unchanged: what creature_poses.py calls effects).
- By name (main_data name table, §22): `ken*` scientist (types 17, 19-22, 49; the bridge victim), `onn*` woman (onna; 23,
  25-27), `sof*` Sophie (34), `zsof*` Sophie variant (67?), `hara*` / `gt_hhara*` / `harihara*` / `stahara*` men (2, 13-15),
  `kyuter*` / `kyubuder*` (60, 78; 20-joint human skeletons with room parts: probably rescue scenes).
- Bosses: `hyum*` Chariot (35 armoured, 36 armour off), `elien` / `devilon*` Hangedman (50, 69), `boss3*` Hermit (64),
  `boss4*` Magician (76). Boss parts that no skeleton lists (bosses draw some parts themselves) are bosses by name prefix
  (hyum, devilon, elien, boss3, boss4), except the small Devilon swarm (type 55: `devilonm_*`, `devilon_*_min`), which is
  an ordinary enemy. Shadow models (`<part>_DD`) follow their part.
- Everything else is zombies, including Taran (`taraba*` / `tarabab*`, types 48 / 90; until 2026-10-15 wrongly called
  the Hermit). The type table now carries the official names of docs/names.md.
- Before: creature_poses.py guessed from replay frames (in a boss frame, IDs not seen with zombies = boss), which made Sophie
  a boss and the Chariot a zombie. The viewer now overrides those categories from this table.

## 26. Camera rails: CAMALL.RBH (2026-10-09)

- `CAM\ALLSTAGE\CAMALL.RBH`: "PIFF" container, "BODY" chunk at 0x20, data at 0x28. A u32 offset per path (the first offset
  / 4 = the count: **142 paths**); each path = 6 u32 offsets to its channels (eye x, y, z, target x, y, z). Channel: u16
  slot count (4, 8, 16 or 32), u16 (2-5, maybe log2), then slots x {frame, value, in-tangent, out-tangent} floats; unused
  slots repeat the last frame. All offsets are relative to the BODY data. Frames at 60 per second.
- Interpolation: cubic Hermite between keys, tangents per frame (h00 v0 + h10 (t1 - t0) out0 + h01 v1 + h11 (t1 - t0) in1).
- **The arcade plays the same data:** the eye position in MAME work RAM (view matrix 0x51EA50) equals the spline exactly
  (error 0.000) in every Chapter 1 dump checked. Work RAM 0x5209E0..: path state (frame counter, path, time as float).
- Chapter 1 order from the replay: path 0 (intro, 860 frames) -> 2 -> 4 -> 5 -> 6 (bridge, -424) -> ...; 1 and 3 are
  alternatives of 0 and 2 (branches). The game's time stops at the stops (waiting for zombies), driven by the event
  scripts; the look direction sometimes differs from eye -> target (e.g. t00760: pitch -11 degrees vs 0), also scripted.
- Viewer: Y rides paths 0, 2, 4, 5, 6 at 60 frames/s (no stops, no look offsets yet). Engine: `src/assets/camera_paths.*`.
- The 7-float records at main_data 0xF12740 (§22) are something else (small values; not this rail).

## 27. Alternate-state placements and event objects (2026-10-10)

- **Alternate states:** 21 groups of placement-table entries share one position (Chapter 1: 14, Chapter 2: 4, Chapters 3-4:
  3). In the replay, 17 of them are never drawn together (one at a time): level-of-detail or state versions chosen by the
  game. Example, the mansion entrance (0, 40, -90): 0x9E5 from far (courtyard), 0x9E4 (with closed doors built in) from the
  bridge, 0x9E3 close up; at the doorway the game draws the real door objects 0x73A `to0a` / 0x73B `to0b` instead (they
  open). With the corrected name table the three are `niwa01_03a` / `_03b` / `_03c` (the old "ono" / "pdolob_aata" were
  the drifted names).
  `tools/arcade_states.py` writes `private\arcade_states.txt` (group lines + the camera positions where the replay drew each
  member); the viewer shows per group the member drawn nearest to the camera, members never drawn stay hidden. With that
  file the loader keeps all placements (ArcadeConfig::keepAlternates) instead of its old hand-made list.
- **Event objects:** objects the game draws that are neither placed nor creature parts. `tools/arcade_props.py` records each
  one's world transforms and the camera positions that saw them (`private\arcade_props.txt`, 3281 transforms: doors, clock,
  bats, thrown pots and test tubes, blood, damage-variant creature parts ...). The viewer uses only the doors for now (names
  with "door" or starting "to0") and the Chapter 3 mechanisms (`r3_*` gates, floor gratings `_ami`, elevator floors `_el`):
  the transform of the sighting nearest to the camera, within 80 units, so doors open as the camera passes. Real placement
  and movement come from the event scripts (not emulated yet). Known flaw: the moving elevator `r3_07a_elel01a` can hang in
  front of the camera during the ride (MAME t08200), because the nearest sighting doesn't follow it.

## 28. PC sky, PC lighting, event opcode names in THOTD.EXE (2026-10-10)

- **PC sky:** the PC has the arcade's sky models (0x608 `skyuv02a`, 0x137C, 0x14B9, 0x1D20, 0x1238) in
  `ART\STAGE1\CREATURE\CREAT2.RBH` (not in the WORLDn.RBH files). The event script switches them (opcodes 0x46 SKYKD, 0x47
  SKYTB below). The viewer draws the PC model where the arcade's sky table puts one (same zones, same offsets).
- **PC lighting:** not found yet. The PC world polygons have no vertex colors or shade values (kinds 0-4: indices, texture
  word, two unknown halfwords 0 / 0xFFFF, UVs); `STAGEn.TBL`, `HOTD.TBL` etc. are 256 x 256 tables (palette blending for the
  8-bit 2D layer, probably not lighting). The renderer (Direct3D, `IDirect3D2` era, function table at 0xC66EFC) has to be
  read in the executable; no x86 disassembler is installed here (see TODO).
- **Event script opcode names:** THOTD.EXE.orig keeps a debug name table at file offset 0x18AE00 ("0x00 : nop" ... "0x5f :
  nop", 16-byte strings). Named opcodes: 01-04 ET1PE/ET1P/ET1P2/ET1P3, 05-08 ET2PE/ET2P/ET2P2/ET2P3, 09-0C ENTBE/ENTB/ENTB2/
  ENTB3 (enemy tables), 0D SKPETO, 0E SKPETU, 0F SKIP, 10 SCNINIT, 14 BGTB, 1A EV3JP1, 1E PLTB, 20 GMTMF, 21 GMTAD,
  22 CLRGFST, 28 WLTB, 32 CLTB (color table: the color-row events of §19?), 33 KGTB, 34 BRTB, 3C CCTB, 41 BSFT, 42 CAMDR2,
  43 PYPMD, 44 SHDWLT, 45 CGMFNSH, 46 SKYKD, 47 SKYTB, 48 FKRTB, 49 CHGSM, 4A NCHGCF, 4B CAMDR (camera drive), 4C GGCOL,
  4D GGSET, 4E REMFRM, 4F TEXCHG, 50 WAIT_ENNUM/EVCOM/EVTB1, 51 WAIT_FRM/EVTB2, 52 WAIT_ITEM/EVTB3, 53 WAIT_CAMJOB/EVTB4,
  54 WAIT_CAMFRM/EVTB5, 55 WAIT_ENALV/EVTB6, 57 RNWCJ, 58 ITEMON, 59 EBLFNSH, 5A SNDON (= 90, the "play music" instruction
  of §4), 5B ATCMD, 5C WAIT, 5D NEXTEV; the rest nop. These are the names for the EVTALL.RBH interpreter (playable slice).

## 29. Event scripts: EVTALL.RBH layout and instruction lengths (2026-10-10)

Tool: `tools/evt_dis.py` (needs THOTD.EXE for the opcode names of §28).
- BODY (file offset 0x28; every offset is relative to it): u32 offsets of 7 event lists, ended by -1: Chapters 1-4 (20, 16,
  8, 5 events), then lists of 2, 1 and 8 events (endings / demos?). Each list: u32 event offsets, ended by -1.
- Event: u32 **section offsets** ended by -1 (each points just after a NEXTEV: resume points / continue checkpoints;
  some point into another event's code, shared code), then the code, then (after the last NEXTEV) the event's data tables,
  which the code references by offset (ENTB*, ET*, FKRTB, WLTB ...).
- Instructions: int32 opcode + int32 operands. Operand counts (from the data; all 60 events decode to their end):
  0: NEXTEV, SCNINIT, WAIT_CAMJOB, WAIT, CAMDR2, EV3JP1 - 1: SNDON, BGTB, FKRTB, CHGSM, SKYKD, SKYTB, WAIT_ITEM,
  WAIT_FRM, WAIT_CAMFRM, WAIT_ENNUM, CAMDR, ITEMON, SKIP, GGSET, WAIT_ENALV, WLTB, TEXCHG, ATCMD, PYPMD, BSFT -
  2: SHDWLT, GGCOL, EBLFNSH, BRTB - list ended by -1: ET1PE..ET2P3 (1-8), ENTBE/ENTB/ENTB2/ENTB3 (9-12), CLTB, KGTB, RNWCJ -
  CCTB: sub-operation + operands: **64 start end a b** = play the camera path from frame start to end (start -1 = from
  where it is), 33 x y, 16-21 one value.
- **The rail with its stops:** e.g. Chapter 1 event 0 (intro): CCTB 64 0 89, WAIT_CAMJOB, CCTB 64 90 309, WAIT_CAMJOB,
  WAIT_FRM 54, SNDON 7, CCTB 64 310 385 ... SNDON 9 (Chapter 1 music), CCTB 33 4 0, WAIT_ENNUM 0 (wait until no enemies
  are left), CAMDR 3 (drive on) ... The camera path of an event isn't settled: event 1 plays frames 0-330 = path 2 (330
  frames, matches the replay), but other events don't map 1:1 to path numbers (alternates for branches: paths 1, 3).
- **Enemy records** (first look; e.g. Chapter 1 event 1, the ENTB/ENTB3 offsets 2440-2960): 40 bytes = +0 u32 kind (0, 96,
  99), +4 u32 flags (0 / 0x80000000), +8 x, y, z (floats, world; y = -25 = the courtyard floor, z -1140 ... -690 along camera
  path 2), +20 0, +24 u32 facing (16-bit angle, e.g. 0xA000), +28 0, +32 u32 = placement mode (low half) and **the
  enemy's hit points** (high half: 100, 110, 120, 230, 300; corrected 2026-10-21: the first look took them for an appear
  frame; evt_ENTB copies them to object +0x70 / +0x72 and the replay dumps show +0x70 dropping with each hit while
  +0x72 keeps the record value; decompilation.md "Enemy spawn records"), +36 u32 ID with flags in the high
  half: **creature type = ID & 0xFF** (confirmed against the replay: the creature objects in the work RAM dumps
  t00560-t00700 stand at the records' positions, and their type +0xCC is 0x0129 -> 41, 0x012A -> 42, 0x012B -> 43,
  0x0100000B -> 11, 0x0100001C -> 28; the high bits are behaviour flags, still open). The shared tables at offsets
  1332-1360 (referenced by every Chapter 1 event) are a different, variable structure (pairs like 178 / 0xFFFF0000, 161 /
  0xFFFF0001, then a 99-record with a position).
- Next: the frame field against the replay timing, the record kinds 0/96/99 and flags, the event -> camera path mapping, the WAIT_* conditions; then an interpreter for the playable slice.

- **Camera path = the 4th CCTB 64 operand:** `CCTB 64 start end path mode` plays frames start..end of CAMALL path `path`
  (mode 0 / 2, meaning open). Chapter 1 event 0 uses path 0, event 1 path 2, event 3 paths 4 then 5 (frames 0-474, 0-255).
  Confirmed with the replay: the arcade program ROM holds the **same event scripts in the same format** (absolute
  addresses instead of BODY offsets; Chapter 1's event list at maincpu 0xE0030, 20 events from 0xE0084), and the arcade's
  script pointer (work RAM 0x52092C) moves through events 0, 1, 3, 4, 5, 6 ... while the camera (fitted to CAMALL) plays
  paths 0, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, ... 29, 30, 31 (Chariot). Event 2 (path 3) is a branch the replay didn't take.
- **PC sky vs the real PC game** (user screenshot `private\screenshots\pc_reference\`): the real PC sky in the courtyard
  is grey-green; the viewer draws the PC model 0x608 with a beige texture. The scripts switch skies (SKYKD / SKYTB, e.g.
  event 0: SKYTB 2, event 1: SKYTB 1) and the PC has several sky models (skyuv02a, skyuv, skyuvb, ...), so the real game
  probably shows another sky model there (or tints it). To check when SKYTB is interpreted.

## 30. PC sound files: GAMESNDS.RBH, O_*.RBH and the full cue table (2026-10-15)

Read from the PC's `sound_cue` (0x412000) and the sound set-up (0x411F10: 8-bit, 11025 Hz, 3 = mono / 8-bit flags),
then checked by playing the samples in the rewrite (`hotd`, src/game/audio.cpp).

**Cue table** (VA 0x5CBDA8, 269 cues 0..268; cue 269 starts the next table, the menu strings), 12 bytes per cue:
`{int32 type, char* name, int32 value}`. The music rows of §4 are the type-3 cues. Types:

| Type | Meaning | value |
|---|---|---|
| 0 | nothing | -1 |
| 1 | play a sample of GAMESNDS.RBH (sound system call 0x405) | sample number (0..0x3F) |
| 2 | stop every voice playing a sample (0x40F find, 0x406 stop) | sample number |
| 3 | CD track (MCI; the mod: `Music\TrackNN.ogg`); loops, except cues 11 (game over) and 13 (stage clear) | track, -1 = none |
| 4 | load `SOUND\O_<name>.RBH` into the bank's load area (0x401), then play it | sample number (0x40..0xD5) |
| 5 | command: 1 = music off, 2 / 3 / 7 = every sample off (0x407), 4..6 nothing | command |

**GAMESNDS.RBH** (PIFF / RBHF, §2): `RBHH`, BODY 0 = 214 sample entries of 40 bytes, BODY 1 = the O_ files' 8-byte
header (`{u32 load offset 0, u16 1, u16 1}`) then the bank (8-bit unsigned PCM, 0x80 = silence; its last 100,616 bytes
are the load area the O_ files go into). The entries' offsets count from after that header (THOTD 0x57BFC0: BODY + 8 +
offset - the load offset; read from the header on, each sample starts with 8 bytes of the one before, mostly 0x00:
GitHub #3, the chainsaw's click). Entry, little-endian:

| Offset | Size | Field |
|---|---|---|
| 0 | u16 | bits per sample (8) |
| 2 | u16 | volume 0..255 (255, 250 or 128) |
| 4 | u32 | length in bytes (= samples) |
| 8 | u32 | rate (11025) |
| 12 | i32 | -1 |
| 16 | u32 | source: 1 = in this bank, 2 = an O_ file |
| 20 | u32 | 1 for the bank; the O_ file's number for source 2 |
| 24 | u32 | offset in BODY 1 (source 2: always the load area, 535,600) |
| 28 | u32 | loop start (non-zero: loops from there; e.g. sample 0x41 ZISIN, the earthquake) |
| 32 | u32 | 0 |
| 36 | u32 | 16.16 value: 250.0 / 128.0 / 505.0 / 511.0; bit 24 is set on the samples that have a stop cue (taken as "loops") |

**O_*.RBH** (150 files): `RBHH` (12 bytes: 0, BODY size, 0), one BODY = `{u32 load offset (535,600), u16 2, u16 file
number}` then the 8-bit data (the entry's length).

**Arcade sound codes to PC cues.** The arcade's `sound_cue` (0x154D0) takes a sound-board command (0xA0xxxx: 0xA010nn
music = PC cue 8 + nn, 0xA012xx / 0xA013xx / 0xA014xx effects and voices); the PC table keeps the arcade's order with
gaps. `tools/cue_pairs.py` pairs them: 79 codes from the routine pairs (the `sound_cue` constants of both versions'
decompiled C, routine by routine, no conflicts), 31 more from the event scripts' SNDON operands aligned between the
arcade ROM and EVTALL.RBH (taken only where the order agrees with the pairs around them), music by the rule: 122
codes. Result: src/game/cue_map.inc. Some arcade sounds have an empty PC cue (type 0: e.g. 0xA0122A, the metal spark).

**The sound board's protocol (arcade).** Each command goes from the main CPU to the board as 3 bytes, high byte first (0xA0 xx yy sound codes; 0xBE xx yy, e.g. 0xBE5000 with 0xA00002 at game over / chapter starts, not decoded), through the i8251 UART at 0x1C80000 (data +0, status +2) at the MIDI rate into the SCSP's MIDI in; the 68000 sound program (epr-19720) reads them. Details and the board's memory map: docs/teaser_status/sound_board.md.

## 31. The 2D sprites: arcade sprite banks and tilemaps, PC SPR.RBH (2026-10-16)

### Arcade (Model 2 + the System 24 tile chip)

The 2D layer is the System 24 tile chip MAME emulates for Model 2 (`segas24_tile`, tile mask 0x3FFF):

| Address | What |
|---|---|
| 0x1000000 / 0x1002000 / 0x1004000 / 0x1006000 | tilemaps 0..3, 64 x 64 cells of u16 (0x80 bytes a row). 0 and 2 are the main layers, 1 and 3 their "window" layers |
| 0x1008000 | line scroll tables (cleared, unused in Chapter 1) |
| 0x100A000 / 0x100A008 | x / y scroll of each tilemap (y bit 15 = layer off); the shadow 0x51E8F4 is copied at vblank |
| 0x100C000 / 0x100D000 | window masks of tilemaps 0 / 1 and 2 / 3: 8 bytes (64 cell bits, bit 15 of word 0 = cell 0) a screen line, 384 lines; a set bit shows the window layer, a clear bit the main layer |
| 0x1080000 | character RAM: 0x4000 tiles of 8 x 8 x 4 bits, 32 bytes each (a row = two little-endian u16, pixel 0 = the top nibble), used as 128 slots of 128 tiles |
| 0x1800000 | palette RAM, 16 RGB555 colours a palette; the game keeps a shadow at work RAM 0x515CA0 (256 palettes) |

A cell: bits 0-13 tile, bits 7-14 palette (so a slot's tiles use the slot's palette; bit 14 selects the upper 128),
bit 15 priority (1 = over the 3D picture). The screen shows cells 0..61 x 0..47 (496 x 384). MAME draws the layers 3,
2, 1, 0 (0 on top) into an RGB bitmap and copies it with black as transparent: a black pixel is see-through even over
a lower layer (the full-screen black sprite 0xA1 is invisible until a palette fade colours it).

Sprites: table **0xA6590**, 12 bytes a sprite (0x276 sprites): pointers to the tiles, the palettes and the layout (in
the program ROM or main_data; 0x6000000.. is main_data's upper half). Tiles: pieces `{u32 count, count x 32 bytes}`
ended by a zero count, each piece starting on a new slot; palettes: one 32-byte palette a piece; layout: `u16 w, u16 h`
then w x h u16 cells (the tile within the bank). `sprite_bank_of` 0xA9B60 gives each sprite's bank (s16). Bank records
at 0x5123F0 (0x80 x 16 bytes: first slot or -1, slots, pieces, locked, tiles source), slot use at 0x512C00.
`sprite_bank_load` (bank, sprite) 0x17D50 allocates slots and copies the tiles / palettes; `sprite_place` 0x17D80 loads
and writes the cells (slot x 0x80 + b x 0x4000 - a x 0x8000 + cell: a = 1 sets the priority); `sprite_draw_anim`
0x17E40 writes them; `sprite_draw` 0x180B0 clears them. Subtitles (`tile_text_draw` 0x9C450): each character's sprite
from table 0x9C250, one cell further a character, with bank 0x2E's palette. Implementation: src/game/sprites.cpp
(the routines) and src/assets/sprites_arcade.h (the compositor); `tools/sprites_tool.py arcade` draws any sprite.

### PC (SPRITE\SPR.RBH + BIGGER.VMC pages)

`SPRITE\SPR.RBH`: `PIFF <size> RBHF RBHH <u32 12> <12 bytes> BODY <u32 size> <data> RELC ...`. The BODY data begins with
1640 u32 offsets (relative to the data), one a sprite ID. A sprite: `u32 pieces; s16 x0, y0, x1, y1 (box); s16 width,
height`, then 16 bytes a piece: `s16 x0, y0, x1, y1` (inside the sprite), `u8 u, u8 v` (in the page), `u16 0`,
`s16 page`, `s16 flags` (1). Units: 640 x 480 screen pixels. The pixels are pages of `TEXTURE.VMC` / `BIGGER.VMC` (§3:
page 7 holds the font: sprite = character - 0x1E, and "Demo"), magenta 0x7C1F transparent.

The PC keeps sprite IDs instead of cells: two layers of 64 x 48 u32 (0x5D24F4 / 0x5D24F8); `sprite_draw_anim`
0x4434B0 clears the cells the sprite covers ((x1 - x0) / 8 x (y1 - y0) / 8 from the box) and stores the ID in the
top-left cell; `pc_sprites_render` 0x4432F0 draws every ID at its cell (8 units of a 512 x 384 grid) through the display
driver; `tile_text_draw` 0x41B1C0 only keeps the subtitle's text pointer (0xD5A5CC), drawn in the PC's font.
The PC's sprite IDs differ from the arcade's (bullet 0x10 -> 0x44A, RELOAD 0x36 -> 0x421, CONTINUE? 0x113 -> 0x51B ...):
src/assets/sprite_map_pc.inc maps all 0x276 arcade sprites to the PC sprite with the same picture
(`tools/sprites_tool.py map`, thumbnails compared; checked by eye for the HUD, continue, game over and boss sprites).
Implementation: src/assets/sprites_pc.h.
