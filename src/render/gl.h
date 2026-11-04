// Minimal OpenGL 3.3 core loader: types, the constants the renderer uses, and function pointers loaded through SDL.
// No system GL header is included, so the names live in namespace gl without the "gl" prefix (gl::Clear, gl::DrawArrays).
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define HOTD_APIENTRY __stdcall
#else
#define HOTD_APIENTRY
#endif

namespace gl {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLfloat = float;
using GLchar = char;
using GLubyte = unsigned char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;

constexpr GLenum FALSE_ = 0, TRUE_ = 1;
constexpr GLenum DEPTH_BUFFER_BIT = 0x0100, COLOR_BUFFER_BIT = 0x4000;
constexpr GLenum TRIANGLES = 0x0004, TRIANGLE_STRIP = 0x0005;
constexpr GLenum SRC_ALPHA = 0x0302, ONE_MINUS_SRC_ALPHA = 0x0303, ONE = 1, DST_COLOR = 0x0306;
constexpr GLenum DEPTH_TEST = 0x0B71, CULL_FACE = 0x0B44, BLEND = 0x0BE2, DEPTH_CLAMP = 0x864F;
constexpr GLenum SCISSOR_TEST = 0x0C11;
constexpr GLenum LEQUAL = 0x0203, LESS = 0x0201, EQUAL = 0x0202;
constexpr GLenum UNSIGNED_BYTE = 0x1401, UNSIGNED_SHORT = 0x1403, FLOAT = 0x1406, UNSIGNED_INT = 0x1405;
constexpr GLenum TEXTURE_BUFFER = 0x8C2A, RGBA32F = 0x8814;   // Draw Depth "Arcade Sort": the polygons' sort keys
constexpr GLenum R16UI = 0x8234, RED_INTEGER = 0x8D94, RGBA16UI = 0x8D76, RGBA_INTEGER = 0x8D99;
constexpr GLenum RED = 0x1903, RGB = 0x1907, RGBA = 0x1908, R8 = 0x8229, RGBA8 = 0x8058, DEPTH_COMPONENT24 = 0x81A6;
constexpr GLenum RGBA16F = 0x881A, HALF_FLOAT = 0x140B;   // crt: the filter's linear-light lines
constexpr GLenum TEXTURE_2D = 0x0DE1, TEXTURE_2D_ARRAY = 0x8C1A;
constexpr GLenum TEXTURE_MIN_FILTER = 0x2801, TEXTURE_MAG_FILTER = 0x2800, TEXTURE_WRAP_S = 0x2802, TEXTURE_WRAP_T = 0x2803;
constexpr GLenum NEAREST = 0x2600, LINEAR = 0x2601, LINEAR_MIPMAP_LINEAR = 0x2703, REPEAT = 0x2901, CLAMP_TO_EDGE = 0x812F;
constexpr GLenum TEXTURE0 = 0x84C0;
constexpr GLenum FRONT_AND_BACK = 0x0408, LINE = 0x1B01, FILL = 0x1B02;   // (Light Gun Edition: Wireframe Mode)
constexpr GLenum UNPACK_ALIGNMENT = 0x0CF5, PACK_ALIGNMENT = 0x0D05;
constexpr GLenum ARRAY_BUFFER = 0x8892, STATIC_DRAW = 0x88E4, DYNAMIC_DRAW = 0x88E8;
constexpr GLenum FRAGMENT_SHADER = 0x8B30, VERTEX_SHADER = 0x8B31, COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82;
constexpr GLenum FRAMEBUFFER = 0x8D40, RENDERBUFFER = 0x8D41, COLOR_ATTACHMENT0 = 0x8CE0, DEPTH_ATTACHMENT = 0x8D00;
constexpr GLenum FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum VENDOR = 0x1F00, RENDERER = 0x1F01, VERSION = 0x1F02;
constexpr GLenum TIME_ELAPSED = 0x88BF, QUERY_RESULT = 0x8866, QUERY_RESULT_AVAILABLE = 0x8867;   // menuperf: GPU time
constexpr GLenum SHADING_LANGUAGE_VERSION = 0x8B8C, INFO_LOG_LENGTH = 0x8B84;   // compat (gl.cpp's checks)
constexpr GLenum DEBUG_OUTPUT = 0x92E0, DEBUG_OUTPUT_SYNCHRONOUS = 0x8242, DONT_CARE = 0x1100;

#define HOTD_GL_FUNCTIONS(X) \
    X(void, Viewport, (GLint x, GLint y, GLsizei w, GLsizei h)) \
    X(void, Scissor, (GLint x, GLint y, GLsizei w, GLsizei h)) \
    X(void, Clear, (GLbitfield mask)) \
    X(void, ClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
    X(void, Enable, (GLenum cap)) \
    X(void, Uniform4f, (GLint loc, GLfloat x, GLfloat y, GLfloat z, GLfloat w)) \
    X(void, BlendFunc, (GLenum sfactor, GLenum dfactor)) \
    X(void, Disable, (GLenum cap)) \
    X(void, DepthFunc, (GLenum func))     X(void, DepthMask, (GLboolean flag)) \
    X(void, DepthRange, (double n, double f)) \
    X(void, ColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a)) \
    X(GLenum, GetError, (void)) \
    X(void, Finish, (void)) \
    X(const GLubyte*, GetString, (GLenum name)) \
    X(void, ReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data)) \
    X(void, PixelStorei, (GLenum pname, GLint param)) \
    X(void, GenTextures, (GLsizei n, GLuint* textures)) \
    X(void, DeleteTextures, (GLsizei n, const GLuint* textures)) \
    X(void, BindTexture, (GLenum target, GLuint texture)) \
    X(void, ActiveTexture, (GLenum texture)) \
    X(void, TexParameteri, (GLenum target, GLenum pname, GLint param)) \
    X(void, TexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void* data)) \
    X(void, TexSubImage2D, (GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const void* data)) \
    X(void, TexImage3D, (GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLsizei d, GLint border, GLenum format, GLenum type, const void* data)) \
    X(void, TexSubImage3D, (GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, GLenum format, GLenum type, const void* data)) \
    X(void, GenerateMipmap, (GLenum target)) \
    X(GLuint, CreateShader, (GLenum type)) \
    X(void, DeleteShader, (GLuint shader)) \
    X(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths)) \
    X(void, CompileShader, (GLuint shader)) \
    X(void, GetShaderiv, (GLuint shader, GLenum pname, GLint* params)) \
    X(void, GetShaderInfoLog, (GLuint shader, GLsizei size, GLsizei* length, GLchar* log)) \
    X(GLuint, CreateProgram, (void)) \
    X(void, DeleteProgram, (GLuint program)) \
    X(void, AttachShader, (GLuint program, GLuint shader)) \
    X(void, LinkProgram, (GLuint program)) \
    X(void, GetProgramiv, (GLuint program, GLenum pname, GLint* params)) \
    X(void, GetProgramInfoLog, (GLuint program, GLsizei size, GLsizei* length, GLchar* log)) \
    X(void, UseProgram, (GLuint program)) \
    X(GLint, GetUniformLocation, (GLuint program, const GLchar* name)) \
    X(void, Uniform1i, (GLint loc, GLint v)) \
    X(void, Uniform1f, (GLint loc, GLfloat v)) \
    X(void, Uniform2f, (GLint loc, GLfloat x, GLfloat y))                X(void, Uniform3f, (GLint loc, GLfloat x, GLfloat y, GLfloat z))     X(void, Uniform1fv, (GLint loc, GLsizei count, const GLfloat* v)) \
    X(void, Uniform3fv, (GLint loc, GLsizei count, const GLfloat* v)) \
    X(void, UniformMatrix4fv, (GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value))     X(void, UniformMatrix3fv, (GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value)) \
    X(void, GenVertexArrays, (GLsizei n, GLuint* arrays)) \
    X(void, DeleteVertexArrays, (GLsizei n, const GLuint* arrays)) \
    X(void, BindVertexArray, (GLuint array)) \
    X(void, GenBuffers, (GLsizei n, GLuint* buffers)) \
    X(void, DeleteBuffers, (GLsizei n, const GLuint* buffers)) \
    X(void, BindBuffer, (GLenum target, GLuint buffer)) \
    X(void, BufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage)) \
    X(void, BufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data)) \
    X(void, TexBuffer, (GLenum target, GLenum internalformat, GLuint buffer)) \
    X(void, VertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* offset)) \
    X(void, VertexAttribIPointer, (GLuint index, GLint size, GLenum type, GLsizei stride, const void* offset)) \
    X(void, EnableVertexAttribArray, (GLuint index)) \
    X(void, DrawArrays, (GLenum mode, GLint first, GLsizei count)) \
    X(void, GenFramebuffers, (GLsizei n, GLuint* fbos)) \
    X(void, DeleteFramebuffers, (GLsizei n, const GLuint* fbos)) \
    X(void, BindFramebuffer, (GLenum target, GLuint fbo)) \
    X(void, FramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
    X(GLenum, CheckFramebufferStatus, (GLenum target)) \
    X(void, GenRenderbuffers, (GLsizei n, GLuint* rbos)) \
    X(void, DeleteRenderbuffers, (GLsizei n, const GLuint* rbos)) \
    X(void, BindRenderbuffer, (GLenum target, GLuint rbo)) \
    X(void, RenderbufferStorage, (GLenum target, GLenum format, GLsizei w, GLsizei h)) \
    X(void, FramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum rbtarget, GLuint rbo)) \
    X(void, GenQueries, (GLsizei n, GLuint* ids)) \
    X(void, BeginQuery, (GLenum target, GLuint id)) \
    X(void, EndQuery, (GLenum target)) \
    X(void, GetQueryObjectiv, (GLuint id, GLenum pname, GLint* params)) \
    X(void, GetQueryObjectui64v, (GLuint id, GLenum pname, uint64_t* params))

#define HOTD_GL_DECLARE(ret, name, args) using PFN_##name = ret(HOTD_APIENTRY*) args; extern PFN_##name name;
HOTD_GL_FUNCTIONS(HOTD_GL_DECLARE)
#undef HOTD_GL_DECLARE

// Loads every function above through SDL_GL_GetProcAddress (needs a current context). Returns the first missing name, or null.
const char* load();
// Light Gun Edition (Wireframe Mode): glPolygonMode where the context has it (desktop OpenGL; ANGLE's ES: nothing)
void polygonMode(GLenum face, GLenum mode);

// compat (2026-10-29: the game on many PCs; checked on Mesa's strict software driver). HOTD_GL_DEBUG=1: a debug context
// (contextAttributes, before SDL_GL_CreateContext), KHR_debug's messages (every error / warning) and the shaders'
// compiler logs into hotd.log (SDL_Log). Off: nothing changes
bool debugEnabled();
void setDebug(bool on);     // (--gl-check: on without the variable)
uint64_t debugProblems();   // the messages so far that are errors, undefined behaviour, deprecated or portability
void contextAttributes();   // 3.3 core, 24-bit depth, double buffered (+ the debug flag with HOTD_GL_DEBUG=1)
void debugInit();           // after load(): the debug callback (HOTD_GL_DEBUG=1)
void debugSummary();        // at the end: how many messages (HOTD_GL_DEBUG=1)
// the driver into the log (vendor, renderer, version, GLSL); false (and a clear warning) when below OpenGL 3.3
bool logDriver(const char* who);
// a shader's / program's compiler log into the log (always on failure; the warnings with HOTD_GL_DEBUG=1); the result:
// compiled / linked
bool shaderLog(GLuint shader, const char* what);
bool programLog(GLuint program, const char* what);
// the bound framebuffer's status (the fallbacks: a format the driver refuses turns that feature off); logs a failure
bool framebufferOk(const char* what);

} // namespace gl
