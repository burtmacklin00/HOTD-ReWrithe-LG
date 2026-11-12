// Key / button bindings (worker "keybind", 2026-10-20; ours: THOTD.EXE's keys were fixed). Per player (1-4): the
// keyboard keys (two each) of aim up / down / left / right, fire, reload, start and pause, and the gamepad buttons of
// fire, reload, start, pause, recentre, gyro hold and precise aim. Options > Controls > Key / Button Bindings: a row
// clicked waits for a key (or a pad button): a new one is added (keys: two at most, the second is replaced), one the
// action has already is taken off; Esc / a click / 5 s cancel; a key or button used twice is marked. Saved in
// launcher.ini (key1.fire=44,228 = scancodes; padbind1.fire=r2,r1,south) only where they differ from the defaults:
// a launcher.ini without them plays as before.
//   The defaults (today's keys): player 1: 1 start, P pause (Esc always pauses); player 2: arrows aim, Space / Right
//   Ctrl fire, Right Shift / Enter reload, 2 start; player 3: W A S D, F, R, 3; player 4: I K J L (keypad 8 5 4 6),
//   H (keypad 0), Y (keypad .), 4. A pad's defaults follow its control style (pads.h); "Auto" until one is changed.
// Fixed (not bindable): Esc (the pause, the capture's cancel), F1, F3, F8 (Photo Mode), F9, F11, F12, the Windows keys, Print Screen.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

namespace hotd::game {

constexpr int kBindPlayers = 4;

// the keyboard's actions
enum KeyAction { kActUp, kActDown, kActLeft, kActRight, kActFire, kActReload, kActStart, kActPause, kKeyActions };
constexpr int kKeySlots = 2;
struct KeyBinds {
    SDL_Scancode k[kKeyActions][kKeySlots] = {};
    bool has(int a, SDL_Scancode sc) const
    {
        return sc != SDL_SCANCODE_UNKNOWN && (k[a][0] == sc || k[a][1] == sc);
    }
    bool operator==(const KeyBinds& o) const;
};
KeyBinds defaultKeys(int player);
const char* keyActionName(int a);    // "Aim Up" ...
std::string keyName(SDL_Scancode sc);   // short: "Space", "R Ctrl", "Kp 0", "A"
std::string keyList(const KeyBinds& b, int a);   // "Space, R Ctrl" / "-"
// a key that cannot be bound (why: what it does), else null
const char* keyFixed(SDL_Scancode sc);
// the other place sc is bound on the keyboard (all players: one keyboard), "P2 Fire", or empty
std::string keyClash(const KeyBinds keys[kBindPlayers], int player, int action, SDL_Scancode sc);

// the gamepad's actions and inputs
enum PadAction { kPaFire, kPaReload, kPaStart, kPaPause, kPaRecentre, kPaGyroHold, kPaPrecise, kPadActions };
enum PadInput {
    kPiSouth, kPiEast, kPiWest, kPiNorth, kPiL1, kPiR1, kPiL2, kPiR2, kPiL3, kPiR3, kPiStart, kPiBack, kPiMisc,
    kPiPaddle1, kPiPaddle2, kPiPaddle3, kPiPaddle4, kPiUp, kPiDown, kPiLeft, kPiRight, kPadInputs
};
struct PadBinds {
    bool custom = false;               // false: the style's own buttons (pads.h), the masks unused
    uint32_t mask[kPadActions] = {};   // the inputs (1 << PadInput) of each action
    bool operator==(const PadBinds& o) const;
};
const char* padActionName(int a);    // "Fire" ...
int padInputOf(SDL_GamepadButton b);   // -1: not bindable (the touchpad's click: its own setting)
// the input's name as the pad's type labels it (Cross / A / B ...); UNKNOWN: Xbox-style
std::string padInputName(int in, int style);   // style: pad_prompts.h (SDL_GamepadType or ours)
std::string padList(uint32_t mask, int style);   // "RT, RB, A" / "-"
const char* padInputKey(int in);     // launcher.ini: "south" ...

// launcher.ini: the lines (each "\nkey..=" / "\npadbind..="), only the bindings that are not the defaults
std::string bindsIni(const KeyBinds keys[kBindPlayers], const PadBinds pads[kBindPlayers]);
// one launcher.ini line: true if it was a binding's (key1.fire= ... / padbind1.fire= ...)
bool bindsIniLine(const std::string& k, const std::string& v, KeyBinds keys[kBindPlayers], PadBinds pads[kBindPlayers]);

// the menu's "press a key / button" (one at a time). Times in ms (SDL_GetTicks; the input test passes its own)
class BindCapture {
public:
    static constexpr uint64_t kTimeoutMs = 5000;
    void start(int player, int action, bool pad, uint64_t nowMs);
    void cancel(const char* why);
    bool active() const { return player_ >= 0; }
    bool pad() const { return pad_; }
    int player() const { return player_; }
    int action() const { return action_; }
    int secondsLeft(uint64_t nowMs) const;
    // an event while active: true = consumed (keys, buttons, triggers, clicks). padDefaults: the player's pad's
    // buttons now (the first change of an Auto pad starts from them)
    bool event(const SDL_Event& e, KeyBinds keys[kBindPlayers], PadBinds& pad, const PadBinds& padDefaults,
               int padType, uint64_t nowMs);
    bool tick(uint64_t nowMs);   // true: it just timed out
    std::string message;         // the last result, for the page's Status row
    int changes = 0;             // bindings changed (the host saves)
private:
    int player_ = -1, action_ = 0;
    bool pad_ = false;
    uint64_t until_ = 0;
};

} // namespace hotd::game
