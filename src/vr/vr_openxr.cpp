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
#include "vr/vr_aim.h"
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
#include <dirent.h>   // (questperf: the threads' CPU from /proc/self/task)
#include <jni.h>
#include <unistd.h>
#include <map>
#include <string>
#else
#define XR_USE_PLATFORM_XLIB   // CHECK (Linux): the GLX binding needs the X display / drawable / context
#include <GL/glx.h>
#define XR_USE_GRAPHICS_API_OPENGL
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    case XR_ERROR_GRAPHICS_DEVICE_INVALID:   // (vrship: the usual cause on a laptop: the game on the integrated GPU)
        return "the game's graphics card / context is not one the runtime can use (a laptop: give hotd.exe the dedicated "
               "graphics card in Windows Settings > System > Display > Graphics)";
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
    bool exitAsked() const override { return exitAsked_; }
    bool focused() const override { return state_ == XR_SESSION_STATE_FOCUSED; }   // (questsim: main.cpp's pause)
    bool pacesFrames() const override { return true; }
    std::string interactionProfile() const override { return !profile_[1].empty() ? profile_[1] : profile_[0]; }   // (vrprompts)
#if defined(__ANDROID__)
    bool perfAroundOff() const override { return aroundOff_; }
#endif
    // vrnull (testing): --vr-head yaw,pitch,roll turns the seat at the first frame (as HOTD_XR_HEAD): a fixed virtual
    // headset (SteamVR's null driver) looks around
    void scriptHead(const float ypr[3], const float[3]) override
    {
        std::copy(ypr, ypr + 3, headYpr_);
        headSet_ = true;
    }

    bool init(const Settings& settings, std::string& err) override
    {
        srgbWanted_ = settings.srgbSwapchain;
        resolutionScale_ = settings.resolutionScale;
#if defined(__ANDROID__)
        foveation_ = settings.foveation, fovDynamic_ = settings.foveationDynamic;
        perfLog_ = settings.perfLog, perfCycle_ = settings.perfCycle;
        dynRes_ = settings.dynamicResolution && !perfCycle_;
#endif
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
        auto hasExt = [&](const char* name) {
            return std::any_of(exts.begin(), exts.end(), [&](const XrExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
        };
        {   // vrship (2026-11-09): the runtime's extensions in the log (each runtime differs: Meta, SteamVR, WMR)
            std::string l;
            for (const auto& e : exts) l += std::string(l.empty() ? "" : " ") + e.extensionName;
            SDL_Log("openxr: extensions: %s", l.c_str());
        }
        if (!hasExt(kGlExtension))
            return fail(err, "extensions", XR_ERROR_EXTENSION_NOT_PRESENT);
        // vrship: optional extensions, enabled only when the runtime has them: the HP Reverb G2's controllers (SteamVR,
        // Windows Mixed Reality; without it the runtime maps them onto another profile)
        hpControllers_ = hasExt(XR_EXT_HP_MIXED_REALITY_CONTROLLER_EXTENSION_NAME);
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
        // vr4 (docs/quest.md "Refresh rate"): the optional ones, enabled when the runtime has them: the display's rate
        // (XR_FB_display_refresh_rate: the Quest's 72 / 90 / 120 Hz) and the performance levels
        // (XR_EXT_performance_settings: sustained high)
        auto has = [&](const char* name) {
            return std::any_of(exts.begin(), exts.end(), [name](const XrExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
        };
        hasRefresh_ = has(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
        hasPerf_ = has(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
        std::vector<const char*> ext = {kGlExtension};
#if defined(__ANDROID__)
        ext.push_back(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
        XrInstanceCreateInfoAndroidKHR android{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
        android.applicationVM = vm_;
        android.applicationActivity = activity_;
#endif
        if (hpControllers_) ext.push_back(XR_EXT_HP_MIXED_REALITY_CONTROLLER_EXTENSION_NAME);
        if (hasRefresh_) ext.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
        if (hasPerf_) ext.push_back(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
#if defined(__ANDROID__)
        // questperf (2026-11-10): fixed foveated rendering (XR_FB_foveation + its configuration, set on the swapchains
        // with XR_FB_swapchain_update_state) and Meta's frame counters (XR_META_performance_metrics: the perf log)
        hasFov_ = foveation_ > 0 || perfCycle_;
        for (const char* e : {XR_FB_FOVEATION_EXTENSION_NAME, XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME,
                              XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME})
            hasFov_ = hasFov_ && has(e);
        if (hasFov_)
            for (const char* e : {XR_FB_FOVEATION_EXTENSION_NAME, XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME,
                                  XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME})
                ext.push_back(e);
        hasMetrics_ = (perfLog_ || dynRes_) && has(XR_META_PERFORMANCE_METRICS_EXTENSION_NAME);
        if (hasMetrics_) ext.push_back(XR_META_PERFORMANCE_METRICS_EXTENSION_NAME);
#endif
        refreshWant_ = settings.refreshRate;
        perfWanted_ = settings.sustainedPerformance;
        autoRecentre_ = settings.autoRecentre;
        SDL_Log("openxr: display refresh rate extension %s, performance settings %s", hasRefresh_ ? "there" : "missing",
                hasPerf_ ? "there" : "missing");
        XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
#if defined(__ANDROID__)
        ici.next = &android;
#endif
        std::strncpy(ici.applicationInfo.applicationName, "HOTD ReWrithe", XR_MAX_APPLICATION_NAME_SIZE - 1);
        ici.applicationInfo.applicationVersion = 1;
        std::strncpy(ici.applicationInfo.engineName, "hotd", XR_MAX_ENGINE_NAME_SIZE - 1);
        ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;   // (all this needs; 1.1 runtimes take 1.0 applications)
        ici.enabledExtensionCount = uint32_t(ext.size());
        ici.enabledExtensionNames = ext.data();
        if (r = xrCreateInstance(&ici, &instance_); XR_FAILED(r)) return fail(err, "xrCreateInstance", r);
        XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
        if (XR_SUCCEEDED(xrGetInstanceProperties(instance_, &ip)))
            SDL_Log("openxr: runtime %s %u.%u.%u", ip.runtimeName, unsigned(XR_VERSION_MAJOR(ip.runtimeVersion)),
                    unsigned(XR_VERSION_MINOR(ip.runtimeVersion)), unsigned(XR_VERSION_PATCH(ip.runtimeVersion)));
        XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
        sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        if (r = xrGetSystem(instance_, &sgi, &system_); XR_FAILED(r)) return fail(err, "xrGetSystem", r);
        XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
        if (XR_SUCCEEDED(xrGetSystemProperties(instance_, system_, &sp))) {
            SDL_Log("openxr: headset %s", sp.systemName);
            // vrnull (2026-11-09): SteamVR's null driver (a virtual headset, "SteamVR/OpenXR : null") reports its fixed
            // head rolled 180 degrees (orientation 0 0 -1 0): drawn as told, the world came out upside down in the
            // runtime's view and the mirror; its fixed pose is levelled at the first frame (seatFix_)
            nullHmd_ = std::strstr(sp.systemName, ": null") != nullptr;
        }
        // (testing: HOTD_XR_HEAD=yaw,pitch,roll degrees turns the seat so the head looks that way: a fixed virtual
        // headset looks around; +yaw left, +pitch up, as the preview's --vr-head)
        if (const char* h = std::getenv("HOTD_XR_HEAD"))
            headSet_ = std::sscanf(h, "%f,%f,%f", &headYpr_[0], &headYpr_[1], &headYpr_[2]) >= 1;
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
        // vr2 (2026-11-09): the recentre's spaces: the runtime's LOCAL itself and the head (VIEW); space_ becomes LOCAL
        // moved to the head's place and heading when a thumbstick is held (recentre)
        xrCreateReferenceSpace(session_, &rs, &baseSpace_);
        rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        xrCreateReferenceSpace(session_, &rs, &viewSpace_);
        if (!makeActions(err)) return false;
        if (!makeSwapchains(err)) return false;
#if defined(__ANDROID__)
        applyFoveation(foveation_);   // questperf
#endif
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
        if (baseSpace_) xrDestroySpace(baseSpace_);
        if (viewSpace_) xrDestroySpace(viewSpace_);
        baseSpace_ = viewSpace_ = XR_NULL_HANDLE;
        if (session_) xrDestroySession(session_);
        if (instance_) xrDestroyInstance(instance_);
        instance_ = XR_NULL_HANDLE, session_ = XR_NULL_HANDLE, space_ = XR_NULL_HANDLE, actions_ = XR_NULL_HANDLE;
    }

    bool beginFrame(FrameState& fs) override
    {
        pollEvents();
        fs = FrameState{};
        frameBegun_ = acquired_ = false;
        bool waited = false;
        if (running_ && !lost_) {
            XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
            frameState_ = XrFrameState{XR_TYPE_FRAME_STATE};
            const uint64_t w0 = SDL_GetTicksNS();
            if (ok(xrWaitFrame(session_, &wi, &frameState_), "xrWaitFrame")) {
                waited = true;
                XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
                frameBegun_ = ok(xrBeginFrame(session_, &bi), "xrBeginFrame");   // (FRAME_DISCARDED is a success code)
            }
            // vrnull (2026-11-09): SteamVR's compositor gone (it crashed / was closed) leaves the session VISIBLE and
            // every xrWaitFrame blocking ~200 ms with no error: the game ran at 5 frames / s. While shown (VISIBLE /
            // FOCUSED: a runtime may throttle the others), 30 waits in a row over 100 ms (~3 s) drop VR (flat play)
            const uint64_t waitNs = SDL_GetTicksNS() - w0;
            const bool shown = state_ == XR_SESSION_STATE_VISIBLE || state_ == XR_SESSION_STATE_FOCUSED;
            slowWaits_ = shown && waitNs > 100000000 ? slowWaits_ + 1 : 0;
            if (slowWaits_ >= 30 && !lost_) {
                SDL_Log("openxr: the runtime stopped answering (xrWaitFrame %.0f ms a frame, %d frames): leaving VR",
                        double(waitNs) / 1e6, slowWaits_);
                lost_ = true;
            }
#if defined(__ANDROID__)
            if (frameBegun_) perfFrame(waitNs, shown);   // questperf: the frame's timing (the perf log, perf_cycle)
#endif
        }
        // vr2: no xrWaitFrame (the headset idle / off, not READY yet): our own ~90 Hz wait, as main.cpp's frame limiter
        // is off while this backend paces the loop (pacesFrames)
        if (!waited) {
            constexpr uint64_t kIdleNs = 11111111;
            const uint64_t now = SDL_GetTicksNS();
            if (idleLast_ && now > idleLast_ && now - idleLast_ < kIdleNs) SDL_DelayPrecise(kIdleNs - (now - idleLast_));
            idleLast_ = SDL_GetTicksNS();
        }
        // vrnull: the null driver's fixed head levelled (and / or HOTD_XR_HEAD's look) once, at the first frame
        if (frameBegun_ && !seatFixDone_ && (nullHmd_ || headSet_)) {
            XrSpaceLocation l{XR_TYPE_SPACE_LOCATION};
            const bool have = XR_SUCCEEDED(xrLocateSpace(viewSpace_, baseSpace_, frameState_.predictedDisplayTime, &l)) &&
                              (l.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
            if (have || !nullHmd_) {
                seatFixDone_ = true;
                // the seat's orientation S: the views in it are S^-1 H; wanted: the look L (identity without
                // HOTD_XR_HEAD), so S = H L^-1 (the null driver's H; a real headset's own H is left to it: S = L^-1)
                float look[4], inv[4], head[4] = {0, 0, 0, 1};
                quatFromYawPitchRoll(headYpr_[0] * 0.01745329f, headYpr_[1] * 0.01745329f, headYpr_[2] * 0.01745329f, look);
                inv[0] = -look[0], inv[1] = -look[1], inv[2] = -look[2], inv[3] = look[3];
                if (nullHmd_) {
                    const XrQuaternionf& q = l.pose.orientation;
                    head[0] = q.x, head[1] = q.y, head[2] = q.z, head[3] = q.w;
                }
                quatMul(head, inv, seatFix_);
                XrPosef identity{};
                identity.orientation.w = 1;
                setPlaySpace(identity);
                SDL_Log("openxr: the seat turned for %s (orientation %.3f %.3f %.3f %.3f)",
                        nullHmd_ ? "SteamVR's null driver (its fixed head levelled)" : "HOTD_XR_HEAD", double(seatFix_[0]),
                        double(seatFix_[1]), double(seatFix_[2]), double(seatFix_[3]));
            }
        }
        // vr2: a thumbstick held down 1 s (either hand; last frame's buttons) recentres: the seat's origin and heading
        // to the head's now (before the eyes are located, so this frame's views and its layer share the space)
        // vr4: the headset back after more than auto_recentre seconds away (taken off: the session stopped / not
        // visible; the runtime's dashboard over the game keeps it VISIBLE: not away): recentred by itself
        {
            const bool shown = running_ && (state_ == XR_SESSION_STATE_VISIBLE || state_ == XR_SESSION_STATE_FOCUSED);
            if (away_.update(shown, double(SDL_GetTicksNS()) / 1e9, double(autoRecentre_))) autoRecentrePending_ = true;
        }
        if (frameBegun_ && autoRecentrePending_) {
            autoRecentrePending_ = false;
            SDL_Log("openxr: the headset is back after more than %.0f s away: recentring", double(autoRecentre_));
            recentre(frameState_.predictedDisplayTime, "auto_recentre (the session was not shown, now it is)");
        }
        // vrfix (user's PC VR playthrough 2026-11-10: 18 recentres in 30 min, three within 5 s, every one from the
        // thumbstick, none from auto_recentre): a deliberate hold only: one thumbstick (not both: a two-handed grip)
        // pressed 2 s without a break, the trigger not pulled meanwhile (playing), and 10 s since the last stick recentre
        if (frameBegun_) {
            const XrTime now = frameState_.predictedDisplayTime;
            const int held = int(hands_[0].stickClick) + int(hands_[1].stickClick);
            const bool firing = hands_[0].trigger > 0.4f || hands_[1].trigger > 0.4f;
            if (held != 1 || firing) {
                holdFrom_ = 0, recentred_ = false;
            } else if (!holdFrom_) {
                holdFrom_ = now;
            } else if (!recentred_ && now - holdFrom_ > 2000000000LL &&
                       (!stickRecentreAt_ || now - stickRecentreAt_ > 10000000000LL)) {
                recentred_ = true;
                stickRecentreAt_ = now;
                recentre(now, hands_[0].stickClick ? "the left thumbstick held 2 s" : "the right thumbstick held 2 s");
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
            viewsOk = ok(xrLocateViews(session_, &li, &vs, 2, &n, v), "xrLocateViews") && n == 2 &&
                      (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);
            if (viewsOk && !viewsLogged_) {   // (vrnull 2026-11-09: the runtime's eyes once, for the first-test log)
                viewsLogged_ = true;
                for (int i = 0; i < 2; ++i)
                    SDL_Log("openxr: eye %d: fov left %.1f right %.1f up %.1f down %.1f deg, at %.3f %.3f %.3f m, "
                            "orientation %.3f %.3f %.3f %.3f", i, double(v[i].fov.angleLeft) * 57.29578,
                            double(v[i].fov.angleRight) * 57.29578, double(v[i].fov.angleUp) * 57.29578,
                            double(v[i].fov.angleDown) * 57.29578, double(v[i].pose.position.x),
                            double(v[i].pose.position.y), double(v[i].pose.position.z), double(v[i].pose.orientation.x),
                            double(v[i].pose.orientation.y), double(v[i].pose.orientation.z),
                            double(v[i].pose.orientation.w));
                SDL_Log("openxr: the headset's frame period %.2f ms", double(frameState_.predictedDisplayPeriod) / 1e6);
            }
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
            e.width = toHeadset_ ? eyeW(i) : kMirrorSize;   // (questperf: perf_cycle's smaller part of the image)
            e.height = toHeadset_ ? eyeH(i) : kMirrorSize;
        }
        fs.head = fs.eye[0].pose;
        for (int k = 0; k < 3; ++k) fs.head.pos[k] = 0.5f * (fs.eye[0].pose.pos[k] + fs.eye[1].pose.pos[k]);
        // the controllers (xrSyncActions: only while focused; other states keep the last)
        if (frameBegun_ && state_ == XR_SESSION_STATE_FOCUSED) {
            const XrActiveActionSet active{actions_, XR_NULL_PATH};
            XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
            si.countActiveActionSets = 1;
            si.activeActionSets = &active;
            if (ok(xrSyncActions(session_, &si), "xrSyncActions")) {
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
                    c.stickClick = getBool(recentre_, hand_[h]);
                    getVec2(stick_, hand_[h], c.stick);   // (vrweapon)
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
            // vrnull (2026-11-09): its shape kept (was squeezed into 1024 x 1024: a 1852 x 2056 eye came out wider);
            // vr.ini desktop_view both / --vr-eye: the right eye beside it (the mirror holds two eye slots)
            gl::BindFramebuffer(kDrawFramebuffer, mirrorFbo_);
            for (int i = 0; i < 2; ++i) {
                if (i == 1 && display == 0) break;
                const Chain& c = chains_[size_t(i)];
                const float s = std::min(1.0f, float(kMirrorSize) / float(std::max(c.w, c.h)));
                const int w = std::max(1, int(float(c.w) * s)), h = std::max(1, int(float(c.h) * s));
                gl::BindFramebuffer(kReadFramebuffer, c.images[c.current].fbo);
                blit_(0, 0, c.w, c.h, i * kMirrorSize, 0, i * kMirrorSize + w, h, gl::COLOR_BUFFER_BIT, gl::LINEAR);
                mirrorW_ = w, mirrorH_ = h;
            }
            mirrorBoth_ = display != 0;
        } else {
            mirrorW_ = mirrorH_ = kMirrorSize;
            mirrorBoth_ = false;   // (not shown in the headset: the scene drew one picture into the first slot)
        }
#if defined(__ANDROID__)
        // questperf: the eyes' depth is not needed past the frame: discarded, not written out of the tile memory
        if (toHeadset_ && acquired_ && invalidate_) {
            const gl::GLenum depth = gl::DEPTH_ATTACHMENT;
            for (Chain& ch : chains_) {
                if (!ch.held) continue;
                gl::BindFramebuffer(gl::FRAMEBUFFER, ch.images[ch.current].fbo);
                invalidate_(gl::FRAMEBUFFER, 1, &depth);
            }
            gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
        }
        const uint64_t e0 = SDL_GetTicksNS();
        if (workT0_ && frameBegun_) {
            const uint64_t w = e0 - workT0_;
            ps_.workNs += w, ps_.workMax = std::max(ps_.workMax, w);
            workT0_ = 0;
        }
#endif
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
            pv[i].subImage.imageRect.extent = {eyeW(i), eyeH(i)};   // (questperf: perf_cycle's part of the image)
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
#if defined(__ANDROID__)
        ps_.endNs += SDL_GetTicksNS() - e0;
#endif
        ok(r, "xrEndFrame");   // (vr2: a lost session drops VR)
        if (XR_FAILED(r) && !endFailLogged_) {
            endFailLogged_ = true;
            SDL_Log("openxr: xrEndFrame: %s", reason(instance_, r).c_str());
        }
        frameBegun_ = false;
    }

    void present(unsigned fbo, int width, int height) override
    {
        // the desktop mirror (vr.ini desktop_view, vrnull 2026-11-09): eye = the left eye (--vr-eye right: the right)
        // filling the window, the eye's middle cropped to the window's shape (never stretched); both = the two eyes
        // side by side, each cropped to its half. (Was: the left eye letterboxed, squeezed square)
        gl::BindFramebuffer(gl::FRAMEBUFFER, fbo);
        gl::Viewport(0, 0, width, height);
        gl::ClearColor(0, 0, 0, 1);
        gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
        if (!blit_ || !mirrorFbo_ || width <= 0 || height <= 0) return;
        gl::BindFramebuffer(kReadFramebuffer, mirrorFbo_);
        gl::BindFramebuffer(kDrawFramebuffer, fbo);
        const bool both = display == 2;
        const int n = both ? 2 : 1, dw = width / n;
        for (int k = 0; k < n; ++k) {
            const int slot = mirrorBoth_ ? (both ? k : display == 1 ? 1 : 0) : 0;
            // the largest part of the eye with the target's shape, centred
            const float want = float(dw) / float(height), have = float(mirrorW_) / float(mirrorH_);
            int sw = mirrorW_, sh = mirrorH_;
            if (have > want) sw = std::max(1, int(float(mirrorH_) * want));
            else sh = std::max(1, int(float(mirrorW_) / want));
            const int sx = slot * kMirrorSize + (mirrorW_ - sw) / 2, sy = (mirrorH_ - sh) / 2;
            blit_(sx, sy, sx + sw, sy + sh, k * dw, 0, k * dw + dw, height, gl::COLOR_BUFFER_BIT, gl::LINEAR);
        }
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
    bool exitAsked_ = false;   // vrflow: EXITING / LOSS_PENDING (the runtime asked the app to quit), not a crash
    bool frameBegun_ = false, toHeadset_ = false, acquired_ = false, haveViews_ = false, endFailLogged_ = false;
    bool viewsLogged_ = false, mirrorBoth_ = false;
    int slowWaits_ = 0;   // (vrnull: xrWaitFrame over 100 ms in a row while shown: the runtime gone)
    bool nullHmd_ = false, headSet_ = false, seatFixDone_ = false;   // (vrnull: the seat's fixed turn, setPlaySpace)
    float headYpr_[3] = {0, 0, 0}, seatFix_[4] = {0, 0, 0, 1};
    static void quatMul(const float a[4], const float b[4], float o[4])   // (x y z w: a then b, as vr_math.h's)
    {
        o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
        o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
        o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
        o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    }
    XrFrameState frameState_{XR_TYPE_FRAME_STATE};
    XrView views_[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    std::vector<Chain> chains_;
    int64_t format_ = 0;
    XrActionSet actions_ = XR_NULL_HANDLE;
    XrAction trigger_ = XR_NULL_HANDLE, squeeze_ = XR_NULL_HANDLE, gripPose_ = XR_NULL_HANDLE, aimPose_ = XR_NULL_HANDLE;
    XrAction primary_ = XR_NULL_HANDLE, secondary_ = XR_NULL_HANDLE, menu_ = XR_NULL_HANDLE, haptic_ = XR_NULL_HANDLE;
    XrAction recentre_ = XR_NULL_HANDLE;   // vr2: a thumbstick's click (held 2 s: recentre, vrfix)
    XrAction stick_ = XR_NULL_HANDLE;      // vrweapon: the thumbstick (Vive: the trackpad) as x / y: right stick left / right = weapon
    XrSpace baseSpace_ = XR_NULL_HANDLE, viewSpace_ = XR_NULL_HANDLE;   // vr2: LOCAL unmoved, the head (the recentre)
    XrTime holdFrom_ = 0, stickRecentreAt_ = 0;   // (vrfix: the last stick recentre: 10 s apart)
    bool recentred_ = false, lostLogged_ = false;
    uint64_t idleLast_ = 0;
    XrPath hand_[2] = {};
    std::string profile_[2];   // vrprompts: each hand's interaction profile ("" none)
    XrSpace gripSpace_[2] = {}, aimSpace_[2] = {};
    Controller hands_[2];
    gl::GLuint mirrorFbo_ = 0, mirrorTex_ = 0, mirrorDepth_ = 0;
    int mirrorW_ = kMirrorSize, mirrorH_ = kMirrorSize;
    PFN_Blit blit_ = nullptr;
    bool srgbWanted_ = true, srgbControl_ = true;
    bool hpControllers_ = false;   // vrship: XR_EXT_hp_mixed_reality_controller enabled (its bindings suggested)
    float resolutionScale_ = 1.0f;
    float eyeScale_ = 1.0f;   // questperf: the part of the eye's image drawn (perf_cycle; 1: all of it)
#if defined(__ANDROID__)
    // questperf: foveation (vr.ini foveation, foveation_dynamic), Meta's counters, the per-second perf log and
    // perf_cycle's part now; the eyes' depth discarded at the frame's end (glInvalidateFramebuffer)
    bool hasFov_ = false, fovDynamic_ = true, hasMetrics_ = false, perfLog_ = false, perfCycle_ = false, aroundOff_ = false;
    int foveation_ = 0, fovNow_ = -1, cyclePart_ = -1;
    PFN_xrQueryPerformanceMetricsCounterMETA query_ = nullptr;
    std::vector<std::pair<std::string, XrPath>> metricPaths_;
    struct PerfStats {
        uint64_t t0 = 0, waitNs = 0, waitMax = 0, workNs = 0, workMax = 0, endNs = 0;
        int frames = 0, late = 0, missed = 0;
    } ps_;
    XrTime lastPredicted_ = 0;
    uint64_t workT0_ = 0, cycleT0_ = 0, threadsAt_ = 0;
    std::map<int, uint64_t> threadTicks_;
    // questperf: dynamic resolution (dynamicResolution): its half-second window, the last scale logged
    bool dynRes_ = false;
    XrPath gpuPath_ = XR_NULL_PATH;
    uint64_t dynT0_ = 0;
    int dynFrames_ = 0, dynLate_ = 0, dynFloorLate_ = 0;
    float dynLogged_ = 1.0f;
    using PFN_Invalidate = void(HOTD_APIENTRY*)(gl::GLenum, gl::GLsizei, const gl::GLenum*);
    PFN_Invalidate invalidate_ = reinterpret_cast<PFN_Invalidate>(SDL_GL_GetProcAddress("glInvalidateFramebuffer"));
#endif
    // vr4: the optional extensions (XR_FB_display_refresh_rate, XR_EXT_performance_settings) and their vr.ini values;
    // the auto-recentre (the headset away and back) and the play space's offset now (the heading-only recentre keeps
    // its place)
    bool hasRefresh_ = false, hasPerf_ = false, perfWanted_ = true, autoRecentrePending_ = false;
    int refreshWant_ = 0;
    // questcrash (2026-11-10): the rate asked (0: none) and how many times it was asked again after the runtime moved
    // the display off it (the headset's first log: 90 Hz at READY, then 90 -> 72 Hz once FOCUSED)
    float refreshAsked_ = 0;
    int refreshReasks_ = 0;
    float autoRecentre_ = 3.0f;
    AwayClock away_;
    XrPosef playOffset_{{0, 0, 0, 1}, {0, 0, 0}};
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
    // vr2 (2026-11-09): a frame call's result; the session / instance lost (the runtime crashed or quit, the headset
    // unplugged) drops VR as EXITING does (was: the error ignored, the game drawing for a dead session)
    bool ok(XrResult r, const char* what)
    {
        if (r == XR_ERROR_SESSION_LOST || r == XR_ERROR_INSTANCE_LOST) {
            if (!lostLogged_) SDL_Log("openxr: %s: %s: leaving VR", what, reason(instance_, r).c_str());
            lostLogged_ = lost_ = true;
        }
        return XR_SUCCEEDED(r);
    }
    // vr4 (docs/quest.md "Refresh rate"): after xrBeginSession: the display's rate (vr.ini refresh_rate; the offered
    // ones logged) and the performance levels (sustained high for the CPU and the GPU: the arcade's world drawn twice
    // a frame); both optional, skipped when the runtime lacks them
    void applyDisplaySettings()
    {
        if (hasRefresh_) {
            PFN_xrEnumerateDisplayRefreshRatesFB list = nullptr;
            PFN_xrRequestDisplayRefreshRateFB request = nullptr;
            PFN_xrGetDisplayRefreshRateFB current = nullptr;
            xrGetInstanceProcAddr(instance_, "xrEnumerateDisplayRefreshRatesFB", reinterpret_cast<PFN_xrVoidFunction*>(&list));
            xrGetInstanceProcAddr(instance_, "xrRequestDisplayRefreshRateFB", reinterpret_cast<PFN_xrVoidFunction*>(&request));
            xrGetInstanceProcAddr(instance_, "xrGetDisplayRefreshRateFB", reinterpret_cast<PFN_xrVoidFunction*>(&current));
            uint32_t n = 0;
            std::vector<float> rates;
            if (list && XR_SUCCEEDED(list(session_, 0, &n, nullptr)) && n > 0) {
                rates.resize(n);
                if (XR_FAILED(list(session_, n, &n, rates.data()))) rates.clear();
            }
            std::string offered;
            for (float f : rates) offered += (offered.empty() ? "" : " / ") + std::to_string(int(std::lround(f)));
            float now = 0;
            if (current) current(session_, &now);
#if defined(__ANDROID__)
            const bool standalone = true;
#else
            const bool standalone = false;
#endif
            const float want = chooseRefreshRate(rates.data(), int(rates.size()), refreshWant_, standalone);
            // questcrash: asked even when the display is at it already (a rate left over from before the app: the
            // runtime dropped back to its 72 Hz default once the session was focused, as nobody had asked for 90)
            const bool asked = request && want > 0 && XR_SUCCEEDED(request(session_, want));
            refreshAsked_ = asked ? want : 0;
            SDL_Log("openxr: display %.0f Hz (offered %s; refresh_rate %s)%s", double(now), offered.empty() ? "none" : offered.c_str(),
                    refreshWant_ ? std::to_string(refreshWant_).c_str() : "auto",
                    asked ? (" -> asked " + std::to_string(int(std::lround(want))) + " Hz").c_str() : "");
        }
        if (hasPerf_ && perfWanted_) {
            PFN_xrPerfSettingsSetPerformanceLevelEXT set = nullptr;
            xrGetInstanceProcAddr(instance_, "xrPerfSettingsSetPerformanceLevelEXT", reinterpret_cast<PFN_xrVoidFunction*>(&set));
            const bool cpu = set && XR_SUCCEEDED(set(session_, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT));
            const bool gpu = set && XR_SUCCEEDED(set(session_, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT));
            SDL_Log("openxr: performance level sustained high: CPU %s, GPU %s", cpu ? "set" : "failed", gpu ? "set" : "failed");
        }
#if defined(__ANDROID__)
        startMetrics();   // questperf
#endif
    }
    // questperf: the eye's picture: the whole swapchain image, or perf_cycle's smaller part of it (eyeScale_)
    int32_t eyeW(int i) const { return std::max<int32_t>(16, int32_t(std::lround(float(chains_[size_t(i)].w) * eyeScale_))); }
    int32_t eyeH(int i) const { return std::max<int32_t>(16, int32_t(std::lround(float(chains_[size_t(i)].h) * eyeScale_))); }
#if defined(__ANDROID__)
    // questperf (user 2026-11-10: "extremely clunky on Meta Quest"): fixed foveated rendering on both eyes'
    // swapchains (XR_FB_foveation: a level profile set with xrUpdateSwapchainFB; the profile can go once set)
    void applyFoveation(int level)
    {
        if (!hasFov_ || level == fovNow_ || chains_.empty()) return;
        PFN_xrCreateFoveationProfileFB create = nullptr;
        PFN_xrDestroyFoveationProfileFB destroy = nullptr;
        PFN_xrUpdateSwapchainFB update = nullptr;
        xrGetInstanceProcAddr(instance_, "xrCreateFoveationProfileFB", reinterpret_cast<PFN_xrVoidFunction*>(&create));
        xrGetInstanceProcAddr(instance_, "xrDestroyFoveationProfileFB", reinterpret_cast<PFN_xrVoidFunction*>(&destroy));
        xrGetInstanceProcAddr(instance_, "xrUpdateSwapchainFB", reinterpret_cast<PFN_xrVoidFunction*>(&update));
        if (!create || !destroy || !update) {
            hasFov_ = false;
            SDL_Log("openxr: foveation: the runtime has no foveation calls");
            return;
        }
        XrFoveationLevelProfileCreateInfoFB lp{XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB};
        lp.level = XrFoveationLevelFB(std::clamp(level, 0, 3));
        lp.verticalOffset = 0;
        lp.dynamic = fovDynamic_ && level > 0 ? XR_FOVEATION_DYNAMIC_LEVEL_ENABLED_FB : XR_FOVEATION_DYNAMIC_DISABLED_FB;
        XrFoveationProfileCreateInfoFB pc{XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB};
        pc.next = &lp;
        XrFoveationProfileFB profile = XR_NULL_HANDLE;
        XrResult r = create(session_, &pc, &profile);
        for (Chain& ch : chains_) {
            if (XR_FAILED(r)) break;
            XrSwapchainStateFoveationFB st{XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB};
            st.profile = profile;
            r = update(ch.handle, reinterpret_cast<const XrSwapchainStateBaseHeaderFB*>(&st));
        }
        if (profile != XR_NULL_HANDLE) destroy(profile);
        fovNow_ = level;
        static const char* const names[4] = {"off", "low", "medium", "high"};
        SDL_Log("openxr: foveation %s%s: %s", names[std::clamp(level, 0, 3)], lp.dynamic ? " (dynamic)" : "",
                XR_SUCCEEDED(r) ? "set" : reason(instance_, r).c_str());
    }
    // questperf: Meta's counters (XR_META_performance_metrics) on, their paths listed once
    void startMetrics()
    {
        if (!hasMetrics_) return;
        PFN_xrSetPerformanceMetricsStateMETA set = nullptr;
        PFN_xrEnumeratePerformanceMetricsCounterPathsMETA list = nullptr;
        xrGetInstanceProcAddr(instance_, "xrSetPerformanceMetricsStateMETA", reinterpret_cast<PFN_xrVoidFunction*>(&set));
        xrGetInstanceProcAddr(instance_, "xrEnumeratePerformanceMetricsCounterPathsMETA", reinterpret_cast<PFN_xrVoidFunction*>(&list));
        xrGetInstanceProcAddr(instance_, "xrQueryPerformanceMetricsCounterMETA", reinterpret_cast<PFN_xrVoidFunction*>(&query_));
        XrPerformanceMetricsStateMETA st{XR_TYPE_PERFORMANCE_METRICS_STATE_META};
        st.enabled = XR_TRUE;
        if (!set || !list || !query_ || XR_FAILED(set(session_, &st))) {
            SDL_Log("openxr: performance metrics: not available");
            hasMetrics_ = false;
            return;
        }
        if (!metricPaths_.empty()) return;
        uint32_t n = 0;
        std::vector<XrPath> paths;
        if (XR_SUCCEEDED(list(instance_, 0, &n, nullptr)) && n) {
            paths.resize(n);
            if (XR_FAILED(list(instance_, n, &n, paths.data()))) paths.clear();
        }
        std::string all;
        for (XrPath p : paths) {
            char buf[XR_MAX_PATH_LENGTH] = {};
            uint32_t len = 0;
            if (XR_FAILED(xrPathToString(instance_, p, sizeof buf, &len, buf))) continue;
            std::string s(buf);
            all += (all.empty() ? "" : " ") + s;
            const size_t k = s.find("perfmetrics_meta/");
            metricPaths_.emplace_back(k == std::string::npos ? s : s.substr(k + 17), p);
            if (s.find("app/gpu_frametime") != std::string::npos) gpuPath_ = p;   // (dynamic resolution's input)
        }
        SDL_Log("openxr: performance metrics: %s", all.empty() ? "no counters" : all.c_str());
    }
    // questperf: per frame (after xrBeginFrame): a refresh missed shows as a predicted display time more than one
    // period after the last one; the per-second line (vr.ini perf_log) and perf_cycle's parts
    void perfFrame(uint64_t waitNs, bool shown)
    {
        const uint64_t now = SDL_GetTicksNS();
        const XrTime pt = frameState_.predictedDisplayTime, per = frameState_.predictedDisplayPeriod;
        bool lateNow = false;
        if (lastPredicted_ && per > 0 && pt > lastPredicted_) {
            const int64_t k = (pt - lastPredicted_ + per / 2) / per;
            if (k > 1) ++ps_.late, ps_.missed += int(k - 1), lateNow = true;
        }
        lastPredicted_ = pt;
        ++ps_.frames;
        ps_.waitNs += waitNs, ps_.waitMax = std::max(ps_.waitMax, waitNs);
        workT0_ = now;
        if (dynRes_ && shown) dynamicResolution(now, per, lateNow);
        if (perfCycle_ && shown) {
            struct Part { int fov; float scale; bool around; const char* what; };
            static const Part parts[] = {
                {0, 1.0f, true, "foveation off, full resolution, around on (as before)"},
                {3, 1.0f, true, "foveation high"},
                {3, 0.7f, true, "foveation high, resolution x 0.7"},
                {3, 1.0f, false, "foveation high, around off"},
                {0, 1.0f, false, "foveation off, around off"},
                {0, 0.7f, true, "foveation off, resolution x 0.7"},
            };
            constexpr int kParts = int(sizeof parts / sizeof parts[0]);
            if (!cycleT0_) cycleT0_ = now;
            const int part = int(((now - cycleT0_) / 10000000000ull) % uint64_t(kParts));
            if (part != cyclePart_) {
                cyclePart_ = part;
                applyFoveation(parts[part].fov);
                eyeScale_ = parts[part].scale;
                aroundOff_ = !parts[part].around;
                SDL_Log("quest perf: cycle part %d: %s", part, parts[part].what);
                ps_ = PerfStats{};
                ps_.t0 = now;
                return;
            }
        }
        if (!ps_.t0) ps_.t0 = now;
        if (now - ps_.t0 < 1000000000ull) return;
        if (perfLog_ && shown) {
            const double s = double(now - ps_.t0) / 1e9, n = double(std::max(ps_.frames, 1));
            std::string m;
            if (hasMetrics_ && query_) {
                for (const auto& [name, path] : metricPaths_) {
                    XrPerformanceMetricsCounterMETA c{XR_TYPE_PERFORMANCE_METRICS_COUNTER_META};
                    if (XR_FAILED(query_(session_, path, &c)) || !(c.counterFlags & XR_PERFORMANCE_METRICS_COUNTER_ANY_VALUE_VALID_BIT_META))
                        continue;
                    char b[96];
                    if (c.counterFlags & XR_PERFORMANCE_METRICS_COUNTER_FLOAT_VALUE_VALID_BIT_META)
                        std::snprintf(b, sizeof b, " %s=%.1f", name.c_str(), double(c.floatValue));
                    else
                        std::snprintf(b, sizeof b, " %s=%u", name.c_str(), c.uintValue);
                    m += b;
                }
            }
            SDL_Log("quest perf: %.1f fps, %d late (%d refreshes missed); wait %.1f ms (max %.1f), our frame %.1f ms (max %.1f), "
                    "xrEndFrame %.1f ms; eye %dx%d, foveation %d%s;%s",
                    double(ps_.frames) / s, ps_.late, ps_.missed, double(ps_.waitNs) / 1e6 / n, double(ps_.waitMax) / 1e6,
                    double(ps_.workNs) / 1e6 / n, double(ps_.workMax) / 1e6, double(ps_.endNs) / 1e6 / n, eyeW(0), eyeH(0),
                    std::max(fovNow_, 0), aroundOff_ ? ", around off" : "", m.c_str());
            if (now - threadsAt_ >= 5000000000ull) threadReport(now);
        }
        ps_ = PerfStats{};
        ps_.t0 = now;
    }
    // questperf (vr.ini dynamic_resolution, the Quest's default): every half second the GPU's frame time (Meta's
    // app/gpu_frametime; without it the late frames) against 85 % of the display's period: over it, the eyes drawn
    // into a smaller part of their images (the pixels go with the scale's square), down to kMinScale; well under it
    // and nothing late, 3 % back up a step, to the whole image. At the smallest and still late for 3 s with refresh_rate
    // auto: 72 Hz asked for the rest of the session
    void dynamicResolution(uint64_t now, XrTime period, bool late)
    {
        // questfb (user 2026-11-10 on the Quest 3: "in game it looks a little worse ... the resolution", the menu blurry):
        // the log had the scale at its floor all through Chapter 1 with the GPU still 11-15 ms (the pixels are not what
        // costs), and pushed there by the loading's CPU-late frames (GPU 1.5 ms): the floor 0.8, and with the GPU's
        // time known, late frames alone (the CPU's) no longer lower it
        constexpr float kMinScale = 0.8f;
        if (late) ++dynLate_;
        ++dynFrames_;
        if (!dynT0_) dynT0_ = now;
        if (now - dynT0_ < 500000000ull) return;
        const double budget = double(period) / 1e6 * 0.85;
        double gpu = -1;
        if (query_ && gpuPath_ != XR_NULL_PATH) {
            XrPerformanceMetricsCounterMETA c{XR_TYPE_PERFORMANCE_METRICS_COUNTER_META};
            if (XR_SUCCEEDED(query_(session_, gpuPath_, &c)) && (c.counterFlags & XR_PERFORMANCE_METRICS_COUNTER_FLOAT_VALUE_VALID_BIT_META))
                gpu = double(c.floatValue);
        }
        const double lateFrac = double(dynLate_) / double(std::max(dynFrames_, 1));
        const float was = eyeScale_;
        const bool gpuKnown = gpu >= 0 && gpu < 1000.0;
        if ((gpuKnown && gpu > budget) || (!gpuKnown && lateFrac > 0.1)) {
            const double f = gpu > budget && gpu < 1000.0 ? std::sqrt(budget / (gpu * 1.1)) : 0.9;
            eyeScale_ = std::max(kMinScale, float(double(eyeScale_) * std::clamp(f, 0.75, 0.97)));
        } else if (gpuKnown && gpu < budget * 0.7) {
            eyeScale_ = std::min(1.0f, eyeScale_ + 0.03f);
        }
        if (std::abs(eyeScale_ - dynLogged_) >= 0.05f || (eyeScale_ == 1.0f && dynLogged_ != 1.0f)) {
            dynLogged_ = eyeScale_;
            SDL_Log("quest perf: dynamic resolution %.2f (%dx%d; GPU %.1f ms a frame, budget %.1f, %d of %d late)",
                    double(eyeScale_), eyeW(0), eyeH(0), gpu, budget, dynLate_, dynFrames_);
        }
        // the smallest and still late: 72 Hz (refresh_rate auto only; once a session)
        dynFloorLate_ = was <= kMinScale + 0.001f && lateFrac > 0.1 && (!gpuKnown || gpu > budget) ? dynFloorLate_ + 1 : 0;
        if (dynFloorLate_ >= 6 && refreshWant_ == 0 && refreshAsked_ > 75.0f) {
            PFN_xrRequestDisplayRefreshRateFB request = nullptr;
            xrGetInstanceProcAddr(instance_, "xrRequestDisplayRefreshRateFB", reinterpret_cast<PFN_xrVoidFunction*>(&request));
            const bool asked = request && XR_SUCCEEDED(request(session_, 72.0f));
            SDL_Log("quest perf: still late at the smallest resolution: the display at 72 Hz (%s)", asked ? "asked" : "refused");
            if (asked) refreshWant_ = 72, refreshAsked_ = 72.0f, eyeScale_ = 0.8f;
            dynFloorLate_ = 0;
        }
        dynT0_ = now, dynFrames_ = dynLate_ = 0;
    }
    // questperf: the process's threads' CPU since the last report (/proc/self/task/<tid>/stat: utime + stime), the
    // busiest first, in % of one core
    void threadReport(uint64_t now)
    {
        const double dt = threadsAt_ ? double(now - threadsAt_) / 1e9 : 0.0;
        threadsAt_ = now;
        const long hz = sysconf(_SC_CLK_TCK);
        DIR* d = opendir("/proc/self/task");
        if (!d) return;
        std::vector<std::pair<double, std::string>> busy;
        std::map<int, uint64_t> ticks;
        double total = 0;
        while (const dirent* e = readdir(d)) {
            const int tid = std::atoi(e->d_name);
            if (tid <= 0) continue;
            char path[64], buf[512];
            std::snprintf(path, sizeof path, "/proc/self/task/%d/stat", tid);
            std::FILE* f = std::fopen(path, "rb");
            if (!f) continue;
            const size_t got = std::fread(buf, 1, sizeof buf - 1, f);
            std::fclose(f);
            buf[got] = 0;
            const char* open = std::strchr(buf, '(');
            const char* close = std::strrchr(buf, ')');
            if (!open || !close || close < open) continue;
            unsigned long ut = 0, st = 0;
            if (std::sscanf(close + 1, " %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &ut, &st) != 2) continue;
            const uint64_t t = uint64_t(ut) + uint64_t(st);
            ticks[tid] = t;
            const auto it = threadTicks_.find(tid);
            if (dt <= 0 || it == threadTicks_.end() || hz <= 0) continue;
            const double pct = double(t - it->second) * 100.0 / (double(hz) * dt);
            total += pct;
            if (pct >= 1.0) busy.emplace_back(pct, std::string(open + 1, size_t(close - open - 1)) + "/" + std::to_string(tid));
        }
        closedir(d);
        threadTicks_ = std::move(ticks);
        if (dt <= 0) return;
        std::sort(busy.begin(), busy.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::string l;
        for (size_t i = 0; i < busy.size() && i < 8; ++i) {
            char b[96];
            std::snprintf(b, sizeof b, "%s%s %.0f%%", i ? ", " : "", busy[i].second.c_str(), busy[i].first);
            l += b;
        }
        SDL_Log("quest perf: threads (%% of one core, %.0f s): all %.0f%%: %s", dt, total, l.c_str());
    }
#endif
    // vr2: the play space (space_) = LOCAL moved to `offset`; the old one destroyed after the new one exists
    void setPlaySpace(const XrPosef& offset)
    {
        XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        rs.poseInReferenceSpace = offset;
        {   // (vrnull: the seat's fixed turn on top: the null driver's levelling, HOTD_XR_HEAD; identity otherwise)
            const float o[4] = {offset.orientation.x, offset.orientation.y, offset.orientation.z, offset.orientation.w};
            float q[4];
            quatMul(o, seatFix_, q);
            rs.poseInReferenceSpace.orientation = XrQuaternionf{q[0], q[1], q[2], q[3]};
        }
        XrSpace s = XR_NULL_HANDLE;
        if (XR_FAILED(xrCreateReferenceSpace(session_, &rs, &s)) || !s) return;
        if (space_) xrDestroySpace(space_);
        space_ = s;
        playOffset_ = offset;
    }
    // vr2: recentre: the seat's origin to the head's place now, its heading the head's (yaw only: the horizon stays level)
    void recentre(XrTime t, const char* why)
    {
        SDL_Log("openxr: recentre: %s", why);   // (vrfix: each recentre's reason in the log)
        if (!baseSpace_ || !viewSpace_) return;
        XrSpaceLocation l{XR_TYPE_SPACE_LOCATION};
        if (XR_FAILED(xrLocateSpace(viewSpace_, baseSpace_, t, &l)))
            return;
        // vr4: the head's position not valid (a 3DoF moment, a Quest without a boundary): the heading alone, the seat's
        // place kept (was: nothing happened); vr_aim.h recentrePose (unit-tested)
        Pose head, out;
        toPose(l.pose, head, true);
        const bool posOk = (l.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
        const float keep[3] = {playOffset_.position.x, playOffset_.position.y, playOffset_.position.z};
        if (!recentrePose(head, (l.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0, posOk, keep, out))
            return;
        XrPosef o{};
        o.orientation.x = out.rot[0], o.orientation.y = out.rot[1], o.orientation.z = out.rot[2], o.orientation.w = out.rot[3];
        o.position.x = out.pos[0], o.position.y = out.pos[1], o.position.z = out.pos[2];
        setPlaySpace(o);
        SDL_Log("openxr: recentred (heading %.0f degrees, %s %.2f %.2f %.2f m)", double(2.0f * std::atan2(out.rot[1], out.rot[3]) * 57.29578f),
                posOk ? "the head at" : "heading only (no head position), the seat kept at", double(o.position.x),
                double(o.position.y), double(o.position.z));
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
    void getVec2(XrAction a, XrPath sub, float out[2])   // vrweapon
    {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = a, gi.subactionPath = sub;
        XrActionStateVector2f st{XR_TYPE_ACTION_STATE_VECTOR2F};
        const bool on = XR_SUCCEEDED(xrGetActionStateVector2f(session_, &gi, &st)) && st.isActive;
        out[0] = on ? st.currentState.x : 0.0f, out[1] = on ? st.currentState.y : 0.0f;
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
                    if (running_) applyDisplaySettings();   // vr4: the refresh rate, the performance levels
                } else if (state_ == XR_SESSION_STATE_STOPPING) {
                    xrEndSession(session_);   // (the headset taken off / the app hidden: back to IDLE, READY again later)
                    running_ = false;
                } else if (state_ == XR_SESSION_STATE_EXITING || state_ == XR_SESSION_STATE_LOSS_PENDING) {
                    lost_ = exitAsked_ = true;   // the runtime quits us / the headset is gone (vrflow: the game closes, main.cpp)
                }
            } else if (ev.type == XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB) {   // vr4 (the log)
                const auto& c = reinterpret_cast<const XrEventDataDisplayRefreshRateChangedFB&>(ev);
                SDL_Log("openxr: display refresh rate %.0f -> %.0f Hz", double(c.fromDisplayRefreshRate), double(c.toDisplayRefreshRate));
                // questcrash: moved off the rate asked (a few times at most: the runtime may insist, e.g. too warm)
                if (refreshAsked_ > 0 && std::abs(c.toDisplayRefreshRate - refreshAsked_) > 0.5f && refreshReasks_ < 3 && running_) {
                    PFN_xrRequestDisplayRefreshRateFB request = nullptr;
                    xrGetInstanceProcAddr(instance_, "xrRequestDisplayRefreshRateFB", reinterpret_cast<PFN_xrVoidFunction*>(&request));
                    ++refreshReasks_;
                    const bool again = request && XR_SUCCEEDED(request(session_, refreshAsked_));
                    SDL_Log("openxr: asked %.0f Hz again: %s", double(refreshAsked_), again ? "ok" : "refused");
                }
            } else if (ev.type == XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT) {   // vr4: the headset too warm: its level changed
                const auto& p = reinterpret_cast<const XrEventDataPerfSettingsEXT&>(ev);
                SDL_Log("openxr: performance notification: domain %d sub-domain %d level %d -> %d", int(p.domain), int(p.subDomain),
                        int(p.fromLevel), int(p.toLevel));
            } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
                lost_ = exitAsked_ = true;
            } else if (ev.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED) {
                // vr2: which bindings the runtime chose for each hand (the first headset test's log: docs/vr.md)
                for (int h = 0; h < 2; ++h) {
                    XrInteractionProfileState st{XR_TYPE_INTERACTION_PROFILE_STATE};
                    char path[XR_MAX_PATH_LENGTH] = {};
                    uint32_t len = 0;
                    if (XR_SUCCEEDED(xrGetCurrentInteractionProfile(session_, hand_[h], &st)) && st.interactionProfile != XR_NULL_PATH &&
                        XR_SUCCEEDED(xrPathToString(instance_, st.interactionProfile, XR_MAX_PATH_LENGTH, &len, path))) {
                        SDL_Log("openxr: %s hand: %s", h ? "right" : "left", path);
                        profile_[h] = path;   // (vrprompts: the prompts name its buttons)
                    } else {
                        SDL_Log("openxr: %s hand: no controller", h ? "right" : "left");
                        profile_[h].clear();
                    }
                }
            } else if (ev.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
                // vr2: the runtime's own recentre (Quest: hold the Meta button; SteamVR: Reset Seated Position) moved
                // LOCAL to the head: our recentre's offset is dropped, the runtime's new origin is the seat
                const auto& c = reinterpret_cast<const XrEventDataReferenceSpaceChangePending&>(ev);
                if (c.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL) {
                    XrPosef identity{};
                    identity.orientation.w = 1;
                    setPlaySpace(identity);
                    SDL_Log("openxr: the runtime recentred the seat");
                }
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
            !make(haptic_, XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Recoil") ||
            !make(recentre_, XR_ACTION_TYPE_BOOLEAN_INPUT, "recentre", "Recentre (hold)") ||
            !make(stick_, XR_ACTION_TYPE_VECTOR2F_INPUT, "stick", "Switch weapon (left / right)")) {
            return fail(err, "xrCreateAction", XR_ERROR_VALIDATION_FAILURE);
        }
        // suggested bindings per interaction profile (the runtime remaps other controllers to one of these). A path the
        // profile lacks fails its whole list: the lists are the spec's (OpenXR 1.0 section 6.4)
        using L = std::vector<std::pair<XrAction, std::string>>;
        auto bind = [&](const char* profileName, const L& list) {
            std::vector<XrActionSuggestedBinding> b;
            for (const auto& [a, p] : list) {
                XrPath path;
                if (XR_SUCCEEDED(xrStringToPath(instance_, p.c_str(), &path))) b.push_back({a, path});
            }
            XrPath prof;
            xrStringToPath(instance_, profileName, &prof);
            XrInteractionProfileSuggestedBinding s{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            s.interactionProfile = prof;
            s.countSuggestedBindings = uint32_t(b.size());
            s.suggestedBindings = b.data();
            if (const XrResult r = xrSuggestInteractionProfileBindings(instance_, &s); XR_FAILED(r))
                SDL_Log("openxr: bindings for %s: %s", profileName, reason(instance_, r).c_str());
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
        // vrflow (user 2026-11-10, "I don't know how to pause"): the left Y is Pause too (Touch, HP; was a reload: the
        // grip, A, B, the shake / flick and pointing down still reload), a dedicated button beside the left menu
        // button, which SteamVR may keep for its dashboard
        bind("/interaction_profiles/oculus/touch_controller",   // (Quest, Rift S; the right menu is the system's)
             common({{trigger_, "/user/hand/left/input/trigger/value"}, {trigger_, "/user/hand/right/input/trigger/value"},
                     {squeeze_, "/user/hand/left/input/squeeze/value"}, {squeeze_, "/user/hand/right/input/squeeze/value"},
                     {primary_, "/user/hand/left/input/x/click"}, {primary_, "/user/hand/right/input/a/click"},
                     {menu_, "/user/hand/left/input/y/click"}, {secondary_, "/user/hand/right/input/b/click"},
                     {menu_, "/user/hand/left/input/menu/click"},
                     {recentre_, "/user/hand/left/input/thumbstick/click"}, {recentre_, "/user/hand/right/input/thumbstick/click"},
                     {stick_, "/user/hand/left/input/thumbstick"}, {stick_, "/user/hand/right/input/thumbstick"}}));
        bind("/interaction_profiles/valve/index_controller",   // (no menu button: the left B)
             common({{trigger_, "/user/hand/left/input/trigger/value"}, {trigger_, "/user/hand/right/input/trigger/value"},
                     {squeeze_, "/user/hand/left/input/squeeze/value"}, {squeeze_, "/user/hand/right/input/squeeze/value"},
                     {primary_, "/user/hand/left/input/a/click"}, {primary_, "/user/hand/right/input/a/click"},
                     {secondary_, "/user/hand/right/input/b/click"}, {menu_, "/user/hand/left/input/b/click"},
                     {recentre_, "/user/hand/left/input/thumbstick/click"}, {recentre_, "/user/hand/right/input/thumbstick/click"},
                     {stick_, "/user/hand/left/input/thumbstick"}, {stick_, "/user/hand/right/input/thumbstick"}}));
        bind("/interaction_profiles/htc/vive_controller",
             common({{trigger_, "/user/hand/left/input/trigger/value"}, {trigger_, "/user/hand/right/input/trigger/value"},
                     {squeeze_, "/user/hand/left/input/squeeze/click"}, {squeeze_, "/user/hand/right/input/squeeze/click"},
                     {primary_, "/user/hand/left/input/trackpad/click"}, {primary_, "/user/hand/right/input/trackpad/click"},
                     {menu_, "/user/hand/left/input/menu/click"}, {menu_, "/user/hand/right/input/menu/click"},
                     {stick_, "/user/hand/left/input/trackpad"}, {stick_, "/user/hand/right/input/trackpad"}}));
        bind("/interaction_profiles/microsoft/motion_controller",   // (Windows Mixed Reality)
             common({{trigger_, "/user/hand/left/input/trigger/value"}, {trigger_, "/user/hand/right/input/trigger/value"},
                     {squeeze_, "/user/hand/left/input/squeeze/click"}, {squeeze_, "/user/hand/right/input/squeeze/click"},
                     {primary_, "/user/hand/left/input/trackpad/click"}, {primary_, "/user/hand/right/input/trackpad/click"},
                     {menu_, "/user/hand/left/input/menu/click"}, {menu_, "/user/hand/right/input/menu/click"},
                     {recentre_, "/user/hand/left/input/thumbstick/click"}, {recentre_, "/user/hand/right/input/thumbstick/click"},
                     {stick_, "/user/hand/left/input/thumbstick"}, {stick_, "/user/hand/right/input/thumbstick"}}));
        if (hpControllers_)   // vrship: HP Reverb G2 (XR_EXT_hp_mixed_reality_controller; the Touch layout: X / Y, A / B)
            bind("/interaction_profiles/hp/mixed_reality_controller",
                 common({{trigger_, "/user/hand/left/input/trigger/value"}, {trigger_, "/user/hand/right/input/trigger/value"},
                         {squeeze_, "/user/hand/left/input/squeeze/value"}, {squeeze_, "/user/hand/right/input/squeeze/value"},
                         {primary_, "/user/hand/left/input/x/click"}, {primary_, "/user/hand/right/input/a/click"},
                         {menu_, "/user/hand/left/input/y/click"}, {secondary_, "/user/hand/right/input/b/click"},
                         {menu_, "/user/hand/left/input/menu/click"}, {menu_, "/user/hand/right/input/menu/click"},
                         {recentre_, "/user/hand/left/input/thumbstick/click"}, {recentre_, "/user/hand/right/input/thumbstick/click"},
                     {stick_, "/user/hand/left/input/thumbstick"}, {stick_, "/user/hand/right/input/thumbstick"}}));
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
        {   // vrship: the runtime's formats in the log (GL enums, its preferred first)
            std::string l;
            for (int64_t f : formats) l += (l.empty() ? "0x" : " 0x") + [](int64_t v) { char b[20]; std::snprintf(b, sizeof b, "%llX", static_cast<unsigned long long>(v)); return std::string(b); }(f);
            SDL_Log("openxr: swapchain formats: %s (0x8C43 = SRGB8_ALPHA8, 0x8058 = RGBA8)", l.c_str());
        }
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
        // (two eye slots side by side: desktop_view both; the scene's own pictures go into the first)
        gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, 2 * kMirrorSize, kMirrorSize, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
        gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, mirrorTex_, 0);
        gl::GenRenderbuffers(1, &mirrorDepth_);
        gl::BindRenderbuffer(gl::RENDERBUFFER, mirrorDepth_);
        gl::RenderbufferStorage(gl::RENDERBUFFER, gl::DEPTH_COMPONENT24, 2 * kMirrorSize, kMirrorSize);
        gl::FramebufferRenderbuffer(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::RENDERBUFFER, mirrorDepth_);
        gl::framebufferOk("openxr mirror");
        gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    }
};

} // namespace

std::unique_ptr<Backend> makeOpenXr() { return std::make_unique<OpenXr>(); }

} // namespace hotd::vr

#endif // HOTD_OPENXR
