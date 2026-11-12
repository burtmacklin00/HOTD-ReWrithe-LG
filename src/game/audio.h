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

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
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
    const std::string& pcRoot() const { return root_; }
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

    // spanish (3lc3z4r's copy, 2026-11-06): the Spanish release's dub ("Doblaje español", docs/spanish.md): dir = the
    // folder of a Spanish PC copy (its dubbed SOUND\O_*.RBH lines and GAMESNDS.RBH's one dubbed sample), empty = the
    // voices as the PC files / the arcade board have them. Set before init. With the arcade board the voice cues then
    // play the Spanish samples instead of the board's English lines (every mode, the arcade models too)
    void setVoiceDir(const std::string& dir);
    const std::string& voiceDir() const { return voiceDir_; }
    bool isVoiceCue(int cue) const;                   // a PC cue the Spanish release dubbed
    static bool isDubbedFile(const std::string& name);   // "VOC02" ...: SOUND\O_<name>.RBH is a dubbed voice line
    // spanish: Audio > Reload Voice: false leaves out the "Reload!" voice of an empty gun's trigger (arcade code
    // 0xA01404 = PC cue 161, GAMESNDS.RBH sample 36: what the Spanish release made silent), the board's and the PC's
    void setReloadVoice(bool on) { reloadVoice_ = on; }
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
    void reportCues(float peak);             // (testing, HOTD_TOTD_SOUNDLIST: the cues by loudness at that peak)
    int pcCueOf(uint32_t arcadeCode) const;  // -1: none
    bool isMusicCode(uint32_t arcadeCode) const;   // its PC cue starts a music track or turns the music off
    bool isMusicStart(uint32_t arcadeCode) const;  // its PC cue starts a music track
    // Survival Mode (ours, game/survival.h): an extra sound: <dir>/<name>.wav when that file is there (RIFF PCM, 8 / 16-bit,
    // mono / stereo, any rate), else the stand-in: PC cue `cue`'s bank sample played `pitch` times faster and cut to
    // `clip` of its length. Loaded once. false: nothing to play (no file, no stand-in). extraSource(name): "file",
    // "stand-in" or "" (not loaded / nothing)
    // totd5: a file cue (O_*.RBH) as the stand-in too; normalize > 0: its peak brought to that share of full scale first
    bool playExtra(const std::string& dir, const std::string& name, int cue, float pitch, float clip, float volume = 1.0f,
                   float normalize = 0.0f);
    std::string extraSource(const std::string& name) const;
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
        // totd7: a normalized extra (playExtra's normalize > 0: The Typing of the Dead 0's key / miss / ding): mixed
        // after the PC sound's limiter, so the music / effects in it never squeeze it
        bool direct = false;
    };
    bool nextDirect_ = false;   // (playExtra -> play: the voice it starts is a direct one)
    std::vector<float> directBuf_;
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
    std::string voiceDir_;   // setVoiceDir
    bool reloadVoice_ = true;   // setReloadVoice
    std::map<int, TrackFile> tracks_;   // track number -> its file (indexTracks)
    bool tracksIndexed_ = false;
    std::set<int> loggedTracks_;        // tracks whose file was logged
    std::vector<Cue> cues_;
    std::vector<SampleInfo> samples_;
    std::vector<uint8_t> bank_;                                  // GAMESNDS.RBH BODY 1
    std::map<std::string, std::vector<uint8_t>> oneShots_;       // O_*.RBH sample data, by name
    std::map<uint32_t, int> cueMap_;                             // arcade code -> PC cue
    struct Extra { std::vector<uint8_t> data; int sample = -1; bool file = false; };
    std::map<std::string, Extra> extras_;                        // playExtra's sounds (8-bit unsigned mono), by name
    mutable std::recursive_mutex lock_;                                // voices / music against the audio thread
    Voice voices_[16];
    Voice tails_[8];   // stopped / replaced voices ramping down (cut)
    std::unique_ptr<Music> music_;
    SDL_AudioStream* stream_ = nullptr;
    // questaudio: the device when opened by hand (Android: at 48 kHz; 0 = SDL_OpenAudioDeviceStream's own), and the
    // log's count of the output's late pulls (audio.cpp, callback)
    uint32_t device_ = 0;
    uint64_t deviceBufNs_ = 0, lastPullNs_ = 0, lateWorstNs_ = 0, lateLogNs_ = 0, lateCount_ = 0;
    // (questaudio2: the output callback's own time and the wait for lock_ in it, worst per 10 s, logged with the late pulls)
    uint64_t cbWorstNs_ = 0, lockWaitWorstNs_ = 0, cbSlow_ = 0;
    std::atomic<uint64_t> lockWaitNs_{0};
    std::atomic<uint64_t> lateTotal_{0};
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
    // audioload (user 2026-11-10: "my audio was crackling", PC VR with the CPU saturated): with a device the board no
    // longer runs inside SDL's callback (under lock_, just in time) but ahead on its own thread into a ring, lead_
    // frames (HOTD_AUDIO_AHEAD_MS, default 46 ms; 0 = the old way); the game's codes go in stamped at the output
    // position + lead_, so they land on the board sample the stamp says whatever the thread timing (a constant
    // latency). The board's samples are the same, only when they are made changes. An underrun fades out, is logged.
    void boardLoop();
    void fakeDeviceLoop();
    void takeBoard(float* out, int frames);
    void ringReset();   // (ringLock_ held) after the board's reset / load / skip: the ring is empty, positions 0
    bool boardCommand(uint32_t code);   // into the ring's queue (false: no ring, the board takes it directly)
    std::thread boardThread_, fakeThread_;
    uint64_t fakeUntil_ = 0;   // HOTD_AUDIO_FAKE_DEVICE=<seconds>: shutdown waits until then (SDL ticks, ns)
    std::atomic<bool> threadsRun_{false};
    mutable std::mutex boardLock_;   // board_ between the board thread and the game thread (lock order: boardLock_, ringLock_)
    mutable std::mutex ringLock_;    // the ring, its positions, the code queue
    std::condition_variable ringSpace_, ringData_;
    std::vector<float> ring_;                    // ringCap_ stereo frames
    size_t ringCap_ = 0, ringRead_ = 0, ringFill_ = 0;
    uint64_t emuPos_ = 0, consumed_ = 0;         // board frames made / taken since the ring's reset
    int lead_ = 0, want_ = 0, resume_ = 0;       // want_: the WAV test's waiting consumer; resume_: frames to start on
    bool ringOn_ = false, ringTest_ = false, primed_ = false, underrun_ = false, boardReady_ = false;
    std::deque<std::pair<uint64_t, uint32_t>> cmds_;   // (board frame, code)
    float lastOut_[2] = {};
    int fadeIn_ = 0;
    uint64_t underrunFrames_ = 0;                // this underrun's length so far
    std::atomic<uint64_t> underruns_{0}, underrunTotal_{0}, underrunLog_{0};   // underrunLog_: frames to log
};

} // namespace hotd::game
