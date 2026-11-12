// Model packs (docs/modding.md "Model packs", 2026-11-09): the meshes a mod puts in for a game model. Our own minimal
// readers (no third-party library): Wavefront OBJ (+ MTL) and glTF 2.0 (.gltf with its .bin / data: URIs, or .glb).
// Both give the same triangle soup: positions as they are in the file (the model's own space, as hotd --export-models
// writes it: x right, y up, z forward), texture v running up (OBJ's way; glTF's v is turned over on reading), one
// material per triangle. Visual only: nothing here touches game state.
#pragma once

#include "assets/mods.h"

#include <memory>
#include <string>
#include <vector>

namespace hotd {

struct ImportMaterial {
    std::string name;      // OBJ newmtl / glTF material name (the export's tex_<texture ID>-c<entry> / page_p<page>: the original texture)
    std::string picture;   // a PNG file inside the mod's folder (OBJ map_Kd, glTF image uri), absolute
    std::shared_ptr<const ReplImage> image;   // glTF: a picture embedded in the file (bufferView / data: URI), decoded
    float kd[3] = {1, 1, 1};                  // OBJ Kd / glTF baseColorFactor
};
struct ImportModel {
    struct Tri { float p[3][3]; float t[3][2]; int mat; };
    std::vector<Tri> tris;
    std::vector<ImportMaterial> mats;
};

// limits a mod's file must stay within (a broken or hostile file fails cleanly instead of eating memory)
constexpr size_t kImportMaxFileBytes = 64u << 20;
constexpr size_t kImportMaxTriangles = 1u << 20;

// a file a mod names stays inside the mod's folder: a relative path without "..", no drive or root ("" = refused)
std::string importInsideDir(const std::string& dir, const std::string& name);

// glTF 2.0: the default scene's (else every root node's) meshes, each node's transform applied (matrix or TRS), as
// triangles (modes 4 / 5 / 6). Skins and morph targets are ignored: a skinned mesh comes in its bind pose. Pictures:
// PNG only (a JPEG one: the material's colour). False with err set for anything malformed
bool readGltf(const std::string& path, ImportModel& out, std::string& err);

// the 3 x 3 rotation of mod.ini rotation = <x>, <y>, <z> (degrees: about x, then y, then z; row-major, applied as m * v)
void importRotation(const float deg[3], float m[9]);

} // namespace hotd
