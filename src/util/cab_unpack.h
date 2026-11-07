// v1.3.1 (user 2026-11-07: the Spanish CD image dropped on the Extras box was refused): the Spanish 2003 PC CD keeps
// the game packed in Microsoft cabinets (CAB\CAB1.CAB .. CAB8.CAB, LZX, files continued from one cabinet into the
// next) for its own installer; only the three VMC texture files are loose. This unpacks such a set and lays the files
// out as the installed game (SOUND\, SPRITE\, ART\STAGEn\, CAM\, EVT\ ...), so the files check and the setup read it
// like an installed copy. Windows: the system's own cabinet.dll (FDI). Linux / macOS: cabextract (it follows the set),
// else bsdtar (libarchive; no files continued across cabinets: the check then says what is missing). Read only.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace hotd::cab {

// `dir` holds a cabinet set of the game (a CAB folder with CAB1.CAB, any case) and no THOTD.EXE: its CAB1.CAB, else ""
std::string gameCabinet(const std::string& dir);

// the set from `cab1` on into `outDir` (its own folder names); false: err says why (a missing tool: toolMissing)
bool extractSet(const std::string& cab1, const std::string& outDir, std::string& err, bool& toolMissing);

// the unpacked files (`unpacked`) and the CD's loose game files (`disc`: its ART\ folder) moved / copied into `out`
// in the installed layout; `knownPaths`: the checked files' install paths (pc_manifest.inc: "SOUND/O_AAAH.RBH" ...),
// which place a file by its name. The count of files placed
int arrange(const std::string& unpacked, const std::string& disc, const std::string& out,
            const std::vector<std::string>& knownPaths);

} // namespace hotd::cab
