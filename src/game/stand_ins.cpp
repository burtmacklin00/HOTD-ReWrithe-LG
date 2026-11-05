// Runtime stand-ins for per-frame system tasks whose routines are not ported yet (runtime.h), and two small system
// tasks ported here because the runtime needs them every frame. UNVERIFIED (teaser).
#include "game/runtime.h"

#include <cstdlib>
#include <cstring>

namespace hotd::game {

using G = A::G;
using port::fbits;
using port::ffrom;

// 0x2B100 camera_task stand-in: the camera jobs (L2) are not ported; the camera stays where the snapshot left it.
// camera_matrices_update's tail as the original sends it: the current slot's (0x51F620) two stored matrices
// (0x51EA80 + slot x 0x154 -> coprocessor stored matrix 1, 0x51EA50 + ... -> stored matrix 0), the current matrix
// left at matrix 0, the view matrix every object is drawn with.
void Runtime::camera_task_stand_in(uint32_t)
{
    const uint32_t slot = mem.u32(0x51F620) * 0x154;
    Tgp::Out none;
    uint32_t m[12];
    for (uint32_t k = 0; k < 12; ++k)
        m[k] = mem.u32(0x51EA80 + slot + 4 * k);
    copro(0x07, m, 12, none);
    uint32_t s[1] = {1};
    copro(0x75, s, 1, none);
    for (uint32_t k = 0; k < 12; ++k)
        m[k] = mem.u32(0x51EA50 + slot + 4 * k);
    copro(0x07, m, 12, none);
    s[0] = 0;
    copro(0x75, s, 1, none);
    std::memcpy(camera, m, sizeof camera);
}

// (scoredisp) the title's input, each title frame before title_code_screen (title_run): the triggers into the input
// blocks' pressed edges (bit 0 of 0x51F518 / 0x51F534: player_input_update's, which the port does not run there), and
// the trigger start: the triggers are the title codes' letters ("Left three times, Right twice, then Start"), so a
// trigger starts the game only after 45 frames (0.8 s) without another trigger; a start button starts it at once.
// HOTD_TITLE_CODE=<L / R letters> (testing): the code by the triggers, a letter every 10 frames from the 60th
void Runtime::title_input()
{
    static const char* titleHook = std::getenv("HOTD_TITLE_CODE");
    if (titleHook) {
        const int k = titleHookFrame - 60;
        gun[0].trigger = gun[1].trigger = false;   // (the hook owns the triggers: one frame a letter)
        if (k >= 0 && k % 10 == 0 && size_t(k / 10) < std::strlen(titleHook))
            gun[titleHook[k / 10] == 'R' ? 1 : 0].trigger = true;
        ++titleHookFrame;
    }
    for (int p = 0; p < 2; ++p) {
        uint16_t& edge = mem.at<uint16_t>(p == 0 ? 0x51F518u : 0x51F534u);
        edge = uint16_t(gun[p].trigger ? edge | 1 : edge & ~1);
    }
    if (gun[0].trigger || gun[1].trigger) {
        for (int p = 0; p < 2; ++p)
            titleStartWait[p] = gun[p].trigger || titleStartWait[p] ? 46 : 0;   // (46: title_wait_step takes one now)
    }
    title_wait_step();
}

// the title's pending trigger start (title_input) counted down: once a frame (the title, and the input task after it)
void Runtime::title_wait_step()
{
    if (titleWaitFrame_ == frameCount)
        return;
    titleWaitFrame_ = frameCount;
    for (int p = 0; p < 2; ++p)
        if (titleStartWait[p] > 0 && --titleStartWait[p] == 0 && !startRequest[p]) {
            const int16_t mode = mem.at<int16_t>(G::game_mode);
            if (mode != 4 && mode != 5)
                startRequest[p] = true;
        }
}

// 0x5DF0 player_input_update, read from the disassembly (0x5DF0..0x601C), for the whole-game differential only
// (game/diff_replay.h: there the vblank interrupt's and inputs_read's results come from the MAME run): the buttons
// from the live switches (input_source 0), the attract demo's stream (1, mode 3) or the how-to-play stream (1, mode 8),
// none (2); then the pressed / released edges. UNVERIFIED (teaser) as a capture; checked by the differential itself.
void Runtime::player_input_update_arcade()
{
    const uint16_t prev0 = mem.at<uint16_t>(0x51F2C8), prev1 = mem.at<uint16_t>(0x51F394);
    mem.at<uint16_t>(0x51F2CA) = prev0;
    mem.at<uint16_t>(0x51F396) = prev1;
    const uint32_t src = mem.u32(G::input_source);
    if (src == 1) {
        if (mem.at<int16_t>(G::game_mode) == 8) {
            const uint32_t s = mem.u32(G::mode8_input_ptr);
            mem.at<uint16_t>(0x51F2C8) = mem.at<uint16_t>(s + 4);
            mem.at<uint16_t>(0x51F394) = mem.at<uint16_t>(s + 0xC);
            mem.at<uint16_t>(0x51F2BC) = mem.at<uint16_t>(s);
            mem.at<uint16_t>(0x51F2BE) = mem.at<uint16_t>(s + 2);
            mem.at<uint16_t>(0x51F388) = mem.at<uint16_t>(s + 8);
            mem.at<uint16_t>(0x51F38A) = mem.at<uint16_t>(s + 0xA);
            if (mem.u32(G::demo_input_advance) != 0)
                mem.set32(G::mode8_input_ptr, s + 16);
        } else {
            const uint32_t s = mem.u32(G::demo_input_ptr);
            static constexpr uint32_t kBytes[8][2] = {{0, 0x51F2DA}, {1, 0x51F2DD}, {2, 0x51F2DB}, {3, 0x51F2DC},
                                                      {8, 0x51F3A6}, {9, 0x51F3A9}, {10, 0x51F3A7}, {11, 0x51F3A8}};
            for (const auto& b : kBytes)
                mem.at<uint8_t>(b[1]) = mem.at<uint8_t>(s + b[0]);
            mem.at<uint16_t>(0x51F2DE) = mem.at<uint16_t>(s + 4);
            mem.at<uint16_t>(0x51F2E0) = mem.at<uint16_t>(s + 6);
            mem.at<uint16_t>(0x51F3AA) = mem.at<uint16_t>(s + 0xC);
            mem.at<uint16_t>(0x51F3AC) = mem.at<uint16_t>(s + 0xE);
            if (mem.u32(G::demo_input_advance) != 0)
                mem.set32(G::demo_input_ptr, s + 16);
        }
    } else if (src == 0) {
        mem.at<uint16_t>(0x51F2C8) = mem.at<uint16_t>(0x51F514);
        mem.at<uint16_t>(0x51F394) = mem.at<uint16_t>(0x51F530);
    } else if (src == 2) {
        mem.at<uint16_t>(0x51F394) = 0;
        mem.at<uint16_t>(0x51F2C8) = 0;
    }
    const uint16_t cur0 = mem.at<uint16_t>(0x51F2C8), cur1 = mem.at<uint16_t>(0x51F394);
    const uint16_t x0 = uint16_t(prev0 ^ cur0), x1 = uint16_t(prev1 ^ cur1);
    mem.at<uint16_t>(0x51F2CC) = uint16_t(cur0 & x0);
    mem.at<uint16_t>(0x51F2CE) = uint16_t(x0 & prev0);
    mem.at<uint16_t>(0x51F398) = uint16_t(cur1 & x1);
    mem.at<uint16_t>(0x51F39A) = uint16_t(x1 & prev1);
}

// 0x5D90 input_task: camera_mode_run (ported), then a stand-in for player_input_update (not ported): the mouse as both players' light
// gun, written where player_shoot reads it: gun mode 2 (the gun's flash phase that allows a shot), trigger edge
// (gun_shot), on screen, aim; a trigger off screen reloads (6 rounds).
void Runtime::input_task_stand_in(uint32_t)
{
    title_wait_step();
    camera_mode_run();   // 0x6CAB0 (ported, L2): the CAMDR / CCTB 33 mode routine of this frame
    if (diffReplay) {   // the differential: the guns and switches are the arcade's (inputs.bin), so the original routine
        player_input_update_arcade();
        return;
    }
    standIn("player_input_update (the mouse as the light gun)", 0x5DF0);
    // the title / attract (title worker): a trigger pull is a start too (the arcade wants the start button; here a
    // player with only a mouse can start from the title). The title itself (mode 2): title_input
    if (const int16_t mode = mem.at<int16_t>(G::game_mode); mode != 2 && mode != 4 && mode != 5)
        for (int p = 0; p < 2; ++p)
            if (gun[p].trigger)
                startRequest[p] = true;
    // the attract demo (title worker): player_input_update's recorded source (input_source 1, set by SCNINIT in mode
    // 3): 16 bytes a frame from demo_input_ptr, per player trigger, on screen, shot, gun mode, aim x / y; the start
    // buttons stay live (a start during the demo starts the game). UNVERIFIED (teaser)
    if (mem.u32(G::input_source) == 1 && mem.at<int16_t>(G::game_mode) == 8) {   // the how-to-play stream (0xC5BC0)
        const uint32_t s = mem.u32(G::mode8_input_ptr);
        ++inputFrames;
        for (uint32_t p = 0; p < 2; ++p) {
            port::a::Player& pl = mem.at<port::a::Player>(G::players, p);
            const uint16_t before = pl.buttons;
            pl.buttons_prev = before;
            pl.buttons = mem.at<uint16_t>(s + p * 8 + 4);
            pl.aim_recorded[0] = mem.at<int16_t>(s + p * 8);
            pl.aim_recorded[1] = mem.at<int16_t>(s + p * 8 + 2);
            const uint16_t ch = uint16_t(before ^ pl.buttons);
            pl.pressed = uint16_t(ch & pl.buttons);
            pl.released = uint16_t(ch & before);
            uint16_t& sw = mem.at<uint16_t>(0x51F99C + p * 2);
            sw = uint16_t(startRequest[p] ? sw | 0x800 : sw & ~0x800);
            startRequest[p] = false;
        }
        if (mem.u32(G::demo_input_advance) != 0)
            mem.set32(G::mode8_input_ptr, s + 0x10);
        return;
    }
    if (mem.u32(G::input_source) == 1 && mem.at<int16_t>(G::game_mode) == 3) {
        const uint32_t s = mem.u32(G::demo_input_ptr);
        ++inputFrames;
        for (uint32_t p = 0; p < 2; ++p) {
            port::a::Player& pl = mem.at<port::a::Player>(G::players, p);
            pl.demo_trigger = mem.at<uint8_t>(s + p * 8);
            pl.demo_on_screen = mem.at<uint8_t>(s + p * 8 + 1);
            pl.demo_shot = mem.at<uint8_t>(s + p * 8 + 2);
            pl.demo_gun_mode = mem.at<uint8_t>(s + p * 8 + 3);
            pl.demo_aim[0] = mem.at<int16_t>(s + p * 8 + 4);
            pl.demo_aim[1] = mem.at<int16_t>(s + p * 8 + 6);
            uint16_t& sw = mem.at<uint16_t>(0x51F99C + p * 2);
            sw = uint16_t(startRequest[p] ? sw | 0x800 : sw & ~0x800);
            startRequest[p] = false;
        }
        if (mem.u32(G::demo_input_advance) != 0)
            mem.set32(G::demo_input_ptr, s + 0x10);
        return;
    }
    mem.set32(G::input_source, 0);
    mem.at<uint8_t>(G::gun_flash_phase) = 2;
    ++inputFrames;
    for (int p = 0; p < seat_count(); ++p) {   // 4P: seats 3 / 4 too (their records in the 4P block)
        port::a::Player& pl = mem.at<port::a::Player>(player_rec(p));
        GunInput in = gun[p];
        if (survival.active())   // Survival Mode's gun (game/survival.h gunInput): the shop's signs, the reload by hand, the SMG
            survival.gunInput(*this, p, in.trigger, in.onScreen, in.held, in.x, in.y);
        pl.on_screen = in.onScreen ? 1 : 0;
        pl.aim[0] = int16_t(in.onScreen ? in.x : 1000.0f);
        pl.aim[1] = int16_t(in.onScreen ? in.y : 1000.0f);
        // the trigger as the gun's flash cycle hands it over (hud3): gun_shot on every pull, on or off the screen, so
        // player_shoot takes an off-screen pull as the reload (rounds 6, its sound 0xA01200 = the PC's cue 0x1C); the
        // gun record's trigger (+0x15) cleared each frame as the arcade's phases 0 / 1 do (player_shoot keeps it only
        // for a pull it could not fire: hud_ammo_draw's empty click). Before, gun_shot was set on screen only and the
        // record's trigger stuck after an empty pull: the reload sound played only by chance, then every frame.
        mem.at<uint8_t>(gun_shot_at(p)) = in.trigger ? 1 : 0;
        mem.at<uint8_t>(gun_input_rec(p) + 0x15) = 0;
        pl.pressed = in.trigger ? 1 : 0;   // bit 0: the trigger (the continue countdowns skip a digit)
        shotsIn[p] += in.trigger && in.onScreen ? 1 : 0;   // --input-test's counts
        reloadsIn[p] += in.trigger && !in.onScreen ? 1 : 0;
        startsIn[p] += startRequest[p] ? 1 : 0;
        if (in.trigger && !in.onScreen) {
            pl.rounds = pm_full_rounds(p);   // 6; PC Mode: the character's rounds
            pl.empty = 0;
        }
        // the start button (player_start_pressed: bit 11 of the player's switch word), one frame per request
        uint16_t& sw = mem.at<uint16_t>(seat_at(0x51F99C, port::coop4::kStartButtons, 2, p));
        // Boss Mode: the PC reads no start button in front-end state 4 (0x436430): nobody joins a boss game
        sw = uint16_t(startRequest[p] && !boss_on() ? sw | 0x800 : sw & ~0x800);
        startRequest[p] = false;
    }
}

} // namespace hotd::game
