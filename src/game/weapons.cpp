// Weapons (ours): see weapons.h and docs/weapons.md
#include "game/weapons.h"

#include "game/audio.h"
#include "game/lang.h"
#include "game/ragdoll.h"
#include "game/runtime.h"
#include "game/survival.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace hotd::game {

using G = A::G;

namespace {
constexpr uint32_t kGunOnly = 0xF1000719;     // the players' gun hand without the hand (assets/arcade_loader.h kGunOnlyKey)
constexpr uint32_t kMagicianHand = 0x1C6C;    // boss4_ter01: the Magician's right hand (the Gallery's t76 skeleton)
constexpr uint32_t kCoin = 0x3B5;             // kinka: the gold coin (prop_drop_gold_coin; Survival's QUICK RELOAD sign)
constexpr uint32_t kFireballFirst = 0x1CAB, kFireballLast = 0x1CB9;   // magician_fireball's looping models (in flight)
constexpr int kMagicianArea = 12;             // the Magician's room's texture area (his parts' textures: view.cpp actorAreas)
constexpr uint32_t kCueShotP1 = 0xA01208, kCueShotP2 = 0xA01207, kCueReload = 0xA01200, kCueTake = 0xA01351;

// the built-in table (data/weapons.ini over it): docs/weapons.md "The weapons"
std::vector<WeaponDef> defaults()
{
    std::vector<WeaponDef> v(kWeapons);
    WeaponDef& pistol = v[kPistol];
    pistol.key = "pistol", pistol.name = "HANDGUN";
    pistol.magazine = 6, pistol.reserveMags = -1, pistol.reload = 36, pistol.rank = 0;
    WeaponDef& smg = v[kSmg];   // (Survival's SMG as survival2 tuned it: 30 rounds, 12 shots a second, 72-frame reload)
    smg.key = "smg", smg.name = "SMG";
    smg.magazine = 30, smg.reserveMags = 4, smg.reload = 72, smg.period = 5, smg.burst = 3, smg.damage = 100;
    smg.fireSound = "smg_fire", smg.fireCue = 30, smg.firePitch = 1.4f, smg.fireClip = 0.4f, smg.fireVolume = 0.9f;
    smg.reloadSound = "smg_reload", smg.reloadPitch = 0.8f;
    smg.price = 1000, smg.drop = 4, smg.rank = 4;
    WeaponDef& rifle = v[kRifle];
    rifle.key = "rifle", rifle.name = "ASSAULT RIFLE";
    rifle.magazine = 30, rifle.reserveMags = 4, rifle.reload = 84, rifle.period = 6, rifle.burst = 4, rifle.damage = 140;
    rifle.fireSound = "rifle_fire", rifle.fireCue = 30, rifle.firePitch = 1.15f, rifle.fireClip = 0.55f, rifle.fireVolume = 1.0f;
    rifle.reloadSound = "rifle_reload", rifle.reloadPitch = 0.7f;
    rifle.scale = 1.15f, rifle.price = 2500, rifle.drop = 3, rifle.rank = 5;
    WeaponDef& dual = v[kDual];
    dual.key = "dual", dual.name = "DOUBLE PISTOLS";
    dual.magazine = 16, dual.reserveMags = 5, dual.reload = 60, dual.period = 0, dual.damage = 200, dual.dual = 14.0f;
    dual.fireSound = "dual_fire", dual.fireCue = 30, dual.firePitch = 1.2f, dual.fireClip = 0.7f;
    dual.reloadSound = "dual_reload", dual.reloadPitch = 0.9f;
    dual.price = 1500, dual.drop = 3, dual.rank = 2;
    WeaponDef& shotgun = v[kShotgun];
    shotgun.key = "shotgun", shotgun.name = "SHOTGUN";
    shotgun.magazine = 6, shotgun.reserveMags = 5, shotgun.reload = 90, shotgun.damage = 130;
    shotgun.pellets = 6, shotgun.spread = 24.0f, shotgun.range = 170.0f;
    shotgun.fireSound = "shotgun_fire", shotgun.fireCue = 30, shotgun.firePitch = 0.7f, shotgun.fireClip = 1.0f;
    shotgun.reloadSound = "shotgun_reload", shotgun.reloadPitch = 0.6f;
    shotgun.scale = 1.25f, shotgun.price = 3000, shotgun.drop = 3, shotgun.rank = 3;
    WeaponDef& hand = v[kHand];
    hand.key = "hand", hand.name = "MAGICIAN'S HAND";
    // survhand (players 2026-11-10: "they made me lose in Survival"): 8000 points bought 12 shots (4 + 2 x 4, a 2 s
    // reload) and the Hand was gone when used up, the light guns' auto best picked it last; the balance runs (Hand
    // buyer, 3 arenas x 8 seeds, 18 rounds) lost 90 / 97 / 135 lives (the Double Pistols' 47 / 63 / 82). Now 6 + 5 x 6
    // shots, a 1.3 s reload, 550 % in a 50 radius, kept empty (Ammo / Max Ammo fill it), the top auto rank: 25 / 39 / 36
    hand.magazine = 6, hand.reserveMags = 5, hand.reload = 75, hand.damage = 550, hand.area = 50.0f, hand.projectile = true;
    hand.keepEmpty = true;
    hand.model = kMagicianHand, hand.pickupModel = kMagicianHand, hand.scale = 0.8f;
    hand.fireSound = "hand_fire", hand.fireGameCue = 0xA01463;   // (magician_fireball's throw: the PC's cue 249)
    // (tests 2026-11-09: as data/weapons.ini, user 2026-11-05 "make it louder": the stand-in cue 249 at 1.8; the
    // built-in table had kept cue 30 at 1.0, heard only without the ini)
    hand.fireCue = 249, hand.fireVolume = 1.8f;
    hand.reloadSound = "hand_reload", hand.reloadPitch = 0.5f;
    hand.price = 8000, hand.drop = 1, hand.rank = 6;
    // Light Gun Edition (Killer Mods): the machine gun (heavier than the SMG: a bigger magazine, faster, a lower shot)
    // and the rocket launcher (one rocket a load, 5 in reserve; each one fired blows up every zombie on the screen:
    // Runtime::lg_fired)
    WeaponDef& mg = v[kMachine];
    mg.key = "machinegun", mg.name = "MACHINE GUN";
    mg.magazine = 50, mg.reserveMags = 4, mg.reload = 96, mg.period = 4, mg.burst = 5, mg.damage = 110;
    mg.fireSound = "machinegun_fire", mg.fireCue = 30, mg.firePitch = 1.0f, mg.fireClip = 0.38f, mg.fireVolume = 1.0f;
    mg.reloadSound = "machinegun_reload", mg.reloadPitch = 0.6f;
    mg.price = 0, mg.drop = 0, mg.rank = 7;
    WeaponDef& rk = v[kRocket];
    rk.key = "rocket", rk.name = "ROCKET LAUNCHER";
    rk.magazine = 1, rk.reserveMags = 5, rk.reload = 60, rk.damage = 100;
    rk.fireSound = "rocket_fire", rk.fireGameCue = 0xA01463;   // (the Magician's fireball throw, as the Hand's)
    rk.reloadSound = "rocket_reload", rk.reloadPitch = 0.5f;
    rk.price = 0, rk.drop = 0, rk.rank = 8;
    return v;
}

std::vector<WeaponDef>& table()
{
    static std::vector<WeaponDef> t = defaults();
    return t;
}

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(uint8_t(s[a]))) ++a;
    while (b > a && std::isspace(uint8_t(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// camera space (x right, y up, z ahead: the arcade screen's x = x 496 / z, y = y 496 / z) -> a DrawModel's 3x4 (the
// coprocessor's layout: the images of the model's x, y, z axes, then the place)
DrawModel placed(uint32_t model, const float ax[3], const float ay[3], const float az[3], const float pos[3], float s)
{
    DrawModel d{};
    d.model = model;
    for (int k = 0; k < 3; ++k) {
        d.m[k] = ax[k] * s;
        d.m[3 + k] = ay[k] * s;
        d.m[6 + k] = az[k] * s;
        d.m[9 + k] = pos[k];
    }
    d.light[0] = 0, d.light[1] = -1, d.light[2] = 0;
    return d;
}
// a model turning about the camera's up axis at a camera-space place (the shop's items, the pickups): the guns
// lie on their side (the barrel across, the slide up), the others stand as modelled
DrawModel turning(uint32_t model, const float pos[3], float angle, float s)
{
    const float c = std::cos(angle), sn = std::sin(angle);
    // RotY(angle) x base
    auto rot = [&](const float v[3], float o[3]) { o[0] = v[0] * c + v[2] * sn, o[1] = v[1], o[2] = -v[0] * sn + v[2] * c; };
    const bool gun = model == kGunOnly || model == 0x719;
    const bool coin = model == 0x3B5 || model == 0x3B6;   // (kinka: a flat disc in the model's x / z: stood up)
    float bx[3] = {gun ? 0.0f : 1.0f, 0, gun ? 1.0f : 0.0f};
    float by[3] = {gun ? 1.0f : 0.0f, gun ? 0.0f : 1.0f, 0};
    float bz[3] = {0, gun ? 1.0f : 0.0f, gun ? 0.0f : 1.0f};
    if (coin) {
        const float y[3] = {0, 0, -1}, z[3] = {0, 1, 0};
        std::copy(y, y + 3, by), std::copy(z, z + 3, bz);
    }
    // survfix3 (user 2026-11-05: "make its fingertips face the same direction as the guns' muzzles"): the guns' muzzle
    // (model -y) is turned to the camera's -x at angle 0 (the barrel pointing left); the Hand's fingertips (model +x)
    // the same way: turned half round about its up axis, so through the shared turn they point where the muzzles do
    if (model == kMagicianHand) {
        const float x[3] = {-1, 0, 0}, z[3] = {0, 0, -1};
        std::copy(x, x + 3, bx), std::copy(z, z + 3, bz);
    }
    float x[3], y[3], z[3];
    rot(bx, x), rot(by, y), rot(bz, z);
    // survfix3 (user 2026-11-05: the Hand "does not rotate the same way as the other items"): the Magician's hand
    // 0x1C6C has its origin at the wrist (bounds x -0.24..5.27, y -0.77..1.36, z -0.90..1.14: the fingers along +x),
    // so turned about its origin it swung round the wrist in a wide circle while the guns, coin and life item turn in
    // place. It turns about its middle: the place given is where its middle is drawn (the same axis, angle and bob)
    float p[3] = {pos[0], pos[1], pos[2]};
    if (model == kMagicianHand) {
        const float mid[3] = {2.52f, 0.30f, 0.12f};
        for (int k = 0; k < 3; ++k)
            p[k] -= (x[k] * mid[0] + y[k] * mid[1] + z[k] * mid[2]) * s;
    }
    return placed(model, x, y, z, p, s);
}
} // namespace

const std::vector<WeaponDef>& weaponDefs() { return table(); }

namespace {
WeaponOptions gOptions;
}
const WeaponOptions& weaponOptions() { return gOptions; }

std::string weaponsIniPath()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const std::string& d : lang::dataDirs())
        if (fs::is_regular_file(fs::path(d) / "data" / "weapons.ini", ec))
            return (fs::path(d) / "data" / "weapons.ini").string();
    return lang::dataDirs().empty() ? std::string("data/weapons.ini") : (fs::path(lang::dataDirs()[0]) / "data" / "weapons.ini").string();
}

std::string weaponsSoundDir()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const std::string& d : lang::dataDirs())
        if (fs::is_directory(fs::path(d) / "data" / "weapons", ec))
            return (fs::path(d) / "data" / "weapons").string();
    return lang::dataDirs().empty() ? std::string("data/weapons") : (fs::path(lang::dataDirs()[0]) / "data" / "weapons").string();
}

// data/weapons.ini: [pistol] [smg] [rifle] [dual] [shotgun] [hand] sections of `key = value` lines (docs/weapons.md);
// a key left out keeps the built-in value
bool loadWeapons(const std::string& path, std::string* log)
{
    std::vector<WeaponDef>& t = table();
    t = defaults();
    gOptions = WeaponOptions();
    std::ifstream f(path);
    if (!f)
        return false;
    WeaponDef* w = nullptr;
    bool options = false;
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        if (const size_t h = line.find('#'); h != std::string::npos)
            line.resize(h);
        line = trim(line);
        if (line.empty())
            continue;
        if (line.front() == '[' && line.back() == ']') {
            const std::string k = trim(line.substr(1, line.size() - 2));
            w = nullptr;
            options = k == "options";
            for (WeaponDef& d : t)
                if (d.key == k) w = &d;
            if (!w && !options && log)
                *log += "weapons.ini: unknown weapon [" + k + "]\n";
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (options) {
            if (k == "auto_best_light_gun")
                gOptions.autoBestLightGun = std::atoi(v.c_str()) != 0;
            continue;
        }
        if (!w)
            continue;
        const int i = std::atoi(v.c_str());
        const float fl = float(std::atof(v.c_str()));
        const uint32_t u = uint32_t(std::strtoul(v.c_str(), nullptr, 0));
        bool known = true;
        if (k == "name") w->name = v;
        else if (k == "magazine") w->magazine = std::clamp(i, 1, 99);
        else if (k == "reserve_magazines") w->reserveMags = std::max(-1, i);
        else if (k == "reload_frames") w->reload = std::clamp(i, 0, 600);
        else if (k == "fire_period") w->period = std::clamp(i, 0, 120);
        else if (k == "burst") w->burst = std::clamp(i, 1, 10);
        else if (k == "damage") w->damage = std::clamp(i, 1, 2000);
        else if (k == "pellets") w->pellets = std::clamp(i, 0, 15);
        else if (k == "spread") w->spread = std::clamp(fl, 0.0f, 200.0f);
        else if (k == "range") w->range = std::max(0.0f, fl);
        else if (k == "area") w->area = std::max(0.0f, fl);
        else if (k == "projectile") w->projectile = u != 0;
        else if (k == "keep_empty") w->keepEmpty = u != 0;
        else if (k == "dual") w->dual = std::clamp(fl, 0.0f, 100.0f);
        else if (k == "model") w->model = u;
        else if (k == "pickup_model") w->pickupModel = u;
        else if (k == "scale") w->scale = std::clamp(fl, 0.05f, 10.0f);
        else if (k == "fire_sound") w->fireSound = v;
        else if (k == "reload_sound") w->reloadSound = v;
        else if (k == "fire_standin_cue") w->fireCue = i;
        else if (k == "fire_standin_speed") w->firePitch = std::clamp(fl, 0.1f, 8.0f);
        else if (k == "fire_standin_length") w->fireClip = std::clamp(fl, 0.05f, 1.0f);
        else if (k == "fire_volume") w->fireVolume = std::clamp(fl, 0.0f, 2.0f);
        else if (k == "fire_game_cue") w->fireGameCue = u;
        else if (k == "reload_standin_cue") w->reloadCue = i;
        else if (k == "reload_standin_speed") w->reloadPitch = std::clamp(fl, 0.1f, 8.0f);
        else if (k == "price") w->price = std::max(0, i);
        else if (k == "drop_weight") w->drop = std::max(0, i);
        else if (k == "auto_rank") w->rank = i;
        else known = false;
        if (known)
            ++n;
        else if (log)
            *log += "weapons.ini: unknown key " + k + " in [" + w->key + "]\n";
    }
    t[kPistol].reserveMags = -1;   // (the handgun never runs dry)
    if (log)
        *log += "weapons.ini: " + std::to_string(n) + " values from " + path + "\n";
    return true;
}

uint32_t Weapons::next()
{
    rng_ = rng_ * 1103515245u + 12345u;
    return (rng_ >> 8) & 0xFFFFFF;
}

uint64_t Weapons::digest() const
{
    uint64_t h = 0xCBF29CE484222325ull;   // (FNV-1a over the logic fields' values)
    auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i, v >>= 8)
            h = (h ^ (v & 0xFF)) * 0x100000001B3ull;
    };
    mix(uint64_t(mode) | uint64_t(live) << 8 | uint64_t(rng_) << 32);
    for (int p = 0; p < 4; ++p) {
        const Seat& s = seat[p];
        mix(uint64_t(autoBest[p]) | uint64_t(uint32_t(request[p])) << 32);
        for (int w = 0; w < kWeapons; ++w)
            mix(uint64_t(s.own[w]) | uint64_t(uint32_t(s.mag[w])) << 8 | uint64_t(uint32_t(s.reserve[w])) << 32);
        mix(uint64_t(uint32_t(s.cur)) | uint64_t(uint32_t(s.reload)) << 32);
        mix(uint64_t(uint32_t(s.timer)) | uint64_t(uint32_t(s.burst)) << 32);
        mix(uint64_t(s.quick) | uint64_t(s.bigMag) << 1 | s.shotFrame << 8);
    }
    mix(pickups.size());
    for (const Pickup& k : pickups) {
        uint32_t b[3];
        std::memcpy(b, k.pos, sizeof b);
        mix(b[0] | uint64_t(b[1]) << 32);
        mix(b[2] | uint64_t(uint32_t(k.weapon)) << 32);
        mix(uint32_t(k.frames));
    }
    mix(uint64_t(uint32_t(dropped)) | uint64_t(uint32_t(taken)) << 32);
    mix(uint32_t(reloadSeat));
    return h;
}

void Weapons::begin(Runtime& rt, Mode m)
{
    std::string log;
    loadWeapons(weaponsIniPath(), &log);
    if (rt.log && !log.empty())
        rt.log(log.substr(0, log.size() - 1));
    *this = Weapons();
    mode = m;
    rt.weaponHooks = m != Off;
    for (int p = 0; p < 4; ++p) {
        seat[p] = Seat();
        seat[p].mag[kPistol] = magazine(p, kPistol);
        rt.weaponShot[p] = {};
        rt.survivalRounds[p] = m != Off ? magazine(p, kPistol) : 0;
    }
    // the Magician's hand: its textures are in his room's area (12), drawn from Chapter 4's stage as the cut creatures
    // and Dr. Curien's are (view.cpp actorAreas)
    if (m != Off) {
        rt.actorAreas[kMagicianHand] = kMagicianArea;
        for (uint32_t k = kFireballFirst; k <= kFireballLast; ++k)   // (survfix: his fireball, the Hand's bolts)
            rt.actorAreas[k] = kMagicianArea;
    }
    if (rt.log && m != Off)
        rt.log(std::string("weapons: ") + (m == Survival ? "Survival" : m == Arsenal ? "Arsenal" : "Drops") + " (" + std::to_string(kWeapons) + " weapons)");
}

bool Weapons::endless(int w) const
{
    // (Light Gun Edition, user 2026-11-08: "I don't want any gun to run out ... use any gun the whole time": in the
    // Killer Mods every reserve endless, the rocket launcher's too; one rocket a load still)
    return def(w).reserveMags < 0 || mode == Arsenal;
}

void Weapons::arm(Runtime& rt, int p)
{
    Seat& s = seat[p & 3];
    s = Seat();
    s.mag[kPistol] = magazine(p, kPistol);
    for (int w = 1; w < kWeapons; ++w)
        if (only < 0 || w == only)
            give(rt, p, w);
    if (only > 0)
        select(rt, p, only);
    else
        rt.mem.at<port::a::Player>(rt.player_rec(p)).rounds = uint8_t(magazine(p, kPistol));
    rt.survivalRounds[p & 3] = magazine(p, s.cur);
    notice[p & 3].clear();
}

void Weapons::end(Runtime& rt)
{
    if (mode == Off)
        return;
    mode = Off;
    live = false;
    rt.weaponHooks = false;
    for (int p = 0; p < 4; ++p) {
        rt.weaponShot[p] = {};
        rt.survivalRounds[p] = 0;
    }
    rt.actorAreas.erase(kMagicianHand);
    for (uint32_t k = kFireballFirst; k <= kFireballLast; ++k)
        rt.actorAreas.erase(k);
    for (auto& bt : rt.weaponBolts)
        bt = {};
}

int Weapons::magazine(int p, int w) const
{
    const int m = def(w).magazine;
    return std::min(99, seat[p & 3].bigMag ? m * 3 / 2 : m);   // (Big Mag: x 1.5)
}

int Weapons::reserveFull(int p, int w) const
{
    const int r = def(w).reserveMags;
    return r < 0 ? 0 : magazine(p, w) * r;
}

int Weapons::reloadFrames(int p) const
{
    const Seat& s = seat[p & 3];
    // Drops: the handgun reloads at once as the arcade's (an off-screen pull); the others take their time
    const int f = (mode == Drops || mode == Arsenal) && s.cur == kPistol ? 0 : def(s.cur).reload;   // (Arsenal: as the arcade's too)
    return s.quick ? f / 2 : f;
}

bool Weapons::hasAmmo(int p, int w) const
{
    const Seat& s = seat[p & 3];
    return s.own[w] && (w == kPistol || endless(w) || s.mag[w] > 0 || s.reserve[w] > 0);
}

bool Weapons::give(Runtime& rt, int p, int w)
{
    Seat& s = seat[p & 3];
    const bool was = s.own[w];
    s.own[w] = true;
    if (w == s.cur) {
        rt.mem.at<port::a::Player>(rt.player_rec(p)).rounds = uint8_t(magazine(p, w));
        rt.mem.at<port::a::Player>(rt.player_rec(p)).empty = 0;
    }
    s.mag[w] = magazine(p, w);
    s.reserve[w] = reserveFull(p, w);
    return !was;
}

void Weapons::refill(Runtime& rt, int p, int w)
{
    Seat& s = seat[p & 3];
    port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
    for (int k = 0; k < kWeapons; ++k) {
        if ((w >= 0 && k != w) || !s.own[k])
            continue;
        s.reserve[k] = reserveFull(p, k);
        if (w < 0) {   // (Max Ammo: the magazines too)
            s.mag[k] = magazine(p, k);
            if (k == s.cur) {
                pl.rounds = uint8_t(s.mag[k]);
                pl.empty = 0;
                s.reload = 0;
            }
        }
    }
}

bool Weapons::select(Runtime& rt, int p, int w)
{
    Seat& s = seat[p & 3];
    if (w < 0 || w >= kWeapons || w == s.cur || !hasAmmo(p, w))
        return false;
    port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
    s.mag[s.cur] = pl.rounds;   // (the magazine stays in that gun)
    // Light Gun Edition (user 2026-11-12: "when you switch weapons the previous weapon automatically is reloaded ...
    // ready to use always"): in the Killer Mods the gun put away goes back full (every reserve endless there), so it
    // is ready when it comes back (the rocket launcher's one rocket too)
    if (mode == Arsenal && endless(s.cur))
        s.mag[s.cur] = magazine(p, s.cur);
    s.cur = w;
    s.reload = 0, s.timer = 0, s.burst = 0;
    ++s.switches;
    rt.survivalRounds[p & 3] = magazine(p, w);
    pl.rounds = uint8_t(std::min(s.mag[w], magazine(p, w)));
    pl.empty = pl.rounds == 0 ? 1 : 0;
    notice[p & 3] = def(w).name;
    noticeUntil[p & 3] = rt.frameCount + 60;
    return true;
}

void Weapons::cycle(Runtime& rt, int p, int dir)
{
    for (int k = 1; k < kWeapons; ++k) {
        const int w = ((seat[p & 3].cur + dir * k) % kWeapons + kWeapons) % kWeapons;
        if (hasAmmo(p, w)) {
            select(rt, p, w);
            return;
        }
    }
}

int Weapons::best(int p) const
{
    int b = kPistol;
    for (int w = 0; w < kWeapons; ++w)
        if (hasAmmo(p, w) && def(w).rank > def(b).rank)
            b = w;
    return b;
}

void Weapons::resetSeat(Runtime& rt, int p)
{
    Seat& s = seat[p & 3];
    const bool quick = s.quick, big = s.bigMag;
    Seat keep = s;
    s = Seat();
    s.quick = quick, s.bigMag = big;
    for (int w = 0; w < kWeapons; ++w)   // (the counts stay for the test line)
        s.shots[w] = keep.shots[w], s.reloads[w] = keep.reloads[w], s.kills[w] = keep.kills[w];
    s.switches = keep.switches;
    s.mag[kPistol] = magazine(p, kPistol);
    rt.survivalRounds[p & 3] = magazine(p, kPistol);
}

// the seat's shot of this frame for the logic (port/game.h WeaponShot): the damage always; firing: the shotgun's
// pellets (offsets from this stream, spread round the aim), the double pistols' second gun, the Hand's explosion
void Weapons::setShot(Runtime& rt, int p, bool firing)
{
    const WeaponDef& d = def(seat[p & 3].cur);
    RuntimeBounds::WeaponShot& w = rt.weaponShot[p & 3];
    w = {};
    w.damage = d.damage;
    if (!firing)
        return;
    if (d.dual > 0.0f && !rt.secondGun[p & 3].on) {   // (vr dual pistols: each VR gun its own aim, port/game.h SecondGun)
        w.off[w.rays][0] = d.dual, w.off[w.rays][1] = d.dual * 0.35f;
        ++w.rays;
        w.flashes = true;
    }
    for (int i = 0; i < d.pellets && w.rays < 16; ++i) {
        const float a = 6.2831853f * (float(i) + float(next() % 100) / 100.0f) / float(std::max(1, d.pellets));
        const float r = d.spread * (0.45f + 0.55f * float(next() % 100) / 100.0f);
        w.off[w.rays][0] = std::cos(a) * r, w.off[w.rays][1] = std::sin(a) * r;
        ++w.rays;
    }
    w.range = d.range;
    w.area = d.area;
    w.projectile = d.projectile && d.area > 0.0f;
}

bool Weapons::pickupScreen(const Runtime& rt, int i, float& x, float& y) const
{
    if (i < 0 || i >= int(pickups.size()))
        return false;
    const float* m = rt.camera;
    const Pickup& k = pickups[size_t(i)];
    float c[3];
    for (int a = 0; a < 3; ++a)
        c[a] = m[a] * k.pos[0] + m[3 + a] * k.pos[1] + m[6 + a] * k.pos[2] + m[9 + a];
    if (c[2] < 5.0f)
        return false;
    x = c[0] * 496.0f / c[2], y = c[1] * 496.0f / c[2];
    return true;
}

int Weapons::pickupAt(const Runtime& rt, float x, float y) const
{
    const float* m = rt.camera;
    for (size_t i = 0; i < pickups.size(); ++i) {
        const Pickup& k = pickups[i];
        float c[3];
        for (int a = 0; a < 3; ++a)
            c[a] = m[a] * k.pos[0] + m[3 + a] * k.pos[1] + m[6 + a] * k.pos[2] + m[9 + a];
        if (c[2] < 5.0f)
            continue;
        const float sx = c[0] * 496.0f / c[2], sy = c[1] * 496.0f / c[2];
        const float r = std::max(18.0f, 6.0f * 496.0f / c[2]);
        if ((sx - x) * (sx - x) + (sy - y) * (sy - y) <= r * r)
            return int(i);
    }
    return -1;
}

void Weapons::gunInput(Runtime& rt, int p, bool& trigger, bool onScreen, bool held, float x, float y)
{
    if (!live || p < 0 || p > 3)
        return;
    Seat& s = seat[p];
    port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
    if (pl.state != 5) {
        request[p] = 0;
        return;
    }
    const bool shots = rt.mem.at<int16_t>(G::game_mode) == 4 && rt.mem.u32(G::shot_enable) != 0;
    s.mag[s.cur] = pl.rounds;
    // the switch (the wheel, the number keys, a pad's shoulders), or the light gun's auto best at each pull
    if (const int r = request[p]; r != 0) {
        request[p] = 0;
        if (r >= 100) select(rt, p, r - 100);
        else cycle(rt, p, r > 0 ? 1 : -1);
    }
    if (autoBest[p] && trigger && onScreen && s.reload == 0)
        select(rt, p, best(p));
    // testing (pictures of each weapon): HOTD_WEAPONS_FORCE=<id 0-5>: that weapon in every hand that holds it
    static const int force = std::getenv("HOTD_WEAPONS_FORCE") ? std::atoi(std::getenv("HOTD_WEAPONS_FORCE")) : -1;
    // (survfix2, the balance runs' time to kill: HOTD_WEAPONS_FORCE_KEEP=1 gives it back when it was used up and lost)
    static const bool forceKeep = std::getenv("HOTD_WEAPONS_FORCE_KEEP") != nullptr;
    if (force >= 0 && force < kWeapons && s.cur != force && (s.own[force] || forceKeep)) {
        if ((s.mag[force] == 0 && s.reserve[force] == 0) || !s.own[force])
            give(rt, p, force);
        select(rt, p, force);
    }
    rt.survivalRounds[p] = magazine(p, s.cur);
    // Drops: a shot at a pickup takes it (the shot is not fired), as the arcade's life item is shot
    if (mode == Drops && trigger && onScreen && shots)
        if (const int i = pickupAt(rt, x, y); i >= 0) {
            const int w = pickups[size_t(i)].weapon;
            const bool up = pickups[size_t(i)].upgrade;
            pickups.erase(pickups.begin() + i);
            ++taken;
            trigger = false;
            if (up) {   // hordepub: an upgrade (Survival's perks, its shop's order)
                notice[p] = givePerk(rt, p);
                noticeUntil[p] = rt.frameCount + 90;
                rt.sound_cue(kCueTake);
                if (rt.log)
                    rt.log("weapons: " + std::to_string(p + 1) + "P took an upgrade (" + notice[p] + ") at frame " + std::to_string(rt.frameCount));
                setShot(rt, p, false);
                return;
            }
            // hordepub (user 2026-11-09: "picking up ammo must not switch you to that gun"): only a new weapon goes in
            // the hand; one already held gets ammo: half its full reserve (a magazine at least) added to its reserve,
            // the gun in the hand unchanged (it switched to it from the handgun before)
            const bool fresh = !s.own[w];
            if (fresh) {
                give(rt, p, w);
                select(rt, p, w);
            } else if (def(w).reserveMags >= 0) {
                const int full = reserveFull(p, w);
                s.reserve[w] = std::min(full, s.reserve[w] + std::max(magazine(p, w), full / 2));
            }
            notice[p] = fresh ? def(w).name : def(w).name + " AMMO";
            noticeUntil[p] = rt.frameCount + 90;
            rt.sound_cue(kCueTake);
            if (rt.log)
                rt.log("weapons: " + std::to_string(p + 1) + "P took the " + def(w).key + " at frame " + std::to_string(rt.frameCount));
            setShot(rt, p, false);
            return;
        }
    // the reload in progress: the gun does nothing until it is done; then the magazine from the reserve
    if (s.reload > 0) {
        trigger = false;
        s.timer = 0, s.burst = 0;
        if (--s.reload == 0) {
            const int mag = magazine(p, s.cur);
            if (endless(s.cur)) {
                pl.rounds = uint8_t(mag);
            } else {
                const int take = std::max(0, std::min(mag - int(pl.rounds), s.reserve[s.cur]));
                s.reserve[s.cur] -= take;
                pl.rounds = uint8_t(int(pl.rounds) + take);
            }
            pl.empty = pl.rounds == 0 ? 1 : 0;
            s.mag[s.cur] = pl.rounds;
        }
        setShot(rt, p, false);
        return;
    }
    // a pull off the screen (or the reload button): the reload by hand
    if (trigger && !onScreen && shots) {
        const int mag = magazine(p, s.cur);
        if (int(pl.rounds) >= mag) {
            trigger = false;
            setShot(rt, p, false);
            return;
        }
        const int f = reloadFrames(p);
        ++s.reloads[s.cur];
        reloadSeat = p;
        if (f == 0 && def(s.cur).reserveMags < 0) {   // (the game's own reload: player_shoot / the input refill)
            setShot(rt, p, false);
            return;
        }
        trigger = false;
        if (!endless(s.cur) && s.reserve[s.cur] == 0) {   // (nothing to load)
            setShot(rt, p, false);
            return;
        }
        s.reload = std::max(1, f);
        rt.sound_cue(kCueReload);
        setShot(rt, p, false);
        return;
    }
    // the held fire: a held trigger on the screen fires every period frames; a pull from an input that cannot tell
    // held (pads, light guns): a burst
    const WeaponDef& d = def(s.cur);
    if (d.period > 0) {
        if (trigger && onScreen) {
            s.timer = 0;
            s.burst = d.burst - 1;
        } else if (onScreen && (held || s.burst > 0) && ++s.timer >= d.period) {
            trigger = true;
            s.timer = 0;
            if (!held && s.burst > 0)
                --s.burst;
        } else if (!onScreen) {
            s.burst = 0;
        }
    }
    const bool firing = trigger && onScreen && shots && pl.rounds > 0;
    if (firing) {
        s.shotFrame = rt.frameCount;
        ++s.shots[s.cur];
    }
    setShot(rt, p, firing);
}

void Weapons::afterFrame(Runtime& rt)
{
    if (!on())
        return;
    rt.weaponFrame = rt.frameCount;
    // (hordepub: a held pickup's time runs only while the shots are on: a rescue's gift waits through its scene)
    const bool shotsOn = rt.mem.at<int16_t>(G::game_mode) == 4 && rt.mem.u32(G::shot_enable) != 0;
    for (size_t i = 0; i < pickups.size();)
        if ((!pickups[i].hold || shotsOn) && --pickups[i].frames <= 0)
            pickups.erase(pickups.begin() + ptrdiff_t(i));
        else
            ++i;
    for (int p = 0; p < rt.seat_count() && p < 4; ++p) {
        Seat& s = seat[p];
        port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
        if (pl.state != 5) {
            // Drops (Horde): out of lives (the continue / game over): the weapons are kept (hordepub, user 2026-11-09:
            // "keep weapons after a continue"; they were reset to the handgun as the Remake's death): the magazine in the
            // hand as it was last frame, put back when the seat plays again (the game's continue loads the handgun's)
            if (mode == Drops && pl.lives <= 0)
                s.down = true;
            continue;
        }
        if (s.down) {
            s.down = false;
            const int m = magazine(p, s.cur);
            pl.rounds = uint8_t(s.cur == kPistol ? m : std::min(s.mag[s.cur], m));
            pl.empty = pl.rounds == 0 ? 1 : 0;
            s.reload = 0, s.timer = 0, s.burst = 0;
            rt.survivalRounds[p] = m;
            if (rt.log)
                rt.log("weapons: " + std::to_string(p + 1) + "P plays again with the " + def(s.cur).key + " (" +
                       std::to_string(std::count(s.own, s.own + kWeapons, true)) + " weapons kept) at frame " + std::to_string(rt.frameCount));
        }
        if (!live)
            continue;
        s.mag[s.cur] = pl.rounds;
        rt.survivalRounds[p] = magazine(p, s.cur);
        if (shotsOn)   // (the balance runs' line: summary's held=)
            ++heldFrames[s.cur], reloadingFrames[s.cur] += s.reload > 0 ? 1 : 0;
        // a weapon with nothing left (its magazine and reserve): gone, back to the best one with ammo (the handgun)
        if (s.cur != kPistol && !endless(s.cur) && pl.rounds == 0 && s.reserve[s.cur] == 0 && s.reload == 0) {
            const int gone = s.cur;
            // (survhand: a keep_empty weapon, the Magician's Hand, stays held empty: Ammo / Max Ammo fill it again)
            s.own[gone] = def(gone).keepEmpty;
            s.mag[gone] = 0;
            s.cur = kPistol;   // (select() keeps the old magazine: the gun is gone)
            const int b = best(p);
            s.cur = b;
            pl.rounds = uint8_t(std::min(s.mag[b], magazine(p, b)));
            pl.empty = pl.rounds == 0 ? 1 : 0;
            rt.survivalRounds[p] = magazine(p, b);
            notice[p] = def(gone).name + " EMPTY";
            noticeUntil[p] = rt.frameCount + 90;
        }
    }
}

void Weapons::kill(Runtime& rt, uint32_t obj, int killer)
{
    if (!live)
        return;
    for (auto& [o, f] : killed_)   // (one report a creature)
        if (o == obj && rt.frameCount < f + 120)
            return;
    killed_.erase(std::remove_if(killed_.begin(), killed_.end(), [&](const auto& k) { return rt.frameCount >= k.second + 120; }), killed_.end());
    killed_.push_back({obj, rt.frameCount});
    if (killer >= 0 && killer < 4)
        ++seat[killer].kills[seat[killer].cur];
    // testing (survfix3, pictures of a pickup): HOTD_WEAPONS_DROP=<id 1-5>: every kill drops that weapon (at most 6 lying)
    static const int forceDrop = std::getenv("HOTD_WEAPONS_DROP") ? std::atoi(std::getenv("HOTD_WEAPONS_DROP")) : -1;
    if (mode != Drops || (forceDrop < 0 && next() % kDropChance != 0) || pickups.size() >= 6)
        return;
    int w = pickDrop();
    if (forceDrop > 0 && forceDrop < kWeapons)
        w = forceDrop;
    if (w < 0)
        return;
    // where it was last drawn (its camera-space centre, Obj +0x38: the body's middle), 10 units nearer (in front of the
    // body, not inside it)
    const float v[3] = {rt.mem.f32(obj + 0x38), rt.mem.f32(obj + 0x3C), rt.mem.f32(obj + 0x40)};
    const float dist = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (v[2] < 5.0f || dist < 1.0f)
        return;   // (not drawn in front of the camera)
    const float pull = std::max(0.5f, 1.0f - 10.0f / dist);
    const float c[3] = {v[0] * pull, v[1] * pull, v[2] * pull};
    dropAt(rt, c, w, kPickupFrames, false, "a kill");
}

int Weapons::pickDrop()
{
    int total = 0;
    for (int w = 0; w < kWeapons; ++w)
        total += def(w).drop;
    if (total <= 0)
        return -1;
    int r = int(next() % uint32_t(total)), w = 0;
    for (; w < kWeapons; ++w)
        if ((r -= def(w).drop) < 0)
            break;
    return w < kWeapons ? w : -1;
}

// a pickup at a camera-space place, back into the world by the view matrix (camera = R world + t: world = R^T (camera -
// t)), so it stays there as the camera moves
bool Weapons::dropAt(Runtime& rt, const float cam[3], int weapon, int frames, bool hold, const char* why, bool upgrade)
{
    if (upgrade)
        weapon = kPistol;
    if (mode != Drops || weapon < 0 || weapon >= kWeapons || pickups.size() >= kMaxPickups)
        return false;
    Pickup k{};
    k.upgrade = upgrade;
    const float* m = rt.camera;
    float c[3];
    for (int a = 0; a < 3; ++a)
        c[a] = cam[a] - m[9 + a];
    for (int j = 0; j < 3; ++j)
        k.pos[j] = m[3 * j] * c[0] + m[3 * j + 1] * c[1] + m[3 * j + 2] * c[2];
    k.weapon = weapon;
    k.frames = frames;
    k.hold = hold;
    pickups.push_back(k);
    ++dropped;
    if (rt.log) {
        float sx = 0, sy = 0;
        const bool seen = pickupScreen(rt, int(pickups.size()) - 1, sx, sy);
        char b[200];
        std::snprintf(b, sizeof b, "weapons: %s pickup (%s) at frame %llu (world %.0f %.0f %.0f, screen %s%.0f %.0f; camera y %.0f)",
                      (upgrade ? std::string("an upgrade") : "a " + def(weapon).key).c_str(), why, (unsigned long long)rt.frameCount, k.pos[0], k.pos[1], k.pos[2],
                      seen ? "" : "behind ", sx, sy, rt.mem.f32(G::camera_pos + 4));
        rt.log(b);
    }
    return true;
}

std::string Weapons::givePerk(Runtime& rt, int p)
{
    Seat& s = seat[p & 3];
    port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
    ++upgrades;
    if (!s.quick) {
        s.quick = true;
        return "QUICK RELOAD";
    }
    if (!s.bigMag) {   // (as Survival's BIG MAG: every magazine x 1.5, the one in the hand full at once)
        s.bigMag = true;
        rt.survivalRounds[p & 3] = magazine(p, s.cur);
        if (pl.state == 5) {
            pl.rounds = uint8_t(magazine(p, s.cur));
            pl.empty = 0;
            s.mag[s.cur] = pl.rounds;
        }
        for (int k = 1; k < kWeapons; ++k)
            if (s.own[k])
                s.reserve[k] = std::max(s.reserve[k], reserveFull(p, k));
        return "BIG MAG";
    }
    refill(rt, p);   // (both held: Max Ammo, every weapon held full)
    return "MAX AMMO";
}

bool Weapons::cue(Runtime& rt, uint32_t c)
{
    if (!live || !rt.audio)
        return false;
    namespace fs = std::filesystem;
    // the user's file in data/weapons, else (the SMG's old place) data/survival, else the stand-in
    auto dirOf = [&](const std::string& name) {
        std::error_code ec;
        const std::string wd = weaponsSoundDir();
        if (fs::is_regular_file(fs::path(wd) / (name + ".wav"), ec))
            return wd;
        if (fs::is_regular_file(fs::path(survivalSoundDir()) / (name + ".wav"), ec))
            return survivalSoundDir();
        return wd;
    };
    if (c == kCueShotP1 || c == kCueShotP2) {
        // player_shoot's shot (players 1 / 3: 0xA01208; 2 / 4: 0xA01207): the seat that fired this frame
        for (int p = 0; p < 4; ++p) {
            if (rt.shot_cue(p) != c)   // (charsel4: Rogan / G their own cue in any seat, look_seat)
                continue;
            const Seat& s = seat[p];
            if (s.shotFrame != rt.frameCount || s.cur == kPistol)
                continue;
            const WeaponDef& d = def(s.cur);
            const std::string name = d.fireSound.empty() ? d.key + "_fire" : d.fireSound;
            const std::string dir = dirOf(name);
            std::error_code ec;
            if (d.fireGameCue != 0 && !fs::is_regular_file(fs::path(dir) / (name + ".wav"), ec)) {
                // the game's own: the Magician's projectile (user 2026-11-05: his own sound only, "make it louder so I can
                // hear it"): its PC bank sample (fire_standin_cue, cue 249) through the extra sounds' mixer at fire_volume,
                // its peak normalised first; the game's cue as before when that sample is not there
                static const bool cueOnly = std::getenv("HOTD_HAND_CUE") != nullptr;   // (testing: the game's cue as before)
                if (cueOnly || !rt.audio->playExtra(dir, name + "_orb", d.fireCue, 1.0f, 1.0f, d.fireVolume, 0.95f))
                    rt.sound_cue(d.fireGameCue);
                return true;
            }
            // (relfix2, user 2026-11-03: nothing to play (no file, no PC sound bank for the stand-in): the handgun's own
            // shot instead, never silence. HOTD_SURVIVAL_SMG_SILENT (testing): the weapons' shots unheard (a WAV of the
            // same run without them: the difference is the weapons' sound alone)
            static const bool silent = survivalEnv("SMG_SILENT") != nullptr;
            const bool ok = silent || rt.audio->playExtra(dir, name, d.fireCue, d.firePitch, d.fireClip, d.fireVolume);
            if (rt.audio->traceCues)
                std::printf("[sound %llu] 0x%06X -> %s shot: %s\n", (unsigned long long)rt.audio->frameNo, c, d.key.c_str(),
                            silent ? "silent (test)" : ok ? rt.audio->extraSource(name).c_str() : "the handgun's");
            return ok;
        }
        return false;
    }
    if (c == kCueReload) {   // (nothing to play: the handgun's reload)
        const Seat& s = seat[reloadSeat & 3];
        if (s.cur == kPistol)
            return false;
        const WeaponDef& d = def(s.cur);
        const std::string name = d.reloadSound.empty() ? d.key + "_reload" : d.reloadSound;
        return rt.audio->playExtra(dirOf(name), name, d.reloadCue, d.reloadPitch, 1.0f);
    }
    return false;
}

bool Weapons::worldRay(const Runtime& rt, const float o[3], const float d[3], float maxT, float& t) const
{
    constexpr float kNear = 2.0f;   // (nothing nearer: the eye's own surroundings)
    float best = maxT;
    bool hit = false;
    const RagdollWalls* w = rt.ragdollWorld ? rt.ragdollWorld() : nullptr;
    if (w && !w->active.empty()) {
        // a triangle (12 floats: its corners, its normal), both sides (Moeller-Trumbore)
        auto tri = [&](const float* p) {
            const float e1[3] = {p[3] - p[0], p[4] - p[1], p[5] - p[2]}, e2[3] = {p[6] - p[0], p[7] - p[1], p[8] - p[2]};
            const float q[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
            const float det = e1[0] * q[0] + e1[1] * q[1] + e1[2] * q[2];
            if (std::abs(det) < 1e-8f)
                return;
            const float inv = 1.0f / det;
            const float s[3] = {o[0] - p[0], o[1] - p[1], o[2] - p[2]};
            const float u = (s[0] * q[0] + s[1] * q[1] + s[2] * q[2]) * inv;
            if (u < 0.0f || u > 1.0f)
                return;
            const float r[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
            const float v = (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) * inv;
            if (v < 0.0f || u + v > 1.0f)
                return;
            const float tt = (e2[0] * r[0] + e2[1] * r[1] + e2[2] * r[2]) * inv;
            if (tt > kNear && tt < best)
                best = tt, hit = true;
        };
        for (const int oi : w->active) {
            if (oi < 0 || size_t(oi) >= w->objs.size() || (rt.onlineGame && size_t(oi) >= w->statics))
                continue;
            const RagdollWalls::Obj& ob = w->objs[size_t(oi)];
            // its box first (slabs, a unit of slack)
            float t0 = 0.0f, t1 = best;
            for (int a = 0; a < 3 && t0 <= t1; ++a) {
                const float lo = ob.lo[a] - 1.0f, hi = ob.hi[a] + 1.0f;
                if (std::abs(d[a]) < 1e-9f) {
                    if (o[a] < lo || o[a] > hi)
                        t0 = 1.0f, t1 = 0.0f;
                    continue;
                }
                float ta = (lo - o[a]) / d[a], tb = (hi - o[a]) / d[a];
                if (ta > tb)
                    std::swap(ta, tb);
                t0 = std::max(t0, ta), t1 = std::min(t1, tb);
            }
            if (t0 > t1)
                continue;
            for (size_t k = 0; k + 11 < ob.tris.size(); k += 12)
                tri(&ob.tris[k]);
            for (size_t k = 0; k + 11 < ob.floors.size(); k += 12)
                tri(&ob.floors[k]);
        }
    } else {
        // headless (no view): the ground the creatures walk on, the floor (their place's height) of the one nearest
        // the ray
        float bestD = 1e30f, floorY = 0.0f;
        for (const uint32_t obj : rt.shootable) {
            const port::a::Obj& ob = rt.mem.at<port::a::Obj>(obj);
            if (ob.part_root == 0)
                continue;
            const float v[3] = {ob.pos[0] - o[0], ob.pos[1] - o[1], ob.pos[2] - o[2]};
            const float along = v[0] * d[0] + v[1] * d[1] + v[2] * d[2];
            if (along <= 0.0f)
                continue;
            const float dd = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] - along * along;
            if (dd < bestD)
                bestD = dd, floorY = ob.pos[1];
        }
        if (bestD < 1e30f && std::abs(d[1]) > 1e-4f) {
            const float tt = (floorY - o[1]) / d[1];
            if (tt > kNear && tt < best)
                best = tt, hit = true;
        }
    }
    if (hit)
        t = best;
    return hit;
}

void Weapons::overlay(const Runtime& rt, std::vector<DrawModel>& out) const
{
    if (!live || rt.mem.at<int16_t>(G::game_mode) != 4)
        return;
    // survfix (user 2026-11-04: "every floating purchasable model must turn in sync"): one clock for every floating
    // model, the shop's and the pickups alike: the same angle and the same bob at the same frame (they each had
    // their own phase, + the item's index)
    const float t = float(rt.frameCount);
    const float angle = t * 0.03f, bob = std::sin(t * 0.05f);
    // Survival's shop items over their signs: 40 units ahead, turning
    for (const ShopItem& it : shopItems) {
        const float z = 40.0f;
        const float pos[3] = {it.x * z / 496.0f, (it.y + 3.0f * bob) * z / 496.0f, z};
        out.push_back(turning(it.model, pos, angle, it.scale));
    }
    // the pickups where they lie (world -> camera by the frame's view matrix), turning, blinking at the end
    for (const Pickup& k : pickups) {
        if (k.frames < 90 && (k.frames / 6) % 2 == 0)
            continue;
        const float* m = rt.camera;
        float c[3];
        for (int a = 0; a < 3; ++a)
            c[a] = m[a] * k.pos[0] + m[3 + a] * k.pos[1] + m[6 + a] * k.pos[2] + m[9 + a];
        if (c[2] < 5.0f)
            continue;
        c[1] += 1.0f * bob;
        const WeaponDef& d = def(k.weapon);
        // (hordepub: an upgrade: the gold coin, Survival's QUICK RELOAD sign's model, twice the gun's scale as there)
        const uint32_t model = k.upgrade ? kCoin : d.pickupModel ? d.pickupModel : kGunOnly;
        // (about 30 pixels across at any distance: the creatures fall far off)
        const float size = std::max(2.5f, c[2] * 0.026f) * (k.upgrade ? 2.0f : 1.0f);
        if (std::getenv("HOTD_WEAPONS_TRACE"))
            std::printf("pickup draw frame %llu: camera %.1f %.1f %.1f size %.1f model %#x\n", (unsigned long long)rt.frameCount, c[0], c[1], c[2], size, model);
        out.push_back(turning(model, c, angle, model == kMagicianHand ? size * 0.4f : size));
    }
    // the Magician's Hand's bolts (port/game.h WeaponBolt): his fireball (magician_fireball's looping models 0x1CAB..
    // 0x1CB9, a model a frame) along its line, facing the camera; at the end its burst as magician_fireball_frame draws
    // it: the same model 1.5 times larger, growing while the hit flashes burn
    for (const auto& bt : rt.weaponBolts) {
        if (bt.seat < 0 || bt.frames <= 0)
            continue;
        const float f = bt.burst > 0 ? 1.0f : std::min(1.0f, float(bt.age) / float(bt.frames));
        float w[3], c[3];
        for (int a = 0; a < 3; ++a)
            w[a] = bt.from[a] + (bt.to[a] - bt.from[a]) * f;
        const float* m = rt.camera;
        for (int a = 0; a < 3; ++a)
            c[a] = m[a] * w[0] + m[3 + a] * w[1] + m[6 + a] * w[2] + m[9 + a];
        if (bt.burst > 0)   // (the burst a little toward the camera: not inside the creature it hit)
            for (int a = 0; a < 3; ++a)
                c[a] *= std::max(0.5f, (c[2] - 10.0f) / c[2]);
        if (c[2] < 2.0f)
            continue;
        const int step = bt.burst > 0 ? 14 - bt.burst : bt.age;
        const uint32_t model = kFireballFirst + uint32_t(step % int(kFireballLast - kFireballFirst + 1));
        // (in flight it grows to his fireball's 1.75 as his does after the throw; the burst 1.5 times that, growing)
        const float s = bt.burst > 0 ? 1.75f * 1.5f * (1.0f + float(step) / 5.0f) : 0.5f + 1.25f * f;
        const float x[3] = {1, 0, 0}, y[3] = {0, 1, 0}, z[3] = {0, 0, 1};
        out.push_back(placed(model, x, y, z, c, s));
    }
    // survfix (user 2026-11-04): no weapon in a hand on the screen outside VR. The arcade draws none (the light gun is
    // the player's gun; the players' gun hand 0x719 only appears in the cut-scenes), so neither does Survival:
    // the weapon shows by its name, its rounds and its shots. VR has its own hands (vr_scene.cpp)
}

std::string Weapons::summary() const
{
    std::string s;
    char b[96];
    for (int w = 0; w < kWeapons; ++w) {
        int shots = 0, reloads = 0, kills = 0, own = 0;
        for (int p = 0; p < 4; ++p)
            shots += seat[p].shots[w], reloads += seat[p].reloads[w], kills += seat[p].kills[w], own += seat[p].own[w] ? 1 : 0;
        std::snprintf(b, sizeof b, "%s%s=%d/%d/%d", s.empty() ? "" : ",", def(w).key.c_str(), shots, reloads, kills);
        s += b;
    }
    int sw = 0;
    for (const Seat& st : seat)
        sw += st.switches;
    std::snprintf(b, sizeof b, " switches=%d pickups=%d/%d", sw, taken, dropped);
    std::string held = " held(frames/reloading)";   // (survhand: the balance runs)
    for (int w = 0; w < kWeapons; ++w) {
        char t[64];
        std::snprintf(t, sizeof t, "%s%s=%lld/%lld", w ? "," : "=", def(w).key.c_str(), heldFrames[w], reloadingFrames[w]);
        held += t;
    }
    return "weapons(shots/reloads/kills)=" + s + b + held + (mode == Drops ? " upgrades=" + std::to_string(upgrades) : std::string());
}

} // namespace hotd::game
