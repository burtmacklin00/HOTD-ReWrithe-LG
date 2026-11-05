// OpenGL 3.3 renderer for StageData from any source. Two shading models side by side (ROADMAP: per-source shading):
//  - DirectRgba (PC): RGBA pages, alpha-tested, bilinear.
//  - Model2Indexed (arcade): per pixel, the Model 2 color pipeline from MAME's draw_scanline_tex (docs/formats.md §13):
//    4-bit shade (optionally bilinear-filtered) -> luma table [luma base * 128 + shade * 8] -> scaled by the polygon's lighting,
//    capped at 63 -> color translation table [channel][palette base value * 256 + brightness].
#pragma once

#include "assets/assets.h"
#include "assets/mods.h"
#include "render/gl.h"

#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace hotd {

struct Camera {
    float pos[3] = {0, 0, 0};
    float yaw = 0, pitch = 0;   // radians; yaw 0 looks along +z, positive yaw turns toward +x (left-handed world)
    float fovY = 0.80f;         // vertical field of view (radians); about 46 degrees like the arcade picture
    float nearZ = 1.0f;         // the perspective's near plane (depth clamp keeps what is nearer; the viewer's Free Cam: less)
    bool perPixelDepth = false; // Depth Buffer whatever Draw Depth says (the viewer's Free Cam: SceneView::freeCamToggle)
    float shift[2] = {0, 0};    // the picture moved by (x, y) in normalised device units, the view unchanged (a moved
                                // projection centre: the hit screen shake, GameView::draw); not for the VR eyes
    void basis(float right[3], float up[3], float fwd[3]) const;
};

struct RenderOptions {
    bool lighting = true;        // per-polygon lighting from the normal (approximation; the real light setup is unknown)
    bool filter = true;          // bilinear filtering of arcade shades (the Model 2 filters texels)
    bool gamma = true;           // arcade: MAME's monitor curve (darker; MAME screenshots have it)
    bool cull = true;            // arcade: Model 2 back-face culling of single-sided polygons
    bool mipmap = true;          // arcade: Model 2 texture mip levels (with filtering on)
    bool pcBilinear = false;     // PC: the driver dialog's bilinear option (off in the release's devs.cfg: point sampling)
    // team (2026-11-03): Options > Video > Transparency: 0 Smooth (the translucent texels cut at half their filtered
    // cover, as before), 1 Clean Edges (the same cut, the colour from the solid texels only: no fringe), 2 Dithered (the
    // cover stippled in a 4 x 4 screen-door pattern, the solid texels' colour)
    int transparency = 0;
    float lightDir[3] = {0.3f, 0.8f, -0.5f};
    float ambient = 0.45f;
    float clear[3] = {0.02f, 0.02f, 0.04f};
    bool clearColor = true;      // false: the frame already holds what lies under the 3D (Hud::drawUnder), depth only
    // the game's grey / sepia look (Runtime::greyPalette, palette_grey_build 0x1A400): arcade, the colour table entry
    // (base, luma) of channel c becomes sepia[avg of the three curves / 4][c] before the output curve; PC without brown
    // pages, the final colour's grey level L = (r + g + b) / 3 becomes sepia[L / 4] (0..255)
    bool grey = false;
    uint8_t sepia[64][3] = {};
    // Light Gun Edition: the fun modes' looks: 1 Night Vision (green), 2 Wireframe (neon lines on black), 3 VHS (washed out)
    int look = 0;
    // cosmods: the Rainbow Blood mod (game/cosmods.h): 0 off; 1 each draw item (a splash, a body part) its own hue; 2 one
    // hue for all; 3 the custom Blood Color (bloodRgb) on the arcade creatures' painted wounds (user 2026-11-03: the same
    // recolour; entry 1023 already holds it). Arcade: polygon palette entry 1023 (the blood colour); PC: the blood
    // texels the loader marked (alpha kRainbowAlpha, PcConfig::kBloodRainbow). rainbowPhase: the hue now, 0..1
    int rainbow = 0;
    float rainbowPhase = 0.0f;
    float bloodRgb[3] = {1.0f, 0.0f, 0.0f};   // mode 3: the custom colour, 0..1
    // the game's own rainbow (user 2026-11-03: as the computer screens behind Curien, palette_animation 0x25310 cycles
    // the 90-colour table 0xAB120): its colours 0..1 when rainbowGame, else a plain hue
    bool rainbowGame = false;
    int rainbowSync = -1;   // Fast: the polygon palette entry (1009..1017, palette_animation's cycling ones) the blood entry takes
    float rainbowTable[90][3] = {};
    // rainbow2 (modes 1 / 2, arcade): the colour rows that hold the creatures' blood (Runtime::bloodRows, bit = row
    // 0..31): every shade of them takes the rainbow per pixel, as the custom colour's tintBloodRows does on the CPU
    uint32_t bloodRowMask = 0;
};
constexpr int kRainbowAlpha = 250;   // cosmods: a PC blood texel's alpha with Rainbow Blood on (drawn opaque)

// Decal layers inside one model: a triangle lying in the plane of an earlier triangle of the same mesh (same facing) and
// overlapping it (the blood and face layers of the creature parts: 598 arcade part models, a few PC ones) gets layer
// 1 + the layer below it (max 3, Vertex::flags bits 13-14). The Model 2 sorts polygons and has no depth buffer, so
// the later layer shows; with our depth buffer coplanar layers z-fought (the blood flickering on the zombies' faces and
// bodies as they moved). The renderer pulls each layer toward the camera by a small depth step. Returns the count.
// First, untextured triangles just in front of a translucent textured one (the heads' iris quads under the eye
// textures) are flattened onto it and put below it (renderer.cpp flattenUnderTranslucent; HOTD_NO_FLATTEN=1 off).
constexpr int kLayerShift = 13;
int markCoplanarLayers(Model& model);
// perf: every model of a stage, on the machine's threads (each model on its own: the same result as one by one)
// background: half the threads (a load ahead while the game runs: the game keeps its share)
int markCoplanarLayersAll(std::map<uint32_t, Model>& models, bool background = false);

// Draw Depth (Video option; --draw-depth): how arcade polygons hide each other. kDepthBuffer: a per-pixel depth
// buffer (with the decal / floor tweaks in renderer.cpp); kArcadeSort: as the Model 2 (MAME model2_v.cpp): every
// polygon at one depth, the min or max camera z of its corners by its own z-sort mode (attr bits 10-11), rounded like
// the hardware's 4.12 sort key, the later polygon in front on a tie. PC models keep the depth buffer in both. Every
// renderer reads it
enum DrawDepth { kDepthBuffer = 0, kArcadeSort = 1 };
// depthfix (2026-10-27): Depth Buffer's key band: each arcade polygon's depth moves toward its sort key by at most this
// share of its distance (renderer.cpp vertex shader), so near-touching surfaces resolve as the arcade, the rest per pixel
constexpr float kKeyBand = 0.2f;
// the viewer's Free Cam (Camera::perPixelDepth, whatever Draw Depth says): a narrower band, so crossings in close-ups
// stay per pixel
constexpr float kKeyBandFreeCam = 0.05f;
void setDrawDepth(int mode);
int drawDepth();
// prepass (2026-10-27): a depth-only pass before the opaque pass (the same discards), then the opaque pass shades each
// pixel about once with the EQUAL depth test (the same picture). Auto by the graphics (main.cpp depthPrepassAuto: on
// with integrated graphics); HOTD_PREPASS=0 / 1 overrides (comparisons)
void setDepthPrepass(bool on);
bool depthPrepass();

class Renderer {
public:
    // 4P (docs/coop4.md): the colour ramps a DrawItem::ramp maps its colours through (players 3 / 4: shadow .. white-hot)
    float rampStops[4][5][3] = {};   // 0 / 1: 4P players 3 / 4; 2 Night Vision's heat, 3 the Exploding Zombies' glow
    bool init(std::string& err);
    void shutdown();
    // Uploads a stage's meshes and texture sets. The StageData must stay alive while it's uploaded.
    void upload(const StageData& stage);
    void release(const StageData& stage);
    // Re-sends a model whose vertex positions changed (same vertex count as uploaded), e.g. a generated skin.
    void updateModel(const StageData& stage, const Model& model);
    // Re-sends one texture set's colour translation table (TextureSet::xlat layout), e.g. after color_row_set (s1).
    void updateXlat(const StageData& stage, int set, const std::vector<uint8_t>& xlat);
    void draw(const std::vector<DrawItem>& items, const Camera& cam, int width, int height, const RenderOptions& opt);
    // Renders into an offscreen framebuffer and returns RGB rows top to bottom.
    std::vector<uint8_t> drawToImage(const std::vector<DrawItem>& items, const Camera& cam, int width, int height,
                                     const RenderOptions& opt);
    std::vector<uint8_t> readBack(int width, int height);
    // Animated arcade palette entries (ArcadePaletteFx::compute): 1024 values, RGB555 | 0x8000 where set; null = none.
    void setPaletteAnimation(const uint16_t* entries);
    // Arcade models whose texture points scroll (u, v in texels per model key); replaces the previous set.
    void setUVScroll(std::map<uint32_t, std::array<float, 2>> scroll) { uvScroll_ = std::move(scroll); }
    // glDrawArrays calls so far (never reset; robust-2's --render statistics take differences)
    uint64_t drawCalls = 0;
    // perf: GL state calls in draw (uniforms, texture / vertex array binds; never reset, as drawCalls)
    uint64_t stateCalls = 0;
    // polish3 (debugging, hotd --render with HOTD_ZFIGHT): added to the camera position, to find depth ties (z-fighting)
    // by rendering a frame twice with a sub-pixel camera move
    float debugJitter[3] = {0, 0, 0};
    // polish3 (debugging, HOTD_ZFIGHT): +1 / -1 moves every triangle's depth by 2e-6 (clip z / w) to a pseudo-random
    // side; a pixel whose winner changes between +1 and -1 is a depth tie between two surfaces (z-fighting). 0 = off
    int debugTie = 0;
    bool debugIds = false;                   // each item drawn in a flat colour = its index (debugNames: what it is)
    std::vector<std::string> debugNames;
    // vr (2026-11-03, docs/vr.md): an explicit eye for the next draws instead of the Camera's view and symmetric
    // perspective: a world -> eye matrix (column-major, rows right / up / forward of the left-handed eye, +z ahead, as
    // draw builds from a Camera) and the frustum's edges as tangents (left / down negative: OpenXR's XrFovf angles'
    // tangents), the same near plane and far plane as a Camera. Null (always outside VR): the Camera's, unchanged.
    // The items' sideOnly strips are drawn whole with it (the 4:3 picture has no place in a headset's view)
    struct EyeView {
        float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        float tanLeft = -1, tanRight = 1, tanUp = 1, tanDown = -1;
        float nearZ = 1.0f;
    };
    const EyeView* eye = nullptr;
    // the view-projection the last draw used (column-major; vr: the eye's, for the overlays drawn over it)
    float lastViewProj[16] = {};

private:
    std::map<uint32_t, std::array<float, 2>> uvScroll_;
    gl::GLint uUVScroll_ = -1;
    struct Range { int first, count, set, variant; bool blend; };
    struct GpuSet {
        Shading shading;
        gl::GLuint layers = 0, layersBrown = 0, luma = 0, xlat = 0, pal = 0;   // layersBrown: PC, TextureSet::layersBrown   // pal: Model 2 polygon palette (TextureSet::basePalette)
        bool lightKnown = false;
        float lightDir[3] = {0, 1, 0}, diffuse[32] = {}, ambient[32] = {}, specScale[32] = {}, specCtl[32] = {};
        gl::GLuint replMap = 0;   // texture packs (assets/mods.h): 512 x 517 RGBA16UI, shader key -> atlas x | layer << 11, y, w, h
        gl::GLuint replRef = 0;   // texture packs, arcade: the same for the reference pictures (StageReplacements::Entry::ref)
        // perf: the PC pages' filter last set on layers / layersBrown (draw sets it only when it changes; -1 = not yet)
        mutable gl::GLint filterLayers = -1, filterBrown = -1;
    };
    struct GpuStage {
        gl::GLuint vao = 0, vbo = 0;
        std::map<uint32_t, std::vector<Range>> models;   // model key -> draw ranges
        std::vector<GpuSet> sets;
        gl::GLuint replAtlas = 0;   // texture packs: the pack pictures, 2048 x 2048 RGBA8 layers (shelf-packed)
        std::map<uint64_t, uint32_t> replVirtual;   // texture packs: StageReplacements::virtualKeys
        std::set<uint32_t> replModels;              // texture packs: model keys drawn from a pack's OBJ (updateModel leaves them)
        std::map<uint32_t, int> modelDepth;         // depthopt: a mod's items' own depth mode (StageReplacements::modelDepth)
        gl::GLuint sortBuf = 0, sortTex = 0;        // Draw Depth "Arcade Sort": 4 RGBA32F texels per triangle (sortKeysOf)
    };
    // the triangles' sort keys: the key polygon's 4 corners (model space), texel 0's w = its mode (0 = none: the
    // per-pixel depth)
    static void sortKeysOf(const Mesh& m, size_t count, std::vector<float>& out);
    std::map<const StageData*, GpuStage> stages_;
    struct Resolved { const GpuStage* g = nullptr; const std::vector<Range>* ranges = nullptr; };
    std::vector<Resolved> resolved_;   // perf: draw's items -> their stage and model ranges (kept: no allocation a frame)
    gl::GLuint program_ = 0, palAnim_ = 0;
    // compat (2026-10-29, the GL debug output): 1x1 textures of each sampler type for the units a set leaves empty (the
    // shader's samplers always point at a complete texture of their own type; empty units are undefined on some drivers)
    gl::GLuint spareUint_ = 0, spareFloat_ = 0, spareArray_ = 0;
    void makeSpareTextures();
    gl::GLuint programBand_ = 0;   // depthfix: the same shaders with KEY_BAND_FRAG (Depth Buffer's key band per pixel)
    gl::GLint uViewProj_ = -1, uOffset_ = -1, uShading_ = -1, uLighting_ = -1, uLightDir_ = -1, uAmbient_ = -1, uFilter_ = -1, uTransp_ = -1;
    gl::GLint uRot_ = -1;
    gl::GLint uCull_ = -1, uMip_ = -1, uPcCullSign_ = -1, uClampUV_ = -1, uDepthBias_ = -1, uFloorY_ = -1, uFloorPull_ = -1, uPush_ = -1, uFade_ = -1, uSideOnly_ = -1, uSideClip_ = -1, uFlat_ = -1, uGrey_ = -1, uSepia_ = -1, uRainbow_ = -1, uRainbowPhase_ = -1, uBloodRgb_ = -1, uRainbowTable_ = -1, uRainbowGame_ = -1, uRainbowSync_ = -1, uBloodRows_ = -1, uTie_ = -1, uTieItem_ = -1, uIdMode_ = -1;
    gl::GLint uCamPos_ = -1, uM2Known_ = -1, uM2Light_ = -1, uM2Diffuse_ = -1, uM2Ambient_ = -1, uM2SpecScale_ = -1, uM2SpecCtl_ = -1, uCamFwd_ = -1, uM2Gamma_ = -1;
    gl::GLint uHasRepl_ = -1, uPcFilter_ = -1, uGreyRepl_ = -1, uSort_ = -1, uSortItem_ = -1, uPrepass_ = -1, uHybrid_ = -1, uKeyless_ = -1, uProj_ = -1, uBiasScale_ = -1;
    bool sortFragDepth_ = false;   // earlyz: Arcade Sort through gl_FragDepth (HOTD_SORT_FRAGDEPTH / _EXACT), else vertex depth
    std::vector<size_t> order_, orderArcade_;   // draw: the items' order in the depth-writing passes (kept: no allocation)
    void uploadReplacements(const StageReplacements& rp, GpuStage& g);
    // the mesh's vertices with the virtual keys of colour-only pictures (StageReplacements::virtualKeys) in lodCoef
    static const Vertex* replPatched(const GpuStage& g, const Mesh& m, std::vector<Vertex>& tmp);
};

} // namespace hotd
