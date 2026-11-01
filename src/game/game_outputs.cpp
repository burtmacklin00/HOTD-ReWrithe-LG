// Light Gun Edition: game outputs (game_outputs.h). Adapted from the HOTD2RE Light Gun Edition's outputs.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static void closeSock(uintptr_t s) { closesocket(static_cast<SOCKET>(s)); }
static bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static void closeSock(uintptr_t s) { close(static_cast<int>(s)); }
static bool wouldBlock() { return errno == EWOULDBLOCK || errno == EAGAIN; }
#endif

#include "game/game_outputs.h"

#include <cstddef>

namespace hotd::game {

namespace {
constexpr uintptr_t kNoSocket = ~uintptr_t(0);
constexpr const char* kRomName = "hotdrw";   // the name tools key their game profile on
constexpr uint64_t kLampBlinkMs = 500;
constexpr int kStatePlaying = 5, kStateOutOfLives = 4, kStateWaiting = 9;
constexpr int kModeGame = 4, kModeGameOver = 5;

void setNonBlocking(uintptr_t s)
{
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(static_cast<SOCKET>(s), FIONBIO, &on);
#else
    fcntl(static_cast<int>(s), F_SETFL, fcntl(static_cast<int>(s), F_GETFL, 0) | O_NONBLOCK);
#endif
}
} // namespace

GameOutputs::~GameOutputs() { stop(); }

int GameOutputs::add(const std::string& name)
{
    outputs_.push_back({name, 0});
    return static_cast<int>(outputs_.size()) - 1;
}

bool GameOutputs::start(const GameOutputsConfig& config, std::string* error)
{
    stop();
    config_ = config;
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        if (error) *error = "WSAStartup failed";
        return false;
    }
    wsa_ = true;
#endif
    const socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) {
#else
    if (s < 0) {
#endif
        if (error) *error = "socket() failed";
        stop();
        return false;
    }
    // Loopback only: the tools run on this PC, and nothing on the network can reach the port.
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(config_.port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#ifndef _WIN32
    int one = 1;   // (a quick restart: the old connections' TIME_WAIT must not block the port)
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(s, 4) != 0) {
        if (error) *error = "port " + std::to_string(config_.port) + " is in use (is DemulShooter or MAME running?)";
        closeSock(static_cast<uintptr_t>(s));
        stop();
        return false;
    }
    listener_ = static_cast<uintptr_t>(s);
    setNonBlocking(listener_);

    outputs_.clear();
    for (int i = 0; i < 2; ++i) {
        const std::string pre = i == 0 ? "P1_" : "P2_";
        lamp_[i] = add(pre + "CtmLmpStart");
        ammo_[i] = add(pre + "Ammo");
        clip_[i] = add(pre + "Clip");
        recoil_[i] = add(pre + "CtmRecoil");
        life_[i] = add(pre + "Life");
        damaged_[i] = add(pre + "Damaged");
        dead_[i] = add(pre + "Dead");
        recoilPulse_[i] = Pulse{recoil_[i], 0, false, 0, config_.recoilOnMs, config_.recoilOffMs};
        damagedPulse_[i] = Pulse{damaged_[i], 0, false, 0, config_.damagedMs, 0};
        track_[i] = Track{};
        lampBlink_[i] = true;
    }
    return true;
}

void GameOutputs::stop()
{
    for (uintptr_t c : clients_) {
        sendTo(c, line("mame_stop", 1));
        closeSock(c);
    }
    clients_.clear();
    if (listener_ != kNoSocket) closeSock(listener_);
    listener_ = kNoSocket;
#ifdef _WIN32
    if (wsa_) WSACleanup();
#endif
    wsa_ = false;
}

std::string GameOutputs::line(const std::string& name, int value) { return name + " = " + std::to_string(value) + "\r"; }

bool GameOutputs::sendTo(uintptr_t client, const std::string& text)
{
    const char* data = text.data();
    int left = static_cast<int>(text.size());
    int stalls = 0;
    while (left > 0) {
#ifdef MSG_NOSIGNAL
        const int n = static_cast<int>(send(static_cast<socket_t>(client), data, left, MSG_NOSIGNAL));
#else
        const int n = static_cast<int>(send(static_cast<socket_t>(client), data, left, 0));
#endif
        if (n > 0) {
            data += n;
            left -= n;
            continue;
        }
        // Tiny messages: a full buffer means the tool stopped reading. Drop it rather than stall the game.
        if (n < 0 && wouldBlock() && ++stalls < 64) continue;
        return false;
    }
    return true;
}

void GameOutputs::sendAll(const std::string& text)
{
    for (size_t i = 0; i < clients_.size();) {
        if (sendTo(clients_[i], text)) {
            ++i;
        } else {
            closeSock(clients_[i]);
            clients_.erase(clients_.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

void GameOutputs::set(int output, int value)
{
    if (output < 0) return;
    Output& o = outputs_[static_cast<size_t>(output)];
    if (o.value == value) return;
    o.value = value;
    sendAll(line(o.name, value));
}

void GameOutputs::acceptClients()
{
    for (;;) {
        const socket_t c = accept(static_cast<socket_t>(listener_), nullptr, nullptr);
#ifdef _WIN32
        if (c == INVALID_SOCKET) break;
#else
        if (c < 0) break;
#endif
        const uintptr_t client = static_cast<uintptr_t>(c);
        setNonBlocking(client);
        int one = 1;
        setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
        std::string hello = line("mame_stop", 1) + "mame_start = " + kRomName + "\r";
        for (const Output& o : outputs_) hello += line(o.name, o.value);
        if (sendTo(client, hello)) clients_.push_back(client);
        else closeSock(client);
    }
    // Drain anything the tools send (MAMEHooker-style requests are not needed) and notice closed connections.
    for (size_t i = 0; i < clients_.size();) {
        char buf[256];
        const int n = static_cast<int>(recv(static_cast<socket_t>(clients_[i]), buf, sizeof(buf), 0));
        if (n == 0 || (n < 0 && !wouldBlock())) {
            closeSock(clients_[i]);
            clients_.erase(clients_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

void GameOutputs::fire(Pulse& pulse, uint64_t nowMs)
{
    if (pulse.on || pulse.until > nowMs) {
        if (pulse.queued < 3) ++pulse.queued;   // rapid fire: separate pulses, never merged into one
        return;
    }
    pulse.on = true;
    pulse.until = nowMs + static_cast<uint64_t>(pulse.onMs);
    set(pulse.output, 1);
}

void GameOutputs::runPulse(Pulse& pulse, uint64_t nowMs)
{
    if (pulse.until > nowMs) return;
    if (pulse.on) {
        pulse.on = false;
        pulse.until = nowMs + static_cast<uint64_t>(pulse.offMs);
        set(pulse.output, 0);
    } else if (pulse.queued > 0) {
        --pulse.queued;
        pulse.on = true;
        pulse.until = nowMs + static_cast<uint64_t>(pulse.onMs);
        set(pulse.output, 1);
    }
}

void GameOutputs::onFrame(const OutputFrame& f, bool live, uint64_t nowMs)
{
    if (!running()) return;
    const bool inGame = f.gameMode == kModeGame;
    const bool gameOver = f.gameMode == kModeGameOver;
    for (int out = 0; out < 2; ++out) {
        const OutputSeat& p = f.seat[out];
        Track& t = track_[out];
        const bool playing = p.valid && inGame && p.state == kStatePlaying;
        const bool dead = p.valid && t.wasPlaying && ((inGame && p.state == kStateOutOfLives) || gameOver);

        // One pulse per bullet: the flag is set only by a real shot (trigger, gun on screen, rounds left) and cleared
        // at the start of every frame, so a reload, an off-screen pull or an empty gun never kicks.
        if (live && playing && p.fired) fire(recoilPulse_[out], nowMs);
        // A life lost while playing. Not on the hit that takes the last life: that one is the death, which Hook of
        // the Reaper's Death_* light commands show from P_Life reaching 0; a Damaged effect on the same lights at the
        // same moment would block it.
        if (live && inGame && p.valid && t.wasPlaying && t.lastLives >= 0 && p.lives < t.lastLives && p.lives > 0)
            fire(damagedPulse_[out], nowMs);

        set(ammo_[out], playing ? p.rounds : 0);
        set(clip_[out], playing && p.rounds > 0 ? 1 : 0);
        set(life_[out], playing ? p.lives : 0);
        set(dead_[out], dead ? 1 : 0);
        lampBlink_[out] = p.valid && !playing;

        if (playing) t.wasPlaying = true;
        else if (!p.valid || (!inGame && !gameOver) || (inGame && p.state == kStateWaiting)) t.wasPlaying = false;
        t.lastLives = p.valid && inGame ? p.lives : -1;
    }
}

void GameOutputs::tick(uint64_t nowMs)
{
    if (!running()) return;
    acceptClients();
    for (int i = 0; i < 2; ++i) {
        runPulse(recoilPulse_[i], nowMs);
        runPulse(damagedPulse_[i], nowMs);
        set(lamp_[i], lampBlink_[i] ? static_cast<int>((nowMs / kLampBlinkMs) & 1) : 0);
    }
}

} // namespace hotd::game
