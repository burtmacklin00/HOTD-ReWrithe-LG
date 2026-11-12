#include "game/scene_view.h"

#include "game/audio.h"
#include "game/keybinds.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace hotd::game {

// actorswap: the bodies that share the players' skeleton part for part (Runtime::coop4_part_map walks it). Rogan 2 /
// G 2 are the 4P mode's players 3 / 4; Sophie and the researcher are PC Mode's picks (THOTD.EXE 0x5CE348's types); a
// body whose part tree differs in a chapter (or is not loaded there) is not swapped: the role keeps its own.
// actors2: the ten researchers too (creature types 17, 19-22, 49 on the man's body, 23, 25-27 on the woman's: the
// players' 20-joint tree, maincpu 0xC7240). followup (2026-10-27): Dr. Curien (60, 78) and the zombie Sophie (67) too:
// their arcade textures are in one area only (Runtime::actorAreas: GameView draws their parts with that area's set,
// from Chapter 4's stage when the scene's area is another), and Curien's head and cane hand are turned to the players'
// part axes (Runtime::actorPartFix). With the PC models the zombie Sophie is Sophie's model (the PC files have no other).
// `key`: the word for --cast and launcher.ini scenecast=
const std::vector<SceneView::Actor>& SceneView::actors()
{
    static const std::vector<Actor> v = {
        {"Rogan", 0x02, "rogan"}, {"G", 0x0D, "g"}, {"Player 3", 0x0E, "p3"}, {"Player 4", 0x0F, "p4"},
        {"Sophie", 0x22, "sophie"},
        {"Researcher", 17, "r1"}, {"Researcher (2)", 19, "r2"}, {"Researcher (3)", 20, "r3"}, {"Researcher (4)", 21, "r4"},
        {"Researcher (5)", 22, "r5"}, {"Researcher (6)", 49, "r6"},
        {"Researcher (woman)", 23, "w1"}, {"Researcher (woman, 2)", 25, "w2"}, {"Researcher (woman, 3)", 26, "w3"},
        {"Researcher (woman, 4)", 27, "w4"},
        {"Dr. Curien", 60, "curien"}, {"Dr. Curien (Dead)", 78, "curien2"}, {"Sophie (zombie)", 67, "zsophie"},
    };
    return v;
}

const char* SceneView::roleName(int r)
{
    static const char* const k[kRoles] = {"Rogan's part", "G's part", "Player 3's part", "Player 4's part"};
    return k[std::clamp(r, 0, kRoles - 1)];
}

void SceneView::castStep(int role, int dir)
{
    if (role < 0 || role >= kRoles)
        return;
    const int n = int(actors().size());
    cast[role] = ((cast[role] + (dir < 0 ? -1 : 1)) % n + n) % n;
}

bool SceneView::castDefault() const
{
    for (int r = 0; r < kRoles; ++r)
        if (cast[r] != r)
            return false;
    return true;
}

std::vector<std::string> SceneView::castLines() const
{
    std::vector<std::string> v = {"ACTORS  Up / Down role  Left / Right actor  A close"};
    for (int r = 0; r < kRoles; ++r)
        v.push_back(std::string(r == castRow ? "> " : "") + roleName(r) + ": < " + castLabel(r) + " >");
    return v;
}

void SceneView::applyCast(Runtime& rt) const
{
    // pmfix2 (user 2026-10-28: the PC Mode extras drawn with the chapter's textures): the window's loop calls this every
    // loop. With the viewer closed (and closed the loop before) the actors are PC Mode's (Runtime::actor_pm_sync, at
    // each game tick): wiping them here every loop left the loops between two ticks (Display Refresh above 60 Hz)
    // without the extras' texture areas, so their bodies were drawn in the chapter's set; headless runs tick every loop
    if (!active() && !rt.actorView)
        return;
    bool changed = rt.actorView != active();
    rt.actorView = active();
    for (int r = 0; r < kRoles; ++r) {
        const int t = active() && cast[r] != r ? actors()[size_t(cast[r])].type : -1;
        changed = changed || rt.actorSwap[r] != t;
        rt.actorSwap[r] = t;
    }
    if (changed) {
        rt.actor_areas_update();   // (Dr. Curien / the zombie Sophie: their texture area)
        rt.actor_reswap();   // (a paused picture shows the new pick at once)
    }
}

bool SceneView::castFrom(const std::string& s)
{
    castReset();
    int r = 0;
    for (size_t p = 0; p <= s.size() && r < kRoles; ++r) {
        size_t q = s.find(',', p);
        if (q == std::string::npos)
            q = s.size();
        std::string w = s.substr(p, q - p);
        p = q + 1;
        for (char& c : w)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        if (w.empty() || w == "-")
            continue;
        while (!w.empty() && std::isspace(static_cast<unsigned char>(w.back())))
            w.pop_back();
        while (!w.empty() && std::isspace(static_cast<unsigned char>(w.front())))
            w.erase(0, 1);
        if (w == "researcher")
            w = "w3";   // (actorswap's word for type 26)
        int found = -1;
        for (int a = 0; a < int(actors().size()); ++a)
            if (w == actors()[size_t(a)].key || w == std::to_string(a))
                found = a;
        if (found < 0)
            return false;
        cast[r] = found;
    }
    return true;
}

std::string SceneView::castString() const
{
    std::string s;
    for (int r = 0; r < kRoles; ++r)
        s += std::string(r ? "," : "") + actors()[size_t(cast[r])].key;
    return s;
}

// where each one plays (4 players, --autofire; HOTD_TRACE_SCENES lists them): the chapter from its start, the route
// where the scene is off the --autofire play's own one. Not reached in those runs, so not listed: 01, 05 / 13
// (knocked flying), 16 (run jump)
const std::vector<SceneEntry>& sceneEntries()
{
    static const std::vector<SceneEntry> v = {
        {0, 0, "The opening run", "", true},            // frame ~244
        // (galfix2, user: "walk and shoot cutscene ... does not play": the --autofire play now takes event 0's other
        // way (events 0 1 3 ...), so 02 and 04 never came; event 0's choice 1 (to event 2) is now part of their route)
        {0, 2, "Walk and shoot", "1:0=1", true},        // ~5902 (the bodies are not in its picture)
        {0, 3, "Ladder", "1:3=1", false},               // ~9018
        {0, 4, "Fall backside", "1:0=1,1:5=1", false},  // ~8259
        {0, 6, "Reload shoot", "1:3=1", true},          // ~12489
        {0, 7, "Jump down a", "1:3=1", true},           // ~12773
        {0, 8, "Finish pose", "", false},               // ~16308
        {1, 9, "Stance shoot", "", true},               // ~183
        {1, 11, "Jump down c", "", true},               // ~1314
        {1, 12, "Reload kick", "", true},               // ~7463
        {1, 10, "Jump down b", "2:5=0", true},             // ~11981
        {1, 14, "Rattle door", "", false},              // ~21715
        {1, Runtime::kSceneBossDeath, "The Hangedman's defeat", "", false},   // (user 2026-10-31: Free Cam on it)
        {2, 15, "Jump down", "", false},    // ~283
        {2, Runtime::kSceneBossDeath, "The Hermit's defeat", "", false},
        {3, 17, "Dash", "", false},                     // ~183
        {3, 18, "Second scene", "", false}, // ~11836
        {3, Runtime::kSceneBossDeath, "The Magician's defeat", "", false},   // (he rises, speaks, blows up)
        // ours (Runtime::kSceneEnding, not a cam_job18 scene): the ending's event 2 after the Magician's defeat, the
        // two players' farewell (~26580 with --default-route; 1 player: the ending's event 0, player 1 alone)
        {3, Runtime::kSceneEnding, "Goodbye Curien", "", false},
        // ours (Runtime::kSceneCredits): the staff roll, the ending's event 3 after the farewell, up to the END card
        // (user 2026-10-26: "add the credits as a cut-scene")
        {3, Runtime::kSceneCredits, "Staff roll", "", false},
    };
    return v;
}

std::vector<std::string> sceneRowLabels()
{
    std::vector<std::string> v;
    for (const SceneEntry& e : sceneEntries()) {
        char b[80];
        std::snprintf(b, sizeof b, "%d-%02d %s", e.chapter + 1, e.scene, e.name);
        v.push_back(b);
    }
    return v;
}

std::string sceneLine(int i)
{
    const SceneEntry& e = sceneEntries()[size_t(i)];
    char b[200];
    if (e.scene == Runtime::kSceneEnding)
        return "The ending, after the Magician: players 3 and 4 follow player 2 a beat later";
    if (e.scene == Runtime::kSceneBossDeath)
        return "The boss's defeat: from the killing shot (Free Cam: F / L3)";
    if (e.scene == Runtime::kSceneCredits)
        return "The ending's staff roll (the credits), after the farewell, up to the END card";
    std::snprintf(b, sizeof b, "Chapter %d, scene %02d%s%s: %s", e.chapter + 1, e.scene, *e.route ? ", route " : "", e.route,
                  e.four ? "four bodies with 3 or 4 players" : "two bodies, as in a 2-player game");
    return b;
}

int sceneEntryOf(int chapter1, int scene)
{
    const auto& v = sceneEntries();
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i].chapter == chapter1 - 1 && v[i].scene == scene)
            return int(i);
    return -1;
}

SceneView::Snap SceneView::take(Runtime& rt)
{
    Snap s;
    s.st = std::make_shared<net::RuntimeState>();
    s.st->save(rt);
    s.sceneStarted = rt.scene_started;
    s.coop4Scene = rt.coop4_scene;
    s.starts = rt.scene_starts;
    s.frame = rt.frameCount;
    s.shootable = rt.shootable;
    return s;
}

void SceneView::put(Runtime& rt, const Snap& s)
{
    rt.sprites_ensure();   // a new game's runtime: the 2D RAMs are mapped on first use (the save-state overwrites them)
    s.st->restore(rt);
    rt.scene_started = s.sceneStarted;
    rt.coop4_scene = s.coop4Scene;
    rt.scene_starts = s.starts;
    rt.shootable = s.shootable;
    music_ = s.music;
    startCue_ = true;
    endingWas_ = false;   // (every save-state is before the ending's start)
    rt.boss_death_center = scene().scene == Runtime::kSceneBossDeath && scene().chapter == 1;   // (viewer only)
}

void SceneView::begin(Runtime& rt)
{
    error.clear();
    paused = slow = false;
    seen_ = false;
    lastArea_ = -1;
    tail_ = 0;
    ring_.clear();
    seekFrames = 0;
    // a new game is a new runtime: a save-state of the last one is not restored into it (the rollback state keeps raw
    // pointers into its own memory: Runtime::cardLatch_; a restore across games crashed in sprites_frame). The seek
    // takes under half a second; Replay within the game uses the save-state
    cache_.clear();
    cues_.clear();
    if (auto it = cache_.find(key()); it != cache_.end()) {
        put(rt, it->second);
        for (auto& g : rt.gun)
            g = {};
        startsSeen_ = rt.scene_starts;
        watchFrom = it->second.frame;
        phase = Phase::Watching;
        applyCast(rt);
        return;
    }
    startsSeen_ = rt.scene_starts;
    deathsSeen_ = scene().chapter == 3 ? rt.magician_deaths : rt.boss_deaths;
    endingWas_ = false;
    seekFrom_ = rt.frameCount;
    ring_.push_back(take(rt));
    phase = Phase::Seeking;
    applyCast(rt);   // actorswap (draws only; the host keeps it current while the viewer is open)
}

void SceneView::seek(Runtime& rt, double ms, const std::function<void(Runtime&)>& input)
{
    if (phase != Phase::Seeking)
        return;
    const auto t0 = std::chrono::steady_clock::now();
    Audio* audio = rt.audio;   // no sound while it runs ahead
    rt.audio = nullptr;
    rt.silentCues = &cues_;   // (the music from them at the scene's start)
    const int chapter = scene().chapter;
    for (;;) {
        if (rt.frameCount % 30 == 0 && ring_.back().frame != rt.frameCount) {
            ring_.push_back(take(rt));
            if (ring_.size() > 6)
                ring_.pop_front();
        }
        if (input)
            input(rt);
        rt.frame();
        ++seekFrames;
        if (started(rt)) {
            foundAt = rt.frameCount;
            // the newest save-state at least lead() frames before the start (else the oldest)
            const Snap* pick = &ring_.front();
            for (const Snap& s : ring_)
                if (s.frame + lead() <= foundAt)
                    pick = &s;
            Snap keep = *pick;
            // the music playing at the save-state: the last music code the silent seek sent before it
            for (const auto& [f, code] : cues_)
                if (f <= keep.frame && audio && audio->isMusicCode(code))
                    keep.music = code;
            cues_.clear();
            ring_.clear();
            put(rt, keep);
            watchFrom = keep.frame;
            cache_[key()] = keep;
            for (auto& g : rt.gun)
                g = {};
            phase = Phase::Watching;
            break;
        }
        const bool ending = endingScene();   // (the chapter goes 3, 4 (the clear), 6 (the ending))
        if (rt.chapterDone || rt.gameOverDone || (rt.chapterChanged >= 0 && !ending) ||
            (ending ? rt.mem.at<int8_t>(A::G::chapter) < chapter : rt.mem.at<int8_t>(A::G::chapter) != chapter) ||
            rt.frameCount - seekFrom_ > 80000) {
            char b[160];
            std::snprintf(b, sizeof b, "Scene %02d was not reached: the play went another way, frame %llu.", scene().scene,
                          (unsigned long long)rt.frameCount);
            error = b;
            failed = true;
            ring_.clear();
            phase = Phase::Off;
            break;
        }
        if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() >= ms)
            break;
    }
    rt.audio = audio;
    rt.silentCues = nullptr;
}

// after a frame: the scene started in it (cam_job18 started this scene in its chapter; the ending's scene: its first
// frame of game_ending, any event: 1 player sees event 0 / 1, player 1 / 2 alone)
bool SceneView::started(Runtime& rt)
{
    if (endingScene()) {
        const int8_t ev = rt.mem.at<int8_t>(A::G::event_index);
        const bool now = rt.mem.at<int16_t>(A::G::game_mode) == 4 && rt.mem.at<int16_t>(A::G::game_submode) == 8 &&
                         rt.mem.at<int8_t>(A::G::chapter) == 6 && (scene().scene == Runtime::kSceneCredits ? ev == 3 : ev <= 2);
        const bool edge = now && !endingWas_;
        endingWas_ = now;
        return edge;
    }
    if (scene().scene == Runtime::kSceneBossDeath) {   // the boss died (boss_deaths) in this chapter
        if ((scene().chapter == 3 ? rt.magician_deaths : rt.boss_deaths) == deathsSeen_)
            return false;
        deathsSeen_ = scene().chapter == 3 ? rt.magician_deaths : rt.boss_deaths;
        return rt.mem.at<int8_t>(A::G::chapter) == scene().chapter;
    }
    if (rt.scene_starts == startsSeen_)
        return false;
    startsSeen_ = rt.scene_starts;
    return rt.scene_started == scene().scene && rt.mem.at<int8_t>(A::G::chapter) == scene().chapter;
}

void SceneView::afterFrame(Runtime& rt)
{
    if (phase == Phase::Over && rt.boss_death_center)
        rt.boss_death_center = false;
    if (phase != Phase::Watching)
        return;
    if (!seen_ && started(rt)) {
        seen_ = true;
        sceneFrame_ = rt.frameCount;
        endEvent_ = rt.mem.at<int8_t>(A::G::event_index);
    }
    if (endingScene()) {   // the ending's scenes: over when its event ends (3: the staff roll; then the ranking)
        const uint64_t most = scene().scene == Runtime::kSceneCredits ? 7200 : 3600;   // (the farewell through the Magician's last words)
        if (seen_ && (rt.mem.at<int8_t>(A::G::event_index) != endEvent_ || rt.mem.at<int8_t>(A::G::chapter) != 6 ||
                      rt.mem.at<int16_t>(A::G::game_submode) != 8 || rt.chapterDone || rt.frameCount - sceneFrame_ > most)) {
            phase = Phase::Over;
            // rpolish (user 2026-10-26, 4-19's held last picture): the frame that ends the scene is the next event's
            // first, whose script has already loaded its texture area (the staff roll's: area 4, a sepia floor and a
            // forest sky band over the hall the walk is still drawn in). The held picture keeps the scene's own area
            // (draw-only: Runtime::textureArea loads nothing; Replay restores the save-state)
            if (lastArea_ > 0)
                rt.textureArea = lastArea_;
        } else if (seen_) {
            lastArea_ = rt.textureArea;
        } else if (rt.frameCount > watchFrom + kLead + 600) {
            phase = Phase::Over;
        }
        return;
    }
    if (scene().scene == Runtime::kSceneBossDeath) {   // a boss's defeat: its death until the chapter's end (10 s at most)
        const uint64_t most = scene().chapter == 3 ? 1400 : 600;   // (the Magician: he rises, speaks and blows up; the ending event ends it)
        if (seen_ && (rt.chapterDone || rt.mem.at<int8_t>(A::G::chapter) != scene().chapter || rt.frameCount - sceneFrame_ > most))
            phase = Phase::Over;
        else if (!seen_ && rt.frameCount > watchFrom + kLead + 600)
            phase = Phase::Over;
        return;
    }
    const auto routine = [&](int p) { return rt.mem.at<port::a::Player>(A::G::players, uint32_t(p)).scene_routine; };
    // the bodies' scene routines (player_body.cpp's, from 0x4BAB0; 0x4B820 is the idle one)
    const bool bodies = routine(0) >= 0x4BAB0 || (players >= 2 && routine(1) >= 0x4BAB0);
    const bool shots = rt.mem.u32(A::G::shot_enable) != 0;
    if (seen_) {
        const uint64_t since = rt.frameCount - sceneFrame_;
        if ((!bodies || shots) && since > 20)
            ++tail_;
        if (tail_ >= 45 || since > 2400)
            phase = Phase::Over;
    } else if (rt.frameCount > watchFrom + kLead + 600) {   // (it never came: as over)
        phase = Phase::Over;
    }
}

bool SceneView::replay(Runtime& rt)
{
    auto it = cache_.find(key());
    if (it == cache_.end())
        return false;
    put(rt, it->second);
    for (auto& g : rt.gun)
        g = {};
    startsSeen_ = rt.scene_starts;
    seen_ = false;
    lastArea_ = -1;
    tail_ = 0;
    paused = false;
    phase = Phase::Watching;
    return true;
}

// the keys' line (the window's lines and Clean View's hint): Free Cam takes W A S D Q E, so slow motion is M there.
// padpass: a pad used last (padType >= 0): its buttons, as that pad names them (keybinds.h padInputName)
// ipolish: Free Cam's pad line names the moves (left stick move, right stick look, shoulders down / up, the left
// stick's click the speed; the right stick's click Free Cam off)
static std::string keysLine(bool paused, bool slow, bool freeCam, int padType, bool fast = false, bool slowCam = false)
{
    std::string s = std::string(paused ? "PAUSED  " : "") + (slow ? "SLOW  " : "") + (freeCam && slowCam ? "SLOW CAM  " : "");
    if (padType >= 0) {
        auto n = [padType](int in) { return padInputName(in, padType); };
        if (freeCam)
            return s + "FREE CAM" + (fast ? " FAST" : "") + "  Sticks move / look  " + n(kPiL1) + " / " + n(kPiR1) + " down / up  " +
                   n(kPiL3) + " speed  " + n(kPiBack) + " slow cam  " + n(kPiR3) + " off  " + n(kPiSouth) + " pause  " + n(kPiWest) + " replay  " + n(kPiEast) + " back";
        return s + n(kPiSouth) + " pause  " + n(kPiWest) + " replay  " + n(kPiNorth) + " slow  " + n(kPiL3) + " free cam  " + n(kPiR1) +
               " clean view  D-Down subtitles  " + n(kPiL1) + " actors  " + n(kPiEast) + " back";
    }
    if (freeCam)
        return s + "FREE CAM  WASD QE move  right-drag look  wheel zoom  Shift / Ctrl speed  C slow cam  F off  Space pause  M slow  R replay  Esc back";
    return s + "Space pause  R replay  S slow  F free cam  C clean view  T subtitles  A actors  Esc back";
}

// the live toggles' line (1 / 2 / 3: Options > Video; H: the hints)
static std::string fxLine(const bool* fx)
{
    return std::string("1 Hit Sparks ") + (fx[0] ? "On" : "Off") + "  2 Muzzle Flash " + (fx[1] ? "On" : "Off") + "  3 Blood " +
           (fx[2] ? "On" : "Off") + "  H Hide hints";
}

static std::string overLine(bool freeCam, int padType)
{
    if (padType >= 0) {
        auto n = [padType](int in) { return padInputName(in, padType); };
        return "Over.  " + n(kPiWest) + " replay  " + (freeCam ? n(kPiR3) + " free cam off  " : std::string()) + n(kPiEast) + " back";
    }
    return freeCam ? "Over.  R replay  F free cam off  Esc back" : "Over.  R replay  Esc back";
}

std::vector<std::string> SceneView::lines(const Runtime& rt) const
{
    std::vector<std::string> v;
    if (hintsOff && phase != Phase::Seeking)
        return v;   // (H: no hints for recording)
    char b[200];
    std::snprintf(b, sizeof b, "CUT-SCENE %d-%02d %s  %dP %s", scene().chapter + 1, scene().scene, scene().name, players,
                  models == 1 ? "PC" : "arcade");
    v.push_back(b);
    if (phase == Phase::Seeking) {
        std::snprintf(b, sizeof b, "Fast-forward ... frame %llu  Esc back", (unsigned long long)rt.frameCount);
        v.push_back(b);
    } else if (phase == Phase::Over) {
        v.push_back(overLine(cam.on, padType));
    } else {
        v.push_back(keysLine(paused, slow, cam.on, padType, cam.fast, cam.slow));
    }
    if (phase == Phase::Watching || phase == Phase::Over)
        v.push_back(fxLine(fx));
    if (castOpen && phase != Phase::Seeking)
        for (const std::string& l : castLines())
            v.push_back(l);
    return v;
}

std::vector<std::string> SceneView::hintLines() const
{
    if (hintsOff)
        return {};
    std::vector<std::string> v = {phase == Phase::Over ? overLine(cam.on, padType)
                                                       : keysLine(paused, slow, cam.on, padType, cam.fast, cam.slow)};
    if (phase != Phase::Seeking)
        v.push_back(fxLine(fx));
    if (castOpen)
        for (const std::string& l : castLines())
            v.push_back(l);
    return v;
}

// (photomode: Free Cam's step is FreeCam::step, game/free_cam.cpp, shared with Photo Mode)

} // namespace hotd::game
