// The renderer's backends: desktop OpenGL 3.3 core or OpenGL ES 3.0 through ANGLE (render/gl_backend.h)
#include "render/gl_backend.h"
#include "render/gl.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace gl {

namespace {

bool gES = false;
std::string gInUse;

// EGL / ANGLE constants (EGL/eglext_angle.h)
constexpr SDL_EGLAttrib kEglNone = 0x3038;
constexpr int kEglPlatformAngle = 0x3202;
constexpr SDL_EGLAttrib kAngleType = 0x3203, kAngleMaxMajor = 0x3204, kAngleMaxMinor = 0x3205;
constexpr SDL_EGLAttrib kAngleD3D9 = 0x3207, kAngleD3D11 = 0x3208;
constexpr SDL_EGLAttrib kAngleDeviceType = 0x3209, kAngleDeviceHardware = 0x320A, kAngleDeviceWarp = 0x320B;

struct AngleChoice {
    bool d3d9 = false, warp = false;
    int flMajor = 0, flMinor = 0;   // 0: the device's best
};
AngleChoice gAngle;

SDL_EGLAttrib* SDLCALL anglePlatformAttribs(void*)
{
    auto* a = static_cast<SDL_EGLAttrib*>(SDL_malloc(sizeof(SDL_EGLAttrib) * 12));   // (SDL frees it)
    int n = 0;
    a[n++] = kAngleType;
    a[n++] = gAngle.d3d9 ? kAngleD3D9 : kAngleD3D11;
    if (!gAngle.d3d9) {
        a[n++] = kAngleDeviceType;
        a[n++] = gAngle.warp ? kAngleDeviceWarp : kAngleDeviceHardware;
        if (gAngle.flMajor > 0) {
            a[n++] = kAngleMaxMajor;
            a[n++] = gAngle.flMajor;
            a[n++] = kAngleMaxMinor;
            a[n++] = gAngle.flMinor;
        }
    }
    a[n++] = kEglNone;
    return a;
}

// (the platform from the environment: tests and old-hardware simulation)
void readAngleEnv()
{
    gAngle = {};
    if (const char* p = std::getenv("HOTD_ANGLE_PLATFORM")) {
        gAngle.d3d9 = SDL_strcasecmp(p, "d3d9") == 0;
        gAngle.warp = SDL_strcasecmp(p, "warp") == 0;
    }
    if (const char* f = std::getenv("HOTD_ANGLE_FL")) {   // "10_0", "10.1", "11"
        gAngle.flMajor = std::atoi(f);
        const char* sep = std::strpbrk(f, "_.");
        gAngle.flMinor = sep ? std::atoi(sep + 1) : 0;
    }
}

SDL_GLContext tryDesktop(SDL_Window* window, std::string& err)
{
    if (std::getenv("HOTD_GL_FAIL")) {   // (tests: graphics without OpenGL 3.3, Auto's fallback)
        err = "OpenGL 3.3: unavailable (HOTD_GL_FAIL)";
        return nullptr;
    }
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (!ctx) {
        err = std::string("OpenGL 3.3: ") + SDL_GetError();
        return nullptr;
    }
    gES = false;
    if (const char* missing = load()) {
        err = std::string("OpenGL 3.3 function missing: ") + missing;
        SDL_GL_MakeCurrent(window, nullptr);
        SDL_GL_DestroyContext(ctx);
        return nullptr;
    }
    gInUse = "OpenGL";
    return ctx;
}

// the SDL attributes / hints for ANGLE. SDL sets a window up for EGL when it is created (its EGL surface), so these
// come before SDL_CreateWindow: prepareWindow, or the Auto fallback's new window
void setupES()
{
    readAngleEnv();
    // SDL on Windows: an ES profile with this hint goes through EGL (libEGL.dll / libGLESv2.dll) instead of WGL; the
    // platform display is ANGLE's, its attributes (Direct3D 11 or 9, the feature level) from the callback above
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_EGL_PLATFORM, kEglPlatformAngle);
    SDL_EGL_SetAttributeCallbacks(anglePlatformAttribs, nullptr, nullptr, nullptr);
}

// ANGLE compiles every shader to HLSL and through d3dcompiler (~1.9 s for the game's programs on an RTX 5080, each
// time a chapter's view is made): its compiled programs kept in memory (EGL_ANDROID_blob_cache), so the next view's
// same shaders load at once,
// and on disk (the cache folder's angle_shaders.bin, records appended: the next start's shaders load at once too)
std::map<std::string, std::vector<uint8_t>> gBlobs;
size_t gBlobBytes = 0;
std::string gBlobFile;
constexpr char kBlobMagic[8] = {'H', 'O', 'T', 'D', 'A', 'N', 'G', '1'};
constexpr size_t kBlobCap = size_t(64) << 20;   // (64 MB: the game's programs take ~90 KB)
std::mutex gBlobLock;   // (ANGLE may call from its worker threads)
void SDLCALL blobSet(const void* key, ptrdiff_t keySize, const void* value, ptrdiff_t valueSize)
{
    std::lock_guard<std::mutex> lock(gBlobLock);
    if (keySize <= 0 || valueSize <= 0 || gBlobBytes + size_t(valueSize) > kBlobCap) return;
    std::vector<uint8_t>& v = gBlobs[std::string(static_cast<const char*>(key), size_t(keySize))];
    gBlobBytes += size_t(valueSize) - v.size();
    v.assign(static_cast<const uint8_t*>(value), static_cast<const uint8_t*>(value) + valueSize);
    if (gBlobFile.empty()) return;
    if (SDL_IOStream* f = SDL_IOFromFile(gBlobFile.c_str(), "ab")) {
        if (SDL_GetIOSize(f) == 0) SDL_WriteIO(f, kBlobMagic, sizeof kBlobMagic);
        SDL_WriteU32LE(f, Uint32(keySize));
        SDL_WriteU32LE(f, Uint32(valueSize));
        SDL_WriteIO(f, key, size_t(keySize));
        SDL_WriteIO(f, value, size_t(valueSize));
        SDL_CloseIO(f);
    }
}
void blobLoad()
{
    if (gBlobFile.empty()) return;
    size_t size = 0;
    auto* data = static_cast<uint8_t*>(SDL_LoadFile(gBlobFile.c_str(), &size));
    if (!data) return;
    bool ok = size >= sizeof kBlobMagic && std::memcmp(data, kBlobMagic, sizeof kBlobMagic) == 0 && size <= kBlobCap * 2;
    for (size_t p = sizeof kBlobMagic; ok && p < size;) {
        if (p + 8 > size) { ok = false; break; }
        uint32_t k = 0, v = 0;
        std::memcpy(&k, data + p, 4), std::memcpy(&v, data + p + 4, 4);
        if (p + 8 + size_t(k) + v > size) { ok = false; break; }
        std::vector<uint8_t>& b = gBlobs[std::string(reinterpret_cast<const char*>(data) + p + 8, k)];
        gBlobBytes += size_t(v) - b.size();
        b.assign(data + p + 8 + k, data + p + 8 + k + v);
        p += 8 + size_t(k) + v;
    }
    SDL_free(data);
    if (!ok || gBlobBytes > kBlobCap) {   // (a damaged or too big file: started again)
        gBlobs.clear(), gBlobBytes = 0;
        SDL_RemovePath(gBlobFile.c_str());
    }
}
ptrdiff_t SDLCALL blobGet(const void* key, ptrdiff_t keySize, void* value, ptrdiff_t valueSize)
{
    std::lock_guard<std::mutex> lock(gBlobLock);
    auto it = gBlobs.find(std::string(static_cast<const char*>(key), size_t(keySize)));
    if (it == gBlobs.end()) return 0;
    if (valueSize >= ptrdiff_t(it->second.size())) std::memcpy(value, it->second.data(), it->second.size());
    return ptrdiff_t(it->second.size());
}
void angleBlobCache()
{
    using SetFuncs = void(SDLCALL*)(void* dpy, decltype(&blobSet), decltype(&blobGet));
    auto set = reinterpret_cast<SetFuncs>(SDL_EGL_GetProcAddress("eglSetBlobCacheFuncsANDROID"));
    if (!set || !SDL_EGL_GetCurrentDisplay()) return;
    blobLoad();
    set(SDL_EGL_GetCurrentDisplay(), blobSet, blobGet);
}

SDL_GLContext tryAngle(SDL_Window* window, std::string& err)
{
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (!ctx) {
        // (ANGLE offers OpenGL ES 3.0 from Direct3D 11 feature level 10_1: on 10_0 / Direct3D 9 graphics only ES 2.0,
        // so there is no ES 3.0 configuration and the window's surface fails)
        err = std::string("DirectX (ANGLE): ") + SDL_GetError() +
              " - the graphics need DirectX 10.1 (Direct3D 10.0 / 9 graphics offer only OpenGL ES 2.0)";
        return nullptr;
    }
    gES = true;
    static bool blobs = false;
    if (!blobs) angleBlobCache(), blobs = true;
    if (const char* missing = load()) {
        err = std::string("DirectX (ANGLE): OpenGL ES function missing: ") + missing;
        SDL_GL_MakeCurrent(window, nullptr);
        SDL_GL_DestroyContext(ctx);
        gES = false;
        return nullptr;
    }
    const char* r = reinterpret_cast<const char*>(GetString(RENDERER));
    gInUse = std::string("DirectX ") + (r && std::strstr(r, "Direct3D9") ? "9" : "11") + " (ANGLE)";
    return ctx;
}

// the Auto fallback: the window again (its title, place, size, flags), now set up for EGL
SDL_Window* recreateWindow(SDL_Window* old)
{
    const std::string title = SDL_GetWindowTitle(old);
    int x = 0, y = 0, w = 0, h = 0, mw = 0, mh = 0;
    SDL_GetWindowPosition(old, &x, &y);
    SDL_GetWindowSize(old, &w, &h);
    SDL_GetWindowMinimumSize(old, &mw, &mh);
    const SDL_WindowFlags keep = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS | SDL_WINDOW_MAXIMIZED;
    const SDL_WindowFlags flags = (SDL_GetWindowFlags(old) & keep) | SDL_WINDOW_OPENGL;
    SDL_DestroyWindow(old);
    // (SDL keeps the first window's WGL state: its EGL surface then fails. The video part started again, clean)
    int depth = 24, dbl = 1;
    SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &depth);
    SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &dbl);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return nullptr;
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, depth);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, dbl);
    setupES();
    const SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetStringProperty(p, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title.c_str());
    SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_X_NUMBER, x);
    SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y);
    SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, h);
    SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, Sint64(flags));
    SDL_Window* n = SDL_CreateWindowWithProperties(p);
    SDL_DestroyProperties(p);
    if (n && mw > 0) SDL_SetWindowMinimumSize(n, mw, mh);
    return n;
}

} // namespace

void setShaderCacheFile(const std::string& path) { gBlobFile = path; }

void prepareWindow(int backend)
{
#if defined(__ANDROID__)
    // quest: the device's OpenGL ES 3 (the Quest: Adreno, ES 3.2) through EGL; no desktop OpenGL, no ANGLE
    (void)backend;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);   // (SDL's minimums would allow a 16-bit RGB565 surface: the menus banded)
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    return;
#endif
    if (backend == kBackendAngle && angleAvailable()) {
        setupES();
    } else if (backend == kBackendGL && SDL_GetHintBoolean(SDL_HINT_OPENGL_ES_DRIVER, false)) {   // (back from ANGLE)
        SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "0");
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_EGL_PLATFORM, 0);
    }
}

const char* backendKey(int b) { return b == kBackendGL ? "gl" : b == kBackendAngle ? "angle" : "auto"; }

int backendFromKey(const char* key)
{
    if (!key) return -1;
    if (!SDL_strcasecmp(key, "auto")) return kBackendAuto;
    if (!SDL_strcasecmp(key, "gl") || !SDL_strcasecmp(key, "opengl")) return kBackendGL;
    if (!SDL_strcasecmp(key, "angle") || !SDL_strcasecmp(key, "directx") || !SDL_strcasecmp(key, "d3d")) return kBackendAngle;
    return -1;
}

bool isES() { return gES; }
std::string backendInUse() { return gInUse; }

bool angleAvailable()
{
#if defined(_WIN32)
    static int found = -1;
    if (found < 0) {
        const char* base = SDL_GetBasePath();
        const std::string dir = base ? base : "";
        SDL_PathInfo info;
        found = SDL_GetPathInfo((dir + "libGLESv2.dll").c_str(), &info) && SDL_GetPathInfo((dir + "libEGL.dll").c_str(), &info) ? 1 : 0;
    }
    return found == 1;
#else
    return false;   // (ANGLE is the Windows build's; elsewhere Mesa / the drivers offer OpenGL 3.3)
#endif
}

SDL_GLContext createContext(SDL_Window*& window, int backend, std::string& err)
{
    err.clear();
#if defined(__ANDROID__)
    (void)backend;   // quest: OpenGL ES always (prepareWindow's attributes), the ES layer's replacements on it
    SDL_GLContext es = SDL_GL_CreateContext(window);
    if (!es) {
        err = std::string("OpenGL ES 3: ") + SDL_GetError();
        return nullptr;
    }
    gES = true;
    if (const char* missing = load()) {
        err = std::string("OpenGL ES function missing: ") + missing;
        SDL_GL_MakeCurrent(window, nullptr);
        SDL_GL_DestroyContext(es);
        gES = false;
        return nullptr;
    }
    gInUse = "OpenGL ES";
    return es;
#endif
    if (backend == kBackendAngle) {
        if (angleAvailable()) return tryAngle(window, err);
        SDL_Log("renderer: DirectX (ANGLE) chosen, but libEGL.dll / libGLESv2.dll are not beside the game: OpenGL");
        backend = kBackendGL;
    }
    SDL_GLContext c = tryDesktop(window, err);
    if (c || backend == kBackendGL || !angleAvailable()) return c;
    // Auto: no OpenGL 3.3 (older graphics): Direct3D through ANGLE, in a new window set up for it
    SDL_Window* n = recreateWindow(window);
    window = n;
    if (!n) {
        err += std::string("\nDirectX (ANGLE): the window: ") + SDL_GetError();
        return nullptr;
    }
    std::string err2;
    if (SDL_GLContext a = tryAngle(window, err2)) {
        SDL_Log("renderer: %s; using %s", err.c_str(), gInUse.c_str());
        err.clear();
        return a;
    }
    err += "\n" + err2;
    return nullptr;
}

// ---- OpenGL ES 3.0: the replacements -------------------------------------------------------------------------------

namespace {

// the ES originals of the replaced functions (and a few gl.h lacks)
PFN_ShaderSource realShaderSource = nullptr;
PFN_Enable realEnable = nullptr;
PFN_Disable realDisable = nullptr;
PFN_ReadPixels realReadPixels = nullptr;
PFN_PixelStorei realPixelStorei = nullptr;
PFN_TexImage2D realTexImage2D = nullptr;
PFN_TexSubImage2D realTexSubImage2D = nullptr;
PFN_BindBuffer realBindBuffer = nullptr;
PFN_BufferData realBufferData = nullptr;
PFN_BufferSubData realBufferSubData = nullptr;
PFN_BindTexture realBindTexture = nullptr;
PFN_DeleteBuffers realDeleteBuffers = nullptr;
PFN_TexParameteri realTexParameteri = nullptr;
using PFN_DepthRangef = void(HOTD_APIENTRY*)(GLfloat n, GLfloat f);
using PFN_GetIntegerv = void(HOTD_APIENTRY*)(GLenum pname, GLint* data);
PFN_DepthRangef esDepthRangef = nullptr;
PFN_GetIntegerv esGetIntegerv = nullptr;

bool gDepthClampExt = false;   // GL_EXT_depth_clamp (ANGLE's Direct3D 11: the rasteriser's depth clip off)
GLint gPackAlign = 4, gUnpackAlign = 4;

constexpr GLenum kTextureBinding2D = 0x8069, kActiveTexture = 0x84E0;
constexpr GLint kTbWidth = 2048;   // texture buffers as 2D textures: 2048 texels a row (hotd_tb in the shaders)

std::string shaderToES(const std::string& s)
{
    const size_t eol = s.find('\n');
    if (s.compare(0, 12, "#version 330") != 0 || eol == std::string::npos) return s;
    std::string body = s.substr(eol + 1);
    std::string head =
        "#version 300 es\n"
        "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\nprecision highp usampler2D;\n"
        "precision highp isampler2D;\nprecision highp sampler2DArray;\nprecision highp usampler2DArray;\n"
        "precision highp sampler3D;\n";
    // texture buffers (ES 3.0 has none): a 2D texture of 2048-texel rows (the buffer's replacement below)
    bool tb = false;
    for (size_t p = 0; (p = body.find("samplerBuffer ", p)) != std::string::npos;) {
        size_t n0 = p + 14, n1 = n0;
        while (n1 < body.size() && (std::isalnum(uint8_t(body[n1])) || body[n1] == '_')) ++n1;
        const std::string name = body.substr(n0, n1 - n0);
        body.replace(p, 14, "highp sampler2D ");
        const std::string from = "texelFetch(" + name + ",", to = "hotd_tb(" + name + ",";
        for (size_t q = 0; (q = body.find(from, q)) != std::string::npos; q += to.size()) body.replace(q, from.size(), to);
        tb = true;
        p = n1;
    }
    if (tb)
        head += "vec4 hotd_tb(highp sampler2D s, int i) { return texelFetch(s, ivec2(i & 2047, i >> 11), 0); }\n";
    return head + "#line 2\n" + body;
}

void HOTD_APIENTRY esShaderSource(GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths)
{
    std::string s;
    for (GLsizei i = 0; i < count; ++i)
        s.append(strings[i], lengths && lengths[i] >= 0 ? size_t(lengths[i]) : std::strlen(strings[i]));
    if (std::getenv("HOTD_GLES_DUMP")) SDL_Log("ES shader:\n%s", shaderToES(s).c_str());
    const std::string es = shaderToES(s);
    const GLchar* p = es.c_str();
    realShaderSource(shader, 1, &p, nullptr);
}

void HOTD_APIENTRY esDepthRange(double n, double f) { esDepthRangef(GLfloat(n), GLfloat(f)); }

// depth clamp: ES has it only as GL_EXT_depth_clamp (ANGLE's Direct3D 11 has it; Direct3D 9 not: the near / far
// planes then clip as without it, a sliver of a decal near the camera at most)
void HOTD_APIENTRY esEnable(GLenum cap) { if (cap != DEPTH_CLAMP || gDepthClampExt) realEnable(cap); }
void HOTD_APIENTRY esDisable(GLenum cap) { if (cap != DEPTH_CLAMP || gDepthClampExt) realDisable(cap); }

void HOTD_APIENTRY esPixelStorei(GLenum pname, GLint param)
{
    if (pname == PACK_ALIGNMENT) gPackAlign = param;
    if (pname == UNPACK_ALIGNMENT) gUnpackAlign = param;
    realPixelStorei(pname, param);
}

size_t rowBytes(GLsizei w, int bpp, GLint align) { return (size_t(w) * bpp + align - 1) / align * align; }

// ES reads RGBA / UNSIGNED_BYTE only (the screenshots and the CRT's still read RGB): read RGBA, packed to RGB
void HOTD_APIENTRY esReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data)
{
    if (format != RGB || type != UNSIGNED_BYTE || w <= 0 || h <= 0) {
        realReadPixels(x, y, w, h, format, type, data);
        return;
    }
    std::vector<uint8_t> rgba(size_t(w) * h * 4);
    realPixelStorei(PACK_ALIGNMENT, 4);
    realReadPixels(x, y, w, h, RGBA, UNSIGNED_BYTE, rgba.data());
    realPixelStorei(PACK_ALIGNMENT, gPackAlign);
    const size_t stride = rowBytes(w, 3, gPackAlign);
    for (GLsizei r = 0; r < h; ++r) {
        uint8_t* d = static_cast<uint8_t*>(data) + stride * r;
        const uint8_t* s = rgba.data() + size_t(w) * 4 * r;
        for (GLsizei i = 0; i < w; ++i) d[i * 3] = s[i * 4], d[i * 3 + 1] = s[i * 4 + 1], d[i * 3 + 2] = s[i * 4 + 2];
    }
}

// ES uploads RGB data only into RGB textures: RGB data for an RGBA texture is widened (alpha 255)
std::vector<uint8_t> widenRGB(GLsizei w, GLsizei h, const void* data)
{
    std::vector<uint8_t> out(size_t(w) * h * 4, 255);
    const size_t stride = rowBytes(w, 3, gUnpackAlign);
    for (GLsizei r = 0; r < h; ++r) {
        const uint8_t* s = static_cast<const uint8_t*>(data) + stride * r;
        uint8_t* d = out.data() + size_t(w) * 4 * r;
        for (GLsizei i = 0; i < w; ++i) d[i * 4] = s[i * 3], d[i * 4 + 1] = s[i * 3 + 1], d[i * 4 + 2] = s[i * 3 + 2];
    }
    return out;
}

// questui (user 2026-11-10: "a white box in the main menu" on the Quest): the uploads in the log with their GL error,
// named by texTag (the caller's), so a run on the headset says which one a native driver refused: on Android the
// first 400 level-0 uploads (a new texture or a new size) and every failed one; on the PC with HOTD_TEX_LOG=1
const char* gTexTag = nullptr;
bool texLogOn()
{
#if defined(__ANDROID__)
    static const bool on = true;
#else
    static const bool on = std::getenv("HOTD_TEX_LOG") && *std::getenv("HOTD_TEX_LOG") != '0';
#endif
    return on;
}
void texLogClear()
{
    if (texLogOn())
        while (GetError() != 0) {}
}
void texLog(const char* fn, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLenum fmt, GLenum type, bool widened)
{
    if (!texLogOn())
        return;
    const char* tag = gTexTag;   // (a name is for the one upload after it)
    gTexTag = nullptr;
    const GLenum e = GetError();
    static int lines = 0;
    if (e == 0 && (level != 0 || lines >= 400))
        return;
    if (e == 0) {   // (once per name, size and format: a picture uploaded every frame is one line)
        static std::map<std::string, int> seen;
        char key[160];
        std::snprintf(key, sizeof key, "%s %dx%d %x", tag ? tag : "?", int(w), int(h), unsigned(ifmt));
        if (seen[key]++ > 0)
            return;
    }
    ++lines;
    GLint bound = 0;
    if (esGetIntegerv) esGetIntegerv(0x8069 /*TEXTURE_BINDING_2D*/, &bound);
    SDL_Log("menu tex: %s %s #%d %dx%d ifmt 0x%04X fmt 0x%04X type 0x%04X%s level %d %s (0x%04X)", tag ? tag : "?", fn,
            int(bound), int(w), int(h), unsigned(ifmt), unsigned(fmt), unsigned(type), widened ? " (RGB widened to RGBA8)" : "",
            int(level), e == 0 ? "ok" : "err", unsigned(e));
}

void HOTD_APIENTRY esTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border,
                                GLenum format, GLenum type, const void* data)
{
    texLogClear();
    // (questui: SRGB8 kept as it is, a legal ES upload: widened to RGBA8 it broke gamma.cpp's copy from an sRGB VR eye,
    // GL_INVALID_OPERATION on a native ES driver: the encodings must match)
    if (format == RGB && type == UNSIGNED_BYTE && internalformat != GLint(RGB) && internalformat != 0x8051 /*RGB8*/ &&
        internalformat != 0x8C41 /*SRGB8*/) {
        std::vector<uint8_t> px;
        if (data) px = widenRGB(w, h, data);
        realPixelStorei(UNPACK_ALIGNMENT, 4);
        realTexImage2D(target, level, GLint(RGBA8), w, h, border, RGBA, UNSIGNED_BYTE, data ? px.data() : nullptr);
        realPixelStorei(UNPACK_ALIGNMENT, gUnpackAlign);
        texLog("TexImage2D", level, internalformat, w, h, format, type, true);
        return;
    }
    realTexImage2D(target, level, internalformat, w, h, border, format, type, data);
    texLog("TexImage2D", level, internalformat, w, h, format, type, false);
}

void HOTD_APIENTRY esTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
                                   GLenum type, const void* data)
{
    texLogClear();
    if (format == RGB && type == UNSIGNED_BYTE && data) {   // (the textures here are RGBA: see esTexImage2D)
        const std::vector<uint8_t> px = widenRGB(w, h, data);
        realPixelStorei(UNPACK_ALIGNMENT, 4);
        realTexSubImage2D(target, level, x, y, w, h, RGBA, UNSIGNED_BYTE, px.data());
        realPixelStorei(UNPACK_ALIGNMENT, gUnpackAlign);
        texLog("TexSubImage2D", 1, 0, w, h, format, type, true);   // (level 1: only a failure is logged)
        return;
    }
    realTexSubImage2D(target, level, x, y, w, h, format, type, data);
    texLog("TexSubImage2D", 1, 0, w, h, format, type, false);
}

// texture buffers (Draw Depth "Arcade Sort"'s keys): ES 3.0 has none. The buffer's data is kept here and mirrored in
// an RGBA32F 2D texture of 2048-texel rows; binding the buffer texture binds that 2D texture (the shaders' hotd_tb)
struct TexBuf {
    std::vector<float> data;   // whole rows
    GLuint tex = 0;
};
std::map<GLuint, TexBuf> gTexBufs;        // buffer -> its data and texture
std::map<GLuint, GLuint> gTexOfBuf;       // (unused ids skipped)
GLuint gBoundTexBuf = 0;
GLint gSaved2D[32] = {};                  // the unit's 2D texture while a buffer texture is bound (the separate target)
GLuint gBoundBufTex[32] = {};

GLint activeUnit()
{
    GLint u = GLint(TEXTURE0);
    esGetIntegerv(kActiveTexture, &u);
    return std::clamp(u - GLint(TEXTURE0), 0, 31);
}

void uploadRows(TexBuf& b, size_t firstRow, size_t rows, bool whole)
{
    if (!b.tex) return;
    GLint prev = 0;
    esGetIntegerv(kTextureBinding2D, &prev);
    realBindTexture(TEXTURE_2D, b.tex);
    const size_t allRows = b.data.size() / (kTbWidth * 4);
    if (whole) {
        realTexImage2D(TEXTURE_2D, 0, GLint(RGBA32F), kTbWidth, GLsizei(allRows), 0, RGBA, FLOAT, b.data.data());
        realTexParameteri(TEXTURE_2D, TEXTURE_MIN_FILTER, GLint(NEAREST));
        realTexParameteri(TEXTURE_2D, TEXTURE_MAG_FILTER, GLint(NEAREST));
    } else if (rows) {
        realTexSubImage2D(TEXTURE_2D, 0, 0, GLint(firstRow), kTbWidth, GLsizei(rows), RGBA, FLOAT,
                          b.data.data() + firstRow * kTbWidth * 4);
    }
    realBindTexture(TEXTURE_2D, GLuint(prev));
}

void HOTD_APIENTRY esBindBuffer(GLenum target, GLuint buffer)
{
    if (target == TEXTURE_BUFFER) { gBoundTexBuf = buffer; return; }
    realBindBuffer(target, buffer);
}

void HOTD_APIENTRY esBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage)
{
    if (target != TEXTURE_BUFFER) { realBufferData(target, size, data, usage); return; }
    TexBuf& b = gTexBufs[gBoundTexBuf];
    const size_t floats = size_t(size) / sizeof(float), rowFloats = size_t(kTbWidth) * 4;
    b.data.assign(std::max<size_t>(1, (floats + rowFloats - 1) / rowFloats) * rowFloats, 0.0f);
    if (data) std::memcpy(b.data.data(), data, floats * sizeof(float));
    uploadRows(b, 0, 0, true);
}

void HOTD_APIENTRY esBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data)
{
    if (target != TEXTURE_BUFFER) { realBufferSubData(target, offset, size, data); return; }
    auto it = gTexBufs.find(gBoundTexBuf);
    if (it == gTexBufs.end() || size <= 0) return;
    TexBuf& b = it->second;
    const size_t f0 = size_t(offset) / sizeof(float), n = size_t(size) / sizeof(float);
    if (f0 + n > b.data.size()) return;
    std::memcpy(b.data.data() + f0, data, n * sizeof(float));
    const size_t rowFloats = size_t(kTbWidth) * 4, r0 = f0 / rowFloats, r1 = (f0 + n - 1) / rowFloats;
    uploadRows(b, r0, r1 - r0 + 1, false);
}

void HOTD_APIENTRY esTexBuffer(GLenum, GLenum, GLuint buffer)
{
    const GLint unit = activeUnit();
    TexBuf& b = gTexBufs[buffer];
    b.tex = gBoundBufTex[unit];
    if (b.data.empty()) b.data.assign(size_t(kTbWidth) * 4, 0.0f);
    uploadRows(b, 0, 0, true);
}

void HOTD_APIENTRY esBindTexture(GLenum target, GLuint texture)
{
    if (target != TEXTURE_BUFFER) { realBindTexture(target, texture); return; }
    const GLint unit = activeUnit();
    if (texture) {
        if (!gBoundBufTex[unit]) esGetIntegerv(kTextureBinding2D, &gSaved2D[unit]);
        gBoundBufTex[unit] = texture;
        realBindTexture(TEXTURE_2D, texture);
    } else if (gBoundBufTex[unit]) {
        gBoundBufTex[unit] = 0;
        realBindTexture(TEXTURE_2D, GLuint(gSaved2D[unit]));
    }
}

void HOTD_APIENTRY esDeleteBuffers(GLsizei n, const GLuint* buffers)
{
    for (GLsizei i = 0; i < n; ++i) gTexBufs.erase(buffers[i]);
    realDeleteBuffers(n, buffers);
}

// GPU timer queries (menuperf's GPU time): skipped on ES (no time: 0, always ready)
void HOTD_APIENTRY esGenQueries(GLsizei n, GLuint* ids) { for (GLsizei i = 0; i < n; ++i) ids[i] = GLuint(i + 1); }
void HOTD_APIENTRY esBeginQuery(GLenum, GLuint) {}
void HOTD_APIENTRY esEndQuery(GLenum) {}
void HOTD_APIENTRY esGetQueryObjectiv(GLuint, GLenum, GLint* params) { *params = 1; }
void HOTD_APIENTRY esGetQueryObjectui64v(GLuint, GLenum, uint64_t* params) { *params = 0; }

template <typename T> T proc(const char* name) { return reinterpret_cast<T>(SDL_GL_GetProcAddress(name)); }

} // namespace

const char* loadES()
{
    // everything gl.h lists, except the desktop-only functions replaced below
    static const char* const kDesktopOnly[] = {"glDepthRange", "glTexBuffer", "glGetQueryObjectiv", "glGetQueryObjectui64v",
                                               "glGenQueries", "glBeginQuery", "glEndQuery"};
#define HOTD_GL_LOAD_ES(ret, name, args) \
    name = reinterpret_cast<PFN_##name>(SDL_GL_GetProcAddress("gl" #name)); \
    if (!name) { \
        bool optional = false; \
        for (const char* d : kDesktopOnly) optional = optional || !std::strcmp(d, "gl" #name); \
        if (!optional) return "gl" #name; \
    }
    HOTD_GL_FUNCTIONS(HOTD_GL_LOAD_ES)
#undef HOTD_GL_LOAD_ES
    esDepthRangef = proc<PFN_DepthRangef>("glDepthRangef");
    esGetIntegerv = proc<PFN_GetIntegerv>("glGetIntegerv");
    if (!esDepthRangef) return "glDepthRangef";
    if (!esGetIntegerv) return "glGetIntegerv";
    const char* ext = reinterpret_cast<const char*>(GetString(0x1F03 /*EXTENSIONS*/));
    gDepthClampExt = ext && std::strstr(ext, "GL_EXT_depth_clamp") != nullptr;
    realShaderSource = ShaderSource, realEnable = Enable, realDisable = Disable, realReadPixels = ReadPixels;
    realPixelStorei = PixelStorei, realTexImage2D = TexImage2D, realTexSubImage2D = TexSubImage2D;
    realBindBuffer = BindBuffer, realBufferData = BufferData, realBufferSubData = BufferSubData;
    realBindTexture = BindTexture, realDeleteBuffers = DeleteBuffers, realTexParameteri = TexParameteri;
    ShaderSource = esShaderSource;
    DepthRange = esDepthRange;
    Enable = esEnable;
    Disable = esDisable;
    ReadPixels = esReadPixels;
    PixelStorei = esPixelStorei;
    TexImage2D = esTexImage2D;
    TexSubImage2D = esTexSubImage2D;
    BindBuffer = esBindBuffer;
    BufferData = esBufferData;
    BufferSubData = esBufferSubData;
    TexBuffer = esTexBuffer;
    BindTexture = esBindTexture;
    DeleteBuffers = esDeleteBuffers;
    GenQueries = esGenQueries;
    BeginQuery = esBeginQuery;
    EndQuery = esEndQuery;
    GetQueryObjectiv = esGetQueryObjectiv;
    GetQueryObjectui64v = esGetQueryObjectui64v;
    SDL_Log("OpenGL ES: depth clamp %s", gDepthClampExt ? "on (GL_EXT_depth_clamp)" : "unavailable");
    return nullptr;
}

void texTag(const char* name)
{
    gTexTag = name;
}

} // namespace gl
