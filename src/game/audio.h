// Sound and music from the PC port's files (docs/runtime.md "Sound"; docs/formats.md "PC sound files").
//
// The arcade logic calls sound_cue(code) with a sound-board command (0xA0xxxx). The runtime turns it into the PC's cue
// number (src/game/cue_map.inc, tools/cue_pairs.py) and does what the PC's sound_cue (0x412000) does with its cue table
// (THOTD.EXE 0x5CBDA8, 12 bytes per cue: type, name, value): 1 play a sample of SOUND\GAMESNDS.RBH, 2 stop a sample,
// 3 a CD track (Music\TrackNN.ogg, looping except cues 11 / 13), 4 load SOUND\O_<name>.RBH and play it, 5 a command
// (1 = music off, 2 / 3 / 7 = every sample off). Samples are 8-bit unsigned mono, 11025 Hz; this mixer plays them on
// an SDL3 audio stream (44.1 kHz stereo float), 16 voices.
//
// OGG music needs stb_vorbis (CMake HOTD_STB_DIR / HOTD_FETCH_STB), MP3 music dr_mp3 (HOTD_DR_LIBS_DIR); without either
// the music cues are logged and skipped.
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace hotd::audio { class SoundBoard; }

struct SDL_AudioStream;

namespace hotd::game {

class Audio {
public:
    Audio();
    ~Audio();
    // pcRoot: the PC game's folder (THOTD.EXE, SOUND\, Music\); device false: no audio device (headless runs: the cues
    // are still decoded and counted)
    bool init(const std::string& pcRoot, bool device, std::string& err);
    void shutdown();
    // --audio-wav: the mixed output goes to a WAV file (44.1 kHz stereo 16-bit), one game frame (1 / the tick rate) per
    // renderFrame() call from the frame loop, instead of the device (nothing is played aloud)
    bool openWav(const std::string& path, std::string& err);
    void renderFrame();
    // rate: the game frames a second the WAV's renderFrame stands for (the game speed: 57.524 arcade, 60). The live
    // device is pulled by its own clock: there the music / board run at their true speed whatever the tick rate
    void setTickRate(double hz) { tickHz_ = hz > 1.0 ? hz : 60.0; }
    // Light Gun Edition: Turbo Speed Run: the live output played faster (pitch with it), 1 = normal
    void setSpeed(float ratio);
    void closeWav();
    bool wavOpen() const { return wav_ != nullptr; }

    void cue(uint32_t arcadeCode);   // the arcade's sound_cue (0x154D0) argument
    void pcCue(int cue);             // the PC's sound_cue (0x412000) argument
    void setVolume(float sfx, float music) { sfxVolume_ = sfx; musicVolume_ = music; }
    // launcher / --no-music: the music is not mixed while off (the track keeps its place); effects play on
    void setMusicOn(bool on) { musicOn_ = on; }
    bool musicOn() const { return musicOn_; }
    // leaving play / the attract: the music and sounds end, the arcade board's too (user 2026-10-18: the attract's board
    // music went on under the menu music)
    void silence() { stopMusic(); stopAll(); musicHeld_ = false; resetBoard(); }
    void resetBoard();
    // the in-game pause (game/pause.h): true holds every sound that plays and the music where they are, false lets them
    // go on from there (never cued again); sounds started while held play
    void pause(bool hold);   // leaving the game (Esc to the launcher): music and sounds end
    // a cut-scene's hold-to-skip (ours, main.cpp): while on, the effects started are silent for their whole length
    // (the scene's lines and sounds, run through in a moment, are not heard); loops play as usual
    void quietEffects(bool on) { std::lock_guard<std::recursive_mutex> g(lock_); quietEffects_ = on; }

    std::function<void(const std::string&)> log;
    std::string packageRoot;                 // the portable package's folder (its data\music: the CD tracks), or empty
    int musicNumbering = 0;                  // the music files' numbers: 0 auto, 1 the PC CD's, 2 the Saturn disc's
    std::map<int, uint64_t> cueCount;        // PC cues played (diagnostics)
    std::map<uint32_t, uint64_t> codeCount;  // arcade sound codes received (diagnostics)
    uint64_t frameNo = 0;                    // set by the frame loop (the cue trace's frame numbers)
    bool traceCues = false;                  // HOTD_TRACE_SOUND: every cue with its frame
    std::string describe(int cue) const;     // "type name / value" of a PC cue (the report)
    int pcCueOf(uint32_t arcadeCode) const;  // -1: none
    bool isMusicCode(uint32_t arcadeCode) const;   // its PC cue starts a music track or turns the music off
    std::set<uint32_t> unmapped;             // arcade codes without a PC cue (logged once)
    std::set<int> silent;                    // PC sample / file cues that had nothing to mix (missing data)
    bool musicAvailable() const;

    // the Jukebox (game/jukebox.h): the PC's music files and cues played directly, and what plays now
    bool playMusicTrack(int track, bool loop);   // Music\TrackNN (false: no file / cannot be opened)
    bool hasMusicTrack(int track);               // a file for that track was found
    bool musicPlaying() const;
    // the track's place and length in seconds (len 0: not known); loops: how often it went round (Loop one)
    bool musicTime(double& pos, double& len, int& loops) const;
    void seekMusic(double seconds);
    void stopPcSound() { stopMusic(); stopAll(); }
    int cueType(int cue) const { return cue >= 0 && cue < int(cues_.size()) ? cues_[size_t(cue)].type : 0; }
    int cueValue(int cue) const { return cue >= 0 && cue < int(cues_.size()) ? cues_[size_t(cue)].value : -1; }
    std::string cueFile(int cue) const { return cue >= 0 && cue < int(cues_.size()) ? cues_[size_t(cue)].name : std::string(); }
    double cueLength(int cue);        // a sample / file cue's length in seconds (0: none)
    bool cueSounding(int cue);        // a voice still plays that cue's sample
    bool cueLoops(int cue) const;     // that sample loops (a loop start, or the "has a stop cue" flag)
    double trackLength(int track);    // a music file's length in seconds (0: none; read once)
    // the arcade board's output level (the peak of the last mixed block, before the gain) and its own volume (-1: the
    // effects volume, as in the game; the Jukebox's music uses the music volume)
    float boardPeak() const { return boardPeak_; }
    void setBoardVolume(float v) { boardVolume_ = v; }
    // the mix into a buffer (frames stereo; the headless Jukebox test), instead of the device / the WAV
    void renderTo(std::vector<float>& out, int frames)
    {
        out.resize(size_t(frames) * 2);
        mix(out.data(), frames);
    }

    // the arcade sound board (src/audio/sound_board.h; built with HOTD_ARCADE_SOUND): the game's sound codes go to it
    // instead of the PC cues, its output is mixed in. program / samples: the ROM files as they are (epr-19720.31;
    // mpr-19721.32 + mpr-19722.34). False (err): not built in or no ROMs; the PC sound stays.
    bool startBoard(const std::vector<uint8_t>& program, const std::vector<uint8_t>& samples, std::string& err);
    void stopBoard();
    bool boardOn() const { return boardOn_; }
    // the board's clock (44.1 kHz samples run since its reset; the Jukebox's arcade time) and a fast run of it with its
    // output thrown away (the Jukebox's seek: the code sent again, then run up to the place)
    uint64_t boardSamples() const;
    void boardSkip(int frames);

    // a GAMESNDS.RBH sample entry (40 bytes): bits 8, volume 0-255, length, rate, -1, source (1 = this bank, 2 = an
    // O_ file), O_ file number, offset in BODY 1, loop start, 0, flags (16.16; bit 24 = loops)
    struct SampleInfo {
        uint16_t bits, volume;
        uint32_t length, rate;
        int32_t unk;
        uint32_t source, file, offset, loopStart, zero, flags;
    };

private:
    struct Cue { int type = 0; std::string name; int32_t value = -1; };
    struct Voice {
        const uint8_t* data = nullptr;   // 8-bit unsigned
        uint32_t length = 0, loopStart = 0;
        bool loop = false;
        double pos = 0, step = 0;
        float volume = 1;
        int sample = -1;
        bool held = false;   // Audio::pause
        bool quiet = false;  // started during a cut-scene's skip (quietEffects): mixed silent to its end
        float amp = 0;       // the start / stop ramp (our own, against clicks): 0 -> 1 over kFade frames at the start
        bool release = false;   // a tail (tails_): ramps down to 0, then ends
    };
    void cut(Voice& v);   // the voice ends: its sound goes on in a tail that ramps down (no click), the voice is free
    struct Music;

    bool loadCueTable(const std::string& exePath, std::string& err);
    bool loadBank(const std::string& path, std::string& err);
    const std::vector<uint8_t>* loadOneShot(const std::string& name);
    bool play(int sample, const uint8_t* data, uint32_t size);   // data null: the bank; false: nothing to mix
    void stopSample(int sample);
    void stopAll();
    struct TrackFile { std::string path, where; int rank = 0; };
    void indexTracks();   // the music files of every music place (audio.cpp "musicdirs")
    void playTrack(int track, bool loop);
    void stopMusic();
    void mix(float* out, int frames);
    static void callback(void* self, SDL_AudioStream* stream, int additional, int total);
    void note(const std::string& s) { if (log) log(s); }

    std::string root_;
    std::map<int, TrackFile> tracks_;   // track number -> its file (indexTracks)
    bool tracksIndexed_ = false;
    std::set<int> loggedTracks_;        // tracks whose file was logged
    std::vector<Cue> cues_;
    std::vector<SampleInfo> samples_;
    std::vector<uint8_t> bank_;                                  // GAMESNDS.RBH BODY 1
    std::map<std::string, std::vector<uint8_t>> oneShots_;       // O_*.RBH sample data, by name
    std::map<uint32_t, int> cueMap_;                             // arcade code -> PC cue
    mutable std::recursive_mutex lock_;                                // voices / music against the audio thread
    Voice voices_[16];
    Voice tails_[8];   // stopped / replaced voices ramping down (cut)
    std::unique_ptr<Music> music_;
    SDL_AudioStream* stream_ = nullptr;
    float sfxVolume_ = 0.8f, musicVolume_ = 0.6f;
    bool quietEffects_ = false;
    bool musicOn_ = true;
    bool musicHeld_ = false;   // Audio::pause
    std::vector<float> mixBuf_;
    std::FILE* wav_ = nullptr;
    uint64_t wavFrames_ = 0;
    double wavFrac_ = 0;
    double tickHz_ = 60.0;
    std::unique_ptr<audio::SoundBoard> board_;
    bool boardOn_ = false, boardHeld_ = false;
    float boardPeak_ = 0, boardVolume_ = -1;
    std::vector<float> boardBuf_;
};

} // namespace hotd::game
