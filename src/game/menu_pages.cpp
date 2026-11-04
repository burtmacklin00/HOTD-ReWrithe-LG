// The menu's settings pages (menu_pages.h). The rows were main.cpp's More Options list; the values, the cycles and the
// launcher.ini keys are the same, grouped by category, each with its help line.
#include "game/menu_pages.h"

#include "game/chapters.h"
#include "game/lang.h"
#include "game/pad_prompts.h"
#include "game/pads.h"
#include "render/bidi.h"
#include "render/crt.h"
#include "render/gl_backend.h"
#include "render/renderer.h"
#include "util/png_read.h"
#include "util/utf8.h"
#include "game/preview_art.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>

namespace hotd::port { extern bool gAttractGore; }
namespace hotd::game {

const char* menuCategoryName(int category)
{
    static const char* const k[kMenuCategories] = {"Video", "Audio", "Controls", "Gameplay", "Mods", "System"};
    return category >= 0 && category < kMenuCategories ? k[category] : "";
}

namespace {
using X = Frontend::Extra;
std::string onOff(bool b) { return b ? "On" : "Off"; }
// uxpolish: the menu's values start with a capital (the launcher.ini / command-line names stay lower case)
std::string capital(const char* s)
{
    std::string t = s ? s : "";
    if (!t.empty() && t[0] >= 'a' && t[0] <= 'z') t[0] = char(t[0] - 'a' + 'A');
    return t;
}
// uxpolish: a row that opens another page ends its value with " >" (Settings >, Open > ...)
std::string page(const char* s) { return std::string(s) + " >"; }
bool gunCalibrated(const float* g) { return !(g[0] == 0.1f && g[1] == 0.1f && g[2] == 0.9f && g[3] == 0.9f); }
// Light Gun Edition: no EXPERIMENTAL tag here: every row that had one is light guns or the 4-player mode, the
// edition's own features (custom blood and online with 3-4 PCs say so in their help lines)
X& experimental(std::vector<X>& r, std::function<bool()> when = []() { return true; })
{
    (void)when;
    return r.back();
}
// remakefix (user 2026-10-22): the last row's values in order (exactly as its value() gives them): the Remake style's
// dropdown list (a click steps the row to the one picked)
void listed(std::vector<X>& r, std::vector<std::string> names) { r.back().choices = std::move(names); }
std::vector<std::string> namesOf(int n, const std::function<std::string(int)>& f)
{
    std::vector<std::string> v;
    for (int i = 0; i < n; ++i) v.push_back(f(i));
    return v;
}
// optui (user 2026-10-26 "some menu options I expect to have a dropdown and not all do"): the last row's values with
// the current one's index (-1: none of them) and the setter of value k (the list sets it at once)
void indexed(std::vector<X>& r, std::vector<std::string> names, std::function<int()> now, std::function<void(int)> set)
{
    r.back().choices = std::move(names);
    r.back().choiceNow = std::move(now);
    r.back().pickAt = std::move(set);
}
// fix28: the last row's comparison pictures (MenuHooks::previewPic of "<kind>_<choice>"; choice -1: whether any can be made)
void previewed(std::vector<X>& r, const MenuHooks& h, std::string kind)
{
    if (!h.previewPic)
        return;
    const auto pic = h.previewPic;
    r.back().preview = [pic, kind](int k) { return pic(k < 0 ? std::string() : kind + "_" + std::to_string(k)); };
}
// a 0 .. 100 % volume in steps of 5 (prerel, user 2026-10-27: "allow increments of 5"; a value between the steps:
// none of them current)
void volumeList(std::vector<X>& r, int& v, std::function<void()> save)
{
    indexed(r, namesOf(21, [](int i) { return std::to_string(i * 5) + "%"; }), [&v]() { return v % 5 == 0 ? v / 5 : -1; },
            [&v, save](int k) { v = k * 5; save(); });
    // menufb (user 2026-10-26: "the music and sfx volume should have a bar"): a bar the mouse drags
    r.back().barGet = [&v]() { return v; };
    r.back().barSet = [&v, save](int n) { v = n; save(); };
    r.back().barStep = 5;   // (the drag snaps to 5 %; Left / Right step 5 %)
}
// the Crosshair row's built-in styles in the list's order (sights.h; 5 / 8 removed) and their names
constexpr int kSightList[] = {0, 1, 2, 3, 4, 6, 7, 10};
constexpr int kSightListN = int(sizeof kSightList / sizeof kSightList[0]);
const char* sightMenuName(int style)
{
    static const char* const k[kSightStyles] = {"Auto", "Arcade", "PC", "Cross", "Ring + Dot", "Dotted Ring", "Brackets",
                                                "Gap Cross", "Pointer", "", "Capped Cross"};
    return k[std::clamp(style, 0, kSightStyles - 1)];
}
}

// menufb (user 2026-10-26: "I think Teammate Crosshair Flashes make more sense ... local mp everyone is full ... one
// player on each PC then everyone else is dimmed but that player is full. By default everyone is full"): the shot
// flashes of the players on another PC (online); the players on this PC always in full (Gameplay > Advanced and the
// Firing Range, where the players past the mouse's show it)
static void teamFlashRow(std::vector<X>& r, LaunchConfig& c, std::function<void()> save)
{
    r.push_back({"Teammate Crosshair Flashes", [&c]() {
                     static const char* const k[3] = {"Full", "Reduced", "Off"};
                     return std::string(k[std::clamp(c.teamFlash - 1, 0, 2)]);
                 },
                 [&c, save]() { c.teamFlash = c.teamFlash <= 1 ? 2 : c.teamFlash == 2 ? 3 : 1; save(); },
                 "Online: the other PCs' players' shot flashes in full, reduced or off. Players on this PC always show in full"});
    indexed(r, {"Full", "Reduced", "Off"}, [&c]() { return std::clamp(c.teamFlash - 1, 0, 2); },
            [&c, save](int k) { c.teamFlash = k + 1; save(); });
}

// the light gun's calibration row (Controls, a Controller page): Reset when the gun was calibrated, else grey with how
std::vector<Frontend::Extra>& lightGunRow(std::vector<Frontend::Extra>& r, const std::string& label, float* cal,
                                          std::function<void()> save)
{
    r.push_back({label, [cal]() { return std::string(gunCalibrated(cal) ? "Reset" : "Not Calibrated"); },
                 [cal, save]() {
                     const float id[4] = {0.1f, 0.1f, 0.9f, 0.9f};
                     std::copy(id, id + 4, cal);
                     if (save) save();
                 },
                 "Reset the light gun's calibration (F9 in the game calibrates it again)"});
    r.back().off = [cal]() {
        return gunCalibrated(cal) ? std::string() : std::string("Not calibrated yet: press F9 in the game to calibrate the light gun");
    };
    experimental(r);
    return r;
}

std::vector<Frontend::Extra> menuGunAssignRows(LaunchConfig& c, const MenuHooks& h)
{
    std::vector<X> r;
    for (int p = 0; p < 4; ++p) {
        r.push_back({"Player " + std::to_string(p + 1) + (p >= 2 ? " (4P)" : ""), [&c, h, p]() {
                         if (h.gunWaiting && h.gunWaiting() == p)
                             return "Pull the Trigger... " + std::to_string(h.gunWaitLeft ? h.gunWaitLeft() : 0);
                         if (c.gunDev[p].empty())
                             return std::string("First Used");
                         const std::string n = c.gunName[p].empty() ? std::string("Device") : c.gunName[p];
                         return h.gunConnected && !h.gunConnected(c.gunDev[p]) ? n + " (Not Connected)" : n;
                     },
                     [h, p]() { if (h.gunAssign) h.gunAssign(p); },
                     "Click, then pull the trigger of the gun (or click the mouse) for Player " + std::to_string(p + 1) +
                         ". Esc: cancel"});
    }
    for (int p = 0; p < 4; ++p) {
        r.push_back({"Clear Player " + std::to_string(p + 1), nullptr, [&c, h, p]() {
                         c.gunDev[p].clear();
                         c.gunName[p].clear();
                         if (h.save) h.save();
                     },
                     "Player " + std::to_string(p + 1) + " takes the first light gun / mouse used again"});
        r.back().off = [&c, p]() { return c.gunDev[p].empty() ? std::string("No device assigned") : std::string(); };
    }
    r.push_back({"Clear All", nullptr, [&c, h]() {
                     for (int p = 0; p < 4; ++p) c.gunDev[p].clear(), c.gunName[p].clear();
                     if (h.save) h.save();
                 },
                 "Every player takes the light guns / mice in the order first used (as before)"});
    return r;
}

// menufb (user 2026-10-26: "you should add a reset to default option"): a page's last row: Reset to Defaults (the host
// asks first: MenuHooks::resetPage, then menuReset)
static void resetRow(std::vector<X>& r, const MenuHooks& h, int page)
{
    r.push_back({"Reset to Defaults", nullptr, [h, page]() { if (h.resetPage) h.resetPage(page); },
                 "Put every setting on this page back to its default (asks first)"});
}

void menuReset(LaunchConfig& c, int page)
{
    const LaunchConfig d;
    switch (page) {
    case kCatVideo:
        c.models = d.models, c.aspect = d.aspect, c.menuStyle = d.menuStyle, c.liveBackground = d.liveBackground;
        c.fullscreen = d.fullscreen, c.exclusive = d.exclusive, c.frameRate = d.frameRate, c.menuFps = d.menuFps, c.vsync = d.vsync, c.smooth = d.smooth;
        c.crosshairs = d.crosshairs, c.hitSparks = d.hitSparks, c.muzzleFlash = d.muzzleFlash, c.bloodParticles = d.bloodParticles;
        c.resFullW = d.resFullW, c.resFullH = d.resFullH, c.resWinW = d.resWinW, c.resWinH = d.resWinH;
        c.crt = d.crt;
        break;
    case kCatAudio:
        c.sound = d.sound, c.sfxVolume = d.sfxVolume, c.music = d.music, c.musicVolume = d.musicVolume, c.menuMusic = d.menuMusic;
        break;
    case kCatControls:
        c.p1Input = d.p1Input, c.p2Input = d.p2Input, c.p3Input = d.p3Input, c.p4Input = d.p4Input;
        c.gunKind = d.gunKind, c.gunFlash = d.gunFlash, c.gunBorder = d.gunBorder;   // (padpass)
        c.padPrompts = d.padPrompts, c.buttonStyle = d.buttonStyle, c.iconOutline = d.iconOutline;   // (padglyphs; menus3)
        c.touchCursor = d.touchCursor, c.gyroCursor = d.gyroCursor, c.cursorSpeed = d.cursorSpeed;
        for (int p = 0; p < 4; ++p) c.gunDev[p].clear(), c.gunName[p].clear();   // (ipolish: Light Guns: Assign)
        break;
    case kCatGameplay:
        c.blood = d.blood, c.bloodRgb = d.bloodRgb, c.autoReload = d.autoReload, c.edgeReload = d.edgeReload, c.flashlight = d.flashlight, c.bigHeads = d.bigHeads, c.shotgun = d.shotgun, c.partyBoard = d.partyBoard, c.hardcore = d.hardcore, c.freeFire = d.freeFire, c.machineGun = d.machineGun, c.weaponSwitch = d.weaponSwitch, c.scoreDisplay = d.scoreDisplay, c.attractGore = d.attractGore, c.holdSkip = d.holdSkip;
        c.sonic = d.sonic, c.title = d.title, c.fbi = d.fbi, c.freePlay = d.freePlay, c.pcSplash = d.pcSplash;   // (feat29)
        break;
    case kResetAdvanced:
        c.gameSpeed = d.gameSpeed, c.musicNumbering = d.musicNumbering, c.teamFlash = d.teamFlash, c.trailer = d.trailer;
        c.boss4p = d.boss4p, c.enemy4p = d.enemy4p, c.cuts = d.cuts;
        break;
    case kResetRange:
        for (int s = 0; s < 4; ++s) c.sights[s] = d.sights[s];
        c.teamFlash = d.teamFlash;
        break;
    default:
        break;
    }
}

std::vector<Frontend::Extra> menuCategoryRows(int category, LaunchConfig& c, const MenuHooks& h)
{
    const auto save = [h]() { if (h.save) h.save(); };
    std::vector<X> r;
    switch (category) {
    case kCatVideo:
        r.push_back({"Models", [&c]() { return std::string(c.models == 1 ? "PC" : "Arcade"); },
                     [&c, save]() { c.models = c.models == 1 ? 0 : 1; save(); },
                     "The 3D models and world: the arcade's (Model 2) or the PC release's"});
        listed(r, {"Arcade", "PC"});
        previewed(r, h, "models");   // (fix28: the same frame with each)
        // aspect1610: 16:9 (shown as "16:9" since prerel; was "Widescreen"; fills any window) -> 16:10 (fills a 16:10 screen, thin bars on a wider one) -> 4:3
        r.push_back({"Aspect", [&c]() { return std::string(c.aspect == 1 ? "4:3" : c.aspect == 2 ? "16:10" : "16:9"); },
                     [&c, save]() { c.aspect = c.aspect == 0 ? 2 : c.aspect == 2 ? 1 : 0; save(); },
                     "16:9 fills the window; 16:10 for 16:10 screens (Steam Deck), thin side bars on wider ones; 4:3 is the original picture with side bars"});
        listed(r, {"16:9", "16:10", "4:3"});
        // remakemenu: the menu's look (game/frontend.h "the menu's two styles")
        r.push_back({"Menu Style", [&c]() { return std::string(c.menuStyle == 1 ? "Remake" : "Classic PC"); },
                     [&c, save, h]() {
                         c.menuStyle = c.menuStyle == 1 ? 0 : 1;
                         save();
                         if (h.menuStyleChanged) h.menuStyleChanged();
                     },
                     "Classic PC: the 1998 PC menu (4:3). Remake: a modern menu as wide as the window"});
        listed(r, {"Classic PC", "Remake"});
        // menuperf (user 2026-10-26: "optimise for low-end devices"): the Remake menu's background, the attract playing
        // live behind it (the most GPU work in the menus); Auto: off on integrated graphics or when the menu runs slow
        r.push_back({"Live Background", [&c]() { return std::string(c.liveBackground == 1 ? "On" : c.liveBackground == 2 ? "Off" : "Auto"); },
                     [&c, save]() { c.liveBackground = (c.liveBackground + 1) % 3; save(); },
                     "The Remake menu's background: the attract playing live (On), a still picture (Off), or Auto (now: " +
                         (h.liveBgAuto ? h.liveBgAuto() : std::string("on")) + ")"});
        r.back().clickBack = [&c, save]() { c.liveBackground = (c.liveBackground + 2) % 3; save(); };
        listed(r, {"Auto", "On", "Off"});
        r.back().off = [&c]() { return c.menuStyle == 1 ? std::string() : std::string("Live Background is the Remake menu's (Menu Style: Remake)"); };
        // rescrash (2026-10-29): Windowed / Borderless (a window filling the screen at the desktop's resolution) /
        // Fullscreen (exclusive, in the Resolution's display mode); applied at once, the game starts that way
        {
            static const char* const kModes[] = {"Windowed", "Borderless", "Fullscreen"};
            auto modeNow = [&c, h]() {
                const int m = h.displayModeNow ? h.displayModeNow() : c.fullscreen ? (c.exclusive ? 2 : 1) : 0;
                return std::clamp(m, 0, 2);
            };
            auto setMode = [h](int m) { if (h.displayModeSet) h.displayModeSet(m); };
            r.push_back({"Display Mode", [modeNow]() { return std::string(kModes[modeNow()]); },
                         [modeNow, setMode]() { setMode((modeNow() + 1) % 3); },
                         "Windowed, Borderless (the screen filled at the desktop's resolution) or Fullscreen (exclusive, in the Resolution below); F11 or Alt+Enter switches to a window and back"});
            r.back().clickBack = [modeNow, setMode]() { setMode((modeNow() + 2) % 3); };
            indexed(r, {"Windowed", "Borderless", "Fullscreen"}, modeNow, setMode);
        }
        // remakefix (user 2026-10-22): the monitor (applied at once; the Resolution below lists its modes)
        r.push_back({"Display", [h]() { return h.displayValue ? h.displayValue() : std::string("Primary"); },
                     [h]() { if (h.displayNext) h.displayNext(); },
                     "The monitor the game shows on (moved at once, kept for the next start)"});
        if (h.displayLabels)
            listed(r, h.displayLabels());
        if (h.displayPick)
            r.back().pick = h.displayPick;
        // (user 2026-10-21) the fullscreen resolution or the window's size, whichever the game shows now
        r.push_back({"Resolution", [h]() { return page(h.resolutionValue ? h.resolutionValue().c_str() : "Auto"); },
                     [h]() { if (h.resolutionPage) h.resolutionPage(); },
                     "Windowed: the window's size; Fullscreen: the display mode (Auto: the desktop's)"});
        r.back().off = [h]() {
            return h.displayModeNow && h.displayModeNow() == 1 ? std::string("Borderless uses the desktop's resolution (Display Mode: Windowed or Fullscreen to pick one)")
                                                               : std::string();
        };
        // (advopt: Game Speed is on the Advanced page, Gameplay's last row)
        r.push_back({"Frame Rate", [&c]() { return frameRateName(c.frameRate); },
                     [&c, save]() { c.frameRate = nextFrameRate(c.frameRate); save(); },
                     "The picture's rate: the display's refresh, a cap in fps, or Unlimited (the game's speed stays the same)"});
        {   // optui: the standard rates as a list (launcher.h frameRates)
            int nr = 0;
            const int* rates = frameRates(nr);
            indexed(r, namesOf(nr, [rates](int i) { return frameRateName(rates[i]); }),
                    [&c, rates, nr]() {
                        for (int i = 0; i < nr; ++i)
                            if (rates[i] == c.frameRate) return i;
                        return -1;
                    },
                    [&c, save, rates](int k) { c.frameRate = rates[k]; save(); });
        }
        // optui (user 2026-10-26 "the menu lags"): the menus' own cap (the game follows Frame Rate)
        r.push_back({"Menu Frame Rate", [&c, h]() {
                         return c.menuFps > 0 ? std::to_string(c.menuFps) + " fps"
                                              : "Auto" + (h.menuFpsAuto ? " (" + h.menuFpsAuto() + ")" : std::string());
                     },
                     [&c, save]() { c.menuFps = c.menuFps == 0 ? 30 : c.menuFps == 30 ? 60 : c.menuFps == 60 ? 120 : 0; save(); },
                     "The menus' picture rate: Auto (120 with a graphics card, 60 on integrated graphics, lower if the menu runs slow), never above the display"});
        r.back().clickBack = [&c, save]() { c.menuFps = c.menuFps == 0 ? 120 : c.menuFps == 120 ? 60 : c.menuFps == 60 ? 30 : 0; save(); };
        indexed(r, {"Auto", "30 fps", "60 fps", "120 fps"}, [&c]() { return c.menuFps == 30 ? 1 : c.menuFps == 60 ? 2 : c.menuFps == 120 ? 3 : 0; },
                [&c, save](int k) { c.menuFps = k == 1 ? 30 : k == 2 ? 60 : k == 3 ? 120 : 0; save(); });
        r.push_back({"Vsync", [&c]() { return std::string(c.frameRate < 0 ? "Off" : c.vsync ? "On" : "Off"); },
                     [&c, save]() { c.vsync = !c.vsync; save(); },
                     "Wait for the display's refresh (no tearing)"});
        r.back().off = [&c]() { return c.frameRate < 0 ? std::string("Vsync is off while Frame Rate is Unlimited") : std::string(); };
        r.push_back({"Smooth Motion", [&c]() { return onOff(c.smooth); },
                     [&c, save]() { c.smooth = !c.smooth; save(); },
                     "Blend the 3D picture between game frames (smoother; the game itself is unchanged)"});
        r.push_back({"Hit Sparks", [&c]() { return onOff(c.hitSparks); },
                     [&c, save]() { c.hitSparks = !c.hitSparks; save(); },
                     "The bright spark bursts where your bullets hit (draw only; the game itself is unchanged)"});
        r.push_back({"Gun Muzzle Flash", [&c]() { return onOff(c.muzzleFlash); },
                     [&c, save]() { c.muzzleFlash = !c.muzzleFlash; save(); },
                     "The flash on the guns of Rogan and G when they fire in the cut-scenes (draw only)"});
        r.push_back({"Blood Particles", [&c]() { return onOff(c.bloodParticles); },
                     [&c, save]() { c.bloodParticles = !c.bloodParticles; save(); },
                     "The blood spray and bursts where shots hit (draw only; the blood marks stay)"});
        // angle (render/gl_backend.h): OpenGL 3.3, or DirectX through ANGLE for older graphics; at the next start
        {
            static const char* const kRenderers[] = {"Auto", "OpenGL", "DirectX (ANGLE)"};
            auto setR = [&c, save](int k) { c.renderer = std::clamp(k, 0, 2); save(); };
            r.push_back({"Renderer", [&c]() { return std::string(kRenderers[std::clamp(c.renderer, 0, 2)]); },
                         [&c, setR]() { setR((c.renderer + 1) % 3); },
                         "Auto: OpenGL where the graphics driver offers OpenGL 3.3, else DirectX (ANGLE, for older graphics). "
                         "Applies at the next start (now: " + (gl::backendInUse().empty() ? std::string("OpenGL") : gl::backendInUse()) + ")"});
            r.back().clickBack = [&c, setR]() { setR((c.renderer + 2) % 3); };
            indexed(r, {"Auto", "OpenGL", "DirectX (ANGLE)"}, [&c]() { return std::clamp(c.renderer, 0, 2); }, setR);
            r.back().off = []() { return gl::angleAvailable() ? std::string() : std::string("DirectX (ANGLE) is the Windows package's (libGLESv2.dll beside the game)"); };
        }
        // depthopt (user 2026-10-27): no Draw Depth row (clipfix's): with the arcade models Arcade Sort is the arcade's
        // own picture and the Depth Buffer only adds glitches. launcher.ini drawdepth= and --draw-depth stay (advanced,
        // docs/modding.md); a mod's own models / pictures pick theirs (mod.ini depth =, assets/mods.h)
        // crt (render/crt.h, worker "crt"): the preset here, the rest on its page; menu.cfg + save only (the frames
        // pick cfg.crt up)
        r.push_back({"CRT Filter", [&c]() { return std::string(crtPresetName(c.crt.preset)); },
                     [&c, save]() { c.crt.preset = (c.crt.preset + 1) % kCrtPresets; save(); },
                     "The CRT look: Off, Arcade Monitor, Consumer TV, Sharp Scanlines or Custom"});
        listed(r, namesOf(kCrtPresets, [](int i) { return std::string(crtPresetName(i)); }));
        r.push_back({"CRT Settings", []() { return page("Open"); }, [h]() { if (h.crtPage) h.crtPage(); },
                     "The CRT filter's own page: the Custom preset's scanlines, mask, curvature, bloom, brightness"});
        // menufb (user 2026-10-26: "clicking crosshairs takes them to a new page / firing range"): every player's own
        // crosshair on the Firing Range page (menuRangeRows), tried out on its targets
        // Light Gun Edition: the crosshairs in play on / off (launcher.ini crosshairs=)
        r.push_back({"Show Crosshairs", [&c]() { return onOff(c.crosshairs); }, [&c, save]() { c.crosshairs = !c.crosshairs; save(); },
                     "Off: no crosshairs in the game (as the arcade with light guns); the Firing Range still shows them"});
        r.push_back({"Crosshairs", []() { return page("Firing Range"); }, [h]() { if (h.rangePage) h.rangePage(); },
                     "Each player's crosshair (style, colour, size, thickness), tried out on a firing range"});
        break;
    case kCatAudio:
        r.push_back({"Sound", [&c]() { return std::string(c.sound == 1 ? "Arcade" : c.sound == 2 ? "PC" : "Auto"); },
                     [&c, save]() { c.sound = (c.sound + 1) % 3; save(); },
                     "The sounds' source: Auto / Arcade = the arcade's sound board, PC = the 1998 PC port's sounds"});
        listed(r, {"Auto", "Arcade", "PC"});
        // (uxpolish: the volumes named as on Options, each beside its switch; the music's rows grey while it is off)
        r.push_back({"Sound Effects Volume", [&c]() { return std::to_string(c.sfxVolume) + "%"; },
                     [&c, save]() { c.sfxVolume = c.sfxVolume >= 100 ? 0 : c.sfxVolume / 5 * 5 + 5; save(); },
                     "The sound effects' volume in steps of 5%, after 100%: 0% (the Sound Effects slider on Options)"});
        volumeList(r, c.sfxVolume, save);
        r.push_back({"Music", [&c]() { return onOff(c.music); }, [&c, save]() { c.music = !c.music; save(); },
                     "The music on or off (the sound effects stay on)"});
        r.push_back({"Music Volume", [&c]() { return std::to_string(c.musicVolume) + "%"; },
                     [&c, save]() { c.musicVolume = c.musicVolume >= 100 ? 0 : c.musicVolume / 5 * 5 + 5; save(); },
                     "The music's volume in steps of 5%, after 100%: 0% (the Music slider on Options)"});
        volumeList(r, c.musicVolume, save);
        r.back().off = [&c]() { return c.music ? std::string() : std::string("The music is off (Music above)"); };
        r.push_back({"Menu Music", [&c]() { return std::string(menuSongOf(c.menuMusic).name); },
                     [&c, save, h]() {
                         c.menuMusic = nextMenuSong(c.menuMusic);
                         if (h.menuMusicChanged) h.menuMusicChanged();
                         save();
                     },
                     "The main menu's song (the PC played Chapter 4's)"});
        {   // optui: every song as a list (picked: played at once)
            int ns = 0;
            const MenuSong* songs = menuSongs(ns);
            indexed(r, namesOf(ns, [songs](int i) { return std::string(songs[i].name); }),
                    [&c, songs, ns]() {
                        for (int i = 0; i < ns; ++i)
                            if (songs[i].track == c.menuMusic) return i;
                        return -1;
                    },
                    [&c, save, h, songs](int k) {
                        c.menuMusic = songs[k].track;
                        if (h.menuMusicChanged) h.menuMusicChanged();
                        save();
                    });
        }
        r.back().off = [&c]() { return c.music ? std::string() : std::string("The music is off (Music above)"); };
        break;
    case kCatControls:
        // pads: each player's input (a gamepad: its Controller page; a light gun: F9 in the game calibrates)
        // (uxpolish: a light gun is experimental: the tag while one is chosen)
        // Light Gun Edition: each player's input: a light gun, a mouse or (players 2-4) the keyboard; no gamepads
        r.push_back({"Player 1 Input", [&c]() { return std::string(c.p1Input == 3 ? "Light Gun" : "Mouse"); },
                     [&c, save]() { c.p1Input = c.p1Input == 3 ? 0 : 3; save(); },
                     "Player 1 aims with a light gun or the mouse"});
        indexed(r, {"Light Gun", "Mouse"}, [&c]() { return c.p1Input == 3 ? 0 : 1; },
                [&c, save](int k) { c.p1Input = k == 0 ? 3 : 0; save(); });
        for (int p = 1; p < 4; ++p) {
            int& in = p == 1 ? c.p2Input : p == 2 ? c.p3Input : c.p4Input;
            r.push_back({"Player " + std::to_string(p + 1) + " Input" + (p >= 2 ? " (4P)" : ""), [&in]() {
                             return std::string(in == 3 ? "Light Gun" : in == 1 ? "Keyboard" : "Mouse");
                         },
                         [&in, save]() { in = in == 3 ? 0 : in == 0 ? 1 : 3; save(); },
                         "Player " + std::to_string(p + 1) + ": a light gun, a mouse of their own or the keyboard (Keys & Buttons)"});
            indexed(r, {"Light Gun", "Mouse", "Keyboard"}, [&in]() { return in == 3 ? 0 : in == 1 ? 2 : 1; },
                    [&in, save](int k) { in = k == 0 ? 3 : k == 2 ? 1 : 0; save(); });
        }
        // keybind (game/keybinds.h): every player's keyboard keys and gamepad buttons (uxpolish: right under the
        // inputs, before the per-player controller pages; its value says it opens a page, as every page row's does)
        r.push_back({"Keys & Buttons", []() { return page("Rebind"); }, [h]() { if (h.bindPage) h.bindPage(); },
                     "Change any player's keyboard keys (fire, reload, start, pause, aim)"});
        // (Light Gun Edition: no gamepad rows: button icons, touchpad / gyro cursor, aim sensitivity, controller pages)
        // padpass: every seat's light gun (players 3 / 4 were missing), the gun's kind and its screen aids, and the
        // calibration from the menu (before: F9 in the game only)
        // ipolish (Light Guns: Assign): which gun / mouse is whose seat (before: the order first used only)
        // Light Gun Edition: the set-up that also runs at a start with a blank LightgunHID.ini
        r.push_back({"Light Gun Setup", []() { return std::string("Start"); }, [h]() { if (h.gunSetup) h.gunSetup(); },
                     "Each player pulls their light gun's trigger in turn; saved in LightgunHID.ini and used at every start"});
        // (Light Gun Edition: no "Light Guns" Assign page: LightgunHID.ini / Light Gun Setup decide whose gun is whose)
        for (int p = 0; p < 4; ++p)
            lightGunRow(r, "Light Gun: Player " + std::to_string(p + 1) + (p >= 2 ? " (4P)" : ""), c.gunCal[p], save);
        r.push_back({"Light Gun Type", [&c]() {
                         static const char* const k[3] = {"IR / Mouse Mode", "Sinden", "CRT Gun"};
                         return std::string(k[std::clamp(c.gunKind, 0, 2)]);
                     },
                     [&c, save]() { c.gunKind = (c.gunKind + 1) % 3; save(); },
                     "IR / Mouse Mode: Gun4IR, AimTrak, a Wii Remote (no flash needed). Sinden: a white border. CRT Gun: the arcade's shot flash"});
        indexed(r, {"IR / Mouse Mode", "Sinden", "CRT Gun"}, [&c]() { return std::clamp(c.gunKind, 0, 2); },
                [&c, save](int k) { c.gunKind = k; save(); });
        r.back().clickBack = [&c, save]() { c.gunKind = (c.gunKind + 2) % 3; save(); };
        experimental(r);
        r.push_back({"Shot Flash", [&c]() {
                         static const char* const k[3] = {"Auto", "Off", "On"};
                         return std::string(k[std::clamp(c.gunFlash, 0, 2)]) + (c.gunFlash == 0 ? (gunFlashOn(c) ? " (On)" : " (Off)") : "");
                     },
                     [&c, save]() { c.gunFlash = (c.gunFlash + 1) % 3; save(); },
                     "WARNING: flashing may cause seizures. The whole screen white for 4 frames on every shot, as the arcade did for its CRT guns. Auto: on for a CRT gun"});
        indexed(r, {"Auto", "Off", "On"}, [&c]() { return std::clamp(c.gunFlash, 0, 2); }, [&c, save](int k) { c.gunFlash = k; save(); });
        r.back().clickBack = [&c, save]() { c.gunFlash = (c.gunFlash + 2) % 3; save(); };
        experimental(r);
        r.push_back({"Gun Border", [&c]() {
                         static const char* const k[5] = {"Auto", "Off", "Thin", "Medium", "Thick"};
                         return std::string(k[std::clamp(c.gunBorder, 0, 4)]) + (c.gunBorder == 0 ? (gunBorderPx(c, 1080) ? " (On)" : " (Off)") : "");
                     },
                     [&c, save]() { c.gunBorder = (c.gunBorder + 1) % 5; save(); },
                     "A white frame round the screen for a Sinden gun to find the screen by (menus and play). Auto: on for a Sinden"});
        indexed(r, {"Auto", "Off", "Thin", "Medium", "Thick"}, [&c]() { return std::clamp(c.gunBorder, 0, 4); },
                [&c, save](int k) { c.gunBorder = k; save(); });
        r.back().clickBack = [&c, save]() { c.gunBorder = (c.gunBorder + 4) % 5; save(); };
        experimental(r);
        r.push_back({"Calibrate Light Guns", [&c]() { return std::string(c.calibrateNext ? "At Next Start" : "Off"); },
                     [&c]() { c.calibrateNext = !c.calibrateNext; },
                     "The next game starts with the calibration: shoot the two targets with each gun (also F9 in the game)"});
        r.back().off = [&c]() {
            return lightGunInPlay(c) ? std::string() : std::string("No player's input is a light gun (Player 1-4 Input above)");
        };
        experimental(r);
        break;
    case kCatGameplay:
        // (uxpolish: the play's own settings first, then the co-op ones, then the attract; "Blood Color" as the PC's
        // Options row spells it; the experimental ones tagged)
        r.push_back({"Blood Color", [&c]() {
                         static const char* const k[6] = {"Red", "Green", "Blue", "Purple", "Yellow", "Custom"};
                         return std::string(k[std::clamp(c.blood, 0, 5)]);
                     },
                     [&c, save]() { c.blood = (c.blood + 1) % 6; save(); },
                     "The blood's colour; Custom (Experimental): your own colour, the bars below"});
        listed(r, {"Red", "Green", "Blue", "Purple", "Yellow", "Custom"});
        previewed(r, h, "blood");
        experimental(r, [&c]() { return c.blood == 5; });
        // menufb (user 2026-10-26: "for custom blood color, players should pick a color via rgb in the main menu and
        // they should know that it does not affect arcade textures currently"): red / green / blue bars and the colour
        {
            auto channel = [&c, save](std::vector<X>& rr, const char* name, int shift) {
                rr.push_back({name, [&c, shift]() { return std::to_string((c.bloodRgb >> shift) & 255); },
                              [&c, save, shift]() {
                                  const int v = (((c.bloodRgb >> shift) & 255) / 16 * 16 + 16) & 255;
                                  c.bloodRgb = (c.bloodRgb & ~(255 << shift)) | (v << shift);
                                  save();
                              },
                              "The custom blood colour's amount of this colour (0-255): drag the bar"});
                rr.back().clickBack = [&c, save, shift]() {
                    const int v = std::max(0, (((c.bloodRgb >> shift) & 255) + 15) / 16 * 16 - 16);
                    c.bloodRgb = (c.bloodRgb & ~(255 << shift)) | (v << shift);
                    save();
                };
                rr.back().barGet = [&c, shift]() { return (c.bloodRgb >> shift) & 255; };
                rr.back().barSet = [&c, save, shift](int v) {
                    c.bloodRgb = (c.bloodRgb & ~(255 << shift)) | (std::clamp(v, 0, 255) << shift);
                    save();
                };
                rr.back().barMax = 255, rr.back().barStep = 1;
                rr.back().off = [&c]() { return c.blood == 5 ? std::string() : std::string("Choose Blood Color: Custom first"); };
            };
            channel(r, "Custom Red", 16);
            channel(r, "Custom Green", 8);
            channel(r, "Custom Blue", 0);
            r.push_back({"Custom Colour", [&c]() {
                             char b[16];
                             std::snprintf(b, sizeof b, "#%06X", unsigned(c.bloodRgb & 0xFFFFFF));
                             return std::string(b);
                         },
                         [&c, save]() { c.blood = 5; save(); },
                         "Your blood colour (Experimental), from the bars above; the arcade models' own textures keep theirs for now"});
            r.back().swatch = [&c]() { return uint32_t(c.bloodRgb & 0xFFFFFF); };
            r.back().off = [&c]() { return c.blood == 5 ? std::string() : std::string("Choose Blood Color: Custom first"); };
            // (the note the player should see without hovering: a grey line of its own)
            r.push_back({"Arcade textures keep their colour for now", nullptr, nullptr,
                         "The custom blood colour does not change the arcade models' own textures yet"});
        }
        r.push_back({"Auto Reload", [&c]() { return onOff(c.autoReload); }, [&c, save]() { c.autoReload = !c.autoReload; save(); },
                     "An empty gun reloads by itself (not in the originals): every player, 3P / 4P too; online the host's"});
        r.push_back({"Edge Reload", [&c]() { return onOff(c.edgeReload); }, [&c, save]() { c.edgeReload = !c.edgeReload; save(); },
                     "As The House of the Dead 2: moving the mouse crosshair onto the screen's edge reloads, no shot needed"});
        // Light Gun Edition: the fun modes (local games; online games are always played as normal)
        r.push_back({"Flashlight Mode", [&c]() { return onOff(c.flashlight); }, [&c, save]() { c.flashlight = !c.flashlight; save(); },
                     "Fun mode: the mansion goes dark and each player's gun lights the screen where it aims"});
        r.push_back({"Big Heads", [&c]() { return onOff(c.bigHeads); }, [&c, save]() { c.bigHeads = !c.bigHeads; save(); },
                     "Fun mode: every creature's head drawn nearly twice as big"});
        r.push_back({"Shotgun Mode", [&c]() { return onOff(c.shotgun); }, [&c, save]() { c.shotgun = !c.shotgun; save(); },
                     "Fun mode: each shot hits a much wider area, but the gun holds only 3 shells"});
        r.push_back({"Free Fire", [&c]() { return onOff(c.freeFire); }, [&c, save]() { c.freeFire = !c.freeFire; save(); },
                     "Fun mode: unlimited ammo, no reloading (for young players and parties)"});
        r.push_back({"Machine Gun", [&c]() { return onOff(c.machineGun); }, [&c, save]() { c.machineGun = !c.machineGun; save(); },
                     "Fun mode: hold the trigger for automatic fire (recoil on every shot), unlimited ammo"});
        r.push_back({"Arsenal Mode", [&c]() { return onOff(c.weaponSwitch); }, [&c, save]() { c.weaponSwitch = !c.weaponSwitch; save(); },
                     "Fun mode: the gun's middle button switches weapons: Pistol, Shotgun, Machine Gun, Rocket Launcher (each player their own)"});
        r.push_back({"Hardcore", [&c]() { return onOff(c.hardcore); }, [&c, save]() { c.hardcore = !c.hardcore; save(); },
                     "Fun mode: one life each, no continues, no joining back in; its own high scores"});
        r.push_back({"Party Scoreboard", [&c]() { return onOff(c.partyBoard); }, [&c, save]() { c.partyBoard = !c.partyBoard; save(); },
                     "Co-op: each player's score, shots, hits and accuracy at the end of a chapter, and the MVP"});
        // skipfix (user 2026-10-27): the hold-to-skip of the cut-scenes the arcade cannot skip (main.cpp)
        // feat29 (user 2026-10-29: "Add Free Play in the settings"): the Cheat screen's Free-Play (THOTD.EXE 0xD5B35C,
        // launcher.ini freeplay=): PC Mode's continues never run out. Arcade games are free play already (no coins)
        r.push_back({"Free Play", [&c]() { return onOff(c.freePlay); }, [&c, save]() { c.freePlay = !c.freePlay; save(); },
                     "On: PC Mode's continues never run out (as the 1998 PC's Cheat screen). Arcade games are always free play"});
        r.push_back({"Hold to Skip Cut-scenes", [&c]() { return onOff(c.holdSkip); }, [&c, save]() { c.holdSkip = !c.holdSkip; save(); },
                     "Hold the trigger 2 s to skip a cut-scene the arcade cannot skip (black bars up): a red ring fills. Offline"});
        r.push_back({"Attract Gore", [&c]() { return onOff(c.attractGore); },
                     [&c, save]() { c.attractGore = !c.attractGore; hotd::port::gAttractGore = c.attractGore; save(); },
                     "The attract demo's shots blow off heads and limbs, as in the 1998 PC version. Off: as the arcade (Revision A)"});
        // scoredisp (tip: RUNESA): the arcade's hidden score line, normally the title code (left gun x3, right gun x2)
        r.push_back({"Score Display", [&c]() { return onOff(c.scoreDisplay); }, [&c, save]() { c.scoreDisplay = !c.scoreDisplay; save(); },
                     "The arcade's hidden SCORE line at the top (or at the title: left gun 3 times, right gun twice). Offline"});
        // Sonic Mode (game/sonic.h): play faster, cut-scenes at the normal speed; its own high scores
        r.push_back({"Sonic Mode", [&c]() { return std::string(sonicName(c.sonic)); },
                     [&c, save]() { c.sonic = (c.sonic + 1) % kSonicPresets; save(); },
                     "Faster play, cut-scenes at the normal speed, its own high scores (also on the main menu)"});
        listed(r, namesOf(kSonicPresets, [](int i) { return std::string(sonicName(i)); }));
        r.push_back({"Title / Attract", [&c]() { return onOff(c.title); }, [&c, save]() { c.title = !c.title; save(); },
                     "The arcade's title screen and attract before Chapter 1 and after 30 s idle in the menu"});
        r.push_back({"FBI Screen", [&c]() { return onOff(c.fbi); }, [&c, save]() { c.fbi = !c.fbi; save(); },
                     "The attract's WINNERS DON'T USE DRUGS screen"});
        r.back().off = [&c]() { return c.title ? std::string() : std::string("Part of the attract: Title / Attract is off"); };
        // feat29 (user 2026-10-29: "the 1998 PC port's SEGA screen and the zombie artwork when you start the game. Off
        // by default"; pmpick, user 2026-10-30: On by default now): the PC's start-up window (THOTD.EXE 0x419FDC: SEGA
        // logo, then the title art) before the menu
        r.push_back({"PC Start-up Screens", [&c]() { return onOff(c.pcSplash); }, [&c, save]() { c.pcSplash = !c.pcSplash; save(); },
                     "The 1998 PC's SEGA screen and zombie title art when the game starts (a key or click skips them)"});
        // advopt (2026-10-21): the rarely changed rows on their own page (the tab strip has no room for a 7th tab)
        r.push_back({"Advanced", []() { return page("Open"); }, [h]() { if (h.advancedPage) h.advancedPage(); },
                     "Rarely changed settings: Game Speed, Music Numbering, Teammate Flashes, 4P Boss Hit Points, Guards"});
        break;
    case kCatMods:
        if (h.modsRows)
            r = h.modsRows();
        else
            r.push_back({"No mods found", []() { return std::string("data\\mods"); }, nullptr,
                         "Mods go in the data\\mods folder; how to make one: docs/modding.md"});
        break;
    case kCatSystem:
        // setup2 (v1.1.0): the setup's Game files page (choose / import the arcade ROMs and the PC game again)
        r.push_back({"Repair / Change Files", []() { return page("Setup"); }, [h]() { if (h.filesCheck) h.filesCheck(); },
                     "The setup's Game files page: check or change your arcade ROMs and PC game (an install imports them again)"});
        r.push_back({"Check for Updates", [&c]() { return onOff(c.updateCheck); },
                     [&c, save]() { c.updateCheck = !c.updateCheck; save(); },
                     "At start-up, ask GitHub whether a newer version is out; it is installed only when you press Update. Next start"});
        r.push_back({"What's New", []() { return page("Open"); }, [h]() { if (h.whatsNew) h.whatsNew(); },
                     "The changes in this version (CHANGELOG.txt)"});
        r.push_back({"Desktop Shortcut", [h]() { return h.shortcutValue ? h.shortcutValue() : std::string("Create"); },
                     [h]() { if (h.shortcut) h.shortcut(); }, "Put a shortcut to the game on the desktop (and the Start menu)"});
        // Add to Steam (util/steam_shortcut.h): its own page, the pictures to pick, then Add (Steam closed)
        r.push_back({"Add to Steam", []() { return page("Pictures"); }, [h]() { if (h.steamPage) h.steamPage(); },
                     "Add the game to Steam with its pictures (pick them on the next page; Steam closed)"});
        // lang (translations, docs/translating.md): English and every data\lang\<code>.txt, each by its own name
        // (the last row: the input test's walks count the rows above). menufb (user 2026-10-26: "Language needs ui
        // improvement"): its own page (menuLanguageRows): each language in its own script, a line of it to preview
        r.push_back({"Language", []() { return page(lang::displayName(lang::code()).c_str()); },
                     [h]() { if (h.languagePage) h.languagePage(); },
                     "The menus' language: each one in its own script with a line of it (files in data\\lang). F5 reloads it"});
        break;
    default:
        break;
    }
    if (category != kCatMods && category != kCatSystem)
        resetRow(r, h, category);   // (menufb: Reset to Defaults, asked first)
    return r;
}

// mods (docs/modding.md, 2026-10-26): the Mods page and each mod's Settings page
std::vector<std::pair<const ModPack*, bool>> modOrder(const LaunchConfig& c, const std::vector<ModPack>& mods)
{
    auto same = [](const std::string& a, const std::string& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y));
               });
    };
    std::vector<std::pair<const ModPack*, bool>> out;
    for (const std::string& id : c.modsOn)
        for (const ModPack& p : mods)
            if (same(p.id, id) && std::none_of(out.begin(), out.end(), [&](const auto& q) { return q.first == &p; }))
                out.push_back({&p, true});
    for (const ModPack& p : mods) {
        if (std::any_of(out.begin(), out.end(), [&](const auto& q) { return q.first == &p; }))
            continue;
        bool off = !p.defaultOn;   // a mod not in mods= nor modsoff= yet: its mod.ini default
        for (const std::string& id : c.modsOff) off |= same(p.id, id);
        out.push_back({&p, !off});
    }
    return out;
}

std::vector<X> menuModRows(LaunchConfig& c, const std::vector<ModPack>& mods, std::function<void()> changed,
                           std::function<void(const std::string& id)> openSettings)
{
    auto setOrder = [&c, changed](const std::vector<std::pair<const ModPack*, bool>>& l) {
        c.modsOn.clear();
        c.modsOff.clear();
        for (const auto& [p, on] : l) (on ? c.modsOn : c.modsOff).push_back(p->id);
        if (changed) changed();
    };
    auto title = [](const ModPack& p) { return p.name.size() > 22 ? util::utf8Head(p.name, 21) + "." : p.name; };
    // the help line: the mod's description (a key of its own: the built-in mods' can be translated)
    auto about = [](const ModPack& p) {
        return !p.description.empty() ? p.description : p.type == "textures" ? std::string("A texture pack") : p.name;
    };
    std::vector<X> r;
    // leftovers (user 2026-10-26: "clicking ragdoll mod also turns on example: green blood"): each row finds its mod by
    // id, not by its place in the list: switching a mod on moves it up the list (the on ones first), so a row's old
    // place pointed at another mod (its value and its click)
    auto at = [](const std::vector<std::pair<const ModPack*, bool>>& l, const std::string& id) {
        for (size_t k = 0; k < l.size(); ++k)
            if (l[k].first->id == id) return int(k);
        return -1;
    };
    const auto order = modOrder(c, mods);
    int packs = 0;
    for (const auto& [p, on] : order) packs += p->type == "textures";
    for (size_t i = 0; i < order.size(); ++i) {
        const ModPack& mp = *order[i].first;
        const std::string id = mp.id;
        r.push_back({title(mp), [&c, &mods, id, at]() {
                         const auto l = modOrder(c, mods);
                         const int k = at(l, id);
                         return onOff(k >= 0 && l[size_t(k)].second);
                     },
                     [&c, &mods, id, at, setOrder]() {
                         auto l = modOrder(c, mods);
                         const int k = at(l, id);
                         if (k < 0) return;
                         l[size_t(k)].second = !l[size_t(k)].second;
                         setOrder(l);
                     },
                     about(mp)});
        r.back().raw = true;   // (a mod's own name: not translated)
        // leftovers (user: "the mod settings need a better experience"): who made it and its version under its name
        // (grey: no action; its help line: the mod's description)
        if (!mp.author.empty() || !mp.version.empty()) {
            const std::string by = std::string(mp.type == "textures" ? "  Texture pack" : "  Built-in mod") +
                                   (mp.author.empty() ? std::string() : ", by " + (mp.author.size() > 18 ? util::utf8Head(mp.author, 17) + "." : mp.author));
            r.push_back({by, [v = mp.version]() { return v.empty() ? std::string() : "v" + v; }, nullptr, about(mp)});
            r.back().raw = true;
        }
        if (!mp.settings.empty())
            r.push_back({"  Settings", []() { return page("Open"); }, [openSettings, id]() { if (openSettings) openSettings(id); },
                         "This mod's own settings (saved on this PC)"});
        if (i > 0 && mp.type == "textures" && packs > 1)   // (the load order matters for the texture packs only)
            r.push_back({"  Move Up", [&c, &mods, id, at]() {
                             const auto l = modOrder(c, mods);
                             const int k = at(l, id);
                             return std::string(k > 0 && l[size_t(k)].second ? "Move" : "-");
                         },
                         [&c, &mods, id, at, setOrder]() {
                             auto l = modOrder(c, mods);
                             const int k = at(l, id);
                             if (k <= 0 || !l[size_t(k)].second) return;
                             std::swap(l[size_t(k)], l[size_t(k) - 1]);
                             setOrder(l);
                         },
                         "Move this mod up the load order: the first texture pack's pictures win"});
    }
    if (order.size() <= 1)
        r.push_back({"More Mods", []() { return std::string("data\\mods"); }, nullptr,
                     "Mods go in the data\\mods folder; how to make one: docs/modding.md"});
    return r;
}

int modsPageTest()
{
    std::vector<ModPack> mods(3);
    mods[0].id = "example", mods[0].name = "Example: Green Blood", mods[0].type = "textures", mods[0].defaultOn = false;
    mods[1].id = "hd", mods[1].name = "HD Pack", mods[1].type = "textures", mods[1].defaultOn = false;
    mods[2].id = "ragdoll", mods[2].name = "Ragdoll", mods[2].type = "ragdoll", mods[2].defaultOn = false;
    LaunchConfig c;
    int checks = 0, failed = 0;
    auto isOn = [&](const std::string& id) {
        for (const auto& [p, on] : modOrder(c, mods))
            if (p->id == id) return on;
        return false;
    };
    auto expect = [&](const char* what, bool ex, bool rg, bool hd) {
        ++checks;
        const bool ok = isOn("example") == ex && isOn("ragdoll") == rg && isOn("hd") == hd;
        if (!ok) ++failed;
        std::printf("mods test: %-44s example %s ragdoll %s hd %s%s\n", what, isOn("example") ? "on " : "off",
                    isOn("ragdoll") ? "on " : "off", isOn("hd") ? "on " : "off", ok ? "" : "   FAILED");
    };
    // the rows built once (as the page shows them until it is built again): each click finds its own mod
    const std::vector<X> rows = menuModRows(c, mods, nullptr, nullptr);
    auto click = [&](const std::string& name) {
        for (const X& x : rows)
            if (x.name == name && x.click) return x.click(), true;
        return false;
    };
    auto shown = [&](const std::string& name) {
        for (const X& x : rows)
            if (x.name == name && x.value) return x.value();
        return std::string();
    };
    expect("at the start (defaults: all off)", false, false, false);
    click("Ragdoll");
    expect("Ragdoll clicked", false, true, false);
    ++checks;
    if (shown("Ragdoll") != onOff(true) || shown("Example: Green Blood") != onOff(false))
        ++failed, std::printf("mods test: the rows show another mod's value   FAILED\n");
    click("Ragdoll");
    expect("Ragdoll clicked again", false, false, false);
    click("HD Pack");
    expect("HD Pack clicked", false, false, true);
    click("Ragdoll");
    expect("Ragdoll clicked (HD on)", false, true, true);
    click("Example: Green Blood");
    expect("Example clicked", true, true, true);
    click("HD Pack");
    expect("HD Pack clicked (off)", true, true, false);
    // depthopt: mod.ini depth = switches the mod's items (assets/mods.h modDepthTest: 5 checks)
    checks += 5, failed += modDepthTest();
    std::printf("mods test: %d checks, %d failed\n", checks, failed);
    return failed;
}

std::vector<X> menuModSettingRows(LaunchConfig& c, const ModPack& mod, std::function<void()> changed)
{
    std::vector<X> r;
    const std::string id = mod.id;
    // leftovers: the mod itself on / off here too (as on the Mods page: mods= / modsoff=; its own default until set),
    // its help line the mod's description
    {
        auto has = [](const std::vector<std::string>& l, const std::string& k) {
            return std::any_of(l.begin(), l.end(), [&](const std::string& s) {
                return s.size() == k.size() && std::equal(s.begin(), s.end(), k.begin(), [](char a, char b) {
                           return std::tolower(uint8_t(a)) == std::tolower(uint8_t(b));
                       });
            });
        };
        auto isOn = [&c, id, has, def = mod.defaultOn]() { return has(c.modsOn, id) || (!has(c.modsOff, id) && def); };
        r.push_back({mod.name, [isOn]() { return onOff(isOn()); },
                     [&c, id, isOn, changed]() {
                         const bool on = isOn();
                         auto drop = [&](std::vector<std::string>& l) {
                             l.erase(std::remove_if(l.begin(), l.end(), [&](const std::string& s) {
                                         return s.size() == id.size() && std::equal(s.begin(), s.end(), id.begin(), [](char a, char b) {
                                                    return std::tolower(uint8_t(a)) == std::tolower(uint8_t(b));
                                                });
                                     }), l.end());
                         };
                         drop(c.modsOn), drop(c.modsOff);
                         (on ? c.modsOff : c.modsOn).push_back(id);
                         if (changed) changed();
                     },
                     mod.description.empty() ? mod.name : mod.description});
        r.back().raw = true;
    }
    for (const ModSetting& st : mod.settings) {
        const std::string key = id + "." + st.key;
        auto value = [&c, key, st]() {
            auto it = c.modSettings.find(key);
            const bool ok = it != c.modSettings.end() && std::find(st.choices.begin(), st.choices.end(), it->second) != st.choices.end();
            return ok ? it->second : st.def;
        };
        auto set = [&c, key, changed](const std::string& v) {
            c.modSettings[key] = v;
            if (changed) changed();
        };
        auto stepBy = [value, set, st](int dir) {
            const auto it = std::find(st.choices.begin(), st.choices.end(), value());
            const int n = int(st.choices.size()), k = it == st.choices.end() ? 0 : int(it - st.choices.begin());
            set(st.choices[size_t(((k + dir) % n + n) % n)]);
        };
        r.push_back({st.label, value, [stepBy]() { stepBy(1); }, st.help.empty() ? st.label : st.help});
        listed(r, st.choices);
        r.back().pick = set;
        r.back().clickBack = [stepBy]() { stepBy(-1); };
    }
    r.push_back({"Defaults", []() { return std::string("Reset"); },
                 [&c, id, changed]() {
                     for (auto it = c.modSettings.begin(); it != c.modSettings.end();)
                         it = it->first.rfind(id + ".", 0) == 0 ? c.modSettings.erase(it) : std::next(it);
                     if (changed) changed();
                 },
                 "This mod's settings back to the mod's own defaults"});
    return r;
}

// advopt (2026-10-21): the Advanced page (Gameplay's last row): the rows moved off Video, Audio and Gameplay, with
// the same values, cycles and launcher.ini keys
// menufb: the Firing Range page's rows for player `seat` (the Player row picks it): that player's crosshair, colour,
// size and thickness (the lists show each choice drawn in that player's colours), then Copy to Every Player
std::vector<Frontend::Extra> menuRangeRows(LaunchConfig& c, const MenuHooks& h, int& seat)
{
    const auto save = [h]() { if (h.save) h.save(); };
    std::vector<X> r;
    int* sp = &seat;
    auto S = [&c, sp]() -> SeatSight& { return c.sights[std::clamp(*sp, 0, 3)]; };
    r.push_back({"Player", [sp]() { return "Player " + std::to_string(*sp + 1); },
                 [sp]() { *sp = (*sp + 1) % 4; }, "Whose crosshair to change: every player has their own"});
    r.back().clickBack = [sp]() { *sp = (*sp + 3) % 4; };
    indexed(r, {"Player 1", "Player 2", "Player 3", "Player 4"}, [sp]() { return *sp; }, [sp](int k) { *sp = k; });
    const std::vector<CustomSight> sets = h.customSights ? h.customSights() : std::vector<CustomSight>{};
    // the sight for these settings, as the game draws it (game/sight_art.cpp; Auto follows the models)
    const auto art = [&c, h, sets](const SeatSight& s, int seatArt, int wh, SightArt& out) {
        const assets::PcSprites* pc = h.sightSprites && sightStyleKept(s.style) <= 2 ? h.sightSprites() : nullptr;
        seatSightArt(s, c.models == 1, pc, sets, seatArt, wh, out);
        return out.w > 0;
    };
    r.push_back({"Crosshair", [S]() {
                     const SeatSight& s = S();
                     return s.style == kSightCustom ? "Custom: " + s.custom : std::string(sightMenuName(s.style));
                 },
                 [S, save, sets]() {
                     nextSight(S().style, S().custom, sets);
                     save();
                 },
                 // feat29 (user 2026-10-29: "make sure the player knows they can add custom crosshairs"): the help
                 // names the folder the PNGs go in (no row of its own: the page's range needs its room). relmenu (user
                 // 2026-10-30: "people have different folders"): the folder's name only, no full path (the hook still
                 // makes the folder)
                 "A built-in style, or add your own crosshair PNGs to the Crosshairs folder"});
    if (h.crosshairDir)
        h.crosshairDir();   // (the settings folder's crosshairs made, so the player finds it)
    {
        std::vector<std::string> names;
        for (int k = 0; k < kSightListN; ++k) names.push_back(sightMenuName(kSightList[k]));
        for (const CustomSight& s : sets) names.push_back("Custom: " + s.name);
        indexed(r, names,
                [S, sets]() {
                    if (S().style == kSightCustom) {
                        const CustomSight* s = findSight(sets, S().custom);
                        return s ? kSightListN + int(s - sets.data()) : -1;
                    }
                    for (int k = 0; k < kSightListN; ++k)
                        if (kSightList[k] == sightStyleKept(S().style)) return k;
                    return -1;
                },
                [S, save, sets](int k) {
                    if (k < kSightListN)
                        S().style = kSightList[k], S().custom.clear();
                    else if (k - kSightListN < int(sets.size()))
                        S().style = kSightCustom, S().custom = sets[size_t(k - kSightListN)].name;
                    save();
                });
        r.back().art = [S, art, sets](int k, int seatArt, int wh, SightArt& out) {
            SeatSight s = S();
            if (k >= 0 && k < kSightListN)
                s.style = kSightList[k], s.custom.clear();
            else if (k >= kSightListN && k - kSightListN < int(sets.size()))
                s.style = kSightCustom, s.custom = sets[size_t(k - kSightListN)].name;
            return art(s, seatArt, wh, out);
        };
    }
    r.push_back({"Colour", [S]() { return std::string(kSightColourNames[std::clamp(S().colour, 0, kSightColours - 1)]); },
                 [S, save]() { S().colour = (S().colour + 1) % kSightColours; save(); },
                 "The crosshair's colour: the player's own (P1 red, P2 blue, P3 green, P4 yellow) or another"});
    r.back().clickBack = [S, save]() { S().colour = (S().colour + kSightColours - 1) % kSightColours; save(); };
    indexed(r, namesOf(kSightColours, [](int i) { return std::string(kSightColourNames[i]); }), [S]() { return S().colour; },
            [S, save](int k) { S().colour = k; save(); });
    r.back().art = [S, art](int k, int seatArt, int wh, SightArt& out) {
        SeatSight s = S();
        if (k >= 0) s.colour = k;
        return art(s, seatArt, wh, out);
    };
    r.push_back({"Size", [S]() { return capital(kSizeNames[std::clamp(S().size, 0, kSizeSteps - 1)]); },
                 [S, save]() { S().size = (S().size + 1) % kSizeSteps; save(); }, "The aim marker's size (the drawn styles)"});
    indexed(r, namesOf(kSizeSteps, [](int i) { return capital(kSizeNames[i]); }), [S]() { return std::clamp(S().size, 0, kSizeSteps - 1); },
            [S, save](int k) { S().size = k; save(); });
    r.back().art = [S, art](int k, int seatArt, int wh, SightArt& out) {
        SeatSight s = S();
        if (k >= 0) s.size = k;
        return art(s, seatArt, wh, out);
    };
    r.push_back({"Thickness", [S]() { return capital(kThickNames[std::clamp(S().thick, 0, kThickSteps - 1)]); },
                 [S, save]() { S().thick = (S().thick + 1) % kThickSteps; save(); }, "The aim marker's line thickness (the drawn styles)"});
    indexed(r, namesOf(kThickSteps, [](int i) { return capital(kThickNames[i]); }), [S]() { return std::clamp(S().thick, 0, kThickSteps - 1); },
            [S, save](int k) { S().thick = k; save(); });
    r.back().art = [S, art](int k, int seatArt, int wh, SightArt& out) {
        SeatSight s = S();
        if (k >= 0) s.thick = k;
        return art(s, seatArt, wh, out);
    };
    teamFlashRow(r, c, save);   // (the range: the mouse's player is "you", the others show it)
    r.push_back({"Copy to Every Player", nullptr, [&c, sp, save]() {
                     for (int k = 0; k < 4; ++k) c.sights[k] = c.sights[std::clamp(*sp, 0, 3)];
                     save();
                 },
                 "Give every player this player's crosshair (their own colours stay unless a colour is chosen)"});
    resetRow(r, h, kResetRange);
    return r;
}

// menufb: the Language page: English and every data\lang\<code>.txt by its own name (in its own script), the one in
// use marked Current, a sample file marked Sample; the help line shows a line of each in it (Info::preview). A pick
// changes the language at once (the page again in it)
std::vector<Frontend::Extra> menuLanguageRows(LaunchConfig& c, const MenuHooks& h)
{
    const auto save = [h]() { if (h.save) h.save(); };
    std::vector<X> r;
    for (const lang::Info& i : lang::available()) {
        const std::string code = i.code;
        const bool sample = i.sample;
        // relmenu (user 2026-10-30: "players can't tell what each language is"): its own name and the English one
        r.push_back({menuLanguageName(i.name, i.english), [code, sample]() { return code == lang::code() ? lang::tr("Current") : sample ? lang::tr("Sample") : std::string(); },
                     [&c, h, save, code]() {
                         c.language = code;
                         lang::set(code);
                         if (h.languageChanged) h.languageChanged();
                         save();
                         if (h.languagePage) h.languagePage();
                     },
                     i.preview.empty() ? i.name : i.preview});
        r.back().raw = true;   // (its own name: not translated; sweep: its Current / Sample are, above)
    }
    return r;
}

// "한국어 (Korean)"; a right-to-left name in a left-to-right menu starts with a left-to-right mark (U+200E, not drawn)
// so it still reads "العربية (Arabic)" from the left
std::string menuLanguageName(const std::string& name, const std::string& english)
{
    if (english.empty() || english == name)
        return name;
    return (render::hasRtl(name) && !lang::rtl() ? "\xE2\x80\x8E" : "") + name + " (" + english + ")";
}

bool isMenuLanguageName(const std::string& text)
{
    const std::string t = text.rfind("\xE2\x80\x8E", 0) == 0 ? text.substr(3) : text;
    for (const lang::Info& i : lang::available())
        if (!i.english.empty() && t == i.name + " (" + i.english + ")")
            return true;
    return false;
}

std::vector<Frontend::Extra> menuAdvancedRows(LaunchConfig& c, const MenuHooks& h)
{
    const auto save = [h]() { if (h.save) h.save(); };
    std::vector<X> r;
    // rate: the logic's tick rate (arcade 57.52 Hz, what the Model 2 runs; 60 Hz, the PC port's)
    r.push_back({"Game Speed", [&c]() { return std::string(c.gameSpeed < 0 ? "Auto" : c.gameSpeed == 1 ? "60 Hz (PC)" : "Arcade (57.52 Hz)"); },
                 [&c, save]() { c.gameSpeed = c.gameSpeed < 0 ? 0 : c.gameSpeed == 0 ? 1 : -1; save(); },
                 "The game logic's rate. Auto: the arcade's 57.52 Hz, PC Mode the PC port's 60 Hz; or always one of them"});
    listed(r, {"Auto", "Arcade (57.52 Hz)", "60 Hz (PC)"});
    r.push_back({"Music Numbering", [&c]() {
                     static const char* const k[3] = {"Auto", "PC CD", "Saturn"};
                     return std::string(k[std::clamp(c.musicNumbering, 0, 2)]);
                 },
                 [&c, save]() { c.musicNumbering = (c.musicNumbering + 1) % 3; save(); },
                 "How your music files are numbered: Auto, the PC CD's tracks or the Saturn disc's"});
    listed(r, {"Auto", "PC CD", "Saturn"});
    teamFlashRow(r, c, save);
    // trailer (user 2026-10-21): for recording gameplay; F7 in play turns it on / off for that run only
    r.push_back({"Trailer Mode", [&c]() { return onOff(c.trailer); }, [&c, save]() { c.trailer = !c.trailer; save(); },
                 "On: no HUD, subtitles, crosshairs or cursor in play, only the picture (for recording). Key in play: F7"});
    r.push_back({"4P Boss Hit Points", [&c]() { return "+" + std::to_string(c.boss4p) + "% a player"; },
                 [&c, save]() {
                     static const int k[] = {0, 10, 15, 20, 25, 30, 40, 50, 75, 100};
                     int i = 0;
                     while (i < 10 && k[i] <= c.boss4p) ++i;
                     c.boss4p = k[i % 10];
                     save();
                 },
                 "The bosses' extra hit points for each player past two (4 players mode)"});
    {   // (optui: the steps as a list)
        static const int k[] = {0, 10, 15, 20, 25, 30, 40, 50, 75, 100};
        indexed(r, namesOf(10, [](int i) { return "+" + std::to_string(k[i]) + "% a player"; }),
                [&c]() { for (int i = 0; i < 10; ++i) if (k[i] == c.boss4p) return i; return -1; },
                [&c, save](int i) { c.boss4p = k[i]; save(); });
    }
    // cpuai (2026-10-26, user: "3P / 4P too easy"): the creatures take more shots with 3 / 4 players
    r.push_back({"4P Enemy Toughness", [&c]() { return "+" + std::to_string(c.enemy4p) + "% a player"; },
                 [&c, save]() {
                     static const int k[] = {0, 10, 15, 20, 25, 30, 40, 50, 75, 100};
                     int i = 0;
                     while (i < 10 && k[i] <= c.enemy4p) ++i;
                     c.enemy4p = k[i % 10];
                     save();
                 },
                 "The creatures take this many more shots for each player past two (4 players mode; 0: as with two)"});
    {
        static const int k[] = {0, 10, 15, 20, 25, 30, 40, 50, 75, 100};
        indexed(r, namesOf(10, [](int i) { return "+" + std::to_string(k[i]) + "% a player"; }),
                [&c]() { for (int i = 0; i < 10; ++i) if (k[i] == c.enemy4p) return i; return -1; },
                [&c, save](int i) { c.enemy4p = k[i]; save(); });
    }
    experimental(r);
    r.push_back({"Guards (Teaser Cuts)", [&c]() { return onOff(c.cuts); }, [&c, save]() { c.cuts = !c.cuts; save(); },
                 "On (best): the game never waits on a part not ported yet; Off: the original's waits (it may stop there)"});
    resetRow(r, h, kResetAdvanced);
    return r;
}

std::vector<Frontend::Extra> menuCoopRows(LaunchConfig& c, const MenuHooks& h)
{
    const auto save = [h]() { if (h.save) h.save(); };
    std::vector<X> r;
    // (uxpolish: the starts first, then the settings they use; the 4 players mode tagged Experimental everywhere)
    r.push_back({"Local 2 Players", []() { return std::string("Start"); }, [h]() { if (h.startCoop) h.startCoop(2); },
                 "Two players on this PC from Chapter 1 (Player 2's input: Options > Controls)"});
    r.push_back({"Local 4 Players", []() { return std::string("Start"); }, [h]() { if (h.startCoop) h.startCoop(4); },
                 "The 4-player game on this PC from Chapter 1: player 1 starts, players 2-4 join with their trigger / start"});
    experimental(r);
    r.push_back({"Online Co-op", []() { return page("Host / Join"); }, [h]() { if (h.online) h.online(); },
                 "Two PCs over the internet or the LAN: host a game or join one (3-4 PCs: Experimental)"});
    // player 2 can also join with key 2; 3 / 4: the experimental 4P mode (docs/coop4.md)
    r.push_back({"Players in Arcade", [&c]() { return std::to_string(c.players); },
                 [&c, save]() { c.players = c.players % 4 + 1; save(); },
                 "How many play from the start of Arcade / Chapter Select (the others join with their trigger / start button)"});
    listed(r, {"1", "2", "3", "4"});
    experimental(r, [&c]() { return c.players >= 3; });
    // computer teammates (local play): player 2 (any game), players 3 / 4 (the 4P mode)
    for (int p = 1; p < 4; ++p) {
        r.push_back({"Player " + std::to_string(p + 1) + ": Human / CPU",
                     [&c, p]() {
                         static const char* const k[4] = {"Human", "CPU Easy", "CPU Normal", "CPU Hard"};
                         return std::string(k[std::clamp(c.seatBot[p], 0, 3)]);
                     },
                     [&c, save, p]() { c.seatBot[p] = (c.seatBot[p] + 1) % 4; save(); },
                     "Player " + std::to_string(p + 1) + " played by you or by the computer (local play" +
                         (p >= 2 ? std::string(", 4 players mode)") : std::string(")"))});
        listed(r, {"Human", "CPU Easy", "CPU Normal", "CPU Hard"});
        if (p >= 2) experimental(r);
    }
    return r;
}

std::vector<Frontend::Extra> menuBindRows(int p, LaunchConfig& c, BindCapture& cap, const MenuHooks& h)
{
    const auto save = [h]() { if (h.save) h.save(); };
    const auto padsChanged = [h]() { if (h.padsChanged) h.padsChanged(); };
    // the table the pad uses now (Auto: its style's buttons) and the pad's type (the buttons' names)
    const auto padNow = [&c, h](int q) {
        return c.padBinds[q].custom ? c.padBinds[q] : h.padDefaults ? h.padDefaults(q) : PadBinds{};
    };
    const auto padType = [h](int q) { return h.padType ? h.padType(q) : int(SDL_GAMEPAD_TYPE_UNKNOWN); };
    std::vector<X> r;
    const X player{"Player", [p]() { return "Player " + std::to_string(p + 1); },
                   [&cap, h, p]() {
                       cap.cancel("");
                       cap.message.clear();
                       if (h.bindPlayer) h.bindPlayer((p + 1) % 4);
                   },
                   "Whose keys and buttons these are (click: the next player)"};
    const X status{"Status", [&cap]() {
                       std::string m = cap.message.empty() ? std::string("Click a row") : cap.message;
                       if (cap.active())
                           m += " " + std::to_string(cap.secondsLeft(SDL_GetTicks())) + "s";
                       return m.size() > 22 ? util::utf8Head(m, 21) + "." : m;
                   }, nullptr,
                   "A key / button the row has is taken off, a new one added; ! = used twice"};
    // page 1: the keyboard (12 rows)
    r.push_back(player);
    r.push_back(status);
    for (int a = 0; a < kKeyActions; ++a)
        r.push_back({std::string("Key: ") + keyActionName(a), [&c, &cap, p, a]() {
                         if (cap.active() && !cap.pad() && cap.player() == p && cap.action() == a)
                             return std::string("Press a key");
                         const KeyBinds& k = c.keys[p];
                         bool clash = false;
                         for (int s = 0; s < kKeySlots; ++s)
                             clash = clash || (k.k[a][s] && !keyClash(c.keys, p, a, k.k[a][s]).empty());
                         return (clash ? "! " : "") + keyList(k, a);
                     },
                     [&cap, p, a]() { cap.start(p, a, false, SDL_GetTicks()); },
                     std::string("Player ") + std::to_string(p + 1) + "'s " + keyActionName(a) +
                         " keys (two at most): click, then press a key (Esc: cancel)" +
                         (a == kActPause ? "; Esc always pauses" : "")});
    r.push_back({"Reset Keys", [&c, p]() { return std::string(c.keys[p] == defaultKeys(p) ? "Defaults" : "Reset"); },
                 [&c, &cap, save, p]() {
                     c.keys[p] = defaultKeys(p);
                     cap.message = "P" + std::to_string(p + 1) + " keys: defaults";
                     save();
                 },
                 "This player's keys back to the defaults"});
    r.push_back({"Reset Everyone", []() { return std::string("Keys + Pads"); },
                 [&c, &cap, save, padsChanged]() {
                     for (int q = 0; q < 4; ++q) {
                         c.keys[q] = defaultKeys(q);
                         c.padBinds[q] = PadBinds{};
                     }
                     cap.message = "Everyone: defaults";
                     save();
                     padsChanged();
                 },
                 "Every player's keys and pad buttons back to the defaults"});
    // page 2: the gamepad
    r.push_back(player);
    r.push_back(status);
    for (int a = 0; a < kPadActions; ++a) {
        static const char* const kHelp[kPadActions] = {
            "shoot", "reload", "start / join / continue (in play: the pause)", "pause", "put the crosshair in the middle",
            "hold the gyro (the ratchet; a tap: the middle)", "slow the aim while held (precise)"};
        r.push_back({std::string("Pad: ") + padActionName(a), [&cap, padNow, padType, p, a]() {
                         if (cap.active() && cap.pad() && cap.player() == p && cap.action() == a)
                             return std::string("Press a button");
                         const PadBinds b = padNow(p);
                         uint32_t others = 0;
                         for (int o = 0; o < kPadActions; ++o)
                             if (o != a) others |= b.mask[o];
                         return ((b.mask[a] & others) ? "! " : "") + padList(b.mask[a], padType(p));
                     },
                     [&cap, p, a]() { cap.start(p, a, true, SDL_GetTicks()); },
                     std::string("The buttons that ") + kHelp[a] + ": click, then press one on the pad (Esc: cancel)"});
    }
    r.push_back({"Reset Pad", [&c, p]() { return std::string(c.padBinds[p].custom ? "Reset" : "Auto"); },
                 [&c, &cap, save, padsChanged, p]() {
                     c.padBinds[p] = PadBinds{};
                     cap.message = "P" + std::to_string(p + 1) + " pad: Auto";
                     save();
                     padsChanged();
                 },
                 "Auto: the buttons of the pad's control style (Controller page); click to undo every change"});
    return r;
}

const char* padFieldHelp(const char* key)
{
    static const struct { const char* key; const char* help; } k[] = {
        {"style", "How the pad aims: a stick, the gyro, the gyro + a stick, or the touchpad"},
        {"speed", "How fast the aim moves (the stick's top speed, the gyro's sensitivity)"},
        {"vert", "The vertical sensitivity against the horizontal"},
        {"invert", "Turn the aim's horizontal or vertical direction around"},
        {"stick", "Which stick aims (or fine-aims)"},
        {"fine", "The fine-aim stick's speed against the aiming speed"},
        {"dead", "The sticks' dead zone in the middle"},
        {"curve", "The stick's response curve (Linear: no acceleration)"},
        {"smooth", "Smooth the gyro's tiny jitter (adds a little lag)"},
        {"space", "World: up stays up when the pad is tilted; Local: the pad's own axes"},
        {"shoot", "The button that shoots"},
        {"touch", "The touchpad: off, a pointer, or a trackpad"},
        {"touchclick", "The touchpad's click: reload or shoot"},
        {"aimsens", "How fast this player's pad aims: the sticks, the gyro and the trackpad (100%: as tuned)"},
        {"trigger", "How far the trigger goes in before it fires"},
        {"adaptive", "DualSense: the trigger's gun-like resistance"},
        {"rumblehit", "Vibration when you are hit"},
        {"rumbleenemy", "Vibration when your shot hits an enemy"},
        {"rumblereload", "Vibration when you reload"},
    };
    for (const auto& e : k)
        if (key && std::strcmp(key, e.key) == 0)
            return e.help;
    return "A controller setting (click: the next value)";
}

} // namespace hotd::game

namespace hotd::game {

std::vector<std::string> menuRankingTables()
{
    std::vector<std::string> t;
    for (int four = 0; four < 2; ++four)
        for (int p = 0; p < kSonicPresets; ++p)
            t.push_back(four ? (p ? std::string("4 Players: ") + sonicName(p) : std::string("4 Players"))
                             : (p ? std::string("Sonic Mode: ") + sonicName(p) : std::string("Arcade Mode")));
    return t;
}

// menus3 (user 2026-10-28): the settings' pictures (preview_art.h): "models_<m>" / "blood_<k>" -> the in-memory key
// "<kind>|<models>|<blood colour 0xRRGGBB>|<sight>" (the sight: player 1's crosshair setting), "" for another name
std::string previewKey(const std::string& name, const LaunchConfig& c, bool pcOk)
{
    const size_t u = name.find('_');
    if (u == std::string::npos)
        return std::string();
    const std::string kind = name.substr(0, u);
    const int k = std::atoi(name.c_str() + u + 1);
    int m = c.models == 1 && pcOk ? 1 : 0;
    uint32_t blood = 0;
    if (kind == "models" && k >= 0 && k < 2)
        m = k;
    else if (kind == "blood" && k >= 0 && k < 6)
        blood = previewBloodRgb(k, c.bloodRgb);
    else
        return std::string();
    const SeatSight& s = c.sights[0];
    char b[160];
    std::snprintf(b, sizeof b, "%s|%d|%06X|%d,%d,%d,%d,%s", kind.c_str(), m, unsigned(blood), s.style, s.size, s.thick, s.colour,
                  s.custom.c_str());
    return b;
}

std::string previewBaseFile(const std::string& dir, int models, int kind)
{
    return dir + (kind == kPreviewModels ? "/bridge_" : "/base_") + (models == 1 ? "pc" : "arcade") + ".png";
}

bool previewRender(const std::string& key, const std::string& dir, const LaunchConfig& c, const MenuHooks& h,
                   std::vector<uint8_t>& rgba, int& w, int& hgt)
{
    const size_t a = key.find('|');
    if (a == std::string::npos)
        return false;
    const int m = std::atoi(key.c_str() + a + 1);
    const size_t b = key.find('|', a + 1);
    const uint32_t blood = b == std::string::npos ? 0 : uint32_t(std::strtoul(key.c_str() + b + 1, nullptr, 16));
    // the bases read once per file (a new one: its time changed)
    static std::map<std::string, std::pair<std::filesystem::file_time_type, PreviewImg>> bases;
    const int kind = key.compare(0, 7, "models|") == 0 ? kPreviewModels : kPreviewBlood;   // (prevbridge: Chapter 1's bridge)
    const std::string f = previewBaseFile(dir, m, kind);
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(f, ec);
    if (ec)
        return false;
    auto it = bases.find(f);
    if (it == bases.end() || it->second.first != t) {
        PreviewImg img;
        std::string err;
        if (!readPngRgba(f, img.w, img.h, img.rgba, err) || img.w <= 0)
            return false;
        it = bases.insert_or_assign(f, std::make_pair(t, std::move(img))).first;
    }
    SightArt art;
    std::vector<CustomSight> sets = h.customSights ? h.customSights() : std::vector<CustomSight>();
    seatSightArt(c.sights[0], m == 1, h.sightSprites ? h.sightSprites() : nullptr, sets, 0, it->second.second.h * 2, art);   // (the sight a little bigger than the game draws it, to be seen)
    PreviewImg o = previewCompose(it->second.second, blood, art, kind);
    rgba = std::move(o.rgba), w = o.w, hgt = o.h;
    return w > 0;
}

} // namespace hotd::game
