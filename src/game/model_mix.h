// mixmodels (GitHub #49 "mix and match", user 2026-11-12): Options > Video > Models: Mixed. Each of five model categories (and the
// HUD) of what the game draws takes its own set, the arcade's (Model 2) or the 1998 PC release's:
//   Players and Humans: Rogan, G, Sophie, the researchers / hostages, Dr. Curien, PC Mode's extra characters
//   Creatures: the zombies and every other enemy (creature types the table calls zombies)
//   Bosses: the Chariot, the Hangedman, the Hermit, the Magician (their bodies, the parts they draw themselves, their
//           own effects: the Chariot's landing dust, the Magician's part flames and death flash)
//   World: the stage, the sky, the props and doors the game draws, the PC's later world files; also the full-screen
//          2D screens and the fades (Runtime::pcModels is the World row in Mixed): the attract / title, the chapter
//          cards, GAME OVER and the route map, the chapter clear's wall, the name entry, the staff roll's cards
//   Effects: blood, the shot flash and sparks, the hit marks, splashes and the other effect models
//   HUD (user 2026-11-12): the in-play 2D over the picture (the bullets, the life torches, the scores and their font,
//          the messages: RELOAD, PRESS START, CONTINUE, the boss bars, the 4P seats' HUD, Survival / Horde lines, the
//          crosshairs' Auto style): the PC sprites or the arcade's tiles (Runtime::hudPc)
// A PC row whose object has no model in the PC files (the "PC model gap": the cut creatures, Sophie lying 0x621 ...)
// draws the arcade's model there (GameView), so nothing goes missing. Arcade and PC (not Mixed) are as before.
#pragma once

#include "assets/arcade_categories.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace hotd::game {

// (kMixHud: the user 2026-11-12, a row of its own: the in-play HUD's 2D; not a model row, GameView never picks it)
enum MixCat : int { kMixHumans = 0, kMixCreatures, kMixBosses, kMixWorld, kMixEffects, kMixHud, kMixCats };
constexpr uint8_t kMixAll = (1u << kMixCats) - 1;

struct ModelMix {
    bool on = false;   // Models: Mixed (with the PC files there)
    uint8_t pc = 0;    // bit c (MixCat): that row is PC
    // Mixed Models > Arcade Extras (user 2026-11-13): the PC rows' arcade fallback also draws the two kinds the real PC
    // leaves out (the bodies' shadows, the Chariot's armor pieces); off: as the 1998 PC (GameView)
    bool extras = true;
    bool pcFor(int c) const { return ((pc >> c) & 1u) != 0; }
};

// a creature type's row (the 94-type table, assets/arcade_categories.cpp: humans, bosses, the rest creatures); the
// runtime's own Sophie strip key (Runtime::kSophieStrip, past the table) is Sophie's
inline int mixCatOfType(int type)
{
    if (type < 0 || type >= 94)
        return type == 134 ? kMixHumans : kMixCreatures;   // (134: Runtime::kSophieStrip)
    const Category c = CreatureCategories::ofType(type);
    return c == Category::Humans ? kMixHumans : c == Category::Bosses ? kMixBosses : kMixCreatures;
}

// launcher.ini mixedmodels= / --mixed-pc: the rows on PC as a comma list of these names ("none": every row arcade)
inline const char* mixCatKey(int c)
{
    static const char* const k[kMixCats] = {"humans", "creatures", "bosses", "world", "effects", "hud"};
    return c >= 0 && c < kMixCats ? k[c] : "";
}
inline std::string mixedModelsKey(int pc)
{
    std::string s;
    for (int c = 0; c < kMixCats; ++c)
        if ((pc >> c) & 1)
            s += (s.empty() ? "" : ",") + std::string(mixCatKey(c));
    return s.empty() ? "none" : s;
}
inline int mixedModelsParse(const std::string& v)
{
    int pc = 0;
    size_t at = 0;
    while (at <= v.size()) {
        const size_t comma = std::min(v.find(',', at), v.size());
        const std::string w = v.substr(at, comma - at);
        for (int c = 0; c < kMixCats; ++c)
            if (w == mixCatKey(c) || w == "all")
                pc |= 1 << c;
        at = comma + 1;
    }
    return pc & kMixAll;
}

} // namespace hotd::game
