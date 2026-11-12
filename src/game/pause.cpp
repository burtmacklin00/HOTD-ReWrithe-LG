// The PC port's in-game pause (pause.h). UNVERIFIED (teaser): every routine here (no capture proof yet).
#include "game/pause.h"

#include "game/runtime.h"

#include <cmath>

namespace hotd::game {

using G = A::G;

// ---- 0x41B204 ----
// The PC's screen fade record (0xD5A5C0: +0x64 the level, +0x68 the target, 16.16) is Runtime::pcFade here.
bool pause_fade_at_rest(const Runtime& rt)
{
    return rt.pcFade.to == 1.0f && std::fabs(rt.pcFadeLevel() - 1.0f) < 1e-4f;
}

// ---- 0x41ACA8 ----
void Pause::enter()
{
    selection_ = 0;
    paused_ = true;
    // (0x412BB4 / 0x412B14 / 0x5CE848: the play-time clock stops; not ported)
    if (onSound)
        onSound(true);   // 0x565BE0 (the CD's pause) + message 0x418 (the effects' buffers stop)
}

// ---- 0x41ACF0 ----
void Pause::leave()
{
    paused_ = false;
    selection_ = 0;
    // (0x5CE848 / 0x412B00: the play-time clock goes on; not ported)
    if (onSound)
        onSound(false);   // 0x565B90 (the CD's resume) + message 0x419 (the effects' buffers play on)
}

// ---- 0x41AD34 ----
void Pause::exit()
{
    // frontend_state = 7 (the menus), 0x5CE668 = 1; 0x565D20 stops the CD; 0x419 then 0x407: the held effects are
    // released and every effect stops; 0x5CE650 = 0; the play-time clock stopped and cleared (0x412B14 / 0x412B40 /
    // 0x412C34). Ours: the host leaves the game (Audio::silence) and goes to the main menu.
    paused_ = false;
    selection_ = 0;
    if (onExit)
        onExit();
}

// ours (#5): the game over's Retry Chapter / Exit (no sound held: the game over has ended)
void Pause::openGameOver()
{
    paused_ = over_ = true;
    selection_ = 0;
}

// ---- 0x4185BC, message 0xF ----
void Pause::focus(bool active)
{
    if (over_)
        return;   // (#5: the game over's menu stays until a choice)
    if (!active)
        enter();   // the same lines as 0x41ACA8 (inline in 0x4185BC)
    else
        leave();   // the same lines as 0x41ACF0
}

// ---- 0x41ADF0 ----
// (the light gun's buttons, 0x5CD354 with gamebios 0xC721C0 / 0xC721D8, are left out: patch 3 turns the gun off)
void Pause::frame(const Runtime& rt, const PauseInput& in)
{
    const int was = selection_;
    if (!paused_) {
        if (rt.mem.at<int16_t>(G::game_mode) != 4)   // (0xDA0CDC)
            return;
        if (rt.mem.u32(G::shot_enable) == 0 && !cutScenes)
            return;
        if (!pause_fade_at_rest(rt))
            return;
        if (!in.toggle)
            return;
        enter();   // the same lines as 0x41ACA8 (inline here)
        return;
    }
    // the mouse's vertical motion picks the item directly: up past 15 Continue, down past 15 Exit
    const int last = itemCount() - 1;   // (1; #5 with Retry Chapter: 2)
    if (in.mouseDy < -15)
        selection_ = 0;
    else if (in.mouseDy > 15)
        selection_ = last;
    // ours: the pointer on an item selects it (when it moved, so the arrow keys still work with the pointer at rest),
    // and a click on an item chooses that item
    if (in.hover >= 0 && in.hover <= last && (in.pointerMoved || in.activate))
        selection_ = in.hover;
    if (in.down) {
        if (++selection_ > last)
            selection_ = 0;
    } else if (in.up) {
        if (--selection_ < 0)
            selection_ = last;
    } else if (in.activate) {
        if (itemAt(selection_) == kRetry) {   // (#5)
            const bool held = !over_;
            paused_ = over_ = false;
            selection_ = 0;
            if (held && onSound)
                onSound(false);
            if (onRetry)
                onRetry();
            return;
        }
        if (itemAt(selection_) == kSettings) {   // (fxux: the pause stays; the host opens the Options pages)
            if (onCue)   // relfix2 (user 2026-11-03: Settings made no sound): Continue's confirm (0x10C)
                onCue(0x10C);
            if (onSettings)
                onSettings();
            return;
        }
        if (itemAt(selection_) == kPhoto) {   // (photomode: the pause stays under Photo Mode; greyed: only the note)
            if (onCue && !photoGrey)
                onCue(0x10C);
            if (onPhoto)
                onPhoto();
            return;
        }
        if (itemAt(selection_) != kContinue) {
            if (itemAt(selection_) == kExit) {
                over_ = false;
                exit();
            }
            return;   // (0x41B04C with the selection unchanged: no cue)
        }
        if (onCue)
            onCue(0x10C);
        leave();
    } else if (in.toggle && !over_) {
        leave();   // Escape / P again: the pause ends (no cue)
    }
    if (selection_ != was && onCue)   // (also after Escape / P with Exit selected, as the original)
        onCue(0x10B);
}

} // namespace hotd::game
