The House of the Dead: ReWrithe - Light Gun Edition
===================================================
Unofficial build of The House of the Dead: ReWrithe (https://github.com/TawhidI-602/The-House-of-the-Dead-ReWrithe)
for light gun setups (arcade cabinets, Gun4IR, Sinden, AimTrak and other guns that act as a mouse). Based on ReWrithe
v1.4.0. New ReWrithe releases are merged in only when they are worth it, so a working setup is never broken by
upstream changes. Not made by, or supported by, ReWrithe's author: please report problems with this edition here, not
on the original's page.


Controls
--------

In a game
.........
  Control                                       What it does
  Trigger                                       Shoot. For a player not in the game (waiting, or out of lives): join / continue (Start)
  Right button, pump, or a shot off the screen  Reload
  Middle button                                 Reload. In Arsenal Mode: the next weapon
  Hold the trigger                              Automatic fire with the SMG, rifle and machine gun (Arsenal, Machine Gun mode, Survival)
  1 key (also P)                                Pause menu: Continue, Settings, Chapter Retry (if on), Exit. Shoot an item to pick it
  Esc (with the launcher)                       Close the game completely (it saves its settings), then the launcher

- Every mode starts with player 1 only; the other players join with their trigger (Boss Mode and online games
  start with everyone, as before).
- The 2-player games keep the original arcade screen. With 3 or 4 players the 4-player layout is used: players 3
  and 4 at the top corners, each player's RELOAD and CONTINUE next to their own HUD.
- Name entry (high scores) and the PC Mode character select: each player uses their own gun.
- Crosshairs are drawn only for players in the game and stay inside the 4:3 picture.
- The Windows pointer never shows in the game.


In the menus
............
  Control                                       What it does
  Player 1's gun                                Aim: the menus follow player 1's gun only (aim help snaps to the nearest item)
  Trigger                                       Select (Shoot)
  Right button                                  Back (Reload)
  UP / DOWN buttons left of a long list         A page up / down (also the tall targets over the scroll bar)
  Keyboard                                      Arrows, Enter and Backspace still work

The on-screen prompts show the gun's buttons (Shoot / Reload), never a controller's.


Light gun setup
...............
The first start shows a full-screen setup page before the game: shoot anywhere with player 1's gun, then player
2's, 3's and 4's. The list at the bottom left shows who is set; DONE, START AGAIN and SKIP at the bottom
right (Enter / Backspace / Esc on the keyboard too). The guns are saved by hardware ID in LightgunHID.ini in the
game's folder (P1_Lightgun_HID = VID_xxxx&PID_xxxx .. P4_Lightgun_HID), so each gun goes to the right player at
every start. Run it again any time: Options > Controls > Light Gun Setup. If no listed gun is connected, the mouse
plays.


What this edition adds
----------------------
- Outputs for Hook of the Reaper (and MAMEHooker, QMamehook, OutputHooker): the MAME network outputs on
  127.0.0.1:8000, game name hotdrw. Recoil once for every bullet that leaves the gun (never for a reload, an
  off-screen pull or an empty gun; every shot of automatic fire); ammo, clip, life, damage, dead and the start lamps
  for players 1-4. Every output is cleared when a game starts or ends, so the last game's lights never stay on. Copy
  setup\Hook of the Reaper\defaultLG\hotdrw.txt into Hook of the Reaper's defaultLG folder. launcher.ini:
  outputs=0 turns them off, outputsport= changes the port.
- Bezel in 4:3: the game draws 4x3\bezel.png over the side bars in 4:3 mode (1920 x 1080, the middle
  transparent, scaled to the window's height). The cut-scene bars stay inside the picture.
- Killer Mods (Options > Killer Mods tab; local games only, online games play as normal). Each shows "Killer
  Mod, by NF". When a game starts, the names of the mods that are on (Options > Mods and Killer Mods) show for 5 seconds.
  One gun mode (Shotgun, Free Fire, Machine Gun, Arsenal) and one challenge (Hardcore to Coin-Op Survival) at a
  time: turning one on turns the others of its group off. The looks, Married Mode, Party Scoreboard and Options >
  Mods go with anything, except Big Head with Weak Point Mode or Exploding Zombies (its heads are drawn big but hit
  where they really are). Turn All Mods Off (top of both pages) switches everything off. Each gun's bullets have
  their own colour: handgun gold, SMG blue, assault rifle orange, double pistols purple, shotgun red,
  Magician's Hand pink, machine gun silver, rocket green.
  - Arsenal Mode: the middle button steps through Handgun, SMG, Assault Rifle, Double Pistols, Shotgun, Magician's
    Hand, Machine Gun and Rocket Launcher, each player their own, with Survival Mode's guns, sounds and HUD. Ammo never
    runs out, for any gun (one rocket a load); each rocket blows up every zombie on the screen in a big explosion.
  - Shotgun Mode / Machine Gun: the whole game with that gun (the machine gun: unlimited ammo).
  - Free Fire: unlimited ammo, no reloading.
  - Hardcore: one life, no continues, its own high scores.
  - One Shot Kill: one shot kills any zombie (bosses as normal), its own high scores.
  - Hostage Crisis: shoot a hostage or scientist and it is game over for everyone.
  - Weak Point Mode: each zombie can be hurt only at its weak point, marked by a pulsing target.
  - Exploding Zombies: about one zombie in three glows; a head shot blows up every zombie on the screen, a hit
    anywhere else blows up only it, and costs you a life.
  - Coin-Op Survival: a 60-second clock; each kill +10 s, each rescue +30 s; time up = game over. Its own high
    scores.
  - Flashlight Mode: the mansion dark, each player's gun lights where it aims.
  - Screen Filter: Night Vision, VHS Camcorder or Wireframe.
  - Married Mode: you are player 1, computer teammates fill every other seat.
  - Party Scoreboard: with 2+ players, each player's score, kills, shots, hits, accuracy and the MVP at a
    chapter's end (off by default).
- Launchers (AutoHotkey 1.1), no pop-ups, so front-ends such as LaunchBox are never interrupted:
  - Launch HOTD ReWrithe.ahk: starts the game; Esc closes it completely; 1 is the pause menu; Hook of the
    Reaper is closed (cleanly) when the game closes. Optional argument 4x3 / 16x9. Your settings in
    Launch HOTD ReWrithe.ini beside it ([Launcher] IgnorePads=1, CloseHotr=0, Debug=1).
  - Launch HOTD ReWrithe - Link Play Host.ahk / - Link Play Join.ahk: online or same-network co-op in one click
    (the host's join code is copied to the clipboard; on the same network the join needs no code).
  - setup\Launch HOTD ReWrithe - Debug.ahk (for testing): writes settings\launcher.log and settings\outputs.log,
    and leaves Hook of the Reaper open.
- Quick settings as programs (close the game first): 2 - Display - 4x3.exe, 2 - Display - 16x9.exe,
  2 - Crosshairs - On.exe, 2 - Crosshairs - Off.exe. In the game: Options > Video > Show Crosshairs.
- Menus for light guns: no controller rows or pictures (the gun's prompts instead), bigger pause-menu targets, aim
  help, the UP / DOWN page buttons.
- 4 players without the "Experimental" tags; Typing of the Dead 0 is player 1's alone.
- Arcade games are always free play. PC Mode keeps its continues (Options > Game > Continues), unless Free Play is
  on.
- No update pop-ups: the update check and What's New are off (launcher.ini lgupdatecheck=1 turns the check on;
  it then asks this edition's releases).
