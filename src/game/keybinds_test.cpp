// --input-test's key / button binding cases (keybinds_test.h).
#include "game/keybinds_test.h"

#include "game/keybinds.h"
#include "game/menu_cursor.h"
#include "game/pad_prompts.h"
#include "game/pause.h"
#include "game/runtime.h"

#include <cmath>
#include <cstdio>

namespace hotd::game {

namespace {

using G = A::G;

SDL_Event keyDown(SDL_Scancode sc)
{
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = sc;
    e.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
    e.key.down = true;
    return e;
}
SDL_Event padDown(SDL_GamepadButton b)
{
    SDL_Event e{};
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    e.gbutton.button = Uint8(b);
    e.gbutton.down = true;
    return e;
}

} // namespace

struct BindTest::Impl {
    SDL_JoystickID id = 0;
    SDL_Joystick* j = nullptr;
    float mark = 0;

    void attach()   // an Xbox pad (SDL's virtual joystick with a gamepad's buttons and axes)
    {
        SDL_VirtualJoystickDesc d;
        SDL_INIT_INTERFACE(&d);
        d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        d.naxes = SDL_GAMEPAD_AXIS_COUNT;
        d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        d.vendor_id = 0x045E;
        d.product_id = 0x0B12;
        d.name = "Test Xbox pad (bindings)";
        id = SDL_AttachVirtualJoystick(&d);
        j = id ? SDL_OpenJoystick(id) : nullptr;
        if (!j)
            SDL_Log("bind test: virtual pad: %s", SDL_GetError());
    }
    void detach()
    {
        if (j) SDL_CloseJoystick(j);
        if (id) SDL_DetachVirtualJoystick(id);
        j = nullptr;
        id = 0;
    }
    void button(SDL_GamepadButton b, bool down) { if (j) SDL_SetJoystickVirtualButton(j, b, down); }
    void trigger(SDL_GamepadAxis ax, float f) { if (j) SDL_SetJoystickVirtualAxis(j, ax, Sint16(-32768.0f + f * 65535.0f)); }
};

BindTest::BindTest() : m_(std::make_unique<Impl>()) {}
BindTest::~BindTest()
{
    m_->detach();
}

bool BindTest::noGameFrame(int i) const { return i == 1182; }

void BindTest::before(int i, Ctx& c)
{
    Impl& m = *m_;
    if (i < kFirst || i > kLast)
        return;
    auto press = [&](SDL_Scancode sc) { c.key(sc, true); c.key(sc, false); };
    if (i == kFirst || i == 1190)   // (nobody runs out of lives meanwhile)
        for (uint32_t p = 0; p < 2; ++p)
            if (c.rt.mem.u32(G::players + p * 0xCC) == 5 && int32_t(c.rt.mem.u32(G::players + p * 0xCC + 0xC)) < 5)
                c.rt.mem.set32(G::players + p * 0xCC + 0xC, 9);
    KeyBinds* k = c.cfg.keys;
    // the keyboard rebound in play (the game reads cfg.keys as the menu leaves them)
    if (i == 1173 || i == 1187) {   // player 2's crosshair in the middle (the pad cases left it off the screen: a shot = a reload)
        c.guns[1].x = c.lw * 0.5f;
        c.guns[1].y = c.lh * 0.5f;
    }
    if (i == 1174) { k[1].k[kActFire][0] = SDL_SCANCODE_G; k[1].k[kActFire][1] = SDL_SCANCODE_UNKNOWN; press(SDL_SCANCODE_SPACE); }
    if (i == 1175) press(SDL_SCANCODE_G);
    if (i == 1176) { k[0].k[kActFire][0] = SDL_SCANCODE_K; k[0].k[kActReload][0] = SDL_SCANCODE_L; press(SDL_SCANCODE_K); }
    if (i == 1177) press(SDL_SCANCODE_L);
    if (i == 1178) {
        k[0].k[kActRight][0] = SDL_SCANCODE_O;
        c.guns[0].x = c.lw * 0.5f;
        m.mark = c.guns[0].x;
        c.key(SDL_SCANCODE_O, true);
    }
    if (i == 1179) { m.mark = c.guns[0].x; c.key(SDL_SCANCODE_O, false); }
    if (i == 1180) { k[1].k[kActStart][0] = SDL_SCANCODE_9; press(SDL_SCANCODE_9); }
    if (i == 1181) press(SDL_SCANCODE_2);
    if (i == 1182) { k[0].k[kActPause][0] = SDL_SCANCODE_U; press(SDL_SCANCODE_U); }
    if (i == 1183) press(SDL_SCANCODE_U);
    if (i == 1184) press(SDL_SCANCODE_P);
    if (i == 1185) {
        for (int p = 0; p < kBindPlayers; ++p) k[p] = defaultKeys(p);
        press(SDL_SCANCODE_SPACE);
    }
    // a virtual Xbox pad for player 2 (Classic), its buttons rebound: West fires, South reloads
    if (i == 1186) {
        m.attach();
        c.cfg.p2Input = 2;
        c.cfg.pad[1] = PadProfile{};
        c.cfg.pad[1].style = 1;
        c.pads.configure(c.cfg);
    }
    if (i == 1188) {
        PadBinds& b = c.cfg.padBinds[1];
        b = c.pads.defaultBinds(1);
        b.custom = true;
        b.mask[kPaFire] = 1u << kPiWest;
        b.mask[kPaReload] = 1u << kPiSouth;
        c.pads.configure(c.cfg);
        m.button(SDL_GAMEPAD_BUTTON_WEST, true);
    }
    if (i == 1189) { m.button(SDL_GAMEPAD_BUTTON_WEST, false); m.trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1.0f); }
    if (i == 1190) { m.trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.0f); m.button(SDL_GAMEPAD_BUTTON_SOUTH, true); }
    if (i == 1191) m.button(SDL_GAMEPAD_BUTTON_SOUTH, false);
    if (i == 1192) {   // a paddle (Elite / Edge) as the fire button
        c.cfg.padBinds[1].mask[kPaFire] = 1u << kPiPaddle1;
        c.pads.configure(c.cfg);
        m.button(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, true);
    }
    if (i == 1193) m.button(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, false);
    if (i == 1194) {   // Reset Pad: the style's own buttons again (R2 fires)
        c.cfg.padBinds[1] = PadBinds{};
        c.pads.configure(c.cfg);
        m.trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1.0f);
    }
    if (i == 1195) { m.trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.0f); m.button(SDL_GAMEPAD_BUTTON_WEST, true); }
    if (i == 1196) m.button(SDL_GAMEPAD_BUTTON_WEST, false);
    if (i == 1198) {
        m.detach();
        c.cfg.p2Input = 0;
        c.cfg.pad[1] = PadProfile{};
        c.cfg.padBinds[1] = PadBinds{};
        c.pads.configure(c.cfg);
    }
}

void BindTest::after(int i, Ctx& c)
{
    Impl& m = *m_;
    if (i < kFirst || i > kLast)
        return;
    char b[256];
    if (i == kFirst) {
        // the defaults: today's keys (main.cpp's before the bindings)
        const KeyBinds d1 = defaultKeys(0), d2 = defaultKeys(1), d3 = defaultKeys(2), d4 = defaultKeys(3);
        c.expect(!d1.has(kActStart, SDL_SCANCODE_1) && d1.has(kActPause, SDL_SCANCODE_1) && d1.has(kActPause, SDL_SCANCODE_P) && !d1.k[kActFire][0] &&
                     d2.has(kActFire, SDL_SCANCODE_SPACE) && d2.has(kActFire, SDL_SCANCODE_RCTRL) &&
                     d2.has(kActReload, SDL_SCANCODE_RSHIFT) && d2.has(kActReload, SDL_SCANCODE_RETURN) &&
                     d2.has(kActLeft, SDL_SCANCODE_LEFT) && d2.has(kActStart, SDL_SCANCODE_2) &&
                     d3.has(kActUp, SDL_SCANCODE_W) && d3.has(kActFire, SDL_SCANCODE_F) && d3.has(kActStart, SDL_SCANCODE_3) &&
                     d4.has(kActUp, SDL_SCANCODE_I) && d4.has(kActUp, SDL_SCANCODE_KP_8) && d4.has(kActFire, SDL_SCANCODE_KP_0) &&
                     d4.has(kActReload, SDL_SCANCODE_KP_PERIOD) && d4.has(kActStart, SDL_SCANCODE_4),
                 "bindings: the default keys are the old fixed ones (P1 1 / P, P2 arrows Space R-Ctrl R-Shift Enter 2, P3 WASD F R 3, P4 IJKL keypad H Y 4)");
        bool same = true;
        for (int p = 0; p < kBindPlayers; ++p) same = same && c.cfg.keys[p] == defaultKeys(p) && !c.cfg.padBinds[p].custom;
        c.expect(same, "bindings: a launcher.ini without them = the defaults (this run's settings)");
        // launcher.ini: nothing written for the defaults; changed ones read back the same
        KeyBinds keys[kBindPlayers];
        PadBinds pads[kBindPlayers];
        for (int p = 0; p < kBindPlayers; ++p) keys[p] = defaultKeys(p);
        const std::string none = bindsIni(keys, pads);
        c.expect(none.empty(), "bindings: launcher.ini gets no binding lines for the defaults (\"" + none + "\")");
        keys[1].k[kActFire][0] = SDL_SCANCODE_G;
        keys[1].k[kActFire][1] = SDL_SCANCODE_UNKNOWN;
        keys[2].k[kActPause][0] = SDL_SCANCODE_TAB;
        keys[3].k[kActReload][0] = keys[3].k[kActReload][1] = SDL_SCANCODE_UNKNOWN;
        pads[0].custom = true;
        pads[0].mask[kPaFire] = (1u << kPiR2) | (1u << kPiPaddle2);
        pads[0].mask[kPaPause] = 1u << kPiMisc;
        const std::string ini = bindsIni(keys, pads);
        KeyBinds back[kBindPlayers];
        PadBinds backP[kBindPlayers];
        for (int p = 0; p < kBindPlayers; ++p) back[p] = defaultKeys(p);
        int lines = 0;
        std::string line;
        for (char ch : ini + "\n") {
            if (ch != '\n') {
                line += ch;
                continue;
            }
            if (const size_t eq = line.find('='); eq != std::string::npos)
                lines += bindsIniLine(line.substr(0, eq), line.substr(eq + 1), back, backP) ? 1 : 0;
            line.clear();
        }
        bool equal = true;
        for (int p = 0; p < kBindPlayers; ++p) equal = equal && back[p] == keys[p] && backP[p] == pads[p];
        std::snprintf(b, sizeof b, "bindings: launcher.ini round trip (%d lines read back, the same: %s)", lines, equal ? "yes" : "no");
        c.expect(equal && lines == 3 + kPadActions, b);
        KeyBinds k1[kBindPlayers] = {defaultKeys(0), defaultKeys(1), defaultKeys(2), defaultKeys(3)};
        bindsIniLine("key1.fire", "41,9", k1, backP);   // Esc (fixed) is left out
        c.expect(k1[0].k[kActFire][0] == SDL_SCANCODE_F && !k1[0].k[kActFire][1], "bindings: launcher.ini: a fixed key (Esc) is not read");
        // the capture (the menu's "press a key"): synthetic events handed to it, no desktop input
        BindCapture cap;
        KeyBinds kb[kBindPlayers] = {defaultKeys(0), defaultKeys(1), defaultKeys(2), defaultKeys(3)};
        PadBinds pb;
        const PadBinds pd = c.pads.defaultBinds(1);
        cap.start(0, kActFire, false, 1000);
        cap.event(keyDown(SDL_SCANCODE_G), kb, pb, pd, SDL_GAMEPAD_TYPE_UNKNOWN, 1500);
        c.expect(!cap.active() && kb[0].has(kActFire, SDL_SCANCODE_G) && cap.changes == 1, "capture: G pressed = P1 Fire (" + cap.message + ")");
        cap.start(0, kActFire, false, 2000);
        cap.event(keyDown(SDL_SCANCODE_G), kb, pb, pd, SDL_GAMEPAD_TYPE_UNKNOWN, 2100);
        c.expect(!kb[0].k[kActFire][0], "capture: G again = taken off (" + cap.message + ")");
        cap.start(1, kActFire, false, 3000);
        const bool ate = cap.event(keyDown(SDL_SCANCODE_ESCAPE), kb, pb, pd, SDL_GAMEPAD_TYPE_UNKNOWN, 3100);
        c.expect(ate && !cap.active() && kb[1] == defaultKeys(1) && cap.message == "Cancelled", "capture: Esc cancels, nothing changed");
        cap.start(1, kActFire, false, 4000);
        c.expect(!cap.tick(8999) && cap.active() && cap.secondsLeft(8999) == 1, "capture: waiting 4.999 s, 1 s left");
        const bool timedOut = cap.tick(9000);
        c.expect(timedOut && !cap.active() && kb[1] == defaultKeys(1) && cap.message.rfind("Timed out", 0) == 0,
                 "capture: 5 s without a key = timed out (" + cap.message + ")");
        cap.start(1, kActFire, false, 10000);
        cap.event(keyDown(SDL_SCANCODE_F11), kb, pb, pd, SDL_GAMEPAD_TYPE_UNKNOWN, 10100);
        c.expect(!cap.active() && kb[1] == defaultKeys(1), "capture: F11 is fixed (" + cap.message + ")");
        cap.start(0, kActFire, false, 11000);
        cap.event(keyDown(SDL_SCANCODE_SPACE), kb, pb, pd, SDL_GAMEPAD_TYPE_UNKNOWN, 11100);
        c.expect(kb[0].has(kActFire, SDL_SCANCODE_SPACE) && cap.message.find("also P2 Fire") != std::string::npos &&
                     keyClash(kb, 0, kActFire, SDL_SCANCODE_SPACE) == "P2 Fire",
                 "capture: Space for P1 Fire = used twice, named (" + cap.message + ")");
        cap.start(0, kActFire, false, 12000);
        cap.event(keyDown(SDL_SCANCODE_J), kb, pb, pd, SDL_GAMEPAD_TYPE_UNKNOWN, 12100);
        cap.start(0, kActFire, false, 13000);
        cap.event(keyDown(SDL_SCANCODE_X), kb, pb, pd, SDL_GAMEPAD_TYPE_UNKNOWN, 13100);
        c.expect(kb[0].k[kActFire][0] == SDL_SCANCODE_SPACE && kb[0].k[kActFire][1] == SDL_SCANCODE_X,
                 "capture: two keys at most (a third replaces the second): " + keyList(kb[0], kActFire));
        cap.start(0, kActFire, false, 14000);
        SDL_Event click{};
        click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        cap.event(click, kb, pb, pd, SDL_GAMEPAD_TYPE_UNKNOWN, 14100);
        c.expect(!cap.active() && cap.message == "Cancelled", "capture: a mouse click cancels");
        // the pad's: an Auto pad's first change starts from its style's buttons
        cap.start(1, kPaFire, true, 15000);
        const bool keyIgnored = cap.event(keyDown(SDL_SCANCODE_G), kb, pb, pd, SDL_GAMEPAD_TYPE_UNKNOWN, 15050) && cap.active();
        cap.event(padDown(SDL_GAMEPAD_BUTTON_WEST), kb, pb, pd, SDL_GAMEPAD_TYPE_XBOXONE, 15100);
        std::snprintf(b, sizeof b, "capture: pad West (X) for Fire: added to the style's R2 R1 A (%s), also Reload named (%s), a key ignored meanwhile",
                      padList(pb.mask[kPaFire], SDL_GAMEPAD_TYPE_XBOXONE).c_str(), cap.message.c_str());
        c.expect(keyIgnored && pb.custom && pb.mask[kPaFire] == (pd.mask[kPaFire] | (1u << kPiWest)) &&
                     pb.mask[kPaReload] == pd.mask[kPaReload] && cap.message.find("also Reload") != std::string::npos, b);
        cap.start(1, kPaFire, true, 16000);
        SDL_Event ax{};
        ax.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
        ax.gaxis.axis = Uint8(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        ax.gaxis.value = 30000;
        cap.event(ax, kb, pb, pd, SDL_GAMEPAD_TYPE_PS5, 16100);
        c.expect(!(pb.mask[kPaFire] >> kPiR2 & 1) && cap.message.find("R2 taken off") == 0, "capture: R2 pulled = taken off Fire (" + cap.message + ")");
        cap.start(1, kPaFire, true, 17000);
        const uint32_t was = pb.mask[kPaFire];
        cap.event(padDown(SDL_GAMEPAD_BUTTON_TOUCHPAD), kb, pb, pd, SDL_GAMEPAD_TYPE_PS5, 17100);
        c.expect(!cap.active() && pb.mask[kPaFire] == was, "capture: the touchpad's click is not bound (" + cap.message + ")");
        std::snprintf(b, sizeof b, "pad defaults (Classic): Fire %s, Reload %s, Start %s, Pause %s",
                      padList(pd.mask[kPaFire], SDL_GAMEPAD_TYPE_PS5).c_str(), padList(pd.mask[kPaReload], SDL_GAMEPAD_TYPE_PS5).c_str(),
                      padList(pd.mask[kPaStart], SDL_GAMEPAD_TYPE_PS5).c_str(), padList(pd.mask[kPaPause], SDL_GAMEPAD_TYPE_PS5).c_str());
        c.expect(pd.mask[kPaFire] == ((1u << kPiR2) | (1u << kPiR1) | (1u << kPiSouth)) &&
                     pd.mask[kPaReload] == ((1u << kPiL2) | (1u << kPiL1) | (1u << kPiEast) | (1u << kPiWest)) &&
                     pd.mask[kPaStart] == 1u << kPiStart && pd.mask[kPaPause] == 1u << kPiBack, b);
        c.expect(padInputName(kPiSouth, SDL_GAMEPAD_TYPE_PS5) == "Cross" && padInputName(kPiSouth, SDL_GAMEPAD_TYPE_XBOXONE) == "A" &&
                     padInputName(kPiSouth, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO) == "B" && padInputName(kPiR2, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO) == "ZR",
                 "pad button names by the pad's type (Cross / A / B, ZR)");
        // padglyphs: every family as SDL reports it here: a virtual pad of each id / name, its prompt style (SDL's type,
        // the id, the name) and two of its buttons' names (one a face button: the one the player presses)
        static const struct {
            uint16_t vid, pid;
            const char* name;
            int style, in1;
            const char* n1;
            int in2;
            const char* n2;
        } kFam[] = {
            {0x045E, 0x028E, "Xbox 360 Controller", SDL_GAMEPAD_TYPE_XBOX360, kPiStart, "Start", kPiBack, "Back"},
            {0x045E, 0x0B12, "Xbox Series X Controller", kPsXboxSeries, kPiStart, "Menu", kPiBack, "View"},
            {0x045E, 0x02EA, "Xbox One Controller", SDL_GAMEPAD_TYPE_XBOXONE, kPiStart, "Menu", kPiMisc, "Share"},
            {0x045E, 0x0B00, "Xbox Elite Wireless Controller Series 2", kPsXboxElite, kPiPaddle1, "P1", kPiSouth, "A"},
            {0x054C, 0x0268, "PLAYSTATION(R)3 Controller", SDL_GAMEPAD_TYPE_PS3, kPiBack, "Select", kPiSouth, "Cross"},
            {0x054C, 0x09CC, "DualShock 4", SDL_GAMEPAD_TYPE_PS4, kPiBack, "Share", kPiStart, "Options"},
            {0x054C, 0x0CE6, "DualSense Wireless Controller", SDL_GAMEPAD_TYPE_PS5, kPiBack, "Create", kPiEast, "Circle"},
            {0x054C, 0x0DF2, "DualSense Edge Wireless Controller", kPsDualSenseEdge, kPiPaddle4, "Fn L", kPiL1, "L1"},
            {0x057E, 0x2009, "Nintendo Switch Pro Controller", SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, kPiSouth, "B", kPiEast, "A"},
            {0x057E, 0x2008, "Nintendo Switch Joy-Con (L/R)", SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR, kPiSouth, "B", kPiPaddle1, "SR (R)"},
            {0x057E, 0x2006, "Nintendo Switch Joy-Con (L)", SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT, kPiL1, "SL", kPiSouth, "Left"},
            {0x057E, 0x2007, "Nintendo Switch Joy-Con (R)", SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT, kPiR1, "SR", kPiSouth, "A"},
            {0x057E, 0x2069, "Nintendo Switch 2 Pro Controller", kPsSwitch2Pro, kPiMisc, "C", kPiSouth, "B"},
            {0x057E, 0x2066, "Joy-Con 2 (R)", kPsJoyCon2R, kPiL1, "SL", kPiEast, "X"},
            {0x057E, 0x0337, "Nintendo GameCube Controller Adapter", kPsGameCube, kPiSouth, "A", kPiR1, "Z"},
            {0x057E, 0x2019, "Nintendo N64 Controller", kPsN64, kPiL2, "Z", kPiSouth, "A"},
            {0x057E, 0x2017, "Nintendo SNES Controller", kPsSnes, kPiBack, "Select", kPiSouth, "B"},
            {0x057E, 0x0330, "Nintendo Wii U Pro Controller", kPsWiiUPro, kPiSouth, "B", kPiL2, "ZL"},
            {0x057E, 0x0306, "Nintendo Wii Remote", kPsWiiRemote, kPiWest, "1", kPiNorth, "2"},
            {0x28DE, 0x1102, "Steam Controller", kPsSteamController, kPiPaddle1, "R Grip", kPiSouth, "A"},
            {0x28DE, 0x1205, "Steam Deck", kPsSteamDeck, kPiPaddle2, "L4", kPiStart, "Menu"},
            {0x1234, 0x5678, "Generic USB Gamepad", -1, kPiSouth, "A", kPiL1, "LB"},
        };
        for (const auto& f : kFam) {
            SDL_VirtualJoystickDesc d;
            SDL_INIT_INTERFACE(&d);
            d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            d.naxes = SDL_GAMEPAD_AXIS_COUNT;
            d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            d.vendor_id = f.vid, d.product_id = f.pid, d.name = f.name;
            const SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
            const int st = id ? padStyleOf(id) : -2;
            const int sdlType = id ? int(SDL_GetGamepadTypeForID(id)) : -1;
            if (id) SDL_DetachVirtualJoystick(id);
            const bool styleOk = f.style >= 0 ? st == f.style : promptFamily(st) == kPfXbox;
            std::snprintf(b, sizeof b, "prompts: %s (%04X:%04X, SDL type %d) -> %s: %s, %s", f.name, f.vid, f.pid, sdlType,
                          padStyleName(st), padInputName(f.in1, st).c_str(), padInputName(f.in2, st).c_str());
            c.expect(styleOk && padInputName(f.in1, st) == f.n1 && padInputName(f.in2, st) == f.n2, b);
        }
        // a Steam Input virtual pad: the type Steam gives (else Xbox names), never Steam's own
        c.expect(padStyleFrom(SDL_GAMEPAD_TYPE_PS5, 0x28DE, 0x11FF, "Steam Virtual Gamepad") == SDL_GAMEPAD_TYPE_PS5 &&
                     promptFamily(padStyleFrom(SDL_GAMEPAD_TYPE_XBOX360, 0x28DE, 0x11FF, "Steam Virtual Gamepad")) == kPfXbox,
                 "prompts: a Steam Input pad shows the pad underneath (PS5 -> PlayStation, else Xbox)");
        // the Button Prompts setting and every style's icon files (data/ui/prompts) named
        c.expect(promptChoiceStyle(0) == 0 && promptChoiceStyle(4) == SDL_GAMEPAD_TYPE_PS5 && promptChoiceStyle(12) == kPsSteamDeck && promptChoiceStyle(1) == kPsXboxSeries && padIcon(kPackXelu, kPsXboxSeries, kPiSouth) == "xelu/xboxseries/XboxSeriesX_A" && padIcon(kPackXelu, SDL_GAMEPAD_TYPE_XBOXONE, kPiSouth) == "xelu/xboxone/XboxOne_A" &&
                     promptChoiceStyle(14) < 0 && padIcon(kPackXelu, SDL_GAMEPAD_TYPE_PS4, kPiBack) == "xelu/ps4/PS4_Share" && padIcon(kPackXelu, kPsGameCube, kPiSouth).empty() && keyIcon(kPackXelu, "Esc") == "xelu/keyboard/Esc_Key_Dark" &&
                     padIcon(kPackKenney, kPsGameCube, kPiSouth) == "kenney/gamecube/gamecube_button_a" && padIcon(kPackKenney, kPsN64, kPiSouth).empty(),
                 "prompts: Button Icons Auto / Xbox Series / PS5 / Steam Deck / Keyboard, the icons by model (Xelu PS4 Share, Esc; GameCube: Kenney A; N64 none)");
        // padglyphs: the menus' pad cursor (menu_cursor.h) from SDL's events: a touchpad stroke right moves it right,
        // its click clicks; the gyro moves it only while ZL / L2 / LT is held (While Holding); Touchpad Off: not used
        {
            MenuCursor mc;
            MenuCursorConfig cc;
            SDL_Event t{};
            t.type = SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN, t.gtouchpad.x = 0.3f, t.gtouchpad.y = 0.5f;
            bool used = mc.event(t, cc, 640);
            for (int k = 1; k <= 5; ++k) {
                t.type = SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION, t.gtouchpad.x = 0.3f + 0.02f * float(k);
                used = mc.event(t, cc, 640) && used;
            }
            SDL_Event bt{};
            bt.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN, bt.gbutton.button = SDL_GAMEPAD_BUTTON_TOUCHPAD;
            used = mc.event(bt, cc, 640) && used;
            float dx = 0, dy = 0, gdx = 0, gdy = 0, gdx2 = 0;
            bool click = false, c2 = false;
            mc.take(dx, dy, click);
            SDL_Event g{};
            g.type = SDL_EVENT_GAMEPAD_SENSOR_UPDATE, g.gsensor.sensor = SDL_SENSOR_GYRO, g.gsensor.data[1] = -1.0f;
            for (int k = 1; k <= 4; ++k) g.gsensor.sensor_timestamp = uint64_t(k) * 10000000ull, mc.event(g, cc, 640);
            mc.take(gdx, gdy, c2);   // (not held: nothing)
            SDL_Event ax{};
            ax.type = SDL_EVENT_GAMEPAD_AXIS_MOTION, ax.gaxis.axis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER, ax.gaxis.value = 32767;
            mc.event(ax, cc, 640);
            for (int k = 5; k <= 8; ++k) g.gsensor.sensor_timestamp = uint64_t(k) * 10000000ull, mc.event(g, cc, 640);
            mc.take(gdx2, gdy, c2);
            MenuCursorConfig off;
            off.touchpad = false;
            t.type = SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION;
            const bool offUsed = mc.event(t, off, 640);
            std::snprintf(b, sizeof b, "pad cursor: touchpad stroke %+.1f units, click %d, gyro not held %+.1f, held %+.1f, Off passes the touch on %d",
                          double(dx), int(click), double(gdx), double(gdx2), int(!offUsed));
            c.expect(used && dx > 20 && std::fabs(dy) < 1 && click && gdx == 0 && gdx2 > 5 && !offUsed, b);
        }
    }
    if (i == 1174) c.counts(0, 0, 0, 0, 0, 0, "P2 Fire rebound to G: Space does nothing");
    if (i == 1175) c.counts(0, 1, 0, 0, 0, 0, "P2 Fire = G");
    if (i == 1176) c.counts(1, 0, 0, 0, 0, 0, "P1 Fire bound to K (the mouse player's keys)");
    if (i == 1177) c.counts(0, 0, 1, 0, 0, 0, "P1 Reload bound to L");
    if (i == 1178) {
        std::snprintf(b, sizeof b, "P1 Aim Right bound to O, held: moved %.2f (wanted %.2f)", c.guns[0].x - m.mark, c.lw / 90.0f);
        c.expect(std::abs(c.guns[0].x - m.mark - c.lw / 90.0f) < 0.01f, b);
    }
    if (i == 1179) c.expect(std::abs(c.guns[0].x - m.mark) < 0.01f, "P1 Aim Right let go: stays");
    if (i == 1180) c.counts(0, 0, 0, 0, 0, 1, "P2 Start bound to 9");
    if (i == 1181) c.counts(0, 0, 0, 0, 0, 0, "P2 Start moved off 2: key 2 does nothing");
    if (i == 1182) c.expect(c.pause.paused(), "P1 Pause bound to U: the pause");
    if (i == 1183) c.expect(!c.pause.paused(), "U again: going on");
    if (i == 1184) c.expect(!c.pause.paused(), "P (the old pause key, rebound) does not pause");
    if (i == 1185) c.counts(0, 1, 0, 0, 0, 0, "keys reset to the defaults: Space = P2 Fire again");
    if (i == 1187)
        c.expect(m.id && c.pads.padOf(1) == m.id, "a virtual Xbox pad for player 2 (bindings)");
    if (i == 1188) c.counts(0, 1, 0, 0, 0, 0, "pad rebound: West (X) fires");
    if (i == 1189) c.counts(0, 0, 0, 0, 0, 0, "pad rebound: R2 (not bound any more) does nothing");
    if (i == 1190) c.counts(0, 0, 0, 1, 0, 0, "pad rebound: South (A) reloads");
    if (i == 1192) c.counts(0, 1, 0, 0, 0, 0, "pad rebound: a back paddle fires");
    if (i == 1194) c.counts(0, 1, 0, 0, 0, 0, "Reset Pad: R2 fires again");
    if (i == 1195) c.counts(0, 0, 0, 1, 0, 0, "Reset Pad: West (X) reloads again");
}

} // namespace hotd::game
