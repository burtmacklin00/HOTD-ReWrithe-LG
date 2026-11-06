// padglyphs: the menus' cursor from a pad's touchpad / gyro (menu_cursor.h)
#include "game/menu_cursor.h"

#include <algorithm>
#include <cmath>

namespace hotd::game {

bool MenuCursor::event(const SDL_Event& e, const MenuCursorConfig& c, float canvasW)
{
    // (GitHub #36, sens36: each its own %, 100 % = the old speed 5; the range as LaunchConfig::kCursorSens*)
    const float speed = float(std::clamp(c.touchSens, 10, 300)) / 100.0f;
    const float gyroSpeed = float(std::clamp(c.gyroSens, 10, 300)) / 100.0f;
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_UP: {
        if (!c.touchpad || e.gtouchpad.finger != 0)
            return c.touchpad;   // (a second finger: nothing)
        Pad& d = pads_[e.gtouchpad.which];
        if (e.type == SDL_EVENT_GAMEPAD_TOUCHPAD_UP) {
            d.down = false;
            return true;
        }
        if (e.type == SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN || !d.down) {
            d.down = true, d.lx = e.gtouchpad.x, d.ly = e.gtouchpad.y;
            return true;
        }
        // relative, as a laptop's trackpad: half the screen's width for a slow stroke across the pad, faster strokes
        // further (the move's length per event: about 0.005 slow, 0.03 fast)
        const float mx = e.gtouchpad.x - d.lx, my = e.gtouchpad.y - d.ly;
        d.lx = e.gtouchpad.x, d.ly = e.gtouchpad.y;
        const float len = std::hypot(mx, my), gain = canvasW * 0.6f * speed * (1.0f + std::min(len, 0.05f) * 40.0f);
        dx_ += mx * gain, dy_ += my * gain * 0.6f;   // (the pad is about 1.6 times as wide as tall)
        lastPad = e.gtouchpad.which;
        ++touches;
        return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (c.touchpad && e.gbutton.button == SDL_GAMEPAD_BUTTON_TOUCHPAD) {
            click_ = true;
            lastPad = e.gbutton.which;
            ++clicks;
            return true;
        }
        return false;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        return c.touchpad && e.gbutton.button == SDL_GAMEPAD_BUTTON_TOUCHPAD;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
            pads_[e.gaxis.which].hold = e.gaxis.value > 16000;
        return false;   // (the trigger's own uses stay)
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE: {
        if (e.gsensor.sensor != SDL_SENSOR_GYRO)
            return false;
        Pad& d = pads_[e.gsensor.which];
        const uint64_t ts = e.gsensor.sensor_timestamp ? e.gsensor.sensor_timestamp : e.gsensor.timestamp;
        const float dt = d.gyroTs && ts > d.gyroTs ? std::min(0.05f, float(ts - d.gyroTs) / 1e9f) : 0.0f;
        d.gyroTs = ts;
        if (c.gyro <= 0 || (c.gyro == 1 && !d.hold) || dt <= 0)
            return false;
        // tilt to point: about 40 degrees turn crosses the screen at 100 %; a little dead zone against drift
        const float yaw = e.gsensor.data[1], pitch = e.gsensor.data[0];
        const float k = canvasW / 0.7f * gyroSpeed * dt;
        if (std::fabs(yaw) > 0.02f) dx_ -= yaw * k;
        if (std::fabs(pitch) > 0.02f) dy_ -= pitch * k;
        lastPad = e.gsensor.which;
        ++gyroMoves;
        return false;
    }
    default:
        return false;
    }
}

bool MenuCursor::take(float& dx, float& dy, bool& click)
{
    dx = dx_, dy = dy_, click = click_;
    const bool any = dx_ != 0 || dy_ != 0 || click_;
    dx_ = dy_ = 0, click_ = false;
    return any;
}

} // namespace hotd::game
