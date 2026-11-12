// vrship (user 2026-11-09): --vr-check (vr_check.h; docs/vr.md "VR check"). The same OpenXR calls as the game's own
// start (vr_openxr.cpp: instance, system, the OpenGL graphics requirements, a session on a hidden window's context),
// listing what each one returns, then everything is destroyed again: the runtime's own window may flash in the headset
// for a moment, nothing is drawn. Written on a PC with Meta Horizon Link as the runtime and no headset connected.
#include "vr/vr_check.h"

#include <SDL3/SDL.h>

#if defined(HOTD_OPENXR) && !defined(__ANDROID__)

#include "hotd_version.h"
#include "render/gl.h"

#if defined(_WIN32)
#define XR_USE_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>
#else
#define XR_USE_PLATFORM_XLIB
#include <GL/glx.h>
#endif
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace hotd::vr {

namespace {

void say(const std::string& s) { SDL_Log("vr-check: %s", s.c_str()); }

std::string version(XrVersion v)
{
    return std::to_string(XR_VERSION_MAJOR(v)) + "." + std::to_string(XR_VERSION_MINOR(v)) + "." + std::to_string(XR_VERSION_PATCH(v));
}

std::string result(XrInstance inst, XrResult r)
{
    char name[XR_MAX_RESULT_STRING_SIZE] = {};
    if (inst != XR_NULL_HANDLE && XR_SUCCEEDED(xrResultToString(inst, r, name))) return std::string(name);
    switch (r) {
    case XR_ERROR_RUNTIME_UNAVAILABLE: return "XR_ERROR_RUNTIME_UNAVAILABLE (no runtime set active, or it would not load)";
    case XR_ERROR_RUNTIME_FAILURE: return "XR_ERROR_RUNTIME_FAILURE";
    case XR_ERROR_FORM_FACTOR_UNAVAILABLE: return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
    default: return "error " + std::to_string(int(r));
    }
}

std::string glFormat(int64_t f)
{
    switch (f) {
    case 0x8058: return "RGBA8";
    case 0x8C43: return "SRGB8_ALPHA8";
    case 0x8051: return "RGB8";
    case 0x8C41: return "SRGB8";
    case 0x881A: return "RGBA16F";
    case 0x881B: return "RGB16F";
    case 0x8814: return "RGBA32F";
    case 0x8C3A: return "R11F_G11F_B10F";
    case 0x8059: return "RGB10_A2";
    case 0x8D62: return "RGB565";
    case 0x81A5: return "DEPTH16";
    case 0x81A6: return "DEPTH24";
    case 0x81A7: return "DEPTH32";
    case 0x8CAC: return "DEPTH32F";
    case 0x88F0: return "DEPTH24_STENCIL8";
    case 0x8CAD: return "DEPTH32F_STENCIL8";
    default: break;
    }
    char b[24];
    std::snprintf(b, sizeof b, "0x%llX", static_cast<unsigned long long>(f));
    return b;
}

// a runtime manifest's "name" and "library_path" (a plain look for the keys: these files are small and flat)
std::string jsonValue(const std::string& text, const char* key)
{
    const size_t k = text.find(std::string("\"") + key + "\"");
    if (k == std::string::npos) return {};
    const size_t a = text.find('"', text.find(':', k) + 1);
    const size_t b = a == std::string::npos ? a : text.find('"', a + 1);
    return a == std::string::npos || b == std::string::npos ? std::string() : text.substr(a + 1, b - a - 1);
}

void describeManifest(const std::string& path, const char* indent)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        say(std::string(indent) + "(the file cannot be read: the runtime is uninstalled or moved)");
        return;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string name = jsonValue(ss.str(), "name"), lib = jsonValue(ss.str(), "library_path");
    say(std::string(indent) + "name \"" + (name.empty() ? "?" : name) + "\", library " + (lib.empty() ? "?" : lib));
}

#if defined(_WIN32)
std::string narrow(const std::wstring& w)
{
    if (w.empty()) return {};
    std::string s(size_t(WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), int(s.size()), nullptr, nullptr);
    return s;
}

// the loader's own choice: XR_RUNTIME_JSON, else HKLM\SOFTWARE\Khronos\OpenXR\1 ActiveRuntime; and the runtimes that
// registered themselves (AvailableRuntimes: the ones a "Set as active" button can pick)
void describeRegistry()
{
    if (const char* env = std::getenv("XR_RUNTIME_JSON")) {
        say(std::string("XR_RUNTIME_JSON (over the registry): ") + env);
        describeManifest(env, "  ");
    }
    wchar_t buf[1024] = {};
    DWORD size = sizeof buf;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime", RRF_RT_REG_SZ, nullptr, buf, &size) ==
        ERROR_SUCCESS) {
        say("active runtime (registry): " + narrow(buf));
        describeManifest(narrow(buf), "  ");
    } else {
        say("active runtime (registry): NONE (no runtime set active: Meta Horizon Link > Settings > General > OpenXR "
            "Runtime, or SteamVR > Settings > OpenXR > Set SteamVR as OpenXR runtime)");
    }
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1\\AvailableRuntimes", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        for (DWORD i = 0;; ++i) {
            wchar_t name[1024];
            DWORD nl = 1024, type = 0, data = 0, dl = sizeof data;
            if (RegEnumValueW(k, i, name, &nl, nullptr, &type, reinterpret_cast<BYTE*>(&data), &dl) != ERROR_SUCCESS) break;
            say(std::string("installed runtime: ") + narrow(name) + (type == REG_DWORD && data ? " (disabled)" : ""));
        }
        RegCloseKey(k);
    } else {
        say("installed runtimes: none registered under AvailableRuntimes (older runtimes do not register there)");
    }
}
#endif

struct ProfileCheck {
    const char* path;
    const char* extension;   // nullptr: core OpenXR 1.0
    const char* what;
};
constexpr ProfileCheck kProfiles[] = {
    {"/interaction_profiles/khr/simple_controller", nullptr, "any controller: the fallback"},
    {"/interaction_profiles/oculus/touch_controller", nullptr, "Meta Quest / Rift Touch, also a Quest through Steam Link"},
    {"/interaction_profiles/valve/index_controller", nullptr, "Valve Index"},
    {"/interaction_profiles/htc/vive_controller", nullptr, "HTC Vive wands"},
    {"/interaction_profiles/microsoft/motion_controller", nullptr, "Windows Mixed Reality"},
    {"/interaction_profiles/hp/mixed_reality_controller", XR_EXT_HP_MIXED_REALITY_CONTROLLER_EXTENSION_NAME, "HP Reverb G2"},
};

} // namespace

int checkOpenXr()
{
    say(std::string("The House of the Dead: ReWrithe ") + HOTD_VERSION_TEXT + ": the OpenXR runtime (VR headsets)");
#if defined(_WIN32)
    describeRegistry();
    if (!LoadLibraryW(L"openxr_loader.dll")) {
        say("RESULT: openxr_loader.dll is missing beside hotd.exe (unzip the whole package again)");
        return 1;
    }
#endif
    uint32_t n = 0;
    if (xrEnumerateApiLayerProperties(0, &n, nullptr) == XR_SUCCESS && n) {
        std::vector<XrApiLayerProperties> layers(n, XrApiLayerProperties{XR_TYPE_API_LAYER_PROPERTIES});
        xrEnumerateApiLayerProperties(n, &n, layers.data());
        std::string l;
        for (const auto& p : layers) l += std::string(l.empty() ? "" : ", ") + p.layerName;
        say("API layers (add-ons between the game and the runtime; one can break VR): " + l);
    } else {
        say("API layers: none");
    }
    XrResult r = xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr);
    if (XR_FAILED(r)) {
        say("RESULT: no OpenXR runtime could be loaded: " + result(XR_NULL_HANDLE, r) +
            ". Start the headset's PC app (Meta Horizon Link or SteamVR) and set it as the OpenXR runtime.");
        return 1;
    }
    std::vector<XrExtensionProperties> exts(n, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, n, &n, exts.data());
    auto has = [&](const char* e) {
        return std::any_of(exts.begin(), exts.end(), [&](const XrExtensionProperties& p) { return std::strcmp(p.extensionName, e) == 0; });
    };
    {
        std::string l;
        for (const auto& e : exts) l += std::string(l.empty() ? "" : " ") + e.extensionName;
        say("extensions (" + std::to_string(n) + "): " + l);
    }
    const bool glOk = has(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
    say(std::string("  XR_KHR_opengl_enable (the game needs it): ") + (glOk ? "yes" : "NO"));
    const bool hpOk = has(XR_EXT_HP_MIXED_REALITY_CONTROLLER_EXTENSION_NAME);
    const bool rateOk = has(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    if (!glOk) {
        say("RESULT: this runtime has no OpenGL support, and the game draws with OpenGL. Use another runtime (Meta Horizon "
            "Link and SteamVR both have it).");
        return 3;
    }
    std::vector<const char*> want = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
    if (hpOk) want.push_back(XR_EXT_HP_MIXED_REALITY_CONTROLLER_EXTENSION_NAME);
    if (rateOk) want.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strncpy(ici.applicationInfo.applicationName, "HOTD ReWrithe (VR check)", XR_MAX_APPLICATION_NAME_SIZE - 1);
    ici.applicationInfo.applicationVersion = 1;
    std::strncpy(ici.applicationInfo.engineName, "hotd", XR_MAX_ENGINE_NAME_SIZE - 1);
    ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ici.enabledExtensionCount = uint32_t(want.size());
    ici.enabledExtensionNames = want.data();
    XrInstance inst = XR_NULL_HANDLE;
    if (r = xrCreateInstance(&ici, &inst); XR_FAILED(r)) {
        say("RESULT: the runtime refused to start (xrCreateInstance): " + result(XR_NULL_HANDLE, r) +
            ". Is its PC app running (Meta Horizon Link / SteamVR)?");
        return 1;
    }
    int code = 0;
    std::string verdict;
    XrSession session = XR_NULL_HANDLE;
    SDL_Window* win = nullptr;
    SDL_GLContext ctx = nullptr;
    bool sdlUp = false;
    do {
        XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
        if (XR_SUCCEEDED(xrGetInstanceProperties(inst, &ip))) say(std::string("runtime: ") + ip.runtimeName + " " + version(ip.runtimeVersion));
        // controller profiles: the game's suggestions are accepted per profile (a profile the runtime does not know fails)
        {
            XrActionSet set = XR_NULL_HANDLE;
            XrActionSetCreateInfo ai{XR_TYPE_ACTION_SET_CREATE_INFO};
            std::strcpy(ai.actionSetName, "check");
            std::strcpy(ai.localizedActionSetName, "Check");
            XrAction pose = XR_NULL_HANDLE;
            XrPath hands[2] = {};
            xrStringToPath(inst, "/user/hand/left", &hands[0]);
            xrStringToPath(inst, "/user/hand/right", &hands[1]);
            if (XR_SUCCEEDED(xrCreateActionSet(inst, &ai, &set))) {
                XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
                ci.actionType = XR_ACTION_TYPE_POSE_INPUT;
                std::strcpy(ci.actionName, "aim");
                std::strcpy(ci.localizedActionName, "Aim");
                ci.countSubactionPaths = 2;
                ci.subactionPaths = hands;
                xrCreateAction(set, &ci, &pose);
            }
            std::string ok, no;
            for (const ProfileCheck& p : kProfiles) {
                const std::string label = std::string(p.path + std::strlen("/interaction_profiles/")) + " (" + p.what + ")";
                if (p.extension && !(p.extension == std::string(XR_EXT_HP_MIXED_REALITY_CONTROLLER_EXTENSION_NAME) && hpOk)) {
                    no += (no.empty() ? "" : "; ") + label + " [needs " + p.extension + "]";
                    continue;
                }
                XrPath prof = XR_NULL_PATH, path = XR_NULL_PATH;
                xrStringToPath(inst, p.path, &prof);
                xrStringToPath(inst, "/user/hand/right/input/aim/pose", &path);
                const XrActionSuggestedBinding b{pose, path};
                XrInteractionProfileSuggestedBinding s{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
                s.interactionProfile = prof;
                s.countSuggestedBindings = 1;
                s.suggestedBindings = &b;
                if (pose && XR_SUCCEEDED(xrSuggestInteractionProfileBindings(inst, &s))) ok += (ok.empty() ? "" : "; ") + label;
                else no += (no.empty() ? "" : "; ") + label;
            }
            say("controller profiles accepted: " + (ok.empty() ? std::string("none") : ok));
            if (!no.empty()) say("controller profiles not accepted: " + no);
            if (set) xrDestroyActionSet(set);
        }
        XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
        sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrSystemId sys = XR_NULL_SYSTEM_ID;
        if (r = xrGetSystem(inst, &sgi, &sys); XR_FAILED(r)) {
            code = 2;
            verdict = "no headset found (" + result(inst, r) + "). Connect it and start Link (Meta) or the Steam Link app "
                      "(SteamVR) in the headset, then run this again.";
            break;
        }
        XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
        if (XR_SUCCEEDED(xrGetSystemProperties(inst, sys, &sp))) {
            char b[256];
            std::snprintf(b, sizeof b, "headset: %s (vendor 0x%X); largest picture %ux%u, %u layers; tracking: turning %s, moving %s",
                          sp.systemName, unsigned(sp.vendorId), unsigned(sp.graphicsProperties.maxSwapchainImageWidth),
                          unsigned(sp.graphicsProperties.maxSwapchainImageHeight), unsigned(sp.graphicsProperties.maxLayerCount),
                          sp.trackingProperties.orientationTracking ? "yes" : "NO", sp.trackingProperties.positionTracking ? "yes" : "NO");
            say(b);
        }
        uint32_t vn = 0;
        xrEnumerateViewConfigurationViews(inst, sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &vn, nullptr);
        std::vector<XrViewConfigurationView> views(vn, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
        xrEnumerateViewConfigurationViews(inst, sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, vn, &vn, views.data());
        for (uint32_t i = 0; i < vn; ++i) {
            char b[160];
            std::snprintf(b, sizeof b, "eye %u: %ux%u recommended (largest %ux%u), %u samples", unsigned(i),
                          unsigned(views[i].recommendedImageRectWidth), unsigned(views[i].recommendedImageRectHeight),
                          unsigned(views[i].maxImageRectWidth), unsigned(views[i].maxImageRectHeight),
                          unsigned(views[i].recommendedSwapchainSampleCount));
            say(b);
        }
        if (vn < 2) {
            code = 3;
            verdict = "the headset has no stereo view (two eyes)";
            break;
        }
        uint32_t bn = 0;
        xrEnumerateEnvironmentBlendModes(inst, sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &bn, nullptr);
        std::vector<XrEnvironmentBlendMode> blends(bn);
        xrEnumerateEnvironmentBlendModes(inst, sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, bn, &bn, blends.data());
        if (std::find(blends.begin(), blends.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE) == blends.end())
            say("blend modes: no OPAQUE (the game asks for it)");
        // the OpenGL side: the runtime's version range, then a session on a hidden window's context (the game's own way)
        PFN_xrGetOpenGLGraphicsRequirementsKHR req = nullptr;
        xrGetInstanceProcAddr(inst, "xrGetOpenGLGraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction*>(&req));
        XrGraphicsRequirementsOpenGLKHR gr{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
        if (!req || XR_FAILED(r = req(inst, sys, &gr))) {
            code = 3;
            verdict = "the runtime's OpenGL requirements call failed (" + result(inst, r) + ")";
            break;
        }
        say("OpenGL the runtime asks: " + version(gr.minApiVersionSupported) + " .. " + version(gr.maxApiVersionSupported));
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            code = 3;
            verdict = std::string("SDL_Init: ") + SDL_GetError();
            break;
        }
        sdlUp = true;
        gl::contextAttributes();
        win = SDL_CreateWindow("hotd --vr-check", 320, 240, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        ctx = win ? SDL_GL_CreateContext(win) : nullptr;
        if (!ctx || gl::load()) {
            code = 3;
            verdict = std::string("no OpenGL 3.3 context on this PC: ") + SDL_GetError();
            break;
        }
        say(std::string("this PC's OpenGL: ") + reinterpret_cast<const char*>(gl::GetString(gl::VENDOR)) + " / " +
            reinterpret_cast<const char*>(gl::GetString(gl::RENDERER)) + " / " + reinterpret_cast<const char*>(gl::GetString(gl::VERSION)));
#if defined(_WIN32)
        XrGraphicsBindingOpenGLWin32KHR gb{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
        gb.hDC = wglGetCurrentDC();
        gb.hGLRC = wglGetCurrentContext();
#else
        XrGraphicsBindingOpenGLXlibKHR gb{XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
        gb.xDisplay = glXGetCurrentDisplay();
        gb.glxDrawable = glXGetCurrentDrawable();
        gb.glxContext = glXGetCurrentContext();
#endif
        XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
        sci.next = &gb;
        sci.systemId = sys;
        if (r = xrCreateSession(inst, &sci, &session); XR_FAILED(r)) {
            code = 3;
            verdict = "the runtime would not take the game's OpenGL picture (xrCreateSession: " + result(inst, r) + ")" +
                      (r == XR_ERROR_GRAPHICS_DEVICE_INVALID ? ". On a laptop: set hotd.exe to the dedicated graphics card "
                                                               "(Windows Settings > System > Display > Graphics)" : "");
            break;
        }
        uint32_t fn = 0;
        xrEnumerateSwapchainFormats(session, 0, &fn, nullptr);
        std::vector<int64_t> formats(fn);
        xrEnumerateSwapchainFormats(session, fn, &fn, formats.data());
        std::string fl;
        for (int64_t f : formats) fl += (fl.empty() ? "" : " ") + glFormat(f);
        say("picture formats (the runtime's order): " + fl);
        const bool srgb = std::find(formats.begin(), formats.end(), int64_t(0x8C43)) != formats.end();
        const bool rgba = std::find(formats.begin(), formats.end(), int64_t(0x8058)) != formats.end();
        say(std::string("  the game takes: ") + (srgb ? "SRGB8_ALPHA8" : rgba ? "RGBA8 (no sRGB format: colors may look pale)" : "NONE"));
        if (!srgb && !rgba) {
            code = 3;
            verdict = "no 8-bit RGBA picture format";
            break;
        }
        uint32_t sn = 0;
        xrEnumerateReferenceSpaces(session, 0, &sn, nullptr);
        std::vector<XrReferenceSpaceType> spaces(sn);
        xrEnumerateReferenceSpaces(session, sn, &sn, spaces.data());
        std::string sl;
        for (XrReferenceSpaceType t : spaces)
            sl += std::string(sl.empty() ? "" : " ") + (t == XR_REFERENCE_SPACE_TYPE_VIEW ? "VIEW" : t == XR_REFERENCE_SPACE_TYPE_LOCAL ? "LOCAL"
                                                       : t == XR_REFERENCE_SPACE_TYPE_STAGE ? "STAGE" : std::to_string(int(t)).c_str());
        say("spaces: " + sl);
        if (rateOk) {
            PFN_xrEnumerateDisplayRefreshRatesFB rates = nullptr;
            xrGetInstanceProcAddr(inst, "xrEnumerateDisplayRefreshRatesFB", reinterpret_cast<PFN_xrVoidFunction*>(&rates));
            uint32_t rn = 0;
            if (rates && XR_SUCCEEDED(rates(session, 0, &rn, nullptr)) && rn) {
                std::vector<float> hz(rn);
                rates(session, rn, &rn, hz.data());
                std::string rl;
                for (float h : hz) rl += (rl.empty() ? "" : " ") + std::to_string(int(h + 0.5f));
                say("refresh rates (Hz): " + rl);
            }
        }
    } while (false);
    if (session) xrDestroySession(session);
    xrDestroyInstance(inst);
    if (ctx) SDL_GL_DestroyContext(ctx);
    if (win) SDL_DestroyWindow(win);
    if (sdlUp) SDL_Quit();
    say(code == 0 ? "RESULT: OK: the game can start in VR with this runtime (\"The House of the Dead ReWrithe VR.exe\" or "
                    "\"Play in VR.bat\")"
                  : "RESULT: " + verdict);
    return code;
}

} // namespace hotd::vr

#else

namespace hotd::vr {

int checkOpenXr()
{
#if defined(__ANDROID__)
    SDL_Log("vr-check: on the headset the game's start logs the same (adb logcat: the openxr: lines)");
#else
    SDL_Log("vr-check: this build has no OpenXR support (built without the OpenXR SDK: docs/vr.md)");
#endif
    return 1;
}

} // namespace hotd::vr

#endif
