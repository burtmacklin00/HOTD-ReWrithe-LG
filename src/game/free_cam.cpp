#include "game/free_cam.h"

#include <algorithm>
#include <cmath>

namespace hotd::game {

void FreeCam::start(const Camera& from)
{
    on = true;
    fast = slow = false;
    for (float& f : slowMove_) f = 0;
    for (float& f : slowLook_) f = 0;
    camera = from;
    camera.nearZ = kNear;
    camera.perPixelDepth = true;
}

void FreeCam::stop()
{
    on = fast = slow = false;
    for (float& f : slowMove_) f = 0;
    for (float& f : slowLook_) f = 0;
}

void FreeCam::step(const Input& in, float dt)
{
    if (!on)
        return;
    dt = std::clamp(dt, 0.0f, 0.1f);   // (a long frame, a breakpoint: no jump)
    Input s = in;
    if (slow) {   // a quarter of the move and the turn, eased (time constant 0.45 s: starts and stops gently)
        const float a = 1.0f - std::exp(-dt / 0.45f);
        for (int k = 0; k < 3; ++k)
            slowMove_[k] += (in.move[k] * 0.25f - slowMove_[k]) * a, s.move[k] = slowMove_[k];
        for (int k = 0; k < 2; ++k)
            slowLook_[k] += (in.look[k] * 0.25f - slowLook_[k]) * a, s.look[k] = slowLook_[k];
    } else {
        for (int k = 0; k < 3; ++k) slowMove_[k] = in.move[k];
        const float an = 1.0f - std::exp(-dt / 0.06f);   // the mouse look eased a little too (the turn is kept, only spread over a few frames)
        for (int k = 0; k < 2; ++k)
            slowLook_[k] += (in.look[k] - slowLook_[k]) * an, s.look[k] = slowLook_[k];
    }
    Camera& c = camera;
    c.yaw += s.look[0];
    c.pitch = std::clamp(c.pitch + s.look[1], -1.55f, 1.55f);
    // the wheel: the field of view a notch at a time (10 % narrower / wider), 5-110 degrees
    if (in.zoom != 0)
        c.fovY = std::clamp(c.fovY * std::pow(0.9f, in.zoom), kFovMin, kFovMax);
    float right[3], up[3], fwd[3];
    c.basis(right, up, fwd);
    const float v = kSpeed * dt * (s.fast ? 4.0f : 1.0f) * (s.fine ? 0.25f : 1.0f);
    for (int k = 0; k < 3; ++k)
        c.pos[k] += v * (s.move[0] * right[k] + s.move[1] * up[k] + s.move[2] * fwd[k]);
}

} // namespace hotd::game
