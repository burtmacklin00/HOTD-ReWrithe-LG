#include "game/photo_mode.h"

#include "game/keybinds.h"

#include <cmath>

namespace hotd::game {

const char* PhotoMode::blocked(bool online, bool vr)
{
    if (online)   // (the lockstep: the other PC's game goes on, this one cannot stand still)
        return "Photo Mode is for offline games only";
    if (vr)       // (the headset is the camera)
        return "Not available in VR";
    return nullptr;
}

void PhotoMode::enter(const Camera& gameCam, bool fromPauseMenu)
{
    on = true;
    fromPause = fromPauseMenu;
    hudHidden = false;
    start_ = gameCam;
    cam.start(gameCam);
    noteUntil_ = 0;
}

void PhotoMode::leave()
{
    on = false;
    cam.stop();
    noteUntil_ = 0;
}

int PhotoMode::fovDegrees() const
{
    return int(std::lround(cam.camera.fovY * 180.0f / 3.14159265f));
}

std::vector<std::string> PhotoMode::helpLines() const
{
    std::vector<std::string> v;
    v.push_back((cam.slow ? "PHOTO MODE  FOV {n}  SLOW CAM" : "PHOTO MODE  FOV {n}"));
    const std::string n = std::to_string(fovDegrees());
    v[0].replace(v[0].find("{n}"), 3, n);
    if (padType >= 0) {   // (the pad's own button names: keybinds.h padInputName)
        auto b = [this](int in) { return padInputName(in, padType); };
        v.push_back("Sticks move / look  " + b(kPiL1) + " / " + b(kPiR1) + " down / up  D-pad up / down FOV  " + b(kPiR3) +
                    " reset FOV  " + b(kPiWest) + " reset camera");
        v.push_back(b(kPiBack) + " slow cam  " + b(kPiNorth) + " hide HUD  " + b(kPiSouth) + " screenshot  " + b(kPiEast) + " exit");
    } else {
        v.push_back("WASD QE move  Shift / Ctrl speed  drag look  wheel or + / - FOV  0 reset FOV");
        v.push_back("R reset camera  C slow cam  H hide HUD  F12 screenshot  F8 / Esc exit");
    }
    return v;
}

} // namespace hotd::game
