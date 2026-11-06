#include "game/launcher.h"

#include "assets/archive.h"
#include "game/chapters.h"
#include "game/files_check.h"
#include "game/installer.h"
#include "game/lang.h"
#include "game/lives.h"
#include "game/sights.h"
#include "render/gamma.h"
#include "util/disc_image.h"
#include "util/file64.h"
#include "util/fullscreen.h"
#include "util/resolution.h"
#include "util/png.h"
#include "util/png_read.h"
#include "util/utf8.h"
#include "util/win_shell.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <tuple>

namespace hotd::game {

namespace {


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
    static const char* const kVert[] = {"50%", "60%", "70%", "80%", "90%", "100%", "110%", "120%", "130%", "140%", "150%"};
    static const char* const kInvert[] = {"Off", "X", "Y", "X + Y"};
    static const char* const kLevel[] = {"Off", "Light", "Medium", "Strong"};
    static const char* const kSmooth[] = {"Off", "Low", "Medium", "High"};
    static const char* const kStick[] = {"Default", "Swapped"};
    static const char* const kShoot[] = {"Auto", "R2", "L2", "R1", "L1", "South (A / Cross)"};
    static const char* const kSpace[] = {"Player", "World", "Local"};
    static const char* const kTouch[] = {"Auto", "Off", "Pointer", "Trackpad"};
    static const char* const kClick[] = {"Reload", "Shoot"};
    static const char* const kTrig[] = {"5%", "10%", "15%", "20%", "25%", "30%", "35%", "40%", "45%"};
    constexpr unsigned C = 1, M = 2, X = 4, T = 8, All = 15;
    static const PadField k[] = {
        {"style", "Style", nullptr, &PadProfile::style, 0, kPadStyles, kPadStyleNames, 0},
        // sens36 (GitHub #36): each input its own whole speed (was: "speed" Sensitivity 1-10 for all of them, "fine"
        // Fine Aim Speed x 10 % of it, the player's aimsens / sticksens / gyrosens / touchsens on top)
        {"aimstick", "Stick Sensitivity", &PadStyle::stickSens, nullptr, kPadSensLo, kPadSensHi, nullptr, C, kPadSensStep},
        {"aimfine", "Fine Stick Sensitivity", &PadStyle::fineSens, nullptr, kPadSensLo, kPadSensHi, nullptr, C | X, kPadSensStep},
        {"aimgyro", "Gyro Sensitivity", &PadStyle::gyroSens, nullptr, kPadSensLo, kPadSensHi, nullptr, M | X, kPadSensStep},
        {"aimtouch", "Trackpad Sensitivity", &PadStyle::touchSens, nullptr, kPadSensLo, kPadSensHi, nullptr, All, kPadSensStep},
        {"vert", "Vertical", &PadStyle::vert, nullptr, 5, 15, kVert, All},
        {"invert", "Invert", &PadStyle::invert, nullptr, 0, 3, kInvert, All},
        {"stick", "Sticks", &PadStyle::stick, nullptr, 0, 1, kStick, C | X},
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

std::string padFieldText(const PadField& f, int v)
{
    v = std::clamp(v, f.lo, f.hi);
    if (f.names)
        return f.names[v - f.lo];
    return std::to_string(v) + (f.step > 1 ? "%" : "");
}

int padFieldNext(const PadField& f, int v, int dir)
{
    const int st = std::max(f.step, 1);
    if (dir > 0)   // (a value between the steps, a converted older one: to the next step)
        return v >= f.hi ? f.lo : std::min(f.hi, (v / st + 1) * st);
    return v <= f.lo ? f.hi : std::max(f.lo, ((v + st - 1) / st - 1) * st);
}

// sens36: the old aim's tables (pads.cpp before #36): Sensitivity 1-10 = the stick's top speed (screen heights / s)
// and the gyro's factor; the trackpad's x Sensitivity / 5 in the Touchpad style only; Fine Aim Speed x 10 % of the
// stick's (Classic: its Sensitivity's, Motion + stick: Sensitivity 5's)
PadSpeeds legacyPadSpeeds(int style, int speed, int fine, int aim, int stick, int gyro, int touch)
{
    static const float kTop[10] = {1.1f, 1.3f, 1.6f, 2.0f, 2.4f, 2.9f, 3.5f, 4.1f, 4.8f, 5.8f};
    static const float kGyro[10] = {0.5f, 0.6f, 0.7f, 0.85f, 1.0f, 1.2f, 1.4f, 1.7f, 2.0f, 2.5f};
    const int sp = std::clamp(speed, 1, 10);
    const float a = float(std::clamp(aim, 5, 20)) / 10.0f, fn = float(std::clamp(fine, 1, 10)) / 10.0f;
    const float sk = float(std::clamp(stick, 5, 20)) / 10.0f, gk = float(std::clamp(gyro, 5, 20)) / 10.0f;
    const float tk = float(std::clamp(touch, 5, 20)) / 10.0f;
    auto pct = [](float f) { return std::clamp(int(std::lround(f * 100.0f)), kPadSensLo, kPadSensHi); };
    PadSpeeds r;
    r.stick = pct(kTop[sp - 1] / 2.4f * sk * a);
    r.fine = pct((style == 1 ? kTop[sp - 1] : 2.4f) * fn / 0.96f * sk * a);
    r.gyro = pct(kGyro[sp - 1] * gk * a);
    r.touch = pct((style == 4 ? float(sp) / 5.0f : 1.0f) * tk * a);
    return r;
}

// the presets' feel values (the rest of a style, its buttons / invert / touchpad / shake, is kept). sens36: the speeds
// as before (Sensitivity / Fine Aim Speed: legacyPadSpeeds), so an older file's preset converts to the same preset
namespace {
struct PadFeel { int speed, fine, dead, curve, turbo, smooth, tighten, gaccel; };
constexpr PadFeel kPadFeel[kPadPresets] = {
    {4, 3, 2, 3, 0, 2, 1, 0},   // Precise: 0.85x gyro / a slower stick, curve 2.5, smoothing to 6 deg/s, tightening 1
    {5, 4, 2, 2, 1, 1, 0, 0},   // Balanced: the defaults (PadStyle)
    {7, 5, 2, 1, 2, 1, 0, 1},   // Fast: 1.4x gyro / a faster stick, curve 1.5, the high turbo, low acceleration
};
// the speeds a preset sets in a style 1-4 (bits: stick, fine, gyro, touch): only the inputs that style aims with
unsigned presetSpeeds(int style)
{
    return style == 1 ? 1 | 2 : style == 2 ? 4 : style == 3 ? 2 | 4 : 8;
}
} // namespace

void applyPadPreset(PadStyle& s, int preset, int style)
{
    if (preset < 1 || preset > kPadPresets)
        return;
    const PadFeel& f = kPadFeel[preset - 1];
    const PadSpeeds v = legacyPadSpeeds(style, f.speed, f.fine);
    const unsigned m = presetSpeeds(style);
    if (m & 1) s.stickSens = v.stick;
    if (m & 2) s.fineSens = v.fine;
    if (m & 4) s.gyroSens = v.gyro;
    if (m & 8) s.touchSens = v.touch;
    s.dead = f.dead, s.curve = f.curve, s.turbo = f.turbo, s.smooth = f.smooth;
    s.tighten = f.tighten, s.gaccel = f.gaccel;
}

int padPresetOf(const PadStyle& s, int style)
{
    const unsigned m = presetSpeeds(style);
    for (int i = 0; i < kPadPresets; ++i) {
        const PadFeel& f = kPadFeel[i];
        const PadSpeeds v = legacyPadSpeeds(style, f.speed, f.fine);
        if (((m & 1) && s.stickSens != v.stick) || ((m & 2) && s.fineSens != v.fine) || ((m & 4) && s.gyroSens != v.gyro) ||
            ((m & 8) && s.touchSens != v.touch))
            continue;
        if (s.dead == f.dead && s.curve == f.curve && s.turbo == f.turbo && s.smooth == f.smooth && s.tighten == f.tighten &&
            s.gaccel == f.gaccel)
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
                            // padv=4 (sens36): each input its own speed (older: converted)
    int padStyleRaw[4] = {-1, -1, -1, -1};   // relpad: pad1.style= as written (padv < 3: 4 was Sideways, 5 Touchpad)
    bool enemy4pSeen = false;   // enemy4p= (cpuai 2026-10-26) not there yet: boss4p= at the old default 25 becomes the new 50
    bool sightSeen[4] = {};     // firing range: a player's crosshair keys there (else player 1's)
    int displayModeSeen = -1, exclusiveSeen = -1;   // rescrash: displaymode= / exclusive= (Video > Display Mode)
    int startLivesSeen = 0, maxLivesSeen = 0;   // GitHub #30: startlives= / maxlives= (an older file: lives= alone)
    // GitHub #36: the menu cursor's cursorspeed= / gyrocursorspeed= (1-10, before sens36; an older file still: the one
    // Cursor Speed's for both) and touchcursorsens= / gyrocursorsens= (%, sens36)
    int cursorSpeedSeen = 0, gyroSpeedSeen = 0, touchCursorSensSeen = 0, gyroCursorSensSeen = 0;
    // sens36: the pad's older speed settings (padv < 4), converted after the loop: per style Sensitivity speed= and
    // Fine Aim Speed fine=, the player's aimsens= / sticksens= / gyrosens= / touchsens=; a pad with any of the new
    // pad1.<style>.aimstick= .. aimtouch= lines is not converted
    struct OldPad { int speed[kPadStyles] = {5, 5, 5, 5}, fine[kPadStyles] = {4, 4, 4, 4}; int aim = 10, stick = 10, gyro = 10, touch = 10; bool fresh = false; };
    OldPad oldPad[4];
    while (std::getline(f, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        const int n = std::atoi(v.c_str());
        // Light Gun Edition: its own keys (their own chain: MSVC's limit on one else-if chain, C1061)
        auto funKey = [&]() {
            if (k == "flashlight") flashlight = n != 0;
            else if (k == "bigheads") bigHeads = n != 0;
            else if (k == "shotgun") shotgun = n != 0;
            else if (k == "partyboard") partyBoard = n != 0;
            else if (k == "hardcore") hardcore = n != 0;
            else if (k == "freefire") freeFire = n != 0;
            else if (k == "machinegun") machineGun = n != 0;
            else if (k == "suddendeath") suddenDeath = n != 0;
            else if (k == "vampire") vampire = n != 0;
            else if (k == "hostagecrisis") hostageCrisis = n != 0;
            else if (k == "roulette") roulette = n != 0;
            else if (k == "survival") survival = n != 0;
            else if (k == "exploding") exploding = n != 0;
            else if (k == "funlook") funLook = std::clamp(n, 0, 3);
            else if (k == "turbo") turbo = std::clamp(n, 0, 2);
            else if (k == "bossrush") bossRush = n != 0;
            else if (k == "married") married = n != 0;
            else if (k == "arsenal") weaponSwitch = n != 0;
            else if (k == "crosshairs") crosshairs = n != 0;   // (before the crosshair keys of the chain below)
            else if (k == "lgupdatecheck") updateCheck = n != 0;
            else if (k == "outputs") outputs = n != 0;
            else if (k == "outputsport") outputsPort = n > 0 && n < 65536 ? n : 8000;
            else return false;
            return true;
        };
        // gunfb: Show Crosshairs / Shot Flash Size (their own ifs: the chain below is at MSVC's nesting limit)
        if (k == "showcrosshairs") { showCrosshairs = n != 0; continue; }
        if (k == "shotflashsize") { shotFlashSize = std::clamp(n, 0, 2); continue; }
        if (bindsIniLine(k, v, keys, padBinds)) {}   // keybind: key1.fire= / padbind1.fire= ...
        else if (funKey()) {}
        else if (k == "arcade") arcade = fromBase(v, base);
        else if (k == "pc") pc = fromBase(v, base);
        else if (k == "models") models = std::clamp(n, 0, 1);
        else if (k == "sound") sound = std::clamp(n, 0, 2);
        else if (k == "players") players = std::clamp(n, 1, 4);   // 3 / 4: the 4P mode (docs/coop4.md)
        else if (k == "p3input") p3Input = n >= 0 && n <= 3 ? n : 2;   // 0 mouse, 1 keyboard, 2 pad, 3 gun
        else if (k == "p2bot" || k == "p3bot" || k == "p4bot") seatBot[k[1] - '1'] = std::clamp(n, 0, 4);
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
            OldPad& op = oldPad[k[3] - '1'];   // (sens36: the older speeds)
            if (st >= 0 && rest == "speed") op.speed[st] = std::clamp(n, 1, 10);
            else if (st >= 0 && rest == "fine") op.fine[st] = std::clamp(n, 1, 10);
            else if (st < 0 && rest == "aimsens") op.aim = std::clamp(n, 5, 20);
            else if (st < 0 && rest == "sticksens") op.stick = std::clamp(n, 5, 20);
            else if (st < 0 && rest == "gyrosens") op.gyro = std::clamp(n, 5, 20);
            else if (st < 0 && rest == "touchsens") op.touch = std::clamp(n, 5, 20);
            else if (st >= 0 && rest.rfind("aim", 0) == 0) op.fresh = true;
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
        else if (k == "lives") lives = livesCodeValid(n) ? n : 9;   // (game/lives.h: the arcade's setting 0-11 or a pair's code)
        else if (k == "startlives") startLivesSeen = n;
        else if (k == "maxlives") maxLivesSeen = n;
        else if (k == "soloseat") soloSeat = n == 1 ? 1 : 0;   // (#29)
        else if (k == "chapterretry") chapterRetry = n != 0;   // (#5)
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
        else if (k == "widen2d") widen2d = std::clamp(n, 0, 1);
        else if (k == "fullscreen") fullscreen = n != 0;
        else if (k == "displaymode") displayModeSeen = std::clamp(n, 0, 2);
        else if (k == "exclusive") exclusiveSeen = n != 0;
        else if (k == "resolution_fullscreen") { const util::Size s = util::parseSize(v); resFullW = s.w, resFullH = s.h; }
        else if (k == "resolution_window") { const util::Size s = util::parseSize(v); resWinW = s.w, resWinH = s.h; }
        else if (k == "autoreload") autoReload = n != 0;
        else if (k == "edgereload") edgeReload = n != 0;
        else if (k == "scoredisplay") scoreDisplay = n != 0;
        else if (k == "attractgore") attractGore = n != 0;
        else if (k == "lastversion") lastVersion = util::utf8Head(v, 24);
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
        else if (k == "cursorspeed") cursorSpeedSeen = std::clamp(n, 1, 10);
        else if (k == "gyrocursorspeed") gyroSpeedSeen = std::clamp(n, 1, 10);   // (#36)
        else if (k == "touchcursorsens") touchCursorSensSeen = std::clamp(n, kCursorSensLo, kCursorSensHi);   // (sens36)
        else if (k == "gyrocursorsens") gyroCursorSensSeen = std::clamp(n, kCursorSensLo, kCursorSensHi);
        else if (k.size() == 10 && k.rfind("mousesens", 0) == 0 && (k[9] == '1' || k[9] == '2')) mouseSens[k[9] - '1'] = std::clamp(n, 5, 20);
        else if (k == "gunborder") gunBorder = std::clamp(n, 0, 4); // padpass: the white border
        else if (k == "trailer") trailer = n != 0;
        else if (k == "hitsparks") hitSparks = n != 0;
        else if (k == "muzzleflash") muzzleFlash = n != 0;
        else if (k == "bloodparticles") bloodParticles = n != 0;
        else if (k == "hitmarks") clearMarks = n != 0;
        else if (k == "transparency") transparency = std::clamp(n, 0, 3);
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
        else if (k == "gamma") { const int g = gammaParse(v.c_str()); if (g > 0) gamma = g; }
        else if (k == "menugamma") { const int g = gammaParse(v.c_str()); if (g > 0) menuGamma = g; }   // (polish)
        else if (k == "blacklevel") blackLevel = std::clamp(n, int(kBlackMin), int(kBlackMax));   // (bugs4, #23)
        else if (k == "vrr") vrr = n != 0;   // (bugs4, #20)
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
    // (#36, sens36) the menu cursor: the % lines, else the older 1-10 (x 20 %: 5 = 100 %; no gyrocursorspeed=: the one
    // Cursor Speed's)
    {
        const int oldTouch = cursorSpeedSeen > 0 ? cursorSpeedSeen : 5, oldGyro = gyroSpeedSeen > 0 ? gyroSpeedSeen : oldTouch;
        touchCursorSens = touchCursorSensSeen > 0 ? touchCursorSensSeen : std::clamp(oldTouch * 20, kCursorSensLo, kCursorSensHi);
        gyroCursorSens = gyroCursorSensSeen > 0 ? gyroCursorSensSeen : std::clamp(oldGyro * 20, kCursorSensLo, kCursorSensHi);
    }
    if (startLivesSeen > 0 || maxLivesSeen > 0)   // GitHub #30: the two settings over lives= (the same pair when both are there)
        lives = livesCode(startLivesSeen > 0 ? startLivesSeen : livesStartOf(lives), maxLivesSeen > 0 ? maxLivesSeen : livesMostOf(lives));
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
        for (int p = 0; p < 4; ++p)
            for (int s = 0; s < kPadStyles; ++s) {
                const PadStyle def;
                PadStyle& st = pad[p].s[s];
                oldPad[p].speed[s] = 5, oldPad[p].fine[s] = 4;
                st.dead = def.dead, st.curve = def.curve, st.smooth = def.smooth;
                st.space = def.space;
            }
    // sens36 (GitHub #36): padv < 4 had one Sensitivity (and Fine Aim Speed) per style and the player's Aim / Stick /
    // Gyro / Trackpad Sensitivity multiplied on top: each input's speed now its own %, the same speed as before
    // (rounded to 1 %, kPadSensLo..Hi). The older keys are not written again (an older build reads its defaults)
    if (padV < 4)
        for (int p = 0; p < 4; ++p) {
            const OldPad& op = oldPad[p];
            if (op.fresh)
                continue;
            for (int s = 0; s < kPadStyles; ++s) {
                const PadSpeeds v = legacyPadSpeeds(s + 1, op.speed[s], op.fine[s], op.aim, op.stick, op.gyro, op.touch);
                PadStyle& st = pad[p].s[s];
                st.stickSens = v.stick, st.fineSens = v.fine, st.gyroSens = v.gyro, st.touchSens = v.touch;
            }
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
      << "\ndifficulty=" << difficulty << "\nlives=" << lives << "\nstartlives=" << livesStartOf(lives) << "\nmaxlives=" << livesMostOf(lives) << "\nsoloseat=" << soloSeat << "\nchapterretry=" << (chapterRetry ? 1 : 0) << "\nblood=" << blood << "\ncuts=" << (cuts ? 1 : 0) << "\ntitle=" << (title ? 1 : 0) << "\nfbi=" << (fbi ? 1 : 0) << "\npcsplash=" << (pcSplash ? 1 : 0) << "\ngametext=" << gameText << "\nlang=" << language
      << "\naspect=" << aspect << "\nwiden2d=" << widen2d << "\nfullscreen=" << ((util::fullscreenLive() >= 0 ? util::fullscreenLive() != 0 : fullscreen) ? 1 : 0) <<"\nautoreload=" << (autoReload ? 1 : 0) << "\nedgereload=" << (edgeReload ? 1 : 0) << "\nflashlight=" << (flashlight ? 1 : 0) << "\nbigheads=" << (bigHeads ? 1 : 0) << "\nshotgun=" << (shotgun ? 1 : 0) << "\npartyboard=" << (partyBoard ? 1 : 0) << "\nhardcore=" << (hardcore ? 1 : 0) << "\nfreefire=" << (freeFire ? 1 : 0) << "\nmachinegun=" << (machineGun ? 1 : 0) << "\narsenal=" << (weaponSwitch ? 1 : 0) << "\nsuddendeath=" << (suddenDeath ? 1 : 0) << "\nvampire=" << (vampire ? 1 : 0) << "\nhostagecrisis=" << (hostageCrisis ? 1 : 0) << "\nroulette=" << (roulette ? 1 : 0) << "\nsurvival=" << (survival ? 1 : 0) << "\nexploding=" << (exploding ? 1 : 0) << "\nfunlook=" << funLook << "\nturbo=" << turbo << "\nbossrush=" << (bossRush ? 1 : 0) << "\nmarried=" << (married ? 1 : 0) << "\nscoredisplay=" << (scoreDisplay ? 1 : 0) << "\nattractgore=" << (attractGore ? 1 : 0) << "\nlgupdatecheck=" << (updateCheck ? 1 : 0) << "\noutputs=" << (outputs ? 1 : 0) << "\ncrosshairs=" << (crosshairs ? 1 : 0) << "\noutputsport=" << outputsPort << "\nlastversion=" << lastVersion << "\nholdskip=" << (holdSkip ? 1 : 0)
      << "\ndisplaymode=" << ((util::fullscreenLive() >= 0 ? util::fullscreenLive() != 0 : fullscreen) ? (exclusive ? 2 : 1) : 0)
      << "\nexclusive=" << (exclusive ? 1 : 0)
      << "\nresolution_fullscreen=" << util::sizeKey({resFullW, resFullH}) << "\nresolution_window=" << util::sizeKey({resWinW, resWinH})
      << "\ngamespeed=" << (gameSpeed < 0 ? "auto" : gameSpeed == 1 ? "60" : "arcade") << "\ncontinues=" << continues << "\nfreeplay=" << (freePlay ? 1 : 0) << "\nframerate=" << (frameRate == 0 ? std::string("display") : frameRate < 0 ? std::string("unlimited") : frameRate == kFrameRateArcade ? std::string("arcade") : std::to_string(frameRate))
      << "\nvsync=" << (vsync ? 1 : 0) << "\nrenderer=" << (renderer == 1 ? "gl" : renderer == 2 ? "angle" : "auto") << "\nsmooth=" << (smooth ? 1 : 0) << "\nhitsparks=" << (hitSparks ? 1 : 0) << "\nmuzzleflash=" << (muzzleFlash ? 1 : 0) << "\nshotflashsize=" << shotFlashSize << "\nshowcrosshairs=" << (showCrosshairs ? 1 : 0) << "\nbloodparticles=" << (bloodParticles ? 1 : 0) << "\nhitmarks=" << (clearMarks ? 1 : 0) << "\ntransparency=" << transparency << "\nnetname=" << netName << "\ndisplay=" << display << "\nnetinfo=" << netInfo << "\nnetpcmode=" << (netPcMode ? 1 : 0) << "\nnetbossmode=" << netBossMode << "\nnetpcchar=" << netPcChar[0] << ',' << netPcChar[1] << "\nsonicmode=" << sonicKey(sonic) << "\nboss4p=" << boss4p << "\nenemy4p=" << enemy4p << "\nteamflash=" << teamFlash << "\ntrailer=" << (trailer ? 1 : 0) << "\nscene4p=" << scene4p
      << "\ncrt=" << crtPresetKey(crt.preset) << "\ncrt.scan=" << crt.scan << "\ncrt.mask=" << crt.mask << "\ncrt.masktype=" << crt.maskType
      << "\ncrt.curve=" << crt.curve << "\ncrt.bloom=" << crt.bloom << "\ncrt.bright=" << crt.bright
      << "\ncrt.crispsights=" << (crt.crispSights ? 1 : 0) << "\ncrt.shotfiltered=" << (crt.shotFiltered ? 1 : 0)
      << "\ngamma=" << [this]() { char b[8]; return std::string(gammaText(gamma, b)); }()
      << "\nmenugamma=" << [this]() { char b[8]; return std::string(gammaText(menuGamma, b)); }()
      << "\nblacklevel=" << blackLevel << "\nvrr=" << (vrr ? 1 : 0)
      << "\nmusic=" << (music ? 1 : 0) << "\nmusicvolume=" << musicVolume << "\nsfxvolume=" << sfxVolume << "\nmusicnumbering=" << musicNumbering << "\nmenumusic=" << menuMusic
      << "\nmenustyle=" << menuStyle << "\nlivebg=" << liveBackground << "\nmenufps=" << menuFps << "\nmenublood=" << menuBlood << "\ngallery=" << galleryEntry << "\ngallerybg=" << galleryBg << "\ngallerylayout=" << galleryLayout;
    f << "\nscenecast=" << sceneCast;   // actors2: the cut-scene viewer's Actors
    f << "\nguntype=" << gunKind << "\ngunflash=" << gunFlash << "\ngunborder=" << gunBorder;   // padpass: light guns
    f << "\npadprompts=" << padPrompts << "\nbuttonstyle=" << buttonStyle << "\niconoutline=" << iconOutline;
    f << "\ntouchcursor=" << touchCursor << "\ngyrocursor=" << gyroCursor;   // padglyphs   // padglyphs: Button Prompts / Style
    // (#36, sens36) the menu cursor's % and, for an older build, its nearest 1-10
    f << "\ntouchcursorsens=" << touchCursorSens << "\ngyrocursorsens=" << gyroCursorSens
      << "\ncursorspeed=" << std::clamp(int(std::lround(touchCursorSens / 20.0)), 1, 10)
      << "\ngyrocursorspeed=" << std::clamp(int(std::lround(gyroCursorSens / 20.0)), 1, 10);
    f << "\nmousesens1=" << mouseSens[0] << "\nmousesens2=" << mouseSens[1];   // (#36)
    for (int i = 0; i < 5; ++i)
        f << "\nsteamart" << i << '=' << steamArt[i];
    for (int s = 0; s < 4; ++s) {   // firing range: each player's crosshair (player 1 without a number)
        const std::string n = s ? std::to_string(s + 1) : std::string();
        const SeatSight& ss = sights[s];
        f << "\ncrosshair" << n << '=' << ss.style << "\ncrosshaircustom" << n << '=' << ss.custom << "\ncrosssize" << n << '='
          << ss.size << "\ncrossthick" << n << '=' << ss.thick << "\ncrosscolour" << n << '=' << ss.colour;
    }
    f << "\np1input=" << p1Input << "\np3input=" << p3Input << "\np4input=" << p4Input << "\np2bot=" << seatBot[1]
      << "\np3bot=" << seatBot[2] << "\np4bot=" << seatBot[3] << "\npadv=4";   // (sens36: 4, each input its own speed)
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


namespace {

// setup2: the setup window's look (user 2026-11-02: "the same color style as the 1998 pc port"): the 1998 PC CD's
// start window (THOTDRUN.EXE) was a plain Windows dialog: a grey face, raised buttons, sunken white fields, black text;
// its picture at the top (here: the header strip, dark, the title in red, the wordmark)
constexpr uint32_t kBg = 0xF0F0F0, kButton = 0xE1E1E1, kButtonHi = 0xE8F0F8, kLight = 0xFFFFFF, kLite2 = 0xF4F4F4,
                   kShadow = 0xA0A0A0, kDark = 0x404040, kText = 0x000000, kDim = 0x404040, kFaint = 0x808080,
                   kGreyed = 0xA0A0A0, kField = 0xFFFFFF, kBlue = 0x000080, kHead = 0x000000, kHeadText = 0xFFFFFF,
                   kHeadDim = 0xFFFFFF, kRed = 0xB00000, kRedTitle = 0xC82020, kOk = 0x107010, kWarn = 0x8A5A00, kBad = 0xB00000;
constexpr float kW = 960, kH = 600;

// the actions of the page's clickable places
enum Action { kNext = 1, kBack, kQuit, kBrowseDir, kZoneA, kZoneP, kFolderA, kFolderP, kOptDesk, kOptStart, kOptSteam,
              kOptMusic, kDoInstall, kPlayHere, kPlay, kOpenFolder, kUpdate, kNotNow, kDownloadPage, kContinue, kStart };

// the painter: logical units (the window is 960 x 600) x s = the picture's pixels
struct Painter {
    float s = 1;
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    const render::TtfText* tf = nullptr;   // data\fonts (lang::anyFont); none: SDL's 8 x 8 font

    int P(float u) const { return int(std::lround(u * s)); }
    int t() const { return std::max(1, int(std::lround(s))); }   // a line's thickness
    void blend(int x, int y, uint32_t c, float a)
    {
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0.0f) return;
        uint8_t* d = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
        a = std::min(a, 1.0f);
        for (int k = 0; k < 3; ++k)
            d[k] = uint8_t(float(d[k]) * (1 - a) + float((c >> (16 - 8 * k)) & 0xFF) * a + 0.5f);
        d[3] = 255;
    }
    void fillPx(int x0, int y0, int x1, int y1, uint32_t c)
    {
        x0 = std::max(x0, 0), y0 = std::max(y0, 0), x1 = std::min(x1, w), y1 = std::min(y1, h);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                uint8_t* d = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
                d[0] = uint8_t(c >> 16), d[1] = uint8_t(c >> 8), d[2] = uint8_t(c), d[3] = 255;
            }
    }
    void fill(float x, float y, float fw, float fh, uint32_t c) { fillPx(P(x), P(y), P(x + fw), P(y + fh), c); }
    void frame(float x, float y, float fw, float fh, uint32_t c)
    {
        const int x0 = P(x), y0 = P(y), x1 = P(x + fw), y1 = P(y + fh), k = t();
        fillPx(x0, y0, x1, y0 + k, c), fillPx(x0, y1 - k, x1, y1, c), fillPx(x0, y0, x0 + k, y1, c), fillPx(x1 - k, y0, x1, y1, c);
    }
    // Windows' DrawEdge: an outline in two colours (top-left, bottom-right), then the bevels made of two of them
    void edge(int x0, int y0, int x1, int y1, uint32_t tl, uint32_t br)
    {
        const int k = t();
        fillPx(x0, y0, x1, y0 + k, tl), fillPx(x0, y0, x0 + k, y1, tl);
        fillPx(x0, y1 - k, x1, y1, br), fillPx(x1 - k, y0, x1, y1, br);
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
    void etchedLine(float x, float y, float lw)   // a group's dividing line
    {
        fill(x, y, lw, 1 / s, kShadow);
        fillPx(P(x), P(y) + 1, P(x + lw), P(y) + 2, kLight);
    }
    void dashed(float x, float y, float fw, float fh, uint32_t c)   // a thin dashed border (the drop zones)
    {
        const int x0 = P(x), y0 = P(y), x1 = P(x + fw), y1 = P(y + fh), k = t(), d = P(6), g = P(4);
        for (int a = x0; a < x1; a += d + g) fillPx(a, y0, std::min(a + d, x1), y0 + k, c), fillPx(a, y1 - k, std::min(a + d, x1), y1, c);
        for (int a = y0; a < y1; a += d + g) fillPx(x0, a, x0 + k, std::min(a + d, y1), c), fillPx(x1 - k, a, x1, std::min(a + d, y1), c);
    }
    void line(float ax, float ay, float bx, float by, float thick, uint32_t c)   // (the status marks)
    {
        const float len = std::hypot(bx - ax, by - ay) * s;
        const int n = std::max(2, int(len * 2)), r = std::max(1, int(std::lround(thick * s / 2)));
        for (int i = 0; i <= n; ++i) {
            const int x = P(ax + (bx - ax) * float(i) / float(n)), y = P(ay + (by - ay) * float(i) / float(n));
            fillPx(x - r, y - r, x + r, y + r, c);
        }
    }
    // the status mark (a plain check / ! / cross), size = its height
    void mark(int state, float x, float y, float size, uint32_t c)
    {
        if (state == 2) { line(x, y + size * 0.55f, x + size * 0.35f, y + size * 0.9f, 2, c); line(x + size * 0.35f, y + size * 0.9f, x + size * 0.9f, y + size * 0.1f, 2, c); }
        else if (state == 3) { fill(x + size * 0.38f, y, size * 0.22f, size * 0.62f, c); fill(x + size * 0.38f, y + size * 0.78f, size * 0.22f, size * 0.22f, c); }
        else if (state == 4) { line(x + size * 0.1f, y + size * 0.1f, x + size * 0.9f, y + size * 0.9f, 2, c); line(x + size * 0.9f, y + size * 0.1f, x + size * 0.1f, y + size * 0.9f, 2, c); }
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
        if (str.empty() || px.empty()) return;
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
        if (iw <= 0 || ih <= 0) return;
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

std::string fileName(const std::string& path)
{
    std::string n = std::filesystem::path(path).filename().string();
    return n.empty() ? path : n;
}

std::string mb(uint64_t bytes)
{
    char b[48];
    if (bytes >= (10ull << 30)) std::snprintf(b, sizeof b, "%.0f GB", double(bytes) / double(1ull << 30));
    else if (bytes >= (1ull << 30)) std::snprintf(b, sizeof b, "%.1f GB", double(bytes) / double(1ull << 30));
    else std::snprintf(b, sizeof b, "%.0f MB", double(bytes) / double(1ull << 20));
    return b;
}

// a path as the system writes it (Windows: backslashes; Linux / macOS: as it is)
std::string backslashes(std::string s)
{
#ifdef _WIN32
    std::replace(s.begin(), s.end(), '/', '\\');
#endif
    return s;
}

// the file a check line names ("MISSING SPRITE/SPR.RBH (1998 PC release)" -> "SPRITE\SPR.RBH")
std::string namedFile(const std::string& line)
{
    std::string s = line.substr(line.find(' '));
    s.erase(0, s.find_first_not_of(' '));
    s = s.substr(0, std::min(s.find(':'), s.find(" (")));
    return backslashes(s);
}

using Verdict = std::tuple<int, std::string, std::vector<std::string>>;   // state, line, more

// the arcade check's report in one short line (the check decides; this only says it)
Verdict arcadeVerdict(const std::string& where, const CheckReport& r)
{
    auto T = [](const char* s) { return lang::tr(s); };
    if (r.ok) return {2, T("All 29 ROM files match (Revision A)"), {}};
    int missing = 0, wrong = 0;
    std::string first;
    for (const std::string& l : r.lines) {
        if (l.rfind("MISSING", 0) == 0) { ++missing; if (first.empty()) first = l; }
        if (l.rfind("WRONG", 0) == 0) ++wrong;
    }
    const std::string hint = placementHint(where, true);
    if (hint.find("PC game") != std::string::npos && hint.rfind("That is the PC game", 0) == 0)
        return {4, T("This is the PC game, not the arcade ROMs"), {}};
    if (wrong) return {4, T("Not Revision A: MAME's \"hotd\" set is needed"), {T("The original release (hotdo) and prototypes do not work.")}};
    {   // MAME's other sets by their zip's name (hotdp: the prototype, hotdo: the original release)
        std::string n = std::filesystem::path(where).filename().string();
        for (char& c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (n.rfind("hotdp", 0) == 0 || n.rfind("hotdo", 0) == 0)
            return {4, T("Not Revision A: MAME's \"hotd\" set is needed"), {T("The original release (hotdo) and prototypes do not work.")}};
    }
    if (missing >= 29 || missing == 0) return {4, T("No arcade ROM files found here"), {hint.empty() ? std::string() : lang::tr(hint)}};
    // (the drop test, 2026-11-03) only Revision A's own program ROMs missing (epr-19696a / epr-19697a ...): another
    // revision's set, not a damaged one
    bool revOnly = missing <= 4;
    for (const std::string& l : r.lines)
        if (l.rfind("MISSING", 0) == 0) revOnly &= l.find("epr-1969") != std::string::npos && l.find("a.") != std::string::npos;
    if (revOnly)
        return {4, T("Not Revision A: MAME's \"hotd\" set is needed"), {T("The original release (hotdo) and prototypes do not work.")}};
    return {4, lang::fmt("{n} of 29 ROM files are missing", {{"n", std::to_string(missing)}}),
            {lang::fmt("{file} is missing", {{"file", namedFile(first)}})}};
}

Verdict pcVerdict(const std::string& where, const CheckReport& r)
{
    auto T = [](const char* s) { return lang::tr(s); };
    std::vector<std::string> notes;
    int missing = 0, wrong = 0;
    bool exeMissing = false;
    std::string firstMissing, firstWrong, why;
    for (const std::string& l : r.lines) {
        if (l.rfind("MISSING", 0) == 0) { ++missing; if (firstMissing.empty()) firstMissing = l; exeMissing |= l.find("THOTD.EXE") != std::string::npos; }
        else if (l.rfind("WRONG", 0) == 0) { ++wrong; if (firstWrong.empty()) firstWrong = l; }
        else if (l.rfind("PC files: ", 0) == 0 && l.find(" match the 1998") == std::string::npos) why = l.substr(10);
        else if (l.rfind("PC files:", 0) != 0 && l.rfind("Music (", 0) != 0 && l.rfind("moved", 0) != 0 && l.rfind("Some PC files", 0) != 0 &&
                 l.rfind("DIFFERS", 0) != 0 && l.rfind("note", 0) != 0)
            notes.push_back(l);   // (an accepted variant: a patched THOTD.EXE ... the check's own words)
    }
    // pchotfix: the check's own short notes (files of another version accepted, sounds missing, no menu art) first
    notes.insert(notes.begin(), r.notes.begin(), r.notes.end());
    if (r.ok) {
        if (notes.empty()) return {2, T("The 1998 PC game: all files match"), {}};
        std::vector<std::string> more;
        for (size_t i = 0; i < notes.size() && i < 2; ++i) more.push_back(lang::tr(notes[i]));
        return {3, T("Accepted, with a note:"), more};
    }
    if (!why.empty() && missing == 0 && wrong == 0) return {4, lang::tr(why), {}};
    const std::string hint = placementHint(where, false);
    if (hint.rfind("Those are the arcade ROMs", 0) == 0) return {4, T("These are the arcade ROMs, not the PC game"), {}};
    if (hint.rfind("Only THOTD.EXE", 0) == 0)   // (a no-CD patch's zip: the drop test, 2026-11-03)
        return {4, T("Only THOTD.EXE is here (a no-CD patch?)"), {T("The whole PC game is needed: drop its folder, CD image or zip.")}};
    if (!exeMissing && missing >= 40) return {4, T("This is the PC demo: the full 1998 PC game is needed"), {}};
    if (exeMissing && missing >= 40) return {4, T("No PC game files found here"), {hint.empty() ? std::string() : lang::tr(hint)}};
    if (!firstMissing.empty())
        return {4, lang::fmt("{file} is missing", {{"file", namedFile(firstMissing)}}),
                {missing + wrong > 1 ? lang::fmt("{n} files missing or different (all of them: settings\\hotd.log)", {{"n", std::to_string(missing + wrong)}}) : std::string()}};
    return {4, lang::fmt("{file} is not the 1998 PC version's file", {{"file", namedFile(firstWrong)}}), {}};
}

namespace fs = std::filesystem;

// a disc image copied out to <cache>\disc_<kind> (a folder of ours: its marker file), then checked there
std::string discFolder(const std::string& cache, const char* kind)
{
    const fs::path d = fs::path(cache) / (std::string("disc_") + kind);
    std::error_code ec;
    if (fs::exists(d / "hotd_disc_copy.txt", ec)) fs::remove_all(d, ec);   // (only our own copy)
    else if (fs::exists(d, ec)) return (fs::path(cache) / (std::string("disc_") + kind + "_" + std::to_string(SDL_GetTicks()))).string();
    return d.string();
}

} // namespace

Launcher::~Launcher()
{
    for (std::thread& t : zoneThread_) if (t.joinable()) t.join();
    if (installThread_.joinable()) installThread_.join();
}

bool Launcher::init(std::string& err)
{
    installDir_ = defaultInstallDir();
    return page_.init(err);
}

void Launcher::open(Mode m)
{
    mode = m;
    scroll_ = 0;
    quiet = false;
    updateErr_.clear();
    if (m == Mode::Repair) {
        step_ = kFiles;
        for (int i = 0; i < 2; ++i) {
            const std::string& p = i == 0 ? cfg.arcade : cfg.pc;
            if (!p.empty()) checkZone(i, p);
        }
    } else if (m == Mode::Update) {
        util::updateInfo(update_);
        download_.reset();
        if (std::getenv("HOTD_UPDATE_AUTO")) pending_ = Result::None, click(kUpdate, nullptr);   // (the test: Update pressed)
    }
}

// the check of one zone's source on its own thread (a new choice while one runs: waits for it)
void Launcher::checkZone(int which, const std::string& path)
{
    if (zoneThread_[which].joinable()) zoneThread_[which].join();
    {
        std::lock_guard<std::mutex> l(zoneMutex_);
        zone_[which] = Zone{};
        zone_[which].path = path;
        zone_[which].state = 1;
        zone_[which].line = lang::tr("Checking ...");
    }
    const std::string cache = cacheDir;
    zoneThread_[which] = std::thread([this, which, path, cache]() {
        Zone z;
        z.path = path;
        z.checked = path;
        std::string src = path, image = path, unzipped;
        auto lowered = [](std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return s; };
        struct Cleanup {   // (what was unpacked from a zip: only needed while its files are copied out)
            std::vector<std::string> files;
            std::string dir;   // a folder of ours (its marker file) removed whole
            ~Cleanup()
            {
                std::error_code ec;
                for (const std::string& f : files) fs::remove(f, ec);
                if (!dir.empty() && fs::exists(fs::path(dir) / "hotd_disc_copy.txt", ec)) fs::remove_all(dir, ec);
            }
        } cleanup;
        // the box's line and loading bar while it works (user 2026-11-03 "add a loading bar")
        auto stage = [&](const std::string& line, float progress) {
            std::lock_guard<std::mutex> l(zoneMutex_);
            if (zone_[which].path != path) return;
            if (!line.empty()) zone_[which].line = line;
            zone_[which].progress = std::max(zone_[which].progress, std::clamp(progress, 0.0f, 1.0f));
        };
        // a folder holding a CD image and no installed game (user 2026-11-03: the folder of a .cue and its .bin files
        // said "THOTD.EXE is missing"): its .cue, else its largest .iso / .img / .bin; "" when it is no CD image
        auto imageIn = [&](const std::string& dir) -> std::string {
            std::string cue, other;
            uintmax_t otherSize = 0;
            bool exe = false;
            std::error_code ec;
            for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (it.depth() > 3) { it.disable_recursion_pending(); continue; }
                const std::string n = lowered(it->path().filename().string()), e = lowered(it->path().extension().string());
                exe |= n == "thotd.exe";
                if (e == ".cue" && cue.empty()) cue = it->path().string();
                if (e == ".iso" || e == ".img" || e == ".bin") {
                    const uintmax_t s = it->file_size(ec);
                    if (s > otherSize) other = it->path().string(), otherSize = s;
                }
            }
            return exe ? std::string() : !cue.empty() ? cue : other;
        };
        stage({}, 0.05f);
        // what is read (the drop test, user 2026-11-03): THOTD.EXE itself dropped = its folder; a zip holding nothing but
        // another archive (and notes) = that archive, unpacked into the cache first
        std::string in = path;
        if (which == 1 && lowered(fs::path(path).filename().string()) == "thotd.exe") in = fs::path(path).parent_path().string();
        if (lowered(fs::path(in).extension().string()) == ".zip") {
            ZipFile outer;
            std::string err;
            if (outer.open(in, err)) {
                const ZipFile::Entry* inner = nullptr;
                bool other = false;
                for (const ZipFile::Entry& e : outer.entries()) {
                    const std::string n = lowered(e.name), ext = fs::path(n).extension().string();
                    if (n.empty() || n.back() == '/') continue;
                    if (ext == ".zip" || ext == ".7z" || ext == ".rar") { if (inner) other = true; else inner = &e; }
                    else if (ext != ".txt" && ext != ".nfo" && ext != ".url" && ext != ".diz" && ext != ".htm" && ext != ".html") other = true;
                }
                std::vector<uint8_t> d;
                if (inner && !other && outer.read(*inner, d, err)) {
                    std::error_code ec;
                    const fs::path dir = fs::path(cache) / (which == 0 ? "arcade_inner" : "pc_inner");
                    fs::create_directories(dir, ec);
                    const std::string f = (dir / fs::path(inner->name).filename()).string();
                    std::ofstream o(hotd::longPath(f), std::ios::binary | std::ios::trunc);
                    if (o.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()))) {
                        o.close();
                        in = f;
                        // (kept in the cache: the setup installs from it, and an arcade zip is read at each start)
                    }
                }
            }
        }
        src = image = z.checked = in;
        // a zip holding a CD image (user 2026-11-02 "allow for read it from zip"): its data track (the first track's
        // .bin, else the .iso / .img / .bin) unpacked into the cache, read as below, then removed
        // a .7z / .rar (user 2026-11-03 "can we support" it): unpacked into the cache by Windows' own tar (libarchive,
        // Windows 10 1803 and later) and checked there as a folder; a CD image in it (the .cue first) read as below
        if (which == 1 && fs::is_directory(in)) {
            if (const std::string img = imageIn(in); !img.empty()) image = img;
        } else if (const std::string ext = lowered(fs::path(in).extension().string()); ext == ".7z" || ext == ".rar") {
            stage(lang::tr("Unpacking ..."), 0.1f);
            const std::string dir = discFolder(cache, which == 0 ? "arcade_unpacked" : "pc_unpacked");
            std::error_code ec;
            fs::create_directories(dir, ec);
            std::ofstream(fs::path(dir) / "hotd_disc_copy.txt") << "Unpacked from " << path << " by the setup (removed with the next one).\n";
#ifdef _WIN32
            const char* root = std::getenv("SystemRoot");
            const std::string tar = std::string(root && *root ? root : "C:\\Windows") + "\\System32\\tar.exe";
            const int rc = winshell::runHidden(tar, "-xf \"" + in + "\" -C \"" + dir + "\"", 600);
#else
            // linux (2026-11-03): libarchive's bsdtar (macOS' own tar; Linux: the libarchive-tools package), else
            // 7-Zip (7zz / 7z / 7za: 7-Zip for Linux or p7zip); GNU tar cannot read .7z / .rar
            int rc = -1;
            std::string tool;
#ifdef __APPLE__
            const char* const tars[] = {"/usr/bin/tar", "bsdtar"};
#else
            const char* const tars[] = {"bsdtar"};
#endif
            for (const char* t : tars)
                if (tool.empty() && !(tool = winshell::findProgram(t)).empty()) rc = winshell::runHidden(tool, "-xf \"" + in + "\" -C \"" + dir + "\"", 600);
            for (const char* t : {"7zz", "7z", "7za"})
                if (tool.empty() && !(tool = winshell::findProgram(t)).empty()) rc = winshell::runHidden(tool, "x -y \"-o" + dir + "\" \"" + in + "\"", 600);
            if (tool.empty()) {
                z.state = 4, z.line = lang::fmt("{file} could not be unpacked", {{"file", fileName(path)}});
                z.more = {lang::tr("No program here unpacks .7z / .rar: install bsdtar (libarchive-tools) or 7-Zip (p7zip), or unpack it yourself and drop the folder here.")};
                std::lock_guard<std::mutex> l(zoneMutex_);
                if (zone_[which].path == path) zone_[which] = z;
                return;
            }
#endif
            bool any = false;
            for (const auto& e : fs::recursive_directory_iterator(dir, ec))
                any |= e.is_regular_file(ec) && e.path().filename() != "hotd_disc_copy.txt";
            if (rc != 0 || !any) {
                z.state = 4, z.line = lang::fmt("{file} could not be unpacked", {{"file", fileName(path)}});
                z.more = {lang::tr("Unpack it yourself (for example with 7-Zip) and drop the folder here.")};
                std::lock_guard<std::mutex> l(zoneMutex_);
                if (zone_[which].path == path) zone_[which] = z;
                return;
            }
            src = z.checked = dir;
            stage({}, 0.35f);
            image = which == 1 ? imageIn(dir) : std::string();   // (a CD image in it: read as below)
            if (!image.empty()) cleanup.dir = dir;               // (then only needed while its files are copied out)
        }
        ZipFile zf;
        bool zipImage = false;
        if (which == 1 && lowered(fs::path(in).extension().string()) == ".zip") {
            std::string err;
            if (zf.open(in, err)) {
                const ZipFile::Entry* best = nullptr;
                bool bestFirst = false, hasExe = false;
                for (const ZipFile::Entry& e : zf.entries()) {
                    const std::string n = lowered(e.name);
                    hasExe |= fs::path(n).filename() == "thotd.exe";
                    const std::string ext = n.size() > 4 ? n.substr(n.size() - 4) : "";
                    const bool first = n.find("track 01") != std::string::npos || n.find("track 1)") != std::string::npos;
                    if ((ext == ".bin" || ext == ".iso" || ext == ".img") && (!best || (first && !bestFirst)))
                        best = &e, bestFirst = first;
                }
                if (best && !hasExe) {
                    stage(lang::tr("Unpacking the CD image from the zip ..."), 0.1f);
                    std::error_code ec;
                    const fs::path dir = fs::path(cache) / "disc_zip";
                    fs::create_directories(dir, ec);
                    unzipped = (dir / fs::path(best->name).filename()).string();
                    cleanup.files.push_back(unzipped);
                    std::vector<uint8_t> d;
                    bool ok = zf.read(*best, d, err);
                    if (ok) {
                        std::ofstream out(unzipped, std::ios::binary);
                        ok = bool(out.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size())));
                    }
                    if (!ok) {
                        fs::remove(unzipped, ec);
                        z.state = 4, z.line = lang::tr("The CD image in the zip could not be unpacked");
                        z.more = {err.empty() ? lang::tr("Not enough disk space?") : err};
                        std::lock_guard<std::mutex> l(zoneMutex_);
                        if (zone_[which].path == path) zone_[which] = z;
                        return;
                    }
                    image = unzipped;
                    zipImage = true;
                }
            }
        }
        // any track's .bin of a disc with its .cue next to it (the drop test: Track 02, music only, said "not a readable
        // CD image"): the whole disc through its .cue
        if (lowered(fs::path(image).extension().string()) == ".bin")
            if (const std::string cue = disc::cueFor(image); !cue.empty()) image = cue;
        if (disc::isDiscImage(image)) {   // a CD image: what it is, then its files copied out and checked
            const disc::Info di = disc::probe(image);
            if (!di.ok) { z.state = 4; z.line = lang::tr("Not a readable CD image (ISO 9660)"); z.more = {di.err}; }
            else if (di.system == "saturn") { z.state = 4; z.line = lang::tr("This is the Saturn version"); z.more = {lang::tr("The game needs the 1998 PC version (and the arcade ROMs).")}; }
            else if (!di.system.empty()) { z.state = 4; z.line = lang::tr("This is a console disc, not the PC game"); }
            else if (which == 0) { z.state = 4; z.line = lang::tr("This is a CD image, not the arcade ROMs"); }
            else {
                const std::string out = discFolder(cache, "pc");
                stage(lang::tr("Copying the files from the CD image ..."), 0.35f);
                std::string err;
                if (!disc::extract(image, out, [&](float f) { stage({}, 0.35f + 0.35f * f); }, err)) { z.state = 4; z.line = lang::tr("The CD image could not be read"); z.more = {err}; }
                else {
                    std::ofstream(fs::path(out) / "hotd_disc_copy.txt") << "Files copied from " << path << " by the setup (removed with the next image).\n";
                    src = z.checked = out;
                    // the CD music (user 2026-11-02): the image's audio tracks as Music\TrackNN.wav next to its files
                    // (a zip: its .cue and the other tracks' files unpacked first, one at a time as needed)
                    std::string cue = disc::cueFor(image);
                    if (zipImage) {
                        cue.clear();
                        const fs::path dir = fs::path(image).parent_path();
                        for (const ZipFile::Entry& e : zf.entries()) {
                            const std::string n = lowered(e.name), ext = n.size() > 4 ? n.substr(n.size() - 4) : "";
                            if (ext != ".cue" && ext != ".bin") continue;
                            const std::string dst = (dir / fs::path(e.name).filename()).string();
                            if (dst == image) continue;
                            stage(lang::tr("Copying the CD music ..."), 0.72f);
                            std::vector<uint8_t> d;
                            std::string e2;
                            if (!zf.read(e, d, e2)) continue;
                            std::ofstream o(dst, std::ios::binary);
                            o.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
                            cleanup.files.push_back(dst);
                            if (ext == ".cue") cue = dst;
                        }
                    }
                    if (!cue.empty()) {
                        stage(lang::tr("Copying the CD music ..."), 0.8f);
                        std::string rerr;
                        const fs::path music = fs::path(out) / "Music";
                        const int n = disc::ripAudio(cue, music.string(), rerr);
                        // the US disc (the dump the user checked, 2026-11-02) has tracks 2 and 6 the other way round
                        // from the EU and Japanese discs and from what the game asks for (track 2 = the title song,
                        // the arcade's ~1 minute one; 6 = the boss, ~5 minutes): a longer 02 than 06 swapped back
                        std::error_code ec;
                        const fs::path t2 = music / "Track02.wav", t6 = music / "Track06.wav", tmp = music / "Track02.tmp";
                        if (fs::exists(t2, ec) && fs::exists(t6, ec) && fs::file_size(t2, ec) > 2 * fs::file_size(t6, ec)) {
                            fs::rename(t2, tmp, ec), fs::rename(t6, t2, ec), fs::rename(tmp, t6, ec);
                            SDL_Log("[files] CD music: tracks 2 and 6 swapped (this disc has them the other way round)");
                        }
                        SDL_Log("[files] CD music: %d track(s) from %s%s%s", n, cue.c_str(), rerr.empty() ? "" : ": ", rerr.c_str());
                    }
                }
            }
            if (z.state == 4) {
                std::lock_guard<std::mutex> l(zoneMutex_);
                if (zone_[which].path == path) zone_[which] = z;
                return;
            }
        }
        stage(lang::tr("Checking the files ..."), 0.9f);
        Verdict v;
        if (which == 0) {
            const CheckReport r = checkArcade(src, cache + "arcade_regions", false);
            for (const std::string& s : r.lines) SDL_Log("[files] %s", s.c_str());
            v = arcadeVerdict(src, r);
        } else {
            std::map<std::string, std::string> found;
            const CheckReport r = checkPc(src, cache + "pc_files.tsv", found);
            for (const std::string& s : r.lines) SDL_Log("[files] %s", s.c_str());
            v = pcVerdict(src, r);
            z.music = musicTracks(src);
        }
        z.state = std::get<0>(v), z.line = std::get<1>(v), z.more = std::get<2>(v);
        std::lock_guard<std::mutex> l(zoneMutex_);
        if (zone_[which].path == path) zone_[which] = z;
    });
}

bool Launcher::zonesReady() const
{
    std::lock_guard<std::mutex> l(zoneMutex_);
    return (zone_[0].state == 2 || zone_[0].state == 3) && (zone_[1].state == 2 || zone_[1].state == 3);
}

void Launcher::testZone(int which, const std::string& path, int state, const std::string& line, const std::vector<std::string>& more)
{
    std::lock_guard<std::mutex> l(zoneMutex_);
    zone_[which] = Zone{path, path, state, line, more, which == 1 ? 11 : 0};
}

void Launcher::testInstallProgress(float f, const std::string& line)
{
    installState_ = 1;
    installFrac_ = f;
    std::lock_guard<std::mutex> l(zoneMutex_);
    installLine_ = line;
}

void Launcher::testDone(const std::string& dest)
{
    installState_ = 2;
    installDir_ = dest;
    step_ = kDone;
}

void Launcher::startInstall()
{
    if (installState_ == 1) return;
    if (installThread_.joinable()) installThread_.join();
    InstallOptions o;
    const bool repairHere = mode == Mode::Repair && installed;
    o.dest = repairHere ? packageRoot : installDir_;
    o.programRoot = packageRoot;
    o.arcade = cfg.arcade, o.pc = cfg.pc;
    o.settingsFrom = cacheDir + "launcher.ini";
    o.desktop = optDesktop_ && !repairHere, o.startMenu = optStartMenu_ && !repairHere, o.steam = optSteam_ && !repairHere;
    o.music = optMusic_;
    o.version = version;
    installState_ = 1;
    installFrac_ = 0;
    installErr_.clear();
    installThread_ = std::thread([this, o]() {
        const InstallResult r = installGame(o, [this](float f, const std::string& s) {
            installFrac_ = f;
            std::lock_guard<std::mutex> l(zoneMutex_);
            installLine_ = s;
        });
        std::lock_guard<std::mutex> l(zoneMutex_);
        installErr_ = r.err;
        installedExe_ = r.gameExe;
        installState_ = r.ok ? 2 : 3;
    });
}

// Next: the page's own step (the files: kept in launcher.ini as chosen, a CD image as its copied folder)
void Launcher::next(SDL_Window* window)
{
    (void)window;
    if (step_ == kWelcome) {
        if (installDirProblem(installDir_, packageRoot).empty() || packageRoot.empty()) step_ = kFiles;
        for (int i = 0; i < 2; ++i) {   // (the files already known: checked as the page opens)
            const std::string& p = i == 0 ? cfg.arcade : cfg.pc;
            if (!p.empty() && zoneCopy(i).state == 0) checkZone(i, p);
        }
        return;
    }
    if (step_ == kFiles) {
        if (!zonesReady()) return;
        cfg.arcade = zoneCopy(0).checked;
        cfg.pc = zoneCopy(1).checked;
        cfg.save(cacheDir + "launcher.ini");
        if (mode == Mode::Repair && !installed) { click(kStart, window); return; }   // (a portable copy: these files, play)
        step_ = mode == Mode::Repair ? kInstall : kOptions;
        return;
    }
    if (step_ == kOptions) { step_ = kInstall; return; }
}

void Launcher::back()
{
    if (installState_ == 1) return;
    if (step_ == kInstall && installState_ == 3) installState_ = 0;
    if (mode == Mode::Repair) { if (step_ == kInstall) step_ = kFiles; return; }
    if (step_ > kWelcome && step_ < kDone) --step_;
}

void Launcher::render(int ww, int wh)
{
    if (!wordTried_) {
        wordTried_ = true;
        std::string err;
        if (wordmarkFile.empty() || !readPngRgba(wordmarkFile, wordW_, wordH_, word_, err) || wordW_ <= 0 || wordH_ <= 0)
            wordW_ = wordH_ = 0;
    }
    const Zone za = zoneCopy(0), zp = zoneCopy(1);
    std::string instLine, instErr;
    {
        std::lock_guard<std::mutex> l(zoneMutex_);
        instLine = installLine_, instErr = installErr_;
    }
    const int ist = installState_;
    if (ist == 2 && step_ == kInstall) step_ = kDone;
    // what the picture shows: drawn again only when it changes
    std::string key = std::to_string(ww) + 'x' + std::to_string(wh) + '|' + std::to_string(int(mode)) + '|' + std::to_string(step_) + '|' +
                      std::to_string(pressed_) + '|' + std::to_string(hover_) + '|' + std::to_string(dropHover_) + '|' +
                      std::to_string(lang::gen()) + '|' + installDir_ + '|' + std::to_string(scroll_) + '|' +
                      std::to_string(optDesktop_) + std::to_string(optStartMenu_) + std::to_string(optSteam_) + std::to_string(optMusic_);
    for (const Zone* z : {&za, &zp}) {
        key += '\n' + z->path + '\t' + std::to_string(z->state) + z->line + '\t' + std::to_string(int(z->progress * 100));
        for (const std::string& s : z->more) key += '\v' + s;
    }
    key += '\r' + std::to_string(ist) + std::to_string(int(installFrac_ * 200)) + instLine + instErr + updateErr_;
    if (download_) key += '\a' + std::to_string(int(download_->state())) + std::to_string(int(download_->progress() * 200)) + download_->message();
    for (const std::string& s : status) key += '\f' + s;
    if (key == drawnKey_) {
        page_.drawAnchored(ww, wh, drawX_, drawY_, 0.0f, 0.0f);
        return;
    }
    drawnKey_ = key;
    auto T = [](const char* s) { return lang::tr(s); };

    Painter p;
    p.tf = lang::anyFont();
    p.s = scale_ = std::clamp(std::min(float(ww) / kW, float(wh) / kH), 0.5f, 4.0f);
    p.w = std::max(1, int(std::ceil(kW * p.s)));
    p.h = std::max(1, int(std::ceil(kH * p.s)));
    p.px.assign(size_t(p.w) * size_t(p.h) * 4, 0);
    std::vector<Hot> hots;
    auto hotAt = [&](float x, float y, float bw, float bh, int action) { hots.push_back({p.P(x), p.P(y), p.P(x + bw), p.P(y + bh), action}); };
    auto fitText = [&](const std::string& s, float maxW, float cap, bool tail) {
        if (p.textW(s, cap) <= maxW) return s;
        for (size_t n = s.size(); n > 4; --n) {
            const std::string cut = tail ? "..." + util::utf8Tail(s, n) : util::utf8Head(s, n) + "...";
            if (cut.size() < s.size() + 3 && p.textW(cut, cap) <= maxW) return cut;
        }
        return std::string("...");
    };
    auto wrap = [&](const std::string& s, float maxW, float cap) {
        return lang::wrapBy(s, maxW, [&](const std::string& x) { return p.textW(x, cap); });
    };
    // a classic push button (raised; pushed in while held; greyed text: not available); primary = the default
    // button (Windows': a black line around it)
    auto button = [&](const std::string& label, float x, float y, float bw, float bh, int action, bool enabled, bool primary) {
        const bool down = enabled && pressed_ == action;
        p.fill(x, y, bw, bh, enabled && hover_ == action ? kButtonHi : kButton);
        const float k = primary && enabled ? 1.0f : 0.0f;
        if (k > 0) p.frame(x, y, bw, bh, kText);
        if (down) p.pushed(x + k, y + k, bw - 2 * k, bh - 2 * k);
        else p.raised(x + k, y + k, bw - 2 * k, bh - 2 * k);
        const float cap = 9.0f, off = down ? 1.0f : 0.0f;
        const std::string shown = fitText(label, bw - 16, cap, false);
        p.text(shown, x + (bw - p.textW(shown, cap)) / 2 + off, y + bh / 2 + cap / 2 + off, cap, enabled ? kText : kGreyed);
        if (enabled) hotAt(x, y, bw, bh, action);
    };
    // a sunken white field
    auto field = [&](float x, float y, float fw, float fh) {
        p.fill(x, y, fw, fh, kField);
        p.sunken(x, y, fw, fh);
    };
    auto para = [&](const std::string& s, float x, float& y, float maxW, float cap, uint32_t c) {
        for (const std::string& l : wrap(s, maxW, cap)) {
            p.text(l, x, y + cap, cap, c);
            y += cap * 1.9f;
        }
    };
    auto progressBar = [&](float x, float y, float bw, float frac) {
        field(x, y, bw, 18);   // (Windows 98's: blue blocks)
        const float inner = bw - 8, filled = inner * std::clamp(frac, 0.0f, 1.0f);
        for (float bx = 0; bx + 1 < filled; bx += 10) p.fill(x + 4 + bx, y + 4, std::min(8.0f, filled - bx), 10, kBlue);
    };
    auto stateColour = [](int st) { return st == 2 ? kOk : st == 3 ? kWarn : st == 4 ? kBad : kDim; };

    // the window: the header (the 1998 start window's picture: dark, the title in red, our wordmark; the steps), the
    // page on the grey face, an etched line over the buttons
    p.fill(0, 0, kW, kH, kBg);
    p.fill(0, 0, kW, 64, kHead);
    p.fill(0, 64, kW, 1, kShadow);
    p.fill(0, 65, kW, 1, kLight);
    if (wordW_ > 0) {   // (user 2026-11-02: the wordmark only)
        const float dh = std::min(40.0f, 260.0f * float(wordH_) / float(wordW_)), dw = dh * float(wordW_) / float(wordH_);
        p.image(word_, wordW_, wordH_, 24, (64 - dh) / 2, dw, dh);
    } else {
        p.text("REWRITHE", 24, 40, 14, kHeadText);
    }
    std::vector<std::string> steps;
    int cur = -1;
    if (mode == Mode::Setup) steps = {T("Install to"), T("Game files"), T("Options"), T("Install"), T("Play")}, cur = step_;
    else if (mode == Mode::Repair) steps = {T("Game files"), T("Import"), T("Play")}, cur = step_ == kFiles ? 0 : step_ == kInstall ? 1 : 2;
    else if (mode == Mode::Update) steps = {T("Update")}, cur = 0;
    else steps = {T("What's new")}, cur = 0;
    {
        float x = kW - 24;
        for (int i = int(steps.size()) - 1; i >= 0; --i) {
            const std::string s = steps.size() > 1 ? std::to_string(i + 1) + "  " + steps[size_t(i)] : steps[size_t(i)];
            const float w = p.textW(s, 8.5f);
            x -= w;
            p.text(s, x, 37, 8.5f, i == cur ? kHeadText : kHeadDim);
            if (i == cur) p.fill(x, 45, w, 2, kRedTitle);
            x -= 22;
        }
    }
    p.etchedLine(24, kH - 58, kW - 48);
    const float L = 48, R = kW - 48, CW = R - L, BY = kH - 44, BH = 30;
    const bool busy = ist == 1 || (download_ && download_->state() == util::UpdateDownload::Running);
    auto title = [&](const std::string& s) { p.text(s, L, 98, 16, kText); };
    float y = 120;

    if (mode == Mode::Setup && step_ == kWelcome) {
        title(T("Install to"));
        y = 124;
        para(T("The game and your game files are copied to this folder. The originals are only read."), L, y, CW, 9, kDim);
        y += 10;
        field(L, y, CW - 140, 30);
        p.text(fitText(installDir_, CW - 160, 9.5f, true), L + 10, y + 20, 9.5f, kText);
        button(T("Browse..."), R - 128, y, 128, 30, kBrowseDir, !packageRoot.empty(), false);
        y += 46;
        const int64_t free = freeBytes(installDir_);
        p.text(lang::fmt("Free space: {free}    Needed: about {need}", {{"free", free >= 0 ? mb(uint64_t(free)) : T("unknown")}, {"need", "450 MB"}}),
               L, y + 9, 9, kDim);
        y += 26;
        const std::string why = packageRoot.empty() ? std::string() : installDirProblem(installDir_, packageRoot);
        if (!why.empty()) {
            p.mark(4, L, y, 11, kBad);
            float yy = y;
            para(lang::tr(why), L + 20, yy, CW - 20, 9, kBad);
            y = yy;
        }
        y = std::max(y, 330.0f);
        if (packageRoot.empty())
            para(T("This is a developer build: it can only play from here (Install needs the release package)."), L, y, CW, 9, kFaint);
        else if (!installed)
            para(T("You can also play from this folder without installing (on the Install page)."), L, y, CW, 9, kFaint);
        button(T("Exit"), L, BY, 110, BH, kQuit, true, false);
        button(T("Next"), R - 130, BY, 130, BH, kNext, why.empty() || packageRoot.empty(), true);
    } else if ((mode == Mode::Setup || mode == Mode::Repair) && step_ == kFiles) {
        title(T("Game files"));
        y = 124;
        para(T("Drop a zip / .7z, a folder or a CD image on each box, or click it to choose. They are checked at once."), L, y, CW, 9, kDim);
        const float zy = 162, zh = 290, gap = 24, zw = (CW - gap) / 2;
        const struct { const Zone* z; const char* name; const char* sub; int click, folder; } zs[2] = {
            {&za, "Arcade ROMs", "Revision A (MAME set \"hotd\")", kZoneA, kFolderA},
            {&zp, "PC game", "The 1998 PC version", kZoneP, kFolderP}};
        for (int i = 0; i < 2; ++i) {
            const Zone& z = *zs[i].z;
            const float x = L + float(i) * (zw + gap);
            const bool hi = dropHover_ == i || hover_ == zs[i].click;
            field(x, zy, zw, zh);
            p.frame(x + 6, zy + 6, zw - 12, zh - 12, hi ? kBlue : z.state == 2 ? kOk : z.state == 3 ? kWarn : z.state == 4 ? kBad : kShadow);
            hotAt(x, zy, zw, zh, zs[i].click);
            float yy = zy + 34;
            p.text(T(zs[i].name), x + 20, yy, 13, kText);
            yy += 22;
            p.text(T(zs[i].sub), x + 20, yy, 8.5f, kDim);
            yy += 40;
            if (z.path.empty()) {
                const std::string a = T("Drop it here"), b = T("or click to choose a zip or CD image");
                p.text(a, x + (zw - p.textW(a, 11)) / 2, zy + zh / 2 + 10, 11, kDim);
                p.text(b, x + (zw - p.textW(b, 8.5f)) / 2, zy + zh / 2 + 34, 8.5f, kFaint);
                if (i == 1) {   // (user 2026-11-03: the .cue is the one that works best; the .bin files are the music)
                    float hy = zy + zh / 2 + 52;
                    for (const std::string& l : wrap(T("A CD image: choose its .cue file (its .bin files hold the CD music)"), zw - 40, 8.5f)) {
                        p.text(l, x + (zw - p.textW(l, 8.5f)) / 2, hy + 8.5f, 8.5f, kFaint);
                        hy += 16;
                    }
                }
            } else {
                p.text(fitText(fileName(z.path), zw - 40, 10, false), x + 20, yy, 10, kText);
                yy += 18;
                p.text(fitText(backslashes(z.path), zw - 40, 7.5f, true), x + 20, yy, 7.5f, kFaint);
                yy += 30;
                const uint32_t c = stateColour(z.state);
                p.mark(z.state, x + 20, yy - 10, 11, c);
                float ly = yy - 11;
                para(z.line, x + 40, ly, zw - 60, 9.5f, c);
                ly += 4;
                if (z.state == 1) {   // (checking: the loading bar)
                    progressBar(x + 40, ly, zw - 80, z.progress);
                    ly += 28;
                }
                for (const std::string& m : z.more)
                    if (!m.empty()) para(m, x + 40, ly, zw - 60, 8.5f, kDim);
                if (i == 1 && (z.state == 2 || z.state == 3))
                    p.text(z.music ? lang::fmt("{n} CD music tracks found", {{"n", std::to_string(z.music)}}) : T("No CD music (the arcade's music plays)"),
                           x + 40, ly + 9, 8.5f, kFaint);
            }
            button(T("Folder..."), x + zw - 130, zy + zh - 46, 110, 28, zs[i].folder, true, false);
        }
        if (mode == Mode::Repair) button(T("Cancel"), L, BY, 110, BH, installed || checked ? kNotNow : kQuit, true, false);
        else button(T("Back"), L, BY, 110, BH, kBack, true, false);
        const bool ready = zonesReady();
        button(mode == Mode::Repair && !installed ? T("Play") : T("Next"), R - 130, BY, 130, BH, kNext, ready, true);
    } else if (mode == Mode::Setup && step_ == kOptions) {
        title(T("Options"));
        y = 150;
        auto box = [&](bool on, const std::string& label, const std::string& note, int action, bool enabled) {
            const float bs = 16;
            p.fill(L, y, bs, bs, enabled ? kField : kBg);
            p.sunken(L, y, bs, bs);
            if (on && enabled) p.mark(2, L + 3, y + 3, 10, kText);
            p.text(label, L + 30, y + 13, 10, enabled ? kText : kFaint);
            if (!note.empty()) p.text(note, L + 30 + p.textW(label, 10) + 14, y + 13, 8.5f, kFaint);
            if (enabled) hotAt(L - 4, y - 6, std::max(300.0f, p.textW(label, 10) + 40), bs + 12, action);
            y += 40;
        };
#if defined(_WIN32)
        box(optDesktop_, T("Desktop shortcut"), "", kOptDesk, true);
        box(optStartMenu_, T("Start menu entry"), "", kOptStart, true);
#elif defined(__APPLE__)   // linux (2026-11-03): no shortcuts on macOS yet (the package's .app)
        box(false, T("Desktop shortcut"), "", kOptDesk, false);
        box(false, T("Start menu entry"), "", kOptStart, false);
#else                      // Linux: desktop entries (~/Desktop, ~/.local/share/applications)
        box(optDesktop_, T("Desktop shortcut"), "", kOptDesk, true);
        box(optStartMenu_, T("Applications menu entry"), "", kOptStart, true);
#endif
        box(optSteam_, T("Add to Steam"), T("(Steam must be closed)"), kOptSteam, true);
        box(optMusic_ && zp.music > 0, T("Import the CD music"),
            zp.music ? lang::fmt("({n} tracks found)", {{"n", std::to_string(zp.music)}}) : T("(none found in your PC game)"), kOptMusic, zp.music > 0);
        button(T("Back"), L, BY, 110, BH, kBack, true, false);
        button(T("Next"), R - 130, BY, 130, BH, kNext, true, true);
    } else if ((mode == Mode::Setup || mode == Mode::Repair) && step_ == kInstall) {
        const bool repairHere = mode == Mode::Repair && installed;
        title(repairHere ? T("Import") : T("Install"));
        y = 140;
        auto row = [&](const std::string& k, const std::string& v) {
            p.text(k, L, y + 10, 9.5f, kDim);
            p.text(fitText(v, CW - 170, 9.5f, true), L + 170, y + 10, 9.5f, kText);
            y += 26;
        };
        row(T("Install to"), repairHere ? backslashes(packageRoot) : installDir_);
        row(T("Arcade ROMs"), backslashes(cfg.arcade));
        row(T("PC game"), backslashes(cfg.pc));
        y += 18;
        if (ist == 1) {
            progressBar(L, y, CW, installFrac_);
            p.text(fitText(instLine, CW, 8.5f, false), L, y + 36, 8.5f, kDim);
        } else if (ist == 3) {
            p.mark(4, L, y, 11, kBad);
            float yy = y;
            para(instErr, L + 20, yy, CW - 20, 9.5f, kBad);
        } else if (packageRoot.empty()) {
            para(T("Install needs the release package; this developer build plays from here."), L, y, CW, 9, kFaint);
        } else {
            para(repairHere ? T("The game files are imported into the game's folder again.")
                            : T("Copies the game and only the files it needs: the 29 arcade ROM files and the PC game's files (and the music if chosen)."),
                 L, y, CW, 9, kDim);
        }
        button(T("Back"), L, BY, 110, BH, kBack, !busy, false);
        if (!repairHere && mode == Mode::Setup)
            button(T("Play from here"), R - 300, BY, 160, BH, kPlayHere, !busy, false);
        button(repairHere ? T("Import") : T("Install"), R - 130, BY, 130, BH, kDoInstall, !busy && !packageRoot.empty(), true);
    } else if ((mode == Mode::Setup || mode == Mode::Repair) && step_ == kDone) {
        title(mode == Mode::Repair ? T("Files imported") : T("Installed"));
        y = 140;
        para(mode == Mode::Repair ? T("The game uses the new files from now on.")
                                  : lang::fmt("The game is in {dir}. Start it with the shortcut from now on.", {{"dir", installDir_}}),
             L, y, CW, 9.5f, kDim);
        button(T("Play"), L + (CW - 240) / 2, 260, 240, 56, kPlay, true, true);
        if (mode == Mode::Setup) {
            const std::string o = T("Open the install folder");
            const float w = p.textW(o, 9.5f);
            p.text(o, L + (CW - w) / 2, 352, 9.5f, hover_ == kOpenFolder ? kRed : kBlue);
            p.fill(L + (CW - w) / 2, 355, w, 1, hover_ == kOpenFolder ? kRed : kBlue);
            hotAt(L + (CW - w) / 2 - 4, 336, w + 8, 24, kOpenFolder);
        }
        button(T("Exit"), L, BY, 110, BH, kQuit, true, false);
    } else {   // What's New / Update: a scrolling text (markdown: headings, bullets), the buttons under it
        std::string text = mode == Mode::Update ? update_.body : whatsNew;
        if (mode == Mode::Update) {
            title(T("Update available"));
            p.text(lang::fmt("Version {new} is out (you have {old}).", {{"new", update_.tag}, {"old", version}}), L, 124, 9.5f, kDim);
        } else {
            title(lang::fmt("What's new in {v}", {{"v", version}}));
        }
        const float ty = 140, th = mode == Mode::Update ? 300.0f : 370.0f;
        field(L, ty, CW, th);
        std::vector<std::pair<std::string, float>> lines;   // (text, cap: 12 a heading); colour by cap
        std::istringstream in(text);
        for (std::string l; std::getline(in, l);) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            for (size_t k; (k = l.find("**")) != std::string::npos;) l.erase(k, 2);
            l.erase(std::remove(l.begin(), l.end(), '`'), l.end());
            if (l.rfind("# ", 0) == 0) continue;   // (the file's title)
            if (l.rfind("<!--", 0) == 0) continue;
            float cap = 8.5f;
            std::string lead;
            if (l.rfind("## ", 0) == 0 || l.rfind("### ", 0) == 0) { cap = 11; l = l.substr(l.find(' ') + 1); }
            else if (l.rfind("- ", 0) == 0 || l.rfind("* ", 0) == 0) { lead = "-  "; l = l.substr(2); }
            else if (l.rfind("  ", 0) == 0) { lead = "   "; l.erase(0, l.find_first_not_of(' ')); }
            if (l.empty()) { lines.push_back({"", 4}); continue; }
            bool first = true;
            for (const std::string& w : wrap(l, CW - 40 - p.textW("-  ", cap), cap)) {
                lines.push_back({(first ? lead : std::string(lead.size(), ' ')) + w, cap});
                first = false;
            }
        }
        float total = 0;
        for (const auto& [s, cap] : lines) total += cap == 4 ? 8 : cap * 2.0f;
        scrollMax_ = std::max(0, int(total - (th - 24)));
        scroll_ = std::clamp(scroll_, 0, scrollMax_);
        float ly = ty + 14 - float(scroll_);
        for (const auto& [s, cap] : lines) {
            const float hgt = cap == 4 ? 8 : cap * 2.0f;
            if (ly >= ty + 4 && ly + hgt <= ty + th - 4 && !s.empty())
                p.text(s, L + 20, ly + cap * 1.4f, cap, cap > 9 ? kText : kDim);
            ly += hgt;
        }
        if (scrollMax_ > 0) {   // a plain scroll bar
            const float bh = std::max(30.0f, (th - 8) * (th - 24) / total), by = ty + 4 + (th - 8 - bh) * float(scroll_) / float(scrollMax_);
            p.fill(R - 18, ty + 2, 16, th - 4, kLite2);
            p.fill(R - 18, by, 16, bh, kButton);
            p.raised(R - 18, by, 16, bh);
        }
        if (mode == Mode::Update) {
            const bool canUpdate = util::kSelfUpdate && !packageRoot.empty();
            float yy = ty + th + 18;
            if (download_ && download_->state() == util::UpdateDownload::Running) {
                progressBar(L, yy, CW, download_->progress());
                p.text(download_->total() ? lang::fmt("{got} of {all}", {{"got", mb(download_->bytes())}, {"all", mb(download_->total())}})
                                          : mb(download_->bytes()), L, yy + 32, 8.5f, kDim);
            } else if (!updateErr_.empty() || (download_ && download_->state() == util::UpdateDownload::Failed)) {
                p.mark(4, L, yy, 11, kBad);
                para(updateErr_.empty() ? download_->message() : updateErr_, L + 20, yy, CW - 20, 9, kBad);
            } else if (!util::kSelfUpdate) {   // linux (2026-11-03): the notice and the download page only
                para(T("On Linux and macOS the game does not update itself: get the new version from the download page."), L, yy, CW, 9, kFaint);
            } else if (!canUpdate) {
                para(T("This copy cannot update itself (a developer build): use the download page."), L, yy, CW, 9, kFaint);
            }
            button(T("Not now"), L, BY, 130, BH, kNotNow, !busy, false);
            if (canUpdate) button(T("Update"), R - 130, BY, 130, BH, kUpdate, !busy, true);
            else button(T("Download page"), R - 170, BY, 170, BH, kDownloadPage, true, true);
        } else {
            button(T("Continue"), R - 130, BY, 130, BH, kContinue, true, true);
        }
    }
    // the start-up check's own lines (a failed check: the Game files page shows its zones; the rest here)
    if (!notes.empty() && mode == Mode::Setup) {
        float ny = kH - 76;
        for (const std::string& n : notes) { p.text(fitText(n, CW, 7.5f, false), L, ny, 7.5f, kFaint); ny -= 12; }
    }
    wantW = kW, wantH = kH;
    drawX_ = std::floor((float(ww) - float(p.w)) / 2.0f);
    drawY_ = std::floor(std::max(0.0f, (float(wh) - float(p.h)) / 2.0f));
    hot_.clear();
    for (const Hot& h : hots)
        hot_.push_back({h.x0 + int(drawX_), h.y0 + int(drawY_), h.x1 + int(drawX_), h.y1 + int(drawY_), h.action});
    if (!shotFile.empty()) {   // pchotfix: --setup-shot: the page into a PNG, nothing drawn
        drawnKey_.clear();
        if (!writePngRgba(shotFile, p.w, p.h, p.px))
            std::fprintf(stderr, "setup shot: cannot write %s\n", shotFile.c_str());
        return;
    }
    page_.setImage(p.px, p.w, p.h);
    page_.drawAnchored(ww, wh, drawX_, drawY_, 0.0f, 0.0f);
}

void Launcher::click(int action, SDL_Window* window)
{
    auto dialog = [&](int target, bool folder) {
        dialogTarget_ = target;
        auto cb = [](void* self, const char* const* files, int) {
            Launcher* l = static_cast<Launcher*>(self);
            std::lock_guard<std::mutex> lock(l->dialogMutex_);
            l->dialogResult_ = files && files[0] ? files[0] : "";
        };
        const std::string cur = target == 3 ? installDir_ : target == 2 ? cfg.pc : cfg.arcade;
        const char* start = cur.empty() ? nullptr : cur.c_str();
        if (folder) {
            SDL_ShowOpenFolderDialog(cb, this, window, start, false);
        } else {
            static const SDL_DialogFileFilter filter[] = {{"Zip, 7z or CD image (.cue)", "zip;7z;rar;cue;iso;bin;img"}, {"All files", "*"}};
            SDL_ShowOpenFileDialog(cb, this, window, filter, 2, start, false);
        }
    };
    switch (action) {
    case kNext: next(window); break;
    case kBack: back(); break;
    case kQuit: pending_ = Result::Quit; break;
    case kBrowseDir: dialog(3, true); break;
    case kZoneA: dialog(1, false); break;
    case kZoneP: dialog(2, false); break;
    case kFolderA: dialog(1, true); break;
    case kFolderP: dialog(2, true); break;
    case kOptDesk: optDesktop_ = !optDesktop_; break;
    case kOptStart: optStartMenu_ = !optStartMenu_; break;
    case kOptSteam: optSteam_ = !optSteam_; break;
    case kOptMusic: optMusic_ = !optMusic_; break;
    case kDoInstall: startInstall(); break;
    case kPlayHere: click(kStart, window); return;
    case kPlay: {
        const bool fresh = mode == Mode::Setup && !installedExe_.empty();
        if (fresh && winshell::launch(installedExe_, "", fs::path(installedExe_).parent_path().string())) {
            pending_ = Result::Quit;   // (the installed copy runs now)
            return;
        }
        mode = Mode::Setup;
        click(kStart, window);
        return;
    }
#ifdef _WIN32
    case kOpenFolder: SDL_OpenURL(("file:///" + backslashes(installDir_)).c_str()); break;
#else
    case kOpenFolder: SDL_OpenURL(("file://" + installDir_).c_str()); break;   // (an absolute path: file:///home/...)
#endif
    case kDownloadPage: SDL_OpenURL(util::kReleasesPage); break;
    case kUpdate: {
        updateErr_.clear();
        if (packageRoot.empty() || !util::kSelfUpdate) { SDL_OpenURL(util::kReleasesPage); break; }
        if (!download_) download_ = std::make_unique<util::UpdateDownload>();
        download_->start(update_, packageRoot + "/settings/update");
        break;
    }
    case kNotNow:
    case kContinue:
        mode = Mode::Setup;
        scroll_ = 0;
        if (checked && pcOk && arcadeOk_) pending_ = Result::Start;   // (back to the menu: the files passed already)
        else click(kStart, window);
        return;
    case kStart: checkRequested_ = true; startAfterCheck_ = true; status = {"Checking the files ..."}; break;
    default: break;
    }
    cfg.save(cacheDir + "launcher.ini");
}

void Launcher::handle(const SDL_Event& e, SDL_Window* window)
{
    int lw = 1, lh = 1, pw = 1, ph = 1;
    if (window) {
        SDL_GetWindowSize(window, &lw, &lh);
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
    }
    auto at = [&](float fx, float fy) {
        const int mx = int(fx * float(pw) / float(std::max(lw, 1))), my = int(fy * float(ph) / float(std::max(lh, 1)));
        int a = -1;
        for (const Hot& h : hot_)
            if (mx >= h.x0 && mx < h.x1 && my >= h.y0 && my < h.y1) a = h.action;
        return a;
    };
    const bool filesPage = (mode == Mode::Setup || mode == Mode::Repair) && step_ == kFiles;
    if (e.type == SDL_EVENT_DROP_POSITION && filesPage) {   // a file dragged over: the zone under it lit
        const int a = at(e.drop.x, e.drop.y);
        dropHover_ = a == kZoneA || a == kFolderA ? 0 : a == kZoneP || a == kFolderP ? 1 : -1;
        return;
    }
    if (e.type == SDL_EVENT_DROP_COMPLETE) { dropHover_ = -1; return; }
    if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) {
        dropHover_ = -1;
        int which = -1;
        if (filesPage) {
            const int a = at(e.drop.x, e.drop.y);
            which = a == kZoneA || a == kFolderA ? 0 : a == kZoneP || a == kFolderP ? 1 : -1;
        }
        if (which < 0 && filesPage) which = zoneCopy(0).path.empty() ? 0 : 1;   // (dropped elsewhere: the empty one)
        if (which >= 0) checkZone(which, e.drop.data);
        return;
    }
    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        scroll_ = std::clamp(scroll_ - int(e.wheel.y * 40), 0, scrollMax_);
        return;
    }
    // linux (2026-11-03, the Steam Deck's Game Mode: no keyboard): a pad's A / Start = Enter, B / Back = Esc, the d-pad's
    // Up / Down = the arrows (the text pages scroll); the rest of the setup is the mouse's (the Deck's touch screen /
    // trackpad)
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        SDL_Keycode k = 0;
        switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_START: k = SDLK_RETURN; break;
        case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_BACK: k = SDLK_ESCAPE; break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: k = SDLK_UP; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: k = SDLK_DOWN; break;
        default: break;
        }
        if (k) {
            SDL_Event key{};
            key.type = SDL_EVENT_KEY_DOWN;
            key.key.key = k;
            key.key.down = true;
            handle(key, window);
        }
        return;
    }
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        const bool text = mode == Mode::WhatsNew || mode == Mode::Update;
        if (text && (e.key.key == SDLK_DOWN || e.key.key == SDLK_PAGEDOWN)) scroll_ = std::min(scroll_ + (e.key.key == SDLK_DOWN ? 40 : 300), scrollMax_);
        else if (text && (e.key.key == SDLK_UP || e.key.key == SDLK_PAGEUP)) scroll_ = std::max(scroll_ - (e.key.key == SDLK_UP ? 40 : 300), 0);
        else if (e.key.key == SDLK_ESCAPE) {
            if (mode == Mode::Update) click(kNotNow, window);
            else if (mode == Mode::WhatsNew) click(kContinue, window);
            else if (step_ == kWelcome || mode == Mode::Repair) pending_ = Result::Quit;
            else back();
        } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
            // Enter: the files known (a portable copy with data\, the input test): straight to the check and Play;
            // else the page's main button
            if (mode == Mode::WhatsNew) click(kContinue, window);
            else if (mode == Mode::Update) click(kNotNow, window);
            else if (!cfg.arcade.empty() && !cfg.pc.empty() && step_ != kInstall && step_ != kDone && installState_ != 1) click(kStart, window);
            else if (step_ == kDone) click(kPlay, window);
            else next(window);
        }
        return;
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION) {
        hover_ = at(e.motion.x, e.motion.y);
        return;
    }
    const bool down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN, up = e.type == SDL_EVENT_MOUSE_BUTTON_UP;
    if ((!down && !up) || e.button.button != SDL_BUTTON_LEFT)
        return;
    const int a = at(e.button.x, e.button.y);
    // as a Windows button: done on the release over the same button
    if (down) {
        pressed_ = a;
        return;
    }
    const int was = pressed_;
    pressed_ = -1;
    if (a >= 0 && a == was)
        click(a, window);
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
    // the setup's zones show the result (a failed check: the Game files page)
    for (int i = 0; i < 2; ++i) {
        if (zoneThread_[i].joinable()) zoneThread_[i].join();
        const Verdict v = i == 0 ? arcadeVerdict(cfg.arcade, a) : pcVerdict(cfg.pc, p);
        const std::string& path = i == 0 ? cfg.arcade : cfg.pc;
        std::lock_guard<std::mutex> l(zoneMutex_);
        zone_[i] = Zone{path, path, path.empty() ? 0 : std::get<0>(v), std::get<1>(v), std::get<2>(v), i == 1 ? musicTracks(path) : 0};
    }
    if (a.ok && p.ok)
        status.push_back("OK: the arcade ROMs (Revision A) and the PC game are ready.");
    else if (a.ok)
        status.push_back("CANNOT START: the PC game did not pass (the game needs both the arcade ROMs and the 1998 PC game).");
    else
        status.push_back("CANNOT START: the arcade ROMs did not pass.");
    if (!(a.ok && p.ok) && mode == Mode::Setup) {
        // the first start with nothing chosen: from the beginning; a failed check of known files: the Game files page
        step_ = cfg.arcade.empty() && cfg.pc.empty() ? kWelcome : kFiles;
    }
    return a.ok && p.ok;
}

Launcher::Result Launcher::frame(int ww, int wh)
{
    {
        std::lock_guard<std::mutex> lock(dialogMutex_);
        if (!dialogResult_.empty()) {
            if (dialogTarget_ == 3) installDir_ = installDirFor(dialogResult_);
            else checkZone(dialogTarget_ - 1, dialogResult_);   // (a new choice: checked at once)
            dialogResult_.clear();
        }
    }
    if (download_ && download_->state() == util::UpdateDownload::Verified) {   // the update: hand over and quit
        std::string msg;
        if (util::handOverUpdate(packageRoot, download_->zipPath(), restartArgs, msg)) {
            SDL_Log("update: %s (%s)", msg.c_str(), download_->zipPath().c_str());
            std::printf("UPDATE: downloaded and verified %s, handed over to the updater\n", download_->zipPath().c_str());
            std::fflush(stdout);
            download_.reset();
            pending_ = Result::Quit;
        } else {
            updateErr_ = "The updater could not start: " + msg;
            download_.reset();
        }
    }
    if (std::getenv("HOTD_UPDATE_AUTO") && download_ && download_->state() == util::UpdateDownload::Failed) {
        std::printf("UPDATE: failed: %s\n", download_->message().c_str());   // (the test: the reason, then quit)
        std::fflush(stdout);
        pending_ = Result::Quit;
        download_.reset();
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

namespace hotd::game {

int Launcher::zoneState(int which) const
{
    return zoneCopy(which).state;
}

} // namespace hotd::game
