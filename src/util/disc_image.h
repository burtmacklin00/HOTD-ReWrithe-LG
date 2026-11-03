// setup2 (the setup's drop zones, user 2026-11-02): a CD image of the player's own disc read as files. ISO 9660 only
// (the primary volume descriptor's names, 8.3 as the 1998 PC CD has them; no Joliet / Rock Ridge / UDF), from an
// .iso / .img (2048-byte sectors) or a .bin / .cue (raw 2352-byte sectors, Mode 1 or Mode 2 Form 1; a .cue's first
// FILE). The setup copies the image's files to a folder and runs the files check there. Read only.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace hotd::disc {

// the name ends in .iso, .img, .bin or .cue (any case)
bool isDiscImage(const std::string& path);

struct Info {
    bool ok = false;          // an ISO 9660 data track was found
    std::string system;       // "saturn", "dreamcast", "playstation" (a console disc), else ""
    std::string volume;       // the volume label
    uint64_t bytes = 0;       // the files' total size
    int files = 0;
    std::string err;          // why not (ok false)
};
Info probe(const std::string& path);

// every file of the image into outDir (folders kept); progress 0..1. false: err says why
bool extract(const std::string& path, const std::string& outDir, const std::function<void(float)>& progress,
             std::string& err);

// (user 2026-11-02 "rip the CD music") the image's CD audio tracks, from its .cue (the path itself, or a .cue next to
// it naming it), into outDir as TrackNN.wav (the CD's own numbers; 16-bit stereo 44.1 kHz; from INDEX 01, the long
// silence at each track's end cut to 1.9 s). The count written (0: no .cue / no audio tracks); err: the last problem
std::string cueFor(const std::string& path);
int ripAudio(const std::string& path, const std::string& outDir, std::string& err);

} // namespace hotd::disc
