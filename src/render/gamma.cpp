#include "render/gamma.h"

#include "render/gl.h"
#include "render/gl_backend.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace hotd {

namespace {

int gGamma = kGammaDefault;
int gBlack = kBlackDefault;   // (bugs4, #23: Black Level)

// the finished frame copied into uSrc (the same size): drawn back through the curve. An sRGB copy (a VR eye's
// SRGB8_ALPHA8 image, its values display-referred as the flat game's: vr_openxr.cpp) is decoded by the sampler: encoded
// back first, so the curve works on the values as stored
const char* kVs = R"(#version 330 core
void main() {
    vec2 c = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    gl_Position = vec4(c * 2.0 - 1.0, 0.0, 1.0);
}
)";
const char* kFs = R"(#version 330 core
out vec4 o;
uniform sampler2D uSrc;
uniform float uInv;
uniform int uSrgb;
void main() {
    vec3 c = texelFetch(uSrc, ivec2(gl_FragCoord.xy), 0).rgb;
    if (uSrgb == 1) c = mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
    o = vec4(pow(clamp(c, 0.0, 1.0), vec3(uInv)), 1.0);
}
)";

// (not in gl.h's list: loaded here, so nothing else changes)
using PFN_CopyTexSubImage2D = void(HOTD_APIENTRY*)(gl::GLenum, gl::GLint, gl::GLint, gl::GLint, gl::GLint, gl::GLint, gl::GLsizei, gl::GLsizei);
using PFN_GetFbAttachParam = void(HOTD_APIENTRY*)(gl::GLenum, gl::GLenum, gl::GLenum, gl::GLint*);
constexpr gl::GLenum kRGB8 = 0x8051, kSRGB8 = 0x8C41, kColorEncoding = 0x8210, kSrgb = 0x8C40;

struct Pass {
    bool tried = false, ok = false;
    gl::GLuint prog = 0, vao = 0, tex = 0;
    gl::GLint uInv = -1, uSrgb = -1;
    int w = 0, h = 0;
    bool texSrgb = false;
    PFN_CopyTexSubImage2D copy = nullptr;
    PFN_GetFbAttachParam attach = nullptr;
};
Pass gPass;

gl::GLuint compile(gl::GLenum type, const char* src)
{
    gl::GLuint s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &src, nullptr);
    gl::CompileShader(s);
    gl::shaderLog(s, "gamma");
    gl::GLint ok = 0;
    gl::GetShaderiv(s, gl::COMPILE_STATUS, &ok);
    if (!ok) { gl::DeleteShader(s); return 0; }
    return s;
}

bool build(Pass& p)
{
    p.copy = reinterpret_cast<PFN_CopyTexSubImage2D>(SDL_GL_GetProcAddress("glCopyTexSubImage2D"));
    p.attach = reinterpret_cast<PFN_GetFbAttachParam>(SDL_GL_GetProcAddress("glGetFramebufferAttachmentParameteriv"));
    const gl::GLuint v = compile(gl::VERTEX_SHADER, kVs), f = v ? compile(gl::FRAGMENT_SHADER, kFs) : 0;
    if (!p.copy || !v || !f) {
        if (v) gl::DeleteShader(v);
        SDL_Log("gamma: the pass is off (%s)", p.copy ? "the shader failed" : "no glCopyTexSubImage2D");
        return false;
    }
    p.prog = gl::CreateProgram();
    gl::AttachShader(p.prog, v);
    gl::AttachShader(p.prog, f);
    gl::LinkProgram(p.prog);
    gl::programLog(p.prog, "gamma");
    gl::DeleteShader(v);
    gl::DeleteShader(f);
    gl::GLint ok = 0;
    gl::GetProgramiv(p.prog, gl::LINK_STATUS, &ok);
    if (!ok) { SDL_Log("gamma: the pass is off (the shader's link failed)"); return false; }
    gl::UseProgram(p.prog);
    gl::Uniform1i(gl::GetUniformLocation(p.prog, "uSrc"), 0);
    p.uInv = gl::GetUniformLocation(p.prog, "uInv");
    p.uSrgb = gl::GetUniformLocation(p.prog, "uSrgb");
    gl::UseProgram(0);
    gl::GenVertexArrays(1, &p.vao);
    gl::GenTextures(1, &p.tex);
    return true;
}

} // namespace

int gamma100() { return gGamma; }

void setGamma100(int g) { gGamma = std::clamp(g, kGammaMin, kGammaMax); }

int blackLevel() { return gBlack; }

void setBlackLevel(int b) { gBlack = std::clamp(b, kBlackMin, kBlackMax); }

int gammaParse(const char* s)
{
    if (!s || !*s) return -1;
    char* end = nullptr;
    const double v = std::strtod(s, &end);
    if (end == s || !std::isfinite(v)) return -1;
    const double h = v > 10.0 ? v : v * 100.0;   // (140: hundredths)
    const int g = int(std::lround(h / kGammaStep)) * kGammaStep;
    return std::clamp(g, kGammaMin, kGammaMax);
}

const char* gammaText(int g, char (&buf)[8])
{
    std::snprintf(buf, sizeof buf, "%d.%02d", g / 100, g % 100);
    return buf;
}

void gammaApply(unsigned fbo, int w, int h)
{
    if (!gammaOn() || w <= 0 || h <= 0) return;
    Pass& p = gPass;
    if (!p.tried) { p.tried = true; p.ok = build(p); }
    if (!p.ok) return;
    gl::BindFramebuffer(gl::FRAMEBUFFER, fbo);
    // a VR eye's image may be sRGB: the copy the same encoding (OpenGL ES copies only between equal encodings)
    gl::GLint enc = 0;
    if (fbo != 0 && p.attach) p.attach(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, kColorEncoding, &enc);
    const bool srgb = enc == gl::GLint(kSrgb);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, p.tex);
    if (w != p.w || h != p.h || srgb != p.texSrgb) {
        p.w = w, p.h = h, p.texSrgb = srgb;
        gl::texTag(srgb ? "gamma copy srgb" : "gamma copy");   // (questui: the upload log)
        gl::TexImage2D(gl::TEXTURE_2D, 0, gl::GLint(srgb ? kSRGB8 : kRGB8), w, h, 0, gl::RGB, gl::UNSIGNED_BYTE, nullptr);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    }
    p.copy(gl::TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
    gl::Viewport(0, 0, w, h);
    gl::Disable(gl::DEPTH_TEST);
    gl::Disable(gl::BLEND);
    gl::Disable(gl::CULL_FACE);
    gl::Disable(gl::SCISSOR_TEST);
    gl::UseProgram(p.prog);
    gl::Uniform1f(p.uInv, 100.0f / float(gGamma));
    gl::Uniform1i(p.uSrgb, srgb ? 1 : 0);
    gl::BindVertexArray(p.vao);
    gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
    gl::BindVertexArray(0);
    gl::UseProgram(0);
    gl::BindTexture(gl::TEXTURE_2D, 0);
}

void gammaImage(std::vector<uint8_t>& px, int channels)
{
    if (!gammaOn() || channels < 3) return;
    uint8_t lut[256];
    const double inv = 100.0 / double(gGamma);
    for (int i = 0; i < 256; ++i)
        lut[i] = uint8_t(std::lround(255.0 * std::pow(double(i) / 255.0, inv)));
    for (size_t i = 0; i + size_t(channels) <= px.size(); i += size_t(channels))
        for (int c = 0; c < 3; ++c) px[i + size_t(c)] = lut[px[i + size_t(c)]];
}

} // namespace hotd
