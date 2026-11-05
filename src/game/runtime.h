// The runtime host (docs/teaser_plan.md R1): runs the ported arcade logic (Game<A>, src/port) as the game itself.
//
// The original address space is mapped from the player's files (program ROM at 0, main_data at 0x2000000) and a work
// RAM snapshot of a running game (MAME dump); the arcade's task system (task_create, task_group_run, task_exit ...) and
// heap (mem_alloc / mem_free) are implemented here on that memory, with the original layouts, so the ported routines
// find their objects where the original left them. Each 1/60 s `frame()` runs game_frame's task group: every task's
// routine at its +0 goes to the ported C++ member of that address (the per-kind registries, src/proof/cases), or to a
// runtime stand-in for systems not ported yet (camera, input, drawing ...). Unported routines and boundaries are
// logged once and skipped (the teaser's "logged no-op task").
//
// Tasks never yield in the middle of a routine (checked 2026-10-15: task_unwind_jump is only reached from
// task_group_idle, task_exit, task_unlink and the unused 0x15150), so task_exit / task_unlink unwind with a C++
// exception back to task_group_run; no fibers are needed.
#pragma once

#include "game/bossmode.h"
#include "game/survival.h"
#include "game/horde.h"
#include "port/game.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace hotd::net { struct RuntimeState; }   // netplay's rollback snapshot (src/net/rollback.h)

namespace hotd::game {

class Audio;
class DiffReplay;
using port::A;
using port::Mem;
using port::Tgp;

// The boundaries of the port not implemented by the runtime: logged once each (tools/gen_runtime_bounds.py).
class RuntimeBounds : public port::Game<A> {
public:
    using port::Game<A>::Game;
    virtual void missing(const char* name, uint32_t addr = 0) = 0;
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100 4702)
#endif
#include "runtime_bounds.inc"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
};

// a model drawn this frame: the object ID and its matrix in camera space (the coprocessor's current matrix when the
// game sent the model) as the coprocessor keeps it: the images of the x, y, z axes, then the translation
// (v = x * m[0..2] + y * m[3..5] + z * m[6..8] + m[9..11])
struct DrawModel {
    uint32_t model;
    float m[12];
    float light[3];   // the geometrizer's light vector when it was drawn (0x8000A0 + 3 words: object_draw's light
                      // record by draw_param, the camera's light directions); not used by the renderer yet
    bool background = false;   // the sky (sky_task): drawn first, behind everything (s1)
    bool shadow = false;       // render: a creature part's shadow pass (part_draw with body +0x6C bit 1), squashed onto
                               // Runtime::shadowFloor by the view (UNVERIFIED (teaser): the stored matrix 3 of
                               // shadow_setup is not built; straight down onto the floor)
    float shadowFloor = 0;
    float shadowM[12] = {};    // fix-world: shadow_setup's world-space projection when drawn (the copy at 0x51FDF0, the
                               // layout of m); the view projects the model with it (zero: not set, squashed)
    uint32_t body = 0;         // feet2: the body (creature) whose body_draw drew it, 0 = none (the view's floor pull
                               // goes by the whole body's height, not by each part model's)
    uint8_t fade = 0xFF;       // T1: draw_fade_begin's level when it was drawn (0xFF opaque .. 0; dying enemies, props
                               // fading out); the original scales the geometrizer's colour table by it. Not used by
                               // the renderer yet
    int8_t ramp = -1;          // 4P: players 3 / 4's shot spark through their colour ramp (Renderer::rampStops)
    uint16_t flat = 0xFFFF;    // flash: drawn with a texture header from the rasteriser's header RAM (model table word
                               // 1 = 0x80xxxx, Runtime::geo_draw_header): every polygon untextured in this polygon
                               // palette entry (0x802400 -> 7, white: the bosses' hit flashes); 0xFFFF = the model's own
    uint32_t task = 0;         // rate: the task (object slot) that drew it (task_current): the view's render interpolation
                               // matches a model between two frames by task + model + body (+ its order); never read by the logic
    uint32_t part = 0;         // ragdoll: the body part (BodyPart address) whose draw hook drew it (and its shadow), 0 = none;
                               // the Ragdoll mod (game/ragdoll.h) poses a dead body's models by part. Never read by the logic
    int16_t joint = -1;        // 4P cut-scenes: the body part's joint (part +0x38) when body_part_draw_hook drew it, -1 none
    uint32_t bossPart = 0;     // relfix (Big Head): the part a boss's own part hook drew (the Chariot's 0x9A040 / 0x9A280,
                               // the Hermit's 0x87560: they never pass body_part_draw_hook, so `part` stays 0 for them
                               // and the Ragdoll mod leaves them alone), 0 = none. Never read by the logic
};

class Ragdolls;   // the Ragdoll mod (game/ragdoll.h): draw-only bodies of the dead
struct RagdollWalls;   // (leftovers: its walls, game/ragdoll.h)

class Runtime : public RuntimeBounds {
    friend struct hotd::net::RuntimeState;   // saves / restores the fields that keep game state between frames
public:
    Runtime(Mem& mem, Tgp& tgp) : RuntimeBounds(mem, tgp) {}

    // player input of this frame (the light gun stand-in): aim in the arcade's screen units (centre 0, x right,
    // y up, +-248 x +-192), on screen, trigger pulled this frame, reload (shot off screen)
    struct GunInput {
        bool onScreen = false, trigger = false;
        float x = 0, y = 0;
        bool held = false;   // Survival Mode's SMG: the trigger is held down (automatic fire); read by nothing else
    };
    GunInput gun[4];   // players 3 / 4: the experimental 4P mode only (docs/coop4.md)
    // --input-test (docs/teaser_status/input.md): what the input task handed the game, counted
    uint32_t inputFrames = 0, shotsIn[4] = {}, reloadsIn[4] = {}, startsIn[4] = {};
    bool livesKept = false;          // testing: a player at 0 lives gets 1 back (game over is not ported yet)
    // GitHub #29 (user 2026-11-03): a one-player game in player 2's seat (1), as the arcade's player 2 start button alone
    // gives it: chapter_start puts player 2 in play and player 1 waiting (arcade games only: not PC Mode / 4P)
    int soloSeat = 0;
    // GitHub #5 (Chapter Retry): players 1 / 2 as the chapter began (chapter_start, game_chapter_start): in play, score,
    // lives; the host's retry starts the chapter again and puts these back
    struct RetryPoint {
        int chapter = -1;
        bool in[2] = {};
        int32_t score[2] = {}, lives[2] = {};
    };
    RetryPoint retryPoint;
    void retry_point_take();
    // developer: the whole-game differential (game/diff_replay.h): a MAME run replayed frame by frame (its inputs, the
    // arcade's player_input_update in the input task, the state record written at each frame's end)
    DiffReplay* diffReplay = nullptr;
    void player_input_update_arcade();   // 0x5DF0 (the differential only)
    void set_task_current(uint32_t t);   // the current-task word (+ the differential's task list)
    // the PC's SKIDMARX cheat, Ctrl + 1 / 2 in play (bit 0 / 1: player 1 / 2 keeps their rounds and lives;
    // docs/teaser_status/pc_cheats.md); cheatRounds / cheatLives: the most seen since it was switched on
    int cheatInfinite = 0, cheatRounds[2] = {}, cheatLives[2] = {};

    void frame();                    // one game frame (game_play -> game_frame); Boss Mode: its undrawn walk-up too
    void frame_one();                // exactly one game frame
    // the cut-scene viewer (game/scene_view.h): the player cut-scene running now (players 1 / 2's scene routines, as
    // coop4_scene_bodies tells them apart), -1 none (game/creature.cpp; reads only)
    int player_scene_now() const;
    std::vector<DrawModel> drawList; // the models of the last frame (geo_draw_model)
    uint64_t nanDraws = 0;           // robust: models not drawn for a non-finite matrix (geo_draw_model)
    std::vector<uint32_t> shootable; // the last frame's depth list (shootable objects), before the shot test
    // generated skins of the last frame (skin_draw 0x214F0, the bellies): the type and the two anchor matrices in
    // camera space (coprocessor layout) the hook saved: A (+0x184, the corners follow it), B (+0x1B4, drawn with it)
    struct SkinDraw {
        int type;
        float a[12], b[12];
        uint32_t body = 0;   // 4P phase 6: the body (creature) drawn (coop4_scene_bodies' copies); never read by the logic
    };
    std::vector<SkinDraw> skinDraws;
    float camera[12] = {};           // the view matrix of the last frame (stored matrix 0)
    uint64_t frameCount = 0;
    // Sonic Mode (game/sonic.h): the last frame a voice line's task (kind 161: 0x9B380 / 0x9B4C0) ran; not game state
    uint64_t voiceFrame = 0;

    // diagnostics: boundary and routine names / addresses reached but not implemented, counted
    std::map<std::string, uint64_t> missingCount;
    std::map<uint32_t, uint64_t> unportedCount;
    std::function<void(const std::string&)> log;
    std::string routineName(uint32_t routine) const;
    std::map<uint32_t, std::string> names;   // from names.tsv when present (diagnostics only)

    void missing(const char* name, uint32_t addr = 0) override;
    // --boundary-report (T1): every call into something not really implemented, by name: a generated boundary stub
    // ("stub"), a routine the dispatch does not know ("unported"), a runtime routine's unported part ("partial"), a
    // runtime stand-in for an unported system ("stand-in"); calls and the first frame
    struct BoundaryHit {
        const char* kind = "";
        uint32_t addr = 0;
        uint64_t calls = 0, first = 0;
    };
    std::map<std::string, BoundaryHit> boundaryHits;
    void boundaryHit(const std::string& name, uint32_t addr, const char* kind);
    void standIn(const char* name, uint32_t addr) { boundaryHit(name, addr, "stand-in"); }
    void unported(uint32_t routine);
    // the sorted list (calls per 1,000 frames, highest first) as a Markdown table
    void writeBoundaryReport(const std::string& path, const std::string& title) const;

    // --- the task system (0x14C10..0x15150) ---
    // task_create_child 0x14FA0, task_clear_work 0x15080, the heap (mem_alloc 0x14710, heap_block_insert 0x14810,
    // mem_free 0x149B0, heap_block_free 0x14A50), heap_reset 0x14640, task_group_create 0x14C10: the port's
    // (src/port/tasks.cpp, proofs-7)
    uint32_t task_create(uint32_t routine, uint32_t size) override;         // 0x14EA0: the port's + a trace line
    void task_exit() override;                                              // 0x150D0 UNVERIFIED (teaser)
    void task_unlink(uint32_t task);                                        // 0x15110 UNVERIFIED (teaser)
    void task_group_run(uint32_t group) override;                           // 0x14D40 UNVERIFIED (teaser)
    void task_group_task(uint32_t group);                                   // 0x14CE0 UNVERIFIED (teaser)

    // routine address -> the port (or a stand-in); false: not available
    bool dispatch(uint32_t routine, const uint32_t* regs);
    void setResult(uint32_t) {}   // the proof cases' returned value (compared by hotd_proof; the dispatch drops it)
    void run(uint32_t routine, uint32_t arg);
    void call_routine(uint32_t routine, uint32_t obj) override
    {
        // a part's draw hook / draw mode routine: not object_draw's own shadow pass (restored also when a task unwinds)
        struct Keep { int& v; int s; ~Keep() { v = s; } } keep{objectDraw_, objectDraw_};
        objectDraw_ = 0;
        run(routine, obj);
    }
    void call_routine_f(uint32_t routine, uint32_t obj, float v) override;
    void debug_halt() override;
    void geo_draw_model(uint32_t model) override;
    // PC Mode: whether the view has a model (the host sets it: GameView::hasModel; unset, as headless: none)
    std::function<bool(uint32_t)> modelAvailable;
    bool pm_model_drawable(uint32_t model) override { return modelAvailable && modelAvailable(model); }
    // carchars (user 2026-10-28: "in the beginning where they drive the car, can you make the custom P1 and P2
    // characters show?"; game/creature.cpp): the opening car's seats (player_char_props_draw) show each seat's own
    // character. A seat whose character has a car model in the model set keeps it (the arcade's Rogan / G pair in
    // their own seats; PC models: the PC's six); any other is built from the character's own body (its part tree and
    // part models, as the actors) in a seated pose, in place of the car model's draw. Draws only
    void car_seat_mark(int seat) override;
    void car_seats_dress();
    int car_seat_type(int seat);   // the creature type to build for the seat, -1: its car model stays
    size_t carSeatDraw_[2] = {0, 0};
    uint64_t carSeatFrame_[2] = {~0ull, ~0ull};
    // leftovers: the Ragdoll mod's walls (the host sets it: GameView::ragdollWalls; unset / null, as headless: none)
    std::function<const RagdollWalls*()> ragdollWorld;
    void geo_matrix_position(uint32_t model, uint32_t out) override;       // 0x14200: draws, returns the position
    void hw_write32(uint32_t addr, uint32_t value) override;               // the geometrizer ports -> geo_write
    void text_cursor(int column, int row) override;                          // 0x17660 (sprites.cpp)
    void text_printf(uint32_t format, int32_t arg = 0) override;            // 0xA4EC0: the HUD's score lines only (sprites.cpp)

    // --- the creature core (src/game/creature.cpp, from the disassembly) ---
    // object_draw 0x67DF0, object_draw_model 0x67F60, body_draw 0x37720, part_draw 0x36240, part_hit_mark 0x9CBD0: the
    // port's (src/port/creature.cpp); the runtime's view of their draws:
    void skin_draw(uint32_t obj) override;                                  // 0x214F0: the engine's skin (skinDraws)
    void sophie_skin_draw(uint32_t obj) override;                           // 0x21C30: the port, and her strip (skinDraws, type kSophieStrip)
    static constexpr int kSophieStrip = 134;                                // SkinDraw::type of Sophie's strip (34 + 100)
    void copro(int n, const uint32_t* in, int count, Tgp::Out& out) override;   // marks the shadow pass (0x77, matrix 3)
    void object_draw(uint32_t obj) override;                                // the port's, marked (objectDraw_)
    void body_draw(uint32_t obj) override;                                  // the port's, marked (objectDraw_)
    uint32_t drawBody_ = 0;                                                 // feet2: inside body_draw (DrawModel::body)
    uint32_t drawPart_ = 0;                                                 // ragdoll: the part the draw hook drew last (DrawModel::part)
    uint32_t drawBossPart_ = 0;                                             // relfix: the part a boss's part hook drew last (DrawModel::bossPart)
    // the Ragdoll mod (game/ragdoll.h, docs/modding.md): after the frame's logic, the dead humanoids' drawn poses replaced
    // by a ragdoll's (the draw list only; the address space is only read). Made on the first frame the mod is on
    std::shared_ptr<Ragdolls> ragdolls;
    void ragdoll_frame();
    bool ragdoll_aim(int k, float& ax, float& ay) const;   // testing (HOTD_RAGDOLL_AIM): a dead body's aim point
    int16_t drawJoint_ = -1;                                                // inside body_part_draw_hook: the part's joint (DrawModel::joint)
    int objectDraw_ = 0;                                                   // inside object_draw / body_draw (their shadow passes)
    bool shadowPass_ = false;                                               // between 0x77 {3} and the next pop
    float shadowBase_[12] = {};                                             // the matrix before the shadow projection
    void body_part_draw_hook(uint32_t part);                                // 0x9CE60 (common path) UNVERIFIED (teaser)
    uint32_t flatHeader_ = 0;                                               // geo_draw_model: the texture header (geo_draw_header)
    uint64_t flatLogged_ = ~0ull;                                           // HOTD_TRACE_FLAT: the last frame logged
    void geo_draw_header(uint32_t model, uint32_t header);                  // a model with another texture header (flash)
    static uint16_t header_ram_flat(uint32_t header);                       // the boot's flat-colour header sets
    bool pc_flash_model(uint32_t& model, uint16_t& flat) const;             // THOTD.EXE's hooks for a flashed part
    void shadow_setup(float y) override;                                    // 0x6C760: the port's (src/port/shadow.cpp) + shadowFloor_
    void shadow_update(uint32_t obj) override;                              // 0x6C9E0: the port's (blow_off.cpp) + shadowFloor_
    float shadowFloor_ = 0;

    // --- enemy support and kind 96 (src/game/enemy_support.cpp) ---
    void player_hit(int player, int shake, int side) override;              // 0x6410: the port's + a log line
    uint32_t projectile_hit_player(uint32_t point, int shake, int side) override;   // 0x6640: the port's + the log line

    // --- game flow ---
    // game_frame 0x262A0, mode4_game 0x25C20 and its sub-modes: the port's (src/port/game_flow.cpp, proofs-7)
    // --- the chapter start (src/game/chapter.cpp) ---
    void chapter_start(int chapter, int event, int players, int mode = 4);  // game_new 0x25C40's game part + chapter_load (mode 3: the attract demo) UNVERIFIED (teaser)
    void game_chapter_start();                                              // 0x25D40 (the port's) + the runtime's: 4P seats 3 / 4, the host's view
    void chapter_load(uint32_t groupPtr) override;                          // 0x25DA0 (the port's) + the runtime's set-up around it
    void player_join(int p);                                                // a start request (the start button; player_start_check joins)
    void InitSetPlayerTask();                                               // 0x5D30 (the port's) + the runtime's forms (a player started in play, 4P)
    // shot_lists_clear 0x453C0, chapter_reset_53e1b0 0x32290, chapter_counters_reset 0x67480: the port's (chapter_tasks.cpp)
    void sky_task(uint32_t task);                                           // 0x33F50: the port's (sky.cpp), models as backgrounds
    bool skyDraw_ = false;                                                  // geo_draw_model: inside sky_task
    void world_draw_task(uint32_t task);                                    // 0x4B300 (state part) UNVERIFIED (teaser)
    void uv_scroll_model(uint32_t model);                                   // 0x1E290 (work RAM part; arcpal)
    void geo_write(uint32_t port, uint32_t value) override;
    void evt_watchdog();                                                    // the teaser's guard against waits on unported objects                 // the geometrizer: the zoom (focal length)
    bool pcModels = false;                                                  // the model source is the PC release (the view's choice)
    bool fbiScreen = false;   // attract mode 1, the arcade's "WINNERS DON'T USE DRUGS" screen: optional, off by default (user 2026-10-17)
    float light[3] = {};                                                    // the current light vector (geo_write)
    float focal = 496.0f;                                                   // the camera's zoom of the last frame
    // TEXCHG's area (texture_area_load 0x34FF0: the engine loads the texture set; GameView draws with it), -1 = the
    // chapter's first set
    int textureArea = -1;
    void texture_area_load(uint32_t area, int32_t) override;               // (chapter.cpp) also resets the colour rows
    // the colour translation rows the event scripts swapped in since the area was loaded (color_row_set 0x13F20:
    // colour table row (2 x the call's row + 1) -> entry of the row library 0xE211); the view rebuilds the current set's colour translation when the serial
    // changes (s1)
    std::map<int, int> colorRows;
    uint32_t colorRowsSerial = 0;
    void color_row_set(int32_t row, int32_t entry) override;               // (chapter.cpp) UNVERIFIED (teaser)
    // display only (the custom Blood Color, user 2026-11-03): the colour table rows (2 x row + 1) GGSET last set from a
    // list whose red and green entries differ: the creatures' blood rows (their wounds' every shade, as the game's own
    // green blood recolours them). Cleared by the area load; a later color_row_set of the row takes it out
    std::set<int> bloodRows;
    void ggset_blood_row(int32_t row) override { bloodRows.insert(2 * row + 1); ++colorRowsSerial; }

    // --- sound (src/game/audio.h) and the game flow (src/game/flow.cpp) ---
    Audio* audio = nullptr;                                                 // null: silent
    // the cut-scene viewer's seek (game/scene_view.h; audio null): the sound codes sent while silent, with their frame
    // (the scene's music is cued again from them). Null: not kept
    std::vector<std::pair<uint64_t, uint32_t>>* silentCues = nullptr;
    // netplay rollback (net/net_game.cpp): every sound code that reaches (or, silent, would reach) the audio, in order;
    // a re-run's sounds compared with the first run's (a late remote shot is heard late, not lost). Null: not kept
    std::vector<uint32_t>* cueLog = nullptr;
    void sound_cue(uint32_t cue) override;                                  // 0x154D0 -> the PC cue table
    void sound_send3(uint32_t v) override;                                  // 0x15590: a command straight to the board
    void sound_queue_reset() override;                                      // 0x15630
    void sound_queue_drain();                                               // one queued command a frame (flow.cpp)
    bool startRequest[4] = {};                                              // the start key since the player's last check
    // scoredisp: a trigger at the title (mode 2) starts the game only after this many frames without another trigger
    // (the triggers there are the title codes' letters: title_code_screen); per player, 0 = none pending
    int titleStartWait[2] = {};
    int titleHookFrame = 0;                                                 // HOTD_TITLE_CODE (testing): frames in the title
    // scoredisp: Gameplay "Score Display" (offline): the title code 7 (0x51F42C, the score line; its character pair is
    // pair 0's) in play when no code is set (not with the 4P mode: its body set B uses the code)
    bool scoreDisplay = false;
    void title_input();                                                     // the title's triggers (stand_ins.cpp)
    void title_wait_step();
    uint64_t titleWaitFrame_ = ~0ull;
    bool gameOverDone = false;                                              // mode 5 ended (next_mode): the host restarts
    bool player_out(int p) const;                                           // state table 0xA8790 +4
    void game_mode_frame();                                                 // the mode: 4 (mode4_game 0x25C20, the port's) / 5
    void set_mode(int mode) override;                                       // 0xDE70: the port's (game_flow.cpp) + a log line
    // player_state_set 0x6190, player_st4_out_of_lives 0x5260, player_continue_start 0x52C0, player_continue_frame 0x5300:
    // the port's (player.cpp, game_flow.cpp)
    // player_st6_game_over 0x5470, player_game_over_wait 0x5540, game_over_camera 0x97490, game_over_walker_task
    // 0x5CCD0, game_over_route_task 0x5CF50: the port's (src/port/game_over.cpp, proofs-5)
    void mode5_game_over();                                                 // 0x971E0 (the dispatch; the sub-modes: the port's, proofs-5)
    // gameover: the game-over scene's camera task (group 0xCA790, sub-mode 1: the fallen players' close-up)
    void game_over_timer_tick() override { port::Game<A>::game_over_timer_tick(); }   // 0x5D4A8 (the port's)
    void player_nop() override {}                                           // 0x94F30 (empty)
    // 0x34A40 rand_seed: rand_state = seed (evt_SCNINIT seeds 0 in the attract demo, so its random hit voices and
    // choices follow the arcade's: MAME attract compare, docs/teaser_status/sound_board.md)
    void rand_seed(uint32_t seed) override { mem.set32(A::G::rand_state, seed); }
    // mode 5's sub-modes are the port's (game_over.cpp); their boundaries here: the frame's set-up is the engine's but
    // palette_animation (geo_frame_begin's 0x25310), no fade / books in the runtime, next_mode ends the game over
    void geo_frame_begin() override { palette_animation(); }                 // 0x1CA0
    void geo_view_setup() override {}                                       // 0x1E10
    void palette_fade_finish() override;                                    // 0x18680 (flow.cpp)
    // --- the title and the attract loop (src/game/attract.cpp, docs/teaser_status/title.md) UNVERIFIED (teaser) ---
    bool attract = false;                                                   // the attract modes run (the host's --title)
    void attract_begin();                                                   // the host: the boot's first mode from here
    void body_parts_mark_role(uint32_t obj, int32_t role);                  // 0x1EA30 (+ 0x1E9B0), attract.cpp
    void attract_frame();                                                   // modes 0-3, 6-9 (game_mode_frame)
    void attract_game_new();                                                // mode 4 sub-mode 0 from the attract: Chapter 1
    void next_mode() override;                                              // 0xDDC0
    void hud_mode_init() override;                                          // 0x205A0
    int hud_layout_by_mode() const;                                         // 0x20BD0
    void coin_prompts();                                                    // 0x1F980 coin_update's free-play prompts
    void coin_credits_seen();                                               // 0x1F980 coin_update: the records' credit copies (arcpal)
    void attract_start();                                                   // 0x239A0
    void attract_run();                                                     // 0x23A40
    void title_start();                                                     // 0xD100
    void title_run();                                                       // 0xD290
    void title_code_screen();                                               // 0xD2C0
    void title_camera_start();                                              // 0xD6A0
    void title_camera_task(uint32_t task, bool moving);                     // 0xD6D0 / 0xD650
    void attract_demo();                                                    // mode 3: 0x23B60 demo_start / 0x23C00 demo_run
    void player_demo_start(uint32_t task) override;                         // 0x57E0
    void prologue_start();                                                  // 0x15C90 mode9_story_start
    void prologue_run();                                                    // 0x15E50 mode9_story
    bool prologue_routine(uint32_t routine, uint32_t obj);                  // the prologue's kinds / scenarios
    void game_ending_start();                                               // 0x35160 (the port's, logged; ending.cpp)
    void game_ending_finish() override;                                     // game_ending's tail: the runtime's ranking
    void sprite_init() override;                                            // 0x170C0
    void player_idle(uint32_t) override {}                                  // 0x55D0 (ret)
    int16_t modeBeforeSet_ = 0;                                             // set_mode's previous mode (attract.cpp)
    int prologueForGame_ = 0, prologuePlayers_ = 1, prologueSeat_ = 0;      // PC models: the prologue after the start
    void how_to_play();                                                     // mode 8: 0x265B0 / 0x26740
    void mode8_ammo_draw();                                                 // 0x270D0
    bool lesson_routine(uint32_t routine, uint32_t obj);                    // kind 130 and mode 8's tasks
    void player_lesson_start(uint32_t task) override;                       // 0x57F0
    void ranking();                                                         // modes 6 / 7: 0x21DB0 / 0x21EC0
    bool ranking_routine(uint32_t routine, uint32_t task);                  // the ranking's tasks
    // --- title-2: the ranking's name entry and its backup (src/game/name_entry.cpp) UNVERIFIED (teaser) ---
    bool titleFlow = false;                                                 // the title's loop: a game ends in the ranking / attract
    bool nameEntryOnClear = false;                                          // the chapter clear asks for a name (launcher runs)
    std::string rankingFile;                                                // the backup RAM's top 10 (empty: not kept)
    void ranking_backup_ensure();                                           // 0x22550 backup_ranking_check (+ the file)
    void ranking_backup_save();                                             // 0x226E0
    void rank_insert() override;                                            // 0x5E5D0
    void game_ending_rank();                                                // game_ending 0x352A0's tail (the teaser: the clear)
    void name_entry_finish();
    // the 4P mode's ranking (docs/coop4.md): its own top 10 (rankingFile + "4p"; the arcade's table untouched) and
    // two rounds of the original name entry: players 1 / 2, then players 3 / 4 in their places (records swapped)
    int rank4Round_ = 0;
    std::vector<uint8_t> rank4Arcade_;
    std::string rank4ArcadeFile_;
    void ranking_read_file();
    void rank4_swap_seats();
    void game_name_entry_start();                                           // 0x5D500 (sub-mode 9)
    void game_name_entry();                                                 // 0x5D6D0 (sub-mode 10)
    void name_entry_model_draw(uint32_t obj);                               // 0x5E370
    bool name_entry_routine(uint32_t routine, uint32_t task);               // group 0xC5470's tasks
    void name_entry_key(int c);                                             // the keyboard: a letter, 8 back, 9 forward, 13 END
    bool name_entry_active() const;
    // Light Gun Edition: the 4P name entry's round (1 players 1 / 2, 2 players 3 / 4 in their places; 0 none)
    int rank4Round() const { return rank4Round_; }
    // Light Gun Edition's fun modes (set by the host for a local game; never online): Free Fire (rounds kept full),
    // Shotgun Mode's shells a load (0 off; the radius is Game::lg_hit_scale), Hardcore (an out-of-lives player is out
    // for the game: no continue, no joining back), Big Heads (draws only)
    struct LgModes { bool freeFire = false, hardcore = false, bigHeads = false, vampire = false, flashlight = false; int shotgunShells = 0; } lgModes;
    // Vampire Mode: each seat's blood meter (1 full .. 0 empty), and the hits / kills seen so far (their refills)
    float lgBlood[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t lgBloodHits_[4] = {}, lgBloodKills_[4] = {};
    bool lgOut_[4] = {false, false, false, false};
    // Arsenal Mode (each seat: 0 Pistol, 1 Shotgun, 2 Machine Gun, 3 Rocket Launcher); off: -1 for every seat
    int lgWeapon[4] = {-1, -1, -1, -1};
    uint8_t lgLastRounds_[4] = {6, 6, 6, 6};
    void lg_weapon_next(int p);   // the middle button: the next weapon, its magazine full
    // Weak-Point Roulette: each zombie's glowing part this moment (by its body), and the parts' rings to draw (camera
    // space centres); rebuilt each frame from the frame's draws (every 2 s another part)
    std::map<uint32_t, uint32_t> lgWeak_;
    // (user 2026-11-05: each zombie's weak point fixed from when it shows: body -> its part, the type, the frame last seen)
    struct LgWeakFixed { uint32_t part; int type; uint64_t seen; };
    std::map<uint32_t, LgWeakFixed> lgWeakFixed_;
    uint32_t lgWeakSeed_ = 0x57454B50u;
    struct LgRing { float x, y, z, r; };
    std::vector<LgRing> lgRings;
    bool lg_part_ok(uint32_t part) override;
    // Exploding Zombies: the bodies drawn last frame (their biggest hit part and its camera-space centre), the blasts to
    // draw (camera space, the frame they went off) and the bodies blown up already
    struct LgBody { uint32_t body, part; float x, y, z, r; int type; };
    std::vector<LgBody> lgBodies_;
    struct LgBlast { float x, y, z; uint64_t frame; };
    std::vector<LgBlast> lgBlasts;
    std::set<uint32_t> lgBoomed_;
    std::map<uint32_t, bool> lgVol_;   // each zombie on screen: volatile or not (decided when it first shows)
    uint32_t lgVolSeed_ = 0x9E3779B9u;   // (our own sequence: the game's rand() is not touched)
    bool lg_volatile(uint32_t body) const;
    void lg_explode(int player, uint32_t part) override;
    void lg_bodies_frame();   // (the bodies' list, the glow / heat colours)
    // the looks (Night Vision 1, Wireframe 2, VHS 3: RenderOptions::look); Night Vision: the creatures in heat colours
    int lgLook = 0;
    bool lgSolo = false;
    bool lgMachineGun = false;   // Machine Gun mode (not Arsenal's): its rounds drawn as the machine gun's (silver)   // a one-player mode (The Typing of the Dead 0): seats 2-4 never join
    // Coin-Op Survival: the clock (seconds left; < 0 off), what was counted so far, the last change (shown by the clock)
    double lgClock = -1;
    bool lgTimeUp = false;
    bool lgSvInit_ = false;   // (the counts taken at the first frame: nothing credited for what came before)
    uint32_t lgSvKills_ = 0, lgSvMiss_[4] = {};
    int lgSvRescued_ = 0, lgSvLives_[4] = {};
    float lgClockDelta = 0;
    uint64_t lgClockDeltaFrame = 0;
    void lg_survival_frame();
    void lg_roulette_frame();
    static int lgMagazine(int weapon) { return weapon == 1 ? 3 : weapon == 2 ? 30 : weapon == 3 ? 1 : 6; }
    static const char* lgWeaponName(int weapon)
    {
        return weapon == 1 ? "SHOTGUN" : weapon == 2 ? "MACHINE GUN" : weapon == 3 ? "ROCKET LAUNCHER" : "PISTOL";
    }
    void lg_modes_frame();
    void lg_big_heads();
    std::deque<int> nameKeys;
    std::string nameKeysTest;                                               // --name-keys (testing)
    bool rankingLowTest = false;                                            // --ranking-low (testing): the top 10 with 0 scores (any game places)
    int whiteFade = -1;                                                     // kind 104's fade from white: level 0..31 (-1 off; the HUD draws it)
    // flash: the palette fades of palette_fade_out_task / _in_task (0x19860 / 0x18F00: the Magician's death). Each step
    // blends the colour curves toward white (palette_blend_white 0x1A110, level 0..32) and writes the level to palette
    // RAM 0x1800124 (2D palette entry 0x92: (31 - level) grey, the cut-scene bars' colour; MAME fullgame_us t14360..
    // t14460: the bars grey / white / grey / black with it). backdrop555: the entry's last value (RGB555, the HUD's bars);
    // paletteWhite(): the curves' level while the blend is uploaded (0x51F704 set, not the grey look), else -1
    uint16_t backdrop555 = 0x8000;
    int paletteWhite_ = -1;
    uint32_t fadeIdSeen_ = 0;
    int paletteWhite() const;
    // PC models: THOTD.EXE's screen fade (pcpolish3). The main 3D view's draw context (0xD5A5C0) has a brightness
    // +0x64 (0x10000 normal, 0 black, 0x20000 white), a target +0x68 and the milliseconds left +0x5C; the view's draw
    // (0x57CCE0) steps it towards the target every drawn frame and scales the picture by it (0x1329440). Started by
    // evt_chapter_start (0x4C0CF0: pc_music_fade_chapter 0x41B2A8 = from black over 1000 ms; the ending 0x41B248 = from
    // white over 3000) and kind 104 (0x448674 -> 0x41B228: from white over 1000) where the arcade fades its palette.
    // Levels as fractions of 0x10000; frames at 60 a second.
    struct PcFade { float from = 1.0f, to = 1.0f; uint64_t start = 0; int frames = 0; } pcFade;
    void pcFadeStart(float from, float to, int ms);
    float pcFadeLevel() const;
    int nameKey_ = 0;
    void bookkeeping_game_end() override {}                                 // 0x16850
    void bookkeeping_play_end(int) override {}                              // 0x166D0: the operator's play-time books (none in the runtime)
    void play_time_start(int) override {}                                   // 0x16670 (the books' clock in backup RAM)
    void play_time_continue(int) override {}                                // 0x16690

    // --- R4: settings, teaser cuts, joining / continuing, the chapter clear (src/game/players.cpp, clear.cpp) ---
    // The operator's game assignments as apply_settings (0xDC20) sets them from the EEPROM: difficulty 0-4 (VERY
    // EASY .. VERY HARD, 0x51F490), lives setting 0-11 (table 0xD780: start / max lives, 0x51F5B1 / 0x51F5B0), blood
    // colour 0-3 (RED, GREEN, BLUE, PURPLE as the arcade's menu values, 0x51F9C0); free play (0x51FDB8 = 1).
    struct Settings {
        int difficulty = 2;     // NORMAL
        int lives = 9;          // the default: 3 lives, up to 5
        int bloodColor = 0;
        bool freePlay = true;   // the teaser has no coins
    };
    Settings settings;
    void apply_settings();                                                  // 0xDC20 (the game part) UNVERIFIED (teaser)
    // docs/teaser_plan.md cut (1): route_choice stays 0 (the default route) and the guards of evt_watchdog / missing()
    // let the chapter go on past unported objects. strict = off: the original's behaviour (a wait on an unported
    // object stalls, as the arcade would hang)
    struct Cuts {
        bool defaultRoute = false;   // T1: off by default since every branch path of Chapter 1 plays (t1.md)
        bool guards = true;
    };
    Cuts cuts;
    std::map<int, uint8_t> routeForce;   // --route C:E=V (T1): route_choice = V in chapter C (1-based) event E (key (C-1) x 100 + E)
    void letterbox_task(uint32_t task);                                     // 0x44840 UNVERIFIED (teaser)
    int letterbox = 0;                                                      // the bars' size this frame (task +0x50, 0..0x28; the HUD draws them)
    uint32_t player_start_check(uint32_t task, int state) override;         // 0x6020: the port's + a log line
    // player_st1_continue 0x4F80, player_st2_rejoin 0x4FE0, player_st3_join 0x4FF0, PlStartInit 0x5870: the port's
    // (src/port/player_join.cpp, proofs-7)
    // kind 176 (0x9B690 .. 0x9BDC0): the port's (src/port/chapter_clear.cpp, proofs-7) + the HUD's view (clear)
    void kind176_chapter_clear(uint32_t obj) override;
    void kind176_results(uint32_t obj) override;
    void kind176_results_hold(uint32_t obj) override;
    void kind176_results_end(uint32_t obj) override;
    void game_chapter_end_wait() override;                                  // 0x26020: the port's; the teaser ends at the next chapter
    // the chapter's results (kind 176) for the HUD: active from the clear to the chapter's end; timer = the object's
    // +0x50 (480 down to 0), the bonus lives from the hostages rescued
    struct ChapterClear {
        bool active = false;
        int timer = 0, rescued = 0, bonus = 0;
        bool held = false;   // skipped (the trigger): everything at once
    };
    ChapterClear clear;
    BossMode boss;                                                          // Boss Mode (game/bossmode.h): ticked at the end of frame()
    // Survival Mode (game/survival.h): its lead-in runs undrawn in frame(), its director in the event task's slot (run,
    // evt_run), its end checked after each frame. Off: none of these hooks does anything
    SurvivalMode survival;
    // Horde Mode (game/horde.h): the script's standard creatures get copies (evt_spawned); off (size 0): nothing
    HordeMode horde;
    void evt_spawned(uint32_t task, uint32_t rec) override
    {
        if (survival.leadIn())
            survival.captured(*this, task, rec);
        else if (horde.on() && !survival.active())
            horde.spawned(*this, task, rec);
    }
    // Survival Mode's points (game.h survival_hits: only while survivalScoring)
    void survival_hits(uint32_t obj, uint32_t seats, int killer, uint32_t headSeats) override
    {
        survival.hits(*this, obj, seats, killer, headSeats);
    }
    // a creature as ENTB3 makes one (kind -> obj_kinds, the record's fields), in the current task group (survival.cpp)
    uint32_t survival_spawn(uint32_t kind, uint32_t flags, const float pos[3], const int32_t ang[3], int type, int action,
                         int life, uint32_t delay, uint32_t substate);
    std::vector<std::pair<uint32_t, uint32_t>> survivalPatches_;   // ROM-image words Survival changed (address, old value)
    void survival_restore();                                       // those words set back (a new game, Survival off)
    uint32_t survivalMusic_ = 0;                                   // the lead-in's last music code (played at the arena)
    bool survival_cue(uint32_t cue);                               // sound_cue's Survival part (survival.cpp); true: handled
    bool chapterDone = false;                                               // the game's end after a clear (the next chapter is not playable)
    int chapterChanged = -1;                                                // game_chapter_start: the chapter (0-3) now loaded; the host re-inits its view and sets it back to -1
    bool continueUnplayable = false;                                        // developer (--continue-chapters): go on into chapters not marked playable

    // --- fix-logic: the grey / sepia look and the vblank list (src/game/palette.cpp), UNVERIFIED (teaser) ---
    // For the renderer (render's part): while `on`, the arcade draws everything through its per-channel gamma tables
    // replaced by sepia: a pixel's grey level L = (r + g + b) / 3 (0..255) becomes sepia[L / 4] (r, g, b 0..255, the
    // ROM's ramp 0xAA050). Chapter 1: the 60 frames of kind 172 after the chapter card (frames ~182..242), kind 182's
    // scenes. serial changes at every switch.
    struct GreyPalette {
        bool on = false;
        uint32_t serial = 0;
        uint8_t sepia[64][3] = {};
    };
    GreyPalette greyPalette;
    int16_t rescuedLogged_ = 0;   // fix-world: hostages_rescued_chapter last logged
    void palette_grey_build() override;                                     // 0x1A400
    void vblank_run();                                                      // the vblank interrupt's once-list 0x511FE0
    void palette_bank2_upload();                                            // 0x1A300
    void palette_restore_vblank() override;                                 // 0x1A710
    void grey_palette_sync();
    // arcfix2: palette RAM bank 2 (0x1802000, the polygon palette) kept as the arcade's for palette_grey_build's copies
    void palette_ram_map();
    uint32_t palette_list_upload(uint32_t dst, uint32_t list);
    void palette_init_bank2();                                              // 0x16AB0's bank-2 part (the boot)
    void vblank_words_upload(uint32_t src, bool workRam = true);            // 0x24D70
    void diff_vblank_ran(uint32_t routine, uint32_t listPtr);               // --diff-replay: a mid-frame vblank's routine
    void diff_replay_start(const std::vector<uint8_t>& bank);               // --diff-replay's start (palette2.bin)
    // kind 182 (0x9B600 .. 0x9B660): the port's (src/port/support_c.cpp, proofs-7)

    // --- palanim: the arcade's palette animation (src/game/palette_anim.cpp, formats.md §24), UNVERIFIED (teaser) ---
    // palette_animation (0x25310, from geo_frame_begin at the start of game_frame) computes the pulsing colours into work
    // RAM 0x52066C..0x5206E0; palette_anim_copy (0x24E90, at the vblank, before the frame) copies work RAM 0x520664..
    // 0x5206AE / 0x5206E0 into polygon palette entries 1021..984 / 959 (+ the constants of 211 and 689). The engine has no
    // palette RAM: the copied entries are kept here for the renderer (RGB555 | 0x8000; 0 = not written, the loaded value
    // stays). serial changes when an entry does. The picture shows the previous frame's values, as in MAME.
    struct PolyPaletteAnim {
        uint16_t entries[1024] = {};
        uint32_t serial = 0;
    };
    PolyPaletteAnim polyPaletteAnim;
    void palette_anim_copy();                                               // 0x24E90

    // --- the 2D layer: sprite banks, sprites, the HUD, the boss life bar, subtitles (src/game/sprites.cpp) ---
    // The original's routines writing tile RAM (0x1000000) / character RAM (0x1080000) / the palette shadow, mapped on
    // first use; the HUD composes them (src/assets/sprites_arcade.h). All UNVERIFIED (teaser).
    void sprites_ensure();                                                  // the RAMs + boot / chapter_load's 2D part
    void sprites_boot2d();                                                  // sprites_ensure's boot part (chapter_load: the chapter part is its own)
    void sprites_cover_lift();                                              // coverOff_: the black cover lifted
    void sprites_frame();                                                   // once a frame: blink_phase, 0x51F560
    void text_layers_clear() override;                                      // 0x171B0
    void sprite_bank_tiles_load(uint32_t rec, int sheet) override;          // 0x17CB0: the port's, not with no free slot
    void palette_queue_add(int32_t first, int32_t count) override;          // 0x1A980 (palette.cpp; the HUD reads the shadow)
    void colour_row_queue(int32_t row);                                     // 0x1A930 (palette.cpp)
    void palette_flush_ram();                                               // 0x1AC70, its work RAM part (palette.cpp)
    void sprite_bank_load(int bank, int sheet) override;                    // 0x17D50
    void sprite_bank_free(int bank) override;                               // 0x17BD0
    void sprite_place(uint32_t layer, int a, int b, int x, int y, int sprite) override;   // 0x17D80
    void sprite_draw_anim(uint32_t layer, int a, int b, int x, int y, uint32_t sprite) override;   // 0x17E40
    void sprite_draw_tinted(uint32_t layer, int a, int b, int x, int y, uint32_t sprite);           // 0x17EE0
    void sprite_draw_clipped(uint32_t layer, int a, int b, int x, int y, int cx0, int cx1, int cy0, int cy1,
                             uint32_t sprite) override;                     // 0x17F80
    void sprite_draw(uint32_t layer, int x, int y, int sprite) override;   // 0x180B0 (takes the sprite off)
    void tilemap_clear_rect(uint32_t layer, int x, int y, int w, int h) override;   // 0x18110
    void gun_flash_on(uint32_t layer) override;                             // 0x18600
    void gun_flash_off(uint32_t layer) override;                            // 0x18630
    void tile_text_draw(int x, int y, uint32_t text) override;              // 0x9C450
    void voice_message_frame_sub(int x, int y, uint32_t text) override;     // 0x9C610
    void chapter_screen_reset() override;                                   // 0x17280
    void hud_lives_draw(uint32_t task) override;                            // 0x94740
    void score_line_hide();                                                 // relplay: Score Display: not in cut-scenes / continues
    void hud_ammo_draw(uint32_t task) override;                             // 0x949F0
    void hud_player_start_draw(uint32_t task) override;                     // 0x94670
    void hud_popup_update(uint32_t task) override;                          // 0x94490
    void hud_start_prompt(int p, int which, int on) override;               // 0x209A0: the port's (hud_state.cpp)
    void hud_prompt_anim(uint32_t routine, int x, int y, int p, int on) override;   // table 0xAA450: free play's (0x20C50, 0x20760)
    bool shotFlash = false;                                                 // --shot-flash: the light gun's screen flash
    int flash_ = 0, flashRounds_[2] = {6, 6};
    // Options "Teammate Crosshair Flashes" (docs/coop4.md section 13; menufb, user 2026-10-26: "if you are local mp ...
    // everyone is full ... one player on each PC then everyone else is dimmed but that player is full. By default
    // everyone is full"): the shot flashes / sparks (shot_effect's 0x95140 / 0x95630 draws) of the players on another
    // PC: 0 / 1 Full (the default), 2 Reduced (smaller, shorter, the same spot's in one frame drawn once), 3 Off.
    // ownSeat: this PC's player online (always full); -1 a local game: every player is this PC's, all full. Draws
    // only: nothing of the game's state is written
    int teammateFlash = 0;
    bool hideSparks = false;   // Options > Hit Sparks off: the shots' hit sparks (draw-only)
    bool hideMuzzle = false;   // Options > Gun Muzzle Flash off: the cut-scene guns' flash (draw-only)
    bool hideBlood = false;    // Options > Blood Particles off: the blood bursts / drips (draw-only)
    int transparency = 3;      // Options > Video > Transparency (draw-only): 0 Smooth, 1 Clean Edges, 2 Dithered (arcade), 3 Auto (by the models); RenderOptions::transparency
    bool hideMarks = false;   // trailer: Trailer Mode: no MISS SHOT marks (models 0x3F5 / 0x3F6; draw-only)
    int ownSeat = -1;
    // gunfb (a light-gun player 2026-11-03: flashes where nobody of them fired): the computer teammates' seats (bit per
    // seat, the host sets it each frame): their shot flashes / sparks are a teammate's, so Teammate Crosshair Flashes
    // reduces or hides them in a local game too (Full, the default, unchanged)
    unsigned cpuSeats = 0;
    // gunfb: Video > Shot Flash Size: the shot flash's (shot_effect_flash 0x95140) and its spark's (0x95630) scale, 1 =
    // the arcade's (draw-only)
    float shotFlashScale = 1.0f;
    bool teamFlashDraw(DrawModel& d);                                       // false: not drawn
    struct TeamFx { uint8_t kind, age; int8_t seat; float x, y; uint32_t task, model; };
    std::vector<TeamFx> teamFx_;                                            // this frame's shot flashes (teamFlashDraw)
    uint64_t teamFxFrame_ = ~0ull;
    std::vector<std::pair<uint16_t*, uint16_t>> cardLatch_;                // polish2: the card flicker's upper half
    void cardLatchFlush();
    bool sprites2d_ = false;                                                // the 2D RAMs mapped and loaded
    bool coverOff_ = false;   // a game started past event 0 (--event): the black cover lifted (hangcmp; see sprites_ensure)
    uint16_t& cell(uint32_t layer, int x, int y);                           // a tilemap cell (64 a row)
    uint32_t sheet_layout(uint32_t sprite);                                 // table 0xA6590 +8: {w, h, cells}
    void sprite_cells(uint32_t layer, int a, int b, int x, int y, uint32_t sprite, bool skipZero);
    // the PC models' view of the same 2D layer (the PC keeps sprite IDs, not cells: 0x4434B0): per layer 0..3 and cell
    // the arcade sprite whose top-left is there (| 0x10000; 0 = none), and the subtitles by place; the HUD draws the
    // PC's own sprites from these (src/assets/sprite_map_pc.inc)
    // 64 x 64 cells a layer (the whole tilemap: the scroll registers pick the 64 x 48 shown, the game-over route map
    // scrolls); pcClip: a clipped draw's part of the sprite in cells (left skip | top skip << 8 | width << 16 |
    // height << 24; 0 = all of it: sprite_draw_clipped)
    static constexpr int kPcRows = 64;
    std::vector<uint32_t> pcCells = std::vector<uint32_t>(4 * kPcRows * 64, 0);
    std::vector<uint32_t> pcClip = std::vector<uint32_t>(4 * kPcRows * 64, 0);
    std::map<std::pair<int, int>, std::string> pcTexts;
    // pcrank: the name entry's 3D pieces this frame (name_entry_model_draw: model, place in arcade screen pixels from
    // the centre, y up, and size against a grid letter's), which the PC files lack: the HUD draws PC sprites there
    struct PcNameModel {
        uint32_t model;
        float x, y, size;
    };
    std::vector<PcNameModel> pcNameModels;
    void pc_cells_put(uint32_t layer, int x, int y, uint32_t sprite, int w, int h, uint32_t clip = 0);
    // display2 (widescreen): the side each tilemap cell's sprite belongs to (layer x 64 rows x 64 cells; 0 = centred,
    // 1 = the left corner, 2 = the right): the per-player HUD (torches, bullets, the 1P / 2P frame, RELOAD and its
    // hint, PRESS START, CONTINUE? and its digit, GAME OVER, HOLD YOUR FIRE 0x107) when the sprite lies wholly in one
    // half of the 62-cell screen. The HUD draws side cells at the window's edges in widescreen.
    std::vector<uint8_t> cellSide = std::vector<uint8_t>(4 * 64 * 64, 0);
    std::vector<uint8_t> cellText = std::vector<uint8_t>(64 * 64, 0);   // layer 0 cells a tile_text_draw line holds
    // rainbow2 (Rainbow Blood's 2D blood and HUD option; display only): what each cell's sprite is, as cellSide (layer
    // x 64 rows x 64 cells): 1 a per-player HUD sprite (hud_player_sprite), 2 a hit mark 0x28..0x2D, 0 anything else
    std::vector<uint8_t> cellHud = std::vector<uint8_t>(4 * 64 * 64, 0);
    bool hud_player_sprite(uint32_t sprite);
    int hud_side_of(uint32_t sprite, int x, int w);
    void pc_cells_clear(uint32_t layer, int x, int y, int w, int h);

    // --- T1: the boundaries the play-through calls most (src/game/t1_bounds.cpp), all UNVERIFIED (teaser) ---
    void hostage_shot_effect(uint32_t obj);                                 // 0x1DCD0
    void hit_screen_shake();                                                // 0x6350 (shake_task 0x5DC0)
    // 4P: players 3 / 4's hit notices (the arcade's pop-up, n = the hit's side +0xB8), shown by the HUD for 60 frames
    struct SeatNotice { int kind = -1; uint64_t until = 0; };
    // 4P phase 6 (docs/coop4.md section 3): players 3 / 4's bodies in the cut-scenes that have room, drawn only (copies
    // of players 1 / 2's body draws of the frame with types 14 / 15's models, offset): never game state (game/creature.cpp)
    void coop4_scene_bodies();
    std::map<uint64_t, std::map<uint32_t, uint32_t>> coop4PartMaps_;   // (from type, to type) -> part / shadow model map
    const std::map<uint32_t, uint32_t>& coop4_part_map(int from, int to);
    // the copy's model for a draw the part map does not know: a head (role class 1) drawn with another model (the
    // ending's G talks: his head's mouth frames 0x282..0x291) -> the copy's own head of that joint; else the model
    uint32_t coop4_copy_model(const std::map<uint32_t, uint32_t>& map, int from, int to, const DrawModel& d);
    uint32_t actor_gun_model(int from, int to, const DrawModel& d);   // rellook: the gun drawn beside the zombie Sophie's own hand
    std::map<uint64_t, std::map<int, uint32_t>> coop4JointMaps_;   // (from type, to type) -> joint -> the to type's model
    // galfix4 (user 2026-10-30: "a gap in her skirt", Sophie as Rogan): the joints whose offsets differ between the two
    // types (the thighs: Rogan's hip joints 3.63 down / 0.73 out, Sophie's 2.53 / 0.50): per joint its parent's role,
    // the offset difference (part data +4.., the actor's minus the body's) and the roles under it (itself included).
    // actor_dress moves those legs to the actor's hip joints and the rest of the body by their mean the other way,
    // so the feet stay where they were (coop4_part_map fills it)
    struct ActorFit {
        int parent = -1;
        struct Leg { int role = -1; float d[3] = {}; std::vector<int> sub; };
        std::vector<Leg> legs;
    };
    std::map<uint64_t, ActorFit> actorFits_;
    void actor_fit(uint64_t key, std::vector<DrawModel>& d, size_t first, std::vector<SkinDraw>& s, size_t firstSkin);
    int coop4_scene_type(int from, int s);   // players 3 / 4's copy type (PC Mode: their picks); -1 = none
    // actorswap (the cut-scene viewer's Actors, game/scene_view.h; like Metal Gear Solid 2's demo theater): per role
    // (players 1-4's bodies) the creature type drawn in its place, -1 = its own. Draws only: after the frame's logic,
    // players 1 / 2's body draws get the part models of that type (coop4_part_map: the same skeleton, part for part;
    // a type whose part tree differs keeps the role's own body), players 3 / 4's copies are made with it. Only the
    // viewer sets it (all -1 otherwise: nothing changes)
    int actorSwap[4] = {-1, -1, -1, -1};
    void actor_swap(size_t nDraw, size_t nSkin);   // players 1 / 2's draws of [0, nDraw) / [0, nSkin)
    // actors2: an actor's own extras and motions (game/creature.cpp): the women (Sophie, the zombie Sophie, the
    // researchers on the woman's body) play the game's woman's version of a motion (actor_motion: the jump down's
    // MO_s_tobio_a, as player_body gives Sophie), the men the man's; actor_pose: per body part the matrix that re-poses
    // a drawn part that way (empty: none); actor_dress: a body's draws / skins as an actor (models, pose, Rogan's arcade
    // coat with Rogan only, Sophie's strip with Sophie only)
    static bool actorFemale(int type);
    static bool actorPartFix(int from, int to, int cls, float* fix);   // Dr. Curien's head / cane hand on another body
    static int actor_motion(int motion, int from, int to);
    const std::map<uint32_t, std::array<float, 12>>& actor_pose(uint32_t body, int from, int to);
    void actor_dress(uint32_t body, int from, int to, const std::map<uint32_t, uint32_t>& map, size_t nDraw, size_t nSkin,
                     std::vector<DrawModel>& dOut, std::vector<SkinDraw>& sOut);
    // pmfix (user 2026-10-28: Sophie's middle missing in Rogan's part): the actor's belly skin when the body it plays
    // has none of its own (Rogan: skin number -1, his arcade coat covers the waist; skin_draw only for a type with a
    // skin): drawn from the body's chest / hips draws (class 8 / 9: the skin's anchors +0x184 / +0x1B4 are exactly
    // those matrices). Arcade models only (the PC draws no skins: its parts are whole)
    bool actor_needs_belly(int from, int to) const;
    uint64_t actorCheckOk_ = 0, actorCheckFail_ = 0;   // HOTD_ACTOR_CHECK (actor_dress): bodies drawn right / wrong
    // galact / polish28: Dr. Curien's cane arm on another body: its chest, upper arm, forearm and cane hand (parts) and
    // his idle's (MO_kyu_b frame 0) joints from the chest down (actor_cane_arm, from the body's draws d[0, n));
    // actor_cane_apply re-builds the three from the drawn chest in draws d[0, n) (actor_dress; players 3 / 4's copies)
    struct CaneArm { uint32_t chest = 0, upper = 0, fore = 0, hand = 0; float j[3][12] = {}; };
    bool actor_cane_arm(uint32_t body, int from, const DrawModel* d, size_t n, CaneArm& c);
    static void actor_cane_apply(const CaneArm& c, DrawModel* d, size_t n);
    // the viewer is open (SceneView::applyCast): each frame's lists are kept as they were before the swap, so a new
    // pick shows at once on a paused picture (actor_reswap: players 1 / 2's roles; 3 / 4's from the next frame)
    bool actorView = false;
    void actor_reswap();
    // pmextra (docs/pcmode.md "Extra Characters"): a PC Mode game's extra characters (7..19) are drawn as actors: each
    // seat's actorSwap = its extra's body type (the game logic runs the base PC character's body), set every frame from
    // pm_char while the viewer is closed; actorPm: some seat has one (actor_dress for players 3 / 4, the texture areas)
    bool actorPm = false;
    void actor_pm_sync();
    // the actors whose arcade textures are in one texture area only (Dr. Curien 60: area 11, his second model 78 and
    // the zombie Sophie 67: area 2; the Gallery's looks): their part models -> that area, while the viewer has one
    // picked (actor_areas_update, from SceneView::applyCast); GameView draws those models with that area's texture set
    // (the chapter's, else that area's chapter stage). Empty otherwise
    std::map<uint32_t, int> actorAreas;
    std::map<int, int> actorAreaLook;   // Survival Mode: an area's colour rows taken from this type's look (galleryLook)
    void actor_areas_update();
    void survival_areas();   // Survival Mode: the cut creatures' part models -> their texture areas (survival.cpp)
    std::vector<DrawModel> actorPreDraw_;
    std::vector<SkinDraw> actorPreSkin_;
    size_t actorOwn_[2] = {0, 0};
    bool actorPreValid_ = false;
    int coop4SceneLogged_ = -2;   // HOTD_TRACE_COOP4_SCENE: the last scene logged
    // the cut-scene variations of players 3 / 4 (docs/coop4.md section 16; launcher.ini scene4p=, --scene4p,
    // HOTD_SCENE4P): "00:B,09:C" picks variation B of scene 00 ...; a scene not named keeps A (the look of sections
    // 10-12). Draws only, as coop4_scene_bodies: the variations replay players 1 / 2's recorded poses of this scene
    // (live, a beat later, or held at one frame) at their own places
    std::string scene4p;
    struct Scene4pRec {
        float cam[12];                        // the camera (world to camera) of the frame
        std::vector<DrawModel> d[2];          // players 1 / 2's body draws, world space
        std::vector<SkinDraw> s[2];
        float root[2][12];                    // the bodies' matrices (+0x1E4), world space
        bool drawn[2] = {false, false};
        int type[2] = {0, 0};
        float p1Side = 1;
        int shot = 0;                         // the camera shot (0 = the scene's first; a camera cut starts the next)
        int shotStart = 0;                    // the record index where this shot started
        // the viewer's actor for seat s (players 3 / 4) as a copy of body i, made at this frame (actor_dress's parts
        // for the variations' copies): the actor type (-1 none), per draw of d[i] the re-pose (actor_pose; empty:
        // none), the belly skin's two (class 8 / 9), Rogan's coat on a body he plays (model, the chest draw; 0 none)
        int dressTo[2][2] = {{-1, -1}, {-1, -1}};
        std::vector<std::array<float, 12>> poseC[2][2];
        std::array<float, 12> skinC[2][2][2] = {};
        bool skinPosed[2][2] = {};
        uint32_t coatModel[2][2] = {};
        int coatAt[2][2] = {{-1, -1}, {-1, -1}};
        CaneArm cane[2][2];                   // polish28: Dr. Curien's cane arm (caneOn: he plays seat s on body i)
        bool caneOn[2][2] = {};
        // pmfix: draws of d[i] the actor's own skins are built on: the belly (actor_needs_belly: chest, hips) and
        // Sophie's strip on a body she plays (the class 10 / 11 thighs); -1 none
        int bellyAt[2][2][2] = {{{-1, -1}, {-1, -1}}, {{-1, -1}, {-1, -1}}};
        int stripAt[2][2][2] = {{{-1, -1}, {-1, -1}}, {{-1, -1}, {-1, -1}}};
    };
    std::vector<Scene4pRec> scene4pRecs_;     // this scene's frames so far
    // the `mirror` word: players 1 / 2's body draws paired left / right (by draw index, from the body's first drawn
    // frame of the scene), so a mirrored copy keeps its gun in the right hand; empty: not pairable
    std::vector<int> scene4pPair_[2];
    std::vector<std::array<float, 12>> scene4pLink_[2];   // and each draw's link to its pair's first-frame place
    bool scene4pPaired_[2] = {false, false};
    std::set<uint32_t> unmappedLogged_;   // HOTD_TRACE_SCENE4P: models copied without a part-map entry (logged once)
    int scene4pScene_ = -1;
    uint64_t scene4pLast_ = ~0ull;
    int coop4_variant_scene() const;          // the player cut-scene now (scenes 15-18 too, kSceneEnding), -1 none
    // ours: the ending's two-player scene after the Magician's defeat as a scene number (the cut-scene viewer's
    // "4-19", scene4p= "19:E"); not one of cam_job18's player scenes
    static constexpr int kSceneEnding = 19;
    // ours: the ending's staff roll (event 3 after the farewell) as the viewer's "4-20" (hudfix4, user 2026-10-26)
    static constexpr int kSceneCredits = 20;
    static constexpr int kSceneBossDeath = 21;   // the cut-scene viewer: the chapter's boss defeat (boss_deaths), ours
    bool ending_scene_now() const             // mode 4 sub-mode 8 (game_ending), chapter slot 6, event 2 (both players)
    {
        return mem.at<int16_t>(A::G::game_mode) == 4 && mem.at<int16_t>(A::G::game_submode) == 8 &&
               mem.at<int8_t>(A::G::chapter) == 6 && mem.at<int8_t>(A::G::event_index) == 2;
    }
    bool coop4_scene_variant(int scene);      // false: variation A (the plain coop4_scene_bodies)
    void coop4_enable();   // testing (the input test's 4-seat section): the 4P mode switched on in a running game
    SeatNotice seatNotice[2];
    // object_draw_body 0x37D80, body_part_draw_mat 0x37B80: the port's (src/port/support_c.cpp, proofs-7)
    void researcher_blood_effect(uint32_t obj);                             // 0x1DB30
    // tumble_corners 0x6A970, tumble_update 0x6ADD0, tumble_start 0x6AF70: the port's (src/port/support_c.cpp)
    void tumble_bounce(uint32_t obj, uint32_t box) override;                // 0x6B380
    void copro_axis_rotate(uint32_t angle, float x, float y, float z);      // coprocessor 0x85 stand-in
    // task_create_child_alloc 0x15010, spin_axis_from_velocity 0x33EC0: the port's (src/port/support_c.cpp, proofs-7)
    void part_damage_callback(uint32_t routine, uint32_t part) override;    // table 0xC66A0
    // 0x2320 draw_fade_begin(level): the models drawn after it fade (DrawModel::fade); 0x1A80 geo_send_words(0, 31,
    // 0x7A0) puts the colour table back (opaque)
    int fade_ = 0xFF;
    void draw_fade_begin(int alpha) override { fade_ = alpha & 0xFF; }
    void geo_send_words(uint32_t a, uint32_t b, uint32_t c) override
    {
        if (a == 0 && b == 31 && c == 0x7A0)
            fade_ = 0xFF;
        else
            missing("geo_send_words", 0x1A80);
    }
    // body_drips_draw 0x60430: the port's (src/port/support_c.cpp, proofs-7)
    void player_paused(uint32_t task);                                      // 0x5730 (state 10)
    void hud_player_notice_show(uint32_t task, int n) override;            // 0x945D0: the port's (hud_state.cpp)
    bool body_hook_special(uint32_t part, uint32_t body, int32_t k, int32_t cls);   // 0x9CE60 types 30 / 34

    // --- stand-ins for systems not ported yet (src/game/stand_ins.cpp) ---
    void camera_task_stand_in(uint32_t task);       // 0x2B100: the snapshot's camera, loaded into the coprocessor
    void input_task_stand_in(uint32_t task);        // 0x5D90: the mouse as the light gun
    // attack_slots_task 0x31C00, depth_lists_task 0x53480: the port's (src/port/chapter_tasks.cpp, proofs-7)
    void nop_task(uint32_t) {}

private:
    // creature core helpers (creature.cpp)
    int32_t joints(uint32_t obj);

    struct Unwind { int code; };     // task_unwind_jump's value: 1 = task exited, 2 = unlinked, 3 = group done
    struct Halt {};
    std::set<uint32_t> nopRoutines_;
    uint32_t geoPort_ = 0;
    int lightWord_ = 3;
    uint32_t geoWords_[12] = {}, geoMat_[12] = {}; int geoWord_ = 0;   // the direct model draw (0x8000B0 matrix, 0x800010 model)
    std::map<uint64_t, uint32_t> geoModels_;                              // its header words -> model ID (the table 0x2E73530)
    std::vector<uint32_t> runStack_;
    uint32_t wdPc_ = 0, wdFrames_ = 0;
    uint64_t wdUnported_ = 0;
    uint64_t unportedTotal() const;
};

} // namespace hotd::game
