// palanim (overnight 2026-10-16): the arcade's own palette animation in the game. Read from the typed decompilation
// (palette_animation 0x25310, unused_24e90 0x24E90, rgb555_unpack / rgb555_pack 0x24F40 / 0x24F80; formats.md §24).
// The same maths as the viewer's ArcadePaletteFx (verified there against 2630 MAME palette dumps), here as the game's
// state: work RAM 0x52066C..0x5206E0 written from the frame counter, the chapter byte and the event flags, then copied
// into the polygon palette entries at the vblank. 0x25310 is the port's now (src/port/palette.cpp, capture-proven, proofs-4); the copy 0x24E90 stays UNVERIFIED (teaser); see docs/teaser_status/render.md
// "palanim" for the checks against the MAME dumps.
#include "game/runtime.h"
#include "game/cosmods.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace hotd::game {

using G = A::G;

// 0x25310 palette_animation: moved to the port (src/port/palette.cpp, proofs-4, capture-proven: docs/proof.md)

// 0x24E90 (the vblank's polygon palette copy) UNVERIFIED (teaser)
void Runtime::palette_anim_copy()
{
    port::Game<A>::palette_anim_copy();   // palette RAM bank 2 (arcfix2: palette_grey_build copies it into work RAM)
    uint16_t next[1024] = {};
    // work RAM 0x520664 + 2n -> entry 1021 - n (n = 0..37), skipping 0x520668, 0x520688, 0x52068A
    for (uint32_t n = 0; n < 38; ++n) {
        const uint32_t a = 0x520664 + 2 * n;
        if (a == 0x520668 || a == 0x520688 || a == 0x52068A) continue;
        next[1021 - n] = mem.at<uint16_t>(a);
    }
    if (mem.u32(G::texture_area) == 9) next[689] = 0x9854;
    next[211] = 0xEBDF;
    rainbowTableCache(*this);   // (v1.3.1: the computer screens' colours for the menus' rainbow blood)
    // GitHub #54: the Magician's colour word (entry 583: his afterimages blue as he flies, red as he swoops / lunges),
    // from his first write while his area stays loaded (the renderer draws the asset palettes, not palette RAM)
    if (magicianColourArea_ != 0xFFFFFFFFu && mem.u32(G::texture_area) == magicianColourArea_ && mem.mapped(0x180248E))
        next[583] = magicianAfterimageWord(uint16_t(mem.at<uint16_t>(0x180248E) | 0x8000u), frameCount);   // (the mod's colours)
    next[959] = mem.at<uint16_t>(0x5206E0);
    // a zero word: work RAM the port never wrote (0x520664 / 0x520666 / 0x52066A come from 0x24D70 at the area palette
    // load, not ported): the loaded value stays. The routine's own colours always carry 0x8000 (rgb555_pack).
    // HOTD_PALANIM_LOG=<file>: one line per frame, "<frame counter> <chapter> <entry 959> <entries 984..1021>" (hex), the
    // counter of the palette_animation call these values come from (checked by tools\palanim_check.py)
    static FILE* logFile = [] { const char* p = std::getenv("HOTD_PALANIM_LOG"); return p ? std::fopen(p, "w") : nullptr; }();
    if (logFile) {
        std::fprintf(logFile, "%u %u %04x", mem.u32(G::frame_counter), unsigned(mem.at<uint8_t>(G::chapter)), next[959]);
        for (int e = 984; e <= 1021; ++e) std::fprintf(logFile, " %04x", next[e]);
        std::fputc('\n', logFile);
    }
    if (!std::equal(next, next + 1024, polyPaletteAnim.entries)) {
        std::copy(next, next + 1024, polyPaletteAnim.entries);
        ++polyPaletteAnim.serial;
    }
}

} // namespace hotd::game
