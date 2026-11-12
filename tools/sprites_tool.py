#!/usr/bin/env python3
"""
sprites_tool.py - the 2D sprites of The House of the Dead: arcade (Model 2 ROM) and PC (1998) (docs/formats.md §31).

Arcade: table 0xA6590 in the program ROM, 12 bytes a sprite: tiles, palettes, layout (pointers into the program ROM /
main_data; 0x6000000.. is the upper half of main_data). Tiles: pieces {u32 count, count x 32 bytes of 8 x 8 4-bit
tiles (each row two little-endian u16 words, pixel 0 = the top nibble)}, ended by a 0 count; each piece starts on a
slot of 128 tiles. Palettes: one 16-colour RGB555 palette (32 bytes) a piece. Layout: u16 width, u16 height (cells),
then the cells: tile number (bits 0-13; tile >> 7 = the piece's slot = its palette). Pen 0 and black are transparent.
PC: SPRITE\\SPR.RBH (descriptors) + ART\\STAGE1\\CREATURE\\BIGGER.VMC (pages) - see src/assets/sprites_pc.h.

Usage (paths default to the repo's private\\ folders):
  py tools/sprites_tool.py arcade <out.png> <ids...>      contact sheet of arcade sprites (hex ids, ranges a-b)
  py tools/sprites_tool.py pc <out.png> <ids...>          contact sheet of PC sprites
  py tools/sprites_tool.py pairs <out.png> <ids...>       arcade sprite next to its PC match
  py tools/sprites_tool.py map <out.inc>                  matches every arcade sprite to a PC sprite (thumbnails) and
                                                          writes src/assets/sprite_map_pc.inc
Options: --regions <arcade regions folder> --pc <PC game folder>
Requires: pillow, numpy.
"""
import sys, os, struct
import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
args = sys.argv[1:]
def opt(name, default):
    if name in args:
        i = args.index(name); v = args[i + 1]; del args[i:i + 2]; return v
    return default
REGIONS = opt('--regions', os.path.join(ROOT, 'private', 'arcade_exports', 'regions'))
PCDIR = opt('--pc', os.path.join(ROOT, 'private', 'pcharness', 'game'))

# --- arcade ---
_rom = _data = None
def _load_arcade():
    global _rom, _data
    if _rom is None:
        _rom = open(os.path.join(REGIONS, 'maincpu.bin'), 'rb').read()
        _data = open(os.path.join(REGIONS, 'main_data.bin'), 'rb').read()
def _off(a):
    if a < 0x200000: return _rom, a
    if 0x2000000 <= a < 0x4000000: return _data, a - 0x2000000
    if 0x6000000 <= a < 0x7000000: return _data, a - 0x5000000
    raise ValueError(hex(a))
def _u32(a): b, o = _off(a); return struct.unpack_from('<I', b, o)[0]
def _u16(a): b, o = _off(a); return struct.unpack_from('<H', b, o)[0]
ARCADE_COUNT = 0x276

def arcade_sprite(i):
    """RGBA array (h*8, w*8, 4) of arcade sprite i"""
    _load_arcade()
    t, p, l = _u32(0xA6590 + i * 12), _u32(0xA6594 + i * 12), _u32(0xA6598 + i * 12)
    tiles, pals = {}, []
    base, a, ci = 0, t, 0
    while True:
        n = _u32(a)
        if n == 0: break
        b, o = _off(a + 4)
        for k in range(n): tiles[base + k] = b[o + k * 32:o + k * 32 + 32]
        pal = [_u16(p + ci * 32 + 2 * c) for c in range(16)]
        pals += [pal] * ((n + 127) // 128)
        base += (n + 127) // 128 * 128; a += 4 + n * 32; ci += 1
    w, h = _u16(l), _u16(l + 2)
    img = np.zeros((h * 8, w * 8, 4), np.uint8)
    for cy in range(h):
        for cx in range(w):
            tn = _u16(l + 4 + 2 * (cy * w + cx)) & 0x3FFF
            if tn not in tiles: continue
            td, pal = tiles[tn], pals[tn >> 7]
            for y in range(8):
                w0, w1 = struct.unpack_from('<HH', td, y * 4)
                px = [(w0 >> (12 - 4 * k)) & 15 for k in range(4)] + [(w1 >> (12 - 4 * k)) & 15 for k in range(4)]
                for x, c in enumerate(px):
                    col = pal[c]
                    if c == 0 or (col & 0x7FFF) == 0: continue
                    img[cy * 8 + y, cx * 8 + x] = ((col & 31) * 255 // 31, (col >> 5 & 31) * 255 // 31, (col >> 10 & 31) * 255 // 31, 255)
    return img

# --- PC ---
_rbh = None; _offs = None; _pages = {}
def _load_pc():
    global _rbh, _offs
    if _rbh is None:
        _rbh = open(os.path.join(PCDIR, 'SPRITE', 'SPR.RBH'), 'rb').read()
        n = struct.unpack_from('<I', _rbh, 0x28)[0] // 4
        _offs = struct.unpack_from('<%dI' % n, _rbh, 0x28)
        f = open(os.path.join(PCDIR, 'ART', 'STAGE1', 'CREATURE', 'BIGGER.VMC'), 'rb').read()
        o = 12
        while o + 8 <= len(f):
            tag, size = f[o:o + 4], struct.unpack_from('<I', f, o + 4)[0]
            if tag == b'BODY':
                num, w, h = struct.unpack_from('<IHH', f, o + 12)
                if w == 256 and h == 256:
                    _pages[num] = (o + 8 + 64, f)
            o += 8 + size

def pc_count():
    _load_pc(); return len(_offs)

def _page(n):
    o, f = _pages[n]
    c = np.frombuffer(f, np.uint16, 256 * 256, o).reshape(256, 256)
    img = np.zeros((256, 256, 4), np.uint8)
    x5 = lambda v: ((v << 3) | (v >> 2)).astype(np.uint8)
    img[..., 0] = x5(c >> 10 & 31); img[..., 1] = x5(c >> 5 & 31); img[..., 2] = x5(c & 31)
    img[..., 3] = np.where(((c & 0x8000) != 0) & ((c & 0x7FFF) != 0x7C1F), 255, 0)
    return img
_page_cache = {}

def pc_sprite(i):
    _load_pc()
    o = 0x28 + _offs[i]
    n = struct.unpack_from('<I', _rbh, o)[0]
    hdr = struct.unpack_from('<6h', _rbh, o + 4)
    img = np.zeros((max(hdr[5], 1), max(hdr[4], 1), 4), np.uint8)
    for k in range(min(n, 64)):
        x0, y0, x1, y1, u, v, _, pg, fl = struct.unpack_from('<4hBBHhh', _rbh, o + 16 + 16 * k)
        if pg not in _pages or x1 <= x0 or y1 <= y0: continue
        if pg not in _page_cache: _page_cache[pg] = _page(pg)
        src = _page_cache[pg][v:v + y1 - y0, u:u + x1 - x0]
        dst = img[y0:y0 + src.shape[0], x0:x0 + src.shape[1]]
        src = src[:dst.shape[0], :dst.shape[1]]
        m = src[..., 3] > 0
        dst[m] = src[m]
    return img

# --- matching ---
def _thumb(img, n=24):
    a = img[..., 3] > 0
    if not a.any(): return None, (0, 0)
    ys, xs = np.where(a)
    c = img[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    rgb = c[..., :3].astype(np.float32) * (c[..., 3:4] > 0)
    t = np.array(Image.fromarray(rgb.astype(np.uint8)).resize((n, n), Image.BILINEAR)).astype(np.float32)
    al = np.array(Image.fromarray(((c[..., 3] > 0) * 255).astype(np.uint8)).resize((n, n), Image.BILINEAR)).astype(np.float32)
    return np.concatenate([t, al[..., None]], 2), c.shape[:2]

# sprites set by hand: the boss life bar cells and frame (the same order on both), the full-screen covers the arcade
# shows only through palette fades (black = transparent on Model 2; drawn opaque they would hide the screen); 0xA6
# (the results screen's area picture) has no PC counterpart
OVERRIDES = {**{a: 0x422 + a - 0xE4 for a in range(0xE4, 0x106)}, 0xA1: 0, 0xA2: 0, 0xA6: 0,
             # hangfix: the Magician's title line "Magician TYPE 0" (the thumbnails took the Hangedman's 0x50B)
             0xE2: 0x50D,
             # rec131 (user's v1.3.1 recording, attract 1:12): the reload boards "SHOOT OUTSIDE OF THE SCREEN WHEN
             # BULLETS ARE OUT." 0x26B (and the older 0x89) are the PC's English / Spanish board 0x4E0 (Spanish CD:
             # RECARGA ANTES DE QUEDARTE SIN BALAS); the thumbnails took the Japanese 0x4DF next to it
             0x89: 0x4E0, 0x26B: 0x4E0,
             # bugs2 (#9): the hit marks 0x28..0x2D are the PC's 0x47E..0x483 in order (the PC's hit-notice table
             # 0x5F2DEC = the arcade's 0xCA5FC entry for entry); the thumbnails took the claws 0x480 for the punch 0x2C
             **{0x28 + k: 0x47E + k for k in range(6)},
             # the game-over route map 0x243.. (map parts 0x243-0x246, trail pieces 0x247-0x267): the PC's own set 0x63A..
             # (the PC route task 0x4B16D4 loads banks 0x63A / 0x63E where the arcade's 0x5CF50 loads 0x243 / 0x247; the
             # PC's route records 0x5EA064 / reveal records 0x5E9FE0 = the arcade's 0xC1810 / 0xC1790 with ids + 0x3F7)
             **{a: a + 0x3F7 for a in range(0x243, 0x268)},
             # the life torches as the PC's hud_lives_draw 0x50B5B0 draws them (hud3: the picture match took the
             # orange set 0x44D.. for 1P and mixed 2P's frames): 1P lit 0xB7.. -> 0x46F.., burning out 0xBE.. -> 0x476..;
             # 2P lit 0xA8.. -> 0x460.., burning out 0xAF.. -> 0x467..
             **{0xB7 + k: 0x46F + k for k in range(7)}, **{0xBE + k: 0x476 + k for k in range(8)},
             **{0xA8 + k: 0x460 + k for k in range(7)}, **{0xAF + k: 0x467 + k for k in range(8)},
             # endlook: the ending's staff roll. The arcade's n-th sheet is 0xAC2A0[record +0x70] (kind120; the
             # records in play order: 0 1 2 7 8 3 4 5 6 9 13 11 10 12, kind122's close redraws 0x1E1 = the last); the
             # PC's n-th is THOTD.EXE 0x5D466C[n] = 0x5DC + n (its kind120 0x473E90 counts 0x5D4648): its own credits
             # (14 sheets, with the PC staff), so the arcade's n-th sheet -> the PC's n-th. 0x1E0 / 0x1E3 (variants in
             # no table) have none; the digits bank 0x2E: 0x1E5 -> 0x5EA (PC game_ending_start 0x473CD8 loads 0x5DC /
             # 0x577 / 0x5EA where the arcade loads 0x1D5 / 0x16F / 0x1E5)
             **{s: 0x5DC + n for n, s in enumerate([0x1D5, 0x1D6, 0x1D7, 0x1DC, 0x1DD, 0x1D8, 0x1D9, 0x1DA, 0x1DB,
                                                    0x1DE, 0x1E2, 0x26E, 0x1DF, 0x1E1])},
             0x1E0: 0, 0x1E3: 0, 0x1E5: 0x5EA,
             # pcrank: the small font 0x1E5.. ('0'-'9' ! !! - ? , ' . .. A-Z a-z, to 0x22A) is the PC's 0x5EA.. (+0x405):
             # THOTD.EXE's name entry 0x4B3E0C loads bank 0x2E with 0x5EA and draws 0x5EB + rank / 0x60F / 0x603 where
             # the arcade's 0x5D6D0 draws 0x1E6 + rank / 0x20A / 0x1FE, and its ordinals table 0x5EDD60 is the
             # arcade's 0xC5530 + 0x405 (1ST 2ND 3RD); the thumbnails had matched the ranking font's glyphs instead
             **{a: a + 0x405 for a in range(0x1E6, 0x22B)},
             # the ranking font's A D F M N (arcade 0x22C-0x230, font table 0x22330) are the PC's 0x5C3-0x5C7 (its font
             # table 0x44B070); the thumbnails took 'X' (0x5BC) for the 'A' ("TXG" for TAG). The PC's 0x5C3 is a tiny
             # boxed 'A' (8 x 9 px, unlike the 24 x 32 carved letters): its plain 'A' 0x5A5 (the arcade's 0x195) instead
             0x22C: 0x5A5, **{0x22D + k: 0x5C4 + k for k in range(4)}}

def build_map():
    pcs = []
    for p in range(1, pc_count()):
        try:
            t, s = _thumb(pc_sprite(p))
        except Exception:
            t = None
        if t is not None: pcs.append((p, t, s))
    m = {}
    for a in range(ARCADE_COUNT):
        t, s = _thumb(arcade_sprite(a))
        best = (1e18, 0)
        if t is not None:
            for p, tp, sp in pcs:
                rh, rw = sp[0] / max(s[0], 1), sp[1] / max(s[1], 1)
                if not (0.9 < rw < 1.8 and 0.9 < rh < 1.8 and abs(rw / rh - 1) < 0.35): continue
                e = float(((t - tp) ** 2).mean())
                if e < best[0]: best = (e, p)
        m[a] = OVERRIDES.get(a, best[1])
    return m

def _ids(lst):
    out = []
    for a in lst:
        if '-' in a:
            lo, hi = [int(x, 16) for x in a.split('-')]; out += range(lo, hi + 1)
        else: out.append(int(a, 16))
    return out

def _sheet(path, imgs, CW=1100):
    x = y = rowh = 0; pos = []
    for im in imgs:
        h, w = im.shape[:2]
        if x + w > CW: x = 0; y += rowh + 12; rowh = 0
        pos.append((x, y)); x += w + 12; rowh = max(rowh, h)
    can = Image.new('RGBA', (CW, y + rowh + 2), (40, 40, 70, 255))
    for im, p in zip(imgs, pos): can.alpha_composite(Image.fromarray(im), p)
    can.save(path)

if __name__ == '__main__':
    cmd = args[0] if args else ''
    if cmd == 'arcade': _sheet(args[1], [arcade_sprite(i) for i in _ids(args[2:])])
    elif cmd == 'pc': _sheet(args[1], [pc_sprite(i) for i in _ids(args[2:])])
    elif cmd == 'pairs':
        m = build_map()
        ims = []
        for i in _ids(args[2:]):
            ims += [arcade_sprite(i), pc_sprite(m[i]) if m[i] else np.zeros((1, 1, 4), np.uint8)]
        _sheet(args[1], ims)
    elif cmd == 'map':
        m = build_map()
        out = ['// GENERATED by tools/sprites_tool.py map: the arcade sprite (table 0xA6590) -> the PC sprite (SPRITE\\SPR.RBH)',
               '// showing the same picture, found by comparing thumbnails (0 = none); overrides in the tool (life bar, covers).',
               '// UNVERIFIED (teaser) except the HUD / continue / game over / boss sprites checked by eye.',
               'static const uint16_t kArcadeToPcSprite[0x%X] = {' % ARCADE_COUNT]
        row = []
        for a in range(ARCADE_COUNT):
            row.append('0x%03X' % m[a])
            if len(row) == 12: out.append('    ' + ', '.join(row) + ','); row = []
        if row: out.append('    ' + ', '.join(row) + ',')
        out.append('};')
        open(args[1], 'w', newline='\n').write('\n'.join(out) + '\n')
    else:
        print(__doc__)
