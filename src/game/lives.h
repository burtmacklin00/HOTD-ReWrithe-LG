// GitHub #30 / #25 (user 2026-11-03): Starting Lives and Max Lives as two settings. The arcade's lives setting is one of
// 12 (start, most) pairs (table 0xD780, apply_settings 0xDC20: 0x51F5B1 / 0x51F5B0); ours keeps that number for those
// pairs (launcher.ini lives=, --lives, the online settings' byte), so the default 9 (3 up to 5) and every older file
// play as before, and codes any other pair as 0x10 x start + most (start 1-9, most start-9). The runtime writes a
// table pair from the ROM's table (as the arcade), any other pair straight into 0x51F5B1 / 0x51F5B0.
#pragma once

#include <algorithm>

namespace hotd::game {

constexpr int kLivesSettings = 12;
constexpr int kLivesStart[kLivesSettings] = {1, 2, 3, 1, 2, 3, 4, 1, 2, 3, 4, 5};   // table 0xD780's start lives
constexpr int kLivesMost[kLivesSettings] = {3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 5};    // and its most (max_lives)
constexpr int kLivesCap = 9;   // GitHub #25: up to 9 (the arcade: 5, its five torches; more: one torch and "x N")

// the code of a (start, most) pair: the arcade's setting when one has it, else 0x10 x start + most
inline int livesCode(int start, int most)
{
    start = std::clamp(start, 1, kLivesCap);
    most = std::clamp(most, start, kLivesCap);
    for (int i = 0; i < kLivesSettings; ++i)
        if (kLivesStart[i] == start && kLivesMost[i] == most)
            return i;
    return 0x10 * start + most;
}
inline bool livesCodeValid(int code)
{
    return (code >= 0 && code < kLivesSettings) ||
           (code >> 4 >= 1 && code >> 4 <= kLivesCap && (code & 15) >= code >> 4 && (code & 15) <= kLivesCap);
}
inline int livesStartOf(int code)
{
    if (!livesCodeValid(code)) code = 9;
    return code < kLivesSettings ? kLivesStart[code] : code >> 4;
}
inline int livesMostOf(int code)
{
    if (!livesCodeValid(code)) code = 9;
    return code < kLivesSettings ? kLivesMost[code] : code & 15;
}

} // namespace hotd::game
