#include "game/launcher.h"

#include "assets/archive.h"
#include "game/chapters.h"
#include "game/files_check.h"
#include "game/lang.h"
#include "game/sights.h"
#include "util/fullscreen.h"
#include "util/resolution.h"
#include "util/png_read.h"
#include "util/utf8.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace hotd::game {

namespace {

// setup: the dialog's buttons (the settings rows are gone: Options in the menu has them all)
enum Action { kArcadeFolder = 1, kArcadeZip, kPcFolder, kPcZip, kCheck, kStart, kQuit,
              kExtraBase = 1000 };   // (kExtraBase + row: the extra section's rows, online co-op)

// a package's path: inside `base` -> relative to it (saving); relative -> under `base` (loading)
std::string toBase(const std::string& p, const std::string& base)
{
    if (base.empty() || p.empty())
        return p;
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::weakly_canonical(p, ec);
    const std::filesystem::path b = std::filesystem::weakly_canonical(base, ec);
    if (ec)
        return p;
    const std::string a = abs.string(), bs = b.string();
    if (a.size() > bs.size() + 1 && SDL_strncasecmp(a.c_str(), bs.c_str(), bs.size()) == 0 &&
        (a[bs.size()] == '\\' || a[bs.size()] == '/'))
        return a.substr(bs.size() + 1);
    return p;
}

std::string fromBase(const std::string& p, const std::string& base)
{
    if (base.empty() || p.empty() || std::filesystem::path(p).is_absolute())
        return p;
    return (std::filesystem::path(base) / p).lexically_normal().string();
}

} // namespace

const char* const kPadStyleKeys[kPadStyles] = {"classic", "motion", "mix", "touch"};
const char* const kPadStyleNames[kPadStyles + 1] = {"Auto", "Classic Pad", "Motion Only", "Motion + Stick", "Touchpad"};

const PadField* padFields(int& n)
{
    static const char* const kCurve[] = {"Linear", "Low", "Medium", "High"};
    static const char* const kDead[] = {"0%", "4%", "8%", "12%", "16%", "20%", "25%"};
    static const char* const kTurbo[] = {"Off", "Low", "High"};
    static const char* const kTighten[] = {"Off", "1 deg/s", "2 deg/s", "3 deg/s"};
    static const char* const kShake[] = {"Off", "Low", "Medium", "High"};
    static const char* const kTen[] = {"10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"};
    static const char* const kVert[] = {"50%", "60%", "70%", "80%", "90%", "100%", "110%", "120%", "130%", "140%", "150%"};
    static const char* const kInvert[] = {"Off", "X", "Y", "X + Y"};
    static const char* const kLevel[] = {"Off", "Light", "Medium", "Strong"};
    static const char* const kSmooth[] = {"Off", "Low", "Medium", "High"};
    static const char* const kStick[] = {"Default", "Swapped"};
    static const char* const kShoot[] = {"Auto", "R2", "L2", "R1", "L1", "South (A / Cross)"};
    static const char* const kSpace[] = {"Player", "World", "Local"};
    static const char* const kTouch[] = {"Auto", "Off", "Pointer", "Trackpad"};
    static const char* const kClick[] = {"Reload", "Shoot"};
    static const char* const kAim[] = {"50%", "60%", "70%", "80%", "90%", "100%", "110%", "120%", "130%", "140%", "150%", "160%",
                                       "170%", "180%", "190%", "200%"};
    static const char* const kTrig[] = {"5%", "10%", "15%", "20%", "25%", "30%", "35%", "40%", "45%"};
    constexpr unsigned C = 1, M = 2, X = 4, T = 8, All = 15;
    static const PadField k[] = {
        {"style", "Style", nullptr, &PadProfile::style, 0, kPadStyles, kPadStyleNames, 0},
        {"speed", "Sensitivity", &PadStyle::speed, nullptr, 1, 10, nullptr, All},
        {"vert", "Vertical", &PadStyle::vert, nullptr, 5, 15, kVert, All},
        {"invert", "Invert", &PadStyle::invert, nullptr, 0, 3, kInvert, All},
        {"stick", "Sticks", &PadStyle::stick, nullptr, 0, 1, kStick, C | X},
        {"fine", "Fine Aim Speed", &PadStyle::fine, nullptr, 1, 10, kTen, C | X},
        {"dead", "Stick Dead Zone", &PadStyle::dead, nullptr, 0, 6, kDead, C | X},
        {"curve", "Stick Curve", &PadStyle::curve, nullptr, 0, 3, kCurve, C | X},
        {"turbo", "Stick Edge Turbo", &PadStyle::turbo, nullptr, 0, 2, kTurbo, C},
        {"smooth", "Gyro Smoothing", &PadStyle::smooth, nullptr, 0, 3, kSmooth, M | X},
        {"tighten", "Gyro Tightening", &PadStyle::tighten, nullptr, 0, 3, kTighten, M | X},
        {"gaccel", "Gyro Acceleration", &PadStyle::gaccel, nullptr, 0, 2, kTurbo, M | X},
        {"space", "Gyro Space", &PadStyle::space, nullptr, 0, 2, kSpace, M | X},
        {"shake", "Shake to Reload", &PadStyle::shake, nullptr, 0, 3, kShake, M | X},
        {"shoot", "Shoot Button", &PadStyle::shoot, nullptr, 0, 5, kShoot, All},
        {"touch", "Touchpad", &PadStyle::touch, nullptr, 0, 3, kTouch, All},
        {"touchclick", "Touchpad Click", &PadStyle::touchClick, nullptr, 0, 1, kClick, All},
        {"aimsens", "Aim Sensitivity", nullptr, &PadProfile::aimSens, 5, 20, kAim, 0},   // (padfix: also on Controls)
        {"trigger", "Trigger Point", nullptr, &PadProfile::trigger, 1, 9, kTrig, 0},
        {"adaptive", "Adaptive Trigger", nullptr, &PadProfile::adaptive, 0, 3, kLevel, 0},
        {"rumblehit", "Vibration: Hit", nullptr, &PadProfile::rumbleHit, 0, 3, kLevel, 0},
        {"rumbleenemy", "Vibration: Enemy Hit", nullptr, &PadProfile::rumbleEnemy, 0, 3, kLevel, 0},
        {"rumblereload", "Vibration: Reload", nullptr, &PadProfile::rumbleReload, 0, 3, kLevel, 0},
    };
    (void)M; (void)T;
    n = int(sizeof k / sizeof k[0]);
    return k;
}

const char* const kPadPresetNames[kPadPresets + 1] = {"Custom", "Precise", "Balanced", "Fast"};

// the presets' feel values (the rest of a style, its buttons / invert / touchpad / shake, is kept)
namespace {
struct PadFeel { int speed, fine, dead, curve, turbo, smooth, tighten, gaccel; };
constexpr PadFeel kPadFeel[kPadPresets] = {
    {4, 3, 2, 3, 0, 2, 1, 0},   // Precise: 0.85x gyro / a slower stick, curve 2.5, smoothing to 6 deg/s, tightening 1
    {5, 4, 2, 2, 1, 1, 0, 0},   // Balanced: the defaults (PadStyle)
    {7, 5, 2, 1, 2, 1, 0, 1},   // Fast: 1.4x gyro / a faster stick, curve 1.5, the high turbo, low acceleration
};
} // namespace

void applyPadPreset(PadStyle& s, int preset)
{
    if (preset < 1 || preset > kPadPresets)
        return;
    const PadFeel& f = kPadFeel[preset - 1];
    s.speed = f.speed, s.fine = f.fine, s.dead = f.dead, s.curve = f.curve, s.turbo = f.turbo, s.smooth = f.smooth;
    s.tighten = f.tighten, s.gaccel = f.gaccel;
}

int padPresetOf(const PadStyle& s)
{
    for (int i = 0; i < kPadPresets; ++i) {
        const PadFeel& f = kPadFeel[i];
        if (s.speed == f.speed && s.fine == f.fine && s.dead == f.dead && s.curve == f.curve && s.turbo == f.turbo &&
            s.smooth == f.smooth && s.tighten == f.tighten && s.gaccel == f.gaccel)
            return i + 1;
    }
    return 0;
}

bool LaunchConfig::load(const std::string& file)
{
    std::ifstream raw(file, std::ios::binary);
    if (!raw)
        return false;
    // firstrun: a damaged launcher.ini (NUL bytes from a crash or a full disk, binary garbage, no key=value line at
    // all) is kept as launcher.ini.damaged and the defaults are used, instead of reading what is left of it
    {
        const std::string all((std::istreambuf_iterator<char>(raw)), std::istreambuf_iterator<char>());
        size_t bad = 0, settings = 0;
        for (unsigned char c : all) bad += c == 0 || (c < 32 && c != '\n' && c != '\r' && c != '\t');
        std::istringstream ls(all);
        for (std::string l; std::getline(ls, l);) settings += l.find('=') != std::string::npos;
        if (bad > 0 || (settings == 0 && all.find_first_not_of(" \t\r\n") != std::string::npos)) {
            raw.close();
            std::error_code ec;
            std::filesystem::rename(file, file + ".damaged", ec);
            SDL_Log("launcher.ini is damaged (%zu bad bytes, %zu settings): kept as launcher.ini.damaged, the defaults are used%s",
                    bad, settings, ec ? " (could not rename it)" : "");
            return false;
        }
    }
    raw.close();
    std::ifstream f(file);
    std::string line;
    bool bloodV2 = false;   // launcher.ini from before yellow (4) had the custom colour at 4
    int padV = 1;           // padv=2: the aim's feel values of worker "padfeel" (2026-10-20); older ones are reset
                            // padv=3 (relpad): no Sideways style (the Touchpad 5 -> 4)
    int padStyleRaw[4] = {-1, -1, -1, -1};   // relpad: pad1.style= as written (padv < 3: 4 was Sideways, 5 Touchpad)
    bool enemy4pSeen = false;   // enemy4p= (cpuai 2026-10-26) not there yet: boss4p= at the old default 25 becomes the new 50
    bool sightSeen[4] = {};     // firing range: a player's crosshair keys there (else player 1's)
    int displayModeSeen = -1, exclusiveSeen = -1;   // rescrash: displaymode= / exclusive= (Video > Display Mode)
    while (std::getline(f, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        const int n = std::atoi(v.c_str());
        if (bindsIniLine(k, v, keys, padBinds)) {}   // keybind: key1.fire= / padbind1.fire= ...
        else if (k == "arcade") arcade = fromBase(v, base);
        else if (k == "pc") pc = fromBase(v, base);
        else if (k == "models") models = std::clamp(n, 0, 1);
        else if (k == "sound") sound = std::clamp(n, 0, 2);
        else if (k == "players") players = std::clamp(n, 1, 4);   // 3 / 4: the 4P mode (docs/coop4.md)
        else if (k == "p3input") p3Input = n >= 0 && n <= 3 ? n : 2;   // 0 mouse, 1 keyboard, 2 pad, 3 gun
        else if (k == "p2bot" || k == "p3bot" || k == "p4bot") seatBot[k[1] - '1'] = std::clamp(n, 0, 3);
        else if (k == "p4input") p4Input = n >= 0 && n <= 3 ? n : 2;
        else if (k == "p2input") p2Input = std::clamp(n, 0, 3);
        else if (k == "p1input") p1Input = n == 2 || n == 3 ? n : 0;
        else if (k.size() > 5 && k.rfind("pad", 0) == 0 && k[3] >= '1' && k[3] <= '4' && k[4] == '.') {
            // pad1.<key> (the player's) or pad1.<style>.<key> (a style's)
            int nf = 0;
            const PadField* pfs = padFields(nf);
            PadProfile& pp = pad[k[3] - '1'];
            std::string rest = k.substr(5);
            int st = -1;
            if (const size_t dot = rest.find('.'); dot != std::string::npos) {
                for (int s = 0; s < kPadStyles; ++s)
                    if (rest.compare(0, dot, kPadStyleKeys[s]) == 0) st = s;
                rest = rest.substr(dot + 1);
            }
            if (st < 0 && rest == "style") padStyleRaw[k[3] - '1'] = n;
            for (int i = 0; i < nf; ++i) {
                if (rest != pfs[i].key) continue;
                if (st >= 0 && pfs[i].styleMember) pp.s[st].*pfs[i].styleMember = std::clamp(n, pfs[i].lo, pfs[i].hi);
                else if (st < 0 && pfs[i].member) pp.*pfs[i].member = std::clamp(n, pfs[i].lo, pfs[i].hi);
            }
        }
        else if (k.size() == 7 && k.rfind("gun", 0) == 0 && k[3] >= '1' && k[3] <= '4' && k.compare(4, 3, "cal") == 0) {
            float c[4];
            if (std::sscanf(v.c_str(), "%f,%f,%f,%f", &c[0], &c[1], &c[2], &c[3]) == 4 && std::abs(c[2] - c[0]) > 0.05f &&
                std::abs(c[3] - c[1]) > 0.05f)
                std::copy(c, c + 4, gunCal[k[3] - '1']);
        }
        else if (k.size() == 7 && k.rfind("gun", 0) == 0 && k[3] >= '1' && k[3] <= '4' && k.compare(4, 3, "dev") == 0)
            gunDev[k[3] - '1'] = v;
        else if (k.size() == 8 && k.rfind("gun", 0) == 0 && k[3] >= '1' && k[3] <= '4' && k.compare(4, 4, "name") == 0)
            gunName[k[3] - '1'] = v;
        else if (k == "difficulty") difficulty = std::clamp(n, 0, 4);
        else if (k == "lives") lives = std::clamp(n, 0, 11);
        else if (k == "blood") blood = std::clamp(n, 0, 5);
        else if (k == "bloodv") bloodV2 = n >= 2;
        else if (k == "padv") padV = n;
        else if (k == "bloodrgb") bloodRgb = int(std::strtoul(v.c_str(), nullptr, 16) & 0xFFFFFF);
        else if (k == "cuts") cuts = n != 0;
        else if (k == "title") title = n != 0;
        else if (k == "fbi") fbi = n != 0;
        else if (k == "pcsplash") pcSplash = n != 0;   // (feat29)
        else if (k == "gametext") gameText = n == 1 ? 1 : 0;
        else if (k == "lang") language = util::utf8Head(v, 16);
        else if (k.rfind("cross", 0) == 0) {   // firing range: player 1's crosshair, then the same keys + 2 / 3 / 4
            const char last = k.back();
            const int s = last >= '2' && last <= '4' ? last - '1' : 0;
            const std::string b = s ? k.substr(0, k.size() - 1) : k;
            SeatSight& ss = sights[s];
            if (b == "crosshair") ss.style = sightStyleKept(std::clamp(n, 0, kSightStyles - 1));
            else if (b == "crosshaircustom") ss.custom = v;
            else if (b == "crosssize") ss.size = std::clamp(n, 0, kSizeSteps - 1);
            else if (b == "crossthick") ss.thick = std::clamp(n, 0, kThickSteps - 1);
            else if (b == "crosscolour") ss.colour = std::clamp(n, 0, kSightColours - 1);
            else continue;
            sightSeen[s] = true;
        }
        else if (k == "aspect") aspect = std::clamp(n, 0, 2);
        else if (k == "fullscreen") fullscreen = n != 0;
        else if (k == "displaymode") displayModeSeen = std::clamp(n, 0, 2);
        else if (k == "exclusive") exclusiveSeen = n != 0;
        else if (k == "resolution_fullscreen") { const util::Size s = util::parseSize(v); resFullW = s.w, resFullH = s.h; }
        else if (k == "resolution_window") { const util::Size s = util::parseSize(v); resWinW = s.w, resWinH = s.h; }
        else if (k == "autoreload") autoReload = n != 0;
        else if (k == "scoredisplay") scoreDisplay = n != 0;
        else if (k == "attractgore") attractGore = n != 0;
        else if (k == "lgupdatecheck") updateCheck = n != 0;
        else if (k == "outputs") outputs = n != 0;
        else if (k == "outputsport") outputsPort = n > 0 && n < 65536 ? n : 8000;
        else if (k == "holdskip") holdSkip = n != 0;
        else if (k == "gamespeed") gameSpeed = v == "auto" || v == "0" ? -1 : v == "arcade" ? 0 : n == 1 || v == "60" ? 1 : -1;   // (the old 0: the default then -> auto)
        else if (k == "continues") continues = std::clamp(n, 0, 9);
        else if (k == "freeplay") freePlay = n != 0;
        else if (k == "framerate") frameRate = parseFrameRate(v);
        else if (k == "vsync") vsync = n != 0;
        else if (k == "renderer") renderer = v == "gl" ? 1 : v == "angle" ? 2 : 0;
        else if (k == "smooth") smooth = n != 0;
        else if (k == "drawdepth") drawDepth = v == "buffer" ? 0 : v == "sort" ? 1 : drawDepth;
        else if (k == "netname") netName = util::utf8Head(v, 16);
        else if (k == "display") display = v;   // remakefix: the monitor (util/resolution.h displayKey)
        else if (k == "netinfo") netInfo = std::clamp(n, 0, 2);
        else if (k == "netpcmode") netPcMode = n != 0;
        else if (k == "netbossmode") netBossMode = n >= 2 && n <= 6 ? n : 0;
        else if (k == "netpcchar") {   // a,b (1-19: 7-19 PC Mode's Extra Characters; 0 Auto)
            int a = 0, b = 0;
            if (std::sscanf(v.c_str(), "%d,%d", &a, &b) >= 1)
                netPcChar[0] = std::clamp(a, 0, 19), netPcChar[1] = std::clamp(b, 0, 19);
        }
        else if (k == "sonicmode") sonic = std::max(sonicParse(v), 0);
        else if (k == "boss4p") boss4p = std::clamp(n, 0, 200);
        else if (k == "enemy4p") enemy4p = std::clamp(n, 0, 200), enemy4pSeen = true;
        else if (k == "teamflash") teamFlash = std::clamp(n, 0, 3);
        else if (k == "guntype") gunKind = std::clamp(n, 0, 2);     // padpass: the light guns' kind
        else if (k == "gunflash") gunFlash = std::clamp(n, 0, 2);   // padpass: the shot flash
        else if (k == "padprompts") padPrompts = std::clamp(n, 0, 14);   // padglyphs: Button Icons
        else if (k == "buttonstyle") buttonStyle = std::clamp(n, 0, 2);   // padglyphs: Button Style
        else if (k == "iconoutline") iconOutline = n ? 1 : 0;   // menus3: Icon Outline
        else if (k == "touchcursor") touchCursor = std::clamp(n, 0, 1);   // padglyphs: the pad cursor
        else if (k == "gyrocursor") gyroCursor = std::clamp(n, 0, 2);
        else if (k == "cursorspeed") cursorSpeed = std::clamp(n, 1, 10);
        else if (k == "gunborder") gunBorder = std::clamp(n, 0, 4); // padpass: the white border
        else if (k == "trailer") trailer = n != 0;
        else if (k == "hitsparks") hitSparks = n != 0;
        else if (k == "muzzleflash") muzzleFlash = n != 0;
        else if (k == "bloodparticles") bloodParticles = n != 0;
        else if (k == "scene4p") scene4p = v;
        else if (k == "scenecast") sceneCast = v;
        else if (k == "crt") crt.preset = std::max(crtPresetParse(v), 0);
        else if (k == "crt.scan") crt.scan = std::clamp(n, 0, 10);
        else if (k == "crt.mask") crt.mask = std::clamp(n, 0, 10);
        else if (k == "crt.masktype") crt.maskType = std::clamp(n, 0, 3);
        else if (k == "crt.curve") crt.curve = std::clamp(n, 0, 10);
        else if (k == "crt.bloom") crt.bloom = std::clamp(n, 0, 10);
        else if (k == "crt.bright") crt.bright = std::clamp(n, 0, 10);
        else if (k == "crt.crispsights") crt.crispSights = n != 0;
        else if (k == "crt.shotfiltered") crt.shotFiltered = n != 0;
        else if (k == "music") music = n != 0;
        else if (k == "musicvolume") musicVolume = std::clamp(n, 0, 100) / 5 * 5;   // (prerel: 5 % steps)
        else if (k == "sfxvolume") sfxVolume = std::clamp(n, 0, 100) / 5 * 5;   // (prerel: 5 % steps)
        else if (k == "menumusic") menuMusic = menuSongOf(n).track;
        else if (k == "menustyle") menuStyle = std::clamp(n, 0, 1);
        else if (k == "livebg") liveBackground = std::clamp(n, 0, 2);
        else if (k == "menufps") menuFps = n == 30 || n == 60 || n == 120 ? n : 0;
        else if (k == "menublood") menuBlood = uint32_t(std::strtoul(v.c_str(), nullptr, 10));
        else if (k == "gallery") galleryEntry = util::utf8Head(v, 16);
        else if (k == "gallerybg") galleryBg = std::clamp(n, 0, 63);
        else if (k == "gallerylayout") galleryLayout = std::clamp(n, 0, 2);
        else if (k == "musicnumbering") musicNumbering = std::clamp(n, 0, 2);
        else if (k.rfind("steamart", 0) == 0 && k.size() == 9 && k[8] >= '0' && k[8] <= '4') steamArt[k[8] - '0'] = v;
        else if (k == "chapter") chapter = std::clamp(n, 1, 4) - 1;
        else if (k.rfind("modset.", 0) == 0 && k.size() > 7) modSettings[k.substr(7)] = util::utf8Head(v, 64);
        else if (k == "mods" || k == "modsoff") {
            std::vector<std::string>& l = k == "mods" ? modsOn : modsOff;
            l.clear();
            std::string item;
            for (char ch : v + ",")
                if (ch == ',') { if (!item.empty()) l.push_back(item); item.clear(); }
                else item += ch;
        }
    }
    for (int s = 1; s < 4; ++s)   // firing range: an older launcher.ini (one crosshair for all): player 1's for everyone
        if (!sightSeen[s])
            sights[s] = sights[0];
    if (!chapterPlayable(chapter))   // a chapter of an older build that is not playable in this one
        chapter = 0;
    if (!bloodV2 && blood == 4)
        blood = 5;
    // rescrash: displaymode= the start's Display Mode (0 Windowed / 1 Borderless / 2 Fullscreen, exclusive), exclusive=
    // the fullscreen F11 goes to from a window; fullscreen= is still written (older versions). Before them: a chosen
    // fullscreen resolution was exclusive
    if (exclusiveSeen >= 0)
        exclusive = exclusiveSeen != 0;
    else if (displayModeSeen >= 1)
        exclusive = displayModeSeen == 2;
    else
        exclusive = resFullW > 0 && resFullH > 0;
    if (displayModeSeen >= 0)
        fullscreen = displayModeSeen != 0;
    if (!enemy4pSeen && boss4p == 25)   // the 4P bosses: +25 % a player was too easy (docs/coop4.md "Enemy pressure")
        boss4p = 50;
    if (padV < 2)   // the speed / dead zone / curve / smoothing / space scales changed: their new defaults
        for (PadProfile& pp : pad)
            for (PadStyle& st : pp.s) {
                const PadStyle def;
                st.speed = def.speed, st.fine = def.fine, st.dead = def.dead, st.curve = def.curve, st.smooth = def.smooth;
                st.space = def.space;
            }
    // relpad (user 2026-10-30): the Sideways grip style is gone. Older files (padv < 3) had it at 4 and the Touchpad at
    // 5: Sideways loads as Motion + stick (the pad held normally), the Touchpad as 4
    for (int p = 0; p < 4; ++p)
        if (padV < 3 && padStyleRaw[p] >= 4)
            pad[p].style = padStyleRaw[p] == 4 ? 3 : 4;
    return true;
}

void LaunchConfig::save(const std::string& file) const
{
    if (readOnly) {
        static bool told = false;
        if (!told)
            SDL_Log("launcher.ini not written (a test run without HOTD_PREF_DIR: the player's settings stay as they are)");
        told = true;
        return;
    }
    // firstrun: written to launcher.ini.new, then put in place (a crash or a full disk mid-write never leaves half a file)
    struct Commit {
        std::ofstream f;
        std::string file;
        ~Commit()
        {
            f.close();
            if (!f) return;
            std::error_code ec;
            std::filesystem::rename(file + ".new", file, ec);
            if (ec) SDL_Log("launcher.ini not saved: %s", ec.message().c_str());
        }
    } c{std::ofstream(file + ".new"), file};
    std::ofstream& f = c.f;
    f << "arcade=" << toBase(arcade, base) << "\npc=" << toBase(pc, base) << "\nmodels=" << models << "\nsound=" << sound << "\nplayers=" << players << "\np2input=" << p2Input
      << "\ndifficulty=" << difficulty << "\nlives=" << lives << "\nblood=" << blood << "\ncuts=" << (cuts ? 1 : 0) << "\ntitle=" << (title ? 1 : 0) << "\nfbi=" << (fbi ? 1 : 0) << "\npcsplash=" << (pcSplash ? 1 : 0) << "\ngametext=" << gameText << "\nlang=" << language
      << "\naspect=" << aspect << "\nfullscreen=" << ((util::fullscreenLive() >= 0 ? util::fullscreenLive() != 0 : fullscreen) ? 1 : 0) <<"\nautoreload=" << (autoReload ? 1 : 0) << "\nscoredisplay=" << (scoreDisplay ? 1 : 0) << "\nattractgore=" << (attractGore ? 1 : 0) << "\nlgupdatecheck=" << (updateCheck ? 1 : 0) << "\noutputs=" << (outputs ? 1 : 0) << "\noutputsport=" << outputsPort << "\nholdskip=" << (holdSkip ? 1 : 0)
      << "\ndisplaymode=" << ((util::fullscreenLive() >= 0 ? util::fullscreenLive() != 0 : fullscreen) ? (exclusive ? 2 : 1) : 0)
      << "\nexclusive=" << (exclusive ? 1 : 0)
      << "\nresolution_fullscreen=" << util::sizeKey({resFullW, resFullH}) << "\nresolution_window=" << util::sizeKey({resWinW, resWinH})
      << "\ngamespeed=" << (gameSpeed < 0 ? "auto" : gameSpeed == 1 ? "60" : "arcade") << "\ncontinues=" << continues << "\nfreeplay=" << (freePlay ? 1 : 0) << "\nframerate=" << (frameRate == 0 ? std::string("display") : frameRate < 0 ? std::string("unlimited") : frameRate == kFrameRateArcade ? std::string("arcade") : std::to_string(frameRate))
      << "\nvsync=" << (vsync ? 1 : 0) << "\nrenderer=" << (renderer == 1 ? "gl" : renderer == 2 ? "angle" : "auto") << "\nsmooth=" << (smooth ? 1 : 0) << "\nhitsparks=" << (hitSparks ? 1 : 0) << "\nmuzzleflash=" << (muzzleFlash ? 1 : 0) << "\nbloodparticles=" << (bloodParticles ? 1 : 0) << "\nnetname=" << netName << "\ndisplay=" << display << "\nnetinfo=" << netInfo << "\nnetpcmode=" << (netPcMode ? 1 : 0) << "\nnetbossmode=" << netBossMode << "\nnetpcchar=" << netPcChar[0] << ',' << netPcChar[1] << "\nsonicmode=" << sonicKey(sonic) << "\nboss4p=" << boss4p << "\nenemy4p=" << enemy4p << "\nteamflash=" << teamFlash << "\ntrailer=" << (trailer ? 1 : 0) << "\nscene4p=" << scene4p
      << "\ncrt=" << crtPresetKey(crt.preset) << "\ncrt.scan=" << crt.scan << "\ncrt.mask=" << crt.mask << "\ncrt.masktype=" << crt.maskType
      << "\ncrt.curve=" << crt.curve << "\ncrt.bloom=" << crt.bloom << "\ncrt.bright=" << crt.bright
      << "\ncrt.crispsights=" << (crt.crispSights ? 1 : 0) << "\ncrt.shotfiltered=" << (crt.shotFiltered ? 1 : 0)
      << "\nmusic=" << (music ? 1 : 0) << "\nmusicvolume=" << musicVolume << "\nsfxvolume=" << sfxVolume << "\nmusicnumbering=" << musicNumbering << "\nmenumusic=" << menuMusic
      << "\nmenustyle=" << menuStyle << "\nlivebg=" << liveBackground << "\nmenufps=" << menuFps << "\nmenublood=" << menuBlood << "\ngallery=" << galleryEntry << "\ngallerybg=" << galleryBg << "\ngallerylayout=" << galleryLayout;
    f << "\nscenecast=" << sceneCast;   // actors2: the cut-scene viewer's Actors
    f << "\nguntype=" << gunKind << "\ngunflash=" << gunFlash << "\ngunborder=" << gunBorder;   // padpass: light guns
    f << "\npadprompts=" << padPrompts << "\nbuttonstyle=" << buttonStyle << "\niconoutline=" << iconOutline;
    f << "\ntouchcursor=" << touchCursor << "\ngyrocursor=" << gyroCursor << "\ncursorspeed=" << cursorSpeed;   // padglyphs   // padglyphs: Button Prompts / Style
    for (int i = 0; i < 5; ++i)
        f << "\nsteamart" << i << '=' << steamArt[i];
    for (int s = 0; s < 4; ++s) {   // firing range: each player's crosshair (player 1 without a number)
        const std::string n = s ? std::to_string(s + 1) : std::string();
        const SeatSight& ss = sights[s];
        f << "\ncrosshair" << n << '=' << ss.style << "\ncrosshaircustom" << n << '=' << ss.custom << "\ncrosssize" << n << '='
          << ss.size << "\ncrossthick" << n << '=' << ss.thick << "\ncrosscolour" << n << '=' << ss.colour;
    }
    f << "\np1input=" << p1Input << "\np3input=" << p3Input << "\np4input=" << p4Input << "\np2bot=" << seatBot[1]
      << "\np3bot=" << seatBot[2] << "\np4bot=" << seatBot[3] << "\npadv=3";
    int nf = 0;
    const PadField* pf = padFields(nf);
    for (int p = 0; p < 4; ++p) {
        for (int i = 0; i < nf; ++i)
            if (pf[i].member)
                f << "\npad" << p + 1 << '.' << pf[i].key << '=' << pad[p].*pf[i].member;
        for (int s = 0; s < kPadStyles; ++s)
            for (int i = 0; i < nf; ++i)
                if (pf[i].styleMember && (pf[i].styleMask >> s & 1))
                    f << "\npad" << p + 1 << '.' << kPadStyleKeys[s] << '.' << pf[i].key << '=' << pad[p].s[s].*pf[i].styleMember;
    }
    for (int p = 0; p < 4; ++p) {
        char c[96];
        std::snprintf(c, sizeof c, "\ngun%dcal=%.4f,%.4f,%.4f,%.4f", p + 1, gunCal[p][0], gunCal[p][1], gunCal[p][2], gunCal[p][3]);
        f << c;
        if (!gunDev[p].empty())   // (ipolish: an assigned seat only)
            f << "\ngun" << p + 1 << "dev=" << gunDev[p] << "\ngun" << p + 1 << "name=" << gunName[p];
    }
    f
      << "\nchapter=" << chapter + 1;
    char rgb[16];
    std::snprintf(rgb, sizeof rgb, "%06X", unsigned(bloodRgb & 0xFFFFFF));
    f << "\nbloodrgb=" << rgb << "\nbloodv=2";
    for (int m = 0; m < 2; ++m) {   // texture packs (docs/mods.md)
        f << (m == 0 ? "\nmods=" : "\nmodsoff=");
        const std::vector<std::string>& l = m == 0 ? modsOn : modsOff;
        for (size_t i = 0; i < l.size(); ++i) f << (i ? "," : "") << l[i];
    }
    for (const auto& [k, v] : modSettings)   // the mods' settings (docs/modding.md)
        f << "\nmodset." << k << '=' << v;
    f << bindsIni(keys, padBinds);   // keybind (only the ones changed from the defaults)
    f << "\n";
}

bool Launcher::init(std::string& err)
{
    return page_.init(err);
}

namespace {

// setup: the 1998 PC CD's start window (THOTDRUN.EXE) was a plain Windows dialog; its look's colours, drawn by us
constexpr uint32_t kFace = 0xF0F0F0, kButton = 0xE1E1E1, kLight = 0xFFFFFF, kLite2 = 0xF4F4F4, kShadow = 0xA0A0A0, kDark = 0x404040,
                   kText = 0x000000, kGreyText = 0x505050, kGreyed = 0xA0A0A0, kRed = 0xB00000, kGreen = 0x107010,
                   kBlue = 0x000080;

// the dialog's painter: logical units (the dialog is 380 wide) x s = the picture's pixels; on false: the layout only
struct Painter {
    float s = 1;
    bool on = false;
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    const render::TtfText* tf = nullptr;   // data\fonts (lang::anyFont); none: SDL's 8 x 8 font

    int P(float u) const { return int(std::lround(u * s)); }
    int t() const { return std::max(1, int(std::lround(s * 0.75f))); }   // a bevel line's thickness
    void blend(int x, int y, uint32_t c, float a)
    {
        if (!on || x < 0 || y < 0 || x >= w || y >= h || a <= 0.0f) return;
        uint8_t* d = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
        a = std::min(a, 1.0f);
        for (int k = 0; k < 3; ++k)
            d[k] = uint8_t(float(d[k]) * (1 - a) + float((c >> (16 - 8 * k)) & 0xFF) * a + 0.5f);
        d[3] = uint8_t(std::max(float(d[3]), a * 255.0f));
    }
    void fillPx(int x0, int y0, int x1, int y1, uint32_t c)
    {
        if (!on) return;
        x0 = std::max(x0, 0), y0 = std::max(y0, 0), x1 = std::min(x1, w), y1 = std::min(y1, h);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                uint8_t* d = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
                d[0] = uint8_t(c >> 16), d[1] = uint8_t(c >> 8), d[2] = uint8_t(c), d[3] = 255;
            }
    }
    void fill(float x, float y, float fw, float fh, uint32_t c) { fillPx(P(x), P(y), P(x + fw), P(y + fh), c); }
    // a rectangle's outline, top-left and bottom-right colours (Windows' DrawEdge, one line)
    void edge(int x0, int y0, int x1, int y1, uint32_t tl, uint32_t br)
    {
        const int k = t();
        fillPx(x0, y0, x1, y0 + k, tl);
        fillPx(x0, y0, x0 + k, y1, tl);
        fillPx(x0, y1 - k, x1, y1, br);
        fillPx(x1 - k, y0, x1, y1, br);
    }
    void bevel(float x, float y, float bw, float bh, uint32_t o1, uint32_t o2, uint32_t i1, uint32_t i2)
    {
        const int x0 = P(x), y0 = P(y), x1 = P(x + bw), y1 = P(y + bh), k = t();
        edge(x0, y0, x1, y1, o1, o2);
        edge(x0 + k, y0 + k, x1 - k, y1 - k, i1, i2);
    }
    void raised(float x, float y, float bw, float bh) { bevel(x, y, bw, bh, kLight, kDark, kLite2, kShadow); }
    void pushed(float x, float y, float bw, float bh) { bevel(x, y, bw, bh, kDark, kLight, kShadow, kLite2); }
    void sunken(float x, float y, float bw, float bh) { bevel(x, y, bw, bh, kShadow, kLight, kDark, kLite2); }
    void frame(float x, float y, float bw, float bh) { bevel(x, y, bw, bh, kLite2, kDark, kLight, kShadow); }
    void etched(float x, float y, float bw, float bh)   // a group box's line
    {
        const int x0 = P(x), y0 = P(y), x1 = P(x + bw), y1 = P(y + bh), k = t();
        edge(x0 + k, y0 + k, x1, y1, kLight, kLight);
        edge(x0, y0, x1 - k, y1 - k, kShadow, kShadow);
    }
    int debugK(float cap) const { return std::max(1, int(std::lround(cap * s / 7.0f))); }
    float em(float cap) const { return lang::pxForCap(cap * s, tf); }
    float textW(const std::string& str, float cap) const   // logical units
    {
        if (tf) return tf->width(str, em(cap)) / s;
        return float(debugK(cap) * 8 * lang::columns(str)) / s;
    }
    // str with its baseline at (x, base)
    void text(const std::string& str, float x, float base, float cap, uint32_t c)
    {
        if (!on || str.empty()) return;
        if (tf) {
            tf->draw(str, em(cap), x * s, base * s, [&](int X, int Y, float a) { blend(X, Y, c, a); });
            return;
        }
        const int k = debugK(cap), n = int(str.size());
        SDL_Surface* sf = SDL_CreateSurface(8 * n, 8, SDL_PIXELFORMAT_RGBA32);
        if (!sf) return;
        if (SDL_Renderer* r = SDL_CreateSoftwareRenderer(sf)) {
            SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
            SDL_RenderClear(r);
            SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
            SDL_RenderDebugText(r, 0, 0, str.c_str());
            SDL_RenderPresent(r);
            SDL_DestroyRenderer(r);
        }
        const int x0 = P(x), y0 = P(base) - 7 * k;
        for (int yy = 0; yy < 8; ++yy)
            for (int xx = 0; xx < 8 * n; ++xx)
                if (static_cast<const uint8_t*>(sf->pixels)[size_t(yy) * size_t(sf->pitch) + size_t(xx) * 4 + 3] > 127)
                    fillPx(x0 + xx * k, y0 + yy * k, x0 + (xx + 1) * k, y0 + (yy + 1) * k, c);
        SDL_DestroySurface(sf);
    }
    // an RGBA picture shrunk into (x, y, dw, dh) (a box filter: the wordmark is 3x art)
    void image(const std::vector<uint8_t>& img, int iw, int ih, float x, float y, float dw, float dh)
    {
        if (!on || iw <= 0 || ih <= 0) return;
        const int X0 = P(x), Y0 = P(y), W = std::max(1, P(dw)), H = std::max(1, P(dh));
        for (int yy = 0; yy < H; ++yy)
            for (int xx = 0; xx < W; ++xx) {
                const int sx0 = xx * iw / W, sx1 = std::max(sx0 + 1, (xx + 1) * iw / W);
                const int sy0 = yy * ih / H, sy1 = std::max(sy0 + 1, (yy + 1) * ih / H);
                float rgb[3] = {}, a = 0;
                for (int sy = sy0; sy < sy1 && sy < ih; ++sy)
                    for (int sx = sx0; sx < sx1 && sx < iw; ++sx) {
                        const uint8_t* q = &img[(size_t(sy) * size_t(iw) + size_t(sx)) * 4];
                        const float qa = float(q[3]) / 255.0f;
                        for (int k = 0; k < 3; ++k) rgb[k] += float(q[k]) * qa;
                        a += qa;
                    }
                const float n = float((sx1 - sx0) * (sy1 - sy0));
                if (a <= 0.0f) continue;
                const uint32_t c = uint32_t(rgb[0] / a) << 16 | uint32_t(rgb[1] / a) << 8 | uint32_t(rgb[2] / a);
                blend(X0 + xx, Y0 + yy, c, a / n);
            }
    }
};

} // namespace

void Launcher::render(int ww, int wh)
{
    if (!wordTried_) {
        wordTried_ = true;
        std::string err;
        if (wordmarkFile.empty() || !readPngRgba(wordmarkFile, wordW_, wordH_, word_, err) || wordW_ <= 0 || wordH_ <= 0)
            wordW_ = wordH_ = 0;
    }
    std::vector<ExtraLine> rows;
    if (extraLines && (!extraWhen || extraWhen()))
        rows = extraLines();
    // Play is greyed out until the arcade ROMs are chosen (and, once checked, passed)
    const bool canPlay = checked && arcadeOk_ && pcOk && !checkRequested_;   // (greyed until both pass, user 2026-10-31)
    // what the picture shows: drawn again only when it changes
    std::string key = std::to_string(ww) + 'x' + std::to_string(wh) + '|' + cfg.arcade + '|' + cfg.pc + '|' +
                      std::to_string(pressed_) + '|' + std::to_string(lang::gen()) + '|' + extraTitle + (canPlay ? "+" : "-") + (windowed ? "w" : "");
    for (const std::string& s : status) key += '\n' + s;
    for (const std::string& s : notes) key += '\r' + s;
    for (const ExtraLine& r : rows) key += '\t' + r.label + '\v' + r.value + (r.clickable ? '1' : '0');
    if (key == drawnKey_) {
        page_.drawAnchored(ww, wh, drawX_, drawY_, 0.0f, 0.0f);
        return;
    }
    drawnKey_ = key;

    auto T = [](const char* s) { return lang::tr(s); };
    // THOTDRUN.EXE's AUTORUNDIALOG (177 x 220 dialog units): a picture at the top (15, 10, 149 x 98), then a column of
    // full-width buttons (15, 119 / 135 / 156 / 172, 149 x 14), a gap, "Exit to Windows" (194). Ours: the same column,
    // a little wider for the files' names; 1 unit = 1 pixel at scale 1
    constexpr float W = 300, M = 16, BW = W - 2 * M, BH = 24;
    float height = 0;                  // (the last pass's: the frame)
    std::vector<Hot> hots;             // (canvas pixels)
    auto paint = [&](Painter& p) -> float {
        auto hotAt = [&](float x, float y, float bw, float bh, int action) {
            if (p.on) hots.push_back({p.P(x), p.P(y), p.P(x + bw), p.P(y + bh), action});
        };
        auto fitText = [&](const std::string& s, float maxW, float cap, bool tail) {
            if (p.textW(s, cap) <= maxW) return s;
            for (size_t n = s.size(); n > 4; --n) {
                const std::string cut = tail ? "..." + util::utf8Tail(s, n) : util::utf8Head(s, n) + "...";
                if (cut.size() < s.size() + 3 && p.textW(cut, cap) <= maxW) return cut;
            }
            return std::string("...");
        };
        // a classic push button (raised; pushed in while held; greyed text: not available)
        auto button = [&](const std::string& label, float x, float y, float bw, float bh, int action, bool enabled) {
            p.fill(x, y, bw, bh, kButton);
            const bool down = enabled && pressed_ == action;
            if (down) p.pushed(x, y, bw, bh);
            else p.raised(x, y, bw, bh);
            const float cap = 7.0f, off = down ? 1.0f : 0.0f;
            const std::string shown = fitText(label, bw - 12, cap, false);
            p.text(shown, x + (bw - p.textW(shown, cap)) / 2 + off, y + bh / 2 + cap / 2 + off, cap, enabled ? kText : kGreyed);
            if (enabled) hotAt(x, y, bw, bh, action);
        };
        auto fileName = [](const std::string& path) {
            std::string n = std::filesystem::path(path).filename().string();
            return n.empty() ? path : n;
        };
        // the window: its frame, the title bar (the reference's: light, the title, an X at the right). windowed: the
        // setup's own small window has the system's title bar and X: the body only
        p.fill(0, 0, W, height, kFace);
        if (!windowed) p.frame(0, 0, W, height);
        const float tb = windowed ? 0.0f : 3.0f, th = windowed ? 0.0f : 26.0f;
        if (!windowed) {
            p.fill(tb, tb, W - 2 * tb, th, kLight);
            p.text(T("The House of the Dead: ReWrithe Setup"), tb + 9, tb + th / 2 + 4, 7.5f, kText);
        }
        if (!windowed) {   // the close box (= Exit)
            const float cb = 26, cx = W - tb - cb, cy = tb;
            if (pressed_ == kQuit) p.fill(cx, cy, cb, th, 0xE81123);
            const int k = std::max(1, p.t()), n = p.P(9), x0 = p.P(cx + cb / 2) - n / 2, y0 = p.P(cy + th / 2) - n / 2;
            const uint32_t c = pressed_ == kQuit ? kLight : kText;
            for (int i = 0; i < n; ++i) {
                p.fillPx(x0 + i, y0 + i, x0 + i + k, y0 + i + k, c);
                p.fillPx(x0 + n - 1 - i, y0 + i, x0 + n - 1 - i + k, y0 + i + k, c);
            }
            hotAt(cx, cy, cb, th, kQuit);
        }
        // the picture (the original: the logo over a zombie; ours: the title and our wordmark on a dark ground)
        float y = tb + th + 12;
        const float ph = 118;
        p.sunken(M, y, BW, ph);
        {
            const float k = float(2 * p.t()) / p.s;
            p.fill(M + k, y + k, BW - 2 * k, ph - 2 * k, 0x120505);
        }
        {
            const std::string t = "THE HOUSE OF THE DEAD";
            p.text(t, M + (BW - p.textW(t, 9.0f)) / 2, y + 34, 9.0f, 0xC82020);
        }
        if (wordW_ > 0) {
            const float dw = BW - 30, dh = dw * float(wordH_) / float(wordW_);
            p.image(word_, wordW_, wordH_, M + 15, y + 42, dw, dh);
        } else {
            p.text("REWRITHE", M + (BW - p.textW("REWRITHE", 16.0f)) / 2, y + 80, 16.0f, 0xE8E0D0);
        }
        {
            const std::string t = T("Unofficial. Plays from your own arcade ROMs and 1998 PC game.");
            const std::string s = fitText(t, BW - 16, 5.5f, false);
            p.text(s, M + (BW - p.textW(s, 5.5f)) / 2, y + ph - 12, 5.5f, 0xA89E90);
        }
        y += ph + 12;
        // the files (user 2026-10-31): each a label with the chosen name, two equal buttons (a zip or a folder, either
        // works for both), a small line on what it is
        const float half = (BW - 6) / 2;
        auto fileRow = [&](const std::string& label, const std::string& path, int aZip, int aFolder, const std::string& note) {
            const std::string name = path.empty() ? T("(not chosen)") : fileName(path);
            p.text(label, M, y + 9, 7.0f, kText);
            p.text(fitText(name, BW - p.textW(label, 7.0f) - 6, 7.0f, true), M + p.textW(label, 7.0f) + 6, y + 9, 7.0f,
                   path.empty() ? kGreyed : kBlue);
            y += 14;
            button(T("Choose Zip..."), M, y, half, BH - 2, aZip, true);
            button(T("Choose Folder..."), M + half + 6, y, half, BH - 2, aFolder, true);
            y += BH + 1;
            for (const std::string& r : lang::wrapBy(note, BW, [&](const std::string& x) { return p.textW(x, 5.5f); })) {
                p.text(r, M, y + 7, 5.5f, kGreyText);
                y += 10;
            }
            y += 8;
        };
        fileRow(T("Arcade ROMs:"), cfg.arcade, kArcadeZip, kArcadeFolder,
                T("Sega Model 2 arcade, Revision A (MAME set \"hotd\"). Any file name."));
        fileRow(T("PC Game:"), cfg.pc, kPcZip, kPcFolder, T("The 1998 PC version: its installed folder or a zip of it. Any name. Its CD music is optional (the arcade's is used)."));
        button(T("Check Files"), M, y, BW, BH, kCheck, !cfg.arcade.empty() || !cfg.pc.empty());
        y += BH + 6;
        button(T("Play The House of the Dead: ReWrithe"), M, y, BW, BH + 6, kStart, canPlay);
        y += BH + 6 + 10;
        // the check's result: OK, or CANNOT START and what to do
        {
            const float cap = 6.0f, lineH = 11;
            std::vector<std::pair<std::string, uint32_t>> lines;
            auto add = [&](const std::string& s, uint32_t c) {
                const std::string shown = lang::active() ? lang::tr(s) : s;
                for (const std::string& r : lang::wrapBy(shown, BW, [&](const std::string& x) { return p.textW(x, cap); }))
                    lines.push_back({r, c});
            };
            for (const std::string& s : status) {
                const size_t lead = s.find_first_not_of(' ');
                const std::string t = lead == std::string::npos ? std::string() : s.substr(lead);
                const uint32_t c = t.rfind("CANNOT START", 0) == 0 || t.rfind("Cannot start", 0) == 0 ? kRed
                                   : t.rfind("->", 0) == 0                                               ? kBlue
                                   : t.rfind("OK", 0) == 0                                               ? kGreen
                                                                                                         : kText;
                add(t, c);
            }
            if (status.empty())
                add(T("Choose both, then Check Files."), kText);
            for (const std::string& s : notes)
                add(s, kGreyText);
            for (const auto& [l, c] : lines) {
                p.text(l, M, y + 8, cap, c);
                y += lineH;
            }
            y += 14;   // (the original's gap before Exit)
        }
        if (false && !rows.empty()) {   // online co-op: not in the setup (user 2026-10-31: both files are needed; online is in the menu)
            p.text(lang::tr(extraTitle), M, y + 9, 7.0f, kText);
            y += 16;
            for (size_t i = 0; i < rows.size(); ++i) {
                const ExtraLine& r = rows[i];
                if (r.clickable)
                    button(r.label, M, y, 150, BH - 4, kExtraBase + int(i), true);
                else
                    p.text(r.label, M, y + 15, 7.0f, kText);
                if (!r.value.empty())
                    p.text(fitText(r.value, BW - 160, 6.5f, false), M + 160, y + 15, 6.5f, kGreyText);
                y += BH;
            }
            y += 8;
        }
        button(T("Exit to Windows"), M, y, BW, BH, kQuit, true);
        return y + BH + 16;
    };
    Painter p;
    p.tf = lang::anyFont();
    const float h1 = paint(p);   // (s = 1: the height, then the scale that fits the window)
    float s = std::min({(float(ww) - 16.0f) / W, float(wh) * 0.96f / h1, float(ww) * 0.6f / W});
    if (windowed)   // the setup's own window: the body fills its width (the host sizes it to wantW x wantH x the DPI scale)
        s = float(ww) / W;
    s = std::clamp(s, 0.5f, 4.0f);
    wantW = W;

    p.s = s;
    height = paint(p);           // (at that scale: the text wraps the same, mostly)
    wantH = height;
    hots.clear();
    p.w = int(std::ceil(W * s)) + 1;
    p.h = int(std::ceil(height * s)) + 1;
    p.px.assign(size_t(p.w) * size_t(p.h) * 4, 0);
    p.on = true;
    paint(p);
    drawX_ = std::floor((float(ww) - float(p.w)) / 2.0f);
    drawY_ = std::floor(std::max(0.0f, (float(wh) - float(p.h)) / 2.0f));
    hot_.clear();
    for (const Hot& h : hots)
        hot_.push_back({h.x0 + int(drawX_), h.y0 + int(drawY_), h.x1 + int(drawX_), h.y1 + int(drawY_), h.action});
    page_.setImage(p.px, p.w, p.h);
    page_.drawAnchored(ww, wh, drawX_, drawY_, 0.0f, 0.0f);
}

void Launcher::click(int action, SDL_Window* window)
{
    if (action >= kExtraBase) {   // an extra row (online co-op)
        if (extraClick) extraClick(action - kExtraBase);
        return;
    }
    switch (action) {
    case kArcadeFolder:
    case kArcadeZip:
    case kPcFolder:
    case kPcZip: {
        dialogTarget_ = action == kPcFolder || action == kPcZip ? 2 : 1;
        auto cb = [](void* self, const char* const* files, int) {
            Launcher* l = static_cast<Launcher*>(self);
            std::lock_guard<std::mutex> lock(l->dialogMutex_);
            l->dialogResult_ = files && files[0] ? files[0] : "";
        };
        const std::string& cur = action == kPcFolder || action == kPcZip ? cfg.pc : cfg.arcade;
        const char* start = cur.empty() ? nullptr : cur.c_str();
        if (action == kArcadeZip || action == kPcZip) {   // pczip: the PC game may stay zipped too
            static const SDL_DialogFileFilter filter[] = {{"Zip archives", "zip"}};
            SDL_ShowOpenFileDialog(cb, this, window, filter, 1, start, false);
        } else {
            SDL_ShowOpenFolderDialog(cb, this, window, start, false);
        }
        break;
    }
    case kCheck: checkRequested_ = true; startAfterCheck_ = false; status = {"Checking the files ..."}; break;
    case kStart: checkRequested_ = true; startAfterCheck_ = true; status = {"Checking the files ..."}; break;
    case kQuit: pending_ = Result::Quit; break;
    default: break;
    }
    cfg.save(cacheDir + "launcher.ini");
}

void Launcher::handle(const SDL_Event& e, SDL_Window* window)
{
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        if (e.key.key == SDLK_ESCAPE)
            pending_ = Result::Quit;
        else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)
            click(kStart, window);
        return;
    }
    const bool down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN, up = e.type == SDL_EVENT_MOUSE_BUTTON_UP;
    if ((!down && !up) || e.button.button != SDL_BUTTON_LEFT)
        return;
    int lw, lh, pw, ph;
    SDL_GetWindowSize(window, &lw, &lh);
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    const int mx = int(e.button.x * float(pw) / float(std::max(lw, 1))), my = int(e.button.y * float(ph) / float(std::max(lh, 1)));
    int at = -1;
    for (const Hot& h : hot_)
        if (mx >= h.x0 && mx < h.x1 && my >= h.y0 && my < h.y1)
            at = h.action;
    // as a Windows button: pushed in while held, done on the release over the same button
    if (down) {
        pressed_ = at;
        return;
    }
    const int was = pressed_;
    pressed_ = -1;
    if (at >= 0 && at == was)
        click(at, window);
}

bool Launcher::runCheck()
{
    status.clear();
    checked = true;
    auto progress = [](const std::string& s) { SDL_Log("[files] %s", s.c_str()); };
    CheckReport a = checkArcade(cfg.arcade, cacheDir + "arcade_regions", false, progress);
    CheckReport p = checkPc(cfg.pc, cacheDir + "pc_files.tsv", pcFiles, progress);
    for (const std::string& s : a.lines)
        SDL_Log("[files] %s", s.c_str());
    for (const std::string& s : p.lines)
        SDL_Log("[files] %s", s.c_str());
    pcOk = p.ok;
    arcadeOk_ = a.ok;
    // setup (user 2026-10-31): one short result: OK, or CANNOT START and what to do (the files' list: hotd.log)
    if (a.ok && p.ok)
        status.push_back("OK: the arcade ROMs (Revision A) and the PC game are ready.");
    else if (a.ok)
        status.push_back("CANNOT START: the PC game did not pass (the game needs both the arcade ROMs and the 1998 PC game).");
    else
        status.push_back("CANNOT START: the arcade ROMs did not pass.");
    auto show = [&](const CheckReport& r, bool arcade) {
        if (r.ok) return;
        bool wrong = false;
        int named = 0;   // relfix: the first few wrong / missing files by name (all of them: hotd.log)
        for (const std::string& s : r.lines) {
            const bool bad = s.rfind("MISSING", 0) == 0 || s.rfind("WRONG", 0) == 0;
            const bool d = bad || s.rfind("moved", 0) == 0;
            wrong |= s.rfind("WRONG", 0) == 0;
            if (!d)
                status.push_back(s);
            else if (bad && named++ < 3)   // "WRONG   THOTD.EXE" / "MISSING SOUND/X.RBH": the name, not the hashes
                status.push_back(s.substr(0, std::min(s.find(':'), s.find(" (1998"))));
        }
        if (arcade && wrong)   // (files of the set's names, other contents: another revision or a bad dump)
            status.push_back("-> These are not Revision A ROMs: the game needs MAME's \"hotd\" set (Revision A); the original release (hotdo) and prototypes do not work.");
        else if (const std::string h = placementHint(arcade ? cfg.arcade : cfg.pc, arcade); !h.empty())
            status.push_back("-> " + h);
    };
    show(a, true);
    show(p, false);
    return a.ok && p.ok;
}

Launcher::Result Launcher::frame(int ww, int wh)
{
    {
        std::lock_guard<std::mutex> lock(dialogMutex_);
        if (!dialogResult_.empty()) {
            (dialogTarget_ == 2 ? cfg.pc : cfg.arcade) = dialogResult_;
            dialogResult_.clear();
            cfg.save(cacheDir + "launcher.ini");
            // a new choice: checked at once (Play stays grey until the check passes)
            checkRequested_ = true, startAfterCheck_ = false;
            status = {"Checking the files ..."};
        }
    }
    static bool drawnChecking = false;
    if (checkRequested_ && drawnChecking) {
        checkRequested_ = drawnChecking = false;
        if (runCheck() && startAfterCheck_)
            pending_ = Result::Start;
        else
            quiet = false;   // (remakefix: the start-up's check failed: the page with its report)
    } else if (checkRequested_) {
        drawnChecking = true;   // one frame with "Checking ..." on screen first
    }
    if (quiet) {   // remakefix: the start-up's own check: nothing drawn (the host clears to black)
        const Result r = pending_;
        pending_ = Result::None;
        return r;
    }
    render(ww, wh);
    const Result r = pending_;
    pending_ = Result::None;
    return r;
}

} // namespace hotd::game
