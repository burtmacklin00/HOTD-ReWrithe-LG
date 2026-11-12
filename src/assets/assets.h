// Asset interface (ROADMAP: "Design principle: switchable model sources" and "PC copy, arcade dump, or both").
//
// The engine never reads a file format. It asks an AssetSource (one per data source: PC install, arcade ROM dump, later mods)
// for a stage, and gets the same in-memory types from every source. Which source supplies which model is decided by a
// SourceSelection: per model (canonical object ID), then per category, then whichever source has the model at all.
//
// Textures always follow their model's source: a Mesh refers to a TextureSet of the StageData it came from, and each
// TextureSet carries its own shading model (PC: direct ARGB pages; arcade: Model 2 indexed shades + color tables).
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hotd {

enum class Source : uint8_t { PC = 0, Arcade = 1 };
constexpr int kSourceCount = 2;
const char* sourceName(Source s);

// Categories the player can switch independently (ROADMAP). Effects = weapons and effects; Humans = scientists, agents,
// Sophie (formats.md §25).
enum class Category : uint8_t { World = 0, Zombies, Bosses, Effects, Humans };
constexpr int kCategoryCount = 5;
const char* categoryName(Category c);

// How a texture set turns texels into colors.
enum class Shading : uint8_t {
    DirectRgba,     // PC: RGBA8 pages (from ARGB1555), alpha 0 = transparent
    Model2Indexed,  // arcade: 4-bit shades -> luma table -> color translation with the polygon's palette base color
};

enum TexFlag : uint16_t {
    kTextured = 1 << 0,
    kMirrorU = 1 << 1,
    kMirrorV = 1 << 2,
    kTranslucent = 1 << 3,  // Model 2: shade 15 is transparent
    kChecker = 1 << 4,
    kDoubleSided = 1 << 5,
    kHalfAlpha = 1 << 6,    // PC: drawn at alpha 0x7F (polygon group head bit 23, THOTD.EXE 0x577870)
    kInsideRect = 1 << 7,   // arcade: all the polygon's texture points lie inside its texture (not repeated): the
                            // bilinear filter does not wrap into the texture's other side at its edges
    // bits 8-12: Model 2 lighting parameter index (polygon attr bits 18-22)
};
constexpr int kLightParamShift = 8;
// arcade Vertex::layer bits 1-4: the polygon's Model 2 z-sort (Mesh::sortKeys)
constexpr uint16_t kSortShift = 1, kSortMask = 3 << kSortShift, kSortQuadFirst = 1 << 3, kSortQuadSecond = 1 << 4;
constexpr uint8_t kSortMin = 1, kSortMax = 2, kSortFar = 3;

// One vertex of a triangle. Per-polygon data is repeated on the polygon's vertices (flat).
struct Vertex {
    float pos[3];        // model space (left-handed world: x right, y up, z forward)
    float uv[2];         // in texels, relative to rect (may run past the rect: textures repeat)
    float normal[3];     // polygon normal, unit length or zero
    uint16_t rect[4];    // texture rectangle within its layer: x, y, w, h (texels)
    uint16_t flags;      // TexFlag
    uint16_t layer;      // layer (sheet or page) within the texture set
    uint8_t color[4];    // DirectRgba: RGBA of untextured polygons. Model2Indexed: palette base r, g, b (5 bit), luma table
    uint16_t palette = 0xFFFF;   // Model2Indexed: the polygon's palette entry 0-1023 (palette animation), 0xFFFF = none
    uint16_t lodCoef = 0;        // Model2Indexed: distance coefficient index (polygon attr bits 27-31), texture mip level
};

struct TextureSet {
    Shading shading = Shading::DirectRgba;
    std::string label;                       // e.g. "BIGGER.VMC world pages" or "MAME dump t00300"
    int width = 0, height = 0;               // size of every layer
    std::vector<std::vector<uint8_t>> layers;  // DirectRgba: RGBA8 (w*h*4). Model2Indexed: one shade 0-15 per texel (w*h)
    // PC only: the same pages through BROWN.TBL / BROWN.PAL, the PC's grey look (drawn while RenderOptions::grey)
    std::vector<std::vector<uint8_t>> layersBrown;
    std::vector<uint16_t> pageIds;           // PC only: the VMC page number of each layer (texture packs' IDs, assets/mods.h)
    // Model2Indexed only:
    std::vector<uint8_t> luma;               // 128 curves x 128 entries, values 0-63
    int romArea = 0;                         // arcade texture area block 1-12 rebuilt from the ROM (TEXCHG n loads area n), 0 = other
    std::vector<uint8_t> xlat;               // color translation: [channel * 32 + base value] * 256 + brightness -> 0-255
    // the polygon palette entries 0-1023 (RGB555): a polygon's base colour = basePalette[Vertex::palette]. perf
    // (2026-10-16): one mesh serves every set and the renderer looks the colour up per set (was one mesh copy per set)
    std::vector<uint16_t> basePalette;
    // Model 2 lighting (MAME geo_parse_*): polygon luma = clamp(|normal . light| * diffuse[i] + ambient[i], 0, 255),
    // 0 instead of |normal . light| when light and viewer are on different sides; i = the polygon's parameter index.
    bool lightKnown = false;                 // false: the renderer's approximation is used
    float lightDir[3] = {0, 1, 0};           // world space, unit length
    float diffuse[32] = {}, ambient[32] = {};
    // the parameter word's specular part (MAME geo_parse_*_s): + spec^k * specScale[i], spec = (2 (n . l) n - l) . z_camera,
    // k = 1 / 2 / 4 / 8 by specCtl[i] (0 = none)
    float specScale[32] = {}, specCtl[32] = {};
};

struct Mesh {
    int textureSet = -1;                     // index into StageData::textureSets; -1: the DrawItem's variant (arcade)
    int variant = -1;                        // -1 = always drawn; else drawn only when the DrawItem asks for this variant
                                             // (arcade: one mesh for every set, textureSet -1: the item's variant picks
                                             // the texture RAM and palette, since the game swaps them by area)
    std::vector<Vertex> vertices;            // triangle list
    bool blend = false;                      // PC: the alpha-0x7F polygons, drawn after everything opaque, no depth writes
    // arcade, Draw Depth "Arcade Sort" (renderer.cpp): the Model 2 sorts each polygon by the min or max camera z of its
    // corners (attr bits 10-11, Vertex::layer bits 1-2 = kSortMin / kSortMax / kSortFar; bit 3 / 4: first / second
    // triangle of a quad). Mode 0 ("the previous polygon's z", also through hidden polygons) has 0 in those bits and
    // its key polygon here: triangle index, the corners (a triangle repeats its last), the key's mode
    struct SortKey { uint32_t tri; float p[4][3]; uint8_t mode; };
    std::vector<SortKey> sortKeys;
    // perf (2026-10-16): after the GPU upload the CPU copy can go (GameView keeps it only for models it rebuilds)
    size_t releasedCount = 0;                // vertices.size() before releaseVertices()
    // questsim (2026-11-10): VR's CPU aim ray (the Quest's aim: OpenGL ES reads no depth) tests the creatures' parts'
    // own triangles: their corners' positions (x, y, z each, the triangle list's order) kept when the vertices go
    // (GameView's releaseStageCpu, while keepMeshPositions: VR on); ~1/4 of the vertices' memory
    std::vector<float> positions;
    static inline bool keepMeshPositions = false;
    void releaseVertices() { releasedCount = vertices.size(); std::vector<Vertex>().swap(vertices); std::vector<SortKey>().swap(sortKeys); }
    size_t vertexCount() const { return vertices.empty() ? releasedCount : vertices.size(); }
};

struct Model {
    uint32_t key = 0;                        // the source's own model key (unique within the StageData)
    std::string name;                        // the source's own name for the model, if it has one (arcade: "PN_..." table)
    std::vector<Mesh> meshes;
    float boundsMin[3] = {0, 0, 0}, boundsMax[3] = {0, 0, 0};
    int preferredVariant = -1;               // variant to draw when the scene doesn't choose one
    std::vector<int> validVariants;          // variants known to look right (empty = all); others fall back to preferredVariant
    size_t triangleCount() const;   // of one variant (the preferred one, or all meshes without variants)
    void computeBounds();
};

struct Placement {
    uint32_t id = 0;                         // canonical object ID (the PC port's object IDs; what selection works on)
    uint32_t modelKey = 0;                   // which Model of this StageData
    float pos[3] = {0, 0, 0};                // world position = rot * model vertex + pos
    float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};  // row-major 3x3 (identity for the world; posed creature parts)
    int romIndex = -1;                       // arcade world: index in the chapter's placement table (zone lists use it)
};

// Everything one source supplies for one stage.
struct StageData {
    Source source = Source::PC;
    int stage = 0;                           // 0-3 = Chapters 1-4
    std::vector<Placement> placements;
    std::map<uint32_t, Model> models;
    std::vector<TextureSet> textureSets;
    std::map<uint32_t, Category> categories; // canonical ID -> category (World if missing)
    // Variant by camera position: the variant of the nearest zone point (arcade: texture set loaded at that place in the game)
    struct Zone { float pos[3]; int variant; };
    std::vector<Zone> zones;
    // Sky / backdrop models: drawn first, centered on the camera (world position = camera + offset), without writing depth;
    // only while the camera's zone variant equals `variant` (arcade: the outdoor areas have one, indoor areas none).
    struct Sky { uint32_t modelKey; int variant; float offset[3]; };
    std::vector<Sky> skies;
    int zoneVariant(const float pos[3]) const;   // -1 if there are no zones
    // Arcade draw zones (docs/decompilation.md "World drawing"): the game draws only the placements listed for the
    // current zone, which the event scripts set (BGTB n). zoneLists[n] = placement table indices (Placement::romIndex).
    std::vector<std::vector<int>> zoneLists;
    Category categoryOf(uint32_t id) const;
};

class AssetSource {
public:
    virtual ~AssetSource() = default;
    virtual Source source() const = 0;
    // False if the player doesn't have this source's data. status() says why (or what was found).
    virtual bool available() const = 0;
    virtual std::string status() const = 0;
    virtual int stageCount() const = 0;
    // Loads one stage. Returns null and sets err on failure.
    virtual std::unique_ptr<StageData> loadStage(int stage, std::string& err) = 0;
    // Extra (unplaced) models a stage's StageData should contain, e.g. creature parts the scene poses itself.
    // Call before loadStage. Sources that don't have a model simply leave it out.
    void requestModels(int stage, const std::vector<uint32_t>& ids)   // adds to earlier requests
    {
        requested_[stage].insert(requested_[stage].end(), ids.begin(), ids.end());
    }

protected:
    std::map<int, std::vector<uint32_t>> requested_;
};

// The player's choice of sources: per model, then per category; falls back to any source that has the model.
struct SourceSelection {
    Source category[kCategoryCount] = {Source::PC, Source::PC, Source::PC, Source::PC, Source::PC};
    std::map<uint32_t, Source> perModel;     // canonical ID -> source

    Source preferred(uint32_t id, Category c) const;
    void setAll(Source s);
};

// One placed model to draw, resolved through the selection.
struct DrawItem {
    const StageData* stage = nullptr;
    const Model* model = nullptr;
    const Placement* placement = nullptr;
    int variant = -1;                        // which mesh variant to draw (see Mesh::variant)
    bool background = false;                 // sky: drawn first, no depth writes
    bool hasLight = false;                   // Model 2 sets: this item's own light vector (world space, unit) instead of the set's
    float light[3] = {0, 0, 0};
    float depthBias = 0;                     // clip-space z pulled toward the camera (x w): decals on coplanar world surfaces
    float floorY = 0;                        // polish2: feet near this world height drawn over the ground (floorPull)
    uint8_t floorPull = 0;                   // 1: a model's feet (a 2.2-unit dip), 2: a floor decal (0.3)
    float push = 0;                          // depth pushed this many world units away along the view ray (screen place kept)
    bool decal = false;                      // flat model on a surface: drawn after the opaque pass without depth writes (draw order)
    float fade = 1.0f;                       // the game's draw fade (draw_fade_begin 0..0xFF / 255): PC alpha, arcade colour scale
    uint16_t flat = 0xFFFF;                  // flash: every polygon untextured in this polygon palette entry (DrawModel::flat)
    int8_t ramp = -1;                        // 4P: the drawn colours through Renderer::rampStops[ramp] (players 3 / 4's shot spark)
    // v1.3.1 (Magician Afterimages mod, GitHub #54): this item's own colour for the Magician's colour word (polygon palette
    // entry 583; RGB555 with bit 15, R in the low bits; 0 none): each afterimage its own rainbow colour
    uint16_t palOver = 0;
    uint16_t pcTint = 0;   // (the same for a PC model: its texels in that colour; 0 none)
    float bloodHue = -1.0f;                  // cosmods: Rainbow Blood's creature colour (game/cosmods.h creatureBloodHue): -1 none,
                                             // 0..1 the hue of its wounds and blood texels, 2 the cycling per-item hue
    bool sideOnly = false;                   // endfloor: drawn only outside the centred 4:3 picture (widescreen's side strips)
    // vr3 (2026-11-09, GameView::vrAround): a neighbouring zone's piece wholly outside the game camera's 4:3 view (beside,
    // behind, above the player): drawn in a VR eye's view only (Renderer::eye), with the zone, depth-tested; never flat
    bool around = false;
    // vr4 (2026-11-09): what the VR frame needs to know of a game-drawn model (never read by the renderer): 1 a
    // creature's / boss's part (the CPU aim ray's targets, the HUD's adaptive distance), 2 the shot's flash (0x95140),
    // 3 the shot's spark (0x95630, model 0x660 / 0x661: both drawn 1 unit in front of the game camera); 4 (questsim) a
    // creature's generated skin (the arcade's joints between the parts: the CPU aim ray's targets); 0 the rest
    uint8_t vrKind = 0;
};

// Resolves which source draws each canonical ID of a stage. stages[s] may be null (source missing or failed to load).
std::vector<DrawItem> resolveScene(const StageData* const stages[kSourceCount], const SourceSelection& sel);

// Helpers shared by loaders.
bool readFile(const std::string& path, std::vector<uint8_t>& out, std::string& err);
bool fileExists(const std::string& path);

} // namespace hotd
