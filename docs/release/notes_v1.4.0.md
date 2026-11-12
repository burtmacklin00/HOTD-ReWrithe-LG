# The House of the Dead: ReWrithe v1.4.0

A fan-made rewrite, **not affiliated with, endorsed by or sponsored by SEGA**. **No game data is included in any
download**: you need your own arcade ROMs (MAME's `hotd`, Revision A) and your own 1998 PC game.

**AI disclaimer:** ReWrithe was made with AI assistance: AI tools helped reverse engineer the original game and combine its code to bring the arcade and PC versions together. No generative AI has been or ever will be used to create artistic assets. Everything you see and hear comes from the original games or from human-made work. Any new model, texture or sound effect will always be made by humans.

**About the sound:** the sound is not "accurate" yet. I tried to make improvements to the arcade sound chip
(GitHub #26: Options > Audio > Sound Chip), but it needs feedback from true fans: if something sounds off compared with
the real cabinet, please tell me which track or effect and where.

Online Survival, Horde Mode (experimental), Character Select, Photo Mode, model packs, editable subtitles, a big VR
update and a round of fixes. Coming from v1.3.x: the game offers the update itself (Update / Not now); your settings and
game files are kept.

## New

- **Horde Mode** (More Modes), **Experimental / Proof of Concept**: the story chapters with far more creatures, weapons
  kept after a continue, pickups from crates, researchers and the bonus room, upgrades, the Magician's Hand works on bosses.
- **Online without port forwarding**: send your Join Code as always; the game now punches through most home routers by
  itself. If yours are strict, your friend gets a Reply Code to send back, and the host pastes the whole chat once for
  the squad. No servers, no accounts (Online Co-op > Address Lookup asks a public STUN server for your address; Off =
  as before). Very strict networks (some mobile / carrier-grade NAT) may still need UPnP or port forwarding: tell us
  whether it worked for you.
- **Survival online co-op**: up to four PCs, peer to peer, the Magician's Hand and every weapon in sync.
- **Survival dog rounds**: every 7th or 8th round is a round of the game's own zombie dogs; clearing it gives a
  **MAX AMMO** for every player.
- **Two new Survival arenas**: Chapter 3's platform and Chapter 4's roof, facing the doorway the Magician blasts open
  (More Modes > Survival > Survival Arena; online too).
- **Character Select** (Options > Gameplay, On): pick your character before Arcade / Chapter Select for 1-4
  players. Rogan and G are the legacy characters and keep their own color and gunshot in any seat.
- **Photo Mode** (F8, or the pause menu; offline only): freeze the game, fly a free camera, change the FOV, hide the HUD,
  full-size screenshots with F12. Idea credited to Tenome.
- **Model packs**: mods can replace models with OBJ / glTF / GLB files or other game models (docs/modding.md). Idea
  credited to Tenome.
- **Editable subtitles** (Options > Gameplay > Subtitles: Off / Original / Custom, and Subtitle Size): change a line's
  text, timing, size or position in `subtitles\en.txt`; the game reloads it while it runs (docs/subtitles.md).
- **The Typing of the Dead 0** (still Experimental): SEGA's rules (after Tenome's TotD EX notes), word packs (your own
  .txt lists), Practice drills, Mutators, Speak the Words (system speech, off by default), a live WPM line and a
  Statistics page.
- **Mods**: Hermit Fly Head Mod (the Hermit wears the game's own unused fly head, arcade models), Rainbow Blood
  afterimages, Menu Blood.
- **Sound Chip** (Options > Audio): the arcade sound with five accuracy fixes over MAME's chip emulation, or MAME's own.
- **17 more languages** (machine translated, 33 in all): Bulgarian, Czech, Danish, Dutch, Filipino, Finnish, Greek,
  Hungarian, Malay, Norwegian, Persian, Portuguese (Portugal), Romanian, Swedish, Thai, Ukrainian, Chinese (Traditional).
- **Cheats** (main menu only, offline only, never saved).

## VR (Experimental)

**All of VR is experimental and still buggy**, PC VR included: it has been played start to finish on a Quest 3 through
SteamVR, but not on other headsets, and rough edges remain (menus, prompts, some effects). Of the two, **PC VR (Quest
Link / Air Link / SteamVR) is the recommended way for now**; the standalone Quest build below is much rougher. Reports
with `settings\hotd.log` are very welcome.

**VR players: please try the VR settings** (Options > Gameplay, the rows marked VR: View (Immersive / Screen), Screen
Distance, Screen Size, Cut-scenes, Comfort Vignette, Laser, Crosshair, Hands in Menus, Dual Pistols) **and tell us
which ones feel best**: we are not sure yet what the defaults should be, and your feedback decides them.

- **The VR launcher**: `The House of the Dead ReWrithe VR.exe` starts straight in VR; `VR Check.bat` tests your
  headset setup. Works with SteamVR and Meta Quest Link.
- **The main menu first, in the headset**: the attract's trigger / A / X opens the main menu on a big screen (point
  with the right controller, trigger = click); a game starts only from the menu.
- **Pause: Y or the left menu button** (Y no longer reloads; grip / A / B still do); "Y / Menu: Pause" shows for a few
  seconds as you start, and the gun points at the pause's items.
- Quitting from SteamVR's (or the runtime's) menu closes the game normally.
- The world all around you; cut-scenes around you or on a big cinema screen (vr.ini `cutscenes`).
- **Comfort options in the game** (Options > Gameplay, also on the Quest): **View (VR)**: Immersive (the world around
  you) or Screen (the whole game on a big screen in front of you, aimed by pointing the gun at it: for players prone
  to motion sickness); **Screen Distance (VR)** (2 / 3 / 4 / 6 m) and **Screen Size (VR)** (Small / Medium / Large)
  for that screen and the menus'; **Cut-scenes (VR)** World / Screen; **Comfort Vignette (VR)** Off / On. Until you
  change one, vr.ini decides as before.
- **Dual Pistols (VR)** (Options > Gameplay): a gun in each hand, each with its own magazine and reloads; the left gun's
  bullets show above the usual row.
- Shots beside or behind the screen no longer fire or reload: aim below the screen (or point the gun down) to reload.
  This also fixes the Hangedman fight, where every shot reloaded.
- The HUD comes forward in front of close creatures; the shot flash sits on the target.
- Shake the gun or flick its nose up to reload (both on by default); recentre and auto-recentre.
- Hands, lasers and crosshairs while you're playing and in first-person cut-scenes (not in the attract or third-person
  cut-scenes); **Laser (VR)** and **Crosshair (VR)** switches in Options > Gameplay. Survival / Horde: **flick the right
  thumbstick left / right to switch weapons**; shaking to reload never skips a cut-scene. **Hands in Menus (VR)** (on by
  default): your hands and gun in front of the menus' big screen.
- Full-screen fades and flashes cover your whole view; the comfort vignette is off by default (vr.ini `vignette = on`).
- Chapter cards, the staff roll, the title and Game Over show on a big screen in the dark.
- The desktop window: vr.ini `desktop_view = eye | both | spectator` (record it with OBS: no capture card needed).
- **Meta Quest 3 standalone (EXTREMELY BUGGY, EXPERIMENTAL)**: `hotd-rewrithe-v1.4.0-quest3-experimental.apk` runs the
  game on the headset itself, no PC needed. It is a first test build: expect bugs, frame drops in busy scenes and
  missing polish (the resolution drops a little only when the headset's GPU falls behind; the sound through OpenSL ES). **For now, playing on your PC with a headset (Quest Link / Air Link / SteamVR) is the recommended way
  to play in VR.** Install: developer mode on, sideload the APK (SideQuest or `adb install`), then copy your own game
  files into the app's folder (docs/quest.md "Install on your Quest 3"). Bug reports with `settings\hotd.log` welcome.
- VR is single player only: no Online Co-op and no 2 / 4 players (Co-op is greyed, "Not available in VR").
- The Typing of the Dead 0 is not playable in VR.

## Changed

- **Magician's Hand** (Survival / Horde): 36 shots instead of 12, a faster reload, more damage and a bigger blast, kept
  when empty (Ammo / Max Ammo refill it), light guns pick it first: after your feedback that it lost runs.
- PC models keep the PC game's own storm sky after the Magician (as THOTD.EXE does).
- US spelling ("color") in all player-facing text.

## Fixed

- Steam Deck fullscreen (#74); Linux: the Light Guns page crash and per-device mice / light guns (#67).
- PC models: the sky after the Magician (#55), the staff roll (#53), the bonus room (#51).
- Widescreen: the side strips no longer show the next area through openings at the 4:3 edge.
- The Magician's waist now flashes white when hit (arcade models, #73).
- Spanish: HOLD YOUR FIRE boxes, the attract's reload board, accented end-screen text, the end screen's chapter number.
- The Typing of the Dead 0: creatures whose word is off screen no longer attack; a clearer miss sound.
- Online: the Magician's Hand no longer desyncs; up to 8 Hand explosions at once; with 3-4 PCs a player is no longer
  dropped at the very end of a game.
- Korean: Curien's name (#77).
- VR: the opening's zombie and researcher no longer sink into the pavement, standing or lying (on the Quest too); the picture upright on SteamVR's virtual
  headset; closing SteamVR mid-game drops back to the flat game instead of freezing.

## Known issues

- **Horde Mode and The Typing of the Dead 0 are experimental / proofs of concept.**
- **The sound is not accurate yet** (see the note at the top).
- **VR is experimental**: tested on SteamVR's virtual headset and in the preview, not on every headset; controller reports welcome.
- **Survival on Chapter 4's roof: some creatures (the big zombie) have glitched textures**; fixed in the next update.
- macOS and the Steam Deck: tested by their automatic builds only.
- Windows Defender may flag `hotd.exe` (a false positive; see v1.3.0's notes for Restore / Allow).
- Everything else as in v1.3.1.


## Downloads

| File | |
|---|---|
| hotd-rewrithe-v1.4.0-win64.zip | Windows 10 / 11 (64-bit) |
| hotd-rewrithe-v1.4.0-linux-x86_64.tar.gz | Linux, Steam Deck |
| hotd-rewrithe-v1.4.0-macos-universal.zip | macOS 11+ (Apple silicon and Intel) |
| hotd-rewrithe-v1.4.0-quest3-experimental.apk | Meta Quest 3 standalone: **extremely buggy, experimental** (PC VR preferred) |
| SHA256SUMS.txt | the SHA-256 of each file |

**For the auto-updater:** the release must carry `hotd-rewrithe-v1.4.0-win64.zip` and `SHA256SUMS.txt` as assets; the
release's description is the changelog the game shows.

Report bugs on the GitHub Issues page with `settings\hotd.log` (never attach game files or ROMs).
