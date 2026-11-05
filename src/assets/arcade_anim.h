// Arcade creature skeletons and motions (docs/formats.md §23, tools/m2_anim.py).
//
// Everything comes from the program ROM (maincpu) and main_data: a skeleton per creature type (part tree with model IDs,
// joint offsets and joint indices), and 674 motions of frame records (root position + three 16-bit angles per joint).
// Part matrices are built the way the game drives the geometry coprocessor: node = parent * T(x, y, -z) * R(joint),
// R(a0, a1, a2) = Rz(a2) * Ry(-a1) * Rx(-a0). The PC creature meshes share the arcade's object IDs and local coordinates,
// so the same matrices pose both.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hotd {

// Rotation (row-major 3x3, applied as rot * v) and translation: v' = rot * v + pos.
struct Xform {
    float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float pos[3] = {0, 0, 0};
};
Xform operator*(const Xform& a, const Xform& b);
Xform inverse(const Xform& a);   // general 3x3 inverse (the creature's root may carry a scale)

struct SkeletonNode {
    uint32_t model = 0;      // object ID drawn with this node's matrix (0 = none)
    float offset[3] = {};    // from the parent's matrix, in the ROM's sign (z is negated when applied)
    uint32_t flag = 0;
    int joint = 0;           // index of the node's angle triple in a frame record (the part's role, part +0x38)
    int parent = -1;         // node index; parents come before their children; node 0 is the root (joint 0)
    bool mirror = false;     // flag bit 0 (part side flags bit 4): in a mirrored motion the part is placed from the body's
                             // matrix, not its parent's (part_draw 0x36240; the feet / legs of the walkers)
    bool second = false;     // the game can take its angles from a second motion (object +0xFE / +0x110 while byte +0x3C6
                             // bit 5 is set: arms while walking): the parts whose side flags bit 5 is clear (part_init
                             // 0x36750: first-level parts by the type's side byte 0x118000 and the role, deeper ones
                             // inherit). (Was flag bit 0, which is the mirror flag: viewer fix, 2026-10-20.)
};

struct Skeleton {
    int joints = 0;
    std::vector<SkeletonNode> nodes;
};

class CreatureAnimations {
public:
    bool load(const std::string& regionsDir, std::string& err);
    bool loaded() const { return !skeletons_.empty(); }
    const std::vector<uint8_t>& mainData() const { return mainData_; }
    const std::vector<uint8_t>& maincpu() const { return maincpu_; }   // (the Gallery's damage / blood tables)

    int typeCount() const { return int(skeletons_.size()); }
    const Skeleton& skeleton(int type) const { return skeletons_[type]; }   // nodes empty if the type has none
    int motionCount() const { return int(motions_.size()); }
    int motionFrames(int m) const { return motions_[m].frames; }
    int motionJoints(int m) const { return motions_[m].joints; }   // 0 if unknown (the last motion)

    // The developers' names from the program ROM (maincpu 0xC8A60 / 0xC8BE0; THOTD.EXE has the same tables):
    // "BO_hyum" for creature type 35 (the Chariot), "MO_z_walk_g" for motion 0x289 ... Empty if out of range.
    const std::string& bodyName(int type) const;
    const std::string& motionName(int motion) const;

    // Joint rotation of frame f (1-based) of motion m, row-major 3x3.
    void jointRotation(int motion, int frame, int joint, float out[9]) const;
    // Every node's matrix in creature space (the object's own position/rotation not included) for frame f (1-based).
    // The root translation is taken relative to frame 1 in x and z, as the game draws moving motions in place.
    // motion2 >= 0: nodes marked `second` take their angles from frame2 of motion2.
    // Mirrored motions (byte table maincpu 0x7C6F0): `mirror` nodes are placed from the body's matrix (part_draw).
    void pose(int type, int motion, int frame, std::vector<Xform>& out, int motion2 = -1, int frame2 = 1) const;
    bool mirrored(int motion) const { return motion >= 0 && motion < int(motions_.size()) && motions_[motion].mirrored; }

    // The part class of a type's role (joint): role table maincpu 0x1184E0 + type x 0xF8 + role x 8, byte 3 (the class
    // body_part_draw_hook 0x9CE60 goes by: 1 head, 6 / 7 hands, 8 / 9 the skin anchors ...); -1 out of range
    int partClass(int type, int role, int byte = 3) const;   // byte 0: the role number the boss hooks test
    // the skeleton node whose joint (role) is `role`, -1 if none (the skin anchors and the hooks go by role)
    int nodeOfRole(int type, int role) const;

private:
    struct Motion { const uint8_t* data = nullptr; int frames = 0; int joints = 0; bool mirrored = false; };
    const int16_t* angles(int motion, int frame) const;   // 3 per joint
    const float* root(int motion, int frame) const;       // x, y, z

    std::vector<uint8_t> maincpu_, mainData_;
    std::vector<Skeleton> skeletons_;
    std::vector<Motion> motions_;
    std::vector<std::string> bodyNames_, motionNames_;
};

} // namespace hotd
