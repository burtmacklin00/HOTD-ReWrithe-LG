// VR mode: the OpenXR backend (vr.h; docs/vr.md "OpenXR backend"). Built when CMake finds the Khronos OpenXR SDK's
// loader (HOTD_OPENXR, Apache 2.0; openxr_loader.dll beside hotd.exe); without it vr.cpp's makeOpenXr returns null and
// --vr openxr says so. Every call is the OpenXR 1.0 core API or XR_KHR_opengl_enable, in the spec's order. Written
// 2026-11-03 on a PC without an OpenXR runtime or a headset: the start-up failure path (no runtime, the headset off,
// the DirectX renderer) is tested, a real session is not; the places to look at on the first headset run are marked
// CHECK (docs/vr.md "First headset test").
//
// Life cycle:
//   init:     the runtime's extensions (XR_KHR_opengl_enable there?) -> xrCreateInstance (API 1.0) -> xrGetSystem (HMD)
//             -> xrGetOpenGLGraphicsRequirementsKHR (the spec: before xrCreateSession) -> xrCreateSession
//             (XrGraphicsBindingOpenGLWin32KHR: the game's HDC / HGLRC, current on this thread; Xlib on Linux) ->
//             xrCreateReferenceSpace (LOCAL: seated, the origin at the head) -> the action set (trigger, squeeze, grip /
//             aim poses, A / B, menu, haptics per hand) with bindings for the simple, Touch, Index, Vive and Windows
//             Mixed Reality controllers -> xrAttachSessionActionSets -> one swapchain per eye (the recommended size,
//             SRGB8_ALPHA8 preferred) and an FBO + depth renderbuffer per swapchain image
//   a frame:  xrPollEvent (READY -> xrBeginSession, STOPPING -> xrEndSession, EXITING / LOSS_PENDING -> lost(): the game
//             drops VR and plays flat) -> running: xrWaitFrame -> xrBeginFrame -> xrLocateViews -> xrSyncActions + the
//             hands -> shouldRender: per eye xrAcquireSwapchainImage + xrWaitSwapchainImage, the scene draws into its
//             FBO, xrReleaseSwapchainImage -> xrEndFrame (one projection layer; none when not rendered). Not running /
//             not shown: the scene draws into the backend's own mirror target (the window still shows the game)
//   present:  the left eye's picture into the desktop window (a mirror, letterboxed)
#ifdef HOTD_OPENXR

#include "vr/vr.h"
#include "vr/vr_math.h"

#include "render/gl.h"

#include <SDL3/SDL.h>

#if defined(_WIN32)
#define XR_USE_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>   // (openxr_platform.h's Win32 part names IUnknown; WIN32_LEAN_AND_MEAN leaves it out)
#define XR_USE_GRAPHICS_API_OPENGL
#elif defined(__ANDROID__)
// quest (2026-11-03, docs/quest.md): the standalone headset. The Khronos loader for Android (libopenxr_loader.so in the
// APK) finds Meta's runtime through the runtime broker; XR_KHR_loader_init_android (the Java VM and the activity),
// XR_KHR_android_create_instance, XR_KHR_opengl_es_enable (the game's EGL display / config / context)
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <EGL/egl.h>
#include <jni.h>
#else
#define XR_USE_PLATFORM_XLIB   // CHECK (Linux): the GLX binding needs the X display / drawable / context
#include <GL/glx.h>
#define XR_USE_GRAPHICS_API_OPENGL
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace hotd::vr {

namespace {

#if defined(__ANDROID__)
// quest: the OpenGL ES names of the GL binding (the same calls and order as the desktop's)
constexpr const char* kGlExtension = XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME;
constexpr const char* kGlRequirements = "xrGetOpenGLESGraphicsRequirementsKHR";
using GraphicsRequirements = XrGraphicsRequirementsOpenGLESKHR;
using PFN_GraphicsRequirements = PFN_xrGetOpenGLESGraphicsRequirementsKHR;
constexpr XrStructureType kGraphicsRequirementsType = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR;
using SwapchainImage = XrSwapchainImageOpenGLESKHR;
constexpr XrStructureType kSwapchainImageType = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
#else
constexpr const char* kGlExtension = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
constexpr const char* kGlRequirements = "xrGetOpenGLGraphicsRequirementsKHR";
using GraphicsRequirements = XrGraphicsRequirementsOpenGLKHR;
using PFN_GraphicsRequirements = PFN_xrGetOpenGLGraphicsRequirementsKHR;
constexpr XrStructureType kGraphicsRequirementsType = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
using SwapchainImage = XrSwapchainImageOpenGLKHR;
constexpr XrStructureType kSwapchainImageType = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
#endif

constexpr gl::GLenum kRGBA8 = 0x8058, kSRGB8A8 = 0x8C43;
constexpr gl::GLenum kFramebufferSrgb = 0x8DB9;   // GL_FRAMEBUFFER_SRGB
constexpr gl::GLenum kReadFramebuffer = 0x8CA8, kDrawFramebuffer = 0x8CA9, kMajorVersion = 0x821B, kMinorVersion = 0x821C;
using PFN_Blit = void(HOTD_APIENTRY*)(gl::GLint, gl::GLint, gl::GLint, gl::GLint, gl::GLint, gl::GLint, gl::GLint, gl::GLint,
                                      gl::GLbitfield, gl::GLenum);
using PFN_GetIntegerv = void(HOTD_APIENTRY*)(gl::GLenum, gl::GLint*);
constexpr int kMirrorSize = 1024;   // the backend's own eye target while the headset does not show the game

// a failed call's reason in words (the usual start-up failures; the rest by number and name when an instance exists)
std::string reason(XrInstance inst, XrResult r)
{
    switch (r) {
    case XR_ERROR_RUNTIME_UNAVAILABLE:
#if defined(__ANDROID__)
        return "no OpenXR runtime on this device (a Meta Quest has one built in)";
#else
        return "no OpenXR runtime is installed / set active (SteamVR, Meta Quest Link, Windows Mixed Reality or Virtual "
               "Desktop: make it the OpenXR runtime)";
#endif
    case XR_ERROR_RUNTIME_FAILURE: return "the OpenXR runtime failed to start";
    case XR_ERROR_FORM_FACTOR_UNAVAILABLE: return "no headset connected (or it is off / asleep)";
    case XR_ERROR_FORM_FACTOR_UNSUPPORTED: return "the runtime has no head-mounted display";
    case XR_ERROR_EXTENSION_NOT_PRESENT: return "the runtime has no OpenGL support (XR_KHR_opengl_enable)";
    case XR_ERROR_API_VERSION_UNSUPPORTED: return "the runtime does not support OpenXR 1.0";
    case XR_ERROR_GRAPHICS_DEVICE_INVALID: return "the game's graphics card / context is not one the runtime can use";
    case XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING: return "graphics requirements not queried (a bug)";
    default: break;
    }
    char name[XR_MAX_RESULT_STRING_SIZE] = {};
    if (inst != XR_NULL_HANDLE && XR_SUCCEEDED(xrResultToString(inst, r, name)))
        return std::string(name) + " (" + std::to_string(int(r)) + ")";
    return "error " + std::to_string(int(r));
}

class OpenXr final : public Backend {
public:
    ~OpenXr() override { shutdown(); }
    const char* name() const override { return "openxr"; }
    bool lost() const override { return lost_; }

    bool init(const Settings& settings, std::string& err) override
    {
        srgbWanted_ = settings.srgbSwapchain;
        resolutionScale_ = settings.resolutionScale;
        // the runtime first: no runtime / no OpenGL there gives a clear reason before anything is made
#if defined(_WIN32)
        // openxr_loader.dll is delay-loaded (CMakeLists.txt): load it now, a missing DLL said instead of a crash
        if (!LoadLibraryW(L"openxr_loader.dll")) {
            err = "openxr: openxr_loader.dll is missing beside hotd.exe (the OpenXR loader)";
            return false;
        }
        // (no active runtime registered: said here in one line; the loader would print five lines of its own)
        if (!std::getenv("XR_RUNTIME_JSON")) {
            DWORD size = 0;
            if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime", RRF_RT_REG_SZ, nullptr,
                             nullptr, &size) != ERROR_SUCCESS)
                return fail(err, "no ActiveRuntime in the registry", XR_ERROR_RUNTIME_UNAVAILABLE);
        }
#endif
#if defined(__ANDROID__)
        // quest: the loader needs the Java VM and the activity before any other call (XR_KHR_loader_init_android)
        if (!initAndroid(err)) return false;
#endif
        uint32_t n = 0;
        XrResult r = xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr);
        if (XR_FAILED(r)) return fail(err, "no runtime", r);
        std::vector<XrExtensionProperties> exts(n, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
        xrEnumerateInstanceExtensionProperties(nullptr, n, &n, exts.data());
        if (std::none_of(exts.begin(), exts.end(), [](const XrExtensionProperties& e) {
                return std::strcmp(e.extensionName, kGlExtension) == 0;
            }))
            return fail(err, "extensions", XR_ERROR_EXTENSION_NOT_PRESENT);
#if defined(_WIN32)
        // the GL binding needs a WGL context: the DirectX renderer (ANGLE) has none
        hdc_ = wglGetCurrentDC();
        hglrc_ = wglGetCurrentContext();
        if (!hdc_ || !hglrc_) {
            err = "openxr: needs the OpenGL renderer (Video > Renderer: OpenGL, or --renderer gl); the DirectX one cannot "
                  "hand its pictures to a headset";
            return false;
        }
#endif
#if defined(__ANDROID__)
        const char* ext[] = {kGlExtension, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME};
        XrInstanceCreateInfoAndroidKHR android{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
        android.applicationVM = vm_;
        android.applicationActivity = activity_;
#else
        const char* ext[] = {kGlExtension};
#endif
        XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
#if defined(__ANDROID__)
        ici.next = &android;
#endif
        std::strncpy(ici.applicationInfo.applicationName, "HOTD ReWrithe", XR_MAX_APPLICATION_NAME_SIZE - 1);
        ici.applicationInfo.applicationVersion = 1;
        std::strncpy(ici.applicationInfo.engineName, "hotd", XR_MAX_ENGINE_NAME_SIZE - 1);
        ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;   // (all this needs; 1.1 runtimes take 1.0 applications)
        ici.enabledExtensionCount = uint32_t(sizeof ext / sizeof ext[0]);
        ici.enabledExtensionNames = ext;
        if (r = xrCreateInstance(&ici, &instance_); XR_FAILED(r)) return fail(err, "xrCreateInstance", r);
        XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
        if (XR_SUCCEEDED(xrGetInstanceProperties(instance_, &ip)))
            SDL_Log("openxr: runtime %s %u.%u.%u", ip.runtimeName, unsigned(XR_VERSION_MAJOR(ip.runtimeVersion)),
                    unsigned(XR_VERSION_MINOR(ip.runtimeVersion)), unsigned(XR_VERSION_PATCH(ip.runtimeVersion)));
        XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
        sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        if (r = xrGetSystem(instance_, &sgi, &system_); XR_FAILED(r)) return fail(err, "xrGetSystem", r);
        XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
        if (XR_SUCCEEDED(xrGetSystemProperties(instance_, system_, &sp)))
            SDL_Log("openxr: headset %s", sp.systemName);
        // XR_KHR_opengl_enable: its requirements call is mandatory before xrCreateSession
        PFN_GraphicsRequirements req = nullptr;
        xrGetInstanceProcAddr(instance_, kGlRequirements, reinterpret_cast<PFN_xrVoidFunction*>(&req));
        GraphicsRequirements gr{kGraphicsRequirementsType};
        if (!req) return fail(err, kGlRequirements, XR_ERROR_FUNCTION_UNSUPPORTED);
        if (r = req(instance_, system_, &gr); XR_FAILED(r)) return fail(err, kGlRequirements, r);
        {   // the context's version against the runtime's range (logged: runtimes ask 4.x but run 3.3 core contexts)
            auto getInt = reinterpret_cast<PFN_GetIntegerv>(SDL_GL_GetProcAddress("glGetIntegerv"));
            gl::GLint major = 0, minor = 0;
            if (getInt) getInt(kMajorVersion, &major), getInt(kMinorVersion, &minor);
            const XrVersion have = XR_MAKE_VERSION(major, minor, 0);
            SDL_Log("openxr: OpenGL %d.%d, the runtime asks %u.%u..%u.%u%s", major, minor,
                    unsigned(XR_VERSION_MAJOR(gr.minApiVersionSupported)), unsigned(XR_VERSION_MINOR(gr.minApiVersionSupported)),
                    unsigned(XR_VERSION_MAJOR(gr.maxApiVersionSupported)), unsigned(XR_VERSION_MINOR(gr.maxApiVersionSupported)),
                    have < gr.minApiVersionSupported ? " (below its minimum: trying anyway)" : "");
        }
#if defined(_WIN32)
        XrGraphicsBindingOpenGLWin32KHR gb{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
        gb.hDC = hdc_;
        gb.hGLRC = hglrc_;   // the game's context (current on this thread; the runtime shares its textures with it)
#elif defined(__ANDROID__)
        // quest: the game's EGL context (SDL's, current on this thread) and the config it was made with
        XrGraphicsBindingOpenGLESAndroidKHR gb{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
        gb.display = eglGetCurrentDisplay();
        gb.context = eglGetCurrentContext();
        {
            EGLint id = 0, count = 0;
            eglQueryContext(gb.display, gb.context, EGL_CONFIG_ID, &id);
            const EGLint want[] = {EGL_CONFIG_ID, id, EGL_NONE};
            EGLConfig cfg = nullptr;
            if (eglChooseConfig(gb.display, want, &cfg, 1, &count) && count == 1) gb.config = cfg;
        }
        if (gb.display == EGL_NO_DISPLAY || gb.context == EGL_NO_CONTEXT) {
            err = "openxr: no current EGL context (the game's OpenGL ES context)";
            shutdown();
            return false;
        }
#else
        XrGraphicsBindingOpenGLXlibKHR gb{XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
        gb.xDisplay = glXGetCurrentDisplay();
        gb.glxDrawable = glXGetCurrentDrawable();
        gb.glxContext = glXGetCurrentContext();   // CHECK (Linux): visualid / glxFBConfig as the runtime needs
#endif
        XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
        sci.next = &gb;
        sci.systemId = system_;
        if (r = xrCreateSession(instance_, &sci, &session_); XR_FAILED(r)) return fail(err, "xrCreateSession", r);
        XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;   // seated: the arcade's on-rails view around the head
        rs.poseInReferenceSpace.orientation.w = 1;
        if (r = xrCreateReferenceSpace(session_, &rs, &space_); XR_FAILED(r)) return fail(err, "xrCreateReferenceSpace", r);
        if (!makeActions(err)) return false;
        if (!makeSwapchains(err)) return false;
        makeMirror();
        blit_ = reinterpret_cast<PFN_Blit>(SDL_GL_GetProcAddress("glBlitFramebuffer"));
        SDL_Log("openxr: session made (%dx%d per eye, %s); waiting for the headset", chains_[0].w, chains_[0].h,
                format_ == kSRGB8A8 ? "sRGB" : "RGBA8");
        return true;
    }

    void shutdown() override
    {
        if (session_ && running_) xrEndSession(session_);
        running_ = false;
        for (Chain& c : chains_) {
            for (Image& im : c.images) {
                if (im.fbo) gl::DeleteFramebuffers(1, &im.fbo);
                if (im.depth) gl::DeleteRenderbuffers(1, &im.depth);
            }
            if (c.handle) xrDestroySwapchain(c.handle);
        }
        chains_.clear();
        if (mirrorFbo_) gl::DeleteFramebuffers(1, &mirrorFbo_);
        if (mirrorTex_) gl::DeleteTextures(1, &mirrorTex_);
        if (mirrorDepth_) gl::DeleteRenderbuffers(1, &mirrorDepth_);
        mirrorFbo_ = mirrorTex_ = mirrorDepth_ = 0;
        for (int h = 0; h < 2; ++h) {
            if (gripSpace_[h]) xrDestroySpace(gripSpace_[h]);
            if (aimSpace_[h]) xrDestroySpace(aimSpace_[h]);
            gripSpace_[h] = aimSpace_[h] = XR_NULL_HANDLE;
        }
        if (actions_) xrDestroyActionSet(actions_);   // (its actions with it)
        if (space_) xrDestroySpace(space_);
        if (session_) xrDestroySession(session_);
        if (instance_) xrDestroyInstance(instance_);
        instance_ = XR_NULL_HANDLE, session_ = XR_NULL_HANDLE, space_ = XR_NULL_HANDLE, actions_ = XR_NULL_HANDLE;
    }

    bool beginFrame(FrameState& fs) override
    {
        pollEvents();
        fs = FrameState{};
        frameBegun_ = acquired_ = false;
        if (running_ && !lost_) {
            XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
            frameState_ = XrFrameState{XR_TYPE_FRAME_STATE};
            if (XR_SUCCEEDED(xrWaitFrame(session_, &wi, &frameState_))) {
                XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
                frameBegun_ = XR_SUCCEEDED(xrBeginFrame(session_, &bi));   // (FRAME_DISCARDED is a success code)
            }
        }
        // the eyes at the predicted display time (the last known when not running)
        bool viewsOk = false;
        if (frameBegun_) {
            XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
            li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            li.displayTime = frameState_.predictedDisplayTime;
            li.space = space_;
            XrViewState vs{XR_TYPE_VIEW_STATE};
            XrView v[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
            uint32_t n = 0;
            viewsOk = XR_SUCCEEDED(xrLocateViews(session_, &li, &vs, 2, &n, v)) && n == 2 &&
                      (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);
            if (viewsOk) {
                views_[0] = v[0], views_[1] = v[1];
                haveViews_ = true;
            }
        }
        fs.displayTime = frameBegun_ ? frameState_.predictedDisplayTime : int64_t(SDL_GetTicksNS());
        // shown in the headset: the frame begun, shouldRender, eyes located -> the swapchain images; else the mirror
        toHeadset_ = frameBegun_ && frameState_.shouldRender == XR_TRUE && viewsOk;
        fs.shouldRender = toHeadset_;
        for (int i = 0; i < 2; ++i) {
            Eye& e = fs.eye[i];
            if (haveViews_) {
                toPose(views_[i].pose, e.pose, true);
                e.fov.tanLeft = std::tan(views_[i].fov.angleLeft);
                e.fov.tanRight = std::tan(views_[i].fov.angleRight);
                e.fov.tanUp = std::tan(views_[i].fov.angleUp);
                e.fov.tanDown = std::tan(views_[i].fov.angleDown);
            } else {   // (no headset picture yet: a plain 100-degree view from the origin, the eyes 64 mm apart)
                e.pose.pos[0] = i ? 0.032f : -0.032f;
                e.pose.valid = true;
                e.fov = Fov{-1.19f, 1.19f, 1.19f, -1.19f};
            }
            e.width = toHeadset_ ? chains_[size_t(i)].w : kMirrorSize;
            e.height = toHeadset_ ? chains_[size_t(i)].h : kMirrorSize;
        }
        fs.head = fs.eye[0].pose;
        for (int k = 0; k < 3; ++k) fs.head.pos[k] = 0.5f * (fs.eye[0].pose.pos[k] + fs.eye[1].pose.pos[k]);
        // the controllers (xrSyncActions: only while focused; other states keep the last)
        if (frameBegun_ && state_ == XR_SESSION_STATE_FOCUSED) {
            const XrActiveActionSet active{actions_, XR_NULL_PATH};
            XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
            si.countActiveActionSets = 1;
            si.activeActionSets = &active;
            if (XR_SUCCEEDED(xrSyncActions(session_, &si))) {
                for (int h = 0; h < 2; ++h) {
                    Controller& c = hands_[h];
                    c = Controller{};
                    const bool g = locate(gripSpace_[h], c.grip), a = locate(aimSpace_[h], c.aim);
                    c.active = g || a;
                    c.trigger = getFloat(trigger_, hand_[h]);
                    c.squeeze = getFloat(squeeze_, hand_[h]);
                    c.primary = getBool(primary_, hand_[h]);
                    c.secondary = getBool(secondary_, hand_[h]);
                    c.menu = getBool(menu_, hand_[h]);
                }
            }
        } else if (state_ != XR_SESSION_STATE_FOCUSED) {
            hands_[0] = hands_[1] = Controller{};   // (the dashboard over the game: no shots through it)
        }
        fs.hand[0] = hands_[0], fs.hand[1] = hands_[1];
        // this frame's swapchain images
        if (toHeadset_) {
            acquired_ = true;
            for (int i = 0; i < 2; ++i) {
                Chain& ch = chains_[size_t(i)];
                XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                XrSwapchainImageWaitInfo wi2{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                wi2.timeout = 1000000000;   // 1 s: never hang the game on a stuck runtime
                const bool got = XR_SUCCEEDED(xrAcquireSwapchainImage(ch.handle, &ai, &ch.current));
                const XrResult w = got ? xrWaitSwapchainImage(ch.handle, &wi2) : XR_ERROR_CALL_ORDER_INVALID;
                ch.held = got && w == XR_SUCCESS;   // (released in endFrame only when its wait succeeded, as the spec asks)
                if (!ch.held) {
                    acquired_ = false;
                    if (got) {   // (a runtime stuck a whole second: VR is dropped, the game goes on flat)
                        SDL_Log("openxr: the swapchain image did not come (%s): leaving VR", reason(instance_, w).c_str());
                        lost_ = true;
                    }
                }
            }
            if (!acquired_) {   // (this frame to the mirror, nothing submitted)
                toHeadset_ = fs.shouldRender = false;
                for (int i = 0; i < 2; ++i) fs.eye[i].width = fs.eye[i].height = kMirrorSize;
            }
        }
        // the game's colours are display-referred (sRGB-encoded already): written to the SRGB8_ALPHA8 image unconverted
        // (GL_FRAMEBUFFER_SRGB off), the runtime reads them as sRGB = the flat game's colours. OpenGL ES: only with
        // GL_EXT_sRGB_write_control (else ES always encodes into an sRGB image: srgbControl_ false, logged at init)
        if (srgbControl_) gl::Disable(kFramebufferSrgb);
        return fs.shouldRender;
    }

    unsigned eyeFramebuffer(int i) override
    {
        if (!toHeadset_) return mirrorFbo_;   // (both eyes the same target: the right eye drawn last is overdrawn by the left)
        const Chain& c = chains_[size_t(i)];
        return c.images[c.current].fbo;
    }
    unsigned eyeTexture(int i) override
    {
        if (!toHeadset_) return mirrorTex_;
        const Chain& c = chains_[size_t(i)];
        return c.images[c.current].tex;
    }

    void endFrame(const FrameState&) override
    {
        // the left eye (drawn last) kept for the window's mirror before the images go back (quest: no window: none)
#if defined(__ANDROID__)
        if (false) {
#else
        if (toHeadset_ && blit_) {
#endif
            const Chain& c = chains_[0];
            gl::BindFramebuffer(kReadFramebuffer, c.images[c.current].fbo);
            gl::BindFramebuffer(kDrawFramebuffer, mirrorFbo_);
            const int w = std::min<int>(c.w, kMirrorSize), h = std::min<int>(c.h, kMirrorSize);
            blit_(0, 0, c.w, c.h, 0, 0, w, h, gl::COLOR_BUFFER_BIT, gl::LINEAR);
            mirrorW_ = w, mirrorH_ = h;
        } else {
            mirrorW_ = mirrorH_ = kMirrorSize;
        }
        for (Chain& ch : chains_) {
            if (!ch.held) continue;
            XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            xrReleaseSwapchainImage(ch.handle, &ri);
            ch.held = false;
        }
        if (!frameBegun_) return;   // (every xrBeginFrame gets its xrEndFrame; none without one)
        XrCompositionLayerProjectionView pv[2];
        for (int i = 0; i < 2; ++i) {
            pv[i] = XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
            pv[i].pose = views_[i].pose;   // the poses the scene drew with (beginFrame's)
            pv[i].fov = views_[i].fov;
            pv[i].subImage.swapchain = chains_[size_t(i)].handle;
            pv[i].subImage.imageRect.extent = {chains_[size_t(i)].w, chains_[size_t(i)].h};
        }
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer.space = space_;
        layer.viewCount = 2;
        layer.views = pv;
        const XrCompositionLayerBaseHeader* layers[] = {reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
        XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
        ei.displayTime = frameState_.predictedDisplayTime;
        ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        ei.layerCount = toHeadset_ && acquired_ ? 1 : 0;   // (not rendered: an empty frame, as the spec asks)
        ei.layers = layers;
        const XrResult r = xrEndFrame(session_, &ei);
        if (XR_FAILED(r) && !endFailLogged_) {
            endFailLogged_ = true;
            SDL_Log("openxr: xrEndFrame: %s", reason(instance_, r).c_str());
        }
        frameBegun_ = false;
    }

    void present(unsigned fbo, int width, int height) override
    {
        // the desktop mirror: the left eye, its shape kept, centred on black
        gl::BindFramebuffer(gl::FRAMEBUFFER, fbo);
        gl::Viewport(0, 0, width, height);
        gl::ClearColor(0, 0, 0, 1);
        gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
        if (!blit_ || !mirrorFbo_ || width <= 0 || height <= 0) return;
        const float s = std::min(float(width) / float(mirrorW_), float(height) / float(mirrorH_));
        const int w = int(float(mirrorW_) * s), h = int(float(mirrorH_) * s), x = (width - w) / 2, y = (height - h) / 2;
        gl::BindFramebuffer(kReadFramebuffer, mirrorFbo_);
        gl::BindFramebuffer(kDrawFramebuffer, fbo);
        blit_(0, 0, mirrorW_, mirrorH_, x, y, x + w, y + h, gl::COLOR_BUFFER_BIT, gl::LINEAR);
        gl::BindFramebuffer(gl::FRAMEBUFFER, fbo);
    }

    void haptic(int hand, float amplitude, float seconds) override
    {
        if (!running_ || state_ != XR_SESSION_STATE_FOCUSED || !haptic_ || hand < 0 || hand > 1) return;
        XrHapticVibration v{XR_TYPE_HAPTIC_VIBRATION};
        v.amplitude = std::clamp(amplitude, 0.0f, 1.0f);
        v.duration = XrDuration(double(seconds) * 1e9);
        v.frequency = XR_FREQUENCY_UNSPECIFIED;
        XrHapticActionInfo hi{XR_TYPE_HAPTIC_ACTION_INFO};
        hi.action = haptic_;
        hi.subactionPath = hand_[hand];
        xrApplyHapticFeedback(session_, &hi, reinterpret_cast<const XrHapticBaseHeader*>(&v));
    }

private:
    struct Image { gl::GLuint tex = 0, fbo = 0, depth = 0; };
    struct Chain {
        XrSwapchain handle = XR_NULL_HANDLE;
        int32_t w = 0, h = 0;
        std::vector<Image> images;
        uint32_t current = 0;
        bool held = false;   // an image acquired and not released yet
    };
    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId system_ = XR_NULL_SYSTEM_ID;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace space_ = XR_NULL_HANDLE;
    XrSessionState state_ = XR_SESSION_STATE_UNKNOWN;
    bool running_ = false, lost_ = false;
    bool frameBegun_ = false, toHeadset_ = false, acquired_ = false, haveViews_ = false, endFailLogged_ = false;
    XrFrameState frameState_{XR_TYPE_FRAME_STATE};
    XrView views_[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    std::vector<Chain> chains_;
    int64_t format_ = 0;
    XrActionSet actions_ = XR_NULL_HANDLE;
    XrAction trigger_ = XR_NULL_HANDLE, squeeze_ = XR_NULL_HANDLE, gripPose_ = XR_NULL_HANDLE, aimPose_ = XR_NULL_HANDLE;
    XrAction primary_ = XR_NULL_HANDLE, secondary_ = XR_NULL_HANDLE, menu_ = XR_NULL_HANDLE, haptic_ = XR_NULL_HANDLE;
    XrPath hand_[2] = {};
    XrSpace gripSpace_[2] = {}, aimSpace_[2] = {};
    Controller hands_[2];
    gl::GLuint mirrorFbo_ = 0, mirrorTex_ = 0, mirrorDepth_ = 0;
    int mirrorW_ = kMirrorSize, mirrorH_ = kMirrorSize;
    PFN_Blit blit_ = nullptr;
    bool srgbWanted_ = true, srgbControl_ = true;
    float resolutionScale_ = 1.0f;
#if defined(_WIN32)
    HDC hdc_ = nullptr;
    HGLRC hglrc_ = nullptr;
#endif
#if defined(__ANDROID__)
    JavaVM* vm_ = nullptr;
    jobject activity_ = nullptr;   // (a global reference: the instance keeps it)
    // quest: xrInitializeLoaderKHR once per process with the Java VM and the activity (SDL's)
    bool initAndroid(std::string& err)
    {
        auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        jobject act = static_cast<jobject>(SDL_GetAndroidActivity());
        if (!env || !act || env->GetJavaVM(&vm_) != JNI_OK) {
            err = "openxr: no Android activity / Java VM";
            return false;
        }
        if (!activity_) activity_ = env->NewGlobalRef(act);
        env->DeleteLocalRef(act);
        static bool loaderReady = false;
        if (loaderReady) return true;
        PFN_xrInitializeLoaderKHR initLoader = nullptr;
        if (XR_FAILED(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                                            reinterpret_cast<PFN_xrVoidFunction*>(&initLoader))) || !initLoader) {
            err = "openxr: the loader has no xrInitializeLoaderKHR";
            return false;
        }
        XrLoaderInitInfoAndroidKHR li{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        li.applicationVM = vm_;
        li.applicationContext = activity_;
        if (const XrResult r = initLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&li)); XR_FAILED(r))
            return fail(err, "xrInitializeLoaderKHR", r);
        loaderReady = true;
        return true;
    }
#endif

    bool fail(std::string& err, const char* what, XrResult r)
    {
        err = std::string("openxr: ") + reason(instance_, r) + " [" + what + "]";
        shutdown();   // (what was made so far; the game goes on flat)
        return false;
    }
    static void toPose(const XrPosef& p, Pose& o, bool valid)
    {
        o.pos[0] = p.position.x, o.pos[1] = p.position.y, o.pos[2] = p.position.z;
        o.rot[0] = p.orientation.x, o.rot[1] = p.orientation.y, o.rot[2] = p.orientation.z, o.rot[3] = p.orientation.w;
        o.valid = valid;
    }
    bool locate(XrSpace s, Pose& o)
    {
        XrSpaceLocation l{XR_TYPE_SPACE_LOCATION};
        if (!s || XR_FAILED(xrLocateSpace(s, space_, frameState_.predictedDisplayTime, &l))) return false;
        const bool v = (l.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && (l.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
        toPose(l.pose, o, v);
        return v;
    }
    float getFloat(XrAction a, XrPath sub)
    {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = a, gi.subactionPath = sub;
        XrActionStateFloat st{XR_TYPE_ACTION_STATE_FLOAT};
        return XR_SUCCEEDED(xrGetActionStateFloat(session_, &gi, &st)) && st.isActive ? st.currentState : 0.0f;
    }
    bool getBool(XrAction a, XrPath sub)
    {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = a, gi.subactionPath = sub;
        XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
        return XR_SUCCEEDED(xrGetActionStateBoolean(session_, &gi, &st)) && st.isActive && st.currentState;
    }
    void pollEvents()
    {
        if (!instance_) return;
        XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
        while (xrPollEvent(instance_, &ev) == XR_SUCCESS) {
            if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                state_ = reinterpret_cast<const XrEventDataSessionStateChanged&>(ev).state;
                SDL_Log("openxr: session state %d", int(state_));
                if (state_ == XR_SESSION_STATE_READY) {
                    XrSessionBeginInfo b{XR_TYPE_SESSION_BEGIN_INFO};
                    b.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    running_ = XR_SUCCEEDED(xrBeginSession(session_, &b));
                } else if (state_ == XR_SESSION_STATE_STOPPING) {
                    xrEndSession(session_);   // (the headset taken off / the app hidden: back to IDLE, READY again later)
                    running_ = false;
                } else if (state_ == XR_SESSION_STATE_EXITING || state_ == XR_SESSION_STATE_LOSS_PENDING) {
                    lost_ = true;   // the runtime quits us / the headset is gone: the game drops VR (main.cpp vrFrame)
                }
            } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
                lost_ = true;
            }
            ev = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
        }
    }
    bool makeActions(std::string& err)
    {
        XrActionSetCreateInfo ai{XR_TYPE_ACTION_SET_CREATE_INFO};
        std::strcpy(ai.actionSetName, "gameplay");
        std::strcpy(ai.localizedActionSetName, "Gameplay");
        if (XrResult r = xrCreateActionSet(instance_, &ai, &actions_); XR_FAILED(r)) return fail(err, "xrCreateActionSet", r);
        xrStringToPath(instance_, "/user/hand/left", &hand_[0]);
        xrStringToPath(instance_, "/user/hand/right", &hand_[1]);
        auto make = [&](XrAction& a, XrActionType t, const char* id, const char* label) {
            XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
            ci.actionType = t;
            std::strcpy(ci.actionName, id);
            std::strcpy(ci.localizedActionName, label);
            ci.countSubactionPaths = 2;
            ci.subactionPaths = hand_;
            return XR_SUCCEEDED(xrCreateAction(actions_, &ci, &a));
        };
        if (!make(trigger_, XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Shoot") ||
            !make(squeeze_, XR_ACTION_TYPE_FLOAT_INPUT, "squeeze", "Reload (grip)") ||
            !make(gripPose_, XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Hand pose") ||
            !make(aimPose_, XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Gun pose") ||
            !make(primary_, XR_ACTION_TYPE_BOOLEAN_INPUT, "primary", "Start (left) / reload (right)") ||
            !make(secondary_, XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary", "Reload") ||
            !make(menu_, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Pause") ||
            !make(haptic_, XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Recoil")) {
            return fail(err, "xrCreateAction", XR_ERROR_VALIDATION_FAILURE);
        }
        // suggested bindings per interaction profile (the runtime remaps other controllers to one of these). A path the
        // profile lacks fails its whole list: the lists are the spec's (OpenXR 1.0 section 6.4)
        using L = std::vector<std::pair<XrAction, std::string>>;
        auto bind = [&](const char* profile, const L& list) {
            std::vector<XrActionSuggestedBinding> b;
            for (const auto& [a, p] : list) {
                XrPath path;
                if (XR_SUCCEEDED(xrStringToPath(instance_, p.c_str(), &path))) b.push_back({a, path});
            }
            XrPath prof;
            xrStringToPath(instance_, profile, &prof);
            XrInteractionProfileSuggestedBinding s{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            s.interactionProfile = prof;
            s.countSuggestedBindings = uint32_t(b.size());
            s.suggestedBindings = b.data();
            if (const XrResult r = xrSuggestInteractionProfileBindings(instance_, &s); XR_FAILED(r))
                SDL_Log("openxr: bindings for %s: %s", profile, reason(instance_, r).c_str());
        };
        // every profile: both hands' grip / aim poses and haptics
        auto common = [&](L l) {
            for (const std::string p : {"/user/hand/left", "/user/hand/right"}) {
                l.push_back({gripPose_, p + "/input/grip/pose"});
                l.push_back({aimPose_, p + "/input/aim/pose"});
                l.push_back({haptic_, p + "/output/haptic"});
            }
            return l;
        };
        bind("/interaction_profiles/khr/simple_controller",
             common({{trigger_, "/user/hand/left/input/select/click"}, {trigger_, "/user/hand/right/input/select/click"},
                     {menu_, "/user/hand/left/input/menu/click"}, {menu_, "/user/hand/right/input/menu/click"}}));
        bind("/interaction_profiles/oculus/touch_controller",   // (Quest, Rift S; the right menu is the system's)
             common({{trigger_, "/user/hand/left/input/trigger/value"}, {trigger_, "/user/hand/right/input/trigger/value"},
                     {squeeze_, "/user/hand/left/input/squeeze/value"}, {squeeze_, "/user/hand/right/input/squeeze/value"},
                     {primary_, "/user/hand/left/input/x/click"}, {primary_, "/user/hand/right/input/a/click"},
                     {secondary_, "/user/hand/left/input/y/click"}, {secondary_, "/user/hand/right/input/b/click"},
                     {menu_, "/user/hand/left/input/menu/click"}}));
        bind("/interaction_profiles/valve/index_controller",   // (no menu button: the left B)
             common({{trigger_, "/user/hand/left/input/trigger/value"}, {trigger_, "/user/hand/right/input/trigger/value"},
                     {squeeze_, "/user/hand/left/input/squeeze/value"}, {squeeze_, "/user/hand/right/input/squeeze/value"},
                     {primary_, "/user/hand/left/input/a/click"}, {primary_, "/user/hand/right/input/a/click"},
                     {secondary_, "/user/hand/right/input/b/click"}, {menu_, "/user/hand/left/input/b/click"}}));
        bind("/interaction_profiles/htc/vive_controller",
             common({{trigger_, "/user/hand/left/input/trigger/value"}, {trigger_, "/user/hand/right/input/trigger/value"},
                     {squeeze_, "/user/hand/left/input/squeeze/click"}, {squeeze_, "/user/hand/right/input/squeeze/click"},
                     {primary_, "/user/hand/left/input/trackpad/click"}, {primary_, "/user/hand/right/input/trackpad/click"},
                     {menu_, "/user/hand/left/input/menu/click"}, {menu_, "/user/hand/right/input/menu/click"}}));
        bind("/interaction_profiles/microsoft/motion_controller",   // (Windows Mixed Reality)
             common({{trigger_, "/user/hand/left/input/trigger/value"}, {trigger_, "/user/hand/right/input/trigger/value"},
                     {squeeze_, "/user/hand/left/input/squeeze/click"}, {squeeze_, "/user/hand/right/input/squeeze/click"},
                     {primary_, "/user/hand/left/input/trackpad/click"}, {primary_, "/user/hand/right/input/trackpad/click"},
                     {menu_, "/user/hand/left/input/menu/click"}, {menu_, "/user/hand/right/input/menu/click"}}));
        for (int h = 0; h < 2; ++h) {
            XrActionSpaceCreateInfo sc{XR_TYPE_ACTION_SPACE_CREATE_INFO};
            sc.poseInActionSpace.orientation.w = 1;
            sc.subactionPath = hand_[h];
            sc.action = gripPose_;
            xrCreateActionSpace(session_, &sc, &gripSpace_[h]);
            sc.action = aimPose_;
            xrCreateActionSpace(session_, &sc, &aimSpace_[h]);
        }
        XrSessionActionSetsAttachInfo at{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        at.countActionSets = 1;
        at.actionSets = &actions_;
        if (XrResult r = xrAttachSessionActionSets(session_, &at); XR_FAILED(r)) return fail(err, "xrAttachSessionActionSets", r);
        return true;
    }
    bool makeSwapchains(std::string& err)
    {
        uint32_t n = 0;
        xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &n, nullptr);
        std::vector<XrViewConfigurationView> vc(n, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
        xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, n, &n, vc.data());
        if (n < 2) return fail(err, "no stereo view configuration", XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED);
        uint32_t fc = 0;
        xrEnumerateSwapchainFormats(session_, 0, &fc, nullptr);
        std::vector<int64_t> formats(fc);
        xrEnumerateSwapchainFormats(session_, fc, &fc, formats.data());
        // the game writes sRGB-encoded colours: SRGB8_ALPHA8 (stored as written, shown as sRGB: the flat game's look).
        // RGBA8 only when the runtime lacks it: the runtime then takes the values as linear (a paler picture; logged)
        // OpenGL ES (quest): an sRGB image is always encoded on write unless GL_EXT_sRGB_write_control turns that off;
        // without it the sRGB image would come out pale: RGBA8 then. vr.ini srgb_swapchain = off forces RGBA8 (a
        // headset-side check: the CHECK of docs/quest.md's first test)
        srgbControl_ = true;
#if defined(__ANDROID__)
        {
            const char* glExt = reinterpret_cast<const char*>(gl::GetString(0x1F03 /*EXTENSIONS*/));
            srgbControl_ = glExt && std::strstr(glExt, "GL_EXT_sRGB_write_control") != nullptr;
            SDL_Log("openxr: OpenGL ES sRGB write control %s", srgbControl_ ? "there" : "missing (RGBA8 swapchain)");
        }
#endif
        format_ = 0;
        if (srgbWanted_ && srgbControl_)
            for (int64_t f : formats) if (f == kSRGB8A8) format_ = f;
        if (!format_) for (int64_t f : formats) if (f == kRGBA8) format_ = f;
        if (!format_) for (int64_t f : formats) if (f == kSRGB8A8) format_ = f;   // (RGBA8 missing: sRGB after all)
        if (!format_) return fail(err, "no 8-bit RGBA swapchain format", XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED);
        if (format_ != kSRGB8A8) SDL_Log("openxr: RGBA8 swapchain (%s; colours may look pale)", srgbWanted_ ? "no sRGB format" : "vr.ini srgb_swapchain off");
        chains_.resize(2);
        for (int i = 0; i < 2; ++i) {
            Chain& ch = chains_[size_t(i)];
            // (vr.ini resolution_scale: the recommended size x it, inside the runtime's maximum; 1: unchanged)
            const auto scaled = [&](uint32_t rec, uint32_t max) {
                return int32_t(std::clamp<uint32_t>(uint32_t(std::lround(float(rec) * resolutionScale_)), 16u, std::max(max, 16u)));
            };
            ch.w = resolutionScale_ == 1.0f ? int32_t(vc[size_t(i)].recommendedImageRectWidth)
                                             : scaled(vc[size_t(i)].recommendedImageRectWidth, vc[size_t(i)].maxImageRectWidth);
            ch.h = resolutionScale_ == 1.0f ? int32_t(vc[size_t(i)].recommendedImageRectHeight)
                                             : scaled(vc[size_t(i)].recommendedImageRectHeight, vc[size_t(i)].maxImageRectHeight);
            XrSwapchainCreateInfo sci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
            sci.format = format_;
            sci.sampleCount = 1;
            sci.width = uint32_t(ch.w), sci.height = uint32_t(ch.h);
            sci.faceCount = 1, sci.arraySize = 1, sci.mipCount = 1;
            if (XrResult r = xrCreateSwapchain(session_, &sci, &ch.handle); XR_FAILED(r)) return fail(err, "xrCreateSwapchain", r);
            uint32_t ic = 0;
            xrEnumerateSwapchainImages(ch.handle, 0, &ic, nullptr);
            std::vector<SwapchainImage> imgs(ic, SwapchainImage{kSwapchainImageType});
            xrEnumerateSwapchainImages(ch.handle, ic, &ic, reinterpret_cast<XrSwapchainImageBaseHeader*>(imgs.data()));
            ch.images.resize(ic);
            for (uint32_t k = 0; k < ic; ++k) {   // an FBO + a depth renderbuffer around each image
                Image& im = ch.images[k];
                im.tex = imgs[k].image;
                gl::GenFramebuffers(1, &im.fbo);
                gl::BindFramebuffer(gl::FRAMEBUFFER, im.fbo);
                gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, im.tex, 0);
                gl::GenRenderbuffers(1, &im.depth);
                gl::BindRenderbuffer(gl::RENDERBUFFER, im.depth);
                gl::RenderbufferStorage(gl::RENDERBUFFER, gl::DEPTH_COMPONENT24, ch.w, ch.h);
                gl::FramebufferRenderbuffer(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::RENDERBUFFER, im.depth);
                if (!gl::framebufferOk("openxr eye")) return fail(err, "eye framebuffer", XR_ERROR_RUNTIME_FAILURE);
            }
        }
        gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
        return true;
    }
    // the backend's own eye target: the frames the headset does not show (before READY, hidden, a skipped frame) and
    // the window's mirror copy
    void makeMirror()
    {
        gl::GenFramebuffers(1, &mirrorFbo_);
        gl::BindFramebuffer(gl::FRAMEBUFFER, mirrorFbo_);
        gl::GenTextures(1, &mirrorTex_);
        gl::BindTexture(gl::TEXTURE_2D, mirrorTex_);
        gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, kMirrorSize, kMirrorSize, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
        gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, mirrorTex_, 0);
        gl::GenRenderbuffers(1, &mirrorDepth_);
        gl::BindRenderbuffer(gl::RENDERBUFFER, mirrorDepth_);
        gl::RenderbufferStorage(gl::RENDERBUFFER, gl::DEPTH_COMPONENT24, kMirrorSize, kMirrorSize);
        gl::FramebufferRenderbuffer(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::RENDERBUFFER, mirrorDepth_);
        gl::framebufferOk("openxr mirror");
        gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    }
};

} // namespace

std::unique_ptr<Backend> makeOpenXr() { return std::make_unique<OpenXr>(); }

} // namespace hotd::vr

#endif // HOTD_OPENXR
