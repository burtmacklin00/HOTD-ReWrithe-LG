#include "game/host.h"

#include "game/boot.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace hotd::game {

struct EmbeddedName { uint32_t addr; const char* name; };
extern const EmbeddedName kEmbeddedNames[];   // (generated: build/generated/names_tsv.cpp)

namespace {

std::vector<uint8_t> readFile(const std::string& path)
{
    std::vector<uint8_t> v;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return v;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    v.resize(size_t(n > 0 ? n : 0));
    v.resize(std::fread(v.data(), 1, v.size(), f));
    std::fclose(f);
    return v;
}

} // namespace

bool Host::load(const std::string& regionsDir, const std::string& snapshot, const std::string& namesTsv, std::string& err)
{
    const std::vector<uint8_t> rom = readFile(regionsDir + "/maincpu.bin");
    const std::vector<uint8_t> data = readFile(regionsDir + "/main_data.bin");
    const std::vector<uint8_t> copro = readFile(regionsDir + "/copro_data.bin");
    // no snapshot: the work RAM is built from the ROM (game/boot.h); a snapshot is the developer option --snapshot
    const std::vector<uint8_t> ram = snapshot.empty() ? std::vector<uint8_t>(Mem::kRamSize, 0) : readFile(snapshot);
    if (rom.empty() || data.empty() || copro.empty()) {
        err = "arcade regions missing in " + regionsDir + " (maincpu.bin, main_data.bin, copro_data.bin)";
        return false;
    }
    if (ram.size() != Mem::kRamSize) {
        err = "work RAM snapshot missing or not 1 MB: " + snapshot;
        return false;
    }
    mem_ = std::make_unique<Mem>();
    mem_->load(0, rom);
    mem_->load(Mem::kData, data);
    // the upper half of main_data is also seen at 0x6000000 (the motion data pointers 0xC7540 point there)
    if (data.size() > 0x1000000)
        mem_->load(0x6000000, std::vector<uint8_t>(data.begin() + 0x1000000, data.end()));
    mem_->load(Mem::kRam, ram);
    copro_.resize(copro.size() / 4);
    std::memcpy(copro_.data(), copro.data(), copro_.size() * 4);
    tgp_ = std::make_unique<Tgp>(copro_.data(), copro_.size());
    runtime_ = std::make_unique<Runtime>(*mem_, *tgp_);
    // stack locals the port passes by address (Game::local_alloc): a scratch region of their own outside the original
    // map (the work RAM's top is used by the heap later in the game)
    constexpr uint32_t kLocals = 0x700000;
    mem_->map(kLocals, 0x10000);
    runtime_->localTop = kLocals;
    runtime_->palette_ram_map();   // palette RAM bank 2 (arcfix2, palette.cpp)
    if (snapshot.empty()) {
        boot_work_ram(*runtime_);
        runtime_->palette_init_bank2();
    }
    if (!namesTsv.empty()) {
        std::ifstream f(namesTsv);
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string kind, pc, arcade, name;
            if (std::getline(ss, kind, '\t') && kind == "func" && std::getline(ss, pc, '\t') && std::getline(ss, arcade, '\t') &&
                std::getline(ss, name, '\t') && arcade != "-")
                runtime_->names[uint32_t(std::stoul(arcade, nullptr, 16))] = name;
        }
    }
    // totd8: the names compiled in (CMakeLists.txt) when the file is not there: a package has no tools\ folder, and
    // Typing of the Dead 0 / the CPU teammates sort objects by these names
    if (runtime_->names.empty())
        for (const EmbeddedName* e = kEmbeddedNames; e->name; ++e)
            runtime_->names[e->addr] = e->name;
    return true;
}

} // namespace hotd::game
