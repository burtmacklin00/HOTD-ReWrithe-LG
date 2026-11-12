# Test checklist (build of 2026-10-19 night)

Play the portable copy: `private\portable\HOTD ReWrithe\HOTDReWritheSetup.bat`. F12 = screenshot (into its `screenshots\`); record
with OBS as usual. For anything odd, note **chapter, place, model source (arcade / PC), sound (PC / arcade), and
whether the game froze or only something looked / sounded wrong**. Tick what works, write a line for what doesn't.

## 1. Start-up and menus
- [ ] **New setup (v1.1.0, setup2)**: copy the package somewhere, empty `data\arcade` / `data\pc`, delete
      `settings\launcher.ini`, double-click `HOTDReWritheSetup.bat`: a 960 x 600 window, dark, the ReWrithe wordmark
      small at the top left, steps at the top right (Install to, Game files, Options, Install, Play), Back / Next at the
      bottom. It cannot be resized; F11 does nothing there.
  - [ ] **Install to**: `C:\Games\HOTD ReWrithe`, Browse... picks another folder (a folder with files in it gets
        `\HOTD ReWrithe` added), free space shown. A folder in Program Files is refused with a red reason, Next grey.
  - [ ] **Game files**: drag your ROM zip from Explorer onto "Arcade ROMs" and the PC game folder (or its zip) onto
        "PC game": each turns green with a check mark. Click a box: a file dialog (zip or CD image); "Folder..." picks a
        folder. Try a wrong file (the PC zip on Arcade ROMs, a folder without THOTD.EXE): red with a plain reason.
        If you have a CD image (.iso / .bin+.cue) of the PC CD, drop it on "PC game" and note what it says.
  - [ ] **Options**: Desktop shortcut and Start menu entry on, Add to Steam off, Import the CD music on with the
        number of tracks found.
  - [ ] **Install**: the bar fills, then **Play**. The game starts from the new folder (`C:\Games\HOTD ReWrithe`), its
        desktop / Start menu shortcuts have your PC game's icon. "Open the install folder" opens it in Explorer.
  - [ ] The installed folder works without your originals: rename your ROM zip / PC folder for a moment, start the game
        from the shortcut: it goes straight to the menu.
  - [ ] Windows Settings > Apps > Installed apps lists "The House of the Dead: ReWrithe" (version 1.1.0). Uninstall:
        it asks first, then asks about your settings and high scores; afterwards the folder (or only `settings\`) and
        the shortcuts are gone. Your original ROM zip / PC game are untouched.
  - [ ] **Play from here** (on the Install page) plays without installing, as before.
  - [ ] Options -> System -> **Repair / Change Files**: the Game files page; Cancel goes back to the menu.
  - [ ] Options -> System -> **What's New**: this version's changes; Continue goes back to the menu. The page also
        shows once by itself the first time a new version starts.
  - [ ] Language Japanese / Korean / Arabic / Hebrew: the setup's texts are translated.
  - [ ] **PC files of another version** (2026-11-02, pchotfix): drop a PC copy whose BIGGER.VMC differs (for example
        the Japanese release): yellow "Accepted, with a note:" and a short note, Next works; the main menu's text and the
        hand cursor look normal, PC models' textures look normal.
  - [ ] A PC game one folder down (`data\pc\<folder>\Game Files\`): the main menu shows (not straight into the game).
  - [ ] Drop a SEGA Saturn disc image (or its zip) on "PC game": red, "This is the Saturn version".
- [ ] **Auto-update** (when a newer release is on GitHub): the page shows its version and changelog, Not now goes to
      the menu, Update shows a progress bar, then the game closes and starts again in the new version; settings kept.
- [ ] The game opens straight in borderless fullscreen (no small window). F11 / Alt+Enter leaves it; quit and restart:
      it opens the way you left it. Options -> "Start Fullscreen" Off / On works.
- [ ] The viewer also opens fullscreen; F11 toggles.
- [ ] **Attract prologue doors** (2026-10-31, attdoor): in the story, where Sophie stands between two zombies, the
      big red double door is behind her (it was an empty black doorway). Arcade and PC models, 4:3 and 16:9.
- [ ] **Attract prologue** (2026-10-27, arcfix): when the story scenes change place (e.g. after the photo of the three
      at the start), the screen flashes white and fades in, as the arcade; no single frame of the old room with
      wrong textures. In the ending, the scene changes still go black first and come back.
- [ ] Main menu music = the Chapter 4 song (the original PC's menu). Options -> "Menu Music" cycles songs by name,
      the menu plays the chosen one (your MP3s in data\music still match).
- [ ] **The new main menu** (1998 PC look kept): Arcade (always Chapter 1), Chapter Select (boss portraits; pick 1-4),
      Boss Mode, PC Mode, Gallery, Jukebox, Co-op (Local 2P / 4 Players / Online Co-op / Human-CPU seats), Sonic Mode,
      High Scores (page arrows: arcade, Sonic presets, 4P tables), Options, Quit. A help line explains each item.
- [ ] Options: the PC's Options screen + tabs **Video / Audio / Controls / Gameplay / Mods / System** (all the old
      "More Options" settings are on these tabs). Mouse, arrows + Enter, a pad, and a light gun all work; any window size fits.
- [ ] **Menu polish** (2026-10-21): every page has the dim controls line at the bottom; Esc / pad B goes back on every
      page. On the main menu Quit, Esc or B asks "Are you sure?" (Esc / B again = No, stays in the menu).
- [ ] Settings pages: pointing anywhere on a row lights label + value and a click changes it. Grey rows say why in the
      help line (Frame Rate Unlimited -> Vsync grey; Music Off -> Music Volume / Menu Music grey; Title / Attract Off ->
      FBI Screen grey; Light Gun rows grey until F9 calibrated a gun). Rows ending in ">" open a page.
- [ ] The small EXPERIMENTAL tag: Local 4 Players, players 3 / 4 rows, 4P Boss
      Hit Points, a Light Gun input, Custom blood. Nothing overlaps or is cut at 4:3, 16:9, 16:10.
- [ ] Controls: Keys & Buttons is now right under the inputs. Name entry: the last cell says "Done".
- [ ] **PC Mode = the PC** (pmbody, 2026-10-27): play PC Mode Chapter 1 and a bit of Boss Mode with the PC game
      folder set. Zombies, dogs, Murrers, barrels and the Chariot behave as before (no one stuck, no shots passing
      through), the camera moves smoothly, Sophie / hostages are carried normally, the rain still falls. Hits land on the
      body part you aim at.
- [ ] **PC Mode Chapters 3-4** (pmch34, 2026-10-27): the iron-ball zombies throw, pull back and drop the ball
      normally (no ball stuck in the air); breaking Chapter 3's machine props scatters pieces; the Hermit's body goes
      away after his death scene; the Chapter 4 Hangedman starts attacking at once (no long hover first); the
      Magician's death flash still shows, and the ending follows a little sooner, with no dark screen left.
- [ ] **PC Mode seats** (pmsolo): solo with player 2 on No Player: no CPU joins (even with a CPU teammate set). 4
      players: each panel's pick plays (No Player = empty seat); the CPU / Player button beside Ready makes a seat a
      CPU or a person; players 3 / 4's cut-scene bodies show their picked characters.
- [ ] **Extras' pictures** (pmpick, 2026-10-30): PC Mode pick (Classic / Remake, 2 and 4 players) with Dr. Curien
      (Dead), Researcher (woman), Researcher (woman, 4) etc.: each shows its own face (never Sophie's or the PC
      Researcher's), on the same black square as the PC's six. High Scores: the small pictures keep their colours.
- [ ] **Extras by model set** (pmpick): Video > Models Arcade: the extras' faces are the arcade's (Dr. Curien (Dead)
      with the arcade's dead face); Models PC: the PC models' faces. Switch in Options, go back to the pick: the
      pictures follow (a second or so the first time).
- [ ] **Remake PC Mode stones** (pmpick): 2-player pick: no brown bits of the PC menu above / beside the two stones
      or between them.
- [ ] **Score gap** (pmpick): High Scores > PC Mode, Classic: "Researcher W2" etc. with a clear gap after the score
      (also in another language); Remake: the long names keep off the score too. Remake pick: the names keep a gap to
      both arrows.
- [ ] **Long Extra names** (namefit): High Scores > PC Mode with "Researcher (woman, 2)" etc.: the name never runs over
      the score ("Researcher W2" in Classic). PC Mode pick (2P and 4P, Classic and Remake): the
      long names sit between the arrows without touching them.
- [ ] **Menu rework** (menu2, 2026-10-21): the main menu is shorter: Arcade, PC Mode, Boss Mode, **More Modes** (Chapter
      Select, Co-op, Sonic Mode), **Extras** (Gallery, Jukebox), High Scores, Options, Quit. A group shows its name in
      yellow and Back (Esc / B). Leaving Chapter Select / Co-op / Gallery / Jukebox comes back to its group. Sonic Mode
      has no EXPERIMENTAL tag any more (main menu, Gameplay, launcher).
- [ ] **Jukebox**: tabs Music / Effects / Voices, one row of buttons (<< Play Stop >> mode Info); the file / codes line
      and the number column show only with **Info** on. Volumes and Exit at the bottom.
- [ ] **menu3**: the controls line at the bottom is short and dim ("Enter/A Choose  Esc/B Back"); in More Modes /
      Extras it says Back, on the top menu Quit. Jukebox: one box shows what plays (name, bar, time; Info line inside).
- [ ] Clear a chapter whose next one is not played on (or the ending): the end screen says "back to the main menu",
      and that is where it goes.
- [ ] **Players' quick choice** (2026-10-26, playerpick): Arcade, a chapter in Chapter Select, a boss in Boss Mode and
      PC Mode now show "Players 1 2 3 4" first, with Human / CPU for each extra player. Start is already selected:
      Enter / A / one click plays. Pick 3 or 4 (click, keys 1-4, Q / E, pad LB / RB): a small "Experimental" line, no
      other clutter. Set player 2 to CPU, start, quit, start again: the same choice is pre-selected (also after a
      restart). Esc / B goes back to where you were and changes nothing. PC Mode: 3-4 players give the four panels.
      Check both menu styles, mouse / keyboard / pad. Online co-op: no such page.
- [ ] **Remake menu style** (2026-10-21): Options > Video > **Menu Style** -> Remake. The menu turns wide (no side
      bars at 16:9 / 16:10 / 3:2; still fine at 4:3): logo on top, items on torn paper strips, Options as paper tabs.
      Check: Up / Down + Enter, Esc / B back, Left / Right change a setting, Q / E (pad LB / RB) switch tabs, the mouse
      shows the bony hand and clicks work. Switch back to Classic PC: the old menu returns exactly as before.
- [ ] **Remake is the default + wordmark** (2026-10-21, logo2): with fresh settings (no launcher.ini) the menu opens
      in the Remake style with the Remake boot. The logo shows "REWRITHE_" in bone white under "THE HOUSE OF THE DEAD"
      (Remake only; Classic PC has none). Your old settings with Menu Style Classic PC stay Classic. The portable
      package shows it too (data\ui\rewrithe_wordmark.png).
- [ ] **Live attract background** (2026-10-21, liveattract): with Menu Style Remake the menu's background is the
      attract actually playing (story, title, how to play, demo, ranking room, over and over): grey, dark, sharp, no
      logos / PRESS START / scores / captions / white flashes, no sound of its own (only the menu music). Keys, clicks
      and pads only work the menu. Change Options > Models (arcade / PC) and Blood Color: the background follows.
      Check it stays smooth at your resolution (Surface too) and fullscreen.
- [ ] **Remake boot** (liveattract): with Menu Style Remake, start the game: SEGA logo, then the zombie story, then the
      attract with sound, then the menu by itself (a key skips to the menu). Waiting on the menu never starts the
      attract any more (the 5 s fade stays). Classic PC: the boot is exactly as before.
- [ ] **Attract Demo** (liveattract): Extras > Gallery > C (the Cut-scenes page) > **Attract Demo**: the original
      attract full screen with sound (logos, flashes and all); any key, click or pad button comes back to that page.
      Try it in both menu styles.
- [ ] **Remake menu blood** (2026-10-21, blood2): the generated splatter is back, reworked: heavy at the top corners,
      down the sides, drips from the top ending in a bead, dark cores with lighter edges and a wet shine. Options >
      Blood Color (green, blue, purple, yellow, a custom colour) recolours it, the panel's red bottom, the handprints
      and the focus bars at once. Check it looks like blood, not noise, at your resolution.
- [ ] **Remake menu on the GPU** (2026-10-21, blood2): the Remake menu looks the same but sharper (text at your
      screen's resolution, up to 1440 lines) and costs almost nothing a frame. Check: the main menu and a settings
      page in a window and in fullscreen, on the Surface too; moving the focus and the hand is smooth; no dark edges
      around the splatter or the paper strips.
- [ ] **Your own splatter** (blood2): click the backdrop (outside the panel and buttons), press B, or press the right
      stick (R3) on a pad: a new pattern fades in with a wet splat sound. Restart: the same pattern comes back.
      Every pattern should still frame the panel.
- [ ] **Blood Color in the backdrop** (blood2): Blood Color green / blue: the settings pages' backdrop turns that
      colour (red stays as before); the main menu's background gets a faint cast of it; the current tab's label and
      the red letters on the strips follow it too (yellow still readable on the paper).
- [ ] **Idle fade** (blood2): leave the Remake main menu alone for 5 s: the menu fades away and the background
      shows; a key, click or mouse move brings it back without doing anything else.
- [ ] **Bigger settings text** (blood2): the settings rows, tabs and help line are larger; 12 rows fit at 16:9 /
      16:10 (11 and scrolling at 4:3 / 3:2); values right-aligned, nothing overlapping.
- [ ] **Remake menu polish** (2026-10-21, remake2): the blood at the screen edges and under the panel is now the
      arcade's own "player hit" splashes (no generated spray). Red item text and the tab / settings text have a dark
      outline and shadow; the paper strips cast a soft shadow. Bottom right: a neat column of prompts with same-size
      key boxes (one row along the bottom on a 3:2 / 4:3 settings page). Check it reads well at 1080p and on the
      Surface; nothing looks blocky or noisy.
- [ ] **The other pages in the Remake style** (2026-10-22, remakepages): with Menu Style Remake, open Chapter Select,
      Boss Mode, PC Mode (2 and 4 players), High Scores (and a boss's times after a Boss Mode run) and the Jukebox:
      each is as wide as the window (no black bars) with the live attract behind it, and keeps all its items.
      Chapter Select / Boss Mode: the bosses as cards; arrows move between them, Enter starts. PC Mode: the
      tombstones in the middle, Start / Exit strips, arrows move between the arrows / Start / Exit. High Scores and
      the Jukebox: Q / E or Left / Right turn the tables / lists (the Jukebox: Page Up / Down still turn its pages).
      Try 16:9, 3:2 and 4:3 windows: nothing overlaps.
- [ ] **Gallery over the Remake backdrop** (remakepages): with Menu Style Remake the Gallery (Classic layout) and its
      Cut-scenes page look as before but the live attract shows at the sides instead of black bars.
- [ ] **High Scores on a fresh install** (remakepages): with no game played yet (new settings folder), open High
      Scores: it shows "No scores yet" (it used to crash).
- [ ] **Remake menu fixes** (2026-10-22, remakefix):
  - [ ] Start the game (settings with only arcade= / pc=, or a fresh folder): no text launcher / "debug" page, just
        black for a moment, then the SEGA logo. Rename your ROM zip once: a short "Find your game files" page appears.
  - [ ] Press a key during the boot attract: the menu's background is never the pink blocky frame; it is the desk /
        telephone shot (from the second start on at once; the first start: dark, then it fades in) and it moves on
        smoothly. Scene changes behind the menu cross-fade instead of cutting.
  - [ ] Idle 5 s on the main menu: everything fades out smoothly; move the mouse / press a key: it fades back in
        smoothly (about half a second), nothing pops; that first key does nothing else.
  - [ ] Quit's "Are you sure?" has the same backdrop colour as the main menu (not a stronger red).
  - [ ] Click around the menus a lot: the bony hand never disappears while you use the mouse; it hides only when you
        use the arrows / a pad and comes back as soon as the mouse moves.
  - [ ] Settings: click (or Enter / A on) Models, Aspect, Menu Style, Display, CRT Filter, Crosshair Size, Sound, Blood
        Color, Difficulty ...: a small list opens under the value, the current one red; arrows / mouse move, click /
        Enter picks, Esc / B / a click outside closes. Left / Right still change the value directly.
  - [ ] **Every list has a dropdown now** (optui, 2026-10-26): Frame Rate, Menu Frame Rate, Crosshair (+ Size,
        Thickness), the volumes, Menu Music, Lives / Continues on the Game tab, the Advanced page, Co-op, the
        Controller pages, the CRT page. A long list (Frame Rate, Menu Music, Lives) scrolls: Up / Down past its end,
        the mouse wheel, or the small arrows. On / Off rows stay a switch (one click).
  - [ ] Options > Video > **crosshair preview**: scroll down to the three Crosshair rows: a box between the names and
        the values shows P1-P4's crosshair on a picture of the game, at the size it will have in play; change style /
        size / thickness and it changes at once. Open the Crosshair list: each choice is drawn as its icon (your own
        crosshairs folder's sets too).
  - [ ] **Frame Rate** list: Display Refresh, 24, 30, Arcade (57.52), 60, 75, 80, 90, 120, 144, 165, 240, 360,
        Unlimited. Arcade in play: the picture at 57.52 a second, smooth with the game.
  - [ ] **Menu Frame Rate**: Auto / 30 / 60 / 120 (Auto: 120 on the RTX PC, 30 on the Surface; never above the
        screen). The menu feels smooth at each; the game itself still follows Frame Rate.
  - [ ] Classic style: a **right-click** on a settings row opens its list (a left click still steps); the wheel
        scrolls a long list or turns the pages; the crosshair preview sits under the Crosshair rows (page 2).
  - [ ] Scrolling Options' rows and opening / closing a dropdown feel instant (no hitch), the Surface too.
  - [ ] Q / E (and the pad's LB / RB) switching tabs makes the menu's click sound; so do Up / Down moves and Esc.
  - [ ] Options > Video > **Display**: lists your monitors (name and size); picking the BenQ moves the game there
        (fullscreen stays fullscreen); restart: it opens on the BenQ. Resolution then lists that monitor's modes.
- [ ] **Package data** (2026-10-26): the portable folder now has data\fonts, data\lang (en / ko / ja / ar / he),
      data\mods (Ragdoll, example) and data\ui: Korean / Japanese / Arabic / Hebrew menus and the Mods page work from
      the package, not only from the repo.

## 1a. Loading and smoothness (perf pass; best checked on the Surface / a slower PC)
- [ ] Stay a second or two on the main menu, then Arcade: the game should start almost at once (no pause).
- [ ] Clearing a chapter: the next chapter starts without a freeze after the results screen.
- [ ] The ending's staff roll (the old chapters' rooms) no longer stops for a moment at each new room.
- [ ] Nothing looks or sounds different from before (HUD, blood, sights, menus, the arcade sound).
- [ ] **Remake menu smoothness** (menuperf, 2026-10-26; both PCs, the Surface most): moving up / down on the main
      menu, Options tabs, Chapter Select, Boss Mode and PC Mode feels instant; the Jukebox with a song playing no
      longer stutters each second (the time and bar tick on; Up / Down moves the red bar at once).
- [ ] Options > Video > **Live Background**: Auto / On / Off (the dropdown). On the Surface, Auto = a still
      background (the help line says "now: off, integrated graphics"); On = the attract behind the menu at 30 pictures a
      second; Off = the still. On the RTX PC, Auto = the live attract as before. Menu Style Classic: the row is grey.
- [ ] The menus no longer run faster than the screen (no tearing, no fan noise in the menu); the menu looks the same
      as before.

## 2. The four chapters (play each; both model sources if you can: Options -> Models)
- [ ] **Chapter 1** unchanged; clearing it goes into Chapter 2 (lives / score kept).
- [ ] **Chapter changes (now done the arcade's way):** after each clear the next chapter starts with your lives and
      score kept and a full gun; for the first second and a half nothing can hurt you. Check 1 -> 2, 2 -> 3, 3 -> 4
      with one player, two players, 4 players, PC Mode and Chapter Select (start at 2 or 3). The HUD (lives, bullets,
      PRESS START for a free seat) shows right away; no black screen stays; nothing freezes.
- [ ] **Chapter 2** start: the intro card in widescreen keeps black side bars (no world at the sides).
- [ ] **Ending credits** in widescreen (both model sources): each staff-roll card and the GAME OVER card show in 4:3
      with black side bars (no 3D past the card). The 3D scenes, END and the rank cards still fill the window.
- [ ] **Chapter 2 Hangedman:** big green bat wings visible; he hovers in front of the moon (not sinking behind the roofs);
      the camera stays on him the whole fight (no driving off along the roof mid-fight); hits flash him white; he dies
      and falls. Tell me anything that still looks wrong (timestamp in your video).
- [ ] **Hangedman wings with Smooth Motion** (hangcheck): at your 280 Hz the wings stay on his shoulders while he flies
      (before: they slid / shook against his body). Compare with the clips in `private\hangedman\index.html`.
- [ ] **Hangedman moon** (moonline): no thin bright diagonal line across the sky near the moon while he flies / flees.
- [ ] After the Hangedman: the Curien cut-scene colours (should be deep red-brown; a fix may still be in progress).
- [ ] Chapter 2's clear leads into **Chapter 3**.
- [ ] **Chapter 3** first scene: the two iron-ball zombies behind the window (shoot the bodies while they wind up to
      throw); the ball has its chain. Can you get through as one player?
- [ ] **Chapter 3 Hermit:** title card, fight on the wall, the drop to the floor, his face flashes white when hit, he dies.
- [ ] Chapter 3's clear leads into **Chapter 4**.
- [ ] **Chapter 4** opening: the Chariot rematch (it lands and fights; it must never walk off in the air).
- [ ] **Chapter 4** rest of the chapter (a Hangedman rematch on the way); **the Magician:** shoot his limbs (his body
      only sparks), fireballs, swoops, barrage; his veins glow; he flashes white when hit; his death fades the screen to
      white and back.
- [ ] **The ending:** Rogan's scene, the staff roll (with PC models: the PC's own credit sheets, no blood / claw pictures),
      the closing room with Rogan visible, END / GAME OVER, the results, the rank and name entry.
- [ ] **The ending, again after the proof round** (its code was moved and checked against the arcade): one player and
      two players; it plays to the end, the results card shows your score and rank, then the name entry or the ranking.
      Anything that looks different from before (missing props in the staff roll, a body standing wrong) -> tell me.
- [ ] Chapter 3 on the other route (rescue the hostages, shoot the route spot): the hostages, the route spot's spark
      mark, and the iron ball's throw when you let a zombie wind up.
- [ ] Name entry (shoot letters): no stray 3D scenery behind the letters; with PC models the PC's carved letters.
- [ ] Ranking screen with PC models: correct letters (TAG not TXG), rank cards, the desk picture behind.

## 3. Widescreen / HUD / crosshairs
- [ ] Fullscreen widescreen: subtitles whole ("Others are still inside", Sophie's "Or else, something terrible will
      happen."), no letter pieces at the screen edges.
- [ ] Chapter-clear results in widescreen: ammo / lives / score inside the 4:3 picture, not at the window edges.
- [ ] Chapter-clear results (the researchers' photo) in widescreen: the sides black, nothing of the 3D beside the
      picture (2026-10-28 sweep: a railing showed at the left in Chapter 1).
- [ ] Attract in widescreen, the white flash cutting to the sewer zombies: the whole screen white, not just the middle.
- [ ] The end screen ("CHAPTER n CLEAR") on black, readable.
- [ ] Crosshairs (Options): arcade, PC, simple cross, ring + dot, corner brackets, gap cross; Crosshair Size and
      Thickness change them; they stay centred on your aim in fullscreen.
- [ ] New **Capped Cross** crosshair (after Gap Cross in the Crosshair list): a plus whose four arms end in thin bars;
      check it in the Firing Range at small / large size and thin / bold thickness, and in each colour.
- [ ] Your own crosshair: put a white `crosshair.png` (or `p1.png` / `p2.png`) in `data\crosshairs\<name>\` -> it appears
      as "Custom: <name>".
- [ ] **16:10** (2026-10-26, AI-tested only: no 16:10 screen here): Options -> Video -> Aspect now cycles
      Widescreen -> 16:10 -> 4:3. On your 16:9 monitor 16:10 shows thin black bars left / right; the HUD (bullets,
      torches, PRESS START) sits at the bars' inner edge, not under them; shooting into a bar reloads. On a 16:10
      screen (Steam Deck 1280 x 800, 1920 x 1200 laptops) Widescreen and 16:10 look the same and fill the screen.
- [ ] (team 2026-11-03) Options > Video > **Transparency** (arcade models): Smooth (as before), Smooth, Clean Edges
      (the brown / dark outline round floor stains, blood splatters and water gone), Dithered (arcade) (their edges in
      a fine screen-door stipple, as the Model 2's checker polygons). Look at the floor stains of the first courtyard and
      the sewer water / blood. AI-checked: Smooth is byte-identical to before (6 frames, arcade and PC models).
- [ ] (team 2026-11-03; trailer comment: "on MAME you can't shoot in the widescreen sides") Widescreen: shoot a zombie
      in the extra picture at the left / right (Chapter 1's first rooms have some) with the mouse, a pad's aim and a
      light gun: it takes hits as in the middle. Only the window's outermost pixel row / column reloads (#15 / #17).
      Headless (AI-checked): the CPU teammates' shots beyond the 4:3 edge (|x| > 248) hit 42 of 53 times, as in the
      middle (the shot test has no 4:3 limit: `shot_resolve` tests every depth-list object, the ray at any x).

## 4. Sound
- [ ] **The arcade sound board is now the default** (Options -> Audio -> Sound: Auto = Arcade, in every mode, PC Mode
      too; PC = the 1998 port's sounds). A fresh start plays the arcade sound: level OK? Music at each chapter start?
- [ ] PC sound (Sound: PC): no pops / clicks on the bony-hand cursor click or the reload; loudness about the same as
      the arcade sound; not "compressed".
- [ ] Arcade sound (Sound: Arcade): sound continues through Chapter 2, 3, 4 (it used to go silent from Chapter 2).
- [ ] Die, continue: the sound comes back (both sound settings). If it ever goes mute: which chapter / place / setting.
- [ ] Pause (Esc / P) and resume: the music continues where it was.

## 5. Your PC install (C:\Games\HOTD, patch 9)
- [ ] The original THOTD.EXE: the main menu / ranking / options now play the name-entry song; Chapter 4 plays the
      Chapter 4 song (Track12). Backups are `*.before-musicfix` if you want to undo.

## 6. Two players on one PC
- [ ] Two mice (or mouse + keyboard): each has its own crosshair; player 2 joins mid-game with start.

## 7. Online co-op (two of your PCs; peer-to-peer, no server)
Copy the whole portable folder to the other PC (the same build on both). Details: docs/netplay.md "How to test".
- [ ] **Host:** main menu -> Co-op -> **Online Co-op** -> Host (game name, password). Windows asks about the
      firewall the first time: allow **Private networks**. The waiting room shows the password, the share code, the
      LAN address and the internet status (UPnP: opened automatically, or "Forward UDP <port>").
- [ ] **Join on the same Wi-Fi / router:** Online Co-op -> Join -> the host's game appears in the list -> click it,
      type the password, Enter. The host's waiting room shows player 2 with a ping; the host presses Start Game.
- [ ] **Join with the share code:** copy the share code on the host (click), paste it on the other PC (Paste Share
      Code / Ctrl+V), Join.
- [ ] A wrong password is refused: "Wrong password ..." shows on the Join page (in full, over a few rows).
- [ ] In play: each sees the other's crosshair; shooting / damage / deaths / hostages match on both screens; **F3** shows
      ping, delay, rollbacks. How does it feel (lag) on your LAN? Does the other player's crosshair jump around?
- [ ] Unplug Wi-Fi briefly on one PC: "connection lost: waiting", then it continues. Quit on one PC: the other says
      the other player / the host left.
- [ ] Any "desync at frame N": note N and what happened just before.
- [ ] Later, over the internet (a friend's house): UPnP or a port forward + the share code.
- [ ] Mixed: one PC with a DualSense, the other with a mouse.
- [ ] **Join a running game:** host -> waiting room -> **Play Alone** (starts at once); on the other PC join from the
      LAN list a minute later: the host shows "Player 2 is joining...", the joiner catches up, press start to join in.
      (Known: the joiner hears no music until the next music cue.)
- [ ] **Rejoin:** turn the joiner's Wi-Fi off for ~10 s (or quit it), then join again with the same password: it comes
      back where it stopped.
- [ ] Online menu with a controller: in the password / address fields the d-pad picks letters (Up / Down change,
      Right adds, Left deletes, Cross keeps, Circle cancels).
- [ ] The setup window shows Host / Join rows only when the menu cannot come up (no PC files; 2026-10-31).
- [ ] Online with Sonic Mode on the host: the other PC follows the host's preset.
- [ ] **New (step-by-step guide: docs/online_howto.md).** Online Co-op -> **Your Name**: set it on each PC; the
      waiting rooms on **both** the host and the joiners show the names and pings.
- [ ] Joining with the host not hosting (or its firewall blocking hotd.exe): after ~20 s a clear "No answer ..."
      message with the firewall / port hint (no endless "Connecting...").
- [ ] Other messages to try: a third PC joining a 2-PC game ("full"); the host stopping while you wait ("The host
      left"); 20 wrong passwords -> the host says so and the right one is refused too until it hosts again.
- [ ] In play: the ping at the bottom, in the middle (green / yellow / red, "LAG" when it struggles; moved there from
      the corner, which had player 2's lamps / player 4's HUD). Readable, not in the way? F3 -> detailed (four short
      lines) -> off (remembered); also Online Co-op -> In-game Info. An F12 screenshot now shows it too.
- [ ] The Online pages' text fits now (long messages on their own rows, long names shortened with ".."; Share Code
      says "Click to copy", the code is in the help line at the bottom). Anything still cut or crowded?
- [ ] "Up to 4 PCs": a friend who joined presses Cancel (or closes the game) before Start: after ~5 s the host's page
      says "<name> left. Seat n is free again.", the other friends' lists show the seat free, and a new friend can join.
- [ ] The release zip has "ONLINE HOWTO.txt" next to README.txt (README's Online Co-op line points to it).
- [ ] Brother's PC + two laptops: host "4P Online: Up to 4 PCs" on the PC with the best connection (cable if
      possible); the laptops join from the list.
- [ ] **New (2026-10-26): one short code.** Host a Game -> Start Hosting shows **Your Code** (like `K7QX-M2PD-H4TA-9BCE`).
      On the other PC: Join a Game -> type it in **Join Code** (small letters / no dashes are fine) -> Enter. Easy to
      read out and type? Also try **Copy Code** (says "Copied!" for 2 s, then the label is empty again) and **Paste
      Code** / Ctrl+V on the other PC.
- [ ] A typo in the code (one letter changed, a letter missing, a 0 or O typed): a plain message at once, no waiting.
- [ ] Same Wi-Fi with UPnP on: the code still joins (it finds the host on your network by itself).
- [ ] **New (netfix, 2026-10-30): no code on your network.** Host on one PC; on the brother's PC Join a Game: the
      host's game shows in the list; one click joins (no code typed). Then the other way round (the brother hosts).
      If the list shows the game but joining says "on your network but its game does not answer": allow hotd.exe in
      Windows Firewall on the host PC (docs/online_howto.md section 2).
- [ ] **New: close + rejoin.** In play, close the joiner's window (X). The host shows "P2 left: waiting for them to
      join again (same code). Enter / Start: go on without them". Start the game again on that PC -> Online Co-op ->
      Join a Game -> click the game in the list (or paste the code right away): back in the same game. Also once with
      Esc -> Exit instead of closing.
- [ ] **New: go on without them.** Close the joiner's window again; on the host press Enter (or Start on the pad):
      the host plays on alone. The joiner can still join that game later.
- [ ] **New: 3 or 4 PCs.** One joiner closes its game: every PC stops; the others show "P2 left: the host waits for
      them or goes on". Rejoin -> all go on; or Enter on the host -> the rest play on without that seat.
- [ ] **New: pause online.** Esc in play on one PC: the other PC shows "P1 PAUSED" (or P2 ...) in the middle until the
      first picks Continue; a long pause (over 10 s) must not drop the other player.
- [ ] **New: only the host skips cut-scenes.** On the joiner's PC, shoot during a cut-scene: it does not skip, a note
      "Only the host can skip" shows for 2 s. The host's shot skips it for both.
- [ ] **New: the other player's gunfire** is heard on both PCs, the host's too (before, the host often heard none of
      player 2's shots).
- [ ] **4P settings online** (2026-10-26): the host's "4P Enemy Toughness" and "4P Boss Hit Points" now apply on
      every PC (the joiner's own values are ignored, nothing desyncs). Set different values on the two PCs, play 4P
      online: no desync message; the joiner's console / log line shows the host's numbers.
- [ ] **New: PC Mode online** (2026-10-27, onlinepm). Host page: **Game Mode -> PC Mode**, **Your Character** (and
      **P3 Character** with two players on your PC). Join page: **Your Character** (Auto = Rogan / G / Rogan 2 / G 2 by
      seat). In the waiting rooms each player's row shows their character ("Sam (you), Sophie, 38 ms"); the joiner's
      page says "Game Mode: PC Mode". Start: everyone plays the character shown, with PC Mode's lives / rounds /
      reload, the host's Continues and Free-Play, at 60 Hz (Game Speed Auto) on both PCs. No desync message.
- [ ] PC Mode online with 4 PCs ("Up to 4 PCs").
- [ ] **New (onlinepm2): joining a running PC Mode game** with your own character: pick e.g. Sophie on the Join page,
      join the host's running game, press start: you play Sophie (her lives / bullets) on both PCs, no desync.
- [ ] **New: PC Mode online game over**: let everyone die (no continues left, or wait out CONTINUE?). The online game
      ends on both PCs at the same moment; each PC asks only its own player(s) for a high-score name, then shows its
      own PC Mode high-score table. The other PC's player is not in your table.
- [ ] **New: continues online**: host Continues 2. One player continues (start during CONTINUE?): the credit count
      drops on both PCs; the other's countdown restarts at 9. With 0 credits left a player out of lives is out at once.
      Host Free-Play (Cheat screen): continues never run out. The joiner's own Continues setting does not matter.
- [ ] **New: remembered picks**: Host page Game Mode PC Mode + a character (and the Join page's character): quit and
      start the game again: still set.
- [ ] Arcade online game over: still restarts the chapter as before.
- [ ] An arcade online game still plays as before (no "PC Mode" rows on the pages).

## 7a. Game speed, frame rate, smooth motion
Options -> Gameplay -> Advanced: **Game Speed** Arcade (default: the cabinet's real 57.52 Hz) / 60 Hz (the PC port's); **Frame Rate**
Display Refresh (default) / 30 ... 540 / Unlimited; **VSync**; **Smooth Motion** (default on).
- [ ] At your 280 Hz: Display Refresh + Smooth Motion: camera pans and moving zombies look smooth (no judder); camera
      cuts are clean (no smear between two shots).
- [ ] Smooth Motion off: the old look (each game frame shown several times).
- [ ] Unlimited / vsync off: still the same game speed (the game never runs faster with more fps).
- [ ] 60 Hz vs Arcade: 60 Hz plays ~4 % faster; Arcade lines the attract / cut-scene music up as on the cabinet.
- [ ] Any moving thing that flickers / jumps with Smooth Motion on (note where).

## 7a1. Draw Depth (2026-10-26, worker "clipfix"; no menu row since 10-27, "depthopt")
Arcade Sort is always on for the arcade models (the PC models always use the depth buffer, as the 1998 PC game).
The old switch is advanced only now: `drawdepth=buffer` in launcher.ini or `--draw-depth buffer` brings the Depth
Buffer back (the items below that say "Depth Buffer" need it).
- [ ] **New (depthopt):** Options -> Video (both menu styles): no Draw Depth row and no Draw Depth picture any more;
      Models / Aspect (and Gameplay > Blood Color) still show their pictures.
- [ ] **New (depthopt):** a launcher.ini that still says `drawdepth=buffer` (from the old row) keeps the Depth Buffer;
      remove the line (or set `sort`) to get the arcade's look back.
- [ ] Attract demo, the two-axe zombie: his black waistband no longer pokes out over his shirt (Arcade Sort);
      Depth Buffer brings the old look back.
- [ ] Play a bit of each chapter with Arcade Sort: anything that now shows through a wall or floor, or a wall
      drawn over a zombie (note chapter and place; the arcade itself has some of these).
- [ ] The ending: the car stays behind the gate's right pillar.

- (depthcmp) Chapter 2, the Hangedman intro (G at the window): no checkered patch on the building's wall with Arcade Sort.
- [ ] (earlyz) On the Surface Pro 7: play Chapter 1 with Arcade Sort at full resolution: smoother than before? The
      picture should look exactly as before (no new flicker, nothing poking through walls).
- [ ] (prepass) On the Surface Pro 7 (the 3D's depth pre-pass is on there by itself): Chapter 1 at full resolution,
      smoother than before? For a comparison start it once with the environment variable HOTD_PREPASS=0 (off).
- [ ] (depthfix) Draw Depth = Depth Buffer: the attract desk (no light through the wall), the two-axe zombie's
      waistband hidden, bodies / pond tombstones on the ground, Chapter 2's bullet holes on the balcony doors, the
      ending car behind the pillar. Known left: Chapter 3's door through the grey wall, a wall strip in the staff roll
      room, no shadow under the ending's walker (Arcade Sort, the default, has none of these).
- [ ] (depthfix) Gallery > Cut-scenes > Free Cam: fly close to doors, bodies and shot sparks: sparks no longer cut
      in pieces by a door, the door knocker shows; anything that still pokes through (note the scene).
- [ ] (depthfix) 4P Goodbye Curien in widescreen: the floor on the left. Not reproduced here (the left shows the
      roof's raised step); a screenshot of what is cut off would help.
- [ ] **(overdraw, 2026-10-28) PC models**: Chapter 2, the stair hall where the chainsaw zombie breaks through the door
      above the railing: his legs and the researcher stay BEHIND the railing (were drawn over it). Chapter 2's
      opening: the bullet holes on the balcony doors show (were hidden). Feet still stand on the floor everywhere
      (note any place where feet / bodies now sink into the ground with PC models).
- [ ] (overdraw) Anything else drawn over something nearer (arcade or PC models): F12 + say which models.

## 7a2. CRT filter
Options -> **CRT Filter** (Off / Arcade Monitor / Consumer TV / Sharp Scanlines / Custom) and **CRT Settings**.
- [ ] Each preset at your 280 Hz 1440p: sharp scanlines, no moire, not too dark; menus filtered too.
- [ ] Aiming with curvature on: the shot lands where the crosshair is (mouse and pad).
- [ ] F12 saves the filtered picture.

## 7a2b. Gamma (issue #23, 2026-11-03)
Options -> Video -> **Game Gamma** and **Menu Gamma** (0.60 - 2.00, default 1.00 each).
- [ ] Menu Gamma: drag the bar / Left / Right: the menu (and the Remake menu's live background) gets brighter / darker at once.
- [ ] Game Gamma, in the game (pause -> Options too): Chapter 1's mansion halls and Chapter 4's tunnel at 1.4: the dark corners
      open up, black stays black, the HUD and crosshairs still look right. Arcade and PC models.
- [ ] With a CRT filter preset on: gamma still applies (after the filter).
- [ ] F12 at 1.4 saves the brighter picture; at 1.00 the game looks exactly as before.
- [ ] Quit and start again: the value is kept (launcher.ini `gamma=`, `menugamma=`); Video's Reset to Defaults puts both back to 1.00.
- [ ] Game Gamma 1.4, Menu Gamma 1.00: the menus and the Gallery as before, the play brighter (and the other way round).
- [ ] An old launcher.ini with only `gamma=1.40`: Game Gamma 1.40, Menu Gamma 1.00.
- [ ] VR (if you have it): the eyes get the same brightness.

## 7a3. Boss Mode (the PC port's)
Main menu -> **Boss Mode**: Chariot / Hangedman / Hermit / Magician / All Bosses.
- [ ] Each boss starts near its fight; the clock (mm:ss:cc, bottom centre since 10-28) runs from the boss's entrance, stops on its
      death and while paused; no continues (game over when everyone is out).
- [ ] All Bosses chains the four with the clock kept; best times go to the boss tables (arrows switch tables).
- [ ] A new best time opens the name entry (the PC's screen): type, or click / aim / pad on the letter grid; the name
      shows in the table. Known: the walk to each boss is played (the PC skipped it).

## 7a4. PC Mode (the PC port's)
Main menu -> **PC Mode**: pick a character per player (Rogan, G, Rogan 2, G 2, Sophie, Researcher), Start.
- [ ] Each character's life / rounds / damage / hit size differ as on the PC; the ammo HUD shows up to 12 bullets.
- [ ] PC rules: no shot flash, the PC's auto reload, kills credit the right player, shots pass scenery.
- [ ] Known: the PC's reload timer is broken in the original (no delay), ported as is.
- [ ] (pcsel4) Players 3 or 4 (More Options / Co-op) -> PC Mode: four panels (P1 red, P2 blue, P3 green, P4 yellow),
      each with a portrait, name, arrows, Ready and the five ratings; nothing overlaps, also in a 16:9 window.
- [ ] (pcsel4) Each player picks on their own: P1 the mouse (or key 1 = Ready), P2 arrows / Space, P3 A / D / F,
      P4 J / L / H, or each player's gamepad (d-pad, South = Ready, East = take back). A Ready player's pick is locked.
- [ ] (pcsel4) The game starts by itself when everyone with a character is Ready ("Waiting for P..." until then), and
      each player plays the character they picked. A computer teammate shows "CPU" with a random pick.
- [ ] (pcsel4) Players 1 or 2: the screen is the PC's original two panels with Start, as before.
- [ ] (pcmode6) PC Mode: zombies turn toward you and your shots hit as before (now on the PC's own maths: tiny
      differences only). Shots at a zombie's edge should hit or miss as on the PC. Arcade games are unchanged.

- [ ] (pcmode7) PC Mode, every chapter: dogs, bats, the knife / axe throwers (Chapter 1 / 3), the jumping Murrers,
      frogs, researchers and thrown drums / barrels move, turn, leap and throw as before (now on the PC's maths: tiny
      differences only). Things that should look different, as on the PC: bats circling do not bob up and down; the
      Murrers of Chapter 1's 4th area stand a little higher. Bosses: unchanged (still the arcade's maths).
- [ ] (pcmode7) PC Mode: shooting still hits where the crosshair is, also while the camera turns (the shot now uses
      the PC's own camera matrix).
- [ ] (pmboss) PC Mode, the bosses (now on the PC's maths and rules): the Chariot walks, turns and gets pushed back
      as before, but its weapon hand can only be shot once it has landed (Chapter 4: wait until it is down); the
      Hangedman hovers a little nearer than in Arcade Mode and its claw / dive look the same; the Hermit spits and
      the Magician throws fireballs and barrages at you as before. Every boss can be beaten.
- [ ] (pmboss) PC Mode: dogs still pick left / right turns after a bite (now the PC's own random numbers); thrown
      drums / barrels tumble and vanish off screen as before; the axe thrower's axe flies from its hand.
- [ ] (pmboss) Arcade Mode bosses: unchanged.
- [ ] (pmboss) PC Mode Chapter 4: the Chariot rematch is harder than in Arcade Mode (its hand only counts as a hit after it lands, it stays closer); it should still be beatable.

### PC Mode bodies, ending and high scores (pcmode5)
- [ ] Pick e.g. Sophie (P1) and Researcher (P2): in the cut-scenes (the opening's run, the game-over scene) and the
      ending the players' bodies are those characters, not Rogan / G. Try each character once if you can.
- [ ] Chapter 1's car (opening) with PC models: the driver is your character (Rogan 2, G 2, Sophie, Researcher have
      their own PC seat models); with arcade models it stays Rogan / G.
- [ ] A PC Mode game over (or the end) goes back to the menus, not to a restart: with a high enough score the name
      entry comes (one per player), then High Scores shows the **PC Mode** table with your row and character.
- [ ] Main menu -> High Scores -> arrows to the last table **PC Mode**: name, stage, score, character + small picture
      (the PC's defaults BART .. ROHAN at first). The Arcade table is untouched by PC Mode games.
- [ ] Arcade games still end as before (3D name entry, arcade table).

### PC Mode continues and Game Speed Auto
- [ ] Options -> **Continues** 0-9 (default 5): in PC Mode every continue takes one from a pool shared by the players;
      at 0 the game is over when a player dies. Arcade and Boss Mode are unchanged.
- [ ] PC Mode, the **last continue** (Continues 1, or use them up): continue on the countdown with the last credit:
      you play on (before: GAME OVER at once). Out of lives again with none left: GAME OVER, and the route map shows
      your walker.
- [ ] Options -> Gameplay -> Advanced -> **Game Speed** Auto (default): the Arcade game runs at 57.52 Hz, PC Mode at 60 Hz (F1 shows
      the tick rate); Arcade / 60 Hz force one. An old `gamespeed=0` in launcher.ini reads as Auto.

### PC Mode Extra Characters (2026-10-28, pmextra)
- [ ] PC Mode pick: after Researcher the arrows go on to the **Extra Characters**: Researcher (1)-(6), Researcher
      (woman) .. (woman, 4), Dr. Curien, Dr. Curien (Dead), Sophie (zombie); then back to Rogan / No Player. Under the
      name: "Extra Characters", "Plays as Researcher / Sophie" (Sophie (zombie): "PC models: Sophie's body"). Both menu
      styles, 2 and 4 panels (4 panels: "Player N - Extra").
- [ ] Play Chapter 1 and the ending with an extra (e.g. Dr. Curien + a woman researcher, also 4 players): the
      cut-scenes and the ending show their bodies (Curien with his cane); lives / rounds / damage as the character
      it plays as. Arcade and PC models.
- [ ] The six PC characters play and look exactly as before.
- [ ] High Scores > PC Mode: an extra's row shows its name (long names smaller) with the small picture of the
      character it plays as.
- [ ] Online PC Mode: pick an extra on the Host / Join page (Your Character), play: everyone sees it; restart the
      launcher: the pick is remembered. Both PCs need this build (protocol 20).
- [ ] Languages: the names and lines in ko / ja / ar / he (machine-translated).

### PC Mode extras / cut-scene actors fixes (2026-10-28, pmfix)
- [ ] "Dr. Curien (2)" is now **Dr. Curien (Dead)** everywhere: PC Mode pick, High Scores, Gallery > Cut-scenes >
      Actors (also in ko / ja / ar / he). An old pick / scenecast is kept.
- [ ] Gallery > Cut-scenes > 4-19 Goodbye Curien with Sophie as Rogan's part (and as Player 3 / 4): her middle is
      there (white blouse and belt between jacket and skirt, front and back). Also in Chapter 1-3 scenes, and Sophie
      / a woman researcher picked in PC Mode. Known: small gaps in her skirt between the legs on Rogan's body (his
      stance is wider than hers).
- [ ] PC Mode with Dr. Curien, Dr. Curien (Dead), Sophie (zombie), arcade models, Frame Rate Display Refresh on your
      fast monitor: in the opening by the car their clothes / face have their own colours all the time (not grey
      stone or white patches; also after watching a cut-scene in the Gallery first).
- [ ] PC Mode pick screen (both menu styles, 2 and 4 players): every Extra Character has its **own picture** (made
      from its model at the first start, about a second, kept in the settings folder's pmportraits): head and
      shoulders on a dark ground like the PC's six; High Scores > PC Mode shows its own small face picture.
      Delete the pmportraits folder: they are made again at the next start.
- [ ] (portraits2) The extras' portraits look like the PC Researcher's: same head size and place, head turned to the
      left, each with its own background colour (Dr. Curien grey, Dr. Curien (Dead) maroon); zombie Sophie and Dr.
      Curien show the whole head and a little of the shoulders. The small High Scores pictures show their faces.

## 7a5. Gallery
Main menu -> **Gallery**: categories down the left (Players, Humans, Creatures, Bosses, Items, Cut Content, Cut-scenes),
64 entries (4 players, 10 humans, 26 creatures, 5 bosses, 10 items, 9 cut). Prev / Next, Motion, Hold / Play under
the window; Model, Background, Reset view, Exit on the left.
- [ ] **gallery2:** click each category; Tab / Shift+Tab and 1-6 go through them, Tab past Cut Content opens the
      Cut-scenes page and Tab there comes back to Players. Q / E stay inside the category (the counter at the top right
      says "Creatures 3 / 27").
- [ ] The info panel reads well: name, "Appears: ...", the facts (type code, hit points), the description, the motion
      ("3 / 12:  Walk  (z_walk_g)"). Tell me any name / chapter / hit points that looks wrong.
- [ ] **B** (or Background) cycles Dark, Chapter 1 sky, Chapter 2 sky, Chapter 4 sky, Grey, Blue, White; the skies
      also with PC models.
- [ ] Turning (arrows, pad sticks) and zooming (wheel, + / -, right stick) are smooth, Home / Reset view swings back.
- [ ] **gallery3:** the model stands still at first (no motion, no slow turn); Space plays the motion, it stays on for
      the next entries until you turn it off (and off again after restarting the game). O: slow turn.
- [ ] **T** (or Full view, pad Start) hides the page: the model fills the whole screen (also widescreen and the
      Surface's 3:2), name and category at the bottom, the key prompts fade after about 3 s and come back on a key;
      drag / right drag / wheel still turn / pan / zoom; T or Esc (pad B) brings the page back.
- [ ] Right or middle drag pans (pad left stick), the triggers zoom, R resets (front, first zoom, no pan).
- [ ] Dark background: a soft light behind the model and a shadow under its feet; the corners darker. L moves a side
      light round the model (arcade models). I hides the info panel (bigger window).
- [ ] B: the two new rooms (Bridge, Chapter 1; Mansion corridor, Chapter 2) stand the model in the game's world. Tell me
      if a room's textures look wrong.
- [ ] **K: the Remake layout** (to compare): portraits on the left (orange frame on the shown one, Cut Content dark),
      Q / E category, W A S D / d-pad / click a portrait to pick, the red name and text on the right (Tab: more), the
      boss cards (Chariot, Hangedman, Hermit, Magician; needs the PC files), Esc Main Menu. Keep it or drop it? K again
      goes back to Classic (remembered).
- [ ] Leave the Gallery on some entry and background, quit the game, start again: the Gallery opens there.
- [ ] Items: the golden frog, the life item (its front with the red cross), the coin (seen from above), the axe, the
      clock / deer head / aquarium (Motion shows their damage steps). Some props say "Not in the PC files" with PC models.
- [ ] Cut Content (**cutrestore**): MrG, Syndy, Debu, Lubin, Disiprin, Neozom and Baba now have their textures from the
      arcade data (MrG: masked wrestler; Syndy: woman in black leather with a stick; Lubin: cap, tattooed chest;
      Disiprin: black laced suit and boots; Debu: Bentley-like fat zombie; Neozom: Sam with a clawed chest; Baba: skull
      face, brown clothes). **U** switches them back to the untextured look and again to the textures. Tell me any part
      that looks scrambled (a face on a leg etc.). Hiru (the leech) and the Hermit's second head as before. With PC
      models they say "Not in the PC files".
- [ ] Pad only: LB / RB, LT / RT, X, Y, View, B work, and the controls line at the bottom switches to the pad's.
- [ ] Every entry loads, both model sources; motions play in place. A few names / descriptions / first motions are guesses
      (tell me what's wrong).
- [ ] **Arcade models look like in the game (viewerfix):** the zombies have their bellies (no gap between chest and
      trousers), pale skin and their in-game clothes colours (not green), the researchers have heads, Rogan has his coat,
      the Hangedman his wings, the Hermit and the Magician their face / veins, Samson's saw spins, Sophie blinks.
      Compare an entry with the same creature in a chapter; tell me any that still differs (name + what).
- [ ] The whole model fits the window (no heads or feet cut off), also after Motion; the Murrer starts above ground.
- [ ] **gallery blood (2026-11-03):** G / Shift+G steps Blood Color on Sam: the wounds turn red, green, blue, purple,
      yellow, custom (arcade and PC models). On the Remake page H shows the Blood panel: Custom Red / Green / Blue change
      the custom colour at once; N Rainbow Blood (its Colours / Speed / Creature Blood rows); J Blood Particles. Leave
      the Gallery: Options > Blood Color and Mods > Rainbow Blood are as before (the Gallery's choice is not saved).
- [ ] **gallery dismemberment:** H again (or Z / X) shows the Damage panel. Sam: Head Damage 1 / Off (the head gone),
      Right / Left Arm Damage 1, 2 / Off (the arm gone at the shoulder), Body Damage 1-3 (at 3 the head and both arms
      go, as in play), Legs Damage 1. With Blood Particles on: a blood burst on the part at each step, blood dripping
      from a cut joint. Baba (Cut Content), the players, the people and the bosses: "No damage models for this one",
      the rows grey. A new entry starts whole.

## 7a5a. Jukebox (main menu, under Gallery)
- [ ] Main menu -> **Jukebox**: the menu music stops; Exit (or Esc / pad East) brings it back.
- [ ] **Music**: each song plays, with the place it is from (Chapter 1, Boss: Magician, Name entry ...). Sound: PC shows
      the time and a moving bar (click the bar: it jumps); Sound: Arcade plays the same songs from the arcade board.
- [ ] **Arcade lengths**: the first time you open the Jukebox the arcade sounds are measured in the background (about
      a minute; with Info on: "length: being measured"). Then Sound: Arcade shows time / total and a bar like PC, e.g.
      Chapter 1 "0:12 / 2:32" and ", loops 2:24" after the name. At the end of the bar the song should go on smoothly
      (the bar starts again in the loop part). Click the bar: "Seeking" for a moment, then it plays from there.
      Effects too (Game over 6.6 s: it ends when the bar is full).
- [ ] **Sound Effects** and **Voices**: click a few rows, both sources. Do the names fit what you hear? (Many are
      only "Sample 28" or the file's name: tell me any you recognise.)
- [ ] Play once / Loop one / Loop all / Shuffle do what they say; Prev / Next; Stop; the Music / Effects - and + change
      the volume (same values as in Options).
- [ ] Keys (Up / Down, Left / Right, Enter, Space, Tab, N / P, L, + / -) and a pad (d-pad, South, West, North, shoulders).

## 7a5b. Model Viewer (viewerfix)
- [ ] `Model Viewer.bat` (package) opens; Tab switches arcade / PC; H shows the keys. Outdoors there is a **sky** again,
      and the arcade world has the right textures as you move from area to area (it was all in the first area's set).
- [ ] Y (ride Chapter 1): the zombies look like in the game (pale, bellies); X switches the arcade colour rows (in-game /
      the area's own) to compare.

## 7a6. Secrets (the PC's hidden codes; tip by RUNESA)
On the main menu hold **Ctrl** and type **skidmarx** (again to switch off): Cheat / Edit Char Stats appear under Quit;
then Ctrl + **creature**: Creature Test appears too.
- [ ] Cheat: pick Stage / Scene, Start begins there. Creature Test: turn, step through creatures, "View in Gallery".
- [ ] In play with skidmarx on: Ctrl+1 / Ctrl+2 toggle infinite rounds and lives for P1 / P2 (not online).
- [ ] **Arcade score display** (tip by RUNESA, 2026-10-26): Arcade with Title / Attract on, wait for the title (the
      mansion, HOUSE OF THE DEAD logo). Pull the trigger **3 times** (player 1 / left gun / mouse), then **twice** with
      player 2 (right gun, or player 2's trigger key), quickly, then press Start (key 1 / Enter). In play a white
      **SCORE:00000** line shows at the top over each player and counts up. A single trigger at the title still starts
      the game (after about 1 s now, so a code can be typed).
- [ ] Options -> Gameplay -> **Score Display** On: the same SCORE line in every offline game without the code.
      Online it stays off.

## 7b. Sonic Mode (your fast mode)
Options -> **Sonic Mode**: Off / Frenzy (x1.2) / Overkill (x1.5) / Nightmare (x1.5, other bosses x1.75, the
Magician x2).
- [ ] Play speeds up; cut-scenes, voice lines, title cards, boss intros, results and menus stay at normal speed; the
      music is not sped up.
- [ ] The Magician is noticeably faster than the rest on each preset.
- [ ] Each preset keeps its own High Scores.
- [ ] Any moment that should be normal speed but isn't (or the other way round): note where.

## 7c. Texture packs and custom models (like the SM64 PC port's mods; docs/mods.md)
- [ ] Export your textures: `bin\hotd.exe --export-textures C:\temp\hotdtex` (arcade) or add `--models pc`; the folder
      gets PNGs + index.tsv (the blood ones are named blood-...).
- [ ] Make a pack: `data\mods\MyPack\textures\` with a few edited PNGs (keep the file names), or a `mod.ini` with
      `[recolour]` rules (e.g. `blood = #FFD000`). Start the game: the change shows (blood sprays yellow etc.).
- [ ] Options -> **Mods**: the pack listed by its name, On / Off, Move Up; takes effect at the next chapter start.
- [ ] A custom model: `--export-models C:\temp\hotdobj --model-ids 781`, edit the OBJ, put it in `data\mods\MyPack\models\`.
- [ ] Known: the wounds painted on the arcade bodies stay red (separate skin textures); HUD / 2D sprites not covered yet.

## 7c3. Ragdoll mod, round 2 (2026-10-28; docs/modding.md; since ragrevert only Dismemberment is left of it)
- [ ] Settings: **Dismemberment Off**: shoot zombies' heads and arms: no flying heads, the head / arm stays on the body,
      also on the ragdoll. On: as the arcade.

## 7c2. Mods page and the Ragdoll mod (2026-10-26; docs/modding.md)
- [ ] Options -> **Mods** (Classic and Remake style): rows **Example: Green Blood: Off** and **Ragdoll: Off** (both
      off at first), plus any pack of yours. The help line shows each mod's description.
- [ ] Switch **Example: Green Blood** On, start a chapter: the blood is green. Off again: red from the next chapter.
- [ ] **Ragdoll: On**. Kill zombies: they fall limp (no death motion), legs and arms flop, the body lands on the floor.
      Arcade and PC models.
- [ ] Shoot a dead body: it slides away along your shot and comes to rest (Friction Normal). It stays
      about 8 s after the game would remove it (no blinking), then fades.
- [ ] Ragdoll **Settings**: Ragdoll On / Off, Friction (Rough / Normal / Smooth), Bodies Stay (Arcade, 4-30 s),
      Gravity. Each change works at once in the next kill. **Defaults** puts them back. Quit and restart: kept.
- [ ] Bosses die as before; dogs, Ebitan, frogs keep their own deaths.
- [ ] Online co-op: one PC with Ragdoll On, the other Off: the game stays in step (no desync), scores the same.
- [ ] Note anything odd: a body stuck in the air or sunk in the floor (stairs / slopes), limbs bent wrongly, a body
      that shakes, a body that slides through a wall (known: no wall collision yet).

## 7d. Experimental 4 players (local and online)
Options -> **Players 1 / 2 / 3 / 4 (experimental)**; "Player 3/4 Input (4P)": gamepad by default (light bar
green / yellow), or a light gun / own mouse, or Keyboard.
- [ ] **Keyboard for P3 / P4** (the keys work with any input setting, like P2's): P3 = W A S D aim, F fire, R reload,
      3 joins; P4 = I J K L (or keypad 8 4 5 6) aim, H (keypad 0) fire, Y (keypad .) reload, 4 joins. They don't
      disturb P1's mouse or P2's arrows / Space / Enter. Typing a name in the name entry doesn't move P3 / P4.
- [ ] P3 / P4 pad settings (Controller: Player 3 / 4 pages) and a light-gun calibration (F9) of P3 / P4 are still
      there after restarting the game.
- [ ] **New:** P3 / P4 top bands in a boss fight sit right under the boss life bar (not a gap lower); "3P" / "4P"
      digits are green / yellow like the "P" (not whitish); HOLD YOUR FIRE! and RELOAD are red, CONTINUE? and PRESS
      START BUTTON white for all four players (as P1 / P2's).
- [ ] **New: prompts follow the models (Arcade / PC).** Arcade models: as above (RELOAD with the "shoot outside the
      screen" line, PRESS START BUTTON on an empty seat). PC models: the 1998 PC's own prompts for all four players:
      RELOAD alone (no "shoot outside" line, P1 / P2 too), HOLD YOUR FIRE! red, CONTINUE? white, and no PRESS START
      BUTTON on an empty seat (the PC shows none in a game). With 1-2 players and arcade models nothing changes.
- [ ] **New:** all players out with 3-4 players: P3 / P4's small CONTINUE? sit at the top corners, not on top of the
      big CONTINUE? in the middle.
- [ ] **New: Teammate Flashes** (Options -> Gameplay -> Advanced): with 3-4 players (Auto = Reduced) P1's shot flash is as before,
      the others' flashes are smaller and shorter, and shots on the same spot at once show one flash. Full = all
      full size, Off = only P1's. With 1-2 players nothing changes (Auto = Full). P3 / P4 now have a shot flash too.
- [ ] P3 / P4 RELOAD (red, under their top band) blinks when their gun is empty, with the "shoot outside
      the screen" line after 2 s; when P1 (or P2) on the same side is empty too, P3's (P4's) moves below P1's (P2's).
- [ ] P3 / P4 hit by a shaking hit: their blood splash under their band, the hit sound and voice (P3 Rogan's, P4 G's).
- [ ] Boss fights: P3 / P4 rounds and lamps sit under the boss life bar (not over it), and don't jump down the screen
      when P1 / P2 get hit (their blood splash).
- [ ] High Scores: the arrows reach "4 Players"; after a 4P game its names / scores are there.
- [ ] P3 green / P4 yellow crosshairs and fire lamps; P3 / P4 rounds + lamps at the top corners (both model sources,
      widescreen and 4:3).
- [ ] P3 / P4 shoot, hit, score; start button joins mid-game.
- [ ] Cut-scenes with only P3 / P4 playing show "Rogan 2" / "G 2".
- [ ] Enemies attack all four (P3 / P4 take damage and lose lives); hostages / kills / bonuses credit the real shooter.
- [ ] Bosses have more health with 3 / 4 players (x1.25 / x1.5, tuned so a team with weaker players still wins): do
      the fights feel fair? Too long / too short?
- [ ] P3 / P4 CONTINUE? countdown at their corner; the team's game over only when all four are out; results show four
      scores; the name entry runs twice (P1 / P2, then P3 / P4) into a separate 4P top 10.
- [ ] 4P: only players 3 / 4 playing (1 / 2 never joined), all out: GAME OVER and the route map look normal (no
      walker then, nothing odd).
- [ ] Known, not done yet: P3 / P4 keys are fixed (no key rebinding page yet, for any player).

### 4P cut-scene bodies
- [ ] With 3 / 4 players the cut-scenes 00 / 02 / 06 / 07 / 09 / 10 / 11 / 12 show players 3 / 4 as Rogan 2 / G 2
      beside players 1 / 2, all four visible and standing on the floor (not inside each other). Chapter 2 has 11 (a
      jump down soon after the start), 12 (the bookcase kick) and 10 (a jump down later on); Chapter 1's 07 (the jump
      down from a ledge) is on a different route than the default one.

### Online 4 players (two PCs with two seats each, or up to four PCs)
- [ ] Host with Players 4 (seats 1 + 3 on the host, 2 + 4 on the friend; the friend's Player 2 input plays seat 4).
      The waiting room shows the friend; Start Game: all four crosshairs and lamps work on both PCs, equal scores.
- [ ] Drop the friend's line, then rejoin: the game catches up and keeps four seats. Play Alone still starts a 4-seat game.
- [ ] **Up to 4 separate PCs (one seat each):** Host page -> "4P Online: Up to 4 PCs" (or `--net-pcs 4`). Up to three
      friends join with the code; the host's waiting room lists each with its ping; the LAN list shows n / 4. Start
      Game: all four seats play on every PC with equal scores. Drop one friend and rejoin: only that seat catches up.
- [ ] The joiners' waiting rooms list every player with name and ping too ("Rogan, host", "you").
- [ ] **Mixes (2026-10-27): a PC with two players among up to 4 PCs.** Host page with "Up to 4 PCs": **Your Seats 2**
      (the host plays P1 + P3; the Player 2 input plays P3). A friend's Join page: **Your Seats 2** before Join (gets
      two seats when two are free, e.g. P2 + P4). Try 2 + 1 + 1 (host two, two friends one each) and 1 + 2 + 1. The
      waiting rooms show the second seat as "Name, P1's PC". All crosshairs move on every PC, equal scores.
- [ ] Four PCs: close one friend's game mid-play (or pull its cable): the others go on, its seat(s) empty; it joins
      again with the same code and is back in its seat(s).

### Cut-scene viewer (Gallery -> Cut-scenes, the last category on the left)
- [ ] Pick 2-12 Reload kick, Players 4, Play: after a moment the scene plays with four bodies. At its end
      "Over" shows: R plays it again at once; Esc goes back to the list. Play it again from the list: works (no crash).
- [ ] While it plays: Space pauses and goes on, S slow motion (and back), R from the start, Esc back to the list.
- [ ] Models: PC, then Play the same scene: the PC models.
- [ ] A few others (1-00, 1-06, 1-07, 2-09, 2-10, 2-11) with 4, then 1 player: the right scene each time.
- [ ] Mouse only (click the rows / Play), keyboard only (arrows + Enter, Page Up / Down, P, M, F5) and pad only
      (shoulders pick, X players, Y models, Start plays) all work.
- [ ] `hotd --scene-view 2:12` opens the scene straight away; Esc goes to the Cut-scenes page.
- [ ] After using it, a normal Arcade game still plays as before (no route forced, your players / models).
- [ ] Clean View (On by default on the page): while a scene plays there is no HUD, lamps, crosshairs or mouse
      cursor; the keys' line shows small at the bottom for 2 s, then fades; any key shows it again. At the end
      nothing is written over the picture until you press a key.
- [ ] Clean View: Off (the page, or C while watching): the old look (HUD, crosshairs, the lines at the top left).
- [ ] Subtitles: Off (the page, or T while watching): a scene with talk (2-12) shows no captions. With Clean View On
      too: only the picture (the keys' hint fades after 2 s). Subtitles On + Clean View On: captions, no HUD.
- [ ] Trailer Mode: F7 in a game: "Trailer Mode On (F7)" shows for 1 s, then no lamps, score, ammo, prompts,
      crosshairs, mouse cursor or captions; shooting still works. F7 again: all back. Restart: Off again.
- [ ] Options > Gameplay > Advanced > Trailer Mode On: a new game starts with it on, and it stays on after a restart.
- [ ] Trailer Mode: the fades to black (chapter start, deaths) still show; Esc's pause menu and its cursor still show.
- [ ] Photo Mode (photomode, 2026-11-09): F8 in play (and in a cut-scene, Boss Mode, Survival, TotD 0): everything
      stops, the music holds; the help line at the top. W A S D / Q E move, hold a mouse button and drag to look, the
      wheel / + / - zoom (FOV in the help line), 0 / middle click resets it, R the camera, C slow camera, H hides the HUD
      and the help line, F12 saves a full-size PNG without the help line ("Screenshot saved"). F8 / Esc: the game goes on
      from the same moment, the music from the same note, no shot fired by the clicks. Pause menu -> Photo Mode: Esc
      goes back to the pause. A pad: sticks, LB / RB, d-pad zoom, A screenshot, Y HUD, X reset, B leaves. Online: the
      pause shows "Photo Mode (offline only)" greyed and F8 shows "Photo Mode is for offline games only".
- [ ] The scene's music: 2-12 starts with Chapter 2's music right away, and again on R (no blip or menu music
      from the fast-forward).
- [ ] Record a clip: `hotd --scene-view 2:12 --players 4 --models arcade` in fullscreen, R, record: the clip shows
      only the scene.
- [ ] Players 3 / 4 cut-scene variations: open `private\scene4p\index.html`, watch each scene's A / B / C / D
      (4 players / 3 players buttons) and tell me your pick per scene (e.g. `00:C, 02:D`).
- [ ] After a pick is set (launcher.ini `scene4p=02:D`): `hotd --scene-view 1:2 --players 4` shows that variation;
      `--scene4p 02:A` shows the old look again.
- [ ] Fixed 4-player cut-scenes (new default "F"): open `private\scene4p\fixed\index.html` (before / after side by
      side). In the viewer (Gallery -> Cut-scenes, 4 players) watch all 16 scenes: nobody inside a wall, a door,
      another player or the floor; players 3 / 4 never stand in front of the camera. 2-10 and 4-18 show only 2 players
      on purpose (no room). Tell me any scene that still looks wrong.
- [ ] 4-19 Goodbye Curien, 3 and 4 players: when the camera turns, players 3 / 4 stay where they stand (they do not
      turn with it). Player 3 stands on Rogan's other side (away from G), player 4 beside G on the far side.
      3-15 / 4-17: players 3 / 4 no longer swing round at the start of the scene.
- [ ] Your notes on the 4-player cut-scenes: open `private\scene4p\fixed2\index.html` (one line per scene, before
      and after). Check: 1-00 P3 / P4 there from the first frame of the shot in front of Rogan; 1-03 / 1-04 / 1-07 one
      after another; 1-06 both behind G by the bookshelf; 1-08 no head in the wall when the camera turns; 2-09 / 2-10 /
      2-11 the old looks back (2-11 also with 3 players, Rogan does not hit P3); 2-14 P4 right of Rogan; 3-15 P4 in the
      shot. In the game too (4 players, Chapter 1 and 2).

## 8a0. Your DualSense notes of 10-30 (padfix)
- [ ] Push the crosshair past the screen edge with the stick (or the gyro) and pull R2: it reloads, and the crosshair
      stays just off the edge (no jump to the middle). Come back with the stick.
- [ ] Click the **left stick**: the crosshair goes to the middle. Holding it still slows the aim (precise).
- [ ] The sticks are 20 % faster. **Options -> Controls -> Aim Sensitivity: Player 1** (50-200 %, a bar): try it,
      tell me the number you like.
- [ ] **Sideways grip is gone** (2026-10-30): the Controller page's Style list is Auto / Classic / Motion Only /
      Motion + Stick / Touchpad, no "Grip" row. If your settings had Sideways, the pad now starts as Motion + Stick.
- [ ] **Shake to reload** (reworked 2026-10-30, default Medium): with the pad on your lap, a quick flick of the wrist
      (roll or tilt), a quick jerk up / down (pad flat), and a quick jerk side to side should each reload once. Normal
      aiming, even fast turns, should never reload. Too easy / too hard? Try Low / High and tell me which.
- [ ] Menus with **Player 1 Input = Mouse** and Player 2 = Gamepad: the touchpad / gyro still move the bone hand.
- [ ] **Button Style** with a Switch 2 / GameCube pad (or Button Icons = Switch 2): Xelu is grey in the list and the
      help line says Kenney's icons are shown instead.

## 8a. DualSense aim feel (reworked after your "not fun" test; do this first)
Defaults: Auto -> Motion + Stick, Feel Preset Balanced. Options -> Controls -> Controller P1 -> **Aim Test**.
- [ ] Pad flat on the desk -> **Calibrate Gyro** (2 s still): "Gyro Drift" stays under ~0.2 deg/s.
- [ ] In game: a ~36 deg wrist turn should cross the screen. Too slow / fast? Try Sensitivity 4 / 6 / 7 and tell me the number.
- [ ] Turning AND rolling the wrist both move the crosshair the right way (Gyro Space "Player"; compare "World").
- [ ] Holding still: jitter? (Gyro Smoothing Medium / Gyro Tightening 1). Slow careful aim: laggy or "stuck"?
- [ ] Presets Precise / Balanced / Fast: which is closest?
- [ ] **Shake to reload**: a quick flick of the wrist or a jerk of the pad reloads; never by accident while aiming.
- [ ] Classic (sticks): speed, curve, edge turbo, dead zone OK?
- [ ] Tell me: USB or Bluetooth; Smooth Motion on; what exactly felt bad before (lag / jitter / slow / drift / direction).

## 8. Controllers and light guns (in this build; nothing tested on real hardware yet)
Options -> "Player 1 Input" / "Player 2 Input": Mouse / Keyboard / Gamepad / Light Gun; the "Controller: Player
1 / 2" pages hold each style's settings (see README "Controllers and light guns").
- [ ] DualSense (USB, then Bluetooth): it's recognised; the light bar turns red (P1) / blue (P2).
- [ ] Styles: **Classic** (stick aims, other stick fine-adjusts), **Motion Only**, **Motion + Stick** (the default for
      gyro pads), **Touchpad** (Pointer or Trackpad; also on top of the
      others). Does the aim feel direct (no float, no lag)? Is "up" still up when you tilt / roll the pad (Gyro Space:
      World)?
- [ ] Buttons: R2 shoots on a light press (fire point setting), L2 / L1 / East / West reload, North tap = recentre /
      hold = ratchet, R3 / L3 click recentre, L3 held = precise aim, Start = join / pause; shooting off-screen reloads.
- [ ] Adaptive trigger resistance (strength setting), vibration when you get hit / hit an enemy / reload (separate
      strengths).
- [ ] The pad drives the menus (d-pad / stick, Cross = OK, Circle = back).
- [ ] Unplug the pad mid-game: the game pauses with a message; plug it back, Start continues.
- [ ] Two pads for two players; P1 mouse + P2 pad.
- [ ] If you have them: Xbox pad (sticks), Switch Pro / Joy-Con (gyro; a single Joy-Con held like a gun in Motion
      Only?), DualShock 4.
- [ ] Light gun (if you ever borrow one): F9 calibrates (shoot two targets), each gun its own crosshair.

### Keys & Buttons (rebinding; Options -> Controls -> Keys & Buttons)
- [ ] With nothing changed, every key and pad button works exactly as before (1 / 2 start, P pause, player 2's
      arrows / Space / Enter, the pad's R2 / L2 / Start).
- [ ] Click "Key: Fire", press a key: the row shows it and it fires in the game. Press the same key on that row
      again: it is taken off.
- [ ] Waiting for a key: Esc cancels, a mouse click cancels, doing nothing for 5 s cancels (Status says so).
- [ ] Give two actions the same key: the row shows "!" and Status names the other one.
- [ ] Player row: switch to players 2 / 3 / 4 and change one of their keys.
- [ ] Page 2 (the arrow by Exit): "Pad: Fire", press a pad button (also a trigger, a back paddle if you have one):
      it shoots in the game; the old buttons still do what the row lists.
- [ ] The whole page works with only the keyboard (arrows + Enter) and with only the pad (d-pad + Cross / A).
- [ ] Reset Keys / Reset Pad / Reset Everyone put the defaults back; quit and start the game again: your changes
      are still there.
- [ ] Online co-op with a changed fire key or pad button: it shoots for you on the other PC too.
- [ ] **New: keyboard aim online.** Online game, Player 2 input = Keyboard on your PC: the arrows (or the aim keys you
      set for player 1 / 2) move your crosshair, and the other PC sees it move the same way. 4P on two PCs: player 2's
      keys (arrows, Space, Enter, 2) play your second seat.
- [ ] **New: CPU teammates online.** 4P on two PCs: on your PC set Player 3 (host) or Player 4 (friend) to CPU in
      Co-op > Human-CPU seats; that seat plays by itself on both PCs, equal scores at the end. Up to 4 PCs: the host
      sets a seat to CPU; it plays until a friend joins that seat, then the friend has it.
- [ ] Local game with CPU teammates: the same chapter played twice with the same settings plays the same way.
- [ ] **New: smarter CPU teammates.** Play a chapter with a CPU partner (Easy, then Normal, then Hard):
  - [ ] its crosshair glides to an enemy (no jumping), waits a moment, then fires a few shots, not a constant stream;
  - [ ] it shoots the enemy coming at you / an axe or barrel thrown at you before enemies further away;
  - [ ] it leaves alone enemies far in the background, bodies already falling, crates, tables and other scenery;
  - [ ] no shots while the camera is moving or with nothing dangerous on screen; its crosshair rests on its side;
  - [ ] it reloads in quiet moments (not in the middle of a fight with bullets left);
  - [ ] it never shoots a hostage or a researcher;
  - [ ] with several enemies, two or three CPU partners spread out instead of all shooting the same one, and leave
        the one you are shooting to you;
  - [ ] Easy is slow but still helps; Hard is quick and sure but still looks like a person;
  - [ ] the Magician: CPU partners hit the parts of him that take damage and he goes down.
- [ ] Online name entry (a high score): letters are picked by shooting; typing on the keyboard does nothing there now.

## 8b. Linux / Steam Deck (linuxport, linux2)
- [ ] Windows unchanged: Add to Steam and the desktop shortcut still work as before.
- [ ] **New: online needs the new build on every PC** (protocol 10, new encryption): an older build is told "another
      version". Host and join between two of your Windows PCs as before: it works as before.
- [ ] **New: Linux package.** In WSL: `tools/make_linux_package.sh` (makes `private/portable/HOTD ReWrithe Linux`
      and a .tar.gz). On a Linux PC: unpack, put your files in data/arcade, data/pc, data/music, run ./hotd.sh.
- [ ] Steam Deck (if you have one): Desktop Mode, copy the folder, Steam -> Add a Non-Steam Game -> hotd.sh, play in
      Game Mode: pad aim, R2 shoot, touch screen as a gun (Auto Reload on). Note: the WSL build needs a new glibc;
      if it does not start on the Deck, a Steam Runtime build is needed (docs/porting.md).
- [ ] Cross-play: a Linux PC (or the Deck) and a Windows PC in one online game (either one hosting).

## 9. Report back
For each failure: what / where / model source / sound setting / screenshot or video timestamp. The contact sheet of
every chapter next to MAME is `private\checkpoints\allch_20261019b\index.html` if you want to compare by eye.

### Options: the Advanced page (2026-10-21)

- [ ] Options -> Gameplay: the last row is **Advanced  Open >**; it opens a page with Game Speed, Music Numbering,
      Teammate Flashes, 4P Boss Hit Points and Guards (Teaser Cuts). They are gone from Video, Audio and Gameplay.
- [ ] Each row there still changes and is kept after a restart, as before.
- [ ] Esc (or Exit / B) on the Advanced page goes back to Gameplay.

### Options: lives / auto reload for 3P and 4P, resolution (optfix, 2026-10-21)

- [ ] 4 players (Co-op > 4 Players): set Options -> Life to 1, then to 5: all four players start with that many lamps
      (3P green and 4P yellow too), and a continue gives them that many again.
- [ ] Auto Reload On with 4 players: 3P's and 4P's guns reload by themselves when empty (as 1P / 2P).
- [ ] Online (4P on two PCs): the host's Auto Reload is used for every player, whatever the joiner has set.
      Both PCs need this new build (protocol 11).
- [ ] PC Mode with 4 players: Continues counts players 3 / 4 too (each start and continue takes one).
- [ ] Options -> Video -> Resolution (or Options' Resolution row) in fullscreen: a list of your monitor's modes.
      Pick one: the screen switches at once and asks "Keep this resolution?"; Keep keeps it; Revert, Escape or
      waiting 10 seconds goes back. "Auto (Desktop)" is the old borderless fullscreen.
- [ ] The same in a window (F11 first): a list of window sizes that fit your desktop; the window changes size at once.
- [ ] Both choices are kept after a restart (and F11 goes into your chosen fullscreen resolution).

### Game Text: English / Japanese (gametext, 2026-10-21)

- [ ] Options -> Gameplay -> Game Text: Japanese, then start Chapter 1: the chapter card shows the Japanese title, the
      subtitles are the arcade's vertical Japanese ones on the right (arcade and PC models).
- [ ] The attract's how-to-play (title / attract on): the Japanese speech bubbles and the Japanese RELOAD hint.
- [ ] Back to English: the English subtitles at the bottom, as before. The setting is kept after a restart.
- [ ] Online: the host's Game Text is used on both PCs. Both PCs need this new build (protocol 12).

### QA sweep (finalqa, 2026-10-21)

- [ ] Remake menu at 3:2 or 4:3 (Options): the tab names "Controls" and "Gameplay" sit inside their paper tabs.
- [ ] Chapter 2, the Hangedman fight: do you see a thin bright diagonal line near the moon? (Still open; tell us if
      it also shows in your MAME / arcade video.)
- [ ] 2 players, Chapter 1 (the room after the first stairs) in 16:9: weapons at the left edge of the picture. Tell us
      if this looks wrong to you compared with the arcade.
- [ ] Play a whole game once (Arcade, real lives): continue, game over, name entry, the ending, back to the menu.

### The flying eyeball and Sophie's waist (stubs, 2026-10-21)

- [ ] Chapter 1: shoot a zombie in the face from close by. Sometimes an eyeball flies out, bounces on the floor and
      leaves small blood spots (arcade and PC models).
- [ ] Chapter 1 opening, arcade models: Sophie crawling on the ground has no gap at her waist.

### Cut-scene viewer: pause sound, Goodbye Curien, Free Cam (cutfix, 2026-10-22)

- [ ] Gallery -> Cut-scenes, play any scene, press Space: the music and sounds stop. Space again: they go on from the
      same point. R (replay) starts the sound cleanly. Try both Models: Arcade and PC.
- [ ] The list has a new last row "4-19 Goodbye Curien" (the ending after the Magician). Play it with 4 players:
      players 3 and 4 follow G a moment later, standing beside / behind him, nobody overlapping. Also 3 players.
- [ ] Press F while a scene plays: Free Cam. W A S D / Q E fly, hold the right mouse button and drag to look, the wheel
      zooms, Shift faster, Ctrl slower. Pause (Space) + Free Cam for still shots. F again: back to the scene's camera.
      With a pad: click a stick, left stick move, right stick look, triggers down / up. M is slow motion in Free Cam.
- [ ] Tell us if parts of the world vanish when you look somewhere the scene never shows.

### Shooting at the screen's edge on a tablet (edgereload, 2026-10-22)

- [ ] Surface (touch): tap enemies right at the screen's edges and corners (widescreen): every tap is a shot, no reload.
- [ ] Touch reload: keep one finger on the screen and tap with a second finger: reload.
- [ ] Pen: tap at the edges: shots. The pen's side (barrel) button or its eraser end: reload.
- [ ] Aspect "original 4:3": a tap or pen on the black side bars reloads (that is off the arcade's screen).
- [ ] Mouse / trackpad: pushing the cursor all the way against an edge and clicking still reloads (as before); one
      pixel inside the edge is a shot.

### The Gallery in the Remake menu style (regallery, 2026-10-22)

- [ ] Menu Style Remake, Extras > Gallery: a dark stage, the model large in the middle with a soft light behind it and
      a faint reflection on the floor; categories and the list on the left, the name and description on the right, the
      keys along the bottom. No black bars at 16:9, 16:10, 3:2 (4:3 also works).
- [ ] Q / E (pad LB / RB) or a click in the list: the model fades in, the text on the right fades and slides. A click
      sound each time. Tab or a click on a category: the list slides in.
- [ ] Drag turns, right-drag pans, the wheel zooms; Space plays / pauses the motion (off at first), M next motion,
      L light, B background, S or the Model row: arcade / PC. Click the Viewer rows and the key caps too.
- [ ] Bosses: the weak-point card shows under the stats. Cut Content: the developers' name and texture rows.
- [ ] T: only the model, the name and the keys (they fade after a few seconds); T or Esc: the page back.
      I: the file hidden, a wider stage; I again: back.
- [ ] Cut-scenes (last category or C): the same look; Play and Attract Demo still work; a category click goes back.
- [ ] K: portraits layout, K: the old Classic page, K: the new page again. Menu Style Classic: the Gallery is the old
      Classic page, unchanged.
- [ ] With a pad: the key caps show the pad's buttons.

### Languages (lang, 2026-10-26)

- [ ] Options > System > Language: English, 한국어, 日本語 (samples: only some menu lines are translated). Pick 한국어:
      the main menu, its help line, Options' tabs and Quit's question show Korean at once, in both menu styles
      (Classic and Remake); nothing overlaps. Back to English: everything exactly as before.
- [ ] The dropdown (Remake style) lists the languages by their own names.
- [ ] Quit and start again: the language is kept. (A new settings folder on a Korean / Japanese Windows starts in
      that language by itself.)
- [ ] Korean or Japanese on, play Chapter 1 to the first line "Others are still inside. Save them.": it shows in
      Korean / Japanese instead (arcade and PC models). Pause online: "P2 PAUSED" shows translated.
- [ ] For translators: edit data\lang\ko.txt (e.g. change "아케이드"), press F5 in the menu: the change shows.
      `hotd --lang-check ko` prints what does not fit; docs/translating.md reads clearly.

### Right-to-left languages (rtl, 2026-10-26)

- [ ] Options > System > Language: العربية (Arabic) and עברית (Hebrew) are in the list (samples: a few menu lines).
- [ ] Arabic: the main menu's words are joined letters reading from the right (not separate letters, not backwards),
      both menu styles. Hebrew: the words read from the right.
- [ ] Options pages (both styles): each row's name is at the right, its value / switch at the left; the tabs start
      at the right; in the Remake style Q / E (and Left / Right on the tabs) move the way the arrows point.
- [ ] The controls line ("Enter/A ... Esc/B ...") reads from the right with the Latin keys readable.
- [ ] Back to English (or 한국어): everything looks as before.

### Gallery fixes (galfix, 2026-10-26)

- [ ] Creatures > Harris (the ball-and-chain zombie): arcade models show dark red-brown skin and black clothes as in
      Chapter 3, not the patchy red; PC models: the PC's own red look.
- [ ] Creatures has no "Devilon" any more; Bosses > The Hangedman says Chapters 1, 2 and has his wings (arcade and PC).
- [ ] M through the motions of a few creatures (Kage, Kageta, Taalu, Neopetit, Harris, Simon, Robert): no Magician,
      Chariot or other creature's poses; Kageta / Kage move like the monkey creatures, as in the game.
- [ ] Bosses > The Chariot: large in the view (his body fills it; the halberd may reach out of the picture). Samson,
      Gilmore and Burner also a little larger.
- [ ] Mouse over the model (Remake style): the bony hand stays, see-through, instead of vanishing; over the page it is
      solid. The hand is a little smaller and smoother with a soft shadow everywhere (Classic and Remake menus), and a
      click still lands where the finger tip points.

### 4P cut-scenes, ending, HUD (hudfix4, 2026-10-26)

- [ ] 4 players, Gallery > Cut-scenes > 4-19 Goodbye Curien: P3's cap and P4's beard / glasses stay on while G talks
      and the camera turns; the four stand in an even row, the outer two turn outward, P4 keeps the gun in the right hand.
- [ ] Gallery > Cut-scenes: new row 4-20 Staff roll plays the credits; Replay shows the scenes again (not dark).
- [ ] A 4-player game to the end: no crosshairs in the farewell and the credits; at the end of the credits the car
      stays behind the gate's pillar.
- [ ] Chapter 4, Dr Curien lying on the floor dying: his back is one coat (no inside pieces).
- [ ] 3-4 players in a cut-scene: P3 / P4's HOLD YOUR FIRE! sit in the black top bar like P1 / P2's in the bottom one;
      an empty P3 / P4 seat's PRESS START BUTTON blinks together with P2's.
- [ ] 3-4 players: P2 hurt shows a blue splash / blue claw marks; 2 players: still red.
- [ ] PC Mode with Sophie: in a cut-scene all her bullets go away.
- [ ] Attract demo (16:9 and 4:3): from the title to the how-to-shoot demo the screen goes all black for a moment, no
      black square in the middle.

### CPU teammates and 3-4 player difficulty (cpuai, 2026-10-26)

- [ ] Play with a CPU player 2 on Normal: its sight moves like a person's (overshoots a little, corrects, shots spread
      over the body, now and then a miss or a late reload), not glued to one point.
- [ ] Easy / Normal / Hard feel different: Easy slow and often off, Normal a fair partner (you still do most of the
      work), Hard quick and sure.
- [ ] Bosses with a CPU: it shoots the Chariot's hand while armoured and waits while the armour falls; the Hermit's and
      the Magician's weak spots; it keeps shooting the Hanged Man (not the two people hanging from him).
- [ ] Researchers / hostages next to creatures: a CPU never hits one (it waits or shoots another part of the creature).
- [ ] 3-4 players: creatures take more shots and the game feels harder; Options > Gameplay "4P Enemy Toughness" and
      "4P Boss Hit Points" (both +50 % a player now) change it; 0 % = as before. 1-2 players play exactly as before.
- [ ] 4 players: shoot a researcher on purpose: you lose the life (not another player).

### Cut-scene actor swap (actorswap, 2026-10-26)

- [ ] Extras > Gallery > Cut-scenes: four new rows Rogan's part / G's part / Player 3's part / Player 4's part (Classic and
      Remake menus). Remake: Left / Right or a click changes the actor; Classic: a click; both: keys 1-4 (Shift: back).
- [ ] Pick e.g. G for Rogan's part and Sophie for G's part, Play: the scene plays the same, only the bodies change. Try
      arcade and PC models, and 4 players (P3 / P4 parts) in 1-06, 2-12, 4-19 Goodbye Curien.
- [ ] While watching: A (pad LB) opens the Actors panel: Up / Down the part, Left / Right the actor. Paused, Rogan's /
      G's part change at once; P3 / P4 change when it plays on. A closes it.
- [ ] A normal game (Arcade, Chapter Select): nothing changed, everyone is themselves.

## Boss Mode start, skipping and pausing cut-scenes (bossbug, 2026-10-26)
- [ ] Boss Mode: play a boss, Esc > Exit to the menu, then Arcade: Chapter 1 starts (not that boss). Same with PC Mode
      and Chapter Select.
- [ ] Boss Mode Hermit: Dr. Curien talks in the full green room (not only his platform). Check the other bosses too,
      arcade and PC models.
- [ ] **Changed (skipfix, 10-27):** hold-to-skip works only in cut-scenes the arcade does NOT let you skip, while the
      black bars show (e.g. Boss Mode Hermit: Dr. Curien's talk). Press and hold the trigger (or fire / start key,
      pad A / right trigger) 2 s (fix28c: was 1.5 s, before 3 s): a small red ring in the middle of the bottom black bar fills, then the scene skips.
      Let go early: the ring goes away, nothing skipped.
- [ ] Nothing shows by itself: the chapter card (CHAPTER 1 ...) shows no ring, holding the trigger through it does
      not fill one, and a trigger still held when a scene starts must be let go and pressed again.
- [ ] In a scene the arcade can skip (Chapter 1's opening after the gate), holding shows no ring; a quick press
      still skips it the arcade's way.
- [ ] 4 players: the ring never covers a HOLD YOUR FIRE! (they sit in the bars' corners), 16:9 and 4:3.
- [ ] Options > Gameplay > Hold to Skip Cut-scenes: Off: no ring, no skip; On again: it works. Translated in
      ko / ja / ar / he.
- [ ] A quick trigger press still skips the scenes the arcade lets you skip (as before).
- [ ] **New (fix28, 10-27):** save a researcher / hostage (e.g. Chapter 1, the man who gives a life): while he runs to
      you and gives the life-up / item, holding the trigger shows no ring and skips nothing (until the play is back).
- [ ] **New (fix28):** start Arcade with 1 player, join with player 2 (start key / pad Start) mid-chapter: the next
      cut-scenes show both characters as in a 2-player game (the arcade does the same). PC Mode: P1 Rogan, P2 "No
      Player", P2 joins: the cut-scenes show G for P2 (before: a second Rogan). 4P: player 3 / 4 joining shows beside.
- [ ] **New (fix28):** Options (Remake style) > Video: put the focus on Models or Aspect (Gameplay: Blood
      Color): a small panel of pictures shows what each choice looks like (the game's own picture of Chapter 1, the
      current one framed red; Models: the researcher's face; Aspect: the bars hatched). The first time it says "Making the picture..." for ~30 s (made in the background, the
      menu stays smooth), then the pictures stay (settings folder `previews3\`). Gameplay > Hold to Skip's help says 2 s.
- [ ] **New (fix28c, 10-27):** the same pictures have no red ring / circle and no red edge any more (Aspect: just the
      grey hatched bars). Cut-scene hold-to-skip takes 2 s now (the ring fills over 2 s).
- [ ] Esc / P / pad Start in a cut-scene pauses it (sound paused too); Continue goes on where it was.
- [ ] 4 players: when player 3 (or 2 and 3) is out, the next cut-scene still shows player 4 beside Rogan.
- [ ] 4 players: P3 / P4's HOLD YOUR FIRE! is gone while a chapter's title card shows.
- [ ] 4 players: P3 / P4 joining or continuing play the same gun-cock sound as P1 / P2.
- [ ] 4 players: chapter results show P3 / P4's bullets and lamps under their score boxes.
- [ ] 4 players: P3 / P4's hit splashes (claw marks) show whole, at P1's / P2's spot, not cut at the bottom.
### Leftovers (leftovers, 2026-10-26)

- [ ] Ragdoll mod on (Options > Mods): in Chapter 1's first hall, shoot dead bodies towards the back wall: they stop at
      the wall and slide along it, never half inside it (arcade and PC models).
- [ ] PC models, the very end of the staff roll (the car in the gate): the right pillar covers the car's front, the
      number plate still shows.
- [ ] Jukebox, both menu styles: Page Down / Page Up (or the arrows) turn the list cleanly, no stutter in the Remake
      style.
- [ ] Arabic or Hebrew (Options > System > Language): the PC style's Options page, Auto Reload's 1P / 2P "Off" fits in
      its boxes; the Jukebox's rows stand at the right (numbers with Info at the left); in the Remake style the key
      prompts sit at the bottom left.
- [ ] A fresh install (no settings): blood is red; the Example: Green Blood mod is off until you turn it on.
- [ ] Options > Mods: switch Ragdoll on: only Ragdoll turns on (Example: Green Blood stays off); switch it off and on a
      few times, then Example on and off: each row switches only its own mod. Under each mod a grey line shows what it
      is, who made it and its version.
- [ ] Ragdoll's Settings page: Ragdoll On / Off at the top, then Friction (Rough / Normal / Smooth), Bodies Stay,
      Gravity, Defaults. No Shot Push, no hockey / ice words.
- [ ] Ragdoll on, Friction Normal: shot bodies slide a little and come to rest; they lie on stairs / slopes without
      floating or sinking, and stay still (no twitching) once settled.
- [ ] Ragdoll on (ragall): kill dogs, Murrers, bats, frogs, monkeys / Rubin / Drake / Moody, Ebitan, Taran, rats and the
      axe / kit / iron-ball / chainsaw zombies: each falls limp from its own body parts and lands on the floor (bats
      and ceiling creatures fall all the way down); shoot the bodies: they slide; a blown-off head tumbles. Bosses keep
      their own deaths.

### Menu feedback (menufb, 2026-10-26)

- [ ] Video > Live Background Off (Remake menu): the background still changes scenes, as still pictures that fade into
      each other every 8 s. The very first time with it Off the menu takes the pictures by itself (about half a minute
      of the attract, unseen); after that they load at once.
- [ ] Long lists show a scroll bar at the right (Video's rows, the Frame Rate list, Classic's pages): drag its thumb,
      click the track, or use the wheel.
- [ ] Video > Resolution in fullscreen: only your screen's shape (plus 4:3 / 16:10 if Aspect says so), no repeats;
      "Show All" at the end lists every mode, "Show Fewer" goes back.
- [ ] Video > Crosshairs > Firing Range: pick Player 1-4, then crosshair, colour, size, thickness for that player.
      Shoot the targets with the mouse (or a gun) and with each player's pad: hits burst in the player's colour with
      points, misses leave holes, the score line counts hits / shots. In the game each player has their own crosshair.
- [ ] Teammate Crosshair Flashes (Firing Range and Gameplay > Advanced): Full by default. Local games: every player's
      flash is full. Online: Reduced / Off only changes the other PC's player. On the range the mouse player is "you".
- [ ] Audio: Sound Effects / Music Volume have bars: drag or click them (both menu styles).
- [ ] Audio > Menu Music: a new song plays at once; the short clear / game over jingles are gone from the list.
- [ ] Each settings page ends with "Reset to Defaults": it asks Reset / Cancel; Reset puts that page back.
- [ ] Gameplay > Blood Color Custom: Custom Red / Green / Blue bars, the colour square and #code follow; the grey line
      says the arcade textures keep their colour for now.
- [ ] System > Language opens its own page: each language in its own script, the current one says Current, the help
      line shows a sentence in that language; picking one switches at once.

### Cut-scene actors, part 2 (actors2, 2026-10-26)

- [ ] Gallery > Cut-scenes: the actor list now also has the ten researchers (Researcher, (2)-(6), Researcher (woman),
      (woman, 2)-(4)). Each looks right in arcade and PC models (try 1-06, 2-12, 4-19).
- [ ] Arcade models: Rogan in G's (or P3 / P4's) part wears his long coat; G or anyone else in Rogan's part has none.
- [ ] 1-07 Jump down a (and 2-10 / 2-11): Sophie or a woman researcher in Rogan's / G's part jumps with Sophie's own
      jump (as the game does for Sophie), not Rogan's. Sophie's skirt piece is there.
- [ ] Pick some actors, quit the game, start it again: the Cut-scenes page shows the same picks.
- [ ] Extras > Gallery: it always opens on Thomas Rogan (Players); going back to Players lands on Rogan too.

### Gallery fixes, round 2 (galfix2, 2026-10-27)

- [ ] Background "Bridge (Chapter 1)": the real bridge over the moat with the mansion's red door behind (not the gate
      road where the car comes in); the model stands on the deck, not sunk in it.
- [ ] Background "Mansion corridor (Chapter 2)": the red carpet and paintings, no broken walls / wrong pictures.
- [ ] Viewer rows (Motion, Model, Light, Background, Layout) show < value >. Click the < to go back, the rest of the
      row to go forward; with the mouse on a row, Left / Right step it too.
- [ ] Creatures (26), Humans and other long lists: a thin scroll bar at the list's right (Remake page and the
      portrait layout); it moves as you go down the list.
- [ ] Bosses > The Hangedman: the weak-point picture is big (like the Chariot's), the name on one line and the
      description shown. Check the other bosses' cards too.
- [ ] The human-faced fish is under Cut Content now, not Items.
- [ ] Cut-scenes: 1-02 Walk and Shoot and 1-04 Fall Backside play (1-4 players); the rest still play. A scene that
      can't be reached shows its red message wrapped inside the column.
- [ ] Clear Chapter 1 (arcade models): the photo of the rescued researchers looks like the arcade (every face whole,
      none covered by hair; the right-hand man's face not broken) with Video > Draw Depth = Arcade Sort (the default).

### Controllers and light guns, pass 2 (padpass, 2026-10-27) — with your DualSense

Use only the DualSense (no mouse / keyboard) unless an item says so. Menu Style Remake.
- [ ] The prompts at the bottom right show the DualSense's buttons as soon as you press one: Back = a circle, Accept
      = a cross (drawn shapes), the tab keys L1 / R1. Touch the keyboard: they turn back into Esc / Enter.
- [ ] Options: L1 / R1 go through the tabs (Video, Audio, Controls, Gameplay, Mods, System); the d-pad and the left
      stick move the focus; Cross opens a list / page; Circle goes back from every page.
- [ ] Audio: on Sound Effects Volume / Music Volume, d-pad Left lowers the bar 10 %, Right raises it; at 100 % Right
      does nothing (it used to jump to 0 %). Holding Left / Right keeps stepping.
- [ ] Gameplay: Blood Color Custom, then Custom Red / Green / Blue: Left / Right move each bar 16 and stop at 0 / 255.
- [ ] Controls > Reset to Defaults: Cross asks; d-pad Down to Cancel, Cross: nothing changed. (Reset: the inputs go back.)
- [ ] System > Language: Cross opens it, pick one with the d-pad + Cross, Circle back.
- [ ] Mods: every mod's On / Off and its Settings page work with d-pad + Cross, Circle back.
- [ ] Video > Crosshairs (Firing Range) with Player 2 Input = Gamepad: the DualSense aims a crosshair on the range
      (gyro / stick) and R2 shoots the targets; the d-pad still changes the rows; **Circle now goes back** (before:
      Circle was a reload there and you were stuck).
- [ ] Firing Range with the keyboard: press Tab ("The keys aim here" at the range's bottom); the arrows aim player 2,
      Space shoots; W A S D / F player 3; Tab or Esc gives the keys back to the menu. Also on a 4:3 screen: the range
      shows under the rows (it was missing in the Remake style at 4:3).
- [ ] Pull the DualSense's cable / switch it off on a menu page, plug it back: the menu keeps working with it.
- [ ] Extras > Gallery > Cut-scenes, play one: Cross pause, Square replay, Triangle slow, L1 Actors (d-pad in it),
      **R1 Clean View, d-pad Down subtitles** (new), Circle back. The hint line names the DualSense's buttons.
- [ ] In a cut-scene the arcade cannot skip (bars up): hold your fire button (R2, or what you set) 3 s: the red ring fills and the scene skips.
- [ ] Light guns (only if you have one): Controls > Light Gun Type: Sinden = a white border round the screen (menus
      and play), CRT Gun = the screen flashes white for a moment on every shot (flashing!), IR / Mouse Mode = neither.
      Shot Flash / Gun Border can be forced On / Off. Calibrate Light Guns: At Next Start, start Arcade: the two targets.

## Soak fixes (2026-10-27)
- [ ] Boss Mode: pause (Esc) > Exit to the menu, then pick Arcade: Chapter 1 starts (not the boss again). Same with
      Chapter Select and PC Mode.
- [ ] Play a PC Mode game, quit to the menu, wait for the attract: it plays like the arcade's attract.

## Online rejoin, languages, name entry (misc17, 2026-10-27)
- [ ] Online (2 PCs): close the joiner's game mid-play (or turn its Wi-Fi off and on) and join again at once with the
      same code: it gets back in within a second or two (no "This game is full", no 6 s wait). Same with 3-4 PCs.
- [ ] Options > System > Language: Korean, Japanese, Arabic, Hebrew: every menu page, the Players page (Player 2:
      Human / CPU), the name entry (Boss Mode / PC Mode best scores), Firing Range, Mods, Gallery and Actors, the
      online pages are in the language. Look for text cut off or running out of its box.
- [ ] A language chosen, PC models: the pause (Esc) shows PAUSED / Continue / Exit in the language; running out of
      lives shows "Continues n" in the language. English: the PC's own pictures as before.
- [ ] (Native speakers, if you know any) the Korean / Japanese / Arabic / Hebrew files are machine translations.
- [ ] Clear Chapter 4 with a top-10 score: the arcade's name entry, then the ranking (window).
### Follow-ups (followup, 2026-10-27)

- [ ] 4 players, Chapter 1's first fight: let everyone lose all lives, wait at CONTINUE a few seconds, all continue.
      The zombie waits at its spot while you are out and attacks again soon after; it no longer walks round you.
- [ ] 4 players, PC models: when player 4 (or 3) is hit, the splash / claw marks are see-through like the arcade
      models' (stippled), not a solid yellow (green) block.
- [ ] 4-19 Goodbye Curien with 4 players (the game's ending and Extras > Cut-scenes, any frame rate): after G says
      "Shall we go now?" and the camera cuts, player 3 (and 4) stand on the floor at once, no float / drop.
- [ ] Gallery > Humans: the researchers on the woman's body are "Researcher (woman)", "(woman, 2)", "(woman, 3)",
      "(woman, 4)" as in the Cut-scenes actor list (no "Researcher (5)" woman).
- [ ] Cut-scenes > Actors: Dr. Curien, Dr. Curien (Dead), Sophie (zombie) are in the list. Arcade models: their clothes
      and faces are textured right in any scene (1-06, 2-12, 4-19); the first pick may take a moment once (it loads
      Chapter 4's textures). Curien's head is upright and his cane hangs down beside him (both models).
- [ ] Cut-scenes 1-07 Jump down a with Players 4: put Sophie (or a woman researcher) in player 3's part: she jumps
      with Sophie's own jump; put Rogan in player 4's part (arcade models): he has his long coat.

### Render polish (rpolish, 2026-10-27)

- [ ] Attract (title loop), the close-up of Dr. Curien: his waistcoat is gold like his shirt, no dark brown panel.
- [ ] Extras > Cut-scenes, 4-19 Goodbye Curien (2 or 4 players): let it play to "Over": the held last picture shows the
      hall's blue floor and walls, no sepia floor and no black band with tree tops at the top.
- [ ] Cut-scenes, any scene, Free Cam (F): fly right up to a player: hands stay in front of the guns, the jacket does not
      cover the head; close parts are not cut away.
- [ ] Cut-scenes > Actors, Dr. Curien in a part (e.g. 4-19 with 4 players, variation H): the band between his chest and
      hips is his gold shirt, not grey; the mirrored copy (player 4) stands upright with his cane down.

### Input polish (ipolish, 2026-10-27)

- [ ] Options > Controls > Light Guns (Assign >): click Player 1, pull your light gun's trigger (or click a mouse): the
      row shows the gun's name. Do the same for Player 2 with the other gun. Start a game with both players on Light
      Gun: each gun aims for its own player whichever you use first. Unplug one: its row says (Not Connected).
- [ ] Same page: Esc while a row waits cancels; after 10 s it gives up by itself. Clear Player 1 / Clear All: back to
      "First Used" (the old way: the first gun used is player 1's).
- [ ] Extras > Cut-scenes, a scene, Free Cam with a pad (click a stick): left stick moves, right stick looks, LB / RB
      (L1 / R1) down / up, click the left stick: FAST (again: normal), click the right stick: Free Cam off. The line at
      the bottom names your pad's buttons (Xbox / PlayStation / Nintendo). W A S D / Q E on the keyboard still work.
- [ ] Mods > Ragdoll On, Chapter 1: kill two zombies close together: the bodies pile up / slide off each other instead
      of sinking into each other. Bodies by a door or gate the game opens stop at it.
- [ ] Start a game with Ragdoll Off, pause, switch it On (Mods), go on: bodies already stop at walls (before: only from
      the next chapter).
- [ ] System > Language Arabic (or Hebrew), Menu Style Classic, Extras > Jukebox: the tabs read Voices, Sound Effects,
      Music from left to right (Music at the right, as the Remake style); Left / Right move the way they point.

### PC Mode leftovers (pmmisc, 2026-10-27)
- [ ] Main menu, Ctrl + SKIDMARX, Cheat: turn Free-Play on, then play PC Mode with Continues 0: after losing all lives
      CONTINUE? still comes up and you can continue as often as you like. Turn it off again: a game over at once.
- [ ] PC Mode, 2 players, Continues 1: P2 joins (uses the last credit), then P2 loses all lives while P1 plays: P2's
      corner shows only the counting digit, no CONTINUE? word; after 9 seconds P2's game is over, P1 plays on.
- [ ] PC Mode, Chapter 2 / 3: shoot the drums the zombies throw: they fly, land, roll over a few times and lie still.

### Arcade accuracy from the whole-game differential (ch4proof, 2026-10-27)
- [ ] Chapter 1, the opening cut-scenes: when a player reloads in a cut-scene, the empty magazine drops and falls to the
      floor; in the cat scene (the agent shoots the cat) pieces fly off.
- [ ] Chapter 1, the Hangedman's fly-by (event 3): it bobs up and down while flying past (before: it only sank), and
      its wings flap.
- [ ] Chapter 3, the elevator ride: the elevator stops at the same height as before (a hair higher), no jump.
- [ ] Play to the end of Chapter 4: the ending starts and plays as before; the name entry's letters fly into place.
### Low-end PCs, text cuts, ragdoll (misc18, 2026-10-27)
- [ ] **On the Surface**: the Remake menu (Live Background Auto, Menu Frame Rate Auto) feels smooth now: the hand and
      the focus move at 60 pictures a second (Video > Menu Frame Rate shows "Auto (60 fps)"; it was 30). Walk Options,
      Firing Range, Gallery, Jukebox, Mods, Language. If it still stutters, note the page and what Menu Frame Rate's
      Auto says (it drops to 30 by itself only when the menu really runs slow).
- [ ] Same pages on the RTX PC: everything looks exactly as before (the splatter, the panels' edges, the hand).
- [ ] System > Language Korean / Japanese / Arabic / Hebrew: Jukebox (long song names), Gallery (long names / motion
      values), Online Co-op names: a cut text ends in ".." or "..." with whole letters (no box or broken letter).
- [ ] Mods > Ragdoll On, Chapter 1: shoot zombies next to a researcher being attacked (blood bursting on him): the
      falling bodies no longer bump into him / into the blood; doors and gates still stop them.
### Boss Mode as the 1998 PC, barrels in PC Mode (pmrest, 2026-10-27)
- [ ] Boss Mode, any boss: the game opens on the boss's title card straight away (no walk to the boss, no music or
      sounds before it); the boss song starts with the title, the clock appears then and runs from the title's end.
- [ ] Boss Mode, Chariot / Hermit: after the kill the boss lies there about 3 seconds, then your time and the name
      entry (no chapter-clear screen). Magician: he shakes, sparks, and about 2 seconds later the time (he no longer
      flies away and falls).
- [ ] Boss Mode, Hangedman: after he hits the ground he blinks for about 4.5 seconds (Chapter 2 in Arcade: 3 seconds).
- [ ] Boss Mode with 2 players set: both start at the boss. With 1 player, pressing P2's start during the fight does
      not let P2 join (as on the PC).
- [ ] All Bosses: the four in a row, the clock adds up over the four fights, then the name entry for All Bosses.
- [ ] Main menu > High Scores: turn the tables with Left / Right (or the arrows): after PC Mode come Chariot Boss,
      Hangedman Boss, Hermit Boss, Magician Boss, All Bosses with names and times; a new boss time shows up there.
- [ ] PC Mode: barrels that fly into the scene by themselves (not the drums zombies throw) bounce twice, roll over and
      lie still, then vanish once off screen; nothing sinks into the floor.
- [ ] PC Mode, Chapter 4 (any characters): after the first Hangedman fight the game goes on (it used to wait forever).
- [ ] PC Mode / Boss Mode, Chapter 1's Chariot room: no grandfather clock stands there now (the 1998 PC has none).
- [ ] Boss Mode: the fights feel as before (bats fly straight at you without bobbing, shots go through scenery).

### Boss Mode online (bossnet, 2026-10-27)
- [ ] Online Co-op > Host a Game: **Game Mode** goes Arcade -> PC Mode -> Boss Mode; with Boss Mode a **Boss** row picks
      Chariot / Hangedman / Hermit / Magician / All Bosses.
- [ ] The friend's waiting room (Join) shows "Game Mode: Boss Mode: <the boss>" before the host starts.
- [ ] Start: both PCs open on the same boss's title card; the clock shows the same time on both PCs during the fight.
- [ ] All Bosses online: the four in a row on both PCs, the clock adds up, nothing goes out of step between bosses.
- [ ] At the end (all bosses beaten, or everyone out of lives): the online game ends, each PC types its own name for the
      boss times table on that PC (each PC keeps its own table).
- [ ] A friend whose connection drops can join again with the same code and is back in the same boss fight.
- [ ] Offline Boss Mode plays and times exactly as before.

### Same game data online, Boss choice saved (netdata, 2026-10-27)

- [ ] PC Mode online, both PCs with the 1998 PC game folder set: the friend joins and the game plays as before.
- [ ] PC Mode (or Boss Mode) online, the friend's PC has **no** PC game folder set: the friend gets "The host uses the
      1998 PC files for PC Mode / Boss Mode; this PC doesn't have them" right away (no "games are different" later).
- [ ] The other way round (only the friend has the PC files): the friend gets "... the host doesn't have them".
- [ ] An Arcade game online: a friend without the PC files can still join.
- [ ] Both PCs need the new version: an older copy gets "another version".
- [ ] Host page: set Game Mode to Boss Mode and pick a Boss (e.g. the Hermit), quit, start again: the Host page still
      shows Boss Mode and that boss.

### The sepia scenes' colours (arcfix2, 2026-10-27)

- [ ] Chapter 1's opening (the scientist, right after the chapter card): the whole picture is brown / sepia; the
      scientist's white coat is now brown like the rest (it was a light grey-white).
- [ ] The other sepia moments look the same way: the end of Chapter 1 (Sophie), the end of Chapter 2, the ending's
      END card. Glowing / blinking things (lights, item sparkle) may keep their colour there, as in the arcade.
- [ ] PC models (PC look): the sepia scenes look as before (they use the PC's own brown palette).

### Blood colour, coin prompts, canal water (arcpal, 2026-10-27)

- [ ] Arcade models, each Blood Colour (red, green, blue, purple): shot zombies spray that colour, as before.
- [ ] Custom blood colour: the bursts take the custom colour, as before.
- [ ] Change the Blood Colour in the options, then go back into the game: the bursts take the new colour.
- [ ] Attract / title: PRESS START BUTTON and FREE PLAY still blink and show as before.
- [ ] Chapter 1, the canal bridge: the water still flows as before.

### PC Mode / Boss Mode as the 1998 PC game (pmrest2, 2026-10-27)

- [ ] PC Mode, Chapter 1: no rats run along the floor any more, and the dining room has no breakable table, chairs and
      plates (the 1998 PC game has none). Arcade Mode still has both.
- [ ] Boss Mode, Hermit: when the camera looks up at him on the ceiling, the view is turned as in the 1998 PC game.
- [ ] PC Mode: zombies that turn their heads to look at you still do so smoothly.

### Online from the menu (menuhost, 2026-10-27)

- [ ] Start the game, let the SEGA logo / title play, then Online Co-op > Host a Game > Start Hosting: a friend joins
      with the code from their menu and the game starts on both PCs (as before).
- [ ] Host, then leave the Hosting page alone for a minute: the attract never starts while you host, and a friend can
      still join.
- [ ] Join a Game: the LAN list shows the host's game; click it, type the code, Enter: you are in the waiting room.
- [ ] PC Mode, Chapter 2: the breakable props you know from Arcade Mode that the 1998 PC game leaves out are gone in
      PC Mode (Arcade Mode unchanged).
- [ ] PC Mode, Chapter 3: the first iron-ball zombie swings, throws and drops its ball as before.
- [ ] Language Arabic (or Hebrew / Japanese), classic menu style, Co-op page: the Online Co-op value is in the
      language (cut with "..." if long), not English "Host / Join...".

### Button prompts and the pad cursor (padglyphs, 2026-10-27)

- [ ] Plug in your pad, press a button in the menu: the prompts at the bottom right show that pad's own buttons
      (Xbox: coloured A / B; DualSense: the shapes; Switch: B to accept, A to go back).
- [ ] Options > Controls > Button Icons: pick another model (say PS4): every pad and the keyboard now show PS4 buttons.
      Keyboard: always the keys (Esc, Enter ...). Auto: back to your own pad's.
- [ ] Options > Controls > Button Style: Xelu (grey buttons, coloured letters), Kenney (white), Simple (drawn). Which
      reads best for you?
- [ ] Use the keyboard in the menu: the prompts show key pictures (Esc, Enter, Q / E).
- [ ] DualSense / DualShock 4: in the menu, slide a finger on the touchpad: the bone hand appears and moves like a
      laptop's touchpad; press the touchpad to click. Press the d-pad: the hand goes away again.
- [ ] A pad with a gyro (DualSense, Switch Pro, Joy-Con, Steam Deck): hold ZL / L2 / LT and tilt the pad: the hand
      follows. Controls > Gyro Cursor: Always / Off; Cursor Speed 1-10.
- [ ] If you have them: Joy-Con alone sideways, Switch 2 pad, GameCube adapter, Wii U Pro, Steam Controller: do the
      prompts name the buttons you press?

## Black squares check (blacksq, 2026-10-27)
- [ ] Chapter 1, the green zombie in the fountain: no black squares on its body (arcade and PC models).
- [ ] End of Chapter 1, Sophie lying after the Chariot: her black shadow under her is like the arcade's, no other
      black patches.
- [ ] **Chandelier shadow** (chandshadow): entering the mansion hall (arcade models), the chandelier's black shadow on
      the floor is one rounded blob under its middle, as the arcade; no black shapes left and right of it. Other
      shadows (creatures, Sophie, the Chariot) look as before.
## Release v1.0.0 (relprep, 2026-10-27)

- [ ] Main menu (both styles): a small "v1.0.0" in a corner (Classic: bottom left; Remake: top right); it does not
      cover anything.
- [ ] A fresh copy of the release zip with nothing in data\arcade / data\pc: the WELCOME screen says what is
      needed, with the health-warning line at the bottom; no crash. Then put your files in and start again.
- [ ] `bin\hotd.exe --version` in a terminal prints `The House of the Dead: ReWrithe v1.0.0`.
- [ ] Read the top of README.md (the disclaimer box) and docs/release/notes_v1.0.0.md: anything wrong or missing?
- [ ] Read docs/release/compliance_v1.0.0.md: decide the "needs you" items (the name, src/port, the sums).
- [ ] A mod whose OBJ / MTL names a picture outside its folder (`map_Kd ..\..\x.png`): it is ignored, the
      model shows without that picture.

## Pre-release fixes from your 10-27 notes (prerel, 2026-10-27)
- [ ] Options > Audio: Sound Effects / Music Volume bars step 5 % (drag snaps to 5, Left / Right = 5 %); the Options
  page and the Jukebox volume buttons too. A saved 45 % stays 45 %.
- [ ] Audio > Menu Music: "Chapter 4 / Main Menu" (the default), "The Magician" (was "final boss"), "Unused 1 / 2 / 3
  (arcade)" (the arcade's sound test 8 / 10 / 13, never used by the game). Pick one: it plays at once; go into a game
  and back: the game's own music is normal (also with Sound = PC).
- [ ] Video > Aspect: 16:9 / 16:10 / 4:3 (no "Widescreen"); an older launcher.ini keeps its choice.
- [ ] Gallery > Cut-scenes > Actors: Sophie (zombie) in 4-19 / 2-12 / 1-06 (arcade models): a zombie woman with her
  hair and dress, no grey metal pieces.
- [ ] Controls page / any menu with a pad: Xelu's icons have a light rim and are brighter.
- Known, not fixed: 4P Goodbye Curien (4-19) in 16:9 / 16:10 at any resolution, arcade models: the floor at the far
  left (outside the 4:3 picture) is missing; the PC models draw it. Pictures: `build\shots\prerel\4p_all.png`.
### CPU teammates and researchers (cpu3, 2026-10-28)

- [ ] Play with CPU partners (2P and 4P, any level) through researcher scenes: the CPU never shoots near a
      researcher; when a creature holds one, the CPU mostly waits and lets you do the rescue.
- [ ] Chapter 1 sewer (the brown slugs that crawl and spit): the CPU shoots the slugs, the event ends.
- [ ] A creature just off the screen: the CPU's sight waits at the edge and fires only once it is in view.

### Settings pictures, icons, score, Boss Mode clock (menus3, 2026-10-28)

- [ ] Options > Video > Models: two pictures (Arcade / PC) from Chapter 1's bridge (prevbridge): the big zombie
      carrying the researcher over his head, the mansion's door behind, your crosshair on the zombie. Aspect: no
      pictures.
- [ ] Gameplay > Blood Color: its own picture (the zombie at the door, the researchers running) with a blood burst
      in the colour. Custom: drag the Red / Green / Blue
      bars, then look at Blood Color: the burst is your colour. Dragging the bars is smooth now (no lag).
- [ ] First start on a new PC (or delete the settings folder's previews6 folder): the menu never stalls; the
      pictures say "Making the picture..." for a second, then show.
- [ ] Controls > Icon Outline: Off (default) = Xelu's icons as drawn; On = the light rim. Compare
      private\changelog\img\menus3\xelu_compare.png.
- [ ] Gallery (Remake style), Humans / Creatures: the mouse wheel over the list scrolls it; drag its thin scroll bar.
- [ ] Gameplay > Score Display On (or the title code): each player's SCORE is above his bullets at the bottom
      corners, not at the top. 4:3 and 16:9. (Arcade models only: the PC models never showed the score line.)
- [ ] Boss Mode: the game runs at 60 per second like the PC (Game Speed Auto), so the clock's times match the PC's.

### Gallery, Curien, PC score line (galact, 2026-10-28)

- [ ] Gallery > Humans > Sophie (zombie): her own entry after Sophie Richards, one motion (s_zonbi). Arcade models:
      the same look as the cut-scene viewer's zombie Sophie (galfix3: area 2 with row 13:92, the ending's own look). PC models: Sophie's model (the PC files
      have no zombie Sophie; the page says so). Korean / Japanese / Arabic / Hebrew: her texts translated (AI).
- [ ] Gallery > Bosses: the weak-point card sits beside the stats (each stat its label over its value) and is bigger;
      the Hangedman's about 1.5 times wider than before.
- [ ] Gallery > Cut-scenes > Actors: Dr. Curien in any role (Rogan's, G's ...): his cane stands upright in front of
      him, his hand on it (as the Gallery's first pose), not stuck out sideways. Arcade and PC models.
- [ ] Gameplay > Score Display On, PC models: SCORE:00000 above each player's bullets (PC font), as with arcade models.
- [ ] Gallery > Cut-scenes > Actors rows (both menu styles): a click (Remake: also Enter / A) opens a list of every
      actor, the current one marked; pick one; Esc / B or a click outside closes it. Left / Right, keys 1-4 still step.
- (Both fixed since, polish28 below: PC Curien too white in 4-19; players 3 / 4's Curien copies' cane.)
### Widescreen side holes (endfloor, 2026-10-28)

- [ ] 16:9, arcade models, Chapter 1: in the big hall after the first zombies (the stairs on the left), the far left of
      the picture shows the wall / stairs, not a blank dark area. The middle (4:3) part looks exactly as before.
- [ ] Look at the left / right edges in other rooms (Chapters 1-4): no pieces popping in or doubled walls at the edges.
- Known, not a bug: 4P Goodbye Curien (4-19), arcade models: the dark band on the floor at the far left is the arcade
  floor picture's own shading (the PC floor has none).

### Curien in 4-19 (polish28, 2026-10-28; its ragdoll items are gone since ragrevert)

- [ ] Gallery > Cut-scenes > 4-19 Goodbye Curien, PC models, Dr. Curien (or Dr. Curien (Dead)) in any role: his coat
      looks as in his own scenes (light grey / lilac, not pure white); the scene is not over-bright (light trousers
      not pure white either).
- [ ] Same scene with Curien as player 3 / 4 (Actors rows 3 / 4, variations B+): the cane stands upright in front of
      him, his hand on it, like in players 1 / 2's roles. Arcade and PC models.

### Play does not start a game by itself (autostart, 2026-10-28)

- [ ] Players 4 with CPU teammates on (your usual settings): Play. The attract / title runs and stays there; no game
      starts until you press start (or a key / click for the main menu). After you start, the CPU players join.
- [ ] Start the game while holding a pad's A / Start / trigger (or plug the pad in while holding them): nothing starts
      and the attract does not jump to the menu; after letting go, a new press works as usual.
- [ ] Launch from Explorer with a double-click / Enter: the attract runs (the launch click does not start anything).

### Polish sweep 2 (sweep2, 2026-10-28)

- [ ] Language Arabic or Hebrew, Gallery (Classic menu, or Remake with the Classic layout, K): the categories at the
      right, the model's window at the left; dragging / the wheel on the window still turn / zoom the model.
- [ ] Same, Gallery > Cut-scenes: the categories at the right, the scene lists right-aligned, no row over another.
- [ ] Classic High Scores > PC Mode in Korean / Japanese / Arabic: the rows do not touch; character names in the
      language (Rogan, Sophie, Researcher).
- [ ] Boss Mode (1-4 players, arcade and PC models, 4:3 and 16:9): the clock at the bottom centre, clear of PRESS
      START BUTTON and the 2P lamps.

### Water flicker (ragv2, 2026-10-28; its ragdoll item replaced by ragrevert below)

- [ ] Chapter 1, the pond in front of the mansion bridge: no flicker of the water's texture while walking up to it
      (if it flickers: arcade or PC models, 4:3 or 16:9).

### Gallery fixes 3 (galfix3, 2026-10-28)

- [ ] Gallery > Humans > Sophie (zombie), arcade models: red jacket, white blouse, blonde hair, the DBR badge (as
      Sophie Richards; no grey / green patches). Same in Gallery > Cut-scenes > Actors > Sophie (zombie) (4-19).
- [ ] Optional: finish Chapter 4 with no continue and a score below the top rank: the ending shows Sophie in the
      hall, then the red screen with her zombie face; she looks the same there as in the Gallery.
- [ ] Gallery (Remake), Humans / Creatures: the list's scroll bar looks and works like the Options lists' bar (drag
      the thumb, click the groove, the wheel); the hand moves with the mouse while dragging.
- [ ] Gallery portraits layout (K): its bar is the same kind of bar; dragging it moves through the rows.
- [ ] Gallery, Jukebox, Cut-scenes, Firing Range: the hand never sticks; clicking near the list's right edge in the
      Gallery picks the entry (it used to start a drag that froze the hand).

### Ragdoll mod back to v2 (ragrevert, 2026-10-28)

- [ ] Ragdoll mod on: it plays like the 10-27 night's v2 (same slide, same shadows as the game's, bodies stay drawn in
      cut-scenes as before, shot researchers fall as the game does). Settings: Friction, Bodies Stay, Gravity,
      Dismemberment.
- [ ] Dismemberment Off: no flying heads / arms; the head or arm stays on the body (also on the ragdoll). On: as the arcade.
- [ ] Ragdoll mod off: everything looks as before.

## Opening car shows your characters (carchars, 2026-10-28)
- [ ] PC Mode, Chapter 1 opening: pick extras (e.g. a researcher + a woman researcher, Dr. Curien + zombie Sophie):
      the car shows P1 driving and P2 beside as themselves (not Rogan / G), both model sources.
- [ ] Sophie / Researcher with arcade models: themselves in the car; with PC models: the PC's own car models as before.
- [ ] Rogan + G, G + Rogan: Rogan / G look as before in their usual seats; swapped seats show the right one.
- [ ] Nothing pokes out of the car (legs, arms) in the wide and close shots.

## Display Mode, resolution crash, crash reports (rescrash, 2026-10-29)
- [ ] Options > Video > Display Mode: Windowed / Borderless / Fullscreen each switch at once, no crash; the game starts
      the next time in the one you left.
- [ ] Borderless: Resolution greyed ("Desktop"); Windowed: Resolution = window sizes; Fullscreen: display modes
      (Auto = your desktop's), a change asks "Keep this resolution?".
- [ ] F11 / Alt+Enter from a window goes back to the Display Mode you chose (Borderless or Fullscreen).
- [ ] Change Resolution / Display Mode quickly several times on the Remake menu (live background on): no crash.
- [ ] A laptop with NVIDIA / AMD + integrated graphics: hotd.log's "live background:" line names the NVIDIA / AMD
      card (not the integrated one).
- [ ] After a crash (or ending the game in Task Manager): the next start leaves crash-...-previous-run.txt in the
      settings folder.
## Small features (feat29, 2026-10-29)
- [ ] Remake menu style: at start-up the SEGA logo shows (light grey, fills the screen), then the attract / menu. A key
      or click skips it. Title / Attract Off: the SEGA logo, then the menu.
- [ ] Gameplay > PC Start-up Screens On: next start shows the PC's SEGA screen, then the zombie title art (any key / click
      skips each), in both menu styles. On is the default now (pmpick, 2026-10-30); Off: as before.
- [ ] Gameplay > Free Play On: PC Mode's continues never run out; the Cheat screen's Free-Play shows the same value.
- [ ] Video > Crosshairs (Firing Range) > Crosshair row: the help line shows the crosshairs folder (it exists in
      the settings folder). Put a PNG there, back to the range: it is on the Crosshair row as "Custom: <name>".
- [ ] A Switch Pro pad (Button Style Xelu): its button icons have a shadow and white letters, easy to read.

## Hangedman, Gallery bosses, 4P settings (hangfix, 2026-10-29)
- [ ] Chapter 2 Hangedman: unchanged on purpose. Once his bar is nearly empty he hovers for a few seconds before
      flying off: the arcade does exactly the same (checked frame by frame against MAME, four runs).
- [ ] Gallery > Bosses > The Magician: the weak-point card's title line says "Magician TYPE 0" (was "Hangedman").
- [ ] Gallery bosses: Hangedman Type 041, appears Chapters 1, 2, 4; Hermit Type 6803, 160 hit points; Magician
      Type 0; the Chariot / Hangedman / Hermit texts mention Chapter 4 / the wall drop. Try another language too.
- [ ] Chapter 4 (PC sprites / PC models): the Magician's intro card shows "Magician TYPE 0".
- [ ] 1-2 players with 4P Boss Hit Points / 4P Enemy Toughness at their maximum: bosses and enemies take as many
      shots as with the defaults.

## Widescreen sides in the ending (visfix, 2026-10-29)
- [ ] 16:9, arcade models: the ending's Goodbye Curien (1P: Rogan alone; 4P: all four) has no big wall panels over
      the left / right sides; the floor goes on and the window's sky shows at the right, as with HOTD_WIDE_ZONES=0.
- [ ] The staff roll: the sides stay black (no walls in the right strip).
- [ ] Chapter 1 hall (and the other places from 10-28): still no holes at the sides (walls / stairs left of the stairs).
- [ ] Cut-scene viewer, 4-19 in 1P / 2P / 4P: same as in the game.
- [ ] A laptop with integrated graphics (or `HOTD_PREPASS=1`): no flickering / missing bits of walls, floors or
      people while playing (the depth pre-pass).

## Safe Mode, more PCs (compat, 2026-10-29)
- [ ] `Play (Safe Mode).bat` (portable copy): the game opens in a 1280 x 720 window, the menu shows the still (no live
      attract), no CRT filter; the game plays. `Play.bat` afterwards: as before (fullscreen, your settings).
- [ ] hotd.log starts with a `graphics: OpenGL ...` line naming the card and driver (no WARNING line on this PC).
- [ ] The Remake menu's dark backdrop (while the live attract loads, the Cut-scenes page): no visible pattern or
      banding in the dark (new noise for its dithering).
- [ ] A friend's AMD / Intel laptop: Play.bat; if anything looks wrong, Play (Safe Mode).bat; send hotd.log either way.
## DirectX renderer through ANGLE (angle, 2026-10-29)
- [ ] Options > Video > **Renderer** shows Auto / OpenGL / DirectX (ANGLE); its help line says which one runs now.
      Pick DirectX (ANGLE), quit, start again: the help line now says "DirectX 11 (ANGLE)". Play a bit of Chapter 1:
      same picture as OpenGL (the first start or the first chapter may take ~2 s longer once: shaders compiled, then kept).
- [ ] With DirectX: the CRT filter presets, the Remake menu's live background, the Gallery and F12 screenshots look the same as on OpenGL.
- [ ] Back to Auto: the game starts on OpenGL as before.
- [ ] If you have an old PC or laptop (2008-2012, Intel HD 2000 / 3000): Auto should start it through DirectX by
      itself (hotd.log: "renderer: ... using DirectX 11 (ANGLE)"). Note how fast it runs.

## Clean face in the attract (faceatt, 2026-10-29)
- [ ] Title, wait for HOW TO SHOOT (arcade models, 16:9): the woman researcher running at the left has a clean face
      (no black hair square over her left brow); the small dot (mole) on her chin is still there. Same in 4:3.
- [ ] Chapter 1, Sophie's close-ups: her eyes look as before (she keeps the arcade sort; not changed).

## Cut-scene actors, viewer focus, live background (galfix4, 2026-10-30)
- [ ] Gallery > Cut-scenes, 4-19 Goodbye Curien, Sophie in Rogan's part: no gap in her skirt; she holds Rogan's gun.
      Same with a researcher, Dr. Curien or the zombie Sophie in a player's part (they hold the gun too). Sophie now
      stands at her own (shorter) height, feet on the floor. Both model sets.
- [ ] While a Gallery cut-scene plays, Alt+Tab away and back: the scene is paused (the keys line says PAUSED);
      Space / A / a click goes on. No pause menu with only Exit working.
- [ ] Remake menu, watch the live background through the story (zombie intro): at each shot change no frame without
      the people and no other world flashing in; the old shot cross-fades into the new one.

## Chandelier shadow up close, zombie Sophie's gun hand (rellook, 2026-10-30)
- [ ] Chapter 1, walking into the mansion hall (arcade models, 16:9): the chandelier's shadow on the floor stays round
      blobs (one in the middle, one each side) as the camera walks under it; no hard black trapezoids at the bottom.
- [ ] Gallery > Cut-scenes > Actors: Sophie (zombie) in Rogan's (or G's) part, e.g. 4-19 Goodbye Curien: her gun hand is
      her own pale zombie hand, still holding the gun. Other actors unchanged. PC models: unchanged.
## Menu fixes from the final test (relmenu, 2026-10-30)
- [ ] Options > Video > Crosshairs (Firing Range), Crosshair row: the help line says "add your own crosshair PNGs to
      the Crosshairs folder" (no long folder path). Both menu styles, every language.
- [ ] Free Play is also on the Options page with Difficulty / Life / Continues. Remake: its own row under Continues
      (On / Off). Classic: Continues counts 0 .. 9, then "Free", then 0 again. Gameplay > Free Play and the Cheat
      screen's Free-Play show the same setting.
- [ ] Remake, a long page (Controls): on the last row press Down: the first row, the list back at its top. On the first
      row press Up: the last row. Same with a pad's d-pad.
- [ ] Controls > Button Icons: Switch 2 (or GameCube). Button Style now says Kenney (Xelu has no icons for these
      pads; it is grey in the list). Right / Left go Kenney <-> Simple, skipping Xelu; picking the grey Xelu does
      nothing; the menu keeps working (keys and pad). Button Icons back to Auto with an Xbox / PlayStation pad: Xelu again.
- [ ] System > Language: each language in its own letters plus English: "العربية (Arabic)", "עברית (Hebrew)",
      "日本語 (Japanese)", "한국어 (Korean)", no square boxes. Try it in English and in Arabic.

## Release fixes: game over, Score Display, 4P prompts (relplay, 2026-10-30)
- [ ] Remake menu, Arcade, default settings: lose all lives and let CONTINUE? run out. After GAME OVER the attract
      runs (story, title, how to play, demo) and keeps going round with nothing pressed; no game starts by itself.
      A key, a click or a pad button there goes to the **main menu** (no longer straight into a new Arcade game).
      Same after a game over in a later chapter (it used to restart that chapter at once) and from Chapter Select.
- [ ] Options > Gameplay > Score Display On: SCORE shows in play, is gone in every cut-scene (HOLD YOUR FIRE!) and
      comes back when play goes on. On the CONTINUE? screen there is no torn "SCOR" left over.
- [ ] Co-op > 4 Players with players 3 / 4 not started: on the chapter card ("The First Chapter / Tragedy") no
      PRESS START BUTTON for players 3 / 4 (as players 1 / 2); it blinks again in play.

- [ ] Gameplay > Attract Gore (On by default): the attract demo's shots blow off heads / limbs like the 1998 PC; Off = the arcade (Revision A, none)
- [ ] Gameplay > Attract Gore On: the prologue's sewer scene (after the lab, the zombies under the lamp) has two dead researchers on the walkway, and the camera's last shot ends on one of them in his pool of blood; Off: the empty floor (Revision A)
- [ ] Options > System > Check for Updates (On): after a newer GitHub release, the main menu asks to open the download page once

## Options toggles, Cut-scenes list (galfx, 2026-10-31)
- [ ] Options > Video > Hit Sparks Off: the bright spark bursts where your bullets hit (zombies, bosses, walls) are gone
      in play, Boss Mode, Trailer Mode and the Gallery cut-scenes; the game plays the same. On brings them back.
- [ ] Options > Video > Gun Muzzle Flash Off: the flash on Rogan's / G's gun when they fire in a cut-scene is gone (try
      Chapter 1 "Reload shoot"). On brings it back. Both are kept in launcher.ini and reset by the Video page's Reset.
- [ ] Gallery > Cut-scenes: the list has a scroll bar (wheel and drag), like the other Gallery lists. Choosing a scene
      with Q / E keeps it in view.
- [ ] Gallery > Cut-scenes > 2-21 "The Hangedman's defeat": he falls in front of the camera, no longer into the building.
- [ ] Gallery > Cut-scenes > 4-19 "Goodbye Curien" ends when the Magician speaks; the new 4-22 "The Magician's
      defeat" starts just before "You haven't seen anything yet" and runs to his blast.
- [ ] Cut-scene viewer, Free Cam (F): C (pad: Back / Select) toggles the slow cam: a quarter of the move and turn speed,
      eased in and out (no jerks); "SLOW CAM" shows in the key line. For cinematic recording.

## Round 2 (galfx, 2026-10-31)
- [ ] Options > Video > Blood Particles Off: the blood spray / bursts where shots hit are gone (the blood marks stay);
      On brings them back. Kept in launcher.ini; the Video page's Reset puts it back On.
- [ ] Gallery cut-scene viewer: 1 / 2 / 3 turn Hit Sparks / Gun Muzzle Flash / Blood Particles On / Off live (shown in the
      key line, kept like the Options); H hides / shows all the key hints (for recording).
- [ ] Gallery > 2-21 "The Hangedman's defeat": he lands on the grey road in the courtyard.
- [ ] Free Cam: the mouse look (right-drag) is eased; with the slow cam (C) it is slower too.
- [ ] Gallery > 4-21 "The Magician's defeat": right after you beat him: he rises, speaks and blows up; it ends after the blast.
      "Goodbye Curien" (4-19) is the farewell as before. (4-22 is gone.)
