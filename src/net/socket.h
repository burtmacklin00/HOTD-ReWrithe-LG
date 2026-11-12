// UDP transport for netplay (docs/netplay.md): a small non-blocking IPv4 datagram socket over Winsock (Windows) or BSD
// sockets (Linux / macOS). No game knowledge.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hotd::net {

struct Address {
    uint32_t ip = 0;     // IPv4, host byte order (0 = any)
    uint16_t port = 0;
    bool operator==(const Address& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const Address& o) const { return !(*this == o); }
    std::string str() const;
};

// "a.b.c.d:port", "name:port" (resolved, IPv4) or, with portOnly, "port" (any interface). False: not an address.
bool parseAddress(const std::string& text, Address& out, bool portOnly, std::string& err);

// this PC's IPv4 interfaces that are up (not the loopback): each address and its subnet's directed broadcast address
// (the LAN announcement goes to every one: a virtual adapter (VPN, Hyper-V, VirtualBox) can take 255.255.255.255)
struct LocalIf { uint32_t ip = 0, broadcast = 0; };
std::vector<LocalIf> localInterfaces();
bool isPrivateIp(uint32_t ip);   // 10/8, 172.16/12, 192.168/16, 169.254/16, 100.64/10 (CGNAT), 127/8

// the logs (natpunch 2026-11-10: players attach hotd.log to issues): every public IPv4 address in text written as
// "public#XXXX" (XXXX: a hash with this run's random salt, so two players can be told apart in one log, nothing
// recovered across runs); the port after it stays. Private / loopback addresses stay readable. HOTD_NET_LOG_IPS=1:
// unchanged (developers). salt 0 = this run's (the tests pass their own)
std::string redactLog(const std::string& text, uint32_t salt = 0);
std::string logAddr(const Address& a);   // redactLog(a.str())

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // port 0: an ephemeral port; shared: other sockets may bind the same port (the LAN lobby's listeners)
    bool open(const Address& bind, std::string& err, bool shared = false);
    void enableBroadcast();
    void close();
    bool isOpen() const { return s_ != kInvalid; }
    bool send(const Address& to, const void* data, size_t n);
    // waits up to timeoutMs (0: just a look) for one datagram: its size, 0 = none, -1 = an error. A datagram larger
    // than cap is dropped (reported as 0).
    int recv(void* buf, size_t cap, Address& from, int timeoutMs);
    Address local() const;

private:
    static constexpr intptr_t kInvalid = -1;
    intptr_t s_ = kInvalid;
};

} // namespace hotd::net
