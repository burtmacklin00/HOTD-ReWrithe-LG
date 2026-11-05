#include "net/rollback.h"

#include <cstring>

namespace hotd::net {

namespace {
// the regions a frame writes (HOTD_ROLLBACK_PROBE: work RAM 0x500000, 0x700000, the 2D RAMs 0x1000000 / 0x1080000);
// the ROMs and main_data (0x2000000) are only read
bool written(uint32_t base) { return base >= 0x500000 && base < 0x2000000; }
} // namespace

void RuntimeState::save(game::Runtime& rt)
{
    size_t k = 0;
    for (const port::Mem::Region& r : rt.mem.regions()) {
        if (!written(r.base))
            continue;
        if (k == regions_.size())
            regions_.emplace_back();
        regions_[k].first = r.base;
        regions_[k].second.assign(r.bytes.get(), r.bytes.get() + r.size);
        ++k;
    }
    regions_.resize(k);
    tgp_.reset();
    tgp_.emplace(rt.tgp);
    std::copy(rt.gun, rt.gun + 4, gun_);
    inputFrames_ = rt.inputFrames;
    std::copy(rt.shotsIn, rt.shotsIn + 4, shotsIn_);
    std::copy(rt.reloadsIn, rt.reloadsIn + 4, reloadsIn_);
    std::copy(rt.startsIn, rt.startsIn + 4, startsIn_);
    frameCount_ = rt.frameCount;
    std::copy(rt.startRequest, rt.startRequest + 4, startRequest_);
    seats4_ = rt.seats4;
    std::copy(rt.seatNotice, rt.seatNotice + 2, seatNotice_);
    gameOverDone_ = rt.gameOverDone, chapterDone_ = rt.chapterDone, attract_ = rt.attract, titleFlow_ = rt.titleFlow;
    chapterChanged_ = rt.chapterChanged, textureArea_ = rt.textureArea, whiteFade_ = rt.whiteFade;
    paletteWhite__ = rt.paletteWhite_, nameKey__ = rt.nameKey_, letterbox_ = rt.letterbox;
    clear_ = rt.clear;
    colorRows_ = rt.colorRows;
    colorRowsSerial_ = rt.colorRowsSerial, fadeIdSeen__ = rt.fadeIdSeen_;
    backdrop555_ = rt.backdrop555;
    pcFade_ = rt.pcFade;
    nameKeys_ = rt.nameKeys;
    modeBeforeSet__ = rt.modeBeforeSet_, rescuedLogged__ = rt.rescuedLogged_;
    prologueForGame__ = rt.prologueForGame_, prologuePlayers__ = rt.prologuePlayers_;
    if (!greyPalette_) greyPalette_ = std::make_unique<game::Runtime::GreyPalette>();
    *greyPalette_ = rt.greyPalette;
    if (!polyPaletteAnim_) polyPaletteAnim_ = std::make_unique<game::Runtime::PolyPaletteAnim>();
    *polyPaletteAnim_ = rt.polyPaletteAnim;
    flash__ = rt.flash_;
    std::copy(rt.flashRounds_, rt.flashRounds_ + 2, flashRounds__);
    cardLatch__ = rt.cardLatch_;
    sprites2d__ = rt.sprites2d_, coverOff__ = rt.coverOff_;
    pcCells_ = rt.pcCells, pcClip_ = rt.pcClip;
    pcTexts_ = rt.pcTexts;
    pcNameModels_ = rt.pcNameModels;
    cellSide_ = rt.cellSide, cellText_ = rt.cellText, cellHud_ = rt.cellHud;
    std::copy(rt.light, rt.light + 3, light_);
    focal_ = rt.focal;
    fade__ = rt.fade_;
    geoPort__ = rt.geoPort_, lightWord__ = rt.lightWord_, geoWord__ = rt.geoWord_;
    std::copy(rt.geoWords_, rt.geoWords_ + 12, geoWords__);
    std::copy(rt.geoMat_, rt.geoMat_ + 12, geoMat__);
    wdPc__ = rt.wdPc_, wdFrames__ = rt.wdFrames_, wdUnported__ = rt.wdUnported_;
    pm_.pcmode = rt.pcmode, pm_.msp = rt.pm_msp, pm_.continues = rt.pm_continues;
    pm_.credits = rt.pm_credits, pm_.creditsSeen = rt.pm_credits_seen;
    std::copy(rt.pm_char, rt.pm_char + 4, pm_.char_);
    std::copy(rt.pm_mstack, rt.pm_mstack + 32, pm_.mstack);
    pm_.molbHeightBits = rt.pm_molb_height_bits, pm_.crandSeed = rt.pm_crand_seed, pm_.clockMs = rt.pm_clock_ms;
    std::copy(rt.pm_reload_at, rt.pm_reload_at + 4, pm_.reloadAt);
    std::copy(rt.pm_camera, rt.pm_camera + 12, pm_.camera);
    std::copy(rt.pm_camera_inv, rt.pm_camera_inv + 12, pm_.cameraInv);
    std::copy(rt.pm_camera0_inv, rt.pm_camera0_inv + 12, pm_.camera0Inv);
    std::copy(rt.pm_camera0, rt.pm_camera0 + 12, pm_.camera0);
    pm_.chariotLanded = rt.pm_chariot_landed, pm_.freePlay = rt.pm_free_play, pm_.autoReload = rt.pm_auto_reload;
    pm_.bloodTasks = rt.pm_blood_tasks, pm_.lastKiller = rt.pm_last_killer;
    boss_ = rt.boss;
    bossMode_ = rt.boss_mode, bossEvents_ = rt.boss_events;
    bossNext_ = rt.boss_next, bossNoContinue_ = rt.bossNoContinue, startAtSection0_ = rt.startAtSection0;
    valid = true;
}

void RuntimeState::restore(game::Runtime& rt) const
{
    if (!valid)
        return;
    for (const auto& [base, bytes] : regions_)
        for (const port::Mem::Region& r : rt.mem.regions())
            if (r.base == base && r.size == bytes.size())
                std::memcpy(r.bytes.get(), bytes.data(), bytes.size());
    rt.tgp = *tgp_;
    std::copy(gun_, gun_ + 4, rt.gun);
    rt.inputFrames = inputFrames_;
    std::copy(shotsIn_, shotsIn_ + 4, rt.shotsIn);
    std::copy(reloadsIn_, reloadsIn_ + 4, rt.reloadsIn);
    std::copy(startsIn_, startsIn_ + 4, rt.startsIn);
    rt.frameCount = frameCount_;
    std::copy(startRequest_, startRequest_ + 4, rt.startRequest);
    rt.seats4 = seats4_;
    std::copy(seatNotice_, seatNotice_ + 2, rt.seatNotice);
    rt.gameOverDone = gameOverDone_, rt.chapterDone = chapterDone_, rt.attract = attract_, rt.titleFlow = titleFlow_;
    rt.chapterChanged = chapterChanged_, rt.textureArea = textureArea_, rt.whiteFade = whiteFade_;
    rt.paletteWhite_ = paletteWhite__, rt.nameKey_ = nameKey__, rt.letterbox = letterbox_;
    rt.clear = clear_;
    rt.colorRows = colorRows_;
    rt.colorRowsSerial = colorRowsSerial_ + 1;   // (the view rebuilds its colours: a changed serial)
    rt.fadeIdSeen_ = fadeIdSeen__;
    rt.backdrop555 = backdrop555_;
    rt.pcFade = pcFade_;
    rt.nameKeys = nameKeys_;
    rt.modeBeforeSet_ = modeBeforeSet__, rt.rescuedLogged_ = rescuedLogged__;
    rt.prologueForGame_ = prologueForGame__, rt.prologuePlayers_ = prologuePlayers__;
    rt.greyPalette = *greyPalette_;
    rt.greyPalette.serial += 1;
    rt.polyPaletteAnim = *polyPaletteAnim_;
    rt.polyPaletteAnim.serial += 1;
    rt.flash_ = flash__;
    std::copy(flashRounds__, flashRounds__ + 2, rt.flashRounds_);
    rt.cardLatch_ = cardLatch__;
    rt.sprites2d_ = sprites2d__, rt.coverOff_ = coverOff__;
    rt.pcCells = pcCells_, rt.pcClip = pcClip_;
    rt.pcTexts = pcTexts_;
    rt.pcNameModels = pcNameModels_;
    rt.cellSide = cellSide_, rt.cellText = cellText_, rt.cellHud = cellHud_;
    std::copy(light_, light_ + 3, rt.light);
    rt.focal = focal_;
    rt.fade_ = fade__;
    rt.geoPort_ = geoPort__, rt.lightWord_ = lightWord__, rt.geoWord_ = geoWord__;
    std::copy(geoWords__, geoWords__ + 12, rt.geoWords_);
    std::copy(geoMat__, geoMat__ + 12, rt.geoMat_);
    rt.wdPc_ = wdPc__, rt.wdFrames_ = wdFrames__, rt.wdUnported_ = wdUnported__;
    rt.pcmode = pm_.pcmode, rt.pm_msp = pm_.msp, rt.pm_continues = pm_.continues;
    rt.pm_credits = pm_.credits, rt.pm_credits_seen = pm_.creditsSeen;
    std::copy(pm_.char_, pm_.char_ + 4, rt.pm_char);
    std::copy(pm_.mstack, pm_.mstack + 32, rt.pm_mstack);
    rt.pm_molb_height_bits = pm_.molbHeightBits, rt.pm_crand_seed = pm_.crandSeed, rt.pm_clock_ms = pm_.clockMs;
    std::copy(pm_.reloadAt, pm_.reloadAt + 4, rt.pm_reload_at);
    std::copy(pm_.camera, pm_.camera + 12, rt.pm_camera);
    std::copy(pm_.cameraInv, pm_.cameraInv + 12, rt.pm_camera_inv);
    std::copy(pm_.camera0Inv, pm_.camera0Inv + 12, rt.pm_camera0_inv);
    std::copy(pm_.camera0, pm_.camera0 + 12, rt.pm_camera0);
    rt.pm_chariot_landed = pm_.chariotLanded, rt.pm_free_play = pm_.freePlay, rt.pm_auto_reload = pm_.autoReload;
    rt.pm_blood_tasks = pm_.bloodTasks, rt.pm_last_killer = pm_.lastKiller;
    rt.boss = boss_;
    rt.boss_mode = bossMode_, rt.boss_events = bossEvents_;
    rt.boss_next = bossNext_, rt.bossNoContinue = bossNoContinue_, rt.startAtSection0 = startAtSection0_;
}

} // namespace hotd::net
