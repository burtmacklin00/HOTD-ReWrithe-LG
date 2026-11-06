// VR mode: the settings file and the backend choice (vr.h)
#include "vr/vr.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace hotd::vr {

namespace {
std::string trim(const std::string& s)
{
    const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
bool truthy(const std::string& v) { return v == "1" || v == "on" || v == "true" || v == "yes"; }
void three(const std::string& v, float out[3])   // "x y z" (a value that does not read keeps the old three)
{
    float a = 0, b = 0, c = 0;
    if (std::sscanf(v.c_str(), "%f %f %f", &a, &b, &c) == 3)
        out[0] = a, out[1] = b, out[2] = c;
}
} // namespace

bool Settings::load(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
        return false;
    std::string line, section;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;
        if (line[0] == '[') {
            section = trim(line.substr(1, line.find(']') - 1));
            continue;
        }
        if (section != "vr")
            continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        const float f = float(std::atof(v.c_str()));
        if (k == "follow") follow = v == "level" ? 1 : v == "follow" ? 0 : std::atoi(v.c_str());
        else if (k == "vignette") vignette = truthy(v);
        else if (k == "vignette_strength") vignetteStrength = f;
        else if (k == "vignette_turn") vignetteTurn = f;
        else if (k == "units_per_metre") unitsPerMetre = f > 0 ? f : unitsPerMetre;
        else if (k == "panel_distance") panelDistance = f > 0.2f ? f : panelDistance;
        else if (k == "panel_depth_test") panelDepthTest = truthy(v);
        else if (k == "aim_distance") aimDistance = f > 0.5f ? f : aimDistance;
        else if (k == "laser") laser = truthy(v);
        else if (k == "dual_pistols") dualPistols = truthy(v);
        else if (k == "crosshair_degrees") crosshairDegrees = f > 0 ? f : crosshairDegrees;
        else if (k == "reload_down") reloadDown = truthy(v);
        else if (k == "reload_pitch") reloadPitch = f;
        else if (k == "left_hand") leftHand = truthy(v);
        else if (k == "gun_offset") three(v, gunOffset);
        else if (k == "gun_rotate") three(v, gunRotate);
        else if (k == "left_hand_offset") three(v, leftOffset);
        else if (k == "left_hand_rotate") three(v, leftRotate);
        else if (k == "ipd") ipd = f > 0.03f && f < 0.09f ? f : ipd;
        else if (k == "menu_panel") menuPanel = v == "auto" ? menuPanel : truthy(v);   // quest (docs/quest.md)
        else if (k == "menu_distance") menuDistance = f > 0.5f ? f : menuDistance;
        else if (k == "menu_width") menuWidth = f > 0.3f ? f : menuWidth;
        else if (k == "resolution_scale") resolutionScale = std::clamp(f > 0 ? f : 1.0f, 0.5f, 1.5f);
        else if (k == "hud_lines") hudLines = std::clamp(std::atoi(v.c_str()), 360, 1440);
        else if (k == "arcade_sort") arcadeSort = v == "auto" ? arcadeSort : truthy(v);
        else if (k == "srgb_swapchain") srgbSwapchain = truthy(v);
    }
    return true;
}

std::unique_ptr<Backend> make(const std::string& name)
{
    if (name == "preview")
        return makePreview();
    if (name == "openxr")
        return makeOpenXr();
    return nullptr;
}

#ifndef HOTD_OPENXR
std::unique_ptr<Backend> makeOpenXr() { return nullptr; }   // (built without the OpenXR SDK: vr_openxr.cpp)
#endif

} // namespace hotd::vr
