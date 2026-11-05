#include "render/renderer.h"

#include "assets/mods.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unordered_map>

namespace hotd {

namespace {

const char* kVertexShader = R"(#version 330 core
// visfix (a friend's run on an AMD Radeon 610M: pop-ins and fades): the depth pre-pass (integrated graphics) draws
// depth only, then the colours with EQUAL. Drivers build a position-only copy of this shader for the depth-only draws;
// without invariance its depth may differ in the last bits from the colour draw's, and EQUAL then drops those pixels
// (what lies behind shows through, flickering from frame to frame). invariant: both draws compute the same depth
invariant gl_Position;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNrm;
layout(location = 3) in uvec4 aRect;
layout(location = 4) in uvec2 aFlagsLayer;
layout(location = 5) in uvec4 aColor;
layout(location = 6) in uint aPal;
layout(location = 7) in uint aLodCoef;
flat out uint vLodCoef;
uniform usampler2D uPalAnim;   // 1024 x 1: animated polygon palette entries (RGB555, bit 15 = set)
uniform usampler2D uSetPal;    // 1024 x 1: the drawn Model 2 set's polygon palette (TextureSet::basePalette, RGB555)
uniform int uShading;          // 1: Model2Indexed (the base colour comes from uSetPal)
uniform int uGrey;             // RenderOptions::grey (the polygon palette greyed: palette_bank2_upload)
uniform mat4 uViewProj;
uniform vec3 uOffset;
uniform mat3 uRot;
uniform vec2 uUVScroll;        // texels added to the texture points (scrolling water)
uniform float uDepthBias;      // DrawItem::depthBias
uniform vec3 uCamPos;
uniform float uFloorY;         // DrawItem::floorY / floorPull (polish2)
uniform int uFloorPull;
uniform float uPush;           // DrawItem::push: world units away from the camera (depth only)
uniform float uTie;            // Renderer::debugTie
uniform int uTieItem;
uniform int uFlat;              // DrawItem::flat (arcade): every polygon untextured in this palette entry, -1 = off
uniform int uSort;              // Draw Depth "Arcade Sort" (drawDepth())
uniform int uSortItem;          // the item's place in the list (0-127): ties go to the earlier model
uniform samplerBuffer uSortKeys;   // Renderer::sortKeysOf: 4 texels per triangle of the stage's buffer
uniform vec3 uCamFwd;
uniform float uHybrid;          // depthfix: Depth Buffer's key band (renderer.h DrawDepth), 0 = off
uniform int uKeyless;           // depthopt: the item drawn per pixel under Arcade Sort (a mod's depth = buffer): its keys unread
uniform vec2 uProj;             // depthfix: the projection's depth terms: ndc z = uProj.x - uProj.y / view z
uniform float uBiasScale;       // depthfix: the near plane (the NDC biases were tuned at near 1; Free Cam 0.1)
uniform int uRainbow;           // cosmods: RenderOptions::rainbow (0 off, 1 per item, 2 one hue, 3 the custom colour)
uniform vec3 uBloodRgb;         // cosmods: RenderOptions::bloodRgb (mode 3)
uniform float uRainbowPhase;
uniform vec3 uRainbowTable[90];  // cosmods: the game's rainbow (the Chapter 4 screens: palette_animation, 0xAB120), 0..1
uniform int uRainbowGame;       // 1: vRainbow from uRainbowTable, 0: a plain hue
uniform int uRainbowSync;       // cosmods: the animated palette entry the blood takes (the screens' own colour), -1 none
uniform float uBodyHue;         // cosmods: DrawItem::bloodHue (-1 none; 0..1 a creature's own hue; 2..3 cycling with an offset; 4..5 the same, blood rows only; 6..7 the same, wounds as painted)
flat out vec3 vRainbow;         // cosmods: the rainbow colour of this item (PC: the marked blood texels times it)
flat out float vRainbowPal;     // cosmods: 1 = this polygon is the blood entry 1023 in the rainbow colour (fragment: no row lookup)
out vec3 vWorld;
out vec2 vUV;
flat out uvec4 vRect;
flat out uvec2 vFL;
flat out uvec4 vColor;
flat out vec3 vNrm;
flat out ivec2 vSortDbg;
flat out float vSortDepth;   // Arcade Sort: the polygon's depth (its key), written as it is by every pixel (-1: none)
flat out float vSortKz;   // debugging (HOTD_IDSHOT with HOTD_IDSHOT_TRI): the triangle and its sort mode
flat out float vKeyBand;   // depthfix (KEY_BAND_FRAG): the polygon's rounded key for the fragment's band (-1: none)
out float vBandAdj;        // and the depth tweaks' offset (the pulls / biases, view units)
void main() {
    vWorld = uRot * aPos + uOffset;
    gl_Position = uViewProj * vec4(vWorld, 1.0);
    // DrawItem::depthBias, plus the polygon's decal layer inside its model (markCoplanarLayers, flags bits 13-14)
    float layer = float((aFlagsLayer.x >> 13) & 3u);
    gl_Position.z -= (uDepthBias + layer * 1e-5) * uBiasScale * gl_Position.w;
    // polish2: a decal layer's depth is also taken 0.05 model units nearer along the view ray (the screen position
    // stays): the Model 2 sorts polygons, so a decal drawn after its base shows whole even where it dips a little
    // behind it. The zombie's eyeball (zonbichana_kao 0x781) lies up to 0.014 behind the face's black socket, which
    // hid all but a crescent ("huge pupils"; MAME snap 0028: a white eyeball)
    // polish2: feet drawn over the ground. The game stands creatures on the floor height (0x51EA40, -25 in Chapter 1's
    // courtyard, as in MAME) while some ground models have parts above it (hei01_03a's kerb top at -23, 2 units up:
    // the researchers walk through it), so the feet sank into it (user play-test). The Model 2 sorts polygons and draws
    // the ground first, so in MAME (snaps 0023 / 0024) the feet show whole. Vertices up to 3 units above the floor get
    // their depth from the point along the view ray where a 2.2-unit dip under a level surface would surface again (at
    // most 14 units; fading out from 1.5 to 3 units up).
    // Floor decals (the _DD shadows, blood pools; uFloorPull 2) the same for a 0.3-unit dip (at most 3 units): the
    // Chariot room's floor lies at 1.05, the shadows at the floor height 1.00 (no shadows; MAME 0155 has them).
    // UNVERIFIED (teaser): a depth tweak standing in for the Model 2's polygon sort
    // polish3: the floor pull and the decal layer's pull ADD UP along the view ray (one projection). They used to be two
    // projections, the layer's replacing the floor's, so a decal layer in a pulled body lost the floor pull (up to 14
    // units) while its base polygons kept it: lying Sophie after the Chariot (sofatama_CT13a, uFloorPull 3) showed
    // her green iris quads (layer 0, flattenUnderTranslucent) in front of the eye texture (layer 1, user play-test
    // 2026-10-16 6:22, frames 4096+ of --event 19 --autofire)
    vec3 toCam = uCamPos - vWorld;
    float dist = length(toCam);
    float pull = 0.0;
    if (uFloorPull != 0 && (vWorld.y < uFloorY + 3.0 || uFloorPull == 3)) {
        // feet4: a body lying on the ground (uFloorPull 3: under 6 units tall) is pulled WHOLE by the full kerb dip,
        // every vertex the same (w 1): its world position is right (the lying researcher's lowest vertex 0.2-0.7 under
        // the floor -25, the kerb / ground top 2 units above it), but feet3's 2.5-unit cap is only ~0.5 unit of height
        // at the opening's low camera, so most of him stayed under the ground. A uniform pull keeps his own parts'
        // order (the photo / hand stay behind the face, which the height-faded pull broke)
        float w = uFloorPull == 3 ? 1.0 : clamp((uFloorY + 3.0 - vWorld.y) / 1.5, 0.0, 1.0);
        float dip = uFloorPull == 2 ? 0.3 : 2.2, cap = uFloorPull == 2 ? 3.0 : 14.0;
        pull = min(dip / max(abs(toCam.y) / max(dist, 1e-6), 0.1), cap) * w;
    } else if (uFloorPull == 2 && uShading == 1) {
        // glitch: an arcade decal away from the floor gets the same 0.3-unit dip along its own polygon's normal. Chapter
        // 3's security card (sekom01a 0x16CE, flat) lies 0.1-0.24 units UNDER the console's top (st3_card01a 0x16D0):
        // the Model 2 sorts polygons (MAME model2_v.cpp: the console's big polygon sorts by its own min / max z, the
        // small card's nearer), so the card shows (MAME snap 0351); our depth test hid it whole. PC models untouched
        vec3 n = uRot * aNrm;
        float nl = length(n);
        if (nl > 1e-3)
            pull = min(0.3 / max(abs(dot(toCam, n / nl)) / max(dist, 1e-6), 0.1), 3.0);
    }
    if (layer > 0.0) pull += layer * 0.05 * length(uRot[0]);
    pull -= uPush;
    if (pull != 0.0) {
        float d = min(pull, dist * 0.5);
        vec4 q = uViewProj * vec4(vWorld + toCam / max(dist, 1e-6) * d, 1.0);
        gl_Position.z = (q.z / q.w - (uDepthBias + layer * 1e-5) * uBiasScale) * gl_Position.w;
    }
    // Draw Depth "Arcade Sort" (clipfix, 2026-10-26): the Model 2 has no depth buffer. It sorts whole polygons by one
    // z each (MAME model2_3d_process_polygon: attr bits 10-11 pick the min or max camera z of the corners, 0 = the
    // previous polygon's) into 16-bit keys (float_to_zval: 4.12, the mantissa rounded to 12 bits) and fills the
    // screen near to far, a pixel once. Here every polygon's depth is its key (one depth for all its pixels): key x 256
    // + the item's place in the list (uSortItem), so on a tie the earlier model wins and, inside one model, the later
    // polygon (LEQUAL in draw order). That tie rule matches MAME in the four places compared (2026-10-26): Cyril's
    // waistband under his shirt (attract), Chapter 3's grey wall over the door (snap 354), Chapter 2's room corner
    // (snap 225: the next room's wall stays behind) and that room's window bars (the backdrop stays behind them). The
    // depth tweaks above are replaced (they stood in for the sort). Polygons without a key (pack models): their own z
    vSortDbg = ivec2(gl_VertexID / 3, 0);
    vKeyBand = -1.0;
    vBandAdj = 0.0;
    vSortKz = -1.0;
    vSortDepth = -1.0;
    float kz = 0.0;
    if ((uSort != 0 || uHybrid > 0.0) && uShading == 1) {
        int t = gl_VertexID / 3;
        vec4 k0 = texelFetch(uSortKeys, t * 4);
        int mode = uKeyless != 0 ? 0 : int(k0.w);
        vSortDbg = ivec2(t, mode);
        kz = dot(vWorld - uCamPos, uCamFwd);
        if (mode != 0) {
            vec3 k1 = texelFetch(uSortKeys, t * 4 + 1).xyz, k2 = texelFetch(uSortKeys, t * 4 + 2).xyz, k3 = texelFetch(uSortKeys, t * 4 + 3).xyz;
            vec4 z = vec4(dot(uRot * k0.xyz + uOffset - uCamPos, uCamFwd), dot(uRot * k1 + uOffset - uCamPos, uCamFwd),
                          dot(uRot * k2 + uOffset - uCamPos, uCamFwd), dot(uRot * k3 + uOffset - uCamPos, uCamFwd));
            kz = mode == 1 ? min(min(z.x, z.y), min(z.z, z.w)) : mode == 2 ? max(max(z.x, z.y), max(z.z, z.w)) : 1e10;
        }
        vSortKz = kz;
        // depthfix (2026-10-27): Depth Buffer = per-pixel depth, but every arcade polygon's depth is drawn to its sort key
        // as far as uHybrid x its distance: surfaces that lie that close (bodies / stones on the ground, a door in its
        // wall, a waistband under the shirt, the car at the pillar) resolve as the Model 2's sort does, things farther
        // apart and true crossings in close-ups stay per pixel. The pulls / biases above stay on top as an offset.
        // Linear in 1 / z when the band holds the whole polygon (as the projection), so per-vertex is exact there
        // The key as the arcade rounds it (float_to_zval: 12-bit mantissa), and inside one key step the arcade's tie
        // order: the earlier model first, inside a model the later polygon (here the step is split 128 ways by the
        // item's place, each part by the triangle's index mod 256)
        if (uSort == 0 && gl_Position.w > 1e-4) {
            float w0 = gl_Position.w;
            float kq = kz;
            if (kz >= 4.0 && kz < 1e9) {
                float e = exp2(floor(log2(kz)));
                float step = e / 4096.0, c = floor((kz - e) / step + 0.5) * step + e;
                float tie = (float(127 - min(uSortItem, 127)) + float(t % 256) / 256.0) / 128.0;
                kq = c - 0.5 * step + step * (1.0 - tie) * 0.98;
            }
            // the tweaks' offset without the floor pulls (they stood in for the sort the band now does: a door's foot
            // pulled 14 units nearer took its whole leaf over Chapter 2's bullet holes): the decal layer, uPush, the bias; the
            // decals' / shadows' own small pull stays (drawn late without depth writes, they lie under the floor's band)
            float d2 = min(uFloorPull == 2 ? pull : (layer > 0.0 ? layer * 0.05 * length(uRot[0]) : 0.0) - uPush, dist * 0.5);
            vec4 q2 = uViewProj * vec4(vWorld + toCam / max(dist, 1e-6) * d2, 1.0);
            float adj = uProj.y / max(uProj.x - (q2.z / q2.w - (uDepthBias + layer * 1e-5) * uBiasScale), 1e-9) - w0;
#ifdef KEY_BAND_FRAG
            // the band per pixel (fragment shader): a polygon spanning much depth (a door leaf, a floor) clamped
            // per vertex and interpolated drifted from its own surface (Chapter 2's bullet holes hidden)
            vKeyBand = mode != 0 || (k0.w != 0.0 && uKeyless == 0) ? kq : -1.0;
            vBandAdj = adj;
#else
            float s = clamp(kq, w0 * (1.0 - uHybrid), w0 * (1.0 + uHybrid)) + adj;
            gl_Position.z = (uProj.x - uProj.y / max(s, 1e-3)) * w0;
#endif
        }
    }
    if (uSort != 0 && uShading == 1) {
        // float_to_zval with the z-sort register's value: HOTD sends 4.0 (geometry command 0x04000808 in every display
        // list of the fullgame_bufram dumps; MAME raster command 0x08, z_adjust), so the exponent counts from 2: keys
        // 2^-10 .. 2^17 units apart, below 4 units with less precision (depthcmp, 2026-10-27; before: 1.0 assumed).
        // HOTD_SORT_EXACT: the key unrounded (24 bits, no item order)
        float key;
        uint zv = 0u;
        if (uSort == 2) {
            key = clamp(log2(max(kz, 1e-4)) + 13.0, 0.0, 28.0) / 28.0 * 16777215.0;
        } else {
            uint u = floatBitsToUint(kz);
            int e = int((u >> 23) & 255u) - 127 - 2;
            uint m = (u & 0x7FFFFFu) + 0x400u;
            if (m > 0x7FFFFFu) { e++; m = (m & 0x7FFFFFu) >> 1; }
            m >>= 11;
            zv = kz <= 0.0 ? 0u : e < -12 ? 0u : e < 0 ? (m | 0x1000u) >> uint(-e) : e < 15 ? (uint(e + 1) << 12) | m : 0xFFFFu;
            key = float(zv * 256u + uint(min(uSortItem, 127)) * 2u + 1u);   // (odd steps of 2: the float rounding stays inside)
        }
#ifdef SORT_FRAGDEPTH
        gl_Position.z = (key * 2.0 - 16777215.0) / 16777215.0 * gl_Position.w;
        // depthcmp (2026-10-27): every pixel of the polygon gets this depth bit for bit (fragment shader). Through
        // z / w the interpolated depth wobbled by a step across the polygon, so two polygons with one key were decided
        // per pixel by rounding instead of by draw order (Chapter 2, the Hangedman intro: a checker polygon of the
        // building showed over its wall; MAME render_polygons: one bucket per key, the later polygon in front, as
        // LEQUAL in draw order here)
        vSortDepth = key / 16777215.0;
#else
        // earlyz (2026-10-27): the same order from the vertex depth alone (no gl_FragDepth, so the GPU's early depth
        // test stays on). The 16-bit key zv only, at (zv + 0.25) / 2^24 in the depth buffer: the keyed draws use the
        // depth range 0 .. 2^-8 (Renderer::draw), so near 0 the float error of z * w / w and of the interpolation
        // stays under 0.02 of a 24-bit step and every pixel of the polygon rounds to the same stored value (ties exact,
        // as the gl_FragDepth path). The item's place is not in the depth: the items are drawn last-first instead
        // (Renderer::draw), so on a tie the earlier model, drawn later, wins by LEQUAL, inside one model the later
        // polygon. (zv * 2^-15 + 2^-17 - 1 is exact in a float.)
        gl_Position.z = (float(zv) * (1.0 / 32768.0) + (1.0 / 131072.0) - 1.0) * gl_Position.w;
#endif
    }
    if (uTie != 0.0) {   // Renderer::debugTie
        uint h = uint(gl_VertexID / 3) * 2654435761u + uint(uTieItem) * 40503u;
        gl_Position.z += uTie * (((h >> 15) & 1u) == 0u ? 1.0 : -1.0) * 2e-6 * gl_Position.w;
    }
    vUV = aUV + uUVScroll; vRect = aRect; vFL = aFlagsLayer; vColor = aColor; vNrm = uRot * aNrm; vLodCoef = aLodCoef;
)"  // (MSVC: the raw string's size limit)
R"(
    if (uShading == 1) {
        // chandshadow: HOTD runs the geometrizer without normals (boot: command 7's mode word & 3 = 2, MAME
        // geo_parse_nn_ns): each polygon's normal is the cross product of its transformed corners, normalized, and it
        // decides the face (culling), the light and the texture level. With a regular matrix that is the stored normal
        // turned (uRot * aNrm); a squashed one (the shadow pass: the model projected onto the floor, a singular matrix)
        // turns the corners' normal by the cofactor matrix instead: +-the floor's normal by the side the polygon faces
        // the shadow light, 0 for one seen edge-on. Polygons facing away from the light are rear faces and culled (MAME
        // 0164: the hall's chandelier leaves one blob under its middle, not the whole squashed model)
        vec3 c0 = uRot[0], c1 = uRot[1], c2 = uRot[2];
        float sc = length(c0) * length(c1) * length(c2);
        // (rellook 2026-10-30: singular against the biggest column cubed, not the product of the column lengths. The
        // hall's chandelier copy, diag(1.3, 0, 1.3) as the game builds it, has a middle column of float dust (or exactly 0)
        // pointing anywhere: against its own length that column did not look small, so in most frames the test failed, the normals stayed sideways (culled by the camera's side, drawn at the coarsest level) and
        // the copy drew hard black trapezoids instead of MAME's round blobs (MAME fullgame_us snap 0072). Every matrix
        // the old test caught still passes: sc <= m^3)
        float m = max(length(c0), max(length(c1), length(c2)));
        if (abs(dot(c0, cross(c1, c2))) <= 1e-3 * m * m * m) {
            vec3 g = aNrm.x * cross(c1, c2) + aNrm.y * cross(c2, c0) + aNrm.z * cross(c0, c1);
            float gl = length(g);
            vNrm = gl > 1e-5 * pow(sc, 2.0 / 3.0) ? g / gl : vec3(0.0);
        }
    }
    // flash: the model drawn with a flat header of the header RAM (Runtime::header_ram_flat; MAME draw_scanline_solid):
    // the polygon's texture, translucency, checker and mirror bits gone, its base colour = the palette entry; its light
    // parameter and double-sidedness are the polygon's own (attribute bits)
    uint pal = aPal;
    if (uFlat >= 0 && uShading == 1) {
        pal = uint(uFlat);
        vFL.x &= ~(1u | 2u | 4u | 8u | 16u);
    }
    if (pal < 1024u) {
        if (uShading == 1) {
            uint b = texelFetch(uSetPal, ivec2(int(pal), 0), 0).r;
            vColor.rgb = uvec3(b & 31u, (b >> 5) & 31u, (b >> 10) & 31u);
            // the grey look (palette_grey_build 0x1A400): palette bank 2, the polygon palette, is uploaded greyed
            // (palette_bank2_upload 0x1A300), entries 0-7 and 24-1023: (r + g + b) / 3, even, in all three channels.
            // The animated entries below are written ungreyed every vblank (palette_anim_copy 0x24E90), as there
            if (uGrey == 1 && (pal < 8u || pal >= 24u)) {
                uint g = ((vColor.r + vColor.g + vColor.b) / 3u) & 0x1Eu;
                vColor.rgb = uvec3(g);
            }
        }
        // cosmods: Rainbow Blood in step with the Chapter 4 screens (Fast): the blood entry takes their animated entry
        int palIdx = uRainbowSync >= 0 && uRainbow != 0 && uRainbow != 3 && pal == 1023u ? uRainbowSync : int(pal);
        uint p = texelFetch(uPalAnim, ivec2(palIdx, 0), 0).r;
        if ((p & 0x8000u) != 0u) vColor.rgb = uvec3(p & 31u, (p >> 5) & 31u, (p >> 10) & 31u);
    }
    // cosmods: Rainbow Blood (RenderOptions::rainbow): the hue now, per draw item (its place) or one for all; arcade the
    // blood entry 1023 in it (not in the grey look: the game greys the blood too), PC the marked texels (fragment)
    vRainbow = vec3(1.0);
    vRainbowPal = 0.0;
    if (uRainbow == 3) vRainbow = uBloodRgb;   // the custom Blood Color: the wounds only (entry 1023 has it already)
    else if (uRainbow != 0) {
        // the splashes: a broad wave over the world (an RGB keyboard's: one rainbow in ~250 units, so a flying drop
        // changes colour slowly; was ~20 units and flickered); a body's parts: one colour, cycling (DrawItem::bloodHue)
        float h = uRainbowPhase + (uRainbow == 1 ? dot(uOffset, vec3(0.0043, 0.0029, 0.0061)) : 0.0);
        if (uBodyHue >= 0.0 && uBodyHue < 1.0) h = uBodyHue;   // a creature's own colour
        else if (uBodyHue >= 2.0 && uRainbow == 1) h = uRainbowPhase + fract(uBodyHue);   // cycling with the body's own offset (Together: one colour for all)
        if (uRainbowGame == 1) {   // the game's own rainbow (palette_animation's 90-colour table 0xAB120), blended
            float s = fract(h) * 90.0;
            int i = int(s) % 90;
            vRainbow = mix(uRainbowTable[i], uRainbowTable[(i + 1) % 90], s - floor(s));
        } else
            vRainbow = clamp(abs(fract(h + vec3(0.0, 2.0, 1.0) / 3.0) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
        if (uShading == 1 && pal == 1023u && uGrey == 0 && uRainbowSync < 0) vRainbowPal = 1.0;
    }
}
)";

const char* kFragmentShader = R"(#version 330 core
in vec3 vWorld;
in vec2 vUV;
flat in uvec4 vRect;
flat in uvec2 vFL;
flat in uvec4 vColor;
flat in vec3 vNrm;
flat in uint vLodCoef;
flat in ivec2 vSortDbg;
flat in float vSortKz;
flat in float vSortDepth;
flat in float vKeyBand;
in float vBandAdj;
flat in vec3 vRainbow;
flat in float vRainbowPal;
uniform int uRainbow;
uniform float uBodyHue;
uniform int uBloodRows;         // rainbow2: RenderOptions::bloodRowMask (bit = colour row 0..31)
// cosmods: Rainbow Blood's creature wounds (arcade): the red of a creature's painted blood (its skin's colour rows, not
// the blood entry) in the creature's colour. A pixel's red beyond its other channels is the blood's share: that share
// takes the hue, the grey under it stays; only clearly red pixels (saturation 0.55+, green and blue alike: not orange
// skin or brown cloth)
vec3 woundTint(vec3 c) {
    float m = max(c.g, c.b);
    float s = (c.r - m) / max(c.r, 1e-3);
    float w = smoothstep(0.55, 0.75, s) * step(0.06, c.r) * (1.0 - smoothstep(0.15, 0.3, abs(c.g - c.b) / max(c.r, 1e-3)));
    return mix(c, vec3(m) + (c.r - m) * vRainbow, w);
}
uniform float uHybrid;
uniform vec2 uProj;
out vec4 oColor;
uniform int uShading;            // 0 = DirectRgba, 1 = Model2Indexed
uniform sampler2DArray uLayers;  // RGBA pages, or R8 shade sheets (0-15)
uniform sampler2D uLuma;         // 128 x 128, values 0-63
uniform sampler2D uXlat;         // 256 x 96: row = channel * 32 + base value, column = brightness
uniform int uLighting;
uniform vec3 uLightDir;
uniform float uAmbient;
uniform int uFilter;
uniform int uTransp;             // RenderOptions::transparency: 0 Smooth, 1 Clean Edges, 2 Dithered (arcade)
uniform vec3 uCamPos;
uniform int uM2Gamma;            // MAME's output curve: max((v - 64) * 255 / 191, 0)
uniform int uM2Known;            // Model 2 lighting setup known (light vector + parameter table)
uniform vec3 uM2Light;
uniform float uM2Diffuse[32];
uniform float uM2Ambient[32];
uniform float uM2SpecScale[32];
uniform float uM2SpecCtl[32];
uniform vec3 uCamFwd;            // the camera's +z in world space (MAME's specular uses the camera-space z)
const uint TEXTURED = 1u, MIRRORU = 2u, MIRRORV = 4u, TRANSLUCENT = 8u, CHECKER = 16u, DOUBLESIDED = 32u, HALFALPHA = 64u, INSIDERECT = 128u;
uniform int uClampUV;            // backgrounds: texture coordinates clamped inside the rectangle
uniform int uPcCullSign;         // which side of a PC polygon (normal from its vertex order) is the front
uniform int uCull;               // Model 2 back-face culling (MAME check_culling)
uniform int uMip;                // Model 2 texture mip levels
uniform float uFade;             // DrawItem::fade
uniform int uSideOnly;           // DrawItem::sideOnly: no pixels inside the centred 4:3 picture
uniform vec2 uSideClip;          // its centre x and half width in pixels
uniform int uRamp;              // DrawItem::ramp (-1 off)
uniform int uLook;              // RenderOptions::look (Light Gun Edition)
uniform vec3 uRampStops[5];
uniform int uGrey;               // RenderOptions::grey (the game's palette_grey_build look)
uniform vec3 uSepia[64];         // RenderOptions::sepia, 0..1
uniform int uIdMode;             // Renderer::debugIds
uniform int uTieItem;
vec3 greyLook(vec3 c) {          // grey level L = (r + g + b) / 3 (0..255) -> sepia[L / 4]
    int L = int(floor((c.r + c.g + c.b) * 255.0 / 3.0 + 0.5));
    return uSepia[clamp(L >> 2, 0, 63)];
}

int wrapCoord(int t, int size, bool mirror) {
    if (mirror) { int p = 2 * size; int m = ((t % p) + p) % p; return m < size ? m : p - 1 - m; }
    return ((t % size) + size) % size;
}
// texture packs (assets/mods.h): per set a map from the polygon's texture (PC: its page layer; arcade: sheet, rectangle
// position and size) to the pack picture's place in the atlas: x | atlas layer << 11, y, w, h (w 0 = the original)
uniform int uHasRepl;
uniform usampler2D uReplMap;
uniform sampler2DArray uRepl;
uniform int uPcFilter;           // RenderOptions::pcBilinear
uniform int uGreyRepl;           // RenderOptions::grey (pack pictures always take the sepia mapping)
uniform usampler2D uReplRef;     // arcade: the same map for the reference pictures (the original's exported colours)
ivec2 replPos = ivec2(-1);       // the map texel replEntry found (for uReplRef)
uvec4 replEntry() {
    if (uHasRepl == 0 || (vFL.x & TEXTURED) == 0u) return uvec4(0u);
    // a picture for this polygon colour only (arcade <id>-c<entry>): virtual key 2^18 + n, n + 1 in lodCoef bits 5-15
    uint vn = vLodCoef >> 5;
    if (vn != 0u) {
        int vk = 262144 + int(vn) - 1;
        replPos = ivec2(vk & 511, vk >> 9);
        uvec4 e = texelFetch(uReplMap, replPos, 0);
        if (e.z != 0u) return e;
    }
    int key;
    if (uShading == 1)
        key = ((int(vFL.y) & 1) << 17) | (((int(vRect.x) >> 5) & 63) << 11) | (((int(vRect.y) >> 5) & 31) << 6) |
              (clamp(int(round(log2(float(vRect.z)))) - 5, 0, 7) << 3) | clamp(int(round(log2(float(vRect.w)))) - 5, 0, 7);
    else
        key = int(vFL.y);
    replPos = ivec2(key & 511, (key >> 9) & 511);
    return texelFetch(uReplMap, replPos, 0);
}
vec4 replTexel(uvec4 e, ivec2 t) {
    int x = wrapCoord(t.x, int(e.z), (vFL.x & MIRRORU) != 0u), y = wrapCoord(t.y, int(e.w), (vFL.x & MIRRORV) != 0u);
    return texelFetch(uRepl, ivec3(int(e.x & 2047u) + x, int(e.y & 2047u) + y, int(e.x >> 11u)), 0);
}
// the polygon's texture coordinates (texels of the original) scaled to the picture's size; bilinear when asked or when
// the picture's size differs from the original's (a larger picture is meant to be smooth)
vec4 replSample(uvec4 e, vec2 uv, bool filt, bool clampUV) {
    vec2 sz = vec2(e.zw);
    vec2 p = uv / vec2(vRect.zw) * sz;
    if (clampUV) p = clamp(p, vec2(0.5), sz - 0.5);
    if (!filt && e.z == vRect.z && e.w == vRect.w) return replTexel(e, ivec2(floor(p)));
    p -= 0.5;
    ivec2 i = ivec2(floor(p));
    vec2 f = p - floor(p);
    vec4 a = replTexel(e, i), b = replTexel(e, i + ivec2(1, 0)), c = replTexel(e, i + ivec2(0, 1)), d = replTexel(e, i + ivec2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
)"  // (MSVC: one raw string piece at most ~16 KB; the shader continues)
R"(
// Model 2 mip levels (MAME fetch_bilinear_texel): level L of a texture at ((x - 2048) >> L) & 2047, ((y - 1024) >> L) & 1023
// in texture RAM, on the other sheet for odd levels, size >> L
float shadeAtL(ivec2 t, int L) {
    int x = wrapCoord(t.x, int(vRect.z) >> L, (vFL.x & MIRRORU) != 0u);
    int y = wrapCoord(t.y, int(vRect.w) >> L, (vFL.x & MIRRORV) != 0u);
    int ox = ((int(vRect.x) - 2048) >> L) & 2047, oy = ((int(vRect.y) - 1024) >> L) & 1023;
    return floor(texelFetch(uLayers, ivec3(ox + x, oy + y, (int(vFL.y) & 1) ^ (L & 1)), 0).r * 255.0 + 0.5);
}
float shadeAt(ivec2 t) { return shadeAtL(t, 0); }
// bilinear shade of level L; a = coverage of non-transparent texels (shade 15 is transparent on translucent polygons)
float bilinearL(vec2 uv, int L, out float a) {
    vec2 p = uv / float(1 << L) - 0.5;
    // a texture shown once (kInsideRect): the footprint stays inside it at the polygon's edges (no wrap)
    if ((vFL.x & INSIDERECT) != 0u) p = clamp(p, vec2(0.0), vec2(ivec2(vRect.zw) >> L) - 1.0);
    ivec2 i = ivec2(floor(p));
    vec2 f = p - floor(p);
    float s00 = shadeAtL(i, L), s10 = shadeAtL(i + ivec2(1, 0), L), s01 = shadeAtL(i + ivec2(0, 1), L), s11 = shadeAtL(i + ivec2(1, 1), L);
    a = mix(mix(float(s00 != 15.0), float(s10 != 15.0), f.x), mix(float(s01 != 15.0), float(s11 != 15.0), f.x), f.y);
    return mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);
}
// team (2026-11-03, Transparency "Clean Edges" / "Dithered"): the bilinear shade of the solid texels only (shade 15,
// transparent, left out of the mix: Smooth's mix pulls the edge texels toward entry 15, a brown / dark fringe round blood
// splatters, floor stains and water); a = their cover, as bilinearL's
float bilinearSolid(vec2 uv, int L, out float a) {
    vec2 p = uv / float(1 << L) - 0.5;
    if ((vFL.x & INSIDERECT) != 0u) p = clamp(p, vec2(0.0), vec2(ivec2(vRect.zw) >> L) - 1.0);
    ivec2 i = ivec2(floor(p));
    vec2 f = p - floor(p);
    float s00 = shadeAtL(i, L), s10 = shadeAtL(i + ivec2(1, 0), L), s01 = shadeAtL(i + ivec2(0, 1), L), s11 = shadeAtL(i + ivec2(1, 1), L);
    vec4 w = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y) *
             vec4(float(s00 != 15.0), float(s10 != 15.0), float(s01 != 15.0), float(s11 != 15.0));
    a = w.x + w.y + w.z + w.w;
    return a > 0.0 ? dot(w, vec4(s00, s10, s01, s11)) / a : 15.0;
}
// the cut of a translucent texture's filtered cover: Smooth / Clean Edges at half cover; Dithered by a 4 x 4 ordered (Bayer)
// threshold per screen pixel, a screen-door stipple like the Model 2's checker polygons
bool coverCut(float a) {
    if (uTransp != 2) return a < 0.5;
    ivec2 q = ivec2(gl_FragCoord.xy) & 3;
    const float kBayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    return a < (kBayer[q.y * 4 + q.x] + 0.5) / 16.0;
}
// Model 2 microtexture (MAME fetch_bilinear_texel level -1; polish2): a 128 x 128 detail texture on the other sheet at
// (utexx, utexy), its coordinates scaled by 2^(2^minlod), blended in up to ~50 % where the polygon is closer than
// level 0 (layer bits 8 = on, 9-10 = minlod, 11 = utexx / 128, 12-13 = utexy / 128: ArcadeSource::buildMeshes)
float microShade(vec2 uv) {
    int m = int(vFL.y);
    float s = exp2(float(1 << ((m >> 9) & 3)));
    vec2 p = uv * s - 0.5;
    ivec2 i = ivec2(floor(p));
    vec2 f = p - floor(p);
    int bx = ((m >> 11) & 1) * 128, by = ((m >> 12) & 3) * 128, sh = (m & 1) ^ 1;
    float s00 = floor(texelFetch(uLayers, ivec3(bx + (i.x & 127), by + (i.y & 127), sh), 0).r * 255.0 + 0.5);
    float s10 = floor(texelFetch(uLayers, ivec3(bx + ((i.x + 1) & 127), by + (i.y & 127), sh), 0).r * 255.0 + 0.5);
    float s01 = floor(texelFetch(uLayers, ivec3(bx + (i.x & 127), by + ((i.y + 1) & 127), sh), 0).r * 255.0 + 0.5);
    float s11 = floor(texelFetch(uLayers, ivec3(bx + ((i.x + 1) & 127), by + ((i.y + 1) & 127), sh), 0).r * 255.0 + 0.5);
    return mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);
}
float lightFactor() {
    if (uLighting == 0 || dot(vNrm, vNrm) < 0.01) return 1.0;
    return uAmbient + (1.0 - uAmbient) * abs(dot(normalize(vNrm), uLightDir));
}
)"  // (MSVC: the raw string's size limit)
R"(
// prepass (2026-10-27): the depth-only pass before the opaque pass (Renderer::draw): only main()'s discards, the same
// tests on the same values (a pixel this drops, main() drops too, and the other way round), no colour
uniform int uPrepass;
void prepassDiscards() {
    if (uShading == 1) {
        if (uCull == 1 && (vFL.x & DOUBLESIDED) == 0u && dot(vNrm, vWorld - uCamPos) < 0.0) discard;
        uvec4 re = replEntry();
        bool fromPack = re.z != 0u;
        if ((vFL.x & TEXTURED) != 0u && (vFL.x & TRANSLUCENT) != 0u && !fromPack) {
            if (uFilter == 1) {
                int maxLevel = max(int(round(log2(float(min(int(vRect.z), int(vRect.w)))))) - 1, 0);
                float coef = (vLodCoef & 31u) < 31u ? 350.725 * pow(0.70710678, float(vLodCoef & 15u)) : 0.0;
                float dist = coef * 4.0 * abs(dot(vNrm, vWorld - uCamPos));
                float z = 1.0 / gl_FragCoord.w;
                float lod = uMip == 1 ? (dist > 0.0 ? 2.0 * log2(z) - log2(dist) : 100.0) : 0.0;
                int L = clamp(int(floor(lod)), 0, maxLevel);
                float a;
                bilinearL(vUV, L, a);
                if (lod > 0.0 && L < maxLevel) {
                    float a2;
                    bilinearL(vUV, L + 1, a2);
                    a = mix(a, a2, fract(lod));
                }
                if (coverCut(a)) discard;
            } else if (shadeAt(ivec2(floor(vUV))) == 15.0) discard;
        }
        if (fromPack && replSample(re, vUV, uFilter == 1, false).a < 0.5) discard;
    } else {
        if (uCull == 1 && (vFL.x & DOUBLESIDED) == 0u && float(uPcCullSign) * dot(vNrm, vWorld - uCamPos) < 0.0) discard;
        if (uClampUV == 0 && 1.0 / gl_FragCoord.w > 4000.0) discard;
        uvec4 re = replEntry();
        if (re.z != 0u) {
            if (replSample(re, vUV, uPcFilter == 1, uClampUV == 1).a < 0.5) discard;
        } else if ((vFL.x & TEXTURED) != 0u) {
            vec4 c;
            if (uClampUV == 1) c = textureLod(uLayers, vec3(clamp(vUV, vec2(0.5), vec2(vRect.zw) - 0.5) / vec2(vRect.zw), float(vFL.y)), 0.0);
            else c = texture(uLayers, vec3(vUV / vec2(vRect.zw), float(vFL.y)));
            if (c.a < 0.5) discard;
        }
    }
}
)"  // (MSVC: the raw string's size limit)
R"(
void main() {
#ifdef SORT_FRAGDEPTH
    gl_FragDepth = vSortDepth >= 0.0 ? vSortDepth : gl_FragCoord.z;   // Arcade Sort: the polygon's key (vertex shader)
#endif
#ifdef KEY_BAND_FRAG
    // depthfix: the key band (vertex shader) per pixel: this pixel's own depth (1 / gl_FragCoord.w = view z) drawn to
    // the polygon's rounded key by at most uHybrid of it, the tweaks' offset on top
    if (vKeyBand >= 0.0) {
        float w0 = 1.0 / gl_FragCoord.w;
        float s = clamp(vKeyBand, w0 * (1.0 - uHybrid), w0 * (1.0 + uHybrid)) + vBandAdj;
        gl_FragDepth = clamp((uProj.x - uProj.y / max(s, 1e-3)) * 0.5 + 0.5, 0.0, 1.0);
    } else {
        gl_FragDepth = gl_FragCoord.z;
    }
#endif
    if ((vFL.x & CHECKER) != 0u && ((int(gl_FragCoord.x) + int(gl_FragCoord.y)) & 1) != 0) discard;
    if (uSideOnly == 1 && abs(gl_FragCoord.x - uSideClip.x) < uSideClip.y) discard;
    if (uPrepass == 1) {   // (colour writes off)
        prepassDiscards();
        oColor = vec4(0.0);
        return;
    }
    float lf = lightFactor();
    if (uShading == 1) {
        // Model 2 (MAME geo_parse_*, check_culling): a polygon whose normal points toward the viewer (dot(normal, point) < 0
        // in camera space) is the rear face and is not drawn unless the polygon is double-sided
        if (uCull == 1 && (vFL.x & DOUBLESIDED) == 0u && dot(vNrm, vWorld - uCamPos) < 0.0) discard;
        // polygon luma 0-255 (MAME geo_parse_*); without a known setup: the approximation, scaled to 255
        float polyLuma = 255.0 * lf;
        if (uM2Known == 1 && uLighting == 1) {
            int pi = int((vFL.x >> 8) & 31u);
            float dotl = dot(vNrm, uM2Light), dotp = dot(vNrm, vWorld - uCamPos);
            float l = dotl * dotp < 0.0 ? 0.0 : abs(dotl);
            // MAME geo_parse_*_s: specular = (2 (n . l) n - l) . z, powers by the parameter's control byte
            float spec = 0.0;
            int ctl = int(uM2SpecCtl[pi]);
            if (ctl != 0) {
                spec = max(dot(2.0 * dotl * vNrm - uM2Light, uCamFwd), 0.0);
                if ((ctl >> 1) != 0) spec *= spec;
                if ((ctl >> 2) != 0) spec *= spec;
                if (((ctl + 1) >> 3) != 0) spec *= spec;
                spec *= uM2SpecScale[pi];
            }
            polyLuma = floor(clamp(l * uM2Diffuse[pi] + uM2Ambient[pi] + spec, 0.0, 255.0));
        }
        uvec4 re = replEntry();   // a pack picture (assets/mods.h): below, after the original's colour
        bool fromPack = re.z != 0u;
        float shade = 15.0;
        if ((vFL.x & TEXTURED) != 0u) {
            bool translucent = (vFL.x & TRANSLUCENT) != 0u;
            if (uFilter == 1) {
                // level from the screen-space texel footprint (Model 2 picks it per pixel from depth and a polygon LOD);
                // down to 2x2 texels, blending with the next level like MAME
                int maxLevel = max(int(round(log2(float(min(int(vRect.z), int(vRect.w)))))) - 1, 0);
                // MAME draw_scanline_tex: level = log2(z^2 / distance), distance = coef[attr >> 27] * |normal . point| * LOD
                // (geometrizer); HOTD's coefficient table is 350.725 / sqrt(2)^(i mod 16) (entry 31 unset = 0) and LOD 4.0
                // (work RAM 0x51E8C0, sent once at boot)
                float coef = (vLodCoef & 31u) < 31u ? 350.725 * pow(0.70710678, float(vLodCoef & 15u)) : 0.0;   // (bits 5-15: texture packs)
                float dist = coef * 4.0 * abs(dot(vNrm, vWorld - uCamPos));
                float z = 1.0 / gl_FragCoord.w;
                float lod = uMip == 1 ? (dist > 0.0 ? 2.0 * log2(z) - log2(dist) : 100.0) : 0.0;
                int L = clamp(int(floor(lod)), 0, maxLevel);
                float a;
                bool solid = translucent && uTransp != 0 && !fromPack;   // (Clean Edges / Dithered: the solid texels' colour)
                shade = solid ? bilinearSolid(vUV, L, a) : bilinearL(vUV, L, a);
                if (lod > 0.0 && L < maxLevel) {
                    float a2;
                    float s2 = solid ? bilinearSolid(vUV, L + 1, a2) : bilinearL(vUV, L + 1, a2);
                    float fr = fract(lod);
                    if (solid) {   // the two levels' solid shades by their cover
                        float w0 = a * (1.0 - fr), w1 = a2 * fr;
                        shade = w0 + w1 > 0.0 ? (shade * w0 + s2 * w1) / (w0 + w1) : 15.0;
                    } else
                        shade = mix(shade, s2, fr);
                    a = mix(a, a2, fr);
                } else if (lod < 0.0 && (int(vFL.y) & 0x100) != 0) {
                    // MAME: frac = min(-mml >> minlod, 127) / 256, mml = 128 x the level
                    float fr = min(floor(floor(-lod * 128.0) / float(1 << ((int(vFL.y) >> 9) & 3))), 127.0) / 256.0;
                    shade = mix(shade, microShade(vUV), fr);
                }
                if (translucent && !fromPack && coverCut(a)) discard;
            } else {
                shade = shadeAt(ivec2(floor(vUV)));
                if (translucent && shade == 15.0 && !fromPack) discard;
            }
        }
        int b;
        if ((vFL.x & TEXTURED) != 0u) {
            int idx = int(vColor.w) * 128 + int(shade * 8.0);
            float lum = floor(texelFetch(uLuma, ivec2(idx % 128, idx / 128), 0).r * 255.0 + 0.5);
            b = min(int(lum * polyLuma / 256.0), 63);   // model2rd.ipp: lumaram[lumabase + (t >> 1)] * luma / 256
        } else {
            b = int(polyLuma) >> 2;                     // untextured: luma >> 2
        }
        oColor = vec4(texelFetch(uXlat, ivec2(b, int(vColor.r)), 0).r,
                      texelFetch(uXlat, ivec2(b, 32 + int(vColor.g)), 0).r,
                      texelFetch(uXlat, ivec2(b, 64 + int(vColor.b)), 0).r, 1.0);
        // cosmods: the rainbow blood (entry 1023), each channel between its rows 0 and 31 at this brightness: smooth.
        // Its 5-bit values through the rows flickered (user's clip 2026-11-03): rows between are other colours (the
        // creatures' swapped rows), so a channel crossing one jumped
        if (vRainbowPal > 0.5) {
            vec3 t = oColor.rgb;   // (angle: GLSL ES indexes a fragment output by constants only; it failed to compile)
            for (int c = 0; c < 3; ++c) {
                float lo = texelFetch(uXlat, ivec2(b, 32 * c), 0).r, hi = texelFetch(uXlat, ivec2(b, 32 * c + 31), 0).r;
                t[c] = lo + (hi - lo) * vRainbow[c];
            }
            oColor.rgb = t;
        }
        // rainbow2: the creatures' blood rows (uBloodRows: the rows GGSET set by the blood colour) in the rainbow, every
        // shade (the pale pink inside the wounds too), as tintBloodRows does with the custom colour (game/view.cpp): per
        // brightness the grey under the row (its lowest channel) kept, the colour above it the rainbow's (its brightest
        // channel scaled to 1), times that brightest channel. Per pixel and per item: each splash / body its own hue,
        // nothing re-uploaded. Not with Creature Blood Off (uBodyHue 6..7: the wounds as painted) or the grey look
        int bloodRows = 0;   // (the channels coloured here)
        if ((uRainbow == 1 || uRainbow == 2) && uBloodRows != 0 && uGrey == 0 && uBodyHue < 6.0) {
            float top = max(max(vRainbow.r, vRainbow.g), max(vRainbow.b, 1e-3));
            vec3 t = oColor.rgb;   // (angle: a fragment output indexed by constants only)
            for (int c = 0; c < 3; ++c) {
                int row = int(vColor[c]);
                if (row < 32 && ((uBloodRows >> row) & 1) != 0) {
                    float x0 = texelFetch(uXlat, ivec2(b, row), 0).r, x1 = texelFetch(uXlat, ivec2(b, 32 + row), 0).r,
                          x2 = texelFetch(uXlat, ivec2(b, 64 + row), 0).r;
                    float lo = min(x0, min(x1, x2)), hi = max(x0, max(x1, x2));
                    t[c] = (lo + (hi - lo) * vRainbow[c] / top) * top;
                    ++bloodRows;
                }
            }
            oColor.rgb = t;
        }
        // a pack picture: the reference picture holds the original's colours as the export would write them here (full
        // light, the load-time colour table, the palette entry the loader found closest to the picture). k = this
        // pixel's original brightness (its lighting, palette animation, the colour rows the game swaps: GGSET's bright
        // red blood) over the reference's. Map y bit 15 (the picture lies close to its reference: an edit of this
        // chapter's export): the picture's change from the reference, times k, is added to the original colour (every
        // polygon colour of a shared texture kept; an unchanged export draws as the original). Else (repainted, or
        // exported in another chapter's colours): the picture's colours times k. Its alpha replaces the translucency
        if (fromPack) {
            vec4 c = replSample(re, vUV, uFilter == 1, false);
            if (c.a < 0.5) discard;
            uvec4 er = texelFetch(uReplRef, replPos, 0);
            vec3 ref = er.z != 0u ? replSample(er, vUV, uFilter == 1, false).rgb : vec3(0.0);
            const vec3 Y = vec3(0.299, 0.587, 0.114);
            float k = min((dot(oColor.rgb, Y) + 0.02) / (dot(ref, Y) + 0.02), 4.0);
            vec3 rgb = er.z == 0u ? min(c.rgb * (polyLuma / 255.0), vec3(1.0)) :   // (no reference: a custom model's picture, lit)
                       (re.y & 0x8000u) != 0u ? clamp(oColor.rgb + (c.rgb - ref) * k, 0.0, 1.0) : clamp(c.rgb * k, 0.0, 1.0);
            if (uGreyRepl == 1) rgb = greyLook(rgb);
            if (uM2Gamma == 1) rgb = max((floor(rgb * 255.0 + 0.5) - 64.0) / 191.0, 0.0);
            oColor = vec4(rgb * uFade, 1.0);
            if (uIdMode == 1) oColor = vec4(float(uTieItem & 255) / 255.0, float((uTieItem >> 8) & 255) / 255.0, 0.0, 1.0);
            return;
        }
        // the game's grey look (palette_grey_build 0x1A400) rewrites the colour table itself: entry (base, luma) of
        // channel c = sepia[(curve_r + curve_g + curve_b)(base, luma) / 3 / 4][c], uploaded to 0x1810000 like the curves
        // (color_table_upload_all, 0x80 bytes a base row). Each channel keeps its own base value (not the grey of the
        // final colour), and MAME's output curve still follows: the sepia ramp's dark end (~64) is black on screen
        if (uGrey == 1) {
            ivec3 base = ivec3(vColor.rgb);
            vec3 sep = oColor.rgb;   // (angle: GLSL ES indexes a fragment output by constants only)
            for (int c = 0; c < 3; ++c) {
                int s = 0;
                for (int k = 0; k < 3; ++k) s += int(floor(texelFetch(uXlat, ivec2(b, k * 32 + base[c]), 0).r * 255.0 + 0.5));
                sep[c] = uSepia[clamp((s / 3) >> 2, 0, 63)][c];
            }
            oColor.rgb = sep;
        }
        if (uM2Gamma == 1) oColor.rgb = max((floor(oColor.rgb * 255.0 + 0.5) - 64.0) / 191.0, 0.0);
        // cosmods: Rainbow Blood's creature wounds, on the colour as shown (after the monitor curve: before it the reds
        // are paler, (140, 40, 30) on screen is (168, 94, 86) here)
        // (rainbow2: only a fallback now, for the pixels the blood rows did not colour in all three channels)
        if (uRainbow != 0 && uBodyHue >= 0.0 && uBodyHue < 4.0 && uGrey == 0 && bloodRows < 3) oColor.rgb = woundTint(oColor.rgb);
        oColor.rgb *= uFade;   // draw_fade_begin scales the geometrizer's colour table (no blending on the Model 2)
    } else {
        // PC (THOTD.EXE 0x577870 / 0x577CA0): no lighting; textured polygons have diffuse white, untextured ones a flat
        // palette color; single-sided polygons are culled (0x578F60), bit 23 polygons are drawn at alpha 0x7F
        if (uCull == 1 && (vFL.x & DOUBLESIDED) == 0u && float(uPcCullSign) * dot(vNrm, vWorld - uCamPos) < 0.0) discard;
        // the PC's far distance: 4000 (draw context +0x14 / viewport +0x44, read from the running game); THOTD.EXE drops
        // whole polygons by their sort key and objects by their bounding sphere, here per pixel (backgrounds excepted)
        if (uClampUV == 0 && 1.0 / gl_FragCoord.w > 4000.0) discard;
        vec4 c;
        uvec4 re = replEntry();
        bool fromPack = re.z != 0u;
        if (fromPack) {   // a pack picture (assets/mods.h) for this page
            c = replSample(re, vUV, uPcFilter == 1, uClampUV == 1);
            if (c.a < 0.5) discard;
        } else if ((vFL.x & TEXTURED) != 0u) {
            // the sky (backgrounds): no wrap into the page's other side at face edges (the seams), base level only
            if (uClampUV == 1) c = textureLod(uLayers, vec3(clamp(vUV, vec2(0.5), vec2(vRect.zw) - 0.5) / vec2(vRect.zw), float(vFL.y)), 0.0);
            else c = texture(uLayers, vec3(vUV / vec2(vRect.zw), float(vFL.y)));
            if (c.a < 0.5) discard;
            // cosmods: Rainbow Blood's marked blood texels (alpha 250: white-tinted at the load), times the rainbow colour
            if (uRainbow != 0 && uRainbow != 3 && c.a > 0.95 && c.a < 0.999) c.rgb = mix(c.rgb, c.rgb * vRainbow, clamp((1.0 - c.a) * 51.0, 0.0, 1.0));
        } else {
            c = vec4(vColor) / 255.0;
        }
        // the game's fade (PC: object fade -> D3DTLVERTEX diffuse alpha, UNVERIFIED (teaser) against the PC driver)
        if (uGrey == 1 || (fromPack && uGreyRepl == 1)) c.rgb = greyLook(c.rgb);   // UNVERIFIED (teaser): the PC's own grey look not traced
        oColor = vec4(c.rgb, ((vFL.x & HALFALPHA) != 0u ? 127.0 / 255.0 : 1.0) * uFade);
    }
    if (uLook == 1) {   // Night Vision: the light level as phosphor green, brighter (amplified)
        float l = clamp(dot(oColor.rgb, vec3(0.299, 0.587, 0.114)) * 1.7 + 0.04, 0.0, 1.0);
        oColor.rgb = vec3(l * 0.22, l, l * 0.30);
    } else if (uLook == 2) {   // Wireframe: neon green lines (the polygons drawn as lines)
        oColor = vec4(0.20, 1.0, 0.40, 1.0);
    } else if (uLook == 3) {   // VHS: washed-out, warm colours with lifted blacks
        float l = dot(oColor.rgb, vec3(0.299, 0.587, 0.114));
        oColor.rgb = clamp(mix(oColor.rgb, vec3(l), 0.35) * vec3(1.06, 1.0, 0.90) * 0.92 + 0.05, 0.0, 1.0);
    }
    if (uRamp >= 0 && !(uLook == 2 && uRamp < 2)) {   // 4P: the colours' level (half luminance, half the strongest channel) through the seat's ramp
        float t = clamp(0.5 * dot(oColor.rgb, vec3(0.299, 0.587, 0.114)) + 0.5 * max(oColor.r, max(oColor.g, oColor.b)), 0.0, 1.0) * 4.0;
        int k = min(3, int(t));
        oColor.rgb = mix(uRampStops[k], uRampStops[k + 1], t - float(k));
    }
    if (uIdMode == 1)   // Renderer::debugIds: the draw item's index
        oColor = vec4(float(uTieItem & 255) / 255.0, float((uTieItem >> 8) & 255) / 255.0, 0.0, 1.0);
    if (uIdMode == 3) {   // its sort key z x 64 (24 bits)
        int k = int(clamp(vSortKz * 64.0, 0.0, 16777215.0));
        oColor = vec4(float(k & 255) / 255.0, float((k >> 8) & 255) / 255.0, float((k >> 16) & 255) / 255.0, 1.0);
    }
    if (uIdMode == 2)   // the triangle (stage buffer order) and its sort mode
        oColor = vec4(float(vSortDbg.x & 255) / 255.0, float((vSortDbg.x >> 8) & 255) / 255.0, float(((vSortDbg.x >> 16) & 63) | (vSortDbg.y << 6)) / 255.0, 1.0);
}
)";

gl::GLuint compile(gl::GLenum type, const char* src, std::string& err)
{
    gl::GLuint s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &src, nullptr);
    gl::CompileShader(s);
    gl::shaderLog(s, "renderer");
    gl::GLint ok = 0;
    gl::GetShaderiv(s, gl::COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        gl::GetShaderInfoLog(s, sizeof log, nullptr, log);
        err = std::string("shader compile failed: ") + log;
        gl::DeleteShader(s);
        return 0;
    }
    return s;
}

void multiply(const float a[16], const float b[16], float out[16])   // column-major: out = a * b
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = s;
        }
}

} // namespace

// Polygons seen through a translucent texture's holes (user play-test 2026-10-16: Sophie's eyes, sofatama02a 0x60F):
// an untextured iris quad lies 0.013 units IN FRONT of the eye texture (whose iris texels are shade 15, transparent)
// and tilted ~11 degrees to it. The Model 2 sorts polygons and draws the eye texture over the quad, so the green shows
// only through the holes; our depth buffer let the quad cover the eye (a green blob that came and went as the head
// turned). An untextured triangle whose centroid lies within 0.05 of a translucent textured triangle of the same mesh
// (normals within ~25 degrees, the centroid inside it) is flattened onto that triangle's plane and moved to the front
// of the mesh, so markCoplanarLayers puts the texture one layer above it.
static int flattenUnderTranslucent(Mesh& me)
{
    auto plane = [&](size_t i, float n[3], float& d) {
        const float* a = me.vertices[i].pos;
        const float* b = me.vertices[i + 1].pos;
        const float* c = me.vertices[i + 2].pos;
        const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        n[0] = e1[1] * e2[2] - e1[2] * e2[1];
        n[1] = e1[2] * e2[0] - e1[0] * e2[2];
        n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l <= 1e-6f) return false;
        for (int k = 0; k < 3; ++k) n[k] /= l;
        d = n[0] * a[0] + n[1] * a[1] + n[2] * a[2];
        return true;
    };
    auto inside = [&](size_t i, const float n[3], const float q[3]) {
        for (int k = 0; k < 3; ++k) {
            const float* a = me.vertices[i + size_t(k)].pos;
            const float* b = me.vertices[i + size_t((k + 1) % 3)].pos;
            const float e[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w[3] = {q[0] - a[0], q[1] - a[1], q[2] - a[2]};
            const float c[3] = {e[1] * w[2] - e[2] * w[1], e[2] * w[0] - e[0] * w[2], e[0] * w[1] - e[1] * w[0]};
            if (c[0] * n[0] + c[1] * n[1] + c[2] * n[2] <= 0.0f) return false;
        }
        return true;
    };
    std::vector<size_t> covers;
    for (size_t i = 0; i + 2 < me.vertices.size(); i += 3)
        if ((me.vertices[i].flags & (kTextured | kTranslucent)) == (kTextured | kTranslucent)) covers.push_back(i);
    if (covers.empty()) return 0;
    std::vector<Vertex> under, rest;
    int n = 0;
    for (size_t i = 0; i + 2 < me.vertices.size(); i += 3) {
        bool moved = false;
        float tn[3], td;
        if (!(me.vertices[i].flags & kTextured) && plane(i, tn, td)) {
            float cen[3];
            for (int c = 0; c < 3; ++c) cen[c] = (me.vertices[i].pos[c] + me.vertices[i + 1].pos[c] + me.vertices[i + 2].pos[c]) / 3;
            for (size_t j : covers) {
                float un[3], ud;
                if (!plane(j, un, ud) || un[0] * tn[0] + un[1] * tn[1] + un[2] * tn[2] < 0.9f) continue;
                if (std::abs(un[0] * cen[0] + un[1] * cen[1] + un[2] * cen[2] - ud) > 0.05f || !inside(j, un, cen)) continue;
                for (int k = 0; k < 3; ++k) {
                    Vertex v = me.vertices[i + size_t(k)];
                    const float s = un[0] * v.pos[0] + un[1] * v.pos[1] + un[2] * v.pos[2] - ud;
                    for (int c = 0; c < 3; ++c) v.pos[c] -= s * un[c];
                    under.push_back(v);
                }
                moved = true;
                ++n;
                break;
            }
        }
        if (!moved)
            rest.insert(rest.end(), me.vertices.begin() + std::ptrdiff_t(i), me.vertices.begin() + std::ptrdiff_t(i + 3));
    }
    if (n > 0) {
        under.insert(under.end(), rest.begin(), rest.end());
        me.vertices = std::move(under);
    }
    return n;
}

int markCoplanarLayers(Model& model)
{
    // perf: read once (was per model / per triangle: getenv was most of a chapter's layer time); thread-safe
    // (markCoplanarLayersAll runs models on several threads)
    static std::atomic<int> flattened{0};
    static const bool noFlatten = std::getenv("HOTD_NO_FLATTEN") != nullptr, logFlatten = std::getenv("HOTD_LOG_FLATTEN") != nullptr;
    static const char* const coplanarKey = std::getenv("HOTD_COPLANAR_KEY");
    if (!noFlatten)
        for (Mesh& me : model.meshes)
            if (int f = flattenUnderTranslucent(me)) {
                flattened += f;
                if (logFlatten)
                    std::fprintf(stderr, "flatten: %s (%#x): %d triangles under a translucent texture (total %d)\n",
                                 model.name.c_str(), model.key, f, flattened.load());
            }
    struct Tri { float p[3][3], n[3], d; int layer; };
    auto inside = [](const Tri& t, const float* q) {   // q inside t, projected along t's normal (strictly, small margin)
        for (int k = 0; k < 3; ++k) {
            const float* a = t.p[k];
            const float* b = t.p[(k + 1) % 3];
            const float e[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w[3] = {q[0] - a[0], q[1] - a[1], q[2] - a[2]};
            const float c[3] = {e[1] * w[2] - e[2] * w[1], e[2] * w[0] - e[0] * w[2], e[0] * w[1] - e[1] * w[0]};
            if (c[0] * t.n[0] + c[1] * t.n[1] + c[2] * t.n[2] <= 1e-4f) return false;
        }
        return true;
    };
    // polish3: overlap by seven sample points of each triangle (the centroid, the corners and the edge midpoints, each
    // pulled 20 % toward the centroid so that triangles sharing an edge never count), not by the centroid alone: the
    // mansion facade's window panes (niwa01_03b / 03c), room_2b / room_2ari's, room_n3a's and room_5b's wall decals and
    // the creatures' _dam chest layers overlap their base without either centroid inside the other, and z-fought
    // (texture flicker as the camera moved, user play-test 2026-10-16; hotd --render with HOTD_ZFIGHT=1 finds them)
    auto overlap = [&](const Tri& a, const Tri& b) {
        auto samples = [&](const Tri& s, const Tri& o) {
            float c[3];
            for (int k = 0; k < 3; ++k) c[k] = (s.p[0][k] + s.p[1][k] + s.p[2][k]) / 3;
            if (inside(o, c)) return true;
            for (int v = 0; v < 3; ++v) {
                float q[3], m[3];
                for (int k = 0; k < 3; ++k) {
                    q[k] = c[k] + (s.p[v][k] - c[k]) * 0.8f;
                    m[k] = c[k] + ((s.p[v][k] + s.p[(v + 1) % 3][k]) * 0.5f - c[k]) * 0.8f;
                }
                if (inside(o, q) || inside(o, m)) return true;
            }
            return false;
        };
        return samples(a, b) || samples(b, a);
    };
    int marked = 0;
    for (Mesh& me : model.meshes) {
        std::vector<Tri> tris;
        tris.reserve(me.vertices.size() / 3);
        // quantized plane -> triangles (perf: hashed by the packed normal key; was a std::map of 4-int keys)
        std::unordered_map<int, std::vector<int>> buckets;
        auto bucketKey = [](int x, int y, int z) { return ((x + 16) << 10) | ((y + 16) << 5) | (z + 16); };
        for (size_t i = 0; i + 2 < me.vertices.size(); i += 3) {
            Tri t{};
            for (int k = 0; k < 3; ++k)
                for (int c = 0; c < 3; ++c) t.p[k][c] = me.vertices[i + size_t(k)].pos[c];
            const float e1[3] = {t.p[1][0] - t.p[0][0], t.p[1][1] - t.p[0][1], t.p[1][2] - t.p[0][2]};
            const float e2[3] = {t.p[2][0] - t.p[0][0], t.p[2][1] - t.p[0][1], t.p[2][2] - t.p[0][2]};
            t.n[0] = e1[1] * e2[2] - e1[2] * e2[1];
            t.n[1] = e1[2] * e2[0] - e1[0] * e2[2];
            t.n[2] = e1[0] * e2[1] - e1[1] * e2[0];
            const float l = std::sqrt(t.n[0] * t.n[0] + t.n[1] * t.n[1] + t.n[2] * t.n[2]);
            const int idx = int(tris.size());
            if (l > 1e-6f) {
                for (float& v : t.n) v /= l;
                t.d = t.n[0] * t.p[0][0] + t.n[1] * t.p[0][1] + t.n[2] * t.p[0][2];
                float cen[3];
                for (int c = 0; c < 3; ++c) cen[c] = (t.p[0][c] + t.p[1][c] + t.p[2][c]) / 3;
                // x 8: normals within the 0.995 test (|n1 - n2| < 0.1) are at most one bucket apart
                const std::array<int, 3> nk = {int(std::lround(t.n[0] * 8)), int(std::lround(t.n[1] * 8)), int(std::lround(t.n[2] * 8))};
                // polish3: no plane-offset key (was round(d x 4), neighbours +-1): d = n . p moves by the tilt times the
                // distance from the model's origin, so a pane tilted 0.005 on the mansion facade (niwa01_03b, 10 units
                // from the origin: d -10.76 against the wall's -10.00, 0.035 apart at its centroid) fell three buckets
                // away and z-fought; the 0.05 plane test below does the real check
                // the neighbouring normal buckets too (polish2): a normal component near a rounding edge put the
                // zombie's eyeball decal (zonbichana_kao 0x781, normal 0.878 -0.469 -0.094) and the face under it (0.917
                // -0.391 -0.081) in buckets two apart at x 16, so the eyeball stayed on the face's layer and the depth test hid most of
                // it behind the black socket ("huge pupils"; MAME snap 0028: a white eyeball)
                for (int bn = 0; bn < 27; ++bn) {
                    const int ox = bn % 3 - 1, oy = bn / 3 % 3 - 1, oz = bn / 9 - 1;
                    auto b = buckets.find(bucketKey(nk[0] + ox, nk[1] + oy, nk[2] + oz));
                    if (b == buckets.end()) continue;
                    for (int j : b->second) {
                        const Tri& u = tris[size_t(j)];
                        if (u.n[0] * t.n[0] + u.n[1] * t.n[1] + u.n[2] * t.n[2] < 0.995f) continue;
                        // within 0.05 either side; up to 0.1 when the later triangle lies in front (along the
                        // outward normal; polish3: niwa01_03b's translucent window panes 0.052 over the facade)
                        const float off = u.n[0] * cen[0] + u.n[1] * cen[1] + u.n[2] * cen[2] - u.d;
                        if (off > 0.1f || off < -0.05f) continue;
                        if (overlap(t, u)) t.layer = std::max(t.layer, std::min(u.layer + 1, 3));
                    }
                }
                buckets[bucketKey(nk[0], nk[1], nk[2])].push_back(idx);
                if (const char* ck = coplanarKey; ck && std::strtoul(ck, nullptr, 16) == model.key)
                    for (int j = 0; j < idx; ++j) {
                        const Tri& u = tris[size_t(j)];
                        const float dot = u.n[0] * t.n[0] + u.n[1] * t.n[1] + u.n[2] * t.n[2];
                        if (std::abs(dot) < 0.98f) continue;
                        const float dist = u.n[0] * cen[0] + u.n[1] * cen[1] + u.n[2] * cen[2] - u.d;
                        if (std::abs(dist) > 2.0f || !overlap(t, u)) continue;
                        std::fprintf(stderr, "coplanar %#x: tri %d over %d: dot %.4f dist %.4f layers %d/%d flags %x/%x n %.3f %.3f %.3f / %.3f %.3f %.3f d %.3f / %.3f\n", model.key, idx, j,
                                     dot, dist, t.layer, u.layer, me.vertices[i].flags, me.vertices[size_t(j) * 3].flags,
                                     t.n[0], t.n[1], t.n[2], u.n[0], u.n[1], u.n[2], t.d, u.d);
                    }
            }
            tris.push_back(t);
            if (t.layer > 0) {
                ++marked;
                for (int k = 0; k < 3; ++k) {
                    uint16_t& f = me.vertices[i + size_t(k)].flags;
                    f = uint16_t((f & ~(3u << kLayerShift)) | (unsigned(t.layer) << kLayerShift));
                }
            }
        }
    }
    return marked;
}

int markCoplanarLayersAll(std::map<uint32_t, Model>& models, bool background)
{
    std::vector<Model*> work;
    work.reserve(models.size());
    for (auto& [key, m] : models) work.push_back(&m);
    std::atomic<size_t> next{0};
    std::atomic<int> marked{0};
    auto run = [&] {
        for (size_t i = next++; i < work.size(); i = next++)
            marked += markCoplanarLayers(*work[i]);
    };
    const unsigned hw = std::clamp(std::thread::hardware_concurrency(), 1u, 8u), n = background ? std::max(1u, hw / 2) : hw;
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < n && work.size() > 64; ++t) pool.emplace_back(run);
    run();
    for (std::thread& t : pool) t.join();
    return marked.load();
}

void Camera::basis(float right[3], float up[3], float fwd[3]) const
{
    fwd[0] = std::sin(yaw) * std::cos(pitch);
    fwd[1] = std::sin(pitch);
    fwd[2] = std::cos(yaw) * std::cos(pitch);
    right[0] = std::cos(yaw);
    right[1] = 0;
    right[2] = -std::sin(yaw);
    // left-handed: right x up = forward, so up = forward x right
    up[0] = fwd[1] * right[2] - fwd[2] * right[1];
    up[1] = fwd[2] * right[0] - fwd[0] * right[2];
    up[2] = fwd[0] * right[1] - fwd[1] * right[0];
}

bool Renderer::init(std::string& err)
{
    // earlyz: Arcade Sort's keys from the vertex depth; HOTD_SORT_FRAGDEPTH=1 (and HOTD_SORT_EXACT) the depthcmp path
    // (gl_FragDepth on every pixel: the GPU's early depth test off), kept for comparisons
    sortFragDepth_ = std::getenv("HOTD_SORT_FRAGDEPTH") != nullptr || std::getenv("HOTD_SORT_EXACT") != nullptr;
    auto withDefines = [&](const char* src) {
        std::string s = src;
        if (sortFragDepth_) s.insert(s.find('\n') + 1, "#define SORT_FRAGDEPTH 1\n");
        return s;
    };
    auto link = [&](const char* extra, std::string& e) -> gl::GLuint {
        std::string vsSrc = withDefines(kVertexShader), fsSrc = withDefines(kFragmentShader);
        if (extra) {
            vsSrc.insert(vsSrc.find('\n') + 1, extra);
            fsSrc.insert(fsSrc.find('\n') + 1, extra);
        }
        gl::GLuint vs = compile(gl::VERTEX_SHADER, vsSrc.c_str(), e);
        if (!vs) return 0;
        gl::GLuint fs = compile(gl::FRAGMENT_SHADER, fsSrc.c_str(), e);
        if (!fs) return 0;
        gl::GLuint pr = gl::CreateProgram();
        gl::AttachShader(pr, vs);
        gl::AttachShader(pr, fs);
        gl::LinkProgram(pr);
        gl::programLog(pr, "renderer");
        gl::DeleteShader(vs);
        gl::DeleteShader(fs);
        gl::GLint ok = 0;
        gl::GetProgramiv(pr, gl::LINK_STATUS, &ok);
        if (!ok) {
            char log[4096];
            gl::GetProgramInfoLog(pr, sizeof log, nullptr, log);
            e = std::string("shader link failed: ") + log;
            gl::DeleteProgram(pr);
            return 0;
        }
        gl::UseProgram(pr);
        gl::Uniform1i(gl::GetUniformLocation(pr, "uLayers"), 0);
        gl::Uniform1i(gl::GetUniformLocation(pr, "uLuma"), 1);
        gl::Uniform1i(gl::GetUniformLocation(pr, "uXlat"), 2);
        gl::Uniform1i(gl::GetUniformLocation(pr, "uPalAnim"), 3);
        gl::Uniform1i(gl::GetUniformLocation(pr, "uSetPal"), 4);
        gl::Uniform1i(gl::GetUniformLocation(pr, "uReplMap"), 5);
        gl::Uniform1i(gl::GetUniformLocation(pr, "uRepl"), 6);
        gl::Uniform1i(gl::GetUniformLocation(pr, "uReplRef"), 7);
        gl::Uniform1i(gl::GetUniformLocation(pr, "uSortKeys"), 8);
        return pr;
    };
    // depthfix: Depth Buffer's key band per pixel (KEY_BAND_FRAG: gl_FragDepth, so its own program: the other modes
    // keep the early depth test). Used only when every uniform has the same location in both programs (the draw sends
    // them by the main program's locations); else the band stays per vertex
    std::string bandErr;
    programBand_ = link("#define KEY_BAND_FRAG 1\n", bandErr);
    if (!programBand_)
        std::fprintf(stderr, "renderer: key band program: %s (per vertex instead)\n", bandErr.c_str());
    program_ = link(nullptr, err);
    if (!program_) return false;
    if (programBand_) {
        for (const char* src : {kVertexShader, kFragmentShader})
            for (const char* q = std::strstr(src, "uniform "); q && programBand_; q = std::strstr(q + 8, "uniform ")) {
                const char* nm = std::strchr(q + 8, ' ');
                if (!nm) break;
                std::string name;
                for (++nm; *nm && (std::isalnum((unsigned char)*nm) || *nm == '_'); ++nm) name += *nm;
                if (!name.empty() && gl::GetUniformLocation(program_, name.c_str()) != gl::GetUniformLocation(programBand_, name.c_str())) {
                    std::fprintf(stderr, "renderer: key band program: uniform %s at another place (per vertex instead)\n", name.c_str());
                    gl::DeleteProgram(programBand_);
                    programBand_ = 0;
                }
            }
        gl::UseProgram(program_);
    }
    uSort_ = gl::GetUniformLocation(program_, "uSort");
    uHybrid_ = gl::GetUniformLocation(program_, "uHybrid");
    uKeyless_ = gl::GetUniformLocation(program_, "uKeyless");
    uProj_ = gl::GetUniformLocation(program_, "uProj");
    uBiasScale_ = gl::GetUniformLocation(program_, "uBiasScale");
    uSortItem_ = gl::GetUniformLocation(program_, "uSortItem");
    uPrepass_ = gl::GetUniformLocation(program_, "uPrepass");
    uHasRepl_ = gl::GetUniformLocation(program_, "uHasRepl");
    uPcFilter_ = gl::GetUniformLocation(program_, "uPcFilter");
    uGreyRepl_ = gl::GetUniformLocation(program_, "uGreyRepl");
    gl::GenTextures(1, &palAnim_);
    setPaletteAnimation(nullptr);
    makeSpareTextures();
    uViewProj_ = gl::GetUniformLocation(program_, "uViewProj");
    uOffset_ = gl::GetUniformLocation(program_, "uOffset");
    uRot_ = gl::GetUniformLocation(program_, "uRot");
    uUVScroll_ = gl::GetUniformLocation(program_, "uUVScroll");
    uShading_ = gl::GetUniformLocation(program_, "uShading");
    uLighting_ = gl::GetUniformLocation(program_, "uLighting");
    uLightDir_ = gl::GetUniformLocation(program_, "uLightDir");
    uAmbient_ = gl::GetUniformLocation(program_, "uAmbient");
    uFilter_ = gl::GetUniformLocation(program_, "uFilter");
    uTransp_ = gl::GetUniformLocation(program_, "uTransp");
    uCamPos_ = gl::GetUniformLocation(program_, "uCamPos");
    uM2Known_ = gl::GetUniformLocation(program_, "uM2Known");
    uM2Gamma_ = gl::GetUniformLocation(program_, "uM2Gamma");
    uCull_ = gl::GetUniformLocation(program_, "uCull");
    uMip_ = gl::GetUniformLocation(program_, "uMip");
    uPcCullSign_ = gl::GetUniformLocation(program_, "uPcCullSign");
    uClampUV_ = gl::GetUniformLocation(program_, "uClampUV");
    uDepthBias_ = gl::GetUniformLocation(program_, "uDepthBias");
    uFloorY_ = gl::GetUniformLocation(program_, "uFloorY");
    uFloorPull_ = gl::GetUniformLocation(program_, "uFloorPull");
    uPush_ = gl::GetUniformLocation(program_, "uPush");
    uFade_ = gl::GetUniformLocation(program_, "uFade");
    uSideOnly_ = gl::GetUniformLocation(program_, "uSideOnly");
    uSideClip_ = gl::GetUniformLocation(program_, "uSideClip");
    uFlat_ = gl::GetUniformLocation(program_, "uFlat");
    uTie_ = gl::GetUniformLocation(program_, "uTie");
    uTieItem_ = gl::GetUniformLocation(program_, "uTieItem");
    uIdMode_ = gl::GetUniformLocation(program_, "uIdMode");
    uGrey_ = gl::GetUniformLocation(program_, "uGrey");
    uRainbow_ = gl::GetUniformLocation(program_, "uRainbow");   // (cosmods)
    uRainbowPhase_ = gl::GetUniformLocation(program_, "uRainbowPhase");
    uBloodRgb_ = gl::GetUniformLocation(program_, "uBloodRgb");
    uRainbowTable_ = gl::GetUniformLocation(program_, "uRainbowTable");
    uRainbowGame_ = gl::GetUniformLocation(program_, "uRainbowGame");
    uRainbowSync_ = gl::GetUniformLocation(program_, "uRainbowSync");
    uBloodRows_ = gl::GetUniformLocation(program_, "uBloodRows");   // (rainbow2)
    uSepia_ = gl::GetUniformLocation(program_, "uSepia");
    uM2Light_ = gl::GetUniformLocation(program_, "uM2Light");
    uM2Diffuse_ = gl::GetUniformLocation(program_, "uM2Diffuse");
    uM2Ambient_ = gl::GetUniformLocation(program_, "uM2Ambient");
    uM2SpecScale_ = gl::GetUniformLocation(program_, "uM2SpecScale");
    uM2SpecCtl_ = gl::GetUniformLocation(program_, "uM2SpecCtl");
    uCamFwd_ = gl::GetUniformLocation(program_, "uCamFwd");
    return true;
}

static int gDrawDepth = kDepthBuffer;
void setDrawDepth(int mode) { gDrawDepth = mode == kArcadeSort ? kArcadeSort : kDepthBuffer; }
int drawDepth() { return gDrawDepth; }
static bool gPrepass = false;   // (the game sets it by the graphics: main.cpp depthPrepassAuto)
void setDepthPrepass(bool on) { gPrepass = on; }
bool depthPrepass()
{
    static const char* env = std::getenv("HOTD_PREPASS");
    return env ? env[0] == '1' : gPrepass;
}

void Renderer::shutdown()
{
    while (!stages_.empty()) release(*stages_.begin()->first);
    if (palAnim_) gl::DeleteTextures(1, &palAnim_);
    palAnim_ = 0;
    for (gl::GLuint* t : {&spareUint_, &spareFloat_, &spareArray_})
        if (*t) { gl::DeleteTextures(1, t); *t = 0; }
}

void Renderer::setPaletteAnimation(const uint16_t* entries)
{
    static const uint16_t none[1024] = {};
    gl::ActiveTexture(gl::TEXTURE0 + 3);
    gl::BindTexture(gl::TEXTURE_2D, palAnim_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::R16UI, 1024, 1, 0, gl::RED_INTEGER, gl::UNSIGNED_SHORT, entries ? entries : none);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
    gl::ActiveTexture(gl::TEXTURE0);
}

// compat (2026-10-29): NVIDIA's debug output: "texture object (0) bound to texture image unit 5 / 6 / 7 does not have a
// defined base level" every frame without a texture pack (uReplMap / uRepl / uReplRef: read only with uHasRepl, but
// the samplers are live). An empty or incomplete unit under an integer sampler is undefined in OpenGL 3.3; these
// complete 1x1 textures (zero, NEAREST: integer textures are incomplete with LINEAR) take the empty units
void Renderer::makeSpareTextures()
{
    static const uint16_t zero4[4] = {};
    static const uint8_t zero8[4] = {};
    auto make = [](gl::GLuint& t, gl::GLenum target) {
        gl::GenTextures(1, &t);
        gl::BindTexture(target, t);
        gl::TexParameteri(target, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
        gl::TexParameteri(target, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
    };
    gl::ActiveTexture(gl::TEXTURE0);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    make(spareUint_, gl::TEXTURE_2D);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA16UI, 1, 1, 0, gl::RGBA_INTEGER, gl::UNSIGNED_SHORT, zero4);
    make(spareFloat_, gl::TEXTURE_2D);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, 1, 1, 0, gl::RGBA, gl::UNSIGNED_BYTE, zero8);
    make(spareArray_, gl::TEXTURE_2D_ARRAY);
    gl::TexImage3D(gl::TEXTURE_2D_ARRAY, 0, gl::RGBA8, 1, 1, 1, 0, gl::RGBA, gl::UNSIGNED_BYTE, zero8);
    gl::BindTexture(gl::TEXTURE_2D_ARRAY, 0);
    gl::BindTexture(gl::TEXTURE_2D, 0);
}

void Renderer::upload(const StageData& stage)
{
    if (stages_.count(&stage)) return;
    GpuStage g;
    // texture packs (assets/mods.h): the pictures for this stage, while it still has its texels
    StageReplacements repl;
    if (!texturePacks().empty() && !std::getenv("HOTD_NO_MODS")) repl = texturePacks().build(stage);
    g.replVirtual = repl.virtualKeys;
    g.modelDepth = repl.modelDepth;
    // faceatt (2026-10-29, user: "clean face"): the human heads draw per pixel (keyless) even under Arcade Sort. Their
    // hair polygons' sort keys put a black hair piece over the face as the head turns (the attract's fleeing researcher
    // onnatama_o00a, over her left brow: the arcade shows it too, only at the 4:3 edge). A mod's own depth= wins.
    // HOTD_HEAD_DEPTH=sort keeps the arcade's keys (comparisons)
    static const bool headsKeyed = [] { const char* e = std::getenv("HOTD_HEAD_DEPTH"); return e && std::string(e) == "sort"; }();
    if (stage.source == Source::Arcade && !headsKeyed)
        for (const auto& [key, model] : stage.models)
            for (const char* p : {"onnatama"})
                if (model.name.rfind(p, 0) == 0) { g.modelDepth.emplace(key, kDepthBuffer); break; }
    // perf (2026-10-16): the meshes go straight into the buffer (was a CPU copy of every vertex first)
    size_t total = 0;
    for (const auto& [key, model] : stage.models) {
        std::vector<Range>& ranges = g.models[key];
        if (auto cm = repl.models.find(key); cm != repl.models.end()) {   // a pack's custom model (assets/mods.h)
            const int set = stage.source == Source::PC ? 0 : -1;
            ranges.push_back({int(total), int(cm->second.size()), set, -1, false});
            total += cm->second.size();
            g.replModels.insert(key);
            continue;
        }
        for (const Mesh& m : model.meshes) {
            if (m.vertices.empty()) continue;
            ranges.push_back({int(total), int(m.vertices.size()), m.textureSet, m.variant, m.blend});
            total += m.vertices.size();
        }
    }
    gl::GenVertexArrays(1, &g.vao);
    gl::BindVertexArray(g.vao);
    gl::GenBuffers(1, &g.vbo);
    gl::BindBuffer(gl::ARRAY_BUFFER, g.vbo);
    // compat (2026-10-29, Mesa's debug output: "glBufferSubData to update a GL_STATIC_DRAW buffer", every frame of the
    // menu's live attract): the generated skins are rewritten each frame (updateModel): DYNAMIC_DRAW is the right hint
    gl::BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(total * sizeof(Vertex)), nullptr, gl::DYNAMIC_DRAW);
    {
        size_t at = 0;
        for (const auto& [key, model] : stage.models) {
            if (auto cm = repl.models.find(key); cm != repl.models.end()) {
                gl::BufferSubData(gl::ARRAY_BUFFER, gl::GLintptr(at * sizeof(Vertex)), gl::GLsizeiptr(cm->second.size() * sizeof(Vertex)),
                                  cm->second.data());
                at += cm->second.size();
                continue;
            }
            for (const Mesh& m : model.meshes) {
                if (m.vertices.empty()) continue;
                std::vector<Vertex> tmp;
                gl::BufferSubData(gl::ARRAY_BUFFER, gl::GLintptr(at * sizeof(Vertex)), gl::GLsizeiptr(m.vertices.size() * sizeof(Vertex)),
                                  replPatched(g, m, tmp));
                at += m.vertices.size();
            }
        }
    }
    {   // Draw Depth "Arcade Sort": the triangles' sort keys, in the vertex buffer's order (one per 3 vertices)
        std::vector<float> keys;
        keys.reserve(total / 3 * 16);
        for (const auto& [key, model] : stage.models) {
            if (auto cm = repl.models.find(key); cm != repl.models.end()) {
                keys.resize(keys.size() + cm->second.size() / 3 * 16, 0.0f);
                continue;
            }
            for (const Mesh& m : model.meshes)
                if (!m.vertices.empty()) sortKeysOf(m, m.vertices.size(), keys);
        }
        keys.resize(std::max<size_t>(total / 3 * 16, 16), 0.0f);
        gl::GenBuffers(1, &g.sortBuf);
        gl::BindBuffer(gl::TEXTURE_BUFFER, g.sortBuf);
        gl::BufferData(gl::TEXTURE_BUFFER, gl::GLsizeiptr(keys.size() * sizeof(float)), keys.data(), gl::DYNAMIC_DRAW);   // (compat: also rewritten)
        gl::GenTextures(1, &g.sortTex);
        gl::BindTexture(gl::TEXTURE_BUFFER, g.sortTex);
        gl::TexBuffer(gl::TEXTURE_BUFFER, gl::RGBA32F, g.sortBuf);
        gl::BindTexture(gl::TEXTURE_BUFFER, 0);
        gl::BindBuffer(gl::TEXTURE_BUFFER, 0);
        gl::BindBuffer(gl::ARRAY_BUFFER, g.vbo);
    }
    const gl::GLsizei st = sizeof(Vertex);
    auto off = [](size_t o) { return reinterpret_cast<const void*>(o); };
    gl::VertexAttribPointer(0, 3, gl::FLOAT, gl::FALSE_, st, off(offsetof(Vertex, pos)));
    gl::VertexAttribPointer(1, 2, gl::FLOAT, gl::FALSE_, st, off(offsetof(Vertex, uv)));
    gl::VertexAttribPointer(2, 3, gl::FLOAT, gl::FALSE_, st, off(offsetof(Vertex, normal)));
    gl::VertexAttribIPointer(3, 4, gl::UNSIGNED_SHORT, st, off(offsetof(Vertex, rect)));
    gl::VertexAttribIPointer(4, 2, gl::UNSIGNED_SHORT, st, off(offsetof(Vertex, flags)));
    gl::VertexAttribIPointer(5, 4, gl::UNSIGNED_BYTE, st, off(offsetof(Vertex, color)));
    gl::VertexAttribIPointer(6, 1, gl::UNSIGNED_SHORT, st, off(offsetof(Vertex, palette)));
    gl::VertexAttribIPointer(7, 1, gl::UNSIGNED_SHORT, st, off(offsetof(Vertex, lodCoef)));
    for (gl::GLuint i = 0; i < 8; ++i) gl::EnableVertexAttribArray(i);
    gl::BindVertexArray(0);

    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    for (const TextureSet& ts : stage.textureSets) {
        GpuSet s;
        s.shading = ts.shading;
        s.lightKnown = ts.lightKnown;
        std::copy_n(ts.lightDir, 3, s.lightDir);
        std::copy_n(ts.diffuse, 32, s.diffuse);
        std::copy_n(ts.ambient, 32, s.ambient);
        std::copy_n(ts.specScale, 32, s.specScale);
        std::copy_n(ts.specCtl, 32, s.specCtl);
        const bool rgba = ts.shading == Shading::DirectRgba;
        if (rgba && !ts.layersBrown.empty() && ts.layersBrown.size() == ts.layers.size()) {
            gl::GenTextures(1, &s.layersBrown);
            gl::BindTexture(gl::TEXTURE_2D_ARRAY, s.layersBrown);
            gl::TexImage3D(gl::TEXTURE_2D_ARRAY, 0, gl::RGBA8, ts.width, ts.height, gl::GLsizei(ts.layersBrown.size()), 0, gl::RGBA,
                           gl::UNSIGNED_BYTE, nullptr);
            for (size_t i = 0; i < ts.layersBrown.size(); ++i)
                gl::TexSubImage3D(gl::TEXTURE_2D_ARRAY, 0, 0, 0, gl::GLint(i), ts.width, ts.height, 1, gl::RGBA, gl::UNSIGNED_BYTE,
                                  ts.layersBrown[i].data());
            gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
            gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
            gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_WRAP_S, gl::REPEAT);
            gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_WRAP_T, gl::REPEAT);
        }
        gl::GenTextures(1, &s.layers);
        gl::BindTexture(gl::TEXTURE_2D_ARRAY, s.layers);
        gl::TexImage3D(gl::TEXTURE_2D_ARRAY, 0, rgba ? gl::RGBA8 : gl::R8, ts.width, ts.height, gl::GLsizei(ts.layers.size()), 0,
                       rgba ? gl::RGBA : gl::RED, gl::UNSIGNED_BYTE, nullptr);
        for (size_t i = 0; i < ts.layers.size(); ++i)
            gl::TexSubImage3D(gl::TEXTURE_2D_ARRAY, 0, 0, 0, gl::GLint(i), ts.width, ts.height, 1, rgba ? gl::RGBA : gl::RED,
                              gl::UNSIGNED_BYTE, ts.layers[i].data());
        if (rgba) {
            gl::GenerateMipmap(gl::TEXTURE_2D_ARRAY);
            gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MIN_FILTER, gl::LINEAR_MIPMAP_LINEAR);
            gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
            gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_WRAP_S, gl::REPEAT);
            gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_WRAP_T, gl::REPEAT);
        } else {
            gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
            gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
            auto table = [](gl::GLuint& tex, int w, int h, const std::vector<uint8_t>& data) {
                gl::GenTextures(1, &tex);
                gl::BindTexture(gl::TEXTURE_2D, tex);
                gl::TexImage2D(gl::TEXTURE_2D, 0, gl::R8, w, h, 0, gl::RED, gl::UNSIGNED_BYTE, data.data());
                gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
                gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
            };
            table(s.luma, 128, 128, ts.luma);
            table(s.xlat, 256, 96, ts.xlat);
            // the set's polygon palette (base colours by Vertex::palette)
            std::vector<uint16_t> pal(ts.basePalette);
            pal.resize(1024, 0);
            gl::GenTextures(1, &s.pal);
            gl::BindTexture(gl::TEXTURE_2D, s.pal);
            gl::TexImage2D(gl::TEXTURE_2D, 0, gl::R16UI, 1024, 1, 0, gl::RED_INTEGER, gl::UNSIGNED_SHORT, pal.data());
            gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
            gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
        }
        g.sets.push_back(s);
    }
    uploadReplacements(repl, g);
    stages_[&stage] = std::move(g);
}

void Renderer::sortKeysOf(const Mesh& m, size_t count, std::vector<float>& out)
{
    const std::vector<Vertex>& v = m.vertices;
    size_t side = 0;   // m.sortKeys, in triangle order
    for (size_t t = 0; t < count / 3; ++t) {
        float k[16] = {};
        const Vertex* a = &v[t * 3];
        const int mode = (a->layer & kSortMask) >> kSortShift;
        while (side < m.sortKeys.size() && m.sortKeys[side].tri < t) ++side;
        if (mode != 0) {
            // the polygon's corners: a quad's two triangles (0 1 2, 0 2 3) each lack one, taken from the other
            const Vertex* c[4] = {&a[0], &a[1], &a[2], &a[2]};
            if ((a->layer & kSortQuadFirst) && (t + 1) * 3 + 2 < v.size()) c[3] = &v[t * 3 + 5];
            else if ((a->layer & kSortQuadSecond) && t > 0) c[3] = &v[t * 3 - 2];
            for (int i = 0; i < 4; ++i) std::copy_n(c[i]->pos, 3, &k[i * 4]);
            k[3] = float(mode);
        } else if (side < m.sortKeys.size() && m.sortKeys[side].tri == t) {
            const Mesh::SortKey& s = m.sortKeys[side];
            for (int i = 0; i < 4; ++i) std::copy_n(s.p[i], 3, &k[i * 4]);
            k[3] = float(s.mode);
        }
        out.insert(out.end(), k, k + 16);
    }
}

// texture packs (assets/mods.h): the active packs' pictures for this stage, shelf-packed into 2048 x 2048 layers, and
// per set the map from shader key to the picture's place. Nothing when no pack is active (the shader skips it)
const Vertex* Renderer::replPatched(const GpuStage& g, const Mesh& m, std::vector<Vertex>& tmp)
{
    if (g.replVirtual.empty()) return m.vertices.data();
    tmp = m.vertices;
    for (size_t i = 0; i + 2 < tmp.size(); i += 3) {
        if (!(tmp[i].flags & kTextured)) continue;
        auto it = g.replVirtual.find(uint64_t(arcadeTextureKey(tmp[i])) << 10 | (tmp[i].palette & 0x3FF));
        if (it == g.replVirtual.end()) continue;
        for (int k = 0; k < 3; ++k) tmp[i + size_t(k)].lodCoef = uint16_t((tmp[i + size_t(k)].lodCoef & 31u) | (it->second + 1) << 5);
    }
    return tmp.data();
}

void Renderer::uploadReplacements(const StageReplacements& rp, GpuStage& g)
{
    if (rp.empty()) return;
    constexpr int kAtlas = 2048;
    constexpr uint32_t kMapRows = (kVirtualKey + kMaxVirtual) / 512 + 1;   // keys below 2^18, then the virtual ones
    // tallest first, rows left to right
    std::vector<uint32_t> order(rp.images.size());
    for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return rp.images[a]->height > rp.images[b]->height; });
    struct Place { int x, y, layer; };
    std::vector<Place> place(rp.images.size());
    int x = 0, y = 0, rowH = 0, layer = 0;
    for (uint32_t i : order) {
        const ReplImage& im = *rp.images[i];
        if (x + im.width > kAtlas) { x = 0; y += rowH; rowH = 0; }
        if (y + im.height > kAtlas) { x = 0; y = 0; rowH = 0; ++layer; }
        place[i] = {x, y, layer};
        x += im.width;
        rowH = std::max(rowH, im.height);
    }
    const int layers = layer + 1;
    if (layers > 31) {
        std::fprintf(stderr, "mods: too many pack pictures for one stage (%d atlas layers; 31 at most): packs left out\n", layers);
        return;
    }
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::GenTextures(1, &g.replAtlas);
    gl::BindTexture(gl::TEXTURE_2D_ARRAY, g.replAtlas);
    gl::TexImage3D(gl::TEXTURE_2D_ARRAY, 0, gl::RGBA8, kAtlas, kAtlas, layers, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
    for (size_t i = 0; i < rp.images.size(); ++i) {
        const ReplImage& im = *rp.images[i];
        gl::TexSubImage3D(gl::TEXTURE_2D_ARRAY, 0, place[i].x, place[i].y, place[i].layer, im.width, im.height, 1, gl::RGBA,
                          gl::UNSIGNED_BYTE, im.rgba.data());
    }
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
    std::vector<uint16_t> map, refMap;
    auto put = [&](std::vector<uint16_t>& m, uint32_t key, uint32_t img) {
        uint16_t* e = &m[size_t(key) * 4];
        e[0] = uint16_t(place[img].x | place[img].layer << 11);
        e[1] = uint16_t(place[img].y);
        e[2] = uint16_t(rp.images[img]->width);
        e[3] = uint16_t(rp.images[img]->height);
    };
    auto mapTexture = [&](gl::GLuint& tex, const std::vector<uint16_t>& m) {
        gl::GenTextures(1, &tex);
        gl::BindTexture(gl::TEXTURE_2D, tex);
        gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA16UI, 512, kMapRows, 0, gl::RGBA_INTEGER, gl::UNSIGNED_SHORT, m.data());
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
    };
    for (size_t s = 0; s < g.sets.size() && s < rp.sets.size(); ++s) {
        if (rp.sets[s].empty()) continue;
        map.assign(size_t(512) * kMapRows * 4, 0);
        refMap.assign(map.size(), 0);
        bool anyRef = false;
        for (const StageReplacements::Entry& en : rp.sets[s]) {
            if (en.key >= 512u * kMapRows || en.image >= rp.images.size()) continue;
            put(map, en.key, en.image);
            if (en.additive) map[size_t(en.key) * 4 + 1] |= 0x8000;   // y bit 15: add the change to the game's colour
            if (en.ref < rp.images.size()) {
                put(refMap, en.key, en.ref);
                anyRef = true;
            }
        }
        mapTexture(g.sets[s].replMap, map);
        if (anyRef) mapTexture(g.sets[s].replRef, refMap);
    }
}

void Renderer::updateXlat(const StageData& stage, int set, const std::vector<uint8_t>& xlat)
{
    auto g = stages_.find(&stage);
    if (g == stages_.end() || set < 0 || set >= int(g->second.sets.size()) || xlat.size() < 256 * 96) return;
    const GpuSet& s = g->second.sets[size_t(set)];
    if (!s.xlat) return;
    gl::BindTexture(gl::TEXTURE_2D, s.xlat);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::R8, 256, 96, 0, gl::RED, gl::UNSIGNED_BYTE, xlat.data());
}

void Renderer::updateModel(const StageData& stage, const Model& model)
{
    auto g = stages_.find(&stage);
    if (g == stages_.end()) return;
    auto r = g->second.models.find(model.key);
    if (r == g->second.models.end() || g->second.replModels.count(model.key)) return;
    gl::BindBuffer(gl::ARRAY_BUFFER, g->second.vbo);
    size_t k = 0;
    std::vector<Vertex> tmp;
    for (const Mesh& m : model.meshes) {
        if (m.vertices.empty()) continue;
        if (k >= r->second.size()) break;
        const Range& range = r->second[k++];
        if (int(m.vertices.size()) != range.count) continue;
        gl::BufferSubData(gl::ARRAY_BUFFER, gl::GLintptr(size_t(range.first) * sizeof(Vertex)),
                          gl::GLsizeiptr(m.vertices.size() * sizeof(Vertex)), replPatched(g->second, m, tmp));
        if (g->second.sortBuf) {   // the moved corners' sort keys (generated skins)
            std::vector<float> keys;
            sortKeysOf(m, m.vertices.size(), keys);
            gl::BindBuffer(gl::TEXTURE_BUFFER, g->second.sortBuf);
            gl::BufferSubData(gl::TEXTURE_BUFFER, gl::GLintptr(size_t(range.first) / 3 * 16 * sizeof(float)),
                              gl::GLsizeiptr(keys.size() * sizeof(float)), keys.data());
            gl::BindBuffer(gl::TEXTURE_BUFFER, 0);
        }
    }
}

void Renderer::release(const StageData& stage)
{
    auto it = stages_.find(&stage);
    if (it == stages_.end()) return;
    GpuStage& g = it->second;
    gl::DeleteBuffers(1, &g.vbo);
    gl::DeleteVertexArrays(1, &g.vao);
    for (GpuSet& s : g.sets) {
        gl::DeleteTextures(1, &s.layers);
        if (s.layersBrown) gl::DeleteTextures(1, &s.layersBrown);
        if (s.luma) gl::DeleteTextures(1, &s.luma);
        if (s.xlat) gl::DeleteTextures(1, &s.xlat);
        if (s.pal) gl::DeleteTextures(1, &s.pal);
        if (s.replMap) gl::DeleteTextures(1, &s.replMap);
        if (s.replRef) gl::DeleteTextures(1, &s.replRef);
    }
    if (g.replAtlas) gl::DeleteTextures(1, &g.replAtlas);
    if (g.sortTex) gl::DeleteTextures(1, &g.sortTex);
    if (g.sortBuf) gl::DeleteBuffers(1, &g.sortBuf);
    stages_.erase(it);
}

void Renderer::draw(const std::vector<DrawItem>& items, const Camera& cam, int width, int height, const RenderOptions& opt)
{
    gl::Viewport(0, 0, width, height);
    gl::ClearColor(opt.clear[0], opt.clear[1], opt.clear[2], 1.0f);
    gl::Clear(opt.clearColor ? gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT : gl::DEPTH_BUFFER_BIT);
    gl::Enable(gl::DEPTH_TEST);
    // no near clip, as the Model 2 (MAME model2_v.cpp clips polygons only to the four side planes through the eye and
    // culls those wholly behind it): depth clamp keeps what lies between the eye and the near plane 1, at depth 0. The
    // shot's impact star (shot_effect_flash 0x95140, a camera-facing star at depth 1, scale 0.1) reaches nearer than 1
    // when turned toward its off-axis point and was cut to a sliver. The near plane stays 1 (the depth precision the
    // decal layers were tuned for). HOTD_NO_DEPTH_CLAMP=1 switches it off for comparisons.
    static const bool noClamp = std::getenv("HOTD_NO_DEPTH_CLAMP") != nullptr;
    if (noClamp)
        gl::Disable(gl::DEPTH_CLAMP);
    else
        gl::Enable(gl::DEPTH_CLAMP);
    gl::DepthFunc(gl::LEQUAL);
    gl::Disable(gl::CULL_FACE);

    // view (world -> camera: rows right, up, forward) and a left-handed perspective (camera +z forward)
    float r[3], u[3], f[3];
    cam.basis(r, u, f);
    float e[3] = {cam.pos[0] + debugJitter[0], cam.pos[1] + debugJitter[1], cam.pos[2] + debugJitter[2]};
    float view[16] = {r[0], u[0], f[0], 0, r[1], u[1], f[1], 0, r[2], u[2], f[2], 0,
                      -(r[0] * e[0] + r[1] * e[1] + r[2] * e[2]), -(u[0] * e[0] + u[1] * e[1] + u[2] * e[2]),
                      -(f[0] * e[0] + f[1] * e[1] + f[2] * e[2]), 1};
    static const char* nearEnv = std::getenv("HOTD_NEAR");   // debugging: every camera's near plane (comparisons)
    const float n = std::clamp(nearEnv ? float(std::atof(nearEnv)) : eye ? eye->nearZ : cam.nearZ, 0.01f, 1.0f), fa = 20000.0f, t = 1.0f / std::tan(cam.fovY / 2), aspect = float(width) / float(height);
    // (trailer footage: HOTD_CAM_ZOOM=1.6 narrows every 3D view, a closer camera; the picture only)
    static const float camZoom = std::getenv("HOTD_CAM_ZOOM") ? std::max(0.2f, float(std::atof(std::getenv("HOTD_CAM_ZOOM")))) : 1.0f;
    float proj[16] = {t * camZoom / aspect, 0, 0, 0, 0, t * camZoom, 0, 0, cam.shift[0], cam.shift[1], (fa + n) / (fa - n), 1,
                      0, 0, -2 * fa * n / (fa - n), 0};   // (w = z: the shift moves every point the same on screen)
    if (eye) {   // vr: the eye's own view and (off-axis) frustum; the eye position / forward for the shading from its view
        std::memcpy(view, eye->view, sizeof view);
        for (int k = 0; k < 3; ++k) {
            r[k] = view[k * 4], u[k] = view[k * 4 + 1], f[k] = view[k * 4 + 2];
        }
        for (int k = 0; k < 3; ++k)
            e[k] = -(r[k] * view[12] + u[k] * view[13] + f[k] * view[14]);
        const float sx = eye->tanRight - eye->tanLeft, sy = eye->tanUp - eye->tanDown;
        proj[0] = 2.0f / sx;
        proj[5] = 2.0f / sy;
        proj[8] = -(eye->tanRight + eye->tanLeft) / sx;
        proj[9] = -(eye->tanUp + eye->tanDown) / sy;
    }
    float vp[16];
    multiply(proj, view, vp);
    std::memcpy(lastViewProj, vp, sizeof vp);

    // depthfix: Depth Buffer (and the viewer's Free Cam) with the key band (the vertex / fragment shader);
    // HOTD_KEY_BAND=x sets it (0: the plain per-pixel depth, comparisons)
    const bool sort = drawDepth() == kArcadeSort && !cam.perPixelDepth;
    static const char* bandEnv = std::getenv("HOTD_KEY_BAND");
    const float band = sort ? 0.0f : bandEnv ? std::max(0.0f, float(std::atof(bandEnv))) : cam.perPixelDepth ? kKeyBandFreeCam : kKeyBand;
    const bool bandFrag = band > 0.0f && programBand_ != 0;
    gl::UseProgram(bandFrag ? programBand_ : program_);
    gl::ActiveTexture(gl::TEXTURE0 + 3);
    gl::BindTexture(gl::TEXTURE_2D, palAnim_);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::UniformMatrix4fv(uViewProj_, 1, gl::FALSE_, vp);
    gl::Uniform1i(uLighting_, opt.lighting ? 1 : 0);
    gl::Uniform1i(uFilter_, opt.filter ? 1 : 0);
    gl::Uniform1i(uTransp_, std::clamp(opt.transparency, 0, 2));   // (team: Transparency)
    float l[3] = {opt.lightDir[0], opt.lightDir[1], opt.lightDir[2]};
    float len = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
    gl::Uniform3f(uLightDir_, l[0] / len, l[1] / len, l[2] / len);
    gl::Uniform1f(uAmbient_, opt.ambient);
    gl::Uniform3f(uCamPos_, e[0], e[1], e[2]);
    gl::Uniform3f(uCamFwd_, f[0], f[1], f[2]);
    gl::Uniform1i(uM2Gamma_, opt.gamma ? 1 : 0);
    gl::Uniform1i(uCull_, opt.cull ? 1 : 0);
    gl::Uniform1i(uMip_, opt.mipmap ? 1 : 0);
    gl::Uniform1i(uGrey_, opt.grey ? 1 : 0);
    gl::Uniform1i(uGreyRepl_, opt.grey ? 1 : 0);
    gl::Uniform1i(uRainbow_, opt.rainbow);   // (cosmods: Rainbow Blood)
    gl::Uniform1f(uRainbowPhase_, opt.rainbowPhase);
    gl::Uniform3f(uBloodRgb_, opt.bloodRgb[0], opt.bloodRgb[1], opt.bloodRgb[2]);
    gl::Uniform1i(uRainbowGame_, opt.rainbowGame ? 1 : 0);
    gl::Uniform1i(uRainbowSync_, opt.rainbowSync);
    if (opt.rainbowGame) gl::Uniform3fv(uRainbowTable_, 90, &opt.rainbowTable[0][0]);
    gl::Uniform1i(uBloodRows_, int(opt.bloodRowMask));   // (rainbow2: the blood rows in the rainbow)
    gl::Uniform1i(uPcFilter_, opt.pcBilinear ? 1 : 0);
    gl::Uniform1i(uHasRepl_, 0);
    if (opt.grey) {
        float sep[64 * 3];
        for (int i = 0; i < 64 * 3; ++i) sep[i] = float(opt.sepia[i / 3][i % 3]) / 255.0f;
        gl::Uniform3fv(uSepia_, 64, sep);
    }
    gl::Uniform1i(uPcCullSign_, 1);   // checked by eye: -1 shows the interiors through the walls
    // PC polygons at alpha 0x7F (kHalfAlpha) blend; everything else writes alpha 1
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);

    // backgrounds (sky) first without depth writes, then everything else
    // 2: decals (DrawItem::decal) over the opaque pass, depth-tested, no depth writes: overlapping coplanar ones resolve by
    // draw order (the Model 2 sorts polygons); 3: the blended PC polygons (alpha 0x7F), no depth writes
    // perf (2026-10-21): each uniform and texture binding is sent only when its value changes within this draw (the
    // GL state is the same as when everything was sent for every item: far fewer driver calls), and each item's stage
    // and model ranges are looked up once (were once a pass)
    resolved_.assign(items.size(), Resolved{});
    for (size_t i = 0; i < items.size(); ++i) {
        auto gs = stages_.find(items[i].stage);
        if (gs == stages_.end()) continue;
        auto mr = gs->second.models.find(items[i].model->key);
        if (mr == gs->second.models.end()) continue;
        resolved_[i] = {&gs->second, &mr->second};
    }
    struct Cache {
        float f[9];
        int n = -1;   // -1: not sent yet in this draw
    };
    enum { kOffset, kRot, kBias, kFloorY, kFloorPull, kPush, kFade, kRamp, kBodyHue, kFlat, kUV, kShading, kM2Known, kM2Light, kGrey, kHasRepl,
           kClampUV, kSortItem, kHybrid, kKeyless, kSideOnly, kSlots };
    Cache cache[kSlots];
    auto changed = [&](int slot, const float* v, int n) {
        Cache& c = cache[slot];
        if (c.n == n && std::memcmp(c.f, v, sizeof(float) * size_t(n)) == 0) return false;
        std::memcpy(c.f, v, sizeof(float) * size_t(n));
        c.n = n;
        ++stateCalls;
        return true;
    };
    auto set1i = [&](int slot, gl::GLint loc, int v) {
        float f;
        std::memcpy(&f, &v, sizeof f);   // (the bits compared)
        if (changed(slot, &f, 1)) gl::Uniform1i(loc, v);
    };
    auto set1f = [&](int slot, gl::GLint loc, float v) { if (changed(slot, &v, 1)) gl::Uniform1f(loc, v); };
    auto set3f = [&](int slot, gl::GLint loc, const float* v) { if (changed(slot, v, 3)) gl::Uniform3f(loc, v[0], v[1], v[2]); };
    // the values sent above, before the passes
    set1i(kGrey, uGrey_, opt.grey ? 1 : 0);
    set1i(kHasRepl, uHasRepl_, 0);
    stateCalls -= 2;
    gl::GLuint unitTex[9] = {0, 0, 0, palAnim_, 0, 0, 0, 0, 0};
    bool unitKnown[9] = {false, false, false, true, false, false, false, false, false};
    // Draw Depth (renderer.h): Arcade Sort, the polygons' sort keys on unit 8
    static const bool sortExact = std::getenv("HOTD_SORT_EXACT") != nullptr;   // debugging: the keys not rounded
    gl::Uniform1i(uSort_, sort ? (sortExact ? 2 : 1) : 0);
    gl::Uniform1f(uHybrid_, band);
    gl::Uniform2f(uProj_, (fa + n) / (fa - n), 2 * fa * n / (fa - n));
    gl::Uniform1f(uBiasScale_, n);
    // earlyz: the keys from the vertex depth (not gl_FragDepth): the item's place is not in the depth, so the passes that
    // write depth draw the items last-first (on a key tie the earlier model, drawn later, wins by LEQUAL), the items
    // past 127 first in list order (they shared place 127: the later one won). Only the arcade items swap places among
    // themselves: the others (PC / pack models: their own depth) keep their places and order (their ties: draw order,
    // e.g. the PC door leaves' shared edge). The keyed draws (arcade sets) use the depth range 0 .. 2^-8 (the vertex
    // shader's comment)
    const bool sortVertex = sort && !sortFragDepth_;
    order_.clear();
    if (sortVertex) {
        auto keyed = [&](size_t i) { return items[i].stage->source == Source::Arcade; };
        orderArcade_.clear();
        for (size_t i = 127; i < items.size(); ++i)
            if (keyed(i)) orderArcade_.push_back(i);
        for (size_t i = std::min<size_t>(items.size(), 127); i-- > 0;)
            if (keyed(i)) orderArcade_.push_back(i);
        size_t k = 0;
        for (size_t i = 0; i < items.size(); ++i) order_.push_back(keyed(i) ? orderArcade_[k++] : i);
    }
    bool keyedRange = false;
    int activeUnit = 0;
    auto bindTex = [&](int unit, gl::GLenum target, gl::GLuint tex) {
        if (unitKnown[unit] && unitTex[unit] == tex) return;
        if (activeUnit != unit) {
            gl::ActiveTexture(gl::TEXTURE0 + gl::GLenum(unit));
            activeUnit = unit;
            ++stateCalls;
        }
        gl::BindTexture(target, tex);
        ++stateCalls;
        unitTex[unit] = tex;
        unitKnown[unit] = true;
    };
    static gl::GLint uRamp = -2, uStops = -2, uBodyHue = -2;
    if (uRamp == -2) {
        uRamp = gl::GetUniformLocation(program_, "uRamp");
        uStops = gl::GetUniformLocation(program_, "uRampStops");
        uBodyHue = gl::GetUniformLocation(program_, "uBodyHue");   // (cosmods: Rainbow Blood's creature colour)
    }
    int rampStopsSent = -1;
    static gl::GLint uLook = -2;   // Light Gun Edition: the fun modes' looks
    if (uLook == -2) uLook = gl::GetUniformLocation(program_, "uLook");
    gl::Uniform1i(uLook, opt.look);
    if (opt.look == 2) gl::polygonMode(gl::FRONT_AND_BACK, gl::LINE);
    // Renderer::debugTie / debugIds (debugging): the same for every item; the item's index only when either reads it
    gl::Uniform1f(uTie_, float(debugTie));
    static const bool idTri = std::getenv("HOTD_IDSHOT_TRI") != nullptr;   // debugging: triangles, not items
    gl::Uniform1i(uIdMode_, debugIds ? (idTri ? (std::getenv("HOTD_IDSHOT_TRI")[0] == 'z' ? 3 : 2) : 1) : 0);
    const bool sendItemIndex = debugTie != 0 || debugIds;
    // endfloor: DrawItem::sideOnly items draw only outside the centred 4:3 picture (its half width: 248 / 192 of the
    // half height, the view being hor+)
    gl::Uniform2f(uSideClip_, float(width) * 0.5f, eye ? -1.0f : float(height) * 0.5f * (248.0f / 192.0f));   // (vr: whole)
    const GpuStage* bound = nullptr;
    const GpuSet* setSent = nullptr;   // the set whose light tables are in the uniforms
    int boundSet = -1;
    // prepass (2026-10-27): the opaque pass twice. First depth only (colour writes off, the shader stops after its
    // discards: Renderer's prepassDiscards), the same items, order and state, so the depth buffer ends as the opaque
    // pass leaves it. Then the colours with EQUAL and no depth writes: only the fragments at the final depth are shaded,
    // the last drawn of them wins, as with LEQUAL (alpha 1 in this pass: the later one covers). The early depth test
    // drops the rest before the shader. Not with gl_FragDepth (HOTD_SORT_FRAGDEPTH: no early test) or the debug views
    const bool prepass = depthPrepass() && !(sort && sortFragDepth_) && !bandFrag && !debugIds && debugTie == 0;
    // visfix (user 2026-10-29: the ending's "Goodbye Curien" with walls across the widescreen sides): the neighbouring
    // zones' pieces (sideOnly) only fill what nothing else covers. Drawn in the same passes they stood in front of the
    // zone's own room where a zone swaps a space's dressing (the ending room after the Magician: the fight's panels a
    // few metres from the camera, over the floor and the sky's window). Now they go first, then the depth is cleared and
    // the sky and the zone's items draw over them: they show only where neither drew (the holes they were added for)
    bool hasSide = false;
    for (const DrawItem& it : items)
        hasSide = hasSide || it.sideOnly;
    for (int phase = hasSide ? 0 : 1; phase < 2; ++phase) {
    if (phase == 1 && hasSide) {
        gl::DepthMask(gl::TRUE_);
        gl::Clear(gl::DEPTH_BUFFER_BIT);
    }
    for (int step = phase == 0 ? 1 : 0; step < (prepass ? 5 : 4); ++step) {
    const int pass = prepass && step >= 2 ? step - 1 : step;
    const bool shadeAfterPre = prepass && step == 2;   // the opaque pass's colours after the depth-only one
    // the sky models hide each other by depth (Chapter 1: the mountain band 0x608 lies inside the cloud cylinder 0xA29;
    // drawn in list order without depth the clouds covered the mountains), then the depth is cleared for the scene
    if (pass == 1 && !shadeAfterPre) gl::Clear(gl::DEPTH_BUFFER_BIT);
    if (prepass && step == 1) {
        gl::ColorMask(gl::FALSE_, gl::FALSE_, gl::FALSE_, gl::FALSE_);
        gl::Uniform1i(uPrepass_, 1);
    } else if (shadeAfterPre) {
        gl::ColorMask(gl::TRUE_, gl::TRUE_, gl::TRUE_, gl::TRUE_);
        gl::Uniform1i(uPrepass_, 0);
        gl::DepthFunc(gl::EQUAL);
    } else if (prepass && step == 3) {
        gl::DepthFunc(gl::LEQUAL);
    }
    gl::DepthMask(pass <= 1 && !shadeAfterPre ? gl::TRUE_ : gl::FALSE_);
    set1i(kClampUV, uClampUV_, pass == 0 ? 1 : 0);
    for (size_t io = 0; io < items.size(); ++io) {
        const size_t ii = sortVertex && pass <= 1 ? order_[io] : io;
        const DrawItem& it = items[ii];
        if (it.background != (pass == 0)) continue;
        if (hasSide && it.sideOnly != (phase == 0)) continue;
        // PC models fading out are blended: after the opaque pass, without depth writes (with the decals). Arcade
        // Sort: the arcade decals in the opaque pass, in their list order (their polygons' sort keys place them)
        const bool late = (it.decal && !(sort && it.stage->source == Source::Arcade)) || (it.fade < 1.0f && it.stage->source == Source::PC);
        if ((pass == 1 && late) || (pass == 2 && !late)) continue;
        if (!resolved_[ii].g) continue;
        const GpuStage& g = *resolved_[ii].g;
        if (&g != bound) {
            gl::BindVertexArray(g.vao);
            ++stateCalls;
            if (sort || band > 0.0f || !g.modelDepth.empty()) bindTex(8, gl::TEXTURE_BUFFER, g.sortTex);
            bound = &g;
            boundSet = -1;
        }
        const std::vector<Range>& itemRanges = *resolved_[ii].ranges;
        // (the item's uniforms only when one of its ranges is drawn in this pass, as before: they were sent and then
        // nothing drawn, which left the same state for the next item)
        set3f(kOffset, uOffset_, it.placement->pos);
        if (changed(kRot, it.placement->rot, 9)) gl::UniformMatrix3fv(uRot_, 1, gl::TRUE_, it.placement->rot);   // row-major
        set1f(kBias, uDepthBias_, it.depthBias);
        // depthopt (2026-10-27): a mod's items in their own depth mode (mod.ini depth =; GpuStage::modelDepth), not in
        // the viewer's Free Cam (per pixel there anyway). Under Arcade Sort a "buffer" item reads no sort keys (its
        // depth from its own z, in the sort's depth scale: the arcade polygons around it keep their order); under the
        // Depth Buffer a "sort" item takes its polygons' keys whole (an unlimited key band), a "buffer" one no band
        int itemMode = -1;
        if (!g.modelDepth.empty() && !cam.perPixelDepth)
            if (auto md = g.modelDepth.find(it.model->key); md != g.modelDepth.end()) itemMode = md->second;
        const float itemBand = sort || itemMode < 0 ? band : itemMode == kArcadeSort ? 1e6f : 0.0f;
        set1i(kKeyless, uKeyless_, sort && itemMode == kDepthBuffer ? 1 : 0);
        set1f(kHybrid, uHybrid_, itemBand);
        if (sort || itemBand > 0.0f) set1i(kSortItem, uSortItem_, int(std::min<size_t>(ii, 127)));
        set1f(kFloorY, uFloorY_, it.floorY);
        set1i(kFloorPull, uFloorPull_, int(it.floorPull));
        set1f(kPush, uPush_, it.push);
        set1f(kFade, uFade_, it.fade);
        set1i(kSideOnly, uSideOnly_, it.sideOnly ? 1 : 0);
        set1i(kRamp, uRamp, it.ramp >= 0 && it.ramp < 4 ? it.ramp : -1);
        set1f(kBodyHue, uBodyHue, it.bloodHue);   // cosmods
        if (it.ramp >= 0 && it.ramp < 4 && rampStopsSent != it.ramp) {
            gl::Uniform3fv(uStops, 5, &rampStops[it.ramp][0][0]);
            rampStopsSent = it.ramp;
            ++stateCalls;
        }
        set1i(kFlat, uFlat_, it.stage->source == Source::Arcade && it.flat < 1024 ? int(it.flat) : -1);
        if (sendItemIndex) {
            gl::Uniform1i(uTieItem_, int(ii));
            ++stateCalls;
        }
        if (debugIds && ii >= debugNames.size()) {
            debugNames.resize(items.size());
            for (size_t k = 0; k < items.size(); ++k) {
                char b[160];
                std::snprintf(b, sizeof b, "%s %#x (%s) placement %#x at %.1f %.1f %.1f variant %d fade %.2f%s%s", items[k].stage->source == Source::PC ? "pc" : "arcade",
                              items[k].model->key, items[k].model->name.c_str(), items[k].placement->id, items[k].placement->pos[0],
                              items[k].placement->pos[1], items[k].placement->pos[2], items[k].variant, items[k].fade,
                              items[k].decal ? " decal" : "", items[k].background ? " background" : "");
                debugNames[k] = b;
            }
        }
        {
            auto sc = it.stage->source == Source::Arcade && !uvScroll_.empty() ? uvScroll_.find(it.model->key) : uvScroll_.end();
            const float uv[2] = {sc != uvScroll_.end() ? sc->second[0] : 0.0f, sc != uvScroll_.end() ? sc->second[1] : 0.0f};
            if (changed(kUV, uv, 2)) gl::Uniform2f(uUVScroll_, uv[0], uv[1]);
        }
        for (const Range& rg : itemRanges) {
            if (rg.variant >= 0 && rg.variant != it.variant) continue;
            if (pass > 0 && rg.blend != (pass == 3)) continue;
            // a mesh for every set (textureSet -1, arcade): the item's variant, else the model's preferred one
            const int set = rg.set >= 0 ? rg.set : it.variant >= 0 ? it.variant : std::max(it.model->preferredVariant, 0);
            if (set != boundSet && set >= 0 && set < int(g.sets.size())) {
                const GpuSet& s = g.sets[size_t(set)];
                set1i(kShading, uShading_, s.shading == Shading::Model2Indexed ? 1 : 0);
                if (const bool keyed = sortVertex && s.shading == Shading::Model2Indexed; keyed != keyedRange) {
                    gl::DepthRange(0.0, keyed ? 1.0 / 256.0 : 1.0);
                    keyedRange = keyed;
                    ++stateCalls;
                }
                set1i(kM2Known, uM2Known_, s.lightKnown ? 1 : 0);
                set3f(kM2Light, uM2Light_, s.lightDir);
                if (setSent != &s) {   // (constant per set)
                    gl::Uniform1fv(uM2Diffuse_, 32, s.diffuse);
                    gl::Uniform1fv(uM2Ambient_, 32, s.ambient);
                    gl::Uniform1fv(uM2SpecScale_, 32, s.specScale);
                    gl::Uniform1fv(uM2SpecCtl_, 32, s.specCtl);
                    setSent = &s;
                    stateCalls += 4;
                }
                // PC grey look: the brown pages replace the shader's sepia mapping (TextureSet::layersBrown)
                const bool brown = opt.grey && s.layersBrown != 0;
                bindTex(0, gl::TEXTURE_2D_ARRAY, brown ? s.layersBrown : s.layers);
                set1i(kGrey, uGrey_, opt.grey && !brown ? 1 : 0);
                if (s.shading == Shading::DirectRgba) {
                    // PC: THOTD.EXE (0x583A67) sets D3DRENDERSTATE_TEXTUREMAG / MIN to LINEAR only with the driver
                    // dialog's bilinear option (0x12E8C4C, devs.cfg), else NEAREST; no mip levels. The release's devs.cfg
                    // is empty and the game runs point-sampled (PC harness screenshots): pcBilinear off by default
                    const gl::GLint f = opt.pcBilinear ? gl::GLint(gl::LINEAR) : gl::GLint(gl::NEAREST);
                    gl::GLint& was = brown ? s.filterBrown : s.filterLayers;   // (perf: set once, it stays on the texture)
                    if (was != f) {
                        if (activeUnit != 0) {
                            gl::ActiveTexture(gl::TEXTURE0);
                            activeUnit = 0;
                        }
                        gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MIN_FILTER, f);
                        gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MAG_FILTER, f);
                        was = f;
                        stateCalls += 2;
                    }
                }
                if (s.luma) {
                    bindTex(1, gl::TEXTURE_2D, s.luma);
                    bindTex(2, gl::TEXTURE_2D, s.xlat);
                    bindTex(4, gl::TEXTURE_2D, s.pal);
                } else if (!unitKnown[1]) {   // compat: never an empty unit under a live sampler (makeSpareTextures)
                    bindTex(1, gl::TEXTURE_2D, spareFloat_);
                    bindTex(2, gl::TEXTURE_2D, spareFloat_);
                    bindTex(4, gl::TEXTURE_2D, spareUint_);
                }
                set1i(kHasRepl, uHasRepl_, s.replMap ? 1 : 0);   // texture packs (assets/mods.h)
                if (s.replMap) {
                    bindTex(5, gl::TEXTURE_2D, s.replMap);
                    bindTex(6, gl::TEXTURE_2D_ARRAY, g.replAtlas);
                    bindTex(7, gl::TEXTURE_2D, s.replRef ? s.replRef : s.replMap);   // (PC: no references, never read)
                } else {   // compat: the spare textures (no texture pack; read only with uHasRepl)
                    bindTex(5, gl::TEXTURE_2D, spareUint_);
                    bindTex(6, gl::TEXTURE_2D_ARRAY, spareArray_);
                    bindTex(7, gl::TEXTURE_2D, spareUint_);
                }
                boundSet = set;
            }
            if (set >= 0 && set < int(g.sets.size())) {   // the item's own light (DrawItem::light), else the set's
                const float* lv = it.hasLight ? it.light : g.sets[size_t(set)].lightDir;
                set3f(kM2Light, uM2Light_, lv);
            }
            gl::DrawArrays(gl::TRIANGLES, rg.first, rg.count);
            ++drawCalls;
        }
    }
    }
    }
    if (activeUnit != 0)
        gl::ActiveTexture(gl::TEXTURE0);
    if (keyedRange) gl::DepthRange(0.0, 1.0);
    gl::DepthMask(gl::TRUE_);
    gl::Disable(gl::BLEND);
    gl::BindVertexArray(0);
    if (opt.look == 2) gl::polygonMode(gl::FRONT_AND_BACK, gl::FILL);
}

std::vector<uint8_t> Renderer::readBack(int width, int height)
{
    std::vector<uint8_t> px(size_t(width) * height * 3), rows(px.size());
    gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, width, height, gl::RGB, gl::UNSIGNED_BYTE, px.data());
    for (int y = 0; y < height; ++y)   // GL rows are bottom-up
        std::copy_n(px.begin() + ptrdiff_t(size_t(height - 1 - y) * width * 3), size_t(width) * 3,
                    rows.begin() + ptrdiff_t(size_t(y) * width * 3));
    return rows;
}

std::vector<uint8_t> Renderer::drawToImage(const std::vector<DrawItem>& items, const Camera& cam, int width, int height,
                                           const RenderOptions& opt)
{
    gl::GLuint fbo, color, depth;
    gl::GenFramebuffers(1, &fbo);
    gl::BindFramebuffer(gl::FRAMEBUFFER, fbo);
    gl::GenTextures(1, &color);
    gl::BindTexture(gl::TEXTURE_2D, color);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, width, height, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, color, 0);
    gl::GenRenderbuffers(1, &depth);
    gl::BindRenderbuffer(gl::RENDERBUFFER, depth);
    gl::RenderbufferStorage(gl::RENDERBUFFER, gl::DEPTH_COMPONENT24, width, height);
    gl::FramebufferRenderbuffer(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::RENDERBUFFER, depth);
    std::vector<uint8_t> out;
    if (gl::CheckFramebufferStatus(gl::FRAMEBUFFER) == gl::FRAMEBUFFER_COMPLETE) {
        draw(items, cam, width, height, opt);
        out = readBack(width, height);
    }
    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    gl::DeleteRenderbuffers(1, &depth);
    gl::DeleteTextures(1, &color);
    gl::DeleteFramebuffers(1, &fbo);
    return out;
}

} // namespace hotd
