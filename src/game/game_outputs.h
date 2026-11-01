// Light Gun Edition: game outputs for force-feedback tools (Hook of the Reaper, MAMEHooker, QMamehook, OutputHooker).
//
// Serves the MAME network output protocol on TCP 127.0.0.1:8000 (the one MAME and DemulShooter use):
//   on connect   "mame_stop = 1\r" then "mame_start = hotdrw\r", then every current value
//   on change    "<name> = <value>\r"
//   on exit      "mame_stop = 1\r"
// The names are the same as the HOTD2RE Light Gun Edition's, so one Hook of the Reaper game file fits both
// (setup\Hook of the Reaper\defaultLG\hotdrw.txt).
//
//   (P1-P4: players 3 / 4 in the 4-player mode)
//   P1_CtmRecoil / P2_CtmRecoil     a short pulse for every bullet that leaves the gun (not reloads, not an empty gun)
//   P1_Damaged   / P2_Damaged       a short pulse when the player loses a life (not the last one: that is death)
//   P1_Dead      / P2_Dead          1 while the player is dead (continue countdown / game over), else 0
//   P1_Ammo      / P2_Ammo          rounds in the gun (0 when not playing)
//   P1_Clip      / P2_Clip          1 while the gun has rounds, 0 when empty / not playing
//   P1_Life      / P2_Life          lives (torches) left (0 when not playing)
//   P1_CtmLmpStart / P2_CtmLmpStart start lamp: blinks while the player can join, off while playing
//
// Kept apart from the game's own code (it only reads a snapshot filled in main.cpp), so new ReWrithe releases merge
// without touching it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hotd::game {

// One output player's view of the game, filled once per game frame from the arcade memory (main.cpp outputsFrame).
struct OutputSeat {
    bool valid = false;   // a person's seat on this PC (not a CPU teammate, not another PC's seat online)
    int state = 9;        // the arcade player state: 5 in play, 4 out of lives (continue), 9 not in play, 7 ending
    bool fired = false;   // a bullet left the gun this frame (Player flags bit 1, set only by a real shot)
    int lives = 0;
    int rounds = 0;
};

struct OutputFrame {
    int gameMode = 0;     // 4 in a game, 5 game over, others: title / attract / menus
    OutputSeat seat[4];   // P1-P4 outputs
};

struct GameOutputsConfig {
    int port = 8000;
    int recoilOnMs = 40, recoilOffMs = 40;   // one recoil pulse (DemulShooter's custom recoil defaults)
    int damagedMs = 200;                     // length of the Damaged pulse
    // the outputs log (the debug launcher: HOTD_OUTPUTS_LOG=1): every message sent, the tools connecting / leaving,
    // with the time since the start (ms); "" none. The start lamps' blinking is left out (it is constant).
    std::string logFile;
};

class GameOutputs {
public:
    GameOutputs() = default;
    ~GameOutputs();
    GameOutputs(const GameOutputs&) = delete;
    GameOutputs& operator=(const GameOutputs&) = delete;

    // Opens the listening socket. Returns false (outputs stay off) if the port can't be opened.
    bool start(const GameOutputsConfig& config, std::string* error = nullptr);
    void stop();
    bool running() const { return listener_ != ~uintptr_t(0); }

    // Once per game frame, after the frame ran. `live` = false (headless, replays) updates values without pulses.
    void onFrame(const OutputFrame& f, bool live, uint64_t nowMs);
    // Every loop (also while paused / in the menus): finishes timed pulses, blinks the lamps, takes new connections.
    void tick(uint64_t nowMs);

private:
    struct Output { std::string name; int value = -1; };
    struct Pulse { int output = -1; int queued = 0; bool on = false; uint64_t until = 0; int onMs = 0, offMs = 0; };
    struct Track { int lastLives = -1; bool wasPlaying = false; };

    int add(const std::string& name);
    void set(int output, int value);
    void fire(Pulse& pulse, uint64_t nowMs);
    void runPulse(Pulse& pulse, uint64_t nowMs);
    void acceptClients();
    void sendAll(const std::string& line);
    bool sendTo(uintptr_t client, const std::string& line);
    static std::string line(const std::string& name, int value);

    GameOutputsConfig config_;
    uintptr_t listener_ = ~uintptr_t(0);
    std::vector<uintptr_t> clients_;
    std::vector<Output> outputs_;
    bool wsa_ = false;
    void* log_ = nullptr;        // FILE* (the outputs log)
    uint64_t logT0_ = 0;
    void logLine(const std::string& text);

    static constexpr int kSeats = 4;
    int recoil_[kSeats] = {-1, -1, -1, -1}, damaged_[kSeats] = {-1, -1, -1, -1}, dead_[kSeats] = {-1, -1, -1, -1},
        ammo_[kSeats] = {-1, -1, -1, -1}, clip_[kSeats] = {-1, -1, -1, -1}, life_[kSeats] = {-1, -1, -1, -1},
        lamp_[kSeats] = {-1, -1, -1, -1};
    Pulse recoilPulse_[kSeats], damagedPulse_[kSeats];
    Track track_[kSeats];
    bool lampBlink_[kSeats] = {true, true, true, true};
};

} // namespace hotd::game
