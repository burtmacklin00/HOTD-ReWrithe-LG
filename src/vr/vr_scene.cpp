// VR mode: the VR frame (vr_scene.h, docs/vr.md)
#include "vr/vr_scene.h"

#include "game/hud.h"
#include "game/ragdoll.h"
#include "game/view.h"
#include "render/gamma.h"
#include "render/gl.h"
#include "render/gl_backend.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace hotd::vr {

namespace {

constexpr float kPi = 3.14159265f;
constexpr gl::GLenum kDepthComponent = 0x1902;   // GL_DEPTH_COMPONENT (ReadPixels)

// the hands' models (game units, left-handed as the world), measured from the data (0x719 `ter00a` / its gun-only copy
// kGunOnlyKey, arcade and PC alike: the barrel along the model's -y from y -0.41 (the slide's back) to -2.26 (the
// muzzle), the slide's top +z 0.91, its middle x -0.375 z 0.65, the grip down to z -0.6; 0x717 `tel00a`: the wrist at
// the origin, the fingers along +x curling to -z (the palm faces -z), the thumb at +y, the palm's middle ~(0.45, -0.1,
// 0.05)). Their place on the controller: anchors below
const float kGunRear[3] = {-0.375f, -0.408f, 0.65f};   // the barrel's line at the slide's back
const float kGunMuzzle[3] = {-0.375f, -2.258f, 0.65f};   // ... at the muzzle (the shot's ray starts here)

// the gun hand into the aim pose's local frame (left-handed: +x right, +y up, +z ahead): the barrel (-y) ahead, the
// slide's top (+z) up, x kept; the slide's back on the aim pose's origin (the hand hangs below and behind it, as a
// hand around the controller)
Rigid gunAnchor()
{
    Rigid a;
    const float r[9] = {1, 0, 0, 0, 0, 1, 0, -1, 0};
    std::copy(r, r + 9, a.r);
    float p[3];
    a.rotate(kGunRear, p);
    for (int k = 0; k < 3; ++k) a.t[k] = -p[k];
    return a;
}

// dual pistols (docs/vr.md "Dual pistols"): the left gun is the same gun hand mirrored (model x -> -x: a left hand
// holding the handgun; the renderer turns the normals by the same matrix, so the faces and the light stay right), placed
// as gunAnchor places the right one (the mirrored slide's back on the left aim pose's origin)
Rigid gunAnchorLeft()
{
    Rigid a = gunAnchor();
    for (int k = 0; k < 3; ++k) a.r[k * 3] = -a.r[k * 3];   // (R x diag(-1, 1, 1))
    float p[3];
    a.rotate(kGunRear, p);
    for (int k = 0; k < 3; ++k) a.t[k] = -p[k];
    return a;
}

// the left hand into the grip pose's local frame: the fingers (+x) ahead, the palm (-z) to the right (towards the
// controller in the hand), the thumb (+y) up; the palm's middle on the grip pose's origin (the palm, OpenXR's grip)
Rigid leftAnchor()
{
    Rigid a;
    const float r[9] = {0, 0, -1, 0, 1, 0, 1, 0, 0};
    std::copy(r, r + 9, a.r);
    const float palm[3] = {0.45f, -0.1f, 0.05f};
    float p[3];
    a.rotate(palm, p);
    for (int k = 0; k < 3; ++k) a.t[k] = -p[k];
    return a;
}

gl::GLuint compileProgram(const char* vs, const char* fs, const char* what, std::string& err)
{
    auto one = [&](gl::GLenum type, const char* src) {
        const gl::GLuint s = gl::CreateShader(type);
        gl::ShaderSource(s, 1, &src, nullptr);
        gl::CompileShader(s);
        if (!gl::shaderLog(s, what)) {
            gl::DeleteShader(s);
            return gl::GLuint(0);
        }
        return s;
    };
    const gl::GLuint v = one(gl::VERTEX_SHADER, vs), f = one(gl::FRAGMENT_SHADER, fs);
    if (!v || !f) {
        err = std::string(what) + ": shader";
        return 0;
    }
    const gl::GLuint p = gl::CreateProgram();
    gl::AttachShader(p, v);
    gl::AttachShader(p, f);
    gl::LinkProgram(p);
    gl::DeleteShader(v);
    gl::DeleteShader(f);
    if (!gl::programLog(p, what)) {
        err = std::string(what) + ": program";
        gl::DeleteProgram(p);
        return 0;
    }
    return p;
}

// the reference frame (the player's space in the world) from the game's (blended) camera: follow = its full
// orientation; level = its heading only
Rigid referenceFrom(const Camera& c, int follow)
{
    Camera k = c;
    if (follow == 1) k.pitch = 0;
    float r[3], u[3], f[3];
    k.basis(r, u, f);
    Rigid o;
    for (int i = 0; i < 3; ++i) {
        o.r[i * 3] = r[i], o.r[i * 3 + 1] = u[i], o.r[i * 3 + 2] = f[i];
        o.t[i] = c.pos[i];
    }
    return o;
}

// an eye's view-projection in the player's own space (metres; left-handed, +z ahead; OpenXR LOCAL: the origin at the
// head's start / the last recentre), for what stays put around the player whatever the game camera does (the flat
// screens' big screen, the cinema screen)
void seatViewProj(const Eye& e, Rigid& eyeL, float vp[16])
{
    eyeL = poseToLocal(e.pose, 1.0f);
    float view[16];
    eyeL.viewMatrix(view);
    const float n = 0.05f, fa = 100.0f, sx = e.fov.tanRight - e.fov.tanLeft, sy = e.fov.tanUp - e.fov.tanDown;
    const float proj[16] = {2.0f / sx, 0, 0, 0, 0, 2.0f / sy, 0, 0, -(e.fov.tanRight + e.fov.tanLeft) / sx,
                            -(e.fov.tanUp + e.fov.tanDown) / sy, (fa + n) / (fa - n), 1, 0, 0, -2 * fa * n / (fa - n), 0};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += proj[k * 4 + row] * view[col * 4 + k];
            vp[col * 4 + row] = s;
        }
}

// vr4: a placed model's bounds as a sphere in the world (its box's middle, the half diagonal through rot's columns)
void itemSphere(const DrawItem& it, float c[3], float& r)
{
    const Model& m = *it.model;
    const Placement& p = *it.placement;
    float lc[3], e[3];
    for (int k = 0; k < 3; ++k) lc[k] = 0.5f * (m.boundsMin[k] + m.boundsMax[k]), e[k] = 0.5f * (m.boundsMax[k] - m.boundsMin[k]);
    float r2 = 0;
    for (int i = 0; i < 3; ++i) {
        c[i] = p.rot[i * 3] * lc[0] + p.rot[i * 3 + 1] * lc[1] + p.rot[i * 3 + 2] * lc[2] + p.pos[i];
        const float col = std::sqrt(p.rot[i] * p.rot[i] + p.rot[3 + i] * p.rot[3 + i] + p.rot[6 + i] * p.rot[6 + i]);
        r2 += (col * e[i]) * (col * e[i]);
    }
    r = std::sqrt(r2);
}

// vr4 (docs/vr.md "The HUD and close creatures"): the nearest creature part's depth (game units along the reference
// frame's +z, its nearest side) inside the HUD panel's frustum (tx, ty: the tangents of its half angles); -1: none
float nearestCreature(const std::vector<DrawItem>& items, const Rigid& ref, float tx, float ty)
{
    const Rigid inv = ref.inverse();
    float best = -1.0f;
    for (const DrawItem& it : items) {
        if (it.vrKind != 1 || !it.model || !it.placement || it.fade < 0.05f)
            continue;
        float c[3], r, l[3];
        itemSphere(it, c, r);
        inv.apply(c, l);
        if (l[2] + r <= 0.0f)
            continue;   // (behind)
        const float zf = std::max(l[2], 0.0f);
        if (std::abs(l[0]) > tx * zf + r || std::abs(l[1]) > ty * zf + r)
            continue;   // (outside the panel)
        const float z = std::max(l[2] - r, 0.0f);
        if (best < 0.0f || z < best) {
            best = z;
            static const bool trace = std::getenv("HOTD_VR_HUD_TRACE") != nullptr;   // (debugging: each nearer one)
            if (trace)
                SDL_Log("vr hud: nearest model %#x at %.1f %.1f %.1f r %.1f", it.model->key, l[0], l[1], l[2], r);
        }
    }
    return best;
}

} // namespace

bool Scene::init(std::unique_ptr<Backend> backend, const std::string& vrDir, std::string& err)
{
    vrDir_ = vrDir;
    const bool had = settings.load((std::filesystem::path(vrDir) / "vr.ini").string());
    for (const std::string& o : overrides)   // (--vr-set key=value)
        if (const size_t eq = o.find('='); eq != std::string::npos)
            settings.set(o.substr(0, eq), o.substr(eq + 1));
    ini = settings;   // (vrcomfort: the in-game VR rows' "not set" values)
    if (!backend) {
        err = "vr: no backend";
        return false;
    }
    if (!backend->init(settings, err))
        return false;
    if (!makePrograms(err))
        return false;
    if (settings.questSim) {   // questsim: the Quest's eyes and its aim (OpenGL ES never reads depth: the CPU ray)
        depthReadable_ = false;
        backend->setQuestSim(settings.resolutionScale);
        SDL_Log("vr: quest-sim: no depth readback (the CPU aim ray), Quest 3 eye buffers 1680 x 1760 x %.2f, 90 Hz budget",
                settings.resolutionScale);
    }
    SDL_Log("vr: backend %s, settings %s; hands: the game's own models (gun hand %#x, left hand %#x)", backend->name(),
            had ? "from vr.ini" : "defaults", kGunHandModel, kLeftHandModel);
    gl::GenVertexArrays(1, &spriteVao_);
    gl::GenBuffers(1, &spriteVbo_);
    gl::BindVertexArray(spriteVao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, spriteVbo_);
    gl::BufferData(gl::ARRAY_BUFFER, 64 * 5 * sizeof(float), nullptr, gl::DYNAMIC_DRAW);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, gl::FLOAT, gl::FALSE_, 5 * sizeof(float), nullptr);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, gl::FLOAT, gl::FALSE_, 5 * sizeof(float), (const void*)(3 * sizeof(float)));
    gl::BindVertexArray(0);
    const uint8_t white[4] = {255, 255, 255, 255};
    gl::GenTextures(1, &whiteTex_);
    gl::BindTexture(gl::TEXTURE_2D, whiteTex_);
    gl::texTag("vr white");   // (questui: the upload log, render/gl_backend.h)
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, 1, 1, 0, gl::RGBA, gl::UNSIGNED_BYTE, white);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
    backend_ = std::move(backend);
    return true;
}

void Scene::shutdown()
{
    if (backend_) backend_->shutdown();
    backend_.reset();
    if (spriteVbo_) gl::DeleteBuffers(1, &spriteVbo_);
    if (spriteVao_) gl::DeleteVertexArrays(1, &spriteVao_);
    if (whiteTex_) gl::DeleteTextures(1, &whiteTex_);
    if (panelTex_) gl::DeleteTextures(1, &panelTex_);
    if (panelFbo_) gl::DeleteFramebuffers(1, &panelFbo_);
    if (screenTex_) gl::DeleteTextures(1, &screenTex_);
    if (screenFbo_) gl::DeleteFramebuffers(1, &screenFbo_);
    if (spriteProg_) gl::DeleteProgram(spriteProg_);
    if (cinemaTex_) gl::DeleteTextures(1, &cinemaTex_);
    if (cinemaDepth_) gl::DeleteRenderbuffers(1, &cinemaDepth_);
    if (cinemaFbo_) gl::DeleteFramebuffers(1, &cinemaFbo_);
    cinemaTex_ = cinemaDepth_ = cinemaFbo_ = 0;
    spriteVbo_ = spriteVao_ = whiteTex_ = panelTex_ = panelFbo_ = spriteProg_ = screenTex_ = screenFbo_ = 0;
}

bool Scene::makePrograms(std::string& err)
{
    static const char* spriteVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
uniform mat4 uVp;
out vec2 vUv;
void main() {
    gl_Position = uVp * vec4(aPos, 1.0);
    vUv = aUv;
})";
    // modes: 0 a picture (straight alpha) x tint; 1 the HUD panel (drawn by the HUD over transparent: colour already
    // times alpha, alpha squared: out premultiplied with sqrt(alpha)); 2 the laser (tint, fading along v); 3 the vignette;
    // 4 an opaque picture (quest: the flat screens' big screen); 5 as 4, added times the picture (vrfade: drawSprite's
    // blend); 6 a ring in the tint (questui: the big screen's pointer)
    static const char* spriteFs = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
uniform int uMode;
uniform vec4 uTint;
out vec4 oColor;
void main() {
    if (uMode == 0) {
        vec4 c = texture(uTex, vUv) * uTint;
        if (c.a < 0.01) discard;
        oColor = c;
    } else if (uMode == 1) {
        vec4 c = texture(uTex, vUv);
        oColor = vec4(c.rgb, sqrt(max(c.a, 0.0))) * uTint.a;
    } else if (uMode == 2) {
        float edge = 1.0 - abs(vUv.x * 2.0 - 1.0);
        oColor = vec4(uTint.rgb, uTint.a * edge * (1.0 - 0.75 * vUv.y));
    } else if (uMode == 3) {
        float r = length(vUv * 2.0 - 1.0);
        oColor = vec4(0.0, 0.0, 0.0, uTint.a * smoothstep(0.45, 1.25, r));
    } else if (uMode == 6) {
        float r = length(vUv * 2.0 - 1.0);
        float a = smoothstep(0.45, 0.6, r) * (1.0 - smoothstep(0.85, 1.0, r));
        if (a < 0.01) discard;
        oColor = vec4(uTint.rgb, uTint.a * a);
    } else {
        oColor = vec4(texture(uTex, vUv).rgb * uTint.rgb, 1.0);
    }
})";
    spriteProg_ = compileProgram(spriteVs, spriteFs, "vr sprite", err);
    if (!spriteProg_)
        return false;
    uVp_ = gl::GetUniformLocation(spriteProg_, "uVp");
    uTex_ = gl::GetUniformLocation(spriteProg_, "uTex");
    uMode_ = gl::GetUniformLocation(spriteProg_, "uMode");
    uTint_ = gl::GetUniformLocation(spriteProg_, "uTint");
    return true;
}

void Scene::ensurePanel(int w, int h)
{
    if (panelFbo_ && panelW_ == w && panelH_ == h)
        return;
    if (panelTex_) gl::DeleteTextures(1, &panelTex_);
    if (panelFbo_) gl::DeleteFramebuffers(1, &panelFbo_);
    panelW_ = w, panelH_ = h;
    gl::GenFramebuffers(1, &panelFbo_);
    gl::BindFramebuffer(gl::FRAMEBUFFER, panelFbo_);
    gl::GenTextures(1, &panelTex_);
    gl::BindTexture(gl::TEXTURE_2D, panelTex_);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR_MIPMAP_LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, panelTex_, 0);
    gl::framebufferOk("vr panel");
}

float Scene::cpuRay(const game::GameView& view, const std::vector<DrawItem>& items, const float o[3], const float d[3],
                    float maxDist)
{
    // vr4 (docs/vr.md "Aim depth"): the stage's own triangles (the world objects the zone drew this frame and the
    // moving props: the Ragdoll mod's walls and floors, game/ragdoll.h, built for every chapter) and the creatures'
    // parts (their models' boxes as placed this frame: DrawItem::vrKind 1); the nearest hit past 0.5 units
    constexpr float kMin = 0.5f;
    float best = maxDist;
    bool hit = false;
    if (const game::RagdollWalls* w = view.ragdollWalls())
        for (const int oi : w->active) {
            if (oi < 0 || size_t(oi) >= w->objs.size())
                continue;
            const game::RagdollWalls::Obj& ob = w->objs[size_t(oi)];
            float t0 = 0.0f, t1 = best;
            const float lo[3] = {ob.lo[0] - 0.5f, ob.lo[1] - 0.5f, ob.lo[2] - 0.5f}, hi[3] = {ob.hi[0] + 0.5f, ob.hi[1] + 0.5f, ob.hi[2] + 0.5f};
            if (!rayBox(o, d, lo, hi, t0, t1))
                continue;
            hit = rayTriangles(o, d, ob.tris.data(), ob.tris.size() / 12, kMin, best) || hit;
            hit = rayTriangles(o, d, ob.floors.data(), ob.floors.size() / 12, kMin, best) || hit;
        }
    static const int gapMode = [] { const char* g = std::getenv("HOTD_AIM_GAP"); return g ? std::atoi(g) : 0; }();
    float gapBest = 1e30f;
    for (const DrawItem& it : items) {
        if (it.vrKind == 4 && it.model && it.placement && !std::getenv("HOTD_AIM_BOXES")) {
            // questsim: a creature's skin (its joints: rebuilt every frame, its vertices kept, its bounds not kept up)
            for (const Mesh& me : it.model->meshes)
                if (!me.vertices.empty() && (me.variant < 0 || it.variant < 0 || me.variant == it.variant))
                    hit = rayPlacedTriangles(o, d, it.placement->rot, it.placement->pos, me.vertices.data(), sizeof(Vertex),
                                             me.vertices.size(), kMin, best) || hit;
            continue;
        }
        if (it.vrKind != 1 || !it.model || !it.placement || it.fade < 0.05f)
            continue;
        float t = 0, tExit = 0;   // (the parts' own boxes: shrunk to 0.6 - 0.9 they agreed no better with the picture: tested)
        if (!rayPlacedBox(o, d, it.placement->rot, it.placement->pos, it.model->boundsMin, it.model->boundsMax, kMin, best, t, &tExit))
            continue;
        const float before = best;
        // questsim (2026-11-10): inside the box, the part's own triangles (the drawn variant's meshes; the box was
        // bigger than the model: ~40 % of the CPU rays that stopped nearer than the picture); the box only where the
        // model's corners are gone (Mesh::positions: kept while VR is on)
        bool tested = false;
        static const bool boxesOnly = std::getenv("HOTD_AIM_BOXES") != nullptr;   // (testing: the boxes alone, as before)
        for (const Mesh& me : it.model->meshes) {
            if (boxesOnly) break;
            if (me.variant >= 0 && it.variant >= 0 && me.variant != it.variant)
                continue;
            if (!me.vertices.empty()) {
                tested = true;
                hit = rayPlacedTriangles(o, d, it.placement->rot, it.placement->pos, me.vertices.data(), sizeof(Vertex),
                                         me.vertices.size(), kMin, best) || hit;
            } else if (!me.positions.empty()) {
                tested = true;
                hit = rayPlacedTriangles(o, d, it.placement->rot, it.placement->pos, me.positions.data(), 3 * sizeof(float),
                                         me.positions.size() / 3, kMin, best) || hit;
            }
        }
        if (!tested)
            best = t, hit = true;
        else if (best >= before && gapMode)   // through the box between the part's triangles
            gapBest = std::min(gapBest, gapMode == 1 ? 0.5f * (t + tExit) : t);
    }
    if (gapBest < best)
        best = gapBest, hit = true;
    return hit ? best : -1.0f;
}

float Scene::march(const float o[3], const float d[3], const float vp[16], int w, int h, float maxDist)
{
    if (!depthReadable_)
        return -1.0f;
    // samples along the ray (closer together near the gun), projected into the eye's picture; the depth of their
    // pixels read in one rectangle; the first sample behind the picture's surface, then halving the step between it
    // and the one before (the depth: the eye's per-pixel depth, VR draws without the arcade's polygon sort)
    constexpr int kSamples = 64;
    const float minD = std::min(3.0f, maxDist * 0.5f);
    struct S { float t, x, y, z; bool in; };
    S s[kSamples];
    auto project = [&](float t, S& out) {
        const float p[3] = {o[0] + d[0] * t, o[1] + d[1] * t, o[2] + d[2] * t};
        float c[4];
        for (int r = 0; r < 4; ++r)
            c[r] = vp[r] * p[0] + vp[4 + r] * p[1] + vp[8 + r] * p[2] + vp[12 + r];
        out.t = t;
        out.in = c[3] > 1e-4f;
        if (!out.in) return;
        const float nx = c[0] / c[3], ny = c[1] / c[3], nz = c[2] / c[3];
        out.x = (nx * 0.5f + 0.5f) * float(w), out.y = (ny * 0.5f + 0.5f) * float(h);
        out.z = std::clamp(nz * 0.5f + 0.5f, 0.0f, 1.0f);   // (depth clamp)
        out.in = out.x >= 0 && out.y >= 0 && out.x < float(w) && out.y < float(h);
    };
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int k = 0; k < kSamples; ++k) {
        project(minD * std::pow(maxDist / minD, float(k) / float(kSamples - 1)), s[k]);
        if (!s[k].in) continue;
        x0 = std::min(x0, int(s[k].x)), y0 = std::min(y0, int(s[k].y));
        x1 = std::max(x1, int(s[k].x)), y1 = std::max(y1, int(s[k].y));
    }
    if (x1 < 0)
        return -1.0f;
    const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    depthBuf_.resize(size_t(bw) * size_t(bh));
    gl::PixelStorei(gl::PACK_ALIGNMENT, 4);
    while (gl::GetError() != 0) {}
    gl::ReadPixels(x0, y0, bw, bh, kDepthComponent, gl::FLOAT, depthBuf_.data());
    if (gl::GetError() != 0) {   // (OpenGL ES / ANGLE: no depth reads; the fixed distance then)
        depthReadable_ = false;
        SDL_Log("vr: the eye's depth cannot be read here: the aim's depth from the CPU ray test (aim_depth = auto; "
                "aim_depth = depth: the fixed aim_distance)");
        return -1.0f;
    }
    auto depthAt = [&](const S& q) {
        const int px = std::clamp(int(q.x), x0, x1), py = std::clamp(int(q.y), y0, y1);
        return depthBuf_[size_t(py - y0) * size_t(bw) + size_t(px - x0)];
    };
    auto behind = [&](const S& q) { return q.in && q.z > depthAt(q) + 2e-6f; };
    for (int k = 0; k < kSamples; ++k) {
        float lo = k ? s[k - 1].t : 0.0f, hi = -1.0f;
        // (modfix 2026-11-10: between two samples further apart in the picture than a pixel, a check at every pixel:
        // a thin edge between them (a post, a creature's outline the ray only grazes) was stepped over and the ray
        // went on to the wall behind, the round trips' worst outliers: the gun ray 3x the camera's)
        if (k && s[k - 1].in && s[k].in) {
            const int n = std::min(64, int(std::max(std::abs(s[k].x - s[k - 1].x), std::abs(s[k].y - s[k - 1].y))));
            for (int j = 1; j < n && hi < 0.0f; ++j) {
                S m;
                project(s[k - 1].t + (s[k].t - s[k - 1].t) * float(j) / float(n), m);
                if (behind(m)) hi = m.t;
                else lo = m.t;
            }
        }
        if (hi < 0.0f) {
            if (!behind(s[k])) continue;
            hi = s[k].t;
        }
        for (int i = 0; i < 8; ++i) {
            S m;
            project(0.5f * (lo + hi), m);
            if (behind(m)) hi = m.t;
            else lo = m.t;
        }
        return hi;
    }
    return -1.0f;
}

void Scene::drawSprite(const float* verts, int count, unsigned tex, int mode, const float tint[4], const float vp[16])
{
    gl::UseProgram(spriteProg_);
    gl::UniformMatrix4fv(uVp_, 1, gl::FALSE_, vp);
    gl::Uniform1i(uTex_, 0);
    gl::Uniform1i(uMode_, mode);
    gl::Uniform4f(uTint_, tint[0], tint[1], tint[2], tint[3]);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, tex ? tex : whiteTex_);
    gl::Enable(gl::BLEND);
    if (mode == 1)
        gl::BlendFunc(gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
    else if (mode == 5)   // (vrfade: the PC fade above 1: the picture plus itself times the tint, as Hud::pcFadeDraw)
        gl::BlendFunc(gl::DST_COLOR, gl::ONE);
    else
        gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::BindVertexArray(spriteVao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, spriteVbo_);
    gl::BufferSubData(gl::ARRAY_BUFFER, 0, gl::GLsizeiptr(size_t(count) * 5 * sizeof(float)), verts);
    gl::DrawArrays(gl::TRIANGLE_STRIP, 0, count);
}

// vrfade (vr_scene.h): the frame's full-screen fades over the bound framebuffer's whole viewport, as the flat picture
// has them over the whole window (Hud::draw: the white fade at its alpha over the 3D and the cover under the 2D layer;
// Hud::pcFadeDraw over everything). The eyes: the whole view, not the 4:3 panel's rectangle in it
void Scene::drawFades(bool under)
{
    static const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float v[20] = {-1, -1, 0, 0, 0, 1, -1, 0, 1, 0, -1, 1, 0, 0, 1, 1, 1, 0, 1, 1};
    if (under) {
        if (fadeWhite_ > 0.0f) {
            const float tint[4] = {1, 1, 1, fadeWhite_};
            drawSprite(v, 4, 0, 0, tint, ident);
        }
        if (fadeCover_) {   // (opaque; the panel's own cover cells over it are the same colour)
            const float c = fadeCover_ == 2 ? 1.0f : 0.0f, tint[4] = {c, c, c, 1};
            drawSprite(v, 4, 0, 0, tint, ident);
        }
    } else if (fadePc_ < 0.998f) {   // (Hud::pcFadeDraw: black over it at 1 - level)
        const float tint[4] = {0, 0, 0, std::round((1.0f - std::max(fadePc_, 0.0f)) * 255.0f) / 255.0f};
        drawSprite(v, 4, 0, 0, tint, ident);
    } else if (fadePc_ > 1.002f) {   // (the picture plus itself times level - 1)
        const float a = std::round(std::min(fadePc_ - 1.0f, 1.0f) * 255.0f) / 255.0f, tint[4] = {a, a, a, 1};
        drawSprite(v, 4, 0, 5, tint, ident);
        gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    }
}

bool Scene::playerInControl(const game::Runtime& rt)
{
    if (rt.mem.at<int16_t>(game::A::G::game_mode) != 4 || rt.clear.active || rt.mem.u32(game::A::G::shot_enable) == 0)
        return false;
    const uint32_t sub = rt.mem.u32(game::A::G::game_submode);
    if (sub == 7 || sub == 8)
        return false;   // (the ending: the farewell scene, the staff roll)
    const int32_t st = rt.mem.at<port::a::Player>(rt.player_rec(0)).state;
    return st == 5 || st == 8;
}

void Scene::frame(const Frame& f)
{
    if (!backend_ || !f.view || !f.rt)
        return;
    const game::Runtime& rt = *f.rt;
    const float upm = settings.unitsPerMetre;
    backend_->setOutputSize(f.outW, f.outH);
    backend_->setDual(dual);   // (dual pistols: the preview points the left hand too)
    noteFrame();
    backend_->beginFrame(fs_);
    flatFrame_ = false;
    if (lastPanel_) {   // vrflow: from the big screen: the buttons still held (the click on Arcade) are no new presses
        lastPanel_ = false;
        triggerDown_ = wasDown_ = fs_.hand[1].trigger > 0.4f;
        triggerDownL_ = wasDownL_ = fs_.hand[0].trigger > 0.4f;
        for (int h = 0; h < 2; ++h) {
            const Controller& c = fs_.hand[h];
            gripDown_[h] = c.squeeze > 0.4f;
            wasReload_[h] = gripDown_[h] || c.secondary || (h == 1 && c.primary);
        }
        wasStart_ = fs_.hand[0].primary;
        wasMenu_ = fs_.hand[0].menu || fs_.hand[1].menu;
        pressHeld_ = true;
    }
    frame_ = &f;   // (vrfix: name entry / cinema-screen aim)
    screenAim_ = screenAimL_ = ScreenAim{};
    screenSight_ = false;
    cinemaHands_.clear();

    // the 2D HUD into the panel (the game area's shape, 960 lines)
    const float halfW = std::max(192.0f, f.halfW);
    const int ph = settings.hudLines, pw = int(std::lround(float(ph) * halfW / 192.0f));   // (vr.ini hud_lines: 960)
    ensurePanel(pw, ph);
    gl::BindFramebuffer(gl::FRAMEBUFFER, panelFbo_);
    gl::Viewport(0, 0, pw, ph);
    gl::ClearColor(0, 0, 0, 0);
    gl::Clear(gl::COLOR_BUFFER_BIT);
    // vr3 (docs/vr.md "Cut-scenes"): world (the default): the cut-scene plays around you, so no bars on the screen plane
    // (they cut a band out of the view); cinema: the flat picture as the arcade shows it, bars and all, on a big screen
    // vrend (user 2026-11-10): the full-screen 2D screens (the chapter cards, the staff roll, GAME OVER) always on the
    // cinema screen: big and centred, the room dark around (they are made for the 4:3 screen alone)
    const bool card = game::Hud::vrScreen2D(rt);
    // vrcomfort (view = screen): everything on the cinema screen, play too (screenView: the guns aim at the screen)
    const bool screenView = settings.view == 1;
    bool cinema = card || (f.cutscene && settings.cutscenes == 1) || screenView;
    if (f.hud) f.hud->noBars = f.cutscene && !cinema;
    // vrfade (user 2026-11-10: "the white flash doesn't account for the whole eye"): the full-screen fades left out of
    // the panel (it is only the 4:3 screen's rectangle in the view), drawn over each eye's whole view (drawFades)
    if (f.hud) {
        const game::Hud::ScreenFade sf = game::Hud::screenFade(rt);
        fadeWhite_ = sf.white, fadeCover_ = sf.cover, fadePc_ = sf.pcLevel;
        f.hud->vrFades = true;
    } else {
        fadeWhite_ = 0.0f, fadeCover_ = 0, fadePc_ = 1.0f;
    }
    if (fadeWhite_ > 0.0f || fadeCover_ || fadePc_ < 0.998f || fadePc_ > 1.002f) {
        ++test.fadeFrames;
        static const bool trace = std::getenv("HOTD_VR_FADE_TRACE") != nullptr;   // (tests: the frames to shoot)
        if (trace)
            SDL_Log("vr fade: frame %llu white %.3f cover %d pc %.3f", (unsigned long long)rt.frameCount, fadeWhite_,
                    fadeCover_, fadePc_);
    }
    if (f.drawHud) f.drawHud(pw, ph);
    if (f.hud) f.hud->noBars = false, f.hud->vrFades = false;
    // vrend: the attract / title flow's full-screen 2D (the date card, the title logo, the ranking...): known from the
    // 2D just drawn, so after the HUD (its 3D demo scenes stay around you)
    if (!cinema && f.hud && f.hud->vrAttract2D(rt)) cinema = true;
    cinemaNow = cinema;
    gl::BindTexture(gl::TEXTURE_2D, panelTex_);
    gl::GenerateMipmap(gl::TEXTURE_2D);
    unsigned sightTex = 0;
    float sightAspect = 1.0f;
    const bool sight = f.hud && f.hud->vrSight(rt, 0, ph, sightTex, sightAspect);
    // vrattract (user 2026-11-10: "You should not be able to see hands during the attract demo. Only in gameplay."):
    // the hands, the lasers and the 3D crosshairs only while player 1 is in control (playerInControl); the guns still
    // aim (the pause's items, the attract's start)
    const bool paused = f.hud && f.hud->pauseSelection >= 0;
    // (2 frames on before they show: a chapter's first frame has the shots on for one frame under the bars, before the
    // cut-scene's scene_state 5 turns them off; the pause: the gun points at its items)
    controlRun_ = playerInControl(rt) ? std::min(controlRun_ + 1, 2) : 0;
    // vrfix (user 2026-11-10: hands also in the first-person cut-scenes, through the player's own eyes: meeting Sophie,
    // the Hangedman scene, a researcher handing you something; not in the third-person ones, Rogan / G seen from
    // outside): in game (mode 4, not the ending's sub-modes 7 / 8, no chapter clear, no full-screen 2D), a cut-scene in
    // the world (not on the cinema screen) whose shots show no player body: none of the player skeletons' hands (0x719
    // the gun hand of every player skeleton, 0x717 Rogan's left, 0x36B the empty hand) in the game's draw list for 15
    // frames running (firstPersonRun_, counted in the draw below: a body seen hides them at once)
    const uint32_t subNow = rt.mem.u32(game::A::G::game_submode);
    // (vrcomfort: view = screen: the cut-scene on the screen, the hands in front of it the same way: no 2-frame blink
    // into control after a first-person shot, as handblink in the world)
    const bool cutInGame = f.cutscene && (!cinema || (screenView && !card)) && rt.mem.at<int16_t>(game::A::G::game_mode) == 4 && subNow != 7 &&
                           subNow != 8 && subNow < 9 && !rt.clear.active;
    if (!cutInGame) firstPersonRun_ = 0;
    const bool firstPersonCut = cutInGame && firstPersonRun_ >= 15;
    const bool showAim = controlRun_ >= 2 || paused;   // (the lasers and 3D crosshairs: not in a cut-scene)
    // (a first-person cut-scene straight into control: the hands stay on through controlRun_'s first frame, no blink)
    const bool showHands = showAim || firstPersonCut || (firstPersonLast_ && controlRun_ >= 1);
    firstPersonLast_ = firstPersonCut || (firstPersonLast_ && controlRun_ == 1);
    {
        // --vr-preview-test's check, from the game's state on its own (not playerInControl's shots-on): in control =
        // in game (mode 4), playing (scene_state 2: the bars gone), player 1 playing (state 5), no chapter clear / ending
        const uint32_t sub = rt.mem.u32(game::A::G::game_submode);
        const bool inGame = rt.mem.at<int16_t>(game::A::G::game_mode) == 4 && sub != 7 && sub != 8 && !rt.clear.active;
        const int32_t st = rt.mem.at<port::a::Player>(rt.player_rec(0)).state;
        const bool playing = inGame && rt.mem.at<uint8_t>(game::A::G::scene_state) == 2 && st == 5;
        // (outside: a cut-scene with the shots off; Chapter 1's sewer slugs come with the bars on and the shots on)
        const bool outside = !inGame || (f.cutscene && rt.mem.u32(game::A::G::shot_enable) == 0) || (st != 5 && st != 8);
        if (paused) ++test.handsPaused;
        else if (showHands && outside && firstPersonCut) ++test.handsFirstPerson;   // (vrfix: by design)
        else if (showHands && outside) ++test.handsShownOutside;
        // (counted: hidden with player 1 in control for 2 frames (controlRun_'s by-design 1-frame wait at a hand-over is
        // not a fault: Chapter 1's last frame before the clear), or a blink: shown the frame before (handblink))
        else if (!showHands && playing && (controlRun_ >= 2 || handsShownPrev_)) {
            ++test.handsHiddenInControl;
            if (std::getenv("HOTD_VR_HANDS_TRACE"))
                SDL_Log("vr hands: frame %llu hidden in control (controlRun %d, cut %d, firstPersonLast %d, chapter clear %d)",
                        (unsigned long long)rt.frameCount, controlRun_, int(f.cutscene), int(firstPersonLast_), int(rt.clear.active));
        }
        if (showHands) ++test.handsShown;
        handsShownPrev_ = showHands;
        static const bool trace = std::getenv("HOTD_VR_HANDS_TRACE") != nullptr;
        static int last = -1;
        if (trace && int(showHands) != last)
            SDL_Log("vr hands: frame %llu %s (mode %d sub %u scene %d shots %u player %d)", (unsigned long long)rt.frameCount,
                    showHands ? "shown" : "hidden", int(rt.mem.at<int16_t>(game::A::G::game_mode)), sub,
                    int(rt.mem.at<uint8_t>(game::A::G::scene_state)), rt.mem.u32(game::A::G::shot_enable), int(st));
        last = int(showHands);
    }

    // the newest game camera: the aim's reference (as the game tests its shots)
    float gr[9], gp[3];
    game::GameView::cameraToWorld(rt.camera, gr, gp);
    const float gyaw = std::atan2(gr[2], gr[8]), gpitch = std::atan2(gr[5], std::sqrt(gr[2] * gr[2] + gr[8] * gr[8]));
    GameCamera gcam = gameCameraFrom(gp, gyaw, gpitch, rt.focal);
    gcam.halfW = halfW;
    const float worldUp[3] = {0, 1, 0};
    // vr4: "pointing down" (reload_down, the off-screen state) is the player's own down: the reference frame's up (the
    // player's space, gravity's up in the headset), not the world's. In follow mode the game camera can look steeply
    // down (the Hangedman's fight: pitch -60 to -90 degrees): with the world's up, aiming at the screen's middle was
    // "pointing down" and every pull reloaded (102 of 2127 round trips in a Hangedman Boss Mode run)
    float playerUp[3] = {0, 1, 0};

    game::GameView& view = *f.view;
    const bool clearWas = view.opt.clearColor;
    view.opt.clearColor = true;
    Rigid ref;
    // the guns: [1] the right (always), [0] the left (dual pistols only); each its muzzle, barrel, how far its ray goes
    struct Gun {
        bool on = false;
        float o[3] = {0, 0, 0}, d[3] = {0, 0, 1}, dist = 0, hit[3] = {0, 0, 0};
        Pull pull = Pull::Shoot;   // vr4: what a pull at its aim does now (offscreen_reload; Ignore: the sight greyed)
    } gun[2];
    auto classify = [&](Gun& g) {
        g.pull = pullAction(aimFromRay(gcam, g.o, g.d, g.dist, playerUp, settings.reloadPitch), gcam.halfW, gcam.halfH,
                            settings.offscreenReload);
    };
    for (Gun& g : gun) g.dist = settings.aimDistance * upm;
    float* gunO = gun[1].o;
    float* gunD = gun[1].d;
    const bool dual = this->dual;
    view.drawOverride = [&](const std::vector<DrawItem>& items) {
        const Camera& base = view.camOverride ? *view.camOverride : view.cam;
        // vrfix (user's playthrough 2026-11-10, 16:06-16:09: the Hangedman's end, a dark-blue void below, then the sky
        // upside down): the arcade camera tips past straight down there; its heading (from a nearly vertical forward)
        // swings round and the player's space, following its full pitch, put the floorless underside in front of the
        // eyes. The player's space now never pitches more than 60 degrees (the horizon stays in reach of a turned
        // head) and keeps its last heading while the game camera is within 15 degrees of vertical (no 180-degree swing)
        Camera refCam = base;
        constexpr float kVertical = 75.0f * kPi / 180.0f, kMaxPitch = 60.0f * kPi / 180.0f;
        if (std::abs(refCam.pitch) > kVertical && refYawSet_) refCam.yaw = refYaw_;
        else refYaw_ = refCam.yaw, refYawSet_ = true;
        if (std::abs(base.pitch) > kMaxPitch) ++test.refPitchClamped;
        refCam.pitch = std::clamp(refCam.pitch, -kMaxPitch, kMaxPitch);
        ref = referenceFrom(refCam, settings.follow);
        ref.axis(1, playerUp);
        static const bool camTrace = std::getenv("HOTD_VR_CAM_TRACE") != nullptr;   // (vrattract: the eye vs the game camera)
        if (camTrace) {
            const Rigid eyeW = ref * poseToLocal(fs_.eye[1].pose, upm);
            float ef[3];
            eyeW.axis(2, ef);
            SDL_Log("vr cam: frame %llu mode %d game pos %.1f %.1f %.1f yaw %.1f pitch %.1f fov %.1f%s | eye pos %.1f %.1f "
                    "%.1f fwd %.3f %.3f %.3f | hands %d",
                    (unsigned long long)rt.frameCount, int(rt.mem.at<int16_t>(game::A::G::game_mode)), base.pos[0],
                    base.pos[1], base.pos[2], base.yaw * 180.0f / kPi, base.pitch * 180.0f / kPi, base.fovY * 180.0f / kPi,
                    view.camOverride ? " (override)" : "", eyeW.t[0], eyeW.t[1], eyeW.t[2], ef[0], ef[1], ef[2],
                    int(showHands));
        }
        Camera cam = base;
        cam.perPixelDepth = true;   // (VR: the per-pixel depth: each eye sorting polygons its own way would differ)
        cam.keyBand = kKeyBand;     // (vrnull: but the flat game's key band: the free camera's narrow one sank the
                                    // opening's zombie and researcher into the kerb, user 2026-11-09)
        // comfort: how fast the reference turns (degrees / s) -> the vignette
        float fwd[3];
        ref.axis(2, fwd);
        if (prevTime_ && fs_.displayTime > prevTime_) {
            const float dt = float(double(fs_.displayTime - prevTime_) / 1e9);
            const float step = std::acos(std::clamp(dot3(fwd, prevFwd_), -1.0f, 1.0f)) * 180.0f / kPi;
            const float moved[3] = {ref.t[0] - prevPos_[0], ref.t[1] - prevPos_[1], ref.t[2] - prevPos_[2]};
            // bugs7: a camera cut (the game's camera put somewhere else in one frame: a new shot of a cut-scene, the
            // attract's next scene, a chapter's start) is no turn the player rides: the vignette only reacts to smooth
            // turns. A cut: over 20 degrees, or over 2.5 m, from one step to the next (the game's fastest smooth turns,
            // the Hermit's / the Magician's swoops, stay under 6 degrees a frame)
            const bool cut = step > 20.0f || std::sqrt(dot3(moved, moved)) > 2.5f * upm;
            if (cut) {
                if (settings.vignette) test.vignetteCuts++;
            } else {
                const float ang = step / std::max(dt, 1e-3f);
                const float want = settings.vignette ? std::clamp((ang - settings.vignetteTurn) / settings.vignetteTurn, 0.0f, 1.0f) : 0.0f;
                vignetteNow += (want - vignetteNow) * std::min(1.0f, dt * (want > vignetteNow ? 12.0f : 3.0f));
            }
            if (vignetteNow > 0.5f) test.vignetteOnFrames++;
        }
        std::copy(fwd, fwd + 3, prevFwd_);
        std::copy(ref.t, ref.t + 3, prevPos_);
        prevTime_ = fs_.displayTime;

        // the hands: the game's own models (no models of ours), in game units as the world (their size is the game's);
        // the gun hand on the right controller's aim pose with its barrel on the aim ray, the left hand on the left
        // grip pose; vr.ini's gun_* / left_hand_* move them from there
        auto tune = [&](const float offset[3], const float rotate[3]) {
            Pose p;
            std::copy(offset, offset + 3, p.pos);
            quatFromYawPitchRoll(rotate[1] * kPi / 180.0f, rotate[0] * kPi / 180.0f, rotate[2] * kPi / 180.0f, p.rot);
            p.valid = true;
            return poseToLocal(p, upm);
        };
        const bool right = fs_.hand[1].active && fs_.hand[1].aim.valid;
        gun[1].on = right;
        gun[0].on = dual && fs_.hand[0].active && fs_.hand[0].aim.valid;
        // the left gun's tune: gun_offset / gun_rotate mirrored (x, yaw and roll the other way)
        const float offL[3] = {-settings.gunOffset[0], settings.gunOffset[1], settings.gunOffset[2]};
        const float rotL[3] = {settings.gunRotate[0], -settings.gunRotate[1], -settings.gunRotate[2]};
        const Rigid gunW = ref * poseToLocal(fs_.hand[1].aim, upm) * tune(settings.gunOffset, settings.gunRotate) * gunAnchor();
        const Rigid gunLW = ref * poseToLocal(fs_.hand[0].aim, upm) * tune(offL, rotL) * gunAnchorLeft();
        for (int h = 0; h < 2; ++h) {
            const Rigid& w = h == 1 ? gunW : gunLW;
            float ahead[3];
            w.apply(kGunMuzzle, gun[h].o);
            const float back[3] = {kGunMuzzle[0], kGunMuzzle[1] + 1.0f, kGunMuzzle[2]};   // (the barrel: model -y)
            w.apply(back, ahead);
            for (int k = 0; k < 3; ++k) gun[h].d[k] = gun[h].o[k] - ahead[k];
            normalize3(gun[h].d);
        }
        const bool left = !dual && settings.leftHand && fs_.hand[0].active && fs_.hand[0].grip.valid;
        const Rigid leftW = ref * poseToLocal(fs_.hand[0].grip, upm) * tune(settings.leftOffset, settings.leftRotate) * leftAnchor();
        std::vector<DrawItem> hands;
        DrawItem hi;
        if (showHands && right && view.placedItem(0, kGunHandModel, gunW.r, gunW.t, hi))
            hands.push_back(hi);
        if (showHands && gun[0].on && view.placedItem(1, kGunHandModel, gunLW.r, gunLW.t, hi))   // (dual pistols: mirrored)
            hands.push_back(hi);
        // (questfb, user 2026-11-10 on the Quest: the empty left hand near the face is drawn again, as the gun hand is:
        // vrfix's hiding within 25 cm reverted)
        if (showHands && left && view.placedItem(1, kLeftHandModel, leftW.r, leftW.t, hi))
            hands.push_back(hi);
        RenderOptions handOpt = view.opt;
        handOpt.clearColor = false;   // (over the eye's picture; their own depth: never inside a wall)

        if (cutInGame) {   // vrfix: a first-person shot: no player body drawn (firstPersonCut, above)
            const bool body = std::any_of(items.begin(), items.end(), [](const DrawItem& it) {
                return it.placement && (it.placement->id == 0x719 || it.placement->id == 0x717 || it.placement->id == 0x36B ||
                                        it.placement->id == 0xF1000719u);
            });
            firstPersonRun_ = body ? 0 : std::min(firstPersonRun_ + 1, 15);
        }
        if (cinema) {   // vr3: the cut-scene on the cinema screen; the guns keep their rays (the aim at the cut's end)
            for (Gun& g : gun)
                for (int k = 0; k < 3; ++k) g.hit[k] = g.o[k] + g.d[k] * g.dist;
            // vrfix: the name entry (mode 4 sub-modes 9 / 10; Hud::vrScreen2D puts it here): the flat game's light-gun
            // screen, so the gun aims at the cinema screen itself: its ray (seat space: the screen's own) meets the
            // screen's plane, that point is the arcade's aim (drawCinema draws the sight there, the laser to it and the
            // hands in front of the screen)
            const uint32_t sub = rt.mem.u32(game::A::G::game_submode);
            const bool nameEntry = rt.mem.at<int16_t>(game::A::G::game_mode) == 4 && (sub == 9 || sub == 10);
            // vrcomfort (view = screen): all of play the same way: each gun's ray meets the screen, that point is its
            // aim; off the screen as the world's guns (below it / pointed down: a reload; beside: offscreen_reload)
            const int ch = settings.hudLines, cw = int(std::lround(float(ch) * halfW / 192.0f));   // (flatPicture's)
            const float D = settings.cinemaDistance * upm, hw = settings.cinemaWidth * 0.5f * upm, hh = hw * float(ch) / float(cw);
            if (nameEntry || screenView) {
                screenAim_.on = true;
                screenSight_ = nameEntry || showAim;
                auto meet = [&](const Rigid& seatG, ScreenAim& s) {
                    float o[3], b[3], d[3];
                    seatG.apply(kGunMuzzle, o);
                    const float back[3] = {kGunMuzzle[0], kGunMuzzle[1] + 1.0f, kGunMuzzle[2]};
                    seatG.apply(back, b);
                    for (int k = 0; k < 3; ++k) d[k] = o[k] - b[k];
                    normalize3(d);
                    float t = settings.aimDistance * upm;
                    if (d[2] > 1e-4f && (D - o[2]) / d[2] > 0.0f) {
                        t = (D - o[2]) / d[2];
                        s.hit = true;
                        s.x = (o[0] + d[0] * t) / hw * halfW;
                        s.y = (o[1] + d[1] * t) / hh * 192.0f;
                    } else {
                        s.x = d[0] < 0 ? -4000.0f : 4000.0f;   // (pointed away from the screen)
                    }
                    s.down = d[1] < -std::sin(settings.reloadPitch * kPi / 180.0f);   // (the seat's up: the player's own)
                    for (int k = 0; k < 3; ++k) s.o[k] = o[k] / upm, s.p[k] = (o[k] + d[k] * t) / upm;
                };
                const Rigid seatGun = poseToLocal(fs_.hand[1].aim, upm) * tune(settings.gunOffset, settings.gunRotate) * gunAnchor();
                const Rigid seatLeft = poseToLocal(fs_.hand[0].grip, upm) * tune(settings.leftOffset, settings.leftRotate) * leftAnchor();
                const Rigid seatGunL = poseToLocal(fs_.hand[0].aim, upm) * tune(offL, rotL) * gunAnchorLeft();
                if (right) meet(seatGun, screenAim_);
                if (gun[0].on) screenAimL_.on = true, meet(seatGunL, screenAimL_);   // (dual pistols: the left gun)
                // (the hands' texture set: the model's own; the name entry's area 2 and its colour rows 0xC-0xE turned
                // them gold and red)
                if (showHands && right && view.placedItem(0, kGunHandModel, seatGun.r, seatGun.t, hi))
                    hi.variant = hi.model->preferredVariant, cinemaHands_.push_back(hi);
                if (showHands && gun[0].on && view.placedItem(1, kGunHandModel, seatGunL.r, seatGunL.t, hi))
                    hi.variant = hi.model->preferredVariant, cinemaHands_.push_back(hi);
                if (showHands && left && view.placedItem(1, kLeftHandModel, seatLeft.r, seatLeft.t, hi))
                    hi.variant = hi.model->preferredVariant, cinemaHands_.push_back(hi);
            }
            if (screenView) {
                if (controlRun_ >= 2) ++test.screenPlayFrames;
                // --vr-preview-test (view = screen): the gun points where the cursor was put last frame (the preview's
                // gun follows the cursor a frame late): its aim on the screen against that aim
                if (followGameAim && screenTargetSet_ && screenAim_.hit) {
                    const double err = std::max(std::abs(screenAim_.x - screenTarget_[0]), std::abs(screenAim_.y - screenTarget_[1]));
                    ++test.screenChecked;
                    test.screenSum += err;
                    test.screenWorst = std::max(test.screenWorst, err);
                    if (err > 2.0) ++test.screenOver;
                }
                screenTargetSet_ = false;
                if (followGameAim) {   // the cursor onto the game's own aim on the screen
                    const float ax = testAimSet ? testAim[0] : rt.gun[0].x, ay = testAimSet ? testAim[1] : rt.gun[0].y;
                    if (std::abs(ax) < halfW && std::abs(ay) < 192.0f) {
                        const float P[3] = {ax / halfW * hw / upm, ay / 192.0f * hh / upm, D / upm};   // (seat, metres)
                        Rigid eyeL;
                        float vp[16], c[4];
                        seatViewProj(fs_.eye[1], eyeL, vp);
                        for (int r = 0; r < 4; ++r) c[r] = vp[r] * P[0] + vp[4 + r] * P[1] + vp[8 + r] * P[2] + vp[12 + r];
                        if (c[3] > 0) {
                            backend_->setCursor(1, (c[0] / c[3]) * 0.5f + 0.5f, 0.5f - (c[1] / c[3]) * 0.5f);
                            screenTarget_[0] = ax, screenTarget_[1] = ay, screenTargetSet_ = true;
                        }
                    }
                }
                // the preview: the cursor's ray as far as the screen (its gun aims at the point the cursor shows)
                float co[3], cd[3];
                if (backend_->desktopCursorRay(co, cd) && -cd[2] > 1e-4f)
                    backend_->cursorDistance((D / upm + co[2]) / -cd[2]);   // (tracking -z = the seat's +z)
            }
            drawCinema(view, items, base, halfW);
            ++test.cinemaFrames;
            return;
        }

        // vr4 (docs/vr.md "The HUD and close creatures"): the HUD's screen plane is drawn over everything (no depth
        // test): a creature nearer than it would be painted over (the eyes converge on the creature, the HUD says it is
        // behind: eye strain, and HOTD's grabs come to ~1 m). Adaptive: the plane eases to just in front of the nearest
        // creature inside it (0.25 m before its nearest side, not nearer than hud_min_distance), its angular size kept
        // (the frustum's size at that distance); back out slowly when it is gone
        const float panelFocal = 192.0f / std::tan(base.fovY / 2);
        float panelD = settings.panelDistance;
        {
            const float nearU = nearestCreature(items, ref, halfW / panelFocal, 192.0f / panelFocal);
            const float nearM = nearU >= 0.0f ? nearU / upm : -1.0f;
            float target = settings.panelDistance;
            if (settings.hudDistance == 1 && nearM >= 0.0f)
                target = std::clamp(nearM - 0.25f, std::min(settings.hudMinDistance, settings.panelDistance), settings.panelDistance);
            const float dt = hudTime_ && fs_.displayTime > hudTime_ ? float(double(fs_.displayTime - hudTime_) / 1e9) : 0.0f;
            hudTime_ = fs_.displayTime;
            panelD = settings.hudDistance == 1 ? hudDist_.update(target, dt) : settings.panelDistance;
            if (nearM >= 0.0f) {   // (tests: a creature in the panel's view nearer than the plane: drawn over it)
                ++test.hudCreatureFrames;
                if (nearM < settings.panelDistance) ++test.hudOverFixed;
                if (nearM < settings.hudMinDistance + 0.25f) ++test.hudTooNear;   // (nearer than the panel may come)
                else if (nearM < panelD) ++test.hudOverNow;
            }
            if (panelD < settings.panelDistance - 0.3f) ++test.hudNearFrames;   // (pulled in by more than 0.3 m)
            test.hudNearest = std::min(test.hudNearest, panelD);
        }
        panelNow = panelD;

        // vr4 (docs/vr.md "Shot flash"): the game draws the shot's flash (0x95140) and its spark (0x95630) at the near
        // point 1 unit in front of the game camera (10 cm: the flat picture's size for them). In VR that is right in
        // front of your eyes, before the target (a double image, the eyes crossing). Moved out along the game camera's
        // ray through them to where that ray meets the world (the CPU ray test: the world is not drawn yet), scaled by
        // the same factor (the same size to the game camera: the flat picture's look). The spark flying past the hit
        // goes on as the game moves it (behind the target, as in the flat game)
        // vrfix (user's playthrough 2026-11-10, 28:54 / 29:27: big opaque beige octagons over the view): the Magician's
        // fireball flies to a point 4 units (40 cm) in front of the game camera and bursts there at 1.5 times its size
        // (magician_fireball_aim / _frame): the flat picture's fireball in your face, but in VR 40 cm from the eyes,
        // each eye seeing the model's polygons from its own side. Moved out the same way to at least 1.2 m (the same
        // size to the game camera: the flat look), with its trail copies
        const float fireNear = 1.2f * upm;
        auto fireball = [](const DrawItem& it) {
            const uint32_t id = it.placement ? it.placement->id : 0;
            return (id >= 0x1B98 && id <= 0x1BBE) || (id >= 0x1CAB && id <= 0x1CB9);   // (magician_fireball's models)
        };
        auto nearFire = [&](const DrawItem& it) {
            if (!fireball(it)) return false;
            const float* q = it.placement->pos;
            const float dv[3] = {q[0] - base.pos[0], q[1] - base.pos[1], q[2] - base.pos[2]};
            return dot3(dv, dv) < fireNear * fireNear;
        };
        const std::vector<DrawItem>* drawItems = &items;
        if ((settings.shotFlashAtHit && std::any_of(items.begin(), items.end(), [](const DrawItem& it) { return it.vrKind == 2 || it.vrKind == 3; })) ||
            std::any_of(items.begin(), items.end(), nearFire)) {
            fxItems_ = items;
            fxPlaces_.clear();
            fxPlaces_.reserve(items.size());
            const float* C = base.pos;
            for (DrawItem& it : fxItems_) {
                if (nearFire(it)) {   // vrfix: the fireball out to 1.2 m from the game camera, scaled to keep its look
                    Placement p = *it.placement;
                    float dir[3] = {p.pos[0] - C[0], p.pos[1] - C[1], p.pos[2] - C[2]};
                    const float d0 = std::max(std::sqrt(dot3(dir, dir)), 1e-3f);
                    for (float& v : dir) v /= d0;
                    const float k = fireNear / d0;
                    for (int i = 0; i < 3; ++i) p.pos[i] = C[i] + dir[i] * fireNear;
                    for (float& r : p.rot) r *= k;
                    fxPlaces_.push_back(p);
                    it.placement = &fxPlaces_.back();
                    ++test.fireMoved;
                    continue;
                }
                if (!settings.shotFlashAtHit || (it.vrKind != 2 && it.vrKind != 3) || !it.placement)
                    continue;
                Placement p = *it.placement;
                float dir[3] = {p.pos[0] - C[0], p.pos[1] - C[1], p.pos[2] - C[2]};
                const float d0 = std::sqrt(dot3(dir, dir));
                if (d0 < 1e-3f)
                    continue;
                for (float& v : dir) v /= d0;
                float t = cpuRay(view, items, C, dir, 60.0f * upm);
                if (t <= 0.0f) t = settings.aimDistance * upm;   // (into the sky: the aim's fixed distance)
                const float want = 0.97f * t;   // (just in front of the surface)
                if (d0 >= want)
                    continue;   // (the spark past the hit: as the game moves it)
                const float k = want / d0;
                for (int i = 0; i < 3; ++i) p.pos[i] = C[i] + dir[i] * want;
                for (float& r : p.rot) r *= k;
                fxPlaces_.push_back(p);
                it.placement = &fxPlaces_.back();   // (reserved: the pointers stay)
                ++test.fxMoved;
                test.fxDistSum += double(want / upm);
            }
            drawItems = &fxItems_;
        }

        // the dominant (right) eye first: its depth tells how far the gun's ray and the preview's cursor go
        for (int i : {1, 0}) {
            const Eye& e = fs_.eye[i];
            const Rigid eyeW = ref * poseToLocal(e.pose, upm);
            Renderer::EyeView ev;
            eyeW.viewMatrix(ev.view);
            ev.tanLeft = e.fov.tanLeft, ev.tanRight = e.fov.tanRight, ev.tanUp = e.fov.tanUp, ev.tanDown = e.fov.tanDown;
            ev.nearZ = cam.nearZ;
            gl::BindFramebuffer(gl::FRAMEBUFFER, backend_->eyeFramebuffer(i));
            view.renderer.eye = &ev;
            view.renderer.draw(*drawItems, cam, e.width, e.height, view.opt);
            view.renderer.eye = nullptr;
            float vp[16];
            std::memcpy(vp, view.renderer.lastViewProj, sizeof vp);
            // vrnull (2026-11-10): the right eye's world once more for the aim, with the free camera's narrow key band
            // (the eyes' 0.2 band, which keeps the opening's feet over the kerb, pulls their depth toward the sort
            // keys: the gun's rays read surfaces up to 20 % off). The rays below read this depth (march: the bound
            // framebuffer); the eye's own framebuffer is bound again for the hands.
            // (modfix 2026-11-10: at the eye's own size, no longer half: at half size the rays at grazing angles and
            // outlines read pixels twice as coarse: --vr-preview-test 25 of 1136 round trips over 6 units = 2.2 %, over
            // the 2 % rule. With march's per-pixel checks, over 6 units (full / half size): 3000 frames 17 / 1185 =
            // 1.4 % / 19 / 1136; 2200 frames 9 / 810 / 6 / 753; cinema 21 / 1481 / 15 / 1309; dual 18 / 1216 = 1.5 % /
            // 23 / 1110 = 2.1 %: half size fails the dual run)
            const bool aimPass = i == 1 && depthReadable_ && settings.aimDepth != 3;
            if (aimPass) {
                if (!aimFbo_ || aimW_ != std::max(1, e.width) || aimH_ != std::max(1, e.height)) {
                    if (aimTex_) gl::DeleteTextures(1, &aimTex_);
                    if (aimDepthRb_) gl::DeleteRenderbuffers(1, &aimDepthRb_);
                    if (aimFbo_) gl::DeleteFramebuffers(1, &aimFbo_);
                    aimW_ = std::max(1, e.width), aimH_ = std::max(1, e.height);
                    gl::GenFramebuffers(1, &aimFbo_);
                    gl::BindFramebuffer(gl::FRAMEBUFFER, aimFbo_);
                    gl::GenTextures(1, &aimTex_);
                    gl::BindTexture(gl::TEXTURE_2D, aimTex_);
                    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, aimW_, aimH_, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
                    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, aimTex_, 0);
                    gl::GenRenderbuffers(1, &aimDepthRb_);
                    gl::BindRenderbuffer(gl::RENDERBUFFER, aimDepthRb_);
                    gl::RenderbufferStorage(gl::RENDERBUFFER, gl::DEPTH_COMPONENT24, aimW_, aimH_);
                    gl::FramebufferRenderbuffer(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::RENDERBUFFER, aimDepthRb_);
                    gl::framebufferOk("vr aim depth");
                }
                gl::BindFramebuffer(gl::FRAMEBUFFER, aimFbo_);
                Camera aimCam = cam;
                aimCam.keyBand = kKeyBandFreeCam;
                view.renderer.eye = &ev;
                view.renderer.draw(*drawItems, aimCam, aimW_, aimH_, view.opt);
                view.renderer.eye = nullptr;
            }
            const int mw = aimPass ? aimW_ : e.width, mh = aimPass ? aimH_ : e.height;   // (the depth the rays read)
            // vr4 (vr.ini aim_depth, docs/vr.md "Aim depth"): how far a ray goes: the right eye's depth (desktop
            // OpenGL), or the CPU ray test against the stage's triangles and the creatures' parts (OpenGL ES / ANGLE:
            // the depth cannot be read; aim_depth = cpu: forced, a desktop test of the Quest's path). With the CPU
            // test on a desktop the eye's depth is asked too and the two compared (tests)
            auto depthRay = [&](const float o[3], const float d[3], float maxT) {
                if (settings.aimDepth == 3)
                    return -1.0f;   // (fixed: aim_distance always, as OpenGL ES had before: the tests' baseline)
                const bool cpu = settings.aimDepth == 2 || (settings.aimDepth == 0 && !depthReadable_);
                if (!cpu)
                    return march(o, d, vp, mw, mh, maxT);
                const uint64_t c0 = followGameAim ? SDL_GetTicksNS() : 0;
                const float tc = cpuRay(view, items, o, d, maxT);
                if (followGameAim) test.cpuNs += SDL_GetTicksNS() - c0;
                if (followGameAim && depthReadable_) {
                    const float td = march(o, d, vp, mw, mh, maxT);
                    if (td > 0 || tc > 0) {
                        ++test.cpuCompared;
                        if (td > 0 && tc > 0 && std::abs(td - tc) <= std::max(0.05f * td, 0.2f * upm)) ++test.cpuAgree;
                        else ++test.cpuWhy[tc <= 0 ? 0 : td <= 0 ? 1 : tc < td ? 2 : 3];
                    }
                }
                return tc;
            };
            if (i == 1) {
                for (int h : {1, 0}) {   // each gun's ray in the right eye's depth (dual pistols: the left one too)
                    Gun& g = gun[h];
                    const float t = g.on ? depthRay(g.o, g.d, 60.0f * upm) : -1.0f;
                    g.dist = t > 0 ? t : settings.aimDistance * upm;
                    for (int k = 0; k < 3; ++k) g.hit[k] = g.o[k] + g.d[k] * g.dist;
                    if (t > 0) ++(h == 1 ? test.depthHits : test.leftDepthHits);
                    classify(g);
                }
                float co[3], cd[3];   // the preview: how far the cursor's ray goes (its gun aims there next frame)
                if (backend_->desktopCursorRay(co, cd)) {
                    const Rigid pl = ref;   // the cursor ray: tracking -> the world (as a pose's point and axis)
                    const float lo[3] = {co[0] * upm, co[1] * upm, -co[2] * upm}, ld[3] = {cd[0], cd[1], -cd[2]};
                    float wo[3], wd[3];
                    pl.apply(lo, wo);
                    pl.rotate(ld, wd);
                    const float tc = depthRay(wo, wd, 60.0f * upm);
                    backend_->cursorDistance((tc > 0 ? tc : settings.aimDistance * upm) / upm);
                }
                if (std::getenv("HOTD_VR_DEBUG") && rt.frameCount % 300 == 0) {   // the scale: the eye over the ground
                    float down[3] = {fwd[0], 0, fwd[2]};   // (45 degrees down ahead: straight down is out of view)
                    normalize3(down);
                    down[1] = -1.0f;
                    normalize3(down);
                    const float t = march(eyeW.t, down, vp, mw, mh, 600.0f);
                    SDL_Log("vr: frame %llu the right eye %.1f units over the ground ahead (%.2f m at %.1f units / m)",
                            (unsigned long long)rt.frameCount, t * 0.7071f, t * 0.7071f / upm, upm);
                }
                if (followGameAim) {   // --vr-preview-test: the round trip through the game's own aim
                    ++test.frames;
                    const float ax = testAimSet ? testAim[0] : rt.gun[0].x, ay = testAimSet ? testAim[1] : rt.gun[0].y;
                    float dirC[3] = {ax, ay, gcam.focal}, dirW[3];
                    normalize3(dirC);
                    gcam.toWorld.rotate(dirC, dirW);
                    const float tg = march(gcam.toWorld.t, dirW, vp, mw, mh, 60.0f * upm);
                    if (tg > 0 && std::abs(ax) < halfW && std::abs(ay) < 192.0f) {
                        float wpt[3];
                        for (int k = 0; k < 3; ++k) wpt[k] = gcam.toWorld.t[k] + dirW[k] * tg;
                        // the cursor onto that point (the preview's gun aims at it from the next frame)
                        float c[4];
                        for (int r = 0; r < 4; ++r) c[r] = vp[r] * wpt[0] + vp[4 + r] * wpt[1] + vp[8 + r] * wpt[2] + vp[12 + r];
                        if (c[3] > 0) backend_->setCursor(1, (c[0] / c[3]) * 0.5f + 0.5f, 0.5f - (c[1] / c[3]) * 0.5f);
                        // this frame's chain: the muzzle's ray to the point, its hit in the depth, back to the arcade
                        // (dual pistols: the left gun's from its own muzzle too)
                        for (int h : {1, 0}) {
                            if (!gun[h].on)
                                continue;
                            const float* mo = gun[h].o;
                            float rd[3] = {wpt[0] - mo[0], wpt[1] - mo[1], wpt[2] - mo[2]};
                            const float len = std::sqrt(dot3(rd, rd));
                            normalize3(rd);
                            // (vr4: the gun's ray as play measures it: aim_depth = cpu checks the CPU test against
                            // the picture's own depth, the target point above)
                            // (aim_depth = fixed: the ray's fixed length, the baseline's error)
                            const bool fixedBase = settings.aimDepth == 3;
                            const float th = fixedBase ? settings.aimDistance * upm : depthRay(mo, rd, 60.0f * upm);
                            // the gun's view to the point blocked (something between the hand and it, as a hand
                            // behind a door frame): the gun hits that first, which is right; counted apart
                            if (!fixedBase && th > 0 && th < 0.9f * len) {
                                if (h == 1) ++test.blocked;
                            } else if (th > 0) {
                                const AimResult a = aimFromRay(gcam, mo, rd, th, worldUp, 89.0f);
                                const double err = std::max(std::abs(a.x - ax), std::abs(a.y - ay));
                                if (h == 0) {
                                    ++test.leftChecked;
                                    test.leftSum += err;
                                    continue;
                                }
                                // vr4: the gun pointed at the game's aim well inside its screen: what a pull would do
                                // with the play settings (reload_pitch, offscreen_reload) must be a shot (a boss fight
                                // where every shot reloads would show here: the Hangedman)
                                if (std::abs(ax) < halfW - 8.0f && std::abs(ay) < 184.0f) {
                                    const AimResult ap = aimFromRay(gcam, mo, rd, th, playerUp, settings.reloadPitch);
                                    ++test.aimOnFrames;
                                    if (pullAction(ap, gcam.halfW, gcam.halfH, settings.offscreenReload) != Pull::Shoot) {
                                        ++test.aimOffWhileOn;
                                        if (std::getenv("HOTD_VR_DEBUG"))
                                            SDL_Log("vr test: frame %llu aim %.1f %.1f taken as off: %.1f %.1f down %d behind %d, camera pitch %.1f",
                                                    (unsigned long long)rt.frameCount, ax, ay, ap.x, ap.y, int(ap.down), int(ap.behind),
                                                    double(gpitch * 57.2958f));
                                    }
                                }
                                ++test.checked;
                                test.sum += err;
                                test.worst = std::max(test.worst, err);
                                if (err > 6.0) ++test.over;
                                if (err > 6.0 && std::getenv("HOTD_VR_DEBUG"))
                                    SDL_Log("vr test: frame %llu aim %.1f %.1f -> %.1f %.1f, camera ray %.1f, gun ray %.1f of %.1f",
                                            (unsigned long long)rt.frameCount, ax, ay, a.x, a.y, tg, th, len);
                            }
                        }
                    }
                }
            }
            if (aimPass) {   // (vrnull: back to the eye's own picture after the aim's depth)
                gl::BindFramebuffer(gl::FRAMEBUFFER, backend_->eyeFramebuffer(i));
                gl::Viewport(0, 0, e.width, e.height);
            }
            // over the world: the hands (the game's renderer, the depth cleared: on top of the world), the laser, the
            // crosshair, the HUD panel, the vignette
            // vrfix (user's playthrough 2026-11-10: the lamps / bullets drawn over the raised left hand, "1UP!" behind the
            // gun and the researcher): one rule for the near layer. The world first; then, over it with the depth
            // cleared, the extra-life item (0x3BB / 0x3BC: a message the flat game shows in front of everything, at 1-5 m
            // in camera space) and the hands, by their own depth (a hand nearer than the item covers it); then the HUD
            // panel tested against that depth only: the hands (always nearer than the panel, which stays at least
            // hud_min_distance away) hide the part of the HUD they are physically in front of, and nothing else ever
            // does (no wall / creature: the world's depth is gone), so the HUD stays readable and nothing pops through
            // a hand. (panel_depth_test = on: without hands up, the world's depth still hides it, as before.)
            if (!hands.empty() || !settings.panelDepthTest) {
                nearLayer_.assign(hands.begin(), hands.end());
                for (const DrawItem& it : *drawItems)
                    if (it.placement && (it.placement->id == 0x3BB || it.placement->id == 0x3BC))
                        nearLayer_.push_back(it);
                if (nearLayer_.empty()) {
                    gl::DepthMask(gl::TRUE_);
                    gl::Clear(gl::DEPTH_BUFFER_BIT);   // (the clear depth: GL's default 1)
                } else {
                    view.renderer.eye = &ev;
                    view.renderer.draw(nearLayer_, cam, e.width, e.height, handOpt);
                    view.renderer.eye = nullptr;
                }
            }
            gl::Enable(gl::DEPTH_TEST);
            gl::DepthFunc(gl::LEQUAL);
            gl::Disable(gl::BLEND);
            gl::Disable(gl::CULL_FACE);
            gl::DepthMask(gl::FALSE_);
            float eyeR[3], eyeU[3];
            eyeW.axis(0, eyeR);
            eyeW.axis(1, eyeU);
            for (const Gun& g : gun) {   // a ribbon from each muzzle to its hit, turned to the eye
                if (!g.on || !laserOn || !showAim)   // (vrsight: Laser (VR), else vr.ini laser; vrattract: only in control)
                    continue;
                const float *gunO = g.o, *hit = g.hit;
                float toEye[3] = {eyeW.t[0] - gunO[0], eyeW.t[1] - gunO[1], eyeW.t[2] - gunO[2]}, side[3];
                cross3(g.d, toEye, side);
                normalize3(side);
                const float wdt = 0.004f * upm;
                const float v[20] = {gunO[0] - side[0] * wdt, gunO[1] - side[1] * wdt, gunO[2] - side[2] * wdt, 0, 0,
                                     gunO[0] + side[0] * wdt, gunO[1] + side[1] * wdt, gunO[2] + side[2] * wdt, 1, 0,
                                     hit[0] - side[0] * wdt, hit[1] - side[1] * wdt, hit[2] - side[2] * wdt, 0, 1,
                                     hit[0] + side[0] * wdt, hit[1] + side[1] * wdt, hit[2] + side[2] * wdt, 1, 1};
                const float tint[4] = {1.0f, 0.15f, 0.1f, 0.55f};
                drawSprite(v, 4, 0, 2, tint, vp);
            }
            gl::Disable(gl::DEPTH_TEST);   // (the crosshair and the panel: on top)
            drawFades(true);   // vrfade: the white fade / the cover over the whole eye (under the 2D and the sights, as flat)
            for (const Gun& g : gun) {   // the game's own sight at each gun's hit, the same angular size at any distance
                if (!g.on || !sight || !crosshairOn || !showAim)   // (vrsight: Crosshair (VR), else vr.ini crosshair; vrattract: only in control)
                    continue;
                const float* hit = g.hit;
                const float dx = hit[0] - eyeW.t[0], dy = hit[1] - eyeW.t[1], dz = hit[2] - eyeW.t[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float hs = dist * std::tan(settings.crosshairDegrees * kPi / 360.0f), hw = hs * sightAspect;
                float v[20];
                const float corner[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
                for (int c = 0; c < 4; ++c) {
                    for (int k = 0; k < 3; ++k)
                        v[c * 5 + k] = hit[k] + eyeR[k] * hw * corner[c][0] + eyeU[k] * hs * corner[c][1];
                    v[c * 5 + 3] = corner[c][0] * 0.5f + 0.5f;
                    v[c * 5 + 4] = 0.5f - corner[c][1] * 0.5f;   // (the sight's rows: top first)
                }
                // vr4: a pull here would do nothing (offscreen_reload = below, aimed beside / above / behind the
                // screen): the sight greyed and faint
                const bool grey = g.pull == Pull::Ignore;
                const float tint[4] = {grey ? 0.45f : 1.0f, grey ? 0.45f : 1.0f, grey ? 0.45f : 1.0f, grey ? 0.55f : 1.0f};
                drawSprite(v, 4, sightTex, 0, tint, vp);
            }
            {   // the HUD on the arcade screen's plane: the original picture's frustum at panelDistance
                const float D = panelD * upm, focal = panelFocal;   // (vr4: adaptive: panelD, above)
                const float hh = D * 192.0f / focal, hw = D * halfW / focal;
                gl::Enable(gl::DEPTH_TEST);   // (vrfix: against the near layer: the hands hide what they cover)
                float v[20];
                const float corner[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
                for (int c = 0; c < 4; ++c) {
                    const float lp[3] = {hw * corner[c][0], hh * corner[c][1], D};
                    ref.apply(lp, &v[c * 5]);
                    v[c * 5 + 3] = corner[c][0] * 0.5f + 0.5f;
                    v[c * 5 + 4] = corner[c][1] * 0.5f + 0.5f;
                }
                const float tint[4] = {1, 1, 1, 1};
                drawSprite(v, 4, panelTex_, 1, tint, vp);
                gl::Disable(gl::DEPTH_TEST);
            }
            drawFades(false);   // vrfade: the PC fade over the whole eye (over the 2D, as flat; the vignette stays on top)
            if (vignetteNow > 0.01f) {
                static const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                const float v[20] = {-1, -1, 0, 0, 0, 1, -1, 0, 1, 0, -1, 1, 0, 0, 1, 1, 1, 0, 1, 1};
                // (vrfade: under a white fade / the cover as much less as they cover: the flash's peak is the whole
                // eye; the camera cut under the attract's white flash turns the reference fast, the vignette came on)
                const float covered = fadeCover_ ? 1.0f : fadeWhite_;
                const float tint[4] = {0, 0, 0, vignetteNow * settings.vignetteStrength * (1.0f - covered)};
                drawSprite(v, 4, 0, 3, tint, ident);
            }
            gammaApply(backend_->eyeFramebuffer(i), e.width, e.height);   // gamma (render/gamma.h): the eye's finished picture
            gl::DepthMask(gl::TRUE_);
            gl::Enable(gl::DEPTH_TEST);
            gl::Disable(gl::BLEND);
            gl::BindVertexArray(0);
        }
        // vrnull: desktop_view spectator: the flat picture for the window (the eyes' "around" pieces are eye-only)
        if (settings.desktopView == 2) flatPicture(view, items, base, halfW);
    };
    // the game's list once (a wide picture: the side zones of widescreen are drawn too), the eyes draw it
    // vr3: and the neighbouring zones' pieces outside the game camera's view (GameView::vrAround: behind, beside you)
    view.vrAround = settings.around && !backend_->perfAroundOff();   // (questperf: perf_cycle's parts without it)
    view.draw(rt, 2 * 960, 960, f.prev, f.alpha);
    view.vrAround = false;
    view.drawOverride = nullptr;
    view.opt.clearColor = clearWas;

    backend_->endFrame(fs_);
#if !defined(__ANDROID__)   // (quest: no window to show it in: the copy saved)
    if (settings.desktopView != 2 || !flatFrame_)   // (spectator: the flat picture below, once the aim is known)
        backend_->present(f.outFbo, f.outW, f.outH);
#endif
    gl::BindFramebuffer(gl::FRAMEBUFFER, f.outFbo);
    gl::Viewport(0, 0, f.outW, f.outH);

    // the aim for the next tick
    lastHitDist = gun[1].dist;
    lastAim = aimFromRay(gcam, gunO, gunD, gun[1].dist, playerUp, settings.reloadPitch);
    std::copy(gun[1].hit, gun[1].hit + 3, lastAim.hit);
    // vrcomfort (view = screen): a gun's aim where its ray meets the screen, off it as it points (below the screen or
    // pointed down: pullAction's reload; beside it / away: offscreen_reload)
    auto fromScreen = [&](const ScreenAim& s, AimResult& a) {
        a.onScreen = s.hit && std::abs(s.x) < halfW && std::abs(s.y) < 192.0f;
        a.x = std::clamp(s.x, -4000.0f, 4000.0f), a.y = s.hit ? std::clamp(s.y, -4000.0f, 4000.0f) : 0.0f;
        a.down = s.down, a.behind = !s.hit;
    };
    if (screenAim_.on && screenView) {
        fromScreen(screenAim_, lastAim);
    } else if (screenAim_.on) {   // vrfix: the name entry: the aim where the ray meets the cinema screen (off it: off the screen)
        const bool on = screenAim_.hit && std::abs(screenAim_.x) < halfW && std::abs(screenAim_.y) < 192.0f;
        lastAim.x = on ? screenAim_.x : (screenAim_.x < 0 ? -4000.0f : 4000.0f);
        lastAim.y = on ? screenAim_.y : 0.0f;
        lastAim.onScreen = on, lastAim.down = lastAim.behind = false;
    }
    pending_.valid = fs_.hand[1].active;
    pending_.x = std::clamp(lastAim.x, -4000.0f, 4000.0f);
    pending_.y = std::clamp(lastAim.y, -4000.0f, 4000.0f);
    pending_.onScreen = lastAim.onScreen;
    // vr4: offscreen_reload = below: a pull aimed beside / above / behind the screen does nothing (no shot, no reload,
    // no kick); `ignore` tells main.cpp (the preview's mouse button is dropped there too)
    const Pull pullR = pullAction(lastAim, gcam.halfW, gcam.halfH, settings.offscreenReload);
    pending_.ignore = pullR == Pull::Ignore;
    const float tr = fs_.hand[1].trigger;   // (a hysteresis: pulled at 0.6, let go at 0.4)
    triggerDown_ = triggerDown_ ? tr > 0.4f : tr > 0.6f;
    if (triggerDown_ && !wasDown_) {
        if (pullR != Pull::Ignore) {
            ++pending_.pulls;
            backend_->haptic(1, 0.7f, 0.06f);   // openxr: the gun's kick (the preview: nothing)
        }
        ++(pullR == Pull::Ignore ? test.pullsIgnored : pullR == Pull::Reload ? test.pullsReload : test.pullsShot);
    }
    wasDown_ = triggerDown_;
    pending_.held = triggerDown_ && pullR != Pull::Ignore;

    const bool rightDown = gun[1].on && lastAim.down;   // (vr2: an untracked right gun never "points down")
    if (settings.reloadDown && rightDown && !wasLow_) pending_.reload = pending_.reloadHand[1] = true;
    wasLow_ = rightDown;
    // dual pistols: the left gun the same way (its own aim, trigger, rumble; pointing it down reloads the pair)
    pending_.dual = dual;
    if (gun[0].on) {
        lastAimLeft = aimFromRay(gcam, gun[0].o, gun[0].d, gun[0].dist, playerUp, settings.reloadPitch);
        std::copy(gun[0].hit, gun[0].hit + 3, lastAimLeft.hit);
        if (screenAimL_.on && screenView) fromScreen(screenAimL_, lastAimLeft);   // (vrcomfort: on the screen)
        Aim::Gun& l = pending_.left;
        l.valid = true;
        l.x = std::clamp(lastAimLeft.x, -4000.0f, 4000.0f);
        l.y = std::clamp(lastAimLeft.y, -4000.0f, 4000.0f);
        l.onScreen = lastAimLeft.onScreen;
        const Pull pullL = pullAction(lastAimLeft, gcam.halfW, gcam.halfH, settings.offscreenReload);   // (vr4)
        l.ignore = pullL == Pull::Ignore;
        const float tl = fs_.hand[0].trigger;
        triggerDownL_ = triggerDownL_ ? tl > 0.4f : tl > 0.6f;
        if (triggerDownL_ && !wasDownL_ && pullL != Pull::Ignore) {
            ++l.pulls;
            backend_->haptic(0, 0.7f, 0.06f);   // (the left controller's own kick)
        }
        wasDownL_ = triggerDownL_;
        l.held = triggerDownL_ && pullL != Pull::Ignore;
        if (settings.reloadDown && lastAimLeft.down && !wasLowL_) pending_.reload = pending_.reloadHand[0] = true;
        wasLowL_ = lastAimLeft.down;
    } else {
        pending_.left = Aim::Gun{};
        triggerDownL_ = wasDownL_ = wasLowL_ = false;
    }
    // vr2 (2026-11-09): dual pistols with the right controller off / asleep / out of tracking: the left gun alone is
    // player 1's gun (was: no input at all, pending_.valid followed the right hand only); its aim between shots too
    if (gun[0].on && !gun[1].on) {
        pending_.valid = true;
        pending_.x = pending_.left.x, pending_.y = pending_.left.y, pending_.onScreen = pending_.left.onScreen;
        pending_.ignore = pending_.left.ignore;
    }
    // openxr: either hand's grip, the right A / B (Vive / WMR: the right trackpad) reload (vrflow: the left Y is the
    // menu action now, Pause); the left X (Index: left A; Vive / WMR: the left trackpad) is Start (join / continue);
    // the menu button pauses
    for (int h = 0; h < 2; ++h) {
        const Controller& c = fs_.hand[h];
        gripDown_[h] = gripDown_[h] ? c.squeeze > 0.4f : c.squeeze > 0.6f;
        const bool reload = gripDown_[h] || c.secondary || (h == 1 && c.primary);
        if (reload && !wasReload_[h]) pending_.reload = pending_.reloadHand[h] = true;
        wasReload_[h] = reload;
    }
    // shake-to-reload / wrist-flick reload (vrflick): the gun controller(s) (the right; the left too with dual pistols:
    // each gun's gesture reloads that gun) through the same `reload`; one cooldown per gun for both gestures.
    // --vr-preview-test: the game's frames as the clock (60 a second): the preview's time is the wall clock, and a
    // headless run draws its frames as fast as it can (unevenly), so its gun, moved once a game frame, looked several
    // times faster than the bot's aim really moves (stray shakes in a busy PC's runs)
    const int64_t gestureTime = followGameAim ? int64_t(rt.frameCount) * 16666667LL : fs_.displayTime;
    for (int h = 0; h < 2; ++h) {
        const Controller& c = fs_.hand[h];
        if ((settings.shakeReload || settings.flickReload) && (h == 1 || dual) && c.active) {
            if (gestures_[h].update(c.grip, gestureTime, settings.shakeReload, settings.shakeSensitivity,
                                    settings.flickReload, settings.flickSensitivity)) {
                // noshakeskip (user 2026-11-11: "shaking to reload should NOT skip cut-scenes"): a reload is an
                // off-screen pull, which the arcade's skippable scenes take as a skip: shakes / flicks count only
                // while player 1 is in control (playerInControl), not in cut-scenes, cards or the attract
                if (controlRun_ >= 1) {
                    pending_.reload = pending_.reloadHand[h] = true;
                    SDL_Log("vr: %s reload (the %s gun), frame %llu", gestures_[h].last(), h ? "right" : "left",
                            (unsigned long long)rt.frameCount);
                } else {
                    SDL_Log("vr: %s ignored (a cut-scene: no skip), frame %llu", gestures_[h].last(), (unsigned long long)rt.frameCount);
                }
            }
        } else {
            gestures_[h].reset();
        }
    }
    // vrweapon: the right stick (the gun hand's) flicked past 0.7 left / right, back under 0.3 before the next
    {
        const float sx = fs_.hand[1].stick[0], sy = fs_.hand[1].stick[1];
        if (!stickHeld_ && std::abs(sx) > 0.7f && std::abs(sx) > std::abs(sy)) pending_.weaponStep = sx > 0 ? 1 : -1, stickHeld_ = true;
        else if (stickHeld_ && std::abs(sx) < 0.3f) stickHeld_ = false;
    }
    if (fs_.hand[0].primary && !wasStart_) pending_.start = true;
    wasStart_ = fs_.hand[0].primary;
    // vrflow: any trigger / A / X, wherever the hands point (the attract's way to the main menu); a press needs all
    // of them let go first (the one held from the big screen is no press)
    const bool anyPress = fs_.hand[0].trigger > 0.6f || fs_.hand[1].trigger > 0.6f || fs_.hand[0].primary || fs_.hand[1].primary;
    const bool anyHeld = fs_.hand[0].trigger > 0.4f || fs_.hand[1].trigger > 0.4f || fs_.hand[0].primary || fs_.hand[1].primary;
    if (anyPress && !pressHeld_) pending_.press = true;
    pressHeld_ = pressHeld_ ? anyHeld : anyPress;
    const bool menu = fs_.hand[0].menu || fs_.hand[1].menu;
    if (menu && !wasMenu_) menuPending_ = true;
    wasMenu_ = menu;
#if !defined(__ANDROID__)
    presentSpectator(f);   // (vrnull: desktop_view spectator, with this frame's aim)
#endif
}

// vrnull (user 2026-11-09, docs/vr.md "The desktop window"): vr.ini desktop_view spectator: the flat picture (flatPicture:
// the game camera's view, the HUD) with player 1's sight at the VR gun's aim, letterboxed into the window
void Scene::presentSpectator(const Frame& f)
{
    if (settings.desktopView != 2 || !flatFrame_ || !cinemaFbo_)
        return;
    gl::BindFramebuffer(gl::FRAMEBUFFER, cinemaFbo_);
    unsigned sightTex = 0;
    float sightAspect = 1.0f;
    if (f.hud && f.view && f.rt && pending_.valid && pending_.onScreen &&
        f.hud->vrSight(*f.rt, 0, cinemaH_, sightTex, sightAspect)) {   // (the HUD's own sight, where the gun points)
        float aim[4][2] = {};
        f.view->fromArcade(pending_.x, pending_.y, cinemaW_, cinemaH_, aim[0][0], aim[0][1]);
        bool cross[4];
        std::copy(f.hud->drawCross, f.hud->drawCross + 4, cross);
        std::fill(f.hud->drawCross, f.hud->drawCross + 4, false);
        f.hud->drawCross[0] = true;
        f.hud->drawSights(*f.rt, cinemaW_, cinemaH_, aim);
        std::copy(cross, cross + 4, f.hud->drawCross);
    }
    gammaApply(cinemaFbo_, cinemaW_, cinemaH_);   // (as the eyes: the finished picture)
    using PFN_Blit = void(HOTD_APIENTRY*)(gl::GLint, gl::GLint, gl::GLint, gl::GLint, gl::GLint, gl::GLint, gl::GLint, gl::GLint,
                                          gl::GLbitfield, gl::GLenum);
    static const auto blit = reinterpret_cast<PFN_Blit>(SDL_GL_GetProcAddress("glBlitFramebuffer"));
    gl::BindFramebuffer(gl::FRAMEBUFFER, f.outFbo);
    gl::Viewport(0, 0, f.outW, f.outH);
    gl::ClearColor(0, 0, 0, 1);
    gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
    if (blit && f.outW > 0 && f.outH > 0) {
        const float s = std::min(float(f.outW) / float(cinemaW_), float(f.outH) / float(cinemaH_));
        const int w = int(float(cinemaW_) * s), h = int(float(cinemaH_) * s), x = (f.outW - w) / 2, y = (f.outH - h) / 2;
        constexpr gl::GLenum kRead = 0x8CA8, kDraw = 0x8CA9;
        gl::BindFramebuffer(kRead, cinemaFbo_);
        gl::BindFramebuffer(kDraw, f.outFbo);
        blit(0, 0, cinemaW_, cinemaH_, x, y, x + w, y + h, gl::COLOR_BUFFER_BIT, gl::LINEAR);
    }
    gl::BindFramebuffer(gl::FRAMEBUFFER, f.outFbo);
    gl::Viewport(0, 0, f.outW, f.outH);
}

// vr3 (vr.ini cutscenes = cinema, docs/vr.md "Cut-scenes"): the game's flat picture (the game camera's view as the
// window shows it, the HUD panel with the bars and captions over it) into a texture, shown on a screen
// cinema_distance metres ahead of the seat (cinema_width wide; the player's own space: it stays put while the camera
// cuts), in a dark room
// vrnull: its first part, also the desktop window's spectator picture (vr.ini desktop_view spectator): the flat game's
// picture (the game camera, the renderer's flat path) with the HUD panel over it, in cinemaFbo_ (the game area's shape)
void Scene::flatPicture(game::GameView& view, const std::vector<DrawItem>& items, const Camera& cam, float halfW)
{
    const int ch = settings.hudLines, cw = int(std::lround(float(ch) * halfW / 192.0f));
    if (!cinemaFbo_ || cinemaW_ != cw || cinemaH_ != ch) {
        if (cinemaTex_) gl::DeleteTextures(1, &cinemaTex_);
        if (cinemaDepth_) gl::DeleteRenderbuffers(1, &cinemaDepth_);
        if (cinemaFbo_) gl::DeleteFramebuffers(1, &cinemaFbo_);
        cinemaW_ = cw, cinemaH_ = ch;
        gl::GenFramebuffers(1, &cinemaFbo_);
        gl::BindFramebuffer(gl::FRAMEBUFFER, cinemaFbo_);
        gl::GenTextures(1, &cinemaTex_);
        gl::BindTexture(gl::TEXTURE_2D, cinemaTex_);
        gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, cw, ch, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR_MIPMAP_LINEAR);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
        gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, cinemaTex_, 0);
        gl::GenRenderbuffers(1, &cinemaDepth_);
        gl::BindRenderbuffer(gl::RENDERBUFFER, cinemaDepth_);
        gl::RenderbufferStorage(gl::RENDERBUFFER, gl::DEPTH_COMPONENT24, cw, ch);
        gl::FramebufferRenderbuffer(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::RENDERBUFFER, cinemaDepth_);
        gl::framebufferOk("vr cinema");
    }
    // the flat picture: the game's camera, no eye (the renderer's flat path: what the window would show)
    gl::BindFramebuffer(gl::FRAMEBUFFER, cinemaFbo_);
    gl::Viewport(0, 0, cw, ch);
    view.renderer.draw(items, cam, cw, ch, view.opt);
    static const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float white[4] = {1, 1, 1, 1};
    gl::Disable(gl::DEPTH_TEST);
    gl::Disable(gl::CULL_FACE);
    gl::DepthMask(gl::FALSE_);
    drawFades(true);   // vrfade: the fades the panel leaves out (the flat game's order: under the 2D ...)
    {   // the HUD over it (the panel: the same shape)
        const float v[20] = {-1, -1, 0, 0, 0, 1, -1, 0, 1, 0, -1, 1, 0, 0, 1, 1, 1, 0, 1, 1};
        drawSprite(v, 4, panelTex_, 1, white, ident);
    }
    drawFades(false);   // (... and over it)
    gl::DepthMask(gl::TRUE_);
    gl::Enable(gl::DEPTH_TEST);
    gl::Disable(gl::BLEND);
    gl::BindVertexArray(0);
    flatFrame_ = true;
}

void Scene::drawCinema(game::GameView& view, const std::vector<DrawItem>& items, const Camera& cam, float halfW)
{
    flatPicture(view, items, cam, halfW);
    const int cw = cinemaW_, ch = cinemaH_;
    const float white[4] = {1, 1, 1, 1};
    const float upm = settings.unitsPerMetre;
    // vrfix: the name entry: player 1's sight on the picture where the gun meets the screen (the flat game's crosshair)
    unsigned sightTex = 0;
    float sightAspect = 1.0f;
    // (vrcomfort, view = screen: the same in play, in control / paused: screenSight_; dual pistols: the left gun's too)
    if (screenAim_.on && screenSight_ && crosshairOn && frame_ && frame_->hud &&
        frame_->hud->vrSight(*frame_->rt, 0, ch, sightTex, sightAspect)) {
        gl::BindFramebuffer(gl::FRAMEBUFFER, cinemaFbo_);
        gl::Viewport(0, 0, cw, ch);
        bool cross[4];
        game::Hud& hud = *frame_->hud;
        std::copy(hud.drawCross, hud.drawCross + 4, cross);
        std::fill(hud.drawCross, hud.drawCross + 4, false);
        hud.drawCross[0] = true;
        for (const ScreenAim* s : {&screenAim_, &screenAimL_}) {
            if (!s->on || !s->hit)
                continue;
            float aim[4][2] = {};
            view.fromArcade(s->x, s->y, cw, ch, aim[0][0], aim[0][1]);
            hud.drawSights(*frame_->rt, cw, ch, aim);
        }
        std::copy(cross, cross + 4, hud.drawCross);
    }
    gl::BindTexture(gl::TEXTURE_2D, cinemaTex_);
    gl::GenerateMipmap(gl::TEXTURE_2D);
    const float D = settings.cinemaDistance, hw = settings.cinemaWidth * 0.5f, hh = hw * float(ch) / float(cw);
    for (int i : {1, 0}) {
        const Eye& e = fs_.eye[i];
        gl::BindFramebuffer(gl::FRAMEBUFFER, backend_->eyeFramebuffer(i));
        gl::Viewport(0, 0, e.width, e.height);
        gl::ClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
        Rigid eyeL;
        float vp[16];
        seatViewProj(e, eyeL, vp);
        gl::Disable(gl::DEPTH_TEST);
        gl::Disable(gl::CULL_FACE);
        const float v[20] = {-hw, -hh, D, 0, 0, hw, -hh, D, 1, 0, -hw, hh, D, 0, 1, hw, hh, D, 1, 1};
        drawSprite(v, 4, cinemaTex_, 4, white, vp);
        for (const ScreenAim* s : {&screenAim_, &screenAimL_}) {   // vrfix: the laser from the muzzle to the screen
            if (!s->on || !screenSight_ || !laserOn || cinemaHands_.empty() || (s == &screenAim_ && !fs_.hand[1].active))
                continue;
            const float *o = s->o, *p = s->p;
            float dir[3] = {p[0] - o[0], p[1] - o[1], p[2] - o[2]}, toEye[3] = {eyeL.t[0] - o[0], eyeL.t[1] - o[1], eyeL.t[2] - o[2]}, side[3];
            cross3(dir, toEye, side);
            normalize3(side);
            const float wdt = 0.004f;
            const float lv[20] = {o[0] - side[0] * wdt, o[1] - side[1] * wdt, o[2] - side[2] * wdt, 0, 0,
                                  o[0] + side[0] * wdt, o[1] + side[1] * wdt, o[2] + side[2] * wdt, 1, 0,
                                  p[0] - side[0] * wdt, p[1] - side[1] * wdt, p[2] - side[2] * wdt, 0, 1,
                                  p[0] + side[0] * wdt, p[1] + side[1] * wdt, p[2] + side[2] * wdt, 1, 1};
            const float tint[4] = {1.0f, 0.15f, 0.1f, 0.55f};
            drawSprite(lv, 4, 0, 2, tint, vp);
        }
        if (!cinemaHands_.empty()) {   // vrfix: the hands in front of the screen (seat space in game units, own depth)
            const Rigid eyeU = poseToLocal(e.pose, upm);
            Renderer::EyeView ev;
            eyeU.viewMatrix(ev.view);
            ev.tanLeft = e.fov.tanLeft, ev.tanRight = e.fov.tanRight, ev.tanUp = e.fov.tanUp, ev.tanDown = e.fov.tanDown;
            ev.nearZ = cam.nearZ;
            Camera hc = cam;
            std::copy(eyeU.t, eyeU.t + 3, hc.pos);   // (the hands' fog / light from the eye, as in the world)
            hc.perPixelDepth = true;
            RenderOptions handOpt = view.opt;
            handOpt.clearColor = false;
            view.renderer.eye = &ev;
            view.renderer.draw(cinemaHands_, hc, e.width, e.height, handOpt);
            view.renderer.eye = nullptr;
            gl::BindFramebuffer(gl::FRAMEBUFFER, backend_->eyeFramebuffer(i));
            gl::Viewport(0, 0, e.width, e.height);
        }
        gammaApply(backend_->eyeFramebuffer(i), e.width, e.height);   // (as the world's eyes)
    }
    gl::DepthMask(gl::TRUE_);
    gl::Enable(gl::DEPTH_TEST);
    gl::Disable(gl::BLEND);
    gl::BindVertexArray(0);
}

// vrflow (user 2026-11-10: "a floating white Quest controller"): the runtime shows its own room (and its controller
// models) when the game stops submitting frames: every gap over 50 ms between two of our frames is logged with what
// the game was showing (where)
void Scene::noteFrame()
{
    const uint64_t now = SDL_GetTicksNS();
    if (lastBeginNs_ != 0) {
        const double ms = double(now - lastBeginNs_) / 1e6;
        if (ms > 50.0) {
            ++frameGaps;
            if (ms > worstGapMs) worstGapMs = ms, worstGapWhere = where;
            if (frameGaps <= 50 || frameGaps % 100 == 0)   // (a runtime gone wrong: not a line a frame)
                SDL_Log("vr: frame gap %.0f ms in %s", ms, where.c_str());
        }
    }
    lastBeginNs_ = now;
}

bool Scene::takeMenu()
{
    const bool m = menuPending_;
    menuPending_ = false;
    return m;
}

bool Scene::lost() const { return backend_ && backend_->lost(); }
bool Scene::exitAsked() const { return backend_ && backend_->exitAsked(); }

Scene::Aim Scene::takeAim()
{
    Aim a = pending_;
    pending_.pulls = 0;
    pending_.reload = pending_.reloadHand[0] = pending_.reloadHand[1] = false;
    pending_.start = false;
    pending_.press = false;
    pending_.weaponStep = 0;
    pending_.left.pulls = 0;
    return a;
}

// quest (2026-11-03): a flat screen on a big screen in VR (vr_scene.h panelFrame, docs/quest.md)
Scene::Pointer Scene::panelFrame(unsigned srcFbo, int w, int h, int pointW, int pointH, game::GameView* handsView)
{
    Pointer out;
    if (!backend_ || w <= 0 || h <= 0)
        return out;
    using PFN_Blit = void(HOTD_APIENTRY*)(gl::GLint, gl::GLint, gl::GLint, gl::GLint, gl::GLint, gl::GLint, gl::GLint, gl::GLint,
                                          gl::GLbitfield, gl::GLenum);
    constexpr gl::GLenum kRead = 0x8CA8, kDraw = 0x8CA9;
    static const auto blit = reinterpret_cast<PFN_Blit>(SDL_GL_GetProcAddress("glBlitFramebuffer"));
    // the window's picture (drawn by the screen's own code, not shown: the headset is the display) into the texture
    if (!screenFbo_ || screenW_ != w || screenH_ != h) {
        if (screenTex_) gl::DeleteTextures(1, &screenTex_);
        if (screenFbo_) gl::DeleteFramebuffers(1, &screenFbo_);
        screenW_ = w, screenH_ = h;
        gl::GenFramebuffers(1, &screenFbo_);
        gl::BindFramebuffer(gl::FRAMEBUFFER, screenFbo_);
        gl::GenTextures(1, &screenTex_);
        gl::BindTexture(gl::TEXTURE_2D, screenTex_);
        gl::texTag("vr menu screen");
        gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR_MIPMAP_LINEAR);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
        gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, screenTex_, 0);
        gl::framebufferOk("vr screen");
        // questui: black until the first blit (a new texture's memory is not cleared by every driver: ANGLE zeroes it,
        // a native ES driver may not, and a refused blit then showed whatever was there)
        gl::Viewport(0, 0, w, h);
        gl::Disable(gl::SCISSOR_TEST);
        gl::ClearColor(0, 0, 0, 1);
        gl::Clear(gl::COLOR_BUFFER_BIT);
    }
    if (blit) {
        gl::BindFramebuffer(kRead, srcFbo);
        gl::BindFramebuffer(kDraw, screenFbo_);
#if defined(__ANDROID__)
        while (gl::GetError() != 0) {}   // (the errors before: not the blit's)
#endif
        blit(0, 0, w, h, 0, 0, w, h, gl::COLOR_BUFFER_BIT, gl::NEAREST);
#if defined(__ANDROID__)
        // questui (user 2026-11-10: "a white box in the main menu"): a refused blit (the window surface multisampled,
        // another size, lost) leaves the big screen's picture stale: logged (the first 5, then every 600th)
        if (const unsigned e = gl::GetError(); e != 0) {
            static int n = 0;
            if (n++ < 5 || n % 600 == 0)
                SDL_Log("quest: menu screen blit %dx%d from fbo %u failed: GL error 0x%04X (%d times)", w, h, srcFbo, e, n);
        } else {
            static bool once = false;
            if (!once) once = true, SDL_Log("quest: menu screen blit %dx%d from fbo %u ok", w, h, srcFbo);
        }
#endif
    }
    gl::BindTexture(gl::TEXTURE_2D, screenTex_);
    gl::GenerateMipmap(gl::TEXTURE_2D);

    backend_->setOutputSize(w, h);
    noteFrame();
    backend_->beginFrame(fs_);
    if (!lastPanel_) {   // vrflow: from the game (the attract's trigger): the buttons still held click nothing
        lastPanel_ = true;
        const Controller& pc = fs_.hand[1].active && fs_.hand[1].aim.valid ? fs_.hand[1] : fs_.hand[0];
        panelTrigger_ = pc.trigger > 0.4f;
        panelBack_ = fs_.hand[0].secondary || fs_.hand[1].secondary || fs_.hand[0].menu || fs_.hand[1].menu;
        panelEnter_ = fs_.hand[0].primary || fs_.hand[1].primary;
    }
    // the screen in the player's space (metres; left-handed, +z ahead; OpenXR LOCAL: the origin at the head's start)
    const float D = settings.menuDistance, hw = settings.menuWidth * 0.5f, hh = hw * float(h) / float(w);
    // the pointer: the right controller's aim ray (the left's when the right one is not tracked)
    const Controller& c = fs_.hand[1].active && fs_.hand[1].aim.valid ? fs_.hand[1] : fs_.hand[0];
    bool ray = false;
    float o[3] = {0, 0, 0}, d[3] = {0, 0, 1}, hit[3] = {0, 0, 0};
    if (c.active && c.aim.valid) {
        const Rigid a = poseToLocal(c.aim, 1.0f);
        std::copy(a.t, a.t + 3, o);
        a.axis(2, d);
        ray = true;
        float t = 3.0f;   // (pointing away: a short laser)
        if (d[2] > 1e-3f && (D - o[2]) / d[2] > 0) {
            t = (D - o[2]) / d[2];
            const float hx = o[0] + d[0] * t, hy = o[1] + d[1] * t;
            const float u = (hx + hw) / (2 * hw), v = (hh - hy) / (2 * hh);
            if (u >= 0 && u <= 1 && v >= 0 && v <= 1) {
                out.valid = true;
                out.x = u * float(pointW), out.y = v * float(pointH);
            }
        }
        for (int k = 0; k < 3; ++k) hit[k] = o[k] + d[k] * t;
    }
    const bool trig = panelTrigger_ ? c.trigger > 0.4f : c.trigger > 0.6f;   // (the game's hysteresis)
    out.press = trig && !panelTrigger_;
    out.release = !trig && panelTrigger_;
    panelTrigger_ = trig;
    const bool back = fs_.hand[0].secondary || fs_.hand[1].secondary || fs_.hand[0].menu || fs_.hand[1].menu;
    const bool enter = fs_.hand[0].primary || fs_.hand[1].primary;
    out.back = back && !panelBack_;
    out.enter = enter && !panelEnter_;
    panelBack_ = back, panelEnter_ = enter;
    if (out.press) backend_->haptic(fs_.hand[1].active ? 1 : 0, 0.3f, 0.02f);

    // questfb (user 2026-11-10 on the Quest: "add hands on the menu by default but allow people to turn them off";
    // Gameplay > Hands in Menus (VR)): the gun hand and the empty left hand in front of the big screen, the game's own
    // models from handsView (seat space in game units, as drawCinema's name-entry hands; null: none)
    std::vector<DrawItem> menuHands;
    const float upm = settings.unitsPerMetre;
    if (handsView) {
        DrawItem hi;
        auto tune = [&](const float offset[3], const float rotate[3]) {   // (frame()'s: vr.ini's hand offsets)
            Pose p;
            std::copy(offset, offset + 3, p.pos);
            quatFromYawPitchRoll(rotate[1] * kPi / 180.0f, rotate[0] * kPi / 180.0f, rotate[2] * kPi / 180.0f, p.rot);
            p.valid = true;
            return poseToLocal(p, upm);
        };
        if (fs_.hand[1].active && fs_.hand[1].aim.valid) {
            const Rigid g = poseToLocal(fs_.hand[1].aim, upm) * tune(settings.gunOffset, settings.gunRotate) * gunAnchor();
            if (handsView->placedItem(0, kGunHandModel, g.r, g.t, hi))
                hi.variant = hi.model->preferredVariant, menuHands.push_back(hi);
        }
        if (settings.leftHand && fs_.hand[0].active && fs_.hand[0].grip.valid) {
            const Rigid l = poseToLocal(fs_.hand[0].grip, upm) * tune(settings.leftOffset, settings.leftRotate) * leftAnchor();
            if (handsView->placedItem(1, kLeftHandModel, l.r, l.t, hi))
                hi.variant = hi.model->preferredVariant, menuHands.push_back(hi);
        }
    }
    ++test.panelFrames;
    if (!menuHands.empty() && test.panelHandFrames++ == 0) SDL_Log("vr: the menus' hands drawn (Hands in Menus (VR))");

    for (int i : {1, 0}) {
        const Eye& e = fs_.eye[i];
        gl::BindFramebuffer(gl::FRAMEBUFFER, backend_->eyeFramebuffer(i));
        gl::Viewport(0, 0, e.width, e.height);
        gl::ClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
        Rigid eyeL;
        float vp[16];
        seatViewProj(e, eyeL, vp);
        gl::Disable(gl::DEPTH_TEST);
        gl::Disable(gl::CULL_FACE);
        const float white[4] = {1, 1, 1, 1};
        {
            const float v[20] = {-hw, -hh, D, 0, 0, hw, -hh, D, 1, 0, -hw, hh, D, 0, 1, hw, hh, D, 1, 1};
            drawSprite(v, 4, screenTex_, 4, white, vp);
        }
        if (ray) {
            float toEye[3] = {eyeL.t[0] - o[0], eyeL.t[1] - o[1], eyeL.t[2] - o[2]}, side[3];
            cross3(d, toEye, side);
            normalize3(side);
            const float wdt = 0.003f;
            const float v[20] = {o[0] - side[0] * wdt, o[1] - side[1] * wdt, o[2] - side[2] * wdt, 0, 0,
                                 o[0] + side[0] * wdt, o[1] + side[1] * wdt, o[2] + side[2] * wdt, 1, 0,
                                 hit[0] - side[0] * wdt, hit[1] - side[1] * wdt, hit[2] - side[2] * wdt, 0, 1,
                                 hit[0] + side[0] * wdt, hit[1] + side[1] * wdt, hit[2] + side[2] * wdt, 1, 1};
            const float tint[4] = {1.0f, 0.15f, 0.1f, 0.6f};
            drawSprite(v, 4, 0, 2, tint, vp);
        }
        if (out.valid) {   // the dot where the ray meets the screen
            float er[3], eu[3];
            eyeL.axis(0, er);
            eyeL.axis(1, eu);
            const float s = 0.012f * D;
            float v[20];
            const float corner[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
            for (int k = 0; k < 4; ++k) {
                for (int j = 0; j < 3; ++j) v[k * 5 + j] = hit[j] + (er[j] * corner[k][0] + eu[j] * corner[k][1]) * s;
                v[k * 5 + 3] = corner[k][0] * 0.5f + 0.5f, v[k * 5 + 4] = corner[k][1] * 0.5f + 0.5f;
            }
            // questui (user 2026-11-10, "a white box in the main menu" on the Quest): the dot was a solid white square
            // 6 cm wide (the 1x1 white texture) over the menu's own cursor; now a ring around the cursor (mode 6)
            const float tint[4] = {1.0f, trig ? 0.3f : 1.0f, trig ? 0.2f : 1.0f, 0.9f};
            drawSprite(v, 4, 0, 6, tint, vp);
        }
        gl::Disable(gl::BLEND);
        gl::Enable(gl::DEPTH_TEST);
        gl::BindVertexArray(0);
        if (!menuHands.empty()) {   // questfb: the hands over the big screen (their own depth)
            const Rigid eyeU = poseToLocal(e.pose, upm);
            Renderer::EyeView ev;
            eyeU.viewMatrix(ev.view);
            ev.tanLeft = e.fov.tanLeft, ev.tanRight = e.fov.tanRight, ev.tanUp = e.fov.tanUp, ev.tanDown = e.fov.tanDown;
            Camera hc;
            hc.nearZ = std::min(1.0f, 0.05f * upm);
            ev.nearZ = hc.nearZ;
            std::copy(eyeU.t, eyeU.t + 3, hc.pos);
            hc.perPixelDepth = true;
            RenderOptions handOpt = handsView->opt;
            handOpt.clearColor = false;
            handsView->renderer.eye = &ev;
            handsView->renderer.draw(menuHands, hc, e.width, e.height, handOpt);
            handsView->renderer.eye = nullptr;
            gl::BindVertexArray(0);
        }
    }
    backend_->endFrame(fs_);
#if !defined(__ANDROID__)
    backend_->present(srcFbo, w, h);   // (a PC's window: the headset's left eye; the Quest shows no window)
#endif
    gl::BindFramebuffer(gl::FRAMEBUFFER, srcFbo);
    gl::Viewport(0, 0, w, h);
    return out;
}

} // namespace hotd::vr
