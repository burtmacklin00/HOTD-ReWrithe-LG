#include "render/gl.h"
#include "render/gl_backend.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace gl {

#define HOTD_GL_DEFINE(ret, name, args) PFN_##name name = nullptr;
HOTD_GL_FUNCTIONS(HOTD_GL_DEFINE)
#undef HOTD_GL_DEFINE

const char* load()
{
    if (isES()) return loadES();   // (ANGLE: render/gl_backend.cpp)
#define HOTD_GL_LOAD(ret, name, args) \
    name = reinterpret_cast<PFN_##name>(SDL_GL_GetProcAddress("gl" #name)); \
    if (!name) return "gl" #name;
    HOTD_GL_FUNCTIONS(HOTD_GL_LOAD)
#undef HOTD_GL_LOAD
    return nullptr;
}

void polygonMode(GLenum face, GLenum mode)
{
    using Fn = void(HOTD_APIENTRY*)(GLenum, GLenum);
    static Fn fn = isES() ? nullptr : reinterpret_cast<Fn>(SDL_GL_GetProcAddress("glPolygonMode"));
    if (fn) fn(face, mode);
}

// compat (2026-10-29): HOTD_GL_DEBUG=1's checks (gl.h)
namespace {
int gDebugOn = -1;   // -1: not read yet (HOTD_GL_DEBUG)
}
bool debugEnabled()
{
    if (gDebugOn < 0) {
        const char* e = std::getenv("HOTD_GL_DEBUG");
        gDebugOn = e && e[0] == '1' ? 1 : 0;
    }
    return gDebugOn == 1;
}
void setDebug(bool on) { gDebugOn = on ? 1 : 0; }

void contextAttributes()
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, debugEnabled() ? SDL_GL_CONTEXT_DEBUG_FLAG : 0);
}

namespace {
using DebugProc = void(HOTD_APIENTRY*)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* user);
using PFN_DebugMessageCallback = void(HOTD_APIENTRY*)(DebugProc callback, const void* user);
using PFN_DebugMessageControl = void(HOTD_APIENTRY*)(GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint* ids, GLboolean enabled);
std::mutex gDebugMutex;
struct Seen {
    uint64_t n = 0;
    std::string text;   // the first one's
};
std::map<std::string, Seen> gDebugSeen;   // each kind of message: how many times (the first 3 logged)
std::atomic<uint64_t> gDebugErrors{0}, gDebugWarnings{0}, gDebugNotes{0};
std::atomic<uint64_t> gDebugSerious{0};   // errors, undefined behaviour, deprecated, portability (debugProblems)

const char* debugType(GLenum t)
{
    switch (t) {
    case 0x824C: return "error";
    case 0x824D: return "deprecated";
    case 0x824E: return "undefined behaviour";
    case 0x824F: return "portability";
    case 0x8250: return "performance";
    default: return "other";
    }
}
const char* debugSeverity(GLenum s)
{
    switch (s) {
    case 0x9146: return "high";
    case 0x9147: return "medium";
    case 0x9148: return "low";
    default: return "note";
    }
}

void HOTD_APIENTRY debugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void*)
{
    (void)source;
    if (type == 0x8268 || type == 0x8269 || type == 0x826A)   // markers, push / pop groups
        return;
    std::string text = length > 0 ? std::string(message, size_t(length)) : std::string(message ? message : "");
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    const bool note = severity == 0x826B;   // (notifications: NVIDIA's "buffer will use video memory"; counted only)
    (type == 0x824C ? gDebugErrors : note ? gDebugNotes : gDebugWarnings)++;
    if (type >= 0x824C && type <= 0x824F) ++gDebugSerious;
    uint64_t n;
    {   // (counted by type and id: a driver puts numbers in its texts (Mesa's offsets), one id is one kind of message)
        std::lock_guard<std::mutex> lock(gDebugMutex);
        auto& seen = gDebugSeen[std::string(debugType(type)) + " id " + std::to_string(id)];
        if (seen.text.empty()) seen.text = text;
        n = ++seen.n;
    }
    if (!note && n <= 3)
        SDL_Log("GL debug: %s (%s, id %u): %s%s", debugType(type), debugSeverity(severity), id, text.c_str(),
                n == 3 ? " (repeats are counted, not logged)" : "");
}
} // namespace

void debugInit()
{
    if (!debugEnabled())
        return;
    auto cb = reinterpret_cast<PFN_DebugMessageCallback>(SDL_GL_GetProcAddress("glDebugMessageCallback"));
    if (!cb) cb = reinterpret_cast<PFN_DebugMessageCallback>(SDL_GL_GetProcAddress("glDebugMessageCallbackARB"));
    auto control = reinterpret_cast<PFN_DebugMessageControl>(SDL_GL_GetProcAddress("glDebugMessageControl"));
    if (!control) control = reinterpret_cast<PFN_DebugMessageControl>(SDL_GL_GetProcAddress("glDebugMessageControlARB"));
    if (!cb) {
        SDL_Log("GL debug: this driver has no KHR_debug / ARB_debug_output (only glGetError is checked)");
        return;
    }
    Enable(DEBUG_OUTPUT);
    Enable(DEBUG_OUTPUT_SYNCHRONOUS);
    cb(debugCallback, nullptr);
    if (control)
        control(DONT_CARE, DONT_CARE, DONT_CARE, 0, nullptr, TRUE_);
    while (GetError() != 0) {}
    SDL_Log("GL debug: on (KHR_debug messages and shader logs into this log)");
}

uint64_t debugProblems() { return gDebugSerious.load(); }

void debugSummary()
{
    if (!debugEnabled())
        return;
    std::lock_guard<std::mutex> lock(gDebugMutex);
    SDL_Log("GL debug: %llu errors, %llu warnings, %llu notes, %zu kinds of messages", (unsigned long long)gDebugErrors.load(),
            (unsigned long long)gDebugWarnings.load(), (unsigned long long)gDebugNotes.load(), gDebugSeen.size());
    for (const auto& [key, seen] : gDebugSeen)
        if (seen.n > 3) SDL_Log("GL debug: %llu x %s: %s", (unsigned long long)seen.n, key.c_str(), seen.text.c_str());
}

bool logDriver(const char* who)
{
    auto str = [](GLenum e) {
        const char* s = reinterpret_cast<const char*>(GetString(e));
        return s ? s : "?";
    };
    const char* version = str(VERSION);
    SDL_Log("%s: OpenGL %s; %s; %s; GLSL %s", who, version, str(VENDOR), str(RENDERER), str(SHADING_LANGUAGE_VERSION));
    int major = 0, minor = 0;
    const char* v = version;   // (an OpenGL ES string starts "OpenGL ES ": the numbers after it)
    while (*v && (*v < '0' || *v > '9')) ++v;
    if (std::sscanf(v, "%d.%d", &major, &minor) != 2 || major * 10 + minor < (isES() ? 30 : 33)) {   // (ANGLE: OpenGL ES 3.0)
        SDL_Log("WARNING: the graphics driver reports OpenGL %s; the game needs OpenGL 3.3. Update the graphics driver (from "
                "NVIDIA, AMD or Intel); if the game misbehaves, start it with \"Play (Safe Mode).bat\"", version);
        return false;
    }
    return true;
}

namespace {
void infoLog(bool shader, GLuint id, const char* what, bool ok)
{
    GLint len = 0;
    (shader ? GetShaderiv : GetProgramiv)(id, INFO_LOG_LENGTH, &len);
    if (len <= 1)
        return;
    std::vector<char> log(size_t(len) + 1, 0);
    (shader ? GetShaderInfoLog : GetProgramInfoLog)(id, len, nullptr, log.data());
    std::string s(log.data());
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    if (!s.empty())
        SDL_Log("%s %s: %s: %s", shader ? "shader" : "program", what, ok ? "notes" : "FAILED", s.c_str());
}
} // namespace

bool shaderLog(GLuint shader, const char* what)
{
    GLint ok = 0;
    GetShaderiv(shader, COMPILE_STATUS, &ok);
    if (debugEnabled() || !ok)
        infoLog(true, shader, what, ok != 0);
    return ok != 0;
}

bool programLog(GLuint program, const char* what)
{
    GLint ok = 0;
    GetProgramiv(program, LINK_STATUS, &ok);
    if (debugEnabled() || !ok)
        infoLog(false, program, what, ok != 0);
    return ok != 0;
}

bool framebufferOk(const char* what)
{
    const GLenum st = CheckFramebufferStatus(FRAMEBUFFER);
    if (st == FRAMEBUFFER_COMPLETE)
        return true;
    SDL_Log("graphics: this driver cannot draw into the %s framebuffer (status 0x%X): that feature is turned off", what, st);
    return false;
}

} // namespace gl
