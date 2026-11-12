// hotd_viewer: free-camera viewer for the chapter worlds, PC and arcade sources behind the asset interface.
//
//   hotd_viewer [--root <repo>] [--pc <PC install>] [--regions <arcade regions>] [--dumps <MAME dumps>] [--texsets <file>]
//               [--chapter 1-4] [--source pc|arcade] [--cam x,y,z,yaw,pitch (degrees)] [--size WxH]
//               [--windowed]   not the borderless fullscreen start (F11 / Alt+Enter toggle it; batch and selftest runs are hidden)
//               [--fps N]   frame cap (default 60; 0 = off, vsync only). Vsync is always requested.
//               [--dump-colors]    polygon palette and color translation from the MAME dumps instead of the ROM
//               [--shots <file> --out <folder>]      batch mode: hidden window, one PNG per line of <file>, then exit
//
// Shot file lines: <name> <chapter 1-4> <pc|arcade> <x> <y> <z> <yaw deg> <pitch deg> [<hex id>=<pc|arcade> ...]
//                  [cat:<world|zombies|bosses|effects>=<pc|arcade> ...] [nolight] [nofilter] [nogamma] [fixedsets] [zones] [nosky] [nocreatures]
//                  [noanim] the recorded frame, nothing hidden   [nocull] no Model 2 back-face culling   [nomip] no mip levels
//                  [norows] the areas' own colour rows (not the ones the scripts swap in, X)
//                  [t:<seconds>]  animation time: creatures from their recorded frame, palette animation on (default: off)
//
// Keys: WASD move, Q/E down/up, Shift faster, right mouse button (hold) or M (toggle) = mouse look,
//       Tab = whole game PC <-> arcade, F1-F5 = toggle the source of one category (world, zombies, bosses, weapons/effects,
//       humans),
//       P = toggle the source of the model under the screen center, R = clear per-model choices,
//       1-4 or PageUp/PageDown = chapter, L = lighting, F = arcade texture filtering, G = arcade gamma curve,
//       V = arcade texture sets by camera position (default) or each model's own set, K = sky on/off,
//       O = creatures on/off, N = next recorded creature view (camera of a replay frame with creatures),
//       Y = ride the Chapter 1 rail (camera paths from CAMALL.RBH, formats.md §26), again = free camera,
//       J = animation on/off (creatures and the arcade's palette animation), U / Shift+U = next / previous motion of the creature at the screen center,
//       H = controls overlay on/off, C = print camera,
//       F12 = screenshot to <root>\private\screenshots, Esc = quit.

#include "assets/arcade_anim.h"
#include "assets/creature_pose.h"
#include "util/fullscreen.h"
#include "assets/arcade_categories.h"
#include "assets/arcade_loader.h"
#include "assets/arcade_palette_fx.h"
#include "assets/camera_paths.h"
#include "game/event_script.h"
#include "game/files_check.h"
#include "assets/pc_files.h"
#include "assets/pc_loader.h"
#include "render/gl.h"
#include "render/overlay.h"
#include "render/renderer.h"
#include "util/png.h"
#include "util/window_icon.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <memory>
#include <sstream>
#include <string>

using namespace hotd;

namespace {

constexpr float kDeg = 3.14159265f / 180.0f;
constexpr float kShadowLift = 0.05f;   // units above the floor for the arcade's shadow models

struct Preset { float x, y, z, yawDeg, pitchDeg; };
// Starting cameras: the arcade camera at the start of each chapter's gameplay (MAME work RAM 0x51EA80, fullgame_us dumps).
const Preset kChapterStart[4] = {
    {-39.69f, 4.59f, -426.06f, 6.46f, -0.09f},
    {-8.37f, -3.34f, 0.15f, -89.68f, 8.93f},
    {64.22f, 8.25f, -2.81f, -90.01f, 0.0f},
    {8.48f, 15.0f, -52.70f, -4.13f, 0.0f},
};

struct App {
    std::unique_ptr<AssetSource> sources[kSourceCount];
    std::map<std::pair<int, int>, std::unique_ptr<StageData>> cache;   // (source, chapter) -> data (null = failed)
    std::map<std::pair<int, int>, std::string> errors;
    Renderer renderer;
    SourceSelection sel;
    RenderOptions opt;
    Camera cam;
    int chapter = 0;
    bool zoneSets = true;              // arcade texture set by camera position (as the game loads them); false = each model's own set
    bool sky = true;                   // arcade sky backdrop (K)
    std::vector<Placement> skyPlacements;
    Source lastSkySource = Source(255);   // for the log   // per frame: sky models centered on the camera
    // Posed creatures (tools/creature_poses.py): parts with world transforms, per chapter; drawn from the source their category
    // (or a per-model choice) selects. Arcade parts use the texture set of the area they were recorded in.
    struct Pose { int set; Category cat; bool shadow; Placement pl; int creature = -1; bool driven = false; bool hidden = false; };
    // driven: moved by the creature animation. While animating, recorded effects and flying debris that aren't (isDebris)
    // are hidden: frozen in mid-air they look like floating pieces. Props (corpses, doors) stay where they were recorded.
    std::vector<Pose> poses[4];
    struct View { std::string tag; float pos[3], yaw, pitch; };
    std::vector<View> views[4];
    size_t nextView = 0;
    bool creatures = true;             // O
    bool creatureLight = true;         // the creatures in the game's creature light (shot token nocreaturelight: the set's)
    bool scriptRows = true;            // X: the arcade colour rows the scripts swap in (arcadeAreaColorRows), or the areas' own
    std::map<std::pair<int, int>, std::string> xlatRows;   // (chapter, texture set) -> the rows in its translation now
    // Creature animation (formats.md §23): each recorded creature is matched to its skeleton (by part IDs) and to the motion
    // frame its joints show; it then plays that motion in place from there. Shadows follow their part, flat on the floor.
    CreatureAnimations anim;
    CreatureCategories categories;     // character category per object ID (formats.md §25)
    struct Animated {
        int type = 0, motion = 0, creature = -1;
        float frame = 1;                   // 1-based, fractional while playing
        int start = 1, startMotion = 0;    // the matched motion frame (shots start from it)
        int motion2 = -1, start2 = 1;      // second motion for the `second` nodes (formats.md §23), -1 = none
        float frame2 = 1;
        Xform root;                        // world from creature space
        std::vector<int> nodePose;         // skeleton node -> index into poses[chapter] (-1: not drawn)
        // recorded parts drawn with a node's matrix (times a fixed offset) but under another ID than the skeleton's: damage
        // variants, heads, wings, fangs (the game draws some of them itself)
        struct Attached { int pose, node; Xform offset; };
        std::vector<Attached> attached;
        struct Shadow { int pose, part; float floorY; };   // part: index into poses[chapter] of the part it follows
        std::vector<Shadow> shadows;
        // generated skin (the bellies, decompilation.md "World drawing"): drawn with node B, corners follow node A
        struct SkinPart { int pose; ArcadeSource::Skin skin; };
        std::vector<SkinPart> skins;   // the type's skin, Sophie's strip
    };
    static constexpr uint32_t kSkinKey = 0xF0000000;   // + chapter x 0x1000 + creature index x 2 + which: one model each
    std::vector<Animated> animated[4];
    std::set<int> animatedGroups[4];   // recorded creature groups with an animated creature, or under 4 parts (blood chunks)
    // Parts the game draws as a cycle of numbered models (decompilation.md "Part model swaps"): the Magician's veins
    // (type 76, parts 3 / 4 / 11 / 12) draw base + (vblank / 2) % 25; the recording holds one member (17 = base + 8).
    struct ModelCycle { uint32_t base; int count, recorded, frames; };   // frames: vblanks per model
    static constexpr ModelCycle kModelCycles[] = {
        {0x1C2A, 25, 8, 2},   // boss4_kls01..49
        {0x1C75, 25, 8, 2},   // boss4_urs01..49
        {0x1C47, 25, 8, 2},   // boss4_mls01..49
        {0x1C07, 25, 8, 2},   // boss4_hrs01..49
    };
    struct Cycled { int pose; const ModelCycle* cycle; };
    std::vector<Cycled> cycled[4];
    bool animate = true;               // J
    // Camera rail (Y): the PC's CAMALL.RBH paths in the order the arcade plays them (from the replay; stops not emulated yet)
    CameraPaths rails;
    bool riding = false;
    size_t railStep = 0;
    float railFrame = 0;
    static constexpr int kChapter1Rail[] = {0, 2, 4, 5, 6};
    // Script ride (Y in Chapter 1 when EVTALL.RBH is there): the event scripts drive the rail and spawn the creatures
    EventScripts scripts;
    EventRunner runner;
    bool scriptRide = false;
    struct Spawn { size_t animated; std::vector<int> poses; float age = 0; bool dead = false; };
    std::vector<Spawn> spawns;
    static constexpr float kSpawnLife = 5.0f;   // seconds until a spawned enemy counts as dead (no shooting yet)
    static constexpr int kChapter1Events[] = {0, 1, 3, 4, 5, 6};   // the replay's branch (event 2 = the other one)

    // request every model the Chapter 1 spawns will need (a dry run of the scripts) before the stage loads
    void prepareScripts()
    {
        EventRunner dry;
        dry.start(&scripts, 0, std::vector<int>(std::begin(kChapter1Events), std::end(kChapter1Events)));
        std::set<int> types;
        for (int i = 0; i < 200000 && dry.running(); ++i) {
            dry.step(0);
            for (const SpawnRecord& r : dry.takeSpawns()) types.insert(r.type);
        }
        std::vector<uint32_t> ids;
        for (int t : types)
            for (const SkeletonNode& n : anim.skeleton(t).nodes)
                if (n.model) ids.push_back(n.model);
        for (int t : types)
            for (uint32_t id : partDrawExtraModels(t)) ids.push_back(id);
        for (auto& src : sources) src->requestModels(0, ids);
        SDL_Log("event scripts: Chapter 1 spawns %zu creature types (%zu models)", types.size(), ids.size());
    }

    void spawn(const SpawnRecord& r)
    {
        const Skeleton& sk = anim.skeleton(r.type);
        if (sk.nodes.empty()) return;
        Animated a;
        a.type = r.type;
        a.creature = 100000 + int(spawns.size());
        // the motion each type plays most in the Chapter 1 replay (object +0xD0, t00540-t01380) until the behaviour code
        // that picks motions is decoded; otherwise the first motion of its pool
        static const std::map<int, int> kReplayMotion = {{0, 106}, {2, 461}, {3, 509}, {6, 312}, {7, 179}, {8, 106},
            {11, 560}, {13, 461}, {16, 566}, {17, 211}, {19, 211}, {20, 255}, {21, 374}, {28, 564}, {29, 560}, {34, 220},
            {41, 646}, {42, 644}, {43, 647}, {55, 22}};
        a.motion = -1;
        if (auto it = kReplayMotion.find(r.type); it != kReplayMotion.end() && anim.motionJoints(it->second) == sk.joints)
            a.motion = it->second;
        for (int m = 0; m < anim.motionCount() && a.motion < 0; ++m)
            if (anim.motionJoints(m) == sk.joints) a.motion = m;
        if (a.motion < 0) return;
        a.start = 1;
        a.startMotion = a.motion;
        // object transform T(pos) * R(0, facing, 0) (formats.md §23: R(g1, g2, g3) = Rz(-g1) Ry(-g2) Rx(-g3))
        const float c = std::cos(-r.facing), sn = std::sin(-r.facing);
        a.root.rot[0] = c; a.root.rot[2] = sn; a.root.rot[6] = -sn; a.root.rot[8] = c;
        std::copy(r.pos, r.pos + 3, a.root.pos);
        a.nodePose.assign(sk.nodes.size(), -1);
        Spawn sp;
        for (size_t n = 0; n < sk.nodes.size(); ++n) {
            if (!sk.nodes[n].model) continue;
            Pose p{0, categories.of(sk.nodes[n].model).value_or(Category::Zombies), false, {}};
            p.pl.id = p.pl.modelKey = sk.nodes[n].model;
            p.creature = a.creature;
            p.driven = true;
            a.nodePose[n] = int(poses[0].size());
            sp.poses.push_back(int(poses[0].size()));
            poses[0].push_back(p);
        }
        sp.animated = animated[0].size();
        animated[0].push_back(std::move(a));
        spawns.push_back(sp);
        SDL_Log("spawn type %d %s at %.1f %.1f %.1f (event %d, path %d frame %.0f)", r.type, CreatureCategories::typeName(r.type),
                r.pos[0], r.pos[1], r.pos[2], runner.event(), runner.path(), runner.frame());
    }

    // one game frame of the script ride
    void stepScripts()
    {
        int alive = 0;
        for (Spawn& sp : spawns) {
            if (sp.dead) continue;
            sp.age += 1.0f / 60.0f;
            if (sp.age > kSpawnLife) {
                sp.dead = true;
                for (int pi : sp.poses) poses[0][size_t(pi)].hidden = true;
            } else {
                ++alive;
            }
        }
        runner.step(alive);
        for (const SpawnRecord& r : runner.takeSpawns()) spawn(r);
    }
    ArcadePaletteFx paletteFx;         // arcade palette animation (formats.md §24), from the game frame below
    double gameFrame = 0;              // ~60 per second while animating
    uint16_t paletteAnim[1024] = {};
    std::vector<Xform> scratch;
    std::vector<uint32_t> hookModels;
    // Alternate-state placements (tools/arcade_states.py, formats.md §27): per chapter, groups of objects at one position;
    // the member the replay drew nearest to the camera is shown, the others are hidden.
    struct StateGroup { std::vector<uint32_t> ids; std::map<uint32_t, std::vector<std::array<float, 3>>> seen; };
    std::vector<StateGroup> stateGroups[4];

    void loadStates(const std::string& path)
    {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string kind, id;
            int ch;
            if (!(ss >> kind >> ch) || ch < 0 || ch > 3) continue;
            if (kind == "group") {
                StateGroup g;
                while (ss >> id) g.ids.push_back(uint32_t(std::stoul(id, nullptr, 16)));
                stateGroups[ch].push_back(std::move(g));
            } else if (kind == "seen" && !stateGroups[ch].empty() && (ss >> id)) {
                std::array<float, 3> p{};
                if (ss >> p[0] >> p[1] >> p[2]) stateGroups[ch].back().seen[uint32_t(std::stoul(id, nullptr, 16))].push_back(p);
            }
        }
        size_t n = 0;
        for (auto& g : stateGroups) n += g.size();
        SDL_Log("alternate-state groups: %zu from %s", n, path.c_str());
    }

    // Event objects the game positions itself (tools/arcade_props.py): doors (the mansion entrance 0x73A /
    // 0x73B, ...) and the Chapter 3 gates / gratings / elevators; per object the transform of the replay sighting nearest to the camera, shown within kPropRange.
    struct Prop { int set; Placement pl; std::vector<std::array<float, 3>> at; };
    std::map<uint32_t, std::vector<Prop>> props[4];
    std::vector<Placement> propPlacements;   // per frame
    static constexpr float kPropRange = 80.0f;

    void loadProps(const std::string& path)
    {
        std::ifstream f(path);
        std::string line;
        Prop* last = nullptr;
        size_t n = 0;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string kind;
            ss >> kind;
            if (kind == "prop") {
                int st, set;
                std::string id;
                Prop p{};
                if (!(ss >> st >> set >> id) || st < 0 || st > 3) { last = nullptr; continue; }
                p.set = set;
                p.pl.id = p.pl.modelKey = uint32_t(std::stoul(id, nullptr, 16));
                for (float& r : p.pl.rot) ss >> r;
                ss >> p.pl.pos[0] >> p.pl.pos[1] >> p.pl.pos[2];
                const std::string name = categories.name(p.pl.id);
                // doors, plus the Chapter 3 mechanisms: gates, floor gratings, elevator floors (r3_05a_gate31a,
                // r3_05a_ami01a, r3_06a_el01a); like the doors they show the state of the nearest replay sighting
                const bool r3 = name.rfind("r3_", 0) == 0;
                const bool door = name.find("door") != std::string::npos || name.rfind("to0", 0) == 0 ||
                                  (r3 && (name.find("_gate") != std::string::npos || name.find("_ami") != std::string::npos ||
                                          name.find("_el") != std::string::npos));
                if (!ss || !door) { last = nullptr; continue; }
                auto& v = props[st][p.pl.id];
                v.push_back(p);
                last = &v.back();
                ++n;
            } else if (kind == "at" && last) {
                std::array<float, 3> a{};
                if (ss >> a[0] >> a[1] >> a[2]) last->at.push_back(a);
            }
        }
        for (int s = 0; s < 4; ++s) {
            std::vector<uint32_t> ids;
            for (const auto& [id, v] : props[s]) ids.push_back(id);
            for (auto& src : sources) src->requestModels(s, ids);   // adds to the creature parts requested before
        }
        SDL_Log("event objects (doors, Chapter 3 mechanisms): %zu transforms from %s", n, path.c_str());
    }

    // Arcade draw zones (docs/decompilation.md "World drawing"): the event scripts set the zone (BGTB) before each camera
    // segment; the game then draws only that zone's placements. Free camera: the zone of the nearest rail point.
    // Off by default (Z): the free-camera zone / texture area pick is wrong in places (TODO "Viewer regression").
    // When on, it also picks the texture set from the TEXCHG area instead of the replay-derived zone points.
    bool zoneCull = false;
    static constexpr float kZoneTurn = 150.0f;   // distance a half turn away from a rail point's view counts as
    struct ZoneSample { float pos[3], dir[3]; int zone, area; };   // rail point, view direction (unit, xz), zone
    std::vector<ZoneSample> zoneSamples[4];

    int lastArea = -1;   // texture area of the sample currentZone() picked

    int currentZone()
    {
        if (scriptRide && runner.running() && runner.zone() >= 0) { lastArea = runner.area(); return runner.zone(); }
        auto& samples = zoneSamples[chapter];
        if (samples.empty() && rails.count() > 0 && scripts.eventCount(chapter) > 0) {
            for (const ZoneSpan& z : scripts.zoneSpans(chapter))
                for (int f = z.start; f <= z.end && z.path >= 0 && z.path < rails.count(); f += 5) {
                    float eye[3], target[3];
                    rails.evaluate(z.path, float(f), eye, target);
                    ZoneSample zs{{eye[0], eye[1], eye[2]}, {target[0] - eye[0], 0, target[2] - eye[2]}, z.zone, z.area};
                    const float l = std::hypot(zs.dir[0], zs.dir[2]);
                    if (l > 0) { zs.dir[0] /= l; zs.dir[2] /= l; }
                    samples.push_back(zs);
                }
            SDL_Log("chapter %d: %zu draw-zone points from the event scripts", chapter + 1, samples.size());
        }
        // nearest rail point, counting a different view direction as distance (rails pass the same place facing elsewhere,
        // with another zone); the camera's forward vector as in Renderer (yaw 0 = +z)
        const float fx = std::sin(cam.yaw), fz = std::cos(cam.yaw);
        int best = -1;
        float bestD = 1e30f;
        for (const ZoneSample& zs : samples) {
            const float d = std::hypot(zs.pos[0] - cam.pos[0], zs.pos[1] - cam.pos[1], zs.pos[2] - cam.pos[2]) +
                            kZoneTurn * (1 - (fx * zs.dir[0] + fz * zs.dir[2]));
            if (d < bestD) { bestD = d; best = zs.zone; lastArea = zs.area; }
        }
        return best;
    }

    // hidden alternate states for the current camera position
    std::set<uint32_t> hiddenStates()
    {
        std::set<uint32_t> hide;
        for (const StateGroup& g : stateGroups[chapter]) {
            uint32_t best = 0;
            float bestD = 1e30f;
            for (uint32_t id : g.ids) {
                auto it = g.seen.find(id);
                if (it == g.seen.end()) continue;
                for (const auto& p : it->second) {
                    const float d = std::hypot(p[0] - cam.pos[0], p[1] - cam.pos[1], p[2] - cam.pos[2]);
                    if (d < bestD) { bestD = d; best = id; }
                }
            }
            for (uint32_t id : g.ids) if (id != best) hide.insert(id);
        }
        return hide;
    }
    bool help = true;                  // H: controls overlay

    std::vector<std::string> helpLines() const
    {
        auto onoff = [](bool b) { return b ? "on" : "off"; };
        std::vector<std::string> l = {
            "CONTROLS                                   (H hides this)",
            "WASD move   Q/E down/up   Shift fast",
            "Right mouse (hold) or M: mouse look",
            "Tab   everything PC <-> arcade",
            "F1-F5 world / zombies / bosses / effects / humans source",
            "P     source of the model / creature at the screen center",
            "R     clear per-model choices",
            "1-4, PgUp/PgDn  chapter",
            "N     next recorded creature view",
            std::string("O     creatures (") + onoff(creatures) + ")    K  sky (" + onoff(sky) + ")",
            std::string("J     animation (") + onoff(animate) + ")   U / Shift+U  creature motion",
            std::string("Y     ride Chapter 1 (event scripts: rail, stops, spawns) (") + onoff(riding) + ")",
            std::string("L     lighting (") + onoff(opt.lighting) + ")   G  arcade gamma (" + onoff(opt.gamma) + ")",
            std::string("F     arcade filtering (") + onoff(opt.filter) + ")",
            std::string("V     arcade texture sets: ") + (zoneSets ? "by position" : "per model"),
            std::string("Z     arcade draw zones + texture areas from the ROM, unverified (") + onoff(zoneCull) + ")",
            std::string("X     arcade color rows as in the game (") + onoff(scriptRows) + ")",
            "C     print camera   F12 screenshot   Esc quit",
        };
        return l;
    }

    void loadPoses(const std::string& path)
    {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string kind, cat, id;
            int stage, set;
            ss >> kind;
            if (kind == "pose" && (ss >> stage >> set >> cat >> id) && stage >= 0 && stage < 4) {
                const bool shadow = cat.size() > 7 && cat.compare(cat.size() - 7, 7, "-shadow") == 0;
                if (shadow) cat.resize(cat.size() - 7);
                Pose p{set, cat == "bosses" ? Category::Bosses : cat == "effects" ? Category::Effects : Category::Zombies, shadow, {}};
                p.pl.id = p.pl.modelKey = uint32_t(std::stoul(id, nullptr, 16));
                for (float& r : p.pl.rot) ss >> r;
                ss >> p.pl.pos[0] >> p.pl.pos[1] >> p.pl.pos[2];
                if (!ss) continue;
                if (!(ss >> p.creature)) p.creature = -1;
                // shadows lie exactly on the floor: Model 2 sorts polygons (no depth buffer), here they would z-fight
                if (shadow) p.pl.pos[1] += kShadowLift;
                poses[stage].push_back(p);
            } else if (kind == "view" && (ss >> stage) && stage >= 0 && stage < 4) {
                View v{};
                if (ss >> v.tag >> v.pos[0] >> v.pos[1] >> v.pos[2] >> v.yaw >> v.pitch) views[stage].push_back(v);
            }
        }
        size_t n = 0;
        for (int s = 0; s < 4; ++s) {
            std::vector<uint32_t> ids;
            cycled[s].clear();
            for (int i = 0; i < int(poses[s].size()); ++i) {
                const uint32_t id = poses[s][i].pl.modelKey;
                ids.push_back(id);
                for (const ModelCycle& mc : kModelCycles)
                    if (id == mc.base + mc.recorded) {
                        cycled[s].push_back({i, &mc});
                        for (int k = 0; k < mc.count; ++k) ids.push_back(mc.base + k);
                    }
            }
            for (auto& src : sources) src->requestModels(s, ids);
            n += poses[s].size();
        }
        if (n) SDL_Log("creature poses: %zu parts from %s", n, path.c_str());
    }

    // Match every recorded creature to a skeleton and a motion frame (see Animated).
    void setupAnimation()
    {
        // categories from the creature type table (the replay-based guesses of creature_poses.py were often wrong)
        categories.build(anim);
        for (int ch = 0; ch < 4; ++ch)
            for (Pose& p : poses[ch])
                if (auto c = categories.of(p.pl.id)) p.cat = *c;
        size_t total = 0, exact = 0;
        for (int ch = 0; ch < 4; ++ch) {
            animated[ch].clear();
            animatedGroups[ch].clear();
            std::map<int, std::vector<int>> groups;   // creature -> pose indices (parts, no shadows)
            for (int i = 0; i < int(poses[ch].size()); ++i)
                if (poses[ch][i].creature >= 0 && !poses[ch][i].shadow) groups[poses[ch][i].creature].push_back(i);
            for (auto& [creature, left] : groups) {
                if (left.size() < 4) animatedGroups[ch].insert(creature);
                // a group may hold two creatures standing close together: match skeletons until too few parts are left
                while (left.size() >= 4) {
                    int bestType = -1, bestCount = 0;
                    for (int t = 0; t < anim.typeCount(); ++t) {
                        int n = 0;
                        for (const SkeletonNode& nd : anim.skeleton(t).nodes)
                            if (nd.model && std::any_of(left.begin(), left.end(), [&](int i) { return poses[ch][i].pl.id == nd.model; })) ++n;
                        if (n > bestCount) { bestCount = n; bestType = t; }
                    }
                    if (bestCount < 4) break;
                    const Skeleton& sk = anim.skeleton(bestType);
                    Animated a;
                    a.type = bestType;
                    a.creature = creature;
                    a.nodePose.assign(sk.nodes.size(), -1);
                    for (size_t n = 0; n < sk.nodes.size(); ++n) {
                        if (!sk.nodes[n].model) continue;
                        auto it = std::find_if(left.begin(), left.end(), [&](int i) { return poses[ch][i].pl.id == sk.nodes[n].model; });
                        if (it != left.end()) { a.nodePose[n] = *it; left.erase(it); }
                    }
                    // parent -> child rotations seen in the recording (independent of where the creature stands)
                    struct Pair { int joint; float rel[9]; bool second; };
                    std::vector<Pair> pairs;
                    for (size_t n = 0; n < sk.nodes.size(); ++n) {
                        const int p = sk.nodes[n].parent;
                        if (p < 0 || a.nodePose[n] < 0 || a.nodePose[p] < 0) continue;
                        const float* rp = poses[ch][a.nodePose[p]].pl.rot;
                        const float* rc = poses[ch][a.nodePose[n]].pl.rot;
                        Pair pr{sk.nodes[n].joint, {}, sk.nodes[n].second};
                        for (int i = 0; i < 3; ++i)
                            for (int j = 0; j < 3; ++j) pr.rel[i * 3 + j] = rp[i] * rc[j] + rp[3 + i] * rc[3 + j] + rp[6 + i] * rc[6 + j];
                        pairs.push_back(pr);
                    }
                    // best motion frame for a set of pairs: (hits, error sum, motion, frame)
                    auto search = [&](bool second, int& bm, int& bf) {
                        int bestHits = -1;
                        float bestErr = 1e30f;
                        for (int m = 0; m < anim.motionCount(); ++m) {
                            if (anim.motionJoints(m) != sk.joints) continue;
                            for (int f = 1; f <= anim.motionFrames(m); ++f) {
                                int hits = 0;
                                float sum = 0, r[9];
                                for (const Pair& pr : pairs) {
                                    if (pr.second != second) continue;
                                    anim.jointRotation(m, f, pr.joint, r);
                                    float e = 0;
                                    for (int k = 0; k < 9; ++k) e = std::max(e, std::fabs(r[k] - pr.rel[k]));
                                    hits += e < 0.02f;
                                    sum += e;
                                }
                                if (hits > bestHits || (hits == bestHits && sum < bestErr)) { bestHits = hits; bestErr = sum; bm = m; bf = f; }
                            }
                        }
                        return bestHits;
                    };
                    const int nPrimary = int(std::count_if(pairs.begin(), pairs.end(), [](const Pair& p) { return !p.second; }));
                    int m1 = 0, f1 = 1;
                    const int bestHits = search(false, m1, f1);
                    if (bestHits < 0) continue;   // no motion for this skeleton (its parts are already taken out of `left`)
                    a.motion = m1;
                    a.frame = float(f1);
                    // the `second` nodes: if they don't fit the main frame, they play a second motion (game: +0xFE / +0x110)
                    int fit2 = 0, n2 = int(pairs.size()) - nPrimary;
                    for (const Pair& pr : pairs)
                        if (pr.second) {
                            float r[9], e = 0;
                            anim.jointRotation(m1, f1, pr.joint, r);
                            for (int k = 0; k < 9; ++k) e = std::max(e, std::fabs(r[k] - pr.rel[k]));
                            fit2 += e < 0.02f;
                        }
                    if (n2 > 0 && fit2 < n2) {
                        int m2 = 0, f2 = 1;
                        if (search(true, m2, f2) > fit2) { a.motion2 = m2; a.start2 = f2; a.frame2 = float(f2); }
                    }
                    exact += bestHits == nPrimary;
                    ++total;
                    // root: world = root * node, from the node nearest the root that the recording has
                    anim.pose(a.type, a.motion, int(a.frame), scratch, a.motion2, a.start2);
                    for (size_t n = 0; n < sk.nodes.size(); ++n)
                        if (a.nodePose[n] >= 0) {
                            a.root = fromPlacement(poses[ch][a.nodePose[n]].pl) * inverse(scratch[n]);
                            break;
                        }
                    for (int pi : a.nodePose) if (pi >= 0) poses[ch][pi].driven = true;
                    animatedGroups[ch].insert(creature);
                    a.start = int(a.frame);
                    a.startMotion = a.motion;
                    SDL_Log("chapter %d creature %d: type %d %s %s (%s), motion %d %s frame %d, second motion %d frame %d", ch + 1,
                            creature, a.type, anim.bodyName(a.type).c_str(), CreatureCategories::typeName(a.type),
                            categoryName(CreatureCategories::ofType(a.type)), a.motion, anim.motionName(a.motion).c_str(), a.start,
                            a.motion2, a.start2);
                    animated[ch].push_back(std::move(a));
                }
            }
            attachParts(ch);
            addSkins(ch);
            std::vector<uint32_t> extra;   // the models body_part_draw_hook puts in a part's place (updateAnimation)
            for (const Animated& a : animated[ch])
                for (uint32_t id : partDrawExtraModels(a.type)) extra.push_back(id);
            for (auto& src : sources) src->requestModels(ch, extra);
        }
        SDL_Log("creature animation: %zu creatures matched to a motion frame (%zu exactly)", total, exact);
    }

    // Recorded parts that no skeleton node took (other IDs on a node: damage variants, heads; parts the game draws itself:
    // wings, fangs; recorded as "effects" when small or drawn often) are attached rigidly to the node of an animated
    // creature whose world rotation they share (blood chunks don't). Then shadows are matched to every driven part.
    // flying debris recorded mid-air (hidden while animating): flesh chunks, bullet sparks, splashes
    bool isDebris(uint32_t id) const
    {
        const std::string n = categories.name(id);
        for (const char* pre : {"nikubaan", "tamahi", "gee", "powa", "picha"})
            if (n.rfind(pre, 0) == 0) return true;
        return false;
    }

    static std::string damageBase(const std::string& name)
    {
        const size_t d = name.rfind("_dam");
        return d != std::string::npos && d + 6 <= name.size() && std::isdigit(uint8_t(name[d + 4])) ? name.substr(0, d) : name;
    }

    void attachParts(int ch)
    {
        std::vector<std::vector<Xform>> world(animated[ch].size());
        for (size_t c = 0; c < animated[ch].size(); ++c) {
            const Animated& a = animated[ch][c];
            anim.pose(a.type, a.motion, a.start, scratch, a.motion2, a.start2);
            for (const Xform& x : scratch) world[c].push_back(a.root * x);
        }
        size_t n = 0;
        for (int i = 0; i < int(poses[ch].size()); ++i) {
            Pose& p = poses[ch][i];
            if (p.shadow || p.driven) continue;
            const Xform w = fromPlacement(p.pl);
            // 1. by name: damage variants of a node's model ("zonbimana_mune_dam03a" for "..._dam00a") take its matrix
            const std::string base = damageBase(categories.name(p.pl.id));
            bool named = false;
            // ungrouped creature parts (recorded as "effects" when several creatures share the ID, e.g. bellies): the
            // nearest matching node of any creature in the chapter
            const auto pc = categories.of(p.pl.id);
            const bool creaturePart = pc && *pc != Category::World && *pc != Category::Effects;
            {
                int bc = -1, bk = -1;
                float bd = 30.0f;
                for (size_t c = 0; c < animated[ch].size() && !base.empty(); ++c) {
                    if (p.creature >= 0 ? animated[ch][c].creature != p.creature : !creaturePart) continue;
                    const Skeleton& sk = anim.skeleton(animated[ch][c].type);
                    for (size_t k = 0; k < sk.nodes.size(); ++k)
                        if (sk.nodes[k].model && damageBase(categories.name(sk.nodes[k].model)) == base &&
                            animated[ch][c].nodePose[k] != i) {
                            const Xform& nw = world[c][k];
                            const float dd = std::hypot(nw.pos[0] - w.pos[0], nw.pos[1] - w.pos[1], nw.pos[2] - w.pos[2]);
                            if (dd < bd) { bd = dd; bc = int(c); bk = int(k); }
                        }
                }
                if (bc >= 0) { animated[ch][bc].attached.push_back({i, bk, Xform{}}); named = true; }
            }
            if (named) {
                SDL_Log("  attach %x %s by name", p.pl.id, categories.name(p.pl.id).c_str());
                p.driven = true; ++n; continue;
            }
            // 2. by matrix: same world rotation as a node, near it
            int bestC = -1, bestN = -1;
            float bestD = 25.0f;   // units: a part next to its bone
            for (size_t c = 0; c < animated[ch].size(); ++c) {
                // a group's leftovers go to that group's creatures only; ungrouped parts ("effects") to any creature
                if (p.creature >= 0 && animated[ch][c].creature != p.creature) continue;
                for (size_t k = 0; k < world[c].size(); ++k) {
                    const Xform& nw = world[c][k];
                    float er = 0;
                    for (int j = 0; j < 9; ++j) er = std::max(er, std::fabs(nw.rot[j] - w.rot[j]));
                    if (er > 0.03f) continue;
                    const float d = std::hypot(nw.pos[0] - w.pos[0], nw.pos[1] - w.pos[1], nw.pos[2] - w.pos[2]);
                    if (d < bestD) { bestD = d; bestC = int(c); bestN = int(k); }
                }
            }
            // 3. a creature part (category table) of an animated group that rotates on its own (e.g. the Hermit's fangs,
            //    0x1B02): rigidly to the nearest node
            if (bestC < 0 && (p.creature >= 0 || creaturePart) && pc && *pc != Category::World)
                for (size_t c = 0; c < animated[ch].size(); ++c) {
                    if (p.creature >= 0 && animated[ch][c].creature != p.creature) continue;
                    for (size_t k = 0; k < world[c].size(); ++k) {
                        const Xform& nw = world[c][k];
                        const float d = std::hypot(nw.pos[0] - w.pos[0], nw.pos[1] - w.pos[1], nw.pos[2] - w.pos[2]);
                        if (d < bestD) { bestD = d; bestC = int(c); bestN = int(k); }
                    }
                }
            if (bestC < 0) continue;
            animated[ch][bestC].attached.push_back({i, bestN, inverse(world[bestC][bestN]) * w});
            SDL_Log("  attach %x %s to node %d (%.1f units)", p.pl.id, categories.name(p.pl.id).c_str(), bestN, bestD);
            p.driven = true;
            ++n;
        }
        // shadows: same x/z rows and position as their part, vertical row zero
        for (Animated& a : animated[ch]) {
            std::vector<int> parts;
            for (int pi : a.nodePose) if (pi >= 0) parts.push_back(pi);
            for (const Animated::Attached& at : a.attached) parts.push_back(at.pose);
            for (int i = 0; i < int(poses[ch].size()); ++i) {
                Pose& sp = poses[ch][i];
                if (!sp.shadow || sp.driven) continue;
                for (int pi : parts) {
                    const Placement& pp = poses[ch][pi].pl;
                    float e = std::max(std::fabs(pp.pos[0] - sp.pl.pos[0]), std::fabs(pp.pos[2] - sp.pl.pos[2]));
                    for (int k : {0, 1, 2, 6, 7, 8}) e = std::max(e, std::fabs(pp.rot[k] - sp.pl.rot[k]));
                    if (e < 1e-3f) { a.shadows.push_back({i, pi, sp.pl.pos[1]}); sp.driven = true; break; }
                }
            }
        }
        if (n) SDL_Log("chapter %d: %zu more recorded parts attached to animated creatures", ch + 1, n);
        for (const Pose& p : poses[ch])
            if (!p.driven && !p.shadow && p.creature >= 0 && animatedGroups[ch].count(p.creature) && !isDebris(p.pl.id))
                SDL_Log("  not animated, stays as recorded: %x %s (group %d)", p.pl.id, categories.name(p.pl.id).c_str(), p.creature);
    }

    // A skin model per animated creature whose type has one (the arcade source builds it from the template)
    void addSkins(int ch)
    {
        auto* arcade = dynamic_cast<ArcadeSource*>(sources[int(Source::Arcade)].get());
        if (!arcade || !arcade->available()) return;
        std::vector<ArcadeSource::SkinRequest> req;
        for (size_t c = 0; c < animated[ch].size(); ++c)
          for (int which = 0; which < 2; ++which) {
            Animated& a = animated[ch][c];
            ArcadeSource::Skin skin;
            if (!arcade->skinInfo(a.type, skin, which)) continue;
            // the anchors are roles (the parts' joints): to node indices
            skin.nodeA = anim.nodeOfRole(a.type, skin.nodeA);
            skin.nodeB = anim.nodeOfRole(a.type, skin.nodeB);
            if (skin.nodeA < 0 || skin.nodeB < 0) continue;
            int part = a.nodePose[size_t(skin.nodeB)];
            if (part < 0) part = *std::max_element(a.nodePose.begin(), a.nodePose.end());
            if (part < 0) continue;
            Pose p = poses[ch][size_t(part)];
            p.shadow = false;
            p.driven = true;
            p.hidden = false;
            p.pl.id = p.pl.modelKey = kSkinKey + uint32_t(ch) * 0x1000 + uint32_t(c) * 2 + uint32_t(which);
            a.skins.push_back({int(poses[ch].size()), std::move(skin)});
            poses[ch].push_back(p);
            req.push_back({p.pl.modelKey, a.type, which});
          }
        arcade->requestSkins(ch, req);
        if (!req.empty()) SDL_Log("chapter %d: %zu generated skins (bellies)", ch + 1, req.size());
    }

    // Move a creature's skin corners to its pose (scratch = its node matrices in creature space) and re-send the model
    void updateSkin(const Animated::SkinPart& sp)
    {
        auto it = cache.find({int(Source::Arcade), chapter});
        if (it == cache.end() || !it->second) return;
        StageData& st = *it->second;
        auto m = st.models.find(poses[chapter][size_t(sp.pose)].pl.modelKey);
        if (m == st.models.end()) return;
        const Xform rel = inverse(scratch[size_t(sp.skin.nodeB)]) * scratch[size_t(sp.skin.nodeA)];
        std::vector<std::array<float, 3>> moved;
        for (const auto& c : sp.skin.corners) {
            std::array<float, 3> v;
            for (int i = 0; i < 3; ++i) v[i] = rel.rot[i * 3] * c[0] + rel.rot[i * 3 + 1] * c[1] + rel.rot[i * 3 + 2] * c[2] + rel.pos[i];
            moved.push_back(v);
        }
        static_cast<ArcadeSource*>(sources[int(Source::Arcade)].get())->rebuildSkin(chapter, sp.skin, moved, m->second);
        renderer.updateModel(st, m->second);
    }

    // Advance the chapter's creatures and the palette animation by dt seconds (the game shows one motion frame per frame,
    // ~60 per second). palette = false: no palette animation (the loaded values stay, e.g. for comparison shots).
    void updateAnimation(float dt, bool palette = true)
    {
        if (animate) gameFrame += dt * 60.0;
        ArcadePaletteFx::State ps;
        ps.stage = chapter;
        paletteFx.compute(int(gameFrame), ps, paletteAnim);
        renderer.setPaletteAnimation(palette && paletteFx.loaded() ? paletteAnim : nullptr);
        // scrolling water (uv_scroll_model, decompilation.md "World drawing"): the first texture coordinate (v) of the
        // bridge water models moves frame % 1024 steps of 1/8 texel, the second (u) of 0x8A4 ge00a_wa01a the other way
        {
            const float f = float(int(gameFrame) % 1024) / 8.0f;
            std::map<uint32_t, std::array<float, 2>> scroll;
            for (uint32_t id = 0x915; id <= 0x91C; ++id) scroll[id] = {0.0f, f};
            scroll[0x8A4] = {(1023 - int(gameFrame) % 1024) / 8.0f, 0.0f};
            renderer.setUVScroll(std::move(scroll));
        }
        // J off shows the recorded member, like the rest of the recorded frame
        for (const Cycled& c : cycled[chapter])
            poses[chapter][c.pose].pl.modelKey = c.cycle->base + uint32_t(animate
                ? int(gameFrame) / c.cycle->frames % c.cycle->count : c.cycle->recorded);
        for (Animated& a : animated[chapter]) {
            const int frames = anim.motionFrames(a.motion);
            if (animate) a.frame = std::fmod(a.frame - 1 + dt * 60.0f, float(frames)) + 1;
            if (a.motion2 >= 0 && animate)
                a.frame2 = std::fmod(a.frame2 - 1 + dt * 60.0f, float(anim.motionFrames(a.motion2))) + 1;
            anim.pose(a.type, a.motion, std::clamp(int(a.frame), 1, frames), scratch, a.motion2, int(a.frame2));
            for (size_t n = 0; n < a.nodePose.size(); ++n)
                if (a.nodePose[n] >= 0) {
                    Placement& pl = poses[chapter][a.nodePose[n]].pl;
                    toPlacement(a.root * scratch[n], pl);
                    // body_part_draw_hook: another model in the part's place (Sophie's eyes / hand, Samson's saw, the
                    // players' gun); the extras the game draws with a part were recorded as parts of their own
                    const SkeletonNode& nd = anim.skeleton(a.type).nodes[n];
                    PartDrawState ps;
                    ps.type = a.type;
                    ps.role = nd.joint;
                    ps.cls = anim.partClass(a.type, nd.joint);
                    ps.roleId = anim.partClass(a.type, nd.joint, 0);
                    ps.motion = a.motion;
                    ps.frame = int(a.frame) - 1;
                    ps.tick = int(gameFrame);
                    ps.model = nd.model;
                    partDrawModels(ps, hookModels);
                    pl.modelKey = animate && hookModels.size() == 1 ? hookModels[0] : pl.id;   // J off: as recorded
                }
            for (const Animated::SkinPart& sp : a.skins) {
                toPlacement(a.root * scratch[size_t(sp.skin.nodeB)], poses[chapter][size_t(sp.pose)].pl);
                updateSkin(sp);
            }
            for (const Animated::Attached& at : a.attached) {
                toPlacement(a.root * scratch[at.node] * at.offset, poses[chapter][at.pose].pl);
            }
            for (const Animated::Shadow& s : a.shadows) {
                const Placement& part = poses[chapter][s.part].pl;
                Placement& pl = poses[chapter][s.pose].pl;
                for (int k : {0, 1, 2, 6, 7, 8}) pl.rot[k] = part.rot[k];
                pl.rot[3] = pl.rot[4] = pl.rot[5] = 0;
                pl.pos[0] = part.pos[0]; pl.pos[1] = s.floorY; pl.pos[2] = part.pos[2];
            }
        }
    }

    // U: next (or previous) motion with the same joint count for the creature whose part is at the screen center.
    void switchMotion(int dir)
    {
        auto items = scene();
        const DrawItem* hit = pick(items);
        if (!hit) return;
        for (Animated& a : animated[chapter])
            for (int pi : a.nodePose)
                if (pi >= 0 && &poses[chapter][pi].pl == hit->placement) {
                    a.motion = stepMotion(anim, a.type, a.motion, dir);
                    a.frame = 1;
                    a.motion2 = -1;   // a picked motion plays on every node
                    SDL_Log("creature type %d %s: motion %d %s (%d frames)", a.type, anim.bodyName(a.type).c_str(), a.motion,
                            anim.motionName(a.motion).c_str(), anim.motionFrames(a.motion));
                    return;
                }
    }

    const StageData* stage(int source, int ch)
    {
        auto key = std::make_pair(source, ch);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second.get();
        std::unique_ptr<StageData> st;
        std::string err;
        if (sources[source]->available()) {
            Uint64 t0 = SDL_GetTicks();
            st = sources[source]->loadStage(ch, err);
            if (st) {
                size_t tris = 0;
                for (auto& [k, m] : st->models) tris += m.triangleCount();
                SDL_Log("loaded %s chapter %d: %zu placements, %zu models, %zu triangles, %zu texture sets (%llu ms)",
                        sourceName(Source(source)), ch + 1, st->placements.size(), st->models.size(), tris, st->textureSets.size(),
                        (unsigned long long)(SDL_GetTicks() - t0));
                renderer.upload(*st);
            } else {
                SDL_Log("%s chapter %d: %s", sourceName(Source(source)), ch + 1, err.c_str());
            }
        } else {
            err = sources[source]->status();
        }
        errors[key] = err;
        const StageData* p = st.get();
        cache[key] = std::move(st);
        return p;
    }

    std::vector<DrawItem> scene()
    {
        const StageData* st[kSourceCount];
        for (int s = 0; s < kSourceCount; ++s) st[s] = stage(s, chapter);
        auto items = resolveScene(st, sel);
        const StageData* arc = st[int(Source::Arcade)];
        const int zoneNow = arc && !arc->zoneLists.empty() ? currentZone() : -1;   // also picks lastArea
        // the script ride knows the real zone and area (the runner's BGTB / TEXCHG): applied without Z (the free-camera pick
        // stays behind Z); without it the texture set came from the nearest replay point and flipped as the camera moved
        const bool rideZone = scriptRide && runner.running() && runner.zone() >= 0;
        const int zone = zoneCull || rideZone ? zoneNow : -1;
        if (zone > 0 && zone < int(arc->zoneLists.size())) {
            const std::set<int> in(arc->zoneLists[size_t(zone)].begin(), arc->zoneLists[size_t(zone)].end());
            // the PC world: its objects carry the arcade placements' IDs (the PC's world task 0x491C48 draws the same
            // zone lists), so it is culled by ID; IDs the arcade table lacks stay
            std::set<uint32_t> ids, known;
            for (const Placement& p : arc->placements) {
                known.insert(p.id);
                if (in.count(p.romIndex)) ids.insert(p.id);
            }
            std::erase_if(items, [&](const DrawItem& it) {
                if (it.stage == arc) return it.placement->romIndex >= 0 && !in.count(it.placement->romIndex);
                return it.stage->source == Source::PC && known.count(it.placement->id) && !ids.count(it.placement->id);
            });
        }
        if (!stateGroups[chapter].empty()) {   // without draw zones: the alternate states hidden by position
            const std::set<uint32_t> hide = hiddenStates();
            std::erase_if(items, [&](const DrawItem& it) {
                return hide.count(it.placement->id) > 0 && zone <= 0;
            });
        }
        if (creatures)
            for (const Pose& p : poses[chapter]) {
                if (p.hidden || (scriptRide && p.creature < 100000)) continue;
                int want = int(sel.preferred(p.pl.id, p.cat));
                if (animate && !p.driven && (p.cat == Category::Effects || p.creature < 0 || isDebris(p.pl.id))) continue;
                // shadows exist only on the arcade: drawn when the category comes from the arcade
                if (p.shadow && sel.category[int(p.cat)] != Source::Arcade) continue;
                for (int s : {want, 1 - want}) {
                    if (!st[s]) continue;
                    auto m = st[s]->models.find(p.pl.modelKey);
                    if (m == st[s]->models.end() || m->second.triangleCount() == 0) continue;
                    items.push_back({st[s], &m->second, &p.pl, st[s]->source == Source::Arcade ? p.set : -1});
                    // the creatures' light as the game draws them (object_draw: light record 1, straight up in world
                    // space; the world's points down), not the texture set's
                    if (creatureLight && p.cat != Category::Effects && p.cat != Category::World) {
                        items.back().hasLight = true;
                        items.back().light[1] = 1;
                    }
                    break;
                }
            }
        // event objects (doors): the replay transform nearest to the camera
        propPlacements.clear();
        propPlacements.reserve(props[chapter].size());
        std::vector<std::pair<size_t, int>> propSets;
        // A moving object (the Chapter 3 elevator r3_07a_elel01a) is seen in several states from the same camera spot
        // while the camera waits; among sightings that close, take the state seen from the most places (where it rests).
        static constexpr float kSameSpot = 1.0f;
        for (const auto& [id, list] : props[chapter]) {
            const Prop* best = nullptr;
            float bestD = kPropRange;
            for (const Prop& p : list)
                for (const auto& a : p.at) {
                    const float d = std::hypot(a[0] - cam.pos[0], a[1] - cam.pos[1], a[2] - cam.pos[2]);
                    if (best && std::abs(d - bestD) <= kSameSpot ? p.at.size() > best->at.size() : d < bestD) {
                        bestD = std::min(d, bestD); best = &p;
                    }
                }
            if (!best) continue;
            propPlacements.push_back(best->pl);
            propSets.push_back({propPlacements.size() - 1, best->set});
        }
        for (auto [k, set] : propSets) {
            const Placement& pl = propPlacements[k];
            int want = int(sel.preferred(pl.id, Category::World));
            for (int s : {want, 1 - want}) {
                if (!st[s]) continue;
                auto m = st[s]->models.find(pl.modelKey);
                if (m == st[s]->models.end() || m->second.triangleCount() == 0) continue;
                items.push_back({st[s], &m->second, &pl, st[s]->source == Source::Arcade ? set : -1});
                break;
            }
        }
        // texture set: the area the scripts loaded last (TEXCHG) when known, else the replay-derived zone points; without
        // those (no texset file: the package) the area of the nearest rail point (the event scripts' TEXCHG), as Z has it
        // (the package drew every model in the chapter's first set: most of the world in the wrong textures)
        int areaSet = -1;
        if (arc && (zoneCull || rideZone || arc->zones.empty()) && zoneNow >= 0) {
            areaSet = 0;   // before the first TEXCHG: the chapter's first set
            for (size_t i = 0; i < arc->textureSets.size(); ++i)
                if (lastArea > 0 && arc->textureSets[i].romArea == lastArea) areaSet = int(i);
        }
        if (zoneSets)
            for (DrawItem& it : items) {
                int v = it.stage == arc && areaSet >= 0 ? areaSet : it.stage->zoneVariant(cam.pos);
                const std::vector<int>& ok = it.model->validVariants;
                // the camera's area set only for models whose textures are loaded in it; others keep their own set
                if (v >= 0 && (ok.empty() || std::find(ok.begin(), ok.end(), v) != ok.end())) it.variant = v;
            }
        // the colour rows the event scripts swap into each area's translation (GGCOL / GGSET: the creatures' and some of
        // the world's colours), as the game has them there most of the time (arcadeAreaColorRows; X: the area's own rows);
        // a set the texset file gave rows (measured in MAME, the developer setup) keeps them
        if (arc) {
            auto* source = static_cast<ArcadeSource*>(sources[int(Source::Arcade)].get());
            std::set<int> used;
            for (const DrawItem& it : items)
                if (it.stage == arc && it.variant >= 0 && it.variant < int(arc->textureSets.size())) used.insert(it.variant);
            for (int v : used) {
                const int area = arc->textureSets[size_t(v)].romArea;
                const char* rows = scriptRows ? arcadeAreaColorRows(chapter, area) : "";
                std::string& applied = xlatRows[{chapter, v}];
                if (area <= 0 || applied == rows || source->hasRowSwaps(chapter, v)) continue;
                std::vector<uint8_t> xlat;
                source->colorXlat(area, parseColorRows(rows), xlat);
                renderer.updateXlat(*arc, v, xlat);
                applied = rows;
            }
        }
        // sky: the world's source decides; drawn for the area the camera is in (outdoor areas only)
        // sky: the arcade knows which sky model goes where (StageData::skies, by zone); the PC has the same sky models in
        // CREAT2.RBH (0x608, 0x137C, 0x14B9, 0x1D20, 0x1238), so with the PC world the PC model is drawn when it has one
        const StageData* ws = st[int(sel.category[int(Category::World)])];
        const StageData* skyInfo = ws && !ws->skies.empty() ? ws : st[int(Source::Arcade)];
        skyPlacements.clear();
        if (sky && ws && skyInfo && !skyInfo->skies.empty()) {
            const int v = skyInfo == arc && areaSet >= 0 ? areaSet : skyInfo->zoneVariant(cam.pos);
            std::vector<std::pair<const StageData*, const Model*>> models;
            for (const StageData::Sky& s : skyInfo->skies) {
                if (s.variant != v) continue;
                const StageData* from = nullptr;
                for (const StageData* c : {ws, skyInfo})
                    if (auto m = c->models.find(s.modelKey); !from && m != c->models.end() && m->second.triangleCount() > 0) from = c;
                if (!from) continue;
                if (from->source != lastSkySource) {
                    lastSkySource = from->source;
                    SDL_Log("sky model %#x from %s", s.modelKey, sourceName(from->source));
                }
                skyPlacements.push_back({0, s.modelKey, {cam.pos[0] + s.offset[0], cam.pos[1] + s.offset[1], cam.pos[2] + s.offset[2]}});
                models.push_back({from, &from->models.at(s.modelKey)});
            }
            for (size_t k = 0; k < models.size(); ++k) {
                DrawItem d{models[k].first, models[k].second, &skyPlacements[k], models[k].first->source == Source::Arcade ? v : -1};
                d.background = true;
                items.push_back(d);
            }
        }
        return items;
    }

    void setChapterStart()
    {
        const Preset& p = kChapterStart[chapter];
        if (p.x == 0 && p.y == 0 && p.z == 0) {
            // no preset yet: above the middle of the level
            float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
            for (const DrawItem& it : scene())
                for (int i = 0; i < 3; ++i) {
                    lo[i] = std::min(lo[i], it.model->boundsMin[i] + it.placement->pos[i]);
                    hi[i] = std::max(hi[i], it.model->boundsMax[i] + it.placement->pos[i]);
                }
            cam.pos[0] = (lo[0] + hi[0]) / 2;
            cam.pos[1] = hi[1] + 50;
            cam.pos[2] = (lo[2] + hi[2]) / 2;
            cam.yaw = 0;
            cam.pitch = -60 * kDeg;
            return;
        }
        cam.pos[0] = p.x; cam.pos[1] = p.y; cam.pos[2] = p.z;
        cam.yaw = p.yawDeg * kDeg;
        cam.pitch = p.pitchDeg * kDeg;
    }

    // Model under the screen center: nearest triangle hit along the view ray.
    const DrawItem* pick(const std::vector<DrawItem>& items)
    {
        float r[3], u[3], f[3];
        cam.basis(r, u, f);
        const DrawItem* best = nullptr;
        float bestT = 1e30f;
        for (const DrawItem& it : items)
            if (!it.background)
            for (const Mesh& m : it.model->meshes)
                if (m.variant < 0 || m.variant == it.variant)
                for (size_t i = 0; i + 2 < m.vertices.size(); i += 3) {
                    float w[3][3], v0[3], e1[3], e2[3];
                    const float* R = it.placement->rot;
                    for (int k = 0; k < 3; ++k) {
                        const float* p0 = m.vertices[i + k].pos;
                        for (int c = 0; c < 3; ++c)
                            w[k][c] = R[c * 3] * p0[0] + R[c * 3 + 1] * p0[1] + R[c * 3 + 2] * p0[2] + it.placement->pos[c];
                    }
                    for (int c = 0; c < 3; ++c) {
                        v0[c] = w[0][c] - cam.pos[c];
                        e1[c] = w[1][c] - w[0][c];
                        e2[c] = w[2][c] - w[0][c];
                    }
                    // Moller-Trumbore with origin at the camera
                    float p[3] = {f[1] * e2[2] - f[2] * e2[1], f[2] * e2[0] - f[0] * e2[2], f[0] * e2[1] - f[1] * e2[0]};
                    float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
                    if (std::fabs(det) < 1e-9f) continue;
                    float inv = 1 / det, s[3] = {-v0[0], -v0[1], -v0[2]};
                    float uu = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
                    if (uu < 0 || uu > 1) continue;
                    float q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
                    float vv = (f[0] * q[0] + f[1] * q[1] + f[2] * q[2]) * inv;
                    if (vv < 0 || uu + vv > 1) continue;
                    float t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
                    if (t > 0.1f && t < bestT) { bestT = t; best = &it; }
                }
        return best;
    }

    std::string describe()
    {
        std::string s = "The House of the Dead: ReWrithe - Model Viewer (H = controls) - Chapter " + std::to_string(chapter + 1) + " - ";
        for (int c = 0; c < kCategoryCount; ++c) {
            s += categoryName(Category(c));
            s += std::string("=") + sourceName(sel.category[c]) + (c + 1 < kCategoryCount ? ", " : "");
        }
        s += zoneSets ? " - texture sets by position" : " - texture sets per model";
        if (!sel.perModel.empty()) s += " - " + std::to_string(sel.perModel.size()) + " per-model choice(s)";
        for (int src = 0; src < kSourceCount; ++src)
            if (!stage(src, chapter)) s += std::string(" - ") + sourceName(Source(src)) + " unavailable";
        return s;
    }
};

Source parseSource(const std::string& s) { return s == "arcade" ? Source::Arcade : Source::PC; }

std::string timestamp()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%d_%H%M%S", &tm);
    return buf;
}

int runShots(App& app, const std::string& file, const std::string& outDir, int w, int h)
{
    std::ifstream f(file);
    if (!f) { SDL_Log("can't open %s", file.c_str()); return 1; }
    std::filesystem::create_directories(outDir);
    std::string line;
    int fails = 0;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string name, src;
        int ch;
        float x, y, z, yaw, pitch;
        if (!(ss >> name >> ch >> src >> x >> y >> z >> yaw >> pitch)) { SDL_Log("bad line: %s", line.c_str()); ++fails; continue; }
        app.chapter = std::clamp(ch, 1, 4) - 1;
        app.sel.setAll(parseSource(src));
        app.opt = RenderOptions{};
        app.zoneSets = true;
        app.zoneCull = false;
        app.scriptRide = false;   // a previous ride: shot leaves the runner going
        app.scriptRows = true;
        app.creatureLight = true;
        float animTime = -1, rideTime = -1;
        app.animate = true;
        std::string tok;
        while (ss >> tok) {
            if (tok == "nolight") app.opt.lighting = false;
            else if (tok == "nofilter") app.opt.filter = false;
            else if (tok == "nogamma") app.opt.gamma = false;
            else if (tok == "fixedsets") app.zoneSets = false;
            else if (tok == "nozones") app.zoneCull = false;
            else if (tok == "zones") app.zoneCull = true;
            else if (tok == "nosky") app.sky = false;
            else if (tok == "nocreatures") app.creatures = false;
            else if (tok == "norows") app.scriptRows = false;
            else if (tok == "nocreaturelight") app.creatureLight = false;
            else if (tok == "noanim") app.animate = false;
            else if (tok.rfind("ride:", 0) == 0) rideTime = std::stof(tok.substr(5));
            else if (tok == "nocull") app.opt.cull = false;
            else if (tok == "nomip") app.opt.mipmap = false;
            else if (tok.rfind("t:", 0) == 0) animTime = std::max(0.0f, std::stof(tok.substr(2)));
            else if (auto eq = tok.find('='); eq != std::string::npos) {
                std::string k = tok.substr(0, eq), v = tok.substr(eq + 1);
                if (k.rfind("cat:", 0) == 0) {
                    const char* names[] = {"world", "zombies", "bosses", "effects", "humans"};
                    for (int c = 0; c < kCategoryCount; ++c)
                        if (k.substr(4) == names[c]) app.sel.category[c] = parseSource(v);
                } else {
                    app.sel.perModel[uint32_t(std::stoul(k, nullptr, 16))] = parseSource(v);
                }
            }
        }
        app.cam.pos[0] = x; app.cam.pos[1] = y; app.cam.pos[2] = z;
        app.cam.yaw = yaw * kDeg;
        app.cam.pitch = pitch * kDeg;
        if (rideTime >= 0 && app.scripts.eventCount(0) > 0) {   // ride:<seconds>: the Chapter 1 script ride, from its start
            app.chapter = 0;
            app.scriptRide = true;
            app.runner.start(&app.scripts, 0, std::vector<int>(std::begin(App::kChapter1Events), std::end(App::kChapter1Events)));
            for (App::Spawn& sp : app.spawns) { sp.dead = true; for (int pi : sp.poses) app.poses[0][size_t(pi)].hidden = true; }
            for (int k = 0; k < int(rideTime * 60) && app.runner.running(); ++k) { app.stepScripts(); app.updateAnimation(1.0f / 60.0f, false); }
            float eye[3], target[3];
            app.rails.evaluate(app.runner.path(), app.runner.frame(), eye, target);
            const float dx = target[0] - eye[0], dy = target[1] - eye[1], dz = target[2] - eye[2];
            std::copy(eye, eye + 3, app.cam.pos);
            app.cam.yaw = std::atan2(dx, dz);
            app.cam.pitch = std::atan2(dy, std::sqrt(dx * dx + dz * dz));
            SDL_Log("ride %.1f s: event %d, path %d, frame %.0f", rideTime, app.runner.event(), app.runner.path(), app.runner.frame());
        }
        for (App::Animated& a : app.animated[app.chapter]) { a.motion = a.startMotion; a.frame = float(a.start); a.frame2 = float(a.start2); }
        app.gameFrame = 0;
        app.updateAnimation(std::max(animTime, 0.0f), animTime >= 0);   // palette animation only with t:
        auto items = app.scene();
        auto px = app.renderer.drawToImage(items, app.cam, w, h, app.opt);
        std::string path = outDir + "/" + name + ".png";
        if (px.empty() || !writePng(path, w, h, px)) { SDL_Log("failed: %s", path.c_str()); ++fails; continue; }
        const DrawItem* hit = app.pick(items);
        const StageData* arc = app.stage(int(Source::Arcade), app.chapter);
        const int drawZone = app.currentZone();
        SDL_Log("wrote %s (%zu placed models; center: object %#x from %s, variant %d; draw zone %d, texture area %d)", path.c_str(),
                items.size(), hit ? hit->placement->id : 0u, hit ? sourceName(hit->stage->source) : "-", hit ? hit->variant : -1,
                drawZone, app.lastArea);
    }
    return fails ? 1 : 0;
}

} // namespace

// A portable package (<package>\bin\hotd_viewer.exe with <package>\hotd_package.txt): the player's own files are found in
// its data\ as the game does (arcade = data\hotd.zip, else the first data\arcade zip or loose files; PC = data\pc with
// THOTD.EXE, else a folder one down, else the first data\pc zip). The arcade ROMs go through the files check into
// <package>\settings\arcade_regions (the game's cache, shared with it). F12 screenshots go to <package>\screenshots.
static std::string packageFolder()
{
    const char* base = SDL_GetBasePath();
    const std::filesystem::path pkg = (std::filesystem::path(base ? base : ".") / "..").lexically_normal();
    std::error_code ec;
    return std::filesystem::exists(pkg / "hotd_package.txt", ec) ? pkg.string() : std::string();
}

static void findPackageData(const std::string& pkg, std::string& arcade, std::string& pc)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path data = fs::path(pkg) / "data";
    auto firstZip = [&](const fs::path& dir) -> std::string {
        std::vector<std::string> zips;
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file(ec) && pcfs::isZip(e.path().string())) zips.push_back(e.path().string());
        std::sort(zips.begin(), zips.end());
        return zips.empty() ? std::string() : zips.front();
    };
    auto hasExe = [&](const fs::path& dir) {
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file(ec) && SDL_strcasecmp(e.path().filename().string().c_str(), "THOTD.EXE") == 0) return true;
        return false;
    };
    arcade = fs::is_regular_file(data / "hotd.zip", ec) ? (data / "hotd.zip").string() : firstZip(data / "arcade");
    if (hasExe(data / "pc")) pc = (data / "pc").string();
    if (pc.empty()) {
        std::vector<std::string> dirs;
        for (const auto& e : fs::directory_iterator(data / "pc", ec))
            if (e.is_directory(ec) && hasExe(e.path())) dirs.push_back(e.path().string());
        std::sort(dirs.begin(), dirs.end());
        if (!dirs.empty()) pc = dirs.front();
    }
    if (pc.empty()) pc = firstZip(data / "pc");
    if (arcade.empty() && fs::is_directory(data / "arcade", ec))
        for (const auto& e : fs::recursive_directory_iterator(data / "arcade", ec))
            if (e.is_regular_file(ec) && SDL_strcasecmp(e.path().extension().string().c_str(), ".txt") != 0) {
                arcade = (data / "arcade").string();
                break;
            }
}

int main(int argc, char* argv[])
{
    std::string root, pcDir, regions, dumps, texsets, shots, outDir;
    std::string camArg;
    bool romColors = true;
    std::string poses;   // --poses <file>: posed creatures (default private\\creature_poses.txt if present)
    bool selftest = false;
    bool windowed = false;   // --windowed: not the borderless fullscreen start
    int fpsCap = -1;   // -1 = default (60)   // hidden window; synthetic key presses through the interactive loop, then exit
    int chapter = 1, w = 1280, h = 960;
    Source start = Source::Arcade;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--root") root = next();
        else if (a == "--pc") pcDir = next();
        else if (a == "--regions") regions = next();
        else if (a == "--dumps") dumps = next();
        else if (a == "--texsets") texsets = next();
        else if (a == "--chapter") chapter = std::atoi(next().c_str());
        else if (a == "--source") start = parseSource(next());
        else if (a == "--cam") camArg = next();
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &w, &h);
        else if (a == "--shots") shots = next();
        else if (a == "--out") outDir = next();
        else if (a == "--dump-colors" || a == "--dump-palette") romColors = false;
        else if (a == "--poses") poses = next();
        else if (a == "--selftest") selftest = true;
        else if (a == "--windowed") windowed = true;
        else if (a == "--fullscreen") windowed = false;
        else if (a == "--fps") fpsCap = std::atoi(next().c_str());
        else { SDL_Log("unknown option %s (see the comment at the top of src/viewer/main.cpp)", a.c_str()); return 1; }
    }
    const std::string pkg = packageFolder();
    std::string screenshotsDir;
    if (root.empty() && !pkg.empty()) root = pkg;
    if (!pkg.empty()) {
        screenshotsDir = pkg + "/screenshots";
        std::string a, p;
        findPackageData(pkg, a, p);
        if (pcDir.empty() && !p.empty()) { pcDir = p; SDL_Log("[files] PC files from the package's data: %s", p.c_str()); }
        if (regions.empty() && !a.empty()) {
            SDL_Log("[files] arcade ROMs from the package's data: %s", a.c_str());
            const std::string reg = pkg + "/settings/arcade_regions";
            game::CheckReport r = game::checkArcade(a, reg, false);
            for (const std::string& l : r.lines) SDL_Log("[files] %s", l.c_str());
            if (r.ok) regions = reg;
        }
        if (regions.empty() && pcDir.empty())
            SDL_Log("no game files found: put your arcade ROM zip in data\\arcade and your PC game in data\\pc");
    }
    if (root.empty()) {
        // the exe sits in <root>\build\<config>\ by default
        const char* base = SDL_GetBasePath();
        root = (std::filesystem::path(base ? base : ".") / ".." / "..").lexically_normal().string();
    }
    if (screenshotsDir.empty()) screenshotsDir = root + "/private/screenshots";
    if (regions.empty()) regions = root + "/private/arcade_exports/regions";
    if (pcDir.empty()) {
        // the game launcher's PC folder (pc= in its launcher.ini), else the developer copy (private/pcharness/game)
        if (char* pref = SDL_GetPrefPath("hotd-rewrite", "hotd")) {
            std::ifstream ini(std::string(pref) + "launcher.ini");
            SDL_free(pref);
            for (std::string line; std::getline(ini, line);)
                if (line.rfind("pc=", 0) == 0) pcDir = line.substr(3);
        }
        if (pcDir.empty()) pcDir = root + "/private/pcharness/game";
    }
    if (dumps.empty()) {
        // the texset file names dump tags of the full-game replay; fullgame_bufram is the newest copy (same replay)
        dumps = root + "/private/mame_dumps/fullgame_bufram";
        if (!fileExists(dumps + "/index.txt")) dumps = root + "/private/mame_dumps/fullgame_us";
    }
    if (texsets.empty() && fileExists(root + "/private/arcade_texsets.txt")) texsets = root + "/private/arcade_texsets.txt";

    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    const bool batch = !shots.empty();
    const bool hidden = batch || selftest;
    SDL_Window* window = SDL_CreateWindow("The House of the Dead: ReWrithe - Model Viewer", batch ? 64 : w, batch ? 64 : h,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (hidden ? SDL_WINDOW_HIDDEN : 0));
    if (!window) { SDL_Log("window: %s", SDL_GetError()); return 1; }
    setWindowIconFromPc(window, pcDir);   // the player's own THOTD.EXE icon (silent when missing)
    if (!hidden && !windowed) hotd::util::toggleBorderlessFullscreen(window);   // borderless fullscreen start (user 2026-10-19)
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (!ctx) { SDL_Log("OpenGL 3.3 context: %s", SDL_GetError()); return 1; }
    if (const char* missing = gl::load()) { SDL_Log("OpenGL function missing: %s", missing); return 1; }
    SDL_Log("OpenGL: %s / %s", (const char*)gl::GetString(gl::RENDERER), (const char*)gl::GetString(gl::VERSION));
    // vsync plus a 60 fps cap by default: vsync alone runs at the monitor's rate (280 Hz on the user's monitor = loud fans),
    // and the driver may ignore vsync. --fps N to change, --fps 0 = vsync only.
    const bool vsync = SDL_GL_SetSwapInterval(1);
    if (fpsCap < 0) fpsCap = 60;
    SDL_Log("vsync %s, frame cap %s", vsync ? "on" : "unavailable", fpsCap > 0 ? std::to_string(fpsCap).c_str() : "off");

    App app;
    app.sources[int(Source::PC)] = std::make_unique<PcSource>(PcConfig{pcDir});
    const std::string statesFile = root + "/private/arcade_states.txt";
    const bool haveStates = fileExists(statesFile);
    ArcadeConfig acfg{regions, dumps, texsets, romColors, haveStates};
    acfg.defaultSkies = true;   // no texset file (the package): the skies per area from the loader's table
    app.sources[int(Source::Arcade)] = std::make_unique<ArcadeSource>(acfg);
    if (haveStates) app.loadStates(statesFile);
    for (auto& s : app.sources) SDL_Log("%s: %s", sourceName(s->source()), s->status().c_str());
    if (!poses.empty() || fileExists(root + "/private/creature_poses.txt"))
        app.loadPoses(poses.empty() ? root + "/private/creature_poses.txt" : poses);
    std::string err;
    if (app.anim.load(regions, err)) app.setupAnimation();
    else SDL_Log("creature animation unavailable: %s", err.c_str());
    // the PC's sky models (CREAT2.RBH), requested for every chapter (the arcade decides which one is shown where)
    for (int ch = 0; ch < 4; ++ch) app.sources[int(Source::PC)]->requestModels(ch, {0x608, 0x137C, 0x14B9, 0x1D20, 0x1238});
    if (app.anim.loaded() && app.scripts.load(pcDir + "/EVT/ALLSTAGE/EVTALL.RBH", err)) app.prepareScripts();
    else SDL_Log("event scripts unavailable: %s", err.c_str());
    if (app.anim.loaded() && fileExists(root + "/private/arcade_props.txt")) app.loadProps(root + "/private/arcade_props.txt");
    if (!app.paletteFx.load(regions, err)) SDL_Log("palette animation unavailable: %s", err.c_str());
    if (!app.rails.load(pcDir + "/CAM/ALLSTAGE/CAMALL.RBH", err)) SDL_Log("camera rails unavailable: %s", err.c_str());
    if (!app.renderer.init(err)) { SDL_Log("%s", err.c_str()); return 1; }

    Overlay overlay;
    if (!overlay.init(err)) { SDL_Log("%s", err.c_str()); return 1; }
    int result = 0;
    if (batch) {
        result = runShots(app, shots, outDir.empty() ? screenshotsDir : outDir, w, h);
    } else {
        app.chapter = std::clamp(chapter, 1, 4) - 1;
        app.sel.setAll(start);
        app.setChapterStart();
        if (!camArg.empty()) {
            float yaw = 0, pitch = 0;
            std::sscanf(camArg.c_str(), "%f,%f,%f,%f,%f", &app.cam.pos[0], &app.cam.pos[1], &app.cam.pos[2], &yaw, &pitch);
            app.cam.yaw = yaw * kDeg;
            app.cam.pitch = pitch * kDeg;
        }
        bool running = true, look = false, lookToggle = false;
        Uint64 last = SDL_GetTicksNS(), lastTitle = 0;
        SDL_SetWindowTitle(window, app.describe().c_str());
        // --selftest: one key per 5 frames through the normal event handling
        const SDL_Keycode testKeys[] = {SDLK_2, SDLK_TAB, SDLK_F1, SDLK_P, SDLK_R, SDLK_V, SDLK_L, SDLK_G, SDLK_F, SDLK_PAGEUP,
                                        SDLK_3, SDLK_4, SDLK_1, SDLK_F2, SDLK_F3, SDLK_F4, SDLK_TAB, SDLK_C, SDLK_F12, SDLK_ESCAPE};
        int frame = 0;
        while (running) {
            bool shot = false;
            if (selftest && frame % 5 == 4 && size_t(frame / 5) < std::size(testKeys)) {
                SDL_Event ev{};
                ev.type = SDL_EVENT_KEY_DOWN;
                ev.key.key = testKeys[frame / 5];
                ev.key.down = true;
                ev.key.windowID = SDL_GetWindowID(window);
                SDL_PushEvent(&ev);
            }
            ++frame;
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) running = false;
                else if (hotd::util::isFullscreenKey(e)) hotd::util::toggleBorderlessFullscreen(window);   // F11 / Alt+Enter
                else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) look = true;
                else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT) look = false;
                else if (e.type == SDL_EVENT_MOUSE_MOTION && (look || lookToggle)) {
                    app.cam.yaw += e.motion.xrel * 0.0035f;
                    app.cam.pitch = std::clamp(app.cam.pitch - e.motion.yrel * 0.0035f, -1.55f, 1.55f);
                } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                    SDL_Keycode k = e.key.key;
                    if (k == SDLK_ESCAPE) running = false;
                    else if (k == SDLK_TAB) {
                        // whole game: if anything is arcade, go all PC; else all arcade
                        bool anyArcade = false;
                        for (Source s : app.sel.category) anyArcade |= s == Source::Arcade;
                        app.sel.setAll(anyArcade ? Source::PC : Source::Arcade);
                    } else if (k >= SDLK_F1 && k <= SDLK_F5) {
                        Source& s = app.sel.category[k - SDLK_F1];
                        s = s == Source::PC ? Source::Arcade : Source::PC;
                    } else if (k == SDLK_P) {
                        auto items = app.scene();
                        if (const DrawItem* hit = app.pick(items)) {
                            uint32_t id = hit->placement->id;
                            Source now = hit->stage->source, to = now == Source::PC ? Source::Arcade : Source::PC;
                            // a creature part switches the whole creature (all parts of its group)
                            int creature = -1;
                            for (const App::Pose& p : app.poses[app.chapter])
                                if (&p.pl == hit->placement) creature = p.creature;
                            int n = 0;
                            if (creature >= 0) {
                                for (const App::Pose& p : app.poses[app.chapter])
                                    if (p.creature == creature) { app.sel.perModel[p.pl.id] = to; ++n; }
                            } else {
                                app.sel.perModel[id] = to;
                                n = 1;
                            }
                            // the arcade's name for it (the PC files have none), e.g. "nikubaan02a"
                            std::string name = hit->model->name;
                            if (name.empty() && app.stage(int(Source::Arcade), app.chapter)) {
                                const StageData* a = app.stage(int(Source::Arcade), app.chapter);
                                if (auto m = a->models.find(hit->model->key); m != a->models.end()) name = m->second.name;
                            }
                            SDL_Log("object %#x %s%s: %s -> %s (%d parts)", id, name.c_str(), creature >= 0 ? " (creature)" : "",
                                    sourceName(now), sourceName(to), n);
                        }
                    } else if (k == SDLK_R) app.sel.perModel.clear();
                    else if (k >= SDLK_1 && k <= SDLK_4) { app.chapter = int(k - SDLK_1); app.setChapterStart(); }
                    else if (k == SDLK_PAGEUP || k == SDLK_PAGEDOWN) {
                        app.chapter = (app.chapter + (k == SDLK_PAGEUP ? 1 : 3)) % 4;
                        app.setChapterStart();
                    } else if (k == SDLK_L) app.opt.lighting = !app.opt.lighting;
                    else if (k == SDLK_F) app.opt.filter = !app.opt.filter;
                    else if (k == SDLK_G) app.opt.gamma = !app.opt.gamma;
                    else if (k == SDLK_V) app.zoneSets = !app.zoneSets;
                    else if (k == SDLK_Z) { app.zoneCull = !app.zoneCull; SDL_Log("draw zones %s (zone %d)", app.zoneCull ? "on" : "off", app.currentZone()); }
                    else if (k == SDLK_X) { app.scriptRows = !app.scriptRows; SDL_Log("arcade color rows: %s", app.scriptRows ? "as in the game (the scripts' rows)" : "the areas' own"); }
                    else if (k == SDLK_K) app.sky = !app.sky;
                    else if (k == SDLK_O) app.creatures = !app.creatures;
                    else if (k == SDLK_J) app.animate = !app.animate;
                    else if (k == SDLK_Y && app.rails.count() > 6) {
                        app.riding = !app.riding;
                        if (app.riding) { app.chapter = 0; app.railStep = 0; app.railFrame = 0; }
                        app.scriptRide = app.riding && app.scripts.eventCount(0) > 0;
                        if (app.scriptRide)
                            app.runner.start(&app.scripts, 0,
                                             std::vector<int>(std::begin(App::kChapter1Events), std::end(App::kChapter1Events)));
                    }
                    else if (k == SDLK_U) app.switchMotion((e.key.mod & SDL_KMOD_SHIFT) ? -1 : 1);
                    else if (k == SDLK_H) app.help = !app.help;
                    else if (k == SDLK_N && !app.views[app.chapter].empty()) {
                        const App::View& v = app.views[app.chapter][app.nextView++ % app.views[app.chapter].size()];
                        app.cam.pos[0] = v.pos[0]; app.cam.pos[1] = v.pos[1]; app.cam.pos[2] = v.pos[2];
                        app.cam.yaw = v.yaw * kDeg;
                        app.cam.pitch = v.pitch * kDeg;
                        SDL_Log("creature view %s", v.tag.c_str());
                    }
                    else if (k == SDLK_M) { lookToggle = !lookToggle; SDL_SetWindowRelativeMouseMode(window, lookToggle); }
                    else if (k == SDLK_C)
                        SDL_Log("camera: --cam %.2f,%.2f,%.2f,%.2f,%.2f (chapter %d)", app.cam.pos[0], app.cam.pos[1], app.cam.pos[2],
                                app.cam.yaw / kDeg, app.cam.pitch / kDeg, app.chapter + 1);
                    else if (k == SDLK_F12) shot = true;
                    SDL_SetWindowTitle(window, app.describe().c_str());
                    if (selftest) SDL_Log("selftest key %s -> %s", SDL_GetKeyName(k), app.describe().c_str());
                }
            }
            if (look && !lookToggle) SDL_SetWindowRelativeMouseMode(window, true);
            else if (!lookToggle) SDL_SetWindowRelativeMouseMode(window, false);

            Uint64 now = SDL_GetTicksNS();
            float dt = std::min(float(now - last) / 1e9f, 0.1f);
            last = now;
            const bool* keys = SDL_GetKeyboardState(nullptr);
            float speed = (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) ? 600.0f : 150.0f;
            float r[3], u[3], f[3];
            app.cam.basis(r, u, f);
            float mv[3] = {0, 0, 0};
            auto add = [&](const float d[3], float s) { for (int c = 0; c < 3; ++c) mv[c] += d[c] * s; };
            if (keys[SDL_SCANCODE_W]) add(f, 1);
            if (keys[SDL_SCANCODE_S]) add(f, -1);
            if (keys[SDL_SCANCODE_D]) add(r, 1);
            if (keys[SDL_SCANCODE_A]) add(r, -1);
            const float upv[3] = {0, 1, 0};
            if (keys[SDL_SCANCODE_E]) add(upv, 1);
            if (keys[SDL_SCANCODE_Q]) add(upv, -1);
            for (int c = 0; c < 3; ++c) app.cam.pos[c] += mv[c] * speed * dt;
            if (app.riding && app.scriptRide) {
                app.railFrame += dt * 60.0f;
                for (; app.railFrame >= 1.0f; app.railFrame -= 1.0f) app.stepScripts();
                if (!app.runner.running()) { app.riding = app.scriptRide = false; SDL_Log("script ride: end of the scripted part"); }
                float eye[3], target[3];
                app.rails.evaluate(app.runner.path(), app.runner.frame(), eye, target);
                const float dx = target[0] - eye[0], dy = target[1] - eye[1], dz = target[2] - eye[2];
                std::copy(eye, eye + 3, app.cam.pos);
                app.cam.yaw = std::atan2(dx, dz);
                app.cam.pitch = std::atan2(dy, std::sqrt(dx * dx + dz * dz));
            } else if (app.riding) {
                app.railFrame += dt * 60.0f;
                int path = App::kChapter1Rail[app.railStep];
                if (app.railFrame > app.rails.length(path)) {
                    app.railFrame = 0;
                    app.railStep = (app.railStep + 1) % std::size(App::kChapter1Rail);
                    path = App::kChapter1Rail[app.railStep];
                    SDL_Log("rail: path %d (%.0f frames)", path, app.rails.length(path));
                }
                float eye[3], target[3];
                app.rails.evaluate(path, app.railFrame, eye, target);
                const float dx = target[0] - eye[0], dy = target[1] - eye[1], dz = target[2] - eye[2];
                std::copy(eye, eye + 3, app.cam.pos);
                app.cam.yaw = std::atan2(dx, dz);
                app.cam.pitch = std::atan2(dy, std::sqrt(dx * dx + dz * dz));
            }

            int ww, wh;
            SDL_GetWindowSizeInPixels(window, &ww, &wh);
            app.updateAnimation(dt);
            auto items = app.scene();
            app.renderer.draw(items, app.cam, ww, wh, app.opt);
            if (shot) {
                std::filesystem::create_directories(screenshotsDir);
                std::string path = screenshotsDir + "/viewer_" + timestamp() + ".png";
                if (writePng(path, ww, wh, app.renderer.readBack(ww, wh))) SDL_Log("screenshot: %s", path.c_str());
            }
            if (app.help) {   // after the screenshot: screenshots show the scene only
                overlay.setText(app.helpLines(), wh >= 720 ? 2 : 1);
                overlay.draw(ww, wh);
            }
            if (selftest && frame == 3) {   // the overlay as it looks on screen
                std::filesystem::create_directories(screenshotsDir);
                writePng(screenshotsDir + "/selftest_overlay.png", ww, wh, app.renderer.readBack(ww, wh));
            }
            SDL_GL_SwapWindow(window);
            // frame cap (also slows down a lot while minimized)
            const bool minimized = SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED;
            const int cap = minimized ? 10 : fpsCap;
            if (cap > 0) {
                const Uint64 frameNs = 1000000000ull / Uint64(cap), spent = SDL_GetTicksNS() - now;
                if (spent < frameNs) SDL_DelayPrecise(frameNs - spent);
            }
            if (now - lastTitle > 1000000000ull) {
                lastTitle = now;
                SDL_SetWindowTitle(window, app.describe().c_str());
            }
        }
    }
    app.renderer.shutdown();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
