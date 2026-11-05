// The creature core (teaser L3): a body's part tree (body_init, part_init, body_part_alloc, creature_type_setup), the
// motion family (set_motion, blends, the second motion, the frame counters and the pose of the frame) and the body's
// drawing (object_draw, body_draw, part_draw), which leaves the part matrices and hit spheres the shot test reads.
// First read for the runtime by R1 (src/game/creature.cpp), checked here against the arcade disassembly (R1 left out
// body_draw's fade, the shadow pass of part_draw and object_draw_model's placement). Arcade forms only.
//
// Motion data (formats.md 23): motion m's frames start at the main_data address in 0xC7540[m] (frame counts
// motion_frame_counts[m]); a frame record is the root x, y, z (floats) and three 16-bit angles per joint, padded to a
// word: 4 x ((48 J + 0x7F) >> 5) bytes, J = the type's joint count 0xC73C0[type].
#include "port/game.h"

namespace hotd::port {

namespace cr {
// arcade addresses not in names.tsv yet (A only; names.tsv is shared, added after the teaser)
constexpr uint32_t body_cut = 0x520AE0;          // int16: a part was marked to be severed this frame
constexpr uint32_t body_home = 0x520AE2;         // int16: the part being drawn is the type's home part
constexpr uint32_t frame_data = 0x53E3A0;        // the frame record body_draw poses the parts with
constexpr uint32_t motion2_data = 0x53E3A4;      // the second motion's
constexpr uint32_t joints = 0xC73C0;             // per type: the joint count
constexpr uint32_t motion_data = 0xC7540;        // per motion: its frame records (main_data)
constexpr uint32_t mirrored = 0x7C6F0;           // byte per motion: a mirrored motion
constexpr uint32_t home_class = 0x14A430;        // per type: the part class whose position is the object's home
constexpr uint32_t hit_spheres = 0x520B10;       // per part model: the hit sphere's centre (16 bytes)
constexpr uint32_t shadow_models = 0x14BA80;     // per type: 0 or a table of shadow models by part role
constexpr uint32_t home_raise_a = 0x76770;       // per type: the home's height above the home part
constexpr uint32_t home_raise_b = 0x14A5B0;      // the same with motion_flags3 bit 3
constexpr uint32_t last_enemy_home = 0x51FDE0;   // the home of the last enemy left (x, y, z)
constexpr uint32_t draw_modes = 0xC6690;         // object_draw's routine per draw mode
constexpr uint32_t light_records = 0x520A40;     // 3 words per draw_param: the light sent to the geometrizer
constexpr uint32_t skin_numbers = 0x20E40;       // int16 per type: the generated skin (-1 none)
constexpr uint32_t first_side = 0x118000;        // byte per type: bit 0 = the side of role-1 first-level parts
constexpr uint32_t type_scale = 0x118060;        // float per type: the body scale (names.tsv calls it part_init_p, a func)
constexpr uint32_t type_radius = 0x1181E0;       // float per type: the object's hit radius
constexpr uint32_t skin_buffers = 0x51FDC8;      // the skin buffers (+0xC: per slot 0x348 bytes); Sophie's 0x51FDCC / D0
constexpr uint32_t skin_templates = 0x2F803A0;   // main_data: skin templates (0x640 bytes each)
constexpr uint32_t skin_polygons = 0x2F8D220;    // main_data: int per skin, its polygon count
constexpr uint32_t sophie_strips = 0x21640;      // Sophie's two strip templates (maincpu)
} // namespace cr

// ---------------------------------------------------------------------------------------------------------------
// helpers (inline in the original)

template <class P> int32_t Game<P>::body_joints(uint32_t obj)
{
    return int32_t(mem.u32(cr::joints + 4 * uint32_t(int32_t(of<int16_t>(obj, 0xCC)))));
}

template <class P> uint32_t Game<P>::motion_frame_record(uint32_t obj, int32_t motion, int32_t frame)
{
    const uint32_t words = (uint32_t(body_joints(obj)) * 48 + 0x7F) >> 5;
    return mem.u32(cr::motion_data + 4 * uint32_t(motion)) + uint32_t(frame) * 4 * words;
}

namespace {
// one angle of a pose blend: from + (to - from) x t / 0x10000 the short way round
int16_t blendAngle(uint16_t from, uint16_t to, int32_t t)
{
    const uint32_t d = uint32_t(to - from) & 0xFFFF;
    if (d > 0x7FFF)
        return int16_t(int32_t(from) - int32_t(uint32_t(0x10000 - d) * uint32_t(t)) / 0x10000);
    return int16_t(int32_t(from) + int32_t(d * uint32_t(t)) / 0x10000);
}

uint32_t angleGap(uint16_t a, uint16_t b)
{
    uint32_t d = uint32_t(b - a) & 0xFFFF;
    if (d > 0x7FFF)
        d = 0x10000 - d;
    return d;
}
} // namespace

// the joints of `from` blended toward `to` by t (0x10000 = all `to`) into `out` (records +0xC on); a joint whose
// angles differ by 0x7000 or more anywhere is blended toward the equivalent angles (a0 + 0x8000, -0x8000 - a1,
// a2 + 0x8000) instead
template <class P> void Game<P>::pose_blend_joints(uint32_t obj, uint32_t from, uint32_t to, uint32_t out, int32_t t)
{
    for (int32_t j = 0; j < body_joints(obj); ++j) {
        const uint32_t f = from + 0xC + uint32_t(j) * 6, g = to + 0xC + uint32_t(j) * 6, o = out + 0xC + uint32_t(j) * 6;
        uint16_t tgt[3] = {mem.at<uint16_t>(g), mem.at<uint16_t>(g + 2), mem.at<uint16_t>(g + 4)};
        if (angleGap(mem.at<uint16_t>(f), tgt[0]) > 0x6FFF || angleGap(mem.at<uint16_t>(f + 2), tgt[1]) > 0x6FFF ||
            angleGap(mem.at<uint16_t>(f + 4), tgt[2]) > 0x6FFF) {
            tgt[0] = uint16_t(tgt[0] + 0x8000);
            tgt[1] = uint16_t(0xFFFF8000u - tgt[1]);
            tgt[2] = uint16_t(tgt[2] + 0x8000);
        }
        for (uint32_t k = 0; k < 3; ++k)
            mem.at<int16_t>(o + 2 * k) = blendAngle(mem.at<uint16_t>(f + 2 * k), tgt[k], t);
    }
}

// the root of a blend: x = z = 0, y weighted (from.y x rest + to.y x frame) / (frame + rest)
template <class P> void Game<P>::pose_blend_root(uint32_t from, uint32_t to, uint32_t out, int32_t frame, int32_t rest)
{
    const float r = float(rest), f = float(frame);
    mem.set32(out, 0);
    mem.set32(out + 8, 0);
    mem.set32(out + 4, fbits((mem.f32(from + 4) * r + mem.f32(to + 4) * f) / (f + r)));
}

// ---------------------------------------------------------------------------------------------------------------
// spawning

// 0x36750 part_init (part, parent, part data): flags 0x201, owner, model, hit radius = the model's radius x the type's
// scale, offset, children (mem_alloc 4 x count), role, side flags (bit 4 from the data, bit 5: first-level parts by
// the type's side byte and the role, deeper ones from the parent), depth
template <class P> void Game<P>::part_init(uint32_t part, uint32_t parent, uint32_t data)
{
    BodyPart& p = mem.at<BodyPart>(part);
    const uint32_t body = mem.u32(G::body_current);
    p.flags = 0x201;
    p.owner.a = body;
    const int32_t id = mem.at<int32_t>(data);
    p.part_id = id;
    const float r = mem.f32(G::part_model_radius + uint32_t(id) * 0x10);
    const float s = mem.f32(cr::type_scale + 4 * uint32_t(int32_t(of<int16_t>(body, 0xCC))));
    p.w78 = 0;
    p.hit_radius = s * r;
    for (uint32_t k = 0; k < 3; ++k)   // floats in the part data (copied as words)
        mem.set32(part + 4 + 4 * k, mem.u32(data + 4 + 4 * k));
    p.child_count = mem.at<int32_t>(data + 0x18);
    p.role = mem.at<int32_t>(data + 0x14);
    p.side_flags = uint8_t((p.side_flags & 0xEF) | ((mem.at<uint8_t>(data + 0x10) & 1) << 4));
    p.children.a = p.child_count == 0 ? 0 : mem_alloc(uint32_t(p.child_count) << 2);
    BodyPart& q = mem.at<BodyPart>(part);
    q.depth = mem.at<BodyPart>(parent).depth + 1;
    uint8_t side;
    if (q.depth == 1) {
        const uint8_t b = mem.at<uint8_t>(cr::first_side + uint32_t(int32_t(of<int16_t>(body, 0xCC))));
        side = uint8_t(((q.role == 1 ? b : uint8_t(~b)) & 1) << 5);
    } else {
        side = mem.at<BodyPart>(parent).side_flags & 0x20;
    }
    q.side_flags = uint8_t((q.side_flags & 0xDF) | side);
}

// 0x36880 body_part_alloc (parent, part data, index): a part node (mem_alloc 0xD8) as the parent's child `index`, its
// mark cleared, part_init, then its own children from the data's child list (+0x1C on)
template <class P> void Game<P>::body_part_alloc(uint32_t parent, uint32_t data, int32_t index)
{
    const uint32_t part = mem_alloc(0xD8);
    mem.set32(mem.at<BodyPart>(parent).children.a + 4 * uint32_t(index), part);
    mem.at<BodyPart>(part).index = index;
    mem.at<uint8_t>(parent + 0x28 + uint32_t(index)) = 0;
    mem.at<BodyPart>(part).parent.a = parent;
    part_init(part, parent, data);
    for (int32_t i = 0; i < mem.at<BodyPart>(part).child_count; ++i)
        body_part_alloc(part, mem.u32(data + 0x1C + 4 * uint32_t(i)), i);
}

// 0x38BD0 body_init (obj, type, motion): the root node and the part tree from the type's header (creature_type_headers:
// +0x14 role, +0x18 child count, +0x1C the children's part data), the motion fields, the five pose buffers, the flags;
// the first free entry of player_bodies (14) gets the object (its slot); the type's skin template
template <class P> void Game<P>::body_init(uint32_t obj, int32_t type, uint32_t motion)
{
    const uint32_t header = mem.u32(G::creature_type_headers + 4 * uint32_t(type));
    const uint32_t root = mem_alloc(0xD8);
    mem.set32(G::body_current, obj);
    const int16_t m = int16_t(motion);
    of<int16_t>(obj, 0x100) = m;
    of<int16_t>(obj, 0xD2) = m;
    of<int16_t>(obj, 0xFE) = m;
    of<int16_t>(obj, 0xD0) = m;
    of<int16_t>(obj, 0xCC) = int16_t(type);
    of<uint32_t>(obj, 0xC8) = root;
    BodyPart& r = mem.at<BodyPart>(root);
    r.owner.a = obj;
    r.child_count = mem.at<int32_t>(header + 0x18);
    r.role = mem.at<int32_t>(header + 0x14);
    const uint32_t children = mem_alloc(uint32_t(mem.at<BodyPart>(root).child_count) << 2);
    mem.at<BodyPart>(root).children.a = children;
    of<uint32_t>(obj, 0xF8) = 0;
    of<int16_t>(obj, 0xF4) = 0;
    of<uint32_t>(obj, 0x148) = 0;
    of<uint32_t>(obj, 0x144) = 0;
    of<uint8_t>(obj, 0x3C6) = 0;
    of<uint32_t>(obj, 0x390) = 0;
    of<int16_t>(obj, 0x2E0) = 0xFF;
    of<int8_t>(obj, 0x2EB) = -1;
    of<uint32_t>(obj, 0x11C) = F::body_part_draw_hook;
    const int16_t t = of<int16_t>(obj, 0xCC);
    of<uint8_t>(obj, 0x3C7) &= 0xFD;
    of<uint8_t>(obj, 0x3C4) &= 0x1E;
    of<uint8_t>(obj, 0x3C5) &= 0x84;
    const bool skin = mem.at<int16_t>(cr::skin_numbers + 2 * uint32_t(int32_t(t))) != -1;
    of<uint8_t>(obj, 0x3C4) = uint8_t((skin ? 0x10 : 0) | (of<uint8_t>(obj, 0x3C4) & 0xEF));
    for (uint32_t i = 0; i < 9; ++i)
        of<uint32_t>(obj, 0x120 + 4 * i) = 0;
    const uint32_t size = ((mem.u32(cr::joints + 4 * uint32_t(int32_t(t))) * 0x30 + 0x7F) >> 5) << 2;
    of<uint32_t>(obj, 0xDC) = mem_alloc(size);
    of<uint32_t>(obj, 0xE0) = mem_alloc(size);
    of<uint32_t>(obj, 0xE4) = mem_alloc(size);
    of<uint32_t>(obj, 0x114) = mem_alloc(size);
    of<uint32_t>(obj, 0x118) = mem_alloc(size);
    const uint32_t fd = mem.u32(cr::motion_data + 4 * uint32_t(int32_t(of<int16_t>(obj, 0xD0))));
    of<float>(obj, 0x94) = 1.0f;
    of<uint32_t>(obj, 0xD8) = fd;
    of<uint32_t>(obj, 0x110) = fd;
    if (pm_body() && !pm_motion_tbl.empty()) {   // THOTD.EXE body_init 0x478D4C: the table entry's address (pmbody)
        of<uint32_t>(obj, 0xD8) = cr::motion_data + 4 * uint32_t(int32_t(of<int16_t>(obj, 0xD0)));
        of<uint32_t>(obj, 0x110) = of<uint32_t>(obj, 0xD8);
    }
    mem.at<BodyPart>(root).depth = 0;
    for (int32_t i = 0; i < mem.at<BodyPart>(root).child_count; ++i) {
        const uint32_t data = mem.u32(header + 0x1C + 4 * uint32_t(i));
        const uint32_t part = mem_alloc(0xD8);
        mem.set32(mem.at<BodyPart>(root).children.a + 4 * uint32_t(i), part);
        mem.at<BodyPart>(part).index = i;
        mem.at<uint8_t>(root + 0x28 + uint32_t(i)) = 0;
        mem.at<BodyPart>(part).parent.a = root;
        part_init(part, root, data);
        for (int32_t k = 0; k < mem.at<BodyPart>(part).child_count; ++k)
            body_part_alloc(part, mem.u32(data + 0x1C + 4 * uint32_t(k)), k);
    }
    bool placed = false;
    for (uint32_t i = 0; i < 14; ++i) {
        if (mem.u32(G::player_bodies + 4 * i) == 0) {
            mem.set32(G::player_bodies + 4 * i, obj);
            of<int8_t>(obj, 0x2E6) = int8_t(i);
            placed = true;
            break;
        }
    }
    // Survival Mode only (extraBodySlots, game.h): the 14 taken, a slot from 16 on
    for (uint32_t j = 0; !placed && j < uint32_t(extraBodySlots); ++j)
        if (mem.u32(extraBodyTable + 4 * j) == 0) {
            mem.set32(extraBodyTable + 4 * j, obj);
            of<int8_t>(obj, 0x2E6) = int8_t(16 + j);
            placed = true;
        }
    if (mem.at<int16_t>(cr::skin_numbers + 2 * uint32_t(int32_t(of<int16_t>(obj, 0xCC)))) != -1)
        skin_template_copy(obj);
    if (of<int16_t>(obj, 0xCC) == 0x22)
        sophie_skin_template_copy(obj);
}

// 0x211F0 skin_template_copy (body): the type's skin template (main_data, 0x640 bytes a skin) into the slot's skin
// buffer, polygons x 10 + 7 words
template <class P> void Game<P>::skin_template_copy(uint32_t obj)
{
    const int32_t skin = mem.at<int16_t>(cr::skin_numbers + 2 * uint32_t(int32_t(of<int16_t>(obj, 0xCC))));
    const uint32_t to = uint32_t(int32_t(of<int8_t>(obj, 0x2E6))) * 0x348 + mem.u32(cr::skin_buffers) + 0xC;
    const uint32_t from = cr::skin_templates + uint32_t(skin) * 0x640;
    const uint32_t n = mem.u32(cr::skin_polygons + 4 * uint32_t(skin)) * 10 + 7;
    for (uint32_t i = 0; i < n; ++i)
        mem.set32(to + 4 * i, mem.u32(from + 4 * i));
}

// 0x21800 sophie_skin_template_copy (body): Sophie's two strips: 27 words into the second buffer, 3 constants and
// 44 words into the third
template <class P> void Game<P>::sophie_skin_template_copy(uint32_t obj)
{
    const uint32_t slot = uint32_t(int32_t(of<int8_t>(obj, 0x2E6))) * 0x348;
    const uint32_t a = slot + mem.u32(cr::skin_buffers + 4) + 0xC;
    for (uint32_t i = 0; i < 27; ++i)
        mem.set32(a + 4 * i, mem.u32(cr::sophie_strips + 4 * i));
    const uint32_t b = slot + mem.u32(cr::skin_buffers + 8);
    mem.set32(b + 0xC, 0x3DAC79DC);
    mem.set32(b + 0x10, 0xBF540CD0);
    mem.set32(b + 0x14, 0xBED34DBB);
    for (uint32_t i = 0; i < 44; ++i)
        mem.set32(b + 0x18 + 4 * i, mem.u32(cr::sophie_strips + 0x7C + 4 * i));
}

// 0x32C70 obj_clear_37c: the four extra block pointers (+0x37C..+0x388)
template <class P> void Game<P>::obj_clear_37c(uint32_t obj)
{
    for (uint32_t i = 0; i < 4; ++i)
        of<uint32_t>(obj, 0x37C + 4 * i) = 0;
}

// 0x67C90 creature_part_types_set (part): the part's armour from the role table (not for model 0), then its children
template <class P> void Game<P>::creature_part_types_set(uint32_t part)
{
    BodyPart& p = mem.at<BodyPart>(part);
    if (p.part_id != 0)
        p.armour = mem.at<PartRole>(G::part_roles + (uint32_t(p.role) + uint32_t(mem.at<int32_t>(G::hit_type)) * 0x1F) * 8).armour;
    for (uint32_t i = 0; i < uint32_t(mem.at<BodyPart>(part).child_count); ++i)
        creature_part_types_set(mem.u32(mem.at<BodyPart>(part).children.a + 4 * i));
}

// 0x67CF0 creature_type_setup (obj), after body_init with hit_type = the type: scale (part_init_p) and hit radius by
// type, target / attack slot -1, draw param 1, flags2 3, the per-kind bytes cleared, draw flags, the extra blocks, then
// every part's armour (creature_part_types_set's body inline for the first level)
template <class P> void Game<P>::creature_type_setup(uint32_t obj)
{
    const int32_t type = mem.at<int32_t>(G::hit_type);
    const uint32_t root = of<uint32_t>(obj, 0xC8);
    mem.set32(G::body_current, obj);
    of<float>(obj, 0x94) = mem.f32(cr::type_scale + 4 * uint32_t(type));
    const float radius = mem.f32(cr::type_radius + 4 * uint32_t(type));
    of<int8_t>(obj, 0x2E8) = -1;
    of<int16_t>(obj, 0x6A) = 1;
    of<uint16_t>(obj, 0x6C) = 3;
    of<int8_t>(obj, 0x2E7) = -1;
    of<uint8_t>(obj, 0x2E4) = 0;
    of<uint8_t>(obj, 0x2E5) = 0;
    of<int16_t>(obj, 0x68) = 0;
    of<float>(obj, 0x90) = radius;
    of<int16_t>(obj, 0x6E) = 0;
    of<uint8_t>(obj, 0x3C4) = uint8_t((of<uint8_t>(obj, 0x3C4) & 0xFB) | 8);
    of<uint8_t>(obj, 0x3C7) |= 8;
    obj_clear_37c(obj);
    const BodyPart& r = mem.at<BodyPart>(root);
    for (uint32_t i = 0; i < uint32_t(r.child_count); ++i)
        creature_part_types_set(mem.u32(r.children.a + 4 * i));
}

// ---------------------------------------------------------------------------------------------------------------
// motions

// 0x37FF0 body_pose_copy (obj, from, to): a frame record (root and the type's joints)
template <class P> void Game<P>::body_pose_copy(uint32_t obj, uint32_t from, uint32_t to)
{
    if (pm_body() && !pm_motion_tbl.empty() && from >= cr::motion_data &&
        from - cr::motion_data + 0xC + uint32_t(body_joints(obj)) * 6 <= pm_motion_tbl.size()) {
        // PC Mode: frame_data still at the PC's body_init value (&motion table[motion]): THOTD.EXE copies its table's
        // pointer words (pmbody)
        const uint8_t* src = pm_motion_tbl.data() + (from - cr::motion_data);
        for (uint32_t k = 0; k < 0xC + uint32_t(body_joints(obj)) * 6; ++k)
            mem.at<uint8_t>(to + k) = src[k];
        return;
    }
    for (uint32_t k = 0; k < 3; ++k)
        mem.set32(to + 4 * k, mem.u32(from + 4 * k));
    for (int32_t i = 0; i < body_joints(obj) * 3; ++i)
        mem.at<int16_t>(to + 0xC + uint32_t(i) * 2) = mem.at<int16_t>(from + 0xC + uint32_t(i) * 2);
}

// 0x38060 body_pose_set_rec (part, pose): the part's joint turned by its angles; a mirrored part (side flags bit 4)
// gets the angles of the accumulated matrix instead (coprocessor 0x71); then its children
template <class P> void Game<P>::body_pose_set_rec(uint32_t part, uint32_t pose)
{
    Tgp::Out none;
    copro(0x05, nullptr, 0, none);
    const uint32_t rec = pose + uint32_t(mem.at<BodyPart>(part).role) * 6;
    uint32_t a[3] = {0u - mem.at<uint16_t>(rec + 0x10), mem.at<uint16_t>(rec + 0xE), mem.at<uint16_t>(rec + 0xC)};
    copro(0x86, a, 3, none);
    if ((mem.at<BodyPart>(part).side_flags >> 4) & 1) {
        Tgp::Out o;
        copro(0x71, nullptr, 0, o);
        mem.at<int16_t>(rec + 0xC) = int16_t(o.w[2]);
        mem.at<int16_t>(rec + 0xE) = int16_t(o.w[1]);
        mem.at<int16_t>(rec + 0x10) = int16_t(0u - o.w[0]);
    }
    for (int32_t i = 0; i < mem.at<BodyPart>(part).child_count; ++i)
        body_pose_set_rec(mem.u32(mem.at<BodyPart>(part).children.a + 4 * uint32_t(i)), pose);
    copro(0x06, nullptr, 0, none);
}

// the mirror step of set_motion_blend / set_motion_at_frame: from a mirrored motion to a plain one (or back), the
// pose that changes side is re-angled through the part tree; motion_flags2 bit 4 says so
template <class P> void Game<P>::motion_mirror(uint32_t obj, int32_t oldMotion, int32_t newMotion, uint32_t from, uint32_t to)
{
    const bool was = mem.at<uint8_t>(cr::mirrored + uint32_t(oldMotion)) != 0, is = mem.at<uint8_t>(cr::mirrored + uint32_t(newMotion)) != 0;
    if (was == is) {
        of<uint8_t>(obj, 0x3C6) &= uint8_t(~0x10);
        return;
    }
    const uint32_t pose = was ? to : from;
    const uint32_t root = of<uint32_t>(obj, 0xC8);
    Tgp::Out none;
    copro(0x21, nullptr, 0, none);
    const uint32_t rec = pose + uint32_t(mem.at<BodyPart>(root).role) * 6;
    uint32_t a[3] = {0u - mem.at<uint16_t>(rec + 0x10), mem.at<uint16_t>(rec + 0xE), mem.at<uint16_t>(rec + 0xC)};
    copro(0x86, a, 3, none);
    for (int32_t i = 0; i < mem.at<BodyPart>(root).child_count; ++i)
        body_pose_set_rec(mem.u32(mem.at<BodyPart>(root).children.a + 4 * uint32_t(i)), pose);
    copro(0x06, nullptr, 0, none);
    of<uint8_t>(obj, 0x3C6) |= 0x10;
}

// 0x381D0 set_motion_blend (obj, motion, start frame, blend frames) and 0x38490 set_motion_at_frame (the same, but the
// target keeps playing during the blend: motion_flags2 bit 3): the current pose to blend_from, the target frame to
// blend_to, the frame counters restarted
template <class P> void Game<P>::motion_blend_start(uint32_t obj, uint32_t motion, int start, int frames, bool atFrame)
{
    const int32_t m = int32_t(int16_t(motion));
    const int32_t n = int32_t(mem.u32(G::motion_frame_counts + 4 * uint32_t(m)));
    of<int16_t>(obj, 0xD4) = int16_t(n ? start % n : 0);
    const uint32_t from = of<uint32_t>(obj, 0xE0), to = of<uint32_t>(obj, 0xE4);
    body_pose_copy(obj, of<uint32_t>(obj, 0xD8), from);
    body_pose_copy(obj, motion_frame_record(obj, m, of<int16_t>(obj, 0xD4)), to);
    const int16_t old = of<int16_t>(obj, 0xD0);
    motion_mirror(obj, old, m, from, to);
    uint8_t f2 = of<uint8_t>(obj, 0x3C6);
    of<int16_t>(obj, 0xD0) = int16_t(motion);
    f2 = atFrame ? uint8_t(f2 | 0x0A) : uint8_t((f2 | 0x02) & ~0x08);
    of<uint8_t>(obj, 0x3C6) = f2;
    of<int16_t>(obj, 0xD2) = old;
    of<int16_t>(obj, 0xD6) = int16_t(frames != 0 ? frames : 1);
    of<uint32_t>(obj, 0xF8) = 1;
    of<int16_t>(obj, 0xF6) = 0;
    of<int16_t>(obj, 0xF4) = 1;
    of<uint32_t>(obj, 0xE8) = 0;
    of<uint8_t>(obj, 0x3C5) &= 0xAF;
    of<uint8_t>(obj, 0x3C7) &= uint8_t(~0x02);
}

template <class P> void Game<P>::set_motion_blend(uint32_t obj, uint32_t motion, int a, int frames) { motion_blend_start(obj, motion, a, frames, false); }
template <class P> void Game<P>::set_motion_at_frame(uint32_t obj, uint32_t motion, int a, int frames) { motion_blend_start(obj, motion, a, frames, true); }

// 0x38750 turn_motion_set_speed (obj, motion, start, frames, angle): set_motion_blend, the turn spread over the frames
template <class P> void Game<P>::turn_motion_set_speed(uint32_t obj, uint32_t motion, int a, int frames, uint32_t angle)
{
    set_motion_blend(obj, motion, a, frames);
    of<uint32_t>(obj, 0xE8) = frames != 0 ? uint32_t(int32_t(angle) / frames) : angle;
}

// 0x39180 set_motion: straight to frame 0 of the motion (no blend)
template <class P> void Game<P>::set_motion(uint32_t obj, uint32_t motion)
{
    const int16_t old = of<int16_t>(obj, 0xD0);
    of<int16_t>(obj, 0xD0) = int16_t(motion);
    of<uint32_t>(obj, 0xF8) = 0;
    of<int16_t>(obj, 0xF4) = 0;
    of<int16_t>(obj, 0xF6) = 0;
    of<int16_t>(obj, 0xD2) = old;
    of<uint8_t>(obj, 0x3C5) &= uint8_t(~0x10);
    of<uint8_t>(obj, 0x3C6) &= uint8_t(~0x02);
    of<uint8_t>(obj, 0x3C7) &= uint8_t(~0x02);
}

// 0x391C0 set_motion2: the second motion from its frame 0
template <class P> void Game<P>::set_motion2(uint32_t obj, uint32_t motion)
{
    const int16_t old = of<int16_t>(obj, 0xFE);
    of<int16_t>(obj, 0xFE) = int16_t(motion);
    of<uint32_t>(obj, 0x10C) = 0;
    of<int16_t>(obj, 0x108) = 0;
    of<int16_t>(obj, 0x10A) = 0;
    of<int16_t>(obj, 0x100) = old;
    of<uint8_t>(obj, 0x3C5) &= uint8_t(~0x20);
    of<uint8_t>(obj, 0x3C6) = uint8_t((of<uint8_t>(obj, 0x3C6) | 0x20) & 0xBB);
    of<uint8_t>(obj, 0x3C7) &= 0xFC;
}

// 0x388A0 set_motion2_at_frame (obj, motion, start, frames): the second motion blended in from the current pose4
template <class P> void Game<P>::set_motion2_at_frame(uint32_t obj, uint32_t motion, int start, int frames)
{
    const int16_t old = of<int16_t>(obj, 0xFE);
    of<int16_t>(obj, 0xFE) = int16_t(motion);
    of<uint8_t>(obj, 0x3C6) |= 0x04;
    of<int16_t>(obj, 0x100) = old;
    const int32_t m = int32_t(int16_t(motion));
    const int32_t n = int32_t(mem.u32(G::motion_frame_counts + 4 * uint32_t(m)));
    of<int16_t>(obj, 0x102) = int16_t(n ? start % n : 0);
    of<int16_t>(obj, 0x104) = int16_t(frames != 0 ? frames : 1);
    of<uint32_t>(obj, 0x10C) = 0;
    of<int16_t>(obj, 0x108) = 0;
    of<int16_t>(obj, 0x10A) = 0;
    of<uint8_t>(obj, 0x3C5) &= 0x5F;
    of<uint8_t>(obj, 0x3C7) &= 0xFC;
    const uint8_t f2 = uint8_t(of<uint8_t>(obj, 0x3C6) & ~0x40);
    of<uint8_t>(obj, 0x3C6) = f2;
    if ((f2 >> 5) & 1) {
        body_pose_copy(obj, of<uint32_t>(obj, 0x110), of<uint32_t>(obj, 0x118));
        return;
    }
    of<uint32_t>(obj, 0x110) = of<uint32_t>(obj, 0xD8);
    body_pose_copy(obj, of<uint32_t>(obj, 0xD8), of<uint32_t>(obj, 0x118));
    of<uint8_t>(obj, 0x3C6) |= 0x20;
}

// 0x38A20 set_motion_blend_hold (obj, motion, start, frames, hold): set_motion2_at_frame, held for `hold` frames
template <class P> void Game<P>::set_motion_blend_hold(uint32_t obj, uint32_t motion, int a, int b, int frames)
{
    set_motion2_at_frame(obj, motion, a, b);
    of<int16_t>(obj, 0x106) = int16_t(frames != 0 ? frames : 1);
    of<uint8_t>(obj, 0x3C7) |= 0x01;
}

// 0x39220 body_second_motion (obj, frames): the second motion's split end (0 = off)
template <class P> void Game<P>::body_second_motion(uint32_t obj, int n)
{
    if (!((of<uint8_t>(obj, 0x3C6) >> 5) & 1))
        return;
    if (n == 0) {
        of<uint8_t>(obj, 0x3C6) &= 0x9F;
        of<uint8_t>(obj, 0x3C5) &= 0x7F;
        of<uint8_t>(obj, 0x3C7) &= uint8_t(~0x01);
        return;
    }
    of<int16_t>(obj, 0x106) = int16_t(n);
    of<uint32_t>(obj, 0x10C) = 0;
    of<int16_t>(obj, 0x108) = 0;
    of<int16_t>(obj, 0x10A) = 0;
    of<uint8_t>(obj, 0x3C6) = uint8_t((of<uint8_t>(obj, 0x3C6) & 0x3B) | 0x44);
    of<uint8_t>(obj, 0x3C5) &= 0x5F;
    of<uint8_t>(obj, 0x3C7) &= 0xFC;
    body_pose_copy(obj, of<uint32_t>(obj, 0x110), of<uint32_t>(obj, 0x118));
}

// 0x39330 set_motion2_blend (obj, frames): the main motion blends from the current pose over `frames` (0 = off)
template <class P> void Game<P>::set_motion2_blend(uint32_t obj, int n)
{
    if (!((of<uint8_t>(obj, 0x3C6) >> 5) & 1))
        return;
    if (n == 0) {
        of<uint8_t>(obj, 0x3C6) &= 0x5F;
        of<uint8_t>(obj, 0x3C5) &= uint8_t(~0x40);
        of<uint8_t>(obj, 0x3C7) &= uint8_t(~0x01);
        return;
    }
    of<int16_t>(obj, 0x106) = int16_t(n);
    of<uint32_t>(obj, 0xF8) = 0;
    of<int16_t>(obj, 0xF4) = 0;
    of<int16_t>(obj, 0xF6) = 0;
    of<uint8_t>(obj, 0x3C6) = uint8_t((of<uint8_t>(obj, 0x3C6) & ~0x40) | 0x82);
    of<uint8_t>(obj, 0x3C5) &= 0xAF;
    of<uint8_t>(obj, 0x3C7) &= 0xFC;
    body_pose_copy(obj, of<uint32_t>(obj, 0xD8), of<uint32_t>(obj, 0xE0));
}

// 0x368F0 body_motion2_frame_flags: the frame (and the second motion's) from the frames played, and the Stay flags
// (motion_flags3 bit 1 / bit 2: the frame did not change)
template <class P> void Game<P>::body_motion2_frame_flags(uint32_t obj)
{
    const uint8_t f2 = of<uint8_t>(obj, 0x3C6);
    if ((f2 >> 5) & 1) {
        uint32_t n;
        if ((f2 >> 2) & 1)
            n = uint32_t(int32_t(of<int16_t>(obj, (f2 >> 6) & 1 ? 0x106 : 0x104)) + 1);
        else
            n = mem.u32(G::motion_frame_counts + 4 * uint32_t(int32_t(of<int16_t>(obj, 0xFE))));
        const uint32_t t = of<uint32_t>(obj, 0x10C);
        const uint32_t r = n ? t % n : 0;
        if (int32_t(n) > 1) {
            const uint8_t eq = int32_t(of<int16_t>(obj, 0x108)) == int32_t(r) ? 1 : 0;
            of<uint8_t>(obj, 0x3C7) = uint8_t((of<uint8_t>(obj, 0x3C7) & ~0x04) | (eq << 2));
        }
        of<int16_t>(obj, 0x108) = int16_t(r);
        of<int16_t>(obj, 0x10A) = int16_t(n ? t / n : 0);
    }
    uint32_t n;
    if ((f2 & 0xA0) == 0xA0)
        n = uint32_t(int32_t(of<int16_t>(obj, 0x106)) + 1);
    else if ((f2 >> 1) & 1)
        n = uint32_t(int32_t(of<int16_t>(obj, 0xD6)) + 1);
    else
        n = mem.u32(G::motion_frame_counts + 4 * uint32_t(int32_t(of<int16_t>(obj, 0xD0))));
    const uint32_t t = of<uint32_t>(obj, 0xF8);
    const uint32_t r = n ? t % n : 0;
    if (int32_t(n) > 1) {
        const uint8_t eq = int32_t(of<int16_t>(obj, 0xF4)) == int32_t(r) ? 1 : 0;
        of<uint8_t>(obj, 0x3C7) = uint8_t((of<uint8_t>(obj, 0x3C7) & ~0x02) | (eq << 1));
    }
    of<int16_t>(obj, 0xF4) = int16_t(r);
    of<int16_t>(obj, 0xF6) = int16_t(n ? t / n : 0);
}

// 0x369E0 body_motion_advance: frame 0 skipped for motions with the end flags; the end of a blend (motion_flags bit 6)
// moves to the target's frame (or, with the second motion split, back to the second motion); the second motion's
// split start / loop
template <class P> void Game<P>::body_motion_advance(uint32_t obj)
{
    uint8_t mf = of<uint8_t>(obj, 0x3C5);
    if ((mf & 0x50) != 0 && !((of<uint8_t>(obj, 0x3C7) >> 1) & 1)) {
        mf &= uint8_t(~0x10);
        if (of<int16_t>(obj, 0xF4) == 0) {
            of<int16_t>(obj, 0xF4) = int16_t(of<int16_t>(obj, 0xF4) + 1);
            of<uint32_t>(obj, 0xF8) += 1;
        }
    }
    of<uint8_t>(obj, 0x3C5) = mf;
    uint8_t f2 = of<uint8_t>(obj, 0x3C6);
    if ((of<uint32_t>(obj, 0x3C4) & 0x2004000u) == 0x4000u) {
        const int16_t bf = of<int16_t>(obj, 0xD4);
        f2 &= 0xE5;
        of<int16_t>(obj, 0xF6) = 0;
        of<int16_t>(obj, 0xF4) = bf;
        of<uint32_t>(obj, 0xF8) = uint32_t(int32_t(bf));
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~0x40);
        if ((f2 & 0xA0) == 0xA0) {
            of<uint32_t>(obj, 0xF8) = of<uint32_t>(obj, 0x10C);
            of<int16_t>(obj, 0xF4) = of<int16_t>(obj, 0x108);
            of<int16_t>(obj, 0xD0) = of<int16_t>(obj, 0xFE);
            of<int16_t>(obj, 0xF6) = of<int16_t>(obj, 0x10A);
            f2 &= 0x5F;
            const uint8_t f3 = of<uint8_t>(obj, 0x3C7);
            of<uint8_t>(obj, 0x3C7) = uint8_t((f3 & ~0x02) | ((f3 >> 1) & 0x02));
        }
    }
    of<uint8_t>(obj, 0x3C6) = f2;
    if (!((f2 >> 5) & 1))
        return;
    if ((of<uint32_t>(obj, 0x3C4) & 0x4002000u) == 0x2000u) {
        of<uint8_t>(obj, 0x3C5) &= uint8_t(~0x20);
        uint16_t p = of<uint16_t>(obj, 0x108);
        if (p == 0) {
            of<uint32_t>(obj, 0x10C) += 1;
            p = 1;
        }
        of<uint16_t>(obj, 0x108) = p;
        if (of<uint8_t>(obj, 0x3C7) & 1)
            body_second_motion(obj, of<int16_t>(obj, 0x106));
    }
    if ((of<uint32_t>(obj, 0x3C4) & 0x4008000u) == 0x8000u) {
        const int16_t s = of<int16_t>(obj, 0x102);
        uint8_t g = of<uint8_t>(obj, 0x3C6);
        of<int16_t>(obj, 0x10A) = 0;
        of<int16_t>(obj, 0x108) = s;
        g &= uint8_t(~0x04);
        of<uint32_t>(obj, 0x10C) = uint32_t(int32_t(s));
        if ((g >> 6) & 1)
            g &= 0x9F;
        of<uint8_t>(obj, 0x3C6) = g;
        of<uint8_t>(obj, 0x3C5) &= 0x7F;
    }
}

// 0x36B80 body_motion2_advance (after drawing, the end flags): MotFinish (motion_flags bit 4) when the next frame
// starts another loop of the motion, SplFinish (bit 6) when a blend has run its frames; the second motion's
// MotFinishUp / SplFinishUp (bits 5 / 7)
template <class P> void Game<P>::body_motion2_advance(uint32_t obj)
{
    const uint8_t f2 = of<uint8_t>(obj, 0x3C6);
    if ((f2 >> 5) & 1) {
        uint8_t mf = of<uint8_t>(obj, 0x3C5);
        const uint32_t sub = uint32_t(int32_t(of<int16_t>(obj, 0x10A)));
        const uint32_t t = of<uint32_t>(obj, 0x10C);
        if ((f2 >> 2) & 1) {
            const uint32_t n = uint32_t(int32_t(of<int16_t>(obj, (f2 >> 6) & 1 ? 0x106 : 0x104)));
            mf = uint8_t((mf & 0x7F) | ((n && sub < t / n) ? 0x80 : 0));
        } else {
            const uint32_t n = mem.u32(G::motion_frame_counts + 4 * uint32_t(int32_t(of<int16_t>(obj, 0xFE))));
            mf = uint8_t((mf & ~0x20) | ((n && sub < (t + 1) / n) ? 0x20 : 0));
        }
        of<uint8_t>(obj, 0x3C5) = mf;
    }
    uint8_t mf = of<uint8_t>(obj, 0x3C5);
    const uint32_t sub = uint32_t(int32_t(of<int16_t>(obj, 0xF6)));
    const uint32_t t = of<uint32_t>(obj, 0xF8);
    if ((f2 >> 1) & 1) {
        const uint32_t n = uint32_t(int32_t(of<int16_t>(obj, (f2 >> 7) & 1 ? 0x106 : 0xD6)));
        mf = uint8_t((mf & ~0x40) | ((n && sub < t / n) ? 0x40 : 0));
    } else {
        const uint32_t n = mem.u32(G::motion_frame_counts + 4 * uint32_t(int32_t(of<int16_t>(obj, 0xD0))));
        mf = uint8_t((mf & ~0x10) | ((n && sub < (t + 1) / n) ? 0x10 : 0));
    }
    of<uint8_t>(obj, 0x3C5) = mf;
}

// 0x36CA0 body_pose: frame_data = the frame to draw: the motion's record, or a blend into pose2 (blend_from toward
// blend_to, or toward the second motion's frame when it takes over: motion_flags2 0xA0); then the second motion's data
// (motion2_data), blended into pose3 while it starts (bit 2) or splits (bit 6)
template <class P> void Game<P>::body_pose(uint32_t obj)
{
    const uint8_t f2 = of<uint8_t>(obj, 0x3C6);
    const uint32_t pose2 = of<uint32_t>(obj, 0xDC);
    if ((f2 & 0xA0) == 0xA0) {
        const int32_t span = of<int16_t>(obj, 0x106), fr = of<int16_t>(obj, 0xF4);
        of<uint32_t>(obj, 0x110) = motion_frame_record(obj, of<int16_t>(obj, 0xFE), of<int16_t>(obj, 0x108));
        pose_blend_root(of<uint32_t>(obj, 0xE0), of<uint32_t>(obj, 0x110), pose2, fr, span - fr);
        pose_blend_joints(obj, of<uint32_t>(obj, 0xE0), of<uint32_t>(obj, 0x110), pose2, span ? (fr << 16) / span : 0);
        of<uint32_t>(obj, 0xD8) = pose2;
    } else if ((f2 >> 1) & 1) {
        const int32_t span = of<int16_t>(obj, 0xD6), fr = of<int16_t>(obj, 0xF4);
        of<uint32_t>(obj, 0x30) += of<uint32_t>(obj, 0xE8);
        if ((f2 >> 3) & 1) {
            const int16_t m = of<int16_t>(obj, 0xD0);
            body_pose_copy(obj, motion_frame_record(obj, m, of<int16_t>(obj, 0xD4)), of<uint32_t>(obj, 0xE4));
            const int32_t n = int32_t(mem.u32(G::motion_frame_counts + 4 * uint32_t(int32_t(m))));
            int16_t bf = int16_t(of<uint16_t>(obj, 0xD4) + 1);
            bf = int16_t(n ? int32_t(bf) % n : 0);
            of<int16_t>(obj, 0xD4) = bf == 0 ? int16_t(1) : bf;
        }
        pose_blend_root(of<uint32_t>(obj, 0xE0), of<uint32_t>(obj, 0xE4), pose2, fr, span - fr);
        pose_blend_joints(obj, of<uint32_t>(obj, 0xE0), of<uint32_t>(obj, 0xE4), pose2, span ? (fr << 16) / span : 0);
        of<uint32_t>(obj, 0xD8) = pose2;
    } else {
        of<uint32_t>(obj, 0xD8) = motion_frame_record(obj, of<int16_t>(obj, 0xD0), of<int16_t>(obj, 0xF4));
    }
    const uint8_t g = of<uint8_t>(obj, 0x3C6);
    if (!((g >> 5) & 1))
        return;
    if ((g >> 6) & 1) {
        const int32_t span = of<int16_t>(obj, 0x106), fr = of<int16_t>(obj, 0x108);
        pose_blend_joints(obj, of<uint32_t>(obj, 0x118), of<uint32_t>(obj, 0xD8), of<uint32_t>(obj, 0x114), span ? (fr << 16) / span : 0);
        of<uint32_t>(obj, 0x110) = of<uint32_t>(obj, 0x114);
    } else if ((g >> 2) & 1) {
        const int32_t span = of<int16_t>(obj, 0x104), fr = of<int16_t>(obj, 0x108);
        const uint32_t to = motion_frame_record(obj, of<int16_t>(obj, 0xFE), of<int16_t>(obj, 0x102));
        pose_blend_joints(obj, of<uint32_t>(obj, 0x118), to, of<uint32_t>(obj, 0x114), span ? (fr << 16) / span : 0);
        of<uint32_t>(obj, 0x110) = of<uint32_t>(obj, 0x114);
    } else {
        of<uint32_t>(obj, 0x110) = motion_frame_record(obj, of<int16_t>(obj, 0xFE), of<int16_t>(obj, 0x108));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// drawing

// 0x67DF0 object_draw: the light of the draw param (0x520A40, 3 words to the geometrizer), the draw mode's routine
// (draw_modes) under a pushed matrix, then the object's home position in camera space (view_pos, the shot test's
// sphere) and the home raised by the type's offset; with at most one enemy left its home is remembered; the default
// light (record 0) again
template <class P> void Game<P>::object_draw(uint32_t obj)
{
    auto light = [&](uint32_t rec) {
        hw_write32(0x8000A0, 0xA0A);
        hw_write32(0x804000, mem.u32(rec));
        hw_write32(0x804000, mem.u32(rec + 4));
        hw_write32(0x804000, mem.u32(rec + 8));
    };
    light(cr::light_records + uint32_t(int32_t(of<int16_t>(obj, 0x6A))) * 12);
    Tgp::Out none;
    copro(0x05, nullptr, 0, none);
    const bool pmb = pm_body();   // PC Mode: the PC's transform too (THOTD.EXE object_draw 0x4C3B80, pmbody)
    if (pmb)
        pc_matrix_push();
    const uint32_t routine = mem.u32(cr::draw_modes + 4 * uint32_t(int32_t(of<int16_t>(obj, 0x68))));
    switch (routine) {
    case F::body_draw: body_draw(obj); break;
    case F::object_draw_model: object_draw_model(obj); break;
    default: call_routine(routine, obj); break;
    }
    uint32_t in[3] = {of<uint32_t>(obj, 0x2D4), of<uint32_t>(obj, 0x2D8), of<uint32_t>(obj, 0x2DC)};
    copro(0x06, nullptr, 0, none);
    Tgp::Out o;
    copro(0x1A, in, 3, o);
    if (pmb) {   // the home through the PC's current matrix (the camera's inverse; the file's transform 0x4C1B60)
        pcm_pop();
        float v[3];
        pm_cur().transform(ffrom(in[0]), ffrom(in[1]), ffrom(in[2]), v[0], v[1], v[2]);
        for (int k = 0; k < 3; ++k)
            o.w[k] = fbits(v[k]);
    }
    of<uint32_t>(obj, 0x38) = o.w[0];
    of<uint32_t>(obj, 0x3C) = o.w[1];
    const uint16_t type = of<uint16_t>(obj, 0xCC);
    of<uint32_t>(obj, 0x40) = o.w[2];
    if (type != 0xFFFF) {
        const uint32_t table = (of<uint8_t>(obj, 0x3C7) >> 3) & 1 ? cr::home_raise_b : cr::home_raise_a;
        of<float>(obj, 0x2D8) = of<float>(obj, 0x2D8) + mem.f32(table + 4 * uint32_t(type));
    }
    if (mem.at<int16_t>(G::enemies_alive) <= 1 && !((of<uint32_t>(obj, 0x58) >> 20) & 1)) {
        mem.set32(cr::last_enemy_home, of<uint32_t>(obj, 0x2D4));
        mem.set32(cr::last_enemy_home + 4, of<uint32_t>(obj, 0x2D8));
        mem.set32(cr::last_enemy_home + 8, of<uint32_t>(obj, 0x2DC));
    }
    light(cr::light_records);
}

// 0x67F60 object_draw_model (draw mode 2): T(pos) R(z, y, x) S(scale), the object's model; with flags2 bit 1 the
// model again under stored matrix 3 (the shadow projection)
template <class P> void Game<P>::object_draw_model(uint32_t obj)
{
    Tgp::Out none;
    uint32_t t[3] = {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)};
    copro(0x12, t, 3, none);
    uint32_t r[3] = {of<uint32_t>(obj, 0x34), of<uint32_t>(obj, 0x30), of<uint32_t>(obj, 0x2C)};
    copro(0x86, r, 3, none);
    const uint32_t s = of<uint32_t>(obj, 0x94);
    uint32_t sc[3] = {s, s, s};
    copro(0x13, sc, 3, none);
    geo_draw_model(of<uint32_t>(obj, 0x54));
    if ((of<uint8_t>(obj, 0x6C) >> 1) & 1) {
        copro(0x05, nullptr, 0, none);
        uint32_t three[1] = {3};
        copro(0x77, three, 1, none);
        geo_draw_model(of<uint32_t>(obj, 0x54));
        copro(0x06, nullptr, 0, none);
    }
}

// 0x37720 body_draw: the fade, the frame counters and the pose; the object matrix T(pos) R(z, y, x) S with the root's
// motion step (x / z from the last frame's record, y as recorded; FixMode: the record itself; NoBias, frame 0, a
// plain blend or Stay: y only, nothing for type 0x40), kept at +0x1E4; turned by the root joint, the world position
// back through stored matrix 1 (the camera's inverse) unless FixMode (type 0x40: y too, else the height to +0x150);
// the direction (coprocessor 0x6F); the part tree; severed parts freed; the skins; the end flags; the fade's end
template <class P> void Game<P>::body_draw(uint32_t obj)
{
    const uint32_t root = of<uint32_t>(obj, 0xC8);
    mem.at<int16_t>(cr::body_cut) = 0;
    mem.set32(G::body_current, obj);
    draw_fade_begin(of<int16_t>(obj, 0x2E0));
    body_motion2_frame_flags(obj);
    body_motion_advance(obj);
    body_pose(obj);
    const uint32_t fd = of<uint32_t>(obj, 0xD8);
    Tgp::Out none;
    copro(0x05, nullptr, 0, none);
    // PC Mode: THOTD.EXE body_draw 0x477B00's own transform on PM's stack (pushed; translate, turns z / y / x, the
    // 3 x 3 scaled), in step with the coprocessor's (pmbody)
    const bool pmb = pm_body();
    struct KeepM { pm::Matrix& v; pm::Matrix s; ~KeepM() { v = s; } } keepM{pm_body_m, pm_body_m};
    if (pmb) {
        pc_matrix_push();
        pm::Matrix& c = pm_cur();
        c.translate(of<float>(obj, 0x20), of<float>(obj, 0x24), of<float>(obj, 0x28));
        c.rotate_z(of<uint32_t>(obj, 0x34));
        c.rotate_y(of<uint32_t>(obj, 0x30));
        c.rotate_x(of<uint32_t>(obj, 0x2C));
        const float k = of<float>(obj, 0x94);
        for (int i = 0; i < 9; ++i)
            c.e[i] = c.e[i] * k;
    }
    mem.set32(cr::frame_data, fd);
    uint32_t t[3] = {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)};
    const uint32_t fx = mem.u32(fd), fy = mem.u32(fd + 4), fz = mem.u32(fd + 8);
    copro(0x12, t, 3, none);
    uint32_t r[3] = {of<uint32_t>(obj, 0x34), of<uint32_t>(obj, 0x30), of<uint32_t>(obj, 0x2C)};
    copro(0x86, r, 3, none);
    mem.set32(cr::motion2_data, of<uint32_t>(obj, 0x110));
    const uint32_t sc = of<uint32_t>(obj, 0x94);
    uint32_t s[3] = {sc, sc, sc};
    copro(0x13, s, 3, none);
    const uint8_t f2 = of<uint8_t>(obj, 0x3C6);
    const int16_t type = of<int16_t>(obj, 0xCC);
    bool move = true;
    uint32_t d[3];
    if (f2 & 1) {
        d[0] = fx, d[1] = fy, d[2] = fz ^ 0x80000000u;
    } else if ((of<uint8_t>(obj, 0x3C5) >> 3) & 1 || of<int16_t>(obj, 0xF4) == 0 || (f2 & 10) == 2 ||
               (of<uint8_t>(obj, 0x3C7) >> 1) & 1) {
        if (type == 0x40)
            move = false;
        d[0] = 0, d[1] = fy, d[2] = 0;
    } else {
        const uint32_t words = (uint32_t(body_joints(obj)) * 48 + 0x7F) >> 5;
        const uint32_t base = mem.u32(cr::motion_data + 4 * uint32_t(int32_t(of<int16_t>(obj, 0xD0))));
        if ((f2 >> 3) & 1) {
            const int32_t bf = of<int16_t>(obj, 0xD4);
            const uint32_t p = base + uint32_t(bf * 4 - 4) * words, q = base + uint32_t(bf * 4) * words;
            d[0] = fbits(mem.f32(q) - mem.f32(p)), d[1] = fy, d[2] = fbits(mem.f32(q + 8) - mem.f32(p + 8)) ^ 0x80000000u;
        } else {
            const uint32_t p = base + uint32_t(of<int16_t>(obj, 0xF4) * 4 - 4) * words;
            d[0] = fbits(ffrom(fx) - mem.f32(p));
            d[1] = type == 0x40 ? fbits(ffrom(fy) - mem.f32(p + 4)) : fy;
            d[2] = fbits(ffrom(fz) - mem.f32(p + 8)) ^ 0x80000000u;
        }
    }
    if (move)
        copro(0x12, d, 3, none);
    Tgp::Out m;
    copro(0x11, nullptr, 0, m);
    for (uint32_t k = 0; k < 12; ++k)
        of<uint32_t>(obj, 0x1E4 + 4 * k) = m.w[k];
    const uint32_t cur = mem.u32(cr::frame_data);
    uint32_t a[3] = {0u - mem.at<uint16_t>(cur + 0x10), mem.at<uint16_t>(cur + 0xE), mem.at<uint16_t>(cur + 0xC)};
    if (pmb) {   // the root step, the body matrix kept (the PC's +0x1EC), the root joint's turns
        if (move)
            pm_cur().translate(ffrom(d[0]), ffrom(d[1]), ffrom(d[2]));
        pm_body_m = pm_cur();
        pm_cur().rotate_z(a[0]);
        pm_cur().rotate_y(a[1]);
        pm_cur().rotate_x(a[2]);
    }
    copro(0x86, a, 3, none);
    copro(0x05, nullptr, 0, none);
    uint32_t one[1] = {1};
    copro(0x77, one, 1, none);
    if (pmb) {   // pushed, times stored matrix 1 (the camera, pc_matrix_mul_stored's body copy 0x47593C)
        pc_matrix_push();
        pm_cur().mul_stored(pm_camera);
    }
    // (the PC also keeps the position of type 0x22, Sophie, in chapter slot 5: the attract's prologue)
    if (!(of<uint8_t>(obj, 0x3C6) & 1) && !(pmb && of<int16_t>(obj, 0xCC) == 0x22 && mem.at<int8_t>(G::chapter) == 5)) {
        Tgp::Out p;
        copro(0x20, nullptr, 0, p);
        if (pmb)
            for (int k = 0; k < 3; ++k)
                p.w[k] = fbits(pm_cur().e[9 + k]);
        of<uint32_t>(obj, 0x20) = p.w[0];
        of<uint32_t>(obj, of<int16_t>(obj, 0xCC) == 0x40 ? 0x24 : 0x150) = p.w[1];
        of<uint32_t>(obj, 0x28) = p.w[2];
    }
    Tgp::Out ang;
    copro(0x6F, nullptr, 0, ang);
    if (pmb) {   // the direction: the yaw of the matrix's angles (0x475F38), then popped
        uint32_t yaw = 0, pitch = 0, c = 0;
        pm::matrix_angles_yp(pm_cur(), yaw, pitch, c);
        ang.w[0] = yaw;
        pcm_pop();
    }
    of<uint32_t>(obj, 0x14C) = ang.w[0];
    copro(0x06, nullptr, 0, none);
    for (int32_t i = 0; i < mem.at<BodyPart>(root).child_count; ++i)
        part_draw(mem.u32(mem.at<BodyPart>(root).children.a + 4 * uint32_t(i)));
    copro(0x06, nullptr, 0, none);
    if (pmb)
        pcm_pop();
    if (mem.at<int16_t>(cr::body_cut) != 0)
        body_free_cut_parts(root);
    if ((of<uint8_t>(obj, 0x3C4) >> 4) & 1)
        skin_draw(obj);
    if (of<int16_t>(obj, 0xCC) == 0x22)
        sophie_skin_draw(obj);
    body_motion2_advance(obj);
    if (of<int16_t>(obj, 0x2E0) != 0xFF)
        geo_send_words(0, 31, 0x7A0);
}

// 0x36240 part_draw: part_hit_mark; the parent's matrix kept (+0x7C), the joint offset (z negated); for a mirrored part
// in a mirrored motion (or a plain blend into one) the offset re-taken from the body's matrix; the joint's angles from
// the frame (or the second motion's); the body's draw hook (+0x11C: the model), with flags2 bit 1 the shadow model
// under stored matrix 3; the blood of a severed mark; the home part's world position (the body's home, +0x2D4); the
// hit sphere's centre in camera space (+0x5C, the shot test); then the children
template <class P> void Game<P>::part_draw(uint32_t part)
{
    part_hit_mark(part);
    Tgp::Out none;
    copro(0x05, nullptr, 0, none);
    Tgp::Out saved;
    copro(0x11, nullptr, 0, saved);
    uint32_t off[3] = {mem.u32(part + 4), mem.u32(part + 8), mem.u32(part + 0xC) ^ 0x80000000u};
    for (uint32_t k = 0; k < 12; ++k)
        mem.set32(part + 0x7C + 4 * k, saved.w[k]);
    copro(0x12, off, 3, none);
    // PC Mode: THOTD.EXE's part draw 0x476B70 on PM's stack, in step (pmbody); the kept matrix +0x7C stays the
    // arcade's (the drawing's: severed pieces)
    const bool pmb = pm_body();
    if (pmb) {
        pc_matrix_push();
        pm_cur().translate(ffrom(off[0]), ffrom(off[1]), ffrom(off[2]));
    }
    const uint8_t side = mem.at<BodyPart>(part).side_flags;
    const uint32_t body = mem.u32(G::body_current);
    if (((side >> 4) & 1) && (mem.at<uint8_t>(cr::mirrored + uint32_t(int32_t(of<int16_t>(body, 0xD0)))) != 0 ||
                              (of<uint8_t>(body, 0x3C6) & 18) == 18)) {
        Tgp::Out cur;
        copro(0x11, nullptr, 0, cur);
        const uint32_t b = mem.u32(G::body_current);
        uint32_t bm[12];
        for (uint32_t k = 0; k < 12; ++k)
            bm[k] = of<uint32_t>(b, 0x1E4 + 4 * k);
        copro(0x07, bm, 12, none);
        copro(0x05, nullptr, 0, none);
        copro(0x7A, nullptr, 0, none);
        copro(0x09, cur.w, 12, none);
        Tgp::Out p;
        copro(0x20, nullptr, 0, p);
        copro(0x06, nullptr, 0, none);
        uint32_t v[3] = {p.w[0], p.w[1], p.w[2]};
        copro(0x12, v, 3, none);
        if (pmb) {   // the body matrix inverted (0x475B68) times the part's (0x475798): its translation
            const pm::Matrix local = pm_cur();
            pm::Matrix t = pm_body_m;
            t.invert();
            t.mul(local.e);
            pm_cur() = pm_body_m;
            pm_cur().translate(t.e[9], t.e[10], t.e[11]);
        }
    }
    const uint32_t b = mem.u32(G::body_current);
    const uint32_t data = ((mem.at<BodyPart>(part).side_flags >> 5) & 1) || !((of<uint8_t>(b, 0x3C6) >> 5) & 1)
                              ? mem.u32(cr::frame_data) : mem.u32(cr::motion2_data);
    const uint32_t rec = data + uint32_t(mem.at<BodyPart>(part).role) * 6;
    uint32_t a[3] = {0u - mem.at<uint16_t>(rec + 0x10), mem.at<uint16_t>(rec + 0xE), mem.at<uint16_t>(rec + 0xC)};
    copro(0x86, a, 3, none);
    if (pmb) {
        pm_cur().rotate_z(a[0]);
        pm_cur().rotate_y(a[1]);
        pm_cur().rotate_x(a[2]);
    }
    const uint32_t body2 = mem.u32(G::body_current);
    mem.at<int16_t>(cr::body_home) = 0;
    const uint32_t hook = of<uint32_t>(body2, 0x11C);
    if ((of<uint8_t>(body2, 0x6C) >> 1) & 1) {
        const uint32_t shadows = mem.u32(cr::shadow_models + 4 * uint32_t(int32_t(of<int16_t>(body2, 0xCC))));
        call_routine(hook, part);
        copro(0x05, nullptr, 0, none);
        const uint32_t b3 = mem.u32(G::body_current);
        uint32_t three[1] = {3};
        copro(0x77, three, 1, none);
        if (shadows != 0) {
            const uint32_t table = mem.u32(cr::shadow_models + 4 * uint32_t(int32_t(of<int16_t>(b3, 0xCC))));
            geo_draw_model(mem.u32(table + 4 * uint32_t(mem.at<BodyPart>(part).role)));
        } else {
            geo_draw_model(uint32_t(mem.at<BodyPart>(part).part_id));
        }
        copro(0x06, nullptr, 0, none);
    } else {
        call_routine(hook, part);
    }
    if ((mem.at<BodyPart>(part).flags >> 21) & 1) {
        blood_drip_at(mem.f32(part + 0x5C), mem.f32(part + 0x60), mem.f32(part + 0x64));
        mem.at<BodyPart>(part).flags &= ~(1u << 21);
    }
    if (mem.at<int16_t>(cr::body_home) != 0) {
        copro(0x05, nullptr, 0, none);
        Tgp::Out p;
        copro(0x20, nullptr, 0, p);
        uint32_t one[1] = {1};
        copro(0x73, one, 1, none);
        Tgp::Out w;
        copro(0x1A, p.w, 3, w);
        if (pmb) {   // the part's position through stored matrix 1 (the camera; the file's transform 0x474CE8)
            float h[3];
            pm::from_floats(pm_camera).transform(pm_cur().e[9], pm_cur().e[10], pm_cur().e[11], h[0], h[1], h[2]);
            for (int k = 0; k < 3; ++k)
                w.w[k] = fbits(h[k]);
        }
        const uint32_t b4 = mem.u32(G::body_current);
        of<uint32_t>(b4, 0x2D4) = w.w[0];
        of<uint32_t>(b4, 0x2D8) = w.w[1];
        of<uint32_t>(b4, 0x2DC) = w.w[2];
        copro(0x06, nullptr, 0, none);
    }
    if (of<uint8_t>(mem.u32(G::body_current), 0x6C) & 1) {
        copro(0x05, nullptr, 0, none);
        const uint32_t id = uint32_t(mem.at<BodyPart>(part).part_id);
        uint32_t c[3] = {mem.u32(cr::hit_spheres + id * 16), mem.u32(cr::hit_spheres + id * 16 + 4), mem.u32(cr::hit_spheres + id * 16 + 8)};
        copro(0x12, c, 3, none);
        Tgp::Out p;
        copro(0x20, nullptr, 0, p);
        if (pmb) {   // the hit sphere's centre: translated on a pushed copy, its translation
            pm::Matrix h = pm_cur();
            h.translate(ffrom(c[0]), ffrom(c[1]), ffrom(c[2]));
            for (int k = 0; k < 3; ++k)
                p.w[k] = fbits(h.e[9 + k]);
        }
        mem.set32(part + 0x64, p.w[2]);
        mem.set32(part + 0x5C, p.w[0]);
        mem.set32(part + 0x60, p.w[1]);
        if ((mem.at<BodyPart>(part).flags >> 20) & 1) {
            blood_drip_at(mem.f32(part + 0x5C), mem.f32(part + 0x60), mem.f32(part + 0x64));
            mem.at<BodyPart>(part).flags &= ~(1u << 20);
        }
        copro(0x06, nullptr, 0, none);
    }
    for (int32_t i = 0; i < mem.at<BodyPart>(part).child_count; ++i)
        part_draw(mem.u32(mem.at<BodyPart>(part).children.a + 4 * uint32_t(i)));
    copro(0x06, nullptr, 0, none);
    if (pmb)
        pcm_pop();
}

// 0x9CBD0 part_hit_mark: a part hit this frame (flags bits 1 / 2) loses those bits and bit 8; one that may be severed
// (bit 31) gets its sever mark (role table +4: 1 = in the parent's marks, 2 = its own) and body_cut, then blood
// (bit 20 for the head class 1, else bit 21)
template <class P> void Game<P>::part_hit_mark(uint32_t part)
{
    BodyPart& p = mem.at<BodyPart>(part);
    if ((p.flags & 6) == 0)
        return;
    p.flags &= 0xFFFFFEF9u;
    if (int32_t(p.flags) >= 0)
        return;
    const uint32_t type = uint32_t(int32_t(of<int16_t>(mem.u32(G::body_current), 0xCC)));
    const PartRole& role = mem.at<PartRole>(G::part_roles + (type * 0x1F + uint32_t(p.role)) * 8);
    if (role.sever == 1) {
        mem.at<uint8_t>(p.parent.a + 0x28 + uint32_t(p.index)) |= 1;
        mem.at<int16_t>(cr::body_cut) = 1;
    } else if (role.sever == 2) {
        p.marks |= 1;
        mem.at<int16_t>(cr::body_cut) = 1;
    }
    p.flags |= role.part_class == 1 ? (1u << 20) : (1u << 21);
}

// 0x9EE20 body_hook_angle_ease (body): the two look offsets +0x144 / +0x148 (16-bit angles) ease back to 0 by 0xC0 a
// frame; within 0xC0 of 0 they snap to it
template <class P> void Game<P>::body_hook_angle_ease(uint32_t body)
{
    for (uint32_t off : {0x144u, 0x148u}) {
        uint32_t v = of<uint16_t>(body, off);
        if (v != 0) {
            if (v <= 0xBF || v + 0xC0 > 0x10000)
                v = 0;
            else if (v <= 0x7FFF)
                v = v - 0xC0;
            else
                v = v + 0xC0;
        }
        of<uint32_t>(body, off) = v;
    }
}

} // namespace hotd::port
