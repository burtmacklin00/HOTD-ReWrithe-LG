// The Typing of the Dead 0's HUD (game/typing.h): the prompt boxes over the enemies, the typing line (combo,
// accuracy, WPM) and the chapter's typing results. Placeholder art (SDL's 8 x 8 font in drawn boxes) until the user's
// art comes (docs/typing.md "Assets": the box frames, the font, the typewriter, the results panel).
#pragma once

#include "game/typing.h"
#include "render/overlay.h"

#include <functional>
#include <string>
#include <deque>
#include <vector>

namespace hotd::game {

class TypingHud {
public:
    // toWindow: arcade screen units (centre 0 / 0, y up) -> window pixels (the view's fromArcade and the window scale)
    void draw(const Runtime& rt, const Typing& t, int ww, int wh,
              const std::function<void(float ax, float ay, float& px, float& py)>& toWindow);
    void shutdown();
    // the label's picture (RGBA, w x h): the prompt in a box, the typed letters red, the next one underlined; wraps a
    // long prompt onto two lines. Public for the headless picture test (--totd-label-test)
    static std::vector<uint8_t> labelImage(const Typing::Label& l, int scale, int& w, int& h);
    // totd1 (user 2026-11-03: "use the game's own score font"): the arcade's 8 x 8 text font (the SCORE line's, the
    // boot's debug font: 128 ASCII glyphs at ROM 0x2EF3870, 4-bit pens, palette 0 at ROM 0x168A0), read once from the
    // game's memory. The prompts' letters come from it; a character it has no glyph for falls back to SDL's 8 x 8 font
    static void loadFont(const Runtime& rt);
    static bool hasGlyph(char c);

private:
    struct Slot {
        Overlay ov;
        std::string key;   // what the texture shows now
        bool ready = false;
    };
    std::deque<Slot> slots_;
    Overlay line_, results_;
    bool lineReady_ = false, resultsReady_ = false;
    int lineMiss_ = -1;
    Slot* slot(size_t i);
};

} // namespace hotd::game
