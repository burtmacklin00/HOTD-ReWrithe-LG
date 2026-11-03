// The setup (was the teaser's launcher, docs/teaser_plan.md R4): one dialog in the game's window with the data paths
// (arcade ROMs, the PC game's folder or zip); Start runs the official-files check (game/files_check.h) and goes on to
// the menu. Every other choice is the menu's Options (setup, 2026-10-31: the launcher's own rows are gone); they are
// all kept in the cache folder (launcher.ini, LaunchConfig below). Mouse: the buttons; keys: Enter = Start, Esc = Quit.
#pragma once

#include "game/keybinds.h"
#include "game/sights.h"
#include "render/crt.h"
#include "render/overlay.h"
#include "util/update_check.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <thread>
#include <map>
#include <mutex>
#include <functional>
#include <string>
#include <vector>

namespace hotd::game {

// rate: the "Frame rate" choices in their cycle order (optui, user 2026-10-26: the standard set): 0 the display's
// refresh, 24 .. 360 fps, Arcade (kFrameRateArcade: the Model 2's 57.524 Hz), -1 unlimited (the picture's rate only:
// the logic stays at Game Speed). A rate saved by an older build (100, 180, 540 ...) is kept as it is
constexpr int kFrameRateArcade = 57;
inline const int* frameRates(int& n)
{
    static const int k[] = {0, 24, 30, kFrameRateArcade, 60, 75, 80, 90, 120, 144, 165, 240, 360, -1};
    n = int(sizeof k / sizeof k[0]);
    return k;
}
// the rate in Hz of a cap (0 / -1: none)
inline double frameRateHz(int v) { return v == kFrameRateArcade ? 16000000.0 / (656.0 * 424.0) : v > 0 ? double(v) : 0.0; }
inline int nextFrameRate(int v)
{
    int n;
    const int* k = frameRates(n);
    for (int i = 0; i < n; ++i)
        if (k[i] == v) return k[(i + 1) % n];
    for (int i = 1; i + 1 < n; ++i)   // (an older build's rate: the next one up)
        if (k[i] > v) return k[i];
    return -1;
}
inline std::string frameRateName(int v)
{
    return v == 0 ? std::string("Display Refresh") : v < 0 ? std::string("Unlimited")
           : v == kFrameRateArcade ? std::string("Arcade (57.52)") : std::to_string(v) + " fps";
}
// a launcher.ini / command line value: display | unlimited | arcade | a rate in fps (else the display's refresh)
inline int parseFrameRate(const std::string& s)
{
    if (s == "unlimited") return -1;
    if (s == "arcade") return kFrameRateArcade;
    const int v = std::atoi(s.c_str());
    return v >= 10 && v <= 1000 ? v : 0;
}

// the main menu's song choices: PC CD track, PC sound cue (docs/formats.md), name; the list's order is the cycle's.
// prerel (user 2026-10-27: "add the cut soundtrack (unused) too ... change the final boss music to magician theme
// and change chapter 4 to chapter 4/Main Menu"): the arcade's unused songs (its sound test 8, 10, 13 =
// 0xA01007 / 0xA01009 / 0xA0100C, docs/cut_content.md; no PC track) played by the arcade sound board: code set, track
// = a saved id past the CD's (101..), cue = kArcadeSongCue + the code's low byte (the front end's menuCue; main.cpp
// sends the code to the board). Track 10 (cue 19, MUSIC_STG4BOSS) is the Magician fight's song: named so; track 12
// (cue 22) is the arcade's Chapter 4 song and the PC's main menu song
constexpr int kArcadeSongCue = 1000;
struct MenuSong { int track, cue; const char* name; uint32_t code = 0; };
inline const MenuSong* menuSongs(int& n)
{
    static const MenuSong k[] = {{2, 8, "title"}, {3, 9, "Chapter 1"}, {4, 10, "Chapter 2"}, {9, 16, "Chapter 3"},
                                 {12, 22, "Chapter 4 / Main Menu"}, {6, 12, "boss"}, {10, 19, "The Magician"}, {8, 14, "ending"},
                                 {11, 21, "name entry"},   // (menufb: the clear and game over jingles (cues 13 / 11, no loop) left out:
                                                           // too short for the menu; a saved one: Chapter 4)
                                 {101, kArcadeSongCue + 0x07, "Unused 1 (arcade)", 0xA01007},
                                 {102, kArcadeSongCue + 0x09, "Unused 2 (arcade)", 0xA01009},
                                 {103, kArcadeSongCue + 0x0C, "Unused 3 (arcade)", 0xA0100C}};
    n = int(sizeof k / sizeof k[0]);
    return k;
}
inline const MenuSong& menuSongOf(int track)
{
    int n;
    const MenuSong* k = menuSongs(n);
    for (int i = 0; i < n; ++i)
        if (k[i].track == track) return k[i];
    return k[4];
}
inline int nextMenuSong(int track)
{
    int n;
    const MenuSong* k = menuSongs(n);
    for (int i = 0; i < n; ++i)
        if (k[i].track == track) return k[(i + 1) % n].track;
    return 12;
}

// pads (game/pads.h): one player's gamepad settings. The control styles (each with its own settings, PadStyle):
//   1 Classic pad      held normally, a stick aims (left or right; the other = fine aim), R2 shoots
//   2 Motion only      the gyro aims (the sticks do not)
//   3 Motion + stick   the gyro aims, a stick fine-adjusts (right or left)
//   (relpad, user 2026-10-30: the Sideways / gun grip style is gone, a Switch controller is the better way; launcher.ini
//   files with it load as Motion + stick)
//   4 Touchpad         the touchpad aims (pointer = absolute, or trackpad = relative)
// The touchpad also adds to the other styles (its own setting there). Style "Auto" by the pad's type
// (SDL_GetGamepadType): DualSense / DualShock 4 / Switch Pro / Joy-Con with a gyro: Motion + stick; others: Classic.
constexpr int kPadStyles = 4;
// The aim's feel (worker "padfeel", 2026-10-20; launcher.ini padv=2: older files' feel values are reset to these)
struct PadStyle {
    int speed = 5;        // 1-10: the stick's top speed (Classic: 1.1 .. 5.8 screen heights / s, 5 = 2.4), the
                          // gyro's sensitivity (0.5x .. 2.5x, 5 = 1.0x = 36 degrees of turn across the window's
                          // width), the trackpad's speed (Touchpad)
    int fine = 4;         // 1-10: the fine-aim stick's speed, x 10 % of the aiming speed
    int vert = 10;        // 5-15: the vertical sensitivity, x 10 % of the horizontal
    int dead = 2;         // 0-6: the sticks' radial dead zone 0 / 4 / 8 / 12 / 16 / 20 / 25 % (default 8 %; remapped
                          // from its edge: no jump); full speed from 92 % (the outer dead zone)
    int curve = 2;        // the sticks' response curve: 0 linear, 1 low (power 1.5), 2 medium (2: the default), 3 high
                          // (2.5): a precise centre, a fast edge
    int turbo = 1;        // the aiming stick held at full tilt: 0 off, 1 low (to 1.5x), 2 high (to 2x) over 0.4 s
    int smooth = 1;       // the gyro's tiered smoothing: 0 off, smoothed only below 1 low 3 (default), 2 medium 6,
                          // 3 high 10 degrees / s (faster motion is 1:1; nothing is lost, only the jitter delayed)
    int tighten = 0;      // the gyro's tightening dead zone: 0 off (default), 1-3 degrees / s scaled down below that
    int gaccel = 0;       // the gyro's acceleration: 0 off (default), 1 low (0.8x slow .. 1.4x fast), 2 high (0.6x .. 2x)
    int shake = 2;        // shake to reload (game/shake.h: a flick or jerk): 0 off, 1 low (a hard shake), 2 medium
                          // (default: a quick flick of the wrist), 3 high (the lightest)
    int invert = 0;       // 0 none, 1 x, 2 y, 3 both
    int stick = 0;        // Classic: the aiming stick 0 left, 1 right; Motion + stick: the fine stick 0 right, 1 left
    int shoot = 0;        // the shoot button: 0 auto (R2), 1 R2, 2 L2, 3 R1, 4 L1, 5 South
    int space = 0;        // the gyro: 0 player (default: Gyro Wiki's player space, turning the wrist either way
                          // turns, yaw + roll by gravity), 1 world (the accelerometer's gravity: up stays up when the
                          // pad is tilted or rolled 90 degrees), 2 local (the pad's own axes, as held when recentred)
    int touch = 0;        // the touchpad: 0 auto (Touchpad: pointer; DualSense / DualShock 4: trackpad; else off),
                          // 1 off, 2 pointer (the touch's point = the crosshair's), 3 trackpad (relative)
    int touchClick = 0;   // the touchpad's click / tap: 0 reload, 1 shoot
};
struct PadProfile {
    int style = 0;        // 0 auto, 1-4 (above; launcher.ini padv < 3 had Sideways at 4, Touchpad at 5)
    PadStyle s[kPadStyles];
    int trigger = 3;      // 1-9: the triggers' fire point, x 5 % of the travel (default 15 %: a light press)
    int adaptive = 2;     // DualSense: the adaptive trigger's gun resistance: 0 off, 1 light, 2 medium, 3 strong
    int rumbleHit = 2;    // vibration when the player is hit: 0 off, 1-3 strength
    int rumbleEnemy = 1;  // vibration when the player's shot hits an enemy: 0 off, 1-3
    int rumbleReload = 1; // vibration on a reload: 0 off, 1-3
    // padfix (user 2026-10-30: "players should have a sensitivity option"): Controls > Aim Sensitivity, x 10 % on every
    // aim of the pad (the sticks, the gyro, the trackpad) on top of the style's own Sensitivity; 5-20 (50-200 %)
    int aimSens = 10;
};
// the profile's fields for launcher.ini (pad1.<key>= / pad1.<style>.<key>=) and the More Options > Controller page;
// per style (styleMask: the styles 1-4 it matters for, bit = 1 << (style - 1)) or the player's (styleMask 0)
struct PadField {
    const char* key;
    const char* label;
    int PadStyle::*styleMember;     // a style's field, or
    int PadProfile::*member;        // the player's
    int lo, hi;
    const char* const* names;       // the values' names from lo (null: the number)
    unsigned styleMask;
};
const PadField* padFields(int& n);
// the Controller page's presets for a style's feel: 1 Precise, 2 Balanced (the defaults), 3 Fast; padPresetOf: the
// one the settings match, 0 custom
constexpr int kPadPresets = 3;
extern const char* const kPadPresetNames[kPadPresets + 1];   // Custom, Precise, Balanced, Fast
void applyPadPreset(PadStyle& s, int preset);
int padPresetOf(const PadStyle& s);
extern const char* const kPadStyleKeys[kPadStyles];    // launcher.ini: classic, motion, mix, touch
extern const char* const kPadStyleNames[kPadStyles + 1];   // Auto, Classic Pad, ...

// Sonic Mode (game/sonic.h): the presets 0 Off, 1 Frenzy (x1.2), 2 Overkill (x1.5), 3 Nightmare (names: user
// 2026-10-20; 1 was "Hasty" and 2 "Frenzy" for a few hours)
constexpr int kSonicPresets = 4;
inline const char* sonicKey(int p)   // launcher.ini sonicmode= / --sonic-mode
{
    static const char* const k[kSonicPresets] = {"off", "frenzy", "overkill", "nightmare"};
    return k[p >= 0 && p < kSonicPresets ? p : 0];
}
inline const char* sonicName(int p)   // the menu's
{
    static const char* const k[kSonicPresets] = {"Off", "Frenzy", "Overkill", "Nightmare"};
    return k[p >= 0 && p < kSonicPresets ? p : 0];
}
inline int sonicParse(const std::string& s)   // -1: not a preset ("arcade" / "0" = off)
{
    if (s == "arcade" || s == "0") return 0;
    if (s == "hasty") return 1;   // the first name of preset 1
    for (int p = 0; p < kSonicPresets; ++p)
        if (s == sonicKey(p) || s == std::to_string(p)) return p;
    return -1;
}

struct LaunchConfig {
    std::string arcade;          // a zip or a folder with the ROMs (any names)
    std::string pc;              // the PC game's folder or a .zip of it (pczip)
    int models = 0;              // 0 arcade, 1 PC (world and every model the PC release has)
    int sound = 0;               // 0 auto (the arcade sound board; the PC sound without its ROMs), 1 arcade (the sound board), 2 PC
    int players = 1;             // in play at the start (the other joins with its start button); 3 / 4: the experimental
                                 // 4P mode (docs/coop4.md)
    int p2Input = 0;             // 0 a second mouse, else the keyboard; 1 the keyboard only; 2 a gamepad; 3 a light gun
                                 // (the keyboard keys work for player 2 in every case)
    // pads (game/pads.h): player 1's input 0 the mouse, 2 a gamepad, 3 a light gun (an absolute pointer device)
    int p1Input = 0;
    // 4P mode (players 3 / 4, docs/coop4.md): 0 a mouse of their own (raw input), 1 the keyboard only, 2 a gamepad
    // (default), 3 a light gun (the keyboard keys work with any of them)
    int p3Input = 2, p4Input = 2;
    // computer teammates (local play): seat p (1-3: players 2-4; 3 / 4 in the 4P mode) 0 human, 1 easy, 2 normal, 3 hard
    // (launcher.ini p2bot / p3bot / p4bot)
    int seatBot[4] = {};
    PadProfile pad[4];           // per player (launcher.ini pad1.<field>= .. pad4.<field>=)
    // keybind (game/keybinds.h): each player's keyboard keys and gamepad buttons (launcher.ini key1.<action>= /
    // padbind1.<action>=, written only where they differ from the defaults: none = today's keys)
    KeyBinds keys[4] = {defaultKeys(0), defaultKeys(1), defaultKeys(2), defaultKeys(3)};
    PadBinds padBinds[4];
    // light guns (game/pads.h LightGuns): the raw point (window fractions) each player's gun read when it shot the
    // calibration's two targets at (0.1, 0.1) and (0.9, 0.9) of the window; the defaults = no correction
    float gunCal[4][4] = {{0.1f, 0.1f, 0.9f, 0.9f}, {0.1f, 0.1f, 0.9f, 0.9f}, {0.1f, 0.1f, 0.9f, 0.9f}, {0.1f, 0.1f, 0.9f, 0.9f}};
    // ipolish (Controls > Light Guns): each seat's own gun / mouse (its raw-input device path; empty: the first one
    // used) and its name shown. launcher.ini gun1dev= / gun1name= .. gun4
    std::string gunDev[4], gunName[4];
    // padpass (user 2026-10-26 "light guns typically rely on white screen flashes"): the light guns' kind 0 IR / mouse
    // mode (Gun4IR, AimTrak, a Wii Remote: no flash needed), 1 Sinden (a white border around the picture), 2 CRT (the
    // arcade's white shot flash: the gun reads the beam); the shot flash 0 Auto (on for CRT guns), 1 Off, 2 On; the
    // border 0 Auto (on for a Sinden), 1 Off, 2 Thin, 3 Medium, 4 Thick. Draw-only. launcher.ini guntype= gunflash=
    // gunborder=; --shot-flash = gunflash 2
    int gunKind = 0, gunFlash = 0, gunBorder = 0;
    // padglyphs: Controls > Button Icons: 0 Auto (each pad its own model's; the keys after a key), 1-13 one model's
    // icons on every pad, 14 Keyboard (pad_prompts.h kButtonIconNames; launcher.ini padprompts=; global)
    int padPrompts = 0;
    // padglyphs: Controls > Button Style: 0 Xelu (default), 1 Kenney, 2 Simple (drawn) (launcher.ini buttonstyle=)
    int buttonStyle = 0;
    int iconOutline = 0;   // menus3: Controls > Icon Outline: Xelu's icons with a light rim (launcher.ini iconoutline=; 0 Off)
    // padglyphs: the menus' cursor from player 1's pad (menu_cursor.h): Touchpad Cursor 0 Off / 1 On, Gyro Cursor
    // 0 Off / 1 While Holding ZL / L2 / LT / 2 Always, Cursor Speed 1-10 (launcher.ini touchcursor= gyrocursor= cursorspeed=)
    int touchCursor = 1, gyroCursor = 1, cursorSpeed = 5;
    bool calibrateNext = false;  // padpass: the next game with a light gun starts with its calibration (not saved)
    int difficulty = 2;          // 0-4 VERY EASY .. VERY HARD
    int lives = 9;               // the lives setting 0-11 (table 0xD780)
    int blood = 0;               // 0 red, 1 green, 2 blue, 3 purple, 4 yellow (PC models), 5 custom (display: experimental)
    int bloodRgb = 0xFFFFFF;     // the custom colour 0xRRGGBB (launcher.ini bloodrgb=RRGGBB; white the preset)
    bool cuts = true;            // teaser cut (1): default route + guards past unported objects
    bool title = true;           // the arcade's title screen and attract loop before Chapter 1 (start button: the game)
    bool fbi = false;            // the arcade attract's "WINNERS DON'T USE DRUGS" (FBI) screen; off by default
    bool pcSplash = true;        // feat29 (pmpick: default On, user 2026-10-30): the 1998 PC's start-up screens (its SEGA logo, then its title art) before the menu; launcher.ini pcsplash=
    // display (firing range: per player, game/sights.h SeatSight): the style 0 auto (follows the models), 1 arcade gun
    // marks, 2 the PC's, 3 simple cross, 4 ring + dot, 6 corner brackets, 7 gap cross, 10 capped cross, 9 custom (5 / 8 removed), the
    // custom set's name, the size step 0-4 (2 medium), the thickness step 0-3 (1 = 2 pixels at 1080 lines), the colour
    // (0 the player's). launcher.ini crosshair= crosshaircustom= crosssize= crossthick= crosscolour= (player 1), the
    // same with 2 / 3 / 4 after them (the others; missing: player 1's)
    SeatSight sights[4];
    int aspect = 0;              // display: 0 widescreen (hor+: the whole window), 1 the original 4:3 (pillarboxed), 2 16:10 (hor+ up to 16:10, pillarboxed past it)
    bool fullscreen = true;      // display: start in fullscreen (launcher.ini fullscreen= / displaymode=; F11 in play keeps it; --windowed / --fullscreen)
    // rescrash (Video > Display Mode: Windowed / Borderless / Fullscreen): the fullscreen is exclusive (in the
    // Resolution's display mode), else borderless at the desktop's size (launcher.ini exclusive=, displaymode= 0 / 1 / 2)
    bool exclusive = false;
    // display (user 2026-10-21): Options > Video > Resolution: the fullscreen one (exclusive fullscreen in that display
    // mode; 0 x 0 Auto = the desktop's) and the window's size (0 x 0 Auto = as the player sizes it);
    // launcher.ini resolution_fullscreen= / resolution_window= (auto or WxH; util/resolution.h)
    int resFullW = 0, resFullH = 0, resWinW = 0, resWinH = 0;
    int sonic = 0;               // Sonic Mode (game/sonic.h): 0 off (the arcade's speed), 1 Frenzy, 2 Overkill, 3 Nightmare (launcher.ini sonicmode=)
    // the 4P mode's bosses (docs/coop4.md): their hit points x (1 + boss4p % x (players - 2)) with 3 / 4 players;
    // launcher.ini boss4p= (percent, default 50: cpuai 2026-10-26, was 25; --4p-boss-step overrides it); Gameplay page
    int boss4p = 50;
    // the 4P mode's creatures (docs/coop4.md "Enemy pressure"): with 3 / 4 players a hit's damage / (1 + enemy4p % x
    // (players - 2)); launcher.ini enemy4p= (percent; --4p-enemy-step overrides it); Gameplay page
    int enemy4p = 50;
    // Teammate Flashes (docs/coop4.md section 13): the other players' shot flashes / sparks: 0 Auto (Full with 1-2
    // players, Reduced with 3-4), 1 Full, 2 Reduced, 3 Off; launcher.ini teamflash=; Gameplay page
    int teamFlash = 0;
    // trailer: Trailer Mode (Options > Advanced; F7 in play toggles it for this run only): no HUD, captions, sights or
    // cursor in play (draw-only); launcher.ini trailer=
    bool trailer = false;
    // Hit Sparks / Gun Muzzle Flash (Video page): off hides the spark bursts where the player's bullets hit / the cut-scene
    // guns' muzzle flash (draw-only); launcher.ini hitsparks= / muzzleflash=
    bool hitSparks = true;
    bool muzzleFlash = true;
    bool bloodParticles = true;   // Blood Particles: off hides the blood spray / bursts (draw-only); launcher.ini bloodparticles=
    // players 3 / 4 in the cut-scenes (docs/coop4.md section 16): the variation picked per scene, "00:B,09:C" (a scene
    // not named: A, the look of sections 10-12); launcher.ini scene4p=, --scene4p
    std::string scene4p;
    // actors2: the cut-scene viewer's Actors (game/scene_view.h SceneView::castString, "rogan,g,p3,p4"); launcher.ini
    // scenecast= (empty: every part its own)
    std::string sceneCast;
    bool autoReload = false;     // hud3: an empty gun reloads by itself (as a shot off the screen; not in the originals)
    bool updateCheck = false;    // Options > System > Check for Updates (util/update_check.h). Light Gun Edition: off
                                 // by default and its own key (lgupdatecheck=), so an old updatecheck=1 never pops up
                                 // in a front-end; when on it asks the Light Gun Edition's releases, not the original's
    // Light Gun Edition: outputs for Hook of the Reaper and the like (game/game_outputs.h); launcher.ini outputs= (1 on,
    // 0 off) and outputsport= (the TCP port, 8000 as MAME / DemulShooter)
    bool outputs = true;
    // Light Gun Edition: the players' crosshairs in the game (Options > Video > Show Crosshairs; launcher.ini
    // crosshairs=1 / 0; "2 - Crosshairs - On / Off.exe"). Off: none drawn in play (the menus' Firing Range still shows them)
    bool crosshairs = true;
    int outputsPort = 8000;
    // GitHub #17 (user 2026-11-03 "HOTD 2 style reloading"): a mouse crosshair moved onto the screen's edge reloads,
    // no shot needed (launcher.ini edgereload=)
    bool edgeReload = false;
    // Light Gun Edition: the fun modes (Options > Gameplay; local games only, never online). launcher.ini flashlight=,
    // bigheads=, shotgun=, partyboard=, hardcore=, freefire=
    bool flashlight = false;   // the screen dark, each player's gun a light where it aims (draws only)
    bool bigHeads = false;     // the creatures' heads drawn larger (draws only)
    bool shotgun = false;      // a wider blast (the shot test's radius x2.5), 3 shells a load
    bool partyBoard = true;    // co-op: each player's score, shots, hits and accuracy at a chapter's end, the MVP
    bool hardcore = false;     // one life, no continues, its own high scores
    bool freeFire = false;     // unlimited ammo, no reloading
    // setup2: the version that last ran with these settings (launcher.ini lastversion=; another one: What's New once)
    std::string lastVersion;
    bool attractGore = true;     // Gameplay "Attract Gore": the attract demo blows parts off as the 1998 PC (port::gAttractGore)
    bool scoreDisplay = false;   // scoredisp: the arcade's score line (title code LLLRR's SCORE:%05d) in every offline game
    // skipfix (user 2026-10-27): the cut-scenes' hold-to-skip (main.cpp; only scenes the arcade cannot skip, the bars
    // up); Gameplay "Hold to Skip Cut-scenes", launcher.ini holdskip=
    bool holdSkip = true;
    // gametext: the game's own texts (subtitles / voice-message sprites, chapter cards, RELOAD hint, attract tutorial)
    // 0 English, 1 Japanese (the JAPAN region's, text only: the logic keeps the USA region); launcher.ini gametext=
    int gameText = 0;
    // lang (translations, docs/translating.md): the menus' language, data\lang\<code>.txt ("en" English; "": not
    // chosen yet: the first start picks Windows' UI language when a file for it exists); launcher.ini lang=
    std::string language;
    // rate: the game logic's tick rate: 0 arcade (the Model 2's vblank, 16 MHz / (656 x 424) = 57.524 Hz: what the game
    // is), 1 60 Hz (THOTD.EXE's timeGetTime x 60 / 1000 loop, pc_main_loop 0x419168). launcher.ini gamespeed=, --game-speed
    // -1 auto (the default; launcher.ini gamespeed=auto): arcade games at the arcade rate, PC Mode at 60 Hz as the PC port
    int gameSpeed = -1;
    // PC Mode's Continues (THOTD.EXE Options 0xD5B1E0: 0-9, default 5, config_load 0x421DF4; pc_continues_init
    // 0x4488E4: credits = this + 1, a start / join / continue costs 1). launcher.ini continues=; arcade games: free play
    int continues = 5;
    // the Cheat screen's Free-Play (THOTD.EXE 0xD5B35C, kept in thotd.cfg; pc_continues_init 0x4488E4: 0xDA0B8C = it):
    // PC Mode's continues never run out. launcher.ini freeplay=
    bool freePlay = false;
    // rate: presentation. frameRate: 0 = the display's refresh, a cap in fps (kFrameRates), -1 = unlimited (no cap, vsync
    // off); vsync: wait for the display's refresh (frameRate -1 overrides it); smooth: the 3D picture blended between
    // the last two game frames (render interpolation; game state untouched). launcher.ini framerate= display | unlimited
    // | N, vsync=, smooth=; --frame-rate, --vsync, --smooth
    int frameRate = 0;
    bool vsync = true;
    // angle (render/gl_backend.h): Video > Renderer: 0 Auto (OpenGL 3.3, else DirectX through ANGLE), 1 OpenGL, 2 DirectX
    // (ANGLE). launcher.ini renderer= auto | gl | angle, --renderer; applied at the next start
    int renderer = 0;
    bool smooth = true;
    // clipfix: Draw Depth: 0 Depth Buffer (per pixel), 1 Arcade Sort (the Model 2's polygon sort, the default;
    // render/renderer.h DrawDepth). Advanced only (depthopt: no menu row): launcher.ini drawdepth=buffer | sort; --draw-depth
    int drawDepth = 1;
    // online co-op (src/net/online_menu.h): this player's name in the waiting rooms (empty: the Windows user name) and
    // the in-game connection info (0 off, 1 small: the ping in a corner, 2 detailed: the F3 line). launcher.ini
    // netname=, netinfo=
    std::string netName;
    int netInfo = 1;
    // online PC Mode (docs/netplay.md "PC Mode online"): the Host page's Game Mode (netpcmode= 0 arcade / 1 PC Mode) and
    // this PC's characters (netpcchar=a,b: the first / second seat, 1-6, 0 Auto; the Host and Join pages share them)
    bool netPcMode = false;
    int netPcChar[2] = {};
    // online Boss Mode (netdata): the Host page's Game Mode Boss Mode and its Boss (netbossmode= 0 none, 2 Chariot .. 5
    // Magician, 6 All Bosses)
    int netBossMode = 0;
    // remakefix (user 2026-10-22): the monitor the game opens on (Options > Video > Display; launcher.ini display=, the
    // display's name, " #2" for a second one of that name; empty or not found: the primary display)
    std::string display;
    bool music = true;          // audio: the music (sound effects stay on); set in the launcher only
    int musicVolume = 100;       // audio: 0-100 % (steps of 10) of the music level
    int sfxVolume = 100;         // audio: 0-100 % (steps of 10) of the sound-effect level
    // Add to Steam: the player's chosen picture per kind (cover, banner, hero, logo, icon), relative to the art folder
    // (steam::artDir); empty = the one made from the PC files
    std::string steamArt[5];
    // the Gallery (game/gallery.h): the entry and background shown last (launcher.ini gallery= the entry's key,
    // gallerybg=), so it opens where the player left it
    std::string galleryEntry;
    int galleryBg = 0;
    int menuStyle = 1;           // remakemenu: the menu's look: 0 Classic PC (the 1998 PC port's), 1 Remake (wide; frontend.h;
                                 // logo2: the default)
    // menuperf (user 2026-10-26): the Remake menu's live background (the attract running behind it): 0 Auto (off on
    // integrated graphics, or when the menu's frames take too long), 1 On, 2 Off (livebg=; --live-bg auto|on|off)
    int liveBackground = 0;
    // optui (user 2026-10-26: "the menu lags"): the menus' own frame rate cap (all front-end pages, the Gallery, the
    // Jukebox; the game follows frameRate): 0 Auto (by the graphics: 120 a card, 60 when the menu runs slow, 30
    // integrated), 30, 60, 120; never above the display's refresh (menufps=)
    int menuFps = 0;
    uint32_t menuBlood = 0;     // blood2: the Remake menu's splatter pattern (menublood=; 0 none yet: a random one at the start)
    int galleryLayout = 0;   // gallery3: 0 the style's own (Classic; regallery: the Remake style's Resident Evil-like page), 1 the portraits (gallerylayout=; key K), 2 the Classic page in the Remake style
    int menuMusic = 12;          // audio: the main menu's song, a PC CD track 2-12 (default 12: Chapter 4, what the PC played; 11 = name entry)
    int musicNumbering = 0;      // audio: the music files' numbers: 0 auto, 1 the PC CD's, 2 the Saturn disc's (audio.cpp)
    // mods (docs/modding.md; texture packs: docs/mods.md): the mods switched on, highest priority first (launcher.ini
    // mods=a,b), and the ones switched off (modsoff=); a mod in neither list is new: its mod.ini default (texture packs
    // on, the built-in Ragdoll off), after the others
    std::vector<std::string> modsOn, modsOff;
    // the mods' settings, "<mod>.<key>" -> the value chosen (launcher.ini modset.<mod>.<key>=; per PC, never synced)
    std::map<std::string, std::string> modSettings;
    // crt (render/crt.h): the CRT filter's preset and Custom's settings (launcher.ini crt=off|arcade|tv|sharp|custom,
    // crt.scan= crt.mask= crt.masktype= crt.curve= crt.bloom= crt.bright= (0-10), crt.crispsights= crt.shotfiltered=)
    CrtSettings crt;
    int chapter = 0;             // select: the chapter to start, 0-3 (launcher.ini chapter=1-4); only a playable one (game/chapters.h)
    // a portable package's folder (not saved): arcade / pc paths inside it are saved relative to it, so a moved or
    // copied package still finds its data\ (user 2026-10-17)
    std::string base;
    // test runs without HOTD_PREF_DIR (headless, --input-test, --frontend-shot, ...): save() writes nothing, so a test
    // never changes the player's launcher.ini (main.cpp)
    static inline bool readOnly = false;
    bool load(const std::string& file);
    void save(const std::string& file) const;
};

// setup2 (v1.1.0, user 2026-11-02 "a modern setup"; coordinator: "simple, like a classic game installer"): the setup
// window (960 x 600, fixed; drawn here, no images but our wordmark). Flat and dark, one accent (the game's red), a row
// of steps at the top, Back / Next at the bottom. Modes:
//   Setup     the first start (or a failed files check): Install to (folder, Browse..., free space) > Game files (two
//             drop zones, Arcade ROMs and PC game: a zip, a folder or a CD image dropped or browsed, checked at once:
//             green / yellow (accepted with a note) / red and why) > Options (Desktop shortcut, Start menu entry, Add
//             to Steam, the CD music) > Install (progress; or Play from here) > Play (game/installer.h)
//   Repair    Options > System > Repair / Change files: the Game files page; an install imports them again
//   WhatsNew  once after an update (launcher.ini lastversion=), and Options > System > What's New: CHANGELOG.txt's
//             section of this version
//   Update    a newer GitHub release (util/update_check.h): its version and changelog, Update / Not now; Update
//             downloads, checks the SHA-256 and hands over to bin\hotd_launch.exe --update (the game quits)
// The files check's API (game/files_check.h) decides; this only shows it. Keys: Enter = Next / Play, Esc = Back / Quit.
class Launcher {
public:
    enum class Result { None, Start, Quit };
    enum class Mode { Setup, Repair, WhatsNew, Update };
    enum Page { kWelcome, kFiles, kOptions, kInstall, kDone, kPages };
    ~Launcher();
    bool init(std::string& err);
    void handle(const SDL_Event& e, SDL_Window* window);
    // draws the screen; returns Start once the check passed (then `pcOk` / `pcFiles` tell what the PC check found)
    Result frame(int windowWidth, int windowHeight);
    // another mode (Repair: the Game files page with the current files checked; WhatsNew: `whatsNew`; Update: the
    // release util::updateInfo found)
    void open(Mode m);
    void setPage(int p) { step_ = p; }
    int page() const { return step_; }

    LaunchConfig cfg;
    std::string cacheDir;        // regions, check results, launcher.ini
    std::vector<std::string> status;   // the check's report / messages
    bool pcOk = false;
    std::map<std::string, std::string> pcFiles;
    std::vector<std::string> notes;    // extra lines under the status (the dev data the teaser still needs)
    // ours: an extra section under the buttons (online co-op: src/net/online_menu.h's rows); a clickable row's label
    // is its button, extraClick(row) runs it (setup2: not shown any more; online co-op is in the menu)
    struct ExtraLine { std::string label, value; bool clickable = false; };
    std::string extraTitle;
    std::function<std::vector<ExtraLine>()> extraLines;
    std::function<void(int)> extraClick;
    std::function<bool()> extraWhen;
    // remakefix (user 2026-10-22 "hide the debug menu at the start"): quiet: nothing drawn (a black screen) while the
    // start-up's own files check runs (the files known: straight on to the menu); a failed check shows the page
    bool quiet = false;
    bool checked = false;        // the files check ran at least once (pcOk is its verdict)
    std::string wordmarkFile;    // our "ReWrithe" wordmark (data\ui\rewrithe_wordmark.png; empty: text only)
    bool windowed = false;       // drawn as its own fixed window (the host's)
    float wantW = 960, wantH = 600;   // the window's size in units of the display's scale
    // setup2: where this runs from: packageRoot = the package's folder ("" a developer build: Play from here only);
    // installed = it is an install (hotd_install.txt); version = this build's ("v1.1.0")
    std::string packageRoot, version;
    bool installed = false;
    Mode mode = Mode::Setup;
    std::string whatsNew;        // the What's New page's text (CHANGELOG.txt's section, markdown)
    std::string restartArgs;     // (testing) the updater starts the game again with these
    // testing (HOTD_SETUP_SHOTS): a zone's result put straight in (no check run)
    void testZone(int which, const std::string& path, int state, const std::string& line, const std::vector<std::string>& more);
    void testInstallProgress(float f, const std::string& line);
    void testDone(const std::string& dest);
    // a zone's source checked on its own thread (0 arcade, 1 PC); its state (1 while it runs)
    void checkZone(int which, const std::string& path);
    int zoneState(int which) const;
    // (testing, HOTD_ZONE_TEST) the box's verdict as one line: state, line, the lines under it, the music found
    std::string zoneReport(int which) const
    {
        const Zone z = zoneCopy(which);
        std::string s = std::to_string(z.state) + " | " + z.line;
        for (const std::string& m : z.more) if (!m.empty()) s += " | " + m;
        return s + " | music " + std::to_string(z.music);
    }
    std::string shotFile;        // pchotfix: set: render() writes the page to this PNG instead of drawing it (--setup-shot)
    // pchotfix: --setup-shot: the files check, then the Game files page (960 x 600) into `png`; returns whether Play is
    // allowed
    bool shotPage(const std::string& png)
    {
        shotFile = png;
        windowed = true;
        const bool ok = runCheck();
        step_ = kFiles;
        render(int(wantW), int(wantH));
        return ok;
    }

private:
    struct Hot {
        int x0, y0, x1, y1, action;
    };
    // a drop zone: its source, the check's verdict (0 empty, 1 checking, 2 ok, 3 ok with a note, 4 blocked)
    struct Zone {
        std::string path;        // as chosen
        std::string checked;     // what the check read (a disc image: the folder it was copied to)
        int state = 0;
        std::string line;        // the verdict, one short line
        std::vector<std::string> more;   // a few plain lines under it
        int music = 0;           // (PC) CD tracks found
        float progress = 0;      // while checking (state 1): 0..1, the box's loading bar (user 2026-11-03)
    };
    Overlay page_;
    std::vector<Hot> hot_;
    std::string drawnKey_;       // what the picture shows (rebuilt only when it changes)
    float drawX_ = 0, drawY_ = 0, scale_ = 1;
    std::vector<uint8_t> word_;  // the wordmark's RGBA
    int wordW_ = 0, wordH_ = 0;
    bool wordTried_ = false;
    bool arcadeOk_ = false;      // the last check passed the arcade ROMs
    int pressed_ = -1, hover_ = -1, dropHover_ = -1;
    Result pending_ = Result::None;
    bool checkRequested_ = false, startAfterCheck_ = false;
    std::mutex dialogMutex_;
    std::string dialogResult_;
    int dialogTarget_ = 0;       // 1 arcade, 2 pc, 3 the install folder
    int step_ = kWelcome;
    std::string installDir_;
    bool optDesktop_ = true, optStartMenu_ = true, optSteam_ = false, optMusic_ = true;
    // the zones' checks (one thread each) and the install / the update download (their own threads)
    mutable std::mutex zoneMutex_;
    Zone zone_[2];
    std::thread zoneThread_[2];
    std::thread installThread_;
    std::atomic<int> installState_{0};   // 0 not started, 1 running, 2 done, 3 failed
    std::atomic<float> installFrac_{0};
    std::string installLine_, installErr_, installedExe_;
    std::unique_ptr<util::UpdateDownload> download_;
    util::UpdateInfo update_;
    std::string updateErr_;
    int scroll_ = 0, scrollMax_ = 0;
    void click(int action, SDL_Window* window);
    bool runCheck();
    void render(int windowWidth, int windowHeight);
    void startInstall();
    Zone zoneCopy(int which) const { std::lock_guard<std::mutex> l(zoneMutex_); return zone_[which]; }
    bool zonesReady() const;
    void next(SDL_Window* window);
    void back();
};

} // namespace hotd::game
