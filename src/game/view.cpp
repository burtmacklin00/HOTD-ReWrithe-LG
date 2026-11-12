#include "game/view.h"
#include "game/gallery.h"
#include "game/player_colours.h"
#include "game/cosmods.h"
#include "assets/creature_pose.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <thread>

namespace hotd::game {

namespace {
constexpr float kFocal = 496.0f, kHalfW = 248.0f, kHalfH = 192.0f;

// the coprocessor's matrix (images of the x, y, z axes, then the translation) as a 3x4 [R | t] (row-major R)
struct M34 {
    float r[9], t[3];
};
M34 fromCopro(const float* m)
{
    M34 o;
    for (int k = 0; k < 3; ++k) {
        for (int j = 0; j < 3; ++j)
            o.r[k * 3 + j] = m[3 * j + k];
        o.t[k] = m[9 + k];
    }
    return o;
}
M34 mul(const M34& a, const M34& b)
{
    M34 o;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            o.r[i * 3 + j] = a.r[i * 3] * b.r[j] + a.r[i * 3 + 1] * b.r[3 + j] + a.r[i * 3 + 2] * b.r[6 + j];
        o.t[i] = a.r[i * 3] * b.t[0] + a.r[i * 3 + 1] * b.t[1] + a.r[i * 3 + 2] * b.t[2] + a.t[i];
    }
    return o;
}
// inverse of a rigid [R | t] (the view matrix has no scale)
M34 invRigid(const M34& a)
{
    M34 o;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            o.r[i * 3 + j] = a.r[j * 3 + i];
    for (int i = 0; i < 3; ++i)
        o.t[i] = -(o.r[i * 3] * a.t[0] + o.r[i * 3 + 1] * a.t[1] + o.r[i * 3 + 2] * a.t[2]);
    return o;
}
// rate (render interpolation): blending two frames' world matrices. Models: the 3x3 and the translation lerped element by
// element (two parts joined at a point stay joined: the point's place is the lerp of its two places under either part),
// then each axis rescaled to the lerp of its two lengths (no shrinking while turning); scaled / squashed (shadow)
// matrices blend the same way. The camera: a rigid pose, its orientation slerped.
float axisLen(const M34& a, int j) { return std::sqrt(a.r[j] * a.r[j] + a.r[3 + j] * a.r[3 + j] + a.r[6 + j] * a.r[6 + j]); }
// the cosine between axis j of a and of b (1 when either is degenerate: a squashed shadow's y axis)
float axisCos(const M34& a, const M34& b, int j)
{
    const float la = axisLen(a, j), lb = axisLen(b, j);
    if (la < 1e-6f || lb < 1e-6f)
        return 1.0f;
    return (a.r[j] * b.r[j] + a.r[3 + j] * b.r[3 + j] + a.r[6 + j] * b.r[6 + j]) / (la * lb);
}
M34 blendM34(const M34& a, const M34& b, float t)
{
    M34 o;
    for (int i = 0; i < 9; ++i)
        o.r[i] = a.r[i] + (b.r[i] - a.r[i]) * t;
    for (int i = 0; i < 3; ++i)
        o.t[i] = a.t[i] + (b.t[i] - a.t[i]) * t;
    for (int j = 0; j < 3; ++j) {
        const float la = axisLen(a, j), lb = axisLen(b, j), lo = axisLen(o, j), want = la + (lb - la) * t;
        if (lo > 1e-6f)
            for (int i = 0; i < 3; ++i)
                o.r[i * 3 + j] *= want / lo;
    }
    return o;
}
// a jump between two frames (a cut, a teleport, a reused slot): moved more than maxMove units, or an axis turned more
// than acos(minCos)
bool jumped(const M34& a, const M34& b, float maxMove, float minCos)
{
    const float dx = b.t[0] - a.t[0], dy = b.t[1] - a.t[1], dz = b.t[2] - a.t[2];
    if (!(dx * dx + dy * dy + dz * dz <= maxMove * maxMove))
        return true;
    for (int j = 0; j < 3; ++j)
        if (!(axisCos(a, b, j) >= minCos))
            return true;
    return false;
}
struct Quat { float w, x, y, z; };
Quat quatOf(const float* r)   // r: a row-major rotation
{
    Quat q;
    const float tr = r[0] + r[4] + r[8];
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        q = {0.25f * s, (r[7] - r[5]) / s, (r[2] - r[6]) / s, (r[3] - r[1]) / s};
    } else if (r[0] > r[4] && r[0] > r[8]) {
        const float s = std::sqrt(1.0f + r[0] - r[4] - r[8]) * 2.0f;
        q = {(r[7] - r[5]) / s, 0.25f * s, (r[1] + r[3]) / s, (r[2] + r[6]) / s};
    } else if (r[4] > r[8]) {
        const float s = std::sqrt(1.0f + r[4] - r[0] - r[8]) * 2.0f;
        q = {(r[2] - r[6]) / s, (r[1] + r[3]) / s, 0.25f * s, (r[5] + r[7]) / s};
    } else {
        const float s = std::sqrt(1.0f + r[8] - r[0] - r[4]) * 2.0f;
        q = {(r[3] - r[1]) / s, (r[2] + r[6]) / s, (r[5] + r[7]) / s, 0.25f * s};
    }
    const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return n > 0.0f ? Quat{q.w / n, q.x / n, q.y / n, q.z / n} : Quat{1, 0, 0, 0};
}
M34 slerpRigid(const M34& a, const M34& b, float t)
{
    Quat p = quatOf(a.r), q = quatOf(b.r);
    float d = p.w * q.w + p.x * q.x + p.y * q.y + p.z * q.z;
    if (d < 0.0f) { q = {-q.w, -q.x, -q.y, -q.z}; d = -d; }
    float ka = 1.0f - t, kb = t;
    if (d < 0.9995f) {
        const float th = std::acos(std::min(d, 1.0f)), s = std::sin(th);
        ka = std::sin((1.0f - t) * th) / s;
        kb = std::sin(t * th) / s;
    }
    Quat o{ka * p.w + kb * q.w, ka * p.x + kb * q.x, ka * p.y + kb * q.y, ka * p.z + kb * q.z};
    const float n = std::sqrt(o.w * o.w + o.x * o.x + o.y * o.y + o.z * o.z);
    o = {o.w / n, o.x / n, o.y / n, o.z / n};
    M34 m;
    m.r[0] = 1 - 2 * (o.y * o.y + o.z * o.z), m.r[1] = 2 * (o.x * o.y - o.z * o.w), m.r[2] = 2 * (o.x * o.z + o.y * o.w);
    m.r[3] = 2 * (o.x * o.y + o.z * o.w), m.r[4] = 1 - 2 * (o.x * o.x + o.z * o.z), m.r[5] = 2 * (o.y * o.z - o.x * o.w);
    m.r[6] = 2 * (o.x * o.z - o.y * o.w), m.r[7] = 2 * (o.y * o.z + o.x * o.w), m.r[8] = 1 - 2 * (o.x * o.x + o.y * o.y);
    for (int i = 0; i < 3; ++i)
        m.t[i] = a.t[i] + (b.t[i] - a.t[i]) * t;
    return m;
}
// the thresholds: the camera's cut (UNVERIFIED (teaser): the rides' fastest moves are a few units and degrees a frame)
// and an item's jump
constexpr float kCamCutMove = 40.0f, kCamCutCos = 0.866f;    // 30 degrees
constexpr float kItemJumpMove = 40.0f, kItemJumpCos = 0.5f;  // 60 degrees
// the game's draw fade (t1: DrawModel::fade, draw_fade_begin's level 0..0xFF, 0xFF opaque) when the runtime has it
template <class T> auto drawFade(const T& d, int) -> decltype(float(d.fade)) { return float(d.fade) / 255.0f; }
template <class T> float drawFade(const T&, long) { return 1.0f; }
template <class T> auto drawFlat(const T& d, int) -> decltype(uint16_t(d.flat)) { return d.flat; }   // flash
template <class T> uint16_t drawFlat(const T&, long) { return 0xFFFF; }

// perf: a stage's CPU copies after its upload (the vertices; Mesh::vertexCount still answers; the texture pages), freed
// on a worker thread (freeing a few hundred MB took ~15 ms of the frame). Keys from keepFrom stay (the generated skins,
// GameView::kSkinKey..: rebuilt and re-sent every frame)
void releaseStageCpu(StageData& sd, uint32_t keepFrom)
{
    auto vertices = std::make_unique<std::vector<std::vector<Vertex>>>();
    auto pages = std::make_unique<std::vector<std::vector<uint8_t>>>();
    for (auto& [key, m] : sd.models) {
        if (key >= keepFrom)
            continue;
        for (Mesh& me : m.meshes) {
            if (me.vertices.empty())
                continue;
            me.releasedCount = me.vertices.size();
            if (Mesh::keepMeshPositions) {   // (questsim: VR's CPU aim ray against the parts' triangles)
                me.positions.resize(me.vertices.size() * 3);
                for (size_t i = 0; i < me.vertices.size(); ++i) std::copy_n(me.vertices[i].pos, 3, &me.positions[i * 3]);
            }
            vertices->push_back(std::move(me.vertices));
            me.vertices = std::vector<Vertex>();
        }
    }
    for (TextureSet& ts : sd.textureSets)
        for (std::vector<std::vector<uint8_t>>* v : {&ts.layers, &ts.layersBrown}) {
            for (std::vector<uint8_t>& p : *v) pages->push_back(std::move(p));
            std::vector<std::vector<uint8_t>>().swap(*v);
        }
    std::thread([v = std::move(vertices), p = std::move(pages)]() mutable {
        v.reset();
        p.reset();
    }).detach();
}

// The custom Blood Color (arcade models; user 2026-11-03): the creatures' blood rows (Runtime::bloodRows: the rows GGSET
// sets by the blood colour) in that colour, every shade of them as the game's own green blood recolours them (the pale
// pink inside the wounds too). Per brightness the row's (r, g, b): the grey under it kept, the colour above it the
// custom colour's (the brightest channel scaled to 1), all times the custom colour's brightness
void tintBloodRows(std::vector<uint8_t>& xlat, const std::set<int>& rows, int rgb)
{
    if (rgb < 0 || xlat.size() < 256 * 96)
        return;
    const float t[3] = {float(rgb >> 16 & 0xFF) / 255.0f, float(rgb >> 8 & 0xFF) / 255.0f, float(rgb & 0xFF) / 255.0f};
    const float top = std::max({t[0], t[1], t[2], 1e-3f});
    for (int row : rows) {
        if (row < 0 || row >= 32)
            continue;
        for (int i = 0; i < 256; ++i) {
            uint8_t* c[3] = {&xlat[size_t(row * 256 + i)], &xlat[size_t((32 + row) * 256 + i)], &xlat[size_t((64 + row) * 256 + i)]};
            const float lo = float(std::min({*c[0], *c[1], *c[2]})), hi = float(std::max({*c[0], *c[1], *c[2]}));
            for (int k = 0; k < 3; ++k)
                *c[k] = uint8_t(std::clamp((lo + (hi - lo) * t[k] / top) * top + 0.5f, 0.0f, 255.0f));
        }
    }
}
} // namespace

// remakefix: the menu's live attract: a camera cut between two frames' cameras (Runtime::camera), as draw's
bool GameView::cameraCut(const float* a, const float* b)
{
    return jumped(invRigid(fromCopro(a)), invRigid(fromCopro(b)), kCamCutMove, kCamCutCos);
}

// perf (2026-10-21): the set-up in two parts: prepare (files, models, decal layers: no GL, any thread) and finish (the
// GL part: shaders, upload). init does both at once; the game prepares the next chapter's view on a worker during the
// chapter clear (main.cpp viewPrefetch) and finishes it at the change (was the whole load, 0.4-1.5 s, in one frame)
std::unique_ptr<GameView::Prepared> GameView::prepare(const std::string& regions, const std::string& dumps,
                                                      const std::string& texsets, int chapter, const std::string& pcDir,
                                                      int blood, bool background)
{
    auto p = std::make_unique<Prepared>();
    const uint64_t tStart = SDL_GetTicks();   // perf: the parts of the set-up (the "view:" line)
    p->chapter = chapter;
    p->blood = blood;
    p->arcadeCfg = ArcadeConfig{regions, dumps, texsets, true, false};
    p->source = std::make_unique<ArcadeSource>(p->arcadeCfg);
    if (!p->source->available()) {
        p->err = p->source->status();
        return p;
    }
    // every creature part model (skeleton nodes of every type): the game may draw any of them
    if (!pcDir.empty()) {
        // cosmods: Rainbow Blood on: the custom path in white, its blood texels marked for the renderer's colours
        // (PcConfig::kBloodRainbow); the player's own colour comes back at the next load with the mod off
        const int pcBlood = rainbowBloodSettings().on ? (PcConfig::kBloodCustom | PcConfig::kBloodRainbow | 0xFFFFFF) : blood;
        p->pcCfg = std::make_unique<PcConfig>(PcConfig{pcDir, pcBlood});
        p->pcSource = std::make_unique<PcSource>(*p->pcCfg);
    }
    std::string err;
    if (p->anim.load(regions, err)) {
        std::vector<uint32_t> ids;
        for (int t = 0; t < p->anim.typeCount(); ++t)
            for (const SkeletonNode& n : p->anim.skeleton(t).nodes)
                if (n.model)
                    for (uint32_t k = 0; k < 5; ++k)   // the damage variants follow the model (..._dam00a, _dam01a)
                        ids.push_back(n.model + k);
        // every other model the game may draw through geo_draw_model (props, the opening's car and its wheels / tyre
        // frames 0x952 / 0x953 / 0xA49.., the player characters' 0x5E6 ...): all IDs of the model table; the source
        // leaves out the ones without polygons (R4 fix, 2026-10-15: they were never loaded, so never drawn)
        for (uint32_t id = 1; id < 0x2000; ++id)
            ids.push_back(id);
        p->source->requestModels(chapter, ids);
        if (p->pcSource)
            p->pcSource->requestModels(chapter, ids);
    }
    std::vector<ArcadeSource::SkinRequest> req;
    for (int t = 0; t < 100; ++t) {
        ArcadeSource::Skin sk;
        if (!p->source->skinInfo(t, sk, 0))
            continue;
        p->skins[t] = sk;
        for (int k = 0; k < kSkinSlots; ++k)
            req.push_back({kSkinKey + uint32_t(t) * 16 + uint32_t(k), t, 0});
    }
    if (ArcadeSource::Skin sk; p->source->skinInfo(34, sk, 1)) {   // Sophie's strip (sophie_skin_draw)
        const int t = Runtime::kSophieStrip;
        p->skins[t] = sk;
        for (int k = 0; k < kSkinSlots; ++k)
            req.push_back({kSkinKey + uint32_t(t) * 16 + uint32_t(k), 34, 1});
    }
    p->source->requestSkins(chapter, req);
    const uint64_t t0 = SDL_GetTicks();
    p->setupMs = t0 - tStart;
    p->stage = p->source->loadStage(chapter, err);
    if (!p->stage) {
        p->err = err;
        return p;
    }
    p->stageMs = SDL_GetTicks() - t0;
    Prepared* const pp = p.get();
    auto layers = [pp, background](StageData& sd) {   // decal layers inside the models (render/renderer.h markCoplanarLayers)
        if (std::getenv("HOTD_NO_LAYERS")) return;   // debugging: before / after comparisons
        const uint64_t l0 = SDL_GetTicksNS();
        const int n = markCoplanarLayersAll(sd.models, background);   // (perf: on several threads)
        pp->layersNs += SDL_GetTicksNS() - l0;
        SDL_Log("%s models: %d triangles on a coplanar layer below them", sourceName(sd.source), n);
    };
    // model source PC: the arcade stage stays for its tables (placements, draw zones, names) but is never drawn (user
    // rule "Assets"), so it is neither layered nor uploaded (perf: was, ~1 GB); the PC stage first, the arcade one
    // drawn only when the PC stage fails
    if (p->pcSource && p->pcSource->available()) {
        // the sky models: the PC has the arcade's (CREAT2.RBH, formats.md §28), drawn where the arcade's table puts one
        std::vector<uint32_t> skyIds;
        for (const StageData::Sky& s : p->stage->skies)
            skyIds.push_back(s.modelKey);
        p->pcSource->requestModels(chapter, skyIds);
        const uint64_t p0 = SDL_GetTicksNS();
        p->pcStage = p->pcSource->loadStage(chapter, p->pcErr);
        p->pcNs = SDL_GetTicksNS() - p0;
        if (p->pcStage)
            layers(*p->pcStage);
    }
    if (!p->pcStage)
        layers(*p->stage);
    p->ok = true;
    return p;
}

bool GameView::init(const std::string& regions, const std::string& dumps, const std::string& texsets, int chapter, std::string& err,
                    const std::string& pcDir, int blood)
{
    return finish(prepare(regions, dumps, texsets, chapter, pcDir, blood), err);
}

bool GameView::finish(std::unique_ptr<Prepared> p, std::string& err)
{
    if (!p || !p->ok) {
        err = p ? p->err : "no view prepared";
        return false;
    }
    const uint64_t t1 = SDL_GetTicks();
    chapter_ = p->chapter;
    bloodCustom_ = (p->blood & PcConfig::kBloodCustom) ? (p->blood & 0xFFFFFF) : -1;
    arcadeCfg_ = p->arcadeCfg;
    pcCfg_ = std::move(p->pcCfg);
    source_ = std::move(p->source);
    pcSource_ = std::move(p->pcSource);
    anim_ = std::move(p->anim);
    skins_ = std::move(p->skins);
    stage_ = std::move(p->stage);
    pcStage_ = std::move(p->pcStage);
    // developer (weapons: the in-game models to pick): HOTD_DUMP_MODEL_NAMES=<file>: every model of the stage (id, name,
    // triangles, bounds) and each creature type's skeleton models
    if (const char* dump = std::getenv("HOTD_DUMP_MODEL_NAMES"))
        if (FILE* f = std::fopen(dump, "w")) {
            for (const auto& [id, m] : stage_->models)
                std::fprintf(f, "%#x %s tris %zu bounds %.2f..%.2f %.2f..%.2f %.2f..%.2f\n", id, m.name.c_str(), size_t(m.triangleCount()), m.boundsMin[0],
                             m.boundsMax[0], m.boundsMin[1], m.boundsMax[1], m.boundsMin[2], m.boundsMax[2]);
            for (int t = 0; t < anim_.typeCount(); ++t)
                for (const SkeletonNode& n : anim_.skeleton(t).nodes)
                    if (n.model)
                        std::fprintf(f, "type %d node model %#x\n", t, n.model);
            std::fclose(f);
        }
    for (int s = 0; s < 2; ++s)   // 4P: players 3 / 4's colour ramps (player_colours.h) for their shot sparks
        for (int k = 0; k < 5; ++k)
            for (int c = 0; c < 3; ++c)
                renderer.rampStops[s][k][c] = float(kPlayerRamp[s][k][c]) / 255.0f;
    {   // Light Gun Edition: 2 Night Vision's heat (purple > red > orange > yellow > white), 3 the Exploding Zombies' glow
        static const uint8_t kFun[2][5][3] = {{{50, 0, 70}, {190, 0, 40}, {255, 100, 0}, {255, 210, 40}, {255, 255, 235}},
                                              {{120, 30, 0}, {230, 90, 0}, {255, 160, 20}, {255, 230, 90}, {255, 255, 210}}};
        for (int s = 0; s < 2; ++s)
            for (int k = 0; k < 5; ++k)
                for (int c = 0; c < 3; ++c)
                    renderer.rampStops[2 + s][k][c] = float(kFun[s][k][c]) / 255.0f;
    }
    const uint64_t r0 = SDL_GetTicksNS();
    const bool rendererOk = renderer.init(err);
    const uint64_t rendererNs = SDL_GetTicksNS() - r0;
    if (!rendererOk)
        return false;
    uint64_t uploadNs = 0;
    // perf (2026-10-16): after the upload the CPU copies go (vertices, texture pages); kept: the generated skins (rebuilt
    // and re-sent every frame) and everything with HOTD_LIST_DRAWN_AT (its lists read the vertices)
    const bool keepCpu = std::getenv("HOTD_LIST_DRAWN_AT") != nullptr;
    auto releaseCpu = [&](StageData& sd) {
        if (keepCpu) return;
        releaseStageCpu(sd, sd.source == Source::Arcade ? kSkinKey : ~0u);
    };
    auto upload = [&](StageData& sd) {
        const uint64_t u0 = SDL_GetTicksNS();
        renderer.upload(sd);
        uploadNs += SDL_GetTicksNS() - u0;
    };
    // leftovers: the Ragdoll mod's walls (game/ragdoll.h), from the drawn stage's world placements before their
    // vertices go: each placement's standing triangles in world space (|normal y| < 0.6), with their box
    // ipolish: built whether the mod is on or not (the vertices go after this: the mod switched on mid-game has its
    // walls at once, not from the next chapter), and the props the game moves (doors ...): their models' standing
    // triangles in model space (propWalls_), placed by their drawn matrices every frame (draw)
    walls_ = RagdollWalls{};
    wallOf_.clear();
    propWalls_.clear();
    staticWalls_ = 0;
    {
        const uint64_t w0 = SDL_GetTicksNS();
        const StageData& sd = pcStage_ ? *pcStage_ : *stage_;
        std::set<uint32_t> worldKeys;
        for (const Placement& pl : sd.placements)
            worldKeys.insert(pl.modelKey);
        size_t propTris = 0;
        for (const auto& [id, model] : sd.models) {
            if (worldKeys.count(id))
                continue;
            std::vector<float> t;
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
            for (const Mesh& me : model.meshes) {
                if (me.variant >= 0 && me.variant != model.preferredVariant)
                    continue;
                for (size_t v = 0; v + 2 < me.vertices.size(); v += 3) {
                    const float* a = me.vertices[v].pos;
                    const float* b = me.vertices[v + 1].pos;
                    const float* c = me.vertices[v + 2].pos;
                    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                    const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                    const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    if (nl < 1e-4f)
                        continue;
                    for (const float* p : {a, b, c})
                        for (int i = 0; i < 3; ++i)
                            t.push_back(p[i]), lo[i] = std::min(lo[i], p[i]), hi[i] = std::max(hi[i], p[i]);
                }
            }
            // door-sized: 10+ units high, 6+ wide, not too many triangles (a body part, a shot spark, a pickup: not)
            if (t.empty() || hi[1] - lo[1] < 10.0f || std::max(hi[0] - lo[0], hi[2] - lo[2]) < 6.0f || t.size() / 9 > 3000)
                continue;
            propTris += t.size() / 9;
            propWalls_[id] = std::move(t);
        }
        SDL_Log("view: the ragdolls' moving props: %zu models, %zu triangles", propWalls_.size(), propTris);

        size_t total = 0, floors = 0;
        for (const Placement& pl : sd.placements) {
            auto m = sd.models.find(pl.modelKey);
            if (m == sd.models.end())
                continue;
            RagdollWalls::Obj o;
            o.lo[0] = o.lo[1] = o.lo[2] = 1e30f, o.hi[0] = o.hi[1] = o.hi[2] = -1e30f;
            for (const Mesh& me : m->second.meshes) {
                if (me.variant >= 0 && me.variant != m->second.preferredVariant)
                    continue;
                for (size_t v = 0; v + 2 < me.vertices.size(); v += 3) {
                    float p[9];
                    for (int k = 0; k < 3; ++k) {
                        const float* s = me.vertices[v + size_t(k)].pos;
                        for (int i = 0; i < 3; ++i)
                            p[k * 3 + i] = pl.rot[i * 3] * s[0] + pl.rot[i * 3 + 1] * s[1] + pl.rot[i * 3 + 2] * s[2] + pl.pos[i];
                    }
                    const float e1[3] = {p[3] - p[0], p[4] - p[1], p[5] - p[2]}, e2[3] = {p[6] - p[0], p[7] - p[1], p[8] - p[2]};
                    const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                    const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    if (nl < 1e-4f)
                        continue;   // (a degenerate one)
                    // standing: a wall; lying (floors, steps, slopes; ceilings too, the ragdoll takes only those under
                    // a body): the ground
                    std::vector<float>& to = std::abs(n[1]) / nl >= 0.6f ? o.floors : o.tris;
                    to.insert(to.end(), p, p + 9);
                    for (int i = 0; i < 3; ++i)
                        to.push_back(n[i] / nl);
                    for (int k = 0; k < 3; ++k)
                        for (int i = 0; i < 3; ++i)
                            o.lo[i] = std::min(o.lo[i], p[k * 3 + i]), o.hi[i] = std::max(o.hi[i], p[k * 3 + i]);
                }
            }
            if (o.tris.empty() && o.floors.empty())
                continue;
            total += o.tris.size() / 12;
            floors += o.floors.size() / 12;
            wallOf_[&pl] = int(walls_.objs.size());
            walls_.objs.push_back(std::move(o));
        }
        staticWalls_ = walls_.objs.size();
        walls_.statics = staticWalls_;
        SDL_Log("view: the ragdolls' walls: %zu triangles, ground %zu, in %zu world objects (%.1f ms)", total, floors,
                walls_.objs.size(), double(SDL_GetTicksNS() - w0) / 1e6);
    }
    if (pcStage_) {
        if (!pcStage_->textureSets.empty()) SDL_Log("PC textures: %s", pcStage_->textureSets[0].label.c_str());
        upload(*pcStage_);
        releaseCpu(*pcStage_);
        releaseStageCpu(*stage_, ~0u);   // the arcade geometry is not drawn
    } else {
        if (pcSource_)
            SDL_Log("PC models: %s (the arcade models are drawn)", pcSource_->available() ? p->pcErr.c_str() : pcSource_->status().c_str());
        upload(*stage_);
        releaseCpu(*stage_);
    }
    SDL_Log("view: set-up %llu ms (sources, animations, skins), arcade stage loaded in %llu ms, the rest %llu ms (shaders "
            "%.0f, PC stage %.0f, layers %.0f, upload %.0f; the GL part here %llu ms)", (unsigned long long)p->setupMs,
            (unsigned long long)p->stageMs, (unsigned long long)(SDL_GetTicks() - t1 + (p->pcNs + p->layersNs) / 1000000),
            double(rendererNs) / 1e6, double(p->pcNs) / 1e6, double(p->layersNs) / 1e6, double(uploadNs) / 1e6,
            (unsigned long long)(SDL_GetTicks() - t1));
    const StageData* st[kSourceCount] = {pcStage_.get(), stage_.get()};
    SourceSelection sel;
    sel.setAll(pcStage_ ? Source::PC : Source::Arcade);
    world_ = resolveScene(st, sel);
    if (pcStage_) {
        // model source PC: the PC's own models only (user rule "Assets"): resolveScene fills IDs the PC world lacks with
        // the arcade's; those are left out and logged (a gap to fix from the PC files, never an arcade model)
        std::set<uint32_t> gaps;
        std::vector<DrawItem> own;
        for (const DrawItem& it : world_)
            if (it.stage == pcStage_.get()) own.push_back(it);
            else gaps.insert(it.placement->id);
        world_ = std::move(own);
        for (uint32_t id : gaps) logPcGap(id, "world placement");
        if (std::getenv("HOTD_CHECK_PC_IDS")) checkPcIds();
    }
    for (const Placement& p : stage_->placements) {
        arcadeWorldIds_.insert(p.id);
        romIndexById_.emplace(p.id, p.romIndex);
        romIndicesById_.emplace(p.id, p.romIndex);
        arcadePlacementById_.emplace(p.id, &p);
    }
    for (const std::vector<int>& list : stage_->zoneLists) {
        zoneIndices_.emplace_back(list.begin(), list.end());
        std::set<uint32_t> ids;
        for (const Placement& p : stage_->placements)
            if (zoneIndices_.back().count(p.romIndex)) ids.insert(p.id);
        zoneIds_.push_back(std::move(ids));
    }
    cam.fovY = 2.0f * std::atan(kHalfH / kFocal);
    return true;
}

// title-2: another stage's world for the attract (see view.h); false when it cannot be loaded (drawn without it)
bool GameView::ensureExtra(int stage, const Runtime& rt)
{
    if (extraStage_ == stage)
        return true;
    extraVariant_ = -1;
    if (extraTried_.count(stage) || !source_)
        return false;
    const uint64_t t0 = SDL_GetTicks();
    ExtraLoad r;
    if (prefetchStage_ == stage && prefetch_.valid()) {   // attract2: started ahead by prefetchExtra
        prefetchStage_ = -1;
        r = prefetch_.get();
    } else {
        r.a = loadExtraArcade(stage, rt, r.err);
        if (r.a)
            r = extraWork(stage, std::move(r.a));
    }
    // (only a failed load is not tried again: a loaded one can be needed again after another replaced it, the
    // cut-scene viewer's Replay of the staff roll: its scenes went dark, Chapter 3's world was not loaded back)
    if (!r.a || (pcStage_ && pcSource_ && !r.p)) {
        extraTried_.insert(stage);
        SDL_Log("view: chapter %d's %sworld for the attract: %s (drawn without it)", stage + 1, r.a ? "PC " : "", r.err.c_str());
        return false;
    }
    return finishExtra(stage, std::move(r.a), std::move(r.p), rt, t0);
}

// the slow part of the extra stage, no GL: model source PC its PC stage, else the arcade models' decal layers (as the
// chapter's, render/renderer.h; about 1.1 s for Chapter 4's every model)
GameView::ExtraLoad GameView::extraWork(int stage, std::unique_ptr<StageData> a)
{
    ExtraLoad r;
    r.a = std::move(a);
    if (pcStage_ && pcSource_)
        r.p = pcSource_->loadStage(stage, r.err);
    else
        markCoplanarLayersAll(r.a->models);   // (perf: on several threads)
    return r;
}

// attract2: the prologue's last scene drew Chapter 4's world only when it came (kind108_zone_set), and loading it then
// (the decal layers of every model, or the PC stage: over a second) froze the picture at the end of the scene before
// (the user's play-test: G held about 1.1 s too long; MAME: no pause). Started at the prologue's first frame instead:
// perf (2026-10-21): all of it on a worker thread (was: the arcade stage here, ~80 ms), with the worker's own arcade /
// PC sources (the same files and settings: the same stage; nothing is shared with the drawing, which goes on using
// source_ / pcSource_); the ending's staff roll uses it too (GameView::draw, finishExtra)
void GameView::prefetchExtra(int stage, const Runtime& rt)
{
    if (stage == extraStage_ || stage == prefetchStage_ || extraTried_.count(stage) || !source_ || std::getenv("HOTD_NO_EXTRA_STAGE"))
        return;
    if (prefetch_.valid())   // another one pending: dropped (the worker finishes first)
        prefetch_.get();
    prefetchStage_ = stage;
    const bool pc = pcStage_ && pcSource_ && pcCfg_;
    prefetch_ = std::async(std::launch::async, [stage, pc, ids = extraIds(rt), skins = extraSkins(), acfg = arcadeCfg_,
                                                pcfg = pc ? *pcCfg_ : PcConfig{}]() {
        const uint64_t t0 = SDL_GetTicks();
        ExtraLoad r;
        ArcadeSource src(acfg);
        if (!pc) {
            src.requestModels(stage, ids);
            src.requestSkins(stage, skins);
        }
        r.a = src.loadStage(stage, r.err);
        if (!r.a)
            return r;
        if (pc)
            r.p = PcSource(pcfg).loadStage(stage, r.err);
        else
            markCoplanarLayersAll(r.a->models, true);
        SDL_Log("view: chapter %d's world loaded ahead on a worker in %llu ms", stage + 1, (unsigned long long)(SDL_GetTicks() - t0));
        return r;
    });
    SDL_Log("view: chapter %d's world: loading ahead", stage + 1);
}

// the people the scene draws (every creature part model, as init requests them, and what the game draws now):
// arcade models in the stage's texture sets
std::vector<uint32_t> GameView::extraIds(const Runtime& rt) const
{
    std::vector<uint32_t> ids;
    for (const DrawModel& dm : rt.drawList)
        ids.push_back(dm.model);
    for (int t = 0; t < anim_.typeCount(); ++t)
        for (const SkeletonNode& n : anim_.skeleton(t).nodes)
            if (n.model)
                for (uint32_t k = 0; k < 5; ++k)
                    ids.push_back(n.model + k);
    for (uint32_t id = 1; id < 0x2000; ++id)   // and every other ID, as init (Curien's head 0x1D02 kyubatama_CT19a ...)
        ids.push_back(id);
    return ids;
}

// rpolish: the skins of the chapter's stage (skins_, the same templates) as the extra stage's own models, so a body
// drawn from that stage (the attract's Dr. Curien in Chapter 4's world) has its belly skin in that stage's texture set
std::vector<ArcadeSource::SkinRequest> GameView::extraSkins() const
{
    std::vector<ArcadeSource::SkinRequest> req;
    for (const auto& [t, sk] : skins_) {
        (void)sk;
        const bool strip = t == Runtime::kSophieStrip;
        for (int k = 0; k < kSkinSlots; ++k)
            req.push_back({kSkinKey + uint32_t(t) * 16 + uint32_t(k), strip ? 34 : t, strip ? 1 : 0});
    }
    return req;
}

std::unique_ptr<StageData> GameView::loadExtraArcade(int stage, const Runtime& rt, std::string& err)
{
    // the people the scene draws (every creature part model, as init requests them, and what the game draws now):
    // arcade models in the stage's texture sets
    std::vector<uint32_t> ids;
    for (const DrawModel& dm : rt.drawList)
        ids.push_back(dm.model);
    for (int t = 0; t < anim_.typeCount(); ++t)
        for (const SkeletonNode& n : anim_.skeleton(t).nodes)
            if (n.model)
                for (uint32_t k = 0; k < 5; ++k)
                    ids.push_back(n.model + k);
    for (uint32_t id = 1; id < 0x2000; ++id)   // and every other ID, as init (Curien's head 0x1D02 kyubatama_CT19a ...)
        ids.push_back(id);
    if (!pcStage_) {
        source_->requestModels(stage, ids);
        source_->requestSkins(stage, extraSkins());
    }
    return source_->loadStage(stage, err);
}

bool GameView::finishExtra(int stage, std::unique_ptr<StageData> a, std::unique_ptr<StageData> p, const Runtime& rt, uint64_t t0)
{
    if (extra_) renderer.release(*extra_);
    if (pcExtra_) renderer.release(*pcExtra_);
    extra_ = std::move(a);
    pcExtra_ = std::move(p);
    extraStage_ = stage;
    renderer.upload(pcExtra_ ? *pcExtra_ : *extra_);   // (the arcade models' decal layers: extraWork)
    const StageData* st[kSourceCount] = {pcExtra_.get(), extra_.get()};
    SourceSelection sel;
    sel.setAll(pcExtra_ ? Source::PC : Source::Arcade);
    extraWorld_.clear();
    std::set<uint32_t> gaps;
    for (const DrawItem& it : resolveScene(st, sel))
        if (!pcExtra_ || it.stage == pcExtra_.get())   // model source PC: the PC's own models only (as the chapter's)
            extraWorld_.push_back(it);
        else
            gaps.insert(it.placement->id);
    for (uint32_t id : gaps) logPcGap(id, "attract world placement");
    extraRomIndex_.clear();
    for (const Placement& pl : extra_->placements)
        extraRomIndex_.emplace(pl.id, pl.romIndex);
    std::string areas;
    for (const TextureSet& ts : extra_->textureSets)
        areas += " " + std::to_string(ts.romArea);
    SDL_Log("view: chapter %d's world for the attract loaded (%zu items, texture areas%s; now %d) in %llu ms", stage + 1,
            extraWorld_.size(), areas.c_str(), rt.textureArea, (unsigned long long)(SDL_GetTicks() - t0));
    // perf: the CPU copies go after the upload, as the chapter's (init; ~200 MB an arcade stage); HOTD_LIST_DRAWN_AT's
    // lists read the vertices
    if (!std::getenv("HOTD_LIST_DRAWN_AT"))
        for (StageData* sd : {extra_.get(), pcExtra_.get()})
            if (sd) releaseStageCpu(*sd, ~0u);
    // perf: the ending's staff roll (chapter byte 6) shows Chapters 3, 2 and 1's worlds in turn: the next one loads ahead
    if (rt.mem.at<int8_t>(A::G::chapter) == 6 && stage > 0)
        prefetchExtra(stage - 1, rt);
    return true;
}

// pcfix: the PC world file of the chapter byte past 4 (view.h pcAlt_), loaded on first use; null when the chapter is
// 0-3, the models are the arcade's, or WORLDn.RBH cannot be read (then the chapter's PC world is drawn, as before)
const StageData* GameView::pcAltFor(const Runtime& rt)
{
    if (!pcStage_ || !pcSource_ || std::getenv("HOTD_PC_NO_ALT_WORLD"))
        return nullptr;
    const int c = rt.mem.at<int8_t>(A::G::chapter);
    if (c < 4 || c > 6)
        return nullptr;
    if (pcAlt_ && (pcAltChapter_ == c || (pcAltChapter_ >= 4 && pcAltChapter_ <= 5 && c <= 5)))
        return pcAlt_.get();
    if (pcAltTried_.count(c))
        return nullptr;
    pcAltTried_.insert(c);
    const uint64_t t0 = SDL_GetTicks();
    std::string err;
    std::unique_ptr<StageData> p = pcSource_->loadStage(c, err);
    if (!p) {
        SDL_Log("view: chapter byte %d's PC world: %s (the chapter's PC world is drawn)", c, err.c_str());
        return nullptr;
    }
    if (pcAlt_) renderer.release(*pcAlt_);
    pcAlt_ = std::move(p);
    pcAltChapter_ = c;
    renderer.upload(*pcAlt_);
    if (!std::getenv("HOTD_LIST_DRAWN_AT"))   // (its lists read the vertices)
    for (auto& [key, m] : pcAlt_->models)
        for (Mesh& me : m.meshes) me.releaseVertices();
    for (TextureSet& ts : pcAlt_->textureSets) {
        std::vector<std::vector<uint8_t>>().swap(ts.layers);
        std::vector<std::vector<uint8_t>>().swap(ts.layersBrown);
    }
    altWorld_.clear();
    const StageData* st[kSourceCount] = {pcAlt_.get(), nullptr};
    SourceSelection sel;
    sel.setAll(Source::PC);
    altWorld_ = resolveScene(st, sel);
    if (pcWorldIds_.empty())
        for (const Placement& pl : pcStage_->placements) pcWorldIds_.insert(pl.id);
    SDL_Log("view: chapter byte %d draws the PC's WORLD%d.RBH (%zu objects) in %llu ms", c, c <= 5 ? 5 : 6, altWorld_.size(),
            (unsigned long long)(SDL_GetTicks() - t0));
    return pcAlt_.get();
}

void GameView::logPcGap(uint32_t id, const char* what)
{
    if (!pcGaps_.insert(id).second)
        return;
    auto a = stage_->models.find(id);
    // (some are in the PC files without polygons, e.g. 0x365 / 0x461: the PC draws nothing there either)
    SDL_Log("PC model gap: %#x (%s, arcade name %s) has no polygons in the PC files: not drawn", id, what,
            a != stage_->models.end() && !a->second.name.empty() ? a->second.name.c_str() : "-");
}

// HOTD_CHECK_PC_IDS=1: every ID both sources have, PC bounds against the arcade's (formats.md §21: the parts share IDs and
// local coordinates); a different model under the same ID shows as different bounds
void GameView::checkPcIds() const
{
    int same = 0, differ = 0, arcadeOnly = 0, pcOnly = 0;
    for (const auto& [id, pm] : pcStage_->models) {
        auto am = stage_->models.find(id);
        if (am == stage_->models.end() || am->second.triangleCount() == 0) {
            ++pcOnly;
            SDL_Log("PC ID check: %#x only in the PC files (%zu triangles, %.0f..%.0f %.0f..%.0f %.0f..%.0f)", id, pm.triangleCount(),
                    pm.boundsMin[0], pm.boundsMax[0], pm.boundsMin[1], pm.boundsMax[1], pm.boundsMin[2], pm.boundsMax[2]);
            continue;
        }
        float worst = 0, ext = 1;
        for (int i = 0; i < 3; ++i) {
            ext = std::max(ext, am->second.boundsMax[i] - am->second.boundsMin[i]);
            worst = std::max({worst, std::abs(pm.boundsMin[i] - am->second.boundsMin[i]), std::abs(pm.boundsMax[i] - am->second.boundsMax[i])});
        }
        if (worst <= std::max(1.0f, 0.05f * ext)) {
            ++same;
            continue;
        }
        ++differ;
        SDL_Log("PC ID check: %#x (%s) differs by %.1f (extent %.1f): PC %.0f..%.0f %.0f..%.0f %.0f..%.0f arcade %.0f..%.0f %.0f..%.0f %.0f..%.0f",
                id, am->second.name.c_str(), worst, ext, pm.boundsMin[0], pm.boundsMax[0], pm.boundsMin[1], pm.boundsMax[1],
                pm.boundsMin[2], pm.boundsMax[2], am->second.boundsMin[0], am->second.boundsMax[0], am->second.boundsMin[1],
                am->second.boundsMax[1], am->second.boundsMin[2], am->second.boundsMax[2]);
    }
    for (const auto& [id, am] : stage_->models)
        if (id < kSkinKey && am.triangleCount() > 0 && !pcStage_->models.count(id)) ++arcadeOnly;
    SDL_Log("PC ID check: %d IDs same bounds, %d differ, %d PC only, %d arcade only (loaded IDs)", same, differ, pcOnly, arcadeOnly);
}

void GameView::updateCamera(const Runtime& rt)
{
    const M34 view = fromCopro(rt.camera), inv = invRigid(view);
    // the eye = the inverse's translation; forward = the camera's +z in the world
    cam.pos[0] = inv.t[0];
    cam.pos[1] = inv.t[1];
    cam.pos[2] = inv.t[2];
    const float fx = inv.r[2], fy = inv.r[5], fz = inv.r[8];
    cam.yaw = std::atan2(fx, fz);
    cam.pitch = std::atan2(fy, std::sqrt(fx * fx + fz * fz));
}

void FrameSnapshot::take(const Runtime& rt)
{
    valid = true;
    frame = rt.frameCount;
    std::copy(rt.camera, rt.camera + 12, camera);
    focal = rt.focal;
    drawList = rt.drawList;
    skinDraws = rt.skinDraws;
}

void GameView::draw(const Runtime& rt, int width, int height, const FrameSnapshot* prev, float alpha)
{
    if (std::getenv("HOTD_CLEAR_KEY"))   // debugging (endfloor): magenta where nothing is drawn (coverage scans)
        opt.clear[0] = 1.0f, opt.clear[1] = 0.0f, opt.clear[2] = 1.0f;
    updateCamera(rt);
    // rate: render interpolation between the previous frame (prev) and the newest one, by alpha
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    interpStats = {};
    const bool interp = prev && prev->valid && prev->frame + 1 == rt.frameCount && alpha < 1.0f && !std::getenv("HOTD_NO_INTERP");
    const M34 camCur = invRigid(fromCopro(rt.camera));
    const M34 camPrev = interp ? invRigid(fromCopro(prev->camera)) : camCur;
    bool camCut = false;
    M34 camNow = camCur;   // the camera drawn (camera to world)
    if (interp) {
        camCut = jumped(camPrev, camCur, kCamCutMove, kCamCutCos);
        interpStats.cut = camCut;
        static uint64_t cutLogged = ~0ull;   // debugging (HOTD_INTERP_LIST): the camera cuts seen
        if (camCut && cutLogged != rt.frameCount && std::getenv("HOTD_INTERP_LIST")) {
            cutLogged = rt.frameCount;
            SDL_Log("interp: frame %llu camera cut: %.1f %.1f %.1f -> %.1f %.1f %.1f", (unsigned long long)rt.frameCount,
                    camPrev.t[0], camPrev.t[1], camPrev.t[2], camCur.t[0], camCur.t[1], camCur.t[2]);
        }
        if (!camCut) {
            camNow = slerpRigid(camPrev, camCur, alpha);
            interpStats.camera = true;
            cam.pos[0] = camNow.t[0];
            cam.pos[1] = camNow.t[1];
            cam.pos[2] = camNow.t[2];
            const float fx = camNow.r[2], fy = camNow.r[5], fz = camNow.r[8];
            cam.yaw = std::atan2(fx, fz);
            cam.pitch = std::atan2(fy, std::sqrt(fx * fx + fz * fz));
        }
    }
    const bool blendItems = interp && !camCut;   // across a cut the whole picture is the newest frame's
    // attract2: the prologue (mode 9) ends in Chapter 4's world (kind108_zone_set slot 3): loaded ahead
    if (rt.mem.at<int16_t>(A::G::game_mode) == 9 && chapter_ != 3 && rt.mem.at<int8_t>(A::G::chapter) == 5)
        prefetchExtra(3, rt);
    // perf: the ending (chapter byte 6, Chapter 4's view): its staff roll's first other world, Chapter 3's, loads ahead
    // (finishExtra then starts the next); was a 1.3 s frame each with the arcade models (release hunt: "1.9 s frame")
    if (rt.mem.at<int8_t>(A::G::chapter) == 6 && chapter_ == 3 && extraStage_ < 0 && prefetchStage_ < 0)
        prefetchExtra(2, rt);
    // Survival Mode's cut creatures (Runtime::actorAreaLook): their texture areas from Chapter 4's stage, loaded ahead (the
    // first one seen was a 0.6 s frame)
    if (!rt.actorAreaLook.empty() && chapter_ != 3 && extraStage_ < 0 && prefetchStage_ < 0)
        prefetchExtra(3, rt);
    float focal = blendItems ? prev->focal + (rt.focal - prev->focal) * alpha : rt.focal;
    if (static const char* fs = std::getenv("HOTD_FOCAL_SCALE"); fs && *fs)   // debugging: zoom the view out / in
        focal *= float(std::atof(fs));
    cam.fovY = 2.0f * std::atan(kHalfH / focal);   // the camera's zoom (camera_path_eval)
    // bugs1 (#35, the hit screen shake): hit_screen_shake 0x6350 leaves the view offset in 0x51F610 (x) / 0x51F614 (y),
    // arcade screen pixels (48 frames after a hit: timer x cos(timer x 33.75 degrees), up to 47 lines); every 3D window
    // set-up (the sky task 0x33F50, the world's, ...) adds them to the geometrizer window's centre (0x804000: y + 0x11A,
    // x << 16; the Model 2's y runs down), so the whole 3D picture jumps, the 2D layers stay. It was worked out but never
    // drawn. The PC (0x436EFC) moves its D3D viewport's centre by half that on both axes; our logic is the arcade's.
    // The picture only (the aim stays where it was, as the arcade's gun reads the screen). HOTD_NO_SHAKE=1: off
    static const bool noShake = std::getenv("HOTD_NO_SHAKE") != nullptr;
    const int32_t shakeX = noShake ? 0 : int32_t(rt.mem.u32(0x51F610)), shakeY = noShake ? 0 : int32_t(rt.mem.u32(0x51F614));
    int32_t centreX = std::clamp(shakeX, -96, 96);
    // bugs1 (#34): the ending's 3D window (game_ending 0x352A0 / sky_task_ending 0x34150): its centre x is the middle
    // of its lines 0x53E394 .. 0x53E398 (8 .. 0x1F8, the staff roll narrows it to 8 .. 256: kind120_letterbox_open_frame),
    // not the screen's 256: the staff roll's scenes are centred in the left part beside the sheets (MAME fullgame_us
    // snap 0743). It was drawn centred on the whole screen, the scene's middle under the sheets
    if (rt.mem.at<int16_t>(A::G::game_mode) == 4 && rt.mem.u32(A::G::game_submode) == 8 && rt.mem.at<int8_t>(A::G::chapter) == 6) {
        const int32_t l = int32_t(rt.mem.u32(0x53E394)), r = int32_t(rt.mem.u32(0x53E398));
        if (l >= 0 && r > l && r <= 0x200)
            centreX += (l + r) / 2 - 0x100;
    }
    cam.shift[0] = float(centreX) / kHalfH * float(height) / float(std::max(width, 1));
    cam.shift[1] = -float(std::clamp(shakeY, -96, 96)) / kHalfH;
    // the world task (0x4B300) draws only the current draw zone's placements (BGTB; zone 0 = none). Without this every
    // alternate state (the mansion entrance 0x9E3 / 0x9E4 / 0x9E5, ...) and the pieces of other areas were drawn at once:
    // z-fighting copies and models floating in front of the mansion. The PC world is culled by the same IDs.
    const int zone = std::getenv("HOTD_NO_ZONES") ? -1 : rt.mem.at<uint8_t>(A::G::draw_zone);   // testing: all
    std::vector<DrawItem> items;
    items.reserve(world_.size());
    worldSubs_.clear();
    worldSubs_.reserve(world_.size() + 8);   // (+ the canal's extra water layers below)
    if (!pcStage_) {   // glitch: uv_scroll_model 0x1E290's scrolling water (the canal layers 0x915-0x91C), v + frame % 1024 / 8
        const float f = float(rt.mem.u32(A::G::frame_counter) % 1024u) / 8.0f;
        std::map<uint32_t, std::array<float, 2>> scroll;
        for (uint32_t id = 0x915; id <= 0x91C; ++id) scroll[id] = {0.0f, f};
        renderer.setUVScroll(std::move(scroll));
    }
    const uint32_t plTable = rt.mem.u32(0x5430B4);   // world_init: the chapter's placement table (0 before it ran)
    // the world's light: world_draw_task's zone light (0x5430C0: the camera times the zone's pitch / yaw from table
    // 0xC0C10 applied to (0, 0, 1), i.e. camera space, sent to the geometrizer's light record) -> world space. Was the
    // texture set's captured light for every zone (the Chariot room's walls came out dark). UNVERIFIED (teaser)
    float worldLight[3] = {0, 0, 0};
    bool haveWorldLight = false;
    if (plTable != 0 && !std::getenv("HOTD_SET_LIGHT")) {
        const M34 invc = invRigid(fromCopro(rt.camera));
        const float l[3] = {rt.mem.f32(0x5430C0), rt.mem.f32(0x5430C4), rt.mem.f32(0x5430C8)};
        const float len = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
        if (len > 1e-3f && std::isfinite(len)) {
            haveWorldLight = true;
            for (int i = 0; i < 3; ++i)
                worldLight[i] = (invc.r[i * 3] * l[0] + invc.r[i * 3 + 1] * l[1] + invc.r[i * 3 + 2] * l[2]) / len;
        }
    }
    // title worker: the zone list world_init chose (0x5430B0: the chapter's, or the title screen's 0xC0C20 in mode 2,
    // the same placement table 0xBEF00), when it differs from the chapter's own lists
    std::set<int> memZone;
    bool useMemZone = false;
    if (const uint32_t zl = rt.mem.u32(0x5430B0); zl != 0 && zl != rt.mem.u32(0xC0BB0 + uint32_t(chapter_) * 4) && zone >= 0) {
        useMemZone = true;
        for (uint32_t k = 0; k < 0x32; ++k) {
            const uint8_t i = rt.mem.at<uint8_t>(zl + uint32_t(zone) * 0x32 + k);
            if (i == 0xFF)
                break;
            memZone.insert(int(i));
        }
    }
    // pcfix: the PC's own world file for the chapters past 4 (the how-to-play, the prologue, the ending: view.h pcAlt_)
    const StageData* pcAlt = pcAltFor(rt);
    // endlook: the PC's world task draws only the placement table's entries of the draw zone (world_init 0x491CD0:
    // tables 0x5E963C / 0x5E9658 by chapter byte, the arcade's 0xC0BB0 / 0xC0BD0 entry for entry), each by its ID from
    // the model table; the rest of WORLDn.RBH (the ending's staff-roll scenes 0x907.., ...) only when the game draws it.
    // Drawn whole, WORLD6's scenes stood around the ending's closing room (zone 0: nothing) and hid it
    std::set<uint32_t> altZoneIds;
    const uint32_t zoneLists = rt.mem.u32(0x5430B0);
    const bool altByTable = pcAlt && plTable != 0 && zoneLists != 0 && zone >= 0 && !std::getenv("HOTD_PC_ALT_ALL");
    if (altByTable)
        for (uint32_t k = 0; k < 0x32; ++k) {
            const uint8_t i = rt.mem.at<uint8_t>(zoneLists + uint32_t(zone) * 0x32 + k);
            if (i == 0xFF)
                break;
            altZoneIds.insert(rt.mem.u32(plTable + uint32_t(i) * 0x18));
        }
    walls_.active.clear();   // (leftovers: the ragdolls' walls: the objects drawn this frame; none in the PC's later worlds)
    // endfloor (widescreen, arcade models): a draw zone's list holds what its 4:3 views show, so pieces only the wider
    // view reaches were left out: holes at the picture's sides (Chapter 1's hall walls, ...). The placements of the
    // neighbouring zones (zone +-kWideZoneReach) that the side strips can see are drawn there too, in the strips only
    // (DrawItem::sideOnly: the 4:3 picture stays as it was), none at a placement position the zone already draws (the
    // alternate states: the mansion entrance 0x9E3 / 0x9E4 / 0x9E5, ...). Draw-only. HOTD_WIDE_ZONES=0: off
    static const int wideReach = std::getenv("HOTD_WIDE_ZONES") ? std::atoi(std::getenv("HOTD_WIDE_ZONES")) : 1;
    // vr3 (vrAround, view.h): the eyes look every way: the neighbouring zones' pieces wholly outside the game camera's
    // 4:3 view (beside, behind) are added too, as DrawItem::around (eyes only, depth-tested with the zone), also with
    // PC models (the PC world's pieces by their IDs in the neighbouring zones' lists). Reach: HOTD_VR_ZONES (default 2)
    static const int aroundReach = std::getenv("HOTD_VR_ZONES") ? std::atoi(std::getenv("HOTD_VR_ZONES")) : 2;
    const bool vrWide = vrAround && !pcAlt && !useMemZone && aroundReach > 0 && zone >= 0 && zone < int(zoneIndices_.size());
    const bool wideExtra = vrWide || (!pcStage_ && !pcAlt && !useMemZone && wideReach > 0 && zone >= 0 && zone < int(zoneIndices_.size()) &&
                                        float(width) * 3.0f > float(height) * 4.0f + 1.0f);
    const int reach = vrWide ? std::max(wideReach, aroundReach) : wideReach;
    // a world item in draw zone z's list (the arcade's by its placement index, the PC world's by its ID)
    auto inZone = [&](const DrawItem& it, int z) {
        return it.stage == stage_.get() ? it.placement->romIndex >= 0 && zoneIndices_[size_t(z)].count(it.placement->romIndex) > 0
                                        : zoneIds_[size_t(z)].count(it.placement->id) > 0;
    };
    // a piece's place for "the zone already draws one there": its placement's position (the arcade's; vr3: the PC
    // world's pieces all sit at the origin, their vertices in world space: the middle of their bounds then)
    using Place = std::array<float, 3>;
    auto placeOf = [&](const DrawItem& it) {
        const Placement& p = *it.placement;
        Place o = {p.pos[0], p.pos[1], p.pos[2]};
        if (vrWide && it.stage != stage_.get())
            for (int k = 0; k < 3; ++k) o[size_t(k)] += 0.5f * (it.model->boundsMin[k] + it.model->boundsMax[k]);
        return o;
    };
    std::vector<Place> zonePlaced;
    M34 wideView{};
    float wideHalf = 0.0f;
    if (wideExtra) {
        for (const DrawItem& it : world_)
            if (vrWide ? inZone(it, zone)
                         : it.stage == stage_.get() && it.placement->romIndex >= 0 && zoneIndices_[size_t(zone)].count(it.placement->romIndex))
                zonePlaced.push_back(placeOf(it));
        wideView = fromCopro(rt.camera);
        wideHalf = kHalfH * float(width) / float(height);
    }
    // 0: not drawn; 1: in the side strips (sideOnly); 2: vr3: wholly outside the 4:3 view (around)
    // vr3: a piece of any other zone far away (its nearest point past HOTD_VR_FAR units, default 400: 40 m) too: the
    // landmarks (Chapter 1's mansion seen back from the bridge); never one near you (a zone's alternate dressing of the
    // room you stand in). And one piece per place among the added ones (zones repeat pieces)
    static const float aroundFar = std::getenv("HOTD_VR_FAR") ? float(std::atof(std::getenv("HOTD_VR_FAR"))) : 400.0f;
    std::vector<Place> aroundPlaced;
    auto samePlace = [](const Place& q, const Place& p) {
        return std::abs(q[0] - p[0]) < 0.5f && std::abs(q[1] - p[1]) < 0.5f && std::abs(q[2] - p[2]) < 0.5f;
    };
    auto wideSide = [&](const DrawItem& it) -> int {
        const Placement& p = *it.placement;
        bool near = false, far = false;
        for (int z = std::max(0, zone - reach); z <= std::min(int(zoneIndices_.size()) - 1, zone + reach) && !near; ++z)
            near = z != zone && (vrWide ? inZone(it, z) : zoneIndices_[size_t(z)].count(p.romIndex) > 0);
        if (!near && vrWide && aroundFar > 0.0f)
            for (int z = 0; z < int(zoneIndices_.size()) && !far; ++z)
                far = z != zone && inZone(it, z);
        if (!near && !far)
            return 0;
        const Place here = placeOf(it);
        for (const Place& q : zonePlaced)
            if (samePlace(q, here))
                return 0;
        if (vrWide)
            for (const Place& q : aroundPlaced)
                if (samePlace(q, here))
                    return 0;
        // the model's bounding sphere in camera space (x right, y up, z ahead)
        float lc[3], w[3], c[3];
        for (int k = 0; k < 3; ++k)
            lc[k] = 0.5f * (it.model->boundsMin[k] + it.model->boundsMax[k]);
        const float dx = it.model->boundsMax[0] - it.model->boundsMin[0], dy = it.model->boundsMax[1] - it.model->boundsMin[1],
                    dz = it.model->boundsMax[2] - it.model->boundsMin[2];
        const float r = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
        for (int k = 0; k < 3; ++k)
            w[k] = p.rot[k * 3] * lc[0] + p.rot[k * 3 + 1] * lc[1] + p.rot[k * 3 + 2] * lc[2] + p.pos[k];
        for (int k = 0; k < 3; ++k)
            c[k] = wideView.r[k * 3] * w[0] + wideView.r[k * 3 + 1] * w[1] + wideView.r[k * 3 + 2] * w[2] + wideView.t[k];
        auto out = [&](float a, float half) { return (focal * a - half * c[2]) / std::sqrt(focal * focal + half * half); };
        if (vrWide) {   // the arcade's 4:3 view decides what its zone shows: only what lies wholly outside it is ours
            if (!near && std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) - r < aroundFar)
                return 0;
            int side = 0;
            if (c[2] < -r || out(c[0], kHalfW) > r || out(-c[0], kHalfW) > r || out(c[1], kHalfH) > r || out(-c[1], kHalfH) > r)
                side = 2;
            else if (near && it.stage == stage_.get() &&
                     !(out(c[0], kHalfW) < -r && out(-c[0], kHalfW) < -r && out(c[1], kHalfH) < -r && out(-c[1], kHalfH) < -r))
                side = 1;   // (across the 4:3 edge: the strips' rule)
            if (side != 0) aroundPlaced.push_back(here);
            return side;
        }
        if (c[2] < -r)
            return 0;   // behind the eye
        if (out(c[0], wideHalf) > r || out(-c[0], wideHalf) > r || out(c[1], kHalfH) > r || out(-c[1], kHalfH) > r)
            return 0;   // outside the wide view
        return !(out(c[0], kHalfW) < -r && out(-c[0], kHalfW) < -r) ? 1 : 0;   // (wholly inside the 4:3 picture: nothing for the strips)
    };
    for (const DrawItem& it : pcAlt ? altWorld_ : world_) {
        bool sideOnly = false, around = false;
        if (altByTable) {
            if (!altZoneIds.count(it.placement->id))
                continue;
        } else if (useMemZone) {
            const int ri = it.stage == stage_.get() ? it.placement->romIndex : romIndexOf(it.placement->id, zone);
            if (ri >= 0 && !memZone.count(ri))
                continue;
        } else if (zone >= 0 && zone < int(zoneIndices_.size())) {
            if (it.stage == stage_.get() ? it.placement->romIndex >= 0 && !zoneIndices_[size_t(zone)].count(it.placement->romIndex)
                                         : arcadeWorldIds_.count(it.placement->id) && !zoneIds_[size_t(zone)].count(it.placement->id)) {
                const int side = wideExtra && (vrWide || it.stage == stage_.get()) ? wideSide(it) : 0;
                if (side == 0)
                    continue;
                sideOnly = side == 1, around = side == 2;
            }
        }
        if (!wallOf_.empty() && !pcAlt && !sideOnly && !around)   // (vr3: the eyes' extra pieces are no ragdoll walls)
            if (const auto wo = wallOf_.find(it.placement); wo != wallOf_.end())
                walls_.active.push_back(wo->second);
        DrawItem d = it;
        d.sideOnly = sideOnly;
        d.around = around;
        if (haveWorldLight && it.stage == stage_.get()) {
            d.hasLight = true;
            std::copy(worldLight, worldLight + 3, d.light);
        }
        // world_draw_task (0x4B300, s1 ported its state part): a placement with a model list (+0x14) draws the list's
        // entry its counter (0x5430D0 + i x 4, already advanced this frame) points past; a placement whose +0x10 is not
        // 0xFF fades in through 0x543210 + i x 4 (draw_fade_begin, 0 .. 0xFF). UNVERIFIED (teaser)
        const int ri = it.stage == stage_.get() ? it.placement->romIndex : romIndexOf(it.placement->id, zone);
        if (plTable != 0 && ri >= 0 && ri < 0x50) {
            const uint32_t pl = plTable + uint32_t(ri) * 0x18;
            if (const uint32_t list = rt.mem.u32(pl + 0x14)) {
                const uint32_t k = rt.mem.u32(0x5430D0 + uint32_t(ri) * 4);
                const uint32_t id = rt.mem.u32(list + (k > 0 ? k - 1 : 0) * 4);
                if (const char* la = std::getenv("HOTD_LIST_DRAWN_AT"); la && std::strtoull(la, nullptr, 10) == rt.frameCount)
                    SDL_Log("drawn: placement %d (%#x) model list %#x position %u -> %#x", ri, it.placement->id, list, k, id);
                if (id != 0xFFFFFFFFu && id != it.placement->id) {
                    auto m = it.stage->models.find(id);
                    if (m == it.stage->models.end() || m->second.triangleCount() == 0) {
                        if (pcStage_) logPcGap(id, "world model list");
                        continue;
                    }
                    Placement p = *it.placement;
                    p.id = p.modelKey = id;
                    worldSubs_.push_back(p);
                    d.placement = &worldSubs_.back();
                    d.model = &m->second;
                    d.variant = m->second.preferredVariant;
                }
            }
            if (rt.mem.at<int16_t>(pl + 0x10) != 0xFF)
                d.fade = std::clamp(float(rt.mem.u32(0x543210 + uint32_t(ri) * 4)) / 255.0f, 0.0f, 1.0f);
        }
        items.push_back(d);
        // glitch: world_draw_task's Chapter 1 special case (decompilation.md "World drawing"): placements 0x1C-0x1E (the
        // canal water hashi01_08a-10a) also draw 0x916 / 0x91A, 0x917 / 0x91B, 0x918 / 0x91C (hashi02 / hashi03 _08a-10a,
        // through uv_scroll_model 0x1E290: v scrolled frame_counter % 1024 eighths of a texel). Not drawn, the canal lost
        // its bright checker layer over the dark water (MAME snaps 0059-0060). Arcade models only
        if (chapter_ == 0 && it.stage == stage_.get() && it.stage->source == Source::Arcade && ri >= 0x1C && ri <= 0x1E &&
            !std::getenv("HOTD_NO_CANAL_LAYERS")) {
            for (uint32_t extra : {0x916u + uint32_t(ri - 0x1C), 0x91Au + uint32_t(ri - 0x1C)}) {
                auto m = it.stage->models.find(extra);
                if (m == it.stage->models.end() || m->second.triangleCount() == 0) continue;
                Placement p = *d.placement;
                p.id = p.modelKey = extra;
                worldSubs_.push_back(p);
                DrawItem e = d;
                e.placement = &worldSubs_.back();
                e.model = &m->second;
                e.variant = d.variant;
                items.push_back(e);
            }
        }
    }
    // game over (mode 5, gameover): the original's task groups draw no world (game_over_start reset the heap), only the
    // fallen bodies (sub-mode 1) and the map's walkers (sub-modes 2 / 3) through the draw list
    if (rt.mem.at<uint16_t>(A::G::game_mode) == 5)
        items.clear();
    // title worker: the attract modes without a world task (1: WINNERS DON'T USE DRUGS, 0 / 6 not ported)
    if (const uint16_t m = rt.mem.at<uint16_t>(A::G::game_mode); m == 0 || m == 1 || m == 6 || m == 7)
        items.clear();
    // title-2: the name entry (mode 4 sub-modes 9 / 10) draws no world either, only its letter models
    const bool nameEntry = rt.mem.at<uint16_t>(A::G::game_mode) == 4 && rt.mem.u32(A::G::game_submode) >= 9;
    if (nameEntry)
        items.clear();
    // ... and a placement table of another stage (the prologue's last scene, kind108_zone_set to Chapter 4's world):
    // title-2: that stage's world, loaded on first use (ensureExtra), its draw zone's placements, the texture set of
    // the area the scripts loaded (kind107: area 12) UNVERIFIED (teaser)
    // the custom Blood Color's blood rows (tintBloodRows): a changed colour rebuilds the translations below
    const int xlatBlood = !rainbowBloodSettings().on && !pcStage_ ? bloodCustom_ : -1;
    if (xlatBlood != xlatBlood_) {
        xlatBlood_ = xlatBlood;
        colorRowsSerial_ = rt.colorRowsSerial - 1;
        extraRowsSerial_ = ~0u;
    }
    extraVariant_ = -1;   // (only while that world is drawn: the game's models come from the chapter's stage again)
    if (plTable != 0 && plTable != rt.mem.u32(0xC0BD0 + uint32_t(chapter_) * 4)) {
        items.clear();
        int other = -1;
        for (int s = 0; s < 4; ++s)
            if (plTable == rt.mem.u32(0xC0BD0 + uint32_t(s) * 4)) other = s;
        if (other >= 0 && !std::getenv("HOTD_NO_EXTRA_STAGE") && ensureExtra(other, rt)) {
            int extraVariant = -1;
            if (extra_)
                for (size_t i = 0; i < extra_->textureSets.size(); ++i)
                    if (rt.textureArea > 0 && extra_->textureSets[i].romArea == rt.textureArea) extraVariant = int(i);
            extraVariant_ = extraVariant;
            // the colour rows the scripts swapped in (color_row_set), into the extra set's translation as for the chapter's
            if (extra_ && !pcExtra_ && extraVariant >= 0 && (rt.colorRowsSerial != extraRowsSerial_ || extraVariant != extraRowsSet_) &&
                !std::getenv("HOTD_NO_COLOR_ROWS")) {
                extraRowsSerial_ = rt.colorRowsSerial;
                extraRowsSet_ = extraVariant;
                std::vector<std::pair<int, int>> swaps(rt.colorRows.begin(), rt.colorRows.end());
                std::vector<uint8_t> xlat;
                source_->colorXlat(extra_->textureSets[size_t(extraVariant)].romArea, swaps, xlat);
                tintBloodRows(xlat, rt.bloodRows, xlatBlood_);
                renderer.updateXlat(*extra_, extraVariant, xlat);
            }
            // smallfix: the name entry's placement table is still the ending's: its letters take this stage's texture set
            // (extraVariant_), but its world (Chapter 4's hall) is not drawn behind them (the arcade's name entry draws no world)
            for (const DrawItem& it : extraWorld_) {
                if (nameEntry)
                    break;
                const int ri = it.stage == extra_.get() ? it.placement->romIndex : [&] {
                    auto r = extraRomIndex_.find(it.placement->id);
                    return r == extraRomIndex_.end() ? -1 : r->second;
                }();
                if (useMemZone && !memZone.empty() && ri >= 0 && !memZone.count(ri))
                    continue;
                DrawItem d = it;
                if (d.stage == extra_.get() && extraVariant >= 0) {
                    const std::vector<int>& ok = d.model->validVariants;
                    if (ok.empty() || std::find(ok.begin(), ok.end(), extraVariant) != ok.end())
                        d.variant = extraVariant;
                }
                if (haveWorldLight) {
                    d.hasLight = true;
                    std::copy(worldLight, worldLight + 3, d.light);
                }
                items.push_back(d);
            }
        }
    }
    // texture set: the area the scripts loaded last (TEXCHG), the chapter's first set before that. (It was the nearest
    // replay zone point to the camera, which flipped between sets as the camera moved.)
    int variant = stage_->textureSets.empty() ? -1 : 0;
    for (size_t i = 0; i < stage_->textureSets.size(); ++i)
        if (rt.textureArea > 0 && stage_->textureSets[i].romArea == rt.textureArea) variant = int(i);
    variant_ = variant;   // (vr: placedItem's texture set)
    // the colour rows the event scripts swapped in (color_row_set) into the current set's colour translation (s1)
    if (rt.colorRowsSerial != colorRowsSerial_ && !std::getenv("HOTD_NO_COLOR_ROWS") && variant >= 0 && stage_->textureSets[size_t(variant)].romArea > 0) {
        colorRowsSerial_ = rt.colorRowsSerial;
        // Runtime::colorRows is keyed by the translation table's base value: color_row_set stores 2 x row + 1 (the shadow
        // row 0x512D20 + row x 0x100 is filled from the area list entry `row`; courtyard texset rows 7, 9, 13, 21 = game
        // rows 3, 4, 6, 10). The conversion happens ONLY in color_row_set (chapter.cpp); do not convert again here.
        std::vector<std::pair<int, int>> swaps;
        for (const auto& [row, entry] : rt.colorRows)
            swaps.push_back({row, entry});
        std::vector<uint8_t> xlat;
        source_->colorXlat(stage_->textureSets[size_t(variant)].romArea, swaps, xlat);
        tintBloodRows(xlat, rt.bloodRows, xlatBlood_);
        renderer.updateXlat(*stage_, variant, xlat);
    }
    // the arcade's texture set (TEXCHG area) applies to arcade models only: the PC stage has one texture set (BIGGER.VMC
    // pages) and its meshes have no variants
    auto pickVariant = [&](DrawItem& it) {
        if (it.stage != stage_.get()) return;
        const std::vector<int>& ok = it.model->validVariants;
        if (variant >= 0 && (ok.empty() || std::find(ok.begin(), ok.end(), variant) != ok.end()))
            it.variant = variant;
    };
    for (DrawItem& it : items)
        pickVariant(it);
    placements_.clear();
    placements_.reserve(2 * (rt.drawList.size() + overlay.size()) + stage_->skies.size() + rt.skinDraws.size() + 16);
    // the models the game drew: camera space -> world through the view's inverse
    const M34 inv = invRigid(fromCopro(rt.camera));
    struct PullTop { size_t item; uint32_t body; float top; float lo[3], hi[3]; };
    std::vector<PullTop> pullTops;
    struct HeldDecal { size_t item; float c[3]; uint32_t model; };   // sinkvr: a flat model outside the shadows (the photograph)
    std::vector<HeldDecal> heldDecals;
    auto addModel = [&](uint32_t id, const M34& w, const DrawModel& dm) {
        // model source PC: the PC's model of the same ID (THOTD.EXE draws by ID too: geo_draw_model 0x43D7F8 ->
        // DAT_00d53080[id], filled by ID from CREAT1 / CREAT2 / WORLDn.RBH by 0x419CAC / 0x419CF4; an empty entry draws
        // nothing), never the arcade's
        if (id == 0)
            return;   // model 0: nothing (both versions)
        // developer (clips): HOTD_DEV_ONLY_TYPE=t draws no body but creatures of type t (the room stays; the people go)
        if (static const char* only = std::getenv("HOTD_DEV_ONLY_TYPE"); only && dm.body &&
            (!rt.mem.mapped(dm.body + 0xCC) || rt.mem.at<int16_t>(dm.body + 0xCC) != std::atoi(only)))
            return;
        // the sky comes from the game's sky_task (s1, chapter.cpp: SKYTB's sky_index / SKYKD's sky_mode, the dome turned
        // and lifted by its table row), no longer from the texture area's sky list (that drew a second, unlifted copy)
        if (dm.background && (!sky || (std::getenv("HOTD_SKY_ONLY") && std::strtoul(std::getenv("HOTD_SKY_ONLY"), nullptr, 16) != id)))
            return;
        const StageData* from = pcStage_ ? pcStage_.get() : stage_.get();
        // pcfix: past chapter 4 the PC's table holds that world file's objects (they win, as the world's over the
        // creatures'); the chapter's own world objects are not in it (the creatures still come from CREAT1 / CREAT2)
        if (pcAlt) {
            auto am = pcAlt->models.find(id);
            if (am != pcAlt->models.end() && am->second.triangleCount() > 0)
                from = pcAlt;
            else if (pcWorldIds_.count(id)) {
                logPcGap(id, "drawn by the game, not in this chapter's PC world file");
                return;
            }
        }
        // title-2: while another stage's world is drawn (the prologue's last scene), the arcade models it loaded
        if (!pcStage_ && extra_ && extraVariant_ >= 0 && extra_->models.count(id) && extra_->models.at(id).triangleCount() > 0)
            from = extra_.get();
        // the cut-scene viewer's actors whose textures are in one area only (Runtime::actorAreas: Dr. Curien, the zombie
        // Sophie): that area's texture set: the scene's own when it is in that area, else from Chapter 4's stage (it has
        // every area; loaded once as the prologue's other world, ensureExtra) with the colour rows the game gives them
        // there (the Gallery's look: galleryLook). The scene's own sets are left as they are
        int actorSet = -1;
        if (!pcStage_ && !rt.actorAreas.empty())
            if (const auto aa = rt.actorAreas.find(id); aa != rt.actorAreas.end()) {
                for (size_t i = 0; i < stage_->textureSets.size(); ++i)
                    if (aa->second == rt.textureArea && stage_->textureSets[i].romArea == aa->second)
                        from = stage_.get(), actorSet = int(i);
                if (actorSet < 0 && ensureExtra(3, rt) && extra_ && extra_->models.count(id))
                    for (size_t i = 0; i < extra_->textureSets.size(); ++i)
                        if (extra_->textureSets[i].romArea == aa->second) {
                            from = extra_.get(), actorSet = int(i);
                            if (actorRowsFor_ != extra_.get())
                                actorRowsFor_ = extra_.get(), actorRowsSets_.clear();
                            if (actorRowsSets_.insert(actorSet).second) {
                                // (Survival Mode's cut creatures: their own looks, Runtime::actorAreaLook: area -> type)
                                const auto al = rt.actorAreaLook.find(aa->second);
                                const GalleryLook look = al != rt.actorAreaLook.end() ? galleryLook(al->second)
                                                         : aa->second == 11 ? galleryLook(60) : aa->second == 12 ? galleryLook(78) : aa->second == 2 ? galleryLook(67) : GalleryLook{};
                                std::vector<uint8_t> xlat;
                                source_->colorXlat(aa->second, parseColorRows(look.rows), xlat);
                                renderer.updateXlat(*extra_, actorSet, xlat);
                                SDL_Log("view: actor models of area %d drawn from chapter 4's set %d (rows \"%s\", chapter's area %d)",
                                        aa->second, actorSet, look.rows ? look.rows : "", rt.textureArea);
                            }
                        }
                // pmfix (user 2026-10-28: an extra's textures wrong in PC Mode): when the area's set is not found the
                // model is drawn in the chapter's set (wrong pages): said once per model, for the next test's log
                if (actorSet < 0 && actorMissLogged_.insert(id).second)
                    SDL_Log("view: actor model %#x (area %d) has no texture set: extra stage %d loaded %d, model there %d",
                            id, aa->second, extraStage_, int(extra_ != nullptr), int(extra_ && extra_->models.count(id)));
            }
        // the Hermit Fly Head mod (game/cosmods.h, user 2026-11-09): the arcade models' Hermit head drawn as the game's
        // unused second head (draw only), from the same model source: the 1998 PC files have no 0x1AC6, so the PC
        // models keep their head
        if (id == kHermitHeadModel && hermitHeadSettings().on)
            if (auto fh = from->models.find(kHermitFlyHeadModel); fh != from->models.end() && fh->second.triangleCount() > 0)
                id = kHermitFlyHeadModel;
        auto m = from->models.find(id);
        if (m == from->models.end() || m->second.triangleCount() == 0) {
            if (pcStage_) logPcGap(id, "drawn by the game");
            else if (missingModels_.insert(id).second)
                SDL_Log("model %#x drawn by the game is not loaded", id);
            return;
        }
        if (placements_.size() == placements_.capacity())
            return;   // the reserve keeps the pointers stable
        Placement p;
        p.id = p.modelKey = id;
        std::copy(w.r, w.r + 9, p.rot);
        std::copy(w.t, w.t + 3, p.pos);
        // pcfix: a PC world-file object (WORLDn.RBH) has its vertices in world space, placed already (the arcade's are
        // local to the placement table's entry): THOTD.EXE draws one the game draws itself (kind114_model_timed 0x440E6C,
        // the prologue's room 0xCF3: arcade 0x16210 sets the room's matrix, the PC only the camera's) where the world
        // has it. Here the game's matrix is taken relative to the arcade placement: W x P^-1 (P: translation, the
        // world's rotation is the identity). Without it the prologue's room landed a second offset away: G over black
        // (user play-test 2026-10-18 f1865-f2012)
        if (from != stage_.get() && std::any_of(from->placements.begin(), from->placements.end(),
                                                [id](const Placement& q) { return q.id == id; })) {
            auto ap = arcadePlacementById_.find(id);
            if (ap != arcadePlacementById_.end()) {
                const Placement& q = *ap->second;
                float r[9];
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)   // W.r x Q.r^T
                        r[i * 3 + j] = w.r[i * 3] * q.rot[j * 3] + w.r[i * 3 + 1] * q.rot[j * 3 + 1] + w.r[i * 3 + 2] * q.rot[j * 3 + 2];
                std::copy(r, r + 9, p.rot);
                for (int i = 0; i < 3; ++i)
                    p.pos[i] = w.t[i] - (r[i * 3] * q.pos[0] + r[i * 3 + 1] * q.pos[1] + r[i * 3 + 2] * q.pos[2]);
                if (pcRebased_.insert(id).second)
                    SDL_Log("PC world object %#x drawn by the game (frame %llu): placed relative to its world placement", id,
                            (unsigned long long)rt.frameCount);
            } else if (pcRebased_.insert(id).second)
                SDL_Log("PC world object %#x drawn by the game (frame %llu) has no arcade placement: drawn as the game places it",
                        id, (unsigned long long)rt.frameCount);
        }
        placements_.push_back(p);
        DrawItem d{from, &m->second, &placements_.back(), m->second.preferredVariant};
        pickVariant(d);
        // developer (clips): HOTD_DEV_TYPE_AREA="type:area" draws the creatures of that type with the texture set of that
        // texture area (a creature in another area of its chapter: the Chariot's area-4 pictures in Curien's lab, area 12)
        if (static const char* ta = std::getenv("HOTD_DEV_TYPE_AREA"); ta && dm.body && from == stage_.get() &&
            rt.mem.mapped(dm.body + 0xCC) && rt.mem.at<int16_t>(dm.body + 0xCC) == std::atoi(ta) && std::strchr(ta, ':'))
            for (size_t i = 0; i < stage_->textureSets.size(); ++i)
                if (stage_->textureSets[i].romArea == std::atoi(std::strchr(ta, ':') + 1))
                    d.variant = int(i);
        if (from == extra_.get()) {
            const std::vector<int>& ok = d.model->validVariants;
            if (ok.empty() || std::find(ok.begin(), ok.end(), extraVariant_) != ok.end())
                d.variant = extraVariant_;
        }
        if (actorSet >= 0)
            d.variant = actorSet;
        d.background = dm.background;   // the game's sky (sky_task, s1)
        // in front of coplanar world surfaces: the Model 2 sorts polygons (no depth buffer), so the blood pools and
        // other decals the game draws onto the ground after the world always win there; with a depth buffer they
        // z-fought the ground (blood flickering frame to frame). 1e-5 in clip z: about 1 unit at 500 units' distance
        d.depthBias = 1e-5f;
        // the staff roll's last shot (the ending, chapter byte 6): the car (0x950, its open door 0x951) stands in the gate
        // with its front bumper a few units past the right pillar's face. The Model 2 sorts polygons: in MAME (snap
        // fullgame_us 0776) the pillar covers the car's right side; our depth test drew the bumper over the pillar
        // (user 2026-10-26: "the car pokes out at the end of the credits"). Its depth is taken 8 units further (the
        // picture's place kept): the pillars win, the floor under it still loses (hudfix4)
        if ((dm.model == 0x950 || dm.model == 0x951) && rt.mem.at<int8_t>(A::G::chapter) == 6) {
            static const char* push = std::getenv("HOTD_CAR_PUSH");   // (testing: another distance)
            // (leftovers: the PC's car (WORLD6's 0x950 / 0x951) is longer at the front: 8 still left its bumper over
            // the pillar; 20 hides it there and keeps the number plate over the floor; 28+ the floor starts to win)
            d.push = push ? float(std::atof(push)) : pcStage_ ? 20.0f : 8.0f;
        }
        // Dr Curien's death (Chapter 4, before the Magician's fight): lying on his front, his shoulders and upper arms
        // (kyubkata* / kyubude*) cut through his coat's back (the chest, kyubmune00a 0x1D0B) from inside. The Model 2
        // sorts polygons and the nearer coat back is drawn over them (MAME fullgame_us 0653 / 0654: a whole back); our
        // depth test showed the lining and the shoulder pieces through it (user 2026-10-26: "Dr Curien's inside
        // textures visible in his dying animation"). Lying (the chest's own x axis, upright when he stands, flat on the
        // floor): its depth is taken 2 units nearer (the picture's place kept), so the back wins (hudfix4)
        if (dm.model == 0x1D0B && std::abs(w.r[3]) < 0.5f) {
            static const char* pull = std::getenv("HOTD_CURIEN_PULL");   // (testing: another distance)
            d.push = -(pull ? float(std::atof(pull)) : 2.0f);
        }
        d.fade = std::clamp(drawFade(dm, 0), 0.0f, 1.0f);
        d.flat = drawFlat(dm, 0);
        d.ramp = dm.ramp;
        // vr4: the VR frame's kinds (assets.h DrawItem::vrKind; read only, the flat draw ignores it): the shot's flash /
        // spark by their tasks, a body's parts (not the blood bursts / drips its body draws: by model, as hideBlood)
        if (!dm.shadow && !dm.background) {
            const uint32_t rtn = dm.task != 0 ? rt.mem.u32(dm.task) : 0;
            const bool blood = (dm.model >= 7 && dm.model <= 0x23) || (dm.model >= 0x46B && dm.model <= 0x4A3) ||
                               (dm.model >= 0x1AF && dm.model <= 0x1BF);
            d.vrKind = rtn == A::F::shot_effect_flash ? 2
                       : rtn == A::F::shot_effect_debris && (dm.model == 0x660 || dm.model == 0x661) ? 3
                       : (dm.body != 0 || dm.bossPart != 0) && !blood ? 1 : 0;
        }
        // pcsky (GitHub #55): the PC models keep the storm sky after the Magician explodes, as the real PC game does:
        // THOTD.EXE's death 0x4D1EE4 steps 0x12 / 0x13 are empty (the arcade's color_row_set rows 0 / 6 are not
        // there) and Chapter 4 event 4 has no SKYKD / SKYTB / TEXCHG after the fight (sky index 5, PN_skyuvst4a 0x1D20)
        // cosmods: Rainbow Blood's creature colour, for the body's own parts only (a splash or a pool drawn with a body
        // took that frame's body: its colour jumped every frame, user's clip 2026-11-03)
        d.bloodHue = dm.shadow ? -1.0f : dm.part != 0 ? creatureBloodHue(dm.body) : creatureBloodKeepsRows(dm.body) ? 6.0f : -1.0f;
        if (rt.pcModels)   // (v1.3.1: each afterimage its own colour; the PC models: their texels tinted)
            d.pcTint = magicianAfterimageItemWord(rt, dm.model, dm.task, true);
        else
            d.palOver = magicianAfterimageItemWord(rt, dm.model, dm.task);
        // (rainbow2: the Chariot draws its body without parts: its blood rows kept there too, creatureBloodKeepsRows)
        // decals: flat models (the water splashes picha00a-19a, 0x9F2-0xA05, all at the floor height) and squashed ones (the
        // _DD shadows: a zero vertical row) lie coplanar with the ground and with each other; drawn after the opaque pass
        // without depth writes, overlaps go by draw order instead of float noise (they flickered as the camera moved)
        {
            const Model& mo = m->second;
            float ext[3], big = 0, small = 1e30f;
            for (int i = 0; i < 3; ++i) {
                ext[i] = mo.boundsMax[i] - mo.boundsMin[i];
                big = std::max(big, ext[i]);
                small = std::min(small, ext[i]);
            }
            const float* r = w.r;
            const float det = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) + r[2] * (r[3] * r[7] - r[4] * r[6]);
            d.decal = small <= 0.01f * big || std::abs(det) < 1e-4f;
        }
        // polish2: the game's models stand on the floor height; their feet win over ground a little above it (the
        // Model 2's polygon sort, renderer.cpp uFloorPull)
        if (!d.background) {
            d.floorY = rt.mem.f32(A::G::floor_y);
            // overdraw (2026-10-28): PC models get no standing / lying pull. The 1998 PC draws with a plain z-buffer
            // (no polygon sort to stand in for), and the pull (up to 14 units toward the camera at a low view) put
            // feet and legs over railings, door frames and the steps in front of them (user screenshots 10-27, PC
            // models: the researcher's legs over a railing, f29164). Floor decals keep their 0.3 dip.
            // HOTD_PC_FLOOR_PULL=1: the old pull (comparisons)
            static const bool pcPull = std::getenv("HOTD_PC_FLOOR_PULL") != nullptr;
            const bool pcModel = d.stage->source == Source::PC && !pcPull;
            d.floorPull = d.decal || dm.shadow ? 2 : pcModel ? 0 : 1;
            if (d.floorPull == 1) {
                // only a model that stands (its bounds reach 6+ units over the floor): a body lying on the ground (the
                // first researcher, event 0, holding the photograph: his arm and hand are all within 3 units of the
                // floor) would get every low part pulled over his own face
                const Model& mo = m->second;
                float top = -1e30f, bot = 1e30f;
                PullTop pt{items.size(), dm.body ? dm.body : ~uint32_t(0) - uint32_t(pullTops.size()), 0, {1e30f, 1e30f, 1e30f},
                           {-1e30f, -1e30f, -1e30f}};
                for (int c = 0; c < 8; ++c) {
                    const float x = (c & 1) ? mo.boundsMax[0] : mo.boundsMin[0], y = (c & 2) ? mo.boundsMax[1] : mo.boundsMin[1],
                          z = (c & 4) ? mo.boundsMax[2] : mo.boundsMin[2];
                    top = std::max(top, w.r[3] * x + w.r[4] * y + w.r[5] * z + w.t[1]);
                    bot = std::min(bot, w.r[3] * x + w.r[4] * y + w.r[5] * z + w.t[1]);
                    for (int k = 0; k < 3; ++k) {
                        const float v = w.r[k * 3] * x + w.r[k * 3 + 1] * y + w.r[k * 3 + 2] * z + w.t[k];
                        pt.lo[k] = std::min(pt.lo[k], v), pt.hi[k] = std::max(pt.hi[k], v);
                    }
                }
                pt.top = top;
                // feet4: HOTD_FEET_DEBUG=1 lists each pulled part's world height range against the floor
                if (std::getenv("HOTD_FEET_DEBUG"))
                    std::fprintf(stderr, "feet body %08x model %x floor %.2f bot %.2f top %.2f\n", dm.body, dm.model, d.floorY, bot, top);
                // feet2: by the whole body (every part body_draw drew), not by each part model: a standing creature's
                // per-part models (foot, shin) never reach 6 units on their own, so the opening's zombie and researcher
                // lost the pull and their feet sank into the kerb again. Decided after the loop (pullTops)
                pullTops.push_back(pt);
            } else if (d.floorPull == 2 && !dm.shadow) {
                const Model& mo = m->second;
                HeldDecal hd{items.size(), {0, 0, 0}, dm.model};
                const float c[3] = {(mo.boundsMin[0] + mo.boundsMax[0]) * 0.5f, (mo.boundsMin[1] + mo.boundsMax[1]) * 0.5f,
                                    (mo.boundsMin[2] + mo.boundsMax[2]) * 0.5f};
                for (int k = 0; k < 3; ++k) hd.c[k] = w.r[k * 3] * c[0] + w.r[k * 3 + 1] * c[1] + w.r[k * 3 + 2] * c[2] + w.t[k];
                heldDecals.push_back(hd);
            }
        }
        // the geometrizer's light when the game drew it (camera space, R4's light records) -> world space
        const float* l = dm.light;
        const float len = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
        if (len > 1e-3f) {
            d.hasLight = true;
            for (int i = 0; i < 3; ++i)
                d.light[i] = (inv.r[i * 3] * l[0] + inv.r[i * 3 + 1] * l[1] + inv.r[i * 3 + 2] * l[2]) / len;
        }
        items.push_back(d);
    };
    // debugging: HOTD_TRACE_DRAWN=<file>: every frame's game-drawn models (frame, ID, world position) appended to the file
    static FILE* trace = [] {
        const char* p = std::getenv("HOTD_TRACE_DRAWN");
        return p ? std::fopen(p, "w") : nullptr;
    }();
    // debugging: HOTD_TRACE_TYPE_ROWS=<file>: per creature type drawn, the texture area and the colour rows the scripts
    // had swapped in (GGCOL / GGSET), counted in frames; the file is rewritten every 300 frames (the Gallery's colour
    // table, src/game/gallery.cpp kLooks, came from these)
    if (static const char* typeRows = std::getenv("HOTD_TRACE_TYPE_ROWS"); typeRows && !pcStage_) {
        static std::map<std::string, std::pair<uint64_t, uint64_t>> seen;   // key -> (frames, first frame)
        std::set<int> types;
        for (const DrawModel& dm : rt.drawList)
            if (dm.body != 0)
                types.insert(rt.mem.at<int16_t>(dm.body + 0xCC));
        for (int t : types) {
            std::string k = "type " + std::to_string(t) + " chapter " + std::to_string(int(rt.mem.at<int8_t>(A::G::chapter))) + " area " + std::to_string(rt.textureArea) + " rows";
            for (const auto& [row, entry] : rt.colorRows)
                k += " " + std::to_string(row) + ":" + std::to_string(entry);
            auto& s = seen[k];
            if (s.first++ == 0) s.second = rt.frameCount;
        }
        if (rt.frameCount % 300 == 0)
            if (FILE* f = std::fopen(typeRows, "w")) {
                for (const auto& [k, n] : seen)
                    std::fprintf(f, "%s first %llu frames %llu\n", k.c_str(), (unsigned long long)n.second,
                                 (unsigned long long)n.first);
                std::fclose(f);
            }
    }
    // debugging: HOTD_TRACE_TYPE_MOTIONS=<file>: per creature type drawn, the motions its bodies played (body +0xD0, and
    // +0xFE while the second motion is on), counted in frames; rewritten every 300 frames (the Gallery's motion lists,
    // src/game/gallery.cpp, were checked against these)
    if (static const char* typeMotions = std::getenv("HOTD_TRACE_TYPE_MOTIONS"); typeMotions && !pcStage_) {
        static std::map<std::pair<int, int>, uint64_t> seen;   // (type, motion) -> frames
        std::set<std::pair<int, int>> now;
        for (const DrawModel& dm : rt.drawList)
            if (dm.body != 0) {
                const int t = rt.mem.at<int16_t>(dm.body + 0xCC);
                now.insert({t, rt.mem.at<int16_t>(dm.body + 0xD0)});
                if (rt.mem.at<uint8_t>(dm.body + 0x3C6) & 0x20) now.insert({t, rt.mem.at<int16_t>(dm.body + 0xFE)});
            }
        for (const auto& k : now) ++seen[k];
        if (rt.frameCount % 300 == 0)
            if (FILE* f = std::fopen(typeMotions, "w")) {
                for (const auto& [k, n] : seen)
                    std::fprintf(f, "type %d motion %d frames %llu\n", k.first, k.second, (unsigned long long)n);
                std::fclose(f);
            }
    }
    // the game's models, then the view's own overlay models (display: the players' gun marks)
    std::vector<const DrawModel*> drawn;
    drawn.reserve(rt.drawList.size() + overlay.size());
    for (const DrawModel& dm : rt.drawList)
        drawn.push_back(&dm);
    for (const DrawModel& dm : overlay)
        drawn.push_back(&dm);
    // a drawn model's world matrix seen from the camera pose I (camera to world)
    auto worldOf = [&](const DrawModel& dm, const M34& I) {
        M34 w = mul(I, fromCopro(dm.m));
        if (dm.shadow) {
            // the part's shadow (part_draw's shadow pass): projected along the shadow light onto the floor shadow_setup
            // got: its world-space projection (copy 0x51FDF0: x' = x + m3 (y - floor) .., y' = floor), the part's world
            // matrix premultiplied by it (the original: stored matrix 3 = that projection times the camera;
            // UNVERIFIED (teaser)). Not set (zero) or HOTD_SHADOW_SQUASH: squashed straight down. Flat: a decal
            const float* s = dm.shadowM;
            if (s[0] != 0.0f && s[8] != 0.0f && std::abs(s[10] - dm.shadowFloor) < 1e-3f && !std::getenv("HOTD_SHADOW_SQUASH")) {
                w = mul(fromCopro(s), w);
            } else {
                w.r[3] = w.r[4] = w.r[5] = 0.0f;
                w.t[1] = dm.shadowFloor;
            }
        }
        if (pcStage_) {
            const uint32_t id = dm.model;
            // the shot flash (shot_effect_flash, baaba00a-09a 0x2D-0x36, player 1's frames: PC table 0x5F2EC0[0] = 0x2D
            // as the arcade's 0xCA738): THOTD.EXE 0x50BE08 scales it by 0.08 (0x5A7250), the arcade's 0x95140 by 0.1
            // (pcpolish: ours drew the PC's flat flash models 25 % too big)
            if (id >= 0x2D && id <= 0x36)
                for (float& r : w.r) r *= 0.8f;
            // the blood mark over a shot hostage / researcher (0x3F5 / 0x3F6): the PC (0x447F10 / 0x4480A0, names.tsv calls the second
            // body_pose_set_rec, it is the hostage form) places it at
            // camera depth 1.1 (0x3F8CCCCD), the arcade (0x1DCD0 / 0x1DB30) at 1.0, the same scale: 1 / 1.1 the size
            if (id == 0x3F5 || id == 0x3F6) {
                M34 cm = fromCopro(dm.m);
                for (float& t : cm.t) t *= 1.1f;
                w = mul(I, cm);
            }
        }
        return w;
    };
    // rate: the previous frame's models by key (task, model, body, shadow) in their order; the n-th model of a key now
    // is matched with the n-th of the same key then
    using Key = std::array<uint32_t, 4>;
    auto keyOf = [](const DrawModel& d) { return Key{d.task, d.model, d.body, d.shadow ? 1u : 0u}; };
    // ... then the models left over, by task + body + shadow alone: the n-th left over now with the n-th left over then,
    // when the task left as many over in both frames. A flip-book (the splashes' frames 0x9F2..0xA05: one model a frame
    // whose ID changes) and a changing model among a body's fixed parts (hangcheck: the Hangedman's wings, 0x1401 + his
    // +0x50 % 100 at joint 1, a new ID every frame: they were drawn unblended, up to a tick ahead of his blended body)
    using Loose = std::array<uint32_t, 3>;
    auto looseOf = [](const DrawModel& d) { return Loose{d.task, d.body, d.shadow ? 1u : 0u}; };
    std::vector<size_t> matchOf;   // rt.drawList[i] -> prev->drawList index (SIZE_MAX: none)
    if (blendItems) {
        const size_t nn = rt.drawList.size(), np = prev->drawList.size();
        matchOf.assign(nn, SIZE_MAX);
        std::map<Key, std::vector<size_t>> prevByKey;
        std::map<Key, size_t> keyUsed;
        for (size_t i = 0; i < np; ++i)
            prevByKey[keyOf(prev->drawList[i])].push_back(i);
        std::vector<char> prevUsed(np, 0);
        for (size_t i = 0; i < nn; ++i) {
            const Key k = keyOf(rt.drawList[i]);
            const size_t n = keyUsed[k]++;
            auto pk = prevByKey.find(k);
            if (pk != prevByKey.end() && n < pk->second.size()) {
                matchOf[i] = pk->second[n];
                prevUsed[pk->second[n]] = 1;
            }
        }
        std::map<Loose, std::vector<size_t>> restNow, restPrev;
        for (size_t i = 0; i < nn; ++i)
            if (matchOf[i] == SIZE_MAX && rt.drawList[i].task != 0)
                restNow[looseOf(rt.drawList[i])].push_back(i);
        for (size_t i = 0; i < np; ++i)
            if (!prevUsed[i] && prev->drawList[i].task != 0)
                restPrev[looseOf(prev->drawList[i])].push_back(i);
        for (const auto& [l, v] : restNow)
            if (auto p = restPrev.find(l); p != restPrev.end() && p->second.size() == v.size())
                for (size_t j = 0; j < v.size(); ++j)
                    matchOf[v[j]] = p->second[j];
    }
    for (size_t di = 0; di < drawn.size(); ++di) {
        const DrawModel& dm = *drawn[di];
        // bugs1 (#22, "the gun flash jumps around the crosshair when shooting fast, mostly while the camera moves"): the
        // shot flash (shot_effect_flash 0x95140: coprocessor 0x21, camera space, at the aim point for 10 frames) is glued
        // to the screen in the original. Placed by the newest frame's camera while the picture is drawn from the camera
        // blended between two frames (Smooth Motion), a new flash stood off the aim by the camera's move since the frame
        // before (a matched one was blended in the world: off by the turn), each shot somewhere else. Like the view's
        // overlay: placed from the camera drawn, the newest frame's place, never blended
        const bool flash = di < rt.drawList.size() && dm.task != 0 && !dm.shadow && rt.mem.u32(dm.task) == A::F::shot_effect_flash;
        const bool own = di >= rt.drawList.size() || flash;   // the view's overlay: camera space, follows the camera drawn
        M34 w = worldOf(dm, own ? camNow : inv);
        if (trace)
            std::fprintf(trace, "%llu %X %.2f %.2f %.2f%s r %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n",
                         (unsigned long long)rt.frameCount, dm.model, w.t[0], w.t[1], w.t[2], dm.shadow ? " shadow" : "", w.r[0],
                         w.r[1], w.r[2], w.r[3], w.r[4], w.r[5], w.r[6], w.r[7], w.r[8]);
        if (blendItems && !own) {
            static const bool listInterp = std::getenv("HOTD_INTERP_LIST") != nullptr;   // debugging: what is not blended
            const size_t match = matchOf[di];
            if (match == SIZE_MAX) {
                ++interpStats.unmatched;
                if (listInterp)
                    SDL_Log("interp: frame %llu unmatched model %#x task %#x body %#x shadow %d",
                            (unsigned long long)rt.frameCount, dm.model, dm.task, dm.body, int(dm.shadow));
            } else {
                const M34 wp = worldOf(prev->drawList[match], camPrev);
                if (jumped(wp, w, kItemJumpMove, kItemJumpCos)) {
                    ++interpStats.jumped;
                    if (listInterp)
                        SDL_Log("interp: frame %llu jumped model %#x task %#x: %.1f %.1f %.1f -> %.1f %.1f %.1f",
                                (unsigned long long)rt.frameCount, dm.model, dm.task, wp.t[0], wp.t[1], wp.t[2], w.t[0], w.t[1], w.t[2]);
                } else {
                    w = blendM34(wp, w, alpha);
                    ++interpStats.blended;
                }
            }
        }
        // debugging (hangcheck): HOTD_TRACE_BLENDED=<file>[,task hex]: the models as drawn (after the blend): frame, alpha,
        // task, model, world position, the model's x and z axes
        static FILE* traceBlended = [] {
            const char* p = std::getenv("HOTD_TRACE_BLENDED");
            return p ? std::fopen(std::string(p, std::strcspn(p, ",")).c_str(), "w") : nullptr;
        }();
        static const uint32_t traceTask = [] {
            const char* p = std::getenv("HOTD_TRACE_BLENDED");
            return p && std::strchr(p, ',') ? uint32_t(std::strtoul(std::strchr(p, ',') + 1, nullptr, 16)) : 0u;
        }();
        if (traceBlended && !own && (traceTask == 0 || dm.task == traceTask))
            std::fprintf(traceBlended, "%llu %.3f %X %X %.3f %.3f %.3f %.4f %.4f %.4f %.4f %.4f %.4f\n", (unsigned long long)rt.frameCount,
                         blendItems ? alpha : 1.0f, dm.task, dm.model, w.t[0], w.t[1], w.t[2], w.r[0], w.r[3], w.r[6], w.r[2], w.r[5], w.r[8]);
        if (pcStage_) {
            // the opening's car (kind 181): the PC's car routines (THOTD.EXE 0x496AE0 drive-in, 0x496BE0 brake, 0x496CFC
            // stop, 0x496DFC parked, 0x496E98 skipped) draw their own models where the arcade's draw others: the body
            // 0x1D36 where the arcade draws 0x94F (moving) / 0x951 (parked), the door 0x1D37 at (-9.13, 0, -9.76) on the
            // body (while moving: part of the arcade's 0x94F) / where the arcade draws its door 0x950; no wheels 0x953,
            // tyre frames 0xA49.. / 0xA5C.. or shadow 0x952
            const uint32_t id = dm.model;
            if (id == 0x952 || id == 0x953 || (id >= 0xA49 && id <= 0xA6E))
                continue;
            // the water splashes (splash_9f1, frames 0x9F2-0xA05 picha*: the rain drops kind173 and the drips kind144
            // spawn one where they reach the floor): THOTD.EXE's splash_9f1_spawn (0x486FB0, called from 0x431678 /
            // kind144_drip) is a bare `ret 0xC`, so the PC never shows them (pcpolish: the real PC's rain has no crowns
            // on the ground, harness t00850 / t02500; ours drew them). The game logic stays the arcade's
            if (id >= 0x9F1 && id <= 0xA05)
                continue;
            // (the shot flash's 0.8 scale and the blood mark's depth 1.1: worldOf)
            if (id == 0x94F || id == 0x951) {
                addModel(0x1D36, w, dm);
                if (id == 0x94F) {
                    M34 door = w;
                    const float off[3] = {-9.1349f, 0.0f, -9.7636f};
                    for (int i = 0; i < 3; ++i)
                        door.t[i] = w.r[i * 3] * off[0] + w.r[i * 3 + 1] * off[1] + w.r[i * 3 + 2] * off[2] + w.t[i];
                    addModel(0x1D37, door, dm);
                }
                continue;
            }
            if (id == 0x950) {
                addModel(0x1D37, w, dm);
                continue;
            }
        }
        addModel(dm.model, w, dm);
    }
    // ipolish: the Ragdoll mod's moving props (doors ...): the door-sized models the game drew this frame, outside any
    // body, placed as drawn (the newest frame's matrix): their standing triangles walls, the lying ones ground
    walls_.objs.resize(std::min(walls_.objs.size(), staticWalls_));
    if (ragdollSettings().on && !propWalls_.empty() && !pcAlt)
        for (const DrawModel& dm : rt.drawList) {
            if (dm.body != 0 || dm.shadow || dm.background)
                continue;
            const auto pw = propWalls_.find(dm.model);
            if (pw == propWalls_.end() || !ragdollPropTask(rt, dm.task))   // (misc18: not a creature's effect / item)
                continue;
            const M34 w = worldOf(dm, inv);
            RagdollWalls::Obj o;
            o.lo[0] = o.lo[1] = o.lo[2] = 1e30f, o.hi[0] = o.hi[1] = o.hi[2] = -1e30f;
            const std::vector<float>& src = pw->second;
            for (size_t v = 0; v + 8 < src.size(); v += 9) {
                float p[9];
                for (int k = 0; k < 3; ++k)
                    for (int i = 0; i < 3; ++i)
                        p[k * 3 + i] = w.r[i * 3] * src[v + size_t(k) * 3] + w.r[i * 3 + 1] * src[v + size_t(k) * 3 + 1] +
                                       w.r[i * 3 + 2] * src[v + size_t(k) * 3 + 2] + w.t[i];
                const float e1[3] = {p[3] - p[0], p[4] - p[1], p[5] - p[2]}, e2[3] = {p[6] - p[0], p[7] - p[1], p[8] - p[2]};
                const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (!(nl >= 1e-4f))
                    continue;
                std::vector<float>& to = std::abs(n[1]) / nl >= 0.6f ? o.floors : o.tris;
                to.insert(to.end(), p, p + 9);
                for (int i = 0; i < 3; ++i)
                    to.push_back(n[i] / nl);
                for (int k = 0; k < 3; ++k)
                    for (int i = 0; i < 3; ++i)
                        o.lo[i] = std::min(o.lo[i], p[k * 3 + i]), o.hi[i] = std::max(o.hi[i], p[k * 3 + i]);
            }
            if (o.tris.empty() && o.floors.empty())
                continue;
            walls_.active.push_back(int(walls_.objs.size()));
            walls_.objs.push_back(std::move(o));
            static const bool trace = std::getenv("HOTD_RAGDOLL_TRACE") != nullptr;
            if (trace && rt.frameCount % 120 == 0)
                SDL_Log("view: frame %llu: moving prop %X (task %X, routine %X) a ragdoll wall, %zu triangles", (unsigned long long)rt.frameCount,
                        dm.model, dm.task, dm.task ? rt.mem.u32(dm.task) : 0u, (walls_.objs.back().tris.size() + walls_.objs.back().floors.size()) / 12);
        }
    // the floor pull only for a body that stands (its parts reach 6+ units over the floor), decided per body
    std::map<uint32_t, float> lyingFloor;   // sinkvr: each lying body's floor height (its skins below)
    {
        std::map<uint32_t, float> bodyTop;
        for (const PullTop& p : pullTops) {
            auto [it, fresh] = bodyTop.emplace(p.body, p.top);
            if (!fresh) it->second = std::max(it->second, p.top);
        }
        // sinkvr: a lying body's world box (its real body only), for the flat models it holds
        std::map<uint32_t, std::pair<std::array<float, 3>, std::array<float, 3>>> lyingBox;
        for (const PullTop& p : pullTops)
            if (bodyTop[p.body] < items[p.item].floorY + 6.0f) {
                items[p.item].floorPull = 3;   // feet3: a lying body, the short pull (renderer.cpp)
                if (p.body >= ~uint32_t(0) - uint32_t(pullTops.size())) continue;   // (a lone model: no body)
                lyingFloor[p.body] = items[p.item].floorY;
                auto [it, fresh] = lyingBox.try_emplace(p.body);
                for (int k = 0; k < 3; ++k) {
                    it->second.first[k] = fresh ? p.lo[k] : std::min(it->second.first[k], p.lo[k]);
                    it->second.second[k] = fresh ? p.hi[k] : std::max(it->second.second[k], p.hi[k]);
                }
            }
        // sinkvr: a flat model (not a shadow) inside a lying body's box takes his whole pull too: the opening's dying
        // researcher holds the photograph (model 0x7C4, drawn by its own task, no body). With the pull on his hand
        // only (the VR eyes' / Depth Buffer's key band keeps the lying pull now, renderer.cpp) his palm hid it;
        // under the same pull the band orders them as the arcade does (the photograph over his hand)
        for (const HeldDecal& h : heldDecals)
            for (const auto& [body, box] : lyingBox) {
                bool in = true;
                for (int k = 0; k < 3; ++k) in = in && h.c[k] >= box.first[k] - 0.5f && h.c[k] <= box.second[k] + 0.5f;
                if (in) {
                    if (std::getenv("HOTD_FEET_DEBUG"))
                        std::fprintf(stderr, "feet held model %x at %.2f %.2f %.2f (body %08x)\n", h.model, h.c[0], h.c[1], h.c[2], body);
                    items[h.item].floorPull = 3;
                    break;
                }
            }
    }
    // the skins: corners from node A's space into node B's (B^-1 A), the model rebuilt and drawn with B. Arcade only:
    // the PC's body_draw (THOTD.EXE 0x477B00) calls no skin_draw / sophie_skin_draw (arcade 0x214F0 / 0x21C30); the
    // PC draws its CREAT1 / CREAT2 parts as they are (part_node_draw_rec 0x477EC4 -> geo_draw_model)
    std::map<int, int> used;
    static const std::vector<Runtime::SkinDraw> kNoSkins;
    // rate: the skins' anchors blended as the models (matched by type and order); in world space (the camera cancels
    // out of B^-1 A)
    std::map<int, std::vector<size_t>> prevSkins;
    std::map<int, size_t> skinUsed;
    if (blendItems && !pcStage_)
        for (size_t i = 0; i < prev->skinDraws.size(); ++i)
            prevSkins[prev->skinDraws[i].type].push_back(i);
    for (const Runtime::SkinDraw& sd : pcStage_ ? kNoSkins : rt.skinDraws) {
        auto sk = skins_.find(sd.type);
        if (sk == skins_.end() || used[sd.type] >= kSkinSlots)
            continue;
        const uint32_t skinKey = kSkinKey + uint32_t(sd.type) * 16 + uint32_t(used[sd.type]++);
        // rpolish: the stage whose texture set the body's parts are drawn in. The skin is that stage's model: the attract
        // draws Chapter 4's world and its people from that stage (extraVariant_), and the viewer's Dr. Curien / zombie
        // Sophie take their area's set from Chapter 4's stage when the scene is in another area (as their parts, with
        // the Gallery's colour rows). The chapter's stage's skin in its set 0 there gave Curien's waistcoat a dark
        // panel in the attract (MAME: the gold shirt) and his waist band grey in the viewer (his white shirt)
        StageData* skinStage = stage_.get();
        int skinSet = -1;
        const auto hasSkin = [&](const StageData* st) { return st && st->models.count(skinKey) != 0; };
        if (!rt.actorAreas.empty() && (sd.type == 60 || sd.type == 78 || sd.type == 67)) {
            const int area = sd.type == 60 ? 11 : sd.type == 67 ? 2 : 12;   // (Runtime::actor_areas_update; pmfix: 67 in area 2 as galfix3 made it)
            for (size_t i = 0; i < stage_->textureSets.size(); ++i)
                if (area == rt.textureArea && stage_->textureSets[i].romArea == area)
                    skinSet = int(i);
            if (skinSet < 0 && extraStage_ == 3 && hasSkin(extra_.get()))
                for (size_t i = 0; i < extra_->textureSets.size(); ++i)
                    if (extra_->textureSets[i].romArea == area)
                        skinStage = extra_.get(), skinSet = int(i);
            if (skinSet < 0)   // (as before: the chapter's set of that area, if it has one)
                for (size_t i = 0; i < stage_->textureSets.size(); ++i)
                    if (stage_->textureSets[i].romArea == area)
                        skinSet = int(i);
        } else if (extraVariant_ >= 0 && hasSkin(extra_.get())) {
            skinStage = extra_.get();
            skinSet = extraVariant_;
        }
        auto m = skinStage->models.find(skinKey);
        if (m == skinStage->models.end())
            continue;
        M34 a = mul(inv, fromCopro(sd.a)), b = mul(inv, fromCopro(sd.b));
        if (blendItems) {
            const size_t n = skinUsed[sd.type]++;
            auto ps = prevSkins.find(sd.type);
            if (ps != prevSkins.end() && n < ps->second.size()) {
                const Runtime::SkinDraw& q = prev->skinDraws[ps->second[n]];
                const M34 pa = mul(camPrev, fromCopro(q.a)), pb = mul(camPrev, fromCopro(q.b));
                if (!jumped(pa, a, kItemJumpMove, kItemJumpCos) && !jumped(pb, b, kItemJumpMove, kItemJumpCos)) {
                    a = blendM34(pa, a, alpha);
                    b = blendM34(pb, b, alpha);
                }
            }
        }
        Xform xa, xb;
        std::copy(a.r, a.r + 9, xa.rot);
        std::copy(a.t, a.t + 3, xa.pos);
        std::copy(b.r, b.r + 9, xb.rot);
        std::copy(b.t, b.t + 3, xb.pos);
        const Xform rel = inverse(xb) * xa;
        std::vector<std::array<float, 3>> moved;
        for (const auto& c : sk->second.corners) {
            std::array<float, 3> v;
            for (int i = 0; i < 3; ++i)
                v[i] = rel.rot[i * 3] * c[0] + rel.rot[i * 3 + 1] * c[1] + rel.rot[i * 3 + 2] * c[2] + rel.pos[i];
            moved.push_back(v);
        }
        source_->rebuildSkin(skinStage->stage, sk->second, moved, m->second);
        renderer.updateModel(*skinStage, m->second);
        const M34& w = b;   // world space already
        Placement p;
        p.id = p.modelKey = m->first;
        std::copy(w.r, w.r + 9, p.rot);
        std::copy(w.t, w.t + 3, p.pos);
        if (placements_.size() == placements_.capacity())
            break;   // the reserve below keeps the pointers stable
        placements_.push_back(p);
        DrawItem d{skinStage, &m->second, &placements_.back(), m->second.preferredVariant};
        pickVariant(d);
        if (skinSet >= 0)
            d.variant = skinSet;
        d.flat = sd.flat;   // magflash: the hit flash (skin_draw's header 0x802400)
        d.vrKind = 4;   // questsim: a creature's skin (the joints between its parts: VR's CPU aim ray tests it too)
        // sinkvr: a lying body's skins take its whole pull as its parts do (without it the dying researcher's chest
        // joint stayed under the opening's slab while his arms lay over it, Depth Buffer / VR)
        if (auto lf = lyingFloor.find(sd.body); sd.body != 0 && lf != lyingFloor.end()) {
            d.floorY = lf->second;
            d.floorPull = 3;
        }
        items.push_back(d);
    }
    if (std::getenv("HOTD_NO_CULL")) opt.cull = false;   // debugging
    if (std::getenv("HOTD_NO_MIP")) opt.mipmap = false;   // debugging
    if (std::getenv("HOTD_NO_LIGHT")) opt.lighting = false;   // debugging
    // debugging: HOTD_LIST_DRAWN_AT=<frame>: every item drawn that frame (source, ID, name, position, fade)
    const char* listAt = std::getenv("HOTD_LIST_DRAWN_AT");
    const bool listNow = listAt && std::strtoull(listAt, nullptr, 10) == rt.frameCount;
    if (listNow)
        SDL_Log("drawn: texture area %d, zone %d, set %d light known %d", rt.textureArea, zone, variant,
                variant >= 0 ? int(stage_->textureSets[size_t(variant)].lightKnown) : -1);
    if (listNow && variant >= 0) {
        const TextureSet& ts = stage_->textureSets[size_t(variant)];
        std::string s;
        for (int i = 0; i < 32; ++i)
            s += " " + std::to_string(int(ts.diffuse[i])) + "/" + std::to_string(int(ts.ambient[i]));
        SDL_Log("drawn: set light %.2f %.2f %.2f, diffuse/ambient:%s", ts.lightDir[0], ts.lightDir[1], ts.lightDir[2], s.c_str());
    }
    if (listNow)
        for (const DrawItem& it : items) {
            // the polygons' light parameter indices (Model 2 attr bits 18-22): index x triangles
            int hist[32] = {};
            for (const Mesh& me : it.model->meshes)
                if (me.variant < 0 || me.variant == it.variant)
                    for (size_t v = 0; v < me.vertices.size(); v += 3)
                        ++hist[(me.vertices[v].flags >> kLightParamShift) & 31];
            std::string ps;
            for (int i = 0; i < 32; ++i)
                if (hist[i]) ps += " " + std::to_string(i) + "x" + std::to_string(hist[i]);
            // HOTD_DUMP_WORLD=<file> (polish2): every drawn triangle in world space ("id x y z" x 3 per line), for finding
            // what covers what
            if (const char* dw = std::getenv("HOTD_DUMP_WORLD")) {
                static FILE* wf = std::fopen(dw, "w");
                static bool camDone = false;
                if (wf && !camDone) {
                    camDone = true;
                    std::fprintf(wf, "cam %g %g %g\n", inv.t[0], inv.t[1], inv.t[2]);
                    std::fprintf(wf, "camr %g %g %g %g %g %g %g %g %g focal %g\n", inv.r[0], inv.r[1], inv.r[2], inv.r[3], inv.r[4],
                                 inv.r[5], inv.r[6], inv.r[7], inv.r[8], rt.focal);
                }
                const float* r = it.placement->rot;
                const float* t = it.placement->pos;
                for (const Mesh& me : it.model->meshes)
                    if (wf && (me.variant < 0 || me.variant == it.variant))
                        for (size_t v = 0; v + 2 < me.vertices.size(); v += 3) {
                            std::fprintf(wf, "%x", it.placement->id);
                            for (int k = 0; k < 3; ++k) {
                                const float* p = me.vertices[v + size_t(k)].pos;
                                for (int c = 0; c < 3; ++c)
                                    std::fprintf(wf, " %g", r[c * 3] * p[0] + r[c * 3 + 1] * p[1] + r[c * 3 + 2] * p[2] + t[c]);
                            }
                            std::fprintf(wf, "\n");
                        }
                if (wf) std::fflush(wf);
            }
            if (std::getenv("HOTD_LIST_COLOURS")) {   // the polygons' colours: palette entry / rgb base / luma table
                std::map<std::string, int> combos;
                for (const Mesh& me : it.model->meshes)
                    if (me.variant < 0 || me.variant == it.variant)
                        for (size_t v = 0; v < me.vertices.size(); v += 3) {
                            const Vertex& x = me.vertices[v];
                            char b[96];
                            std::snprintf(b, sizeof b, "p%d:%d,%d,%d:l%d:t%d:f%x:r%d,%d,%d,%d,%d", int(x.palette), x.color[0], x.color[1], x.color[2],
                                          x.color[3], int(x.flags & 1), unsigned(x.flags), x.rect[0], x.rect[1], x.rect[2], x.rect[3], int(x.layer));
                            ++combos[b];
                        }
                if (const char* dm = std::getenv("HOTD_DUMP_MODEL"); dm && std::strtoul(dm, nullptr, 16) == it.placement->id) {
                    FILE* f = std::fopen("model_dump.txt", "w");
                    for (const Mesh& me : it.model->meshes)
                        if (f && (me.variant < 0 || me.variant == it.variant))
                            for (const Vertex& x : me.vertices)
                                std::fprintf(f, "%g %g %g %g %g %g %g %g %d %d %d %d %d %d %d %d %d %d %d\n", x.pos[0], x.pos[1], x.pos[2],
                                             x.normal[0], x.normal[1], x.normal[2], x.uv[0], x.uv[1], x.rect[0], x.rect[1], x.rect[2],
                                             x.rect[3], int(x.layer), int(x.flags), int(x.palette), x.color[0], x.color[1], x.color[2], x.color[3]);
                    if (f) std::fclose(f);
                }
                ps += " colours";
                for (const auto& [k, n] : combos) ps += " " + k + "x" + std::to_string(n);
            }
            SDL_Log("drawn: %s %#x %s at %.0f %.0f %.0f fade %.2f variant %d (area %d, %zu valid)%s light %d %.2f %.2f %.2f params%s",
                    sourceName(it.stage->source), it.placement->id, it.model->name.c_str(), it.placement->pos[0],
                    it.placement->pos[1], it.placement->pos[2], it.fade, it.variant,
                    it.variant >= 0 && it.variant < int(it.stage->textureSets.size()) ? it.stage->textureSets[size_t(it.variant)].romArea : -1,
                    it.model->validVariants.size(), it.background ? " (sky)" : "", int(it.hasLight), it.light[0], it.light[1], it.light[2],
                    ps.c_str());
        }
    // the game's grey / sepia look (fix-logic's Runtime::greyPalette: Chapter 1's opening frames ~182-242, kind 182)
    opt.grey = rt.greyPalette.on && !std::getenv("HOTD_NO_GREY");
    // (team: Options > Video > Transparency; 3 Auto: the arcade models dithered as the Model 2 draws them, the PC models
    // with clean edges)
    opt.transparency = rt.transparency == 3 ? (pcStage_ ? 1 : 2) : rt.transparency;
    if (opt.grey)
        std::copy(&rt.greyPalette.sepia[0][0], &rt.greyPalette.sepia[0][0] + 64 * 3, &opt.sepia[0][0]);
    // cosmods: the Rainbow Blood mod (game/cosmods.h): the hue from the game's frame (the same picture at the same frame).
    // PC pages loaded with the mod on keep their marked (white) blood texels until the next load: switched off since,
    // they are drawn red (one hue, 0)
    {
        const RainbowBloodSettings& rb = rainbowBloodSettings();
        const bool marked = pcStage_ && pcCfg_ && (pcCfg_->blood & PcConfig::kBloodRainbow);
        // the custom Blood Color (arcade models; user 2026-11-03): the same recolour in that one colour on the enemies'
        // painted wounds (entry 1023 below has it for the bursts and the gore; the PC pages are tinted at the load)
        const int custom = !rb.on && !pcStage_ ? bloodCustom_ : -1;
        customWoundBlood() = custom;
        opt.rainbow = rb.on ? (rb.together ? 2 : 1) : marked ? 2 : custom >= 0 ? 3 : 0;
        opt.rainbowPhase = rb.on ? float(std::fmod(double(rt.frameCount) * rb.speed / 60.0, 1.0)) : 0.0f;
        // Fast: in step with the Chapter 4 screens (user 2026-11-03): their clock, the game's frame counter, one table
        // step per 1.5 frames (palette_animation's entry 0)
        if (rb.on && rb.speed > 0.4f) {
            // (the screens' entry 1009 runs 24 frames of the table behind entry 0 here: matched by pictures)
            const double off = -24.0;
            opt.rainbowPhase = float(std::fmod((double(rt.mem.u32(A::G::frame_counter)) + off) / 135.0 + 1.0, 1.0));
            // arcade models: the blood entry takes the screens' own animated entry (palette_anim_copy: work RAM
            // 0x52066C + 2i -> entry 1017 - i), drawn through the colour rows as the screens are: the same colour

            if (!pcStage_)
                opt.rainbowSync = 1009;   // (entry 8: the big screens behind Curien, matched by pictures)
        }
        // the colours: the game's own rainbow (user 2026-11-03: as the computer screens behind Curien in Chapter 4,
        // palette_animation 0x25310 steps 9 entries through the 90-colour RGB555 table 0xAB120, R in the low bits); a
        // plain hue if the table is not there (each entry has a channel at 31)
        if (opt.rainbow == 1 || opt.rainbow == 2) {
            opt.rainbowGame = true;
            for (int i = 0; i < 90 && opt.rainbowGame; ++i) {
                const uint16_t v = rt.mem.mapped(0xAB120) ? rt.mem.at<uint16_t>(0xAB120 + 2u * uint32_t(i)) : 0;
                const int ch[3] = {v & 31, v >> 5 & 31, v >> 10 & 31};
                opt.rainbowGame = ch[0] == 31 || ch[1] == 31 || ch[2] == 31;
                for (int c = 0; c < 3; ++c) opt.rainbowTable[i][c] = float(ch[c]) / 31.0f;
            }
        }
        if (custom >= 0)
            for (int c = 0; c < 3; ++c) opt.bloodRgb[c] = float(custom >> (16 - 8 * c) & 0xFF) / 255.0f;
        // rainbow2 (user 2026-11-03: the rainbow on all the blood): the creatures' blood rows (every shade of every
        // painted wound, the people's and the bosses' too) cycle per pixel in the renderer, as the custom colour's
        // tintBloodRows recolours them; the arcade models only (the PC's blood is its marked texels)
        opt.bloodRowMask = 0;
        if (rb.on && !pcStage_)
            for (int row : rt.bloodRows)
                if (row >= 0 && row < 32) opt.bloodRowMask |= 1u << row;
    }
    // the game-over route map (mode 5 from sub-mode 2, fix-world-2): the original's task group draws only the walkers
    // (not ported) over the 2D map, no world
    if (rt.mem.at<uint16_t>(A::G::game_mode) == 5 && rt.mem.u32(A::G::game_submode) >= 4)
        items.clear();
    // the blood colour of the bursts, drips and gore (akabushu 0x7-0x23, 0x46B-0x4A3 ...: polygon palette entry 1023):
    // maincpu 0x24E5C stores [0xAB110 + 2 x blood_color] there (0x801F red, 0xB340 green, 0xFC12 blue, 0xE01C purple;
    // every fullgame_us MAME dump has 0x801F). Was the effect record's 0x8018 for every setting: the bursts stayed red
    // while GGSET recoloured the faces (perf item 4, 2026-10-16). The PC models have their own blood pages.
    // palanim (2026-10-16): with it the game's own palette animation (Runtime::polyPaletteAnim: palette_animation 0x25310
    // + the vblank copy 0x24E90: entries 959, 984-1021 pulse / cycle; HOTD_NO_PALANIM = the loaded values)
    if (!pcStage_) {
        // display: the custom colour (experimental) as the entry's RGB555 (R in the low bits, as 0x801F red)
        const uint16_t custom = bloodCustom_ < 0 ? 0
            : uint16_t(0x8000u | ((unsigned(bloodCustom_) & 0xFF) >> 3) << 10 | ((unsigned(bloodCustom_) >> 8 & 0xFF) >> 3) << 5 |
                       ((unsigned(bloodCustom_) >> 16 & 0xFF) >> 3));
        // arcpal (2026-10-27): entry 1023 drawn from the runtime's palette RAM bank 2 (0x18027FE), as the arcade does:
        // the area load's vblank_words_upload writes the 0xAB110 colour there and palette_grey_build /
        // palette_bank2_upload grey it with the rest of the bank (entries 24-1023; palette_anim_copy does not write
        // 1023 back, so the blood is grey in the sepia scenes too). The custom colour replaces the table's, greyed the
        // same way while the bank holds the grey copy. Before the first area load (bank word 0): the table's.
        const uint16_t table = uint16_t(rt.mem.at<uint16_t>(0xAB110 + 2u * (rt.mem.at<uint8_t>(A::G::blood_color) & 3u)) | 0x8000u);
        const auto grey555 = [](uint16_t c) {   // palette_grey_build's grey of an RGB555 word
            const uint32_t s = ((c >> 5) & 0x1Fu) + (c & 0x1Fu) + ((c >> 10) & 0x1Fu);
            return uint16_t((c & 0x8000u) | ((s / 3u) & 0x1Eu) * 0x421u);
        };
        // A blood colour setting changed in the menus after the last area load (the arcade cannot: its EEPROM setting
        // only changes in the test menu): the bank still holds another table colour, drawn as the new setting's.
        uint16_t bank = rt.mem.mapped(0x18027FE) ? rt.mem.at<uint16_t>(0x18027FE) : uint16_t(0);
        if (!(bank & 0x8000u)) bank = table;
        for (uint32_t i = 0; i < 4; ++i) {
            const uint16_t other = uint16_t(rt.mem.at<uint16_t>(0xAB110 + 2u * i) | 0x8000u);
            if (other != table && bank == other) bank = table;
            else if (other != table && bank == grey555(other) && bank != grey555(table)) bank = grey555(table);
        }
        const bool bankGrey = bank != table && bank == grey555(table);
        const uint16_t blood = std::getenv("HOTD_NO_BLOOD_ENTRY") ? 0 : custom ? (bankGrey ? grey555(custom) : custom) : bank;
        static const bool noAnim = std::getenv("HOTD_NO_PALANIM") != nullptr;
        const uint32_t serial = noAnim ? 0 : rt.polyPaletteAnim.serial;
        if (blood != bloodEntry_ || serial != palAnimSerial_) {
            bloodEntry_ = blood;
            palAnimSerial_ = serial;
            std::vector<uint16_t> anim(1024, 0);
            if (!noAnim)
                for (size_t i = 0; i < 1024; ++i)
                    if (rt.polyPaletteAnim.entries[i] & 0x8000u) anim[i] = rt.polyPaletteAnim.entries[i];
            anim[1023] = blood;
            renderer.setPaletteAnimation(anim.data());
        }
    }
    if (drawOverride) {   // vr: the eyes draw the items (docs/vr.md)
        drawOverride(items);
        return;
    }
    renderer.draw(items, camOverride ? *camOverride : cam, width, height, opt);
}

void GameView::cameraToWorld(const float* coproCamera, float rot[9], float pos[3])
{
    const M34 inv = invRigid(fromCopro(coproCamera));
    std::copy(inv.r, inv.r + 9, rot);
    std::copy(inv.t, inv.t + 3, pos);
}

void GameView::toArcade(float px, float py, int width, int height, float& ax, float& ay) const
{
    const float s = kHalfH / (float(height) / 2.0f);
    ax = (px - float(width) / 2.0f) * s;
    ay = -(py - float(height) / 2.0f) * s;
}

void GameView::fromArcade(float ax, float ay, int width, int height, float& px, float& py) const
{
    const float s = kHalfH / (float(height) / 2.0f);
    px = float(width) / 2.0f + ax / s;
    py = float(height) / 2.0f - ay / s;
}

bool GameView::placedItem(int slot, uint32_t id, const float rot[9], const float pos[3], DrawItem& out)
{
    if (slot < 0 || slot >= int(std::size(placed_)))
        return false;
    const StageData* from = pcStage_ ? pcStage_.get() : stage_.get();
    if (!from)
        return false;
    auto m = from->models.find(id);
    if (m == from->models.end() || m->second.triangleCount() == 0)
        return false;
    Placement& p = placed_[slot];
    p = Placement{};
    p.id = p.modelKey = id;
    std::copy(rot, rot + 9, p.rot);
    std::copy(pos, pos + 3, p.pos);
    out = DrawItem{from, &m->second, &p, m->second.preferredVariant};
    if (from == stage_.get() && variant_ >= 0) {   // (the arcade's texture set of the area, as draw's pickVariant)
        const std::vector<int>& ok = m->second.validVariants;
        if (ok.empty() || std::find(ok.begin(), ok.end(), variant_) != ok.end())
            out.variant = variant_;
    }
    return true;
}

bool GameView::hasModel(uint32_t id) const
{
    const StageData* from = pcStage_ ? pcStage_.get() : stage_.get();
    if (!from)
        return false;
    auto m = from->models.find(id);
    if (m == from->models.end() || m->second.triangleCount() == 0)
        return false;
    static std::set<uint32_t> logged;
    if (logged.insert(id).second)
        SDL_Log("model %#x (%s): bounds %.2f..%.2f %.2f..%.2f %.2f..%.2f", id, pcStage_ ? "PC" : "arcade", m->second.boundsMin[0],
                m->second.boundsMax[0], m->second.boundsMin[1], m->second.boundsMax[1], m->second.boundsMin[2], m->second.boundsMax[2]);
    return true;
}

} // namespace hotd::game
