// The per-player HUD's game logic (proofs-5): the torches' state (0x51F5C0), the reload prompt's timer (player
// +0x40), the hit-notice popups (0x51F480) and their sounds. The cells themselves are written by the sprite routines
// (sprite_draw_anim 0x17E40 / sprite_draw 0x180B0: boundaries, the runtime's src/game/sprites.cpp). First written in
// the runtime (src/game/sprites.cpp, hud), moved here for their capture proofs. Arcade forms only (the PC forms are
// replayed). Declarations: decl/player.h, decl/support_b.h.
#include "port/game.h"

namespace hotd::port {

namespace hud_detail {
constexpr uint32_t kTileRam = 0x1000000, kTorches = 0x51F5C0, kPopups = 0x51F480;
} // namespace hud_detail

// 0x94740 hud_lives_draw (player task): the torches; a lit one flickers (6 frames a step, 7 steps, each torch its own
// phase from 0xCA6F0), a lost one burns out over 42 frames (8 steps), a new one flashes the base 0x14; debug mode 7
// prints the score (SCORE:%05d at row 4: the title code LLLRR, title_code_screen)
template <class P> void Game<P>::hud_lives_draw(uint32_t task)
{
    using namespace hud_detail;
    if constexpr (P::pc) {
        (void)task;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const int p = mem.template at<int8_t>(task + 0x20);
        const uint32_t pl = G::players + uint32_t(p) * 0xCC;
        const int n = mem.template at<uint8_t>(G::max_lives);
        for (int i = 0; i < n; ++i) {
            const uint32_t lit = kTorches + uint32_t(p) * 10 + uint32_t(i) * 2, burn = lit + 1;
            bool fresh = false;
            if (int32_t(mem.u32(pl + 0xC)) < i + 1) {
                if (mem.template at<int8_t>(lit) != 0) {
                    mem.template at<int8_t>(lit) = 0;
                    mem.template at<int8_t>(burn) = 0;
                }
            } else if (mem.template at<int8_t>(lit) == 0) {
                mem.template at<int8_t>(lit) = 1;
                fresh = true;
            }
            const int xo = 6 + 4 * i, xt = 0x34 - 4 * i;   // the torch's base place (player 1 / player 2)
            if (mem.template at<int8_t>(lit) == 0) {
                const int8_t b = mem.template at<int8_t>(burn);
                if (b < 42) {
                    const int step = int(int8_t((b / 6) % 8));
                    sprite_draw_anim(kTileRam, 1, 0, p == 0 ? xo : xt, 0x2A, uint32_t(step + (p == 0 ? 0xBE : 0xAF)));
                    mem.template at<int8_t>(burn) = int8_t(b + 1);
                } else {
                    sprite_draw(kTileRam, p == 0 ? xo : xt, 0x2A, p == 0 ? 0xBE : 0xAF);
                }
            } else {
                const uint32_t step = (mem.u32(0xCA6F0 + uint32_t(i) * 4) + mem.u32(G::frame_counter)) / 6 % 7;
                if (p == 0)
                    sprite_draw_anim(kTileRam, 1, 0, 4 * i + 7, 0x2A, step + 0xB7);
                else
                    sprite_draw_anim(kTileRam, 1, 0, 0x35 - 4 * i, 0x2A, step + 0xA8);
                if (fresh)
                    sprite_draw_anim(kTileRam, 1, 0, p == 0 ? 4 * i + 6 : 0x34 - 4 * i, 0x2B, 0x14);
            }
        }
        if (mem.u32(0x51F42C) == 7) {
            text_cursor(p * 0x1A + 0xE, 4);
            text_printf(0x94730, int32_t(mem.u32(pl + 8)));   // "SCORE:%05d" with the score (+8)
        }
    }
}

// 0x949F0 hud_ammo_draw (player task): the six bullets (player 1 from the left, player 2 from the right), RELOAD
// blinking (45 of 60 frames) and after 120 frames the region's "shoot outside the screen" line (table 0xCA700); its
// voice at the trigger
template <class P> void Game<P>::hud_ammo_draw(uint32_t task)
{
    using namespace hud_detail;
    if constexpr (P::pc) {
        (void)task;
    } else {
        const int p = mem.template at<int8_t>(task + 0x20);
        const uint32_t pl = G::players + uint32_t(p) * 0xCC;
        const uint8_t scene = mem.template at<uint8_t>(G::scene_state);
        // PC Mode (ReWrithe on the arcade logic; THOTD.EXE hud_ammo_draw 0x50B7E4): no bullets while the reload timer
        // runs, up to 12 of them
        if (scene != 3 && scene != 5 && !(pm_on() && pm_reload_at[p & 3] != 0)) {
            for (int i = 0; i < (pm_on() ? 12 : 6); ++i) {
                const int x = p == 0 ? 2 + 2 * i : 0x3B - 2 * i;
                if (int8_t(mem.template at<uint8_t>(pl + 0x3D)) <= i) {
                    sprite_draw(kTileRam, x, 0x26, 0x10);
                    break;
                }
                sprite_draw_anim(kTileRam, 1, 0, x, 0x26, 0x10);
            }
        }
        const int rx = p * 0x1F + 4;
        const uint32_t hint = mem.u32(0xCA700 + text_region() * 4);
        // Light Gun Edition (ours): the 4P mode: RELOAD right above the player's own bullets and torches (row 0x20), no
        // "shoot outside the screen" line, so the four players' prompts never stack in the middle (players 3 / 4's
        // sit under their bands at the top: Hud::drawSeatHud); 1-2 players: the arcade's rows 0x14 / 0x1B
        const bool lg4 = coop4_on();
        const int ry = lg4 ? 0x20 : 0x14;
        const int32_t lives = int32_t(mem.u32(pl + 0xC));
        int32_t timer = int32_t(mem.u32(pl + 0x40));
        if (mem.template at<uint8_t>(pl + 0x3E) != 0 && lives != 0 && scene != 3 && scene != 5 && mem.u32(G::shot_enable) != 0) {
            if (timer % 0x3C < 0x2D) {
                sprite_draw_anim(kTileRam, 1, 0, rx, ry, 0x36);
                if (timer > 0x77 && !lg4)
                    sprite_draw_anim(kTileRam, 1, 0, rx, 0x1B, hint);
            } else {
                sprite_draw(kTileRam, rx, ry, 0x36);
                if (timer > 0x77 && !lg4)
                    sprite_draw(kTileRam, rx, 0x1B, int(hint));
            }
            if (timer != 0 && mem.template at<uint8_t>(G::gun_inputs + uint32_t(p) * 0x1C + 0x15) != 0 &&
                mem.template at<uint8_t>(G::gun_flash_phase) == 2)
                sound_cue(timer < 0x78 ? 0xA01404 : 0xA01405);
            timer = int32_t(mem.u32(pl + 0x40));
            mem.set32(pl + 0x40, uint32_t(timer + 1));
            return;
        }
        if (timer != 0 || lives == 0 || scene == 3 || scene == 5) {
            mem.set32(pl + 0x40, 0);
            sprite_draw(kTileRam, rx, ry, 0x36);
            if (!lg4)
                sprite_draw(kTileRam, rx, 0x1B, int(hint));
        }
    }
}

// 0x94670 hud_player_start_draw (player task): the start prompt off, the player's frame (1P / 2P, table 0xCA6D0) and
// the lit torches' bases (their burn-out frames cleared)
template <class P> void Game<P>::hud_player_start_draw(uint32_t task)
{
    using namespace hud_detail;
    if constexpr (P::pc) {
        (void)task;
    } else {
        const int p = mem.template at<int8_t>(task + 0x20);
        const int step = p == 0 ? 4 : -4;
        hud_start_prompt(p, 0, 0);
        const uint32_t row = 0xCA6D0 + uint32_t(p) * 12;
        sprite_draw_anim(kTileRam, 1, 0, int(mem.u32(row)), 0x2A, mem.u32(row + 8));
        const int n = mem.template at<uint8_t>(G::max_lives);
        for (int i = 0, x = 0; i < n; ++i, x += step) {
            const uint32_t lit = kTorches + uint32_t(p) * 10 + uint32_t(i) * 2;
            if (mem.template at<int8_t>(lit) != 0) {
                sprite_draw_anim(kTileRam, 1, 0, int(mem.u32(row + 4)) + x, 0x2B, 0x14);
                mem.template at<int8_t>(lit + 1) = 0;
            }
        }
    }
}

// 0x94490 hud_popup_update (player task): the player's popup (hud_player_notice_show) taken off after its 60 frames
// or when the player count changed; its voice at frame 55 (rand: one of two)
template <class P> void Game<P>::hud_popup_update(uint32_t task)
{
    using namespace hud_detail;
    if constexpr (P::pc) {
        (void)task;
    } else {
        const int p = mem.template at<int8_t>(task + 0x20);
        const uint32_t r = kPopups + uint32_t(p) * 8;
        if (mem.template at<uint8_t>(r) != 0) {
            const int8_t t = mem.template at<int8_t>(r + 1);
            mem.template at<int8_t>(r + 1) = int8_t(t - 1);
            if (t == 1 || int32_t(mem.u32(r + 4)) < mem.template at<int16_t>(G::players_in_game_b)) {
                mem.template at<int8_t>(r + 1) = 0;
                const int kind = mem.template at<int8_t>(r + 3), who = int32_t(mem.u32(r + 4));
                const uint32_t pos = 0x94368 + uint32_t(kind * 16 + who * 8);
                const uint32_t sprite = mem.u32(0xCA5FC + uint32_t(kind * 8 + who * 4));
                mem.template at<uint8_t>(r) = 0;
                sprite_draw(0x1004000, mem.template at<int8_t>(r + 2) + int32_t(mem.u32(pos)), int32_t(mem.u32(pos + 4)),
                            int(sprite));
            }
        }
        if (mem.template at<int8_t>(r + 1) == '7') {
            const int32_t v = int32_t(rand());
            const bool odd = v % 2 != 0;
            if (p == 0)
                sound_cue(odd ? 0xA01455 : 0xA01456);
            else
                sound_cue(odd ? 0xA01458 : 0xA01459);
        }
    }
}

// 0x945D0 hud_player_notice_show (player task, n): once at a time per player, a 60-frame popup on layer 2 (the hit
// notice: sprite from 0xCA5FC by n and the players in game, place from 0x94368 + the player's offset 0x9441E) and its
// sound (0xCA660[n]); hud_popup_update takes it off
template <class P> void Game<P>::hud_player_notice_show(uint32_t task, int n)
{
    using namespace hud_detail;
    if constexpr (P::pc) {
        (void)task, (void)n;
    } else {
        const int p = mem.template at<int8_t>(task + 0x20);
        const uint32_t r = kPopups + uint32_t(p) * 8;
        if (mem.template at<uint8_t>(r) != 0)
            return;
        mem.template at<uint8_t>(r) = 1;
        const int who = mem.template at<int16_t>(G::players_in_game_b);
        mem.template at<int8_t>(r + 1) = 0x3C;
        mem.set32(r + 4, uint32_t(who));
        mem.template at<int8_t>(r + 3) = int8_t(n);
        const int8_t dx = mem.template at<int8_t>(0x9441E + uint32_t(p + who * 2));
        const uint32_t pos = 0x94368 + uint32_t(n * 16 + who * 8);
        mem.template at<int8_t>(r + 2) = dx;
        sprite_draw_anim(0x1004000, 1, 0, dx + int32_t(mem.u32(pos)), int32_t(mem.u32(pos + 4)),
                         mem.u32(0xCA5FC + uint32_t(n * 8 + who * 4)));
        sound_cue(mem.u32(0xCA660 + uint32_t(n) * 4));
    }
}

// 0x263D0 hud_sprites_load (chapter_load): the HUD's banks (frames, torches, bullets, RELOAD, the region's off-screen
// line, 0x107), the HUD frame 0xA2 on layer 1, the gun flash phase 0
template <class P> void Game<P>::hud_sprites_load()
{
    if constexpr (!P::pc) {
        sprite_bank_load(0x1A, 0xE);
        sprite_bank_load(0x1F, 0x14);
        sprite_bank_load(0x1D, 0xA8);
        sprite_bank_load(0x1E, 0xB7);
        sprite_bank_load(0x20, 0x10);
        sprite_bank_load(4, 0x28);
        sprite_bank_load(0x4C, 0x271);
        sprite_bank_load(0x37, 0x36);
        if (text_region() == 0)
            sprite_bank_load(0x38, 0x37);
        else
            sprite_bank_load(0x4F, 0x26D);
        sprite_bank_load(0x19, 0x107);
        sprite_draw_anim(0x1002000, 1, 0, 0, 0, 0xA2);
        mem.template at<uint8_t>(G::gun_flash_phase) = 0;
    }
}

// 0x94CC0 hud_player_full_draw (player task): the player's HUD taken off (frame, torches, bullets) for a cut-scene;
// debug mode 7 clears the score line
template <class P> void Game<P>::hud_player_full_draw(uint32_t task)
{
    using namespace hud_detail;
    if constexpr (P::pc) {
        (void)task;
    } else {
        const int p = mem.template at<int8_t>(task + 0x20);
        const int n = mem.template at<uint8_t>(G::max_lives);
        if (p == 0) {
            sprite_draw(kTileRam, 2, 0x2A, 0xE);
            for (int i = 0; i < n; ++i) {
                sprite_draw(kTileRam, 6 + 4 * i, 0x2B, 0x14);
                sprite_draw(kTileRam, 7 + 4 * i, 0x2A, 0xB7);
            }
            // (PC Mode: up to 12 bullets, hud_ammo_draw; the arcade's six cleared, bullets 7-12 stayed over the
            // cut-scenes: user 2026-10-26, Sophie's "bullet icons stretch over in cut-scenes")
            if (mem.u32(G::ammo_enabled) != 0)
                for (int x = 2; x <= (pm_on() ? 24 : 12); x += 2)
                    sprite_draw(kTileRam, x, 0x26, 0x10);
        } else {
            sprite_draw(kTileRam, 0x39, 0x2A, 0xF);
            for (int i = 0; i < n; ++i) {
                sprite_draw(kTileRam, 0x34 - 4 * i, 0x2B, 0x14);
                sprite_draw(kTileRam, 0x35 - 4 * i, 0x2A, 0xA8);
            }
            if (mem.u32(G::ammo_enabled) != 0)
                for (int x = 0x3B; x >= (pm_on() ? 0x25 : 0x31); x -= 2)
                    sprite_draw(kTileRam, x, 0x26, 0x10);
        }
        if (mem.u32(0x51F42C) == 7) {
            text_cursor(p * 0x1A + 0xE, 4);
            text_printf(0x94CB0);   // "           " (11 spaces: the score line cleared)
        }
    }
}

// 0x94F40 hud_sprite107_blink (player task): sprite 0x107 at the player's corner, 45 of every 60 frames
template <class P> void Game<P>::hud_sprite107_blink(uint32_t task)
{
    using namespace hud_detail;
    if constexpr (P::pc) {
        (void)task;
    } else {
        const int x = mem.template at<int8_t>(task + 0x20) == 0 ? 1 : 0x29;
        if (mem.u32(G::frame_counter) % 0x3C < 0x2D)
            sprite_draw_anim(kTileRam, 1, 0, x, 0x2B, 0x107);
        else
            sprite_draw(kTileRam, x, 0x2B, 0x107);
    }
}

// 0x94FD0 hud_sprite107_draw (player task): sprite 0x107 taken off
template <class P> void Game<P>::hud_sprite107_draw(uint32_t task)
{
    using namespace hud_detail;
    if constexpr (P::pc)
        (void)task;
    else
        sprite_draw(kTileRam, mem.template at<int8_t>(task + 0x20) == 0 ? 1 : 0x29, 0x2B, 0x107);
}

// 0x209A0 hud_start_prompt (player, which, on): the prompt record 0x51E8A0 + player x 8 + which x 4 {s16 on, s16 was};
// when it was or is on, the prompt routine of table 0xAA450 by the credits (0x51F604; 0x51F608 for a player out of
// lives; free play 0x51FDB8 = 1: the first) at the place of table 0xAA440, told whether it is on
template <class P> void Game<P>::hud_start_prompt(int p, int which, int on)
{
    if constexpr (P::pc) {
        (void)p, (void)which, (void)on;
    } else {
        const uint32_t r = 0x51E8A0 + uint32_t(p * 8 + which * 4);
        const int16_t was = mem.template at<int16_t>(r);
        mem.template at<int16_t>(r) = int16_t(on);
        mem.template at<int16_t>(r + 2) = was;
        uint32_t state = was != 0 ? 1 : 0;
        if (int16_t(on) != 0)
            state |= 2;
        if (state == 0)
            return;
        uint32_t k;
        if (mem.u32(G::players + uint32_t(p) * 0xCC) == 4)
            k = 1 < int32_t(mem.u32(0x51F608)) ? 2 : 0;
        else
            k = 1 < int32_t(mem.u32(0x51F604)) ? 1 : 0;
        if (mem.u32(0x51FDB8) == 1)
            k = 0;
        hud_prompt_anim(mem.u32(0xAA450 + k * 4), mem.template at<int8_t>(0xAA440 + uint32_t((p != 0 ? 1 : 0) + which * 3)),
                        mem.template at<int8_t>(0xAA442 + uint32_t(which * 3)), p, state != 1 ? 1 : 0);
    }
}

// --- continue / game over (the flow's player states call these): the player's place from table 0xCA714 (x, y) ---
// Light Gun Edition (ours): in the 4P mode players 1 / 2's CONTINUE? / GAME OVER sit right above their own bands at the
// bottom corners (as players 3 / 4's at the top: Hud::drawSeatHud), not in the middle where all four stacked
namespace hud_lg {
template <class G> int contX(const G& g, int p)
{
    return g.coop4_on() && p < 2 ? (p == 0 ? 2 : 0x24) : int(g.mem.template at<int8_t>(0xCA714 + uint32_t(p) * 2));
}
template <class G> int contY(const G& g, int p)
{
    return g.coop4_on() && p < 2 ? 0x1F : int(g.mem.template at<int8_t>(0xCA715 + uint32_t(p) * 2));
}
}

// 0x95010 hud_continue_sprite (player): CONTINUE? (0x113). PC Mode (THOTD.EXE 0x50BC24): only when the player
// can_continue (0x45B8F0: a credit left, or Free-Play); without one the countdown's digit runs alone
template <class P> void Game<P>::hud_continue_sprite(int p)
{
    if (pm_on() && !pm_can_continue())
        return;
    sprite_place(0x1000000, 1, 0, hud_lg::contX(*this, p), hud_lg::contY(*this, p), 0x113);
}

// 0x95040 hud_continue_digit (player, digit): the countdown digit (table 0xCA720)
template <class P> void Game<P>::hud_continue_digit(int p, int digit)
{
    sprite_place(0x1000000, 1, 0, hud_lg::contX(*this, p) + 0x16, hud_lg::contY(*this, p),
                 mem.template at<int16_t>(0xCA720 + uint32_t(digit) * 2));
}

// 0x95080 hud_continue_label (player): CONTINUE? and its digit taken off, bank 8 freed
template <class P> void Game<P>::hud_continue_label(int p)
{
    const int x = hud_lg::contX(*this, p), y = hud_lg::contY(*this, p);
    sprite_draw(0x1000000, x, y, 0x113);
    sprite_draw(0x1000000, x + 0x16, y, 0x114);
    sprite_bank_free(8);
}

// 0x950E0 hud_game_over_sprite (player): GAME OVER (0xA5) at the player's place
template <class P> void Game<P>::hud_game_over_sprite(int p)
{
    sprite_place(0x1000000, 1, 0, hud_lg::contX(*this, p), hud_lg::contY(*this, p), 0xA5);
}

// 0x95110 hud_game_over_sprite_final (player): taken off, bank 0x12 freed
template <class P> void Game<P>::hud_game_over_sprite_final(int p)
{
    sprite_draw(0x1000000, hud_lg::contX(*this, p), hud_lg::contY(*this, p), 0xA5);
    sprite_bank_free(0x12);
}

// 0x26260 continue_screen_draw: the centre CONTINUE (0x108) and its digit taken off, bank 7 freed
template <class P> void Game<P>::continue_screen_draw()
{
    sprite_draw(0x1000000, 6, 0, 0x108);
    sprite_draw(0x1000000, 0x2B, 0xF, 0x109);
    sprite_bank_free(7);
}

// --- the boss life bar (0x543530: shown (scene_init_flag), +1 blinking out, +2 x, +4 y, +6 maximum, +0xA width,
// +0xC blink state, +0xE the blink phase seen, +0x10 blink frames) ---

// 0x6BA90 life_bar_init (x, y, maximum)
template <class P> void Game<P>::life_bar_init(int x, int y, int maximum)
{
    if constexpr (P::pc) {
        (void)x, (void)y, (void)maximum;
    } else {
        mem.template at<uint8_t>(0x543530) = 1;
        mem.template at<uint8_t>(0x543531) = 0;
        mem.template at<int16_t>(0x54353A) = 0;
        mem.template at<int16_t>(0x54353E) = int16_t(mem.u32(G::blink_phase) - 1);
        mem.template at<int16_t>(0x543532) = int16_t(x);
        mem.template at<int16_t>(0x543534) = int16_t(y);
        mem.template at<int16_t>(0x543536) = int16_t(maximum);
        sprite_bank_load(1, 0xE4);
    }
}

// 0x6BAF0 life_bar_draw (value): 38 cells; the width 0x130 x value / maximum eased 3 a frame; each cell's sprite from
// the table 0xC6830 by the width left (0x104 empty, 0xE4 full)
template <class P> void Game<P>::life_bar_draw(int32_t value)
{
    const int32_t maximum = mem.template at<int16_t>(0x543536);   // (0: the original divides by zero; not reached)
    const int32_t want = maximum != 0 ? value * 0x130 / maximum : 0;
    const int32_t w = mem.template at<int16_t>(0x54353A);
    int32_t nw;
    if (w < want)
        nw = w + 3 <= want ? w + 3 : want;
    else
        nw = (want < w && want <= w - 3) ? w - 3 : (want < w ? want : w);
    mem.template at<int16_t>(0x54353A) = int16_t(nw);
    const int x0 = mem.template at<int16_t>(0x543532), y = mem.template at<int16_t>(0x543534) + 1;
    int32_t ptr = int32_t(0xC6830) + int32_t(int16_t(nw)) * 2;
    for (int i = 1; i < 0x27; ++i, ptr -= 16) {
        uint32_t s;
        if (ptr >= 0xC683F)
            s = 0xE4;
        else if (ptr < 0xC6830)
            s = 0x104;
        else
            s = uint32_t(int32_t(mem.template at<int16_t>(uint32_t(ptr))));
        sprite_draw_anim(0x1004000, 1, 0, i + x0, y, s);
    }
}

// 0x6BC40 life_bar_update (value, finish): once a frame (blink_phase changed): the frame 0x105 and the bar; after
// `finish` 60 frames of blinking, then the bank freed and -1
template <class P> int32_t Game<P>::life_bar_update(int32_t value, int finish)
{
    if constexpr (P::pc) {
        (void)finish;
        return value;
    } else {
        if (mem.template at<uint8_t>(0x543530) == 0 || mem.template at<int16_t>(0x54353E) == int16_t(mem.u32(G::blink_phase)))
            return value;
        mem.template at<int16_t>(0x54353E) = int16_t(mem.u32(G::blink_phase));
        const int x = mem.template at<int16_t>(0x543532), y = mem.template at<int16_t>(0x543534);
        if (mem.template at<uint8_t>(0x543531) == 0) {
            sprite_draw_anim(0x1004000, 1, 0, x, y, 0x105);
            life_bar_draw(value);
            if (finish != 0) {
                mem.template at<uint8_t>(0x543531) = 1;
                mem.template at<int16_t>(0x54353C) = 0;
            }
            return 0;
        }
        const int16_t state = mem.template at<int16_t>(0x54353C);
        if (state == 0) {
            mem.template at<int16_t>(0x543540) = 0x3C;
            mem.template at<int16_t>(0x54353C) = 1;
        } else if (state != 1) {
            return value;
        }
        const int16_t frames = mem.template at<int16_t>(0x543540);
        const int16_t left = int16_t(frames - 1);
        mem.template at<int16_t>(0x543540) = left;
        if (frames == 1) {
            sprite_bank_free(1);
            mem.template at<uint8_t>(0x543530) = 0;
            return -1;
        }
        if ((uint8_t(left) >> 1) & 1) {
            sprite_draw_anim(0x1004000, 1, 0, x, y, 0x105);
            life_bar_draw(value);
            return value;
        }
        sprite_draw(0x1004000, x, y, 0x105);
        return value;
    }
}

} // namespace hotd::port
