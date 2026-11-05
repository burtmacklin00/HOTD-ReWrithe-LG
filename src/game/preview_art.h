// menus3 (user 2026-10-28): the settings' pictures (Options > Video > Models, Gameplay > Blood Color). Moments of the
// attract, rendered by the game itself in-process from the player's own files (main.cpp prvFrame) and kept in the
// settings folder as "bases" (cropped, nothing drawn on them). Blood Color: the story's frame kPreviewFrame (the two-axe
// zombie coming out of the door, the researchers running from him). Models (prevbridge): Chapter 1's frame
// kPreviewBridgeFrame (the bridge). Every picture the pages show is a base with the player 1 crosshair on the zombie
// and, for Blood Color, a hit's blood burst there in the colour: drawn here on the CPU (a few ms), so the Custom colour
// is a live picture with no game run at all. Nothing of it is in the package.
#pragma once

#include "game/sights.h"

#include <cstdint>
#include <vector>

namespace hotd::game {

constexpr int kPreviewFrame = 1065;                          // Blood Color's attract frame (its story: mode 9)
// prevbridge (user 2026-10-28: "the Chapter 1 bridge where the strong zombie carries the researcher"): Video > Models'
// frame of Chapter 1 (--chapter 1 --lives-kept 1, its frame count from the chapter's start): the big zombie on the
// bridge with the researcher over his head, whole, the mansion's entrance behind (arcade and PC models alike). The
// --autofire player clears the way there and stops before the carry (it would shoot him: MISS SHOT)
constexpr int kPreviewBridgeFrame = 4320;
constexpr int kPreviewBridgeFireUntil = 4200;  // (the --autofire player before it, from the frog on the fountain; none after)
constexpr int kPreviewRenderW = 960, kPreviewRenderH = 540;  // rendered at
constexpr int kPreviewPicW = 480, kPreviewPicH = 270;        // kept / shown at
// fxux (user 2026-11-03: "add a preview picture per setting, from real game pictures"): Video > Hit Marks' and
// Transparency's pictures, from the same Chapter 1 run as the bridge's (the --autofire player, frames from the
// chapter's start): kPreviewMarksFrame, player 1 just hit (the 2D marks over the zombie's face, drawn once per choice);
// kPreviewTranspFrame, the blood pools on the bridge to the mansion (a second run, the --autofire player all the way) (the 3D drawn once per Transparency mode, the arcade
// models only: the PC's have no see-through edges). Their cuts are 1:1 (marks) and 3x (pools) so the dots show
constexpr int kPreviewMarksFrame = 1992;
constexpr int kPreviewTranspFrame = 9066;
enum PreviewKind { kPreviewBlood = 0, kPreviewModels = 1, kPreviewMarks = 2, kPreviewTransp = 3 };  // which base (frame, cut, aim)

struct PreviewImg {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
};

// the render (kPreviewRenderW x kPreviewRenderH RGB rows, top first) cut to the kind's zombie and researchers and
// scaled to kPreviewPicW x kPreviewPicH RGBA
PreviewImg previewCrop(const std::vector<uint8_t>& rgb, int w, int h, int kind);
// fxux: the marks' / the pools' cut (kPreviewMarks / kPreviewTransp): kPreviewPicW x kPreviewPicH, nearest pixels
PreviewImg previewCropFx(const std::vector<uint8_t>& rgb, int w, int h, int kind);
// the blood colour of a Blood Color value (0-4 the presets as deep as the red; 5 custom: customRgb 0xRRGGBB)
uint32_t previewBloodRgb(int blood, int customRgb);
// the shot: a blood burst in rgb (0: none) and the sight (art.w 0: none) on the kind's zombie
PreviewImg previewCompose(const PreviewImg& base, uint32_t bloodRgb, const SightArt& sight, int kind);

} // namespace hotd::game
