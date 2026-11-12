// Weapons (ours, user 2026-11-04; docs/weapons.md): Survival Mode's guns (and the other modes'). The
// handgun (the game's own), the SMG (the handgun's model for now), the assault rifle, the double pistols (two guns, two
// flashes, a bigger magazine), the shotgun (a spread of rays around the aim, short range) and the Magician's Hand (his
// hand on the screen, his projectile's sound, the game's hit flash as an explosion: area damage, few shots).
//
// Each weapon's numbers and sounds come from data/weapons.ini (the built-in defaults below when it is missing): fire
// rate (a held trigger: a shot every n frames; a pull from an input that cannot hold, a pad or a light gun: a burst),
// damage (percent of the handgun's, part_hit's own damage scaled), magazine, reserve, reload time, the rays and the
// explosion, the models, the sounds (data/weapons/<name>.wav when the user drops one there, else the game's own: the
// handgun's shot sped up / slowed down, the Magician's projectile cue). Prices (Survival's shop) and pickup weights
// (the kills' drops) too.
//
// The game's shot is used as it is: the input's trigger fires player_shoot (one round, the ray, the flash, the
// sound); the weapon adds what the handgun has not (port/game.h WeaponShot: extra rays, the explosion, the damage
// percent), set for each seat before its frame. The magazine is the game's own rounds counter (Player +0x3D) with the
// seat's magazine size (Game::survivalRounds); the HUD's bullets show up to 6, past that one bullet and "x N".
//
// Switching: the mouse wheel, the number keys 1-6 (player 1), a pad's shoulder buttons / d-pad left / right (in these
// modes only), and for light guns an "auto best" choice (the best weapon with ammo at each pull). In co-op every seat has
// its own weapons. Normal play: Weapons::mode Off and Game::weaponHooks false: nothing of it runs.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hotd::test { struct Access; }   // (tests/unit: hotd_unit's look at the private parts)

namespace hotd::game {

class Runtime;
struct DrawModel;

// (Light Gun Edition: the Killer Mods' machine gun and rocket launcher after Survival's six: Arsenal / Machine Gun
// mode only, never sold or dropped)
enum WeaponId { kPistol, kSmg, kRifle, kDual, kShotgun, kHand, kMachine, kRocket, kWeapons };

struct WeaponDef {
    std::string key;                // the ini section / its id (pistol, smg, rifle, dual, shotgun, hand)
    std::string name;               // shown (an English text: through the language files)
    int magazine = 6;               // rounds a magazine (1..99)
    int reserveMags = 4;            // magazines outside the gun when full (bought / a pickup / Max Ammo); -1: never runs out
    int reload = 36;                // frames a reload takes (0: at once, the game's own)
    int period = 0;                 // a held trigger fires every n frames (0: one shot a pull)
    int burst = 1;                  // a pull of an input that cannot hold (pads, light guns): shots
    int damage = 100;               // percent of the game's own damage a hit
    int pellets = 0;                // extra rays (the shotgun)
    float spread = 0;               // their spread around the aim (arcade screen units)
    float range = 0;                // they hit only nearer than this (camera units; 0: any)
    bool keepEmpty = false;         // used up: still held, empty (Ammo / Max Ammo fill it), not gone (survhand: the Hand)
    float area = 0;                 // an explosion at the hit (camera units; 0: none)
    bool projectile = false;        // the explosion flies there first: the Magician's fireball (survfix), no direct hit
    float dual = 0;                 // two guns: the second's offset (arcade screen units; 0: one gun)
    uint32_t model = 0x719;         // (survfix: not drawn; no weapon is shown in a hand outside VR, as the arcade)
    uint32_t pickupModel = 0;       // the model of its pickup / shop item (0: the gun without the hand)
    float scale = 1.0f;             // (the on-screen model's size: not drawn either)
    std::string fireSound, reloadSound;   // data/weapons/<name>.wav (empty: the game's own shot / reload)
    int fireCue = 30;               // the stand-in: the PC's cue played ..
    float firePitch = 1.0f, fireClip = 1.0f, fireVolume = 1.0f;   // .. that much faster, cut to that share, that loud
    uint32_t fireGameCue = 0;       // instead: one of the game's own cues (the Magician's projectile 0xA01463)
    int reloadCue = 28;             // the reload's stand-in (the PC's cue 28: the handgun's reload) ..
    float reloadPitch = 1.0f;       // .. that much faster
    int price = 0;                  // Survival's shop (0: not sold)
    int drop = 0;                   // Drops: the weight of its pickup among the drops (0: never dropped)
    int rank = 0;                   // the auto best weapon's order (higher first)
};

// the table (data/weapons.ini over the defaults; loaded once, reloaded by loadWeapons)
const std::vector<WeaponDef>& weaponDefs();
// the ini's [options]: auto_best_light_gun (1): a light gun's player gets the best weapon with ammo at each pull (a gun
// has no wheel or shoulder buttons); 0: the handgun until switched (a pad's buttons)
struct WeaponOptions { bool autoBestLightGun = true; };
const WeaponOptions& weaponOptions();
bool loadWeapons(const std::string& path, std::string* log = nullptr);   // false: no file (the defaults stay)
std::string weaponsIniPath();     // the first data folder's data/weapons.ini
std::string weaponsSoundDir();    // data/weapons (the user's sound files)

class Weapons {
public:
    // Arsenal (Light Gun Edition, Killer Mods): every player has every weapon (only: one weapon alone, Shotgun /
    // Machine Gun mode), the middle button steps through them; their reserves never run out but the rocket launcher's
    enum Mode { Off, Survival, Drops, Arsenal };
    int only = -1;                  // Arsenal: the one weapon of Shotgun / Machine Gun mode (-1: every weapon)
    bool endless(int w) const;      // its reserve never runs out (the handgun; Arsenal: all but the rocket launcher)
    void arm(Runtime& rt, int p);   // Arsenal: the seat's weapons (every one, or the only one in the hand)
    Mode mode = Off;
    bool live = false;              // the weapons work now (Survival: the arena reached; Drops: in play)
    bool autoBest[4] = {};          // this seat's weapon is picked for it (a light gun; main sets it each frame)
    int request[4] = {};            // the input's switch: 0 none, -1 / +1 previous / next, 100 + id that weapon
    static constexpr int kPickupFrames = 420;   // Drops: a pickup stays 7 s
    static constexpr int kDropChance = 9;       // Drops: one kill in n drops one (the copies make it ~3x the kills)

    struct Seat {
        bool own[kWeapons] = {true};
        int mag[kWeapons] = {};
        int reserve[kWeapons] = {};
        int cur = kPistol;
        int reload = 0;             // frames left of the reload (0: ready)
        int timer = 0, burst = 0;   // the held fire's count; a pull's burst left
        bool quick = false, bigMag = false;   // Survival's perks: half the reload, magazines x 1.5
        bool down = false;          // Drops: out of lives (its weapons kept for the continue: afterFrame)
        uint64_t shotFrame = 0;     // the last shot's frame (the cue's seat, the model's kick)
        int shots[kWeapons] = {}, reloads[kWeapons] = {}, switches = 0, kills[kWeapons] = {};
    };
    Seat seat[4];
    // hold: its time runs only with the shots on; upgrade: Survival's perks instead of a weapon (givePerk)
    struct Pickup { float pos[3]; int weapon; int frames; bool hold = false; bool upgrade = false; };
    std::vector<Pickup> pickups;    // the drops lying in the world
    static constexpr size_t kMaxPickups = 10;   // (the kills' drops: 6 of them at most)
    int pickDrop();                 // a weapon by the drop weights (-1: none)
    // hordepub (user 2026-11-09: weapons from rescued researchers, crates, the bonus room): a pickup at a camera-space
    // place (x right, y up, z ahead: as Obj +0x38), turned into the world's; why: the log's. false: none (full, Off).
    // upgrade: an upgrade pickup (weapon ignored; Survival's QUICK RELOAD sign's model, the game's gold coin)
    bool dropAt(Runtime& rt, const float cam[3], int weapon, int frames, bool hold, const char* why, bool upgrade = false);
    // hordepub (user 2026-11-09: "upgrades (Survival's Big Mag, Quick Reload, ...)" in Horde): the seat's next upgrade,
    // Survival's shop order: Quick Reload, then Big Mag, then Max Ammo (every weapon held full); its name (the notice)
    std::string givePerk(Runtime& rt, int p);
    int upgrades = 0;               // (the stats: upgrades given)
    // (survhand, the balance runs' line: per weapon the frames it was in a hand in play with the shots on, and of
    // those the frames it was reloading; stats only, not in the digest)
    long long heldFrames[kWeapons] = {}, reloadingFrames[kWeapons] = {};
    int dropped = 0, taken = 0;     // Drops: pickups made / taken
    int reloadSeat = 0;             // the seat whose reload sound plays (the reload cue's)

    bool on() const { return mode != Off; }
    void begin(Runtime& rt, Mode m);
    void end(Runtime& rt);
    int magazine(int p, int w) const;      // the seat's magazine of that weapon (Big Mag: x 1.5)
    int reserveFull(int p, int w) const;   // its full reserve
    int reloadFrames(int p) const;         // the current weapon's reload (Quick Reload: half)
    bool give(Runtime& rt, int p, int w);  // owned, a full magazine and reserve; true when it was new
    void refill(Runtime& rt, int p, int w = -1);   // the reserve full (-1: every owned weapon, the magazines too)
    bool select(Runtime& rt, int p, int w);        // that weapon in the hand (owned, ammo left)
    void cycle(Runtime& rt, int p, int dir);       // the next / previous owned one with ammo
    void resetSeat(Runtime& rt, int p);            // back to the handgun alone (Drops: a continue)
    bool hasAmmo(int p, int w) const;
    int best(int p) const;                         // the auto best weapon of the seat
    // the input's gun (Runtime::input_task_stand_in) for seat p, after Survival's shop: the switch, the pickups, the
    // reload (by hand: a pull off the screen; it takes reloadFrames from the reserve), the held / burst fire, the shot's
    // extra rays (Game::weaponShot)
    void gunInput(Runtime& rt, int p, bool& trigger, bool onScreen, bool held, float x, float y);
    void afterFrame(Runtime& rt);                  // Runtime::frame: the pickups' time, the magazines kept
    void kill(Runtime& rt, uint32_t obj, int killer);   // Game::weapon_kill: the drops
    bool cue(Runtime& rt, uint32_t cue);           // sound_cue's first look: the weapons' own shots / reloads
    // survfix3: Game::weapon_world_ray (the Magician's Hand's fireball: where its aim ray meets the stage): the view's
    // triangles drawn this frame (the ragdolls' walls and ground, Runtime::ragdollWorld); without them (headless) the
    // floor of the creature nearest the ray. o / d world space (d a unit vector); t: the distance, within maxT
    bool worldRay(const Runtime& rt, const float o[3], const float d[3], float maxT, float& t) const;
    // the models (camera space, as the view's overlay): the pickups turning where they lie, Survival's shop items
    // over their signs (shopItems), all on one clock; no weapon in a hand (survfix: the arcade shows none; VR: its own)
    struct ShopItem { uint32_t model; float x, y, scale; };   // arcade screen units (y up)
    std::vector<ShopItem> shopItems;               // Survival: this frame's floating items (set by SurvivalMode)
    void overlay(const Runtime& rt, std::vector<DrawModel>& out) const;
    std::string notice[4];                         // a seat's moment ("SMG EMPTY", "RIFLE"): the HUD shows it ..
    uint64_t noticeUntil[4] = {};                  // .. up to this frame
    // a pickup under the aim (arcade screen units): its index, -1 none
    int pickupAt(const Runtime& rt, float x, float y) const;
    bool pickupScreen(const Runtime& rt, int i, float& x, float& y) const;   // its place on the screen (false: behind)
    std::string summary() const;                   // the test line's part
    // online (Survival, protocol 22): a digest of the weapons' logic state (each seat's weapons, magazines, reloads,
    // perks, the requests, the pickups, the random stream; not the notices' texts) for the desync check
    uint64_t digest() const;
    const WeaponDef& def(int w) const { return weaponDefs()[size_t(w)]; }

private:
    friend struct ::hotd::test::Access;   // (tests/unit: the weapons' random stream)
    uint32_t rng_ = 0x57504E53;     // own stream ("WPNS"): the game's rand() is not touched
    uint32_t next();
    std::vector<std::pair<uint32_t, uint64_t>> killed_;   // (Drops) creatures already counted (a kill reported twice)
    void setShot(Runtime& rt, int p, bool firing);
};

} // namespace hotd::game
