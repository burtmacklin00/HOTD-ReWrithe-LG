// The cosmetic built-in mods (docs/modding.md; GitHub #8 / #10, cosmods 2026-11-03): Rainbow Blood and Big Head.
// Like the Ragdoll mod (game/ragdoll.h) their code is the game's own and their folder (data\mods\rainbow-blood,
// data\mods\big-head) gives them their texts and settings. Both are off by default.
//
// DRAW ONLY: Rainbow Blood is a renderer colour (RenderOptions::rainbow: the arcade's blood palette entry 1023 and the
// PC's blood texels, marked at the load); Big Head scales the drawn head models in the frame's draw list after the
// frame's logic (bigHeadFrame). Nothing here writes the address space: the game logic, the hit test, the scores, the
// proofs and netplay are those of the game without the mods.
#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace hotd::game {

class Runtime;

struct RainbowBloodSettings {
    bool on = false;
    bool together = false;   // "Colours: Together": all blood one colour at a time; else each splash / part its own
    // hue cycles per second (Still 0, Slow 0.1, Normal 0.2, Fast 0.444): the user (2026-11-03) wanted an RGB PC's slow
    // rainbow wave (Normal: one cycle in 5 s; was 2 s); Fast is the Chapter 4 screens' own (a table step per 1.5 frames)
    float speed = 0.2f;
    // "Creature Blood" (user 2026-11-03): 0 Own Colour Each (each creature gets a hue when it appears: its painted wounds,
    // its blood texels and the blood on its parts in it for its whole life), 1 Rainbow Cycle (those cycle as the
    // splashes do; the default, as the user asked), 2 Off (the wounds as the game paints them; the blood on its parts
    // as the splashes)
    int creature = 1;
    // "HUD & Crosshair" (user 2026-11-03, rainbow2): 0 Off (default), 1 Crosshair (the players' sights cycle, each
    // player a phase of their own), 2 HUD (the player HUD: bullets, 1P / 2P, torches, the score, its prompts), 3 Both
    int hud = 0;
    bool menu = false;   // v1.3.1 "Menu Blood: Rainbow": the menus' blood too (Off by default: each change redraws the menu)
};
RainbowBloodSettings& rainbowBloodSettings();
// rainbow2: the rainbow colour now (0..1 RGB) at hue offset `offset` (added to the phase; "Together" leaves it out
// unless `always`), the same as the renderer's (render/renderer.cpp uRainbowTable): the game's 90-colour table 0xAB120
// blended, a plain hue if it is missing. false: the mod is off (rgb untouched)
bool rainbowColour(const Runtime& rt, float offset, float rgb[3], bool always = false);
// v1.3.1 (user 2026-11-07: "the rainbow blood mod to affect the main menu blood"): the colour for the menus' blood at
// `seconds` (the computer screens' table once a game has read it, else a plain hue; 45 steps a cycle). false: mod off
bool rainbowMenuColour(double seconds, float rgb[3], bool smooth = false);   // (smooth: the continuous cycle for the GPU-tinted menu; else 12 steps)
void rainbowTableCache(const Runtime& rt);   // (the table read once from a runtime with the game loaded: palette_anim_copy)
// The custom Blood Color on the arcade models (user 2026-11-03: the Rainbow Blood recolour with one colour): the
// creatures' painted wounds take it too (RenderOptions::rainbow 3). 0xRRGGBB, -1 none; the view sets it each frame
int& customWoundBlood();
// mod.ini [settings] keys colours / speed / creature, values as the Mods page shows them
void rainbowBloodConfigure(bool on, const std::map<std::string, std::string>& values);
// Rainbow Blood's creatures (draws only): each enemy body (DrawModel::body, its type an enemy: not the people, not the
// bosses) gets a hue the first frame it is drawn (or drawn again after 2 s away, or with another type: a new creature
// in the slot), from its body address and that frame (the same in every run). Call once per game frame after the
// draw list is complete (Runtime::frame)
void creatureBloodFrame(Runtime& rt);
// the DrawItem::bloodHue of a model drawn by this body: -1 none (the mods off, no body); 0..1 an enemy's own hue; 2 +
// its offset: cycling (Rainbow Cycle); 4 + the body's offset: cycling, its blood rows too but no red-pixel test (people,
// bosses: rainbow2); 6 + the body's offset: cycling, wounds as painted (Creature Blood Off)
float creatureBloodHue(uint32_t body);
// rainbow2: the body's colours stay as painted although they are blood rows (none now: the Chariot, whose whole
// body is drawn in them, cycles like the game's own green blood recolours it, as the user asked)
bool creatureBloodKeepsRows(uint32_t body);

struct BigHeadSettings {
    bool on = false;
    float scale = 2.0f;      // the heads' size (1.5x, 2x, 3x)
    bool everyone = true;    // "Heads: Everyone": the people too (the players in cut-scenes, Sophie, the researchers, Curien;
                             // the default since v1.2.0, user 2026-11-03; Enemies: the creatures only)
    bool bosses = true;      // the bosses' heads (the Chariot, the Hangedman, the Hermit, the Magician)
};
BigHeadSettings& bigHeadSettings();
// mod.ini [settings] keys size / who / bosses
void bigHeadConfigure(bool on, const std::map<std::string, std::string>& values);

// Hermit Fly Head (user 2026-11-09): the Hermit (Chapter 3's boss) drawn with the game's own unused second head,
// boss31_kao 0x1AC6 (two big glowing red eyes; docs/cut_content.md), in place of the head the game uses, boss3_kao
// 0x1AD9. Both are in the arcade's chapter 3 models with the same texture area (11); the 1998 PC files have no
// 0x1AC6, so with the PC models the mod draws the PC's own head (nothing changes). Draw only (GameView::addModel)
struct HermitHeadSettings {
    bool on = false;
};
HermitHeadSettings& hermitHeadSettings();
void hermitHeadConfigure(bool on, const std::map<std::string, std::string>& values);
constexpr uint32_t kHermitHeadModel = 0x1AD9, kHermitFlyHeadModel = 0x1AC6;

// Magician Afterimages (user 2026-11-07, GitHub #54): the colour of his afterimages (polygon palette entry 583, which
// the game sets 0xFEC0 blue as he flies, 0x801F red for the swoops and lunges) per phase: -1 Original, -2 Rainbow,
// else 0xRRGGBB (a named colour or "#RRGGBB" in the mod.ini / launcher.ini). Draw only
struct MagicianAfterimageSettings {
    bool on = false;
    int phase1 = -1, phase2 = -1;
    float speed = 0.2f;   // rainbow hue cycles per second
};
MagicianAfterimageSettings& magicianAfterimageSettings();
void magicianAfterimageConfigure(bool on, const std::map<std::string, std::string>& values);
// the colour word to draw for the game's `word` at `frame` (RGB555 with bit 15, R in the low bits): the game's own
// unless the mod changes that phase
uint16_t magicianAfterimageWord(uint16_t word, uint32_t frame);
// the DrawItem::palOver of a model drawn by the task `task` (v1.3.1: "each afterimage its own colour", user
// 2026-11-07): the Magician's afterimage (model 0x1C20) in a Rainbow phase: the Chapter 4 computer screens' colours
// (palette_animation's 90-colour table 0xAB120) by the frame it appeared, so each one keeps its colour and the next one
// takes the next; 0 none (the palette entry's colour)
uint16_t magicianAfterimageItemWord(const Runtime& rt, uint32_t model, uint32_t task, bool pc = false);
// (pc: the PC models, whose afterimages are textured, always blue in the PC game: every colour the mod sets per item)
// whether a creature type gets a big head with the settings now (the mod on; Heads: the people; Bosses). Also the
// Gallery's (relfix: it follows the Mods page's Big Head)
bool bigHeadTypeOn(int type);
// the Big Head mod: each drawn head (a body part of class 1 in the part role table: the part whose look angles the
// game turns) and every part below it in the part tree scaled about the head's own pivot (its joint). The frame's
// draw list only (shadows too); call after the frame's other draw-list passes (Runtime::frame)
void bigHeadFrame(Runtime& rt);

} // namespace hotd::game
