#include "game/files_check.h"

#include "assets/archive.h"
#include "assets/pc_files.h"
#include "util/file64.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace hotd::game {

namespace fs = std::filesystem;

namespace {

// MAME's hotd (Revision A) set, from src/mame/sega/model2.cpp (as tools/m2_roms.py): the file, its region and place
// (word interleave: a file's 16-bit words go to every other word of the region, offset 0 or 2; the sound ROMs are
// byte-swapped 16-bit), size, CRC-32 and SHA-1. Hashes are public metadata, not ROM data.
enum Method { W32, SWAP16 };
struct Rom {
    const char* name;
    const char* region;
    uint32_t offset, size, crc;
    const char* sha1;
    Method method;
};
const Rom kRoms[] = {
    {"epr-19696a.15", "maincpu", 0x000000, 0x080000, 0x42adc32e, "92931e8339dbba23d7ade178fcc56d37291411cb", W32},
    {"epr-19697a.16", "maincpu", 0x000002, 0x080000, 0x1e247cd5, "693e929d543bad880ea69d781155949f0aa246de", W32},
    {"epr-19694.13", "maincpu", 0x100000, 0x080000, 0xe85ca1a3, "3d688be98f78fe40c2af1e91df6decd500400ae9", W32},
    {"epr-19695.14", "maincpu", 0x100002, 0x080000, 0xcd52b461, "bc96ab2a4ba7f30c0b89814acc8931c8bf800a82", W32},
    {"mpr-19704.11", "main_data", 0x0000000, 0x400000, 0xaa80dbb0, "24e63f4392847f288971469cd10448536eb435d4", W32},
    {"mpr-19705.12", "main_data", 0x0000002, 0x400000, 0xf906843b, "bee4f43b3ad15d93a2f9f07b873c9cf5d228e2f9", W32},
    {"mpr-19702.9", "main_data", 0x0800000, 0x400000, 0xfc8aa3b7, "b64afb17d9c97277d8c4f20811f14f65a61cbb56", W32},
    {"mpr-19703.10", "main_data", 0x0800002, 0x400000, 0x208d993d, "e5c45ea5621f99661a87ffe88e24764d2bbcb51e", W32},
    {"mpr-19700.7", "main_data", 0x1000000, 0x400000, 0x0558cfd3, "94440839d3325176c2d03f39a78949d0ef040bba", W32},
    {"mpr-19701.8", "main_data", 0x1000002, 0x400000, 0x224a8929, "933770546d46abca400e7f524eff2ae89241e56d", W32},
    {"epr-19698.5", "main_data", 0x1800000, 0x080000, 0xe7a7b6ea, "77cb53f8730fdb55080b70910ab8c750d79acb02", W32},
    {"epr-19699.6", "main_data", 0x1800002, 0x080000, 0x8160b3d9, "9dab483c60624dddba8085e94a4325739592ec17", W32},
    {"epr-19707.29", "copro_data", 0x000000, 0x080000, 0x384fd133, "6d060378d0f801b04d12e7ee874f2fa0572992d9", W32},
    {"epr-19706.30", "copro_data", 0x000002, 0x080000, 0x1277531c, "08d3e733ba9989fcd32290634171c73f26ab6e2b", W32},
    {"mpr-19715.17", "polygons", 0x0000000, 0x400000, 0x3ff7dda7, "0a61b091bb0bc659f0cbca8ad401d0925a1dc2ea", W32},
    {"mpr-19711.21", "polygons", 0x0000002, 0x400000, 0x080d13f1, "4167428a2a903aea2c14631ccf924afb81338b89", W32},
    {"mpr-19714.18", "polygons", 0x0800000, 0x400000, 0x3e55ab49, "70b4c1627db80e6734112c02265495e2b4a53278", W32},
    {"mpr-19710.22", "polygons", 0x0800002, 0x400000, 0x80df1036, "3cc59bb4910aa5382e95762f63325c06b763bd23", W32},
    {"mpr-19713.19", "polygons", 0x1000000, 0x400000, 0x4d092cd3, "b6d0be283c25235249186751c7f025a7c38d2f36", W32},
    {"mpr-19709.23", "polygons", 0x1000002, 0x400000, 0xd08937bf, "c92571e35960f27dc8b0b059f12167026d0666d1", W32},
    {"mpr-19712.20", "polygons", 0x1800000, 0x400000, 0x41577943, "25a0d921c8662043c5860dc7a226d4895ff9fff6", W32},
    {"mpr-19708.24", "polygons", 0x1800002, 0x400000, 0x5cb790f2, "d3cae450186bc62fd746b14d6a05cb397efcfe40", W32},
    {"mpr-19718.27", "textures", 0x0000000, 0x400000, 0xa9de5924, "3ebac2aeb1467939337c9a5c87ad9c293560dae2", W32},
    {"mpr-19716.25", "textures", 0x0000002, 0x400000, 0x45c7dcce, "f602cabd879c69afee544848feafb9fb9f5d51e2", W32},
    {"mpr-19719.28", "textures", 0x0800000, 0x400000, 0x838f8343, "fe6622b5917f9a99c097fd60d9446ac6b481fa75", W32},
    {"mpr-19717.26", "textures", 0x0800002, 0x400000, 0x393e440b, "927ac9cad22f87b339cc86043678470ff139ce1f", W32},
    {"epr-19720.31", "audiocpu", 0x000000, 0x080000, 0xb367d21d, "1edaed489a3518ddad85728e416319f940ea02bb", SWAP16},
    {"mpr-19721.32", "samples", 0x000000, 0x400000, 0xf5d8fa9a, "6836973a687c59dd80f8e6c30d33155e306be199", SWAP16},
    {"mpr-19722.34", "samples", 0x400000, 0x400000, 0xa56fa539, "405a892bc368ba862ba71bda7525b421d6973c0e", SWAP16},
};
constexpr size_t kRomCount = sizeof kRoms / sizeof kRoms[0];
// the regions the rewrite reads, and their sizes
const std::pair<const char*, uint32_t> kRegions[] = {
    {"maincpu", 0x200000}, {"main_data", 0x2000000}, {"copro_data", 0x800000}, {"polygons", 0x2000000}, {"textures", 0x1000000},
    {"audiocpu", 0x80000}, {"samples", 0x800000}};   // the sound board (src/audio/sound_board.h): the ROM files as they are
constexpr const char* kStampName = "hotd_regions.stamp";
constexpr const char* kStampHeader = "hotd regions v2 (MAME hotd, Revision A)";

struct PcFile {
    const char* path;
    uint64_t size;
    const char* sha1;
};
const PcFile kPcFiles[] = {
#include "game/pc_manifest.inc"
};

std::string lower(std::string s)
{
    for (char& c : s)
        c = char(std::tolower(uint8_t(c)));
    return s;
}

std::string baseName(const std::string& p)
{
    const size_t k = p.find_last_of("/\\");
    return k == std::string::npos ? p : p.substr(k + 1);
}

bool readWhole(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = hotd::fopenLong(path);
    if (!f)
        return false;
    hotd::fseek64(f, 0, SEEK_END);
    const int64_t n = hotd::ftell64(f);
    hotd::fseek64(f, 0, SEEK_SET);
    out.resize(size_t(n > 0 ? n : 0));
    const bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

// a file's identity for "remembered" results: size and modification time
std::string fileStamp(const fs::path& p)
{
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec)
        return "";
    const auto t = fs::last_write_time(p, ec).time_since_epoch().count();
    return std::to_string(size) + " " + std::to_string(t);
}

// the files of `where` (a file, or a folder searched up to 6 levels deep; pchotfix: was 4, which missed the PC game
// two folders down, e.g. data\pc\<rip>\Game Files\ART\STAGE1\WORLD\WORLD1.RBH)
std::vector<fs::path> listFiles(const fs::path& where)
{
    std::vector<fs::path> files;
    std::error_code ec;
    if (fs::is_regular_file(where, ec)) {
        files.push_back(where);
        return files;
    }
    fs::recursive_directory_iterator it(where, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        if (it.depth() > 6) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec))
            files.push_back(it->path());
    }
    return files;
}

// where one ROM was found: a zip entry or a loose file
struct Source {
    fs::path file;
    int entry = -1;   // index into the zip's entries, or -1 for a loose file
};

} // namespace

CheckReport checkArcade(const std::string& where, const std::string& regionsDir, bool force,
                        const std::function<void(const std::string&)>& progress)
{
    CheckReport r;
    auto say = [&](const std::string& s) { if (progress) progress(s); };
    if (where.empty() || !fs::exists(where)) {
        // firstrun: "not chosen yet" for an empty path (was "'' not found")
        r.lines.push_back((where.empty() ? std::string("Arcade ROMs: not chosen yet.") : "Arcade ROMs: '" + where + "' not found.") +
                          " Choose the folder or zip with your House of the Dead (Model 2, Revision A) ROM files.");
        return r;
    }
    say("Looking for the arcade ROMs ...");
    // 1. candidates: zip entries (size + CRC from the zip's directory) and loose files of a ROM's size (CRC read)
    std::set<uint32_t> sizes;
    for (const Rom& rom : kRoms)
        sizes.insert(rom.size);
    std::map<fs::path, ZipFile> zips;
    std::vector<Source> found(kRomCount);
    std::vector<std::string> wrong(kRomCount);   // a file named like the ROM with other contents
    auto consider = [&](const fs::path& file, int entry, const std::string& name, uint64_t size, uint32_t crc) {
        for (size_t i = 0; i < kRomCount; ++i) {
            if (!found[i].file.empty())
                continue;
            if (size == kRoms[i].size && crc == kRoms[i].crc) {
                found[i] = {file, entry};
                wrong[i].clear();
            } else if (lower(baseName(name)) == kRoms[i].name) {
                char buf[160];
                std::snprintf(buf, sizeof buf, "size %llu, CRC-32 %08x", (unsigned long long)size, crc);
                wrong[i] = file.string() + (entry >= 0 ? " : " + name : "") + " (" + buf + ")";
            }
        }
    };
    for (const fs::path& f : listFiles(where)) {
        std::error_code ec;
        const uint64_t size = fs::file_size(f, ec);
        if (lower(f.extension().string()) == ".zip") {
            ZipFile z;
            std::string err;
            if (!z.open(f.string(), err))
                continue;
            for (size_t k = 0; k < z.entries().size(); ++k) {
                const ZipFile::Entry& e = z.entries()[k];
                consider(f, int(k), e.name, e.size, e.crc);
            }
            zips.emplace(f, std::move(z));
        } else if (sizes.count(uint32_t(size)) && size == uint32_t(size)) {
            std::vector<uint8_t> d;
            if (readWhole(f.string(), d))
                consider(f, -1, f.filename().string(), d.size(), crc32(d.data(), d.size()));
        }
    }
    int missing = 0;
    for (size_t i = 0; i < kRomCount; ++i)
        if (found[i].file.empty()) {
            ++missing;
            if (!wrong[i].empty())
                r.lines.push_back(std::string("WRONG   ") + kRoms[i].name + ": " + wrong[i] + "; expected size " +
                                  std::to_string(kRoms[i].size) + ", CRC-32 " + [&] { char b[12]; std::snprintf(b, sizeof b, "%08x", kRoms[i].crc); return std::string(b); }() +
                                  " (MAME hotd, Revision A)");
            else
                r.lines.push_back(std::string("MISSING ") + kRoms[i].name + " (" + kRoms[i].region + ")");
        }
    if (missing) {
        r.lines.push_back(std::to_string(missing) + " of " + std::to_string(kRomCount) +
                          " ROMs missing or different: these files are not MAME's hotd set (The House of the Dead, Revision A). "
                          "Other revisions (the prototype, the original release) are not supported.");
        return r;
    }
    // 2. remembered: the same source files (sizes, dates) as when the regions were built
    std::set<fs::path> used;
    for (const Source& s : found)
        used.insert(s.file);
    std::string stamp = std::string(kStampHeader) + "\n";
    for (const fs::path& p : used)
        stamp += fs::absolute(p).string() + "\t" + fileStamp(p) + "\n";
    const fs::path dir(regionsDir);
    bool have = !force;
    for (const auto& [name, size] : kRegions) {
        std::error_code ec;
        have = have && fs::file_size(dir / (std::string(name) + ".bin"), ec) == size && !ec;
    }
    if (have) {
        std::ifstream f(dir / kStampName, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        if (ss.str() == stamp) {
            r.ok = true;
            r.lines.push_back("Arcade ROMs: all " + std::to_string(kRomCount) + " match MAME's hotd (Revision A) (checked before; " +
                              std::to_string(used.size()) + " source file(s) unchanged)");
            return r;
        }
    }
    // 3. read every ROM, check its SHA-1, and build the regions
    std::map<std::string, std::vector<uint8_t>> regions;
    for (const auto& [name, size] : kRegions)
        regions[name].assign(size, 0);
    int bad = 0;
    for (size_t i = 0; i < kRomCount; ++i) {
        const Rom& rom = kRoms[i];
        say(std::string("Checking ") + rom.name + " ...");
        std::vector<uint8_t> d;
        std::string err;
        bool ok = found[i].entry >= 0 ? zips.at(found[i].file).read(zips.at(found[i].file).entries()[size_t(found[i].entry)], d, err)
                                      : readWhole(found[i].file.string(), d);
        if (!ok || d.size() != rom.size || crc32(d.data(), d.size()) != rom.crc || sha1Hex(d.data(), d.size()) != rom.sha1) {
            ++bad;
            r.lines.push_back(std::string("WRONG   ") + rom.name + ": " + found[i].file.string() +
                              (err.empty() ? ": its SHA-1 differs from MAME's hotd (Revision A)" : ": " + err));
            continue;
        }
        auto it = regions.find(rom.region);
        if (it == regions.end())
            continue;
        std::vector<uint8_t>& out = it->second;
        if (rom.method == SWAP16) {   // the sound board's ROMs: kept as in the file (the board swaps the words)
            std::copy(d.begin(), d.end(), out.begin() + rom.offset);
            continue;
        }
        for (uint32_t k = 0; k + 1 < rom.size; k += 2) {
            const uint32_t at = rom.offset + k * 2;
            out[at] = d[k];
            out[at + 1] = d[k + 1];
        }
    }
    if (bad) {
        r.lines.push_back(std::to_string(bad) + " ROM(s) differ from MAME's hotd (Revision A): not the official arcade release.");
        return r;
    }
    // main_data 0x1800000..0x18FFFFF is mirrored up to 0x1FFFFFF (ROM_COPY)
    std::vector<uint8_t>& md = regions["main_data"];
    for (uint32_t dst = 0x1900000; dst < 0x2000000; dst += 0x100000)
        std::copy(md.begin() + 0x1800000, md.begin() + 0x1900000, md.begin() + dst);
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (const auto& [name, data] : regions) {
        std::ofstream f(dir / (name + ".bin"), std::ios::binary);
        f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!f) {
            r.lines.push_back("Cannot write " + (dir / (name + ".bin")).string());
            return r;
        }
    }
    std::ofstream(dir / kStampName, std::ios::binary) << stamp;
    r.ok = true;
    r.lines.push_back("Arcade ROMs: all " + std::to_string(kRomCount) + " match MAME's hotd (Revision A); regions built in " + dir.string());
    return r;
}

namespace {
const struct { size_t at; uint8_t eu, us; } kEuBits[] = {
    {17109976, 0xFD, 0xBD}, {20519160, 0xFD, 0xBD}, {20648664, 0xFE, 0xDE}, {36476376, 0xFD, 0xBD}, {42048312, 0xEE, 0xCE}};
} // namespace

bool isEuBigger(const std::string& rel, const std::vector<uint8_t>& d)
{
    std::string r = rel;
    for (char& c : r) c = c == '\\' ? '/' : char(std::tolower(static_cast<unsigned char>(c)));
    if (r != "art/stage1/creature/bigger.vmc" || d.size() != 55211636) return false;
    for (const auto& b : kEuBits)
        if (d[b.at] != b.eu) return false;
    return true;
}

bool fixEuBigger(const std::string& rel, std::vector<uint8_t>& d)
{
    if (!isEuBigger(rel, d)) return false;
    for (const auto& b : kEuBits) d[b.at] = b.us;
    return true;
}

// pczip: the PC game as a folder or a .zip (assets/pc_files.h; in a zip the install may be one folder down). Every
// file is an item: its lower-case path relative to the install, the path to read it by (a real file, or
// "<zip>/<entry>"), its size and its identity for remembered results (size + date; in a zip: the zip's + the CRC-32)
CheckReport checkPc(const std::string& folder, const std::string& cacheFile, std::map<std::string, std::string>& found,
                    const std::function<void(const std::string&)>& progress)
{
    CheckReport r;
    found.clear();
    auto say = [&](const std::string& s) { if (progress) progress(s); };
    std::error_code ec;
    struct Item {
        std::string path;
        uint64_t size = 0;
        std::string stamp;
    };
    std::map<std::string, Item> byPath;   // lower-case relative path -> the file
    std::vector<Item> all;
    if (const std::string why = pcfs::unsupportedArchive(folder); !why.empty()) {
        r.lines.push_back("PC files: " + why);
        return r;
    }
    if (pcfs::isZip(folder)) {
        std::vector<pcfs::ZipItem> items;
        std::string err;
        if (!pcfs::listZip(folder, items, err)) {
            r.lines.push_back("PC files: cannot read the zip: " + err);
            return r;
        }
        const std::string zipStamp = fileStamp(folder);
        for (const pcfs::ZipItem& z : items) {
            char crc[16];
            std::snprintf(crc, sizeof crc, "%08x", z.crc);
            Item it{z.path, z.size, zipStamp + " " + crc};
            byPath[z.rel] = it;
            all.push_back(std::move(it));
        }
    } else if (!folder.empty() && fs::is_directory(folder, ec)) {
        for (const fs::path& p : listFiles(folder)) {
            Item it{p.string(), uint64_t(fs::file_size(p, ec)), fileStamp(p)};
            byPath[lower(fs::relative(p, folder, ec).generic_string())] = it;
            all.push_back(std::move(it));
        }
    } else {
        r.lines.push_back(folder.empty() ? std::string("PC files: not chosen yet (the folder, or a .zip, of the installed 1998 PC version).") : "PC files: '" + folder + "' not found. Choose the folder (or the .zip) of your installed PC version (1998).");
        return r;
    }
    // remembered results: manifest path \t real file \t its stamp
    std::map<std::string, std::pair<std::string, std::string>> memo;
    {
        std::ifstream f(cacheFile);
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string rel, real, stamp;
            if (std::getline(ss, rel, '\t') && std::getline(ss, real, '\t') && std::getline(ss, stamp))
                memo[rel] = {real, stamp};
        }
    }
    auto matches = [&](const Item& p, const PcFile& want) -> bool {
        if (p.size != want.size || p.stamp.empty())
            return false;
        auto m = memo.find(want.path);
        if (m != memo.end() && m->second.first == p.path && m->second.second == p.stamp)
            return true;
        std::vector<uint8_t> d;
        return pcfs::readFile(p.path, d) && sha1Hex(d.data(), d.size()) == want.sha1;
    };
    std::map<std::string, std::string> stamps;   // found file -> its stamp (for the remembered results)
    int bad = 0, moved = 0;
    std::vector<const PcFile*> lost;
    for (const PcFile& want : kPcFiles) {
        say(std::string("Checking ") + want.path + " ...");
        const std::string rel = lower(want.path);
        auto at = byPath.find(rel);
        if (at != byPath.end() && matches(at->second, want)) {
            found[want.path] = at->second.path;
            stamps[at->second.path] = at->second.stamp;
            continue;
        }
        // a patched install: the patch's backup of the original (THOTD.EXE.orig / .bak ...)
        bool backup = false;
        for (const char* ext : {".orig", ".bak"}) {
            auto orig = byPath.find(rel + ext);
            if (!backup && orig != byPath.end() && matches(orig->second, want)) {
                found[want.path] = orig->second.path;
                stamps[orig->second.path] = orig->second.stamp;
                backup = true;
            }
        }
        if (backup)
            continue;
        lost.push_back(&want);   // pchotfix: a different file at its path is looked for elsewhere first (below)
    }
    // not at its path (or another file there): looked for by size + SHA-1 among the folder's (zip's) files (once; the
    // place is remembered)
    // pchotfix (players 2026-11-02, GitHub #21 / #2, the trailer's comments): a file that is there but differs from the
    // 1998 release (another version: the Japanese release, a patch, a repack) is ACCEPTED with a note, no longer a
    // block: the loaders read such files defensively (sprites_pc.cpp, pc_loader.cpp: the Japanese page numbers,
    // damaged pages passed over). Only what the game cannot work without blocks Play: THOTD.EXE (a Windows program),
    // GAMESNDS.RBH, the world / creature models and BIGGER.VMC missing entirely or not even the file type. A missing
    // SOUND\O_*.RBH (one voice / sound effect) is a note: that sound stays silent.
    std::set<std::string> variants;   // accepted different files (not remembered as matches)
    int differ = 0, silent = 0;
    for (const PcFile* want : lost) {
        const std::string rel = lower(want->path);
        const Item* hit = nullptr;
        auto m = memo.find(want->path);
        if (m != memo.end())
            for (const Item& it : all)
                if (it.path == m->second.first && matches(it, *want)) {
                    hit = &it;
                    break;
                }
        for (size_t i = 0; !hit && i < all.size(); ++i)
            if (matches(all[i], *want))
                hit = &all[i];
        if (hit) {
            ++moved;
            found[want->path] = hit->path;
            stamps[hit->path] = hit->stamp;
            r.lines.push_back(std::string("moved   ") + want->path + " found as " + hit->path);
            continue;
        }
        // another version of the file: at its path, else under the same name one or more folders down (a copy of the
        // CD in a sub-folder)
        const Item* other = nullptr;
        if (auto at = byPath.find(rel); at != byPath.end())
            other = &at->second;
        for (const auto& [k, it] : byPath)
            if (!other && k.size() > rel.size() && k.compare(k.size() - rel.size(), rel.size(), rel) == 0 &&
                (k[k.size() - rel.size() - 1] == '/' || k[k.size() - rel.size() - 1] == '\\'))
                other = &it;
        const bool sound = rel.rfind("sound/o_", 0) == 0;
        if (other) {
            std::vector<uint8_t> head;
            const bool read = pcfs::readFile(other->path, head) && !head.empty();
            const bool exe = rel == "thotd.exe";
            const bool typeOk = read && (exe ? head.size() > 0x400 && head[0] == 'M' && head[1] == 'Z'
                                             : head.size() >= 64 && std::memcmp(head.data(), "PIFF", 4) == 0);
            if (typeOk) {
                found[want->path] = other->path;
                variants.insert(other->path);
                ++differ;
                std::string what = "a different version of the file";
                if (exe)
                    what = "a patched or other version of THOTD.EXE";   // relfix: only its sound cue table and icon are read
                else if (rel == "art/stage1/creature/bigger.vmc" && other->size == 54293628)
                    what = "the Japanese PC release's version (read with its own page numbers)";
                else if (isEuBigger(rel, head))
                    what = "the European CD's version (5 bits off; set back to the US file when installed)";
                else if (other->size == want->size)
                    what = "a few bytes differ (another pressing of the CD, or a read error while copying it)";
                r.lines.push_back(std::string("DIFFERS ") + want->path + ": " + what + ", accepted (size " +
                                  std::to_string(other->size) + "; the 1998 release's: " + std::to_string(want->size) + ")");
                continue;
            }
            if (sound) {
                ++silent;
                r.lines.push_back(std::string("note    ") + want->path + " cannot be read (" + other->path + "): that sound stays silent");
                continue;
            }
            ++bad;
            r.lines.push_back(std::string("WRONG   ") + want->path + ": " + other->path + " is not a " +
                              (exe ? "Windows program" : "House of the Dead data file") + " (empty, cut or another file; size " +
                              std::to_string(other->size) + ", the 1998 release's: " + std::to_string(want->size) + ")");
            continue;
        }
        if (sound) {
            ++silent;
            r.lines.push_back(std::string("note    ") + want->path + " missing: that sound stays silent");
            continue;
        }
        ++bad;
        r.lines.push_back(std::string("MISSING ") + want->path + " (1998 PC release)");
    }
    // pchotfix (GitHub #13): the PC game one or more folders down ("moved" files): the files the check does not list
    // (SPRITE\SPR.RBH: the menu's pictures, MENU / TITLE / STAGEn palettes, B_<colour>.VMC ...) are found the same way,
    // from the folder THOTD.EXE is in (else the menu fell back to the text launcher and went straight to the game)
    std::map<std::string, std::string> extra;
    if (const auto exe = found.find("THOTD.EXE"); exe != found.end()) {
        const std::string& e = exe->second;
        const size_t cut = e.size() >= 9 ? e.size() - 9 : 0;   // "thotd.exe"
        if (lower(e.substr(cut)) == "thotd.exe" && cut > 0) {
            const std::string root = lower(e.substr(0, cut));
            for (const Item& it : all) {
                const std::string p = lower(it.path);
                if (p.size() > root.size() && p.compare(0, root.size(), root) == 0) {
                    std::string k = p.substr(root.size());
                    for (char& c : k)
                        if (c == '\\') c = '/';
                    extra.emplace(k, it.path);
                }
            }
        }
    }
    {
        std::ofstream f(cacheFile);
        for (const auto& [rel, real] : found)
            if (!variants.count(real))
                f << rel << '\t' << real << '\t' << stamps[real] << '\n';
    }
    {   // (after the remembered list: those are the checked files only)
        std::set<std::string> listed;
        for (const auto& [rel, real] : found)
            listed.insert(lower(rel));
        for (const auto& [k, real] : extra)
            if (!listed.count(k))
                found[k] = real;
    }
    if (!found.count("sprite/spr.rbh") && !pcfs::exists(folder + "/SPRITE/SPR.RBH"))
        r.lines.push_back("note    SPRITE/SPR.RBH not found: the menu's pictures cannot be shown (the simple menu instead)");
    int music = 0;
    for (int t = 2; t <= 30; ++t) {
        char name[32];
        std::snprintf(name, sizeof name, "music/track%02d.ogg", t);
        music += byPath.count(name) ? 1 : 0;
    }
    r.ok = bad == 0;
    const int matching = int(std::size(kPcFiles)) - bad - differ - silent;
    r.lines.push_back("PC files: " + std::to_string(matching) + " of " + std::to_string(std::size(kPcFiles)) +
                      " match the 1998 PC release" + (pcfs::isZip(folder) ? " (in the zip)" : "") +
                      (moved ? " (" + std::to_string(moved) + " found elsewhere in the " + (pcfs::isZip(folder) ? "zip" : "folder") + ")" : "") +
                      (differ ? ", " + std::to_string(differ) + " another version (accepted)" : "") +
                      (silent ? ", " + std::to_string(silent) + " sound(s) missing (silent)" : ""));
    r.lines.push_back("Music (optional, not checked): " + std::to_string(music) + " CD track(s) found (Music\\TrackNN.ogg)");
    if (!r.ok)
        r.lines.push_back("Some PC files are missing or not readable: the game needs the 1998 PC release's files.");
    // pchotfix: the setup's short notes (yellow under the green OK)
    for (const std::string& s : r.lines) {
        if (s.rfind("DIFFERS ", 0) == 0 && r.notes.size() < 3) {
            const std::string name = s.substr(8, s.find(':') - 8);
            const bool ja = s.find("Japanese") != std::string::npos;
            if (s.find("European CD's") != std::string::npos) {
                r.notes.push_back(baseName(name) + ": the European CD's version (5 bits off), made the US file when installed.");
                continue;
            }
            r.notes.push_back(baseName(name) + ": " +
                              (ja ? "the Japanese PC release's version" : name == "THOTD.EXE" ? "a patched or other version" : "a different version of the file") +
                              ", accepted; an unpatched 1998 copy is recommended.");
        }
    }
    if (differ > 3)
        r.notes.push_back(std::to_string(differ - 3) + " more file(s) of another version accepted (the list: settings\\hotd.log).");
    if (silent)
        r.notes.push_back(std::to_string(silent) + " sound file(s) missing: those sounds stay silent.");
    for (const std::string& s : r.lines)
        if (s.rfind("note    SPRITE", 0) == 0)
            r.notes.push_back("SPRITE\\SPR.RBH not found: the simple menu instead of the PC game's menu art.");
    return r;
}

} // namespace hotd::game

namespace hotd::game {

std::string placementHint(const std::string& where, bool arcade)
{
    const std::string what = arcade ? "the arcade ROMs" : "the PC game";
    if (where.empty())
        return arcade ? "Choose your arcade ROM zip (MAME's hotd set) with [zip] next to Arcade ROMs, or put it in the package's "
                        "data\\arcade folder and start again."
                      : "Choose your PC game's folder (or a .zip of it) next to PC game folder, or put it in the package's data\\pc "
                        "folder and start again.";
    std::error_code ec;
    const fs::path p(where);
    if (!fs::exists(p, ec))
        return "That place does not exist any more (moved, renamed or a drive not connected): choose " + what + " again.";
    const std::string ext = lower(p.extension().string());
    if (ext == ".7z" || ext == ".rar")
        return "A " + ext + " archive is not read: unpack it (or repack it as a .zip) and choose that.";
    // pchotfix (player copies 2026-11-02): a disc image's kind from its first sectors: the SEGA Saturn version (its
    // boot sector "SEGA SEGASATURN") or a PC CD (ISO 9660); raw 2352-byte sectors or 2048-byte ones
    auto discKind = [](const std::vector<uint8_t>& d) -> int {   // 1 Saturn, 2 a PC CD, 0 unknown
        auto at = [&](size_t off, const char* s) {
            const size_t n = std::strlen(s);
            return d.size() >= off + n && std::memcmp(d.data() + off, s, n) == 0;
        };
        if (at(0, "SEGA SEGASATURN") || at(16, "SEGA SEGASATURN") || at(24, "SEGA SEGASATURN"))
            return 1;
        if (at(16 * 2048 + 1, "CD001") || at(16 * 2352 + 16 + 1, "CD001") || at(16 * 2352 + 24 + 1, "CD001"))
            return 2;
        return 0;
    };
    auto discHint = [&](int kind) -> std::string {
        if (kind == 1)
            return "That is the SEGA Saturn version (a disc image), not the PC game: ReWrithe needs the 1998 Windows PC version.";
        if (arcade)
            return "That is a disc image, not the arcade ROMs: the arcade ROMs are MAME's hotd zip.";
        return "A CD image is not read: copy all the files of the PC game's CD (open or mount the image) into a folder and "
               "choose that folder.";
    };
    auto fileHead = [](const std::string& path) {
        std::vector<uint8_t> d(17 * 2352 + 64);
        FILE* f = hotd::fopenLong(path);
        d.resize(f ? std::fread(d.data(), 1, d.size(), f) : 0);
        if (f) std::fclose(f);
        return d;
    };
    if (ext == ".iso" || ext == ".bin" || ext == ".cue" || ext == ".img" || ext == ".mdf" || ext == ".nrg" || ext == ".chd") {
        std::string image = where;
        if (ext == ".cue") {   // its first FILE "..."
            std::ifstream cue(where);
            std::string line;
            while (std::getline(cue, line))
                if (const size_t a = line.find('"'), b = line.rfind('"'); line.find("FILE") != std::string::npos && a != b) {
                    image = (p.parent_path() / line.substr(a + 1, b - a - 1)).string();
                    break;
                }
        }
        return discHint(discKind(fileHead(image)));
    }
    // what the place holds: the names of up to a few thousand files (a zip's entries, or a folder a few levels deep)
    std::vector<std::string> names;
    bool zipInZip = false;
    std::string innerZip;
    if (fs::is_directory(p, ec)) {
        int n = 0;
        for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator() && n < 4000; it.increment(ec), ++n) {
            if (ec) break;
            if (it.depth() > 3) { it.disable_recursion_pending(); continue; }
            if (it->is_regular_file(ec)) names.push_back(lower(it->path().filename().string()));
        }
    } else {
        ZipFile z;
        std::string err;
        if (!z.open(where, err))
            return "That file is not a zip (or it is damaged): choose " + what + " as a .zip or a folder.";
        const ZipFile::Entry* image = nullptr;   // pchotfix: a disc image in the zip (its first track)
        bool imageFirst = false;
        for (const ZipFile::Entry& e : z.entries()) {
            const std::string n = lower(baseName(e.name));
            names.push_back(n);
            const std::string ne = lower(e.name);
            const std::string ie = ne.size() > 4 ? ne.substr(ne.size() - 4) : "";
            const bool firstTrack = ne.find("track 01") != std::string::npos || ne.find("track 1)") != std::string::npos;
            if ((ie == ".bin" || ie == ".iso" || ie == ".img") && (!image || (firstTrack && !imageFirst))) {
                image = &e;
                imageFirst = firstTrack;
            }
            if (ne.size() > 4 && (ne.compare(ne.size() - 4, 4, ".zip") == 0 || ne.compare(ne.size() - 3, 3, ".7z") == 0 ||
                                  ne.compare(ne.size() - 4, 4, ".rar") == 0)) {
                zipInZip = true;
                if (innerZip.empty()) innerZip = e.name;
            }
        }
        if (image && std::find(names.begin(), names.end(), "thotd.exe") == names.end()) {
            std::vector<uint8_t> d;
            std::string rerr;
            if (z.read(*image, d, rerr)) {
                d.resize(std::min(d.size(), size_t(17 * 2352 + 64)));
                return discHint(discKind(d));
            }
            return discHint(0);
        }
    }
    bool hasPcExe = false, hasRomName = false, hasMameOther = false;
    for (const std::string& n : names) {
        hasPcExe |= n == "thotd.exe";
        hasRomName |= (n.rfind("mpr-197", 0) == 0 || n.rfind("epr-196", 0) == 0 || n.rfind("epr-197", 0) == 0);
        hasMameOther |= n.rfind("mpr-", 0) == 0 || n.rfind("epr-", 0) == 0;
    }
    if (arcade && hasPcExe)
        return "That is the PC game (THOTD.EXE is in it): choose it next to PC game folder; Arcade ROMs needs MAME's hotd zip.";
    if (!arcade && hasRomName && !hasPcExe)
        return "Those are the arcade ROMs: choose them next to Arcade ROMs; PC game folder needs the 1998 PC release.";
    if (zipInZip)
        return "This zip holds another archive (" + innerZip + "): unpack the outer one and choose the inner zip.";
    if (arcade && hasMameOther && !hasRomName)
        return "These look like ROMs of another game (or another revision): The House of the Dead needs MAME's hotd set (Revision A).";
    if (!arcade && !hasPcExe)
        return "No THOTD.EXE in there: choose the folder the PC game was installed to (where THOTD.EXE is), or a .zip of it.";
    if (!arcade && names.size() < 10)   // pchotfix: a no-CD patch's zip (THOTD.EXE alone)
        return "Only THOTD.EXE in there (a patch?): the PC game's other files (ART, SOUND, SPRITE ...) are needed too: "
               "choose the whole installed folder, or copy the patched THOTD.EXE into it.";
    return {};
}

} // namespace hotd::game

namespace hotd::game {

const ArcadeRomId* arcadeRomIds(int& n)
{
    static std::vector<ArcadeRomId> ids = [] {
        std::vector<ArcadeRomId> v;
        for (const Rom& r : kRoms)
            v.push_back({r.name, r.size, r.crc});
        return v;
    }();
    n = int(ids.size());
    return ids.data();
}

} // namespace hotd::game
