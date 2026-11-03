// PC loader (The House of the Dead, PC 1998). Formats: docs/formats.md §3 (VMC), §9 (world RBH).
// World geometry from ART\STAGEn\WORLD\WORLDn.RBH; world textures are pages 512 and up of ART\STAGE1\CREATURE\BIGGER.VMC
// (the texture word of a world polygon is the VMC page number).
#pragma once

#include "assets/assets.h"

#include <string>

namespace hotd {

struct PcConfig {
    std::string installDir;   // the player's PC install (read only)
    int blood = 0;            // blood colour 0 red (default) .. 3 purple (the launcher setting); -1: the install's TEXTURE.VMC;
                              // kBloodCustom | 0xRRGGBB (display, experimental): B_RED's blood texels tinted to that colour
    static constexpr int kBloodCustom = 0x1000000;
};

// display2: the given BIGGER.VMC pages as the PC game draws them with blood colour `blood` (PcConfig::blood): from
// B_<colour>.VMC (custom: B_RED's blood texels tinted), RGBA8 through tbl / pal when given. false: red / none / missing
bool pcBloodPages(const std::string& installDir, int blood, const std::vector<uint16_t>& pages,
                  std::vector<std::vector<uint8_t>>& out, std::string& err, const std::vector<uint8_t>* tbl = nullptr,
                  const std::vector<uint8_t>* pal = nullptr);

// pchotfix (players 2026-11-02, GitHub #2 / #21): a VMC's page numbering. The Japanese PC release (and the copies
// made from it: its BIGGER.VMC is 54,293,628 bytes, the B_<colour>.VMC sets smaller too) has 7 fewer pages below 512:
// its pages 1..330 are the US / European release's 8..337 (those added pages 1-7: the grey menu font and a picture);
// the world pages 512 and up are the same. Returns what to add to the file's page numbers below 512 (7 for the
// Japanese layout, else 0); read once per file (its chunk headers)
int pcVmcPageShift(const std::string& path);

class PcSource : public AssetSource {
public:
    explicit PcSource(PcConfig cfg);
    Source source() const override { return Source::PC; }
    bool available() const override { return available_; }
    std::string status() const override { return status_; }
    int stageCount() const override { return 4; }
    std::unique_ptr<StageData> loadStage(int stage, std::string& err) override;

private:
    PcConfig cfg_;
    bool available_ = false;
    std::string status_;
};

} // namespace hotd
