// --input-test's gamepad / gyro / touchpad / light-gun cases (pads_test.h).
#include "game/pads_test.h"

#include "game/menu_cursor.h"
#include "game/menu_pages.h"
#include "game/pad_prompts.h"
#include "game/pause.h"
#include "game/runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace hotd::game {

namespace {

using G = A::G;
constexpr float kPi = 3.14159265f;

// sens36 (GitHub #36): the fix section's independence trials from frame kInd0, kIndLen frames each: per (style, input)
// three runs (all four speeds 100 %; the input 100 % and every other 250 %; the input 200 %, the others 100 %), then
// two with the profile an older launcher.ini converted to (Classic's stick, Motion + stick's gyro)
constexpr int kInd0 = 90, kIndLen = 16, kIndInputs = 6, kIndTrials = kIndInputs * 3 + 2;
struct IndInput { int style, input; };   // input 0 the aiming stick, 1 the fine stick, 2 the gyro, 3 the trackpad
IndInput indInput(int t)
{
    static const IndInput k[kIndInputs] = {{1, 0}, {1, 1}, {1, 3}, {3, 1}, {3, 2}, {3, 3}};
    return t < kIndInputs * 3 ? k[t / 3] : t == kIndInputs * 3 ? IndInput{1, 0} : IndInput{3, 2};
}
const char* const kIndNames[4] = {"Stick", "Fine Stick", "Gyro", "Trackpad"};
static_assert(PadTest::kFixFrames > kInd0 + kIndTrials * kIndLen + 2, "the fix section's frames: the trials and the end");

// a virtual pad and what the game sent it (rumble, the light bar, the DualSense effect)
struct VPad {
    SDL_JoystickID id = 0;
    SDL_Joystick* j = nullptr;
    int rumbles = 0, effects = 0;
    Uint16 lo = 0, hi = 0;
    uint8_t fx[47] = {};
    int led[3] = {-1, -1, -1};
    uint64_t ts = 1000000000ull;   // the sensors' timestamps (ns): 1/60 s a frame
};

bool SDLCALL vRumble(void* u, Uint16 lo, Uint16 hi)
{
    VPad* v = static_cast<VPad*>(u);
    ++v->rumbles;
    v->lo = lo;
    v->hi = hi;
    return true;
}
bool SDLCALL vLed(void* u, Uint8 r, Uint8 g, Uint8 b)
{
    VPad* v = static_cast<VPad*>(u);
    v->led[0] = r;
    v->led[1] = g;
    v->led[2] = b;
    return true;
}
bool SDLCALL vEffect(void* u, const void* data, int size)
{
    VPad* v = static_cast<VPad*>(u);
    ++v->effects;
    std::copy(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + std::min(size, 47), v->fx);
    return true;
}
bool SDLCALL vSensors(void*, bool) { return true; }

} // namespace

struct PadTest::Impl {
    VPad a, b, c;   // a DualSense, an Xbox pad, a Switch Pro
    VPad d, e;      // 4P (601-630): players 3 / 4's Xbox pads
    uint32_t shots4[2] = {}, reloads4[2] = {};
    float mark[2][2] = {};
    int evdevSeen = 0;   // (linuxmice, 596: the mice counted before the switch)
    int rumbles0 = 0;
    int fixChecks = 0;   // padfix: the fix section's checks so far
    float ind[kIndTrials] = {};   // sens36: each independence trial's x motion
    PadProfile migrated;          // sens36: player 1's profile from an older launcher.ini (fixAfter 1)

    // kind 0: DualSense (gyro, accelerometer, touchpad), 1: Xbox (none), 2: Switch Pro (gyro, accelerometer)
    bool attach(VPad& v, int kind)
    {
        static const SDL_VirtualJoystickSensorDesc sensors[2] = {{SDL_SENSOR_GYRO, 60.0f}, {SDL_SENSOR_ACCEL, 60.0f}};
        static const SDL_VirtualJoystickTouchpadDesc touch[1] = {{2, {0, 0, 0}}};
        SDL_VirtualJoystickDesc d;
        SDL_INIT_INTERFACE(&d);
        d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        d.naxes = SDL_GAMEPAD_AXIS_COUNT;
        d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        d.vendor_id = kind == 0 ? 0x054C : kind == 1 ? 0x045E : 0x057E;
        d.product_id = kind == 0 ? 0x0CE6 : kind == 1 ? 0x0B12 : 0x2009;
        d.name = kind == 0 ? "Test DualSense" : kind == 1 ? "Test Xbox pad" : "Test Switch Pro";
        d.nsensors = kind == 1 ? 0 : 2;
        d.sensors = kind == 1 ? nullptr : sensors;
        d.ntouchpads = kind == 0 ? 1 : 0;
        d.touchpads = kind == 0 ? touch : nullptr;
        d.userdata = &v;
        d.Rumble = vRumble;
        d.SetLED = vLed;
        d.SendEffect = vEffect;
        d.SetSensorsEnabled = vSensors;
        v = VPad{};
        v.id = SDL_AttachVirtualJoystick(&d);
        v.j = v.id ? SDL_OpenJoystick(v.id) : nullptr;
        if (!v.j)
            SDL_Log("pad test: virtual pad %d: %s", kind, SDL_GetError());
        return v.j != nullptr;
    }
    void detach(VPad& v)
    {
        if (v.j) SDL_CloseJoystick(v.j);
        if (v.id) SDL_DetachVirtualJoystick(v.id);
        v.j = nullptr;
        v.id = 0;
    }
    void button(VPad& v, SDL_GamepadButton btn, bool down) { SDL_SetJoystickVirtualButton(v.j, btn, down); }
    void axis(VPad& v, SDL_GamepadAxis ax, float f)   // sticks -1..1, triggers 0..1 (the virtual trigger's raw range)
    {
        const bool trig = ax == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || ax == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        const float raw = trig ? -32768.0f + f * 65535.0f : f * 32767.0f;
        SDL_SetJoystickVirtualAxis(v.j, ax, Sint16(std::clamp(raw, -32768.0f, 32767.0f)));
    }
    // one frame's sensor readings (SDL 3.2's virtual pads keep one reading per update: flushed after each)
    void sensors(VPad& v, const float* accel, const float* gyro)
    {
        if (accel) {
            SDL_SendJoystickVirtualSensorData(v.j, SDL_SENSOR_ACCEL, v.ts, accel, 3);
            SDL_UpdateJoysticks();
        }
        if (gyro) {
            SDL_SendJoystickVirtualSensorData(v.j, SDL_SENSOR_GYRO, v.ts, gyro, 3);
            SDL_UpdateJoysticks();
        }
        v.ts += 16666667ull;
    }
    // n gyro readings in one frame (a real pad's 250-1000 Hz), each with its own timestamp
    void gyroN(VPad& v, const float* gyro, int n)
    {
        for (int k = 0; k < n; ++k) {
            SDL_SendJoystickVirtualSensorData(v.j, SDL_SENSOR_GYRO, v.ts, gyro, 3);
            SDL_UpdateJoysticks();
            v.ts += 16666667ull / uint64_t(n);
        }
    }
    void touch(VPad& v, bool down, float x, float y) { SDL_SetJoystickVirtualTouchpad(v.j, 0, 0, down, x, y, down ? 1.0f : 0.0f); }
};

PadTest::PadTest() : m_(std::make_unique<Impl>()) {}
PadTest::~PadTest() = default;

bool PadTest::noGameFrame(int i) const
{
    return i == 333 || i == 334 || i == 337 || (i >= 372 && i <= 374) || i == 563 || i == 564 || i == 587 || i == 588;
}

void PadTest::before(int i, Ctx& c)
{
    Impl& m = *m_;
    if (i < kFirst || i > kLast)
        return;
    GunState* g = c.guns;
    auto markGuns = [&]() {
        for (int p = 0; p < 2; ++p) { m.mark[p][0] = g[p].x; m.mark[p][1] = g[p].y; }
    };
    auto topUp = [&]() {   // (the test runs long: nobody runs out of lives meanwhile)
        for (uint32_t p = 0; p < 2; ++p)
            if (c.rt.mem.u32(G::players + p * 0xCC) == 5 && int32_t(c.rt.mem.u32(G::players + p * 0xCC + 0xC)) < 5)
                c.rt.mem.set32(G::players + p * 0xCC + 0xC, 9);
    };
    PadProfile& p2 = c.cfg.pad[1];
    const float flat[3] = {0, 9.81f, 0}, rolled[3] = {-9.81f, 0, 0};
    if (i == 331 || (i >= 400 && i % 50 == 0)) topUp();
    // a DualSense for player 2 (player 1 stays on the mouse): Classic first
    if (i == 331) {
        m.attach(m.a, 0);
        c.cfg.p2Input = 2;
        p2 = PadProfile{};
        p2.style = 1;
        c.pads.configure(c.cfg);
    }
    if (i == 333 || i == 335) m.button(m.a, SDL_GAMEPAD_BUTTON_START, true);   // (player 2 joined at 306: in play)
    if (i == 334 || i == 336) m.button(m.a, SDL_GAMEPAD_BUTTON_START, false);
    if (i == 337) m.button(m.a, SDL_GAMEPAD_BUTTON_BACK, true);
    if (i == 338) { m.button(m.a, SDL_GAMEPAD_BUTTON_BACK, false); m.button(m.a, SDL_GAMEPAD_BUTTON_EAST, true); }
    if (i == 339) m.button(m.a, SDL_GAMEPAD_BUTTON_EAST, false);
    if (i == 339) { markGuns(); m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 0.5f); }
    if (i == 349) m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 0);
    if (i == 350) { markGuns(); m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 0.04f); }
    if (i == 355) m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 0);
    if (i >= 356 && i <= 361) {
        static const float t[6] = {0.16f, 0.10f, 0.20f, 0.0f, 0.16f, 0.0f};
        m.axis(m.a, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, t[i - 356]);
    }
    if (i == 361) m.rumbles0 = m.a.rumbles;
    if (i == 362) m.button(m.a, SDL_GAMEPAD_BUTTON_EAST, true);
    if (i == 363) m.button(m.a, SDL_GAMEPAD_BUTTON_EAST, false);
    if (i == 364) m.button(m.a, SDL_GAMEPAD_BUTTON_WEST, true);
    if (i == 365) m.button(m.a, SDL_GAMEPAD_BUTTON_WEST, false);
    if (i == 366) m.axis(m.a, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1.0f);
    if (i == 367) m.axis(m.a, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0);
    if (i == 368) { m.rumbles0 = m.a.rumbles; c.rt.mem.set32(G::players + 0xCC + 0xC, c.rt.mem.u32(G::players + 0xCC + 0xC) - 1); }
    if (i == 370) { m.rumbles0 = m.a.rumbles; c.rt.mem.set32(G::players + 0xCC + 8, c.rt.mem.u32(G::players + 0xCC + 8) + 100); }
    if (i == 372) m.button(m.a, SDL_GAMEPAD_BUTTON_START, true);
    if (i == 373) { m.button(m.a, SDL_GAMEPAD_BUTTON_START, false); m.button(m.a, SDL_GAMEPAD_BUTTON_DPAD_DOWN, true); }
    if (i == 374) { m.button(m.a, SDL_GAMEPAD_BUTTON_DPAD_DOWN, false); m.button(m.a, SDL_GAMEPAD_BUTTON_DPAD_UP, true); }
    if (i == 375) { m.button(m.a, SDL_GAMEPAD_BUTTON_DPAD_UP, false); m.button(m.a, SDL_GAMEPAD_BUTTON_START, true); }
    if (i == 376) m.button(m.a, SDL_GAMEPAD_BUTTON_START, false);
    if (i == 377) { markGuns(); m.axis(m.a, SDL_GAMEPAD_AXIS_RIGHTX, 1.0f); }
    if (i == 387) m.axis(m.a, SDL_GAMEPAD_AXIS_RIGHTX, 0);
    if (i == 388) { markGuns(); m.button(m.a, SDL_GAMEPAD_BUTTON_LEFT_STICK, true); m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 1.0f); }
    if (i == 398) { m.button(m.a, SDL_GAMEPAD_BUTTON_LEFT_STICK, false); m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 0); }
    if (i == 399) { p2.s[0].invert = 2; c.pads.configure(c.cfg); markGuns(); m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTY, 0.5f); }
    if (i == 404) { m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTY, 0); p2.s[0].invert = 0; c.pads.configure(c.cfg); }
    // Motion only: the gyro (yaw, pitch; world space with the accelerometer: a rolled pad), the sticks ignored
    if (i == 405) { p2.style = 2; c.pads.configure(c.cfg); }
    if (i == 406) m.sensors(m.a, flat, nullptr);
    if (i == 407) { markGuns(); m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 1.0f); }
    if (i >= 407 && i <= 416) { const float r[3] = {0, -0.5f, 0}; m.sensors(m.a, nullptr, r); }
    if (i == 417) m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 0);
    if (i == 418) markGuns();
    if (i >= 418 && i <= 427) { const float r[3] = {0.3f, 0, 0}; m.sensors(m.a, nullptr, r); }
    if (i == 429) { p2.s[1].space = 1; c.pads.configure(c.cfg); }   // World (the default Player space: below)
    if (i >= 429 && i <= 468) {
        if (i == 459) { g[1].x = c.lw * 0.5f; markGuns(); }   // (padfix: the faster sticks left it near the edge)
        const float r[3] = {0.5f, 0, 0};   // the pad rolled onto its right side, turned right
        m.sensors(m.a, rolled, i >= 459 ? r : nullptr);
    }
    if (i == 470) { p2.s[1].space = 2; c.pads.configure(c.cfg); markGuns(); }   // Local
    if (i >= 470 && i <= 479) { const float r[3] = {0.5f, 0, 0}; m.sensors(m.a, nullptr, r); }
    if (i == 480) { p2.s[1].space = 0; c.pads.configure(c.cfg); }
    // Motion + stick: the ratchet (North held), the fine stick, North tapped = the middle, the off-screen reload
    if (i == 481) { p2.style = 1; c.pads.configure(c.cfg); }
    if (i == 482) { p2.style = 3; c.pads.configure(c.cfg); }
    if (i == 483) m.sensors(m.a, flat, nullptr);
    if (i == 484) { markGuns(); m.button(m.a, SDL_GAMEPAD_BUTTON_NORTH, true); }
    if (i >= 484 && i <= 493) { const float r[3] = {0, -0.5f, 0}; m.sensors(m.a, nullptr, r); }
    if (i == 504) m.button(m.a, SDL_GAMEPAD_BUTTON_NORTH, false);
    if (i == 505) m.button(m.a, SDL_GAMEPAD_BUTTON_RIGHT_STICK, true);
    if (i == 506) { m.button(m.a, SDL_GAMEPAD_BUTTON_RIGHT_STICK, false); markGuns(); m.axis(m.a, SDL_GAMEPAD_AXIS_RIGHTX, 1.0f); }
    if (i == 516) m.axis(m.a, SDL_GAMEPAD_AXIS_RIGHTX, 0);
    if (i == 517) m.button(m.a, SDL_GAMEPAD_BUTTON_NORTH, true);
    if (i == 518) m.button(m.a, SDL_GAMEPAD_BUTTON_NORTH, false);
    if (i >= 519 && i <= 528) { const float r[3] = {0, -3.0f, 0}; m.sensors(m.a, nullptr, r); }
    if (i == 529) m.button(m.a, SDL_GAMEPAD_BUTTON_SOUTH, true);
    if (i == 530) m.button(m.a, SDL_GAMEPAD_BUTTON_SOUTH, false);
    // the touchpad: a trackpad (DualSense's auto), a tap and the click = reload; Touchpad style: a pointer
    if (i == 531) { g[1].x = c.lw * 0.5f; g[1].y = c.lh * 0.5f; markGuns(); m.touch(m.a, true, 0.5f, 0.5f); }   // (padfix: the off-screen reload left it off the window)
    if (i == 532) m.touch(m.a, true, 0.6f, 0.5f);
    if (i == 533) m.touch(m.a, true, 0.7f, 0.5f);
    if (i == 534) m.touch(m.a, false, 0.7f, 0.5f);
    if (i == 535) m.touch(m.a, true, 0.3f, 0.3f);
    if (i == 536) m.touch(m.a, false, 0.3f, 0.3f);
    if (i == 537) m.button(m.a, SDL_GAMEPAD_BUTTON_TOUCHPAD, true);
    if (i == 538) m.button(m.a, SDL_GAMEPAD_BUTTON_TOUCHPAD, false);
    if (i == 539) { p2.style = 4; c.pads.configure(c.cfg); }
    if (i == 540) m.touch(m.a, true, 0.25f, 0.75f);
    if (i == 541) m.touch(m.a, false, 0.25f, 0.75f);
    if (i == 542) { p2.s[3].touchClick = 1; c.pads.configure(c.cfg); m.touch(m.a, true, 0.5f, 0.5f); }
    if (i == 543) { m.touch(m.a, false, 0.5f, 0.5f); }
    if (i == 544) { p2.s[3].touchClick = 0; p2.style = 3; c.pads.configure(c.cfg); }
    // Motion + stick (relpad: was the Sideways grip): raising the front = up; R1 and R2 shoot; Shoot Button L2: L2
    // shoots
    if (i == 545) m.sensors(m.a, flat, nullptr);
    if (i == 546) markGuns();
    if (i >= 546 && i <= 555) { const float r[3] = {0.3f, 0, 0}; m.sensors(m.a, nullptr, r); }
    if (i == 557) m.button(m.a, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    if (i == 558) m.button(m.a, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
    if (i == 559) m.axis(m.a, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1.0f);
    if (i == 560) m.axis(m.a, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    if (i == 561) { p2.s[2].shoot = 2; c.pads.configure(c.cfg); m.axis(m.a, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1.0f); }
    if (i == 562) { m.axis(m.a, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0); p2.s[2].shoot = 0; c.pads.configure(c.cfg); }
    // hot-plug: the DualSense unplugged in play (the pause, the message), an Xbox pad plugged in (player 2's; the
    // Motion + stick needs a gyro: Classic), its Start continues
    if (i == 563) m.detach(m.a);
    if (i == 564) m.attach(m.b, 1);
    if (i == 565) m.button(m.b, SDL_GAMEPAD_BUTTON_START, true);
    if (i == 566) m.button(m.b, SDL_GAMEPAD_BUTTON_START, false);
    // two pads, two players: a Switch Pro for player 1 (Classic)
    if (i == 567) {
        c.cfg.p1Input = 2;
        c.cfg.pad[0] = PadProfile{};
        c.cfg.pad[0].style = 1;
        c.pads.configure(c.cfg);
        m.attach(m.c, 2);
    }
    if (i == 569) { markGuns(); m.axis(m.c, SDL_GAMEPAD_AXIS_LEFTX, 0.5f); }
    if (i == 579) m.axis(m.c, SDL_GAMEPAD_AXIS_LEFTX, 0);
    if (i == 580) {
        c.cfg.p1Input = 0;
        c.cfg.p2Input = 0;
        c.cfg.pad[0] = c.cfg.pad[1] = PadProfile{};
        c.pads.configure(c.cfg);
        m.detach(m.b);
        m.detach(m.c);
    }
    // light guns: player 2's absolute device 9001 (raw input fed directly), player 1's mouse 9002 by its motion
    if (i == 581) {
        c.cfg.p2Input = 3;
        const float id[4] = {0.1f, 0.1f, 0.9f, 0.9f};   // (no calibration yet)
        std::copy(id, id + 4, c.cfg.gunCal[1]);
        c.lightGuns.configure(c.cfg);
        c.lightGuns.reset();
        c.lightGuns.absolute(9001, 0.25f, 0.5f);
    }
    if (i == 582) c.lightGuns.buttons(9001, 0x1 | 0x2);
    if (i == 583) c.lightGuns.buttons(9001, 0x4 | 0x8);
    if (i == 584) { c.lightGuns.absolute(9001, 1.2f, 0.5f); c.lightGuns.buttons(9001, 0x1 | 0x2); }
    if (i == 585) { markGuns(); c.lightGuns.relative(9002, 30, 0); }
    if (i == 586) { markGuns(); c.desktopMotion(10, 10); }
    if (i == 587) c.lightGuns.startCalibration();
    if (i == 588) { c.lightGuns.absolute(9001, 0.15f, 0.2f); c.lightGuns.buttons(9001, 0x1 | 0x2); }
    if (i == 589) { c.lightGuns.absolute(9001, 0.85f, 0.8f); c.lightGuns.buttons(9001, 0x1 | 0x2); }
    if (i == 590) c.lightGuns.absolute(9001, 0.15f, 0.2f);
    if (i == 591) {
        c.cfg.p2Input = 0;
        const float id[4] = {0.1f, 0.1f, 0.9f, 0.9f};
        std::copy(id, id + 4, c.cfg.gunCal[1]);
        c.lightGuns.configure(c.cfg);
        c.lightGuns.reset();
    }
    // ipolish (592-596): Controls > Light Guns > Assign: two guns fed in (A 9101, B 9102); player 1 assigned gun B
    if (i == 592) {
        c.cfg.p1Input = c.cfg.p2Input = 3;
        c.cfg.gunDev[0] = "test-gun-b", c.cfg.gunName[0] = "Gun B";
        c.lightGuns.configure(c.cfg);
        c.lightGuns.reset();
        c.lightGuns.describe(9101, "test-gun-a", "Gun A");
        c.lightGuns.describe(9102, "test-gun-b", "Gun B");
        c.lightGuns.absolute(9101, 0.3f, 0.4f);   // gun A first: not player 1's
    }
    if (i == 593) c.lightGuns.absolute(9102, 0.7f, 0.6f);
    if (i == 594) {   // the Assign page: player 2 waits; gun B's pull takes player 2 (from player 1)
        c.lightGuns.startAssign(1);
        c.lightGuns.buttons(9102, 0x1 | 0x2);
    }
    if (i == 595) c.lightGuns.buttons(9101, 0x1 | 0x2);   // gun A (left without a seat): the free player 1
    if (i == 596) {
        c.cfg.p1Input = c.cfg.p2Input = 0;
        c.cfg.gunDev[0].clear(), c.cfg.gunName[0].clear();
        c.lightGuns.assigned = {};
        c.lightGuns.configure(c.cfg);
        c.lightGuns.reset();
    }
    // linuxmice (597-600, GitHub #67): Linux's evdev events (recorded input_event type / code / value) for an absolute
    // gun 9201 (ABS 0..1000 x 0..500: player 2) and a mouse 9202 (REL: player 1), parsed on every platform
    auto ev = [&](uint64_t d, unsigned type, unsigned code, int value) { c.lightGuns.evdevEvent(d, type, code, value); };
    if (i == 596) {   // (after 596's reset: P1 Mouse, P2 Mouse 2) two mice moved: counted only, then one crosshair each
        markGuns();
        ev(9301, 2, 0, 5), ev(9301, 0, 0, 0), ev(9302, 2, 1, 7), ev(9302, 0, 0, 0);
        m.evdevSeen = c.lightGuns.evdevMiceSeen();
        c.lightGuns.evdevMiceOn();
    }
    if (i == 597) {
        c.cfg.p2Input = 3;
        c.lightGuns.configure(c.cfg);
        c.lightGuns.reset();
        c.lightGuns.evdevRange(9201, 0, 1000, 0, 500);
        ev(9201, 3, 0, 250), ev(9201, 3, 1, 250), ev(9201, 0, 0, 0);   // ABS_X 250, ABS_Y 250, SYN_REPORT
    }
    if (i == 598) {
        markGuns();
        ev(9202, 2, 0, 12), ev(9202, 2, 0, 8), ev(9202, 2, 1, -6), ev(9202, 0, 0, 0);   // REL_X 12 + 8, REL_Y -6: one packet
        ev(9202, 2, 0, 50), ev(9202, 0, 3, 0), ev(9202, 2, 0, 70), ev(9202, 0, 0, 0);   // SYN_DROPPED: up to the report skipped
    }
    if (i == 599) {
        ev(9201, 1, 0x110, 1), ev(9201, 0, 0, 0), ev(9201, 1, 0x110, 0), ev(9201, 0, 0, 0);   // BTN_LEFT down / up
        ev(9201, 1, 0x110, 2), ev(9201, 0, 0, 0);   // (an auto-repeat: nothing)
    }
    if (i == 600) {   // pointed off the screen (0, 0 as several guns report it), the trigger: a reload
        ev(9201, 3, 0, 0), ev(9201, 3, 1, 0), ev(9201, 1, 0x110, 1), ev(9201, 0, 0, 0);
        ev(9201, 1, 0x110, 0), ev(9201, 0, 0, 0);
    }
    // 601-630: the 4-seat section (docs/coop4.md): the 4P mode on in this game, players 3 / 4 on two more pads (their
    // light bars green / yellow), their start buttons join, their triggers shoot, a left trigger reloads; then off
    if (i == 601) {
        c.cfg.p2Input = 0;   // (linuxmice: player 2's evdev gun off again)
        c.lightGuns.configure(c.cfg);
        c.lightGuns.reset();
        c.rt.coop4_enable();
        c.cfg.players = 4;
        c.cfg.p3Input = c.cfg.p4Input = 2;
        c.pads.configure(c.cfg);
        m.attach(m.d, 1);
        m.attach(m.e, 1);
    }
    if (i == 604) m.button(m.d, SDL_GAMEPAD_BUTTON_START, true);
    if (i == 605) m.button(m.d, SDL_GAMEPAD_BUTTON_START, false);
    if (i == 606) m.button(m.e, SDL_GAMEPAD_BUTTON_START, true);
    if (i == 607) m.button(m.e, SDL_GAMEPAD_BUTTON_START, false);
    if (i == 611) {
        topUp();
        for (int p = 0; p < 2; ++p) {
            m.shots4[p] = c.rt.shotsIn[p + 2];
            m.reloads4[p] = c.rt.reloadsIn[p + 2];
        }
    }
    if (i == 612) m.axis(m.d, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1.0f);
    if (i == 613) m.axis(m.d, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    if (i == 614) m.axis(m.e, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1.0f);
    if (i == 615) m.axis(m.e, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    if (i == 618) m.axis(m.d, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1.0f);
    if (i == 619) m.axis(m.d, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0);
    if (i == 625) {   // back to two players
        m.detach(m.d);
        m.detach(m.e);
        c.rt.seats4 = 0;
        c.cfg.players = 2;
        c.pads.configure(c.cfg);
    }
    // padfeel (2026-10-20): the aim's feel. A DualSense again for player 2, Motion only, the default Player space
    const float zero[3] = {0, 0, 0}, drift[3] = {0.02f, -0.01f, 0.015f}, drift2[3] = {0.03f, -0.01f, 0.015f};
    if (i == 652) {
        m.attach(m.a, 0);
        c.cfg.p2Input = 2;
        p2 = PadProfile{};
        p2.style = 2;
        c.pads.configure(c.cfg);
    }
    if (i >= 653 && i <= 655) { m.sensors(m.a, flat, nullptr); m.gyroN(m.a, zero, 4); }   // (readings 1/240 s apart)
    // several gyro readings a frame: all integrated (4 at 240 Hz = the same turn as 1 at 60 Hz)
    if (i == 656) markGuns();
    if (i >= 656 && i <= 665) { const float r[3] = {0, -0.5f, 0}; m.gyroN(m.a, r, 4); }
    // Player space: the pad tilted 45 degrees (its face to the player), turned about the vertical = full yaw
    if (i >= 667 && i <= 669) { const float a[3] = {0, 6.934f, 6.934f}; m.sensors(m.a, a, zero); }
    if (i == 670) markGuns();
    if (i >= 670 && i <= 679) { const float a[3] = {0, 6.934f, 6.934f}, r[3] = {0, -0.35355f, -0.35355f}; m.sensors(m.a, a, r); }
    // the tiered smoothing: a tiny turn (1.15 degrees / s) is delayed, not lost
    if (i == 681) markGuns();
    if (i >= 681 && i <= 690) { const float r[3] = {0, -0.02f, 0}; m.sensors(m.a, flat, r); }
    if (i >= 691 && i <= 710) m.sensors(m.a, flat, zero);
    // Calibrate Gyro: 2 s still with a drift = the bias; no aim meanwhile; then the drift moves nothing
    if (i == 712) { c.pads.calibrate(1); markGuns(); }
    if (i >= 712 && i <= 861) m.sensors(m.a, flat, drift);
    if (i == 852) markGuns();
    // the drift changes: learnt while the pad lies still (after 1 s, time constant 1 s)
    if (i >= 863 && i <= 1062) m.sensors(m.a, flat, drift2);
    // Classic with the tuned stick: the curve's value, frame-rate independence (after()), the edge turbo
    if (i == 1064) { p2.style = 1; c.pads.configure(c.cfg); m.button(m.a, SDL_GAMEPAD_BUTTON_RIGHT_STICK, true); }
    if (i == 1065) m.button(m.a, SDL_GAMEPAD_BUTTON_RIGHT_STICK, false);
    if (i == 1066) { markGuns(); m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 0.75f); }
    if (i == 1076) m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 0);
    if (i == 1077) m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 0.75f);
    if (i == 1078) m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 0);
    if (i == 1080) { p2.s[0].stickSens = 46; c.pads.configure(c.cfg); g[1].x = 5; m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 1.0f); }   // (sens36: the old speed 1)
    if (i == 1113) markGuns();
    if (i == 1116) { m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 0); p2.s[0].stickSens = 100; c.pads.configure(c.cfg); }
    // shake to reload (Motion + stick, Medium; relpad: game/shake.h): a jolt and one back (two frames later) reloads
    // and does not move the crosshair; an aim turn (3 rad/s, 0.3 g) is 1:1 and no reload; a wrist flick (a fast turn
    // with a jolt) reloads, its turn dropped, the flick back is no second reload (the cooldown)
    if (i == 1120) { p2.style = 3; c.pads.configure(c.cfg); m.button(m.a, SDL_GAMEPAD_BUTTON_RIGHT_STICK, true); }
    if (i == 1121) m.button(m.a, SDL_GAMEPAD_BUTTON_RIGHT_STICK, false);
    if (i >= 1120 && i <= 1122) m.sensors(m.a, flat, drift2);   // (the pad at rest reads its drift)
    if (i == 1123) { markGuns(); const float a[3] = {30, 9.81f, 0}, r[3] = {drift2[0], -0.5f + drift2[1], drift2[2]}; m.sensors(m.a, a, r); }
    if (i == 1124) m.sensors(m.a, flat, drift2);
    if (i == 1125) { const float a[3] = {-30, 9.81f, 0}, r[3] = {drift2[0], -0.5f + drift2[1], drift2[2]}; m.sensors(m.a, a, r); }
    if (i >= 1126 && i <= 1149) m.sensors(m.a, flat, drift2);
    if (i == 1150) { markGuns(); const float a[3] = {3, 9.81f, 0}, r[3] = {drift2[0], -3 + drift2[1], drift2[2]}; m.sensors(m.a, a, r); }
    if (i == 1151) { const float a[3] = {-3, 9.81f, 0}, r[3] = {drift2[0], 3 + drift2[1], drift2[2]}; m.sensors(m.a, a, r); }
    if (i == 1152 || i == 1153) m.sensors(m.a, flat, drift2);
    if (i == 1154) { markGuns(); const float a[3] = {30, 9.81f, 0}, r[3] = {drift2[0], -11 + drift2[1], drift2[2]}; m.sensors(m.a, a, r); }
    if (i == 1155) { const float a[3] = {-30, 9.81f, 0}, r[3] = {drift2[0], 11 + drift2[1], drift2[2]}; m.sensors(m.a, a, r); }
    if (i >= 1156 && i <= 1158) m.sensors(m.a, flat, drift2);
    // the touchpad pointer: the jitter of a resting finger moves nothing; both edges reachable
    if (i == 1160) { p2.style = 4; c.pads.configure(c.cfg); }
    if (i == 1161) m.touch(m.a, true, 0.5f, 0.5f);
    if (i == 1162) m.touch(m.a, true, 0.5008f, 0.4993f);
    if (i == 1163) m.touch(m.a, true, 0.0f, 1.0f);
    if (i == 1164) m.touch(m.a, false, 0.0f, 1.0f);
    if (i == 1165) {
        m.detach(m.a);
        c.cfg.p2Input = 0;
        p2 = PadProfile{};
        c.pads.configure(c.cfg);
    }
}

void PadTest::after(int i, Ctx& c)
{
    Impl& m = *m_;
    if (i < kFirst || i > kLast)
        return;
    GunState* g = c.guns;
    const float lw = float(c.lw), lh = float(c.lh);
    const float stick = 2.4f * lh / 60.0f;                      // pixels a frame at full tilt (sensitivity 5: 2.4 heights / s, padfix)
    const float half = 0.25f;                                   // the stick half way: ((0.5 - 8 %) / (92 % - 8 %))^2
    const float perRad = lw / (36.0f * kPi / 180.0f);           // the gyro (sensitivity 5 = 1.0x: 36 degrees a width)
    char b[256];
    auto moved = [&](int p, float wx, float wy, float tol, const char* what) {
        const float dx = g[p].x - m.mark[p][0], dy = g[p].y - m.mark[p][1];
        std::snprintf(b, sizeof b, "%s: P%d moved (%.2f, %.2f), wanted (%.2f, %.2f)", what, p + 1, dx, dy, wx, wy);
        c.expect(std::abs(dx - wx) <= tol && std::abs(dy - wy) <= tol, b);
    };
    auto at = [&](int p, float x, float y, float tol, const char* what) {
        std::snprintf(b, sizeof b, "%s: P%d at (%.1f, %.1f), wanted (%.1f, %.1f)", what, p + 1, g[p].x, g[p].y, x, y);
        c.expect(std::abs(g[p].x - x) <= tol && std::abs(g[p].y - y) <= tol, b);
    };
    const bool paused = c.pause.paused();
    if (i == 332) {
        std::snprintf(b, sizeof b, "a DualSense (virtual) for player 2: pad %u (want %u), style %d (Classic 1), type %s",
                      unsigned(c.pads.padOf(1)), unsigned(m.a.id), c.pads.styleOf(1),
                      SDL_GetGamepadStringForType(SDL_GetGamepadTypeForID(m.a.id)));
        c.expect(m.a.id && c.pads.padOf(1) == m.a.id && c.pads.styleOf(1) == 1 && c.pads.padOf(0) == 0, b);
        std::snprintf(b, sizeof b, "player 2's light bar blue: (%d, %d, %d)", m.a.led[0], m.a.led[1], m.a.led[2]);
        c.expect(m.a.led[0] == 0 && m.a.led[1] == 64 && m.a.led[2] == 255, b);
        std::snprintf(b, sizeof b, "the adaptive trigger: %d effect(s), right trigger mode 0x%02X (0x25 weapon), enable 0x%02X, strength %d (medium 5)",
                      m.a.effects, m.a.fx[10], m.a.fx[0], m.a.fx[13] + 1);
        c.expect(m.a.effects >= 1 && m.a.fx[0] == 0x04 && m.a.fx[10] == 0x25 && m.a.fx[13] == 4 && m.a.fx[11] == 0x24, b);
    }
    if (i == 333) {
        const int st = int32_t(c.rt.mem.u32(G::players + 0xCC));
        c.expect(paused && st == 5, "pad Start, player 2 in play (state " + std::to_string(st) + "): the pause");
        c.counts(0, 0, 0, 0, 0, 0, "pad Start in play: no start request");
    }
    if (i == 335) c.expect(!paused, "pad Start while paused: going on");
    if (i == 337) c.expect(paused, "pad Back (Share / View / Minus) in play: the pause");
    if (i == 338) { c.expect(!paused, "East (B / Circle) while paused: going on"); c.counts(0, 0, 0, 0, 0, 0, "East closing the pause: no reload"); }
    if (i == 349) {
        moved(1, 10 * half * stick, 0, 0.6f, "Classic: left stick half right for 10 frames (8 % dead zone, curve 2)");
        moved(0, 0, 0, 0.01f, "Classic: player 1 (the mouse) stays");
    }
    if (i == 355) moved(1, 0, 0, 0.01f, "Classic: the stick at 4 % (inside the 8 % dead zone)");
    if (i == 356) c.counts(0, 1, 0, 0, 0, 0, "R2 at 16 % (the fire point 15 %) = a shot");
    if (i == 357) c.counts(0, 0, 0, 0, 0, 0, "R2 back to 10 % (held: above 60 % of the fire point)");
    if (i == 358) c.counts(0, 0, 0, 0, 0, 0, "R2 at 20 %, still held: no second shot");
    if (i == 359) c.counts(0, 0, 0, 0, 0, 0, "R2 let go");
    if (i == 360) c.counts(0, 1, 0, 0, 0, 0, "R2 again = a shot");
    if (i == 362) c.counts(0, 0, 0, 1, 0, 0, "East (B / Circle) = reload");
    if (i == 363) {
        std::snprintf(b, sizeof b, "the reload's vibration: %d rumble(s) (low %u)", m.a.rumbles - m.rumbles0, unsigned(m.a.lo));
        c.expect(m.a.rumbles > m.rumbles0, b);
    }
    if (i == 364) c.counts(0, 0, 0, 1, 0, 0, "West (X / Square) = reload");
    if (i == 366) c.counts(0, 0, 0, 1, 0, 0, "L2 = reload");
    if (i == 369) {
        std::snprintf(b, sizeof b, "getting hit: vibration %d (low %u, wanted 0.7 = %u)", m.a.rumbles - m.rumbles0, unsigned(m.a.lo), unsigned(0.7f * 65535));
        c.expect(m.a.rumbles == m.rumbles0 + 1 && m.a.lo == Uint16(0.7f * 65535), b);
    }
    if (i == 371) {
        std::snprintf(b, sizeof b, "hitting an enemy (the score up): vibration %d (high %u, wanted 0.2 = %u)", m.a.rumbles - m.rumbles0,
                      unsigned(m.a.hi), unsigned(0.2f * 65535));
        c.expect(m.a.rumbles == m.rumbles0 + 1 && m.a.hi == Uint16(0.2f * 65535), b);
    }
    if (i == 372) { c.expect(paused && c.pause.selection() == 0, "pad Start in play = the pause"); c.counts(0, 0, 0, 0, 0, 0, "the pause: no start"); }
    if (i == 373) c.expect(paused && c.pause.selection() == 1, "the pause: d-pad down = Exit");
    if (i == 374) c.expect(paused && c.pause.selection() == 0, "the pause: d-pad up = Continue");
    if (i == 375) c.expect(!paused, "the pause: Start again = going on");
    if (i == 387) moved(1, 10 * 0.4f * stick, 0, 0.6f, "Classic: the right stick = fine aim (40 %)");
    if (i == 398) moved(1, 10 * 0.35f * stick, 0, 0.6f, "Classic: left stick click held = precise (35 %)");
    if (i == 404) moved(1, 0, -5 * half * stick, 0.6f, "Classic, invert Y: the stick down moves up");
    if (i == 406) c.expect(c.pads.styleOf(1) == 2, "Motion only: style " + std::to_string(c.pads.styleOf(1)));
    if (i == 417) moved(1, 10 * 0.5f / 60.0f * perRad, 0, 1.0f, "Motion only: the pad turned right 0.5 rad/s for 10 readings (the stick ignored)");
    if (i == 428) moved(1, 0, -10 * 0.3f / 60.0f * perRad, 1.0f, "Motion only: the pad's front raised = up");
    if (i == 469) {
        const float w = 10 * 0.5f / 60.0f * perRad;
        moved(1, w, 0, w * 0.04f, "world space: the pad rolled onto its side, turned right = right (gravity from the accelerometer)");
    }
    if (i == 480) moved(1, 0, -10 * 0.5f / 60.0f * perRad, 1.0f, "local space: the same rolled turn = the pad's own pitch (up)");
    if (i == 494) moved(1, 0, 0, 0.01f, "Motion + stick: North held = the ratchet (the pad turns, the crosshair stays)");
    if (i == 504) moved(1, 0, 0, 0.01f, "Motion + stick: North let go after 0.3 s: no recentre");
    if (i == 505) at(1, lw / 2, lh / 2, 0.01f, "the right stick's click = the middle");
    if (i == 516) moved(1, 10 * 0.4f * stick, 0, 0.6f, "Motion + stick: the right stick fine-adjusts");
    if (i == 518) at(1, lw / 2, lh / 2, 0.01f, "North tapped = the middle");
    if (i == 529) {
        c.expect(!c.rt.gun[1].onScreen, "the gyro turned off the window's right: off the screen");
        c.counts(0, 0, 0, 1, 0, 0, "South off the screen (gyro) = reload");
    }
    if (i == 530) {   // padfix (user 2026-10-30: "the reticle gets reset to the centre"): it stays off the window
        std::snprintf(b, sizeof b, "the gyro's off-screen reload: the crosshair stays off the window (%.1f, %.1f)", g[1].x, g[1].y);
        c.expect(g[1].x > lw && g[1].x <= lw + lh * 0.05f + 0.01f, b);
    }
    if (i == 534) moved(1, (0.2f - 0.0015f) * lw * 1.2f, 0, 1.0f, "the touchpad as a trackpad (DualSense auto): a swipe right");
    if (i == 536) c.counts(0, 0, 0, 1, 0, 0, "a touchpad tap = reload");
    if (i == 537) c.counts(0, 0, 0, 1, 0, 0, "the touchpad's click = reload");
    if (i == 540) at(1, (0.25f * 1.1f - 0.05f) * lw, (0.75f * 1.1f - 0.05f) * lh, 0.5f, "Touchpad style: the touch's point = the crosshair's");
    if (i == 543) c.counts(0, 1, 0, 0, 0, 0, "Touchpad style, click = shoot: a tap shoots");
    if (i == 556) moved(1, 0, -10 * 0.3f / 60.0f * perRad, 1.0f, "Motion + stick: the front raised = up");
    if (i == 557) c.counts(0, 1, 0, 0, 0, 0, "Motion + stick: R1 = a shot");
    if (i == 559) c.counts(0, 1, 0, 0, 0, 0, "Motion + stick: R2 = a shot");
    if (i == 561) c.counts(0, 1, 0, 0, 0, 0, "Shoot Button L2: L2 = a shot");
    if (i == 563) {
        const auto n = c.pads.notice();
        c.expect(c.pads.lost(1) && paused && !n.empty(), "player 2's pad unplugged in play: the pause and the message (" +
                                                             (n.empty() ? std::string("none") : n[0]) + ")");
    }
    if (i == 564) {
        std::snprintf(b, sizeof b, "an Xbox pad plugged in: player 2's (%u, want %u), style %d (Classic: no gyro), still paused %d",
                      unsigned(c.pads.padOf(1)), unsigned(m.b.id), c.pads.styleOf(1), int(paused));
        c.expect(!c.pads.lost(1) && c.pads.padOf(1) == m.b.id && c.pads.styleOf(1) == 1 && c.pads.notice().empty() && paused, b);
    }
    if (i == 565) c.expect(!paused, "the new pad's Start: going on");
    if (i == 568) {
        std::snprintf(b, sizeof b, "two pads: P1 %u (Switch Pro %u, light %d %d %d), P2 %u (Xbox %u)", unsigned(c.pads.padOf(0)), unsigned(m.c.id),
                      m.c.led[0], m.c.led[1], m.c.led[2], unsigned(c.pads.padOf(1)), unsigned(m.b.id));
        c.expect(c.pads.padOf(0) == m.c.id && c.pads.padOf(1) == m.b.id && m.c.led[0] == 255 && m.c.led[1] == 0, b);
    }
    if (i == 579) {
        moved(0, 10 * half * stick, 0, 0.6f, "two pads: P1's stick moves P1");
        moved(1, 0, 0, 0.01f, "two pads: P2 stays");
    }
    if (i == 581) at(1, 0.25f * lw, 0.5f * lh, 0.01f, "light gun (absolute device): player 2 at its point");
    if (i == 582) c.counts(0, 1, 0, 0, 0, 0, "light gun: trigger (left) = a shot");
    if (i == 583) c.counts(0, 0, 0, 1, 0, 0, "light gun: right button = reload");
    if (i == 584) c.counts(0, 0, 0, 1, 0, 0, "light gun pointed off the window: the trigger = reload");
    if (i == 585) moved(0, 30, 0, 0.01f, "light guns on: player 1's mouse by its raw motion");
    if (i == 586) moved(0, 0, 0, 0.01f, "light guns on: the desktop cursor's motion ignored");
    float tu = 0, tv = 0;
    if (i == 587) c.expect(c.lightGuns.calibrating() && c.lightGuns.calibrationTarget(tu, tv) && tu == 0.1f, "F9: the calibration, target top left");
    if (i == 588) {
        c.expect(c.lightGuns.calibrating() && c.lightGuns.calibrationTarget(tu, tv) && tu == 0.9f, "calibration: the second target (bottom right)");
        c.counts(0, 0, 0, 0, 0, 0, "calibration shots are not shots");
    }
    if (i == 589) {
        const float* k = c.cfg.gunCal[1];
        std::snprintf(b, sizeof b, "calibrated: (%.2f, %.2f) (%.2f, %.2f), wanted (0.15, 0.20) (0.85, 0.80)", k[0], k[1], k[2], k[3]);
        c.expect(!c.lightGuns.calibrating() && std::abs(k[0] - 0.15f) < 1e-4f && std::abs(k[1] - 0.2f) < 1e-4f &&
                     std::abs(k[2] - 0.85f) < 1e-4f && std::abs(k[3] - 0.8f) < 1e-4f, b);
        c.counts(0, 0, 0, 0, 0, 0, "the last calibration shot is no shot");
    }
    if (i == 590) at(1, 0.1f * lw, 0.1f * lh, 0.05f, "calibrated: the gun's reading at the first target = the target");
    if (i == 592) {
        std::snprintf(b, sizeof b, "assigned guns: gun A used first goes to player 2 (%llu), player 1 kept for gun B (%llu)",
                      (unsigned long long)c.lightGuns.deviceOf(1), (unsigned long long)c.lightGuns.deviceOf(0));
        c.expect(c.lightGuns.deviceOf(1) == 9101 && c.lightGuns.deviceOf(0) == 0, b);
    }
    if (i == 593) {
        c.expect(c.lightGuns.deviceOf(0) == 9102, "assigned guns: gun B takes player 1");
        at(0, 0.7f * lw, 0.6f * lh, 0.01f, "assigned guns: player 1 aims with gun B");
    }
    if (i == 594) {
        const auto& a = c.lightGuns.assigned;
        std::snprintf(b, sizeof b, "Assign: player 2 waited, gun B's pull took it (seat %d, %s; P1 pin '%s', P2 device %llu)", a.seat,
                      a.key.c_str(), c.lightGuns.pin[0].c_str(), (unsigned long long)c.lightGuns.deviceOf(1));
        c.expect(a.seat == 1 && a.key == "test-gun-b" && a.name == "Gun B" && c.lightGuns.pin[1] == "test-gun-b" &&
                     c.lightGuns.pin[0].empty() && c.lightGuns.deviceOf(1) == 9102 && c.lightGuns.assigning() < 0, b);
        c.counts(0, 0, 0, 0, 0, 0, "Assign: the pull is no shot");
    }
    if (i == 595) {
        c.expect(c.lightGuns.deviceOf(0) == 9101, "Assign: gun A moves on to the free player 1");
        c.counts(1, 0, 0, 0, 0, 0, "Assign: gun A's trigger shoots for player 1");
    }
    if (i == 596) {
        std::snprintf(b, sizeof b, "evdev: P1 Mouse / P2 Mouse 2: two mice counted (%d), then the first used = P1 (%llu), "
                      "the second = P2 (%llu)", m.evdevSeen, (unsigned long long)c.lightGuns.deviceOf(0),
                      (unsigned long long)c.lightGuns.deviceOf(1));
        c.expect(m.evdevSeen == 2 && c.lightGuns.active() && c.lightGuns.deviceOf(0) == 9301 && c.lightGuns.deviceOf(1) == 9302, b);
        moved(0, 0, 0, 0.01f, "evdev: the mice's motion before the switch: no crosshair moved by it");
    }
    if (i == 597) at(1, 0.25f * lw, 0.5f * lh, 0.01f, "evdev: the absolute gun's ABS_X / ABS_Y in its range: player 2 there");
    if (i == 598) moved(0, 20, -6, 0.01f, "evdev: the mouse's REL_X / REL_Y summed per packet, a dropped packet skipped");
    if (i == 599) c.counts(0, 1, 0, 0, 0, 0, "evdev: the gun's BTN_LEFT = one shot (its auto-repeat none)");
    if (i == 600) c.counts(0, 0, 0, 1, 0, 0, "evdev: the gun at (0, 0) (off the screen): the trigger = reload");
    // the 4-seat section (before: 601-625)
    auto seat = [&](int p) { return int32_t(c.rt.mem.u32(c.rt.player_rec(p))); };
    if (i == 603) {
        std::snprintf(b, sizeof b, "4P: players 3 / 4's pads (%u, %u; want %u, %u)", unsigned(c.pads.padOf(2)), unsigned(c.pads.padOf(3)),
                      unsigned(m.d.id), unsigned(m.e.id));
        c.expect(c.pads.padOf(2) == m.d.id && c.pads.padOf(3) == m.e.id && m.d.id != 0 && m.e.id != 0, b);
        std::snprintf(b, sizeof b, "4P: the light bars green / yellow (%d %d %d / %d %d %d)", m.d.led[0], m.d.led[1], m.d.led[2],
                      m.e.led[0], m.e.led[1], m.e.led[2]);
        c.expect(m.d.led[0] == 0 && m.d.led[1] == 255 && m.d.led[2] == 0 && m.e.led[0] == 255 && m.e.led[1] == 200 && m.e.led[2] == 0, b);
    }
    if (i == 610) {
        std::snprintf(b, sizeof b, "4P: players 3 / 4 joined with their pads' Start (states %d %d)", seat(2), seat(3));
        c.expect(seat(2) == 5 && seat(3) == 5, b);
    }
    if (i == 617) {
        std::snprintf(b, sizeof b, "4P: players 3 / 4's right triggers fired (shots %u %u, from %u %u)", c.rt.shotsIn[2], c.rt.shotsIn[3],
                      m.shots4[0], m.shots4[1]);
        c.expect(c.rt.shotsIn[2] == m.shots4[0] + 1 && c.rt.shotsIn[3] == m.shots4[1] + 1, b);
    }
    if (i == 621) {
        std::snprintf(b, sizeof b, "4P: player 3's left trigger reloads (reloads %u, from %u)", c.rt.reloadsIn[2], m.reloads4[0]);
        c.expect(c.rt.reloadsIn[2] == m.reloads4[0] + 1, b);
    }
    // hangfix (user: "make sure the 4P health settings do not bleed over to other modes if you are playing 1-2 players
    // only"): back to two seats, the 4P Boss Hit Points / Enemy Toughness at their maximum change no hit points or damage
    if (i == 626) {
        const uint32_t dmg = G::hit_damage;
        const int16_t keep = c.rt.mem.at<int16_t>(dmg);
        const float bs = c.rt.coop4BossStep, es = c.rt.coop4EnemyStep;
        c.rt.coop4BossStep = c.rt.coop4EnemyStep = 2.0f;
        c.rt.mem.at<int16_t>(dmg) = 600;
        c.rt.coop4_boss_scale(dmg, dmg);
        const int16_t boss = c.rt.mem.at<int16_t>(dmg);
        c.rt.mem.at<int16_t>(dmg) = 40;
        c.rt.coop4_enemy_damage();
        const int16_t hit = c.rt.mem.at<int16_t>(dmg);
        std::snprintf(b, sizeof b, "2 seats: 4P steps at 200 %% leave a boss's 600 hit points (%d) and a hit's 40 damage (%d)", boss, hit);
        c.expect(boss == 600 && hit == 40, b);
        c.rt.mem.at<int16_t>(dmg) = keep;
        c.rt.coop4BossStep = bs, c.rt.coop4EnemyStep = es;
    }
    // padfeel: the aim's feel
    if (i == 666) moved(1, 10 * 0.5f / 60.0f * perRad, 0, 1.0f, "gyro: 4 readings a frame (240 Hz), every one integrated");
    if (i == 680) moved(1, 10 * 0.5f / 60.0f * perRad, 0, 1.5f, "Player space: the pad tilted 45 degrees, turned about the vertical = full yaw");
    const float tiny = 10 * 0.02f / 60.0f * perRad;
    if (i == 690) {
        std::snprintf(b, sizeof b, "tiered smoothing: a tiny turn (1.15 deg/s) delayed: %.2f of %.2f px so far", g[1].x - m.mark[1][0], tiny);
        c.expect(g[1].x - m.mark[1][0] > 0.2f * tiny && g[1].x - m.mark[1][0] < 0.9f * tiny, b);
    }
    if (i == 711) moved(1, tiny, 0, 0.3f, "tiered smoothing: the tiny turn not lost");
    if (i == 760) {
        moved(1, 0, 0, 0.01f, "Calibrate Gyro: no aim while held still");
        c.expect(c.pads.calibrating(1), "Calibrate Gyro: still calibrating after 0.8 s");
    }
    float bias[3];
    c.pads.gyroBias(1, bias);
    if (i == 852) {
        std::snprintf(b, sizeof b, "Calibrate Gyro: done %d, bias (%.4f, %.4f, %.4f), wanted (0.02, -0.01, 0.015)", int(!c.pads.calibrating(1)),
                      bias[0], bias[1], bias[2]);
        c.expect(!c.pads.calibrating(1) && std::abs(bias[0] - 0.02f) < 5e-4f && std::abs(bias[1] + 0.01f) < 5e-4f && std::abs(bias[2] - 0.015f) < 5e-4f, b);
    }
    if (i == 862) moved(1, 0, 0, 0.5f, "Calibrate Gyro: the drift moves nothing after");
    if (i == 1063) {
        std::snprintf(b, sizeof b, "the drift learnt while still: bias x %.4f, wanted near 0.03 (was 0.02)", bias[0]);
        c.expect(std::abs(bias[0] - 0.03f) < 0.004f, b);
    }
    if (i == 1075) {
        const float k = (0.75f - 0.08f) / 0.84f;
        moved(1, 10 * k * k * stick, 0, 0.6f, "Classic: the stick at 75 % = 64 % speed (the curve)");
    }
    if (i == 1077) {   // the same motion by real time whatever the frame rate: 1 frame of 1/60 s = 4 of 1/240 s
        const float x0 = g[1].x;
        c.pads.aim(g, c.lw, c.lh, 1.0f / 60.0f);
        const float d1 = g[1].x - x0, x1 = g[1].x;
        for (int k = 0; k < 4; ++k) c.pads.aim(g, c.lw, c.lh, 1.0f / 240.0f);
        std::snprintf(b, sizeof b, "frame-rate independent stick: %.3f px at 60 Hz, %.3f px at 240 Hz", d1, g[1].x - x1);
        c.expect(d1 > 1.0f && std::abs(d1 - (g[1].x - x1)) < 0.01f, b);
    }
    if (i == 1116) {   // Stick Sensitivity 46 % (1.1 heights / s, the old speed 1), held at full tilt 0.6 s: the turbo's 1.5x
        const float base = 0.46f * 2.4f * lh / 60.0f;
        moved(1, 3 * 1.5f * base, 0, 0.5f, "Classic: the stick held at full tilt = the edge turbo (1.5x)");
    }
    if (i == 1123) c.counts(0, 0, 0, 0, 0, 0, "shake: the first jolt alone is no reload");
    if (i == 1125) c.counts(0, 0, 0, 1, 0, 0, "shake: a jolt and one back = reload");
    // (i == 1136: the shake's motion is no longer held back: the crosshair follows the aim, user 2026-10-31)
    if (i == 1150) {
        c.counts(0, 0, 0, 0, 0, 0, "an aim turn (3 rad/s, 0.3 g): no reload");
        moved(1, 3.0f / 60.0f * perRad, 0, 1.0f, "an aim turn: 1:1, not held back");
    }
    if (i == 1151) c.counts(0, 0, 0, 0, 0, 0, "an aim turn back: no reload");
    if (i == 1154) c.counts(0, 0, 0, 1, 0, 0, "a wrist flick (11 rad/s, a jolt) = reload");
    if (i == 1155) c.counts(0, 0, 0, 0, 0, 0, "the flick back: no second reload (the cooldown)");
    if (i == 1158) moved(1, 0, 0, 0.5f, "a wrist flick: the crosshair did not move");
    if (i == 1161) at(1, 0.5f * lw, 0.5f * lh, 0.5f, "touchpad pointer: the middle");
    if (i == 1162) at(1, 0.5f * lw, 0.5f * lh, 0.01f, "touchpad pointer: a resting finger's jitter moves nothing");
    if (i == 1163) {
        std::snprintf(b, sizeof b, "touchpad pointer: the corner reaches past the window's edges (%.1f, %.1f)", g[1].x, g[1].y);
        c.expect(g[1].x <= 0.0f && g[1].y >= lh, b);
    }
    if (i == 1166) {
        // sens36: per style, a preset sets that style's own inputs' speeds (Classic: the stick 83 %, the fine stick 63 %;
        // Motion + stick: the gyro 85 %, the fine stick 75 %; Touchpad: the trackpad 80 %), as the old Sensitivity 4 /
        // Fine Aim Speed 3 did; the others' untouched
        bool ok = true;
        std::string got;
        for (int st = 1; st <= kPadStyles; ++st) {
            PadStyle s;
            const int bal = padPresetOf(s, st);
            applyPadPreset(s, 1, st);
            got += " " + std::to_string(bal) + std::to_string(padPresetOf(s, st)) + ":" + std::to_string(s.stickSens) + "/" +
                   std::to_string(s.fineSens) + "/" + std::to_string(s.gyroSens) + "/" + std::to_string(s.touchSens);
            static const int want[4][4] = {{83, 63, 100, 100}, {100, 100, 85, 100}, {100, 75, 85, 100}, {100, 100, 100, 80}};
            ok = ok && bal == 2 && padPresetOf(s, st) == 1 && s.stickSens == want[st - 1][0] && s.fineSens == want[st - 1][1] &&
                 s.gyroSens == want[st - 1][2] && s.touchSens == want[st - 1][3];
        }
        std::snprintf(b, sizeof b, "feel presets per style (defaults = Balanced, Precise applied = Precise, stick/fine/gyro/trackpad %%):%s", got.c_str());
        c.expect(ok, b);
    }
}

// padfix (2026-10-30): the user's DualSense notes (pads_test.h). A DualSense for player 2 again (player 1: the mouse)
void PadTest::fixBefore(int k, Ctx& c)
{
    Impl& m = *m_;
    if (k < 0 || k >= kFixFrames)
        return;
    GunState* g = c.guns;
    PadProfile& p2 = c.cfg.pad[1];
    auto markGuns = [&]() {
        for (int p = 0; p < 2; ++p) { m.mark[p][0] = g[p].x; m.mark[p][1] = g[p].y; }
    };
    const float flat[3] = {0, 9.81f, 0};
    if (k == 0) {
        for (uint32_t p = 0; p < 2; ++p)   // (both still in play, enough lives)
            if (c.rt.mem.u32(G::players + p * 0xCC) == 5 && int32_t(c.rt.mem.u32(G::players + p * 0xCC + 0xC)) < 5)
                c.rt.mem.set32(G::players + p * 0xCC + 0xC, 9);
        m.attach(m.a, 0);
        c.cfg.p2Input = 2;
        p2 = PadProfile{};
        p2.style = 1;
        c.pads.configure(c.cfg);
    }
    // 1: the stick past the window's right edge (the margin), the trigger there = the off-screen reload, no recentre
    if (k == 2) { g[1].x = float(c.lw) - 5.0f; g[1].y = float(c.lh) * 0.5f; m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 1.0f); }
    if (k == 7) m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 0);
    if (k == 8) m.axis(m.a, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1.0f);
    if (k == 9) m.axis(m.a, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    // 2: a click of the left stick = the middle
    if (k == 10) m.button(m.a, SDL_GAMEPAD_BUTTON_LEFT_STICK, true);
    if (k == 11) m.button(m.a, SDL_GAMEPAD_BUTTON_LEFT_STICK, false);
    // 3: Classic's Stick Sensitivity 200 %: the same stick twice as far (padfix: was the player's Aim Sensitivity; the
    // other inputs' independence: the trials from kInd0, sens36)
    if (k == 12) { p2.s[0].stickSens = 200; c.pads.configure(c.cfg); markGuns(); m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 0.5f); }
    if (k == 22) { m.axis(m.a, SDL_GAMEPAD_AXIS_LEFTX, 0); p2.s[0].stickSens = 100; c.pads.configure(c.cfg); }
    // 4 (relpad: the Sideways grip is gone): Motion + stick held normally, flat: raising the front = up, turning right
    // = right; the face buttons as printed (West, East = reload); old launcher.ini files with Sideways (fixAfter)
    if (k == 23) {
        p2.style = 3;
        c.pads.configure(c.cfg);
        g[1].x = c.lw * 0.5f;
        g[1].y = c.lh * 0.5f;
    }
    if (k == 24 || k == 25) m.sensors(m.a, flat, nullptr);
    if (k == 26) markGuns();
    if (k >= 26 && k <= 35) { const float r[3] = {0.3f, 0, 0}; m.sensors(m.a, nullptr, r); }   // the front raised
    if (k == 37) markGuns();
    if (k >= 37 && k <= 46) { const float r[3] = {0, -0.5f, 0}; m.sensors(m.a, nullptr, r); }  // turned right (about up)
    if (k == 60) m.button(m.a, SDL_GAMEPAD_BUTTON_WEST, true);
    if (k == 61) { m.button(m.a, SDL_GAMEPAD_BUTTON_WEST, false); m.button(m.a, SDL_GAMEPAD_BUTTON_EAST, true); }
    if (k == 62) m.button(m.a, SDL_GAMEPAD_BUTTON_EAST, false);
    if (k == 86) {
        m.detach(m.a);
        c.cfg.p2Input = 0;
        p2 = PadProfile{};
        c.pads.configure(c.cfg);
    }
    // 7 (sens36, GitHub #36): each input's speed is its own. A DualSense for player 2 again; per trial (kIndLen frames)
    // the style and the four speeds set, the crosshair at 30 % of the width, one input moved (frames 2 .. 11), the
    // crosshair's x motion kept (fixAfter)
    const float zero[3] = {0, 0, 0};
    if (k == kInd0 - 2) {
        m.attach(m.a, 0);
        c.cfg.p2Input = 2;
        p2 = PadProfile{};
        p2.style = 1;
        c.pads.configure(c.cfg);
    }
    if (k >= kInd0 && k < kInd0 + kIndTrials * kIndLen) {
        const int t = (k - kInd0) / kIndLen, j = (k - kInd0) % kIndLen;
        const IndInput in = indInput(t);
        if (j == 0) {
            p2 = t >= kIndInputs * 3 ? m.migrated : PadProfile{};
            p2.style = in.style;
            if (t < kIndInputs * 3) {
                PadStyle& s = p2.s[in.style - 1];
                int* v[4] = {&s.stickSens, &s.fineSens, &s.gyroSens, &s.touchSens};
                const int var = t % 3;
                for (int q = 0; q < 4; ++q)
                    *v[q] = var == 1 && q != in.input ? 250 : var == 2 && q == in.input ? 200 : 100;
            }
            c.pads.configure(c.cfg);
            g[1].x = c.lw * 0.3f;
            g[1].y = c.lh * 0.5f;
        }
        if (j == 2) markGuns();
        if (in.input <= 1) {   // the aiming stick (left) / the fine stick (right) half way
            const SDL_GamepadAxis ax = in.input == 0 ? SDL_GAMEPAD_AXIS_LEFTX : SDL_GAMEPAD_AXIS_RIGHTX;
            if (j == 2) m.axis(m.a, ax, 0.5f);
            if (j == 12) m.axis(m.a, ax, 0);
        }
        if (in.input == 3) {   // a stroke right on the touchpad (a trackpad: DualSense auto)
            if (j >= 2 && j <= 4) m.touch(m.a, true, 0.4f + 0.05f * float(j - 2), 0.5f);
            if (j == 5) m.touch(m.a, false, 0.5f, 0.5f);
        }
        if (in.style == 3) {   // the gyro: turned right 0.5 rad/s (frames 2 .. 11), else lying flat and still
            const float r[3] = {0, -0.5f, 0};
            m.sensors(m.a, flat, in.input == 2 && j >= 2 && j <= 11 ? r : zero);
        }
    }
    if (k == kInd0 + kIndTrials * kIndLen + 1) {
        m.detach(m.a);
        c.cfg.p2Input = 0;
        p2 = PadProfile{};
        c.pads.configure(c.cfg);
    }
}

void PadTest::fixAfter(int k, Ctx& c)
{
    Impl& m = *m_;
    if (k < 0 || k >= kFixFrames)
        return;
    GunState* g = c.guns;
    const float lw = float(c.lw), lh = float(c.lh);
    const float stick = 2.4f * lh / 60.0f, half = 0.25f;
    const float perRad = lw / (36.0f * kPi / 180.0f);
    char b[512];
    auto expect = [&](bool ok, const std::string& what) { ++m.fixChecks; c.expect(ok, what); };
    auto counts = [&](int a, int b2, int c2, int d, const char* what) { ++m.fixChecks; c.counts(a, b2, c2, d, 0, 0, what); };
    auto moved = [&](float wx, float wy, float tol, const char* what) {
        const float dx = g[1].x - m.mark[1][0], dy = g[1].y - m.mark[1][1];
        std::snprintf(b, sizeof b, "%s: P2 moved (%.2f, %.2f), wanted (%.2f, %.2f)", what, dx, dy, wx, wy);
        expect(std::abs(dx - wx) <= tol && std::abs(dy - wy) <= tol, b);
    };
    auto middle = [&](const char* what) {
        std::snprintf(b, sizeof b, "%s: P2 at (%.1f, %.1f), wanted the middle", what, g[1].x, g[1].y);
        expect(std::abs(g[1].x - lw / 2) < 0.01f && std::abs(g[1].y - lh / 2) < 0.01f, b);
    };
    if (k == 0) {
        // 5: the menu cursor: two pads' gyros (each its own times, each holding its own L2) both move the hand; one
        // pad's L2 does not count for the other
        {
            MenuCursor mc;
            MenuCursorConfig cc;
            SDL_Event ax{};
            ax.type = SDL_EVENT_GAMEPAD_AXIS_MOTION, ax.gaxis.axis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER, ax.gaxis.value = 32767;
            ax.gaxis.which = 11;
            mc.event(ax, cc, 640);
            SDL_Event gy{};
            gy.type = SDL_EVENT_GAMEPAD_SENSOR_UPDATE, gy.gsensor.sensor = SDL_SENSOR_GYRO, gy.gsensor.data[1] = -1.0f;
            for (int n = 1; n <= 4; ++n) {   // (interleaved: pad 11 at 10 ms steps, pad 12 at 5 s + 10 ms steps)
                gy.gsensor.which = 11, gy.gsensor.sensor_timestamp = uint64_t(n) * 10000000ull, mc.event(gy, cc, 640);
                gy.gsensor.which = 12, gy.gsensor.sensor_timestamp = 5000000000ull + uint64_t(n) * 10000000ull, mc.event(gy, cc, 640);
            }
            float dx1 = 0, dy = 0, dx2 = 0;
            bool cl = false;
            mc.take(dx1, dy, cl);
            ax.gaxis.which = 12;
            mc.event(ax, cc, 640);
            for (int n = 5; n <= 8; ++n)
                gy.gsensor.which = 12, gy.gsensor.sensor_timestamp = 5000000000ull + uint64_t(n) * 10000000ull, mc.event(gy, cc, 640);
            mc.take(dx2, dy, cl);
            std::snprintf(b, sizeof b, "menu cursor, two pads: A's gyro with its L2 %+.1f (B without its own L2: nothing), B with its L2 %+.1f",
                          double(dx1), double(dx2));
            expect(dx1 > 5 && dx1 < 30 && dx2 > 30 && dx2 < 40, b);
        }
        // 6: Button Style: which packs have a family's icons; the Controls row greys the missing ones and says so
        std::snprintf(b, sizeof b, "button packs: Xelu Switch 2 %d, Kenney Switch 2 %d, Xelu PS5 %d, Xelu GameCube %d, N64 -> %d (Simple 2)",
                      int(packHasIcons(kPackXelu, kPsSwitch2Pro)), int(packHasIcons(kPackKenney, kPsSwitch2Pro)),
                      int(packHasIcons(kPackXelu, SDL_GAMEPAD_TYPE_PS5)), int(packHasIcons(kPackXelu, kPsGameCube)), packFor(kPackXelu, kPsN64));
        expect(!packHasIcons(kPackXelu, kPsSwitch2Pro) && packHasIcons(kPackKenney, kPsSwitch2Pro) && packHasIcons(kPackXelu, SDL_GAMEPAD_TYPE_PS5) &&
                   !packHasIcons(kPackXelu, kPsGameCube) && packFor(kPackXelu, kPsSwitch2Pro) == kPackKenney && packFor(kPackXelu, kPsN64) == kPackSimple &&
                   packFor(kPackKenney, SDL_GAMEPAD_TYPE_PS5) == kPackKenney,
               b);
        {
            LaunchConfig lc = c.cfg;
            lc.padPrompts = 0, lc.buttonStyle = 0;
            MenuHooks h;
            int fam = kPsSwitch2Pro;
            h.promptStyleNow = [&fam]() { return fam; };
            std::vector<Frontend::Extra> rows = menuCategoryRows(kCatControls, lc, h);
            const Frontend::Extra* bs = nullptr;
            for (const Frontend::Extra& e : rows)
                if (e.name == "Button Style") bs = &e;
            std::snprintf(b, sizeof b, "Button Style with a Switch 2 pad: Xelu grey %d, Kenney grey %d, help \"%s\"",
                          bs && bs->choiceOff ? int(bs->choiceOff(0)) : -1, bs && bs->choiceOff ? int(bs->choiceOff(1)) : -1,
                          bs ? bs->help.c_str() : "(no row)");
            expect(bs && bs->choiceOff && bs->choiceOff(0) && !bs->choiceOff(1) && !bs->choiceOff(2) &&
                       bs->help.find("No Xelu icons") != std::string::npos && bs->help.find("Kenney") != std::string::npos, b);
            fam = SDL_GAMEPAD_TYPE_PS5;
            rows = menuCategoryRows(kCatControls, lc, h);
            bs = nullptr;
            Frontend::Extra *tc = nullptr, *gc = nullptr;
            int aimRows = 0;
            for (Frontend::Extra& e : rows) {
                if (e.name == "Button Style") bs = &e;
                if (e.name == "Touchpad Cursor Sensitivity") tc = &e;
                if (e.name == "Gyro Cursor Sensitivity") gc = &e;
                aimRows += e.name.rfind("Aim Sensitivity", 0) == 0;
            }
            expect(bs && bs->choiceOff && !bs->choiceOff(0) && bs->help.find("No ") == std::string::npos,
                   "Button Style with a DualSense: nothing grey, the usual help");
            // 3 (sens36, GitHub #36): Controls: no master Aim Sensitivity rows; the menu cursor's two rows each set
            // their own value only (the touchpad's bar to 150, the gyro's list to 200 %)
            const bool ok = tc && gc && tc->barSet && tc->barGet && tc->value && gc->pickAt && gc->value;
            std::string v1, v2;
            int t1 = 0, g1 = 0;
            if (ok) {
                tc->barSet(150);
                v1 = tc->value();
                t1 = lc.touchCursorSens, g1 = lc.gyroCursorSens;
                gc->pickAt(19);   // 10 % + 19 steps of 10
                v2 = gc->value();
            }
            std::snprintf(b, sizeof b, "Controls: %d Aim Sensitivity rows; Touchpad Cursor Sensitivity bar 150 = \"%s\" (touch %d, gyro %d), "
                          "Gyro Cursor Sensitivity 200%% = \"%s\" (touch %d, gyro %d)",
                          aimRows, v1.c_str(), t1, g1, v2.c_str(), lc.touchCursorSens, lc.gyroCursorSens);
            expect(ok && aimRows == 0 && v1 == "150%" && t1 == 150 && g1 == 100 && v2 == "200%" && lc.touchCursorSens == 150 &&
                       lc.gyroCursorSens == 200, b);
        }
        // 8 (sens36): the menu cursor's touchpad and gyro speeds apart: each scales its own motion only
        {
            auto run = [](int ts, int gs, float& touchDx, float& gyroDx) {
                MenuCursor mc;
                MenuCursorConfig cc;
                cc.gyro = 2, cc.touchSens = ts, cc.gyroSens = gs;
                SDL_Event t{};
                t.type = SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN, t.gtouchpad.which = 21, t.gtouchpad.x = 0.3f, t.gtouchpad.y = 0.5f;
                mc.event(t, cc, 640);
                for (int n = 1; n <= 5; ++n)
                    t.type = SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION, t.gtouchpad.x = 0.3f + 0.02f * float(n), mc.event(t, cc, 640);
                float dy = 0;
                bool cl = false;
                mc.take(touchDx, dy, cl);
                SDL_Event gy{};
                gy.type = SDL_EVENT_GAMEPAD_SENSOR_UPDATE, gy.gsensor.sensor = SDL_SENSOR_GYRO, gy.gsensor.data[1] = -1.0f;
                gy.gsensor.which = 21;
                for (int n = 1; n <= 4; ++n)
                    gy.gsensor.sensor_timestamp = uint64_t(n) * 10000000ull, mc.event(gy, cc, 640);
                mc.take(gyroDx, dy, cl);
            };
            float t0, g0, tT, gT, tG, gG;
            run(100, 100, t0, g0);
            run(250, 100, tT, gT);
            run(100, 250, tG, gG);
            std::snprintf(b, sizeof b, "menu cursor: touchpad / gyro at 100/100 %.2f / %.2f, touchpad 250 %.2f / %.2f, gyro 250 %.2f / %.2f",
                          double(t0), double(g0), double(tT), double(gT), double(tG), double(gG));
            expect(t0 > 5 && g0 > 5 && std::abs(tT - 2.5f * t0) < 0.01f * t0 && std::abs(gT - g0) < 0.001f &&
                       std::abs(tG - t0) < 0.001f && std::abs(gG - 2.5f * g0) < 0.01f * g0, b);
        }
        // 9 (sens36): the Controller page's speed rows: one per input, each a % (10-500), no Sensitivity / Fine Aim
        // Speed / Aim Sensitivity left, no help line says "on top of"
        {
            int nf = 0;
            const PadField* pf = padFields(nf);
            std::string keys;
            bool okf = true;
            for (int i = 0; i < nf; ++i) {
                const std::string kk = pf[i].key, help = padFieldHelp(pf[i].key);
                okf = okf && kk != "speed" && kk != "fine" && kk != "aimsens" && kk != "sticksens" && kk != "gyrosens" &&
                      kk != "touchsens" && help.find("on top of") == std::string::npos;
                if (pf[i].step > 1) {
                    keys += " " + kk;
                    okf = okf && pf[i].styleMember && pf[i].lo == kPadSensLo && pf[i].hi == kPadSensHi && padFieldText(pf[i], 120) == "120%";
                }
            }
            std::snprintf(b, sizeof b, "Controller page: the per-input speed rows%s (want aimstick aimfine aimgyro aimtouch), 10-500 %%", keys.c_str());
            expect(okf && keys == " aimstick aimfine aimgyro aimtouch", b);
        }
    }
    if (k == 1) {
        // 10 (sens36): an older launcher.ini (padv 3: Sensitivity / Fine Aim Speed per style, the player's Aim / Stick /
        // Gyro / Trackpad Sensitivity, the menu cursor 1-10) converts once to the same speed per input (the old
        // formulas written out here, in screen heights / s, x 1.0 gyro and x 1.0 trackpad); a padv 4 file is kept
        const char* pd = std::getenv("HOTD_PREF_DIR");
        const std::filesystem::path dir = pd && *pd ? std::filesystem::path(pd) : std::filesystem::temp_directory_path();
        const std::string file = (dir / "sens36_migrate_test.ini").string();
        auto loadFrom = [&](const char* text) {
            { std::ofstream o(file, std::ios::binary); o << text; }
            LaunchConfig lc;
            lc.load(file);
            return lc;
        };
        LaunchConfig o = loadFrom("padv=3\npad1.classic.speed=7\npad1.classic.fine=3\npad1.motion.speed=4\npad1.mix.speed=8\n"
                                  "pad1.mix.fine=6\npad1.touch.speed=3\npad1.aimsens=15\npad1.sticksens=8\npad1.gyrosens=12\n"
                                  "pad1.touchsens=20\npad2.classic.speed=10\npad2.aimsens=20\npad2.sticksens=20\ncursorspeed=7\n"
                                  "gyrocursorspeed=3\n");
        m.migrated = o.pad[0];
        const PadStyle *cl = &o.pad[0].s[0], *mo = &o.pad[0].s[1], *mx = &o.pad[0].s[2], *tp = &o.pad[0].s[3];
        // the old speeds: Classic stick 3.5 x 0.8 x 1.5, fine 3.5 x 30 % x 0.8 x 1.5, trackpad 2.0 x 1.5; Motion gyro
        // 0.85 x 1.2 x 1.5; Motion + stick gyro 1.7 x 1.2 x 1.5, fine 2.4 x 60 % x 0.8 x 1.5; Touchpad trackpad 3/5 x 2.0 x 1.5
        const float want[7] = {3.5f * 0.8f * 1.5f, 3.5f * 0.3f * 0.8f * 1.5f, 2.0f * 1.5f, 0.85f * 1.2f * 1.5f, 1.7f * 1.2f * 1.5f,
                               2.4f * 0.6f * 0.8f * 1.5f, 0.6f * 2.0f * 1.5f};
        const float now[7] = {2.4f * cl->stickSens / 100.0f, 0.96f * cl->fineSens / 100.0f, cl->touchSens / 100.0f, mo->gyroSens / 100.0f,
                              mx->gyroSens / 100.0f, 0.96f * mx->fineSens / 100.0f, tp->touchSens / 100.0f};
        bool same = true;
        for (int i = 0; i < 7; ++i) same = same && std::abs(now[i] - want[i]) <= 0.005f * want[i] + 0.0001f;
        std::snprintf(b, sizeof b, "older launcher.ini converted: Classic %d/%d/%d%%, Motion gyro %d%%, Motion + stick %d/%d%%, Touchpad %d%%, "
                      "P2 Classic stick %d%% (clamped), menu cursor %d/%d%%: the same speeds %d",
                      cl->stickSens, cl->fineSens, cl->touchSens, mo->gyroSens, mx->gyroSens, mx->fineSens, tp->touchSens,
                      o.pad[1].s[0].stickSens, o.touchCursorSens, o.gyroCursorSens, int(same));
        expect(same && o.pad[1].s[0].stickSens == kPadSensHi && o.touchCursorSens == 140 && o.gyroCursorSens == 60 &&
                   o.pad[2].s[0].stickSens == 100 && o.pad[2].s[2].gyroSens == 100, b);
        // saved: padv=4, the new keys, none of the old pad keys; read back the same; a padv 4 file is not converted again
        const bool ro = LaunchConfig::readOnly;   // (this test's own file only)
        LaunchConfig::readOnly = false;
        o.save(file);
        LaunchConfig::readOnly = ro;
        std::string all;
        {
            std::ifstream in(file);
            all.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        LaunchConfig back;
        back.load(file);
        LaunchConfig v4 = loadFrom("padv=4\npad1.classic.aimstick=130\npad1.classic.speed=9\npad1.aimsens=20\n");
        std::error_code ec;
        std::filesystem::remove(file, ec);
        const bool kept = back.pad[0].s[0].stickSens == cl->stickSens && back.pad[0].s[2].gyroSens == mx->gyroSens &&
                          back.pad[0].s[3].touchSens == tp->touchSens && back.touchCursorSens == 140 && back.gyroCursorSens == 60;
        std::snprintf(b, sizeof b, "launcher.ini saved: padv=4 %d, aimstick %d, old pad keys %d, cursorspeed=7 %d; read back the same %d; "
                      "a padv 4 file kept (stick %d, gyro %d)",
                      int(all.find("padv=4") != std::string::npos), int(all.find(".classic.aimstick=175") != std::string::npos),
                      int(all.find(".speed=") != std::string::npos || all.find("aimsens=") != std::string::npos),
                      int(all.find("\ncursorspeed=7") != std::string::npos), int(kept), v4.pad[0].s[0].stickSens, v4.pad[0].s[2].gyroSens);
        expect(all.find("padv=4") != std::string::npos && all.find(".classic.aimstick=175") != std::string::npos &&
                   all.find(".speed=") == std::string::npos && all.find("aimsens=") == std::string::npos &&
                   all.find("\ncursorspeed=7") != std::string::npos && kept && v4.pad[0].s[0].stickSens == 130 && v4.pad[0].s[2].gyroSens == 100,
               b);
    }
    if (k == 7) {
        std::snprintf(b, sizeof b, "Classic: the stick pushes the crosshair past the window's right edge (x %.1f, window %.0f, margin %.1f)", g[1].x, lw,
                      lh * 0.05f);
        expect(g[1].x > lw && g[1].x <= lw + lh * 0.05f + 0.01f, b);
    }
    if (k == 8) {
        expect(!c.rt.gun[1].onScreen, "the stick past the edge: off the screen");
        counts(0, 0, 0, 1, "R2 with the stick past the edge = the off-screen reload");
    }
    if (k == 9) {
        std::snprintf(b, sizeof b, "after the off-screen reload the crosshair stays (x %.1f; not the middle)", g[1].x);
        expect(g[1].x > lw, b);
    }
    if (k == 11) middle("a click of the left stick");
    if (k == 22) moved(10 * half * stick * 2.0f, 0, 1.2f, "Classic's Stick Sensitivity 200%: the stick half way for 10 frames");
    if (k == 23) expect(c.pads.styleOf(1) == 3, "Motion + stick: style " + std::to_string(c.pads.styleOf(1)));
    if (k == 24) {
        // relpad: launcher.ini from before (padv 2): Sideways (4) loads as Motion + stick, the Touchpad (5) as 4, its
        // pad1.sideways.* lines are ignored; padv 3: 4 = the Touchpad; a saved file says padv=4 (sens36; was 3) and keeps the style
        const char* pd = std::getenv("HOTD_PREF_DIR");
        const std::filesystem::path dir = pd && *pd ? std::filesystem::path(pd) : std::filesystem::temp_directory_path();
        const std::string file = (dir / "relpad_padv_test.ini").string();
        auto loadFrom = [&](const char* text) {
            { std::ofstream o(file, std::ios::binary); o << text; }
            LaunchConfig lc;
            lc.load(file);
            return lc;
        };
        LaunchConfig o2 = loadFrom("padv=2\npad1.style=4\npad2.style=5\npad3.style=3\npad1.sideways.hand=1\npad1.sideways.shake=0\n");
        std::snprintf(b, sizeof b, "launcher.ini padv 2: styles %d %d %d (want 3 4 3), Motion + stick's shake %d (want 2)", o2.pad[0].style,
                      o2.pad[1].style, o2.pad[2].style, o2.pad[0].s[2].shake);
        expect(o2.pad[0].style == 3 && o2.pad[1].style == 4 && o2.pad[2].style == 3 && o2.pad[0].s[2].shake == 2, b);
        LaunchConfig o3 = loadFrom("padv=3\npad1.style=4\npad2.style=9\n");
        std::snprintf(b, sizeof b, "launcher.ini padv 3: styles %d %d (want 4 = Touchpad, 4 clamped)", o3.pad[0].style, o3.pad[1].style);
        expect(o3.pad[0].style == 4 && o3.pad[1].style == 4, b);
        o3.pad[0].style = 2;
        const bool ro = LaunchConfig::readOnly;   // (this test's own file only)
        LaunchConfig::readOnly = false;
        o3.save(file);
        LaunchConfig::readOnly = ro;
        std::ifstream in(file);
        const std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        LaunchConfig back;
        back.load(file);
        std::error_code ec;
        std::filesystem::remove(file, ec);
        std::snprintf(b, sizeof b, "launcher.ini saved (%zu bytes): padv=4 %d, sideways keys %d, the style kept %d", all.size(),
                      int(all.find("padv=4") != std::string::npos), int(all.find("sideways") != std::string::npos), back.pad[0].style);
        expect(all.find("padv=4") != std::string::npos && all.find("sideways") == std::string::npos && back.pad[0].style == 2, b);
    }
    if (k == 25) {
        int nf = 0;
        const PadField* pf = padFields(nf);
        bool hand = false;
        int hi = -1;
        for (int i = 0; i < nf; ++i) {
            hand = hand || std::string(pf[i].key) == "hand";
            if (std::string(pf[i].key) == "style") hi = pf[i].hi;
        }
        std::snprintf(b, sizeof b, "the Controller page: no Grip row (%d), styles 0..%d, the last \"%s\"", int(hand), hi, kPadStyleNames[kPadStyles]);
        expect(!hand && hi == 4 && std::string(kPadStyleNames[4]) == "Touchpad", b);
    }
    if (k == 36) moved(0, -10 * 0.3f / 60.0f * perRad, 1.0f, "Motion + stick (flat): the front raised = up");
    if (k == 47) moved(10 * 0.5f / 60.0f * perRad, 0, 1.0f, "Motion + stick (flat): turned right = right");
    if (k == 60) counts(0, 0, 0, 1, "Motion + stick: West = reload (as printed)");
    if (k == 61) counts(0, 0, 0, 1, "Motion + stick: East = reload (as printed)");
    // 7 (sens36, GitHub #36): each trial's motion; then per input: the other three speeds at 250 % move it exactly as
    // at 100 % (nothing shared on top), its own 200 % twice as far; the converted older profile as fast as before
    if (k >= kInd0 && k < kInd0 + kIndTrials * kIndLen && (k - kInd0) % kIndLen == 14)
        m.ind[(k - kInd0) / kIndLen] = g[1].x - m.mark[1][0];
    if (k == kInd0 + kIndTrials * kIndLen) {
        for (int i = 0; i < kIndInputs; ++i) {
            const IndInput in = indInput(i * 3);
            const float a = m.ind[i * 3], o = m.ind[i * 3 + 1], two = m.ind[i * 3 + 2];
            std::snprintf(b, sizeof b, "%s, %s Sensitivity: alone at 100%% %.2f px, the other three at 250%% %.2f, its own 200%% %.2f (want the same, x2)",
                          in.style == 1 ? "Classic" : "Motion + stick", kIndNames[in.input], a, o, two);
            expect(a > 3.0f && std::abs(o - a) <= 0.01f * a + 0.3f && std::abs(two - 2.0f * a) <= 0.02f * a + 0.6f, b);
        }
        // the older file's Classic stick 175 % (old: Sensitivity 7 = 3.5 x Stick 80 % x Aim 150 % heights / s) half
        // way 10 frames; its Motion + stick gyro 306 % (old: 1.7 x Gyro 120 % x Aim 150 %) turned 0.5 rad/s 10 readings
        const float st = m.ind[kIndInputs * 3], gy = m.ind[kIndInputs * 3 + 1];
        const float wantSt = 10 * half * 3.5f * 0.8f * 1.5f * lh / 60.0f, wantGy = 10 * 0.5f / 60.0f * perRad * 1.7f * 1.2f * 1.5f;
        std::snprintf(b, sizeof b, "the converted older settings move as before: Classic stick %.2f px (old %.2f)", st, wantSt);
        expect(std::abs(st - wantSt) <= 0.01f * wantSt + 0.5f, b);
        std::snprintf(b, sizeof b, "the converted older settings move as before: Motion + stick gyro %.2f px (old %.2f)", gy, wantGy);
        expect(std::abs(gy - wantGy) <= 0.015f * wantGy + 1.0f, b);
    }
    if (k == kFixFrames - 1) {
        std::snprintf(b, sizeof b, "padfix: %d of %d checks ran", m.fixChecks, kFixChecks);
        c.expect(m.fixChecks == kFixChecks, b);
    }
}

} // namespace hotd::game
