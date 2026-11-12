// padglyphs: the button prompts per controller family. A pad's "prompt style" (an int): SDL's SDL_GamepadType values
// (0..10: Xbox 360, Xbox One / Series, PS3, PS4, PS5, Switch Pro, Joy-Con L / R / pair) and our own past them for the
// pads SDL 3.2 reports as another type (or STANDARD): told apart by vendor / product id and name (padStyleOf).
// keybinds.h padInputName(in, style) names each input as that pad prints it, by SDL's positions (south / east ...):
// a Nintendo pad's south is its B, a GameCube's south its A (SDL maps the GameCube by label).
// Options > Controls > Button Icons (launcher.ini padprompts=): 0 Auto (each pad its own; the keyboard after a key),
// 1-13 one model's icons on any pad (promptChoiceStyle), 14 Keyboard. HOTD_FE_PAD=<padStyleKey>: --frontend-shot.
// The glyphs: Frontend::rPadGlyph (an icon PNG data/ui/prompts/<dir>/<button>.png when present, else drawn).
#pragma once

#include <SDL3/SDL.h>

#include <string>

namespace hotd::game {

enum PadStyleExtra {
    kPsXboxElite = 32,   // Xbox Elite (1 / 2): Xbox One names + paddles P1-P4
    kPsDualSenseEdge,    // DualSense Edge: PS5 names + back buttons / Fn
    kPsSwitch2Pro,       // Switch 2 Pro (057E:2069; SDL 3.2 does not know it: by id)
    kPsJoyCon2L,         // Joy-Con 2 (L) / (R), sideways (057E:2067 / 2066)
    kPsJoyCon2R,
    kPsGameCube,         // GameCube (Wii U adapter 057E:0337, Switch 2's GameCube controller 057E:2073)
    kPsN64,              // Nintendo Switch Online N64 (057E:2019)
    kPsSnes,             // Nintendo Switch Online SNES (057E:2017)
    kPsWiiUPro,          // Wii U Pro (SDL's Wii driver: by name)
    kPsWiiRemote,        // Wii Remote held sideways (+ Nunchuk: C / Z)
    kPsSteamController,  // Steam Controller (28DE:1102 / 1142)
    kPsSteamDeck,        // Steam Deck (28DE:1205)
    kPsGeneric,          // an unknown pad's DirectInput / HID buttons: Button 1 ..
    kPsXboxSeries,       // Xbox Series X|S (045E:0B12 / 0B13 / 0B20 / 0B21; SDL: XBOXONE): its own icons
    // vrprompts (user 2026-11-10: "ReWrithe does not support button icons for any VR"): a VR run's controllers, by the
    // OpenXR interaction profile the runtime chose (vrStyleOfProfile). No pack in data/ui/prompts has VR icons: their
    // prompts are the drawn caps with the controller's own labels (Trigger, B, the menu button's three bars ...)
    kPsVrTouch,          // oculus/touch_controller (Quest 1-3 / Pro, Rift S; Quest Link, Steam Link, SteamVR)
    kPsVrIndex,          // valve/index_controller
    kPsVrVive,           // htc/vive_controller (wands)
    kPsVrWmr,            // microsoft/motion_controller (Windows Mixed Reality)
    kPsVrHp,             // hp/mixed_reality_controller (Reverb G2)
    kPsVrSimple,         // khr/simple_controller (select / menu only)
    kPsLast
};
enum PromptFamily { kPfXbox, kPfPlayStation, kPfNintendo, kPfSteam, kPfGeneric, kPfVr };

// the style of a connected joystick (SDL's type, vendor / product, name)
int padStyleOf(SDL_JoystickID id);
// the same from its parts (the test's virtual pads; padStyleOf calls it)
int padStyleFrom(SDL_GamepadType type, uint16_t vendor, uint16_t product, const char* name);
int promptFamily(int style);
// "xbox360", "xboxone", "ps5", "switch", "gamecube" ... (HOTD_FE_PAD, the icon folders); "" unknown
const char* padStyleKey(int style);
int padStyleByKey(const std::string& key);   // -1: none
const char* padStyleName(int style);          // "Xbox One / Series", "DualSense" ... (the logs, the tests)
// Button Icons' forced style (1 Xbox Series, 2 Xbox One, 3 360, 4 PS5, 5 PS4, 6 PS3, 7 Switch, 8 Switch 2, 9 Wii,
// 10 Wii U, 11 GameCube, 12 Steam Deck, 13 Steam Controller), 0: Auto, -1: Keyboard (14)
extern const char* const kButtonIconNames[15];
int promptChoiceStyle(int choice);
// the icons (data/ui/prompts/<it>.png; "" none): Controls > Button Style 0 Xelu's Free Controller & Key Prompts (the
// default; 100 px), 1 Kenney's Input Prompts (64 px, <family>/double/ 128 px), 2 Simple (none: the drawn caps /
// shapes / text). Both packs CC0 (data/ui/prompts/LICENSE-*.txt, NOTICE.md). A part Xelu lacks: Kenney's, then drawn
enum PromptPack { kPackXelu, kPackKenney, kPackSimple };
std::string padIcon(int pack, int style, int in);   // "xelu/ps5/PS5_Cross", "kenney/gamecube/gamecube_button_a"
std::string keyIcon(int pack, const std::string& key);   // a keyboard / mouse key: "xelu/keyboard/Esc_Key_Dark"
// padfix: whether a pack has icons for a pad family (its face buttons; Simple: always), and the pack its buttons are
// drawn with when that one is chosen (Xelu -> Kenney -> Simple: the first that has them)
bool packHasIcons(int pack, int style);
int packFor(int pack, int style);
// vrprompts: a VR controller's style (kPsVrTouch ..) from an OpenXR interaction profile path
// ("/interaction_profiles/oculus/touch_controller", or its short name "touch", "index", "vive", "wmr", "hp", "simple");
// an unknown / empty one: Touch (the Quest's, the controller most players have). isVrStyle: one of them
int vrStyleOfProfile(const std::string& profile);
bool isVrStyle(int style);
// vrprompts: a controls line's key words for a VR controller, in any language: the translated line's "Enter/A" (the
// pad's A after a slash: Choose), "Esc/B" / "Backspace/B" ... (B: back / delete) and a lone "Enter" become the
// controller's buttons (keybinds.h padInputName: south, east, start). The 33 languages keep those key words as they
// are (data/lang), so one rule serves them all; a line without them is returned as it was
std::string vrPromptLine(const std::string& line, int style);

} // namespace hotd::game
