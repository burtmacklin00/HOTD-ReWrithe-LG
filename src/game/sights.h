// The crosshair choices (display): the built-in styles' names and steps, and the player's own pictures. A `crosshairs`
// folder (the package's data\crosshairs and the settings folder's crosshairs) holds either
//   crosshair.png            one picture, tinted per player (1P red, 2P blue; draw it white or grey), or
//   p1.png (+ p2.png)        one picture per player, used as they are (no p2.png: p1.png for both),
// directly in the folder or in a sub-folder of it (the sub-folder's name is the choice's name); any other .png directly
// in the folder is a tinted single picture named after the file. A picture pixel = a window pixel at 1080 lines
// (scaled with the window height, and by the Crosshair size setting), centred on the aim.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hotd::assets { class PcSprites; }

namespace hotd::game {

// the styles (LaunchConfig::crosshair): 0 auto, 1 arcade, 2 PC, 3 simple cross, 4 ring + dot, 6 corner brackets,
// 7 gap cross, 10 capped cross (user 2026-10-31: a plus whose arms end in thin bars across; after the gap cross in the
// menu), 9 a custom picture set (LaunchConfig::crosshairCustom), 11 off (gunfb, a light-gun player's feedback
// 2026-11-03: no crosshair at all, as the arcade; the aim and the HUD prompts unchanged; after the capped cross in the
// menu). 5 (dotted ring) and 8 (pointer) were removed (user 2026-10-19); their numbers stay unused so saved settings
// keep their meaning: a saved 5 / 8 becomes 4 / 3.
constexpr int kSightStyles = 12;
constexpr int kSightCustom = 9;
constexpr int kSightCapped = 10;
constexpr int kSightOff = 11;
inline int sightStyleKept(int style) { return style == 5 ? 4 : style == 8 ? 3 : style; }
const char* sightStyleName(int style);   // "simple cross", "ring + dot", ...
// the command-line word -> style (-1 unknown); "custom:<name>" is style 9 with the name in `custom`
int sightStyleFromWord(const std::string& word, std::string& custom);
// firing range (user 2026-10-26 "player 1 2 3 4 want different options"): each player's own crosshair
// (LaunchConfig::sights[seat]; launcher.ini crosshair= / crosshaircustom= / crosssize= / crossthick= / crosscolour= for
// player 1, the same keys ending in 2, 3, 4 for the others; a player without them gets player 1's)
struct SeatSight {
    int style = 0;             // the styles above
    std::string custom;        // style 9: the custom set's name
    int size = 2, thick = 1;   // the Crosshair size / thickness steps
    int colour = 0;            // kSightColourNames: 0 the player's own colour (P1 red, P2 blue, P3 green, P4 yellow)
    bool operator==(const SeatSight& o) const
    {
        return style == o.style && custom == o.custom && size == o.size && thick == o.thick && colour == o.colour;
    }
};
// the crosshair colours: 0 the player's colour, then fixed ones (a custom set with its own p1 / p2 pictures keeps them)
constexpr int kSightColours = 10;
extern const char* const kSightColourNames[kSightColours];
uint32_t sightColourRgb(int colour);   // 0xRRGGBB; 0 for the player's colour
// Crosshair size / thickness steps
constexpr int kSizeSteps = 5, kThickSteps = 4;
extern const char* const kSizeNames[kSizeSteps];
extern const char* const kThickNames[kThickSteps];
float sightSizeFactor(int step);        // 0.6 .. 1.6 (default step 2 = 1.0)
float sightThicknessPx(int step);       // line thickness in pixels at 1080 lines: 1 .. 4 (default step 1 = 2)

struct CustomSight {
    std::string name;       // "Custom: <name>" in the menus: the folder's or the file's name
    std::string p1, p2;     // the PNG files (p2 empty: p1 for both players)
    bool tint = false;      // p1 is a white / grey picture tinted per player
};
// every set found in the given crosshairs folders (the first folder's wins on equal names), sorted by name
std::vector<CustomSight> findCustomSights(const std::vector<std::string>& dirs);
// the next choice after (style, custom): the built-in styles, then each custom set
void nextSight(int& style, std::string& custom, const std::vector<CustomSight>& sets);
std::string sightLabel(int style, const std::string& custom);
// the set of that name (case ignored; null: none)
const CustomSight* findSight(const std::vector<CustomSight>& sets, const std::string& name);

// optui: a sight's picture on the CPU (the HUD's textures, the menu's crosshair preview): rgba w x h; drawn dw x dh
// pixels with its pixel (cx, cy) on the aim (linear: scaled, drawn smooth)
struct SightArt {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0, dw = 0, dh = 0, cx = 0, cy = 0;
    bool linear = false;
};
// the PC's sight (kind 1) or the arcade style (kind 0: its shape in the player's colour; the drawn triangles without the
// PC sprites; kind 2 the 25 x 25 simple cross) for seat 0-3, in the PC's 640 x 480 pixels (dw x dh: scaled to a window
// wh pixels high, as the HUD draws it). false: the PC sprites are needed and missing (pc null or not loaded)
// rgb (firing range): not 0: the picture in that colour (its shading kept), else the player's colour
bool sightSpriteArt(const assets::PcSprites* pc, int kind, int seat, int wh, SightArt& out, uint32_t rgb = 0);
// the drawn styles (3 simple cross, 4 ring + dot, 6 corner brackets, 7 gap cross) and 9 a custom set (custom: its
// files; null: the simple cross) for seat 0-3 in the pixels of a window wh pixels high, with the Crosshair size /
// thickness steps. false: the custom set could not be read (err: why; the simple cross drawn instead)
bool sightPixelArt(int style, int size, int thick, const CustomSight* custom, int wh, int seat, SightArt& out,
                   std::string* err = nullptr, uint32_t rgb = 0);
// firing range: a player's crosshair as the game draws it (Auto follows the models: the PC's sight with the PC models,
// else the arcade style; without the PC's sprites the arcade style's drawn shape). false: a custom set could not be
// read (err; the simple cross drawn instead)
bool seatSightArt(const SeatSight& s, bool pcModels, const assets::PcSprites* pc, const std::vector<CustomSight>& sets,
                  int seat, int wh, SightArt& out, std::string* err = nullptr);

} // namespace hotd::game
