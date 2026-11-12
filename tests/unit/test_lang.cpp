// lang: the shipped language files (data\lang): well formed, the {placeholders} of each key kept, every character
// drawable with the language's fonts (or the PC fonts' Latin-1 when it names none), the lookup itself (tr / fmt).
// Suite "lang" holds the rules whose break is a bug; suite "langdone" (CTest label "translation", not in CI) lists
// the keys of en.txt a language has not translated yet and the stale ones (a translators' to-do, not a fault).
#include "check.h"

#include "game/lang.h"
#include "render/ttf.h"
#include "util/utf8.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <regex>
#include <set>

namespace fs = std::filesystem;
using namespace hotd;

namespace {

struct LangFile {
    std::string code;
    std::vector<std::pair<std::string, std::string>> lines;   // key, value (in file order)
    std::map<std::string, std::string> map;
    std::vector<std::string> malformed;   // lines that are neither a comment nor "key = value"
};

std::string langDir() { return (fs::path(test::sourceDir()) / "data" / "lang").string(); }

LangFile readLang(const fs::path& p)
{
    LangFile f;
    f.code = p.stem().string();
    std::ifstream in(p, std::ios::binary);
    std::string l;
    bool first = true;
    while (std::getline(in, l)) {
        if (first && l.size() >= 3 && uint8_t(l[0]) == 0xEF && uint8_t(l[1]) == 0xBB && uint8_t(l[2]) == 0xBF) l = l.substr(3);
        first = false;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (l.empty() || l[0] == '#') continue;
        const size_t eq = l.find(" = ");
        if (eq == std::string::npos) {
            // "lang.font = " (an empty value) ends with " =": allowed
            if (l.size() >= 2 && l.compare(l.size() - 2, 2, " =") == 0) {
                f.lines.emplace_back(l.substr(0, l.size() - 2), "");
                f.map[l.substr(0, l.size() - 2)] = "";
                continue;
            }
            f.malformed.push_back(l);
            continue;
        }
        f.lines.emplace_back(l.substr(0, eq), l.substr(eq + 3));
        f.map[l.substr(0, eq)] = l.substr(eq + 3);
    }
    return f;
}

std::vector<LangFile> allLangs()
{
    std::vector<LangFile> v;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(langDir(), ec))
        if (e.path().extension() == ".txt" && e.path().stem() != "en") v.push_back(readLang(e.path()));
    std::sort(v.begin(), v.end(), [](const LangFile& a, const LangFile& b) { return a.code < b.code; });
    return v;
}

std::multiset<std::string> placeholders(const std::string& s)
{
    std::multiset<std::string> out;
    static const std::regex re("\\{[a-z0-9_]+\\}");
    for (auto it = std::sregex_iterator(s.begin(), s.end(), re); it != std::sregex_iterator(); ++it) out.insert(it->str());
    return out;
}

// printf-style pieces (%d, %s, %.1f ...) a value must keep as its key has them
std::multiset<std::string> printfPieces(const std::string& s)
{
    std::multiset<std::string> out;
    static const std::regex re("%[-+ 0#]*[0-9]*(\\.[0-9]+)?[dsfuxXc%]");
    for (auto it = std::sregex_iterator(s.begin(), s.end(), re); it != std::sregex_iterator(); ++it) out.insert(it->str());
    return out;
}

bool special(const std::string& k) { return k.rfind("lang.", 0) == 0 || k.rfind("sub.", 0) == 0; }

} // namespace

TEST(lang, files_well_formed)
{
    const LangFile en = readLang(fs::path(langDir()) / "en.txt");
    CHECK(en.lines.size() > 1000);
    CHECK(en.malformed.empty());
    const auto langs = allLangs();
    CHECK(langs.size() >= 15);
    for (const LangFile* f : [&] { std::vector<const LangFile*> v{&en}; for (auto& l : langs) v.push_back(&l); return v; }()) {
        for (const std::string& m : f->malformed) CHECK_MSG(false, f->code + ": not \"key = text\": " + m.substr(0, 80));
        std::set<std::string> seen;
        for (const auto& [k, v] : f->lines) {
            CHECK_MSG(seen.insert(k).second, f->code + ": key twice: " + k.substr(0, 80));
            CHECK_MSG(util::utf8Valid(k) && util::utf8Valid(v), f->code + ": not UTF-8: " + k.substr(0, 60));
            CHECK_MSG(v.find('\t') == std::string::npos, f->code + ": a tab in " + k.substr(0, 60));
        }
        CHECK_MSG(f->map.count("lang.name") && !f->map.at("lang.name").empty(), f->code + ": no lang.name");
    }
    // en.txt: each key is its own text (it is the English the code holds)
    for (const auto& [k, v] : en.lines)
        if (!special(k)) CHECK_MSG(k == v, "en.txt: " + k.substr(0, 60) + " = " + v.substr(0, 60));
}

TEST(lang, placeholders_kept)
{
    const LangFile en = readLang(fs::path(langDir()) / "en.txt");
    int compared = 0;
    for (const LangFile& f : allLangs())
        for (const auto& [k, v] : f.lines) {
            if (special(k) || !en.map.count(k) || v.empty()) continue;
            ++compared;
            CHECK_MSG(placeholders(k) == placeholders(v), f.code + ": {placeholders} differ: " + k.substr(0, 70) + " => " + v.substr(0, 70));
            CHECK_MSG(printfPieces(k) == printfPieces(v), f.code + ": % pieces differ: " + k.substr(0, 70));
            // a \n in the key: the translation breaks its lines too (the box is laid out for them)
            const bool kn = k.find("\\n") != std::string::npos, vn = v.find("\\n") != std::string::npos;
            CHECK_MSG(!kn || vn, f.code + ": the \\n line break lost: " + k.substr(0, 70));
        }
    CHECK(compared > 15 * 1000);
}

TEST(lang, glyphs_in_fonts)
{
    // what the game draws a language's text with (game/lang.cpp loadFonts): its lang.font list, then every font in
    // data/fonts for the characters those lack; a language without lang.font: the PC fonts (Latin-1), and a text with
    // a character past U+00FF in data/fonts' fonts (fontFor). Every character must be in one of them
    const fs::path fonts = fs::path(test::sourceDir()) / "data" / "fonts";
    std::map<std::string, std::unique_ptr<render::TtfFont>> cache;
    auto fontOf = [&](const std::string& name) -> const render::TtfFont* {
        auto& f = cache[name];
        if (!f) {
            f = std::make_unique<render::TtfFont>();
            std::string err;
            if (!f->load((fonts / name).string(), err)) CHECK_MSG(false, "font " + name + ": " + err);
        }
        return f->loaded() ? f.get() : nullptr;
    };
    std::vector<const render::TtfFont*> all;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(fonts, ec)) {
        const std::string x = e.path().extension().string();
        if (x == ".ttf" || x == ".otf")
            if (const render::TtfFont* t = fontOf(e.path().filename().string())) all.push_back(t);
    }
    CHECK(all.size() >= 6);
    for (const LangFile& f : allLangs()) {
        bool named = false;
        if (auto it = f.map.find("lang.font"); it != f.map.end() && !it->second.empty()) {
            std::string names = it->second;
            for (size_t a = 0; a <= names.size();) {
                size_t b = names.find(',', a);
                if (b == std::string::npos) b = names.size();
                std::string n = names.substr(a, b - a);
                n.erase(0, n.find_first_not_of(' '));
                n.erase(n.find_last_not_of(' ') + 1);
                if (!n.empty()) {
                    CHECK_MSG(fs::exists(fonts / n), f.code + ": lang.font " + n + " not in data/fonts");
                    named = true;
                }
                a = b + 1;
            }
        }
        std::set<uint32_t> missing;
        std::string example;
        for (const auto& [k, v] : f.lines) {
            if (k.rfind("lang.", 0) == 0) continue;
            for (size_t i = 0; i < v.size();) {
                const uint32_t cp = render::utf8Next(v, i);
                if (cp <= 0x20 || cp == 0xA0 || cp == 0x200F || cp == 0x200E || cp == 0x200B) continue;
                if (!named && cp <= 0xFF) continue;   // (the PC fonts' Latin-1)
                bool have = false;
                for (const render::TtfFont* t : all) have = have || t->glyphIndex(cp) != 0;
                if (!have && missing.insert(cp).second && example.empty()) example = v;
            }
        }
        std::string cps;
        for (uint32_t c : missing) {
            char b[16];
            std::snprintf(b, sizeof b, " U+%04X", c);
            cps += b;
        }
        CHECK_MSG(missing.empty(), f.code + ": characters no font draws:" + cps + " (e.g. \"" + example.substr(0, 60) + "\")");
    }
}

TEST(lang, lookup)
{
    lang::setDataDirs({test::sourceDir()});
    const auto avail = lang::available();
    CHECK(avail.size() >= 16);
    CHECK(!avail.empty() && avail[0].code == "en");
    // English: the text as given
    CHECK(lang::set("en"));
    CHECK(!lang::active());
    CHECK_EQ(lang::tr("Options"), std::string("Options"));
    CHECK_EQ(lang::fmt("Chapter {n}", {{"n", "3"}}), std::string("Chapter 3"));
    // every shipped language loads, translates a key the file has, and keeps the pieces of a {placeholder} key
    for (const LangFile& f : allLangs()) {
        std::string err;
        CHECK_MSG(lang::set(f.code, &err), f.code + ": " + err);
        CHECK(lang::active());
        CHECK_EQ(lang::code(), f.code);
        if (auto it = f.map.find("Options"); it != f.map.end()) CHECK_EQ(lang::tr("Options"), it->second);
        for (const auto& [k, v] : f.lines) {
            const auto ph = placeholders(k);
            if (ph.size() != 1 || v.empty() || *ph.begin() != "{n}") continue;
            const std::string out = lang::fmt(k, {{"n", "7319"}});
            CHECK_MSG(out.find("7319") != std::string::npos && out.find("{n}") == std::string::npos, f.code + ": " + k);
            break;
        }
        // an unknown text falls back to itself
        CHECK_EQ(lang::tr("zz unknown text 123"), std::string("zz unknown text 123"));
    }
    CHECK(!lang::set("xx-none"));
    CHECK(lang::set("en"));
    // columns: CJK two each, Latin one
    CHECK_EQ(lang::columns("abc"), 3);
    CHECK_EQ(lang::columns("\xE6\x97\xA5\xE6\x9C\xAC"), 4);
    const auto wrapped = lang::wrapColumns("one two three four five", 9);
    CHECK(wrapped.size() >= 3);
    for (const std::string& w : wrapped) CHECK_MSG(lang::columns(w) <= 9, w);
    std::string latin;
    CHECK(lang::toLatin1("Espa\xC3\xB1ol", latin) && latin == "Espa\xF1ol");
    CHECK(!lang::toLatin1("\xE6\x97\xA5", latin));
}

TEST(langdone, every_key_translated)
{
    const LangFile en = readLang(fs::path(langDir()) / "en.txt");
    for (const LangFile& f : allLangs()) {
        std::vector<std::string> miss, stale;
        for (const auto& [k, v] : en.lines)
            if (!f.map.count(k) && k != "lang.font" && k != "lang.font_scale") miss.push_back(k);
        for (const auto& [k, v] : f.lines)
            if (!en.map.count(k) && !special(k)) stale.push_back(k);
        CHECK_MSG(miss.empty(), f.code + ": " + std::to_string(miss.size()) + " keys of en.txt not translated (first: " +
                                    (miss.empty() ? std::string() : miss[0].substr(0, 60)) + ")");
        CHECK_MSG(stale.empty(), f.code + ": " + std::to_string(stale.size()) + " keys no longer in en.txt (first: " +
                                     (stale.empty() ? std::string() : stale[0].substr(0, 60)) + ")");
    }
}
