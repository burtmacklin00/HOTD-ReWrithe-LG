// Light guns (pads.h LightGuns): absolute pointer devices per player through Windows' raw input (Linux: evdev), calibration. Ours.
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
#ifdef __linux__
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
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
    if (evdevMice_)   // (linuxmice: Linux's second mouse used: both mice through evdev)
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
        d.u = u, d.v = v;   // (where every gun points: PC Mode's select reads a pull off the screen)
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
    if (capture_) {   // Light Gun Edition: LightgunHID.ini's set-up waits for a trigger pull or a reload button (nothing else happens)
        if (flags & (0x1 | 0x4 | 0x10)) {
            if (d.key.empty())
                d.key = "raw:" + std::to_string(id);
            if (d.name.empty())
                d.name = d.absolute ? "Light Gun" : "Mouse";
            captured = Captured{true, d.key, d.name, (flags & 0x1) == 0, d.absolute, d.u, d.v};
            capture_ = false;
        }
        return;
    }
    if (!menuKey_.empty() && assignSeat_ < 0) {   // Light Gun Edition: the menus follow Player 1's gun only
        const bool off = d.u < 0.0f || d.u > 1.0f || d.v < 0.0f || d.v > 1.0f;
        if (d.key == menuKey_) {
            if (seatPick_ == 4 && ((flags & 0x14) || ((flags & 0x1) && off))) {   // (PC Mode, 4 panels: player 1's Ready)
                seatPress[0].reload = true;
                return;
            }
            if (flags & 0x1) menu_.trigger = menu_.held = true;
            if (flags & 0x2) menu_.held = false;
            if (flags & 0x14) menu_.alt = true;   // (the right or the middle button: both are reload)
        } else if (seatPick_ && !d.key.empty()) {   // PC Mode's select: players 2-4's own guns on their own panels
            for (int p = 1; p < (seatPick_ == 4 ? 4 : 2); ++p)
                if (pin[p] == d.key) {
                    if ((flags & 0x1) && !off) seatPress[p].trigger = true;
                    if ((flags & 0x14) || ((flags & 0x1) && off)) seatPress[p].reload = true;
                }
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
    if (flags & 0x1) d.held = true;    // (Light Gun Edition: Machine Gun mode)
    if (flags & 0x2) d.held = false;
    if (flags & 0x1) {
        if (calibrating())
            calibrationShot(d);   // (no shot)
        else
            d.eTrigger = true;
    }
    if (flags & 0x4) d.eReload = true;
    if (flags & 0x10) (weaponSwitch ? d.eWeapon : d.eReload) = true;   // Light Gun Edition: the middle button reloads,
                                                                        // or with Arsenal Mode the next weapon
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
#elif defined(__linux__)
    for (const EvNode& n : evNodes_)
        if (n.key == key) return true;
    const uint64_t now = SDL_GetTicks();   // (as Windows': the pointer devices listed, each second)
    if (!listedOnce_ || now - listedAt_ >= 1000) {
        listedOnce_ = true;
        listedAt_ = now;
        listed_.clear();
        evdevScan(nullptr, &listed_);
    }
    return std::find(listed_.begin(), listed_.end(), key) != listed_.end();
#else
    return false;
#endif
}

// linuxmice (GitHub #67): evdev, portable part. Relative counts move a mouse player's crosshair as Windows' raw
// mickeys do; an absolute device's range is the window (0, 0: off the screen, as several guns report on Windows)
LightGuns::Evd& LightGuns::evd(uint64_t id)
{
    for (Evd& e : evd_)
        if (e.id == id) return e;
    evd_.push_back(Evd{});
    evd_.back().id = id;
    return evd_.back();
}

void LightGuns::evdevRange(uint64_t id, int minX, int maxX, int minY, int maxY)
{
    Evd& e = evd(id);
    e.minX = minX, e.maxX = maxX, e.minY = minY, e.maxY = maxY;
    e.x = minX, e.y = minY;
}

void LightGuns::evdevEvent(uint64_t id, unsigned type, unsigned code, int value)
{
    // (linux/input.h's numbers, here so that the input test runs anywhere)
    constexpr unsigned kSyn = 0x00, kKey = 0x01, kRel = 0x02, kAbs = 0x03;
    constexpr unsigned kSynReport = 0, kSynDropped = 3;
    constexpr unsigned kBtnLeft = 0x110, kBtnRight = 0x111, kBtnMiddle = 0x112;
    Evd& e = evd(id);
    if (e.dropping) {   // (after SYN_DROPPED: everything up to the next SYN_REPORT is skipped, as the kernel asks)
        if (type == kSyn && code == kSynReport)
            e.dropping = false;
        return;
    }
    if (type == kRel) {
        if (code == 0) e.dx += float(value);        // REL_X
        else if (code == 1) e.dy += float(value);   // REL_Y
    } else if (type == kAbs) {
        if (code == 0) e.x = value, e.absMoved = true;        // ABS_X
        else if (code == 1) e.y = value, e.absMoved = true;   // ABS_Y
    } else if (type == kKey && value != 2) {   // (2: the key's auto-repeat)
        const bool down = value != 0;
        if (code == kBtnLeft) e.flags |= down ? 0x1u : 0x2u;
        else if (code == kBtnRight) e.flags |= down ? 0x4u : 0x8u;
        else if (code == kBtnMiddle) e.flags |= down ? 0x10u : 0x20u;
    } else if (type == kSyn && code == kSynDropped) {   // the kernel's buffer ran over: this packet is incomplete
        e.dx = e.dy = 0;
        e.flags = 0;
        e.absMoved = false;
        e.dropping = true;
    } else if (type == kSyn && code == kSynReport) {
        const bool feed = active() || assignSeat_ >= 0;   // (else no player reads raw input: counted only)
        if (e.absMoved) {
            float u = -0.05f, v = -0.05f;
            if (e.maxX > e.minX && e.maxY > e.minY && (e.x != e.minX || e.y != e.minY)) {
                u = float(e.x - e.minX) / float(e.maxX - e.minX);
                v = float(e.y - e.minY) / float(e.maxY - e.minY);
            }
            if (feed) absolute(id, u, v);
        } else if ((e.dx != 0 || e.dy != 0) && feed) {
            relative(id, e.dx, e.dy);
        }
        if (e.flags && feed)
            buttons(id, e.flags);
        if (!feed && (e.dx != 0 || e.dy != 0 || (e.flags & 0x15)) && std::find(seen_.begin(), seen_.end(), id) == seen_.end())
            seen_.push_back(id);   // (not active: only counted, for evdevMiceSeen)
        e.dx = e.dy = 0;
        e.flags = 0;
        e.absMoved = false;
    }
}

void LightGuns::evdevMiceOn()
{
    if (evdevMice_)
        return;
    evdevMice_ = true;
    for (Dev& d : devs_)   // (nothing old: the counted motion was the desktop cursor's)
        d.dx = d.dy = 0, d.eTrigger = d.eReload = d.eStart = false;
    for (uint64_t id : seen_)   // the order used: player 1, player 2
        place(dev(id));
}

#ifdef __linux__
namespace {
bool bitOn(const unsigned long* bits, unsigned n)
{
    constexpr unsigned kLong = unsigned(sizeof(unsigned long) * 8);
    return (bits[n / kLong] >> (n % kLong)) & 1ul;
}
// a stable 64-bit number for a device's lasting name (FNV-1a; never 0)
uint64_t evdevId(const std::string& key)
{
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : key) h = (h ^ c) * 1099511628211ull;
    return h ? h : 1;
}
}

// every /dev/input/event* that is a mouse or a light gun (a left button and X / Y motion, relative or absolute; a
// touchpad's finger tool and the pads' sticks are no gun): open: kept open (its id, name and range told), else only
// its lasting name listed. The note: why none can be read
void LightGuns::evdevScan(std::vector<EvNode>* open, std::vector<std::string>* keys)
{
    DIR* dir = opendir("/dev/input");
    int denied = 0, pointers = 0;
    if (dir) {
        for (dirent* de; (de = readdir(dir)) != nullptr;) {
            if (std::strncmp(de->d_name, "event", 5) != 0)
                continue;
            const std::string path = std::string("/dev/input/") + de->d_name;
            bool known = false;
            for (const EvNode& n : evNodes_)
                known = known || n.path == path;
            if (known) {
                ++pointers;
                continue;
            }
            const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) {
                if (errno == EACCES || errno == EPERM) ++denied;
                continue;
            }
            constexpr size_t kLongs = (KEY_MAX + 1 + sizeof(unsigned long) * 8 - 1) / (sizeof(unsigned long) * 8);
            unsigned long ev[kLongs] = {}, key[kLongs] = {}, rel[kLongs] = {}, abs[kLongs] = {};
            ioctl(fd, EVIOCGBIT(0, sizeof ev), ev);
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof key), key);
            ioctl(fd, EVIOCGBIT(EV_REL, sizeof rel), rel);
            ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs);
            const bool relXY = bitOn(ev, EV_REL) && bitOn(rel, REL_X) && bitOn(rel, REL_Y);
            const bool absXY = bitOn(ev, EV_ABS) && bitOn(abs, ABS_X) && bitOn(abs, ABS_Y) && !bitOn(key, BTN_TOOL_FINGER);
            if (!bitOn(ev, EV_KEY) || !bitOn(key, BTN_LEFT) || !(relXY || absXY)) {
                ::close(fd);
                continue;
            }
            ++pointers;
            char name[256] = {}, phys[256] = {};
            ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
            ioctl(fd, EVIOCGPHYS(sizeof phys - 1), phys);
            input_id iid{};
            ioctl(fd, EVIOCGID, &iid);
            char vp[32];
            std::snprintf(vp, sizeof vp, "%04x:%04x", unsigned(iid.vendor), unsigned(iid.product));
            // its lasting name: vendor:product and where it is plugged in (the same port: the same seat next time)
            const std::string k = std::string("evdev:") + vp + ":" + (*phys ? std::string(phys) : path);
            if (keys)
                keys->push_back(k);
            if (!open) {
                ::close(fd);
                continue;
            }
            EvNode n;
            n.fd = fd;
            n.path = path;
            n.key = k;
            n.id = evdevId(k);
            n.name = *name ? std::string(name) : std::string("Mouse");
            describe(n.id, k, n.name);
            if (absXY && !relXY) {
                input_absinfo ax{}, ay{};
                ioctl(fd, EVIOCGABS(ABS_X), &ax);
                ioctl(fd, EVIOCGABS(ABS_Y), &ay);
                evdevRange(n.id, ax.minimum, ax.maximum, ay.minimum, ay.maximum);
            }
            SDL_Log("light guns (evdev): %s: %s, %s (%s)", path.c_str(), *name ? name : "(no name)",
                    absXY && !relXY ? "absolute (gun)" : "relative (mouse)", k.c_str());
            open->push_back(n);
        }
        closedir(dir);
    }
    std::string note;
    if (pointers == 0)
        note = denied > 0 ? "Linux: no access to the mice: add yourself to the input group (sudo usermod -aG input $USER), "
                            "then log out and in"
                          : "Linux: no mouse or light gun found in /dev/input";
    if (open && note != evdevNote_) {
        if (!note.empty())
            SDL_Log("light guns (evdev): %s (%d device(s) in /dev/input could not be opened)", note.c_str(), denied);
        evdevNote_ = note;
    }
}
#endif

void LightGuns::evdevPoll(bool on)
{
#ifdef __linux__
    if (!on) {
        for (EvNode& n : evNodes_) ::close(n.fd);
        evNodes_.clear();
        evd_.clear();
        evScanAt_ = 0;
        return;
    }
    const uint64_t now = SDL_GetTicks();
    if (evScanAt_ == 0 || now - evScanAt_ >= 2000) {   // (first: at once; then new devices every 2 s)
        evScanAt_ = now ? now : 1;
        evdevScan(&evNodes_, nullptr);
    }
    for (size_t i = 0; i < evNodes_.size();) {
        EvNode& n = evNodes_[i];
        input_event buf[64];
        bool gone = false;
        for (;;) {
            const ssize_t got = ::read(n.fd, buf, sizeof buf);
            if (got <= 0) {
                gone = got == 0 || (errno != EAGAIN && errno != EINTR);   // (ENODEV: unplugged)
                break;
            }
            if (dev(n.id).key.empty())   // (reset() forgot it: a new game; as Windows' raw input, named again)
                describe(n.id, n.key, n.name);
            for (size_t k = 0; k < size_t(got) / sizeof buf[0]; ++k)
                evdevEvent(n.id, buf[k].type, buf[k].code, buf[k].value);
        }
        if (gone) {
            SDL_Log("light guns (evdev): %s gone", n.path.c_str());
            ::close(n.fd);
            evNodes_.erase(evNodes_.begin() + std::ptrdiff_t(i));
        } else {
            ++i;
        }
    }
#else
    (void)on;
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
    evdevMice_ = false;   // (linuxmice: the desktop cursor again until a second mouse is used)
    seen_.clear();
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
        g.held = d.held && !paused && !calibrating();
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
            if (d.eWeapon) g.weaponNext = true;
            if (d.eStart) { if (onJoin) onJoin(p); else rt.player_join(p); }
        }
        d.eTrigger = d.eReload = d.eStart = d.eWeapon = false;
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
    // Light Gun Edition: a pull off the screen (the reload habit) clicks nothing in the menus: it would land on the
    // edge (often Back / Exit in a corner)
    if (trigger && (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f))
        trigger = false;
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
