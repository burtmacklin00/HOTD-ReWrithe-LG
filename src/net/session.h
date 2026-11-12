// Netplay session (docs/netplay.md): the handshake (CPace with the session code), the encrypted packet layer with replay
// protection, and the lockstep's frame-tagged input exchange and state hashes. No game knowledge: the game glue
// (src/net/net_game.*) turns its inputs into Runtime::gun and hashes the game state.
#pragma once

#include "net/crypto.h"
#include "net/socket.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hotd::net {

// 2: CPace; 3: the waiting room (WELCOME, PING); 4 (two branches): joining a running game / Sonic Mode; 5: both;
// 6: the game speed (the settings: ..., hashEvery, joinFrame, logBytes, the routes, sonic, gameSpeed); 7: four seats
// online (docs/coop4.md phase 7: the settings' seats byte, two seats' inputs per PC and frame); 8: up to four PCs (the
// settings' pcs and seat bytes, WELCOME's seat, ROSTER, the host's relayed four-seat frames); 9: player names (CONFIRM
// carries the joiner's, ROSTER every seat's), ROSTER to two-PC joiners too, REFUSE "locked"
// 10: one crypto on every system (cross-play: Monocypher): CPace on X25519 + Elligator 2, XChaCha20-Poly1305, BLAKE2b
// 11: the settings' autoReload byte (the host's Auto Reload for every seat)
// 12: the settings' gameText byte (the host's Game Text: English / Japanese)
// 13: PING's paused-seats byte (P<n> PAUSED on every PC); the LAN announcement's public address (the join code)
// 14: the settings' boss4p / enemy4p bytes (the host's 4P Boss Hit Points / 4P Enemy Toughness, percent)
// 15: up to four PCs with one or two seats each (CONFIRM's optional seats byte, the settings' seat byte's high nibble =
// the joiner's second seat, its inputs two seats wide, ROSTER's "second seat of" nibble)
// 16: PC Mode online (docs/pcmode.md "Online"): the settings' pcMode / pmChar[4] / pmContinues / pmFreePlay bytes,
// CONFIRM's fixed seats byte + the joiner's two PC Mode characters, ROSTER's character byte per seat
// 17: an input's flags bits 4-6: PC Mode, the seat's character (1-6) sent with its start (a joiner's own pick at its
// first start in a running game: Input::kChar)
// 18: Boss Mode online (docs/netplay.md "Boss Mode online"): the settings' bossMode byte (0, or 2-6: Chariot ..
// Magician, All Bosses) after pmFreePlay; ROSTER's last byte: the host's Boss Mode (the joiner's waiting room shows it)
// 19: the same game data (docs/netplay.md "Same game data"): CONFIRM carries the joiner's data digests (the arcade
// ROMs', the 1998 PC files' that PC Mode / Boss Mode read); the host refuses a mismatch (REFUSE 5-8: a clear message)
// 20: PC Mode's Extra Characters (docs/pcmode.md "Extra Characters"): characters 1-19 everywhere (settings, CONFIRM,
// ROSTER), an input's character in flags bits 3-7 (was bits 4-6, 1-6)
// 21: the Character Select of the arcade games (charsel, cosmetic): the settings' pmChar[4] in an arcade game (pcMode 0)
// are all 0 (no picks) or every seat's character 1-19; an input's character with its start there too
// 22: Survival Mode online (docs/netplay.md "Survival online", worker "survnet" 2026-11-09): the settings' Survival
// bytes after bossMode (arena + 1, seed, cap, cut creatures, the testing waves / flags); an input without its start
// carries the Survival extras in flags bits 3-7 (bit 3: the trigger held, bits 4-7: the weapon switch); ROSTER's last
// byte 0x10 + arena: the host's Survival game
constexpr uint16_t kProtocol = 22;
constexpr int kPmCharMax = 19;   // the last PC Mode character (port::Game::kPmCharCount - 1: the PC's 1-6, the extras 7-19)
constexpr uint32_t kMaxLog = 8u << 20;   // the input log a joining PC accepts at most (bytes)
constexpr uint32_t kMaxJoinFrames = 4'000'000;   // a running game joined at most this far in (~19 hours; bounds the replay's memory)
constexpr size_t kMaxName = 16;          // a player's name (printable ASCII)
constexpr size_t kMaxPacket = 1200;
// BYE's frame count of a PC that leaves mid-game (closed the window, Exit; 2026-10-30): not the end of the run, so the
// host keeps the game and its seat open for a rejoin (NetGame::leave)
constexpr uint32_t kByeLeaving = 0xFFFFFFFFu;
constexpr int kMaxPlayers = 2;   // PCs (the 4-seat game: each PC owns two seats, PC k = seats k and k + 2)

// one PC's input of one frame (the light gun: aim in arcade screen units, on screen, trigger, start). The 4-seat game
// (Settings::seats 4): flags2 / x2 / y2 = the PC's second seat (the host's player 3, the client's player 4)
struct Input {
    // kChar (protocol 17; 20: bits 3-7, was 4-6) = PC Mode, the seat's character (1-19) with its start; 0 none (an
    // arcade game, Auto)
    enum : uint8_t { kOnScreen = 1, kTrigger = 2, kStart = 4, kAll = 7, kChar = 0xF8, kCharShift = 3 };
    // protocol 22, Survival Mode: an input WITHOUT kStart carries the Survival extras in bits 3-7 instead of a
    // character: kHeld (the trigger held: the automatic weapons) and the weapon switch (kWeapon: 0 none, 1 previous,
    // 2 next, 3 + id that weapon (0-5), 9 the light gun's auto best weapon, 10 / 11 auto best + previous / next).
    // Only a Survival game sends or reads them (NetGame::applyInputs); every other game's inputs are as before
    enum : uint8_t { kHeld = 0x08, kWeapon = 0xF0, kWeaponShift = 4 };
    enum : int { kWeaponNone = 0, kWeaponPrev = 1, kWeaponNext = 2, kWeaponPick = 3, kWeaponAuto = 9, kWeaponCodes = 12 };
    // (with a start: characters 20-31 none; without: any Survival extras below kWeaponCodes)
    static bool flagsOk(uint8_t f)
    {
        return (f & kStart) ? (f >> kCharShift) <= kPmCharMax : (f >> kWeaponShift) < kWeaponCodes;
    }
    uint8_t flags = 0;
    float x = 0, y = 0;
    uint8_t flags2 = 0;
    float x2 = 0, y2 = 0;
    bool operator==(const Input& o) const
    {
        return flags == o.flags && x == o.x && y == o.y && flags2 == o.flags2 && x2 == o.x2 && y2 == o.y2;
    }
    // rollback's prediction from the last real input: the aims stay, no trigger / start repeated (Survival: the
    // trigger held stays held, no weapon switch repeated)
    Input predicted() const
    {
        Input p = *this;
        auto keep = [](uint8_t f) { return uint8_t(f & (kOnScreen | ((f & kStart) ? 0 : kHeld))); };
        p.flags = keep(flags);
        p.flags2 = keep(flags2);
        return p;
    }
};

// the host's game settings, sent to the client so both simulate the same game (all ranges checked on receipt)
struct Settings {
    uint8_t chapter = 0;        // 0-3
    uint8_t event = 0;
    uint8_t players = 1;        // 1-2 at the start
    uint8_t models = 0;         // 0 arcade, 1 PC
    uint8_t difficulty = 2;     // 0-4
    uint8_t lives = 0;          // 0-11 (the launcher's lives setting) or a (start, most) pair's code (game/lives.h, #30)
    uint8_t blood = 0;          // 0-5 (the launcher's blood setting)
    uint8_t sound = 0;          // 0-2 (LaunchConfig::sound)
    uint8_t livesKept = 0, cuts = 1, defaultRoute = 0, continueChapters = 0, chapterStart = 1;
    uint8_t delay = 3;          // the input delay in frames, 1-15
    uint16_t hashEvery = 1;     // 1-600
    uint32_t joinFrame = 0;     // > 0: a game already running: the joiner replays logBytes of the host's input log to this frame
    uint32_t logBytes = 0;
    struct Route { uint16_t key; uint8_t v; };
    std::vector<Route> routes;  // --route (at most 32; key < 1000, v < 3)
    uint8_t sonic = 0;          // Sonic Mode (game/sonic.h) 0-3: the host's preset, both PCs pace the ticks alike (after the routes)
    uint8_t gameSpeed = 0;      // rate: 0 arcade (57.524 Hz), 1 60 Hz; both PCs tick at the host's rate (after sonic)
    uint8_t seats = 2;          // 2, or 4: the 4P mode online (docs/coop4.md phase 7; each PC two seats) (protocol 7)
    uint8_t pcs = 2;            // 2, or 4: up to four PCs with one seat each (seats 4; the host relays) (protocol 8)
    uint8_t seat = 1;           // the receiving PC's seat (0-based: 1-3; the host fills it per joiner)
    uint8_t seat2 = 0;          // up to four PCs: the receiving PC's second seat (1-3), 0 none (protocol 15: seat's high nibble)
    uint8_t autoReload = 0;     // 0 / 1: the host's Auto Reload, for every seat (protocol 11)
    uint8_t gameText = 0;       // 0 English / 1 Japanese: the host's Game Text (protocol 12; its sprite banks differ in the RAM)
    uint8_t boss4p = 50;        // 0-200: the host's 4P Boss Hit Points (percent a player past two; protocol 14)
    uint8_t enemy4p = 50;       // 0-200: the host's 4P Enemy Toughness (percent a player past two; protocol 14)
    // PC Mode online (protocol 16; docs/pcmode.md "Online"): the host's choice; every seat's character (1-6 of
    // THOTD.EXE's table: the host's own, each joiner's own pick, the defaults Rogan / G / Rogan 2 / G 2 for the rest);
    // the host's Continues (0-9) and Free-Play. All zero in an arcade game
    uint8_t pcMode = 0;
    uint8_t pmChar[4] = {};
    uint8_t pmContinues = 0;
    uint8_t pmFreePlay = 0;
    // Boss Mode online (protocol 18; game/bossmode.h): 0 none, 2 Chariot, 3 Hangedman, 4 Hermit, 5 Magician, 6 All
    // Bosses (never with pcMode): every PC starts that boss's game, the clock in game ticks
    uint8_t bossMode = 0;
    // Survival Mode online (protocol 22; game/survival.h): 0 none, else the arena + 1 (1-16; never with pcMode /
    // bossMode); the director's seed, the cap of creatures at once (1-64), the cut creatures (0 / 1); testing: the SMG's
    // free round, every weapon's free round (0 none), survivalTest bit 0 the test buyer, bit 1 the weapon cycle
    uint8_t survival = 0;
    uint32_t survivalSeed = 0x484F5244;
    uint8_t survivalCap = 24, survivalCut = 1, survivalSmgWave = 0, survivalWeaponsWave = 0, survivalTest = 0;
};

// seat s of a frame's two PC inputs (the 4-seat layout: seats 0 / 1 primary, 2 / 3 the second fields)
inline Input seatOf(const Input in[2], int s)
{
    const Input& p = in[s & 1];
    Input r;
    if (s < 2) r.flags = p.flags, r.x = p.x, r.y = p.y;
    else r.flags = p.flags2, r.x = p.x2, r.y = p.y2;
    return r;
}
inline void setSeat(Input in[2], int s, const Input& v)
{
    Input& p = in[s & 1];
    if (s < 2) p.flags = v.flags, p.x = v.x, p.y = v.y;
    else p.flags2 = v.flags, p.x2 = v.x, p.y2 = v.y;
}

struct Options {
    bool host = false;
    Address addr;               // host: the bind address (ip 0 = any); client: the host
    std::string code;           // normalised by normaliseCode
    int simLoss = 0;            // --net-sim: percent of sent packets dropped
    int simDelayMs = 0;         //            each sent packet delayed
    int fuzz = 0;               // --net-fuzz N: N malformed packets to the peer once connected (testing)
    int relaySeat = 0;          // the host of up to four PCs: a seat's session (1-3, on the hub's socket); 0: not
    int wantSeats = 1;          // a joiner: the seats it asks for (2: its CONFIRM says so; a host of up to four PCs gives
                                // two when two are free; a two-PC host ignores it)
    bool hub = false;           // the host of up to four PCs: the socket's owner (accepts nobody itself, dispatches)
    std::string name;           // this player's name (the waiting rooms' rosters; sanitised, at most kMaxName)
    int pcChar[2] = {};         // a joiner: its PC Mode characters (first seat, second seat; 1-6, 0 = the host's default)
    int connectTimeoutS = 15;   // client: no answer from the host in this long = a clear error (0: wait forever)
    // protocol 19: the game data this PC's logic reads (docs/netplay.md "Same game data"). romData: the arcade ROMs'
    // digest (0 = unknown: not compared); pcData: the 1998 PC files' data PC Mode / Boss Mode read (CAMALL.RBH's camera
    // keys, THOTD.EXE's motion table; 0 = no PC files: the ROM's camera keys). The joiner sends both in its CONFIRM;
    // the host refuses other ROMs always, other PC data when its game reads it (needPcData: PC Mode / Boss Mode)
    uint64_t romData = 0, pcData = 0;
    bool needPcData = false;
    std::function<void(const std::string&)> log;
    // the join code's hole punching (natpunch.h): every datagram the socket's owner receives goes here first; true =
    // consumed (a STUN answer, a punch packet), not the session's
    std::function<bool(const Address&, const uint8_t*, size_t)> raw;
};

// upper case, letters / digits only; "" when not 4-32 characters
std::string normaliseCode(const std::string& code);
std::string randomCode();       // 40 random bits as 8 Crockford base32 characters (the join code carries them)
std::string sanitiseName(const std::string& name);   // printable ASCII, at most kMaxName, trimmed
// the plain-English hint for "the host cannot be reached" (the firewall, the port forward)
std::string reachHint(uint16_t port);

// with: up to four PCs, a PC's second seat: the seat of that PC's first (0-3; its name and ping are the PC's), -1 none
// pcChar: PC Mode, the seat's character (0-6); -1: an arcade game (protocol 16)
// boss: seat 0's (the host's) only: the game's Boss Mode (0 none, 2-6; protocol 18)
// survival: seat 0's only: the game's Survival arena + 1 (0 none; protocol 22)
struct RosterSeat { bool in = false; uint16_t pingMs = 0; std::string name; int with = -1; int pcChar = -1; int boss = 0; int survival = 0; };
// ROSTER's payload (four seats: in (+ (with + 1) << 4), ping, name, character (0xFF: none); then the host's Boss Mode
// byte (0x10 + arena: its Survival game)): fixed 81 bytes; the decoder checks every field
std::vector<uint8_t> encodeRoster(const RosterSeat r[4]);
bool decodeRoster(const uint8_t* p, size_t n, RosterSeat out[4]);

class Session {
public:
    Session();
    ~Session();

    bool start(const Options& o, std::string& err);   // opens the socket
    // the host of up to four PCs: a seat's session on the hub's socket (the hub hands it its packets)
    bool startShared(const Options& o, Session& hub, std::string& err);
    void setSibling(int seat, Session* s) { if (seat >= 1 && seat <= 3) siblings_[seat - 1] = s; }
    // the host of up to four PCs: a seat held as another PC's second seat (the hub gives it no new joiner)
    bool reserved = false;
    bool pending() const { return havePending_; }
    int peerWants() const { return peerWants_; }      // host: the seats the joiner's CONFIRM asked for (1 / 2)
    int peerChar(int i) const { return peerChar_[i & 1]; }   // host: the joiner's PC Mode characters (0: the default)
    // up to four PCs: the seats of this session's PC (a joiner's own, or a host seat session's peer): 2 = its inputs
    // carry its second seat (18 bytes a frame)
    void setPcSeats(int n) { pcSeats_ = n == 2 ? 2 : 1; }
    int secondSeat() const { return multi_ && !opt_.host && received_.seat2 ? received_.seat2 : -1; }   // a joiner's
    // the handshake, a step: 1 = connected (client: settings received), 0 = still going, -1 = failed (err). The host
    // passes its settings; the client gets them.
    int handshake(Settings& settings, std::string& err, int waitMs);
    // host: the waiting room is over: the settings go to the client (the input delay in them), the lockstep starts
    void release(const Settings& s);
    // joining a running game (docs/netplay.md): the host's log (sent after the settings, in chunks, acknowledged);
    // the client: the log once complete. startAt: the lockstep from frame base (host: its own inputs from there are
    // already in; the client's first delay frames are neutral on both sides)
    void setLog(std::vector<uint8_t> log) { log_ = std::move(log); logAcked_ = 0; }
    bool logComplete() const { return received_.logBytes == 0 || logHave_ == received_.logBytes; }
    const std::vector<uint8_t>& log() const { return log_; }
    void startAt(uint32_t base);
    // host without a peer (playing alone, open for a join): the remote input of rame is neutral
    void fillRemote(uint32_t frame);
    // a host's new session (the old peer lost): its own inputs base.. carried over
    void seedLocal(uint32_t base, const std::vector<Input>& in);
    bool released() const { return !settingsBlob_.empty(); }
    // client: in the host's waiting room (the code was right); host: player 2 is in
    bool peerConnected() const { return connected_; }
    double rttMs() const { return rttMs_; }           // smoothed round trip (PING / PONG every 250 ms)
    double rttLastMs() const { return rttLastMs_; }
    int pingSamples() const { return pingSamples_; }
    int suggestedDelay() const;                       // the input delay for the measured round trip
    Address local() const { return hub_ ? hub_->sock_.local() : sock_.local(); }
    bool isHost() const { return opt_.host; }
    int localPlayer() const { return multi_ ? (opt_.host ? 0 : seat_) : opt_.host ? 0 : 1; }
    bool multi() const { return multi_; }             // up to four PCs, one seat each (the host relays)
    int peerSeat() const { return opt_.relaySeat; }   // a host's seat session: its PC's seat
    // the roster (up to four PCs): each seat's PC in and its ping to the host (ms); the client: the host's ROSTER
    using RosterSeat = net::RosterSeat;
    RosterSeat roster[4];
    bool rosterReceived = false;
    const std::string& peerName() const { return peerName_; }   // host: the joiner's name (from its CONFIRM)
    const std::string& name() const { return opt_.name; }
    int wrongCodes() const { return hub_ ? hub_->failures_ : failures_; }
    void setWrongCodes(int n) { (hub_ ? hub_->failures_ : failures_) = n; }   // (a host's new session keeps the count)
    void sendRoster(const RosterSeat r[4]);           // host: to this seat's PC
    // the relay (multi): the host's full frames (the four seats) to its PC; the client: the host's, when in
    void submitFull(uint32_t frame, const Input full[2]);
    bool remoteFull(uint32_t frame, Input out[2]) const;
    void markRun(uint32_t frame) { lastRun_ = std::max(lastRun_, frame); }   // the host's relay is at frame (the window)
    void seedFull(uint32_t base, const std::vector<std::array<Input, 2>>& frames);
    Address peer() const { return peer_; }
    // hole punching: a datagram outside the protocol from this session's socket (STUN, a punch packet)
    void sendPlain(const Address& to, const uint8_t* p, size_t n) { sock().send(to, p, n); }
    // the client, before the host answered: the host's other address (a punch reached it there); false: too late
    bool retarget(const Address& a);
    bool hostAnswered() const { return gotAck_ || connected_; }
    // the pause (bits = seats 0-3): what this PC's PINGs say is paused, and what the peer's last PING said
    void setPausedOut(uint8_t seats);
    uint8_t peerPaused() const { return peerPaused_; }

    // after the game started: the started state's hash, exchanged (READY); 1 = the peer's arrived, 0 = not yet
    void setReady(uint64_t bootHash);
    int peerReady(uint64_t& peerHash);

    // the lockstep. Frames 0 .. delay-1 are neutral for both players.
    void submit(uint32_t frame, const Input& in);     // the local player's input for `frame` (frame = run frame + delay)
    bool inputs(uint32_t frame, Input out[kMaxPlayers]);   // both players' inputs of `frame`, when in
    bool localInput(uint32_t frame, Input& in);       // (rollback) one side's input of `frame`, when known
    bool remoteInput(uint32_t frame, Input& in) const;
    int64_t remoteContig() const { return remoteContig_; }
    void frameHash(uint32_t frame, uint64_t h);       // the state after `frame`
    void pump(int waitMs);                            // receive, (re)send; waits up to waitMs for a packet
    void bye(uint32_t frames, uint64_t hash);         // the end: sent a few times by pump
    bool peerBye(uint32_t& frames, uint64_t& hash) const;

    double silentSeconds() const;                     // since the last authentic packet from the peer
    // host: the peer's PC asked to join again (a new HELLO from its address while it is silent): its seat is handed
    // back at once instead of after the 6 s silence ("the game is full" before)
    bool rejoinAsked() const { return rejoinAsked_; }
    bool desync = false;
    uint32_t desyncFrame = 0, desyncLocal = 0, desyncPeer = 0;
    uint32_t hashesCompared = 0;
    uint64_t dropped[6] = {};   // malformed, unauthenticated, replayed, stranger, out of window, refused
    uint64_t sent = 0, received = 0;
    std::string counters() const;
    uint8_t delay() const { return delay_; }
    void setDelay(uint8_t d) { delay_ = d; }
    uint32_t joinFrame() const { return received_.joinFrame; }
    int seatsPerPc() const { return seats_; }          // 1, or 2 in the 4-seat game (the input packets' stride)
    void setSeats(int seats) { seats_ = seats >= 4 && !multi_ ? 2 : 1; }

private:
    using Clock = std::chrono::steady_clock;
    Options opt_;
    UdpSocket sock_;
    Address peer_;
    bool connected_ = false;
    uint8_t delay_ = 3;
    uint16_t hashEvery_ = 1;
    int seats_ = 1;                      // seats per PC (2: the 4-seat game)
    bool multi_ = false;                 // up to four PCs: the host's packets carry full frames (four seats)
    int seat_ = 1;                       // the client's seat (multi)
    Session* hub_ = nullptr;             // a seat session: the hub owning the socket
    Session* siblings_[3] = {};          // the hub: the seats' sessions
    UdpSocket& sock() { return hub_ ? hub_->sock_ : sock_; }
    bool owns(const Address& from) const { return (connected_ && from == peer_) || (havePending_ && from == pending_); }
    int pcSeats_ = 1;                    // multi: this PC's (a joiner) / the peer PC's (a host's seat) seats
    int peerWants_ = 1;
    int peerChar_[2] = {};
    size_t sendStride() const { return multi_ ? (opt_.host ? 36 : size_t(9 * pcSeats_)) : size_t(9 * seats_); }
    size_t recvStride() const { return multi_ ? (opt_.host ? size_t(9 * pcSeats_) : 36) : size_t(9 * seats_); }
    size_t maxInputs() const { return sendStride() > 18 ? 24 : sendStride() > 9 ? 40 : 64; }   // inputs per packet (1200 bytes)

    // handshake
    std::unique_ptr<crypto::Cpace> ecdh_;   // CPace (the host: a fresh one per new HELLO)
    uint8_t myNonce_[16] = {}, peerNonce_[16] = {};
    uint8_t ackPacket_[160] = {};   // the host's HELLO_ACK for the pending client (resent on a repeated HELLO)
    size_t ackLen_ = 0;
    Address pending_;
    bool havePending_ = false, gotAck_ = false;
    int failures_ = 0;
    std::vector<uint8_t> settingsBlob_;
    Clock::time_point lastHello_{}, lastSend_{}, lastRecv_{}, lastRefuse_{}, lastBye_{}, lastReady_{}, lastPing_{}, lastSettings_{};
    std::string refused_;
    std::string peerName_;
    Clock::time_point started_{}, helloWindow_{};
    int hellos_ = 0;                     // new handshakes in the current second (the socket's owner counts)
    bool helloAllowed();
    int& failures() { return hub_ ? hub_->failures_ : failures_; }
    void refuse(const Address& to, uint8_t why);
    Address dataRefused_;                // protocol 19: the last joiner refused for its game data (its repeats: refused again)
    uint8_t dataRefusedWhy_ = 0;
    bool rejoinAsked_ = false;
    bool rejoinHello(const Address& from, const uint8_t* hello);   // marks rejoinAsked_ (see the .cpp)
    Settings received_;
    double rttMs_ = 0, rttLastMs_ = 0;
    int pingSamples_ = 0;
    uint8_t pausedOut_ = 0, peerPaused_ = 0;
    void initLockstep();
    void periodic();
    uint32_t base_ = 0;                  // the lockstep's first frame (0, or the join frame)
    std::vector<uint8_t> log_;           // host: the log to send; client: the log received
    uint32_t logAcked_ = 0, logHave_ = 0;
    std::vector<uint8_t> logChunks_;     // client: chunks received (1 = in)
    Clock::time_point lastLog_{};
    void sendLog();

    // packet keys and sequence numbers
    std::unique_ptr<crypto::Aead> tx_, rx_, pendingRx_, pendingTx_;
    uint32_t txDir_ = 0, rxDir_ = 0;
    uint64_t txSeq_ = 0, rxMax_ = 0, rxWindow_ = 0;

    // READY / BYE
    bool readySet_ = false, peerReady_ = false;
    uint64_t bootHash_ = 0, peerBootHash_ = 0;
    bool byeSet_ = false, peerBye_ = false;
    uint32_t byeFrames_ = 0, peerByeFrames_ = 0;
    uint64_t byeHash_ = 0, peerByeHash_ = 0;
    int byeSends_ = 0;

    // inputs: rings by frame
    static constexpr uint32_t kRing = 512;
    struct Slot { uint32_t frame = ~0u; Input in; Input in2; };   // in2: a relayed full frame's second PC
    Slot local_[kRing], remote_[kRing];
    int64_t localLast_ = -1, localAcked_ = -1, remoteContig_ = -1;
    struct HashSlot { uint32_t frame = ~0u; uint32_t h = 0; };
    HashSlot myHash_[kRing], peerHash_[kRing];
    std::deque<HashSlot> recentHashes_;   // the last 16 to send
    uint32_t lastRun_ = 0;               // the newest frame asked for (bounds the remote's window)

    // the simulated network (--net-sim)
    struct Delayed { Clock::time_point due; Address to; std::vector<uint8_t> bytes; };
    std::deque<Delayed> delayed_;
    uint64_t simRng_ = 0x9E3779B97F4A7C15ull;
    std::vector<std::vector<uint8_t>> fuzzSamples_;   // copies of sent packets (the fuzz replays / flips them)
    bool fuzzed_ = false;

    void log(const std::string& s) const { if (opt_.log) opt_.log(s); }
    void sendRaw(const Address& to, const uint8_t* p, size_t n);
    void flushDelayed();
    bool sendSealed(uint8_t type, const uint8_t* payload, size_t n);
    // an authentic packet's payload (decrypted into out); false: dropped (counted)
    bool openSealed(crypto::Aead& key, uint32_t dir, const uint8_t* p, size_t n, uint8_t& type, std::vector<uint8_t>& out, bool replayCheck);
    void receiveAll(int waitMs);
    void handle(const Address& from, const uint8_t* p, size_t n, Settings* settings);
    void handlePayload(uint8_t type, const std::vector<uint8_t>& pl, Settings* settings);
    bool deriveKeys(const uint8_t peerPub[crypto::kPubSize], const uint8_t clientNonce[16], const uint8_t clientPub[crypto::kPubSize],
                    const uint8_t hostNonce[16], const uint8_t hostPub[crypto::kPubSize]);
    void sendInputs();
    void fuzz();
    Settings* hsSettings_ = nullptr;
    bool gotSettings_ = false;
};

// settings on the wire (little-endian, fixed layout); decode checks every range
std::vector<uint8_t> encodeSettings(const Settings& s);
bool decodeSettings(const uint8_t* p, size_t n, Settings& s);
// a running game's input log (NetGame::encodeLog's format, docs/netplay.md "Joining a running game"): every count,
// flag and float checked; at most kMaxJoinFrames frames
bool decodeInputLog(const std::vector<uint8_t>& v, std::vector<std::array<Input, 2>>& frames, std::vector<uint32_t>& restarts);

} // namespace hotd::net
