#include "render/overlay.h"

#include <SDL3/SDL.h>

#include "game/lang.h"

#include <algorithm>
#include <cmath>

namespace hotd {

namespace {

const char* kVs = R"(#version 330 core
uniform vec4 uRect;   // x, y, w, h in normalized device coordinates (y up)
out vec2 vUV;
void main() {
    vec2 c = vec2(gl_VertexID & 1, gl_VertexID >> 1);   // 0,0 1,0 0,1 1,1 (triangle strip)
    vUV = vec2(c.x, 1.0 - c.y);
    gl_Position = vec4(uRect.xy + c * uRect.zw, 0.0, 1.0);
}
)";

const char* kFs = R"(#version 330 core
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uTex;
void main() { oColor = texture(uTex, vUV); }
)";

gl::GLuint compile(gl::GLenum type, const char* src, std::string& err)
{
    gl::GLuint s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &src, nullptr);
    gl::CompileShader(s);
    gl::shaderLog(s, "overlay");
    gl::GLint ok = 0;
    gl::GetShaderiv(s, gl::COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl::GetShaderInfoLog(s, sizeof log, nullptr, log);
        err = std::string("overlay shader: ") + log;
        return 0;
    }
    return s;
}

} // namespace

bool Overlay::init(std::string& err)
{
    gl::GLuint vs = compile(gl::VERTEX_SHADER, kVs, err), fs = vs ? compile(gl::FRAGMENT_SHADER, kFs, err) : 0;
    if (!vs || !fs) return false;
    program_ = gl::CreateProgram();
    gl::AttachShader(program_, vs);
    gl::AttachShader(program_, fs);
    gl::LinkProgram(program_);
    gl::programLog(program_, "overlay");
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);
    gl::GLint ok = 0;
    gl::GetProgramiv(program_, gl::LINK_STATUS, &ok);
    if (!ok) { err = "overlay shader link failed"; return false; }
    uRect_ = gl::GetUniformLocation(program_, "uRect");
    gl::UseProgram(program_);
    gl::Uniform1i(gl::GetUniformLocation(program_, "uTex"), 0);
    gl::GenVertexArrays(1, &vao_);
    gl::GenTextures(1, &texture_);
    return true;
}

void Overlay::shutdown()
{
    if (texture_) gl::DeleteTextures(1, &texture_);
    if (vao_) gl::DeleteVertexArrays(1, &vao_);
    texture_ = vao_ = 0;
}

void Overlay::setText(const std::vector<std::string>& linesIn, int scale)
{
    if (linesIn == lines_ && scale == scale_ && texW_ && langGen_ == lang::gen()) return;
    lines_ = linesIn;
    langGen_ = lang::gen();
    scale_ = std::max(1, scale);
    hi_ = false;
    // lang: the lines in the language; drawn in its font when one has a character the 8 x 8 font lacks
    std::vector<std::string> shown = linesIn;
    if (translate && lang::on())
        for (std::string& l : shown) l = lang::tr(l);
    const render::TtfText* tf = nullptr;
    for (const std::string& l : shown)
        if (!tf && !lang::plain(l)) tf = lang::fontFor(l);
    if (tf) {
        setTextTtf(shown, *tf);
        return;
    }
    const std::vector<std::string>& lines = shown;
    size_t cols = 0;
    for (const std::string& l : lines) cols = std::max(cols, l.size());
    const int ch = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE, pad = 6, gap = 3;
    texW_ = int(cols) * ch + 2 * pad;
    texH_ = int(lines.size()) * (ch + gap) - gap + 2 * pad;
    SDL_Surface* surf = SDL_CreateSurface(texW_, texH_, SDL_PIXELFORMAT_RGBA32);
    if (!surf) { texW_ = 0; return; }
    SDL_Renderer* r = SDL_CreateSoftwareRenderer(surf);
    if (r) {
        SDL_SetRenderDrawColor(r, 0, 0, 0, Uint8(panel_));   // translucent panel
        SDL_RenderClear(r);
        SDL_SetRenderDrawColor(r, color_[0], color_[1], color_[2], 255);
        for (size_t i = 0; i < lines.size(); ++i)
            SDL_RenderDebugText(r, float(pad), float(pad + int(i) * (ch + gap)), lines[i].c_str());
        SDL_RenderPresent(r);
        SDL_DestroyRenderer(r);
    }
    gl::BindTexture(gl::TEXTURE_2D, texture_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    // copy row by row (the surface pitch can be larger than width * 4)
    std::vector<uint8_t> px(size_t(texW_) * texH_ * 4);
    for (int y = 0; y < texH_; ++y)
        SDL_memcpy(px.data() + size_t(y) * texW_ * 4, static_cast<uint8_t*>(surf->pixels) + size_t(y) * surf->pitch, size_t(texW_) * 4);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, texW_, texH_, 0, gl::RGBA, gl::UNSIGNED_BYTE, px.data());
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
    SDL_DestroySurface(surf);
}

// lang: the lines in a TrueType font on the 8 x 8 font's grid (one column a character, two for a wide one), pixels
// at the scale (the texture drawn 1:1)
void Overlay::setTextTtf(const std::vector<std::string>& lines, const render::TtfText& tf)
{
    hi_ = true;
    const int S = scale_, cell = 8 * S, pad = 6 * S, gap = 3 * S;
    int cols = 0;
    for (const std::string& l : lines) cols = std::max(cols, lang::columns(l));
    const float em = lang::pxForCap(6.0f * float(S), &tf);
    for (const std::string& l : lines)   // (rtl: a right-to-left line is drawn whole, shaped: its width may pass the grid)
        if (render::hasRtl(l)) cols = std::max(cols, int(std::ceil(tf.width(l, em) / float(cell))));
    texW_ = cols * cell + 2 * pad;
    texH_ = int(lines.size()) * (cell + gap) - gap + 2 * pad;
    if (texW_ <= 0 || texH_ <= 0) { texW_ = 0; return; }
    std::vector<uint8_t> px(size_t(texW_) * size_t(texH_) * 4, 0);
    for (size_t i = 0; i + 3 < px.size(); i += 4) px[i + 3] = uint8_t(panel_);
    for (size_t r = 0; r < lines.size(); ++r) {
        const std::string& l = lines[r];
        const float base = float(pad + int(r) * (cell + gap)) + 7.0f * float(S);
        int col = 0;
        if (render::hasRtl(l)) {   // rtl: the line whole (display order, Arabic joined), from the right edge
            tf.draw(l, em, float(texW_ - pad) - tf.width(l, em), base, [&](int X, int Y, float a) {
                if (X < 0 || Y < 0 || X >= texW_ || Y >= texH_) return;
                uint8_t* d = &px[(size_t(Y) * size_t(texW_) + size_t(X)) * 4];
                for (int c = 0; c < 3; ++c) d[c] = uint8_t(float(d[c]) * (1 - a) + float(color_[c]) * a + 0.5f);
                d[3] = uint8_t(std::max(float(d[3]), a * 255.0f));
            });
            continue;
        }
        for (size_t i = 0; i < l.size();) {
            const size_t at = i;
            const uint32_t cp = render::utf8Next(l, i);
            const int w = render::isCjk(cp) ? 2 : 1;
            const float adv = tf.width(l.substr(at, i - at), em);
            const float x = float(pad + col * cell) + (float(w * cell) - adv) / 2.0f;
            tf.draw(l.substr(at, i - at), em, x, base, [&](int X, int Y, float a) {
                if (X < 0 || Y < 0 || X >= texW_ || Y >= texH_) return;
                uint8_t* d = &px[(size_t(Y) * size_t(texW_) + size_t(X)) * 4];
                for (int c = 0; c < 3; ++c) d[c] = uint8_t(float(d[c]) * (1 - a) + float(color_[c]) * a + 0.5f);
                d[3] = uint8_t(std::max(float(d[3]), a * 255.0f));
            });
            col += w;
        }
    }
    gl::BindTexture(gl::TEXTURE_2D, texture_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, texW_, texH_, 0, gl::RGBA, gl::UNSIGNED_BYTE, px.data());
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
}

void Overlay::setImage(const std::vector<uint8_t>& rgba, int w, int h, bool smooth)
{
    lines_.clear();   // (the next setText builds its texture again)
    if (w <= 0 || h <= 0 || rgba.size() < size_t(w) * size_t(h) * 4) { texW_ = 0; return; }
    hi_ = true;
    scale_ = 1;
    texW_ = w;
    texH_ = h;
    gl::BindTexture(gl::TEXTURE_2D, texture_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, rgba.data());
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, smooth ? gl::LINEAR : gl::NEAREST);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, smooth ? gl::LINEAR : gl::NEAREST);
}

void Overlay::drawIn(int w, int h, float x, float y, float pw, float ph)
{
    if (!texW_ || w <= 0 || h <= 0) return;
    gl::Viewport(0, 0, w, h);
    gl::Disable(gl::DEPTH_TEST);
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::UseProgram(program_);
    gl::Uniform4f(uRect_, -1.0f + 2.0f * x / float(w), 1.0f - 2.0f * (y + ph) / float(h), 2.0f * pw / float(w), 2.0f * ph / float(h));
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, texture_);
    gl::BindVertexArray(vao_);
    gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
    gl::BindVertexArray(0);
    gl::Disable(gl::BLEND);
    gl::Enable(gl::DEPTH_TEST);
}

void Overlay::draw(int w, int h)
{
    if (!texW_ || w <= 0 || h <= 0) return;
    const float margin = 8.0f;
    // top-left corner, in normalized device coordinates
    drawRect(w, h, -1.0f + 2.0f * margin / float(w), 1.0f - 2.0f * margin / float(h));
}

void Overlay::drawAt(int w, int h, float x, float y)
{
    if (!texW_ || w <= 0 || h <= 0) return;
    const float pw = float(texW_) * outScale(), ph = float(texH_) * outScale();
    drawRect(w, h, -1.0f + 2.0f * (x - pw / 2.0f) / float(w), 1.0f - 2.0f * (y - ph / 2.0f) / float(h));
}

void Overlay::drawAnchored(int w, int h, float x, float y, float ax, float ay)
{
    if (!texW_ || w <= 0 || h <= 0) return;
    const float pw = float(texW_) * outScale(), ph = float(texH_) * outScale();
    drawRect(w, h, -1.0f + 2.0f * (x - pw * ax) / float(w), 1.0f - 2.0f * (y - ph * ay) / float(h));
}

void Overlay::drawRect(int w, int h, float x0, float y1)
{
    const float pw = float(texW_) * outScale(), ph = float(texH_) * outScale();
    const float sw = 2.0f * pw / float(w), sh = 2.0f * ph / float(h);
    gl::Viewport(0, 0, w, h);
    gl::Disable(gl::DEPTH_TEST);
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::UseProgram(program_);
    gl::Uniform4f(uRect_, x0, y1 - sh, sw, sh);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, texture_);
    gl::BindVertexArray(vao_);
    gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
    gl::BindVertexArray(0);
    gl::Disable(gl::BLEND);
    gl::Enable(gl::DEPTH_TEST);
}

} // namespace hotd

namespace hotd {

namespace {
// the ring (skipfix, user 2026-10-27: "a simple red ring"): a thin red arc filling clockwise from the top over a
// faint red track, nothing else; anti-aliased by fwidth
const char* kRingFs = R"(#version 330 core
in vec2 vUV;
out vec4 oColor;
uniform float uFill;
uniform vec4 uColor;
void main() {
    vec2 p = vUV * 2.0 - 1.0;   // y down
    float r = length(p), aa = fwidth(r);
    float ring = smoothstep(0.70 - aa, 0.70 + aa, r) * (1.0 - smoothstep(0.94 - aa, 0.94 + aa, r));
    float a = atan(p.x, -p.y);
    float f = (a < 0.0 ? a + 6.2831853 : a) / 6.2831853;
    oColor = vec4(uColor.rgb, ring * (f <= uFill ? 0.95 : 0.22) * uColor.a);
}
)";
} // namespace

bool SkipRing::init(std::string& err)
{
    gl::GLuint vs = compile(gl::VERTEX_SHADER, kVs, err), fs = vs ? compile(gl::FRAGMENT_SHADER, kRingFs, err) : 0;
    if (!vs || !fs) return false;
    program_ = gl::CreateProgram();
    gl::AttachShader(program_, vs);
    gl::AttachShader(program_, fs);
    gl::LinkProgram(program_);
    gl::programLog(program_, "overlay");
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);
    gl::GLint ok = 0;
    gl::GetProgramiv(program_, gl::LINK_STATUS, &ok);
    if (!ok) { err = "skip ring shader link failed"; return false; }
    uRect_ = gl::GetUniformLocation(program_, "uRect");
    uFill_ = gl::GetUniformLocation(program_, "uFill");
    uColor_ = gl::GetUniformLocation(program_, "uColor");
    gl::GenVertexArrays(1, &vao_);
    return true;
}

void SkipRing::shutdown()
{
    if (vao_) gl::DeleteVertexArrays(1, &vao_);
    vao_ = 0;
}

void SkipRing::draw(int w, int h, float x, float y, float size, float fill)
{
    if (!program_ || w <= 0 || h <= 0) return;
    gl::Viewport(0, 0, w, h);
    gl::Disable(gl::DEPTH_TEST);
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::UseProgram(program_);
    gl::Uniform4f(uRect_, 2.0f * (x - size * 0.5f) / float(w) - 1.0f, 1.0f - 2.0f * (y + size * 0.5f) / float(h),
                  2.0f * size / float(w), 2.0f * size / float(h));
    gl::Uniform1f(uFill_, fill);
    gl::Uniform4f(uColor_, color_[0], color_[1], color_[2], color_[3]);
    gl::BindVertexArray(vao_);
    gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
    gl::BindVertexArray(0);
    gl::Disable(gl::BLEND);
    gl::Enable(gl::DEPTH_TEST);
}

} // namespace hotd
