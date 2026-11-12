#include "game/sights.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

namespace hotd::game {

namespace fs = std::filesystem;

const char* const kSizeNames[kSizeSteps] = {"tiny", "small", "medium", "large", "huge"};
const char* const kThickNames[kThickSteps] = {"thin", "medium", "thick", "bold"};
const char* const kSightColourNames[kSightColours] = {"Player Color", "White", "Red",    "Blue", "Green",
                                                      "Yellow",        "Orange", "Pink", "Cyan", "Purple"};
uint32_t sightColourRgb(int colour)
{
    static const uint32_t k[kSightColours] = {0, 0xF4F4F4, 0xFF3C3C, 0x5AAAFF, 0x46EB46, 0xFFE128, 0xFF9628, 0xFF6EC8, 0x3CF0F0, 0xB46EFF};
    return k[std::clamp(colour, 0, kSightColours - 1)];
}

namespace {
std::string lower(std::string s)
{
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}

const char* sightStyleName(int style)
{
    static const char* const k[kSightStyles] = {"auto (follows the models)", "arcade style", "PC (its sight)", "simple cross",
                                                "ring + dot", "dotted ring", "corner brackets", "gap cross", "pointer", "custom",
                                                "capped cross", "off (none drawn)"};
    return k[std::clamp(style, 0, kSightStyles - 1)];
}

int sightStyleFromWord(const std::string& word, std::string& custom)
{
    const std::string w = lower(word);
    if (w.rfind("custom:", 0) == 0) { custom = word.substr(7); return kSightCustom; }
    static const char* const k[kSightStyles] = {"auto", "arcade", "pc", "cross", "ring", "", "brackets", "gap", "", "",
                                                "capped", "off"};   // 5 / 8 removed, 9 custom:<name>
    for (int i = 0; i < kSightStyles; ++i)
        if (k[i][0] && w == k[i]) return i;
    return w == "none" ? kSightOff : -1;
}

float sightSizeFactor(int step) { static const float f[kSizeSteps] = {0.6f, 0.8f, 1.0f, 1.3f, 1.6f}; return f[std::clamp(step, 0, kSizeSteps - 1)]; }
float sightThicknessPx(int step) { return float(std::clamp(step, 0, kThickSteps - 1) + 1); }

std::vector<CustomSight> findCustomSights(const std::vector<std::string>& dirs)
{
    std::map<std::string, CustomSight> found;
    std::error_code ec;
    // the PNGs directly in a folder: crosshair.png -> tinted; p1.png (+ p2.png) -> as they are
    auto scan = [&](const fs::path& dir, const std::string& name, bool root) {
        std::map<std::string, fs::path> png;   // lower-case file name -> path
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file(ec) && lower(e.path().extension().string()) == ".png")
                png[lower(e.path().filename().string())] = e.path();
        std::vector<CustomSight> out;
        if (auto it = png.find("crosshair.png"); it != png.end())
            out.push_back({name, it->second.string(), "", true});
        else if (auto p1 = png.find("p1.png"); p1 != png.end())
            out.push_back({name, p1->second.string(), png.count("p2.png") ? png["p2.png"].string() : "", false});
        if (root)   // loose pictures in the folder itself: each one a tinted choice named after its file
            for (const auto& [fn, path] : png)
                if (fn != "crosshair.png" && fn != "p1.png" && fn != "p2.png")
                    out.push_back({path.stem().string(), path.string(), "", true});
        for (auto& c : out) found.emplace(lower(c.name), c);
    };
    for (const std::string& d : dirs) {
        if (d.empty() || !fs::is_directory(d, ec))
            continue;
        scan(d, "crosshair", true);
        for (const auto& e : fs::directory_iterator(d, ec))
            if (e.is_directory(ec))
                scan(e.path(), e.path().filename().string(), false);
    }
    std::vector<CustomSight> v;
    for (auto& [k, c] : found) v.push_back(c);
    return v;
}

void nextSight(int& style, std::string& custom, const std::vector<CustomSight>& sets)
{
    if (style == kSightCustom) {
        for (size_t i = 0; i < sets.size(); ++i)
            if (lower(sets[i].name) == lower(custom)) {
                if (i + 1 < sets.size()) { custom = sets[i + 1].name; return; }
                break;
            }
        style = 0;
        custom.clear();
        return;
    }
    // the built-in styles in order, the capped cross (10) after the gap cross (7), Off (11) after it, then the custom sets
    int n = style == 7 ? kSightCapped : style == kSightCapped ? kSightOff : style == kSightOff ? kSightCustom : style + 1;
    if (sightStyleKept(n) != n) ++n;   // skip the removed styles (5, 8)
    if (n != kSightCustom) { style = n; return; }
    if (!sets.empty()) { style = kSightCustom; custom = sets[0].name; return; }
    style = 0;
}

std::string sightLabel(int style, const std::string& custom)
{
    return style == kSightCustom ? "Custom: " + custom : std::string(sightStyleName(style));
}

} // namespace hotd::game
