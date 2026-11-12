// The Typing of the Dead 0 (typing.h): prompts, lock-on, the letters' shots, the attack window, the stats, the
// scripted typist.
#include "game/typing.h"

#include "game/audio.h"
#include "game/bot.h"
#include "game/typing_packs.h"

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
#include <tuple>

namespace hotd::port { extern bool (*gAttackHold)(uint32_t obj); extern bool (*gHitHold)(int player); extern bool (*gPreyHold)(uint32_t obj); extern bool (*gBossHold)(uint32_t obj); }   // the hooks (port/game.h)

namespace hotd::game {
extern bool (*gTotdProjectileHold)();   // (enemy_support.cpp: Runtime::projectile_hit_player)

using G = A::G;
using port::a::Player;

namespace {
constexpr int kKeepUnseen = 300;     // frames a prompt stays with an enemy out of the list (a flicker, a duck, behind
                                     // something): it keeps its word when it comes back (totd1: was 60, words changed)
constexpr int kFinishTries = 40;     // frames the killing shot is tried (the enemy behind something, moving)
constexpr int16_t kHeldHp = 30000;   // a letter's shot: the hit points for that frame (no kill)
constexpr int kSmallHp = 10;        // creatures with this many hit points or fewer get no letter shots (a bat dies at
                                     // any hit: its first letter killed it, totd1)
constexpr uint64_t kMeasure = 8;     // frames an enemy is tracked before its prompt (its speed toward the camera: the
                                     // word's length by threat, totd2)
// totd7 (user 2026-11-05: "the G and H flicker", "a huge no-no"): a box stays up while its target is out of the list for
// up to kSeenLatch frames; a word goes from one that cannot be shot only after kNoShootFrames in a row; a quick key
// changes side only past kSideMargin units from the screen's middle
constexpr uint64_t kSeenLatch = 60;
constexpr uint64_t kNoShootFrames = 10;
constexpr float kSideMargin = 60.0f;

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
bool bossHook(uint32_t obj) { return gHolder && gHolder->holdsBoss(obj); }
bool projHook() { return gHolder && gHolder->holdsProjectile(); }
} // namespace

Typing::Typing() : rng_(0x70D0), classify_(std::make_unique<Bot>(0x70D0, 1)) {}
Typing::~Typing()
{
    if (gHolder == this) {
        gHolder = nullptr;
        port::gAttackHold = nullptr;
        port::gHitHold = nullptr;
        port::gPreyHold = nullptr;
        port::gBossHold = nullptr;
        gTotdProjectileHold = nullptr;
    }
}

size_t Typing::promptCount() const
{
    size_t n = 0;
    for (const auto& [k, v] : lists_) n += v.size();
    return n;
}

bool Typing::load(const std::vector<std::string>& dataDirs, std::string& err, const std::string& dictionary,
                  const std::vector<std::string>& packs)
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
            if (k.rfind("drill_", 0) == 0) {   // totdpacks: Practice (its own if: the chain below is long for MSVC)
                if (k == "drill_words") cfg_.drillWords = std::max(1, i);
                else if (k == "drill_on_screen") cfg_.drillMax = std::clamp(i, 1, 7);
                else if (k == "drill_gap") perDifficulty(v, cfg_.drillGap, flt);
                else if (k == "drill_seconds") perDifficulty(v, cfg_.drillSeconds, flt);
                else if (k == "drill_per_letter") perDifficulty(v, cfg_.drillPerLetter, flt);
                continue;
            }
            if (k == "dictionary") cfg_.dictionary = v;
            else if (k == "auto_skip_punctuation") cfg_.autoSkipPunct = i != 0;
            else if (k == "drop_lock_keys") cfg_.dropLockKeys = std::clamp(i, 0, 2);
            else if (k == "case_from_difficulty") cfg_.caseFromDifficulty = i;
            else if (k == "combo_bonus") cfg_.comboBonus = i;
            else if (k == "recent_words") cfg_.recentWords = std::max(0, i);   // (totdex: SEGA's rules)
            else if (k == "shift_grace") cfg_.shiftGrace = i != 0;
            else if (k == "practice_share") cfg_.practiceShare = std::clamp(flt(v), 0.0f, 1.0f);   // (totdrec: Practice Your Misses)
            else if (k == "practice_words") cfg_.practiceWords = std::max(0, i);
            else if (k == "practice_keys") cfg_.practiceKeys = std::clamp(i, 0, 26);
            else if (k == "word_level_hit") cfg_.wordLevelHit = std::max(0, i);
            else if (k == "word_level_min") cfg_.wordLevelMin = std::min(0, i);
            else if (k == "word_level_words") cfg_.wordLevelWords = std::max(1, i);
            else if (k == "key_cost_grade") cfg_.keyCostGrade = i != 0;
            else if (k == "rank_points") {
                std::vector<int> p;
                for (const std::string& s : splitList(v)) p.push_back(std::atoi(s.c_str()));
                if (!p.empty()) cfg_.rankPoints = p;
            }
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
            else if (k == "offscreen_hold") cfg_.offscreenHold = std::max(0.0f, flt(v));   // (totdrec)
            else if (k == "strike_seen") perDifficulty(v, cfg_.strikeSeen, flt);
            else if (k == "attack_queue") perDifficulty(v, cfg_.attackQueue, flt);
            else if (k == "hit_reset") perDifficulty(v, cfg_.hitReset, flt);
            else if (k == "rescue_wait") perDifficulty(v, cfg_.rescueWait, flt);
            else if (k == "attack_hold") cfg_.attackHold = std::clamp(i, 0, 2);
            else if (k == "game_speed") perDifficulty(v, cfg_.gameSpeed, flt);
            else if (k == "boss_sentences") perDifficulty(v, cfg_.bossSentences, [](const std::string& s) { return std::max(1, std::atoi(s.c_str())); });
            else if (k == "auto_names") cfg_.autoNames = splitList(v);
            else if (k == "auto_delay") cfg_.autoDelay = flt(v);
            else if (k == "idle_seconds") cfg_.idleSeconds = flt(v);
            else if (k == "boss_rest") perDifficulty(v, cfg_.bossRest, flt);
            else if (k == "small_wait") perDifficulty(v, cfg_.smallWait, flt);
            else if (k == "small_queue") perDifficulty(v, cfg_.smallQueue, flt);
            else if (k == "ball_speed") perDifficulty(v, cfg_.ballSpeed, flt);
            else if (k == "barrage_words") perDifficulty(v, cfg_.barrageWords, [](const std::string& s) { return std::max(0, std::atoi(s.c_str())); });
            else if (k == "boss_shots") cfg_.bossShots = std::max(1, i);
            else if (k == "boss_shot_gap") cfg_.bossShotGap = std::max(1, i);
            else if (k == "boss_strike_seconds") cfg_.bossStrikeSeconds = std::max(0.5f, flt(v));
            else if (k == "boss_strike_gap") perDifficulty(v, cfg_.bossStrikeGap, flt);
            else if (k == "boss_block_warn") perDifficulty(v, cfg_.bossBlockWarn, flt);
            else if (k == "boss_block_letters") perDifficulty(v, cfg_.bossBlockLetters, [](const std::string& s) { return std::max(0, std::atoi(s.c_str())); });
            else if (k == "box_anchor") cfg_.boxAnchor = std::clamp(flt(v), 0.0f, 1.0f);
            else if (k == "boss_final_x") cfg_.bossFinalX = std::clamp(flt(v), 0.2f, 5.0f);
            else if (k == "fireball_words") cfg_.fireballWords = i != 0;
            else if (k.rfind("boss_x_", 0) == 0) cfg_.bossX[k.substr(7)] = std::clamp(flt(v), 0.2f, 5.0f);
            else if (k == "boss_damage_x") cfg_.bossDamageX = std::max(1, i);
            else if (k == "boss_window_seconds") perDifficulty(v, cfg_.bossWindowSeconds, flt);
            else if (k == "boss_window_per_letter") perDifficulty(v, cfg_.bossWindowPerLetter, flt);
            else if (k == "item_names") cfg_.itemNames = splitList(v);
            else if (k == "route_names") cfg_.routeNames = splitList(v);
            else if (k == "small_names") cfg_.smallNames = splitList(v);
            else if (k == "sounds") cfg_.sounds = i != 0;
            else if (k == "key_cue") cfg_.keyCue = i;
            else if (k == "miss_cue") cfg_.missCue = i;
            else if (k == "ding_cue") cfg_.dingCue = i;
            else if (k == "key_volume") cfg_.keyVolume = std::clamp(flt(v), 0.0f, 4.0f);
            else if (k == "miss_volume") cfg_.missVolume = std::clamp(flt(v), 0.0f, 4.0f);
            else if (k == "ding_volume") cfg_.dingVolume = std::clamp(flt(v), 0.0f, 4.0f);
            else if (k == "sound_peak") cfg_.soundPeak = std::clamp(flt(v), 0.0f, 1.0f);
            else if (k == "shot_sound") cfg_.shotSound = uint32_t(std::strtoul(v.c_str(), nullptr, 0));
            else if (k == "quick_keys") cfg_.quickKeys = i != 0;
            else if (k == "quick_left" && !v.empty()) cfg_.quickLeft = low(uint8_t(v[0]));
            else if (k == "quick_right" && !v.empty()) cfg_.quickRight = low(uint8_t(v[0]));
            else if (k == "group_dx") cfg_.groupDx = flt(v);
            else if (k == "group_dy") cfg_.groupDy = flt(v);
            else if (k == "group_dz") cfg_.groupDz = flt(v);
            else if (k == "prop_group_dx") cfg_.propGroupDx = flt(v);
            else if (k == "prop_group_dy") cfg_.propGroupDy = flt(v);
            else if (k == "prop_group_dz") cfg_.propGroupDz = flt(v);
            else if (k == "group_max") cfg_.groupMax = std::max(1, i);
            else if (k == "pickup_delay") cfg_.pickupDelay = flt(v);
            else if (k == "box_shadow") cfg_.boxShadow = i != 0;
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
    // totd4: the sounds' files (each from the last folder that has it; none: the game's folder, the PC stand-in)
    soundDir_.clear();
    for (const char* n : {"key", "miss", "ding"}) {
        for (auto d = dataDirs.rbegin(); d != dataDirs.rend() && !soundDir_.count(n); ++d)
            if (std::filesystem::exists(*d + "/data/typing/" + n + ".wav", ec))
                soundDir_[n] = *d + "/data/typing";
        if (!soundDir_.count(n) && !dataDirs.empty())
            soundDir_[n] = dataDirs.front() + "/data/typing";
    }
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
    // totdpacks (after Tenome's Typing of the Dead EX word packs, MIT, ideas only): the packs chosen, mixed: each list
    // the union of theirs (pick() pools the lists and takes each prompt once); the built-in lists only when "builtin" is
    // one of them. A list none of them has: the nearest one they have (pick(): the longest list), but the bosses'
    // sentences from the built-in lists when no chosen pack has any (as Tenome's packs fall back to the game's words)
    packsInfo.clear(), packsMissing.clear();
    text_.clear(), textKey.clear();
    textPos = 0;
    if (!packs.empty() && !(packs.size() == 1 && packs[0] == kBuiltinPack)) {
        const auto builtinLists = lists_;
        const bool builtin = std::find(packs.begin(), packs.end(), kBuiltinPack) != packs.end();
        if (!builtin) lists_.clear();
        const std::vector<TypingPack> all = typingPacksScan(dataDirs);
        std::string took = builtin ? std::string(kBuiltinPack) : std::string();
        for (const std::string& id : packs) {
            if (id == kBuiltinPack) continue;
            const auto pk = std::find_if(all.begin(), all.end(), [&](const TypingPack& p) { return p.id == id; });
            if (pk == all.end()) {
                packsMissing += (packsMissing.empty() ? "" : ", ") + id;
                continue;
            }
            for (const auto& [name, l] : pk->lists)
                lists_[name].insert(lists_[name].end(), l.begin(), l.end());
            took += (took.empty() ? "" : " + ") + id;
            if (pk->text && !pk->pieces.empty()) {   // (order = text: its pieces in order, the packs' in their order)
                text_.insert(text_.end(), pk->pieces.begin(), pk->pieces.end());
                textKey += (textKey.empty() ? "" : "+") + id;
                took += " (a text, " + std::to_string(pk->pieces.size()) + " pieces)";
            }
        }
        bool bossAny = false;
        for (const std::string& b : cfg_.bossLists) bossAny = bossAny || !lists_[b].empty();
        if (!bossAny)
            for (const std::string& b : cfg_.bossLists)
                if (auto it = builtinLists.find(b); it != builtinLists.end()) lists_[b] = it->second;
        for (auto it = lists_.begin(); it != lists_.end();)   // (no empty lists: pick()'s fallback looks for the longest)
            it = it->second.empty() ? lists_.erase(it) : std::next(it);
        if (promptCount() == 0) {   // (none of the packs found / usable: the built-in lists)
            lists_ = builtinLists;
            took = kBuiltinPack;
            text_.clear(), textKey.clear();
        }
        if (!textKey.empty()) textKey += ":" + std::to_string(text_.size());
        packsInfo = took + (bossAny ? "" : " (the built-in boss sentences)");
    }
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
    bossSeenAt_ = bossStrikeUntil_ = bossUnseenAsk_ = nextFreeStrike_ = blockFrom_ = blockShotFrom_ = blockShotUntil_ = warnUntil_ = staggerAt_ = 0, bossStriker_ = blockObj_ = 0;
    strikeBlocked_ = false;
    bossLanded_ = true;
    lastBossFinish_ = 0;
    bossDownUntil_ = 0;
    recent_.clear();
    textServed = textLoops = 0;   // (totdpacks: textPos stays: main.cpp sets the saved place)
    altGoal_.clear();
    lastShifted_ = false;
    wordLevel = levelRun_ = 0;
    attacks = hits = smallTyped = smallKilled = smallMissed = letterKills = 0;
    attackGapMin = UINT32_MAX;
    given.clear();
    popups.clear();
    follow_.clear();
    killed_.clear();
    resume_ = 0;
    freeSinceMs_ = 0;
    appearedAt_ = INT_MIN;
    neutral_.clear();
    idleSince_ = 0;
    lastHit_ = bossRestUntil_ = 0;
    hitFrom.clear();
    blocked_ = 0;
    quickTyped = quickIgnored = ignoredKeys = groupsMade = groupKills = pickupsAuto = 0;
}

// totdpacks: the mutators' ids in launcher.ini (totd_mutators=)
std::string TypingMutators::csv() const
{
    std::string s;
    const auto add = [&](bool on, const char* id) { if (on) s += (s.empty() ? "" : ",") + std::string(id); };
    add(longWords, "long"), add(shortWords, "short"), add(oneLife, "onelife"), add(noBackspace, "nobackspace");
    add(doubleSpeed, "double"), add(reversed, "reverse");
    return s;
}
TypingMutators TypingMutators::parse(const std::string& csv)
{
    TypingMutators m;
    for (const std::string& id : splitList(csv)) {
        if (id == "long") m.longWords = true, m.shortWords = false;
        else if (id == "short") m.shortWords = true, m.longWords = false;
        else if (id == "onelife") m.oneLife = true;
        else if (id == "nobackspace") m.noBackspace = true;
        else if (id == "double") m.doubleSpeed = true;
        else if (id == "reverse") m.reversed = true;
    }
    return m;
}

bool Typing::cancel()
{
    if (mutators.noBackspace) return false;   // (totdpacks: No Backspace: a word begun stays locked)
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

// totdex (SEGA's judge, Tod.exe: a key that misses is judged again as its shifted character when Shift was down
// for the key before it: Shift let go a moment early in "Run!" or "Help?"): US key pairs
uint32_t Typing::shiftOf(uint32_t k)
{
    static const char* from = "1234567890-=[]\\;',./`";
    static const char* to = "!@#$%^&*()_+{}|:\"<>?~";
    if (k >= 'a' && k <= 'z') return k - 'a' + 'A';
    if (k > 0 && k < 128)
        if (const char* p = std::strchr(from, int(k))) return uint32_t(uint8_t(to[p - from]));
    return k;
}

bool Typing::shiftedKey(uint32_t k)
{
    return (k >= 'A' && k <= 'Z') || (k > 0 && k < 128 && std::strchr("!@#$%^&*()_+{}|:\"<>?~", int(k)));
}

bool Typing::keyMatches(char want, uint32_t k, bool prevShift) const
{
    return same(want, k) || (cfg_.shiftGrace && prevShift && shiftOf(k) != k && same(want, shiftOf(k)));
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
bool Typing::pick(const std::vector<std::string>& lists, int minLen, int maxLen, uint32_t self, std::string& out,
                  const std::vector<std::string>* onlyWords, const std::string* onlyKeys)
{
    const bool only = onlyWords || onlyKeys;   // (totdrec: Practice Your Misses)
    // totd2: the lists pooled (each prompt once), then by length
    std::vector<std::string> pool;
    std::set<std::string> have;
    if (onlyWords) {
        for (const std::string& s : *onlyWords)
            if (have.insert(s).second) pool.push_back(s);
    } else
        for (const std::string& list : lists)
            if (auto it = lists_.find(list); it != lists_.end())
                for (const std::string& s : it->second)
                    if (have.insert(s).second) pool.push_back(s);
    // (lists missing from the dictionary: totdpacks: every list it has but the bosses' (a pack of words only gave its
    // tough zombies boss sentences), by length as always; were: the longest one it has)
    const bool bossPick = !lists.empty() && std::find(cfg_.bossLists.begin(), cfg_.bossLists.end(), lists[0]) != cfg_.bossLists.end();
    if (pool.empty() && !only)
        for (const auto& [k, l] : lists_)
            if (bossPick || std::find(cfg_.bossLists.begin(), cfg_.bossLists.end(), k) == cfg_.bossLists.end())
                for (const std::string& s : l)
                    if (have.insert(s).second) pool.push_back(s);
    if (pool.empty() && !only)
        for (const auto& [k, l] : lists_)
            if (l.size() > pool.size()) pool = l;
    if (pool.empty()) {
        if (only) return false;
        out = "zombie";
        return false;
    }
    if (mutators.reversed)   // (totdpacks: the Reversed mutator: every prompt backwards; the rules on what is shown)
        for (std::string& s : pool) std::reverse(s.begin(), s.end());
    const std::vector<std::string>* v = &pool;
    // (totdrec: a practice word has one of the missed keys)
    const auto keyed = [&](const std::string& s) {
        if (!onlyKeys) return true;
        for (const char c : s)
            if (onlyKeys->find(char(low(uint8_t(c)))) != std::string::npos) return true;
        return false;
    };
    std::vector<size_t> cand;
    for (int widen = 0; cand.empty() && widen < (only ? 3 : 20); ++widen)   // (none that long: the nearest lengths)
        for (size_t i = 0; i < v->size(); ++i) {
            const int n = int((*v)[i].size());
            if ((minLen <= 0 || n >= minLen - widen) && (maxLen <= 0 || n <= maxLen + widen) && keyed((*v)[i])) cand.push_back(i);
        }
    if (cand.empty() && only)
        return false;
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
    // totd7: the quick keys (G / H by screen side) never start a word
    const auto quickFirst = [&](const std::string& s) {
        return cfg_.quickKeys && !s.empty() && (low(uint8_t(s[0])) == cfg_.quickLeft || low(uint8_t(s[0])) == cfg_.quickRight);
    };
    // totdex (SEGA's rule, Tod.exe's picker: never one of its last 16 picks): first without the recent_words last
    // prompts, then (a short list) with them
    bool strict = cfg_.recentWords > 0;
    const auto ok = [&](const std::string& s) {
        if (s.empty() || firstUsed[uint8_t(low(uint8_t(s[0]))) & 127] || shown.count(s) || quickFirst(s)) return false;
        return !strict || std::find(recent_.begin(), recent_.end(), s) == recent_.end();
    };
    const auto took = [&](const std::string& s) {
        out = s;
        if (cfg_.recentWords > 0 && maxLen != 1) {   // (the one-letter quick / small ones repeat by nature)
            recent_.push_back(s);
            while (int(recent_.size()) > cfg_.recentWords) recent_.pop_front();
        }
        return true;
    };
    size_t at = 0;
    for (int pass = 0; pass < 2; ++pass, strict = false) {
        if (pass == 1 && (!cfg_.recentWords || only)) break;   // (a practice word: never a recent one)
        // a few at random, then the whole list from a random place (a short list nearly used up)
        for (int tries = 0; tries < 12; ++tries) {
            const std::string& s = (*v)[cand[size_t(rng_() % cand.size())]];
            if (ok(s)) return took(s);
        }
        at = size_t(rng_() % cand.size());
        for (size_t i = 0; i < cand.size(); ++i) {
            const std::string& s = (*v)[cand[(at + i) % cand.size()]];
            if (ok(s)) return took(s);
        }
    }
    if (only)
        return false;
    out = (*v)[cand[at]];
    for (size_t i = 0; i < cand.size() && quickFirst(out); ++i)
        out = (*v)[cand[(at + i) % cand.size()]];
    return false;
}

// totdrec (step 3 of docs/typing.md's Tenome plan: "practice from your misses", user 2026-11-08; Tenome's Typing of
// the Dead EX keeps the same statistics): Practice Your Misses (off by default) gives practice_share of the creature
// words from your misses: half of them (when there are any of about that length) one of the words you missed most,
// the rest a word of the lists with one of the letters you missed most. The same rules as every pick (first letters,
// no two alike on the screen, not one of the last recent_words)
void Typing::setPractice(std::vector<std::string> words, std::string keys)
{
    practiceWords_.clear();
    for (std::string& w : words)
        if (!w.empty() && int(practiceWords_.size()) < std::max(0, cfg_.practiceWords)) practiceWords_.push_back(std::move(w));
    practiceKeys_.clear();
    for (const char c : keys)
        if (c >= 'a' && c <= 'z' && practiceKeys_.find(c) == std::string::npos && int(practiceKeys_.size()) < std::max(0, cfg_.practiceKeys))
            practiceKeys_ += c;
    practicePicks = practiceFromWords = practiceFromKeys = 0;
}

bool Typing::practicePick(const std::vector<std::string>& lists, int minLen, int maxLen, uint32_t self, std::string& out)
{
    if (!practiceOn() || cfg_.practiceShare <= 0.0f || float(rng_() % 1000) >= cfg_.practiceShare * 1000.0f)
        return false;
    const bool wordsFirst = !practiceWords_.empty() && (practiceKeys_.empty() || rng_() % 2 == 0);
    for (int k = 0; k < 2; ++k) {
        const bool words = (k == 0) == wordsFirst;
        if (words && !practiceWords_.empty() && pick(lists, minLen, maxLen, self, out, &practiceWords_, nullptr)) {
            ++practiceFromWords;
            return true;
        }
        if (!words && !practiceKeys_.empty() && pick(lists, minLen, maxLen, self, out, nullptr, &practiceKeys_)) {
            ++practiceFromKeys;
            return true;
        }
    }
    return false;
}

// totdpacks (Tenome's Typing of the Dead EX types a pack in file order; here a pack's order = text): the text's next
// piece when its first letter is free now (no other word on the screen starts with it, not shown); else false and the
// piece waits (the creature gets a word of the lists). The last piece: the text from its start. A piece may start
// with a quick key's letter (G / H: "he", "gone"): that key goes to a quick target on its side first, else it locks
// the word (the key handler below)
bool Typing::textPick(uint32_t self, std::string& out)
{
    if (text_.empty()) return false;
    if (textPos >= text_.size()) textPos = 0;
    std::string s = text_[textPos];
    if (mutators.reversed) std::reverse(s.begin(), s.end());   // (the Reversed mutator)
    const char f = low(uint8_t(s[0]));
    for (const auto& [o, t] : targets_)
        if (o != self && !t.text.empty() && t.finishing == 0 && (low(uint8_t(t.text[0])) == f || t.text == s))
            return false;
    out = s;
    ++textServed;
    if (++textPos >= text_.size()) textPos = 0, ++textLoops;
    return true;
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
    } else if (t.kind == 1 && t.routine == 0x73010 && !t.quick) {   // (totd8: the Magician's fireballs: a word)
        lists = cfg_.wordLists, minLen = cfg_.wordMin[d][0], maxLen = cfg_.wordMax[d][0];
        t.cls = "fireball";
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
    // totdpacks (the Long Words / Short Words mutators): the creatures' words (and a fireball's) from the far band's
    // longest lengths up (a tough one's phrase from its longest), or the near band's shortest (a tough one a word)
    if (!t.quick && (mutators.longWords || mutators.shortWords) &&
        (t.cls == "tough" || t.cls == "captor" || t.cls == "rescue scene" || t.cls == "fireball" || t.cls.rfind("zombie", 0) == 0)) {
        if (mutators.longWords && t.cls == "tough")
            minLen = cfg_.toughMax[dc], maxLen = cfg_.toughMax[dc] + 4;
        else if (mutators.longWords)
            lists = cfg_.wordLists, minLen = cfg_.wordMax[dc][2], maxLen = cfg_.wordMax[dc][2] + 3;
        else
            lists = cfg_.wordLists, minLen = maxLen = cfg_.wordMin[d][0];
    }
    // totdex (SEGA's word level: Tod.exe's words get easier after a hit and harder again while you do well): below 0 a
    // creature's word is one letter shorter a level (a tough one's phrase two), never under 2 letters
    if (wordLevel < 0 && !t.quick && (t.cls == "tough" || t.cls == "captor" || t.cls == "rescue scene" || t.cls.rfind("zombie", 0) == 0)) {
        const int k = t.cls == "tough" ? 2 : 1;
        const int floor = std::min(minLen, 2);
        minLen = std::max(floor, minLen + wordLevel * k);
        maxLen = std::max(minLen, maxLen + wordLevel * k);
    }
    std::string s;
    if (t.quick)   // (totd7: one key by screen side, no word)
        s = std::string(1, t.x < 0 ? cfg_.quickLeft : cfg_.quickRight), lists = {"quick"}, minLen = maxLen = 1;
    else {
        // (totdrec: Practice Your Misses: the creatures' words, a fireball's)
        const bool creature = t.cls == "tough" || t.cls == "captor" || t.cls == "rescue scene" || t.cls == "fireball" ||
                              t.cls.rfind("zombie", 0) == 0;
        if (creature && practiceOn()) ++practicePicks;
        if (creature && textPick(t.obj, s))   // (totdpacks: a text in order: its next piece, whatever its length)
            t.cls += " text", lists = {"text"};
        else if (!(creature && practicePick(lists, minLen, maxLen, t.obj, s)))
            pick(lists, minLen, maxLen, t.obj, s);
    }
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

// totdex: Tod.exe's key costs (Tenome's fit of the word files' difficulty numbers, 99.9 % of them: re/NOTES.md
// "Difficulty model"): a s t h 3, most letters 4, c m n x z 5, p q 6, v 8, space 2, digits 10, symbols 13, a capital
// +4 when case counts; in letters (/ 4). factor: Tod.exe's rank scales a word under 20 by 1.1 (stricter), from 61 by
// 0.8 (easier): the WPM's factor 1 / that
float Typing::keyEffort(const std::string& s, bool caseCounts, float& factor)
{
    int d = 0;
    for (const char ch : s) {
        const unsigned char c = uint8_t(ch);
        const char l = low(c);
        if (c == ' ') d += 2;
        else if (std::isdigit(c)) d += 10;
        else if (l >= 'a' && l <= 'z') {
            d += std::strchr("asth", l) ? 3 : std::strchr("cmnxz", l) ? 5 : std::strchr("pq", l) ? 6 : l == 'v' ? 8 : 4;
            if (caseCounts && std::isupper(c)) d += 4;
        } else d += 13;
    }
    factor = d < 20 ? 1.0f / 1.1f : d >= 61 ? 1.0f / 0.8f : 1.0f;
    return float(d) / 4.0f;
}

void TypingRecord::add(const TypingStats& now, TypingStats& last)
{
    const auto diff = [](uint32_t a, uint32_t b) { return a >= b ? a - b : a; };   // (a new game's stats: all of it)
    const bool fresh = now.letters < last.letters || now.words < last.words || now.typingMs < last.typingMs;
    const TypingStats zero;
    const TypingStats& l = fresh ? zero : last;
    letters += diff(now.letters, l.letters);
    misses += diff(now.misses, l.misses);
    words += diff(now.words, l.words);
    typingMs += std::max(0.0, now.typingMs - l.typingMs);
    for (int i = 0; i < 128; ++i) keyMiss[i] += diff(now.keyMiss[i], l.keyMiss[i]);
    for (const auto& [w, n] : now.wordMiss) {
        const auto it = l.wordMiss.find(w);
        const uint32_t d = diff(n, it == l.wordMiss.end() ? 0 : it->second);
        if (d) wordMiss[w] += d;
    }
    last = now;
}

bool TypingRecord::load(const std::string& path)
{
    std::ifstream f(path);
    if (!f) return false;
    for (std::string line; std::getline(f, line);) {
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("word\t", 0) == 0) {   // word<TAB>prompt<TAB>misses
            const size_t tab = line.rfind('\t');
            if (tab > 5) wordMiss[line.substr(5, tab - 5)] += uint32_t(std::strtoul(line.c_str() + tab + 1, nullptr, 10));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        const uint32_t n = uint32_t(std::strtoul(v.c_str(), nullptr, 10));
        if (k == "games") games = n;
        else if (k == "letters") letters = n;
        else if (k == "misses") misses = n;
        else if (k == "words") words = n;
        else if (k == "typing_ms") typingMs = std::atof(v.c_str());
        else if (k.size() == 9 && k.rfind("key_miss_", 0) == 0) {}   // (an empty key name)
        else if (k.size() == 10 && k.rfind("key_miss_", 0) == 0) keyMiss[uint8_t(k[9]) & 127] = n;
    }
    return true;
}

bool TypingRecord::save(const std::string& path) const
{
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << "# The Typing of the Dead 0: your typing over all games (Options > Gameplay > Typing of the Dead 0 > Statistics)\n";
    f << "games=" << games << "\nletters=" << letters << "\nmisses=" << misses << "\nwords=" << words << "\ntyping_ms="
      << uint64_t(typingMs) << "\n";
    for (int i = 33; i < 127; ++i)
        if (keyMiss[i] && i != '=' && i != '#') f << "key_miss_" << char(i) << "=" << keyMiss[i] << "\n";
    // the 200 most missed prompts (the file stays small)
    std::vector<std::pair<uint32_t, std::string>> w;
    for (const auto& [s, n] : wordMiss) w.push_back({n, s});
    std::sort(w.begin(), w.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    for (size_t i = 0; i < w.size() && i < 200; ++i)
        if (w[i].second.find('\t') == std::string::npos && w[i].second.find('\n') == std::string::npos)
            f << "word\t" << w[i].second << "\t" << w[i].first << "\n";
    return bool(f);
}

std::string TypingRecord::topKeys(int n) const
{
    std::vector<std::pair<uint32_t, int>> k;
    for (int i = 33; i < 127; ++i)
        if (keyMiss[i]) k.push_back({keyMiss[i], i});
    std::sort(k.begin(), k.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    std::string s;
    for (size_t i = 0; i < k.size() && int(i) < n; ++i)
        s += (s.empty() ? "" : ", ") + std::string(1, char(std::toupper(k[i].second))) + " " + std::to_string(k[i].first);
    return s;
}

std::string TypingRecord::topWords(int n) const
{
    std::vector<std::pair<uint32_t, std::string>> w;
    for (const auto& [s, c] : wordMiss) w.push_back({c, s});
    std::sort(w.begin(), w.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    std::string s;
    for (size_t i = 0; i < w.size() && int(i) < n; ++i)
        s += (s.empty() ? "" : ", ") + w[i].second + " " + std::to_string(w[i].first);
    return s;
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
    // totdex (SEGA's word level): creature words typed since the last hit raise it again, one level every
    // word_level_words
    if (wordLevel < 0 && t.kind == 0 && !t.quick && ++levelRun_ >= cfg_.wordLevelWords)
        ++wordLevel, levelRun_ = 0;
    char g = 0;
    if (t.kind == 2 || t.left == 0) {
        const double from = std::max(t.bornMs, freeSinceMs_);
        const double gms = std::max(50.0, nowMs_ - from);
        const float acc = t.lettersAll + t.missesAll ? 100.0f * float(t.lettersAll) / float(t.lettersAll + t.missesAll) : 100.0f;
        // (totd5, the feel report: a thrown axe typed at once graded C: one letter is no measure of speed; a small one
        // or a thrown thing counts as 3 letters: S within 0.7 s, A within 1 s)
        float n = float(t.kind == 1 || t.small ? std::max<uint32_t>(t.lettersAll, 3) : t.lettersAll);
        float factor = 1.0f;
        // totdex (SEGA's rank, Tod.exe's word finish: the time against the word's key costs): the letters as key
        // costs (v slower than a, digits and symbols slow), short words a little stricter, long ones easier; a tough
        // one's earlier prompts count by their letters
        if (cfg_.keyCostGrade && !t.quick && t.lettersAll > 0) {
            const float e = keyEffort(t.text, caseSensitive(), factor);
            n = std::max(n - float(t.text.size()) + e, t.kind == 1 || t.small ? 3.0f : 1.0f);
        }
        const float wpm = n / 5.0f / float(gms / 60000.0) * factor;
        g = killGrade(acc, wpm);
        for (TypingStats* s : {&game, &chapter})
            ++s->killGrade[gradeIndex(g)];
        // totdex (Tod.exe's speed bonus by grade): the kill's rank points
        if (const int gi = gradeIndex(g); gi < int(cfg_.rankPoints.size()) && cfg_.rankPoints[size_t(gi)] > 0)
            rt.mem.at<Player>(G::players, 0).score += cfg_.rankPoints[size_t(gi)];
        Popup p;
        p.x = t.x, p.y = t.y, p.grade = g, p.frame = frame_, p.boss = t.kind == 2;
        popups.push_back(p);
        t.lettersAll = t.missesAll = 0;
        t.bornMs = nowMs_;
    }
    freeSinceMs_ = nowMs_;
    sound(rt, "ding");   // (totd4: the word done)
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
        // totd3 (user 2026-11-03: "no boss fight works"): the shots of a sentence only counted when they landed: a
        // sentence typed while the Hangedman flew off the screen or the Chariot turned did nothing, and the hit points
        // per shot vary by boss (the Hangedman took 17 sentences at 60 WPM, a slower typist was beaten first). Now a
        // part with hit points loses 1 / boss_sentences of them per sentence at once, wherever it is; one shot follows
        // for its hit reaction (its damage taken back in after()), the last sentence's shots kill it (hit points 1)
        const int16_t hp = rt.mem.at<int16_t>(t.obj + 0x70), hpMax = rt.mem.at<int16_t>(t.obj + 0x72);
        if (hpMax > 0 && hp > 0 && hp <= hpMax && int32_t(rt.mem.u32(t.obj + 8)) == t.serial) {
            const int n = bossSentencesFor(rt, t.obj);
            const int dmg = (int(hpMax) + n - 1) / n;
            t.bossGoal = std::max(0, int(hp) - dmg - (int(hp) - dmg < dmg / 3 ? dmg : 0));   // (a sliver left: this one kills)
            rt.mem.at<int16_t>(t.obj + 0x70) = int16_t(std::max(1, t.bossGoal));
            t.bossShotsLeft += t.bossGoal > 0 ? 1 : cfg_.bossShots;
            if (trace_)
                std::printf("frame %llu: TOTD boss %X sentence: hp %d -> %d (of %d, %d sentences)\n", (unsigned long long)frame_, t.obj, hp, t.bossGoal, hpMax, n);
        } else {
            // totdex (found while checking SEGA's rules: Chapter 1's Chariot never died in some runs, base build too,
            // Hard 75 WPM seed 2; Normal 55 seed 1 with recent_words): its sentences sat on its hit box (no hit points
            // of its own), whose shots stopped landing once the armour broke, so the frame's bar (480) stayed at 320 /
            // 144 and the event waited for ever. The boss part with a bar on the screen takes the sentence instead (the
            // rule above), and the shots go at it
            uint32_t alt = 0;
            for (uint32_t o : rt.shootable) {
                const int k = classify_->targetKind(rt, o);
                const int16_t h = rt.mem.at<int16_t>(o + 0x70), hm = rt.mem.at<int16_t>(o + 0x72);
                if (o != t.obj && (k == 2 || k == 4) && hm > 0 && h > 0 && h <= hm) { alt = o; break; }
            }
            if (alt) {
                const int16_t ahp = rt.mem.at<int16_t>(alt + 0x70), amax = rt.mem.at<int16_t>(alt + 0x72);
                const int n = bossSentencesFor(rt, alt);
                const int dmg = (int(amax) + n - 1) / n;
                const int goal = std::max(0, int(ahp) - dmg - (int(ahp) - dmg < dmg / 3 ? dmg : 0));
                rt.mem.at<int16_t>(alt + 0x70) = int16_t(std::max(1, goal));
                altGoal_[alt] = goal;
                t.bossAlt = alt, t.bossAltSerial = int32_t(rt.mem.u32(alt + 8));
                t.bossShotsLeft += goal > 0 ? 1 : cfg_.bossShots;
                t.bossGoal = goal > 0 ? -1 : 0;   // (0: the killing shots go on until they land, below)
                if (trace_)
                    std::printf("frame %llu: TOTD boss %X sentence on %X: hp %d -> %d (of %d, %d sentences)\n", (unsigned long long)frame_, t.obj,
                                alt, ahp, goal, amax, n);
            } else
                t.bossShotsLeft += cfg_.bossShots;
        }
        // totd1: an attack whose shots did no damage (the boss turning away, in its own attack or hit reaction: the
        // Chariot took 25 sentences instead of 7) goes on, a shot every boss_shot_gap, until one does (3 s at most)
        bossLanded_ = false;
        // (totd8: a missed window's strike stays owed until the boss strikes: its strike now waits for the miss
        // (holdsBoss) and may begin only seconds later, after this sentence; up to totd7 typing this one forgave it)
        lastBossFinish_ = frame_;
        // totd5 (the feel report: "the boss is a non-stop wall of sentences; TotD: a sentence about every 8 s, then
        // seconds of boss action"): no sentence for boss_rest seconds (its hits held, its window not running)
        bossRestUntil_ = frame_ + ticks(cfg_.bossRest[difficulty_]);
        // (totd3: the reaction shot 1 s at most; the killing one until it lands, 20 s: the boss back on the screen)
        t.bossPendingUntil = frame_ + (t.bossGoal > 0 ? 60 : t.bossGoal == 0 ? 1200 : 180);
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
        // totd5 (user 2026-11-04: "a destroyed / typed projectile can never damage the player"): typed to its end, it
        // never hits (holdsHit / holdsProjectile: its routine's hit is held), whether its shot lands or not
        neutral_[t.obj] = {t.serial, frame_ + 900};
    }
    if (t.kind == 3) {   // totd5: an item / a prop / a lock typed: followed 4 s (broken, taken: its routine changes or it goes)
        for (TypingStats* s : {&game, &chapter})
            ++s->items[1];
        follow_.push_back({t.obj, t.serial, rt.mem.u32(t.obj), rt.mem.at<int16_t>(t.obj + 0x5C), frame_ + 240, hits, 3});
    }
    if (!t.holding)   // (the hit points to put back if the killing shot never lands)
        t.realHp = rt.mem.at<int16_t>(t.obj + 0x70);
    t.finishShots = 0;
    t.strikes = 0;
    t.nextStrike = 0;
    // (an item: a shot or three picks it up, it has no hit points; a prop / a lock: a shot every 6 frames until it
    // breaks: some take a few, totd2). totd5: the creatures' and thrown things' one shot, then direct hits (strike)
    // until they go: 1.5 s for a small one / a thrown thing, 3 s for the others (behind something, the camera moving)
    t.finishing = t.kind == 3 ? 12 : t.kind == 1 || t.small ? 90 : 180;
    finishMembers(rt, t);
}

// totd7: a group member's leader (its target, with its word up and seen), else null
Typing::Target* Typing::leaderOf(const Target& t)
{
    if (!t.leader)
        return nullptr;
    const auto it = targets_.find(t.leader);
    if (it == targets_.end() || it->second.leader || (it->second.text.empty() && it->second.finishing == 0))
        return nullptr;
    return &it->second;
}

// totd7 (user 2026-11-05: "one word or phrase shot all of them"): the group's word done: each member goes as the
// leader does (its own killing shot or hit, its hit held meanwhile)
void Typing::finishMembers(Runtime& rt, Target& leader)
{
    for (auto& [o, m] : targets_) {
        if (m.leader != leader.obj || m.finishing)
            continue;
        if (!m.holding)
            m.realHp = rt.mem.at<int16_t>(o + 0x70);
        m.finishShots = 0, m.strikes = 0, m.nextStrike = 0;
        m.finishing = m.kind == 3 ? 12 : m.kind == 1 || m.small ? 90 : 180;
        m.text = leader.text, m.typed = m.text.size();   // (done with the leader's word: no word of its own later)
        if (m.kind == 1 || m.small)
            neutral_[o] = {m.serial, frame_ + 900};
        ++groupKills;
        if (trace_)
            std::printf("frame %llu: TOTD group %X goes with %X\n", (unsigned long long)frame_, o, leader.obj);
    }
}

// totd7 (user 2026-11-05: "every typed letter fires the gun again with its normal shot sound"): the arcade gun's own
// shot sound (player 1's code, the board or its PC cue), straight to the sound (the game's memory never sees it)
void Typing::shotSound(Runtime& rt)
{
    static const bool off = std::getenv("HOTD_TOTD_NOSHOT") != nullptr;
    if (!cfg_.sounds || !cfg_.shotSound || !rt.audio || off)   // (HOTD_TOTD_NOSHOT, testing: the loudness measure)
        return;
    rt.audio->cue(cfg_.shotSound);
}

// totd5 (user 2026-11-04: "the frogs get a barrage of bullets: one typed letter = one shot, never bursts"): a word's
// killing shot that did not land (a hopping frog, the aim a frame behind, a creature behind a pillar) is not fired
// again and again (totd4: up to 40 shots, one a frame); its hit is given as a landed shot's: a thrown thing is broken
// (a kit's hits left 0: its own shot-down routine next frame; others the hit bits, hit points 1), a creature's biggest
// damaging body part takes player 1's hit (partHit; its own enemy_part_hits next frame, hit points 1: the kill), a
// small one without a body the hit bits (flags 0x102, as totd1's "hit given")
void Typing::strike(Runtime& rt, Target& t)
{
    auto& mem = rt.mem;
    if (mem.u32(t.obj) == 0 || int32_t(mem.u32(t.obj + 8)) != t.serial)
        return;
    ++t.strikes;
    const std::string n = nameOf(rt, mem.u32(t.obj));
    bool body = false;
    if (t.kind == 1 && (n == "flying_axe_frame" || n == "held_object_flight" || n == "held_object_throw_flight")) {
        mem.at<int16_t>(t.obj + 0x70) = 0;   // (held_object_shot_check: no hits left = destroyed)
    } else if (n == "iron_ball_follow") {
        mem.at<uint32_t>(t.obj + 0x58) |= 0x102u;   // (totd6: the iron ball: its spark; its hit is held, neutral_)
    } else {
        if (t.kind == 0 && mem.at<int16_t>(t.obj + 0x72) > 0 && mem.at<int16_t>(t.obj + 0x70) > 1)
            mem.at<int16_t>(t.obj + 0x70) = 1, t.holding = false;
        else if (t.kind == 1 && mem.at<int16_t>(t.obj + 0x70) > 1)
            mem.at<int16_t>(t.obj + 0x70) = 1;
        body = t.kind == 0 && partHit(rt, t.obj, 0);
        if (!body)
            mem.at<uint32_t>(t.obj + 0x58) |= 0x102u;
    }
    if (trace_)
        std::printf("frame %llu: TOTD strike %X %s (%s, strike %d, hp %d)\n", (unsigned long long)frame_, t.obj, n.c_str(),
                    body ? "body part" : "flags", t.strikes, mem.at<int16_t>(t.obj + 0x70));
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
    auto it = targets_.find(obj);
    if (it != targets_.end() && it->second.leader && targets_.count(it->second.leader))   // (totd7: a group member: its
        it = targets_.find(it->second.leader);                                            // group's word and grace)
    if (it == targets_.end() || it->second.text.empty())   // no word (off the screen, not in the fight yet): it waits
        return true;
    const Target& t = it->second;
    // totd5 (the feel report 2026-11-05: "zombies freeze in front of you while held: TotD's keep moving and winding
    // up"): attack_hold 2 lets a creature with a word begin its attack (the wind-up, the swing); only its hit waits on
    // its grace (holdsHit: the attacker's own timer). attack_hold 1 (totd2's window): the attack itself waits
    // totd7 (user 2026-11-05: "the animations where they hit you do not match: animation-wise they hit you but you
    // don't take damage because you still have time to type it out"): attack_hold 2 no longer lets the attack begin
    // inside the grace (totd5 held only its hit, so the swing / bite played and nothing happened). The creature walks
    // up and stands at you (its own idle / step while its attack choice is refused); once its grace is over (and
    // attack_gap after the last attack began) its attack begins on its own timing and its hit lands with it (graceHolds
    // lets it through): what you see hit you, hits you. Typed first, it dies before any strike
    static const bool old6 = std::getenv("HOTD_TOTD_HOLD6") != nullptr;   // (testing: round 6's rule, the comparison)
    if (cfg_.attackHold == 2 && old6)
        return t.finishing > 0;
    // totdrec (the user's recording 2026-11-05, 14:33-14:34 and 15:12-15:13: an agile one went off the picture's edge,
    // its box with it, then leapt in from there and bit before a word could be read; Chapter 2 headless, Normal 55: a
    // kind 70 on a wall out of the picture, its box gone 12 frames, struck, hit 32 frames later): with its box off the
    // screen it waits as one with no word, up to offscreen_hold seconds since its box went (an event never waits on it
    // for ever)
    static const bool oldFresh = std::getenv("HOTD_TOTD_OLDFRESH") != nullptr;   // (testing: before totdrec)
    if (cfg_.attackHold == 2 && !oldFresh && t.finishing == 0 && boxUpFor(it->first) == 0) {
        const auto sh = shown_.find(it->first);
        const uint64_t went = sh != shown_.end() && sh->second.serial == t.serial ? sh->second.last : t.born;
        if (frame_ < went + ticks(cfg_.offscreenHold)) {
            ++offscreenHeld;
            return true;
        }
    }
    if (cfg_.attackHold == 2)
        return t.finishing > 0 || t.readyAt == 0 || frame_ < t.readyAt ||
               (lastAttack_ && frame_ < lastAttack_ + ticks(cfg_.attackGap[difficulty_]));
    if (lastAttack_ && frame_ < lastAttack_ + ticks(cfg_.attackGap[difficulty_]))
        return true;
    return t.finishing > 0 || t.readyAt == 0 || frame_ < t.readyAt;
}

// totdrec: frames in a row a target's box has been on the screen up to this tick (0: not up now). The boxes of a tick
// are made in before(); the attack / hit hooks ask during the tick, so a box up now was last drawn at frame_
uint64_t Typing::boxUpFor(uint32_t obj) const
{
    const auto t = targets_.find(obj);
    if (t != targets_.end() && t->second.leader && targets_.count(t->second.leader))
        obj = t->second.leader;
    const auto sh = shown_.find(obj);
    if (sh == shown_.end() || sh->second.last + 1 < frame_ || (rt_ && int32_t(rt_->mem.u32(obj + 8)) != sh->second.serial))
        return 0;
    return frame_ - sh->second.since + 1;
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
        // totd7 (user 2026-11-05: "you still can't save the two locked up researchers"): Chapter 1's keypad cell (event
        // 6): its creature is at the researchers behind the bars, with no word (not shootable) until the keypad is
        // shot; its bite waits while a lock's key is up or being taken down (the rescue cannot begin before the door)
        for (const auto& [o2, l] : targets_)
            held = held || (l.kind == 3 && !l.text.empty() && l.lastSeen + 2 >= frame_ &&
                            nameOf(*rt_, rt_->mem.u32(o2)).find("lock") != std::string::npos);
    } else
        held = it->second.finishing > 0 || frame_ < it->second.preyReadyAt;
    // totd7 (user 2026-11-05: "scripted events should be the same speed, like the researchers in the beginning being
    // pushed by the two creatures"): rescue_wait 0 (Normal and up): no rescue timer at all, the arcade's own pace (a
    // captor typed is held: it is going down); only a lock's key up still holds the cell's creature
    if (cfg_.rescueWait[difficulty_] <= 0.0f && !(it != targets_.end() && it->second.finishing > 0)) {
        held = false;
        if (it == targets_.end() || it->second.text.empty())
            for (const auto& [o2, l] : targets_)
                held = held || (l.kind == 3 && !l.text.empty() && l.lastSeen + 2 >= frame_ &&
                                nameOf(*rt_, rt_->mem.u32(o2)).find("lock") != std::string::npos);
    }
    // (held at the researcher: it does not take this tick's step either, after(); walking on into him it pushed him
    // aside, and Chapter 1's first researcher, freed late, then ran round his goal for ever: the event never went on)
    if (held && !preyHeldAt_.count(obj))
        preyHeldAt_[obj] = {rt_->mem.f32(obj + 0x20), rt_->mem.f32(obj + 0x28)};
    return held;
}

// totd8 (the coordinator 2026-11-05, the user's point of round 7 carried to the bosses: "the animations where they hit
// you do not match"): round 7 let a boss strike on its own timing and held its hit until a sentence's window ran out,
// so between sentences the Chariot's swings, the Hermit's claws, the Hangedman's claw runs and swoops, the Magician's
// lunges played and nothing happened. Now the strike itself waits (port::gBossHold, asked where each boss begins one;
// held, the boss idles, poses, hovers or swoops on): it begins once a window ran out (bossPunish_) and its hit lands
// (graceHolds: the striker's hits for boss_strike_seconds or until one landed). A boss whose sentence has not been seen
// for 2 s (off the screen, out of the shootable list: nothing to type) is not held, so no fight can wait for ever;
// its hit stays held (graceHolds)
bool Typing::holdsBoss(uint32_t obj)
{
    if (!rt_)
        return false;
    if (obj == bossStriker_ && frame_ < bossStrikeUntil_)
        return false;   // (its strike under way: every step of it asks again)
    // (and a boss whose last sentence was typed, going down: Chapter 4's Hangedman, at 0 over the floor, lives on and
    // dies only to a hit in his claw run: held, he hovered 20 s longer)
    // (a boss not seen yet, its first sentence coming: held up to 4 s from its first ask)
    const auto it = targets_.find(obj);
    if (bossSeenAt_ && bossSeenAt_ + 30 >= frame_)
        bossUnseenAsk_ = 0;
    else if (!bossUnseenAsk_)
        bossUnseenAsk_ = frame_;
    if ((bossUnseenAsk_ && frame_ > bossUnseenAsk_ + 240) || (it != targets_.end() && it->second.bossGoal == 0)) {
        ++bossUnheld;
        return false;
    }
    // totd8 (user 2026-11-05: "the Magician is just flying around and not even trying to attack"; the PC TotD: its
    // bosses attack between sentences and a short word blocks them): in the rest after a sentence a boss strikes at its
    // own pace (boss_strike_gap seconds apart at most); its next sentence comes at once, and its first
    // boss_block_letters letters typed before the hit block the strike (the boss staggers: a shot, no damage), else it
    // lands. The sentence comes boss_block_warn seconds before the strike begins (the measure: the Chariot's swing hits
    // 0.47 s after it begins: no typist blocked it); blocked already, it still begins (seen) and the boss staggers 12
    // frames into it
    if (warnUntil_ && blockObj_ == obj) {
        if (frame_ < warnUntil_) {
            ++bossHeldFrames;
            return true;
        }
        warnUntil_ = 0;
        if (strikeBlocked_)
            staggerAt_ = frame_ + 12;
        ++bossFreeStrikes;
    } else {
        // (totd8: not while a thrown thing with a word is up: the Magician's fireballs and his lunge at once cost a 60 WPM
        // typist two lives a volley)
        bool thrown = false;
        for (const auto& [o2, u] : targets_)
            thrown = thrown || (u.kind == 1 && !u.text.empty() && u.finishing == 0 && u.lastSeen + 2 >= frame_);
        const bool free = !bossPunish_ && !thrown && frame_ < bossRestUntil_ && frame_ >= nextFreeStrike_ && cfg_.bossBlockLetters[difficulty_] > 0;
        if (!bossPunish_ && !free) {
            ++bossHeldFrames;
            return true;
        }
        if (free) {
            nextFreeStrike_ = frame_ + ticks(cfg_.bossStrikeGap[difficulty_]);
            bossRestUntil_ = frame_;   // (its sentence now: the block)
            blockObj_ = obj, blockFrom_ = frame_, strikeBlocked_ = false;
            warnUntil_ = frame_ + std::max<uint64_t>(1, ticks(cfg_.bossBlockWarn[difficulty_]));
            ++bossHeldFrames;
            return true;
        }
        blockObj_ = 0;
    }
    bossStriker_ = obj;
    bossStrikeUntil_ = frame_ + ticks(cfg_.bossStrikeSeconds);
    ++bossStrikes;
    if (trace_)
        std::printf("frame %llu: TOTD boss strike let go %X %s\n", (unsigned long long)frame_, obj, nameOf(*rt_, rt_->mem.u32(obj)).c_str());
    return false;
}

// the hit hold (port::gHitHold, player_hit): in a boss fight a hit on player 1 lands only after a missed sentence
// window (totd1); elsewhere (totd2, the self-test's 40 WPM typist took 16-18 hits a chapter from attacks that never ask
// gAttackHold: the agile kind 70's, the vomit, the taran, a standard enemy's attack already begun) only while a creature
// whose attack timer ran out is there. Thrown things hit through projectile_hit_player, not here: they carry prompts
bool Typing::holdsHit(int player) const
{
    if (!rt_ || player != 0)
        return false;
    // totd5 (user 2026-11-04: "typed axes and barrels still hurt"): a thing typed to its end (a thrown axe, a knife, a
    // frog's leap: the routine hitting now, the current task) never hits
    if (harmlessNow())
        return true;
    return graceHolds("player_hit");
}

// a hit on player 1 by a creature (player_hit, or a creature's own projectile_hit_player: the iron ball, a Murrer's
// leap): held unless a sentence's window ran out (a boss fight) or a creature's grace ran out (attack_hold 2)
bool Typing::graceHolds(const char* via) const
{
    const auto allowed = [&]() { noteHit(via); return false; };
    if (!cfg_.attackHold)
        return allowed();
    // totd8 (the coordinator 2026-11-05: "between boss sentences a boss's own strikes still play with no damage: hold
    // the strike animation itself"): a boss's strike begins only once a sentence's window ran out (holdsBoss, asked
    // where each boss begins one: port::gBossHold); that strike's hit lands, whatever is typed meanwhile
    const uint32_t task = rt_ ? rt_->mem.u32(G::task_current) : 0;
    if (task && task == bossStriker_ && frame_ < bossStrikeUntil_) {
        if (strikeBlocked_ && blockObj_ == task) {   // (totd8: blocked by its sentence's first letters: no hit)
            ++blockedHits;
            return true;
        }
        strikeLanded_ = true;
        return allowed();
    }
    // (totd8: the boss's own hits and the Magician's barrage; the creatures in a boss fight, the Hangedman's bats, go by
    // their own grace below as anywhere else: up to totd7 a bat's strike there showed with no damage)
    const bool own = rt_ && (classify_->targetKind(*rt_, task) == 2 || (targets_.count(task) && targets_.at(task).kind == 2));
    if (bossFight_ && (own || !rt_ || rt_->mem.u32(task) == 0x74150 || !targets_.count(task))) {
        if (!bossPunish_) ++(own ? heldBoss : heldInFight);
        if (trace_ && !bossPunish_)
            std::printf("frame %llu: TOTD held boss-fight hit %X %s (%s)\n", (unsigned long long)frame_, task,
                        rt_ ? nameOf(*rt_, rt_->mem.u32(task)).c_str() : "", via);
        return bossPunish_ ? allowed() : true;   // (totd3: kept: on their own timing the Hangedman landed 9 hits on a 60 WPM typist)
    }
    if (cfg_.attackHold == 2 && rt_) {
        // totd5: the attacker's own grace (the current task: the creature whose swing lands now): its hit lands once
        // its grace from its word shown ran out and attack_gap after the last hit; a creature without a word (off the
        // screen) never hits; anything else (a scripted scene's hit) as the arcade
        auto it = targets_.find(task);
        if (it != targets_.end() && it->second.leader && targets_.count(it->second.leader))   // (totd7: a group member:
            it = targets_.find(it->second.leader);                                            // its group's grace)
        // totd6: a boss's own hit (the Hermit off the screen or on a wall, the Hangedman's swoop, a barrage shot: no
        // sentence seen that tick, so totd5 let 4 hits a chapter through) lands only after a missed sentence window,
        // as in its fight
        if ((it != targets_.end() && it->second.kind == 2) || rt_->mem.u32(task) == 0x74150 || classify_->targetKind(*rt_, task) == 2) {
            if (!bossPunish_) ++heldBoss;
            if (trace_ && !bossPunish_)
                std::printf("frame %llu: TOTD held boss hit %X %s, unseen (%s)\n", (unsigned long long)frame_, task,
                            nameOf(*rt_, rt_->mem.u32(task)).c_str(), via);
            return bossPunish_ ? allowed() : true;
        }
        if (it != targets_.end() && it->second.kind == 0) {
            const Target& t = it->second;
            // totd7: its attack began only once its grace was over (holds()), so its hit lands as it shows; the few
            // that never ask (the agile kind 70, the Ebitans' vomit, the taran) strike on their own timing and hit too
            // (round 6 held those hits: the creature visibly struck and nothing happened). Typed to its end: it is going
            // down (no hit). HOTD_TOTD_HOLD6: round 6's rule
            static const bool old6 = std::getenv("HOTD_TOTD_HOLD6") != nullptr;
            if (t.finishing > 0 || (old6 && (t.text.empty() || !t.readyAt || frame_ < t.readyAt)) || (!old6 && t.text.empty())) {
                // (totd7, the report: a creature's strike that hit nothing: its word still up, not typed)
                if (t.finishing == 0) {
                    ++heldCreature;
                    if (trace_)
                        std::printf("frame %llu: TOTD held strike %X %s (grace %lld ticks left)\n", (unsigned long long)frame_, task,
                                    nameOf(*rt_, rt_->mem.u32(task)).c_str(), t.readyAt ? (long long)t.readyAt - (long long)frame_ : -1LL);
                }
                return true;
            }
            // (totd7: no attack_gap here any more: an attack that began after its grace (holds()) lands as it shows)
            // totdrec (the user's recording 2026-11-05): ... but only once its box has been on the screen strike_seen
            // in a row: one that leapt in from outside the picture (its box new, or gone) bites for nothing (HOTD_TOTD_OLDFRESH:
            // before)
            static const bool oldFresh = std::getenv("HOTD_TOTD_OLDFRESH") != nullptr;
            if (!oldFresh && !t.small && boxUpFor(it->first) < ticks(cfg_.strikeSeen[difficulty_])) {
                ++heldFresh;
                if (trace_)
                    std::printf("frame %llu: TOTD held fresh strike %X %s (box up %llu frames)\n", (unsigned long long)frame_, task,
                                nameOf(*rt_, rt_->mem.u32(task)).c_str(), (unsigned long long)boxUpFor(it->first));
                return true;
            }
            return allowed();
        }
        return allowed();
    }
    for (const auto& [o, t] : targets_)
        if (t.kind == 0 && t.readyAt && t.readyAt <= frame_)
            return allowed();
    return true;
}

// totd5: the routine running now (the current task) is a thing typed to its end (Typing::neutral_)
bool Typing::harmlessNow() const
{
    if (!rt_)
        return false;
    const uint32_t task = rt_->mem.u32(G::task_current);
    const auto it = neutral_.find(task);
    return it != neutral_.end() && rt_->mem.u32(task) != 0 && int32_t(rt_->mem.u32(task + 8)) == it->second.first;
}

// totd5: a projectile at the screen (Runtime::projectile_hit_player): true = no hit (it was typed)
bool Typing::holdsProjectile() const
{
    if (harmlessNow()) {
        if (trace_)
            std::printf("frame %llu: TOTD typed %s reached the screen: no hit\n", (unsigned long long)frame_,
                        nameOf(*rt_, rt_->mem.u32(rt_->mem.u32(G::task_current))).c_str());
        ++blocked_;
        return true;
    }
    // (a thrown thing not typed: it hits; a creature's own reach (the iron ball swung at you, a Murrer's leap): the
    // grace as player_hit's, totd5: the iron ball's hits came through without one)
    const uint32_t task = rt_ ? rt_->mem.u32(G::task_current) : 0;
    if (!task || classify_->targetKind(*rt_, task) == 1) {
        noteHit("projectile_hit_player");
        return false;
    }
    return graceHolds("projectile_hit_player");
}

// (the report: which routine's hit got through, by name; a typed one's should never)
void Typing::noteHit(const char* via) const
{
    if (!rt_)
        return;
    const uint32_t task = rt_->mem.u32(G::task_current);
    std::string n = nameOf(*rt_, rt_->mem.u32(task));
    if (n.empty()) {
        char b[16];
        std::snprintf(b, sizeof b, "0x%X", rt_->mem.u32(task));
        n = b;
    }
    tickHitVia_ = n + " (" + via + ")";
    tickHitObj_ = task;
}

// totd6: an iron ball in its throw at the player (iron_ball_follow, its owner's motion 0x279 in the ball's extra
// record +0x37C, the ball's step +0x66 = 0x21: flying; iron_ball_pos_attack, port/ch3_kinds.cpp)
bool Typing::isBallThrow(Runtime& rt, uint32_t obj) const
{
    auto& mem = rt.mem;
    if (nameOf(rt, mem.u32(obj)) != "iron_ball_follow")
        return false;
    const uint32_t x = mem.u32(obj + 0x37C);
    return mem.mapped(x) && mem.at<int16_t>(x + 2) == 0x279 && mem.at<int16_t>(obj + 0x66) == 0x21;
}

// totd8 (user 2026-11-05: "the bosses are too easy; look at the video reference for health"; the PC TotD's bars,
// docs/typing.md): the sentences a boss part takes, boss_sentences by difficulty; the last boss (the Magician)
// boss_final_x as many (the reference's last bosses about 16 sentences, the others about 9)
int Typing::bossSentencesFor(Runtime& rt, uint32_t obj) const
{
    const int n = std::max(1, cfg_.bossSentences[difficulty_]);
    // (totd8, the user's pairs: the Hangedman as the reference's first boss (9 sentences), the Chariot as its second (10),
    // the Hermit the general rule, the Magician as its own (16): boss_x_<name> x boss_sentences)
    const std::string name = nameOf(rt, rt.mem.u32(obj));
    float x = rt.mem.u32(obj) == 0x71010 ? cfg_.bossFinalX : 1.0f;
    for (const auto& [k, v] : cfg_.bossX)
        if (name.find(k) != std::string::npos) x = v;
    return std::max(1, int(std::lround(float(n) * x)));
}

bool Typing::bodyCircles(Runtime& rt, uint32_t obj, std::vector<Circle>& out)
{
    out.clear();
    auto& mem = rt.mem;
    const uint32_t root = mem.u32(obj + 0xC8);
    if (!mem.mapped(root))
        return false;
    const std::function<void(uint32_t, int)> walk = [&](uint32_t part, int depth) {
        if (!mem.mapped(part) || depth > 16)
            return;
        const float r = mem.f32(part + 0x74), z = mem.f32(part + 0x64);
        if (mem.u32(part) != 0 && r > 0.0f && z > 1.0f)
            out.push_back({mem.f32(part + 0x5C) * 496.0f / z, mem.f32(part + 0x60) * 496.0f / z, r * 496.0f / z});
        const uint32_t k = mem.u32(part + 0x34);
        for (uint32_t i = 0; i < k && i < 64; ++i)
            walk(mem.u32(mem.u32(part + 0x24) + 4 * i), depth + 1);
    };
    const uint32_t k = mem.u32(root + 0x34);
    for (uint32_t i = 0; i < k && i < 64; ++i)
        walk(mem.u32(mem.u32(root + 0x24) + 4 * i), 0);
    return !out.empty();
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
    port::gBossHold = nullptr;
    gTotdProjectileHold = nullptr;
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
        shown_.clear();
        lock_ = 0;
        idleSince_ = 0;
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
    // (totd5, the feel report: the RANK pop-ups lingered 1.25 s over the boxes: 0.75 s)
    std::erase_if(popups, [&](const Popup& p) { return frame_ > p.frame + 45; });
    for (auto it = killed_.begin(); it != killed_.end();)
        it = frame_ > it->second.second ? killed_.erase(it) : std::next(it);
    for (auto it = neutral_.begin(); it != neutral_.end();)   // (totd5: typed things, harmless while they last;
        it = frame_ > it->second.second || mem.u32(it->first) == 0 || int32_t(mem.u32(it->first + 8)) != it->second.first ||
                     (nameOf(rt, mem.u32(it->first)) == "iron_ball_follow" && !isBallThrow(rt, it->first))   // totd6: an iron
                 ? neutral_.erase(it) : std::next(it);                                                      // ball: its throw)
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
    bool creatureWords = false;   // (totd5: a creature's word up: the props wait)
    for (const auto& [o, t] : targets_)
        creatureWords = creatureWords || (t.kind == 0 && !t.text.empty() && !t.autoShot && t.lastSeen + 2 >= frame_);
    for (uint32_t obj : rt.shootable) {
        const int k = classify_->targetKind(rt, obj);
        int kind = -1;
        float x = 0, y = 0, th = 0;
        bool dmg = false;
        // totd6 (user 2026-11-05: "the Magician's barrage projectiles get words"): a barrage shot (0x74150) gets a short
        // word once it leaves its circle round him (step +0x60 >= 3: up, waiting, flying at you), at most barrage_words
        // at a time (dozens fly, 500-700 a fight; the rest have none: in a boss fight their hits wait on a missed
        // sentence anyway, holdsHit). totd2-5: none
        if (k == 1 && mem.u32(obj) == 0x74150 && !targets_.count(obj)) {
            int up = 0;
            for (const auto& [o2, t2] : targets_)
                up += (t2.routine == 0x74150 || mem.u32(o2) == 0x74150) && t2.finishing == 0 && (t2.lastSeen + 2 >= frame_ || t2.born + 1 >= frame_);   // (totd8: a shot just given one counts: 11 at once)
            if (mem.at<int16_t>(obj + 0x60) < 3 || up >= cfg_.barrageWords[difficulty_])
                continue;
        }
        if (k == 2 && frame_ < bossDownUntil_)   // (totd3: a boss just killed: no new sentences)
            continue;
        // totd6 (user 2026-11-05: "the iron ball gets its own word"): Chapter 3 / 4's chain zombies throw their ball at
        // you (iron_ball_pos_attack: the owner's motion 0x279, the ball's step 0x21 in flight; its hit through
        // projectile_hit_player from the ball's own task). The ball is a decoy to the CPU teammates (shots spark); in
        // its throw it is a thrown thing here: a word, slowed as one, typed = its hit held (neutral_ for this flight)
        if (k == 4 && isBallThrow(rt, obj) && aimPoint(rt, obj, 0, x, y)) {
            kind = 1;
            th = 900.0f;
            dmg = true;
        } else if (k == 0 || k == 1 || k == 2) {
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
            // totd5 (user 2026-11-04: "the locked researcher where you shoot the lock: typing must do it"): a cell's lock
            // gets its word only once the game lets it be shot (kind 106's padlock, Chapter 3's cell: from flag 0x51E970,
            // then 6 s; kind 77, Chapter 1's cells: from its event flag; both step +0x60 = 2). totd2-4 gave the padlock its
            // word 200 units off, before it could be shot: typed for nothing, given up, its new word came late
            if (item && (n.find("padlock") != std::string::npos || n.find("kind77_lock") != std::string::npos) &&
                (mem.at<int16_t>(obj + 0x60) != 2 || (mem.u32(obj + 0x58) & 1) == 0))
                item = false;
            // totd5 (the feel report: "30 % of prompts are props (baskets, signs) next to live zombies"): a prop, a sign
            // or a route spot gets no new word while a creature's word is up (pickups and locks do: they go)
            // qa1 (the user's play: a "u" on a sign in a zombie fight; a corridor with "road", "seed", "nest" on the walls
            // and one over the second zombie's head): also while creatures are in the fight without a word yet, and a
            // prop's word given before they came is taken back (not locked, nothing typed: it gets a word again later).
            // A crate / barrel with an item inside waits too (it stays where it is; Chapter 1's corridor had three
            // crates' words round two zombies, one over a zombie's head); a pickup itself (a life, a coin) does not
            if (item && (creatureWords || creatures) && n.find("lock") == std::string::npos) {
                bool pickup = false;
                for (const std::string& p : cfg_.itemNames)
                    pickup = pickup || (!p.empty() && n.find(p) != std::string::npos);
                if (!pickup) {
                    if (auto had = targets_.find(obj); had == targets_.end())
                        item = false;
                    else if (lock_ != obj && had->second.typed == 0 && had->second.finishing == 0) {
                        targets_.erase(had);
                        item = false;
                    }
                }
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
            // totd3 (user 2026-11-03: "prompts on frogs and hanging Ebitans don't make sense"): no word; the layer
            // shoots them by itself a moment after they show (a frog dies at a shot; a hanging Ebitan falls dead)
            if (kind == 0) {
                const std::string n = nameOf(rt, mem.u32(obj));
                for (const std::string& p : cfg_.autoNames)
                    t.autoShot = t.autoShot || (!p.empty() && n.find(p) != std::string::npos);
                if (t.autoShot && cfg_.quickKeys)   // (totd7: the hanging Ebitans are quick ones: G / H, not shot by themselves)
                    t.autoShot = false, t.small = true;
                if (t.autoShot) {
                    if (trace_)
                        std::printf("frame %llu: TOTD auto %X %s (no prompt, shot by itself)\n", (unsigned long long)frame_, obj, n.c_str());
                    t.small = true;
                    t.nextAuto = frame_ + std::max<uint64_t>(1, ticks(cfg_.autoDelay));
                }
            }
            // totd7 (user 2026-11-05): the quick ones (thrown things, the small quick creatures, crates / barrels /
            // props, the rescue locks / keypads) take one key by screen side, never a word; a pickup (a coin, the gold
            // frog, a life: item_names) is collected by the layer pickup_delay after it shows (the crate's frog too)
            if (cfg_.quickKeys && (kind == 1 || t.small || kind == 3)) {
                t.quick = true;
                if (kind == 3) {
                    const std::string n = nameOf(rt, mem.u32(obj));
                    for (const std::string& p : cfg_.itemNames)
                        t.pickup = t.pickup || (!p.empty() && n.find(p) != std::string::npos);
                    if (t.pickup) {
                        t.quick = false, t.autoShot = true;
                        t.nextAuto = frame_ + std::max<uint64_t>(1, ticks(cfg_.pickupDelay));
                        if (trace_)
                            std::printf("frame %llu: TOTD pickup %X %s (collected by itself)\n", (unsigned long long)frame_, obj, n.c_str());
                    }
                }
            }
            // totd8 (the PC TotD's Magician, 36:20-39:21: every fireball he throws carries a word or a short phrase):
            // his fireballs a near zombie's word (fireball_words 1), not a G / H key
            if (kind == 1 && mem.u32(obj) == 0x73010 && cfg_.fireballWords)
                t.quick = false;
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
        if (t.autoShot) {   // (totd3: no prompt; its shot once it has been seen auto_delay)
            if (t.finishing == 0 && !t.landedAt && frame_ >= t.nextAuto) {
                t.finishing = t.kind == 3 ? 12 : kFinishTries;
                if (t.pickup && t.finishShots == 0 && t.strikes == 0)
                    ++pickupsAuto;
            }
            continue;
        }
        // totd5 (user 2026-11-04: "distant zombies that never attack must not get word boxes or hold up progress;
        // careful with creatures behind cover that do come"): a creature gets its word once it is in the fight by its
        // own state, not by distance alone: shootable now (flags bit 0; Chapter 1's route runner clears it), and at
        // you (an attack place +0x2E7), coming closer (the tracked speed), within far_depth, after a researcher, or in
        // a rescue scene. One standing far off gets none (holds() keeps it from attacking without a word anyway);
        // only when nothing else is up for idle_seconds while the game still counts enemies alive (the event may wait
        // on it) does it get one
        bool active = true;
        if (kind == 0 && !t.small && t.text.empty()) {
            // (kind 70 with +0x324 bit 3: its hits are not taken, kind70_frame: Chapter 1's route runner)
            const bool shootable = (mem.u32(obj + 0x58) & 1) != 0 &&
                                   !(nameOf(rt, mem.u32(obj)).rfind("kind70", 0) == 0 && (mem.at<uint8_t>(obj + 0x324) & 8));
            const bool engaged = mem.at<int8_t>(obj + 0x2E7) >= 0;
            const bool coming = classify_->approach(obj) > 0.05f;
            const bool within = mem.f32(obj + 0x40) < cfg_.farDepth;
            active = shootable && (engaged || coming || within || isCaptor(rt, obj) || peopleNow_ ||
                                   (idleSince_ && frame_ >= idleSince_ + ticks(cfg_.idleSeconds) && mem.at<uint16_t>(G::enemies_alive) > 0));
            if (!active && trace_ && frame_ % 120 == 0)
                std::printf("frame %llu: TOTD idle %X %s (z %.0f speed %.2f shootable %d): no word\n", (unsigned long long)frame_, obj,
                            nameOf(rt, mem.u32(obj)).c_str(), mem.f32(obj + 0x40), classify_->approach(obj), int(shootable));
        }
        // (a word up on one that cannot be shot any more, not begun: gone, as the route runner leaving)
        // (totd7, user 2026-11-05: "the G and H flicker": a flag that drops for a frame or two took the word away and a new
        // one came the next frame; now only after kNoShootFrames frames in a row)
        if (kind == 0 && !t.text.empty() && t.typed == 0 && !t.finishing && lock_ != obj &&
            ((mem.u32(obj + 0x58) & 1) == 0 || (nameOf(rt, mem.u32(obj)).rfind("kind70", 0) == 0 && (mem.at<uint8_t>(obj + 0x324) & 8)))) {
            if (!t.offSince)
                t.offSince = frame_;
            if (noLatch_ || frame_ >= t.offSince + kNoShootFrames)
                t.text.clear(), t.offSince = 0;
        } else
            t.offSince = 0;
        // totd7 (user 2026-11-05, "creatures packed close together share one word, as the sewer slugs in TotD 2"; crates
        // / barrels side by side one key): a group member has no word of its own while its leader keeps one (the
        // leader gone or done: its own word next); one about to get a word joins a leader of the same routine close by
        // on the screen and in depth (group_* / prop_group_*, group_max in all): the leader's word takes them all down
        if (t.leader && !leaderOf(t))
            t.leader = 0;
        if (!t.leader && t.text.empty() && t.typed == 0 && active && cfg_.groupMax > 1 && (kind == 0 || (kind == 3 && t.quick)) &&
            !isCaptor(rt, obj) && nameOf(rt, mem.u32(obj)).find("lock") == std::string::npos &&
            (kind != 0 || t.small || mem.f32(obj + 0x40) < cfg_.nearDepth || frame_ >= t.born + kMeasure)) {
            const bool prop = kind == 3;
            const float gx = prop ? cfg_.propGroupDx : cfg_.groupDx, gy = prop ? cfg_.propGroupDy : cfg_.groupDy,
                        gz = prop ? cfg_.propGroupDz : cfg_.groupDz;
            for (auto& [o2, l] : targets_) {
                if (o2 == obj || l.leader || l.text.empty() || l.finishing || !l.seen || l.kind != kind || l.quick != t.quick ||
                    mem.u32(o2) != mem.u32(obj) || std::abs(l.x - x) > gx || std::abs(l.y - y) > gy ||
                    std::abs(mem.f32(o2 + 0x40) - mem.f32(obj + 0x40)) > gz)
                    continue;
                int n = 1;
                for (const auto& [o3, m] : targets_)
                    n += m.leader == o2;
                if (n >= cfg_.groupMax)
                    continue;
                t.leader = o2;
                if (n == 1)
                    ++groupsMade;
                if (trace_)
                    std::printf("frame %llu: TOTD group %X %s joins %X \"%s\" (%d in all)\n", (unsigned long long)frame_, obj,
                                nameOf(rt, mem.u32(obj)).c_str(), o2, l.text.c_str(), n + 1);
                break;
            }
        }
        if (!t.leader && t.text.empty() && t.typed == 0 && active && !(kind == 1 && frame_ < t.born + 3 && k != 4) &&   // (totd3: a thrown thing seen 3 frames: no flashes; not the ball)
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
            if (kind == 3)
                for (TypingStats* s : {&game, &chapter})
                    ++s->items[0];
            if (kind == 2)
                game.bossSeen = chapter.bossSeen = true;
            if (trace_)
                std::printf("frame %llu: TOTD new %X %s kind %d%s hp %d/%d z %.0f speed %.2f band %d \"%s\"\n", (unsigned long long)frame_, obj,
                            rt.routineName(t.routine).c_str(), kind, t.small ? " small" : "", mem.at<int16_t>(obj + 0x70), hpMax,
                            mem.f32(obj + 0x40), classify_->approach(obj), t.band, t.text.c_str());
        }
        // totd7: a quick one's key follows its side of the screen (G left of the middle, H right)
        // (with a margin round the middle, kSideMargin units: a key changes only for one well into the other half, never
        // G / H / G for one moving round the middle)
        if (t.quick && !t.text.empty() && t.finishing == 0 && t.typed == 0) {
            const float margin = noLatch_ ? 0.0f : kSideMargin;
            if (x < -margin || (margin == 0.0f && x < 0))
                t.text = std::string(1, cfg_.quickLeft);
            else if (x > margin || margin == 0.0f)
                t.text = std::string(1, cfg_.quickRight);
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
        const bool tried = t.finishShots > 0 || t.strikes > 0;   // (totd5: its shot, or its hit given)
        const bool dead = !same || (t.kind != 2 && (byHp ? hp <= 0 && (t.finishing ? tried : !t.seen)
                                                         : (t.finishing && tried && !t.seen) ||
                                                           ((t.small || t.kind == 1) && t.finishing && t.landedAt)));
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
            if (dead && t.finishing && t.kind == 3)   // (totd7: a prop typed and breaking: no new key on its pieces' last
                killed_[t.obj] = {t.serial, frame_ + 150};   // frames (a box back for a moment: a flicker))
            if (trace_)
                std::printf("frame %llu: TOTD drop %X typed %zu dead %d seen %d\n", (unsigned long long)frame_, t.obj, t.typed, int(dead), int(t.seen));
            if (lock_ == t.obj) lock_ = 0;
            release(rt, t);
            it = targets_.erase(it);
        } else
            ++it;
    }
    // totd3: a boss just killed (its dying fall, its other parts still shootable a moment): no boss sentences (the
    // Chariot's last part kept one up over its death, typed for nothing)
    if (frame_ < bossDownUntil_)
        for (auto it = targets_.begin(); it != targets_.end();)
            if (it->second.kind == 2) {
                if (lock_ == it->first) lock_ = 0;
                release(rt, it->second);
                it = targets_.erase(it);
            } else
                ++it;
    // two prompts on the screen starting with the same letter (one came back into view, a list used up): the later
    // one, not begun, gets another (TotD never shows two at once: the first key must pick one)
    for (auto& [o, t] : targets_) {
        if (!t.seen || t.finishing || t.typed > 0 || lock_ == o || t.text.empty() || t.quick) continue;   // (totd7: G / H shared)
        for (auto& [o2, u] : targets_)
            if (o2 != o && u.seen && !u.finishing && !u.text.empty() && !u.quick && (u.born < t.born || (u.born == t.born && o2 < o)) &&
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
        // (totd3, attack_hold 2: the grace runs from the prompt shown, while it walks up)
        if (cfg_.attackHold == 1 && !(t.seen && (engaged || close || t.small)) && frame_ < t.born + ticks(cfg_.attackWait[d]))
            continue;
        t.arrive = frame_;
        size_t letters = t.text.size() - t.typed;
        if (t.left > 0) letters *= 2;
        const float wait = t.small ? cfg_.smallWait[d] : cfg_.attackWait[d];   // (totd6: small_wait; totd5 half attack_wait)
        t.readyAt = frame_ + std::max<uint64_t>(1, ticks(wait + cfg_.attackPerLetter[d] * float(letters)));
        // totd6 (user 2026-11-05: "at 40 WPM Normal ended in GAME OVER in Chapters 2-4"; the measure: three tough ones
        // at once, "you", "fresh air", "cup of tea", each with its own grace from the same moment: the third struck
        // while the first two were typed): the graces queue as a typist types them, one after another. A creature's
        // grace begins no sooner than the end of the latest grace still running (plus attack_queue seconds to move
        // to it): attack_wait to read it, then attack_per_letter (the difficulty's typing speed) a letter. The small
        // ones (one letter, they break in) neither queue nor hold the queue up
        if (!t.small && cfg_.attackQueue[d] >= 0) {
            uint64_t end = 0;
            for (const auto& [o2, u] : targets_)
                if (o2 != o && u.kind == 0 && !u.small && !u.text.empty() && u.finishing == 0 && u.readyAt > frame_)
                    end = std::max(end, u.readyAt);
            if (end)
                t.readyAt = std::max(t.readyAt, end + ticks(cfg_.attackQueue[d] + cfg_.attackPerLetter[d] * float(letters)));
        }
        // totd8 (round 7's table: Normal at 55 WPM ended in a game over in Chapter 2; the measure: the Hangedman's
        // swarm, four bats keyed at once, each with small_wait from the same moment: a 60 WPM typist took them 20
        // frames apart and the fourth struck first): the small ones queue among themselves, small_queue seconds a key
        if (t.small && cfg_.smallQueue[d] > 0) {
            uint64_t end = 0;
            for (const auto& [o2, u] : targets_)
                if (o2 != o && u.kind == 0 && u.small && !u.text.empty() && u.finishing == 0 && u.readyAt > frame_)
                    end = std::max(end, u.readyAt);
            if (end)
                t.readyAt = std::max(t.readyAt, end + ticks(cfg_.smallQueue[d]));
        }
    }
    // totd2 (user 2026-11-03: "bosses don't work"): one boss sentence at a time. A boss of several parts (the Chariot's
    // armour and body, two at once) had a sentence each, both drawn in the one boss panel over each other: unreadable.
    // The part shown: the one locked or begun, else the most dangerous; the others wait (their window held)
    bossPrimary_ = 0;
    {
        float best = -1;
        for (auto& [o, t] : targets_) {
            if (t.kind != 2 || t.text.empty() || (!t.seen && !(t.lastSeen && frame_ <= t.lastSeen + kSeenLatch) && o != lock_ && t.typed == 0)) continue;   // (begun: shown off the screen too)
            // totdex (found by the recent_words runs: Chapter 1's Chariot never died): its frame part (the bar's hit
            // points) came back as a new target after its armour-break routine with a lower threat than its hit box
            // (no hit points), and every sentence after went to the hit box for nothing (152 sentences, the event
            // waiting on it). A part whose hit points a sentence takes goes first
            const int16_t php = mem.at<int16_t>(o + 0x70), pmax = mem.at<int16_t>(o + 0x72);
            const float sc = t.threat + (o == lock_ ? 1e7f : 0.0f) + (t.typed > 0 ? 1e6f : 0.0f) + (pmax > 0 && php > 0 ? 1e5f : 0.0f);
            if (sc > best) best = sc, bossPrimary_ = o;
        }
        for (auto& [o, t] : targets_)
            if (t.kind == 2 && ((o != bossPrimary_ && t.typed == 0) || frame_ < bossRestUntil_)) t.born = frame_;
    }
    const bool bossRest = frame_ < bossRestUntil_;   // (totd5: the boss acts; its sentence comes after)
    // totd8: a free strike's block: its sentence's first boss_block_letters letters typed (in the warning or before
    // the hit; graceHolds holds the hit); the boss staggers (one shot at its sentence's part, its damage taken back:
    // blockShotUntil_) 12 frames into its strike, so the strike is seen and seen blocked
    if (blockObj_ && !strikeBlocked_ && (frame_ < bossStrikeUntil_ || warnUntil_))
        for (auto& [o, t] : targets_)
            if (t.kind == 2 && o == bossPrimary_ && t.born >= blockFrom_ && t.typed >= size_t(cfg_.bossBlockLetters[difficulty_])) {
                strikeBlocked_ = true;
                ++bossBlocks;
                staggerAt_ = warnUntil_ ? 0 : frame_;   // (in the warning: when the strike begins, holdsBoss)
                if (trace_)
                    std::printf("frame %llu: TOTD boss strike blocked (\"%s\", %zu typed)\n", (unsigned long long)frame_, t.text.c_str(), t.typed);
                break;
            }
    if (staggerAt_ && frame_ >= staggerAt_) {
        staggerAt_ = 0;
        if (auto it = targets_.find(bossPrimary_); it != targets_.end()) {
            it->second.bossShotsLeft += 1;
            it->second.nextBossShot = frame_;
            blockShotFrom_ = frame_, blockShotUntil_ = frame_ + 30;
        }
    }
    // a boss sentence's window: not finished in time, a new one (the attack window missed)
    // (the window's length: a base and a time per letter, in the game's ticks: Sonic Mode's faster ticks shorten it)
    const auto windowOf = [&](const Target& t) {
        return uint64_t(std::max(1.0f, cfg_.bossWindowSeconds[difficulty_] + cfg_.bossWindowPerLetter[difficulty_] * float(t.text.size())) * 60.0f);
    };
    for (auto& [o, t] : targets_)
        if (t.kind == 2 && t.finishing == 0 && !t.text.empty() && frame_ > t.born + windowOf(t)) {
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
        if (t && t->typed < t->text.size())   // totdex (the Statistics page): the letter that should have come, the prompt
            for (TypingStats* s : {&game, &chapter}) {
                ++s->keyMiss[uint8_t(low(uint8_t(t->text[t->typed]))) & 127];
                ++s->wordMiss[t->text];
            }
        if (t || !labels.empty())   // (totd4: a wrong letter's sound; none for keys with no prompt up)
            sound(rt, "miss");
    };
    // totdex (SEGA's judge): keyMatches (tests: tests/unit/test_typing.cpp)
    const auto isShifted = [](uint32_t k) { return shiftedKey(k); };
    bool prevShift = false;
    const auto matches = [&](char want, uint32_t k) { return keyMatches(want, k, prevShift); };
    // (totd7: a box on the screen this tick, its target seen now or within kSeenLatch frames: what is shown is typable)
    const auto shownNow = [&](const Target& x) { return (x.seen || (!noLatch_ && x.lastSeen && frame_ <= x.lastSeen + kSeenLatch)) && (noLatch_ || frame_ >= x.hiddenUntil); };
    for (uint32_t key : queue_) {
        prevShift = lastShifted_;
        lastShifted_ = isShifted(key);
        // totd7 (user 2026-11-05: "typing during cut-scenes or when no word is up ruins the PERFECT streak"): a key with
        // nothing up to type (no box: an empty screen, between waves; cut-scenes never queue keys) is ignored entirely:
        // no miss, no streak reset, no shot
        if (labels.empty() && !lock_) {
            ++ignoredKeys;
            continue;
        }
        if (key == 0 || key >= 128) { miss(nullptr); continue; }
        Target* t = nullptr;
        if (lock_) {
            auto it = targets_.find(lock_);
            if (it == targets_.end() || it->second.finishing) lock_ = 0;
            else t = &it->second;
        }
        // totd7 (user 2026-11-05: "quick attacks use one key by screen side: G for a target on the left half, H for the
        // right"): G / H (unless the locked word's next letter) takes the quick one on that side (a thrown thing
        // first, then the most dangerous) down at once; the locked word stays locked. With none on that side the key
        // does nothing (no miss, the streak kept)
        if (cfg_.quickKeys && (low(key) == cfg_.quickLeft || low(key) == cfg_.quickRight) &&
            !(t && !t->quick && t->typed < t->text.size() && same(t->text[t->typed], key))) {
            Target* q = nullptr;
            float best = -1;
            for (auto& [o, x] : targets_) {
                if (!x.quick || !shownNow(x) || x.finishing || x.leader || x.text.empty() || low(uint8_t(x.text[0])) != low(key)) continue;
                const float th = x.threat + (x.kind == 1 ? 10000.0f : 0.0f);
                if (th > best) best = th, q = &x;
            }
            // totdpacks: none on that side but a word that starts with the key (a text's "he" / "gone": the lists have
            // none): the key locks it as any first letter
            bool word = false;
            for (const auto& [o, x] : targets_)
                word = word || (!t && !q && !x.quick && shownNow(x) && !x.finishing && x.typed == 0 && !x.text.empty() &&
                                x.kind != 2 && same(x.text[0], key));
            if (!q && !word) {
                ++quickIgnored;
                if (trace_)
                    std::printf("frame %llu: TOTD quick %c: nothing on that side (ignored)\n", (unsigned long long)frame_, char(key));
                continue;
            }
            if (q) {
                ++q->lettersAll;
                ++game.letters, ++chapter.letters;
                q->startMs = nowMs_;
                q->typed = q->text.size();
                ++quickTyped;
                sound(rt, "key");   // (its killing shot fires below: the gun's own sound)
                lastHitFrame = frame_, lastHitObj = q->obj;
                const uint32_t keep = t ? t->obj : 0;
                if (trace_)
                    std::printf("frame %llu: TOTD quick %c: %X %s\n", (unsigned long long)frame_, char(key), q->obj, nameOf(rt, mem.u32(q->obj)).c_str());
                finished(rt, *q);
                lock_ = keep;   // (the word being typed stays locked)
                continue;
            }
        }
        if (!t) {
            // the lock: the prompts on the screen whose next letter is this one; the most dangerous of them
            float best = -1;
            for (auto& [o, x] : targets_) {
                if (!shownNow(x) || x.finishing || x.quick || x.typed >= x.text.size() || (x.kind == 2 && (o != bossPrimary_ || bossRest))) continue;
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
        } else if (!matches(t->text[t->typed], key)) {
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
        sound(rt, "key");   // (totd4: the key's sound; the word's ding in finished())
        lastHitFrame = frame_, lastHitObj = t->obj;
        if (t->typed >= t->text.size()) {
            finished(rt, *t);
            if (t->kind == 2)   // (a boss sentence's last letter: its shots follow by rounds; the gun's sound now)
                shotSound(rt);
        } else if (t->kind == 0 && !t->small && mem.at<int16_t>(t->obj + 0x72) > kSmallHp && shots_.empty())
            // a letter's shot (enemies with hit points to hold; a projectile, an item, a small creature (a bat dies at
            // any hit), a creature without hit points (the frogs) would go at the first letter: they get the last only)
            shots_.push_back({t->obj, t->serial, true});
        else   // totd7 (user 2026-11-05: "every typed letter fires the gun with its shot sound, as the PC TotD"): a
            shotSound(rt);   // letter without a shot of its own now (a boss sentence, an item, a tick's second letter)
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
            fire(rt, t.bossAlt && mem.u32(t.bossAlt) && int32_t(mem.u32(t.bossAlt + 8)) == t.bossAltSerial ? Shot{t.bossAlt, t.bossAltSerial, false}
                                                                                                         : Shot{t.obj, t.serial, false})) {
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
                if (t.kind == 3) {   // an item / a prop / a lock: a shot every 6 frames until it breaks (some take a few)
                    if ((t.finishShots > 0 || t.strikes > 0) && frame_ < t.nextBossShot)
                        continue;
                    t.nextBossShot = frame_ + 6;
                    --t.finishing;
                    // (totd7: one key breaks it: a crate with hit points left takes this shot as its last)
                    if (mem.at<int16_t>(o + 0x72) > 0 && mem.at<int16_t>(o + 0x70) > 1)
                        mem.at<int16_t>(o + 0x70) = 1;
                    const bool fired = fire(rt, {t.obj, t.serial, false});
                    // totd5: from the second try its shot's hit is given too (a lock / a crate the aim missed: the
                    // lock's hit bit, as a landed shot's; the arcade's own break / rescue / item follows)
                    if (t.finishShots > 1 || !fired) {
                        ++t.strikes;
                        mem.at<uint32_t>(o + 0x58) |= 0x102u;
                        if (trace_)
                            std::printf("frame %llu: TOTD item hit given %X %s\n", (unsigned long long)frame_, o, nameOf(rt, mem.u32(o)).c_str());
                    }
                    if (t.finishing == 0)
                        giveUp(rt, t);
                    if (fired) { shot = true; break; }
                    continue;
                }
                --t.finishing;
                // totd5 (user 2026-11-04: "one typed letter = one shot, never bursts / autofire"): the word's one
                // killing shot (totd4 fired one a frame, up to 40, until one landed: a frog under a barrage); if it
                // did not land, its hit is given directly (strike) every 10 frames, no shot fired, until it goes
                bool fired = false;
                if (t.finishShots == 0 && fire(rt, {t.obj, t.serial, false})) {
                    fired = true;
                    if (smallOne)
                        killShot_ = o;
                    t.nextStrike = frame_ + (smallOne ? 1 : 3);   // (a creature's shot lands in a frame or two)
                    if (t.kind == 1) {   // (a thrown thing: broken by its shot whatever it hit: a barrel took 2-3)
                        strike(rt, t);
                        t.nextStrike = frame_ + 10;
                    }
                } else if (t.finishShots == 0 && t.nextStrike == 0)
                    t.nextStrike = frame_;   // (no shot possible now: off the screen, a researcher in the way)
                if (!fired && t.nextStrike && frame_ >= t.nextStrike && !(smallOne && t.landedAt)) {
                    strike(rt, t);
                    if (smallOne)
                        t.landedAt = frame_;   // (its hit given: it falls / breaks; harmless anyway, neutral_)
                    t.nextStrike = frame_ + 10;
                }
                if (t.finishing == 0) {   // it never went (out of reach the whole time): the
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
    bool bossUp = false;
    for (const auto& [o, t] : targets_)
        bossFight_ = bossFight_ || (t.kind == 2 && t.seen), bossUp = bossUp || (t.kind == 2 && !t.text.empty());
    if (bossFight_)
        bossSeenAt_ = frame_;
    // (totd8: a missed window's strike is owed while the boss is about, off the screen for a moment too: its strike
    // waits for it, holdsBoss)
    if (!bossUp)
        bossPunish_ = false, bossStrikeUntil_ = 0, bossUnseenAsk_ = 0;
    rt_ = &rt;
    gHolder = this;
    // (totd3: attack_hold 0: no attack / hit holds, the arcade's own timing; 2: the short grace (holds(), holdsHit()
    // outside boss fights); the rescue timer stays: a captor's bite on a researcher)
    port::gAttackHold = cfg_.attackHold ? holdHook : nullptr;
    port::gHitHold = hitHook;   // (totd5: always: a typed thing's hit; attack_hold's rules inside)
    port::gPreyHold = preyHook;
    static const bool boss7 = std::getenv("HOTD_TOTD_BOSS7") != nullptr;   // (testing: round 7, the strikes not held)
    port::gBossHold = cfg_.attackHold && !boss7 ? bossHook : nullptr;   // (totd8: a boss strike waits for a missed window)
    gTotdProjectileHold = projHook;
    tickHitVia_.clear(), tickHitObj_ = 0;
    for (auto& [o, t] : targets_) {
        if (t.kind != 0 || !t.seen || !cfg_.attackHold) continue;
        const std::string n = nameOf(rt, mem.u32(o));
        if ((n == "kind32_carry_wait" || n == "kind32_throw_wait") && mem.at<int32_t>(o + 0x2EC) <= 1 && holds(o))
            mem.at<int32_t>(o + 0x2EC) = 2;
    }
    targetedWas_ = mem.at<uint8_t>(G::player_targeted);
    invulnWas_ = int32_t(mem.u32(G::player_invulnerable));

    // totd3 (user 2026-11-03: "no projectiles: the thrown axes, knives, the boss projectiles get no working prompts"):
    // only the axes / barrels were slowed (their velocity); the Magician's fireballs, Drake's knives, the spit were at
    // the player in 0.5-0.7 s, before a person reads them. Every other thrown thing with a prompt keeps projectile_speed
    // of each tick's step (its position put back part of the way after the tick, after())
    projPos_.clear();
    {
        const float sp = std::clamp(cfg_.projectileSpeed[difficulty_], 0.1f, 1.0f);
        for (auto& [o, t] : targets_) {
            const std::string rn = t.kind == 1 ? nameOf(rt, mem.u32(o)) : std::string();
            // (totd6: an iron ball from its first sight, ball_speed: thrown from 40 units at 3 a frame it reached the
            // screen 0.4 s after its word showed, before a person reads it)
            const bool ball = rn == "iron_ball_follow";
            const float k = ball ? std::clamp(cfg_.ballSpeed[difficulty_], 0.05f, 1.0f) : sp;
            if (t.kind != 1 || !t.seen || t.finishing || (t.text.empty() && !ball) || k >= 1.0f) continue;
            if (rn == "flying_axe_frame" || rn == "held_object_flight" || rn == "held_object_throw_flight")
                continue;   // (their velocity is slowed, above)
            projPos_.push_back({o, t.serial, mem.f32(o + 0x20), mem.f32(o + 0x24), mem.f32(o + 0x28), k});
        }
    }
    // the HUD's labels, the most dangerous first
    if (lock_ != lockWas_)   // (totd5: the lock-on burst's start)
        lockWas_ = lock_, lockFrame_ = frame_;
    labels.clear();
    for (auto& [o, t] : targets_) {
        // (totd7, user 2026-11-05: "the G and H flicker", "a huge no-no"): a target out of the shootable list or its aim
        // point for a frame or two (a hit reaction's flag, a flash between two body poses) keeps its box at its last
        // place for kSeenLatch frames: a box goes once (typed, its target gone, its time out), never on / off / on
        const bool seenNow = t.seen || (!noLatch_ && t.lastSeen && frame_ <= t.lastSeen + kSeenLatch);
        if (static const bool why = std::getenv("HOTD_TOTD_FLICKER") != nullptr; why) {   // (the flicker check: why a box shown last tick goes)
            const auto sh = shown_.find(o);
            if (sh != shown_.end() && sh->second.last + 1 == frame_ &&
                ((!seenNow && !(t.kind == 2 && o == bossPrimary_)) || t.finishing || t.text.empty() || (t.kind == 2 && (o != bossPrimary_ || bossRest))))
                std::printf("frame %llu: TOTD box %X goes: seen %d last seen %llu finishing %d text \"%s\" kind %d boss %d rest %d\n",
                            (unsigned long long)frame_, o, int(t.seen), (unsigned long long)t.lastSeen, t.finishing, t.text.c_str(),
                            t.kind, int(o == bossPrimary_), int(bossRest));
        }
        const bool hide = (!seenNow && !(t.kind == 2 && o == bossPrimary_)) || t.finishing || t.text.empty() ||
                          (t.kind == 2 && (o != bossPrimary_ || bossRest));
        // (a box that goes stays gone a second: back before that would be a blink; a typed word's box never comes back
        // on that target with the same word)
        if (!noLatch_ && hide && !t.finishing) {
            const auto sh = shown_.find(o);
            if (sh != shown_.end() && sh->second.last + 1 == frame_)
                t.hiddenUntil = frame_ + 61;
        }
        if (hide || (!noLatch_ && frame_ < t.hiddenUntil)) continue;
        Label l;
        l.obj = o, l.x = t.x, l.y = t.y, l.text = t.text, l.typed = t.typed, l.locked = lock_ == o;
        l.kind = t.kind == 0 && t.small ? 1 : t.kind;
        l.left = t.kind == 2 ? 0 : t.left;
        l.quick = t.quick;
        // totd7: a group's word over the group's middle (its members on the screen)
        for (const auto& [o2, m] : targets_)
            if (m.leader == o && m.seen && !m.finishing) {
                l.x += m.x, l.y += m.y;
                ++l.group;
            }
        l.x /= float(l.group), l.y /= float(l.group);
        // totd8 (user 2026-11-05: "make sure the text boxes are not blocking the zombies too much; look at the video
        // for reference"; the PC TotD measured, docs/typing.md): its box sits on the creature's belly, its centre about
        // 53 % down the body from the top of the head, the head and shoulders clear; a far one's above the head. Ours
        // sat on the object's centre (the chest: the arms and their swing hidden). The body: its parts' hit spheres
        // (bodyCircles); box_anchor 0: the centre as before
        static const float anchorEnv = std::getenv("HOTD_TOTD_BOXANCHOR") ? float(std::atof(std::getenv("HOTD_TOTD_BOXANCHOR"))) : -1.0f;   // (testing)
        const float anchor = anchorEnv >= 0.0f ? anchorEnv : cfg_.boxAnchor;
        if (t.kind == 0 && !t.small && !t.quick && l.group == 1 && anchor > 0.0f) {
            static thread_local std::vector<Circle> body;
            if (bodyCircles(rt, o, body)) {
                float top = 1e9f, bottom = -1e9f;
                for (const Circle& c : body)
                    top = std::min(top, c.y - c.r), bottom = std::max(bottom, c.y + c.r);
                if (bottom > top && bottom - top < 1200.0f) {
                    l.top = top, l.bottom = bottom;
                    l.y = top + anchor * (bottom - top);   // (a small one's box above its head: the HUD, by its size)
                }
            }
        }
        l.urgent = t.urgent;
        l.window = t.kind == 2 ? std::clamp(1.0f - float(frame_ - t.born) / float(windowOf(t)), 0.0f, 1.0f) : -1.0f;
        if (t.kind == 0 && t.readyAt > t.arrive && cfg_.attackHold == 1)
            l.attack = frame_ >= t.readyAt ? 0.0f : float(t.readyAt - frame_) / float(t.readyAt - t.arrive);
        // totd4 (the reference video: a box turns from green to yellow when its zombie is at you): at an attack place,
        // within 45 units, or its grace over (it may strike now); a boss sentence in its last 40 %
        if (t.kind == 0)
            l.danger = mem.at<int8_t>(o + 0x2E7) >= 0 || mem.f32(o + 0x40) < 45.0f || (t.readyAt && frame_ >= t.readyAt);
        else if (t.kind == 1)
            l.danger = true;
        else if (t.kind == 2)
            l.danger = l.window < 0.4f;
        l.hitAge = lastHitObj == o ? int(frame_ - lastHitFrame) : 1000;
        l.lockAge = l.locked ? int(std::min<uint64_t>(1000, frame_ - lockFrame_)) : 1000;
        l.z = mem.f32(o + 0x40);
        if (float ax, ay; t.kind != 2 && aimPoint(rt, o, 0, ax, ay))   // (totd6: its step over the tick, after())
            labelAt_[o] = {ax, ay};
        labels.push_back(l);
    }
    std::stable_sort(labels.begin(), labels.end(), [&](const Label& a, const Label& b) {
        if (a.locked != b.locked) return a.locked;
        return targets_[a.obj].threat > targets_[b.obj].threat;
    });
    // totd7 (the flicker check, the report's TOTD FLICKER line): each box against its last showing: back within 60
    // frames for the same target after a gap = a blink; a quick key's letter changed = a flip
    for (const Label& l : labels) {
        const int32_t serial = int32_t(mem.u32(l.obj + 8));
        auto it = shown_.find(l.obj);
        if (it != shown_.end() && it->second.serial == serial) {
            Shown& s = it->second;
            const uint64_t gap = frame_ - s.last;
            if (gap > 1) s.since = frame_;   // (totdrec: a new showing)
            if (gap > 1 && gap <= 60) {
                ++blinks;
                if (trace_ || std::getenv("HOTD_TOTD_FLICKER"))
                    std::printf("frame %llu: TOTD FLICKER %X %s \"%s\": gone %llu frames\n", (unsigned long long)frame_, l.obj,
                                nameOf(rt, mem.u32(l.obj)).c_str(), l.text.c_str(), (unsigned long long)(gap - 1));
            } else if (gap > 60)
                ++longGaps;
            // (a key changing side once, its target crossed well into the other half, is a change; back again within
            // 60 frames is a flicker)
            if (l.quick && s.text != l.text && gap <= 1) {
                if (s.flipAt && frame_ <= s.flipAt + 60) {
                    ++keyFlips;
                    if (trace_ || std::getenv("HOTD_TOTD_FLICKER"))
                        std::printf("frame %llu: TOTD FLICKER %X key %s -> %s\n", (unsigned long long)frame_, l.obj, s.text.c_str(), l.text.c_str());
                }
                s.flipAt = frame_;
            }
            s.last = frame_, s.text = l.text;
        } else
            shown_[l.obj] = {serial, frame_, l.text, 0, frame_};
    }
    if (frame_ % 600 == 0)
        std::erase_if(shown_, [&](const auto& e) { return frame_ > e.second.last + 600; });
    // (totd5: nothing to type since: the far idle creatures' fallback word)
    if (!labels.empty())
        idleSince_ = 0;
    else if (!idleSince_)
        idleSince_ = frame_;
}

void Typing::after(Runtime& rt)
{
    auto& mem = rt.mem;
    // totd8 (the coordinator 2026-11-05: a thrown axe in the package with no G / H): every thrown thing the game lets
    // be shot gets its prompt within prompt_check frames (20) of its first frame on the screen; the report counts the
    // late ones and the ones that never got one (TOTD PROMPT CHECK)
    for (uint32_t obj : rt.shootable) {
        if (classify_->targetKind(rt, obj) != 1 || mem.u32(obj) == 0x74150)   // (the barrage: barrage_words at a time, by design)
            continue;
        float x = 0, y = 0;
        if (!aimPoint(rt, obj, 0, x, y))
            continue;
        auto& p = projSeen_[obj];
        if (p.serial != int32_t(mem.u32(obj + 8)))
            p = {frame_, int32_t(mem.u32(obj + 8)), 0}, ++projSeen;
        if (p.state != 0)
            continue;
        const auto it = targets_.find(obj);
        if (it != targets_.end() && !it->second.text.empty()) {
            p.state = 1;
            ++(frame_ - p.first <= 20 ? projPrompted : projLate);
        } else if (frame_ - p.first > 20) {
            p.state = 2;
            ++projNever;
            std::printf("frame %llu: TOTD PROMPT CHECK: %X %s on the screen %llu frames with no prompt\n", (unsigned long long)frame_, obj,
                        nameOf(rt, mem.u32(obj)).c_str(), (unsigned long long)(frame_ - p.first));
        }
    }
    // the attack window ends with the tick
    if (gHolder == this) {
        port::gAttackHold = nullptr;
        port::gHitHold = nullptr;
        port::gPreyHold = nullptr;
        port::gBossHold = nullptr;
        gTotdProjectileHold = nullptr;
        gHolder = nullptr;
    }
    for (const auto& [o, p] : preyHeldAt_)   // (the held captors back where they were: no step into the researcher)
        if (mem.u32(o) != 0)
            mem.at<float>(o + 0x20) = p.first, mem.at<float>(o + 0x28) = p.second;
    preyHeldAt_.clear();
    if (!projPos_.empty()) {   // (totd3: the thrown things' step this tick, projectile_speed of it kept)
        for (const ProjPos& p : projPos_)
            if (const float sp = p.k; mem.u32(p.obj) != 0 && int32_t(mem.u32(p.obj + 8)) == p.serial && (mem.u32(p.obj + 0x58) & 0x100u) == 0) {
                mem.at<float>(p.obj + 0x20) = p.x + (mem.f32(p.obj + 0x20) - p.x) * sp;
                mem.at<float>(p.obj + 0x24) = p.y + (mem.f32(p.obj + 0x24) - p.y) * sp;
                mem.at<float>(p.obj + 0x28) = p.z + (mem.f32(p.obj + 0x28) - p.z) * sp;
            }
        projPos_.clear();
    }
    // totd6 (the feel report: "boxes drift against bodies when the camera moves: Smooth Motion draws the picture
    // blended between the last two ticks"): each box's step over this tick (its aim point now minus before the tick),
    // the HUD adds alpha of it. A boss (several weak points by turns) and anything gone or off the screen: no step
    for (Label& l : labels) {
        l.dx = l.dy = 0;
        float bx, by, ax, ay;
        if (l.kind == 2 || mem.u32(l.obj) == 0)
            continue;
        const auto it = targets_.find(l.obj);
        if (it == targets_.end() || int32_t(mem.u32(l.obj + 8)) != it->second.serial)
            continue;
        if (labelAt_.count(l.obj) && aimPoint(rt, l.obj, 0, ax, ay)) {
            std::tie(bx, by) = labelAt_[l.obj];
            if (std::abs(ax - bx) < 120.0f && std::abs(ay - by) < 120.0f)   // (a cut, a jump: no blend)
                l.dx = ax - bx, l.dy = ay - by;
        }
    }
    labelAt_.clear();
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
        if (hit) {
            ++hits, ++game.hits, ++chapter.hits, bossPunish_ = false;
            lastHit_ = rt.frameCount;
            if (cfg_.wordLevelHit > 0) {   // totdex (SEGA's word level: down after a hit)
                wordLevel = std::max(cfg_.wordLevelMin, wordLevel - cfg_.wordLevelHit);
                levelRun_ = 0;
            }
            ++hitFrom[tickHitVia_.empty() ? std::string("(other)") : tickHitVia_];
            // totdrec (the user's recording 2026-11-05, 14:34 / 15:13: an agile one leapt in with no box and hit): a
            // creature's hit with its box on the screen (up within the last 20 frames) or not
            long long boxAge = -1;
            if (auto ht = targets_.find(tickHitObj_); ht != targets_.end() && ht->second.kind == 0) {
                uint32_t bo = tickHitObj_;
                if (ht->second.leader && targets_.count(ht->second.leader)) bo = ht->second.leader;
                const auto sh = shown_.find(bo);
                boxAge = sh == shown_.end() ? 100000 : (long long)(frame_ - sh->second.last);
                ++(boxAge > 20 ? hitsUnshown : hitsShown);
            }
            if (trace_ || (boxAge > 20 && std::getenv("HOTD_TOTD_UNSHOWN")))
                std::printf("frame %llu: TOTD hit by %s (box age %lld)\n", (unsigned long long)rt.frameCount, tickHitVia_.c_str(), boxAge);
            if (strikeLanded_)   // (totd8: the boss's strike landed: its next one waits for the next missed window)
                bossStrikeUntil_ = 0, ++bossStrikesLanded;
        }
        strikeLanded_ = false;
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
                    t.readyAt = rt.frameCount + std::max<uint64_t>(1, ticks(cfg_.attackHold == 1 ? cfg_.attackWait[difficulty_] : cfg_.hitReset[difficulty_]));
                }
            if (trace_) {
                std::printf("frame %llu: TOTD attack%s%s\n", (unsigned long long)rt.frameCount, began ? " began" : "", hit ? " hit" : "");
                for (const auto& [o, t] : targets_)   // (totdrec: who: the near creatures' state / step, grace left)
                    if (t.kind == 0 && mem.f32(o + 0x40) < 40.0f)
                        std::printf("frame %llu: TOTD   near %X %s state %d step %d z %.0f grace %lld \"%s\"\n", (unsigned long long)rt.frameCount, o,
                                    nameOf(rt, mem.u32(o)).c_str(), int(mem.at<int16_t>(o + 0x5C)), int(mem.at<int16_t>(o + 0x60)),
                                    mem.f32(o + 0x40), t.readyAt ? (long long)t.readyAt - (long long)rt.frameCount : -1LL, t.text.c_str());
            }
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
        if (hp <= 0 && hp < b.hp)   // (totd3: a boss killed: its other parts' prompts go, before()
            if (auto it = targets_.find(b.obj); (it != targets_.end() && (it->second.bossGoal == 0 || mem.at<int16_t>(b.obj + 0x72) > 0)) ||
                                                altGoal_.count(b.obj))
                bossDownUntil_ = rt.frameCount + 600;
        // totdex: a part taking another part's sentences: its bar is set by them; a landed shot's damage taken back
        // (the last sentence's goal 0: the shot kills it the game's own way)
        if (auto ag = altGoal_.find(b.obj); ag != altGoal_.end() && targets_.find(b.obj) == targets_.end()) {
            if (hp <= 0)
                altGoal_.erase(ag);
            else if (ag->second > 0 && hp < b.hp)
                mem.at<int16_t>(b.obj + 0x70) = int16_t(std::max<int>(ag->second, 1));
            continue;
        }
        if (hp > 0 && hp < b.hp && frame_ < blockShotUntil_ && lastBossFinish_ < blockShotFrom_) {   // (totd8: a block's stagger shot: no damage)
            mem.at<int16_t>(b.obj + 0x70) = b.hp;
            continue;
        }
        if (hp <= 0 || hp >= b.hp)
            continue;
        if (auto it = targets_.find(b.obj); it != targets_.end() && it->second.bossGoal >= 0) {
            // (totd3: a part on boss_sentences: its sentences set the hit points; the reaction shot's damage back)
            if (it->second.bossGoal > 0 && b.hp == it->second.bossGoal && hp < it->second.bossGoal)
                mem.at<int16_t>(b.obj + 0x70) = int16_t(it->second.bossGoal);
            continue;
        }
        // totd8 (user 2026-11-05: "the bosses are too easy; look at the video reference for health"): a part with a
        // life bar of its own (the Chariot's armour: its hits on the armour, not on boss_sentences) takes 1 /
        // boss_sentences of that bar a sentence, as the parts on boss_sentences do (round 7: its one hit x
        // boss_damage_x, 3 sentences for the armour); boss_damage_x for the rest
        const int16_t hpMax = mem.at<int16_t>(b.obj + 0x72);
        const int extra = hpMax > 0 ? std::max(0, (int(hpMax) + bossSentencesFor(rt, b.obj) - 1) / bossSentencesFor(rt, b.obj) - (b.hp - hp))
                                    : (b.hp - hp) * (cfg_.bossDamageX - 1);
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
        else if (f.what == 3)   // (totd5: an item taken / a prop broken / a lock shot: another routine or state, gone,
            ok = gone || r != f.routine || st != f.state || (mem.u32(f.obj + 0x58) & 1) == 0;   // or not shootable any more)
        else if (f.what == 1) {
            ok = (gone || r != f.routine || over) && hits == f.hitsAt;   // (a barrel: the first hit knocks it away, same routine)
            over = over || hits != f.hitsAt;
        } else
            ok = gone || r != f.routine || st != f.state || classify_->targetKind(rt, f.obj) == 5;
        if (ok || over) {
            if (ok)
                for (TypingStats* s : {&game, &chapter})
                    ++(f.what == 0 ? s->bats : f.what == 1 ? s->projectiles : f.what == 3 ? s->items : s->small)[2];
            if (trace_)
                std::printf("frame %llu: TOTD followed %X %s: %s\n", (unsigned long long)rt.frameCount, f.obj, nameOf(rt, f.routine).c_str(),
                            ok ? "killed" : "NOT killed");
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
    if (t.autoShot) {   // (totd3: a frog / hanging Ebitan out of reach: tried again in a second)
        t.finishShots = 0;
        t.nextAuto = frame_ + 60;
        return;
    }
    if (mem.u32(t.obj) != 0 && int32_t(mem.u32(t.obj + 8)) == t.serial && t.kind == 0 && mem.at<int16_t>(t.obj + 0x72) > 0 &&
        mem.at<int16_t>(t.obj + 0x70) > 0)
        mem.at<int16_t>(t.obj + 0x70) = std::max<int16_t>(1, t.realHp);
    if (trace_)
        std::printf("frame %llu: TOTD gave up on %X (%d killing shots), hp back to %d\n", (unsigned long long)frame_, t.obj,
                    t.finishShots, mem.at<int16_t>(t.obj + 0x70));
    t.holding = false;
    t.hiddenUntil = frame_ + 61;   // (totd7: its box came back the next frame with a new word: a blink)
    t.finishShots = 0;
    if (lock_ == t.obj) lock_ = 0;
    t.text = newPrompt(rt, t);
    t.typed = 0;
    t.startMs = -1;
    skipPunct(t);
}

// totd4 (user 2026-11-04): a key's / a miss's / a finished word's sound. data/typing/<name>.wav when there, else the PC
// game's cue (TypingConfig key_cue / miss_cue / ding_cue): a bank sample through Audio::playExtra (its volume), a file
// cue (the ding's O_LIFEUP) as the PC plays it. Straight to the mixer, never through the game's sound queue: the game's
// memory and frames stay as without the sounds (a muted / headless run plays the same)
void Typing::sound(Runtime& rt, const char* name) { soundTo(rt.audio, name); }
// (totdpacks: Practice has no Runtime: the menu's audio)
void Typing::soundTo(Audio* audio, const char* name)
{
    struct { Audio* audio; } rt{audio};   // (the body below as it was, rt.audio)
    static const bool off = std::getenv("HOTD_TOTD_NOSOUND") != nullptr;   // (testing: the same run without them)
    if (!cfg_.sounds || !rt.audio || off)
        return;
    const std::string n = name;
    const int cue = n == "key" ? cfg_.keyCue : n == "miss" ? cfg_.missCue : cfg_.dingCue;
    const float vol = n == "key" ? cfg_.keyVolume : n == "miss" ? cfg_.missVolume : cfg_.dingVolume;
    if (static const bool list = std::getenv("HOTD_TOTD_SOUNDLIST") != nullptr; list && !soundSaid_.count("#list")) {   // (testing: smallfix2)
        soundSaid_.insert("#list");
        rt.audio->reportCues(cfg_.soundPeak);
    }
    const auto dir = soundDir_.find(n);
    // (playExtra's stand-in: a bank sample, totd5: or a file cue's one-shot (the ding's O_LIFEUP), brought to
    // sound_peak of full scale: totd4's played at the bank's own level, 1.5 dB under the game's sounds, masked)
    const int type = rt.audio->cueType(cue);
    const bool bank = type == 1 || type == 4;
    const bool ok = rt.audio->playExtra(dir != soundDir_.end() ? dir->second : std::string("data/typing"), n, bank ? cue : -1,
                                        1.0f, 1.0f, vol, cfg_.soundPeak);
    if (!ok && !bank && cue >= 0)
        rt.audio->pcCue(cue);
    if (soundSaid_.insert(n).second)   // (once each: where the sound comes from)
        std::printf("TOTD sound %s: %s\n", name, ok ? (rt.audio->extraSource(n) == "file" ? (dir->second + "/" + n + ".wav").c_str() : ("PC cue " + std::to_string(cue)).c_str())
                                                    : cue >= 0 ? ("PC cue " + std::to_string(cue) + " (" + rt.audio->describe(cue) + ")").c_str() : "none");
}

// totdpacks: Practice (typing.h): words float across a fixed screen from one side to the other in lanes, at most
// drill_on_screen at once, a new one every drill_gap seconds (at once when none is up), drill_words in all; a word that
// reaches the far side untyped is lost (no attack, no lives). The keys are judged as in play (the first letter locks
// the word furthest along that starts with it, a wrong key a miss with the lock kept, Shift grace, case as the
// difficulty, Escape / Backspace by drop_lock_keys); the words picked by pick()'s rules (unique first letters on the
// screen, none of the last recent_words)
void Typing::drillStart(bool fromMisses)
{
    drill_ = true;
    drillFromMisses = fromMisses && !practiceWords_.empty();
    drillTotal = std::max(1, cfg_.drillWords);
    drillShown = drillTyped = drillLost = drillSameFirst = drillRepeats = 0;
    drillLostWords.clear();
    drillNextId_ = 0, drillNextAt_ = 0;
    drillSpeed_.clear();
    drillRecent_.clear();
    targets_.clear();
    labels.clear();
    lock_ = 0;
}

void Typing::drillTick(Audio* audio, double tickMs)
{
    if (!drill_) return;
    ++frame_;
    nowMs_ += tickMs;
    const int d = difficulty_;
    constexpr float kOut = 200.0f;   // (a word's centre starts / ends this far from the middle: its box at the 496-wide picture's edge)
    // a new word: from the side, in a lane no word near its start is in
    if (drillShown < uint32_t(drillTotal) && int(targets_.size()) < std::max(1, cfg_.drillMax) &&
        (targets_.empty() || frame_ >= drillNextAt_)) {
        const uint32_t id = ++drillNextId_;
        Target& t = targets_[id];
        t.obj = id;
        std::string s;
        bool got = false;
        if (drillFromMisses) {
            got = pick(cfg_.wordLists, 0, 0, id, s, &practiceWords_, nullptr);
            if (!got) {   // (a short list of misses: they come again, the recent rule set aside)
                const std::deque<std::string> keep = recent_;
                recent_.clear();
                got = pick(cfg_.wordLists, 0, 0, id, s, &practiceWords_, nullptr);
                recent_ = keep;
            }
        }
        if (!got && !textPick(id, s)) {
            if (drillShown % 5 == 4) pick({cfg_.toughList[d]}, cfg_.toughMin[d], cfg_.toughMax[d], id, s);
            else pick(cfg_.wordLists, cfg_.wordMin[d][1], cfg_.wordMax[d][2], id, s);
            // (the check counts the lists' words only: a short list of misses and a text repeat by nature)
            if (std::find(drillRecent_.begin(), drillRecent_.end(), s) != drillRecent_.end()) ++drillRepeats;
            drillRecent_.push_back(s);
            while (int(drillRecent_.size()) > std::max(1, cfg_.recentWords)) drillRecent_.pop_front();
        }
        t.text = s;
        t.cls = "practice";
        const bool fromLeft = rng_() % 2 == 0;
        bool used[7] = {};
        for (const auto& [o, x] : targets_)
            if (o != id && std::abs(x.x) > 120.0f) used[std::clamp(int(std::lround((110.0f - x.y) / 36.0f)), 0, 6)] = true;
        int lane = int(rng_() % 7);
        for (int k = 0; k < 7 && used[lane]; ++k) lane = (lane + 1) % 7;
        t.x = fromLeft ? -kOut : kOut, t.y = 110.0f - 36.0f * float(lane);
        const float seconds = cfg_.drillSeconds[d] + cfg_.drillPerLetter[d] * float(s.size());
        drillSpeed_[id] = (fromLeft ? 1.0f : -1.0f) * 2.0f * kOut / std::max(1.0f, seconds * 60.0f);
        t.born = frame_, t.bornMs = nowMs_;
        ++drillShown;
        drillNextAt_ = frame_ + ticks(cfg_.drillGap[d]);
    }
    // the keys (as before()'s judge, without the game: no shots, no quick keys)
    const auto shiftOf = [](uint32_t k) -> uint32_t {
        static const char* from = "1234567890-=[]\\;',./`";
        static const char* to = "!@#$%^&*()_+{}|:\"<>?~";
        if (k >= 'a' && k <= 'z') return k - 'a' + 'A';
        if (k > 0 && k < 128)
            if (const char* p = std::strchr(from, int(k))) return uint32_t(uint8_t(to[p - from]));
        return k;
    };
    const auto isShifted = [](uint32_t k) { return (k >= 'A' && k <= 'Z') || (k > 0 && k < 128 && std::strchr("!@#$%^&*()_+{}|:\"<>?~", int(k))); };
    for (uint32_t key : queue_) {
        const bool prevShift = lastShifted_;
        lastShifted_ = isShifted(key);
        if (targets_.empty()) { ++ignoredKeys; continue; }
        Target* t = nullptr;
        if (lock_)
            if (auto it = targets_.find(lock_); it != targets_.end()) t = &it->second;
        const auto miss = [&](Target* m) {
            ++game.misses, ++chapter.misses;
            combo = 0;
            lastMissFrame = uint32_t(frame_);
            if (m && m->typed < m->text.size())
                for (TypingStats* st : {&game, &chapter}) {
                    ++st->keyMiss[uint8_t(low(uint8_t(m->text[m->typed]))) & 127];
                    ++st->wordMiss[m->text];
                }
            soundTo(audio, "miss");
        };
        if (!t) {
            float best = -1;
            for (auto& [o, x] : targets_) {
                if (x.text.empty() || !same(x.text[0], key)) continue;
                const float along = std::abs(x.x - (drillSpeed_[o] > 0 ? -kOut : kOut));
                if (along > best) best = along, t = &x;
            }
            if (!t) { miss(nullptr); continue; }
            lock_ = t->obj;
            lockFrame_ = frame_;
        } else if (!(same(t->text[t->typed], key) || (cfg_.shiftGrace && prevShift && shiftOf(key) != key && same(t->text[t->typed], shiftOf(key))))) {
            miss(t);
            t->missedThis = true;
            continue;
        }
        ++game.letters, ++chapter.letters;
        if (t->startMs < 0) t->startMs = nowMs_;
        ++t->typed;
        skipPunct(*t);
        soundTo(audio, "key");
        lastHitFrame = frame_, lastHitObj = t->obj;
        if (t->typed >= t->text.size()) {
            ++game.words, ++chapter.words, ++drillTyped;
            if (!t->missedThis) game.maxCombo = std::max(game.maxCombo, ++combo);
            soundTo(audio, "ding");
            drillSpeed_.erase(t->obj);
            targets_.erase(t->obj);
            lock_ = 0;
        }
    }
    queue_.clear();
    if (lock_) game.typingMs += tickMs, chapter.typingMs += tickMs;
    // the words float on; past the far side: lost
    for (auto it = targets_.begin(); it != targets_.end();) {
        Target& t = it->second;
        t.x += drillSpeed_[t.obj];
        if (std::abs(t.x) > kOut + 1.0f && (drillSpeed_[t.obj] > 0) == (t.x > 0)) {
            ++drillLost;
            ++drillLostWords[t.text];
            combo = 0;
            if (lock_ == t.obj) lock_ = 0;
            soundTo(audio, "miss");
            drillSpeed_.erase(t.obj);
            it = targets_.erase(it);
        } else
            ++it;
    }
    // the rule check: no two words up with the same first letter
    bool first[128] = {};
    for (const auto& [o, t] : targets_) {
        const uint8_t f = uint8_t(low(uint8_t(t.text.empty() ? ' ' : t.text[0]))) & 127;
        drillSameFirst += first[f];
        first[f] = true;
    }
    // what the HUD draws: each word in its lane; yellow on the last fifth of its way
    labels.clear();
    for (const auto& [o, t] : targets_) {
        Label l;
        l.obj = o;
        l.x = t.x, l.y = t.y;
        l.text = t.text;
        l.typed = t.typed;
        l.locked = o == lock_;
        const float along = std::abs(t.x - (drillSpeed_.at(o) > 0 ? -kOut : kOut)) / (2.0f * kOut);
        l.danger = along > 0.8f;
        l.lockAge = l.locked ? int(frame_ - lockFrame_) : 1000;
        l.hitAge = lastHitObj == o ? int(frame_ - lastHitFrame) : 1000;
        l.z = 50.0f;
        labels.push_back(l);
    }
    std::stable_sort(labels.begin(), labels.end(), [](const Label& a, const Label& b) { return a.locked > b.locked; });
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
    // totd8 (testing, HOTD_TOTD_LETTHROW=1, the coordinator 2026-11-05: "a thrown axe with no prompt: the typist always
    // typed the thrower first"): the throwers' words left alone 6 s (they throw: their axes / knives / barrels must
    // show their key), the crates' / locks' 1.5 s
    static const bool letThrow = std::getenv("HOTD_TOTD_LETTHROW") != nullptr;
    const auto heldOff = [&](const Typing::Label& l) {
        if (!letThrow)
            return false;
        const double age = nowMs_ - seenMs_[l.obj];
        const std::string n = nameOf(rt, rt.mem.u32(l.obj));
        if (l.kind == 0 && (n.find("throw") != std::string::npos || n.find("kind32") != std::string::npos ||
                            n.find("carry") != std::string::npos || n.find("iron_ball") != std::string::npos))
            return age < 6000.0;
        return l.kind == 3 && age < 1500.0;
    };
    const auto known = [&](const Typing::Label& l) { return nowMs_ - seenMs_[l.obj] >= react && !heldOff(l); };
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
