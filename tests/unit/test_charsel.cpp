// charsel4 (user 2026-11-09): whose look and sound a seat takes (Game::look_seat): the seat's own (P1 red, P2 blue, P3
// green, P4 yellow) except Rogan and G, "the legacy characters", who keep player 1's / player 2's in any seat; only with
// the seats' characters drawn (PC Mode or a Character Select pick), so the arcade's own games and the proofs keep the
// seat. The gunshot cue follows it (0xA01208 player 1 / 3, 0xA01207 player 2 / 4). The label recolour
// (player_colours.h rankRecolour) takes the other label's own shades
#include "check.h"

#include "game/player_colours.h"
#include "game/runtime.h"

#include <memory>

using namespace hotd;

namespace {

using GameA = port::Game<port::A>;
constexpr int kRogan = 1, kG = 2, kRogan2 = 3, kG2 = 4, kSophie = 5, kResearcher = 6, kNoPlayer = 0;
constexpr uint32_t kShotP1 = 0xA01208, kShotP2 = 0xA01207;

struct World {
    port::Mem mem;
    std::vector<uint32_t> copro = std::vector<uint32_t>(0x8000, 0);
    std::unique_ptr<port::Tgp> tgp;
    std::unique_ptr<game::Runtime> rt;
    World()
    {
        mem.map(port::Mem::kRam, port::Mem::kRamSize);
        tgp = std::make_unique<port::Tgp>(copro.data(), copro.size());
        rt = std::make_unique<game::Runtime>(mem, *tgp);
    }
    void picks(int a, int b, int c, int d)
    {
        rt->pm_char[0] = a, rt->pm_char[1] = b, rt->pm_char[2] = c, rt->pm_char[3] = d;
    }
};

} // namespace

TEST(charsel, legacy_rule_table)
{
    for (int seat = 0; seat < 4; ++seat) {
        CHECK_EQ(GameA::look_seat_of(seat, kRogan), 0);   // Rogan: player 1's in any seat
        CHECK_EQ(GameA::look_seat_of(seat, kG), 1);       // G: player 2's in any seat
        for (int ch : {kNoPlayer, kRogan2, kG2, kSophie, kResearcher, 7, 12, 19})
            CHECK_EQ(GameA::look_seat_of(seat, ch), seat);   // everyone else: the seat's own
    }
    CHECK_EQ(GameA::shot_cue_of(0), kShotP1);
    CHECK_EQ(GameA::shot_cue_of(1), kShotP2);
    CHECK_EQ(GameA::shot_cue_of(2), kShotP1);   // (player 3: player 1's gunshot, as before)
    CHECK_EQ(GameA::shot_cue_of(3), kShotP2);
}

TEST(charsel, arcade_games_keep_the_seat)
{
    // no PC Mode and no pick: the seat, whatever pm_char holds (its default {1, 0, 1, 2}: Rogan in seat 3, G in seat 4)
    World w;
    w.rt->pcmode = 0, w.rt->charpick = 0;
    w.picks(kG, kRogan, kRogan, kG);
    for (int p = 0; p < 4; ++p) {
        CHECK_EQ(w.rt->look_seat(p), p);
        CHECK_EQ(w.rt->shot_cue(p), (p & 1) == 0 ? kShotP1 : kShotP2);
    }
}

TEST(charsel, character_select_picks)
{
    World w;
    w.rt->charpick = 1;
    // the user's examples: G solo in seat 1 = blue + G's gunshot; Rogan in seat 2 = red + Rogan's gunshot; Sophie in
    // seat 1 = red (the seat)
    w.picks(kG, kG, kRogan2, kG2);
    CHECK_EQ(w.rt->look_seat(0), 1);
    CHECK_EQ(w.rt->shot_cue(0), kShotP2);
    w.picks(kSophie, kRogan, kRogan2, kG2);
    CHECK_EQ(w.rt->look_seat(0), 0);
    CHECK_EQ(w.rt->shot_cue(0), kShotP1);
    CHECK_EQ(w.rt->look_seat(1), 0);
    CHECK_EQ(w.rt->shot_cue(1), kShotP1);
    // both players G: both blue
    w.picks(kG, kG, kRogan2, kG2);
    CHECK_EQ(w.rt->look_seat(0), 1);
    CHECK_EQ(w.rt->look_seat(1), 1);
    // seats 3 / 4: a legacy character takes its own colour (not green / yellow), the others the seat's
    w.picks(kSophie, kResearcher, kG, kRogan);
    CHECK_EQ(w.rt->look_seat(2), 1);
    CHECK_EQ(w.rt->look_seat(3), 0);
    CHECK_EQ(w.rt->shot_cue(2), kShotP2);
    CHECK_EQ(w.rt->shot_cue(3), kShotP1);
    w.picks(kSophie, kResearcher, kRogan2, kG2);
    for (int p = 0; p < 4; ++p)
        CHECK_EQ(w.rt->look_seat(p), p);
    // the unpicked seats' defaults (main.cpp: k + 1: Rogan, G, Rogan 2, G 2) keep the seat
    w.picks(1, 2, 3, 4);
    for (int p = 0; p < 4; ++p)
        CHECK_EQ(w.rt->look_seat(p), p);
}

TEST(charsel, pc_mode_defaults_unchanged)
{
    World w;
    w.rt->pcmode = 1;
    // PC Mode's defaults: P1 Rogan, P2 G (or No Player until joining), 4 panels Rogan, G, Rogan 2, G 2: the seats' own
    w.picks(kRogan, kG, kRogan2, kG2);
    for (int p = 0; p < 4; ++p)
        CHECK_EQ(w.rt->look_seat(p), p);
    w.picks(kRogan, kNoPlayer, kRogan2, kG2);
    CHECK_EQ(w.rt->look_seat(1), 1);
    // swapped: G in seat 1, Rogan in seat 2
    w.picks(kG, kRogan, kRogan2, kG2);
    CHECK_EQ(w.rt->look_seat(0), 1);
    CHECK_EQ(w.rt->look_seat(1), 0);
    CHECK_EQ(w.rt->shot_cue(0), kShotP2);
    CHECK_EQ(w.rt->shot_cue(1), kShotP1);
}

TEST(charsel, online_same_on_both_pcs)
{
    // the picks are synced (pm_char, charpick): two runtimes with the same picks compute the same looks / cues
    World a, b;
    for (int mode = 0; mode < 3; ++mode)
        for (int c0 = 0; c0 < 8; ++c0)
            for (int c1 = 0; c1 < 8; ++c1) {
                for (World* w : {&a, &b}) {
                    w->rt->pcmode = mode == 1, w->rt->charpick = mode == 2;
                    w->picks(c0, c1, (c0 + 3) % 8, (c1 + 5) % 8);
                }
                for (int p = 0; p < 4; ++p) {
                    CHECK_EQ(a.rt->look_seat(p), b.rt->look_seat(p));
                    CHECK_EQ(a.rt->shot_cue(p), b.rt->shot_cue(p));
                }
            }
}

TEST(charsel, label_rank_recolour)
{
    // a 4 x 1 "label" of player 1's art: a dark rim (neutral), two reds of different brightness, a white core
    uint8_t px[4 * 4] = {20, 20, 20, 255, 120, 10, 10, 255, 250, 40, 30, 255, 255, 255, 255, 255};
    // player 2's label's coloured texels (blue) and one neutral one that must not be used
    const uint8_t ref[3 * 4] = {30, 60, 200, 255, 200, 200, 200, 255, 10, 20, 90, 255};
    CHECK(game::artTexelColoured(px + 4, 0));
    CHECK(!game::artTexelColoured(px + 0, 0));
    CHECK(!game::artTexelColoured(px + 12, 0));
    CHECK(game::artTexelColoured(ref + 0, 1));
    CHECK(!game::artTexelColoured(ref + 4, 1));
    CHECK(game::rankRecolour(px, 4, 1, 0, 0, 4, 1, 0, ref, 3, 1));
    // the darker red takes the darker blue, the brighter red the brighter blue; rim and core stay
    CHECK_EQ(int(px[4]), 10);
    CHECK_EQ(int(px[6]), 90);
    CHECK_EQ(int(px[8]), 30);
    CHECK_EQ(int(px[10]), 200);
    CHECK_EQ(int(px[0]), 20);
    CHECK_EQ(int(px[12]), 255);
    // again: nothing red is left (the arcade pass may run it on the same picture twice)
    CHECK(!game::rankRecolour(px, 4, 1, 0, 0, 4, 1, 0, ref, 3, 1));
}
