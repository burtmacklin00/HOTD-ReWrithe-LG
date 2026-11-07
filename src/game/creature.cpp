// The creature core, read from the arcade disassembly (2026-10-15) and run by the runtime in place of the port's
// boundaries (runtime.h): object_draw, body_draw, part_draw, the per-part hook, body_pose and the motion frame / flag
// routines, the set_motion family, part_hit_mark. Written in the port's style (one routine per original function, the
// original layouts, the coprocessor through copro()), so L3 (docs/teaser_plan.md) can move them into src/port and
// prove them with captures. UNVERIFIED (teaser): no capture proof yet.
//
// Motion data (formats.md 23): motion m's frames start at the main_data address in 0xC7540[m] (frame counts
// 0xC7FD0[m]); a frame record is the root x, y, z (floats) and three 16-bit angles per joint, padded to a word:
// 4 x ((48 J + 0x7F) >> 5) bytes, J = the type's joint count 0xC73C0[type].
#include "game/runtime.h"

#include "assets/arcade_loader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

namespace hotd::game {

using G = A::G;
using port::fbits;
using port::ffrom;

namespace {
constexpr uint32_t kBodyCurrent = 0x51FE2C;    // the body being drawn
constexpr uint32_t kBodyCut = 0x520AE0;        // int16: a part was marked to be severed this frame
constexpr uint32_t kBodyHome = 0x520AE2;       // int16: the part being drawn is the type's home part
constexpr uint32_t kFrameData = 0x53E3A0;      // the frame data body_draw poses with
constexpr uint32_t kMotion2Data = 0x53E3A4;    // the second motion's
constexpr uint32_t kJoints = 0xC73C0, kMotionData = 0xC7540, kMotionFrames = 0xC7FD0;
constexpr uint32_t kMirrored = 0x7C6F0;        // byte per motion: a mirrored motion (body_pose_set_rec)
constexpr uint32_t kPartRoles = 0x1184E0;      // 0xF8 per type, 8 per role (PartRole)
constexpr uint32_t kHomeClass = 0x14A430;      // per type: the part class whose position is the object's home
constexpr uint32_t kHitSpheres = 0x520B10;     // per part ID: the hit sphere's centre (16 bytes)
constexpr uint32_t kSkinNumbers = 0x20E40;     // int16 per type: the generated (belly) skin, -1 none (port creature.cpp)
// Rogan's arcade coat (body_part_draw_hook: models 0xB5.., 0x119.. in the ending): the chest's SECOND draw, never the
// part's own model (pmfix: the rule is by the draw, not only by the model number, so no body part is ever dropped)
bool isRoganCoat(const DrawModel& d, uint32_t ownModel)
{
    return d.model != ownModel && d.model >= 0xB5 && d.model < 0x119 + 0x32;
}
// the ending's character bodies by their frame routine (ending.cpp): player 1's (alone 0x58CC0 / of two 0x59120) -> 0,
// player 2's (0x596F0 / 0x59B10) -> 1, else -1
int endingBodySeat(uint32_t routine)
{
    return routine == 0x58CC0u || routine == 0x59120u ? 0 : routine == 0x596F0u || routine == 0x59B10u ? 1 : -1;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------
// helpers (no original routine of their own)

int32_t Runtime::joints(uint32_t obj) { return int32_t(mem.u32(kJoints + 4 * uint32_t(int32_t(mem.at<int16_t>(obj + 0xCC))))); }

// one angle of a pose blend: from + (to - from) x t / 0x10000 the short way round (the product as the i960's 32-bit
// multiply, the shift dividing toward zero)
static int16_t blendAngle(uint16_t from, uint16_t to, int32_t t)
{
    const uint32_t d = uint32_t(to - from) & 0xFFFF;
    if (d > 0x7FFF)
        return int16_t(int32_t(from) - int32_t(uint32_t(0x10000 - d) * uint32_t(t)) / 0x10000);
    return int16_t(int32_t(from) + int32_t(d * uint32_t(t)) / 0x10000);
}

static uint32_t angleGap(uint16_t a, uint16_t b)
{
    uint32_t d = uint32_t(b - a) & 0xFFFF;
    if (d > 0x7FFF)
        d = 0x10000 - d;
    return d;
}

// ---------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------

// 0x67DF0 object_draw, 0x67F60 object_draw_model, 0x37720 body_draw, 0x36240 part_draw, 0x9CBD0 part_hit_mark: the
// port's (src/port/creature.cpp, proven in l3_cr1; proofs-7 dropped the runtime's first copies). The runtime keeps its
// view of what they draw: skin_draw (the bellies: the engine builds the skin from the two anchors), and the shadow
// pass (coprocessor 0x77 with stored matrix 3 before the shadow model: geo_draw_model marks that model a shadow and
// keeps the matrix before the multiply; the view squashes it onto the floor with shadow_setup's projection)
void Runtime::skin_draw(uint32_t obj)
{
    SkinDraw sd{mem.at<int16_t>(obj + 0xCC), {}, {}};
    std::memcpy(sd.a, mem.ptr(obj + 0x184), sizeof sd.a);
    std::memcpy(sd.b, mem.ptr(obj + 0x1B4), sizeof sd.b);
    sd.body = obj;
    skinDraws.push_back(sd);
}

// 0x21C30 sophie_skin_draw: the port (polygon RAM, the geometrizer writes); the view draws her strip from the loader's
// template (ArcadeSource::skinInfo which 1): corners follow +0x2A4, drawn with +0x274
void Runtime::sophie_skin_draw(uint32_t obj)
{
    port::Game<A>::sophie_skin_draw(obj);
    SkinDraw sd{kSophieStrip, {}, {}};
    std::memcpy(sd.a, mem.ptr(obj + 0x2A4), sizeof sd.a);
    std::memcpy(sd.b, mem.ptr(obj + 0x274), sizeof sd.b);
    sd.body = obj;
    skinDraws.push_back(sd);
}

void Runtime::object_draw(uint32_t obj)
{
    struct Keep { int& v; int s; ~Keep() { v = s; } } keep{objectDraw_, objectDraw_};
    ++objectDraw_;
    port::Game<A>::object_draw(obj);
}

void Runtime::body_draw(uint32_t obj)
{
    struct Keep { int& v; int s; ~Keep() { v = s; } } keep{objectDraw_, objectDraw_};
    struct KeepBody { uint32_t& v; uint32_t s; ~KeepBody() { v = s; } } keepBody{drawBody_, drawBody_}, keepPart{drawPart_, drawPart_},
        keepBossPart{drawBossPart_, drawBossPart_};
    ++objectDraw_;
    drawBody_ = obj;
    drawPart_ = 0;
    drawBossPart_ = 0;
    port::Game<A>::body_draw(obj);
}

void Runtime::copro(int n, const uint32_t* in, int count, Tgp::Out& out)
{
    if (n == 0x77 && count == 1 && in[0] == 3 && objectDraw_ > 0) {   // the shadow pass (object_draw's): the matrix
                                                                       // before the projection is multiplied in
        shadowPass_ = true;
        std::memcpy(shadowBase_, tgp.matrix(), sizeof shadowBase_);
    } else if (n == 0x06) {
        shadowPass_ = false;
    }
    port::Game<A>::copro(n, in, count, out);
}

// 0x9CE60 body_part_draw_hook (common path): by the part's class (role table +3) the look angles of the head (+0x120..
// +0x128) / torso (+0x12C..) / class 8 (+0x138..), and the anchor matrices the effects and skins use (+0x154 head,
// +0x184 / +0x1B4 skin anchors, +0x214, +0x244, Sophie's +0x274 / +0x2A4); the type's home class marks the home part;
// then the part's model. Not yet: the eye / target beams (class 1 with draw_flags bit 7 / motion_flags bit 0) and the
// special types 2, 13-15, 30, 34 (logged; drawn with the default model).
void Runtime::body_part_draw_hook(uint32_t part)
{
    drawPart_ = part;   // ragdoll: this part's models and its shadow (DrawModel::part; draws only)
    struct KeepJoint { int16_t& v; int16_t s; ~KeepJoint() { v = s; } } keepJoint{drawJoint_, drawJoint_};
    drawJoint_ = int16_t(mem.u32(part + 0x38));
    const uint32_t body = mem.u32(kBodyCurrent);
    const int32_t type = mem.at<int16_t>(body + 0xCC);
    const int8_t cls = mem.at<int8_t>(kPartRoles + uint32_t(type) * 0xF8 + mem.u32(part + 0x38) * 8 + 3);
    Tgp::Out none;
    auto turn = [&](uint32_t off, int fn, bool neg) {
        const uint32_t v = mem.u32(body + off);
        if (v == 0)
            return;
        uint32_t a[1] = {neg ? 0u - v : v};
        copro(fn, a, 1, none);
        if (pm_body()) {   // PC Mode: the PC's hook 0x51D014 turns PM's stack too (its file's copies 0x51B5B4 x,
                           // 0x51B320 y, 0x51B428 z; pmbody)
            if (fn == 0x14)
                pm_cur().rotate_x(a[0]);
            else if (fn == 0x15)
                pm_cur().rotate_y(a[0]);
            else
                pm_cur().rotate_z(a[0]);
        }
    };
    auto save = [&](uint32_t off) {
        Tgp::Out m;
        copro(0x11, nullptr, 0, m);
        for (uint32_t k = 0; k < 12; ++k)
            mem.set32(body + off + 4 * k, m.w[k]);
        if (pm_body()) {   // PC Mode: the PC's hook saves its own matrix there. The hands (+0x214 / +0x244: the axe and
                           // held-object PC forms read them back through pm_copro_words_at) whole, in the coprocessor's
                           // layout; the others only their translation (the same words in both layouts: the head's
                           // 3 x 3 still feeds the arcade's eye beam). pmbody
            const port::pm::Matrix& c = pm_cur();
            if (off == 0x214 || off == 0x244)
                for (uint32_t row = 0; row < 3; ++row)
                    for (uint32_t col = 0; col < 3; ++col)
                        mem.set32(body + off + 4 * (col * 3 + row), fbits(c.e[row * 3 + col]));
            for (uint32_t k = 9; k < 12; ++k)
                mem.set32(body + off + 4 * k, fbits(c.e[k]));
        }
    };
    const uint8_t df = mem.at<uint8_t>(body + 0x3C4);
    switch (cls) {
    case 1:
        if ((df >> 7) & 1)   // looks at the camera (the port)
            body_hook_eye_beam(body);
        else if (mem.at<uint8_t>(body + 0x3C5) & 1)   // at the object +0x390 (the port)
            body_hook_target_beam(body);
        else
            body_hook_angle_ease(body);
        turn(0x128, 0x16, true);
        turn(0x124, 0x15, false);
        turn(0x120, 0x14, false);
        save(0x154);
        break;
    case 2: case 3:
        turn(0x134, 0x16, true);
        turn(0x130, 0x15, false);
        turn(0x12C, 0x14, false);
        break;
    case 6: save(0x244); break;
    case 7: save(0x214); break;
    case 8:
        turn(0x140, 0x16, true);
        turn(0x13C, 0x15, false);
        turn(0x138, 0x14, false);
        if ((df >> 4) & 1)
            save(0x184);
        break;
    case 9: if ((df >> 4) & 1) save(0x1B4); break;
    case 10: if (type == 34) save(0x2A4); break;
    case 11: if (type == 34) save(0x274); break;
    default: break;
    }
    if (int32_t(mem.u32(kHomeClass + 4 * uint32_t(type))) == int32_t(cls))
        mem.at<int16_t>(kBodyHome) = 1;
    if (type == 50 || type == 69) {
        // 0x9DACC (types 50 the Hangedman, 69; ch2proof, from the disassembly): at joint 1 the hit-flash counter
        // (+0x2F4 of the part's body) counts down to 0 and the wings are drawn there: model (+0x50 % 100) + 0x1401
        // (type 50) / + 0x195F (type 69), kept in +0x54 (the fight's bob reads it); then the part's own model, with
        // the hit flash (model record word 1 = 0x802400) while the counter is odd, draw flags bit 0 set / cleared
        const uint32_t owner = mem.u32(part + 0x40);
        if (mem.u32(part + 0x38) == 1) {
            const int32_t c = int32_t(mem.u32(owner + 0x2F4)) - 1;
            mem.set32(owner + 0x2F4, uint32_t(c < 0 ? 0 : c));
            const uint32_t wing = uint32_t(int32_t(mem.u32(body + 0x50)) % 100) + (type == 50 ? 0x1401u : 0x195Fu);
            mem.set32(owner + 0x54, wing);
            geo_draw_model(wing);
        }
        if (int32_t(mem.u32(owner + 0x2F4)) % 2 != 0) {
            // flash: the model's table words straight to the geometrizer with texture header 0x802400 (header RAM:
            // the boot's flat white set, Runtime::geo_draw_header); THOTD.EXE's hook (0x51D8D4) draws nothing then
            geo_draw_header(mem.u32(part), 0x802400);
            mem.at<uint8_t>(owner + 0x3C4) |= 1;
        } else {
            geo_draw_model(mem.u32(part));
            mem.at<uint8_t>(owner + 0x3C4) &= 0xFE;
        }
        return;
    }
    const uint32_t k = uint32_t(int32_t(int16_t(uint16_t(type) - 2)) & 0xFFFF);
    if ((k == 28 || k == 32) && body_hook_special(part, body, int32_t(k), cls))   // T1 (t1_bounds.cpp)
        return;
    {
        // ch4proof (the whole-game differential found type 55's +0x54 missing; read from the decompilation of
        // 0x9CE60's type switch): the other types' animated models. Type 55 (the Hangedman's fly-by, Chapter 1) on
        // joint 1: its own model, the wing model timer % 100 + 0x7CD, and the owner's +0x54 = timer % 100 + 0x1401
        // (hangedman_flyby_frame bobs up or down by it). The rest draw only: type 48 class 9 with flags bit 28 model
        // 0x16EF; 59 class 2 blink % 50 + 0x1618; 60 class 1 in motion 0x12A (frame / 3) % 15 + 0x1B52; 64 class 1
        // its model and blink % 49 + 0x1AEE; 70 / 92 class 2 blink % 50 + 0x1895; 78 class 1 in motions 0x128 /
        // 0x12A (frame / 3) % 15 + 0x1CEF; 84 joint 5 with part +0x78 clear 0x1771 + blink % 15; 88 / 93 class 2
        // blink % 50 + 0x1A56
        const int32_t blink = mem.at<int32_t>(G::blink_phase);
        const int32_t timer = int32_t(mem.u32(body + 0x50));
        const int32_t motion = mem.at<int16_t>(body + 0xD0), frame = mem.at<int16_t>(body + 0xF4);
        int32_t model = -1;
        switch (k) {
        case 0x2E:
            if (cls == 9 && ((mem.u32(body + 0x58) >> 28) & 1))
                model = 0x16EF;
            break;
        case 0x35:
            if (mem.u32(part + 0x38) == 1) {
                geo_draw_model(mem.u32(part));
                geo_draw_model(uint32_t(timer % 100 + 0x7CD));
                mem.set32(mem.u32(part + 0x40) + 0x54, uint32_t(timer % 100 + 0x1401));
                return;
            }
            break;
        case 0x39:
            if (cls == 2)
                model = blink % 0x32 + 0x1618;
            break;
        case 0x3A:
            if (cls == 1 && motion == 0x12A)
                model = int16_t((frame / 3) % 0xF) + 0x1B52;
            break;
        case 0x3E:
            if (cls == 1) {
                geo_draw_model(mem.u32(part));
                model = blink % 0x31 + 0x1AEE;
            }
            break;
        case 0x44: case 0x5A:
            if (cls == 2)
                model = blink % 0x32 + 0x1895;
            break;
        case 0x4C:
            if (cls == 1 && (motion == 0x128 || motion == 0x12A))
                model = int16_t((frame / 3) % 0xF) + 0x1CEF;
            break;
        case 0x52:
            if (mem.u32(part + 0x38) == 5 && mem.at<uint8_t>(part + 0x78) == 0)
                model = 0x1771 + blink % 0xF;
            break;
        case 0x56: case 0x5B:
            if (cls == 2)
                model = blink % 0x32 + 0x1A56;
            break;
        default: break;
        }
        if (model >= 0) {
            geo_draw_model(uint32_t(model));
            return;
        }
    }
    if (k == 0x4A) {
        // flash (from the disassembly 0x9E0F8..0x9E814): type 76, the Magician. The part's own counter +0xAC counts
        // down to 0; the owner's +0x2F4 too on class 8. The flash is on while the owner was shot this frame (+0x58
        // bit 8) or its +0x2F4 or the part's +0xAC is odd, but not in chapter 5. Classes 3 / 4 / 11 / 12 first draw a
        // vein model of the 25-member cycle (blink_phase / 2 % 25 + 0x1C2A / 0x1C75 / 0x1C47 / 0x1C07), flashed too;
        // class 8 sets the owner's draw flags bit 0 to the flash; then the part's model: with the flash's texture
        // header 0x802400, or in chapter 4 with event flag 96 (0x51E9A0) the classes 3 / 4 / 11 / 12 with header
        // 0x8F0400 (the boot's flat set of palette entry 959, palette_animation's 0x5206E0), else plain
        const uint32_t owner = mem.u32(part + 0x40);
        const int32_t c = int32_t(mem.u32(part + 0xAC)) - 1;
        mem.set32(part + 0xAC, uint32_t(c < 0 ? 0 : c));
        auto flashOn = [&] {
            const bool on = ((mem.at<uint16_t>(owner + 0x58) >> 8) & 1) || int32_t(mem.u32(owner + 0x2F4)) % 2 != 0 ||
                            int32_t(mem.u32(part + 0xAC)) % 2 != 0;
            return on && mem.at<uint8_t>(G::chapter) != 5;
        };
        const uint32_t vein = uint32_t(mem.at<int32_t>(G::blink_phase) / 2 % 25);
        uint32_t base = 0;
        switch (cls) {
        case 3: base = 0x1C2A; break;
        case 4: base = 0x1C75; break;
        case 11: base = 0x1C47; break;
        case 12: base = 0x1C07; break;
        case 8: {
            const int32_t t = int32_t(mem.u32(owner + 0x2F4)) - 1;
            mem.set32(owner + 0x2F4, uint32_t(t < 0 ? 0 : t));
            mem.at<uint8_t>(owner + 0x3C4) = uint8_t((mem.at<uint8_t>(owner + 0x3C4) & 0xFE) | (flashOn() ? 1 : 0));
            break;
        }
        default: break;
        }
        if (base != 0) {
            if (flashOn())
                geo_draw_header(base + vein, 0x802400);
            else
                geo_draw_model(base + vein);
        }
        if (flashOn()) {
            geo_draw_header(mem.u32(part), 0x802400);
            return;
        }
        if ((cls == 3 || cls == 4 || cls == 11 || cls == 12) && mem.at<uint8_t>(0x51E9A0) != 0 && mem.at<uint8_t>(G::chapter) == 3) {
            geo_draw_header(mem.u32(part), 0x8F0400);
            return;
        }
        geo_draw_model(mem.u32(part));
        return;
    }
    if (k == 0 || k == 11 || k == 12 || k == 13) {
        // the players' characters (types 2, 13-15; R4, from the decompilation of 0x9CE60, UNVERIFIED (teaser)): by
        // class, motion and frame another model in the part's place (the drawn gun, hand poses), and on class 8 parts
        // of type 2 (Rogan) a second model after the part's own (animated by frame)
        const int32_t motion = mem.at<int16_t>(body + 0xD0), f = mem.at<int16_t>(body + 0xF4);
        auto between = [](int32_t v, int32_t lo, int32_t hi) { return lo < v && v < hi; };
        if (cls == 7 && between(motion, 0xC1, 0xC4) && between(f, 0x1D, 100)) {
            geo_draw_model(0x716);
            return;
        }
        if (cls == 6) {
            if (motion == 0x168) { geo_draw_model(0x36B); return; }
            if (between(motion, 0xC1, 0xC4) && f == 2) {
                // ch4proof (from the disassembly 0x9D76C..0x9D904): frame 2 of the reload drops the magazine, a
                // magazine_drop task (0x9CCD0, 0x3C8 bytes) at (-0.5, -0.4, 0) from the hand turned -0xC00 on x and
                // 0x8000 on y (coprocessor 0x20: place, 0x6F: headings into +0x30 / +0x2C / +0x34), its drift / fall
                // speeds 2 x cos / sin of the x heading x 0.0167 (0x1C / 0x1B) in +0x78 / +0x7C
                const uint32_t t = task_create(F::magazine_drop, 0x3C8);
                Tgp::Out pos, ang, c, s;
                copro(0x05, nullptr, 0, none);
                uint32_t tr[3] = {0xBF000000u, 0xBECCCCCDu, 0};
                copro(0x12, tr, 3, none);
                uint32_t rx[1] = {0xFFFFF400u};
                copro(0x14, rx, 1, none);
                uint32_t ry[1] = {0x8000};
                copro(0x15, ry, 1, none);
                copro(0x20, nullptr, 0, pos);
                for (uint32_t i = 0; i < 3; ++i)
                    mem.set32(t + 0x20 + 4 * i, pos.w[i]);
                copro(0x6F, nullptr, 0, ang);
                mem.set32(t + 0x30, ang.w[0]);
                mem.set32(t + 0x2C, ang.w[1]);
                mem.set32(t + 0x34, ang.w[2]);
                copro(0x06, nullptr, 0, none);
                uint32_t a[1] = {mem.u32(t + 0x2C)};
                copro(0x1C, a, 1, c);
                mem.set32(t + 0x78, port::fbits(port::ffrom(c.w[0]) * 2.0f * port::ffrom(0x3C888F86u)));
                copro(0x1B, a, 1, s);
                mem.set32(t + 0x50, 0);
                mem.set32(t + 0x7C, port::fbits(port::ffrom(s.w[0]) * 2.0f * port::ffrom(0x3C888F86u)));
            }
            if (between(motion, 0xC1, 0xC4) || (between(motion, 0x16F, 0x172) && between(f, 0x4C, 0x57))) {
                geo_draw_model(0x2B3);
                return;
            }
            if ((motion == 0xBC && between(f, 0x13, 0x29)) || (motion == 0x153 && between(f, 0, 0x13))) {
                geo_draw_model(0x71B);
                return;
            }
            int32_t model = -1;
            if (motion == 0x1CF)
                model = between(f, 0x2D, 0x37) ? f + 0x1D : between(f, 0x4D, 0x57) ? f - 3 : between(f, 0x6C, 0x76) ? f - 0x22 : -1;
            else if ((motion == 0x1CE || motion == 0x1D2) && between(f, 0, 10))
                model = f + 0x4A;
            if (model >= 0) {
                // the gun 0x71B with its camera-space place read back into the part's +0x5C (geo_matrix_position),
                // the prop turned 0x4000 on z and -0x4000 on x, drawn; at frames 47 / 79 / 110 of 0x1CF and 2 of
                // 0x1CE / 0x1D2 (the body's +0x3C7 bit 1 clear) a cat piece flies off there (caaat_piece_spawn).
                // ch4proof (the whole-game differential, Chapter 1, from the disassembly 0x9D454..0x9D700): the place
                // was not kept, the z turn and the pieces were missing
                geo_matrix_position(0x71B, part + 0x5C);
                uint32_t az[1] = {0x4000};
                copro(0x16, az, 1, none);
                uint32_t a[1] = {0xFFFFC000u};
                copro(0x14, a, 1, none);
                if (pm_body())
                    pm_cur().rotate_x(a[0]);
                geo_draw_model(uint32_t(model));
                const bool piece = motion == 0x1CF ? (f == 0x2F || f == 0x4F || f == 0x6E) : f == 2;
                if (piece && !((mem.at<uint8_t>(body + 0x3C7) >> 1) & 1))
                    caaat_piece_spawn(body, mem.u32(part + 0x5C), mem.u32(part + 0x60), mem.u32(part + 0x64));
                return;
            }
        }
        if (cls == 8) {
            geo_draw_model(mem.u32(part));
            // the arcade's coat (models 0xB5.., 0x119..) is a separate model; the PC release's Rogan parts include the
            // coat, so with the PC model source it is not drawn (it would show the arcade coat over the PC one)
            if (type == 2 && motion != 0x168 && !pcModels) {
                const int32_t ch = mem.at<int8_t>(G::chapter);
                const int32_t sub = int32_t(int16_t(mem.at<int16_t>(body + 0xF6) % 2));
                const int32_t m = ch == 5 || ch == 6 ? int32_t(mem.u32(G::frame_counter) / 2 % 0x32) + 0x119
                                                     : (sub * 0x32 + f / 2) % 100 + 0xB5;
                geo_draw_model(uint32_t(m));
            }
            return;
        }
    }
    geo_draw_model(mem.u32(part));
}

// ---------------------------------------------------------------------------------------------------------------
// 4P phase 6 (docs/coop4.md section 3): players 3 / 4's bodies in the cut-scenes that have room. Draw side only: after
// the frame's logic, players 1 / 2's body draws of this frame (DrawModel::body, SkinDraw::body) are copied with the
// models of types 14 / 15 ("Rogan 2" / "G 2": the same human skeleton, part for part) and moved beside their pair.
// Nothing here writes the address space (the part trees and type headers are only read), so the simulation, the shot
// test and the netplay hashes are those of the two-body game.

// the part models of type `from` -> those of type `to`, walking both types' part trees (creature_type_headers) in step;
// the shadow models (per role) too. Empty when the trees differ (no copy is drawn then).
const std::map<uint32_t, uint32_t>& Runtime::coop4_part_map(int from, int to)
{
    const uint64_t key = (uint64_t(uint32_t(from)) << 32) | uint32_t(to);
    if (auto it = coop4PartMaps_.find(key); it != coop4PartMaps_.end())
        return it->second;
    std::map<uint32_t, uint32_t>& out = coop4PartMaps_[key];
    std::map<int, uint32_t>& joints = coop4JointMaps_[key];
    bool ok = true;
    ActorFit& fit = actorFits_[key];
    fit = {};
    std::function<void(uint32_t, uint32_t, int, int, std::vector<int>&)> walk = [&](uint32_t a, uint32_t b, int depth, int parent,
                                                                                    std::vector<int>& sub) {
        if (!ok || depth > 16)
            return;
        out[mem.u32(a)] = mem.u32(b);
        joints[int(mem.u32(b + 0x14))] = mem.u32(b);   // (part data +0x14: the role, the part node's +0x38)
        const int role = mem.at<int32_t>(a + 0x14);
        sub.push_back(role);
        const int32_t n = mem.at<int32_t>(a + 0x18);
        if (n != mem.at<int32_t>(b + 0x18) || n < 0 || n > 32) {
            ok = false;
            return;
        }
        // galfix4: a joint placed elsewhere on the actor (more than a hundredth): its leg (part data +4.., the offset)
        ActorFit::Leg leg;
        for (uint32_t k = 0; k < 3; ++k)
            leg.d[k] = mem.f32(b + 4 + 4 * k) - mem.f32(a + 4 + 4 * k);
        const bool moved = parent >= 0 && (std::fabs(leg.d[0]) > 0.01f || std::fabs(leg.d[1]) > 0.01f || std::fabs(leg.d[2]) > 0.01f) &&
                           (fit.parent < 0 || fit.parent == parent);
        std::vector<int> mine = {role};
        for (int32_t i = 0; i < n; ++i)
            walk(mem.u32(a + 0x1C + 4 * uint32_t(i)), mem.u32(b + 0x1C + 4 * uint32_t(i)), depth + 1, role, mine);
        sub.insert(sub.end(), mine.begin() + 1, mine.end());
        if (moved) {
            fit.parent = parent;
            leg.role = role;
            leg.sub = std::move(mine);
            fit.legs.push_back(std::move(leg));
        }
    };
    const uint32_t ha = mem.u32(G::creature_type_headers + 4 * uint32_t(from));
    const uint32_t hb = mem.u32(G::creature_type_headers + 4 * uint32_t(to));
    const int32_t n = mem.at<int32_t>(ha + 0x18);
    if (ha == 0 || hb == 0 || n != mem.at<int32_t>(hb + 0x18) || n < 0 || n > 32)
        ok = false;
    std::vector<int> all;
    for (int32_t i = 0; ok && i < n; ++i)
        walk(mem.u32(ha + 0x1C + 4 * uint32_t(i)), mem.u32(hb + 0x1C + 4 * uint32_t(i)), 1, -1, all);
    if (!ok)
        fit = {};
    if (log && !fit.legs.empty())
        log("actor fit: type " + std::to_string(from) + " -> " + std::to_string(to) + ": " + std::to_string(fit.legs.size()) +
            " joints placed elsewhere under role " + std::to_string(fit.parent));
    const uint32_t sa = mem.u32(0x14BA80 + 4 * uint32_t(from)), sb = mem.u32(0x14BA80 + 4 * uint32_t(to));   // shadow models by role
    if (ok && sa != 0 && sb != 0)
        for (uint32_t r = 0; r < 32; ++r)
            if (const uint32_t m = mem.u32(sa + 4 * r); m != 0 && !out.count(m))
                out[m] = mem.u32(sb + 4 * r);
    if (!ok)
        out.clear(), joints.clear();
    if (log)
        log("4P cut-scene bodies: type " + std::to_string(from) + " -> " + std::to_string(to) + ": " +
            (ok ? std::to_string(out.size()) + " models" : std::string("part trees differ, not drawn")));
    return out;
}

// (user 2026-10-26: "P3 / P4's hat / beard / glasses pop in and out" in 4-19: G's talking head draws his mouth frames,
// models the part tree does not list, so the copies drew G's own head there, without Rogan 2's cap or G 2's beard and
// glasses, whenever he spoke)
uint32_t Runtime::coop4_copy_model(const std::map<uint32_t, uint32_t>& map, int from, int to, const DrawModel& d)
{
    // galfix4 (user 2026-10-30, 4-19 with Sophie as Rogan: "she is not holding a gun either"): the players' gun hand
    // (class 6: the part's own model is the hand with the gun, body_part_draw_hook swaps in the empty hand 0x36B / the
    // reload hands by motion) stays the role's on an actor who has no gun of their own (Sophie, the researchers, Dr.
    // Curien, the zombie Sophie): the role's prop goes with the role. A player type playing it uses their own gun hand
    auto player = [](int t) { return t == 2 || (t >= 13 && t <= 15); };
    if (!d.shadow && player(from) && !player(to) && d.joint >= 0 && d.joint < 31 &&
        mem.at<int8_t>(kPartRoles + uint32_t(from) * 0xF8 + uint32_t(d.joint) * 8 + 3) == 6) {
        // rellook (user 2026-10-30: the zombie Sophie's gun hand "looks healthy", Rogan's): her own right hand (her part
        // model of the joint, drawn in her texture area 2), whichever hand the hook swapped in (the gun hand, the empty
        // hand 0x36B); the gun goes on as a second draw (actor_gun_model). Arcade models only (the PC's zombie Sophie is
        // Sophie's model)
        if (to == 67 && !pcModels)
            if (auto it = map.find(mem.u32(d.part)); it != map.end())
                return it->second;
        return d.model;
    }
    if (auto it = map.find(d.model); it != map.end())
        return it->second;
    if (d.joint < 0 || d.joint >= 31 || mem.at<int8_t>(kPartRoles + uint32_t(from) * 0xF8 + uint32_t(d.joint) * 8 + 3) != 1)
        return d.model;   // (a hand's gun, a prop: the same model for every type)
    const auto jm = coop4JointMaps_.find((uint64_t(uint32_t(from)) << 32) | uint32_t(to));
    if (jm != coop4JointMaps_.end())
        if (auto j = jm->second.find(d.joint); j != jm->second.end())
            return j->second;
    return d.model;
}

// rellook: the second draw of a player's gun hand played by the zombie Sophie (coop4_copy_model gives her own hand):
// the gun alone (kGunOnlyKey, ArcadeSource::loadStage), with the hand's matrix. 0: none (another actor, another hand)
uint32_t Runtime::actor_gun_model(int from, int to, const DrawModel& d)
{
    auto player = [](int t) { return t == 2 || (t >= 13 && t <= 15); };
    if (to != 67 || pcModels || d.shadow || !player(from) || d.joint < 0 || d.joint >= 31 || d.model != kPlayerGunHand ||
        mem.at<int8_t>(kPartRoles + uint32_t(from) * 0xF8 + uint32_t(d.joint) * 8 + 3) != 6)
        return 0;
    return kGunOnlyKey;
}

// the type players 3 / 4 (s 0 / 1) are drawn with as a copy of a body of type `from`: 14 / 15; PC Mode: the seat's
// picked character's (when its part tree matches, else 14 / 15). -1: no copy (the body is that seat's own: set B)
int Runtime::coop4_scene_type(int from, int s)
{
    if (const int t = actorSwap[2 + (s & 1)]; t >= 0 && (t == from || !coop4_part_map(from, t).empty()))
        return t;   // actorswap: the cut-scene viewer's pick for this seat (a part tree that differs: the usual body)
    if (chars_on()) {
        if (mem.u32(port::coop4::kMagic) == 0x34504334u && mem.at<uint8_t>(port::coop4::kBodySetB) != 0)
            return -1;
        const int t = pm_body_type(2 + s);
        if (t == from || !coop4_part_map(from, t).empty())
            return t;
    }
    return from == 14 + s ? -1 : 14 + s;
}

namespace {
// the player cut-scene of a scene routine (player +0xB0; player_body.cpp's routines by address), -1 = none
int coop4SceneOf(uint32_t r)
{
    static const int32_t starts[][2] = {{0x4BAB0, 0}, {0x4BFB0, 2}, {0x4C3A0, 3}, {0x4C550, 4}, {0x4C700, 5},
                                         {0x4C980, 6}, {0x4CC00, 7}, {0x4CEE0, 8}, {0x4D0B0, 9}, {0x4D2F0, 10},
                                         {0x4D450, 11}, {0x4D5C0, 12}, {0x4D710, 13}, {0x4D870, 14}, {0x4DB00, -1}};
    if (r < uint32_t(starts[0][0]))
        return -1;
    int s = -1;
    for (const auto& e : starts)
        if (r >= uint32_t(e[0]))
            s = int(e[1]);
    return s;
}

// per scene: four bodies or the two-set rule; per player 3 / 4, in camera space from its pair body: `out` = across the
// picture (outward of the pair; negative: toward the middle), `away` = deeper. Judged by eye (main checkout
// private\coop4f\, retuned in private\coop4g\; 06 / 07 / 10 / 11 / 12 told apart by Game::coop4_scene and retuned by
// worker "coop4h": what coop4g took for 07 / 06 in a whole-game run were Chapter 2's 11 / 12)
struct SceneFour {
    bool four;
    float out[2], away[2];
};
constexpr SceneFour kSceneFour[15] = {
    {true, {5, 6}, {13, 17}},      // 00 the opening run
    {false, {}, {}},               // 01
    {true, {5, 6}, {13, 17}},      // 02 walk and shoot
    {false, {}, {}},               // 03 ladder
    {false, {}, {}},               // 04 fall backside
    {false, {}, {}},               // 05 knocked flying
    {true, {4, -6}, {2, 10}},      // 06 reload shoot (Chapter 1 route 1:3=1,1:4=1 ~12488: 3 beside 1 in front, 4 right of 2)
    {true, {9, 6}, {6, 4}},        // 07 jump down a (Chapter 1 route 1:3=1,1:4=1 ~12772: 3 in the doorway left of 1, 4 right of 2)
    {false, {}, {}},               // 08 finish pose
    {true, {7, -8}, {6, 0}},       // 09 stance shoot (4 between 2 at the left door and 1, 3 right of 1)
    {true, {6, -3}, {10, 8}},      // 10 jump down b (Chapter 2 ~11980: 3 jumps beside 1, 4 waits behind the rail between 1 and 2)
    {true, {9, -4}, {10, 12}},     // 11 jump down c (Chapter 2 ~1313: 3 left of 1, 4 beside 2 on the stairs)
    {true, {8, -2}, {14, 26}},     // 12 reload kick (Chapter 2 ~7462: 3 kicks beside 1, 4 deeper between 1 and 2)
    {false, {}, {}},               // 13 knocked flying b
    {false, {}, {}},               // 14 rattle door
};
} // namespace

int Runtime::player_scene_now() const
{
    int scene = coop4SceneOf(mem.at<port::a::Player>(G::players, 0).scene_routine);
    if (scene < 0)
        scene = coop4SceneOf(mem.at<port::a::Player>(G::players, 1).scene_routine);
    if ((scene == 7 && (scene_started == 10 || scene_started == 11)) || (scene == 6 && scene_started == 12) ||
        (scene == 5 && scene_started == 13))
        scene = scene_started;
    return scene;
}

void Runtime::coop4_scene_bodies()
{
    static const bool twoBodies = std::getenv("HOTD_COOP4_TWO_BODIES") != nullptr;   // testing: the two-body picture (before / after shots)
    if (twoBodies || !coop4_on())
        return;
    // the players in play (state 5); in the ending game_ending_start put players 1 / 2 to 7 (players 3 / 4 stay 5)
    if (ending_scene_now()) {
        int n = 0;
        for (int p = 0; p < seat_count(); ++p)
            n += mem.at<int32_t>(player_rec(p)) == 5 || mem.at<int32_t>(player_rec(p)) == 7 ? 1 : 0;
        if (n < 3)
            return;
    } else if (coop4_players_real() < 3) {
        // two in play: the arcade's two bodies show them when they are players 1 and 2, or players 3 / 4 alone (the
        // set B bodies); player 1 or 2 with player 3 or 4 (the others out: user 2026-10-26 "when p3 died ... only
        // Rogan showed up") draws the seat's body beside the one arcade body, as with three players
        const bool front = mem.at<int32_t>(player_rec(0)) == 5 || mem.at<int32_t>(player_rec(1)) == 5;
        const bool back = mem.at<int32_t>(player_rec(2)) == 5 || mem.at<int32_t>(player_rec(3)) == 5;
        if (!front || !back)
            return;
    }
    if (coop4_scene_variant(coop4_variant_scene()))   // a variation other than A picked for this scene
        return;
    const bool seat[2] = {mem.at<int32_t>(player_rec(2)) == 5, mem.at<int32_t>(player_rec(3)) == 5};
    const uint32_t body[2] = {mem.at<port::a::Player>(G::players, 0).body.a, mem.at<port::a::Player>(G::players, 1).body.a};
    int scene = coop4SceneOf(mem.at<port::a::Player>(G::players, 0).scene_routine);
    if (scene < 0)
        scene = coop4SceneOf(mem.at<port::a::Player>(G::players, 1).scene_routine);
    // the later routines of scenes 10 / 11 (jump downs b / c) are 07's, 12's (reload kick) 06's, 13's 05's: the scene
    // cam_job18 started tells them apart
    if ((scene == 7 && (coop4_scene == 10 || coop4_scene == 11)) || (scene == 6 && coop4_scene == 12) ||
        (scene == 5 && coop4_scene == 13))
        scene = coop4_scene;
    static const bool trace = std::getenv("HOTD_TRACE_COOP4_SCENE") != nullptr;
    if (trace && log && scene != coop4SceneLogged_) {
        log("4P cut-scene " + std::to_string(scene) + " at frame " + std::to_string(frameCount));
        coop4SceneLogged_ = scene;
    }
    // testing: HOTD_COOP4_SCENE_TUNE="S=out3,out4,away3,away4;..." overrides (and turns on) scene S's offsets
    static const std::vector<std::pair<int, SceneFour>> tune = [] {
        std::vector<std::pair<int, SceneFour>> v;
        if (const char* e = std::getenv("HOTD_COOP4_SCENE_TUNE"))
            for (const char* p = e; *p;) {
                int sc = 0;
                SceneFour f{true, {}, {}};
                if (std::sscanf(p, "%d=%f,%f,%f,%f", &sc, &f.out[0], &f.out[1], &f.away[0], &f.away[1]) == 5 && sc >= 0 && sc < 15)
                    v.emplace_back(sc, f);
                const char* q = std::strchr(p, ';');
                p = q ? q + 1 : p + std::strlen(p);
            }
        return v;
    }();
    if (scene < 0 || body[0] == 0)
        return;
    SceneFour sf = kSceneFour[scene];
    for (const auto& [sc, f] : tune)
        if (sc == scene)
            sf = f;
    if (!sf.four)
        return;
    // the drawn bodies' places (camera space: the body matrix +0x1E4's translation) and axes
    bool drawn[2] = {false, false};
    for (const DrawModel& d : drawList)
        for (int i = 0; i < 2; ++i)
            drawn[i] = drawn[i] || (d.body != 0 && d.body == body[i]);
    if (!drawn[0] && !drawn[1])
        return;
    float at[2][3] = {}, bx[2][3] = {};
    for (int i = 0; i < 2; ++i)
        if (body[i] != 0)
            for (int k = 0; k < 3; ++k) {
                bx[i][k] = mem.f32(body[i] + 0x1E4 + 4 * uint32_t(k));
                at[i][k] = mem.f32(body[i] + 0x1E4 + 4 * uint32_t(9 + k));
            }
    // player 1's side of the picture (camera x: player 1's body left or right of player 2's; one body: its own x axis)
    const float p1Side = drawn[0] && drawn[1] ? (at[0][0] < at[1][0] ? -1.0f : 1.0f)
                                              : (bx[drawn[0] ? 0 : 1][0] < 0 ? -1.0f : 1.0f);
    const size_t nDraw = drawList.size(), nSkin = skinDraws.size();
    for (int s = 0; s < 2; ++s) {
        if (!seat[s])
            continue;
        // player 3 copies player 1's body, player 4 player 2's (player 1's when player 2's is not drawn)
        const int src = s == 1 && drawn[1] ? 1 : drawn[0] ? 0 : 1;
        const int fromType = mem.at<int16_t>(body[src] + 0xCC), toType = coop4_scene_type(fromType, s);
        if (toType < 0)
            continue;
        const std::map<uint32_t, uint32_t>& map = coop4_part_map(fromType, toType);
        if (map.empty())
            continue;
        // the offset in camera space: across the picture (player 3 out on player 1's side, player 4 on player 2's) and
        // deeper; a copy of player 1's body for player 4 goes on player 1's side, further out (player 3 out of play:
        // player 3's own place beside player 1)
        const float side = src == 0 ? p1Side : -p1Side;
        const int slot = s == 1 && src == 0 && !seat[0] ? 0 : s;
        const float out = s == 1 && src == 0 && seat[0] ? sf.out[0] * 2.0f : sf.out[slot];
        const float off[3] = {side * out, 0.0f, sf.away[slot]};
        if (trace && log && frameCount % 40 == 0) {
            char buf[200];
            std::snprintf(buf, sizeof buf, "4P body %d from %d drawn %d%d at %.1f %.1f %.1f / %.1f %.1f %.1f off %.1f %.1f %.1f", s + 3, src + 1,
                          int(drawn[0]), int(drawn[1]), at[0][0], at[0][1], at[0][2], at[1][0], at[1][1], at[1][2], off[0], off[1], off[2]);
            log(buf);
        }
        if ((actorView || actorPm) && actorSwap[2 + s] >= 0) {
            // actors2 (the cut-scene viewer's pick for this seat): the actor's pose, Rogan's coat, Sophie's strip
            std::vector<DrawModel> dOut;
            std::vector<SkinDraw> sOut;
            actor_dress(body[src], fromType, toType, map, nDraw, nSkin, dOut, sOut);
            for (DrawModel& c : dOut) {
                for (int k = 0; k < 3; ++k)
                    c.m[9 + k] += off[k];
                drawList.push_back(c);
            }
            for (SkinDraw& c : sOut) {
                for (int k = 0; k < 3; ++k)
                    c.a[9 + k] += off[k], c.b[9 + k] += off[k];
                skinDraws.push_back(c);
            }
            continue;
        }
        for (size_t i = 0; i < nDraw; ++i) {
            const DrawModel& d = drawList[i];
            if (d.body != body[src])
                continue;
            if (isRoganCoat(d, mem.u32(d.part)))   // Rogan's coat (body_part_draw_hook): his own
                continue;
            DrawModel c = d;
            c.model = coop4_copy_model(map, fromType, toType, d);
            const uint32_t gunModel = actor_gun_model(fromType, toType, d);   // (rellook)
            for (int k = 0; k < 3; ++k)
                c.m[9 + k] += off[k];
            drawList.push_back(c);
            if (gunModel != 0)
                c.model = gunModel, drawList.push_back(c);
        }
        for (size_t i = 0; i < nSkin; ++i) {
            if (skinDraws[i].body != body[src])
                continue;
            SkinDraw c = skinDraws[i];
            c.type = toType;
            for (int k = 0; k < 3; ++k)
                c.a[9 + k] += off[k], c.b[9 + k] += off[k];
            skinDraws.push_back(c);
        }
    }
}

// actorswap (the cut-scene viewer's Actors): players 1 / 2's body draws of this frame with another type's part models
// (the same pose: each part keeps its matrix). Every human body shares the players' skeleton part for part (Rogan, G,
// players 3 / 4, Sophie, the zombie Sophie, both Dr. Curiens, the ten researchers: the same 20 joints and child
// counts, BO_ part trees at maincpu 0xC7240), so coop4_part_map maps them; a type whose part tree differs is not swapped
// (the role's own actor stays).
// actors2 (user 2026-10-26): the actor's own extras go with it (Rogan's arcade coat, Sophie's strip), and a woman plays a
// motion the game has a woman's version of with that one (actor_pose)
namespace {
// copro layout (axes images, then the translation; DrawModel::m): a after b
void apMul(const float* a, const float* b, float* o)
{
    float t[12];
    for (int j = 0; j < 4; ++j) {
        const float* v = b + 3 * j;
        for (int i = 0; i < 3; ++i)
            t[3 * j + i] = a[i] * v[0] + a[3 + i] * v[1] + a[6 + i] * v[2] + (j == 3 ? a[9 + i] : 0.0f);
    }
    std::memcpy(o, t, sizeof t);
}
void apInvRigid(const float* c, float* o)
{
    float t[12];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            t[3 * j + i] = c[3 * i + j];
    for (int i = 0; i < 3; ++i)
        t[9 + i] = -(c[3 * i] * c[9] + c[3 * i + 1] * c[10] + c[3 * i + 2] * c[11]);
    std::memcpy(o, t, sizeof t);
}
// part_draw's step: T(x, y, -z) (the joint offset) then the joint's angles a0, a1, a2 (coprocessor 0x86 with
// {-a2, a1, a0}: Rz(a2) Ry(-a1) Rx(-a0); 0x10000 = a turn)
void apJoint(const float off[3], const uint16_t a[3], float* o)
{
    constexpr float k = 3.14159265358979f / 32768.0f;
    const float x = -float(int16_t(a[0])) * k, y = -float(int16_t(a[1])) * k, z = float(int16_t(a[2])) * k;
    const float cx = std::cos(x), sx = std::sin(x), cy = std::cos(y), sy = std::sin(y), cz = std::cos(z), sz = std::sin(z);
    const float r[9] = {cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx,
                        sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx,
                        -sy,     cy * sx,                cy * cx};   // (row-major)
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            o[3 * j + i] = r[i * 3 + j];
    o[9] = off[0], o[10] = off[1], o[11] = off[2];
}
} // namespace

// Dr. Curien (types 60 / 78) as an actor on another body (part class `cls`): two of his parts are modelled for his
// own motions. The head: his motions hold the head joint about 0x4000 further round its first axis than the players'
// (rest frames, motions 298 / 362 frame 0: his 0x056E under a chest at 0x320D, the players' 0xBF94 under 0x4000: his
// head model is built lying on its side), so on the players' motions it lay on its side. The right hand (class 6)
// holds his cane, which the players' hand angles pointed out sideways ("a long stick"): it takes his own hand's rest
// angles relative to the forearm (players' 0000 BFFF 4000 -> his EAC9 DB68 545C), the cane down along his side. The
// matrix goes on the right of the drawn part's; false: none
bool Runtime::actorPartFix(int from, int to, int cls, float* fix)
{
    if (!(to == 60 || to == 78) || from == 60 || from == 78)
        return false;
    const float zero[3] = {0, 0, 0};
    if (cls == 1) {
        const uint16_t a[3] = {0x4000, 0, 0};
        apJoint(zero, a, fix);
        return true;
    }
    if (cls == 7) {
        const uint16_t pa[3] = {0xF23B, 0x00F0, 0xEA34}, ca[3] = {0x05C7, 0x1CEC, 0xE912};
        float rp[12], rc[12], inv[12];
        apJoint(zero, pa, rp);
        apJoint(zero, ca, rc);
        apInvRigid(rp, inv);
        apMul(inv, rc, fix);
        return true;
    }
    return false;
}

bool Runtime::actorFemale(int type)
{
    return type == 34 || type == 67 || type == 23 || type == 25 || type == 26 || type == 27;   // Sophie, zombie Sophie, the researchers on the woman's body (onn*)
}

// the woman's / man's version of a motion: the game's own pair (player_body: scenes 07 / 10 / 11's jump down, MO_p_tobio_a
// 0x178 for the men, MO_s_tobio_a 400 for Sophie, 97 frames each); -1 none (the part's own motion is kept)
int Runtime::actor_motion(int motion, int from, int to)
{
    if (actorFemale(to) == actorFemale(from))
        return -1;
    static const int16_t kPairs[][2] = {{0x178, 400}};   // {man's, woman's}
    for (const auto& p : kPairs)
        if (motion == p[actorFemale(to) ? 0 : 1])
            return p[actorFemale(to) ? 1 : 0];
    return -1;
}

// the pose of `body` (type from) as actor `to`: per body part (BodyPart address) the matrix C to apply after the drawn
// one (m C), the parts re-posed with the actor's own version of the motion (actor_motion) at the same frame: each joint
// angle moved by (its version - the motion's own) at that frame, so a blend into the motion stays a blend; the root's
// height too. Empty: nothing to change (no such version, a mirrored motion). Reads only: body_pose's frame data
// (+0xD8, the second motion's +0x110), the part tree, the motion records
const std::map<uint32_t, std::array<float, 12>>& Runtime::actor_pose(uint32_t body, int from, int to)
{
    static std::map<uint32_t, std::array<float, 12>> out;
    out.clear();
    const int motion = mem.at<int16_t>(body + 0xD0);
    const int target = actor_motion(motion, from, to);
    if (target < 0)
        return out;
    const uint8_t f2 = mem.at<uint8_t>(body + 0x3C6);
    if (mem.at<uint8_t>(kMirrored + uint32_t(motion)) != 0 || mem.at<uint8_t>(kMirrored + uint32_t(target)) != 0 || (f2 & 18) == 18)
        return out;
    const int32_t nOld = int32_t(mem.u32(kMotionFrames + 4 * uint32_t(motion))), nNew = int32_t(mem.u32(kMotionFrames + 4 * uint32_t(target)));
    const int32_t f = mem.at<int16_t>(body + 0xF4);
    if (nOld <= 0 || nNew <= 0 || f < 0 || f >= nOld || f >= nNew)
        return out;
    const uint32_t fd = mem.u32(body + 0xD8), m2 = mem.u32(body + 0x110);
    const uint32_t recOld = motion_frame_record(body, motion, f), recNew = motion_frame_record(body, target, f);
    if (fd == 0 || recOld == 0 || recNew == 0)
        return out;
    auto angles = [&](uint32_t data, int role, bool moved, uint16_t a[3]) {
        for (uint32_t k = 0; k < 3; ++k) {
            const uint32_t at = uint32_t(role) * 6 + 0xC + 2 * k;
            a[k] = mem.at<uint16_t>(data + at);
            if (moved)
                a[k] = uint16_t(a[k] + mem.at<uint16_t>(recNew + at) - mem.at<uint16_t>(recOld + at));
        }
    };
    // the root: body_draw's step (FixMode: the record's x, y, -z; else the height y) and the root joint's angles
    float dOld[3] = {0, mem.f32(fd + 4), 0}, dNew[3] = {0, mem.f32(fd + 4) + mem.f32(recNew + 4) - mem.f32(recOld + 4), 0};
    if (f2 & 1) {
        dOld[0] = mem.f32(fd), dOld[2] = -mem.f32(fd + 8);
        dNew[0] = dOld[0] + mem.f32(recNew) - mem.f32(recOld), dNew[2] = dOld[2] - (mem.f32(recNew + 8) - mem.f32(recOld + 8));
    }
    uint16_t a[3];
    float rootOld[12], rootNew[12];
    angles(fd, 0, false, a);
    apJoint(dOld, a, rootOld);
    angles(fd, 0, true, a);
    apJoint(dNew, a, rootNew);
    const bool split = (f2 >> 5) & 1;   // the second motion's parts (side bit 5 clear) keep their angles
    std::function<void(uint32_t, const float*, const float*, int)> walk = [&](uint32_t part, const float* pOld, const float* pNew, int depth) {
        const int32_t n = mem.at<int32_t>(part + 0x34);
        if (depth > 16 || n < 0 || n > 32)
            return;
        for (int32_t i = 0; i < n; ++i) {
            const uint32_t c = mem.u32(mem.u32(part + 0x24) + 4 * uint32_t(i));
            if (c == 0)
                continue;
            const float off[3] = {mem.f32(c + 4), mem.f32(c + 8), -mem.f32(c + 0xC)};
            const bool own = ((mem.at<uint8_t>(c + 0xD4) >> 5) & 1) || !split;
            const int role = mem.at<int32_t>(c + 0x38);
            float j[12], qOld[12], qNew[12];
            angles(own ? fd : m2, role, false, a);
            apJoint(off, a, j);
            apMul(pOld, j, qOld);
            angles(own ? fd : m2, role, own, a);
            apJoint(off, a, j);
            apMul(pNew, j, qNew);
            float inv[12];
            apInvRigid(qOld, inv);
            std::array<float, 12>& C = out[c];
            apMul(inv, qNew, C.data());
            walk(c, qOld, qNew, depth + 1);
        }
    };
    if (const uint32_t root = mem.u32(body + 0xC8); root != 0)
        walk(root, rootOld, rootNew, 1);
    // testing: HOTD_TRACE_ACTOR_POSE: how well the rebuilt pose matches the drawn one (the body's matrix from each part:
    // m P^-1 should be the same for every part; the head / chest look turns of the draw hook are not rebuilt)
    static const bool trace = std::getenv("HOTD_TRACE_ACTOR_POSE") != nullptr;
    if (trace && log && frameCount % 10 == 0) {
        float ref[12] = {};
        bool have = false;
        float worst = 0;
        int worstJoint = -1;
        std::function<void(uint32_t, const float*, int)> chk = [&](uint32_t part, const float* p, int depth) {
            const int32_t n = mem.at<int32_t>(part + 0x34);
            if (depth > 16 || n < 0 || n > 32)
                return;
            for (int32_t i = 0; i < n; ++i) {
                const uint32_t c = mem.u32(mem.u32(part + 0x24) + 4 * uint32_t(i));
                const float off[3] = {mem.f32(c + 4), mem.f32(c + 8), -mem.f32(c + 0xC)};
                const bool own = ((mem.at<uint8_t>(c + 0xD4) >> 5) & 1) || !split;
                float j[12], q[12];
                angles(own ? fd : m2, mem.at<int32_t>(c + 0x38), false, a);
                apJoint(off, a, j);
                apMul(p, j, q);
                for (const DrawModel& d : drawList)
                    if (d.body == body && d.part == c && !d.shadow) {
                        float inv[12], b[12];
                        apInvRigid(q, inv);
                        apMul(d.m, inv, b);
                        if (!have)
                            std::memcpy(ref, b, sizeof ref), have = true;
                        for (int k = 0; k < 12; ++k)
                            if (std::fabs(b[k] - ref[k]) > worst)
                                worst = std::fabs(b[k] - ref[k]), worstJoint = mem.at<int32_t>(c + 0x38);
                        break;
                    }
                chk(c, q, depth + 1);
            }
        };
        chk(mem.u32(body + 0xC8), rootOld, 1);
        char buf[200];
        std::snprintf(buf, sizeof buf, "actor pose: body type %d motion 0x%X -> 0x%X frame %d: %zu parts, rebuild off by %.4f (joint %d)",
                      from, motion, target, f, out.size(), worst, worstJoint);
        log(buf);
    }
    return out;
}

// body (type from) drawn as actor `to`: its draws of [0, nDraw) / skins of [0, nSkin) as that actor's, to dOut / sOut:
// the part models (coop4_part_map; `map` not empty), the pose (actor_pose), Rogan's arcade coat with Rogan only (the
// hook's second model on the chest, 0xB5.. / 0x119..: his own, not on a body he does not play; drawn on any body he
// plays, as body_part_draw_hook picks it by that body's motion and frame; not with the PC models, whose Rogan parts have
// the coat), Sophie's strip (sophie_skin_draw: the corners on the right thigh +0x2A4, drawn with the left one +0x274) with
// Sophie only
void Runtime::actor_dress(uint32_t body, int from, int to, const std::map<uint32_t, uint32_t>& map, size_t nDraw, size_t nSkin,
                          std::vector<DrawModel>& dOut, std::vector<SkinDraw>& sOut)
{
    const std::map<uint32_t, std::array<float, 12>>& pose = actor_pose(body, from, to);
    auto posed = [&](uint32_t part, const float* m, float* o) {
        if (auto it = pose.find(part); it != pose.end())
            apMul(m, it->second.data(), o);
        else if (o != m)
            std::memcpy(o, m, 12 * sizeof(float));
    };
    auto cls = [&](const DrawModel& d) {
        return d.joint >= 0 && d.joint < 31 ? int(mem.at<int8_t>(kPartRoles + uint32_t(from) * 0xF8 + uint32_t(d.joint) * 8 + 3)) : -1;
    };
    const size_t firstOut = dOut.size();      // (this body's draws in dOut start here)
    size_t thigh[2] = {SIZE_MAX, SIZE_MAX};   // dOut's class 10 / 11 part draws (Sophie's strip: their new matrices)
    size_t trunk[2] = {SIZE_MAX, SIZE_MAX};   // dOut's class 8 / 9 part draws (pmfix: the actor's belly on Rogan's body)
    size_t ownKept = 0;                       // (the body's own part draws drawn as the actor's: HOTD_ACTOR_CHECK)
    uint32_t partOf[2] = {0, 0};              // the class 8 / 9 parts (a belly skin's anchors)
    for (size_t i = 0; i < nDraw && i < drawList.size(); ++i) {
        const DrawModel& d = drawList[i];
        if (d.body != body)
            continue;
        if (from == 2 && to != 2 && isRoganCoat(d, mem.u32(d.part)))   // Rogan's coat: his own
            continue;
        DrawModel c = d;
        c.model = coop4_copy_model(map, from, to, d);
        if (c.model == 0)
            continue;   // (a part the actor has none of: Dr. Curien's toes "space"; model 0 is not a body part)
        posed(d.part, d.m, c.m);
        const int k = cls(d);
        if (float fix[12]; actorPartFix(from, to, k, fix))   // (Dr. Curien's head / cane hand)
            apMul(c.m, fix, c.m);
        dOut.push_back(c);
        if (const uint32_t gunModel = actor_gun_model(from, to, d); gunModel != 0) {   // (rellook; a class 6 draw: no index below)
            DrawModel g = c;
            g.model = gunModel;
            dOut.push_back(g);
        }
        ownKept += !d.shadow && d.model == mem.u32(d.part) ? 1 : 0;
        if (!d.shadow && k == 8)
            partOf[0] = d.part;
        if (!d.shadow && k == 9)
            partOf[1] = d.part;
        if (!d.shadow && (k == 10 || k == 11) && d.model == mem.u32(d.part))
            thigh[k - 10] = dOut.size() - 1;
        if (!d.shadow && (k == 8 || k == 9) && d.model == mem.u32(d.part))
            trunk[k - 8] = dOut.size() - 1;
        if (to == 2 && from != 2 && !pcModels && !d.shadow && k == 8 && d.model == mem.u32(d.part)) {
            // Rogan's coat on the body he plays (body_part_draw_hook's rule, from that body's motion and frame)
            const int32_t motion = mem.at<int16_t>(body + 0xD0), f = mem.at<int16_t>(body + 0xF4);
            if (motion != 0x168) {
                const int32_t ch = mem.at<int8_t>(G::chapter);
                const int32_t sub = int32_t(int16_t(mem.at<int16_t>(body + 0xF6) % 2));
                DrawModel coat = c;
                coat.model = uint32_t(ch == 5 || ch == 6 ? int32_t(mem.u32(G::frame_counter) / 2 % 0x32) + 0x119
                                                         : (sub * 0x32 + f / 2) % 100 + 0xB5);
                dOut.push_back(coat);
            }
        }
    }
    // galact (user 2026-10-28: "Curien does not look right, especially with his cane attached"): Dr. Curien keeps his
    // cane arm as in his own scenes (MO_kyu_b, his idle, frame 0: the cane upright in front of him, the Gallery's first
    // look): the cane hand (class 7), its forearm and upper arm re-built from the drawn chest with his idle's angles,
    // whatever the role's motion (only the hand turned, actorPartFix, left the players' hanging / swinging arm holding
    // the cane out sideways or slanting back). From the drawn chest, so mirrored motions (most of G's) too
    if ((to == 60 || to == 78) && from != 60 && from != 78 && firstOut < dOut.size() && nDraw <= drawList.size())
        if (CaneArm cane; actor_cane_arm(body, from, drawList.data(), nDraw, cane))
            actor_cane_apply(cane, dOut.data() + firstOut, dOut.size() - firstOut);
    const bool strip = thigh[0] != SIZE_MAX && thigh[1] != SIZE_MAX;
    bool belly = false;   // (the body drew a belly skin of its own)
    const size_t firstSkin = sOut.size();
    for (size_t i = 0; i < nSkin && i < skinDraws.size(); ++i) {
        const SkinDraw& s = skinDraws[i];
        if (s.body != body)
            continue;
        if (s.type == kSophieStrip)   // Sophie's own (not on the actor: from == 34, to != 34)
            continue;
        SkinDraw c = s;
        c.type = to;
        posed(partOf[0], s.a, c.a);
        posed(partOf[1], s.b, c.b);
        sOut.push_back(c);
        belly = true;
    }
    // galfix4: the actor's own hip joints (the belly / strip below are built from the moved parts)
    actor_fit((uint64_t(uint32_t(from)) << 32) | uint32_t(to), dOut, firstOut, sOut, firstSkin);
    if (!belly && trunk[0] != SIZE_MAX && trunk[1] != SIZE_MAX && actor_needs_belly(from, to)) {
        // pmfix: the actor's own belly on a body without one (Rogan's): from the drawn chest / hips
        SkinDraw c{to, {}, {}};
        std::memcpy(c.a, dOut[trunk[0]].m, sizeof c.a);
        std::memcpy(c.b, dOut[trunk[1]].m, sizeof c.b);
        c.body = body;
        sOut.push_back(c);
    }
    if (to == 34 && from != 34 && strip) {   // Sophie's strip on the body she plays
        SkinDraw c{kSophieStrip, {}, {}};
        std::memcpy(c.a, dOut[thigh[0]].m, sizeof c.a);
        std::memcpy(c.b, dOut[thigh[1]].m, sizeof c.b);
        c.body = body;
        sOut.push_back(c);
    }
    // pmfix render check (HOTD_ACTOR_CHECK=1, with --scene-view / PC Mode runs): every body part of the body is drawn
    // as the actor's (only Rogan's coat, a second draw on his chest, is left out) and an actor with a belly skin gets
    // one (arcade models); "actor check" lines in the log, FAILED ones at once
    static const bool check = std::getenv("HOTD_ACTOR_CHECK") != nullptr;
    if (check && log) {
        std::string why;
        size_t want = 0;   // the body's own part draws the actor has a model for (ownKept: drawn as the actor's)
        for (size_t i = 0; i < nDraw && i < drawList.size(); ++i) {
            const DrawModel& d = drawList[i];
            if (d.body == body && !d.shadow && d.model == mem.u32(d.part) && coop4_copy_model(map, from, to, d) != 0)
                ++want;
        }
        const size_t got = ownKept;
        if (got < want)
            why += " " + std::to_string(want - got) + " of " + std::to_string(want) + " body parts not drawn;";
        bool hasBelly = false;
        for (const SkinDraw& s : sOut)
            hasBelly = hasBelly || s.type == to;
        if (!pcModels && trunk[0] != SIZE_MAX && trunk[1] != SIZE_MAX && mem.at<int16_t>(kSkinNumbers + 2 * uint32_t(to)) != -1 &&
            !hasBelly)
            why += " no belly skin;";
        if (to == 34 && from != 34 && !pcModels && strip) {
            bool hasStrip = false;
            for (const SkinDraw& s : sOut)
                hasStrip = hasStrip || s.type == kSophieStrip;
            if (!hasStrip)
                why += " no strip;";
        }
        ++(why.empty() ? actorCheckOk_ : actorCheckFail_);
        if (!why.empty() && actorCheckFail_ <= 20)
            log("actor check FAILED: frame " + std::to_string(frameCount) + " type " + std::to_string(from) + " as " + std::to_string(to) + ":" + why);
        if ((actorCheckOk_ + actorCheckFail_) % 500 == 0)
            log("actor check: " + std::to_string(actorCheckOk_) + " ok, " + std::to_string(actorCheckFail_) + " failed");
    }
}

// galfix4 (user 2026-10-30, 4-19 with Sophie as Rogan: "there is a gap in her skirt"): the actor's legs hang from the
// actor's own hip joints. Sophie's thigh joints sit 2.53 down / 0.50 out on her hips, Rogan's 3.63 / 0.73: on his
// body her skirt halves (the thighs) hung 1.1 below her hip piece, a slit across the skirt. Each leg (the joints
// coop4_part_map found placed elsewhere and the parts under them) is moved by its offset difference turned by the drawn
// hips, the rest of the body by the legs' mean the other way: the hips meet the thighs and the feet stay on the floor
// (the actor stands at their own height). Shadows are left as drawn (projected on the floor)
void Runtime::actor_fit(uint64_t key, std::vector<DrawModel>& d, size_t first, std::vector<SkinDraw>& s, size_t firstSkin)
{
    const auto it = actorFits_.find(key);
    if (it == actorFits_.end() || it->second.legs.empty() || first >= d.size())
        return;
    const ActorFit& fit = it->second;
    const DrawModel* hips = nullptr;
    for (size_t i = first; i < d.size() && !hips; ++i)
        if (!d[i].shadow && d[i].joint == fit.parent)
            hips = &d[i];
    if (!hips)
        return;
    std::vector<std::array<float, 3>> move(fit.legs.size());
    float mean[3] = {0, 0, 0};
    for (size_t l = 0; l < fit.legs.size(); ++l) {
        const float* o = fit.legs[l].d;
        const float lx = o[0], ly = o[1], lz = -o[2];   // (apJoint's T(x, y, -z))
        for (int k = 0; k < 3; ++k) {
            move[l][size_t(k)] = hips->m[k] * lx + hips->m[3 + k] * ly + hips->m[6 + k] * lz;
            mean[k] += move[l][size_t(k)] / float(fit.legs.size());
        }
    }
    for (size_t i = first; i < d.size(); ++i) {
        if (d[i].shadow)
            continue;
        const float* by = mean;
        float leg[3];
        for (size_t l = 0; l < fit.legs.size(); ++l)
            if (std::find(fit.legs[l].sub.begin(), fit.legs[l].sub.end(), int(d[i].joint)) != fit.legs[l].sub.end()) {
                for (int k = 0; k < 3; ++k)
                    leg[k] = mean[k] - move[l][size_t(k)];
                by = leg;
                break;
            }
        for (int k = 0; k < 3; ++k)
            d[i].m[9 + k] -= by[k];
    }
    for (size_t i = firstSkin; i < s.size(); ++i)   // (the body's own skins: on its chest / hips)
        for (int k = 0; k < 3; ++k)
            s[i].a[9 + k] -= mean[k], s[i].b[9 + k] -= mean[k];
}

bool Runtime::actor_needs_belly(int from, int to) const
{
    if (pcModels || from == to || from < 0 || to < 0 || from >= 100 || to >= 100)
        return false;
    return mem.at<int16_t>(kSkinNumbers + 2 * uint32_t(from)) == -1 && mem.at<int16_t>(kSkinNumbers + 2 * uint32_t(to)) != -1;
}

bool Runtime::actor_cane_arm(uint32_t body, int from, const DrawModel* d, size_t n, CaneArm& c)
{
    auto cls = [&](const DrawModel& q) {
        return q.joint >= 0 && q.joint < 31 ? int(mem.at<int8_t>(kPartRoles + uint32_t(from) * 0xF8 + uint32_t(q.joint) * 8 + 3)) : -1;
    };
    uint32_t hand = 0;
    for (size_t i = 0; i < n && !hand; ++i)
        if (d[i].body == body && !d[i].shadow && cls(d[i]) == 7)
            hand = d[i].part;
    std::map<uint32_t, uint32_t> parent;
    std::function<void(uint32_t, int)> tree = [&](uint32_t part, int depth) {
        const int32_t k = mem.at<int32_t>(part + 0x34);
        if (depth > 16 || k < 0 || k > 32)
            return;
        for (int32_t i = 0; i < k; ++i)
            if (const uint32_t ch = mem.u32(mem.u32(part + 0x24) + 4 * uint32_t(i)); ch != 0) {
                parent[ch] = part;
                tree(ch, depth + 1);
            }
    };
    if (const uint32_t root = mem.u32(body + 0xC8); root != 0 && hand != 0)
        tree(root, 1);
    c.hand = hand;
    c.fore = parent.count(hand) ? parent[hand] : 0;
    c.upper = parent.count(c.fore) ? parent[c.fore] : 0;
    c.chest = parent.count(c.upper) ? parent[c.upper] : 0;
    const uint32_t rec = motion_frame_record(body, 289, 0);   // MO_kyu_b (20 joints, as the players' skeleton)
    if (c.chest == 0 || rec == 0)
        return false;
    const uint32_t parts[3] = {c.upper, c.fore, c.hand};
    for (int k = 0; k < 3; ++k) {
        const uint32_t p = parts[k];
        const float off[3] = {mem.f32(p + 4), mem.f32(p + 8), -mem.f32(p + 0xC)};
        const uint32_t at = uint32_t(mem.at<int32_t>(p + 0x38)) * 6 + 0xC;
        const uint16_t a[3] = {mem.at<uint16_t>(rec + at), mem.at<uint16_t>(rec + at + 2), mem.at<uint16_t>(rec + at + 4)};
        apJoint(off, a, c.j[k]);
    }
    return true;
}

void Runtime::actor_cane_apply(const CaneArm& c, DrawModel* d, size_t n)
{
    float chest[12];
    bool have = false;
    for (size_t i = 0; i < n && !have; ++i)
        if (d[i].part == c.chest && !d[i].shadow)
            std::memcpy(chest, d[i].m, sizeof chest), have = true;
    if (!have)
        return;
    float m[3][12];
    apMul(chest, c.j[0], m[0]);
    apMul(m[0], c.j[1], m[1]);
    apMul(m[1], c.j[2], m[2]);
    for (size_t i = 0; i < n; ++i)
        if (!d[i].shadow && (d[i].part == c.upper || d[i].part == c.fore || d[i].part == c.hand))
            std::memcpy(d[i].m, m[d[i].part == c.upper ? 0 : d[i].part == c.fore ? 1 : 2], sizeof chest);
}

void Runtime::actor_swap(size_t nDraw, size_t nSkin)
{
    if (actorSwap[0] < 0 && actorSwap[1] < 0)
        return;
    nDraw = std::min(nDraw, drawList.size());
    nSkin = std::min(nSkin, skinDraws.size());
    // the players' bodies; the ending's are its own tasks (ending_p1_b_frame 0x59120, ending_p2_b_frame 0x59B10, as
    // coop4_scene_variant finds them; charsel: the one-player ending's too, ending_p1_a_frame 0x58CC0 /
    // ending_p2_a_frame 0x596F0: Dr. Curien alone in the ending was drawn as the Researcher he plays)
    uint32_t bodies[2] = {mem.at<port::a::Player>(G::players, 0).body.a, mem.at<port::a::Player>(G::players, 1).body.a};
    for (size_t i = 0; i < nDraw; ++i)
        if (const uint32_t b = drawList[i].body; b != 0)
            if (const int s = endingBodySeat(mem.u32(b)); s >= 0)
                bodies[s] = b;
    std::vector<DrawModel> dOut;
    std::vector<SkinDraw> sOut;
    uint32_t swapped[2] = {0, 0};
    for (int p = 0; p < 2; ++p) {
        const int to = actorSwap[p];
        const uint32_t body = bodies[p];
        if (to < 0 || body == 0 || (p == 1 && body == bodies[0]))
            continue;
        const int from = mem.at<int16_t>(body + 0xCC);
        if (from == to)
            continue;
        const std::map<uint32_t, uint32_t>& map = coop4_part_map(from, to);
        if (map.empty())
            continue;
        actor_dress(body, from, to, map, nDraw, nSkin, dOut, sOut);
        swapped[p] = body;
    }
    if (!swapped[0] && !swapped[1])
        return;
    auto mine = [&](uint32_t b) { return b != 0 && (b == swapped[0] || b == swapped[1]); };
    size_t w = 0;
    for (size_t i = 0; i < drawList.size(); ++i)
        if (i >= nDraw || !mine(drawList[i].body))
            drawList[w++] = drawList[i];
    drawList.resize(w);
    w = 0;
    for (size_t i = 0; i < skinDraws.size(); ++i)
        if (i >= nSkin || !mine(skinDraws[i].body))
            skinDraws[w++] = skinDraws[i];
    skinDraws.resize(w);
    drawList.insert(drawList.end(), dOut.begin(), dOut.end());
    skinDraws.insert(skinDraws.end(), sOut.begin(), sOut.end());
}

void Runtime::actor_reswap()
{
    if (!actorView || !actorPreValid_)
        return;
    drawList = actorPreDraw_;
    skinDraws = actorPreSkin_;
    actor_swap(actorOwn_[0], actorOwn_[1]);
}

// pmextra: the PC Mode seats' extra characters as actors (draws only; the viewer's own picks win while it is open)
void Runtime::actor_pm_sync()
{
    if (actorView)
        return;
    int want[4] = {-1, -1, -1, -1};
    bool any = false;
    for (int p = 0; p < 4 && chars_on(); ++p)
        if (const PmExtra* x = pm_extra(pm_char[p]))
            want[p] = x->type, any = true;
    if (!any && !actorPm)
        return;
    if (std::equal(want, want + 4, actorSwap) && any == actorPm) {
        // (Dr. Curien / the zombie Sophie: their part models are known once their type is loaded, which a chapter's
        // start or the ending may do after the pick: the texture areas looked at again twice a second)
        if (any && frameCount % 30 == 0)
            actor_areas_update();
        return;
    }
    std::copy(want, want + 4, actorSwap);
    actorPm = any;
    actor_areas_update();
}

// the picked actors' texture areas (runtime.h actorAreas): the part models of their type (its part tree, as
// coop4_part_map walks it)
void Runtime::actor_areas_update()
{
    actorAreas.clear();
    if (!actorView && !actorPm)
        return;
    // (gallery.cpp galleryLook: where the game draws them. galfix3: the zombie Sophie in area 2 with row 13:92, as the
    // ending's event 6 draws her (area 2: kind111_texture_area; Chapter 4's stage has area 2 too, arcade_loader.cpp))
    static const int kAreas[][2] = {{60, 11}, {78, 12}, {67, 2}};
    for (int r = 0; r < 4; ++r)
        for (const auto& ta : kAreas) {
            if (actorSwap[r] != ta[0])
                continue;
            std::function<void(uint32_t, int)> walk = [&](uint32_t part, int depth) {
                if (part == 0 || depth > 16)
                    return;
                if (const uint32_t m = mem.u32(part); m != 0)
                    actorAreas[m] = ta[1];
                const int32_t n = mem.at<int32_t>(part + 0x18);
                for (int32_t i = 0; i >= 0 && i < n && n <= 32; ++i)
                    walk(mem.u32(part + 0x1C + 4 * uint32_t(i)), depth + 1);
            };
            const uint32_t h = mem.u32(G::creature_type_headers + 4 * uint32_t(ta[0]));
            const int32_t n = h ? mem.at<int32_t>(h + 0x18) : 0;
            for (int32_t i = 0; i >= 0 && i < n && n <= 32; ++i)
                walk(mem.u32(h + 0x1C + 4 * uint32_t(i)), 1);
        }
}

// charsel: with the PC models the ending's talking heads (Rogan's 0x748 .. 0x757, G's 0x282 .. 0x291: ending.cpp
// ending_body_hand) have no model of their own in the 1998 PC files, so the talking body was drawn without a head
// (seen 2026-11-06 in the 2-player ending, "I guess everything's over", also with the default characters): that
// body's own closed head (0x2B / 0x281) in their place. Draws only (the part's model in the address space stays the
// arcade's: both model sets run alike, online too)
void Runtime::ending_head_fallback()
{
    if (!pcModels || !modelAvailable)
        return;
    for (DrawModel& d : drawList) {
        if (d.body == 0 || endingBodySeat(mem.u32(d.body)) < 0)
            continue;
        const uint32_t closed = d.model >= 0x748 && d.model <= 0x757 ? 0x2Bu : d.model >= 0x282 && d.model <= 0x291 ? 0x281u : 0;
        if (closed != 0 && !modelAvailable(d.model) && modelAvailable(closed))
            d.model = closed;
    }
}

// charsel: the part-ownership check (runtime.h part_owner_check; HOTD_PART_CHECK=1, after actor_swap)
void Runtime::part_owner_check()
{
    static const bool on = std::getenv("HOTD_PART_CHECK") != nullptr;
    if (!on || !log)
        return;
    auto owned = [&](int type) -> const std::set<uint32_t>& {
        auto it = partOwned_.find(type);
        if (it != partOwned_.end() && !it->second.empty())
            return it->second;
        std::set<uint32_t>& s = partOwned_[type];
        std::function<void(uint32_t, int)> walk = [&](uint32_t part, int depth) {
            if (part == 0 || depth > 16)
                return;
            if (const uint32_t m = mem.u32(part); m != 0)
                s.insert(m);
            const int32_t n = mem.at<int32_t>(part + 0x18);
            for (int32_t i = 0; i >= 0 && i < n && n <= 32; ++i)
                walk(mem.u32(part + 0x1C + 4 * uint32_t(i)), depth + 1);
        };
        const uint32_t h = type >= 0 && type < 100 ? mem.u32(G::creature_type_headers + 4 * uint32_t(type)) : 0;
        const int32_t n = h ? mem.at<int32_t>(h + 0x18) : 0;
        for (int32_t i = 0; i >= 0 && i < n && n <= 32; ++i)
            walk(mem.u32(h + 0x1C + 4 * uint32_t(i)), 1);
        auto range = [&](uint32_t a, uint32_t b) { for (uint32_t m = a; m <= b; ++m) s.insert(m); };
        if (type == 2 || (type >= 13 && type <= 15)) {
            // the players' gun hands, shared by the four player types (body_part_draw_hook: the empty hand, the
            // reload, the torch / gun poses, the opening's gun frames f + 0x4A ..; the gun alone)
            for (uint32_t m : {0x716u, 0x36Bu, 0x2B3u, 0x71Bu, kPlayerGunHand, kGunOnlyKey})
                s.insert(m);
            range(0x4A, 0x54);
        }
        if (type == 2) {
            range(0xB5, 0x119 + 0x31);   // his arcade coat
            s.insert(0x2B), range(0x748, 0x757);   // the ending's talking head (ending_p1_*_frame)
        }
        if (type == 13)
            range(0x281, 0x291);   // the ending's talking head (ending_p2_*_frame)
        if (type == 60)
            range(0x1B52, 0x1B60);   // body_part_draw_hook's talking heads
        if (type == 78)
            range(0x1CEF, 0x1CFD);
        if (type == 34)
            range(0x60E, 0x61F), s.insert(0x631);   // Sophie's heads (looking, talking, eyes closed) and hand (body_hook_special)
        return s;
    };
    uint32_t bodies[2] = {mem.at<port::a::Player>(G::players, 0).body.a, mem.at<port::a::Player>(G::players, 1).body.a};
    for (const DrawModel& d : drawList)
        if (d.body != 0)
            if (const int s = endingBodySeat(mem.u32(d.body)); s >= 0)
                bodies[s] = d.body;
    int drawnAs[2] = {-1, -1};
    for (int p = 0; p < 2; ++p) {
        if (bodies[p] == 0)
            continue;
        const int from = mem.at<int16_t>(bodies[p] + 0xCC);
        const int to = actorSwap[p];
        drawnAs[p] = to >= 0 && to != from && !coop4_part_map(from, to).empty() ? to : from;
    }
    const uint32_t path = mem.u32(G::camera_path);
    auto flush = [&] {
        if (partFrames_ != 0 && (partDrawn_[0] || partDrawn_[1]))
            log("part check: path " + std::to_string(partPath_) + " frames " + std::to_string(partFrom_) + "-" +
                std::to_string(partFrom_ + partFrames_ - 1) + ": P1 type " + std::to_string(partType_[0]) + " " +
                std::to_string(partDrawn_[0]) + " parts " + std::to_string(partForeign_[0]) + " foreign, P2 type " +
                std::to_string(partType_[1]) + " " + std::to_string(partDrawn_[1]) + " parts " +
                std::to_string(partForeign_[1]) + " foreign (total foreign " + std::to_string(partTotalForeign_) + ", missing " + std::to_string(partMissing_) + ")");
        partPath_ = path, partFrom_ = frameCount, partFrames_ = 0;
        partDrawn_[0] = partDrawn_[1] = partForeign_[0] = partForeign_[1] = 0;
    };
    if (path != partPath_ || drawnAs[0] != partType_[0] || drawnAs[1] != partType_[1] || partFrames_ >= 600)
        flush();
    partType_[0] = drawnAs[0], partType_[1] = drawnAs[1];
    ++partFrames_;
    for (const DrawModel& d : drawList) {
        if (d.body == 0 || d.shadow)
            continue;
        const int p = d.body == bodies[0] ? 0 : d.body == bodies[1] ? 1 : -1;
        if (p < 0 || drawnAs[p] < 0)
            continue;
        ++partDrawn_[p];
        if (pcModels && modelAvailable && d.model != 0 && !modelAvailable(d.model) && ++partMissing_ <= 20)   // (no model to draw: a part missing)
            log("part check MISSING: frame " + std::to_string(frameCount) + " P" + std::to_string(p + 1) + " model " + std::to_string(d.model));
        if (d.model == 0 || owned(drawnAs[p]).count(d.model))   // (model 0: nothing drawn)
            continue;
        ++partForeign_[p];
        if (++partTotalForeign_ <= 40)
            log("part check FOREIGN: frame " + std::to_string(frameCount) + " P" + std::to_string(p + 1) + " drawn as type " +
                std::to_string(drawnAs[p]) + " (body type " + std::to_string(mem.at<int16_t>(bodies[p] + 0xCC)) +
                ") model 0x" + [&] { char b[16]; std::snprintf(b, sizeof b, "%X", d.model); return std::string(b); }() +
                " joint " + std::to_string(d.joint));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// carchars: the opening car's seats (runtime.h). player_char_props_draw notes each seat's car-model draw
// (car_seat_mark); after the frame's logic, a seat whose character has no car model of its own gets the character's
// body instead: its part tree (creature_type_headers) with its own part models, posed seated (carPose: joint angles
// over a frame of the players' motions), placed in that draw's matrix (the seat's place in the car): the car body
// hides the legs as it hides the car models' (no part shows outside it in the opening's shots). Draws only.
// Testing (fitting): HOTD_CAR_POSE / _FIT<seat> / _J<seat> (carPose), HOTD_CAR_OVERLAY=<type>[,<seat 1's type>] (built
// over the car models too; with HOTD_CAR_BUILTONLY in their place), HOTD_CAR_NOBODY (the car body left out),
// HOTD_CAR_CANE, HOTD_CAR_TREE / HOTD_CAR_TRACE (log lines)

void Runtime::car_seat_mark(int seat)
{
    carSeatDraw_[seat & 1] = drawList.size();
    carSeatFrame_[seat & 1] = frameCount;
}

int Runtime::car_seat_type(int seat)
{
    const int own = seat == 0 ? 2 : 13;   // the car models' own: Rogan driving, G beside him (arcade and PC)
    if (const char* e = std::getenv("HOTD_CAR_OVERLAY"))   // testing (fitting): that type over the car model
    {
        int t[2] = {0, 0};
        std::sscanf(e, "%d,%d", &t[0], &t[1]);
        return t[seat] > 0 ? t[seat] : t[0] > 0 ? t[0] : own;
    }
    if (actorView) {   // the cut-scene viewer's actors (players 1 / 2's roles)
        const int t = actorSwap[seat];
        return t < 0 || t == own ? -1 : t;
    }
    if (!chars_on())
        return -1;
    const int c = pm_seat_char(seat);
    if (c <= 0)
        return -1;
    if (const PmExtra* x = pm_extra(c))
        return x->type;
    if (pcModels && pm_model_drawable(uint32_t(pm_table(c).model)))
        return -1;   // the PC's own car model of the character (as THOTD.EXE draws it)
    const int type = pm_table(c).type;
    return type == own ? -1 : type;
}

namespace {
// the seated pose: MO_p_temoti (0x177) frame 0, the players' standing pose, with per role (the players' 20-joint
// tree) these turns added to the frame's joint angles (a0, a1, a2 as a frame record holds them, 0x10000 a turn): the
// thighs (roles 4 / 8) a quarter turn forward, the knees (5 / 9) a quarter turn back; the arms (upper 14 / 17, fore
// 15 / 18; the two sides turn opposite ways): seat 0 (driving) the upper arms forward, elbows bent, the hands on the
// wheel at the car model's hands; seat 1 the upper arms down, the forearms forward, the hands in the lap. The fit:
// the root's place in the car model's space (x forward, y up, z across), its turn about the up axis, a scale (the car
// models' figures are smaller than the bodies) and a lean back (degrees; the passenger a little more). Fitted with
// G's body drawn over the arcade's Rogan car model and Rogan's over G's (HOTD_CAR_OVERLAY=13,2 HOTD_CAR_NOBODY=1):
// heads at the car models' head height, hands at the driver's hands
struct CarJoint { int role; uint16_t a[3]; };
struct CarPose { int motion, frame; float fit[6]; std::vector<CarJoint> joints; };
CarPose carPoseDefault(int seat)
{
    CarPose p{0x177, 0, {-3, -8.5f, 1.2f, 180, 0.9f, 8}, {{4, {0, 0, 0x4000}}, {8, {0, 0, 0x4000}}, {5, {0, 0, 0xC000}}, {9, {0, 0, 0xC000}}}};
    if (seat == 0) {
        p.joints.insert(p.joints.end(), {{14, {0, 0, 0xD400}}, {15, {0, 0, 0xD000}}, {17, {0, 0, 0x2C00}}, {18, {0, 0, 0x3000}}});
    } else {
        p.fit[1] = -8.8f, p.fit[5] = 12;
        p.joints.insert(p.joints.end(), {{14, {0, 0, 0xF800}}, {15, {0, 0, 0xC800}}, {17, {0, 0, 0x0800}}, {18, {0, 0, 0x3800}}});
    }
    return p;
}
// testing (tuning): HOTD_CAR_POSE=motion,frame  HOTD_CAR_FIT<seat>=x,y,z,yaw,scale,lean  HOTD_CAR_J<seat>=role:a0,a1,a2/... (hex)
CarPose carPose(int seat)
{
    CarPose p = carPoseDefault(seat);
    if (const char* e = std::getenv("HOTD_CAR_POSE"))
        std::sscanf(e, "%i,%i", &p.motion, &p.frame);
    const std::string fs = "HOTD_CAR_FIT" + std::to_string(seat), js = "HOTD_CAR_J" + std::to_string(seat);
    if (const char* e = std::getenv(fs.c_str()))
        std::sscanf(e, "%f,%f,%f,%f,%f,%f", &p.fit[0], &p.fit[1], &p.fit[2], &p.fit[3], &p.fit[4], &p.fit[5]);
    if (const char* e = std::getenv(js.c_str())) {
        const std::string s = e;
        size_t at = 0;
        while (at < s.size()) {
            size_t end = s.find('/', at);
            if (end == std::string::npos)
                end = s.size();
            int r = 0;
            unsigned a0 = 0, a1 = 0, a2 = 0;
            if (std::sscanf(s.substr(at, end - at).c_str(), "%d:%x,%x,%x", &r, &a0, &a1, &a2) == 4) {
                p.joints.erase(std::remove_if(p.joints.begin(), p.joints.end(), [&](const CarJoint& j) { return j.role == r; }),
                               p.joints.end());
                p.joints.push_back({r, {uint16_t(a0), uint16_t(a1), uint16_t(a2)}});
            }
            at = end + 1;
        }
    }
    return p;
}
} // namespace

void Runtime::car_seats_dress()
{
    for (int s = 1; s >= 0; --s) {   // (seat 1's draw comes after seat 0's: replaced first, seat 0's index stays)
        if (carSeatFrame_[s] != frameCount || carSeatDraw_[s] >= drawList.size())
            continue;
        const int type = car_seat_type(s);
        if (type < 0 || type >= 100)
            continue;
        const uint32_t header = mem.u32(G::creature_type_headers + 4 * uint32_t(type));
        const int32_t n = header ? mem.at<int32_t>(header + 0x18) : 0;
        if (n <= 0 || n > 32 || mem.u32(kJoints + 4 * uint32_t(type)) != 20)
            continue;   // (a body without the players' 20 joints: the car model stays)
        const CarPose pose = carPose(s);
        constexpr uint32_t kWords = (20u * 48 + 0x7F) >> 5;   // a 20-joint frame record's words
        const uint32_t rec = mem.u32(kMotionData + 4 * uint32_t(pose.motion)) + uint32_t(pose.frame) * 4 * kWords;
        // (Dr. Curien's cane arm keeps the seat's pose: his idle's arm (actor_cane_arm) held the cane across the car;
        // only the cane hand is turned, the cane down along the arm, actorPartFix)
        const uint32_t cane = std::getenv("HOTD_CAR_CANE") ? mem.u32(kMotionData + 4 * 289u) : 0;   // testing: MO_kyu_b's arm
        const DrawModel car = drawList[carSeatDraw_[s]];
        auto cls = [&](int role) {
            return role >= 0 && role < 31 ? int(mem.at<int8_t>(kPartRoles + uint32_t(type) * 0xF8 + uint32_t(role) * 8 + 3)) : -1;
        };
        // (HOTD_CAR_CANE: Dr. Curien's cane arm, the cane hand (class 7), its forearm and upper arm, with his idle's
        // angles as actor_cane_arm gives it; his head is turned as on the players' motions, actorPartFix)
        std::set<int> caneRoles;
        auto angles = [&](int role, uint16_t a[3]) {
            const uint32_t src = caneRoles.count(role) ? cane : rec;
            for (uint32_t k = 0; k < 3; ++k)
                a[k] = mem.at<uint16_t>(src + uint32_t(role) * 6 + 0xC + 2 * k);
            if (src == rec)
                for (const CarJoint& j : pose.joints)
                    if (j.role == role)
                        for (int k = 0; k < 3; ++k)
                            a[k] = uint16_t(a[k] + j.a[k]);
        };
        std::vector<DrawModel> parts;
        int at[12];   // parts' index by class (8 chest, 9 hips, 10 / 11 thighs), -1 none
        std::fill(at, at + 12, -1);
        std::vector<int> path;
        int pass = 1;
        std::function<void(uint32_t, const float*, int)> walk = [&](uint32_t data, const float* parent, int depth) {
            const float off[3] = {mem.f32(data + 4), mem.f32(data + 8), -mem.f32(data + 0xC)};
            const int role = mem.at<int32_t>(data + 0x14), k = cls(role);
            if (cane && k == 7) {
                caneRoles.insert(role);
                for (size_t i = path.size() >= 2 ? path.size() - 2 : 0; i < path.size(); ++i)
                    caneRoles.insert(path[i]);
            }
            uint16_t a[3];
            angles(role, a);
            float j[12], q[12];
            apJoint(off, a, j);
            apMul(parent, j, q);
            static const bool tree = std::getenv("HOTD_CAR_TREE") != nullptr;
            if (tree && log && pass == 1 && frameCount % 60 == 0) {
                char buf[200];
                std::snprintf(buf, sizeof buf, "car tree: type %d role %d parent %d class %d model 0x%X off %.2f %.2f %.2f angles %04X %04X %04X",
                              type, role, path.empty() ? 0 : path.back(), k, mem.u32(data), off[0], off[1], off[2], a[0], a[1], a[2]);
                log(buf);
            }
            if (const uint32_t model = mem.u32(data); model != 0) {
                DrawModel d = car;
                d.model = model;
                if (k == 6 && (type == 2 || (type >= 13 && type <= 15)))   // the players' own hand holds the gun: an
                    d.model = 0x36B;                                          // empty one (the hook's, MO_p_kousya)
                apMul(car.m, q, d.m);
                if (float fix[12]; (k == 1 || (k == 7 && !cane)) && actorPartFix(2, type, k, fix))
                    apMul(d.m, fix, d.m);
                d.joint = int16_t(role);
                parts.push_back(d);
                if (k >= 0 && k < 12)
                    at[k] = int(parts.size()) - 1;
                if (k == 8 && type == 2 && !pcModels) {   // Rogan's arcade coat (its first frame; PC models: built in)
                    d.model = 0xB5;
                    parts.push_back(d);
                }
            }
            const int32_t c = mem.at<int32_t>(data + 0x18);
            path.push_back(role);
            for (int32_t i = 0; i < c && c <= 32 && depth < 16; ++i)
                walk(mem.u32(data + 0x1C + 4 * uint32_t(i)), q, depth + 1);
            path.pop_back();
        };
        // the root: the seat's fit (place, turn), then the frame's height and root angles
        float base[12];
        {
            float fit[12], lean[12], root[12];
            const uint16_t yaw[3] = {0, uint16_t(int(pose.fit[3] * 65536.0f / 360.0f)), 0};
            const uint16_t back[3] = {0, 0, uint16_t(int(pose.fit[5] * 65536.0f / 360.0f))};
            const float zero[3] = {0, 0, 0};
            apJoint(pose.fit, yaw, fit);
            apJoint(zero, back, lean);
            for (int k = 0; k < 9; ++k)
                lean[k] *= pose.fit[4];
            apMul(fit, lean, fit);
            uint16_t a[3];
            angles(0, a);
            const float d[3] = {0, mem.f32(rec + 4), 0};
            apJoint(d, a, root);
            apMul(fit, root, base);
        }
        for (pass = cane ? 0 : 1; pass < 2; ++pass) {   // (Curien: a first walk finds his cane arm's roles)
            parts.clear();
            std::fill(at, at + 12, -1);
            for (int32_t i = 0; i < n; ++i)
                walk(mem.u32(header + 0x1C + 4 * uint32_t(i)), base, 1);
        }
        if (std::getenv("HOTD_CAR_NOBODY"))   // testing: the car body left out (the legs seen)
            for (DrawModel& d : drawList)
                if (d.model == 0x94F || d.model == 0x951)
                    d.model = 0x7FFFFFFF;
        static const bool trace = std::getenv("HOTD_CAR_TRACE") != nullptr;
        if (trace && log && frameCount % 30 == 0) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "car seat %d: type %d, %zu parts in place of model 0x%X", s, type, parts.size(), car.model);
            log(buf);
        }
        static const bool overlay = std::getenv("HOTD_CAR_OVERLAY") != nullptr && !std::getenv("HOTD_CAR_BUILTONLY");   // testing: the car model kept too (fitting)
        if (!overlay)
            drawList.erase(drawList.begin() + std::ptrdiff_t(carSeatDraw_[s]));
        drawList.insert(drawList.begin() + std::ptrdiff_t(carSeatDraw_[s]), parts.begin(), parts.end());
        // the belly skin (arcade models, a type that has one) and Sophie's strip, as her body draws them
        if (!pcModels && at[8] >= 0 && at[9] >= 0 && mem.at<int16_t>(kSkinNumbers + 2 * uint32_t(type)) != -1) {
            SkinDraw c{type, {}, {}};
            std::memcpy(c.a, parts[size_t(at[8])].m, sizeof c.a);
            std::memcpy(c.b, parts[size_t(at[9])].m, sizeof c.b);
            skinDraws.push_back(c);
        }
        if (!pcModels && type == 34 && at[10] >= 0 && at[11] >= 0) {
            SkinDraw c{kSophieStrip, {}, {}};
            std::memcpy(c.a, parts[size_t(at[10])].m, sizeof c.a);
            std::memcpy(c.b, parts[size_t(at[11])].m, sizeof c.b);
            skinDraws.push_back(c);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The cut-scene variations of players 3 / 4 (docs/coop4.md section 16; user 2026-10-20: "make multiple possible
// variations ... so I can compare and pick"). Each scene has A = the look above (or the two-set rule) and B, C, D ...
// below. A variation is one line per seat, "player 3 | player 4", of comma-separated words:
//   src:1 / src:2   whose body is copied (default: player 3 player 1's, player 4 player 2's)
//   delay:N         the pose of N frames ago (a beat later; before the scene's frame N: its first pose)
//   hold:F          the pose of the scene's frame F, kept from then on (hold:0: still from the start)
//   out:X up:Y away:Z   the place, in camera space at the copied frame, from the copied body: X across the picture
//                   (outward on that body's side; negative: toward the middle), Y up, Z deeper
//   yaw:D           turned D degrees about the body's own up axis (180: facing back, guarding the rear)
//   from:F until:F  drawn only from / until the scene's frame F
//   off             not drawn
// The copied frames are kept in world space (the camera of their frame), so a held or delayed body stands at its
// own spot while the camera moves. Draws only: nothing of the game's state is read back or written.
namespace {
// a camera cut: the camera moved more than this in one frame (world units; a body is about 17 high) or its view axis
// turned more (the dot of the two frames' view axes below this)
constexpr float kScene4pCutMove = 6.0f, kScene4pCutTurn = 0.985f;
struct SeatSpec {
    bool on = false;
    int src = -1, delay = 0, hold = -1, from = 0, until = 1 << 30, cam = -1;
    float out = 0, up = 0, away = 0, yaw = 0;
    // "fix" (the F looks, user 2026-10-22): the place is set once per camera shot (from the shot's first camera, so a
    // delayed copy does not slide or jump inside a shot), kept on the copied body's floor (the offset has no part along
    // the body's up axis; `up` is then along that axis) and nudged apart from the other bodies (`gap`)
    bool fix = false;
    float gap = 7;
    std::vector<int> hide;   // hide:1+3: not drawn in camera shots 1 and 3 (the scene's shots from 0)
    // wx:X,wz:Z (sets fix): the place in world space instead: X / Z world units (the floor's x, z) from the copied
    // body, `up` up; no camera in it, so the copy keeps its spot and facing whatever the camera does (user 2026-10-22:
    // "P3 / P4 all rotate to face the camera when the camera rotates": cam:N took the place from the camera of each
    // frame before the scene's frame N, so the copies swung round with an orbiting camera)
    bool world = false;
    float wx = 0, wz = 0;
    // (cam:N, fix: the place from the camera of the scene's frame N instead of the shot's first; for a shot whose
    // camera swings a long way, e.g. 3-15's dive from above into the corridor)
    // mirror (user 2026-10-26 on 4-19: "three turn the same direction"): the copied pose mirrored left / right about the
    // copied body's own middle (the plane through its place, across its x axis), so the copy turns the other way; the
    // left / right parts swapped (each part follows its pair's mirrored motion), so the gun stays in the right hand
    bool mirror = false;
};
SeatSpec parseSeat(const std::string& s)
{
    SeatSpec r;
    size_t p = 0;
    while (p < s.size()) {
        size_t q = s.find(',', p);
        if (q == std::string::npos)
            q = s.size();
        std::string w = s.substr(p, q - p);
        while (!w.empty() && w.front() == ' ') w.erase(w.begin());
        while (!w.empty() && w.back() == ' ') w.pop_back();
        p = q + 1;
        if (w.empty())
            continue;
        r.on = true;
        if (w == "off") {
            r.on = false;
            return r;
        }
        if (w == "fix") {
            r.fix = true;
            continue;
        }
        if (w == "mirror") {
            r.mirror = true;
            continue;
        }
        const size_t c = w.find(':');
        if (c == std::string::npos)
            continue;
        if (w.compare(0, c, "hide") == 0) {
            for (const char* h = w.c_str() + c + 1; *h;) {
                char* e = nullptr;
                const long v = std::strtol(h, &e, 10);
                if (e == h)
                    break;
                r.hide.push_back(int(v));
                h = *e == '+' ? e + 1 : e;
            }
            continue;
        }
        const std::string k = w.substr(0, c);
        const float v = std::strtof(w.c_str() + c + 1, nullptr);
        if (k == "src") r.src = std::clamp(int(v), 1, 2) - 1;
        else if (k == "delay") r.delay = std::max(0, int(v));
        else if (k == "hold") r.hold = std::max(0, int(v));
        else if (k == "from") r.from = int(v);
        else if (k == "until") r.until = int(v);
        else if (k == "out") r.out = v;
        else if (k == "up") r.up = v;
        else if (k == "away") r.away = v;
        else if (k == "yaw") r.yaw = v;
        else if (k == "gap") r.gap = std::max(0.0f, v);
        else if (k == "cam") r.cam = std::max(0, int(v));
        else if (k == "wx") r.wx = v, r.world = r.fix = true;
        else if (k == "wz") r.wz = v, r.world = r.fix = true;
    }
    return r;
}

// the built-in variations ({scene, letter, "player 3 | player 4"}; A is always the look of kSceneFour)
struct VariantDef {
    int scene;
    char letter;
    const char* spec;
};
// (picked by eye in renders, 4 players arcade 16:9; main checkout private\scene4p\ shows them all)
#define SCENE4P_FOUR(s, o3, o4, a3, a4)                                                                                 \
    {s, 'B', "src:2,out:" #o3 ",away:" #a3 " | src:2,out:" #o4 ",away:" #a4},                                          \
    {s, 'C', "src:2,yaw:180,out:" #o3 ",away:" #a3 " | src:2,yaw:180,out:" #o4 ",away:" #a4},                          \
    {s, 'D', "delay:15,out:" #o3 ",away:" #a3 " | delay:30,out:" #o4 ",away:" #a4}
#define SCENE4P_TWO(s)                                                                                                 \
    {s, 'B', "out:6,away:6 | out:6,away:6"},                                                                           \
    {s, 'C', "src:2,hold:0,out:8,away:8 | src:2,hold:0,out:4,away:14"},                                               \
    {s, 'D', "delay:20,out:6,away:6 | delay:40,out:6,away:6"}
constexpr VariantDef kVariants[] = {
    // 00 opening run: B a beat later, C they stay at the car
    {0, 'B', "delay:20,out:5,away:13 | delay:40,out:6,away:17"},
    {0, 'C', "hold:436,out:5,away:13 | hold:436,out:6,away:17,yaw:30"},
    // 02 the mansion door: B both wait by the door with G, C player 3 shoots too (player 4 waits), D all shoot in turn
    {2, 'B', "src:2,out:-3,away:9 | src:2,out:-7,away:14"},
    {2, 'C', "src:1,out:-5,away:7 | src:2,out:-4,away:3"},
    {2, 'D', "src:1,delay:25,out:-5,away:7 | src:1,delay:50,out:-9,away:16"},
    // the four-body scenes (A: kSceneFour's places): B wait as player 2 does, C the same turned round (guarding the
    // rear), D A a beat later
    SCENE4P_FOUR(6, 4, -6, 2, 10), SCENE4P_FOUR(7, 9, 6, 6, 4), SCENE4P_FOUR(9, 7, -8, 6, 0),
    SCENE4P_FOUR(10, 6, -3, 10, 8), SCENE4P_FOUR(11, 9, -4, 10, 12), SCENE4P_FOUR(12, 8, -2, 14, 26),
    // the two-set scenes (A: two bodies): B all four, C players 3 / 4 stand still (player 2's first pose), D B a beat later
    SCENE4P_TWO(3), SCENE4P_TWO(4), SCENE4P_TWO(8), SCENE4P_TWO(14), SCENE4P_TWO(15), SCENE4P_TWO(17), SCENE4P_TWO(18),
    // E (user 2026-10-22: "make them mirror player 2 but delay them and give them space to make that natural"):
    // players 3 / 4 copy player 2's motions, player 3 8 frames later, player 4 16, spaced out beside / behind player 2
    // (the default of the ending's scene; on every other scene a choice: their tight shots were not checked one by one)
#define SCENE4P_E(s) {s, 'E', "src:2,delay:8,out:8,away:9 | src:2,delay:16,out:15,away:18"}
    SCENE4P_E(Runtime::kSceneEnding), SCENE4P_E(0), SCENE4P_E(2), SCENE4P_E(3), SCENE4P_E(4), SCENE4P_E(6), SCENE4P_E(7),
    SCENE4P_E(8), SCENE4P_E(9), SCENE4P_E(10), SCENE4P_E(11), SCENE4P_E(12), SCENE4P_E(14), SCENE4P_E(15), SCENE4P_E(17),
    SCENE4P_E(18),
#undef SCENE4P_E
    // F, the default of every scene (worker "scene4fix", user 2026-10-22: "a lot of the 4-player cut-scenes do not make
    // sense, or there are clipping issues"): E tuned scene by scene by eye (renders of all 16 scenes, 3 / 4 players,
    // arcade and PC: main checkout private\scene4p\fixed\), placed once per camera shot on the copied body's floor and
    // kept apart from the other bodies ("fix"); 10 (the drop from the stairs, seen from below) and 18 (Curien's talk, the
    // camera at the players' backs) have no room: two bodies as the arcade
    {0, 'F', "src:2,delay:8,out:8,away:9,fix | src:2,delay:16,out:15,away:18,fix"},           // run from the car
    {2, 'F', "src:2,delay:10,out:-3,away:9,fix | src:2,delay:20,out:-7,away:14,fix"},        // wait at the wall with G
    {3, 'F', "src:2,delay:8,out:8,away:9,fix | src:2,delay:16,out:15,away:18,fix"},           // wait at the ladder's foot
    {4, 'F', "src:2,delay:6,out:-1,away:22,fix | src:1,delay:12,out:-1,away:22,fix,gap:9"},  // all four fall, behind
    {6, 'F', "src:2,delay:10,out:-3,away:10,fix | src:2,delay:20,out:-8,away:16,fix"},       // on the balcony behind G
    {7, 'F', "src:2,delay:8,out:9,away:1,fix | src:2,delay:16,out:17,away:2,fix"},            // in a row on the walkway
    {8, 'F', "src:2,delay:6,out:9,away:3,fix | src:1,delay:6,out:9,away:3,fix"},              // in a row, both ends
    {9, 'F', "src:2,delay:8,out:-23,away:5,fix | src:2,delay:16,out:-29,away:-2,fix"},       // on the balcony, right
    {10, 'F', "off | off"},
    {11, 'F', "src:2,delay:8,out:-9,away:-2,fix | src:2,delay:16,out:-4,away:10,fix"},       // beside / behind G
    {12, 'F', "src:2,delay:8,out:8,away:6,fix | src:2,delay:16,out:-3,away:20,fix"},          // beside G, one behind
    {14, 'F', "src:2,delay:8,out:8,away:9,fix | src:2,delay:16,out:15,away:18,fix"},          // in a row behind G
    // (15 / 17 / 19 in world space, worker "scene19": cam:N swung the copies round with the camera before frame N;
    // 15 / 17 keep the places frame N's camera gave)
    {15, 'F', "src:1,delay:14,wx:6,wz:-22 | src:1,delay:28,wx:-4,wz:-30"},   // jump in, left
    {17, 'F', "src:2,delay:8,wx:7,wz:14 | src:1,delay:16,wx:-5,wz:26"},      // on the bridge
    {18, 'F', "off | off"},
    // Rogan (0, 0) and G (14.3, -8) on the floor: P3 on Rogan's other side, a step back; P4 beside G on the far side
    {Runtime::kSceneEnding, 'F', "src:2,delay:8,wx:-22.3,wz:3 | src:2,delay:16,wx:8.8,wz:-2.6"},
    // G, the user's notes on F (worker "scene4b", 2026-10-22; main checkout private\scene4p\fixed2\): the new default of
    // these scenes (00 / 02 / 12 / 17 / 18 / 19 keep F, 11 goes back to A); world places (wx / wz) by the bodies' floor
    // positions in HOTD_TRACE_SCENE4P
    {3, 'G', "src:2,delay:10,out:8,away:9,fix | src:2,delay:24,out:15,away:18,fix"},          // ladder: one after another
    {4, 'G', "src:2,delay:8,out:-1,away:22,fix | src:1,delay:22,out:-1,away:22,fix,gap:9"},   // fall: one after another
    {6, 'G', "src:2,delay:10,wx:-3,wz:9 | src:2,delay:20,wx:-11,wz:8"},    // behind G, left, in front of the shelves
    {7, 'G', "src:2,delay:10,wx:3.3,wz:8.4 | src:2,delay:20,wx:-4,wz:-19"},   // P3 beside G, P4 left of Rogan (off his jump)
    {8, 'G', "src:2,delay:6,wx:-8,wz:-1 | src:2,delay:14,wx:-16,wz:-4"},   // a row left of G, off the wall and the camera's path
    {9, 'G', "src:2,delay:6,wx:-4,wz:21 | src:2,delay:12,wx:0,wz:8"},      // A's places, both mirroring G
    {10, 'G', "hold:0,out:7,away:10 | out:-3,away:8"},                      // A, P3 waits on the stairs (A: jumped into Rogan)
    {14, 'G', "src:2,delay:8,wx:12,wz:-1.5 | src:2,delay:18,wx:-22,wz:-9"},  // P3 behind G, P4 right of Rogan
    {15, 'G', "src:1,delay:14,wx:6.5,wz:-17 | src:1,delay:28,wx:7,wz:10"},   // P3 left by G, P4 right of Rogan
    // H (worker "hudfix4", user 2026-10-26 on 4-19: "not evenly spaced; three turn the same direction"): a row of four
    // evenly spaced, P4 | G | P3 | Rogan, each the Rogan - G distance / 2 (16.4 / 2) apart (P3 on Rogan's other side
    // was cut off by the second shot's frame); P4 mirrors G (turns outward, away from G, as Rogan turns: the outer two
    // turn the other way from the inner two), right-handed (the `mirror` word swaps the limbs)
    {Runtime::kSceneEnding, 'H', "src:2,delay:8,wx:-7.15,wz:4 | src:2,delay:16,wx:7.15,wz:-4,mirror"},
};
// the default variation of a scene not named in scene4p= (none listed: A); A .. F stay selectable ("19:E", "09:A")
constexpr std::pair<int, char> kDefaultPick[] = {{0, 'F'},  {2, 'F'},  {3, 'G'},  {4, 'G'},  {6, 'G'},  {7, 'G'},
                                                 {8, 'G'},  {9, 'G'},  {10, 'G'}, {11, 'A'}, {12, 'F'}, {14, 'G'},
                                                 {15, 'G'}, {17, 'F'}, {18, 'F'}, {Runtime::kSceneEnding, 'H'}};
#undef SCENE4P_FOUR
#undef SCENE4P_TWO

// "00:B,9:C" -> scene -> letter
std::map<int, char> parsePicks(const std::string& s)
{
    std::map<int, char> m;
    size_t p = 0;
    while (p < s.size()) {
        size_t q = s.find(',', p);
        if (q == std::string::npos)
            q = s.size();
        int sc = -1;
        char l = 0;
        if (std::sscanf(s.substr(p, q - p).c_str(), " %d:%c", &sc, &l) == 2 && sc >= 0 && sc <= Runtime::kSceneEnding)
            m[sc] = char(std::toupper(static_cast<unsigned char>(l)));
        p = q + 1;
    }
    return m;
}

// copro layout (axes images, then the translation): a after b
void cMul(const float* a, const float* b, float* o)
{
    float t[12];
    for (int j = 0; j < 4; ++j) {
        const float* v = b + 3 * j;
        for (int i = 0; i < 3; ++i)
            t[3 * j + i] = a[i] * v[0] + a[3 + i] * v[1] + a[6 + i] * v[2] + (j == 3 ? a[9 + i] : 0.0f);
    }
    std::memcpy(o, t, sizeof t);
}
void cInvRigid(const float* c, float* o)
{
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            o[3 * j + i] = c[3 * i + j];
    for (int i = 0; i < 3; ++i)
        o[9 + i] = -(c[3 * i] * c[9] + c[3 * i + 1] * c[10] + c[3 * i + 2] * c[11]);
}
} // namespace

int Runtime::coop4_variant_scene() const
{
    if (ending_scene_now())   // the ending's two-player scene after the Magician's defeat
        return kSceneEnding;
    const uint32_t r0 = mem.at<port::a::Player>(G::players, 0).scene_routine;
    const uint32_t r1 = mem.at<port::a::Player>(G::players, 1).scene_routine;
    const auto inScene = [](uint32_t r) { return r >= 0x4BAB0 && r < 0x4E400; };
    if (!inScene(r0) && !inScene(r1))
        return -1;
    if ((r0 >= 0x4D870 || r1 >= 0x4D870) && coop4_scene >= 14)   // 14-18: past kSceneFour's address list
        return coop4_scene;
    return player_scene_now();
}

bool Runtime::coop4_scene_variant(int scene)
{
    if (scene < 0)
        return false;
    static const char* env = std::getenv("HOTD_SCENE4P");   // testing / rendering: over launcher.ini's scene4p=
    static std::string pickedFrom = "\x01";
    static std::map<int, char> picks;
    const std::string& src = env ? std::string(env) : scene4p;
    if (src != pickedFrom) {
        pickedFrom = src;
        picks = parsePicks(src);
    }
    // a scene not named: its default (kDefaultPick), else A
    char letter = 'A';
    if (const auto pk = picks.find(scene); pk != picks.end())
        letter = pk->second;
    else
        for (const auto& [sc, l] : kDefaultPick)
            if (sc == scene)
                letter = l;
    if (letter == 'A')
        return false;
    // the variation's words: HOTD_SCENE4P_DEF="00B=spec|spec;09C=..." (testing: new ones without a rebuild), else the table
    std::string spec;
    if (const char* d = std::getenv("HOTD_SCENE4P_DEF"))
        for (const char* p = d; *p;) {
            const char* q = std::strchr(p, ';');
            const std::string e(p, q ? size_t(q - p) : std::strlen(p));
            int sc = -1;
            char l = 0;
            if (std::sscanf(e.c_str(), " %d%c=", &sc, &l) == 2 && sc == scene && std::toupper(static_cast<unsigned char>(l)) == letter)
                spec = e.substr(e.find('=') + 1);
            p = q ? q + 1 : p + std::strlen(p);
        }
    if (spec.empty())
        for (const VariantDef& v : kVariants)
            if (v.scene == scene && v.letter == letter)
                spec = v.spec;
    if (spec.empty())
        return false;
    const size_t bar = spec.find('|');
    const SeatSpec seatSpec[2] = {parseSeat(spec.substr(0, bar)), parseSeat(bar == std::string::npos ? std::string() : spec.substr(bar + 1))};

    // this frame recorded (world space)
    if (scene != scene4pScene_ || frameCount != scene4pLast_ + 1) {
        scene4pRecs_.clear();
        for (int i = 0; i < 2; ++i)
            scene4pPair_[i].clear(), scene4pLink_[i].clear(), scene4pPaired_[i] = false;
    }
    scene4pScene_ = scene;
    scene4pLast_ = frameCount;
    uint32_t body[2] = {mem.at<port::a::Player>(G::players, 0).body.a, mem.at<port::a::Player>(G::players, 1).body.a};
    if (scene == kSceneEnding) {   // the ending's bodies are its own tasks: ending_p1_b_frame 0x59120, ending_p2_b_frame 0x59B10
        body[0] = body[1] = 0;
        for (const DrawModel& d : drawList)
            if (d.body != 0) {
                const uint32_t r = mem.u32(d.body);
                if (r == 0x59120u)
                    body[0] = d.body;
                else if (r == 0x59B10u)
                    body[1] = d.body;
            }
    }
    {
        // (Runtime::camera is set after this; the view of this frame is the camera slot's stored matrix 0)
        const uint32_t slot = 0x51EA50 + mem.u32(0x51F620) * 0x154;
        float cam[12], inv[12];
        for (uint32_t k = 0; k < 12; ++k)
            cam[k] = mem.f32(slot + 4 * k);
        cInvRigid(cam, inv);
        Scene4pRec rec;
        std::memcpy(rec.cam, cam, sizeof cam);
        for (const DrawModel& d : drawList)
            for (int i = 0; i < 2; ++i)
                if (d.body != 0 && d.body == body[i]) {
                    DrawModel w = d;
                    cMul(inv, d.m, w.m);
                    rec.d[i].push_back(w);
                    rec.drawn[i] = true;
                }
        for (const SkinDraw& s : skinDraws)
            for (int i = 0; i < 2; ++i)
                if (s.body != 0 && s.body == body[i]) {
                    SkinDraw w = s;
                    cMul(inv, s.a, w.a);
                    cMul(inv, s.b, w.b);
                    rec.s[i].push_back(w);
                }
        float at[2][3] = {}, bx[2][3] = {};
        for (int i = 0; i < 2; ++i)
            if (body[i] != 0) {
                float m[12];
                for (uint32_t k = 0; k < 12; ++k)
                    m[k] = mem.f32(body[i] + 0x1E4 + 4 * k);
                cMul(inv, m, rec.root[i]);
                for (int k = 0; k < 3; ++k)
                    bx[i][k] = m[k], at[i][k] = m[9 + k];
                rec.type[i] = mem.at<int16_t>(body[i] + 0xCC);
            }
        // the viewer's actors for players 3 / 4 (section 14 "actors2", now also in the variations): what actor_dress
        // gives a body playing them, kept with the frame (a delayed copy draws an older frame): the woman's / man's jump
        // (actor_pose), Rogan's arcade coat on a body he plays
        for (int s = 0; s < 2 && (actorView || actorPm); ++s)
            for (int i = 0; i < 2; ++i) {
                if (actorSwap[2 + s] < 0 || !rec.drawn[i])
                    continue;
                const int from = rec.type[i], to = coop4_scene_type(from, s);
                if (to < 0 || coop4_part_map(from, to).empty())
                    continue;
                rec.dressTo[s][i] = to;
                const std::vector<DrawModel>& dd = rec.d[i];
                // polish28: Dr. Curien's cane arm, as actor_dress gives it (was: the cane hand turned only)
                if ((to == 60 || to == 78) && from != 60 && from != 78)
                    rec.caneOn[s][i] = actor_cane_arm(body[i], from, dd.data(), dd.size(), rec.cane[s][i]);
                auto cls = [&](const DrawModel& d) {
                    return d.joint >= 0 && d.joint < 31 ? int(mem.at<int8_t>(kPartRoles + uint32_t(from) * 0xF8 + uint32_t(d.joint) * 8 + 3)) : -1;
                };
                const std::map<uint32_t, std::array<float, 12>>& pose = actor_pose(body[i], from, to);
                float probe[12];
                const bool fixParts = actorPartFix(from, to, 1, probe);   // (Dr. Curien: actorPartFix)
                if (!pose.empty() || fixParts) {
                    static const std::array<float, 12> kId = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
                    std::vector<std::array<float, 12>>& pc = rec.poseC[s][i];
                    pc.assign(dd.size(), kId);
                    rec.skinC[s][i][0] = rec.skinC[s][i][1] = kId;
                    for (size_t j = 0; j < dd.size(); ++j) {
                        if (auto it = pose.find(dd[j].part); it != pose.end()) {
                            pc[j] = it->second;
                            if (const int k = cls(dd[j]); !dd[j].shadow && (k == 8 || k == 9))
                                rec.skinC[s][i][k - 8] = it->second;
                        }
                        if (float fix[12]; fixParts && actorPartFix(from, to, cls(dd[j]), fix))
                            apMul(pc[j].data(), fix, pc[j].data());
                    }
                    rec.skinPosed[s][i] = true;
                }
                // pmfix: the actor's own skins built on this body's draws, as actor_dress gives them: its belly on a
                // body without one (Rogan's), Sophie's strip on a body she plays
                {
                    bool belly = false;
                    for (const SkinDraw& sd : rec.s[i])
                        belly = belly || sd.type != kSophieStrip;
                    const bool wantBelly = !belly && actor_needs_belly(from, to), wantStrip = to == 34 && from != 34;
                    for (size_t j = 0; j < dd.size() && (wantBelly || wantStrip); ++j) {
                        const int k = cls(dd[j]);
                        if (dd[j].shadow || dd[j].model != mem.u32(dd[j].part))
                            continue;
                        if (wantBelly && (k == 8 || k == 9))
                            rec.bellyAt[s][i][k - 8] = int(j);
                        if (wantStrip && (k == 10 || k == 11))
                            rec.stripAt[s][i][k - 10] = int(j);
                    }
                }
                if (to == 2 && from != 2 && !pcModels) {   // Rogan's coat (actor_dress's rule, this frame's motion)
                    const int32_t motion = mem.at<int16_t>(body[i] + 0xD0), f = mem.at<int16_t>(body[i] + 0xF4);
                    for (size_t j = 0; j < dd.size() && motion != 0x168; ++j)
                        if (!dd[j].shadow && cls(dd[j]) == 8 && dd[j].model == mem.u32(dd[j].part)) {
                            const int32_t ch = mem.at<int8_t>(G::chapter);
                            const int32_t sub = int32_t(int16_t(mem.at<int16_t>(body[i] + 0xF6) % 2));
                            rec.coatModel[s][i] = uint32_t(ch == 5 || ch == 6 ? int32_t(mem.u32(G::frame_counter) / 2 % 0x32) + 0x119
                                                                               : (sub * 0x32 + f / 2) % 100 + 0xB5);
                            rec.coatAt[s][i] = int(j);
                            break;
                        }
                }
            }
        rec.p1Side = rec.drawn[0] && rec.drawn[1] ? (at[0][0] < at[1][0] ? -1.0f : 1.0f)
                                                  : (bx[rec.drawn[0] ? 0 : 1][0] < 0 ? -1.0f : 1.0f);
        // a camera cut (the camera jumps or turns more in one frame than any of the scenes' moves) starts a new shot
        if (!scene4pRecs_.empty()) {
            const Scene4pRec& pr = scene4pRecs_.back();
            float pinv[12];
            cInvRigid(pr.cam, pinv);
            float dp = 0, dot = 0;
            for (int k = 0; k < 3; ++k) {
                dp += (inv[9 + k] - pinv[9 + k]) * (inv[9 + k] - pinv[9 + k]);
                dot += cam[3 * k + 2] * pr.cam[3 * k + 2];
            }
            dp = std::sqrt(dp);
            const bool cut = dp > kScene4pCutMove || dot < kScene4pCutTurn;
            rec.shot = pr.shot + (cut ? 1 : 0);
            rec.shotStart = cut ? int(scene4pRecs_.size()) : pr.shotStart;
            static const bool traceCut = std::getenv("HOTD_TRACE_SCENE4P") != nullptr;
            if (traceCut && log && (cut || dp > kScene4pCutMove * 0.25f || dot < 1 - (1 - kScene4pCutTurn) * 0.25f)) {
                char buf[160];
                std::snprintf(buf, sizeof buf, "scene4p %d rec %d: camera moved %.2f turned %.4f%s -> shot %d", scene,
                              int(scene4pRecs_.size()), dp, dot, cut ? " CUT" : "", rec.shot);
                log(buf);
            }
        }
        scene4pRecs_.push_back(std::move(rec));
    }
    const int n = int(scene4pRecs_.size()), now = n - 1;
    float cur[12];
    std::memcpy(cur, scene4pRecs_.back().cam, sizeof cur);
    const Scene4pRec& shotRec = scene4pRecs_[size_t(scene4pRecs_.back().shotStart)];   // this shot's first frame
    const bool seat[2] = {mem.at<int32_t>(player_rec(2)) == 5, mem.at<int32_t>(player_rec(3)) == 5};
    struct Placed {
        bool on = false, fix = false;
        const Scene4pRec* r = nullptr;
        int srcI = 0, toType = 0;
        const std::map<uint32_t, uint32_t>* map = nullptr;
        float place[12] = {};   // world -> world: turned about the body's place, then moved
        float at[3] = {};       // where the body's place lands (world)
        float up[3] = {};       // the copied body's up axis (unit)
        float gap = 0;
        bool mirror = false;
        float mir[12] = {};     // mirror: world -> world, the reflection about the copied body's middle (its x axis)
    } placed[2];
    for (int s = 0; s < 2; ++s) {
        const SeatSpec& sp = seatSpec[s];
        if (!seat[s] || !sp.on || now < sp.from || now > sp.until)
            continue;
        if (std::find(sp.hide.begin(), sp.hide.end(), scene4pRecs_.back().shot) != sp.hide.end())
            continue;
        int idx = sp.hold >= 0 ? std::min(sp.hold, now) : std::max(0, now - sp.delay);
        int srcI = sp.src >= 0 ? sp.src : s;
        // fix, delayed: not from before this shot when the copied body was moved to its new spot at the cut (the game
        // puts the bodies elsewhere for a new shot; a frame from before would draw the copy at the old spot, out of the
        // picture, until the delay caught up: user 2026-10-22 on 1-00, "P3 / P4 pop in"): the shot's first pose until then
        // (the body can be moved a frame or two after the camera's cut: from the first frame at its new spot)
        if (const int st = scene4pRecs_.back().shotStart; sp.fix && sp.hold < 0 && st > 0 && idx < st + 3) {
            // also a small re-seat at the cut: the body set a little higher / lower (the new shot's floor) or turned
            // round (the rotations' normalised inner product (1 + 2 cos angle) / 3 below 2 / 3: more than 60 degrees
            // in a frame) where it stood still the frame before (a fall or a turn going on across the cut is not one).
            // 4-19 (user 2026-10-27, "P3 Rogan floats for a split second" after "Shall we go now?"): G is put 0.2 lower
            // and turned at the cut, and P3 (8 frames behind G) stood 0.2 above the floor until its delay caught up,
            // then dropped (P4, 16 behind, likewise)
            auto reseat = [&](int k) {
                if (k < 1)
                    return false;
                const float* a = scene4pRecs_[size_t(k - 1)].root[srcI];
                const float* b = scene4pRecs_[size_t(k)].root[srcI];
                float rab = 0, raa = 0, rbb = 0;
                for (int i = 0; i < 9; ++i)
                    rab += a[i] * b[i], raa += a[i] * a[i], rbb += b[i] * b[i];
                return (raa > 0 && rbb > 0 && rab / std::sqrt(raa * rbb) < 2.0f / 3.0f) || std::fabs(a[10] - b[10]) > 0.05f;
            };
            for (int k = st; k <= std::min(now, st + 3); ++k) {
                const Scene4pRec& a = scene4pRecs_[size_t(k - 1)];
                const Scene4pRec& b = scene4pRecs_[size_t(k)];
                if (!a.drawn[srcI] || !b.drawn[srcI])
                    continue;
                float d2 = 0;
                for (int i = 0; i < 3; ++i)
                    d2 += (a.root[srcI][9 + i] - b.root[srcI][9 + i]) * (a.root[srcI][9 + i] - b.root[srcI][9 + i]);
                if (d2 > kScene4pCutMove * kScene4pCutMove) {
                    idx = std::max(idx, k);
                    break;
                }
                if (reseat(k) && !(k >= 2 && scene4pRecs_[size_t(k - 2)].drawn[srcI] && reseat(k - 1)))
                    idx = std::max(idx, k);
            }
        }
        // a frame where the copied body was drawn (held: the nearest before; the other body when that one never was;
        // held or delayed: else its first drawn frame, so a delayed body does not pop in after the scene's start)
        int found = -1;
        for (int k = idx; k >= 0 && found < 0; --k)
            if (scene4pRecs_[size_t(k)].drawn[srcI])
                found = k;
        for (int k = idx + 1; k < n && found < 0 && (sp.hold >= 0 || sp.delay > 0); ++k)
            if (scene4pRecs_[size_t(k)].drawn[srcI])
                found = k;
        if (found < 0)
            continue;
        const Scene4pRec& r = scene4pRecs_[size_t(found)];
        const int fromType = r.type[srcI], toType = fromType == 0 ? -1 : coop4_scene_type(fromType, s);
        if (toType < 0)
            continue;
        const std::map<uint32_t, uint32_t>& map = coop4_part_map(fromType, toType);
        if (map.empty())
            continue;
        Placed& pl = placed[s];
        pl.on = true;
        pl.fix = sp.fix;
        pl.r = &r;
        pl.srcI = srcI;
        pl.toType = toType;
        pl.map = &map;
        pl.gap = sp.gap;
        if (sp.mirror) {   // the plane through the copied body's place, its normal the body's x axis (left / right)
            const float* rt = r.root[srcI];
            float nx[3] = {rt[0], rt[1], rt[2]};
            const float l = std::sqrt(nx[0] * nx[0] + nx[1] * nx[1] + nx[2] * nx[2]);
            if (l > 1e-4f) {
                for (float& v : nx)
                    v /= l;
                const float d = rt[9] * nx[0] + rt[10] * nx[1] + rt[11] * nx[2];
                for (int j = 0; j < 3; ++j)
                    for (int i = 0; i < 3; ++i)
                        pl.mir[3 * j + i] = (i == j ? 1.0f : 0.0f) - 2.0f * nx[i] * nx[j];
                for (int i = 0; i < 3; ++i)
                    pl.mir[9 + i] = 2.0f * d * nx[i];
                pl.mirror = true;
            }
            // the left / right pairs of the body's draws (once a scene, from its first drawn frame: the bodies stand
            // there): each draw's place in the body's own frame, mirrored across its x, matched to the nearest draw
            // (both ways, the same shadow pass); unmatched near the middle: its own pair. The gun hand then takes the
            // other hand's mirrored place, so the copy stays right-handed (user: "keep them right-handed")
            if (!scene4pPaired_[srcI]) {
                scene4pPaired_[srcI] = true;
                const Scene4pRec* first = nullptr;
                for (const Scene4pRec& q : scene4pRecs_)
                    if (q.drawn[srcI]) {
                        first = &q;
                        break;
                    }
                if (first) {
                    const float* ro = first->root[srcI];
                    const std::vector<DrawModel>& dd = first->d[srcI];
                    const size_t m = dd.size();
                    std::vector<std::array<float, 3>> loc(m);
                    for (size_t j = 0; j < m; ++j)
                        for (int a = 0; a < 3; ++a) {   // R^T (t - p): the root's axis a dotted with the offset
                            float v = 0;
                            for (int i = 0; i < 3; ++i)
                                v += ro[3 * a + i] * (dd[j].m[9 + i] - ro[9 + i]);
                            loc[j][size_t(a)] = v;
                        }
                    const auto best = [&](size_t j) {
                        size_t b = j;
                        float bd = 1e30f;
                        for (size_t k = 0; k < m; ++k) {
                            if (dd[k].shadow != dd[j].shadow)
                                continue;
                            const float dx = loc[k][0] + loc[j][0], dy = loc[k][1] - loc[j][1], dz = loc[k][2] - loc[j][2];
                            const float d2 = dx * dx + dy * dy + dz * dz;
                            if (d2 < bd)
                                bd = d2, b = k;
                        }
                        return b;
                    };
                    std::vector<int>& pr = scene4pPair_[srcI];
                    pr.resize(m);
                    int pairs = 0;
                    for (size_t j = 0; j < m; ++j) {
                        const size_t k = best(j);
                        pr[j] = k != j && best(k) == j ? int(k) : int(j);
                        pairs += pr[j] != int(j) ? 1 : 0;
                    }
                    // each draw's link X = F_k^-1 S0 F_j (F: the first frame's world matrices, S0 its mirror): later,
                    // S M_k X puts part j where part k's motion since then, mirrored, takes it (exact at that frame,
                    // whatever the models' own axes are)
                    float s0[12] = {}, nx[3] = {ro[0], ro[1], ro[2]};
                    const float l0 = std::sqrt(nx[0] * nx[0] + nx[1] * nx[1] + nx[2] * nx[2]);
                    for (float& v : nx)
                        v = l0 > 1e-4f ? v / l0 : 0.0f;
                    const float d0 = ro[9] * nx[0] + ro[10] * nx[1] + ro[11] * nx[2];
                    for (int j = 0; j < 3; ++j)
                        for (int i = 0; i < 3; ++i)
                            s0[3 * j + i] = (i == j ? 1.0f : 0.0f) - 2.0f * nx[i] * nx[j];
                    for (int i = 0; i < 3; ++i)
                        s0[9 + i] = 2.0f * d0 * nx[i];
                    std::vector<std::array<float, 12>>& lk = scene4pLink_[srcI];
                    lk.assign(m, {});
                    for (size_t j = 0; j < m; ++j) {
                        float inv[12], t[12];
                        cInvRigid(dd[size_t(pr[j])].m, inv);
                        cMul(s0, dd[j].m, t);
                        cMul(inv, t, lk[j].data());
                    }
                    if (log)
                        log("scene4p " + std::to_string(scene) + ": mirror pairs of body " + std::to_string(srcI + 1) + ": " +
                            std::to_string(pairs / 2) + " of " + std::to_string(m) + " draws");
                    static const bool tracePairs = std::getenv("HOTD_TRACE_SCENE4P") != nullptr;
                    if (tracePairs && log)
                        for (size_t j = 0; j < m; ++j) {
                            char buf[160];
                            std::snprintf(buf, sizeof buf, "  draw %zu model 0x%X%s at %.2f %.2f %.2f -> %d", j, dd[j].model,
                                          dd[j].shadow ? " shadow" : "", loc[j][0], loc[j][1], loc[j][2], pr[j]);
                            log(buf);
                        }
                }
            }
        }
        // the place: the camera-space offset (of the copied frame; fix: of this shot's first frame) into world space,
        // the turn about the body's up axis
        const Scene4pRec& camRec = !sp.fix ? r : sp.cam >= 0 ? scene4pRecs_[size_t(std::min(sp.cam, now))] : shotRec;
        const float side = srcI == 0 ? camRec.p1Side : -camRec.p1Side;
        const float off[3] = {side * sp.out, sp.fix ? 0.0f : sp.up, sp.away};
        const float* rt0 = r.root[srcI];
        float* u = pl.up;
        for (int k = 0; k < 3; ++k)   // fix: the world's up (y points down), as a falling / tumbling body's axis turns
            u[k] = sp.fix ? (k == 1 ? -1.0f : 0.0f) : rt0[3 + k];
        const float len = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        for (int k = 0; k < 3; ++k)
            u[k] = len > 0 ? u[k] / len : 0.0f;
        const float a = sp.yaw * 3.14159265f / 180.0f, c = std::cos(a), sn = std::sin(a), t = 1 - c;
        const float ux[3][3] = {{0, -u[2], u[1]}, {u[2], 0, -u[0]}, {-u[1], u[0], 0}};   // the cross-product matrix
        for (int j = 0; j < 3; ++j)        // column j: the image of axis j (Rodrigues)
            for (int i = 0; i < 3; ++i)
                pl.place[3 * j + i] = (i == j ? c : 0.0f) + t * u[i] * u[j] + sn * ux[i][j];
        float ow[3];   // the offset: camera-space axes (rows of the camera's rotation) into world space
        for (int i = 0; i < 3; ++i)
            ow[i] = camRec.cam[3 * i] * off[0] + camRec.cam[3 * i + 1] * off[1] + camRec.cam[3 * i + 2] * off[2];
        if (sp.fix) {   // on the copied body's floor: `out` / `away` along the camera's across / view axes laid flat on
                        // the floor (each its full length, also under a camera looking up or down), `up` along the up axis
            float ax[2][3];
            for (int a2 = 0; a2 < 2; ++a2) {
                const int col = a2 == 0 ? 0 : 2;   // camera x, z axes in world space: rows of the rotation
                float l = 0, d = 0;
                for (int i = 0; i < 3; ++i)
                    ax[a2][i] = camRec.cam[3 * i + col], d += ax[a2][i] * u[i];
                for (int i = 0; i < 3; ++i)
                    ax[a2][i] -= d * u[i], l += ax[a2][i] * ax[a2][i];
                l = std::sqrt(l);
                for (int i = 0; i < 3; ++i)
                    ax[a2][i] = l > 1e-4f ? ax[a2][i] / l : 0.0f;
            }
            if (std::fabs(ax[1][0]) + std::fabs(ax[1][2]) < 1e-4f)   // looking straight down / up: on screen up
                for (int i = 0; i < 3; ++i)
                    ax[1][i] = u[1] * ax[0][2] * (i == 0 ? 1.0f : 0.0f) - u[1] * ax[0][0] * (i == 2 ? 1.0f : 0.0f);
            for (int i = 0; i < 3; ++i)
                ow[i] = off[0] * ax[0][i] + off[2] * ax[1][i] + sp.up * u[i];
            static const bool traceOw = std::getenv("HOTD_TRACE_SCENE4P") != nullptr;
            if (traceOw && log && (now == sp.cam || now == scene4pRecs_.back().shotStart)) {   // as world words
                float ci[12];
                cInvRigid(camRec.cam, ci);
                char buf[200];
                std::snprintf(buf, sizeof buf, "scene4p %d rec %d seat %d: camera offset = wx:%.1f,wz:%.1f (camera at %.1f %.1f %.1f)",
                              scene, now, s + 3, ow[0], ow[2], ci[9], ci[10], ci[11]);
                log(buf);
            }
            if (sp.world)
                for (int i = 0; i < 3; ++i)
                    ow[i] = (i == 0 ? sp.wx : i == 2 ? sp.wz : 0.0f) + sp.up * u[i];
        }
        const float* p = rt0 + 9;
        for (int i = 0; i < 3; ++i) {
            const float qp = pl.place[i] * p[0] + pl.place[3 + i] * p[1] + pl.place[6 + i] * p[2];
            pl.place[9 + i] = p[i] - qp + ow[i];   // the body's place p lands on p + ow
            pl.at[i] = p[i] + ow[i];
        }
    }
    // fix: no overlaps. Players 3 / 4 (their bodies' places) are pushed apart across the floor only (no part along the
    // up axis) from players 1 / 2 as they stand now and from each other, to at least `gap`; a few rounds, as a push
    // away from one body can bring it near another
    {
        const Scene4pRec& nowRec = scene4pRecs_.back();
        for (int round = 0; round < 4; ++round)
            for (int s = 0; s < 2; ++s) {
                Placed& pl = placed[s];
                if (!pl.on || !pl.fix)
                    continue;
                const float* others[3] = {nullptr, nullptr, nullptr};
                for (int i = 0; i < 2; ++i)
                    if (nowRec.drawn[i])
                        others[i] = nowRec.root[i] + 9;
                if (placed[1 - s].on)
                    others[2] = placed[1 - s].at;
                for (const float* o : others) {
                    if (!o)
                        continue;
                    float d[3], along = 0;
                    for (int i = 0; i < 3; ++i)
                        d[i] = pl.at[i] - o[i];
                    for (int i = 0; i < 3; ++i)
                        along += d[i] * pl.up[i];
                    for (int i = 0; i < 3; ++i)
                        d[i] -= along * pl.up[i];
                    float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                    if (dist >= pl.gap)
                        continue;
                    if (dist < 0.01f) {   // on top of each other: out along this shot's camera x (across the floor)
                        for (int i = 0; i < 3; ++i)
                            d[i] = shotRec.cam[3 * i] * (s == 0 ? -1.0f : 1.0f);
                        float a2 = 0;
                        for (int i = 0; i < 3; ++i)
                            a2 += d[i] * pl.up[i];
                        for (int i = 0; i < 3; ++i)
                            d[i] -= a2 * pl.up[i];
                        dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                        if (dist < 0.01f)
                            continue;
                        for (float& v : d)
                            v /= dist;
                        dist = 0;
                    } else
                        for (float& v : d)
                            v /= dist;
                    const float push = pl.gap - dist;
                    for (int i = 0; i < 3; ++i)
                        pl.at[i] += d[i] * push, pl.place[9 + i] += d[i] * push;
                }
            }
    }
    static const bool tracePlace = std::getenv("HOTD_TRACE_SCENE4P") != nullptr;
    static const bool traceAll = tracePlace && std::getenv("HOTD_TRACE_SCENE4P")[0] == '2';   // =2: every frame
    if (tracePlace && log && (now % 20 == 0 || traceAll)) {
        const Scene4pRec& b = scene4pRecs_.back();
        char buf[400];
        float ci[12];
        cInvRigid(b.cam, ci);
        // (yaw: the body's z axis on the floor, degrees)
        const auto yawOf = [](const float* m) { return std::atan2(m[6], m[8]) * 57.29578f; };
        std::snprintf(buf, sizeof buf, "scene4p %d rec %d shot %d drawn %d%d copies %d%d: p1 %.1f %.1f %.1f p2 %.1f %.1f %.1f p3 %.1f %.1f %.1f p4 %.1f %.1f %.1f camera %.1f %.1f %.1f yaw p1 %.0f p2 %.0f",
                      scene, now, b.shot, int(b.drawn[0]), int(b.drawn[1]), int(placed[0].on), int(placed[1].on), b.root[0][9],
                      b.root[0][10], b.root[0][11], b.root[1][9], b.root[1][10], b.root[1][11],
                      placed[0].at[0], placed[0].at[1], placed[0].at[2], placed[1].at[0], placed[1].at[1], placed[1].at[2], ci[9], ci[10], ci[11],
                      yawOf(b.root[0]), yawOf(b.root[1]));
        log(buf);
    }
    for (int ps = 0; ps < 2; ++ps) {
        const Placed& pl = placed[ps];
        if (!pl.on)
            continue;
        float toCam[12];
        cMul(cur, pl.place, toCam);
        if (pl.mirror)
            cMul(toCam, pl.mir, toCam);
        const std::vector<DrawModel>& src = pl.r->d[pl.srcI];
        const std::vector<int>& pair = scene4pPair_[pl.srcI];
        const bool paired = pl.mirror && pair.size() == src.size() && scene4pLink_[pl.srcI].size() == src.size();
        // the viewer's actor for this seat (made with the frame): its pose, Rogan's coat
        const bool dressed = pl.r->dressTo[ps][pl.srcI] == pl.toType;
        const std::vector<std::array<float, 12>>& poseC = pl.r->poseC[ps][pl.srcI];
        const bool posed = dressed && poseC.size() == src.size();
        auto drawM = [&](size_t j, float* o) {   // draw j's world matrix, re-posed for the actor
            if (posed)
                apMul(src[j].m, poseC[j].data(), o);
            else
                std::memcpy(o, src[j].m, 12 * sizeof(float));
        };
        const bool ownCoat = dressed && pl.toType == 2 && pl.r->type[pl.srcI] == 2;   // Rogan copied as Rogan: his coat
        const int coatAt = dressed ? pl.r->coatAt[ps][pl.srcI] : -1;
        const size_t firstOut = drawList.size();   // (polish28: this copy's draws, for Dr. Curien's cane arm)
        std::vector<size_t> outAt(src.size(), SIZE_MAX);   // (pmfix: each source draw's copy in drawList)
        for (size_t j = 0; j < src.size() + (coatAt >= 0 ? 1 : 0); ++j) {
            const bool coat = j == src.size();   // Rogan's coat on the body he plays (after the body's draws)
            const size_t sj = coat ? size_t(coatAt) : j;
            const DrawModel& d = src[sj];
            if (!coat && !ownCoat && isRoganCoat(d, mem.u32(d.part)))   // Rogan's coat: his own
                continue;
            DrawModel c = d;
            c.model = coat ? pl.r->coatModel[ps][pl.srcI] : coop4_copy_model(*pl.map, pl.r->type[pl.srcI], pl.toType, d);
            if (c.model == 0)
                continue;   // (a part the actor has none of)
            if (static const bool tr = std::getenv("HOTD_TRACE_SCENE4P") != nullptr; tr && log && !pl.map->count(d.model) && unmappedLogged_.insert(d.model).second)
                log("scene4p " + std::to_string(scene) + ": model " + std::to_string(d.model) + " joint " + std::to_string(d.joint) + " -> " + std::to_string(c.model) +
                    " (class " + std::to_string(d.joint >= 0 ? int(mem.at<int8_t>(kPartRoles + uint32_t(pl.r->type[pl.srcI]) * 0xF8 + uint32_t(d.joint) * 8 + 3)) : -1) +
                    ") not in the part map");
            if (paired) {   // mirror: this part's model where its pair's motion, mirrored, takes it (S M_k X)
                float m[12], pm[12];
                const size_t k = size_t(pair[sj]);
                drawM(k, pm);
                // rpolish (user 2026-10-26, 4-19 H: the mirrored Dr. Curien held his cane out, his head on its side):
                // his part fix (actorPartFix: the head, the cane hand) belongs to the part in its own frame, so the
                // pair's fix comes off before the link and this part's goes on after it (was: the pair's, before)
                const int fromType = pl.r->type[pl.srcI];
                const auto partCls = [&](const DrawModel& q) {
                    return q.joint >= 0 && q.joint < 31 ? int(mem.at<int8_t>(kPartRoles + uint32_t(fromType) * 0xF8 + uint32_t(q.joint) * 8 + 3)) : -1;
                };
                float fk[12], fj[12];
                if (posed && actorPartFix(fromType, pl.toType, partCls(src[k]), fk)) {
                    float inv[12];
                    apInvRigid(fk, inv);
                    apMul(pm, inv, pm);
                }
                cMul(pm, scene4pLink_[pl.srcI][sj].data(), m);
                if (posed && !coat && actorPartFix(fromType, pl.toType, partCls(d), fj))
                    apMul(m, fj, m);
                cMul(toCam, m, c.m);
            } else {
                float m[12];
                drawM(sj, m);
                cMul(toCam, m, c.m);   // (a shadow's projection, shadowM, is world space: kept)
            }
            if (!coat)
                outAt[sj] = drawList.size();
            drawList.push_back(c);
            if (const uint32_t gunModel = coat ? 0 : actor_gun_model(pl.r->type[pl.srcI], pl.toType, d); gunModel != 0)   // (rellook)
                c.model = gunModel, drawList.push_back(c);
        }
        // polish28 (user 10-28: Curien in the P3 / P4 roles "still only gets the old hand fix"): his cane arm re-built
        // from this copy's drawn chest with his idle's joints, as actor_dress does for players 1 / 2
        if (dressed && pl.r->caneOn[ps][pl.srcI] && firstOut < drawList.size())
            actor_cane_apply(pl.r->cane[ps][pl.srcI], drawList.data() + firstOut, drawList.size() - firstOut);
        const bool skinPosed = dressed && pl.r->skinPosed[ps][pl.srcI];
        const size_t firstSkin = skinDraws.size();
        for (const SkinDraw& sd : pl.r->s[pl.srcI]) {
            if (dressed && sd.type == kSophieStrip && pl.toType != 34)   // Sophie's strip: hers only
                continue;
            SkinDraw c = sd;
            c.type = dressed && sd.type == kSophieStrip ? sd.type : pl.toType;
            float a[12], b[12];
            std::memcpy(a, sd.a, sizeof a);
            std::memcpy(b, sd.b, sizeof b);
            if (skinPosed) {
                apMul(sd.a, pl.r->skinC[ps][pl.srcI][0].data(), a);
                apMul(sd.b, pl.r->skinC[ps][pl.srcI][1].data(), b);
            }
            cMul(toCam, a, c.a);
            cMul(toCam, b, c.b);
            skinDraws.push_back(c);
        }
        if (dressed)   // galfix4: the actor's own hip joints (actor_dress's rule)
            actor_fit((uint64_t(uint32_t(pl.r->type[pl.srcI])) << 32) | uint32_t(pl.toType), drawList, firstOut, skinDraws, firstSkin);
        // pmfix: the actor's belly on a body without one (Rogan's), Sophie's strip on a body she plays, from this
        // copy's drawn chest / hips and thighs (actor_dress's rule)
        if (dressed) {
            auto ownSkin = [&](const int* at, int type) {
                if (at[0] < 0 || at[1] < 0 || outAt[size_t(at[0])] == SIZE_MAX || outAt[size_t(at[1])] == SIZE_MAX)
                    return;
                SkinDraw c{type, {}, {}};
                std::memcpy(c.a, drawList[outAt[size_t(at[0])]].m, sizeof c.a);
                std::memcpy(c.b, drawList[outAt[size_t(at[1])]].m, sizeof c.b);
                skinDraws.push_back(c);
            };
            ownSkin(pl.r->bellyAt[ps][pl.srcI], pl.toType);
            ownSkin(pl.r->stripAt[ps][pl.srcI], kSophieStrip);
        }
    }
    return true;
}

} // namespace hotd::game
