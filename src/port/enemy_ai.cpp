// Enemies: the standard enemy's approach / attack state machine (enemy_ai), A and PC (PA / PM), read from the
// disassembly of both (Ghidra's C loses the coprocessor distance and turn calls). Distances: A coprocessor 0x1F on the
// floor plane (y = 0), PC its table square root; the turn: A coprocessor, PC its software matrix helpers (and a
// minimum slowdown of 4); the step forward at the motion's speed is in the arcade's enemy_ai only.
#include "port/game.h"

#include <climits>
#include <cmath>
#include <cstring>

namespace hotd::port {

// coprocessor 0x1F: distance from (ax, 0, az) to (bx, 0, bz)
template <class P> float Game<P>::floor_distance(float ax, float az, float bx, float bz)
{
    uint32_t in[6] = {fbits(ax), 0, fbits(az), fbits(bx), 0, fbits(bz)};
    Tgp::Out o;
    copro(0x1F, in, 6, o);
    return ffrom(o.w[0]);
}

// 0x76AE0 / PC 0x4D91A8 (enemy): the target update: target_pos = the body x / z of the solo player (players_seen 1)
// or of the wanted player (2), y 0; else unchanged (enemy_ai has the same code inline in the arcade)
template <class P> void Game<P>::kind11_leap_move(uint32_t obj)
{
    Enemy& e = mem.at<Enemy>(obj);
    const int32_t seen = e.players_seen;
    int32_t p;
    if (seen == 1)
        p = mem.at<int8_t>(G::solo_player_is_p2);
    else if (seen == 2)
        p = e.slot_wanted;
    else
        return;
    const uint32_t body = mem.u32(G::players + uint32_t(p) * 0xCC + 0xBC);
    const float x = mem.f32(body + ob(0x20));
    e.target_pos[1] = 0.0f;
    e.target_pos[0] = x;
    e.target_pos[2] = mem.f32(body + ob(0x28));
}

// the floor distance from (ax, az) to (bx, bz): A coprocessor 0x1F, PC pc_fast_sqrt3((bx - ax)^2 + (bz - az)^2)
template <class P> float Game<P>::enemy_distance(float ax, float az, float bx, float bz)
{
    if constexpr (P::pc) {
        const float dx = bx - ax, dz = bz - az;
        return pc_fast_sqrt3(dx * dx + dz * dz);
    } else {
        if (pc_path()) {   // PC Mode on the arcade logic: the PC's table square root (docs/pcmode.md row 10)
            const float dx = bx - ax, dz = bz - az;
            return pm_fast_sqrt(dx * dx + dz * dz);
        }
        return floor_distance(ax, az, bx, bz);
    }
}

// the PC's turn toward target_pos (PC 0x4DA030 enemy_ai, 0x52307C kind11_leap, each with its own copies of the
// helpers): the heading (0, 0, 10) turned by the yaw; the angles of (target - pos); their cosine c; slowdown (1 - c)
// 60 (c <= 0: 60), in enemy_ai at least 4 (kind 11: no minimum); pc_angle_turn (backwards: 4 times slower)
template <class P> void Game<P>::enemy_turn_pc(uint32_t obj, bool backwards, bool atLeast4)
{
    auto E = [&]() -> Enemy& { return mem.at<Enemy>(obj); };
    if constexpr (!P::pc)
        return;   // PC only
    else {
    pc_matrix_push();
    pc_matrix_identity();
    pc_matrix_rotate_y(uint32_t(E().ang[1]));
    float hx = 0.0f, hy = 0.0f, hz = 0.0f;
    pc_matrix_transform2(0.0f, 0.0f, 10.0f, hx, hy, hz);
    pcm_pop();
    const float dz = E().target_pos[2] - E().pos[2];
    const float dx = E().target_pos[0] - E().pos[0];
    uint32_t yaw = 0, pitch = 0;
    pc_vec_angles2(dx, 0.0f, dz, yaw, pitch);
    const float c = pc_vec_cos(hx, hy, hz, dx, 0.0f, dz);
    yaw &= 0xFFFF;
    float slow = !(0.0f >= c) ? (1.0f - c) * 60.0f : 60.0f;
    if (atLeast4 && !(slow >= 4.0f))
        slow = 4.0f;
    if (backwards) {
        const int32_t base = int32_t((uint32_t(E().ang[1]) + 0x8000) & 0xFFFF);
        E().ang[1] = int32_t((uint32_t(pc_angle_turn(base, yaw, 1.0f, slow * 4.0f)) + 0x8000) & 0xFFFF);
    } else {
        E().ang[1] = pc_angle_turn(E().ang[1], yaw, 1.0f, slow);
    }
    }
}

// the arcade's turn toward target_pos (inline in enemy_ai and kind11_leap): heading h = the yaw applied to (0, 0, 10);
// a = the angles of (target - pos); the closer the two directions (c = cos between them), the slower the turn: yaw +=
// delta / (1 + 60 (1 - c)) (c <= 0: 60); backwards (enemy_ai sub-state 3): the yaw + 0x8000 turned 4 times slower
template <class P> void Game<P>::enemy_turn_a(uint32_t obj, bool backwards, bool atLeast4)
{
    auto E = [&]() -> Enemy& { return mem.at<Enemy>(obj); };
    Tgp::Out o, none;
    copro(0x21, nullptr, 0, none);
    uint32_t a1[1] = {uint32_t(E().ang[1])};
    copro(0x15, a1, 1, none);
    uint32_t v[3] = {0, 0, fbits(10.0f)};
    copro(0x1A, v, 3, o);
    const uint32_t h0 = o.w[0], h1 = o.w[1], h2 = o.w[2];
    copro(0x06, nullptr, 0, none);
    const float dx = E().target_pos[0] - E().pos[0];
    const float dz = E().target_pos[2] - E().pos[2];
    uint32_t d[3] = {fbits(dx), 0, fbits(dz)};
    copro(0x0F, d, 3, o);
    uint32_t A0 = o.w[0], A1 = o.w[1];
    uint32_t s = uint32_t(int32_t(int16_t(A1 & 0xFFFF)));
    if (((s << 1) ^ s) & 0x8000) {
        A1 = 0x8000 - A1;
        A0 = A0 + 0x8000;
    }
    uint32_t c[6] = {h0, h1, h2, fbits(dx), 0, fbits(dz)};
    copro(0x89, c, 6, o);
    const float cosv = ffrom(o.w[0]);
    A0 &= 0xFFFF;
    float slow = cosv > 0.0f ? (1.0f - cosv) * 60.0f : 60.0f;
    if (atLeast4 && !(slow >= 4.0f))   // PC Mode (the PC's enemy_ai 0x4DA030): the slowdown is at least 4
        slow = 4.0f;
    auto turn = [&](uint32_t base, float sl) {
        uint32_t delta = (A0 - base) & 0xFFFF;
        float q;
        if (int32_t(delta) > 0x7FFF)
            q = -float(int32_t(0x10000 - delta)) / (sl + 1.0f);
        else
            q = float(int32_t(delta)) / (sl + 1.0f);
        return int32_t(q + float(int32_t(base)));
    };
    if (backwards) {
        const uint32_t base = (uint32_t(E().ang[1]) + 0x8000) & 0xFFFF;
        E().ang[1] = int32_t((uint32_t(turn(base, slow * 4.0f)) + 0x8000) & 0xFFFF);
    } else {
        E().ang[1] = turn(uint32_t(E().ang[1]), slow);
    }
}

// 0x77B20 / PC 0x4DA030 (enemy). States (+0x5C): 1 wait until the target is within 70 (and delay 0), then walk (2) or, with a front
// attack slot (<= 3), step 3; 2 walk until a front slot; 3 approach to 40, take a slot (5) or keep walking (4); 4 take a
// slot when one frees; 5 within 25: pick an attack (rand) against a free player and attack (8) or lunge (7), else
// wait (6); 6 at the slot: 1 in 32 frames attack the same way; 7 lunge until within reach; 8 the attack's hit frame
// hits the player (player_hit; with flags bit 21 the enemy leaves afterwards); 9 retreat until 25 away, then 6.
// Then (delay 0) the turn toward the target and the step forward at the motion's speed.
template <class P> void Game<P>::enemy_ai(uint32_t obj)
{
    auto E = [&]() -> Enemy& { return mem.at<Enemy>(obj); };
    // the target: the solo player's or the wanted player's body (players_seen 1 / 2)
    auto updateTarget = [&]() { kind11_leap_move(obj); };   // inline in the arcade, PC 0x4D91A8 / 0x4D920C
    auto targetDistance = [&]() { return enemy_distance(E().pos[0], E().pos[2], E().target_pos[0], E().target_pos[2]); };
    auto list = [&]() {
        uint32_t row = mem.u32(G::enemy_motion_lists + uint32_t(int32_t(E().type)) * 4);
        return mem.u32(row + uint32_t(int32_t(E().action)) * 4);
    };
    auto attackRec = [&]() {
        uint32_t row = mem.u32(G::enemy_attacks + uint32_t(int32_t(E().type)) * 4);
        return mem.u32(row + uint32_t(int32_t(E().action)) * 4) + uint32_t(int32_t(E().attack)) * 24;
    };
    // the walk motion with a random start frame (states 2 -> 3, 4 -> 5)
    auto walkOn = [&]() {
        E().substate = E().walk_variant == 1 ? 2 : 1;
        int32_t wv = E().walk_variant;
        uint32_t m;
        int32_t r;
        if (!(1 >= wv)) {
            r = int32_t(rand());
            m = mem.u32(G::enemy_walk_motions_b + uint32_t(wv) * 4);
        } else {
            uint32_t l = list();
            int32_t sub = E().substate;
            r = int32_t(rand());
            m = mem.u32(l + uint32_t(sub) * 8);
        }
        set_motion_blend(obj, m, r % 10, 10);
    };
    auto standMotion = [&]() {
        int32_t wv = E().walk_variant;
        return !(1 >= wv) ? mem.u32(G::enemy_stand_motions_b + uint32_t(wv) * 4) : mem.u32(list());
    };
    // the player to attack: the solo player, or (2 players) the one the attack slot number names, if not targeted yet
    auto freePlayer = [&](bool& found) -> uint8_t {
        found = false;
        uint8_t who = 0xFF;
        if (gAttackHold && gAttackHold(obj))   // (ours: The Typing of the Dead 0's window, game.h; null in the arcade)
            return who;
        if (mem.at<uint16_t>(G::players_in_game) == 1) {
            if (mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(mem.at<int8_t>(G::solo_player_is_p2)))) == 0) {
                who = mem.at<uint8_t>(G::solo_player_is_p2);
                found = true;
            }
        } else if (mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(E().attack_slot))) == 0) {
            found = true;
            who = uint8_t(E().attack_slot);
        }
        return who;
    };
    auto chooseAttack = [&]() {
        uint32_t parts = E().parts_lost & 7u;
        int32_t r = (int32_t(rand()) >> 4) % 10;
        uint32_t row = mem.u32(G::enemy_attack_choice + uint32_t(int32_t(E().type)) * 4);
        uint32_t tbl = mem.u32(row + uint32_t(int32_t(E().action)) * 4);
        E().attack = int8_t(mem.at<uint8_t>(tbl + (parts & 7) * 10 + uint32_t(r)));
    };
    auto attackNow = [&](bool flag31) {
        uint32_t rec = attackRec();
        set_motion_blend(obj, mem.u32(rec), 0, 5);
        E().state = 8;
        E().substate = 4;
        if (flag31)
            E().flags |= 1u << 31;
        enemy_voice(obj, 3);
    };
    auto setTarget = [&](uint8_t who) {
        E().target = int8_t(who);
        mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(int8_t(who)))) = 1;
    };
    auto toWait = [&]() {
        E().substate = 0;
        E().state = 6;
        set_motion_blend(obj, mem.u32(list()), 0, 10);
    };

    const uint32_t k = uint32_t(int32_t(int16_t(uint16_t(E().state) - 1)));
    if (k <= 8) {
        switch (k) {
        case 0: {   // state 1
            updateTarget();
            float d = targetDistance();
            if (E().delay != 0 || d > 70.0f)
                break;
            if (!(3 < E().attack_slot)) {
                E().state = 3;
                break;
            }
            E().state = 2;
            E().substate = 0;
            set_motion_blend(obj, standMotion(), 0, 10);
            break;
        }
        case 1:     // state 2
            updateTarget();
            if (3 < E().attack_slot)
                break;
            E().state = 3;
            walkOn();
            break;
        case 2: {   // state 3
            updateTarget();
            float d = targetDistance();
            if (d > 40.0f)
                break;
            if (enemy_attack_slot_take(obj) != 0) {
                E().state = 5;
                break;
            }
            E().state = 4;
            E().substate = 0;
            set_motion_blend(obj, standMotion(), 0, 10);
            break;
        }
        case 3:     // state 4
            enemy_target_distance(obj);
            if (enemy_attack_slot_take(obj) == 0)
                break;
            E().state = 5;
            walkOn();
            break;
        case 4: {   // state 5
            updateTarget();
            float d = targetDistance();
            if (d > 25.0f)
                break;
            bool found;
            uint8_t who = freePlayer(found);
            if (!found || mem.u32(G::shake_timer) != 0 || mem.at<uint16_t>(G::players_in_game) == 0) {
                toWait();
                break;
            }
            if (E().ai_at_once != 0) {
                (void)rand();
                E().ai_at_once = 0;
                E().attack = 7;
            } else {
                chooseAttack();
            }
            uint32_t rec = attackRec();
            if (d > mem.f32(rec + 0xC)) {
                uint32_t lunge = mem.u32(rec + 4);
                if (int32_t(E().motion) != int32_t(lunge))
                    set_motion_blend(obj, lunge, 0, 10);
                E().state = 7;
            } else {
                attackNow(false);
            }
            setTarget(who);
            break;
        }
        case 5: {   // state 6
            enemy_target_distance(obj);
            int32_t slot = E().attack_slot, want = E().slot_wanted;
            if (slot != want && mem.at<uint8_t>(G::attack_slots + uint32_t(want) * 20) == 0) {
                mem.at<uint8_t>(G::attack_slots + uint32_t(slot) * 20) = 0;
                mem.set32(G::attack_slots + uint32_t(int32_t(E().attack_slot)) * 20 + 4, 0);
                mem.at<uint8_t>(G::attack_slots + uint32_t(E().slot_wanted) * 20) = 1;
                mem.set32(G::attack_slots + uint32_t(E().slot_wanted) * 20 + 4, obj);
                E().attack_slot = int8_t(uint8_t(E().slot_wanted));
            }
            float d = targetDistance();
            bool found;
            uint8_t who = freePlayer(found);
            int32_t r = int32_t(rand()) >> 4;
            if (r % 32 != 1 || !found || mem.u32(G::shake_timer) != 0)
                break;
            const uint16_t pig = mem.at<uint16_t>(G::players_in_game);
            if (pig == 0)
                break;
            if (uint8_t(E().attack_slot) == 1 && pig == 1 && mem.at<uint8_t>(G::attack_side) == 0 &&
                1 < int32_t(mem.at<int16_t>(G::enemies_alive)))
                break;
            chooseAttack();
            uint32_t rec = attackRec();
            if (d > mem.f32(rec + 0xC)) {
                set_motion_blend(obj, mem.u32(rec + 4), 0, 10);
                E().state = 7;
                E().substate = 1;
            } else {
                attackNow(true);
            }
            setTarget(who);
            break;
        }
        case 6: {   // state 7
            updateTarget();
            float d = targetDistance();
            uint32_t rec = attackRec();
            if (d > mem.f32(rec + 0xC))
                break;
            attackNow(true);
            break;
        }
        case 7: {   // state 8
            updateTarget();
            uint32_t rec = attackRec();
            if (int32_t(E().frame) != int32_t(mem.u32(rec + 0x10)))
                break;
            uint8_t needs = mem.at<uint8_t>(rec + 0x15);
            if ((E().parts_lost & (needs & 7)) == needs)
                break;
            const int8_t side = mem.at<int8_t>(rec + 0x14);
            if (((E().flags >> 21) & 1) == 0) {
                player_hit(E().target, 1, side);
                break;
            }
            player_hit(E().target, 0, side);
            kind10_remove(obj);
            if constexpr (P::pc)
                break;
            return;
        }
        case 8: {   // state 9
            const uint16_t pig = mem.at<uint16_t>(G::players_in_game);
            float tx, tz;
            if (pig == 1 || pig == 2) {
                int32_t p = pig == 1 ? int32_t(mem.at<int8_t>(G::solo_player_is_p2)) : E().slot_wanted;
                uint32_t body = mem.u32(G::players + uint32_t(p) * 0xCC + 0xBC);
                float d = enemy_distance(E().pos[0], E().pos[2], mem.f32(body + ob(0x20)), mem.f32(body + ob(0x28)));
                if (d < 25.0f)
                    break;
                toWait();
                tx = mem.f32(body + ob(0x20));
                tz = mem.f32(body + ob(0x28));
            } else {
                float d = enemy_distance(E().pos[0], E().pos[2], mem.f32(G::camera_pos), mem.f32(G::camera_pos + 8));
                if (d < 25.0f)
                    break;
                toWait();
                tx = mem.f32(G::camera_pos);
                tz = mem.f32(G::camera_pos + 8);
            }
            E().target_pos[1] = 0.0f;
            E().target_pos[0] = tx;
            E().target_pos[2] = tz;
            break;
        }
        }
    }

    if constexpr (P::pc) {
        if (E().delay != 0) {
            E().delay = E().delay - 1;
            return;
        }
        enemy_turn_pc(obj, uint16_t(E().substate) == 3, true);
        return;
    }
    Tgp::Out o, none;
    if (E().delay != 0) {
        E().delay = E().delay - 1;
    } else if (pc_path()) {
        // PC Mode (docs/pcmode.md rows 3 / 10): the PC's turn (enemy_turn_pc on plain values: its sin / cos and
        // arctangent tables, table square root, x87 float32 order) with its minimum slowdown 4
        E().ang[1] = pm::enemy_turn(E().ang[1], E().pos, E().target_pos, E().substate == 3, true);
    } else {
        enemy_turn_a(obj, E().substate == 3);
    }
    // step forward at the motion's speed (state 7: the lunge speed)
    float speed;
    if (uint16_t(E().state) == 7)
        speed = mem.f32(attackRec() + 8);
    else
        speed = mem.f32(list() + uint32_t(int32_t(E().substate)) * 8 + 4);
    if (speed == 0.0f)
        return;
    copro(0x21, nullptr, 0, none);
    uint32_t a1[1] = {uint32_t(E().ang[1])};
    copro(0x15, a1, 1, none);
    uint32_t v[3] = {0, 0, fbits(speed)};
    copro(0x1A, v, 3, o);
    E().pos[0] = E().pos[0] + ffrom(o.w[0]);
    E().pos[2] = E().pos[2] + ffrom(o.w[2]);
    copro(0x06, nullptr, 0, none);
}

// PC 0x4D8D7C (angle, target, k, slow): angle + delta k / (k + slow), delta = (target - angle) & 0xFFFF taken the
// short way round (above 0x7FFF: -(0x10000 - delta)), x87 float32, truncated
template <class P> int32_t Game<P>::pc_angle_turn(int32_t angle, uint32_t target, float k, float slow)
{
    const uint32_t delta = (target - uint32_t(angle)) & 0xFFFF;
    const float den = k + slow;
    float q;
    if (int32_t(delta) > 0x7FFF)
        q = -(float(int32_t(0x10000 - delta)) * k);
    else
        q = float(int32_t(delta)) * k;
    q = q / den;
    const float r = pm::add_round(double(q), double(angle));   // fild angle; fadd: the exact sum rounded once
    const double t = std::trunc(double(r));
    return t > -2147483649.0 && t < 2147483648.0 ? int32_t(t) : INT32_MIN;
}

// PC 0x4D7A0C (a, b): the cosine between two vectors: ((a0 b0 + a1 b1) + a2 b2) / (|b| |a|), lengths by
// pc_fast_sqrt3((x x + y y) + z z)
template <class P> float Game<P>::pc_vec_cos(float a0, float a1, float a2, float b0, float b1, float b2)
{
    const float dot = (a0 * b0 + a1 * b1) + a2 * b2;
    const float la = pc_fast_sqrt3((a0 * a0 + a1 * a1) + a2 * a2);
    const float lb = pc_fast_sqrt3((b0 * b0 + b1 * b1) + b2 * b2);
    return dot / (lb * la);
}

// PC 0x4D7B4C: the current matrix = the identity (pc_identity_matrix)
template <class P> void Game<P>::pc_matrix_identity()
{
    if constexpr (P::pc) {
        const uint32_t m = mem.u32(G::pc_matrix_ptr);
        for (uint32_t i = 0; i < 12; ++i)
            mem.set32(m + 4 * i, mem.u32(G::pc_identity_matrix + 4 * i));
    } else {
        pm_cur().identity();   // PC Mode on the arcade logic: the runtime's own stack (Game::pm_mstack)
    }
}

// PC 0x4D74FC (angle): the current matrix turned about y: s, c from pc_trig_table; each row (a, b, d) becomes
// (c a + s d, b, c d - s a)
template <class P> void Game<P>::pc_matrix_rotate_y(uint32_t angle)
{
    if constexpr (P::pc) {
        const int32_t i = (int32_t(int16_t(angle & 0xFFFF)) / 16) & 0xFFF;
        const float s = mem.f32(G::pc_trig_table + uint32_t(i) * 8), c = mem.f32(G::pc_trig_table + uint32_t(i) * 8 + 4);
        const uint32_t m = mem.u32(G::pc_matrix_ptr);
        for (uint32_t row = 0; row < 3; ++row) {
            const uint32_t r = m + row * 12;
            const float a = mem.f32(r), d = mem.f32(r + 8);
            const float na = s * d + c * a, nd = c * d + (-s) * a;
            mem.set32(r, fbits(na));
            mem.set32(r + 8, fbits(nd));
        }
    } else {
        pm_cur().rotate_y(angle);
    }
}

// PC 0x4D70F8 / 0x4D71E0: copies of pc_matrix_transform / pc_vec_angles in the enemy file (the second with
// pc_fast_sqrt3, the same table)
template <class P> void Game<P>::pc_matrix_transform2(float x, float y, float z, float& ox, float& oy, float& oz)
{
    pc_matrix_transform(x, y, z, ox, oy, oz);
}
template <class P> void Game<P>::pc_vec_angles2(float x, float y, float z, uint32_t& yaw, uint32_t& pitch)
{
    pc_vec_angles(x, y, z, yaw, pitch);
}

// boundaries: not ported yet

} // namespace hotd::port
