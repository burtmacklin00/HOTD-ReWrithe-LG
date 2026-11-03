// ISO 9660 from a CD image (disc_image.h).
#include "util/disc_image.h"

#include "util/file64.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace hotd::disc {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s)
{
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// the image's data file and its sectors: `size` bytes a sector, the user data at `offset`
struct Image {
    std::FILE* f = nullptr;
    int size = 2048, offset = 0;
    ~Image() { if (f) std::fclose(f); }
    bool sector(uint32_t lba, uint8_t* out) const
    {
        if (hotd::fseek64(f, int64_t(lba) * size + offset, SEEK_SET) != 0) return false;
        return std::fread(out, 1, 2048, f) == 2048;
    }
};

// a .cue's first FILE "<name>" (relative to the .cue's folder)
std::string cueData(const std::string& cue)
{
    std::ifstream f(hotd::longPath(cue));
    std::string line;
    while (std::getline(f, line)) {
        const size_t k = line.find("FILE");
        if (k == std::string::npos) continue;
        const size_t q1 = line.find('"', k), q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
        std::string name = q2 == std::string::npos ? std::string() : line.substr(q1 + 1, q2 - q1 - 1);
        if (name.empty()) {   // FILE name.bin BINARY (no quotes)
            std::string rest = line.substr(k + 4);
            rest.erase(0, rest.find_first_not_of(" \t"));
            name = rest.substr(0, rest.find_first_of(" \t"));
        }
        if (name.empty()) return {};
        return (fs::path(cue).parent_path() / name).string();
    }
    return {};
}

bool open(const std::string& path, Image& img, std::string& err)
{
    std::string data = path;
    if (lower(fs::path(path).extension().string()) == ".cue") {
        data = cueData(path);
        if (data.empty()) { err = "the .cue names no data file"; return false; }
    }
    img.f = hotd::fopenLong(data);
    if (!img.f) { err = "cannot open " + fs::path(data).filename().string(); return false; }
    uint8_t head[16] = {};
    if (std::fread(head, 1, 16, img.f) != 16) { err = "the image is too small"; return false; }
    static const uint8_t kSync[12] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};
    if (std::memcmp(head, kSync, 12) == 0) {   // raw sectors: the mode byte says where the 2048 bytes are
        img.size = 2352;
        img.offset = head[15] == 2 ? 24 : 16;
    }
    // the primary volume descriptor at sector 16 ("CD001"); a raw image whose first sector is audio: try both modes
    uint8_t s[2048];
    for (int tries = 0; tries < 3; ++tries) {
        if (img.sector(16, s) && s[0] == 1 && std::memcmp(s + 1, "CD001", 5) == 0) return true;
        if (tries == 0 && img.size == 2048) { img.size = 2352, img.offset = 16; continue; }
        if (img.size == 2352) img.offset = img.offset == 16 ? 24 : 16;
    }
    err = "no ISO 9660 data track (an audio CD or another format)";
    return false;
}

struct Entry {
    std::string path;   // relative, '/'
    uint32_t lba = 0, size = 0;
};

// the tree from the root directory (depth-limited; a loop or a damaged record ends that branch)
void walk(const Image& img, uint32_t lba, uint32_t size, const std::string& prefix, int depth, std::vector<Entry>& out)
{
    if (depth > 8 || size > 16u * 1024 * 1024) return;
    std::vector<uint8_t> dir;
    for (uint32_t done = 0, k = 0; done < size; done += 2048, ++k) {
        uint8_t s[2048];
        if (!img.sector(lba + k, s)) return;
        dir.insert(dir.end(), s, s + 2048);
    }
    for (size_t at = 0; at < size && at < dir.size();) {
        const uint8_t len = dir[at];
        if (len == 0) { at = (at / 2048 + 1) * 2048; continue; }   // records never cross a sector
        if (at + len > dir.size() || len < 34) break;
        const uint8_t* r = &dir[at];
        const uint32_t elba = le32(r + 2), esize = le32(r + 10);
        const bool isDir = (r[25] & 2) != 0;
        const uint8_t nlen = r[32];
        std::string name(reinterpret_cast<const char*>(r + 33), std::min<size_t>(nlen, size_t(len) - 33));
        at += len;
        if (nlen == 1 && (name[0] == 0 || name[0] == 1)) continue;   // . and ..
        if (const size_t semi = name.find(';'); semi != std::string::npos) name.resize(semi);
        if (!name.empty() && name.back() == '.') name.pop_back();
        if (name.empty() || name.find("..") != std::string::npos || name.find_first_of("/\\:") != std::string::npos)
            continue;
        if (isDir) walk(img, elba, esize, prefix + name + "/", depth + 1, out);
        else out.push_back({prefix + name, elba, esize});
    }
}

bool list(const std::string& path, Image& img, std::vector<Entry>& out, Info& info)
{
    if (!open(path, img, info.err)) return false;
    uint8_t s[2048];
    if (img.sector(0, s)) {   // a console disc's system area: its maker's header in the first sector
        if (std::memcmp(s, "SEGA SEGASATURN", 15) == 0) info.system = "saturn";
        else if (std::memcmp(s, "SEGA SEGAKATANA", 15) == 0) info.system = "dreamcast";
    }
    uint8_t pvd[2048];
    img.sector(16, pvd);
    if (std::memcmp(pvd + 8, "PLAYSTATION", 11) == 0) info.system = "playstation";
    std::string vol(reinterpret_cast<const char*>(pvd + 40), 32);
    vol.erase(vol.find_last_not_of(' ') + 1);
    info.volume = vol;
    walk(img, le32(pvd + 156 + 2), le32(pvd + 156 + 10), "", 0, out);
    info.ok = true;
    for (const Entry& e : out) info.bytes += e.size;
    info.files = int(out.size());
    return true;
}

} // namespace

bool isDiscImage(const std::string& path)
{
    const std::string e = lower(fs::path(path).extension().string());
    return e == ".iso" || e == ".img" || e == ".bin" || e == ".cue";
}

Info probe(const std::string& path)
{
    Info info;
    Image img;
    std::vector<Entry> entries;
    list(path, img, entries, info);
    return info;
}

bool extract(const std::string& path, const std::string& outDir, const std::function<void(float)>& progress, std::string& err)
{
    Info info;
    Image img;
    std::vector<Entry> entries;
    if (!list(path, img, entries, info)) { err = info.err; return false; }
    std::error_code ec;
    uint64_t done = 0;
    std::vector<uint8_t> buf(2048);
    for (const Entry& e : entries) {
        const fs::path out = fs::path(outDir) / fs::path(e.path);
        fs::create_directories(out.parent_path(), ec);
        std::ofstream f(hotd::longPath(out.string()), std::ios::binary | std::ios::trunc);
        if (!f) { err = "cannot write " + out.string(); return false; }
        for (uint32_t k = 0, left = e.size; left > 0; ++k) {
            if (!img.sector(e.lba + k, buf.data())) { err = "the image ends inside " + e.path; return false; }
            const uint32_t n = std::min<uint32_t>(left, 2048);
            f.write(reinterpret_cast<const char*>(buf.data()), n);
            left -= n;
            done += n;
            if (progress && (k & 255) == 0 && info.bytes) progress(float(double(done) / double(info.bytes)));
        }
        if (!f) { err = "cannot write " + out.string() + " (disk full?)"; return false; }
    }
    if (progress) progress(1.0f);
    return true;
}

std::string cueFor(const std::string& path)
{
    if (lower(fs::path(path).extension().string()) == ".cue") return path;
    std::error_code ec;
    const std::string want = lower(fs::path(path).filename().string());
    for (const auto& e : fs::directory_iterator(fs::path(path).parent_path(), ec)) {
        if (lower(e.path().extension().string()) != ".cue") continue;
        std::ifstream f(e.path());
        for (std::string line; std::getline(f, line);)
            if (line.find("FILE") != std::string::npos && lower(line).find(want) != std::string::npos) return e.path().string();
    }
    return {};
}

int ripAudio(const std::string& path, const std::string& outDir, std::string& err)
{
    const std::string cue = cueFor(path);
    if (cue.empty()) return 0;   // (an .iso / a .bin alone: no audio tracks known)
    // the cue's tracks: file, number, audio or not, INDEX 00 / 01 in frames (1/75 s = one 2352-byte sector)
    struct Track { std::string file; int number = 0; bool audio = false; int64_t i0 = -1, i1 = -1; };
    std::vector<Track> tracks;
    {
        std::ifstream f(hotd::longPath(cue));
        std::string file;
        for (std::string line; std::getline(f, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::string t = line;
            t.erase(0, std::min(t.size(), t.find_first_not_of(" \t")));
            if (t.rfind("FILE", 0) == 0) {
                const size_t q1 = t.find('"'), q2 = q1 == std::string::npos ? q1 : t.find('"', q1 + 1);
                std::string name = q2 == std::string::npos ? std::string() : t.substr(q1 + 1, q2 - q1 - 1);
                if (name.empty()) {
                    std::string rest = t.substr(4);
                    rest.erase(0, std::min(rest.size(), rest.find_first_not_of(" \t")));
                    name = rest.substr(0, rest.find_first_of(" \t"));
                }
                file = (fs::path(cue).parent_path() / name).string();
            } else if (t.rfind("TRACK", 0) == 0) {
                Track tr;
                tr.file = file;
                tr.number = std::atoi(t.c_str() + 5);
                tr.audio = t.find("AUDIO") != std::string::npos;
                tracks.push_back(tr);
            } else if (t.rfind("INDEX", 0) == 0 && !tracks.empty()) {
                int idx = 0, mm = 0, ss = 0, ff = 0;
                if (std::sscanf(t.c_str() + 5, "%d %d:%d:%d", &idx, &mm, &ss, &ff) == 4) {
                    const int64_t at = (int64_t(mm) * 60 + ss) * 75 + ff;
                    if (idx == 0) tracks.back().i0 = at;
                    else if (idx == 1) tracks.back().i1 = at;
                }
            }
        }
    }
    std::error_code ec;
    int n = 0;
    for (size_t i = 0; i < tracks.size(); ++i) {
        const Track& t = tracks[i];
        if (!t.audio || t.i1 < 0 || t.number < 1 || t.number > 99) continue;
        std::FILE* f = hotd::fopenLong(t.file);
        if (!f) { err = "cannot read " + t.file; continue; }
        hotd::fseek64(f, 0, SEEK_END);
        const int64_t fileBytes = hotd::ftell64(f);
        // from INDEX 01 (the pregap left out) to the next track's start in the same file, else the file's end
        int64_t endBytes = fileBytes;
        if (i + 1 < tracks.size() && tracks[i + 1].file == t.file) {
            const Track& nx = tracks[i + 1];
            endBytes = std::min(fileBytes, (nx.i0 >= 0 ? nx.i0 : nx.i1) * 2352);
        }
        const int64_t start = t.i1 * 2352;
        if (endBytes <= start) { std::fclose(f); continue; }
        std::vector<int16_t> pcm(size_t((endBytes - start) / 4) * 2);
        hotd::fseek64(f, start, SEEK_SET);
        const size_t got = std::fread(pcm.data(), 4, pcm.size() / 2, f);
        std::fclose(f);
        pcm.resize(got * 2);
        // the CD's tracks end in ~15 s of silence: cut to 1.9 s after the last sound, as the PC rips do (the tracks
        // loop: no long gap before each start)
        // (the last 0.5 s block louder than -50 dB RMS: the end's faint noise does not count, nor a lone click in the
        // silence: a sound of under 1 s with 2 s of silence before it)
        const size_t block = 22050;
        auto loud = [&](size_t end) {   // the block ending at frame `end`
            double sum = 0;
            for (size_t k = (end - block) * 2; k < end * 2; ++k) sum += double(pcm[k]) * double(pcm[k]);
            return std::sqrt(sum / double(block * 2)) >= 104.0;
        };
        size_t last = pcm.size() / 2 / block * block;
        while (last >= block) {
            if (!loud(last)) { last -= block; continue; }
            size_t a = last;   // the loud run's start
            while (a >= block && loud(a)) a -= block;
            size_t q = a;      // the silence before it
            while (q >= block && !loud(q) && a - q < 4 * block) q -= block;
            if (last - a < 2 * block && a - q >= 4 * block) { last = a; continue; }
            break;
        }
        const size_t frames = std::min(pcm.size() / 2, last + size_t(1.9 * 44100));
        if (frames == 0) continue;
        fs::create_directories(outDir, ec);
        char name[32];
        std::snprintf(name, sizeof name, "Track%02d.wav", t.number);
        std::ofstream o(fs::path(outDir) / name, std::ios::binary | std::ios::trunc);
        auto put32 = [&](uint32_t v) { const char b[4] = {char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; o.write(b, 4); };
        auto put16 = [&](uint16_t v) { const char b[2] = {char(v), char(v >> 8)}; o.write(b, 2); };
        const uint32_t bytes = uint32_t(frames * 4);
        o.write("RIFF", 4), put32(36 + bytes), o.write("WAVEfmt ", 8), put32(16), put16(1), put16(2), put32(44100), put32(44100 * 4),
            put16(4), put16(16), o.write("data", 4), put32(bytes);
        o.write(reinterpret_cast<const char*>(pcm.data()), std::streamsize(bytes));   // (CD audio: little-endian, as a .wav)
        if (!o) { err = "cannot write " + (fs::path(outDir) / name).string(); continue; }
        ++n;
    }
    return n;
}

} // namespace hotd::disc
