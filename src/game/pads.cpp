// Gamepads (pads.h Pads): SDL3's gamepad API, stick / gyro / touchpad aiming per player, hot-plug, feedback. Ours:
// neither original had them.
#include "game/pads.h"
#include "game/pad_prompts.h"

#include "game/pause.h"
#include "game/runtime.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hotd::game {

using G = A::G;

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDeg = kPi / 180.0f;
// the gyro ("real world sensitivity", as Steam Input / JoyShockMapper / Gyro Wiki): at 1.0x a turn of kSpanDeg
// degrees spans the window's width, the same pixels per degree up / down; Gyro Sensitivity's % multiplies it (sens36:
// was Sensitivity 1-10 = 0.5x .. 2.5x)
constexpr float kSpanDeg = 36.0f;
// the gyro's tiered smoothing 1-3: below this rate (degrees / s) fully smoothed, from half of it blended, above it 1:1
constexpr float kSmoothDeg[4] = {0.0f, 3.0f, 6.0f, 10.0f};
constexpr float kSmoothTau = 0.06f;            // the smoothed part's time constant (s)
// the gyro's acceleration 1-2: the sensitivity x lo at <= 5 degrees / s .. x hi at >= 60 (off: 1)
constexpr float kAccelLo[3] = {1.0f, 0.8f, 0.6f}, kAccelHi[3] = {1.0f, 1.4f, 2.0f};
// the drift: "still" = the rates' 0.5 s mean below 1.5 degrees / s, their noise below 0.4 degrees / s and the
// accelerometer steady, for 1 s; the bias then follows the mean (time constant 1 s)
constexpr float kStillRate = 1.5f * kDeg, kStillNoise = 0.4f * kDeg;
// the sticks: the top speed (screen heights / s at full tilt) at 100 % (sens36: Stick Sensitivity's / Fine Stick
// Sensitivity's % multiply them; was Sensitivity 1-10 = 1.1 .. 5.8, Fine Aim Speed x 10 % of it); the radial dead
// zones; the curve's power; the turbo (held at full tilt: after 0.15 s up to x kTurbo over 0.4 s)
// (padfix, user 2026-10-30: "the right stick should move a little faster": every step 20 % faster, 5 = 2.4; was 2.0)
constexpr float kStickTop = 2.4f, kFineTop = 0.96f;
// sens36: a per-input speed's % as a factor
float sensOf(int pct) { return float(std::clamp(pct, kPadSensLo, kPadSensHi)) / 100.0f; }
constexpr float kDead[7] = {0.0f, 0.04f, 0.08f, 0.12f, 0.16f, 0.20f, 0.25f};
constexpr float kOuter = 0.92f;
constexpr float kCurve[4] = {1.0f, 1.5f, 2.0f, 2.5f};
constexpr float kTurbo[3] = {1.0f, 1.5f, 2.0f};
constexpr float kG = 9.80665f;
// the touchpad's dead band (fractions of the pad): the jitter of a resting finger
constexpr float kTouchBand = 0.0015f;
// the players' colours (the light bar): P1 red, P2 blue, P3 green, P4 yellow
constexpr uint8_t kColour[4][3] = {{255, 0, 0}, {0, 64, 255}, {0, 255, 0}, {255, 200, 0}};
constexpr float kPrecise = 0.35f;   // left stick click held: the aim's speed
constexpr uint64_t kTapNs = 200000000ull;   // a tap: North (recentre) / the touchpad (its click action) let go within 0.2 s
// padfix (user 2026-10-30: "pressing the left stick should centre the crosshair"): a click of the left stick (let go
// within 0.15 s) = the middle; held longer it stays the precise aim
constexpr uint64_t kClickNs = 150000000ull;
// the aim's margin past the window's edges (window heights): a pad's crosshair can point off the screen there, where
// a trigger pull is the light gun's off-screen reload (padfix: the sticks too; it stays there, no recentre)
constexpr float kMargin = 0.05f;

int playerState(const Runtime& rt, int p) { return int32_t(rt.mem.u32(Runtime::player_rec(p))); }   // 4P: seats 3 / 4 too
int playerLives(const Runtime& rt, int p) { return int32_t(rt.mem.u32(Runtime::player_rec(p) + 0xC)); }
int gameMode(const Runtime& rt) { return rt.mem.at<int16_t>(G::game_mode); }

// a stick (x, y in -1..1) after the radial dead zone and the curve: the direction kept, the length remapped from the
// dead zone's edge (no jump, no lag outside it)
void stickCurve(float x, float y, float dead, float curve, float& ox, float& oy)
{
    const float m = std::sqrt(x * x + y * y);
    if (m <= dead || m <= 0.0f) {
        ox = oy = 0;
        return;
    }
    const float mm = std::min(1.0f, (m - dead) / (kOuter - dead));   // full speed from the outer dead zone
    const float s = (curve == 1.0f ? mm : std::pow(mm, curve)) / m;
    ox = x * s;
    oy = y * s;
}

float axis(SDL_Gamepad* g, SDL_GamepadAxis a) { return std::max(-1.0f, SDL_GetGamepadAxis(g, a) / 32767.0f); }

bool sonyType(SDL_GamepadType t) { return t == SDL_GAMEPAD_TYPE_PS4 || t == SDL_GAMEPAD_TYPE_PS5; }
bool gyroType(SDL_GamepadType t)
{
    return sonyType(t) || t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO || t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
           t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT || t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
}

float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float len3(const float a[3]) { return std::sqrt(dot3(a, a)); }

// a dead band: the filtered point follows the raw one only by what is beyond `band` (no jitter, no lag past it)
void band(float raw, float& f, float b)
{
    if (raw > f + b) f = raw - b;
    else if (raw < f - b) f = raw + b;
}

} // namespace

void Pads::configure(const LaunchConfig& c)
{
    want[0] = c.p1Input == 2;
    want[1] = c.p2Input == 2;
    want[2] = c.players >= 3 && c.p3Input == 2;   // the 4P mode (docs/coop4.md): players 3 / 4
    want[3] = c.players >= 3 && c.p4Input == 2;
    bool trig = false;
    for (int p = 0; p < kInputPlayers; ++p) {
        trig = trig || prof[p].adaptive != c.pad[p].adaptive;
        prof[p] = c.pad[p];
        binds[p] = c.padBinds[p];
    }
    if (started_) {
        for (Pad& d : pads_)
            if (trig) d.effectFor = -2;   // the adaptive trigger's strength changed: sent again
        assign();
    }
}

bool Pads::start()
{
    if (started_)
        return true;
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        SDL_Log("pads: no gamepad support (%s)", SDL_GetError());
        return false;
    }
    started_ = true;
    refresh();
    return true;
}

void Pads::stop()
{
    for (Pad& d : pads_) {
        d.player = -1;
        feedback(d);   // the trigger's resistance off
        SDL_CloseGamepad(d.g);
    }
    pads_.clear();
    if (started_)
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    started_ = false;
}

Pads::Pad* Pads::find(SDL_JoystickID id)
{
    for (Pad& d : pads_)
        if (d.id == id) return &d;
    return nullptr;
}

const Pads::Pad* Pads::padFor(int p) const
{
    for (const Pad& d : pads_)
        if (d.player == p) return &d;
    return nullptr;
}

void Pads::refresh()
{
    if (!started_)
        return;
    ++refreshes_;
    int n = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    bool changed = false;
    // the pads that went (unplugged, out of battery, out of range)
    for (size_t i = 0; i < pads_.size();) {
        Pad& d = pads_[i];
        const bool listed = ids && std::find(ids, ids + n, d.id) != ids + n;
        if (listed && SDL_GamepadConnected(d.g)) {
            ++i;
            continue;
        }
        SDL_Log("pads: %s (%u) disconnected%s", d.name.c_str(), unsigned(d.id),
                d.player >= 0 ? (" (player " + std::to_string(d.player + 1) + "'s)").c_str() : "");
        if (d.player >= 0) {
            lost_[d.player] = true;
            lostShown_[d.player] = false;
        }
        SDL_CloseGamepad(d.g);
        pads_.erase(pads_.begin() + std::ptrdiff_t(i));
        changed = true;
    }
    // the pads that came
    for (int i = 0; i < n; ++i) {
        if (find(ids[i]) || (virtualOnly && !SDL_IsJoystickVirtual(ids[i])))   // (the input test: its virtual pads only)
            continue;
        SDL_Gamepad* g = SDL_OpenGamepad(ids[i]);
        if (!g) {
            SDL_Log("pads: cannot open gamepad %u: %s", unsigned(ids[i]), SDL_GetError());
            continue;
        }
        Pad d;
        d.id = ids[i];
        d.g = g;
        d.type = SDL_GetGamepadType(g);
        d.prompt = padStyleOf(ids[i]);
        const char* nm = SDL_GetGamepadName(g);
        d.name = nm && *nm ? nm : "gamepad";
        d.gyro = SDL_GamepadHasSensor(g, SDL_SENSOR_GYRO);
        d.accel = SDL_GamepadHasSensor(g, SDL_SENSOR_ACCEL);
        d.touch = SDL_GetNumGamepadTouchpads(g) > 0;
        d.rate = d.gyro ? SDL_GetGamepadSensorDataRate(g, SDL_SENSOR_GYRO) : 0.0f;
        // autostart (user 2026-10-28): what is held as it comes is its state, not a press (let go first)
        d.openedNs = SDL_GetTicksNS();
        d.openedRefresh = refreshes_;
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && b < 32; ++b)
            if (SDL_GetGamepadButton(g, SDL_GamepadButton(b)))
                d.stale |= 1u << b;
        d.trig[0] = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 30000;   // (the triggers: their hysteresis; pulled all the way)
        d.trig[1] = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 30000;
        if (d.stale || d.trig[0] || d.trig[1])
            SDL_Log("pads: %s: buttons held as it came (0x%X, triggers %d %d): ignored until let go", d.name.c_str(),
                    unsigned(d.stale), d.trig[0] ? 1 : 0, d.trig[1] ? 1 : 0);
        SDL_Log("pads: %s (%u, %s, prompts %s) connected:%s%s%s", d.name.c_str(), unsigned(d.id), SDL_GetGamepadStringForType(d.type), padStyleName(d.prompt),
                d.gyro ? " gyro" : "", d.accel ? " accelerometer" : "", d.touch ? " touchpad" : "");
        pads_.push_back(d);
        changed = true;
    }
    SDL_free(ids);
    if (changed)
        assign();
}

int Pads::styleOf(const Pad& d) const
{
    if (d.player < 0)
        return 1;
    int s = prof[d.player].style;
    if (s <= 0 || s > kPadStyles)
        s = d.gyro && gyroType(d.type) ? 3 : 1;
    if ((s == 2 || s == 3) && !d.gyro)
        return 1;
    return s == 4 && !d.touch ? 1 : s;
}

const PadStyle& Pads::styleSet(const Pad& d) const
{
    static const PadStyle none;
    return d.player < 0 ? none : prof[d.player].s[styleOf(d) - 1];
}

int Pads::touchOf(const Pad& d) const
{
    if (d.player < 0 || !d.touch)
        return 1;
    const int t = styleSet(d).touch;
    return t == 0 ? (styleOf(d) == 4 ? 2 : sonyType(d.type) ? 3 : 1) : t;
}

// the buttons that shoot / reload (bits): the chosen shoot button, R1 and South too; every other one of these reloads
namespace {
constexpr unsigned kBR2 = 1, kBL2 = 2, kBR1 = 4, kBL1 = 8, kBSouth = 16, kBEast = 32, kBWest = 64, kBAll = 127;
}

unsigned Pads::shootMask(const Pad& d) const
{
    const PadStyle& st = styleSet(d);
    static const unsigned kChosen[6] = {kBR2, kBR2, kBL2, kBR1, kBL1, kBSouth};
    return kChosen[std::clamp(st.shoot, 0, 5)] | kBR1 | kBSouth;
}

void Pads::press(Pad& d, unsigned b)
{
    if (shootMask(d) & b) d.eTrigger = true;
    else if (kBAll & b) d.eReload = true;
}

// keybind: the default path's buttons as a table (press(), event()): the shoot mask fires, the rest of R2 / L2 / R1 /
// L1 / South / East / West reloads, Start, Back = the pause, R3 recentres, North held = the gyro's ratchet (tapped:
// recentre), L3 held = precise
PadBinds Pads::defaultBinds(int p) const
{
    Pad tmp;
    const Pad* d = padFor(p);
    if (!d) {   // no pad: the profile's style (Auto: Classic, what a pad without a gyro gets)
        tmp.player = p;
        tmp.gyro = tmp.touch = true;
        d = &tmp;
    }
    static const int kIn[7] = {kPiR2, kPiL2, kPiR1, kPiL1, kPiSouth, kPiEast, kPiWest};   // kBR2 .. kBWest
    const unsigned shoot = d->player >= 0 ? shootMask(*d) : (kBR2 | kBR1 | kBSouth);
    PadBinds b;
    for (int i = 0; i < 7; ++i)
        b.mask[(shoot >> i & 1) ? kPaFire : kPaReload] |= 1u << kIn[i];
    b.mask[kPaStart] = 1u << kPiStart;
    b.mask[kPaPause] = 1u << kPiBack;
    b.mask[kPaRecentre] = 1u << kPiR3;
    b.mask[kPaGyroHold] = 1u << kPiNorth;
    b.mask[kPaPrecise] = 1u << kPiL3;
    return b;
}

void Pads::bound(Pad& d, int in, bool down, uint64_t ts)
{
    const PadBinds& b = binds[d.player];
    for (int a = 0; a < kPadActions; ++a) {
        if (!(b.mask[a] >> in & 1))
            continue;
        switch (a) {
        case kPaFire: if (down) d.eTrigger = d.eChoose = true; break;   // (the pause: the fire buttons choose)
        case kPaReload: if (down) d.eReload = true; break;
        case kPaStart: if (down) d.eStart = true; break;
        case kPaPause: if (down) d.eBack = true; break;
        case kPaRecentre: if (down) d.eRecentre = true; break;
        case kPaGyroHold:   // held: the ratchet; tapped: the middle
            if (down) {
                d.ratchet = true;
                d.northAt = ts;
            } else if (d.ratchet) {
                d.ratchet = false;
                if (ts - d.northAt < kTapNs) d.eRecentre = true;
            }
            break;
        case kPaPrecise:   // held: precise; clicked (padfix): the middle
            if (down) d.preciseAt = ts;
            else if (d.precise && ts - d.preciseAt < kClickNs) d.eRecentre = true;
            d.precise = down;
            break;
        default: break;
        }
    }
}

SDL_GamepadType Pads::padType(int p) const
{
    const Pad* d = padFor(p);
    return d ? d->type : SDL_GAMEPAD_TYPE_UNKNOWN;
}

int Pads::promptStyle(int p) const
{
    const Pad* d = padFor(p);
    return d ? d->prompt : int(SDL_GAMEPAD_TYPE_UNKNOWN);
}

int Pads::styleOf(int p) const
{
    const Pad* d = padFor(p);
    return d ? styleOf(*d) : 0;
}

int Pads::touchOf(int p) const
{
    const Pad* d = padFor(p);
    return d ? touchOf(*d) : 0;
}

std::string Pads::padName(int p) const
{
    const Pad* d = padFor(p);
    return d ? d->name : std::string();
}

void Pads::assign()
{
    for (Pad& d : pads_)
        if (d.player >= 0 && !want[d.player])
            d.player = -1;
    for (int p = 0; p < kInputPlayers; ++p) {
        if (!want[p]) {
            lost_[p] = false;
            continue;
        }
        if (padOf(p))
            continue;
        for (Pad& d : pads_)
            if (d.player < 0) {
                d.player = p;
                lost_[p] = false;
                SDL_Log("pads: player %d: %s (%u)", p + 1, d.name.c_str(), unsigned(d.id));
                break;
            }
    }
    for (Pad& d : pads_) {
        SDL_SetGamepadPlayerIndex(d.g, d.player);   // the player lights (Switch / Xbox 360 / DualSense's LEDs)
        const int s = styleOf(d);
        const bool on = d.gyro && d.player >= 0 && s >= 2 && s <= 3;
        if (on != d.sensorsOn) {
            if (!SDL_SetGamepadSensorEnabled(d.g, SDL_SENSOR_GYRO, on) && on)
                SDL_Log("pads: %s: the gyro cannot be turned on (%s)", d.name.c_str(), SDL_GetError());
            if (d.accel)
                SDL_SetGamepadSensorEnabled(d.g, SDL_SENSOR_ACCEL, on);
            d.sensorsOn = on;
            d.lastTs = d.lastAccTs = 0;
            d.acc[0] = d.acc[1] = d.pend[0] = d.pend[1] = 0;
            d.smooth[0] = d.smooth[1] = 0;
            d.grav[0] = d.grav[1] = d.grav[2] = 0;
            d.shake.reset();
            d.holdUntil = 0;
            d.cal = false;
        }
        feedback(d);
    }
}

void Pads::feedback(Pad& d)
{
    if (d.effectFor == d.player)
        return;
    d.effectFor = d.player;
    const int p = d.player;
    if (p >= 0) {
        const uint8_t* c = kColour[p & 3];
        SDL_SetGamepadLED(d.g, c[0], c[1], c[2]);   // the light bar (DualSense / DualShock 4); others: unsupported
    }
    if (d.type != SDL_GAMEPAD_TYPE_PS5)
        return;
    // the DualSense's adaptive right trigger (SDL's DS5EffectsState_t, 47 bytes: enable bits 1 = 0x04 the right
    // trigger's effect at bytes 10-20). "Gun": the weapon effect (0x25): resistance from zone 2 that gives way at
    // zone 5 with a click, strength 6 of 8 (the effect's layout as in the public DualSense trigger documentation);
    // off: 0x05
    uint8_t fx[47] = {};
    fx[0] = 0x04;
    if (p >= 0 && prof[p].adaptive > 0) {
        static const uint8_t kStrength[4] = {0, 3, 5, 8};   // light, medium, strong (of 8)
        const uint16_t zones = uint16_t((1u << 2) | (1u << 5));
        fx[10] = 0x25;
        fx[11] = uint8_t(zones & 0xFF);
        fx[12] = uint8_t(zones >> 8);
        fx[13] = uint8_t(kStrength[std::clamp(prof[p].adaptive, 1, 3)] - 1);
    } else {
        fx[10] = 0x05;
    }
    if (SDL_SendGamepadEffect(d.g, fx, int(sizeof fx))) {
        if (p >= 0) ++effects[p];
    } else if (p >= 0 && prof[p].adaptive) {
        SDL_Log("pads: %s: no adaptive trigger (%s)", d.name.c_str(), SDL_GetError());
    }
}

int Pads::playerOf(SDL_JoystickID id) const
{
    for (const Pad& d : pads_)
        if (d.id == id)
            return d.player;
    return -1;
}

SDL_JoystickID Pads::padOf(int p) const
{
    const Pad* d = padFor(p);
    return d ? d->id : 0;
}

bool Pads::fireHeld(int p) const
{
    const Pad* d = padFor(p);
    if (!d || !d->g)
        return false;
    auto trig = [d](SDL_GamepadAxis a) { return SDL_GetGamepadAxis(d->g, a) > 16000; };
    auto btn = [d](SDL_GamepadButton b) { return SDL_GetGamepadButton(d->g, b); };
    if (custom(*d)) {   // keybind: the player's Fire inputs
        static const SDL_GamepadButton kBtn[kPadInputs] = {
            SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
            SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_INVALID,
            SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_START,
            SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_MISC1, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
            SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, SDL_GAMEPAD_BUTTON_DPAD_UP,
            SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
        const uint32_t m = binds[d->player].mask[kPaFire];
        for (int in = 0; in < kPadInputs; ++in)
            if (m >> in & 1) {
                if (in == kPiL2 ? trig(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) : in == kPiR2 ? trig(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) : btn(kBtn[in]))
                    return true;
            }
        return false;
    }
    const unsigned m = shootMask(*d);
    return ((m & kBR2) && trig(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) || ((m & kBL2) && trig(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) ||
           ((m & kBR1) && btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) || ((m & kBL1) && btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) ||
           ((m & kBSouth) && btn(SDL_GAMEPAD_BUTTON_SOUTH));
}

void Pads::gyro(Pad& d, const float data[3], uint64_t ts)
{
    // every reading is integrated with its own time (SDL queues every report: several a frame at 250-1000 Hz): from
    // the sensor's timestamps; the first one, or after a gap (readings lost, the gyro off), the pad's data rate
    float dt = std::min(d.rate > 0 ? 1.0f / d.rate : 1.0f / 200.0f, 0.05f);
    if (d.lastTs && ts > d.lastTs && ts - d.lastTs < 50000000ull)
        dt = float(double(ts - d.lastTs) * 1e-9);
    d.lastTs = ts;
    float r[3];
    for (int k = 0; k < 3; ++k) r[k] = data[k] - d.bias[k];
    d.omega = len3(r);
    // gravity in the pad's axes, carried by the turn (a world-fixed vector turns by -omega in the pad's frame); the
    // accelerometer corrects it slowly (accel())
    if (const float gl = len3(d.grav); gl > 0.5f) {
        const float g0[3] = {d.grav[0], d.grav[1], d.grav[2]};
        d.grav[0] += (g0[1] * r[2] - g0[2] * r[1]) * dt;
        d.grav[1] += (g0[2] * r[0] - g0[0] * r[2]) * dt;
        d.grav[2] += (g0[0] * r[1] - g0[1] * r[0]) * dt;
        const float n = len3(d.grav);
        for (float& v : d.grav) v /= n;
    }
    // still? (the drift): the raw rates' running mean and noise, the accelerometer steady
    {
        const float a = 1.0f - std::exp(-dt / 0.5f);
        float dev = 0, mr[3];
        for (int k = 0; k < 3; ++k) {
            d.mW[k] += a * (data[k] - d.mW[k]);
            dev += (data[k] - d.mW[k]) * (data[k] - d.mW[k]);
            mr[k] = d.mW[k] - d.bias[k];
        }
        d.vW += a * (dev - d.vW);
        const bool steady = !d.accel || d.linDev < 0.4f;
        d.stillT = len3(mr) < kStillRate && std::sqrt(d.vW) < kStillNoise && steady ? d.stillT + dt : 0.0f;
    }
    if (d.cal) {   // Calibrate Gyro: 2 s held still = the bias; any motion starts it again; no aim meanwhile
        float dv[3];
        for (int k = 0; k < 3; ++k) dv[k] = d.calN ? data[k] - d.calSum[k] / float(d.calN) : 0.0f;
        if (len3(dv) > 3.0f * kDeg || (d.accel && d.linDev > 0.8f)) {
            d.calT = 0;
            d.calN = 0;
            d.calSum[0] = d.calSum[1] = d.calSum[2] = 0;
        }
        d.calT += dt;
        for (int k = 0; k < 3; ++k) d.calSum[k] += data[k];
        ++d.calN;
        if (d.calT >= 2.0f) {
            for (int k = 0; k < 3; ++k) d.bias[k] = d.mW[k] = d.calSum[k] / float(d.calN);
            d.cal = false;
            d.calDone = 100;
            SDL_Log("pads: %s: gyro calibrated, bias (%.3f, %.3f, %.3f) degrees/s", d.name.c_str(), d.bias[0] / kDeg,
                    d.bias[1] / kDeg, d.bias[2] / kDeg);
        } else {
            d.calDone = int(d.calT / 2.0f * 100.0f);
        }
        d.smooth[0] = d.smooth[1] = 0;
        return;
    }
    if (d.stillT > 1.0f)   // lying still: the bias follows the mean (the drift fix)
        for (int k = 0; k < 3; ++k) d.bias[k] += (d.mW[k] - d.bias[k]) * std::min(1.0f, dt / 1.0f);
    if (d.player < 0)
        return;
    const PadStyle& pr = styleSet(d);
    // shake to reload (game/shake.h): a flick of the wrist (the turn's rate); before this reading's motion is used, so
    // a flick's own turn is held back and dropped
    if (const int lv = shakeLevel(d); lv > 0 && !d.cal) {
        if (d.shake.gyro(r, ts, lv))
            shakeReload(d, ts);   // (user 2026-10-31: the crosshair keeps following the aim: no motion held back)
    }
    // SDL's pad axes: x right, y up, z towards the player; the gyro's rates are counter-clockwise around each. The
    // pointing direction f: the pad's forward -z (relpad: the Sideways grip, its left / right end, is gone)
    const float gl = len3(d.grav);
    float yaw = 0, pitch = 0;
    if (pr.space == 0) {
        // Player space (Gyro Wiki, Jibb Smart): yaw = the turn about gravity of the pad's yaw + roll axes, its size up
        // to their combined turn (x 1.41 relaxed: turning the wrist either way turns the crosshair), pitch = the pad's
        const float yawL = r[1], rollL = r[2];
        const float uY = gl > 0.5f ? d.grav[1] : 1.0f, uF = gl > 0.5f ? d.grav[2] : 0.0f;
        const float wy = yawL * uY + rollL * uF;
        yaw = std::copysign(std::min(std::abs(wy) * 1.41f, std::sqrt(yawL * yawL + rollL * rollL)), wy);
        pitch = r[0];
    } else {
        // World: up = gravity now; Local: the pad's y, or gravity when last recentred. yaw = around up, pitch = around
        // the horizontal axis across f (f flattened to up's plane, crossed with up): a rolled or tilted pad still turns
        // the crosshair left / right when turned left / right and up / down when raised / lowered
        const float f[3] = {0.0f, 0.0f, -1.0f};
        float u[3] = {d.up[0], d.up[1], d.up[2]};
        if (pr.space == 1 && gl > 0.5f)
            for (int k = 0; k < 3; ++k) u[k] = d.grav[k] / gl;
        yaw = dot3(r, u);
        pitch = r[0];
        const float fu = dot3(f, u);
        const float fh[3] = {f[0] - fu * u[0], f[1] - fu * u[1], f[2] - fu * u[2]};
        const float ax[3] = {fh[1] * u[2] - fh[2] * u[1], fh[2] * u[0] - fh[0] * u[2], fh[0] * u[1] - fh[1] * u[0]};
        const float al = len3(ax);
        if (al > 0.2f)   // (pointing straight up / down: the grip's own axis)
            pitch = dot3(r, ax) / al;
    }
    float in[2] = {-yaw, -pitch};   // + = right, + = down (window coordinates), rad/s
    if (pr.invert & 1) in[0] = -in[0];
    if (pr.invert & 2) in[1] = -in[1];
    float mag = std::sqrt(in[0] * in[0] + in[1] * in[1]);
    // the tightening dead zone (off by default): below it the motion is scaled down (rest jitter), no step
    if (const float t = float(std::clamp(pr.tighten, 0, 3)) * kDeg; t > 0 && mag < t && mag > 0) {
        in[0] *= mag / t;
        in[1] *= mag / t;
        mag = mag * mag / t;
    }
    // the tiered smoothing (JoyShockMapper's): below thr / 2 everything goes through the smoothing, from thr 1:1; the
    // smoothed part is an exponential average of the part not passed directly, so no motion is lost, only delayed
    if (const float thr = kSmoothDeg[std::clamp(pr.smooth, 0, 3)] * kDeg; thr > 0) {
        const float w = std::clamp((mag - thr * 0.5f) / (thr * 0.5f), 0.0f, 1.0f);
        const float a = 1.0f - std::exp(-dt / kSmoothTau);
        for (int k = 0; k < 2; ++k) {
            d.smooth[k] += a * (in[k] * (1.0f - w) - d.smooth[k]);
            in[k] = in[k] * w + d.smooth[k];
        }
    } else {
        d.smooth[0] = d.smooth[1] = 0;
    }
    // the acceleration (off by default): the sensitivity from x lo at 5 degrees / s to x hi at 60
    if (const int ga = std::clamp(pr.gaccel, 0, 2); ga > 0) {
        const float t = std::clamp((mag / kDeg - 5.0f) / 55.0f, 0.0f, 1.0f);
        const float m = kAccelLo[ga] + (kAccelHi[ga] - kAccelLo[ga]) * t;
        in[0] *= m;
        in[1] *= m;
    }
    d.liveDeg += (mag / kDeg - d.liveDeg) * std::min(1.0f, dt / 0.1f);
    if (ts >= d.holdUntil && (d.pend[0] != 0 || d.pend[1] != 0)) {
        if (!d.shakeDrop && !d.ratchet) {   // no shake after all: the motion held back is applied
            d.acc[0] += d.pend[0];
            d.acc[1] += d.pend[1];
        }
        d.pend[0] = d.pend[1] = 0;
    }
    if (ts >= d.holdUntil)
        d.shakeDrop = false;
    for (int k = 0; k < 2; ++k) {
        if (d.ratchet)   // North held: the pad turns, the crosshair stays (the ratchet)
            continue;
        if (ts < d.holdUntil) d.pend[k] += in[k] * dt;   // a shake may be starting: held back
        else d.acc[k] += in[k] * dt;
    }
}

void Pads::accel(Pad& d, const float a[3], uint64_t ts)
{
    // at rest the accelerometer reads +g upwards (SDL); gravity's estimate is carried by the gyro (gyro()) and pulled
    // towards the accelerometer's direction while it reads about 1 g (time constant 0.5 s); a pad found far off the
    // estimate while it hardly turns (the start, a pad put down) takes the reading as is
    float dt = std::min(d.rate > 0 ? 1.0f / d.rate : 1.0f / 200.0f, 0.05f);
    if (d.lastAccTs && ts > d.lastAccTs && ts - d.lastAccTs < 50000000ull)
        dt = float(double(ts - d.lastAccTs) * 1e-9);
    d.lastAccTs = ts;
    const float n = len3(a);
    if (n < 1.0f)
        return;
    const float an[3] = {a[0] / n, a[1] / n, a[2] / n};
    const bool oneG = std::abs(n - kG) < 1.0f;
    if (len3(d.grav) < 0.5f || (oneG && d.omega < 1.0f && dot3(an, d.grav) < 0.87f))
        for (int k = 0; k < 3; ++k) d.grav[k] = an[k];
    float lin[3];
    for (int k = 0; k < 3; ++k) lin[k] = a[k] - kG * d.grav[k];   // the pad's own (linear) acceleration
    const float lm = len3(lin);
    d.linDev += (lm - d.linDev) * std::min(1.0f, dt / 0.2f);
    if (std::abs(n - kG) < 1.5f) {
        const float k = std::min(1.0f, dt / 0.5f);
        for (int i = 0; i < 3; ++i) d.grav[i] += k * (an[i] - d.grav[i]);
        const float gl = len3(d.grav);
        for (float& v : d.grav) v /= gl;
    }
    // shake to reload (game/shake.h): a jerk of the pad (its linear acceleration), any axis
    if (d.player < 0 || d.cal)
        return;
    if (const int lv = shakeLevel(d); lv > 0) {
        if (d.shake.accel(lin, dt, ts, lv))
            shakeReload(d, ts);   // (user 2026-10-31: the crosshair keeps following the aim: no motion held back)
    } else {
        d.shake.reset();
    }
}

int Pads::shakeLevel(const Pad& d) const
{
    const int s = styleOf(d);
    return d.player >= 0 && s >= 2 && s <= 3 ? std::clamp(styleSet(d).shake, 0, 3) : 0;
}

void Pads::shakeReload(Pad& d, uint64_t /*ts*/)
{
    d.eShake = true;
    ++d.shakes;
    SDL_Log("pads: %s: shake = reload", d.name.c_str());
}

// autostart (user 2026-10-28): a button's event that is the pad's first state, not a press: one held as the pad was
// opened, or one SDL reports down right after (its first report: 0.3 s; the input test's virtual pads: two refreshes),
// and its let-go. Such a button counts again from its next press
bool Pads::staleButton(const SDL_Event& e)
{
    if (e.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.type != SDL_EVENT_GAMEPAD_BUTTON_UP)
        return false;
    Pad* d = find(e.gbutton.which);
    if (!d || e.gbutton.button >= 32)
        return false;
    const uint32_t bit = 1u << e.gbutton.button;
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
        if (!(d->stale & bit))
            return false;
        d->stale &= ~bit;
        return true;
    }
    constexpr uint64_t kFirstReportNs = 300000000ull;
    const bool first = refreshes_ - d->openedRefresh < 2 || (!virtualOnly && e.gbutton.timestamp < d->openedNs + kFirstReportNs);
    if (!(d->stale & bit) && !first)
        return false;
    d->stale |= bit;
    return true;
}

bool Pads::event(const SDL_Event& e)
{
    if (e.type < SDL_EVENT_JOYSTICK_AXIS_MOTION || e.type >= SDL_EVENT_FINGER_DOWN)
        return false;   // not a joystick / gamepad event
    if (staleButton(e))
        return true;   // (autostart: consumed, nothing done)
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
    case SDL_EVENT_GAMEPAD_REMOVED:
    case SDL_EVENT_GAMEPAD_REMAPPED:
        refresh();
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (Pad* d = find(e.gbutton.which); d && weaponShoulders && d->player >= 0 && d->player < kInputPlayers) {
            // weapons (Survival): L1 / d-pad left the previous weapon, R1 / d-pad right the next
            const int b = e.gbutton.button;
            if (b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER || b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
                --weaponStep[d->player];
                break;
            }
            if (b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER || b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                ++weaponStep[d->player];
                break;
            }
        }
        if (Pad* d = find(e.gbutton.which); d && custom(*d)) {
            // keybind: the player's own buttons; the pause menu's keep theirs (South chooses, East closes, the d-pad picks)
            const SDL_GamepadButton btn = SDL_GamepadButton(e.gbutton.button);
            if (const int in = padInputOf(btn); in >= 0) bound(*d, in, true, e.gbutton.timestamp);
            if (btn == SDL_GAMEPAD_BUTTON_SOUTH) d->eChoose = true;
            else if (btn == SDL_GAMEPAD_BUTTON_EAST) d->eClose = true;
            else if (btn == SDL_GAMEPAD_BUTTON_DPAD_UP) d->eUp = true;
            else if (btn == SDL_GAMEPAD_BUTTON_DPAD_DOWN) d->eDown = true;
            else if (btn == SDL_GAMEPAD_BUTTON_TOUCHPAD) { d->eTouch = true; d->tClicked = true; }
        } else if (d) {
            // the pause menu's buttons (South chooses, East closes, the d-pad picks), then the game's
            const int pb = e.gbutton.button;
            if (pb == SDL_GAMEPAD_BUTTON_SOUTH) d->eChoose = true;
            else if (pb == SDL_GAMEPAD_BUTTON_EAST) d->eClose = true;
            else if (pb == SDL_GAMEPAD_BUTTON_DPAD_UP) d->eUp = true;
            else if (pb == SDL_GAMEPAD_BUTTON_DPAD_DOWN) d->eDown = true;
            switch (pb) {
            case SDL_GAMEPAD_BUTTON_SOUTH: press(*d, kBSouth); break;
            case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: press(*d, kBR1); break;
            case SDL_GAMEPAD_BUTTON_EAST: press(*d, kBEast); break;
            case SDL_GAMEPAD_BUTTON_WEST: press(*d, kBWest); break;
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: press(*d, kBL1); break;
            case SDL_GAMEPAD_BUTTON_NORTH: d->ratchet = true; d->northAt = e.gbutton.timestamp; break;
            case SDL_GAMEPAD_BUTTON_RIGHT_STICK: d->eRecentre = true; break;
            case SDL_GAMEPAD_BUTTON_LEFT_STICK: d->precise = true; d->preciseAt = e.gbutton.timestamp; break;
            case SDL_GAMEPAD_BUTTON_START: d->eStart = true; break;
            case SDL_GAMEPAD_BUTTON_BACK: d->eBack = true; break;
            case SDL_GAMEPAD_BUTTON_TOUCHPAD: d->eTouch = true; d->tClicked = true; break;
            default: break;
            }
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (Pad* d = find(e.gbutton.which); d && custom(*d)) {
            if (const int in = padInputOf(SDL_GamepadButton(e.gbutton.button)); in >= 0) bound(*d, in, false, e.gbutton.timestamp);
        } else if (d) {
            const int b = e.gbutton.button;
            if (b == SDL_GAMEPAD_BUTTON_NORTH) {
                d->ratchet = false;
                if (e.gbutton.timestamp - d->northAt < kTapNs)
                    d->eRecentre = true;   // a tap: the middle
            } else if (b == SDL_GAMEPAD_BUTTON_LEFT_STICK) {
                // padfix: a click (not held for the precise aim) = the middle
                if (d->precise && e.gbutton.timestamp - d->preciseAt < kClickNs)
                    d->eRecentre = true;
                d->precise = false;
            }
        }
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (Pad* d = find(e.gaxis.which)) {
            const float v = e.gaxis.value / 32767.0f;
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                const int k = e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ? 1 : 0;
                // the fire point: a light press (the player's, default 15 % of the travel); let go below 60 % of it
                const float at = std::clamp(d->player >= 0 ? prof[d->player].trigger : 3, 1, 9) * 0.05f;
                if (!d->trig[k] && v >= at) {
                    d->trig[k] = true;
                    if (custom(*d)) {   // keybind: the player's own table
                        bound(*d, k == 1 ? kPiR2 : kPiL2, true, e.gaxis.timestamp);
                    } else {
                        press(*d, k == 1 ? kBR2 : kBL2);
                        if (shootMask(*d) & (k == 1 ? kBR2 : kBL2)) d->eChoose = true;   // the pause: the shoot trigger chooses
                    }
                } else if (d->trig[k] && v < at * 0.6f) {
                    d->trig[k] = false;
                    if (custom(*d)) bound(*d, k == 1 ? kPiR2 : kPiL2, false, e.gaxis.timestamp);
                }
            } else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {   // the pause menu's flick
                if (v < -0.6f && d->flick != -1) { d->flick = -1; d->eUp = true; }
                else if (v > 0.6f && d->flick != 1) { d->flick = 1; d->eDown = true; }
                else if (std::abs(v) < 0.3f) d->flick = 0;
            }
        }
        break;
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
        if (Pad* d = find(e.gsensor.which); d && d->sensorsOn) {
            if (e.gsensor.sensor == SDL_SENSOR_GYRO) {
                gyro(*d, e.gsensor.data, e.gsensor.sensor_timestamp);
            } else if (e.gsensor.sensor == SDL_SENSOR_ACCEL) {   // gravity's "up", the shake
                accel(*d, e.gsensor.data, e.gsensor.sensor_timestamp);
            }
        }
        break;
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
        if (Pad* d = find(e.gtouchpad.which); d && e.gtouchpad.touchpad == 0) {
            const float x = e.gtouchpad.x, y = e.gtouchpad.y;
            if (e.type == SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN && !d->tDown) {
                d->tDown = true;
                d->tFinger = e.gtouchpad.finger;
                d->tAt = e.gtouchpad.timestamp;
                d->tX0 = d->tX = d->tFx = x;
                d->tY0 = d->tY = d->tFy = y;
                d->tMoved = 0;
                d->tClicked = false;
            } else if (d->tDown && e.gtouchpad.finger == d->tFinger) {
                // the dead band: a resting finger's jitter moves nothing (pointer and trackpad alike)
                const float fx0 = d->tFx, fy0 = d->tFy;
                band(x, d->tFx, kTouchBand);
                band(y, d->tFy, kTouchBand);
                d->tDx += d->tFx - fx0;
                d->tDy += d->tFy - fy0;
                d->tX = x;
                d->tY = y;
                d->tMoved = std::max(d->tMoved, std::hypot(x - d->tX0, y - d->tY0));
                if (e.type == SDL_EVENT_GAMEPAD_TOUCHPAD_UP) {
                    // a tap (short, still, not a click): the touchpad's action, when the touchpad is on
                    if (!d->tClicked && e.gtouchpad.timestamp - d->tAt < kTapNs && d->tMoved < 0.03f && touchOf(*d) != 1)
                        d->eTouch = true;
                    d->tDown = false;
                    d->tFinger = -1;
                }
            }
        }
        break;
    default:
        break;
    }
    return true;
}

bool Pads::menuKey(const SDL_Event& e, SDL_Event& key)
{
    if (e.type == SDL_EVENT_GAMEPAD_ADDED || e.type == SDL_EVENT_GAMEPAD_REMOVED) {
        refresh();
        return false;
    }
    SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;
    SDL_Keycode k = 0;
    bool down = true;
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
        down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        if (!find(e.gbutton.which))
            return false;   // (the input test: a physical pad's buttons are not menu keys; autostart: a pad not opened yet)
        if (staleButton(e))
            return false;   // (autostart: held as it came)
        switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: sc = SDL_SCANCODE_UP; k = SDLK_UP; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: sc = SDL_SCANCODE_DOWN; k = SDLK_DOWN; break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: sc = SDL_SCANCODE_LEFT; k = SDLK_LEFT; break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: sc = SDL_SCANCODE_RIGHT; k = SDLK_RIGHT; break;
        case SDL_GAMEPAD_BUTTON_SOUTH:
        case SDL_GAMEPAD_BUTTON_START: sc = SDL_SCANCODE_RETURN; k = SDLK_RETURN; break;
        case SDL_GAMEPAD_BUTTON_EAST:
        case SDL_GAMEPAD_BUTTON_BACK: sc = SDL_SCANCODE_ESCAPE; k = SDLK_ESCAPE; break;
        default: return false;
        }
    } else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
               (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY)) {
        Pad* d = find(e.gaxis.which);
        if (!d)
            return false;
        const float v = e.gaxis.value / 32767.0f;
        const bool y = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY;
        int& f = y ? d->flick : d->flickX;
        const int now = v < -0.6f ? -1 : v > 0.6f ? 1 : std::abs(v) < 0.3f ? 0 : f;
        if (now == f)
            return false;
        const int was = f;
        f = now;
        const int dir = now != 0 ? now : was;   // the flick's key: down when it starts, up when the stick comes back
        down = now != 0;
        sc = y ? (dir < 0 ? SDL_SCANCODE_UP : SDL_SCANCODE_DOWN) : (dir < 0 ? SDL_SCANCODE_LEFT : SDL_SCANCODE_RIGHT);
        k = y ? (dir < 0 ? SDLK_UP : SDLK_DOWN) : (dir < 0 ? SDLK_LEFT : SDLK_RIGHT);
    } else {
        return false;
    }
    key = SDL_Event{};
    key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    key.key.scancode = sc;
    key.key.key = k;
    key.key.down = down;
    return true;
}

void Pads::reset(GunState guns[], int lw, int lh)
{
    for (int p = 0; p < kInputPlayers; ++p) {
        recentre_[p] = false;
        lives_[p] = score_[p] = 0;
        reloads_[p] = 0;
        if (want[p]) {
            guns[p].x = lw * 0.5f;
            guns[p].y = lh * 0.5f;
        }
    }
    for (Pad& d : pads_) {
        d.acc[0] = d.acc[1] = 0;
        d.tDx = d.tDy = 0;
        d.eTrigger = d.eReload = d.eShake = d.eStart = d.eBack = d.eRecentre = d.eUp = d.eDown = d.eChoose = d.eClose = d.eTouch = false;
    }
}

// the crosshair to the middle; with a gyro the pad's present "up" becomes local space's (the calibration: point the
// pad at the screen's centre, as held, and press)
void Pads::recentre(Pad& d, GunState& g, int lw, int lh)
{
    g.x = lw * 0.5f;
    g.y = lh * 0.5f;
    d.acc[0] = d.acc[1] = 0;
    const float gl = std::sqrt(dot3(d.grav, d.grav));
    if (gl > 0.5f)
        for (int k = 0; k < 3; ++k) d.up[k] = d.grav[k] / gl;
}

void Pads::apply(Runtime& rt, GunState guns[], Pause& pause, PauseInput& pin, int lw, int lh)
{
    const int mode = gameMode(rt);
    for (int p = 0; p < kInputPlayers; ++p)
        if (lost_[p] && !lostShown_[p]) {   // the player's pad went: the pause (in play), the notice meanwhile
            lostShown_[p] = true;
            if (lostPause && mode == 4 && !pause.paused())
                pause.focus(false);
        }
    const bool paused = pause.paused();
    for (Pad& d : pads_) {
        const int p = d.player;
        if (p >= 0 && recentre_[p]) {
            recentre_[p] = false;
            recentre(d, guns[p], lw, lh);
        }
        if (p >= 0 && paused) {
            pin.up = pin.up || d.eUp;
            pin.down = pin.down || d.eDown;
            pin.activate = pin.activate || d.eChoose;
            pin.toggle = pin.toggle || d.eStart || d.eBack || d.eClose;
            d.acc[0] = d.acc[1] = 0;   // the motion while paused is dropped
            d.tDx = d.tDy = 0;
        } else if (p >= 0) {
            GunState& g = guns[p];
            const int t = touchOf(d);
            const PadStyle& st = styleSet(d);
            if (t == 2 && d.tDown) {
                // the touchpad as a pointer: the touch's point (after the dead band) is the crosshair's; the pad's
                // whole area = the window's (both edges reachable: its outer 4.5 % aims off the window = the reload)
                const float m = lh * 0.05f;
                g.x = std::clamp((d.tFx * 1.1f - 0.05f) * float(lw), -m, float(lw) + m);
                g.y = std::clamp((d.tFy * 1.1f - 0.05f) * float(lh), -m, float(lh) + m);
            }
            // the crosshair pointed off the window (the stick / gyro / touchpad past the edge): the light gun's reload
            // gesture (main's off-screen shot). padfix (user 2026-10-30: "if you do, the reticle gets reset to the
            // centre"): it stays where it is (was: back to the middle with a gyro); R3 / a left stick click recentre
            if (d.eTrigger || (d.eTouch && st.touchClick == 1))
                g.trigger = true;
            if (d.eReload || (d.eTouch && st.touchClick == 0)) g.reload = true;
            // shake to reload (user 2026-10-31: it skipped cut-scenes): only in play with the shots on
            if (d.eShake && mode == 4 && playerState(rt, gamePlayer[p]) == 5 && rt.mem.u32(G::shot_enable) != 0)
                g.reload = true;
            if (d.eRecentre) recentre(d, g, lw, lh);
            if (d.eStart) {
                if (mode == 4 && playerState(rt, gamePlayer[p]) == 5) pin.toggle = true;   // in play: the pause
                else if (onJoin) onJoin(p);                                    // start / join / continue
                else rt.player_join(p);
            }
            if (d.eBack && mode == 4) pin.toggle = true;
        }
        d.eTrigger = d.eReload = d.eShake = d.eStart = d.eBack = d.eRecentre = d.eUp = d.eDown = d.eChoose = d.eClose = d.eTouch = false;
    }
    // vibration: getting hit (a life lost in play), hitting an enemy (the score went up), a reload; each its strength
    for (int p = 0; p < kInputPlayers; ++p) {
        const PadProfile& pr = prof[p];
        static const float kHit[4] = {0, 0.45f, 0.7f, 1.0f}, kEnemy[4] = {0, 0.2f, 0.4f, 0.65f}, kReload[4] = {0, 0.2f, 0.35f, 0.55f};
        const int gp = gamePlayer[p];   // (netplay: this PC's player is the game's player 1 or 2)
        const bool inPlay = playerState(rt, gp) == 5;
        const int lives = playerLives(rt, gp), score = int32_t(rt.mem.u32(Runtime::player_rec(gp) + 8));
        if (pr.rumbleHit > 0 && inPlay && lives < lives_[p])
            rumble(p, kHit[std::clamp(pr.rumbleHit, 0, 3)], kHit[std::clamp(pr.rumbleHit, 0, 3)] * 0.8f, 300);
        else if (pr.rumbleEnemy > 0 && inPlay && score > score_[p])
            rumble(p, kEnemy[std::clamp(pr.rumbleEnemy, 0, 3)] * 0.5f, kEnemy[std::clamp(pr.rumbleEnemy, 0, 3)], 70);
        else if (pr.rumbleReload > 0 && inPlay && rt.reloadsIn[gp] != reloads_[p])
            rumble(p, kReload[std::clamp(pr.rumbleReload, 0, 3)], kReload[std::clamp(pr.rumbleReload, 0, 3)] * 0.5f, 90);
        lives_[p] = lives;
        score_[p] = score;
        reloads_[p] = rt.reloadsIn[gp];
    }
}

namespace {
// the aiming / fine sticks of a style (Classic: left aims by default, the other fine; Motion + stick: right fine)
void stickAxes(const PadStyle& st, SDL_GamepadAxis& ax, SDL_GamepadAxis& ay, SDL_GamepadAxis& fxA, SDL_GamepadAxis& fyA)
{
    const bool leftAims = st.stick == 0;
    ax = leftAims ? SDL_GAMEPAD_AXIS_LEFTX : SDL_GAMEPAD_AXIS_RIGHTX;
    ay = leftAims ? SDL_GAMEPAD_AXIS_LEFTY : SDL_GAMEPAD_AXIS_RIGHTY;
    fxA = leftAims ? SDL_GAMEPAD_AXIS_RIGHTX : SDL_GAMEPAD_AXIS_LEFTX;
    fyA = leftAims ? SDL_GAMEPAD_AXIS_RIGHTY : SDL_GAMEPAD_AXIS_LEFTY;
}
float turboOf(const PadStyle& st, float edgeT)
{
    return 1.0f + (kTurbo[std::clamp(st.turbo, 0, 2)] - 1.0f) * std::clamp((edgeT - 0.15f) / 0.4f, 0.0f, 1.0f);
}
} // namespace

void Pads::aim(GunState guns[], int lw, int lh, float dt)
{
    dt = std::clamp(dt, 0.0f, 0.1f);   // (a stall: no jump)
    for (Pad& d : pads_) {
        const int p = d.player;
        if (p < 0)
            continue;
        const int s = styleOf(d), t = touchOf(d);
        const PadStyle& st = styleSet(d);
        const float slow = d.precise ? kPrecise : 1.0f;
        float dx = 0, dy = 0;
        if (s == 1 || s == 3) {   // the sticks: Classic = one aims, the other fine-adjusts; Motion + stick = the fine one
            const float dead = kDead[std::clamp(st.dead, 0, 6)], curve = kCurve[std::clamp(st.curve, 0, 3)];
            SDL_GamepadAxis ax, ay, fxA, fyA;
            stickAxes(st, ax, ay, fxA, fyA);
            float vx = 0, vy = 0, fx = 0, fy = 0;
            if (s == 1) {
                const float lx = axis(d.g, ax), ly = axis(d.g, ay);
                stickCurve(lx, ly, dead, curve, vx, vy);
                // the edge turbo: held at full tilt (not while precise)
                d.edgeT = std::sqrt(lx * lx + ly * ly) >= kOuter && !d.precise ? d.edgeT + dt : 0.0f;
                const float mult = turboOf(st, d.edgeT);
                vx *= mult;
                vy *= mult;
            }
            stickCurve(axis(d.g, fxA), axis(d.g, fyA), dead, curve, fx, fy);
            // pixels a second at full tilt (sens36, GitHub #36): the aiming stick's own Stick Sensitivity, the fine
            // stick's own Fine Stick Sensitivity (Classic's other stick, Motion + stick's), nothing shared on top
            const float speed = float(lh) * kStickTop * sensOf(st.stickSens), fine = float(lh) * kFineTop * sensOf(st.fineSens);
            float sx = (vx * speed + fx * fine) * dt * slow, sy = (vy * speed + fy * fine) * dt * slow;
            if (s == 1 && (st.invert & 1)) sx = -sx;   // (Motion + stick: the invert is the gyro's)
            if (s == 1 && (st.invert & 2)) sy = -sy;
            dx += sx;
            dy += sy;
        }
        if (s >= 2 && s <= 3) {   // the gyro: the turn since the last call (every reading integrated)
            // (#36, sens36: Gyro Sensitivity, the gyro's whole speed)
            const float perRad = float(lw) * sensOf(st.gyroSens) / (kSpanDeg * kDeg);
            dx += d.acc[0] * perRad * slow;
            dy += d.acc[1] * perRad * slow;
        }
        d.acc[0] = d.acc[1] = 0;
        if (t == 3) {   // the trackpad: the finger's motion (the touchpad about 2.2 : 1: the same speed both ways)
            // (#36, sens36: Trackpad Sensitivity, the trackpad's whole speed in every style)
            const float k = float(lw) * 1.2f * sensOf(st.touchSens) * slow;
            float tx = d.tDx * k, ty = d.tDy * k * 0.45f;
            if (s == 4 || s == 1) {   // (the gyro's invert is applied to its own rates)
                if (st.invert & 1) tx = -tx;
                if (st.invert & 2) ty = -ty;
            }
            dx += tx;
            dy += ty;
        }
        d.tDx = d.tDy = 0;
        dy *= float(std::clamp(st.vert, 5, 15)) / 10.0f;
        // (sens36: no Aim Sensitivity over them any more: each input's own speed above is its whole speed)
        // the stick, the gyro and the touchpad, as a light gun, can point a little past the window's edges: a trigger
        // pull there is the off-screen reload (padfix, user 2026-10-30: "you can't shoot outside the screen": the
        // sticks stopped at the edge before)
        const float m = float(lh) * kMargin;
        GunState& g = guns[p];
        g.x = std::clamp(g.x + dx, -m, float(lw) + m);
        g.y = std::clamp(g.y + dy, -m, float(lh) + m);
    }
}

Pads::AimProbe Pads::probe(int p, int lw, int lh) const
{
    AimProbe a;
    const Pad* d = padFor(p);
    if (!d)
        return a;
    a.pad = true;
    const int s = styleOf(*d);
    const PadStyle& st = styleSet(*d);
    a.gyro = d->gyro && s >= 2 && s <= 3;
    a.shakes = d->shakes;
    a.calib = d->cal ? std::min(d->calDone, 99) : d->calDone;
    a.driftDeg = len3(d->bias) / kDeg;
    float widths = 0;
    if (a.gyro) {
        a.gyroDeg = d->liveDeg;
        a.spanDeg = kSpanDeg / sensOf(st.gyroSens);   // (sens36: Gyro Sensitivity alone)
        widths = a.gyroDeg / a.spanDeg;
    }
    if (s == 1 || s == 3) {
        SDL_GamepadAxis ax, ay, fxA, fyA;
        stickAxes(st, ax, ay, fxA, fyA);
        if (s == 3) ax = fxA, ay = fyA;
        const float lx = axis(d->g, ax), ly = axis(d->g, ay);
        float vx, vy;
        stickCurve(lx, ly, kDead[std::clamp(st.dead, 0, 6)], kCurve[std::clamp(st.curve, 0, 3)], vx, vy);
        a.stick = std::min(1.0f, std::sqrt(lx * lx + ly * ly));
        // (sens36: Classic's aiming stick x its Stick Sensitivity; Motion + stick's fine stick against Classic's 100 %
        // top speed, x its Fine Stick Sensitivity: 40 % at 100 %, as Fine Aim Speed's default before)
        a.stickOut = std::sqrt(vx * vx + vy * vy) * (s == 1 ? turboOf(st, d->edgeT) : kFineTop / kStickTop * sensOf(st.fineSens));
        widths += a.stickOut * kStickTop * (s == 1 ? sensOf(st.stickSens) : 1.0f) * float(lh) / float(std::max(lw, 1));
    }
    a.widths = widths * (d->precise ? kPrecise : 1.0f);
    return a;
}

void Pads::calibrate(int p)
{
    for (Pad& d : pads_)
        if (d.player == p && d.gyro) {
            d.cal = true;
            d.calT = 0;
            d.calN = 0;
            d.calSum[0] = d.calSum[1] = d.calSum[2] = 0;
            d.calDone = 0;
        }
}

bool Pads::calibrating(int p) const
{
    const Pad* d = padFor(p);
    return d && d->cal;
}

void Pads::gyroBias(int p, float out[3]) const
{
    const Pad* d = padFor(p);
    for (int k = 0; k < 3; ++k) out[k] = d ? d->bias[k] : 0.0f;
}

std::vector<std::string> Pads::notice() const
{
    std::vector<std::string> l;
    for (int p = 0; p < kInputPlayers; ++p)
        if (lost_[p])
            l.push_back("PLAYER " + std::to_string(p + 1) + ": CONTROLLER DISCONNECTED - connect it again");
    return l;
}

void Pads::rumble(int p, float low, float high, int ms)
{
    for (Pad& d : pads_)
        if (d.player == p) {
            SDL_RumbleGamepad(d.g, Uint16(std::clamp(low, 0.0f, 1.0f) * 65535), Uint16(std::clamp(high, 0.0f, 1.0f) * 65535), Uint32(ms));
            ++rumbles[p];
        }
}

} // namespace hotd::game

namespace hotd::game {
void Pads::rangeApply(GunState guns[], int lw, int lh)
{
    for (Pad& d : pads_) {
        const int p = d.player;
        if (p >= 0) {
            if (recentre_[p]) {
                recentre_[p] = false;
                recentre(d, guns[p], lw, lh);
            }
            GunState& g = guns[p];
            const int t = touchOf(d);
            const PadStyle& st = styleSet(d);
            if (t == 2 && d.tDown) {   // (the touchpad as a pointer, as in play)
                const float m = lh * 0.05f;
                g.x = std::clamp((d.tFx * 1.1f - 0.05f) * float(lw), -m, float(lw) + m);
                g.y = std::clamp((d.tFy * 1.1f - 0.05f) * float(lh), -m, float(lh) + m);
            }
            if (d.eTrigger || (d.eTouch && st.touchClick == 1))
                g.trigger = true;   // (padfix: off the window it stays there, as in play)
            if (d.eReload || (d.eTouch && st.touchClick == 0)) g.reload = true;
            if (d.eRecentre) recentre(d, g, lw, lh);
        }
        d.eTrigger = d.eReload = d.eShake = d.eStart = d.eBack = d.eRecentre = d.eUp = d.eDown = d.eChoose = d.eClose = d.eTouch = false;
    }
}
} // namespace hotd::game
