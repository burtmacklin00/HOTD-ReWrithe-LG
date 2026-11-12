// The resolution choice (user 2026-10-21: Options > Video > Resolution). Display Mode (rescrash, 2026-10-29):
// Borderless = the desktop's size (util/fullscreen.h: the window one pixel taller than its display); Fullscreen = one
// of the display's modes, exclusive (Auto: the desktop's). In a window: Auto = the window as the player sizes it, or a common size that fits
// the desktop. launcher.ini resolution_fullscreen= / resolution_window= ("auto" or WxH). The picture is drawn at the
// window's size as before (render scale / internal resolution unchanged).
#pragma once
#include <SDL3/SDL.h>

#include <string>
#include <vector>

namespace hotd::util {

struct Size {
    int w = 0, h = 0;   // 0 x 0: Auto
    bool automatic() const { return w <= 0 || h <= 0; }
    bool operator==(const Size& o) const { return w == o.w && h == o.h; }
};

// the game's choice (the window code reads it: F11, the start); set from LaunchConfig. rescrash (Video > Display
// Mode): exclusive = Fullscreen (exclusive, in `fullscreen`'s display mode; Auto: the desktop's), else fullscreen is
// Borderless (a window filling the display at the desktop's size; `fullscreen` not used)
struct ResolutionChoice {
    Size fullscreen, window;
    bool exclusive = false;
};
ResolutionChoice& resolutionLive();

// Video > Display Mode (launcher.ini displaymode=): the window, borderless at the desktop's size, exclusive fullscreen
enum DisplayMode { kWindowed = 0, kBorderless = 1, kExclusive = 2 };
int displayMode(SDL_Window* w);               // the window's now
// into that mode now (from any other; the choice's sizes). A hidden window (the tests) never changes the display's
// mode: exclusive is only noted there. false: the display mode could not be set (borderless instead)
bool setDisplayMode(SDL_Window* w, int mode);

std::string sizeKey(Size s);                  // "auto" / "1920x1080" (launcher.ini)
Size parseSize(const std::string& v);         // "1920x1080" -> 1920 x 1080; anything else: Auto
std::string sizeName(Size s);                 // "Auto" / "1920 x 1080" (the menu)

// deckfs (GitHub #74, a Steam Deck: "Fullscreen ... remains in borderless", shots near the edges reloaded): the game
// runs under gamescope (Steam Deck Game Mode / Big Picture on SteamOS): the compositor shows the focused window scaled
// to its screen, whatever the window's size and place, so a display mode of our own means nothing there and the
// display's bounds do not say which part of the window is seen (all of it is). XDG_CURRENT_DESKTOP=gamescope or
// GAMESCOPE_WAYLAND_DISPLAY; HOTD_GAMESCOPE=1 / 0 (testing) over them. Windows: never
bool underGamescope();
// deckfs: Fullscreen without a display mode of our own (the window manager's / compositor's fullscreen at the desktop's
// size: under gamescope, or no display mode of the chosen size on Linux / macOS)
bool windowFullscreenDesktop(SDL_Window* w);
// deckfs: one log line of the window's state (the video driver, gamescope, the window's size and place, its pixels,
// the display's bounds and desktop mode, the fullscreen flags): at the start and on a Display Mode change
void logWindowState(SDL_Window* w, const char* when);
// deckfs: the part of a window (lw x lh at wx, wy) its display (d) shows, in window coordinates: the window's
// intersection with the display, unless `whole` (fullscreen / gamescope: the whole window is the picture) or the
// window is mostly off its display (then the window's own edges)
SDL_Rect visibleArea(const SDL_Rect& d, int wx, int wy, int lw, int lh, bool whole);

bool windowExclusive(SDL_Window* w);          // exclusive fullscreen (a display mode)
bool windowFullscreen(SDL_Window* w);         // borderless or exclusive fullscreen
// the display's fullscreen modes (SDL_GetFullscreenDisplayModes), one per size (the refresh rates folded), at least
// 640 x 480, smallest first
std::vector<Size> fullscreenSizes(SDL_Window* w);
// the common window sizes that fit the display's usable area (the taskbar and the title bar left free), smallest first
std::vector<Size> windowSizes(SDL_Window* w);
// menufb (user 2026-10-26: "too many different resolutions in different aspect ratios ... it should rely on the aspect
// ratio"): of `all`, the sizes the shape of the display's desktop or of `aspect` (width / height; 0 none: the Aspect
// setting's 4:3 / 16:10), within 2 %; `keep` (the current one) stays in whatever its shape
std::vector<Size> sizesOfShape(const std::vector<Size>& all, SDL_Window* w, float aspect, Size keep);

// F11 / Alt+Enter: into fullscreen as chosen (Borderless, or Fullscreen in its display mode), out of it to the window
// (the chosen window size, else the one it had)
void toggleFullscreen(SDL_Window* w);
// the current mode's choice now (a menu change): borderless / exclusive in the display mode; a window = its size,
// centred on its display. false: the display mode could not be set (the window stays borderless)
bool applyResolution(SDL_Window* w);

} // namespace hotd::util

namespace hotd::util {
// remakefix (user 2026-10-22 "you don't account for which monitor the player wants to play on"): the displays
// (SDL_GetDisplays): key = the display's name (" #2", " #3" for more of the same name; launcher.ini display=), label =
// the key and its desktop size (the menu's value), the primary first as SDL lists them
struct DisplayInfo {
    SDL_DisplayID id = 0;
    std::string key, label;
    int w = 0, h = 0;
};
std::vector<DisplayInfo> displays();
SDL_DisplayID displayByKey(const std::string& key);   // not found / empty: the primary display
std::string displayKeyOf(SDL_DisplayID id);           // "" when unknown
// the window onto display d: a window centred on it, fullscreen (borderless or a display mode) on it again
void moveToDisplay(SDL_Window* w, SDL_DisplayID d);
} // namespace hotd::util
