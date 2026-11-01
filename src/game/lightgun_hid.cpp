// Light Gun Edition: LightgunHID.ini (game/lightgun_hid.h).
#include "game/lightgun_hid.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hotd::game {

namespace {
std::string trim(const std::string& s)
{
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}
std::string upper(std::string s)
{
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
bool hex4(const std::string& s, size_t at)
{
    if (at + 4 > s.size()) return false;
    for (size_t i = at; i < at + 4; ++i)
        if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}
// "P1_LIGHTGUN_HID" .. "P4_LIGHTGUN_HID" -> 0..3, else -1
int playerOfKey(const std::string& keyUpper)
{
    for (int p = 0; p < LightgunHid::kPlayers; ++p)
        if (keyUpper == "P" + std::to_string(p + 1) + "_LIGHTGUN_HID") return p;
    return -1;
}
std::string sectionOf(const std::string& line)   // "[Lightguns]" -> "LIGHTGUNS", "" if not a section line
{
    const std::string t = trim(line);
    if (t.size() < 2 || t.front() != '[') return {};
    const size_t e = t.find(']');
    return upper(trim(t.substr(1, e == std::string::npos ? std::string::npos : e - 1)));
}
} // namespace

std::string LightgunHid::normalise(const std::string& value)
{
    std::string v = upper(trim(value));
    // "1209:0001", "0x1209:0x0001", "1209/0001", "1209,0001"
    std::string a, b;
    const size_t sep = v.find_first_of(":/,");
    if (sep != std::string::npos) {
        a = trim(v.substr(0, sep)), b = trim(v.substr(sep + 1));
        if (a.rfind("0X", 0) == 0) a = a.substr(2);
        if (b.rfind("0X", 0) == 0) b = b.substr(2);
        if (a.size() == 4 && b.size() == 4 && hex4(a, 0) && hex4(b, 0))
            return "VID_" + a + "&PID_" + b;
    }
    return v;
}

std::string LightgunHid::idOfPath(const std::string& path)
{
    const std::string u = upper(path);
    const size_t v = u.find("VID_");
    if (v == std::string::npos || !hex4(u, v + 4)) return {};
    const size_t p = u.find("PID_", v);
    if (p == std::string::npos || !hex4(u, p + 4)) return {};
    return "VID_" + u.substr(v + 4, 4) + "&PID_" + u.substr(p + 4, 4);
}

bool LightgunHid::load(const std::string& file)
{
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    LightgunHid c;
    std::string section, line;
    while (std::getline(f, line)) {
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF)
            line = line.substr(3);   // (a UTF-8 BOM from Notepad)
        const std::string t = trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t[0] == '[') { section = sectionOf(t); continue; }
        const size_t eq = t.find('=');
        if (eq == std::string::npos || section != "LIGHTGUNS") continue;
        std::string value = t.substr(eq + 1);
        if (const size_t sc = value.find(';'); sc != std::string::npos) value = value.substr(0, sc);   // a trailing comment
        const int p = playerOfKey(upper(trim(t.substr(0, eq))));
        if (p >= 0) c.id[p] = normalise(value);
    }
    *this = c;
    return true;
}

bool LightgunHid::blank() const
{
    for (const std::string& s : id)
        if (!s.empty()) return false;
    return true;
}

bool LightgunHid::save(const std::string& file) const
{
    // the file as it is (its other sections and comments kept), else a new one
    std::vector<std::string> lines;
    bool crlf = true;
    {
        std::ifstream f(file, std::ios::binary);
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            const std::string all = ss.str();
            crlf = all.find('\n') == std::string::npos || all.find("\r\n") != std::string::npos;
            std::string line;
            std::istringstream in(all);
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
            }
        }
    }
    auto keyLine = [&](int p) { return "P" + std::to_string(p + 1) + "_Lightgun_HID = " + id[p]; };
    bool written[kPlayers] = {};
    int sectionAt = -1, lastKeyAt = -1;
    std::string section;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string t = trim(lines[i]);
        if (!t.empty() && t[0] == '[') {
            section = sectionOf(t);
            if (section == "LIGHTGUNS" && sectionAt < 0) sectionAt = int(i), lastKeyAt = int(i);
            continue;
        }
        if (section != "LIGHTGUNS" || t.empty() || t[0] == ';' || t[0] == '#') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const int p = playerOfKey(upper(trim(t.substr(0, eq))));
        if (p < 0) continue;
        lines[i] = keyLine(p);
        written[p] = true;
        lastKeyAt = int(i);
    }
    std::vector<std::string> add;   // the keys not in the file yet: P1 / P2 always, P3 / P4 when set
    for (int p = 0; p < kPlayers; ++p)
        if (!written[p] && (p < 2 || !id[p].empty())) add.push_back(keyLine(p));
    if (sectionAt < 0) {
        if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back("");
        lines.push_back("[Lightguns]");
        lines.insert(lines.end(), add.begin(), add.end());
    } else {
        lines.insert(lines.begin() + (lastKeyAt + 1), add.begin(), add.end());
    }
    const std::string tmp = file + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        for (const std::string& l : lines) f << l << (crlf ? "\r\n" : "\n");
        if (!f) return false;
    }
    std::remove(file.c_str());
    return std::rename(tmp.c_str(), file.c_str()) == 0;
}

std::vector<std::string> LightgunHid::resolve(const std::vector<std::string>& paths) const
{
    std::vector<std::string> out(kPlayers);
    std::vector<std::string> pu(paths.size());
    for (size_t i = 0; i < paths.size(); ++i) pu[i] = upper(paths[i]);
    for (int p = 0; p < kPlayers; ++p) {
        if (id[p].empty()) continue;
        for (size_t i = 0; i < paths.size(); ++i) {
            if (pu[i].find(id[p]) == std::string::npos) continue;
            if (std::find(out.begin(), out.end(), paths[i]) != out.end()) continue;   // (one gun, one player)
            out[size_t(p)] = paths[i];
            break;
        }
    }
    return out;
}

std::vector<std::string> rawMousePaths()
{
    std::vector<std::string> out;
#ifdef _WIN32
    UINT n = 0;
    if (GetRawInputDeviceList(nullptr, &n, sizeof(RAWINPUTDEVICELIST)) != 0 || n == 0 || n > 256) return out;
    std::vector<RAWINPUTDEVICELIST> l(n);
    const UINT got = GetRawInputDeviceList(l.data(), &n, sizeof(RAWINPUTDEVICELIST));
    for (UINT i = 0; got != UINT(-1) && i < got; ++i) {
        if (l[i].dwType != RIM_TYPEMOUSE) continue;
        UINT len = 0;
        if (GetRawInputDeviceInfoA(l[i].hDevice, RIDI_DEVICENAME, nullptr, &len) != 0 || len == 0 || len > 1024) continue;
        std::string s(len, '\0');
        if (GetRawInputDeviceInfoA(l[i].hDevice, RIDI_DEVICENAME, s.data(), &len) == UINT(-1)) continue;
        s.resize(std::strlen(s.c_str()));
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));   // (as LightGuns' device keys)
        if (!s.empty()) out.push_back(s);
    }
    std::sort(out.begin(), out.end());
#endif
    return out;
}

} // namespace hotd::game
