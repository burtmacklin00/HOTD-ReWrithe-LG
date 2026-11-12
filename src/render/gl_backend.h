// The renderer's backends (angle, 2026-10-29): desktop OpenGL 3.3 core, or OpenGL ES 3.0 through ANGLE (Google's
// translator to Direct3D 11; libEGL.dll + libGLESv2.dll beside hotd.exe) for older graphics without OpenGL 3.3
// (Intel HD 2000 / 3000, GMA X4500, older AMD / NVIDIA: Direct3D 10 class). The renderer itself is written once against
// render/gl.h; on ANGLE this layer swaps a few of gl.h's function pointers for ES versions (the shaders rewritten as
// GLSL ES 3.00, texture buffers as 2D textures, RGB reads / uploads as RGBA, depth clamp only where the extension is,
// no GPU timer queries). The desktop path never goes through it: its function pointers and shaders are unchanged.
// quest (2026-11-03): Android (the Meta Quest) has OpenGL ES only: there the same ES layer runs on the device's own
// driver (EGL, OpenGL ES 3.x; no ANGLE), whatever the Renderer setting says. docs/renderer_backends.md, docs/quest.md
#pragma once

#include <string>

struct SDL_Window;
struct SDL_GLContextState;

namespace gl {

// Video > Renderer (launcher.ini renderer=, --renderer): Auto = OpenGL 3.3 where the driver offers it, else ANGLE
enum Backend { kBackendAuto = 0, kBackendGL = 1, kBackendAngle = 2 };
const char* backendKey(int b);        // "auto" / "gl" / "angle"
int backendFromKey(const char* key);  // -1: unknown

// Before SDL_CreateWindow (after the OpenGL 3.3 attributes): the ES attributes when the backend is ANGLE (SDL sets a
// window up for EGL when it is created)
void prepareWindow(int backend);
// ANGLE's compiled shaders kept in this file too (the next start skips the ~2 s compile); empty: memory only
void setShaderCacheFile(const std::string& path);
// Creates and makes current the window's context for the backend asked for (Auto: OpenGL 3.3 core, then ANGLE) and
// loads gl.h's functions. Null on failure, err says why in plain words. Auto's fallback to ANGLE makes a new window
// (the same title, place, size and flags: window changes; the caller sets its icon again).
// Tests (env): HOTD_ANGLE_PLATFORM=d3d11 (default) | d3d9 | warp (D3D11's software rasteriser);
// HOTD_ANGLE_FL=10_0 | 10_1 | 11_0 (D3D11's highest feature level: old graphics simulated); HOTD_GL_FAIL=1 (tests):
// the OpenGL 3.3 attempt fails, as on graphics without it (Auto's fallback)
SDL_GLContextState* createContext(SDL_Window*& window, int backend, std::string& err);

bool isES();                   // the context is OpenGL ES (ANGLE)
std::string backendInUse();    // "OpenGL" / "DirectX 11 (ANGLE)" / "DirectX 9 (ANGLE)" (empty before a context)
bool angleAvailable();         // libGLESv2.dll / libEGL.dll found beside the game

// gl.cpp's load() on ES: the functions, with the ES replacements installed. Returns the first missing name, or null
const char* loadES();
// questui: the name the ES layer's upload log gives the next uploads ("menu tex: <name> ..."; Android, HOTD_TEX_LOG=1)
void texTag(const char* name);

} // namespace gl
