// The game's picture (R1): the chapter's world from the arcade source, the models the ported logic drew this frame
// (Runtime::drawList, through geo_draw_model) at their matrices, seen from the game's camera (the coprocessor view
// matrix), with the arcade's projection (focal length 496 on a 496 x 384 screen).
#pragma once

#include "assets/arcade_anim.h"
#include "assets/arcade_loader.h"
#include "assets/pc_loader.h"
#include "game/ragdoll.h"
#include "game/runtime.h"
#include "render/renderer.h"

#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace hotd::game {

// rate (render interpolation, docs/teaser_status/rate.md): the drawn state of the game frame before the newest one,
// copied by the frame loop before each tick (read only: drawing never touches the game state)
struct FrameSnapshot {
    bool valid = false;
    uint64_t frame = 0;   // Runtime::frameCount when copied (the newest frame is frame + 1)
    float camera[12] = {};
    float focal = 496.0f;
    std::vector<DrawModel> drawList;
    std::vector<Runtime::SkinDraw> skinDraws;
    void take(const Runtime& rt);
};

class GameView {
public:
    // regions / dumps / texsets as hotd_viewer takes them (assets/arcade_loader.h); chapter 0-3. pcDir non-empty: the
    // model source is the PC release (R4's model switch): the PC world, sky and every model by ID from the PC files only
    // (creature parts share the arcade's IDs); an ID the PC lacks is logged ("PC model gap") and not drawn, no arcade
    // fallback (user rule "Assets"); no generated skins (the PC draws none). blood: the blood colour setting (PC pages)
    bool init(const std::string& regions, const std::string& dumps, const std::string& texsets, int chapter, std::string& err,
              const std::string& pcDir = {}, int blood = 0);
    // perf (2026-10-21): init in two parts: prepare (the files, the models, the decal layers; no GL, so on any thread:
    // the next chapter's view during the chapter clear) and finish (the GL part: shaders, upload; on the GL thread)
    struct Prepared {
        int chapter = 0, blood = 0;
        bool ok = false;
        std::string err;
        ArcadeConfig arcadeCfg;
        std::unique_ptr<PcConfig> pcCfg;
        std::unique_ptr<ArcadeSource> source;
        std::unique_ptr<PcSource> pcSource;
        CreatureAnimations anim;
        std::map<int, ArcadeSource::Skin> skins;
        std::unique_ptr<StageData> stage, pcStage;
        std::string pcErr;   // the PC stage could not be loaded (the arcade models are drawn)
        uint64_t setupMs = 0, stageMs = 0, pcNs = 0, layersNs = 0;
    };
    static std::unique_ptr<Prepared> prepare(const std::string& regions, const std::string& dumps, const std::string& texsets,
                                             int chapter, const std::string& pcDir = {}, int blood = 0, bool background = false);
    bool finish(std::unique_ptr<Prepared> p, std::string& err);
    // remakefix: a camera cut between two frames' Runtime::camera (the menu's live attract cross-fades there)
    static bool cameraCut(const float* a, const float* b);
    void shutdown() { renderer.shutdown(); }
    // menus3: the custom blood colour (0xRRGGBB; -1 none) changed in place (the arcade models' palette entry 1023:
    // drawn so from the next frame; the PC models' blood pages keep the colour they were loaded with)
    void setBloodCustom(int rgb) { bloodCustom_ = rgb; }

    // the camera of the last frame (the runtime's view matrix)
    void updateCamera(const Runtime& rt);
    // prev / alpha (rate): the 3D picture blended from the previous frame (alpha 0) to the newest (1): the camera
    // (position lerp, orientation slerp) and every game model / skin matched between the frames (DrawModel::task +
    // model + body + order); unmatched items, items that jumped (a cut, a teleport, a new spawn) and every item across
    // a camera cut are drawn as the newest frame has them. Null / alpha 1: the newest frame exactly (screenshots)
    void draw(const Runtime& rt, int width, int height, const FrameSnapshot* prev = nullptr, float alpha = 1.0f);
    // rate: statistics of the last draw (items blended, unmatched, jumped; the camera blended / cut)
    struct InterpStats { int blended = 0, unmatched = 0, jumped = 0; bool camera = false, cut = false; } interpStats;

    // window pixel -> the arcade screen (centre 0, x right, y up; on screen within +-248 x +-192)
    void toArcade(float px, float py, int width, int height, float& ax, float& ay) const;
    void fromArcade(float ax, float ay, int width, int height, float& px, float& py) const;

    Renderer renderer;
    RenderOptions opt;
    // display: models the view draws after the game's (camera-space matrices as DrawModel::m): the players' gun marks
    std::vector<DrawModel> overlay;
    // display: the model source's model `id` is loaded (arcade: the stage's, PC: the PC files')
    bool hasModel(uint32_t id) const;
    // leftovers: the Ragdoll mod's walls (game/ragdoll.h; null: none built: the mod off at the chapter's load)
    const RagdollWalls* ragdollWalls() const { return walls_.objs.empty() ? nullptr : &walls_; }
    Camera cam;
    // the cut-scene viewer's Free Cam (game/scene_view.h): the picture seen from this camera instead of `cam` (the
    // game's, still worked out from the frame: the models' places come from it); null: the game's. Draws only
    const Camera* camOverride = nullptr;
    bool sky = true;
    // vr (2026-11-03, docs/vr.md): set, draw hands this frame's items to it instead of drawing them (the VR frame
    // draws them once per eye through `renderer`, from `cam`: the game's camera, blended as the picture would be).
    // Empty (always outside VR): draw draws as before
    std::function<void(const std::vector<DrawItem>& items)> drawOverride;
    // vr: the game camera of a frame as camera -> world (row-major 3x3 rotation, columns right / up / forward of the
    // left-handed camera, then the eye's position): Runtime::camera inverted (the eye-independent aim reference)
    static void cameraToWorld(const float* coproCamera, float rot[9], float pos[3]);
    // vr (openxr worker 2026-11-03): one of the game's own models placed by the VR frame (the players' gun hand 0x719,
    // Rogan's left hand 0x717 on the controllers): an item from the model source (the PC files' model with PC models
    // on), the arcade's texture set of the area the last draw used, world = rot (row-major) x model + pos. slot 0..3:
    // its placement (kept until the next call for that slot). false: the model is not loaded
    bool placedItem(int slot, uint32_t id, const float rot[9], const float pos[3], DrawItem& out);
private:
    Placement placed_[4];   // vr: placedItem's placements
    int variant_ = -1;      // vr: the arcade texture set the last draw picked
    RagdollWalls walls_;                              // leftovers: see ragdollWalls()
    std::map<const Placement*, int> wallOf_;          // a world placement's object in walls_ (standing triangles only)
    // ipolish: the door-sized models the game draws (not world placements): their triangles in model space (9 floats
    // each); each frame the drawn ones are placed by their matrices into walls_ past its first staticWalls_ objects
    std::map<uint32_t, std::vector<float>> propWalls_;
    size_t staticWalls_ = 0;
    std::unique_ptr<ArcadeSource> source_;
    std::unique_ptr<StageData> stage_;
    std::unique_ptr<PcSource> pcSource_;
    std::unique_ptr<StageData> pcStage_;    // the PC source's stage (model source PC), else null
    CreatureAnimations anim_;
    std::vector<DrawItem> world_;           // resolved once
    std::vector<Placement> placements_;     // this frame's creature / object placements (stable while drawing)
    std::set<uint32_t> missingModels_;
    std::set<uint32_t> pcGaps_;             // model source PC: IDs the PC files lack (logged once, not drawn)
    void logPcGap(uint32_t id, const char* what);
    void checkPcIds() const;
    // draw zones (BGTB, work RAM draw_zone): per zone, the arcade placement indices and their object IDs (the PC
    // world's objects carry the same IDs, so the PC world is culled by ID)
    std::vector<std::set<int>> zoneIndices_;
    int chapter_ = 0;   // the stage loaded (init), for the zone-list check in draw (title worker)
    // title-2: another chapter's world drawn by the attract (the prologue's last scene: kind108_zone_set -> Chapter 4's
    // placement table), loaded on first use and kept: its arcade stage (placements, texture sets) and, with the PC
    // models, its PC stage; the world items resolved once
    int extraStage_ = -1;
    std::set<int> extraTried_;
    std::unique_ptr<StageData> extra_, pcExtra_;
    std::vector<DrawItem> extraWorld_;
    std::map<uint32_t, int> extraRomIndex_;
    bool ensureExtra(int stage, const Runtime& rt);
    // the viewer's actors with a texture area of their own (Runtime::actorAreas): the extra stage's sets given their
    // colour rows (the Gallery's look), for that loaded stage
    const StageData* actorRowsFor_ = nullptr;
    std::set<int> actorRowsSets_;
    std::set<uint32_t> actorMissLogged_;   // pmfix: actor models drawn without their area's set (logged once)
    // attract2: started ahead (the prologue's first frame): the arcade stage now, the slow rest (extraWork: the PC
    // stage or the decal layers) on a worker thread; ensureExtra takes the result
    struct ExtraLoad {
        std::unique_ptr<StageData> a, p;   // the arcade stage, the PC one (model source PC)
        std::string err;
    };
    // perf (2026-10-21): the whole load on the worker, with its own sources (arcadeCfg_ / pcCfg_: nothing shared with
    // the drawing); also the ending's staff roll (Chapters 3, 2, 1's worlds, each loaded ahead of the next; were
    // 1.3 s frames each with the arcade models)
    void prefetchExtra(int stage, const Runtime& rt);
    ArcadeConfig arcadeCfg_;
    std::unique_ptr<PcConfig> pcCfg_;   // (model source PC)
    std::vector<uint32_t> extraIds(const Runtime& rt) const;   // the model IDs an extra stage loads
    std::vector<ArcadeSource::SkinRequest> extraSkins() const;   // rpolish: the skins (bellies) an extra stage builds too
    std::unique_ptr<StageData> loadExtraArcade(int stage, const Runtime& rt, std::string& err);
    ExtraLoad extraWork(int stage, std::unique_ptr<StageData> a);
    bool finishExtra(int stage, std::unique_ptr<StageData> a, std::unique_ptr<StageData> p, const Runtime& rt, uint64_t t0);
    int prefetchStage_ = -1;
    // declared after pcSource_: destroyed (waited for) before the worker's source goes
    std::future<ExtraLoad> prefetch_;
    int extraVariant_ = -1;                 // this frame's texture set of the extra stage (-1: not drawn)
    uint32_t extraRowsSerial_ = ~0u;
    int extraRowsSet_ = -1;
    // pcfix: model source PC, the chapters past 4 draw the PC's own world file: THOTD.EXE pc_chapter_files_load
    // (0x418AA0) takes chapter 0-3 -> WORLD1-4.RBH, 4 / 5 (the how-to-play, the prologue) -> WORLD5.RBH, 6 (the ending)
    // -> WORLD6.RBH, and refills the model table (0xD53080) from it: those scenes' objects (the prologue's room_n1a 0xCF3,
    // which the game draws itself) are made for them there. Loaded on first use and kept; null: not such a chapter
    std::unique_ptr<StageData> pcAlt_;
    int pcAltChapter_ = -1;                 // the chapter byte pcAlt_ was loaded for (4 and 5 share WORLD5)
    std::set<int> pcAltTried_;
    std::vector<DrawItem> altWorld_;
    std::set<uint32_t> pcWorldIds_;         // the chapter's own PC world objects (not in the table while pcAlt_ is)
    const StageData* pcAltFor(const Runtime& rt);
    std::vector<std::set<uint32_t>> zoneIds_;
    std::map<uint32_t, int> romIndexById_;  // arcade placement ID -> its table index (the PC world's objects by ID)
    std::map<uint32_t, const Placement*> arcadePlacementById_;   // the first arcade placement of each world ID (pcfix)
    std::set<uint32_t> pcRebased_;          // PC world objects the game drew (logged once)
    int romIndexOf(uint32_t id) const { auto r = romIndexById_.find(id); return r == romIndexById_.end() ? -1 : r->second; }
    std::vector<Placement> worldSubs_;      // this frame's world placements drawn with a model-list entry (stable)
    std::set<uint32_t> arcadeWorldIds_;     // every ID of the arcade placement table (PC objects outside it: always drawn)
    // generated skins (the bellies): per creature type the loader's template, drawn through kSkinSlots models of
    // their own (rebuilt each frame from the anchors the game saved)
    static constexpr uint32_t kSkinKey = 0xF0000000;   // + type x 16 + slot
    static constexpr int kSkinSlots = 8;
    std::map<int, ArcadeSource::Skin> skins_;
    uint32_t colorRowsSerial_ = 0;          // Runtime::colorRowsSerial last applied (s1: color_row_set)
    uint32_t palAnimSerial_ = ~0u;          // Runtime::polyPaletteAnim.serial last sent (palanim)
    uint16_t bloodEntry_ = 0;             // polygon palette entry 1023 last sent (the blood colour, arcade models)
    int bloodCustom_ = -1;                // display: the custom blood colour 0xRRGGBB (experimental), else -1
    int xlatBlood_ = -1;                  // the custom colour last put into the blood rows' translation (-1 none)
};

} // namespace hotd::game
