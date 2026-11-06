// Borderless fullscreen shared by the game and the viewer (F11 / Alt+Enter; user 2026-10-19: both start this way).
// The window is one pixel taller than its display so Windows does not treat it as exclusive fullscreen (OBS window
// capture keeps working), see toggleBorderlessFullscreen. bugs4 (GitHub #20): with Video > Variable Refresh Rate on it
// covers the display exactly (borderlessExact): the driver's fullscreen flip, which G-Sync / FreeSync need.
#pragma once
#include <SDL3/SDL.h>

namespace hotd::util {

inline bool isFullscreenKey(const SDL_Event& e)
{
    return e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
           (e.key.key == SDLK_F11 || ((e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) && (e.key.mod & SDL_KMOD_ALT)));
}

// -1 until the player toggles a visible window; then 0 / 1: the state the game keeps for the next start (launcher.ini fullscreen=)
inline int& fullscreenLive() { static int v = -1; return v; }

// bugs4 (#20): Borderless exactly the display's size (Variable Refresh Rate on). Off (the default): one pixel taller.
// The extra row was added (89f861fc, 2026-10-16) because a window covering the display exactly is promoted by the GPU
// driver to independent flip (no desktop compositor) and screen recorders' window capture (OBS) captured nothing; the
// same promotion is what lets G-Sync / FreeSync engage in a borderless window, so VRR needs the exact size
inline bool& borderlessExact() { static bool v = false; return v; }

inline void toggleBorderlessFullscreen(SDL_Window* window)
{
    static int sx = SDL_WINDOWPOS_CENTERED, sy = SDL_WINDOWPOS_CENTERED, sw = 1280, sh = 720;
    const bool on = (SDL_GetWindowFlags(window) & SDL_WINDOW_BORDERLESS) != 0;
    if (!on) {
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) {
            SDL_RestoreWindow(window);
            SDL_SyncWindow(window);
        }
        SDL_GetWindowPosition(window, &sx, &sy);
        SDL_GetWindowSize(window, &sw, &sh);
        SDL_Rect r{};
        if (!SDL_GetDisplayBounds(SDL_GetDisplayForWindow(window), &r)) return;
        SDL_SetWindowBordered(window, false);
        SDL_SetWindowPosition(window, r.x, r.y);
        SDL_SetWindowSize(window, r.w, r.h + (borderlessExact() ? 0 : 1));
        SDL_RaiseWindow(window);
    } else {
        SDL_SetWindowBordered(window, true);
        SDL_SetWindowSize(window, sw, sh);
        SDL_SetWindowPosition(window, sx, sy);
    }
    SDL_SyncWindow(window);
    if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN))
        fullscreenLive() = on ? 0 : 1;
}

// bugs4 (#20): borderlessExact changed: a Borderless window (not exclusive fullscreen) to its new height now
inline void setBorderlessExact(SDL_Window* window, bool exact)
{
    if (borderlessExact() == exact)
        return;
    borderlessExact() = exact;
    const SDL_WindowFlags f = SDL_GetWindowFlags(window);
    if (!(f & SDL_WINDOW_BORDERLESS) || (f & SDL_WINDOW_FULLSCREEN))
        return;
    SDL_Rect r{};
    if (!SDL_GetDisplayBounds(SDL_GetDisplayForWindow(window), &r))
        return;
    SDL_SetWindowPosition(window, r.x, r.y);
    SDL_SetWindowSize(window, r.w, r.h + (exact ? 0 : 1));
    SDL_SyncWindow(window);
}

} // namespace hotd::util
