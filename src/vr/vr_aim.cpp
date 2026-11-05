// VR mode: the gun's ray -> the arcade's 2D aim (vr_aim.h, docs/vr.md "Aiming")
#include "vr/vr_aim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hotd::vr {

AimResult aimFromRay(const GameCamera& cam, const float origin[3], const float dir[3], float dist, const float worldUp[3],
                     float reloadPitchDeg)
{
    AimResult a;
    float d[3] = {dir[0], dir[1], dir[2]};
    normalize3(d);
    for (int k = 0; k < 3; ++k)
        a.hit[k] = origin[k] + d[k] * dist;
    // pointing down: the ray's angle under the horizon
    const float below = -dot3(d, worldUp);
    a.down = below > std::sin(reloadPitchDeg * 3.14159265f / 180.0f);
    // the hit in the game camera's space (x right, y up, z ahead), projected as the arcade's screen (focal length)
    const Rigid toCam = cam.toWorld.inverse();
    float c[3];
    toCam.apply(a.hit, c);
    if (c[2] <= 1e-3f) {
        a.behind = true;
        a.x = c[0] >= 0 ? 4000.0f : -4000.0f;   // (far off the screen, on the side it went)
        a.y = 0;
        return a;
    }
    a.x = cam.focal * c[0] / c[2];
    a.y = cam.focal * c[1] / c[2];
    a.onScreen = !a.down && std::abs(a.x) < cam.halfW - 1e-3f && std::abs(a.y) < cam.halfH;
    return a;
}

void arcadeToWorld(const GameCamera& cam, float x, float y, float dist, float out[3])
{
    float d[3] = {x, y, cam.focal};
    normalize3(d);
    const float c[3] = {d[0] * dist, d[1] * dist, d[2] * dist};
    cam.toWorld.apply(c, out);
}

GameCamera gameCameraFrom(const float pos[3], float yaw, float pitch, float focal)
{
    // the renderer's Camera::basis (render/renderer.cpp): the same axes, so the aim agrees with the picture
    GameCamera g;
    const float f[3] = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    const float r[3] = {std::cos(yaw), 0.0f, -std::sin(yaw)};
    float u[3];
    cross3(f, r, u);
    for (int k = 0; k < 3; ++k) {
        g.toWorld.r[k * 3 + 0] = r[k];
        g.toWorld.r[k * 3 + 1] = u[k];
        g.toWorld.r[k * 3 + 2] = f[k];
        g.toWorld.t[k] = pos[k];
    }
    g.focal = focal;
    return g;
}

int selfTest(int& checks)
{
    int failed = 0;
    checks = 0;
    auto check = [&](bool ok, const char* what, double a = 0, double b = 0) {
        ++checks;
        if (!ok) {
            ++failed;
            std::printf("vr aim test FAILED: %s (%g, %g)\n", what, a, b);
        }
    };
    const float up[3] = {0, 1, 0};
    {   // straight ahead from the camera: the screen's centre
        const float p[3] = {0, 0, 0};
        const GameCamera g = gameCameraFrom(p, 0, 0, 496.0f);
        const float o[3] = {0, 0, 0}, d[3] = {0, 0, 1};
        const AimResult a = aimFromRay(g, o, d, 50.0f, up, 55.0f);
        check(std::abs(a.x) < 1e-3f && std::abs(a.y) < 1e-3f && a.onScreen, "centre", a.x, a.y);
    }
    // known arcade points from known cameras, the gun off the eye (a hand's offset), several distances: the ray from
    // the muzzle through the world point the camera sees at (x, y) maps back to (x, y)
    uint32_t seed = 12345;
    auto rnd = [&](float lo, float hi) {
        seed = seed * 1664525u + 1013904223u;
        return lo + (hi - lo) * float(seed >> 8) / float(1u << 24);
    };
    double worst = 0;
    for (int i = 0; i < 200; ++i) {
        const float p[3] = {rnd(-500, 500), rnd(-50, 50), rnd(-500, 500)};
        const GameCamera g = gameCameraFrom(p, rnd(-3.1f, 3.1f), rnd(-0.6f, 0.6f), rnd(300.0f, 700.0f));
        const float x = rnd(-240, 240), y = rnd(-185, 185), dist = rnd(15, 400);
        float target[3], muzzle[3];
        arcadeToWorld(g, x, y, dist, target);
        const float off[3] = {rnd(-4, 4), rnd(-4, 1), rnd(-1, 5)};   // the hand: right / below / ahead of the eye
        g.toWorld.apply(off, muzzle);
        float d[3] = {target[0] - muzzle[0], target[1] - muzzle[1], target[2] - muzzle[2]};
        const float len = std::sqrt(dot3(d, d));
        const AimResult a = aimFromRay(g, muzzle, d, len, up, 89.0f);
        const double e = std::max(std::abs(a.x - x), std::abs(a.y - y));
        worst = std::max(worst, e);
        check(e < 0.05 && a.onScreen, "round trip", x, a.x);
    }
    std::printf("vr aim test: round trip worst error %.5f arcade units (200 rays)\n", worst);
    {   // off the 4:3 screen at x = 300, on it in widescreen (half width 341)
        const float p[3] = {0, 0, 0};
        GameCamera g = gameCameraFrom(p, 0.3f, 0.1f, 496.0f);
        float t[3];
        arcadeToWorld(g, 300.0f, 20.0f, 80.0f, t);
        const AimResult a = aimFromRay(g, p, t, 80.0f, up, 55.0f);
        check(!a.onScreen && std::abs(a.x - 300.0f) < 0.01f, "off the 4:3 screen", a.x, a.y);
        g.halfW = 192.0f * 16.0f / 9.0f;
        const AimResult b = aimFromRay(g, p, t, 80.0f, up, 55.0f);
        check(b.onScreen, "on the 16:9 screen", b.x, b.y);
    }
    {   // pointing behind the camera, pointing down
        const float p[3] = {0, 0, 0}, back[3] = {0.1f, 0, -1}, down[3] = {0, -1, 0.3f};
        const GameCamera g = gameCameraFrom(p, 0, 0, 496.0f);
        const AimResult a = aimFromRay(g, p, back, 30.0f, up, 55.0f);
        check(a.behind && !a.onScreen, "behind", a.x, a.y);
        const AimResult b = aimFromRay(g, p, down, 30.0f, up, 55.0f);
        check(b.down && !b.onScreen, "down", b.x, b.y);
    }
    {   // the tracking space -> the player's space: an unturned head looks ahead (+z); turned 90 degrees left (right-
        // handed yaw +90) it looks along -x; units scaled, z mirrored
        Pose h;
        h.pos[0] = 0.1f, h.pos[1] = 0.2f, h.pos[2] = -0.5f;
        Rigid l = poseToLocal(h, 10.0f);
        float f[3];
        l.axis(2, f);
        check(std::abs(f[2] - 1) < 1e-5f && std::abs(l.t[2] - 5.0f) < 1e-5f && std::abs(l.t[0] - 1.0f) < 1e-5f, "pose ahead", f[2], l.t[2]);
        quatFromYawPitchRoll(3.14159265f / 2, 0, 0, h.rot);
        l = poseToLocal(h, 10.0f);
        l.axis(2, f);
        check(std::abs(f[0] + 1) < 1e-5f, "pose turned left", f[0], f[2]);
        float u[3];
        quatFromYawPitchRoll(0, 0.4f, 0, h.rot);   // looking up: forward gains +y
        l = poseToLocal(h, 10.0f);
        l.axis(2, f);
        l.axis(1, u);
        check(f[1] > 0.38f && u[1] > 0.9f, "pose looking up", f[1], u[1]);
    }
    std::printf("vr aim test: %d checks, %d failed\n", checks, failed);
    return failed;
}

} // namespace hotd::vr
