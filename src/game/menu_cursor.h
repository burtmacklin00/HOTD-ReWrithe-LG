// padglyphs: the menus' bone-hand cursor from a pad (user 2026-10-27: "allow compatible controllers to use the
// bone cursor like the ps5 controller with the touchpad"): the touchpad as a laptop's trackpad (relative, accelerated;
// its click = a left click) and the gyro (tilt to point: while ZL / L2 / LT is held, or always). Both move the menu's
// pointer as the mouse does (the hand shows; the d-pad / stick hides it again, as after the mouse).
// Which pads: SDL's touchpads (DualSense, DualShock 4; a Steam Deck's / Steam Controller's trackpads only through Steam
// Input, which sends the mouse itself) and gyros (Switch Pro, Joy-Con, DualSense, DualShock 4, Steam Deck / Controller;
// Switch 2 pads if SDL reports a gyro). A Wii Remote points through a DolphinBar in mouse mode (a mouse: already works).
// Options > Controls: Touchpad Cursor (On / Off), Gyro Cursor (Off / While Holding / Always), Cursor Speed (1-10).
// padfix (user 2026-10-30: "Touchpad moves the bone hand only when Player 1 Input in the settings is set to Gamepad"):
// every pad moves it, whichever player it is for (was: player 1's or one not given to a player); each pad's finger,
// held trigger and gyro time apart
#pragma once

#include <SDL3/SDL.h>

#include <map>

namespace hotd::game {

struct MenuCursorConfig {
    bool touchpad = true;   // launcher.ini touchcursor=
    int gyro = 1;           // 0 off, 1 while ZL / L2 / LT is held, 2 always (launcher.ini gyrocursor=)
    int speed = 5;          // 1-10: the touchpad's (launcher.ini cursorspeed=)
    int gyroSpeed = 5;      // 1-10: the gyro's (GitHub #36; launcher.ini gyrocursorspeed=, missing: cursorspeed=)
};

class MenuCursor {
public:
    // an event of any pad: true = used up (the touchpad's touches and click)
    bool event(const SDL_Event& e, const MenuCursorConfig& c, float canvasW);
    // this frame's move (menu units) and click; false: none
    bool take(float& dx, float& dy, bool& click);
    bool gyroWanted(const MenuCursorConfig& c) const { return c.gyro > 0; }
    int touches = 0, gyroMoves = 0, clicks = 0;   // (the test)

private:
    struct Pad {
        bool down = false;
        float lx = 0, ly = 0;         // the finger's last point (0..1)
        bool hold = false;            // ZL / L2 / LT held
        uint64_t gyroTs = 0;          // the last gyro sample's time (ns)
    };
    std::map<SDL_JoystickID, Pad> pads_;
    float dx_ = 0, dy_ = 0;
    bool click_ = false;
};

} // namespace hotd::game
