// The ranking's name entry (docs/teaser_status/title-2.md): mode 4's sub-modes 9 / 10 after the ending (game_ending
// 0x352A0 -> rank_insert 0x5E5D0 -> game_name_entry_start 0x5D500 / game_name_entry 0x5D6D0, task group 0xC5470) and
// the top 10 in backup RAM 0x1D00104 (12 bytes an entry: name, rank card, score), kept in a file like the arcade's
// NVRAM. Read from the typed decompilation and the disassembly; UNVERIFIED (teaser), no capture proof.
//
// The arcade asks for a name only after the ending (rank_insert's one caller is game_ending); a game over goes to
// the ranking (mode 5 -> 7 in table 0xA9630) without one. The teaser ends after Chapter 1: its clear stands in for
// the ending (the players' rank cards from score_rank_get 0x9BE20 as kind179_chapter_results sets them).
#include "game/runtime.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace hotd::game {

using G = A::G;
using port::a::Player;

namespace {
constexpr uint32_t kTable = 0x1D00104;      // backup RAM: the top 10, 12 bytes an entry
constexpr uint32_t kEntry = 0x520AF0;       // per player 12 bytes: aim x, aim y (float), +8 slot, +9 rank, +0xA lock
constexpr uint32_t kDone = 0x543416;        // name_entry_done
constexpr uint32_t kTimer = 0x543414;       // name_entry_timer (u16)
constexpr uint32_t kGroup = 0x543410;
constexpr uint32_t kLight = 0x543418;

uint32_t fbits(float v)
{
    uint32_t u;
    std::memcpy(&u, &v, 4);
    return u;
}

} // namespace

// 0x9BE20 score_rank_get(player, table = players at the results): the rank card, 0 = best (also the ending's,
// ending.cpp)
int32_t score_rank(int32_t score, int table)
{
    static const int32_t t1[] = {62000, 55000, 49000, 43000, 37000, 31000, 25000};
    static const int32_t t2[] = {62000, 48000, 37000, 31000, 25000, 19000, 13000};
    const int32_t* t = table == 1 ? t1 : t2;
    for (int i = 0; i < 7; ++i)
        if (score >= t[i])
            return i;
    return 7;
}

// backup_ranking_check / backup_ranking_reset (0x22550 / 0x225C0): the top 10 from the ROM's defaults (0xAA4A0),
// then the saved table (the arcade's backup RAM; here rankingFile)
void Runtime::ranking_backup_ensure()
{
    if (mem.mapped(0x1D00000))
        return;
    mem.map(0x1D00000, 0x10000);
    for (uint32_t i = 0; i < 0x78; ++i)
        mem.at<uint8_t>(kTable + i) = mem.at<uint8_t>(0xAA4A0 + i);
    ranking_read_file();
    if (rankingLowTest)   // --ranking-low (testing, misc17): every score 0, so a headless game reaches the name entry
        for (uint32_t e = 0; e < 10; ++e)
            mem.at<int32_t>(kTable + e * 12 + 8) = 0;
}

// the saved table (rankingFile) over the backup RAM's, when it is valid
void Runtime::ranking_read_file()
{
    if (rankingFile.empty())
        return;
    FILE* f = std::fopen(rankingFile.c_str(), "rb");
    if (!f)
        return;
    uint8_t b[0x78];
    const bool ok = std::fread(b, 1, sizeof b, f) == sizeof b;
    std::fclose(f);
    bool valid = ok;
    for (uint32_t e = 0; valid && e < 10; ++e) {   // names printable, the rank card 0..7, the score sane
        for (uint32_t k = 0; k < 3; ++k)
            valid = valid && b[e * 12 + k] >= 0x20 && b[e * 12 + k] < 0x5B;
        int32_t s;
        std::memcpy(&s, b + e * 12 + 8, 4);
        valid = valid && b[e * 12 + 3] == 0 && b[e * 12 + 4] < 8 && s >= 0 && s < 10000000;
    }
    if (!valid) {
        if (log)
            log("ranking: " + rankingFile + " not valid, the defaults");
        return;
    }
    for (uint32_t i = 0; i < 0x78; ++i)
        mem.at<uint8_t>(kTable + i) = b[i];
}

// 0x226E0 backup_ranking_save
void Runtime::ranking_backup_save()
{
    if (rankingFile.empty())
        return;
    FILE* f = std::fopen(rankingFile.c_str(), "wb");
    if (!f) {
        if (log)
            log("ranking: cannot write " + rankingFile);
        return;
    }
    for (uint32_t i = 0; i < 0x78; ++i)
        std::fputc(mem.at<uint8_t>(kTable + i), f);
    std::fclose(f);
}

// 0x5E5D0 rank_insert: the players in the game into the top 10 (a score equal to an entry's goes above it), the
// higher score first; their places to 0x520AF9 / 0x520B05 (-1: not ranked), the new names "   "
void Runtime::rank_insert()
{
    ranking_backup_ensure();
    int8_t place[2] = {-1, -1};
    auto insert = [&](int p) -> int {
        if (noRanking) return -1;   // (totdpacks: a TotD 0 game with a mutator on: no table)
        const Player& pl = mem.at<Player>(G::players, uint32_t(p));
        if (pl.score < mem.at<int32_t>(kTable + 9 * 12 + 8))
            return -1;
        int i = 9;
        while (i > 0 && pl.score >= mem.at<int32_t>(kTable + uint32_t(i - 1) * 12 + 8))
            --i;
        for (int k = 9; k > i; --k) {   // the entries below move down one
            const uint32_t d = kTable + uint32_t(k) * 12, s = d - 12;
            for (uint32_t b = 0; b < 12; ++b)
                mem.at<uint8_t>(d + b) = mem.at<uint8_t>(s + b);
        }
        const uint32_t e = kTable + uint32_t(i) * 12;
        std::memcpy(&mem.at<uint8_t>(e), "   ", 4);
        mem.at<uint8_t>(e + 4) = uint8_t(pl.rank);
        mem.at<int32_t>(e + 8) = pl.score;
        return i;
    };
    bool in0 = mem.at<Player>(G::players, 0).state != 9, in1 = mem.at<Player>(G::players, 1).state != 9;
    if (cheated) {   // (smallfix: a game with a cheat on: no place, no name entry)
        in0 = in1 = false;
        if (log)
            log("ranking: a cheat was on, no place");
    }
    if (in0 && !in1) {
        place[0] = int8_t(insert(0));
    } else if (in1 && !in0) {
        place[1] = int8_t(insert(1));
    } else if (in0 && in1) {
        const int first = mem.at<Player>(G::players, 1).score < mem.at<Player>(G::players, 0).score ? 0 : 1;
        place[first] = int8_t(insert(first));
        place[1 - first] = int8_t(insert(1 - first));
        if (place[1 - first] >= 0 && place[first] >= place[1 - first]) {   // pushed down by the second
            place[first] = int8_t(place[first] + 1);
            if (place[first] > 9)
                place[first] = -1;
        }
    }
    mem.at<int8_t>(kEntry + 9) = place[0];
    mem.at<int8_t>(kEntry + 12 + 9) = place[1];
    if (log)
        log("ranking: rank_insert P1 " + std::to_string(place[0]) + " P2 " + std::to_string(place[1]));
}

// the teaser's end of Chapter 1 as the arcade's end of the game (game_ending 0x352A0's tail): the rank cards
// (score_rank_get), rank_insert; a place -> sub-mode 9 (the name entry), else next_mode (the ranking) or, without
// the title, the end screen
void Runtime::game_ending_rank()
{
    if (pm_on()) {
        // PC Mode (THOTD.EXE mode67_ranking_pc 0x44D0C4): no 3D name entry; the scores go to the PC Mode table in the
        // menus (frontend_state 7; the host's pcModeFinish, main.cpp) and the arcade's top 10 stays as it was
        if (log)
            log("ranking: PC Mode, the menus' table");
        chapterDone = true;
        return;
    }
    if (coop4_on() && rank4Round_ == 0) {
        // 4P: the 4P table in the backup RAM's place for both rounds (name_entry_finish puts the arcade's back)
        ranking_backup_ensure();
        rank4Arcade_.assign(&mem.at<uint8_t>(kTable), &mem.at<uint8_t>(kTable) + 0x78);
        rank4ArcadeFile_ = rankingFile;
        rankingFile = rankingFile.empty() ? std::string() : rankingFile + "4p";
        for (uint32_t i = 0; i < 0x78; ++i)
            mem.at<uint8_t>(kTable + i) = mem.at<uint8_t>(0xAA4A0 + i);
        ranking_read_file();
        if (rankingLowTest)   // (--ranking-low: the 4P table too)
            for (uint32_t e = 0; e < 10; ++e)
                mem.at<int32_t>(kTable + e * 12 + 8) = 0;
        rank4Round_ = 1;
        const int n = coop4_players_real();
        for (int p = 2; p < 4; ++p) {
            Player& pl = mem.at<Player>(player_rec(p));
            if (pl.state != 9)
                pl.rank = score_rank(pl.score, n > 1 ? 2 : 1);
        }
    }
    const int playing = (mem.at<Player>(G::players, 0).state != 9 ? 1 : 0) + (mem.at<Player>(G::players, 1).state != 9 ? 1 : 0);
    for (uint32_t p = 0; p < 2; ++p) {
        Player& pl = mem.at<Player>(G::players, p);
        if (pl.state != 9)
            pl.rank = score_rank(pl.score, playing > 1 ? 2 : 1);
    }
    mem.set32(0x51F42C, 0);
    rank_insert();
    if (mem.at<int8_t>(kEntry + 9) < 0 && mem.at<int8_t>(kEntry + 12 + 9) < 0) {
        mem.at<int16_t>(G::players_in_game) = 0;
        mem.at<int16_t>(G::players_in_game_b) = 0;
        name_entry_finish();
        return;
    }
    mem.set32(G::game_submode, 9);
}

// after the name entry (or without a place): the title's loop goes on (next_mode: mode 4 -> 7, the ranking); without
// the title the host's end screen
void Runtime::name_entry_finish()
{
    if (rank4Round_ == 1) {   // 4P: players 3 / 4's round, in players 1 / 2's places
        rank4Round_ = 2;
        rank4_swap_seats();
        rank_insert();
        if (mem.at<int8_t>(kEntry + 9) >= 0 || mem.at<int8_t>(kEntry + 12 + 9) >= 0) {
            if (log)
                log("ranking: players 3 / 4's names");
            mem.set32(G::game_submode, 9);
            return;
        }
    }
    if (rank4Round_ == 2) {   // the records back, the 4P table kept, the arcade's back in place
        rank4_swap_seats();
        ranking_backup_save();
        for (uint32_t i = 0; i < 0x78 && i < rank4Arcade_.size(); ++i)
            mem.at<uint8_t>(kTable + i) = rank4Arcade_[i];
        rankingFile = rank4ArcadeFile_;
        rank4Round_ = 0;
    }
    if (titleFlow) {
        attract = true;
        textureArea = -1;
        next_mode();
    } else {
        chapterDone = true;
    }
}

// 0x5D500 game_name_entry_start (sub-mode 9)
void Runtime::game_name_entry_start()
{
    if (greyPalette.on)
        palette_restore_vblank();
    palette_fade_finish();
    heap_reset();
    sprites2d_ = false;   // sprite_init
    sprites_ensure();
    sprite_slots_mark();
    texture_area_load(2, -1);
    mem.set32(0x51F9B4, 2);
    mem.set32(G::shot_enable, 1);
    for (uint32_t p = 0; p < 2; ++p) {
        Player& pl = mem.at<Player>(G::players, p);
        if (pl.state != 9 && mem.at<int8_t>(kEntry + p * 12 + 9) >= 0)
            pl.state = 8;
    }
    // the light: coprocessor 0x1A of (0, 0, 1) in camera space (0x21)
    mem.set32(kLight, 0);
    mem.set32(kLight + 4, 0);
    mem.set32(kLight + 8, fbits(1.0f));
    mem.at<uint8_t>(G::scene_state) = 0;
    mem.at<uint8_t>(G::sky_mode) = 0;
    color_row_set(0xC, 0x4A);
    color_row_set(0xD, 0x73);
    color_row_set(0xE, 0x4E);
    sound_cue(0xA0100D);
    mem.set32(kGroup, task_group_create(0xC5470));
    sprite_draw_anim(0x1002000, 1, 0, 0, 0, 0xA2);   // name_entry_frame_sprite 0x5E4F0
    mem.at<uint8_t>(G::gun_flash_phase) = 0;
    hud_mode_init();
    mem.set32(G::game_submode, mem.u32(G::game_submode) + 1);
    nameKeys.clear();
    for (char c : nameKeysTest)   // --name-keys (testing): '<' back, '>' forward, '#' END
        nameKeys.push_back(c == '<' ? 8 : c == '>' ? 9 : c == '#' ? 13 : c);
    if (log)
        log("ranking: name entry");
}

// 0x5D6D0 game_name_entry (sub-mode 10): the tasks; when done the names (banned ones blanked: name_is_banned
// 0x5E550, list 0x5E520), the backup saved, next_mode
void Runtime::game_name_entry()
{
    geo_frame_begin();
    // the keyboard (the teaser's addition): a key is a shot at its letter by the first player entering a name
    nameKey_ = 0;
    if (!nameKeys.empty() && frameCount % 8 == 0) {   // (one every 8 frames: the flying letters' spacing)
        nameKey_ = nameKeys.front();
        nameKeys.pop_front();
    }
    pcNameModels.clear();
    task_group_run(mem.u32(kGroup));
    if (mem.at<uint8_t>(kDone) == 0)
        return;
    for (uint32_t p = 0; p < 2; ++p) {
        const int8_t r = mem.at<int8_t>(kEntry + p * 12 + 9);
        if (r < 0)
            continue;
        const uint32_t e = kTable + uint32_t(r) * 12;
        bool banned = false;
        for (uint32_t l = 0x5E520; mem.at<uint8_t>(l) != 0xFF && !banned; l += 4) {
            bool same = true;
            for (uint32_t k = 0; mem.at<uint8_t>(e + k) != 0; ++k)
                same = same && mem.at<uint8_t>(e + k) == mem.at<uint8_t>(l + k);
            banned = same;
        }
        if (banned)
            for (uint32_t k = 0; mem.at<uint8_t>(e + k) != 0; ++k)
                mem.at<uint8_t>(e + k) = ' ';
        if (log)
            log(std::string("ranking: P") + char('1' + p) + " place " + std::to_string(r + 1) + " \"" +
                std::string(reinterpret_cast<const char*>(&mem.at<uint8_t>(e)), 3) + "\"");
    }
    sound_cue(0xA0145B);
    sound_cue(0xA00002);
    ranking_backup_save();
    for (uint32_t p = 0; p < 2; ++p)
        if (mem.at<Player>(G::players, p).state == 8)
            mem.at<Player>(G::players, p).state = 9;
    name_entry_finish();
}

// the keyboard: 'A'-'Z', '.', '-', '?', '!', 8 = back, 13 = END
void Runtime::name_entry_key(int c)
{
    if (mem.at<int16_t>(G::game_mode) == 4 && mem.u32(G::game_submode) == 10 && nameKeys.size() < 8)
        nameKeys.push_back(c);
}

bool Runtime::name_entry_active() const
{
    return mem.at<int16_t>(G::game_mode) == 4 && mem.u32(G::game_submode) == 10;
}

// 0x5E370 name_entry_model_draw: model +0x54 in camera space (coprocessor 0x21) at (x, y) screen units (x 0.001 /
// 496 at depth +0x40), turned +0x4C (z), +0x48 (y), +0x44 (x), scaled +0x94
void Runtime::name_entry_model_draw(uint32_t o)
{
    Tgp::Out none;
    geo_write(0x8000A0, 0xA0A);
    for (uint32_t k = 0; k < 3; ++k)
        geo_write(0x804000, mem.u32(kLight + k * 4));
    copro(0x21, nullptr, 0, none);
    const uint32_t at[3] = {fbits(mem.f32(o + 0x38) * 0.001f / 496.0f), fbits(mem.f32(o + 0x3C) * 0.001f / 496.0f),
                            mem.u32(o + 0x40)};
    copro(0x12, at, 3, none);
    const uint32_t rz[1] = {mem.u32(o + 0x4C)}, ry[1] = {mem.u32(o + 0x48)}, rx[1] = {mem.u32(o + 0x44)};
    copro(0x16, rz, 1, none);
    copro(0x15, ry, 1, none);
    copro(0x14, rx, 1, none);
    const uint32_t s[3] = {mem.u32(o + 0x94), mem.u32(o + 0x94), mem.u32(o + 0x94)};
    copro(0x13, s, 3, none);
    geo_draw_model(mem.u32(o + 0x54));
    copro(0x06, nullptr, 0, none);
    // pcrank: the PC files have none of these models (word_fumidai 0xAC8, the letters 0xACA-0xAF4: THOTD.EXE keeps
    // the name entry's code, 0x4B3E0C.., but its mode 4 sub-modes 9 / 10 are empty): with the PC models the grid,
    // the flying letters, the timer and the underscores are drawn by the HUD from the PC's sprites at the projected
    // place (x, y at depth +0x40 against 0.001; size: the scale over depth against a grid letter's 1.8e-5 / 0.001)
    if (pcModels) {
        const float z = mem.f32(o + 0x40);
        if (z > 1e-6f) {
            const float k = 0.001f / z;
            pcNameModels.push_back({mem.u32(o + 0x54), mem.f32(o + 0x38) * k, mem.f32(o + 0x3C) * k,
                                    mem.f32(o + 0x94) / z / (1.8e-5f / 0.001f)});
        }
    }
}

// the name entry's tasks (run() through prologue_routine -> ranking_routine)
bool Runtime::name_entry_routine(uint32_t routine, uint32_t o)
{
    auto obj = [&](uint32_t routine2) {
        const uint32_t t = task_create(routine2, 0x3C8);
        mem.set32(t + 0x44, 0);
        mem.set32(t + 0x48, 0);
        mem.set32(t + 0x4C, 0);
        mem.at<int16_t>(t + 0x6A) = 1;
        return t;
    };
    auto underscore = [&](uint32_t slot, uint32_t p) {   // 0x5E260
        const uint32_t t = obj(0x5FCF0);
        mem.set32(t + 0x3C, fbits(-149.0f));
        mem.set32(t + 0x40, 0x3A83126F);
        mem.set32(t + 0x94, 0x3792CCF7);
        mem.set32(t + 0x54, 0xAC8);
        mem.set32(t + 0x50, 0);
        mem.set32(t + 0x38, fbits(float(p) * 280.0f + float(slot) * 60.0f - 205.0f));
    };
    switch (routine) {
    case 0x55E0:   // player_st8_shoot_only (the players' state 8 in the name entry)
        mem.set32(o, 0x5600);
        [[fallthrough]];
    case 0x5600:   // player_shoot_only: 6 rounds kept, the gun (no HUD)
        mem.at<Player>(G::players, uint32_t(int32_t(mem.at<port::a::PlayerTask>(o).player))).rounds = 6;
        player_shoot(o);
        return true;
    case 0x5D7B0:   // name_entry_camera_start
        task_create(0x5FB60, 0x20);
        camera_globals_reset();
        return true;
    case 0x5FB60:   // name_entry_camera_task
        camera_matrices_update();
        return true;
    case 0x5D7D0: {   // name_entry_screen_start: the title, the backdrop, per player YOUR RANK and the underscores
        tilemap_clear_rect(0x1000000, 0, 0, 0x3E, 0x30);
        sprite_bank_load(0, 0x124);
        sprite_draw_anim(0x1000000, 0, 0, 0x12, 1, 0x124);
        tilemap_clear_rect(0x1004000, 0, 0, 0x3E, 0x30);
        sprite_bank_load(0x29, 0x11F);
        sprite_draw_anim(0x1004000, 0, 0, 0, 0, 0x11F);
        sprite_bank_load(0x2E, 0x1E5);
        sprite_bank_load(0xE, 0x125);
        sprite_bank_load(0xF, 0x126);
        for (uint32_t p = 0; p < 2; ++p) {
            const int8_t r = mem.at<int8_t>(kEntry + p * 12 + 9);
            if (r == -1)
                continue;
            sprite_draw_anim(0x1000000, 0, 0, p == 0 ? 0 : 0x3A, 0x27, p == 0 ? 0x125 : 0x126);
            const int x0 = int(p) * 0x38;
            if (r < 3) {
                for (uint32_t k = 0; k < 3; ++k)
                    sprite_draw_anim(0x1000000, 0, 0, x0 + int(k) + 1, 0x25, mem.u32(0xC5530 + uint32_t(r) * 0x10 + k * 4));
            } else {
                int x;
                if (r < 9) {
                    sprite_draw_anim(0x1000000, 0, 0, x0 + 1, 0x25, uint32_t(r) + 0x1E6);
                    sprite_draw_anim(0x1000000, 0, 0, x0 + 2, 0x25, 0x20A);
                    x = x0 + 3;
                } else {
                    sprite_draw_anim(0x1000000, 0, 0, x0 + 1, 0x25, 0x1E6);
                    sprite_draw_anim(0x1000000, 0, 0, x0 + 2, 0x25, 0x1E5);
                    sprite_draw_anim(0x1000000, 0, 0, x0 + 3, 0x25, 0x20A);
                    x = x0 + 4;
                }
                sprite_draw_anim(0x1000000, 0, 0, x, 0x25, 0x1FE);
            }
            for (uint32_t s = 0; s < 3; ++s)
                underscore(s, p);
            mem.at<uint8_t>(kEntry + p * 12 + 8) = 0;
            mem.at<uint8_t>(kEntry + p * 12 + 0xA) = 0;
        }
        mem.at<uint8_t>(kDone) = 0;
        return true;
    }
    case 0x5DE10: {   // name_entry_letters_spawn: 33 letter models in rows (table 0xC54A0)
        uint32_t i = 0;
        for (float y = 90.0f; y >= -60.0f; y -= 50.0f)
            for (float x = -198.0f; x <= 198.0f; x += 44.0f) {
                if (i >= 0x21)
                    continue;
                const uint32_t t = obj(0x5DAB0);
                float px = x;
                if (i > 0x1E)
                    px += i == 0x20 ? 252.0f : 12.0f;
                mem.set32(t + 0x38, fbits(px));
                mem.set32(t + 0x3C, fbits(y));
                mem.set32(t + 0x40, 0x3A83126F);
                mem.set32(t + 0x48, 0x8000);
                mem.set32(t + 0x50, i + 1);
                mem.set32(t + 0x2EC, 0);
                mem.set32(t + 0x94, 0x3796FEB5);
                mem.set32(t + 0x54, mem.u32(0xC54A0 + i * 4));
                ++i;
            }
        return true;
    }
    case 0x5DF20:   // name_entry_cursor_spawn: per ranked player its aim (model 0x65F, not drawn)
        for (uint32_t p = 0; p < 2; ++p) {
            if (mem.at<int8_t>(kEntry + p * 12 + 9) == -1)
                continue;
            const uint32_t t = obj(0x5FB70);
            mem.set32(t + 0x38, 0);
            mem.set32(t + 0x3C, 0);
            mem.set32(t + 0x44, 0x4000);
            mem.set32(t + 0x50, 0);
            mem.at<int16_t>(t + 0x64) = int16_t(p);
            mem.set32(t + 0x40, 0x3A83126F);
            mem.set32(t + 0x94, 0x37FBA882);
            mem.set32(t + 0x54, 0x65F);
        }
        return true;
    case 0x5E180: {   // name_entry_timer_spawn: the countdown model 9 .. 0 (0xADA .. 0xAD1)
        const uint32_t t = obj(0x5FBE0);
        mem.set32(t + 0x38, 0);
        mem.at<uint16_t>(kTimer) = 0x78;
        mem.set32(t + 0x3C, fbits(-110.0f));
        mem.set32(t + 0x40, 0x3A83126F);
        mem.set32(t + 0x48, 0x8000);
        mem.set32(t + 0x94, 0x3827C5AC);
        mem.set32(t + 0x54, 0xADA);
        return true;
    }
    case 0x5FB70: {   // name_entry_cursor_task: the player's aim
        const uint32_t p = uint32_t(mem.at<int16_t>(o + 0x64));
        const Player& pl = mem.at<Player>(G::players, p);
        mem.set32(kEntry + p * 12, fbits(float(pl.aim[0])));
        mem.set32(kEntry + p * 12 + 4, fbits(float(pl.aim[1])));
        mem.set32(o + 0x38, mem.u32(kEntry + p * 12));
        mem.set32(o + 0x3C, mem.u32(kEntry + p * 12 + 4));
        return true;
    }
    case 0x5FBE0: {   // name_entry_timer_task: a digit every 120 frames (cue 0xA01352); after 0 the entry ends
        const uint16_t t = uint16_t(mem.at<uint16_t>(kTimer) - 1);
        mem.at<uint16_t>(kTimer) = t;
        if (t == 0) {
            mem.at<uint16_t>(kTimer) = 0x78;
            const uint32_t m = mem.u32(o + 0x54) - 1;
            mem.set32(o + 0x54, m);
            if (m == 0xAD0)
                mem.at<uint8_t>(kDone) = 1;
            else
                sound_cue(0xA01352);
        }
        name_entry_model_draw(o);
        return true;
    }
    case 0x5DAB0: {   // name_entry_letter_task
        const uint32_t model = mem.u32(o + 0x54);
        const float x = mem.f32(o + 0x38), y = mem.f32(o + 0x3C);
        for (uint32_t p = 0; p < 2; ++p) {
            const uint32_t pe = kEntry + p * 12;
            const int8_t r = mem.at<int8_t>(pe + 9);
            if (r == -1)
                continue;
            const float dx = std::fabs(x - mem.f32(pe)), dy = std::fabs(y - mem.f32(pe + 4));
            const bool on = dx < 20.0f && dy < 15.0f;
            if (mem.at<uint8_t>(pe + 0xA) == 0 && on) {
                mem.at<uint8_t>(pe + 0xA) = 1;
                const uint32_t h = obj(0x5FD00);   // name_entry_highlight_spawn 0x5E300
                mem.set32(h + 0x38, fbits(x));
                mem.set32(h + 0x3C, fbits(y));
                mem.set32(h + 0x40, 0x3A83126F);
                mem.set32(h + 0x54, 0xAC9);
                mem.set32(h + 0x94, 0x37E27E0F);
                mem.at<int16_t>(h + 0x64) = int16_t(p);
            }
            // the keyboard's shot: the first player with a place
            const bool keyShot = nameKey_ != 0 && (p == 0 || mem.at<int8_t>(kEntry + 9) == -1) && [&] {
                if (nameKey_ >= 'A' && nameKey_ <= 'Z') return model == 0xADB + uint32_t(nameKey_ - 'A');
                switch (nameKey_) {
                case '.': return model == 0xACA;
                case '-': return model == 0xACB;
                case '?': return model == 0xACC;
                case '!': return model == 0xACD;
                case 8: return model == 0xACE;
                case 9: return model == 0xACF;
                case 13: return model == 0xAD0;
                default: return false;
                }
            }();
            const bool shot = keyShot || ((mem.at<Player>(G::players, p).flags >> 1 & 1) != 0 && on);
            const uint8_t slot = mem.at<uint8_t>(pe + 8);
            if (!shot || (slot == 0 && model == 0xACE) || (slot == 3 && model != 0xACE && model != 0xAD0))
                continue;
            char c = ' ';
            bool special = false;
            switch (model) {
            case 0xACA: c = '.'; break;
            case 0xACB: c = '-'; break;
            case 0xACC: c = '?'; break;
            case 0xACD: c = '!'; break;
            case 0xACE:   // back
                mem.at<uint8_t>(pe + 8) = uint8_t(slot - 1);
                special = true;
                mem.at<uint8_t>(kTable + uint32_t(slot - 1) + uint32_t(r) * 12) = 0x20;
                break;
            case 0xACF:   // forward
                special = true;
                mem.at<uint8_t>(pe + 8) = uint8_t(slot + 1);
                break;
            case 0xAD0:   // END
                mem.at<uint8_t>(kDone) = 1;
                special = true;
                break;
            default:
                c = char(model + 0x66);
                sound_cue(0xA01363);
                break;
            }
            if (!special) {   // a flying copy to the slot (name_entry_flying_letter_spawn 0x5E0D0)
                mem.at<uint8_t>(pe + 8) = uint8_t(slot + 1);
                const uint32_t t = obj(0x5DFB0);
                mem.set32(t + 0x40, 0x3A03126F);
                mem.set32(t + 0x48, 0x8000);
                mem.set32(t + 0x344, 0x3A83126F);
                mem.set32(t + 0x30C, fbits(175.0f));
                mem.set32(t + 0x2EC, 0x8000);
                mem.set32(t + 0x38, 0);
                mem.set32(t + 0x3C, 0);
                mem.set32(t + 0x44, 0);
                mem.set32(t + 0x4C, 0);
                mem.at<int16_t>(t + 0x6A) = 1;
                mem.set32(t + 0x33C, fbits(float(p) * 280.0f + float(slot) * 60.0f - 205.0f));
                mem.set32(t + 0x340, fbits(-140.0f));
                mem.set32(t + 0x54, model & 0xFFFF);
                mem.set32(t + 0x94, 0x37C9539C);
                mem.set32(t + 0x50, slot);
                mem.at<int16_t>(t + 0x64) = int16_t(p);
                mem.at<uint8_t>(kTable + uint32_t(slot) + uint32_t(r) * 12) = uint8_t(c);
            }
            mem.set32(o + 0x2EC, 0x3200);
        }
        const int32_t spin = int32_t(mem.u32(o + 0x2EC));
        if (spin < 1) {
            mem.set32(o + 0x48, 0x8000);
        } else {
            mem.set32(o + 0x48, uint32_t(spin + int32_t(mem.u32(o + 0x48))));
            mem.set32(o + 0x2EC, uint32_t(spin - 0x140));
        }
        name_entry_model_draw(o);
        return true;
    }
    case 0x5DFB0: {   // name_entry_flying_letter_task: spirals into the slot; a slot taken back -> it falls
        const uint32_t p = uint32_t(mem.at<int16_t>(o + 0x64));
        if (int32_t(mem.at<uint8_t>(kEntry + p * 12 + 8)) < int32_t(mem.u32(o + 0x50)) + 1) {
            const uint32_t t = obj(0x5FC60);   // name_entry_falling_letter_spawn 0x5E1F0
            mem.set32(t + 0x38, mem.u32(o + 0x38));
            mem.set32(t + 0x3C, mem.u32(o + 0x3C));
            mem.set32(t + 0x40, mem.u32(o + 0x40));
            mem.set32(t + 0x7C, fbits(0.5f));
            mem.set32(t + 0x94, 0x37C9539C);
            mem.set32(t + 0x54, mem.u32(o + 0x54) & 0xFFFF);
            task_exit();
            return true;
        }
        Tgp::Out out;
        const uint32_t a[1] = {mem.u32(o + 0x2EC)};
        copro(0x1B, a, 1, out);
        float sn = 0.0f;
        if (out.n) std::memcpy(&sn, &out.w[0], 4);
        Tgp::Out out2;
        copro(0x1C, a, 1, out2);
        float cs = 0.0f;
        if (out2.n) std::memcpy(&cs, &out2.w[0], 4);
        const float r = mem.f32(o + 0x30C);
        mem.set32(o + 0x38, fbits(mem.f32(o + 0x33C) + r * sn));
        mem.set32(o + 0x3C, fbits(mem.f32(o + 0x340) + r * cs));
        float z = mem.f32(o + 0x40) + port::ffrom(0x387BA882u);   // 6e-5 (0x5E048; ch4proof: was 1.5e-5)
        if (z > mem.f32(o + 0x344))
            z = mem.f32(o + 0x344);
        mem.set32(o + 0x40, fbits(z));
        if (r >= 1.6f)
            mem.set32(o + 0x2EC, mem.u32(o + 0x2EC) + (p == 0 ? uint32_t(-0x800) : 0x800u));
        mem.set32(o + 0x30C, fbits(r * 0.9f));
        name_entry_model_draw(o);
        return true;
    }
    case 0x5FC60: {   // name_entry_falling_letter_task: gravity 0.2, spinning, until y < -250
        if (mem.f32(o + 0x3C) < -250.0f) {
            task_exit();
            return true;
        }
        const float v = mem.f32(o + 0x7C);
        mem.set32(o + 0x44, mem.u32(o + 0x44) + 0x800);
        mem.set32(o + 0x4C, mem.u32(o + 0x4C) + 0x600);
        mem.set32(o + 0x3C, fbits(mem.f32(o + 0x3C) + v));
        mem.set32(o + 0x7C, fbits(v - 0.2f));
        mem.set32(o + 0x40, fbits(mem.f32(o + 0x40) + 2e-6f));
        name_entry_model_draw(o);
        return true;
    }
    case 0x5FCF0:   // name_entry_model_task
        name_entry_model_draw(o);
        return true;
    case 0x5FD00: {   // name_entry_highlight_task: while the aim stays on the letter
        const uint32_t p = uint32_t(mem.at<int16_t>(o + 0x64));
        if (std::fabs(mem.f32(o + 0x38) - mem.f32(kEntry + p * 12)) >= 20.0f ||
            std::fabs(mem.f32(o + 0x3C) - mem.f32(kEntry + p * 12 + 4)) >= 15.0f) {
            mem.at<uint8_t>(kEntry + p * 12 + 0xA) = 0;
            task_exit();
            return true;
        }
        name_entry_model_draw(o);
        return true;
    }
    default:
        return false;
    }
}

// 4P ranking: players 1 / 2's records and players 3 / 4's swapped (the name entry's tasks and rank_insert read
// players 1 / 2)
void Runtime::rank4_swap_seats()
{
    for (int p = 0; p < 2; ++p)
        for (uint32_t i = 0; i < 0xCC; ++i)
            std::swap(mem.at<uint8_t>(player_rec(p) + i), mem.at<uint8_t>(player_rec(p + 2) + i));
}

} // namespace hotd::game
