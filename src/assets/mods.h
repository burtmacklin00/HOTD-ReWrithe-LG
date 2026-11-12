// Texture packs (docs/mods.md): folders data\mods\<pack>\ with an optional mod.ini and textures\*.png named by texture
// ID; the renderer swaps a pack's picture in for that texture when it uploads a stage (Renderer::upload), the original
// where a pack has none. Visual only: nothing here touches game state (netplay: each PC may use its own packs).
//
// Texture IDs (the token at the end of a PNG's name, after its last '_'; anything before it is free text):
//   PC models:     p<page>       the BIGGER.VMC page number (256 x 256; world pages 512 and up), e.g. world_p0517.png
//   arcade models: <16 hex>      a hash of the texture's texels (4-bit shades) and size, the same in every texture area
//                                and chapter that holds the same picture, e.g. zonbi_kao_3f09c1d2aa804b17.png
// hotd --export-textures <folder> writes every texture of the chosen model source under these names.
#pragma once

#include "assets/assets.h"
#include "util/recolour.h"

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace hotd {

// one picture a pack puts in (RGBA8, any size up to 2048 x 2048)
struct ReplImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

// what the renderer needs for one stage: the pictures, and per texture set its (shader key -> picture) entries.
// Shader key: PC = the layer (page slot) index; arcade = sheet << 17 | x/32 << 11 | y/32 << 6 | log2(w/32) << 3 | log2(h/32)
// Arcade pictures for one polygon colour only (<id>-c<palette entry>): the polygons of that texture in that palette
// entry get a virtual key kVirtualKey + n (n carried in Vertex::lodCoef bits 5-15 as n + 1, set by the renderer's
// upload); virtualKeys maps (arcade key << 10 | palette entry) -> n
constexpr uint32_t kVirtualKey = 1u << 18;
constexpr uint32_t kMaxVirtual = 2047;
struct StageReplacements {
    std::vector<std::shared_ptr<const ReplImage>> images;
    // per TextureSet: (key, picture, reference) - reference (arcade): the original's colours as exported (textureRgba), whose
    // brightness the shader compares with the game's own colour of the pixel; kNoRef on the PC
    struct Entry { uint32_t key, image, ref; bool additive = false; };   // additive: see renderer.cpp (arcade pack pictures)
    static constexpr uint32_t kNoRef = ~0u;
    std::vector<std::vector<Entry>> sets;
    std::map<uint64_t, uint32_t> virtualKeys;
    // custom models (models/<label>_o<object ID hex>.obj): model key -> its triangles (textured ones keyed like the
    // pictures: PC a layer past the set's pages, arcade a virtual key), drawn instead of the original's meshes
    std::map<uint32_t, std::vector<Vertex>> models;
    // depthopt (2026-10-27): model key -> its depth mode (render/renderer.h DrawDepth: 0 Depth Buffer, 1 Arcade Sort) for
    // the models a mod draws (its custom model, or its pictures on the model's polygons), by that mod's mod.ini depth =
    // (auto: buffer for a custom model, none for pictures: the game's setting). The renderer draws each such item that
    // way whatever the game's Draw Depth is (Renderer::draw)
    std::map<uint32_t, int> modelDepth;
    bool empty() const { return images.empty() && models.empty(); }
};
// the arcade shader key of a polygon (its first vertex)
uint32_t arcadeTextureKey(const Vertex& v);

// a mod's setting (mod.ini [settings]: <key> = <label> | <default> | <choice>, <choice> ... | <help>), shown on its
// page under Options > Mods; the value chosen is saved per PC (launcher.ini modset.<mod>.<key>=)
struct ModSetting {
    std::string key, label, def, help;
    std::vector<std::string> choices;
};

struct ModPack {
    std::string id;                  // the folder name (what launcher.ini and --mod use)
    std::string dir;
    std::string name, author, version, description;
    // docs/modding.md: the mod's type: "textures" (a texture pack: the default) or a built-in mod's code ("ragdoll");
    // defaultOn: on until the player switches it (mod.ini default = on / off; the built-in mods are off)
    std::string type = "textures";
    bool defaultOn = true;
    bool builtin = false;            // made by the game (no folder found for it)
    std::vector<ModSetting> settings;
    // depthopt: mod.ini depth = auto | sort | buffer: how this mod's models / pictures hide each other (-1 auto: a
    // custom model per pixel, pictures on arcade models as the game's setting; 0 buffer; 1 sort: render/renderer.h DrawDepth)
    int depth = -1;
    int targets = 3;                 // bit 0: PC models, bit 1: arcade models (mod.ini models = pc / arcade / both)
    std::map<std::string, std::string> pngs;   // texture ID (lower case) -> file
    std::map<uint32_t, std::string> objs;      // object ID -> models/<label>_o<hex>.obj (custom models, docs/mods.md)
    // modelpacks (2026-11-09, docs/modding.md "Model packs"): mod.ini [model] sections, one game model drawn as another
    // mesh each; they come before the objs above (which act as entries without a transform). Draw only
    struct ModelRepl {
        uint32_t id = 0;                       // the model (object ID) replaced
        std::string file;                      // an OBJ / glTF / GLB in the mod's folder (absolute); empty: gameId
        uint32_t gameId = 0;                   // file = game:<hex>: another model of the same stage (the player's own files)
        float scale[3] = {1, 1, 1}, offset[3] = {0, 0, 0}, rotation[3] = {0, 0, 0};   // scale, then rotate (degrees x, y, z), then move
        int targets = 3;                       // models = pc / arcade / both (bit 0 PC, bit 1 arcade; within the pack's own)
        int line = 0;                          // the [model] section's line in mod.ini (messages)
    };
    std::vector<ModelRepl> modelRepls;
    struct Rule { std::string pattern; Recolour op; };
    std::vector<Rule> rules;         // [recolour] <pattern> = <op>, pattern on "<label>_<id>" (* and ? wildcards)
    int bloodRgb = -1;               // [recolour] blood = #RRGGBB: the blood colour (the Blood Color option's custom path)
};

// one texture of a stage as the packs name it (also the export's list)
struct TexEntry {
    int set = 0;
    uint32_t key = 0;                // shader key (StageReplacements)
    std::string id;                  // texture ID token
    std::string label;               // first model using it (sanitised), for file names / rules
    int width = 0, height = 0;
    int sheet = 0, x = 0, y = 0;     // arcade: where in the set's texture RAM
    bool translucent = false;        // arcade: drawn by translucent polygons (shade 15 = transparent)
    bool blood = false;              // arcade: drawn in the blood colour entry (palette 1023); PC: set by the export
    uint16_t palette = 0xFFFF;       // arcade: the most used polygon palette entry / luma curve (the export's colours)
    uint8_t lumaBase = 0;
    std::map<uint16_t, uint8_t> palettes;   // arcade: every palette entry its polygons use (-> its luma curve)
    std::set<std::string> users;     // model names (arcade) / object IDs (PC)
};

// every texture of a stage (arcade: each texture set's rectangles its models use)
// allSets false (the export): arcade, each model's own sets only (its valid variants, else its preferred one)
std::vector<TexEntry> listStageTextures(const StageData& stage, bool allSets = true);
// a texture as RGBA8: PC its page; arcade its shades through the entry's palette colour at full light, before the
// monitor curve (the colours an unchanged export puts back)
bool textureRgba(const StageData& stage, const TexEntry& e, ReplImage& out);

// the packs found in these folders (each sub-folder one pack), sorted by id
std::vector<ModPack> findModPacks(const std::vector<std::string>& roots);
// the mods: findModPacks, plus the built-in Ragdoll mod (type ragdoll, off) when no folder holds one
std::vector<ModPack> findMods(const std::vector<std::string>& roots);
// depthopt: mod.ini depth = <value>: -1 auto, 0 buffer, 1 sort; -2 not one of them
int parseModDepth(const std::string& value);
// depthopt: the depth mode a mod's items get (-1: the game's own): its custom models (model true) or its pictures
int modDepthFor(const ModPack& p, bool model);
// depthopt (hotd --mods-test): a mod's depth = switches the items it draws (a custom model on a small stage). Returns
// the failures
int modDepthTest();
// modelpacks (hotd --mods-test): [model] parsing, OBJ / glTF / GLB loading on a small stage, the transform, game:<id>,
// and malformed files / lines failing cleanly (no model, a message, no crash). Returns the failures; adds its checks
int modelPackTest(int& checks);
// a [settings] line's value (<label> | <default> | <choices> | <help>); false: not that form
bool parseModSetting(const std::string& key, const std::string& value, ModSetting& out);

class TexturePacks {
public:
    // the active packs, highest priority first (its picture wins where two packs have the same texture)
    void setActive(std::vector<ModPack> packs);
    const std::vector<ModPack>& active() const { return active_; }
    bool empty() const { return active_.empty(); }
    // the blood colour of the first active pack that sets one, else -1
    int bloodRgb() const;
    // the pictures for one stage (call while the stage still has its texel data, i.e. before the upload frees it)
    StageReplacements build(const StageData& stage) const;

private:
    std::vector<ModPack> active_;
    mutable std::mutex mu_;
    mutable std::map<std::string, std::shared_ptr<const ReplImage>> cache_;   // file -> picture
    std::shared_ptr<const ReplImage> load(const std::string& file) const;
};

// the process's packs (main sets them from launcher.ini / --mod / --no-mods; the renderer reads them at upload)
TexturePacks& texturePacks();

// hotd --export-textures: the textures of one stage to <folder>/<label>_<id>.png (each ID once over every stage: the
// index merges them); bloodLayers (PC): the layers holding blood texels. Returns the number of files written
struct ExportIndex {
    struct Row {
        std::string file;
        int width = 0, height = 0;
        bool translucent = false, blood = false;
        std::set<int> chapters;
        std::set<std::string> users;
        std::set<uint16_t> palettes;   // arcade: the polygon palette entries it is drawn in
        std::string bloodFile;         // arcade: the <id>-c1023 picture (the blood colour entry)
    };
    std::map<std::string, Row> rows;   // by texture ID
};
int exportStageTextures(const StageData& stage, const std::string& folder, ExportIndex& index,
                        const std::set<int>* bloodLayers = nullptr);
bool writeExportIndex(const ExportIndex& index, const std::string& file, const std::string& source);

} // namespace hotd

namespace hotd {
// hotd --export-textures <folder> [--models arcade|pc]: every texture of the chosen source (all chapters) to
// <folder>/<arcade|pc>/<label>_<id>.png and index.tsv; models 0 arcade (regions: the checked ROM regions), 1 PC.
// Returns the files written, -1 with err on failure
int exportTextures(const std::string& folder, int models, const std::string& regions, const std::string& pcDir, std::string& err);
} // namespace hotd

namespace hotd {
// hotd --export-models <folder> --model-ids <hex,...> [--models arcade|pc]: those models as OBJ + MTL + PNG in
// <folder>/<arcade|pc>/models/<label>_o<id>.obj (the start of a custom model; docs/mods.md). Returns the count
int exportModels(const std::string& folder, int models, const std::string& regions, const std::string& pcDir,
                 const std::vector<uint32_t>& ids, std::string& err);
} // namespace hotd
