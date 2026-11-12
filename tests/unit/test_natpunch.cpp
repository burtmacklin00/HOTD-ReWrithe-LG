// net (worker "natpunch", 2026-11-10): the hole punching's pieces (src/net/natpunch.h): STUN's Binding Request /
// Response (an RFC 5769 test vector), the reply code, and a mock STUN server + two punching sockets on the loopback
// with a simulated restricted NAT on each (no internet).
#include "check.h"

#include "net/natpunch.h"
#include "net/lobby.h"

#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

using namespace hotd;

namespace {

// RFC 5769 section 2.2: a sample IPv4 Binding success response (SOFTWARE, XOR-MAPPED-ADDRESS 192.0.2.1:32853,
// MESSAGE-INTEGRITY, FINGERPRINT)
const uint8_t kRfc5769Response[] = {
    0x01, 0x01, 0x00, 0x3c, 0x21, 0x12, 0xa4, 0x42, 0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xae,
    0x80, 0x22, 0x00, 0x0b, 0x74, 0x65, 0x73, 0x74, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x20,
    0x00, 0x20, 0x00, 0x08, 0x00, 0x01, 0xa1, 0x47, 0xe1, 0x12, 0xa6, 0x43,
    0x00, 0x08, 0x00, 0x14, 0x2b, 0x91, 0xf5, 0x99, 0xfd, 0x9e, 0x90, 0xc3, 0x8c, 0x74, 0x89, 0xf9, 0x2a, 0xf9, 0xba, 0x53,
    0xf0, 0x6b, 0xe7, 0xd7,
    0x80, 0x28, 0x00, 0x04, 0xc0, 0x7d, 0x4c, 0x96};

// pumps a socket into its NatTraversal (what Session::receiveAll does with Options::raw)
void pump(net::UdpSocket& s, net::NatTraversal& n)
{
    uint8_t buf[1500];
    for (;;) {
        net::Address from;
        const int k = s.recv(buf, sizeof buf, from, 0);
        if (k <= 0) return;
        n.onPacket(from, buf, size_t(k));
    }
}

template <class F>
bool waitFor(F done, int ms)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!done()) {
        if (std::chrono::steady_clock::now() > until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

} // namespace

TEST(net, stun_messages)
{
    const uint8_t tx[12] = {0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xae};
    const std::vector<uint8_t> req = net::stunRequest(tx);
    CHECK_EQ(req.size(), size_t(20));
    CHECK(req[0] == 0x00 && req[1] == 0x01 && req[2] == 0 && req[3] == 0);   // Binding Request, no attributes
    CHECK(req[4] == 0x21 && req[5] == 0x12 && req[6] == 0xa4 && req[7] == 0x42);   // the magic cookie
    CHECK(std::memcmp(req.data() + 8, tx, 12) == 0);
    CHECK(net::isStunPacket(req.data(), req.size()));

    net::Address m;   // the RFC's own sample answer
    CHECK_EQ(net::stunParseResponse(kRfc5769Response, sizeof kRfc5769Response, tx, m), 1);
    CHECK_EQ(m.ip, 0xC0000201u);   // 192.0.2.1
    CHECK_EQ(int(m.port), 32853);
    uint8_t other[12];
    std::memcpy(other, tx, 12);
    other[11] ^= 1;
    CHECK_EQ(net::stunParseResponse(kRfc5769Response, sizeof kRfc5769Response, other, m), 0);   // another transaction
    uint8_t cut[sizeof kRfc5769Response];
    std::memcpy(cut, kRfc5769Response, sizeof cut);
    cut[22] = 0x00, cut[23] = 0x60;   // SOFTWARE's length past the message's end
    CHECK_EQ(net::stunParseResponse(cut, sizeof cut, tx, m), -1);
    CHECK(!net::isStunPacket(kRfc5769Response, 19));
    CHECK_EQ(net::stunParseResponse(req.data(), req.size(), tx, m), 0);   // a request is no answer

    // the mock server's answer, read back
    const net::Address who{0xCB00712Du, 50123};
    const std::vector<uint8_t> resp = net::stunResponse(req.data(), req.size(), who);
    net::Address back;
    CHECK_EQ(net::stunParseResponse(resp.data(), resp.size(), tx, back), 1);
    CHECK(back == who);
    CHECK(net::stunResponse(resp.data(), resp.size(), who).empty());   // (an answer is not answered)
}

TEST(net, reply_code)
{
    net::ReplyCode r;
    r.pub = {0xCB00712Du, 61234};
    r.lan = {0xC0A80117u, 50000};   // 192.168.1.23
    const std::string c = net::makeReplyCode(r);
    CHECK_EQ(c.size(), size_t(23));   // 20 symbols, 3 dashes
    net::ReplyCode b;
    CHECK_EQ(net::parseReplyCode(c, b), 1);
    CHECK(b.pub == r.pub && b.lan == r.lan);
    std::string lower, bare;
    for (char ch : c) {
        lower += char(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
        if (ch != '-') bare += ch;
    }
    CHECK_EQ(net::parseReplyCode(" " + lower + "\n", b), 1);
    CHECK_EQ(net::parseReplyCode(bare, b), 1);
    // the other LAN classes; none (a LAN address outside them: only the public one)
    for (uint32_t lan : {0x0A0B0C0Du, 0xAC1F0102u, 0u, 0x08080808u}) {
        r.lan = {lan, 40001};
        CHECK_EQ(net::parseReplyCode(net::makeReplyCode(r), b), 1);
        const bool carried = lan && lan != 0x08080808u;
        CHECK(b.pub == r.pub && b.lan.ip == (carried ? lan : 0u) && b.lan.port == (carried ? 40001 : 0));
    }
    // not one: a 16-symbol join code, an address; a letter never used
    CHECK_EQ(net::parseReplyCode(net::makeJoinCode({0xCB00712Du, 47800}, "K7QF2MXD"), b), 0);
    CHECK_EQ(net::parseReplyCode("192.168.1.20:47800", b), 0);
    std::string u = bare;
    u[3] = 'U';
    CHECK_EQ(net::parseReplyCode(u, b), -1);
    // typos: every single-symbol change of 20 random codes, most caught by the 8-bit check
    int typos = 0, passed = 0;
    uint32_t seed = 12345;
    auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed; };
    const char alpha[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    int same = 0;
    for (int k = 0; k < 20; ++k) {
        net::ReplyCode x;
        x.pub = {rnd() | 1u, uint16_t(rnd() | 1)};
        x.lan = {0xC0A80000u | (rnd() & 0xFFFF), uint16_t(rnd() | 1)};
        const std::string s = net::makeReplyCode(x);
        net::ReplyCode y;
        if (net::parseReplyCode(s, y) == 1 && y.pub == x.pub && y.lan == x.lan) ++same;
        std::string t;
        for (char ch : s) if (ch != '-') t += ch;
        for (size_t i = 0; i < t.size(); ++i) {
            std::string w = t;
            w[i] = alpha[(std::strchr(alpha, t[i]) - alpha + 1 + int(rnd() % 31)) % 32];
            ++typos;
            if (net::parseReplyCode(w, y) == 1) ++passed;
        }
    }
    CHECK_EQ(same, 20);
    CHECK(passed * 50 < typos * 2);   // (an 8-bit check: ~1 in 256 gets through; a little more allowed)
}

TEST(net, mock_stun_and_punch)
{
    // two "PCs" on the loopback, each behind a simulated restricted NAT; two mock STUN servers (the second answers
    // 7 ports off for B only: a symmetric NAT)
    std::string err;
    net::StunMock s1, s2;
    CHECK(s1.start(47961, 0, err));
    CHECK(s2.start(47962, 0, err));
    net::StunMock s3;
    CHECK(s3.start(47963, 7, err));
    net::UdpSocket a, b;
    CHECK(a.open({0x7F000001, 0}, err));
    CHECK(b.open({0x7F000001, 0}, err));
    net::NatTraversal na, nb;
    na.init([&a](const net::Address& to, const uint8_t* p, size_t n) { a.send(to, p, n); }, nullptr);
    nb.init([&b](const net::Address& to, const uint8_t* p, size_t n) { b.send(to, p, n); }, nullptr);
    na.simNat = nb.simNat = true;
    na.startStun("127.0.0.1:47961, 127.0.0.1:47962");
    nb.startStun("127.0.0.1:47961,127.0.0.1:47963");
    auto step = [&]() { na.tick(); nb.tick(); pump(a, na); pump(b, nb); };
    CHECK(waitFor([&]() { step(); return na.stun() == net::NatTraversal::Stun::Done && nb.stun() == net::NatTraversal::Stun::Done; }, 5000));
    CHECK(na.mapped() == a.local());
    // (B asks two servers at once and keeps the first answer: s1's (its own port) or s3's (7 off), whichever comes
    // first; the macOS runner's loopback answered s3 first. Both are right for a symmetric NAT)
    CHECK(nb.mapped() == b.local() || (nb.mapped().ip == b.local().ip && nb.mapped().port == uint16_t(b.local().port + 7)));
    CHECK_EQ(na.answers(), 2);
    CHECK(!na.symmetric());
    CHECK(nb.symmetric());   // (the two servers saw two ports)

    // A punches B first: B's NAT drops it (B never sent to A); then B punches A (A pasted B's reply code / B the
    // host's code): both hear each other
    na.punch(b.local(), 2.0);   // (B's socket itself: its first answer may be the symmetric server's 7-off port)
    for (int i = 0; i < 30; ++i) { step(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    CHECK(nb.simDropped > 0);
    CHECK(!nb.reached().ip);
    nb.punch(na.mapped(), 2.0);
    CHECK(waitFor([&]() { step(); return na.reached().ip && nb.reached().ip; }, 3000));
    CHECK(na.reached() == b.local());
    CHECK(nb.reached() == a.local());
    CHECK(s1.answered() >= 2);
}

TEST(net, stun_real_answer)
{
    // the manual check (no internet in CI): HOTD_STUN_SAMPLE = a file with the transaction id + a real server's
    // answer in hex (the worker's PowerShell query); our parser must find the mapped address. Only "ok" is printed
    const char* path = std::getenv("HOTD_STUN_SAMPLE");
    if (!path || !*path) return;
    std::string hex;
    if (FILE* f = std::fopen(path, "rb")) {
        char buf[1200];
        hex.assign(buf, std::fread(buf, 1, sizeof buf, f));
        std::fclose(f);
    }
    std::vector<uint8_t> b;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        if (!std::isxdigit(static_cast<unsigned char>(hex[i]))) break;
        b.push_back(uint8_t(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    CHECK(b.size() > 12 + 20);
    if (b.size() <= 32) return;
    net::Address m;
    CHECK_EQ(net::stunParseResponse(b.data() + 12, b.size() - 12, b.data(), m), 1);
    CHECK(m.ip != 0 && m.port != 0);
    std::printf("      (a real STUN server's answer parsed: ok)\n");
}

TEST(net, log_redaction)
{
    // public addresses hidden, ports kept, the same address = the same tag; private / loopback ones readable
    const uint32_t salt = 0x1234567u;
    const std::string a = net::redactLog("player 2 joined from 203.0.113.45:47801", salt);
    CHECK(a.find("203.0.113.45") == std::string::npos);
    CHECK(a.rfind("player 2 joined from public#", 0) == 0);
    CHECK(a.size() > 4 && a.compare(a.size() - 6, 6, ":47801") == 0);
    const std::string b = net::redactLog("No answer from 203.0.113.45:47800; also 198.51.100.7", salt);
    CHECK(b.find("203.0.113.45") == std::string::npos && b.find("198.51.100.7") == std::string::npos);
    CHECK(b.find(a.substr(21, 11)) != std::string::npos);   // (the same tag for the same address)
    CHECK(net::redactLog("x 198.51.100.7", salt) != net::redactLog("x 203.0.113.45", salt));
    for (const char* keep : {"hosting on UDP 192.168.1.20:47800", "10.0.0.5", "172.16.4.1:9", "127.0.0.1:47910",
                             "version 1.3.1", "Monocypher 4.0.3", "frame 1.2.3.4.5", "ROMs 3d9c38443bc2f6ce", "0.0.0.0:47800"})
        CHECK_EQ(net::redactLog(keep, salt), std::string(keep));
    CHECK(net::redactLog("a8.8.8.8", salt) == "a8.8.8.8");   // (inside a word: not an address)
    CHECK(net::redactLog("[8.8.8.8]", salt).find("8.8.8.8") == std::string::npos);
    CHECK(net::redactLog("8.8.8.8", salt).rfind("public#", 0) == 0);
    CHECK(net::logAddr({0xC0A80114u, 47800}) == "192.168.1.20:47800");
}

TEST(net, reply_codes_in_a_chat_paste)
{
    // natpunch2: one paste for the whole squad: a group chat's copy with names, times, noise, three good codes (with
    // dashes, without, lower case with spaces), one repeated, one with a typo
    net::ReplyCode a, b, c;
    a.pub = {0xCB00712Du, 61234}, a.lan = {0xC0A80117u, 50000};
    b.pub = {0xC6336407u, 40999}, b.lan = {0x0A000005u, 50123};
    c.pub = {0x5DB8D822u, 1025}, c.lan = {};
    const std::string ca = net::makeReplyCode(a), cb = net::makeReplyCode(b), cc = net::makeReplyCode(c);
    std::string bare, lowerSpaced;
    for (char ch : cb) if (ch != '-') bare += ch;
    for (char ch : cc) lowerSpaced += ch == '-' ? ' ' : char(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
    std::string typo = net::makeReplyCode(a);
    typo[0] = typo[0] == '7' ? '8' : '7';   // (another first symbol: the check refuses it, or it is another code)
    const std::string chat =
        "[19:02] Sophie: ok here's mine\n[19:02] Sophie: " + ca + "\n"
        "[19:03] G: " + bare + " (the game says no direct answer??)\n"
        "[19:03] Thomas Rogan: lol mine -> " + lowerSpaced + ", hurry up\n"
        "[19:04] Sophie: again: " + ca + "\n"
        "[19:04] Harry: " + typo + " <- typed it by hand\n"
        "[19:05] G: also HOTD-0ZR0000VPAPZ-EJ32MVQK and 2WFU-MBKD-YH5B-72XV are old codes, 192.168.1.20:47800\n";
    const std::vector<net::ReplyCode> f = net::findReplyCodes(chat);
    int hits = 0;
    bool typoIn = false;
    net::ReplyCode t;
    const bool typoValid = net::parseReplyCode(typo, t) == 1;
    for (const net::ReplyCode& r : f) {
        if (r.pub == a.pub && r.lan == a.lan) ++hits;
        else if (r.pub == b.pub && r.lan == b.lan) ++hits;
        else if (r.pub == c.pub && r.lan == c.lan) ++hits;
        else if (typoValid && r.pub == t.pub) typoIn = true;
    }
    CHECK_EQ(hits, 3);   // each of the three once (the repeat left out)
    CHECK_EQ(f.size(), size_t(3 + (typoIn ? 1 : 0)));
    CHECK(f.size() >= 1 && f[0].pub == a.pub);   // in order
    CHECK(net::findReplyCodes("no codes here, just 12345678901234567890 and ABCDEFGHJKMNPQRSTVWX").size() <= 1);   // (8-bit check)
    CHECK(net::findReplyCodes("").empty());
    // a code split over two lines is not one (a new line ends it)
    CHECK(net::findReplyCodes(ca.substr(0, 11) + "\n" + ca.substr(12)).empty());
}
