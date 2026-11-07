// The runtime host: the arcade's task system and heap on the original memory, the routine dispatch, game_frame
// (runtime.h). The task loop (0x14CE0 .. 0x15150) read from the disassembly; UNVERIFIED (teaser): no capture proof yet
// (they are exercised by every frame the runtime plays). The heap and the task creation: the port's (tasks.cpp).
#include "game/runtime.h"
#include "game/diff_replay.h"
#include "game/cosmods.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstring>

namespace hotd::game {

using G = A::G;
using F = A::F;

namespace {
constexpr uint32_t kTaskNop = 0x14E90;       // the default exit routine (ret)
constexpr uint32_t kTaskIdle = 0x14E70;      // task_group_idle: the ring's head; unwinds with 3
constexpr uint32_t kTaskGroupTask = 0x14CE0;
constexpr uint32_t kTaskUnlink = 0x15110;
} // namespace

std::string Runtime::routineName(uint32_t routine) const
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%X", routine);
    auto it = names.find(routine);
    return it == names.end() ? std::string(buf) : std::string(buf) + " " + it->second;
}

void Runtime::missing(const char* name, uint32_t addr)
{
    if (missingCount[name]++ == 0 && log)
        log(std::string("boundary not implemented: ") + name);
    if (addr == 0)   // a generated stub without an address comment, or a runtime routine's unported part
        for (const auto& [a, n] : names)
            if (n == name) {
                addr = a;
                break;
            }
    boundaryHit(name, addr, addr != 0 || std::strchr(name, ' ') == nullptr ? "stub" : "partial");
    // a creature spawned without the creature core (L3) would run on an uninitialised body: its task ends here (the
    // event watchdog passes the waits on it). Gone by itself once body_init / creature_type_setup are ported.
    const std::string n = name;
    if (cuts.guards && (n == "body_init" || n == "creature_type_setup") &&
        mem.u32(mem.u32(G::task_current)) != F::evt_run)
        throw Halt{};
}

void Runtime::debug_halt()
{
    if (log)
        log("debug_halt (the original's endless loop after an error text)");
    throw Halt{};
}

// ---------------------------------------------------------------------------------------------------------------
// The heap (mem_alloc, mem_free ...) and task_create_child / task_clear_work: the port's (src/port/tasks.cpp,
// proofs-7). Tasks: +0 routine, +4 exit routine, +8 serial, +0xC magic, +0x10 / +0x14 the ring (prev / next), +0x18
// the creating task, +0x1C its +8.

// 0x14EA0: a new task at the end of the current group's ring
uint32_t Runtime::task_create(uint32_t routine, uint32_t size)
{
    const uint32_t p = port::Game<A>::task_create(routine, size);
    // debugging (HOTD_TRACE_SPRITES): the item pickups and their on-screen gifts (the 3D items drawn over the picture)
    static const bool trace = std::getenv("HOTD_TRACE_SPRITES") != nullptr;
    if (trace && (routine == F::hostage_gift_task || routine == F::hostage_gift_fly || routine == F::gift_effect ||
                  routine == F::item_sparkle || routine == F::item_life_frame || routine == F::prop_coin_run ||
                  routine == F::prop_gold_frog_init))
        std::fprintf(stderr, "frame %llu task %s\n", (unsigned long long)frameCount, routineName(routine).c_str());
    return p;
}

// 0x150D0: the exit routine, then the task becomes task_unlink (freed on its next turn); back to the task loop
void Runtime::task_exit()
{
    const uint32_t cur = mem.u32(G::task_current);
    run(mem.u32(cur + 4), cur);
    mem.set32(cur + 8, 0);
    mem.set32(cur + 0xC, 0);
    mem.set32(cur, kTaskUnlink);
    throw Unwind{1};
}

// 0x15110: out of the ring, freed; the loop goes on with the next task
void Runtime::task_unlink(uint32_t task)
{
    const uint32_t prev = mem.u32(task + 0x10), next = mem.u32(task + 0x14);
    mem.set32(prev + 0x14, next);
    mem.set32(next + 0x10, prev);
    mem_free(task);
    set_task_current(next);
    throw Unwind{2};
}

// 0x14CE0: a new group's head: its start table's routines once, then the idle head
void Runtime::task_group_task(uint32_t group)
{
    for (uint32_t t = mem.u32(group + 0x20); mem.u32(t) != 0; t += 4)
        run(mem.u32(t), 0);
    mem.set32(group, kTaskIdle);
    throw Unwind{3};
}

void Runtime::set_task_current(uint32_t t)
{
    mem.set32(G::task_current, t);
    if (diffReplay)
        diffReplay->noteTask(t);
}

// 0x14D40: every task of the ring once, from the one after the head; the head (task_group_idle) ends the run
void Runtime::task_group_run(uint32_t group)
{
    const uint32_t savedLocal = localTop;
    set_task_current(group);
    mem.set32(G::task_group_current, group);
    bool advance = true;
    for (;;) {
        uint32_t t = mem.u32(G::task_current);
        if (advance)
            t = mem.u32(t + 0x14);
        advance = true;
        set_task_current(t);
        try {
            run(mem.u32(t), t);
        } catch (const Unwind& u) {
            localTop = savedLocal;
            if (u.code == 3) {
                // the depth list (the shootable objects) the tasks built, before game_frame's shot test clears it
                // (the bot / --autofire aim at them)
                shootable.clear();
                for (uint32_t i = 0; i < mem.u32(G::depth_list_count); ++i)
                    shootable.push_back(mem.u32(G::depth_list + 12 * i + 4));
                return;
            }
            if (u.code == 2)
                advance = false;
        } catch (const Halt&) {
            // the original stops for good; the runtime ends the task instead (logged)
            localTop = savedLocal;
            const uint32_t cur = mem.u32(G::task_current);
            if (log)
                log("task " + routineName(mem.u32(cur)) + " halted: removed");
            mem.set32(cur, kTaskUnlink);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------

bool Runtime::dispatch(uint32_t routine, const uint32_t* regs)
{
    Runtime& a = *this;
    auto f = [&](int i) { float v; std::memcpy(&v, &regs[i], 4); return v; };
    (void)f;
    // testing (hangcmp): HOTD_STALL_ROUTINE=<hex address> treats that routine as not ported (a stall for the guard)
    static const uint32_t stall = std::getenv("HOTD_STALL_ROUTINE") ? uint32_t(std::strtoul(std::getenv("HOTD_STALL_ROUTINE"), nullptr, 16)) : 0;
    if (stall != 0 && routine == stall)
        return false;
    if (routine == 0x9A040 || routine == 0x9A280 || routine == 0x87560)   // relfix: the bosses' part hooks (Big Head; draws only)
        drawBossPart_ = regs[0];
    switch (routine) {
#include "dispatch_arcade.inc"   // every src/proof/cases/*_arcade.inc (generated by CMakeLists.txt)
    default: return false;
    }
}

void Runtime::run(uint32_t routine, uint32_t arg)
{
    // a routine chain this deep is a loop through unported code (the original would hang or overflow too): logged,
    // and the task is ended as after a debug_halt
    struct Depth {
        std::vector<uint32_t>& s;
        Depth(std::vector<uint32_t>& v, uint32_t r) : s(v) { s.push_back(r); }
        ~Depth() { s.pop_back(); }
    } depth(runStack_, routine);
    if (runStack_.size() > 64) {
        if (log) {
            std::string chain;
            for (size_t i = runStack_.size() - 6; i < runStack_.size(); ++i)
                chain += " " + routineName(runStack_[i]);
            log("routine chain too deep, last:" + chain);
        }
        throw Halt{};
    }
    if (routine == 0x9B4C0 || routine == 0x9B380)   // Sonic Mode (game/sonic.h): a voice line (kind 161) is on
        voiceFrame = frameCount;
    switch (routine) {
    case kTaskNop: return;
    case kTaskIdle: throw Unwind{3};
    case kTaskGroupTask: task_group_task(arg); return;
    case kTaskUnlink: task_unlink(arg); return;
    case 0x5D90: input_task_stand_in(arg); return;
    case 0x50B0:   // player_in_game_frame (the port, L3); --lives-kept (testing) keeps one life
        if (livesKept && !((livesKeptSkip >> (mem.at<int8_t>(arg + 0x20) & 7)) & 1) &&
            mem.at<port::a::Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(arg + 0x20)))).lives <= 0) {
            missing("player out of lives (kept at 1: --lives-kept)");
            mem.at<port::a::Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(arg + 0x20)))).lives = 1;
        }
        if (const int p = mem.at<int8_t>(arg + 0x20); (p == 0 || p == 1) && (cheatInfinite & (1 << p))) {
            // the PC's SKIDMARX cheat (pc_gun_input 0x4358CC with 0x5CD374 and key bit 1 / 2 of 0x5CCEF8: rounds and
            // lives refilled every frame; docs/teaser_status/pc_cheats.md): ours keeps the most rounds / lives seen
            port::a::Player& pl = mem.at<port::a::Player>(G::players, uint32_t(p));
            cheatRounds[p] = std::max<int>(cheatRounds[p], pl.rounds);
            cheatLives[p] = std::max<int>(cheatLives[p], pl.lives);
            if (pl.state == 5) {
                pl.rounds = uint8_t(cheatRounds[p]);
                pl.empty = 0;
                pl.lives = cheatLives[p];
            }
        }
        player_in_game_frame(arg);
        return;
    case port::coop4::kSeatRoutine:   // 4P (docs/coop4.md): seat 3 / 4's frame; --lives-kept as above
        if (port::a::Player& pl = mem.at<port::a::Player>(player_rec(mem.at<int8_t>(arg + 0x20)));
            livesKept && !((livesKeptSkip >> (mem.at<int8_t>(arg + 0x20) & 7)) & 1) && pl.state == 5 && pl.lives <= 0)
            pl.lives = 1;
        {
            const int p = mem.at<int8_t>(arg + 0x20);
            const int32_t was = mem.at<port::a::Player>(player_rec(p)).state;
            coop4_seat_frame(arg);
            const int32_t now = mem.at<port::a::Player>(player_rec(p)).state;
            if (now != was && log)   // the 4P seats' state changes (as player_start_check's lines for players 1 / 2)
                log("player " + std::to_string(p + 1) + (now == 4 ? " out of lives (CONTINUE?)" : now == 6 ? " game over" : now == 5 ? (was == 4 ? " continues" : " joins") : now == 9 ? " waits" : " state " + std::to_string(now)));
        }
        return;
    case 0x44840: letterbox_task(arg); return;
    case 0x5470: player_st6_game_over(arg); return;
    case 0x5540: player_game_over_wait(arg); return;
    case 0x97740: game_over_state0_camera(); return;   // the port's (game_over.cpp, proofs-5)
    case 0x97490: game_over_camera(arg); return;
    case 0x97770: camera_globals_reset(); return;
    case 0x5CCA0: game_over_camera_start(); return;
    case 0x5D4F0: game_over_camera_task(); return;
    case 0x5D4D0: game_over_route_start(); return;
    case 0x5CF50: game_over_route_task(arg); return;
    case 0x5CE70: game_over_walkers_start(); return;
    case 0x5CCD0: game_over_walker_task(arg); return;
    // T1 (t1_bounds.cpp) UNVERIFIED (teaser)
    case 0x1DCD0: hostage_shot_effect(arg); return;
    case 0x5DC0: hit_screen_shake(); return;   // shake_task: shake_nop + hit_screen_shake
    case 0x2E110: return;                      // molb_floor_none
    case 0x1DB30: researcher_blood_effect(arg); return;
    case 0x5720: mem.set32(arg, 0x5730); return;   // player_st10_paused (T1)
    case 0x5730: player_paused(arg); return;
    case 0x9CE60: body_part_draw_hook(arg); return;
    case 0x87550: return;                          // object_draw_none (draw mode 3: ret)
    case 0x55D0: return;                           // player_idle (the ending's state 7 players: ret)
    // ported (L2) but not in the proof registry: the CAMDR set-ups the event interpreter calls through call_routine
    case 0x66AD0:   // evt_run (the port) after the teaser's guards (cut 1)
        if (survival.active() && survival.eventFrame(*this))
            return;   // Survival Mode: the script frozen at the arena, Survival's director in its place (game/survival.h)
        if (auto it = routeForce.find(int(mem.at<int8_t>(G::chapter)) * 100 + int(mem.at<int8_t>(G::event_index)));
            it != routeForce.end())
            mem.at<uint8_t>(G::route_choice) = it->second;   // --route (T1): a branch forced, as run_capture -Route
        else if (cuts.defaultRoute)
            mem.at<uint8_t>(G::route_choice) = 0;
        if (cuts.guards)
            evt_watchdog();
        break;
    case 0x33F50: sky_task(arg); return;   // s1 (chapter.cpp)
    case 0x4B300: world_draw_task(arg); return;   // s1 (chapter.cpp)
    // the title and the attract loop (attract.cpp) UNVERIFIED (teaser)
    case 0xD6A0: title_camera_start(); return;
    case 0xD6D0: title_camera_task(arg, false); return;
    case 0xD650: title_camera_task(arg, true); return;
    case 0x23AD0:   // attract_camera_start: an empty camera task, camera_globals_reset
        task_create(0x23AF0, 0x20);
        camera_globals_reset();
        return;
    case 0x23AF0: return;   // attract_camera_nop
    case 0x23C70:   // demo_timer: the demo's length down; below 2 the demo ends (0x53DE94 = 0)
        mem.set32(0x53DE90, mem.u32(0x53DE90) - 1);
        if (int32_t(mem.u32(0x53DE90)) < 2)
            mem.set32(0x53DE94, 0);
        return;
    case 0x67C60:   // scnin_clear_sprites (a task SCNINIT starts): after 4 frames the vblank clears layer 2
        mem.set32(arg + 0x50, mem.u32(arg + 0x50) - 1);
        if (mem.u32(arg + 0x50) == 0) {
            vblank_callback_add(0x67C40);
            task_exit();
        }
        return;
    case 0x67C40: tilemap_clear_rect(0x1004000, 0, 0, 0x3E, 0x30); return;   // scnin_clear_sprites_now
    case 0x2D690: camdr_mode1(); return;
    case 0x2D6F0: camdr_mode2(); return;
    case 0x2D770: camdr_mode3(); return;
    default: break;
    }
    const uint32_t regs[8] = {arg, 0, 0, 0, 0, 0, 0, 0};
    if (dispatch(routine, regs))
        return;
    if (prologue_routine(routine, arg))   // title worker (attract.cpp): after the port's own (a later port wins)
        return;
    unported(routine);
}

void Runtime::unported(uint32_t routine)
{
    if (unportedCount[routine]++ == 0 && log)
        log("routine not ported: " + routineName(routine));
    auto it = names.find(routine);
    char buf[16];
    std::snprintf(buf, sizeof buf, "sub_%X", routine);
    boundaryHit(it == names.end() ? std::string(buf) : it->second, routine, "unported");
}

void Runtime::boundaryHit(const std::string& name, uint32_t addr, const char* kind)
{
    BoundaryHit& h = boundaryHits[name];
    if (h.calls++ == 0) {
        h.kind = kind;
        h.addr = addr;
        h.first = frameCount;
    }
}

// --boundary-report: the hits, highest count first, as a Markdown table (docs/teaser_status/t1_boundaries.md)
void Runtime::writeBoundaryReport(const std::string& path, const std::string& title) const
{
    std::vector<std::pair<std::string, BoundaryHit>> v(boundaryHits.begin(), boundaryHits.end());
    std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second.calls > b.second.calls; });
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f)
        return;
    const double frames = frameCount ? double(frameCount) : 1.0;
    std::fprintf(f, "### %s\n\n%llu frames, %zu entries.\n\n", title.c_str(), (unsigned long long)frameCount, v.size());
    std::fprintf(f, "| # | routine | address | kind | calls | per 1,000 frames | first frame |\n|---|---|---|---|---|---|---|\n");
    int i = 0;
    for (const auto& [name, h] : v) {
        char addr[16] = "-";
        if (h.addr)
            std::snprintf(addr, sizeof addr, "0x%X", h.addr);
        std::fprintf(f, "| %d | %s | %s | %s | %llu | %.1f | %llu |\n", ++i, name.c_str(), addr, h.kind,
                     (unsigned long long)h.calls, double(h.calls) * 1000.0 / frames, (unsigned long long)h.first);
    }
    std::fclose(f);
}

uint64_t Runtime::unportedTotal() const
{
    // T1: system tasks that run every frame without holding up a wait (the world / sky draw, the hit notice, kind
    // 182's grey palette) do not count: with them the guard passed the Chariot's WAIT_ENNUM after 20 s, the fight cut
    // short
    uint64_t n = 0;
    for (const auto& [r, c] : unportedCount)
        if (r != 0x4B300 && r != 0x33F50 && r != 0x6290 && r != 0x9B600)
            n += c;
    return n;
}

// The teaser's guard (docs/teaser_plan.md, cut 1; on in every launcher game): a soft-lock safety net only. An event
// script wait still waiting after 20 s **while routines that are not ported ran during it** (the only things that
// could have been meant to end it): WAIT_ITEM n gets its flag set by the runtime, WAIT_ENNUM / WAIT_ENALV are passed.
// Logged as "teaser guard" boundaries. hangcmp (2026-10-19): WAIT_ITEM used to be forced after 20 s whatever ran; with
// Chapters 1-4 and the ending ported that only cut ported scenes short (the Hangedman's flags 193 / 194 in a slow
// fight: the camera drove off while he was still at the moon). With everything ported the guard never acts; a wait on
// a ported object waits as on the arcade.
void Runtime::evt_watchdog()
{
    const uint32_t pc = mem.u32(G::evt_pc);
    if (pc != wdPc_) {
        wdPc_ = pc;
        wdFrames_ = 0;
        wdUnported_ = unportedTotal();
        return;
    }
    ++wdFrames_;
    if (wdFrames_ < 1200 || unportedTotal() <= wdUnported_)
        return;
    const uint32_t op = mem.u32(G::evt_handlers + mem.u32(pc) * 4);
    char buf[128];
    // flags set by objects the runtime runs itself are never forced: 254 = kind 176's results done (0x51EA3E)
    const bool ownFlag = op == F::evt_WAIT_ITEM && mem.u32(pc + 4) == 254;
    if (op == F::evt_WAIT_ITEM && !ownFlag) {
        const uint32_t n = mem.u32(pc + 4);
        mem.at<uint8_t>(G::event_flags + n) = 1;
        std::snprintf(buf, sizeof buf, "WAIT_ITEM %u at 0x%X: flag set by the runtime (unported routines ran)", n, pc);
    } else if (op == F::evt_WAIT_ENNUM || op == F::evt_WAIT_ENALV) {
        mem.set32(G::evt_pc, pc + 8);
        std::snprintf(buf, sizeof buf, "%s at 0x%X passed by the runtime (unported objects alive)",
                      op == F::evt_WAIT_ENNUM ? "WAIT_ENNUM" : "WAIT_ENALV", pc);
    } else {
        return;
    }
    wdFrames_ = 0;
    wdUnported_ = unportedTotal();
    boundaryHit(std::string("teaser guard: ") + buf, pc, "guard");
    if (log)
        log(buf);
}

void Runtime::call_routine_f(uint32_t routine, uint32_t obj, float v)
{
    uint32_t regs[8] = {obj, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(&regs[1], &v, 4);
    if (dispatch(routine, regs))
        return;
    unported(routine);
}

// the geometrizer's registers (camera_matrices_update, camera_path_eval, object_draw's light records): 0x800090
// starts the zoom (focal length) as the next word; 0x8000A0 (command 0x0A) starts the light vector as the next three
// words (data port 0x804000), kept as the current light that every model drawn after it carries (DrawModel::light)
void Runtime::geo_write(uint32_t port, uint32_t value)
{
    if (port == 0x804000) {
        if (geoPort_ == 0x800090) {
            float f;
            std::memcpy(&f, &value, 4);
            if (f > 1.0f && f < 10000.0f)
                focal = f;
            geoPort_ = 0;
        } else if (geoPort_ == 0x8000A0 && lightWord_ < 3) {
            std::memcpy(&light[lightWord_], &value, 4);
            if (++lightWord_ == 3)
                geoPort_ = 0;
        } else if ((geoPort_ == 0x8000B0 && geoWord_ < 12) || (geoPort_ == 0x800010 && geoWord_ < 4)) {
            // hermit: a model drawn by writing the geometrizer directly (hermit_part_draw_hook's hurt flash: the
            // matrix (command 0x0B, 12 words), then the model (command 0x01: the table entry's words 0, texture header,
            // 2, 3)), drawn as geo_draw_model would with that matrix; flash: a header in the header RAM (0x802400) is
            // drawn flat (header_ram_flat)
            geoWords_[geoWord_++] = value;
            if (geoPort_ == 0x8000B0 && geoWord_ == 12) {
                std::memcpy(geoMat_, geoWords_, sizeof geoMat_);
                geoPort_ = 0;
            } else if (geoPort_ == 0x800010 && geoWord_ == 4) {
                geoPort_ = 0;
                if (geoModels_.empty())
                    for (uint32_t id = 0x1C00; id-- > 0;) {
                        const uint32_t e = 0x2E73530 + 16 * id;
                        geoModels_[(uint64_t(mem.u32(e)) << 32) ^ (uint64_t(mem.u32(e + 8)) << 16) ^ mem.u32(e + 12)] = id;
                    }
                const auto it = geoModels_.find((uint64_t(geoWords_[0]) << 32) ^ (uint64_t(geoWords_[2]) << 16) ^ geoWords_[3]);
                uint32_t id = it != geoModels_.end() ? it->second : 0;
                uint16_t flat = header_ram_flat(geoWords_[1]);
                if (it != geoModels_.end() && pc_flash_model(id, flat)) {
                    DrawModel d{id, {}, {light[0], light[1], light[2]}};
                    d.flat = flat;
                    static const bool trace = std::getenv("HOTD_TRACE_FLAT") != nullptr;
                    if (trace && log && flat != 0xFFFF && frameCount != flatLogged_) {
                        flatLogged_ = frameCount;
                        log("flat draw (geometrizer): frame " + std::to_string(frameCount) + " model " + std::to_string(id));
                    }
                    std::memcpy(d.m, geoMat_, sizeof d.m);
                    d.fade = uint8_t(fade_);
                    d.body = drawBody_;
                    d.part = drawBody_ != 0 ? drawPart_ : 0;
                    d.bossPart = drawBody_ != 0 ? drawBossPart_ : 0;
                    d.task = mem.u32(G::task_current);
                    bool finite = true;
                    for (float v : d.m)
                        finite = finite && std::isfinite(v);
                    if (finite)
                        drawList.push_back(d);
                }
            }
        }
        return;
    }
    geoPort_ = port;
    if (port == 0x8000A0)
        lightWord_ = 0;
    if (port == 0x8000B0 || port == 0x800010)
        geoWord_ = 0;
}

// the port's raw writes to the geometrizer's ports (the light records of object_draw / props) are the same as geo_write;
// other hardware registers (the coprocessor's command ports 0x8800x0 are copro calls in the port) are not modelled
void Runtime::hw_write32(uint32_t addr, uint32_t value)
{
    if (addr == 0x804000 || (addr >= 0x800000 && addr < 0x800100))
        geo_write(addr, value);
    if (addr >= 0x1802000 && addr < 0x1802800 && mem.mapped(addr))   // palette bank 2: the Magician's colour word (stos)
        mem.at<uint16_t>(addr) = uint16_t(value);
    if (addr == 0x1800124) {
        // flash: palette entry 0x92 (the low half; 0x93 = the high half, 0). A fade step writes (31 - level) grey with
        // bit 15; 0 at a fade-in's end. The PC (THOTD.EXE 0x4D1EE4 state 0x11: 0x41B268, the screen to white over
        // 1500 ms, kept until the ending's fade from white) on the fade out's first step (its level still 32)
        backdrop555 = uint16_t(value);
        paletteWhite_ = (value & 0x8000) ? 31 - int(value & 31) : -1;
        const uint32_t id = mem.u32(0x517CA0);
        if (id != fadeIdSeen_) {
            fadeIdSeen_ = id;
            if (pcModels && mem.at<int32_t>(0x517CA4) == 32 && (value & 0x8000))
                pcFadeStart(1.0f, 2.0f, 1500);
            // polish28 (user 10-28: "Curien with PC models looks too white in 4-19"): the Magician's end fades to
            // white (id 1, above) and the farewell (Goodbye Curien, the viewer's 4-19) fades in from white (id 2:
            // level 0 up to 33, a step every 11 frames, f30966-31328 with --default-route) before the ending's
            // chapter 6 fades from white again. The PC's white was held over the whole farewell: every colour
            // doubled (Curien's coat, the light trousers and legs pure white). Now it comes back with the arcade's
            // fade-in (363 frames); the ending's own fade from white (chapter.cpp) is unchanged
            else if (pcModels && mem.at<int32_t>(0x517CA4) == 0 && pcFade.to > 1.5f)
                pcFadeStart(pcFadeLevel(), 1.0f, 6050);
        }
    }
}

int Runtime::paletteWhite() const
{
    if (mem.at<uint16_t>(0x51F704) == 0 || mem.at<uint16_t>(G::palette_grey_on) != 0)
        return -1;
    return paletteWhite_;
}

void Runtime::geo_draw_model(uint32_t model)
{
    DrawModel d{model, {}, {light[0], light[1], light[2]}};
    std::memcpy(d.m, tgp.matrix(), sizeof d.m);
    d.fade = uint8_t(fade_);
    d.body = drawBody_;
    d.part = drawBody_ != 0 ? drawPart_ : 0;
    d.bossPart = drawBody_ != 0 ? drawBossPart_ : 0;
    d.joint = drawJoint_;
    d.task = mem.u32(G::task_current);   // rate: the view's interpolation key (read only)
    d.flat = flatHeader_ != 0 ? header_ram_flat(flatHeader_) : 0xFFFF;
    if (coop4_on() && (model == 0x660 || model == 0x661))   // 4P: players 3 / 4's shot spark (the kSparks ring) in their ramp
        for (uint32_t k = 0; k < 4; ++k)
            if (const uint32_t e = mem.u32(port::coop4::kSparks + k * 8 + 4); mem.u32(port::coop4::kSparks + k * 8) == d.task && d.task != 0 &&
                ((mem.u32(G::frame_counter) - (e >> 8)) & 0xFFFFFF) < 64)   // (a spark lives 60 frames: no reused task)
                d.ramp = int8_t((e & 0xFF) - 2);
    if (coop4_on() && d.task != 0 && mem.u32(d.task) == F::shot_effect_flash)   // 4P: players 3 / 4's shot flash (kFlashes)
        for (uint32_t k = 0; k < 4; ++k)
            if (const uint32_t e = mem.u32(port::coop4::kFlashes + k * 8 + 4); mem.u32(port::coop4::kFlashes + k * 8) == d.task &&
                ((mem.u32(G::frame_counter) - (e >> 8)) & 0xFFFFFF) < 16)   // (a flash lives 10 frames)
                d.ramp = int8_t((e & 0xFF) - 2);
    if (coop4_on() && (model == 0x3F6 || model == 0x3BC)) {   // 4P: players 3 / 4's MISS SHOT mark / extra-life gift
        const uint32_t r = mem.u32(d.task);
        const uint8_t who = mem.at<uint8_t>(d.task + 0x2EB);
        if ((r == F::hostage_shot_effect || r == F::hostage_gift_fly) && (who == 2 || who == 3))
            d.ramp = int8_t(who - 2);
    }
    if (hideMarks && (model == 0x3F5 || model == 0x3F6))   // trailer: Trailer Mode's no MISS SHOT mark
        return;
    if (hideSparks || hideMuzzle || hideBlood) {
        const uint32_t rtn = d.task != 0 ? mem.u32(d.task) : 0;
        if (hideSparks && (rtn == F::shot_flash || rtn == F::part_spark_anim || rtn == F::hit_spark || rtn == F::shot_effect_flash ||
                           rtn == F::shot_effect_debris))
            return;
        if (hideBlood && (rtn == F::blood_burst || rtn == F::blood_drip || rtn == F::hit_splash || (model >= 7 && model <= 0x23) ||
                           (model >= 0x46B && model <= 0x4A3) || (model >= 0x1AF && model <= 0x1BF)))   // (the bursts and drips are drawn by their bodies too: by model)
            return;
        if (hideMuzzle && rtn == F::muzzle_flash)
            return;
    }
    if (!teamFlashDraw(d))
        return;
    // gunfb: Video > Shot Flash Size: the shot's flash at the aim (0x95140, the star) and its spark (0x95630, the orange
    // ball that starts at the same near point: the big fireball of the shot's first frames) scaled about their own
    // places: their matrices' axes only (draw-only)
    if (shotFlashScale != 1.0f && d.task != 0 &&
        (mem.u32(d.task) == F::shot_effect_flash || (mem.u32(d.task) == F::shot_effect_debris && (d.model == 0x660 || d.model == 0x661))))
        for (int k = 0; k < 9; ++k)
            d.m[k] *= shotFlashScale;
    if (!pc_flash_model(d.model, d.flat))
        return;
    // robust (2026-10-16): a model with a non-finite matrix is not drawn (counted in nanDraws). Seen: blood_burst
    // (0x44EF0) reads its body part's camera-space place for 29 frames, also after the body freed its parts (as the
    // original does: the freed memory's contents then); NaN vertices are undefined for the PC renderer.
    bool finite = true;
    for (float v : d.m)
        finite = finite && std::isfinite(v);
    if (!finite) {
        ++nanDraws;
        static const bool traceNan = std::getenv("HOTD_TRACE_NAN_DRAW") != nullptr;   // who draws a NaN matrix
        if (traceNan && log) {
            const uint32_t t = mem.u32(G::task_current);
            char buf[160];
            std::snprintf(buf, sizeof buf, "NaN draw: model 0x%X by task 0x%X %s pos %g %g %g", model, t,
                          routineName(mem.u32(t)).c_str(), mem.f32(t + 0x20), mem.f32(t + 0x24), mem.f32(t + 0x28));
            log(buf);
        }
        return;
    }
    // pcsparks (2026-10-16): the shot debris (model 0x660 + player, a dithered orange ball) in PC mode as THOTD.EXE's
    // shot_effect_debris (0x50BD70) draws it: not for its first 5 ticks (timer > 5), gone after 30, flying 40 units a tick
    // (0x5A7258, set by shot_effect 0x50BF10) where the arcade's (0x95630) draws it from the first frame for 60 at 20.
    // Ours was the big orange ball over every hit, its first frames right in front of the camera (the real PC shows
    // only the flash 0x2D's white puffs and the streaks there: pc_look\pcpolish\full t11205-t13956). The arcade logic
    // runs on; its place n frames on (start + 20 n along the shot ray from the eye) is pushed out along that ray by
    // another 20 n (the camera's turn since the shot left out). UNVERIFIED (teaser): judged by headless shots
    if (pcModels && (model == 0x660 || model == 0x661)) {
        const uint32_t t = mem.u32(G::task_current);
        if (mem.u32(t) == F::shot_effect_debris) {
            const uint32_t n = mem.u32(t + 0x50);
            if (n <= 5 || n > 30)
                return;
            const float len = std::sqrt(d.m[9] * d.m[9] + d.m[10] * d.m[10] + d.m[11] * d.m[11]);
            if (len > 0.0f)
                for (int k = 9; k < 12; ++k) d.m[k] *= 1.0f + 20.0f * float(n) / len;
        }
    }
    d.background = skyDraw_;   // the sky (sky_task)
    if (shadowPass_) {
        // render: a shadow model (part_draw / object_draw_model with body +0x6C bit 1): the matrix before stored matrix 3
        // (shadow_setup's projection) was multiplied in; the view squashes it onto the floor with that projection
        std::memcpy(d.m, shadowBase_, sizeof d.m);
        d.shadow = true;
        d.shadowFloor = shadowFloor_;
        for (uint32_t k = 0; k < 12; ++k)   // fix-world: shadow_setup's projection (its copy 0x51FDF0..) for the view
            d.shadowM[k] = mem.f32(0x51FDF0 + 4 * k);
    }
    drawList.push_back(d);
}

// Options "Teammate Flashes" (user 2026-10-20: "seeing 4 gun flashes makes it hard to see what we are shooting at"):
// each shot (shot_effect 0x95270) draws a flash at the aim point (0x95140: model 0xCA738[player] + frame, 10 frames)
// and a spark flying into the picture (0x95630: model 0x660 + player, 60 frames). Players other than this PC's own:
// Full as the arcade; Reduced: the flash 0.45 x its size for its first 4 frames (of 10), the spark 0.6 x for 20 (of
// 60), and a flash / spark at the same spot as another one of the same age (started in the same frame) drawn once
// (the own one wins); Off: none. Auto: Full with 1-2 players (the arcade's look unchanged), Reduced in the 4P mode.
// Only the frame's draw list changes
bool Runtime::teamFlashDraw(DrawModel& d)
{
    const int mode = teammateFlash == 0 ? 1 : teammateFlash;   // (menufb: Auto = Full; only another PC's players change)
    if (mode == 1 || d.task == 0)
        return true;
    const uint32_t r = mem.u32(d.task);
    uint8_t kind;
    int seat = -1;
    if (r == F::shot_effect_flash) {
        kind = 0;
        const uint32_t base = d.model - mem.u32(d.task + 0x50);
        for (uint32_t p = 0; p < 2 && seat < 0; ++p)
            if (mem.u32(0xCA738 + p * 4) == base)
                seat = int(p);
    } else if (r == F::shot_effect_debris && (d.model == 0x660 || d.model == 0x661)) {
        kind = 1;
        seat = int(d.model - 0x660);
    } else {
        return true;
    }
    if (d.ramp >= 0)
        seat = d.ramp + 2;   // 4P: players 3 / 4's flash / spark (the kFlashes / kSparks rings, above)
    const int age = int(mem.u32(d.task + 0x50));
    if (teamFxFrame_ != frameCount) {
        teamFxFrame_ = frameCount;
        teamFx_.clear();
    }
    const float z = d.m[11];
    const float x = std::abs(z) > 1e-4f ? d.m[9] / z : d.m[9], y = std::abs(z) > 1e-4f ? d.m[10] / z : d.m[10];
    static const bool trace = std::getenv("HOTD_TRACE_TEAM_FLASH") != nullptr;
    auto done = [&](bool drawn, const char* why) {
        if (trace && log) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "team flash: frame %llu %s model 0x%X age %d seat %d at %.3f %.3f: %s",
                          (unsigned long long)frameCount, kind ? "spark" : "flash", d.model, age, seat, x, y, why);
            log(buf);
        }
        return drawn;
    };
    // (menufb: a local game: every player is this PC's, all full; gunfb: but a computer teammate's are a teammate's)
    const bool own = (ownSeat < 0 || seat == ownSeat) && !(seat >= 0 && ((cpuSeats >> seat) & 1) != 0);
    if (!own && mode == 3)
        return done(false, "off");
    if (!own && age > (kind == 0 ? 3 : 20))
        return done(false, "ended");
    // the same spot in the same frame (both just started): drawn once, the own one kept
    const char* why = own ? "own" : "reduced";
    for (size_t i = 0; i < teamFx_.size();) {
        const TeamFx e = teamFx_[i];
        if (e.kind != kind || e.age != uint8_t(age) || std::abs(e.x - x) > 0.03f || std::abs(e.y - y) > 0.03f) {
            ++i;
            continue;
        }
        if (!own)
            return done(false, "merged");
        if (e.seat == ownSeat) {
            ++i;
            continue;
        }
        for (size_t k = drawList.size(); k-- > 0;)   // the own one replaces a teammate's drawn before it
            if (drawList[k].task == e.task && drawList[k].model == e.model) {
                drawList.erase(drawList.begin() + std::ptrdiff_t(k));
                break;
            }
        teamFx_.erase(teamFx_.begin() + std::ptrdiff_t(i));
        why = "own, a teammate's merged";
    }
    if (!own)
        for (int k = 0; k < 9; ++k)
            d.m[k] *= kind == 0 ? 0.45f : 0.6f;
    teamFx_.push_back({kind, uint8_t(age), int8_t(seat), x, y, d.task, d.model});
    return done(true, why);
}

// flash: a model drawn with the table's other words but another texture header (word 1), as the bosses' part hooks
// write the geometrizer themselves (0x9DACC, 0x9E0F8, hermit_part_draw_hook): the current matrix (command 0x0B), then
// the model (command 0x01)
void Runtime::geo_draw_header(uint32_t model, uint32_t header)
{
    struct Keep { uint32_t& v; uint32_t s; ~Keep() { v = s; } } keep{flatHeader_, flatHeader_};
    flatHeader_ = header;
    static const bool trace = std::getenv("HOTD_TRACE_FLAT") != nullptr;   // the frames with a flash draw
    if (trace && log && frameCount != flatLogged_) {
        flatLogged_ = frameCount;
        char buf[96];
        std::snprintf(buf, sizeof buf, "flat draw: frame %llu model 0x%X header 0x%X", (unsigned long long)frameCount, model, header);
        log(buf);
    }
    geo_draw_model(model);
}

// flash: the rasteriser's texture header RAM (MAME model2_v.cpp: a header address with bit 23 reads the RAM at
// address & 0xFFFF, written by command 0x04). The boot (0xAE0..0xBD0) fills eight blocks with 0x21B0 (address, 0x200
// words, colour n): the words 0, n << 6 repeated, i.e. every header {0, n << 6, 0, n << 6}: untextured (bit 14 clear),
// opaque, not checkered, colour base n; written twice, the second time 0x100 words on, so 0x300 words a block. A model
// drawn with such a header is flat in polygon palette entry n (lit by its polygons' own light parameters, MAME
// draw_scanline_solid), whatever its polygons' header steps. HOTD's models stay inside a block (at most 0x1E8 header
// words a model; the 6 bigger ones are world models never drawn so). Other header RAM: cleared at the boot (MAME:
// zero), entry 0. 0xFFFF: not a header RAM address
uint16_t Runtime::header_ram_flat(uint32_t header)
{
    if ((header & 0x800000) == 0)
        return 0xFFFF;
    static const struct { uint32_t at; uint16_t colour; } kSets[] = {
        {0x0800, 0}, {0x0C00, 1}, {0x1000, 2}, {0x1400, 3}, {0x0400, 0x3BF}, {0xEC00, 0x3F9}, {0x2000, 6}, {0x2400, 7}};
    const uint32_t a = header & 0xFFFF;
    for (const auto& s : kSets)
        if (a >= s.at && a < s.at + 0x300)
            return s.colour;
    return 0;
}

// flash with the PC models: THOTD.EXE's part hooks draw no flash colour. The Hangedman (0x51D84E: the part not drawn
// while +0x2FC is odd) and the Hermit's face (0x4EF2B8: neither model on odd frames) leave the part out; the Magician
// (0x51DF7E) draws a white copy the PC data has for 15 of his parts (0x1D3C..0x1D4A), else model 0 (nothing). The
// other flat sets (0x8F0400: chapter 4's vein glow) are drawn as the model itself. False: not drawn
bool Runtime::pc_flash_model(uint32_t& model, uint16_t& flat) const
{
    if (!pcModels || flat == 0xFFFF)
        return true;
    if (flat != 7) {
        flat = 0xFFFF;
        return true;
    }
    static const uint16_t kWhite[][2] = {{0x1C66, 0x1D3C}, {0x1C43, 0x1D3D}, {0x1C6C, 0x1D3E}, {0x1C69, 0x1D3F}, {0x1C21, 0x1D40},
                                         {0x1C27, 0x1D41}, {0x1C24, 0x1D42}, {0x1BBF, 0x1D43}, {0x1C60, 0x1D44}, {0x1C63, 0x1D45},
                                         {0x1C6F, 0x1D46}, {0x1BC2, 0x1D47}, {0x1C01, 0x1D48}, {0x1C04, 0x1D49}, {0x1C72, 0x1D4A}};
    for (const auto& w : kWhite)
        if (model == w[0]) {
            model = w[1];
            flat = 0xFFFF;
            return true;
        }
    return false;
}

// 0x14200 geo_matrix_position(model, out): the model is drawn with the current matrix, read back from the
// geometrizer; its translation (camera space) goes to out (x, y, z) when out is not 0
void Runtime::geo_matrix_position(uint32_t model, uint32_t out)
{
    geo_draw_model(model);
    if (out != 0)   // from the geometrizer's matrix (a non-finite one is not in the draw list)
        for (uint32_t k = 0; k < 3; ++k)
            mem.set32(out + 4 * k, port::fbits(tgp.matrix()[9 + k]));
}

// one frame: mode 4 (the game: mode4_game, game_frame: the port's) or 5 (game over)
void Runtime::frame()
{
    frame_one();
    // Boss Mode's walk-up (THOTD.EXE pc_main_loop 0x419168: `while (pc_boss_next) frame_tick();` before the frame it
    // draws): the frames up to the boss's entrance (0x412BC4 clears it) run at once, undrawn and silent
    for (int n = 0; boss_next_on() && mem.at<int16_t>(G::game_mode) == 4 && !chapterDone && n < 40000; ++n)
        frame_one();
    // Boss Mode's flow (pc_main_loop 0x419168 after each frame: the clock, the win, All Bosses' next boss, the end):
    // part of the game step, so rollback's re-runs and a joining PC's replay do it alike (bossnet)
    if (boss.active())
        boss.tick(*this, boss.tickMs);
    // Survival Mode (game/survival.h): the lead-in up to the arena at once, undrawn and silent (as Boss Mode's walk-up), then
    // the end of the game checked
    if (survival.active()) {
        for (int n = 0; survival.leadIn() && mem.at<int16_t>(G::game_mode) == 4 && !chapterDone && n < 40000; ++n)
            frame_one();
        survival.tick(*this);
    }
    if (weapons.on()) {   // the weapons (game/weapons.h): Survival's once the arena is held, the other modes' in play
        weapons.live = weapons.mode == Weapons::Survival ? survival.frozen && survival.phase != SurvivalMode::Over
                                                         : mem.at<int16_t>(G::game_mode) == 4;
        weapons.afterFrame(*this);
    }
}

void Runtime::frame_one()
{
    // the sound queue's drain, once a frame after the frame's logic (flow.cpp sound_queue_drain)
    struct Drain { Runtime& r; ~Drain() { r.sound_queue_drain(); } } drain{*this};
    sprites_frame();   // the 2D layer's counters (src/game/sprites.cpp)
    vblank_run();      // fix-logic: the vblank's once-list (palette_restore_vblank ...)
    palette_anim_copy();   // palanim: the vblank's polygon palette copy 0x24E90 (last frame's palette_animation)
    // PC Mode's millisecond clock (the PC's timeGetTime for the reload timer): 1000 / 60 ms a frame, as the PC
    // harness's tick clock (docs/pcmode.md)
    pm_clock_ms = uint32_t((frameCount + 1) * 1000 / 60);
    drawList.clear();
    skinDraws.clear();
    if (diffReplay)   // the differential: the arcade's writes between its last frame's end and this frame's logic
        diffReplay->apply(mem);
    // frame_tick 0xD900's counters before the mode routine, in every mode: frame_counter and 0x51F794 (the frames
    // since chapter_counters_reset: letterbox_task's attract test reads it). ch4proof (the whole-game differential):
    // 0x51F794 was never counted, and frame_counter not in the game over (mode 5)
    mem.set32(G::frame_counter, mem.u32(G::frame_counter) + 1);
    mem.set32(0x51F794, mem.u32(0x51F794) + 1);
    if (mem.at<int16_t>(G::game_mode) == 5) {   // game over (gameover: the scene's draws and camera, flow.cpp)
        game_mode_frame();
        if (diffReplay)
            diffReplay->record(mem);
        const uint32_t slot = 0x51EA50 + mem.u32(0x51F620) * 0x154;
        for (uint32_t k = 0; k < 12; ++k)
            camera[k] = mem.f32(slot + 4 * k);
        ++frameCount;
        return;
    }
    // scoredisp: Gameplay "Score Display": the title code LLLRR's 7 in play (hud_lives_draw prints SCORE:%05d)
    if (scoreDisplay && mem.at<int16_t>(G::game_mode) == 4 && mem.u32(0x51F42C) == 0 && !coop4_on())
        mem.set32(0x51F42C, 7);
    game_mode_frame();
    lg_modes_frame();   // Light Gun Edition: the fun modes (local games)
    if (mem.at<int16_t>(G::game_mode) == 4)
        score_line_hide();   // relplay: the score line only in play (draws only: its own cells)
    actor_pm_sync();   // pmextra: PC Mode's extra characters drawn as actors (before players 3 / 4's copies)
    car_seats_dress();   // carchars: the opening car's seats as their own characters (draws only)
    const size_t ownDraws = drawList.size(), ownSkins = skinDraws.size();
    coop4_scene_bodies();   // 4P phase 6: players 3 / 4's cut-scene bodies (draws only)
    // fix-world: the play-through log shows each rescue (the results panel's RESCUED x)
    if (const int16_t r = mem.at<int16_t>(G::hostages_rescued_chapter); r != rescuedLogged_) {
        if (r > rescuedLogged_ && log)
            log("hostage rescued: " + std::to_string(r) + " this chapter (frame " + std::to_string(frameCount) + ")");
        rescuedLogged_ = r;
    }
    // the view every object was drawn with: the current camera slot's stored matrix 0 (camera_matrices_update)
    const uint32_t slot = 0x51EA50 + mem.u32(0x51F620) * 0x154;
    for (uint32_t k = 0; k < 12; ++k)
        camera[k] = mem.f32(slot + 4 * k);
    ragdoll_frame();   // the Ragdoll mod (game/ragdoll.h): the dead bodies' drawn poses (draws only; after the camera: it needs it)
    if (lg_exploding || lgLook == 1 || lgWeapon[0] >= 0 || weapons.mode == Weapons::Arsenal)
        lg_bodies_frame();   // (Arsenal: the rocket's targets)   // Light Gun Edition: Exploding Zombies / Night Vision (the bodies, their colours)
    if (lg_roulette)
        lg_roulette_frame();   // Light Gun Edition: Weak-Point Roulette (the glowing parts for the next shots)
    if (lgModes.bigHeads)
        lg_big_heads();   // Light Gun Edition: Big Heads (draws only)
    // actorswap: the cut-scene viewer's Actors (draws only; off outside the viewer)
    if ((actorPreValid_ = actorView)) {
        actorPreDraw_ = drawList;
        actorPreSkin_ = skinDraws;
        actorOwn_[0] = ownDraws, actorOwn_[1] = ownSkins;
    }
    ending_head_fallback();   // charsel: the PC models: the ending's talking heads (draws only)
    actor_swap(ownDraws, ownSkins);
    part_owner_check();   // charsel: HOTD_PART_CHECK (testing; draws only)
    bigHeadFrame(*this);   // cosmods: the Big Head mod (game/cosmods.h): the drawn heads scaled (draws only; off: nothing)
    creatureBloodFrame(*this);   // cosmods: Rainbow Blood's creatures (their hues; draws only; off: nothing kept)
    // developer (clips): HOTD_DEV_ROWS_DUMP=frame prints the colour rows then (call row:entry, b = a blood row), and
    // HOTD_DEV_ROWS="frame:row:entry[b],row:entry[b],..." sets them then (a creature in another chapter's colours)
    if (static const char* dump = std::getenv("HOTD_DEV_ROWS_DUMP"); dump && frameCount == std::strtoull(dump, nullptr, 10) && log) {
        std::string s = "dev rows:";
        for (const auto& [key, entry] : colorRows)
            s += " " + std::to_string((key - 1) / 2) + ":" + std::to_string(entry) + (bloodRows.count(key) ? "b" : "");
        log(s);
    }
    if (static const char* rows = std::getenv("HOTD_DEV_ROWS"); rows && frameCount == std::strtoull(rows, nullptr, 10))
        for (const char* s = std::strchr(rows, ':'); s && *s; s = std::strchr(s, ',')) {
            int row = 0, entry = 0, n = 0;
            if (std::sscanf(s + 1, "%d:%d%n", &row, &entry, &n) < 2)
                break;
            color_row_set(row, entry);
            if (s[1 + n] == 'b')
                ggset_blood_row(row);
            s += 1 + n;
        }
    // developer (pictures and clips; changes the game): HOTD_DEV_SPAWN="frame:type:x:y:z:yaw:life[;...]" makes a
    // creature there at that frame (survival_spawn, as ENTB3; yaw in the game's angle units), waiting ~40 s before it acts
    if (static const char* dev = std::getenv("HOTD_DEV_SPAWN"); dev)
        for (const char* s = dev; s && *s; s = std::strchr(s, ';') ? std::strchr(s, ';') + 1 : nullptr) {
            unsigned long long f = 0;
            int type = 0, yaw = 0, life = 100;
            float pos[3] = {};
            if (std::sscanf(s, "%llu:%d:%f:%f:%f:%d:%d", &f, &type, &pos[0], &pos[1], &pos[2], &yaw, &life) >= 5 && f == frameCount) {
                const int32_t ang[3] = {0, yaw, 0};
                survival_spawn(0, 0, pos, ang, type, 0, life, 250, 0);
                if (log)
                    log("dev spawn: type " + std::to_string(type) + " at frame " + std::to_string(frameCount));
            }
        }
    // developer: HOTD_DEV_BLEED="type:every" bursts blood (blood_burst_spawn, the game's own) from a drawn body part of
    // each creature of that type every n frames (clips of the blood where no shot can land, e.g. a cut-scene)
    if (static const char* bleed = std::getenv("HOTD_DEV_BLEED"); bleed) {
        int type = -1, every = 20;
        std::sscanf(bleed, "%d:%d", &type, &every);
        if (every > 0 && frameCount % uint64_t(every) == 0) {
            std::set<uint32_t> done;
            uint32_t pick = 0;
            for (const DrawModel& d : drawList)
                if (d.part && d.body && !d.shadow && !done.count(d.body) && mem.mapped(d.body + 0xCC) &&
                    mem.at<int16_t>(d.body + 0xCC) == type && (pick++ % 3) == uint32_t(frameCount / uint64_t(every)) % 3) {
                    done.insert(d.body);
                    blood_burst_spawn(d.part);
                }
        }
    }
    if (diffReplay)   // the differential: the state at the frame's end (frame_tick's write of 0xF0F to 0x8000F0)
        diffReplay->record(mem);
    // debugging (attract2): HOTD_TRACE_FLOW=<file>: one line per change of mode / sub-mode / scene / event pc
    // (frame, mode, sub-mode, scene_number, event_index, evt_pc), for comparison with tools\mame_flow_trace.lua
    static FILE* flowLog = [] { const char* p = std::getenv("HOTD_TRACE_FLOW"); return p ? std::fopen(p, "w") : nullptr; }();
    if (flowLog) {
        static std::string last;
        char line[160];
        std::snprintf(line, sizeof line, "%d %u %d %d %X", int(mem.at<int16_t>(G::game_mode)), mem.u32(G::game_submode),
                      int(mem.at<int16_t>(G::scene_number)), int(mem.at<int8_t>(G::event_index)), mem.u32(G::evt_pc));
        if (last != line) {
            std::fprintf(flowLog, "%llu %s\n", static_cast<unsigned long long>(frameCount), line);
            std::fflush(flowLog);
            last = line;
        }
    }
    ++frameCount;
}

} // namespace hotd::game

namespace hotd::game {

// Light Gun Edition: the fun modes' game part, after each frame's logic (in play only)
void Runtime::lg_modes_frame()
{
    if (mem.at<int16_t>(G::game_mode) != 4)
        return;
    lg_survival_frame();   // Coin-Op Survival's clock (off: nothing)
    if (lg_crisis_fail || lgTimeUp) {   // Hostage Crisis: a hostage was shot / Coin-Op Survival: the time is up: every player out, no continues
        lgModes.hardcore = true;
        bossNoContinue = true;
        for (int p = 0; p < seat_count(); ++p) {
            port::a::Player& pl = mem.at<port::a::Player>(player_rec(p));
            lgOut_[p & 3] = true;
            if (pl.state == 5) pl.lives = 0;
        }
    }
    for (int p = 0; p < seat_count(); ++p) {
        port::a::Player& pl = mem.at<port::a::Player>(player_rec(p));
        const int w = lgWeapon[p & 3];
        if (w >= 0) {   // Arsenal Mode: the weapon's blast, splash and magazine (a reload refills to it)
            // (user 2026-11-05: the shotgun did not feel much different: its spread now hits everything in it, as the
            // rocket's blast does, a little wider)
            lg_seat_scale[p & 3] = w == 1 ? 3.0f : w == 3 ? 5.0f : 1.0f;
            lg_dmg_scale[p & 3] = w == 1 ? 2.0f : 1.0f;   // (user 2026-11-05: the shotgun a bit more powerful)
            lg_splash[p & 3] = w == 1 || w == 3;
            lg_hide_bullets[p & 3] = w > 0;
            const int mag = lgMagazine(w);
            if (w == 3 && !lgModes.freeFire) {
                // the rocket launcher: a reload takes a rocket from the reserve (none left: it stays empty); a fired
                // rocket is gone (switching weapons never reloads it)
                const int q = p & 3;
                if (pl.rounds == pm_full_rounds(p) && lgLastRounds_[q] < pl.rounds) {
                    if (!lgRocketLoaded[q] && lgRockets[q] > 0)
                        --lgRockets[q], lgRocketLoaded[q] = true;
                    pl.rounds = lgRocketLoaded[q] ? 1 : 0;
                } else if (pl.rounds > 1) {
                    pl.rounds = lgRocketLoaded[q] ? 1 : 0;
                }
                if (pl.rounds == 0) {
                    lgRocketLoaded[q] = false;
                    pl.empty = 1;
                }
                lgLastRounds_[q] = pl.rounds;
            } else {
            if (pl.rounds == pm_full_rounds(p) && lgLastRounds_[p & 3] < pl.rounds && mag > pl.rounds)
                pl.rounds = uint8_t(mag);   // (a reload: the game's 6, the machine gun's 30)
            if (lgModes.freeFire && pl.state == 5)
                pl.rounds = uint8_t(mag), pl.empty = 0;
            if (pl.rounds > mag)
                pl.rounds = uint8_t(mag);
            lgLastRounds_[p & 3] = pl.rounds;
            }
        } else {
            const bool engine = weapons.mode == Weapons::Arsenal;   // (the creator's weapons: Arsenal / Shotgun / Machine Gun)
            if (engine && pl.state == 5 && !lgArmed_[p & 3]) {   // (a seat's first frame in play: its weapons)
                weapons.arm(*this, p);
                lgArmed_[p & 3] = true;
            }
            if (lgModes.freeFire && pl.state == 5) {   // Free Fire: the gun never runs dry
                if (engine) {
                    Weapons::Seat& s = weapons.seat[p & 3];
                    pl.rounds = uint8_t(weapons.magazine(p, s.cur));
                    s.reserve[s.cur] = weapons.reserveFull(p, s.cur);
                    s.reload = 0;
                } else {
                    pl.rounds = pm_full_rounds(p);
                }
                pl.empty = 0;
            }
            if (lgModes.shotgunShells > 0 && pl.rounds > lgModes.shotgunShells)   // Shotgun Mode: a load is 3 shells
                pl.rounds = uint8_t(lgModes.shotgunShells);
            // Shotgun Mode as Arsenal's shotgun: everything in the spread is hit; the red shells and their count drawn by
            // the HUD (the game's own six bullets erased: a reload drew six and three were left on the screen)
            lg_splash[p & 3] = lgModes.shotgunShells > 0;
            lg_hide_bullets[p & 3] = lgModes.shotgunShells > 0 || lgMachineGun;   // (Machine Gun mode: silver rounds)
            lg_seat_scale[p & 3] = 1.0f;
            lg_dmg_scale[p & 3] = lgModes.shotgunShells > 0 ? 2.0f : 1.0f;   // (Shotgun Mode: double damage)
        }
        if (lgModes.vampire) {
            // Vampire Mode: the meter drains in about 15 s in play; a hit gives 8 %, a kill 30 %; empty: a life lost (as an
            // enemy's hit: invulnerable a moment), then full again
            const uint32_t h = lg_hits[p & 3], k = lg_kills[p & 3];
            float& b = lgBlood[p & 3];
            if (pl.state == 5 && mem.at<uint8_t>(G::scene_state) == 2) {
                if (h > lgBloodHits_[p & 3]) b += 0.08f * float(h - lgBloodHits_[p & 3]);
                if (k > lgBloodKills_[p & 3]) b += 0.30f * float(k - lgBloodKills_[p & 3]);
                b = std::min(1.0f, b) - 1.0f / (15.0f * 57.5f);
                if (b <= 0.0f) {
                    if (player_hit_check(p) == 0 && mem.at<int32_t>(invuln_at(p)) == 0) {
                        player_hit_take(p, 1, 0);
                        mem.at<int32_t>(invuln_at(p)) = 0x5A;
                        player_hit_keep_life(p);
                    }
                    b = 1.0f;
                }
            } else if (pl.state != 5) {
                b = 1.0f;   // (a new life / a join: full)
            }
            lgBloodHits_[p & 3] = h, lgBloodKills_[p & 3] = k;
        }
        if (lgModes.hardcore && pl.state == 4) {   // Hardcore: out of lives = out of the game (no countdown to wait)
            lgOut_[p & 3] = true;
            pl.continue_timer = 0;
        }
    }
}

// Light Gun Edition: Big Heads: every body part of the head class (part roles: class 1) drawn 1.8 times larger about
// its joint (the neck), it and its shadow; the hit spheres are the game's own (draws only)
void Runtime::lg_big_heads()
{
    constexpr uint32_t kRoles = 0x1184E0;   // (creature.cpp kPartRoles: 0xF8 a type, 8 a role)
    constexpr float kScale = 1.8f;
    for (DrawModel& d : drawList) {
        if (d.part == 0 || d.body == 0 || !mem.mapped(d.part) || !mem.mapped(d.body))
            continue;
        const int32_t type = mem.at<int16_t>(d.body + 0xCC);
        const uint32_t joint = mem.u32(d.part + 0x38);
        if (type < 0 || type > 0x7F || joint >= 31)
            continue;
        if (mem.at<int8_t>(kRoles + uint32_t(type) * 0xF8 + joint * 8 + 3) != 1)
            continue;
        for (int k = 0; k < 9; ++k)
            d.m[k] *= kScale;
    }
}

} // namespace hotd::game

namespace hotd::game {

// Light Gun Edition: Arsenal Mode: the seat's next weapon, its magazine full (a switch is a reload)
void Runtime::lg_weapon_next(int p)
{
    if (p < 0 || p >= 4 || lgWeapon[p] < 0 || p >= seat_count())
        return;
    lgWeapon[p] = (lgWeapon[p] + 1) % 4;
    port::a::Player& pl = mem.at<port::a::Player>(player_rec(p));
    if (pl.state == 5) {
        pl.rounds = uint8_t(lgMagazine(lgWeapon[p]));
        pl.empty = 0;
        if (lgWeapon[p] == 3 && !lgModes.freeFire) {   // (the rocket: only one already loaded, never a free one)
            pl.rounds = lgRocketLoaded[p] ? 1 : 0;
            pl.empty = pl.rounds == 0;
        }
    }
    lgLastRounds_[p] = pl.rounds;
    if (log)
        log(std::string("player ") + std::to_string(p + 1) + ": " + lgWeaponName(lgWeapon[p]));
}

} // namespace hotd::game

namespace hotd::game {

// Light Gun Edition: Weak-Point Roulette: the zombies (the humanoid creature types that lose parts: zom, debu,
// zonbiman, mummy, haris, gilmoa, mummy2, dkiller, mon) get one glowing body part each, another one every 2 s (by the
// body and the moment, so it never flickers within the 2 s); other creatures and the bosses as normal
static bool lgZombieType(int type)
{
    static const int kTypes[] = {0, 8, 11, 12, 16, 24, 31, 32, 40, 57, 70, 79, 80, 81};
    return std::find(std::begin(kTypes), std::end(kTypes), type) != std::end(kTypes);
}

void Runtime::lg_roulette_frame()
{
    lgWeak_.clear();
    lgRings.clear();
    if (mem.at<int16_t>(G::game_mode) != 4)
        return;
    std::map<uint32_t, std::vector<uint32_t>> parts;   // body -> its drawn parts
    for (const DrawModel& d : drawList) {
        if (d.part == 0 || d.body == 0 || d.shadow || !mem.mapped(d.body) || !mem.mapped(d.part))
            continue;
        if (mem.at<port::a::BodyPart>(d.part).hit_radius <= 0.0f)
            continue;   // (a part no shot can hit)
        const int type = mem.at<int16_t>(d.body + 0xCC);
        if (!lgZombieType(type))
            continue;
        std::vector<uint32_t>& v = parts[d.body];
        if (std::find(v.begin(), v.end(), d.part) == v.end())
            v.push_back(d.part);
    }
    // each zombie keeps the weak point it got when it first showed (user 2026-11-05: "a fixed weak point that doesn't
    // move"), one of its bigger parts (the larger half by hit radius: not a hand); a new one only when that part is
    // gone (shot off) or the body slot is a new creature (another type, or away for 2 s)
    for (auto it = lgWeakFixed_.begin(); it != lgWeakFixed_.end();)
        it = it->second.seen + 115 < frameCount ? lgWeakFixed_.erase(it) : std::next(it);
    for (auto& [body, v] : parts) {
        if (v.size() < 2)
            continue;
        const int type = mem.at<int16_t>(body + 0xCC);
        auto f = lgWeakFixed_.find(body);
        if (f == lgWeakFixed_.end() || f->second.type != type || std::find(v.begin(), v.end(), f->second.part) == v.end()) {
            std::vector<uint32_t> big = v;
            std::sort(big.begin(), big.end(), [&](uint32_t a, uint32_t b) {
                return mem.at<port::a::BodyPart>(a).hit_radius > mem.at<port::a::BodyPart>(b).hit_radius;
            });
            big.resize(std::max<size_t>(1, (big.size() + 1) / 2));
            lgWeakSeed_ = lgWeakSeed_ * 1664525u + 1013904223u;
            lgWeakFixed_[body] = {big[(lgWeakSeed_ >> 16) % big.size()], type, frameCount};
            f = lgWeakFixed_.find(body);
        }
        f->second.seen = frameCount;
        const uint32_t part = f->second.part;
        lgWeak_[body] = part;
        const port::a::BodyPart& bp = mem.at<port::a::BodyPart>(part);
        if (bp.view_pos[2] > 1.0f)
            lgRings.push_back({bp.view_pos[0], bp.view_pos[1], bp.view_pos[2], bp.hit_radius});
    }
}

bool Runtime::lg_part_ok(uint32_t part)
{
    if (!mem.mapped(part))
        return true;
    const uint32_t body = mem.at<port::a::BodyPart>(part).owner.a;
    const auto it = lgWeak_.find(body);
    return it == lgWeak_.end() || it->second == part;
}

} // namespace hotd::game

namespace hotd::game {

// Light Gun Edition: the bodies drawn this frame: their biggest hit part (the blast's reach), Night Vision's heat
// colours (every creature), the Exploding Zombies' glow (about one zombie in four, decided when it first shows)
void Runtime::lg_bodies_frame()
{
    lgBodies_.clear();
    std::map<uint32_t, size_t> at;
    for (DrawModel& d : drawList) {
        if (d.part == 0 || d.body == 0 || d.shadow || !mem.mapped(d.body) || !mem.mapped(d.part))
            continue;
        if (lgLook == 1 && d.ramp < 0)
            d.ramp = 2;   // Night Vision: heat
        const port::a::BodyPart& bp = mem.at<port::a::BodyPart>(d.part);
        if (bp.hit_radius <= 0.0f || bp.view_pos[2] <= 1.0f)
            continue;
        const auto it = at.find(d.body);
        if (it == at.end()) {
            at[d.body] = lgBodies_.size();
            lgBodies_.push_back({d.body, d.part, bp.view_pos[0], bp.view_pos[1], bp.view_pos[2], bp.hit_radius,
                                 int(mem.at<int16_t>(d.body + 0xCC))});
        } else if (bp.hit_radius > lgBodies_[it->second].r) {
            LgBody& b = lgBodies_[it->second];
            b.part = d.part, b.x = bp.view_pos[0], b.y = bp.view_pos[1], b.z = bp.view_pos[2], b.r = bp.hit_radius;
        }
    }
    lgBlasts.erase(std::remove_if(lgBlasts.begin(), lgBlasts.end(), [&](const LgBlast& b) { return b.frame + 60 < frameCount; }),
                   lgBlasts.end());   // (the blasts' puffs: the last one ends 51 frames in)
    if (!lg_exploding)
        return;
    std::map<uint32_t, bool> vol;
    for (const LgBody& b : lgBodies_) {
        if (!lgZombieType(b.type))
            continue;
        const auto it = lgVol_.find(b.body);
        if (it != lgVol_.end()) {
            vol[b.body] = it->second;
        } else {
            lgVolSeed_ = lgVolSeed_ * 1664525u + 1013904223u;
            vol[b.body] = (lgVolSeed_ >> 16) % 100 < 35;   // (about one zombie in three)
        }
    }
    lgVol_.swap(vol);
    for (auto it = lgBoomed_.begin(); it != lgBoomed_.end();)   // (gone from the screen: its slot can be a new zombie)
        it = at.count(*it) ? std::next(it) : lgBoomed_.erase(it);
    for (DrawModel& d : drawList)
        if (d.body != 0 && !d.shadow && lg_volatile(d.body))
            d.ramp = 3;   // the glow
}

bool Runtime::lg_volatile(uint32_t body) const
{
    const auto it = lgVol_.find(body);
    return lg_exploding && it != lgVol_.end() && it->second && !lgBoomed_.count(body);
}

// a body taken out by a blast: hit by the shooter on its biggest part (as a shot there); a zombie with one hit point
// left, so the hit kills (the bosses' fights: hits only)
void Runtime::lg_blast_hit(int player, const LgBody& b)
{
    const uint32_t bit = 1u << ((player & 1) + 1);
    port::a::BodyPart& bp = mem.at<port::a::BodyPart>(b.part);
    bp.flags |= bit | 0x100u;
    mem.at<port::a::Obj>(b.body).flags |= bit | 0x100u;
    if (coop4_on()) {
        coop4_hit_add(b.part, player);
        coop4_hit_add(b.body, player);
    }
    if (lgZombieType(b.type) && mem.at<uint8_t>(0x543530) == 0 && mem.at<int16_t>(b.body + 0x70) > 1)
        mem.at<int16_t>(b.body + 0x70) = 1;
}

// every zombie on the screen blown up by the shooter (a blast drawn on each); bodies blown up here glow no more
void Runtime::lg_blast_all(int player)
{
    for (const LgBody& b : lgBodies_) {
        if (!lgZombieType(b.type))
            continue;   // (zombies only: never the hostages / scientists)
        lgBoomed_.insert(b.body);
        lgBlasts.push_back({b.x, b.y, b.z, frameCount});
        lg_blast_hit(player, b);
    }
}

// the shot (shot_resolve): a glowing zombie hit (user 2026-11-05): on its head, it blows up with every zombie on the
// screen (the reward); anywhere else, only it blows up, and the blast hurts the shooter (a life, as an enemy's hit)
void Runtime::lg_explode(int player, uint32_t part)
{
    if (!mem.mapped(part))
        return;
    const port::a::BodyPart& hp = mem.at<port::a::BodyPart>(part);
    const uint32_t body = hp.owner.a;
    if (!lg_volatile(body) || !mem.mapped(body))
        return;
    const int type = mem.at<int16_t>(body + 0xCC);
    const bool head = type >= 0 && type <= 0x7F && hp.role >= 0 && hp.role < 31 &&
                      mem.at<int8_t>(0x1184E0u + uint32_t(type) * 0xF8 + uint32_t(hp.role) * 8 + 3) == 1;
    if (head) {
        lg_blast_all(player);
        return;
    }
    lgBoomed_.insert(body);
    LgBody me{body, part, hp.view_pos[0], hp.view_pos[1], hp.view_pos[2], hp.hit_radius, type};
    for (const LgBody& b : lgBodies_)
        if (b.body == body) me = b;
    lgBlasts.push_back({me.x, me.y, me.z, frameCount});
    lg_blast_hit(player, me);
    player_hit_lose_life(player, 1, 0, 1, 0x5A);
}

// Arsenal Mode's rocket launcher (user 2026-11-05): every shot fired kills every zombie on the screen
void Runtime::lg_fired(int player)
{
    const bool rocket = lgWeapon[player & 3] == 3 ||
                        (weapons.mode == Weapons::Arsenal && weapons.seat[player & 3].cur == kRocket);
    if (rocket && mem.at<int16_t>(G::game_mode) == 4)
        lg_blast_all(player);
}

// Coin-Op Survival: the clock runs down in play (not in cut-scenes, the results, the continue countdowns); a kill +2 s,
// a rescue +15 s, a lost life -10 s, a miss -1.5 s; at 0 the game is over for everyone
void Runtime::lg_survival_frame()
{
    if (lgClock < 0 || lgTimeUp)
        return;
    if (!lgSvInit_) {
        lgSvInit_ = true;
        lgSvKills_ = lg_kills[0] + lg_kills[1] + lg_kills[2] + lg_kills[3];
        lgSvRescued_ = mem.at<int16_t>(G::hostages_rescued_chapter);
        for (int p = 0; p < 4; ++p) {
            lgSvMiss_[p] = lg_shots[p] > lg_hits[p] ? lg_shots[p] - lg_hits[p] : 0;
            lgSvLives_[p] = mem.at<port::a::Player>(player_rec(p)).lives;
        }
        return;
    }
    double d = 0;
    uint32_t kills = 0;
    for (int p = 0; p < 4; ++p) kills += lg_kills[p];
    if (kills > lgSvKills_) d += 10.0 * double(kills - lgSvKills_);   // (user 2026-11-05: 10 s a kill)
    lgSvKills_ = kills;
    const int resc = mem.at<int16_t>(G::hostages_rescued_chapter);
    if (resc > lgSvRescued_) d += 30.0 * double(resc - lgSvRescued_);   // (30 s a rescue)
    lgSvRescued_ = resc;
    bool anyone = false;
    for (int p = 0; p < seat_count(); ++p) {
        const port::a::Player& pl = mem.at<port::a::Player>(player_rec(p));
        const uint32_t miss = lg_shots[p] > lg_hits[p] ? lg_shots[p] - lg_hits[p] : 0;
        lgSvMiss_[p] = miss;
        // (user 2026-11-05: no time taken off: only the countdown)
        lgSvLives_[p] = pl.lives;
        anyone = anyone || pl.state == 5;
    }
    if (d != 0) {
        lgClock += d;
        lgClockDelta = float(d);
        lgClockDeltaFrame = frameCount;
    }
    if (anyone && mem.at<uint8_t>(G::scene_state) == 2 && !clear.active && letterbox == 0 && mem.u32(G::shot_enable) != 0)
        lgClock -= 1.0 / 57.524;
    lgClock = std::min(lgClock, 599.0);
    if (lgClock <= 0) {
        lgClock = 0;
        lgTimeUp = true;
    }
}

} // namespace hotd::game
