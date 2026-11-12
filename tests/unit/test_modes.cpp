// the modes' pure parts: The Typing of the Dead 0's prompt picker (unique first letters, no word twice on the screen,
// not one of the last recent_words, never a quick key G / H first, the same seed the same words) and SEGA's judge
// (case, Shift grace); the weapons' table (data/weapons.ini = the built-in values) and magazine / reload maths;
// Survival's rounds, hit points, random streams (the same seed plays the same), arenas, prices and score table
#include "check.h"

#include "game/pads.h"
#include "game/survival.h"
#include "game/typing.h"
#include "game/weapons.h"
#include "game/lang.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;
using namespace hotd;

namespace hotd::test {
// the friend of Typing, SurvivalMode and Weapons (their headers): the private parts these tests look at
struct Access {
    static auto& targets(game::Typing& t) { return t.targets_; }
    static auto& recent(game::Typing& t) { return t.recent_; }
    static auto& lists(game::Typing& t) { return t.lists_; }
    static bool pick(game::Typing& t, const std::vector<std::string>& lists, int minLen, int maxLen, uint32_t self, std::string& out)
    {
        return t.pick(lists, minLen, maxLen, self, out);
    }
    static void addTarget(game::Typing& t, uint32_t obj, const std::string& text)
    {
        auto& x = t.targets_[obj];
        x.text = text;
    }
    static uint32_t next(game::SurvivalMode& s) { return s.next(); }
    static uint32_t& rng(game::SurvivalMode& s) { return s.rng_; }
    static uint32_t next(game::Weapons& w) { return w.next(); }
    struct GunDev { bool found = false, absolute = false; float u = 0, v = 0, dx = 0, dy = 0; bool trigger = false, reload = false; };
    static GunDev dev(const game::LightGuns& g, uint64_t id)
    {
        GunDev r;
        for (const auto& d : g.devs_)
            if (d.id == id) r = {true, d.absolute, d.u, d.v, d.dx, d.dy, d.eTrigger, d.eReload};
        return r;
    }
};
} // namespace hotd::test

using test::Access;

namespace {
bool loadTyping(game::Typing& t)
{
    std::string err;
    const bool ok = t.load({test::sourceDir()}, err);
    if (!ok) CHECK_MSG(false, "typing load: " + err);
    return ok;
}
char low(char c) { return char(std::tolower(uint8_t(c))); }
} // namespace

TEST(typing, dictionary_loads)
{
    game::Typing t;
    if (!loadTyping(t)) return;
    const auto& lists = Access::lists(t);
    for (const char* l : {"short", "easy", "words", "long", "phrases", "hard_phrases", "boss", "boss_easy", "boss_hard"})
        CHECK_MSG(lists.count(l) && lists.at(l).size() >= 10, l);
    // every prompt typable: printable ASCII (the typist's keyboard), no leading / trailing space
    for (const auto& [name, words] : lists)
        for (const std::string& w : words) {
            bool ok = !w.empty() && w.front() != ' ' && w.back() != ' ';
            for (char c : w) ok = ok && uint8_t(c) >= 0x20 && uint8_t(c) < 0x7F;
            CHECK_MSG(ok, name + ": \"" + w + "\"");
        }
    const auto& c = t.config();
    CHECK_EQ(c.recentWords, 16);
    CHECK(c.quickKeys && c.quickLeft == 'g' && c.quickRight == 'h');
}

TEST(typing, picker_rules)
{
    game::Typing t;
    if (!loadTyping(t)) return;
    t.start(2, 0x1234);
    const std::vector<std::string> lists = {"words"};
    // five targets on the screen: each new prompt's first letter is not one of theirs, not G / H, not shown twice
    std::set<char> firsts;
    for (uint32_t obj = 1; obj <= 8; ++obj) {
        std::string w;
        CHECK(Access::pick(t, lists, 3, 8, obj, w));
        const char f = low(w[0]);
        CHECK_MSG(!firsts.count(f), "first letter twice: " + w);
        CHECK_MSG(f != 'g' && f != 'h', "a quick key first: " + w);
        firsts.insert(f);
        Access::addTarget(t, obj, w);
    }
    // the last recent_words picks never again (targets cleared between picks: only the recent rule applies)
    Access::targets(t).clear();
    std::deque<std::string> last;
    for (int i = 0; i < 200; ++i) {
        std::string w;
        CHECK(Access::pick(t, lists, 0, 0, 999, w));
        CHECK_MSG(std::find(last.begin(), last.end(), w) == last.end(), "repeated within 16: " + w);
        last.push_back(w);
        if (last.size() > 16) last.pop_front();
    }
    CHECK(Access::recent(t).size() <= 16);
    // the same seed: the same prompts
    auto seq = [&](uint64_t seed) {
        game::Typing u;
        loadTyping(u);
        u.start(2, seed);
        std::string all;
        for (int i = 0; i < 30; ++i) {
            std::string w;
            Access::pick(u, lists, 3, 8, 7, w);
            all += w + ",";
        }
        return all;
    };
    CHECK_EQ(seq(5), seq(5));
    CHECK(seq(5) != seq(6));
    // a list nearly used up (every first letter taken): false, but still a prompt
    game::Typing v;
    loadTyping(v);
    v.start(2, 1);
    uint32_t obj = 1;
    for (char c = 'a'; c <= 'z'; ++c) Access::addTarget(v, obj++, std::string(1, c) + "xx");
    std::string w;
    CHECK(!Access::pick(v, lists, 3, 8, 500, w));
    CHECK(!w.empty());
}

TEST(typing, judge_case_and_shift_grace)
{
    game::Typing t;
    if (!loadTyping(t)) return;
    // Normal (2): case ignored
    t.start(2);
    CHECK(!t.caseSensitive());
    CHECK(t.keyMatches('a', 'a', false));
    CHECK(t.keyMatches('A', 'a', false));
    CHECK(t.keyMatches('a', 'A', false));
    CHECK(!t.keyMatches('a', 'b', false));
    CHECK(!t.keyMatches('a', 0, false));
    CHECK(!t.keyMatches('a', 0x100 + 'a', false));   // (not ASCII)
    // Shift let go a moment early: "Run!" typed "Run1" after a shifted key counts; not without the shift before it
    CHECK(t.keyMatches('!', '1', true));
    CHECK(!t.keyMatches('!', '1', false));
    CHECK(t.keyMatches('?', '/', true));
    CHECK(t.keyMatches('"', '\'', true));
    CHECK(!t.keyMatches('!', '2', true));
    // from case_from_difficulty on (typing.ini: 5, never; the built-in 3): capitals as shown
    t.config().caseFromDifficulty = 3;
    t.start(3);
    CHECK(t.caseSensitive());
    CHECK(!t.keyMatches('A', 'a', false));
    CHECK(t.keyMatches('A', 'A', false));
    CHECK(t.keyMatches('R', 'r', true));     // (Shift released early on a capital after a capital)
    CHECK(!t.keyMatches('a', 'A', false));
    // shift_grace off: none of that
    t.config().shiftGrace = false;
    CHECK(!t.keyMatches('!', '1', true));
    CHECK(!t.keyMatches('R', 'r', true));
    // the key pairs
    CHECK_EQ(game::Typing::shiftOf('a'), uint32_t('A'));
    CHECK_EQ(game::Typing::shiftOf('5'), uint32_t('%'));
    CHECK_EQ(game::Typing::shiftOf('`'), uint32_t('~'));
    CHECK_EQ(game::Typing::shiftOf('A'), uint32_t('A'));
    CHECK_EQ(game::Typing::shiftOf(0), 0u);
    CHECK(game::Typing::shiftedKey('Q') && game::Typing::shiftedKey('?') && !game::Typing::shiftedKey('q') && !game::Typing::shiftedKey('/'));
    // the key costs (Tod.exe): a s t h 3, most 4, c m n x z 5, p q 6, v 8, space 2, digits 10, symbols 13
    float f = 0;
    CHECK_NEAR(game::Typing::keyEffort("ast", false, f), 9 / 4.0, 1e-6);
    CHECK_NEAR(f, 1 / 1.1, 1e-6);   // (short)
    CHECK_NEAR(game::Typing::keyEffort("v p", false, f), (8 + 2 + 6) / 4.0, 1e-6);
    CHECK_NEAR(game::Typing::keyEffort("A", true, f), (3 + 4) / 4.0, 1e-6);
    CHECK_NEAR(game::Typing::keyEffort("A", false, f), 3 / 4.0, 1e-6);
    CHECK_NEAR(game::Typing::keyEffort("1!", false, f), 23 / 4.0, 1e-6);
    game::Typing::keyEffort(std::string(16, 'v'), false, f);
    CHECK_NEAR(f, 1 / 0.8, 1e-6);   // (long)
}

TEST(weapons, table_matches_ini)
{
    // data/weapons.ini shows the built-in values ("a key left out keeps the built-in value shown here"): the same
    game::loadWeapons((fs::path(test::tempDir("weapons")) / "none.ini").string());
    const std::vector<game::WeaponDef> defs = game::weaponDefs();
    std::string log;
    CHECK(game::loadWeapons((fs::path(test::sourceDir()) / "data" / "weapons.ini").string(), &log));
    const auto& ini = game::weaponDefs();
    if (!CHECK_EQ(ini.size(), defs.size())) return;
    CHECK_EQ(int(defs.size()), int(game::kWeapons));
    for (size_t i = 0; i < defs.size(); ++i) {
        const auto &a = defs[i], &b = ini[i];
        const std::string k = a.key;
        CHECK_MSG(a.key == b.key && a.name == b.name, k);
        CHECK_MSG(a.magazine == b.magazine && a.reserveMags == b.reserveMags && a.reload == b.reload, k + ": magazine / reload");
        CHECK_MSG(a.period == b.period && a.burst == b.burst && a.damage == b.damage && a.pellets == b.pellets, k + ": fire");
        CHECK_MSG(a.spread == b.spread && a.range == b.range && a.area == b.area && a.projectile == b.projectile && a.dual == b.dual, k + ": shape");
        CHECK_MSG(a.price == b.price && a.drop == b.drop && a.rank == b.rank, k + ": price / drop / rank");
        CHECK_MSG(a.pickupModel == b.pickupModel, k + ": pickup_model");
        CHECK_MSG(a.fireSound == b.fireSound && a.reloadSound == b.reloadSound && a.fireCue == b.fireCue && a.fireGameCue == b.fireGameCue,
                  k + ": sounds");
        CHECK_MSG(a.firePitch == b.firePitch && a.fireClip == b.fireClip && a.fireVolume == b.fireVolume && a.reloadCue == b.reloadCue &&
                      a.reloadPitch == b.reloadPitch,
                  k + ": stand-in sounds");
        CHECK_MSG(a.model == b.model && a.scale == b.scale, k + ": model");
        CHECK_MSG(b.magazine >= 1 && b.magazine <= 99 && b.reload >= 0 && b.burst >= 1, k + ": in range");
    }
    // the auto ranks: each weapon its own
    std::set<int> ranks;
    for (const auto& d : ini) CHECK_MSG(ranks.insert(d.rank).second, d.key);
}

TEST(weapons, magazine_maths)
{
    game::Weapons w;
    const auto& d = game::weaponDefs();
    for (int k = 0; k < game::kWeapons; ++k) {
        w.seat[0].bigMag = false;
        const int m = w.magazine(0, k);
        CHECK_EQ(m, std::min(99, d[size_t(k)].magazine));
        CHECK_EQ(w.reserveFull(0, k), d[size_t(k)].reserveMags < 0 ? 0 : m * d[size_t(k)].reserveMags);
        w.seat[0].bigMag = true;
        CHECK_EQ(w.magazine(0, k), std::min(99, d[size_t(k)].magazine * 3 / 2));   // (Big Mag: x 1.5)
        w.seat[0].cur = k;
        w.seat[0].quick = false;
        w.mode = game::Weapons::Survival;
        const int r = w.reloadFrames(0);
        CHECK_EQ(r, d[size_t(k)].reload);
        w.seat[0].quick = true;
        CHECK_EQ(w.reloadFrames(0), r / 2);   // (Quick Reload: half)
        // Drops: the handgun reloads at once (the arcade's off-screen pull)
        w.mode = game::Weapons::Drops;
        w.seat[0].quick = false;
        CHECK_EQ(w.reloadFrames(0), k == game::kPistol ? 0 : d[size_t(k)].reload);
    }
    CHECK_EQ(w.magazine(4, 0), w.magazine(0, 0));   // (a seat number wraps: p & 3)
    // the random stream: the same start, the same draws; a copy goes on alike (rollback copies the whole object)
    game::Weapons a, b;
    for (int i = 0; i < 100; ++i) CHECK_EQ(Access::next(a), Access::next(b));
    game::Weapons c = a;
    CHECK_EQ(Access::next(c), Access::next(a));
    CHECK_EQ(a.digest(), c.digest());
    a.seat[2].mag[1] = 3;
    CHECK(a.digest() != c.digest());
}

TEST(survival, rounds_and_health)
{
    using S = game::SurvivalMode;
    int prev = 0;
    for (int r = 1; r <= 80; ++r) {
        const int n1 = S::roundCount(r, 1);
        CHECK_MSG(n1 >= prev, "round " + std::to_string(r) + " has fewer creatures than the one before");
        CHECK(n1 >= 1 && n1 <= 150);
        prev = n1;
        for (int p = 2; p <= 4; ++p) CHECK_MSG(S::roundCount(r, p) >= S::roundCount(r, p - 1), "round " + std::to_string(r));
        CHECK_EQ(S::roundCount(r, 9), S::roundCount(r, 4));   // (players clamped)
        CHECK_MSG(S::roundHealth(r + 1) >= S::roundHealth(r), "health falls after round " + std::to_string(r));
    }
    CHECK_EQ(S::roundCount(1, 1), 6);
    CHECK_EQ(S::roundCount(0, 1), 6);
    CHECK_NEAR(S::roundHealth(3), 1.0, 0);
    CHECK_NEAR(S::roundHealth(9), 1.72, 1e-5);
    CHECK_NEAR(S::roundHealth(10), 1.72 * 1.1, 1e-4);   // (continuous at the switch to x 1.1)
}

TEST(survival, seeded_and_tables)
{
    // the director's stream: the seed decides it (begin starts it from the seed)
    game::SurvivalMode a, b;
    a.seed = b.seed = 0xABCDEF;
    a.begin(0), b.begin(0);
    CHECK_EQ(Access::rng(a), 0xABCDEFu);
    for (int i = 0; i < 1000; ++i) CHECK_EQ(Access::next(a), Access::next(b));
    CHECK_EQ(a.digest(), b.digest());
    game::SurvivalMode c;
    c.seed = 0xABCDEE;
    c.begin(0);
    CHECK(c.digest() != b.digest() || Access::next(c) != Access::next(b));
    // survdogs: the first dogs' round 7 or 8 by the seed, from its own stream (the director's untouched: rng is the
    // seed after begin, checked above); the same seed the same round; over many seeds both 7 and 8; in the digest
    {
        bool seen[2] = {};
        for (uint32_t sd = 1; sd <= 64; ++sd) {
            game::SurvivalMode d, e;
            d.seed = e.seed = sd * 0x9E3779B9u;
            d.begin(0), e.begin(0);
            CHECK_MSG(d.dogWave == 7 || d.dogWave == 8, std::to_string(d.dogWave));
            CHECK_EQ(d.dogWave, e.dogWave);
            CHECK_EQ(Access::rng(d), d.seed);
            seen[d.dogWave - 7] = true;
        }
        CHECK(seen[0] && seen[1]);
        game::SurvivalMode d = b;
        d.dogKilled = 3;
        CHECK(d.digest() != b.digest());
        d = b;
        d.dogWave += 1;
        CHECK(d.digest() != b.digest());
    }
    // begin keeps the setup, forgets the game
    a.kills = 50, a.wave = 7, a.maxAlive = 10;
    a.begin(0);
    CHECK(a.kills == 0 && a.wave == 0 && a.maxAlive == 10 && a.seed == 0xABCDEFu);
    // arenas: each key and number parses back; nonsense none
    const auto& ar = game::survivalArenas();
    CHECK(!ar.empty());
    for (size_t i = 0; i < ar.size(); ++i) {
        CHECK_EQ(game::survivalArenaParse(ar[i].key), int(i));
        CHECK_EQ(game::survivalArenaParse(std::to_string(i + 1)), int(i));
    }
    CHECK_EQ(game::survivalArenaParse("nowhere"), -1);
    CHECK_EQ(game::survivalArenaParse(""), 0);
    // the shop: every item priced, a weapon's price its table's
    for (int it = 0; it < game::SurvivalMode::kItems; ++it) CHECK_MSG(a.price(it) > 0, std::to_string(it));
    CHECK_EQ(a.price(-1), 0);
    CHECK_EQ(a.price(game::SurvivalMode::kItems), 0);
    // the score table: insert ranks, save / load round trip
    game::SurvivalScores s;
    game::SurvivalScores::Row r{"AAA", 9999999, 12, 0};
    const int rank = s.insert(r);
    CHECK_EQ(rank, 0);
    CHECK_EQ(s.insert({"BBB", 0, 0, 0}), -1);
    CHECK(s.isHigh(999999));
    const std::string f = (fs::path(test::tempDir("survival")) / "survival_scores.txt").string();
    CHECK(s.save(f));
    game::SurvivalScores l;
    CHECK(l.load(f));
    for (int i = 0; i < 10; ++i)
        CHECK_MSG(l.rows[i].name == s.rows[i].name && l.rows[i].score == s.rows[i].score && l.rows[i].wave == s.rows[i].wave &&
                      l.rows[i].arena == s.rows[i].arena,
                  std::to_string(i));
    // sorted best first
    for (int i = 1; i < 10; ++i) CHECK(l.rows[i - 1].score >= l.rows[i].score);
}

TEST(evdev, packets)
{
    // linuxmice: the kernel's input_event stream (linux/input.h numbers) into the light guns' raw input
    constexpr unsigned kSyn = 0, kKey = 1, kRel = 2, kAbs = 3, kReport = 0, kDropped = 3, kLeft = 0x110, kRight = 0x111;
    game::LightGuns g;
    g.want[0] = true;   // (a gun player: raw input fed)
    // a mouse: relative motion summed until SYN_REPORT
    g.evdevEvent(1, kRel, 0, 5);
    g.evdevEvent(1, kRel, 1, -3);
    g.evdevEvent(1, kRel, 0, 2);
    CHECK(!Access::dev(g, 1).found);   // (nothing before the report)
    g.evdevEvent(1, kSyn, kReport, 0);
    auto m = Access::dev(g, 1);
    CHECK(m.found && !m.absolute);
    CHECK_NEAR(m.dx, 7, 0);
    CHECK_NEAR(m.dy, -3, 0);
    // a gun: its ABS range is the window (fractions)
    g.evdevRange(2, 0, 1000, 100, 300);
    g.evdevEvent(2, kAbs, 0, 250);
    g.evdevEvent(2, kAbs, 1, 250);
    g.evdevEvent(2, kSyn, kReport, 0);
    auto a = Access::dev(g, 2);
    CHECK(a.found && a.absolute);
    CHECK_NEAR(a.u, 0.25, 1e-6);
    CHECK_NEAR(a.v, 0.75, 1e-6);
    // only X moved: Y kept from before
    g.evdevEvent(2, kAbs, 0, 1000);
    g.evdevEvent(2, kSyn, kReport, 0);
    a = Access::dev(g, 2);
    CHECK_NEAR(a.u, 1.0, 1e-6);
    CHECK_NEAR(a.v, 0.75, 1e-6);
    // the gun at its range's corner (off screen / no light): off the window
    g.evdevEvent(2, kAbs, 0, 0);
    g.evdevEvent(2, kAbs, 1, 100);
    g.evdevEvent(2, kSyn, kReport, 0);
    a = Access::dev(g, 2);
    CHECK(a.u < 0 && a.v < 0);
    // the trigger: BTN_LEFT down; auto-repeat (2) ignored
    g.evdevEvent(2, kKey, kLeft, 1);
    g.evdevEvent(2, kKey, kLeft, 2);
    g.evdevEvent(2, kSyn, kReport, 0);
    CHECK(Access::dev(g, 2).trigger);
    g.evdevEvent(2, kKey, kRight, 1);
    g.evdevEvent(2, kSyn, kReport, 0);
    CHECK(Access::dev(g, 2).reload);
    // SYN_DROPPED: that packet and everything to the next report are skipped
    const auto before = Access::dev(g, 1);
    g.evdevEvent(1, kRel, 0, 100);
    g.evdevEvent(1, kSyn, kDropped, 0);
    g.evdevEvent(1, kRel, 0, 50);
    g.evdevEvent(1, kSyn, kReport, 0);
    CHECK_NEAR(Access::dev(g, 1).dx, before.dx, 0);
    g.evdevEvent(1, kRel, 0, 1);
    g.evdevEvent(1, kSyn, kReport, 0);
    CHECK_NEAR(Access::dev(g, 1).dx, before.dx + 1, 0);
    // no gun player (not active): only counted as "seen", in the order used
    game::LightGuns q;
    q.evdevEvent(7, kRel, 0, 3);
    q.evdevEvent(7, kSyn, kReport, 0);
    q.evdevEvent(8, kKey, kLeft, 1);
    q.evdevEvent(8, kSyn, kReport, 0);
    q.evdevEvent(7, kRel, 1, 3);
    q.evdevEvent(7, kSyn, kReport, 0);
    CHECK_EQ(q.evdevMiceSeen(), 2);
    CHECK(!Access::dev(q, 7).found);
    // the Assign page: the first trigger pull takes the seat
    q.startAssign(1);
    q.evdevEvent(9, kKey, kLeft, 1);
    q.evdevEvent(9, kSyn, kReport, 0);
    CHECK_EQ(q.assigned.seat, 1);
    CHECK_EQ(q.assigning(), -1);
}
