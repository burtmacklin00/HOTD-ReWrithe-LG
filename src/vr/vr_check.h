// vrship (user 2026-11-09): --vr-check: what the PC's OpenXR runtime offers, without starting the game (docs/vr.md
// "VR check"): the active runtime (its registry entry and name / version), the other runtimes installed, API layers,
// the extensions, the headset, the eyes' sizes, the swapchain formats, which controller profiles take the game's
// bindings, the refresh rates; one verdict line. Every line goes to the console and hotd.log (SDL_Log).
#pragma once

namespace hotd::vr {

// 0: VR can start with this runtime; 1: no runtime / loader; 2: no headset; 3: a later step failed (the lines say)
int checkOpenXr();

} // namespace hotd::vr
