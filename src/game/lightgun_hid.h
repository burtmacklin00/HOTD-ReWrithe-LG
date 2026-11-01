// Light Gun Edition: LightgunHID.ini, the players' light guns by hardware ID, in the game's folder (next to
// "The House of the Dead ReWrithe.exe", where "1 - Light Gun Setup.exe" writes it too). Only the guns' IDs are in it:
//
//   [Lightguns]
//   P1_Lightgun_HID = VID_1209&PID_0001
//   P2_Lightgun_HID = VID_1209&PID_0002
//   P3_Lightgun_HID = ...           (only with a third / fourth gun: the 4-player mode)
//   P4_Lightgun_HID = ...
//
// A value is any part of the gun's Windows device ID (case does not matter); "1209:0001" is read as VID_1209&PID_0001.
// Two guns with the same ID: the lower player takes the first one connected, the next player the second.
//
// At start-up the IDs give each player their own gun (launcher.ini gun1dev= ..., the player's input set to Light Gun);
// a blank file (or none) starts the in-game set-up: each player pulls their gun's trigger, and the file is written.
#pragma once

#include <string>
#include <vector>

namespace hotd::game {

struct LightgunHid {
    static constexpr int kPlayers = 4;
    std::string id[kPlayers];   // as written in the file (normalised); empty: that player has no gun set

    bool load(const std::string& file);         // false: no file
    bool save(const std::string& file) const;   // keeps the file's other lines; P3 / P4 written only when set
    bool blank() const;                         // no gun set at all

    // "VID_XXXX&PID_XXXX" from a raw-input device path (\\?\hid#vid_1209&pid_0001&mi_00#...), else ""
    static std::string idOfPath(const std::string& path);
    // "1209:0001" / "0x1209:0x0001" -> "VID_1209&PID_0001"; else the value trimmed, upper case
    static std::string normalise(const std::string& value);
    // each player's connected device path (empty: none set, or not connected): for every player with an ID, the first
    // connected path containing it that no lower player took
    std::vector<std::string> resolve(const std::vector<std::string>& paths) const;
};

// The raw-input mice connected now (light guns report as mice): their device paths, lower case, sorted (a steady order
// for two identical guns). Empty off Windows.
std::vector<std::string> rawMousePaths();

} // namespace hotd::game
