// hotd_nettest: netplay's tests without the game (docs/netplay.md). Everything on 127.0.0.1. Exit code 0 = all passed.
//   crypto: Monocypher's known answers (X25519, Elligator 2, BLAKE2b, XChaCha20-Poly1305), CPace, tampering refused
//   handshake: the right code connects and gets the settings; a wrong code is refused
//   lockstep: 3000 frames of inputs both ways with 20 % loss: every frame's inputs equal on both sides
//   fuzz: settings decoder on random bytes; 20000 malformed / replayed / flipped packets into a running session
//   four PCs: a seat that leaves the waiting room is renewed; the next joiner gets it
#include "net/lobby.h"
#include "net/session.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <string>

using namespace hotd::net;

static int failures = 0;
constexpr size_t kSurvivalBytes = 10;   // (protocol 22: the settings' Survival bytes after bossMode)
static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

// a free UDP port (the OS picks one): no fixed ports, so a second copy of the test or a game hosting on this PC can't
// take a test's port ("UDP port ... is already in use" was the occasional failure on a busy PC)
static uint16_t freePort()
{
    UdpSocket s;
    std::string err;
    return s.open({0x7F000001, 0}, err) ? s.local().port : 0;
}

static bool connectPair(Session& h, Session& c, uint16_t port, const std::string& hostCode, const std::string& clientCode,
                        Settings& hs, Settings& cs, std::string& err, int loss = 0, int fuzz = 0, const std::string& hostName = "",
                        const std::string& clientName = "", const int* clientChars = nullptr)
{
    Options ho, co;
    if (clientChars)   // (protocol 16: the joiner's PC Mode characters in its CONFIRM)
        co.pcChar[0] = clientChars[0], co.pcChar[1] = clientChars[1];
    ho.host = true;
    ho.addr = {0x7F000001, port};
    ho.code = normaliseCode(hostCode);
    ho.name = hostName;
    co.addr = {0x7F000001, port};
    co.code = normaliseCode(clientCode);
    co.name = clientName;
    co.simLoss = ho.simLoss = loss;
    co.fuzz = ho.fuzz = fuzz;
    if (!h.start(ho, err) || !c.start(co, err))
        return false;
    for (int i = 0; i < 2000; ++i) {
        std::string e1, e2;
        const int a = h.handshake(hs, e1, 1), b = c.handshake(cs, e2, 1);
        if (a > 0 && !h.released() && h.pingSamples() >= 2) h.release(hs);   // the host's Start after a few pings
        if (b < 0) { err = e2; return false; }
        if (a < 0) { err = e1; return false; }
        if (a > 0 && b > 0) return true;
    }
    err = "timeout";
    return false;
}

int main()
{
    // crypto (Monocypher, every system): known answers from the library's own test vectors, CPace, the AEAD
    crypto::selfTest(check);
    {
        uint8_t r1[32] = {}, r2[32] = {};
        check(crypto::random(r1, 32) && crypto::random(r2, 32) && std::memcmp(r1, r2, 32) != 0, "the OS gives random bytes");
        uint8_t key[32] = {1, 2, 3};
        auto g = crypto::makeAead(key);
        uint8_t nonce[crypto::kNonceSize] = {}, ct[64 + 16], pt[64];
        const char msg[] = "the house of the dead";
        check(g && g->seal(nonce, "aad", 3, msg, sizeof msg, ct) && g->open(nonce, "aad", 3, ct, sizeof msg + 16, pt) &&
                  std::memcmp(pt, msg, sizeof msg) == 0, "XChaCha20-Poly1305 round trip");
        ct[3] ^= 1;
        check(g && !g->open(nonce, "aad", 3, ct, sizeof msg + 16, pt), "a flipped ciphertext bit is refused");
        ct[3] ^= 1;
        check(g && !g->open(nonce, "aae", 3, ct, sizeof msg + 16, pt), "other associated data is refused");
        nonce[20] ^= 1;
        check(g && !g->open(nonce, "aad", 3, ct, sizeof msg + 16, pt), "another nonce is refused");
    }
    // the LAN lobby (loopback) and the share code
    {
        LobbyListener ls;
        LobbyAnnouncer an;
        std::string err;
        const uint16_t lobbyPort = freePort();
        const bool up = ls.start({0x7F000001, lobbyPort}, err) && an.start({0x7F000001, lobbyPort}, err);
        LobbyGame g;
        g.addr.port = 47791, g.name = "Rogan's game\x01", g.players = 1, g.chapter = 0, g.build = buildId();
        g.publicIp = 0xCB00712D;   // (protocol 13: the host's internet address, for a LAN joiner with the join code)
        for (int i = 0; i < 30 && up && ls.games().empty(); ++i) {
            an.tick(g);
            ls.poll(50);
        }
        check(up && ls.games().size() == 1 && ls.games()[0].addr == Address{0x7F000001, 47791} &&
                  ls.games()[0].name == "Rogan's game?" && ls.games()[0].build == buildId() && ls.games()[0].publicIp == 0xCB00712D,
              ("an announced game is listed (name sanitised, the sender's address) " + err).c_str());
        {   // (2026-10-30) a game open on the LAN: its password in the announcement; a game needing the code: none
            LobbyGame o = g;
            o.needsCode = false, o.code = "K7QF2MXD";
            std::vector<uint8_t> v = encodeAnnouncement(o);
            LobbyGame d;
            const bool open = decodeAnnouncement(v.data(), v.size(), d) && !d.needsCode && d.code == "K7QF2MXD";
            o.needsCode = true;
            v = encodeAnnouncement(o);
            const bool closed = decodeAnnouncement(v.data(), v.size(), d) && d.needsCode && d.code.empty() &&
                                std::string(v.begin(), v.end()).find("K7QF2MXD") == std::string::npos;
            o.needsCode = false;
            v = encodeAnnouncement(o);
            v[v.size() - 3] = '-';   // (a password of other characters than letters / digits: refused)
            const bool bad = !decodeAnnouncement(v.data(), v.size(), d);
            v = encodeAnnouncement(o);
            v.pop_back();   // (cut short)
            const bool cut = !decodeAnnouncement(v.data(), v.size(), d);
            check(open && closed && bad && cut, "a LAN-open game's announcement carries its password; a game needing the code never does; a bad / cut one is refused");
            const std::vector<LocalIf> ifs = localInterfaces();
            std::string list;
            for (const LocalIf& f : ifs) list += " " + Address{f.ip, 0}.str() + " -> " + Address{f.broadcast, 0}.str();
            std::printf("      (this PC's networks:%s)\n", list.empty() ? " none" : list.c_str());
            bool sane = true;
            for (const LocalIf& f : ifs) sane = sane && f.ip && (f.broadcast & f.ip) == f.ip && (f.ip >> 24) != 127;
            check(sane && isPrivateIp(0xC0A80114) && isPrivateIp(0x0A000001) && isPrivateIp(0xAC100001) && !isPrivateIp(0xCB00712D) &&
                      !isPrivateIp(0x08080808), "the networks' broadcast addresses; private / internet addresses told apart");
        }
        std::mt19937_64 rng(3);
        int accepted = 0;
        for (int i = 0; i < 200000; ++i) {
            uint8_t b[96];
            const size_t n = rng() % sizeof b;
            for (size_t k = 0; k < n; ++k) b[k] = uint8_t(rng());
            if (i % 2 && n >= 6) b[0] = 'H', b[1] = 'N', b[2] = 'L', b[3] = '1', b[4] = uint8_t(kProtocol), b[5] = 0;
            LobbyGame x;
            if (decodeAnnouncement(b, n, x)) {
                ++accepted;
                if (x.name.size() > 32 || x.chapter > 3) { check(false, "an announcement decoded out of range"); break; }
            }
        }
        std::printf("      (random announcements accepted: %d of 200000, all in range)\n", accepted);
        check(true, "the announcement decoder on 200,000 random packets");
        Address a{0xC0A80114, 7777}, b;
        std::string code;
        const std::string sc = makeShareCode(a, "K7QF2MXD");
        check(parseShareCode(" " + sc + "\n", b, code) && b == a && code == "K7QF2MXD", ("the share code round trip: " + sc).c_str());
        // a custom password (not 8 Crockford symbols) or a port outside 47800-47803: the long form still
        const std::string lc = makeShareCode(a, "FOURPCS1");
        check(lc.rfind("HOTD-", 0) == 0 && parseShareCode(lc, b, code) && b == a && code == "FOURPCS1", ("the long share code: " + lc).c_str());
        std::string bad = lc;
        bad[8] = bad[8] == 'A' ? 'B' : 'A';
        check(!parseShareCode(bad, b, code), "a mistyped long share code is refused (the check byte)");
        // the join code (16 symbols): the address, port 47800-47803 and the 40-bit password in one
        const Address j{0xCB00712D, 47801};
        const std::string jc = makeJoinCode(j, "K7QF2MXD");
        check(jc.size() == 19 && jc[4] == '-' && jc.find_first_of("01ILO") == std::string::npos && makeShareCode(j, "K7QF2MXD") == jc,
              ("a join code: " + jc).c_str());
        std::string lower, bare;
        for (char c : jc) {
            lower += char(std::tolower(static_cast<unsigned char>(c)));
            if (c != '-') bare += c;
        }
        check(parseJoinCode(jc, b, code) == 1 && b == j && code == "K7QF2MXD", "the join code round trip");
        check(parseJoinCode(" " + lower + "\n", b, code) == 1 && b == j && code == "K7QF2MXD", "the join code in lower case");
        check(parseJoinCode(bare, b, code) == 1 && b == j && parseJoinCode(bare.substr(0, 8) + " " + bare.substr(8), b, code) == 1,
              "the join code without dashes / with a space");
        check(parseJoinCode(bare.substr(0, 15), b, code) == -1 && parseJoinCode(bare + "A", b, code) == -3 &&
                  parseJoinCode("O" + bare.substr(1), b, code) == -2 && parseJoinCode(bare.substr(0, 15) + "1", b, code) == -2,
              "a short / long code, a 0 O 1 I L: refused with its reason");
        // every single-symbol typo and every swap of two neighbours: almost all caught by the check symbol (the rest:
        // another address or password, i.e. no answer / one wrong-password try)
        const char kSet[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
        int typos = 0, passed = 0;
        for (size_t i = 0; i < bare.size(); ++i)
            for (const char* c = kSet; *c; ++c)
                if (*c != bare[i]) {
                    std::string t = bare;
                    t[i] = *c;
                    ++typos;
                    if (parseJoinCode(t, b, code) == 1) ++passed;
                }
        int swaps = 0, swapsPassed = 0;
        for (size_t i = 0; i + 1 < bare.size(); ++i)
            if (bare[i] != bare[i + 1]) {
                std::string t = bare;
                std::swap(t[i], t[i + 1]);
                ++swaps;
                if (parseJoinCode(t, b, code) == 1) ++swapsPassed;
            }
        std::printf("      (join code typos not caught: %d of %d single symbols, %d of %d swaps)\n", passed, typos, swapsPassed, swaps);
        check(passed * 10 < typos && swapsPassed * 5 <= swaps, "the join code's check symbol catches most typos");
        // 2,000 random addresses / passwords: each comes back exactly
        std::mt19937_64 r2(7);
        int same = 0;
        for (int i = 0; i < 2000; ++i) {
            const Address x{uint32_t(r2()) | 1u, uint16_t(47800 + r2() % 4)};
            const std::string pw = randomCode();
            Address y;
            std::string pc;
            const std::string c = makeJoinCode(x, pw);
            if (c.size() == 19 && parseJoinCode(c, y, pc) == 1 && y == x && pc == pw) ++same;
        }
        check(same == 2000, "2,000 random join codes round-trip (address, port, 40-bit password)");
    }
    // CPace: 200 codes, each agrees with itself and with no neighbour; fresh key pairs differ
    {
        int good = 0;
        for (int i = 0; i < 200; ++i) {
            const std::string code = "CODE" + std::to_string(i), next = "CODE" + std::to_string(i + 1);
            auto a = crypto::makeCpace(code.data(), code.size(), "nettest"), b = crypto::makeCpace(code.data(), code.size(), "nettest"),
                 c = crypto::makeCpace(next.data(), next.size(), "nettest");
            uint8_t ka[64], kb[64], kc[64];
            if (a && b && c && std::memcmp(a->publicKey(), b->publicKey(), 32) != 0 && a->derive(b->publicKey(), "t", 1, ka) &&
                b->derive(a->publicKey(), "t", 1, kb) && std::memcmp(ka, kb, 64) == 0 && a->derive(c->publicKey(), "t", 1, ka) &&
                c->derive(a->publicKey(), "t", 1, kc) && std::memcmp(ka, kc, 64) != 0)
                ++good;
        }
        check(good == 200, "CPace: 200 codes agree with themselves, never with another code");
    }
    // settings
    {
        Settings s;
        s.chapter = 2, s.players = 2, s.models = 1, s.difficulty = 4, s.delay = 5, s.hashEvery = 7, s.sonic = 3;
        s.routes = {{3, 1}, {104, 2}};
        s.gameSpeed = 1, s.autoReload = 1;
        Settings t;
        const auto v = encodeSettings(s);
        check(decodeSettings(v.data(), v.size(), t) && t.chapter == 2 && t.players == 2 && t.models == 1 && t.delay == 5 &&
                  t.hashEvery == 7 && t.sonic == 3 && t.routes.size() == 2 && t.routes[1].key == 104 && t.gameSpeed == 1 && t.autoReload == 1,
              "settings round trip");
        // protocol 14 (misc16): the host's 4P Boss Hit Points / 4P Enemy Toughness travel with the settings; out of
        // range (> 200) refused
        Settings s4;
        s4.players = 4, s4.seats = 4, s4.pcs = 4, s4.seat = 2, s4.boss4p = 125, s4.enemy4p = 0;
        Settings t4;
        auto v4 = encodeSettings(s4);
        check(decodeSettings(v4.data(), v4.size(), t4) && t4.boss4p == 125 && t4.enemy4p == 0 && t4.players == 4,
              "settings: 4P boss / enemy percent round trip");
        v4[v4.size() - 10 - kSurvivalBytes] = 201;   // (protocol 16: 7 PC Mode bytes after enemy4p; protocol 18: the Boss Mode byte; 22: Survival)
        check(!decodeSettings(v4.data(), v4.size(), t4), "settings: 4P boss percent over 200 refused");
        // protocol 15: a joiner's second seat (up to four PCs) in the seat byte's high nibble; none = the byte as before
        Settings s5 = s4;
        s5.seat = 1, s5.seat2 = 3;
        Settings t5;
        auto v5 = encodeSettings(s5);
        const bool two = decodeSettings(v5.data(), v5.size(), t5) && t5.seat == 1 && t5.seat2 == 3;
        s5.seat2 = 0;
        const auto v6 = encodeSettings(s5);
        const bool one = decodeSettings(v6.data(), v6.size(), t5) && t5.seat == 1 && t5.seat2 == 0 && v6[v6.size() - 13 - kSurvivalBytes] == 1;
        s5.seat2 = 1;   // the same seat twice
        const auto v7 = encodeSettings(s5);
        Settings s2p;   // a two-PC game with a second seat: refused
        s2p.seat2 = 3;
        const auto v8 = encodeSettings(s2p);
        check(two && one && !decodeSettings(v7.data(), v7.size(), t5) && !decodeSettings(v8.data(), v8.size(), t5),
              "settings: a joiner's second seat round trip (up to four PCs only, never its first seat again)");
        std::mt19937_64 rng(1);
        int accepted = 0;
        for (int i = 0; i < 1000000; ++i) {
            uint8_t b[128];
            const size_t n = rng() % sizeof b;
            for (size_t k = 0; k < n; ++k) b[k] = uint8_t(rng());
            if (i % 2 && n >= 2) b[0] = uint8_t(kProtocol), b[1] = 0;
            Settings x;
            if (decodeSettings(b, n, x)) {
                ++accepted;
                if (x.chapter > 3 || x.players < 1 || x.players > x.seats || x.delay < 1 || x.delay > 15 || x.joinFrame > kMaxJoinFrames) { check(false, "decoded out of range"); break; }
            }
        }
        std::printf("      (random settings blobs accepted: %d of 1000000, all in range)\n", accepted);
        check(true, "settings decoder on 1,000,000 random blobs");
    }
    // handshake: right and wrong code
    {
        Session h, c;
        Settings hs, cs;
        hs.chapter = 1, hs.difficulty = 3, hs.delay = 4;
        std::string err;
        check(connectPair(h, c, freePort(), "ABCD-EFGH", "abcdefgh", hs, cs, err) && cs.chapter == 1 && cs.difficulty == 3 &&
                  cs.delay == 4, ("the right code connects; the client gets the settings " + err).c_str());
    }
    {
        Session h, c;
        Settings hs, cs;
        std::string err;
        const bool ok = connectPair(h, c, freePort(), "ABCD-EFGH", "ABCD-EFGX", hs, cs, err);
        check(!ok && err.find("Wrong code") != std::string::npos, ("a wrong code is refused: " + err).c_str());
    }
    // the names and the roster (protocol 9): the joiner's name in its CONFIRM, every seat's in the host's ROSTER
    {
        Session h, c;
        Settings hs, cs;
        std::string err;
        const bool ok = connectPair(h, c, freePort(), "NAME-TEST", "NAME-TEST", hs, cs, err, 0, 0, "Rogan", "G\x01" "inger \x7f");
        RosterSeat r[4];
        r[0] = {true, 0, h.name()};
        r[1] = {true, 33, h.peerName()};
        h.sendRoster(r);
        for (int i = 0; i < 200 && !c.rosterReceived; ++i) { h.pump(1); c.pump(1); }
        check(ok && h.peerName() == "Ginger" && c.rosterReceived && c.roster[0].name == "Rogan" && c.roster[1].name == "Ginger" &&
                  c.roster[1].pingMs == 33 && c.roster[1].in && !c.roster[2].in,
              ("the joiner's name reaches the host (sanitised), the roster reaches the joiner: " + err).c_str());
        // the pause (protocol 13): the paused seats ride on the PINGs, at once on a change
        c.setPausedOut(2);
        for (int i = 0; i < 200 && h.peerPaused() != 2; ++i) { h.pump(1); c.pump(1); }
        const bool seen = h.peerPaused() == 2;
        c.setPausedOut(0);
        h.setPausedOut(1);
        for (int i = 0; i < 200 && (h.peerPaused() != 0 || c.peerPaused() != 1); ++i) { h.pump(1); c.pump(1); }
        check(ok && seen && h.peerPaused() == 0 && c.peerPaused() == 1, "a pause reaches the other PC (P2 PAUSED, then P1 PAUSED)");
        // (2026-10-30) the host's "P2 left, the game waits" (the high bits) reaches the joiner; a joiner leaving mid-game
        // sends BYE kByeLeaving (the host keeps its seat) instead of a frame count
        h.setPausedOut(0x20);
        for (int i = 0; i < 200 && c.peerPaused() != 0x20; ++i) { h.pump(1); c.pump(1); }
        const bool leftSeen = c.peerPaused() == 0x20;
        c.bye(kByeLeaving, 0);
        uint32_t pf = 0;
        uint64_t ph = 0;
        for (int i = 0; i < 200 && !h.peerBye(pf, ph); ++i) { h.pump(1); c.pump(1); }
        check(ok && leftSeen && h.peerBye(pf, ph) && pf == kByeLeaving, "P<n> left reaches the joiner; a joiner leaving mid-game: BYE kByeLeaving at the host");
    }
    // PC Mode online (protocol 16): the joiner's two characters in its CONFIRM; the host's settings carry PC Mode, each
    // seat's character, Continues, Free-Play; the roster each seat's character
    {
        auto hp = std::make_unique<Session>(), cp = std::make_unique<Session>();   // (heap: a Session is large)
        Session &h = *hp, &c = *cp;
        Settings hs, cs;
        hs.pcMode = 1, hs.pmContinues = 3, hs.pmFreePlay = 1, hs.seats = 4, hs.players = 4;
        const uint8_t chars[4] = {1, 5, 2, 6};
        std::copy(chars, chars + 4, hs.pmChar);
        const int picks[2] = {5, 6};
        std::string err;
        const bool ok = connectPair(h, c, freePort(), "PCMODE-1", "PCMODE-1", hs, cs, err, 0, 0, "Rogan", "Sophie", picks);
        check(ok && h.peerChar(0) == 5 && h.peerChar(1) == 6 && h.peerWants() == 1,
              ("PC Mode: the joiner's two characters reach the host in its CONFIRM: " + err).c_str());
        check(ok && cs.pcMode == 1 && cs.pmContinues == 3 && cs.pmFreePlay == 1 && std::equal(chars, chars + 4, cs.pmChar),
              "PC Mode: the host's settings reach the joiner (PC Mode, each seat's character, Continues, Free-Play)");
        RosterSeat r[4];
        r[0] = {true, 0, h.name(), -1, 1};
        r[1] = {true, 20, h.peerName(), -1, 5};
        r[3].pcChar = 6;
        h.sendRoster(r);
        for (int i = 0; i < 200 && !c.rosterReceived; ++i) { h.pump(1); c.pump(1); }
        check(ok && c.rosterReceived && c.roster[0].pcChar == 1 && c.roster[1].pcChar == 5 && c.roster[2].pcChar == -1 &&
                  c.roster[3].pcChar == 6,
              "PC Mode: the roster carries each seat's character to the joiner");
    }
    {
        auto h2p = std::make_unique<Session>(), c2p = std::make_unique<Session>();   // (heap: a Session is large)
        Session &h2 = *h2p, &c2 = *c2p;   // a joiner without a pick: 0 (the host's default for its seat)
        Settings hs2, cs2;
        std::string err;
        const bool ok2 = connectPair(h2, c2, freePort(), "PCMODE-2", "PCMODE-2", hs2, cs2, err);
        check(ok2 && h2.peerChar(0) == 0 && h2.peerChar(1) == 0 && cs2.pcMode == 0 && cs2.pmChar[0] == 0,
              "PC Mode: no pick = 0 (the seat's default); an arcade game's settings: PC Mode off, no characters");
    }
    // protocol 17: an input's start may carry the seat's PC Mode character (20, pmextra: bits 3-7, 1-19; 20-31 refused)
    check(Input::flagsOk(Input::kStart | 6 << Input::kCharShift) && Input::flagsOk(Input::kAll) && Input::flagsOk(0x08) &&
              Input::flagsOk(Input::kStart | 19 << Input::kCharShift) && Input::flagsOk(Input::kAll | 13 << Input::kCharShift) &&
              !Input::flagsOk(Input::kStart | 20 << Input::kCharShift) && !Input::flagsOk(0xF8) && !Input::flagsOk(0xFF),
          "PC Mode: an input's character 1-19 accepted (6, 13, 19: the PC's and the extras), 20-31 refused");
    {   // the settings' PC Mode bytes: out of range refused
        Settings s;
        s.pcMode = 1, s.pmContinues = 9;
        const uint8_t ok[4] = {1, 2, 13, 19};   // (pmextra: 13 / 19, Extra Characters)
        std::copy(ok, ok + 4, s.pmChar);
        Settings t;
        auto v = encodeSettings(s);
        const bool good = decodeSettings(v.data(), v.size(), t) && t.pcMode == 1 && t.pmContinues == 9 && t.pmChar[2] == 13 &&
                          t.pmChar[3] == 19;
        auto bad = [&](size_t fromEnd, uint8_t val) {
            auto w = v;
            w[w.size() - kSurvivalBytes - fromEnd] = val;   // (protocol 22: the Survival bytes after them)
            return !decodeSettings(w.data(), w.size(), t);
        };
        // from the end: 1 Boss Mode, 2 free play, 3 continues, 4-7 the characters (seat 4 .. 1), 8 PC Mode
        const bool refused = bad(2, 2) && bad(3, 10) && bad(4, 20) && bad(7, 0) && bad(8, 2) && bad(1, 6);
        Settings a;   // an arcade game with a character: refused
        a.pmChar[1] = 2;
        const auto av = encodeSettings(a);
        check(good && refused && !decodeSettings(av.data(), av.size(), t),
              "PC Mode settings: characters 13 / 19 kept; free play > 1, continues > 9, a character 0 / > 19, PC Mode > 1, an arcade game's character refused");
        RosterSeat r[4], d[4];
        r[0] = {true, 0, "Rogan", -1, 6};
        auto rv = encodeRoster(r);
        const bool rgood = decodeRoster(rv.data(), rv.size(), d) && d[0].pcChar == 6 && d[1].pcChar == -1;
        r[1] = {true, 0, "Curien", -1, 17};   // (pmextra: an Extra Character)
        const auto rv2 = encodeRoster(r);
        const bool rextra = decodeRoster(rv2.data(), rv2.size(), d) && d[0].pcChar == 6 && d[1].pcChar == 17;
        rv[19] = 20;
        check(rgood && rextra && !decodeRoster(rv.data(), rv.size(), d), "the roster's character byte: 0-19 / none (17 kept); 20 refused");
    }
    // Boss Mode online (protocol 18): the settings' Boss Mode byte (0, 2-6; never with PC Mode), the roster's (the host's
    // Boss Mode for the joiner's waiting room), and over the wire
    {
        bool trip = true;
        for (uint8_t m : {0, 2, 3, 4, 5, 6}) {
            Settings s, t;
            s.bossMode = m;
            const auto v = encodeSettings(s);
            trip = trip && decodeSettings(v.data(), v.size(), t) && t.bossMode == m && v[v.size() - 1 - kSurvivalBytes] == m;
        }
        bool refused = true;
        for (uint8_t m : {1, 7, 255}) {
            Settings s, t;
            auto v = encodeSettings(s);
            v[v.size() - 1 - kSurvivalBytes] = m;
            refused = refused && !decodeSettings(v.data(), v.size(), t);
        }
        Settings pb, t;   // Boss Mode and PC Mode together: refused
        pb.pcMode = 1, pb.bossMode = 6;
        for (uint8_t& c : pb.pmChar) c = 1;
        const auto pv = encodeSettings(pb);
        check(trip && refused && !decodeSettings(pv.data(), pv.size(), t),
              "Boss Mode settings: 0 / 2-6 round trip; 1, 7, 255 and Boss Mode with PC Mode refused");
        RosterSeat r[4], d[4];
        r[0] = {true, 0, "Rogan"};
        r[0].boss = 4;
        auto rv = encodeRoster(r);
        const bool rgood = rv.size() == 81 && rv.back() == 4 && decodeRoster(rv.data(), rv.size(), d) && d[0].boss == 4 && d[1].boss == 0;
        r[0].boss = 0;
        const auto rz = encodeRoster(r);
        const bool rnone = decodeRoster(rz.data(), rz.size(), d) && d[0].boss == 0;
        rv.back() = 1;
        const bool r1 = !decodeRoster(rv.data(), rv.size(), d);
        rv.back() = 7;
        check(rgood && rnone && r1 && !decodeRoster(rv.data(), rv.size(), d) && !decodeRoster(rv.data(), 80, d),
              "Boss Mode: the roster's last byte (the host's Boss Mode) round trip; 1 / 7 / the old 80-byte roster refused");
        auto hp = std::make_unique<Session>(), cp = std::make_unique<Session>();   // (heap: a Session is large)
        Session &h = *hp, &c = *cp;
        Settings hs, cs;
        hs.bossMode = 6, hs.players = 2;
        std::string err;
        const bool ok = connectPair(h, c, freePort(), "BOSSMODE", "BOSSMODE", hs, cs, err);
        r[0].boss = 6;
        h.sendRoster(r);
        for (int i = 0; i < 200 && !c.rosterReceived; ++i) { h.pump(1); c.pump(1); }
        check(ok && cs.bossMode == 6 && cs.pcMode == 0 && c.rosterReceived && c.roster[0].boss == 6,
              ("Boss Mode: the host's All Bosses reaches the joiner (settings and roster): " + err).c_str());
    }
    // Survival online (protocol 22): the settings' Survival bytes (the arena + 1, seed, cap, cut, the testing bytes; never
    // with PC Mode / Boss Mode), the roster's 0x10 + arena, an input's Survival extras (held, the weapon switch)
    {
        Settings s, t;
        s.survival = 4, s.survivalSeed = 0xDEADBEEF, s.survivalCap = 64, s.survivalCut = 0, s.survivalSmgWave = 3;
        s.survivalWeaponsWave = 7, s.survivalTest = 3;
        const auto v = encodeSettings(s);
        const bool trip = decodeSettings(v.data(), v.size(), t) && t.survival == 4 && t.survivalSeed == 0xDEADBEEF &&
                          t.survivalCap == 64 && t.survivalCut == 0 && t.survivalSmgWave == 3 && t.survivalWeaponsWave == 7 &&
                          t.survivalTest == 3 && t.bossMode == 0;
        auto bad = [&](size_t at, uint8_t val) {   // (at: from the Survival bytes' start)
            auto w = v;
            w[w.size() - kSurvivalBytes + at] = val;
            return !decodeSettings(w.data(), w.size(), t);
        };
        const bool refused = bad(0, 17) && bad(5, 0) && bad(5, 65) && bad(6, 2) && bad(9, 4);
        Settings sb = s, sp = s;   // Survival with Boss Mode / PC Mode: refused
        sb.bossMode = 2;
        sp.pcMode = 1;
        for (uint8_t& c : sp.pmChar) c = 1;
        const auto bv = encodeSettings(sb), pv = encodeSettings(sp);
        check(trip && refused && !decodeSettings(bv.data(), bv.size(), t) && !decodeSettings(pv.data(), pv.size(), t),
              "Survival settings: round trip; arena 17, cap 0 / 65, cut 2, test 4, with Boss Mode / PC Mode refused");
        RosterSeat r[4], d[4];
        r[0] = {true, 0, "Rogan"};
        r[0].survival = 1;
        auto rv = encodeRoster(r);
        const bool rgood = rv.size() == 81 && rv.back() == 0x10 && decodeRoster(rv.data(), rv.size(), d) && d[0].survival == 1 &&
                           d[0].boss == 0;
        rv.back() = 0x13;
        const bool r4 = decodeRoster(rv.data(), rv.size(), d) && d[0].survival == 4;
        rv.back() = 0x0F;
        const bool r15 = !decodeRoster(rv.data(), rv.size(), d);
        rv.back() = 0x20;
        check(rgood && r4 && r15 && !decodeRoster(rv.data(), rv.size(), d),
              "Survival: the roster's last byte 0x10 + arena round trip; 0x0F / 0x20 refused");
        Input in;
        in.flags = uint8_t(Input::kOnScreen | Input::kTrigger | Input::kHeld | (Input::kWeaponPick + 5) << Input::kWeaponShift);
        const Input p = in.predicted();
        Input st;   // a start's character bits are not held
        st.flags = uint8_t(Input::kStart | Input::kOnScreen | 1 << Input::kCharShift);
        check(Input::flagsOk(in.flags) && Input::flagsOk(uint8_t(11 << Input::kWeaponShift)) &&
                  !Input::flagsOk(uint8_t(12 << Input::kWeaponShift)) && p.flags == (int(Input::kOnScreen) | int(Input::kHeld)) &&
                  st.predicted().flags == Input::kOnScreen,
              "Survival: an input's extras (held, weapon codes 0-11) accepted, 12+ refused; the prediction keeps held only");
        auto hp = std::make_unique<Session>(), cp = std::make_unique<Session>();
        Session &h = *hp, &c = *cp;
        Settings hs, cs;
        hs.survival = 1, hs.players = 2, hs.survivalSeed = 1234;
        std::string err;
        const bool ok = connectPair(h, c, freePort(), "SURVIVAL", "SURVIVAL", hs, cs, err);
        check(ok && cs.survival == 1 && cs.survivalSeed == 1234 && cs.bossMode == 0,
              ("Survival: the host's arena and seed reach the joiner: " + err).c_str());
    }
    // up to four PCs: a seat's PC leaves the waiting room before Start; its seat's session is renewed (NetGame::poll
    // does it after kWaitingLeftS of silence) and the next joiner gets that seat, the other joiner stays in
    {
        Options ho;
        ho.host = true, ho.hub = true;
        ho.addr = {0x7F000001, freePort()};
        ho.code = normaliseCode("FOUR-PCS1");
        ho.name = "Rogan";
        Session hub;
        std::unique_ptr<Session> seat[4];
        std::string err;
        auto renew = [&](int s) {
            seat[s] = std::make_unique<Session>();
            Options so = ho;
            so.hub = false;
            so.relaySeat = s;
            return seat[s]->startShared(so, hub, err);
        };
        bool ok = hub.start(ho, err) && renew(1) && renew(2) && renew(3);
        auto joiner = [&](const char* name) {
            auto c = std::make_unique<Session>();
            Options co;
            co.addr = ho.addr;
            co.code = ho.code;
            co.name = name;
            if (!c->start(co, err)) c.reset();
            return c;
        };
        // pumps the host's side and the joiners until done() (or 3 s)
        auto run = [&](std::initializer_list<Session*> cs, const std::function<bool()>& done) {
            const auto t0 = std::chrono::steady_clock::now();
            while (!done() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3)) {
                Settings hs;
                std::string e;
                hub.handshake(hs, e, 1);
                for (int s = 1; s <= 3; ++s) seat[s]->handshake(hs, e, 0);
                for (Session* c : cs) { Settings x; c->handshake(x, e, 0); }
            }
            return done();
        };
        auto a = joiner("Ann"), b = joiner("Bob");
        ok = ok && a && b && run({a.get(), b.get()}, [&] { return a->peerConnected() && b->peerConnected(); });
        const int seatA = a ? a->localPlayer() : -1, seatB = b ? b->localPlayer() : -1;
        check(ok && seatA == 1 && seatB == 2 && seat[2]->peerName() == "Bob",
              ("four PCs: two joiners in the waiting room, seats 2 and 3: " + err).c_str());
        b.reset();   // Bob's PC closes (no goodbye)
        run({a.get()}, [&] { return seat[2]->silentSeconds() > 1.0; });
        const bool silent = seat[2]->silentSeconds() > 1.0 && seat[1]->silentSeconds() < 0.5;
        ok = ok && renew(2);   // (NetGame: after kWaitingLeftS)
        auto c = joiner("Cid");
        ok = ok && c && run({a.get(), c.get()}, [&] { return c->peerConnected(); });
        check(ok && silent && c->localPlayer() == 2 && seat[2]->peerName() == "Cid" && a->peerConnected() &&
                  seat[1]->peerConnected() && seat[1]->silentSeconds() < 0.5 && !seat[3]->peerConnected(),
              ("four PCs: a seat left before Start is free again: the next joiner gets seat 3, the first stays in: " + err).c_str());
    }
    // up to four PCs with a two-seat PC (protocol 15) and the host's relay: Dee asks for two seats (P2 + P4, as
    // NetGame::takePair gives), Eve gets P3, a fourth PC is told the game is full (P4 is Dee's); then 600 relayed frames at
    // 10 % loss: the host joins its own seat, Dee's two (18-byte inputs) and Eve's into four-seat frames, both joiners
    // get every frame whole, the state hashes compared on every link
    {
        Options ho;
        ho.host = true, ho.hub = true;
        ho.addr = {0x7F000001, freePort()};
        ho.code = normaliseCode("RELAY-4PC");
        ho.name = "Rogan";
        ho.simLoss = 10;
        Session hub;
        std::unique_ptr<Session> seat[4];
        std::string err;
        bool ok = hub.start(ho, err);
        for (int s = 1; s <= 3 && ok; ++s) {
            seat[s] = std::make_unique<Session>();
            Options so = ho;
            so.hub = false, so.relaySeat = s;
            ok = seat[s]->startShared(so, hub, err);
        }
        auto joiner = [&](const char* name, int seats) {
            auto c = std::make_unique<Session>();
            Options co;
            co.addr = ho.addr, co.code = ho.code, co.name = name, co.wantSeats = seats, co.simLoss = 10;
            if (!c->start(co, err)) c.reset();
            return c;
        };
        auto run = [&](std::initializer_list<Session*> cs, const std::function<bool()>& done, int ms = 3000) {
            const auto t0 = std::chrono::steady_clock::now();
            while (!done() && std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(ms)) {
                Settings hs;
                std::string e;
                hub.handshake(hs, e, 1);
                for (int s = 1; s <= 3; ++s) seat[s]->handshake(hs, e, 0);
                for (Session* c : cs) { Settings x; c->handshake(x, e, 0); }
            }
            return done();
        };
        auto dee = joiner("Dee", 2);
        ok = ok && dee && run({dee.get()}, [&] { return dee->peerConnected() && seat[1]->peerConnected(); });
        const bool wants = ok && seat[1]->peerWants() == 2;
        seat[3]->reserved = true;   // (NetGame::takePair: P4 is Dee's second seat)
        seat[1]->setPcSeats(2);
        auto eve = joiner("Eve", 1);
        ok = ok && eve && run({dee.get(), eve.get()}, [&] { return eve->peerConnected(); });
        const bool eveSeat = ok && eve->localPlayer() == 2 && seat[2]->peerWants() == 1;
        std::string fullErr;
        {
            auto fay = joiner("Fay", 1);
            std::string e;
            Settings x;
            int r = 0;
            run({dee.get(), eve.get()}, [&] { r = fay ? fay->handshake(x, e, 0) : -1; return r != 0; }, 4000);
            fullErr = e;
        }
        check(wants && eveSeat && fullErr.find("full") != std::string::npos,
              ("four PCs, a PC with two seats: its CONFIRM asks for two, the next joiner gets P3, a fourth PC is told the game is full: " + fullErr).c_str());
        // Start: the settings with each PC's seats
        Settings base;
        base.seats = 4, base.pcs = 4, base.players = 4, base.delay = 2;
        Settings sd = base, se = base, gd, ge;
        sd.seat = 1, sd.seat2 = 3, se.seat = 2;
        seat[1]->release(sd);
        seat[2]->release(se);
        int rd = 0, re = 0;
        run({}, [&] {
            std::string e;
            Settings hs;
            hub.handshake(hs, e, 1);
            for (int s = 1; s <= 3; ++s) seat[s]->handshake(hs, e, 0);
            if (rd <= 0) rd = dee->handshake(gd, e, 0);
            if (re <= 0) re = eve->handshake(ge, e, 0);
            return rd > 0 && re > 0;
        });
        check(rd > 0 && re > 0 && gd.seat2 == 3 && dee->secondSeat() == 3 && dee->localPlayer() == 1 && ge.seat2 == 0 &&
                  eve->secondSeat() == -1,
              "four PCs: the settings give Dee seats 2 + 4, Eve seat 3");
        auto inputOf = [](int s, uint32_t f) {
            Input in;
            in.flags = uint8_t((f * 5 + uint32_t(s) * 3) % 8);
            in.x = float(int(f % 400) - 200) + 0.5f * float(s);
            in.y = float(int(f % 300) - 150) - float(s);
            return in;
        };
        for (Session* s : {seat[1].get(), seat[2].get(), dee.get(), eve.get()}) s->setReady(42);   // (READY: then the resends)
        const uint32_t D = 2, N = 600;
        uint32_t relayed = D, gotD = D, gotE = D;
        uint32_t subD = D - 1, subE = D - 1;
        bool same = true;
        const auto t0 = std::chrono::steady_clock::now();
        while ((relayed < N || gotD < N || gotE < N) && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(20)) {
            // the joiners: their input of frame f + D once frame f arrived (the lockstep), their seats' only
            if (subD < gotD + D && subD + 1 < N + D) {
                ++subD;
                Input in = inputOf(1, subD);
                const Input s4 = inputOf(3, subD);
                in.flags2 = s4.flags, in.x2 = s4.x, in.y2 = s4.y;
                dee->submit(subD, in);
            }
            if (subE < gotE + D && subE + 1 < N + D) {
                ++subE;
                eve->submit(subE, inputOf(2, subE));
            }
            hub.pump(1);
            for (int s = 1; s <= 3; ++s) seat[s]->pump(0);
            dee->pump(0), eve->pump(0);
            // the host: frame f relayed once both PCs' inputs of f are in
            Input xd, xe;
            while (relayed < N) {
                seat[1]->markRun(relayed), seat[2]->markRun(relayed);
                if (!seat[1]->remoteInput(relayed, xd) || !seat[2]->remoteInput(relayed, xe)) break;
                Input full[2];
                setSeat(full, 0, inputOf(0, relayed));
                setSeat(full, 1, Input{xd.flags, xd.x, xd.y});
                setSeat(full, 3, Input{xd.flags2, xd.x2, xd.y2});
                setSeat(full, 2, Input{xe.flags, xe.x, xe.y});
                for (int s = 0; s < 4; ++s) {
                    const Input a = seatOf(full, s), b = inputOf(s, relayed);
                    if (a.flags != b.flags || a.x != b.x || a.y != b.y) same = false;
                }
                seat[1]->submitFull(relayed, full);
                seat[2]->submitFull(relayed, full);
                const uint64_t h = uint64_t(relayed) * 0x9E3779B97F4A7C15ull;
                seat[1]->frameHash(relayed, h), seat[2]->frameHash(relayed, h);
                ++relayed;
            }
            // the joiners: every seat of each frame as the host joined it
            for (int side = 0; side < 2; ++side) {
                Session& c = side ? *eve : *dee;
                uint32_t& g = side ? gotE : gotD;
                Input out[2], own;
                while (g < N && c.remoteFull(g, out)) {
                    c.localInput(g, own);
                    for (int s = 0; s < 4; ++s) {
                        const Input a = seatOf(out, s), b = inputOf(s, g);
                        if (a.flags != b.flags || a.x != b.x || a.y != b.y) same = false;
                    }
                    c.frameHash(g, uint64_t(g) * 0x9E3779B97F4A7C15ull);
                    ++g;
                }
            }
        }
        for (int i = 0; i < 300; ++i) { hub.pump(1); for (int s = 1; s <= 3; ++s) seat[s]->pump(0); dee->pump(0), eve->pump(0); }
        check(relayed == N && gotD == N && gotE == N && same,
              ("four PCs: 600 relayed frames at 10 % loss, the two-seat PC's both seats and every PC's input whole on every PC (" +
               std::to_string(relayed) + "/" + std::to_string(gotD) + "/" + std::to_string(gotE) + ")").c_str());
        check(!dee->desync && !eve->desync && !seat[1]->desync && !seat[2]->desync && seat[1]->hashesCompared > 100 &&
                  seat[2]->hashesCompared > 100,
              ("four PCs: the state hashes compared on both links, no desync (" + std::to_string(seat[1]->hashesCompared) + ", " +
               std::to_string(seat[2]->hashesCompared) + ")").c_str());
    }
    // the failures a player can meet: each one a clear message, none a silent hang
    auto attempt =[](uint16_t port, const std::string& code, int timeoutS, std::string& err, int maxMs = 6000) {
        Session c;
        Options co;
        co.addr = {0x7F000001, port};
        co.code = normaliseCode(code);
        co.connectTimeoutS = timeoutS;
        if (!c.start(co, err)) return -1;
        Settings cs;
        const auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(maxMs)) {
            const int r = c.handshake(cs, err, 5);
            if (r != 0) return r;
            if (c.peerConnected()) return 1;   // in the host's waiting room
        }
        err = "(still waiting: " + c.counters() + (c.peerConnected() ? " in" : " out") + ")";
        return 0;
    };
    auto pumpHost = [](Session& h, bool& stop) {   // the host's side, on a thread
        Settings hs;
        std::string e;
        while (!stop) { h.handshake(hs, e, 2); h.pump(0); }
    };
    {   // nobody at the address (the firewall / no port forward look the same): "No answer ... Windows Firewall"
        std::string err;
        const auto t0 = std::chrono::steady_clock::now();
        const uint16_t port = freePort();
        const int r = attempt(port, "ABCD-EFGH", 1, err);
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(r < 0 && err.find("No answer") != std::string::npos && err.find("Firewall") != std::string::npos &&
                  err.find("UDP " + std::to_string(port)) != std::string::npos && s < 3.0,
              ("no host: \"No answer\" with the firewall / port hint after the timeout: " + err).c_str());
    }
    {   // a game already full: "This game is full"
        const uint16_t port = freePort();
        Session h, c;
        Settings hs, cs;
        std::string err, err3;
        const bool ok = connectPair(h, c, port, "FULL-GAME", "FULL-GAME", hs, cs, err);
        bool stop = false;
        std::thread t([&] { while (!stop) { h.pump(2); c.pump(0); } });
        const int r = attempt(port, "FULL-GAME", 10, err3);
        stop = true;
        t.join();
        check(ok && r < 0 && err3.find("full") != std::string::npos, ("a third PC is told the game is full: " + err3).c_str());
    }
    {   // the same PC back 1.5 s after it dropped (a new process: another port): not "full"; the host hands its seat back
        const uint16_t port = freePort();
        auto hp = std::make_unique<Session>();   // (on the heap: main's stack holds enough sessions already)
        Session& h = *hp;
        Settings hs, cs;
        std::string err, err2;
        bool ok = false;
        {
            auto c = std::make_unique<Session>();
            ok = connectPair(h, *c, port, "BACK-AGAIN", "BACK-AGAIN", hs, cs, err);
            for (int i = 0; i < 20; ++i) { h.pump(2); c->pump(0); }
        }   // (c gone without a goodbye: a crash, a pulled cable)
        const auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1500)) h.pump(5);
        bool stop = false;
        std::thread t([&] { while (!stop && !h.rejoinAsked()) h.pump(2); });
        const int r = attempt(port, "BACK-AGAIN", 10, err2, 1500);
        stop = true;
        t.join();
        check(ok && r == 0 && err2.find("full") == std::string::npos && h.rejoinAsked() && h.silentSeconds() < 6.0,
              ("a PC joining again 1.5 s after it dropped is not told \"full\"; the host sees it is back: " + err2).c_str());
    }
    {   // another version: the host answers REFUSE "version"; the joiner says so
        const uint16_t port = freePort();
        Session h;
        Options ho;
        ho.host = true, ho.addr = {0x7F000001, port}, ho.code = "VERSIONS";
        std::string err;
        h.start(ho, err);
        UdpSocket raw;
        raw.open({0x7F000001, 0}, err);
        uint8_t hello[120] = {'H', 'N', 'P', '1', 1, uint8_t(kProtocol - 1), 0};
        raw.send({0x7F000001, port}, hello, sizeof hello);
        Settings hs;
        std::string e;
        h.handshake(hs, e, 50);
        uint8_t b[64];
        Address from;
        const int n = raw.recv(b, sizeof b, from, 500);
        check(n == 6 && b[4] == 9 && b[5] == 3, "an older version's HELLO gets REFUSE \"version\"");
        // the joiner's side: a fake host that refuses with "version" -> the message
        UdpSocket fake;
        const uint16_t port2 = freePort();   // (after the host above took its port)
        fake.open({0x7F000001, port2}, err);
        bool stop = false;
        std::thread t([&] {
            uint8_t q[2048];
            Address f;
            while (!stop) {
                const int k = fake.recv(q, sizeof q, f, 20);
                if (k > 0) { const uint8_t r[6] = {'H', 'N', 'P', '1', 9, 3}; fake.send(f, r, 6); }
            }
        });
        std::string err2;
        const int r = attempt(port2, "VERSIONS", 5, err2);
        stop = true;
        t.join();
        check(r < 0 && err2.find("another version") != std::string::npos, ("the joiner: \"another version\": " + err2).c_str());
    }
    {   // a guesser: 20 wrong passwords lock the host (even the right one is refused then, with its own message)
        const uint16_t port = freePort();
        Session h;
        Options ho;
        ho.host = true, ho.addr = {0x7F000001, port}, ho.code = "LOCKTEST";
        std::string err;
        h.start(ho, err);
        bool stop = false;
        std::thread t([&] { pumpHost(h, stop); });
        int wrong = 0;
        for (int i = 0; i < 20; ++i) {
            std::string e;
            if (attempt(port, "GUESS" + std::to_string(100 + i), 10, e) < 0 && e.find("Wrong code") != std::string::npos) ++wrong;
        }
        std::string e2;
        const int r = attempt(port, "LOCKTEST", 10, e2);
        stop = true;
        t.join();
        Settings hs;
        std::string he;
        const int hr = h.handshake(hs, he, 0);
        check(wrong == 20 && r < 0 && e2.find("too many wrong codes") != std::string::npos && hr < 0 &&
                  he.find("Too many wrong codes") != std::string::npos && h.wrongCodes() == 20,
              ("20 wrong passwords lock the host; the right one is then refused with \"too many wrong passwords\": " + e2).c_str());
    }
    {   // a flood of HELLOs (each would cost a key exchange): at most 16 a second; the real joiner still gets in
        const uint16_t port = freePort();
        Session h;
        Options ho;
        ho.host = true, ho.addr = {0x7F000001, port}, ho.code = "FLOODTST";
        std::string err;
        h.start(ho, err);
        UdpSocket raw;
        raw.open({0x7F000001, 0}, err);
        std::mt19937_64 rng(7);
        for (int i = 0; i < 400; ++i) {
            uint8_t hello[120] = {'H', 'N', 'P', '1', 1, uint8_t(kProtocol), uint8_t(kProtocol >> 8)};
            for (int k = 7; k < 23 + int(crypto::kPubSize); ++k) hello[k] = uint8_t(rng());
            raw.send({0x7F000001, port}, hello, sizeof hello);
        }
        bool stop = false;
        std::thread t([&] { pumpHost(h, stop); });
        std::string e;
        const int r = attempt(port, "FLOODTST", 10, e, 8000);
        stop = true;
        t.join();
        check(r > 0 && h.dropped[5] >= 200, ("400 forged HELLOs: rate-limited; the real joiner still gets into the waiting room (before Start): " + e + " " + h.counters()).c_str());
    }
    // the parsers on random and mutated input: the roster, a running game's input log
    {
        RosterSeat r[4], d[4];
        r[0] = {true, 0, "Rogan"}, r[1] = {true, 120, "G"}, r[3] = {true, 65535, "0123456789ABCDEFGHIJ"};
        const auto v = encodeRoster(r);
        check(v.size() == 81 && decodeRoster(v.data(), v.size(), d) && d[0].name == "Rogan" && d[1].pingMs == 120 && !d[2].in &&
                  d[3].name == "0123456789ABCDEF" && d[3].with == -1 && v[0] == 1 && v[20] == 1 && v[19] == 0xFF,
              "the roster round trip (names cut at 16; no second seats: the bytes as before)");
        // protocol 15: a PC's second seat names the seat of its first ("P2's PC")
        RosterSeat w[4], wd[4];
        w[0] = {true, 0, "Rogan"}, w[1] = {true, 40, "Dee"}, w[2] = {true, 0, "Rogan", 0}, w[3] = {true, 40, "Dee", 1};
        auto wv = encodeRoster(w);
        const bool wok = decodeRoster(wv.data(), wv.size(), wd) && wd[2].with == 0 && wd[3].with == 1 && wd[1].with == -1 && wd[3].in;
        auto bad = wv;
        bad[3 * 20] = 0x41;   // seat 3 "the second seat of seat 3": itself
        auto bad2 = wv;
        bad2[3 * 20] = 0x20;  // a second seat that is not in
        check(wok && !decodeRoster(bad.data(), bad.size(), wd) && !decodeRoster(bad2.data(), bad2.size(), wd),
              "the roster: a PC's second seat round trip; itself / not in refused");
        std::mt19937_64 rng(11);
        int accepted = 0;
        bool sane = true;
        for (int i = 0; i < 300000; ++i) {
            std::vector<uint8_t> b = v;
            if (i % 3 == 0) b.resize(rng() % 100);
            for (int k = 0, m = 1 + int(rng() % 6); k < m && !b.empty(); ++k) b[rng() % b.size()] = uint8_t(rng());
            if (i % 7 == 0) for (auto& x : b) x = uint8_t(rng());
            RosterSeat o[4];
            if (decodeRoster(b.data(), b.size(), o)) {
                ++accepted;
                for (const auto& s : o)
                    for (char ch : s.name)
                        if (ch < 32 || ch > 126 || s.name.size() > kMaxName) sane = false;
            }
        }
        std::printf("      (mutated rosters accepted: %d of 300000)\n", accepted);
        check(sane, "the roster decoder on 300,000 mutated packets: names always printable, at most 16");
    }
    {
        // a valid log: "HL", 10 frames, one restart at 4, two runs (6 + 4 frames)
        std::vector<uint8_t> v = {'H', 'L', 10, 0, 0, 0, 1, 0, 4, 0, 0, 0};
        auto run = [&v](uint16_t n, uint8_t flags) {
            v.push_back(uint8_t(n)), v.push_back(uint8_t(n >> 8));
            for (int pc = 0; pc < 2; ++pc) { v.push_back(flags); for (int k = 0; k < 8; ++k) v.push_back(0); }
        };
        run(6, 1), run(4, 3);
        std::vector<std::array<Input, 2>> frames;
        std::vector<uint32_t> restarts;
        check(decodeInputLog(v, frames, restarts) && frames.size() == 10 && restarts.size() == 1 && frames[7][1].flags == 3,
              "the input log round trip");
        std::vector<uint8_t> big = {'H', 'L', 0, 0x09, 0x3D, 0, 0, 0};   // 4,000,000 frames claimed, none there
        const auto t0 = std::chrono::steady_clock::now();
        const bool bigOk = decodeInputLog(big, frames, restarts);
        big[2] = 1, big[3] = 0x09, big[4] = 0x3D;                         // 4,000,001: over the limit
        check(!bigOk && !decodeInputLog(big, frames, restarts) &&
                  std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(200),
              "a log claiming millions of frames without them is refused at once (no huge allocation)");
        std::mt19937_64 rng(5);
        int accepted = 0;
        bool sane = true;
        for (int i = 0; i < 300000; ++i) {
            std::vector<uint8_t> b = v;
            if (i % 4 == 0) b.resize(rng() % (v.size() + 8));
            for (int k = 0, m = 1 + int(rng() % 4); k < m && !b.empty(); ++k) b[rng() % b.size()] = uint8_t(rng());
            if (decodeInputLog(b, frames, restarts)) {
                ++accepted;
                for (const auto& f : frames)
                    for (const Input& in : f)
                        if (!Input::flagsOk(in.flags) || !Input::flagsOk(in.flags2) || !std::isfinite(in.x) || std::fabs(in.x) > 4096.0f) sane = false;
                if (frames.size() > kMaxJoinFrames) sane = false;
            }
        }
        std::printf("      (mutated logs accepted: %d of 300000)\n", accepted);
        check(sane, "the input log decoder on 300,000 mutated logs: every accepted one in range");
    }
    // lockstep with loss, and the fuzz
    for (int pass = 0; pass < 2; ++pass) {
        Session h, c;
        Settings hs, cs;
        hs.delay = 3;
        std::string err;
        const bool fuzz = pass == 1;
        const uint16_t port = freePort();
        if (!connectPair(h, c, port, "QWER-TYUP", "QWER-TYUP", hs, cs, err, fuzz ? 0 : 20, fuzz ? 20000 : 0)) {
            check(false, ("connect for the lockstep: " + err).c_str());
            continue;
        }
        if (fuzz) {   // oversized datagrams from a stranger (over 1200 bytes, and over the 2048-byte buffer): dropped, the
                      // receive loop goes on
            UdpSocket raw;
            std::string e;
            raw.open({0x7F000001, 0}, e);
            std::vector<uint8_t> big(5000, 0x41);
            std::memcpy(big.data(), "HNP1", 4);
            for (int i = 0; i < 40; ++i) raw.send({0x7F000001, port}, big.data(), i % 2 ? 1300 : 5000);
        }
        h.setReady(42);
        c.setReady(42);
        uint64_t x;
        for (int i = 0; i < 2000 && (!h.peerReady(x) || !c.peerReady(x)); ++i) { h.pump(1); c.pump(1); }
        auto inputOf = [](int p, uint32_t f) {
            Input in;
            in.flags = uint8_t((f * 7 + uint32_t(p)) % 8 | (f % 20) << Input::kCharShift);   // (protocol 20: characters 0-19)
            in.x = float(int(f % 400) - 200) + 0.25f * float(p);
            in.y = float(int(f % 300) - 150);
            return in;
        };
        const uint32_t N = 3000;
        uint32_t fh = 0, fc = 0;
        bool same = true;
        int spins = 0;
        uint32_t submitted[2] = {h.delay() - 1u, c.delay() - 1u};
        while ((fh < N || fc < N) && spins < 2000000) {
            ++spins;
            for (int side = 0; side < 2; ++side) {
                Session& s = side ? c : h;
                uint32_t& f = side ? fc : fh;
                if (f >= N) { s.pump(0); continue; }
                if (submitted[side] < f + s.delay()) {
                    s.submit(f + s.delay(), inputOf(side, f + s.delay()));
                    submitted[side] = f + s.delay();
                }
                s.pump(0);
                Input in[2];
                if (s.inputs(f, in)) {
                    for (int p = 0; p < 2; ++p) {
                        const Input want = f < s.delay() ? Input{} : inputOf(p, f);
                        if (!(in[p] == want)) same = false;
                    }
                    s.frameHash(f, uint64_t(f) * 0x9E3779B97F4A7C15ull);
                    ++f;
                }
            }
        }
        check(fh == N && fc == N && same, fuzz ? "3000 lockstep frames under 20000 fuzz packets: every input equal"
                                               : "3000 lockstep frames with 20 % loss: every input equal on both sides");
        check(!h.desync && !c.desync && h.hashesCompared > 100, "the state hashes compared, no desync");
        std::printf("      host: %s\n      client: %s\n", h.counters().c_str(), c.counters().c_str());
        if (fuzz) {
            check(h.dropped[0] + h.dropped[1] + h.dropped[2] > 15000, "the host dropped the malformed / forged / replayed packets");
            check(c.dropped[0] + c.dropped[1] + c.dropped[2] > 15000, "the joiner dropped the host side's fuzz too");
            check(h.dropped[0] >= 40, "the oversized datagrams were dropped as malformed");
        }
    }
    {   // protocol 19, the same game data: the joiner's digests in its CONFIRM; the host refuses other ROMs always, other
        // PC files (CAMALL.RBH / THOTD.EXE's motion table) in a PC Mode / Boss Mode game, each with its own sentence
        struct Side { uint64_t rom, pc; };
        // "" = in the waiting room; else the joiner's message. hub: a host of up to four PCs (the seat sessions);
        // loss: percent of the host's packets lost (the refusal answered again on the joiner's repeats)
        auto dataJoin = [](Side hs, bool need, Side cs, bool hub, int loss, std::string& hostLog) -> std::string {
            Options ho, co;
            ho.host = true, ho.addr = {0x7F000001, freePort()}, ho.code = normaliseCode("DATA-TEST"), ho.hub = hub;
            ho.romData = hs.rom, ho.pcData = hs.pc, ho.needPcData = need, ho.simLoss = loss;
            ho.log = [&hostLog](const std::string& s) { hostLog += s + "\n"; };
            co.addr = ho.addr, co.code = ho.code, co.name = "Joe", co.romData = cs.rom, co.pcData = cs.pc;
            co.connectTimeoutS = 6;
            Session h, c;
            std::unique_ptr<Session> seat[4];
            std::string err;
            if (!h.start(ho, err) || !c.start(co, err)) return "start: " + err;
            for (int s = 1; s <= 3 && hub; ++s) {
                seat[s] = std::make_unique<Session>();
                Options so = ho;
                so.hub = false, so.relaySeat = s;
                if (!seat[s]->startShared(so, h, err)) return "seat: " + err;
                h.setSibling(s, seat[s].get());
            }
            const auto t0 = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(8)) {
                Settings x, y;
                std::string e1, e2;
                h.handshake(x, e1, 1);
                for (int s = 1; s <= 3 && hub; ++s) seat[s]->handshake(x, e1, 0);
                const int b = c.handshake(y, e2, 0);
                if (b < 0) return e2;
                if (c.peerConnected()) return "";
            }
            return "timeout";
        };
        const uint64_t R = 0x1111222233334444ull, R2 = 0x5555666677778888ull, P = 0xAAAABBBBCCCCDDDDull, P2 = 0x1234567812345678ull;
        std::string lg;
        check(dataJoin({R, P}, true, {R, P}, false, 0, lg).empty(), "same data: a PC Mode / Boss Mode joiner gets in");
        check(dataJoin({R, P}, false, {R, 0}, false, 0, lg).empty(), "an arcade game: the PC files need not match");
        check(dataJoin({0, 0}, true, {R, 0}, false, 0, lg).empty(), "a ROM digest unknown on one side: not compared");
        lg.clear();
        std::string e = dataJoin({R, P}, true, {R, 0}, false, 0, lg);
        check(e.find("The host uses the 1998 PC files") != std::string::npos && e.find("doesn't have them") != std::string::npos &&
                  lg.find("refused") != std::string::npos && lg.find("Joe") != std::string::npos,
              ("PC Mode, the joiner has no PC files: refused; joiner: " + e + "; host: " + lg.substr(std::min(lg.size(), lg.rfind("refused")))).c_str());
        lg.clear();
        e = dataJoin({R, 0}, true, {R, P}, false, 0, lg);
        check(e.find("the host doesn't have them") != std::string::npos, ("the host has no PC files: refused: " + e).c_str());
        lg.clear();
        e = dataJoin({R, P}, true, {R, P2}, false, 0, lg);
        check(e.find("are different from this PC's") != std::string::npos && e.find("THOTD.EXE") != std::string::npos,
              ("other PC files: refused: " + e).c_str());
        lg.clear();
        e = dataJoin({R, P}, false, {R2, P}, false, 0, lg);
        check(e.find("arcade ROMs are different") != std::string::npos, ("other ROMs (an arcade game too): refused: " + e).c_str());
        lg.clear();
        e = dataJoin({R, P}, true, {R, 0}, false, 70, lg);
        check(e.find("doesn't have them") != std::string::npos, ("70 % of the host's packets lost: the refusal still arrives: " + e).c_str());
        lg.clear();
        e = dataJoin({R, P}, true, {R, 0}, true, 0, lg);
        check(e.find("doesn't have them") != std::string::npos, ("a host of up to four PCs: refused the same way: " + e).c_str());
        check(dataJoin({R, P}, true, {R, P}, true, 0, lg).empty(), "a host of up to four PCs, same data: the joiner gets in");
    }
    std::printf("%s (%d failed)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
