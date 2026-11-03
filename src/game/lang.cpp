// lang: the language files and the text lookup (lang.h)
#include "game/lang.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hotd::lang {

namespace {

namespace fs = std::filesystem;

struct Pattern {
    std::vector<std::string> parts;   // literal, name, literal, name, ... (even: literal; odd: a placeholder's name)
    std::string key, value;
};

struct State {
    std::vector<std::string> dirs;
    std::string code = "en", file;
    bool active = false;
    uint64_t gen = 1;
    std::unordered_map<std::string, std::string> exact;
    std::vector<Pattern> pats;
    std::unordered_map<std::string, std::string> memo;
    std::map<std::string, std::string> meta;   // lang.*
    std::map<std::pair<int, int>, std::string> subs;
    // the fonts (kept alive after a reload: a menu worker may still hold one)
    std::vector<std::unique_ptr<render::TtfFont>> fonts;
    std::unique_ptr<render::TtfText> text;
    std::vector<std::unique_ptr<render::TtfFont>> oldFonts;
    std::vector<std::unique_ptr<render::TtfText>> oldTexts;
    float fontScale = 1.0f;
    bool upper = true;
    bool rtl = false;   // (rtl) the language reads right to left (lang.rtl, else by its letters)
    // every font in data/fonts (loaded once): the fallbacks after a language's own, and English's for a text with
    // characters the PC fonts lack
    std::vector<std::unique_ptr<render::TtfFont>> allFonts;
    std::unique_ptr<render::TtfText> allText;
    bool allTried = false;
    // recording
    bool recording = false;
    std::set<std::string> recordedSet, lineSet;
    std::vector<std::string> recorded;
    std::vector<std::string> codePatterns;
    std::vector<Pattern> codePats;
};

State& st()
{
    static State s;
    return s;
}
std::recursive_mutex& mu()
{
    static std::recursive_mutex m;
    return m;
}

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

std::string unescape(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char c = s[++i];
            o += c == 'n' ? '\n' : c == 't' ? '\t' : c;
        } else
            o += s[i];
    }
    return o;
}

std::string escape(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        // misc17: " = " inside a text ("Enter = START, Esc = QUIT") as " \= " (the reader splits a line at its first
        // " = "; unescape gives the '=' back)
        if (c == '=' && i > 0 && s[i - 1] == ' ' && i + 1 < s.size() && s[i + 1] == ' ') o += "\\=";
        else if (c == '\n') o += "\\n";
        else if (c == '\\') o += "\\\\";
        else if (c == '\t') o += "\\t";
        else o += c;
    }
    // (a value starting with '#' or space would read as a comment / be trimmed)
    if (!o.empty() && (o[0] == '#' || o[0] == ' ')) o = "\\" + o;
    return o;
}

bool parsePattern(const std::string& key, Pattern& p)
{
    p.parts.clear();
    std::string lit;
    for (size_t i = 0; i < key.size(); ++i) {
        if (key[i] == '{') {
            const size_t e = key.find('}', i);
            if (e == std::string::npos || e == i + 1) return false;
            const std::string name = key.substr(i + 1, e - i - 1);
            if (name.find_first_of(" {") != std::string::npos) return false;
            p.parts.push_back(lit);
            p.parts.push_back(name);
            lit.clear();
            i = e;
        } else
            lit += key[i];
    }
    if (p.parts.empty()) return false;
    p.parts.push_back(lit);
    p.key = key;
    return true;
}

// the pieces of text for the pattern's placeholders (each one at least one character); false: no match
bool matchFrom(const Pattern& p, size_t part, const std::string& s, size_t at, std::vector<std::string>& vals)
{
    const std::string& lit = p.parts[part];
    if (s.compare(at, lit.size(), lit) != 0) return false;
    at += lit.size();
    if (part + 1 >= p.parts.size()) return at == s.size();
    const std::string& next = p.parts[part + 2];
    const std::string& name = p.parts[part + 1];
    // {n}, {n1} ...: a number only (digits and . , : / -); other names: any text
    const bool number = !name.empty() && name[0] == 'n' && name.find_first_not_of("0123456789", 1) == std::string::npos;
    for (size_t end = at + 1; end <= s.size(); ++end) {
        if (number && std::string("0123456789.,:/-").find(s[end - 1]) == std::string::npos) return false;
        if (!next.empty() && s.compare(end, next.size(), next) != 0) continue;
        vals.push_back(s.substr(at, end - at));
        if (matchFrom(p, part + 2, s, end, vals)) return true;
        vals.pop_back();
    }
    return false;
}

std::string exactOr(const std::string& k)
{
    auto it = st().exact.find(k);
    if (it != st().exact.end()) return it->second;
    // (a piece the code wrote with a small first letter, "Experimental: four players ...": its capitalised key)
    if (!k.empty() && k[0] >= 'a' && k[0] <= 'z') {
        std::string c = k;
        c[0] = char(c[0] - 'a' + 'A');
        if ((it = st().exact.find(c)) != st().exact.end()) return it->second;
    }
    return k;
}

std::string fill(const std::string& value, const Pattern& p, const std::vector<std::string>& vals, bool translate)
{
    std::string o = value;
    for (size_t v = 0; v < vals.size(); ++v) {
        const std::string tag = "{" + p.parts[v * 2 + 1] + "}";
        const std::string piece = translate ? exactOr(trim(vals[v])) : vals[v];
        for (size_t at = 0; (at = o.find(tag, at)) != std::string::npos; at += piece.size())
            o.replace(at, tag.size(), piece);
    }
    return o;
}

std::string langFile(const std::string& code)
{
    for (const std::string& d : st().dirs) {
        std::error_code ec;
        const fs::path p = fs::path(d) / "data" / "lang" / (code + ".txt");
        if (fs::exists(p, ec)) return p.string();
    }
    return {};
}

bool parseFile(const std::string& path, State& s, std::string& err)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "cannot open " + path;
        return false;
    }
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        ++n;
        if (n == 1 && line.size() >= 3 && uint8_t(line[0]) == 0xEF && uint8_t(line[1]) == 0xBB && uint8_t(line[2]) == 0xBF)
            line = line.substr(3);
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        size_t eq = t.find(" = ");
        size_t vlen = 3;
        if (eq == std::string::npos) {
            eq = t.find('=');
            vlen = 1;
        }
        if (eq == std::string::npos || eq == 0) continue;
        const std::string key = unescape(trim(t.substr(0, eq)));
        const std::string val = unescape(trim(t.substr(eq + vlen)));
        if (key.rfind("lang.", 0) == 0) {
            s.meta[key] = val;
            continue;
        }
        if (val.empty()) continue;   // (an empty translation: English)
        if (key.rfind("sub.", 0) == 0) {
            int e = -1, l = -1;
            if (std::sscanf(key.c_str() + 4, "%d.%d", &e, &l) == 2) s.subs[{e, l}] = val;
            continue;
        }
        Pattern p;
        if (key.find('{') != std::string::npos && parsePattern(key, p)) {
            p.value = val;
            s.pats.push_back(std::move(p));
        } else
            s.exact[key] = val;
    }
    return true;
}

void loadAllFonts(State& s)
{
    if (s.allTried) return;
    s.allTried = true;
    std::vector<fs::path> files;
    for (const std::string& d : s.dirs) {
        std::error_code ec;
        const fs::path dir = fs::path(d) / "data" / "fonts";
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            std::string x = e.path().extension().string();
            std::transform(x.begin(), x.end(), x.begin(), [](char c) { return char(std::tolower(uint8_t(c))); });
            if (x == ".otf" || x == ".ttf" || x == ".ttc") files.push_back(e.path());
        }
        if (!files.empty()) break;   // (the first folder with fonts: the package's, else the checkout's)
    }
    std::sort(files.begin(), files.end());
    std::vector<const render::TtfFont*> ptrs;
    for (const fs::path& p : files) {
        auto f = std::make_unique<render::TtfFont>();
        std::string e;
        if (f->load(p.string(), e)) {
            ptrs.push_back(f.get());
            s.allFonts.push_back(std::move(f));
        }
    }
    // (rtl: the fonts with Latin letters first: the first one sizes the text, pxForCap; Noto Sans Arabic / Hebrew
    // have no Latin)
    std::stable_partition(ptrs.begin(), ptrs.end(), [](const render::TtfFont* f) { return f->glyphIndex('A') != 0; });
    if (!ptrs.empty()) s.allText = std::make_unique<render::TtfText>(ptrs);
}

bool loadFonts(State& s, const std::string& list, const std::string& langPath, std::string& err)
{
    std::stringstream ss(list);
    std::string name;
    std::vector<std::unique_ptr<render::TtfFont>> fonts;
    while (std::getline(ss, name, ',')) {
        name = trim(name);
        if (name.empty()) continue;
        std::vector<fs::path> tries{fs::path(name)};
        if (!langPath.empty()) tries.push_back(fs::path(langPath).parent_path() / name);
        for (const std::string& d : s.dirs) tries.push_back(fs::path(d) / "data" / "fonts" / name);
        bool ok = false;
        for (const fs::path& p : tries) {
            std::error_code ec;
            if (!fs::is_regular_file(p, ec)) continue;
            auto f = std::make_unique<render::TtfFont>();
            std::string e;
            if (f->load(p.string(), e)) {
                fonts.push_back(std::move(f));
                ok = true;
                break;
            }
            err = name + ": " + e;
        }
        if (!ok && err.empty()) err = "font " + name + " not found (data/fonts)";
    }
    if (fonts.empty()) return false;
    std::vector<const render::TtfFont*> ptrs;
    for (auto& f : fonts) ptrs.push_back(f.get());
    loadAllFonts(s);   // (then the other fonts there: characters the language's font lacks)
    for (auto& f : s.allFonts) {
        bool same = false;
        for (auto& g : fonts) same = same || fs::path(g->file()).filename() == fs::path(f->file()).filename();
        if (!same) ptrs.push_back(f.get());
    }
    for (auto& f : s.fonts) s.oldFonts.push_back(std::move(f));
    if (s.text) s.oldTexts.push_back(std::move(s.text));
    s.fonts = std::move(fonts);
    s.text = std::make_unique<render::TtfText>(ptrs);
    return true;
}

} // namespace

void setDataDirs(const std::vector<std::string>& dirs)
{
    std::lock_guard<std::recursive_mutex> lk(mu());
    st().dirs = dirs;
}

const std::vector<std::string>& dataDirs() { return st().dirs; }

// menufb: the line each language shows as its preview on the Language page
static const char* const kPreviewKey = "Difficulty, lives, volumes and all settings";

std::vector<Info> available()
{
    std::lock_guard<std::recursive_mutex> lk(mu());
    std::vector<Info> out{{"en", "English", "", false, kPreviewKey}};
    std::set<std::string> seen{"en"};
    for (const std::string& d : st().dirs) {
        std::error_code ec;
        const fs::path dir = fs::path(d) / "data" / "lang";
        if (!fs::is_directory(dir, ec)) continue;
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".txt") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const fs::path& p : files) {
            const std::string code = p.stem().string();
            if (code.find('.') != std::string::npos) continue;   // (ko.all.txt: --export-lang's full list, not a language)
            if (!seen.insert(code).second) continue;
            State tmp;
            std::string err;
            parseFile(p.string(), tmp, err);
            Info i;
            i.code = code;
            i.file = p.string();
            i.name = tmp.meta.count("lang.name") ? tmp.meta["lang.name"] : code;
            i.sample = tmp.meta.count("lang.sample") && tmp.meta["lang.sample"] == "1";
            if (const auto pv = tmp.exact.find(kPreviewKey); pv != tmp.exact.end()) i.preview = pv->second;
            // relmenu (user 2026-10-30: "players can't tell what each language is"): its English name too
            static const char* const kEnglish[][2] = {
                {"ar", "Arabic"}, {"he", "Hebrew"}, {"ja", "Japanese"}, {"ko", "Korean"}, {"zh", "Chinese"}, {"fr", "French"},
                {"de", "German"}, {"es", "Spanish"}, {"it", "Italian"}, {"pt", "Portuguese"}, {"ru", "Russian"}, {"pl", "Polish"},
                {"nl", "Dutch"}, {"tr", "Turkish"}, {"fa", "Persian"}, {"ur", "Urdu"}, {"hi", "Hindi"}, {"bn", "Bengali"},
                {"th", "Thai"}, {"vi", "Vietnamese"}, {"id", "Indonesian"}, {"uk", "Ukrainian"}, {"sv", "Swedish"}, {"el", "Greek"}};
            if (tmp.meta.count("lang.english"))
                i.english = tmp.meta["lang.english"];
            else
                for (const auto& e : kEnglish)
                    if (code == e[0]) i.english = e[1];
            out.push_back(i);
        }
    }
    return out;
}

std::string displayName(const std::string& code)
{
    for (const Info& i : available())
        if (i.code == code) return i.name;
    return code;
}

bool set(const std::string& codeIn, std::string* err)
{
    std::lock_guard<std::recursive_mutex> lk(mu());
    State& s = st();
    const std::string code = codeIn.empty() ? "en" : codeIn;
    s.exact.clear();
    s.pats.clear();
    s.memo.clear();
    s.meta.clear();
    s.subs.clear();
    for (auto& f : s.fonts) s.oldFonts.push_back(std::move(f));
    s.fonts.clear();
    if (s.text) s.oldTexts.push_back(std::move(s.text));
    s.fontScale = 1.0f;
    s.upper = true;
    s.rtl = false;
    render::setBaseRtl(false);
    s.code = "en";
    s.file.clear();
    s.active = false;
    ++s.gen;
    if (code == "en") return true;
    const std::string path = langFile(code);
    std::string e;
    if (path.empty()) {
        if (err) *err = "no data/lang/" + code + ".txt";
        return false;
    }
    if (!parseFile(path, s, e)) {
        if (err) *err = e;
        return false;
    }
    s.code = code;
    s.file = path;
    s.active = true;
    if (s.meta.count("lang.font_scale")) s.fontScale = std::clamp(float(std::atof(s.meta["lang.font_scale"].c_str())), 0.3f, 3.0f);
    if (s.meta.count("lang.upper")) s.upper = s.meta["lang.upper"] != "0";
    // rtl: lang.rtl = 1 / 0; none: right to left when the language's name, else most of its texts, are in a right-to-left
    // script (Arabic, Hebrew ...)
    if (s.meta.count("lang.rtl")) s.rtl = s.meta["lang.rtl"] == "1";
    else if (s.meta.count("lang.name") && render::hasRtl(s.meta["lang.name"])) s.rtl = true;
    else {
        size_t r = 0;
        for (const auto& kv : s.exact) r += render::hasRtl(kv.second);
        s.rtl = !s.exact.empty() && r * 2 > s.exact.size();
    }
    render::setBaseRtl(s.rtl);
    if (s.meta.count("lang.font") && !s.meta["lang.font"].empty()) {
        std::string fe;
        if (!loadFonts(s, s.meta["lang.font"], path, fe) && err) *err = fe;
    }
    return true;
}

const std::string& code() { return st().code; }
bool active() { return st().active; }
bool on() { return st().active || st().recording; }
uint64_t gen() { return st().gen; }

bool reload(std::string* err)
{
    const std::string c = st().code;
    return set(c, err);
}

std::string tr(const std::string& english)
{
    State& s = st();
    if (!s.active && !s.recording)
        return english;
    std::lock_guard<std::recursive_mutex> lk(mu());
    if (s.recording) {
        const std::string k = trim(english);
        if (!k.empty() && s.recordedSet.insert(k).second) s.recorded.push_back(k);
    }
    if (!s.active)
        return english;
    if (auto it = s.memo.find(english); it != s.memo.end())
        return it->second;
    size_t a = 0, b = english.size();
    while (a < b && english[a] == ' ') ++a;
    while (b > a && english[b - 1] == ' ') --b;
    const std::string key = english.substr(a, b - a);
    std::string out = english;
    if (!key.empty()) {
        auto it = s.exact.find(key);
        if (it != s.exact.end())
            out = english.substr(0, a) + it->second + english.substr(b);
        else
            for (const Pattern& p : s.pats) {
                std::vector<std::string> vals;
                if (matchFrom(p, 0, key, 0, vals)) {
                    out = english.substr(0, a) + fill(p.value, p, vals, true) + english.substr(b);
                    break;
                }
            }
    }
    if (s.memo.size() > 50000) s.memo.clear();
    s.memo.emplace(english, out);
    return out;
}

std::string fmt(const std::string& pattern, const std::vector<std::pair<std::string, std::string>>& args)
{
    std::string o;
    {
        State& s = st();
        std::lock_guard<std::recursive_mutex> lk(mu());
        if (s.recording && s.recordedSet.insert(pattern).second) s.recorded.push_back(pattern);
        o = pattern;
        if (s.active)
            for (const Pattern& p : s.pats)
                if (p.key == pattern) {
                    o = p.value;
                    break;
                }
    }
    for (const auto& [k, v] : args) {
        const std::string tag = "{" + k + "}";
        const std::string piece = tr(v);
        for (size_t at = 0; (at = o.find(tag, at)) != std::string::npos; at += piece.size())
            o.replace(at, tag.size(), piece);
    }
    return o;
}

const std::string* subtitle(int event, int line)
{
    State& s = st();
    if (!s.active) return nullptr;
    auto it = s.subs.find({event, line});
    return it != s.subs.end() ? &it->second : nullptr;
}

const render::TtfText* font()
{
    State& s = st();
    return s.active && s.text && !s.text->empty() ? s.text.get() : nullptr;
}
int columns(const std::string& s)
{
    int n = 0;
    for (size_t i = 0; i < s.size();) n += render::isCjk(render::utf8Next(s, i)) ? 2 : 1;
    return n;
}

std::vector<std::string> wrapColumns(const std::string& s, int width)
{
    // units: a word or one CJK character, each with whether a space came before it; packed greedily by columns (a
    // space is kept between two units only where the text had one)
    struct Unit { std::string text; bool space; };
    std::vector<Unit> units;
    std::string word;
    bool space = false;
    auto flush = [&]() {
        if (!word.empty()) units.push_back({word, space}), space = false;
        word.clear();
    };
    for (size_t i = 0; i < s.size();) {
        const size_t at = i;
        const uint32_t cp = render::utf8Next(s, i);
        if (cp == ' ' || cp == '\n' || cp == '\t') {
            flush();
            space = true;
            continue;
        }
        if (render::isCjk(cp)) {
            flush();
            units.push_back({s.substr(at, i - at), space});
            space = false;
            continue;
        }
        word += s.substr(at, i - at);
    }
    flush();
    std::vector<std::string> out;
    std::string line;
    int cols = 0;
    for (const Unit& u : units) {
        const int w = columns(u.text), sep = !line.empty() && u.space ? 1 : 0;
        if (!line.empty() && cols + sep + w > width) {
            out.push_back(line);
            line.clear();
            cols = 0;
        }
        if (!line.empty() && u.space) line += ' ', ++cols;
        line += u.text;
        cols += w;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

std::vector<std::string> wrapBy(const std::string& s, float maxW, const std::function<float(const std::string&)>& width)
{
    // (TtfText::wrap's units: a word with its spaces, or one CJK character; a line of the words that fit)
    std::vector<std::string> units;
    std::string cur;
    bool curCjk = false;
    auto flush = [&]() {
        if (!cur.empty()) units.push_back(cur);
        cur.clear();
        curCjk = false;
    };
    for (size_t i = 0; i < s.size();) {
        const size_t at = i;
        const uint32_t cp = render::utf8Next(s, i);
        const std::string ch = s.substr(at, i - at);
        if (cp == '\n') { flush(); units.push_back("\n"); continue; }
        if (render::isCjk(cp)) { flush(); cur = ch; curCjk = true; continue; }
        if (curCjk && cp != ' ' && cp != '.' && cp != ',' && cp != '!' && cp != '?' && cp != ')') flush();
        cur += ch;
        if (cp == ' ') flush();
    }
    flush();
    auto trimEnd = [](std::string t) { while (!t.empty() && t.back() == ' ') t.pop_back(); return t; };
    std::vector<std::string> lines;
    std::string line;
    for (const std::string& u : units) {
        if (u == "\n") { lines.push_back(trimEnd(line)); line.clear(); continue; }
        if (!line.empty() && width(trimEnd(line + u)) > maxW) {
            lines.push_back(trimEnd(line));
            line.clear();
        }
        // a word wider than the whole line (a long file path, user 2026-11-03): broken where it fills the line
        if (line.empty() && width(trimEnd(u)) > maxW) {
            for (size_t i = 0; i < u.size();) {
                const size_t at = i;
                render::utf8Next(u, i);
                const std::string ch = u.substr(at, i - at);
                if (!line.empty() && width(line + ch) > maxW) {
                    lines.push_back(line);
                    line.clear();
                }
                line += ch;
            }
            continue;
        }
        line += u;
    }
    if (!line.empty() || lines.empty()) lines.push_back(trimEnd(line));
    return lines;
}

bool plain(const std::string& s)
{
    for (const char c : s)
        if (uint8_t(c) >= 0x80) return false;
    return true;
}

const render::TtfText* fontFor(const std::string& s)
{
    // (an ASCII text, an English one left untranslated or a number: the PC fonts as in English, the same size)
    if (plain(s)) return nullptr;
    if (const render::TtfText* t = font()) return t;
    bool wide = false;
    for (size_t i = 0; i < s.size() && !wide;) wide = render::utf8Next(s, i) > 0xFF;
    if (!wide) return nullptr;   // (Latin-1: the PC fonts have those letters)
    std::lock_guard<std::recursive_mutex> lk(mu());
    loadAllFonts(st());
    return st().allText.get();
}

const render::TtfText* anyFont()
{
    if (const render::TtfText* t = font()) return t;
    std::lock_guard<std::recursive_mutex> lk(mu());
    loadAllFonts(st());
    return st().allText.get();
}

float fontScale() { return st().fontScale; }
bool upperCase() { return !st().active || st().upper; }
bool rtl() { return st().active && st().rtl; }

float pxForCap(float capPx, const render::TtfText* t)
{
    if (!t || t->empty()) return capPx;
    const render::TtfFont& f = t->primary();
    const float k = t == font() ? st().fontScale : 0.9f;
    return capPx * float(f.unitsPerEm()) / float(std::max(1, f.capHeight())) * k;
}

bool toLatin1(const std::string& s, std::string& out)
{
    out.clear();
    for (size_t i = 0; i < s.size();) {
        const uint32_t cp = render::utf8Next(s, i);
        if (cp > 0xFF) return false;
        out += char(uint8_t(cp));
    }
    return true;
}

void record(bool on)
{
    std::lock_guard<std::recursive_mutex> lk(mu());
    st().recording = on;
    if (on) st().recorded.clear(), st().recordedSet.clear(), st().lineSet.clear();
}
const std::vector<std::string>& recorded()
{
    State& s = st();
    std::lock_guard<std::recursive_mutex> lk(mu());
    if (!s.lineSet.empty()) {   // (a line noted after it was recorded)
        std::vector<std::string> keep;
        for (const std::string& k : s.recorded)
            if (!s.lineSet.count(k)) keep.push_back(k);
        s.recorded.swap(keep);
    }
    return s.recorded;
}

const std::vector<std::string>& noteLines(const std::vector<std::string>& lines)
{
    State& s = st();
    if (s.recording && lines.size() > 1) {
        std::lock_guard<std::recursive_mutex> lk(mu());
        for (const std::string& l : lines) s.lineSet.insert(trim(l));
    }
    return lines;
}

void addPattern(const std::string& pattern)
{
    std::lock_guard<std::recursive_mutex> lk(mu());
    State& s = st();
    if (std::find(s.codePatterns.begin(), s.codePatterns.end(), pattern) != s.codePatterns.end()) return;
    Pattern p;
    if (!parsePattern(pattern, p)) return;
    s.codePatterns.push_back(pattern);
    s.codePats.push_back(p);
}
const std::vector<std::string>& patterns() { return st().codePatterns; }

std::string keyOf(const std::string& text)
{
    std::lock_guard<std::recursive_mutex> lk(mu());
    for (const Pattern& p : st().codePats) {
        std::vector<std::string> vals;
        if (matchFrom(p, 0, text, 0, vals)) return p.key;
    }
    return text;
}

bool exportFile(const std::string& path, const std::vector<std::string>& keys,
                const std::vector<std::pair<std::string, std::string>>& extra, std::string& err)
{
    std::ofstream o(path, std::ios::binary);
    if (!o) {
        err = "cannot write " + path;
        return false;
    }
    std::lock_guard<std::recursive_mutex> lk(mu());
    State& s = st();
    const bool en = !s.active;
    o << "# The House of the Dead: ReWrithe - language file (UTF-8). How to translate: docs/translating.md\n"
         "# Each line: English text = your text. Keep {placeholders} as they are (they may move). \\n = a new line.\n"
         "# A line left out (or commented with #) shows in English. F5 in the menu reloads this file.\n"
         "# Written by: hotd --export-lang " << (en ? std::string("en") : s.code) << "\n\n";
    auto metaOr = [&](const char* k, const char* d) { return s.meta.count(k) ? s.meta[k] : std::string(d); };
    o << "lang.name = " << (en ? "English" : metaOr("lang.name", s.code.c_str())) << "\n";
    if (!en && s.meta.count("lang.english")) o << "lang.english = " << s.meta["lang.english"] << "\n";   // (relmenu)
    o << "# a TTF / OTF in data/fonts (or next to this file) for characters the game's own fonts lack (Korean,\n"
         "# Japanese, Chinese, Cyrillic ...); a comma list: fallbacks. Empty: the game's fonts (Latin-1 letters only)\n";
    o << "lang.font = " << (en ? "" : metaOr("lang.font", "")) << "\n";
    o << "lang.font_scale = " << (en ? "1" : metaOr("lang.font_scale", "1")) << "\n";
    o << "lang.upper = " << (en ? "1" : metaOr("lang.upper", "1")) << "\n";
    if (!en && s.meta.count("lang.rtl")) o << "lang.rtl = " << s.meta["lang.rtl"] << "\n";   // (rtl)
    o << "lang.sample = " << (en ? "0" : metaOr("lang.sample", "0")) << "\n\n";
    for (const std::string& k : keys) {
        if (k.rfind("lang.", 0) == 0) continue;
        std::string v;
        bool have = en;
        if (!en) {
            if (auto it = s.exact.find(k); it != s.exact.end()) v = it->second, have = true;
            else
                for (const Pattern& p : s.pats)
                    if (p.key == k) { v = p.value; have = true; break; }
        }
        if (en) v = k;
        if (have) o << escape(k) << " = " << escape(v) << "\n";
        else o << "# " << escape(k) << " = " << escape(k) << "\n";
    }
    if (!extra.empty()) {
        o << "\n# ---- cut-scene subtitles: sub.<event>.<line> (the game's own line in the comment above each) ----\n";
        for (const auto& [k, english] : extra) {
            int e = -1, l = -1;
            std::sscanf(k.c_str() + 4, "%d.%d", &e, &l);
            const std::string* t = en ? nullptr : subtitle(e, l);
            o << "# " << escape(english) << "\n";
            if (t) o << k << " = " << escape(*t) << "\n";
            else o << (en ? "" : "# ") << k << " = " << (en ? escape(english) : escape(english)) << "\n";
        }
    }
    return bool(o);
}

std::string autoPick()
{
    std::vector<std::string> cands;
#ifdef _WIN32
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0) > 0) {
        std::string n;
        for (const wchar_t* c = name; *c; ++c) n += char(*c < 128 ? *c : '?');
        cands.push_back(n);
    }
#endif
    if (const char* l = std::getenv("LANG")) {
        std::string n = l;
        n = n.substr(0, n.find('.'));
        std::replace(n.begin(), n.end(), '_', '-');
        cands.push_back(n);
    }
    for (std::string c : cands) {
        if (c.empty()) continue;
        std::transform(c.begin(), c.end(), c.begin(), [](char ch) { return char(std::tolower(uint8_t(ch))); });
        std::vector<std::string> tries{c};
        // Chinese: zh-tw / zh-hk / zh-hant -> zh-tw, else zh-cn; then the language alone
        if (c.rfind("zh", 0) == 0) tries.push_back(c.find("tw") != std::string::npos || c.find("hk") != std::string::npos || c.find("hant") != std::string::npos ? "zh-tw" : "zh-cn");
        tries.push_back(c.substr(0, c.find('-')));
        for (const std::string& t : tries)
            if (t != "en" && !langFile(t).empty()) return t;
        if (c.rfind("en", 0) == 0) return "";
    }
    return "";
}

} // namespace hotd::lang
