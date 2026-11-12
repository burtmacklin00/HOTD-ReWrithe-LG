// Photo Mode (ours, worker photomode 2026-11-09; the idea from Tenome's MIT-licensed "Typing of the Dead EX" community
// patch for HOTD2's Typing of the Dead, https://github.com/Tenome/Typing-of-the-Dead-EX: idea only, no code; credited
// in CREDITS.txt). In any local game (Arcade, PC Mode, Boss Mode, Survival, The Typing of the Dead 0, ...) F8 or the
// pause menu's "Photo Mode" freezes the game completely and lets the player look around with a free camera (the cut-
// scene viewer's, game/free_cam.h, drawn through GameView::camOverride), change the field of view, hide the HUD and
// save full-size screenshots (F12's path). Leaving it the game goes on exactly where it was.
//
// The freeze is the pause's: main.cpp treats Photo Mode as paused (`paused`), so no game tick runs, the sound is held
// in place (Audio::pause), the hold-to-skip clock, the pads' aim and the triggers wait as in the pause; nothing here
// writes game memory, the random numbers or any timer (--photo-test checks the state hash before / after and the
// chapter's end against a plain run). Offline only: online the lockstep's other PC goes on (blocked() says why).
#pragma once

#include "game/free_cam.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hotd::game {

class PhotoMode {
public:
    bool on = false;
    bool fromPause = false;   // entered from the pause menu: leaving shows the pause again (the game stays paused)
    bool hudHidden = false;   // H / a pad's Y: no HUD, sights, captions or help line (the next entry: shown again)
    FreeCam cam;
    int padType = -1;         // the help line names a pad's buttons (the type of the pad used last), -1 the keys

    // why Photo Mode cannot start now (the note shown, an English key of data/lang), null: it can
    static const char* blocked(bool online, bool vr);
    // on, the free camera at the game's camera as last drawn
    void enter(const Camera& gameCam, bool fromPauseMenu);
    void leave();
    void resetFov() { cam.camera.fovY = start_.fovY; }
    void resetCamera() { cam.start(start_); }
    int fovDegrees() const;
    const Camera* camNow() const { return on ? &cam.camera : nullptr; }
    // the help lines (English: Overlay translates them; patterns with {placeholders} in data/lang)
    std::vector<std::string> helpLines() const;
    // a short note under the help line (also with the HUD hidden; never in a screenshot): ms from SDL_GetTicks
    void note(const std::string& text, uint64_t nowMs, uint64_t forMs = 2000)
    {
        note_ = text;
        noteUntil_ = nowMs + forMs;
    }
    std::string noteNow(uint64_t nowMs) const { return nowMs < noteUntil_ ? note_ : std::string(); }

private:
    Camera start_;   // the game's camera at the entry (R: back there; the field of view's reset)
    std::string note_;
    uint64_t noteUntil_ = 0;
};

} // namespace hotd::game
