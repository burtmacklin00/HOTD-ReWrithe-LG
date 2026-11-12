// STUN, the reply code and hole punching (natpunch.h; docs/netplay.md "Hole punching"). STUN from
// RFC 5389 sections 6 (the header), 15.1 / 15.2 (MAPPED-ADDRESS / XOR-MAPPED-ADDRESS); nothing taken from a library.
#include "net/natpunch.h"

#include "net/crypto.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace hotd::net {

namespace {

constexpr uint16_t kBindingRequest = 0x0001, kBindingSuccess = 0x0101;
constexpr uint16_t kAttrMapped = 0x0001, kAttrXorMapped = 0x0020;
constexpr uint8_t kPunchHello = 1, kPunchAck = 2;
const char kCrockford[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr size_t kReplyLen = 20;
constexpr int kReplyBits = 92;
constexpr int kReplyCheck = 100 - kReplyBits;

uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
void putBe16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8), p[1] = uint8_t(v); }
void putBe32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (24 - 8 * i)); }

int crockfordValue(char c)
{
    if (c >= 'a' && c <= 'z') c = char(c - 32);
    if (c == 'O') return 0;
    if (c == 'I' || c == 'L') return 1;
    for (int i = 0; i < 32; ++i)
        if (kCrockford[i] == c) return i;
    return -1;
}

// a bit string, most significant first
struct Bits {
    std::vector<uint8_t> b;   // one bit per byte (small)
    void put(uint64_t v, int n) { for (int i = n - 1; i >= 0; --i) b.push_back(uint8_t((v >> i) & 1)); }
    uint64_t get(size_t& at, int n) const
    {
        uint64_t v = 0;
        for (int i = 0; i < n; ++i) v = v << 1 | (at < b.size() ? b[at++] : 0);
        return v;
    }
};

// the check: BLAKE2b of the payload bits, its first bits
uint32_t checkOf(const Bits& payload, int bits)
{
    uint8_t h[4];
    crypto::blake2b(h, sizeof h, "HOTDreply", 9, payload.b.data(), payload.b.size());
    return be32(h) >> (32 - bits);
}

// a LAN address's class (1 10/8, 2 172.16/12, 3 192.168/16) and its low 24 bits; 0: none carried
int lanClass(uint32_t ip)
{
    if ((ip >> 24) == 10) return 1;
    if ((ip >> 20) == 0xAC1) return 2;
    if ((ip >> 16) == 0xC0A8) return 3;
    return 0;
}
uint32_t lanFrom(int cls, uint32_t low)
{
    switch (cls) {
    case 1: return 0x0A000000u | (low & 0xFFFFFF);
    case 2: return 0xAC100000u | (low & 0x0FFFFF);
    case 3: return 0xC0A80000u | (low & 0xFFFF);
    default: return 0;
    }
}

} // namespace

// ---- STUN ----
std::vector<uint8_t> stunRequest(const uint8_t txid[12])
{
    std::vector<uint8_t> r(kStunHeader, 0);
    putBe16(r.data(), kBindingRequest);
    putBe16(r.data() + 2, 0);
    putBe32(r.data() + 4, kStunCookie);
    std::memcpy(r.data() + 8, txid, 12);
    return r;
}

bool isStunPacket(const uint8_t* p, size_t n)
{
    return n >= kStunHeader && (p[0] & 0xC0) == 0 && be32(p + 4) == kStunCookie && (be16(p + 2) & 3) == 0 &&
           kStunHeader + be16(p + 2) <= n;
}

int stunParseResponse(const uint8_t* p, size_t n, const uint8_t txid[12], Address& mapped)
{
    if (!isStunPacket(p, n) || be16(p) != kBindingSuccess || std::memcmp(p + 8, txid, 12) != 0)
        return 0;
    const size_t end = kStunHeader + be16(p + 2);
    Address plain, xored;
    bool havePlain = false, haveXor = false;
    for (size_t at = kStunHeader; at < end;) {
        if (at + 4 > end) return -1;
        const uint16_t type = be16(p + at), len = be16(p + at + 2);
        const size_t v = at + 4;
        if (v + len > end) return -1;
        // (an address attribute: 0, family 1 = IPv4, the port, the address; IPv6 (family 2) is not used here)
        if ((type == kAttrMapped || type == kAttrXorMapped) && len >= 8 && p[v + 1] == 1) {
            Address a{be32(p + v + 4), be16(p + v + 2)};
            if (type == kAttrXorMapped) {
                a.port = uint16_t(a.port ^ (kStunCookie >> 16));
                a.ip ^= kStunCookie;
                xored = a, haveXor = true;
            } else {
                plain = a, havePlain = true;
            }
        }
        at = v + ((len + 3u) & ~3u);   // (attributes are padded to 4 bytes)
    }
    if (!haveXor && !havePlain)
        return 0;
    mapped = haveXor ? xored : plain;
    return mapped.ip && mapped.port ? 1 : 0;
}

std::vector<uint8_t> stunResponse(const uint8_t* req, size_t n, const Address& mapped)
{
    if (!isStunPacket(req, n) || be16(req) != kBindingRequest)
        return {};
    std::vector<uint8_t> r(kStunHeader + 12, 0);
    putBe16(r.data(), kBindingSuccess);
    putBe16(r.data() + 2, 12);
    putBe32(r.data() + 4, kStunCookie);
    std::memcpy(r.data() + 8, req + 8, 12);
    uint8_t* a = r.data() + kStunHeader;
    putBe16(a, kAttrXorMapped);
    putBe16(a + 2, 8);
    a[5] = 1;
    putBe16(a + 6, uint16_t(mapped.port ^ (kStunCookie >> 16)));
    putBe32(a + 8, mapped.ip ^ kStunCookie);
    return r;
}

bool StunMock::start(uint16_t port, int portShift, std::string& err)
{
    stop();
    shift_ = portShift;
    if (!sock_.open(Address{0x7F000001, port}, err))
        return false;
    stopping_ = false;
    thread_ = std::thread([this]() {
        uint8_t buf[600];
        while (!stopping_) {
            Address from;
            const int n = sock_.recv(buf, sizeof buf, from, 50);
            if (n <= 0) continue;
            Address m = from;
            m.port = uint16_t(m.port + shift_);
            const std::vector<uint8_t> r = stunResponse(buf, size_t(n), m);
            if (!r.empty()) {
                sock_.send(from, r.data(), r.size());
                ++answered_;
            }
        }
    });
    return true;
}

void StunMock::stop()
{
    stopping_ = true;
    if (thread_.joinable()) thread_.join();
    sock_.close();
}

// ---- the reply code ----
std::string makeReplyCode(const ReplyCode& v)
{
    Bits b;
    b.put(2, 2);   // (the kind: 2 = a reply code; room for another kind later)
    b.put(v.pub.ip, 32);
    b.put(v.pub.port, 16);
    const int cls = lanClass(v.lan.ip);
    b.put(uint64_t(cls), 2);
    b.put(cls ? v.lan.ip & 0xFFFFFF : 0, 24);
    b.put(cls ? v.lan.port : 0, 16);
    Bits all = b;
    all.put(checkOf(b, kReplyCheck), kReplyCheck);
    std::string s;
    size_t at = 0;
    for (size_t i = 0; i < kReplyLen; ++i) {
        if (i && i % 5 == 0) s += '-';
        s += kCrockford[all.get(at, 5)];
    }
    return s;
}

int parseReplyCode(const std::string& text, ReplyCode& v)
{
    std::vector<int> sym;
    for (char c : text) {
        if (c == '-' || c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        sym.push_back(crockfordValue(c));
    }
    if (sym.size() != kReplyLen)
        return 0;
    for (int x : sym)
        if (x < 0) return -1;
    Bits all;
    for (int x : sym) all.put(uint64_t(x), 5);
    Bits payload;
    payload.b.assign(all.b.begin(), all.b.begin() + kReplyBits);
    size_t at = size_t(kReplyBits);
    if (all.get(at, kReplyCheck) != checkOf(payload, kReplyCheck))
        return -2;
    at = 0;
    if (payload.get(at, 2) != 2u)
        return -2;
    ReplyCode r;
    r.pub.ip = uint32_t(payload.get(at, 32));
    r.pub.port = uint16_t(payload.get(at, 16));
    const int cls = int(payload.get(at, 2));
    r.lan.ip = lanFrom(cls, uint32_t(payload.get(at, 24)));
    r.lan.port = uint16_t(payload.get(at, 16));
    if (!r.lan.ip) r.lan.port = 0;
    if ((r.pub.ip == 0) != (r.pub.port == 0) || (r.pub.ip == 0 && r.lan.ip == 0) || (r.lan.ip && !r.lan.port))
        return -2;
    v = r;
    return 1;
}

std::vector<ReplyCode> findReplyCodes(const std::string& text)
{
    // the runs of letters / digits, and whether a run is joined to the next one by '-' / spaces / tabs only (at most
    // 3 of them: "XXXXX-XXXXX", "XXXXX XXXXX"; a new line or other punctuation ends a code)
    struct Run { size_t at, len; bool joins; };
    std::vector<Run> runs;
    for (size_t i = 0; i < text.size() && runs.size() < 20000;) {
        if (!std::isalnum(static_cast<unsigned char>(text[i]))) { ++i; continue; }
        size_t j = i;
        while (j < text.size() && std::isalnum(static_cast<unsigned char>(text[j]))) ++j;
        size_t k = j;
        while (k < text.size() && k - j < 4 && (text[k] == '-' || text[k] == ' ' || text[k] == '\t')) ++k;
        runs.push_back({i, j - i, k > j && k - j <= 3 && k < text.size() && std::isalnum(static_cast<unsigned char>(text[k]))});
        i = j;
    }
    std::vector<ReplyCode> out;
    std::vector<std::string> seen;
    for (size_t r = 0; r < runs.size(); ++r) {
        std::string s;
        for (size_t q = r; q < runs.size(); ++q) {
            s += text.substr(runs[q].at, runs[q].len);
            if (s.size() >= kReplyLen || !runs[q].joins) break;
        }
        if (s.size() != kReplyLen) continue;
        ReplyCode c;
        if (parseReplyCode(s, c) != 1) continue;
        const std::string key = makeReplyCode(c);
        if (std::find(seen.begin(), seen.end(), key) == seen.end()) {
            seen.push_back(key);
            out.push_back(c);
        }
    }
    return out;
}

// ---- hole punching ----
bool isPunchPacket(const uint8_t* p, size_t n)
{
    return n == kPunchSize && std::memcmp(p, "HOTP", 4) == 0 && (p[4] == kPunchHello || p[4] == kPunchAck);
}

void NatTraversal::init(Send send, std::function<void(const std::string&)> log)
{
    reset();
    send_ = std::move(send);
    log_ = std::move(log);
    if (!crypto::random(&id_, sizeof id_) || id_ == 0)
        id_ = 0x48505431u;
}

void NatTraversal::reset()
{
    if (resolving_.valid()) resolving_.wait();
    resolving_ = {};
    servers_.clear();
    targets_.clear();
    sentTo_.clear();
    stun_ = Stun::Off;
    mapped_ = {};
    reached_ = {};
    symmetric_ = false;
    answers_ = 0;
}

void NatTraversal::startStun(const std::string& list)
{
    if (list.empty() || list == "off") { stun_ = Stun::Off; return; }
    stun_ = Stun::Resolving;
    stunStart_ = Clock::now();
    // (a name's lookup can take a moment: on a thread; the order kept = the fallback order)
    resolving_ = std::async(std::launch::async, [list]() {
        std::vector<Address> r;
        size_t at = 0;
        while (at <= list.size() && r.size() < 4) {
            size_t e = list.find(',', at);
            if (e == std::string::npos) e = list.size();
            std::string s = list.substr(at, e - at);
            while (!s.empty() && s.front() == ' ') s.erase(s.begin());
            while (!s.empty() && s.back() == ' ') s.pop_back();
            Address a;
            std::string err;
            if (!s.empty() && parseAddress(s, a, false, err) && a.ip && std::find(r.begin(), r.end(), a) == r.end())
                r.push_back(a);
            at = e + 1;
        }
        return r;
    });
}

void NatTraversal::allow(const Address& a)
{
    if (a.ip && std::find(sentTo_.begin(), sentTo_.end(), a) == sentTo_.end() && sentTo_.size() < 64)
        sentTo_.push_back(a);
}

void NatTraversal::sendTo(const Address& to, const uint8_t* p, size_t n)
{
    allow(to);
    if (send_) send_(to, p, n);
}

void NatTraversal::punch(const Address& to, double seconds)
{
    if (!to.ip || !to.port)
        return;
    const auto until = Clock::now() + std::chrono::milliseconds(int(seconds * 1000));
    for (Target& t : targets_)
        if (t.addr == to) { t.until = until; return; }
    if (targets_.size() < 24)
        targets_.push_back({to, until, {}});
    allow(to);
}

bool NatTraversal::punching() const
{
    const auto now = Clock::now();
    for (const Target& t : targets_)
        if (now < t.until) return true;
    return false;
}

void NatTraversal::tick()
{
    const auto now = Clock::now();
    if (stun_ == Stun::Resolving && resolving_.valid() &&
        resolving_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        for (const Address& a : resolving_.get()) {
            Server s;
            s.addr = a;
            if (!crypto::random(s.txid, sizeof s.txid)) continue;
            servers_.push_back(s);
        }
        resolving_ = {};
        stun_ = servers_.empty() ? Stun::Failed : Stun::Asking;
        stunStart_ = now;
        if (servers_.empty()) log("STUN: no server could be found (no internet, or the names do not resolve)");
    }
    if (stun_ == Stun::Asking) {
        for (Server& s : servers_)
            if (!s.answered && s.tries < 6 && (s.tries == 0 || now - s.last >= std::chrono::milliseconds(500))) {
                ++s.tries;
                s.last = now;
                const std::vector<uint8_t> r = stunRequest(s.txid);
                sendTo(s.addr, r.data(), r.size());
            }
        // done: two answers, or one and 1 s more for a second, or the 3.5 s without any
        const bool any = answers_ > 0;
        if (answers_ >= 2 || (any && now - firstAnswer_ > std::chrono::seconds(1)) ||
            (now - stunStart_ > std::chrono::milliseconds(3500))) {
            stun_ = any ? Stun::Done : Stun::Failed;
            log(any ? std::string("STUN: got this PC's internet address (") + std::to_string(answers_) + " server" +
                          (answers_ > 1 ? "s" : "") + (symmetric_ ? "; the port differs per server: a strict NAT)" : ")")
                    : "STUN: no answer from the STUN servers");
        }
    }
    for (Target& t : targets_)
        if (now < t.until && now - t.last >= std::chrono::milliseconds(150)) {
            t.last = now;
            uint8_t p[kPunchSize] = {'H', 'O', 'T', 'P', kPunchHello};
            std::memcpy(p + 5, &id_, 4);
            sendTo(t.addr, p, sizeof p);
        }
}

bool NatTraversal::onPacket(const Address& from, const uint8_t* p, size_t n)
{
    if (simNat && std::find(sentTo_.begin(), sentTo_.end(), from) == sentTo_.end()) {
        ++simDropped;   // (the simulated NAT: nobody this PC sent to)
        return true;
    }
    if (isStunPacket(p, n)) {
        for (Server& s : servers_) {
            Address m;
            if (s.answered || from != s.addr || stunParseResponse(p, n, s.txid, m) != 1) continue;
            s.answered = true;
            s.mapped = m;
            if (answers_++ == 0) {
                mapped_ = m;
                firstAnswer_ = Clock::now();
            } else if (m.port != mapped_.port || m.ip != mapped_.ip) {
                symmetric_ = true;
            }
        }
        return true;
    }
    if (!isPunchPacket(p, n))
        return false;
    for (const Target& t : targets_)
        if (t.addr == from && !reached_.ip) reached_ = from;
    if (p[4] == kPunchHello) {
        // the answer (rate-limited: 64 a second; the same size as the hello, so nothing is amplified)
        const auto now = Clock::now();
        if (now - ackWindow_ > std::chrono::seconds(1)) ackWindow_ = now, acks_ = 0;
        if (++acks_ <= 64) {
            uint8_t a[kPunchSize] = {'H', 'O', 'T', 'P', kPunchAck};
            std::memcpy(a + 5, p + 5, 4);
            if (send_) send_(from, a, sizeof a);
        }
    }
    return true;
}

} // namespace hotd::net
