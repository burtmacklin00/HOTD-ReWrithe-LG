# Render fixes: status (2026-10-15)

Branch `render-fixes` (build folder `build-render`). Shared code (hotd_engine: src/assets, src/render) and both
front ends: `hotd` (src/game/view.*) and `hotd_viewer` (src/viewer/main.cpp). Screenshots in this session's
scratchpad only (none committed; private/ stays out of git).

## The problems the user reported, cause and fix

### 0. Arcade textures change at some distances (reported during this session) - FIXED (both programs)

- **Cause:** Model 2 mip levels. Level L of a texture lives at ((x - 2048) >> L) & 2047, ((y - 1024) >> L) & 1023
  on the other sheet for odd L (MAME, as the shader already does). Level 1 is in the ROM blocks (checked against the
  MAME dump t00300: the other sheet's quadrant x 1024+, y 512+ is a 2 x 2 average of level 0, 90 % within one shade).
  **Levels 2 and up all land in the corner the program generates at area load** (sheet x 1536-2047, y 768-1023,
  formats.md §17), which `ArcadeSource::buildTextureRam` (the `rom:<n>` sets of arcade_texsets.txt) left at 0. So at
  the distance where the per-pixel level reaches 2 a polygon showed shade 0 / unrelated texels, and switched back
  when the camera moved closer or further.
- **Fix:** src/assets/arcade_loader.cpp (loadTextureSet, ROM sets): levels 2-10 generated as a rounded 2 x 2 average
  of the level above, in the original's addresses. Against the dumps (t00300, t02200): 37-44 % of the corner's texels
  equal, 79-88 % within one shade, about as close as the ROM's own level 1 is to that filter. The program's exact
  filter (maincpu 0xCDC-0x1000) is not read yet; reading it would make the corner exact.
- **Before / after** (viewer, Chapter 1 courtyard looking at the mansion from z = -700 and -1000): before, the iron
  fence behind the hedges and the ground to the right were a dark band / wrong texels; after, they show their own
  texture. Close up (z = -426) nearly nothing changes (265 px); `nomip` renders are identical before and after.

### 1. Both texture sets / both copies drawn, flickering when things move - FIXED (cause found, two parts)

- **Cause a (hotd):** GameView drew the whole world (every placement of the chapter, `resolveScene`) with no draw
  zone. With draw zones in the arcade data the loader keeps every alternate state (the mansion entrance 0x9E3 /
  0x9E4 / 0x9E5, doors, ...) for the zone to choose, so all of them were drawn at the same place: z-fighting copies
  that flicker as the camera moves. The PC world (WORLD1.RBH has a separate object per state) had the same problem.
- **Cause b (hotd and viewer):** the arcade texture set (`Mesh::variant`) came from `StageData::zoneVariant(cam.pos)`
  = the nearest replay-derived zone point, which flips between sets as the camera moves. In the viewer this was the
  case whenever draw zones were off (the default, key Z), also during the script ride.
- **Not the cause:** two model sources at once. `resolveScene` picks one source per ID; the game's draw list takes
  the PC model when the PC has one, else the arcade's, never both.
- **Fix:** hotd: the zone is the game's own `draw_zone` (work RAM 0x51FDD4, set by BGTB), the texture set the last
  TEXCHG area (`Runtime::texture_area_load` 0x34FF0 now records the area: `Runtime::textureArea`; the chapter's first
  set before the first TEXCHG). Viewer: during the script ride the runner's BGTB zone and TEXCHG area are applied
  without Z (the free-camera zone pick stays behind Z, it is still wrong in places); the sky's set follows the area.

### 4. Floating textures in front of the mansion - FIXED with 1a

- **Cause:** same as 1a: pieces of other zones (the courtyard's, the mansion interior's) were drawn everywhere.
- **Fix:** hotd draws only the current zone's placements, as the world task 0x4B300 does; zone 0 draws none (world_init
  sets 0 until the first BGTB). **The PC world is culled by ID**: the PC's own world task (THOTD.EXE 0x491C48, called
  from world_init 0x491CD0) walks the same zone lists (byte placement indices, 0x18-byte placement records at
  DAT_00da60b0) and draws every mesh of `DAT_00d53080[id]`, so a PC object is drawn when its ID is in the zone's list.
  PC objects whose ID is not in the arcade's placement table stay drawn (how many there are per
  chapter: not counted). Same culling in the viewer when zones apply.
- **Before / after** (viewer ride at 20 s, PC models): before, extra tree rows and a hedge band from other zones
  stood behind the path; after, only the zone's trees. Arcade models: 11,538 px changed, same areas.

### 2. PC mode: no PC sky, no PC lighting - sky FIXED, lighting FIXED (the PC has none: unlit, culled, alpha)

- **Sky:** GameView drew the arcade's sky models even with model source PC. Now it requests the sky model IDs from
  the PC source (CREAT2.RBH has 0x608, 0x137C, 0x14B9, 0x1D20, 0x1238, formats.md §28) and draws the PC's model where
  the arcade's sky table puts one, as the viewer already did. (The game's SKYKD / SKYTB state, work RAM 0x51F77A /
  0x51F77B, is not used yet: the sky follows the texture area as before.) Before / after (`hotd --models pc`, frame
  1100): before, the arcade's sepia sky over the PC world; after, the PC's own grey sky.
  **Seams fixed (render-lighting):** thin vertical lines between the PC sky's faces came from REPEAT wrapping with
  bilinear mipmaps at the face edges; backgrounds (pass 0) now sample with texture coordinates clamped half a texel
  inside the rectangle, base level only (`uClampUV`).
  **Horizon band (render-leftovers): looked at, left as it is.** Chapter 1's sky is two CREAT2.RBH models drawn around
  the camera (0x608, the dome, y -41..132 at offset y -9; 0xA29, y -317..812). The dark strip between the sky's lower
  edge and the ground is in the arcade original too (MAME snapshots of the courtyard,
  private\mame_dumps\fullgame_us\snap\hotd: a dark band of far hedges / darkness under the clouds), so it is not changed. Not compared with the PC port.
- **Lighting: the PC has none. FIXED (render-lighting).** Read in THOTD.EXE: geo_draw_model (0x43D7F8) and the world
  task (0x491C48) queue meshes with 0x43D948; the callbacks 0x43DD1C / tlPOLY_PlotMesh 0x491FB4 do a bounding-sphere
  frustum test (0x43DB5C / 0x491DFC) and call the polygon dispatcher **0x578BB0**. Its transform (0x5792B0 clipped /
  0x5793E0 unclipped) reads 12-byte xyz vertices only (no normals) and writes 0x1C-byte screen vertices (x, y, z,
  1/z, clip flags at +0x18). The dispatcher calls the Direct3D driver table at 0xC66EF8 (installed by 0x573CA0 into
  the pointer 0xC649E0) per polygon kind: +0x78 untextured triangle (0x577CA0), +0x7C untextured quad, +0x80 textured
  triangle (0x577870), +0x84 textured quad. Those build D3DTLVERTEX records with **diffuse = alpha << 24 | 0xFFFFFF
  for textured polygons** (white: the texture as it is) and a **flat palette color** for untextured ones (palette
  [record u16 after the indices], 0xFF080808 for 0); no light vector, no per-vertex term, specular not written.
  Polygon group head bits (high half): **0x80 = alpha 0x7F** instead of 0xFF; **0x200 = double-sided**, else the
  back-face test 0x578F60 (drawn when the projected 2D cross product is >= 0); bits 13-15 pick the depth-sort key
  (table 0xC67644: max z 0x579180, average 0x5791D0, ...). Our renderer had shaded the PC with an invented light
  (0.3, 0.8, -0.5) / ambient 0.45 (darker, flat-lit faces) and drawn everything double-sided and opaque.
  **Now:** DirectRgba polygons are drawn unlit, single-sided ones culled (sign checked by eye: the other sign shows the
  interiors through the walls), 0x80 polygons at alpha 0x7F with blending (TexFlag `kHalfAlpha`; pc_loader keeps the
  head's high half). Before / after (`hotd --models pc`, frame 1100; viewer PC start view): brighter, unshaded PC
  models and ground as the PC draws them; same geometry.
- **Untextured PC polygons (render-leftovers): cause found, still grey.** 0x577CA0's color is
  `[[mesh + 8] + 4 * index] & 0xFFFFFF` (index = the u16 after the vertex indices; 0 -> 0xFF080808). In the files the
  mesh header's +8 field relocates to the header itself (RELC, BODY 0), so it must be set at run time (not found:
  pc_chapter_files_load's 0x419CAC / 0x419CF4 only store the header pointers in DAT_00d53080). Next: find the writer
  of header +8 (a watch in the PC harness, or the callers of 0x5769C0). Few polygons are affected (WORLD1: 92 untextured of 13,456).
- **Alpha-0x7F polygons (render-leftovers): drawn after everything opaque.** pc_loader puts them in a mesh of their own
  (`Mesh::blend`); the renderer draws them in a third pass after the opaque ones, without depth writes. Not sorted among
  themselves (the PC sorts every polygon by the key of head bits 13-15); viewer PC shots unchanged (1 px) where none
  are in view.

### 3. PC models look unshaded and always LOD - cause found, no LOD exists on the PC; shading as the PC (unlit)

- **LOD:** the PC has one detail level per part (CREAT1 / CREAT2.RBH, no LOD copies, formats.md §22) and its draw path
  has no distance choice: 0x43DD1C only culls by the frustum and draws the one mesh of the ID. So "LOD-looking" PC
  creatures are the PC's own meshes (e.g. 0x5E4: 191 polygons vs the arcade's 353). The arcade also sends one model
  per part (no LOD switch in geo_draw_model). Nothing to pick; with the arcade's models for creatures (the launcher's
  model switch per category, later) the detail is the arcade's.
- **Shading:** the PC draws its textures unlit (see 2), now so do we; the flat look against the arcade is the PC's
  own (the arcade's ground textures also carry baked shadows, the PC's do not).

## Per-model arcade light (render-leftovers, after R4's light records)

- R4 (faf3515) gives every draw-list entry the geometrizer's light vector when it was drawn (`DrawModel::light`,
  camera space: object_draw's light record by draw_param / the camera's light directions, ports 0x8000A0 + 0x804000).
  GameView turns it into world space with the view's inverse (normalized) and sets `DrawItem::light`; the renderer
  uses it as the Model 2 light (`uM2Light`, MAME geo_parse_* luma) for that item instead of the texture set's
  (0, -1, 0); the set's diffuse / ambient table is kept. World placements keep the set's light.
- Checked: `hotd --models arcade` frames 400 / 1100: creatures shaded by their own light (no black or blown-out
  models). **UNVERIFIED** against MAME screenshots of the same frames. PC items ignore it (the PC has no lighting).

## Blood flickering (render-blood, user report 2026-10-15 evening)

- **What the blood is:** the pools are the game-drawn models 0x9F2-0xA05 (`picha00a`..`picha19a`, all 20 drawn
  about once per frame at different places, every one at y = -25, the floor height; one was drawn twice at the same
  spot). The thin dotted streaks are rain, 0x7C4 `amee00a` (20 per frame), not blood. Shadows (`_DD`, a zero
  vertical row) also lie on the floor.
- **Cause (the one found):** the Model 2 has no depth buffer; it sorts polygons, so decals drawn onto the ground
  after the world always show. Our depth buffer made the pools coplanar with the ground and with each other: the
  winner per pixel depends on float noise of each model's matrix, so overlapping pools / pool edges can change as
  the camera moves.
- **Fix:** GameView marks game-drawn models that are flat (smallest bounds extent <= 1 % of the largest) or squashed
  (|det| of the matrix < 1e-4) as `DrawItem::decal`, and gives every game-drawn model a small clip-space depth bias
  (`depthBias` 1e-5, about 1 unit at 500 units) toward the camera. The renderer draws decals in their own pass after
  the opaque one, depth-tested, without depth writes (overlaps go by draw order, like the Model 2), then the PC's
  alpha-0x7F polygons.
- **Checked:** consecutive frames 1093-1100 (both model sources) and 2000-2007 (arcade, `--autofire`), with the fix
  switched on and off in the same build: the red-pixel changes between frames are the same (camera motion), i.e.
  **the flicker the user saw was not reproduced in these frames**; nothing got worse. The pools keep their shape over
  the five frames checked by eye.
- **Left:** reproduce the user's case (where / which blood: pools, hit spurts, the zombies' bloody damage textures
  `_dam` variants); MAME frames of the same moment. If it is the pool edges sparkling: the translucent coverage test
  (`a < 0.5` after mip blending) on grazing decals changes with sub-texel camera motion; worth comparing with MAME.

## Changed files

- src/assets/arcade_loader.cpp: mip levels 2-10 for ROM-rebuilt texture sets.
- src/game/runtime.h: `textureArea`, `texture_area_load` override (records TEXCHG's area; loads nothing).
- src/game/view.h / view.cpp: draw zones (arcade by placement index, PC by ID), texture set from the TEXCHG area,
  PC sky models.
- src/viewer/main.cpp: the ride applies the zone / area without Z, PC world culled by ID, sky set from the area.
- render-lighting: src/assets/assets.h (`kHalfAlpha`), src/assets/pc_loader.cpp (head bits 0x80 / 0x200),
  src/render/renderer.* (PC unlit, PC back-face culling, alpha 0x7F blending, clamped sky UVs).
- render-leftovers: `Mesh::blend` (assets.h), pc_loader's blended mesh, the renderer's third pass;
  `DrawItem::light` (assets.h), GameView's per-model light, the renderer's per-item `uM2Light`.
- render-blood: `DrawItem::decal` / `depthBias` (assets.h), GameView's decal test, the renderer's decal pass and
  `uDepthBias`.

## Left

- PC untextured palette colors (the run-time writer of mesh + 8), sorting among the alpha polygons (above).
- The exact mip filter (maincpu 0xCDC-0x1000) and the mip level choice (our per-pixel level is MAME's formula with
  our depth; not compared pixel for pixel with MAME at distance).
- The sky from the game's SKYKD / SKYTB state instead of the texture area.
- `--event E` starts: the TEXCHG area before E is not known (first set until the next TEXCHG); the event scripts'
  zone spans could give it.
- Not checked: Chapters 2-4 (the teaser is Chapter 1), MAME side-by-side screenshots of the same frames.

## PC look parity (overnight 2026-10-15/16, branch `claude/pc-look-parity`, build folder `build-render2`)

Judged against screenshots of the real PC game: the PC harness copy (`tools\pcharness\run_pc.ps1`) captured with
PrintWindow every 40-50 ticks (no focus change; scratch script in the session's scratchpad), `hotd --models pc` /
`--models arcade` with `--shot-at` at the matching moments. Evidence in `private\screenshots\pc_look\` (not committed).

### Fixed
- **Blood flicker on the zombies' faces / bodies (TODO 4): cause found, fixed.** Not the `_dam` variants (a per-frame
  trace of every drawn ID, `HOTD_TRACE_DRAWN=<file>`, shows no model switching and no two variants at one place) but
  **decal layers inside one model**: the blood / face polygons lie in the plane of the skin polygons of the same mesh
  (598 arcade part models, 77,598 triangles over all arcade meshes; 496 PC triangles). The Model 2 sorts polygons, our
  depth buffer made them z-fight as the zombie moved. `markCoplanarLayers` (render/renderer.h) gives such a triangle
  layer 1 + the layer under it (flags bits 13-14), the vertex shader pulls each layer 1e-5 clip z toward the camera.
  Frames 930-937, arcade models: `blood_face_f930-937_before.png` (striped, changing blood) / `_after.png` (stable).
  `HOTD_NO_LAYERS=1` switches it off for comparisons.
- **The mountains (both sources):** Chapter 1's sky is 0x608 (`skyuv02a`, the snowy mountain band) inside 0xA29 (the
  clouds); drawn in list order without depth the clouds covered the mountains (PC: `real_pc_t614_mountains.png`). The
  sky pass now writes depth and the depth is cleared after it.
- **Point sampling (PC):** THOTD.EXE sets TEXTUREMAG / MIN to LINEAR only with the driver dialog's bilinear option
  (`d3d_render_states_init` 0x583960, option 0x12E8C4C), else NEAREST, no mip levels; the harness game is
  point-sampled (blocky texels in every capture). PC sets are NEAREST now; `RenderOptions::pcBilinear` for the option.
  (Not settled: an empty devs.cfg defaults the option to 1 and opens the driver dialog; how the harness passes the
  dialog was not looked at. The captures are the evidence.)
  **Settled (pcd3d, GitHub #79, 2026-11-12): THOTD.EXE has two drivers** and the point-sampled look is the other one.
  See "The PC's two renderers" below; Options > Video > **PC Renderer** (Software / Direct3D) picks the look.

### The PC's two renderers (pcd3d, GitHub #79, 2026-11-12; read in THOTD.EXE.orig, disassembly + private\decomp\pc)
- **The choice:** the window set-up 0x563440 looks for the words `ddraw` (0x5C6E80) and `d3d` (0x5C6E88) in the
  command line: `d3d` without `ddraw` installs the Direct3D driver (0x573CA0: driver pointer 0xC649E0 = table
  0xC66EF8, **type 3** in its first word), anything else the DirectDraw **software** driver (0x565E70 / 0x5677F0:
  table 0xC652A4, **type 1**). So a plain `THOTD.EXE` is the software renderer. The game code asks the type only for
  the screen mode (0x418434 / 0x4184E4 / 0x418550, driver +0x68): type 1 gets **8 bits per pixel** (640 x 480, or
  320 x 240 with option bit 0x20 of 0x5CE534; its mode list 0x5676D0 takes 8-bit modes up to 1024 x 768), any other
  type 640 x 480 x **16**.
- **Both tables have the same slots** (+0x74 queue a polygon record, +0x78 / +0x7C untextured triangle / quad, +0x80 /
  +0x84 textured, +0xBC / +0xC4 the 2D sprites):

  | | Software (type 1) | Direct3D (type 3) |
  |---|---|---|
  | untextured triangle | 0x57F230: the mesh colour as RGB555 \| 0x8000 through the 64 KB table at [0xC64CA4] (`STAGEn.TBL`) to a **palette index** (record +7) | 0x577CA0: D3DTLVERTEX diffuse = the colour & 0xFFFFFF, 0 -> 0x080808, alpha 0xFF (0x7F with head bit 0x80) |
  | textured triangle | 0x57EEB0: flags 0x92, + 0x800 with head bit 0x40 or beyond the distance [0xC64CD0], + 0x400 with head bit 0x100 (the rasteriser's meaning of 0x400 / 0x800 not read); head bit 0x80 not looked at | 0x577870: flags 0x92, or 0x292 at alpha 0x7F with head bit 0x80; diffuse white |
  | back faces | head bit 0x200 clear: 0x586426 (the 2D cross product's sign) | the same test in the dispatcher (0x578F60), D3D CULLMODE none |
  | depth | always the sort key (painter's order) | the z-buffer (key 0 / 1.0) unless the dialog's sort option 0x12E8C54 |
  | texels | 8-bit, the stage palette (`STAGEn.PAL`), point-sampled | filtered, see below |
  | 2D sprites | 0x57DBD0 / 0x57DF50: flags 0xA2 (+ 0x400 unless flag bit 2) | 0x576E20 / 0x577110: D3D quads, flags **0x10A4** (0x12A4 and alpha 0x7F with flag bit 1), texture coordinates + 0.25 texel |

- **Direct3D's render states** (0x583960..0x583AC0, `SetRenderState` = device +0x5C): ZENABLE (7) 1 with a z-buffer;
  ZWRITEENABLE (14) 0 here; SRCBLEND (19) SRCALPHA, DESTBLEND (20) INVSRCALPHA, ALPHABLENDENABLE (27) 1;
  TEXTUREMAPBLEND (21) MODULATEALPHA; COLORKEYENABLE (41) 1; FILLMODE (8) solid; SHADEMODE (9) Gouraud; CULLMODE (22)
  none; DITHERENABLE (26) 1; SPECULARENABLE (29) 0 (the earlier note's "fog off" was this state; fog is never set);
  TEXTUREPERSPECTIVE (4) 1; TEXTUREADDRESS (3) wrap; **TEXTUREMAG (17) / TEXTUREMIN (18) = 2 LINEAR with the dialog's
  bilinear option 0x12E8C4C, else 1 NEAREST** (0x583A67); no mip levels.
- **The dialog's defaults** (0x575284, `D3DDRIVERSELECT`, when devs.cfg does not parse: the release's is 2 bytes):
  texture depth 0x12E8C58 = 8, **bilinear 0x12E8C4C = 1**, sort 0x12E8C54 = 0, half-size textures 0x12E8C5C = 0.
- **The 2D stays sharp in Direct3D:** the batch draw 0x582ED0 sets TEXTUREMAG / MIN to NEAREST around every polygon
  record whose flags have 0x1000 (0x583133..0x5831B4) and back to LINEAR after it; only the sprite routines queue
  0x1000 (0x576E69.., 0x577177..: a scan of the code section for an immediate push with bit 0x1000 before a +0x74 call
  finds only those six `push 0x10A4 / 0x12A4`).
- **Texture formats in Direct3D:** 0x584390 / 0x5850B0 pick the device's texture format: 8-bit palettised when the
  dialog's depth is 8 and the device offers one (0x585040), else the nearest 16-bit one (0x5848A0 converts the
  ARGB1555 pages by the format's masks); with 0x12E8C5C (set by the dialog, or when video memory is short) 0x584550 halves a texture's size when its header
  word is 500 or more. With 8-bit
  textures the driver's palette load (+0x34 = 0x585530) reads `STAGEn.PAL` and the same `.TBL` as the software driver.
- **What ReWrithe draws.** Software (the default, the look so far): the ARGB1555 pages in their own colours,
  point-sampled, z-buffer. That is the software driver's sampling without its 8-bit palette (the original's software
  picture has 256 colours; the issue's captures show banded skies and a grey car where Direct3D's is blue). Direct3D: `RenderOptions::pcBilinear` = the
  Direct3D driver with 16-bit textures and bilinear on: LINEAR on every 3D polygon's texture (the sky too), no mip
  levels, the transparent texels cut at half the filtered cover (the dark fringe of the black key texels stays, as a
  colour-keyed filtered texture has it), the HUD / 2D point-sampled as before. `--pc-renderer software|d3d`,
  launcher.ini `pcrenderer=`. Not done: DITHERENABLE (a 16-bit target's pattern; our target is 8 bits per channel),
  the 8-bit palettised texture path (either renderer), the software driver's painter's sort and its 0x400 / 0x800
  polygon flags (not read).
- **PC untextured polygons:** colour = the mesh's own colour table at header + 8 (an offset in BODY 0: the table lies
  just before the header; read with ReadProcessMemory from the running harness game, `d3d_tri_untextured` 0x577CA0),
  & 0xFFFFFF, 0 -> 0x080808. Were grey (the scientist's eyebrows / eyes now dark as in the PC).
- **PC world objects lost:** WORLDn.RBH entries are variable (id, 0xFFFF, mesh offsets, 0; 0x419CF4); 0xCFD has none,
  so the fixed 12-byte reader lost 0xCED, 0x111D, 0x111E, 0x91F (the wall beside the gate: `hotd_pc_f700` vs the
  harness t897) and 0xAC3. Objects with several meshes get all of them.
- **PC textures with the PC's blood colour:** the game draws TEXTURE.VMC (BIGGER.VMC with the blood colour baked in);
  the user's install has yellow blood (THOTD.CFG +0x28 = 4), so the zombies' half faces were red instead of yellow.
- **No arcade models in PC mode (user rule "Assets"):** no fallback for world placements, skies or game-drawn IDs; no
  generated skins (the PC body_draw 0x477B00 calls no skin_draw); the arcade texture-set index no longer touches PC
  items. A gap is logged once (`PC model gap: ...`) and not drawn, as THOTD.EXE's geo_draw_model (0x43D7F8) skips an
  empty `DAT_00d53080[id]`.
- **Model IDs (TODO 4b a):** the PC draws by ID like the arcade (`DAT_00d53080[id]`, filled by ID); the 96 skeletons
  of the PC (0x5EF8E0) and the arcade (0xC7240) have the same node models; `HOTD_CHECK_PC_IDS=1` compares bounds of
  the 4,067 shared IDs: 3,245 equal, the others the PC's simplified effect frames / world objects in world space, no
  shifted IDs. **One real difference: the opening car.** The PC's car routines (0x496AE0..0x496E98) draw their own
  models: body 0x1D36 (arcade 0x94F moving / 0x951 parked), door 0x1D37 at (-9.13, 0, -9.76) on the body (arcade
  0x950), no wheels / tyre frames / shadow (0x952, 0x953, 0xA49..0xA6E). GameView substitutes them in PC mode.

### PC gaps (logged, not drawn; the PC draws nothing there either)
- World placements without polygons in WORLD1.RBH: 0xCDE `r1_kuroita01a`, 0xCE8 `room5a_CT00a`, 0xCFD `room_nk1a`.
- Game-drawn: 0x365 / 0x366 `kensakil00a` / `kensakir00a`, 0x461 / 0x462 `neil_sakil` / `neil_sakir` (in CREAT1 with
  no polygons; THOTD.EXE has headers for them, empty).
- Rogan's coat frames 0xB5.. / 0x119.. (the PC hook 0x51D6F0 draws them like the arcade; DAT_00d53080 has none in the
  running PC game): not drawn in either. The PC Rogan really has the short coat and the grey patch below it (harness
  t958 shows the same as `hotd --models pc`).

### PC renderer library read (names.tsv)
`pc_mesh_polygons_draw` 0x578BB0, `pc_poly_backface` 0x578F60, `d3d_tri_untextured` 0x577CA0, `d3d_tri_textured`
0x577870, `d3d_render_states_init` 0x583960, `pc_draw_driver_table` 0xC66EF8 (file data), `pc_poly_sort_key_fns`
0xC67644, `d3d_opt_bilinear` 0x12E8C4C, `d3d_opt_sort` 0x12E8C54. Render states: z-buffer (driver choice), SRCALPHA /
INVSRCALPHA, MODULATEALPHA, colour key on, Gouraud, **no culling in D3D** (the back-face test is the CPU's 0x578F60),
dither on, **fog off**, perspective correction, wrap. Sort keys (head bits 13-15) are used only with d3d_opt_sort
(default 0: keys 0 / 1.0, the z-buffer decides); polygons with head bit 0x80 go to a separate list (flags 0x252) at
alpha 0x7F. Far clip: polygons whose key reaches the viewport's far value (+0x14) are dropped; objects by the bounding
sphere against +0x44 (values not read).

- **Projection / far distance (read from the running PC game: draw context [0xD5A5C0], its viewport +0x18):** x scale
  640 and y scale -620 for a 640 x 480 screen (+-320 x +-240; cut-scenes +-320 x +-190 from y 50), i.e. the arcade's
  field of view (320 / 640 = 248 / 496, 240 / 620 = 192 / 496) with slightly wide pixels: ours (focal 496 on the
  arcade's 496 x 384) matches. Far distance 4000 (context +0x14 for polygons, viewport +0x44 for objects): PC pixels
  beyond 4000 are discarded now (per pixel; the PC drops whole polygons / objects). Ours was 20000 for everything.

### Left
- Colour: sky equal (avg 154/152/143 vs 150/151/143); mountains and ground about 5-15 % brighter / redder in ours
  (the PC renders to a 16-bit surface with dither; not modelled).
- Not compared yet: FOV / aspect (the PC is 640 x 480, 4:3), far clip values, alpha polygon order among themselves.
- The blood decal pools / hit spurts not re-checked against the PC.

### Later in the night
- **Draw fade (t1's DrawModel::fade, draw_fade_begin 0..0xFF):** DrawItem::fade; arcade: colour scale (the Model 2
  scales the geometrizer's colour table, no blending); PC: alpha, those items blended after the opaque pass without
  depth writes. GameView reads the field only when the runtime has it. UNVERIFIED (teaser) against the PC driver.
- PC harness long run (12,000 ticks, `tools\pcharness\run_pc_shots.ps1`): the corridor of event 6, the balcony, the
  stairs, the Chariot room look like `hotd --models pc` (brightness, point sampling, yellow blood). Props the PC files
  have no polygons for (logged gaps, the PC draws nothing either; its prop table 0x5D468C has the arcade's IDs):
  isu_02a, tabul_02a, kabin_01a, item_kira01a-49a, tuubo24a-29a (break frames), kyochuu00a-20a, r1mado_02z,
  hndl_buki_CT000a, tokei / tokei_furiko / tokei_hari01a / 02a, sofi_d.
- PrintWindow sometimes captures white frames (the first 6,000 ticks of that run); a second run fixes it.
- `--event E` starts show only the sky until the first BGTB (zone 0) and the sky indoors (TEXCHG area unknown): not
  render bugs; judge play-throughs from the chapter start.
- Not done: the sky from SKYKD / SKYTB, polygon order inside the PC's alpha list, the 16-bit dithered output colours.

### Round 2 (after the merge of t1 / s1 / hud)
- **Sky from the game state:** s1's sky_task (SKYTB / SKYKD, the dome lifted and turned by its row of 0xAC1F0) now
  draws the sky alone; the texture area's sky list (`arcade_texsets.txt` `sky` lines, offset -9) is no longer drawn
  (both together drew a second, unlifted copy: the mountains looked about twice as tall as in MAME). `HOTD_SKY_ONLY`
  still isolates one sky model.
- **Colour rows (arcade, the opening's tint):** color_row_set's row is the colour shadow row; color_curve_tables_set
  (0x1330) fills shadow row i from the area list's entry i, i.e. translation base 1 + 2i. GameView passed the row as
  the base, so the event scripts' swaps recoloured the wrong rows: blue hedges, a blue roof edge, green moss in the
  opening. Fixed in GameView (the texset file's `rows` lines already use base values). Frame 300 now matches MAME
  snapshot 0012 (warm sky, grey wall, blue-grey floor). `HOTD_NO_COLOR_ROWS=1` switches the swaps off.
- **Left, written down:** the PC ground / mountains 5-15 % brighter than the harness captures: the sky matches within
  4 levels, so it is not a global gain; the compared ground areas were not the same view (one batch, no lead). The
  order inside the PC's alpha list (few polygons in Chapter 1: effects 0x6-0x23, world 0x928 / 0xCF5 / 0x111C-0x111E).
  The arcade zombie's face in frame 940 is greener than in MAME 0020 (not traced: light record or another type).

## fix-world (overnight 2026-10-16, branch `claude/fix-world`, build folder `build-fixworld`)

Checked with `hotd --checkpoints` (both sources; screenshots in the session scratchpad, MAME snaps 0011 / 0146 /
0147 / 0154). Frame numbers below are the default-route autofire run's.
1. **Grey / sepia look (both sources): done.** RenderOptions::grey / sepia from Runtime::greyPalette (fix-logic); the
   shader maps the final colour's grey level L = (r + g + b) / 3 to sepia[L / 4] (arcade: after the xlat and the
   output curve, i.e. in place of the gamma tables). Frames ~182-242 now sepia as MAME t00240 (snap 0011);
   `HOTD_NO_GREY=1` off. PC: same formula, UNVERIFIED (the PC install has GREY.PAL / GREY.TBL, not read).
   **PC c2 grey sky / blocky clouds: not a bug** - the real PC captures (`pc_look\real_pc_t454_scientist.png`,
   `real_pc_t614_mountains.png`) have the same grey-green sky, point-sampled; the arcade's warm tint is colour rows,
   which the PC does not have.
2. **PC yellow severed parts: the blood setting.** PC mode drew the install's TEXTURE.VMC (THOTD.CFG: yellow) whatever
   the game's setting; the launcher's red now draws BIGGER.VMC (THOTD.EXE 0x42713C copies it when B_<colour>.VMC is
   missing: BIGGER is the red set). c4 PC now red like the arcade. Green / blue / purple: still the install's cache
   (B_*.VMC PACK compression not cracked). `PcConfig::blood`, GameView::init(..., blood).
3. **PC Sophie on white: the room's floor (0x111D room_6bb, the only world model of zone 48), PC's own texture.**
   Not changed: the floor texels are near-white / grey in BIGGER.VMC, point-sampled at close range; the render worker's
   harness run found the PC Chariot room like `hotd --models pc`. Not re-captured at this moment (harness not mine).
4. **Arcade Chariot room: floor colour right, walls too dark - not fixed.** MAME 0147 / 0154: light-blue tiled floor
   (ours the same colour; c5a is just much closer). The walls (orange wallpaper) are much darker than MAME's. Checked:
   the world light is right (MAME work RAM t02940-t03100: 0x5430C0 in camera space = (0, -1, 0) in the world; ours
   the same), the set (area 4) has its light record (diffuse/ambient table: index 12 159/96, 13 152/62, others
   ambient 160-255). Lead: which parameter index the wall polygons use, or MAME's luma for polygons perpendicular to
   the light; `HOTD_LIST_DRAWN_AT=<frame>` prints the set's table and every drawn item.
5. **world_draw_task in the view: done (UNVERIFIED).** Per arcade placement (PC objects by ID): the model-list entry
   (0x5430D0 counter, list at +0x14) and the fade (0x543210 when +0x10 != 0xFF -> DrawItem::fade); the zone light
   0x5430C0 -> world space for world items (DrawItem::light). Shadows: shadow_setup's world projection (its copy at
   0x51FDF0, DrawModel::shadowM, along the shadow light onto the floor) instead of the straight-down squash
   (`HOTD_SHADOW_SQUASH=1` for the old one). Chapter 1 checkpoints look as before (no list / fade placement seen).
6. **"RESCUED x 0" is right for the run.** New log lines: the autofire shoots the hostages / researchers (frames
   1621, 1636, 4246, 6511, 7802, 7817, 10891) and the default route reaches only events 0, 2, 3, 4, 5, 6, 19; no
   rescue happens, the counter is not the bug.
7. **c3 checkpoint:** `c3_courtyard event 3 300` (the default route skips event 1); reached in both sources (frame 2663).

Other workers' files touched (minimal): src/game/t1_bounds.cpp (blood_mark_effect log), src/game/runtime.cpp / .h
(rescue log, DrawModel::shadowM), src/game/creature.cpp (shadowM copy), src/main.cpp (blood to GameView).

## fix-world-2 (overnight 2026-10-16, branch `claude/fix-world-2`, build folder `build-fixworld2`)

1. **Arcade Chariot room walls: not a bug.** Pixel values equal MAME: wall (29,16,0) ours vs (24,13,0) / (29,16,0) in
   snap 0154 (the snaps look brighter in some image viewers). Every input of room_6bb (0x111D) compared with the MAME
   dumps t03000 / t03100 (snap k = dump tags[k]; 0147-0154 = t02960-t03100): texture RAM 0 % differing, luma tables,
   polygon palette, colour rows 16-19 equal; the walls use light parameter 0 (diffuse 0, ambient 255). Added: MAME's
   specular term (parameters 12 / 13, UNVERIFIED: no Chapter 1 model uses them) and debug switches
   (HOTD_LIST_DRAWN_AT now prints parameter histograms; HOTD_LIST_COLOURS, HOTD_DUMP_MODEL=<id>,
   HOTD_DUMP_TEXSETS=<prefix>, HOTD_NO_MIP, HOTD_NO_LIGHT). Rows 7 / 9 / 11 / 15 / 29 (green / blue 19 slightly) differ
   from MAME at t03000: run-time colour rows (s1's color_row_set), not used by the room.
2. **Low-priority 2D cells under the 3D: done.** MAME model2 screen_update order: low cells, polygons, priority cells.
   Hud::drawUnder draws the low cells first, the renderer then keeps the colour (RenderOptions::clearColor = false),
   so the 3D (sky included) covers them where it draws. Chapter clear: the area picture 0xA6 inside the metal frame
   (no 3D there). PC: pcCells has no priority (unchanged).
3. **Game-over route map: 2D part ported** (game_over_route_task 0x5CF50: map sprites 0x243 / 0x244, the route trail
   from scene_history, scroll, reveal records, the 180-frame timer after the route). The view draws no 3D from mode 5
   sub-mode 2 on. Not ported: the walkers (players' characters on the map, 3D) and the camera task. Checked with
   `--lives-kept 0 --lives-out 200` (map frames ~1300-1600, GAME OVER sheet 1650). UNVERIFIED (no MAME capture).
4. **PC blood colours: done.** B_<colour>.VMC PACK chunks unpacked (pc_loader.cpp unpackVmcChunk; format in the code
   comment); B_YELLO.VMC unpacks byte for byte to the install's TEXTURE.VMC. The launcher's blood 0-3 draws B_RED /
   B_GREEN / B_BLUE / B_PURPL.VMC (BIGGER.VMC if missing).
5. **PC mountains / ground: not fixed (one batch, no lead).** Harness captures (`private\screenshots\pc_look\fw2`,
   ticks 50-952; t764 = our frame ~1060, the subtitle) vs `hotd --models pc` frame 1060: sky equal (p50 164,153,139 vs
   164,155,148); ground dark levels about equal; the snow highlights differ in hue, not by a gain: real p75 / p90
   (210,201,185) / (219,208,188), ours (222,198,198) / (239,206,206) - ours has blue = green there (texels like
   29,25,25), the real is warm (b < g < r). Not a global brightness: other texels for the mountain band in the PC
   (another page / file) or an overlay in front of it (the PC's alpha list: world 0x928 / 0xCF5) are the leads.
   dgVoodoo colour settings are neutral (Brightness / Color / Contrast 100); the PC's diffuse is white (0x577870).

Other workers' files touched (minimal): src/game/hud.* (drawUnder), src/assets/sprites_arcade.* (compose pass),
src/main.cpp (drawUnder before view->draw), src/game/flow.cpp / runtime.h (route map), src/assets/arcade_loader.*
(specular words, dump switch).

## palanim (overnight 2026-10-16, branch `claude/palanim`, build folder `build-palanim`)

The arcade game's own palette animation now runs in the game (model source arcade). UNVERIFIED (teaser) as game code.

- `src/game/palette_anim.cpp`: `Runtime::palette_animation` = maincpu 0x25310 (called from `game_frame` where the
  original's `geo_frame_begin` calls it): writes work RAM 0x52066C..0x5206E0 from the frame counter, the chapter byte and
  the flags 0x51E982 / 0x51E9A1 / 0x51E9B2 / 0x51E9B3 (sets 0x51E9B3 like the original). `Runtime::palette_anim_copy` =
  0x24E90 (at the start of `Runtime::frame`, the vblank): work RAM 0x520664 + 2n -> entry 1021 - n, 0x5206E0 -> 959,
  211 = 0xEBDF, 689 = 0x9854 in area 9, into `Runtime::polyPaletteAnim` (the engine has no palette RAM). So the picture
  shows the previous frame's values, as in MAME. 0x5206AC (entry 985, the item sparkle written by props.cpp) goes the
  same way.
- `GameView::draw`: sends those entries (plus the blood entry 1023) through `Renderer::setPaletteAnimation` when they
  change. `HOTD_NO_PALANIM=1` = the loaded values (the old look).
- What animates: entries 984-1021 and 959 (the 90-colour cycle, the 96 / 64-frame pulses, the 14 light strips pulsing
  out of phase, entry 984 the blood pulse 0x801F -> 0x800C).
- Evidence: `HOTD_PALANIM_LOG=<file>` (one line per frame) + `tools\palanim_check.py <log> private\mame_dumps\fullgame_us`:
  the animation depends only on the frame counter (period 43200) and chapter / flags, so each Chapter 1 MAME dump is
  compared with the game's line for counter - 1: 8000-frame headless run, **64 dumps matched, 64 exact** (1920 entries);
  the same with counter - 2: 0 exact (the check is not vacuous). Checkpoint screenshots (arcade models) with / without:
  only c5_chariot_b differs (80 pixels, the chariot's blood, entry 984); the other checkpoint views draw none of the
  animated entries.
- PC: no equivalent. The PC's `geo_frame_begin` pair (0x569510) only sets matrices, there is no `palette_animation` pair,
  and `palette_animation_step` (0x455E80) has no callers; the PC models keep their own colours. Nothing done in PC mode.

Left: the 0x25310 other branch (game mode 9 scene 3: `palette_animation_step` 0x24020 on palette 0xD, attract side, not
run by this runtime); 0x24D70's work RAM copy of the effect record at the area palette load (entries 1021 / 1020 / 1018
keep their loaded values, same colours); entry 985 never set in the 8000-frame run (no item sparkle reached);
chapter-3 / stage-6 flag cases unchecked (teaser is Chapter 1). Other workers' files touched: src/game/runtime.* (two
calls + declarations), src/game/view.* (the palette block), CMakeLists.txt (one source).

## User play-test 2026-10-16 (branch `claude/visual`, build folder `build-visual`)

1. **Floating window on the way to the Chariot room (arcade models): FIXED.** The corridor at the start of event 19
   (and route 4 / 7's event 18 corridor) drew a 200-unit lattice window standing across the corridor. It is world
   placement **0x37** of Chapter 1 (`room5a_CT00a` 0xCE8 with a 32-frame model list room5a_CT00..CT31: the corridor's
   right-wall windows). `world_draw_task` (0x4B300) sends, for placements 0x37 / 0x47 only, copro 0x15 (rotate Y)
   0x4000 after the translation (the decompilation's `_DAT_00884000 = 0x40000a801515`; docs/decompilation.md had read
   it as "a geometry-engine setting"); Chapter 2's 0x1B gets copro 0x16 0x4000. The arcade loader now sets
   `Placement::rot` for them (Tgp::rot's row convention: (x, y, z) -> (-z, y, x)): the windows stand on the right wall
   (x ~342). Evidence: `--default-route` frames 12300-12500 (event 19 + 10..210) before (the window across the
   corridor, filling the view at 12380-12440) / after (windows on the right wall) vs MAME fullgame_us snaps 0142 /
   0143 (event 19 +57 / +172): the same right-wall windows. PC models: unaffected (0xCE8 has no polygons in
   WORLD1.RBH; the PC's room_5b has the windows built in). `HOTD_LIST_DRAWN_AT` now also prints list placements'
   model-list position. The viewer gets the fix through the loader.
2. **Sophie's green eye popping up (arcade models): FIXED.** Not a texture switch: green eyes are right (MAME snap
   0148, the close-up at event 19 +~690, shows green irises). Frames 12905-12943 (`--default-route`, event 19 close-up)
   showed a solid green patch over her left eye, coming and going as the head turned, drawn over the hair strands. Her
   head (sofatama02a 0x60F, and every other head: 205 arcade models, 820 triangles) has per eye an **untextured green
   quad** (palette 175, colour 6,26,0) lying 0.013 units **in front of** the translucent eye texture (shade 15 =
   transparent iris) and tilted ~11 degrees to it. The Model 2 sorts polygons and draws the eye texture over the quad,
   so the green only shows through the iris hole; our depth buffer let the quad win (the coplanar-layer test needs
   normals within ~6 degrees). Fix (render/renderer.cpp `flattenUnderTranslucent`, run by markCoplanarLayers): an
   untextured triangle within 0.05 of a translucent textured triangle of the same mesh (normals within ~25 degrees,
   centroid inside it) is projected onto that plane and moved to the mesh's front, so the texture gets the layer above.
   Evidence: frames 12905-12943 before (green patch at 12905-12919, 12925-12929, 12935-12943) / after (textured
   irises, no patch; blinks unchanged). `HOTD_NO_FLATTEN=1` gives the old look, `HOTD_LOG_FLATTEN=1` lists the
   models. PC models: no such quads (PC eyes are in the texture), unchanged. Also touched: the results screen's
   researcher group 0x65E and a few room2_* world models (2 triangles each, behind translucent textures). Possibly the
   cause of the "arcade zombie's face greener than MAME 0020" note above (not re-checked).

3. **Lying researchers under the ground (arcade models, opening): FIXED (feet4).** Not a logic / pose issue: with
   `HOTD_FEET_DEBUG=1` the lying researcher (frame 720, models 0x5E4 / 0x5E6, no body grouping) has his lowest vertex
   only 0.2-0.7 under the floor -25 and his top ~5 above it, i.e. he lies on the game's floor height as in MAME; the
   courtyard's ground / kerb polygons sit up to 2 units above that floor, so it is depth only (the Model 2's polygon
   sort draws the ground first). feet3's lying-body pull (uFloorPull 3) was capped at 2.5 units along the view ray,
   only ~0.5 unit of height at the opening's low camera. Fix (renderer.cpp vertex shader): a lying body (< 6 units
   tall) is pulled whole by the full 2.2-unit dip (cap 14), the same weight for every vertex, so the body's own
   parts keep their order (photo / hand stay behind the face). Checked frames 720 (standing feet unchanged), 1130,
   1170 (photo researcher's face clean, both lying researchers whole).

## Draw Depth: the Model 2's polygon sort (clipfix, 2026-10-26)
- The Model 2 has no depth buffer (MAME model2_v.cpp model2_3d_process_polygon / model2_3d_frame_end): every polygon
  gets one 16-bit key (float_to_zval of the min or max camera z of its corners, attr bits 10-11; mode 0 = the
  previous polygon's z, hidden link-0 polygons included), the screen is filled near to far and a pixel once.
- Renderer: Draw Depth "Arcade Sort" (render/renderer.h DrawDepth) gives each arcade polygon its key as its depth
  (vertex shader; the key polygon's corners in a per-stage RGBA32F buffer texture, Renderer::sortKeysOf; Mesh::sortKeys
  for mode 0), key x 256 + 2 x item + 1: ties go to the earlier draw item, inside one item to the later polygon.
  Matched MAME at the attract's two-axe zombie, Chapter 1 pond, Chapter 2 bedroom (corner and window), Chapter 3
  grey wall, the ending car. UNVERIFIED: the z-sort register (command 0x08, z_adjust) taken as 1.0.
- Debugging: HOTD_IDSHOT=1 with HOTD_IDSHOT_TRI=t (triangle index + sort mode) or =z (key z x 64); HOTD_SORT_EXACT=1
  (keys not rounded).

### depthcmp (2026-10-27): 21 places against MAME and the real 1998 PC
- Sheets: private\changelog\img\depthcmp\index.html (MAME | 1998 PC | Depth Buffer | Arcade Sort | exact ties | a
  QOL test | the user's screenshot). The 1998 PC column: THOTD.EXE under the PC harness (docs/pc-harness.md), one
  auto-aim run through the whole game (77,645 ticks) and one attract run, captures every 20-30 ticks.
- Fix 1: every pixel of a polygon now gets its key's depth exactly (gl_FragDepth). Before, the depth interpolated
  through z / w wobbled by a step across a polygon, so polygons with the same key were decided by rounding, not by
  draw order: Chapter 2's Hangedman intro showed a checker polygon of the building over its wall (MAME does not).
- Fix 2: the z-sort register is 4.0, not 1.0 (geometry command 0x04000808 with 4.0 in every display list of the
  fullgame_bufram dumps), so keys count from exponent 2. No visible change at the 21 places.
- Arcade Sort matches MAME at all of them except Curien's waistcoat in the attract (a dark panel in every mode: not
  depth; rpolish: his belly skin was the chapter's stage's model in another area's texture set, fixed). Places the user saw that are in the original too: the building's shadow slab in the courtyard (MAME snap
  23), the black wall edge and blood chunks in the Chapter 2 hall (snaps 202 / 240), the lava over the walkway edge
  (snap 491).
- A third "Clean" mode (no pops at all) was tried and not added: per-pixel depth near the key (pond stones sink, the
  car pokes through the pillar), per-pixel for big polygons only (the lava pop stays, the Hangedman checker comes
  back), per-pixel inside key ties (changes the lava hall and the bedroom corner away from MAME). None was clean everywhere.

### earlyz (2026-10-27): the same order without gl_FragDepth
- gl_FragDepth on every pixel turned the GPU's early depth test off. Now the key is the vertex depth: every corner
  clip z = c x w (c from the 16-bit key), the arcade draws in depth range 0 .. 2^-8 at (key + 0.25) / 2^24, so the
  float error stays under 0.02 of a depth step and every pixel stores the same value (ties exact). The item's place
  left the depth: the depth-writing passes draw the arcade items last-first (past 127 first, in list order), so the
  earlier model still wins a tie and inside a model the later polygon; PC / pack models keep their order.
- Checked: 0 pixels differ from the gl_FragDepth path in 6,924 frames (every 30th frame of Chapters 1-4 with the
  ending and the attract, both models, 1280 x 720). `HOTD_SORT_FRAGDEPTH=1` keeps the old path (comparisons);
  numbers in docs/performance.md "Early depth test for Arcade Sort".

### prepass (2026-10-27): depth-only pre-pass
- The opaque pass drawn twice: depth only (the shader only runs its discards), then the colours with EQUAL depth and
  no depth writes, so a hidden pixel is no longer shaded (the full shader ~1 screen instead of ~2). Same winner as
  before; sky, decals and blended passes unchanged; both Draw Depth modes.
- Checked: 0 pixels differ (prepass off vs on) in 6,924 frames with Arcade Sort and 6,924 with Depth Buffer (every
  30th frame of Chapters 1-4 with the ending and the attract, both models, 1280 x 720).
- Auto: on with integrated / software graphics only (RTX 5080: about even, up to 7 % slower in Chapters 1 / 4).
  `HOTD_PREPASS=0 / 1` decides. Numbers in docs/performance.md "Depth-only pre-pass".

### depthfix (2026-10-27): Depth Buffer with the arcade's keys, Free Cam, verdict
- Depth Buffer is now a hybrid: each arcade polygon's depth is drawn toward its sort key (the key as the arcade rounds
  it, with its tie order: the earlier model first, inside a model the later polygon) by at most 20 % of its own
  distance, per pixel (renderer.cpp `KEY_BAND_FRAG`, its own program because of `gl_FragDepth`; Arcade Sort keeps the
  early depth test; no pre-pass in Depth Buffer). Surfaces that close resolve as the Model 2's sort does, things
  farther apart and crossings stay per pixel. The floor pulls are dropped for keyed polygons (a door's foot pulled 14
  units nearer hid Chapter 2's bullet holes in the old Depth Buffer too); decals / shadows keep their small pull.
  `kKeyBand` (renderer.h); `HOTD_KEY_BAND=x` overrides (0 = the old Depth Buffer, comparisons).
- Free Cam (the viewer, any Draw Depth) uses the same with a 5 % band (`kKeyBandFreeCam`), so close-up crossings stay
  per pixel; and the NDC depth biases now scale with the near plane (Free Cam's 0.1 made them 10x larger in units).
  Checked (1:2 Walk and shoot, camera at the door): the shot sparks were cut in pieces by the door, the knocker hidden;
  now whole, as the game camera shows them.
- 4-19 widescreen (4P Goodbye Curien, left floor): not reproduced as a cut. No 4:3 culling in the draw path (the only
  frustum test, coprocessor 0x7B, is game logic except Chapter 1's canal water); the left edge at 1280 x 720 shows the
  roof's raised step. Needs the user's screenshot.
- Scan, every 60th frame of Chapters 1-4 (with the ending) and the attract, 320 x 240: frames where Depth Buffer
  differs visibly from Arcade Sort (> 400 pixels): arcade models 487 / 2,482 before, 192 after (5 % band: 291);
  PC models 0 / 2,482 both (the PC models never use the keys). Arcade Sort = the arcade's order by construction.
- Places (sheets: private\changelog\img\depthfix\index.html, MAME | 1998 PC | Depth Buffer | Arcade Sort; our runs:
  `--strict --default-route --autofire --lives-kept 1`, attract `--title`; ch1_gate / ch2_hall / ch4_bolt no longer
  reached at their old frames, ch2_corner 5400, ch2_ivy 6270, ch2_hanged 15670, ch4_bolt 6150, end_car 34130):

| Place | Depth Buffer before | Depth Buffer after | Arcade Sort |
|---|---|---|---|
| att_desk | light through the wall | = MAME | = MAME |
| att_curien | waistcoat panel (not depth) | same | same |
| att_cyril | waistband over the shirt | = MAME | = MAME |
| ch1_kneel | bodies cut into the ground | = MAME | = MAME |
| ch1_pond | stones / shadows sunk | = MAME | = MAME |
| ch1_gate | (other moment) | = Arcade Sort | (other moment) |
| ch1_shadow | slab (original) | = MAME | = MAME |
| ch2_doors | bullet holes hidden | = MAME | = MAME |
| ch2_corner | ok | = MAME | = MAME |
| ch2_ivy | ivy hidden | = MAME | = MAME |
| ch2_hall | (other moment) | = Arcade Sort | (other moment) |
| ch2_hanged | ok | = MAME | = MAME |
| ch3_wall | door through the wall | **still wrong** | = MAME |
| ch3_pit | tiny blood | = Arcade Sort | = MAME |
| ch3_lava | (no pop) | the pop, as MAME | the pop, as MAME |
| ch3_hermit | ok | = MAME | = MAME |
| ch4_bolt | ok | = Arcade Sort | ok |
| ch4_magician | ok | = Arcade Sort | = MAME |
| end_credits | wall strip | **still wrong** | ok |
| end_car | car over the pillar | = MAME | = MAME |
| end_walk | no shadow | **still wrong** | shadow |

- Verdict: **Arcade Sort stays the default** (matches MAME at every place compared; Depth Buffer: 3 left, down from
  8). What Depth Buffer still misses: surfaces more than 20 % of their distance apart (Chapter 3's door, the staff
  roll room's wall strip) and shadows held at the same band edge as a big floor polygon. A wider band copies more of
  the arcade; 0.2 was the best of 0.05 / 0.2.
- Testing aids: `HOTD_SHOT_EVERY=N` (a shot every Nth frame with --render), `HOTD_SHOT_MODES=1` (each shot also drawn
  with each Draw Depth: render_f<F>_buffer.png / _sort.png).

## depthopt: Draw Depth off the menu, per-mod depth (2026-10-27)
- User: Draw Depth does not help players with the arcade models (Arcade Sort is the arcade's picture, the Depth Buffer
  only adds glitches). The Options > Video row and its preview pictures are gone (both menu styles; PreviewMaker makes
  models / aspect / blood only). `drawdepth=sort|buffer` (launcher.ini) and `--draw-depth` stay as advanced switches,
  default sort. Per-pixel depth stays where it is better: PC models, the viewer's Free Cam (`Camera::perPixelDepth`).
- Mods: mod.ini `depth = auto | sort | buffer` (assets/mods.h `ModPack::depth`, `modDepthFor`). `TexturePacks::build`
  fills `StageReplacements::modelDepth`: a custom model gets its pack's mode (auto: buffer); a model whose polygons draw a
  picture / colour rule of a pack with an explicit `sort` / `buffer` gets that (auto: none, the game's setting).
  `Renderer::draw` per item (not in Free Cam): under Arcade Sort a "buffer" item sets `uKeyless` (the vertex shader reads
  no sort key: mode 0, depth from the vertex's own z in the sort's key scale, so it still sits among the arcade
  polygons); under Depth Buffer a "sort" item gets an unlimited key band (`uHybrid` 1e6: its polygons at their keys
  whole), a "buffer" item no band. Custom models have no keys, so `sort` changes nothing for them.
- Check: `hotd --mods-test` (modDepthTest: a PC stage with a custom model; depth none / auto / buffer -> Depth Buffer,
  sort -> Arcade Sort; a second model untouched; the values parsed).

## blacksq: black squares on the fat zombie, the chandelier shadow, lying Sophie (2026-10-27)
User report (from before the depth / palette fixes): black squares "like textures / shadows" on the fat zombie's
stomach, the chandelier's shadow when entering the mansion, Sophie lying down. Checked on this build against MAME
(fresh MAME runs, snaps every 0.25-0.5 s; ours `--render` every 60 frames, close-ups every 4-6). Sheets:
private\changelog\img\blacksq\ (sheet_zombie / sheet_sophie / sheet_chandelier.png).
- **Green zombie in the fountain (Chapter 1, frames 4060-4140; MAME 0058)**: no black patches (arcade or PC models).
  Its big black shadow on the water is in MAME too. Already fixed.
- **Sophie lying after the Chariot (frames 15000-15840; MAME 0290)**: her shadow (the shadow pass, low-detail parts
  0x28 / 0x448 / 0x65B ...) is solid black under her, the same shape as MAME's. PC models: no black areas. Already fixed.
- **Chandelier shadow, mansion hall (frame 5488; MAME 0164, the same camera)**: fixed later by chandshadow (below). Was: NOT fixed. The game draws the chandelier
  0xCE7 a second time squashed onto the floor (y row 0, scale 1.3) after draw_fade_begin(0), all black. MAME shows one
  rounded blob under the middle; ours shows the whole squashed model: two extra black trapezoids left / right and a
  bigger middle (they win the polygon sort over the floor: shadow keys ~69 units, floor triangle max z 124). Without the
  copy no black is left (sheet's 4th picture). Not the cause: textures (byte-exact ROM mips), culling (HOTD_NO_CULL: no
  change), Depth Buffer vs Arcade Sort (same). Open question: what in draw_fade_begin(0) (maincpu 0x2320: geometrizer
  function 6, texture parameters from ROM 0x8B8, which also sets the distance coefficient) or the Model 2's handling
  of a zero-y-row matrix (normals of level polygons become 0) removes most of the polygons in MAME. PC models: no
  shadow at all there (the PC draws none).
- Frame scan (chapters 1-4, both models, every 60th frame, 2,700+ frames): near-black rectangles inside lit areas
  found only on Chapter 3's lava-hall stairs (step risers, the same in both models): nothing new. Attract not scanned.
- Debugging: HOTD_TRACE_DRAWN lines now end with " shadow" for a shadow-pass draw and the world matrix (r ...).

## chandshadow: the chandelier's shadow, polygon normals from the corners (2026-10-27)
- Cause: HOTD runs the geometrizer **without stored normals**. Boot (maincpu, after geo_log_table_upload) writes
  command 7 (mode) and then command 0x16 (LOD); in MAME's geometrizer (model2_v.cpp geo_mode) the mode word is the
  next word of the list, so mode & 3 = 2: geo_parse_nn_ns. There each polygon's normal is the cross product of its
  transformed corners (P0, P1, Pa), normalized; it decides the face (rear + not double-sided = culled,
  check_culling), the light and the texture level (distance = coef x |normal . point| x LOD). For a regular matrix that
  is the stored normal turned, which is what we did (uRot x aNrm). The shadow pass draws with a singular matrix (the
  model projected onto the floor along the shadow light, shadow_setup): there the corners' normal is the stored one
  turned by the matrix's **cofactor** matrix: +-the floor's normal by the side the polygon faces the light, 0 when
  seen edge-on. Polygons facing away from the light become rear faces and are culled; the rest get a unit normal (a
  finer texture level, so the translucent texels cut the blob's round edge). Ours turned the normal with the
  squashing matrix itself: every polygon stayed (two black trapezoids from the arms' undersides) and the middle was
  drawn at the coarsest level (a solid rectangle).
- Fix (renderer.cpp vertex shader, arcade models only, draw-only): when the item's 3x3 is (near) singular
  (|det| < 1e-3 x the product of the column lengths: the shadow pass, the squashed _DD shadows), vNrm = normalize(
  n.x (c1 x c2) + n.y (c2 x c0) + n.z (c0 x c1)) (columns c0-c2), 0 when tiny. Other matrices unchanged (for a
  rotation the cofactor matrix is the matrix itself). PC models untouched.
- Result: frame 5488 shows one rounded blob under the middle, the shape of MAME 0164's
  (private\changelog\img\chandshadow\sheet_chandshadow.png: ours before / after / MAME).
- Scan (arcade models, `HOTD_SHOT_EVERY=60`, before / after, `--chapter 1-4`, 40,000 frames; Chapter 4 strict to the
  ending): 2,580 frames, 59 differ, all in shadow-pass shadows only: the hall's chandelier again (Chapter 1 5580: the
  hard rectangle becomes a round blob), creature shadows with a rounder edge or a few culled polygons (most under
  150 pixels at 496x384), and Chapter 2 34200: a black shadow piece that was drawn over a crate's bottom is gone.
  Not checked frame by frame against MAME (only 0164); attract not scanned. PC models: no shadow pass, unchanged.
- **rellook (2026-10-30, user: still a "harsh shadow" at 16:9)**: the squash test compared the determinant with the
  product of the column lengths. The chandelier copy diag(1.3, 0, 1.3) has a middle column of float dust (or 0)
  pointing anywhere, which against its own length did not look small: in most frames (5500-5600, walking in) the test
  failed, the normals stayed sideways and the copy drew hard black trapezoids (the lamps' and the middle's shapes)
  near the camera. Now: |det| <= 1e-3 x (biggest column)^3 (everything the old test caught still passes). Frames
  5520 / 5570 now show round blobs, as MAME `private\mame_dumps\fullgame_us` snap 0072 (the same walk-in); 5488
  (MAME 0164) unchanged.

## overdraw: things drawn over nearer things, PC models (2026-10-28)
User: "objects rendering over each other" in the portable's screenshots of 10-27 (86 shots, PC models, 1P through
Chapters 1-2 and a 4P ending). Each checked against our renders (`--models pc` / arcade, 1280 x 720) and the 1998 PC
(PC harness run `private\depthcmp_scratch\pc_full`). Sheets: private\changelog\img\overdraw\.

| Screenshot | What | Cause | Status |
|---|---|---|---|
| f29147-29186 (Ch 2 stair hall) | the chainsaw zombie's legs / feet and the researcher over the railing and the door's foot | the floor pull (polish2 feet: depth up to 14 units toward the camera for parts within 3 units of the floor), made for the arcade's kerbs, also on PC models | **fixed** (bot frame 10440: behind the railing, as the 1998 PC t24353 and our arcade) |
| (scan) Ch 2 opening doors | bullet holes hidden by the doors | the same pull on the door leaves (game-drawn) | **fixed** (as the 1998 PC t15471 / MAME) |
| f31698 (Ch 2 water room) | pipes over the near railing bars | probably the same pull (not reached by the bot's camera) | likely fixed, unverified |
| f30326 / f30355 (Ch 2 lab) | the big cyan tank over the wall | the PC's own tank texture (1998 PC t25509 the same) | not a bug |
| f7129-7189 (Ch 1 corridor) | a body over the black doorway | the room behind is black in both model sets (arcade render the same) | not a draw-order bug |
| f19067 / f19092 (Ch 2 continue) | thin white lines over CONTINUE | the CONTINUE logo's own swash | not a bug |
| f31612-33135 (4P ending) | (none: P3 / P4 walk-off) | - | - |

- Fix (view.cpp, draw-only): PC-model items get no standing / lying floor pull (`floorPull` 0); floor decals and
  shadows keep the 0.3-unit dip. The 1998 PC draws with a plain z-buffer, nothing to stand in for.
  `HOTD_PC_FLOOR_PULL=1` restores the old pull (comparisons). Arcade models: unchanged (Arcade Sort ignores the pulls).
- Scan (every 60th frame, `--chapter 1-4`, Chapter 4 strict to the ending, 480 x 270): arcade models unchanged by
  construction; PC models 1,107 of 2,580 frames differ (feet / legs within a few units of the floor), the biggest ones
  checked: legs behind railings, bullet holes back, lying bodies whole (no sinking seen).
- Checked, not a cause: Arcade Sort mixing keyed and keyless items (with PC models every item is PC-sourced:
  view.cpp drops arcade fallbacks), the alpha-0x7F pass (depth-tested), the near plane.

### Widescreen side strips (endfloor, 2026-10-28)

- Cause: the arcade world draws only the current draw zone's placements (BGTB); the zone lists were made for the 4:3
  views, so pieces only a wider view reaches are missing at the sides (arcade models; the PC world drew them).
- Fix (view.cpp + renderer.cpp, draw-only): in widescreen, placements of zones zone +-1 whose bounding sphere lies in the
  wide view but not wholly inside the 4:3 band, and not at a position the zone draws, are drawn with
  `DrawItem::sideOnly` (fragments inside the centred 496 x 384 picture discarded). The original picture: pixel-identical.
  `HOTD_WIDE_ZONES=0` off.
- 4-19 floor at the far left: not missing geometry (ray-picked: the room4_05a floor polygon), the floor texture's baked
  shading. Not changed.
- Scan (every 60th frame, chapters 1-4, magenta clear `HOTD_CLEAR_KEY=1`): arcade-only side holes 28 -> 15 frames; the
  rest also in the 4:3 picture or tiny.
- visfix (2026-10-29): the sideOnly pieces only fill holes. Drawn with the zone's own items they stood in front of them
  where a neighbouring zone is another dressing of the same space (the ending room after the Magician: the fight's
  wall panels over Goodbye Curien's floor and window). renderer.cpp now draws them first, clears the depth, then the
  sky and the zone's items over them: they show only where nothing else drew.
