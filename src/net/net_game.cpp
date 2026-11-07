#include "net/net_game.h"

#include "game/audio.h"
#include "game/runtime.h"
#include "util/utf8.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <thread>
#if defined(_MSC_VER) && defined(_M_X64)
#include <math.h>   // _set_FMA3_enable
#endif

namespace hotd::net {

namespace {
double nowS()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// a fast 64-bit hash of words (four lanes), enough to tell two states apart
struct Hasher {
    uint64_t lane[4] = {0x9E3779B97F4A7C15ull, 0xC2B2AE3D27D4EB4Full, 0x165667B19E3779F9ull, 0x27D4EB2F165667C5ull};
    void words(const uint8_t* p, size_t n)   // n: a multiple of 32
    {
        for (size_t i = 0; i + 32 <= n; i += 32)
            for (int k = 0; k < 4; ++k) {
                uint64_t w;
                std::memcpy(&w, p + i + size_t(k) * 8, 8);
                lane[k] = (lane[k] ^ w) * 0x100000001B3ull;
                lane[k] ^= lane[k] >> 29;
            }
    }
    void value(uint64_t v)
    {
        uint8_t b[32] = {};
        std::memcpy(b, &v, 8);
        words(b, 32);
    }
    void bytes(const void* p, size_t n)   // any length (the tail zero padded to 32)
    {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        words(b, n & ~size_t(31));
        if (n & 31) {
            uint8_t t[32] = {};
            std::memcpy(t, b + (n & ~size_t(31)), n & 31);
            words(t, 32);
        }
    }
    uint64_t done() const
    {
        uint64_t h = 0;
        for (int k = 0; k < 4; ++k) {
            h ^= lane[k] + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
            h *= 0xFF51AFD7ED558CCDull;
            h ^= h >> 33;
        }
        return h;
    }
};

constexpr uint32_t kRam = 0x500000, kRamEnd = 0x600000;
constexpr uint32_t kSoundQueue = 0x512340, kSoundQueueEnd = 0x5123A0;   // flow.cpp: differs with the sound source

void hashRam(port::Mem& mem, Hasher& h)
{
    for (uint32_t a = kRam; a < kRamEnd; a += 0x1000) {
        if (!mem.mapped(a))
            continue;
        const uint8_t* p = mem.ptr(a);
        if (a == (kSoundQueue & ~0xFFFu)) {
            uint8_t page[0x1000];
            std::memcpy(page, p, sizeof page);
            std::memset(page + (kSoundQueue & 0xFFF), 0, kSoundQueueEnd - kSoundQueue);
            h.words(page, sizeof page);
        } else {
            h.words(p, 0x1000);
        }
    }
}

// PC Mode's runtime fields that live between frames (the ones rollback's RuntimeState::Pm keeps: docs/pcmode.md), so
// an online PC Mode game's desync check sees them too
void hashPm(const game::Runtime& rt, Hasher& h)
{
    h.value(uint64_t(uint32_t(rt.pcmode)) | uint64_t(uint32_t(rt.pm_msp)) << 32);
    for (int k = 0; k < 4; ++k)
        h.value(uint64_t(uint32_t(rt.pm_char[k])) | uint64_t(rt.pm_reload_at[k]) << 32);
    h.value(uint64_t(uint32_t(rt.pm_continues)) | uint64_t(uint32_t(rt.pm_credits)) << 32);
    h.value(uint64_t(uint32_t(rt.pm_credits_seen)) | uint64_t(rt.pm_clock_ms) << 32);
    h.value(uint64_t(rt.pm_molb_height_bits) | uint64_t(rt.pm_crand_seed) << 32);
    h.value(uint64_t(rt.pm_chariot_landed) | uint64_t(rt.pm_free_play) << 1 | uint64_t(rt.pm_auto_reload) << 2 |
            uint64_t(uint32_t(rt.pm_blood_tasks)) << 8);
    h.value(uint64_t(uint32_t(rt.pm_last_killer)));
    h.bytes(rt.pm_mstack, sizeof rt.pm_mstack);
    h.bytes(rt.pm_camera, sizeof rt.pm_camera);
    h.bytes(rt.pm_camera_inv, sizeof rt.pm_camera_inv);
    h.bytes(rt.pm_camera0_inv, sizeof rt.pm_camera0_inv);
    h.bytes(rt.pm_camera0, sizeof rt.pm_camera0);
}

// Boss Mode's fields that live between frames (the ones rollback's RuntimeState keeps; protocol 18): the mode, the boss,
// the phase, the clock (its ms bits and step), the result, the players; the logic's boss_mode / pc_boss_next / boss
// calls / no continue / section-0 start. A Boss Mode game runs the PC's maths (pc_path): PC Mode's fields too
void hashBoss(const game::Runtime& rt, Hasher& h)
{
    const game::BossMode& b = rt.boss;
    uint64_t t = 0, step = 0;
    std::memcpy(&t, &b.timeMs, sizeof t);
    std::memcpy(&step, &b.tickMs, sizeof step);
    h.value(uint64_t(uint32_t(b.mode)) | uint64_t(uint32_t(b.index)) << 8 | uint64_t(uint32_t(b.phase)) << 16 |
            uint64_t(uint32_t(b.result)) << 24 | uint64_t(b.clockOn) << 32 | uint64_t(b.shown) << 33 |
            uint64_t(b.justFinished) << 34 | uint64_t(b.nextStarted) << 35 | uint64_t(uint32_t(b.players)) << 40);
    h.value(t);
    h.value(step);
    h.value(uint64_t(uint32_t(b.dyingFrames)) | uint64_t(rt.boss_events) << 32);
    h.value(uint64_t(uint32_t(rt.boss_mode)) | uint64_t(rt.boss_next) << 32 | uint64_t(rt.bossNoContinue) << 33 |
            uint64_t(rt.startAtSection0) << 34);
    hashPm(rt, h);
}
bool bossGame(const game::Runtime& rt) { return rt.boss.active() || rt.boss_mode != 0; }
} // namespace

bool NetArgs::parse(const std::string& a, const std::function<std::string()>& next, std::string& err)
{
    auto num = [&](int lo, int hi, int& out) {
        const std::string v = next();
        char* e = nullptr;
        const long x = std::strtol(v.c_str(), &e, 10);
        if (v.empty() || *e || x < lo || x > hi)
            err = a + ": expected a number " + std::to_string(lo) + "-" + std::to_string(hi);
        else
            out = int(x);
    };
    if (a == "--host") host = next();
    else if (a == "--join") join = next();
    else if (a == "--code") code = next();
    else if (a == "--net-delay") {
        const std::string v = next();
        if (v == "auto") delay = 0;
        else {
            char* e = nullptr;
            const long x = std::strtol(v.c_str(), &e, 10);
            if (v.empty() || *e || x < 1 || x > 15) err = "--net-delay: auto or 1-15";
            else delay = int(x);
        }
    }
    else if (a == "--lan-name") lanName = util::utf8Head(next(), 32);
    else if (a == "--net-name") playerName = sanitiseName(next());
    else if (a == "--lobby") lobby = next();
    else if (a == "--no-lan") noLan = true;
    else if (a == "--lan-code") lanCode = true;
    else if (a == "--net-drop") {   // a PC left mid-game: wait (for it, until the host goes on), continue (at once), N s then go on
        const std::string v = next();
        char* e = nullptr;
        const long x = std::strtol(v.c_str(), &e, 10);
        if (v == "wait") dropContinueS = -1;
        else if (v == "continue") dropContinueS = 0;
        else if (!v.empty() && !*e && x >= 1 && x <= 3600) dropContinueS = int(x);
        else err = "--net-drop: wait, continue or 1-3600 (seconds, then on without them)";
    }
    else if (a == "--net-bind") bind = next();
    else if (a == "--net-alone") alone = true;
    else if (a == "--net-solo-fps") num(1, 100000, soloFps);
    else if (a == "--net-rollback") num(0, int(NetGame::kMaxRollback), rollback);
    else if (a == "--net-port") num(1, 65535, menuPort);
    else if (a == "--net-pcs") {
        num(2, 4, pcs);
        if (pcs == 3) pcs = 4;
    }
    else if (a == "--net-wait") num(0, 3, waitFor);
    else if (a == "--net-seats") num(1, 2, seats);
    else if (a == "--net-pc-char") {   // a[,b]: this PC's PC Mode characters (1-19: 7-19 the extras; 0 = the default)
        const std::string v = next();
        int c0 = 0, c1 = 0;
        const int got = std::sscanf(v.c_str(), "%d,%d", &c0, &c1);
        if (got < 1 || c0 < 0 || c0 > kPmCharMax || c1 < 0 || c1 > kPmCharMax)
            err = "--net-pc-char: expected <1-19>[,<1-19>] (0: the default)";
        else
            pcChar[0] = c0, pcChar[1] = got == 2 ? c1 : 0;
    }
    else if (a == "--upnp") {
        upnp = next();
        if (upnp != "on" && upnp != "off" && upnp != "dry-run") err = "--upnp: on, off or dry-run";
    }
    else if (a == "--net-hash-every") num(1, 600, hashEvery);
    else if (a == "--net-timeout") num(1, 3600, timeout);
    else if (a == "--net-fuzz") num(0, 1000000, fuzz);
    else if (a == "--net-sim") {   // loss %, delay ms
        const std::string v = next();
        if (std::sscanf(v.c_str(), "%d,%d", &simLoss, &simDelay) != 2 || simLoss < 0 || simLoss > 90 || simDelay < 0 || simDelay > 2000)
            err = "--net-sim: expected <loss percent 0-90>,<delay ms 0-2000>";
    }
    else return false;
    return true;
}

uint64_t NetGame::stateHash(port::Mem& mem)
{
    Hasher h;
    hashRam(mem, h);
    return h.done();
}

uint64_t NetGame::gameHash(game::Runtime& rt)
{
    // the work RAM (the 4P block 0x50C000..0x50FFFF in it) and, in the 4-seat game, the runtime's per-seat fields that
    // live between frames (the start requests, the input counters); in PC Mode its runtime fields (hashPm). An arcade
    // game of two seats: the work RAM only, as before. Boss Mode: its fields (hashBoss)
    if (!rt.coop4_on() && !rt.pcmode && !bossGame(rt))
        return stateHash(rt.mem);
    Hasher h;
    hashRam(rt.mem, h);
    if (rt.coop4_on())
        for (int p = 0; p < 4; ++p) {
            h.value(uint64_t(rt.startRequest[p]) | uint64_t(rt.shotsIn[p]) << 8 | uint64_t(rt.reloadsIn[p]) << 32);
            h.value(rt.startsIn[p]);
        }
    if (rt.pcmode || rt.charpick)   // (charsel: an arcade game's Character Select picks too)
        hashPm(rt, h);
    else if (bossGame(rt))
        hashBoss(rt, h);
    return h.done();
}

int NetGame::rollbackProbe(game::Runtime& rt, int span)
{
    // a span of frames run twice from one RuntimeState (rollback's snapshot): the work RAM after each frame and the
    // written regions at the end must come out the same
    auto written = [&rt]() {
        Hasher h;
        for (const port::Mem::Region& r : rt.mem.regions())
            if (r.base >= 0x500000 && r.base < 0x2000000)
                h.words(r.bytes.get(), r.size & ~31u);
        return h.done();
    };
    RuntimeState snap;
    snap.save(rt);
    const uint64_t frame0 = rt.frameCount;
    std::vector<uint64_t> a, b;
    uint64_t endA = 0, endB = 0;
    auto log = rt.log;
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            snap.restore(rt);
            rt.log = [](const std::string&) {};
        }
        for (int i = 0; i < span; ++i) {
            rt.gun[0] = rt.gun[1] = {};
            if (i % 5 == 0) { rt.gun[0].trigger = rt.gun[0].onScreen = true; rt.gun[0].x = float(i * 7 % 200 - 100); }
            rt.frame();
            (pass ? b : a).push_back(stateHash(rt.mem));
        }
        (pass ? endB : endA) = written();
    }
    rt.log = log;
    int first = -1;
    for (int i = 0; i < span; ++i)
        if (a[size_t(i)] != b[size_t(i)]) { first = i; break; }
    if (first < 0 && endA != endB)
        first = span;
    std::printf("ROLLBACK probe at frame %llu: %s\n", (unsigned long long)frame0,
                first < 0 ? "the same after the restore" : ("differs from frame +" + std::to_string(first)).c_str());
    return first < 0 ? 0 : 1;
}
uint64_t NetGame::bootHash(game::Runtime& rt)
{
    Hasher h;
    hashRam(rt.mem, h);
    for (const port::Mem::Region& r : rt.mem.regions())
        if (r.base + r.size <= kRam || r.base >= kRamEnd) {
            h.value(r.base);
            h.words(r.bytes.get(), r.size & ~31u);
        }
    h.value(uint64_t(rt.pcModels) | uint64_t(rt.livesKept) << 1 | uint64_t(rt.cuts.guards) << 2 |
            uint64_t(rt.cuts.defaultRoute) << 3 | uint64_t(rt.continueUnplayable) << 4);
    if (rt.pcmode)   // PC Mode online: the characters, Continues, Free-Play (an arcade game: as before)
        hashPm(rt, h);
    else if (bossGame(rt))   // Boss Mode online (protocol 18): the boss, the clock's step ..
        hashBoss(rt, h);
    const uint64_t v = h.done();
    if (std::getenv("HOTD_BOOT_HASH_PARTS")) {   // developer: which part of a start state differs (a join's "came out different")
        Hasher a;
        hashRam(rt.mem, a);
        std::printf("boothash parts: ram %016llx", (unsigned long long)a.done());
        for (const port::Mem::Region& r : rt.mem.regions())
            if (r.base + r.size <= kRam || r.base >= kRamEnd) {
                Hasher b;
                b.words(r.bytes.get(), r.size & ~31u);
                std::printf(" %X:%016llx", r.base, (unsigned long long)b.done());
            }
        Hasher c, d;
        hashBoss(rt, c);
        hashPm(rt, d);
        std::printf(" mode %d frameCount %llu chapter %d event %d gm %d", rt.boss.mode, (unsigned long long)rt.frameCount,
                    int(rt.mem.at<int8_t>(game::A::G::chapter)), int(rt.mem.at<int8_t>(game::A::G::event_index)),
                    int(rt.mem.at<int16_t>(game::A::G::game_mode)));
        std::printf(" flags %d%d%d%d%d boss %016llx pm %016llx timeMs %.3f tickMs %.6f idx %d phase %d players %d next %d ev %u\n",
                    int(rt.pcModels), int(rt.livesKept), int(rt.cuts.guards), int(rt.cuts.defaultRoute), int(rt.continueUnplayable),
                    (unsigned long long)c.done(), (unsigned long long)d.done(), rt.boss.timeMs, rt.boss.tickMs, rt.boss.index,
                    int(rt.boss.phase), rt.boss.players, int(rt.boss_next), rt.boss_events);
    }
    return v ? v : 1;
}

std::string NetGame::shownCode() const
{
    return code_.size() == 8 ? code_.substr(0, 4) + "-" + code_.substr(4) : code_;
}

Address NetGame::lanAddress() const
{
    Address a = session_.local();
    if (a.ip == 0)
        a.ip = localLanAddress();
    return a;
}

std::string NetGame::shareCode() const
{
    Address a = lanAddress();
    if (upnp_.externalIp())
        a.ip = upnp_.externalIp();
    return makeShareCode(a, code_);
}

bool NetGame::open(const NetArgs& a, std::string& err)
{
#if defined(_MSC_VER) && defined(_M_X64)
    // the UCRT's math functions take FMA3 paths on CPUs that have it (other bits than without): off, so both PCs
    // compute the same whatever their CPUs (docs/netplay.md "Float determinism")
    _set_FMA3_enable(0);
#endif
    args_ = a;
    rollback_ = std::clamp(a.rollback, 0, kMaxRollback);
    ring_.clear();
    ring_.resize(rollback_ > 0 ? kRingFrames : 0);
    Options o;
    o.host = !a.host.empty();
    o.log = log;
    o.simLoss = a.simLoss;
    o.simDelayMs = a.simDelay;
    o.fuzz = a.fuzz;
    o.name = a.playerName;
    o.wantSeats = a.seats;   // (a joiner: asks a host of up to four PCs for two seats)
    o.pcChar[0] = a.pcChar[0], o.pcChar[1] = a.pcChar[1];   // (a joiner: its PC Mode characters, in its CONFIRM)
    o.connectTimeoutS = a.timeout;   // the client: "No answer from the host" (with the firewall / port hint) after this
    // the same game data (protocol 19): a joiner sends its digests; the host compares them with its own (the PC files'
    // only in a game that reads them: PC Mode, Boss Mode)
    o.romData = a.romData, o.pcData = a.pcData;
    o.needPcData = o.host && (a.pcMode || a.bossMode != 0);
    std::string joinCode = a.code;
    if (!o.host && parseShareCode(a.join, o.addr, joinCode)) {
        // a share code: the address and the code in one
    } else if (!parseAddress(o.host ? a.host : a.join, o.addr, o.host, err)) {
        if (!o.host && err.find("resolve") == std::string::npos)
            err = "That is not a code or an address. Type or paste the host's code (like ABCD-EFGH-JKMN-PQRS), or its address "
                  "and port like 192.168.1.20:47800.";
        else if (!o.host)
            err = "Cannot find the host \"" + a.join.substr(0, a.join.rfind(':')) + "\": check the address (or use the code).";
        return false;
    }
    if (!o.host && o.addr.ip == 0) {
        err = "The host's address is missing: type or paste the host's code, or address:port.";
        return false;
    }
    if (joinCode.empty()) {
        if (!o.host) { err = "Type the host's password (or use the host's code: it holds the password)."; return false; }
        o.code = randomCode();
    } else {
        o.code = normaliseCode(joinCode);
        if (o.code.empty()) { err = "The password must be 4-32 letters or digits (dashes and spaces are ignored)."; return false; }
        if (o.code.size() < 8 && log)
            log("warning: a password shorter than 8 characters is easier to guess (fine on a LAN)");
    }
    code_ = o.code;
    multi_ = o.host && a.pcs == 4;
    o.hub = multi_;
    opts_ = o;
    if (log) {   // (the tests: which data each PC brings)
        char d[96];
        std::snprintf(d, sizeof d, "game data: ROMs %016llx, PC files %016llx%s", (unsigned long long)o.romData,
                      (unsigned long long)o.pcData, o.pcData ? "" : " (none)");
        log(d);
    }
    if (!session_.start(o, err))
        return false;
    if (multi_) {   // up to four PCs: a session per seat (P2-P4) on the hub's socket
        relay_.assign(kRelayRing, {});
        own_.assign(kRelayRing, {});
        for (int s = 1; s <= 3; ++s)
            renewSeat(s);
        if (a.seats == 2) {   // the host plays two seats: P1 and P3 (the 4P mode's left side); joiners get P2, P4
            pair_[0] = 2, owner_[2] = 0;
            seat(2).reserved = true;
        }
    }
    if (o.host) {
        // the LAN lobby: announced (no secrets) unless --no-lan; tests send it to --lobby (the loopback)
        if (!a.noLan) {
            Address to{0xFFFFFFFFu, kLobbyPort};
            std::string e;
            if (!a.lobby.empty() && !parseAddress(a.lobby, to, false, e)) { err = "--lobby: " + e; return false; }
            if (!announcer_.start(to, e) && log)
                log("the LAN list will not show this game: " + e);
        }
        // UPnP: on for a real host (all interfaces); a loopback test never asks the router
        const PortMapper::Mode m = a.upnp == "dry-run"              ? PortMapper::Mode::DryRun
                                   : o.addr.ip != 0 || a.upnp == "off" ? PortMapper::Mode::Off
                                                                       : PortMapper::Mode::On;
        upnp_.start(session_.local().port, m);
        if (log)
            log("hosting on UDP " + lanAddress().str() + "; password " + shownCode() + "; join code " + shareCode());
        upnpLogged_ = false;
    } else if (log) {
        log("joining " + o.addr.str() + " ...");
    }
    return true;
}

int NetGame::poll(Settings& settings, std::string& err, int waitMs)
{
    const int r = session_.handshake(settings, err, waitMs);
    announce(false);
    if (!upnpLogged_ && session_.isHost() && upnp_.state() != PortMapper::State::Working && log) {
        upnpLogged_ = true;
        log(upnp_.message());
    }
    if (r < 0)
        return -1;
    if (multi_) {   // the seats' handshakes (the hub received their packets); the roster to each PC
        for (int s = 1; s <= 3; ++s) {
            Settings unused;
            std::string e;
            if (owner_[s] >= 0)   // (another PC's second seat)
                continue;
            if (seat(s).handshake(unused, e, 0) < 0) {
                if (log) log("player " + std::to_string(s + 1) + ": " + e);
                renewSeat(s);
            } else if (!started_ && seat(s).peerConnected() && (seat(s).silentSeconds() > kWaitingLeftS || seat(s).rejoinAsked())) {
                // a joined PC left the waiting room (Cancel, closed, its connection dropped): the seat is free again
                // (a new session: the next joiner gets it; the others' rosters show it free within half a second)
                const std::string who = seat(s).peerName();
                waitingLeft = (who.empty() ? "Player " + std::to_string(s + 1) : who) + " left. Seat " +
                              std::to_string(s + 1) + " is free again.";
                if (log) log("player " + std::to_string(s + 1) + " left the waiting room: the seat is free again");
                renewSeat(s);
                rosterAt_ = -1.0;   // (the rosters at once)
            } else if (!started_ && seat(s).peerConnected() && !pair_[s] && seat(s).peerWants() == 2) {
                takePair(s);        // (a PC asking for two seats: its second, when one is free)
                rosterAt_ = -1.0;
            }
        }
        sendRosters();
        return started_ ? 1 : 0;
    }
    if (session_.isHost()) {
        sendRosters();   // (the joiner's waiting room: the names and pings too)
        return session_.released() || solo_ ? 1 : 0;
    }
    if (session_.rosterReceived && log) {   // a joiner: the waiting room's seats changed (one joined / left): logged
        int mask = 0;
        for (int s = 0; s < 4; ++s) mask |= session_.roster[s].in ? 1 << s : 0;
        if (mask != rosterMask_) {
            rosterMask_ = mask;
            std::string t = "the waiting room's seats:";
            for (int s = 0; s < 4; ++s)
                if (mask & (1 << s)) t += " " + std::to_string(s + 1) + (s == localPlayer() ? " (this PC)" : "");
            log(t);
        }
    }
    if (r > 0) {
        settings_ = settings;   // (the client: the host's game; its seats)
        multi_ = settings.pcs == 4;
        charsFixed_ = true;
    }
    return r;
}

bool NetGame::peerIn() const
{
    if (multi_ && session_.isHost()) {
        for (int s = 1; s <= 3; ++s)
            if (seat(s).peerConnected()) return true;
        return false;
    }
    return session_.peerConnected();
}

int NetGame::seatChar(int s) const
{
    if (s < 0 || s > 3)
        return -1;
    if (charsFixed_)   // the game's (the host started it; a joiner has its settings)
        return settings_.pcMode || settings_.pmChar[0] != 0 ? settings_.pmChar[s] : -1;   // (charsel: an arcade game's picks too)
    if (!session_.isHost())   // a joiner in the waiting room: the host's ROSTER
        return session_.rosterReceived ? session_.roster[s].pcChar : -1;
    if (!args_.pcMode && !args_.charPick && !charPickOn_)   // (charsel: an arcade game with Character Select picks)
        return -1;
    auto pick = [s](int c) { return c >= 1 && c <= kPmCharMax ? c : s + 1; };   // 0: the seat's default Rogan / G / Rogan 2 / G 2
    if (multi_) {
        const int o = owner_[s] >= 0 ? owner_[s] : s;   // (a PC's second seat: that PC's second pick)
        if (o == 0)
            return pick(args_.pcChar[s == 0 ? 0 : 1]);
        return seat(o).peerConnected() ? pick(seat(o).peerChar(o != s ? 1 : 0)) : s + 1;
    }
    if (s == 0 || s == 2)   // two PCs: the host P1 (+ P3 in the 4-seat game), the joiner P2 (+ P4)
        return pick(args_.pcChar[s / 2]);
    return session_.peerConnected() ? pick(session_.peerChar(s / 2)) : s + 1;
}

int NetGame::bossMode() const
{
    if (charsFixed_)
        return settings_.bossMode;
    if (!session_.isHost())
        return session_.rosterReceived ? session_.roster[0].boss : 0;
    return args_.pcMode ? 0 : args_.bossMode;
}

void NetGame::roster(Session::RosterSeat out[4]) const
{
    auto ping = [](const Session& s) { return uint16_t(std::min(s.rttMs() + 0.5, 65535.0)); };
    if (multi_ && session_.isHost()) {
        out[0] = {true, 0, session_.name()};
        for (int s = 1; s <= 3; ++s) {
            const int k = owner_[s] >= 0 ? owner_[s] : s;   // (a PC's second seat: that PC's name and ping)
            if (k == 0) { out[s] = {true, 0, session_.name(), 0}; continue; }
            const bool in = seat(k).peerConnected() && (!started_ || seatLive(k));
            out[s] = {in, ping(seat(k)), in ? seat(k).peerName() : std::string(), k != s && in ? k : -1};
        }
        for (int s = 0; s < 4; ++s)
            out[s].pcChar = seatChar(s);   // (PC Mode: each seat's character)
        out[0].boss = bossMode();
        return;
    }
    if (session_.isHost()) {   // two PCs: this one and the other
        out[0] = {true, 0, session_.name()};
        out[1] = {session_.peerConnected(), ping(session_), session_.peerConnected() ? session_.peerName() : std::string()};
        out[2] = out[3] = {};
        for (int s = 0; s < 4; ++s)
            out[s].pcChar = seatChar(s);
        out[0].boss = bossMode();
        return;
    }
    // a joiner: the host's ROSTER (each seat's name and its ping to the host); before it came: the host and this PC
    for (int s = 0; s < 4; ++s)
        out[s] = session_.roster[s];
    if (!session_.rosterReceived) {
        for (int s = 0; s < 4; ++s) out[s] = {};
        out[0] = {session_.peerConnected(), 0, ""};
        out[session_.localPlayer()] = {session_.peerConnected(), ping(session_), session_.name()};
    }
    out[session_.localPlayer()].pingMs = ping(session_);   // (this PC's own, fresh)
}

void NetGame::sendRosters()
{
    const double now = nowS();
    if (now - rosterAt_ < 0.5)
        return;
    rosterAt_ = now;
    Session::RosterSeat r[4];
    roster(r);
    if (!multi_) {
        session_.sendRoster(r);
        return;
    }
    for (int s = 1; s <= 3; ++s)
        if (owner_[s] < 0) seat(s).sendRoster(r);
}

void NetGame::renewSeat(int s)
{
    dropPair(s);   // (its PC's second seat is free too)
    seat_[s - 1] = std::make_unique<Session>();
    Options so = opts_;
    so.hub = false;
    so.relaySeat = s;
    std::string e;
    if (!seat_[s - 1]->startShared(so, session_, e) && log)
        log("seat " + std::to_string(s + 1) + ": " + e);
    seat_[s - 1]->reserved = owner_[s] >= 0;
    seatState_[s] = Seat::Empty;
}

int NetGame::secondSeat() const
{
    if (!multi())
        return settings_.seats == 4 ? localPlayer() + 2 : -1;
    if (session_.isHost())
        return pair_[0] ? pair_[0] : -1;
    return session_.secondSeat();
}

int NetGame::pcAt(int s) const
{
    if (s == 0 || owner_[s] == 0)
        return 0;
    const int k = owner_[s] > 0 ? owner_[s] : s;
    return seatLive(k) ? k : -1;
}

void NetGame::takePair(int s)
{
    // the joiner's own seat's partner on the same side (P2 + P4) when free, else any free seat; none: one seat
    if (pair_[s] || seat(s).peerWants() != 2)
        return;
    auto freeSeat = [this, s](int t) {
        return t >= 1 && t <= 3 && t != s && owner_[t] < 0 && !pair_[t] && seatState_[t] == Seat::Empty &&
               !seat(t).peerConnected() && !seat(t).pending();
    };
    int t = freeSeat(s + 2) ? s + 2 : 0;
    for (int k = 1; k <= 3 && !t; ++k)
        if (freeSeat(k)) t = k;
    if (!t) {   // (the waiting room asks again while it lasts: a seat may come free)
        seat(s).setPcSeats(1);
        return;
    }
    pair_[s] = t, owner_[t] = s;
    seat(t).reserved = true;
    seat(s).setPcSeats(2);
    if (log) log("player " + std::to_string(s + 1) + "'s PC plays seats " + std::to_string(s + 1) + " and " + std::to_string(t + 1));
}

void NetGame::dropPair(int s)
{
    const int t = pair_[s];
    if (!t || s == 0)
        return;
    pair_[s] = 0, owner_[t] = -1;
    renewSeat(t);   // (a fresh, open seat)
}

void NetGame::announce(bool inGame)
{
    LobbyGame g;
    g.addr.port = session_.local().port;
    g.name = args_.lanName.empty() ? "HOTD game" : args_.lanName;
    g.players = uint8_t((session_.peerConnected() && !solo_ && !waitRejoin_ ? 2 : 1) * (settings_.seats == 4 ? 2 : 1));
    g.maxPlayers = settings_.seats;
    g.chapter = chapter_;
    // open on the LAN (the default): the password in the announcement (a LAN click joins; the internet needs the code)
    g.needsCode = args_.lanCode || normaliseCode(code_).empty();
    if (!g.needsCode)
        g.code = code_;
    // a host playing alone (or waiting for player 2 to come back) can be joined: listed as joinable
    g.inGame = !(solo_ || waitRejoin_) && (inGame || session_.released());
    g.build = buildId();
    g.publicIp = upnp_.externalIp();
    if (multi_ && session_.isHost()) {   // up to four PCs: n / 4, joinable while a seat is free
        Session::RosterSeat r[4];
        roster(r);
        int in = 0;
        for (const auto& x : r) in += x.in ? 1 : 0;
        g.players = uint8_t(in);
        g.maxPlayers = 4;
        g.inGame = in >= 4;
    }
    announcer_.tick(g);
}

void NetGame::hostStart(Settings s)
{
    if (args_.delay == 0)   // auto: from the measured round trip; with rollback a short delay, the rest predicted
        s.delay = uint8_t(rollback_ > 0 ? std::min(session_.suggestedDelay(), 2) : session_.suggestedDelay());
    chapter_ = s.chapter;
    // PC Mode online (protocol 16): every seat's character as the waiting room showed it (the joiners' own picks); the
    // host's Continues / Free-Play. An arcade game: all zero; charsel: an arcade game where the host or a joiner picked
    // characters (Character Select, cosmetic): every seat's character too (a seat not picked: Rogan / G / Rogan 2 / G 2)
    s.pcMode = args_.pcMode ? 1 : 0;
    if (!args_.pcMode) {
        bool picks = args_.charPick;
        if (multi_) {
            for (int o = 1; o < 4; ++o)
                picks = picks || (seat(o).peerConnected() && (seat(o).peerChar(0) != 0 || seat(o).peerChar(1) != 0));
        } else {
            picks = picks || (session_.peerConnected() && (session_.peerChar(0) != 0 || session_.peerChar(1) != 0));
        }
        charPickOn_ = picks;
    }
    for (int k = 0; k < 4; ++k)
        s.pmChar[k] = uint8_t(args_.pcMode || charPickOn_ ? seatChar(k) : 0);
    if (!args_.pcMode)
        s.pmContinues = s.pmFreePlay = 0;
    s.bossMode = uint8_t(bossMode());   // Boss Mode (protocol 18): the host's boss game (never with PC Mode)
    charsFixed_ = true;
    if (log && s.bossMode)
        log("Boss Mode: " + std::string(game::kBossNames[s.bossMode - 2]));
    if (log && args_.pcMode)
        log("PC Mode: characters " + std::to_string(s.pmChar[0]) + ", " + std::to_string(s.pmChar[1]) + ", " +
            std::to_string(s.pmChar[2]) + ", " + std::to_string(s.pmChar[3]) + "; continues " + std::to_string(s.pmContinues) +
            (s.pmFreePlay ? ", free play" : ""));
    if (log && !args_.pcMode && charPickOn_)
        log("Character Select: characters " + std::to_string(s.pmChar[0]) + ", " + std::to_string(s.pmChar[1]) + ", " +
            std::to_string(s.pmChar[2]) + ", " + std::to_string(s.pmChar[3]));
    if (multi_) {   // up to four PCs: each joined PC gets its seat; the free seats can join the running game
        s.seats = 4, s.pcs = 4;
        if (args_.delay == 0) {
            int d = 0;
            for (int k = 1; k <= 3; ++k)
                if (seat(k).peerConnected()) d = std::max(d, seat(k).suggestedDelay());
            s.delay = uint8_t(rollback_ > 0 ? std::min(std::max(d, 2), 2) : std::max(d, 2));
        }
        // the seats in at the start: the first ones with a PC up to the first free one (a later PC's seats, and a
        // free seat's PC that joins the running game, press start)
        auto filled = [this](int k) {
            const int o = owner_[k] >= 0 ? owner_[k] : k;
            return o == 0 || seat(o).peerConnected();
        };
        int first = 1;
        while (first <= 3 && filled(first)) ++first;
        s.players = uint8_t(first);
        settings_ = s;
        session_.setDelay(s.delay);
        for (int k = 1; k <= 3; ++k) {
            if (owner_[k] >= 0 || !seat(k).peerConnected()) continue;
            Settings ks = s;
            ks.seat = uint8_t(k);
            ks.seat2 = uint8_t(pair_[k]);
            if (seat(k).peerWants() == 2 && !pair_[k] && log)
                log("player " + std::to_string(k + 1) + " asked for two seats; only one was free");
            seat(k).release(ks);
            seatState_[k] = Seat::Active;
            seatFrom_[k] = 0;
        }
        for (uint32_t f = 0; f < s.delay; ++f)
            relay_[f] = own_[f] = {f, {}};
        relayNext_ = s.delay;
        started_ = true;
        if (log)
            log("starting with " + std::to_string(s.players) + " player(s), input delay " + std::to_string(s.delay) +
                " frames; free seats can join the running game");
        return;
    }
    if (!session_.peerConnected()) {   // alone: player 2 can join the running game later (and presses start)
        s.players = 1;
        settings_ = s;
        solo_ = true;
        session_.setDelay(s.delay);
        session_.setSeats(s.seats);
        session_.startAt(0);
        if (log) log("playing alone, open for player 2 to join (input delay " + std::to_string(s.delay) + " frames)");
        return;
    }
    settings_ = s;
    session_.release(s);
    if (log)
        log("starting: round trip " + std::to_string(int(session_.rttMs() + 0.5)) + " ms, input delay " +
            std::to_string(s.delay) + " frames");
}

void NetGame::close()
{
    announcer_.stop();
    upnp_.stop();
}

bool NetGame::connect(const NetArgs& a, Settings& settings, std::string& err, int timeoutS)
{
    NetArgs b = a;
    if (!a.join.empty() && (a.join == "lan" || a.join.rfind("lan:", 0) == 0)) {   // --join lan[:name]: the LAN list
        LobbyListener ls;
        Address bind{0, kLobbyPort};
        if (!a.lobby.empty() && !parseAddress(a.lobby, bind, false, err)) return false;
        if (!ls.start(bind, err)) return false;
        const std::string want = a.join.size() > 4 ? a.join.substr(4) : "";
        const double t0 = nowS();
        while (b.join == a.join && nowS() - t0 < (timeoutS > 0 ? timeoutS : 10)) {
            ls.poll(50);
            for (const LobbyGame& g : ls.games())
                if (!g.inGame && g.build == buildId() && (want.empty() || g.name == want)) {
                    b.join = g.addr.str();
                    if (b.code.empty() && !g.needsCode)   // (a game open on the LAN: its announced password)
                        b.code = g.code;
                    if (log) log("found \"" + g.name + "\" on the LAN at " + g.addr.str() + (g.needsCode ? "" : " (open on the LAN: no code needed)"));
                    break;
                }
        }
        if (b.join == a.join) { err = "no game found on the LAN"; return false; }
    }
    if (!open(b, err))
        return false;
    if (session_.isHost() && a.alone) {   // --net-alone: play at once; player 2 can join the running game
        hostStart(settings);
        return true;
    }
    const double t0 = nowS();
    double joinedAt = -1;
    for (;;) {
        const int r = poll(settings, err, 20);
        if (r < 0)
            return false;
        if (r > 0)
            break;
        // the command line's host starts by itself once player 2 is in and a few pings measured the round trip (up to
        // four PCs: once --net-wait PCs are in)
        if (multi_) {
            int in = 0, pings = 99;
            for (int k = 1; k <= 3; ++k)
                if (seat(k).peerConnected()) ++in, pings = std::min(pings, seat(k).pingSamples());
            if (in >= std::max(1, a.waitFor)) {
                if (joinedAt < 0) joinedAt = nowS();
                if (pings >= 4 || nowS() - joinedAt > 2.0)
                    hostStart(settings);
            }
        } else if (session_.isHost() && session_.peerConnected()) {
            if (joinedAt < 0) joinedAt = nowS();
            if (session_.pingSamples() >= 4 || nowS() - joinedAt > 2.0)
                hostStart(settings);
        }
        if (timeoutS > 0 && nowS() - t0 > timeoutS) {
            err = session_.isHost() ? "nobody joined in " + std::to_string(timeoutS) + " s"
                                    : "no answer from the host in " + std::to_string(timeoutS) + " s. " + reachHint(session_.peer().port);
            return false;
        }
    }
    if (multi_ && session_.isHost()) {
        if (log)
            for (int k = 1; k <= 3; ++k)
                if (seatLive(k))
                    log("connected: player " + std::to_string(k + 1) + " at " + seat(k).peer().str() + ", round trip " +
                        std::to_string(int(seat(k).rttMs() + 0.5)) + " ms");
        return true;
    }
    if (log)
        log(std::string("connected: ") + (session_.isHost() ? "player 2 at " : "the host at ") + session_.peer().str() +
            ", round trip " + std::to_string(int(session_.rttMs() + 0.5)) + " ms, input delay " +
            std::to_string(session_.delay()) + " frames");
    return true;
}

bool NetGame::ready(game::Runtime& rt, std::string& err, int timeoutS)
{
    if (solo_)   // alone: nobody to compare with yet (a joiner compares at its join frame)
        return true;
    if (multi_ && session_.isHost()) {   // up to four PCs: every joined PC's started state
        const uint64_t mine = bootHash(rt);
        for (int k = 1; k <= 3; ++k)
            if (seatLive(k)) seat(k).setReady(mine);
        const double t0 = nowS();
        for (;;) {
            pumpAll(10);
            bool all = true;
            for (int k = 1; k <= 3; ++k) {
                uint64_t theirs = 0;
                if (!seatLive(k)) continue;
                if (!seat(k).peerReady(theirs)) { all = false; continue; }
                if (theirs != mine) {
                    err = "Player " + std::to_string(k + 1) + "'s game is different at the start: every PC needs the same "
                          "HOTD ReWrithe build and the same game files (arcade ROMs / PC files).";
                    return false;
                }
            }
            if (all) break;
            if (nowS() - t0 > timeoutS) { err = "A joined PC did not start its game in time (it left, or lost the connection)."; return false; }
        }
        if (log) log("every PC's game started the same");
        return true;
    }
    const uint64_t mine = bootHash(rt);
    session_.setReady(mine);
    const double t0 = nowS();
    uint64_t theirs = 0;
    while (!session_.peerReady(theirs)) {
        session_.pump(10);
        if (nowS() - t0 > timeoutS) { err = "The other PC did not start its game in time (it left, or lost the connection)."; return false; }
    }
    if (theirs != mine) {
        char b[96];
        std::snprintf(b, sizeof b, "the start states differ: %016llx here, %016llx there", (unsigned long long)mine,
                      (unsigned long long)theirs);
        if (log) log(b);
        err = "The two PCs' games are different at the start: both need the same HOTD ReWrithe build and the same game "
              "files (arcade ROMs / PC files).";
        return false;
    }
    if (log) {
        char b[96];
        std::snprintf(b, sizeof b, "both games started the same (state %016llx)", (unsigned long long)mine);
        log(b);
    }
    return true;
}

uint8_t NetGame::pausedSeats() const
{
    const int me = localPlayer();
    // this PC's seats (up to four PCs: both of a PC with two; the two-PC games as before: the first only)
    const uint8_t mine = uint8_t(1u << me | (multi() && secondSeat() > 0 ? 1u << secondSeat() : 0));
    uint8_t m = paused_ ? mine : 0;
    if (multi_ && session_.isHost()) {   // the host of up to four PCs: each seat's PC tells its own
        for (int s = 1; s <= 3; ++s)
            if (seatLive(s) && seat(s).peerConnected())
                m |= uint8_t(seat(s).peerPaused() & (1u << s | (pair_[s] ? 1u << pair_[s] : 0)));
    } else if (session_.peerConnected()) {   // the other PC (the host: every other seat it knows)
        // (bits 4-7, 2026-10-30: the host's "these seats' PCs left, the game waits"; only the host's count)
        m |= uint8_t(session_.peerPaused() & ~mine & (session_.isHost() ? 0x0F : 0xFF));
    }
    if (session_.isHost())
        m |= uint8_t(dropHold_ << 4);
    return m;
}

int NetGame::dropMode() const
{
    if (args_.dropContinueS != -2)
        return args_.dropContinueS;
    return askOnDrop || !multi_ ? -1 : 0;   // (a headless game of up to four PCs: on at once, as before)
}

std::string NetGame::dropText() const
{
    std::string who;
    for (int k = 1; k < 4; ++k)
        if (dropHold_ & (1u << k)) who += (who.empty() ? "P" : " + P") + std::to_string(k + 1);
    if (who.empty()) who = "A player";
    const int mode = dropMode();
    std::string s = who + " left: waiting for them to join again (same code). Enter / Start: go on without them";
    if (mode > 0)
        s += " (on by itself in " + std::to_string(std::max(0, int(mode - (nowS() - dropAt_) + 0.999))) + " s)";
    return s;
}

void NetGame::dropContinue()
{
    if (!dropAsk())
        return;
    if (log) log("the host goes on without the players who left (frame " + std::to_string(frame_) + "): their seats are free to join");
    if (!multi_ && waitRejoin_) {   // two PCs: alone now, still open for a join (as Play Alone)
        waitRejoin_ = false;
        solo_ = true;
    }
    dropHold_ = 0, dropPcs_ = 0;
    syncPause();
}

void NetGame::leave(int lingerMs)
{
    if (left_ || finished_)   // (finish told the end already)
        return;
    left_ = true;
    if (multi_ && session_.isHost()) {   // the host: the game ends for every PC (a normal BYE each, as before)
        for (int k = 1; k <= 3; ++k)
            if (seatLive(k)) seat(k).bye(frame_, 0);
    } else {
        session_.bye(kByeLeaving, 0);
    }
    const double t0 = nowS();
    while (nowS() - t0 < lingerMs / 1000.0)
        pumpAll(5);
    if (log) log("left the online game at frame " + std::to_string(frame_));
}

std::string NetGame::pausedText() const
{
    const uint8_t own = uint8_t((1u << localPlayer() | (multi() && secondSeat() > 0 ? 1u << secondSeat() : 0u)) << 4);
    const uint8_t all = uint8_t(pausedSeats() & ~own);   // (a PC joining again: its own seat's "left" not shown)
    if (!session_.isHost() && (all >> 4)) {   // the host waits: a PC left (its choice: wait for it / go on)
        std::string who;
        for (int k = 0; k < 4; ++k)
            if (all & (16u << k)) who += (who.empty() ? "P" : " + P") + std::to_string(k + 1);
        return who + " left: the host waits for them or goes on";
    }
    const uint8_t m = uint8_t(all & 0x0F & ~(1u << localPlayer()) & ~(multi() && secondSeat() > 0 ? 1u << secondSeat() : 0u));
    if (!m)
        return "";
    std::string s;
    for (int k = 0; k < 4; ++k)
        if (m & (1u << k)) s += "P" + std::to_string(k + 1) + " ";
    return s + "PAUSED";
}

void NetGame::syncPause()
{
    const uint8_t m = pausedSeats();
    if (multi_ && session_.isHost()) {
        for (int s = 1; s <= 3; ++s)
            if (seatLive(s)) seat(s).setPausedOut(m);
    } else {
        session_.setPausedOut(m);
    }
}

void NetGame::setPaused(bool on)
{
    if (on != paused_ && log)
        log(on ? "this PC paused" : "this PC goes on");
    paused_ = on;
    syncPause();
}

void NetGame::idle()
{
    syncPause();
    pumpAll(0);
    syncPause();
}

NetGame::Step NetGame::step(game::Runtime& rt, const std::function<Input()>& sample, int waitMs)
{
    syncPause();
    if (multi_)
        return stepMulti(rt, sample, waitMs);
    if (session_.desync) {
        if (!reportedDesync_ && log) {
            char b[160];
            std::snprintf(b, sizeof b, "NET: desync at frame %u: this PC's state hash %08x, the other's %08x", session_.desyncFrame,
                          session_.desyncLocal, session_.desyncPeer);
            log(b);
        }
        reportedDesync_ = true;
        status = "Out of sync with the other PC at frame " + std::to_string(session_.desyncFrame) + ": the game stopped (Esc)";
        return Step::Stop;
    }
    // the host alone (player 2 not there yet, or gone): open for a join; a joiner holds the game until it caught up
    if (session_.isHost() && (solo_ || waitRejoin_)) {
        if (!joining_) {
            Settings unused;
            std::string e;
            session_.handshake(unused, e, 0);
            if (session_.peerConnected() && frame_ > 0)
                beginJoin(rt);
        }
        if (joining_) {
            session_.pump(std::min(waitMs, 5));
            uint64_t h = 0;
            if (session_.peerReady(h)) {
                if (h == joinHash_) {
                    joining_ = solo_ = waitRejoin_ = false;
                    dropHold_ = 0, dropPcs_ = 0;
                    if (log) log("player 2 joined the running game at frame " + std::to_string(frame_));
                } else {
                    if (log) log("the joining PC's game came out different (another build or other game files): not joined");
                    renewSession();
                }
            } else if (session_.silentSeconds() > 10.0) {
                if (log) log("the joining PC went away");
                renewSession();
            }
            if (joining_ || waitRejoin_ || !solo_) {
                announce(true);
                if (joining_) {
                    status = "Player 2 is joining...";
                    return Step::Wait;
                }
            }
        }
        if (waitRejoin_) {
            announce(false);
            // (2026-10-30) the host's choice: wait for them (the seat kept: they join again with the same code) or go
            // on without them (playing alone, still open for a join: as Play Alone)
            const int mode = dropMode();
            if (mode == 0 || (mode > 0 && nowS() - dropAt_ >= mode)) {
                dropContinue();
            } else {
                status = dropText();
                return Step::Wait;
            }
        }
        if (solo_) {
            session_.fillRemote(frame_);   // player 2's input: none
            if (args_.soloFps > 0) {       // (testing: headless at a set speed, so a joiner can come)
                static double next = 0;
                const double now = nowS();
                if (next < now - 1.0) next = now;
                if (now < next) std::this_thread::sleep_for(std::chrono::duration<double>(next - now));
                next += 1.0 / args_.soloFps;
            }
        }
    }
    const uint32_t due = frame_ + session_.delay();
    if (int64_t(due) > submitted_) {
        session_.submit(due, picked(sample()));
        submitted_ = due;
    }
    Input in[kMaxPlayers];
    session_.pump(0);
    if (rollback_ > 0) {   // rollback: run ahead on a prediction, corrected when the real input comes
        reconcile(rt);
        if (session_.desync)
            return step(rt, sample, 0);   // (reported above)
        const int lp = localPlayer(), rp = 1 - lp;
        if (frame_ - confirmed_ < uint32_t(rollback_) && session_.localInput(frame_, in[lp])) {
            Frame& fr = ring_[frame_ % kRingFrames];
            fr.predicted = !session_.remoteInput(frame_, in[rp]);
            if (fr.predicted) {
                in[rp] = lastRemote_.predicted();   // the aim stays, no trigger / start repeated
                ++predicted_;
            }
            fr.state.save(rt);
            fr.cues.clear();
            rt.cueLog = &fr.cues;   // (the sounds this run of the frame sends: a re-run compares)
            fr.used[0] = in[0], fr.used[1] = in[1];
            waitStart_ = -1;
            status.clear();
            apply(rt, in, frame_);
            announce(true);
            return Step::Run;
        }
        if (waitMs > 0) { session_.pump(waitMs); reconcile(rt); }
    } else if (!session_.inputs(frame_, in) && waitMs > 0)
        session_.pump(waitMs);
    announce(true);
    if (rollback_ > 0 || !session_.inputs(frame_, in)) {
        if (waitStart_ < 0) {
            waitStart_ = nowS();
            ++waits_;
        }
        const double waited = nowS() - waitStart_;
        uint32_t pf;
        uint64_t ph;
        if (session_.peerBye(pf, ph)) {   // the other PC ended its game (it quit, or its run ended)
            if (session_.isHost() && pf == kByeLeaving) {   // player 2 closed / quit mid-game: its seat kept for a rejoin
                if (log) log("player 2 left the game (closed or quit)");
                beginRejoinWait(rt);
                return Step::Wait;
            }
            status = pf == kByeLeaving ? "The host left the game (Esc: quit)" : "The other player left the game (Esc: quit)";
            return Step::Stop;
        }
        // the host: player 2 silent for 6 s: back to the last confirmed frame and open for them to join again
        // (or at once when the same PC asks to join again: Session::rejoinAsked)
        if (session_.isHost() && session_.peerConnected() && (session_.silentSeconds() > 6.0 || session_.rejoinAsked())) {
            beginRejoinWait(rt);
            return Step::Wait;
        }
        const double silent = session_.silentSeconds();
        status = silent > 20.0 ? "The other PC seems gone (no signal for " + std::to_string(int(silent)) + " s). Esc: quit"
                 : silent > 3.0 ? "Connection to the other PC lost for " + std::to_string(int(silent)) + " s: waiting (Esc: quit)"
                 : !pausedText().empty() ? pausedText()
                 : waited > 0.3 ? "Waiting for the other player..."
                                : "";
        return Step::Wait;
    }
    waitStart_ = -1;
    status.clear();
    apply(rt, in, frame_);
    return Step::Run;
}

void NetGame::apply(game::Runtime& rt, const Input in[kMaxPlayers], uint32_t f)
{
    // (the history a joining PC replays: the inputs each frame ran with; a re-simulated frame overwrites its entry)
    if (history_.size() <= f)
        history_.resize(size_t(f) + 1);
    history_[f] = {in[0], in[1]};
    applyInputs(rt, in);
}

bool NetGame::onlyHostSkips(const game::Runtime& rt)
{
    return rt.mem.at<int16_t>(game::A::G::game_mode) == 4 && rt.mem.u32(game::A::G::shot_enable) == 0 && !rt.name_entry_active();
}

Input NetGame::picked(Input in) const
{
    if (!settings_.pcMode && settings_.pmChar[0] == 0)   // (an arcade game without Character Select picks: the inputs as before)
        return in;
    auto put = [](uint8_t& f, int c) {
        if ((f & Input::kStart) && c >= 1 && c <= kPmCharMax)
            f = uint8_t((f & ~Input::kChar) | c << Input::kCharShift);
    };
    put(in.flags, args_.pcChar[0]);
    put(in.flags2, args_.pcChar[1]);   // (this PC's second seat: the host's P3, a two-seat joiner's second)
    return in;
}

void NetGame::applyInputs(game::Runtime& rt, const Input in[kMaxPlayers])
{
    // only the host skips a cut-scene (user 2026-10-26): while the shots are off in play (a cut-scene: a trigger there is
    // the skip request) the other PCs' triggers are dropped; from the game's state, so alike on every PC
    const bool hostOnly = onlyHostSkips(rt);
    for (int p = 0; p < kMaxPlayers; ++p) {
        game::Runtime::GunInput& g = rt.gun[p];
        g.onScreen = (in[p].flags & Input::kOnScreen) != 0;
        g.trigger = (in[p].flags & Input::kTrigger) != 0 && !(hostOnly && p != 0);
        g.x = in[p].x;
        g.y = in[p].y;
    }
    // the 4-seat game (docs/coop4.md phase 7): each PC's second seat, seats 2 / 3 (the host's player 3, the client's 4;
    // up to four PCs: seat 2 is a joiner's or the host's second; only P1 skips there either way)
    const bool four = rt.coop4_on();
    if (four)
        for (int p = 0; p < kMaxPlayers; ++p) {
            game::Runtime::GunInput& g = rt.gun[p + 2];
            g.onScreen = (in[p].flags2 & Input::kOnScreen) != 0;
            g.trigger = (in[p].flags2 & Input::kTrigger) != 0 && !(hostOnly && (p != 0 || multi()));
            g.x = in[p].x2;
            g.y = in[p].y2;
        }
    // PC Mode (protocol 17): a start carrying a character (Input::kChar) gives a seat that is not in the game (waiting:
    // state 9; never a continue) that character before its start runs (PlStartInit / the 4P join read it): a joiner's
    // own pick in a running game. From the frame's inputs and the game's state, so alike on every PC and in a replay
    if (rt.pcmode || rt.charpick)   // (charsel: an arcade game's Character Select picks too)
        for (int s = 0; s < (four ? 4 : 2); ++s) {
            const uint8_t f = s < 2 ? in[s].flags : in[s - 2].flags2;
            const int c = (f & Input::kChar) >> Input::kCharShift;
            if ((f & Input::kStart) && c >= 1 && c <= kPmCharMax && rt.mem.u32(rt.player_rec(s)) == 9)
                rt.pm_char[s] = c;
        }
    for (int p = 0; p < kMaxPlayers; ++p) {
        if (in[p].flags & Input::kStart)
            rt.player_join(p);
        if (four && (in[p].flags2 & Input::kStart))
            rt.player_join(p + 2);
    }
}

std::vector<uint32_t> NetGame::cuesOf(uint32_t from, uint32_t to) const
{
    std::vector<uint32_t> v;
    for (uint32_t f = from; f < to; ++f) {
        const std::vector<uint32_t>& c = ring_[f % kRingFrames].cues;
        v.insert(v.end(), c.begin(), c.end());
    }
    return v;
}

void NetGame::playLate(game::Runtime& rt, const std::vector<uint32_t>& heard, const std::vector<uint32_t>& now)
{
    // the re-run's sounds that the first run did not send (a remote shot that came after its frame ran on a
    // prediction: user 2026-10-26 "the host couldn't hear P2's gunfire"): heard now, a few frames late, instead of never.
    // A sound the first run sent and the re-run did not (a predicted shot that was not) has played already
    std::map<uint32_t, int> left;
    for (uint32_t c : heard) ++left[c];
    for (uint32_t c : now) {
        int& n = left[c];
        if (n > 0) { --n; continue; }
        ++lateCues_;
        if (rt.audio) rt.audio->cue(c);
    }
}

void NetGame::reconcile(game::Runtime& rt)
{
    // the frames before frame_ whose real remote input is now known: the same as predicted -> confirmed (their hash
    // reported); different -> back to that frame's snapshot and every frame since again (no sound), then on
    if (multi_) {
        reconcileMulti(rt);
        return;
    }
    const int rp = 1 - localPlayer();
    while (confirmed_ < frame_) {
        Input real;
        if (!session_.remoteInput(confirmed_, real))
            break;
        lastRemote_ = real;
        Frame& fr = ring_[confirmed_ % kRingFrames];
        if (fr.predicted && !(real == fr.used[rp])) {
            ++rollbacks_;
            const uint32_t from = confirmed_, to = frame_;
            fr.state.restore(rt);
            game::Audio* audio = rt.audio;
            rt.audio = nullptr;   // the sounds played already (at the prediction); the new ones: late, below
            auto log = std::move(rt.log);
            rt.log = [](const std::string&) {};   // (its lines too)
            const std::vector<uint32_t> heard = cuesOf(from, to);
            for (uint32_t f = from; f < to; ++f) {
                Frame& g = ring_[f % kRingFrames];
                Input in[kMaxPlayers];
                session_.localInput(f, in[localPlayer()]);
                g.predicted = !session_.remoteInput(f, in[rp]);
                if (g.predicted)
                    in[rp] = (f == from ? real : lastRemoteAt(f)).predicted();
                if (f != from)
                    g.state.save(rt);
                g.used[0] = in[0], g.used[1] = in[1];
                apply(rt, in, f);
                g.cues.clear();
                rt.cueLog = &g.cues;
                rt.frame();
                rt.cueLog = nullptr;
                g.hash = gameHash(rt);
                ++resimulated_;
            }
            rt.audio = audio;
            rt.log = std::move(log);
            playLate(rt, heard, cuesOf(from, to));
            continue;   // (the frame is now simulated with the real input: confirmed below on the next pass)
        }
        session_.frameHash(confirmed_, fr.hash);
        ++confirmed_;
    }
}

Input NetGame::lastRemoteAt(uint32_t f) const
{
    // the prediction for frame f: the newest real remote input before it
    Input in;
    for (uint32_t k = f; k-- > 0 && f - k <= kRingFrames;)
        if (session_.remoteInput(k, in))
            return in;
    return lastRemote_;
}

bool NetGame::settled() const
{
    return rollback_ == 0 || confirmed_ >= frame_;
}

void NetGame::settle(game::Runtime& rt, int timeoutMs)
{
    uint32_t df, dm, dt;
    for (const double t = nowS(); !settled() && !anyDesync(df, dm, dt) && nowS() - t < timeoutMs / 1000.0;) {
        pumpAll(5);
        if (multi_ && session_.isHost()) relay();
        reconcile(rt);
    }
}

// the input log a joining PC replays (docs/netplay.md "Joining a running game"): "HL" u32 frames, u16 restarts and
// their frames (u32, the frames run before each restart), then runs: u16 count + the two players' inputs (9 bytes
// each). The 4-seat game: "H4", each PC's input 18 bytes (its two seats). The decoder checks every count, flag and float.
std::vector<uint8_t> NetGame::encodeLog(uint32_t frames) const
{
    const bool four = settings_.seats == 4;
    std::vector<uint8_t> v = {'H', uint8_t(four ? '4' : 'L')};
    auto p32 = [&v](uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i))); };
    auto pin = [&v, &p32, four](const Input& in) {
        v.push_back(in.flags);
        uint32_t x, y;
        std::memcpy(&x, &in.x, 4), std::memcpy(&y, &in.y, 4);
        p32(x), p32(y);
        if (four) {
            v.push_back(in.flags2);
            std::memcpy(&x, &in.x2, 4), std::memcpy(&y, &in.y2, 4);
            p32(x), p32(y);
        }
    };
    p32(frames);
    std::vector<uint32_t> rs;
    for (uint32_t r : restarts_)
        if (r <= frames) rs.push_back(r);
    v.push_back(uint8_t(rs.size())), v.push_back(uint8_t(rs.size() >> 8));
    for (uint32_t r : rs) p32(r);
    for (uint32_t f = 0; f < frames;) {
        const auto& cur = history_[f];
        uint32_t n = 1;
        while (f + n < frames && n < 0xFFFF && history_[f + n][0] == cur[0] && history_[f + n][1] == cur[1]) ++n;
        v.push_back(uint8_t(n)), v.push_back(uint8_t(n >> 8));
        pin(cur[0]), pin(cur[1]);
        f += n;
    }
    return v;
}

bool NetGame::decodeLog(const std::vector<uint8_t>& v, std::vector<std::array<Input, 2>>& frames, std::vector<uint32_t>& restarts)
{
    return decodeInputLog(v, frames, restarts);   // (session.cpp: hotd_nettest fuzzes it)
}

void NetGame::renewSession()
{
    // a new session on the same port (the old peer's keys gone): this PC's own inputs from frame_ on carried over
    std::vector<Input> mine;
    for (int64_t f = frame_; f <= submitted_; ++f) {
        Input in;
        session_.localInput(uint32_t(f), in);
        mine.push_back(in);
    }
    const uint8_t d = session_.delay();
    const int wrong = session_.wrongCodes();   // (the wrong-password limit is for the whole hosting, not per session)
    session_.~Session();
    new (&session_) Session();
    std::string e;
    if (!session_.start(opts_, e) && log)
        log("hosting again failed: " + e);
    session_.setWrongCodes(wrong);
    session_.setDelay(d);
    session_.setSeats(settings_.seats);
    session_.seedLocal(frame_, mine);
    joining_ = false;
}

void NetGame::beginRejoinWait(game::Runtime& rt)
{
    // rollback: back to the last frame both inputs confirm (the joiner replays to there)
    if (rollback_ > 0 && frame_ > confirmed_) {
        ring_[confirmed_ % kRingFrames].state.restore(rt);
        frame_ = confirmed_;
    }
    history_.resize(frame_);
    if (log) log("player 2 lost the connection at frame " + std::to_string(frame_) + ": open for them to join again");
    renewSession();
    waitRejoin_ = true;
    dropHold_ = uint8_t(settings_.seats == 4 ? 0x0A : 0x02);   // (P2, and P4 in the 4-seat game on two PCs)
    dropPcs_ = 1;
    dropAt_ = nowS();
}

void NetGame::beginJoin(game::Runtime& rt)
{
    // the game is held at frame F: the joiner gets the start settings and the log of every frame's inputs (and the
    // restarts), replays them, and both compare the state at F before going on together
    const uint32_t F = frame_;
    history_.resize(F);
    std::vector<uint8_t> lg = encodeLog(F);
    Settings js = settings_;
    js.joinFrame = F;
    js.logBytes = uint32_t(lg.size());
    js.delay = session_.delay();
    session_.setLog(std::move(lg));
    session_.release(js);
    joinHash_ = bootHash(rt);
    session_.setReady(joinHash_);
    joining_ = true;
    if (log) log("player 2 is joining at frame " + std::to_string(F) + " (log " + std::to_string(js.logBytes) + " bytes)");
}

bool NetGame::joinLog(std::vector<std::array<Input, 2>>& frames, std::vector<uint32_t>& restarts, std::string& err) const
{
    if (!decodeLog(session_.log(), frames, restarts) || frames.size() != session_.joinFrame()) {
        err = "the host's game log is not valid";
        return false;
    }
    return true;
}

void NetGame::joinedAt(uint32_t F)
{
    session_.startAt(F);
    frame_ = F;
    confirmed_ = F;
    submitted_ = int64_t(F) + session_.delay() - 1;
    history_.resize(F);
}

std::string NetGame::stats() const
{
    char b[200];
    // short lines (the overlay sits between the players' HUD corners at the bottom)
    std::snprintf(b, sizeof b, "ping %d ms (last %d)\ndelay %d  frame %u\nrollback %d: %llu (%llu resim)\nwaited %llu  %s",
                  int(session_.rttMs() + 0.5), int(session_.rttLastMs() + 0.5), int(session_.delay()), frame_, rollback_,
                  (unsigned long long)rollbacks_, (unsigned long long)resimulated_, (unsigned long long)waits_,
                  session_.desync ? "DESYNC" : "in sync");
    return b;
}

NetGame::Link NetGame::link()
{
    Link l;
    const double now = nowS();
    if (multi_ && session_.isHost()) {
        l.alone = true;
        for (int s = 1; s <= 3; ++s)
            if (seatLive(s)) l.alone = false, l.pingMs = std::max(l.pingMs, int(seat(s).rttMs() + 0.5));
    } else if (session_.isHost() && (solo_ || waitRejoin_)) {
        l.alone = true;
    } else {
        l.pingMs = int(session_.rttMs() + 0.5);
    }
    // lag: about half a second of corrections a second (30 frames re-simulated), or the game waiting for inputs twice
    // a second; shown for 3 s after
    if (now - linkAt_ >= 1.0) {
        if (linkAt_ > 0 && (resimulated_ - linkResim_ >= 30 || waits_ - linkWaits_ >= 2))
            lagUntil_ = now + 3.0;
        linkAt_ = now, linkResim_ = resimulated_, linkWaits_ = waits_;
    }
    l.lag = !l.alone && (now < lagUntil_ || (waitStart_ >= 0 && now - waitStart_ > 0.2));
    l.level = l.lag || l.pingMs >= 150 ? 2 : l.pingMs >= 80 ? 1 : 0;
    return l;
}

void NetGame::after(game::Runtime& rt)
{
    // testing: HOTD_NET_DESYNC_AT=F changes one byte of this PC's work RAM after frame F (the desync check must report F)
    static const long long desyncAt = std::getenv("HOTD_NET_DESYNC_AT") ? std::atoll(std::getenv("HOTD_NET_DESYNC_AT")) : -1;
    if (desyncAt >= 0 && frame_ == uint64_t(desyncAt) && rt.mem.mapped(0x5FFF00))
        rt.mem.at<uint8_t>(0x5FFF00) ^= 0x5A;
    rt.cueLog = nullptr;
    if (rollback_ > 0)   // reported once the frame is confirmed (reconcile)
        ring_[frame_ % kRingFrames].hash = gameHash(rt);
    else
        frameHashAll(frame_, gameHash(rt));
    ++frame_;
}

// ---- up to four PCs (docs/netplay.md "Four PCs"): the host relays every frame's four seats ----

void NetGame::pumpAll(int waitMs)
{
    session_.pump(waitMs);   // (the hub: receives every seat's packets)
    if (multi_ && session_.isHost())
        for (int s = 1; s <= 3; ++s)
            seat(s).pump(0);
}

bool NetGame::anyDesync(uint32_t& frame, uint32_t& mine, uint32_t& theirs) const
{
    bool d = false;
    auto take = [&](const Session& s) {
        if (s.desync && (!d || s.desyncFrame < frame)) d = true, frame = s.desyncFrame, mine = s.desyncLocal, theirs = s.desyncPeer;
    };
    take(session_);
    if (multi_ && session_.isHost())
        for (int s = 1; s <= 3; ++s)
            if (seatLive(s)) take(seat(s));
    return d;
}

void NetGame::frameHashAll(uint32_t f, uint64_t h)
{
    if (multi_ && session_.isHost()) {
        for (int s = 1; s <= 3; ++s)
            if (seatLive(s)) seat(s).frameHash(f, h);
        return;
    }
    session_.frameHash(f, h);
}

bool NetGame::ownKnown(uint32_t f, Input& own)
{
    if (session_.isHost()) {
        const Full& o = own_[f % kRelayRing];
        if (o.frame != f) return false;
        own = o.in[0];
        return true;
    }
    return session_.localInput(f, own);
}

bool NetGame::realFull(uint32_t f, Input out[2]) const
{
    if (session_.isHost()) {
        const Full& r = relay_[f % kRelayRing];
        if (r.frame != f) return false;
        out[0] = r.in[0], out[1] = r.in[1];
        return true;
    }
    return session_.remoteFull(f, out);
}

void NetGame::predictFull(uint32_t f, Input out[2])
{
    // the newest relayed frame before f, without its triggers / starts; this PC's own seat as it is
    Input base[2] = {lastFull_[0], lastFull_[1]};
    for (uint32_t k = f; k-- > 0 && f - k <= kRingFrames;)
        if (realFull(k, base)) break;
    out[0] = base[0].predicted(), out[1] = base[1].predicted();
    Input own;
    if (ownKnown(f, own))
        setSeat(out, session_.isHost() ? 0 : session_.localPlayer(), own);
    if (ownKnown(f, own) && secondSeat() > 0)   // (a PC with two seats: its second from the same input)
        setSeat(out, secondSeat(), Input{own.flags2, own.x2, own.y2});
    if (session_.isHost() && hostSeats) {   // the computer teammates' seats are known here (as relay() takes them)
        const Full& o = own_[f % kRelayRing];
        if (o.frame == f)
            for (int s = 1; s <= 3; ++s)
                if (pcAt(s) < 0 || f < seatFrom(s))
                    setSeat(out, s, seatOf(o.in, s));
    }
}

uint8_t NetGame::freeSeats() const
{
    uint8_t m = 0;
    if (multi_)
        for (int s = 1; s <= 3; ++s)
            if (pcAt(s) < 0)
                m = uint8_t(m | (1u << s));
    return m;
}

void NetGame::relay()
{
    // host: frame f goes to every seat's PC once this PC's and every joined seat's input of f are in (a free seat:
    // none, or its computer teammate's from hostSeats)
    while (int64_t(relayNext_) <= submitted_) {
        const uint32_t f = relayNext_;
        const Full& o = own_[f % kRelayRing];
        if (o.frame != f) break;
        Input full[2] = {o.in[0], o.in[1]};   // this PC's seat and the computer teammates' (hostSeats)
        bool ok = true;
        for (int s = 1; s <= 3 && ok; ++s) {
            if (!seatLive(s) || f < seatFrom_[s]) continue;
            seat(s).markRun(f);
            Input x;
            if (!seat(s).remoteInput(f, x)) ok = false;
            else {
                setSeat(full, s, Input{uint8_t(x.flags), x.x, x.y});
                if (pair_[s]) setSeat(full, pair_[s], Input{x.flags2, x.x2, x.y2});   // (its PC's second seat)
            }
        }
        if (!ok) break;
        Full& r = relay_[f % kRelayRing];
        r.frame = f, r.in[0] = full[0], r.in[1] = full[1];
        for (int s = 1; s <= 3; ++s)
            if (seatLive(s)) seat(s).submitFull(f, full);
        ++relayNext_;
    }
}

void NetGame::reconcileMulti(game::Runtime& rt)
{
    // as reconcile: the frames whose real four inputs are now known (relayed): as predicted -> confirmed, else again
    while (confirmed_ < frame_) {
        Input real[2];
        if (!realFull(confirmed_, real))
            break;
        lastFull_[0] = real[0], lastFull_[1] = real[1];
        Frame& fr = ring_[confirmed_ % kRingFrames];
        if (fr.predicted && !(real[0] == fr.used[0] && real[1] == fr.used[1])) {
            ++rollbacks_;
            const uint32_t from = confirmed_, to = frame_;
            fr.state.restore(rt);
            game::Audio* audio = rt.audio;
            rt.audio = nullptr;
            auto lg = std::move(rt.log);
            rt.log = [](const std::string&) {};
            const std::vector<uint32_t> heard = cuesOf(from, to);
            for (uint32_t f = from; f < to; ++f) {
                Frame& g = ring_[f % kRingFrames];
                Input in[kMaxPlayers];
                g.predicted = !realFull(f, in);
                if (g.predicted)
                    predictFull(f, in);
                if (f != from)
                    g.state.save(rt);
                g.used[0] = in[0], g.used[1] = in[1];
                apply(rt, in, f);
                g.cues.clear();
                rt.cueLog = &g.cues;
                rt.frame();
                rt.cueLog = nullptr;
                g.hash = gameHash(rt);
                ++resimulated_;
            }
            rt.audio = audio;
            rt.log = std::move(lg);
            playLate(rt, heard, cuesOf(from, to));
            continue;
        }
        frameHashAll(confirmed_, fr.hash);
        ++confirmed_;
    }
}

void NetGame::beginJoinSeat(game::Runtime& rt, int s)
{
    // a free seat's PC joins the running game at F (every frame before it confirmed): the start settings and the log,
    // its replay, the states compared at F; its inputs count from the next frame not yet relayed
    const uint32_t F = frame_;
    history_.resize(F);
    std::vector<uint8_t> lg = encodeLog(F);
    Settings js = settings_;
    js.joinFrame = F;
    js.logBytes = uint32_t(lg.size());
    js.delay = session_.delay();
    js.seat = uint8_t(s);
    Session& ss = seat(s);
    js.seat2 = uint8_t(pair_[s]);
    ss.setLog(std::move(lg));
    ss.release(js);
    std::vector<std::array<Input, 2>> sent;
    for (uint32_t f = F; f < relayNext_; ++f)
        sent.push_back({relay_[f % kRelayRing].in[0], relay_[f % kRelayRing].in[1]});
    ss.seedFull(F, sent);
    seatFrom_[s] = relayNext_;
    seatJoinHash_[s] = bootHash(rt);
    ss.setReady(seatJoinHash_[s]);
    seatState_[s] = Seat::Joining;
    if (log) log("player " + std::to_string(s + 1) + " is joining at frame " + std::to_string(F) + " (log " + std::to_string(js.logBytes) + " bytes)");
}

NetGame::Step NetGame::stepMulti(game::Runtime& rt, const std::function<Input()>& sample, int waitMs)
{
    uint32_t df = 0, dm = 0, dt = 0;
    if (anyDesync(df, dm, dt)) {
        if (!reportedDesync_ && log) {
            char b[160];
            std::snprintf(b, sizeof b, "NET: desync at frame %u: this PC's state hash %08x, the other's %08x", df, dm, dt);
            log(b);
        }
        reportedDesync_ = true;
        status = "Out of sync with another PC at frame " + std::to_string(df) + ": the game stopped (Esc)";
        return Step::Stop;
    }
    const bool host = session_.isHost();
    pumpAll(0);
    if (host) {
        // the seats: a lost PC's seat is free again (it can join again); a new PC in a free seat joins the running game
        bool joining = false;
        for (int s = 1; s <= 3; ++s) {
            Session& ss = seat(s);
            if (seatState_[s] == Seat::Empty) {
                if (owner_[s] >= 0) continue;   // (another PC's second seat)
                Settings unused;
                std::string e;
                if (ss.handshake(unused, e, 0) < 0) renewSeat(s);
                else if (ss.peerConnected() && !holdJoin_) {
                    takePair(s);   // (a PC asking for two seats: its second, when one is free)
                    holdJoin_ = s;
                }
            } else if (seatState_[s] == Seat::Joining) {
                uint64_t h = 0;
                if (ss.peerReady(h)) {
                    if (h == seatJoinHash_[s]) {
                        seatState_[s] = Seat::Active;
                        if (log) log("player " + std::to_string(s + 1) + " joined the running game at frame " + std::to_string(frame_));
                        if (dropPcs_ > 0) {   // a PC back while the game waits for the PCs that left: one fewer to wait for
                            const uint8_t mine = uint8_t(1u << s | (pair_[s] ? 1u << pair_[s] : 0));
                            dropHold_ = uint8_t(dropHold_ & mine ? dropHold_ & ~mine : dropHold_ & (dropHold_ - 1));
                            if (--dropPcs_ == 0) dropHold_ = 0;
                        }
                    } else {
                        if (log) log("player " + std::to_string(s + 1) + "'s game came out different: not joined");
                        renewSeat(s);
                    }
                } else if (ss.silentSeconds() > 10.0 || ss.rejoinAsked()) {
                    if (log) log("player " + std::to_string(s + 1) + "'s PC went away while joining");
                    renewSeat(s);
                } else {
                    joining = true;
                }
            } else {
                uint32_t pf; uint64_t ph;
                const bool bye = ss.peerBye(pf, ph);
                if (ss.silentSeconds() > 6.0 || ss.rejoinAsked() || bye) {   // (rejoinAsked: its PC is back)
                    if (log) log("player " + std::to_string(s + 1) + " left (frame " + std::to_string(frame_) + "): the seat is free to join again");
                    // (2026-10-30) a PC that closed / quit or dropped (not one whose run ended): the game waits for the
                    // host's choice (wait for a rejoin / go on without it; dropMode), every PC shows "P<n> left"
                    if (!(bye && pf != kByeLeaving) && dropMode() != 0) {
                        dropHold_ = uint8_t(dropHold_ | 1u << s | (pair_[s] ? 1u << pair_[s] : 0));
                        ++dropPcs_;
                        dropAt_ = nowS();
                        if (log) log("the game waits for player " + std::to_string(s + 1) + " (or the host goes on without them)");
                    }
                    renewSeat(s);
                }
            }
        }
        sendRosters();
        announce(true);
        if (holdJoin_ && !joining) {   // the frames run so far confirmed first (the log holds real inputs only)
            relay();
            reconcileMulti(rt);
            if (confirmed_ >= frame_) {
                beginJoinSeat(rt, holdJoin_);
                holdJoin_ = 0;
                joining = true;
            } else if (waitMs > 0) {
                pumpAll(waitMs);
            }
            if (!joining) {
                status = "Player " + std::to_string(holdJoin_ + 1) + " is joining...";
                return Step::Wait;
            }
        }
        if (joining) {
            if (waitMs > 0) pumpAll(std::min(waitMs, 5));
            status = "A player is joining...";
            return Step::Wait;
        }
        if (dropPcs_ > 0) {   // a PC left: no frame runs (every PC waits) until it is back or the host goes on
            const int mode = dropMode();
            if (mode == 0 || (mode > 0 && nowS() - dropAt_ >= mode)) {
                dropContinue();
            } else {
                syncPause();
                if (waitMs > 0) pumpAll(std::min(waitMs, 5));
                status = dropText();
                return Step::Wait;
            }
        }
        if (args_.soloFps > 0) {   // (testing: headless at a set speed, so a PC can join while it runs)
            static double next = 0;
            const double now = nowS();
            if (next < now - 1.0) next = now;
            if (now < next) std::this_thread::sleep_for(std::chrono::duration<double>(next - now));
            next += 1.0 / args_.soloFps;
        }
    }
    const uint32_t due = frame_ + session_.delay();
    if (int64_t(due) > submitted_) {
        Input s = picked(sample());
        Input own{uint8_t(s.flags), s.x, s.y};   // this PC's seat, and its second one's in the second fields
        if (secondSeat() > 0)
            own.flags2 = s.flags2, own.x2 = s.x2, own.y2 = s.y2;
        if (host) {
            Full& o = own_[due % kRelayRing];
            o = {due, {own, {}}};
            if (hostSeats) {   // computer teammates in the free seats (this PC's own seats stay)
                hostSeats(rt, due, o.in);
                setSeat(o.in, 0, own);
                if (pair_[0]) setSeat(o.in, pair_[0], Input{own.flags2, own.x2, own.y2});
            }
        }
        else session_.submit(due, own);
        submitted_ = due;
    }
    if (host) relay();
    Input in[kMaxPlayers];
    for (int pass = 0; pass < 2; ++pass) {
        if (rollback_ > 0) {
            reconcileMulti(rt);
            if (anyDesync(df, dm, dt))
                return stepMulti(rt, sample, 0);
            Input own;
            if (frame_ - confirmed_ < uint32_t(rollback_) && ownKnown(frame_, own)) {
                Frame& fr = ring_[frame_ % kRingFrames];
                fr.predicted = !realFull(frame_, in);
                if (fr.predicted) {
                    predictFull(frame_, in);
                    ++predicted_;
                }
                fr.state.save(rt);
                fr.cues.clear();
                rt.cueLog = &fr.cues;
                fr.used[0] = in[0], fr.used[1] = in[1];
                waitStart_ = -1;
                status.clear();
                apply(rt, in, frame_);
                return Step::Run;
            }
        } else if (realFull(frame_, in)) {
            waitStart_ = -1;
            status.clear();
            apply(rt, in, frame_);
            return Step::Run;
        }
        if (pass == 0 && waitMs > 0) {
            pumpAll(waitMs);
            if (host) relay();
        } else {
            break;
        }
    }
    if (!host) announce(true);
    if (waitStart_ < 0) {
        waitStart_ = nowS();
        ++waits_;
    }
    const double waited = nowS() - waitStart_;
    uint32_t pf;
    uint64_t ph;
    if (!host && session_.peerBye(pf, ph)) {
        status = "The host ended the game (Esc: quit)";
        return Step::Stop;
    }
    const double silent = host ? 0.0 : session_.silentSeconds();
    status = silent > 20.0 ? "The host seems gone (no signal for " + std::to_string(int(silent)) + " s). Esc: quit"
             : silent > 3.0 ? "Connection to the host lost for " + std::to_string(int(silent)) + " s: waiting (Esc: quit)"
             : !pausedText().empty() ? pausedText()
             : waited > 0.3 ? "Waiting for the other players..."
                            : "";
    return Step::Wait;
}

std::string NetGame::finish(game::Runtime& rt, int lingerMs)
{
    if (left_)   // (this PC left mid-game: nothing to compare)
        return "NET: result=left frames=" + std::to_string(frame_);
    finished_ = true;
    settle(rt, lingerMs);   // rollback: the last frames confirmed first (a late correction may still change them)
    const uint64_t mine = gameHash(rt);
    if (multi_ && session_.isHost()) {   // up to four PCs: every seat's PC's end compared with this one
        int live = 0;
        for (int k = 1; k <= 3; ++k)
            if (seatLive(k)) seat(k).bye(frame_, mine), ++live;
        const double t0 = nowS();
        double gotAt = -1;
        while (nowS() - t0 < lingerMs / 1000.0) {
            pumpAll(5);
            bool all = true;
            for (int k = 1; k <= 3; ++k) {
                uint32_t pf; uint64_t ph;
                if (seatLive(k) && !seat(k).peerBye(pf, ph)) all = false;
            }
            if (all && gotAt < 0) gotAt = nowS();
            if (gotAt >= 0 && nowS() - gotAt > 0.3) break;
        }
        uint32_t df = 0, dm, dt;
        const bool desync = anyDesync(df, dm, dt);
        std::string result = desync ? "desync" : "ok", peers;
        uint32_t compared = 0;
        for (int k = 1; k <= 3; ++k) {
            if (!seatLive(k)) continue;
            uint32_t pf = 0; uint64_t ph = 0;
            const bool got = seat(k).peerBye(pf, ph);
            if (!desync && result == "ok") result = !got ? "nobye" : pf == frame_ && ph == mine ? "ok" : "mismatch";
            char b[96];
            std::snprintf(b, sizeof b, " p%d_frames=%u p%d_hash=%016llx", k + 1, pf, k + 1, (unsigned long long)ph);
            peers += b;
            compared += seat(k).hashesCompared;
        }
        char b[512];
        std::snprintf(b, sizeof b, "NET: result=%s player=1 pcs=%d frames=%u hash=%016llx%s desync_frame=%d rollback=%d predicted=%llu rollbacks=%llu resimulated=%llu late_sounds=%llu waits=%llu hashes_compared=%u",
                      result.c_str(), live + 1, frame_, (unsigned long long)mine, peers.c_str(), desync ? int(df) : -1, rollback_,
                      (unsigned long long)predicted_, (unsigned long long)rollbacks_, (unsigned long long)resimulated_,
                      (unsigned long long)lateCues_,
                      (unsigned long long)waits_, compared);
        return b;
    }
    session_.bye(frame_, mine);
    const double t0 = nowS();
    uint32_t pf = 0;
    uint64_t ph = 0;
    double gotAt = -1;
    while (nowS() - t0 < lingerMs / 1000.0) {
        session_.pump(5);
        if (gotAt < 0 && session_.peerBye(pf, ph))
            gotAt = nowS();
        if (gotAt >= 0 && nowS() - gotAt > 0.3)   // a little longer: our BYE reaches the peer too
            break;
    }
    const bool got = session_.peerBye(pf, ph);
    const char* result = session_.desync ? "desync" : !got ? "nobye" : pf == frame_ && ph == mine ? "ok" : "mismatch";
    char b[768];
    std::snprintf(b, sizeof b, "NET: result=%s player=%d frames=%u hash=%016llx peer_frames=%u peer_hash=%016llx desync_frame=%d rollback=%d predicted=%llu rollbacks=%llu resimulated=%llu late_sounds=%llu waits=%llu %s",
                  result, localPlayer() + 1, frame_, (unsigned long long)mine, pf, (unsigned long long)ph,
                  session_.desync ? int(session_.desyncFrame) : -1, rollback_, (unsigned long long)predicted_,
                  (unsigned long long)rollbacks_, (unsigned long long)resimulated_, (unsigned long long)lateCues_, (unsigned long long)waits_,
                  session_.counters().c_str());
    return b;
}

} // namespace hotd::net
