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
};
RainbowBloodSettings& rainbowBloodSettings();
// rainbow2: the rainbow colour now (0..1 RGB) at hue offset `offset` (added to the phase; "Together" leaves it out
// unless `always`), the same as the renderer's (render/renderer.cpp uRainbowTable): the game's 90-colour table 0xAB120
// blended, a plain hue if it is missing. false: the mod is off (rgb untouched)
bool rainbowColour(const Runtime& rt, float offset, float rgb[3], bool always = false);
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
// whether a creature type gets a big head with the settings now (the mod on; Heads: the people; Bosses). Also the
// Gallery's (relfix: it follows the Mods page's Big Head)
bool bigHeadTypeOn(int type);
// the Big Head mod: each drawn head (a body part of class 1 in the part role table: the part whose look angles the
// game turns) and every part below it in the part tree scaled about the head's own pivot (its joint). The frame's
// draw list only (shadows too); call after the frame's other draw-list passes (Runtime::frame)
void bigHeadFrame(Runtime& rt);

} // namespace hotd::game
