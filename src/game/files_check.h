// The official-files check (docs/teaser_plan.md, the Engine table's last row): the launcher checks that the player's
// files are the official releases, by content only (folder, zip and file names are free).
//
// Arcade: every ROM of MAME's `hotd` set (Revision A, 29 files) is matched by size + CRC-32 among the zips and loose
// files found in the chosen place (a zip, or a folder searched a few levels deep), then its SHA-1 is checked when it
// is read. The program, data, coprocessor, polygon and texture regions are rebuilt from them (as tools/m2_roms.py)
// into the cache folder; the result is remembered (the sources' sizes and dates), so the next start reads nothing.
// PC: the files the rewrite reads (src/game/pc_manifest.inc) are looked for by their path in the chosen folder
// (case-insensitive) and checked by size + SHA-1; a patched install whose patch kept a `.orig` copy passes through
// that copy; a file not found there (renamed or moved) is looked for once by size + SHA-1 among the folder's files
// and the place remembered. Every mismatch is reported with the file and the release expected.
// pchotfix (2026-11-02): a file there of another version (Japanese release, patch, repack) is accepted with a note;
// only THOTD.EXE and the main data files missing (or not even the file type) block Play; a missing sound is a note.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace hotd::game {

struct CheckReport {
    bool ok = false;
    std::vector<std::string> lines;   // what was found / wrong, one line each (for the launcher and the log)
    std::vector<std::string> notes;   // pchotfix: short notes for the setup when ok (files of another version accepted ...)
};

// the arcade set: `where` = a zip or a folder; on success the regions (maincpu.bin, main_data.bin, copro_data.bin,
// polygons.bin, textures.bin) are in `regionsDir` (created). progress: a short status line now and then (may be empty)
CheckReport checkArcade(const std::string& where, const std::string& regionsDir, bool force,
                        const std::function<void(const std::string&)>& progress = {});

// the PC files: `folder` = the PC game's folder; the resolved place of every file (manifest path -> real path) goes
// to `found` and is remembered in `cacheFile`. Music (Music\TrackNN.ogg, CD audio rips) is reported, not checked.
CheckReport checkPc(const std::string& folder, const std::string& cacheFile, std::map<std::string, std::string>& found,
                    const std::function<void(const std::string&)>& progress = {});

// the map of checked files goes to hotd::setPcFileMap (assets/archive.h), which the loaders consult

} // namespace hotd::game

namespace hotd::game {

// firstrun: after a failed check, one line telling the player what to put where (empty: nothing to add), from what the
// chosen place holds: nothing chosen, a .7z / .rar / CD image, a zip of zips, the PC game where the ROMs belong (or the
// other way round). `arcade`: the place was chosen as the arcade ROMs (else as the PC files)
std::string placementHint(const std::string& where, bool arcade);

// the European CD's BIGGER.VMC (user 2026-11-02 "patch their BIGGER.VMC for them"): the US file with 5 bits set (5
// texels of the texture pages a shade off: a mastering fault, not a change). isEuBigger: d is that file (rel: its path
// in the install, any case / slashes); fixEuBigger: those 5 bytes set back, the US file again (true: patched)
bool isEuBigger(const std::string& rel, const std::vector<uint8_t>& d);

// spanish (docs/spanish.md): `folder` (a PC game folder or .zip) holds the Spanish release's voices: its
// SOUND\O_VOC02.RBH is the Spanish CD's (size + SHA-1, pc_manifest_es.inc). Read once per folder (remembered)
bool isSpanishRelease(const std::string& folder);
// the checked PC files' install paths (pc_manifest.inc: "THOTD.EXE", "SOUND/O_AAAH.RBH" ...; v1.3.1: the CD
// cabinets' files placed by name, util/cab_unpack.h)
std::vector<std::string> pcManifestPaths();
// the folder of the Spanish voices for Audio > Voice: `spanishPc` if it is one, else `pc` if it is, else
// <packageRoot>\data\pc_spanish (or a folder in it) if it is; empty: none
std::string spanishVoiceDir(const std::string& pc, const std::string& spanishPc, const std::string& packageRoot);
// spanish2: Audio > Voice (LaunchConfig::voice: 0 Auto = the Spanish dub while the language is Español, 1 Spanish,
// 2 English): the Spanish voices wanted
bool spanishVoiceOn(int voice, const std::string& language);
// spanish2: the Spanish copy's ART\STAGE1\CREATURE\BIGGER.VMC for the translated texture pages
// (setPcSpanishTextures) while `language` is "es" and a Spanish copy is there (spanishVoiceDir); else ""
std::string spanishTextureVmc(const std::string& language, const std::string& pc, const std::string& spanishPc,
                              const std::string& packageRoot);
bool fixEuBigger(const std::string& rel, std::vector<uint8_t>& d);

} // namespace hotd::game

namespace hotd::game {

// setup2 (the installer, game/installer.h): the arcade set's file names, sizes and CRC-32s (MAME hotd, Revision A), so
// the setup can copy exactly those 29 files out of the player's zip / folder into the install's data\arcade
struct ArcadeRomId {
    const char* name;
    uint32_t size, crc;
};
const ArcadeRomId* arcadeRomIds(int& n);

} // namespace hotd::game
