# VR mode

User request 2026-11-03: a VR mode "using the player hand and gun along with the crosshair shown". Status: the whole
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
- **Off the screen reloads**, as with the light gun: a pull while the ray's point is off the arcade screen, behind
  the camera, or with the gun pointed steeply down (`reload_down`, below `reload_pitch` degrees) is the off-screen
  shot. Pointing down alone (the gesture) also reloads once.
- **The crosshair** is the game's own sight picture (the one the HUD would draw for player 1, any Firing Range style,
  `Hud::vrSight`) on a quad at the ray's hit point, turned to the eye and scaled with the distance so it keeps the same
  angular size (`crosshair_degrees`); a faint **laser** runs from the muzzle to it (`laser`).
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

VR draws the arcade models with the **per-pixel depth** (`Camera::perPixelDepth`, the cut-scene viewer's Free Cam
setting) whatever Draw Depth says: Arcade Sort sorts each polygon by its distance to the eye, and two eyes 6 cm apart
could sort a pair differently: one eye would see a different polygon in front (stereo rivalry). It also makes the
depth buffer a real distance, which the aim reads.

## Comfort

- `follow = follow` (default): the reference frame takes the game camera's full orientation (the arcade's framing:
  when the game looks up at the Chariot, so do you). `follow = level`: only its position and heading; its pitch is not
  forced on you (look up yourself). In both the camera's motion is the game's: VR cannot change the rails.
- `vignette`: the view's edges darken while the reference frame turns faster than `vignette_turn` degrees / second
  (up to `vignette_strength`); it eases in fast and out slowly. Smooth Motion (the frame blending) applies to the eyes
  too, so the camera moves at the display's rate, not the game's 57.5 Hz.
- The HUD stays on the screen plane, never glued to the face.

Not done yet (ideas): a "cinema" option for cut-scenes (the flat picture on a big screen while the camera cuts
around), snap-turn is meaningless on rails, a seated height offset (OpenXR LOCAL puts the origin at the head).

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

## Dual pistols

Player request 2026-11-04: a gun in each hand. `vr.ini dual_pistols = on` (or `--vr-dual`); off by default (one gun).

- **The left gun** is the same gun hand `0x719` **mirrored** (model x -> -x: a left hand holding the handgun; there
  is no left hand with a gun in the game). The renderer turns the normals by the same matrix (`uRot * aNrm`), so the
  faces, their culling and the light stay right. It sits on the left aim pose as the right one sits on the right
  (`gunAnchorLeft`), moved by `gun_offset` / `gun_rotate` mirrored (x, yaw and roll the other way). The empty left
  hand `0x717` is not drawn then.
- **Each gun aims and fires for itself**: its own ray (from its muzzle, into the right eye's depth), laser, the game's
  sight at its hit, trigger (the same hysteresis), rumble on its own controller (`haptic(0 / 1)`), its own "pointed
  down" reload gesture. Either grip / B / Y, or a pull of either gun off the screen, reloads.
- **Into the game** (main.cpp `vrGunInput`): it stays one player with one magazine (the game's own; Survival's Double
  Pistols: its 16), the shots are player 1's. A pull of one gun is player 1's shot at that gun's aim (the shot flash
  there too). Both pulled in the same tick: the right gun's shot and the left gun's own ray beside it, as the Double
  Pistols' second gun (`port/game.h SecondGun`: after the seat's shot, `weapon_extra_shots` tests the left aim's ray,
  hits the nearest, its own shot flash; one round for the pair, as the Double Pistols take). Both pulled but one off
  the screen: the reload first, the other gun's pull in the next tick. Between shots player 1's aim is the right gun's.
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
- `--vr-preview-test --vr-dual` (dual pistols): both guns follow the game's aim from their own hands, both round trips
  are checked, and the VR guns are player 1's input (main.cpp `vrDualTestInput`): each `--autofire` pull goes in
  turn to the right trigger, the left one, both (the preview's scripted triggers, one VR frame held). Counted per gun:
  pulls, shots (a round gone), hits (`stat_hits`; the left ray beside a right shot: `SecondGun::hits`). Pass: the
  one-gun criteria, and each gun alone and both together shot, both guns hit, the left round trips' mean under 2.
  2200 frames: right 10 shots / 8 hits alone, left 10 / 8, both 11 shots with 9 + 8 hits; left mean error 0.28.
  With Survival's Double Pistols (`HOTD_WEAPONS_FORCE=3 --vr-preview-test --survival-test 2
  --survival-weapons-wave 1`): two VR guns by themselves, 27 hits each.
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
left eye as a mirror). Use the OpenGL renderer (Video > Renderer: OpenGL, or Auto on a card with OpenGL 3.3): the
DirectX renderer (ANGLE) has no WGL context to hand to the runtime; the game says so and plays flat.

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
LOSS_PENDING -> the game drops VR and goes on flat. Frames (running): `xrWaitFrame` -> `xrBeginFrame` ->
`xrLocateViews` -> (focused) `xrSyncActions` + the hands -> shouldRender and located eyes: acquire + wait (100 ms
timeout: never a hang) the images, draw, release -> `xrEndFrame` with one projection layer (none when not shown).
Every `xrBeginFrame` gets its `xrEndFrame`. Not running / not shown / an image not ready: the scene draws into the
backend's own 1024 x 1024 mirror target instead (the window keeps showing the game).

**Controls** (suggested bindings; runtimes remap other controllers):

| profile | shoot | reload | Start | pause |
|---|---|---|---|---|
| `khr/simple_controller` | select | (point the gun down) | - | menu |
| `oculus/touch_controller` (Quest, Rift) | trigger | grip, A, B, Y | X | left menu |
| `valve/index_controller` | trigger | grip, right A, right B | left A | left B |
| `htc/vive_controller` | trigger | grip, right trackpad click | left trackpad click | menu |
| `microsoft/motion_controller` (WMR) | trigger | grip, right trackpad click | left trackpad click | menu |

The right hand is the gun (aim pose); a shot gives the right controller a short rumble (`/output/haptic`, 60 ms).
Pointing the gun steeply down also reloads (`reload_down`), and a shot off the screen does, as with the light gun.

## First headset test

1. Install / start a runtime (above), headset on and awake; Video > Renderer: OpenGL.
2. `hotd --vr openxr` from a terminal (or add `--vr openxr` to a shortcut). The log should say `openxr: runtime ...`,
   `openxr: headset ...`, `openxr: session made (W x H per eye, sRGB)`, then session states 1, 2 (READY), 3 ... 5
   (FOCUSED). Anything else: send the log.
3. In the headset: the world around you at the game camera; the HUD at the arcade screen's place, 3 m ahead.
4. The colours should match the flat game (too pale or too dark: the swapchain format; the log line says which).
5. The right controller: the game's gun hand, the barrel along the controller's pointing direction, the laser and
   the crosshair where it points; pull the trigger (a rumble, a shot where the crosshair is). If the gun sits wrong in
   the hand, tune `gun_offset` / `gun_rotate` in `data/vr/vr.ini` (and the left hand's).
6. Reload: grip or A / B; point down. Start: X. Pause: the left menu button (Index: left B).
7. Take the headset off / put it on (STOPPING -> READY: the picture comes back), open the runtime's dashboard (no
   shots go through it), quit from the dashboard (the game goes on flat, no crash).
8. The window's VSync: the headset paces the frames (`xrWaitFrame`); if the headset stutters, turn VSync off in
   Video (the window's swap then does not wait on the monitor too).

## Known limits / left to do

- The game logic draws only what its camera needs: models the arcade never shows outside the 4:3 view may be
  missing when you turn your head far (the world's zones are widened as for widescreen; creatures behind you are
  usually there but may pop).
- Full-screen fades and flashes are on the HUD panel only (the screen plane), not the whole view.
- The menus / front end are flat (in the window); VR starts with the game. On the Quest (and on a PC with vr.ini
  `menu_panel = on`) every flat screen is shown on a big screen in VR, the right controller's ray as the mouse
  (Scene::panelFrame, docs/quest.md); the pause menu takes the VR gun's aim and trigger.
- Reading the eye's depth for the aim needs desktop OpenGL (ANGLE: the fixed `aim_distance` is used).
- Players 2-4 keep their own inputs (mouse / pad / light gun); only player 1 is the VR gun (dual pistols: both VR guns).
- OpenXR, untested without a headset: the hands' fit on each controller, the colours (sRGB), the pacing with the
  window's VSync, the GL binding on each runtime (some ask for a newer OpenGL than the game's 3.3 core context: logged),
  Linux (the Xlib binding is written but not built). The aim uses the last frame's controller pose (one frame late).
