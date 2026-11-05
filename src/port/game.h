// The game ported to C++ (docs/port.md): one member function per original routine, same names (tools/ghidra/names.tsv)
// and structure, on the original data layouts (port/types.h) in the original address space (port/mem.h).
// Game<A> is the arcade (A), Game<Pc> the PC port (PA and PM: the PC code's own frontend_state tests decide between
// them at run time); where the two versions' code differs, `if constexpr (P::pc)` keeps both in the one routine.
// Routines not ported yet are virtual "boundaries": the proof harness replays their recorded effects (docs/proof.md).
#pragma once

#include "port/coop4.h"
#include "port/globals.h"
#include "port/mem.h"
#include "port/pm_maths.h"
#include "port/tgp.h"
#include "port/types.h"
#include "port/util.h"

#include <array>
#include <cmath>

namespace hotd::port {

// The Typing of the Dead 0 (ours, game/typing.h, user 2026-11-03: "a clear window before it hits"): asked by the
// standard enemy's attack choice (enemy_ai states 5 / 6) and pick_target_player whether this creature's attack waits
// (true: no free player for it this frame). Set only for the ticks of a Typing of the Dead 0 game; null everywhere
// else (the arcade, PC Mode, the proofs: nothing changes). damage.cpp
extern bool (*gAttackHold)(uint32_t obj);
// and (player_hit) whether a hit on this player is held: in a boss fight a hit lands only once a sentence's window ran
// out (TotD: the boss strikes when its sentence was not typed in time). Null outside those ticks
extern bool (*gHitHold)(int player);
// and (totd2, user 2026-11-03: "the grabbed researchers can't be saved") whether a creature that reached the researcher
// it chases waits before its bite / strike (its prompt's rescue timer has not run out): the run-after routines'
// arrival tests (enemy_actions.cpp). Null outside those ticks
extern bool (*gPreyHold)(uint32_t obj);
// Gameplay "Attract Gore" (default on): the attract's hit parts and blow-offs (damage.cpp) and the prologue's sewer
// corpses (event.cpp evt_attract_restore) as the 1998 PC; off: Revision A exactly
extern bool gAttractGore;

template <class P> class Game {
public:
    HOTD_LAYOUT_TYPES
    // the version's offset of an unnamed object field, given by its arcade offset
    static constexpr uint32_t ob(uint32_t a) { return P::template off<Obj>(a); }

    Game(Mem& mem, Tgp& tgp) : mem(mem), tgp(tgp) {}
    virtual ~Game() = default;

    Mem& mem;
    Tgp& tgp;   // the arcade's geometry coprocessor (A only)

    // The experimental 4-player mode (docs/coop4.md, port/coop4.h): set by the runtime for --players 4 (the arcade
    // logic only); 0 in every 1-2 player game and in the proofs, so nothing of seats 3 / 4 runs, reads or writes then.
    int seats4 = 0;
    // Light Gun Edition (game/game_outputs.h): each seat's bullets fired by its own input (not the attract's demo), counted
    // where player_shoot sets the Player's "fired" flag (the shot test clears that flag again in the same frame, so the
    // outputs read this count); never read by the logic
    uint32_t lg_shots[4] = {0, 0, 0, 0};
    // Light Gun Edition's fun modes (game logic: local games only; 1 / 0 in the proofs and online): Shotgun Mode's shot
    // test radius factor, and each seat's shots that hit something (the Party Scoreboard's, real input only)
    float lg_hit_scale = 1.0f;
    uint32_t lg_hits[4] = {0, 0, 0, 0};
    // Light Gun Edition's weapons (Arsenal Mode): each seat's own shot-test radius factor (on top of lg_hit_scale)
    // and the rocket's splash (every candidate of the shot is hit, not only the nearest)
    float lg_seat_scale[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool lg_splash[4] = {false, false, false, false};
    bool lg_hide_bullets[4] = {false, false, false, false};
    bool lg_one_hit = false;                  // Sudden Death: any hit kills an enemy (the bosses keep their own fights)
    uint32_t lg_kills[4] = {0, 0, 0, 0};      // each seat's enemy kills (the Party Scoreboard, Vampire Mode)
    bool lg_hostage_crisis = false;           // Hostage Crisis: a shot hostage / researcher ends the game for everyone
    bool lg_crisis_fail = false;              // (a hostage was shot: the runtime ends the game)
    bool lg_roulette = false;                 // Weak-Point Roulette: a zombie takes hits only on its glowing part
    virtual bool lg_part_ok(uint32_t part) { (void)part; return true; }   // (the runtime's: the glowing part)
    float lg_dmg_scale[4] = {1.0f, 1.0f, 1.0f, 1.0f};   // each seat's damage x (the shotgun's: 2)
    bool lg_exploding = false;                // Exploding Zombies: some zombies glow; a hit not on the head blows them up
    virtual void lg_explode(int player, uint32_t part) { (void)player, (void)part; }   // (the runtime's: the blast)
    virtual void lg_fired(int player) { (void)player; }   // a gun shot fired (the runtime's: Arsenal's rocket)
    // Options "Game Text: Japanese" (ours): the region's text-only effects (the subtitles / voice-message sprites,
    // chapter cards, RELOAD hint, the attract tutorial's text) as the JAPAN region 0 draws them, while the logic keeps
    // reading the real region 0x51F791 (attract order, credits, the opening title's timing). Off in the proofs.
    bool jaText = false;
    uint32_t text_region() const { return jaText ? 0u : uint32_t(mem.template at<uint8_t>(0x51F791)); }
    // the 4P mode: the player cut-scene cam_job18 started last (player_scene_routines[n]; -1 none), for the runtime's
    // drawn bodies of players 3 / 4 (a scene's later routines share address ranges: 10 / 11 run 07's, 12 06's)
    int coop4_scene = -1;
    // the cut-scene viewer (game/scene_view.h, ours): the player cut-scene cam_job18 started last, in any game, and a
    // count of them; never read by the logic
    int scene_started = -1;
    uint32_t scene_starts = 0;
    // Boss Mode (THOTD.EXE front-end state 4; game/bossmode.h): no continue, game over as soon as every player is out
    // (game_continue_begin 0x458808 / game_continue_countdown 0x4588A0). Set by the runtime; false in the proofs
    bool bossNoContinue = false;
    // Boss Mode's rules in the boss code (pmrest, docs/teaser_status/bossmode.md "PC rules"): the PC's own tests of
    // frontend_state == 4. The runtime sets boss_mode (THOTD.EXE 0xC723B8: 2-6; 0 in every other game and in the
    // proofs) and boss_next (pc_boss_next 0x5CD360: the walk to the boss is run undrawn, without sounds, ENTB3
    // spawns or title texts, and camera paths start 5 frames before their end). The bosses report the PC's calls of
    // the Boss Mode routines in boss_events (kBoss*), which the runtime's BossMode reads after each frame
    int boss_mode = 0;
    bool boss_next = false;
    uint32_t boss_events = 0;
    uint32_t boss_deaths = 0;   // (the cut-scene viewer's boss defeats: every boss death, any mode; not game state)
    uint32_t magician_deaths = 0;   // (the viewer: the Magician's defeats, boss_magician case 0; chapter 4 has other boss events before)
    bool boss_death_center = false;   // (the viewer's Hangedman defeat only: he falls where the camera looks, clear of the building)
    enum : uint32_t {
        kBossEntrance = 1,     // 0x412BC4: pc_boss_next = 0, the clock shown, the boss song (cue 12; Magician 19)
        kBossClockStart = 2,   // 0x412B00: the clock runs (the title card's end)
        kBossClockStop = 4,    // 0x412B14: the clock stops (the boss's death)
        kBossWin = 8,          // 0x412C08: result 1, the clock hidden
    };
    bool boss_on() const
    {
        if constexpr (P::pc)
            return mem.template at<int32_t>(G::frontend_state) == 4;
        else
            return boss_mode != 0;
    }
    bool boss_next_on() const
    {
        if constexpr (P::pc)
            return mem.u32(G::pc_boss_next) != 0;
        else
            return boss_mode != 0 && boss_next;
    }
    // a Boss Mode routine call (0x412B00 / 14 / BC4 / C08): only in a Boss Mode game on the arcade logic (the PC's
    // own clock routines are not modelled: Game<Pc> never runs Boss Mode)
    void boss_event(uint32_t e)
    {
        if constexpr (!P::pc) {
            if (e == kBossClockStop)
                ++boss_deaths;
            if (boss_mode == 0)
                return;
            if (e == kBossEntrance)
                boss_next = false;
            boss_events |= e;
        }
    }
    // Boss Mode's start (bossbug): the PC's game_new 0x458588 takes the first section from boss_starts +8 (0 for all
    // four: the event's opening section, its BGTB / GGCOL / the boss song) where the arcade's evt_chapter_start starts
    // a game at section 1; evt_chapter_start takes it once. Set by the runtime; false in the proofs
    bool startAtSection0 = false;
    // Survival Mode (ours, game/survival.h): told of each object the spawn opcodes ENTB / ENTB3 / ENTBE made (its fields set,
    // before it first runs). Nothing here: the runtime's Survival Mode captures the lead-in's creature spawns (their places
    // become the arena's spawn points). A normal game and the proofs: no effect
    virtual void evt_spawned(uint32_t task, uint32_t rec) { (void)task, (void)rec; }
    bool coop4_on() const
    {
        if constexpr (Seats<P>::max == 4)
            return seats4 != 0;
        else
            return false;
    }
    // the seats the per-player loops run over: 2, or 4 in the 4P mode
    int seat_count() const { return coop4_on() ? 4 : 2; }

    // PC Mode (docs/pcmode.md). The PC code tests frontend_state == 5 itself; the arcade logic has no PC Mode, so the
    // runtime sets `pcmode` (0 in every arcade game and in the proofs: nothing below changes the arcade then) and
    // the character per seat (THOTD.EXE 0x5CE4E4[p]; 0..6 of table 0x5CE348)
    int pcmode = 0;
    int pm_char[4] = {1, 0, 1, 2};
    bool pm_on() const
    {
        if constexpr (P::pc)
            return mem.template at<int32_t>(G::frontend_state) == 5;
        else
            return pcmode != 0;
    }
    // THOTD.EXE's character table 0x5CE348 (its defaults: the player's install has no cstats.cfg)
    struct PmStats {
        const char* name;
        int32_t type, model, portrait, sprite2, hp, rounds;
        float damage;
        int32_t reload_ms;
        float hit_size;
    };
    // pmextra (ReWrithe's, not THOTD.EXE's; user 2026-10-28): the Extra Characters after the PC's 7 (characters 7..19):
    // the cut-scene viewer's other actors (game/scene_view.cpp). Each plays by a PC character's rules (`base`: the men
    // researchers and both Dr. Curiens -> the PC's Researcher 6, the women researchers and the zombie Sophie -> Sophie
    // 5): pm_table gives the base's row (its type, models, numbers) under the extra's name, so the game logic is the
    // base's exactly; only the drawn body differs (`type`: Runtime::actor_pm_sync puts it in actorSwap, the viewer's
    // actor-swap draws). pcModel false: the 1998 PC files have no model of it (the base's body with --models pc)
    struct PmExtra { const char* name; int32_t type; int base; const char* key; bool pcModel; };
    static constexpr int kPmPcChars = 7, kPmCharCount = 20;
    static const PmExtra* pm_extra(int ch)
    {
        static const PmExtra e[kPmCharCount - kPmPcChars] = {
            {"Researcher (1)", 17, 6, "r1", true}, {"Researcher (2)", 19, 6, "r2", true},
            {"Researcher (3)", 20, 6, "r3", true}, {"Researcher (4)", 21, 6, "r4", true},
            {"Researcher (5)", 22, 6, "r5", true}, {"Researcher (6)", 49, 6, "r6", true},
            {"Researcher (woman)", 23, 5, "w1", true}, {"Researcher (woman, 2)", 25, 5, "w2", true},
            {"Researcher (woman, 3)", 26, 5, "w3", true}, {"Researcher (woman, 4)", 27, 5, "w4", true},
            {"Dr. Curien", 60, 6, "curien", true}, {"Dr. Curien (Dead)", 78, 6, "curien2", true},
            {"Sophie (zombie)", 67, 5, "zsophie", false},
        };
        return ch >= kPmPcChars && ch < kPmCharCount ? &e[ch - kPmPcChars] : nullptr;
    }
    // the PC character whose rules a character plays by (itself for 0..6)
    static int pm_rules_char(int ch) { const PmExtra* x = pm_extra(ch); return x ? x->base : ch; }
    static const PmStats& pm_table(int ch)
    {
        if (const PmExtra* x = pm_extra(ch)) {
            static const auto ext = [] {
                std::array<PmStats, kPmCharCount - kPmPcChars> a{};
                for (int i = 0; i < int(a.size()); ++i) {
                    a[size_t(i)] = pm_table(pm_extra(kPmPcChars + i)->base);
                    a[size_t(i)].name = pm_extra(kPmPcChars + i)->name;
                }
                return a;
            }();
            return ext[size_t(x - pm_extra(kPmPcChars))];
        }
        static const PmStats t[7] = {
            {"NULL PLAYER", 0x02, 0x188, 0xEF, 0xF5, 5, 8, 1.0f, 250, 1.0f},
            {"Rogan", 0x02, 0x188, 0xEF, 0xF5, 3, 6, 1.0f, 0, 1.0f},
            {"G", 0x0D, 0x189, 0xED, 0xF3, 3, 2, 4.0f, 250, 1.2f},
            {"Rogan 2", 0x0E, 0x1D38, 0xF0, 0xF6, 2, 12, 1.0f, 0, 0.8f},
            {"G 2", 0x0F, 0x1D39, 0xEE, 0xF4, 5, 6, 0.5f, 250, 1.2f},
            {"Sophie", 0x22, 0x1D3A, 0xF1, 0xF7, 4, 12, 0.5f, 500, 0.8f},
            {"Researcher", 0x1A, 0x1D3B, 0xF2, 0xF8, 2, 10, 1.5f, 500, 1.0f},
        };
        return t[ch >= 0 && ch < 7 ? ch : 1];
    }
    // seat p's character: the PC's 0x5CE4E4[p], the runtime's pm_char[p]
    int pm_seat_char(int p) const
    {
        if constexpr (P::pc)
            return mem.template at<int32_t>(G::pc_mode_chars + uint32_t(p) * 4);
        else
            return pm_char[p & 3];
    }
    // the creature type of seat p's body in PC Mode (+0x10 through 0x5CE358: player_char_models_set 0x50C114, the
    // endings 0x4AA52C-0x4AB148; character 0 (No Player) has Rogan's type 2)
    int32_t pm_body_type(int p) const { return pm_table(pm_seat_char(p)).type; }
    // the PC's table square root (pc_fast_sqrt 0x4FC76C / 0x41347C / 0x4D7934) for PC Mode on the arcade logic: the
    // exponent halved, the exponent's parity and the mantissa's top 7 bits index a 256-word table (THOTD.EXE 0x5CCEFC).
    // The table is rebuilt here: word i = the float bits of sqrt((1 + (i & 127) / 128) x (i & 128 ? 2 : 1)) - 0x3F800000
    // (all 256 words checked against THOTD.EXE, 2026-10-20). The PC's other maths for PM: port/pm_maths.h
    static float pm_fast_sqrt(float x) { return pm::fast_sqrt(x); }
    // the PC's own code path: the PC itself, or PC Mode / Boss Mode on the arcade logic, where the routines whose PC
    // branch is only the PC's maths and rules (the other enemy kinds' copies of the PC helpers, docs/pcmode.md row 10;
    // the PA rules every PC mode shares: kill credit, the researcher's gift, shots through scenery) run that branch:
    // `if (pc_path())` in place of `if constexpr (P::pc)`; PC Mode's own rules (characters, credits, reload) test
    // pm_on(). Boss Mode is a PC mode too (pccap 2026-10-27: its PC captures match frame by frame only on the PC's
    // maths). Arcade games never take it
    bool pc_path() const
    {
        if constexpr (P::pc)
            return true;
        else
            return pcmode != 0 || boss_mode != 0;
    }
    // x / d for d = 10, 100, 1000, 10000: THOTD.EXE never divides by these, it multiplies by the float reciprocal
    // (0.1 / 0.01 / 0.001 / 0.0001: 82 fmul, no fdiv in its code), so on the PC's path that rounds differently (pmrest2)
    float pdiv(float x, float d) const
    {
        if (pc_path())
            return x * float(1.0 / double(d));
        return x / d;
    }
    // PC Mode on the arcade logic: the PC's software matrix stack (pc_matrix_ptr's 0x30-byte slots on the PC) for the
    // PC branches that run there; pushed and popped inside one routine, never kept. pc_matrix_push / _identity /
    // _rotate_* / _translate* / _transform* work on it in Game<A> (port/pm_maths.h: the PC's maths, checked against
    // THOTD.EXE's own code by `hotd_proof pmmaths`)
    pm::Matrix pm_mstack[32];   // (32: a body's part tree pushes one slot per level, pmbody)
    int pm_msp = 0;
    pm::Matrix& pm_cur() { return pm_mstack[pm_msp & 31]; }
    // PC Mode / Boss Mode on the arcade logic (pmbody): the body drawing (object_draw, body_draw, part_draw and the part
    // hooks' turns) also runs the PC's body transform (THOTD.EXE body_draw 0x477B00, its part draw 0x476B70, the body
    // file's helper copies) on PM's matrix stack, in step with the coprocessor's, and the values the logic reads
    // (positions, the direction, the home, view_pos, the parts' hit-sphere centres) are the PC's; the coprocessor's
    // matrices still draw. Never on the PC itself (Game<Pc> has no drawing)
    bool pm_body() const
    {
        if constexpr (P::pc)
            return false;
        else
            return pcmode != 0 || boss_mode != 0;
    }
    // PC Mode's camera paths: the BODY of the PC's CAM\ALLSTAGE\CAMALL.RBH as words (offsets in bytes from its start;
    // set by the host from the PC files, empty without them: the ROM's keys then; camera_path_eval)
    std::vector<uint32_t> pm_cam;
    // the PC's motion table (THOTD.EXE 0x5EFBD0.., its pointer words as bytes; set by the host from the PC files): the
    // PC's body_init leaves frame_data pointing at the table entry (&table[motion], no dereference), so a blend
    // started before the body's first drawing starts from those pointer words (pmbody; body_init, body_pose_copy)
    std::vector<uint8_t> pm_motion_tbl;
    pm::Matrix pm_body_m;   // the body matrix of the body being drawn (the PC's +0x1EC copy; the arcade's is +0x1E4)
    // the PC's sin (k 0) / cos (k 1) table pc_trig_table at a 16-bit angle (index / 16, truncated), on either version
    float pc_trig(uint32_t angle, int k)
    {
        if constexpr (P::pc) {
            const int32_t i = (int32_t(int16_t(angle & 0xFFFF)) / 16) & 0xFFF;
            return mem.f32(G::pc_trig_table + uint32_t(i) * 8 + uint32_t(k) * 4);
        } else {
            return pm::trig(angle, k);
        }
    }
    // the current matrix's element i, on either version (the PC: in its memory at pc_matrix_ptr)
    float pcm(int i)
    {
        if constexpr (P::pc)
            return mem.f32(mem.u32(G::pc_matrix_ptr) + uint32_t(i) * 4);
        else
            return pm_cur().e[i];
    }
    void pcm_set(int i, float v)
    {
        if constexpr (P::pc)
            mem.set32(mem.u32(G::pc_matrix_ptr) + uint32_t(i) * 4, fbits(v));
        else
            pm_cur().e[i] = v;
    }
    // the matrix stack popped (pc_matrix_ptr - 0x30)
    void pcm_pop()
    {
        if constexpr (P::pc)
            mem.set32(G::pc_matrix_ptr, mem.u32(G::pc_matrix_ptr) - 0x30);
        else
            --pm_msp;
    }
    // the identity copied into the current matrix inline (rep movsd from pc_identity_matrix)
    void pcm_identity()
    {
        if constexpr (P::pc) {
            const uint32_t m = mem.u32(G::pc_matrix_ptr);
            for (uint32_t k = 0; k < 0x30; k += 4)
                mem.set32(m + k, mem.u32(G::pc_identity_matrix + k));
        } else {
            pm_cur().identity();
        }
    }
    // the PC's stored matrix 1 (the camera) copied into the current matrix (PM: pm_camera)
    void pcm_load_camera()
    {
        if constexpr (P::pc) {
            const uint32_t m = mem.u32(G::pc_matrix_ptr);
            for (uint32_t k = 0; k < 0x30; k += 4)
                mem.set32(m + k, mem.u32(G::pc_stored_matrices + 0x30 + k));
        } else {
            for (int i = 0; i < 12; ++i)
                pm_cur().e[i] = pm_camera[i];
        }
    }
    // the PC's stored matrix 5 (camera record 0's inverse) copied into the current matrix (PM: pm_camera0_inv)
    void pcm_load_stored5()
    {
        if constexpr (P::pc) {
            std::memmove(mem.ptr(mem.u32(G::pc_matrix_ptr)), mem.ptr(G::pc_stored_matrices + 5 * 0x30), 0x30);
        } else {
            for (int i = 0; i < 12; ++i)
                pm_cur().e[i] = pm_camera0_inv[i];
        }
    }
    // the PC's stored matrix 6 (camera record 0's view) copied into the current matrix (PM: pm_camera0)
    void pcm_load_stored6()
    {
        if constexpr (P::pc) {
            std::memmove(mem.ptr(mem.u32(G::pc_matrix_ptr)), mem.ptr(G::pc_stored_matrices + 6 * 0x30), 0x30);
        } else {
            for (int i = 0; i < 12; ++i)
                pm_cur().e[i] = pm_camera0[i];
        }
    }
    // the PC's pc_molb_height_offset (0x5D7F48 ..: 12 for Chapter 1 event 4 scene 1's Murrer members, else 0), on
    // either version (PM: a runtime copy)
    float molb_height_offset()
    {
        if constexpr (P::pc)
            return mem.f32(G::pc_molb_height_offset);
        else
            return ffrom(pm_molb_height_bits);
    }
    void molb_height_offset_set(uint32_t bits)
    {
        if constexpr (P::pc)
            mem.set32(G::pc_molb_height_offset, bits);
        else
            pm_molb_height_bits = bits;
    }
    uint32_t pm_molb_height_bits = 0;
    // the PC-only matrix helpers' default bodies (decl/*.h; the proofs replay them as boundaries): PC Mode on the
    // arcade logic runs them on its own stack
    void pmx_translate(float x, float y, float z)
    {
        if constexpr (!P::pc)
            pm_cur().translate(x, y, z);
        else
            (void)x, (void)y, (void)z;
    }
    void pmx_rotate_x(uint32_t a)
    {
        if constexpr (!P::pc)
            pm_cur().rotate_x(a);
        else
            (void)a;
    }
    void pmx_rotate_y(uint32_t a)
    {
        if constexpr (!P::pc)
            pm_cur().rotate_y(a);
        else
            (void)a;
    }
    void pmx_rotate_z(uint32_t a)   // pc_matrix_rotate_z* and pc_matrix_rotate_b* (a z turn)
    {
        if constexpr (!P::pc)
            pm_cur().rotate_z(a);
        else
            (void)a;
    }
    void pmx_identity()
    {
        if constexpr (!P::pc)
            pm_cur().identity();
    }
    // pc_vec_angles9: the yaw / pitch of (x, y, z) written to two addresses
    void pmx_vec_angles(float x, float y, float z, uint32_t yawAt, uint32_t pitchAt)
    {
        if constexpr (!P::pc) {
            uint32_t yaw = 0, pitch = 0;
            pm::vec_angles(x, y, z, yaw, pitch);
            mem.set32(yawAt, yaw);
            mem.set32(pitchAt, pitch);
        } else {
            (void)x, (void)y, (void)z, (void)yawAt, (void)pitchAt;
        }
    }
    // pc_matrix_angles8: the current matrix's angles written to three addresses (pm::matrix_angles8)
    void pmx_matrix_angles8(uint32_t outA, uint32_t outB, uint32_t outC)
    {
        if constexpr (!P::pc) {
            uint32_t a = 0, b = 0, c = 0;
            pm::matrix_angles8(pm_cur(), a, b, c);
            mem.set32(outA, a);
            mem.set32(outB, b);
            mem.set32(outC, c);
        } else {
            (void)outA, (void)outB, (void)outC;
        }
    }
    // PC 0x4C5924 tumble_corners for PC Mode's held objects: the eight box corners (x outer, z inner) through the
    // current matrix of PM's stack (the file's transform copy 0x4C1B60), into the arcade's corner buffer 0x5434C0
    // that tumble_update reads
    void pm_tumble_corners(uint32_t box)
    {
        uint32_t i = 0;
        for (uint32_t xo : {0u, 4u})
            for (uint32_t yo : {8u, 0xCu})
                for (uint32_t zo : {0x10u, 0x14u}) {
                    float o[3];
                    pm_cur().transform(mem.f32(box + xo), mem.f32(box + yo), mem.f32(box + zo), o[0], o[1], o[2]);
                    for (uint32_t k = 0; k < 3; ++k)
                        mem.set32(0x5434C0 + 12 * i + 4 * k, fbits(o[k]));
                    ++i;
                }
    }
    // pc_matrix_mul8: the current matrix times the 12 floats at `matrix`
    void pmx_mul(uint32_t matrix)
    {
        if constexpr (!P::pc) {
            float m[12];
            for (uint32_t k = 0; k < 12; ++k)
                m[k] = mem.f32(matrix + 4 * k);
            pm_cur().mul(m);
        } else {
            (void)matrix;
        }
    }
    // the PC's camera matrix (its stored matrix 1, camera_matrices_update) for PC Mode on the arcade logic: set by
    // camera_matrices_update in PM, read by the shot ray (docs/pcmode.md row 10)
    float pm_camera[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    // its inverse (the PC's stored matrix 0, also its current matrix after camera_matrices_update: the one enemy code
    // transforms points with, e.g. enemy_target_distance's screen side)
    float pm_camera_inv[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    // camera record 0's inverse (the PC's stored matrix 5; the Murrers' second view position)
    float pm_camera0_inv[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    // camera record 0's view (the PC's stored matrix 6: the thrown objects' launch frame)
    float pm_camera0[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    // PC Mode on the arcade logic's boss code (docs/pcmode.md row 10, pmboss): the bosses have only their arcade port
    // (no Game<Pc> form), so their PC branches are PM-only and work on local pm::Matrix values
    bool pm_boss() const
    {
        if constexpr (P::pc)
            return false;
        else
            return pcmode != 0 || boss_mode != 0;   // (pmbody: Boss Mode too: the PC runs its own boss code there)
    }
    // the PC's stored matrices 1 (camera), 0 (its inverse), 5 / 6 (camera record 0's inverse / view) as pm matrices
    pm::Matrix pm_m1() const { return pm::from_floats(pm_camera); }
    pm::Matrix pm_m0() const { return pm::from_floats(pm_camera_inv); }
    pm::Matrix pm_m5() const { return pm::from_floats(pm_camera0_inv); }
    pm::Matrix pm_m6() const { return pm::from_floats(pm_camera0); }
    // the PC's view test (pc_view_test and its copies) on PM's stored matrix 0
    float pm_view_test(float x, float y, float z, float r) const { return pm::view_test(pm_camera_inv, x, y, z, r); }
    // the arcade coprocessor's current matrix (a body part's, while the arcade's body drawing calls a part hook) in
    // the PC's layout: PM runs the PC's maths on it (the PC's own part matrices come from its renderer)
    pm::Matrix pm_copro_current()
    {
        if (pm_body())   // pmbody: during the body drawing PM's own stack holds the PC's part matrix
            return pm_cur();
        Tgp::Out o;
        copro(0x11, nullptr, 0, o);
        return pm::from_copro_words(o.w);
    }
    // a unit vector: the coprocessor's normalise 0x0C, in PC Mode the boss files' inline form (1 / pc_fast_sqrt((x x +
    // y y) + z z), each part times it)
    Tgp::Out boss_unit(uint32_t x, uint32_t y, uint32_t z)
    {
        Tgp::Out o;
        if (pm_boss()) {
            const float d[3] = {ffrom(x), ffrom(y), ffrom(z)};
            float u[3];
            pm::normalize(d, u);
            for (int k = 0; k < 3; ++k)
                o.w[k] = fbits(u[k]);
            o.n = 3;
        } else {
            const uint32_t in[3] = {x, y, z};
            copro(0x0C, in, 3, o);
        }
        return o;
    }
    // the boss files' placing pattern: a matrix with the translation t (coprocessor 0x88), turned about z (0x16, when
    // rz) then y (0x15), the point (x, y, z) through it (0x1A). PC Mode: a local pm::Matrix (the PC writes the
    // translation words, then its rotate / transform helper copies); the arcade on the coprocessor's current matrix
    // (the caller's push / pop around it)
    Tgp::Out boss_xform(const uint32_t t[3], bool rz, uint32_t az, uint32_t ay, float x, float y, float z)
    {
        Tgp::Out o;
        if (pm_boss()) {
            pm::Matrix m;
            for (int k = 0; k < 3; ++k)
                m.e[9 + k] = ffrom(t[k]);
            if (rz)
                m.rotate_z(az);
            m.rotate_y(ay);
            float r[3];
            m.transform(x, y, z, r[0], r[1], r[2]);
            for (int k = 0; k < 3; ++k)
                o.w[k] = fbits(r[k]);
            o.n = 3;
            return o;
        }
        Tgp::Out none;
        copro(0x88, t, 3, none);
        if (rz)
            copro(0x16, &az, 1, none);
        copro(0x15, &ay, 1, none);
        const uint32_t p[3] = {fbits(x), fbits(y), fbits(z)};
        copro(0x1A, p, 3, o);
        return o;
    }
    // a point through stored matrix n (coprocessor 0x74 n, 0x1A, 0x06); PC Mode: the PC's stored matrix n (0, 1, 5,
    // 6: pm_camera_inv / pm_camera / pm_camera0_inv / pm_camera0) through the boss file's transform copy
    Tgp::Out boss_stored_xform(uint32_t n, uint32_t x, uint32_t y, uint32_t z)
    {
        Tgp::Out o;
        if (pm_boss()) {
            const pm::Matrix m = n == 0 ? pm_m0() : n == 5 ? pm_m5() : n == 6 ? pm_m6() : pm_m1();
            float r[3];
            m.transform(ffrom(x), ffrom(y), ffrom(z), r[0], r[1], r[2]);
            for (int k = 0; k < 3; ++k)
                o.w[k] = fbits(r[k]);
            o.n = 3;
            return o;
        }
        Tgp::Out none;
        copro(0x74, &n, 1, none);
        const uint32_t p[3] = {x, y, z};
        copro(0x1A, p, 3, o);
        copro(0x06, nullptr, 0, none);
        return o;
    }
    // a camera path point for the boss code: in PC Mode at the frame chopped to an integer (the PC's camera_path_eval
    // 0x461980 chops it (pc_float_chop, fistp) and evaluates the same keyframed curves at that integer frame)
    uint32_t boss_path_frame(uint32_t frame) const
    {
        if (!pm_boss())
            return frame;
        return fbits(float(pm::fistp(std::trunc(double(ffrom(frame))))));
    }
    pm::Matrix pm_copro_words_at(uint32_t a)
    {
        uint32_t w[12];
        for (uint32_t k = 0; k < 12; ++k)
            w[k] = mem.u32(a + 4 * k);
        return pm::from_copro_words(w);
    }
    // the PC's C library rand seed (THOTD.EXE's per-thread seed: 1 at start-up; the runtime: 1 at each PC Mode game's
    // start) for the dogs' crand in PM (docs/pcmode.md row 10)
    uint32_t pm_crand_seed = 1;
    // the PC's Chariot flag 0xDA67FC (no arcade copy): its hit box is shootable only once it is down (1 at set-up in
    // Chapter 1, else set when chariot_act2_land lands). Read only in PC Mode
    bool pm_chariot_landed = false;
    // the runtime: whether a model id can be drawn (the PC Mode characters' own car models 0x1D38.. exist only in the
    // PC files); false: the caller draws the arcade's model instead
    virtual bool pm_model_drawable(uint32_t model) { (void)model; return false; }
    // carchars (draws only): player_char_props_draw is about to draw seat `seat`'s car model; the runtime notes the
    // draw to put the seat's own character there after the frame (nothing in the game state)
    virtual void car_seat_mark(int seat) { (void)seat; }
    // the gun's full load: 6, in PC Mode the seat's character's rounds (+0x24, low byte); Survival Mode: the seat's
    // magazine (survivalRounds: the handgun's or the SMG's, the Big Mag perk's)
    uint8_t pm_full_rounds(int p) const
    {
        return survivalRounds[p & 3] ? uint8_t(survivalRounds[p & 3]) : pm_on() ? uint8_t(pm_table(pm_seat_char(p)).rounds) : uint8_t(6);
    }
    int survivalRounds[4] = {};   // Survival Mode (game/survival.h): each seat's magazine; 0 everywhere else (set by the runtime)
    // Survival Mode (survival2, user 2026-11-03: points as in Call of Duty's Zombies): enemy_part_hits tells the runtime
    // of each frame a creature was hit (the seats whose shots hit it: bits; its killer when the hits killed it, else -1;
    // the seats that hit its head, part class 1: bits) and the game's own add_score does nothing (the score is
    // Survival's points). survivalScoring false (every other game, the proofs): neither
    bool survivalScoring = false;
    uint32_t survivalHitSeats = 0;   // (part_hit: the seats whose shots hit a part of the creature this frame; read only by Survival)
    virtual void survival_hits(uint32_t obj, uint32_t seats, int killer, uint32_t headSeats) { (void)obj, (void)seats, (void)killer, (void)headSeats; }
    // Survival Mode: body slots past the original's (player_bodies 0x51F440, 16 words: the creatures' slots 0-13, the
    // players' bodies 14 / 15; a body's slot +0x2E6 also picks its place in the three skin buffers, 0x348 bytes a slot):
    // slots 16.. in a table of the runtime's (extraBodyTable, extraBodySlots words), used only when 0-13 are all taken.
    // 0 everywhere else: the original's 14 (a 15th creature got no slot and shared slot 0's skin and table entry)
    int extraBodySlots = 0;
    uint32_t extraBodyTable = 0;
    uint32_t body_slot_addr(int32_t slot) const
    {
        return extraBodySlots && slot >= 16 ? extraBodyTable + 4 * uint32_t(slot - 16) : G::player_bodies + 4 * uint32_t(slot);
    }
    // the runtime's reload timer (pc_reload_time 0x5CE4DC[p] on the PC): the frame clock in ms at the last reload
    // (0 = none), for the arcade logic; the runtime advances pm_clock_ms (1000 / 60 ms a frame)
    uint32_t pm_reload_at[4] = {};
    // testing only (CPU teammate measurements, --team-stats): per seat the shots that hit an object or a body part
    // (shot_hit_nearest; scene items not counted) and the hostages / researchers shot. Never read by the game (not in
    // the rollback snapshot or the desync hash)
    uint32_t stat_hits[4] = {}, stat_hostages[4] = {};
    // (team 2026-11-03) the object the seat's last hit landed on (a body part's owner): the CPU teammates' kill counts
    uint32_t stat_last_hit[4] = {};
    // PC Mode's continues (docs/pcmode.md row 8): THOTD.EXE pc_continues_init 0x4488E4 gives credits = the Continues
    // option (0xD5B1E0, 0-9; set by the runtime) + 1, one pool for both players (0xDA0B98 = 0: always slot 0 of
    // 0xD9F790); a start, a join and a continue each cost 1 (0xDA0B7C / 0xDA0B88; 0x448934 takes them, never below
    // 0); can_continue 0x45B8F0: credits - 1 >= 0. pm_credits_seen: game_continue_begin's snapshot (0xDA0EF0).
    // Arcade games never read these (free play, as the arcade)
    int pm_continues = 5;
    int pm_credits = 0, pm_credits_seen = 0;
    // the Cheat screen's Free-Play (0xD5B35C; set by the runtime): pc_continues_init copies it to 0xDA0B8C, and with it
    // can_continue 0x45B8F0 is always 1 and 0x448934 takes no credit (the game start's own -1, 0x43BAD8, still runs)
    bool pm_free_play = false;
    bool pm_can_continue() const { return pm_free_play || pm_credits >= 1; }
    void pm_credit_take()   // 0x448934 (a join / a continue: cost 1, the shared slot 0, never below 0)
    {
        if (!pm_free_play)
            pm_credits = pm_credits > 1 ? pm_credits - 1 : 0;
    }
    uint32_t pm_clock_ms = 0;
    // the Auto Reload setting for PC Mode (the PC's 0xD5B1F4[p] == 1; the runtime's launcher setting, one for all
    // seats): with it pc_gun_input refills an empty gun itself (player_shoot's PC Mode branch)
    bool pm_auto_reload = false;
    // the runtime's copy of the PC's pc_blood_task_count (0x5D7F6C): live part_bleed + blood_drip tasks; a new one only
    // below 9 (THOTD.EXE 0x4864DC / 0x486520 / 0x486378 count up, the tasks' exits 0x4863FC / 0x4862AC down; sprite_init
    // 0x412660 zeroes it, the runtime at each chapter start). PC Mode on the arcade logic only (never Game<Pc>)
    int32_t pm_blood_tasks = 0;
    bool pm_blood_take()   // false: no new bleed / drip task (the PC's cap)
    {
        if constexpr (!P::pc) {
            if (pm_on()) {
                if (pm_blood_tasks > 8)
                    return false;
                ++pm_blood_tasks;
            }
        }
        return true;
    }
    void pm_blood_end()   // a bleed / drip task's exit
    {
        if constexpr (!P::pc)
            if (pm_on())
                --pm_blood_tasks;
    }
    // the runtime's copy of the PC's pc_last_killer (0xDA0AE8): set by every kill credit, read by the tie rule and the
    // researcher's life gift (PC Mode only)
    int32_t pm_last_killer = 0;
    // a seat that starts with character 0 (No Player): THOTD.EXE player_waiting_frame 0x436B60 gives it Rogan, or G
    // when the other player is Rogan; 4P: the first of Rogan / G no other seat has, else Rogan (docs/pcmode.md)
    void pm_join_char(int p)
    {
        if constexpr (!P::pc) {
            if (!pm_on() || pm_char[p & 3] != 0)
                return;
            bool rogan = false, g = false;
            for (int s = 0; s < seat_count(); ++s)
                if (s != (p & 3)) {
                    rogan |= pm_char[s] == 1;
                    g |= pm_char[s] == 2;
                }
            pm_char[p & 3] = !rogan ? 1 : !g ? 2 : 1;
        } else {
            (void)p;
        }
    }
    // a per-player entry: seats 0 / 1 the original's (base + p x stride), seats 2 / 3 the 4P block's (ext + ...)
    static constexpr uint32_t seat_at(uint32_t base, uint32_t ext, uint32_t stride, int p)
    {
        return p < 2 ? base + uint32_t(p) * stride : ext + uint32_t(p - 2) * stride;
    }
    // player p's record (Player, 0xCC): seats 0 / 1 at G::players, 2 / 3 in the 4P block
    static constexpr uint32_t player_rec(int p) { return seat_at(G::players, coop4::kPlayers, 0xCC, p); }

    // One coprocessor call (function n, FIFO input words); the game issues them through ports 0x880000 / 0x884000.
    virtual void copro(int n, const uint32_t* in, int count, Tgp::Out& out) { tgp.call(n, in, count, out); }

    // The original's stack locals that it passes by address (point lists, output words) live in the original address
    // space too: local_alloc takes them from localTop (set by whoever runs the port: the proof harness puts it in the
    // routine's stack window), a LocalScope gives them back at the end of the routine.
    uint32_t localTop = 0;
    uint32_t local_alloc(uint32_t size) { const uint32_t a = localTop; localTop += (size + 15) & ~15u; return a; }
    struct LocalScope {
        Game& g;
        uint32_t saved;
        explicit LocalScope(Game& game) : g(game), saved(game.localTop) {}
        ~LocalScope() { g.localTop = saved; }
    };
    // after a boundary call: its output through pointer argument `arg` lands at `to` (the proof harness copies it
    // from where the original's local was; a real build needs nothing here)
    virtual void relocate(int arg, uint32_t to, uint32_t size) { (void)arg; (void)to; (void)size; }

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100)   // the stubs ignore their arguments
#endif
    // the routines, one file per system (docs/port.md "Adding a kind"): the core, then one per kind, then the
    // core enemy systems and the boundaries; a kind's own new boundary stubs go in its file
#include "port/decl/core.h"
#include "port/decl/molb.h"
#include "port/decl/scene_zombie.h"
#include "port/decl/researchers.h"
#include "port/decl/kit_zombie.h"
#include "port/decl/bats.h"
#include "port/decl/dogs.h"
#include "port/decl/frog.h"
#include "port/decl/agile.h"
#include "port/decl/event.h"
#include "port/decl/camera.h"
#include "port/decl/creature.h"
#include "port/decl/player.h"
#include "port/decl/walkers.h"
#include "port/decl/hostages.h"
#include "port/decl/scene_objects.h"
#include "port/decl/hangedman.h"
#include "port/decl/props.h"
#include "port/decl/chariot.h"
#include "port/decl/hangedman_fight.h"
#include "port/decl/sophie.h"
#include "port/decl/player_body.h"
#include "port/decl/ch2_kinds.h"
#include "port/decl/props_ch2.h"
#include "port/decl/hostages_ch2.h"
#include "port/decl/ch3_kinds.h"
#include "port/decl/props_ch3.h"
#include "port/decl/hostages_ch3.h"
#include "port/decl/hermit.h"
#include "port/decl/ch4_kinds.h"
#include "port/decl/props_ch4.h"
#include "port/decl/magician.h"
#include "port/decl/ending.h"
#include "port/decl/core_enemy.h"
#include "port/decl/support.h"
#include "port/decl/support_b.h"
#include "port/decl/core_bounds.h"
#include "port/decl/coop4.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

private:
    float floor_distance(float ax, float az, float bx, float bz);             // coprocessor 0x1F on y = 0
    void guard_link_prey(uint32_t obj);                                       // inline in enemy_guard
    void guard_leave(uint32_t obj);                                           // inline in actions 1 / 20
    void guard_bite_slot(uint32_t obj, uint32_t next);                        // inline in the run-after routines
    bool guard_ch2_release(uint32_t obj);                                     // inline in the scripted routines
    void guard_depth_add(uint32_t obj);                                       // inline in the scripted routines
    void guard_prey_lost(uint32_t obj, uint32_t prey, uint32_t next);         // inline in the run-after routines
    void shot_test_list(int player, uint32_t list, int countOffset);          // body of 0x45850 / 0x45B60
    float pc_fast_sqrt_table(float x);                                        // the body of the three PC copies
};

using Arcade = Game<A>;
using PcGame = Game<Pc>;

} // namespace hotd::port
