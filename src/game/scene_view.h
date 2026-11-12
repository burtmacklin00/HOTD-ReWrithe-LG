// ReWrithe's cut-scene viewer (ours, the Gallery's "Cut-scenes" page; docs/coop4.md section 14): one player cut-scene
// of the game's own scene code, watched again and again without playing up to it. The chapter starts (with a forced
// route where the scene needs one), the game runs headless (no picture, no sound, the --autofire player) until the
// scene's start (cam_job18, Game::scene_starts), then from a save-state 1.5 s before it the scene plays live; the
// save-state is kept for the game, so Replay is instant (a new Play seeks again, under 0.5 s). Nothing here
// changes a normal game (Game::scene_started / scene_starts are written by cam_job18 and only read here).
#pragma once

#include "game/free_cam.h"
#include "game/runtime.h"
#include "net/rollback.h"
#include "render/renderer.h"

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace hotd::game {

struct SceneEntry {
    int chapter;         // 0-3
    int scene;           // player_scene_routines[n] (cam_job18's n)
    const char* name;    // ours
    const char* route;   // --route's form ("1:3=1"), empty: the game's own route (the --autofire play)
    bool four;           // four bodies with 3-4 players (game/creature.cpp kSceneFour); else the two-set rule
};
const std::vector<SceneEntry>& sceneEntries();
int sceneEntryOf(int chapter1, int scene);   // --scene-view C:S -> the entry, -1 none
std::vector<std::string> sceneRowLabels();   // the Cut-scenes page's rows ("2-12 Reload kick")
std::string sceneLine(int i);                // its line under the list

class SceneView {
public:
    enum class Phase { Off, Seeking, Watching, Over };
    // the page's choices
    int entry = 0, players = 4, models = 0;   // models: 0 arcade, 1 PC
    Phase phase = Phase::Off;
    bool paused = false, slow = false;
    // Clean View (the page's choice, on by default; key C while watching): for recording clips. While watching no HUD,
    // lamps, prompts or crosshairs (Hud::clean), no mouse cursor, and the keys' line only for kHintMs after the start
    // or a key (the end's line only after a key)
    bool clean = true;
    // trailer: Subtitles (the page's choice, on by default; key T while watching): Off hides the captions (Hud::noText)
    bool subtitles = true;
    // padpass: the keys' line names a pad's buttons (the type of the pad used last: SDL_GamepadType) or, -1, the keys
    int padType = -1;
    bool noTextNow() const { return !subtitles && phase != Phase::Seeking && phase != Phase::Off; }
    // actorswap (user 2026-10-26, like Metal Gear Solid 2's demo theater): Actors, who plays each role. The roles are
    // players 1-4's bodies (Rogan's, G's, and players 3 / 4's in the 4P scenes), the actors the human bodies every
    // chapter loads (Runtime::actorSwap: draws only). cast[r]: an index of actors(), the role's own by default (r)
    struct Actor { const char* name; int type; const char* key; };   // key: --cast / launcher.ini scenecast= (actors2)
    static const std::vector<Actor>& actors();
    static constexpr int kRoles = 4;
    static const char* roleName(int r);   // "Rogan's part" ...
    int cast[kRoles] = {0, 1, 2, 3};
    void castStep(int role, int dir);     // the role's next / previous actor (wraps)
    bool castDefault() const;             // every role its own
    void castReset() { for (int r = 0; r < kRoles; ++r) cast[r] = r; }
    std::string castLabel(int role) const { return actors()[size_t(cast[role])].name; }
    // the viewer's Actors panel (key A, a pad's left shoulder, while watching): Up / Down the role, Left / Right the actor
    bool castOpen = false;
    int castRow = 0;
    std::vector<std::string> castLines() const;   // the panel's rows ("> Rogan's part: < G >")
    // Runtime::actorSwap from the cast (while the viewer is active; else every role its own)
    void applyCast(Runtime& rt) const;
    // --cast (testing): "g,rogan,p3,p4" (names or numbers of actors(), by role); false: a word not known
    bool castFrom(const std::string& s);
    // actors2: the cast as castFrom reads it ("rogan,g,p3,p4"), kept in launcher.ini scenecast=
    std::string castString() const;
    static constexpr uint64_t kHintMs = 2000, kHintFadeMs = 500;
    uint64_t hintFrom = 0;   // the hint's start (ms, SDL_GetTicks), 0: none
    void poke(uint64_t nowMs) { hintFrom = nowMs ? nowMs : 1; }
    // the hint's strength now: 1 shown, 0 gone, between: fading out over its last kHintFadeMs
    float hint(uint64_t nowMs) const
    {
        if (castOpen && phase != Phase::Seeking && phase != Phase::Off)
            return 1.0f;   // (the Actors panel stays while open)
        if (!hintFrom || nowMs >= hintFrom + kHintMs)
            return 0.0f;
        const uint64_t left = hintFrom + kHintMs - nowMs;
        return left >= kHintFadeMs ? 1.0f : float(left) / float(kHintFadeMs);
    }
    bool cleanNow() const { return clean && phase != Phase::Seeking && phase != Phase::Off; }
    // the scene's start (after begin / replay put the save-state back): the music to cue again (the seek ran silent);
    // true once per start. code: the last music code before the save-state (0: none kept)
    bool takeStart(uint32_t& code)
    {
        if (!startCue_)
            return false;
        startCue_ = false;
        code = music_;
        return true;
    }
    std::string error;   // why the last Play stopped (shown on the page), empty: none
    bool failed = false; // the seek did not find it (the window goes back to the page)
    bool active() const { return phase != Phase::Off; }
    // Watching, before the scene's start: the lead-in still gets the seek's gun input (the save-state's play goes on as
    // it did, so the scene comes again); from the start no input (a shot off the screen would skip the scene)
    bool leadIn() const { return phase == Phase::Watching && !seen_; }
    const SceneEntry& scene() const { return sceneEntries()[size_t(entry)]; }

    // after the chapter started: a kept save-state of this scene is restored (Watching), else the seek begins
    void begin(Runtime& rt);
    // Seeking: up to `ms` milliseconds of headless frames; input(rt) gives a frame's gun input (the autofire player).
    // Found: the save-state restored, Watching; not found (the chapter ended, a game over, 80000 frames): Off + error
    void seek(Runtime& rt, double ms, const std::function<void(Runtime&)>& input);
    // Watching, after each game frame: the scene's end (its bodies done and the shooting on again, or 40 s) -> Over
    void afterFrame(Runtime& rt);
    bool replay(Runtime& rt);   // the save-state again (Watching from the lead-in)
    void stop() { phase = Phase::Off; paused = slow = castOpen = false; cam.stop(); }

    // Free Cam (key F, a pad's stick click; user 2026-10-22, for trailer footage): draws only. The 3D picture is seen
    // from a camera of our own (game/free_cam.h, GameView::camOverride; photomode: shared with Photo Mode) that starts
    // where the scene's camera is; the game, its camera and the scene go on underneath (paused: a photo mode). Off: the
    // scene's camera again at once. cam.fast: a pad's left-stick click (x4); cam.slow: C / Back (1/4 speed, eased)
    FreeCam cam;
    bool hintsOff = false;   // H: the key hints hidden (recording; kept for the session)
    bool fx[3] = {true, true, true};   // Hit Sparks / Gun Muzzle Flash / Blood Particles (main.cpp sets them from the options)
    void freeCamToggle(const Camera& sceneCam) { cam.toggle(sceneCam); }
    const Camera* freeCamNow() const { return cam.on && (phase == Phase::Watching || phase == Phase::Over) ? &cam.camera : nullptr; }
    std::vector<std::string> lines(const Runtime& rt) const;   // the window's text over the picture
    std::vector<std::string> hintLines() const;                // Clean View's small hint (the keys)

    uint64_t foundAt = 0, watchFrom = 0;   // the scene's start frame and the save-state's (testing)
    uint64_t seekFrames = 0;               // frames run by the last seek
    static constexpr int kLead = 90;       // the save-state: this many frames (1.5 s) before the scene's start
    // the ending's scene: a short lead-in (its 1.5 s were the chapter's last sky shot; save-states every 30 frames: 20-49 frames)
    int lead() const { return endingScene() ? 20 : kLead; }
    // the ending's scenes (the farewell, the staff roll): found by the ending's event, not by cam_job18
    bool endingScene() const { return scene().scene == Runtime::kSceneEnding || scene().scene == Runtime::kSceneCredits; }

private:
    struct Snap {
        std::shared_ptr<net::RuntimeState> st;
        int sceneStarted = -1, coop4Scene = -1;
        uint32_t starts = 0;
        uint64_t frame = 0;
        std::vector<uint32_t> shootable;   // the last frame's (the autofire aims from it; not in the rollback state)
        uint32_t music = 0;                // the last music code before it (the seek's silent cues), 0 none
    };
    static Snap take(Runtime& rt);
    void put(Runtime& rt, const Snap& s);
    std::vector<std::pair<uint64_t, uint32_t>> cues_;   // the seek's silent sound codes (Runtime::silentCues)
    bool startCue_ = false;
    uint32_t music_ = 0;
    std::tuple<int, int, int> key() const { return {entry, players, models}; }
    std::map<std::tuple<int, int, int>, Snap> cache_;
    std::deque<Snap> ring_;   // the seek's save-states, every 30 frames (the last few)
    uint32_t startsSeen_ = 0;
    uint32_t deathsSeen_ = 0;   // (kSceneBossDeath: the boss deaths seen)
    uint64_t seekFrom_ = 0;
    bool seen_ = false;       // Watching: the scene has started
    int tail_ = 0;            // frames since its bodies were done
    uint64_t sceneFrame_ = 0; // the frame it started
    bool started(Runtime& rt);   // after a frame: this scene started in it
    int lastArea_ = -1;       // the ending's scenes: the texture area of the scene's last watched frame (afterFrame)
    bool endingWas_ = false;  // the ending's scene (Runtime::kSceneEnding): the last frame was in it
    int endEvent_ = -1;       // its event (2: both players; 0 / 1: one alone)
};

} // namespace hotd::game
