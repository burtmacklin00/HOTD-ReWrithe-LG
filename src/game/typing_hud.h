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
    // alpha (totd6): Smooth Motion's blend of the picture between the last two ticks (1: the newest tick, no blend);
    // each box at its aim point before the tick plus alpha of its step (Typing::Label::dx)
    void draw(const Runtime& rt, const Typing& t, int ww, int wh,
              const std::function<void(float ax, float ay, float& px, float& py)>& toWindow, float alpha = 1.0f);
    void shutdown();
    // the label's picture (RGBA, w x h): the prompt in a box, the typed letters red, the next one underlined; wraps a
    // long prompt onto two lines. Public for the headless picture test (--totd-label-test)
    static std::vector<uint8_t> labelImage(const Typing::Label& l, int scale, int& w, int& h);
    // totd7: the boxes' one-pixel drop shadow (typing.ini box_shadow, HOTD_TOTD_SHADOW=1; off by default)
    static inline bool shadow = false;
    // totd7 (the report): boxes drawn, boxes whose place would have been past the picture's edge (now kept inside), boxes
    // still past it after that (must be 0)
    static inline uint64_t boxStats[3] = {};
    // totd8 (HOTD_TOTD_COVER=1): a creature's body (its hit spheres) hidden by its own box, by depth (near < 30, mid,
    // far > 70): boxes, the sum of their body shares hidden, boxes with a head, the sum of head shares, heads over half hidden
    static inline double cover[3][5] = {};
    // totd1 (user 2026-11-03: "use the game's own score font"): the arcade's 8 x 8 text font (the SCORE line's, the
    // boot's debug font: 128 ASCII glyphs at ROM 0x2EF3870, 4-bit pens, palette 0 at ROM 0x168A0), read once from the
    // game's memory. The prompts' letters come from it; a character it has no glyph for falls back to SDL's 8 x 8 font
    static void loadFont(const Runtime& rt);
    static bool hasGlyph(char c);
    // totd6: a line of HUD text in the game's font (no box), its letters in `col` with a dark outline (PERFECT nn)
    static std::vector<uint8_t> textImage(const std::string& s, const uint8_t col[3], int scale, int& w, int& h);

private:
    struct Slot {
        Overlay ov;
        std::string key;   // what the texture shows now
        bool ready = false;
        int w = 0, h = 0;  // totd5: its picture's size (the layout pass)
    };
    std::deque<Slot> slots_;
    Overlay line_, results_, perfect_;
    bool perfectReady_ = false;
    std::string perfectKey_;
    std::string resultsKey_;   // qa1: the results' picture (scale and lines) now shown
    bool lineReady_ = false, resultsReady_ = false;
    int lineMiss_ = -1;
    uint64_t busyAt_ = 0;   // totd4: the last tick with a prompt up (the typing line shows only in a fight, as TotD's HUD)
    Slot* slot(size_t i);
};

} // namespace hotd::game
