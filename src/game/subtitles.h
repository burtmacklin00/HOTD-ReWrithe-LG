// subs (user 2026-11-09, after Tenome's MIT "Typing of the Dead EX", whose mod/README.md lets players edit their own
// subtitle files in any language, with timing, size and position): editable cut-scene captions (docs/subtitles.md).
//
// A caption is one line of a voice message (kind 161, voice_message_task 0x9B380): the subtitle table
// G::voice_subtitles holds 96 bytes an event, line 1 at +1, line 2 at +0x31; its id is <event>.<line> (the same id
// as data/lang's sub.<event>.<line>). A subtitle file (subtitles/<language code>.txt in an On mod's folder, in the
// settings folder, or data/subtitles/<code>.txt) changes a line's text, its start / end (frames or ms against the
// game's own), its size and its place. Options > Gameplay > Subtitles: Off / Original (the game's lines, a language
// file's translation: as before) / Custom (the file's changes; a missing file or line: Original exactly), and
// Subtitle Size. Draw-only: nothing here touches the game's memory.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace hotd::subs {

enum Mode { kOff = 0, kOriginal = 1, kCustom = 2 };
enum Place { kPlaceDefault = 0, kPlaceBottom, kPlaceTop, kPlaceY };

struct Entry {
    bool hasText = false;
    std::string text;            // (hasText; "" with hide)
    bool hide = false;           // the line not shown at all
    int start = 0, end = 0;      // frames (60 a second) after (+) / before (-) the game's own start / end
    int size = 100;              // percent of the caption's normal size
    Place place = kPlaceDefault;
    float y = 0.0f;              // kPlaceY: the line's centre, percent of the 4:3 screen's height from the top
    // changes nothing (the game's own line as it is): Custom draws the original then
    bool neutral(const std::string& gameText) const;
};

// the settings (LaunchConfig subtitles= / subtitlesize=): set at start and when the Options change
void configure(int mode, int sizePercent);
int mode();
int sizePercent();
const char* modeName(int mode);        // "Off" / "Original" / "Custom"
int modeParse(const std::string& s);   // off / original / custom (or 0 / 1 / 2); -1 none
// Subtitle Size's steps (percent)
const std::vector<int>& sizeSteps();

// the folders searched, first wins: the On mods' folders (<mod>/subtitles/<code>.txt), the settings folder
// (<pref>/subtitles/<code>.txt), the data folders (<dir>/data/subtitles/<code>.txt; lang::dataDirs())
void setModDirs(const std::vector<std::string>& dirs);
void setPrefDir(const std::string& dir);

// the current language's file (loaded again when the language, the folders or the file's time change); null: no
// line for that id
const Entry* find(int event, int line);
const std::string& fileInUse();        // "" none
// parses one file's text into entries (event * 2 + line - 1); bad lines are listed in errors (line numbers)
std::map<int, Entry> parse(const std::string& text, std::vector<std::string>* errors = nullptr);

// HOTD_TRACE_SUBS=1: each caption's id, frame and text printed as the game shows / clears it
bool trace();

} // namespace hotd::subs
