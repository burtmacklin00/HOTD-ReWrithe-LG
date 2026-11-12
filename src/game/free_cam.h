// A free camera of our own (draws only): the cut-scene viewer's Free Cam (game/scene_view.h, user 2026-10-22, for
// trailer footage) and Photo Mode (game/photo_mode.h) share it. It starts where the game's camera is and is drawn
// through GameView::camOverride; the game, its camera and its state are never touched. Moved every rendered frame by
// real time: keys W A S D / arrows, Q / E down / up, Shift x4 / Ctrl x0.25, the mouse's drag and wheel (the field of
// view), a pad's sticks / triggers / shoulders (main.cpp freeCamInput gathers them).
#pragma once

#include "render/renderer.h"

namespace hotd::game {

struct FreeCam {
    bool on = false;
    bool fast = false;   // ipolish: a pad's left-stick click: the speed x4 until clicked again (off: off)
    bool slow = false;   // C / Back: 1/4 speed, eased in and out (cinematic shots)
    Camera camera;       // its place, yaw / pitch (Camera::basis) and field of view
    struct Input {
        float move[3] = {};   // right, up, forward: -1..1 each (keys WASD / QE / arrows, the left stick, the triggers)
        float look[2] = {};   // the turn this frame: right, up (radians: the mouse drag, the right stick x dt)
        float zoom = 0;       // wheel notches: + narrower (zoom in)
        bool fast = false, fine = false;   // Shift x4, Ctrl x0.25
    };
    static constexpr float kSpeed = 60.0f;   // world units a second (the arcade's: a body is about 17 high)
    // rpolish (user 2026-10-26: "near-plane clipping cuts foreground bodies", f14218): close up, Draw Depth's Arcade
    // Sort (one depth per polygon, the Model 2's sort keys, made for the game's own cameras) drew a body's big polygons
    // over its nearer parts (a jacket over the head, a hand through the gun). The free camera draws with per-pixel
    // depth whatever Draw Depth says, and a near plane of 0.1 (the game's 1 put everything nearer at one depth: depth
    // clamp). Only here: the game's own camera keeps the setting and 1 (the decal layers' depth precision)
    static constexpr float kNear = 0.1f;
    static constexpr float kFovMin = 0.087f, kFovMax = 1.92f;   // 5 .. 110 degrees

    // on, from `from` (the game's camera as last drawn); off
    void start(const Camera& from);
    void stop();
    void toggle(const Camera& from) { if (on) stop(); else start(from); }
    void step(const Input& in, float dt);

private:
    float slowMove_[3] = {}, slowLook_[2] = {};   // (the eased input)
};

} // namespace hotd::game
