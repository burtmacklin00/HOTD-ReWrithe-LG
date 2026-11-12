// Netplay's game glue (docs/netplay.md): the lockstep between the net session and the game runtime. The game loop
// (headless or the window) asks step() before each game frame and after() after it; everything else stays in
// src/net/session.*. The connection: open() then poll() each frame (the waiting room; the host's hostStart() ends
// it), or connect() for the command line (blocking).
#pragma once

#include "net/lobby.h"
#include "net/natpunch.h"
#include "net/rollback.h"
#include "net/session.h"
#include "net/upnp.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace hotd::game { class Runtime; }
namespace hotd::port { class Mem; }

namespace hotd::net {

// the netplay options (the command line's; the menu fills the same)
struct NetArgs {
    std::string host, join, code;   // --host [addr:]port, --join addr:port | share code | lan[:name], --code XXXX
    std::string lanName;            // --lan-name: the game's name in the LAN list
    std::string playerName;         // --net-name: this player's name in the waiting rooms (the menu: "Your Name")
    std::string lobby;              // --lobby addr:port (tests: the LAN list on the loopback)
    std::string upnp = "on";        // --upnp on | off | dry-run
    std::string bind;               // --net-bind addr: the menu's host binds there (tests: 127.0.0.1)
    int menuPort = 0;               // --net-port N: the menu's host port (default 47800)
    bool noLan = false;             // --no-lan: not announced
    bool lanCode = false;           // --lan-code: the LAN list needs the code too (default: a LAN click joins, the
                                    // announcement carries the password; docs/netplay.md "LAN lobby")
    // a joined PC left mid-game (closed, quit, or silent 6 s; docs/netplay.md "A player left"): -2 the default (a
    // window game, and a two-PC game: wait until it rejoins or the host chooses to go on; a headless game of up to
    // four PCs: on at once, as before), -1 wait, 0 continue at once, N > 0: wait N s, then on without it (--net-drop)
    int dropContinueS = -2;
    int delay = 0, hashEvery = 1, timeout = 20, simLoss = 0, simDelay = 0, fuzz = 0;   // delay 0 = auto
    int rollback = 8;               // --net-rollback N: frames run ahead on a prediction (0: lockstep only)
    bool alone = false;             // --net-alone: the host plays at once; player 2 can join the running game
    int soloFps = 0;                // --net-solo-fps N (testing): a headless host alone runs N frames a second
    int pcs = 2;                    // --net-pcs 4: the host takes up to three joining PCs, one seat each (the 4P game)
    int waitFor = 1;                // --net-wait N: the command line's host starts once N PCs joined (up to four PCs)
    int seats = 1;                  // --net-seats 2: up to four PCs, this PC plays two seats (the host: P1 + P3; a joiner
                                    // asks, and gets two when two are free); the menu's "Seats on this PC"
    // PC Mode online (docs/pcmode.md "Online"): the host's game is PC Mode (--pc-mode; the Host page's Game Mode); this
    // PC's characters for its first / second seat (--net-pc-char a[,b]; the pages' Your Character): 1-6, 0 = the default
    bool pcMode = false;
    int pcChar[2] = {};
    // charsel: an arcade game (not PC Mode) whose host picked characters (Character Select, cosmetic; main: --char /
    // characters=, then pcChar holds those picks): the settings carry every seat's character (protocol 21)
    bool charPick = false;
    // Boss Mode online (protocol 18): the host's boss game (--boss-mode; the Host page's Game Mode): 0 none, 2-6
    int bossMode = 0;
    // Survival Mode online (protocol 22): the host's Survival game (--survival with --host; the Host page's Game Mode):
    // 0 none, else the arena + 1 (never with PC Mode / Boss Mode)
    int survival = 0;
    // the same game data (protocol 19; docs/netplay.md "Same game data"): this PC's digests (main fills them before
    // open: gameDataDigests); the host refuses a joiner whose ROMs differ, or whose PC data differ in a PC Mode / Boss
    // Mode game
    uint64_t romData = 0, pcData = 0;
    // online without port forwarding (natpunch.h; docs/netplay.md "Hole punching"): the STUN servers in fallback order
    // (--stun a,b | off; launcher.ini netstun= / stunservers=), "off" = as before (UPnP / port forwarding only).
    // Testing: --stun-mock p[,p2] (tiny STUN servers on 127.0.0.1 in this process; "p+N": that one answers N ports
    // off, a symmetric NAT), --net-sim-nat (a restricted NAT in front of this PC), --code-out FILE (the host's join
    // code once it has its address), --reply-out FILE (a joiner's reply code), --reply-from FILE (the host reads a
    // reply code there), --join @FILE (the code read from a file, waited for)
    std::string stun = kDefaultStun;
    std::string stunMock, codeOut, replyOut, replyFrom;
    bool simNat = false;
    bool active() const { return !host.empty() || !join.empty(); }
    // consumes one option (and its value through next); false: not a netplay option. err set on a bad value
    bool parse(const std::string& a, const std::function<std::string()>& next, std::string& err);
};

class NetGame {
public:
    static constexpr int kMaxRollback = 12;
    ~NetGame() { close(); }
    // the connection without blocking: open, then poll() every frame: 1 = start the game (client: the host's settings
    // are in `settings`; host: hostStart was called), 0 = still in the waiting room, -1 = failed (err)
    bool open(const NetArgs& a, std::string& err);
    int poll(Settings& settings, std::string& err, int waitMs = 0);
    void hostStart(Settings s);                  // host: Start (the input delay from the measured ping when auto)
    bool peerIn() const;                         // host: a player is in; client: in the waiting room
    // up to four PCs (docs/netplay.md "Four PCs"): one seat each, the host relays every frame's four inputs
    bool multi() const { return multi_ || session_.multi(); }
    int seatsHere() const { return secondSeat() >= 0 ? 2 : 1; }   // this PC's seats
    // this PC's second seat (the 4-seat game on two PCs: localPlayer + 2; up to four PCs: the host's P3 with --net-seats
    // 2, a joiner's from the settings), -1 none
    int secondSeat() const;
    bool mySeat(int s) const { return s == localPlayer() || s == secondSeat(); }
    // the waiting room's / the game's seats: each seat's PC in, and its ping to the host (the host's own: in, 0)
    void roster(Session::RosterSeat out[4]) const;
    // PC Mode online: seat s's character (the host: its own pick, a joiner's from its CONFIRM, else the seat's default
    // Rogan / G / Rogan 2 / G 2; once started the game's), -1 an arcade game
    int seatChar(int s) const;
    // Boss Mode online (protocol 18): the game's Boss Mode (0 none, 2-6: game/bossmode.h): the host's choice; a joiner's
    // waiting room: the host's ROSTER; once started: the game's settings
    int bossMode() const;
    // Survival Mode online (protocol 22): the game's arena + 1 (0 none), as bossMode(): the host's choice, a joiner's
    // ROSTER, once started the settings
    int survival() const;
    void close();                               // the LAN announcement and the UPnP mapping end
    // opens the session and waits for the other PC (host: up to timeoutS, 0 = until one joins; it starts by itself once
    // player 2 is in). The host passes its settings; the client receives the host's
    bool connect(const NetArgs& a, Settings& settings, std::string& err, int timeoutS);
    // after the game started: both PCs' started states compared (the same ROMs / files / build)
    bool ready(game::Runtime& rt, std::string& err, int timeoutS);

    std::string shownCode() const;               // the password, XXXX-XXXX
    std::string shareCode() const;               // the address (internet address when UPnP gave it) + the code
    Address lanAddress() const;
    const PortMapper& upnp() const { return upnp_; }
    // hole punching (docs/netplay.md "Hole punching"): the host's STUN state for its Internet line; a joiner's reply
    // code (its public + LAN address) once its direct try had no answer for a few seconds ("" none); the host pastes it:
    // its punches open the way (msg: what the waiting room says)
    const NatTraversal& nat() const { return nat_; }
    std::string replyCode() const;
    bool addReply(const std::string& text, std::string& msg);
    // the waiting rooms' line about it: "Getting your address...", "Waiting for your friend...", the strict-NAT
    // warning, "Couldn't connect: ..." ("" nothing to say)
    std::string natNote() const;

    enum class Step { Run, Wait, Stop };
    // before a game frame: `sample` gives the local player's input (called once per frame, for frame + delay); Run =
    // both players' inputs are in Runtime::gun (and the start buttons pressed): run the frame. Wait = not yet (pump
    // again; status says why after a moment). Stop = a desync (status says where)
    Step step(game::Runtime& rt, const std::function<Input()>& sample, int waitMs);
    void after(game::Runtime& rt);               // the frame ran: its state hash
    // rollback: every frame run so far is confirmed (both real inputs); a game over / the end are acted on only then
    bool settled() const;
    void settle(game::Runtime& rt, int timeoutMs);   // waits (pumping, correcting) until settled
    // the end: the final frame and state hash exchanged (up to lingerMs); prints / returns "NET: result=..."
    std::string finish(game::Runtime& rt, int lingerMs);

    // the pause online (docs/netplay.md "Pause"): this PC's pause menu open or not, every loop; while paused idle()
    // keeps the link alive (no frame runs; the other PCs wait). Every PC learns who paused (PING's paused byte; the host
    // of up to four PCs passes it on); pausedText: "P2 PAUSED" for the other PCs' pauses ("" none)
    void setPaused(bool on);
    void idle();
    uint8_t pausedSeats() const;                 // bit s: seat s's PC is paused (this one's too)
    std::string pausedText() const;
    int localPlayer() const { return session_.localPlayer(); }
    uint32_t frame() const { return frame_; }
    double silentSeconds() const { return session_.silentSeconds(); }
    Session& session() { return session_; }
    const Session& session() const { return session_; }
    std::string status;                          // for the window: "" = nothing to show
    // the host of up to four PCs: a joined PC that went silent in the waiting room (left, or its connection dropped) for
    // kWaitingLeftS: its seat is free again for a new joiner, the rosters say so; the newest such note (the menu shows
    // it and clears it)
    static constexpr double kWaitingLeftS = 5.0;
    std::string waitingLeft;
    std::string stats() const;                   // the network-stats overlay's lines (newlines between)
    // the in-game connection indicator (small, a corner): the ping (a host of up to four PCs: its worst seat's), a
    // level (0 good < 80 ms, 1 so-so < 150 ms, 2 poor) and a lag warning (many rollback corrections or waits in the
    // last seconds); alone: nobody connected (a host playing alone / waiting for a rejoin)
    struct Link { int pingMs = 0; int level = 0; bool lag = false; bool alone = false; };
    Link link();
    std::function<void(const std::string&)> log;
    // the host of up to four PCs: the seats no PC plays (computer teammates, docs/netplay.md "CPU teammates"), filled
    // in its own input of frame f (seats 1-3, setSeat); asked once per input frame. A seat's PC's input replaces it
    std::function<void(game::Runtime&, uint32_t, Input[2])> hostSeats;
    uint8_t freeSeats() const;                   // a game of up to four PCs: the seats (bits) no PC plays now

    // rollback's question (HOTD_ROLLBACK_PROBE, developer): is the memory image the whole state? Snapshot every mapped
    // region, run `span` frames, restore, run them again with the same input: the per-frame hashes must match. Prints
    // a line per probe; returns the probes that differed
    static int rollbackProbe(game::Runtime& rt, int span);
    static uint64_t stateHash(port::Mem& mem);   // the work RAM (the sound command queue left out)
    static uint64_t gameHash(game::Runtime& rt); // stateHash + the 4-seat game's runtime fields (the desync check)
    const Settings& gameSettings() const { return settings_; }   // the game's start settings (host and client)
    static uint64_t bootHash(game::Runtime& rt); // every mapped region + the logic's settings flags

    // joining a running game (docs/netplay.md): the host marks its game's restarts (a game over starts the chapter
    // again); the joiner replays the host's log (joinLog: every frame's two inputs, the restarts) from the start
    // settings, then joinedAt(frame) and ready() compare the two states there
    void markRestart() { restarts_.push_back(frame_); }
    uint32_t joinFrame() const { return session_.isHost() ? 0 : session_.joinFrame(); }
    bool joinLog(std::vector<std::array<Input, 2>>& frames, std::vector<uint32_t>& restarts, std::string& err) const;
    void joinedAt(uint32_t frame);
    void applyInputs(game::Runtime& rt, const Input in[kMaxPlayers]);   // Runtime::gun + the start buttons
    // a cut-scene in play (the shots off): only the host's triggers count (the skip); the others' are dropped
    static bool onlyHostSkips(const game::Runtime& rt);
    // PC Mode (protocol 17): this PC's own characters (--net-pc-char, the pages' picks) put into its seats' starts
    // (Input::kChar), so a seat that joins the running game plays its own pick from its first start on every PC
    Input picked(Input in) const;
    bool alone() const { return solo_ || waitRejoin_; }   // the host without player 2 (open for a join)
    // a player left mid-game (docs/netplay.md "A player left", 2026-10-30): the host's game waits (every PC shows "P<n>
    // left") until that PC joins again or the host goes on without it. dropAsk: the host's choice is open now (the
    // window: Enter / Start = dropContinue); askOnDrop: a window game (the main loop sets it; headless: NetArgs)
    bool askOnDrop = false;
    bool dropAsk() const { return session_.isHost() && dropPcs_ > 0; }
    void dropContinue();
    // this PC leaves mid-game (the window closed, Exit): BYE kLeaving, so the host keeps the seat for a rejoin; the
    // host leaving ends the game for the others (as before)
    void leave(int lingerMs);
    static bool decodeLog(const std::vector<uint8_t>& v, std::vector<std::array<Input, 2>>& frames, std::vector<uint32_t>& restarts);

private:
    Session session_;
    NetArgs args_;
    std::string code_;
    LobbyAnnouncer announcer_;
    PortMapper upnp_;
    // hole punching (natpunch.h): STUN on the session's socket, the punches; the tests' mock STUN servers
    NatTraversal nat_;
    StunMock stunMock_[2];
    bool punchJoin_ = false;                     // a joiner trying an internet host: punches, the reply code
    double openedAt_ = 0, replyPunchAt_ = -1;    // a host: the last reply code's punches started (-1 none)
    bool codeWritten_ = false, replyWritten_ = false;
    std::vector<std::string> repliesUsed_;       // a host: the reply codes punched to this hosting (a repeat: ignored)
    double replyReadAt_ = -1;                    // testing (--reply-from): the file's last look, and its content then
    std::string replyFileSeen_;
    void natPoll();
    uint8_t chapter_ = 0;
    uint32_t frame_ = 0;
    int64_t submitted_ = -1;
    bool reportedDesync_ = false;
    double waitStart_ = -1;
    uint64_t waits_ = 0;                         // frames that had to wait for the other PC
    bool upnpLogged_ = true;
    bool paused_ = false;
    void syncPause();                            // the paused seats into the PINGs
    void announce(bool inGame);
    void apply(game::Runtime& rt, const Input in[kMaxPlayers], uint32_t frame);
    Options opts_;
    Settings settings_;                          // the game's start settings (a joiner's)
    std::vector<std::array<Input, 2>> history_;  // every frame's inputs (the log)
    std::vector<uint32_t> restarts_;
    bool solo_ = false, waitRejoin_ = false, joining_ = false;
    uint8_t dropHold_ = 0;                       // the seats (bits) of the PCs that left, the game waiting for them
    int dropPcs_ = 0;                            // how many PCs the game waits for (0: none, it runs)
    double dropAt_ = 0;
    bool left_ = false, finished_ = false;      // leave() / finish() done
    int dropMode() const;                        // NetArgs::dropContinueS resolved (-1 wait, 0 on at once, N s)
    std::string dropText() const;                // the host's line while it waits
    bool charsFixed_ = false;                    // the game's settings are set (seatChar: the game's characters)
    mutable bool charPickOn_ = false;            // charsel: an arcade game with Character Select picks (the host's or a joiner's; hostStart)
    uint64_t joinHash_ = 0;
    std::vector<uint8_t> encodeLog(uint32_t frames) const;
    void renewSession();
    void beginRejoinWait(game::Runtime& rt);
    void beginJoin(game::Runtime& rt);
    // rollback (docs/netplay.md "Rollback"): up to rollback_ frames run on a prediction of the other player's input
    static constexpr uint32_t kRingFrames = 16;
    struct Frame {
        RuntimeState state;                      // before the frame
        Input used[kMaxPlayers];                 // the inputs it ran with
        bool predicted = false;                  // the remote one was a prediction
        uint64_t hash = 0;                       // the state after it (reported once confirmed)
        std::vector<uint32_t> cues;              // the sounds its last run sent (Runtime::cueLog)
    };
    std::vector<uint32_t> cuesOf(uint32_t from, uint32_t to) const;
    void playLate(game::Runtime& rt, const std::vector<uint32_t>& heard, const std::vector<uint32_t>& now);
    int rollback_ = 0;
    std::vector<Frame> ring_;
    uint32_t confirmed_ = 0;                     // frames before this ran with both real inputs (hashes reported)
    Input lastRemote_;
    uint64_t predicted_ = 0, rollbacks_ = 0, resimulated_ = 0;
    uint64_t lateCues_ = 0;                      // sounds a re-run found that its first run missed (played late)
    double linkAt_ = 0, lagUntil_ = 0;           // link(): the last second's counts
    uint64_t linkResim_ = 0, linkWaits_ = 0;
    void reconcile(game::Runtime& rt);
    Input lastRemoteAt(uint32_t f) const;

    // up to four PCs: the host's seat sessions (session_ is the socket's hub), the relay, the client's full frames
    bool multi_ = false, started_ = false;
    std::unique_ptr<Session> seat_[3];           // seats 1-3 (P2-P4)
    enum class Seat : uint8_t { Empty, Joining, Active };
    Seat seatState_[4] = {};
    uint32_t seatFrom_[4] = {};                  // the frame from which the seat's PC's inputs count (before: none)
    uint64_t seatJoinHash_[4] = {};
    struct Full { uint32_t frame = ~0u; Input in[2]; };
    static constexpr uint32_t kRelayRing = 512;
    std::vector<Full> relay_, own_;              // host: the relayed frames; its own inputs
    uint32_t relayNext_ = 0;
    Input lastFull_[2];
    int holdJoin_ = 0;                           // host: a seat's PC waits to join the running game
    double rosterAt_ = 0;
    int rosterMask_ = -1;                // a joiner: the seats in at the last roster (logged on a change)
    Session& seat(int s) { return *seat_[s - 1]; }
    const Session& seat(int s) const { return *seat_[s - 1]; }
    bool seatLive(int s) const { return seatState_[s] != Seat::Empty; }
    // a PC with two seats (protocol 15): pair_[k] = the second seat of the PC whose first is k (0: none); owner_[t] = the
    // first seat of the PC that holds t as its second (-1: none). Seat t's own session is then reserved (no joiner)
    int pair_[4] = {};
    int owner_[4] = {-1, -1, -1, -1};
    int pcAt(int s) const;                   // the first seat of the PC playing seat s now (0: the host), -1: free
    uint32_t seatFrom(int s) const { const int k = pcAt(s); return k > 0 ? seatFrom_[k] : 0; }
    void takePair(int s);                    // seat s's PC asked for two: its second seat, when one is free
    void dropPair(int s);
    void renewSeat(int s);
    void relay();
    void pumpAll(int waitMs);
    bool anyDesync(uint32_t& frame, uint32_t& mine, uint32_t& theirs) const;
    bool ownKnown(uint32_t f, Input& own);
    bool realFull(uint32_t f, Input out[2]) const;
    void predictFull(uint32_t f, Input out[2]);
    void frameHashAll(uint32_t f, uint64_t h);
    Step stepMulti(game::Runtime& rt, const std::function<Input()>& sample, int waitMs);
    void reconcileMulti(game::Runtime& rt);
    void beginJoinSeat(game::Runtime& rt, int s);
    void sendRosters();
};

} // namespace hotd::net
