#include "net/session.h"

#include "game/lives.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace hotd::net {

namespace {
constexpr uint8_t kMagic[4] = {'H', 'N', 'P', '1'};
enum Type : uint8_t {
    kHello = 1, kHelloAck = 2, kRefuse = 9,                                   // plain
    kConfirm = 16, kSettings = 17, kReady = 18, kInput = 19, kBye = 20,       // sealed
    kWelcome = 21, kPing = 22, kPong = 23, kLog = 24, kLogAck = 25, kRoster = 26,
};
// 5-8 (protocol 19, "Same game data"): other arcade ROMs; the host's game reads the 1998 PC files and the joiner has
// none; the joiner has them and the host has none; both have them but they differ
enum Refuse : uint8_t { kWrongCode = 1, kBusy = 2, kVersion = 3, kLocked = 4, kRomData = 5, kPcHostOnly = 6, kPcJoinerOnly = 7,
                        kPcDiffer = 8 };
// the refusal's sentence for the joiner (forJoiner) or the host's log
std::string dataRefusal(uint8_t why, bool forJoiner)
{
    switch (why) {
    case kRomData:
        return forJoiner ? "The host's arcade ROMs are different from this PC's: both PCs need the same ROM set (MAME's hotd)."
                         : "its arcade ROMs are different from this PC's (both PCs need the same ROM set)";
    case kPcHostOnly:
        return forJoiner ? "The host uses the 1998 PC files for PC Mode / Boss Mode; this PC doesn't have them. Choose the PC "
                           "game folder in the launcher, or ask the host for an Arcade game."
                         : "this game uses the 1998 PC files (PC Mode / Boss Mode) and that PC doesn't have them";
    case kPcJoinerOnly:
        return forJoiner ? "This PC uses the 1998 PC files for PC Mode / Boss Mode; the host doesn't have them. The host can "
                           "choose its PC game folder in the launcher, or host an Arcade game."
                         : "that PC uses the 1998 PC files for PC Mode / Boss Mode and this PC doesn't have them";
    default:
        return forJoiner ? "The host's 1998 PC files are different from this PC's (another THOTD.EXE or CAMALL.RBH): PC Mode / "
                           "Boss Mode need the same PC files on both PCs."
                         : "its 1998 PC files are different from this PC's (another THOTD.EXE or CAMALL.RBH)";
    }
}
constexpr int kMaxHellosPerSecond = 16;   // new handshakes a host starts per second (each costs a key exchange)
constexpr size_t kRosterSeat = 1 + 2 + kMaxName + 1;   // (protocol 16: + the PC Mode character)
constexpr size_t kHelloSize = 120;              // padded: never smaller than the answer (no amplification)
constexpr size_t kAckSize = 4 + 1 + 2 + 16 + crypto::kPubSize + 16;   // magic, type, version, host nonce, host key, client nonce
constexpr size_t kHeader = 4 + 1 + 8;           // magic, type, sequence number (the AEAD's associated data)
constexpr size_t kMaxHashes = 16;   // (inputs per packet: Session::maxInputs)
constexpr uint32_t kDirC2S = 1, kDirH2C = 2;
constexpr char kChannel[] = "HOTD ReWrithe netplay v10";   // CPace's channel label, the transcript's first field
constexpr int kMaxFailures = 20;
constexpr double kRejoinSilentS = 1.0;   // a seat's PC silent this long + a new HELLO from its address: it is back
enum Drop { kMalformed, kUnauth, kReplay, kStranger, kWindow, kRefused };

void put16(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); }
void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i))); }
void put64(std::vector<uint8_t>& v, uint64_t x) { for (int i = 0; i < 8; ++i) v.push_back(uint8_t(x >> (8 * i))); }
void putF(std::vector<uint8_t>& v, float f) { uint32_t x; std::memcpy(&x, &f, 4); put32(v, x); }
uint16_t get16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t get64(const uint8_t* p) { return uint64_t(get32(p)) | uint64_t(get32(p + 4)) << 32; }
float getF(const uint8_t* p) { const uint32_t x = get32(p); float f; std::memcpy(&f, &x, 4); return f; }

// the AEAD nonce: direction, sequence number, zeros (each direction has its own key too)
void nonceOf(uint32_t dir, uint64_t seq, uint8_t n[crypto::kNonceSize])
{
    for (int i = 0; i < 4; ++i) n[i] = uint8_t(dir >> (8 * i));
    for (int i = 0; i < 8; ++i) n[4 + i] = uint8_t(seq >> (8 * i));
    for (size_t i = 12; i < crypto::kNonceSize; ++i) n[i] = 0;
}
} // namespace

std::string normaliseCode(const std::string& code)
{
    std::string s;
    for (char c : code)
        if (std::isalnum(static_cast<unsigned char>(c)))
            s += char(std::toupper(static_cast<unsigned char>(c)));
        else if (c != '-' && c != ' ')
            return "";
    return s.size() >= 4 && s.size() <= 32 ? s : "";
}

std::string randomCode()
{
    // 40 random bits as 8 Crockford base32 symbols: the join code carries them whole (lobby.h makeJoinCode)
    static const char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    uint8_t r[5] = {};
    if (!crypto::random(r, sizeof r))
        return "";
    uint64_t v = 0;
    for (uint8_t b : r)
        v = v << 8 | b;
    std::string s;
    for (int i = 7; i >= 0; --i)
        s += kAlphabet[(v >> (5 * i)) & 31];
    return s;
}

std::string sanitiseName(const std::string& name)
{
    std::string s;
    for (char c : name)
        if (s.size() < kMaxName && c >= 32 && c < 127)
            s += c;
    while (!s.empty() && s.back() == ' ') s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}

std::string reachHint(uint16_t port)
{
    return "Is the host hosting, and the code right? On the host PC allow hotd.exe in Windows "
           "Firewall. Over the internet the host's router must forward UDP " + std::to_string(port) + " (or have UPnP on).";
}

std::vector<uint8_t> encodeRoster(const RosterSeat r[4])
{
    std::vector<uint8_t> v;
    for (int s = 0; s < 4; ++s) {
        // (protocol 15: a PC's second seat: the seat of its first + 1 in the high nibble; 0 otherwise, as before)
        v.push_back(uint8_t((r[s].in ? 1 : 0) | (r[s].in && r[s].with >= 0 && r[s].with < 4 && r[s].with != s ? (r[s].with + 1) << 4 : 0)));
        put16(v, r[s].pingMs);
        const std::string n = sanitiseName(r[s].name);
        for (size_t i = 0; i < kMaxName; ++i)
            v.push_back(i < n.size() ? uint8_t(n[i]) : 0);
        v.push_back(r[s].pcChar >= 0 && r[s].pcChar <= kPmCharMax ? uint8_t(r[s].pcChar) : 0xFF);   // (protocol 16; 20: to 19)
    }
    // the host's Boss Mode (protocol 18); 0x10 + arena: its Survival game (protocol 22)
    v.push_back(r[0].survival >= 1 && r[0].survival <= 16 ? uint8_t(0x10 + r[0].survival - 1)
                : r[0].boss >= 2 && r[0].boss <= 6 ? uint8_t(r[0].boss) : 0);
    return v;
}

bool decodeRoster(const uint8_t* p, size_t n, RosterSeat out[4])
{
    if (n != 4 * kRosterSeat + 1)
        return false;
    const uint8_t boss = p[4 * kRosterSeat];   // (protocol 18: 0, or 2-6; protocol 22: 0x10-0x1F Survival)
    if (boss == 1 || (boss > 6 && boss < 0x10) || boss > 0x1F)
        return false;
    RosterSeat t[4];
    for (int s = 0; s < 4; ++s) {
        const uint8_t* q = p + s * kRosterSeat;
        const int with = (q[0] >> 4) - 1;
        if ((q[0] & 0x0F) > 1 || with > 3 || with == s || (with >= 0 && !(q[0] & 1)))
            return false;
        t[s].in = (q[0] & 1) != 0;
        t[s].with = with;
        t[s].pingMs = get16(q + 1);
        size_t len = 0;
        while (len < kMaxName && q[3 + len] != 0) ++len;
        for (size_t i = len; i < kMaxName; ++i)   // NUL padded: nothing after the end
            if (q[3 + i] != 0) return false;
        t[s].name = sanitiseName(std::string(reinterpret_cast<const char*>(q + 3), len));
        const uint8_t ch = q[3 + kMaxName];
        if (ch > kPmCharMax && ch != 0xFF)
            return false;
        t[s].pcChar = ch == 0xFF ? -1 : ch;
    }
    t[0].boss = boss <= 6 ? boss : 0;
    t[0].survival = boss >= 0x10 ? boss - 0x10 + 1 : 0;
    for (int s = 0; s < 4; ++s)
        out[s] = t[s];
    return true;
}

std::vector<uint8_t> encodeSettings(const Settings& s)
{
    std::vector<uint8_t> v;
    put16(v, kProtocol);
    for (uint8_t b : {s.chapter, s.event, s.players, s.models, s.difficulty, s.lives, s.blood, s.sound, s.livesKept, s.cuts,
                      s.defaultRoute, s.continueChapters, s.chapterStart, s.delay})
        v.push_back(b);
    put16(v, s.hashEvery);
    put32(v, s.joinFrame);
    put32(v, s.logBytes);
    const size_t n = std::min<size_t>(s.routes.size(), 32);
    v.push_back(uint8_t(n));
    for (size_t i = 0; i < n; ++i) {
        put16(v, s.routes[i].key);
        v.push_back(s.routes[i].v);
    }
    v.push_back(s.sonic);       // Sonic Mode (after the routes)
    v.push_back(s.gameSpeed);   // rate (protocol 6)
    v.push_back(s.seats);       // 2 / 4 (protocol 7)
    v.push_back(s.pcs);         // 2 / 4, and the receiver's seat (protocol 8)
    v.push_back(uint8_t(s.seat | s.seat2 << 4));   // (protocol 15: the second seat in the high nibble)
    v.push_back(s.autoReload); // the host's Auto Reload for every seat (protocol 11)
    v.push_back(s.gameText);    // the host's Game Text (protocol 12)
    v.push_back(s.boss4p);      // the host's 4P Boss Hit Points / 4P Enemy Toughness (protocol 14)
    v.push_back(s.enemy4p);
    v.push_back(s.pcMode);      // PC Mode (protocol 16): on, each seat's character, Continues, Free-Play
    for (uint8_t c : s.pmChar)
        v.push_back(c);
    v.push_back(s.pmContinues);
    v.push_back(s.pmFreePlay);
    v.push_back(s.bossMode);    // Boss Mode (protocol 18): 0, or 2-6
    v.push_back(s.survival);    // Survival Mode (protocol 22): 0, or the arena + 1; its seed, cap, cut, testing
    put32(v, s.survivalSeed);
    for (uint8_t b : {s.survivalCap, s.survivalCut, s.survivalSmgWave, s.survivalWeaponsWave, s.survivalTest})
        v.push_back(b);
    return v;
}

bool decodeSettings(const uint8_t* p, size_t n, Settings& s)
{
    // protocol 7: 2 version, 14 bytes, 2 hashEvery, 4 joinFrame, 4 logBytes, 1 routes + 3 each, 1 sonic, 1 gameSpeed,
    // 1 seats; protocol 8: 1 pcs, 1 seat; protocol 11: 1 autoReload; protocol 12: 1 gameText; protocol 14: 1 boss4p, 1 enemy4p;
    // protocol 16: 1 pcMode, 4 pmChar, 1 pmContinues, 1 pmFreePlay; protocol 18: 1 bossMode; protocol 22: 1 survival,
    // 4 survivalSeed, 1 survivalCap, 1 survivalCut, 1 survivalSmgWave, 1 survivalWeaponsWave, 1 survivalTest
    if (n < 54 || get16(p) != kProtocol)
        return false;
    const size_t routes = p[26];
    if (routes > 32 || n != 54 + 3 * routes)
        return false;
    Settings t;
    uint8_t* f[] = {&t.chapter, &t.event, &t.players, &t.models, &t.difficulty, &t.lives, &t.blood, &t.sound, &t.livesKept,
                    &t.cuts, &t.defaultRoute, &t.continueChapters, &t.chapterStart, &t.delay};
    for (size_t i = 0; i < 14; ++i)
        *f[i] = p[2 + i];
    t.hashEvery = get16(p + 16);
    t.joinFrame = get32(p + 18);
    t.logBytes = get32(p + 22);
    if (t.chapter > 3 || t.players < 1 || t.players > 4 || t.models > 1 || t.difficulty > 4 || !game::livesCodeValid(t.lives) || t.blood > 5 ||
        t.sound > 2 || t.livesKept > 1 || t.cuts > 1 || t.defaultRoute > 1 || t.continueChapters > 1 || t.chapterStart > 1 ||
        t.delay < 1 || t.delay > 15 || t.hashEvery < 1 || t.hashEvery > 600 || t.joinFrame > kMaxJoinFrames ||
        t.logBytes > kMaxLog || (t.joinFrame > 0) != (t.logBytes > 0))
        return false;
    for (size_t i = 0; i < routes; ++i) {
        const Settings::Route r{get16(p + 27 + 3 * i), p[29 + 3 * i]};
        if (r.key >= 1000 || r.v > 2)
            return false;
        t.routes.push_back(r);
    }
    t.sonic = p[27 + 3 * routes];
    t.gameSpeed = p[28 + 3 * routes];
    t.seats = p[29 + 3 * routes];
    t.pcs = p[30 + 3 * routes];
    t.seat = p[31 + 3 * routes] & 0x0F;
    t.seat2 = p[31 + 3 * routes] >> 4;
    t.autoReload = p[32 + 3 * routes];
    t.gameText = p[33 + 3 * routes];
    t.boss4p = p[34 + 3 * routes];
    t.enemy4p = p[35 + 3 * routes];
    t.pcMode = p[36 + 3 * routes];
    for (size_t k = 0; k < 4; ++k)
        t.pmChar[k] = p[37 + 3 * routes + k];
    t.pmContinues = p[41 + 3 * routes];
    t.pmFreePlay = p[42 + 3 * routes];
    t.bossMode = p[43 + 3 * routes];
    t.survival = p[44 + 3 * routes];
    t.survivalSeed = get32(p + 45 + 3 * routes);
    t.survivalCap = p[49 + 3 * routes];
    t.survivalCut = p[50 + 3 * routes];
    t.survivalSmgWave = p[51 + 3 * routes];
    t.survivalWeaponsWave = p[52 + 3 * routes];
    t.survivalTest = p[53 + 3 * routes];
    // (Survival: an arena 1-16, never with PC Mode / Boss Mode; a cap 1-64)
    if (t.survival > 16 || (t.survival && (t.pcMode || t.bossMode)) || t.survivalCap < 1 || t.survivalCap > 64 ||
        t.survivalCut > 1 || t.survivalTest > 3)
        return false;
    if (t.pcMode > 1 || t.pmContinues > 9 || t.pmFreePlay > 1)
        return false;
    if (t.bossMode == 1 || t.bossMode > 6 || (t.bossMode && t.pcMode))   // (Boss Mode: 2-6, never PC Mode)
        return false;
    // (PC Mode: every seat a character 1-19; an arcade game: all 0, or with Character Select picks every seat's 1-19:
    // protocol 21; never with Boss Mode)
    const bool picks = t.pcMode || t.pmChar[0] != 0;
    if (picks && t.bossMode)
        return false;
    for (uint8_t c : t.pmChar)
        if (picks ? c < 1 || c > kPmCharMax : c != 0)
            return false;
    if (t.sonic > 3 || t.gameSpeed > 1 || (t.seats != 2 && t.seats != 4) || t.players > t.seats || t.autoReload > 1 || t.gameText > 1 ||
        t.boss4p > 200 || t.enemy4p > 200)
        return false;
    if ((t.pcs != 2 && t.pcs != 4) || (t.pcs == 4 && t.seats != 4) || t.seat < 1 || t.seat > (t.pcs == 4 ? 3 : 1))
        return false;
    if (t.seat2 != 0 && (t.pcs != 4 || t.seat2 > 3 || t.seat2 == t.seat))
        return false;
    s = t;
    return true;
}


bool decodeInputLog(const std::vector<uint8_t>& v, std::vector<std::array<Input, 2>>& frames, std::vector<uint32_t>& restarts)
{
    auto g32 = [&v](size_t o) { return uint32_t(v[o]) | uint32_t(v[o + 1]) << 8 | uint32_t(v[o + 2]) << 16 | uint32_t(v[o + 3]) << 24; };
    if (v.size() < 8 || v[0] != 'H' || (v[1] != 'L' && v[1] != '4'))
        return false;
    const size_t seats = v[1] == '4' ? 2 : 1, rec = 2 + 2 * 9 * seats;   // a run: count + two PCs' inputs
    const uint32_t total = g32(2);
    const size_t nr = size_t(v[6] | v[7] << 8);
    if (total == 0 || total > kMaxJoinFrames || v.size() < 8 + nr * 4)   // (bounds the frames' memory: ~160 MB at most)
        return false;
    restarts.clear();
    for (size_t i = 0; i < nr; ++i) {
        const uint32_t r = g32(8 + i * 4);
        if (r > total || (!restarts.empty() && r < restarts.back())) return false;
        restarts.push_back(r);
    }
    frames.clear();
    frames.reserve(std::min<uint32_t>(total, 1u << 16));   // (grows with what the log really holds)
    for (size_t o = 8 + nr * 4; o < v.size();) {
        if (o + rec > v.size()) return false;
        const uint32_t n = uint32_t(v[o] | v[o + 1] << 8);
        std::array<Input, 2> in;
        for (size_t k = 0; k < 2 * seats; ++k) {   // PC k / seats, its seat k % seats
            const size_t q = o + 2 + k * 9;
            Input& pi = in[k / seats];
            uint8_t& fl = k % seats ? pi.flags2 : pi.flags;
            float& fx = k % seats ? pi.x2 : pi.x;
            float& fy = k % seats ? pi.y2 : pi.y;
            fl = v[q];
            const uint32_t x = g32(q + 1), y = g32(q + 5);
            std::memcpy(&fx, &x, 4), std::memcpy(&fy, &y, 4);
            if (!Input::flagsOk(fl) || !std::isfinite(fx) || !std::isfinite(fy) || std::fabs(fx) > 4096.0f || std::fabs(fy) > 4096.0f)
                return false;
        }
        if (n == 0 || frames.size() + n > total) return false;
        frames.insert(frames.end(), n, in);
        o += rec;
    }
    return frames.size() == total;
}
Session::Session() = default;
Session::~Session() = default;

bool Session::start(const Options& o, std::string& err)
{
    opt_ = o;
    opt_.name = sanitiseName(o.name);
    started_ = Clock::now();
    if (!crypto::available()) { err = "online play is not available on this system"; return false; }
    if (opt_.code.empty()) { err = "no session code"; return false; }
    // CPace (docs/netplay.md "Security"): the key exchange's generator is the session code mapped to the curve, so a
    // handshake proves the code without giving an eavesdropper or a fake host anything to test guesses against offline
    ecdh_ = crypto::makeCpace(opt_.code.data(), opt_.code.size(), kChannel);
    if (!ecdh_ || !crypto::random(myNonce_, sizeof myNonce_)) { err = "the key exchange could not start"; return false; }
    Address bind;
    if (opt_.host)
        bind = opt_.addr;
    else
        bind.ip = opt_.addr.ip == 0x7F000001 ? 0x7F000001 : 0;   // a loopback test stays on the loopback
    multi_ = opt_.hub;
    if (hub_ == nullptr && !sock_.open(bind, err))
        return false;
    if (!opt_.host)
        peer_ = opt_.addr;
    simRng_ ^= uint64_t(opt_.host ? 1 + opt_.relaySeat * 7 : 2) * 0x2545F4914F6CDD1Dull;
    return true;
}

bool Session::startShared(const Options& o, Session& hub, std::string& err)
{
    hub_ = &hub;
    Options so = o;
    so.hub = false;
    if (!start(so, err))
        return false;
    multi_ = true;
    hub.setSibling(opt_.relaySeat, this);
    return true;
}

void Session::sendRaw(const Address& to, const uint8_t* p, size_t n)
{
    ++sent;
    if (opt_.simLoss > 0) {
        simRng_ ^= simRng_ << 13, simRng_ ^= simRng_ >> 7, simRng_ ^= simRng_ << 17;
        if (int(simRng_ % 100) < opt_.simLoss)
            return;
    }
    if (opt_.simDelayMs > 0) {
        delayed_.push_back({Clock::now() + std::chrono::milliseconds(opt_.simDelayMs), to, std::vector<uint8_t>(p, p + n)});
        return;
    }
    sock().send(to, p, n);
}

void Session::flushDelayed()
{
    const auto now = Clock::now();
    while (!delayed_.empty() && delayed_.front().due <= now) {
        sock().send(delayed_.front().to, delayed_.front().bytes.data(), delayed_.front().bytes.size());
        delayed_.pop_front();
    }
}

bool Session::sendSealed(uint8_t type, const uint8_t* payload, size_t n)
{
    if (!tx_ || n + kHeader + crypto::kTagSize > kMaxPacket)
        return false;
    uint8_t pkt[kMaxPacket];
    std::memcpy(pkt, kMagic, 4);
    pkt[4] = type;
    const uint64_t seq = ++txSeq_;
    for (int i = 0; i < 8; ++i) pkt[5 + i] = uint8_t(seq >> (8 * i));
    uint8_t nonce[crypto::kNonceSize] = {};
    nonceOf(txDir_, seq, nonce);
    if (!tx_->seal(nonce, pkt, kHeader, payload, n, pkt + kHeader))
        return false;
    const size_t len = kHeader + n + crypto::kTagSize;
    sendRaw(peer_, pkt, len);
    lastSend_ = Clock::now();
    if (opt_.fuzz && fuzzSamples_.size() < 32)
        fuzzSamples_.emplace_back(pkt, pkt + len);
    return true;
}

bool Session::openSealed(crypto::Aead& key, uint32_t dir, const uint8_t* p, size_t n, uint8_t& type, std::vector<uint8_t>& out,
                         bool replayCheck)
{
    if (n < kHeader + crypto::kTagSize + 1 || n > kMaxPacket) { ++dropped[kMalformed]; return false; }
    type = p[4];
    const uint64_t seq = get64(p + 5);
    if (seq == 0) { ++dropped[kMalformed]; return false; }
    if (replayCheck && seq <= rxMax_ && (rxMax_ - seq >= 64 || (rxWindow_ >> (rxMax_ - seq)) & 1)) {
        ++dropped[kReplay];   // (checked again after the authentication: cheap rejection of copies first)
        return false;
    }
    uint8_t nonce[crypto::kNonceSize] = {};
    nonceOf(dir, seq, nonce);
    out.resize(n - kHeader - crypto::kTagSize);
    if (!key.open(nonce, p, kHeader, p + kHeader, n - kHeader, out.data())) { ++dropped[kUnauth]; return false; }
    // the replay window (after the authentication: only the peer can move it)
    if (seq > rxMax_) {
        const uint64_t shift = seq - rxMax_;
        rxWindow_ = shift >= 64 ? 0 : rxWindow_ << shift;
        rxWindow_ |= 1;
        rxMax_ = seq;
    } else {
        rxWindow_ |= uint64_t(1) << (rxMax_ - seq);
    }
    lastRecv_ = Clock::now();
    ++received;
    return true;
}

bool Session::deriveKeys(const uint8_t peerPub[crypto::kPubSize], const uint8_t clientNonce[16], const uint8_t clientPub[crypto::kPubSize],
                         const uint8_t hostNonce[16], const uint8_t hostPub[crypto::kPubSize])
{
    // the transcript (fixed sizes): the channel label, the client's nonce and key, the host's nonce and key
    uint8_t t[sizeof kChannel - 1 + 2 * (16 + crypto::kPubSize)];
    uint8_t* q = t;
    std::memcpy(q, kChannel, sizeof kChannel - 1), q += sizeof kChannel - 1;
    std::memcpy(q, clientNonce, 16), q += 16;
    std::memcpy(q, clientPub, crypto::kPubSize), q += crypto::kPubSize;
    std::memcpy(q, hostNonce, 16), q += 16;
    std::memcpy(q, hostPub, crypto::kPubSize);
    uint8_t keys[64];   // client to host, host to client
    if (!ecdh_->derive(peerPub, t, sizeof t, keys))
        return false;
    pendingRx_ = crypto::makeAead(opt_.host ? keys : keys + 32);
    pendingTx_ = crypto::makeAead(opt_.host ? keys + 32 : keys);
    crypto::wipe(keys, sizeof keys);
    return pendingRx_ && pendingTx_;
}

int Session::handshake(Settings& settings, std::string& err, int waitMs)
{
    hsSettings_ = &settings;
    const auto now = Clock::now();
    if (!opt_.host && !connected_ && now - lastHello_ >= std::chrono::milliseconds(250)) {
        lastHello_ = now;
        if (!gotAck_) {   // HELLO
            uint8_t p[kHelloSize] = {};
            std::memcpy(p, kMagic, 4);
            p[4] = kHello;
            p[5] = uint8_t(kProtocol), p[6] = uint8_t(kProtocol >> 8);
            std::memcpy(p + 7, myNonce_, 16);
            std::memcpy(p + 23, ecdh_->publicKey(), crypto::kPubSize);
            sendRaw(peer_, p, sizeof p);
        } else {          // CONFIRM: only the right session code makes it authentic for the host; this player's name
            std::vector<uint8_t> c = {uint8_t(kProtocol), uint8_t(kProtocol >> 8), 'H', 'O', 'T', 'D', uint8_t(opt_.name.size())};
            c.insert(c.end(), opt_.name.begin(), opt_.name.end());
            // the seats wanted (1 / 2), this player's PC Mode characters (0: the host's default; protocol 16)
            c.push_back(uint8_t(opt_.wantSeats == 2 ? 2 : 1));
            for (int ch : opt_.pcChar)
                c.push_back(uint8_t(ch >= 1 && ch <= kPmCharMax ? ch : 0));
            put64(c, opt_.romData);   // this PC's game data (protocol 19): the host compares it with its own
            put64(c, opt_.pcData);
            sendSealed(kConfirm, c.data(), c.size());
        }
    }
    // the client: no answer at all (a wrong address, the host's firewall, no port forward), or a handshake that never
    // finishes: a clear message instead of "Connecting..." forever
    if (!opt_.host && !connected_ && refused_.empty() && opt_.connectTimeoutS > 0 &&
        now - started_ > std::chrono::seconds(opt_.connectTimeoutS)) {
        refused_ = !gotAck_ ? "No answer from " + peer_.str() + ". " + reachHint(peer_.port)
                            : "The host answered but the connection did not finish (packets lost on the way?). Try again.";
    }
    flushDelayed();
    receiveAll(waitMs);
    hsSettings_ = nullptr;
    periodic();
    if (gotSettings_)
        settings = received_;
    if (!refused_.empty()) { err = refused_; return -1; }
    if (opt_.host && !hub_ && failures() >= kMaxFailures) {   // (the socket's owner reports it; its seats refuse quietly)
        err = "Too many wrong codes were tried: nobody else can join now. Stop hosting and host again (you get a new code).";
        return -1;
    }
    return connected_ && (opt_.host || (gotSettings_ && logComplete())) ? 1 : 0;
}

void Session::initLockstep()
{
    // the lockstep's start: frames base .. base+delay-1 neutral for both players
    for (uint32_t f = base_; f < base_ + delay_; ++f) {
        local_[f % kRing] = {f, {}};
        remote_[f % kRing] = {f, {}};
    }
    localLast_ = localAcked_ = remoteContig_ = int64_t(base_) + delay_ - 1;
    lastRun_ = base_;
}

void Session::release(const Settings& s)
{
    if (!opt_.host || !settingsBlob_.empty())
        return;
    settingsBlob_ = encodeSettings(s);
    setSeats(s.seats);
    delay_ = s.delay;
    hashEvery_ = s.hashEvery;
    if (s.joinFrame > 0) {   // a game already running: the host's own inputs are in; the joiner's first ones neutral
        base_ = s.joinFrame;
        for (uint32_t f = base_; f < base_ + delay_; ++f)
            remote_[f % kRing] = {f, {}};
        remoteContig_ = std::max<int64_t>(remoteContig_, int64_t(base_) + delay_ - 1);
        localAcked_ = int64_t(base_) - 1;
        lastRun_ = base_;
        peerReady_ = false;
    } else {
        initLockstep();
    }
    if (connected_)
        sendSealed(kSettings, settingsBlob_.data(), settingsBlob_.size());
}

void Session::startAt(uint32_t base)
{
    // the joiner, after its replay: frames base .. base+delay-1 are its neutral ones; the host's come in packets
    base_ = base;
    for (uint32_t f = base_; f < base_ + delay_; ++f)
        local_[f % kRing] = {f, {}};
    localLast_ = localAcked_ = int64_t(base_) + delay_ - 1;
    remoteContig_ = int64_t(base_) - 1;
    lastRun_ = base_;
}

void Session::fillRemote(uint32_t frame)
{
    remote_[frame % kRing] = {frame, {}};
    while (remote_[uint32_t(remoteContig_ + 1) % kRing].frame == uint32_t(remoteContig_ + 1))
        ++remoteContig_;
    lastRun_ = std::max(lastRun_, frame);
}

void Session::seedLocal(uint32_t base, const std::vector<Input>& in)
{
    for (size_t i = 0; i < in.size() && i < kRing / 2; ++i)
        local_[(base + uint32_t(i)) % kRing] = {base + uint32_t(i), in[i]};
    localLast_ = int64_t(base) + int64_t(std::min<size_t>(in.size(), kRing / 2)) - 1;
    localAcked_ = int64_t(base) - 1;
    remoteContig_ = int64_t(base) - 1;
    lastRun_ = base;
}

void Session::sendLog()
{
    // host: the log in 1 KB chunks from the first one not acknowledged, up to 32 every 20 ms (the joiner acknowledges
    // what it has in order)
    const auto now = Clock::now();
    if (!opt_.host || log_.empty() || logAcked_ >= log_.size() || now - lastLog_ < std::chrono::milliseconds(20))
        return;
    lastLog_ = now;
    for (uint32_t off = logAcked_, k = 0; off < log_.size() && k < 32; off += 1024, ++k) {
        std::vector<uint8_t> v;
        put32(v, off);
        const size_t n = std::min<size_t>(1024, log_.size() - off);
        v.insert(v.end(), log_.begin() + off, log_.begin() + off + n);
        sendSealed(kLog, v.data(), v.size());
    }
}

void Session::periodic()
{
    if (connected_ && opt_.host && released())
        sendLog();
    const auto now = Clock::now();
    if (!opt_.host && gotSettings_ && received_.logBytes > 0 && now - lastLog_ >= std::chrono::milliseconds(30)) {
        lastLog_ = now;   // the joiner: what it has, in order
        uint32_t have = 0;
        while (have < received_.logBytes && logChunks_[have / 1024]) have = std::min(received_.logBytes, have + 1024);
        uint8_t b[4] = {uint8_t(have), uint8_t(have >> 8), uint8_t(have >> 16), uint8_t(have >> 24)};
        sendSealed(kLogAck, b, 4);
    }
    // PING every 250 ms while connected (the waiting room's and the game's ping; the host's input delay)
    if (!connected_ || now - lastPing_ < std::chrono::milliseconds(250))
        return;
    lastPing_ = now;
    const uint64_t t = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
    uint8_t b[9];
    for (int i = 0; i < 8; ++i) b[i] = uint8_t(t >> (8 * i));
    b[8] = pausedOut_;   // (protocol 13: the seats paused, as this PC knows them)
    sendSealed(kPing, b, 9);
}

void Session::setPausedOut(uint8_t seats)
{
    if (seats == pausedOut_)
        return;
    pausedOut_ = seats;
    lastPing_ = {};   // (told at once: the next pump sends a PING)
}

int Session::suggestedDelay() const
{
    // the frames the input waits, from the measured round trip: one way + 12 ms of slack, at least 2 (LAN: 2,
    // ~60 ms internet: 3, ~120 ms: 5), at most 8; no samples yet: 3
    if (pingSamples_ == 0)
        return 3;
    const double ms = rttMs_ / 2.0 + 12.0;
    return std::clamp(int(std::ceil(ms / (1000.0 / 60.0))), 2, 8);
}

void Session::receiveAll(int waitMs)
{
    if (hub_) {   // a seat's session: the hub receives (and hands each packet to its seat)
        hub_->receiveAll(waitMs);
        return;
    }
    uint8_t buf[2048];
    for (int k = 0; k < 256; ++k) {
        Address from;
        const int n = sock_.recv(buf, sizeof buf, from, k == 0 ? waitMs : 0);
        if (n <= 0)
            return;
        if (opt_.raw && opt_.raw(from, buf, size_t(n)))   // (the join code's hole punching: STUN, punch packets)
            continue;
        if (size_t(n) > kMaxPacket) { ++dropped[kMalformed]; continue; }
        handle(from, buf, size_t(n), hsSettings_);
    }
}

void Session::handle(const Address& from, const uint8_t* p, size_t n, Settings* settings)
{
    if (n < 5 || std::memcmp(p, kMagic, 4) != 0) { ++dropped[kMalformed]; return; }
    const uint8_t type = p[4];
    const auto now = Clock::now();
    if (opt_.hub) {
        // the host of up to four PCs: a packet to the seat whose PC sent it; a new HELLO to a free seat (P2, P3, P4
        // in order), or busy
        for (Session* s : siblings_)
            if (s && s->owns(from)) { s->handle(from, p, n, settings); return; }
        if (type == kHello && n == kHelloSize) {
            if (failures_ >= kMaxFailures) { refuse(from, kLocked); ++dropped[kRefused]; return; }
            for (int pass = 0; pass < 2; ++pass)
                for (Session* s : siblings_)
                    if (s && !s->connected_ && !s->reserved && (pass == 1 || !s->havePending_)) { s->handle(from, p, n, settings); return; }
            for (Session* s : siblings_)   // a seat's own PC back (a new process: another port): no refusal
                if (s && s->rejoinHello(from, p)) return;
            refuse(from, kBusy);
        }
        for (Session* s : siblings_)   // (a joiner refused for its game data, protocol 19: its repeats answered again)
            if (s && s->dataRefusedWhy_ && s->dataRefused_ == from && type == kConfirm) { refuse(from, s->dataRefusedWhy_); break; }
        ++dropped[kStranger];
        return;
    }
    if (opt_.host) {
        if (type == kHello) {
            if (n != kHelloSize) { ++dropped[kMalformed]; return; }
            if (connected_) {   // another one while a game runs (or the client's late copy): busy, rate-limited
                if (rejoinHello(from, p))   // (the peer's own PC joining again: its seat is handed back)
                    return;
                if (from != peer_)
                    refuse(from, kBusy);
                ++dropped[from == peer_ ? kMalformed : kStranger];
                return;
            }
            if (get16(p + 5) != kProtocol) {
                refuse(from, kVersion);
                return;
            }
            if (havePending_ && from == pending_ && std::memcmp(peerNonce_, p + 7, 16) == 0) {
                sendRaw(from, ackPacket_, ackLen_);   // the client's repeat: the same answer
                return;
            }
            // too many wrong passwords (a guesser): no more joins; a flood of new handshakes: each costs a key
            // exchange, so at most kMaxHellosPerSecond a second (the real joiner's repeats get through next second)
            if (failures() >= kMaxFailures) { refuse(from, kLocked); ++dropped[kRefused]; return; }
            if (!helloAllowed()) { ++dropped[kRefused]; return; }
            uint8_t hostNonce[16];
            if (!crypto::random(hostNonce, 16)) return;
            // a fresh CPace key pair for every new handshake (CPace's keys are ephemeral: one per session)
            auto fresh = crypto::makeCpace(opt_.code.data(), opt_.code.size(), kChannel);
            if (!fresh) return;
            ecdh_ = std::move(fresh);
            std::memcpy(peerNonce_, p + 7, 16);
            if (!deriveKeys(p + 23, p + 7, p + 23, hostNonce, ecdh_->publicKey())) { ++dropped[kMalformed]; return; }
            pending_ = from;
            havePending_ = true;
            std::memcpy(ackPacket_, kMagic, 4);
            ackPacket_[4] = kHelloAck;
            ackPacket_[5] = uint8_t(kProtocol), ackPacket_[6] = uint8_t(kProtocol >> 8);
            std::memcpy(ackPacket_ + 7, hostNonce, 16);
            std::memcpy(ackPacket_ + 23, ecdh_->publicKey(), crypto::kPubSize);
            std::memcpy(ackPacket_ + 23 + crypto::kPubSize, p + 7, 16);
            ackLen_ = kAckSize;
            sendRaw(from, ackPacket_, ackLen_);
            log("a player at " + from.str() + " is joining");
            return;
        }
        if (type < kConfirm) { ++dropped[kMalformed]; return; }
        if (connected_ && from != peer_) { ++dropped[kStranger]; return; }
        if (!connected_) {
            if (!havePending_ || from != pending_) {
                if (dataRefusedWhy_ && from == dataRefused_)   // (a refused joiner's repeated CONFIRM: the answer again)
                    refuse(from, dataRefusedWhy_);
                ++dropped[kStranger];
                return;
            }
            uint8_t t = 0;
            std::vector<uint8_t> pl;
            rxMax_ = 0, rxWindow_ = 0;
            // CONFIRM: version, "HOTD", the name's length (at most kMaxName), the name, the seats wanted (1 / 2), the
            // two characters (0-19), the game data's digests (ROMs, PC files: 8 bytes each)
            if (!openSealed(*pendingRx_, kDirC2S, p, n, t, pl, false) || t != kConfirm || pl.size() < 26 ||
                get16(pl.data()) != kProtocol || std::memcmp(pl.data() + 2, "HOTD", 4) != 0 || pl[6] > kMaxName ||
                pl.size() != 26 + size_t(pl[6]) || pl[7 + pl[6]] < 1 || pl[7 + pl[6]] > 2 || pl[8 + pl[6]] > kPmCharMax || pl[9 + pl[6]] > kPmCharMax) {   // (charsel: 1-19 as protocol 20 says; was 6: a joiner with an extra character was refused as a wrong password)
                ++failures();
                refuse(from, kWrongCode);
                log("refused " + from.str() + ": wrong password (" + std::to_string(failures()) + " of " +
                    std::to_string(kMaxFailures) + ")");
                havePending_ = false;
                return;
            }
            peerName_ = sanitiseName(std::string(reinterpret_cast<const char*>(pl.data() + 7), pl[6]));
            // the same game data (protocol 19): other ROMs never (0: unknown on a side, not compared); other PC data
            // when this game reads it (PC Mode / Boss Mode: the camera keys, the motion table). The right code, so a
            // plain refusal: the joiner shows the reason, the host logs it; the joiner's seat stays free
            {
                const uint64_t rom = get64(pl.data() + 10 + pl[6]), pc = get64(pl.data() + 18 + pl[6]);
                const uint8_t why = rom && opt_.romData && rom != opt_.romData ? kRomData
                                    : !opt_.needPcData || pc == opt_.pcData  ? 0
                                    : !pc                                    ? kPcHostOnly
                                    : !opt_.pcData                           ? kPcJoinerOnly
                                                                             : kPcDiffer;
                if (why) {
                    refuse(from, why);
                    dataRefused_ = from, dataRefusedWhy_ = why;
                    log("refused " + from.str() + (peerName_.empty() ? "" : " (" + peerName_ + ")") + ": " + dataRefusal(why, false));
                    havePending_ = false;
                    pendingRx_.reset(), pendingTx_.reset();
                    return;
                }
            }
            peerWants_ = pl[7 + pl[6]];
            peerChar_[0] = pl[8 + pl[6]], peerChar_[1] = pl[9 + pl[6]];
            rx_ = std::move(pendingRx_);
            tx_ = std::move(pendingTx_);
            rxDir_ = kDirC2S, txDir_ = kDirH2C;
            peer_ = from;
            connected_ = true;
            havePending_ = false;
            lastRecv_ = now;
            log("player " + std::to_string(multi_ ? opt_.relaySeat + 1 : 2) + (peerName_.empty() ? "" : " (" + peerName_ + ")") +
                " joined from " + from.str());
            // in the waiting room until the host starts (release): WELCOME now (up to four PCs: with the seat), the
            // settings then
            const uint8_t seat = uint8_t(opt_.relaySeat);
            if (settingsBlob_.empty())
                sendSealed(kWelcome, &seat, 1);   // (0: a two-PC game)
            else
                sendSealed(kSettings, settingsBlob_.data(), settingsBlob_.size());
            return;
        }
    } else {
        if (from != peer_) { ++dropped[kStranger]; return; }
        if (type == kRefuse) {
            if (n != 6) { ++dropped[kMalformed]; return; }
            if (connected_) { ++dropped[kRefused]; return; }   // unauthenticated: ignored once in a game
            refused_ = p[5] == kWrongCode ? "Wrong code: the host refused it. Check the code with the host (or paste it)."
                       : p[5] == kBusy    ? "This game is full (no free seat, or it is running and not open for joining)."
                       : p[5] == kVersion ? "The host has another version of HOTD ReWrithe: both PCs need the same build."
                       : p[5] == kLocked  ? "The host stopped taking players after too many wrong codes: the host has to host again."
                       : p[5] >= kRomData && p[5] <= kPcDiffer ? dataRefusal(p[5], true)
                                          : "The host refused the connection.";
            return;
        }
        if (type == kHelloAck) {
            if (n != kAckSize || gotAck_ || std::memcmp(p + 23 + crypto::kPubSize, myNonce_, 16) != 0) {
                ++dropped[kMalformed];
                return;
            }
            if (get16(p + 5) != kProtocol) {   // (a host of another version that answers anyway)
                refused_ = "The host has another version of HOTD ReWrithe: both PCs need the same build.";
                return;
            }
            if (!deriveKeys(p + 23, myNonce_, ecdh_->publicKey(), p + 7, p + 23)) { ++dropped[kMalformed]; return; }
            rx_ = std::move(pendingRx_);
            tx_ = std::move(pendingTx_);
            rxDir_ = kDirH2C, txDir_ = kDirC2S;
            gotAck_ = true;
            lastHello_ = {};   // the CONFIRM at once
            return;
        }
        if (type < kConfirm || !rx_) { ++dropped[kMalformed]; return; }
    }
    uint8_t t = 0;
    std::vector<uint8_t> pl;
    if (!openSealed(*rx_, rxDir_, p, n, t, pl, true))
        return;
    handlePayload(t, pl, settings);
}

void Session::handlePayload(uint8_t type, const std::vector<uint8_t>& pl, Settings* settings)
{
    const uint8_t* p = pl.data();
    const size_t n = pl.size();
    switch (type) {
    case kConfirm:   // host: the client did not hear WELCOME / the settings yet
        if (opt_.host && connected_ && !peerReady_) {
            const uint8_t seat = uint8_t(opt_.relaySeat);
            if (settingsBlob_.empty())
                sendSealed(kWelcome, &seat, 1);   // (0: a two-PC game)
            else
                sendSealed(kSettings, settingsBlob_.data(), settingsBlob_.size());
        }
        return;
    case kWelcome:   // client: in the host's waiting room
        // one byte: 0 = a two-PC game, 1-3 = this PC's seat (up to four PCs). (Before protocol 9 the two-PC WELCOME was
        // empty, and an empty sealed payload is dropped: two-PC joiners only got in with the settings at Start)
        if (opt_.host || n != 1 || p[0] > 3) { ++dropped[kMalformed]; return; }
        if (p[0] > 0 && !gotSettings_)
            multi_ = true, seat_ = p[0];
        connected_ = true;
        return;
    case kRoster:    // client: each seat's PC, its ping and name (the waiting room)
        if (opt_.host || !decodeRoster(p, n, roster)) { ++dropped[kMalformed]; return; }
        rosterReceived = true;
        return;
    case kPing:
        if (n != 9) { ++dropped[kMalformed]; return; }   // (p[8]: bits 0-3 paused seats; 4-7 (2026-10-30): the host's seats left, the game waiting)
        peerPaused_ = p[8];
        sendSealed(kPong, p, 8);
        return;
    case kPong: {
        if (n != 8) { ++dropped[kMalformed]; return; }
        const uint64_t t = get64(p);
        const uint64_t now = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
        if (t > now || now - t > 10'000'000'000ull) { ++dropped[kMalformed]; return; }   // (only our own times come back)
        const double ms = double(now - t) / 1e6;
        rttMs_ = pingSamples_ == 0 ? ms : rttMs_ * 0.8 + ms * 0.2;
        rttLastMs_ = ms;
        ++pingSamples_;
        return;
    }
    case kSettings: {
        if (opt_.host || gotSettings_) return;
        Settings s;
        if (!decodeSettings(p, n, s)) { ++dropped[kMalformed]; refused_ = "The host's game settings could not be read (another version?)."; return; }
        if (settings) *settings = s;
        received_ = s;
        multi_ = s.pcs == 4;
        seat_ = multi_ ? s.seat : 1;
        pcSeats_ = multi_ && s.seat2 ? 2 : 1;   // (a joiner of up to four PCs with two seats: its inputs 18 bytes)
        setSeats(s.seats);
        delay_ = s.delay;
        hashEvery_ = s.hashEvery;
        if (s.joinFrame == 0)
            initLockstep();
        else {   // a game already running: its log comes next (startAt after the replay)
            log_.assign(s.logBytes, 0);
            logChunks_.assign((s.logBytes + 1023) / 1024, 0);
            logHave_ = 0;
        }
        gotSettings_ = true;
        connected_ = true;
        return;
    }
    case kLog: {   // the joiner: a chunk of the host's input log
        if (opt_.host || !gotSettings_ || received_.logBytes == 0 || n < 5) { ++dropped[kMalformed]; return; }
        const uint32_t off = get32(p);
        const size_t len = n - 4;
        if (off % 1024 != 0 || off >= received_.logBytes || len != std::min<size_t>(1024, received_.logBytes - off)) {
            ++dropped[kMalformed];
            return;
        }
        if (!logChunks_[off / 1024]) {
            std::memcpy(log_.data() + off, p + 4, len);
            logChunks_[off / 1024] = 1;
            logHave_ += uint32_t(len);
        }
        return;
    }
    case kLogAck: {   // the host: what the joiner has
        if (!opt_.host || n != 4) { ++dropped[kMalformed]; return; }
        logAcked_ = std::max(logAcked_, std::min<uint32_t>(get32(p), uint32_t(log_.size())));
        return;
    }
    case kReady:
        if (n != 8) { ++dropped[kMalformed]; return; }
        peerBootHash_ = get64(p);
        peerReady_ = true;
        // answered (so the peer knows we are ready too) while its inputs have not come yet, at most every 50 ms (two
        // answering PCs would otherwise echo READY forever)
        if (readySet_ && remoteContig_ < int64_t(base_) + delay_ && Clock::now() - lastReady_ >= std::chrono::milliseconds(50))
            setReady(bootHash_);
        return;
    case kBye:
        if (n != 12) { ++dropped[kMalformed]; return; }
        peerByeFrames_ = get32(p);
        peerByeHash_ = get64(p + 4);
        peerBye_ = true;
        return;
    case kInput: {
        if (n < 11) { ++dropped[kMalformed]; return; }
        const uint8_t player = p[0];
        const int32_t ack = int32_t(get32(p + 1));
        const uint32_t first = get32(p + 5);
        const size_t count = p[9];
        const size_t stride = recvStride();   // one PC's input: 9 bytes a seat (the host's relayed frames: four seats)
        const uint8_t from = multi_ ? uint8_t(opt_.host ? opt_.relaySeat : 0) : uint8_t(1 - localPlayer());
        if (player != from || count > 64 || ack < -1 || n < 10 + count * stride + 1) {
            ++dropped[kMalformed];
            return;
        }
        const size_t hcount = p[10 + count * stride];
        if (hcount > kMaxHashes || n != 11 + count * stride + hcount * 8) { ++dropped[kMalformed]; return; }
        if (uint64_t(first) + count > 0xFFFFFFF0ull) { ++dropped[kMalformed]; return; }
        // validate everything before using any of it
        for (size_t i = 0; i < count * (stride / 9); ++i) {
            const uint8_t* q = p + 10 + i * 9;
            const float x = getF(q + 1), y = getF(q + 5);
            if (!Input::flagsOk(q[0]) || !std::isfinite(x) || !std::isfinite(y) || std::fabs(x) > 4096.0f ||
                std::fabs(y) > 4096.0f) {
                ++dropped[kMalformed];
                return;
            }
        }
        peerReady_ = true;   // (an input means the peer started)
        localAcked_ = std::max<int64_t>(localAcked_, std::min<int64_t>(ack, localLast_));
        const uint64_t limit = uint64_t(lastRun_) + kRing / 2;
        for (size_t i = 0; i < count; ++i) {
            const uint32_t f = first + uint32_t(i);
            if (int64_t(f) <= remoteContig_) continue;
            if (f > limit) { ++dropped[kWindow]; break; }
            const uint8_t* q = p + 10 + i * stride;
            Input in{q[0], getF(q + 1), getF(q + 5)}, in2;
            if (stride >= 18)
                in.flags2 = q[9], in.x2 = getF(q + 10), in.y2 = getF(q + 14);
            if (stride == 36) {   // a relayed full frame: the second PC's pair
                in2 = Input{q[18], getF(q + 19), getF(q + 23)};
                in2.flags2 = q[27], in2.x2 = getF(q + 28), in2.y2 = getF(q + 32);
            }
            remote_[f % kRing] = {f, in, in2};
        }
        while (remote_[uint32_t(remoteContig_ + 1) % kRing].frame == uint32_t(remoteContig_ + 1))
            ++remoteContig_;
        const uint8_t* h = p + 11 + count * stride;
        for (size_t i = 0; i < hcount; ++i) {
            const uint32_t f = get32(h + i * 8), v = get32(h + i * 8 + 4);
            if (f > limit || uint64_t(f) + kRing / 2 < lastRun_ || peerHash_[f % kRing].frame == f) continue;
            peerHash_[f % kRing] = {f, v};
            if (myHash_[f % kRing].frame == f) {
                ++hashesCompared;
                if (myHash_[f % kRing].h != v && (!desync || f < desyncFrame)) {
                    desync = true;
                    desyncFrame = f, desyncLocal = myHash_[f % kRing].h, desyncPeer = v;
                }
            }
        }
        return;
    }
    default:
        ++dropped[kMalformed];
    }
}

void Session::setReady(uint64_t bootHash)
{
    bootHash_ = bootHash;
    readySet_ = true;
    lastReady_ = Clock::now();
    uint8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = uint8_t(bootHash >> (8 * i));
    sendSealed(kReady, b, 8);
}

int Session::peerReady(uint64_t& peerHash)
{
    peerHash = peerBootHash_;
    return peerReady_ && peerBootHash_ != 0 ? 1 : 0;
}

void Session::submit(uint32_t frame, const Input& in)
{
    if (int64_t(frame) != localLast_ + 1)
        return;   // (the glue submits each frame once, in order)
    local_[frame % kRing] = {frame, in};
    localLast_ = frame;
    sendInputs();
}

void Session::submitFull(uint32_t frame, const Input full[2])
{
    if (int64_t(frame) != localLast_ + 1)
        return;
    local_[frame % kRing] = {frame, full[0], full[1]};
    localLast_ = frame;
    sendInputs();
}

bool Session::remoteFull(uint32_t frame, Input out[2]) const
{
    const Slot& r = remote_[frame % kRing];
    if (r.frame != frame)
        return false;
    out[0] = r.in, out[1] = r.in2;
    return true;
}

void Session::seedFull(uint32_t base, const std::vector<std::array<Input, 2>>& frames)
{
    // a seat's PC joining at base: the relayed frames from there (more follow with submitFull)
    for (size_t i = 0; i < frames.size() && i < kRing / 2; ++i)
        local_[(base + uint32_t(i)) % kRing] = {base + uint32_t(i), frames[i][0], frames[i][1]};
    localLast_ = int64_t(base) + int64_t(std::min<size_t>(frames.size(), kRing / 2)) - 1;
    localAcked_ = int64_t(base) - 1;
}

void Session::sendRoster(const RosterSeat r[4])
{
    if (!connected_ || !tx_)
        return;
    const std::vector<uint8_t> b = encodeRoster(r);
    sendSealed(kRoster, b.data(), b.size());
}

void Session::refuse(const Address& to, uint8_t why)
{
    // unauthenticated and 6 bytes (smaller than any HELLO: no amplification); at most 5 a second (a wrong password's
    // answer always: one per handshake anyway)
    Session& o = hub_ ? *hub_ : *this;
    const auto now = Clock::now();
    if (why != kWrongCode && now - o.lastRefuse_ < std::chrono::milliseconds(200))
        return;
    o.lastRefuse_ = now;
    const uint8_t r[6] = {kMagic[0], kMagic[1], kMagic[2], kMagic[3], kRefuse, why};
    sendRaw(to, r, sizeof r);
}

bool Session::helloAllowed()
{
    Session& o = hub_ ? *hub_ : *this;
    const auto now = Clock::now();
    if (now - o.helloWindow_ >= std::chrono::seconds(1))
        o.helloWindow_ = now, o.hellos_ = 0;
    if (o.hellos_ >= kMaxHellosPerSecond)
        return false;
    ++o.hellos_;
    return true;
}

bool Session::localInput(uint32_t frame, Input& in)
{
    lastRun_ = std::max(lastRun_, frame);
    if (local_[frame % kRing].frame != frame)
        return false;
    in = local_[frame % kRing].in;
    return true;
}

bool Session::remoteInput(uint32_t frame, Input& in) const
{
    if (remote_[frame % kRing].frame != frame)
        return false;
    in = remote_[frame % kRing].in;
    return true;
}

bool Session::inputs(uint32_t frame, Input out[kMaxPlayers])
{
    lastRun_ = std::max(lastRun_, frame);
    const Slot& l = local_[frame % kRing];
    const Slot& r = remote_[frame % kRing];
    if (l.frame != frame || r.frame != frame)
        return false;
    out[localPlayer()] = l.in;
    out[1 - localPlayer()] = r.in;
    return true;
}

void Session::frameHash(uint32_t frame, uint64_t h64)
{
    if (frame % hashEvery_ != 0)
        return;
    const uint32_t h = uint32_t(h64 ^ (h64 >> 32));
    myHash_[frame % kRing] = {frame, h};
    recentHashes_.push_back({frame, h});
    while (recentHashes_.size() > kMaxHashes)
        recentHashes_.pop_front();
    if (peerHash_[frame % kRing].frame == frame) {
        ++hashesCompared;
        if (peerHash_[frame % kRing].h != h && (!desync || frame < desyncFrame)) {
            desync = true;
            desyncFrame = frame, desyncLocal = h, desyncPeer = peerHash_[frame % kRing].h;
        }
    }
}

void Session::sendInputs()
{
    if (!connected_ || !tx_)
        return;
    std::vector<uint8_t> v;
    v.reserve(800);
    v.push_back(uint8_t(multi_ ? (opt_.host ? 0 : seat_) : localPlayer()));
    put32(v, uint32_t(int32_t(remoteContig_)));
    const size_t stride = sendStride();
    const int64_t from = std::max(localAcked_ + 1, localLast_ - int64_t(maxInputs()) + 1);
    const size_t count = localLast_ >= from ? size_t(localLast_ - from + 1) : 0;
    put32(v, uint32_t(from < 0 ? 0 : from));
    v.push_back(uint8_t(count));
    for (size_t i = 0; i < count; ++i) {
        const Slot& sl = local_[uint32_t(from + int64_t(i)) % kRing];
        for (const Input* in : {&sl.in, &sl.in2}) {
            v.push_back(in->flags);
            putF(v, in->x);
            putF(v, in->y);
            if (stride >= 18) {
                v.push_back(in->flags2);
                putF(v, in->x2);
                putF(v, in->y2);
            }
            if (stride != 36)
                break;
        }
    }
    v.push_back(uint8_t(recentHashes_.size()));
    for (const HashSlot& h : recentHashes_) {
        put32(v, h.frame);
        put32(v, h.h);
    }
    sendSealed(kInput, v.data(), v.size());
}

void Session::bye(uint32_t frames, uint64_t hash)
{
    byeSet_ = true;
    byeFrames_ = frames;
    byeHash_ = hash;
    byeSends_ = 0;
}

bool Session::peerBye(uint32_t& frames, uint64_t& hash) const
{
    frames = peerByeFrames_;
    hash = peerByeHash_;
    return peerBye_;
}

void Session::fuzz()
{
    // testing (--net-fuzz): malformed packets of every kind to the peer, a few per pump, interleaved with the game's
    uint8_t b[1600];
    for (int k = 0; k < 8 && opt_.fuzz > 0; ++k, --opt_.fuzz) {
        simRng_ ^= simRng_ << 13, simRng_ ^= simRng_ >> 7, simRng_ ^= simRng_ << 17;
        const uint64_t r = simRng_;
        for (size_t i = 0; i < sizeof b; ++i) b[i] = uint8_t((r >> (i % 56)) * 31 + i * 7);
        size_t n = size_t(r >> 8) % 1500;
        const int kind = int(opt_.fuzz % 8);
        if (kind >= 1) std::memcpy(b, kMagic, 4);
        if (kind == 1) b[4] = uint8_t(r >> 20);
        if (kind == 2) { b[4] = kHello; n = kHelloSize; }
        if (kind == 6) { b[4] = kInput; n = 13 + 40 + 16; }
        if (kind == 7) { b[4] = kInput; n = 1500; }
        if ((kind == 3 || kind == 4 || kind == 5) && !fuzzSamples_.empty()) {
            const std::vector<uint8_t>& s = fuzzSamples_[size_t(r >> 32) % fuzzSamples_.size()];
            n = s.size();
            std::memcpy(b, s.data(), n);
            if (kind == 4) b[size_t(r >> 40) % n] ^= uint8_t(1u << ((r >> 50) % 8));   // one bit flipped
            if (kind == 5) n = size_t(r >> 40) % n;                                     // cut short
        }
        sock().send(peer_, b, n);   // (past the simulated network)
    }
}

void Session::pump(int waitMs)
{
    flushDelayed();
    receiveAll(waitMs);
    flushDelayed();
    if (!connected_)
        return;
    const auto now = Clock::now();
    if (opt_.host && !peerReady_ && !settingsBlob_.empty() && now - lastSettings_ >= std::chrono::milliseconds(200)) {
        lastSettings_ = now;
        sendSealed(kSettings, settingsBlob_.data(), settingsBlob_.size());
    }
    periodic();
    if (readySet_ && remoteContig_ < int64_t(base_) + delay_ && now - lastSend_ >= std::chrono::milliseconds(100))
        setReady(bootHash_);   // until the peer's first input shows it started
    else if (readySet_ && now - lastSend_ >= std::chrono::milliseconds(15))
        sendInputs();          // a keepalive, and the unacknowledged inputs again
    if (byeSet_ && byeSends_ < 40 && (byeSends_ == 0 || now - lastBye_ >= std::chrono::milliseconds(20))) {
        ++byeSends_;
        lastBye_ = now;
        uint8_t b[12];
        for (int i = 0; i < 4; ++i) b[i] = uint8_t(byeFrames_ >> (8 * i));
        for (int i = 0; i < 8; ++i) b[4 + i] = uint8_t(byeHash_ >> (8 * i));
        sendSealed(kBye, b, sizeof b);
        sendInputs();
    }
    if (opt_.fuzz > 0 && readySet_ && peerReady_)
        fuzz();
}

bool Session::retarget(const Address& a)
{
    if (opt_.host || connected_ || gotAck_ || !a.ip)
        return false;
    if (a != peer_) {
        peer_ = a;
        lastHello_ = {};   // (the HELLO there at once)
    }
    return true;
}

bool Session::rejoinHello(const Address& from, const uint8_t* hello)
{
    // The same PC joining again before the 6 s silence ran out (a restarted game, a Wi-Fi drop): a HELLO from the
    // connected peer's address (its IP; a new process has another port) with a new nonce, while that peer has been
    // silent for a second. A live PC never sends HELLO once in, and another PC behind the same router finds the seat's
    // PC not silent, so it is still told "full". NetGame hands the seat back (rejoinAsked: the seat renewed at once);
    // the joiner's next HELLO (every 250 ms) lands in the fresh session and goes through the password check as any join.
    if (!opt_.host || !connected_ || from.ip != peer_.ip || silentSeconds() < kRejoinSilentS ||
        get16(hello + 5) != kProtocol || (from == peer_ && std::memcmp(peerNonce_, hello + 7, 16) == 0))
        return false;
    if (!rejoinAsked_)
        log("player " + std::to_string(opt_.relaySeat ? opt_.relaySeat + 1 : 2) + "'s PC is joining again from " +
            from.str() + ": its seat back at once");
    rejoinAsked_ = true;
    return true;
}

double Session::silentSeconds() const
{
    if (lastRecv_ == Clock::time_point{})
        return 0;
    return std::chrono::duration<double>(Clock::now() - lastRecv_).count();
}

std::string Session::counters() const
{
    char b[384];
    std::snprintf(b, sizeof b, "sent=%llu received=%llu dropped_malformed=%llu dropped_unauth=%llu dropped_replay=%llu "
                  "dropped_stranger=%llu dropped_window=%llu dropped_refused=%llu hashes_compared=%u",
                  (unsigned long long)sent, (unsigned long long)received, (unsigned long long)dropped[kMalformed],
                  (unsigned long long)dropped[kUnauth], (unsigned long long)dropped[kReplay],
                  (unsigned long long)dropped[kStranger], (unsigned long long)dropped[kWindow], (unsigned long long)dropped[kRefused], hashesCompared);
    return b;
}

} // namespace hotd::net
