// The Typing of the Dead 0's word packs (worker totdpacks, 2026-11-09; the idea after Tenome's MIT "The Typing of the
// Dead EX" word packs: https://github.com/Tenome/Typing-of-the-Dead-EX, ideas only, no code). docs/typing.md "Word packs".
//
// A pack is a folder data/typing/packs/<id>/ of plain word lists (one word / phrase / sentence a line, UTF-8, '#'
// comments) with an optional pack.ini, or a single .txt dropped in data/typing/packs/ (the pack <id> = its name). Each
// line goes to one of the game's lists (typing.ini's names): a file named like a list (short.txt, words.txt, boss.txt ..:
// a copy of data/typing/default is a pack as it is) or named by pack.ini (<list> = file.txt) goes there whole; every
// other line is sorted by its letters (words: short / easy / words / long; phrases; sentences: the bosses'). A line with
// a character the prompts' font cannot draw (printable ASCII: the arcade's 8 x 8 font) is skipped and the character
// listed (Typing::Label text is ASCII). order = text: the pack's lines (its text) are served in order to the creatures.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace hotd::game {

struct TypingPack {
    std::string id;            // the folder's / file's name (launcher.ini totd_packs=)
    std::string name;          // pack.ini name = (else the id)
    std::string language;      // pack.ini language = (shown only)
    std::string path;          // the folder or the .txt
    bool text = false;         // pack.ini order = text: its lines in order (a story typed through the chapter)
    std::map<std::string, std::vector<std::string>> lists;   // the game's list name -> lines
    std::vector<std::string> pieces;   // order = text: the text in order, cut at spaces into pieces of <= chunk letters
    size_t lines = 0;          // lines taken
    size_t skipped = 0;        // lines skipped (a character the font lacks, too long / short for pack.ini's limits)
    std::string missing;       // the characters the font lacks (UTF-8, each once, the first 16)
    // "Spooky Words (en): 812 lines, 3 skipped (missing: é ñ)": the menu's help line
    std::string describe() const;
};

// the built-in list's id in totd_packs= (the game's own dictionary, typing.ini's dictionary = folder)
inline constexpr const char* kBuiltinPack = "builtin";

// every pack under each data folder's data/typing/packs (a later folder's pack of the same id replaces an earlier one's),
// read whole (small text files), sorted by id
std::vector<TypingPack> typingPacksScan(const std::vector<std::string>& dataDirs);
// one pack from its folder / file (false: nothing usable in it)
bool typingPackRead(const std::string& path, TypingPack& out);
// "builtin, spooky" -> {"builtin", "spooky"} (empty: {"builtin"}); and back
std::vector<std::string> typingPackIds(const std::string& csv);
std::string typingPackCsv(const std::vector<std::string>& ids);

} // namespace hotd::game
