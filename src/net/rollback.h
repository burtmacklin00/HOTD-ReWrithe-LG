// Rollback's snapshot (docs/netplay.md "Rollback"): the game's whole state between two frames, saved before each
// predicted frame and restored when the other player's real input differs from the prediction. The state is the
// written memory regions (work RAM, the 2D RAMs; HOTD_ROLLBACK_PROBE showed nothing else in memory changes), the
// geometry coprocessor's registers and B-RAM, and the runtime's own fields that live between frames (listed in
// rollback.cpp; a friend of game::Runtime). A field added to the runtime later that keeps game state between frames
// must be added there too: the rollback test (two PCs, --net-rollback, the hashes) shows a miss as a desync.
#pragma once

#include "game/runtime.h"

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace hotd::net {

struct RuntimeState {
    void save(game::Runtime& rt);
    void restore(game::Runtime& rt) const;
    bool valid = false;

private:
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> regions_;   // base, bytes
    std::optional<port::Tgp> tgp_;
    game::Runtime::GunInput gun_[4];   // (4: the 4-seat game, docs/coop4.md phase 7)
    int seats4_ = 0;
    game::Runtime::SeatNotice seatNotice_[2];
    uint32_t inputFrames_ = 0, shotsIn_[4] = {}, reloadsIn_[4] = {}, startsIn_[4] = {};
    uint64_t frameCount_ = 0;
    bool startRequest_[4] = {}, gameOverDone_ = false, chapterDone_ = false, attract_ = false, titleFlow_ = false;
    int chapterChanged_ = -1, textureArea_ = -1, whiteFade_ = -1, paletteWhite__ = -1, nameKey__ = 0, letterbox_ = 0;
    game::Runtime::ChapterClear clear_;
    std::map<int, int> colorRows_;
    uint32_t colorRowsSerial_ = 0, fadeIdSeen__ = 0;
    uint16_t backdrop555_ = 0x8000;
    game::Runtime::PcFade pcFade_;
    std::deque<int> nameKeys_;
    int16_t modeBeforeSet__ = 0, rescuedLogged__ = 0;
    int prologueForGame__ = 0, prologuePlayers__ = 1;
    std::unique_ptr<game::Runtime::GreyPalette> greyPalette_;
    std::unique_ptr<game::Runtime::PolyPaletteAnim> polyPaletteAnim_;
    int flash__ = 0, flashRounds__[2] = {6, 6};
    std::vector<std::pair<uint16_t*, uint16_t>> cardLatch__;
    bool sprites2d__ = false, coverOff__ = false;
    std::vector<uint32_t> pcCells_, pcClip_;
    std::map<std::pair<int, int>, std::string> pcTexts_;
    std::vector<game::Runtime::PcNameModel> pcNameModels_;
    std::vector<uint8_t> cellSide_, cellText_, cellHud_;
    float light_[3] = {}, focal_ = 496.0f;
    int fade__ = 0xFF;
    uint32_t geoPort__ = 0, geoWords__[12] = {}, geoMat__[12] = {}, wdPc__ = 0, wdFrames__ = 0;
    int lightWord__ = 3, geoWord__ = 0;
    uint64_t wdUnported__ = 0;
    // PC Mode's runtime fields (Game::pm_*, docs/pcmode.md; offered online since protocol 16): the
    // ones that live between frames, so an online PC Mode game rolls back (NetGame::gameHash hashes them too)
    struct Pm {
        int pcmode = 0, char_[4] = {}, msp = 0, continues = 5, credits = 0, creditsSeen = 0;
        port::pm::Matrix mstack[32];
        uint32_t molbHeightBits = 0, crandSeed = 1, reloadAt[4] = {}, clockMs = 0;
        float camera[12] = {}, cameraInv[12] = {}, camera0Inv[12] = {}, camera0[12] = {};
        bool chariotLanded = false, freePlay = false, autoReload = false;
        int32_t bloodTasks = 0, lastKiller = 0;
    } pm_;
    // Boss Mode (offered online since protocol 18): the mode's state (clock, boss, phase ..) and the logic's Boss Mode
    // fields (port/game.h: boss_mode, pc_boss_next, the boss calls of the frame, no continue, the section-0 start)
    game::BossMode boss_;
    int bossMode_ = 0;
    uint32_t bossEvents_ = 0;
    bool bossNext_ = false, bossNoContinue_ = false, startAtSection0_ = false;
};

} // namespace hotd::net
