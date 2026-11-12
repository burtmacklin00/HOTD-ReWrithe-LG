// rollback (net/rollback.h) and the desync hash (NetGame::gameHash) on a runtime with synthetic memory (no ROMs: the
// work RAM zeroed, nothing run): the same state hashes the same, any change of the state a hash covers changes it,
// a snapshot's restore puts every saved part back (the story's work RAM, the 4-seat fields, PC Mode, Boss Mode,
// Survival and its weapons, the Hand's bolts / explosions and their clock)
#include "check.h"

#include "game/runtime.h"
#include "net/net_game.h"
#include "net/rollback.h"

#include <cstring>
#include <memory>

using namespace hotd;

namespace {

struct World {
    port::Mem mem;
    std::vector<uint32_t> copro = std::vector<uint32_t>(0x8000, 0);
    std::unique_ptr<port::Tgp> tgp;
    std::unique_ptr<game::Runtime> rt;
    World()
    {
        mem.map(port::Mem::kRam, port::Mem::kRamSize);
        mem.map(0x1800000, 0x20000);   // (a 2D RAM's place: written regions above the work RAM are saved too)
        tgp = std::make_unique<port::Tgp>(copro.data(), copro.size());
        rt = std::make_unique<game::Runtime>(mem, *tgp);
    }
    uint64_t hash() { return net::NetGame::gameHash(*rt); }
    void fill(uint32_t seed)
    {
        uint32_t x = seed;
        for (uint32_t a = port::Mem::kRam; a < port::Mem::kRam + port::Mem::kRamSize; a += 4) {
            x = x * 1664525u + 1013904223u;
            mem.set32(a, x);
        }
    }
};

void survivalOn(game::Runtime& rt)
{
    rt.survival.arena = 0;
    rt.survival.phase = game::SurvivalMode::Wave;
    rt.weapons.mode = game::Weapons::Survival;
}

} // namespace

TEST(rollback, hash_deterministic)
{
    World a, b;
    a.fill(7), b.fill(7);
    CHECK_EQ(a.hash(), b.hash());
    CHECK_EQ(a.hash(), a.hash());
    // one byte anywhere in the work RAM (outside the sound queue)
    for (uint32_t at : {0x500000u, 0x512339u, 0x5123A0u, 0x5ABCDEu, 0x5FFFFFu}) {
        const uint64_t before = b.hash();
        b.mem.at<uint8_t>(at) ^= 0x10;
        CHECK_MSG(b.hash() != before, std::to_string(at));
        b.mem.at<uint8_t>(at) ^= 0x10;
        CHECK_EQ(b.hash(), before);
    }
    // the sound command queue (0x512340..0x51239F: it differs with the sound source, flow.cpp) is left out
    {
        const uint64_t before = b.hash();
        b.mem.at<uint8_t>(0x512345) ^= 0x10;
        b.mem.at<uint8_t>(0x51239F) ^= 0x10;
        CHECK_EQ(b.hash(), before);
    }
    // a runtime field outside the hash's mode (a 2-seat story game: the work RAM only) leaves it as it is
    const uint64_t h = a.hash();
    a.rt->frameCount += 100;
    CHECK_EQ(a.hash(), h);
}

TEST(rollback, hash_covers_modes)
{
    // Survival: the mode's and the weapons' state, the Hand's bolts, explosions and their clock
    {
        World w;
        survivalOn(*w.rt);
        const uint64_t h0 = w.hash();
        World plain;
        CHECK(h0 != plain.hash());   // (Survival on: its digest is in)
        auto changed = [&](const char* what, auto&& change) {
            World x;
            survivalOn(*x.rt);
            CHECK_EQ(x.hash(), h0);
            change(*x.rt);
            CHECK_MSG(x.hash() != h0, what);
        };
        changed("survival.kills", [](game::Runtime& r) { r.survival.kills = 3; });
        changed("survival.wave", [](game::Runtime& r) { r.survival.wave = 2; });
        changed("survival.seat points", [](game::Runtime& r) { r.survival.seat[1].points = 50; });
        changed("survival.buys", [](game::Runtime& r) { r.survival.seat[0].buys[game::SurvivalMode::Hand] = 1; });
        changed("weapons.mag", [](game::Runtime& r) { r.weapons.seat[0].mag[0] = 5; });
        changed("weapons.reload", [](game::Runtime& r) { r.weapons.seat[3].reload = 9; });
        changed("weapons.cur", [](game::Runtime& r) { r.weapons.seat[1].cur = 2; });
        changed("weapons.pickup", [](game::Runtime& r) { r.weapons.pickups.push_back({{1, 2, 3}, 2, 100}); });
        changed("bolt", [](game::Runtime& r) { r.weaponBolts[2].seat = 1, r.weaponBolts[2].age = 4; });
        changed("bolt target", [](game::Runtime& r) { r.weaponBolts[0].to[1] = 0.5f; });
        changed("blast", [](game::Runtime& r) { r.weaponBlast[1].seat = 0, r.weaponBlast[1].frame = 77; });
        changed("blast done", [](game::Runtime& r) { r.weaponBlast[0].done[0] = 0x123, r.weaponBlast[0].doneN = 1; });
        changed("weaponFrame", [](game::Runtime& r) { r.weaponFrame = 1234; });
    }
    // Boss Mode: the mode's clock, phase and boss
    {
        World w;
        w.rt->boss.mode = 6;
        const uint64_t h0 = w.hash();
        World x;
        x.rt->boss.mode = 6;
        CHECK_EQ(x.hash(), h0);
        x.rt->boss.timeMs = 1500.25;
        CHECK(x.hash() != h0);
        x.rt->boss.timeMs = 0;
        CHECK_EQ(x.hash(), h0);
        x.rt->boss.index = 2;
        CHECK(x.hash() != h0);
    }
    // the 4-seat game: the seats' start requests and input counters
    {
        World w;
        w.rt->seats4 = 4;
        if (w.rt->coop4_on()) {
            const uint64_t h0 = w.hash();
            w.rt->startsIn[3] = 1;
            CHECK(w.hash() != h0);
            w.rt->startsIn[3] = 0;
            w.rt->startRequest[2] = true;
            CHECK(w.hash() != h0);
        }
    }
    // PC Mode: its runtime fields
    {
        World w;
        w.rt->pcmode = 1;
        const uint64_t h0 = w.hash();
        w.rt->pm_continues = 3;
        CHECK(w.hash() != h0);
    }
}

TEST(rollback, snapshot_restores_everything)
{
    World w;
    w.fill(42);
    survivalOn(*w.rt);
    w.rt->seats4 = 4;
    w.rt->frameCount = 1000;
    w.rt->gun[0].x = 10, w.rt->gun[0].trigger = true;
    w.rt->survival.kills = 5, w.rt->survival.seat[0].points = 120;
    w.rt->weapons.seat[0].mag[1] = 7;
    w.rt->weaponFrame = 999;
    w.rt->weaponShot[1].rays = 2;
    w.rt->weaponBolts[3].seat = 1;
    w.rt->weaponBlast[2].seat = 0, w.rt->weaponBlast[2].frame = 990;
    w.rt->boss.timeMs = 77.5;
    w.mem.set32(0x1800010, 0xDEADBEEF);
    std::vector<uint8_t> ram(port::Mem::kRamSize);
    std::memcpy(ram.data(), w.mem.ptr(port::Mem::kRam), ram.size());
    const uint64_t h0 = w.hash();

    net::RuntimeState s;
    s.save(*w.rt);
    // a predicted frame changes everything
    w.fill(43);
    w.rt->frameCount = 1001;
    w.rt->gun[0].x = -50, w.rt->gun[0].trigger = false;
    w.rt->survival.kills = 9, w.rt->survival.seat[0].points = 10, w.rt->survival.wave = 4;
    w.rt->weapons.seat[0].mag[1] = 0, w.rt->weapons.pickups.push_back({{0, 0, 0}, 1, 5});
    w.rt->weaponFrame = 1000;
    w.rt->weaponShot[1].rays = 0;
    w.rt->weaponBolts[3].seat = -1;
    w.rt->weaponBlast[2].frame = 1000;
    w.rt->boss.timeMs = 99;
    w.mem.set32(0x1800010, 0);
    CHECK(w.hash() != h0);

    s.restore(*w.rt);
    CHECK_EQ(w.hash(), h0);
    CHECK(std::memcmp(ram.data(), w.mem.ptr(port::Mem::kRam), ram.size()) == 0);
    CHECK_EQ(w.mem.u32(0x1800010), 0xDEADBEEFu);
    CHECK_EQ(w.rt->frameCount, uint64_t(1000));
    CHECK(w.rt->gun[0].x == 10 && w.rt->gun[0].trigger);
    CHECK_EQ(w.rt->survival.kills, 5);
    CHECK_EQ(w.rt->survival.wave, 0);
    CHECK_EQ(w.rt->survival.seat[0].points, 120);
    CHECK_EQ(w.rt->weapons.seat[0].mag[1], 7);
    CHECK(w.rt->weapons.pickups.empty());
    CHECK_EQ(w.rt->weaponFrame, uint64_t(999));
    CHECK_EQ(w.rt->weaponShot[1].rays, 2);
    CHECK_EQ(w.rt->weaponBolts[3].seat, 1);
    CHECK_EQ(w.rt->weaponBlast[2].frame, uint64_t(990));
    CHECK_EQ(w.rt->boss.timeMs, 77.5);
    // a second save into the same snapshot (its buffers reused) and restore: the same again
    s.save(*w.rt);
    w.fill(1);
    s.restore(*w.rt);
    CHECK_EQ(w.hash(), h0);
}
