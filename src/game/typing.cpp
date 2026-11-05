// The Typing of the Dead 0 (typing.h): prompts, lock-on, the letters' shots, the attack window, the stats, the
// scripted typist.
#include "game/typing.h"

#include "game/bot.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace hotd::port { extern bool (*gAttackHold)(uint32_t obj); extern bool (*gHitHold)(int player); extern bool (*gPreyHold)(uint32_t obj); }   // the hooks (port/game.h)

namespace hotd::game {

using G = A::G;
using port::a::Player;

namespace {
constexpr int kKeepUnseen = 300;     // frames a prompt stays with an enemy out of the list (a flicker, a duck, behind
                                     // something): it keeps its word when it comes back (totd1: was 60, words changed)
constexpr int kFinishTries = 40;     // frames the killing shot is tried (the enemy behind something, moving)
constexpr int16_t kHeldHp = 30000;   // a letter's shot: the hit points for that frame (no kill)
constexpr int kSmallTries = 6;       // killing shots at a small target / projectile before its hit is given (below)
constexpr int kSmallHp = 10;        // creatures with this many hit points or fewer get no letter shots (a bat dies at
                                     // any hit: its first letter killed it, totd1)
constexpr uint64_t kMeasure = 8;     // frames an enemy is tracked before its prompt (its speed toward the camera: the
                                     // word's length by threat, totd2)

std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
char low(uint32_t c) { return c < 128 ? char(std::tolower(int(c))) : '\0'; }
// the routine's name (names.tsv) without the address
std::string nameOf(Runtime& rt, uint32_t routine)
{
    const std::string n = rt.routineName(routine);
    const size_t sp = n.find(' ');
    return sp == std::string::npos ? std::string() : n.substr(sp + 1);
}
std::vector<std::string> splitList(const std::string& v)
{
    std::vector<std::string> out;
    std::stringstream ss(v);
    for (std::string p; std::getline(ss, p, ',');)
        if (!trim(p).empty()) out.push_back(trim(p));
    return out;
}
// "a, b, c, d, e": one value per difficulty (fewer: the last one repeated)
template <class T, class F> void perDifficulty(const std::string& v, T (&out)[5], F conv)
{
    const std::vector<std::string> p = splitList(v);
    for (int d = 0; d < 5 && !p.empty(); ++d)
        out[d] = conv(p[size_t(std::min<int>(d, int(p.size()) - 1))]);
}
// "1-1, 1-2, 2-3, ...": letters min-max per difficulty
void letters(const std::string& v, int (&mn)[5], int (&mx)[5])
{
    std::string r[5];
    perDifficulty(v, r, [](const std::string& s) { return s; });
    for (int d = 0; d < 5; ++d) {
        int a = 0, b = 0;
        if (std::sscanf(r[d].c_str(), "%d-%d", &a, &b) == 2)
            mn[d] = std::max(1, a), mx[d] = std::max(mn[d], b);
    }
}

// port::gAttackHold during a Typing of the Dead 0 tick
Typing* gHolder = nullptr;
bool holdHook(uint32_t obj) { return gHolder && gHolder->holds(obj); }
bool hitHook(int player) { return gHolder && gHolder->holdsHit(player); }
bool preyHook(uint32_t obj) { return gHolder && gHolder->holdsPrey(obj); }
} // namespace

Typing::Typing() : rng_(0x70D0), classify_(std::make_unique<Bot>(0x70D0, 1)) {}
Typing::~Typing()
{
    if (gHolder == this) {
        gHolder = nullptr;
        port::gAttackHold = nullptr;
        port::gHitHold = nullptr;
        port::gPreyHold = nullptr;
    }
}

size_t Typing::promptCount() const
{
    size_t n = 0;
    for (const auto& [k, v] : lists_) n += v.size();
    return n;
}

bool Typing::load(const std::vector<std::string>& dataDirs, std::string& err, const std::string& dictionary)
{
    std::error_code ec;
    const auto str = [](const std::string& s) { return s; };
    const auto flt = [](const std::string& s) { return float(std::atof(s.c_str())); };
    // the tuning: each folder's typing.ini in turn (a mod's last: it overrides)
    for (const std::string& d : dataDirs) {
        std::ifstream f(d + "/data/typing/typing.ini");
        for (std::string line; std::getline(f, line);) {
            line = trim(line.substr(0, line.find('#')));
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
            const int i = std::atoi(v.c_str());
            if (k == "dictionary") cfg_.dictionary = v;
            else if (k == "auto_skip_punctuation") cfg_.autoSkipPunct = i != 0;
            else if (k == "drop_lock_keys") cfg_.dropLockKeys = std::clamp(i, 0, 2);
            else if (k == "case_from_difficulty") cfg_.caseFromDifficulty = i;
            else if (k == "combo_bonus") cfg_.comboBonus = i;
            else if (k == "combo_cap") cfg_.comboCap = i;
            else if (k == "tough_list") perDifficulty(v, cfg_.toughList, str);
            else if (k == "item_list") perDifficulty(v, cfg_.itemList, str);
            else if (k == "small_list") cfg_.smallList = v;
            else if (k == "small_letters") letters(v, cfg_.smallMin, cfg_.smallMax);
            else if (k == "projectile_letters") letters(v, cfg_.projMin, cfg_.projMax);
            else if (k == "word_lists") cfg_.wordLists = splitList(v);
            else if (k == "boss_lists") cfg_.bossLists = splitList(v);
            else if (k == "word_letters_near" || k == "word_letters_mid" || k == "word_letters_far") {
                const int b = k == "word_letters_near" ? 0 : k == "word_letters_mid" ? 1 : 2;
                int mn[5], mx[5];
                for (int q = 0; q < 5; ++q) mn[q] = cfg_.wordMin[q][b], mx[q] = cfg_.wordMax[q][b];
                letters(v, mn, mx);
                for (int q = 0; q < 5; ++q) cfg_.wordMin[q][b] = mn[q], cfg_.wordMax[q][b] = mx[q];
            }
            else if (k == "tough_letters") letters(v, cfg_.toughMin, cfg_.toughMax);
            else if (k == "boss_letters") letters(v, cfg_.bossMin, cfg_.bossMax);
            else if (k == "near_seconds") cfg_.nearSeconds = flt(v);
            else if (k == "far_seconds") cfg_.farSeconds = flt(v);
            else if (k == "near_depth") cfg_.nearDepth = flt(v);
            else if (k == "far_depth") cfg_.farDepth = flt(v);
            else if (k == "projectile_speed") perDifficulty(v, cfg_.projectileSpeed, flt);
            else if (k == "tough_hp") cfg_.toughHp = i;
            else if (k == "two_prompts_hp") cfg_.twoPromptsHp = i;
            else if (k == "longer_from_chapter") cfg_.longerFromChapter = i;
            else if (k == "attack_wait") perDifficulty(v, cfg_.attackWait, flt);
            else if (k == "attack_per_letter") perDifficulty(v, cfg_.attackPerLetter, flt);
            else if (k == "attack_gap") perDifficulty(v, cfg_.attackGap, flt);
            else if (k == "rescue_wait") perDifficulty(v, cfg_.rescueWait, flt);
            else if (k == "boss_shots") cfg_.bossShots = std::max(1, i);
            else if (k == "boss_shot_gap") cfg_.bossShotGap = std::max(1, i);
            else if (k == "boss_damage_x") cfg_.bossDamageX = std::max(1, i);
            else if (k == "boss_window_seconds") perDifficulty(v, cfg_.bossWindowSeconds, flt);
            else if (k == "boss_window_per_letter") perDifficulty(v, cfg_.bossWindowPerLetter, flt);
            else if (k == "item_names") cfg_.itemNames = splitList(v);
            else if (k == "route_names") cfg_.routeNames = splitList(v);
            else if (k == "small_names") cfg_.smallNames = splitList(v);
            else if ((k.rfind("grade_", 0) == 0 && k.size() == 7) || (k.rfind("kill_grade_", 0) == 0 && k.size() == 12)) {
                float a = 0, w = 0;
                if (std::sscanf(v.c_str(), "%f %f", &a, &w) == 2) {
                    const char g = char(std::toupper(k.back()));
                    auto& gs = k[0] == 'k' ? cfg_.killGrades : cfg_.grades;
                    auto it = std::find_if(gs.begin(), gs.end(), [&](const auto& x) { return x.g == g; });
                    if (it != gs.end()) it->acc = a, it->wpm = w;
                    else gs.push_back({g, a, w});
                }
            }
        }
    }
    if (!dictionary.empty())
        cfg_.dictionary = dictionary;
    // the lists the tuning names: each from the last folder that has it (a mod's replaces the game's)
    std::set<std::string> names = {cfg_.smallList};
    names.insert(cfg_.wordLists.begin(), cfg_.wordLists.end());
    names.insert(cfg_.bossLists.begin(), cfg_.bossLists.end());
    for (int d = 0; d < 5; ++d)
        names.insert({cfg_.toughList[d], cfg_.itemList[d]});
    lists_.clear();
    std::string looked;
    for (const std::string& name : names)
        for (auto d = dataDirs.rbegin(); d != dataDirs.rend(); ++d) {
            const std::string p = *d + "/data/typing/" + cfg_.dictionary + "/" + name + ".txt";
            if (!std::filesystem::exists(p, ec))
                continue;
            std::vector<std::string>& v = lists_[name];
            std::ifstream f(p);
            for (std::string line; std::getline(f, line);) {
                line = trim(line);
                if (line.empty() || line[0] == '#') continue;
                v.push_back(line);
            }
            break;
        }
    for (const std::string& d : dataDirs)
        looked += (looked.empty() ? "" : ", ") + d + "/data/typing/" + cfg_.dictionary;
    if (promptCount() == 0) {
        err = "no prompts in data/typing/" + cfg_.dictionary + " (looked in " + looked + ")";
        return false;
    }
    return true;
}

void Typing::start(int difficulty, uint64_t seed)
{
    difficulty_ = std::clamp(difficulty, 0, 4);
    bossDamageUntil_ = 0;
    rng_.seed(seed);
    classify_ = std::make_unique<Bot>(seed, 1);
    targets_.clear();
    queue_.clear();
    shots_.clear();
    labels.clear();
    lock_ = 0;
    combo = 0;
    game = chapter = {};
    chapters.clear();
    resultsShown = false;
    lastChapter_ = -1;
    clearWas_ = false;
    nowMs_ = 0;
    lastAttack_ = 0;
    bossPunish_ = bossFight_ = false;
    bossLanded_ = true;
    lastBossFinish_ = 0;
    attacks = hits = smallTyped = smallKilled = smallMissed = letterKills = 0;
    attackGapMin = UINT32_MAX;
    given.clear();
    popups.clear();
    follow_.clear();
    killed_.clear();
    resume_ = 0;
    freeSinceMs_ = 0;
    appearedAt_ = INT_MIN;
}

bool Typing::cancel()
{
    if (!lock_) return false;
    if (auto it = targets_.find(lock_); it != targets_.end())   // the prompt from its start again
        it->second.typed = 0, it->second.startMs = -1;
    lock_ = 0;
    return true;
}

bool Typing::inPlay(Runtime& rt)
{
    auto& mem = rt.mem;
    if (mem.at<int16_t>(G::game_mode) != 4 || mem.at<int16_t>(G::game_submode) != 2)
        return false;
    if (mem.at<uint8_t>(G::scene_state) != 2 || rt.letterbox != 0 || mem.u32(G::shot_enable) == 0 || rt.clear.active)
        return false;
    if (mem.u32(G::evt_clrgfst_flags) & 0x20)   // the chapter card
        return false;
    return mem.at<Player>(G::players, 0).state == 5;
}

bool Typing::skippable(char c) const
{
    return cfg_.autoSkipPunct && c != ' ' && !std::isalnum(static_cast<unsigned char>(c));
}
void Typing::skipPunct(Target& t) const
{
    while (t.typed < t.text.size() && skippable(t.text[t.typed]))
        ++t.typed;
}

bool Typing::same(char want, uint32_t key) const
{
    if (key >= 128 || key == 0)
        return false;
    return caseSensitive() ? char(key) == want : low(key) == low(uint8_t(want));
}

bool Typing::isSmall(Runtime& rt, uint32_t obj) const
{
    const std::string n = nameOf(rt, rt.mem.u32(obj));
    for (const std::string& p : cfg_.smallNames)
        if (!p.empty() && n.find(p) != std::string::npos)
            return true;
    return false;
}

// a prompt whose first letter no other target has now (TotD: the first key picks one enemy; totd1: all the targets
// held, seen or not, so one coming back from behind something keeps its letter apart too) and that no other shows
bool Typing::pick(const std::vector<std::string>& lists, int minLen, int maxLen, uint32_t self, std::string& out)
{
    // totd2: the lists pooled (each prompt once), then by length
    std::vector<std::string> pool;
    std::set<std::string> have;
    for (const std::string& list : lists)
        if (auto it = lists_.find(list); it != lists_.end())
            for (const std::string& s : it->second)
                if (have.insert(s).second) pool.push_back(s);
    if (pool.empty())   // (lists missing from the dictionary: the longest one it has)
        for (const auto& [k, l] : lists_)
            if (l.size() > pool.size()) pool = l;
    if (pool.empty()) {
        out = "zombie";
        return false;
    }
    const std::vector<std::string>* v = &pool;
    std::vector<size_t> cand;
    for (int widen = 0; cand.empty() && widen < 20; ++widen)   // (none that long: the nearest lengths)
        for (size_t i = 0; i < v->size(); ++i) {
            const int n = int((*v)[i].size());
            if ((minLen <= 0 || n >= minLen - widen) && (maxLen <= 0 || n <= maxLen + widen)) cand.push_back(i);
        }
    if (cand.empty())
        for (size_t i = 0; i < v->size(); ++i) cand.push_back(i);
    bool firstUsed[128] = {};
    std::set<std::string> shown;
    for (const auto& [o, t] : targets_)
        if (o != self && !t.text.empty() && t.finishing == 0) {
            firstUsed[uint8_t(low(uint8_t(t.text[0]))) & 127] = true;
            shown.insert(t.text);
        }
    // (totd2: a projectile's / small creature's first letter breaks into a locked word: never one of the letters still
    // to type in that word, or the key would go on with the word instead)
    if (auto me = targets_.find(self); me != targets_.end() && me->second.urgent && lock_ && lock_ != self)
        if (auto lk = targets_.find(lock_); lk != targets_.end())
            for (size_t i = lk->second.typed; i < lk->second.text.size(); ++i)
                firstUsed[uint8_t(low(uint8_t(lk->second.text[i]))) & 127] = true;
    const auto ok = [&](const std::string& s) { return !s.empty() && !firstUsed[uint8_t(low(uint8_t(s[0]))) & 127] && !shown.count(s); };
    // a few at random, then the whole list from a random place (a short list nearly used up)
    for (int tries = 0; tries < 12; ++tries) {
        const std::string& s = (*v)[cand[size_t(rng_() % cand.size())]];
        if (ok(s)) { out = s; return true; }
    }
    const size_t at = size_t(rng_() % cand.size());
    for (size_t i = 0; i < cand.size(); ++i) {
        const std::string& s = (*v)[cand[(at + i) % cand.size()]];
        if (ok(s)) { out = s; return true; }
    }
    out = (*v)[cand[at]];
    return false;
}

// totd2 (user 2026-11-03: "each enemy's word length should depend on how far away it spawns and how soon it would hit
// you"): 0 near (spawned within near_depth, or at you within near_seconds at the speed it comes closer: one that jumps
// out next to you), 2 far (beyond far_depth and later than far_seconds: a slow walker far off), else 1. The depth is
// the game's own camera-space z (+0x40) at the first sight; the speed the CPU teammates' tracking of it (Bot); one
// that took an attack place (+0x2E7) is at you soon: never far
int Typing::bandOf(Runtime& rt, Target& t)
{
    const float z = std::max(0.0f, rt.mem.f32(t.obj + 0x40));
    const float speed = classify_->approach(t.obj);   // units a frame toward the camera (0: not coming / not known)
    const float eta = speed > 0.02f ? std::max(0.0f, z - 20.0f) / speed / 60.0f : 1e9f;
    const bool engaged = rt.mem.at<int8_t>(t.obj + 0x2E7) >= 0;
    t.z0 = z;
    if (z < cfg_.nearDepth || eta < cfg_.nearSeconds)
        return 0;
    if (z > cfg_.farDepth && eta > cfg_.farSeconds && !engaged)
        return 2;
    return 1;
}

std::string Typing::newPrompt(Runtime& rt, Target& t)
{
    const int d = difficulty_;
    const int dc = std::clamp(d + (rt.mem.at<int8_t>(G::chapter) + 1 >= cfg_.longerFromChapter ? 1 : 0), 0, 4);
    int minLen = 0, maxLen = 0;
    std::vector<std::string> lists;
    const std::string n = nameOf(rt, rt.mem.u32(t.obj));
    if (t.kind == 2) {
        lists = cfg_.bossLists, minLen = cfg_.bossMin[d], maxLen = cfg_.bossMax[d];
        t.cls = "boss";
    } else if (t.kind == 3) {
        lists = cfg_.wordLists, minLen = cfg_.wordMin[dc][1], maxLen = cfg_.wordMax[dc][1];   // (totd2: the pool: no repeats)
        lists.push_back(cfg_.itemList[dc]);
        t.cls = "item";
    } else if (t.kind == 1 || t.small) {
        lists = {cfg_.smallList}, minLen = t.kind == 1 ? cfg_.projMin[d] : cfg_.smallMin[d], maxLen = t.kind == 1 ? cfg_.projMax[d] : cfg_.smallMax[d];
        t.cls = t.kind == 1 ? "projectile" : n.find("bat_") != std::string::npos ? "bat" : "small";
    } else if (rt.mem.at<int16_t>(t.obj + 0x72) >= cfg_.toughHp && t.band > 0) {
        // the tough ones: a short phrase (a near one: the far band's longest words instead)
        lists = {cfg_.toughList[dc]}, minLen = cfg_.toughMin[dc], maxLen = cfg_.toughMax[dc];
        t.cls = "tough";
    } else if (isCaptor(rt, t.obj) || peopleNow_) {
        // totd2: a creature after a researcher, or any in a rescue scene (a researcher on the screen: the scene's own
        // timer runs): the shortest words (the rescue is a race)
        lists = cfg_.wordLists, minLen = cfg_.wordMin[d][0], maxLen = cfg_.wordMin[d][0];
        t.cls = isCaptor(rt, t.obj) ? "captor" : "rescue scene";
    } else {
        const int b = rt.mem.at<int16_t>(t.obj + 0x72) >= cfg_.toughHp ? 2 : t.band;
        lists = cfg_.wordLists, minLen = cfg_.wordMin[dc][b], maxLen = cfg_.wordMax[dc][b];
        t.cls = b == 0 ? "zombie near" : b == 1 ? "zombie mid" : "zombie far";
    }
    std::string s;
    pick(lists, minLen, maxLen, t.obj, s);
    t.pickLists = lists, t.pickMin = minLen, t.pickMax = maxLen;
    t.list = lists.size() == 1 ? lists[0] : t.cls;
    auto& g = given[t.list];
    ++g.first, g.second += uint32_t(s.size());
    for (TypingStats* st : {&game, &chapter}) {
        auto& l = st->lengths[t.cls];
        ++l.first, l.second += uint32_t(s.size());
    }
    return s;
}

void Typing::chapterEnd(Runtime& rt)
{
    chapter.chapterNo = lastChapter_ + 1;
    // the people rescued (the clear's RESCUED) and those that came to be rescued this chapter (the game's own counts)
    chapter.rescued = rt.mem.at<int16_t>(G::hostages_rescued_chapter);
    if (appearedAt_ != INT_MIN)
        chapter.rescueSeen = rt.mem.at<int16_t>(G::hostages_appeared) - appearedAt_;
    appearedAt_ = INT_MIN;
    chapter.bossBeaten = chapter.bossSeen;   // (a chapter ends after its boss)
    game.rescued += chapter.rescued, game.rescueSeen += chapter.rescueSeen;
    chapters.push_back(chapter);
    chapter = {};
    resultsShown = true;
}

std::string Typing::summary(const TypingStats& s) const
{
    char b[256];
    std::snprintf(b, sizeof b, "letters %u misses %u accuracy %.1f%% words %u boss %u windows_missed %u wpm %.1f max_combo %u fastest %.2fs \"%s\" grade %c",
                  s.letters, s.misses, s.accuracy(), s.words, s.bossPrompts, s.windowsMissed, s.wpm(), s.maxCombo,
                  s.fastestMs / 1000.0, s.fastestText.c_str(), grade(s));
    return b;
}

char Typing::grade(const TypingStats& s) const
{
    for (const auto& g : cfg_.grades)
        if (s.accuracy() >= g.acc && s.wpm() >= g.wpm)
            return g.g;
    return 'E';
}

char Typing::killGrade(float accuracy, float wpm) const
{
    for (const auto& g : cfg_.killGrades)
        if (accuracy + 1e-3f >= g.acc && wpm >= g.wpm)
            return g.g;
    return 'F';
}

void Typing::finished(Runtime& rt, Target& t)
{
    const double ms = t.startMs >= 0 ? nowMs_ - t.startMs : 0;
    for (TypingStats* s : {&game, &chapter}) {
        ++s->words;
        if (t.kind == 2) ++s->bossPrompts;
        if (ms > 0 && (s->fastestMs == 0 || ms < s->fastestMs) && t.text.size() >= 3)
            s->fastestMs = ms, s->fastestText = t.text;
    }
    // Overkill's combo: words in a row without a miss; the bonus points (0: none)
    if (!t.missedThis) {
        ++combo;
        game.maxCombo = std::max(game.maxCombo, combo);
        chapter.maxCombo = std::max(chapter.maxCombo, combo);
        if (cfg_.comboBonus > 0) {
            Player& pl = rt.mem.at<Player>(G::players, 0);
            pl.score += cfg_.comboBonus * int32_t(std::min<uint32_t>(combo, uint32_t(std::max(cfg_.comboCap, 1))));
        }
    }
    // totd2 (user 2026-11-03: "each enemy killed shows a grade (F..S) from how fast and how accurately its word was
    // typed"): the kill's letters (all its prompts) and misses, over the time from when it could be typed (its prompt
    // shown, or the last prompt done if later: the reaction counts, waiting on another word does not); a boss: each
    // sentence (one attack)
    char g = 0;
    if (t.kind == 2 || t.left == 0) {
        const double from = std::max(t.bornMs, freeSinceMs_);
        const double gms = std::max(50.0, nowMs_ - from);
        const float acc = t.lettersAll + t.missesAll ? 100.0f * float(t.lettersAll) / float(t.lettersAll + t.missesAll) : 100.0f;
        const float wpm = float(t.lettersAll) / 5.0f / float(gms / 60000.0);
        g = killGrade(acc, wpm);
        for (TypingStats* s : {&game, &chapter})
            ++s->killGrade[gradeIndex(g)];
        Popup p;
        p.x = t.x, p.y = t.y, p.grade = g, p.frame = frame_, p.boss = t.kind == 2;
        popups.push_back(p);
        t.lettersAll = t.missesAll = 0;
        t.bornMs = nowMs_;
    }
    freeSinceMs_ = nowMs_;
    if (verbose)
        std::printf("frame %llu: TOTD typed \"%s\" (%s, %s, %.2f s%s), combo %u%s%c\n", (unsigned long long)frame_, t.text.c_str(),
                    t.kind == 0 ? (t.small ? "small" : "enemy") : t.kind == 1 ? "projectile" : t.kind == 2 ? "boss" : "item",
                    t.list.c_str(), ms / 1000.0, t.missedThis ? ", with misses" : "", combo, g ? " grade " : "", g ? g : ' ');
    lock_ = 0;
    // an interrupt done (a projectile, a bat typed in the middle of a word): back to that word
    if (resume_ && resume_ != t.obj) {
        if (auto it = targets_.find(resume_); it != targets_.end() && it->second.typed > 0 && it->second.finishing == 0)
            lock_ = resume_;
    }
    resume_ = 0;
    t.missedThis = false;
    t.startMs = -1;
    if (t.kind == 2) {   // a boss: the attack on the weak point, then the next sentence (a new window)
        t.bossShotsLeft += cfg_.bossShots;
        // totd1: an attack whose shots did no damage (the boss turning away, in its own attack or hit reaction: the
        // Chariot took 25 sentences instead of 7) goes on, a shot every boss_shot_gap, until one does (3 s at most)
        bossLanded_ = false;
        bossPunish_ = false;   // (typed in time: the boss's next hit is held again)
        lastBossFinish_ = frame_;
        t.bossPendingUntil = frame_ + 180;
        t.text = newPrompt(rt, t);
        t.typed = 0;
        t.born = frame_;
        skipPunct(t);
        return;
    }
    if (t.left > 0) {   // a tough enemy (Overkill): this prompt is one attack, the next prompt follows
        --t.left;
        shots_.push_back({t.obj, t.serial, false});
        t.text = newPrompt(rt, t);
        t.typed = 0;
        skipPunct(t);
        return;
    }
    if (t.kind == 1 || t.small) {
        ++smallTyped;
        // (the report: typed; followed for 1.5 s: did the game take it as shot)
        const int what = t.cls == "bat" ? 0 : t.cls == "projectile" ? 1 : 2;
        for (TypingStats* s : {&game, &chapter})
            ++(what == 0 ? s->bats : what == 1 ? s->projectiles : s->small)[1];
        follow_.push_back({t.obj, t.serial, rt.mem.u32(t.obj), rt.mem.at<int16_t>(t.obj + 0x5C), frame_ + 90, hits, what});
    }
    if (!t.holding)   // (the hit points to put back if the killing shot never lands)
        t.realHp = rt.mem.at<int16_t>(t.obj + 0x70);
    t.finishShots = 0;
    // (an item: a shot or three picks it up, it has no hit points; a prop / a lock: a shot every 6 frames until it
    // breaks: some take a few, totd2)
    t.finishing = t.kind == 3 ? 12 : kFinishTries;
}

// player 1's shot at the target this tick; false: not now (off the screen, a hostage in the way, gone)
bool Typing::fire(Runtime& rt, const Shot& s)
{
    auto& mem = rt.mem;
    if (mem.u32(s.obj) == 0 || int32_t(mem.u32(s.obj + 8)) != s.serial)
        return false;
    float x, y;
    const auto it = targets_.find(s.obj);
    // where: the centre (the bots' aim); a killing shot that did not land yet: round the body parts that take damage
    // (an enemy whose centre lies between its hit spheres)
    const int tries = it != targets_.end() && it->second.finishing > 0 ? it->second.finishShots : 0;
    if (!(tries > 0 && it->second.kind == 0 && partAim(rt, s.obj, uint32_t(tries - 1), x, y)) &&
        !aimPoint(rt, s.obj, uint32_t(frame_ / 7), x, y))
        return false;
    // totd2 (user 2026-11-03: "the grabbed researchers can't be saved"): a researcher in front of the aim point (a
    // zombie holding or biting one) stopped every shot, so the killing shot never fired and the prompt was given up
    // (the self-test: "gave up ... 0 killing shots"); now the body parts that take damage are tried for one he does
    // not cover (a person aims at the head over the shoulder), and only when none is clear no shot is fired
    if (classify_->hostageCovers(rt, s.obj, x, y)) {
        bool clear = false;
        for (uint32_t pick = 0; pick < 8 && !clear; ++pick)
            clear = partAim(rt, s.obj, pick, x, y) && !classify_->hostageCovers(rt, s.obj, x, y);
        if (!clear)
            return false;
    }
    const int kind = it != targets_.end() ? it->second.kind : 0;
    const int16_t hp = mem.at<int16_t>(s.obj + 0x70), hpMax = mem.at<int16_t>(s.obj + 0x72);
    if (kind == 0 && hpMax > 0 && it != targets_.end()) {
        Target& t = it->second;
        if (s.hold && hp > 0) {
            // a letter: it hits, it cannot kill. The damage of a shot lands a frame or two after the trigger, so the
            // hit points are held up from the first letter until the prompt is done (hold(), each tick), the real
            // ones kept to put back if the prompt is let go
            if (!t.holding)
                t.realHp = hp, t.holding = true;
            mem.at<int16_t>(s.obj + 0x70) = kHeldHp;
        } else if (t.finishing > 0) {
            t.holding = false;
            mem.at<int16_t>(s.obj + 0x70) = 1;   // the last letter: this shot kills (any part that takes damage; kind 70
                                                  // fighting on below 0 too: its own death check sees the hit)
        } else if (t.holding) {   // a tough enemy's prompt done (one attack): the real hit points take this shot
            t.holding = false;
            mem.at<int16_t>(s.obj + 0x70) = t.realHp;
        }
    }
    Runtime::GunInput& g = rt.gun[0];
    if (it != targets_.end() && it->second.finishing > 0 && !s.hold)
        ++it->second.finishShots;
    g.onScreen = g.trigger = true;
    g.x = x;
    g.y = y;
    if (trace_)
        std::printf("frame %llu: TOTD shot at %X (%s) %.0f %.0f hp %d/%d\n", (unsigned long long)frame_, s.obj,
                    s.hold ? "letter" : "real", x, y, hp, hpMax);
    return true;
}

// totd1 (user 2026-11-03: "in TotD a zombie walks up, then gives you a clear window before it hits; its attack waits on a
// timer you can see"): a creature at the player may attack once its timer ran out (Target::readyAt: attack_wait +
// attack_per_letter x its prompt's letters after it arrived) and attack_gap after the last attack on player 1. Asked
// from inside the tick (port::gAttackHold): the standard enemies' attack choice, pick_target_player (bats, the agile
// ones, frogs, scene zombies). Bosses, projectiles and people: their own rules (false)
bool Typing::holds(uint32_t obj)
{
    if (!rt_)
        return false;
    if (classify_->targetKind(*rt_, obj) != 0)
        return false;
    if (lastAttack_ && frame_ < lastAttack_ + ticks(cfg_.attackGap[difficulty_]))
        return true;
    const auto it = targets_.find(obj);
    if (it == targets_.end())   // no prompt (off the screen, not in the fight yet): nothing to type, it waits
        return true;
    const Target& t = it->second;
    return t.finishing > 0 || t.readyAt == 0 || frame_ < t.readyAt;
}

// totd2 (user 2026-11-03: "the locked-up / grabbed researchers can't be saved ... using the game's own rescue logic"):
// a creature chasing a researcher (the run-after routines' arrival, port::gPreyHold) bites only once its prompt's
// rescue timer ran out (rescue_wait + attack_per_letter x its letters from the prompt shown); typed in time, the game's
// own rescue follows its death (the thanks, the item, the bonus), else its own bite (the researcher dies). One without
// a prompt yet (off the screen, its prompt coming) waits rescue_wait at most
bool Typing::holdsPrey(uint32_t obj)
{
    if (!rt_)
        return false;
    const auto it = targets_.find(obj);
    bool held;
    if (it == targets_.end() || it->second.text.empty()) {
        uint64_t& f = preyAsked_[obj];
        if (!f) f = frame_;
        held = frame_ < f + ticks(cfg_.rescueWait[difficulty_]);
    } else
        held = it->second.finishing > 0 || frame_ < it->second.preyReadyAt;
    // (held at the researcher: it does not take this tick's step either, after(); walking on into him it pushed him
    // aside, and Chapter 1's first researcher, freed late, then ran round his goal for ever: the event never went on)
    if (held && !preyHeldAt_.count(obj))
        preyHeldAt_[obj] = {rt_->mem.f32(obj + 0x20), rt_->mem.f32(obj + 0x28)};
    return held;
}

// the hit hold (port::gHitHold, player_hit): in a boss fight a hit on player 1 lands only after a missed sentence
// window (totd1); elsewhere (totd2, the self-test's 40 WPM typist took 16-18 hits a chapter from attacks that never ask
// gAttackHold: the agile kind 70's, the vomit, the taran, a standard enemy's attack already begun) only while a creature
// whose attack timer ran out is there. Thrown things hit through projectile_hit_player, not here: they carry prompts
bool Typing::holdsHit(int player) const
{
    if (!rt_ || player != 0)
        return false;
    if (bossFight_)
        return !bossPunish_;
    for (const auto& [o, t] : targets_)
        if (t.kind == 0 && t.readyAt && t.readyAt <= frame_)
            return false;
    return true;
}

bool Typing::isCaptor(Runtime& rt, uint32_t obj) const
{
    const std::string n = nameOf(rt, rt.mem.u32(obj));
    for (const char* p : {"Researcher", "researcher", "BiteAgent", "act7_", "act12_", "act16_"})
        if (n.find(p) != std::string::npos)
            return true;
    return false;
}

void Typing::before(Runtime& rt, double tickMs)
{
    auto& mem = rt.mem;
    frame_ = rt.frameCount;
    rt_ = nullptr;
    port::gAttackHold = nullptr;
    port::gHitHold = nullptr;
    port::gPreyHold = nullptr;
    // the chapter's end: its results (rt.clear: the clear's results are up)
    if (rt.clear.active && !clearWas_)
        chapterEnd(rt);
    clearWas_ = rt.clear.active;
    if (!rt.clear.active && resultsShown && inPlay(rt))
        resultsShown = false;
    Player& pl = mem.at<Player>(G::players, 0);
    if (pl.state == 5)   // no reloads in this mode (typing never empties the gun)
        pl.rounds = 6, pl.empty = 0;
    if (static bool was = false; trace_ && inPlay(rt) != was)   // (developer: the typing's play phase begins / ends)
        was = !was, std::printf("frame %llu: TOTD in play %d (scene_state %d, shots %u, letterbox %d)\n", (unsigned long long)frame_,
                                int(was), int(mem.at<uint8_t>(G::scene_state)), mem.u32(G::shot_enable), int(rt.letterbox));
    if (!inPlay(rt)) {
        for (auto& [o, t] : targets_)
            release(rt, t);
        queue_.clear();
        shots_.clear();
        labels.clear();
        targets_.clear();
        bossHp_.clear();
        lock_ = 0;
        return;
    }
    lastChapter_ = mem.at<int8_t>(G::chapter);
    if (trace_ && frame_ % 120 == 0) {   // (developer: the enemies the event may wait on)
        std::string s;
        for (uint32_t obj : rt.shootable)
            s += " " + nameOf(rt, mem.u32(obj));
        std::printf("frame %llu: TOTD enemies alive %d, shootable:%s\n", (unsigned long long)frame_, int(mem.at<uint16_t>(G::enemies_alive)), s.c_str());
    }
    if (frame_ % 600 == 0)
        preyAsked_.clear();
    if (appearedAt_ == INT_MIN)   // (the chapter's people to rescue: counted from its first moment of play)
        appearedAt_ = mem.at<int16_t>(G::hostages_appeared);
    std::erase_if(popups, [&](const Popup& p) { return frame_ > p.frame + 75; });
    for (auto it = killed_.begin(); it != killed_.end();)
        it = frame_ > it->second.second ? killed_.erase(it) : std::next(it);
    rt.gun[0] = {};   // (in play the gun is the typing's alone; elsewhere, cut-scene skips, the continue, the name entry: the player's)
    nowMs_ += tickMs;
    if (lock_) {
        game.typingMs += tickMs;
        chapter.typingMs += tickMs;
    }
    const int d = difficulty_;

    // the prompts on the screen: every shootable object the CPU teammates would take as a target, the items
    for (auto& [o, t] : targets_) t.seen = false;
    std::vector<std::pair<float, float>> people;   // (the researchers on the screen: a rescue scene; where)
    bool creatures = false;                         // (and creatures in the fight)
    for (uint32_t obj : rt.shootable) {
        const int k = classify_->targetKind(rt, obj);
        if (float px, py; k == 3 && aimPoint(rt, obj, 0, px, py))
            people.push_back({px, py});
        creatures = creatures || k == 0;
    }
    peopleNow_ = !people.empty();
    for (uint32_t obj : rt.shootable) {
        const int k = classify_->targetKind(rt, obj);
        int kind = -1;
        float x = 0, y = 0, th = 0;
        bool dmg = false;
        // (totd2: the Magician's barrage, dozens of homing shots at once (0x74150, 500-700 prompts a fight): none; in a
        // boss fight a hit only lands after a missed sentence window anyway, see holdsHit)
        if (k == 1 && mem.u32(obj) == 0x74150)
            continue;
        if (k == 0 || k == 1 || k == 2) {
            kind = k;
            th = classify_->targetThreat(rt, frame_, obj, dmg, x, y);
        } else if (k == 6) {   // scenery: the items (a life, the bonus coins), the locks and route spots, some props
            const std::string n = nameOf(rt, mem.u32(obj));
            if (trace_) {
                static std::map<uint32_t, bool> told;
                if (!told[mem.u32(obj)])
                    told[mem.u32(obj)] = true, std::printf("frame %llu: TOTD scenery %s\n", (unsigned long long)frame_, rt.routineName(mem.u32(obj)).c_str());
            }
            bool item = false;   // typing.ini item_names: the routines' name parts that are pickups (the gold frog ...)
            for (const std::string& p : cfg_.itemNames)
                item = item || (!p.empty() && n.find(p) != std::string::npos);
            // totd2 (user 2026-11-03: "the locked-up researchers can't be saved"): the padlocks, lock tasks, route
            // spots and signs (route_names: a researcher behind a locked door, the way to one) and, in a rescue scene
            // (a researcher on the screen), the props (Chapter 2's curled-up researcher is reached by breaking the
            // crate by him); and props with an item inside (flags bits 5-7: a life, a coin, the gold frog)
            for (const std::string& p : cfg_.routeNames)
                item = item || (!p.empty() && n.find(p) != std::string::npos);
            const bool prop = n.rfind("prop", 0) == 0;
            if (item || prop) {
                if (!aimPoint(rt, obj, 0, x, y))
                    continue;
                for (const auto& [px, py] : people)   // (a prop by a researcher with no creature about: within a box of
                    item = item || (prop && !creatures && std::abs(px - x) < 90.0f && std::abs(py - y) < 110.0f);   // the screen round him)
                item = item || (prop && (mem.at<uint8_t>(obj + 0x58) & 0xE0) != 0);
            }
            if (item && n.find("dropped_weapon") == std::string::npos) {
                kind = 3;
                th = 100.0f;
                dmg = true;
            }
        }
        if (trace_ && k == 3)   // (developer: the people's routines as they change: the rescues)
            people_[obj];
        if (static const uint32_t watch = std::getenv("HOTD_TOTD_OBJ") ? uint32_t(std::strtoul(std::getenv("HOTD_TOTD_OBJ"), nullptr, 16)) : 0;
            watch == obj && frame_ % 30 == 0)   // (developer: one object's class and threat)
            std::printf("frame %llu: TOTD watch %X %s class %d threat %.0f damageable %d z %.1f hp %d engaged %d\n",
                        (unsigned long long)frame_, obj, rt.routineName(mem.u32(obj)).c_str(), k, th, int(dmg),
                        mem.f32(obj + 0x40), mem.at<int16_t>(obj + 0x70), int(mem.at<int8_t>(obj + 0x2E7)));
        if (kind < 0 || th <= 0 || !dmg)
            continue;
        const int32_t serial = int32_t(mem.u32(obj + 8));
        auto it = targets_.find(obj);
        if (it != targets_.end() && it->second.serial != serial) {   // another task in the same place
            if (lock_ == obj) lock_ = 0;
            targets_.erase(it);
            it = targets_.end();
        }
        if (it == targets_.end()) {
            // totd2: one already typed to its end (a frog falling, a bat dropping, a barrel knocked away: still
            // shootable a moment) gets no new prompt (totd1 gave a dying frog another word)
            if (auto k2 = killed_.find(obj); k2 != killed_.end() && k2->second.first == serial && frame_ < k2->second.second)
                continue;
            Target t;
            t.obj = obj, t.serial = serial, t.routine = mem.u32(obj), t.kind = kind, t.born = frame_;
            t.small = kind == 0 && isSmall(rt, obj);
            t.urgent = kind == 1 || t.small;
            t.z0 = mem.f32(obj + 0x40);
            it = targets_.emplace(obj, t).first;
        }
        Target& t = it->second;
        t.seen = true;
        t.routine = mem.u32(obj);
        t.x = x, t.y = y, t.threat = th;
        // its prompt: at once for the near ones, bosses, items, projectiles, small ones; the others after a few frames
        // of the tracking (how fast it comes: the threat band, totd2)
        if (t.text.empty() && t.typed == 0 &&
            (kind != 0 || t.small || mem.f32(obj + 0x40) < cfg_.nearDepth || frame_ >= t.born + kMeasure)) {
            const int hpMax = mem.at<int16_t>(obj + 0x72);
            t.band = kind == 0 && !t.small ? bandOf(rt, t) : 1;
            t.left = kind == 0 && !t.small && hpMax >= cfg_.twoPromptsHp && t.band > 0 ? 1 : 0;
            t.text = newPrompt(rt, t);
            t.bornMs = nowMs_;
            t.preyReadyAt = frame_ + std::max<uint64_t>(1, ticks(cfg_.rescueWait[d] + cfg_.attackPerLetter[d] * float(t.text.size() * (t.left + 1))));
            skipPunct(t);
            if (kind == 1)   // a throw: an attack (the next one waits attack_gap)
                lastAttack_ = frame_;
            const int what = t.cls == "bat" ? 0 : t.cls == "projectile" ? 1 : t.cls == "small" ? 2 : -1;
            if (what >= 0)
                for (TypingStats* s : {&game, &chapter})
                    ++(what == 0 ? s->bats : what == 1 ? s->projectiles : s->small)[0];
            if (kind == 2)
                game.bossSeen = chapter.bossSeen = true;
            if (trace_)
                std::printf("frame %llu: TOTD new %X %s kind %d%s hp %d/%d z %.0f speed %.2f band %d \"%s\"\n", (unsigned long long)frame_, obj,
                            rt.routineName(t.routine).c_str(), kind, t.small ? " small" : "", mem.at<int16_t>(obj + 0x70), hpMax,
                            mem.f32(obj + 0x40), classify_->approach(obj), t.band, t.text.c_str());
        }
        // totd2 (user 2026-11-03: "no projectiles: they still don't work in real play"): a thrown axe / barrel was at
        // the player 0.6 s after the throw, too soon to read and type it. While it carries a prompt it flies at
        // projectile_speed of its speed (TotD's thrown things drift at you): its velocity (+0x78..) x k, its gravity
        // (+0x84..) x k^2: the same arc, k times as slow. Once, at the first sight
        if (kind == 1 && std::getenv("HOTD_TOTD_PROJ"))   // (developer: a projectile's flight)
            std::printf("frame %llu: TOTD proj %X %s z %.1f pos %.1f %.1f %.1f vel %.2f %.2f %.2f\n", (unsigned long long)frame_, obj,
                        nameOf(rt, mem.u32(obj)).c_str(), mem.f32(obj + 0x40), mem.f32(obj + 0x20), mem.f32(obj + 0x24),
                        mem.f32(obj + 0x28), mem.f32(obj + 0x78), mem.f32(obj + 0x7C), mem.f32(obj + 0x80));
        if (kind == 1 && !t.slowed) {
            t.slowed = true;
            const std::string rn = nameOf(rt, mem.u32(obj));
            const float sp = std::clamp(cfg_.projectileSpeed[difficulty_], 0.1f, 1.0f);
            if (sp < 1.0f && (rn == "flying_axe_frame" || rn == "held_object_flight" || rn == "held_object_throw_flight"))
                for (uint32_t a = 0; a < 3; ++a) {
                    mem.at<float>(obj + 0x78 + 4 * a) *= sp;
                    mem.at<float>(obj + 0x84 + 4 * a) *= sp * sp;
                }
        }
    }
    for (auto it = people_.begin(); it != people_.end();) {   // (developer: HOTD_TOTD_TRACE, the people's routines)
        const uint32_t r = mem.u32(it->first);
        if (r != it->second)
            std::printf("frame %llu: TOTD person %X %s z %.0f state %d\n", (unsigned long long)frame_, it->first,
                        rt.routineName(r).c_str(), mem.f32(it->first + 0x40), int(mem.at<int16_t>(it->first + 0x5C)));
        if (frame_ % 60 == 0 && r)
            std::printf("frame %llu: TOTD person %X at %.1f %.1f z %.1f sub %d vel %.2f %.2f camera %.1f %.1f\n", (unsigned long long)frame_, it->first,
                        mem.f32(it->first + 0x20), mem.f32(it->first + 0x28), mem.f32(it->first + 0x40), int(mem.at<int16_t>(it->first + 0x60)),
                        mem.f32(it->first + 0x78), mem.f32(it->first + 0x80), mem.f32(G::camera_pos), mem.f32(G::camera_pos + 8));
        it->second = r;
        it = r == 0 ? people_.erase(it) : std::next(it);
    }
    // gone: objects no longer there (another serial), out of the list too long, finished and dead
    for (auto it = targets_.begin(); it != targets_.end();) {
        Target& t = it->second;
        const bool same = mem.u32(t.obj) != 0 && int32_t(mem.u32(t.obj + 8)) == t.serial;
        const int16_t hp = mem.at<int16_t>(t.obj + 0x70), hpMax = mem.at<int16_t>(t.obj + 0x72);
        // (down: its hit points used up and no longer a target; kind 70 fights on below 0 and stays a target)
        // (no hit points (frogs, items, projectiles) or small (a bat dies at any hit, its hit points kept): the
        // killing shot fired and no longer a target)
        const bool byHp = hpMax > 0 && !t.small && t.kind != 1;
        // (a small creature hit by its killing shot: dead, though it stays shootable while it falls)
        const bool dead = !same || (t.kind != 2 && (byHp ? hp <= 0 && (t.finishing ? t.finishShots > 0 : !t.seen)
                                                         : (t.finishing && t.finishShots > 0 && !t.seen) ||
                                                           (t.small && t.finishing && t.landedAt)));
        // (a small creature's hit points mean nothing: a Murrer's +0x70 is another word, -12225: its prompt was renewed
        // every frame)
        if (t.seen) t.lastSeen = frame_;
        bool drop = dead;
        // not on the screen as a target: kept a while (it ducked, the camera moves on a little), then let go
        if (!t.seen && !dead && t.bossShotsLeft == 0 && t.finishing == 0 && lock_ != t.obj && t.kind != 2 &&
            frame_ > t.lastSeen + kKeepUnseen)
            drop = true;
        if (t.kind == 1 && !t.seen && t.finishing == 0 && lock_ != t.obj && frame_ > t.lastSeen + 2)
            drop = true;   // a projectile out of reach (landed, hit, knocked away by someone else)
        if (!t.seen && !dead && lock_ == t.obj && frame_ > t.lastSeen + 2 * 60) {
            lock_ = 0;   // the locked enemy gone from the screen for 2 s: the lock goes (its prompt starts again)
            t.typed = 0, t.startMs = -1;
            release(rt, t);
        }
        if (drop) {
            if (dead && t.finishing && (t.kind == 1 || t.small)) {
                ++smallKilled;   // (the self-test: a bat / a thrown axe gone at its last letter)
                killed_[t.obj] = {t.serial, frame_ + 150};   // (still shootable while it falls: no new prompt)
            }
            if (trace_)
                std::printf("frame %llu: TOTD drop %X typed %zu dead %d seen %d\n", (unsigned long long)frame_, t.obj, t.typed, int(dead), int(t.seen));
            if (lock_ == t.obj) lock_ = 0;
            release(rt, t);
            it = targets_.erase(it);
        } else
            ++it;
    }
    // two prompts on the screen starting with the same letter (one came back into view, a list used up): the later
    // one, not begun, gets another (TotD never shows two at once: the first key must pick one)
    for (auto& [o, t] : targets_) {
        if (!t.seen || t.finishing || t.typed > 0 || lock_ == o || t.text.empty()) continue;
        for (auto& [o2, u] : targets_)
            if (o2 != o && u.seen && !u.finishing && !u.text.empty() && (u.born < t.born || (u.born == t.born && o2 < o)) &&
                low(uint8_t(u.text[0])) == low(uint8_t(t.text[0]))) {
                std::string s;
                if (pick(t.pickLists, t.pickMin, t.pickMax, o, s))
                    t.text = s;
                skipPunct(t);
                break;
            }
    }
    // the attack timers: a creature that came at the player (took an attack place, close by, or its prompt up for a
    // while: the bats, the agile ones) may attack after attack_wait + attack_per_letter x its prompt (small ones: half)
    for (auto& [o, t] : targets_) {
        if (t.kind != 0 || t.arrive || t.text.empty()) continue;
        const bool engaged = mem.at<int8_t>(o + 0x2E7) >= 0, close = mem.f32(o + 0x40) < 45.0f;
        if (!(t.seen && (engaged || close || t.small)) && frame_ < t.born + ticks(cfg_.attackWait[d]))
            continue;
        t.arrive = frame_;
        size_t letters = t.text.size() - t.typed;
        if (t.left > 0) letters *= 2;
        const float wait = (t.small ? 0.5f : 1.0f) * cfg_.attackWait[d];
        t.readyAt = frame_ + std::max<uint64_t>(1, ticks(wait + cfg_.attackPerLetter[d] * float(letters)));
    }
    // totd2 (user 2026-11-03: "bosses don't work"): one boss sentence at a time. A boss of several parts (the Chariot's
    // armour and body, two at once) had a sentence each, both drawn in the one boss panel over each other: unreadable.
    // The part shown: the one locked or begun, else the most dangerous; the others wait (their window held)
    bossPrimary_ = 0;
    {
        float best = -1;
        for (auto& [o, t] : targets_) {
            if (t.kind != 2 || t.text.empty() || (!t.seen && o != lock_ && t.typed == 0)) continue;   // (begun: shown off the screen too)
            const float sc = t.threat + (o == lock_ ? 1e7f : 0.0f) + (t.typed > 0 ? 1e6f : 0.0f);
            if (sc > best) best = sc, bossPrimary_ = o;
        }
        for (auto& [o, t] : targets_)
            if (t.kind == 2 && o != bossPrimary_ && t.typed == 0) t.born = frame_;
    }
    // a boss sentence's window: not finished in time, a new one (the attack window missed)
    // (the window's length: a base and a time per letter, in the game's ticks: Sonic Mode's faster ticks shorten it)
    const auto windowOf = [&](const Target& t) {
        return uint64_t(std::max(1.0f, cfg_.bossWindowSeconds[difficulty_] + cfg_.bossWindowPerLetter[difficulty_] * float(t.text.size())) * 60.0f);
    };
    for (auto& [o, t] : targets_)
        if (t.kind == 2 && t.finishing == 0 && frame_ > t.born + windowOf(t)) {
            if (t.typed > 0)   // (a sentence never begun is only renewed: two boss parts up at once)
                ++game.windowsMissed, ++chapter.windowsMissed;
            // the boss's next hit lands (TotD: not typed in time, it strikes); not when another of its sentences was
            // typed meanwhile (two parts up at once, the player on the other)
            if (t.typed > 0 || lastBossFinish_ < t.born)
                bossPunish_ = true;
            if (verbose && t.typed > 0)
                std::printf("frame %llu: TOTD boss window missed (\"%s\", %zu / %zu typed)\n", (unsigned long long)frame_,
                            t.text.c_str(), t.typed, t.text.size());
            if (lock_ == o) lock_ = 0;
            t.text = newPrompt(rt, t);
            t.typed = 0;
            t.startMs = -1;
            t.born = frame_;
            skipPunct(t);
        }

    // the keys of this tick
    const auto miss = [&](Target* t) {
        ++game.misses, ++chapter.misses;
        combo = 0;
        lastMissFrame = uint32_t(frame_);
        if (t) t->missedThis = true, ++t->missesAll;
    };
    for (uint32_t key : queue_) {
        if (key == 0 || key >= 128) { miss(nullptr); continue; }
        Target* t = nullptr;
        if (lock_) {
            auto it = targets_.find(lock_);
            if (it == targets_.end() || it->second.finishing) lock_ = 0;
            else t = &it->second;
        }
        if (!t) {
            // the lock: the prompts on the screen whose next letter is this one; the most dangerous of them
            float best = -1;
            for (auto& [o, x] : targets_) {
                if (!x.seen || x.finishing || x.typed >= x.text.size() || (x.kind == 2 && o != bossPrimary_)) continue;
                if (!same(x.text[x.typed], key)) continue;
                const float th = x.threat + (x.typed > 0 ? 5000.0f : 0.0f);   // (one already begun first)
                if (th > best) best = th, t = &x;
            }
            if (!t) {
                if (trace_)
                    std::printf("frame %llu: TOTD miss %c (nothing to lock onto, %zu shown)\n", (unsigned long long)frame_, char(key), labels.size());
                miss(nullptr);
                continue;
            }
            lock_ = t->obj;
        } else if (!same(t->text[t->typed], key)) {
            // totd2 (user 2026-11-03: "no bats and no projectiles: they still don't work in real play"): a thrown axe
            // or a bat came while a word was locked, so its letters were misses until that word was done, and it hit
            // first (the self-test's typist always finished its word first, so it never saw this). A letter that is
            // not the locked word's next one but starts a projectile's / small creature's prompt breaks in: that one
            // is typed, then the lock goes back to the word (its letters kept)
            Target* u = nullptr;
            float best = -1;
            for (auto& [o, x] : targets_)
                if (&x != t && x.urgent && x.seen && !x.finishing && x.typed == 0 && !x.text.empty() && same(x.text[0], key) &&
                    x.threat > best)
                    best = x.threat, u = &x;
            if (!u) {
                miss(t);   // a wrong letter: a miss; the lock stays (as in the SEGA games)
                continue;
            }
            if (trace_)
                std::printf("frame %llu: TOTD interrupt: %X \"%s\" over %X\n", (unsigned long long)frame_, u->obj, u->text.c_str(), lock_);
            if (!t->urgent)
                resume_ = lock_;
            lock_ = u->obj;
            t = u;
        }
        ++t->lettersAll;
        ++game.letters, ++chapter.letters;
        if (t->startMs < 0) t->startMs = nowMs_;
        ++t->typed;
        skipPunct(*t);
        if (t->typed >= t->text.size())
            finished(rt, *t);
        else if (t->kind == 0 && !t->small && mem.at<int16_t>(t->obj + 0x72) > kSmallHp)
            // a letter's shot (enemies with hit points to hold; a projectile, an item, a small creature (a bat dies at
            // any hit), a creature without hit points (the frogs) would go at the first letter: they get the last only)
            shots_.push_back({t->obj, t->serial, true});
    }
    queue_.clear();

    // the held enemies (a prompt begun): their hit points up again for this tick; a prompt dropped (Escape): let go
    for (auto& [o, t] : targets_) {
        if (!t.holding || t.finishing) continue;
        if (t.typed == 0) release(rt, t);
        else if (mem.u32(o) != 0 && int32_t(mem.u32(o + 8)) == t.serial && mem.at<int16_t>(o + 0x70) > 0)
            mem.at<int16_t>(o + 0x70) = kHeldHp;
    }
    // this tick's shot (one a tick): a boss attack's next round (its moment counts), else a queued letter, else a
    // killing shot
    bool shot = false;
    for (auto& [o, t] : targets_)
        if ((t.bossShotsLeft > 0 || (!bossLanded_ && frame_ < t.bossPendingUntil)) && frame_ >= t.nextBossShot &&
            fire(rt, {t.obj, t.serial, false})) {
            if (t.bossShotsLeft == 0) ++t.bossShotsLeft;   // (an extra round of a pending attack)
            --t.bossShotsLeft;
            t.nextBossShot = frame_ + uint64_t(std::max(1, cfg_.bossShotGap));
            bossDamageUntil_ = frame_ + 8;   // (the hit lands a frame or two later)
            shot = true;
            break;
        }
    while (!shots_.empty() && !shot) {
        const Shot s = shots_.front();
        shots_.erase(shots_.begin());
        shot = fire(rt, s);
    }
    // the killing shots: projectiles and small creatures first (they are on the player soonest)
    killShot_ = 0;
    for (int pass = 0; pass < 2 && !shot; ++pass)
        for (auto& [o, t] : targets_)
            if (t.finishing > 0 && (pass == 1) != (t.kind == 1 || t.small)) {
                const bool smallOne = t.kind == 1 || t.small;
                // a small one hit: no more shots while it reacts (totd1: the shots of every frame restarted a frog's
                // death and its points each time, so it never went; a barrel may need another a moment later)
                if (smallOne && t.landedAt && frame_ < t.landedAt + 10)
                    continue;
                if (t.kind == 3 && t.finishShots > 0 && frame_ < t.nextBossShot)
                    continue;   // (a prop / lock: its hit reaction between shots)
                if (t.kind == 3)
                    t.nextBossShot = frame_ + 6;
                --t.finishing;
                const bool fired = fire(rt, {t.obj, t.serial, false});
                if (fired && smallOne)
                    killShot_ = o;
                // totd1 (user 2026-11-03: "make sure finishing the word really kills or deflects it"): a small creature
                // or a thrown thing still not hit after kSmallTries killing shots (a hopping frog, a bat turning, the
                // aim a frame behind) is hit as a shot that landed would hit it: flags bit 8 (hit) and bit 1 (by
                // player 1: the points); their own routines take it from there (a bat / frog / Murrer falls, an axe
                // or a barrel is knocked away)
                if (fired && smallOne && !t.landedAt && t.finishShots >= kSmallTries) {
                    if (trace_)
                        std::printf("frame %llu: TOTD hit given to %X (flags %08X state %d)\n", (unsigned long long)frame_, o,
                                    mem.u32(o + 0x58), int(mem.at<int16_t>(o + 0x5C)));
                    mem.at<uint32_t>(o + 0x58) |= 0x102u;
                    t.landedAt = frame_;
                }
                if (t.finishing == 0) {   // the killing shot never got through (behind something, out of reach): the
                    if (t.kind == 1 || t.small) ++smallMissed;
                    giveUp(rt, t);        // enemy as it was, a new prompt
                }
                if (fired) { shot = true; break; }
            }
    // a boss sentence's attack: the bosses' hit points now (after(): what the attack took, x boss_damage_x)
    bossHp_.clear();
    if (frame_ < bossDamageUntil_)
        for (uint32_t obj : rt.shootable) {
            const int k = classify_->targetKind(rt, obj);
            if ((k == 2 || k == 4) && mem.at<int16_t>(obj + 0x70) > 0)
                bossHp_.push_back({obj, int32_t(mem.u32(obj + 8)), mem.at<int16_t>(obj + 0x70)});
        }

    // the attack window for this tick (port::gAttackHold, till after()); the throwers' countdowns (kind 32: their
    // throw has no target test) held at 1 likewise
    bossFight_ = false;
    for (const auto& [o, t] : targets_)
        bossFight_ = bossFight_ || (t.kind == 2 && t.seen);
    if (!bossFight_)
        bossPunish_ = false;
    rt_ = &rt;
    gHolder = this;
    port::gAttackHold = holdHook;
    port::gHitHold = hitHook;
    port::gPreyHold = preyHook;
    for (auto& [o, t] : targets_) {
        if (t.kind != 0 || !t.seen) continue;
        const std::string n = nameOf(rt, mem.u32(o));
        if ((n == "kind32_carry_wait" || n == "kind32_throw_wait") && mem.at<int32_t>(o + 0x2EC) <= 1 && holds(o))
            mem.at<int32_t>(o + 0x2EC) = 2;
    }
    targetedWas_ = mem.at<uint8_t>(G::player_targeted);
    invulnWas_ = int32_t(mem.u32(G::player_invulnerable));

    // the HUD's labels, the most dangerous first
    labels.clear();
    for (auto& [o, t] : targets_) {
        if ((!t.seen && !(t.kind == 2 && o == bossPrimary_)) || t.finishing || t.text.empty() || (t.kind == 2 && o != bossPrimary_)) continue;
        Label l;
        l.obj = o, l.x = t.x, l.y = t.y, l.text = t.text, l.typed = t.typed, l.locked = lock_ == o;
        l.kind = t.kind == 0 && t.small ? 1 : t.kind;
        l.left = t.kind == 2 ? 0 : t.left;
        l.urgent = t.urgent;
        l.window = t.kind == 2 ? std::clamp(1.0f - float(frame_ - t.born) / float(windowOf(t)), 0.0f, 1.0f) : -1.0f;
        if (t.kind == 0 && t.readyAt > t.arrive)
            l.attack = frame_ >= t.readyAt ? 0.0f : float(t.readyAt - frame_) / float(t.readyAt - t.arrive);
        labels.push_back(l);
    }
    std::stable_sort(labels.begin(), labels.end(), [&](const Label& a, const Label& b) {
        if (a.locked != b.locked) return a.locked;
        return targets_[a.obj].threat > targets_[b.obj].threat;
    });
}

void Typing::after(Runtime& rt)
{
    auto& mem = rt.mem;
    // the attack window ends with the tick
    if (gHolder == this) {
        port::gAttackHold = nullptr;
        port::gHitHold = nullptr;
        port::gPreyHold = nullptr;
        gHolder = nullptr;
    }
    for (const auto& [o, p] : preyHeldAt_)   // (the held captors back where they were: no step into the researcher)
        if (mem.u32(o) != 0)
            mem.at<float>(o + 0x20) = p.first, mem.at<float>(o + 0x28) = p.second;
    preyHeldAt_.clear();
    // a small target's / projectile's killing shot: did it land (the shot's hit bit in its flags)
    if (killShot_) {
        if (auto it = targets_.find(killShot_); it != targets_.end() && mem.u32(killShot_) != 0 &&
                                                 int32_t(mem.u32(killShot_ + 8)) == it->second.serial &&
                                                 (mem.u32(killShot_ + 0x58) & 0x100u) && !it->second.landedAt)
            it->second.landedAt = rt.frameCount;
        killShot_ = 0;
    }
    if (rt_) {
        rt_ = nullptr;
        // an attack on player 1 began this tick (an enemy took the player as its target), or player 1 was hit (a
        // Murrer's leap, a thrown thing): the next one waits attack_gap; the creatures whose timer had run out wait
        // attack_wait again (TotD: the zombie steps back, its timer starts over)
        const bool began = targetedWas_ == 0 && mem.at<uint8_t>(G::player_targeted) != 0;
        const bool hit = invulnWas_ <= 0 && int32_t(mem.u32(G::player_invulnerable)) > 0;
        if (trace_ && targetedWas_ != mem.at<uint8_t>(G::player_targeted))
            std::printf("frame %llu: TOTD player 1 targeted %d -> %d\n", (unsigned long long)rt.frameCount, int(targetedWas_),
                        int(mem.at<uint8_t>(G::player_targeted)));
        if (hit && trace_) {   // (developer: what could have hit: the boss punish, creatures whose timer ran out, projectiles)
            std::string who;
            for (const auto& [o, t] : targets_)
                if ((t.kind == 0 && t.readyAt && t.readyAt <= rt.frameCount) || t.kind == 1 || (t.small && t.seen) ||
                    (t.kind == 0 && mem.f32(o + 0x40) < 30.0f)) {
                    char b[64];
                    std::snprintf(b, sizeof b, "(z %.0f ready %lld%s)", mem.f32(o + 0x40),
                                  t.readyAt ? (long long)t.readyAt - (long long)rt.frameCount : -9999LL, t.typed ? " typing" : "");
                    who += " " + nameOf(rt, mem.u32(o)) + b;
                }
            for (uint32_t obj : rt.shootable)   // (and creatures with no prompt, near)
                if (!targets_.count(obj) && classify_->targetKind(rt, obj) == 0 && mem.f32(obj + 0x40) < 30.0f)
                    who += " " + nameOf(rt, mem.u32(obj)) + "(no prompt)";
            std::printf("frame %llu: TOTD hit taken: boss %d punish %d, ready:%s\n", (unsigned long long)rt.frameCount, int(bossFight_),
                        int(bossPunish_), who.c_str());
        }
        if (hit) ++hits, ++game.hits, ++chapter.hits, bossPunish_ = false;
        if (began || hit) {
            if (began) {
                ++attacks;
                if (lastAttack_)
                    attackGapMin = std::min<uint32_t>(attackGapMin, uint32_t(rt.frameCount - lastAttack_));
            }
            lastAttack_ = rt.frameCount;
            for (auto& [o, t] : targets_)
                if (t.kind == 0 && t.readyAt && t.readyAt <= rt.frameCount) {
                    t.arrive = rt.frameCount;
                    t.readyAt = rt.frameCount + std::max<uint64_t>(1, ticks(cfg_.attackWait[difficulty_]));
                }
            if (trace_)
                std::printf("frame %llu: TOTD attack%s%s\n", (unsigned long long)rt.frameCount, began ? " began" : "", hit ? " hit" : "");
        }
    }
    // a boss attack's hit: the bosses stagger at the first hit and take no more for a while (the Chariot: 40 a hit), so
    // a finished sentence is about one hit; it counts boss_damage_x times (the extra taken from the hit points after
    // the boss's own hit routine ran: never below 1, so the boss's own checks, armour off / death, come with the next
    // hit, as a real one would)
    for (const BossHp& b : bossHp_) {
        if (mem.u32(b.obj) == 0 || int32_t(mem.u32(b.obj + 8)) != b.serial)
            continue;
        const int16_t hp = mem.at<int16_t>(b.obj + 0x70);
        if (hp < b.hp)
            bossLanded_ = true;   // (the attack did damage: a pending one ends)
        if (hp <= 0 || hp >= b.hp)
            continue;
        const int extra = (b.hp - hp) * (cfg_.bossDamageX - 1);
        mem.at<int16_t>(b.obj + 0x70) = int16_t(std::max(1, int(hp) - extra));
        if (trace_)
            std::printf("frame %llu: TOTD boss %X hit %d -> %d (x%d)\n", (unsigned long long)rt.frameCount, b.obj, b.hp, mem.at<int16_t>(b.obj + 0x70), cfg_.bossDamageX);
    }
    bossHp_.clear();
    // totd2: the typed small targets / projectiles: killed as the game sees it (a bat falling: state 5 / 6; a thrown
    // thing knocked away or broken: another routine, or gone, with no hit on player 1 meanwhile; a frog / dog / Murrer:
    // another routine or state, or gone), else not (the report's third number)
    for (auto it = follow_.begin(); it != follow_.end();) {
        const Follow& f = *it;
        const bool gone = mem.u32(f.obj) == 0 || int32_t(mem.u32(f.obj + 8)) != f.serial;
        const int16_t st = gone ? -1 : mem.at<int16_t>(f.obj + 0x5C);
        const uint32_t r = gone ? 0 : mem.u32(f.obj);
        bool ok = false, over = rt.frameCount > f.until;
        if (f.what == 0)
            ok = gone || st == 5 || st == 6;
        else if (f.what == 1) {
            ok = (gone || r != f.routine || over) && hits == f.hitsAt;   // (a barrel: the first hit knocks it away, same routine)
            over = over || hits != f.hitsAt;
        } else
            ok = gone || r != f.routine || st != f.state || classify_->targetKind(rt, f.obj) == 5;
        if (ok || over) {
            if (ok)
                for (TypingStats* s : {&game, &chapter})
                    ++(f.what == 0 ? s->bats : f.what == 1 ? s->projectiles : s->small)[2];
            if (trace_)
                std::printf("frame %llu: TOTD followed %X: %s\n", (unsigned long long)rt.frameCount, f.obj, ok ? "killed" : "NOT killed");
            it = follow_.erase(it);
        } else
            ++it;
    }
    if (std::getenv("HOTD_TOTD_BOSSHP"))   // (developer: the bosses' hit points as they change)
        for (uint32_t obj : rt.shootable) {
            static std::map<uint32_t, int> was;
            if (classify_->targetKind(rt, obj) != 2 && classify_->targetKind(rt, obj) != 4) continue;
            const int hp = mem.at<int16_t>(obj + 0x70);
            if (was[obj] != hp)
                std::printf("frame %llu: boss %X %s hp %d (gun %d)\n", (unsigned long long)rt.frameCount, obj,
                            rt.routineName(mem.u32(obj)).c_str(), hp, int(rt.gun[0].trigger));
            was[obj] = hp;
        }
    // a held enemy that went down after all before its prompt was done (the self-test counts them: should be none)
    for (auto& [o, t] : targets_)
        if (t.holding && !t.finishing && !t.wentDown && mem.u32(o) != 0 && int32_t(mem.u32(o + 8)) == t.serial &&
            mem.at<int16_t>(o + 0x70) <= 0) {
            t.wentDown = true;
            ++letterKills;
            if (trace_)
                std::printf("frame %llu: TOTD held %X went down (hp %d)\n", (unsigned long long)rt.frameCount, o, mem.at<int16_t>(o + 0x70));
        }
}

// the killing shot never landed: the enemy's hit points as before the prompt, a new prompt for it
void Typing::giveUp(Runtime& rt, Target& t)
{
    auto& mem = rt.mem;
    if (mem.u32(t.obj) != 0 && int32_t(mem.u32(t.obj + 8)) == t.serial && t.kind == 0 && mem.at<int16_t>(t.obj + 0x72) > 0 &&
        mem.at<int16_t>(t.obj + 0x70) > 0)
        mem.at<int16_t>(t.obj + 0x70) = std::max<int16_t>(1, t.realHp);
    if (trace_)
        std::printf("frame %llu: TOTD gave up on %X (%d killing shots), hp back to %d\n", (unsigned long long)frame_, t.obj,
                    t.finishShots, mem.at<int16_t>(t.obj + 0x70));
    t.holding = false;
    t.finishShots = 0;
    if (lock_ == t.obj) lock_ = 0;
    t.text = newPrompt(rt, t);
    t.typed = 0;
    t.startMs = -1;
    skipPunct(t);
}

// a held enemy let go (the prompt dropped, cancelled, a cut-scene): its real hit points back
void Typing::release(Runtime& rt, Target& t)
{
    if (t.holding && rt.mem.u32(t.obj) != 0 && int32_t(rt.mem.u32(t.obj + 8)) == t.serial && rt.mem.at<int16_t>(t.obj + 0x70) > 0)
        rt.mem.at<int16_t>(t.obj + 0x70) = std::max<int16_t>(1, t.realHp);
    t.holding = false;
}

// ---- the scripted typist (testing) ----

namespace {
// a key next to `c` on a QWERTY keyboard (a typist's slip)
char neighbour(char c, uint64_t r)
{
    static const char* rows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    const char lc = char(std::tolower(static_cast<unsigned char>(c)));
    for (int y = 0; y < 3; ++y)
        if (const char* p = std::strchr(rows[y], lc); p && lc) {
            const int x = int(p - rows[y]), n = int(std::strlen(rows[y]));
            const int nx = std::clamp(x + ((r & 1) ? 1 : -1), 0, n - 1);
            return rows[y][nx != x ? nx : (x > 0 ? x - 1 : x + 1)];
        }
    return char('a' + r % 26);
}
} // namespace

Typist::Typist(uint64_t seed, float wpm, float accuracy) : rng_(seed * 0x9E3779B97F4A7C15ull + 99), wpm_(wpm), acc_(accuracy) {}

void Typist::tick(Runtime& rt, Typing& t, double tickMs, const std::function<void(uint32_t key)>& press)
{
    ++f_;
    nowMs_ += tickMs;
    if (!Typing::inPlay(rt) || t.labels.empty()) {
        carry_ = 0;
        lostMs_ = -1;
        seenMs_.clear();
        return;
    }
    // what the screen shows: when each prompt was first seen (a person reads it a reaction time later)
    std::map<uint32_t, double> now;
    for (const Typing::Label& l : t.labels) {
        const auto it = seenMs_.find(l.obj);
        now[l.obj] = it != seenMs_.end() ? it->second : nowMs_;
    }
    seenMs_.swap(now);
    const double react = 250.0 + 9000.0 / double(std::max(10.0f, wpm_));   // 70 WPM: 0.38 s, 40 WPM: 0.48 s
    const auto known = [&](const Typing::Label& l) { return nowMs_ - seenMs_[l.obj] >= react; };
    // a locked prompt off the screen: wait a second, then Escape (drop it) and type another
    const Typing::Label* lockedL = nullptr;
    for (const Typing::Label& l : t.labels)
        if (l.locked) lockedL = &l;
    if (t.locked() && !lockedL) {
        if (lostMs_ < 0) lostMs_ = nowMs_;
        if (nowMs_ - lostMs_ < 1000.0)
            return;
        press(27);
        lostMs_ = -1;
        carry_ = 0;
        return;
    }
    lostMs_ = -1;
    if (f_ < waitUntil_)
        return;
    const double perKey = 60000.0 / (double(std::max(1.0f, wpm_)) * 5.0);
    carry_ += tickMs;
    if (carry_ < perKey)
        return;
    carry_ = std::min(carry_ - perKey, perKey);
    // the next prompt: a thrown thing / bat / small creature read meanwhile breaks in (the first letter); else the
    // locked one; else the one whose attack timer runs out first, else the most dangerous (the HUD's order)
    const Typing::Label* l = nullptr;
    for (const Typing::Label& x : t.labels)
        if (x.urgent && !x.locked && x.typed == 0 && known(x) && (!lockedL || !lockedL->urgent)) {
            l = &x;
            break;
        }
    if (!l)
        l = lockedL;
    if (!l) {
        float best = 2.0f;
        for (const Typing::Label& x : t.labels)
            if (known(x) && x.attack >= 0 && x.attack < best)
                best = x.attack, l = &x;
        for (int pass = 0; pass < 2; ++pass)   // (the creatures and the boss before the items, as a player would)
            for (const Typing::Label& x : t.labels)
                if (!l && known(x) && (pass == 1 || x.kind != 3))
                    l = &x;
    }
    if (!l || l->typed >= l->text.size())
        return;
    // as shown (capitals with Shift from Hard), else in lower case as one types without Shift
    const char want = t.caseSensitive() ? l->text[l->typed] : char(std::tolower(static_cast<unsigned char>(l->text[l->typed])));
    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng_) >= acc_ && std::isalpha(static_cast<unsigned char>(want)))
        press(uint8_t(neighbour(want, rng_())));
    else
        press(uint8_t(want));
    if (l->typed + 1 >= l->text.size()) {   // a prompt done: a short look for the next (5..15 ticks)
        waitUntil_ = f_ + 5 + rng_() % 11;
        carry_ = 0;
    }
}

} // namespace hotd::game
