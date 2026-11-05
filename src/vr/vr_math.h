// VR mode: the little math between the tracking space (OpenXR: right-handed, metres, -z ahead) and the game's world
// (left-handed, game units, +z ahead of the camera). docs/vr.md "Spaces".
#pragma once

#include "vr/vr.h"

#include <cmath>

namespace hotd::vr {

// a rigid transform: x' = R x + t (R row-major; its columns are the frame's right / up / forward axes)
struct Rigid {
    float r[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float t[3] = {0, 0, 0};

    void apply(const float in[3], float out[3]) const
    {
        for (int i = 0; i < 3; ++i)
            out[i] = r[i * 3] * in[0] + r[i * 3 + 1] * in[1] + r[i * 3 + 2] * in[2] + t[i];
    }
    void rotate(const float in[3], float out[3]) const
    {
        for (int i = 0; i < 3; ++i)
            out[i] = r[i * 3] * in[0] + r[i * 3 + 1] * in[1] + r[i * 3 + 2] * in[2];
    }
    Rigid operator*(const Rigid& b) const
    {
        Rigid o;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j)
                o.r[i * 3 + j] = r[i * 3] * b.r[j] + r[i * 3 + 1] * b.r[3 + j] + r[i * 3 + 2] * b.r[6 + j];
            o.t[i] = r[i * 3] * b.t[0] + r[i * 3 + 1] * b.t[1] + r[i * 3 + 2] * b.t[2] + t[i];
        }
        return o;
    }
    Rigid inverse() const
    {
        Rigid o;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                o.r[i * 3 + j] = r[j * 3 + i];
        for (int i = 0; i < 3; ++i)
            o.t[i] = -(o.r[i * 3] * t[0] + o.r[i * 3 + 1] * t[1] + o.r[i * 3 + 2] * t[2]);
        return o;
    }
    void axis(int k, float out[3]) const { out[0] = r[k], out[1] = r[3 + k], out[2] = r[6 + k]; }
    // world -> this frame as the renderer's column-major view matrix (rows right / up / forward)
    void viewMatrix(float m[16]) const
    {
        const Rigid inv = inverse();
        for (int c = 0; c < 3; ++c)
            for (int rr = 0; rr < 3; ++rr)
                m[c * 4 + rr] = inv.r[rr * 3 + c];
        m[3] = m[7] = m[11] = 0;
        m[12] = inv.t[0], m[13] = inv.t[1], m[14] = inv.t[2], m[15] = 1;
    }
    // this frame (local -> world) as a column-major model matrix
    void modelMatrix(float m[16]) const
    {
        for (int c = 0; c < 3; ++c)
            for (int rr = 0; rr < 3; ++rr)
                m[c * 4 + rr] = r[rr * 3 + c];
        m[3] = m[7] = m[11] = 0;
        m[12] = t[0], m[13] = t[1], m[14] = t[2], m[15] = 1;
    }
};

inline float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline void normalize3(float v[3])
{
    const float l = std::sqrt(dot3(v, v));
    if (l > 1e-12f) v[0] /= l, v[1] /= l, v[2] /= l;
}
inline void cross3(const float a[3], const float b[3], float o[3])
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

// a unit quaternion (x, y, z, w) as a row-major rotation (right-handed, the usual formula)
inline void quatToMatrix(const float q[4], float r[9])
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    r[0] = 1 - 2 * (y * y + z * z), r[1] = 2 * (x * y - z * w), r[2] = 2 * (x * z + y * w);
    r[3] = 2 * (x * y + z * w), r[4] = 1 - 2 * (x * x + z * z), r[5] = 2 * (y * z - x * w);
    r[6] = 2 * (x * z - y * w), r[7] = 2 * (y * z + x * w), r[8] = 1 - 2 * (x * x + y * y);
}
// a row-major rotation as a unit quaternion (x, y, z, w)
inline void matrixToQuat(const float r[9], float q[4])
{
    const float tr = r[0] + r[4] + r[8];
    if (tr > 0) {
        const float s = std::sqrt(tr + 1.0f) * 2;
        q[3] = 0.25f * s, q[0] = (r[7] - r[5]) / s, q[1] = (r[2] - r[6]) / s, q[2] = (r[3] - r[1]) / s;
    } else if (r[0] > r[4] && r[0] > r[8]) {
        const float s = std::sqrt(1.0f + r[0] - r[4] - r[8]) * 2;
        q[3] = (r[7] - r[5]) / s, q[0] = 0.25f * s, q[1] = (r[1] + r[3]) / s, q[2] = (r[2] + r[6]) / s;
    } else if (r[4] > r[8]) {
        const float s = std::sqrt(1.0f + r[4] - r[0] - r[8]) * 2;
        q[3] = (r[2] - r[6]) / s, q[0] = (r[1] + r[3]) / s, q[1] = 0.25f * s, q[2] = (r[5] + r[7]) / s;
    } else {
        const float s = std::sqrt(1.0f + r[8] - r[0] - r[4]) * 2;
        q[3] = (r[3] - r[1]) / s, q[0] = (r[2] + r[6]) / s, q[1] = (r[5] + r[7]) / s, q[2] = 0.25f * s;
    }
}
// a quaternion from yaw (about +y), pitch (about +x), roll (about -z = the view axis), right-handed, applied
// yaw * pitch * roll (the preview's head and gun)
inline void quatFromYawPitchRoll(float yaw, float pitch, float roll, float q[4])
{
    auto mulq = [](const float a[4], const float b[4], float o[4]) {
        o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
        o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
        o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
        o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    };
    const float qy[4] = {0, std::sin(yaw / 2), 0, std::cos(yaw / 2)};
    const float qp[4] = {std::sin(pitch / 2), 0, 0, std::cos(pitch / 2)};
    const float qr[4] = {0, 0, -std::sin(roll / 2), std::cos(roll / 2)};
    float t[4];
    mulq(qy, qp, t);
    mulq(t, qr, q);
}

// a tracking-space pose (right-handed metres) as a transform in the player's left-handed game-unit space (the
// reference frame's local space: +z ahead): z mirrored (S R S for the rotation, S = diag(1, 1, -1)), scaled to units
inline Rigid poseToLocal(const Pose& p, float unitsPerMetre)
{
    Rigid o;
    quatToMatrix(p.rot, o.r);
    // S R S: the entries with one z index change sign; then the axis that looked -z (ahead) looks +z
    o.r[2] = -o.r[2], o.r[5] = -o.r[5], o.r[6] = -o.r[6], o.r[7] = -o.r[7];
    o.t[0] = p.pos[0] * unitsPerMetre;
    o.t[1] = p.pos[1] * unitsPerMetre;
    o.t[2] = -p.pos[2] * unitsPerMetre;
    return o;
}

} // namespace hotd::vr
