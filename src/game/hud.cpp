// The HUD (hud.h).
#include "game/hud.h"

#include "assets/sprites_arcade.h"
#include "assets/sprites_pc.h"
#include "game/cosmods.h"
#include "game/lang.h"
#include "game/player_colours.h"
#include "game/sights.h"
#include "util/png_read.h"
#include "render/gl.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace hotd::game {

using G = A::G;
using port::a::Player;

namespace {

const char* kVs = R"(#version 330 core
uniform vec4 uRect;   // x, y, w, h in normalized device coordinates (y up)
out vec2 vUV;
void main() {
    vec2 c = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    vUV = vec2(c.x, 1.0 - c.y);
    gl_Position = vec4(uRect.xy + c * uRect.zw, 0.0, 1.0);
}
)";

const char* kFs = R"(#version 330 core
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uTex;
uniform vec4 uTint;   // rainbow2: a > 0.5: the brightest channel times rgb (the sights in the rainbow)
void main() {
    oColor = texture(uTex, vUV);
    if (uTint.a > 0.5) oColor.rgb = max(oColor.r, max(oColor.g, oColor.b)) * uTint.rgb;
}
)";

gl::GLuint compile(gl::GLenum type, const char* src, std::string& err)
{
    gl::GLuint s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &src, nullptr);
    gl::CompileShader(s);
    gl::shaderLog(s, "hud");
    gl::GLint ok = 0;
    gl::GetShaderiv(s, gl::COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl::GetShaderInfoLog(s, sizeof log, nullptr, log);
        err = std::string("hud shader: ") + log;
        return 0;
    }
    return s;
}

constexpr uint32_t kTileRam = 0x1000000, kCharRam = 0x1080000, kPalShadow = 0x515CA0;
#include "assets/sprite_map_pc.inc"

constexpr uint32_t kLifeBar = 0x543530;   // sprites.cpp: shown, +0xA width

// the sprite's first cell is at (x, y) of the layer with priority (sprite_place / sprite_draw_anim with a = 1, b = 0)
bool placedAt(const Runtime& rt, uint32_t layer, int x, int y, uint32_t sprite)
{
    port::Mem& mem = rt.mem;
    if (!mem.mapped(kTileRam))
        return false;
    const int32_t slot = int32_t(mem.u32(0x5123F0 + uint32_t(int32_t(mem.at<int16_t>(0xA9B60 + sprite * 2))) * 16));
    if (slot < 0)
        return false;
    const uint32_t lay = mem.u32(0xA6590 + sprite * 12 + 8);
    const uint16_t want = uint16_t(int16_t(slot) * 0x80 - 0x8000 + mem.at<uint16_t>(lay + 4));
    return mem.at<uint16_t>(layer + uint32_t(y * 0x80 + x * 2)) == want;
}

} // namespace

bool Hud::init(std::string& err)
{
    for (int p = 0; p < 2; ++p) {
        if (!player_[p].init(err) || !cross_[p].init(err))
            return false;
        // player 1 red, player 2 blue (the arcade's gun colours)
        player_[p].setStyle(120, p == 0 ? 255 : 120, p == 0 ? 90 : 170, p == 0 ? 80 : 255);
        cross_[p].setStyle(0, p == 0 ? 255 : 90, p == 0 ? 60 : 170, p == 0 ? 60 : 255);
        cross_[p].setText({"  |  ", "--+--", "  |  "}, 2);
    }
    for (int p = 0; p < 4; ++p) {
        if (!cpuTag_[p].init(err))
            return false;
        cpuTag_[p].setStyle(0, kPlayerRgb[p][0], kPlayerRgb[p][1], kPlayerRgb[p][2]);
        cpuTag_[p].setText({"CPU"}, 1);
    }
    for (int p = 0; p < 2; ++p) {
        if (!seatText_[p].init(err))
            return false;
        seatText_[p].setStyle(0, kPlayerRgb[p + 2][0], kPlayerRgb[p + 2][1], kPlayerRgb[p + 2][2]);
    }
    for (int p = 2; p < 4; ++p) {   // the 4P mode's players 3 / 4 (green / yellow, player_colours.h)
        if (!cross_[p].init(err))
            return false;
        cross_[p].setStyle(0, kPlayerRgb[p][0], kPlayerRgb[p][1], kPlayerRgb[p][2]);
        cross_[p].setText({"  |  ", "--+--", "  |  "}, 2);
    }
    if (!centre_.init(err) || !debug_.init(err) || !bar_.init(err) || !boss_.init(err) || !title_.init(err) ||
        !clear_.init(err) || !label_.init(err) || !pause_.init(err) || !langPc_[0].init(err) || !langPc_[1].init(err) ||
        !survival_.init(err) || !survivalBanner_.init(err))
        return false;
    for (Overlay& o : survivalSign_) {
        if (!o.init(err))
            return false;
        o.setStyle(170, 130, 130, 130);
    }
    pause_.setStyle(150, 255, 255, 255);
    for (Overlay& o : langPc_) o.setStyle(0, 255, 255, 255);
    centre_.setStyle(0, 255, 230, 120);
    bar_.setStyle(255, 0, 0, 0);
    boss_.setStyle(150, 255, 200, 60);
    title_.setStyle(0, 255, 60, 40);
    clear_.setStyle(170, 255, 240, 200);
    survival_.setStyle(120, 255, 220, 120);
    survivalBanner_.setStyle(0, 255, 70, 50);
    label_.setStyle(120, 200, 200, 200);
    label_.setText({"PC HUD sprites (SPR.RBH): placeholder"}, 1);
    // the arcade 2D layer's texture
    gl::GLuint vs = compile(gl::VERTEX_SHADER, kVs, err), fs = vs ? compile(gl::FRAGMENT_SHADER, kFs, err) : 0;
    if (!vs || !fs)
        return false;
    program_ = gl::CreateProgram();
    gl::AttachShader(program_, vs);
    gl::AttachShader(program_, fs);
    gl::LinkProgram(program_);
    gl::programLog(program_, "hud");
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);
    gl::GLint ok = 0;
    gl::GetProgramiv(program_, gl::LINK_STATUS, &ok);
    if (!ok) {
        err = "hud shader link failed";
        return false;
    }
    uRect_ = gl::GetUniformLocation(program_, "uRect");
    uTint_ = gl::GetUniformLocation(program_, "uTint");
    gl::UseProgram(program_);
    gl::Uniform1i(gl::GetUniformLocation(program_, "uTex"), 0);
    gl::GenVertexArrays(1, &vao_);
    gl::GenTextures(1, &texture_);
    return true;
}

// the arcade's 2D layer (priority cells: over the 3D picture) where the 496 x 384 screen lies in the window: the
// view fits the screen's height (GameView::fromArcade), centred
void Hud::drawArcade2D(const Runtime& rt, int ww, int wh)
{
    port::Mem& mem = rt.mem;
    if (!mem.mapped(kTileRam) || !mem.mapped(kCharRam))
        return;
    drawArcadePass(rt, ww, wh, true);
}

// display2: widescreen: not on the chapter-clear results (Runtime::clear.active: the results layout is the 4:3 picture); three passes (centred; the left corner's cells moved to the window's left edge; the right's
// to its right edge, the same distance from the edge as on the arcade's 496-pixel screen). UNVERIFIED (teaser)
bool Hud::drawArcadePass(const Runtime& rt, int ww, int wh, bool high, bool composed)
{
    port::Mem& mem = rt.mem;
    const int W = assets::Arcade2D::kWidth, H = assets::Arcade2D::kHeight;
    const bool sides = anchorEdges && !rt.clear.active && std::any_of(rt.cellSide.begin(), rt.cellSide.end(), [](uint8_t s) { return s != 0; });
    const uint8_t* tiles = !(clean || noText || subHide_) ? mem.ptr(kTileRam) : composed ? tilesCopy_.data() : filteredTiles(rt);   // (composed: drawUnder filtered it)
    if (!composed)
        tiles = withoutP2Notice(rt, tiles);
    if (!sides) {
        if (!composed && !assets::Arcade2D::compose(tiles, mem.ptr(kCharRam), mem.ptr(kPalShadow), high, rgba_))
            return false;
        std::vector<uint8_t>* pic[1] = {&rgba_};
        translucent2D(rt, tiles, high, pic, 1);
        rainbow2D(rt, tiles, high, pic, 1);
        upload(rgba_, W, H, ww, wh);
        return true;
    }
    const float x0 = leftEdge(W, H, ww, wh);
    bool drawn[3] = {};   // perf: the three sides' pictures in one pass (were three)
    assets::Arcade2D::composeSides(tiles, mem.ptr(kCharRam), mem.ptr(kPalShadow), high, rt.cellSide.data(), arcadeSides_,
                                   drawn);
    std::vector<uint8_t>* pics[3] = {&arcadeSides_[0], &arcadeSides_[1], &arcadeSides_[2]};
    translucent2D(rt, tiles, high, pics, 3);
    rainbow2D(rt, tiles, high, pics, 3);
    bool any = false;
    for (int s = 0; s < 3; ++s)
        if (drawn[s]) {
            upload(arcadeSides_[s], W, H, ww, wh, s == 0 ? 0.0f : s == 1 ? -x0 : x0);
            any = true;
        }
    return any;
}

// rainbow2 (Rainbow Blood, user 2026-11-03: the rainbow on all the blood, and the HUD option): the arcade layer's hit
// marks (Runtime::cellHud 2: sprites 0x28..0x2D, the stippled blood splashes over the play area when a player is hit)
// by their red share, and with HUD & Crosshair: HUD / Both the player HUD (cellHud 1) by its brightest channel, in the
// rainbow. Those cells are composed alone (hudMask_); a picture's pixel that equals the mask's is theirs (not covered
// by another layer's cell). Each Splash: the marks' hue moves across the screen (as the 3D splashes' with their
// place); the HUD's halves take players 1 / 2's phases (as their sights)
void Hud::rainbow2D(const Runtime& rt, const uint8_t* tiles, bool high, std::vector<uint8_t>* const* pics, int n)
{
    const RainbowBloodSettings& s = rainbowBloodSettings();
    if (!s.on || !tiles || rt.greyPalette.on)   // (the grey look: grey, as the 3D blood)
        return;
    bool marks = false, hud = false;
    for (uint8_t k : rt.cellHud)
        marks = marks || k == 2, hud = hud || (k == 1 && s.hud >= 2);
    const int W = assets::Arcade2D::kWidth, H = assets::Arcade2D::kHeight;
    port::Mem& mem = rt.mem;
    // the score lines (Score Display: hud_lives_draw's SCORE:%05d, layer 0 text at row 0x24 of each player's corner,
    // as drawPc2D reads them) count as the HUD too
    cellsHud_ = rt.cellHud;
    if (s.hud >= 2 && mem.mapped(kTileRam) && mem.u32(0x51F42C) == 7)
        for (int p = 0; p < 2; ++p) {
            constexpr int kLen = 11, row = 0x24;
            const int nx = p == 0 ? 2 : 0x3D - kLen;
            std::string line;
            for (int i = 0; i < kLen; ++i) {
                const uint16_t v = mem.at<uint16_t>(kTileRam + uint32_t(row * 0x80 + (nx + i) * 2));
                line += (v & 0xFF) >= 0x20 && (v & 0xFF) < 0x7F ? char(v & 0xFF) : ' ';
            }
            if (line.compare(0, 6, "SCORE:") == 0) {
                for (int i = 0; i < kLen; ++i) cellsHud_[size_t(row * 64 + nx + i)] = 1;
                hud = true;
            }
        }
    for (int want = 2; want >= 1; --want) {
        if (!(want == 2 ? marks : hud))
            continue;
        if (!assets::Arcade2D::compose(tiles, mem.ptr(kCharRam), mem.ptr(kPalShadow), high, hudMask_, cellsHud_.data(), want))
            continue;
        // the colours: marks per 8 pixels along x + y (Each Splash; one with Together), the HUD per half
        float col[2][3];
        std::vector<std::array<float, 3>> wave;
        if (want == 2) {
            wave.resize(size_t((W + H) / 8 + 1));
            for (size_t d = 0; d < wave.size(); ++d)
                rainbowColour(rt, float(d * 8) / float(W + H) * 0.5f, wave[d].data());
        } else {
            rainbowColour(rt, 0.0f, col[0], true);
            rainbowColour(rt, 0.5f, col[1], true);
        }
        for (int k = 0; k < n; ++k) {
            std::vector<uint8_t>& px = *pics[k];
            if (px.size() < size_t(W) * H * 4 || hudMask_.size() < size_t(W) * H * 4)
                continue;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const size_t o = (size_t(y) * W + size_t(x)) * 4;
                    uint8_t* d = &px[o];
                    const uint8_t* m = &hudMask_[o];
                    if (!m[3] || !d[3] || d[0] != m[0] || d[1] != m[1] || d[2] != m[2])
                        continue;
                    if (want == 2) {
                        const float* c = wave[size_t((x + y) / 8)].data();
                        const int g = std::max(d[1], d[2]), share = d[0] - g;
                        if (share > 0)
                            for (int ch = 0; ch < 3; ++ch)
                                d[ch] = uint8_t(std::clamp(float(ch == 0 ? g : d[ch]) + float(share) * c[ch] + 0.5f, 0.0f, 255.0f));
                    } else {
                        const float* c = col[x < W / 2 ? 0 : 1];
                        const float v = float(std::max({d[0], d[1], d[2]}));
                        for (int ch = 0; ch < 3; ++ch)
                            d[ch] = uint8_t(std::clamp(v * c[ch] + 0.5f, 0.0f, 255.0f));
                    }
                }
        }
    }
}

// bugs1 (#35, user report: "the PC port's D3D mode had true transparent scar / hit marks; can those replace the dithered
// look?"): Options > Video "Hit Marks: See-through". The arcade's marks (sprites 0x28..0x2D, Runtime::cellHud 2) are
// stippled: every other pixel pen 0, the 3D showing through (half the pixels). Here the same picture is made whole and
// half see-through: each mark pixel at alpha 1/2, each stipple hole (a transparent pixel between two mark pixels, across
// or up and down) their average at 1/2. The marks' pixels are found as rainbow2D finds them (their cells composed
// alone; a picture's pixel equal to the mask's). The PC Direct3D's own blend level was not read: 1/2 (the stipple's
// coverage). UNVERIFIED against the PC's D3D
void Hud::translucent2D(const Runtime& rt, const uint8_t* tiles, bool high, std::vector<uint8_t>* const* pics, int n)
{
    if (!translucentMarks || !tiles || std::none_of(rt.cellHud.begin(), rt.cellHud.end(), [](uint8_t k) { return k == 2; }))
        return;
    const int W = assets::Arcade2D::kWidth, H = assets::Arcade2D::kHeight;
    port::Mem& mem = rt.mem;
    if (!assets::Arcade2D::compose(tiles, mem.ptr(kCharRam), mem.ptr(kPalShadow), high, markMask_, rt.cellHud.data(), 2) ||
        markMask_.size() < size_t(W) * H * 4)
        return;
    const uint8_t* m = markMask_.data();
    auto mark = [&](int x, int y) { return x >= 0 && y >= 0 && x < W && y < H && m[(size_t(y) * W + x) * 4 + 3] != 0; };
    for (int k = 0; k < n; ++k) {
        std::vector<uint8_t>& px = *pics[k];
        if (px.size() < size_t(W) * H * 4)
            continue;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const size_t o = (size_t(y) * W + size_t(x)) * 4;
                uint8_t* d = &px[o];
                if (m[o + 3]) {
                    if (d[3] && d[0] == m[o] && d[1] == m[o + 1] && d[2] == m[o + 2])
                        d[3] = 128;
                    continue;
                }
                if (d[3])
                    continue;   // (another cell's picture there)
                int a = -1, b = -1;
                if (mark(x - 1, y) && mark(x + 1, y))
                    a = int(o) - 4, b = int(o) + 4;
                else if (mark(x, y - 1) && mark(x, y + 1))
                    a = int(o) - W * 4, b = int(o) + W * 4;
                if (a < 0)
                    continue;
                // (the neighbours as the picture has them: a mark pixel another layer covers is not averaged in)
                const uint8_t* pa = &px[size_t(a)];
                const uint8_t* pb = &px[size_t(b)];
                if (!pa[3] || !pb[3])
                    continue;
                for (int c = 0; c < 3; ++c)
                    d[c] = uint8_t((int(pa[c]) + int(pb[c]) + 1) / 2);
                d[3] = 128;
            }
    }
}

// trailer (ours, draw-only): a copy of tile RAM for the arcade's priority pass without the HUD's cells (clean) and / or
// the subtitles' text cells (noText: layer 0's Runtime::cellText). The black cover (sprite 0xA1 on layer 2, which the
// fades show) stays: layer 2's cells equal to its top-left cell while the cover is placed there
const uint8_t* Hud::filteredTiles(const Runtime& rt)
{
    const uint8_t* src = rt.mem.ptr(kTileRam);
    tilesCopy_.assign(src, src + 0x10000);
    const auto at = [&](int l, int x, int y) {
        return reinterpret_cast<uint16_t*>(&tilesCopy_[size_t(l) * 0x2000 + size_t(y) * 0x80 + size_t(x) * 2]);
    };
    const bool cover = rt.pcCells[size_t(2 * Runtime::kPcRows) * 64] == (0xA1u | 0x10000u);
    const uint16_t coverCell = cover ? *at(2, 0, 0) : uint16_t(0);
    for (int l = 0; l < 4; ++l)
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                uint16_t* c = at(l, x, y);
                const bool text = l == 0 && rt.cellText[size_t(y) * 64 + size_t(x)];
                if (text ? noText || subHide_ : clean && !(cover && l == 2 && *c == coverCell))
                    *c = 0;
            }
    return tilesCopy_.data();
}

// the cover placed now: sprite 0xA1 (black) or 0x8D (white: texture_area_load's other cover, the prologue's and the
// attract's flash cut, sweep 2026-10-28) in layer 2's top-left cell and its cells still on the screen (all the same
// tile; Runtime::pcCells keeps the sprite after sprites_cover_lift clears the cells). 0: none, else the sprite
uint32_t Hud::coverShown(const Runtime& rt)
{
    const uint32_t spr = rt.pcCells[size_t(2 * Runtime::kPcRows) * 64];
    if ((spr != (0xA1u | 0x10000u) && spr != (0x8Du | 0x10000u)) || !rt.mem.mapped(kTileRam))
        return 0;
    const auto at = [&](int x, int y) { return rt.mem.at<uint16_t>(kTileRam + 0x4000 + uint32_t(y * 0x80 + x * 2)); };
    const uint16_t c = at(0, 0);
    return c != 0 && at(31, 24) == c && at(61, 47) == c ? spr & 0xFFFFu : 0;
}

bool Hud::p2Notice(const Runtime& rt, uint32_t& sprite, int& x, int& y)
{
    port::Mem& mem = rt.mem;
    const uint32_t r = 0x51F480 + 8;   // (its +4 is players_in_game_b, the art's row as the original indexes it, not the player)
    if (!rt.coop4_on() || mem.at<uint8_t>(r) == 0)
        return false;
    const int kind = mem.at<int8_t>(r + 3), who = int32_t(mem.u32(r + 4));
    if (who < 0 || who >= 4 || kind < 0 || kind >= 16)
        return false;
    const uint32_t pos = 0x94368 + uint32_t(kind * 16 + who * 8);
    sprite = mem.u32(0xCA5FC + uint32_t(kind * 8 + who * 4));
    if (sprite == 0 || sprite >= 0x400 || !mem.mapped(pos))
        return false;
    x = mem.at<int8_t>(r + 2) + int32_t(mem.u32(pos));
    y = int32_t(mem.u32(pos + 4));
    return true;
}

const uint8_t* Hud::withoutP2Notice(const Runtime& rt, const uint8_t* tiles)
{
    uint32_t spr = 0;
    int x0 = 0, y0 = 0;
    if (!p2Notice(rt, spr, x0, y0))
        return tiles;
    port::Mem& mem = rt.mem;
    const uint32_t lay = mem.u32(0xA6590 + spr * 12 + 8);   // its layout: u16 w, u16 h, w x h cells
    if (lay == 0 || !mem.mapped(lay))
        return tiles;
    tilesNoP2_.assign(tiles, tiles + 0x10000);
    const int w = mem.at<uint16_t>(lay), h = mem.at<uint16_t>(lay + 2);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const int x = x0 + i, y = y0 + j;
            if (x < 0 || x >= 64 || y < 0 || y >= 64 || mem.at<uint16_t>(lay + 4 + uint32_t(j * w + i) * 2) == 0)
                continue;
            std::memset(&tilesNoP2_[size_t(2) * 0x2000 + size_t(y) * 0x80 + size_t(x) * 2], 0, 2);   // (layer 2, 0x1004000)
        }
    return tilesNoP2_.data();
}

bool Hud::drawUnder(const Runtime& rt, int ww, int wh, const float clear[3])
{
    port::Mem& mem = rt.mem;
    // pcsparks (2026-10-16): the PC's game-over route map (mode 5 sub-modes 2 / 3): the walker (game_over_walker_task,
    // THOTD.EXE 0x4B1448 -> body_draw) shows over the map's opaque picture (real PC pcpolish2\gameover t03351-t03465:
    // the figure on the trail at FRONTYARD); ours drew the map over it, hiding it. The PC 2D layer goes under the 3D
    // there. UNVERIFIED (teaser): the PC's draw order for that task group not read
    if (rt.pcModels && pcSprites_.loaded() && mem.at<int16_t>(G::game_mode) == 5 &&
        (mem.u32(G::game_submode) == 2 || mem.u32(G::game_submode) == 3)) {
        gl::Viewport(0, 0, ww, wh);
        gl::ClearColor(clear[0], clear[1], clear[2], 1.0f);
        gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
        drawPc2D(rt, ww, wh);
        pcUnderFrame_ = (long long)rt.frameCount;
        return true;
    }
    if (rt.pcModels || !mem.mapped(kTileRam) || !mem.mapped(kCharRam))
        return false;   // PC: Runtime::pcCells carries no priority (all over the 3D)
    subHide_ = pauseSelection < 0 && !noText && lang::active() && !subLines(rt).empty();   // (lang: as draw() will)
    const bool composed = assets::Arcade2D::compose(withoutP2Notice(rt, clean || noText || subHide_ ? filteredTiles(rt) : mem.ptr(kTileRam)),
                                                    mem.ptr(kCharRam), mem.ptr(kPalShadow), false, rgba_);
    subHide_ = false;
    if (!composed)
        return false;
    gl::Viewport(0, 0, ww, wh);
    gl::ClearColor(clear[0], clear[1], clear[2], 1.0f);
    gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
    drawArcadePass(rt, ww, wh, false, true);   // (perf: the picture just composed, not composed again)
    return true;
}

// one RGBA picture of the screen over the 3D view: the view fits the screen's height (GameView::fromArcade), centred
void Hud::upload(const std::vector<uint8_t>& rgba, int W, int H, int ww, int wh, float dx, bool fit)
{
    gl::BindTexture(gl::TEXTURE_2D, texture_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, W, H, 0, gl::RGBA, gl::UNSIGNED_BYTE, rgba.data());
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    const float s = fit ? std::min(float(ww) / float(W), float(wh) / float(H)) : float(wh) / float(H);
    const float pw = float(W) * s, x0 = float(ww) / 2.0f - pw / 2.0f + dx;
    const float ph = float(H) * s, y0 = (float(wh) - ph) / 2.0f;   // (fit: letterboxed; else 0)
    gl::Viewport(0, 0, ww, wh);
    gl::Disable(gl::DEPTH_TEST);
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::UseProgram(program_);
    gl::Uniform4f(uRect_, -1.0f + 2.0f * x0 / float(ww), -1.0f + 2.0f * y0 / float(wh), 2.0f * pw / float(ww), 2.0f * ph / float(wh));
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, texture_);
    gl::BindVertexArray(vao_);
    gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
    gl::BindVertexArray(0);
    gl::Disable(gl::BLEND);
    gl::Enable(gl::DEPTH_TEST);
}

// a full-width band of window rows [y0, y1) (y down) in one colour
void Hud::fillRect(const uint8_t rgb[3], int ww, int wh, int y0, int y1)
{
    if (y1 <= y0 || wh <= 0)
        return;
    const uint8_t px[4] = {rgb[0], rgb[1], rgb[2], 255};
    gl::BindTexture(gl::TEXTURE_2D, texture_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, 1, 1, 0, gl::RGBA, gl::UNSIGNED_BYTE, px);
    gl::Viewport(0, 0, ww, wh);
    gl::Disable(gl::DEPTH_TEST);
    gl::UseProgram(program_);
    const float h = 2.0f / float(wh);
    gl::Uniform4f(uRect_, -1.0f, 1.0f - float(y1) * h, 2.0f, float(y1 - y0) * h);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, texture_);
    gl::BindVertexArray(vao_);
    gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
    gl::BindVertexArray(0);
    gl::Enable(gl::DEPTH_TEST);
}

// the PC's screen fade (Runtime::PcFade) over the whole window, the 3D and the 2D: the picture times the level (below
// 1: black over it with alpha 1 - level; above 1: the picture added to itself times level - 1, clamped, as the PC's
// colour scale saturates)
void Hud::pcFadeDraw(float level, int ww, int wh)
{
    if (level > 0.998f && level < 1.002f)
        return;
    const bool up = level > 1.0f;
    const float v = up ? std::min(level - 1.0f, 1.0f) : 1.0f - std::max(level, 0.0f);
    const uint8_t px[4] = {uint8_t(up ? v * 255.0f + 0.5f : 0), uint8_t(up ? v * 255.0f + 0.5f : 0),
                           uint8_t(up ? v * 255.0f + 0.5f : 0), uint8_t(up ? 255 : v * 255.0f + 0.5f)};
    gl::BindTexture(gl::TEXTURE_2D, texture_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, 1, 1, 0, gl::RGBA, gl::UNSIGNED_BYTE, px);
    gl::Viewport(0, 0, ww, wh);
    gl::Disable(gl::DEPTH_TEST);
    gl::Enable(gl::BLEND);
    if (up)
        gl::BlendFunc(gl::DST_COLOR, gl::ONE);   // dst + dst x (level - 1)
    else
        gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::UseProgram(program_);
    gl::Uniform4f(uRect_, -1.0f, -1.0f, 2.0f, 2.0f);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, texture_);
    gl::BindVertexArray(vao_);
    gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
    gl::BindVertexArray(0);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::Disable(gl::BLEND);
    gl::Enable(gl::DEPTH_TEST);
}

// the PC models: each arcade sprite the original's routines placed (Runtime::pcCells, layers 3 .. 0; the window
// layers 1 / 3 only while their mask is on) as the PC sprite showing the same picture, at its cell (8 units of the
// PC's 512 x 384 grid = 10 pixels of its 640 x 480 screen); the subtitles in the PC's font (sprite = character - 0x1E,
// centred where the arcade centres its line)
void Hud::drawPc2D(const Runtime& rt, int ww, int wh)
{
    pcSprites_.setBrown(rt.greyPalette.on);   // the PC's grey look: the 2D through BROWN.PAL too (Hud::draw resets it)
    const int W = assets::PcSprites::kWidth, H = assets::PcSprites::kHeight;
    rgba_.assign(size_t(W) * H * 4, 0);
    // rainbow2 (Rainbow Blood): every sprite's blood texels (the GAME OVER screen's blood) in the rainbow; the hit marks
    // (Runtime::cellHud 2: the PC's 0x47E..0x483, the same red in every blood set) by their red share, their hue moving
    // across the screen with Each Splash; with HUD & Crosshair: HUD / Both the player HUD by its brightest channel in
    // players 1 / 2's phases (the screen's halves), as the arcade layer (rainbow2D)
    const bool rbOn = rainbowBloodSettings().on && !rt.greyPalette.on, rbHud = rbOn && rainbowBloodSettings().hud >= 2;
    {
        float c[3];
        pcSprites_.setBloodTexelTint(rbOn && rainbowColour(rt, 0.0f, c) ? c : nullptr);
    }
    auto rbTint = [&](int kind, int ax, int ay) {   // (ax, ay: the arcade screen pixel)
        float c[3];
        if (kind == 2 ? rainbowColour(rt, float(ax + ay) / 880.0f * 0.5f, c) : rainbowColour(rt, ax < 248 ? 0.0f : 0.5f, c, true))
            pcSprites_.setTint(kind == 2 ? 1 : 2, c);
    };
    port::Mem& mem = rt.mem;
    bool any = false, sideAny[2] = {};
    struct PcLine { std::string text; int x, y; };   // (lang: the PC font's words as text, see the end)
    std::vector<PcLine> langPc;
    for (auto& b : sideRgba_)
        std::fill(b.begin(), b.end(), uint8_t(0));
    constexpr int kRows = Runtime::kPcRows;
    const int16_t mode = mem.at<int16_t>(G::game_mode);
    const bool inGame = mode == 4 || mode == 5;
    for (int l = 3; l >= 0 && !clean; --l) {   // (Clean View / Trailer Mode: no sprites)
        if ((l & 1) && (!mem.mapped(kTileRam) || mem.u32(l < 2 ? 0x100C000 : 0x100D000) == 0))
            continue;
        // the layer's scroll registers (as Arcade2D::compose: 0x100A000 + 2 x layer x, 0x100A008 + 2 x layer y, y bit
        // 15 = off) in whole cells: the game-over route map scrolls layers 1 / 2 (pcpolish2; elsewhere 0)
        int hs = 0, vs = 0;
        if (mem.mapped(0x100A000)) {
            const uint16_t vr = mem.at<uint16_t>(0x100A008 + 2u * uint32_t(l));
            if (vr & 0x8000)
                continue;
            hs = mem.at<uint16_t>(0x100A000 + 2u * uint32_t(l)) & 0x1FF;
            vs = vr & 0x1FF;
        }
        for (int cy = 0; cy < kRows; ++cy)
            for (int cx = 0; cx < 64; ++cx) {
                const size_t ci = size_t((l * kRows + cy) * 64 + cx);
                const uint32_t v = rt.pcCells[ci];
                if (!v || (v & 0xFFFF) >= 0x276)
                    continue;
                // the cell's place on the screen (arcade pixels; the 512 x 512 map wraps): a sprite starting below
                // the screen's bottom is drawn from above (its top wrapped)
                int sx = (cx * 8 - hs) & 511, sy = (cy * 8 - vs) & 511;
                if (sx >= 496) sx -= 512;
                if (sy >= 384) sy -= 512;
                // the window mask (as Arcade2D::compose, per cell): where a pair's bit is set the window layer (1 / 3)
                // shows, elsewhere layer 0 / 2 (polish: the title card's window layer had "HOLD YOUR FIRE!" over it)
                if (mem.mapped(kTileRam) && sy >= 0 && sx >= 0) {
                    const int mx = sx >> 3;
                    const uint32_t line = (l < 2 ? 0x100C000u : 0x100D000u) + uint32_t(sy) * 8 + uint32_t(mx >> 4) * 2;
                    if ((((mem.at<uint16_t>(line) >> (15 - (mx & 15))) & 1) != 0) != ((l & 1) != 0))
                        continue;
                }
                // the start prompt (PRESS START BUTTON, 0x40) in a game: the PC's hud_start_prompt is empty (its
                // continue screen shows "Continues n" instead, below; real PC capture pcpolish2 t02264)
                if ((v & 0xFFFF) == 0x40 && inGame)
                    continue;
                // the RELOAD hint line ("shoot outside of the screen", table 0xCA700 by region): the PC's own
                // hud_ammo_draw (THOTD.EXE 0x50B7E4) draws RELOAD (0x421) alone; its hint sprite 0x527 is loaded
                // (bank 0x38) but never drawn
                if (inGame && mem.mapped(0xCA700) && (v & 0xFFFF) == mem.u32(0xCA700 + rt.text_region() * 4))
                    continue;
                const int id = kArcadeToPcSprite[v & 0xFFFF];
                if (!id)
                    continue;
                // display2: a corner's sprite goes to its side canvas, moved to the window's edge below
                const int side = anchorEdges && !rt.clear.active ? rt.cellSide[ci] : 0;
                if (side && sideRgba_[side - 1].size() != size_t(W) * H * 4)
                    sideRgba_[side - 1].assign(size_t(W) * H * 4, 0);
                std::vector<uint8_t>& canvas = side ? sideRgba_[side - 1] : rgba_;
                (side ? sideAny[side - 1] : any) = true;
                const int px = sx * 10 / 8, py = sy * 10 / 8;
                if (rbOn && (rt.cellHud[ci] == 2 || (rt.cellHud[ci] == 1 && rbHud)))   // rainbow2
                    rbTint(rt.cellHud[ci], sx, sy);
                if (const uint32_t clip = rt.pcClip[ci]) {
                    // a clipped draw: the PC picture's matching part (cells of 10 PC pixels)
                    const int skipL = int(clip & 0xFF), skipT = int(clip >> 8 & 0xFF), cw = int(clip >> 16 & 0xFF),
                              ch = int(clip >> 24);
                    // pcrank: a PC picture at the arcade's own size (the rank cards 0x593..: 224 x 232 as the arcade's
                    // 28 x 29 cells, where the rest is drawn 1.25 times larger) skips 8 of its pixels a cell
                    const int u = pcSprites_.height(id) == (skipT + ch) * 8 && pcSprites_.width(id) == (skipL + cw) * 8 ? 8 : 10;
                    pcSprites_.draw(canvas, id, px - skipL * u, py - skipT * u, px, py, px + cw * u, py + ch * u);
                } else if (translucentMarks && rt.cellHud[ci] == 2) {
                    // bugs1 (#35, Hit Marks: See-through): the PC's hit mark (0x47E..0x483, a whole picture) at half
                    // alpha over what is drawn, as its Direct3D mode shows them (the blend level not read: 1/2)
                    const int sw = pcSprites_.width(id), sh = pcSprites_.height(id);
                    markLayer_.assign(size_t(W) * H * 4, 0);
                    pcSprites_.draw(markLayer_, id, px, py);
                    for (int y = std::max(0, py); y < std::min(H, py + sh); ++y)
                        for (int x = std::max(0, px); x < std::min(W, px + sw); ++x) {
                            const size_t o = (size_t(y) * W + size_t(x)) * 4;
                            const float a = float(markLayer_[o + 3]) / 510.0f, da = float(canvas[o + 3]) / 255.0f;
                            if (a <= 0.0f)
                                continue;
                            const float oa = a + da * (1.0f - a);
                            for (int c = 0; c < 3; ++c)
                                canvas[o + c] = uint8_t(std::clamp((float(markLayer_[o + c]) * a + float(canvas[o + c]) * da * (1.0f - a)) / oa + 0.5f, 0.0f, 255.0f));
                            canvas[o + 3] = uint8_t(std::clamp(oa * 255.0f + 0.5f, 0.0f, 255.0f));
                        }
                } else {
                    pcSprites_.draw(canvas, id, px, py);
                }
                pcSprites_.setTint(0, nullptr);
            }
    }
    // galact (user 2026-10-28: "score display with PC models shows no SCORE line"): the score line (the title code /
    // Gameplay > Score Display; hud_lives_draw's SCORE:%05d, a text line of layer 0 that Runtime::hud_lives_draw moves
    // to each player's HUD corner, row 0x24) is the arcade's debug font, which the PC's 2D never draws: here in the PC's
    // font (flag 0x20000: character - 0x20 + 0x19D, as "Press SPACE to skip Demo") at the same cells, anchored to the
    // player's side in widescreen as the rest of the HUD. Ours: the PC has no score line
    if (inGame && !clean && mem.mapped(kTileRam) && mem.u32(0x51F42C) == 7) {
        static const bool top = std::getenv("HOTD_SCORE_TOP") != nullptr;
        for (int p = 0; p < 2; ++p) {
            constexpr int kLen = 11;
            const int row = top ? 4 : 0x24, nx = top ? p * 0x1A + 0xE : p == 0 ? 2 : 0x3D - kLen;
            std::string s;
            for (int i = 0; i < kLen; ++i) {
                const uint16_t v = mem.at<uint16_t>(kTileRam + uint32_t(row * 0x80 + (nx + i) * 2));
                const int c = v & 0xFF;
                s += c >= 0x20 && c < 0x7F && (v & 0x8000) ? char(c) : ' ';
            }
            if (s.compare(0, 6, "SCORE:") != 0)
                continue;
            const int side = anchorEdges && !rt.clear.active && !top ? rt.cellSide[size_t(row) * 64 + size_t(nx)] : 0;
            if (side && sideRgba_[side - 1].size() != size_t(W) * H * 4)
                sideRgba_[side - 1].assign(size_t(W) * H * 4, 0);
            std::vector<uint8_t>& canvas = side ? sideRgba_[side - 1] : rgba_;
            (side ? sideAny[side - 1] : any) = true;
            auto glyph = [](unsigned char c) { return c > 0x20 && c < 0x7F ? int(c) - 0x20 + 0x19D : -1; };
            int width = 0;
            for (unsigned char c : s) {
                const int g = glyph(c);
                width += g < 0 || pcSprites_.width(g) == 0 ? 8 : pcSprites_.width(g) + 1;
            }
            // player 1 from the line's left cell, player 2 ending at its right one (the arcade's cells times 1.25)
            int x = p == 0 || top ? nx * 10 : (nx + kLen) * 10 - width;
            for (unsigned char c : s) {
                const int g = glyph(c);
                if (g < 0 || pcSprites_.width(g) == 0) {
                    x += 8;
                    continue;
                }
                if (rbHud) rbTint(1, p * 300, 0);   // (rainbow2: the HUD option; the player's half)
                pcSprites_.draw(canvas, g, x, row * 10);
                pcSprites_.setTint(0, nullptr);
                x += pcSprites_.width(g) + 1;
            }
        }
    }
    // pcrank: the name entry's 3D pieces (Runtime::pcNameModels; the PC files lack the models) as PC sprites centred
    // at their place (arcade screen pixels from the 496 x 384 screen's centre, y up, times 1.25 from the 640 x 480
    // screen's centre as the 3D view centres the picture): the letters A-Z / . - ? ! in the PC's carved font
    // (0x5A5.. / 0x5C1 0x5C0 0x5C2 0x5BF, the PC ranking's), back / forward its menu arrows 0xD8 / 0xD9 (yellow 0xDA / 0xDB under a highlight), END in the carved
    // letters, the timer the ranking digits 0x59B.., the underscores (word_fumidai 0xAC8) a bar, the highlight 0xAC9
    // a frame. UNVERIFIED (teaser): the real PC never shows this screen (its sub-modes 9 / 10 are empty)
    if (rt.name_entry_active() && !rt.pcNameModels.empty()) {
        auto put = [&](int id, float cx, float cy, float s) {
            const int w = pcSprites_.width(id), h = pcSprites_.height(id);
            const std::vector<uint8_t> img = pcSprites_.image(id);
            if (w <= 0 || h <= 0 || img.empty() || s <= 0.05f)
                return;
            const int dw = std::max(1, int(std::lround(w * s))), dh = std::max(1, int(std::lround(h * s)));
            const int x0 = int(std::lround(cx - dw * 0.5f)), y0 = int(std::lround(cy - dh * 0.5f));
            for (int j = 0; j < dh; ++j)
                for (int i = 0; i < dw; ++i) {
                    const int x = x0 + i, y = y0 + j;
                    if (x < 0 || y < 0 || x >= W || y >= H)
                        continue;
                    const uint8_t* src = &img[(size_t(std::min(h - 1, j * h / dh)) * w + size_t(std::min(w - 1, i * w / dw))) * 4];
                    if (src[3])
                        std::memcpy(&rgba_[(size_t(y) * W + x) * 4], src, 4);
                }
        };
        auto box = [&](float cx, float cy, int bw, int bh, int t, const uint8_t c[4]) {
            for (int y = int(cy) - bh / 2; y < int(cy) + bh / 2; ++y)
                for (int x = int(cx) - bw / 2; x < int(cx) + bw / 2; ++x) {
                    const bool edge = y < int(cy) - bh / 2 + t || y >= int(cy) + bh / 2 - t || x < int(cx) - bw / 2 + t ||
                                      x >= int(cx) + bw / 2 - t;
                    if (edge && x >= 0 && y >= 0 && x < W && y < H)
                        std::memcpy(&rgba_[(size_t(y) * W + x) * 4], c, 4);
                }
        };
        // the arrows under a highlight turn yellow (the PC menu's hover)
        std::vector<std::pair<float, float>> lit;
        for (const auto& m : rt.pcNameModels)
            if (m.model == 0xAC9)
                lit.push_back({m.x, m.y});
        auto isLit = [&](const Runtime::PcNameModel& m) {
            for (const auto& [x, y] : lit)
                if (std::fabs(x - m.x) < 1.0f && std::fabs(y - m.y) < 1.0f)
                    return true;
            return false;
        };
        static const uint8_t kBar[4] = {200, 190, 170, 255}, kLit[4] = {255, 214, 64, 255};
        for (const auto& m : rt.pcNameModels)   // the underscores first: under the letters flying onto them
            if (m.model == 0xAC8)
                box(320.0f + m.x * 1.25f, 240.0f - m.y * 1.25f, 50, 4, 2, kBar);
        for (const auto& m : rt.pcNameModels) {
            const float cx = 320.0f + m.x * 1.25f, cy = 240.0f - m.y * 1.25f, s = m.size;
            const uint32_t id = m.model;
            if (id >= 0xADB && id <= 0xAF4)
                put(int(0x5A5 + id - 0xADB), cx, cy, s * 1.4f);
            else if (id >= 0xACA && id <= 0xACD)
                put(id == 0xACA ? 0x5C1 : id == 0xACB ? 0x5C0 : id == 0xACC ? 0x5C2 : 0x5BF, cx, cy, s * 1.4f);
            else if (id == 0xACE || id == 0xACF)
                put((id == 0xACE ? 0xD8 : 0xD9) + (isLit(m) ? 2 : 0), cx, cy, s * 1.4f);
            else if (id == 0xAD0) {   // END: E N D
                const float step = 26.0f * s;
                put(0x5A9, cx - step, cy, s * 1.4f);
                put(0x5B2, cx, cy, s * 1.4f);
                put(0x5A8, cx + step, cy, s * 1.4f);
            } else if (id >= 0xAD1 && id <= 0xADA)
                put(int(0x59B + id - 0xAD1), cx, cy, s * 1.0f);
            else if (id == 0xAC9)
                box(cx, cy, 48, 50, 2, kLit);
        }
        any = true;
    }
    // the subtitle: the PC keeps one line (its tile_text_draw 0x41B1C0 stores the last string; the arcade's second
    // line, lower, comes later) and draws it every frame (0x41B114, not while input bit 5): the 640 x 480 font (glyph
    // routine 0x428494 with flag 0x400000: sprite = character - 0x20 + 0x2DB; 0x7F.. and 0xBF.. not used by the
    // English lines), centred on the screen (width 0x427F64: glyph width + 1, a space 8) with its top at y 360
    // (0x168; 320 x 240 mode: font 0x37A at 180). UNVERIFIED (teaser): compared with the real PC's opening line
    // (pc_look fw2 t00952: white, black-edged, centred low)
    const std::string* line = nullptr;
    int lineY = -1;
    for (const auto& [pos, text] : rt.pcTexts)
        if (!text.empty() && pos.second > lineY) {
            line = &text;
            lineY = pos.second;
        }
    if (line && !noText && !subHide_) {   // (Subtitles Off / Trailer Mode: none; lang: a translation instead)
        auto glyph = [](unsigned char c) { return c > 0x20 && c < 0x7F ? int(c) - 0x20 + 0x2DB : -1; };
        int width = 0;
        for (unsigned char c : *line) {
            const int g = glyph(c);
            width += g < 0 || pcSprites_.width(g) == 0 ? 8 : pcSprites_.width(g) + 1;
        }
        int x = (W - width) / 2;
        for (unsigned char c : *line) {
            const int g = glyph(c);
            if (g < 0 || pcSprites_.width(g) == 0) {
                x += 8;
                continue;
            }
            pcSprites_.draw(rgba_, g, x, 360);
            x += pcSprites_.width(g) + 1;
        }
        any = true;
    }
    // "Continues n" (PC hud_continues_text 0x41B304, from the continue countdown 0x4588A0 while a player is out of
    // lives): "Continues " in the font of flag 0x1000000 (sprite = table 0x5CF4A4[character]) and the count in that of
    // 0x2000000 (0x5CF2A4[character]: digits 0xAC..0xB5), the pair centred on 0x270 (glyph width + 1, a space 8), the
    // words' top at y 0x1C2, the count's at 0x1BE. The count is player 1's continues left (0x00D9F790: the PC's
    // "continues" option + 1, less one a continue). The rewrite plays in free play (no limit): shown as 9 (the real
    // install's setting in the capture) less the continues taken (the score's last digit, player_st1_continue), not
    // below 0, hidden at 0 as the PC. UNVERIFIED (teaser) past the first continue.
    if (mode == 4 && !clean) {   // the countdown 0x4588A0 and a player's own (player_continue_frame -> 0x50BC68)
        bool out = false;
        for (int p = 0; p < 2; ++p)
            out = out || rt.mem.at<Player>(G::players, uint32_t(p)).state == 4;
        // PC Mode: the credits left (docs/pcmode.md row 8: the Continues option's pool)
        const int left = rt.pm_on() ? rt.pm_credits : 9 - std::max(rt.mem.at<Player>(G::players, 0u).score, 0) % 10;
        if (out && left > 0 && lang::active()) {   // (a language: the words in its font at the same place, below)
            langPc.push_back({"Continues " + std::to_string(left), 0x270 / 2, 0x1BE});
        } else if (out && left > 0) {
            static const char kWord[] = "Continues ";
            auto letter = [](char c) -> int {
                switch (c) {
                case 'C': return 0xC5; case 'o': return 0xC0; case 'n': return 0xBF; case 't': return 0xC2;
                case 'i': return 0xBE; case 'u': return 0xC3; case 'e': return 0xBD; case 's': return 0xC1;
                default: return -1;
                }
            };
            int width = 0;
            for (const char* c = kWord; *c; ++c) {
                const int g = letter(*c);
                width += g < 0 || pcSprites_.width(g) == 0 ? 8 : pcSprites_.width(g) + 1;
            }
            int x = (0x270 - width) / 2;
            for (const char* c = kWord; *c; ++c) {
                const int g = letter(*c);
                if (g < 0 || pcSprites_.width(g) == 0) {
                    x += 8;
                    continue;
                }
                pcSprites_.draw(rgba_, g, x, 0x1C2);
                x += pcSprites_.width(g) + 1;
            }
            pcSprites_.draw(rgba_, 0xAC + left, x, 0x1BE);
            any = true;
        }
    }
    // THOTD.EXE 0x41B3F8 demo_skip_prompt (UNVERIFIED (teaser): the routine from the decompilation, placement
    // compared with the harness capture): while an idle screen of modes 2 / 3 / 9 runs (0x419168 sets 0x5CE658 when
    // no key / button is pressed; not in mode 8) the line "Press SPACE to skip Demo" blinks every 500 ms (here 30
    // ticks), centred (width 0x427F64: glyph width + 1, a space 8) at y 0x168 in the font of flag 0x20000 (sprite =
    // character - 0x20 + 0x19D, 0x41AA54 draws it)
    const bool demoLine = !clean && rt.attract && (mode == 2 || mode == 3 || mode == 9) && (rt.frameCount / 30) % 2 == 0;
    if (demoLine && lang::active()) {
        langPc.push_back({"Press SPACE to skip Demo", 0x280 / 2, 0x168});
    } else if (demoLine) {
        static const char* kText = "Press SPACE to skip Demo";
        auto glyph = [](unsigned char c) { return c > 0x20 && c < 0x7F ? int(c) - 0x20 + 0x19D : -1; };
        int width = 0;
        for (const char* c = kText; *c; ++c) {
            const int g = glyph(uint8_t(*c));
            width += g < 0 || pcSprites_.width(g) == 0 ? 8 : pcSprites_.width(g) + 1;
        }
        int x = (0x280 - width) / 2;
        for (const char* c = kText; *c; ++c) {
            const int g = glyph(uint8_t(*c));
            if (g < 0 || pcSprites_.width(g) == 0) {
                x += 8;
                continue;
            }
            pcSprites_.draw(rgba_, g, x, 0x168);
            x += pcSprites_.width(g) + 1;
        }
        any = true;
    }
    pcSprites_.setBloodTexelTint(nullptr);   // (rainbow2)
    if (any)
        upload(rgba_, W, H, ww, wh);
    const float x0 = leftEdge(W, H, ww, wh);
    for (int s = 0; s < 2; ++s)
        if (sideAny[s])
            upload(sideRgba_[s], W, H, ww, wh, s == 0 ? -x0 : x0);
    // lang (misc17): the PC font's words (its sprites hold only the English letters these lines use) in the language:
    // drawn as text, centred on the PC's spot (640 x 480 scaled to the window height, as upload)
    const float sc = float(wh) / float(H);
    for (size_t i = 0; i < langPc.size() && i < 2; ++i) {
        const PcLine& l = langPc[i];
        langPc_[i].setText({l.text}, std::max(1, int(sc * 2.0f + 0.5f)));
        langPc_[i].drawAnchored(ww, wh, float(ww) / 2.0f + (float(l.x) - float(W) / 2.0f) * sc, float(l.y) * sc, 0.5f, 0.0f);
    }
}

// the PC models' placeholder (until the PC's sprites are decoded): the same information as text in the original's
// places
void Hud::drawPlaceholder(const Runtime& rt, int ww, int wh, int scale)
{
    const bool blink = rt.frameCount % 60 < 45;    // hud_ammo_draw's reload prompt: % 60 < 45
    const int16_t mode = rt.mem.at<int16_t>(G::game_mode);
    const uint32_t sub = rt.mem.u32(G::game_submode);
    for (int p = 0; p < 2; ++p) {
        const Player& pl = rt.mem.at<Player>(G::players, uint32_t(p));
        std::vector<std::string> lines;
        char buf[64];
        std::snprintf(buf, sizeof buf, "%dP  %07d", p + 1, std::max(pl.score, 0));
        lines.push_back(buf);
        if (pl.state == 5) {
            std::string torches = "LIFE ";
            for (int i = 0; i < std::clamp(pl.lives, 0, 9); ++i)
                torches += "* ";
            lines.push_back(torches);
            std::string ammo = "AMMO ";
            for (int i = 0; i < 6; ++i)
                ammo += i < pl.rounds ? "| " : ". ";
            lines.push_back(ammo);
            lines.push_back(pl.rounds == 0 && blink ? "RELOAD!" : "");
        } else if (pl.state == 4) {
            const uint32_t t = sub == 4 && mode == 4 ? rt.mem.u32(0x52071C) : uint32_t(pl.continue_timer);
            std::snprintf(buf, sizeof buf, "CONTINUE? %u", std::min(t >> 12, 9u));
            lines.push_back(buf);
            lines.push_back(blink ? "PRESS START" : "");
        } else if (pl.state == 6) {
            lines.push_back("GAME OVER");
        } else {
            lines.push_back(blink ? "PRESS START" : "");
            lines.push_back(p == 0 ? "(key 1)" : "(key 2 / middle button)");
        }
        player_[p].setText(lines, scale);
        const float margin = 12.0f;
        player_[p].drawAnchored(ww, wh, p == 0 ? margin : float(ww) - margin, float(wh) - margin, p == 0 ? 0.0f : 1.0f, 1.0f);
    }
    std::vector<std::string> centre;
    if (mode == 4 && sub == 4) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "CONTINUE?  %u", std::min(rt.mem.u32(0x52071C) >> 12, 9u));
        centre = {buf};
    } else if (mode == 5) {
        centre = {sub >= 3 ? "GAME OVER" : ""};
    }
    // the boss's life bar (sprites.cpp keeps the original's state at 0x543530) and the Chariot's title card (its
    // sprite 0xDF on layer 2 at 7, 5)
    if (rt.mem.at<uint8_t>(kLifeBar) != 0) {
        const int cells = 38;
        const int full = std::clamp(rt.mem.at<int16_t>(kLifeBar + 0xA) / 8, 0, cells);
        boss_.setText({"BOSS " + std::string(size_t(full), '#') + std::string(size_t(cells - full), '.')}, scale - 1);
        boss_.drawAnchored(ww, wh, ww * 0.5f, 12.0f, 0.5f, 0.0f);
    }
    if (placedAt(rt, 0x1004000, 7, 5, 0xDF)) {
        title_.setText({"THE CHARIOT"}, scale * 2);
        title_.drawAt(ww, wh, ww * 0.5f, wh * 0.3f);
    }
    if (!centre.empty() && !centre[0].empty()) {
        centre_.setText(centre, scale * 2);
        centre_.drawAt(ww, wh, ww * 0.5f, wh * 0.45f);
    }
    label_.drawAnchored(ww, wh, ww * 0.5f, float(wh) - 4.0f, 0.5f, 1.0f);
}

void Hud::loadPcSprites()
{
    pcTried_ = true;
    std::string err;
    if (pcDir_.empty() || !pcSprites_.load(pcDir_, err, 1, blood_)) {
        label_.setText({"PC HUD sprites: " + (pcDir_.empty() ? std::string("no PC folder") : err) + " (placeholder)"}, 1);
        return;
    }
    // pchotfix: a damaged / unknown SPR.RBH or BIGGER.VMC: the sights (0x45E / 0x45F) and the 1P mark not on pages that
    // were read: the HUD as without PC sprites (its own drawing), not empty or garbage
    for (const int id : {0x45E, 0x45F})
        if (!pcSprites_.drawable(id)) {
            std::fprintf(stderr, "PC HUD sprites: sprite 0x%X cannot be drawn from these PC files: the HUD's own drawing\n", unsigned(id));
            pcSprites_ = assets::PcSprites{};
            return;
        }
}

// The sights (user 2026-10-16: the originals' crosshairs, not the shot spark 0x660). The PC draws sprites 0x45E (1P,
// orange) / 0x45F (2P, blue) at the aim in play (seen in the real game, private\screenshots\pc_crosshair); they are
// 36 x 36 pixels of its 640 x 480 screen, drawn at the PC's size (its 480 lines scaled to the window's height). The
// arcade has none (a light gun): its style is the same four-triangle shape in the arcade's player colours (1P red, 2P
// blue), so the arcade mode shows no PC pixels. The drawn sights (user 2026-10-17: thinner, crisp in fullscreen): the
// simple cross (3), ring + dot (4), corner brackets (6), gap cross (7), capped cross (10) (dotted ring 5 / pointer 8 removed, user
// 2026-10-19) in window pixels, sized from the window height (12 of the PC's pixels each side, times the Crosshair size
// step), the line the Crosshair thickness step in pixels at 1080 lines; style 9: the player's own PNG pictures
// (game/sights.h), scaled with the window height; their centre pixel on the aim's pixel. Firing range (user
// 2026-10-26): each player's own style, size, thickness and colour (SeatSight); the pictures: game/sight_art.cpp (the
// menu's Firing Range draws the same ones). Players 3 / 4 in the player colour: players 1 / 2's art recoloured.
bool Hud::buildSeatSight(int seat, bool pcModels, int wh)
{
    const game::SeatSight& s = sights[seat];
    const int style = game::sightStyleKept(s.style);
    const bool sprite = style <= 2;
    if (sprite && !pcTried_)
        loadPcSprites();   // the arcade style takes the PC sight's shape too
    // (the sprites: drawn at the PC's 480 lines and scaled; the others: at the window's height)
    const int artWh = sprite ? assets::PcSprites::kHeight : wh;
    const std::string key = std::to_string(style) + "|" + s.custom + "|" + std::to_string(s.size) + "|" + std::to_string(s.thick) + "|" +
                            std::to_string(s.colour) + "|" + std::to_string(artWh) + "|" + (pcModels ? "pc" : "arc") + "|" +
                            (pcSprites_.loaded() ? "s" + pcDir_ : std::string("-"));
    if (key == seatKey_[seat])
        return seatOk_[seat];
    seatKey_[seat] = key;
    std::vector<game::CustomSight> sets;
    if (style == game::kSightCustom)
        sets = game::findCustomSights(customDirs);
    game::SightArt a;
    std::string err;
    if (!game::seatSightArt(s, pcModels, &pcSprites_, sets, seat, artWh, a, &err) && style == game::kSightCustom) {
        if (!err.empty()) std::fprintf(stderr, "crosshair: %s\n", err.c_str());
        std::fprintf(stderr, "crosshair: P%d's custom set \"%s\" not found or broken, drawing the simple cross\n", seat + 1, s.custom.c_str());
    }
    seatOk_[seat] = a.w > 0 && a.h > 0 && a.rgba.size() >= size_t(a.w) * size_t(a.h) * 4;
    if (!seatOk_[seat])
        return false;
    seatSprite_[seat] = sprite;
    seatW_[seat] = a.w, seatH_[seat] = a.h;
    seatDw_[seat] = a.dw, seatDh_[seat] = a.dh, seatCx_[seat] = a.cx, seatCy_[seat] = a.cy;
    if (!seatTex_[seat]) gl::GenTextures(1, &seatTex_[seat]);
    gl::BindTexture(gl::TEXTURE_2D, seatTex_[seat]);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, a.w, a.h, 0, gl::RGBA, gl::UNSIGNED_BYTE, a.rgba.data());
    const int f = sprite || a.linear ? gl::LINEAR : gl::NEAREST;
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, f);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, f);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    return true;
}

// a player's sight on window pixel (x, y): a sprite centred at the PC's size, a drawn one's centre pixel on the aim's pixel
void Hud::drawSight(int player, int ww, int wh, float x, float y)
{
    float r[4];
    if (seatSprite_[player])
        sightRect(seatW_[player], seatH_[player], wh, x, y, r);
    else
        r[0] = std::floor(x) - float(seatCx_[player]), r[1] = std::floor(y) - float(seatCy_[player]), r[2] = float(seatDw_[player]),
        r[3] = float(seatDh_[player]);
    const unsigned tex = seatTex_[player];
    const float pw = r[2], ph = r[3];
    gl::Viewport(0, 0, ww, wh);
    gl::Disable(gl::DEPTH_TEST);
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::UseProgram(program_);
    gl::Uniform4f(uRect_, -1.0f + 2.0f * r[0] / float(ww), 1.0f - 2.0f * (r[1] + ph) / float(wh),
                  2.0f * pw / float(ww), 2.0f * ph / float(wh));
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, tex);
    if (sightTintOn_[player])   // rainbow2: Rainbow Blood's crosshair (drawSights)
        gl::Uniform4f(uTint_, sightTint_[player][0], sightTint_[player][1], sightTint_[player][2], 1.0f);
    gl::BindVertexArray(vao_);
    gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
    gl::BindVertexArray(0);
    if (sightTintOn_[player])
        gl::Uniform4f(uTint_, 0.0f, 0.0f, 0.0f, 0.0f);
    gl::Disable(gl::BLEND);
    gl::Enable(gl::DEPTH_TEST);
}

void Hud::sightRect(int w, int h, int wh, float x, float y, float out[4])
{
    const float s = float(wh) / float(assets::PcSprites::kHeight);
    out[2] = float(w) * s;
    out[3] = float(h) * s;
    out[0] = x - out[2] / 2.0f;
    out[1] = y - out[3] / 2.0f;
}

void Hud::setPcDir(const std::string& dir, int blood)
{
    if (dir == pcDir_ && blood == blood_ && pcTried_)
        return;
    pcDir_ = dir;
    blood_ = blood;
    pcTried_ = false;
    // perf (2026-10-16): loaded at the start (was on the first PC frame: a 70-990 ms first frame)
    if (!dir.empty())
        loadPcSprites();
}

// THOTD.EXE 0x41AB7C (UNVERIFIED (teaser)): while paused, three labels of the string table (0x428448) centred on the
// 640 x 480 screen (width 0x427F64: glyph width + 1, a space or a missing glyph 8): 0x5F "PAUSED" at y 200 in the font
// of flag 0x2000000, 0x5E "Continue" at y 0xF8 and 0x24 "Exit" at y 0x110 in that of 0x1000000, the selected one with
// 0x40000 too (the highlight font); drawn by 0x41AA54 (sprite origin at the point). The fonts' sprites are THOTD.EXE's
// tables (0x428494): 0x2000000 -> 0x5CF2A4[c], 0x1000000 -> 0x5CF4A4[c], 0x1000000 | 0x40000 -> 0x5CF6A4[c] (only
// the letters these words use are set there; read from the exe once, copied here).
// Boss Mode (game/bossmode.h): THOTD.EXE 0x418CC8 while 0x5CD380 is set: the clock (0x412B78, at most 3600000 ms) as
// "%02d:%02d:%02d" (minutes, seconds, hundredths) at (500, 0x1B8) of the 640 x 480 screen in the font of flag 0x2000000
// (0x5CF2A4: digits 0xAC..0xB5, ':' 0xB6), drawn by 0x41AA54 (glyph width + 1). Not drawn while paused (the PC stops
// the clock then too). UNVERIFIED (teaser): not compared with a capture.
// Ours (sweep2): centred at the bottom instead of x 500: there it touched the arcade HUD's PRESS START BUTTON (player 2's
// empty seat, arcade models) and player 2's lamps / 2P (both models) at the bottom right; the digits in cells of the
// widest digit's width (the clock does not move about as its digits change)
void Hud::drawBossClock(const Runtime& rt, int ww, int wh)
{
    if (!pcTried_)
        loadPcSprites();
    if (!pcSprites_.loaded())
        return;
    const int W = assets::PcSprites::kWidth, H = assets::PcSprites::kHeight;
    rgba_.assign(size_t(W) * H * 4, 0);
    int cell = 0;
    for (int d = 0; d < 10; ++d)
        cell = std::max(cell, pcSprites_.width(0xAC + d));
    const std::string t = rt.boss.clockText();
    auto widthOf = [&](char c) {
        const int g = c >= '0' && c <= '9' ? 0xAC + (c - '0') : c == ':' ? 0xB6 : 0;
        return g == 0 || pcSprites_.width(g) == 0 ? 8 : (c == ':' ? pcSprites_.width(g) : cell) + 1;
    };
    int total = 0;
    for (char c : t) total += widthOf(c);
    int x = (W - total) / 2;
    for (char c : t) {
        const int g = c >= '0' && c <= '9' ? 0xAC + (c - '0') : c == ':' ? 0xB6 : 0;
        if (g != 0 && pcSprites_.width(g) != 0)
            pcSprites_.draw(rgba_, g, x + (c == ':' ? 0 : (cell - pcSprites_.width(g)) / 2), 0x1B8);
        x += widthOf(c);
    }
    upload(rgba_, W, H, ww, wh);
}

// Survival Mode (ours, game/survival.h): the wave line at the top centre (the wave, the creatures left, the SMG's rounds
// per player in play) and the moment's banner (WAVE n, SMG UNLOCKED, WAVE CLEARED, GAME OVER) in the middle
void Hud::drawSurvival(const Runtime& rt, int ww, int wh, int scale)
{
    const SurvivalMode& h = rt.survival;
    std::vector<std::string> top = h.hudLines(rt);
    // the shop (survival2): the signs between the rounds where the arcade screen's place for them lies (the 496 x 384
    // screen fitted to the window's height, centred; arcade units y up), a shot at one buys it. Yellow: a player in play
    // can buy it now; grey: not
    if (h.shopOpen()) {
        const float k = float(wh) / 384.0f;
        for (int i = 0; i < SurvivalMode::kItems; ++i) {
            const SurvivalMode::Sign& s = SurvivalMode::kSigns[i];
            bool can = false;
            for (int p = 0; p < rt.seat_count(); ++p)
                can = can || h.canBuy(rt, p, i);
            if (can != signLit_[i]) {
                signLit_[i] = can;
                survivalSign_[i].setStyle(170, can ? 255 : 130, can ? 220 : 130, can ? 90 : 130);
            }
            survivalSign_[i].setText({s.name, std::to_string(s.price)}, std::max(1, scale - 1));
            survivalSign_[i].drawAt(ww, wh, float(ww) * 0.5f + s.x * k, float(wh) * 0.5f - s.y * k);
        }
    }
    if (!top.empty()) {
        survival_.setText(top, std::max(1, scale - 1));
        survival_.drawAnchored(ww, wh, ww * 0.5f, wh * 0.11f, 0.5f, 0.0f);
    }
    if (!h.banner.empty()) {
        survivalBanner_.setText({h.banner}, h.banner.size() > 20 ? scale : scale + 1);
        survivalBanner_.drawAt(ww, wh, ww * 0.5f, wh * 0.38f);
    }
}

// lang: the subtitle table (G::voice_subtitles, 96 bytes an event: line 1 at +1, line 2 at +0x31) read from the
// game's memory once; the lines tile_text_draw shows now (Runtime::pcTexts) that have a translation
std::vector<std::string> Hud::subLines(const Runtime& rt)
{
    std::vector<std::string> out;
    if (!lang::active() || rt.pcTexts.empty())
        return out;
    if (!subIndexed_ && rt.mem.mapped(G::voice_subtitles)) {
        subIndexed_ = true;
        int bad = 0;
        for (int e = 0; e < 256 && bad < 8; ++e)
            for (int l = 0; l < 2; ++l) {
                const uint32_t a = G::voice_subtitles + uint32_t(e) * 96 + (l ? 0x31u : 1u);
                std::string s;
                for (uint32_t i = 0; i < 48 && rt.mem.mapped(a + i); ++i) {
                    const uint8_t c = rt.mem.at<uint8_t>(a + i);
                    if (!c) break;
                    if (c < 0x20 || c > 0x7E) { s.clear(); break; }
                    s += char(c);
                }
                if (l == 0) bad = s.find_first_not_of(' ') == std::string::npos ? bad + 1 : 0;
                if (s.find_first_not_of(' ') != std::string::npos) subIndex_[s].push_back(e * 2 + l);
            }
    }
    std::vector<std::pair<int, std::string>> lines;
    for (const auto& [pos, text] : rt.pcTexts) {
        auto it = subIndex_.find(text);
        if (it == subIndex_.end()) continue;
        for (const int k : it->second)
            if (const std::string* t = lang::subtitle(k / 2, k % 2 + 1)) {
                lines.push_back({pos.second, *t});
                break;
            }
    }
    std::sort(lines.begin(), lines.end());
    for (const auto& l : lines) out.push_back(l.second);
    return out;
}

void Hud::drawSubtitles(const std::vector<std::string>& lines, int ww, int wh)
{
    constexpr int W = 1280, H = 960;   // (the 4:3 screen at twice the PC's 640 x 480)
    std::string all;
    for (const std::string& l : lines) all += l;
    const render::TtfText* tf = lang::fontFor(all);
    if (!tf) tf = lang::anyFont();   // (an ASCII / Latin-1 translation: a font from data/fonts all the same)
    if (!tf) return;
    if (lines != subShown_ || subRgba_.size() != size_t(W) * H * 4) {
        subShown_ = lines;
        subRgba_.assign(size_t(W) * H * 4, 0);
        const float em = 40.0f;
        std::vector<std::string> rows;
        for (const std::string& l : lines)
            for (const std::string& r : tf->wrap(l, em, W - 120.0f)) rows.push_back(r);
        std::vector<float> cov(size_t(W) * H, 0.0f);
        float base = 0.75f * H + em * 0.8f;
        for (const std::string& r : rows) {
            const float x = (float(W) - tf->width(r, em)) / 2.0f;
            tf->draw(r, em, x, base, [&](int px, int py, float a) {
                if (px >= 0 && py >= 0 && px < W && py < H) cov[size_t(py) * W + size_t(px)] = std::min(1.0f, cov[size_t(py) * W + size_t(px)] + a);
            });
            base += em * 1.25f;
        }
        for (int y = 2; y < H - 2; ++y)   // white letters with a dark edge (2 pixels), as the game's own lines
            for (int x = 2; x < W - 2; ++x) {
                float edge = 0;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx) edge = std::max(edge, cov[size_t(y + dy) * W + size_t(x + dx)]);
                if (edge <= 0.0f) continue;
                const float a = cov[size_t(y) * W + size_t(x)];
                uint8_t* d = &subRgba_[(size_t(y) * W + size_t(x)) * 4];
                const uint8_t v = uint8_t(255.0f * a / std::max(a + (1.0f - a) * edge, 1e-3f));
                d[0] = d[1] = d[2] = v;
                d[3] = uint8_t(255.0f * std::max(a, edge * 0.9f));
            }
    }
    upload(subRgba_, W, H, ww, wh);
}

void Hud::drawPause(int selection, int ww, int wh)
{
    auto glyph = [](char c, uint32_t flags) -> int {
        if (flags & 0x2000000) {   // 0x5CF2A4
            switch (c) {
            case 'A': return 0xB7; case 'D': return 0xB8; case 'E': return 0xB9; case 'P': return 0xBA;
            case 'S': return 0xBB; case 'U': return 0xBC; default: return 0;
            }
        }
        const int hi = (flags & 0x40000) ? 10 : 0;   // 0x5CF6A4 = 0x5CF4A4's sprites + 10
        switch (c) {
        case 'e': return 0xBD + hi; case 'i': return 0xBE + hi; case 'n': return 0xBF + hi; case 'o': return 0xC0 + hi;
        case 's': return 0xC1 + hi; case 't': return 0xC2 + hi; case 'u': return 0xC3 + hi; case 'x': return 0xC4 + hi;
        case 'C': return 0xC5 + hi; case 'E': return 0xC6 + hi; default: return 0;
        }
    };
    struct Line { const char* text; int y; uint32_t flags; };
    const Line lines[3] = {{"PAUSED", 200, 0x2000000},
                           {"Continue", 0xF8, (selection == 0 ? 0x40000u : 0u) | 0x1000000},
                           {"Exit", 0x110, (selection == 1 ? 0x40000u : 0u) | 0x1000000}};
    if (!pcTried_)
        loadPcSprites();
    // fxux: every way of drawing it keeps its items' rectangles (pauseItemAt: the pointer, the VR gun)
    pauseRects_.clear();
    const bool own = pauseLines.size() >= 2;   // (#5 / fxux: Settings, Retry Chapter, the game over's menu)
    const std::vector<std::string> L = own ? pauseLines : std::vector<std::string>{"PAUSED", "Continue", "Exit"};
    const int items = int(L.size()) - 1;
    if (!pcSprites_.loaded() || lang::active()) {   // no PC sprites (or a language: its words): the lines as text
        std::vector<std::string> t = {L[0], ""};
        for (int i = 0; i < items; ++i)
            t.push_back(i == selection ? "> " + lang::tr(L[size_t(i) + 1]) + " <" : L[size_t(i) + 1]);
        const int S = wh >= 720 && ww >= 1280 ? 3 : 2;
        pause_.setText(t, S);
        pause_.drawAt(ww, wh, ww * 0.5f, wh * 0.5f);
        // (Overlay's 8 x 8 lines: 6 S padding, 11 S a line, centred on the window; in 640 x 480 units of its height)
        const float s = float(wh) / 480.0f, top = float(wh) * 0.5f - float(int(t.size()) * 11 - 3 + 12) * float(S) * 0.5f;
        for (int i = 0; i < items; ++i) {
            const float y0 = top + float(6 + (i + 2) * 11) * float(S) - 1.5f * float(S);
            pauseRects_.push_back({210.0f, y0 / s, 430.0f, (y0 + 11.0f * float(S)) / s});
        }
        return;
    }
    const int W = assets::PcSprites::kWidth, H = assets::PcSprites::kHeight;
    rgba_.assign(size_t(W) * H * 4, 0);
    if (own) {
        // fxux: the title in its pause font when it has the letters (PAUSED), else the menu's white 32; the items in the
        // menu's white 24 (the chosen one yellow 24, as the menus' hovered rows), 28 apart from the PC's Continue row
        auto row = [&](const std::string& text, int y, int base) {
            auto id = [&](char c) { return c > ' ' && c <= '~' ? base + (c - 0x20) : 0; };
            int width = 0;
            for (char c : text)
                width += id(c) == 0 || pcSprites_.width(id(c)) == 0 ? 8 : pcSprites_.width(id(c)) + 1;
            int x = (0x280 - width) / 2;
            for (char c : text) {
                if (id(c) == 0 || pcSprites_.width(id(c)) == 0) {
                    x += 8;
                    continue;
                }
                pcSprites_.draw(rgba_, id(c), x, y);
                x += pcSprites_.width(id(c)) + 1;
            }
        };
        if (L[0] != "PAUSED")
            row(L[0], 196, 0xFE);   // (white 32)
        for (int i = 0; i < items; ++i) {
            const int y = 0xF8 + 28 * i;
            row(L[size_t(i) + 1], y, i == selection ? 0x23C : 0x19D);
            pauseRects_.push_back({210.0f, float(y - 4), 430.0f, float(y + 24)});
        }
        // relfix2 (user 2026-11-03): the menu's 24 / 32 fonts have no edge of their own (hard to read over bright
        // scenes): a black edge around them as the pause font's own (PAUSED, Continue, Exit): 1 pixel solid, a soft
        // second one; PAUSED is drawn after it with its own edge
        {
            std::vector<uint8_t> a(size_t(W) * H, 0);
            for (size_t p = 0; p < a.size(); ++p) a[p] = rgba_[p * 4 + 3];
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    if (a[size_t(y) * W + size_t(x)]) continue;
                    int edge = 0;
                    for (int dy = -2; dy <= 2 && edge < 255; ++dy)
                        for (int dx = -2; dx <= 2; ++dx) {
                            const int sx = x + dx, sy = y + dy;
                            if (sx < 0 || sy < 0 || sx >= W || sy >= H || !a[size_t(sy) * W + size_t(sx)]) continue;
                            const int d = std::abs(dx) + std::abs(dy);
                            edge = std::max(edge, std::max(std::abs(dx), std::abs(dy)) <= 1 ? 255 : d <= 3 ? 120 : 0);
                        }
                    if (!edge) continue;
                    uint8_t* d = &rgba_[(size_t(y) * W + size_t(x)) * 4];
                    d[0] = d[1] = d[2] = 0;
                    d[3] = uint8_t(edge);
                }
        }
        if (L[0] == "PAUSED") {
            int width = 0, x = 0;
            for (char c : L[0]) width += pcSprites_.width(glyph(c, 0x2000000)) + 1;
            x = (0x280 - width) / 2;
            for (char c : L[0]) {
                pcSprites_.draw(rgba_, glyph(c, 0x2000000), x, 200);
                x += pcSprites_.width(glyph(c, 0x2000000)) + 1;
            }
        }
        upload(rgba_, W, H, ww, wh);
        return;
    }
    pauseRects_.push_back({230.0f, 244.0f, 410.0f, 270.0f});   // (the PC's two rows: as main.cpp had them)
    pauseRects_.push_back({230.0f, 270.0f, 410.0f, 296.0f});
    for (const Line& l : lines) {
        int width = 0;   // 0x427F64
        for (const char* c = l.text; *c; ++c) {
            const int g = *c == ' ' ? 0 : glyph(*c, l.flags);
            width += g == 0 || pcSprites_.width(g) == 0 ? 8 : pcSprites_.width(g) + 1;
        }
        int x = (0x280 - width) / 2;
        for (const char* c = l.text; *c; ++c) {   // 0x41AA54
            const int g = *c == ' ' ? 0 : glyph(*c, l.flags);
            if (g == 0 || pcSprites_.width(g) == 0) {
                x += 8;
                continue;
            }
            pcSprites_.draw(rgba_, g, x, l.y);
            x += pcSprites_.width(g) + 1;
        }
    }
    upload(rgba_, W, H, ww, wh);
}

namespace {

// the 4P HUD's private 2D layer (drawSeatHud): sprites "loaded" into a character RAM / palette of its own straight
// from the sheets (as sprite_bank_tiles_load / _palette_load copy them), so any sprite can be drawn without touching
// the game's banks, slots or palettes (no game state changes: the 2D layer is the game's)
struct PrivLayer {
    std::vector<uint8_t> tiles, chars, pal;
    int slot = 1;
    void reset()
    {
        tiles.assign(0x10000, 0);
        chars.assign(0x80000, 0);
        pal.assign(0x2000, 0);
        slot = 1;
        for (int l = 1; l < 4; ++l)
            tiles[size_t(0xA008 + 2 * l + 1)] = 0x80;   // the y scroll's bit 15: layers 1-3 off
    }
    bool put(port::Mem& mem, uint32_t sprite, int x, int y)
    {
        constexpr uint32_t kSheets = 0xA6590;
        const uint32_t sheet = kSheets + sprite * 12;
        uint32_t t = mem.u32(sheet), src = mem.u32(sheet + 4);
        if (t == 0 || !mem.mapped(t))
            return false;
        const int first = slot;
        do {
            const uint32_t c = mem.u32(t);
            const int n = int((c + 0x7F) / 0x80);
            if (slot + n > 0x80)
                return false;
            for (uint32_t i = 0; i < c * 32; ++i)
                chars[size_t(slot) * 0x1000 + i] = mem.at<uint8_t>(t + 4 + i);
            for (int k = 0; k < n; ++k)
                for (uint32_t i = 0; i < 0x20; ++i)
                    pal[size_t(slot + k) * 0x20 + i] = mem.at<uint8_t>(src + i);
            slot += n;
            t += 4 + c * 32;
            src += 0x20;
        } while (mem.u32(t) != 0);
        const uint32_t lay = mem.u32(sheet + 8);
        const int w = mem.at<uint16_t>(lay), h = mem.at<uint16_t>(lay + 2);
        const uint16_t base = uint16_t(int16_t(first) * 0x80 - 0x8000);
        uint32_t s = lay + 4;
        for (int r = 0; r < h; ++r)
            for (int k = 0; k < w; ++k, s += 2) {
                const int cx = x + k, cy = y + r;
                if (cx < 0 || cx >= 64 || cy < 0 || cy >= 64)
                    continue;
                const uint16_t v = uint16_t(base + mem.at<uint16_t>(s));
                tiles[size_t(cy * 0x80 + cx * 2)] = uint8_t(v);
                tiles[size_t(cy * 0x80 + cx * 2 + 1)] = uint8_t(v >> 8);
            }
        return true;
    }
};

// an RGBA picture's part with alpha (x0, y0, w, h); false: empty
bool alphaBox(const std::vector<uint8_t>& px, int W, int H, int& x0, int& y0, int& w, int& h)
{
    int a = W, b = H, c = -1, d = -1;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (px[(size_t(y) * W + x) * 4 + 3]) {
                a = std::min(a, x); b = std::min(b, y); c = std::max(c, x); d = std::max(d, y);
            }
    if (c < 0)
        return false;
    x0 = a; y0 = b; w = c - a + 1; h = d - b + 1;
    return true;
}

SeatImg crop(const std::vector<uint8_t>& px, int W, int H)
{
    SeatImg o;
    int x0, y0;
    if (!alphaBox(px, W, H, x0, y0, o.w, o.h))
        return o;
    o.px.assign(size_t(o.w) * o.h * 4, 0);
    for (int y = 0; y < o.h; ++y)
        std::copy_n(&px[(size_t(y0 + y) * W + x0) * 4], size_t(o.w) * 4, &o.px[size_t(y) * o.w * 4]);
    return o;
}

// over `dst` (W x H) at (x, y), alpha blended
void paste(std::vector<uint8_t>& dst, int W, int H, const SeatImg& s, int x, int y)
{
    for (int j = 0; j < s.h; ++j)
        for (int i = 0; i < s.w; ++i) {
            const int X = x + i, Y = y + j;
            if (X < 0 || Y < 0 || X >= W || Y >= H)
                continue;
            const uint8_t* q = &s.px[(size_t(j) * s.w + i) * 4];
            if (!q[3])
                continue;
            uint8_t* d = &dst[(size_t(Y) * W + X) * 4];
            const int a = q[3];
            for (int c = 0; c < 3; ++c)
                d[c] = uint8_t((q[c] * a + d[c] * (255 - a)) / 255);
            d[3] = uint8_t(std::max<int>(d[3], a));
        }
}

// a sprite as a cropped RGBA picture in the model source's own art (arcade: its sheet through a private layer; PC: the
// matching PC sprite)
SeatImg spriteImg(port::Mem& mem, bool pc, const assets::PcSprites& pcs, uint32_t sprite)
{
    if (pc) {
        SeatImg o;
        if (sprite >= 0x276)
            return o;
        const int id = kArcadeToPcSprite[sprite];
        if (!id)
            return o;
        const std::vector<uint8_t> px = pcs.image(id);
        return px.empty() ? o : crop(px, pcs.width(id), pcs.height(id));
    }
    static PrivLayer l;
    l.reset();
    if (!l.put(mem, sprite, 0, 0))
        return {};
    std::vector<uint8_t> rgba;
    if (!assets::Arcade2D::compose(l.tiles.data(), l.chars.data(), l.pal.data(), true, rgba))
        return {};
    return crop(rgba, assets::Arcade2D::kWidth, assets::Arcade2D::kHeight);
}

} // namespace

// The 4P mode (docs/coop4.md): players 3 / 4's HUD. The original draws only two corners (hud_lives_draw / hud_ammo_draw:
// player 1 bottom left, player 2 bottom right); seats 3 / 4 get the same pieces at the top corners (P3 left, P4 right,
// as players 1 / 2 lay them out, moved up by 0x25 cells): their "3P" / "4P" label, the rounds (0x10, one per round
// left) and per life a torch base (0x14) with its flickering fire lamp (7 steps of 6 frames, each torch its phase from
// 0xCA6F0); out of lives CONTINUE? (0x113) and its digit (0xCA720). All in the players' own art: player 2's lamps
// (blue flames) through the seat's colour ramp (player_colours.h, user 2026-10-20: shades, not a flat recolour), the
// label built from the countdown's digit and player 2's label's "P", through the ramp too; the rounds as they are.
// Arcade: the sheets through a private 2D layer (no game banks used); PC: its own matching sprites. The band moves down
// past anything the game draws there (the boss life bar, a title card: tile layers 0 / 2 not empty under it).
// GitHub #25 (user 2026-11-03: "9 lives support"; HOTD 3 / 4 / Scarlet Dawn show "x N" above 5): the arcade's HUD has
// five torch places a player (cells 6 .. 25 / 0x34 .. 0x21, players 1 / 2 meet in the middle: no room for 9), so with
// more than 5 lives hud_lives_draw keeps the first torch and this draws "x N" where the second torch is (player 1:
// cells 10 / 12, player 2 mirrored: 0x2F / 0x31), rows 0x2A-0x2B as the torches: the name entry's letter X (0x1AC) and
// the results' score digits (0x181 + n), white as the game draws them, the PC's own matching sprites with PC models.
// In play and in the chapter's results (the torches' own times), not in the cut-scenes (their HUD is taken off)
void Hud::drawLivesCount(const Runtime& rt, int ww, int wh)
{
    port::Mem& mem = rt.mem;
    if (mem.at<int16_t>(G::game_mode) != 4 || !mem.mapped(kTileRam))
        return;
    if (mem.at<uint8_t>(G::scene_state) != 2 && !rt.clear.active)
        return;
    const bool pc = rt.pcModels && pcSprites_.loaded();
    if (!pc && rt.pcModels)
        return;
    const int W = pc ? assets::PcSprites::kWidth : assets::Arcade2D::kWidth;
    const int H = pc ? assets::PcSprites::kHeight : assets::Arcade2D::kHeight;
    for (int p = 0; p < 2; ++p) {
        const Player& pl = mem.at<Player>(G::players, uint32_t(p));
        if (pl.state != 5 || pl.lives <= 5)
            continue;
        const bool left = p == 0;
        const uint32_t pieces[2][2] = {{0x1ACu, uint32_t(left ? 10 : 0x2F)},
                                       {0x181u + uint32_t(std::min<int32_t>(pl.lives, 9)), uint32_t(left ? 12 : 0x31)}};
        seatRgba_.assign(size_t(W) * H * 4, 0);
        bool any = false;
        if (pc) {
            for (const auto& c : pieces)
                if (const int id = kArcadeToPcSprite[c[0]]) {
                    pcSprites_.draw(seatRgba_, id, int(c[1]) * 10, 0x2A * 10);
                    any = true;
                }
        } else {
            static PrivLayer l;
            l.reset();
            for (const auto& c : pieces)
                any = l.put(mem, c[0], int(c[1]), 0x2A) || any;
            if (any)
                any = assets::Arcade2D::compose(l.tiles.data(), l.chars.data(), l.pal.data(), true, seatRgba_);
        }
        if (!any)
            continue;
        // widescreen with the corners at the window's edges (display2): beside the side's torch
        const float x0 = anchorEdges && !rt.clear.active ? leftEdge(W, H, ww, wh) : 0.0f;
        upload(seatRgba_, W, H, ww, wh, left ? -x0 : x0);
    }
}

void Hud::drawSeatHud(const Runtime& rt, int ww, int wh)
{
    port::Mem& mem = rt.mem;
    if (mem.at<int16_t>(G::game_mode) != 4)
        return;
    // the ending (sub-modes 7 / 8: game_ending_start put players 1 / 2 to state 7, so their HUD is gone): none for
    // players 3 / 4 either (their HOLD YOUR FIRE! blinked through the Magician's farewell; cutfix 2026-10-22)
    if (const int16_t sub = mem.at<int16_t>(G::game_submode); sub == 7 || sub == 8)
        return;
    // the lamps / rounds: in play (as hud_lives_draw) and in the chapter's results (as players 1 / 2's there; user
    // 2026-10-26: P3 / P4's score boxes showed without them; the band goes under their boxes)
    const bool inClear = rt.clear.active;
    const bool hudOn = mem.at<uint8_t>(G::scene_state) == 2 || inClear;
    const bool pc = rt.pcModels && pcSprites_.loaded();
    if (!pc && (rt.pcModels || !mem.mapped(kTileRam) || !mem.mapped(0x2000000)))
        return;
    const int unit = pc ? 10 : 8;   // pixels a cell (the PC's picture is 1.25 times the arcade's)
    const int W = pc ? assets::PcSprites::kWidth : assets::Arcade2D::kWidth;
    const int H = pc ? assets::PcSprites::kHeight : assets::Arcade2D::kHeight;
    for (int seat = 2; seat < 4; ++seat) {
        const Player& pl = mem.at<Player>(rt.player_rec(seat));
        if (pl.state != 5 && pl.state != 4 && pl.state != 9)
            continue;
        const bool blinkOn = mem.u32(G::frame_counter) % 0x3C < 0x2D;   // (45 of 60 frames, as the arcade's prompts)
        // PRESS START BUTTON blinks with player 2's (hud_prompt_anim: coin_update's counter 0x51F560, off in one phase
        // of three, 32 frames each); it blinked by the frame counter, P3 / P4 together but out of step with P2 (user
        // 2026-10-26: "one shared blink phase for all")
        const bool startOn = (mem.u32(0x51F560) >> 5) % 3 != 2;
        const bool left = seat == 2;
        // the band (cells): moved down past the game's own cells there (life bar, title cards)
        const int bx0 = left ? 1 : 0x24, bx1 = left ? 0x1A : 0x3D;
        // (players 1 / 2's hit notices on layer 2 (hud_player_notice_show: the blood splash, kPopups 0x51F480) do not
        // count: they come and go in 60 frames and the band jumped down under them)
        auto notice = [&](int x, int y) {
            for (uint32_t p = 0; p < 2; ++p) {
                const uint32_t r = 0x51F480 + p * 8;
                if (mem.at<uint8_t>(r) == 0)
                    continue;
                const int kind = mem.at<int8_t>(r + 3), who = int32_t(mem.u32(r + 4));
                const uint32_t pos = 0x94368 + uint32_t(kind * 16 + who * 8);
                const uint32_t spr = mem.u32(0xCA5FC + uint32_t(kind * 8 + who * 4));
                if (spr >= 0x400 || !mem.mapped(pos))
                    continue;
                const uint32_t lay = mem.u32(0xA6590 + spr * 12 + 8);
                if (lay == 0 || !mem.mapped(lay))
                    continue;
                const int px = mem.at<int8_t>(r + 2) + int32_t(mem.u32(pos)), py = int32_t(mem.u32(pos + 4));
                if (x >= px && x < px + mem.at<uint16_t>(lay) && y >= py && y < py + mem.at<uint16_t>(lay + 2))
                    return true;
            }
            return false;
        };
        // (a row at a time, so the band sits right under the life bar: it went down in steps of 4 rows, up to 3 rows
        // lower than needed; user 2026-10-20 "P3 and P4 HUD are slightly too low")
        // No free spot in 16 rows (the game's own CONTINUE screen with players 1 / 2 out: the rope and the big
        // CONTINUE? fill them): the band stays at the top; it went down onto the big CONTINUE? (promptcol)
        int dy = -0x25;
        bool found = false;
        for (int tries = 0; tries < 16 && !found; ++tries) {
            bool busy = false;
            for (uint32_t layer : {0x1000000u, 0x1004000u})
                for (int y = 1 + dy + 0x25; y < 8 + dy + 0x25 && !busy; ++y)
                    for (int x = bx0; x <= bx1 && !busy; ++x)
                        busy = mem.at<uint16_t>(layer + uint32_t(y * 0x80 + x * 2)) != 0 && !(layer == 0x1004000u && notice(x, y));
            if (!busy)
                found = true;
            else
                dy += 1;
        }
        if (!found)
            dy = -0x25;
        if (inClear)   // the results: under the seat's score (its digits at row 4) as players 1 / 2's sit under theirs (0x23)
            dy = 4 - 0x23;
        seatDy_[seat - 2] = dy;   // (Light Gun Edition: Arsenal Mode's rounds go with the band)
        // kind 0 as it is, 1 flames (blue texels), 2 all. The prompts (RELOAD, HOLD YOUR FIRE!, CONTINUE?, PRESS START
        // BUTTON) are kind 0: the arcade draws them in their own colours for players 1 and 2 alike (red / white), so
        // players 3 / 4's too (user 2026-10-20: "the HOLD YOUR FIRE is supposed to be red")
        struct Piece { int x, y; uint32_t sprite; int kind; };
        std::vector<Piece> pieces;
        bool label = false;
        // where the window layer shows (a chapter's title card): players 1 / 2's prompts are hidden by the same window
        // mask (drawPc2D / Arcade2D::compose); cy: the cell row
        auto windowed = [&](int cx, int cy) {
            const int sx = cx * 8, sy = std::clamp(cy * 8 + 4, 0, 383);
            for (uint32_t base : {0x100C000u, 0x100D000u})
                if (mem.mapped(base) && ((mem.at<uint16_t>(base + uint32_t(sy) * 8 + uint32_t(sx >> 7) * 2) >> (15 - ((sx >> 3) & 15))) & 1))
                    return true;
            return false;
        };
        // Light Gun Edition: everyone out (the arcade's own big CONTINUE? in the middle counts down for the whole
        // team, the seats' own countdowns wait): no second, frozen CONTINUE? at players 3 / 4's corners
        bool anyPlaying = false;
        for (int q = 0; q < rt.seat_count(); ++q)
            anyPlaying = anyPlaying || mem.at<Player>(rt.player_rec(q)).state == 5;
        if (pl.state == 4 && !anyPlaying) {
            // (the team's countdown is the big one): PRESS START BUTTON at the corner, blinking with players 1 / 2's
            // (user 2026-11-05: theirs showed, players 3 / 4 could continue too but had no prompt)
            const int sx0 = left ? 2 : 0x24, sy0 = 0x27 + dy;
            if (startOn && !pc && (!rt.pm_on() || rt.pm_can_continue()))
                pieces.push_back({sx0, sy0, 0x40, 0});
        } else if (pl.state == 4) {
            const int d = std::clamp(pl.continue_timer / 4096, 0, 9);
            const int x = left ? 2 : 0x24;
            if (!rt.pm_on() || rt.pm_can_continue())   // PC Mode: CONTINUE? only with a credit (0x50BC24), the digit always
                pieces.push_back({x, 0x27 + dy, 0x113, 0});
            pieces.push_back({x + 0x16, 0x27 + dy, uint32_t(mem.at<int16_t>(0xCA720 + uint32_t(d) * 2)), 0});
        } else if (pl.state == 9) {
            // an empty seat: PRESS START BUTTON (0x40, the arcade's free-play prompt) at its corner, blinking. PC models:
            // none, as players 1 / 2's there (the PC's hud_start_prompt is empty in a game; drawPc2D)
            // relplay (user 2026-10-30: P3 / P4's PRESS START BUTTON over the chapter's intro card): not where the window
            // layer shows, as players 1 / 2's (their prompt is under the card)
            const int sx0 = left ? 2 : 0x24, sy0 = 0x27 + dy;
            if (startOn && !pc && !windowed(sx0, sy0) && !windowed(sx0 + 8, sy0) && !windowed(sx0 + 16, sy0))
                pieces.push_back({sx0, sy0, 0x40, 0});
        } else if (const uint8_t sc = mem.at<uint8_t>(G::scene_state); sc == 4 && !inClear) {
            // a cut-scene (scene_state 4, as player_in_game_frame): HOLD YOUR FIRE! (0x107) at the seat's corner, blinking,
            // red as players 1 / 2's (the arcade's own colours, not the seat's; user 2026-10-20). In the top letterbox
            // bar as players 1 / 2's sits in the bottom one: rows 0..4 mirror their rows 0x2B..0x2F (row 1 reached a
            // few lines into the picture; user 2026-10-26: "too low ... never over cut-scenes")
            // Not where the window layer shows (a chapter's title card: players 1 / 2's prompt is hidden by the same
            // window mask, drawPc2D / Arcade2D::compose; user 2026-10-26: P3 / P4's showed over the cards)
            const int hx = left ? 1 : 0x29;
            if (blinkOn && !windowed(hx, 0) && !windowed(hx + 7, 0) && !windowed(hx + 14, 0))
                pieces.push_back({hx, 0, 0x107, 0});
        } else if (hudOn) {
            label = true;
            if (mem.u32(G::ammo_enabled) != 0 && !rt.lg_hide_bullets[seat])   // (Arsenal Mode: drawArsenalAmmo's)
                for (int i = 0; i < int(pl.rounds) && i < (rt.pm_on() ? 12 : 6); ++i)   // PC Mode: up to 12 (0x50B7E4)
                    pieces.push_back({left ? 2 + 2 * i : 0x3B - 2 * i, 0x26 + dy, 0x10, 0});
            // (GitHub #25: more than 5 lives: one torch and "x N" as players 1 / 2's, drawLivesCount)
            const int n = pl.lives > 5 ? 1 : std::min<int>(pl.lives, mem.at<uint8_t>(G::max_lives));
            if (pl.lives > 5) {
                pieces.push_back({left ? 10 : 0x2F, 0x2A + dy, 0x1AC, 0});
                pieces.push_back({left ? 12 : 0x31, 0x2A + dy, 0x181u + uint32_t(std::min<int32_t>(pl.lives, 9)), 0});
            }
            for (int i = 0; i < n; ++i) {
                pieces.push_back({left ? 4 * i + 6 : 0x34 - 4 * i, 0x2B + dy, 0x14, 1});
                const uint32_t step = (mem.u32(0xCA6F0 + uint32_t(i) * 4) + mem.u32(G::frame_counter)) / 6 % 7;
                pieces.push_back({left ? 4 * i + 7 : 0x35 - 4 * i, 0x2A + dy, step + 0xA8u, 1});
            }
        }
        // RELOAD! (0x36) blinking while the gun is empty, as hud_ammo_draw (empty +0x3E, lives, not in scene states 3 /
        // 5, shots on), after 120 frames with the region's "shoot outside the screen" line (0xCA700) under it: players
        // 1 / 2 have it in the middle rows (y 0x14 / 0x1B) of their halves; players 3 / 4 right under their band (y 0xA /
        // 0x11 without a boss life bar). When that would cover the same side's player 1 / 2 prompt (theirs shown:
        // the gun empty), it goes under that one instead (y 0x1F, above player 1 / 2's own band, no line).
        // The count is the HUD's own (frames drawn since the gun went empty), no game state
        {
            uint64_t& since = seatEmptySince_[seat - 2];
            const uint8_t scene = mem.at<uint8_t>(G::scene_state);
            auto prompt = [&](const Player& q) {
                return q.state == 5 && q.empty != 0 && q.lives != 0 && scene != 3 && scene != 5 && mem.u32(G::shot_enable) != 0;
            };
            if (prompt(pl)) {
                if (since == 0 || since > rt.frameCount + 1)
                    since = rt.frameCount + 1;
                const uint64_t t = rt.frameCount + 1 - since;
                if (t % 0x3C < 0x2D) {
                    const int rx = left ? 4 : 0x23;
                    // Light Gun Edition: always right under the seat's own band, RELOAD alone (players 1 / 2's sit
                    // right above theirs at the bottom now: hud_ammo_draw in the 4P mode), so nothing stacks
                    const int ry = 0x2F + dy;
                    const bool line = false;
                    (void)t;
                    pieces.push_back({rx, ry, 0x36, 0});
                    if (line)
                        pieces.push_back({rx, ry + 7, mem.u32(0xCA700 + rt.text_region() * 4), 0});
                }
            } else {
                since = 0;
            }
        }
        // a hit's notice for 60 frames: the arcade's two-player pop-up (hud_player_notice_show 0x945D0 with
        // players_in_game_b 2: sprite 0xCA5FC + kind x 8 + 8, place 0x94368 + kind x 16 + 16 plus the side's offset
        // 0x9441E + 4 / 5), player 3 as player 1's, player 4 as player 2's. It sat under the seat's band (row 8 on), and
        // the claw marks (up to 48 rows high) ran off the screen's bottom (user 2026-10-26: "blood ... gets cut off")
        if (const Runtime::SeatNotice& sn = rt.seatNotice[seat - 2]; sn.kind >= 0 && sn.kind < 16 && rt.frameCount < sn.until) {   // (kind 9: the claw marks)
            const uint32_t k = uint32_t(sn.kind);
            const uint32_t spr = mem.u32(0xCA5FC + k * 8 + 8);
            if (spr > 0 && spr < 0x400)
                pieces.push_back({mem.at<int8_t>(0x9441E + (left ? 4u : 5u)) + int32_t(mem.u32(0x94368 + k * 16 + 16)),
                                  int32_t(mem.u32(0x94368 + k * 16 + 20)), spr, 2});
        }
        seatRgba_.assign(size_t(W) * H * 4, 0);
        for (int kind = 0; kind < 3; ++kind) {
            SeatImg all;
            all.w = W;
            all.h = H;
            all.px.assign(size_t(W) * H * 4, 0);
            bool any = false;
            if (pc) {
                for (const Piece& c : pieces)
                    if (c.kind == kind && c.sprite < 0x276)
                        if (const int id = kArcadeToPcSprite[c.sprite]) {
                            pcSprites_.draw(all.px, id, c.x * 10, c.y * 10);
                            any = true;
                        }
            } else {
                static PrivLayer l;
                l.reset();
                for (const Piece& c : pieces)
                    if (c.kind == kind)
                        any = l.put(mem, c.sprite, c.x, c.y) || any;
                if (any)
                    any = assets::Arcade2D::compose(l.tiles.data(), l.chars.data(), l.pal.data(), true, all.px);
            }
            if (!any)
                continue;
            // the hit notice (kind 2) with the PC models: the PC's splash / claw marks are solid pictures, the arcade's
            // are stippled (every other pixel pen 0: the 3D shows through). Players 3 / 4's take the arcade's look, a
            // checkerboard on the PC picture (user 2026-10-27: player 4's was a solid yellow block); players 1 / 2's
            // stay the PC's own
            if (pc && kind == 2)
                for (int y = 0; y < H; ++y)
                    for (int x = (y & 1); x < W; x += 2)
                        all.px[(size_t(y) * W + x) * 4 + 3] = 0;
            if (kind > 0) {
                Recolour r = playerRecolour(seat);
                if (kind == 2)
                    r.select = Recolour::All;
                recolourRgba(all.px.data(), all.px.size() / 4, r);
            }
            paste(seatRgba_, W, H, all, 0, 0);
        }
        if (label) {   // "3P" / "4P" where players 1 / 2's "1P" / "2P" are (0xCA6D0: x 2 / 0x39)
            SeatImg& lab = seatLabel_[seat - 2][pc ? 1 : 0];
            if (lab.px.empty()) {
                const SeatImg p2 = spriteImg(mem, pc, pcSprites_, 0xF);   // player 2's "2P"
                const SeatImg dg = spriteImg(mem, pc, pcSprites_, uint32_t(mem.at<int16_t>(0xCA720 + uint32_t(seat + 1) * 2)));
                if (!p2.px.empty() && !dg.px.empty()) {
                    // the "P": player 2's label right of the first empty column in its middle third
                    int cut = p2.w / 2;
                    for (int x = p2.w / 3; x < p2.w * 2 / 3; ++x) {
                        bool empty = true;
                        for (int y = 0; y < p2.h && empty; ++y)
                            empty = p2.px[(size_t(y) * p2.w + x) * 4 + 3] == 0;
                        if (empty) {
                            cut = x;
                            break;
                        }
                    }
                    SeatImg pp;
                    pp.w = p2.w - cut;
                    pp.h = p2.h;
                    pp.px.assign(size_t(pp.w) * pp.h * 4, 0);
                    for (int y = 0; y < pp.h; ++y)
                        std::copy_n(&p2.px[(size_t(y) * p2.w + cut) * 4], size_t(pp.w) * 4, &pp.px[size_t(y) * pp.w * 4]);
                    // the countdown's digit (3 / 4) scaled to the label's height (nearest), then the "P"
                    const int dh = p2.h, dw = std::max(1, dg.w * dh / std::max(1, dg.h));
                    lab.w = dw + 1 + pp.w;
                    lab.h = dh;
                    lab.px.assign(size_t(lab.w) * lab.h * 4, 0);
                    for (int y = 0; y < dh; ++y)
                        for (int x = 0; x < dw; ++x)
                            std::copy_n(&dg.px[(size_t(y * dg.h / dh) * dg.w + size_t(x * dg.w / dw)) * 4], 4,
                                        &lab.px[(size_t(y) * lab.w + x) * 4]);
                    // the digit's shades from player 2's own "2": the countdown's digit art is pale (nearly white), so
                    // it came out white-green / white-yellow next to a coloured "P" (user 2026-10-20: "colors wrong,
                    // like the number 3 and 4"). Each of its texels takes the colour of the "2"'s texel of the same
                    // brightness rank (dark outline .. bright core), then all of it goes through the seat's ramp as the "P"
                    {
                        auto level = [](const uint8_t* c) { return c[0] * 299 + c[1] * 587 + c[2] * 114; };
                        std::vector<const uint8_t*> ref;
                        for (int y = 0; y < p2.h; ++y)
                            for (int x = 0; x < cut; ++x)
                                if (const uint8_t* c = &p2.px[(size_t(y) * p2.w + x) * 4]; c[3] > 0x80)
                                    ref.push_back(c);
                        std::vector<uint8_t*> mine;
                        for (int y = 0; y < lab.h; ++y)
                            for (int x = 0; x < dw; ++x)
                                if (uint8_t* c = &lab.px[(size_t(y) * lab.w + x) * 4]; c[3] != 0)
                                    mine.push_back(c);
                        if (!ref.empty() && !mine.empty()) {
                            std::stable_sort(ref.begin(), ref.end(), [&](const uint8_t* a, const uint8_t* b) { return level(a) < level(b); });
                            std::stable_sort(mine.begin(), mine.end(), [&](const uint8_t* a, const uint8_t* b) { return level(a) < level(b); });
                            for (size_t i = 0; i < mine.size(); ++i)
                                std::copy_n(ref[i * ref.size() / mine.size()], 3, mine[i]);
                        }
                    }
                    paste(lab.px, lab.w, lab.h, pp, dw + 1, 0);
                    Recolour r = playerRecolour(seat);
                    r.select = Recolour::All;
                    recolourRgba(lab.px.data(), lab.px.size() / 4, r);
                }
            }
            if (!lab.px.empty()) {
                const int x = left ? 2 * unit : (0x39 + 4) * unit - lab.w;   // its right edge about where "2P"'s is
                paste(seatRgba_, W, H, lab, x, (0x2A + dy) * unit);
            }
        }
        // widescreen with the corners at the window's edges (display2): players 3 / 4 go with players 1 / 2's sides
        const float x0 = anchorEdges ? leftEdge(W, H, ww, wh) : 0.0f;
        upload(seatRgba_, W, H, ww, wh, left ? -x0 : x0);
    }
    // player 2's hit notice (hud_player_notice_show: the blood splash / claw marks of kPopups 0x51F480, on the game's
    // layer 2) in player 2's colour, as players 3 / 4's are in theirs: the same sprite at the same cells drawn again
    // through a blue ramp over the red one (user 2026-10-26: blue only with more than 2 players; this HUD runs only in
    // the 3-4 player mode, 1-2 players keep the arcade's look)
    uint32_t spr = 0;
    int nx = 0, ny = 0;
    if (p2Notice(rt, spr, nx, ny)) {
        {
            SeatImg img;
            img.w = W;
            img.h = H;
            img.px.assign(size_t(W) * H * 4, 0);
            bool any = false;
            if (pc) {
                if (spr < 0x276)
                    if (const int id = kArcadeToPcSprite[spr]) {
                        pcSprites_.draw(img.px, id, nx * 10, ny * 10);
                        any = true;
                    }
            } else {
                static PrivLayer l;
                l.reset();
                any = l.put(mem, spr, nx, ny) && assets::Arcade2D::compose(l.tiles.data(), l.chars.data(), l.pal.data(), true, img.px);
            }
            if (any) {
                Recolour rc;
                rc.op = Recolour::Ramp;
                rc.select = Recolour::RedDominant;
                for (int k = 0; k < 5; ++k)
                    for (int c = 0; c < 3; ++c)
                        rc.ramp[k][c] = kPlayer2BloodRamp[k][c];
                recolourRgba(img.px.data(), img.px.size() / 4, rc);
                upload(img.px, W, H, ww, wh);
            }
        }
    }
}

// widescreen: the window outside the 4:3 screen (496 x 384, or the PC's 640 x 480, fitted to the height) cleared black
void Hud::blackSides(int ww, int wh, float grey, int sides)
{
    const int gw = int(std::lround(double(wh) * 496.0 / 384.0));
    if (gw >= ww)
        return;
    const int bar = (ww - gw) / 2;
    gl::Viewport(0, 0, ww, wh);
    gl::Enable(gl::SCISSOR_TEST);
    gl::ClearColor(grey, grey, grey, 1.0f);
    if (sides & 1) {
        gl::Scissor(0, 0, bar, wh);
        gl::Clear(gl::COLOR_BUFFER_BIT);
    }
    if (sides & 2) {
        gl::Scissor(ww - bar - ((ww - gw) & 1), 0, bar + ((ww - gw) & 1), wh);
        gl::Clear(gl::COLOR_BUFFER_BIT);
    }
    gl::Disable(gl::SCISSOR_TEST);
}

// credits43: the ending (mode 4 sub-mode 8) shows a full-height 2D card on the cover layer (0x1004000, 62 x 48 cells):
// its right column taken top to bottom, and either the staff roll (event 3: kind120_letterbox_open_frame's credit
// sheets beside the 3D window; the PC draws its own sheets there, not reaching the 4:3 edge) or a card whose 2D picture
// (rgba_, W x H, just drawn) is opaque at the screen's left and right edges (kind116_end_sprite's GAME OVER card). The
// rank cards fill the cover too but are see-through at the edges (the 3D shows around them on the arcade's screen).
// bugs1 (#34, user report: "the other half can extend to full widescreen, as on the M2 emulator"): the staff roll's
// sheets are the screen's right part (kind120_letterbox_open_frame narrows the 3D window to 8 .. 256), so only the right
// side is blacked (the sheet's black goes on to the edge); the 3D window's left part goes on to the window's left edge
int Hud::endingCard(const Runtime& rt, int W, int H) const
{
    port::Mem& mem = rt.mem;
    if (mem.at<int16_t>(G::game_mode) != 4 || mem.u32(G::game_submode) != 8 || !mem.mapped(0x1004000))
        return 0;
    for (int y : {0, 47})
        if (mem.at<uint16_t>(0x1004000u + uint32_t(y * 0x80 + 61 * 2)) == 0)
            return 0;
    if (mem.at<int8_t>(G::event_index) == 3)
        return std::getenv("HOTD_CREDITS_43") ? 3 : 2;   // (HOTD_CREDITS_43=1: both sides, as before)
    if (rgba_.size() != size_t(W) * H * 4)
        return 0;
    for (int x : {0, W - 1})
        for (int y : {H / 8, H / 2, H * 7 / 8})
            if (rgba_[(size_t(y) * W + x) * 4 + 3] < 128)
                return 0;
    return 3;
}

void Hud::draw(const Runtime& rt, int ww, int wh, const float aim[4][2], bool showDebug, const std::vector<std::string>& debug)
{
    const int scale = wh >= 720 && ww >= 1280 ? 3 : 2;
    // ch2video: widescreen: while the chapter card shows (kind177_chapter_card: evt_clrgfst_flags bit 0x20) the window
    // outside the 4:3 screen stays black, as in 4:3. The card covers the 496 x 384 screen only; Chapter 2's world is
    // already lit behind it (Chapter 1's is still black then), so its sides showed the 3D. UNVERIFIED (teaser)
    if (widescreen && rt.mem.mapped(G::evt_clrgfst_flags) && (rt.mem.u32(G::evt_clrgfst_flags) & 0x20u) != 0)
        blackSides(ww, wh);
    // sweep (2026-10-28): the chapter clear's results (Runtime::clear: the wall of plates and the researchers, 3D, made
    // for the 4:3 screen): in widescreen the set's other pieces showed beside it (Chapter 1: a railing at the left edge)
    else if (rt.clear.active)
        blackSides(ww, wh);
    // the black cover (chapter_screen_reset's sprite 0xA1 over the whole 62 x 48 cell screen on layer 2, while a scene's
    // textures load: the attract's cut from the title to the how-to-shoot demo, ~6 frames, longer while its world loads
    // on a slow PC): the arcade's screen is all black then. It covered only the 496 x 384 screen, the next scene's 3D
    // showed beside it (user 2026-10-26: "a black square ... right before the how-to-shoot tutorial", 16:9 and 4:3:
    // a 4:3 window is a little wider than the arcade's screen). The window's sides black too (hudfix4)
    // sweep (2026-10-28): the white cover (0x8D, the attract's cut to the sewer zombies at frame 7200, 5 frames) too:
    // it whitened the 4:3 middle only, the old scene showed beside it; the sides white then
    else if (const uint32_t cover = coverShown(rt))
        blackSides(ww, wh, cover == 0x8D && !std::getenv("HOTD_NO_WHITE") ? 1.0f : 0.0f);   // (HOTD_NO_WHITE: black sides)
    // title-2: kind 104's fade from white (palette_blend_white: white - (white - colour) x level / 32) as white over
    // the 3D picture, alpha (32 - level) / 32, under the 2D layer (MAME attract_fade snaps 0128-0132: the 3D and the
    // backdrop fade from white, PRESS START / FREE PLAY stay as they are)
    // flash: also the Magician's death fades (Runtime::paletteWhite: palette_fade_out_task / _in_task)
    static const bool noWhite = std::getenv("HOTD_NO_WHITE") != nullptr;   // (trailer footage: no white fades)
    const int whiteLevel = noWhite ? -1 : rt.whiteFade >= 0 ? rt.whiteFade : rt.paletteWhite();
    if (whiteLevel >= 0 && whiteLevel < 32 && !rt.pcModels) {   // PC models: the PC's own fade (pcFadeDraw)
        // the 496 x 384 screen's shape in original 4:3; widescreen: the window's shape, so the flash fills the whole
        // picture (it only whitened the 4:3 middle, user 2026-10-17)
        const int H = 24, W = widescreen ? std::max(31, int(float(H) * float(areaW(ww, wh)) / float(std::max(wh, 1)) + 0.5f)) : 31;
        rgba_.assign(size_t(W) * H * 4, 255);
        const uint8_t a = uint8_t((32 - whiteLevel) * 255 / 32);
        for (size_t i = 3; i < rgba_.size(); i += 4)
            rgba_[i] = a;
        upload(rgba_, W, H, ww, wh);
    }
    // the cut-scene bars (letterbox_task: 0..0x28 lines of the 384-line arcade screen, top and bottom)
    static const bool noBars = std::getenv("HOTD_NO_BARS") != nullptr;   // (trailer footage: no cut-scene bars)
    if (rt.letterbox > 0 && !noBars) {
        // the panel is 11 px a text row + 9 (render/overlay.cpp); was 8 a row, i.e. 60 of 40 lines (polish)
        const int rows = std::max(1, (rt.letterbox * wh / 384 - 9 + 5) / 11);
        if (rows != barRows_) {
            barRows_ = rows;
            bar_.setText(std::vector<std::string>(size_t(rows), std::string(size_t(4096 / 8), ' ')), 1);
        }
        if (barWidth > 0 && barWidth < float(ww)) {   // (Light Gun Edition: inside the game's picture, not over the bezel)
            const float bx = (float(ww) - barWidth) * 0.5f, bh = bar_.drawnHeight();
            bar_.drawIn(ww, wh, bx, 0.0f, barWidth, bh);
            bar_.drawIn(ww, wh, bx, float(wh) - bh, barWidth, bh);
        } else {
            bar_.drawAnchored(ww, wh, 0.0f, 0.0f, 0.0f, 0.0f);
            bar_.drawAnchored(ww, wh, 0.0f, float(wh), 0.0f, 1.0f);
        }
        // flash: the bars show 2D palette entry 0x92 (Runtime::backdrop555; black but in the Magician's death fades,
        // MAME fullgame_us t14360..t14440), through the tile layers' colour curve (brightness 0x40: about c x 255 / 31,
        // fullgame_us t14340's table) and MAME's output gamma; over the black panel, its height (11 px a row + 9)
        const uint16_t c = rt.backdrop555;
        if ((c & 0x7FFF) != 0 && !rt.pcModels) {
            uint8_t rgb[3];
            for (int k = 0; k < 3; ++k) {
                const int v = int((c >> (5 * k)) & 31) * 255 / 31;
                rgb[k] = uint8_t(std::max((v - 64) * 255 / 191, 0));
            }
            const int h = std::min(rows * 11 + 9, wh / 2);
            fillRect(rgb, ww, wh, 0, h);
            fillRect(rgb, ww, wh, wh - h, wh);
        }
    }
    if (rt.pcModels && !pcTried_)
        loadPcSprites();
    const bool paused = pauseSelection >= 0;
    const std::vector<std::string> subs = paused || noText ? std::vector<std::string>() : subLines(rt);   // lang
    subHide_ = !subs.empty();
    if (paused) {
        // the pause (0x418CC8): no 2D sprites, subtitle or chapter-clear lines
    }
    else if (rt.pcModels && pcSprites_.loaded()) {
        if (pcUnderFrame_ != (long long)rt.frameCount) {   // (not when drawUnder drew it under the 3D)
            drawPc2D(rt, ww, wh);
            if (const int sides = widescreen ? endingCard(rt, assets::PcSprites::kWidth, assets::PcSprites::kHeight) : 0)
                blackSides(ww, wh, 0.0f, sides);   // credits43: the ending's 4:3 cards (the 2D is inside the 4:3 screen)
        }
    }
    else if (rt.pcModels) {
        if (!clean)   // (Clean View / Trailer Mode: none)
            drawPlaceholder(rt, ww, wh, scale);
    }
    else {
        drawArcade2D(rt, ww, wh);
        if (const int sides = widescreen ? endingCard(rt, assets::Arcade2D::kWidth, assets::Arcade2D::kHeight) : 0)
            blackSides(ww, wh, 0.0f, sides);
    }
    if (!paused && !clean && rt.coop4_on())   // the 4P mode: players 3 / 4's HUD at the top corners
        drawSeatHud(rt, ww, wh);
    if (!paused && !clean && !rt.lgBanner.empty())   // Light Gun Edition: the mods on in this game
        drawModBanner(rt, ww, wh);
    if (!paused && !clean && rt.lgClock >= 0)   // Light Gun Edition: Coin-Op Survival's clock
        drawClock(rt, ww, wh);
    if (!paused && !clean && (rt.lgWeapon[0] >= 0 || rt.lgModes.shotgunShells > 0 || rt.lgMachineGun))   // Light Gun Edition: Arsenal / Shotgun Mode's rounds
        drawArsenalAmmo(rt, ww, wh);
    if (!paused && !clean && rt.mem.at<uint8_t>(G::max_lives) > 5)   // GitHub #25: "x N" beside the torch
        drawLivesCount(rt, ww, wh);
    if (!paused && !clean && rt.boss.shown)   // Boss Mode: the fight's clock (0x418CC8)
        drawBossClock(rt, ww, wh);
    if (!paused && !clean && rt.survival.active())   // Survival Mode: the wave line and the banner (game/survival.h)
        drawSurvival(rt, ww, wh, scale);
    // the chapter clear (kind 176): its lines appear as the original's sprites do (420, 360, 300, 240 frames left)
    if (rt.clear.active && rt.pcModels && !pcSprites_.loaded() && !paused && !clean) {   // the placeholder only: the sprites show it
        const int t = rt.clear.held ? -1 : rt.clear.timer;
        char buf[64];
        std::vector<std::string> lines = {"CHAPTER " + std::to_string(rt.mem.at<int8_t>(G::chapter) + 1) + " CLEAR", ""};
        for (int p = 0; p < 2; ++p)
            if (rt.mem.at<Player>(G::players, uint32_t(p)).state == 5) {
                std::snprintf(buf, sizeof buf, "%dP  %07d", p + 1, std::max(rt.mem.at<Player>(G::players, uint32_t(p)).score, 0));
                lines.push_back(buf);
            }
        lines.push_back("");
        if (t <= 0x1A4) {
            std::snprintf(buf, sizeof buf, "HOSTAGES RESCUED  %s", t <= 0x168 ? std::to_string(rt.clear.rescued).c_str() : "");
            lines.push_back(buf);
        }
        if (t <= 300) {
            std::snprintf(buf, sizeof buf, "BONUS  %s", t <= 0xF0 ? ("+" + std::to_string(rt.clear.bonus) + " LIFE").c_str() : "");
            lines.push_back(buf);
        }
        clear_.setText(lines, scale);
        clear_.drawAt(ww, wh, ww * 0.5f, wh * 0.45f);
    }
    pcSprites_.setBrown(false);
    if (!subs.empty())
        drawSubtitles(subs, ww, wh);
    subHide_ = false;
    if (rt.pcModels)
        pcFadeDraw(rt.pcFadeLevel(), ww, wh);
    if (paused)
        drawPause(pauseSelection, ww, wh);
    if (showDebug) {
        debug_.setText(debug, wh >= 720 ? 2 : 1);
        debug_.draw(ww, wh);
    }
    if (!deferSights)
        drawSights(rt, ww, wh, aim);
}

// the sights (split from draw for the CRT filter: crisp sights are drawn over the filtered picture)
void Hud::drawSights(const Runtime& rt, int ww, int wh, const float aim[4][2])
{
    const bool paused = pauseSelection >= 0;
    const int16_t mode = rt.mem.at<int16_t>(G::game_mode);
    // the sight: auto = the model source's (PC: its sprites; arcade: the arcade-style one); the PC's sprites in the
    // arcade mode only when chosen (and loaded); the text cross only if no sight could be built (per player: the
    // firing range's own settings, buildSeatSight)
    // the ending (sub-modes 7 / 8: the farewell scene and the staff roll): no sights at all. game_ending_start put
    // players 1 / 2 to state 7 (theirs went); players 3 / 4 stay 5, so their crosshairs stayed over the credits
    // (hudfix4, user 2026-10-26)
    const uint32_t sub = rt.mem.u32(G::game_submode);
    const bool ending = mode == 4 && (sub == 7 || sub == 8);
    for (int p = 0; p < rt.seat_count(); ++p) {   // 4P: players 3 / 4 too (their records in the 4P block)
        drewSight[p] = false;
        const int32_t st = rt.mem.at<Player>(rt.player_rec(p)).state;
        if (pauseSight && paused && p == 0 && !clean) {   // Light Gun Edition: the pause menu's pointer
            drewSight[p] = true;
            if (buildSeatSight(p, rt.pcModels, wh))
                drawSight(p, ww, wh, aim[p][0], aim[p][1]);
            else
                cross_[p].drawAt(ww, wh, aim[p][0], aim[p][1]);
        } else if (drawCross[p] && !paused && !clean && !ending && (st == 5 || st == 8) && mode == 4) {
            drewSight[p] = true;
            if (!showSights || game::sightStyleKept(sights[p].style) == game::kSightOff)
                continue;   // gunfb: no crosshair (Show Crosshairs off, or the player's Crosshair Off): nothing drawn
            // rainbow2: Rainbow Blood's HUD & Crosshair (Crosshair / Both): the sight cycles through the rainbow, each
            // player half a cycle (P2) or a quarter (P3 / P4) on, also with Colours: Together, so they stay apart
            static const float kSightPhase[4] = {0.0f, 0.5f, 0.25f, 0.75f};
            sightTintOn_[p] = (rainbowBloodSettings().hud & 1) && rainbowColour(rt, kSightPhase[p], sightTint_[p], true);
            if (buildSeatSight(p, rt.pcModels, wh))
                drawSight(p, ww, wh, aim[p][0], aim[p][1]);
            else
                cross_[p].drawAt(ww, wh, aim[p][0], aim[p][1]);
            if (cpu[p])   // a computer teammate: a small CPU tag under its sight
                cpuTag_[p].drawAnchored(ww, wh, aim[p][0], aim[p][1] + float(wh) / 30.0f, 0.5f, 0.0f);
        }
    }
}


// Light Gun Edition: Arsenal Mode: for each playing player holding the shotgun, the machine gun or the rocket launcher,
// up to 3 of the game's bullets (0x10) in the weapon's colour where the bullets are (the game's own are hidden:
// lg_hide_bullets) and the rounds left after them in the players' label digits (the countdown's digits at the "1P" /
// "2P" label's height, in that label's shades; players 3 / 4 in theirs through their colour ramp)
void Hud::drawArsenalAmmo(const Runtime& rt, int ww, int wh)
{
    port::Mem& mem = rt.mem;
    if (mem.at<int16_t>(G::game_mode) != 4 || mem.at<uint8_t>(G::scene_state) != 2 || rt.clear.active)
        return;
    const bool pc = rt.pcModels && pcSprites_.loaded();
    if (!pc && (rt.pcModels || !mem.mapped(kTileRam) || !mem.mapped(0x2000000)))
        return;
    const int unit = pc ? 10 : 8;
    const int W = pc ? assets::PcSprites::kWidth : assets::Arcade2D::kWidth;
    const int H = pc ? assets::PcSprites::kHeight : assets::Arcade2D::kHeight;
    const int src = pc ? 1 : 0;
    // the bullets: the game's own picture, its brightness through the weapon's colour
    static const uint8_t kTint[4][3] = {{255, 255, 255}, {215, 45, 35}, {205, 210, 220}, {70, 210, 70}};
    for (int w = 1; w < 4; ++w) {
        SeatImg& b = arsenalBullet_[w][src];
        if (!b.px.empty()) continue;
        b = spriteImg(mem, pc, pcSprites_, 0x10);
        for (size_t i = 0; i + 3 < b.px.size(); i += 4) {
            const float l = std::max({b.px[i], b.px[i + 1], b.px[i + 2]}) / 255.0f;
            for (int c = 0; c < 3; ++c)
                b.px[i + c] = uint8_t(std::min(255.0f, kTint[w][c] * (0.25f + 0.95f * l)));
        }
    }
    auto digit = [&](int seat, int d) -> const SeatImg& {
        SeatImg& out = arsenalDigit_[seat][d][src];
        if (!out.px.empty()) return out;
        const int labSeat = seat == 0 ? 0 : 1;   // (players 2-4: player 2's label; 3 / 4 then through their ramp)
        const SeatImg lab = spriteImg(mem, pc, pcSprites_, mem.u32(0xCA6D0 + uint32_t(labSeat) * 12 + 8));
        const SeatImg dg = spriteImg(mem, pc, pcSprites_, uint32_t(mem.at<int16_t>(0xCA720 + uint32_t(d) * 2)));
        if (lab.px.empty() || dg.px.empty()) return out;
        const int dh = lab.h, dw = std::max(1, dg.w * dh / std::max(1, dg.h));
        out.w = dw, out.h = dh;
        out.px.assign(size_t(dw) * dh * 4, 0);
        for (int y = 0; y < dh; ++y)
            for (int x = 0; x < dw; ++x)
                std::copy_n(&dg.px[(size_t(y * dg.h / dh) * dg.w + size_t(x * dg.w / dw)) * 4], 4, &out.px[(size_t(y) * dw + x) * 4]);
        // the label's own digit (its left half) gives the shades, darkest to brightest by rank
        auto level = [](const uint8_t* c) { return c[0] * 299 + c[1] * 587 + c[2] * 114; };
        std::vector<const uint8_t*> ref;
        for (int y = 0; y < lab.h; ++y)
            for (int x = 0; x < lab.w / 2; ++x)
                if (const uint8_t* c = &lab.px[(size_t(y) * lab.w + x) * 4]; c[3] > 0x80) ref.push_back(c);
        std::vector<uint8_t*> mine;
        for (size_t i = 0; i + 3 < out.px.size(); i += 4)
            if (out.px[i + 3]) mine.push_back(&out.px[i]);
        if (!ref.empty() && !mine.empty()) {
            std::stable_sort(ref.begin(), ref.end(), [&](const uint8_t* a, const uint8_t* b) { return level(a) < level(b); });
            std::stable_sort(mine.begin(), mine.end(), [&](const uint8_t* a, const uint8_t* b) { return level(a) < level(b); });
            for (size_t i = 0; i < mine.size(); ++i)
                std::copy_n(ref[i * ref.size() / mine.size()], 3, mine[i]);
        }
        if (seat >= 2) {
            Recolour r = playerRecolour(seat);
            r.select = Recolour::All;
            recolourRgba(out.px.data(), out.px.size() / 4, r);
        }
        return out;
    };
    const float x0 = anchorEdges ? leftEdge(W, H, ww, wh) : 0.0f;
    for (int side = 0; side < 2; ++side) {   // the left seats (1, 3) and the right ones (2, 4): each side to its edge
        std::vector<uint8_t> canvas;
        for (int seat = side; seat < rt.seat_count(); seat += 2) {
            // (Shotgun Mode / Machine Gun mode: drawn as Arsenal's shotgun / machine gun)
            const int w = rt.lgWeapon[seat] >= 0 ? rt.lgWeapon[seat] : rt.lgModes.shotgunShells > 0 ? 1 : rt.lgMachineGun ? 2 : -1;
            const port::a::Player& pl = mem.at<port::a::Player>(rt.player_rec(seat));
            if (w <= 0 || pl.state != 5 || mem.u32(G::ammo_enabled) == 0) continue;
            if (canvas.empty()) canvas.assign(size_t(W) * H * 4, 0);
            const bool left = side == 0;
            const int dy = seat >= 2 ? seatDy_[seat - 2] : 0, row = 0x26 + dy;
            const SeatImg& b = arsenalBullet_[w][src];
            const int n = std::min<int>(pl.rounds, 3);
            for (int i = 0; i < n; ++i)
                paste(canvas, W, H, b, (left ? 2 + 2 * i : 0x3B - 2 * i) * unit, row * unit);
            // the count after the three bullets' places (unlimited ammo, Free Fire / Machine Gun mode: none)
            if (rt.lgModes.freeFire) continue;
            const std::string t = std::to_string(int(pl.rounds));
            int tw = 0;
            for (char ch : t) tw += digit(seat, ch - '0').w + 1;
            int x = left ? (2 + 2 * 3) * unit + unit / 2 : (0x3B - 2 * 3 + 1) * unit - unit / 2 - tw;
            for (char ch : t) {
                const SeatImg& g = digit(seat, ch - '0');
                paste(canvas, W, H, g, x, row * unit - (g.h - 2 * unit) / 2);
                x += g.w + 1;
            }
        }
        if (!canvas.empty())
            upload(canvas, W, H, ww, wh, side == 0 ? -x0 : x0);
    }
}

bool Hud::vrSight(const Runtime& rt, int seat, int wh, unsigned& tex, float& aspect)
{
    const int16_t mode = rt.mem.at<int16_t>(G::game_mode);
    const uint32_t sub = rt.mem.u32(G::game_submode);
    const bool ending = mode == 4 && (sub == 7 || sub == 8);
    if (seat < 0 || seat >= rt.seat_count() || pauseSelection >= 0 || clean || ending || mode != 4)
        return false;
    if (!showSights || game::sightStyleKept(sights[seat].style) == game::kSightOff)
        return false;   // gunfb: no crosshair
    const int32_t st = rt.mem.at<Player>(rt.player_rec(seat)).state;
    if ((st != 5 && st != 8) || !buildSeatSight(seat, rt.pcModels, wh))
        return false;
    tex = seatTex_[seat];
    aspect = seatSprite_[seat] ? float(seatW_[seat]) / float(std::max(1, seatH_[seat]))
                               : float(seatDw_[seat]) / float(std::max(1, seatDh_[seat]));
    return true;
}

} // namespace hotd::game

// ---- blood2: a Remake menu screen composed on the GPU (Frontend::gpuLayers) ----
namespace hotd::game {
namespace {

// redblocks (user 2026-11-04: the Remake main menu under big flat dark-red squares on the layers' 32 px tiles, the
// widgets gone, the hand a box): the Remake passes' own vertex shader, the whole quad's texture (0..1, v down).
// They shared the 2D layer's kVs, whose uUV (wide2d: the texture's range) they never set: every texel read at
// (0, 0) (the splatter's corner: its dark red over each tile it has), and the backdrop's uUV read as that range
const char* kRemakeVs = R"(#version 330 core
uniform vec4 uRect;   // x, y, w, h in normalized device coordinates (y up)
out vec2 vUV;
void main() {
    vec2 c = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    vUV = vec2(c.x, 1.0 - c.y);
    gl_Position = vec4(uRect.xy + c * uRect.zw, 0.0, 1.0);
}
)";
// the backdrop: the source's part across the screen, grey and dark, tinted (c * mul + add), the vignette (as
// Frontend::rBackground's CPU loop, per window pixel)
const char* kRemakeBgFs = R"(#version 330 core
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uTex;
uniform vec4 uUV;
uniform float uGrey;   // 0 RGBA, 1 grey (its red channel), 2 none (the dark fill)
uniform vec3 uMul, uAdd;
uniform float uFade;   // the main menu idle (0 .. 1): lighter, a gentler vignette
uniform float uAlpha;  // remakefix: drawn over the picture it fades from (1: opaque)
void main() {
    vec4 t = texture(uTex, mix(uUV.xy, uUV.zw, vUV));
    vec3 c = uGrey > 1.5 ? vec3(20.0, 16.0, 16.0) : (uGrey > 0.5 ? vec3(t.r * 204.0) : t.rgb * 255.0);
    float l = dot(c, vec3(0.3, 0.59, 0.11));
    c = (l * 0.8 + c * 0.2) * mix(0.62, 0.85, uFade) * uMul + uAdd;
    vec2 v = vUV * 2.0 - 1.0;
    float vig = 1.0 - mix(0.6, 0.3, uFade) * pow(clamp((sqrt(v.x * v.x * 0.8 + v.y * v.y) - 0.35) / 0.85, 0.0, 1.0), 1.4);
    // (regallery: dithered, no rings in the dark. compat: an integer hash; the sin() hash's large arguments are
    // imprecise on some GPUs (patterns instead of noise))
    uvec2 q = uvec2(gl_FragCoord.xy);
    uint h = q.x * 1664525u + q.y * 1013904223u;
    h ^= h >> 16u; h *= 0x7feb352du; h ^= h >> 15u; h *= 0x846ca68bu; h ^= h >> 16u;
    float dz = float(h & 0xFFFFu) / 65536.0 - 0.5;
    oColor = vec4(clamp((c * vig + dz) / 255.0, 0.0, 1.0), uAlpha);
}
)";
// a premultiplied layer at an alpha (uShadow 1: black in its shape, the hand's shadow)
const char* kRemakeFgFs = R"(#version 330 core
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uTex;
uniform float uAlpha;
uniform float uShadow;
void main() {
    vec4 t = texture(uTex, vUV);
    oColor = mix(t, vec4(0.0, 0.0, 0.0, t.a), uShadow) * uAlpha;
}
)";

gl::GLuint linkRemake(const char* fs, std::string& err)
{
    const gl::GLuint v = compile(gl::VERTEX_SHADER, kRemakeVs, err), f = v ? compile(gl::FRAGMENT_SHADER, fs, err) : 0;
    if (!v || !f)
        return 0;
    const gl::GLuint p = gl::CreateProgram();
    gl::AttachShader(p, v);
    gl::AttachShader(p, f);
    gl::LinkProgram(p);
    gl::programLog(p, "hud");
    gl::DeleteShader(v);
    gl::DeleteShader(f);
    gl::GLint ok = 0;
    gl::GetProgramiv(p, gl::LINK_STATUS, &ok);
    if (!ok)
        return 0;
    gl::UseProgram(p);
    gl::Uniform1i(gl::GetUniformLocation(p, "uTex"), 0);
    return p;
}

// RGBA (straight alpha; an alpha above 0 counted as 255 when binary) -> premultiplied, for linear filtering without
// dark fringes
void premultiply(const std::vector<uint8_t>& in, size_t n, std::vector<uint8_t>& out, bool binary)
{
    out.resize(n * 4);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* s = &in[i * 4];
        uint8_t* d = &out[i * 4];
        const uint32_t a = binary ? (s[3] ? 255u : 0u) : s[3];
        if (a == 255)
            d[0] = s[0], d[1] = s[1], d[2] = s[2], d[3] = 255;
        else if (a == 0)
            d[0] = d[1] = d[2] = d[3] = 0;
        else
            d[0] = uint8_t((s[0] * a + 127) / 255), d[1] = uint8_t((s[1] * a + 127) / 255), d[2] = uint8_t((s[2] * a + 127) / 255), d[3] = uint8_t(a);
    }
}

// misc18: a layer's tiles with any pixel (alpha above 0; premultiplied, the others are all 0 and blend to nothing):
// the tiles meeting (x0, y0)-(x1, y1) looked at again (all of them when the size changed)
constexpr int kTile = 32;
void maskUpdate(Hud::TileMask& m, const std::vector<uint8_t>& rgba, int w, int h, int x0, int y0, int x1, int y1)
{
    if (m.w != w || m.h != h) {
        m.w = w, m.h = h, m.cols = (w + kTile - 1) / kTile, m.rows = (h + kTile - 1) / kTile;
        m.on.assign(size_t(m.cols) * size_t(m.rows), 0);
        x0 = y0 = 0, x1 = w, y1 = h;
    }
    const int c0 = std::max(0, x0 / kTile), c1 = std::min(m.cols, (x1 + kTile - 1) / kTile);
    const int r0 = std::max(0, y0 / kTile), r1 = std::min(m.rows, (y1 + kTile - 1) / kTile);
    for (int r = r0; r < r1; ++r)
        for (int c = c0; c < c1; ++c) {
            bool any = false;
            const int ye = std::min(h, (r + 1) * kTile), xe = std::min(w, (c + 1) * kTile);
            for (int y = r * kTile; y < ye && !any; ++y) {
                const uint8_t* p = &rgba[(size_t(y) * size_t(w) + size_t(c * kTile)) * 4 + 3];
                for (int x = c * kTile; x < xe; ++x, p += 4)
                    if (*p) { any = true; break; }
            }
            m.on[size_t(r) * size_t(m.cols) + size_t(c)] = any;
        }
}

// misc18: the runs of tiles on (a tile and its neighbours within dx / dy tiles: the filter's reach) on each tile row,
// a run merged with the same run on the rows below; c0, r0, c1, r1 each
void maskRects(const Hud::TileMask& m, int dx, int dy, std::vector<int>& rects)
{
    std::vector<uint8_t> grow(m.on.size(), 0);
    for (int r = 0; r < m.rows; ++r)
        for (int c = 0; c < m.cols; ++c)
            if (m.on[size_t(r) * size_t(m.cols) + size_t(c)])
                for (int y = std::max(0, r - dy); y <= std::min(m.rows - 1, r + dy); ++y)
                    for (int x = std::max(0, c - dx); x <= std::min(m.cols - 1, c + dx); ++x)
                        grow[size_t(y) * size_t(m.cols) + size_t(x)] = 1;
    rects.clear();
    std::vector<int> open, now;   // (the rects of the row above, still open: their index / 4 in rects)
    for (int r = 0; r < m.rows; ++r) {
        now.clear();
        for (int c = 0; c < m.cols;) {
            if (!grow[size_t(r) * size_t(m.cols) + size_t(c)]) { ++c; continue; }
            int e = c;
            while (e < m.cols && grow[size_t(r) * size_t(m.cols) + size_t(e)]) ++e;
            int hit = -1;
            for (int i : open)
                if (rects[size_t(i) * 4] == c && rects[size_t(i) * 4 + 2] == e) hit = i;
            if (hit >= 0) rects[size_t(hit) * 4 + 3] = r + 1, now.push_back(hit);
            else {
                now.push_back(int(rects.size() / 4));
                rects.insert(rects.end(), {c, r, e, r + 1});
            }
            c = e;
        }
        std::swap(open, now);
    }
}

void texParams(bool mip)
{
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, mip ? gl::LINEAR_MIPMAP_LINEAR : gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
}

} // namespace

void Hud::drawRemake(const Frontend::RemakeLayers& L, int ww, int wh)
{
    // menublocks (user 2026-11-04: the main menu's live background under big flat dark-red squares on the layers' 32 px
    // tiles, the hand a dark box, the layers faded out (the idle), right after the settings pictures were made in the
    // background; not reproduced here): the layers drawn opaque, as by another program. Our GL objects checked each
    // frame; one gone (another part's clean-up deleting a name it no longer owned) -> all of them made again, the
    // layers uploaded again
    if (bgProg_ && fgProg_ && (!gl::IsProgram(bgProg_) || !gl::IsProgram(fgProg_) || !gl::IsVertexArray(vao_) || !gl::IsTexture(bgTex_) ||
                    (fgGen_ != ~0ull && !gl::IsTexture(fgTex_)) || (splatGen_ && !gl::IsTexture(splatTex_[splatCur_])))) {
        std::fprintf(stderr, "remake menu: its GL objects were gone (programs %d %d, vao %d): made again\n",
                     int(gl::IsProgram(bgProg_)), int(gl::IsProgram(fgProg_)), int(gl::IsVertexArray(vao_)));
        for (unsigned* p : {&bgProg_, &fgProg_})
            if (gl::IsProgram(*p)) gl::DeleteProgram(*p);
        if (!gl::IsVertexArray(vao_))
            gl::GenVertexArrays(1, &vao_);
        bgProg_ = fgProg_ = 0;
        bgGen_ = fgGen_ = partsGen_ = animGen_ = ~0ull;
        bgSrc_ = nullptr;
        bgW_ = bgH_ = fgW_ = fgH_ = 0;
        splatGen_ = 0;
        splatOld_ = false;
        handId_ = -1;
        partTex_.clear();
        animTex_.clear();
    }
    if (!bgProg_) {
        std::string err;
        bgProg_ = linkRemake(kRemakeBgFs, err);
        fgProg_ = linkRemake(kRemakeFgFs, err);
        if (!bgProg_ || !fgProg_) {
            std::fprintf(stderr, "remake menu shaders: %s\n", err.c_str());
            return;
        }
        uBgRect_ = gl::GetUniformLocation(bgProg_, "uRect"), uBgUV_ = gl::GetUniformLocation(bgProg_, "uUV");
        uBgGrey_ = gl::GetUniformLocation(bgProg_, "uGrey"), uBgMul_ = gl::GetUniformLocation(bgProg_, "uMul");
        uBgAdd_ = gl::GetUniformLocation(bgProg_, "uAdd"), uBgFade_ = gl::GetUniformLocation(bgProg_, "uFade");
        uBgAlpha_ = gl::GetUniformLocation(bgProg_, "uAlpha");
        uFgRect_ = gl::GetUniformLocation(fgProg_, "uRect"), uFgAlpha_ = gl::GetUniformLocation(fgProg_, "uAlpha");
        uFgShadow_ = gl::GetUniformLocation(fgProg_, "uShadow");
        gl::GenTextures(1, &bgTex_);
        // compat (2026-10-29, NVIDIA's debug output: "texture object .. does not have a defined base level", every frame
        // of the dark fill while the live attract loads): a 1x1 black picture until a source is uploaded (the dark fill
        // and the cross-fade bind bgTex_ without one)
        {
            static const uint8_t black[4] = {0, 0, 0, 255};
            gl::BindTexture(gl::TEXTURE_2D, bgTex_);
            gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
            gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, 1, 1, 0, gl::RGBA, gl::UNSIGNED_BYTE, black);
            texParams(false);
        }
        gl::GenTextures(1, &fgTex_);
        gl::GenTextures(2, splatTex_);
        gl::GenTextures(1, &handTex_);
    }
    float s, x0, y0;
    pictureRect(L.unitsW, Frontend::kHeight, float(ww), float(wh), s, x0, y0);
    // a window rectangle (y down) as uRect (normalized device coordinates, y up)
    auto rect = [&](int loc, float x, float y, float w, float h) {
        gl::Uniform4f(loc, -1.0f + 2.0f * x / float(ww), 1.0f - 2.0f * (y + h) / float(wh), 2.0f * w / float(ww), 2.0f * h / float(wh));
    };
    const float pw = float(L.unitsW) * s, ph = float(Frontend::kHeight) * s;
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::ActiveTexture(gl::TEXTURE0);
    // the backdrop: its source uploaded when it changes (the hall once); liveattract: the live attract's texture as it is
    gl::BindTexture(gl::TEXTURE_2D, L.tex ? L.tex : bgTex_);
    if (!L.tex && L.src && (L.srcGen != bgGen_ || L.src != bgSrc_ || L.sw != bgW_ || L.sh != bgH_)) {
        bgGen_ = L.srcGen, bgSrc_ = L.src, bgW_ = L.sw, bgH_ = L.sh;
        if (L.grey)
            gl::TexImage2D(gl::TEXTURE_2D, 0, gl::R8, L.sw, L.sh, 0, gl::RED, gl::UNSIGNED_BYTE, L.src);
        else
            gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, L.sw, L.sh, 0, gl::RGBA, gl::UNSIGNED_BYTE, L.src);
        texParams(false);
        remakeUploadBytes += size_t(L.sw) * size_t(L.sh) * (L.grey ? 1 : 4);
    }
    gl::Viewport(0, 0, ww, wh);
    gl::Disable(gl::DEPTH_TEST);
    gl::Disable(gl::BLEND);
    // menublocks: every other state these passes rely on set here too (none left over from the settings pictures' or
    // the live attract's renders): no scissor / stencil / culling, all channels written, the blend adding, unit 0's
    // own sampling (no sampler object)
    gl::Disable(gl::SCISSOR_TEST);
    gl::Disable(gl::STENCIL_TEST);
    gl::Disable(gl::CULL_FACE);
    gl::ColorMask(gl::TRUE_, gl::TRUE_, gl::TRUE_, gl::TRUE_);
    gl::BlendEquation(gl::FUNC_ADD);
    gl::BindSampler(0, 0);
    gl::UseProgram(bgProg_);
    rect(uBgRect_, x0, y0, pw, ph);
    gl::Uniform3f(uBgMul_, L.mul[0], L.mul[1], L.mul[2]);
    gl::Uniform3f(uBgAdd_, L.add[0], L.add[1], L.add[2]);
    gl::Uniform1f(uBgFade_, L.fade);
    gl::BindVertexArray(vao_);
    // remakefix: a cross-fade (the live attract's scene cut, its first picture over the still / the dark fill): the
    // picture faded from first, opaque, then the backdrop over it at mix
    const bool fading = L.mix < 1.0f && (L.tex || L.fill);
    if (fading) {
        gl::BindTexture(gl::TEXTURE_2D, L.tex2 ? L.tex2 : bgTex_);
        gl::Uniform4f(uBgUV_, L.uv2[0], L.uv2[1], L.uv2[2], L.uv2[3]);
        gl::Uniform1f(uBgGrey_, L.tex2 ? 0.0f : 2.0f);
        gl::Uniform1f(uBgAlpha_, 1.0f);
        gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
        gl::Enable(gl::BLEND);
        gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
        gl::BindTexture(gl::TEXTURE_2D, L.tex ? L.tex : bgTex_);
    }
    gl::Uniform4f(uBgUV_, L.u0, L.v0, L.u1, L.v1);
    gl::Uniform1f(uBgGrey_, L.tex ? 0.0f : !L.src ? 2.0f : L.grey ? 1.0f : 0.0f);
    gl::Uniform1f(uBgAlpha_, fading ? L.mix : 1.0f);
    const float show = 1.0f - L.fade;   // (the main menu idle: the layers over the backdrop fade out)
    if (!L.noBackdrop)   // (regallery: the Gallery's own stage is drawn under the layers)
        gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
    gl::Disable(gl::BLEND);
    auto upload = [&](unsigned tex, const std::vector<uint8_t>& rgba, int w, int h) {
        premultiply(rgba, size_t(w) * size_t(h), premul_, false);
        gl::BindTexture(gl::TEXTURE_2D, tex);
        gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, premul_.data());
        gl::GenerateMipmap(gl::TEXTURE_2D);   // (a window smaller than the layer: smooth, not shimmering)
        texParams(true);
        remakeUploadBytes += size_t(w) * size_t(h) * 4;
    };
    const double now = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    // the splatter: a new one (pattern, colour, size) into the other texture, the old one kept to fade out
    if (L.splat && L.splatGen != splatGen_ && L.splat->size() >= size_t(L.spw) * size_t(L.sph) * 4) {
        splatOld_ = splatGen_ != 0;
        splatCur_ ^= 1;
        splatGen_ = L.splatGen;
        splatFrom_ = now;
        upload(splatTex_[splatCur_], *L.splat, L.spw, L.sph);
        splatMask_[splatCur_].w = -1;   // (misc18: all of it looked at)
        maskUpdate(splatMask_[splatCur_], *L.splat, L.spw, L.sph, 0, 0, L.spw, L.sph);
    }
    // the widgets' layer: uploaded when redrawn
    if (L.fg && L.fgGen != fgGen_ && L.fg->size() >= size_t(L.fw) * size_t(L.fh) * 4) {
        // leftovers: a part redrawn on the layer this texture has (a settings list scrolled, the Jukebox's page
        // turned): only that part uploaded, not the whole screen
        const int* d = L.fgDirty;
        const int dx0 = std::clamp(d[0], 0, L.fw), dy0 = std::clamp(d[1], 0, L.fh);
        const int dx1 = std::clamp(d[2], 0, L.fw), dy1 = std::clamp(d[3], 0, L.fh);
        if (fgGen_ != ~0ull && L.fgGen == fgGen_ + 1 && fgW_ == L.fw && fgH_ == L.fh && dx1 > dx0 && dy1 > dy0) {
            const int sw = dx1 - dx0, sh = dy1 - dy0;
            std::vector<uint8_t> sub(size_t(sw) * size_t(sh) * 4);   // (the part's rows, then premultiplied)
            for (int y = 0; y < sh; ++y)
                std::memcpy(&sub[size_t(y) * size_t(sw) * 4], &(*L.fg)[(size_t(dy0 + y) * size_t(L.fw) + size_t(dx0)) * 4], size_t(sw) * 4);
            premultiply(sub, size_t(sw) * size_t(sh), premul_, false);
            gl::BindTexture(gl::TEXTURE_2D, fgTex_);
            gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
            gl::TexSubImage2D(gl::TEXTURE_2D, 0, dx0, dy0, sw, sh, gl::RGBA, gl::UNSIGNED_BYTE, premul_.data());
            gl::GenerateMipmap(gl::TEXTURE_2D);
            remakeUploadBytes += premul_.size();
            maskUpdate(fgMask_, *L.fg, L.fw, L.fh, dx0, dy0, dx1, dy1);
        } else {
            upload(fgTex_, *L.fg, L.fw, L.fh);
            fgW_ = L.fw, fgH_ = L.fh;
            fgMask_.w = -1;
            maskUpdate(fgMask_, *L.fg, L.fw, L.fh, 0, 0, L.fw, L.fh);
        }
        fgGen_ = L.fgGen;
    }
    if (!L.noBackdrop)
        remakeFillPixels += uint64_t(pw * ph) * (fading ? 2u : 1u);
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::ONE, gl::ONE_MINUS_SRC_ALPHA);   // (premultiplied)
    gl::UseProgram(fgProg_);
    gl::Uniform1f(uFgShadow_, 0.0f);   // (menublocks: not the hand's shadow, whatever was left)
    rect(uFgRect_, x0, y0, pw, ph);
    // misc18: a whole-picture layer drawn scissored to its tiles with pixels and their neighbours within the filter's
    // reach (2 texels magnified, more for the mip levels when smaller); the scissors cut the window along the same tile
    // edges, so they never overlap (a pixel blended twice) nor leave a gap; the same quad: the same bytes
    std::vector<int> rects;
    auto drawMasked = [&](const TileMask& m) {
        if (m.w <= 0 || m.h <= 0 || m.on.empty() || fullLayers) {
            gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
            remakeFillPixels += uint64_t(pw * ph);
            return;
        }
        const float kx = pw / float(m.w), ky = ph / float(m.h);
        const float reachX = kx >= 1.0f ? 2.0f : 2.0f + 4.0f / kx, reachY = ky >= 1.0f ? 2.0f : 2.0f + 4.0f / ky;
        maskRects(m, int(std::ceil(reachX / float(kTile))), int(std::ceil(reachY / float(kTile))), rects);
        auto edgeX = [&](int c) { return c <= 0 ? 0 : c >= m.cols ? ww : std::clamp(int(std::lround(x0 + float(c * kTile) * kx)), 0, ww); };
        auto edgeY = [&](int r) { return r <= 0 ? 0 : r >= m.rows ? wh : std::clamp(int(std::lround(y0 + float(r * kTile) * ky)), 0, wh); };
        gl::Enable(gl::SCISSOR_TEST);
        for (size_t i = 0; i + 3 < rects.size(); i += 4) {
            const int* r = &rects[i];
            const int ax = edgeX(r[0]), bx = edgeX(r[2]), ay = edgeY(r[1]), by = edgeY(r[3]);
            if (bx <= ax || by <= ay) continue;
            gl::Scissor(ax, wh - by, bx - ax, by - ay);
            gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
            remakeFillPixels += uint64_t(bx - ax) * uint64_t(by - ay);
        }
        gl::Disable(gl::SCISSOR_TEST);
    };
    if (splatGen_ && L.splat) {   // (regallery: none on the Gallery pages) the splatter (a new one: the old fading out, the new fading in, 0.35 s)
        const float t = std::clamp(float(now - splatFrom_) / 350.0f, 0.0f, 1.0f);
        if (t >= 1.0f)
            splatOld_ = false;
        if (splatOld_) {
            gl::BindTexture(gl::TEXTURE_2D, splatTex_[splatCur_ ^ 1]);
            gl::Uniform1f(uFgAlpha_, (1.0f - t) * show);
            drawMasked(splatMask_[splatCur_ ^ 1]);
        }
        gl::BindTexture(gl::TEXTURE_2D, splatTex_[splatCur_]);
        gl::Uniform1f(uFgAlpha_, t * show);
        drawMasked(splatMask_[splatCur_]);
    }
    if (L.fg) {   // (remakepages: none: the backdrop alone, under the Gallery's PC page)
        gl::BindTexture(gl::TEXTURE_2D, fgTex_);
        gl::Uniform1f(uFgAlpha_, show);
        drawMasked(fgMask_);
    }
    // regallery: the Remake Gallery's parts that fade / slide (the file panel, the entries' list): uploaded when
    // redrawn, each at its own alpha and offset every frame
    if (L.anim && L.fw > 0 && L.fh > 0) {
        const std::vector<Frontend::RemakeLayers::Part>& parts = *L.anim;
        while (animTex_.size() < parts.size()) {
            unsigned t = 0;
            gl::GenTextures(1, &t);
            animTex_.push_back(t);
        }
        if (L.animGen != animGen_) {
            animGen_ = L.animGen;
            for (size_t i = 0; i < parts.size(); ++i)
                if (parts[i].w > 0 && parts[i].h > 0)
                    upload(animTex_[i], parts[i].rgba, parts[i].w, parts[i].h);
        }
        const float kx = pw / float(L.fw), ky = ph / float(L.fh);
        for (size_t i = 0; i < parts.size(); ++i) {
            const Frontend::RemakeLayers::Part& p = parts[i];
            if (p.w <= 0 || p.h <= 0 || p.alpha <= 0.0f)
                continue;
            gl::BindTexture(gl::TEXTURE_2D, animTex_[i]);
            gl::Uniform1f(uFgAlpha_, p.alpha * show);
            rect(uFgRect_, x0 + (float(p.x0) + p.dx) * kx, y0 + float(p.y0) * ky, float(p.w) * kx, float(p.h) * ky);
            gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
        }
        gl::Uniform1f(uFgAlpha_, show);
    }
    // the focus / hover / help parts over it (small: uploaded when they change)
    if (L.parts && L.fw > 0 && L.fh > 0) {
        const std::vector<Frontend::RemakeLayers::Part>& parts = *L.parts;
        while (partTex_.size() < parts.size()) {
            unsigned t = 0;
            gl::GenTextures(1, &t);
            partTex_.push_back(t);
        }
        if (L.partsGen != partsGen_) {
            partsGen_ = L.partsGen;
            for (size_t i = 0; i < parts.size(); ++i)
                upload(partTex_[i], parts[i].rgba, parts[i].w, parts[i].h);
        }
        const float kx = pw / float(L.fw), ky = ph / float(L.fh);
        gl::Uniform1f(uFgAlpha_, show);
        for (size_t i = 0; i < parts.size(); ++i) {
            const Frontend::RemakeLayers::Part& p = parts[i];
            gl::BindTexture(gl::TEXTURE_2D, partTex_[i]);
            rect(uFgRect_, x0 + float(p.x0) * kx, y0 + float(p.y0) * ky, float(p.w) * kx, float(p.h) * ky);
            gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
        }
    }
    // the hand
    if (L.hand) {
        gl::BindTexture(gl::TEXTURE_2D, handTex_);
        if (L.handId != handId_) {
            handId_ = L.handId;
            std::vector<uint8_t> pm;
            premultiply(*L.hand, size_t(L.hiw) * size_t(L.hih), pm, true);
            gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, L.hiw, L.hih, 0, gl::RGBA, gl::UNSIGNED_BYTE, pm.data());
            texParams(false);
            remakeUploadBytes += size_t(L.hiw) * size_t(L.hih) * 4;
        }
        // galfix: 90 % of the sprite's size (its top left, the finger tip, at the pointer), smooth (linear filter),
        // over a soft shadow down and to the right (drawn twice, a little apart: softer), at the hand's alpha (over the
        // Gallery's model: see-through)
        const float hw = float(L.hiw) * s * Frontend::kHandScale, hh = float(L.hih) * s * Frontend::kHandScale;
        const float off = 1.6f * s;
        gl::Uniform1f(uFgShadow_, 1.0f);
        for (int k = 0; k < 2; ++k) {
            gl::Uniform1f(uFgAlpha_, 0.25f * show * L.handAlpha);
            rect(uFgRect_, x0 + L.hx * s + off + float(k) * 0.6f * s, y0 + L.hy * s + off + float(k) * 0.6f * s, hw, hh);
            gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
        }
        gl::Uniform1f(uFgShadow_, 0.0f);
        gl::Uniform1f(uFgAlpha_, show * L.handAlpha);
        rect(uFgRect_, x0 + L.hx * s, y0 + L.hy * s, hw, hh);
        gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
    }
    gl::BindVertexArray(0);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::Disable(gl::BLEND);
    gl::Enable(gl::DEPTH_TEST);
}

void Hud::drawClock(const Runtime& rt, int ww, int wh)
{
    port::Mem& mem = rt.mem;
    if (mem.at<int16_t>(G::game_mode) != 4 || rt.clear.active)
        return;
    const bool pc = rt.pcModels && pcSprites_.loaded();
    if (!pc && (rt.pcModels || !mem.mapped(kTileRam) || !mem.mapped(0x2000000)))
        return;
    const int unit = pc ? 10 : 8;
    const int W = pc ? assets::PcSprites::kWidth : assets::Arcade2D::kWidth;
    const int H = pc ? assets::PcSprites::kHeight : assets::Arcade2D::kHeight;
    const bool low = rt.lgClock < 10.0;
    if (low && mem.u32(G::frame_counter) % 30 >= 18)
        return;   // (blinking)
    const int secs = int(std::ceil(rt.lgClock));
    const std::string t = std::to_string(std::clamp(secs, 0, 599));
    const int dh = 3 * unit;   // the digits 3 cells high
    std::vector<SeatImg> gl;
    int tw = 0;
    for (char ch : t) {
        const SeatImg dg = spriteImg(mem, pc, pcSprites_, uint32_t(mem.at<int16_t>(0xCA720 + uint32_t(ch - '0') * 2)));
        if (dg.px.empty()) return;
        SeatImg o;
        o.h = dh, o.w = std::max(1, dg.w * dh / std::max(1, dg.h));
        o.px.assign(size_t(o.w) * o.h * 4, 0);
        for (int y = 0; y < o.h; ++y)
            for (int x = 0; x < o.w; ++x) {
                const uint8_t* q = &dg.px[(size_t(y * dg.h / o.h) * dg.w + size_t(x * dg.w / o.w)) * 4];
                uint8_t* d = &o.px[(size_t(y) * o.w + x) * 4];
                std::copy_n(q, 4, d);
                if (low) d[1] = uint8_t(d[1] / 5), d[2] = uint8_t(d[2] / 5);   // (red)
            }
        tw += o.w + unit / 4;
        gl.push_back(std::move(o));
    }
    std::vector<uint8_t> canvas(size_t(W) * H * 4, 0);
    int x = (W - tw) / 2;
    for (const SeatImg& g : gl) {
        paste(canvas, W, H, g, x, unit / 2);
        x += g.w + unit / 4;
    }
    upload(canvas, W, H, ww, wh);
}

void Hud::drawModBanner(const Runtime& rt, int ww, int wh)
{
    port::Mem& mem = rt.mem;
    if (mem.at<int16_t>(G::game_mode) != 4)
        return;
    if (rt.lgBannerFrame == 0)
        rt.lgBannerFrame = rt.frameCount;   // (from the game's first frame in play)
    const uint64_t age = rt.frameCount - rt.lgBannerFrame;
    constexpr uint64_t kShow = 288;   // 5 s of game frames
    if (age >= kShow)
        return;
    const float fade = age + 30 > kShow ? float(kShow - age) / 30.0f : 1.0f;
    const std::vector<std::string>& L = rt.lgBanner;
    if (!pcTried_)
        loadPcSprites();
    if (!pcSprites_.loaded()) {   // (no PC sprites: the lines as text)
        static Overlay o;
        static bool ok = false, tried = false;
        if (!tried) { tried = true; std::string e; ok = o.init(e); if (ok) o.setStyle(0, 255, 255, 255), o.translate = false; }
        if (ok) {
            o.setText(L, wh >= 1000 ? 3 : 2);
            o.drawAnchored(ww, wh, ww * 0.5f, wh * 0.4f, 0.5f, 0.5f);
        }
        return;
    }
    const int W = assets::PcSprites::kWidth, H = assets::PcSprites::kHeight;   // (the PC's 640 x 480)
    rgba_.assign(size_t(W) * H * 4, 0);
    const int n = int(L.size()), gap = n > 12 ? 24 : 28;
    const int top = std::max(24, 200 - n * gap / 2);
    for (int r = 0; r < n; ++r) {
        auto id = [](char c) { return c > ' ' && c <= '~' ? 0x19D + (c - 0x20) : 0; };   // (the menu's white 24)
        int width = 0;
        for (char c : L[size_t(r)])
            width += id(c) == 0 || pcSprites_.width(id(c)) == 0 ? 8 : pcSprites_.width(id(c)) + 1;
        int x = (0x280 - width) / 2;
        const int y = top + r * gap;
        for (char c : L[size_t(r)]) {
            if (id(c) == 0 || pcSprites_.width(id(c)) == 0) { x += 8; continue; }
            pcSprites_.draw(rgba_, id(c), x, y);
            x += pcSprites_.width(id(c)) + 1;
        }
        if (r & 1)   // every second line red (the white letters' shades kept)
            for (int yy = std::max(0, y - 2); yy < std::min(H, y + gap); ++yy)
                for (int xx = 0; xx < W; ++xx) {
                    uint8_t* d = &rgba_[(size_t(yy) * W + size_t(xx)) * 4];
                    if (!d[3]) continue;
                    const int l = std::max({d[0], d[1], d[2]});
                    d[0] = uint8_t(std::min(255, l + 20)), d[1] = uint8_t(l * 12 / 100), d[2] = uint8_t(l * 8 / 100);
                }
    }
    // a black edge round the letters (as the pause menu's: the menu font has none of its own), then the fade
    std::vector<uint8_t> a(size_t(W) * H, 0);
    for (size_t p = 0; p < a.size(); ++p) a[p] = rgba_[p * 4 + 3];
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (a[size_t(y) * W + size_t(x)]) continue;
            int edge = 0;
            for (int dy = -2; dy <= 2 && edge < 255; ++dy)
                for (int dx = -2; dx <= 2; ++dx) {
                    const int sx = x + dx, sy = y + dy;
                    if (sx < 0 || sy < 0 || sx >= W || sy >= H || !a[size_t(sy) * W + size_t(sx)]) continue;
                    const int d = std::abs(dx) + std::abs(dy);
                    edge = std::max(edge, std::max(std::abs(dx), std::abs(dy)) <= 1 ? 255 : d <= 3 ? 120 : 0);
                }
            if (!edge) continue;
            uint8_t* d = &rgba_[(size_t(y) * W + size_t(x)) * 4];
            d[0] = d[1] = d[2] = 0;
            d[3] = uint8_t(edge);
        }
    if (fade < 1.0f)
        for (size_t p = 3; p < rgba_.size(); p += 4) rgba_[p] = uint8_t(float(rgba_[p]) * fade);
    upload(rgba_, W, H, ww, wh);
}

} // namespace hotd::game
