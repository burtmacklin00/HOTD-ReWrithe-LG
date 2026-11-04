// Text overlay (controls help): lines are drawn with SDL's built-in 8x8 debug font into a surface (software renderer),
// uploaded as a texture and drawn as a translucent panel in the top-left corner over the 3D view.
#pragma once

#include "render/gl.h"
#include "render/ttf.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hotd {

class Overlay {
public:
    bool init(std::string& err);
    void shutdown();
    // Rebuilds the texture only when the text or scale changes.
    void setText(const std::vector<std::string>& lines, int scale);
    // setup (game/launcher.cpp): a ready RGBA picture (w x h, rows top first), drawn 1:1 instead of text
    void setImage(const std::vector<uint8_t>& rgba, int w, int h, bool smooth = false);
    // Light Gun Edition (the 4:3 bezel): the picture scaled into the window rectangle (x, y, w, h; pixels, top-left)
    void drawIn(int windowWidth, int windowHeight, float x, float y, float w, float h);
    void draw(int windowWidth, int windowHeight);
    // centred on a window pixel instead of the top-left corner (crosshairs)
    void drawAt(int windowWidth, int windowHeight, float x, float y);
    // with the point (ax, ay) of the panel (fractions: 0 0 = top-left, 1 1 = bottom-right) at window pixel (x, y)
    void drawAnchored(int windowWidth, int windowHeight, float x, float y, float ax, float ay);
    // the panel's size as drawn (window pixels; 0 before any text)
    float drawnWidth() const { return float(texW_) * outScale(); }
    float drawnHeight() const { return float(texH_) * outScale(); }
    // the panel's alpha (0 = text only) and the text colour; set before setText
    void setStyle(int panelAlpha, uint8_t r, uint8_t g, uint8_t b) { panel_ = panelAlpha; color_[0] = r; color_[1] = g; color_[2] = b; texW_ = 0; }
    // lang (game/lang.h): each line goes through the language's lookup (off: the caller translated them: the text
    // launcher, whose clickable words sit at fixed columns); a line with characters the 8 x 8 font lacks is drawn in
    // the language's font, every character in its column (Hangul / kana / CJK: two), at the scale's resolution
    bool translate = true;

private:
    gl::GLuint program_ = 0, vao_ = 0, texture_ = 0;
    gl::GLint uRect_ = -1;
    int texW_ = 0, texH_ = 0, scale_ = 1, panel_ = 170;
    bool hi_ = false;   // lang: the texture is at the scale's resolution (drawn 1:1)
    float outScale() const { return hi_ ? 1.0f : float(scale_); }
    uint64_t langGen_ = 0;
    uint8_t color_[3] = {235, 235, 220};
    void drawRect(int w, int h, float x0, float y1);
    void setTextTtf(const std::vector<std::string>& lines, const render::TtfText& tf);
    std::vector<std::string> lines_;
};

// a cut-scene's hold-to-skip (main.cpp): a plain red ring filling clockwise from the top (fill 0..1)
class SkipRing {
public:
    bool init(std::string& err);
    void shutdown();
    // centred on window pixel (x, y), `size` pixels across
    void draw(int windowWidth, int windowHeight, float x, float y, float size, float fill);
    // Light Gun Edition: another colour (and alpha scale) for the rings that are not the skip's
    void setColor(float r, float g, float b, float a = 1.0f) { color_[0] = r, color_[1] = g, color_[2] = b, color_[3] = a; }
private:
    gl::GLuint program_ = 0, vao_ = 0;
    gl::GLint uRect_ = -1, uFill_ = -1, uColor_ = -1;
    float color_[4] = {0.86f, 0.06f, 0.06f, 1.0f};
};

} // namespace hotd
