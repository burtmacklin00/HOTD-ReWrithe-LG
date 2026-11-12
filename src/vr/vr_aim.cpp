// VR mode: the gun's ray -> the arcade's 2D aim (vr_aim.h, docs/vr.md "Aiming")
#include "vr/vr_aim.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

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

Pull pullAction(const AimResult& a, float halfW, float halfH, int policy)
{
    if (a.onScreen)
        return Pull::Shoot;
    if (policy == 1 || a.down)
        return Pull::Reload;
    // below the screen: under the bottom edge, |x| inside the screen's half width (+ a quarter) plus how far below it
    // is (a wedge: aiming at the floor a little to the side still reloads; a point beside you, whose projection runs
    // off to huge x, does not)
    const float below = -a.y - halfH;
    if (!a.behind && below >= 0.0f && std::abs(a.x) <= halfW * 1.25f + below)
        return Pull::Reload;
    return Pull::Ignore;
}

bool rayBox(const float o[3], const float d[3], const float lo[3], const float hi[3], float& t0, float& t1)
{
    for (int a = 0; a < 3 && t0 <= t1; ++a) {
        if (std::abs(d[a]) < 1e-12f) {
            if (o[a] < lo[a] || o[a] > hi[a])
                return false;
            continue;
        }
        float ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a];
        if (ta > tb)
            std::swap(ta, tb);
        t0 = std::max(t0, ta), t1 = std::min(t1, tb);
    }
    return t0 <= t1;
}

// the ray into a placement's own space through rot's inverse (a linear map keeps the ray's parameter t)
static bool rayToLocal(const float o[3], const float d[3], const float rot[9], const float pos[3], float lo3[3], float ld[3])
{
    const float* m = rot;
    const float det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) < 1e-12f)
        return false;
    const float k = 1.0f / det;
    const float inv[9] = {(m[4] * m[8] - m[5] * m[7]) * k, (m[2] * m[7] - m[1] * m[8]) * k, (m[1] * m[5] - m[2] * m[4]) * k,
                          (m[5] * m[6] - m[3] * m[8]) * k, (m[0] * m[8] - m[2] * m[6]) * k, (m[2] * m[3] - m[0] * m[5]) * k,
                          (m[3] * m[7] - m[4] * m[6]) * k, (m[1] * m[6] - m[0] * m[7]) * k, (m[0] * m[4] - m[1] * m[3]) * k};
    const float r[3] = {o[0] - pos[0], o[1] - pos[1], o[2] - pos[2]};
    for (int i = 0; i < 3; ++i) {
        lo3[i] = inv[i * 3] * r[0] + inv[i * 3 + 1] * r[1] + inv[i * 3 + 2] * r[2];
        ld[i] = inv[i * 3] * d[0] + inv[i * 3 + 1] * d[1] + inv[i * 3 + 2] * d[2];
    }
    return true;
}

bool rayPlacedTriangles(const float o[3], const float d[3], const float rot[9], const float pos[3], const void* verts,
                        size_t stride, size_t vertexCount, float minT, float& best)
{
    float lo[3], ld[3];
    if (!rayToLocal(o, d, rot, pos, lo, ld))
        return false;
    const uint8_t* base = static_cast<const uint8_t*>(verts);
    bool hit = false;
    for (size_t i = 0; i + 2 < vertexCount; i += 3) {   // (Moeller-Trumbore, both sides, as rayTriangles)
        const float* a = reinterpret_cast<const float*>(base + i * stride);
        const float* b = reinterpret_cast<const float*>(base + (i + 1) * stride);
        const float* c = reinterpret_cast<const float*>(base + (i + 2) * stride);
        const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const float q[3] = {ld[1] * e2[2] - ld[2] * e2[1], ld[2] * e2[0] - ld[0] * e2[2], ld[0] * e2[1] - ld[1] * e2[0]};
        const float det = e1[0] * q[0] + e1[1] * q[1] + e1[2] * q[2];
        if (std::abs(det) < 1e-12f)
            continue;
        const float inv = 1.0f / det;
        const float s[3] = {lo[0] - a[0], lo[1] - a[1], lo[2] - a[2]};
        const float u = (s[0] * q[0] + s[1] * q[1] + s[2] * q[2]) * inv;
        if (u < 0.0f || u > 1.0f)
            continue;
        const float r[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
        const float v = (ld[0] * r[0] + ld[1] * r[1] + ld[2] * r[2]) * inv;
        if (v < 0.0f || u + v > 1.0f)
            continue;
        const float t = (e2[0] * r[0] + e2[1] * r[1] + e2[2] * r[2]) * inv;
        if (t > minT && t < best)
            best = t, hit = true;
    }
    return hit;
}

bool rayPlacedBox(const float o[3], const float d[3], const float rot[9], const float pos[3], const float lo[3],
                  const float hi[3], float minT, float maxT, float& t, float* tExit)
{
    float lo3[3], ld[3];
    if (!rayToLocal(o, d, rot, pos, lo3, ld))
        return false;
    float t0 = minT, t1 = maxT;
    if (!rayBox(lo3, ld, lo, hi, t0, t1))
        return false;
    t = t0;
    if (tExit) *tExit = t1;
    return true;
}

bool rayTriangles(const float o[3], const float d[3], const float* tris, size_t count, float minT, float& best)
{
    bool hit = false;
    for (size_t i = 0; i < count; ++i) {   // (Moeller-Trumbore, both sides)
        const float* p = tris + i * 12;
        const float e1[3] = {p[3] - p[0], p[4] - p[1], p[5] - p[2]}, e2[3] = {p[6] - p[0], p[7] - p[1], p[8] - p[2]};
        const float q[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
        const float det = e1[0] * q[0] + e1[1] * q[1] + e1[2] * q[2];
        if (std::abs(det) < 1e-10f)
            continue;
        const float inv = 1.0f / det;
        const float s[3] = {o[0] - p[0], o[1] - p[1], o[2] - p[2]};
        const float u = (s[0] * q[0] + s[1] * q[1] + s[2] * q[2]) * inv;
        if (u < 0.0f || u > 1.0f)
            continue;
        const float r[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
        const float v = (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) * inv;
        if (v < 0.0f || u + v > 1.0f)
            continue;
        const float t = (e2[0] * r[0] + e2[1] * r[1] + e2[2] * r[2]) * inv;
        if (t > minT && t < best)
            best = t, hit = true;
    }
    return hit;
}

float HudDistance::update(float target, float dt)
{
    if (now < 0.0f) {
        hold = 0.0f;
        return now = target;
    }
    dt = std::clamp(dt, 0.0f, 0.1f);
    if (target < now) {   // nearer: quickly (time constant ~0.04 s), and hold there
        now += (target - now) * std::min(1.0f, dt * 25.0f);
        hold = 0.5f;
    } else if (hold > 0.0f) {
        hold -= dt;
    } else {   // back out slowly (~0.25 s time constant)
        now += (target - now) * std::min(1.0f, dt * 4.0f);
    }
    return now;
}

float chooseRefreshRate(const float* rates, int count, int want, bool standalone)
{
    if (count <= 0)
        return 0.0f;
    if (want <= 0) {
        if (!standalone)
            return 0.0f;
        want = 90;
    }
    float best = 0.0f;
    if (want == 90 && standalone) {   // (auto on a standalone headset: 90, else the highest under it)
        for (int i = 0; i < count; ++i)
            if (rates[i] <= 90.5f && rates[i] > best) best = rates[i];
        if (best > 0.0f) return best;
    }
    for (int i = 0; i < count; ++i)
        if (best == 0.0f || std::abs(rates[i] - float(want)) < std::abs(best - float(want))) best = rates[i];
    return best;
}

bool recentrePose(const Pose& head, bool orientationValid, bool positionValid, const float keep[3], Pose& out)
{
    if (!orientationValid)
        return false;
    const float* q = head.rot;   // the yaw of the head's -z (right-handed, +y up)
    const float yaw = std::atan2(2.0f * (q[0] * q[2] + q[3] * q[1]), 1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1]));
    out = Pose{};
    out.rot[1] = std::sin(yaw * 0.5f), out.rot[3] = std::cos(yaw * 0.5f);
    for (int k = 0; k < 3; ++k) out.pos[k] = positionValid ? head.pos[k] : keep[k];
    out.valid = true;
    return true;
}

bool AwayClock::update(bool shown, double now, double seconds)
{
    if (!started_) {   // (the start: the headset not showing us yet counts as away: its first picture after > `seconds`
                       // recentres too, harmless: LOCAL's origin is the head's then anyway)
        started_ = true;
        away_ = !shown, since_ = now;
        return false;
    }
    if (!shown) {
        if (!away_) away_ = true, since_ = now;
        return false;
    }
    if (!away_)
        return false;
    away_ = false;
    return seconds > 0.0 && now - since_ > seconds;
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

// the turn from rotation a to b (unit quaternions x, y, z, w): b x conj(a) (the world's axes), its angle (radians) and
// unit axis
static float turnBetween(const float a[4], const float b[4], float axis[3])
{
    float q[4] = {b[3] * -a[0] + b[0] * a[3] + b[1] * -a[2] - b[2] * -a[1],
                  b[3] * -a[1] - b[0] * -a[2] + b[1] * a[3] + b[2] * -a[0],
                  b[3] * -a[2] + b[0] * -a[1] - b[1] * -a[0] + b[2] * a[3],
                  b[3] * a[3] - b[0] * -a[0] - b[1] * -a[1] - b[2] * -a[2]};
    if (q[3] < 0) q[0] = -q[0], q[1] = -q[1], q[2] = -q[2], q[3] = -q[3];
    const float s = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
    for (int k = 0; k < 3; ++k) axis[k] = s > 1e-9f ? q[k] / s : 0.0f;
    return 2.0f * std::atan2(s, q[3]);
}

bool ShakeDetector::update(const Pose& grip, int64_t timeNs, float g)
{
    if (!grip.valid) {
        reset();
        return false;
    }
    const float kDeg = 180.0f / 3.14159265f;
    const double t = double(timeNs) * 1e-9;
    const double dt = t - t_;
    if (have_ && std::abs(dt) < 0.002) return false;   // (the same frame again: nothing new)
    float v[3] = {0, 0, 0}, axis[3];
    float turn = 0;
    if (have_ && dt > 0) {
        for (int k = 0; k < 3; ++k) v[k] = float((grip.pos[k] - pos_[k]) / dt);
        turn = turnBetween(rot_, grip.rot, axis);
    }
    const float speed = std::sqrt(dot3(v, v)), rate = have_ && dt > 0 ? float(turn * kDeg / dt) : 0.0f;
    // (the first frame, time going back, a gap over 0.1 s, a tracking jump: the history starts again)
    if (!have_ || dt < 0 || dt > 0.1 || speed > 6.0f || rate > 2500.0f) {
        nv_ = 0, jolt_ = false, twist_ = 0;
        for (int k = 0; k < 3; ++k) pos_[k] = grip.pos[k];
        std::copy(grip.rot, grip.rot + 4, rot_);
        t_ = t, have_ = true;
        return false;
    }
    // the acceleration: the change of the velocity between the frames' middles, smoothed (time constant 12 ms)
    const double vt = t - 0.5 * dt;
    float acc[3] = {0, 0, 0};
    if (nv_ > 0) {
        const float span = float(std::max(vt - velT_, 0.002));
        const float a = 1.0f - std::exp(-span / 0.012f);
        for (int k = 0; k < 3; ++k) acc_[k] += a * ((v[k] - vel_[k]) / span - acc_[k]);
        std::copy(acc_, acc_ + 3, acc);
    } else {
        for (int k = 0; k < 3; ++k) acc_[k] = 0;
    }
    float prevPos[3], prevRot[4];
    std::copy(pos_, pos_ + 3, prevPos);
    std::copy(rot_, rot_ + 4, prevRot);
    for (int k = 0; k < 3; ++k) pos_[k] = grip.pos[k], vel_[k] = v[k];
    std::copy(grip.rot, grip.rot + 4, rot_);
    t_ = t, velT_ = vt, nv_ = std::min(nv_ + 1, 2);
    if (t < quietUntil_) {
        jolt_ = false, twist_ = 0;
        return false;
    }
    bool shook = false;
    // the jolt: the start (|a| over the level), then the stop (half of it the other way)
    const float A = g * 9.81f, am = std::sqrt(dot3(acc, acc));
    if (jolt_ && t - joltT_ > 0.25) jolt_ = false;
    if (jolt_ && dot3(v, joltDir_) >= 0.4f) ++joltRun_;   // (a one-frame jump of the tracking moves on one frame only)
    if (jolt_ && t - joltT_ >= 0.035 && dot3(acc, joltDir_) <= -0.5f * A) {
        jolt_ = false;
        float d[3] = {grip.pos[0] - joltPos_[0], grip.pos[1] - joltPos_[1], grip.pos[2] - joltPos_[2]}, ax[3];
        const float travel = std::sqrt(dot3(d, d)), aimTurn = turnBetween(joltRot_, grip.rot, ax);
        shook = joltRun_ >= 2 && travel >= 0.03f && aimTurn * 0.7f <= travel;   // (else: an aim move with the arm)
        if (shook) kind_ = "jolt";
    }
    if (!jolt_ && !shook && nv_ >= 2 && am >= A) {
        jolt_ = true, joltT_ = t, joltRun_ = dot3(v, acc) >= 0.4f * am ? 1 : 0;
        for (int k = 0; k < 3; ++k) joltDir_[k] = acc[k] / am, joltPos_[k] = prevPos[k];
        std::copy(prevRot, prevRot + 4, joltRot_);
    }
    // the twist: a swing of the wrist and back
    const float W = 350.0f * g / 3.0f, step = turn * kDeg;
    if (twist_ == 1) {
        if (rate >= 0.5f * W && dot3(axis, axis_) > 0.5f) {
            swept_ += step, ++swingN_, sweptMax_ = std::max(sweptMax_, step);
        } else {
            // each way: 2 frames or more, none of them 65 % of it or more: a jump is one frame (the preview's gun
            // snapping to the next target: a frame's jump and its depth's correction the next)
            twist_ = swept_ >= 20.0f && swingN_ >= 2 && sweptMax_ < 0.65f * swept_ ? 2 : 0;
            twistT_ = t - dt, back_ = 0, backN_ = 0, backMax_ = 0;
        }
    }
    if (twist_ == 2) {
        if (t - twistT_ > 0.15 + 1e-6 && back_ == 0) {
            twist_ = 0;
        } else if (rate >= 0.75f * W && dot3(axis, axis_) < -0.5f) {
            back_ += step, ++backN_, backMax_ = std::max(backMax_, step);
            if (back_ >= 15.0f && backN_ >= 2 && backMax_ < 0.65f * back_) shook = true, kind_ = "twist";
        } else if (back_ > 0) {
            twist_ = 0;   // (the way back stopped short)
        }
    }
    if (twist_ == 0 && !shook && rate >= W) {
        twist_ = 1, swept_ = step, swingN_ = 1, sweptMax_ = step;
        std::copy(axis, axis + 3, axis_);
    }
    if (shook) {
        jolt_ = false, twist_ = 0;
        quietUntil_ = t + 0.7;
    }
    return shook;
}

bool FlickDetector::update(const Pose& grip, int64_t timeNs, float degPerSec)
{
    if (!grip.valid) {
        reset();
        return false;
    }
    const float kDeg = 180.0f / 3.14159265f;
    const double t = double(timeNs) * 1e-9;
    const double dt = n_ > 0 ? t - time_[head_] : 0;
    if (n_ > 0 && std::abs(dt) < 0.002) return false;   // (the same frame again: nothing new)
    float m[9];
    quatToMatrix(grip.rot, m);
    const float d[3] = {-m[2], -m[5], -m[8]};   // the muzzle: the grip's -z
    // the turn since the newest entry in the grip's own axes: dq = conj(prev) x now (x: pitch, muzzle up; y: yaw)
    const float* a = rot_;
    const float* b = grip.rot;
    float q[4] = {a[3] * b[0] - a[0] * b[3] - a[1] * b[2] + a[2] * b[1],
                  a[3] * b[1] + a[0] * b[2] - a[1] * b[3] - a[2] * b[0],
                  a[3] * b[2] - a[0] * b[1] + a[1] * b[0] - a[2] * b[3],
                  a[3] * b[3] + a[0] * b[0] + a[1] * b[1] + a[2] * b[2]};
    if (q[3] < 0) q[0] = -q[0], q[1] = -q[1], q[2] = -q[2], q[3] = -q[3];
    const float sv = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
    const float angle = 2.0f * std::atan2(sv, q[3]) * kDeg;
    const float rate = dt > 0 ? float(angle / dt) : 0;
    auto push = [&] {
        head_ = (head_ + 1) % kHist;
        for (int k = 0; k < 3; ++k) dir_[head_][k] = d[k];
        time_[head_] = t;
        n_ = std::min(n_ + 1, kHist);
        std::copy(grip.rot, grip.rot + 4, rot_);
    };
    // (the first frame, time going back, a gap over 0.1 s, a tracking jump: no rate, the history starts again)
    if (n_ == 0 || dt < 0 || dt > 0.1 || rate > 2500.0f) {
        n_ = 0, fastRun_ = 0, armed_ = false, fastT_ = -1;
        push();
        return false;
    }
    const float scale = sv > 1e-9f ? rate / sv : 0;
    const float pitch = q[0] * scale, yaw = q[1] * scale;   // deg/s about the grip's x / y
    push();
    if (t < quietUntil_) {
        fastRun_ = 0, armed_ = false, fastT_ = -1;
        return false;
    }
    if (pitch >= degPerSec && pitch >= 2.0f * std::abs(yaw)) {
        ++fastRun_;
        if (fastRun_ >= 2) fastT_ = t;   // (vrgest: 2 time-stamped frames even over a dropped one; never 1 long frame: a
                                         // tracking jump over a gap looks like one)
    } else {
        fastRun_ = 0;
    }
    if (!armed_ && fastT_ >= 0 && t - fastT_ <= 0.2) {
        // the rise over the last 0.15 s: from an older entry to now, mostly upward
        const float elevNow = std::asin(std::clamp(d[1], -1.0f, 1.0f)) * kDeg;
        for (int i = 1; i < n_ && !armed_; ++i) {
            const int j = (head_ - i + kHist) % kHist;
            if (t - time_[j] > 0.2 + 1e-6) break;
            const float* o = dir_[j];
            const float rise = elevNow - std::asin(std::clamp(o[1], -1.0f, 1.0f)) * kDeg;
            if (rise < 20.0f) continue;
            const float total = std::acos(std::clamp(dot3(o, d), -1.0f, 1.0f)) * kDeg;
            const float side = std::sqrt(std::max(0.0f, total * total - rise * rise));
            if (side <= 0.5f * rise) armed_ = true, armT_ = t;
        }
    }
    if (armed_) {
        if (pitch < 0.25f * degPerSec) {   // stopped or coming back: the reload
            armed_ = false, fastRun_ = 0, fastT_ = -1;
            quietUntil_ = t + 0.7;
            return true;
        }
        if (t - armT_ > 0.35) armed_ = false, fastT_ = -1;   // still turning: a spin, not a flick
    }
    return false;
}

bool ReloadGestures::update(const Pose& grip, int64_t timeNs, bool shakeOn, float shakeMps, bool flickOn, float flickDps)
{
    const double t = double(timeNs) * 1e-9;
    const bool shook = shakeOn && shake_.update(grip, timeNs, shakeMps);
    if (!shakeOn) shake_.reset();
    const bool flicked = flickOn && flick_.update(grip, timeNs, flickDps);
    if (!flickOn) flick_.reset();
    if (!(shook || flicked) || t < quietUntil_) return false;
    last_ = flicked ? "flick" : std::strcmp(shake_.kind(), "twist") == 0 ? "shake (twist)" : "shake (jolt)";
    quietUntil_ = t + 0.7;   // (both detectors quiet: the rest of this gesture is not a second one)
    shake_.quiet(quietUntil_);
    flick_.quiet(quietUntil_);
    return true;
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
    {   // vr4: offscreen_reload: below (0) / any (1)
        const float p[3] = {0, 0, 0};
        const GameCamera g = gameCameraFrom(p, 0.2f, 0.05f, 496.0f);
        auto at = [&](float x, float y) {
            float t[3];
            arcadeToWorld(g, x, y, 60.0f, t);
            return aimFromRay(g, p, t, 60.0f, up, 55.0f);
        };
        check(pullAction(at(10, 20), 248, 192, 0) == Pull::Shoot, "pull on the screen");
        check(pullAction(at(600, 0), 248, 192, 0) == Pull::Ignore, "pull beside the screen: nothing");
        check(pullAction(at(600, 0), 248, 192, 1) == Pull::Reload, "pull beside the screen, any: reload");
        check(pullAction(at(0, 260), 248, 192, 0) == Pull::Ignore, "pull above the screen: nothing");
        check(pullAction(at(0, -250), 248, 192, 0) == Pull::Reload, "pull below the screen: reload");
        check(pullAction(at(330, -260), 248, 192, 0) == Pull::Reload, "pull below, a little to the side: reload");
        check(pullAction(at(2000, -230), 248, 192, 0) == Pull::Ignore, "pull far beside, a little low: nothing");
        const float back[3] = {0.1f, 0, -1}, down[3] = {0, -1, 0.3f};
        check(pullAction(aimFromRay(g, p, back, 30.0f, up, 55.0f), 248, 192, 0) == Pull::Ignore, "pull behind: nothing");
        check(pullAction(aimFromRay(g, p, down, 30.0f, up, 55.0f), 248, 192, 0) == Pull::Reload, "pull pointing down: reload");
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
    {   // shake-to-reload (90 Hz frames): a back-and-forth shake reloads once; slow aiming, a fast steady turn, one flick
        // and a gentle shake give none; a wrist shake counts; two shakes a second apart are two reloads
        const float pi = 3.14159265f, thr = 2.0f, flk = 300.0f;   // (vrgest: the defaults: shake 2 g, flick 300 deg/s)
        int lastN = 0;   // (vrgest: each case's count printed: the before / after table)
        auto gcheck = [&](bool ok, const char* what, double a = 0, double b = 0) {
            std::printf("vr gesture: %-40s %d\n", what, lastN);
            check(ok, what, a, b);
        };
        auto run = [&](auto motion, int tenths) {   // motion(t, pose); the shakes counted
            ShakeDetector d;
            int n = 0;
            for (int f = 0; f < tenths * 9; ++f) {
                const double t = f / 90.0;
                Pose p;
                p.valid = true;
                motion(t, p);
                if (d.update(p, int64_t(t * 1e9) + 5000000000LL, thr)) ++n;
            }
            lastN = n;
            return n;
        };
        // vrgest: a 3 Hz, 8 cm swing of the hand (2.9 g: was "a slow wobble", no reload) is a shake; aiming around, 1.5 Hz
        // 5 cm (0.45 g) is not
        gcheck(run([&](double t, Pose& p) { p.pos[0] = 0.08f * std::sin(2 * pi * 3 * float(t)); }, 30) >= 3, "3 Hz shake (+-8 cm), 3 s");   // (a reload each 0.7 s)
        gcheck(run([&](double t, Pose& p) { p.pos[0] = 0.05f * std::sin(2 * pi * 1.5f * float(t)); }, 30) == 0, "slow wobble (+-5 cm, 1.5 Hz)");
        for (float w : {4.0f, 7.0f})   // the gun swung round the player (arm 0.5 m) at 4 and 7 rad/s: one direction at a time
            gcheck(run([&](double t, Pose& p) {
                      const float a = w * float(t);
                      p.pos[0] = 0.5f * std::sin(a), p.pos[2] = -0.5f * std::cos(a);
                      quatFromYawPitchRoll(-a, 0, 0, p.rot);
                  }, 3) == 0, "steady turn", w);
        // one push out (0.4 m in 0.1 s: 4 m/s) and stop: vrgest: a jolt (from rest); from the very first frame its start
        // is not seen (none)
        gcheck(run([&](double t, Pose& p) { p.pos[2] = t < 0.1 ? float(-t * 4.0) : -0.4f; }, 30) == 0, "a push from the start, no return");
        gcheck(run([&](double t, Pose& p) { p.pos[2] = -0.4f * std::clamp(float(t) - 1.0f, 0.0f, 0.1f) * 10; }, 30) == 1, "one jolt out and stop");
        auto shake = [=](float amp, double t0) {   // 6 Hz up and down for half a second from t0
            return [=](double t, Pose& p) {
                if (t >= t0 && t < t0 + 0.5) p.pos[1] += amp * std::sin(2 * pi * 6 * float(t - t0));
            };
        };
        gcheck(run(shake(0.06f, 1.0), 30) == 1, "gentle shake");
        gcheck(run(shake(0.12f, 1.0), 30) == 1, "one shake = one reload");
        // a wrist shake: the gun turned about its grip +-0.6 rad at 6 Hz = 4.5 m/s at the muzzle
        gcheck(run([&](double t, Pose& p) {
                  if (t >= 1.0 && t < 1.5) quatFromYawPitchRoll(0, 0.6f * std::sin(2 * pi * 6 * float(t - 1.0)), 0, p.rot);
              }, 30) == 1, "wrist shake");
        gcheck(run([&](double t, Pose& p) {
                  shake(0.12f, 1.0)(t, p);
                  shake(0.12f, 2.0)(t, p);
              }, 40) == 2, "two shakes");
        {   // an untracked controller never shakes; a hitch (a jump over 0.2 s) is not a speed
            ShakeDetector d;
            Pose p;
            int n = 0;
            for (int f = 0; f < 90; ++f) {
                p.pos[1] = (f & 1) ? 0.2f : -0.2f;
                n += d.update(p, int64_t(f) * 11111111LL, thr) ? 1 : 0;
            }
            check(n == 0, "invalid pose");
            p.valid = true;
            p.pos[0] = 0;
            d.update(p, 1000000000LL, thr);
            p.pos[0] = 3;
            check(!d.update(p, 1200000000LL, thr), "hitch");
        }
        // vrflick: one-frame jumps there and back (tracking glitches; the preview's gun snapping between targets: its
        // shake reloads in --vr-preview-test --vr-dual before) are not swings
        gcheck(run([&](double t, Pose& p) {
                  const int f = int(t * 90 + 0.5);
                  if (f >= 90 && f % 6 == 0) p.pos[0] = (f / 6) % 2 ? 0.1f : -0.1f;
              }, 30) == 0, "one-frame jumps");
        // vrflick (user 2026-11-10): the wrist-flick reload with the shake on too (the defaults: ReloadGestures, the
        // shared cooldown), scripted 90 Hz poses. The gun held 0.3 m ahead, 0.25 m low; pitch / yaw in degrees
        const float deg = pi / 180.0f;
        auto runG = [&](auto motion, int tenths, bool shakeOn = true, bool flickOn = true) {
            ReloadGestures g;
            int n = 0;
            for (int f = 0; f < tenths * 9; ++f) {
                const double t = f / 90.0;
                Pose p;
                p.valid = true;
                p.pos[1] = -0.25f, p.pos[2] = -0.3f;
                motion(t, p);
                if (g.update(p, int64_t(t * 1e9) + 5000000000LL, shakeOn, thr, flickOn, flk)) ++n;
            }
            lastN = n;
            return n;
        };
        auto ease = [](double t, double t0, double len) {   // 0 -> 1 from t0 over len s (smoothstep: peak 1.5 x the mean)
            const double u = std::clamp((t - t0) / len, 0.0, 1.0);
            return float(u * u * (3 - 2 * u));
        };
        auto aimAt = [&](Pose& p, float yawDeg, float pitchDeg) { quatFromYawPitchRoll(yawDeg * deg, pitchDeg * deg, 0, p.rot); };
        // must reload once: a wrist flick (45 degrees up in 80 ms: peak 840 deg/s) held up; one that comes back down;
        // an arm flick (the forearm: 50 degrees in 0.1 s, the hand 0.25 m up)
        gcheck(runG([&](double t, Pose& p) { aimAt(p, 0, -5 + 45 * ease(t, 1.0, 0.08)); }, 30) == 1, "wrist flick");
        gcheck(runG([&](double t, Pose& p) {
                  aimAt(p, 10, -5 + 40 * ease(t, 1.0, 0.07) - 40 * ease(t, 1.1, 0.25));
              }, 30) == 1, "wrist flick and back");
        gcheck(runG([&](double t, Pose& p) {
                  const float u = ease(t, 1.0, 0.1);
                  aimAt(p, -15, -10 + 50 * u);
                  p.pos[1] += 0.25f * u, p.pos[2] += 0.1f * u;
              }, 30) == 1, "arm flick");
        gcheck(runG([&](double t, Pose& p) { aimAt(p, 0, -5 + 45 * ease(t, 1.0, 0.08)); }, 30, false, true) == 1, "flick, shake off");
        gcheck(runG([&](double t, Pose& p) { aimAt(p, 0, -5 + 45 * ease(t, 1.0, 0.08)); }, 30, true, false) == 0, "flick, flick off");
        // must not reload: fast yaw sweeps across targets (120 degrees in 0.15 s: 1200 deg/s, a little pitch wobble);
        // fast diagonal moves between targets (50 x 30 and 30 x 40 degrees in 0.1 / 0.08 s); a downward snap; slow raises
        gcheck(runG([&](double t, Pose& p) {
                  const int k = int(t / 0.4);
                  const float u = ease(t, k * 0.4, 0.15), from = (k & 1) ? 60.0f : -60.0f;
                  aimAt(p, from - 2 * from * u, -8 + 4 * std::sin(float(t) * 9));
              }, 40) == 0, "fast yaw sweeps");
        gcheck(runG([&](double t, Pose& p) {
                  const int k = int(t / 0.35);
                  const float u = ease(t, k * 0.35, 0.1), s = (k & 1) ? -1.0f : 1.0f;
                  aimAt(p, s * (-25 + 50 * u), s * (-15 + 30 * u));
              }, 40) == 0, "fast diagonal aim moves");
        gcheck(runG([&](double t, Pose& p) {
                  const int k = int(t / 0.35);
                  const float u = ease(t, k * 0.35, 0.08), s = (k & 1) ? -1.0f : 1.0f;
                  aimAt(p, s * (-15 + 30 * u), s * (-20 + 40 * u));
              }, 40) == 0, "steep diagonal aim moves");
        gcheck(runG([&](double t, Pose& p) { aimAt(p, 0, 10 - 50 * ease(t, 1.0, 0.08)); }, 30) == 0, "downward snap");
        gcheck(runG([&](double t, Pose& p) {
                  aimAt(p, 0, -60 * ease(t, 1.0, 0.08) + 60 * ease(t, 1.5, 0.6));   // (a snap down, a slow raise back)
              }, 30) == 0, "down, slowly back");
        gcheck(runG([&](double t, Pose& p) { aimAt(p, 20 * ease(t, 1.0, 0.3), -10 + 60 * ease(t, 1.0, 0.6)); }, 30) == 0, "slow raise");
        gcheck(runG([&](double t, Pose& p) { aimAt(p, 0, -10 + 40 * ease(t, 1.0, 0.3)); }, 30) == 0, "brisk raise (40 in 0.3 s)");
        gcheck(runG([&](double t, Pose& p) {   // a sideways snap (60 degrees in 70 ms) and a roll of the wrist
                  aimAt(p, 60 * ease(t, 1.0, 0.07), 0);
                  if (t > 1.5) quatFromYawPitchRoll(60 * deg, 0, 70 * deg * ease(t, 1.5, 0.07), p.rot);
              }, 30) == 0, "sideways snap, roll");
        // recoil-like jitters: kicks of 15 degrees up in 2 frames (675 deg/s) and back, 4 a second; a 15 Hz tremble of
        // 8 degrees (750 deg/s peaks)
        gcheck(runG([&](double t, Pose& p) {
                  const double c = std::fmod(t, 0.25);
                  aimAt(p, 0, float(c < 2 / 90.0 ? 15 * c * 45 : std::max(0.0, 15 - (c - 2 / 90.0) * 150)));
              }, 30) == 0, "recoil kicks");
        gcheck(runG([&](double t, Pose& p) { aimAt(p, 0, 8 * std::sin(2 * pi * 15 * float(t))); }, 30) == 0, "15 Hz tremble");
        {   // an untracked controller, flicked; a tracking jump (50 degrees up in one frame); a gap of 0.2 s mid-flick
            ReloadGestures g;
            int n = 0;
            for (int f = 0; f < 270; ++f) {
                const double t = f / 90.0;
                Pose p;
                aimAt(p, 0, -5 + 45 * ease(std::fmod(t, 1.0), 0.5, 0.08));
                n += g.update(p, int64_t(t * 1e9), true, thr, true, flk) ? 1 : 0;
            }
            check(n == 0, "flick, untracked");
            gcheck(runG([&](double t, Pose& p) { aimAt(p, 0, t >= 1.0 ? 45.0f : -5.0f); }, 30) == 0, "tracking jump");
            n = 0;
            for (int f = 0; f < 270; ++f) {
                const double t = f / 90.0;
                if (t > 1.0 && t < 1.2) continue;   // (no frames: the runtime lost the controller)
                Pose p;
                p.valid = true;
                aimAt(p, 0, -5 + 45 * ease(t, 1.05, 0.1));   // (raised while unseen: up in one step after it)
                n += g.update(p, int64_t(t * 1e9) + 10000000000LL, true, thr, true, flk) ? 1 : 0;
            }
            check(n == 0, "tracking hitch");
        }
        // must reload twice: two flicks 1 s apart (the gun lowered slowly in between)
        gcheck(runG([&](double t, Pose& p) {
                  aimAt(p, 0, -5 + 45 * ease(t, 1.0, 0.08) - 45 * ease(t, 1.3, 0.5) + 45 * ease(t, 2.0, 0.08));
              }, 30) == 2, "two flicks");
        // must reload once: a shake right after a flick (the cooldown shared), and the wrist shake (it is a flick too)
        gcheck(runG([&](double t, Pose& p) {
                  aimAt(p, 0, -5 + 45 * ease(t, 1.0, 0.08));
                  shake(0.12f, 1.15)(t, p);
              }, 30) == 1, "flick then shake");
        gcheck(runG([&](double t, Pose& p) {
                  if (t >= 1.0 && t < 1.5) quatFromYawPitchRoll(0, 0.6f * std::sin(2 * pi * 6 * float(t - 1.0)), 0, p.rot);
              }, 30) == 1, "wrist shake, flick on");
        // the shake cases with the flick on: the same counts
        gcheck(runG(shake(0.06f, 1.0), 30) == 1, "gentle shake, flick on");
        gcheck(runG(shake(0.12f, 1.0), 30) == 1, "one shake, flick on");
        gcheck(runG([&](double t, Pose& p) {
                  shake(0.12f, 1.0)(t, p);
                  shake(0.12f, 2.0)(t, p);
              }, 40) == 2, "two shakes, flick on");
        // vrgest (user 2026-11-10 on a Quest 3): a Quest's pose stream: 72 / 90 Hz display times, frames dropped (1 in
        // 12; 1 in 50 two in a row: gaps up to 42 ms), time stamps off by up to 1 ms, the pose trembling (+-0.6 mm,
        // +-0.15 degrees: tracking noise); both gestures on at the defaults
        auto runQ = [&](const std::function<void(double, Pose&)>& motion, double secs, int hz, uint32_t s, bool shakeOn,
                        float g, bool flickOn, float dps) {
            ReloadGestures gs;
            int n = 0;
            auto r = [&](float lo, float hi) {
                s = s * 1664525u + 1013904223u;
                return lo + (hi - lo) * float(s >> 8) / float(1u << 24);
            };
            for (int f = 0; f < int(secs * hz); ++f) {
                const float u = r(0, 1);
                if (u < 0.02f) { ++f; continue; }   // (two frames dropped)
                if (u < 0.10f) continue;            // (one)
                const double t = double(f) / hz + r(-0.001f, 0.001f);
                Pose p;
                p.valid = true;
                p.pos[1] = -0.25f, p.pos[2] = -0.3f;
                motion(t, p);
                for (int k = 0; k < 3; ++k) p.pos[k] += r(-0.0006f, 0.0006f);
                const float e = 0.15f * deg * 0.5f, a[4] = {r(-e, e), r(-e, e), r(-e, e), 1.0f}, * b = p.rot;
                float q[4] = {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
                              a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                              a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
                              a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
                const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
                for (int k = 0; k < 4; ++k) p.rot[k] = q[k] / l;
                if (gs.update(p, int64_t(t * 1e9) + 5000000000LL, shakeOn, g, flickOn, dps)) ++n;
            }
            return n;
        };
        // the gun on an arm: the grip `radius` m from the shoulder (0.2 m right, 0.45 m up of the base pose), along
        // its aim (an aim move of the whole arm moves the grip too)
        auto onArm = [&](Pose& p, float yawDeg, float pitchDeg, float radius) {
            aimAt(p, yawDeg, pitchDeg);
            float m[9];
            quatToMatrix(p.rot, m);
            p.pos[0] = 0.0f - radius * m[2], p.pos[1] = 0.2f - radius * m[5], p.pos[2] = 0.15f - radius * m[8];
        };
        struct QCase {
            const char* name;
            std::function<void(double, Pose&)> motion;
            double secs;
            int want;
        };
        const QCase qcases[] = {
            // never a reload: holding still, aiming fast (the wrist; the whole arm: the grip 0.45 / 0.6 m from the
            // shoulder), recoil, a trembling hand, a tracking jump, walking about
            {"Q still", [&](double, Pose&) {}, 3, 0},
            {"Q fast yaw sweeps", [&](double t, Pose& p) {
                 const int k = int(t / 0.4);
                 const float u = ease(t, k * 0.4, 0.15), from = (k & 1) ? 60.0f : -60.0f;
                 aimAt(p, from - 2 * from * u, -8 + 4 * std::sin(float(t) * 9));
             }, 4, 0},
            {"Q fast diagonal aim moves", [&](double t, Pose& p) {
                 const int k = int(t / 0.35);
                 const float u = ease(t, k * 0.35, 0.1), s = (k & 1) ? -1.0f : 1.0f;
                 aimAt(p, s * (-25 + 50 * u), s * (-15 + 30 * u));
             }, 4, 0},
            {"Q steep diagonal aim moves", [&](double t, Pose& p) {
                 const int k = int(t / 0.35);
                 const float u = ease(t, k * 0.35, 0.08), s = (k & 1) ? -1.0f : 1.0f;
                 aimAt(p, s * (-15 + 30 * u), s * (-20 + 40 * u));
             }, 4, 0},
            {"Q arm sweeps (0.45 m)", [&](double t, Pose& p) {
                 const int k = int(t / 0.4);
                 const float u = ease(t, k * 0.4, 0.15), s = (k & 1) ? -1.0f : 1.0f;
                 onArm(p, s * (-25 + 50 * u), s * (-8 + 16 * u), 0.45f);
             }, 4, 0},
            {"Q arm sweeps (0.6 m)", [&](double t, Pose& p) {
                 const int k = int(t / 0.4);
                 const float u = ease(t, k * 0.4, 0.15), s = (k & 1) ? -1.0f : 1.0f;
                 onArm(p, s * (-25 + 50 * u), s * (-8 + 16 * u), 0.6f);
             }, 4, 0},
            {"Q arm snaps (12 deg in 80 ms, 0.6 m)", [&](double t, Pose& p) {
                 const int k = int(t / 0.3);
                 const float u = ease(t, k * 0.3, 0.08), s = (k & 1) ? -1.0f : 1.0f;
                 onArm(p, s * (-6 + 12 * u), -5, 0.6f);
             }, 4, 0},
            {"Q aim snaps with overshoot", [&](double t, Pose& p) {
                 const int k = int(t / 0.5);
                 const float s = (k & 1) ? -1.0f : 1.0f;
                 aimAt(p, s * (-20 + 46 * ease(t, k * 0.5, 0.12) - 6 * ease(t, k * 0.5 + 0.12, 0.08)), -5);
             }, 4, 0},
            {"Q aim snaps in one frame (preview gun)", [&](double t, Pose& p) {
                 const int k = int(t / 0.12);   // (the next target each 0.12 s: 15 - 30 degrees in one frame)
                 aimAt(p, float(k % 3 - 1) * 15.0f, float(k % 2) * 10.0f - 5.0f);
             }, 4, 0},
            {"Q aim snaps in two frames (preview gun)", [&](double t, Pose& p) {
                 // (the preview's gun points at the cursor ray at the depth read a frame late: a snap to the next
                 // target is 80 % of the way one frame, the rest the next)
                 const int k = int(t / 0.12);
                 const float u = t - k * 0.12 < 0.012 ? 0.8f : 1.0f;
                 auto yawOf = [](int i) { return float(i % 3 - 1) * 15.0f; };
                 auto pitchOf = [](int i) { return float(i % 2) * 10.0f - 5.0f; };
                 aimAt(p, yawOf(k + 2) + (yawOf(k + 3) - yawOf(k + 2)) * u, pitchOf(k + 1) + (pitchOf(k) - pitchOf(k + 1)) * u);
             }, 4, 0},
            {"Q recoil kicks", [&](double t, Pose& p) {
                 const double c = std::fmod(t, 0.25);
                 aimAt(p, 0, float(c < 2 / 90.0 ? 15 * c * 45 : std::max(0.0, 15 - (c - 2 / 90.0) * 150)));
             }, 3, 0},
            {"Q 15 Hz tremble", [&](double t, Pose& p) { aimAt(p, 0, 8 * std::sin(2 * pi * 15 * float(t))); }, 3, 0},
            {"Q tracking jumps", [&](double t, Pose& p) {
                 const bool away = std::fmod(t, 1.0) >= 0.5;   // (45 degrees up and 15 cm off in one frame, and back)
                 aimAt(p, 0, away ? 40.0f : -5.0f);
                 if (away) p.pos[1] += 0.15f;
             }, 3, 0},
            {"Q small tracking jumps (2 cm)", [&](double t, Pose& p) {
                 if (std::fmod(t, 0.2) >= 0.1) p.pos[0] += 0.02f;
             }, 3, 0},
            {"Q slow wobble (+-5 cm, 1.5 Hz)", [&](double t, Pose& p) { p.pos[0] += 0.05f * std::sin(2 * pi * 1.5f * float(t)); }, 3, 0},
            {"Q walking (+-2 cm bob, 2 Hz)", [&](double t, Pose& p) {
                 p.pos[1] += 0.02f * std::sin(2 * pi * 2 * float(t)), p.pos[0] += 0.03f * std::sin(2 * pi * float(t));
                 aimAt(p, 5 * std::sin(2 * pi * 0.5f * float(t)), -5);
             }, 3, 0},
            // exactly one reload: a gentle shake, a gentle wrist shake, a short flick, a single jolt each way
            {"Q gentle shake (+-4 cm, 5 Hz)", [&](double t, Pose& p) {
                 if (t >= 1.0 && t < 1.6) p.pos[1] += 0.04f * std::sin(2 * pi * 5 * float(t - 1.0));
             }, 3, 1},
            {"Q gentle wrist shake (+-20 deg, 4 Hz)", [&](double t, Pose& p) {
                 aimAt(p, 0, t >= 1.0 && t < 1.6 ? 20 * std::sin(2 * pi * 4 * float(t - 1.0)) : 0.0f);
             }, 3, 1},
            {"Q short flick (25 deg in 70 ms)", [&](double t, Pose& p) { aimAt(p, 0, -5 + 25 * ease(t, 1.0, 0.07)); }, 3, 1},
            {"Q jolt down (10 cm in 0.1 s)", [&](double t, Pose& p) {
                 const float u = ease(t, 1.0, 0.1);
                 p.pos[1] -= 0.1f * u;
                 aimAt(p, 0, -3 * u);
             }, 3, 1},
            {"Q jolt up (10 cm in 0.1 s)", [&](double t, Pose& p) {
                 const float u = ease(t, 1.0, 0.1);
                 p.pos[1] += 0.1f * u;
                 aimAt(p, 0, 3 * u);
             }, 3, 1},
            {"Q jolt sideways (10 cm in 0.1 s)", [&](double t, Pose& p) {
                 const float u = ease(t, 1.0, 0.1);
                 p.pos[0] += 0.1f * u;
                 aimAt(p, 3 * u, 0);
             }, 3, 1},
            {"Q jolt forward (10 cm in 0.1 s)", [&](double t, Pose& p) { p.pos[2] -= 0.1f * ease(t, 1.0, 0.1); }, 3, 1},
        };
        const int rates[2] = {72, 90};
        for (const QCase& c : qcases) {
            char counts[64];
            int at = 0;
            bool ok = true;
            for (int hz : rates)
                for (uint32_t s = 1; s <= 3; ++s) {
                    const int n = runQ(c.motion, c.secs, hz, s * 7919u + uint32_t(hz), true, thr, true, flk);
                    ok = ok && n == c.want;
                    at += std::snprintf(counts + at, sizeof(counts) - at, "%d ", n);
                }
            std::printf("vr gesture: %-40s %s(72 Hz x3, 90 Hz x3)\n", c.name, counts);
            check(ok, c.name);
        }
        // the margins: how far each gesture's level can go (down for a case that must not reload, up for one that must)
        // before the count changes, over the six streams (one gesture on at a time)
        if (std::getenv("HOTD_VR_GESTURE_MARGINS")) {
            for (const QCase& c : qcases) {
                auto farthest = [&](bool shake, float level) {   // the count furthest from the wanted one
                    int w = c.want;
                    for (int hz : rates)
                        for (uint32_t s = 1; s <= 3; ++s) {
                            const int n = runQ(c.motion, c.secs, hz, s * 7919u + uint32_t(hz), shake, level, !shake, level);
                            if (n != c.want) w = n;
                        }
                    return w;
                };
                float edge[2];
                for (int sh = 1; sh >= 0; --sh) {
                    const float def = sh ? thr : flk, stepK = c.want ? 1.02f : 1.0f / 1.02f, stop = sh ? (c.want ? 12.0f : 0.5f) : (c.want ? 1500.0f : 100.0f);
                    float lv = def;
                    edge[sh] = -1;
                    while (c.want ? lv <= stop : lv >= stop) {
                        if (farthest(sh == 1, lv) != c.want) { edge[sh] = lv; break; }
                        lv *= stepK;
                    }
                }
                std::printf("vr gesture margin: %-40s shake %s at %.2f g, flick at %.0f deg/s (-1: never)\n", c.name,
                            c.want ? "lost" : "fires", edge[1], edge[0]);
            }
        }
    }
    std::printf("vr aim test: %d checks, %d failed\n", checks, failed);
    return failed;
}

} // namespace hotd::vr
