# Texture packs and custom models

Texture packs and custom models change how the game looks. They never change how it plays: in online co-op, each PC
can use its own packs. Nothing of Sega's is shipped. You make a pack from **your own** files with the export commands
below, then edit the pictures.

## Where packs go

Each pack is a folder:

- `data\mods\<pack>\` in the game's folder, or
- `mods\<pack>\` in the settings folder (next to `launcher.ini`), or
- a folder you name with `--mods-dir <folder>`.

```
data\mods\yellow-blood\
    mod.ini                 (optional)
    textures\               PNGs, named by texture ID (sub-folders are fine)
    models\                 OBJ + MTL + PNG, named by object ID
```

`mod.ini` (every line is optional):

```ini
[mod]
name = Yellow Blood
author = you
version = 1
description = ...
models = arcade         ; arcade / pc / both (default both): the model source the pack is for

[recolour]
blood = #FFD820         ; the blood colour (works like Blood Color: Custom; only while Blood Color is Red)
*-c1023 = tint #FFD820  ; <pattern> = tint #RRGGBB | hue <degrees> | multiply #RRGGBB
```

## Turning packs on and off

A texture pack is one kind of mod (`type = textures`, the default): the folder, `mod.ini` and the Mods page are
explained in [docs/modding.md](modding.md). In short: Options -> **Mods**. Every mod found has a row (On / Off). Mods
that are On come first, by priority: when two packs replace the same texture, the higher one wins. **Move Up** raises
a mod. A texture pack's change applies the next time a chapter starts. The choices are saved in `launcher.ini`
(`mods=a,b` for the On mods in order, `modsoff=c` for the Off ones). A new pack is On by default (`default = off` in
its `mod.ini` keeps it off until switched on).

Command line: `--mod <pack>` (you can repeat it) uses only those mods, and the first pack wins. `--no-mods` uses none.
`HOTD_NO_MODS=1` turns texture packs off in the renderer.

## Texture IDs (file names)

A picture's ID is the **last part of its file name, after the last `_`**. Anything before that is up to you
(`zombie-face_3f09c1d2aa804b17.png` and `3f09c1d2aa804b17.png` are the same texture).

| Models | ID | Meaning |
|---|---|---|
| PC | `p<page>` (e.g. `page_p0550.png`) | A `BIGGER.VMC` page, 256 x 256, holding many small textures. |
| Arcade | 16 hex digits (e.g. `neil_kao_0adc233bed58a045.png`) | A hash of the texture's texels and size. The same picture has the same ID in every chapter and texture area. |
| Arcade | `<16 hex>-c<entry>` (e.g. `..._63749d296fdbe8ae-c1023.png`) | The same texture, but only where it is drawn in polygon palette entry `<entry>`. Entry 1023 is the blood colour. |

The arcade colours its 4-bit textures per polygon (palette entry, lighting, colour tables). So one arcade texture can show
in several colours. A plain `<hash>.png` applies everywhere the texture is drawn, and `-c<entry>` applies to one colour
only. The export lists the entries each texture uses (`palettes` column).

### Pictures

- PNG files of any size up to 4096 x 4096 (pictures larger than 2048 are halved to fit). Any colour type works, but not
  interlaced PNGs. Alpha is kept, and pixels with alpha under 50 % are transparent.
- A picture of a different size than the original is stretched over the same area and filtered, so HD packs work.
- **Arcade:** the export writes each texture's colours at full light. The game then compares your picture with that
  reference:
  - Your picture is an edit of the export: your changes are added on top of the game's own colour for each pixel. Lighting,
    palette animation and the colour swaps the game makes (the bright red blood) all stay. An unchanged picture looks
    exactly like the original, and it is skipped.
  - Your picture is repainted, or comes from another chapter's colours: your colours are used at the game's brightness.
- **PC:** your picture's colours are used as they are. The grey look applies to them too.

## Exporting your textures (a starting point)

```
hotd --export-textures <folder> [--models arcade|pc]
```

This writes `<folder>\arcade\` or `<folder>\pc\` with one PNG per texture, named `<label>_<id>.png`. The label is the first
model that uses the texture. It also writes `index.tsv`, which lists for each texture its ID, file, size, flags (`blood`,
`translucent`), chapters, palette entries and the models that use it. For every arcade texture drawn in the blood colour,
it also writes `blood-<label>_<id>-c1023.png`.

The files are made from **your** ROMs or PC install. Don't share exported game art. Share only what you drew yourself.

### Example: yellow blood (arcade)

1. `hotd --export-textures C:\work\hotd-export`
2. Copy the `blood-*-c1023.png` files into `data\mods\yellow-blood\textures\`.
3. Recolour them yellow in any editor.

Or, with no pictures at all, use a `mod.ini` like the one above (`*-c1023 = tint #FFD820` plus `blood = #FFD820`). The PC
release already has yellow blood (Blood Color: Yellow). On the arcade, the wounds painted on the bodies are separate
textures in skin palettes, so recolour those by name if you want them changed too.

## Recolour rules

In the `[recolour]` section, write `<pattern> = <operation>`. The pattern is matched against `<label>_<id>` and against the ID
alone, where `*` matches anything and `?` matches one character. Examples: `*-c1023`, `neil_kao_*`, `p0550`.

The operations are:

- `tint #RRGGBB`: each texel's brightness, in that colour.
- `hue <degrees>`: shifts the hue.
- `multiply #RRGGBB`: multiplies each channel by that colour.

Rules apply on top of a pack's picture, or on top of the original's exported colours. The `blood = #RRGGBB` key sets the
blood colour, the same way the Custom blood colour option does.

## Custom models (OBJ)

```
hotd --export-models <folder> --model-ids 781,a23 [--models arcade|pc]
```

This writes `<folder>\<arcade|pc>\models\<label>_o<id>.obj` with its `.mtl` and pictures. The model stays in its own
space (x right, y up, z forward, left-handed), so it keeps the same origin and the same part layout. Edit it in any 3D tool,
then put it in a pack's `models\` folder. The file name must end with `_o<object ID in hex>`.

- Materials keep the export's names: `tex_<texture ID>-c<entry>` on the arcade, `page_p<page>` on the PC. While their
  picture is unchanged, they draw the original texture, with the game's colours and lighting. If you repaint the
  picture, your version is drawn over the original's colours, like a texture picture.
- Any other material with a `map_Kd` PNG draws that picture, using the polygon's light on the arcade. A material without
  a picture uses its `Kd` colour.
- OBJ: `v`, `vt`, `f` (polygons, negative indices), `usemtl`, `mtllib` (`newmtl`, `Kd`, `map_Kd`). Normals are computed
  per face, and faces are drawn from both sides.
- **What works:**
  - **Props and world objects:** replace any model by its ID.
  - **Animated creatures:** each body part is its own model ID (for example the zombie head is `0x781`), posed by the
    game's skeleton. Replace a part with a model in the **same part space**. The export gives you that space. Moving the
    pivot or merging parts is not possible, because the skeleton and the hit areas are the game's.
  - **Hit detection:** it never changes. Shots use the game's own collision.
- glTF 2.0 (`.gltf` / `.glb`), a scale / rotation / offset, `game:<id>` (another model of the player's own files) and
  per-model-set IDs: a `[model]` section in `mod.ini` (docs/modding.md "Model packs", 2026-11-09).

## Next

- Mip levels for large pictures. Pack pictures are filtered but not mip-mapped, so HD pictures shimmer a little far away.
- The 2D sprites (HUD, the PC's blood drops) are not covered yet.
- Arcade palette rules (recolour a palette entry, including animated ones like the fire lamps) without pictures.
- Each stage's pictures are packed into 2048 x 2048 atlas layers, with at most 31 layers. That is about 500 MB of
  pictures at most, so a full HD repaint of every arcade texture at 4x does not fit yet.

## How it works (code)

- `src/assets/mods.*`: finds packs and reads `mod.ini`, gives textures their IDs (`listStageTextures`), handles the
  export, and builds `TexturePacks::build` (the pictures for a stage, the reference images, the colour-only virtual keys,
  and the OBJ models).
- `src/render/renderer.cpp`: `uploadReplacements` packs the pictures into an atlas, with a 512 x 517 RGBA16UI map per
  texture set from shader key to atlas place, plus a reference map. `replEntry` and `replSample` in the fragment
  shader do the lookup. Arcade keys are worked out from the polygon's texture rectangle, and the PC key is the page
  layer. Colour-only and model pictures use virtual keys carried in `Vertex::lodCoef` bits 5-15.
- `src/util/recolour.*`: the one recolour helper, shared with the 4-player colours.
- Netplay: packs are visual only. Nothing in the runtime reads them.
