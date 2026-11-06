// --input-test's gamepad / gyro / touchpad / light-gun cases (pads.h; docs/teaser_status/pads.md). SDL's virtual
// joysticks (SDL_AttachVirtualJoystick with a gamepad's buttons and axes, a gyro, an accelerometer and a touchpad)
// stand in for the pads: the same events as a real pad reach the game loop. Light guns: their raw input fed
// directly (the hidden test window gets no raw input). Frames kFirst..kLast of the input test's second game.
#pragma once

#include "game/pads.h"

#include <functional>
#include <memory>
#include <string>

namespace hotd::game {

class PadTest {
public:
    static constexpr int kFirst = 331, kLast = 1170;
    static constexpr int kChecks = 108;   // (relfix: the shake hold check left out) (601-630: the 4-seat section, 626: 4P steps with two seats (hangfix); 652-1166: padfeel, the aim's feel)   // the checks its cases make (expect / counts calls through Ctx), kFirst..kLast
    struct Ctx {
        Runtime& rt;
        GunState* guns;
        Pads& pads;
        LightGuns& lightGuns;
        LaunchConfig& cfg;
        Pause& pause;
        int lw, lh;
        std::function<void(bool, const std::string&)> expect;
        // shots P1 / P2, reloads P1 / P2, starts P1 / P2 this frame
        std::function<void(int, int, int, int, int, int, const char*)> counts;
        std::function<void(float x, float y)> desktopMotion;   // an SDL mouse motion of the desktop cursor (ID 0)
    };
    PadTest();
    ~PadTest();
    void before(int frame, Ctx& c);   // this frame's input (phase 0)
    void after(int frame, Ctx& c);    // this frame's checks (phase 1)
    bool noGameFrame(int frame) const;   // paused / calibrating: no game frame runs
    // padfix (2026-10-30, the user's DualSense notes): its own frames k = 0 .. kFixFrames - 1 (main: after the shot
    // flash's): the sticks past the window's edge and the off-screen reload without a recentre, the left stick's click,
    // Aim Sensitivity, Motion + stick held flat (relpad: was the Sideways grip; old launcher.ini files), the menu cursor from any
    // pad, Button Style's grey choices. The last frame checks that all kFixChecks checks ran
    // sens36 (GitHub #36): Stick Sensitivity (was Aim Sensitivity) in 3; from frame 90 each input's speed proven its own,
    // the menu cursor's two, an older launcher.ini's speeds converted
    static constexpr int kFixFrames = 414, kFixChecks = 32;
    void fixBefore(int k, Ctx& c);
    void fixAfter(int k, Ctx& c);
private:
    struct Impl;
    std::unique_ptr<Impl> m_;
};

} // namespace hotd::game
