// Combat support: attack positions taken and queued, kill scores and the researcher penalty, the blood marks over shot
// hostages, the Molb's floor modes, jump velocities, the room bounds, the camera side, picking a target player, the
// splines the walkers follow, the demo test. First written as runtime overrides (T1, src/game/t1_bounds.cpp), read from
// the disassembly. Arcade forms (decl/support_b.h).
#include "port/game.h"

#include <cstdlib>

namespace hotd::port {

namespace {
constexpr uint32_t kSlots = 0x51F9F0;   // AttackSlot[16] (0x14 each): +0 taken, +4 holder, +8 pos[3] (arcade)
constexpr uint32_t slotAt(int32_t i) { return kSlots + uint32_t(i) * 0x14; }
} // namespace

// 0x32320 enemy_slot_take: the first free position 2..12, +0x2E7 = it
template <class P> void Game<P>::enemy_slot_take(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        for (int32_t i = 2; i <= 12; ++i)
            if (mem.at<uint8_t>(slotAt(i)) == 0) {
                mem.at<int8_t>(obj + 0x2E7) = int8_t(i);
                mem.set32(slotAt(i) + 4, obj);
                mem.at<uint8_t>(slotAt(i)) = 1;
                return;
            }
    }
}

// 0x31F00 attack_queue_role: from a back position to a free front-waiting one: 5 -> 2; 4 -> 2 / 3 (a coin flip when
// both are free); 6 -> 3; flag bit 30 cleared
template <class P> void Game<P>::attack_queue_role(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        auto taken = [&](int32_t i) { return mem.at<uint8_t>(slotAt(i)) != 0; };
        const int32_t cur = mem.at<int8_t>(obj + 0x2E7);
        int32_t c = -1;
        if (cur == 5) {
            if (!taken(2))
                c = 2;
        } else if (cur == 4) {
            if (!taken(2) && !taken(3))
                c = (int32_t(rand()) % 2) != 0 ? 2 : 3;
            else if (!taken(2))
                c = 2;
            else if (!taken(3))
                c = 3;
        } else if (cur == 6) {
            if (!taken(3))
                c = 3;
        }
        if (c < 0)
            return;
        mem.at<uint8_t>(slotAt(cur)) = 0;
        mem.at<uint8_t>(slotAt(c)) = 1;
        mem.set32(slotAt(cur) + 4, 0);
        mem.set32(slotAt(c) + 4, obj);
        mem.at<int8_t>(obj + 0x2E7) = int8_t(c);
        mem.set32(obj + 0x58, mem.u32(obj + 0x58) & 0xBFFFFFFFu);
    }
}

// 0x831D0 attack_front_busy(obj): position 0 holder -> is 1 taken; position 1 holder -> is 0 taken; else either
template <class P> uint32_t Game<P>::attack_front_busy(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
        return 0;
    } else {
        const int8_t s = mem.at<int8_t>(obj + 0x2E7);
        if (s == 0)
            return mem.at<uint8_t>(slotAt(1));
        if (s == 1)
            return mem.at<uint8_t>(slotAt(0));
        return mem.at<uint8_t>(slotAt(0)) != 0 || mem.at<uint8_t>(slotAt(1)) != 0 ? 1 : 0;
    }
}

// 0x304D0 kill_score_award(player, points): to that player if in the game (state 5); 2 = either (a coin flip,
// whoever is in the game)
template <class P> void Game<P>::kill_score_award(int player, int points)
{
    auto in = [&](uint32_t p) { return int32_t(mem.u32(G::players + p * 0xCC)) == 5; };
    if (player == 0 || player == 1) {
        if (in(uint32_t(player)))
            add_score(player, points);
        return;
    }
    if (player != 2)
        return;
    if ((int32_t(rand()) & 1) == 0) {
        if (in(0))
            add_score(0, points);
        else if (in(1))
            add_score(1, points);
    } else {
        if (in(1))
            add_score(1, points);
        else if (in(0))
            add_score(0, points);
    }
}

// 0x30490 kill_score_shooter: to the player whose shot hit (+0x58 bit 1: player 1, bit 2: player 2, both: either)
template <class P> void Game<P>::kill_score_shooter(uint32_t obj, int points)
{
    if constexpr (P::pc) {
        (void)obj, (void)points;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const uint8_t f = mem.at<uint8_t>(obj + 0x58);
        if (coop4_on()) {   // 4P: the real shooters (the hit table); several: one at random of those in the game
            uint32_t seats = coop4_hit_seats(obj);
            if (seats == 0)
                seats = (f >> 1) & 3;
            int in[4], n = 0;
            for (int s = 0; s < 4; ++s)
                if (((seats >> s) & 1) && int32_t(mem.u32(player_rec(s))) == 5)
                    in[n++] = s;
            if (n > 0)
                add_score(n == 1 ? in[0] : in[uint32_t(rand()) % uint32_t(n)], points);
            return;
        }
        int p = ((f >> 1) & 1) ? 0 : -1;
        if ((f >> 2) & 1)
            p += 2;
        kill_score_award(p, points);
    }
}

// 0x304C0 enemy_kill_score: to the player in +0x2EB
template <class P> void Game<P>::enemy_kill_score(uint32_t obj, int points)
{
    if constexpr (P::pc) {
        (void)obj, (void)points;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const int k = mem.at<int8_t>(obj + 0x2EB);
        if (coop4_on() && (k == 2 || k == 3)) {   // 4P: the killer is seat 3 / 4 (not the arcade's "either" 2)
            if (int32_t(mem.u32(player_rec(k))) == 5)
                add_score(k, points);
            return;
        }
        kill_score_award(k, points);
    }
}

// 0x6BDF0 researcher_scream_penalty(obj, woman, shooter known): a scream (men 0xC6850 [rand & 3] / 0xC6860 [rand % 2],
// women 0xC6868 / 0xC6870 [rand % 2]) and -100 points: to the shooter, else to every player in the game
template <class P> void Game<P>::researcher_scream_penalty(uint32_t obj, uint32_t woman, uint32_t known)
{
    if constexpr (P::pc) {
        (void)obj, (void)woman, (void)known;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        uint32_t cue;
        if ((woman & 0xFF) == 0) {
            if ((known & 0xFF) != 0)
                cue = mem.u32(0xC6860 + uint32_t(int32_t(rand()) % 2) * 4);
            else
                cue = mem.u32(0xC6850 + (rand() & 3) * 4);
        } else {
            if ((known & 0xFF) != 0)
                cue = mem.u32(0xC6870 + uint32_t(int32_t(rand()) % 2) * 4);
            else
                cue = mem.u32(0xC6868 + uint32_t(int32_t(rand()) % 2) * 4);
        }
        if ((known & 0xFF) != 0) {
            kill_score_shooter(obj, -100);
        } else {
            for (int p = 0; p < seat_count(); ++p)   // (4P: all four)
                if (int32_t(mem.u32(player_rec(p))) == 5)
                    add_score(p, -100);
        }
        sound_cue(cue);
    }
}

// 0x70870 researcher_hit_by_player(obj): a held researcher shot by a player: the blood mark (0x1DB30) at its place,
// player_hit on the shooter (+0x58 bit 1: player 1), the scream and the -100 (researcher_scream_penalty, +0x328 =
// woman)
template <class P> void Game<P>::researcher_hit_by_player(uint32_t obj)
{
    if (lg_hostage_crisis)
        lg_crisis_fail = true;   // Light Gun Edition: Hostage Crisis: game over for everyone
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const uint32_t t = task_create(F::researcher_blood_effect, 0x58);
        mem.set32(t + 0x20, mem.u32(obj + 0x2D4));
        mem.set32(t + 0x24, mem.u32(obj + 0x2D8));
        mem.set32(t + 0x50, 0);
        mem.set32(t + 0x28, mem.u32(obj + 0x2DC));
        {   // (testing only: the shooter's count)
            const int proxy = ((mem.at<uint8_t>(obj + 0x58) >> 1) & 1) ? 0 : 1;
            ++stat_hostages[(coop4_on() ? coop4_shooter(obj, proxy) : proxy) & 3];
        }
        const int proxy = ((mem.at<uint8_t>(obj + 0x58) >> 1) & 1) ? 0 : 1;
        if (coop4_on()) {
            // 4P (cpuai 2026-10-26): the shooter's seat loses the life, as the arcade's shooter does (player_hit would
            // pass a hit on side 0 / 1 round the players like an enemy's attack: coop4_victim)
            const int s = coop4_shooter(obj, proxy);
            if (player_hit_check(s) == 0 && mem.at<int32_t>(invuln_at(s)) == 0 && mem.at<uint16_t>(G::game_mode) != 3) {
                player_hit_take(s, 0, 0);
                mem.at<int32_t>(invuln_at(s)) = 0x5A;
                player_hit_keep_life(s);
            }
        } else {
            player_hit(proxy, 0, 0);
        }
        researcher_scream_penalty(obj, mem.at<uint8_t>(obj + 0x328), 1);
    }
}

// 0x1DCD0 hostage_shot_effect / 0x1DB30 researcher_blood_effect (task): the blood mark over a shot hostage /
// researcher: model 0x3F5 (player 1; researcher_blood_effect always) / 0x3F6 at its camera-space place (depth clamped to
// 30..100), facing the camera, scaled 496 / z x 0.004; frames 0-39 every frame, 40-59 every other frame, then gone
template <class P> void Game<P>::blood_mark_effect(uint32_t obj, bool fixed)
{
    const int32_t t = int32_t(mem.u32(obj + 0x50));
    const bool draw = t <= 0x27 || (t < 0x3C && (t % 2) != 0);
    if (!draw && t > 0x3B)
        task_exit();
    if (draw) {
        Tgp::Out o, none;
        uint32_t p[3] = {mem.u32(obj + 0x20), mem.u32(obj + 0x24), mem.u32(obj + 0x28)};
        copro(0x1A, p, 3, o);
        float x = ffrom(o.w[0]), y = ffrom(o.w[1]), z = ffrom(o.w[2]);
        float lim = 0.0f;
        if (!(z < 100.0f))
            lim = 100.0f;
        else if (!(z > 30.0f))
            lim = 30.0f;
        if (lim != 0.0f) {
            const float k = lim / z;
            x *= k;
            y *= k;
            z = k * z;
        }
        mem.set32(obj + 0x38, fbits(x));
        mem.set32(obj + 0x3C, fbits(y));
        mem.set32(obj + 0x40, fbits(z));
        const float s = (496.0f / z) * ffrom(0x3B83126Fu);
        copro(0x21, nullptr, 0, none);
        uint32_t tr[3] = {fbits(x / z), fbits(y / z), fbits(1.0f)};
        copro(0x12, tr, 3, none);
        uint32_t a[1] = {0x8000};
        copro(0x15, a, 1, none);
        uint32_t sc[3] = {fbits(s), fbits(s), fbits(s)};
        copro(0x13, sc, 3, none);
        geo_draw_model(fixed || look_seat(mem.at<uint8_t>(obj + 0x2EB)) == 0 ? 0x3F5 : 0x3F6);   // (charsel4: by look_seat)
        copro(0x06, nullptr, 0, none);
    }
    mem.set32(obj + 0x50, mem.u32(obj + 0x50) + 1);
}
template <class P> void Game<P>::hostage_shot_effect(uint32_t obj) { blood_mark_effect(obj, false); }
template <class P> void Game<P>::researcher_blood_effect(uint32_t obj) { blood_mark_effect(obj, true); }

// the floor under an object (floor_probe from (x, 32768, z), as the molb floor modes): the hit point (inline in them)
template <class P> void Game<P>::floor_under(uint32_t obj, float out[3])
{
    LocalScope scope(*this);
    const uint32_t p = local_alloc(0x40);
    mem.set32(p, mem.u32(obj + 0x20));
    mem.set32(p + 4, fbits(32768.0f));
    mem.set32(p + 8, mem.u32(obj + 0x28));
    floor_probe(p, p + 0x10, p + 0x20, p + 0x30);
    for (uint32_t k = 0; k < 3; ++k)
        out[k] = mem.f32(p + 0x10 + 4 * k);
}

// the floor modes' level: the floor + the type's height; PC Mode / Boss Mode: THOTD.EXE 0x467B08 / 0x467B70 /
// 0x467BF8: (the floor + pc_molb_height_offset) + the type's height (pmbody)
template <class P> float Game<P>::molb_floor_level(uint32_t obj, float floor)
{
    const float h = mem.f32(G::creature_floor_height + uint32_t(int32_t(mem.at<int16_t>(obj + 0xCC))) * 4);
    return pc_path() ? (floor + molb_height_offset()) + h : h + floor;
}

// 0x2E120 molb_floor_stick (floor mode 1): on the floor (+ the type's height 0x14B780)
template <class P> void Game<P>::molb_floor_stick(uint32_t obj)
{
    float f[3];
    floor_under(obj, f);
    mem.set32(obj + 0x20, fbits(f[0]));
    mem.set32(obj + 0x28, fbits(f[2]));
    mem.set32(obj + 0x24, fbits(molb_floor_level(obj, f[1])));
}

// 0x2E180 molb_floor_above (floor mode 3): never below the floor
template <class P> void Game<P>::molb_floor_above(uint32_t obj)
{
    float f[3];
    floor_under(obj, f);
    const float y = molb_floor_level(obj, f[1]);
    if (mem.f32(obj + 0x24) < y) {
        mem.set32(obj + 0x20, fbits(f[0]));
        mem.set32(obj + 0x28, fbits(f[2]));
        mem.set32(obj + 0x24, fbits(y));
    }
}

// 0x2E200 molb_floor_bounce (floor mode 2): at the floor: put on it, bounce count +0x320 + 1, vertical speed reversed
template <class P> void Game<P>::molb_floor_bounce(uint32_t obj)
{
    float f[3];
    floor_under(obj, f);
    const float y = molb_floor_level(obj, f[1]);
    if (mem.f32(obj + 0x24) <= y) {
        mem.set32(obj + 0x24, fbits(y));
        mem.set32(obj + 0x320, mem.u32(obj + 0x320) + 1);
        mem.set32(obj + 0x7C, fbits(-mem.f32(obj + 0x7C)));
    }
}

// 0x6A6F0 velocity_for_arc(obj, from, to, gravity, frames): the velocity landing on `to` after `frames` under gravity
// (acceleration (0, -g, 0))
template <class P> void Game<P>::velocity_for_arc(uint32_t obj, uint32_t from, uint32_t to, uint32_t gravity, uint32_t frames)
{
    if constexpr (P::pc) {
        (void)obj, (void)from, (void)to, (void)gravity, (void)frames;   // the PC form: not ported (vtable only)
    } else {
        const float g = ffrom(gravity), n = ffrom(frames);
        mem.set32(obj + 0x84, 0);
        mem.set32(obj + 0x8C, 0);
        mem.set32(obj + 0x88, fbits(-g));
        if (pc_path()) {   // PC Mode (docs/pcmode.md row 10): the PC's 0x4C56F0 (times 1 / n, its x87 order)
            const float f[3] = {mem.f32(from), mem.f32(from + 4), mem.f32(from + 8)};
            const float t[3] = {mem.f32(to), mem.f32(to + 4), mem.f32(to + 8)};
            float v[3];
            pm::arc_velocity(f, t, g, n, v);
            for (uint32_t k = 0; k < 3; ++k)
                mem.set32(obj + 0x78 + 4 * k, fbits(v[k]));
            return;
        }
        const float dy = mem.f32(to + 4) - mem.f32(from + 4);
        mem.set32(obj + 0x78, fbits((mem.f32(to) - mem.f32(from)) / n));
        mem.set32(obj + 0x7C, fbits(((dy + dy) + (g * n) * n) / (n + n)));
        mem.set32(obj + 0x80, fbits((mem.f32(to + 8) - mem.f32(from + 8)) / n));
    }
}

// 0x6A760 velocity_for_arc_speed(obj, from, to, gravity, speed, outFrames): the flight time = the floor distance (to,
// from; coprocessor 0x1F with y 0) / speed (its integer part to outFrames), then as velocity_for_arc (inline there)
template <class P> void Game<P>::velocity_for_arc_speed(uint32_t obj, uint32_t from, uint32_t to, uint32_t gravity, uint32_t speed,
                                                       uint32_t outFrames)
{
    if constexpr (P::pc) {
        (void)obj, (void)from, (void)to, (void)gravity, (void)speed, (void)outFrames;   // not ported (vtable only)
    } else {
        float n;
        if (pc_path()) {   // PC Mode (docs/pcmode.md row 10): the PC's 0x4C576C (its table square root)
            const float f[3] = {mem.f32(from), mem.f32(from + 4), mem.f32(from + 8)};
            const float t[3] = {mem.f32(to), mem.f32(to + 4), mem.f32(to + 8)};
            n = pm::arc_frames(f, t, ffrom(speed));
        } else {
            uint32_t in[6] = {mem.u32(to), 0, mem.u32(to + 8), mem.u32(from), 0, mem.u32(from + 8)};   // on the floor
            Tgp::Out o;
            copro(0x1F, in, 6, o);
            n = ffrom(o.w[0]) / ffrom(speed);
        }
        mem.set32(outFrames, uint32_t(cvtzril(n)));
        velocity_for_arc(obj, from, to, gravity, fbits(n));
    }
}

// 0x2F990 room_bounds_bounce: inside the room box (x 0x51FE38..3C, z 0x51FE48..4C) for the next step: at a wall the
// position is put on it and that speed x -0.2; the first wall hit (+0x334 = 0, +0x328 < 2): sound 0xA01224 and wall
// dust; 1 = a wall was hit, else -1
template <class P> uint32_t Game<P>::room_bounds_bounce(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
        return 0;
    } else {
        const float px = mem.f32(obj + 0x20), pz = mem.f32(obj + 0x28);
        uint32_t wall = 0xFFFFFFFFu;
        auto bounce = [&](uint32_t posOff, uint32_t velOff, uint32_t limit, uint32_t angle) {
            mem.set32(obj + posOff, mem.u32(limit));
            mem.set32(obj + velOff, fbits(float(double(mem.f32(obj + velOff)) * -0.2)));
            mem.set32(obj + 0x324, mem.u32(obj + 0x324) | 0x80);
            wall = angle;
        };
        if (mem.f32(obj + 0x78) + px < mem.f32(G::room_x0))
            bounce(0x20, 0x78, G::room_x0, 0x4000);
        if (mem.f32(G::room_x1) < mem.f32(obj + 0x78) + px)
            bounce(0x20, 0x78, G::room_x1, 0xFFFFC000u);
        if (mem.f32(obj + 0x80) + pz < mem.f32(G::room_z0))
            bounce(0x28, 0x80, G::room_z0, 0);
        if (mem.f32(G::room_z1) < mem.f32(obj + 0x80) + pz)
            bounce(0x28, 0x80, G::room_z1, 0x8000);
        const bool hit = int8_t(mem.at<uint8_t>(obj + 0x324)) < 0;
        mem.set32(obj + 0x324, mem.u32(obj + 0x324) & 0xFFFFFA7Fu);
        if (!hit)
            return 0xFFFFFFFFu;
        if (mem.u32(obj + 0x334) == 0 && int32_t(mem.u32(obj + 0x328)) < 2) {
            sound_cue(0xA01224);
            wall_dust_spawn(obj, wall);
            mem.set32(obj + 0x334, 1);
        }
        return 1;
    }
}

// 0x2F6C0 camera_side_set: +0x2E9 = the camera side: x farther than z: 1 if x <= camera x else 0; else 3 if z <=
// camera z else 2 (the differences truncated to integers)
template <class P> void Game<P>::camera_side_set(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const int32_t dx = std::abs(int32_t(mem.f32(obj + 0x20) - mem.f32(G::camera_pos)));
        const int32_t dz = std::abs(int32_t(mem.f32(obj + 0x28) - mem.f32(G::camera_pos + 8)));
        int8_t side;
        if (dz < dx)
            side = mem.f32(G::camera_pos) < mem.f32(obj + 0x20) ? 0 : 1;
        else
            side = mem.f32(G::camera_pos + 8) < mem.f32(obj + 0x28) ? 2 : 3;
        mem.at<int8_t>(obj + 0x2E9) = side;
    }
}

// 0x27470 player_in_play(p): the camera stopped and the player in the game (or the attract demo)
template <class P> uint32_t Game<P>::player_in_play(int p)
{
    if (mem.u32(G::camera_stopped) != 2)
        return 0;
    if (coop4_on() && (p == 0 || p == 1) && mem.at<int16_t>(G::game_mode) != 3)   // 4P: side 0 / 1 = seats 0, 2 / 1, 3
        return int32_t(mem.u32(player_rec(p))) == 5 || int32_t(mem.u32(player_rec(p + 2))) == 5 ? 1 : 0;
    return mem.at<int16_t>(G::game_mode) == 3 || int32_t(mem.u32(G::players + uint32_t(p) * 0xCC)) == 5 ? 1 : 0;
}

// 0x274C0 combat_active: the camera stopped and a player in the game (or the attract demo)
template <class P> uint32_t Game<P>::combat_active()
{
    if constexpr (P::pc) {
        return 0;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        if (mem.u32(G::camera_stopped) != 2)
            return 0;
        return mem.at<int16_t>(G::game_mode) == 3 || mem.at<int16_t>(G::players_in_game) != 0 ? 1 : 0;
    }
}

// 0x2F7B0 pick_target_player: +0x2E8 = a player in play not already targeted (0x5209A0 + p), random when both, -1 none
template <class P> void Game<P>::pick_target_player(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        mem.at<int8_t>(obj + 0x2E8) = -1;
        if (gAttackHold && gAttackHold(obj))   // (ours: The Typing of the Dead 0's window, game.h; null in the arcade)
            return;
        int n = 0;
        if (player_in_play(0) && mem.at<uint8_t>(G::player_targeted) == 0) {
            ++n;
            mem.at<int8_t>(obj + 0x2E8) = 0;
        }
        if (player_in_play(1) && mem.at<uint8_t>(G::player_targeted + 1) == 0) {
            ++n;
            mem.at<int8_t>(obj + 0x2E8) = 1;
        }
        if (n == 2)
            mem.at<int8_t>(obj + 0x2E8) = int8_t(int32_t(rand()) % 2);
    }
}

// 0x2F830 pick_target_player_any: the same without the "already targeted" test
template <class P> void Game<P>::pick_target_player_any(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        mem.at<int8_t>(obj + 0x2E8) = -1;
        int n = 0;
        if (player_in_play(0)) {
            ++n;
            mem.at<int8_t>(obj + 0x2E8) = 0;
        }
        if (player_in_play(1)) {
            ++n;
            mem.at<int8_t>(obj + 0x2E8) = 1;
        }
        if (n == 2)
            mem.at<int8_t>(obj + 0x2E8) = int8_t(int32_t(rand()) % 2);
    }
}

// 0x6A6C0 pick_target_player_rand: +0x2E8 = the solo player (0x51F700), or rand & 1 with two
template <class P> void Game<P>::pick_target_player_rand(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        uint8_t t = mem.at<uint8_t>(G::solo_player_is_p2);
        if (mem.at<int16_t>(G::players_in_game) == 2)
            t = uint8_t(rand() & 1);
        mem.at<uint8_t>(obj + 0x2E8) = t;
    }
}

// 0x347B0 spline_segment_init(buf, points): the three points, then the quadratic B-spline's control points (2 p0 - p1,
// p1, 2 p2 - p1) at +0x24
template <class P> void Game<P>::spline_segment_init(uint32_t buf, uint32_t points)
{
    if constexpr (P::pc) {
        (void)buf, (void)points;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        float p[9];
        for (uint32_t k = 0; k < 9; ++k) {
            p[k] = mem.f32(points + 4 * k);
            mem.set32(buf + 4 * k, fbits(p[k]));
        }
        for (uint32_t k = 0; k < 3; ++k) {
            mem.set32(buf + 0x24 + 4 * k, fbits((p[k] + p[k]) - p[3 + k]));
            mem.set32(buf + 0x30 + 4 * k, fbits(p[3 + k]));
            mem.set32(buf + 0x3C + 4 * k, fbits((p[6 + k] + p[6 + k]) - p[3 + k]));
        }
    }
}

// 0x34880 spline_eval(buf, t, out): ((1-t)^2 / 2) c0 + ((1-t) t + 1/2) c1 + (t^2 / 2) c2 (control points at +0x24)
template <class P> void Game<P>::spline_eval(uint32_t buf, float t, uint32_t out)
{
    const float u = 1.0f - t;
    const float a = (u * u) * 0.5f, b = (u * t) + 0.5f, c = (t * t) * 0.5f;
    for (uint32_t k = 0; k < 3; ++k)
        mem.set32(out + 4 * k, fbits((a * mem.f32(buf + 0x24 + 4 * k) + b * mem.f32(buf + 0x30 + 4 * k)) +
                                     c * mem.f32(buf + 0x3C + 4 * k)));
}

// 0x2DB70 spline_advance(obj, buf, speed, step, outA, outB): outA / outB = the object's angles +0x2C / +0x30; while
// the spline point at t (+0x33C) is nearer than speed x 1.1 on the floor (coprocessor 0x1F), t += step; then the
// angles toward that point (coprocessor 0x0F: outB = the first, outA = the second, folded by half a turn)
template <class P> void Game<P>::spline_advance(uint32_t obj, uint32_t buf, uint32_t speed, uint32_t step, uint32_t outA, uint32_t outB)
{
    if constexpr (P::pc) {
        (void)obj, (void)buf, (void)speed, (void)step, (void)outA, (void)outB;   // not ported (vtable only)
    } else {
        mem.set32(outA, mem.u32(obj + 0x2C));
        mem.set32(outB, mem.u32(obj + 0x30));
        const float lim = ffrom(speed) * ffrom(0x3F8CCCCDu);
        LocalScope scope(*this);
        const uint32_t pt = local_alloc(0x10);
        auto dist = [&]() {
            spline_eval(buf, mem.f32(obj + 0x33C), pt);
            if (pc_path()) {   // PC Mode / Boss Mode: THOTD.EXE 0x4677FC: its table square root of dx dx + dz dz (pmbody)
                const float dx = mem.f32(pt) - mem.f32(obj + 0x20), dz = mem.f32(pt + 8) - mem.f32(obj + 0x28);
                return pm::fast_sqrt(dx * dx + dz * dz);
            }
            uint32_t in[6] = {mem.u32(obj + 0x20), 0, mem.u32(obj + 0x28), mem.u32(pt), 0, mem.u32(pt + 8)};
            Tgp::Out o;
            copro(0x1F, in, 6, o);
            return ffrom(o.w[0]);
        };
        if (!(dist() < lim))
            return;
        float d;
        do {
            d = dist();
            mem.set32(obj + 0x33C, fbits(mem.f32(obj + 0x33C) + ffrom(step)));
        } while (d < lim);
        uint32_t v[3] = {fbits(mem.f32(pt) - mem.f32(obj + 0x20)), fbits(mem.f32(pt + 4) - mem.f32(obj + 0x24)),
                         fbits(mem.f32(pt + 8) - mem.f32(obj + 0x28))};
        Tgp::Out o;
        if (pc_path()) {   // the file's pc_vec_angles copy 0x465DEC (folded already)
            uint32_t yaw = 0, pitch = 0;
            pm::vec_angles(ffrom(v[0]), ffrom(v[1]), ffrom(v[2]), yaw, pitch);
            o.put(yaw);
            o.put(pitch);
        } else {
            copro(0x0F, v, 3, o);
        }
        mem.set32(outB, o.w[0]);
        mem.set32(outA, o.w[1]);
        const uint32_t s = mem.at<uint16_t>(outA);
        if (((s << 1) ^ s) & 0x8000) {
            mem.set32(outA, 0x8000 - mem.u32(outA));
            mem.set32(outB, mem.u32(outB) + 0x8000);
        }
    }
}

// 0x27510 is_demo: the attract demo runs (not in the gun test): mode 4 sub-mode 8, or table 0xAB300 by the mode
template <class P> uint32_t Game<P>::is_demo()
{
    if constexpr (P::pc) {
        return 0;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        if (mem.u32(G::gun_test_mode) != 0)
            return 0;
        const int16_t mode = mem.at<int16_t>(G::game_mode);
        if (mode != 4)
            return mem.u32(0xAB300 + uint32_t(int32_t(mode)) * 4);
        return mem.u32(G::game_submode) == 8 ? 1 : 0;
    }
}

} // namespace hotd::port
