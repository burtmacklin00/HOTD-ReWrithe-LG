// --input-test's key / button binding cases (keybinds.h; worker "keybind"): the defaults are today's keys, launcher.ini
// writes nothing for them and reads its lines back, the menu's capture (synthetic SDL events handed to it directly: a
// key, a key taken off, Esc, the time-out, a fixed key, a key used twice, a pad button / trigger, a click), then in
// play: rebound keyboard keys (fire, reload, aim, start, pause) and a virtual pad's rebound buttons (SDL's virtual
// joystick, as pads_test.h). Frames kFirst..kLast of the input test's second game, after the pad cases.
#pragma once

#include "game/pads.h"

#include <functional>
#include <memory>
#include <string>

namespace hotd::game {

class BindTest {
public:
    static constexpr int kFirst = 1172, kLast = 1200;
    static constexpr int kChecks = 64;   // (vrprompts: + the VR controllers' styles) (padglyphs: + 22 families, Steam Input, the setting / icons, the pad cursor) the checks its cases make (expect / counts calls through Ctx)
    struct Ctx {
        Runtime& rt;
        GunState* guns;
        Pads& pads;
        LaunchConfig& cfg;
        Pause& pause;
        int lw, lh;
        std::function<void(bool, const std::string&)> expect;
        // shots P1 / P2, reloads P1 / P2, starts P1 / P2 this frame
        std::function<void(int, int, int, int, int, int, const char*)> counts;
        std::function<void(SDL_Scancode, bool)> key;   // a key event into the game's queue (down / up)
    };
    BindTest();
    ~BindTest();
    void before(int frame, Ctx& c);   // this frame's input (phase 0)
    void after(int frame, Ctx& c);    // this frame's checks (phase 1)
    bool noGameFrame(int frame) const;   // paused: no game frame runs
private:
    struct Impl;
    std::unique_ptr<Impl> m_;
};

} // namespace hotd::game
