// The event script interpreter (arcade 0x66A10..0x67C00 and the spawn opcodes 0x31500..0x31A70; PC 0x4C0CF0..0x4C1AFA
// and 0x46E548..0x46E950), read from both disassemblies. The event task (evt_task) runs evt_run once a frame: it calls
// the handler of each instruction (evt_handlers, by opcode) until one sets evt_yield. Instructions are an int32
// opcode and int32 operands (docs/formats.md §29); evt_pc is the address of the current one. The WAIT_* opcodes
// yield on their first call and move on once their condition holds and a player is in play (evt_players_active);
// NEXTEV moves to the next event section (scene_number) or, at the end of an event, to the next event by the route
// table (route_branches: type 0 jump, 1 by route_choice, 2 next). CCTB only queues its instruction in a ring for the
// camera task; CAMDR / CAMDR2 / SCNINIT call the camera drive modes (camdr_modes, boundaries until the camera is
// ported). PC differences: the event lists come from EVTALL.RBH (pc_event_lists), the chapter start loads the PC
// chapter files and fades the music, debug prints where events are skipped, the CCTB ring is written one slot on,
// GGCOL / GGSET / TEXCHG only skip their operands, WAIT_CAMFRM clears evt_yield when skipped, SCNINIT starts no
// sprite clear, NEXTEV sends no sound commands at a chapter's end, the spawn opcodes skip kinds with no routine
// (ENTB2 / ENTB3 / ENTBE) and ENTB places through the PC's software matrices.
#include "port/game.h"

namespace hotd::port {

// arcade: event_lists in the program ROM; PC: the lists of EVTALL.RBH
template <class P> uint32_t Game<P>::evt_event_lists()
{
    if constexpr (P::pc)
        return mem.u32(G::pc_event_lists);
    else
        return G::event_lists;
}

// 0x66A10: the event task; the route choice cleared, the first section (1; the attract demo: 0) of the chapter's
// current event, the interpreter's state reset (evt_state 2 = the camera's job done)
template <class P> void Game<P>::evt_chapter_start()
{
    task_create(F::evt_task, P::pc ? 0x28 : 0x20);
    mem.at<int8_t>(G::route_choice) = 0;
    const int32_t chapter = mem.at<int8_t>(G::chapter);
    if constexpr (P::pc) {
        pc_chapter_files_load(chapter, 3);
        mem.set32(G::pc_load_state, 3);
        if (mem.at<uint8_t>(G::chapter) == 6)
            pc_music_fade_ending();
        else
            pc_music_fade_chapter();
    } else {
        mem.at<int8_t>(G::scene_number) = int8_t(mem.at<uint16_t>(G::game_mode) != 3 && !startAtSection0);
    }
    startAtSection0 = false;
    const uint32_t event = mem.u32(mem.u32(evt_event_lists() + uint32_t(chapter) * 4) +
                                   uint32_t(int32_t(mem.at<int8_t>(G::event_index))) * 4);
    const uint32_t pc = mem.u32(event + uint32_t(int32_t(mem.at<int8_t>(G::scene_number))) * 4);
    mem.set32(G::evt_last_pc, 0);
    mem.set32(G::evt_cctb_ring_pos, 0);
    mem.set32(G::evt_cctb_ring_read, 0);
    mem.set32(G::evt_camjobs, 0);
    mem.set32(G::evt_yield, 0);
    mem.set32(G::evt_skip_on, 0);
    mem.set32(G::evt_state, 2);
    mem.set32(G::evt_skip, 0);
    mem.set32(G::evt_pc, pc);
}

// 0x66AC0: the task's routine becomes evt_run
template <class P> void Game<P>::evt_task(uint32_t task)
{
    mem.set32(task, F::evt_run);
}

// 0x66AD0: evt_players_active = a player in play (lives > 0 and state 5) or the attract demo; then the handlers of
// the instructions until one yields
template <class P> void Game<P>::evt_run(uint32_t task)
{
    (void)task;
    const Player& p1 = mem.at<Player>(G::players);
    const Player& p2 = mem.at<Player>(G::players + uint32_t(sizeof(Player)));
    uint32_t active = 0;
    if ((p1.lives > 0 && p1.state == 5) || (p2.lives > 0 && p2.state == 5) || is_demo() != 0)
        active = 1;
    if (coop4_on())   // 4P: players 3 / 4 in play keep the events going too (with 1 and 2 out: a soft-lock before)
        for (int s = 2; s < 4; ++s) {
            const Player& ps = mem.at<Player>(player_rec(s));
            if (ps.lives > 0 && ps.state == 5)
                active = 1;
        }
    mem.set32(G::evt_players_active, active);
    do {
        const uint32_t pc = mem.u32(G::evt_pc);
        const uint32_t op = mem.u32(pc);
        if (pc != mem.u32(G::evt_last_pc)) {
            mem.set32(G::evt_last_pc, pc);
            evt_attract_restore(pc);   // Attract Gore: the PC's prologue spawns Revision A dropped (off: nothing)
        }
        mem.set32(G::evt_opcode, op);
        evt_call(mem.u32(G::evt_handlers + op * 4));
    } while (mem.u32(G::evt_yield) == 0);
}

// a callx through evt_handlers / evt_et_handlers (the same routines in both versions, apart from GGCOL / GGSET /
// TEXCHG); nop (opcodes the scripts never use) returns at once
template <class P> void Game<P>::evt_call(uint32_t routine)
{
    if constexpr (P::pc) {
        if (routine == F::evt_skip1)
            return evt_skip1();
    } else {
        if (routine == F::evt_GGSET)
            return evt_GGSET();
        if (routine == F::evt_TEXCHG)
            return evt_TEXCHG();
    }
    switch (routine) {
    case F::nop: return;
    case F::evt_ET1P: return evt_ET1P();
    case F::evt_ET2P: return evt_ET2P();
    case F::evt_ENTBE: return evt_ENTBE();
    case F::evt_ENTB: return evt_ENTB();
    case F::evt_ENTB2: return evt_ENTB2();
    case F::evt_ENTB3: return evt_ENTB3();
    case F::evt_SKPETO: return evt_SKPETO();
    case F::evt_SKPETU: return evt_SKPETU();
    case F::evt_SKIP: return evt_SKIP();
    case F::evt_SCNINIT: return evt_SCNINIT();
    case F::evt_BGTB: return evt_BGTB();
    case F::evt_EV3JP1: return evt_EV3JP1();
    case F::evt_GMTMF: return evt_GMTMF();
    case F::evt_GMTAD: return evt_GMTAD();
    case F::evt_CLRGFST: return evt_CLRGFST();
    case F::evt_WLTB: return evt_WLTB();
    case F::evt_CLTB: return evt_CLTB();
    case F::evt_KGTB: return evt_KGTB();
    case F::evt_CCTB: return evt_CCTB();
    case F::evt_BSFT: return evt_BSFT();
    case F::evt_CAMDR2: return evt_CAMDR2();
    case F::evt_PYPMD: return evt_PYPMD();
    case F::evt_SHDWLT: return evt_SHDWLT();
    case F::evt_CGMFNSH: return evt_CGMFNSH();
    case F::evt_SKYKD: return evt_SKYKD();
    case F::evt_SKYTB: return evt_SKYTB();
    case F::evt_FKRTB: return evt_FKRTB();
    case F::evt_CHGSM: return evt_CHGSM();
    case F::evt_NCHGCF: return evt_NCHGCF();
    case F::evt_CAMDR: return evt_CAMDR();
    case F::evt_GGCOL: return evt_GGCOL();
    case F::evt_REMFRM: return evt_REMFRM();
    case F::evt_WAIT_ENNUM: return evt_WAIT_ENNUM();
    case F::evt_WAIT_FRM: return evt_WAIT_FRM();
    case F::evt_WAIT_ITEM: return evt_WAIT_ITEM();
    case F::evt_WAIT_CAMJOB: return evt_WAIT_CAMJOB();
    case F::evt_WAIT_CAMFRM: return evt_WAIT_CAMFRM();
    case F::evt_WAIT_ENALV: return evt_WAIT_ENALV();
    case F::evt_RNWCJ: return evt_RNWCJ();
    case F::evt_ITEMON: return evt_ITEMON();
    case F::evt_SNDON: return evt_SNDON();
    case F::evt_ATCMD: return evt_ATCMD();
    case F::evt_WAIT: return evt_WAIT();
    case F::evt_NEXTEV: return evt_NEXTEV();
    default: call_routine(routine, 0); return;
    }
}

// a callx through camdr_modes (the camera drive mode's set-up); the PC passes the table offset in eax
template <class P> void Game<P>::evt_camdr_call(int32_t mode)
{
    call_routine(mem.u32(G::camdr_modes + uint32_t(mode) * 4), P::pc ? uint32_t(mode) * 4 + 0x24 : 0);
}

// ET1P / ET2P with the other player count: skip the block (its words up to -1)
template <class P> void Game<P>::evt_skip_block()
{
    uint32_t pc = mem.u32(G::evt_pc);
    do
        pc += 4;
    while (mem.u32(pc) != 0xFFFFFFFFu);
    mem.set32(G::evt_pc, pc + 4);
}

// 0x31500: ET1PE .. ET1P3 = ENTBE .. ENTB3 only with one player in the game
template <class P> void Game<P>::evt_ET1P()
{
    if (mem.at<uint16_t>(G::players_in_game_b) != 1)
        return evt_skip_block();
    evt_call(mem.u32(G::evt_et_handlers + mem.u32(G::evt_opcode) * 4));
}

// 0x31560: ET2PE .. ET2P3 = ENTBE .. ENTB3 only with two players
template <class P> void Game<P>::evt_ET2P()
{
    if (mem.at<uint16_t>(G::players_in_game_b) != 2)
        return evt_skip_block();
    evt_call(mem.u32(G::evt_et_handlers + mem.u32(G::evt_opcode) * 4));
}

// 0x315C0: ENTB rec ... -1: a task per 40-byte record (kind +0 -> obj_kinds): flags +4 | 1 -> +0x58, type +0x24 ->
// +0xCC, action +0x25 -> +0x64, hit points +0x22 -> +0x70 / +0x72, angles +0x14..+0x1C -> +0x2C..+0x34; the position
// +8..+0x10 by placement mode (+0x20 & 3): 0 as it is, 1 through stored matrix 1 (the camera), 2 / 3 the record's
// x / z through it, then along the camera's up vector to the record's height
template <class P> void Game<P>::evt_ENTB()
{
    Tgp::Out none;
    if constexpr (P::pc) {
        pc_matrix_push();
        pcm_load_camera();
    } else {
        const uint32_t slot[1] = {1};
        copro(0x74, slot, 1, none);
    }
    uint32_t pc = mem.u32(G::evt_pc) + 4;
    mem.set32(G::evt_pc, pc);
    while (mem.u32(pc) != 0xFFFFFFFFu) {
        evt_entb_record(mem.u32(pc));
        pc = mem.u32(G::evt_pc) + 4;
        mem.set32(G::evt_pc, pc);
    }
    if constexpr (P::pc)
        pcm_pop();
    else
        copro(0x06, nullptr, 0, none);
    mem.set32(G::evt_pc, mem.u32(G::evt_pc) + 4);
}

// ENTB's body for one record (inside its camera matrix push)
template <class P> void Game<P>::evt_entb_record(uint32_t rec)
{
    Tgp::Out none;
    {
        const uint32_t t = task_create(mem.u32(G::obj_kinds + mem.u32(rec) * 4), P::pc ? 0x3D0 : 0x3C8);
        task_clear_work(t);
        of<uint8_t>(t, 0x3C4) &= uint8_t(0xFD);
        of<uint32_t>(t, 0x58) = mem.u32(rec + 4) | 1;
        of<uint16_t>(t, 0xCC) = mem.at<uint8_t>(rec + 0x24);
        of<uint16_t>(t, 0x64) = mem.at<uint8_t>(rec + 0x25);
        const uint16_t hp = mem.at<uint16_t>(rec + 0x22);
        of<uint16_t>(t, 0x72) = hp;
        of<uint16_t>(t, 0x70) = hp;
        const uint32_t mode = mem.at<uint16_t>(rec + 0x20) & 3;
        const float rx = mem.f32(rec + 8), ry = mem.f32(rec + 12), rz = mem.f32(rec + 16);
        uint32_t pos[3] = {mem.u32(rec + 8), mem.u32(rec + 12), mem.u32(rec + 16)};
        if (mode == 1) {
            if constexpr (P::pc) {
                float o[3];
                pc_point_transform(rx, ry, rz, o[0], o[1], o[2]);
                for (int i = 0; i < 3; ++i)
                    pos[i] = fbits(o[i]);
            } else {
                Tgp::Out o;
                copro(0x1A, pos, 3, o);
                for (int i = 0; i < 3; ++i)
                    pos[i] = o.w[i];
            }
        } else if (mode != 0) {
            float p[3], u[3];
            if constexpr (P::pc) {
                pc_point_transform(rx, 0.0f, rz, p[0], p[1], p[2]);
                pc_matrix_push();
                const uint32_t m = mem.u32(G::pc_matrix_ptr);
                mem.set32(m + 0x24, 0);
                mem.set32(m + 0x28, 0);
                mem.set32(m + 0x2C, 0);
                pc_point_transform(0.0f, 1.0f, 0.0f, u[0], u[1], u[2]);
                mem.set32(G::pc_matrix_ptr, m - 0x30);
            } else {
                Tgp::Out o, up;
                const uint32_t floor[3] = {pos[0], 0, pos[2]};
                copro(0x1A, floor, 3, o);
                copro(0x05, nullptr, 0, none);
                const uint32_t zero[3] = {0, 0, 0};
                copro(0x88, zero, 3, none);
                const uint32_t y1[3] = {0, fbits(1.0f), 0};
                copro(0x1A, y1, 3, up);
                for (int i = 0; i < 3; ++i) {
                    p[i] = ffrom(o.w[i]);
                    u[i] = ffrom(up.w[i]);
                }
            }
            const float k = (ry - p[1]) / u[1];
            pos[0] = fbits(k * u[0] + p[0]);
            pos[2] = fbits(k * u[2] + p[2]);
            if constexpr (!P::pc)
                copro(0x06, nullptr, 0, none);
        }
        of<uint32_t>(t, 0x20) = pos[0];
        of<uint32_t>(t, 0x24) = pos[1];
        of<uint32_t>(t, 0x28) = pos[2];
        of<uint32_t>(t, 0x2C) = mem.u32(rec + 0x14);
        of<uint32_t>(t, 0x30) = mem.u32(rec + 0x18);
        of<uint32_t>(t, 0x34) = mem.u32(rec + 0x1C);
        evt_spawned(t, rec);   // (Survival Mode's lead-in; nothing otherwise)
    }
}

// Attract Gore (Gameplay option, port::gAttractGore; user 2026-11-03): the prologue's sewer scene (mode 9, scene 6) as
// the 1998 PC plays it. The PC's EVTALL.RBH has "ENTB 0xFE310 0xFE340" there (list 5 event 0, offset 668, between
// GGCOL 9 and GGSET 24): two kind-118 corpses (type 17, the dead researchers sitai_ken 0x5E8 / 0x5E6 with their blood).
// Revision A took the instruction out of the ROM's script (GGCOL 9 106 at 0xFDC54 is followed by GGSET 24 at 0xFDC64)
// but left both records in the ROM, referenced by nothing. Here: ENTB on them when the script reaches that GGSET.
template <class P> void Game<P>::evt_attract_restore(uint32_t pc)
{
    if constexpr (!P::pc) {
        if (!gAttractGore || pc != 0xFDC64 || mem.at<int16_t>(G::game_mode) != 9 || mem.at<int8_t>(G::chapter) != 5 ||
            mem.u32(G::obj_kinds + 0x76 * 4) != 0x5C0A0 || mem.u32(0xFE310) != 0x76 || mem.u32(0xFE340) != 0x76)
            return;
        Tgp::Out none;
        const uint32_t slot[1] = {1};
        copro(0x74, slot, 1, none);
        evt_entb_record(0xFE310);
        evt_entb_record(0xFE340);
        copro(0x06, nullptr, 0, none);
    } else {
        (void)pc;   // the PC's script has the instruction
    }
}

// 0x31870: ENTB2 rec ... -1: scene objects from short records (kind, value -> +0x70 / +0x72)
template <class P> void Game<P>::evt_ENTB2()
{
    uint32_t pc = mem.u32(G::evt_pc) + 4;
    mem.set32(G::evt_pc, pc);
    while (mem.u32(pc) != 0xFFFFFFFFu) {
        const uint32_t rec = mem.u32(pc);
        const uint32_t routine = mem.u32(G::obj_kinds + mem.u32(rec) * 4);
        if (P::pc && routine == 0) {
            pc_debug_print();
        } else {
            const uint32_t t = task_create(routine, P::pc ? 0x3D0 : 0x3C8);
            task_clear_work(t);
            const uint16_t v = mem.at<uint16_t>(rec + 4);
            of<uint16_t>(t, 0x72) = v;
            of<uint16_t>(t, 0x70) = v;
        }
        pc = mem.u32(G::evt_pc) + 4;
        mem.set32(G::evt_pc, pc);
    }
    mem.set32(G::evt_pc, pc + 4);
}

// 0x31900: ENTB3 rec ... -1: as ENTB with the position as it is, record bytes +0x26 / +0x27 -> +0x320 / +0x324
// (PC: nothing while boss mode moves on to the next boss)
template <class P> void Game<P>::evt_ENTB3()
{
    uint32_t pc = mem.u32(G::evt_pc) + 4;
    mem.set32(G::evt_pc, pc);
    while (mem.u32(pc) != 0xFFFFFFFFu) {
        const uint32_t rec = mem.u32(pc);
        const uint32_t routine = mem.u32(G::obj_kinds + mem.u32(rec) * 4);
        bool spawn = true;
        if constexpr (P::pc) {
            if (routine == 0) {
                pc_debug_print();
                spawn = false;
            } else {
                spawn = mem.u32(G::pc_boss_next) == 0;
            }
        } else if (boss_next_on()) {
            spawn = false;   // Boss Mode on the arcade logic: as the PC (pmrest)
        }
        if (spawn) {
            const uint32_t t = task_create(routine, P::pc ? 0x3D0 : 0x3C8);
            task_clear_work(t);
            of<uint8_t>(t, 0x3C4) &= uint8_t(0xFD);
            of<uint32_t>(t, 0x58) = mem.u32(rec + 4) | 1;
            of<uint16_t>(t, 0xCC) = mem.at<uint8_t>(rec + 0x24);
            of<uint16_t>(t, 0x64) = mem.at<uint8_t>(rec + 0x25);
            const uint16_t hp = mem.at<uint16_t>(rec + 0x22);
            of<uint16_t>(t, 0x72) = hp;
            of<uint16_t>(t, 0x70) = hp;
            for (uint32_t i = 0; i < 6; ++i)
                of<uint32_t>(t, 0x20 + 4 * i) = mem.u32(rec + 8 + 4 * i);
            of<uint32_t>(t, 0x320) = mem.at<uint8_t>(rec + 0x26);
            of<uint32_t>(t, 0x324) = mem.at<uint8_t>(rec + 0x27);
            evt_spawned(t, rec);   // (Survival Mode's lead-in; nothing otherwise)
        }
        pc = mem.u32(G::evt_pc) + 4;
        mem.set32(G::evt_pc, pc);
    }
    mem.set32(G::evt_pc, pc + 4);
}

// 0x319C0: ENTBE rec ... -1: flags, position and angles as they are, +0x20 -> +0x70 / +0x72, the rest of the
// record (+0x24..) -> +0x35C
template <class P> void Game<P>::evt_ENTBE()
{
    uint32_t pc = mem.u32(G::evt_pc) + 4;
    mem.set32(G::evt_pc, pc);
    while (mem.u32(pc) != 0xFFFFFFFFu) {
        const uint32_t rec = mem.u32(pc);
        const uint32_t routine = mem.u32(G::obj_kinds + mem.u32(rec) * 4);
        if (P::pc && routine == 0) {
            pc_debug_print();
        } else {
            const uint32_t t = task_create(routine, P::pc ? 0x3D0 : 0x3C8);
            task_clear_work(t);
            of<uint32_t>(t, 0x58) = mem.u32(rec + 4) | 1;
            for (uint32_t i = 0; i < 6; ++i)
                of<uint32_t>(t, 0x20 + 4 * i) = mem.u32(rec + 8 + 4 * i);
            const uint16_t v = mem.at<uint16_t>(rec + 0x20);
            of<uint32_t>(t, 0x35C) = rec + 0x24;
            of<uint16_t>(t, 0x72) = v;
            of<uint16_t>(t, 0x70) = v;
            evt_spawned(t, rec);   // (Survival Mode's lead-in; nothing otherwise)
        }
        pc = mem.u32(G::evt_pc) + 4;
        mem.set32(G::evt_pc, pc);
    }
    mem.set32(G::evt_pc, pc + 4);
}

// 0x66B60: WLTB table: the room bounds (6 floats) from the table
template <class P> void Game<P>::evt_WLTB()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    const uint32_t t = mem.u32(pc + 4);
    mem.set32(G::room_x0, mem.u32(t));
    mem.set32(G::room_x1, mem.u32(t + 4));
    mem.set32(G::room_y0, mem.u32(t + 8));
    mem.set32(G::room_y1, mem.u32(t + 12));
    mem.set32(G::room_z0, mem.u32(t + 16));
    mem.set32(G::room_table, t);
    mem.set32(G::room_z1, mem.u32(t + 20));
    mem.set32(G::evt_pc, pc + 8);
}

// 0x66BE0: CLTB ... -1: evt_cltb = its list
template <class P> void Game<P>::evt_CLTB()
{
    uint32_t pc = mem.u32(G::evt_pc);
    mem.set32(G::evt_cltb, pc + 4);
    do
        pc += 4;
    while (mem.u32(pc) != 0xFFFFFFFFu);
    mem.set32(G::evt_pc, pc + 4);
}

// 0x66C20: KGTB ... -1: evt_kgtb = its list
template <class P> void Game<P>::evt_KGTB()
{
    uint32_t pc = mem.u32(G::evt_pc);
    mem.set32(G::evt_kgtb, pc + 4);
    do
        pc += 4;
    while (mem.u32(pc) != 0xFFFFFFFFu);
    mem.set32(G::evt_pc, pc + 4);
}

// 0x66C60: BGTB n: draw zone n
template <class P> void Game<P>::evt_BGTB()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    mem.set32(G::evt_pc, pc + 8);
    mem.at<uint8_t>(G::draw_zone) = mem.at<uint8_t>(pc + 4);
}

// 0x66C90: CCTB job operands: one more camera job, the instruction into the ring (unless events are skipped);
// 1 + job / 16 operands follow
template <class P> void Game<P>::evt_CCTB()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    if (mem.u32(G::evt_skip) != 0) {
        if constexpr (P::pc)
            pc_debug_print();
    } else {
        const uint32_t pos = mem.u32(G::evt_cctb_ring_pos);
        if constexpr (P::pc)
            mem.set32(G::evt_cctb_ring + (pos + 1) * 4, pc);   // the PC's ring is written one slot on
        else
            mem.set32(G::evt_cctb_ring + pos * 4, pc);
        mem.set32(G::evt_cctb_ring_pos, (pos + 1) & 15);
        mem.set32(G::evt_camjobs, mem.u32(G::evt_camjobs) + 1);
    }
    const int32_t job = int32_t(mem.u32(pc + 4));
    mem.set32(G::evt_pc, pc + 8 + uint32_t(job / 16) * 4);
}

// 0x66D00: WAIT_ENNUM n: until at most n enemies are counted (and the last one has left the route)
template <class P> void Game<P>::evt_WAIT_ENNUM()
{
    if (mem.u32(G::evt_yield) == 0) {
        mem.set32(G::evt_yield, 1);
        return;
    }
    const uint32_t pc = mem.u32(G::evt_pc);
    if (int32_t(mem.at<int16_t>(G::enemy_count)) <= int32_t(mem.u32(pc + 4)) && mem.u32(G::evt_players_active) != 0 &&
        mem.at<uint8_t>(G::enemy_left_route) != 0) {
        mem.set32(G::evt_pc, pc + 8);
        mem.set32(G::evt_yield, 0);
    }
}

// 0x66D60: WAIT_FRM n: n frames
template <class P> void Game<P>::evt_WAIT_FRM()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    if (mem.u32(G::evt_skip) != 0) {
        if constexpr (P::pc)
            pc_debug_print();
        mem.set32(G::evt_pc, pc + 8);
        return;
    }
    if (mem.u32(G::evt_yield) == 0) {
        mem.set32(G::evt_wait_frames, mem.u32(pc + 4));
        mem.set32(G::evt_yield, 1);
        return;
    }
    int32_t n = int32_t(mem.u32(G::evt_wait_frames));
    if (n >= 0)
        --n;
    if (n < 0 && mem.u32(G::evt_players_active) != 0) {
        mem.set32(G::evt_pc, pc + 8);
        n = -1;
        mem.set32(G::evt_yield, 0);
    }
    mem.set32(G::evt_wait_frames, uint32_t(n));
}

// 0x66DF0: WAIT_ITEM n: until event flag n is set
template <class P> void Game<P>::evt_WAIT_ITEM()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    if (mem.u32(G::evt_skip) != 0) {
        if constexpr (P::pc)
            pc_debug_print();
        mem.set32(G::evt_pc, pc + 8);
        return;
    }
    if (mem.u32(G::evt_yield) == 0) {
        mem.set32(G::evt_yield, 1);
        return;
    }
    if (mem.at<uint8_t>(G::event_flags + mem.u32(pc + 4)) != 0 && mem.u32(G::evt_players_active) != 0) {
        mem.set32(G::evt_pc, pc + 8);
        mem.set32(G::evt_yield, 0);
    }
}

// 0x66E60: WAIT_CAMJOB: until the queued camera jobs are done
template <class P> void Game<P>::evt_WAIT_CAMJOB()
{
    if (mem.u32(G::evt_skip) != 0) {
        if constexpr (P::pc)
            pc_debug_print();
    } else {
        if (mem.u32(G::evt_camjobs) != 0) {
            mem.set32(G::evt_yield, 1);
            return;
        }
        if (mem.u32(G::evt_players_active) == 0)
            return;
        mem.set32(G::evt_yield, 0);
    }
    mem.set32(G::evt_pc, mem.u32(G::evt_pc) + 4);
}

// 0x66EC0: WAIT_CAMFRM n: until the camera is past frame n of its path (n = 0: until its path job has no frames
// left)
template <class P> void Game<P>::evt_WAIT_CAMFRM()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    if (mem.u32(G::evt_skip) != 0) {
        if constexpr (P::pc) {
            pc_debug_print();
            mem.set32(G::evt_yield, 0);
        }
        mem.set32(G::evt_pc, pc + 8);
        return;
    }
    if (mem.u32(G::evt_yield) == 0) {
        mem.set32(G::evt_yield, 1);
        return;
    }
    if (mem.u32(G::evt_players_active) == 0)
        return;
    const int32_t n = int32_t(mem.u32(pc + 4));
    if (n != 0) {
        const float frame = mem.f32(G::camera_frame);
        if constexpr (P::pc) {
            if (double(n) >= double(frame))   // fild / fcomp: the integer exactly
                return;
        } else {
            if (frame <= float(n))
                return;
        }
    } else if (int32_t(mem.u32(G::camera_frames_left)) > 0) {
        return;
    }
    mem.set32(G::evt_pc, pc + 8);
    mem.set32(G::evt_yield, 0);
}

// 0x66F50: WAIT_ENALV n: until at most n enemies are alive (and the last one has left the route)
template <class P> void Game<P>::evt_WAIT_ENALV()
{
    if (mem.u32(G::evt_yield) == 0) {
        mem.set32(G::evt_yield, 1);
        return;
    }
    const uint32_t pc = mem.u32(G::evt_pc);
    if (int32_t(mem.at<int16_t>(G::enemies_alive)) <= int32_t(mem.u32(pc + 4)) && mem.u32(G::evt_players_active) != 0 &&
        mem.at<uint8_t>(G::enemy_left_route) != 0) {
        mem.set32(G::evt_pc, pc + 8);
        mem.set32(G::evt_yield, 0);
    }
}

// 0x66FB0: BSFT n (16-bit)
template <class P> void Game<P>::evt_BSFT()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    mem.set32(G::evt_pc, pc + 8);
    mem.at<uint16_t>(G::evt_bsft) = mem.at<uint16_t>(pc + 4);
}

// 0x66FE0: PYPMD n
template <class P> void Game<P>::evt_PYPMD()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    mem.set32(G::evt_pc, pc + 8);
    mem.set32(G::evt_pypmd, mem.u32(pc + 4));
}

// 0x67010: SHDWLT a b: shadow / light set-up
template <class P> void Game<P>::evt_SHDWLT()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    shadow_light_reset(mem.u32(pc + 4), mem.u32(pc + 8));
    mem.set32(G::evt_pc, mem.u32(G::evt_pc) + 12);
}

// 0x67040: CGMFNSH n (byte)
template <class P> void Game<P>::evt_CGMFNSH()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    mem.set32(G::evt_pc, pc + 8);
    mem.at<uint8_t>(G::evt_cgmfnsh) = mem.at<uint8_t>(pc + 4);
}

// 0x67070: SKYKD n: the sky
template <class P> void Game<P>::evt_SKYKD()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    mem.set32(G::evt_pc, pc + 8);
    mem.at<uint8_t>(G::sky_index) = mem.at<uint8_t>(pc + 4);
}

// 0x670A0: SKYTB n: the sky's mode
template <class P> void Game<P>::evt_SKYTB()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    mem.set32(G::evt_pc, pc + 8);
    mem.at<uint8_t>(G::sky_mode) = mem.at<uint8_t>(pc + 4);
}

// 0x670D0: FKRTB ptr: the floor height from *ptr
template <class P> void Game<P>::evt_FKRTB()
{
    const uint32_t pc = mem.u32(G::evt_pc) + 4;
    const uint32_t y = mem.u32(mem.u32(pc));
    mem.set32(G::evt_pc, pc + 4);
    mem.set32(G::floor_y, y);
}

// 0x67100: CHGSM n: scene_state (PC: and a debug print)
template <class P> void Game<P>::evt_CHGSM()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    mem.set32(G::evt_pc, pc + 8);
    mem.at<uint8_t>(G::scene_state) = mem.at<uint8_t>(pc + 4);
    if constexpr (P::pc)
        pc_debug_print();
}

// 0x67130: NCHGCF n (byte)
template <class P> void Game<P>::evt_NCHGCF()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    mem.set32(G::evt_pc, pc + 8);
    mem.at<uint8_t>(G::evt_nchgcf) = mem.at<uint8_t>(pc + 4);
}

// the body of CAMDR / CAMDR2 (instruction at `at`): the camera drive mode's set-up; the camera stops its path job
// (job nop, evt_state 1), one camera job less, the players' CCTB 16 flags cleared
template <class P> void Game<P>::evt_camdr_set(uint32_t at)
{
    const int32_t mode = int32_t(mem.u32(at + 4));
    mem.set32(G::camera_kind, 1);
    mem.set32(G::camera_mode, uint32_t(mode));
    evt_camdr_call(mode);
    mem.set32(G::camera_stopped, 1);
    mem.set32(G::camera_records + 0x4C, F::nop);
    mem.set32(G::evt_state, 1);
    mem.set32(G::camera_drive_flag, 0);
    mem.at<Player>(G::players).cam_flags &= uint8_t(0xFE);
    mem.at<Player>(G::players + uint32_t(sizeof(Player))).cam_flags &= uint8_t(0xFE);
    mem.set32(G::evt_camjobs, mem.u32(G::evt_camjobs) - 1);
    mem.set32(G::evt_pc, mem.u32(G::evt_pc) + 8);
    mem.set32(G::camera_drive_mode, mem.u32(at + 4));
}

// 0x67160: CAMDR mode: the camera drives in mode `mode` (camdr_modes)
template <class P> void Game<P>::evt_CAMDR()
{
    evt_camdr_set(mem.u32(G::evt_pc));
}

// 0x67220: CAMDR2 mode: as CAMDR, but yields until no player is out (not in play by player_state_routines and no
// lives) or the attract demo runs
template <class P> void Game<P>::evt_CAMDR2()
{
    if (mem.u32(G::evt_yield) == 0)
        mem.set32(G::evt_yield, 1);
    const Player& p1 = mem.at<Player>(G::players);
    const Player& p2 = mem.at<Player>(G::players + uint32_t(sizeof(Player)));
    auto out = [&](const Player& p) {
        return mem.at<uint8_t>(G::player_state_routines + 7 + uint32_t(p.state) * 8) == 0 && p.lives <= 0;
    };
    if ((out(p1) || out(p2)) && is_demo() == 0)
        return;
    evt_camdr_set(mem.u32(G::evt_pc));
    mem.set32(G::evt_yield, 0);
}

// 0x67350: GGCOL row entry: one color row (PC: the operands skipped)
template <class P> void Game<P>::evt_GGCOL()
{
    if constexpr (!P::pc) {
        const uint32_t pc = mem.u32(G::evt_pc);
        color_row_set(int32_t(mem.u32(pc + 4)), int32_t(mem.u32(pc + 8)));
    }
    mem.set32(G::evt_pc, mem.u32(G::evt_pc) + 12);
}

// 0x67380 (arcade only): GGSET n: color rows 2-5 from list n by the blood color (-1: none; 0xFFFF entries: kept)
template <class P> void Game<P>::evt_GGSET()
{
    if constexpr (!P::pc) {
        const int32_t n = int32_t(mem.u32(mem.u32(G::evt_pc) + 4));
        const uint32_t list = n == -1 ? G::ggset_none
                                      : mem.u32(mem.u32(G::ggset_lists + uint32_t(n) * 4) + uint32_t(mem.at<uint8_t>(G::blood_color)) * 4);
        for (int32_t row = 0; row < 4; ++row)
            if (mem.at<uint16_t>(list + uint32_t(row) * 2) != 0xFFFF) {
                color_row_set(row + 2, mem.at<int16_t>(list + uint32_t(row) * 2));
                // (host, display only: the custom blood colour) the row's red and green lists differ: a blood row
                if (n != -1) {
                    const uint32_t lists = mem.u32(G::ggset_lists + uint32_t(n) * 4);
                    if (mem.at<uint16_t>(mem.u32(lists) + uint32_t(row) * 2) != mem.at<uint16_t>(mem.u32(lists + 4) + uint32_t(row) * 2))
                        ggset_blood_row(row + 2);
                }
            }
        mem.set32(G::evt_pc, mem.u32(G::evt_pc) + 8);
    }
}

// 0x67450 (arcade only): TEXCHG area: texture area change
template <class P> void Game<P>::evt_TEXCHG()
{
    if constexpr (!P::pc) {
        texture_area_load(mem.u32(mem.u32(G::evt_pc) + 4), -1);
        mem.set32(G::evt_pc, mem.u32(G::evt_pc) + 8);
    }
}

// PC 0x4C1514 (PC only): GGSET / TEXCHG skip their operand
template <class P> void Game<P>::evt_skip1()
{
    mem.set32(G::evt_pc, mem.u32(G::evt_pc) + 8);
}

// 0x675C0: SCNINIT: a section's start: the camera's record 0 from scninit_camera at frame 0, drive mode 3; the
// counters and the scene flags reset; the input source (live / the attract demo's / mode 8's recording); the event
// appended to scene_history; the attract demo and mode 8 clear the sprite layer 4 frames later (arcade)
template <class P> void Game<P>::evt_SCNINIT()
{
    for (uint32_t i = 0; i < 7; ++i)
        mem.set32(G::camera_records + 0xD0 + 4 * i, mem.u32(G::scninit_camera + 4 * i));
    mem.set32(G::camera_frame, 0);
    mem.set32(G::camera_kind, 1);
    mem.set32(G::camera_mode, 3);
    evt_camdr_call(3);
    mem.set32(G::camera_hold_hostage, 0);
    mem.set32(G::blink_phase, 0);
    mem.at<uint16_t>(G::evt_bsft) = 0;
    mem.set32(G::camera_frames_left, 0x7FFFFFFF);
    scnin_reset_543530();
    shadow_light_reset(0xC000, 0);
    mem.at<Player>(G::players + uint32_t(sizeof(Player))).cam_flags &= uint8_t(0xFE);
    mem.at<Player>(G::players).cam_flags &= uint8_t(0xFE);
    mem.set32(G::evt_pypmd, 0);
    if (is_demo() != 0)
        rand_seed(0);
    const uint16_t mode = mem.at<uint16_t>(G::game_mode);
    uint32_t source = 0;
    if (mode == 3) {
        mem.set32(G::demo_input_advance, 1);
        mem.set32(G::demo_input_ptr, mem.u32(0x100000 + mem.u32(G::demo_index) * 4));
        source = 1;
    } else if (mode == 8) {
        mem.set32(G::demo_input_advance, 0);
        mem.set32(G::mode8_input_ptr, G::mode8_input);
        source = 1;
    }
    mem.at<uint8_t>(G::evt_nchgcf) = 1;
    mem.set32(G::input_source, source);
    mem.at<uint8_t>(G::attack_side) = 0;
    mem.set32(G::headless_countdown, 0);
    mem.at<uint8_t>(G::sky_mode) = 0;
    mem.set32(G::evt_pc, mem.u32(G::evt_pc) + 4);
    const uint8_t h = mem.at<uint8_t>(G::scene_history_pos);
    const uint32_t row = G::scene_history + uint32_t(int32_t(mem.at<int8_t>(G::chapter))) * 16;
    mem.at<uint8_t>(row + uint32_t(int32_t(int8_t(h)))) = mem.at<uint8_t>(G::event_index);
    mem.at<uint8_t>(G::scene_history_pos) = uint8_t(h + 1);
    mem.at<uint8_t>(row + uint32_t(int32_t(int8_t(h + 1)))) = 0xFF;
    if constexpr (!P::pc)
        if (mode == 3 || mode == 8)
            scnin_clear_sprites_start();
}

// 0x677A0: the task that clears the sprite layer 4 frames later
template <class P> void Game<P>::scnin_clear_sprites_start()
{
    const uint32_t t = task_create(F::scnin_clear_sprites, 0x58);
    mem.set32(t + 0x50, 4);
}

// 0x6BA80
template <class P> void Game<P>::scnin_reset_543530()
{
    mem.at<uint8_t>(G::scene_init_flag) = 0;
}

// 0x677C0: REMFRM n: the camera path job's frames left
template <class P> void Game<P>::evt_REMFRM()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    mem.set32(G::evt_pc, pc + 8);
    mem.set32(G::camera_frames_left, mem.u32(pc + 4));
}

// 0x677F0: RNWCJ state n: evt_state, n more camera jobs
template <class P> void Game<P>::evt_RNWCJ()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    mem.set32(G::evt_state, mem.u32(pc + 4));
    mem.set32(G::evt_pc, pc + 12);
    mem.set32(G::evt_camjobs, mem.u32(G::evt_camjobs) + mem.u32(pc + 8));
}

// 0x67830: ITEMON n: event flag n set
template <class P> void Game<P>::evt_ITEMON()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    mem.set32(G::evt_pc, pc + 8);
    mem.at<uint8_t>(G::event_flags + mem.u32(pc + 4)) = 1;
}

// 0x67860: SNDON cue
template <class P> void Game<P>::evt_SNDON()
{
    sound_cue(mem.u32(mem.u32(G::evt_pc) + 4));
    mem.set32(G::evt_pc, mem.u32(G::evt_pc) + 8);
}

// 0x67890: ATCMD n: attack_side
template <class P> void Game<P>::evt_ATCMD()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    mem.set32(G::evt_pc, pc + 8);
    mem.at<uint8_t>(G::attack_side) = mem.at<uint8_t>(pc + 4);
}

// 0x678C0: GMTMF n
template <class P> void Game<P>::evt_GMTMF()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    mem.set32(G::evt_pc, pc + 8);
    mem.set32(G::evt_gmtmf, mem.u32(pc + 4));
}

// 0x678F0: GMTAD n: evt_gmt += n
template <class P> void Game<P>::evt_GMTAD()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    mem.set32(G::evt_pc, pc + 8);
    mem.set32(G::evt_gmt, mem.u32(G::evt_gmt) + mem.u32(pc + 4));
}

// 0x67920: CLRGFST: bit 1 of evt_clrgfst_flags cleared
template <class P> void Game<P>::evt_CLRGFST()
{
    mem.at<uint8_t>(G::evt_clrgfst_flags) &= uint8_t(0xFD);
    mem.set32(G::evt_pc, mem.u32(G::evt_pc) + 4);
}

// 0x67950: SKPETO n x ... -1: the block runs while evt_gmt < n (unsigned), else it is skipped
template <class P> void Game<P>::evt_SKPETO()
{
    uint32_t pc = mem.u32(G::evt_pc);
    if (mem.u32(G::evt_gmt) < mem.u32(pc + 4)) {
        pc += 8;
    } else {
        pc += 12;
        while (mem.u32(pc) != 0xFFFFFFFFu)
            pc += 4;
        pc += 4;
    }
    mem.set32(G::evt_pc, pc);
}

// 0x679A0: SKPETU n x ... -1: the block runs while evt_gmt >= n (unsigned), else it is skipped
template <class P> void Game<P>::evt_SKPETU()
{
    uint32_t pc = mem.u32(G::evt_pc);
    if (mem.u32(G::evt_gmt) >= mem.u32(pc + 4)) {
        pc += 8;
    } else {
        pc += 12;
        while (mem.u32(pc) != 0xFFFFFFFFu)
            pc += 4;
        pc += 4;
    }
    mem.set32(G::evt_pc, pc);
}

// 0x679F0: SKIP n: n != 0 a skippable section starts (evt_skip_end cleared), 0 it ends (evt_skip cleared)
template <class P> void Game<P>::evt_SKIP()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    if (mem.u32(pc + 4) != 0) {
        mem.set32(G::evt_skip_on, 1);
        mem.set32(G::evt_skip_end, 0);
    } else {
        mem.set32(G::evt_skip_on, 0);
        mem.set32(G::evt_skip, 0);
    }
    mem.set32(G::evt_pc, pc + 8);
}

// 0x67A40: EV3JP1 flag address: jump when event flag `flag` is 1
template <class P> void Game<P>::evt_EV3JP1()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    if (mem.at<uint8_t>(G::event_flags + mem.u32(pc + 4)) == 1)
        mem.set32(G::evt_pc, mem.u32(pc + 8));
    else
        mem.set32(G::evt_pc, pc + 12);
}

// 0x67A80: WAIT: stop here (for good)
template <class P> void Game<P>::evt_WAIT()
{
    mem.set32(G::evt_yield, 1);
}

// 0x67A90: NEXTEV: the event's next section; after its last one the next event by route_branches (type 0: target,
// 1: target by route_choice, 2: the next event) from section 1; the route choice is cleared. At the end of the
// chapter's list: chapter_ended, the chapter-end sub-mode (game mode 4), the following chapter's event from the
// table's entry before, and the script goes on in the shared list (event_lists[4] event 1)
template <class P> void Game<P>::evt_NEXTEV()
{
    if constexpr (P::pc)
        if (mem.u32(G::frontend_state) == 6)
            mem.set32(G::pc_load_state, 4);
    const uint32_t lists = evt_event_lists();
    mem.at<int8_t>(G::scene_number) = int8_t(mem.at<int8_t>(G::scene_number) + 1);
    const int32_t chapter = mem.at<int8_t>(G::chapter);
    int32_t event = mem.at<int8_t>(G::event_index);
    const uint32_t list = mem.u32(lists + uint32_t(chapter) * 4);
    if (mem.u32(mem.u32(list + uint32_t(event) * 4) + uint32_t(int32_t(mem.at<int8_t>(G::scene_number))) * 4) == 0xFFFFFFFFu) {
        const uint32_t b = mem.u32(G::route_branches + uint32_t(chapter) * 4) + uint32_t(event) * 16;
        const int8_t type = mem.at<int8_t>(b);
        if (type == 1)
            event = mem.at<uint8_t>(b + 4 + uint32_t(int32_t(mem.at<int8_t>(G::route_choice))) * 4);
        else if (type == 0)
            event = mem.at<uint8_t>(b + 4);
        else if (type == 2)
            event = event + 1;
        mem.at<int8_t>(G::scene_number) = 1;
        mem.at<uint8_t>(G::event_index) = uint8_t(event);
    }
    event = mem.at<int8_t>(G::event_index);
    if (mem.u32(list + uint32_t(event) * 4) != 0xFFFFFFFFu) {
        const uint32_t pc = mem.u32(mem.u32(list + uint32_t(event) * 4) + uint32_t(int32_t(mem.at<int8_t>(G::scene_number))) * 4);
        mem.at<int8_t>(G::route_choice) = 0;
        mem.set32(G::evt_pc, pc);
        if constexpr (P::pc)
            pc_debug_print();
        return;
    }
    // the end of the chapter's list
    mem.at<uint16_t>(G::chapter_ended) = 1;
    mem.at<uint16_t>(G::evt_bsft) = 0;
    if (mem.at<int16_t>(G::game_mode) == 4 && mem.at<int16_t>(G::game_submode) != 8) {
        mem.at<int16_t>(G::game_submode) = 5;
        if constexpr (!P::pc) {
            sound_queue_reset();
            sound_send3(0xA00002);
            sound_send3(0xBE5000);
        }
    }
    mem.at<uint8_t>(G::event_index) = mem.at<uint8_t>(mem.u32(G::route_branches + uint32_t(chapter) * 4) + uint32_t(event) * 16 - 12);
    if constexpr (P::pc)
        pc_debug_print();
    mem.set32(G::evt_pc, mem.u32(mem.u32(mem.u32(lists + 0x10) + 4)));
}

// PC 0x46C6D0 (PC only): the current matrix applied to a point (the maths of pc_matrix_transform)
template <class P> void Game<P>::pc_point_transform(float x, float y, float z, float& ox, float& oy, float& oz)
{
    if constexpr (P::pc) {
        const uint32_t m = mem.u32(G::pc_matrix_ptr);
        auto e = [&](int i) { return mem.f32(m + uint32_t(i) * 4); };
        ox = ((e(1) * y + e(0) * x) + e(2) * z) + e(9);
        oy = ((e(3) * x + e(4) * y) + e(5) * z) + e(10);
        oz = ((e(7) * y + e(6) * x) + e(8) * z) + e(11);
    } else {
        pm_cur().transform(x, y, z, ox, oy, oz);   // PC Mode on the arcade logic
    }
}

} // namespace hotd::port
