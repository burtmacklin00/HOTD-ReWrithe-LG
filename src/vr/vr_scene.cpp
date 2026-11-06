// VR mode: the VR frame (vr_scene.h, docs/vr.md)
#include "vr/vr_scene.h"

#include "game/hud.h"
#include "game/view.h"
#include "render/gamma.h"
#include "render/gl.h"

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

} // namespace

bool Scene::init(std::unique_ptr<Backend> backend, const std::string& vrDir, std::string& err)
{
    vrDir_ = vrDir;
    const bool had = settings.load((std::filesystem::path(vrDir) / "vr.ini").string());
    if (!backend) {
        err = "vr: no backend";
        return false;
    }
    if (!backend->init(settings, err))
        return false;
    if (!makePrograms(err))
        return false;
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
    // 4 an opaque picture (quest: the flat screens' big screen)
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
        SDL_Log("vr: the eye's depth cannot be read here: the aim uses the fixed distance (aim_distance)");
        return -1.0f;
    }
    auto depthAt = [&](const S& q) {
        const int px = std::clamp(int(q.x), x0, x1), py = std::clamp(int(q.y), y0, y1);
        return depthBuf_[size_t(py - y0) * size_t(bw) + size_t(px - x0)];
    };
    auto behind = [&](const S& q) { return q.in && q.z > depthAt(q) + 2e-6f; };
    for (int k = 0; k < kSamples; ++k) {
        if (!behind(s[k])) continue;
        float lo = k ? s[k - 1].t : 0.0f, hi = s[k].t;
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
    else
        gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::BindVertexArray(spriteVao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, spriteVbo_);
    gl::BufferSubData(gl::ARRAY_BUFFER, 0, gl::GLsizeiptr(size_t(count) * 5 * sizeof(float)), verts);
    gl::DrawArrays(gl::TRIANGLE_STRIP, 0, count);
}

void Scene::frame(const Frame& f)
{
    if (!backend_ || !f.view || !f.rt)
        return;
    const game::Runtime& rt = *f.rt;
    const float upm = settings.unitsPerMetre;
    backend_->setOutputSize(f.outW, f.outH);
    backend_->setDual(dual);   // (dual pistols: the preview points the left hand too)
    backend_->beginFrame(fs_);

    // the 2D HUD into the panel (the game area's shape, 960 lines)
    const float halfW = std::max(192.0f, f.halfW);
    const int ph = settings.hudLines, pw = int(std::lround(float(ph) * halfW / 192.0f));   // (vr.ini hud_lines: 960)
    ensurePanel(pw, ph);
    gl::BindFramebuffer(gl::FRAMEBUFFER, panelFbo_);
    gl::Viewport(0, 0, pw, ph);
    gl::ClearColor(0, 0, 0, 0);
    gl::Clear(gl::COLOR_BUFFER_BIT);
    if (f.drawHud) f.drawHud(pw, ph);
    gl::BindTexture(gl::TEXTURE_2D, panelTex_);
    gl::GenerateMipmap(gl::TEXTURE_2D);
    unsigned sightTex = 0;
    float sightAspect = 1.0f;
    const bool sight = f.hud && f.hud->vrSight(rt, 0, ph, sightTex, sightAspect);

    // the newest game camera: the aim's reference (as the game tests its shots)
    float gr[9], gp[3];
    game::GameView::cameraToWorld(rt.camera, gr, gp);
    const float gyaw = std::atan2(gr[2], gr[8]), gpitch = std::atan2(gr[5], std::sqrt(gr[2] * gr[2] + gr[8] * gr[8]));
    GameCamera gcam = gameCameraFrom(gp, gyaw, gpitch, rt.focal);
    gcam.halfW = halfW;
    const float worldUp[3] = {0, 1, 0};

    game::GameView& view = *f.view;
    const bool clearWas = view.opt.clearColor;
    view.opt.clearColor = true;
    Rigid ref;
    // the guns: [1] the right (always), [0] the left (dual pistols only); each its muzzle, barrel, how far its ray goes
    struct Gun {
        bool on = false;
        float o[3] = {0, 0, 0}, d[3] = {0, 0, 1}, dist = 0, hit[3] = {0, 0, 0};
    } gun[2];
    for (Gun& g : gun) g.dist = settings.aimDistance * upm;
    float* gunO = gun[1].o;
    float* gunD = gun[1].d;
    const bool dual = this->dual;
    view.drawOverride = [&](const std::vector<DrawItem>& items) {
        const Camera& base = view.camOverride ? *view.camOverride : view.cam;
        ref = referenceFrom(base, settings.follow);
        Camera cam = base;
        cam.perPixelDepth = true;   // (VR: the per-pixel depth: each eye sorting polygons its own way would differ)
        // comfort: how fast the reference turns (degrees / s) -> the vignette
        float fwd[3];
        ref.axis(2, fwd);
        if (prevTime_ && fs_.displayTime > prevTime_) {
            const float dt = float(double(fs_.displayTime - prevTime_) / 1e9);
            const float ang = std::acos(std::clamp(dot3(fwd, prevFwd_), -1.0f, 1.0f)) * 180.0f / kPi / std::max(dt, 1e-3f);
            const float want = settings.vignette ? std::clamp((ang - settings.vignetteTurn) / settings.vignetteTurn, 0.0f, 1.0f) : 0.0f;
            vignetteNow += (want - vignetteNow) * std::min(1.0f, dt * (want > vignetteNow ? 12.0f : 3.0f));
        }
        std::copy(fwd, fwd + 3, prevFwd_);
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
        if (right && view.placedItem(0, kGunHandModel, gunW.r, gunW.t, hi))
            hands.push_back(hi);
        if (gun[0].on && view.placedItem(1, kGunHandModel, gunLW.r, gunLW.t, hi))   // (dual pistols: mirrored)
            hands.push_back(hi);
        if (left && view.placedItem(1, kLeftHandModel, leftW.r, leftW.t, hi))
            hands.push_back(hi);
        RenderOptions handOpt = view.opt;
        handOpt.clearColor = false;   // (over the eye's picture; their own depth: never inside a wall)

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
            view.renderer.draw(items, cam, e.width, e.height, view.opt);
            view.renderer.eye = nullptr;
            float vp[16];
            std::memcpy(vp, view.renderer.lastViewProj, sizeof vp);
            if (i == 1) {
                for (int h : {1, 0}) {   // each gun's ray in the right eye's depth (dual pistols: the left one too)
                    Gun& g = gun[h];
                    const float t = g.on ? march(g.o, g.d, vp, e.width, e.height, 60.0f * upm) : -1.0f;
                    g.dist = t > 0 ? t : settings.aimDistance * upm;
                    for (int k = 0; k < 3; ++k) g.hit[k] = g.o[k] + g.d[k] * g.dist;
                    if (t > 0) ++(h == 1 ? test.depthHits : test.leftDepthHits);
                }
                float co[3], cd[3];   // the preview: how far the cursor's ray goes (its gun aims there next frame)
                if (backend_->desktopCursorRay(co, cd)) {
                    const Rigid pl = ref;   // the cursor ray: tracking -> the world (as a pose's point and axis)
                    const float lo[3] = {co[0] * upm, co[1] * upm, -co[2] * upm}, ld[3] = {cd[0], cd[1], -cd[2]};
                    float wo[3], wd[3];
                    pl.apply(lo, wo);
                    pl.rotate(ld, wd);
                    const float tc = march(wo, wd, vp, e.width, e.height, 60.0f * upm);
                    backend_->cursorDistance((tc > 0 ? tc : settings.aimDistance * upm) / upm);
                }
                if (std::getenv("HOTD_VR_DEBUG") && rt.frameCount % 300 == 0) {   // the scale: the eye over the ground
                    float down[3] = {fwd[0], 0, fwd[2]};   // (45 degrees down ahead: straight down is out of view)
                    normalize3(down);
                    down[1] = -1.0f;
                    normalize3(down);
                    const float t = march(eyeW.t, down, vp, e.width, e.height, 600.0f);
                    SDL_Log("vr: frame %llu the right eye %.1f units over the ground ahead (%.2f m at %.1f units / m)",
                            (unsigned long long)rt.frameCount, t * 0.7071f, t * 0.7071f / upm, upm);
                }
                if (followGameAim) {   // --vr-preview-test: the round trip through the game's own aim
                    ++test.frames;
                    const float ax = testAimSet ? testAim[0] : rt.gun[0].x, ay = testAimSet ? testAim[1] : rt.gun[0].y;
                    float dirC[3] = {ax, ay, gcam.focal}, dirW[3];
                    normalize3(dirC);
                    gcam.toWorld.rotate(dirC, dirW);
                    const float tg = march(gcam.toWorld.t, dirW, vp, e.width, e.height, 60.0f * upm);
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
                            const float th = march(mo, rd, vp, e.width, e.height, 60.0f * upm);
                            // the gun's view to the point blocked (something between the hand and it, as a hand
                            // behind a door frame): the gun hits that first, which is right; counted apart
                            if (th > 0 && th < 0.9f * len) {
                                if (h == 1) ++test.blocked;
                            } else if (th > 0) {
                                const AimResult a = aimFromRay(gcam, mo, rd, th, worldUp, 89.0f);
                                const double err = std::max(std::abs(a.x - ax), std::abs(a.y - ay));
                                if (h == 0) {
                                    ++test.leftChecked;
                                    test.leftSum += err;
                                    continue;
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
            // over the world: the hands (the game's renderer, the depth cleared: on top of the world), the laser, the
            // crosshair, the HUD panel, the vignette
            if (!hands.empty()) {
                view.renderer.eye = &ev;
                view.renderer.draw(hands, cam, e.width, e.height, handOpt);
                view.renderer.eye = nullptr;
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
                if (!g.on || !settings.laser)
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
            for (const Gun& g : gun) {   // the game's own sight at each gun's hit, the same angular size at any distance
                if (!g.on || !sight)
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
                const float tint[4] = {1, 1, 1, 1};
                drawSprite(v, 4, sightTex, 0, tint, vp);
            }
            {   // the HUD on the arcade screen's plane: the original picture's frustum at panelDistance
                const float D = settings.panelDistance * upm, focal = 192.0f / std::tan(base.fovY / 2);
                const float hh = D * 192.0f / focal, hw = D * halfW / focal;
                if (settings.panelDepthTest) gl::Enable(gl::DEPTH_TEST);
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
            if (vignetteNow > 0.01f) {
                static const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                const float v[20] = {-1, -1, 0, 0, 0, 1, -1, 0, 1, 0, -1, 1, 0, 0, 1, 1, 1, 0, 1, 1};
                const float tint[4] = {0, 0, 0, vignetteNow * settings.vignetteStrength};
                drawSprite(v, 4, 0, 3, tint, ident);
            }
            gammaApply(backend_->eyeFramebuffer(i), e.width, e.height);   // gamma (render/gamma.h): the eye's finished picture
            gl::DepthMask(gl::TRUE_);
            gl::Enable(gl::DEPTH_TEST);
            gl::Disable(gl::BLEND);
            gl::BindVertexArray(0);
        }
    };
    // the game's list once (a wide picture: the side zones of widescreen are drawn too), the eyes draw it
    view.draw(rt, 2 * 960, 960, f.prev, f.alpha);
    view.drawOverride = nullptr;
    view.opt.clearColor = clearWas;

    backend_->endFrame(fs_);
#if !defined(__ANDROID__)   // (quest: no window to show it in: the copy saved)
    backend_->present(f.outFbo, f.outW, f.outH);
#endif
    gl::BindFramebuffer(gl::FRAMEBUFFER, f.outFbo);
    gl::Viewport(0, 0, f.outW, f.outH);

    // the aim for the next tick
    lastHitDist = gun[1].dist;
    lastAim = aimFromRay(gcam, gunO, gunD, gun[1].dist, worldUp, settings.reloadPitch);
    std::copy(gun[1].hit, gun[1].hit + 3, lastAim.hit);
    pending_.valid = fs_.hand[1].active;
    pending_.x = std::clamp(lastAim.x, -4000.0f, 4000.0f);
    pending_.y = std::clamp(lastAim.y, -4000.0f, 4000.0f);
    pending_.onScreen = lastAim.onScreen;
    const float tr = fs_.hand[1].trigger;   // (a hysteresis: pulled at 0.6, let go at 0.4)
    triggerDown_ = triggerDown_ ? tr > 0.4f : tr > 0.6f;
    if (triggerDown_ && !wasDown_) {
        ++pending_.pulls;
        backend_->haptic(1, 0.7f, 0.06f);   // openxr: the gun's kick (the preview: nothing)
    }
    wasDown_ = triggerDown_;
    pending_.held = triggerDown_;

    if (settings.reloadDown && lastAim.down && !wasLow_) pending_.reload = true;
    wasLow_ = lastAim.down;
    // dual pistols: the left gun the same way (its own aim, trigger, rumble; pointing it down reloads the pair)
    pending_.dual = dual;
    if (gun[0].on) {
        lastAimLeft = aimFromRay(gcam, gun[0].o, gun[0].d, gun[0].dist, worldUp, settings.reloadPitch);
        std::copy(gun[0].hit, gun[0].hit + 3, lastAimLeft.hit);
        Aim::Gun& l = pending_.left;
        l.valid = true;
        l.x = std::clamp(lastAimLeft.x, -4000.0f, 4000.0f);
        l.y = std::clamp(lastAimLeft.y, -4000.0f, 4000.0f);
        l.onScreen = lastAimLeft.onScreen;
        const float tl = fs_.hand[0].trigger;
        triggerDownL_ = triggerDownL_ ? tl > 0.4f : tl > 0.6f;
        if (triggerDownL_ && !wasDownL_) {
            ++l.pulls;
            backend_->haptic(0, 0.7f, 0.06f);   // (the left controller's own kick)
        }
        wasDownL_ = triggerDownL_;
        l.held = triggerDownL_;
        if (settings.reloadDown && lastAimLeft.down && !wasLowL_) pending_.reload = true;
        wasLowL_ = lastAimLeft.down;
    } else {
        pending_.left = Aim::Gun{};
        triggerDownL_ = wasDownL_ = wasLowL_ = false;
    }
    // openxr: either hand's grip, the right A / B (Vive / WMR: the right trackpad), the left Y reload; the left X (Index:
    // left A; Vive / WMR: the left trackpad) is Start (join / continue); the menu button pauses
    for (int h = 0; h < 2; ++h) {
        const Controller& c = fs_.hand[h];
        gripDown_[h] = gripDown_[h] ? c.squeeze > 0.4f : c.squeeze > 0.6f;
        const bool reload = gripDown_[h] || c.secondary || (h == 1 && c.primary);
        if (reload && !wasReload_[h]) pending_.reload = true;
        wasReload_[h] = reload;
    }
    if (fs_.hand[0].primary && !wasStart_) pending_.start = true;
    wasStart_ = fs_.hand[0].primary;
    const bool menu = fs_.hand[0].menu || fs_.hand[1].menu;
    if (menu && !wasMenu_) menuPending_ = true;
    wasMenu_ = menu;
}

bool Scene::takeMenu()
{
    const bool m = menuPending_;
    menuPending_ = false;
    return m;
}

bool Scene::lost() const { return backend_ && backend_->lost(); }

Scene::Aim Scene::takeAim()
{
    Aim a = pending_;
    pending_.pulls = 0;
    pending_.reload = false;
    pending_.start = false;
    pending_.left.pulls = 0;
    return a;
}

// quest (2026-11-03): a flat screen on a big screen in VR (vr_scene.h panelFrame, docs/quest.md)
Scene::Pointer Scene::panelFrame(unsigned srcFbo, int w, int h, int pointW, int pointH)
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
        gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR_MIPMAP_LINEAR);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
        gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, screenTex_, 0);
        gl::framebufferOk("vr screen");
    }
    if (blit) {
        gl::BindFramebuffer(kRead, srcFbo);
        gl::BindFramebuffer(kDraw, screenFbo_);
        blit(0, 0, w, h, 0, 0, w, h, gl::COLOR_BUFFER_BIT, gl::NEAREST);
    }
    gl::BindTexture(gl::TEXTURE_2D, screenTex_);
    gl::GenerateMipmap(gl::TEXTURE_2D);

    backend_->setOutputSize(w, h);
    backend_->beginFrame(fs_);
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

    for (int i : {1, 0}) {
        const Eye& e = fs_.eye[i];
        gl::BindFramebuffer(gl::FRAMEBUFFER, backend_->eyeFramebuffer(i));
        gl::Viewport(0, 0, e.width, e.height);
        gl::ClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
        const Rigid eyeL = poseToLocal(e.pose, 1.0f);
        float view[16], vp[16];
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
                v[k * 5 + 3] = 0, v[k * 5 + 4] = 0;
            }
            const float tint[4] = {1.0f, trig ? 0.3f : 1.0f, trig ? 0.2f : 1.0f, 0.9f};
            drawSprite(v, 4, 0, 0, tint, vp);
        }
        gl::Disable(gl::BLEND);
        gl::Enable(gl::DEPTH_TEST);
        gl::BindVertexArray(0);
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
