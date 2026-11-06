# The Remake menu style (remakemenu, 2026-10-21)

Options > Video > **Menu Style**: **Classic PC** (the 1998 PC menu at 4:3) or **Remake** (the default since logo2; a wide menu laid
out like The House of the Dead: Remake, 2022, MegaPixel Studio / Forever Entertainment). Only the Remake's *layout and
behaviour* are copied. Every picture is ours: the player's own PC files (the logo sprite 0xD4, the hall 0xD1, the PC's
white 32 font, the bony hand 0xF9..), the arcade's own hit-mark blood (sprites 0x28..0x2D from the player's arcade ROM,
the splash shown when a player is hit; the PC's copies 0x47E..0x483 without the ROM) and procedural drawing (torn
panels, paper, handprints), plus
our own attract running live behind the menu (liveattract). No art, logo, font or sound of the Remake is used.

## 1. What the Remake's menus look like (research)

Sources: the user's screenshots of the Remake (main menu, Options > Quality; 2026-10-21), the Steam guide "Player's
Guide to the House of the Dead: Remake" (steamcommunity id 2801051962), gamepretty.com's controls / cheats guide,
reviews (Nintendo Insider, Rely on Horror, Light Gun Fanatics, Nintendo Everything's feature list), the Sinden wiki.
"(uncertain)" = not seen in a picture or a source.

**Main menu** (screenshot)
- Background: footage of the arcade demo (the user: "video footage of the arcade demo"), desaturated and dark, with a
  vignette and blood splatter / runs at the edges.
- The logo (THE HOUSE OF THE DEAD + "Remake") at the top centre, a figure beside it.
- Under the logo a dark torn / burnt panel, its bottom glowing red with blood.
- The items, centred, each on a torn off-white parchment strip with ragged ends, slightly offset and tilted; the text in
  red grungy capitals (small caps: the first letter taller). Items: **Continue, New Campaign, Statistics, Leaderboard,
  Gallery, Options, Exit**.
- Bottom right: **ACCEPT** with an Enter key icon over red claw slashes. Bottom centre: publisher logos and (c) SEGA.
- Highlight / hover effect and sounds: (uncertain; not visible in a still).
- New Campaign opens a set-up screen: difficulty (Easy, Normal, Hard, Arcade), scoring (Classic / Modern), 1 or 2
  players, the story or another mode (reviews).

**Options** (screenshot of the Quality tab)
- A wide dark torn panel across the screen, the red glow at its bottom; the background a dark red-tinted room.
- Tabs across its top, each a parchment strip: **Controls, Quality, Display, Sound, Gameplay, Language**; the current
  one raised with red text, the others dark text.
- Rows: label left in light grey grungy small caps; the value right with a down-chevron (choices: Display Resolution
  2560x1440, Full Screen Mode = Full Screen Window, Shadow Quality = High) or a white handprint for a switch
  (Anti-Aliasing, Bloom, Motion Blur, Ambient Occlusion). A small mark left of the row in focus.
- **Reset to defaults** centred under the rows.
- Prompts stacked at the bottom right: **Right (E), Left (Q), Back (Esc), Accept (Enter)**, each key in a box over red
  claw slashes.
- Settings named by the sources: controller type per player (keyboard / mouse for P1 / P2), crosshair sensitivity,
  automatic reload delay, reload sound, crosshair on / off and colours, shot flash colours, blood options (20 since a
  2022 update), language, HUD on / off, UI scaling, brightness, volumes, performance mode, gyro settings (Switch), the
  window mode (Quality > Full Screen Mode). Exact rows of Controls / Display / Sound / Gameplay: (uncertain).
- Controls cannot be rebound in the Remake (gamepretty).
- Transitions between screens: (uncertain).

## 2. Ours

One menu model, two layouts (game/frontend.h "the menu's two styles"): `mainMenuItems()` (the main menu and its
groups), `optionsRows()` (Options' rows) and the settings categories' `extras` are the same for both styles; Classic
lays them out as the PC did (frontend.cpp), Remake as below (frontend_remake.cpp).

| Remake | Ours |
|---|---|
| Arcade demo footage, grey, dark, vignette, blood | liveattract: the attract **running live** behind the menu (section 3), at the window's resolution, sharp; grey, dark, vignette, a faint Blood Color cast; the arcade's hit marks at the sides (remake2: a smear + splat left, the slash + splat right, a smear under the panel's red bottom). Without the arcade files: the PC's hall picture slowly drifting. Settings pages: tinted in the Blood Color |
| Logo top centre | The PC's own logo sprite 0xD4 (with the hanging figure), scaled |
| Torn panel, red glow | Procedural torn panel (noise edges, burnt rim, grey haze, red glow rising to a ragged bottom) |
| Items on parchment strips | Our items (Arcade, PC Mode, Boss Mode, More Modes, Extras, High Scores, Options, Quit; groups with their name in red and Back) on procedural paper strips, red small caps from the PC's white 32 font with worn holes. The one in focus is larger, glows red, slides right, a claw mark at its left |
| Tabs Controls / Quality / Display / Sound / Gameplay / Language | **Game** (Options' own rows: Difficulty, Life, Continues, Auto Reload, Blood Color, Sound Effects, Music, Controller Config) + our categories **Video, Audio, Controls, Gameplay, Mods, System**. Pages without tabs (Co-op, Keys & Buttons, a Controller page ...) show their title on one raised tab |
| Rows, chevron / handprint | Label left, value right with a down-chevron; On / Off values as a bloody handprint (Off: faint); "Open >" rows a ">" mark. The focused row: a red bar fading to the right with a red mark. More rows than fit (12): they scroll with the focus; chevron arrows above / below for the mouse |
| Reset to defaults | Not added (no per-page defaults in our settings yet) |
| Prompts bottom right | Right (E / RB), Left (Q / LB), Back (Esc / B; on the top menu Quit), Accept (Enter / A); a pad's buttons in rings once a pad is used; Back / Left / Right are clickable. remake2: a right-aligned column (18-unit margin, 26 a row), same-size key caps (30 x 18) with a crisp border and soft shadow, the arcade's claw mark 0x2A small behind; one row along the bottom when a settings panel leaves no room (3:2, 4:3) |
| (none) | The bony hand for the mouse (hidden while the keyboard / a pad moves the focus; galfix 2026-10-26: 90 % of the sprite, smooth, a soft shadow, both menu styles; see-through over the Gallery's model); our help line for the item in focus at the bottom |
| Language tab, other modes, Statistics | Not in ours |

**Keys**: Up / Down (held: repeat) move the focus; Enter / Space / A accept; Esc / B back (each page's Back; the top
menu: Quit, which asks "Are you sure?" with Yes / No strips); Left / Right change the focused row's value (Options'
rows step back and forth, the others cycle as a click); Q / E, Page Up / Down, LB / RB the tabs (Left / Right too when
no row has the focus).

**Size**: a Remake screen is 480 units high and as wide as the window (640 at 4:3, 768 at 16:10, 720 at 3:2, 854 at
16:9, up to 1200 for 2.5:1), drawn with 2 pixels a unit (1708 x 960 at 16:9) and scaled to the window. Resizing the
window lays it out again. The drawing is layered and cached (about 4-5 ms a frame on this PC; most frames only copy).

**The other pages** (remakepages, 2026-10-22; frontend_remake_pages.cpp, PC Mode / Jukebox in their own files): in
the Remake style these pages are Remake screens too (as wide as the window, the live attract + splatter behind,
prompts at the bottom right, the help line), with the Classic page's items, texts and actions:

| Page | Remake layout |
|---|---|
| Chapter Select (9) | Four cards across the width (torn panel with the red glow, the PC's boss portrait 0xE3.. (lit: 0xE8..), "Chapter N" on a strip, the boss under it), The Ending strip (no action) with its line, Exit. Untinted backdrop |
| Boss Mode (7) | Five cards (the four bosses and All Bosses, 0xE3..0xE7, lit +5), named by the string table ("Chariot Boss" ..), Exit. Untinted |
| PC Mode (16) | The PC's tombstones 0xA4 (the original's identity) centred, cut to the two stones (an arch + straight sides, soft edges, the bottom fading): the panels' places as the PC's, texts in the Remake letters, the arrows as chevrons, Start / Exit as strips; 4 players: the seats' coloured frames, Ready as a small strip. Untinted |
| High Scores (8) / a boss's times (32-36) | The table on a dark torn panel, its name on a raised tab with chevrons beside it; the tables / bosses turn with Left / Right, Q / E or the Left / Right prompts; the row just entered on a red bar; PC Mode's table with the character's name and picture. Settings tint |
| Jukebox (20) | The lists as parchment tabs (Q / E; Left / Right are the host's), the rows on a torn panel (the chosen one on a red bar, page chevrons beside), the "now playing" box torn with a blood-red bar, the buttons and volumes as strips; Page Up / Down still turn its pages. Settings tint |
| Gallery (17) / Cut-scenes (19) | regallery (2026-10-22, user: "look like a Resident Evil gallery"): its own page in the manner of the Resident Evil 2 / 3 / 4 remakes' and Requiem's galleries (`gallery_re.cpp`; layout and feel only, our drawing and the PC font): no live attract, no splatter; the host draws the Gallery's stage under the page (`Gallery::reStage`: a soft wall light and a floor pool round the feet, the shadow, the model mirrored in the floor and faded, the panels' shade and the vignette; `RemakeLayers::noBackdrop`). Left: "Gallery", a red rule, the categories (the current on a red bar), the category's entries in a scrolling list. Right: the file (name in large outlined caps, rule, category + n / N, the description, labelled stat rows from the facts, the weak-point card) and the Viewer rows (Motion, Model, Light, Background, Layout; a click steps them). Bottom: key caps (Entry, Category, Rotate, Zoom, Pan, Light, Play / Pause, Next Motion, Hide UI, Background, Back; a pad's buttons after a pad is used), one row or two balanced rows. The file and the list are `RemakeLayers::anim` parts: a new entry / category cross-fades and slides them (0.26 s), the model fades in and slides from the right (0.28 s). T: the full view (the name and the keys only, faded out by the host). The Cut-scenes page: the scenes by chapter in the middle, the scene's file with Players / Models / Clean View / Subtitles rows, Play and Attract Demo. K: Remake -> portraits (gallery3) -> the Classic page over the backdrop (gallerylayout=0 / 1 / 2) |

The arrows on Chapter Select, Boss Mode and PC Mode move the focus to the nearest item that way (`rGrid_`). Not
changed: the name entry and the secret screens (PC layout in both styles). Classic is byte-identical (the
`--frontend-shot` pictures of every page before / after).

A settings page's list goes round (relmenu, 2026-10-30): Down on its last row scrolls it to the top and focuses its
first row, Up on the first row the last one (before: the first row still shown, mid-list). The Game tab also has Free
Play under Continues (the Classic Options screen: Continues 0 .. 9, then "Free").

**Text** (remake2): the red item text, the tabs' red, the rows, the prompts and the help line have a dark outline
(0.75 units, ~1.7 pixels at 1080 lines) and a soft drop shadow (rText's fx); the paper strips cast a soft shadow.

**Testing**: `HOTD_FE_STYLE=remake` (or `--menu-style remake`) with `--frontend-shot` draws a Remake screen;
`HOTD_FE_FOCUS=n` moves the focus down n times, `HOTD_FE_MS=<ms>` sets its clock (the hall's drift; this PNG tool
has no live attract: the hall), `HOTD_FE_BENCH=n` times n frames, `HOTD_FE_BLOOD=N|RRGGBB` sets the blood colour (the
splatter: docs/blood_splatter.md). `HOTD_MENU_SHOT=n HOTD_MENU_SHOT_PNG=<file>` (a hidden window) pictures the real
menu n frames after the splash (the live attract one tick a frame; `HOTD_LIVE_SKIP=N` starts it N frames on: another
scene). The input test walks the Remake style by keys and clicks (21 checks), checks the live attract (running, drawn,
muted, no shot or start reached it), the Gallery's Attract Demo in both styles and the Remake boot.

## 3. The live background (liveattract, 2026-10-21)

User: "if they can run the game, they can run the background". The Remake screens' backdrop is the arcade's attract
running live, not a recording (the old `menu_footage.bin` is gone; an old file in the settings folder is ignored).

- **How**: main.cpp's `liveFrame` gives the menu its own game runtime (`Host`, loaded on a worker) and its own
  `GameView` (the normal renderer; prepared on a worker, its GL part on the menu's thread). The runtime starts the
  attract (`attract_begin`) and is ticked at the game speed in real time (at most 4 ticks a frame); each new frame is
  drawn by `GameView::draw` into an offscreen texture at the window's size. `Hud::drawRemake`'s backdrop shader
  samples that texture (no copy to the CPU) and makes it grey / dark, tints it (Blood Color; the settings pages
  stronger) and adds the vignette. The user (reference picture of the Remake's menu): little or no blur, so it is
  sampled 1:1 (cut to the screen's shape when the window is narrower than 4:3 or wider than 2.5:1).
- **Only the 3D**: the HUD is never drawn for it, so no logos, PRESS START, FREE PLAY, captions, scores, title cards,
  the FBI screen (game mode 1 is off for it) or white fades (those are the HUD's). Frames where the world draws
  nothing (the story's first black seconds) are drawn into a second texture and not shown: the last 3D frame stays.
- **Muted, no input**: its runtime has no sound device; no gun / start input is ever given to it (the menu's keys,
  clicks and pads only reach the menu; the input test checks its shot / start counts stay 0).
- **Loops**: the attract goes on by itself: story (mode 9), title (2), how to play (8), the demo (3; its demo
  index moves on each round), the ranking room (6), and again.
- **Follows the settings**: Options > Models (arcade / PC) and Blood Color: a change loads it again on a worker (the
  old picture stays until the new one draws).
- **When**: only in the Remake style and only while a Remake screen shows (main menu, Quit, Options and its pages);
  on the PC-layout pages (Gallery, High Scores ...) it holds where it is. Any game (Arcade, Chapter Select, Boss Mode,
  PC Mode, online, the cut-scene viewer, an attract) frees it first: one game view at a time. Classic never runs it.
- **Fallback**: no arcade files (or it cannot load): the hall picture (0xD1) as before. `HOTD_NO_LIVE_ATTRACT=1`
  turns it off (developer).
- **Live Background** (menuperf, 2026-10-26; Options > Video, `livebg=` 0 Auto / 1 On / 2 Off, `--live-bg
  auto|on|off`): Off shows the still (the story's first shot, saved once; the hall without one) and runs nothing.
  Auto is off on integrated / software graphics (Intel UHD / Iris / Core Ultra's Arc, AMD APUs and the Steam Deck,
  llvmpipe: `weakGpu` in main.cpp; `HOTD_GPU_CLASS=weak|strong` to test) and turns off for the run when the menu's
  GPU time stays over 10 ms a frame or a third of the frames are late (3 s windows). On weak graphics, or a GPU over 6
  ms a frame, the attract draws every other tick (30 pictures a second) and at most 1080 lines (scaled up).
  docs/performance.md "Menus".
- **Cost** (hidden window, 600 frames, `HOTD_MENU_PERF=630`, this PC): see the table in docs/changelog/2026-10-21.md
  "Live attract". The live part costs 0.1-0.25 ms CPU a frame; CPU + GPU of the whole menu frame 0.35-0.8 ms (0.2 ms
  without it). A change of attract part (title -> how to play, PC models) loads its scenery once: one ~26 ms frame,
  as the attract itself has in the game.
- **Developer**: `HOTD_LIVE_DUMP=<dir>[,N]` writes every N-th live picture (game mode in the name),
  `HOTD_LIVE_TICKS=N` N ticks a frame in the automated runs, `HOTD_LIVE_SKIP=N`.

**Boot (Remake style)**: the SEGA logo (3 s or a key; no title art), then the attract full screen with its sound from
the story (the "zombie intro", mode 9) through the title, how to play, the demo and the ranking, then the main menu
by itself (a key earlier: the menu at once). No 30 s idle attract in the Remake style (the background is the attract);
the 5 s idle fade stays. **Classic**: unchanged (SEGA logo, title art, the attract until a key, the idle attract;
its attract and menu pictures are identical before / after this change).

**Attract Demo**: the Gallery's Cut-scenes page (Gallery, then C or the last category) has **Attract Demo**, in both
styles: the original attract full screen with sound, untouched (2D, flashes and all, as the boot's); any key, click
or pad button goes back to the Cut-scenes page.

## 4. Fixes after the user's play (remakefix, 2026-10-22)

- **Start-up backdrop** ("the background does not load quickly enough so it is stuck there"): the stretched, blocky
  pink frame was the live attract's own first frames: the story (mode 9) draws one frame of Chapter 1's world from the
  story's camera, then nothing for about 3.5 s, and that frame was held. Now: a frame is shown only from a part's 12th
  frame and the story's 206th (its desk / telephone shot); the frames before tick 8 at a time (not drawn on screen), so
  the desk comes about 0.4 s after the load. Until then: a **still** of that very frame (`menu_backdrop_<arcade|pc>_<blood>.png`
  in the settings folder, written once by the first run and again when the window size changes), else the dark fill
  (never the hall). The live picture **cross-fades** in (0.5 s; over the still it is the same picture). A camera cut,
  another attract part or a gap also cross-fade (0.5 s, eased): three textures (shown, drawn into, faded from). The
  hall picture is left only for when the live attract cannot run (no arcade files).
- **Idle fade** ("does not fade back smoothly"): out after 5 s idle in 0.6 s, back in 0.45 s on any input, both eased
  (smoothstep); every layer together (backdrop lighter, panel, logo, wordmark, splatter, prompts, help, hand). The input
  that wakes it does nothing else while the menu is more than half gone; a mouse move still moves the hand.
- **Quit** keeps the main menu's backdrop tint (it had the settings pages' stronger red).
- **The hand vanishing on clicks**: the host counts a mouse button as "the keyboard" (the prompts' keys), and a press
  with the keyboard flag hid the hand. Now only a key's / pad button's press or a key move hides it; a click never; any
  mouse move shows it.
- **Dropdowns**: a settings row whose value is a short choice (2-8 values: Models, Aspect, Menu Style, Display, CRT
  Filter, Crosshair Size / Thickness, Sound, the Player inputs, Blood Color, Sonic Mode; Game tab: Difficulty, Blood
  Color) opens a small list under its value (above it near the bottom) on a click / Enter / A: a dark torn panel, the
  values in outlined text, the current one red with a red diamond, the focus as a row's red bar. Up / Down or the
  mouse move, a click / Enter picks (the row's own click steps to it: the same save and hooks), Esc / B / Back or a
  click outside closes (that click does nothing else). Left / Right still step the value on every row; longer lists
  (Frame Rate, Crosshair, Menu Music, Lives ...) keep stepping. Classic is unchanged (`Extra::choices`, `Extra::pick`).
- **Sounds**: the PC menu's only sound is its click (cue 28, GAMESNDS sample 13); the Remake style plays it on a focus
  move (Up / Down, a scroll), Q / E / Page Up / Down / LB / RB and Left / Right on the tabs, Left / Right on a row, Esc /
  B (back), a dropdown's open / pick (as a click) and close.
- **Display** (Options > Video, above Resolution): the monitors (SDL_GetDisplays: name and desktop size), applied at
  once (a window centred on it; fullscreen, borderless or a display mode, moves with it), launcher.ini `display=` (the
  display's name, " #2" for a second one of the same name; missing: the primary), used at start-up before the
  fullscreen. Resolution lists the modes / window sizes of the display the window is on.
- **Start-up without the text launcher** (coordinator, "hide the debug menu at the start"): with both game paths known
  (launcher.ini, the package's data or the command line; any other setting missing: defaults) the launcher draws
  nothing (black) while the files check runs, then the boot. Only a failed check shows it; a player's run shows a
  short "Find your game files" page (the two folders, Start, Quit, All Settings). Options > System > Files Check still
  opens the whole page. F1's developer lines need `--dev-lines` or `--dev-keys` in a player's run.

Testing: `HOTD_MENU_SEQ=<dir>,<from>,<to>,<every>` writes the menu's frames (the idle fade); the input test's Remake
walk adds the dropdown by keys and mouse, the hand after clicks, the tabs' sound and Quit's tint.

## 5. The players' quick choice (playerpick, 2026-10-26)

User: "It's annoying that players have to go into Options to select players just to play 4P Arcade, PC Mode, Boss
Mode, etc." Now every local game start asks first, on one small page (screen 22, `Frontend::askPlayers`):
Arcade (also with a Sonic Mode preset on: its name is in the title), Chapter Select after the chapter, Boss Mode after
the boss, PC Mode before its panels (its four panels follow the count). Co-op's Local 2 / 4 Players start at once as
before; online never asks (the session's seats decide).

- **Players 1 2 3 4** in one big row, the chosen one marked (Remake: a lit parchment strip in a red glow; Classic:
  the PC's white 32 font, the chosen number yellow). 3 / 4 show one small line "Experimental: the 4 players mode".
- One **Player N: Human / CPU** button per seat past player 1; CPU uses that seat's level from Options > Co-op
  (Easy / Normal / Hard; Normal when it was never set). Shown as "Player 3: CPU Normal".
- **Start** has the focus / the hand when the page opens, so one press (Enter, A, or a click without moving the mouse)
  plays with the last choice. **Back** (Esc / B) returns to the page before with its focus where it was; nothing is kept.
- Keys: 1-4 pick the count, Q / E (pad LB / RB) one less / more; both put the focus back on Start. Arrows / d-pad
  move between the buttons (Remake), the hand as usual (Classic).
- Remembered: Start writes `players=` and `p2bot=` .. `p4bot=` in launcher.ini (the same settings Options > Co-op
  shows). 1 player also turns player 2's CPU off (that CPU plays whenever it is set); players 3 / 4's seats keep
  their setting below 3 players (unused there).
- Pictures: `--frontend-screen 22` (HOTD_FE_PAGE = the players, HOTD_FE_STYLE=remake).

Input test steps: Remake (Chapter 2 and the Hanged Man: Enter opens it, Esc comes back on the same card; PC Mode: the
last choice on Start, Up / Enter on a seat, Up / Enter on 1, the 3 key, Q, Esc keeps nothing, then a single Enter
opens PC Mode with 2 players kept), Classic by a pad (RB, B, LB / RB / A) and the mouse (Arcade, Esc, Arcade, one
click starts the game).

## Comparison pictures (fix28, 2026-10-27)

Video > Models / Aspect and Gameplay > Blood Color show (depthopt, 10-27: the Draw Depth row and its pictures are gone), while the row has the focus, a small panel of
the game's own pictures of each choice (up to 3 side by side, the current one framed red; Blood Color: the current
one). `Frontend::Extra::preview(k)` gives choice k's PNG (menu_pages.cpp `previewed`, `previewSpec`: Chapter 1 with the
autofire, frame 2300; the blood frame 1450). main.cpp `PreviewMaker` makes them on demand: one hidden copy of the game
at a time (`--frames F+1 --autofire --render --size 480x270 --shot-at F --preview-run` + the value's option), below
normal priority, written to `<settings>\previews\<name>.part\` and renamed when whole; the page polls it 3 times a
second (`Frontend::previewGen`) and draws the pictures when they arrive (no stall; ~2 s a picture, 10 in ~22 s; 12 before depthopt). The
custom blood colour's picture is named by the colour. Classic style: none (not done). Testing: `hotd --preview-pics
<folder>` makes all 10 and exits; `HOTD_FE_PREVIEWS=<folder>` with `--frontend-shot` draws them
(`HOTD_FE_STYLE=remake HOTD_FE_FOCUS=1 --frontend-screen 3`: Aspect).

fix28b (user: "it has to be noticeable"): Models: Chapter 1 frame 1150, cut round the rescued researcher's face;
Draw Depth: Chapter 3 frame 2745 (`--chapter 3`), the grey wall after the first door where the Depth Buffer still
shows the brown door through it (depthfix's ch3_wall, checked on this build), cut to it with a red ring; Aspect: the
bars hatched grey and the picture edged red. `PreviewSpec` crop / ring / bars, applied by the maker after the run.
The pictures are kept in `previews2\` now (the first ones, in `previews\`, are not used).

fix28c (user: "get rid of the red circle, none of that"): no ring and no red edge on any picture (`PreviewSpec` has
crop / bars only; Aspect keeps the grey hatching); kept in `previews3\` so the ringed ones are never shown. Draw
Depth stays on the Chapter 3 wall: the lava hall (depthcmp ch3_lava, `--chapter 3 --strict --default-route` frame
22280; `--chapter 3` alone 15440) was checked on this build and Arcade Sort / Depth Buffer both draw the lava pop
there now; only a few dithered shadow pixels differ, nothing you would see in a small picture.

### menus3 (2026-10-28): the pictures from the attract, made in-process

The user: the old pictures were "horrible"; use "the two axe zombie and the researcher running away from it from the
attract demo"; "loading another instance of the game is not smart"; a crosshair in them. Now:

- **Two moments of the game** (`game/preview_art.h`). Gameplay > Blood Color: the attract story's frame 1065
  (`kPreviewFrame`), the two-axe zombie in the doorway and the two researchers running from him, with a hit's blood
  burst on the zombie in that colour. Video > Models (prevbridge, user 10-28: "the Chapter 1 bridge where the strong
  zombie carries the researcher"): Chapter 1's frame 4320 (`kPreviewBridgeFrame`, counted from the chapter's start),
  the big zombie on the bridge with the researcher over his head and the mansion's entrance behind, shown with each
  model set (the whole 16:9 frame). Player 1's crosshair (your Crosshair setting) is on the zombie in every picture.
  **Aspect has no pictures now.**
- **Made by the game itself, in the same program** (`main.cpp prvFrame`): no second game process. At the menu's
  start, when they are missing, a runtime and a view of their own (as the live background's) load on a worker
  thread; the attract's story runs to frame 1065 a few milliseconds of each menu frame (no stall), then a fresh
  runtime (loaded on a worker, the same view) plays Chapter 1 as the checks' `--chapter 1 --lives-kept 1` does: the
  `--autofire` player up to frame 4200 (`kPreviewBridgeFireUntil`: it clears the way and would shoot the carrier),
  then no input, at full speed, drawn only at frame 4320. Each frame is drawn at 960 x 540, cut and kept as
  `<settings>\previews6\base_<arcade|pc>.png` (Blood Color) and `bridge_<arcade|pc>.png` (Models; the PC ones only
  with the PC files). About 1 s a model set (~0.2 s of it the menu frames' work, 3 ms a frame at most). Then the
  runtime and the view are let go. Made again only when missing or when the arcade / PC files' folders change
  (`previews6\source.txt`).
- **Every picture is drawn from those two on the CPU** (`menu_pages.cpp previewRender`, `preview_art.cpp`): the
  crosshair, the blood burst (our drawing, in the colour as the menus' blood is); the Custom colour's picture is
  drawn live from the same frame for each colour (a few ms; Frontend `previewImage`, "mem:" keys), so no files per
  colour and no game run.
- **Why not shipped as PNGs**: they are the game's renders of SEGA's art from the player's own files, so the package
  has none; each player's game makes its own on first start.
- **Blood Color Custom lag fixed**: each step of a dragged colour bar was another key for the live background, which
  loaded the whole attract again every step. Arcade models now take the new colour at once
  (`GameView::setBloodCustom`); PC models load again only after the colour rests 0.8 s.
- Testing: `HOTD_PREVIEW_TEST=1` (hidden window) makes them and prints `settings pictures: N of M made`;
  `HOTD_PREVIEW_OUT=<folder>` writes every picture the pages would show; `HOTD_FE_PREVIEWS=<previews6 folder>` with
  `--frontend-shot` draws them on the pages; `HOTD_PREVIEW_DUMP=<folder>` saves Chapter 1's whole render every 10
  frames in the 400 before its frame (`HOTD_PREVIEW_DUMP_FROM=<frame>` from there; `HOTD_PREVIEW_BRIDGE=<frame>`,
  `HOTD_PREVIEW_FIRE_UNTIL=<frame>` another frame / autofire end; to choose a frame).

### menus3: Icon Outline, Gallery list scroll

- Controls > **Icon Outline** (launcher.ini `iconoutline=`, default **Off**): Xelu's icons as drawn; On: prerel's light
  rim and brightening. Comparison: `private\changelog\img\menus3\xelu_compare.png` (left Off, right On; top the
  Controls page, bottom the main menu, DualSense icons). `HOTD_FE_OUTLINE=1` for `--frontend-shot`.
- Remake Gallery: the entries' list (Humans, Creatures ...) scrolls with the wheel over it and its scroll bar can be
  pressed / dragged (`Frontend::galleryListWheel` / `galleryListBar`); a new entry centres it again. Elsewhere the
  wheel zooms the model as before.
