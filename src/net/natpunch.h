// Online co-op without port forwarding (docs/netplay.md "Hole punching", worker "natpunch" 2026-11-10): our own
// minimal STUN client (RFC 5389 Binding Request / Response, XOR-MAPPED-ADDRESS), sent from the game's own netplay
// socket so the mapped port is the one the other PCs reach (the host's join code then carries it); a joiner's reply
// code (its public + LAN address) for when the direct try gets no answer; UDP hole punching (both PCs send small
// "hello" packets to each other's endpoints, then the normal handshake runs on the same socket). No server of our
// own, no relay. Written from the RFC (no STUN / ICE library's code).
#pragma once

#include "net/socket.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <string>
#include <thread>
#include <vector>

namespace hotd::net {

// ---- STUN (RFC 5389) ----
constexpr uint32_t kStunCookie = 0x2112A442;
constexpr size_t kStunHeader = 20;
// the public STUN servers asked by default (the order is the fallback order; launcher.ini stun=, --stun)
constexpr const char* kDefaultStun = "stun.l.google.com:19302,stun.cloudflare.com:3478,stun1.l.google.com:19302";
// a Binding Request (20 bytes: type 0x0001, length 0, the magic cookie, the transaction id)
std::vector<uint8_t> stunRequest(const uint8_t txid[12]);
// a STUN message's header (the two top bits 0, the cookie, the length a multiple of 4 and the size)
bool isStunPacket(const uint8_t* p, size_t n);
// a Binding success response to txid: 1 = mapped set (XOR-MAPPED-ADDRESS, else MAPPED-ADDRESS; IPv4), 0 = not one
// (another transaction, another type, no IPv4 address), -1 = malformed (an attribute past the end)
int stunParseResponse(const uint8_t* p, size_t n, const uint8_t txid[12], Address& mapped);
// the answer a server gives (the tests' mock server): a Binding success response with XOR-MAPPED-ADDRESS = mapped;
// empty: req is not a Binding Request
std::vector<uint8_t> stunResponse(const uint8_t* req, size_t n, const Address& mapped);

// a tiny STUN server for the tests (no internet): answers every Binding Request on 127.0.0.1:port with the sender's
// address (+ portShift: a "symmetric NAT" that maps each destination to another port). Its own thread
class StunMock {
public:
    ~StunMock() { stop(); }
    bool start(uint16_t port, int portShift, std::string& err);
    void stop();
    int answered() const { return answered_; }
private:
    UdpSocket sock_;
    std::thread thread_;
    std::atomic<bool> stopping_{false};
    std::atomic<int> answered_{0};
    int shift_ = 0;
};

// ---- the reply code ----
// The host's code stays the join code it always was (lobby.h makeShareCode: now with the STUN-mapped address when
// UPnP gave none). When a joiner's direct try gets no answer, its page shows a reply code for the host's waiting
// room: the joiner's public address (STUN) and LAN address with its port = 92 bits (a 2-bit kind first) + an 8-bit
// check: 20 symbols, XXXXX-XXXXX-XXXXX-XXXXX. The alphabet: Crockford base32 (0-9, A-Z without I L O U); read with
// O = 0 and I / L = 1, dashes and spaces ignored. A LAN address carried: 10/8, 172.16/12, 192.168/16 (else none).
struct ReplyCode {
    Address pub;                // the joiner's public (STUN-mapped) address; ip 0: unknown (LAN only)
    Address lan;                // its LAN address and port
};
std::string makeReplyCode(const ReplyCode& v);
// 1 = a reply code (v set); 0 = not one (another length); -1 a letter never used, -2 a typo (the check)
int parseReplyCode(const std::string& text, ReplyCode& v);
// natpunch2 (one paste for the whole squad, user 2026-11-10): every valid reply code in a block of text (a group
// chat's copy: names, times, other words, several codes, any case, dashes / spaces / no separators). A code is a run
// of letters / digits of 20 symbols, or neighbouring runs joined by '-', spaces or tabs that make exactly 20; only
// codes that pass the check count (a typo'd one is left out), each once, in order
std::vector<ReplyCode> findReplyCodes(const std::string& text);

// ---- hole punching ----
// a punch packet: "HOTP", type (1 hello, 2 its answer), the sender's 32-bit id (an answer: the hello's), zero padding
// to 16 bytes (never larger than what it answers). Carries no secret: the session's handshake (CPace) authenticates
constexpr size_t kPunchSize = 16;
bool isPunchPacket(const uint8_t* p, size_t n);

class NatTraversal {
public:
    using Send = std::function<void(const Address&, const uint8_t*, size_t)>;
    ~NatTraversal() { reset(); }
    void init(Send send, std::function<void(const std::string&)> log);
    void reset();
    // asks the STUN servers ("name:port" list, comma separated; resolved on a thread) for this socket's public address
    void startStun(const std::string& servers);
    // an incoming datagram: true = consumed (a STUN answer, a punch packet, or dropped by simNat)
    bool onPacket(const Address& from, const uint8_t* p, size_t n);
    void tick();                       // the (re)sends; call often (every frame)
    void punch(const Address& to, double seconds = 10.0);   // hellos every 150 ms for that long (to.ip 0: ignored)
    bool punching() const;             // a punch still running
    enum class Stun { Off, Resolving, Asking, Done, Failed };
    Stun stun() const { return stun_; }
    Address mapped() const { return mapped_; }   // this socket's public address (Done)
    bool symmetric() const { return symmetric_; } // two servers saw two ports: direct connections may fail
    int answers() const { return answers_; }
    Address reached() const { return reached_; } // the first punch target heard from (ip 0: none yet)
    // testing (--net-sim-nat): a restricted NAT in front of this PC: datagrams from an address never sent to (by
    // this object: STUN, punches, allow()) are dropped
    bool simNat = false;
    void allow(const Address& a);
    uint64_t simDropped = 0;
private:
    using Clock = std::chrono::steady_clock;
    Send send_;
    std::function<void(const std::string&)> log_;
    Stun stun_ = Stun::Off;
    std::future<std::vector<Address>> resolving_;
    struct Server { Address addr; uint8_t txid[12] = {}; int tries = 0; Clock::time_point last{}; bool answered = false; Address mapped; };
    std::vector<Server> servers_;
    Clock::time_point stunStart_{}, firstAnswer_{};
    Address mapped_;
    bool symmetric_ = false;
    int answers_ = 0;
    struct Target { Address addr; Clock::time_point until{}, last{}; };
    std::vector<Target> targets_;
    std::vector<Address> sentTo_;
    uint32_t id_ = 0;
    Address reached_;
    Clock::time_point ackWindow_{};
    int acks_ = 0;
    void sendTo(const Address& to, const uint8_t* p, size_t n);
    void log(const std::string& s) const { if (log_) log_(s); }
};

} // namespace hotd::net
