// The Ragdoll mod (docs/modding.md, user 2026-10-26: "like the HOTD Remake"): when a creature dies, its drawn
// body becomes a ragdoll (position-based dynamics on the skeleton's parts: each part a small rigid tetrahedron of
// particles, the joints tying a child's pivot to its parent, simple angle limits, the floor at the creature's height with
// a little friction); shots that hit a dead body push it along the shot, so it slides over the floor for a while; bodies
// settle, then fade out when the arcade removes them (or after the Bodies Stay time).
//
// DRAW ONLY: Runtime::ragdoll_frame runs after the frame's logic and changes the frame's draw list (and skins) alone.
// The address space is only read, so the game logic, the scores, the proofs and netplay are those of the game without
// the mod (each PC may have it on or off). The dead enemy's own logic goes on as normal; only its drawn pose is the
// ragdoll's. Bosses keep their own deaths (not the players / Sophie / bosses). ragall (2026-10-27): every creature kind
// that dies: each body type's own part tree (dogs, Ebitan, frogs, the agile ones, Taran ...), and the creatures drawn by
// their task alone (bats, Murrers, rats, slugs, insects, falling frogs, the severed heads) from their models.
#pragma once

#include <cstdint>
#include <map>
#include <vector>
#include <string>

namespace hotd::game {

struct RagdollSettings {
    bool on = false;          // the mod is on (Options > Mods; launcher.ini mods= / --mod ragdoll)
    float push = 1.0f;        // a shot's push (fixed: no setting since leftovers)
    float friction = 0.12f;   // the floor's friction per frame of contact (Smooth 0.05, Normal 0.12, Rough 0.3)
    int stayFrames = 480;     // after the arcade removes the body, how long the ragdoll stays (0: it goes with the arcade's)
    float gravity = 1.0f;     // x the arcade's own fall (0.0272 units per frame per frame)
    bool dismember = true;    // ragdoll2: Dismemberment On (the arcade's); Off: the severed pieces not drawn, the parts kept
};
RagdollSettings& ragdollSettings();
// the mod's settings from its values (mod.ini [settings] keys friction / stay / gravity / gore, values as the Mods page shows)
void ragdollConfigure(bool on, const std::map<std::string, std::string>& values);
class Runtime;
// misc18: a model drawn by this task (outside any body) may be a moving prop's wall: not the effects on a creature or
// its parts (blood bursts / drips, hit splashes, a boss's glow / armour / afterimage), a creature fading in, what a
// creature holds or throws, the shots' effects, splashes, pickups. Before, a blood burst's frames (models 7..0x23,
// 0x14 among them: door-sized) were walls on a living researcher, and the bodies bumped into him
bool ragdollPropTask(const Runtime& rt, uint32_t task);

// leftovers: the scene's walls for the ragdolls (draw only): the world's standing triangles (their normal within ~53
// degrees of level), per world object, built by the view when the chapter loads with the mod on; `active`: the objects
// the world drew last frame (the draw zone's). A ragdoll's particle that would cross one stays on its side
struct RagdollWalls {
    struct Obj {
        float lo[3], hi[3];        // its triangles' box (world space)
        std::vector<float> tris;   // 12 floats a triangle: its corners, its unit normal (world space)
        std::vector<float> floors; // the lying ones (|normal y| >= 0.6), the same way: the ground under a body
    };
    std::vector<Obj> objs;
    std::vector<int> active;
    size_t statics = 0;            // objs [0, statics) the world's, the rest this frame's moving props (doors ...)
};

} // namespace hotd::game
