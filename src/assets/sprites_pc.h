// The PC port's 2D sprites (docs/formats.md §31): SPRITE\SPR.RBH holds the descriptors, the pixels are pages of
// ART\STAGE1\CREATURE\BIGGER.VMC (the same page numbers the game loads from TEXTURE.VMC).
//
// SPR.RBH: "PIFF" <size> "RBHF" "RBHH" <u32 12> <12 bytes> "BODY" <u32 size> <data> "RELC" <relocations>. The BODY data
// starts with 1640 u32 offsets (relative to the data), one a sprite ID; a sprite: u32 piece count, s16 bounding box
// x0, y0, x1, y1, s16 width, height, then the pieces, 16 bytes each: s16 x0, y0, x1, y1 (inside the sprite), u8 u,
// u8 v (in the page), u16 0, s16 page number, s16 flags (1). Coordinates are in 640 x 480 screen pixels. The PC's 2D
// layers are 64 x 48 cells of sprite IDs (0x5D24F4 / 0x5D24F8): sprite_draw_anim (0x4434B0) clears the cells the
// sprite covers ((x1 - x0) / 8 x (y1 - y0) / 8) and stores the ID in its top-left cell; the renderer (0x4432F0) draws
// each ID at its cell (8 units of a 512 x 384 grid). Page pixels: ARGB1555, transparent when the top bit is clear or
// the colour is the key magenta 0x7C1F.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hotd::assets {

class PcSprites {
public:
    static constexpr int kWidth = 640, kHeight = 480;
    // stage 1..4: the colours go through STAGEn.TBL / STAGEn.PAL (the PC's 8-bit palette), as the game draws them
    // bloodColour: PcConfig::blood (display2: the pages of that blood set, as the PC game's TEXTURE.VMC)
    // palette (menuart): another TBL / PAL pair by name ("MENU": the front end's menu.pal) instead of STAGEn
    bool load(const std::string& installDir, std::string& err, int stage = 1, int bloodColour = 0,
              const std::string& palette = "");
    bool loaded() const { return !sprites_.empty(); }
    // draws sprite `id` with its origin at (x, y) of a kWidth x kHeight RGBA canvas
    void draw(std::vector<uint8_t>& canvas, int id, int x, int y) const;
    // the same, only inside the canvas rectangle [cx0, cx1) x [cy0, cy1); canvasW: the canvas's width (remakemenu:
    // the front end's wide screens; the height is the canvas's size / canvasW)
    void draw(std::vector<uint8_t>& canvas, int id, int x, int y, int cx0, int cy0, int cx1, int cy1,
              int canvasW = kWidth) const;
    // the sprite's width in pixels (0 when unknown)
    int width(int id) const;
    int height(int id) const;   // (menuart) the sprite's height in pixels (0 when unknown)
    // the sprite alone as a width(id) x height(id) RGBA image (not clipped to the 640 x 480 screen; transparent where
    // no piece is drawn); empty when unknown (Add to Steam: the art from the player's own PC files)
    std::vector<uint8_t> image(int id) const;
    // the PC's grey look (pcpolish3): while set, the pages through BROWN.TBL / BROWN.PAL (as the textures)
    void setBrown(bool on) { brown_ = on && !brownPages_.empty(); }
    // pmfix2: a picture of ours under a sprite ID past the PC's (the PC Mode extras' portraits, made from the models at
    // first launch): w x h RGBA (alpha 0: not drawn), drawn with its top-left at (x + ox, y + oy); kept over load()
    void addImage(int id, int w, int h, std::vector<uint8_t> rgba, int ox = 0, int oy = 0);
    // the top-left of sprite id's pieces (0, 0 when unknown): where a picture standing in for it goes
    void origin(int id, int& ox, int& oy) const;
    // pchotfix: sprite id has pieces and every one of their pages was read (false: not drawn, or only in part: another
    // version of SPR.RBH / BIGGER.VMC, or a damaged one)
    bool drawable(int id) const;

private:
    struct Piece {
        int16_t x0, y0, x1, y1;
        uint8_t u, v;
        int16_t page;
    };
    struct Sprite {
        int16_t box[4] = {}, w = 0, h = 0;
        std::vector<Piece> pieces;
        uint8_t shade = 0;   // pchotfix: 0, or the texels scaled by shade / 255 (the grey font drawn from the white one)
    };
    std::vector<Sprite> sprites_;
    std::map<int, std::vector<uint8_t>> pages_;   // page number -> 256 x 256 RGBA
    std::map<int, std::vector<uint8_t>> brownPages_;   // the same through BROWN.TBL / BROWN.PAL
    bool brown_ = false;
    struct Custom { int w = 0, h = 0, ox = 0, oy = 0; std::vector<uint8_t> rgba; };
    std::map<int, Custom> custom_;
};

} // namespace hotd::assets
