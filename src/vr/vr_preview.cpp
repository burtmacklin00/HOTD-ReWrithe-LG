// VR mode: the desktop "preview" backend (vr.h; docs/vr.md "Preview"): VR without a headset, to build and test the VR
// picture and aiming. Two eyes side by side in the window (or one), a headset-like frustum per eye (wider toward the
// temple, as real lenses), the head on the numeric keypad, the gun on the mouse: the right hand's gun points at what
// the cursor shows (the ray from the eye through the cursor, as far as the eye's depth says), so a desktop player aims
// as with the mouse while the gun, the laser and the 3D crosshair do what a tracked controller would.
//   Keypad 4 / 6: turn the head left / right; 8 / 2: up / down; 7 / 9: roll; 1 / 3: lean left / right; 5: recentre.
//   Dual pistols: both guns point at the cursor's point; keypad 0 / . pull the left / right gun's trigger.
#include "vr/vr.h"
#include "vr/vr_math.h"

#include "render/gl.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hotd::vr {

namespace {

constexpr float kPi = 3.14159265f;

class Preview final : public Backend {
public:
    const char* name() const override { return "preview"; }

    bool init(const Settings& s, std::string& err) override
    {
        ipd_ = s.ipd;
        if (!makeProgram(err))
            return false;
        return true;
    }

    void shutdown() override
    {
        for (Target& t : eyes_) freeTarget(t);
        if (prog_) gl::DeleteProgram(prog_);
        if (vao_) gl::DeleteVertexArrays(1, &vao_);
        prog_ = vao_ = 0;
    }

    bool event(const SDL_Event& e) override
    {
        if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
            const bool down = e.type == SDL_EVENT_KEY_DOWN;
            switch (e.key.scancode) {
            case SDL_SCANCODE_KP_4: keys_[0] = down; return true;
            case SDL_SCANCODE_KP_6: keys_[1] = down; return true;
            case SDL_SCANCODE_KP_8: keys_[2] = down; return true;
            case SDL_SCANCODE_KP_2: keys_[3] = down; return true;
            case SDL_SCANCODE_KP_7: keys_[4] = down; return true;
            case SDL_SCANCODE_KP_9: keys_[5] = down; return true;
            case SDL_SCANCODE_KP_1: keys_[6] = down; return true;
            case SDL_SCANCODE_KP_3: keys_[7] = down; return true;
            case SDL_SCANCODE_KP_0: trigger_[0] = down ? 1.0f : 0.0f; return true;        // dual pistols: the left
            case SDL_SCANCODE_KP_PERIOD: trigger_[1] = down ? 1.0f : 0.0f; return true;   // / right gun's trigger
            case SDL_SCANCODE_KP_5:
                if (down) std::fill(ypr_, ypr_ + 3, 0.0f), std::fill(lean_, lean_ + 3, 0.0f);
                return true;
            default: break;
            }
        }
        if (e.type == SDL_EVENT_MOUSE_MOTION) {   // (not taken: the game sees the mouse too; VR replaces its aim)
            SDL_Window* w = SDL_GetWindowFromID(e.motion.windowID);
            int ww = 0, wh = 0;
            if (w && SDL_GetWindowSize(w, &ww, &wh) && ww > 0 && wh > 0) {
                const float x = e.motion.x / float(ww), y = e.motion.y / float(wh);
                if (display == 2)
                    setCursor(x < 0.5f ? 0 : 1, x < 0.5f ? x * 2 : x * 2 - 1, y);
                else
                    setCursor(display, x, y);
            }
        }
        return false;
    }

    void setOutputSize(int w, int h) override { outW_ = std::max(2, w), outH_ = std::max(2, h); }
    void setQuestSim(float scale) override { questScale_ = std::clamp(scale, 0.5f, 1.5f); }
    void setCursor(int eye, float u, float v) override
    {
        cursorEye_ = std::clamp(eye, 0, 1);
        cursor_[0] = u, cursor_[1] = v;
        haveCursor_ = true;
    }
    void scriptHead(const float ypr[3], const float lean[3]) override
    {
        for (int k = 0; k < 3; ++k) ypr_[k] = ypr[k] * kPi / 180.0f, lean_[k] = lean[k];
        scripted_ = true;
    }
    void cursorDistance(float m) override { cursorDist_ = std::clamp(m, 0.3f, 200.0f); }
    void setDual(bool on) override { dual_ = on; }
    void scriptTrigger(int hand, float v) override { trigger_[std::clamp(hand, 0, 1)] = std::clamp(v, 0.0f, 1.0f); }

    bool desktopCursorRay(float o[3], float d[3]) override
    {
        if (!haveCursor_ || !last_.eye[cursorEye_].pose.valid)
            return false;
        const Eye& e = last_.eye[cursorEye_];
        float r[9];
        quatToMatrix(e.pose.rot, r);
        const float tx = e.fov.tanLeft + cursor_[0] * (e.fov.tanRight - e.fov.tanLeft);
        const float ty = e.fov.tanUp - cursor_[1] * (e.fov.tanUp - e.fov.tanDown);
        const float l[3] = {tx, ty, -1.0f};
        for (int k = 0; k < 3; ++k) {
            o[k] = e.pose.pos[k];
            d[k] = r[k * 3] * l[0] + r[k * 3 + 1] * l[1] + r[k * 3 + 2] * l[2];
        }
        normalize3(d);
        return true;
    }

    // questsim: HOTD_QUEST_LIFECYCLE=F (the test): from the preview's frame F the Quest's lifecycle scripted: 300
    // frames (5 s) VISIBLE not FOCUSED (the Meta menu over the game), FOCUSED 300, then STOPPING 300 (the headset off:
    // nothing shown, READY -> FOCUSED again after), FOCUSED 300, then the app paused 2 s (SDL's Android activity blocks
    // the loop in onPause) and resumed
    bool focused() const override { return phase_ != 1 && phase_ != 3; }
    void lifecycle(FrameState& fs)
    {
        static const char* env = std::getenv("HOTD_QUEST_LIFECYCLE");
        if (!env || questScale_ <= 0.0f) return;
        const long long f = (long long)frameNo_++ - std::atoll(env);
        const int phase = f < 0 ? 0 : f < 300 ? 1 : f < 600 ? 2 : f < 900 ? 3 : f < 1200 ? 4 : f == 1200 ? 5 : 6;
        if (phase != phase_) {
            static const char* names[] = {"FOCUSED", "VISIBLE (the Meta menu)", "FOCUSED", "STOPPING (headset off)",
                                          "READY -> FOCUSED", "the app paused 2 s", "resumed, FOCUSED"};
            SDL_Log("quest-sim lifecycle: preview frame %llu: %s", (unsigned long long)frameNo_ - 1, names[phase]);
            std::printf("quest-sim lifecycle: %s\n", names[phase]);
            phase_ = phase;
            if (phase == 5) SDL_Delay(2000);
        }
        if (phase_ == 3) fs.shouldRender = false;
        if (phase_ == 1 || phase_ == 3) { trigger_[0] = trigger_[1] = 0.0f; }   // (OpenXR: no input unless FOCUSED)
    }

    bool beginFrame(FrameState& fs) override
    {
        const uint64_t now = SDL_GetTicksNS();
        const float dt = lastNs_ ? std::min(0.1f, float(double(now - lastNs_) / 1e9)) : 0.0f;
        lastNs_ = now;
        if (!scripted_) {   // the keypad: 90 degrees / s, 0.5 m / s
            const float turn = kPi / 2 * dt, move = 0.5f * dt;
            ypr_[0] += (keys_[0] ? turn : 0) - (keys_[1] ? turn : 0);
            ypr_[1] += (keys_[2] ? turn : 0) - (keys_[3] ? turn : 0);
            ypr_[2] += (keys_[5] ? turn : 0) - (keys_[4] ? turn : 0);
            lean_[0] += (keys_[7] ? move : 0) - (keys_[6] ? move : 0);
            ypr_[1] = std::clamp(ypr_[1], -1.4f, 1.4f);
            lean_[0] = std::clamp(lean_[0], -0.5f, 0.5f);
        }
        fs = FrameState{};
        fs.displayTime = int64_t(now);
        fs.head.valid = true;
        quatFromYawPitchRoll(ypr_[0], ypr_[1], ypr_[2], fs.head.rot);
        std::copy(lean_, lean_ + 3, fs.head.pos);
        float hr[9];
        quatToMatrix(fs.head.rot, hr);
        // the eyes: half the IPD left / right of the head along its x axis; each eye's picture = its half of the
        // window (or the whole one); a headset-like frustum, 10 % wider toward the temple, 100 degrees high
        const bool both = display == 2;
        const int ew = both ? outW_ / 2 : outW_, eh = outH_;
        const float tanV = std::tan(50.0f * kPi / 180.0f), aspect = float(ew) / float(eh), tanH = tanV * aspect;
        for (int i = 0; i < 2; ++i) {
            Eye& e = fs.eye[i];
            const float side = i == 0 ? -0.5f : 0.5f;
            for (int k = 0; k < 3; ++k)
                e.pose.pos[k] = fs.head.pos[k] + hr[k * 3] * side * ipd_;
            std::copy(fs.head.rot, fs.head.rot + 4, e.pose.rot);
            e.pose.valid = true;
            const float outer = tanH * 1.05f, inner = tanH * 0.95f;   // (the same total width as a symmetric one)
            e.fov.tanLeft = i == 0 ? -outer : -inner;
            e.fov.tanRight = i == 0 ? inner : outer;
            e.fov.tanUp = tanV;
            e.fov.tanDown = -tanV;
            e.width = ew, e.height = eh;
            if (questScale_ > 0.0f) {
                // questsim: the Quest 3 (docs/quest.md "Simulating the Quest 3"): Meta's default eye buffer 1680 x 1760
                // (x resolution_scale), its per-eye frustum (about 52 degrees to the temple, 44 to the nose, 48 up and
                // down: ~110 x 96 for the pair)
                const float t52 = std::tan(52.0f * kPi / 180.0f), t44 = std::tan(44.0f * kPi / 180.0f),
                            t48 = std::tan(48.0f * kPi / 180.0f);
                e.fov.tanLeft = i == 0 ? -t52 : -t44;
                e.fov.tanRight = i == 0 ? t44 : t52;
                e.fov.tanUp = t48, e.fov.tanDown = -t48;
                e.width = int(1680 * questScale_ + 0.5f) & ~1, e.height = int(1760 * questScale_ + 0.5f) & ~1;
            }
            ensureTarget(eyes_[i], e.width, e.height);
        }
        // the right hand's gun: below and right of the head (turned with its heading only), pointing at what the
        // cursor shows (cursorDist_: the scene's depth along the cursor ray); without a cursor: straight ahead
        const float yawOnly[4] = {0, std::sin(ypr_[0] / 2), 0, std::cos(ypr_[0] / 2)};
        float yr[9];
        quatToMatrix(yawOnly, yr);
        // (dual pistols: the left hand holds its gun as the right one, mirrored, and points it at the cursor's point too)
        const float hand[2][3] = {{dual_ ? -0.17f : -0.20f, dual_ ? -0.25f : -0.32f, dual_ ? -0.32f : -0.30f}, {0.17f, -0.25f, -0.32f}};
        for (int h = 0; h < 2; ++h) {
            Controller& c = fs.hand[h];
            c.active = true;
            float p[3];
            for (int k = 0; k < 3; ++k)
                p[k] = fs.head.pos[k] + yr[k * 3] * hand[h][0] + yr[k * 3 + 1] * hand[h][1] + yr[k * 3 + 2] * hand[h][2];
            float fwd[3] = {-yr[2], -yr[5] - 0.35f, -yr[8]};   // (the idle left hand: ahead, a little down)
            c.trigger = trigger_[h];
            if (h == 1 || dual_) {
                float o[3], d[3];
                last_ = fs;   // (the cursor ray from this frame's eyes)
                if (desktopCursorRay(o, d)) {
                    for (int k = 0; k < 3; ++k)
                        fwd[k] = o[k] + d[k] * cursorDist_ - p[k];
                } else {
                    fwd[0] = -yr[2], fwd[1] = -yr[5], fwd[2] = -yr[8];
                }
            }
            normalize3(fwd);
            // a look rotation: -z along fwd, +y as near the world's up as it goes
            const float wup[3] = {0, 1, 0};
            float x[3], y[3];
            cross3(wup, fwd, x);   // (right = up x back = -(up x fwd))
            x[0] = -x[0], x[1] = -x[1], x[2] = -x[2];
            if (dot3(x, x) < 1e-6f) x[0] = 1, x[1] = x[2] = 0;
            normalize3(x);
            const float back[3] = {-fwd[0], -fwd[1], -fwd[2]};
            cross3(back, x, y);
            const float r[9] = {x[0], y[0], back[0], x[1], y[1], back[1], x[2], y[2], back[2]};
            std::copy(p, p + 3, c.grip.pos);
            matrixToQuat(r, c.grip.rot);
            c.grip.valid = true;
            c.aim = c.grip;
        }
        scriptedButtons(fs);
        lifecycle(fs);
        if (!focused()) fs.hand[0] = fs.hand[1] = Controller{};   // (questsim: as OpenXR outside FOCUSED)
        last_ = fs;
        return true;
    }

    unsigned eyeFramebuffer(int i) override { return eyes_[i].fbo; }
    unsigned eyeTexture(int i) override { return eyes_[i].color; }
    void endFrame(const FrameState&) override {}

    void present(unsigned fbo, int width, int height) override
    {
        gl::BindFramebuffer(gl::FRAMEBUFFER, fbo);
        gl::Viewport(0, 0, width, height);
        gl::ClearColor(0, 0, 0, 1);
        gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
        gl::Disable(gl::DEPTH_TEST);
        gl::Disable(gl::BLEND);
        gl::UseProgram(prog_);
        gl::BindVertexArray(vao_);
        gl::ActiveTexture(gl::TEXTURE0);
        for (int i = 0; i < 2; ++i) {
            if (display != 2 && display != i)
                continue;
            const int x0 = display == 2 ? (i == 0 ? 0 : width / 2) : 0, w = display == 2 ? width / 2 : width;
            gl::Viewport(x0, 0, w, height);
            gl::BindTexture(gl::TEXTURE_2D, eyes_[i].color);
            gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
        }
        gl::BindVertexArray(0);
        gl::Viewport(0, 0, width, height);
        gl::Enable(gl::DEPTH_TEST);
    }

private:
    // vrflow (tests): HOTD_VR_BUTTONS="<frame>:<hand><button> ..." presses a controller button for 4 preview frames
    // from that frame (beginFrame calls: the game's and the big screen's frames alike). hand l / r; button t trigger,
    // a primary (A / X), b secondary (B), y the left Y (OpenXR: bound to Pause, as the menu button), m menu, g grip, < / > the stick left / right (vrweapon);
    // x: the runtime asks the app to quit (OpenXR EXITING: lost() + exitAsked(), the game closes).
    // E.g. "200:rt 400:ra 1500:ly 1700:ly": the attract's trigger, A on the menu, pause, Continue
    void scriptedButtons(FrameState& fs)
    {
        static const char* env = std::getenv("HOTD_VR_BUTTONS");
        if (!env) return;
        const uint64_t n = btnFrame_++;
        for (const char* s = env; *s;) {
            char* end = nullptr;
            const unsigned long long at = std::strtoull(s, &end, 10);
            if (end == s || *end != ':' || !end[1] || !end[2]) break;
            const int h = end[1] == 'l' ? 0 : 1;
            const char b = end[2];
            if (n >= at && n < at + 4) {
                Controller& c = fs.hand[h];
                if (b == 't') c.trigger = 1.0f;
                else if (b == 'a') c.primary = true;
                else if (b == 'b') c.secondary = true;
                else if (b == 'y' || b == 'm') c.menu = true;   // (the left Y: OpenXR's Pause action, as the menu)
                else if (b == 'g') c.squeeze = 1.0f;
                else if (b == '<') c.stick[0] = -1.0f;   // (vrweapon: the stick flicked left / right)
                else if (b == '>') c.stick[0] = 1.0f;
                else if (b == 'x') exit_ = true;   // (the runtime asks the app to quit: OpenXR EXITING)
                if (n == at) SDL_Log("vr preview: scripted %s %c at frame %llu", h ? "right" : "left", b, at);
            }
            s = end + 3;
            while (*s == ' ') ++s;
        }
    }
    uint64_t btnFrame_ = 0;
    bool exit_ = false;

public:
    bool lost() const override { return exit_; }
    bool exitAsked() const override { return exit_; }

private:
    struct Target {
        unsigned fbo = 0, color = 0, depth = 0;
        int w = 0, h = 0;
    };
    Target eyes_[2];
    float ipd_ = 0.064f;
    int outW_ = 1280, outH_ = 720;
    float questScale_ = 0.0f;   // questsim: > 0 the Quest 3's eyes (setQuestSim)
    uint64_t frameNo_ = 0;      // questsim: HOTD_QUEST_LIFECYCLE's clock (beginFrame calls)
    int phase_ = 0;
    float ypr_[3] = {0, 0, 0}, lean_[3] = {0, 0, 0};
    bool keys_[8] = {};
    bool scripted_ = false;
    bool dual_ = false;
    float trigger_[2] = {0, 0};
    uint64_t lastNs_ = 0;
    int cursorEye_ = 1;
    float cursor_[2] = {0.5f, 0.5f};
    bool haveCursor_ = false;
    float cursorDist_ = 8.0f;
    FrameState last_;
    gl::GLuint prog_ = 0, vao_ = 0;

    static void freeTarget(Target& t)
    {
        if (t.fbo) gl::DeleteFramebuffers(1, &t.fbo);
        if (t.color) gl::DeleteTextures(1, &t.color);
        if (t.depth) gl::DeleteRenderbuffers(1, &t.depth);
        t = Target{};
    }
    static void ensureTarget(Target& t, int w, int h)
    {
        if (t.fbo && t.w == w && t.h == h)
            return;
        freeTarget(t);
        t.w = w, t.h = h;
        gl::GenFramebuffers(1, &t.fbo);
        gl::BindFramebuffer(gl::FRAMEBUFFER, t.fbo);
        gl::GenTextures(1, &t.color);
        gl::BindTexture(gl::TEXTURE_2D, t.color);
        gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
        gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, t.color, 0);
        gl::GenRenderbuffers(1, &t.depth);
        gl::BindRenderbuffer(gl::RENDERBUFFER, t.depth);
        gl::RenderbufferStorage(gl::RENDERBUFFER, gl::DEPTH_COMPONENT24, w, h);
        gl::FramebufferRenderbuffer(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::RENDERBUFFER, t.depth);
        gl::framebufferOk("vr preview eye");
    }
    bool makeProgram(std::string& err)
    {
        static const char* vs = R"(#version 330 core
out vec2 vUv;
void main() {
    vec2 p = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";
        static const char* fs = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
out vec4 oColor;
void main() { oColor = vec4(texture(uTex, vUv).rgb, 1.0); })";
        auto compile = [&](gl::GLenum type, const char* src) {
            const gl::GLuint s = gl::CreateShader(type);
            gl::ShaderSource(s, 1, &src, nullptr);
            gl::CompileShader(s);
            if (!gl::shaderLog(s, "vr preview")) {
                err = "vr preview: shader";
                return gl::GLuint(0);
            }
            return s;
        };
        const gl::GLuint v = compile(gl::VERTEX_SHADER, vs), f = compile(gl::FRAGMENT_SHADER, fs);
        if (!v || !f)
            return false;
        prog_ = gl::CreateProgram();
        gl::AttachShader(prog_, v);
        gl::AttachShader(prog_, f);
        gl::LinkProgram(prog_);
        gl::DeleteShader(v);
        gl::DeleteShader(f);
        if (!gl::programLog(prog_, "vr preview")) {
            err = "vr preview: program";
            return false;
        }
        gl::UseProgram(prog_);
        gl::Uniform1i(gl::GetUniformLocation(prog_, "uTex"), 0);
        gl::GenVertexArrays(1, &vao_);
        return true;
    }
};

} // namespace

std::unique_ptr<Backend> makePreview() { return std::make_unique<Preview>(); }

} // namespace hotd::vr
