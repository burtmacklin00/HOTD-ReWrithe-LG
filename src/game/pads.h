// Controllers and light guns (ROADMAP "Controllers and light guns", worker "pads", 2026-10-19). Ours: neither original
// had them (the arcade has two light guns; the PC port a mouse / keyboard).
//
// Pads: SDL3's gamepad API (SDL_Gamepad: DualSense, DualShock 4, DualShock 3 (with its driver), Xbox One / Series,
// Switch Pro, Joy-Con pairs / singles, and anything else SDL maps). A player whose input is "Gamepad" gets the first
// pad connected that no other player has (in connection order); a pad that is unplugged mid-game pauses the game with
// a message for that player until a pad is plugged in again (hot-plug). Each player has a profile (launcher.h
// PadProfile, launcher.ini pad1.* / pad2.*); its "Auto" values follow the pad's type.
//   aim (worker "padfeel", 2026-10-20: the feel by the public gyro / stick practice, Gyro Wiki / JoyShockMapper /
//   Steam Input): moved every rendered frame by real time (Pads::aim), never per game tick.
//   - sticks: speeds per second (screen heights / s at full tilt), a radial inner dead zone (default 8 %) and an outer
//     one (full speed from 92 %), a power curve (default 2: a precise centre, a fast edge), the edge turbo (held at
//     full tilt for 0.15 s: up to 1.5x over 0.4 s), no smoothing; the other stick = fine aim (also on top of the gyro)
//   - gyro: "real world sensitivity": at 1.0x a turn of 36 degrees spans the window's width (the same pixels per
//     degree up / down); every sensor reading integrated with its own timestamp (SDL delivers several a frame); the
//     space Player (Gyro Wiki's player space: yaw about gravity, its size from yaw + roll, pitch local; the default),
//     World (yaw about gravity, pitch about the horizontal axis) or Local; gravity = the accelerometer fused with the
//     gyro (a complementary filter); tiered smoothing only below a few degrees / s (the motion delayed, never lost);
//     an optional tightening dead zone and acceleration (both off by default); the drift (bias) learnt while the pad
//     lies still, plus "Calibrate Gyro" (hold still 2 s)
//   - shake to reload (gyro styles, default on; relpad: game/shake.h): a quick flick or jerk of the pad, any axis (a
//     wrist flick / roll past a turn rate, or a jerk up / down, forward / back or side to side: a peak of linear
//     acceleration and one back) = the reload, then a 0.4 s cooldown; the gyro's motion meanwhile is held back and
//     dropped (the crosshair stays). Slow aim sweeps, tremor and rumble do not count
//   - the touchpad (DualSense / DualShock 4: pointer = the touch position is the crosshair's, or trackpad =
//     relative; a small dead band against jitter)
//   right trigger (fires at a light press: "Trigger Point") / right shoulder / South (A, Cross) = trigger;
//   left trigger / left shoulder / East / West = reload; the touchpad's click or a tap = reload or shoot (profile)
//   North (Y, Triangle): held = the gyro's ratchet (the crosshair stays while the pad is turned back), tapped = the
//   crosshair to the middle; right stick click = the middle; left stick: clicked = the middle, held = precise (35 % speed)
//   a trigger pulled with the crosshair off the window (the stick, gyro or touchpad past the edge) = the reload; the
//   crosshair stays there (padfix; was: back to the middle). Each input its own speed (sens36, GitHub #36: Stick /
//   Fine Stick / Gyro / Trackpad Sensitivity per style, nothing shared on top; was x the player's Aim Sensitivity)
//   Start = start / join / continue (not in play), the pause (in play); Back / Share / Minus = the pause
//   (keybind: every one of these per player on Options > Controls > Keys & Buttons, game/keybinds.h; untouched = "Auto")
//   paused: d-pad / stick up / down pick, South / trigger = choose, Start / East = close
//   menus (the front end, the attract, the end screen): d-pad / stick = arrows, South / Start = Enter, East / Back = Esc
//   feedback: rumble on a hit and a reload (+ every shot: profile), the light bar in the player's colour (P1 red,
//   P2 blue, P3 green, P4 yellow), the DualSense's adaptive right trigger with a gun's feel (resistance that gives)
//
// LightGuns: Sinden, AimTrak, GunCon 3 (with a mouse driver), a Wii Remote through a Mayflash DolphinBar (mouse
// mode) and the like present themselves to Windows as a mouse that reports absolute positions. A player whose input
// is "Light Gun" gets such a device (the absolute ones, in the order first moved); while any player uses a gun the
// game reads every mouse through Windows' raw input itself (main.cpp's WM_INPUT hook): absolute devices set their
// player's crosshair, relative ones (ordinary mice) move the mouse players' crosshairs by their motion, so the shared
// desktop cursor (moved by every device) is not used for aiming. Left = trigger, right = reload, middle = start; a
// gun pointed off the window (or reporting 0, 0) aims off the screen = the reload. F9 in the game: calibration (each
// gun shoots two targets), kept in launcher.ini (gun1cal / gun2cal). ipolish: Controls > Light Guns > Assign gives a
// seat its own device (a trigger pull takes it; launcher.ini gun1dev= / gun1name=); unassigned seats: the order used.
#pragma once

#include "game/launcher.h"
#include "game/shake.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hotd::game {

class Runtime;
class Pause;
struct PauseInput;

constexpr int kInputPlayers = 4;   // players 3 / 4: the experimental 4P mode only (docs/coop4.md)

// a player's light gun in window coordinates, and trigger / reload since the last game frame (main.cpp's guns)
struct GunState {
    float x = 0, y = 0;
    bool trigger = false, reload = false;
    bool weaponNext = false;   // Light Gun Edition: Arsenal Mode: the middle button pressed (the next weapon)
    // edgereload: last placed by a touch / pen (an absolute pointer, put on the picture directly): the window's
    // outermost pixel is a shot there, not the confined mouse's "pushed against the edge" reload
    bool pointer = false;
    bool held = false;   // Survival Mode's SMG: the mouse button / fire key is down now (Runtime::GunInput::held)
};

class Pads {
public:
    bool want[kInputPlayers] = {};        // the player's input is a gamepad
    PadProfile prof[kInputPlayers];       // the players' profiles
    // keybind (game/keybinds.h): the players' buttons (LaunchConfig::padBinds); not custom: the style's own (below)
    PadBinds binds[kInputPlayers];
    // --input-test: only SDL's virtual pads (the test's own) are opened; physical pads and their buttons are left out,
    // so the test runs the same whatever is plugged in
    bool virtualOnly = false;
    // weapons (game/weapons.h; Survival in play, main sets it each frame): a pad's shoulder buttons L1 / R1 and the
    // d-pad's left / right are the previous / next weapon (their bound actions not done meanwhile: the triggers L2 / R2
    // reload / fire by default); weaponStep: the steps asked since main last read them, by player
    bool weaponShoulders = false;
    int weaponStep[kInputPlayers] = {};
    void configure(const LaunchConfig& c);

    bool start();   // the gamepad subsystem (SDL_INIT_GAMEPAD)
    void stop();
    // every loop on every screen: opens pads that came, closes the ones that went, assigns them to players
    void refresh();
    // a gamepad event (button, axis, sensor, touchpad, device): consumed (true) or not one
    bool event(const SDL_Event& e);
    // menus: a gamepad button / stick flick as the key the menus know (arrows, Enter, Escape); false: none
    bool menuKey(const SDL_Event& e, SDL_Event& key);

    // the game loop, once per loop after the events: the pads' buttons into the guns (trigger / reload), the start
    // buttons, the pause; a pad lost in play opens the pause; the touchpad pointer; the rumble on hits / reloads / shots
    void apply(Runtime& rt, GunState guns[], Pause& pause, PauseInput& pin, int lw, int lh);
    // every loop (every rendered frame) while the game runs: the aim's motion over dt seconds of real time (the sticks
    // by their speed, the gyro's and the trackpad's motion since the last call), so the crosshair drawn and the shot
    // that follows are the newest input's
    void aim(GunState guns[], int lw, int lh, float dt);
    // menufb: the menu's Firing Range (no game running): the pads' buttons into the guns (trigger / reload, the
    // recentre, the touchpad pointer); start / back are left to the menu
    void rangeApply(GunState guns[], int lw, int lh);
    // the Controller page's Aim Test: what the player's pad reads now
    struct AimProbe {
        bool pad = false, gyro = false;
        float gyroDeg = 0;        // the gyro's turn (degrees / s, the aiming axes, before the sensitivity)
        float stick = 0;          // the aiming stick's deflection (0-1), and after the dead zones / curve / turbo
        float stickOut = 0;
        float widths = 0;         // the crosshair's speed from them now (window widths / s)
        float spanDeg = 0;        // the gyro: degrees of turn across the window's width at this sensitivity
        float driftDeg = 0;       // the gyro's learnt bias (degrees / s)
        int calib = -1;           // "Calibrate Gyro": -1 not run, 0-99 holding still (%), 100 done
        int shakes = 0;           // shake reloads seen
    };
    AimProbe probe(int p, int lw, int lh) const;
    void calibrate(int p);        // "Calibrate Gyro": the pad held still for 2 s gives the bias (it restarts on motion)
    bool calibrating(int p) const;
    void gyroBias(int p, float out[3]) const;   // (the input test)
    // a new game: the crosshairs of the pad players in the middle, the motion dropped
    void reset(GunState guns[], int lw, int lh);

    // a player's start button (null: Runtime::player_join; netplay: through the lockstep); lostPause: a pad lost in
    // play opens the pause (netplay: off, the message only)
    std::function<void(int)> onJoin;
    bool lostPause = true;

    SDL_JoystickID padOf(int p) const;   // 0: none
    // padpass: the player's fire held now (its own fire buttons: the style's shoot mask, or its Keys & Buttons Fire; the
    // triggers past half way): the cut-scene's hold-to-skip for any pad (a Shoot Button of L2 fires with L2)
    bool fireHeld(int p) const;
    int playerOf(SDL_JoystickID id) const;   // the player a pad is assigned to (-1: none; the PC Mode screen's seats)
    bool lost(int p) const { return lost_[p]; }
    // what the player's profile resolves to with its pad (Auto by the pad's type; a style the pad cannot do: Classic):
    // the style 1-4 (launcher.h PadProfile), the touchpad 1 off, 2 pointer, 3 trackpad; 0: no pad
    int styleOf(int p) const;
    int touchOf(int p) const;
    std::string padName(int p) const;
    SDL_GamepadType padType(int p) const;   // UNKNOWN: no pad
    int promptStyle(int p) const;           // padglyphs: its button prompts' style (pad_prompts.h), UNKNOWN: no pad
    // keybind: the buttons the player's pad has with its style now (Auto: the pad's; no pad: the profile's style, else
    // Classic) as a binding table: what an Auto pad's first change on the Key / Button Bindings page starts from
    PadBinds defaultBinds(int p) const;
    // the message while a player's pad is lost (empty: none)
    std::vector<std::string> notice() const;
    void rumble(int p, float low, float high, int ms);
    int rumbles[kInputPlayers] = {};   // rumbles started (the input test)
    int effects[kInputPlayers] = {};   // adaptive-trigger effects sent (the input test)
    // the game's player each input slot plays (netplay: this PC's slot 0 is the game's player 1 or 2): its state for
    // the pause / start choice and the rumble
    int gamePlayer[kInputPlayers] = {0, 1, 2, 3};

private:
    struct Pad {
        SDL_JoystickID id = 0;
        SDL_Gamepad* g = nullptr;
        SDL_GamepadType type = SDL_GAMEPAD_TYPE_UNKNOWN;
        int prompt = 0;   // padglyphs: the button prompts' style (pad_prompts.h padStyleOf)
        int player = -1;
        int effectFor = -2;           // the adaptive trigger / light bar were set for this player (-1 none)
        bool gyro = false, accel = false, touch = false;   // what the pad has
        bool sensorsOn = false;
        float acc[2] = {};            // the gyro's turn since the last aim (right, down), radians
        uint64_t lastTs = 0;          // the last gyro reading's sensor timestamp (ns)
        uint64_t lastAccTs = 0;       // the last accelerometer reading's
        float rate = 0;               // the gyro's data rate (Hz), 0 unknown
        float bias[3] = {};           // the gyro's drift (rad/s), learnt while the pad is still / calibrated
        float grav[3] = {};           // "up" in the pad's axes (the accelerometer fused with the gyro); 0: none yet
        float up[3] = {0, 1, 0};      // "local" space's up: the pad's y, or gravity when last recentred (calibrated)
        float smooth[2] = {};         // the tiered smoothing's smoothed part (rad/s)
        float edgeT = 0;              // the aiming stick held at full tilt (s): the turbo
        float omega = 0;              // the last gyro reading's turn rate (rad/s, the bias off)
        // still detection (the drift): the rates' running mean / variance, the accelerometer's unsteadiness
        float mW[3] = {}, vW = 0, linDev = 0, stillT = 0;
        // Calibrate Gyro
        bool cal = false;
        float calT = 0, calSum[3] = {};
        int calN = 0, calDone = -1;
        // shake to reload
        ShakeDetector shake;          // (game/shake.h)
        int shakes = 0;
        uint64_t holdUntil = 0;
        float pend[2] = {};
        bool shakeDrop = false;
        // the Aim Test's readout
        float liveDeg = 0;
        bool trig[2] = {};            // the triggers pressed (left, right; with hysteresis)
        int flick = 0, flickX = 0;    // the left stick's menu flicks
        bool ratchet = false;         // North held: the gyro's motion is not applied
        uint64_t northAt = 0;         // when North went down (ns)
        bool precise = false;         // left stick click held
        uint64_t preciseAt = 0;       // padfix: when it went down (ns): let go within 0.15 s = a click = the middle
        // the touchpad's first finger
        bool tDown = false, tClicked = false;
        int tFinger = -1;
        uint64_t tAt = 0;
        float tX0 = 0, tY0 = 0, tX = 0, tY = 0, tMoved = 0, tDx = 0, tDy = 0;
        float tFx = 0, tFy = 0;       // the touch point after the dead band (the jitter)
        // edges since the last apply
        bool eShake = false;   // a shake to reload: only in play with the shots on (never skips a cut-scene)
        bool eTrigger = false, eReload = false, eStart = false, eBack = false, eRecentre = false, eUp = false,
             eDown = false, eChoose = false, eClose = false, eTouch = false;
        // autostart: the buttons held when the pad came (its first state: plugged in / found at the start with a
        // button down) are not presses: ignored until let go (bits: SDL_GamepadButton)
        uint32_t stale = 0;
        uint64_t openedNs = 0;        // SDL_GetTicksNS when opened
        uint64_t openedRefresh = 0;   // refreshes_ when opened
        std::string name;
    };
    uint64_t refreshes_ = 0;
    bool staleButton(const SDL_Event& e);   // autostart: a held-at-connection button's event (swallowed)
    std::vector<Pad> pads_;
    bool started_ = false;
    bool lost_[kInputPlayers] = {};
    bool lostShown_[kInputPlayers] = {};   // the pause opened for it already
    bool recentre_[kInputPlayers] = {};    // the gyro's off-screen reload: recentre after the shot
    int lives_[kInputPlayers] = {}, score_[kInputPlayers] = {};   // the rumble: the lives / score / reloads seen last
    uint32_t reloads_[kInputPlayers] = {};
    Pad* find(SDL_JoystickID id);
    const Pad* padFor(int p) const;
    void assign();
    void feedback(Pad& d);   // the light bar and the adaptive trigger for the pad's player
    void gyro(Pad& d, const float data[3], uint64_t ts);
    void accel(Pad& d, const float data[3], uint64_t ts);
    int shakeLevel(const Pad& d) const;      // shake to reload: the level 1-3 of the style in use (0: off)
    void shakeReload(Pad& d, uint64_t ts);   // a shake seen: the reload, the motion dropped
    void press(Pad& d, unsigned button);   // a shoot / reload button (kB* bits) by the style's map
    bool custom(const Pad& d) const { return d.player >= 0 && binds[d.player].custom; }
    void bound(Pad& d, int input, bool down, uint64_t ts);   // keybind: an input (PadInput) by the player's table
    void recentre(Pad& d, GunState& g, int lw, int lh);
    int styleOf(const Pad& d) const;
    const PadStyle& styleSet(const Pad& d) const;
    int touchOf(const Pad& d) const;
    unsigned shootMask(const Pad& d) const;
};

// padpass: the light guns' screen aids (draw-only; LaunchConfig::gunKind / gunFlash / gunBorder). A light gun in play:
// a seat in play whose input is "Light Gun" (players 3 / 4 in the 4P mode)
bool lightGunInPlay(const LaunchConfig& c);
// the arcade's white shot flash wanted (Auto: a CRT gun in play)
bool gunFlashOn(const LaunchConfig& c);
// the white border's width in pixels for a window wh pixels high (Auto: a Sinden in play: Medium); 0: none
int gunBorderPx(const LaunchConfig& c, int wh);
// the arcade's flash cycle (gun_input_update 0x46A90 / gun_flash 0x46D50): a trigger pull starts 4 white frames (its
// own and the next 3: gun_flash_phase 4 .. 1); pulls during them start none (the 4-frame lockout). Kept by the host,
// one step a game tick: the game's own state is not touched
struct GunFlash {
    int left = 0;       // flash frames still to come (gun_flash_phase)
    bool now = false;   // the last tick was a flash frame (the picture after it: white)
    void tick(bool pulled, bool allowed)
    {
        if (left == 0 && pulled && allowed)
            left = 4;
        now = left > 0;
        if (left > 0)
            --left;
    }
    void reset() { left = 0, now = false; }
};

class LightGuns {
public:
    bool want[kInputPlayers] = {};       // the player's input is a light gun
    bool mouseWant[kInputPlayers] = {};  // the player's input is a mouse (moved by its raw motion while guns are on)
    float cal[kInputPlayers][4] = {};    // LaunchConfig::gunCal
    void configure(const LaunchConfig& c);
    bool active() const;                 // any player uses a gun: the raw-input mode

    // raw input (main.cpp's WM_INPUT hook; the input test calls these directly)
    void absolute(uint64_t dev, float u, float v);   // a position, in fractions of the window
    void relative(uint64_t dev, float dx, float dy); // a mouse's motion (window coordinates)
    void buttons(uint64_t dev, unsigned flags);      // RI_MOUSE_* flags: 0x1 / 0x2 left down / up, 0x4 / 0x8 right, 0x10 / 0x20 middle
#ifdef _WIN32
    void winRawInput(const void* rawInput, void* hwnd);   // a RAWINPUT (mouse) for the game window
#endif

    // the game loop, once per loop after the events: positions and buttons into the guns, the start buttons
    void apply(Runtime& rt, GunState guns[], bool paused, int lw, int lh);
    std::function<void(int)> onJoin;   // as Pads::onJoin
    void reset();   // a new game: the devices assigned again (in the order used)

    // calibration (F9 in the game): every gun player shoots a target at (0.1, 0.1), then one at (0.9, 0.9) of the window
    void startCalibration();
    void cancelCalibration() { calPlayer_ = -1; }
    bool calibrating() const { return calPlayer_ >= 0; }
    bool calibrationTarget(float& u, float& v) const;   // the target to shoot (window fractions)
    std::vector<std::string> calibrationLines() const;
    bool calibrated = false;   // a calibration finished (main saves cfg.gunCal); main clears it

    uint64_t deviceOf(int p) const;   // the input test

    // ipolish (Controls > Light Guns): each seat's own device (LaunchConfig::gunDev: its lasting raw-input name, the
    // device path; empty: the first gun / mouse used, as before). A pinned device goes to its seat whatever came
    // first (a mouse on a gun seat aims by its motion); a seat whose device is not connected is left to the others
    std::string pin[kInputPlayers];
    // a device's lasting name and the name shown (main.cpp's raw input asks Windows once per device; the input test
    // names its own)
    void describe(uint64_t dev, const std::string& key, const std::string& name);
    bool connected(const std::string& key);   // that device is plugged in (Windows' raw-input list, checked each second)
    // the Assign page: seat p waits for a trigger pull (left button) from any raw-input gun / mouse, which takes it
    void startAssign(int seat) { assignSeat_ = seat; assigned = Assigned{}; }
    void cancelAssign() { assignSeat_ = -1; }
    int assigning() const { return assignSeat_; }   // the seat waiting, -1 none
    struct Assigned {
        int seat = -1;
        std::string key, name;
    };
    Assigned assigned;   // a pull took a seat (the host saves gunDev / gunName and clears it)
    // Light Gun Edition (game/lightgun_hid.h, LightgunHID.ini's set-up): the next trigger pull from any raw-input gun /
    // mouse is only noted (no seat taken, no shot, no button), for the set-up to decide
    struct Captured {
        bool got = false;
        std::string key, name;
        bool reload = false;   // the right / middle button (else the trigger)
        bool absolute = false;  // a light gun (u, v: where it points, window fractions); else a mouse
        float u = 0.5f, v = 0.5f;
    };
    void startCapture() { capture_ = true; captured = Captured{}; }
    void stopCapture() { capture_ = false; }
    bool capturing() const { return capture_; }
    Captured captured;
    // Light Gun Edition: the menus follow Player 1's gun only (its key: the device path; "" off): its position and
    // buttons are kept for takeMenu, every other device's input is dropped (the game's own placement waits)
    void setMenuGun(const std::string& key) { if (key != menuKey_) { menuKey_ = key; menu_ = MenuGun{}; } }
    bool menuGunOn() const { return !menuKey_.empty(); }
    // the menu gun's input since the last call: its window position (calibrated, pixels of lw x lh) when it moved, a
    // trigger pull (the click), whether the trigger is held, a right-button press; false: nothing new
    bool takeMenu(int lw, int lh, bool& moved, float& x, float& y, bool& trigger, bool& held, bool& alt);
    // Light Gun Edition: a trigger pull of a player not in play (waiting / out of lives) is their Start as well
    bool triggerStarts = true;
    // Light Gun Edition: PC Mode's character select (the menus follow Player 1's gun): each player's own gun (their
    // LightgunHID.ini / Assign gun) for their own panel: 0 off, 2 the 2-panel screen (players 2's gun), 4 the
    // 4-panel screen (players 2-4's guns; player 1's reload and off-screen pull too, its trigger stays the menu's click)
    struct SeatPress { bool trigger = false, reload = false; };   // reload: the reload button or a pull off the screen
    void setSeatPick(int mode) { if (mode != seatPick_) { seatPick_ = mode; for (SeatPress& s : seatPress) s = {}; } }
    SeatPress seatPress[4];
    bool weaponSwitch = false;   // Light Gun Edition: Arsenal Mode: the middle button switches weapons (else it reloads)
    // Light Gun Edition (the gun set-up screen's crosshairs): where each light gun points (window fractions; guns
    // that have reported a position, on or off the screen)
    struct Point { std::string key; float u = 0, v = 0; };
    std::vector<Point> points() const
    {
        std::vector<Point> r;
        for (const Dev& d : devs_)
            if (d.absolute) r.push_back(Point{d.key, d.u, d.v});
        return r;
    }
private:
    int seatPick_ = 0;
    struct Dev {
        uint64_t id = 0;
        std::string key, name;   // (ipolish) describe()
        bool absolute = false;
        int player = -1;
        float u = 0.5f, v = 0.5f;      // absolute: the last raw position (window fractions)
        float dx = 0, dy = 0;          // relative: the motion since the last apply
        bool eTrigger = false, eReload = false, eStart = false;
        bool held = false;   // (Light Gun Edition: the trigger held down)
        bool eWeapon = false;   // (Light Gun Edition: the middle button with Arsenal Mode on)
    };
    std::vector<Dev> devs_;
    int calPlayer_ = -1, calStep_ = 0;
    int assignSeat_ = -1;
    bool capture_ = false;   // (Light Gun Edition) startCapture()
    std::string menuKey_;    // (Light Gun Edition) setMenuGun()
    struct MenuGun { bool moved = false, trigger = false, held = false, alt = false; float u = 0.5f, v = 0.5f; } menu_;
    std::vector<std::string> listed_;   // connected(): the raw-input mice listed (device paths), and when
    uint64_t listedAt_ = 0;
    bool listedOnce_ = false;
    float calRead_[kInputPlayers][4] = {};
    Dev& dev(uint64_t id);
    void place(Dev& d);   // a device without a player: the next gun / mouse player that has none
    void calibrationShot(Dev& d);
};

} // namespace hotd::game
