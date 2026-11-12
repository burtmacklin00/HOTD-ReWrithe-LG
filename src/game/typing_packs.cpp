// The Typing of the Dead 0's word packs (typing_packs.h).
#include "game/typing_packs.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace hotd::game {

namespace fs = std::filesystem;

namespace {
std::string trimmed(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
std::string lower(std::string s)
{
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// the game's own list names (typing.ini's defaults): a file named so goes to that list whole
const std::set<std::string>& listNames()
{
    static const std::set<std::string> n = {"short", "easy", "words", "long", "phrases", "hard_phrases",
                                            "boss_short", "boss_easy", "boss", "boss_hard"};
    return n;
}
// a line by its letters: a word (no space) short / easy / words / long, a short phrase (up to 17) both phrase lists,
// a longer line the bosses' sentences by length (a pack with none: the built-in ones, Typing::load)
std::vector<std::string> sortInto(const std::string& s)
{
    const size_t n = s.size();
    if (s.find(' ') == std::string::npos)
        return {n <= 3 ? "short" : n <= 5 ? "easy" : n <= 8 ? "words" : "long"};
    if (n <= 17)
        return {"phrases", "hard_phrases"};
    return {n <= 22 ? "boss_easy" : n <= 30 ? "boss" : "boss_hard"};
}
// the text's line cut at spaces into pieces of at most `chunk` letters (a longer word: a piece by itself)
void cut(const std::string& line, size_t chunk, std::vector<std::string>& out)
{
    std::stringstream ss(line);
    std::string piece;
    for (std::string w; ss >> w;) {
        if (!piece.empty() && piece.size() + 1 + w.size() > chunk) {
            out.push_back(piece);
            piece.clear();
        }
        piece += (piece.empty() ? "" : " ") + w;
    }
    if (!piece.empty()) out.push_back(piece);
}
// a file's lines: the BOM and '#' comments off, tabs as spaces, runs of spaces as one; a line with a character the
// prompts' font has no glyph for (anything but printable ASCII) skipped, its characters noted
std::vector<std::string> readLines(const fs::path& p, TypingPack& pk, int minLetters, int maxLetters)
{
    std::vector<std::string> out;
    std::ifstream f(p, std::ios::binary);
    bool first = true;
    for (std::string line; std::getline(f, line);) {
        if (first && line.size() >= 3 && uint8_t(line[0]) == 0xEF && uint8_t(line[1]) == 0xBB && uint8_t(line[2]) == 0xBF)
            line.erase(0, 3);
        first = false;
        for (char& c : line)
            if (c == '\t') c = ' ';
        line = trimmed(line);
        if (line.empty() || line[0] == '#') continue;
        std::string clean;
        bool bad = false;
        for (size_t i = 0; i < line.size(); ++i) {
            const uint8_t c = uint8_t(line[i]);
            if (c >= 32 && c < 127) {
                if (!(c == ' ' && !clean.empty() && clean.back() == ' ')) clean += char(c);
                continue;
            }
            bad = true;
            size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;   // (a UTF-8 character's bytes)
            const std::string ch = line.substr(i, len);
            if (c >= 0x80 && pk.missing.find(ch) == std::string::npos && pk.missing.size() < 64)
                pk.missing += (pk.missing.empty() ? "" : " ") + ch;
            i += len - 1;
        }
        const int n = int(clean.size());
        if (bad || clean.empty() || (minLetters > 0 && n < minLetters) || (maxLetters > 0 && n > maxLetters)) {
            ++pk.skipped;
            continue;
        }
        out.push_back(clean);
    }
    return out;
}
} // namespace

std::string TypingPack::describe() const
{
    std::string s = name + (language.empty() ? "" : " (" + language + ")") + ": " + std::to_string(lines) + " lines";
    if (text) s += ", a text in order";
    if (skipped) s += ", " + std::to_string(skipped) + " skipped";
    if (!missing.empty()) s += " (the font lacks " + missing + ")";
    return s;
}

bool typingPackRead(const std::string& path, TypingPack& pk)
{
    std::error_code ec;
    const fs::path p(path);
    pk.path = path;
    if (pk.id.empty()) pk.id = fs::is_directory(p, ec) ? p.filename().string() : p.stem().string();
    if (pk.name.empty()) pk.name = pk.id;
    std::vector<fs::path> files;
    std::map<std::string, std::string> fileList;   // pack.ini: file -> list
    std::set<std::string> textFiles;
    int minLetters = 0, maxLetters = 0;
    size_t chunk = 18;
    if (fs::is_directory(p, ec)) {
        std::ifstream ini(p / "pack.ini");
        for (std::string line; std::getline(ini, line);) {
            line = trimmed(line.substr(0, line.find('#')));
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = lower(trimmed(line.substr(0, eq))), v = trimmed(line.substr(eq + 1));
            if (k == "name") pk.name = v;
            else if (k == "language") pk.language = v;
            else if (k == "order") pk.text = lower(v) == "text";
            else if (k == "chunk") chunk = size_t(std::clamp(std::atoi(v.c_str()), 4, 60));
            else if (k == "min_letters") minLetters = std::max(0, std::atoi(v.c_str()));
            else if (k == "max_letters") maxLetters = std::max(0, std::atoi(v.c_str()));
            else if (k == "text" || listNames().count(k)) {
                std::stringstream ss(v);
                for (std::string f; std::getline(ss, f, ',');)
                    if (!trimmed(f).empty()) {
                        if (k == "text") textFiles.insert(lower(trimmed(f)));
                        else fileList[lower(trimmed(f))] = k;
                    }
            }
        }
        for (const auto& e : fs::directory_iterator(p, ec))
            if (e.is_regular_file(ec) && lower(e.path().extension().string()) == ".txt")
                files.push_back(e.path());
        std::sort(files.begin(), files.end());
    } else if (fs::is_regular_file(p, ec))
        files.push_back(p);
    for (const fs::path& f : files) {
        const std::string fn = lower(f.filename().string()), stem = lower(f.stem().string());
        std::vector<std::string> lines = readLines(f, pk, minLetters, maxLetters);
        pk.lines += lines.size();
        const auto named = fileList.find(fn);
        const std::string list = named != fileList.end() ? named->second : listNames().count(stem) ? stem : std::string();
        // order = text: the files pack.ini names as text = (none named: every file not given to a list), in order
        if (pk.text && (textFiles.count(fn) || (textFiles.empty() && list.empty())))
            for (const std::string& l : lines) cut(l, chunk, pk.pieces);
        for (const std::string& l : lines) {
            if (!list.empty()) pk.lists[list].push_back(l);
            else
                for (const std::string& to : sortInto(l)) pk.lists[to].push_back(l);
        }
    }
    // (a text's pieces are words for the rest too: a creature whose turn finds the text's next first letter taken)
    for (const std::string& s : pk.pieces)
        if (s.find(' ') != std::string::npos || s.size() > 1)
            for (const std::string& to : sortInto(s))
                if (std::find(pk.lists[to].begin(), pk.lists[to].end(), s) == pk.lists[to].end()) pk.lists[to].push_back(s);
    return pk.lines > 0;
}

std::vector<TypingPack> typingPacksScan(const std::vector<std::string>& dataDirs)
{
    std::map<std::string, TypingPack> byId;
    std::error_code ec;
    for (const std::string& d : dataDirs) {
        const fs::path dir = fs::path(d) / "data" / "typing" / "packs";
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            const bool folder = e.is_directory(ec);
            if (!folder && !(e.is_regular_file(ec) && lower(e.path().extension().string()) == ".txt")) continue;
            const std::string id = folder ? e.path().filename().string() : e.path().stem().string();
            if (id.empty() || id == kBuiltinPack || id.find(',') != std::string::npos) continue;
            TypingPack pk;
            pk.id = id;
            if (typingPackRead(e.path().string(), pk)) byId[id] = std::move(pk);
        }
    }
    std::vector<TypingPack> out;
    for (auto& [id, pk] : byId) out.push_back(std::move(pk));
    return out;
}

std::vector<std::string> typingPackIds(const std::string& csv)
{
    std::vector<std::string> out;
    std::stringstream ss(csv);
    for (std::string s; std::getline(ss, s, ',');)
        if (!trimmed(s).empty() && std::find(out.begin(), out.end(), trimmed(s)) == out.end()) out.push_back(trimmed(s));
    if (out.empty()) out.push_back(kBuiltinPack);
    return out;
}

std::string typingPackCsv(const std::vector<std::string>& ids)
{
    std::string s;
    for (const std::string& i : ids) s += (s.empty() ? "" : ",") + i;
    return s;
}

} // namespace hotd::game
