// subs: the subtitle files and the Subtitles settings (subtitles.h, docs/subtitles.md)
#include "game/subtitles.h"

#include "game/lang.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace hotd::subs {

namespace {

namespace fs = std::filesystem;

struct State {
    int mode = kCustom, size = 100;
    std::vector<std::string> modDirs;
    std::string prefDir;
    // the loaded file: its language, the folders' generation, its path and time (checked once a second at most)
    std::string code = "?", file;
    uint64_t langGen = 0, dirGen = 1, loadedDirGen = 0;
    fs::file_time_type time{};
    long long checkedAt = -1000;
    std::map<int, Entry> entries;
};

State& st()
{
    static State s;
    return s;
}

std::string trim(const std::string& s)
{
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// \n a line break, \\ a backslash (the rest as written)
std::string unescape(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == 'n' || s[i + 1] == '\\')) {
            o += s[i + 1] == 'n' ? '\n' : '\\';
            ++i;
        } else
            o += s[i];
    }
    return o;
}

// "+12", "-5", "250ms", "-0.5s": frames (60 a second)
bool parseTime(const std::string& v, int& out)
{
    if (v.empty()) return false;
    char* endp = nullptr;
    const double n = std::strtod(v.c_str(), &endp);
    const std::string unit = endp ? std::string(endp) : std::string();
    if (endp == v.c_str()) return false;
    if (unit.empty() || unit == "f") out = int(std::lround(n));
    else if (unit == "ms") out = int(std::lround(n * 60.0 / 1000.0));
    else if (unit == "s") out = int(std::lround(n * 60.0));
    else return false;
    return true;
}

std::vector<fs::path> candidates(const std::string& code)
{
    State& s = st();
    std::vector<fs::path> v;
    const std::string name = code + ".txt";
    for (const std::string& d : s.modDirs) v.push_back(fs::path(d) / "subtitles" / name);
    if (!s.prefDir.empty()) v.push_back(fs::path(s.prefDir) / "subtitles" / name);
    for (const std::string& d : lang::dataDirs()) v.push_back(fs::path(d) / "data" / "subtitles" / name);
    return v;
}

long long nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void refresh()
{
    State& s = st();
    const std::string code = lang::code();
    const bool changed = code != s.code || lang::gen() != s.langGen || s.dirGen != s.loadedDirGen;
    const long long now = nowMs();
    if (!changed && now - s.checkedAt < 1000) return;
    s.checkedAt = now;
    std::error_code ec;
    std::string file;
    for (const fs::path& p : candidates(code))
        if (fs::is_regular_file(p, ec)) {
            file = p.string();
            break;
        }
    const fs::file_time_type t = file.empty() ? fs::file_time_type{} : fs::last_write_time(file, ec);
    if (!changed && file == s.file && t == s.time) return;
    s.code = code, s.langGen = lang::gen(), s.loadedDirGen = s.dirGen;
    s.file = file, s.time = t;
    s.entries.clear();
    if (file.empty()) return;
    std::ifstream f(fs::u8path(file), std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::vector<std::string> errors;
    s.entries = parse(ss.str(), &errors);
    if (trace() || !errors.empty()) {
        std::fprintf(stderr, "subs: %s: %zu line(s)\n", file.c_str(), s.entries.size());
        for (const std::string& e : errors) std::fprintf(stderr, "subs: %s\n", e.c_str());
    }
}

} // namespace

bool Entry::neutral(const std::string& gameText) const
{
    return !hide && start == 0 && end == 0 && size == 100 && place == kPlaceDefault && (!hasText || trim(text) == trim(gameText));
}

void configure(int mode, int sizePercent)
{
    st().mode = std::clamp(mode, 0, 2);
    st().size = std::clamp(sizePercent, 25, 400);
}
int mode() { return st().mode; }
int sizePercent() { return st().size; }

const char* modeName(int m) { return m == kOff ? "Off" : m == kOriginal ? "Original" : "Custom"; }

int modeParse(const std::string& v)
{
    std::string s;
    for (char c : v) s += char(std::tolower(static_cast<unsigned char>(c)));
    if (s == "off" || s == "0") return kOff;
    if (s == "original" || s == "1") return kOriginal;
    if (s == "custom" || s == "2") return kCustom;
    return -1;
}

const std::vector<int>& sizeSteps()
{
    static const std::vector<int> v = {75, 100, 125, 150, 175, 200};
    return v;
}

void setModDirs(const std::vector<std::string>& dirs)
{
    if (dirs != st().modDirs) {
        st().modDirs = dirs;
        ++st().dirGen;
    }
}

void setPrefDir(const std::string& dir)
{
    if (dir != st().prefDir) {
        st().prefDir = dir;
        ++st().dirGen;
    }
}

const Entry* find(int event, int line)
{
    if (line < 1 || line > 2) return nullptr;
    refresh();
    const auto& m = st().entries;
    auto it = m.find(event * 2 + line - 1);
    return it != m.end() ? &it->second : nullptr;
}

const std::string& fileInUse()
{
    refresh();
    return st().file;
}

// <event>.<line> [start=..] [end=..] [size=..] [pos=top|bottom|<y>] [hide] = text
std::map<int, Entry> parse(const std::string& text, std::vector<std::string>* errors)
{
    std::map<int, Entry> out;
    std::istringstream in(text);
    std::string raw;
    int n = 0;
    auto bad = [&](const std::string& why) {
        if (errors) errors->push_back("line " + std::to_string(n) + ": " + why);
    };
    while (std::getline(in, raw)) {
        ++n;
        if (n == 1 && raw.size() >= 3 && uint8_t(raw[0]) == 0xEF && uint8_t(raw[1]) == 0xBB && uint8_t(raw[2]) == 0xBF)
            raw = raw.substr(3);
        const std::string t = trim(raw);
        if (t.empty() || t[0] == '#') continue;
        // the head (id and options) ends at the first " = " (or a last "=" with nothing after: the game's text)
        std::string head = t, body;
        bool hasText = false;
        if (const size_t eq = t.find(" = "); eq != std::string::npos) {
            head = trim(t.substr(0, eq));
            body = t.substr(eq + 3);
            hasText = true;
        } else if (t.back() == '=') {
            head = trim(t.substr(0, t.size() - 1));
        }
        std::istringstream hs(head);
        std::string id;
        hs >> id;
        int e = -1, l = -1;
        char tail = 0;
        if (std::sscanf(id.c_str(), "%d.%d%c", &e, &l, &tail) != 2 || e < 0 || e > 255 || l < 1 || l > 2) {
            bad("not <event>.<line> (e.g. 1.1): " + id);
            continue;
        }
        Entry en;
        en.hasText = hasText;
        en.text = unescape(body);
        bool ok = true;
        for (std::string opt; hs >> opt;) {
            const size_t q = opt.find('=');
            std::string k = q == std::string::npos ? opt : opt.substr(0, q), v = q == std::string::npos ? "" : opt.substr(q + 1);
            for (char& c : k) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (k == "hide") en.hide = true;
            else if (k == "start" && parseTime(v, en.start)) {}
            else if (k == "end" && parseTime(v, en.end)) {}
            else if (k == "size" && !v.empty()) {
                if (v.back() == '%') v.pop_back();
                en.size = std::clamp(std::atoi(v.c_str()), 10, 400);
            } else if (k == "pos" && (v == "top" || v == "bottom")) en.place = v == "top" ? kPlaceTop : kPlaceBottom;
            else if (k == "pos" && !v.empty() && (std::isdigit(static_cast<unsigned char>(v[0])) || v[0] == '.')) {
                if (v.back() == '%') v.pop_back();
                en.place = kPlaceY;
                en.y = std::clamp(float(std::atof(v.c_str())), 0.0f, 100.0f);
            } else {
                bad("unknown option: " + opt);
                ok = false;
            }
        }
        if (ok) out[e * 2 + l - 1] = en;
    }
    return out;
}

bool trace()
{
    static const bool on = [] {
        const char* v = std::getenv("HOTD_TRACE_SUBS");
        return v && *v && *v != '0';
    }();
    return on;
}

} // namespace hotd::subs
