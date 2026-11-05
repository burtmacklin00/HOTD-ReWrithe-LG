// gamma (GitHub issue #23, 2026-11-03): Options > Video > Gamma, a power curve over the final picture (launcher.ini
// gamma=, --gamma). Players of the real cabinets say the game is too dark, and it is darker than they remember in MAME
// too: something in the arcade's chain was brighter than MAME's output curve (docs/formats.md, "Why the picture is
// darker than the cabinets"). out = in ^ (1 / gamma) per channel: 1.0 = the picture unchanged (nothing is drawn, every
// pixel as before), above 1 the dark and middle tones open up while black stays black and white stays white (not a
// flat brightness lift).
//
// One pass over the finished frame (the 3D of either model set, the 2D layer, the HUD, the menus, after the CRT filter),
// so the renderer's shaders are untouched: the frame is copied into a texture and drawn back through the curve. The
// VR eyes get the same pass on each eye's image (vr_scene.cpp), screenshots read back after it or through gammaImage.
#pragma once

#include <cstdint>
#include <vector>

namespace hotd {

constexpr int kGammaMin = 60, kGammaMax = 200, kGammaStep = 5, kGammaDefault = 100;   // hundredths (0.60 .. 2.00)

// the gamma in effect now, in hundredths (the frames set it from their LaunchConfig: the menu's while the menu runs)
int gamma100();
void setGamma100(int g);
inline bool gammaOn() { return gamma100() != kGammaDefault; }
// a value as read (launcher.ini / --gamma: "1.4", "1.35"; 140 also taken as hundredths) -> hundredths on the 0.05
// steps, clamped; -1: not a number
int gammaParse(const char* s);
// "1.40"
const char* gammaText(int g100, char (&buf)[8]);

// the picture in framebuffer fbo (w x h) through the curve, in place (nothing at 1.0). Leaves fbo bound with its
// viewport, depth test / blending / scissor off, no program, texture unit 0 active
void gammaApply(unsigned fbo, int w, int h);
// a picture on the CPU (RGB or RGBA bytes: channels 3 / 4; alpha kept), as gammaApply does it
void gammaImage(std::vector<uint8_t>& px, int channels = 3);

} // namespace hotd
