// Light guns (pads.h LightGuns): absolute pointer devices per player through Windows' raw input, calibration. Ours.
#include "game/pads.h"

#include "game/runtime.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hotd::game {

bool lightGunInPlay(const LaunchConfig& c)
{
    return c.p1Input == 3 || c.p2Input == 3 || (c.players >= 3 && (c.p3Input == 3 || c.p4Input == 3));
}

bool gunFlashOn(const LaunchConfig& c)
{
    return c.gunFlash == 2 || (c.gunFlash == 0 && c.gunKind == 2 && lightGunInPlay(c));
}

int gunBorderPx(const LaunchConfig& c, int wh)
{
    const int k = c.gunBorder == 0 ? (c.gunKind == 1 && lightGunInPlay(c) ? 3 : 1) : c.gunBorder;
    static const float kFrac[5] = {0, 0, 0.012f, 0.022f, 0.035f};   // (Thin / Medium / Thick of the window's height)
    return k >= 2 ? std::max(2, int(std::lround(float(wh) * kFrac[std::clamp(k, 0, 4)]))) : 0;
}

void LightGuns::configure(const LaunchConfig& c)
{
    want[0] = c.p1Input == 3;
    want[1] = c.p2Input == 3;
    mouseWant[0] = c.p1Input == 0;
    mouseWant[1] = c.p2Input == 0;
    // the 4P mode (docs/coop4.md): players 3 / 4 with a light gun or a mouse of their own (both through raw input)
    want[2] = c.players >= 3 && c.p3Input == 3;
    want[3] = c.players >= 3 && c.p4Input == 3;
    mouseWant[2] = c.players >= 3 && c.p3Input == 0;
    mouseWant[3] = c.players >= 3 && c.p4Input == 0;
    for (int p = 0; p < kInputPlayers; ++p) {
        std::copy(c.gunCal[p], c.gunCal[p] + 4, cal[p]);
        pin[p] = c.gunDev[p];
    }
}

bool LightGuns::active() const
{
    if (mouseWant[2] || mouseWant[3])   // the 4P mode: players 3 / 4's own mice need raw input too
        return true;
    for (bool w : want)
        if (w) return true;
    return false;
}

LightGuns::Dev& LightGuns::dev(uint64_t id)
{
    for (Dev& d : devs_)
        if (d.id == id) return d;
    devs_.push_back(Dev{});
    devs_.back().id = id;
    return devs_.back();
}

void LightGuns::place(Dev& d)
{
    if (d.player >= 0)
        return;
    // ipolish: the seat that was given this device (Assign): its own, whatever took the seat first (that one goes on
    // to the next free seat at its next input)
    if (!d.key.empty())
        for (int p = 0; p < kInputPlayers; ++p) {
            if (pin[p] != d.key || !(want[p] || mouseWant[p]))
                continue;
            for (Dev& o : devs_)
                if (o.player == p) o.player = -1;
            d.player = p;
            SDL_Log("light guns: player %d: its assigned device %s (%llu)", p + 1, d.name.c_str(), (unsigned long long)d.id);
            return;
        }
    for (int p = 0; p < kInputPlayers; ++p) {
        if (!(d.absolute ? want[p] : mouseWant[p]))
            continue;
        if (!pin[p].empty() && connected(pin[p]))
            continue;   // (ipolish) kept for its own device
        bool taken = false;
        for (const Dev& o : devs_)
            taken = taken || o.player == p;
        if (!taken) {
            d.player = p;
            SDL_Log("light guns: player %d: %s device %llu", p + 1, d.absolute ? "absolute (gun)" : "relative (mouse)",
                    (unsigned long long)d.id);
            return;
        }
    }
}

void LightGuns::absolute(uint64_t id, float u, float v)
{
    Dev& d = dev(id);
    if (!menuKey_.empty() && !capture_ && assignSeat_ < 0) {   // Light Gun Edition: the menus follow Player 1's gun only
        if (d.key == menuKey_) menu_.u = u, menu_.v = v, menu_.moved = true;
        return;
    }
    if (!d.absolute) {
        d.absolute = true;
        if (d.player >= 0 && !want[d.player] && (d.key.empty() || pin[d.player] != d.key))
            d.player = -1;   // it moved as a mouse first (a button): a gun after all
    }
    place(d);
    d.u = u;
    d.v = v;
}

void LightGuns::relative(uint64_t id, float dx, float dy)
{
    Dev& d = dev(id);
    if (d.absolute)
        return;
    place(d);
    d.dx += dx;
    d.dy += dy;
}

void LightGuns::buttons(uint64_t id, unsigned flags)
{
    Dev& d = dev(id);
    if (capture_) {   // Light Gun Edition: LightgunHID.ini's set-up waits for a trigger pull (nothing else happens)
        if (flags & 0x1) {
            if (d.key.empty())
                d.key = "raw:" + std::to_string(id);
            if (d.name.empty())
                d.name = d.absolute ? "Light Gun" : "Mouse";
            captured = Captured{true, d.key, d.name};
            capture_ = false;
        }
        return;
    }
    if (!menuKey_.empty() && assignSeat_ < 0) {   // Light Gun Edition: the menus follow Player 1's gun only
        if (d.key == menuKey_) {
            if (flags & 0x1) menu_.trigger = menu_.held = true;
            if (flags & 0x2) menu_.held = false;
            if (flags & 0x4) menu_.alt = true;
        }
        return;
    }
    if (assignSeat_ >= 0) {   // ipolish: the Assign page waits: a trigger pull takes the seat (no shot, no button)
        if (flags & 0x1) {
            const int s = assignSeat_;
            assignSeat_ = -1;
            if (d.key.empty())
                d.key = "raw:" + std::to_string(id);
            if (d.name.empty())
                d.name = d.absolute ? "Light Gun" : "Mouse";
            for (int p = 0; p < kInputPlayers; ++p)
                if (pin[p] == d.key) pin[p].clear();   // (one device, one seat)
            pin[s] = d.key;
            for (Dev& o : devs_)
                if (o.player == s) o.player = -1;
            d.player = -1;
            place(d);
            assigned = Assigned{s, d.key, d.name};
            SDL_Log("light guns: player %d assigned: %s (%s)", s + 1, d.name.c_str(), d.key.c_str());
        }
        return;
    }
    place(d);
    if (flags & 0x1) {
        if (calibrating())
            calibrationShot(d);   // (no shot)
        else
            d.eTrigger = true;
    }
    if (flags & 0x4) d.eReload = true;
    if (flags & 0x10) d.eReload = true;   // Light Gun Edition: the middle button reloads (the trigger is Start)
}

#ifdef _WIN32
namespace {
// a raw-input device's path (\\?\HID#VID_...#...#{...}): the same device keeps it from run to run (lower case)
std::string rawDevicePath(HANDLE h)
{
    UINT n = 0;
    if (GetRawInputDeviceInfoA(h, RIDI_DEVICENAME, nullptr, &n) != 0 || n == 0 || n > 1024)
        return {};
    std::string s(n, '\0');
    if (GetRawInputDeviceInfoA(h, RIDI_DEVICENAME, s.data(), &n) == UINT(-1))
        return {};
    s.resize(std::strlen(s.c_str()));
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// the name shown: the HID product string (hid.dll's HidD_GetProductString), else its vendor / product numbers
std::string rawDeviceName(const std::string& path)
{
    using GetProduct = BOOLEAN(__stdcall*)(HANDLE, PVOID, ULONG);
    static GetProduct get = []() -> GetProduct {
        HMODULE hid = LoadLibraryA("hid.dll");
        return hid ? reinterpret_cast<GetProduct>(reinterpret_cast<void*>(GetProcAddress(hid, "HidD_GetProductString"))) : nullptr;
    }();
    if (get) {
        HANDLE f = CreateFileA(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            wchar_t w[128] = {};
            const bool ok = get(f, w, sizeof w - sizeof w[0]);
            CloseHandle(f);
            std::string s;
            for (int i = 0; ok && i < 127 && w[i]; ++i)
                s += w[i] >= 32 && w[i] < 127 ? char(w[i]) : '?';
            while (!s.empty() && s.back() == ' ') s.pop_back();
            if (!s.empty())
                return s;
        }
    }
    const size_t v = path.find("vid_"), p = path.find("pid_");
    if (v != std::string::npos && p != std::string::npos && v + 8 <= path.size() && p + 8 <= path.size()) {
        std::string s = path.substr(v + 4, 4) + ":" + path.substr(p + 4, 4);
        for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
        return "USB Device " + s;
    }
    return "Mouse Device";
}
}

void LightGuns::winRawInput(const void* raw, void* hwnd)
{
    const RAWINPUT& ri = *static_cast<const RAWINPUT*>(raw);
    if (ri.header.dwType != RIM_TYPEMOUSE || !ri.header.hDevice)
        return;
    const RAWMOUSE& m = ri.data.mouse;
    const uint64_t id = uint64_t(reinterpret_cast<uintptr_t>(ri.header.hDevice));
    if (dev(id).key.empty()) {   // ipolish: its lasting name (the device path) and the name shown, once
        const std::string path = rawDevicePath(ri.header.hDevice);
        if (!path.empty())
            describe(id, path, rawDeviceName(path));
    }
    if (m.usFlags & MOUSE_MOVE_ABSOLUTE) {
        float u = -0.05f, v = -0.05f;   // 0, 0: what several guns report off the screen
        if (m.lLastX || m.lLastY) {
            const bool desk = (m.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
            const int x0 = desk ? GetSystemMetrics(SM_XVIRTUALSCREEN) : 0, y0 = desk ? GetSystemMetrics(SM_YVIRTUALSCREEN) : 0;
            const int w = GetSystemMetrics(desk ? SM_CXVIRTUALSCREEN : SM_CXSCREEN), h = GetSystemMetrics(desk ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
            const float sx = x0 + float(m.lLastX) * float(w) / 65535.0f, sy = y0 + float(m.lLastY) * float(h) / 65535.0f;
            RECT rc;
            POINT o{0, 0};
            if (GetClientRect(static_cast<HWND>(hwnd), &rc) && ClientToScreen(static_cast<HWND>(hwnd), &o) && rc.right > 0 && rc.bottom > 0) {
                u = (sx - float(o.x)) / float(rc.right);
                v = (sy - float(o.y)) / float(rc.bottom);
            }
        }
        absolute(id, u, v);
    } else if (m.lLastX || m.lLastY) {
        relative(id, float(m.lLastX), float(m.lLastY));
    }
    const unsigned b = m.usButtonFlags & 0x3F;
    if (b)
        buttons(id, b);
}
#endif

void LightGuns::describe(uint64_t id, const std::string& key, const std::string& name)
{
    Dev& d = dev(id);
    d.key = key;
    d.name = name;
}

bool LightGuns::connected(const std::string& key)
{
    if (key.empty())
        return false;
    for (const Dev& d : devs_)
        if (d.key == key) return true;
#ifdef _WIN32
    const uint64_t now = SDL_GetTicks();
    if (!listedOnce_ || now - listedAt_ >= 1000) {
        listedOnce_ = true;
        listedAt_ = now;
        listed_.clear();
        UINT n = 0;
        if (GetRawInputDeviceList(nullptr, &n, sizeof(RAWINPUTDEVICELIST)) == 0 && n > 0 && n < 256) {
            std::vector<RAWINPUTDEVICELIST> l(n);
            const UINT got = GetRawInputDeviceList(l.data(), &n, sizeof(RAWINPUTDEVICELIST));
            for (UINT i = 0; got != UINT(-1) && i < got; ++i)
                if (l[i].dwType == RIM_TYPEMOUSE)
                    listed_.push_back(rawDevicePath(l[i].hDevice));
        }
    }
    return std::find(listed_.begin(), listed_.end(), key) != listed_.end();
#else
    return false;
#endif
}

void LightGuns::reset()
{
    for (Dev& d : devs_) {
        d.player = -1;
        d.dx = d.dy = 0;
        d.eTrigger = d.eReload = d.eStart = false;
    }
    devs_.clear();   // assigned again in the order used
    calPlayer_ = -1;
}

void LightGuns::apply(Runtime& rt, GunState guns[], bool paused, int lw, int lh)
{
    for (Dev& d : devs_) {
        const int p = d.player;
        if (p < 0) {
            d.dx = d.dy = 0;
            d.eTrigger = d.eReload = d.eStart = false;
            continue;
        }
        GunState& g = guns[p];
        if (d.absolute) {
            float u = d.u, v = d.v;
            if (!calibrating()) {   // the calibration: raw at its targets -> the targets
                const float* c = cal[p];
                if (std::abs(c[2] - c[0]) > 0.05f && std::abs(c[3] - c[1]) > 0.05f) {
                    u = 0.1f + (u - c[0]) * 0.8f / (c[2] - c[0]);
                    v = 0.1f + (v - c[1]) * 0.8f / (c[3] - c[1]);
                }
            }
            const float m = lh * 0.05f;
            g.x = std::clamp(u * float(lw), -m, float(lw) + m);
            g.y = std::clamp(v * float(lh), -m, float(lh) + m);
        } else {
            const float m = lh * 0.05f;
            g.x = std::clamp(g.x + d.dx, -m, float(lw) + m);
            g.y = std::clamp(g.y + d.dy, -m, float(lh) + m);
        }
        d.dx = d.dy = 0;
        if (!paused && !calibrating()) {
            if (d.eTrigger) {
                g.trigger = true;
                // Light Gun Edition: the trigger is Start too for a player not in play (9 waiting, 4 out of lives:
                // join / continue), as the other light gun games of the set; in play it only shoots
                if (triggerStarts && p < rt.seat_count()) {
                    const int32_t st = rt.mem.at<port::a::Player>(rt.player_rec(p)).state;
                    if (st == 9 || st == 4) { if (onJoin) onJoin(p); else rt.player_join(p); }
                }
            }
            if (d.eReload) g.reload = true;
            if (d.eStart) { if (onJoin) onJoin(p); else rt.player_join(p); }
        }
        d.eTrigger = d.eReload = d.eStart = false;
    }
}

bool LightGuns::takeMenu(int lw, int lh, bool& moved, float& x, float& y, bool& trigger, bool& held, bool& alt)
{
    moved = menu_.moved, trigger = menu_.trigger, held = menu_.held, alt = menu_.alt;
    menu_.moved = menu_.trigger = menu_.alt = false;
    float u = menu_.u, v = menu_.v;
    const float* c = cal[0];   // Player 1's calibration, as in play
    if (std::abs(c[2] - c[0]) > 0.05f && std::abs(c[3] - c[1]) > 0.05f) {
        u = 0.1f + (u - c[0]) * 0.8f / (c[2] - c[0]);
        v = 0.1f + (v - c[1]) * 0.8f / (c[3] - c[1]);
    }
    x = std::clamp(u, 0.0f, 1.0f) * float(lw);
    y = std::clamp(v, 0.0f, 1.0f) * float(lh);
    return moved || trigger || alt;
}

uint64_t LightGuns::deviceOf(int p) const
{
    for (const Dev& d : devs_)
        if (d.player == p) return d.id;
    return 0;
}

void LightGuns::startCalibration()
{
    calPlayer_ = -1;
    for (int p = 0; p < kInputPlayers; ++p)
        if (want[p]) { calPlayer_ = p; break; }
    calStep_ = 0;
    if (calPlayer_ >= 0)
        SDL_Log("light guns: calibration (player %d first)", calPlayer_ + 1);
}

bool LightGuns::calibrationTarget(float& u, float& v) const
{
    if (calPlayer_ < 0)
        return false;
    u = v = calStep_ == 0 ? 0.1f : 0.9f;
    return true;
}

std::vector<std::string> LightGuns::calibrationLines() const
{
    if (calPlayer_ < 0)
        return {};
    return {"LIGHT GUN CALIBRATION", "",
            "Player " + std::to_string(calPlayer_ + 1) + ": shoot the centre of the target (" +
                (calStep_ == 0 ? "top left" : "bottom right") + ")",
            "", "Esc: cancel"};
}

void LightGuns::calibrationShot(Dev& d)
{
    if (d.player != calPlayer_ || !d.absolute)
        return;
    calRead_[calPlayer_][calStep_ * 2] = d.u;
    calRead_[calPlayer_][calStep_ * 2 + 1] = d.v;
    if (++calStep_ < 2)
        return;
    const float* r = calRead_[calPlayer_];
    if (std::abs(r[2] - r[0]) > 0.05f && std::abs(r[3] - r[1]) > 0.05f) {
        std::copy(r, r + 4, cal[calPlayer_]);
        SDL_Log("light guns: player %d calibrated: (%.3f, %.3f) (%.3f, %.3f)", calPlayer_ + 1, r[0], r[1], r[2], r[3]);
        calibrated = true;
    } else {
        SDL_Log("light guns: player %d: the two shots were too close together; calibration kept as it was", calPlayer_ + 1);
    }
    calStep_ = 0;
    int next = -1;
    for (int p = calPlayer_ + 1; p < kInputPlayers; ++p)
        if (want[p]) { next = p; break; }
    calPlayer_ = next;
}

} // namespace hotd::game
