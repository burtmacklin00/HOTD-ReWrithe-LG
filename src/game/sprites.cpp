// The arcade's 2D layer as the original drives it (docs/formats.md §31, src/assets/sprites_arcade.h): sprite banks
// (0x17930..0x17D50: tiles into character RAM, palettes into the shadow 0x515CA0), the sprite routines that write
// tilemap cells (0x17D80..0x18110), the HUD routines (0x94430..0x95110, 0x20760.., 0x263D0), the boss life bar
// (0x6BA90..0x6BC40), the subtitles (0x9C450 / 0x9C610), the window masks (0x18600 / 0x18630). The runtime maps tile
// RAM (0x1000000) and character RAM (0x1080000) into the arcade address space, so these routines write where the
// original writes; the HUD composes those RAMs each frame (Hud, Arcade2D::compose).
// Read from the typed decompilation / disassembly: UNVERIFIED (teaser), no capture proof, except the bank routines
// (src/port/sprite_banks.cpp) and the per-player HUD state (src/port/hud_state.cpp), moved to the port and proven (proofs-5).
#include "game/runtime.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace hotd::game {

using G = A::G;
using port::a::Player;

namespace {
constexpr uint32_t kTileRam = 0x1000000, kCharRam = 0x1080000;
constexpr uint32_t kBanks = 0x5123F0;     // 0x80 records of 16 bytes: +0 first slot (-1: not loaded), +4 slots,
                                          // +8 pieces (s16), +0xA locked (s16), +0xC the tiles' source
constexpr uint32_t kSlots = 0x512C00;     // 0x80 bytes: the character RAM slot (128 tiles) is used
constexpr uint32_t kSlotsFixed = 0x512BF0;   // slots kept by sprite_slots_mark (the font and the fixed banks)
constexpr uint32_t kSheets = 0xA6590;     // 12 bytes a sprite: tiles, palettes, layout {u16 w, u16 h, w x h cells}
constexpr uint32_t kPalShadow = 0x515CA0; // 256 palettes of 16 RGB555 words
constexpr uint32_t kHudCounter = 0x51F560;   // coin_update's counter (the prompts blink by bit 5)
constexpr uint32_t kGlyphs = 0x9C250;     // tile_text_draw: sprite of each character (0 = none)
constexpr uint32_t kDebugMode = 0x51F42C;
} // namespace

uint16_t& Runtime::cell(uint32_t layer, int x, int y) { return mem.at<uint16_t>(layer + uint32_t(y * 0x80 + x * 2)); }

uint32_t Runtime::sheet_layout(uint32_t sprite) { return mem.u32(kSheets + sprite * 12 + 8); }

// the PC's sprite-ID layers (sprites_pc.h): a sprite clears the cells it covers and sits in its top-left cell
void Runtime::pc_cells_put(uint32_t layer, int x, int y, uint32_t sprite, int w, int h, uint32_t clip)
{
    const uint32_t l = (layer - kTileRam) >> 13;
    // pcrank: the ranking's rank cards (ranking_table_task: sprite 0x231 + card at row y + table 0xAA518[card], down to
    // y - 25) are tall sprites with the picture in their lowest rows, drawn bottom row first: each card's empty upper
    // rows cover the cells of the cards below it. On the arcade those cells only lose their (empty) top tiles; here a
    // card sat in its top-left cell (often above the screen: not stored at all) and the next card's clear took it. A
    // card now sits in its first picture row (the layout's first row with a cell not 0) as a clipped draw of the PC
    // picture's rest and covers only those rows
    int top = 0;
    if (sprite >= 0x231 && sprite <= 0x238 && clip == 0 && w > 0 && layer >= kTileRam && l < 4) {
        const uint32_t lay = sheet_layout(sprite);
        for (bool empty = true; empty && top < h - 1;) {
            for (int c = 0; c < w && empty; ++c)
                empty = mem.at<uint16_t>(lay + 4 + uint32_t(top * w + c) * 2) == 0;
            if (empty)
                ++top;
        }
    }
    // (any sprite) a top above the screen: from row 0, its rows above skipped
    top = std::max(top, std::min(-y, h - 1));
    if (top > 0 && clip == 0) {
        y += top;
        h -= top;
        clip = uint32_t(top & 0xFF) << 8 | uint32_t(w & 0xFF) << 16 | uint32_t(h & 0xFF) << 24;
    }
    pc_cells_clear(layer, x, y, w, h);
    if (layer >= kTileRam && l < 4) {
        const uint8_t side = uint8_t(hud_side_of(sprite, x, w));
        // (rainbow2; HOLD YOUR FIRE! 0x107 is a warning, not a player's HUD)
        const uint8_t kind = hud_player_sprite(sprite) && sprite != 0x107 ? 1 : hit_mark_sprite(sprite) ? 2 : 0;
        for (int r = std::max(y, 0); r < std::min(y + h, 64); ++r)
            for (int c = std::max(x, 0); c < std::min(x + w, 64); ++c) {
                // a subtitle / text cell stays centred (a HUD sprite's transparent cells over it must not take it)
                const bool text = l == 0 && cellText[size_t(r) * 64 + size_t(c)];
                cellSide[(l * 64 + uint32_t(r)) * 64 + uint32_t(c)] = text ? uint8_t(0) : side;
                cellHud[(l * 64 + uint32_t(r)) * 64 + uint32_t(c)] = text ? uint8_t(0) : kind;
            }
    }
    if (layer >= kTileRam && l < 4 && x >= 0 && x < 64 && y >= 0 && y < kPcRows) {
        const size_t i = (l * kPcRows + uint32_t(y)) * 64 + uint32_t(x);
        pcCells[i] = sprite | 0x10000;
        pcClip[i] = clip;
    }
}

// display2: the per-player HUD sprites (hud_state.cpp's routines; the original lays them out for its 4:3 screen at the
// corners / per side) anchor to their side when wholly in one half of the 62-cell screen; everything else is centred.
// UNVERIFIED (teaser): the set is read from the routines, not from a list in the original
int Runtime::hud_side_of(uint32_t sprite, int x, int w)
{
    if (!hud_player_sprite(sprite))
        return 0;
    if (x + w <= 31)
        return 1;
    if (x >= 31)
        return 2;
    return 0;
}

// the per-player HUD sprites (hud_side_of; rainbow2: Rainbow Blood's HUD option tints them, Runtime::cellHud)
bool Runtime::hud_player_sprite(uint32_t sprite)
{
    bool perPlayer = sprite == 0xE || sprite == 0xF || sprite == 0x10 || sprite == 0x14 ||   // frames, bullets, torch base
                     (sprite >= 0xA8 && sprite <= 0xC5) ||                                   // torches (lit / burning out)
                     sprite == 0x36 || sprite == 0x40 || sprite == 0x107 ||                  // RELOAD, PRESS START, 0x107
                     sprite == 0x113 || sprite == 0x114 || sprite == 0xA5;                   // CONTINUE?, GAME OVER
    // the hit marks 0x28..0x2D stay centred: big splashes over the play area, not corner HUD
    if (!perPlayer && mem.mapped(0xCA700)) {
        for (uint32_t k = 0; k < 4 && !perPlayer; ++k)
            perPlayer = mem.u32(0xCA700 + k * 4) == sprite;   // the "shoot outside the screen" hint by region
        for (uint32_t d = 0; d < 10 && !perPlayer; ++d)
            perPlayer = uint32_t(mem.at<int16_t>(0xCA720 + d * 2)) == sprite;   // the continue digits
    }
    return perPlayer;
}

// issues5 (GitHub #56): the hit marks are every sprite of hud_player_notice_show's table 0xCA5FC (kind x 2 players),
// not only 0x28..0x2D: the scratches and slashes 0x271..0x275 are there too (the table's words read: 0, 0x28..0x2D
// for kinds 0-6, 0x271..0x275 for kinds 7-11, then other data)
bool Runtime::hit_mark_sprite(uint32_t sprite)
{
    return (sprite >= 0x28 && sprite <= 0x2D) || (sprite >= 0x271 && sprite <= 0x275);
}

void Runtime::pc_cells_clear(uint32_t layer, int x, int y, int w, int h)
{
    const uint32_t l = (layer - kTileRam) >> 13;
    if (layer < kTileRam || l >= 4)
        return;
    for (int r = std::max(y, 0); r < std::min(y + h, kPcRows); ++r)
        for (int c = std::max(x, 0); c < std::min(x + w, 64); ++c)
            pcCells[(l * kPcRows + uint32_t(r)) * 64 + uint32_t(c)] = 0;
}

// the first call of any 2D routine: the RAMs mapped, the boot's 2D set-up (sprite_init 0x170C0, sprite_banks_init
// 0x17930), then chapter_load's 2D part (0x25DA0: chapter_screen_reset, sprite_slots_mark, the region's banks,
// hud_sprites_load, hud_mode_init). The work RAM snapshot's bank records describe a character RAM the runtime does
// not have, so everything is loaded again. (chstart: a chapter start runs the boot part only, sprites_boot2d; the
// chapter part is then chapter_load's own, the port's)
void Runtime::sprites_ensure()
{
    if (sprites2d_)
        return;
    sprites2d_ = true;
    sprites_boot2d();
    if (mem.at<int16_t>(G::game_mode) != 3 && mem.at<int16_t>(G::game_mode) != 8) {
        chapter_screen_reset();
        sprite_slots_mark();
    }
    sprites_cover_lift();
    const int chapter = mem.at<int8_t>(G::chapter);
    if (text_region() == 0) {
        sprite_bank_load(0x27, 0x127);
        if (chapter != 4) {
            const uint32_t s = mem.u32(0xAB240 + uint32_t(chapter) * 4);
            sprite_bank_load(mem.at<int16_t>(G::sprite_bank_of + s * 2), int(s));
        }
    } else if (chapter != 4) {
        sprite_bank_load(0x2E, 0x1E5);
    }
    hud_sprites_load();
    // hud_mode_init (the prompt records of table 0xAA1E0 are the attract / coin prompts: not drawn in the teaser)
    mem.set32(kHudCounter, 0);
    mem.set32(0x51E8A0, 0);
    mem.set32(0x51E8A8, 0);
}

// the RAMs mapped, the boot's 2D set-up: sprite_init (the debug font (128 glyphs) into slot 0, the text layers
// cleared), sprite_banks_init
void Runtime::sprites_boot2d()
{
    if (!mem.mapped(kTileRam))
        mem.map(kTileRam, 0x10000);
    if (!mem.mapped(kCharRam))
        mem.map(kCharRam, 0x80000);
    for (uint32_t i = 0; i < 0x80 * 32; ++i)
        mem.at<uint8_t>(kCharRam + i) = mem.at<uint8_t>(0x2EF3870 + i);
    // scoredisp: palette_init 0x16AB0's shadow part (boot_hw_init): palette 0 = ROM 0x168A0 (0x8000 black, 0xFFFF white,
    // greys 0xA94A): the debug font's colours (the title code's SCORE line; MAME: the same 16 words in play). The rest of
    // palette_init writes the palette RAM directly (the text colours, two ROM palettes), which the port does not use
    for (uint32_t i = 0; i < 16; ++i)
        mem.at<uint16_t>(kPalShadow + i * 2) = mem.at<uint16_t>(0x168A0 + i * 2);
    text_layers_clear();
    sprite_banks_init();
}

// hangcmp (2026-10-19): a game started past event 0 (--event N, developer): the arcade always starts a game at
// event 0, whose script lifts chapter_screen_reset's black cover (scnin_clear_sprites_now 0x67C40). Started
// elsewhere the cover stayed over the 3D (headless --render shots black but for the holes the boss titles
// cleared); lifted here as that op does. Not in the arcade (the attract demo's mode 3 places no cover)
void Runtime::sprites_cover_lift()
{
    if (coverOff_) {
        coverOff_ = false;
        tilemap_clear_rect(0x1004000, 0, 0, 0x3E, 0x30);
    }
}

// once a frame (the main loop's counters the 2D routines read: 0x80B0's blink_phase, coin_update's 0x51F560)
void Runtime::sprites_frame()
{
    sprites_ensure();
    cardLatchFlush();
    // debugging: HOTD_DUMP_2D=<frame> writes tile RAM, character RAM and the palette shadow at that frame (cwd)
    static const char* dumpAt = std::getenv("HOTD_DUMP_2D");
    if (dumpAt && frameCount == uint64_t(std::atoll(dumpAt))) {
        if (FILE* f = std::fopen("dump2d.bin", "wb")) {
            std::fwrite(mem.ptr(kTileRam), 1, 0x10000, f);
            std::fwrite(mem.ptr(kCharRam), 1, 0x80000, f);
            std::fwrite(mem.ptr(kPalShadow), 1, 0x2000, f);
            std::fwrite(mem.ptr(kBanks), 1, 0x800, f);
            std::fclose(f);
        }
    }
    mem.set32(G::blink_phase, mem.u32(G::blink_phase) + 1);
    mem.set32(kHudCounter, mem.u32(kHudCounter) + 1);
    coin_credits_seen();
    // the light gun's screen flash (0x46D50 gun_flash, its picture only: the window mask of layers 0 / 1 filled for 4
    // frames on a shot, showing the grey screen 0xA2 of layer 1). The runtime's gun stand-in keeps gun_flash_phase
    // at 2 for the shot logic, so the flash keeps its own counter. Off unless asked (--shot-flash): a strobe on
    // every shot is hard on the eyes with a mouse. PC Mode: no new flash (the PC port has none: player_shoot 0x4365C4
    // fires on the pull's frame without flash phases; docs/pcmode.md row 5).
    if (shotFlash) {
        // a shot fired last frame: a round less than before (player_shoot's flag is cleared again within the frame)
        bool shot = false;
        for (int p = 0; p < 2; ++p) {
            const int r = mem.at<Player>(G::players, uint32_t(p)).rounds;
            shot |= r < flashRounds_[p];
            flashRounds_[p] = r;
        }
        const bool allowed = mem.at<int16_t>(G::game_mode) == 4 && mem.at<uint8_t>(G::scene_state) != 4;
        if (flash_ == 0 && shot && allowed && !pm_on()) {
            gun_flash_on(0x100C000);
            flash_ = 4;
        } else if (flash_ > 0 && --flash_ == 0) {
            gun_flash_off(0x100C000);
        }
    }
}

// 0x171B0 text_layers_clear: the scroll shadow, the four tilemaps, the line scroll tables, the masks, the registers
void Runtime::text_layers_clear()
{
    std::fill(pcCells.begin(), pcCells.end(), 0u);
    std::fill(cellSide.begin(), cellSide.end(), uint8_t(0));
    std::fill(cellHud.begin(), cellHud.end(), uint8_t(0));
    std::fill(cellText.begin(), cellText.end(), uint8_t(0));
    pcTexts.clear();
    cardLatch_.clear();
    for (uint32_t a = 0x51E8F4; a < 0x51E904; a += 2)
        mem.at<uint16_t>(a) = 0;
    for (uint32_t i = 0; i < 0x8000; i += 2)
        mem.at<uint16_t>(kTileRam + i) = 0;
    for (uint32_t i = 0; i < 0x2000; i += 2)
        mem.at<uint16_t>(0x100C000 + i) = 0;
    for (uint32_t i = 0; i < 0x1000; i += 2)
        mem.at<uint16_t>(0x1008000 + i) = 0;
    for (uint32_t i = 0; i < 0x10; i += 2)
        mem.at<uint16_t>(0x100A000 + i) = 0;
}

// 0x17930 sprite_banks_init, 0x17A10 sprite_slots_mark, 0x17AA0 sprite_bank_size, 0x17AF0 sprite_slots_free, 0x17B20 sprite_bank_alloc,
// 0x17BD0 sprite_bank_free, 0x17C10 sprite_bank_palette_load, 0x17CB0 sprite_bank_tiles_load, 0x17D50
// sprite_bank_load: the port's (src/port/sprite_banks.cpp, proofs-5); the runtime maps the 2D RAMs first

// 0x17CB0: not with no free slot (the original would write below character RAM)
void Runtime::sprite_bank_tiles_load(uint32_t rec, int sheet)
{
    if (int32_t(mem.u32(rec)) < 0) {
        if (log)
            log("sprite_bank_load: no free slots for a bank (record 0x" + std::to_string(rec) + ")");
        return;
    }
    port::Game<A>::sprite_bank_tiles_load(rec, sheet);
}

void Runtime::sprite_bank_load(int bank, int sheet)
{
    sprites_ensure();
    port::Game<A>::sprite_bank_load(bank, sheet);
}

void Runtime::sprite_bank_free(int bank)
{
    sprites_ensure();
    port::Game<A>::sprite_bank_free(bank);
}

// the cells of sprite_place / sprite_draw_anim / sprite_draw_tinted: slot x 0x80 + b x 0x4000 - a x 0x8000 + cell
void Runtime::sprite_cells(uint32_t layer, int a, int b, int x, int y, uint32_t sprite, bool skipZero)
{
    const uint32_t lay = sheet_layout(sprite);
    const int w = mem.at<uint16_t>(lay), h = mem.at<uint16_t>(lay + 2);
    const int32_t slot = int32_t(mem.u32(kBanks + uint32_t(int32_t(mem.at<int16_t>(G::sprite_bank_of + sprite * 2))) * 16));
    const uint16_t base = uint16_t(int16_t(slot) * 0x80 + b * 0x4000 - a * 0x8000);
    uint32_t src = lay + 4;
    static const bool trace = std::getenv("HOTD_TRACE_SPRITES") != nullptr;   // debugging: each sprite newly shown
    if (trace && w > 0 && h > 0 && cell(layer, x, y) != uint16_t(base + mem.at<uint16_t>(src)))
        std::fprintf(stderr, "frame %llu sprite 0x%X layer %X at %d,%d\n", (unsigned long long)frameCount, sprite, layer, x, y);
    pc_cells_put(layer, x, y, sprite, w, h);
    if (layer == 0x1002000)
        cardLatchFlush();
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c, src += 2) {
            const uint16_t v = mem.at<uint16_t>(src);
            if (!skipZero || v != 0)
                cell(layer, x + c, y + r) = uint16_t(base + v);
        }
}

// 0x17D80 sprite_place (layer, a, b, x, y, sprite): loads the sprite's bank, then draws it
void Runtime::sprite_place(uint32_t layer, int a, int b, int x, int y, int sprite)
{
    sprite_bank_load(mem.at<int16_t>(G::sprite_bank_of + uint32_t(sprite) * 2), sprite);
    sprite_cells(layer, a, b, x, y, uint32_t(sprite), false);
}

// 0x17E40 sprite_draw_anim: the cells (the bank is loaded already)
void Runtime::sprite_draw_anim(uint32_t layer, int a, int b, int x, int y, uint32_t sprite)
{
    sprites_ensure();
    sprite_cells(layer, a, b, x, y, sprite, false);
}

// 0x17EE0 sprite_draw_tinted: the same without its empty cells
void Runtime::sprite_draw_tinted(uint32_t layer, int a, int b, int x, int y, uint32_t sprite)
{
    sprites_ensure();
    sprite_cells(layer, a, b, x, y, sprite, true);
}

// 0x17F80 sprite_draw_clipped (layer, a, b, x, y, clip x0, x1, y0, y1, sprite)
void Runtime::sprite_draw_clipped(uint32_t layer, int a, int b, int x, int y, int cx0, int cx1, int cy0, int cy1, uint32_t sprite)
{
    sprites_ensure();
    const uint32_t lay = sheet_layout(sprite);
    const int w = mem.at<uint16_t>(lay);
    int rows = mem.at<uint16_t>(lay + 2);
    const int32_t slot = int32_t(mem.u32(kBanks + uint32_t(int32_t(mem.at<int16_t>(G::sprite_bank_of + sprite * 2))) * 16));
    const uint16_t base = uint16_t(int16_t(slot) * 0x80 + b * 0x4000 - a * 0x8000);
    uint32_t src = lay + 4;
    int skipL = 0, skipR = 0, cw = w;
    if (x < cx0) {
        skipL = cx0 - x;
        cw = w - skipL;
        x = cx0;
    }
    if (cx1 < x + cw - 1) {
        skipR = x + cw - (cx1 + 1);
        cw -= skipR;
    }
    int skipT = 0;
    if (y < cy0) {
        const int d = cy0 - y;
        skipT = d;
        y = cy0;
        rows -= d;
        src += uint32_t(d * w * 2);
    }
    if (cy1 < rows + y - 1)
        rows -= rows + y - (cy1 + 1);
    // the PC picture: only the part the arcade's cells show (pcpolish2: the route map's lower parts, records 0xC1790)
    const bool whole = skipL == 0 && skipT == 0 && cw == w && rows == mem.at<uint16_t>(lay + 2);
    if (cw > 0 && rows > 0)
        pc_cells_put(layer, x, y, sprite, cw, rows,
                     whole ? 0u : uint32_t(skipL & 0xFF) | uint32_t(skipT & 0xFF) << 8 | uint32_t(cw & 0xFF) << 16 | uint32_t(rows & 0xFF) << 24);
    // polish2: the chapter card's film flicker (kind177_chapter_card_frame 0x9B1C0) writes the lower half (rows
    // 24-47) on even frames and, after waiting for the raster (the beam already past the upper half), the upper half
    // (rows 0-23) on odd frames with the next even frame's picture. On the Model 2 that upper write is only seen from
    // the next frame on, together with the lower half's: one clean picture every 2 frames (MAME snaps 0010 / 0174).
    // Drawn at once (the frame's end state), every odd frame showed the two halves of different pictures (a seam
    // across the card's middle). So the upper half's cells reach tile RAM at the next frame's start (or before a
    // later draw on the same layer in this frame: the card's end). UNVERIFIED (teaser): the latch is the display's.
    const bool latch = layer == 0x1002000 && cy0 == 0 && cy1 == 23 && sprite >= 0x7D && sprite <= 0x7F;
    for (int r = 0; r < rows; ++r) {
        src += uint32_t(skipL * 2);
        for (int c = 0; c < cw; ++c, src += 2) {
            const uint16_t v = uint16_t(base + mem.at<uint16_t>(src));
            if (latch)
                cardLatch_.push_back({&cell(layer, x + c, y + r), v});
            else
                cell(layer, x + c, y + r) = v;
        }
        src += uint32_t(skipR * 2);
    }
}

// polish2: the chapter card's deferred upper-half cells (sprite_draw_clipped)
void Runtime::cardLatchFlush()
{
    for (const auto& [p, v] : cardLatch_)
        *p = v;
    cardLatch_.clear();
}

// 0x180B0 sprite_draw (layer, x, y, sprite): clears the sprite's cells (the name is the original's call order: the
// routine takes a sprite off)
void Runtime::sprite_draw(uint32_t layer, int x, int y, int sprite)
{
    sprites_ensure();
    if (layer == 0x1002000)
        cardLatchFlush();
    const uint32_t lay = sheet_layout(uint32_t(sprite));
    const int w = mem.at<uint16_t>(lay), h = mem.at<uint16_t>(lay + 2);
    pc_cells_clear(layer, x, y, w, h);
    if (layer == kTileRam)
        for (int r = std::max(y, 0); r < std::min(y + h, 64); ++r)
            for (int c = std::max(x, 0); c < std::min(x + w, 64); ++c)
                cellText[size_t(r) * 64 + size_t(c)] = 0;
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            cell(layer, x + c, y + r) = 0;
}

// 0x18110 tilemap_clear_rect (layer, x, y, w, h)
void Runtime::tilemap_clear_rect(uint32_t layer, int x, int y, int w, int h)
{
    sprites_ensure();
    if (layer == 0x1002000)
        cardLatchFlush();
    pc_cells_clear(layer, x, y, w, h);
    if (layer == kTileRam)
        for (int r = std::max(y, 0); r < std::min(y + h, 64); ++r)
            for (int c = std::max(x, 0); c < std::min(x + w, 64); ++c)
                cellText[size_t(r) * 64 + size_t(c)] = 0;
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            cell(layer, x + c, y + r) = 0;
}

// 0x18600 / 0x18630: the window mask of a layer pair filled / cleared (0xC00 bytes: 384 lines): the window layer
// (1 or 3) shows over the whole screen (the shot flash's white screen, the title's frames)
void Runtime::gun_flash_on(uint32_t layer)
{
    sprites_ensure();
    for (uint32_t i = 0; i < 0xC00; i += 4)
        mem.set32(layer + i, 0xFFFFFFFFu);
}

void Runtime::gun_flash_off(uint32_t layer)
{
    sprites_ensure();
    for (uint32_t i = 0; i < 0xC00; i += 4)
        mem.set32(layer + i, 0);
}

// 0x9C450 tile_text_draw (x, y, text): each character's sprite (table 0x9C250) on layer 0, one cell further a
// character, over the 3D picture, with the palette of bank 0x2E's slot (the font sheet 0x1E5)
void Runtime::tile_text_draw(int x, int y, uint32_t text)
{
    if (boss_next_on() || survival.leadIn())   // Boss Mode's undrawn walk-up: THOTD.EXE's tile_text_draw does nothing while pc_boss_next (Survival's lead-in as it)
        return;
    sprites_ensure();
    const uint16_t base = uint16_t(int16_t(mem.u32(kBanks + 0x2E * 16)) * 0x80 + 0x8000);
    pcTexts[{x, y}] = reinterpret_cast<const char*>(mem.ptr(text));
    for (int i = 0; mem.at<uint8_t>(text + uint32_t(i)) != 0 && x + i < 64; ++i)
        if (x + i >= 0 && y >= 0 && y < 63) {   // display2: centred text (both rows of the glyphs)
            for (uint32_t r = 0; r < 2; ++r) {
                cellSide[(uint32_t(y) + r) * 64 + uint32_t(x + i)] = 0;
                cellText[(uint32_t(y) + r) * 64 + uint32_t(x + i)] = 1;
            }
        }
    if (std::getenv("HOTD_TRACE_SPRITES"))
        std::fprintf(stderr, "frame %llu text at %d,%d: %s\n", (unsigned long long)frameCount, x, y,
                     reinterpret_cast<const char*>(mem.ptr(text)));
    for (int i = 0; mem.at<uint8_t>(text) != 0; ++text, ++i) {
        const uint32_t spr = mem.u32(kGlyphs + uint32_t(mem.at<uint8_t>(text)) * 4);
        if (spr == 0)
            continue;
        const uint32_t lay = sheet_layout(spr);
        const int w = mem.at<uint16_t>(lay), h = mem.at<uint16_t>(lay + 2);
        uint32_t src = lay + 4;
        for (int r = 0; r < h; ++r)
            for (int c = 0; c < w; ++c, src += 2)
                cell(kTileRam, x + i + c, y + r) = uint16_t(mem.at<uint16_t>(src) + base);
    }
}

// 0x9C610 voice_message_frame_sub: the same cells cleared
void Runtime::voice_message_frame_sub(int x, int y, uint32_t text)
{
    sprites_ensure();
    pcTexts.erase({x, y});
    for (int i = 0; mem.at<uint8_t>(text) != 0; ++text, ++i) {
        if (x + i >= 0 && x + i < 64 && y >= 0 && y < 63)
            for (uint32_t r = 0; r < 2; ++r)
                cellText[(uint32_t(y) + r) * 64 + uint32_t(x + i)] = 0;
        const uint32_t lay = sheet_layout(mem.u32(kGlyphs + uint32_t(mem.at<uint8_t>(text)) * 4));
        const int w = mem.at<uint16_t>(lay), h = mem.at<uint16_t>(lay + 2);
        for (int r = 0; r < h; ++r)
            for (int c = 0; c < w; ++c)
                cell(kTileRam, x + i + c, y + r) = 0;
    }
}

// 0x17660 text_cursor (column, row): the debug text's cursor (0x51F770 / 0x51F774) and its line start (0x51F9E8)
void Runtime::text_cursor(int column, int row)
{
    mem.set32(0x51F9E8, uint32_t(column));
    mem.set32(0x51F770, uint32_t(column));
    mem.set32(0x51F774, uint32_t(row));
}

// 0xA4EC0 text_printf -> text_vprintf 0xA4F50 -> text_putc 0x17710 (scoredisp, 2026-10-26): each character a cell of
// layer 0 at the cursor (column + row x 64), the tile = the character (the debug font sprite_init put in slot 0) |
// the colour 0x5123E0 | 0x8000 (priority), the cursor one column on (up to column 0x3E); '\n' to the line start's
// column one row down, '\t' to the next multiple of 8 (text_control_char 0x176A0). Only the formats the port draws:
// the HUD's score line (hud_lives_draw "SCORE:%05d" 0x94730, hud_player_full_draw's 11 spaces 0x94CB0, the title code
// LLLRR); the developers' error texts (the cameras' "not defined", the heap's, the guards') stay unprinted, as before
// (the original stops after them; the port goes on). The formatter: %d with a 0 flag / width, %%
void Runtime::text_printf(uint32_t format, int32_t arg)
{
    if (format != 0x94730 && format != 0x94CB0)
        return;
    std::string s;
    for (uint32_t a = format; mem.at<uint8_t>(a) != 0 && s.size() < 64; ++a) {
        const char c = char(mem.at<uint8_t>(a));
        if (c != '%') { s += c; continue; }
        const bool zero = mem.at<uint8_t>(a + 1) == '0';
        if (zero) ++a;
        int width = 0;
        while (mem.at<uint8_t>(a + 1) >= '0' && mem.at<uint8_t>(a + 1) <= '9')
            width = width * 10 + (mem.at<uint8_t>(++a) - '0');
        const char conv = char(mem.at<uint8_t>(++a));
        if (conv == 'd') {
            std::string d = std::to_string(arg < 0 ? -int64_t(arg) : int64_t(arg));
            while (int(d.size()) + (arg < 0 ? 1 : 0) < width)
                d.insert(d.begin(), zero ? '0' : ' ');
            s += (arg < 0 ? "-" : "") + d;
        } else {
            s += conv;   // (%%; nothing else in these formats)
        }
    }
    int32_t& x = mem.at<int32_t>(0x51F770);
    int32_t& y = mem.at<int32_t>(0x51F774);
    const uint16_t colour = uint16_t(mem.u32(0x5123E0));
    for (const char ch : s) {
        const uint8_t c = uint8_t(ch);
        if (c < 0x20) {   // text_control_char 0x176A0
            if (c == '\t') {
                x = (x + 8) & ~7;
                if (x > 0x3D) { x = 0; if (y < 0x2F) ++y; }
            } else if (c == '\n') {
                x = int32_t(mem.u32(0x51F9E8));
                if (y < 0x2F) ++y;
            }
            continue;
        }
        if (x >= 0 && x < 64 && y >= 0 && y < 64)
            cell(kTileRam, x, y) = uint16_t(c | colour | 0x8000);
        if (x < 0x3E)
            ++x;
    }
}

// 0x17280 chapter_screen_reset: the black cover 0xA1 on layer 2, layer 0 cleared, unlocked records free
void Runtime::chapter_screen_reset()
{
    sprite_place(0x1004000, 1, 0, 0, 0, 0xA1);
    tilemap_clear_rect(kTileRam, 0, 0, 0x3E, 0x30);
    mem.set32(0x5123E0, 0);
    for (uint32_t a : {0x5123E4u, 0x5123E6u, 0x5123E8u, 0x5123EAu})
        mem.at<uint16_t>(a) = 0;
    mem.set32(0x51F770, 0);
    mem.set32(0x51F774, 0);
    for (uint32_t b = 0; b < 0x80; ++b)
        if (mem.at<int16_t>(kBanks + b * 16 + 10) == 0)
            mem.set32(kBanks + b * 16, 0xFFFFFFFFu);
}

// 0x263D0 hud_sprites_load: the port's (src/port/hud_state.cpp, proofs-5)

// --- the per-player HUD (player_in_game_frame 0x50B0 calls these) ---

// 0x94740 hud_lives_draw, 0x949F0 hud_ammo_draw: the port's (src/port/hud_state.cpp, proofs-5)
void Runtime::hud_lives_draw(uint32_t task)
{
    sprites_ensure();
    port::Game<A>::hud_lives_draw(task);
    // menus3 (user 2026-10-28: "place it somewhere like in the remake"): the score line (the title code / Gameplay >
    // Score Display: the arcade prints SCORE:%05d at the top, row 4, column 0xE / 0x28) moved to the player's own HUD
    // corner, the row above the bullets (player 1 from the bullets' left edge, player 2 ending at their right edge),
    // and anchored to that side in widescreen as the rest of the player's HUD (cellSide). Ours: the arcade has it at
    // the top; HOTD_SCORE_TOP=1 keeps it there
    static const bool top = std::getenv("HOTD_SCORE_TOP") != nullptr;
    if (mem.u32(0x51F42C) == 7 && !top) {
        const int p = mem.at<int8_t>(task + 0x20);
        if (p == 0 || p == 1) {
            constexpr int kLen = 11, kRow = 0x24;   // "SCORE:00000"; the bullets are at row 0x26
            const int ox = p * 0x1A + 0xE, nx = p == 0 ? 2 : 0x3D - kLen;
            for (int i = 0; i < kLen; ++i) {
                uint16_t& from = cell(kTileRam, ox + i, 4);
                const uint16_t v = from;
                from = 0;
                cell(kTileRam, nx + i, kRow) = v;
                cellSide[size_t(kRow) * 64 + size_t(nx + i)] = uint8_t(p + 1);
            }
        }
    }
}

// relplay (user 2026-10-30: Score Display "hide SCORE during cut-scenes"): the moved score line (hud_lives_draw above,
// row 0x24) is printed only in play (scene state 2) and its cells stayed: it showed through every cut-scene, and on the
// continue screen as a torn "SCOR" (that screen clears part of the row). Hidden there, as the bullets and torches: a
// player's line only while that player plays and the scene is play; hud_lives_draw prints it again next time
void Runtime::score_line_hide()
{
    static const bool top = std::getenv("HOTD_SCORE_TOP") != nullptr;
    if (top || mem.u32(kDebugMode) != 7 || !mem.mapped(kTileRam))
        return;
    const bool play = mem.at<uint8_t>(G::scene_state) == 2;
    for (int p = 0; p < 2; ++p) {
        if (play && mem.at<Player>(G::players, uint32_t(p)).state == 5)
            continue;
        constexpr int kLen = 11, kRow = 0x24;
        const int nx = p == 0 ? 2 : 0x3D - kLen;
        static const char kWord[] = "SCOR";   // (the continue screen leaves this much of it)
        bool ours = true;
        for (int i = 0; i < 4 && ours; ++i) {
            const uint16_t v = cell(kTileRam, nx + i, kRow);
            ours = (v & 0x8000) && (v & 0xFF) == uint8_t(kWord[i]);
        }
        if (!ours)
            continue;
        for (int i = 0; i < kLen; ++i)
            if (uint16_t& v = cell(kTileRam, nx + i, kRow); v & 0x8000)   // (the line's characters only)
                v = 0;
    }
}

void Runtime::hud_ammo_draw(uint32_t task)
{
    sprites_ensure();
    port::Game<A>::hud_ammo_draw(task);
}

// 0x94CC0 hud_player_full_draw, 0x94F40 hud_sprite107_blink, 0x94FD0 hud_sprite107_draw: the port's (hud_state.cpp)

// 0x94670 hud_player_start_draw, 0x94490 hud_popup_update, 0x945D0 hud_player_notice_show: the port's
// (src/port/hud_state.cpp, proofs-5)
void Runtime::hud_player_start_draw(uint32_t task)
{
    sprites_ensure();
    port::Game<A>::hud_player_start_draw(task);
}

void Runtime::hud_popup_update(uint32_t task)
{
    sprites_ensure();
    port::Game<A>::hud_popup_update(task);
}

void Runtime::hud_player_notice_show(uint32_t task, int n)
{
    sprites_ensure();
    if (std::getenv("HOTD_TRACE_SPRITES"))
        std::fprintf(stderr, "frame %llu notice %d (player %d, popup %d)\n", (unsigned long long)frameCount, n,
                     mem.at<int8_t>(task + 0x20), mem.at<uint8_t>(0x51F480 + uint32_t(mem.at<int8_t>(task + 0x20)) * 8));
    port::Game<A>::hud_player_notice_show(task, n);
}

// 0x209A0 hud_start_prompt: the port's (src/port/hud_state.cpp, proofs-5)
void Runtime::hud_start_prompt(int p, int which, int on)
{
    sprites_ensure();
    if ((seatsHeldOut >> (p & 7)) & 1)   // (Survival, survrevive: a seat out until the next round has no PRESS START)
        on = 0;
    port::Game<A>::hud_start_prompt(p, which, on);
}

// the prompt routines of table 0xAA450 (x, y, player, on): the teaser runs in free play, so each is drawn as
// hud_prompt_anim_a 0x20C50: on: PRESS START (0x40, 0x20760 hud_start_text_draw; no credit count in free play) by
// coin_update's counter (bit 5, one phase in three off); off: the place cleared (hud_sprite_clear). The coin modes'
// other prompt cycles (0x20870, 0x20CB0 / 0x20D80) need credits. UNVERIFIED (teaser)
void Runtime::hud_prompt_anim(uint32_t, int x, int y, int, int on)
{
    const int phase = on == 0 || noStartPrompt ? 2 : int((mem.u32(kHudCounter) >> 5) % 3);   // (totd5: none in TotD 0)
    if (phase == 2) {
        tilemap_clear_rect(kTileRam, x, y, 0x13, 4);   // hud_sprite_clear
        return;
    }
    sprite_draw_anim(kTileRam, 1, 0, x + 1, y, 0x40);   // hud_sprite_put
}

// --- continue / game over (the flow's player states call these) ---


// 0x95010 hud_continue_sprite, 0x95040 hud_continue_digit, 0x95080 hud_continue_label, 0x950E0 hud_game_over_sprite,
// 0x95110 hud_game_over_sprite_final, 0x26260 continue_screen_draw: the port's (src/port/hud_state.cpp, proofs-5)

// --- the boss life bar (0x6BA90..0x6BC40; the Chariot's chariot_life_bar_task, the Hermit's, 0x52C30) ---

// 0x6BA90 life_bar_init, 0x6BAF0 life_bar_draw, 0x6BC40 life_bar_update: the port's (src/port/hud_state.cpp,
// proofs-5)

} // namespace hotd::game
