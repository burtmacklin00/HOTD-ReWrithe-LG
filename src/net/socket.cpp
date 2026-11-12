#include "net/socket.h"

#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
using SockLen = int;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SockLen = socklen_t;
#endif

namespace hotd::net {

namespace {
#ifdef _WIN32
bool startup()
{
    static const bool ok = [] {
        WSADATA d;
        return WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }();
    return ok;
}
using Sock = SOCKET;
#else
bool startup() { return true; }
using Sock = int;
#endif

sockaddr_in toSockaddr(const Address& a)
{
    sockaddr_in s{};
    s.sin_family = AF_INET;
    s.sin_port = htons(a.port);
    s.sin_addr.s_addr = htonl(a.ip);
    return s;
}
} // namespace

std::string Address::str() const
{
    char b[32];
    std::snprintf(b, sizeof b, "%u.%u.%u.%u:%u", ip >> 24, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF, port);
    return b;
}

bool parseAddress(const std::string& text, Address& out, bool portOnly, std::string& err)
{
    const size_t colon = text.rfind(':');
    std::string host, port;
    if (colon == std::string::npos) {
        if (!portOnly) { err = "expected <address>:<port>, got \"" + text + "\""; return false; }
        port = text;
    } else {
        host = text.substr(0, colon);
        port = text.substr(colon + 1);
    }
    if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos ||
        std::atoi(port.c_str()) < 1 || std::atoi(port.c_str()) > 65535) {
        err = "bad port \"" + port + "\" (1-65535)";
        return false;
    }
    out.port = uint16_t(std::atoi(port.c_str()));
    out.ip = 0;
    if (host.empty())
        return true;
    if (!startup()) { err = "the network is not available (WSAStartup)"; return false; }
    in_addr a{};
    if (inet_pton(AF_INET, host.c_str(), &a) == 1) {
        out.ip = ntohl(a.s_addr);
        return true;
    }
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
        err = "cannot resolve \"" + host + "\"";
        return false;
    }
    out.ip = ntohl(reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(res);
    return true;
}

bool isPrivateIp(uint32_t ip)
{
    return (ip >> 24) == 10 || (ip >> 20) == 0xAC1 || (ip >> 16) == 0xC0A8 || (ip >> 16) == 0xA9FE || (ip >> 22) == (0x64400000u >> 22) ||
           (ip >> 24) == 127;
}

std::string redactLog(const std::string& text, uint32_t salt)
{
    if (salt == 0) {
        static const bool showAll = [] { const char* e = std::getenv("HOTD_NET_LOG_IPS"); return e && *e == '1'; }();
        if (showAll) return text;
        static const uint32_t runSalt = [] {
            const uint32_t s = uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()) ^
                               uint32_t(uint64_t(std::chrono::system_clock::now().time_since_epoch().count()) >> 7);
            return s ? s : 0x9E3779B9u;
        }();
        salt = runSalt;
    }
    auto digit = [&](size_t i) { return i < text.size() && text[i] >= '0' && text[i] <= '9'; };
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        // a dotted quad starting here (not inside a longer number, version string or word)
        if (digit(i) && (i == 0 || !(digit(i - 1) || text[i - 1] == '.' || std::isalpha(static_cast<unsigned char>(text[i - 1]))))) {
            size_t j = i;
            uint32_t ip = 0;
            int parts = 0;
            bool ok = true;
            while (parts < 4) {
                size_t k = j;
                uint32_t v = 0;
                while (digit(k) && k - j < 4) v = v * 10 + uint32_t(text[k++] - '0');
                if (k == j || k - j > 3 || v > 255) { ok = false; break; }
                ip = ip << 8 | v;
                j = k;
                if (++parts < 4) {
                    if (j < text.size() && text[j] == '.' && digit(j + 1)) ++j;
                    else { ok = false; break; }
                }
            }
            if (ok && !digit(j) && !(j + 1 < text.size() && text[j] == '.' && digit(j + 1))) {
                if (isPrivateIp(ip) || ip == 0 || ip == 0xFFFFFFFFu) {
                    out.append(text, i, j - i);
                } else {
                    uint32_t h = (ip ^ salt) * 0x9E3779B1u;   // (a keyed mix: the same address = the same tag in one run)
                    h ^= h >> 15, h *= 0x85EBCA77u, h ^= h >> 13;
                    char tag[16];
                    std::snprintf(tag, sizeof tag, "public#%04X", unsigned(h >> 16));
                    out += tag;
                }
                i = j;
                continue;
            }
            while (digit(i) || (i < text.size() && text[i] == '.')) out += text[i++];   // (not an address: as it is)
            continue;
        }
        out += text[i++];
    }
    return out;
}

std::string logAddr(const Address& a)
{
    return redactLog(a.str());
}

std::vector<LocalIf> localInterfaces()
{
    std::vector<LocalIf> r;
    auto add = [&r](uint32_t ip, int prefix) {
        if (!ip || (ip >> 24) == 127 || prefix <= 0 || prefix > 30)
            return;
        const uint32_t mask = ~0u << (32 - prefix);
        for (const LocalIf& x : r)
            if (x.ip == ip) return;
        if (r.size() < 16) r.push_back({ip, ip | ~mask});
    };
#ifdef _WIN32
    if (!startup())
        return r;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf;
    for (int tries = 0; tries < 3; ++tries) {
        buf.resize(size);
        const ULONG e = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                             nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
        if (e == ERROR_BUFFER_OVERFLOW) continue;
        if (e != NO_ERROR) return r;
        for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
                continue;
            for (auto* u = a->FirstUnicastAddress; u; u = u->Next)
                if (u->Address.lpSockaddr && u->Address.lpSockaddr->sa_family == AF_INET)
                    add(ntohl(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr), u->OnLinkPrefixLength);
        }
        break;
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0)
        return r;
    for (ifaddrs* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !a->ifa_netmask || !(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK))
            continue;
        const uint32_t mask = ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_netmask)->sin_addr.s_addr);
        int prefix = 0;
        for (uint32_t m = mask; m & 0x80000000u; m <<= 1) ++prefix;
        add(ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr), prefix);
    }
    freeifaddrs(list);
#endif
    return r;
}

UdpSocket::~UdpSocket() { close(); }

void UdpSocket::enableBroadcast()
{
    if (s_ == kInvalid)
        return;
    int on = 1;
    ::setsockopt(Sock(s_), SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof on);
}

bool UdpSocket::open(const Address& bind, std::string& err, bool shared)
{
    close();
    if (!startup()) { err = "the network is not available (WSAStartup)"; return false; }
    Sock s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) { err = "socket() failed"; return false; }
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    // an ICMP "port unreachable" (the peer not started yet) must not fail the next recvfrom (WSAECONNRESET)
    BOOL no = FALSE;
    DWORD ret = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &no, sizeof no, nullptr, 0, &ret, nullptr, nullptr);
#else
    if (s < 0) { err = "socket() failed"; return false; }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    if (shared) {
        int on = 1;
        ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof on);
    }
    const sockaddr_in a = toSockaddr(bind);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) {
        err = "UDP port " + std::to_string(bind.port) + " is already in use (is another copy of the game hosting?)";
#ifdef _WIN32
        closesocket(s);
#else
        ::close(s);
#endif
        return false;
    }
    s_ = intptr_t(s);
    return true;
}

void UdpSocket::close()
{
    if (s_ == kInvalid)
        return;
#ifdef _WIN32
    closesocket(Sock(s_));
#else
    ::close(Sock(s_));
#endif
    s_ = kInvalid;
}

bool UdpSocket::send(const Address& to, const void* data, size_t n)
{
    if (s_ == kInvalid || n > 65000)
        return false;
    const sockaddr_in a = toSockaddr(to);
    return ::sendto(Sock(s_), static_cast<const char*>(data), int(n), 0, reinterpret_cast<const sockaddr*>(&a), sizeof a) == int(n);
}

int UdpSocket::recv(void* buf, size_t cap, Address& from, int timeoutMs)
{
    if (s_ == kInvalid)
        return -1;
    if (timeoutMs > 0) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(Sock(s_), &set);
        timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
        if (::select(int(s_ + 1), &set, nullptr, nullptr, &tv) <= 0)
            return 0;
    }
    sockaddr_in a{};
    SockLen len = sizeof a;
    const int n = int(::recvfrom(Sock(s_), static_cast<char*>(buf), int(cap), 0, reinterpret_cast<sockaddr*>(&a), &len));
    if (n < 0) {
#ifdef _WIN32
        const int e = WSAGetLastError();
        if (e == WSAEMSGSIZE)   // a datagram bigger than the buffer (cut, consumed): reported as too long, so the
            return int(cap) + 1;  // caller drops it and goes on reading (0 would end its loop: a flood could stall it)
        if (e == WSAEWOULDBLOCK || e == WSAECONNRESET)
            return 0;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED)
            return 0;
#endif
        return -1;
    }
    if (len < SockLen(sizeof a) || a.sin_family != AF_INET)
        return 0;
    from.ip = ntohl(a.sin_addr.s_addr);
    from.port = ntohs(a.sin_port);
    return n;
}

Address UdpSocket::local() const
{
    Address r;
    if (s_ == kInvalid)
        return r;
    sockaddr_in a{};
    SockLen len = sizeof a;
    if (::getsockname(Sock(s_), reinterpret_cast<sockaddr*>(&a), &len) == 0) {
        r.ip = ntohl(a.sin_addr.s_addr);
        r.port = ntohs(a.sin_port);
    }
    return r;
}

} // namespace hotd::net
