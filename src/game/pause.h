// The PC port's in-game pause (THOTD.EXE 1998; docs/teaser_status/pause.md). One routine per original function:
//   0x41ADF0 pause_frame     every frame of the main loop: in play, Escape / P opens the pause; while paused the
//                            menu's keys (up / down, Enter / keypad Enter / Space / a mouse button, mouse motion)
//   0x41ACA8 pause_enter     the pause's start (also message 0xF "inactive": the window lost the focus)
//   0x41ACF0 pause_leave     the pause's end (also message 0xF "active": the window got the focus back)
//   0x41AD34 pause_exit      the menu's Exit: the game ends, frontend_state 7 (the main menu)
//   0x41AB7C pause_draw      Hud::drawPause: PAUSED, Continue, Exit (the selected one in the highlight font)
// State: 0x5CD358 paused, 0x5CE844 the selection (0 Continue, 1 Exit), 0x5CE848 the play-time clock was running
// (0x412B14 stops it, 0x412B00 restarts it: the boss modes' timer, not ported).
// The sound: pause_enter calls the CD pause 0x565BE0 and posts 0x418 to the sound window (every DirectSound buffer
// that plays is stopped: IDirectSoundBuffer::Stop keeps its position); pause_leave calls the CD resume 0x565B90 and
// posts 0x419 (those buffers Play again: they go on from where they stopped). Ours: Audio::pause / resume hold the
// voices and the music in place, nothing is cued again (patches 6 / 8 in C:\Games\HOTD\PATCHES.md: the user's
// game resumes only the track that played; with ogg-winmm a resumed song started from the top, ours goes on).
//
// UNVERIFIED (teaser): from the decompilation and the disassembly; not compared with a PC harness capture.
#pragma once

#include <functional>

namespace hotd::game {

class Runtime;

// one frame of the player's keys / mouse for the pause (edges since the last frame)
struct PauseInput {
    bool toggle = false;     // an Escape (DIK 0x01) or P (DIK 0x19) edge
    bool up = false;         // an up-arrow (0xC8) or keypad 8 (0x48) edge
    bool down = false;       // a down-arrow (0xD0) or keypad 2 (0x50) edge
    bool activate = false;   // an Enter (0x1C) / keypad Enter (0x9C) / Space (0x39) edge or a mouse button (0x12FDB84)
    float mouseDy = 0;       // the mouse's vertical motion this frame (0x12FDB9C, DirectInput: mickeys; ours: pixels)
    // ours (user 2026-10-17: the PC's mouse rule alone felt odd): the item under the pointer (its index in the pause's
    // items, -1 none) and whether the pointer moved this frame; pointing at an item selects it, a click there chooses it
    int hover = -1;
    bool pointerMoved = false;
};

class Pause {
public:
    bool paused() const { return paused_; }
    int selection() const { return selection_; }

    std::function<void(int cue)> onCue;          // the PC's sound_cue (0x412000): 0x10B the selection moved, 0x10C Continue
    std::function<void(bool hold)> onSound;      // true: 0x565BE0 + 0x418 (hold); false: 0x565B90 + 0x419 (go on)
    std::function<void()> onExit;                // 0x41AD34: the game ends, the main menu (the host leaves the game)

    // 0x41ADF0; rt: the pause opens only in play (game mode 4, shot_enable, the screen fade at 1.0: 0x41B204)
    void frame(const Runtime& rt, const PauseInput& in);
    void enter();                 // 0x41ACA8
    void leave();                 // 0x41ACF0
    void exit();                  // 0x41AD34
    // message 0xF (voice_message_frame_sub 0x4185BC; posted by 0x5633D0 from the window procedure's WM_ACTIVATEAPP):
    // inactive -> the pause with Continue selected; active -> the pause ends (whatever opened it)
    void focus(bool active);
    void reset() { paused_ = false; selection_ = 0; over_ = false; }
    // ours (user 2026-10-26): the pause opens in a cut-scene too (shot_enable 0; the PC's 0x41ADF0 only with the shots
    // on). The host sets it for local games (online the PCs' lockstep keeps the original rule)
    bool cutScenes = false;   // a new game (0x4185BC message 0 clears 0x5CD358)

    // GitHub #5 (user 2026-11-03: "Level retry ... from the pause menu like in the remake"): Gameplay > Chapter Retry on
    // (the host sets `retry` for the games that can retry): the pause has Continue, Retry Chapter, Exit; after a game
    // over openGameOver() shows GAME OVER with Retry Chapter, Exit (Escape / P do not close it). Off: the PC's pause
    enum Item { kContinue, kExit, kRetry, kSettings, kPhoto };
    bool retry = false;
    std::function<void()> onRetry;               // Retry Chapter chosen (the host restarts the chapter)
    void openGameOver();
    bool gameOver() const { return paused_ && over_; }
    // fxux (user 2026-11-03): Settings right below Continue (the host sets `settings`): the Options pages over the paused
    // game (onSettings; the pause stays open, Back returns to it). The pause's items: Continue, Settings, Photo Mode
    // (photomode), Retry Chapter (#5), Exit; the game over's: Retry Chapter, Exit
    bool settings = false;
    std::function<void()> onSettings;
    // photomode (game/photo_mode.h): Photo Mode after Settings (the host sets `photo` for local games and online,
    // where `photoGrey` greys it: chosen there it only calls onPhoto, which shows why). The pause stays open
    // underneath (Photo Mode left: the pause again)
    bool photo = false, photoGrey = false;
    std::function<void()> onPhoto;
    int itemCount() const { return over_ ? 2 : 2 + (retry ? 1 : 0) + (settings ? 1 : 0) + (photo ? 1 : 0); }
    Item itemAt(int i) const
    {
        if (over_)
            return i == 0 ? kRetry : kExit;
        if (i == 0)
            return kContinue;
        int k = 1;
        if (settings && i == k++)
            return kSettings;
        if (photo && i == k++)
            return kPhoto;
        return retry && i == k ? kRetry : kExit;
    }

private:
    bool paused_ = false;   // 0x5CD358
    int selection_ = 0;     // 0x5CE844
    bool over_ = false;     // (#5: the game over's menu)
};

// 0x41B204: the screen fade is at rest at 1.0 (0xD5A5C0 +0x64 == +0x68 == 0x10000)
bool pause_fade_at_rest(const Runtime& rt);

} // namespace hotd::game
