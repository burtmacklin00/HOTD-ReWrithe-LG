// The resolution choice (resolution.h)
#include "util/resolution.h"

#include "util/fullscreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hotd::util {

ResolutionChoice& resolutionLive()
{
    static ResolutionChoice c;
    return c;
}

std::string sizeKey(Size s)
{
    return s.automatic() ? std::string("auto") : std::to_string(s.w) + "x" + std::to_string(s.h);
}

Size parseSize(const std::string& v)
{
    int w = 0, h = 0;
    if (std::sscanf(v.c_str(), "%dx%d", &w, &h) != 2 || w < 320 || h < 240 || w > 16384 || h > 16384)
        return {};
    return {w, h};
}

std::string sizeName(Size s)
{
    return s.automatic() ? std::string("Auto") : std::to_string(s.w) + " x " + std::to_string(s.h);
}

namespace {
bool gHiddenExclusive = false;   // rescrash: a hidden window "in" exclusive fullscreen (the tests: the display's mode untouched)
bool hidden(SDL_Window* w) { return (SDL_GetWindowFlags(w) & SDL_WINDOW_HIDDEN) != 0; }
} // namespace

bool windowExclusive(SDL_Window* w)
{
    return (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0 || (gHiddenExclusive && hidden(w));
}

bool windowFullscreen(SDL_Window* w)
{
    return windowExclusive(w) || (SDL_GetWindowFlags(w) & SDL_WINDOW_BORDERLESS) != 0;
}

bool underGamescope()
{
#ifdef _WIN32
    return false;
#else
    static const bool on = []() {
        if (const char* t = std::getenv("HOTD_GAMESCOPE"); t && *t)
            return *t != '0';
        const char* desk = std::getenv("XDG_CURRENT_DESKTOP");
        const char* gs = std::getenv("GAMESCOPE_WAYLAND_DISPLAY");
        return (desk && std::strstr(desk, "gamescope")) || (gs && *gs);
    }();
    return on;
#endif
}

namespace {
bool gHiddenDesktopFs = false;   // deckfs: a hidden window's exclusive note is the desktop's fullscreen (the tests)
} // namespace

bool windowFullscreenDesktop(SDL_Window* w)
{
    if (gHiddenExclusive && hidden(w))
        return gHiddenDesktopFs;
    return (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0 && SDL_GetWindowFullscreenMode(w) == nullptr;
}

void logWindowState(SDL_Window* w, const char* when)
{
    int lw = 0, lh = 0, pw = 0, ph = 0, wx = 0, wy = 0;
    SDL_GetWindowSize(w, &lw, &lh);
    SDL_GetWindowSizeInPixels(w, &pw, &ph);
    SDL_GetWindowPosition(w, &wx, &wy);
    SDL_DisplayID d = SDL_GetDisplayForWindow(w);
    if (!d)
        d = SDL_GetPrimaryDisplay();
    SDL_Rect b{};
    SDL_GetDisplayBounds(d, &b);
    const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(d);
    const SDL_WindowFlags f = SDL_GetWindowFlags(w);
    const char* drv = SDL_GetCurrentVideoDriver();
    const bool fs = (f & SDL_WINDOW_FULLSCREEN) != 0, bl = (f & SDL_WINDOW_BORDERLESS) != 0;
    SDL_Log("window (%s): %s%s, %d x %d at %d, %d (%d x %d pixels, density %.2f, scale %.2f); display %d x %d at %d, %d, "
            "desktop %d x %d; %s%s%s",
            when, drv ? drv : "?", underGamescope() ? ", gamescope" : "", lw, lh, wx, wy, pw, ph,
            double(SDL_GetWindowPixelDensity(w)), double(SDL_GetWindowDisplayScale(w)), b.w, b.h, b.x, b.y, dm ? dm->w : 0,
            dm ? dm->h : 0, fs ? (windowFullscreenDesktop(w) ? "fullscreen (the desktop's)" : "fullscreen (a display mode)") : "",
            bl ? (fs ? ", borderless" : "borderless") : "", (fs || bl) ? "" : "windowed");
}

SDL_Rect visibleArea(const SDL_Rect& d, int wx, int wy, int lw, int lh, bool whole)
{
    const SDL_Rect all{0, 0, lw, lh};
    if (whole || d.w <= 0 || d.h <= 0)
        return all;
    const int x0 = std::max(0, d.x - wx), y0 = std::max(0, d.y - wy);
    const int x1 = std::min(lw, d.x + d.w - wx), y1 = std::min(lh, d.y + d.h - wy);
    if (x1 - x0 >= lw / 2 && y1 - y0 >= lh / 2)   // (a window mostly off its display: the window's own edges)
        return SDL_Rect{x0, y0, x1 - x0, y1 - y0};
    return all;
}

namespace {
void sortUnique(std::vector<Size>& v)
{
    std::sort(v.begin(), v.end(), [](const Size& a, const Size& b) { return a.w != b.w ? a.w < b.w : a.h < b.h; });
    v.erase(std::unique(v.begin(), v.end()), v.end());
}

SDL_DisplayID displayOf(SDL_Window* w)
{
    const SDL_DisplayID d = SDL_GetDisplayForWindow(w);
    return d ? d : SDL_GetPrimaryDisplay();
}

// exclusive fullscreen in the display's mode closest to s (the highest refresh rate of that size; Auto: the desktop's
// size). rescrash: a hidden window (the tests) only notes it, so no test ever changes the display's mode
#ifndef _WIN32
// deckfs (GitHub #74): Fullscreen as the window manager's / compositor's fullscreen at the desktop's size (no display
// mode of our own). A Steam Deck's log: "no display mode near 1920 x 1080 (Couldn't find any matching video modes)",
// so Fullscreen stayed Borderless (in Game Mode and on the desktop); under gamescope it is the only fullscreen there is
bool enterDesktopFullscreen(SDL_Window* w, const char* why)
{
    if (hidden(w)) {
        gHiddenExclusive = gHiddenDesktopFs = true;
        SDL_Log("resolution: fullscreen at the desktop's size, %s (a hidden window: noted, the display unchanged)", why);
        return true;
    }
    if (!SDL_SetWindowFullscreenMode(w, nullptr) || !SDL_SetWindowFullscreen(w, true)) {
        SDL_Log("resolution: the desktop's fullscreen not set either (%s): borderless", SDL_GetError());
        SDL_SetWindowFullscreen(w, false);
        return false;
    }
    SDL_SyncWindow(w);
    SDL_Log("resolution: fullscreen at the desktop's size, %s", why);
    return true;
}
#endif

bool enterExclusive(SDL_Window* w, Size s)
{
    gHiddenDesktopFs = false;
#ifndef _WIN32
    if (underGamescope())
        return enterDesktopFullscreen(w, "gamescope's (it scales the game to the screen: the same picture as Borderless)");
#endif
    if (s.automatic()) {
        if (const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(displayOf(w)))
            s = {dm->w, dm->h};
        else
            return false;
    }
    SDL_DisplayMode mode{};
    if (!SDL_GetClosestFullscreenDisplayMode(displayOf(w), s.w, s.h, 0.0f, true, &mode)) {
        SDL_Log("resolution: no display mode near %d x %d (%s)", s.w, s.h, SDL_GetError());
#ifndef _WIN32
        return enterDesktopFullscreen(w, "no display mode of that size");
#else
        return false;
#endif
    }
    if (hidden(w)) {
        gHiddenExclusive = true;
        SDL_Log("resolution: fullscreen %d x %d (a hidden window: noted, the display unchanged)", mode.w, mode.h);
        return true;
    }
    if (!SDL_SetWindowFullscreenMode(w, &mode) || !SDL_SetWindowFullscreen(w, true)) {
        SDL_Log("resolution: %d x %d not set (%s)", mode.w, mode.h, SDL_GetError());
        SDL_SetWindowFullscreen(w, false);
#ifndef _WIN32
        return enterDesktopFullscreen(w, "the display mode could not be set");
#else
        return false;
#endif
    }
    SDL_SyncWindow(w);
    SDL_Log("resolution: fullscreen %d x %d at %.2f Hz", mode.w, mode.h, double(mode.refresh_rate));
    return true;
}

void leaveExclusive(SDL_Window* w)
{
    if (!windowExclusive(w))
        return;
    if (gHiddenExclusive && hidden(w)) {
        gHiddenExclusive = false;
        return;
    }
    gHiddenExclusive = false;
    SDL_SetWindowFullscreen(w, false);
    SDL_SyncWindow(w);
}

void sizeWindow(SDL_Window* w, Size s)
{
    if (s.automatic())
        return;
    if (SDL_GetWindowFlags(w) & SDL_WINDOW_MAXIMIZED) {
        SDL_RestoreWindow(w);
        SDL_SyncWindow(w);
    }
    const SDL_DisplayID d = displayOf(w);
    SDL_SetWindowSize(w, s.w, s.h);
    SDL_SetWindowPosition(w, int(SDL_WINDOWPOS_CENTERED_DISPLAY(d)), int(SDL_WINDOWPOS_CENTERED_DISPLAY(d)));
    SDL_SyncWindow(w);
}
} // namespace

std::vector<Size> fullscreenSizes(SDL_Window* w)
{
    std::vector<Size> v;
    int n = 0;
    if (SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(displayOf(w), &n)) {
        for (int i = 0; i < n; ++i)
            if (modes[i] && modes[i]->w >= 640 && modes[i]->h >= 480)
                v.push_back({modes[i]->w, modes[i]->h});
        SDL_free(modes);
    }
    sortUnique(v);
    return v;
}

std::vector<Size> windowSizes(SDL_Window* w)
{
    static const Size kCommon[] = {{640, 480},   {800, 600},   {1024, 768},  {1280, 720},  {1280, 960},  {1366, 768},
                                   {1600, 900},  {1600, 1200}, {1920, 1080}, {2560, 1440}, {3200, 1800}, {3840, 2160}};
    SDL_Rect r{};
    if (!SDL_GetDisplayUsableBounds(displayOf(w), &r))
        r = {0, 0, 1280, 720 + 64};
    std::vector<Size> v;
    for (const Size& s : kCommon)
        if (s.w <= r.w && s.h + 32 <= r.h)   // (the title bar)
            v.push_back(s);
    return v;
}

void toggleFullscreen(SDL_Window* w)
{
    const ResolutionChoice& c = resolutionLive();
    if (windowFullscreen(w)) {
        leaveExclusive(w);
        if (SDL_GetWindowFlags(w) & SDL_WINDOW_BORDERLESS)
            toggleBorderlessFullscreen(w);   // back to the window it was
        else if (!(SDL_GetWindowFlags(w) & SDL_WINDOW_HIDDEN))
            fullscreenLive() = 0;
        sizeWindow(w, c.window);
    } else {
        toggleBorderlessFullscreen(w);
        if (c.exclusive)
            enterExclusive(w, c.fullscreen);
    }
}

bool applyResolution(SDL_Window* w)
{
    const ResolutionChoice& c = resolutionLive();
    if (!windowFullscreen(w)) {
        sizeWindow(w, c.window);
        return true;
    }
    if (!c.exclusive) {
        leaveExclusive(w);   // back to the borderless window at the desktop's size
        if (!(SDL_GetWindowFlags(w) & SDL_WINDOW_BORDERLESS)) {
            toggleBorderlessFullscreen(w);
        }
        return true;
    }
    if (!(SDL_GetWindowFlags(w) & SDL_WINDOW_BORDERLESS) && !windowExclusive(w))
        toggleBorderlessFullscreen(w);
    return enterExclusive(w, c.fullscreen);
}

int displayMode(SDL_Window* w)
{
    return windowExclusive(w) ? kExclusive : windowFullscreen(w) ? kBorderless : kWindowed;
}

bool setDisplayMode(SDL_Window* w, int mode)
{
    ResolutionChoice& c = resolutionLive();
    if (mode == kWindowed) {
        if (windowFullscreen(w))
            toggleFullscreen(w);   // (to the chosen window size, else the one it had)
        else
            sizeWindow(w, c.window);
        return true;
    }
    c.exclusive = mode == kExclusive;
    if (!windowFullscreen(w)) {
        toggleBorderlessFullscreen(w);
        return c.exclusive ? enterExclusive(w, c.fullscreen) : true;
    }
    return applyResolution(w);   // (borderless <-> exclusive, or the mode again)
}

} // namespace hotd::util

namespace hotd::util {

std::vector<DisplayInfo> displays()
{
    std::vector<DisplayInfo> v;
    int n = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&n);
    for (int i = 0; ids && i < n; ++i) {
        DisplayInfo d;
        d.id = ids[i];
        const char* name = SDL_GetDisplayName(ids[i]);
        d.key = name && *name ? name : "Display " + std::to_string(i + 1);
        int same = 0;
        for (const DisplayInfo& o : v)
            if (o.key == d.key || o.key.rfind(d.key + " #", 0) == 0)
                ++same;
        if (same > 0)
            d.key += " #" + std::to_string(same + 1);
        SDL_Rect r{};
        if (SDL_GetDisplayBounds(ids[i], &r))
            d.w = r.w, d.h = r.h;
        d.label = d.key + (d.w > 0 ? " (" + std::to_string(d.w) + " x " + std::to_string(d.h) + ")" : std::string());
        v.push_back(std::move(d));
    }
    SDL_free(ids);
    return v;
}

SDL_DisplayID displayByKey(const std::string& key)
{
    if (!key.empty())
        for (const DisplayInfo& d : displays())
            if (d.key == key)
                return d.id;
    return SDL_GetPrimaryDisplay();
}

std::string displayKeyOf(SDL_DisplayID id)
{
    for (const DisplayInfo& d : displays())
        if (d.id == id)
            return d.key;
    return "";
}

void moveToDisplay(SDL_Window* w, SDL_DisplayID d)
{
    if (!w || !d || SDL_GetDisplayForWindow(w) == d)
        return;
    const bool exclusive = windowExclusive(w), borderless = (SDL_GetWindowFlags(w) & SDL_WINDOW_BORDERLESS) != 0;
    leaveExclusive(w);
    if (borderless)
        toggleBorderlessFullscreen(w);   // (the window it was, on the old display)
    if (SDL_GetWindowFlags(w) & SDL_WINDOW_MAXIMIZED) {
        SDL_RestoreWindow(w);
        SDL_SyncWindow(w);
    }
    SDL_SetWindowPosition(w, int(SDL_WINDOWPOS_CENTERED_DISPLAY(d)), int(SDL_WINDOWPOS_CENTERED_DISPLAY(d)));
    SDL_SyncWindow(w);
    if (borderless || exclusive) {
        toggleBorderlessFullscreen(w);   // (the new display's bounds)
        if (exclusive)
            enterExclusive(w, resolutionLive().fullscreen);
    }
    SDL_Log("display: the window on %s", displayKeyOf(d).c_str());
}

} // namespace hotd::util

namespace hotd::util {
std::vector<Size> sizesOfShape(const std::vector<Size>& all, SDL_Window* w, float aspect, Size keep)
{
    float desk = 0;
    if (const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(displayOf(w)); dm && dm->h > 0)
        desk = float(dm->w) / float(dm->h);
    std::vector<Size> v;
    for (const Size& s : all) {
        const float r = s.h > 0 ? float(s.w) / float(s.h) : 0.0f;
        const bool shape = (desk > 0 && std::fabs(r - desk) <= desk * 0.02f) || (aspect > 0 && std::fabs(r - aspect) <= aspect * 0.02f);
        if (shape || s == keep || s.automatic())
            v.push_back(s);
    }
    return v;
}
} // namespace hotd::util
