// padglyphs: the pads' prompt styles (pad_prompts.h)
#include "game/pad_prompts.h"
#include "game/keybinds.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace hotd::game {

namespace {

bool has(const char* name, const char* s) { return name && SDL_strcasestr(name, s) != nullptr; }

const struct { int style; const char* key; const char* name; } kStyles[] = {
    {SDL_GAMEPAD_TYPE_XBOX360, "xbox360", "Xbox 360"},
    {SDL_GAMEPAD_TYPE_XBOXONE, "xboxone", "Xbox One / Series"},
    {kPsXboxElite, "elite", "Xbox Elite"},
    {SDL_GAMEPAD_TYPE_PS3, "ps3", "PS3"},
    {SDL_GAMEPAD_TYPE_PS4, "ps4", "PS4"},
    {SDL_GAMEPAD_TYPE_PS5, "ps5", "DualSense"},
    {kPsDualSenseEdge, "edge", "DualSense Edge"},
    {SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, "switch", "Switch Pro"},
    {SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR, "joycon", "Joy-Con pair"},
    {SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT, "joyconl", "Joy-Con (L) sideways"},
    {SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT, "joyconr", "Joy-Con (R) sideways"},
    {kPsSwitch2Pro, "switch2", "Switch 2 Pro"},
    {kPsJoyCon2L, "joycon2l", "Joy-Con 2 (L) sideways"},
    {kPsJoyCon2R, "joycon2r", "Joy-Con 2 (R) sideways"},
    {kPsGameCube, "gamecube", "GameCube"},
    {kPsN64, "n64", "N64 (NSO)"},
    {kPsSnes, "snes", "SNES (NSO)"},
    {kPsWiiUPro, "wiiupro", "Wii U Pro"},
    {kPsWiiRemote, "wii", "Wii Remote"},
    {kPsSteamController, "steamcontroller", "Steam Controller"},
    {kPsSteamDeck, "steamdeck", "Steam Deck"},
    {kPsXboxSeries, "xboxseries", "Xbox Series X|S"},
    {kPsGeneric, "generic", "Generic"},
    {kPsVrTouch, "vrtouch", "Meta Quest Touch (VR)"},
    {kPsVrIndex, "vrindex", "Valve Index (VR)"},
    {kPsVrVive, "vrvive", "HTC Vive (VR)"},
    {kPsVrWmr, "vrwmr", "Windows Mixed Reality (VR)"},
    {kPsVrHp, "vrhp", "HP Reverb G2 (VR)"},
    {kPsVrSimple, "vrsimple", "Simple controller (VR)"},
    {SDL_GAMEPAD_TYPE_STANDARD, "standard", "Standard (Xbox names)"},
};

} // namespace

int padStyleFrom(SDL_GamepadType type, uint16_t vendor, uint16_t product, const char* name)
{
    // by id first: the pads SDL 3.2 reports as another type (or STANDARD)
    if (vendor == 0x045E && (product == 0x02E3 || product == 0x0B00 || product == 0x0B05 || product == 0x0B22))
        return kPsXboxElite;
    if (vendor == 0x045E && (product == 0x0B12 || product == 0x0B13 || product == 0x0B20 || product == 0x0B21))
        return kPsXboxSeries;
    if (vendor == 0x054C && product == 0x0DF2)
        return kPsDualSenseEdge;
    if (vendor == 0x28DE) {
        if (product == 0x1205) return kPsSteamDeck;
        if (product == 0x1102 || product == 0x1142) return kPsSteamController;
        // 0x11FF: Steam Input's virtual pad: SDL's type is the pad underneath (Steam tells it), else Xbox
    }
    if (vendor == 0x057E) {
        switch (product) {
        case 0x2069: return kPsSwitch2Pro;
        case 0x2066: return kPsJoyCon2R;
        case 0x2067: return kPsJoyCon2L;
        case 0x0337: case 0x2073: return kPsGameCube;
        case 0x2019: return kPsN64;
        case 0x2017: return kPsSnes;
        default: break;
        }
    }
    // SDL's Wii driver names them (its type: STANDARD); the Wii U Pro shares the Wii Remote Plus's id
    if (has(name, "Wii U Pro") || has(name, "Classic Controller")) return kPsWiiUPro;
    if (has(name, "Wii Remote")) return kPsWiiRemote;
    if (has(name, "GameCube")) return kPsGameCube;
    if (type > SDL_GAMEPAD_TYPE_STANDARD && type < SDL_GAMEPAD_TYPE_COUNT)
        return int(type);
    // a generic type: the name (a mapping without a type, a wrapper, a Bluetooth name)
    if (has(name, "Steam Deck")) return kPsSteamDeck;
    if (has(name, "Steam Controller")) return kPsSteamController;
    if (has(name, "DualSense")) return SDL_GAMEPAD_TYPE_PS5;
    if (has(name, "DualShock 4") || has(name, "PS4")) return SDL_GAMEPAD_TYPE_PS4;
    if (has(name, "PS3") || has(name, "PLAYSTATION(R)3")) return SDL_GAMEPAD_TYPE_PS3;
    if (has(name, "Xbox Series")) return kPsXboxSeries;
    if (has(name, "Xbox 360")) return SDL_GAMEPAD_TYPE_XBOX360;
    if (has(name, "Xbox")) return SDL_GAMEPAD_TYPE_XBOXONE;
    if (has(name, "N64")) return kPsN64;
    if (has(name, "SNES")) return kPsSnes;
    if (has(name, "Joy-Con")) return SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
    if (has(name, "Pro Controller") && vendor == 0x057E) return SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO;
    return int(type);   // STANDARD / UNKNOWN: SDL's Xbox-like positions, named as an Xbox pad
}

int padStyleOf(SDL_JoystickID id)
{
    return padStyleFrom(SDL_GetGamepadTypeForID(id), SDL_GetJoystickVendorForID(id), SDL_GetJoystickProductForID(id),
                        SDL_GetJoystickNameForID(id));
}

int promptFamily(int style)
{
    switch (style) {
    case SDL_GAMEPAD_TYPE_PS3: case SDL_GAMEPAD_TYPE_PS4: case SDL_GAMEPAD_TYPE_PS5: case kPsDualSenseEdge:
        return kPfPlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    case kPsSwitch2Pro: case kPsJoyCon2L: case kPsJoyCon2R: case kPsGameCube: case kPsN64: case kPsSnes:
    case kPsWiiUPro: case kPsWiiRemote:
        return kPfNintendo;
    case kPsSteamController: case kPsSteamDeck:
        return kPfSteam;
    case kPsGeneric:
        return kPfGeneric;
    case kPsVrTouch: case kPsVrIndex: case kPsVrVive: case kPsVrWmr: case kPsVrHp: case kPsVrSimple:
        return kPfVr;
    default:
        return kPfXbox;
    }
}

const char* padStyleKey(int style)
{
    for (const auto& s : kStyles)
        if (s.style == style) return s.key;
    return style == SDL_GAMEPAD_TYPE_UNKNOWN ? "xboxone" : "";
}

int padStyleByKey(const std::string& key)
{
    // (the families' short names too: xbox, playstation, nintendo, steam)
    if (key == "xbox") return kPsXboxSeries;
    if (key == "playstation" || key == "ps") return SDL_GAMEPAD_TYPE_PS5;
    if (key == "nintendo") return SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO;
    if (key == "steam") return kPsSteamDeck;
    for (const auto& s : kStyles)
        if (key == s.key) return s.style;
    return -1;
}

const char* padStyleName(int style)
{
    for (const auto& s : kStyles)
        if (s.style == style) return s.name;
    return "Unknown (Xbox names)";
}

static std::string kenneyIconRaw(int style, int in)
{
    if (in < 0 || in >= 21)
        return {};
    // (PadInput order: south east west north, L1 R1 L2 R2 L3 R3, Start Back Misc, paddles R1 L1 R2 L2, d-pad U D L R;
    // "" = no icon: the drawn glyph / the text)
    using T = const char* const[21];
    static T kXbox = {"xbox/xbox_button_a", "xbox/xbox_button_b", "xbox/xbox_button_x", "xbox/xbox_button_y", "xbox/xbox_lb", "xbox/xbox_rb",
                      "xbox/xbox_lt", "xbox/xbox_rt", "xbox/xbox_ls", "xbox/xbox_rs", "xbox/xbox_button_menu", "xbox/xbox_button_view",
                      "xbox/xbox_button_share", "", "", "", "", "xbox/xbox_dpad_up", "xbox/xbox_dpad_down", "xbox/xbox_dpad_left", "xbox/xbox_dpad_right"};
    static T kPs = {"playstation/playstation_button_cross", "playstation/playstation_button_circle", "playstation/playstation_button_square",
                    "playstation/playstation_button_triangle", "playstation/playstation_trigger_l1", "playstation/playstation_trigger_r1",
                    "playstation/playstation_trigger_l2", "playstation/playstation_trigger_r2", "playstation/playstation_button_l3",
                    "playstation/playstation_button_r3", "playstation/playstation5_button_options", "playstation/playstation5_button_create",
                    "playstation/playstation5_button_mute", "", "", "", "", "playstation/playstation_dpad_up", "playstation/playstation_dpad_down",
                    "playstation/playstation_dpad_left", "playstation/playstation_dpad_right"};
    static T kSw = {"switch/switch_button_b", "switch/switch_button_a", "switch/switch_button_y", "switch/switch_button_x", "switch/switch_button_l",
                    "switch/switch_button_r", "switch/switch_button_zl", "switch/switch_button_zr", "switch/switch_stick_l", "switch/switch_stick_r",
                    "switch/switch_button_plus", "switch/switch_button_minus", "", "", "", "", "", "switch/switch_up", "switch/switch_down",
                    "switch/switch_left", "switch/switch_right"};
    static T kSw2 = {"switch2/switch_button_b", "switch2/switch_button_a", "switch2/switch_button_y", "switch2/switch_button_x", "switch2/switch_button_l",
                     "switch2/switch_button_r", "switch2/switch_button_zl", "switch2/switch_button_zr", "switch2/switch_stick_l", "switch2/switch_stick_r",
                     "switch2/switch_button_plus", "switch2/switch_button_minus", "switch2/switch_button_c", "switch2/switch_button_gr",
                     "switch2/switch_button_gl", "", "", "switch2/switch_up", "switch2/switch_down", "switch2/switch_left", "switch2/switch_right"};
    static T kGc = {"gamecube/gamecube_button_a", "gamecube/gamecube_button_b", "gamecube/gamecube_button_x", "gamecube/gamecube_button_y", "",
                    "gamecube/gamecube_button_z", "gamecube/gamecube_trigger_l", "gamecube/gamecube_trigger_r", "gamecube/gamecube_stick",
                    "gamecube/gamecube_stick_c", "gamecube/gamecube_button_start", "", "", "", "", "", "", "gamecube/gamecube_dpad_up",
                    "gamecube/gamecube_dpad_down", "gamecube/gamecube_dpad_left", "gamecube/gamecube_dpad_right"};
    static T kWiiU = {"wiiu/wiiu_button_b", "wiiu/wiiu_button_a", "wiiu/wiiu_button_y", "wiiu/wiiu_button_x", "wiiu/wiiu_button_l", "wiiu/wiiu_button_r",
                      "wiiu/wiiu_button_zl", "wiiu/wiiu_button_zr", "wiiu/wiiu_stick_l", "wiiu/wiiu_stick_r", "wiiu/wiiu_button_plus",
                      "wiiu/wiiu_button_minus", "", "", "", "", "", "wiiu/wiiu_dpad_up", "wiiu/wiiu_dpad_down", "wiiu/wiiu_dpad_left", "wiiu/wiiu_dpad_right"};
    static T kWii = {"wii/wii_button_b", "wii/wii_button_a", "wii/wii_button_1", "wii/wii_button_2", "wii/wii_button_c", "", "wii/wii_button_z", "",
                     "wii/wii_stick", "", "wii/wii_button_plus", "wii/wii_button_minus", "", "", "", "", "", "wii/wii_dpad_up", "wii/wii_dpad_down",
                     "wii/wii_dpad_left", "wii/wii_dpad_right"};
    static T kSc = {"steamcontroller/steam_button_a", "steamcontroller/steam_button_b", "steamcontroller/steam_button_x", "steamcontroller/steam_button_y",
                    "steamcontroller/steam_lb", "steamcontroller/steam_rb", "steamcontroller/steam_lt", "steamcontroller/steam_rt",
                    "steamcontroller/steam_stick", "", "steamcontroller/steam_button_start_icon", "steamcontroller/steam_button_back_icon", "",
                    "steamcontroller/steam_rg", "steamcontroller/steam_lg", "", "", "steamcontroller/steam_dpad_up", "steamcontroller/steam_dpad_down",
                    "steamcontroller/steam_dpad_left", "steamcontroller/steam_dpad_right"};
    static T kDeck = {"steamdeck/steamdeck_button_a", "steamdeck/steamdeck_button_b", "steamdeck/steamdeck_button_x", "steamdeck/steamdeck_button_y",
                      "steamdeck/steamdeck_button_l1", "steamdeck/steamdeck_button_r1", "steamdeck/steamdeck_button_l2", "steamdeck/steamdeck_button_r2",
                      "steamdeck/steamdeck_stick_l", "steamdeck/steamdeck_stick_r", "steamdeck/steamdeck_button_options", "steamdeck/steamdeck_button_view",
                      "steamdeck/steamdeck_button_quickaccess", "steamdeck/steamdeck_button_r4", "steamdeck/steamdeck_button_l4",
                      "steamdeck/steamdeck_button_r5", "steamdeck/steamdeck_button_l5", "steamdeck/steamdeck_dpad_up", "steamdeck/steamdeck_dpad_down",
                      "steamdeck/steamdeck_dpad_left", "steamdeck/steamdeck_dpad_right"};
    // the per-model differences
    switch (style) {
    case SDL_GAMEPAD_TYPE_XBOX360:
        if (in == 10) return "xbox/xbox_button_start";
        if (in == 11) return "xbox/xbox_button_back";
        if (in == 12) return {};
        return kXbox[in];
    case kPsXboxElite: {
        static const char* const kP[4] = {"xbox/xbox_elite_paddle_top_right", "xbox/xbox_elite_paddle_top_left",
                                          "xbox/xbox_elite_paddle_bottom_right", "xbox/xbox_elite_paddle_bottom_left"};
        return in >= 13 && in <= 16 ? kP[in - 13] : kXbox[in];
    }
    case SDL_GAMEPAD_TYPE_PS3:
        if (in == 10) return "playstation/playstation3_button_start";
        if (in == 11) return "playstation/playstation3_button_select";
        return in == 12 ? std::string() : kPs[in];
    case SDL_GAMEPAD_TYPE_PS4:
        if (in == 10) return "playstation/playstation4_button_options";
        if (in == 11) return "playstation/playstation4_button_share";
        return in == 12 ? std::string() : kPs[in];
    case SDL_GAMEPAD_TYPE_PS5:
        return kPs[in];
    case kPsDualSenseEdge: {
        static const char* const kP[4] = {"playstation/playstation5_elite_rb", "playstation/playstation5_elite_lb", "playstation/playstation5_elite_fn_r",
                                          "playstation/playstation5_elite_fn_l"};
        return in >= 13 && in <= 16 ? kP[in - 13] : kPs[in];
    }
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        return kSw[in];
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: {
        static const char* const kP[4] = {"switch/switch_button_sr", "switch/switch_button_sl", "switch/switch_button_sl", "switch/switch_button_sr"};
        return in >= 13 && in <= 16 ? kP[in - 13] : kSw[in];
    }
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT: case kPsJoyCon2L: {
        static T kL = {"switch/switch_left", "switch/switch_down", "switch/switch_up", "switch/switch_right", "switch/switch_button_sl",
                       "switch/switch_button_sr", "", "", "switch/switch_stick_l", "", "switch/switch_button_minus", "", "", "", "", "", "", "", "", "", ""};
        return kL[in];
    }
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: case kPsJoyCon2R: {
        static T kR = {"switch/switch_button_a", "switch/switch_button_x", "switch/switch_button_b", "switch/switch_button_y", "switch/switch_button_sl",
                       "switch/switch_button_sr", "", "", "switch/switch_stick_r", "", "switch/switch_button_plus", "", "", "", "", "", "", "", "", "", ""};
        return kR[in];
    }
    case kPsSwitch2Pro: return kSw2[in];
    case kPsGameCube: return kGc[in];
    case kPsSnes:   // (the Switch's letters and shoulders; Start / Select as text)
        return in < 6 || in >= 17 ? kSw[in] : std::string();
    case kPsWiiUPro: return kWiiU[in];
    case kPsWiiRemote: return kWii[in];
    case kPsSteamController: return kSc[in];
    case kPsSteamDeck: return kDeck[in];
    case kPsN64: case kPsGeneric: return {};
    case kPsVrTouch: case kPsVrIndex: case kPsVrVive: case kPsVrWmr: case kPsVrHp: case kPsVrSimple: return {};   // (no VR icons)
    default: return kXbox[in];   // Xbox One / Series, STANDARD, UNKNOWN
    }
}

// Xelu's Free Controller & Key Prompts (CC0): one set a model (360 / One / Series, PS3 / PS4 / PS5, Switch, Wii, Wii U,
// Steam Controller, Steam Deck); none for Switch 2, GameCube, N64, generic pads, the paddles (Kenney's then)
static std::string xeluIconRaw(int style, int in)
{
    // a set: its folder, its prefix, then the inputs' names (PadInput order; "" none)
    struct Set { const char* dir; const char* pre; const char* n[21]; };
#define XD "Dpad_Up", "Dpad_Down", "Dpad_Left", "Dpad_Right"
    static const Set kSeries = {"xboxseries", "XboxSeriesX_", {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Left_Stick_Click", "Right_Stick_Click",
                                                              "Menu", "View", "Share", "", "", "", "", XD}};
    static const Set kOne = {"xboxone", "XboxOne_", {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Left_Stick_Click", "Right_Stick_Click", "Menu",
                                                    "Windows", "", "", "", "", "", XD}};
    static const Set k360 = {"xbox360", "360_", {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Left_Stick_Click", "Right_Stick_Click", "Start", "Back",
                                                "", "", "", "", "", XD}};
    static const Set kPs3 = {"ps3", "PS3_", {"Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "Left_Stick_Click", "Right_Stick_Click",
                                            "Start", "Select", "", "", "", "", "", XD}};
    static const Set kPs4 = {"ps4", "PS4_", {"Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "Left_Stick_Click", "Right_Stick_Click",
                                            "Options", "Share", "", "", "", "", "", XD}};
    static const Set kPs5 = {"ps5", "PS5_", {"Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "Left_Stick_Click", "Right_Stick_Click",
                                            "Options", "Share", "Microphone", "", "", "", "", XD}};
    static const Set kSw = {"switch", "Switch_", {"B", "A", "Y", "X", "LB", "RB", "LT", "RT", "Left_Stick_Click", "Right_Stick_Click", "Plus", "Minus",
                                                 "Square", "", "", "", "", XD}};
    static const Set kJcL = {"switch", "Switch_", {"Left", "Down", "Up", "Right", "", "", "", "", "Left_Stick_Click", "", "Minus", "", "", "", "", "",
                                                  "", "", "", "", ""}};
    static const Set kJcR = {"switch", "Switch_", {"A", "X", "B", "Y", "", "", "", "", "Right_Stick_Click", "", "Plus", "", "", "", "", "", "", "",
                                                  "", "", ""}};
    static const Set kSnes = {"switch", "Switch_", {"B", "A", "Y", "X", "LB", "RB", "", "", "", "", "", "", "", "", "", "", "", XD}};
    static const Set kWiiU = {"wiiu", "WiiU_", {"B", "A", "Y", "X", "L", "R", "ZL", "ZR", "Left_Stick", "Right_Stick", "Plus", "Minus", "", "", "",
                                               "", "", XD}};
    static const Set kWii = {"wii", "Wii_", {"B", "A", "1", "2", "C", "", "Z", "", "Stick", "", "Plus", "Minus", "", "", "", "", "", XD}};
    static const Set kSc = {"steam", "Steam_", {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Stick", "", "Start", "Back", "", "Right_Grip",
                                               "Left_Grip", "", "", "", "", "", ""}};
    static const Set kDeck = {"steamdeck", "SteamDeck_", {"A", "B", "X", "Y", "L1", "R1", "L2", "R2", "Left_Stick_Click", "Right_Stick_Click", "Menu",
                                                         "Square", "Dots", "R4", "L4", "R5", "L5", XD}};
#undef XD
    const Set* s = nullptr;
    switch (style) {
    case SDL_GAMEPAD_TYPE_XBOX360: s = &k360; break;
    case SDL_GAMEPAD_TYPE_XBOXONE: case kPsXboxElite: s = &kOne; break;
    case SDL_GAMEPAD_TYPE_PS3: s = &kPs3; break;
    case SDL_GAMEPAD_TYPE_PS4: s = &kPs4; break;
    case SDL_GAMEPAD_TYPE_PS5: case kPsDualSenseEdge: s = &kPs5; break;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: s = &kSw; break;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT: case kPsJoyCon2L: s = &kJcL; break;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: case kPsJoyCon2R: s = &kJcR; break;
    case kPsSnes: s = &kSnes; break;
    case kPsWiiUPro: s = &kWiiU; break;
    case kPsWiiRemote: s = &kWii; break;
    case kPsSteamController: s = &kSc; break;
    case kPsSteamDeck: s = &kDeck; break;
    case kPsSwitch2Pro: case kPsGameCube: case kPsN64: case kPsGeneric: return {};
    case kPsVrTouch: case kPsVrIndex: case kPsVrVive: case kPsVrWmr: case kPsVrHp: case kPsVrSimple: return {};   // (no VR icons)
    default: s = &kSeries; break;   // Xbox Series, STANDARD, UNKNOWN
    }
    return *s->n[in] ? std::string(s->dir) + "/" + s->pre + s->n[in] : std::string();
}

std::string padIcon(int pack, int style, int in)
{
    if (in < 0 || in >= 21)
        return {};
    const std::string r = pack == kPackXelu ? xeluIconRaw(style, in) : kenneyIconRaw(style, in);
    return r.empty() ? r : (pack == kPackXelu ? "xelu/" : "kenney/") + r;
}

bool packHasIcons(int pack, int style)
{
    return pack >= kPackSimple || !padIcon(pack, style, 0).empty();   // (0: kPiSouth, keybinds.h)
}

int packFor(int pack, int style)
{
    for (int p = std::clamp(pack, 0, int(kPackSimple)); p < kPackSimple; ++p)
        if (packHasIcons(p, style)) return p;
    return kPackSimple;
}

std::string keyIcon(int pack, const std::string& key)
{
    // a key's prompt name (keybinds.h keyName / the prompts' "Esc", "F5", "Wheel" ...) -> Xelu's / Kenney's file
    static const struct { const char* key; const char* xelu; const char* kenney; } kKeys[] = {
        {"Esc", "Esc", "escape"}, {"Enter", "Enter", "enter"}, {"Return", "Enter", "enter"}, {"Space", "Space", "space"},
        {"Tab", "Tab", "tab"}, {"Shift", "Shift", "shift"}, {"L Shift", "Shift", "shift"}, {"R Shift", "Shift", "shift"},
        {"Ctrl", "Ctrl", "ctrl"}, {"L Ctrl", "Ctrl", "ctrl"}, {"R Ctrl", "Ctrl", "ctrl"}, {"Alt", "Alt", "alt"}, {"L Alt", "Alt", "alt"},
        {"R Alt", "Alt", "alt"}, {"Del", "Del", "delete"}, {"Delete", "Del", "delete"}, {"Backspace", "Backspace", "backspace"},
        {"Home", "Home", "home"}, {"End", "End", "end"}, {"PgUp", "Page_Up", "page_up"}, {"PgDn", "Page_Down", "page_down"},
        {"Insert", "Insert", "insert"}, {"Up", "Arrow_Up", "arrow_up"}, {"Down", "Arrow_Down", "arrow_down"}, {"Left", "Arrow_Left", "arrow_left"},
        {"Right", "Arrow_Right", "arrow_right"}, {"Wheel", "Mouse_Middle", "mouse_scroll"}, {"Drag", "Mouse_Left", "mouse_left"},
        {"Click", "Mouse_Left", "mouse_left"}, {"R-Drag", "Mouse_Right", "mouse_right"}, {"-", "Minus", "minus"}, {"+", "Plus", "plus"},
    };
    for (const auto& k : kKeys)
        if (key == k.key)
            return pack == kPackXelu ? std::string("xelu/keyboard/") + k.xelu + "_Key_Dark" : std::string("kenney/keyboard/") + (std::strncmp(k.kenney, "mouse", 5) == 0 ? "" : "keyboard_") + k.kenney;
    // a letter, a digit, F1-F12
    const bool fkey = key.size() >= 2 && key.size() <= 3 && key[0] == 'F' && std::isdigit(uint8_t(key[1])) && std::atoi(key.c_str() + 1) >= 1 &&
                      std::atoi(key.c_str() + 1) <= 12;
    if ((key.size() == 1 && std::isalnum(uint8_t(key[0]))) || fkey) {
        std::string lower = key;
        for (char& ch : lower) ch = char(std::tolower(uint8_t(ch)));
        std::string up = key;
        for (char& ch : up) ch = char(std::toupper(uint8_t(ch)));
        return pack == kPackXelu ? "xelu/keyboard/" + up + "_Key_Dark" : "kenney/keyboard/keyboard_" + lower;
    }
    return {};
}

const char* const kButtonIconNames[15] = {"Auto", "Xbox Series", "Xbox One", "Xbox 360", "PS5", "PS4", "PS3", "Switch", "Switch 2",
                                         "Wii", "Wii U", "GameCube", "Steam Deck", "Steam Controller", "Keyboard"};

int promptChoiceStyle(int choice)
{
    static const int kStyle[15] = {0, kPsXboxSeries, SDL_GAMEPAD_TYPE_XBOXONE, SDL_GAMEPAD_TYPE_XBOX360, SDL_GAMEPAD_TYPE_PS5,
                                   SDL_GAMEPAD_TYPE_PS4, SDL_GAMEPAD_TYPE_PS3, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, kPsSwitch2Pro,
                                   kPsWiiRemote, kPsWiiUPro, kPsGameCube, kPsSteamDeck, kPsSteamController, -1};
    return choice > 0 && choice < 15 ? kStyle[choice] : 0;
}

int vrStyleOfProfile(const std::string& p)
{
    auto has = [&p](const char* s) { return p.find(s) != std::string::npos; };
    if (has("valve/index") || p == "index") return kPsVrIndex;
    if (has("htc/vive") || p == "vive") return kPsVrVive;
    if (has("microsoft/motion") || p == "wmr") return kPsVrWmr;
    if (has("hp/mixed_reality") || p == "hp") return kPsVrHp;
    if (has("khr/simple") || p == "simple") return kPsVrSimple;
    return kPsVrTouch;   // oculus/touch_controller, meta/touch_*, none reported yet, unknown
}

bool isVrStyle(int style) { return style >= kPsVrTouch && style <= kPsVrSimple; }

std::string vrPromptLine(const std::string& line, int style)
{
    // the button that is Enter on the big screen (vr_scene.cpp panelFrame: A / X; Vive / WMR: the trackpad); the
    // simple controller has none (its select clicks)
    const char* enter = style == kPsVrVive || style == kPsVrWmr ? "Trackpad" : style == kPsVrSimple ? "Select" : "A";
    std::string out;
    size_t at = 0;
    while (at < line.size()) {
        // one word at a time (between spaces / brackets; a UTF-8 letter never holds those bytes)
        const size_t end = std::min(line.find_first_of(" ()", at), line.size());
        if (end == at) {
            out += line[at++];
            continue;
        }
        const std::string w = line.substr(at, end - at);
        if (w.size() > 2 && w.compare(w.size() - 2, 2, "/A") == 0)
            out += padInputName(kPiSouth, style);   // "Enter/A": Choose
        else if (w.size() > 2 && w.compare(w.size() - 2, 2, "/B") == 0)
            out += padInputName(kPiEast, style);    // "Esc/B", "Backspace/B", "Rücktaste/B": Back / Delete
        else if (w == "Enter")
            out += enter;
        else
            out += w;
        at = end;
    }
    return out;
}

} // namespace hotd::game
