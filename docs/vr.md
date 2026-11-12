# VR mode

**Experimental** (user 2026-11-10). User request 2026-11-03: a VR mode "using the player hand and gun along with the crosshair shown". Status: the whole
VR frame, the aiming and a desktop **preview** backend work and are tested headless; the **OpenXR** backend (a real
headset) is built (OpenXR SDK 1.1.65's loader, API 1.0) and its start-up failures are tested, but it has not run with
a headset yet: the development PC has no OpenXR runtime (see "OpenXR backend" and "First headset test").
**Standalone Meta Quest 3** (an Android APK, OpenGL ES, the same OpenXR backend with the Android / ES binding; the menus
on a big screen with the controller as the mouse): docs/quest.md.

```
hotd --vr preview                       # VR in the window: both eyes side by side, head on the keypad, gun on the mouse
hotd --vr preview --vr-eye right        # one eye fills the window
hotd --vr openxr                        # a headset (SteamVR / Quest Link / WMR / Virtual Desktop); flat if none
hotd --vr-preview-test --autofire --mute --lives-kept 1 --frames 2200 --size 1600x800 --shot-at 2100 --shot-dir <dir>
```

Nothing of VR runs without `--vr`: the normal game's pictures are byte-identical with and without the VR code (the
regression runs and `--render` pictures are compared, see "Tests").

## The idea

The House of the Dead is an on-rails light-gun game: the game moves the camera, the player only aims. VR keeps that:

- **The game camera stays authoritative.** The player's space (the "reference frame") is attached to the game's
  camera every frame; the tracked head turns and leans on top of it. The game logic never sees the head.
- **The gun is a ray.** The right controller holds the game's own gun hand (attached to its aim pose, the barrel on
  the controller's pointing ray). Its ray leaves the muzzle,
  goes into the world until it meets what the eye sees (the eye's depth buffer), and that 3D point is projected through
  the *game camera* (not the eye: the aim does not move when the head moves) onto the arcade's 496 x 384 screen. That
  2D point is player 1's gun input, exactly what the mouse or a light gun gives the game.
- **Off the screen** (vr4, 2026-11-09; vr.ini `offscreen_reload`): VR draws the world all around you, so a pull at a
  creature beside or behind you is off the arcade's 4:3 screen. With `below` (the default) such a pull **does
  nothing** (no shot, no reload, no rumble; the crosshair is greyed while it would): only a pull aimed **below the
  screen** (under its bottom edge, in a wedge that widens downward from its sides: the floor in front of you), with the
  gun **pointed steeply down** (`reload_down`, more than `reload_pitch` degrees under *your* horizon), or a reload
  button / grip / shake reloads. `any`: every pull off the screen reloads, the light gun's rule (as before). Pointing
  down alone (the gesture) also reloads once. "Down" is the player's own down (the reference frame's up), not the
  world's: in follow mode the game camera looks steeply down in the Hangedman's fight (pitch -60 to -90 degrees), and
  with the world's up every pull at the screen's middle there counted as "pointing down" and reloaded (port0r's
  "every shot reloads"): 102 of 2127 round trips in a Hangedman Boss Mode run before, 0 after (`vr_aim.h pullAction`,
  `Scene::frame`; the preview's mouse button is dropped too while ignored, main.cpp `vrGunInput`).
- **The crosshair** is the game's own sight picture (the one the HUD would draw for player 1, any Firing Range style,
  `Hud::vrSight`) on a quad at the ray's hit point, turned to the eye and scaled with the distance so it keeps the same
  angular size (`crosshair_degrees`); a faint **laser** runs from the muzzle to it (`laser`).
- **Laser / crosshair off** (vrsight, user 2026-11-10: "allow the ability to turn off crosshair and laser"):
  **Options > Gameplay > Laser (VR)** and **Crosshair (VR)**, On / Off, next to Dual Pistols (VR) and made the same way:
  rows that exist only in a VR run (`MenuHooks::vrLaserDefault` / `vrCrosshairDefault`), taken at once, also from the
  pause's Settings (launcher.ini `vrlaser=` / `vrcrosshair=`: -1 not set, 0 off, 1 on). Not set, `vr.ini laser` /
  `crosshair` (both on) decide. Off hides it for every gun (dual pistols and Survival's Double Pistols too; with the
  crosshair off its greyed off-screen state is not shown either); the aim, its depth, the shots and the reloads are
  unchanged (`Scene::laserOn` / `crosshairOn`, set by main.cpp `vrFrame` each frame). The flat game's **Video > Show
  Crosshairs** (launcher.ini `showcrosshairs=`) already hid the VR crosshair before (`Hud::vrSight` draws no sight
  with it off, as player 1's Firing Range Crosshair Off): that stays, so the VR crosshair shows only with both on; the
  laser never followed it. The desktop spectator view (`desktop_view = spectator`) keeps its 2D sight. Pictures:
  `--vr-preview-test --shot-at 2100` with `vrlaser=0` / `vrcrosshair=0` in launcher.ini, or
  `--vr-set laser=off --vr-set crosshair=off`.
- **The HUD** (lamps, ammo, scores, captions, cut-scene bars, the pause menu, player 2's sight) is drawn by the normal
  HUD code into a texture (the game area's shape, 960 lines) and shown on the **arcade screen's plane**: a quad
  `panel_distance` metres ahead of the reference frame, exactly as large as the original picture's frustum there. So
  the HUD sits where the flat game put it, and in follow mode it moves with the game camera, not with the head.

## Spaces

| space | handedness | units | ahead |
|---|---|---|---|
| tracking (OpenXR LOCAL / the preview) | right-handed | metres | -z |
| the player's space (the reference frame's local space) | left-handed | game units | +z |
| the game world | left-handed | game units | the camera's +z |

`vr_math.h poseToLocal` converts a tracking pose: z mirrored (S R S with S = diag(1, 1, -1)) and scaled by
`units_per_metre`. Eye in the world = reference x eye pose; its view matrix goes to the renderer.

**Scale**: `units_per_metre = 10` by default. Evidence: the ragdolls' gravity is 0.0272 units per frame squared at
57.5 Hz = 90 units / s^2, about 9.2 units per metre if it is 9.8 m/s^2; the zombies stand about human height next to
the preview's hands at that scale (pictures below). `HOTD_VR_DEBUG=1` logs the eye's height over the ground ahead
every 300 frames to check it on other scenes.

## What changed in the renderer (shared with the normal game)

- `Renderer::EyeView` / `Renderer::eye` (render/renderer.h): a draw can take an explicit world -> eye matrix and an
  **off-axis frustum** (the four edge tangents, as OpenXR's XrFovf gives them) instead of the Camera's symmetric
  perspective. The eye position / forward for the Model 2 shading come from that matrix, the near / far planes and the
  depth tricks stay the same. Null (always outside VR): the old code path, bit for bit. The same hook serves any future
  off-axis view (multi-monitor, a photo mode's lens shift, head tracking on a flat screen).
- `Renderer::lastViewProj`: the matrix of the last draw, for overlays drawn over the 3D in the same space.
- `GameView::drawOverride` (game/view.h): the game builds its draw list once (zones, interpolation, skins, palette)
  and hands it over; VR draws it per eye. Unset: `draw` draws as before.
- `GameView::cameraToWorld`: the game camera of a frame as a rigid transform (the aim's reference).
- `Hud::vrSight`: player N's current sight picture and whether it would show now.
- The `sideOnly` strips (widescreen's extra zone pieces) are drawn whole in an eye view.

## All around you

User 2026-11-09: "looking behind you shows the world behind you". The arcade draws what its camera sees: each
**draw zone** (the stretch of track the camera is on) has a list of the world pieces its 4:3 views show, and the
world task draws only that list. In VR the head turns anywhere, so with the list alone the world behind and beside you
had holes (Chapter 1: the courtyard's buildings behind you, the mansion when you look back from the bridge). Now
(vr.ini `around = on`, the default; `GameView::vrAround`, set by the VR frame only):

- the pieces of the **neighbouring zones** (the current one +- 2, `HOTD_VR_ZONES`) that lie **wholly outside the game
  camera's 4:3 view** (behind, beside, above you) are added for the eyes (`DrawItem::around`: drawn in an eye view
  only, depth-tested with the zone; the renderer never draws them in a flat picture);
- the pieces of **any other zone** too when they are **far away** (their nearest point over 400 units, 40 m:
  `HOTD_VR_FAR`), the landmarks; never one near you (a zone's other dressing of the room you stand in);
- not where the zone already draws a piece (the alternate states at one place: the mansion entrance's 0x9E3 / 0x9E4 /
  0x9E5, ...), and one piece per place among the added ones; what the game camera itself sees stays exactly as the
  arcade's list says (pieces across the 4:3 edge: the widescreen strips' rule, as before);
- with PC models the same through the PC world's IDs in the zone lists (its pieces sit at the origin with their
  vertices in world space: their bounds' middle is their place);
- they are draw-only: no ragdoll walls, no game logic; the flat picture is byte-identical (the `--render` pictures,
  arcade and PC models, compared with the build before).

The sky dome already goes all around (the game's sky models are cylinders / domes); up and down were fine. What is
still the game's own: creatures and props the game logic does not draw off its screen (the PC kinds' `pc_view_test`
in PC Mode decides that as THOTD.EXE does; not changed: that is the game), and real voids (Chapter 4's halls have no
walls where the camera never looks: black there, as in the data).

Pictures (`--vr preview --vr-eye right --vr-head yaw,0,0`; +yaw turns left): `HOTD_NO_ZONES=1` (every piece of the
chapter at once) as the reference; Chapter 1 frame 3500 looking right now shows the building there, frame 4500
looking back from the bridge the mansion, PC models alike; Chapters 2-4 match the reference except for alternate
states. vr.ini `around = off`: as before.

VR draws the arcade models with the **per-pixel depth** (`Camera::perPixelDepth`, the cut-scene viewer's Free Cam
setting) whatever Draw Depth says: Arcade Sort sorts each polygon by its distance to the eye, and two eyes 6 cm apart
could sort a pair differently: one eye would see a different polygon in front (stereo rivalry). It also makes the
depth buffer a real distance, which the aim reads.

## Comfort

- `follow = follow` (default): the reference frame takes the game camera's full orientation (the arcade's framing:
  when the game looks up at the Chariot, so do you). `follow = level`: only its position and heading; its pitch is not
  forced on you (look up yourself). In both the camera's motion is the game's: VR cannot change the rails.
- `vignette` (default off, user 2026-11-10; on for players prone to motion sickness): the view's edges darken while the reference frame turns faster than `vignette_turn` degrees / second
  (up to `vignette_strength`); it eases in fast and out slowly. Smooth Motion (the frame blending) applies to the eyes
  too, so the camera moves at the display's rate, not the game's 57.5 Hz.
- The HUD stays on the screen plane, never glued to the face.

### Screen view and the comfort rows in the game

vrcomfort (user 2026-11-10, from a player's comment: "a window at a certain distance to prevent motion sickness or an
immersive mode to feel like you're inside the game", each player's own). On the Quest vr.ini is hard to reach, so the
comfort settings are rows of **Options > Gameplay** (the Quest's Controls page), Experimental, only in a VR run (all of
them apply on the Quest too: none hidden), taken at once (the pause's Settings too):

| Row | Values | launcher.ini | vr.ini (when not set) |
| --- | --- | --- | --- |
| View (VR) | Immersive / Screen | `vrview=` 0 / 1 | `view = immersive` / `screen` |
| Screen Distance (VR) | 2 / 3 / 4 / 6 m | `vrscreendist=` 2 / 3 / 4 / 6 | `cinema_distance` (3), `menu_distance` (2.5) |
| Screen Size (VR) | Small / Medium / Large | `vrscreensize=` 0 / 1 / 2 | `cinema_width` (4), `menu_width` (3.2) |
| Cut-scenes (VR) | World / Screen | `vrcutscenes=` 0 / 1 | `cutscenes = world` / `cinema` |
| Comfort Vignette (VR) | Off / On | `vrvignette=` 0 / 1 | `vignette` (off) |

Each launcher.ini key is -1 until the row is clicked: vr.ini decides (and the row shows vr.ini's value; a Screen Size
vr.ini's widths match none of shows Custom). Nothing changes for a player who never touches them.

- **View (VR) = Screen**: the whole game, play included, on the cinema screen ("Cut-scenes": the flat picture with the
  HUD, the bars and the fades, in a dark room, fixed in your own space: the game camera's turns and cuts never move
  you). The guns aim at the screen as the name entry's does: each gun's ray (seat space) meets the screen's plane, and
  that point on the picture is the arcade aim, exactly (no depth involved). The sight is drawn into the picture where
  the gun meets it, the laser runs from the muzzle to the screen, the hands are in front of it (only in control, as in
  the world). Off the screen as the world's guns: aimed below the screen or the gun pointed down
  (`reload_pitch` under your own horizon) a pull reloads; beside / above / away from it `offscreen_reload` decides
  (below: nothing). Dual pistols: both guns on the screen. The vignette is not drawn on the screen (nothing turns).
- **Screen Distance / Size**: the cinema screen (Screen view, cut-scenes on the screen, the chapter cards, the name
  entry) and the menus' big screen. Size is the screen's width as a part of its distance: Small 0.9 x, Medium 1.33 x
  (vr.ini's 4 m at 3 m, about 67 degrees), Large 1.8 x (the menus' screen 0.96 x that: its own 3.2 m at 2.5 m).
  Distance alone keeps vr.ini's angular size (farther = bigger, the same in view; only the eyes' depth changes).
- Code: `vr::Settings::view`, `Scene::ini` (vr.ini as loaded) and main.cpp `vrComfortApply` (the rows over it each
  frame, the menus' big screen too), `Scene::frame`'s cinema branch (`screenAim_` / `screenAimL_`), `menu_pages.cpp
  vrRows` (`MenuHooks::vrComfortDefault`).
- Test: `--vr-preview-test --vr-set view=screen` puts the preview's cursor on the game's aim on the screen each frame;
  the next frame the gun (it points at the cursor) must meet the screen at that aim: `vr screen view test: 1843 frames
  of play on the screen (screen 3.0 m, 4.00 m wide); 2999 round trips ..., mean error 0.000, worst 0.000 arcade
  units, 0 over 2` (3000 frames). With `--vr-dual` the VR guns are player 1's input: right gun 23 hits, left 14 (3000
  frames). launcher.ini `vrview=1 vrscreendist=6 vrscreensize=2`: the same at 6 m, 10.8 m wide.

### The HUD and close creatures

vr4 (2026-11-09). The HUD panel is drawn over everything (no depth test), `panel_distance` (3 m) ahead. A creature
nearer than that (HOTD's grabs come to about 1 m) was painted over by the HUD while the eyes converge on the creature:
the HUD says "behind", its picture says "in front" (eye strain). Two ways out were weighed:

- **the HUD with the depth test** (`panel_depth_test = on`, kept as an option): the near creature hides the lamps and
  the bullets exactly when you need to see them (reload during a grab), and so do nearby walls and door frames;
- **adaptive distance** (`hud_distance = adaptive`, **the default**): the panel eases to just in front of the
  nearest creature inside its view (0.25 m before that creature's nearest side, never nearer than `hud_min_distance`,
  0.7 m), keeping its angular size (the frustum's size at that distance: the HUD looks the same, only its depth
  moves). It comes forward fast (a lunge, ~0.1 s), stays 0.5 s, and goes back out slowly. The HUD stays readable and
  always in front of what it covers. `hud_distance = fixed`: always at `panel_distance` (as before).

**The hands and the HUD** (vrfix, user's playthrough 2026-11-10: the lamps / bullets drawn over the raised left hand,
"1UP!" behind the gun and the researcher). One rule: the world, then a near layer over it with the depth cleared (the
hands and the extra-life item 0x3BB / 0x3BC, which the flat game shows in front of everything), each by its own depth;
then the HUD panel depth-tested against that layer only. The hands are always nearer than the panel (it never comes
nearer than `hud_min_distance`), so a hand hides the part of the HUD it is really in front of (lower it to read the
HUD, as with a real object), and nothing else ever covers the HUD: walls and creatures can't, since their depth is
cleared. `panel_depth_test = on` keeps the world's depth only while no hands are drawn.

Chosen: adaptive (readability during grabs; no conflict). The creatures are the game's own draw list (the models a
body or a boss draws: `DrawItem::vrKind` 1, the blood bursts left out), their bounds as spheres
(`vr_scene.cpp nearestCreature`): no depth read, so it works the same on OpenGL ES (the Quest). Walls nearer than the
panel do not move it (the camera passes door frames all the time; the HUD sits at the screen's edges).
`--vr-preview-test` prints `vr hud test:` (5000 frames of Chapter 1: a creature inside the panel's view in 3422
frames, nearer than 3 m in 1405 (all drawn over by a fixed panel), over the adaptive panel 99 (the easing's
lag; plus 624 nearer than 0.95 m, where the panel stops at its minimum)).

Snap-turn is meaningless on rails. **Recentre** (2026-11-09): OpenXR's LOCAL space puts the seat's origin at
the head when the game starts; hold one thumbstick down for 2 s to move it to where the head is now (its place,
height and heading; the horizon stays level), e.g. after sitting back or turning the chair. The runtime's own recentre
(Quest: hold the Meta button; SteamVR: Reset Seated Position) works too and drops ours. The preview's is keypad 5.
vr4: when the head's **position is not valid** (a 3DoF moment, a Quest with no boundary set up) the recentre turns the
seat to the head's heading alone and keeps its place (before: nothing happened). **Auto-recentre**: the headset back
after more than `auto_recentre` seconds away (3; taken off, the app hidden: the session not VISIBLE / FOCUSED; the
runtime's dashboard over the game does not count) recentres by itself (`openxr: the headset is back ...` in the log);
`auto_recentre = off` never. (`vr_aim.h recentrePose`, `AwayClock`; unit-tested: `hotd_unit vr`.)

## Aim depth

vr4 (2026-11-09). The gun's ray goes into the world until it meets something; that point is projected through the
game camera (the aim) and the crosshair drawn there. How far it goes (vr.ini `aim_depth`):

- **desktop OpenGL** (`auto`): the right eye's depth buffer (`Scene::march`), exactly what is drawn; since
  2026-11-10 (vrnull) from the right eye's world drawn once more (at half size; modfix: the eye's size) with the free camera's narrow key band
  (the eyes draw with the flat game's 0.2 band, `Camera::keyBand`, so the opening's zombie and researcher stand on the
  kerb, but that band pulls the depth toward the sort keys: the rays read surfaces up to 20 % off). Measured
  (`--vr-preview-test`, 1600 x 800): 2200 frames 10 of 753 trips over 6 units, mean 0.81 (the eye's depth: 15 / 812,
  1.10); cinema 5000 frames 18 / 1305 = 1.4 %, mean 0.76 (main: 28 / 1473 = 1.9 %); frame time 1.53 ms (1.50).
  (modfix, 2026-11-10: that pass is now at the eye's own size, and `march` checks every pixel between two of its
  samples: at half size the 3000-frame test failed, 25 of 1136 = 2.2 %, a thin outline the ray grazed was stepped over
  and the ray went on to the wall behind. Now 3000 frames 17 / 1185 = 1.4 %, mean 0.42 (0.79); 2200 frames 9 / 810,
  mean 0.46; cinema 21 / 1481 = 1.4 %; dual 18 / 1216 = 1.5 % (half size with the per-pixel checks: dual 23 / 1110 =
  2.1 %); the preview's frame about 0.8 ms more at 1600 x 800, a busy PC's runs);
- **OpenGL ES (the Quest) / ANGLE** (`auto`: the depth cannot be read back there): a **CPU ray test** (`Scene::cpuRay`)
  against the stage's own triangles (the world objects the zone drew this frame and the moving props: the walls and
  floors `GameView` builds for every chapter, game/ragdoll.h) and the creatures' parts (each part model's box as
  placed this frame, `DrawItem::vrKind` 1); about 3 microseconds a ray. Before vr4 it was a fixed `aim_distance` (8 m):
  the crosshair at the wrong depth on every target, and, worse, the **aim itself off**: the point 8 m along the gun's
  ray, seen from the game camera, is not the target unless the target is 8 m away (the hand is off the eye: parallax);
- `aim_depth = cpu` forces the CPU test on a desktop (the Quest's path, tested against the eye's depth), `depth` the
  eye's depth only (the fixed distance where unreadable), `fixed` the old fixed distance (the tests' baseline).

Test (`--vr-preview-test --vr-set aim_depth=... --frames 5000`, Chapter 1, the round trip through the game's aim):

| aim_depth | round trips | mean error (arcade units) | over 6 units |
|---|---|---|---|
| depth (the PC) | 2434 | 0.62 | 1.3 % |
| cpu (the Quest now) | 2321 | 1.37 | 6.1 % |
| fixed (the Quest before) | 3331 | 66.1 | 68 % |

With `cpu` the run also compares each CPU ray with the eye's depth (`vr cpu ray test:` line: 57 % within 5 % / 0.2 m;
most others the CPU nearer: thin poles and rails the depth march steps over, alpha-cut texels, the parts' boxes being
a little larger than the limbs). Pass for `cpu`: mean under 2, at most 8 % over 6, half the rays agreeing.

## Cut-scenes

User 2026-11-09: cut-scenes in place and fully viewable, the headset as the camera. A cut-scene is the game in play
with `scene_state` 4 (the HUD's HOLD YOUR FIRE!) or the cut-scene bars on (`letterbox`), not the chapter's results
(main.cpp `vrFrame`: `Scene::Frame::cutscene`). vr.ini `cutscenes`:

- **`world`** (the default): the cut-scene plays around you in the world. You stand where the game's camera is (the
  reference frame follows it as in play, `follow` applies) and look around freely: the characters, the room, behind
  you too ("All around you"). The arcade's **bars are not drawn** (`Hud::noBars`, set by the VR frame only): on the
  HUD's screen plane they were two black bands floating 3 m ahead that cut the scene's top and bottom out of the
  view; the captions (subtitles) and HOLD YOUR FIRE! stay on the screen plane, where the flat game puts them. The
  camera's cuts move you with them (the game's camera is the camera); if that is too much, use cinema.
- **`cinema`**: while a cut-scene plays you sit in a dark room in front of a big screen (`cinema_distance` metres
  ahead of your seat, 3; `cinema_width` metres wide, 4: about 67 degrees) showing the flat picture as the window
  would (the game camera's view, the bars, the captions and fades over it: `Scene::drawCinema`). The screen is fixed
  in your own space (the seat: OpenXR LOCAL, the recentre moves it), so the camera's cuts and turns do not move
  you: for comfort when the camera cuts fast. Play goes back to the world when the cut-scene ends (the bars gone).

In both the guns keep aiming (their rays; no shot goes through in a cut-scene, as in the game). Test pictures:
`--vr preview --vr-eye right --chapter 1 --render --shot-at 300 --shot-at 400 --shot-at 700 [--vr-head 180,0,0]`
(Chapter 1's opening: the bars gone in world, looking back shows the road and the courtyard), the same with
`--vr-set cutscenes=cinema` (the screen; `--vr-head 30,0,0`: it stays put while you turn).

## Shot flash

vr4 (2026-11-09), checked: the shot's flash and spark are **not** HUD sprites (they are not on the panel). They are
3D models the game logic places itself: `shot_effect` (0x95270) puts the flash (0x95140, model table 0xCA738 +
frame, scale 0.1) and the spark (0x95630, model 0x660 / 0x661, flying 20 units a frame along the shot's ray) at the
**near point**, the aim at camera depth 1 unit (port/hit_effects.cpp). In the flat picture that is the right size; in
VR it is 10 cm in front of your eyes: a large double image before the target (both eyes disagree by a hand's width,
picture `--vr-set shot_flash_at_hit=off --shot-at 1607`: the star left of the target in one eye, right in the other).

`shot_flash_at_hit = on` (the default): in the eyes' draw only, each flash / spark still in front of where the game
camera's ray through it meets the world (the CPU ray test, "Aim depth": the world is not drawn yet at that point) is
moved out along that ray to just in front of the surface and scaled by the same factor, so the game camera sees it
exactly as before (the flat look) while the eyes see it on the target. A spark already past the hit flies on as the
game moves it. The models are found by their tasks (`DrawItem::vrKind` 2 / 3, set by `GameView`; the flat draw
ignores it). `--vr-preview-test`: `vr shot flash test: 183 flash / spark draws moved out to the hit (mean 13.3 m)` in
2200 frames; frame 1607: the flash on the zombie in both eyes.

## Hands and gun: the game's own models

User 2026-11-03: no made-up or generated assets. The hands are the game's own models, read at run time from the
player's game data like every other model (the arcade ROMs, or the PC files with PC models on: `GameView::placedItem`
takes the model source's model and the area's texture set), drawn by the game's renderer:

| on | model | what it is |
|---|---|---|
| right controller (aim pose) | `0x719` `PN_ter00a` | the AMS agents' right hand holding the handgun: joint 19 of every player skeleton (Rogan, G and both alternate skins), the cut-scenes' gun hand; arcade and PC (CREAT1.RBH) |
| left controller (grip pose) | `0x717` `PN_tel00a` | Rogan's left hand (joint 16); arcade and PC (CREAT1.RBH) |

There is no stand-alone gun model in either game (the 7,477 arcade object names and the PC CREAT files were searched:
gun / pistol / jyu / kenju / buki / weapon / magazin; the gun exists only inside 0x719, and the code's gun-only copy
`kGunOnlyKey` is 0x719 without its skin polygons, arcade only). The SMG has no model in the game: the
handgun is drawn then too. The previous worker's placeholder boxes (pistol, SMG, hands) and the OBJ loader
(`src/vr/vr_models.*`) were removed.

Their place (measured from the data, `vr_scene.cpp`): 0x719's barrel runs along its model -y (the slide's back at
y -0.41, the muzzle at -2.26, its middle line x -0.375 z 0.65, the top +z); it is turned so the barrel lies on the aim
pose's ray, the top up, the slide's back on the aim pose's origin (the hand hangs below and behind it). The shot's ray
starts at the muzzle, on the same line. 0x717 (the wrist at the origin, fingers +x, palm -z, thumb +y) is turned so the
fingers point ahead, the palm faces right (the controller in it), the thumb up, the palm's middle on the grip pose.
Both are in game units, so their size is the game's own (the gun ~23 cm at 10 units / m). The hands are drawn after
the world with the depth cleared (on top of walls, never cut by them). `vr.ini` moves them per controller:
`gun_offset` / `gun_rotate` (metres along the aim pose's axes / pitch yaw roll degrees), `left_hand_offset` /
`left_hand_rotate`.

Only while you play (user 2026-11-10: "You should not be able to see hands during the attract demo. Only in
gameplay."): the hands, the lasers and the 3D crosshairs are drawn while player 1 is in control
(`vr::Scene::playerInControl`: in game, not the ending or a chapter clear, the shots on (`shot_enable`: off in the
cut-scenes and the chapter cards), player 1 playing (state 5 / 8, as the flat crosshair); two frames in a row). The
title, the attract demo, the story intro, cut-scenes, chapter cards, results, name entry and the continue screen show
none; the pause keeps them (the gun points at its items). The aim is still worked out underneath. `--vr-preview-test`
prints `vr hands test:` (frames shown outside control and hidden while playing: both must be 0);
`HOTD_VR_HANDS_TRACE=1` logs each change, `HOTD_VR_CAM_TRACE=1` the game camera and the right eye every frame.

## Dual pistols

Player request 2026-11-04: a gun in each hand. vr4 (user 2026-11-09): **Options > Gameplay > Dual Pistols (VR)**, a
row that exists only in a VR run (`--vr ...`; hidden in the flat game: `MenuHooks::vrDualDefault`), taken at once,
also from the pause's Settings (launcher.ini `vrdual=`: -1 not set, 0 off, 1 on). Not set, `vr.ini dual_pistols`
(the default / fallback, off) decides; `--vr-dual` forces it on. New texts in all 16 `data/lang` files.

**A magazine each** (vr4, user 2026-11-09; offline, not Survival's Double Pistols weapon): before, the pair shared
player 1's one magazine. Now the right gun is the game's own magazine (the Player record's rounds / empty) and the
left gun has its own (`Runtime::VrLeftGun`): its own reloads (its grip, pointing it down, its shake, a pull of it
below the screen: the off-screen rule per gun; `Scene::Aim::reloadHand`), its own count. One gun acts a tick
(main.cpp `vrGunInputSeparate`): the right gun's pull / reload as player 1's input as always, else the left gun's,
whose tick runs with the left magazine swapped into the player's record (`Runtime::vrLeftBefore` / `vrLeftAfter`
around the tick's frame), so the game's own `player_shoot` fires or reloads it (its sounds, RELOAD!'s flag, Infinite
Ammo, PC Mode's rounds). Both pulled at once: the right shot, the left one in the next tick (17 ms). Auto Reload
reloads an empty left gun too; the reload key reloads both. Out of play (a continue, a new game) the left magazine is
full again. The flat game and one-gun VR never touch it. **Online**: one shared magazine as before (the rollback
state keeps one per player; VR is local, the input path stays the game's).

**The HUD**: the left gun's rounds as a second row of player 1's bullets (the game's sprite 0x10, its colours)
**directly above** the usual row: the same x places and spacing, one bullet's height higher (`Hud::drawVrLeftRounds`;
arcade cells 2 + 2 i, row 0x26 minus the sprite's rows; the PC models' matching sprite). The torches and "1P" are
below it, players 3 / 4's bands at the top, RELOAD! in the middle rows: nothing above it in the HUD. Pictures:
`--vr-preview-test --vr-dual --shot-at 2100` (arcade: 6 left, 4 right), the same with `--models pc`.

- **The left gun** is the same gun hand `0x719` **mirrored** (model x -> -x: a left hand holding the handgun; there
  is no left hand with a gun in the game). The renderer turns the normals by the same matrix (`uRot * aNrm`), so the
  faces, their culling and the light stay right. It sits on the left aim pose as the right one sits on the right
  (`gunAnchorLeft`), moved by `gun_offset` / `gun_rotate` mirrored (x, yaw and roll the other way). The empty left
  hand `0x717` is not drawn then.
- **Each gun aims and fires for itself**: its own ray (from its muzzle, into the right eye's depth), laser, the game's
  sight at its hit, trigger (the same hysteresis), rumble on its own controller (`haptic(0 / 1)`), its own "pointed
  down" reload gesture. Either grip / A / B, or a pull of either gun off the screen, reloads.
- **Into the game** (main.cpp `vrGunInput`; this paragraph: online and Survival's Double Pistols, one magazine; offline
  see "A magazine each" above): it stays one player with one magazine (the game's own; Survival's Double
  Pistols: its 16), the shots are player 1's. A pull of one gun is player 1's shot at that gun's aim (the shot flash
  there too). Both pulled in the same tick: the right gun's shot and the left gun's own ray beside it, as the Double
  Pistols' second gun (`port/game.h SecondGun`: after the seat's shot, `weapon_extra_shots` tests the left aim's ray,
  hits the nearest, its own shot flash; one round for the pair, as the Double Pistols take). Both pulled but one off
  the screen: the reload first, the other gun's pull in the next tick. Between shots player 1's aim is the right gun's
  (the right controller off / asleep / untracked: the left gun alone is player 1's gun, its aim too; 2026-11-09).
- **Every mode**: the arcade game, PC Mode, Boss Mode... as two guns for player 1. **Survival**: the weapon
  decides: the Double Pistols in player 1's hands are always two VR guns (their fixed second ray is then not added:
  each gun aims for itself, `Weapons::setShot`), every other weapon one gun.
- Preview: both guns point at the cursor's point (from their own hands); keypad 0 / . pull the left / right trigger.
- OpenXR: nothing new: both hands' aim poses, triggers and haptics were already bound for every profile.

## Preview (desktop simulation)

`--vr preview`: both eyes side by side in the window (`--vr-eye left | right | both`), a headset-like frustum per eye
(100 degrees high, 5 % wider toward the temple, so the off-axis path is exercised), IPD 64 mm.

| input | does |
|---|---|
| mouse | the gun points at what the cursor shows in either eye (the ray from that eye through the cursor, as far as that eye's depth says; the gun then aims at that point from the hand, so the muzzle's parallax is real) |
| mouse buttons / keys | the game's own trigger / reload (unchanged) |
| keypad 4 / 6, 8 / 2, 7 / 9 | turn the head left / right, up / down, roll |
| keypad 1 / 3 | lean left / right |
| keypad 5 | recentre |
| `--vr-head yaw,pitch,roll` | hold the head there (degrees; tests) |
| keypad 0 / . | dual pistols: the left / right gun's trigger |
| `--vr-dual` | dual pistols on (as vr.ini `dual_pistols = on`) |
| `--vr-set key=value` | a vr.ini key over the file's (repeatable; tests: `--vr-set around=off`) |

## Tests

- `--vr-preview-test` first runs the aim's math (`vr::selfTest`): 200 random game cameras, zooms, hand offsets and
  arcade points (the ray from the muzzle to the world point the camera sees at (x, y) must map back to (x, y): worst
  error 0.0009 units), the 4:3 / 16:9 screen edges, behind the camera, pointing down, the tracking -> player space
  conversion (ahead, turned left, looking up). Then it runs `--render` with the preview: every frame the game's own aim
  (`--autofire`) is turned into the world point the game camera sees there (the right eye's depth), the cursor is put
  on it, and the whole chain is checked in that frame: the gun's ray from its muzzle to that point, its hit in the
  eye's depth, back through the game camera to arcade units. Pass: mean error under 2 arcade units and at most 2 % of
  the trips over 6. Trips where something stands between the hand and the point (the gun's view blocked) are counted
  apart (the gun then hits that, which is right).
  vr4: each checked trip with the game's aim well inside its screen also asks what a pull would do with the play
  settings (`reload_pitch`, `offscreen_reload`): it must be a shot (at most 2 % otherwise; `vr offscreen test:` line).
  `--vr-preview-test --boss-mode hangedman --frames 4000`: 2127 such trips, 0 taken as off (102 before the
  player's-down fix). The math part checks `pullAction` (on / beside / above / below / behind / pointing down, both
  policies); `hotd_unit vr` too (`vr.offscreen_reload`).
- `--vr-preview-test --vr-dual` (dual pistols): both guns follow the game's aim from their own hands, both round trips
  are checked, and the VR guns are player 1's input (main.cpp `vrDualTestInput`): each `--autofire` pull goes in
  turn to the right trigger, the left one, both (the preview's scripted triggers, one VR frame held). Counted per gun:
  pulls, shots (a round gone), hits (`stat_hits`; the left ray beside a right shot: `SecondGun::hits`). Pass: the
  one-gun criteria, and each gun alone and both together shot, both guns hit, the left round trips' mean under 2.
  2200 frames: right 10 shots / 8 hits alone, left 10 / 8, both 11 shots with 9 + 8 hits; left mean error 0.28.
  vr4, a magazine each (offline): "both" is the right shot and the left one a tick later (no "both" shot); the left
  gun's reload button is pressed (scripted) a second after its magazine runs dry; `vr dual magazines test:` counts the
  left magazine's rounds spent and reloads, the ticks with the left gun empty and the right loaded, and the reloads of
  one gun with the other's rounds unchanged (all must be > 0). 3000 frames: right 30 shots / 21 hits, left 27 / 16;
  left 27 rounds / 4 reloads; 218 ticks left empty with the right loaded; alone: left 4, right 5.
  With Survival's Double Pistols (`HOTD_WEAPONS_FORCE=3 --vr-preview-test --survival-test 2
  --survival-weapons-wave 1`): two VR guns by themselves, 27 hits each.
- `--vr-preview-test --vr-set cutscenes=cinema --frames 5000` (the cinema screen): the cut-scenes go to the screen
  (`vr cinema test: N frames on the cinema screen`, N > 0 to pass), the round trips are the play frames between
  (5000 frames: 1803 on the screen, 1471 trips, mean 0.91). Shorter runs are mostly cut-scene and too few trips.
- The normal game is unchanged: the WORKER_RULES regression runs and two `--render` pictures are byte-compared with
  the build before the VR work.

## OpenXR backend

`src/vr/vr_openxr.cpp` (OpenXR 1.0 core + `XR_KHR_opengl_enable`).

**Build.** The Khronos **OpenXR SDK**'s prebuilt loader (1.1.65, Apache License 2.0) unpacked into
`private/deps/openxr` (not committed; it has `OpenXRConfig.cmake` at its root, `include/openxr`,
`x64/bin/openxr_loader.dll`, `share/doc/openxr/LICENSE`). CMake finds it there (or through `-DOpenXR_DIR=<sdk>` /
`CMAKE_PREFIX_PATH`) and turns `HOTD_OPENXR` on by itself; without the SDK it stays off and the build is as before
(`--vr openxr` then says "built without HOTD_OPENXR"). `openxr_loader.dll` is copied beside hotd.exe after the build
and is **delay-loaded**: hotd.exe starts without it (only `--vr openxr` needs it, and says so when it is missing).
`tools/make_portable_hotd.ps1` puts it in `bin\`, its licence text in `licenses\OpenXR-LICENSE.txt` (a copy is
`docs/release/LICENSE-OpenXR-Apache-2.0.txt`; NOTICE.md and CREDITS list it) and `data\vr\vr.ini`.

**Runtimes.** Any OpenXR runtime set as the active one: **SteamVR** (Valve Index, HTC Vive, any SteamVR headset;
SteamVR > Settings > OpenXR > set as active), **Meta Quest Link / Air Link** (the Meta Quest Link app: Settings >
General > OpenXR Runtime), **Windows Mixed Reality** (the WMR / OpenXR Tools app; Windows 10 / 11 before 24H2),
**Virtual Desktop** (its streamer's VDXR runtime, or through SteamVR). Then `hotd --vr openxr` (the window shows the
left eye as a mirror). A headset run uses the OpenGL renderer whatever Video > Renderer says (main 80db1576; only an
explicit `--renderer angle` keeps ANGLE, which has no WGL context to hand to the runtime: the game says so and plays
flat).

**Extensions** (vrship, 2026-11-09): required only `XR_KHR_opengl_enable` (Android: `XR_KHR_opengl_es_enable` +
`XR_KHR_android_create_instance`); optional, enabled when the runtime lists it: `XR_EXT_hp_mixed_reality_controller`
(its bindings). Nothing else is assumed: no depth layer, no refresh-rate or color-space extension, LOCAL and VIEW
spaces only (every runtime has them), the format chosen from the runtime's list (SRGB8_ALPHA8, else RGBA8; SteamVR,
Meta and WMR all offer both for OpenGL). Each start logs the runtime's extensions (`openxr: extensions: ...`) and
swapchain formats (`openxr: swapchain formats: 0x8C43 ...`). Meta Horizon Link 1.208 on the development PC
(2026-11-09, `--vr-check`, no headset connected): 36 extensions with `XR_KHR_opengl_enable`,
`XR_FB_display_refresh_rate`, `XR_META_touch_controller_plus` (not enabled by the game: the Quest 3's controllers then
report as `oculus/touch_controller`); the bindings of all five core profiles accepted.

**When it cannot start** (no runtime installed / active, the headset off or asleep, the runtime without OpenGL, the
DirectX renderer, the DLL missing) the game logs one line with the reason (e.g. `vr: openxr: no headset connected
(or it is off / asleep) [xrGetSystem]`) and plays flat; headless `--render` runs too.

**Life cycle.** init: the runtime's extensions (a clear reason when there is none) -> `xrCreateInstance` (API 1.0) ->
`xrGetSystem` (HMD) -> `xrGetOpenGLGraphicsRequirementsKHR` (mandatory before the session; the context's version is
logged against it) -> `xrCreateSession` with `XrGraphicsBindingOpenGLWin32KHR` (the game's HDC / HGLRC) -> LOCAL
reference space (seated) -> actions -> two swapchains (the recommended size; **SRGB8_ALPHA8** preferred: the game
writes sRGB-encoded colours, `GL_FRAMEBUFFER_SRGB` stays off so they are stored as written and shown as the flat game
shows them; RGBA8 only when the runtime lacks it, logged: a paler picture) with an FBO + depth per image. Session
states: READY -> `xrBeginSession`, STOPPING -> `xrEndSession` (headset off / app hidden; READY again later), EXITING /
LOSS_PENDING, or any frame call returning `XR_ERROR_SESSION_LOST` / `XR_ERROR_INSTANCE_LOST` (the runtime crashed,
the headset unplugged; logged once) -> the game drops VR and goes on flat, its frame limiter and vsync back (vrflow
2026-11-10: EXITING / LOSS_PENDING, the runtime's Quit, close the game the normal way instead: `Backend::exitAsked`). Frames
(running): `xrWaitFrame` -> `xrBeginFrame` -> `xrLocateViews` -> (focused) `xrSyncActions` + the hands -> shouldRender
and located eyes: acquire + wait (1 s timeout: never a hang; a runtime stuck that long drops VR) the images, draw,
release -> `xrEndFrame` with one projection layer (none when not shown). Every `xrBeginFrame` gets its `xrEndFrame`.
Not running / not shown / an image not ready: the scene draws into the backend's own mirror target (its first 1024 x 1024 slot)
instead (the window keeps showing the game).

**Pacing** (2026-11-09). While a headset session exists the headset paces the game loop (`xrWaitFrame`: 72 / 90 /
120 Hz): the game's frame limiter, the window's vsync and the display beat of the flat game are off (they held a 90 Hz
headset at a 60 Hz monitor's rate: judder). While the headset shows nothing (not READY yet, taken off) the backend
waits ~11 ms a frame by itself (no busy loop). The menus are flat in the window and keep their limiter (vr.ini
`menu_panel = on`: on the big screen in VR, paced by the headset too). The game's ticks are on their own clock
(57.5 / 60 Hz) as always; Smooth Motion blends them at the headset's rate.

**Log** for the first test: `openxr: left hand: /interaction_profiles/...` / `right hand: ...` (the bindings the runtime
chose; `no controller` until one is on), `openxr: recentred ...`, `openxr: the runtime recentred the seat`, and once
the eyes' field of view / place / orientation and `the headset's frame period N ms` (2026-11-09).

### The desktop window

User 2026-11-09 ("is there a way to see the VR better on PC view"). vr.ini `desktop_view` sets what the game's window
shows while you play in VR (OpenXR; the preview keeps `--vr-eye`, spectator works there too):

| value | the window shows |
|---|---|
| `eye` (default) | the left eye (`--vr-eye right`: the right), filling the window: the eye's middle cropped to the window's shape, never stretched |
| `both` | both eyes side by side, each cropped to its half (`--vr-eye both` the same) |
| `spectator` | the normal flat game picture from the game's camera, the HUD over it and player 1's sight at the VR gun's aim, letterboxed to the game area's shape: for watching, streaming, recording |

The eyes are copied into the backend's mirror (two 1024-pixel slots, each eye's shape kept: it was squeezed into one
1024 x 1024 square, a 1852 x 2056 eye came out wider) and blitted to the window. Spectator draws the flat picture once
more (the cinema screen's code: `Scene::flatPicture`, at `hud_lines` lines; the eyes' "around" pieces are left out as
in the flat game) after the eyes: measured with `--vr-preview-test` (1600 x 800, 2200 frames) +0.5 ms a frame (1.41 ->
1.92 ms mean); in a world cut-scene it shows no bars (the HUD panel's, as the eyes), cinema cut-scenes show them.

### Testing without a headset (SteamVR's null driver)

2026-11-09, private/notes/vr_steamvr_null_test.md. SteamVR's built-in **null driver** is a virtual headset (no
controllers): `C:\...\Steam\config\steamvr.vrsettings` with `steamvr`: `"requireHmd": false, "forcedDriver": "null",
"activateMultipleDrivers": true`, `driver_null`: `"enable": true` (and `power`: `"turnOffScreensTimeout": 3600,
"pauseCompositorOnStandby": false`: without them SteamVR puts the motionless "headset" to standby after ~10 s and the
session drops to SYNCHRONIZED). Point only the game at SteamVR (the system's active runtime unchanged):
`XR_RUNTIME_JSON=<SteamVR>\steamxr_win64.json hotd --vr openxr --renderer gl`; the runtime starts SteamVR itself.

- The null driver reports its fixed head **rolled 180 degrees** (orientation 0 0 -1 0, the left eye at +x): drawn as
  told, the world was upside down in SteamVR's view and the game's window. The backend recognises the system
  (`SteamVR/OpenXR : null`) and levels that fixed pose at the first frame (`openxr: the seat turned for SteamVR's null
  driver`); a real headset is never turned.
- `--vr-head yaw,pitch,roll` (or `HOTD_XR_HEAD=yaw,pitch,roll`) turns the seat at the first frame with OpenXR too, so
  the fixed virtual head looks around (tests; +yaw left).
- `--shot-at N` in a VR run saves the window's picture (the `desktop_view`).

**Controls** (suggested bindings; runtimes remap other controllers):

| profile | shoot | reload | Start | pause | recentre (hold 1 s) |
|---|---|---|---|---|---|
| `khr/simple_controller` | select | (point the gun down), shake, flick up | - | menu | (the runtime's) |
| `oculus/touch_controller` (Quest, Rift) | trigger | grip, A, B, shake, flick up | X | left menu, left Y | a thumbstick press |
| `valve/index_controller` | trigger | grip, right A, right B, shake, flick up | left A | left B | a thumbstick press |
| `htc/vive_controller` | trigger | grip, right trackpad click, shake, flick up | left trackpad click | menu | (the runtime's) |
| `microsoft/motion_controller` (WMR) | trigger | grip, right trackpad click, shake, flick up | left trackpad click | menu | a thumbstick press |
| `hp/mixed_reality_controller` (Reverb G2; when the runtime has `XR_EXT_hp_mixed_reality_controller`) | trigger | grip, A, B, shake, flick up | X | menu, left Y | a thumbstick press |

(shake / flick up: gestures of the gun controller, on by default: "Shake to reload", "Wrist flick to reload" below.)

**Button prompts** (vrprompts, user 2026-11-10: "ReWrithe does not support button icons for any VR"). In a VR run
(`--vr openxr` / `preview`) every menu prompt names the VR controller's buttons, over Controls > Button Icons: the
Remake menus' caps (Accept: Trigger, Back: B, the menu button drawn as its three bars; the tabs' Left / Right prompts
are left out: the ray clicks the tabs), the classic menus' controls line ("Enter/A Choose    Esc/B Back" becomes
"Trigger Choose    B Back"), name entry, the pause's Settings pages. The style comes from the interaction profile the
runtime chose (`Backend::interactionProfile`, logged as `openxr: right hand: ...`; none yet / the preview: Touch's;
`HOTD_VR_PROFILE=touch|index|vive|wmr|hp|simple` for tests) through `pad_prompts.h vrStyleOfProfile` (`kPsVrTouch` ..)
and `keybinds.cpp padNames` (Touch / HP: Trigger, B, X, Y, Grip, Stick, Menu; Index: the pause is the left B, "B (L)";
Vive / WMR: back and pause on the menu button, Enter on the trackpad; simple: Select / Menu). The controls lines are
replaced after translation (`vrPromptLine`: a word ending "/A" -> choose, "/B" -> back, a lone "Enter" -> the
controller's Enter): all 33 `data/lang` files keep those key words, so no new strings. No pack in `data/ui/prompts`
(Xelu's, Kenney's) has VR controller icons: the caps are the game's own drawn ones (Button Style shows Simple, the
others greyed with "No Xelu icons for Meta Quest Touch (VR)"). The flat game is unchanged (`Frontend::vrStyle` 0).
Photo Mode and The Typing of the Dead 0 are not offered in VR; the in-game pause / Continue screens name no buttons.

**Pause** (vrflow, user 2026-11-10: "I don't know how to pause"): the left menu button, and on Touch / HP the **left
Y** too (OpenXR's `menu` action, "Pause" in SteamVR's binding page; Y was one of five reload buttons, the grip / A / B,
the gestures and pointing down still reload; with dual pistols the left gun reloads with its grip). SteamVR may keep
the left menu button for its dashboard: Y always reaches the game. The first time player 1 is in control in a game,
the HUD shows "Y / Menu: Pause" for 6 s (Index "Left B: Pause", Vive / WMR "Menu: Pause"; lang key `{buttons}:
Pause`, all 33 files). In the pause the hands stay; the gun points at Continue / Retry / Exit and the trigger
chooses; the pause button again = Continue.

### Main menu first

vrflow (user 2026-11-10: "it starts immediately in Arcade; I want it to show the main menu first", PC VR and Quest):

- A headset run's menus are on the big screen (vr.ini `menu_panel = auto` = on with OpenXR, PC and Quest; the
  preview keeps `auto` = off: its mouse is the cursor). The right controller is the pointer: a red ray and a dot,
  trigger = click, A / X = Enter, B / menu = Esc.
- The menus' attract (the boot's, "a key or a click: the main menu"): a trigger, A, X or the pause button = that key:
  the main menu, never a game (the VR guns' input, X's Start included, does not reach the attract's game any more).
  A button still held at the switch (the trigger that left the attract, the click on Arcade) is no new press on the
  other side (`Scene::lastPanel_`).
- Coin / Start: only through the menu (Arcade). The VR exe adds only `--vr openxr`, no `--auto-start`.
- What the game draws: menus / setup / end screen = the big screen, the ray and its dot (no hand models); play = the
  game's gun hand(s), laser, crosshair; the attract / cut-scenes = no hands, no ray; pause = the hands, laser and
  crosshair over the pause items. It never draws a controller model: a white controller is the runtime's own
  (SteamVR shows its room and the controllers' models whenever the app submits no frame).
- **Frame gaps**: every gap over 50 ms between two headset frames is logged `vr: frame gap N ms in <where>` (menu,
  setup, files page, end screen, loading, attract, game, pause); at the end `vr frame gaps: N over 50 ms (the worst
  ...)`. The game's load (enterGame) submits nothing while it loads: expect one gap "in loading" per game start.
- Preview tests: `HOTD_VR_BUTTONS="<frame>:<hand><button> ..."` (vr_preview.cpp: hand l / r, button t a b y m g)
  presses controller buttons for 4 preview frames.

The right hand is the gun (aim pose); a shot gives the right controller a short rumble (`/output/haptic`, 60 ms).
Pointing the gun steeply down also reloads (`reload_down`), and a shot off the screen does, as with the light gun.

### Shake to reload

User request 2026-11-09, like the shake games with a real light gun. `vr.ini shake_reload = on` (**on by default**
since user 2026-11-10; was off): a brisk jolt or a shake of the gun controller reloads (vrgest: one jolt is enough),
as the grip / A / B / pointing down do (it is the same `reload` the scene hands the game, so online play and rollback
see an ordinary reload). With dual pistols each controller's shake reloads its own gun (vr4: a magazine each). The
shake and the wrist flick (below) share one 0.7 s cooldown per gun (`vr_aim.h ReloadGestures`): one gesture = one
reload.

- **vrgest (user 2026-11-10, on a Quest 3)**: "the shake to reload was not great, or the flick. I expected the same
  ease as a DualSense controller." The DualSense's shake (`game/shake.h`) reloads on one jerk of the pad (its
  acceleration past 0.7 g, any axis, then the other way: a single jolt's start and stop) or one quick turn. The VR
  rule before needed the muzzle over 3.5 m/s twice, in opposite directions (two strict swings). Now, as on the pad,
  **one brisk jolt in any direction is enough**. What VR cannot take over from the pad: there the aim barely moves the
  pad, in VR it swings the whole controller (a fast aim move's muzzle accelerates at 10 g and more), so the jolt is
  measured at the grip and told apart from aiming by how little the gun turns, and a turn alone needs a way back.
- **Detector** (`vr_aim.h ShakeDetector`, per hand, from the pose per frame and the poses' own time stamps: dropped
  frames and uneven times are measured as they are; the runtime's velocity is not needed). Either of:
  - **a jolt**: the grip's acceleration (the second derivative of its position, between the frames' middles, smoothed
    over ~12 ms) at / over `shake_sensitivity` (**g**, default **2**, clamped 0.5..12; lower = easier; was m/s, 3.5),
    then within 0.25 s (35 ms or more after) at / over half of it the other way: the hand starting and stopping. Up,
    down, sideways, forward, a shake (many jolts): all count. Aiming moves the grip too (the arm swings the gun round
    the shoulder), so over the jolt the gun may turn at most its grip's travel / 0.7 m (radians: 8 degrees for 10 cm;
    an arm of 0.6 m or less always turns more), the grip must travel 3 cm or more and move its way on 2 frames or
    more (tracking noise, a one-frame jump);
  - **a twist** (the wrist, the grip in place): the gun turning at / over 350 deg/s x `shake_sensitivity` / 3 (233
    at 2 g) for 20 degrees or more, then within 0.15 s at / over 3/4 of that rate the other way for 15 degrees. One
    turn alone is aiming (a flick up has its own rule, below); fast sweeps across targets pause longer before turning
    back, a trembling hand and a recoil kick never sweep 20 degrees. Each way takes 2 frames or more and no frame
    carries 65 % of it or more: a real swing is spread over its frames, a jump is not (found by the preview runs: the
    preview's gun snaps to the next target in one frame and corrects a few degrees the next, its depth read a frame
    late: 1 stray reload per scripted run before this rule).
  The log says which: `vr: shake (jolt) reload (the right gun), frame N`, `shake (twist)`, `flick`.
  A grip speed over 6 m/s or a turn over 2500 deg/s in one frame is a tracking jump (the history starts again), as are
  an untracked pose and a gap over 0.1 s (a gap of 42 ms, two dropped frames at 72 Hz, is measured, not reset). After
  a shake: 0.7 s of quiet, so one shake = one reload (a 3 s shake: 4).
- **Test**: `--vr-preview-test`'s math part (`vr::selfTest`) feeds scripted pose sequences and prints each case's
  count (`vr gesture:` lines; `HOTD_VR_GESTURE_MARGINS=1` adds `vr gesture margin:` lines: the level at which each
  case's count would change). 90 Hz cases: steady turns, a slow wobble (1.5 Hz, 5 cm), a push from the first frame
  (its start unseen) give no reload; a 6 cm and a 12 cm shake, a jolt out and stop, a wrist shake give exactly one;
  two shakes a second apart give two; an untracked pose, a tracking hitch and one-frame jumps give none. Then the
  **Quest streams** (vrgest): every case at 72 and 90 Hz, three random streams each: frames dropped (1 in 12; 1 in 50
  two in a row), time stamps off by up to 1 ms, the pose trembling +-0.6 mm / +-0.15 degrees. None for: holding
  still, fast yaw sweeps (120 degrees in 0.15 s), fast diagonal moves, aim sweeps of the whole arm (50 degrees in
  0.15 s, the grip 0.45 / 0.6 m from the shoulder), small fast arm snaps (12 degrees in 80 ms, 0.6 m), aim snaps
  with an overshoot back, aim snaps in one frame and in two (the preview's gun), recoil kicks, a 15 Hz tremble, tracking jumps (40 degrees + 15 cm in a frame, and back),
  small 2 cm jumps, a slow wobble, walking. Exactly one for: a gentle shake (+-4 cm, 5 Hz), a gentle wrist shake
  (+-20 degrees, 4 Hz), a short flick (25 degrees in 70 ms), a jolt of 10 cm in 0.1 s down / up / sideways /
  forward. Margins at the defaults: the jolts still reload up to 3.0 - 3.3 g (1.5x the default), the gentle shake to
  4.1 g, the wrist shake to 4.3 g; of the cases that must not reload only the slow wobble depends on the level (it
  would at 1.46 g); the others are held by the turn, the timing and the sweep, at any level down to 0.5 g.
  (The preview's mouse gun runs the gestures too: a fast mouse wiggle can shake.)
- **Headset**: aim around quickly (no reload), then jolt the gun once, down or sideways, briskly (a reload). Too hard
  to trigger: lower `shake_sensitivity` (try 1.5); firing on fast aiming: raise it (try 3); `shake_reload = off`
  turns it off.

### Wrist flick to reload

User request 2026-11-10 ("shake to reload should be on by default as well as flicking the wrist"): the Wii light-gun
games' flick-up reload. `vr.ini flick_reload = on` (default on): one sharp upward snap of the gun's muzzle reloads that
gun (dual pistols: each gun only itself), through the same `reload` as the grip / shake (online / rollback see an
ordinary reload).

- **Detector** (`vr_aim.h FlickDetector`, per hand, from the grip pose per frame): the shake detector deliberately
  ignores a single flick (a lone fast swing looks like fast aiming), so the flick has its own rule, all of it:
  - the gun's **pitch rate** (its angular velocity about its own lateral axis, muzzle up, from the frame-to-frame
    rotation and the poses' time stamps) at / over `flick_sensitivity` (deg/s, default **300**, clamped 150..1500;
    lower = easier; vrgest 2026-11-10: was 600, clamped 300..) on **2 frames in a row** or more (frames, not a time:
    one long frame over a dropped one can be a tracking jump: found by the Quest streams below), the pitch rate at
    least **twice the yaw rate** there;
  - within the last **0.2 s** the muzzle's elevation rose by **20 degrees** or more (vrgest: was 30 in 0.15 s), its
    sideways turn over that stretch at most **half** the rise;
  - then within **0.35 s** (was 0.3) it stops or comes back (the pitch rate under a quarter of `flick_sensitivity`):
    the reload is then. Still turning up after 0.35 s: none;
  - a frame turning over 2500 deg/s is a tracking jump (dropped, the history starts again), as are an untracked pose
    and a gap over 0.1 s; after a flick (or a shake) 0.7 s of quiet for both gestures.
  Fast sweeps across targets are yaw-dominant, diagonal aim moves turn sideways about as much as they rise, recoil and
  a trembling hand never rise 20 degrees (15 and 16 in the tests), downward / sideways snaps and rolls never rise at
  all. Of the cases that must not reload none depends on `flick_sensitivity` (none reloads at any level down to
  100 deg/s): the rise, the yaw and the sideways limits hold them; the level is what tells a deliberate snap from a
  brisk raise of the aim (40 degrees in 0.3 s peaks at 200 deg/s).
- **Test** (`--vr-preview-test`'s math part, `vr::selfTest`, scripted 90 Hz poses, the shake on too): exactly one
  reload for a wrist flick (45 degrees in 80 ms) held up, one that comes back down, an arm flick (50 degrees in 0.1 s,
  the hand 0.25 m up); none for fast yaw sweeps (120 degrees in 0.15 s), fast diagonal moves (50 x 30 degrees in
  0.1 s, 30 x 40 in 80 ms), a downward snap, a snap down and a slow raise back, slow and brisk raises (60 degrees in
  0.6 s, 40 in 0.3 s), a sideways snap and a wrist roll, recoil kicks (15 degrees in 2 frames, 4 a second), a 15 Hz
  tremble of 8 degrees, an untracked pose, a tracking jump and a tracking hitch; two for two flicks 1 s apart; one for
  a shake right after a flick (the shared cooldown) and for the wrist shake; the shake cases give the same counts
  with the flick on. In the preview runs (the gun following the bot's aim; the log says `vr: flick reload (the right
  gun), frame N` for each gesture reload) both on: 0 gesture reloads in the plain 2200 frames, cinema 5000, dual 3000
  and `aim_depth=cpu` 5000, every count the same as with both off. Two fixes found there: the gestures' clock in
  `--vr-preview-test` is the game's frames (the preview's time is the wall clock and a headless run draws unevenly,
  as fast as it can: its gun looked several times faster than it moves; 14 stray shake reloads in a busy PC's dual
  run), and a shake's swing needs 2 frames (above). vrgest: the Quest streams (above, "Shake to reload") add a short
  flick (25 degrees in 70 ms: one reload in all six streams; it still reloads up to 359 deg/s, 1.2x the default) and
  the false-positive cases at 72 / 90 Hz with dropped frames and noise (all 0).
- **Headset**: aim around quickly across targets (no reload), then flick the gun's nose up sharply with the wrist, as
  with a Wii Zapper (a reload). Too hard: lower `flick_sensitivity` (try 220); reloads while aiming: raise it (try
  450); `flick_reload = off` turns it off.

### The Typing of the Dead 0 is not playable in VR

User 2026-11-09. With `--vr` (openxr or preview, also `--vr-preview-test`): More Modes > The Typing of the Dead 0 is
greyed, its help line "Not available in VR" (`Frontend::totdNoVr`; the string is in all 16 `data/lang` files);
`--totd` / `--totd-test` / `--totd-play` / `--typing` together with `--vr` are refused at start-up (exit 2, a message
on stderr and stdout) instead of dropping VR, so nobody is left in a flat game with a headset on; the menu's action
(feAction 48) is ignored in VR with a `hotd.log` line. The launcher / attract paths never start it (no other entry
exists). Without `--vr` TotD 0 is unchanged. Quick test: `hotd --vr preview --totd` and `hotd --vr-preview-test
--totd-test` must print "not available in VR" and exit 2.

### Single player only

User 2026-11-10: "I do not expect VR to support online. Only single player". With `--vr` (openxr or preview, also
`--vr-preview-test`): the main menu's Co-op (2 / 4 players, Online Co-op) is greyed, "Not available in VR"
(`Frontend::coopNoVr`); the Online page never opens (`hooks.online`); Survival / Horde / PC Mode do not ask the
players (`playersAsk`); `startGame` starts one player (`cfg.players` 1, no computer teammates, player 1's seat) and
sets `Runtime::vrSolo`, so another player's Start (a second pad / mouse / gun / key 2) never joins
(`Runtime::player_join`); `--host` / `--join` with `--vr` are refused at start-up (exit 2, "Online Co-op is not
available in VR"). Quick test: `hotd --vr preview --host 7777` must print it and exit 2.

## Packaging and launching

User 2026-11-09 (a player who has never used a game with a normal and a VR mode). VR is the same program, started
with `--vr openxr`; there is no separate VR build or download. The ways in:

- **The setup** (game/launcher.cpp Options page): **Create a VR shortcut** (off by default; Windows and Linux). With
  it the install makes `The House of the Dead - ReWrithe (VR)` beside the normal shortcut (Desktop / Start menu, as
  ticked; the Desktop when neither is), the same game exe with the arguments `--vr openxr`
  (`winshell::createShortcutTo`'s `args`: the .lnk's arguments, Linux: the desktop entry's `Exec=`). It is in the
  install's list, so the uninstaller removes it.
- **The VR exe** (vrship, 2026-11-09): `The House of the Dead ReWrithe VR.exe` (`util::kGameVrExeName`), made by
  `util::makeGameExe` beside `The House of the Dead ReWrithe.exe` (at the setup's files check, the install and
  `--make-game-exe`) as a copy of it: the `hotd_launch` stub with the PC game's icon. The stub looks at its own file
  name: one ending in ` VR` starts `bin\hotd.exe --vr openxr <its arguments>` (unless they name `--vr` already). The
  install lists it for the uninstaller and never copies one from a package (made fresh, as the other one).
- **The portable package** (tools/make_portable_hotd.ps1): the VR exe above once the setup ran; `Play in VR.bat`
  (`bin\hotd.exe --vr openxr`, extra arguments passed on) stays as the fallback; `VR Check.bat` (`--vr-check`, then
  a pause); README.txt "Play in VR".
- **The main menu** in a VR run: on the big screen in the headset ("Main menu first"; vr.ini `menu_panel = off` keeps
  it flat on the monitor, its controls line then says "VR: put the headset on when the game starts", `Frontend::vrNote`).
- **The player guide**: README.md "How to play in VR" (Meta Quest through Quest Link / Air Link with the Meta Quest
  Link app as the OpenXR runtime; SteamVR headsets; the renderer; the controls; comfort).

Not done (ideas): a VR item in the main menu that starts VR from a normal run (the OpenXR session needs the GL
context made for it at start-up; a restart with `--vr openxr` would do), a Steam shortcut with the VR arguments.

## VR check (`--vr-check`)

vrship, 2026-11-09 (src/vr/vr_check.cpp). `hotd --vr-check` (the portable package: `VR Check.bat`) starts no game: it
lists, in the console and `hotd.log`, every line prefixed `vr-check:`:

- the active runtime as the loader finds it: `XR_RUNTIME_JSON` when set, else the registry's
  `HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime` with its manifest's name and library; the runtimes registered under
  `AvailableRuntimes` (the ones a "set as active" button can pick); the API layers (add-ons such as OpenXR Toolkit:
  one can break VR);
- the runtime's extensions (and whether `XR_KHR_opengl_enable` is there), its name and version;
- the controller profiles whose bindings it accepts (the five core ones, the HP one when its extension is there);
- the headset (`xrGetSystem`: name, vendor, largest picture, tracking), the eyes' recommended / largest sizes, the
  OpenGL version range the runtime asks; then a session on a hidden window's OpenGL context, as the game makes one: the
  picture formats in the runtime's order and the one the game takes, the reference spaces, the refresh rates
  (`XR_FB_display_refresh_rate`, Meta);
- a last `RESULT:` line: `OK` or the step that failed and what to do. Exit code 0 OK, 1 no runtime / loader, 2 no
  headset, 3 a later step.

Run it once per runtime (Meta, then SteamVR: "SteamVR" below) and send both hotd.log files. Tested on the
development PC (Meta Horizon Link 1.208 active, no headset): the runtime, its 36 extensions and the five profiles
listed, `RESULT: no headset found (XR_ERROR_FORM_FACTOR_UNAVAILABLE)`, exit 2. The session part (formats, spaces,
rates) needs a headset: not run yet.

## SteamVR (and switching the runtime)

The game talks to **the one OpenXR runtime set active** on the PC (a Windows registry entry both apps write). With a
Meta Quest there are two ways to play on the PC: **Meta's** runtime (Meta Horizon Link: Link cable or Air Link) or
**SteamVR's** (the Quest streams from SteamVR through Valve's free **Steam Link** app on the headset, or through Link
with SteamVR running on top). The game's code is the same for both (OpenXR 1.0 core + `XR_KHR_opengl_enable`, which
both have); what differs is checked by `--vr-check`.

**Set up SteamVR with a Quest 3 (once):**

1. On the PC: install **Steam** (store.steampowered.com), sign in, then Library > Tools (or search) > **SteamVR** >
   Install.
2. On the headset: the **Store** (Meta Horizon Store) > search **Steam Link** (by Valve, free) > Get. The PC and the
   headset on the same network: the PC by cable to the router is best, the headset on 5 GHz Wi-Fi.
3. Start Steam on the PC. In the headset open **Steam Link** (App Library): it finds the PC; pick it, enter the code it
   shows into Steam on the PC if asked. SteamVR starts on the PC by itself and you see the SteamVR home.
4. Make SteamVR the OpenXR runtime: on the PC, SteamVR's small window > the menu (three lines, top left) > **Settings**
   > **OpenXR** (in the left list; not there: **Advanced Settings > Show** at the bottom of that list first; on older
   versions it is under Developer) > **"Set SteamVR as OpenXR runtime"** (Windows may ask for permission: Yes). The line above the button then says SteamVR is the current runtime.
5. Check it: `VR Check.bat` (or `bin\hotd.exe --vr-check`). Expected: `active runtime (registry): ...steamxr_win64.json`,
   `runtime: SteamVR/OpenXR ...`, the headset `... Quest 3 ...` (SteamVR may call it "Oculus Quest3" or similar), the
   formats with `SRGB8_ALPHA8`, `RESULT: OK`. Send hotd.log.
6. Play: `The House of the Dead ReWrithe VR.exe` (or `Play in VR.bat`) while Steam Link shows the SteamVR home. The
   right controller is the gun (`oculus/touch_controller` through SteamVR: the log says `openxr: right hand:
   /interaction_profiles/oculus/touch_controller`). The **left menu button** pauses; the right Meta button is the
   headset's own (Steam Link's menu). SteamVR's own dashboard (the game gets no input while it is open) sits on a
   button SteamVR chooses for the Touch controllers: if the left menu button opens that dashboard instead of the
   game's pause, say so (SteamVR > Settings > Controllers > Manage Controller Bindings can move it for this game; Y still
   pauses, X still starts).

**Back to Meta's runtime:** the **Meta Horizon Link** app on the PC > **Settings** > **General** > **OpenXR Runtime**
> **"Set Meta Horizon Link as active"** (the button only shows while another runtime is active). `--vr-check` then says
`oculus_openxr_64.json` / `Oculus ...` again. Switching back and forth is harmless and can be done any time with the
game closed; each app's button only changes that one registry entry (`HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime`).

**What the code does for SteamVR (audited 2026-11-09, no SteamVR on the development PC):** the instance asks only
`XR_KHR_opengl_enable` (SteamVR has it) plus the optional HP extension when listed; `xrGetOpenGLGraphicsRequirementsKHR`
is called before the session (SteamVR fails the session otherwise) and the context's version only logged against its
range; the binding is the game's own WGL context, current on the thread that renders and submits; formats: SRGB8_ALPHA8
(SteamVR's OpenGL list has it), RGBA8 as the fallback, with `GL_FRAMEBUFFER_SRGB` off so the colors are as on the
monitor; the swapchain usage is color attachment + sampled + transfer source (the window mirror's blit), no MSAA; the
action set's names are lowercase / unique and every action has a localized name (SteamVR's binding UI shows them);
suggested bindings for Touch (= a Quest through SteamVR), Index (system button not used; left B = pause), Vive (menu
buttons; the system button is SteamVR's), WMR and the HP G2; session states READY / STOPPING / EXITING / LOSS_PENDING
and `XR_ERROR_SESSION_LOST` handled (quitting from SteamVR's dashboard drops the game to flat, no crash); the
runtime's own recenter ("Reset seated position") arrives as `XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING` and is
followed. Known to watch on the first SteamVR run: the color (if pale or dark: `srgb_swapchain = off` in vr.ini and say
which was right), the gun's angle in the hand (SteamVR's Touch grip pose may differ from Meta's by a few degrees:
`gun_rotate`), smoothness over Wi-Fi (Steam Link's own bitrate / resolution settings).

## Testing with a Meta Quest 3: the three ways

For a first-time VR player; each way ends with "send settings\hotd.log". Before any of them: charge the headset and
the controllers, set up the headset as Meta's own setup asks (account, Guardian / play area), play seated.

**A. Quest Link (Meta's runtime, the PC renders; the simplest)**

1. On the PC: the **Meta Horizon Link** app (installed here) running and signed in; its Settings > General > OpenXR
   Runtime says Meta Horizon Link is active (else press "Set ... as active").
2. Connect the Quest 3 with a USB-C data cable (or Air Link: Quick Settings > Link > Air Link). In the headset: Quick
   Settings > **Link** > **Launch**; the grey Link home appears.
3. On the PC: `VR Check.bat` (you can look at the PC through the headset: the Link bar's monitor button). The last line
   should be `RESULT: OK`.
4. Start `The House of the Dead ReWrithe VR.exe`. The main menu is on a big screen in the headset (point with the right
   controller, trigger = click), then the game is all around you. Trigger = shoot; grip / A / B = reload; X =
   Start; left menu button or Y = pause; hold a thumbstick 1 s = recenter.
5. Check: smooth head turning, colors as on the monitor, the gun in your right hand pointing where the controller
   points, sound from the PC (the Link app's audio setting decides headset or PC speakers).

**B. SteamVR through Steam Link (Valve's runtime)**: the setup above ("SteamVR"), steps 1 to 6, then the same checks
as in A; afterwards switch back to Meta ("Back to Meta's runtime") so A works again (Link also runs SteamVR games
either way; only the OpenXR runtime entry decides which one this game talks to).

**C. Standalone APK (no PC while playing)**: docs/quest.md "First time with a Quest: step by step": Developer Mode
(free developer account, the phone app's switch), install the APK with SideQuest / the Developer Hub / adb, start it
once, copy `hotd.zip` and the PC game into the headset's game folder (Windows Explorer works), then the checklist
"First test on the headset". The menus are on a big screen in the headset there (the right controller is the mouse).
Its log: `settings\hotd.log` in that folder, or `adb logcat -s SDL SDL/APP`.

## First headset test

1. Install / start a runtime (above), headset on and awake (`--vr-check` says `RESULT: OK`).
2. `hotd --vr openxr` from a terminal (or add `--vr openxr` to a shortcut). The log should say `openxr: runtime ...`,
   `openxr: headset ...`, `openxr: session made (W x H per eye, sRGB)`, then session states 1, 2 (READY), 3 ... 5
   (FOCUSED) and `openxr: left hand / right hand: /interaction_profiles/...` (the controllers' bindings). Anything
   else: send the log.
3. In the headset: the world around you at the game camera; the HUD at the arcade screen's place, 3 m ahead.
4. The colours should match the flat game (too pale or too dark: the swapchain format; the log line says which).
5. The right controller: the game's gun hand, the barrel along the controller's pointing direction, the laser and
   the crosshair where it points; pull the trigger (a rumble, a shot where the crosshair is). If the gun sits wrong in
   the hand, tune `gun_offset` / `gun_rotate` in `data/vr/vr.ini` (and the left hand's).
6. Reload: grip or A / B; point down. Start: X. Pause: the left menu button or Y (Index: left B; Vive / WMR: menu).
7. Take the headset off / put it on (STOPPING -> READY: the picture comes back), open the runtime's dashboard (no
   shots go through it), quit from the dashboard (the game closes normally; a runtime crash: the game goes on flat).
8. Smoothness: the headset paces the frames (`xrWaitFrame`; the game's limiter and the window's VSync are off by
   themselves in VR, "Pacing"): turning the head should be smooth at the headset's rate. The window's mirror may tear
   (no vsync): normal.
9. Recentre: lean or turn the chair, hold one thumbstick down 2 s: the view snaps so the game's camera is where your
   head is (`openxr: recentred` in the log). The runtime's own recentre does the same.

## Known limits / left to do

- The game logic draws only what its camera needs: the world around you is filled from the neighbouring zones ("All
  around you"); creatures behind you are usually there but may pop (the game's own view tests, not changed).
- Full-screen fades and flashes are on the HUD panel only (the screen plane), not the whole view (world cut-scenes
  too; cinema shows them on its screen as the flat game does).
- With a headset (the Quest, and a PC since vrflow: vr.ini `menu_panel = auto`) every flat screen is shown on a big
  screen in VR, the right controller's ray as the mouse (Scene::panelFrame, docs/quest.md); the pause menu takes the
  VR gun's aim and trigger. `menu_panel = off` (a PC): the menus flat in the window, VR starts with the game.
- The game's load (enterGame: a chapter, the menus' attract) submits no headset frame while it runs: the runtime
  shows its own loading view meanwhile (logged as a frame gap "in loading", "Main menu first").
- Reading the eye's depth for the aim needs desktop OpenGL (OpenGL ES / ANGLE: the CPU ray test, "Aim depth").
- Players 2-4 keep their own inputs (mouse / pad / light gun); only player 1 is the VR gun (dual pistols: both VR guns).
- OpenXR, untested without a headset: the hands' fit on each controller, the colours (sRGB), the pacing (the
  2026-11-09 change: compiled, the start-up failure path tested, never run with a headset), the recentre and the
  profile log, the GL binding on each runtime (some ask for a newer OpenGL than the game's 3.3 core context: logged),
  Linux (the Xlib binding is written but not built). The aim uses the last frame's controller pose (one frame late).
