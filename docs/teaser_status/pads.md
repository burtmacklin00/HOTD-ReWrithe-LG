# Pads: gamepads, motion aiming, touchpad, light guns (worker "pads", 2026-10-19)

Branch `claude/pads`. ROADMAP "Controllers and light guns" plus the user's requests of the day (flawless controller
support, the DualSense in full, control styles). Neither original had gamepads; everything here is ours. The player
documentation is README.md "Controllers and light guns" (and the package README, tools/make_portable_hotd.ps1).

## Code

| File | What |
|---|---|
| src/game/pads.h / pads.cpp | `Pads`: SDL3 gamepads (open / close on hot-plug, per-player assignment in connection order, the five control styles, sticks, gyro in world / local space, ratchet / recentre, touchpad pointer / trackpad, triggers with a fire point, button maps, pause / menu keys, rumble, light bar, DualSense adaptive trigger) |
| src/game/lightguns.cpp | `LightGuns`: absolute pointer devices per player from Windows' raw input (main.cpp's WM_INPUT hook), relative mice for the mouse players meanwhile, F9 calibration |
| src/game/launcher.h / .cpp | `PadProfile` / `PadStyle` (per player, per style), `padFields()` (launcher.ini keys and the Controller page's rows), `p1Input`, `p2Input` 0-3, `gunCal` |
| src/game/pads_test.h / .cpp | the input test's pad / gun cases (frames 331-1170: 601-630 the 4-seat cases, 652-1166 the aim feel) |
| src/main.cpp | small hooks: `pads.refresh()` each loop; `pads.event` / `pads.menuKey` in the event loops; `pads.apply` + `lightGuns.apply` after the events; `pads.aim` every loop before the ticks (real time); the notice / calibration overlay; More Options rows and the Controller pages; `--p1-input`, `--p2-input`, `--p1-style`, `--p2-style` |

Netplay: a pad's / gun's start button goes through the lockstep (`onJoin` -> `netStart`), a lost pad shows the
message without pausing; the stick / gyro aim moves this PC's `guns[0]` as the mouse does.

## Design notes (what the user asked for, how)

- Latency: the gyro is integrated per sensor reading (its timestamps; the first reading / after a gap: the pad's data
  rate), consumed by the next game tick; no smoothing by default (Low / Medium / High smooth only rates below 1 / 2 /
  4 degrees/s, tiered: faster motion passes untouched); a 0.17 degrees/s dead zone and a slow drift estimate while the
  pad is still. Sticks: linear by default, radial dead zone (default 5 %) remapped from its edge (no jump, no lag
  outside it), no ramp unless a curve is chosen.
- World space: up = the accelerometer's gravity (low-passed); yaw = rotation about it; pitch = rotation about the
  horizontal axis across the pointing direction (forward -z). A rolled or tilted pad
  turns the crosshair the way the hand turns (tested: rolled 90 degrees). Local space: the pad's y (or gravity at the
  last recentre) and the same pitch construction.
- Styles: Classic / Motion only / Motion + stick / Touchpad (relpad 2026-10-30: the Sideways grip was removed; a
  Switch controller is the better gun grip); each has its own settings; the touchpad adds to every style.
- Hot-plug: SDL_GetGamepads each loop; a player's pad gone in play = the PC's focus-loss pause + a message; a new pad
  is assigned at once (its Start continues).
- Feedback: SDL_RumbleGamepad (hit / enemy hit = score up / reload, separate strengths), SDL_SetGamepadLED (player
  colours), SDL_SendGamepadEffect with SDL's DS5EffectsState_t layout (enable bits 0x04, right trigger effect 0x25
  "weapon": zones 2-5, strength 3 / 5 / 8 of 8); off (0x05) when the pad has no player.
- Light guns: while any player is "Light Gun" every mouse is read through raw input (absolute devices = guns, the
  others = mice for mouse players), so the shared desktop cursor is not used to aim. Absolute coordinates are mapped
  to the window through the virtual desktop / primary monitor and the window's client rectangle; (0, 0) = off screen.
  Calibration: two targets at 10 % / 90 % of the window, a per-axis linear fit. tools/calibrate_gun.ps1 is the old
  MAME tool (disabled since 2026-10-08), not used.

## Aim feel (worker "padfeel", 2026-10-20)

The user's first DualSense test: aiming "was not fun". Reviewed against public gyro / stick practice (Gyro Wiki,
JoyShockMapper, Steam Input) from the code (no hardware here). What was wrong and what changed:

- **The aim moved per game tick, not per rendered frame**: `Pads::tick` ran inside the 57.5 Hz tick loop with a fixed
  1/60 s, so on a 120 / 144 Hz display (Smooth motion) the pad's crosshair stepped at 57.5 Hz while the mouse's moved
  every frame, and the stick speed was tied to the tick count. Now `Pads::aim(guns, lw, lh, dt)` runs every loop before
  the ticks with the real elapsed time (clamped 0.1 s; the input test: 1/60 s a loop): the crosshair drawn and the shot
  of the following tick are the newest input's.
- **The drift estimate ate slow aiming**: it learnt whenever the rate was under 1.7 degrees/s, with a 0.2 s time
  constant at 250 Hz, i.e. a slow deliberate pan was absorbed into the bias within a fraction of a second (and drifted
  back afterwards). Now: "still" = the rates' 0.5 s mean below 1.5 degrees/s **and** their noise below 0.4 degrees/s
  **and** the accelerometer steady, for 1 s (a hand-held pad has tremor; a pad on a table does not), then the bias
  follows with a 1 s time constant. Plus **Calibrate Gyro** on the Aim Test page (2 s still; restarts on motion; no aim
  meanwhile).
- **Units**: sensitivity 1-10 = 0.5x .. 2.5x of "36 degrees of turn spans the window's width", the same pixels per
  degree vertically (Vertical scales it). Before: 80 .. 16 degrees per width (5 = 38), close but not a multiplier.
- **Gyro space**: new default **Player** (Gyro Wiki's player space: yaw = the turn about gravity of the pad's yaw and
  roll axes, its size up to their combined turn, relaxed x 1.41; pitch = local) so turning *or* rolling the wrist turns
  the crosshair naturally whatever the pad's tilt. World (yaw about gravity, pitch about the horizontal axis; for a pad
  rolled 90 degrees) and Local remain.
- **Gravity**: was the accelerometer low-passed (0.15 a reading: shaking and turning bent it). Now a complementary
  filter: carried by the gyro every reading, pulled to the accelerometer (time constant 0.5 s) only while it reads
  about 1 g; a large mismatch while the pad hardly turns (the start, a pad put down) takes the reading at once.
- **Every sensor reading** was already integrated with its timestamp (checked; SDL queues every report); kept, and now
  tested with 4 readings a frame.
- **Smoothing**: was off by default with a hard 0.17 degrees/s dead zone. Now tiered smoothing as JoyShockMapper does
  it (below thr / 2 smoothed, above thr 1:1, the smoothed part an exponential average of the remainder, so motion is
  delayed, never lost), default Low = 3 degrees/s (Medium 6, High 10); the hard dead zone is gone; an optional
  **tightening** dead zone (1-3 degrees/s, off) and **acceleration** (Low 0.8x -> 1.4x, High 0.6x -> 2x between 5 and
  60 degrees/s, off).
- **Sticks**: speeds per second; top speed 0.9 .. 4.8 screen heights/s (5 = 2.0; was 1.4); inner radial dead zone 8 %
  (0-25 %), **outer** dead zone (full speed from 92 %); curve default Medium = power 2 (Linear / 1.5 / 2 / 2.5); **Edge
  Turbo** (Classic's aiming stick held at full tilt 0.15 s: up to 1.5x / 2x over 0.4 s; never while precise); no
  smoothing. The fine stick uses the same curve.
- **Touchpad**: a 0.15 % dead band (the filtered point follows the finger only beyond it) for the pointer and the
  trackpad; the pointer maps the whole pad to the window with its outer 4.5 % off the window (both edges reachable).
- **Shake to Reload** (user addition; reworked by relpad 2026-10-30, `game/shake.h`): gyro styles, Off / Low / Medium
  (default) / High. Two short-window peak detectors, any axis: the turn rate past 7 / 5 / 4.5 rad/s within 0.1 s of
  the pad being slow (a wrist flick or roll), or the linear acceleration (accelerometer minus the fused gravity,
  smoothed over 15 ms against rumble) past 11 / 7 / 5 m/s^2 and then 70 % of it the other way 30-300 ms later (a jerk
  up / down, forward / back, side to side). Then a 0.4 s cooldown. A candidate holds the gyro's motion back (at most
  ~0.12 s): applied if no reload follows, dropped if one does, and 0.2 s after a reload too (the crosshair stays).
  Was: only a 1.8-3.5 g jolt and back, never while turning faster than 10 rad/s (so a wrist flick never counted).
- **Defaults per pad**: Auto style: DualSense / DualShock 4 / Switch Pro / Joy-Con = Motion + Stick, Xbox and others =
  Classic (unchanged); the tuned values are PadStyle's defaults. launcher.ini `padv=2`: older files get the new feel
  defaults (their speed / dead zone / curve / smoothing / space scales changed); buttons, grip, invert, touchpad kept.
- **Controller page**: Feel Preset (Precise / Balanced / Fast; Custom when changed), the new rows (Stick Edge Turbo,
  Gyro Tightening, Gyro Acceleration, Shake to Reload), **Aim Test** (live: gyro deg/s, the gyro's degrees per screen
  width, stick deflection -> output, crosshair speed in screens/s, the learnt drift, shake reloads seen, Calibrate
  Gyro), rebuilt every 0.15 s while shown.

## Key / button rebinding (worker "keybind", 2026-10-20)

- `src/game/keybinds.{h,cpp}`: per player (1-4) the keyboard's Aim Up / Down / Left / Right, Fire, Reload, Start, Pause
  (two scancodes each) and the pad's Fire, Reload, Start, Pause, Recentre, Gyro Hold (North's ratchet / tap), Precise Aim
  (a mask over South / East / West / North, L1 / R1 / L2 / R2, L3 / R3, Start, Back, Misc, the four paddles, the
  d-pad). The defaults are the old fixed keys; a pad is "Auto" (not custom: the style's own buttons, unchanged code
  path) until its first change, which starts from `Pads::defaultBinds` (the style's buttons as a table). The touchpad's
  click keeps its own setting (Touchpad Click); Guide is not bindable; the pause menu's South / East / d-pad and the
  menus' pad keys are fixed. Fixed keys: Esc (always the pause, and the capture's cancel), F1, F3, F9, F11, F12,
  the Windows keys, Print Screen.
- main.cpp: every key goes through `cfg.keys` (aim held per player, pause, start, fire, reload; players 3 / 4 only in a
  local 4P game as before); netplay takes the same guns / start bits, so online co-op follows the bindings.
- Menu: Options > Controls > **Keys & Buttons** (`menuBindRows`, `--frontend-screen 18`, HOTD_FE_PAGE 1 = the pad's
  page): Player (cycles 1-4), Status (the last result, the countdown, "also P2 Fire!"), the rows ("!" = used twice),
  Reset Keys / Reset Everyone / Reset Pad. `BindCapture` takes the next key / pad button / trigger (Esc, a click, the
  pad's East / Back while waiting for a key, or 5 s: cancelled); a key / button the row has is taken off, a new one
  added (keys: the second replaced when full).
- launcher.ini: `key1.fire=44,228` (scancodes), `padbind1.fire=r2,r1,south` (all seven actions once a pad is custom);
  written only where different from the defaults, so older files and untouched settings read and write as before.
- Test (`--input-test`): the menu walk opens Keys & Buttons, clicks Key: Fire, presses G (bound, saved in the game's
  settings), Reset Keys, Escape; `keybinds_test.{h,cpp}` (frames 1172-1200, 38 checks): the defaults, launcher.ini
  (nothing for the defaults, a round trip, Esc refused), the capture with synthetic events (bind, take off, Esc,
  time-out, F11 refused, a clash named, two keys at most, a click, a pad button / trigger / touchpad), then in play:
  P2 Fire moved to G, P1 Fire / Reload / Aim Right / Pause on K / L / O / U, P2 Start on 9, the old keys dead, the
  defaults back; a virtual Xbox pad with West = fire, South = reload, R2 unbound, a back paddle firing, Reset Pad.

## Tests

`hotd --input-test <file>` (HOTD_PREF_DIR = a scratch folder): **1467 checks, 0 failed** after padfeel (911 on main before it: the pad section now runs to frame 1170, so the per-frame "input task ran once" check adds 530, plus 25 aim-feel checks: multi-reading gyro, player space, tiered smoothing, Calibrate Gyro, drift learning, stick curve, frame-rate independence, edge turbo, shake reload / no reload on swings, touchpad dead band and edges, presets; 867 at the pads branch, 524 before it), also
with `--models pc --aspect 4:3`. SDL's virtual joysticks (SDL_AttachVirtualJoystick with a gamepad's 26 buttons and 6
axes, a gyro + accelerometer at 60 Hz, a touchpad) stand in for a DualSense (054C:0CE6: SDL reports it as PS5), an
Xbox pad (045E:0B12) and a Switch Pro (057E:2009); their Rumble / SetLED / SendEffect callbacks record what the game
sent. (SDL 3.2.10's virtual sensors keep one reading per update, `max_sensor_events` is never raised: the test calls
SDL_UpdateJoysticks after each reading.) The hidden window has no focus: the test sets
SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS (only the test).

Cases (frames 331-600, the second game, player 1 on the mouse): the DualSense assigned to player 2, its light bar blue,
the adaptive trigger's weapon effect (mode, zones, medium strength); Start in play = the pause, Start again = going on,
Back = the pause, East closes it without a reload; Classic: left stick half way for 10 ticks (exact distance, 5 % dead
zone), a stick inside the dead zone, R2 fire point / hysteresis / no double shot, East / West / L2 reload, the reload's
vibration, getting hit (strength), hitting an enemy (strength), the pause menu with the d-pad and Start, the right
stick's fine aim, left stick click = precise, invert Y; Motion only: yaw right (exact pixels), the stick ignored, pitch
up, world space with the pad rolled 90 degrees (turning right still = right), local space (the same motion = pitch);
Motion + stick: the ratchet (North held: no motion), a long hold = no recentre, right stick click = middle, the fine
stick, North tap = middle, the gyro pushed off the window + South = reload and recentre; the touchpad: a trackpad swipe
(exact), a tap = reload, the click = reload; Touchpad style: pointer position, tap = shoot (Touchpad Click: Shoot);
Motion + stick (relpad: was Sideways): raising the front = up, R1 / R2 = shot; Shoot Button L2: L2 = shot; hot-plug: the
DualSense unplugged in play (the pause, the message), an Xbox pad plugged in (player 2's, Classic: no gyro, the message
gone), its Start continues; two pads (a Switch Pro for player 1, red light; its stick moves player 1 only); light guns:
an absolute device's position, its trigger / right button / off-screen reload, a relative mouse moving player 1 by its
raw motion, the desktop cursor ignored, F9 calibration (two targets, no shots, the fit stored), the calibrated mapping.

`hotd --frames 40000 --autofire --mute --lives-kept 1` (and `--models pc`): chapter 1 ended at 16695 (unchanged).
`hotd --frontend-shot <png> --frontend-screen 4` draws a Controller page (HOTD_FE_PAGE=0-2 its pages).

## Needs a real device (nothing here was tried with hardware)

- Every real pad's mapping and type as SDL sees it (DualSense / DualShock 4 over USB and Bluetooth, DualShock 3 with
  its driver, Xbox One / Series, Switch Pro, Joy-Con pair and single); the player lights.
- The gyro's feel: sensitivity steps, the sign of yaw / pitch on each pad, world space on a real accelerometer (the
  gravity sign assumed: at rest +y reads +g), the drift estimate, the ratchet, a single Joy-Con's axes (SDL may
  already rotate them for the sideways hold).
- The DualSense: the adaptive trigger's weapon effect (bytes from the public DualSense documentation; SDL sends it
  only in its "enhanced" mode, which an application's effect request turns on), the light bar colour, rumble
  strengths, the touchpad's pointer mapping (1.1x with 5 % margins) and the trackpad's speed, tap timing (0.2 s).
- Hot-plug of real devices (Bluetooth sleep / wake), two real pads at once, a pad lost during netplay.
- Light guns: Sinden / AimTrak / GunCon 3 / DolphinBar really reporting MOUSE_MOVE_ABSOLUTE through raw input, the
  coordinate mapping on multi-monitor and high-DPI desktops, what each gun reports off screen, two guns at once, the
  calibration flow, and that WM_INPUT reaches the hook (the hidden test window gets none: the test feeds the
  devices directly).
- The More Options rows / Controller pages clicked through by a person (the page layout was checked with
  --frontend-shot).

## Pass 2: menus by pad, prompts, light-gun screen aids (worker "padpass", 2026-10-27)

- Prompts: `FrontendInput::padType` (SDL_GetGamepadTypeForID of the button's pad) -> `Frontend::padKey` turns the
  prompts' Xbox names into the pad's (keybinds.h `padInputName`); PlayStation face buttons drawn as shapes
  (`rPadSymbol`). The cut-scene viewer's hint line too (`SceneView::padType`). `HOTD_FE_PAD=xbox|ps5|switch` for
  `--frontend-shot`.
- Value bars: Left / Right step a bar row (volume 10, colour 16) and stop at the ends (frontend_remake.cpp).
- Firing Range: a player's pad's South / East are the menu's (were the pad's own); Tab = `Frontend::rangeKeys`: every
  player's Aim / Fire keys (menu.cfg.keys) aim / shoot there (`gRangeKeyAim`, `gRangeGuns`); the range also at 4:3.
- Light guns: `LaunchConfig::gunKind` (IR / Sinden / CRT), `gunFlash`, `gunBorder`, `calibrateNext`; `GunFlash` (pads.h)
  = the arcade's cycle (a pull: 4 frames, no restart meanwhile) kept by the host per tick; `drawGunAids` (main.cpp)
  clears white (scissor) over everything after the overlays: the whole window on a flash frame, else the border. The
  old `Runtime::shotFlash` 2D-layer path is no longer switched on (`--shot-flash` = gunflash 2).
- Test: input-test stage 8 (after PC Mode's seats): 47 steps by a virtual DualSense / Switch Pro; frames kFlash0..+6
  (after the edge cases): the flash cycle, the pixels of a flash / border frame (an FBO), Auto per type.

## Pass 3: button prompts per controller model, the pad cursor (worker "padglyphs", 2026-10-27)

**Which pad is which** (`src/game/pad_prompts.*`, `padStyleOf`): SDL 3.2.10's type first, then vendor / product id,
then the name (for a generic / STANDARD type). The "prompt style" is SDL's `SDL_GamepadType` or ours past it.

| Family | How it is told | Names (south / east / west / north, shoulders, Start / Back) | Icons (Xelu / Kenney) |
|---|---|---|---|
| Xbox 360 | SDL type | A B X Y, LB RB LT RT, Start / Back | Xelu 360 |
| Xbox One | SDL type | A B X Y, Menu / View | Xelu One |
| Xbox Series X\|S | 045E:0B12 / 0B13 / 0B20 / 0B21 or name | A B X Y, Menu / View, Share | Xelu Series |
| Xbox Elite 1 / 2 | 045E:02E3 / 0B00 / 0B05 / 0B22 | + paddles P1-P4 | Xelu One + Kenney paddles |
| PS3 / PS4 / PS5 | SDL type | Cross Circle Square Triangle, L1 R1 L2 R2, Start / Select, Options / Share, Options / Create | Xelu PS3 / PS4 / PS5 |
| DualSense Edge | 054C:0DF2 | + Back R / Back L / Fn R / Fn L | Xelu PS5 + Kenney back / Fn |
| Switch Pro, Joy-Con pair | SDL type | B A Y X (SDL south = B), L R ZL ZR, + / - | Xelu Switch |
| Joy-Con L / R alone (sideways) | SDL type | L: arrows (Left at the bottom); R: A X B Y; SL / SR shoulders | Xelu Switch + Kenney SL / SR |
| Switch 2 Pro, Joy-Con 2 | 057E:2069 / 2066 / 2067 (SDL 3.2.10 does not know them) | as Switch + C, GR / GL | Kenney Switch 2 |
| GameCube | 057E:0337 adapter, 057E:2073, name | A B X Y (SDL maps by label), Z, L / R triggers | Kenney GameCube |
| N64 / SNES (Switch Online) | 057E:2019 / 2017 | N64: A, B, C buttons, Z (SDL 3.2's map, unchecked); SNES: B A Y X | N64 none (drawn); SNES Xelu Switch letters |
| Wii U Pro | name (SDL's Wii driver) | B A Y X, L R ZL ZR | Xelu Wii U |
| Wii Remote (+ Nunchuk) | name | B A 1 2 (sideways), + / -, C / Z | Xelu Wii |
| Steam Controller / Steam Deck | 28DE:1102 / 1142 / 1205 | A B X Y; grips; L4 R4 L5 R5 | Xelu Steam / Steam Deck |
| Steam Input virtual pad | 28DE:11FF | the type Steam reports underneath, else Xbox | as that type |
| Generic / unknown | STANDARD / UNKNOWN | Xbox names (SDL's Xbox-like positions) | Xelu Series |

Not detectable here: none of these pads is on the development PC; every row is checked with SDL virtual pads of that
id / name (input test, 22 rows). SDL 3.2.10 reports Switch 2 pads and NSO GameCube only by id (no driver: they may not
work as gamepads at all until a newer SDL). The Wii Remote's IR camera is not exposed by SDL: use a Mayflash DolphinBar
in mouse mode (it is then a mouse / light gun, which already works); its accelerometer is not used for a cursor (too
imprecise). A Switch 2 Joy-Con's mouse mode works only if Windows / SDL sees it as a mouse.

**Settings** (Options > Controls): **Button Icons** Auto (each pad its own model's; the keyboard's after a key) / Xbox
Series / Xbox One / Xbox 360 / PS5 / PS4 / PS3 / Switch / Switch 2 / Wii / Wii U / GameCube / Steam Deck / Steam
Controller / Keyboard (`padprompts=` 0-14, global: the prompts are one screen's); **Button Style** Xelu (default) /
Kenney / Simple (`buttonstyle=`). Icons: `data/ui/prompts/xelu|kenney/<family>/` (only the used ones;
`hotd --list-prompt-icons` lists them), drawn by `Frontend::rPadGlyph` (box-filtered; Kenney's 128 px "double" files
past 64 screen pixels); a button with no icon in the style: Kenney's (Xelu style), then the drawn cap / shapes / text.
The cut-scene viewer's hint line stays text (the model's names).

**Pad cursor** (`src/game/menu_cursor.*`): in the menus, player 1's pad (or one not given to a player) moves the bone
hand: **Touchpad Cursor** On / Off (DualSense / DualShock 4: relative like a laptop's trackpad, accelerated; its click
= a left click), **Gyro Cursor** Off / While Holding ZL / L2 / LT (default) / Always (any pad SDL reports a gyro for:
Switch Pro, Joy-Con, DualSense, DualShock 4, Steam Deck / Controller), **Cursor Speed** 1-10. The hand shows as for the
mouse; the d-pad / stick hides it again. Steam Deck / Steam Controller trackpads: through Steam Input's mouse (SDL 3.2
exposes no touchpad for them). No recentre: the pointer is relative, like a mouse.

Tests: input test (+24: every family's names / style / icons with virtual pads, Steam Input, the settings; the cursor
from synthetic SDL events); `HOTD_CURSOR_TEST=1` (a virtual DualSense with touchpad + gyro on the real main menu:
touchpad +138 units, gyro +55, click; Remake and Classic, `--no-title` for Classic). Pictures: `HOTD_FE_PAD=<style>`,
`HOTD_FE_BUTTONS=xelu|kenney|simple`, `HOTD_SCENE_PROMPTS=<style>`; contact sheet
`wt-padglyphs\build\shots\padglyphs\contact_sheet.png`.

## Pass 4: the user's DualSense notes of 2026-10-30 (worker "padfix")

| Note | Change |
|---|---|
| "you can't shoot outside the screen, and if you do, the reticle gets reset to the centre" | Every style's crosshair can go 5 % of the window's height past its edges (`kMargin`; the sticks stopped at the edge before); a trigger pull there = the off-screen reload; the crosshair **stays** (the gyro's recentre after it is gone). Reload buttons unchanged. |
| "Pressing the left stick should centre the crosshair" | L3 let go within 0.15 s (`kClickNs`) = the middle; held longer it stays the precise aim. Custom bindings: the Precise Aim button's click does the same. R3 / North tap unchanged. |
| "The right stick should move a little faster ... a sensitivity option" | `kStickTop` 20 % faster (5 = 2.4 screen heights / s, was 2.0; the fine stick follows). **Controls > Aim Sensitivity: Player 1-4** (50-200 %, a bar; `PadProfile::aimSens`, launcher.ini `pad1.aimsens=` 5-20, also on the Controller page): x on every aim of the pad (sticks, gyro, trackpad). |
| "The sideways grip does not work ... 90 degrees either way" | (Removed again in pass 5.) Sideways = the pad turned 90 degrees, face up (or on its edge), the far end at the screen. The thumb's stick (right-handed: right, left-handed: left) fine-aims, turned 90 degrees for the face-up hold (`aim()`); the face buttons are taken by their place (`sideButton`: the one at the screen = North's job, the one at the player = South's ...; left-handed the d-pad too); the gyro's player space takes the pad's "up" axis (y on its edge, z face up) by gravity, so raising the far end = up and turning = left / right in both holds. Shoot: R2 / L2 by hand (unchanged); the pause menu keeps the printed buttons. |
| "Touchpad moves the bone hand only when Player 1 Input ... is Gamepad" | The menu cursor takes every pad's touchpad / gyro (was: player 1's or an unassigned pad; a DualSense given to player 2 did nothing); `MenuCursor` keeps each pad's finger / L2 / gyro time apart. |
| "There are no Xelu Switch 2 icons; if they don't exist the player should know" | `packHasIcons` / `packFor` (pad_prompts.h): Button Style greys the packs without icons for the pad shown now (Button Icons' model, else the pad used last / connected: `MenuHooks::promptStyleNow`), a grey choice cannot be picked (`Extra::choiceOff`, both menu styles), and the help line says what is drawn instead ("No Xelu icons for Switch 2 Pro: Kenney's are shown"). The drawing already fell back Xelu -> Kenney -> drawn caps per button. |

Tests (`--input-test`, SDL virtual DualSense, after the shot-flash frames, `PadTest::fixBefore / fixAfter`, 20 checks):
the stick past the right edge + R2 = reload, off the screen, the crosshair stays; L3 click = middle; Aim Sensitivity
200 % = twice the distance, the Controls row's list / bar; Sideways right-handed face up: raising the left end = up,
turning right = right, the right stick towards the screen = up, West tapped = middle, North = reload; left-handed:
the left stick, d-pad Left = reload, raising the right end = up; two pads' gyros moving the menu cursor; the packs per
family and Button Style's grey choices / help. Older cases updated: the gyro's off-screen reload no longer recentres,
the stick speeds x 1.2. `HOTD_CURSOR_TEST=2`: the menu cursor test with the pad given to player 2 (passed; the cursor tests now open only their virtual pad and get background events, so a real pad plugged in neither breaks them nor gets its light bar changed).

Needs the real pad: (the Sideways holds: removed in pass 5) (which way the user actually turns it; the face-up and on-edge holds are both
handled by gravity), whether the 5 % margin is enough / too much, the new stick speed.

## Pass 5: the user's final DualSense test, 2026-10-30 (worker "relpad")

- **The Sideways grip is gone** ("a Switch controller is the better alternative"): the style, its Grip row, the face
  buttons / stick turned with the pad (`sideButton`), the gyro's sideways axes, its translations and docs. The styles
  are now 1 Classic, 2 Motion only, 3 Motion + stick, 4 Touchpad (`kPadStyles` 4). launcher.ini `padv=3`; an older
  file's `pad1.style=4` (Sideways) loads as Motion + stick, `=5` (Touchpad) as 4, its `pad1.sideways.*` lines are
  ignored. `--p1-style sideways` = mix.
- **Shake to reload** reworked (above; "a quick jerk or flick of the wrist should reload", the pad on the lap).
  Offline test `hotd_shaketest` (synthetic 250 Hz gyro + accelerometer traces at Low / Medium / High):

  | case | Low | Medium | High |
  |---|---|---|---|
  | flick / jerk forward on the lap (1.0 g) | 0 | 1 | 1 |
  | wrist roll flick (350 deg/s) | 0 | 1 | 1 |
  | wrist pitch flick (300 deg/s) | 0 | 1 | 1 |
  | up / down, held flat (0.9 g) | 0 | 1 | 1 |
  | side to side (4 Hz, 0.8 g) | 0 | 1 | 1 |
  | hard shake (1.6 g, 500 deg/s) | 1 | 1 | 1 |
  | slow aim sweep (175 deg/s) | 0 | 0 | 0 |
  | fast aim snap (235 deg/s) | 0 | 0 | 0 |
  | tremor + noise while aiming (3 s) | 0 | 0 | 0 |
  | rumble (0.4 g of noise, 2 s) | 0 | 0 | 0 |
  | shaking 4 s at 5 Hz (cooldown 0.4 s) | | 10 | |

  `--input-test`: a jolt and one back = reload (the crosshair stays), an aim turn (3 rad/s, 0.3 g) = 1:1 and no
  reload, a wrist flick (11 rad/s) = reload with its turn dropped, the flick back = no second reload; old launcher.ini
  files (padv 2 / 3) and the saved file (padv=3, no sideways keys).

Needs the real pad: whether Medium is generous enough on the lap and never fires while aiming (Low / High otherwise).

## Per-input sensitivity (GitHub #36 follow-up, worker sens36, 2026-11-04)

Reporter after v1.2.0: the per-input sliders were still multiplied by the style's Sensitivity, Fine Aim Speed and the
player's Aim Sensitivity ("players can accidentally double the sensitivity"). Now each input's speed is its own and
nothing shared multiplies it (`PadStyle::stickSens / fineSens / gyroSens / touchSens`, %, 10-500, step 10, 100 % =
the old default feel; Controller page rows Stick / Fine Stick / Gyro / Trackpad Sensitivity, a bar each, per style:
Classic stick + fine stick + trackpad, Motion gyro + trackpad, Motion + stick gyro + fine stick + trackpad, Touchpad
trackpad). Removed: Sensitivity, Fine Aim Speed, Aim Sensitivity (Controller page) and Controls > Aim Sensitivity:
Player 1-4. Vertical / invert / curve / dead zone / smoothing and the precise aim (L3 held) stay.
The menu cursor (no stick cursor: the sticks move the selection): Controls > Touchpad Cursor Sensitivity / Gyro Cursor
Sensitivity (10-300 %, 100 % = the old Cursor Speed 5; launcher.ini `touchcursorsens=` / `gyrocursorsens=`).
launcher.ini `padv=4`, `pad1.<style>.aimstick= / aimfine= / aimgyro= / aimtouch=`. An older file (padv < 4, no new
keys) converts once with `legacyPadSpeeds` (the old formulas: kStickTop, kGyroSens, Fine x 10 %, x Aim / Stick / Gyro /
Trackpad Sensitivity), rounded to 1 %, clamped to 500 %. The old pad keys are not written again (an older build reads
its defaults); `cursorspeed= / gyrocursorspeed=` still are (the nearest 1-10). Presets set each style's own inputs with
the old preset's speeds, so an old Precise / Fast converts to the same preset.
Tests (fix frames 90-413): per (style, input) the other three at 250 % move it exactly as at 100 %, its own 200 % twice
as far (Classic stick / fine / trackpad, Motion + stick fine / gyro / trackpad); the menu cursor's two likewise; an
older launcher.ini converted (values and the converted profile's stick / gyro motion against the old formula).
