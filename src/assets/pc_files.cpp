// The PC files from a folder or a zip (pc_files.h).
#include "assets/pc_files.h"

#include "assets/archive.h"
#include "util/file64.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <unordered_map>

namespace hotd::pcfs {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s)
{
    for (char& c : s)
        c = c == '\\' ? '/' : char(std::tolower(uint8_t(c)));
    return s;
}

bool endsWithNoCase(const std::string& s, const char* tail)
{
    const size_t n = std::char_traits<char>::length(tail);
    return s.size() >= n && lower(s.substr(s.size() - n)) == tail;
}

bool regularFile(const std::string& p, uint64_t* size = nullptr, int64_t* time = nullptr)
{
    std::error_code ec;
    const fs::path path(p);
    if (!fs::is_regular_file(path, ec))
        return false;
    if (size) *size = fs::file_size(path, ec);
    if (time) *time = int64_t(fs::last_write_time(path, ec).time_since_epoch().count());
    return true;
}

// "<zip>/<inside>": the first ".zip" path part that is a regular file
bool split(const std::string& path, std::string& zip, std::string& inner)
{
    const std::string l = lower(path);
    for (size_t i = l.find(".zip/"); i != std::string::npos; i = l.find(".zip/", i + 1)) {
        std::string z = path.substr(0, i + 4);
        if (regularFile(z)) {
            zip = std::move(z);
            inner = path.substr(i + 5);
            while (!inner.empty() && (inner[0] == '/' || inner[0] == '\\'))
                inner.erase(0, 1);
            return !inner.empty();
        }
    }
    return false;
}

struct Zip {
    ZipFile file;
    std::unordered_map<std::string, size_t> byName;   // lower-case name -> entry
    std::string root;                                 // the install's folder in the zip, lower case ("" or "x/")
    uint64_t size = 0;
    int64_t time = 0;
};

std::mutex& lock()
{
    static std::mutex m;
    return m;
}

// the open zips (re-read when the file changed)
std::shared_ptr<Zip> openZip(const std::string& path, std::string* err = nullptr)
{
    static std::map<std::string, std::shared_ptr<Zip>> zips;
    uint64_t size = 0;
    int64_t time = 0;
    if (!regularFile(path, &size, &time)) {
        if (err) *err = path + ": not found";
        return nullptr;
    }
    std::lock_guard<std::mutex> g(lock());
    auto it = zips.find(path);
    if (it != zips.end() && it->second->size == size && it->second->time == time)
        return it->second;
    auto z = std::make_shared<Zip>();
    std::string e;
    if (!z->file.open(path, e)) {
        if (err) *err = e;
        return nullptr;
    }
    z->size = size;
    z->time = time;
    const auto& entries = z->file.entries();
    bool top = false;
    std::string down;
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string n = lower(entries[i].name);
        z->byName.emplace(n, i);
        if (n == "thotd.exe" || n.rfind("art/", 0) == 0 || n.rfind("sound/", 0) == 0)
            top = true;
        const size_t s = n.find('/');
        if (down.empty() && s != std::string::npos && s + 1 < n.size()) {
            const std::string rest = n.substr(s + 1);
            if (rest == "thotd.exe" || rest.rfind("art/stage1/", 0) == 0)
                down = n.substr(0, s + 1);
        }
    }
    z->root = top ? "" : down;
    zips[path] = z;
    return z;
}

const ZipFile::Entry* find(const Zip& z, const std::string& inner)
{
    const std::string k = lower(inner);
    auto it = z.byName.find(k);
    if (it == z.byName.end() && !z.root.empty())
        it = z.byName.find(z.root + k);
    return it == z.byName.end() ? nullptr : &z.file.entries()[it->second];
}

// the last inflated entries (BIGGER.VMC is read by the textures and the sprites, a stage after another)
std::shared_ptr<const std::vector<uint8_t>> inflated(const Zip& z, const ZipFile::Entry& e, std::string* err)
{
    static std::deque<std::pair<std::string, std::shared_ptr<const std::vector<uint8_t>>>> recent;
    const std::string key = z.file.path() + "|" + std::to_string(z.time) + "|" + e.name;
    {
        std::lock_guard<std::mutex> g(lock());
        for (const auto& [k, d] : recent)
            if (k == key)
                return d;
    }
    auto d = std::make_shared<std::vector<uint8_t>>();
    std::string why;
    if (!z.file.read(e, *d, why)) {
        if (err) *err = why;
        return nullptr;
    }
    std::lock_guard<std::mutex> g(lock());
    recent.emplace_front(key, d);
    uint64_t total = 0;
    for (size_t i = 0; i < recent.size(); ++i) {
        total += recent[i].second->size();
        if (i >= 3 || (i > 0 && total > (160u << 20))) {
            recent.resize(i);
            break;
        }
    }
    return d;
}

// a real file's path as it is on disk: Windows' file names match whatever their case; elsewhere (Linux, a case-sensitive
// macOS volume) a missing path is looked up a part at a time ignoring case, so "ART/STAGE1/..." finds a copy of the CD
// whose names became "art/stage1/..." or "Art/Stage1/...". Unchanged when nothing matches.
std::string onDisk(const std::string& path)
{
#ifdef _WIN32
    return path;
#else
    std::error_code ec;
    if (path.empty() || fs::exists(fs::path(path), ec))
        return path;
    fs::path p = path[0] == '/' ? fs::path("/") : fs::path();
    size_t s = 0;
    while (s <= path.size()) {
        const size_t e = std::min(path.find_first_of("/\\", s), path.size());
        const std::string part = path.substr(s, e - s);
        s = e + 1;
        if (part.empty() || part == ".")
            continue;
        fs::path next = p.empty() ? fs::path(part) : p / part;
        if (part != ".." && !fs::exists(next, ec)) {
            bool found = false;
            const std::string want = lower(part);
            for (const auto& entry : fs::directory_iterator(p.empty() ? fs::path(".") : p, ec)) {
                const std::string n = entry.path().filename().string();
                if (n.size() == part.size() && lower(n) == want) {
                    next = p.empty() ? fs::path(n) : p / n;
                    found = true;
                    break;
                }
            }
            if (!found)
                return path;
        }
        p = next;
    }
    return p.string();
#endif
}

} // namespace

bool isZip(const std::string& path)
{
    return endsWithNoCase(path, ".zip") && regularFile(path);
}

bool inZip(const std::string& path)
{
    std::string z, i;
    return split(path, z, i);
}

std::string unsupportedArchive(const std::string& path)
{
    for (const char* ext : {".rar", ".7z"})
        if (endsWithNoCase(path, ext))
            return std::filesystem::path(path).filename().string() + " is a " + std::string(ext + 1) +
                   " archive that could not be opened: unpack it (or repack it as a .zip) and choose that.";
    return "";
}

bool exists(const std::string& path)
{
    return size(path) >= 0;
}

int64_t size(const std::string& path)
{
    std::string zip, inner;
    if (split(path, zip, inner)) {
        auto z = openZip(zip);
        const ZipFile::Entry* e = z ? find(*z, inner) : nullptr;
        return e ? int64_t(e->size) : -1;
    }
    uint64_t s = 0;
    return regularFile(onDisk(path), &s) ? int64_t(s) : -1;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out, std::string* err)
{
    std::string zip, inner;
    if (split(path, zip, inner)) {
        auto z = openZip(zip, err);
        const ZipFile::Entry* e = z ? find(*z, inner) : nullptr;
        if (!e) {
            if (z && err) *err = path + ": not in the zip";
            return false;
        }
        std::string why;
        if (!z->file.read(*e, out, why)) {
            if (err) *err = why;
            return false;
        }
        return true;
    }
    FILE* f = hotd::fopenLong(onDisk(path));
    if (!f) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    hotd::fseek64(f, 0, SEEK_END);
    const int64_t n = hotd::ftell64(f);
    hotd::fseek64(f, 0, SEEK_SET);
    out.resize(size_t(n > 0 ? n : 0));
    const bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    if (!ok && err) *err = "cannot read " + path;
    return ok;
}

namespace {

// a stored zip entry: [base, base + size) of the zip file, read in 64 KB blocks
class SegmentBuf : public std::streambuf {
public:
    SegmentBuf(FILE* f, uint64_t base, uint64_t size) : f_(f), base_(base), size_(size) { setg(buf_, buf_, buf_); }
    ~SegmentBuf() override { std::fclose(f_); }

protected:
    int_type underflow() override
    {
        if (gptr() < egptr())
            return traits_type::to_int_type(*gptr());
        start_ += uint64_t(egptr() - eback());
        setg(buf_, buf_, buf_);
        if (start_ >= size_)
            return traits_type::eof();
        const size_t n = size_t(std::min<uint64_t>(sizeof buf_, size_ - start_));
        if (hotd::fseek64(f_, int64_t(base_ + start_), SEEK_SET) != 0 || std::fread(buf_, 1, n, f_) != n)
            return traits_type::eof();
        setg(buf_, buf_, buf_ + n);
        return traits_type::to_int_type(*gptr());
    }
    pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode) override
    {
        const int64_t cur = int64_t(start_) + (gptr() - eback());
        const int64_t to = (dir == std::ios_base::beg ? 0 : dir == std::ios_base::cur ? cur : int64_t(size_)) + int64_t(off);
        if (to < 0 || uint64_t(to) > size_)
            return pos_type(off_type(-1));
        if (uint64_t(to) >= start_ && uint64_t(to) <= start_ + uint64_t(egptr() - eback())) {
            setg(eback(), eback() + (uint64_t(to) - start_), egptr());
        } else {
            start_ = uint64_t(to);
            setg(buf_, buf_, buf_);
        }
        return pos_type(off_type(to));
    }
    pos_type seekpos(pos_type p, std::ios_base::openmode m) override { return seekoff(off_type(p), std::ios_base::beg, m); }

private:
    FILE* f_;
    uint64_t base_, size_, start_ = 0;   // start_: the file offset of buf_[0] (relative to base_)
    char buf_[65536];
};

// a deflated entry, inflated
class MemoryBuf : public std::streambuf {
public:
    explicit MemoryBuf(std::shared_ptr<const std::vector<uint8_t>> d) : d_(std::move(d))
    {
        char* p = const_cast<char*>(reinterpret_cast<const char*>(d_->data()));
        setg(p, p, p + d_->size());
    }

protected:
    pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode) override
    {
        const off_type cur = gptr() - eback(), end = egptr() - eback();
        const off_type to = (dir == std::ios_base::beg ? 0 : dir == std::ios_base::cur ? cur : end) + off;
        if (to < 0 || to > end)
            return pos_type(off_type(-1));
        setg(eback(), eback() + to, egptr());
        return pos_type(to);
    }
    pos_type seekpos(pos_type p, std::ios_base::openmode m) override { return seekoff(off_type(p), std::ios_base::beg, m); }

private:
    std::shared_ptr<const std::vector<uint8_t>> d_;
};

class OwnedStream : public std::istream {
public:
    explicit OwnedStream(std::unique_ptr<std::streambuf> b) : std::istream(b.get()), buf_(std::move(b)) {}

private:
    std::unique_ptr<std::streambuf> buf_;
};

} // namespace

std::unique_ptr<std::istream> openStream(const std::string& path)
{
    std::string zip, inner;
    if (!split(path, zip, inner)) {
        auto f = std::make_unique<std::ifstream>(hotd::longPath(onDisk(path)), std::ios::binary);
        if (!*f)
            return nullptr;
        return f;
    }
    auto z = openZip(zip);
    const ZipFile::Entry* e = z ? find(*z, inner) : nullptr;
    if (!e)
        return nullptr;
    std::string err;
    if (e->method == 0 && e->compSize == e->size) {
        uint64_t base = 0;
        FILE* f = nullptr;
        if (!z->file.dataOffset(*e, base, err) || !(f = hotd::fopenLong(zip)))
            return nullptr;
        return std::make_unique<OwnedStream>(std::make_unique<SegmentBuf>(f, base, e->size));
    }
    auto d = inflated(*z, *e, &err);
    if (!d)
        return nullptr;
    return std::make_unique<OwnedStream>(std::make_unique<MemoryBuf>(std::move(d)));
}

bool listZip(const std::string& zip, std::vector<ZipItem>& out, std::string& err)
{
    out.clear();
    auto z = openZip(zip, &err);
    if (!z)
        return false;
    for (const ZipFile::Entry& e : z->file.entries()) {
        ZipItem it;
        it.rel = lower(e.name);
        if (!z->root.empty() && it.rel.rfind(z->root, 0) == 0)
            it.rel.erase(0, z->root.size());
        it.path = zip + "/" + e.name;
        it.size = e.size;
        it.crc = e.crc;
        out.push_back(std::move(it));
    }
    return true;
}

} // namespace hotd::pcfs
