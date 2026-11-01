// Key / button bindings (keybinds.h): the defaults, the names, launcher.ini, the menu's capture.
#include "game/keybinds.h"
#include "game/pad_prompts.h"
#include "util/utf8.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace hotd::game {

namespace {

const char* const kKeyIni[kKeyActions] = {"up", "down", "left", "right", "fire", "reload", "start", "pause"};
const char* const kPadIni[kPadActions] = {"fire", "reload", "start", "pause", "recentre", "gyrohold", "precise"};
const char* const kPadInIni[kPadInputs] = {"south", "east", "west", "north", "l1", "r1", "l2", "r2", "l3", "r3", "start",
                                           "back", "misc", "paddle1", "paddle2", "paddle3", "paddle4", "up", "down", "left", "right"};
// the SDL button of each input (L2 / R2: the trigger axes)
const SDL_GamepadButton kPadInButton[kPadInputs] = {
    SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_INVALID,
    SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_BACK,
    SDL_GAMEPAD_BUTTON_MISC1, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,
    SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    SDL_GAMEPAD_BUTTON_DPAD_RIGHT};

// padglyphs: each style's names for the inputs (PadInput order: south east west north, L1 R1 L2 R2 L3 R3, Start Back,
// Misc, paddles 1-4 (SDL's right 1, left 1, right 2, left 2), the d-pad), by SDL's positions: a Nintendo pad's south
// is its B; a GameCube's south its A, a Wii Remote's (sideways) its B (SDL's drivers map those by label)
const char* const* padNames(int style)
{
    using N = const char* const[kPadInputs];
#define DP "D-Up", "D-Down", "D-Left", "D-Right"
    static N k360 = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "Start", "Back", "Misc", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", DP};
    static N kOne = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "Menu", "View", "Share", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", DP};
    static N kElite = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "Menu", "View", "Share", "P1", "P3", "P2", "P4", DP};
    static N kPs3 = {"Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3", "Start", "Select", "Misc", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", DP};
    static N kPs4 = {"Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3", "Options", "Share", "Misc", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", DP};
    static N kPs5 = {"Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3", "Options", "Create", "Mic", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", DP};
    static N kEdge = {"Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3", "Options", "Create", "Mic", "Back R", "Back L", "Fn R", "Fn L", DP};
    static N kPro = {"B", "A", "Y", "X", "L", "R", "ZL", "ZR", "LS", "RS", "+", "-", "Capture", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", DP};
    static N kPair = {"B", "A", "Y", "X", "L", "R", "ZL", "ZR", "LS", "RS", "+", "-", "Capture", "SR (R)", "SL (L)", "SL (R)", "SR (L)", DP};
    static N kPro2 = {"B", "A", "Y", "X", "L", "R", "ZL", "ZR", "LS", "RS", "+", "-", "C", "GR", "GL", "Paddle 3", "Paddle 4", DP};
    // one Joy-Con held sideways: SL / SR its shoulders; the left one's face its arrows (sideways: Left at the bottom)
    static N kJcL = {"Left", "Down", "Up", "Right", "SL", "SR", "ZL", "ZR", "Stick", "RS", "-", "Back", "Capture", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", DP};
    static N kJcR = {"A", "X", "B", "Y", "SL", "SR", "ZL", "ZR", "Stick", "RS", "+", "Back", "Misc", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", DP};
    // GameCube: SDL a:A b:B x:X y:Y, Z its right shoulder, L / R the analog triggers, the C stick the right stick
    static N kGc = {"A", "B", "X", "Y", "ZL", "Z", "L", "R", "Stick", "C", "Start", "Back", "Misc", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", DP};
    // N64 (Switch Online): A / B and the C buttons (SDL 3.2's map; not checked on a real pad)
    static N kN64 = {"A", "C-Down", "B", "C-Left", "L", "R", "Z", "ZR", "Stick", "C-Up", "Start", "C-Right", "Capture", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", DP};
    static N kSnes = {"B", "A", "Y", "X", "L", "R", "ZL", "ZR", "LS", "RS", "Start", "Select", "Misc", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", DP};
    static N kWiiU = {"B", "A", "Y", "X", "L", "R", "ZL", "ZR", "LS", "RS", "+", "-", "Misc", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", DP};
    // Wii Remote sideways (SDL: B south, A east, 1 west, 2 north, + Start, - Back); the Nunchuk: C, Z
    static N kWii = {"B", "A", "1", "2", "C", "RB", "Z", "RT", "Stick", "RS", "+", "-", "Misc", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", DP};
    static N kSc = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "Start", "Back", "Misc", "R Grip", "L Grip", "Paddle 3", "Paddle 4", DP};
    static N kDeck = {"A", "B", "X", "Y", "L1", "R1", "L2", "R2", "L3", "R3", "Menu", "View", "...", "R4", "L4", "R5", "L5", DP};
    static N kGen = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "17", DP};
#undef DP
    switch (style) {
    case SDL_GAMEPAD_TYPE_XBOX360: return k360;
    case kPsXboxElite: return kElite;
    case SDL_GAMEPAD_TYPE_PS3: return kPs3;
    case SDL_GAMEPAD_TYPE_PS4: return kPs4;
    case SDL_GAMEPAD_TYPE_PS5: return kPs5;
    case kPsDualSenseEdge: return kEdge;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: return kPro;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return kPair;
    case kPsSwitch2Pro: return kPro2;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT: case kPsJoyCon2L: return kJcL;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: case kPsJoyCon2R: return kJcR;
    case kPsGameCube: return kGc;
    case kPsN64: return kN64;
    case kPsSnes: return kSnes;
    case kPsWiiUPro: return kWiiU;
    case kPsWiiRemote: return kWii;
    case kPsSteamController: return kSc;
    case kPsSteamDeck: return kDeck;
    case kPsGeneric: return kGen;
    default: return kOne;   // Xbox One / Series, STANDARD, UNKNOWN, a Steam Input pad Steam names no type for
    }
}

std::string join(const std::string& a, const std::string& b) { return a.empty() ? b : a + ", " + b; }

} // namespace

bool KeyBinds::operator==(const KeyBinds& o) const
{
    for (int a = 0; a < kKeyActions; ++a)
        for (int s = 0; s < kKeySlots; ++s)
            if (k[a][s] != o.k[a][s]) return false;
    return true;
}

bool PadBinds::operator==(const PadBinds& o) const
{
    if (custom != o.custom) return false;
    if (!custom) return true;
    for (int a = 0; a < kPadActions; ++a)
        if (mask[a] != o.mask[a]) return false;
    return true;
}

KeyBinds defaultKeys(int player)
{
    KeyBinds b;
    auto set = [&](int a, SDL_Scancode k0, SDL_Scancode k1 = SDL_SCANCODE_UNKNOWN) { b.k[a][0] = k0; b.k[a][1] = k1; };
    switch (player) {
    case 0:   // the mouse aims, fires and reloads; P (and Esc, fixed) = the pause
        // Light Gun Edition: 1 opens the pause menu too (the light gun cabinets' Start key), and has no Start of its
        // own: the trigger (a gun's or the mouse's) joins and continues (main.cpp)
        set(kActPause, SDL_SCANCODE_P, SDL_SCANCODE_1);
        break;
    case 1:
        set(kActUp, SDL_SCANCODE_UP);
        set(kActDown, SDL_SCANCODE_DOWN);
        set(kActLeft, SDL_SCANCODE_LEFT);
        set(kActRight, SDL_SCANCODE_RIGHT);
        set(kActFire, SDL_SCANCODE_SPACE, SDL_SCANCODE_RCTRL);
        set(kActReload, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_RETURN);
        set(kActStart, SDL_SCANCODE_2);
        break;
    case 2:   // the 4P mode (docs/coop4.md)
        set(kActUp, SDL_SCANCODE_W);
        set(kActDown, SDL_SCANCODE_S);
        set(kActLeft, SDL_SCANCODE_A);
        set(kActRight, SDL_SCANCODE_D);
        set(kActFire, SDL_SCANCODE_F);
        set(kActReload, SDL_SCANCODE_R);
        set(kActStart, SDL_SCANCODE_3);
        break;
    case 3:
        set(kActUp, SDL_SCANCODE_I, SDL_SCANCODE_KP_8);
        set(kActDown, SDL_SCANCODE_K, SDL_SCANCODE_KP_5);
        set(kActLeft, SDL_SCANCODE_J, SDL_SCANCODE_KP_4);
        set(kActRight, SDL_SCANCODE_L, SDL_SCANCODE_KP_6);
        set(kActFire, SDL_SCANCODE_H, SDL_SCANCODE_KP_0);
        set(kActReload, SDL_SCANCODE_Y, SDL_SCANCODE_KP_PERIOD);
        set(kActStart, SDL_SCANCODE_4);
        break;
    default:
        break;
    }
    return b;
}

const char* keyActionName(int a)
{
    static const char* const k[kKeyActions] = {"Aim Up", "Aim Down", "Aim Left", "Aim Right", "Fire", "Reload", "Start", "Pause"};
    return a >= 0 && a < kKeyActions ? k[a] : "";
}

std::string keyName(SDL_Scancode sc)
{
    if (sc == SDL_SCANCODE_UNKNOWN)
        return "-";
    std::string n = SDL_GetScancodeName(sc);
    if (n.empty())
        return "Key " + std::to_string(int(sc));
    if (sc == SDL_SCANCODE_RETURN) return "Enter";
    if (sc == SDL_SCANCODE_UP) return "Up";
    if (sc == SDL_SCANCODE_DOWN) return "Down";
    if (sc == SDL_SCANCODE_LEFT) return "Left";
    if (sc == SDL_SCANCODE_RIGHT) return "Right";
    if (n.rfind("Keypad ", 0) == 0) n = "Kp " + n.substr(7);
    else if (n.rfind("Right ", 0) == 0) n = "R " + n.substr(6);
    else if (n.rfind("Left ", 0) == 0) n = "L " + n.substr(5);
    return util::utf8Head(n, 10);   // (a key's name on a non-Latin layout: whole letters, misc18)
}

std::string keyList(const KeyBinds& b, int a)
{
    std::string s;
    for (int i = 0; i < kKeySlots; ++i)
        if (b.k[a][i] != SDL_SCANCODE_UNKNOWN) s = join(s, keyName(b.k[a][i]));
    return s.empty() ? "-" : s;
}

const char* keyFixed(SDL_Scancode sc)
{
    switch (sc) {
    case SDL_SCANCODE_ESCAPE: return "the pause";
    case SDL_SCANCODE_F1: return "developer lines";
    case SDL_SCANCODE_F3: return "network stats";
    case SDL_SCANCODE_F9: return "light-gun calibration";
    case SDL_SCANCODE_F11: return "fullscreen";
    case SDL_SCANCODE_F12: return "screenshot";
    case SDL_SCANCODE_LGUI: case SDL_SCANCODE_RGUI: case SDL_SCANCODE_APPLICATION: return "Windows";
    case SDL_SCANCODE_PRINTSCREEN: return "Windows";
    case SDL_SCANCODE_UNKNOWN: return "unknown key";
    default: return sc >= SDL_SCANCODE_COUNT ? "unknown key" : nullptr;
    }
}

std::string keyClash(const KeyBinds keys[kBindPlayers], int player, int action, SDL_Scancode sc)
{
    std::string s;
    for (int q = 0; q < kBindPlayers; ++q)
        for (int a = 0; a < kKeyActions; ++a)
            if ((q != player || a != action) && keys[q].has(a, sc))
                s = join(s, "P" + std::to_string(q + 1) + " " + keyActionName(a));
    return s;
}

const char* padActionName(int a)
{
    static const char* const k[kPadActions] = {"Fire", "Reload", "Start", "Pause", "Recentre", "Gyro Hold", "Precise Aim"};
    return a >= 0 && a < kPadActions ? k[a] : "";
}

int padInputOf(SDL_GamepadButton b)
{
    if (b == SDL_GAMEPAD_BUTTON_INVALID)
        return -1;
    for (int i = 0; i < kPadInputs; ++i)
        if (kPadInButton[i] == b) return i;
    return -1;   // the touchpad's click (its own setting), Guide (the system's)
}

std::string padInputName(int in, int style)
{
    if (in < 0 || in >= kPadInputs)
        return "?";
    return padNames(style)[in];
}

std::string padList(uint32_t mask, int type)
{
    std::string s;
    // the triggers and shoulders first (the usual shoot buttons), then the rest in their order
    static const int kOrder[kPadInputs] = {kPiR2, kPiL2, kPiR1, kPiL1, kPiSouth, kPiEast, kPiWest, kPiNorth, kPiL3, kPiR3, kPiStart,
                                           kPiBack, kPiMisc, kPiPaddle1, kPiPaddle2, kPiPaddle3, kPiPaddle4, kPiUp, kPiDown, kPiLeft, kPiRight};
    for (int in : kOrder)
        if (mask >> in & 1) s = join(s, padInputName(in, type));
    if (s.size() > 20)
        s = s.substr(0, 18) + "..";
    return s.empty() ? "-" : s;
}

const char* padInputKey(int in) { return in >= 0 && in < kPadInputs ? kPadInIni[in] : ""; }

std::string bindsIni(const KeyBinds keys[kBindPlayers], const PadBinds pads[kBindPlayers])
{
    std::string s;
    for (int p = 0; p < kBindPlayers; ++p) {
        const KeyBinds def = defaultKeys(p);
        for (int a = 0; a < kKeyActions; ++a) {
            if (keys[p].k[a][0] == def.k[a][0] && keys[p].k[a][1] == def.k[a][1])
                continue;
            s += "\nkey" + std::to_string(p + 1) + "." + kKeyIni[a] + "=";
            bool first = true;
            for (int i = 0; i < kKeySlots; ++i)
                if (keys[p].k[a][i] != SDL_SCANCODE_UNKNOWN) {
                    s += (first ? "" : ",") + std::to_string(int(keys[p].k[a][i]));
                    first = false;
                }
        }
        if (!pads[p].custom)
            continue;
        for (int a = 0; a < kPadActions; ++a) {
            s += "\npadbind" + std::to_string(p + 1) + "." + kPadIni[a] + "=";
            bool first = true;
            for (int in = 0; in < kPadInputs; ++in)
                if (pads[p].mask[a] >> in & 1) {
                    s += std::string(first ? "" : ",") + kPadInIni[in];
                    first = false;
                }
        }
    }
    return s;
}

bool bindsIniLine(const std::string& k, const std::string& v, KeyBinds keys[kBindPlayers], PadBinds pads[kBindPlayers])
{
    auto items = [&v]() {
        std::vector<std::string> l;
        std::string item;
        for (char ch : v + ",")
            if (ch == ',') { if (!item.empty()) l.push_back(item); item.clear(); }
            else if (ch != ' ' && ch != '\r') item += ch;
        return l;
    };
    if (k.size() > 5 && k.rfind("key", 0) == 0 && k[3] >= '1' && k[3] <= '4' && k[4] == '.') {
        const int p = k[3] - '1';
        for (int a = 0; a < kKeyActions; ++a) {
            if (k.compare(5, std::string::npos, kKeyIni[a]) != 0)
                continue;
            SDL_Scancode got[kKeySlots] = {};
            int n = 0;
            for (const std::string& it : items()) {
                const int sc = std::atoi(it.c_str());
                if (n < kKeySlots && sc > 0 && sc < SDL_SCANCODE_COUNT && !keyFixed(SDL_Scancode(sc)))
                    got[n++] = SDL_Scancode(sc);
            }
            std::copy(got, got + kKeySlots, keys[p].k[a]);
            return true;
        }
        return true;   // (a key of a later build: ignored)
    }
    if (k.size() > 9 && k.rfind("padbind", 0) == 0 && k[7] >= '1' && k[7] <= '4' && k[8] == '.') {
        const int p = k[7] - '1';
        for (int a = 0; a < kPadActions; ++a) {
            if (k.compare(9, std::string::npos, kPadIni[a]) != 0)
                continue;
            uint32_t m = 0;
            for (const std::string& it : items())
                for (int in = 0; in < kPadInputs; ++in)
                    if (it == kPadInIni[in]) m |= 1u << in;
            pads[p].custom = true;
            pads[p].mask[a] = m;
            return true;
        }
        return true;
    }
    return false;
}

void BindCapture::start(int player, int action, bool pad, uint64_t nowMs)
{
    player_ = player;
    action_ = action;
    pad_ = pad;
    until_ = nowMs + kTimeoutMs;
    message = pad ? "Press a pad button (Esc: cancel)" : "Press a key (Esc: cancel)";
}

void BindCapture::cancel(const char* why)
{
    if (player_ >= 0)
        message = why;
    player_ = -1;
}

int BindCapture::secondsLeft(uint64_t nowMs) const
{
    return active() && nowMs < until_ ? int((until_ - nowMs + 999) / 1000) : 0;
}

bool BindCapture::tick(uint64_t nowMs)
{
    if (!active() || nowMs < until_)
        return false;
    cancel("Timed out: nothing changed");
    return true;
}

bool BindCapture::event(const SDL_Event& e, KeyBinds keys[kBindPlayers], PadBinds& pad, const PadBinds& padDefaults,
                        int padType, uint64_t nowMs)
{
    if (!active())
        return false;
    if (tick(nowMs))
        return false;
    const std::string who = "P" + std::to_string(player_ + 1) + " ";
    auto padToggle = [&](int in) {
        if (!pad.custom) {   // the first change of an Auto pad: from the buttons it has now
            pad = padDefaults;
            pad.custom = true;
        }
        const uint32_t bit = 1u << in;
        const std::string n = padInputName(in, padType);
        uint32_t& m = pad.mask[action_];
        if (m & bit) {
            m &= ~bit;
            message = n + " taken off " + who + "Pad " + padActionName(action_);
        } else {
            m |= bit;
            std::string also;
            for (int a = 0; a < kPadActions; ++a)
                if (a != action_ && (pad.mask[a] & bit)) also = join(also, padActionName(a));
            message = also.empty() ? n + " is now " + who + "Pad " + padActionName(action_)
                                   : n + ": also " + also + "!";
        }
        ++changes;
        player_ = -1;
    };
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN: {
        if (e.key.repeat)
            return true;
        const SDL_Scancode sc = e.key.scancode;
        if (sc == SDL_SCANCODE_ESCAPE) {
            cancel("Cancelled");
            return true;
        }
        if (pad_)
            return true;   // (waiting for a pad button: keys other than Esc do nothing)
        if (const char* f = keyFixed(sc)) {
            message = keyName(sc) + " is fixed (" + f + ")";
            player_ = -1;
            return true;
        }
        SDL_Scancode* k = keys[player_].k[action_];
        const std::string n = keyName(sc);
        if (k[0] == sc || k[1] == sc) {   // one it has: taken off
            if (k[0] == sc) k[0] = k[1];
            k[1] = SDL_SCANCODE_UNKNOWN;
            message = n + " taken off " + who + keyActionName(action_);
        } else {
            if (k[0] == SDL_SCANCODE_UNKNOWN) k[0] = sc;
            else k[1] = sc;   // (two at most: the second replaced)
            const std::string also = keyClash(keys, player_, action_, sc);
            message = also.empty() ? n + " is now " + who + keyActionName(action_) : n + ": also " + also + "!";
        }
        ++changes;
        player_ = -1;
        return true;
    }
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        cancel("Cancelled");
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        const SDL_GamepadButton b = SDL_GamepadButton(e.gbutton.button);
        if (!pad_) {   // waiting for a key: the pad's East / Back cancel
            if (b == SDL_GAMEPAD_BUTTON_EAST || b == SDL_GAMEPAD_BUTTON_BACK) cancel("Cancelled");
            return true;
        }
        const int in = padInputOf(b);
        if (in < 0) {
            message = b == SDL_GAMEPAD_BUTTON_TOUCHPAD ? "The touchpad click: its own setting (Controller page)"
                                                      : "That button cannot be bound";
            player_ = -1;
            return true;
        }
        padToggle(in);
        return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (pad_ && (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) &&
            e.gaxis.value > 16000)
            padToggle(e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ? kPiR2 : kPiL2);
        return true;   // (the sticks do not move the menu meanwhile)
    default:
        return false;
    }
}

} // namespace hotd::game
