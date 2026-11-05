// The Typing of the Dead 0 (ours, worker totd0, user 2026-11-03): The House of the Dead played by typing, after SEGA's
// The Typing of the Dead (2000), Typing of the Dead 3 and Typing of the Dead: Overkill (2013). docs/typing.md.
//
// A layer over the arcade logic, like PC Mode and Sonic Mode: the game runs unchanged, and this class turns typed
// letters into player 1's light-gun input. Each enemy on the screen gets a prompt (a letter, a word, a phrase or a
// sentence, by its toughness, the chapter and the difficulty) in a box over it; the first letter typed locks onto the
// enemy with that first letter (the most dangerous one when several match), each further correct letter is a shot at
// it. The letters' shots hit (flinch, blood, part damage, points) but cannot kill: the enemy's hit points are held up
// for that one frame and put back after it; the last letter's shot is the killing shot (hit points 1 first). Wrong
// letters are misses (accuracy), they never drop the lock. Hostages / researchers get no prompt and no shot is ever
// fired where one covers the aim point. Bosses: a long sentence per attack, each finished sentence = boss_shots real
// shots at the weak point; a sentence not finished in its window is replaced (the window missed).
//
// Determinism: everything here runs between ticks from the game's own state and the keys queued for the tick, so a
// headless run with the scripted typist (Typist, --totd-test) plays the same frames every time; with the mode off
// nothing here runs at all (the arcade / PC Mode / Sonic Mode games stay byte-identical).
#pragma once

#include "game/runtime.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace hotd::game {

class Bot;

// data/typing/typing.ini (and a mod's copy over it). Arrays of 5: one value per difficulty setting (0 very easy,
// 1 easy, 2 normal, 3 hard, 4 very hard: the game's own Options > Difficulty)
struct TypingConfig {
    std::string dictionary = "default";
    bool autoSkipPunct = false;   // punctuation is passed over by itself (Overkill: typed)
    int dropLockKeys = 1;         // 0 nothing drops the lock (the SEGA games), 1 Escape, 2 Escape and Backspace
    int caseFromDifficulty = 3;   // from this difficulty capitals are typed as shown (Shift); below it case is ignored
    int comboBonus = 20, comboCap = 10;
    // totd1 (user 2026-11-03: "in TotD most normal enemies get a single word"): the word list (a file <name>.txt in
    // the dictionary folder) each kind of target takes, by difficulty
    std::string toughList[5] = {"easy", "words", "phrases", "hard_phrases", "hard_phrases"};
    std::string itemList[5] = {"easy", "easy", "words", "words", "long"};
    std::string smallList = "short";   // bats, Murrers, frogs, thrown things: 1-3 letters
    int smallMin[5] = {1, 1, 2, 2, 3}, smallMax[5] = {1, 2, 2, 3, 3};
    // totd2 (user 2026-11-03: "words and phrases are too long"; "a close or fast attacker gets a short word, a far, slow
    // walker a longer one"): the enemies' single words come from all of word_lists, by length: the letters (min, max)
    // for a near / mid / far threat by difficulty (Typing::bandOf: the spawn depth and how soon it would be at you)
    std::vector<std::string> wordLists = {"short", "easy", "words", "long"};
    int wordMin[5][3] = {{3, 3, 4}, {3, 4, 4}, {3, 4, 5}, {4, 5, 6}, {4, 6, 7}};
    int wordMax[5][3] = {{3, 4, 5}, {4, 5, 6}, {4, 5, 7}, {5, 7, 9}, {6, 8, 10}};
    int toughMin[5] = {4, 5, 7, 9, 11}, toughMax[5] = {6, 7, 11, 15, 17};   // the tough ones: tough_list by length
    float nearSeconds = 2.0f, farSeconds = 5.0f;   // the threat bands: at you within near_seconds; later than far_seconds
    float nearDepth = 30.0f, farDepth = 70.0f;     // ... or spawned closer than near_depth; farther than far_depth
    // the bosses' sentences: all of boss_lists, letters (min, max) by difficulty
    std::vector<std::string> bossLists = {"boss_easy", "boss", "boss_hard"};
    int bossMin[5] = {14, 16, 16, 22, 30}, bossMax[5] = {20, 22, 25, 34, 44};
    // thrown things fly at this share of their speed while they carry a prompt (TotD: an axe drifts at you)
    float projectileSpeed[5] = {0.3f, 0.35f, 0.4f, 0.5f, 0.6f};
    int projMin[5] = {1, 1, 1, 2, 2}, projMax[5] = {1, 1, 2, 2, 3};   // projectile_letters (from small_list)
    int toughHp = 200;            // hpMax >= this: the tough list
    int twoPromptsHp = 400;       // hpMax >= this (and not near): two prompts in a row (Overkill)
    int longerFromChapter = 5;    // from this chapter (1-4) enemies take the next difficulty's lists (5: never)
    std::vector<std::string> smallNames = {"bat_", "molb", "frog", "dog_"};   // small_names: routine name parts of small creatures
    std::vector<std::string> itemNames = {"frog", "gold", "coin", "life", "item_", "medic"};   // item_names (scenery)
    std::vector<std::string> routeNames = {"_lock", "padlock", "route_spot", "shootable_sign"};   // route_names (scenery, totd2)
    // the attack window (TotD: an enemy at you waits on a timer before it hits): seconds after it arrives (took an
    // attack place / came close), plus per letter of its prompt; and the least time between two attacks
    float attackWait[5] = {6.0f, 5.0f, 3.5f, 2.5f, 1.5f};
    float attackPerLetter[5] = {0.35f, 0.3f, 0.25f, 0.15f, 0.1f};
    float attackGap[5] = {4.0f, 3.0f, 2.0f, 1.2f, 0.6f};
    // totd2 (user 2026-11-03: "the grabbed researchers can't be saved"): a creature that reached the researcher it
    // chases waits rescue_wait + attack_per_letter x its prompt's letters (from the prompt shown) before its bite
    float rescueWait[5] = {6.0f, 5.0f, 4.0f, 3.0f, 2.5f};
    int bossShots = 3, bossShotGap = 6, bossDamageX = 3;
    // a boss sentence's window: base + per letter (seconds; 0.4 s a letter = 30 WPM)
    float bossWindowSeconds[5] = {5.0f, 4.0f, 3.0f, 3.0f, 2.0f}, bossWindowPerLetter[5] = {0.6f, 0.5f, 0.4f, 0.3f, 0.25f};
    struct Grade { char g; float acc, wpm; };
    std::vector<Grade> grades = {{'S', 98, 70}, {'A', 95, 55}, {'B', 90, 40}, {'C', 80, 28}, {'D', 65, 15}};
    // totd2 (user 2026-11-03: "a per-kill rating like the real Typing of the Dead"): each kill's grade from its
    // accuracy (%) and its speed (WPM from the moment it could be typed: the prompt shown or the last one done, to the
    // last letter: the reaction counts), best first; anything below the last: F
    std::vector<Grade> killGrades = {{'S', 100, 50}, {'A', 100, 36}, {'B', 85, 27}, {'C', 70, 19}, {'D', 50, 11}};
};

// one chapter's (or the game's) typing stats
struct TypingStats {
    uint32_t letters = 0;        // correct letters typed
    uint32_t misses = 0;         // wrong letters (and letters with nothing to lock onto)
    uint32_t words = 0;          // prompts finished
    uint32_t bossPrompts = 0;    // of which boss sentences
    uint32_t windowsMissed = 0;  // boss sentences not finished in time
    uint32_t maxCombo = 0;
    int chapterNo = 0;           // the game's chapter (1-4) of these results
    double typingMs = 0;         // time with a prompt locked (WPM's minutes)
    double fastestMs = 0;        // the fastest prompt, first letter to last (0: none yet)
    std::string fastestText;
    // totd2: the kills' grades (S A B C D F), and (the self-test's report) what the user's points ask about
    uint32_t killGrade[6] = {};
    uint32_t hits = 0;           // hits taken by player 1
    // bats / projectiles / the other small ones (frogs, dogs, Murrers): prompts shown, typed, killed (the game's own
    // state afterwards: gone, falling, knocked away, with no hit on the player from it)
    uint32_t bats[3] = {}, projectiles[3] = {}, small[3] = {};
    int rescueSeen = 0, rescued = 0;   // the chapter's people to rescue (hostages_appeared) and rescued
    bool bossSeen = false, bossBeaten = false;
    std::map<std::string, std::pair<uint32_t, uint32_t>> lengths;   // the prompts by enemy class: count, letters
    float accuracy() const { return letters + misses ? 100.0f * float(letters) / float(letters + misses) : 100.0f; }
    // words per minute (5 letters a word, the standard measure) over the locked time
    float wpm() const { return typingMs > 0 ? float(letters) / 5.0f / float(typingMs / 60000.0) : 0.0f; }
};

class Typing {
public:
    Typing();
    ~Typing();
    // the tuning and the dictionary from data/typing under each data folder (later folders, i.e. mods, override);
    // false: no prompts found (err says where it looked)
    bool load(const std::vector<std::string>& dataDirs, std::string& err, const std::string& dictionary = "");
    // a new game: difficulty 0..4 (the game's setting: very easy .. very hard), the prompts' seed (fixed: the runs repeat)
    void start(int difficulty, uint64_t seed = 0x70D0);
    int difficulty() const { return difficulty_; }
    // capitals typed as shown (difficulty >= case_from_difficulty)
    bool caseSensitive() const { return difficulty_ >= cfg_.caseFromDifficulty; }

    // input (the window's text events, or the Typist): one character (any case), Backspace / Escape (drop the lock;
    // true if one was dropped), queued for the next tick
    void typed(uint32_t ch) { queue_.push_back(ch); }
    bool cancel();
    bool locked() const { return lock_ != 0; }
    // the play phase: keys are taken only then (player 1 in play, shots allowed, no cut-scene / results / card)
    static bool inPlay(Runtime& rt);

    // before rt.frame(): the prompts on the screen, the queued keys, player 1's gun for this tick. tickMs: the tick's
    // real length (Sonic Mode: shorter), for the WPM / fastest times only (never for the game)
    void before(Runtime& rt, double tickMs);
    // after rt.frame(): the held hit points put back, kills / misses counted, chapter results
    void after(Runtime& rt);

    // what the HUD draws: the prompts over their enemies (arcade screen units, centre 0 / 0, y up)
    struct Label {
        uint32_t obj = 0;
        float x = 0, y = 0;      // the enemy's aim point
        std::string text;        // the whole prompt
        size_t typed = 0;        // letters done
        bool locked = false;
        int kind = 0;            // 0 enemy, 1 projectile / small creature (bat, Murrer, frog), 2 boss, 3 item
        int left = 0;            // prompts still to come after this one (tough enemies, bosses)
        bool urgent = false;     // a projectile / bat / small creature: its first letter interrupts a locked word
        float window = -1;       // a boss sentence: the time left (0..1), else -1
        float attack = -1;       // an enemy at you: the time left before it may attack (1..0), -1 none (not yet close)
    };
    std::vector<Label> labels;
    // totd2: the kills' grades shown for a moment where the enemy was (arcade screen units; boss: under its sentence)
    struct Popup { float x = 0, y = 0; char grade = 'F'; uint64_t frame = 0; bool boss = false; };
    std::vector<Popup> popups;
    uint64_t frame() const { return frame_; }
    uint32_t combo = 0;
    uint32_t lastMissFrame = 0;  // (the HUD: a red flash)
    TypingStats game, chapter;
    // the chapter results (rt.clear.active: the HUD's results panel), and every chapter's for the end
    std::vector<TypingStats> chapters;
    bool resultsShown = false;
    // (the self-test) the chapter so far as a result when it ends without a clear (Chapter 4: the Magician, the ending)
    void flush(Runtime& rt) { if (chapter.words || chapter.letters) chapterEnd(rt); }
    char grade(const TypingStats& s) const;
    char killGrade(float accuracy, float wpm) const;
    static int gradeIndex(char g)
    {
        const char* o = "SABCDF";
        for (int i = 0; i < 6; ++i)
            if (o[i] == g) return i;
        return 5;
    }
    const TypingConfig& config() const { return cfg_; }
    TypingConfig& config() { return cfg_; }
    size_t promptCount() const;
    std::string summary(const TypingStats& s) const;
    bool verbose = false;        // (the self-test: a line per prompt)
    bool trace_ = std::getenv("HOTD_TOTD_TRACE") != nullptr;   // developer: prompts given / dropped, locks, shots
    // (the self-test's checks) attacks begun on player 1 and the frames between them; small targets / projectiles
    // typed and how many of those were gone (killed / knocked away) within 30 frames of their last letter; the
    // prompts given per list (their letters: the mean length)
    uint32_t attacks = 0, hits = 0, attackGapMin = UINT32_MAX, smallTyped = 0, smallKilled = 0, smallMissed = 0, letterKills = 0;
    std::map<std::string, std::pair<uint32_t, uint32_t>> given;   // list -> prompts, letters
    // the attack hold (port::gAttackHold) for this tick: true = the creature's attack waits
    bool holds(uint32_t obj);
    // the boss hit hold (port::gHitHold): in a boss fight a hit on player 1 lands only after a missed window
    // totd2: outside a boss fight, a hit through player_hit lands only while a creature whose timer ran out is there
    // (some attacks never ask gAttackHold: the agile ones, the vomit, the taran): nothing hits before its timer
    bool holdsHit(int player) const;
    // the rescue hold (port::gPreyHold): a captor's bite on its researcher waits on its rescue timer
    bool holdsPrey(uint32_t obj);

private:
    struct Target {
        uint32_t obj = 0, routine = 0;
        int32_t serial = 0;
        int kind = 0;            // as Label::kind
        std::string text;        // the prompt shown (as written)
        size_t typed = 0;
        int left = 0;            // prompts after this one
        uint64_t born = 0;       // the frame it got the prompt (bosses: the window's start)
        uint64_t lastSeen = 0;   // the last frame it was on the screen as a target
        double startMs = -1;     // the first letter's time
        bool seen = false;       // on the screen this tick
        float x = 0, y = 0, threat = 0;
        int finishing = 0;       // the killing shot's tries left (0: not finishing)
        int finishShots = 0;     // killing shots fired
        int bossShotsLeft = 0;
        uint64_t nextBossShot = 0;   // the boss attack's next round (boss_shot_gap apart)
        uint64_t bossPendingUntil = 0;   // a boss attack without damage yet: more rounds until this frame
        bool missedThis = false; // a miss during this prompt (the combo)
        bool holding = false;    // the letters' shots began: the hit points held up (kHeldHp) until the prompt is done
        int16_t realHp = 0;      // the hit points before the hold (put back when the prompt is let go)
        bool small = false;      // a bat / Murrer / frog: a short word, no letter shots (any hit kills it), the last kills
        std::string list;        // the word list its prompt came from
        uint64_t arrive = 0;     // the frame it came at the player (an attack place, close by): its attack timer runs
        uint64_t readyAt = 0;    // the frame from which it may attack (0: not yet at the player)
        bool wentDown = false;   // (the self-test) a held enemy that went down before its prompt was done
        uint64_t landedAt = 0;   // a small target's / projectile's killing shot hit it (its flags' hit bit after the tick)
        // totd2
        double bornMs = 0;       // the prompt shown (the kill's grade: the time from here, or from the last prompt done)
        uint32_t lettersAll = 0, missesAll = 0;   // over all its prompts (the grade)
        int band = 1;            // the threat: 0 near, 1 mid, 2 far (the word's length)
        float z0 = 0;            // its depth when first seen
        bool slowed = false;     // a projectile slowed (projectile_speed)
        bool urgent = false;     // a projectile / small creature: interrupts a locked word
        std::string cls;         // its class for the report (zombie, tough, bat, projectile, small, boss, item)
        std::vector<std::string> pickLists;   // where its prompt came from (a re-pick: the same lists and lengths)
        int pickMin = 0, pickMax = 0;
        uint64_t preyReadyAt = 0;   // a captor: the frame from which its bite on the researcher may land
    };
    // totd2: a typed small target / projectile followed for a while: did it really go (killed, knocked away)
    struct Follow { uint32_t obj; int32_t serial; uint32_t routine; int16_t state; uint64_t until; uint32_t hitsAt; int what; };
    std::vector<Follow> follow_;
    std::map<uint32_t, std::pair<int32_t, uint64_t>> killed_;   // done with (obj -> serial, until frame): no new prompt
    uint32_t resume_ = 0;            // the word an interrupt (a projectile, a bat) broke into: locked again after it
    double freeSinceMs_ = 0;         // the last prompt done (a kill's grade counts from here at the earliest)
    int appearedAt_ = 0;             // hostages_appeared at the chapter's start
    int bandOf(Runtime& rt, Target& t);
    std::map<uint32_t, uint64_t> preyAsked_;   // captors without a prompt yet: the frame their hold was first asked
    bool isCaptor(Runtime& rt, uint32_t obj) const;
    bool peopleNow_ = false;
    std::map<uint32_t, std::pair<float, float>> preyHeldAt_;   // captors held at their researcher this tick: x, z                    // a researcher on the screen this tick (a rescue scene)
    std::map<uint32_t, uint32_t> people_;   // (HOTD_TOTD_TRACE) the people seen and their routines
    struct Shot { uint32_t obj; int32_t serial; bool hold; };   // hold: a letter's shot (cannot kill)
    TypingConfig cfg_;
    std::map<std::string, std::vector<std::string>> lists_;   // the dictionary's lists by name
    std::map<uint32_t, Target> targets_;   // by object
    uint32_t lock_ = 0;                     // the locked object (0: none)
    uint32_t bossPrimary_ = 0;              // the boss part whose sentence is shown (totd2: one at a time)
    std::vector<uint32_t> queue_;
    std::vector<Shot> shots_;
    std::mt19937_64 rng_;
    std::unique_ptr<Bot> classify_;         // the CPU teammates' target classes / threat (game/bot.h)
    int difficulty_ = 4;
    uint64_t frame_ = 0;
    double nowMs_ = 0;
    uint64_t bossDamageUntil_ = 0;   // a boss attack's damage x until this frame
    struct BossHp { uint32_t obj; int32_t serial; int16_t hp; };
    std::vector<BossHp> bossHp_;     // the bosses' hit points before the tick (a boss attack's window)
    int lastChapter_ = -1;
    bool clearWas_ = false;
    Runtime* rt_ = nullptr;          // (the attack hold during the tick)
    uint64_t lastAttack_ = 0;        // the frame the last attack on player 1 began
    uint8_t targetedWas_ = 0;        // player 1's "targeted" flag before the tick
    int32_t invulnWas_ = 0;
    uint32_t killShot_ = 0;          // the object a killing shot went at this tick (after(): did it land)
    bool bossLanded_ = true;         // the last boss attack did damage
    bool bossFight_ = false;         // a boss sentence is up (this tick)
    bool bossPunish_ = false;        // a boss window ran out: its next hit lands
    uint64_t lastBossFinish_ = 0;    // the frame the last boss sentence was typed
    // a prompt from the list `list` (letters minLen..maxLen; 0: any) whose first letter no other target's has (false:
    // none such, out = any of the list)
    bool pick(const std::vector<std::string>& lists, int minLen, int maxLen, uint32_t self, std::string& out);
    std::string newPrompt(Runtime& rt, Target& t);   // the target's next prompt (its list by kind, difficulty, chapter)
    bool isSmall(Runtime& rt, uint32_t obj) const;
    bool same(char want, uint32_t key) const;        // the key types this character (case as caseSensitive())
    uint64_t ticks(float seconds) const { return uint64_t(std::max(0.0f, seconds) * 60.0f); }
    bool skippable(char c) const;
    void skipPunct(Target& t) const;
    void finished(Runtime& rt, Target& t);
    bool fire(Runtime& rt, const Shot& s);
    void chapterEnd(Runtime& rt);
    void release(Runtime& rt, Target& t);
    void giveUp(Runtime& rt, Target& t);
};

// the scripted typist (--totd-test, testing): plays from what the HUD shows as a person would (totd2: "test the real
// keyboard input path"): a prompt is read a reaction time after it appears; projectiles and bats are typed as soon
// as read (they interrupt a locked word), else the one whose attack timer is lowest, else the most dangerous; a
// locked prompt gone from the screen for a second is dropped with Escape. It types at `wpm` with `accuracy` (0..1,
// a wrong key is a neighbour on the keyboard), from a seed. Its keys go out through `press` (main.cpp: SDL key / text
// events into the window's own event handling); 27 = Escape. One call per tick before Typing::before
class Typist {
public:
    Typist(uint64_t seed, float wpm, float accuracy);
    void tick(Runtime& rt, Typing& t, double tickMs, const std::function<void(uint32_t key)>& press);
private:
    std::mt19937_64 rng_;
    float wpm_, acc_;
    double carry_ = 0, nowMs_ = 0;
    uint64_t waitUntil_ = 0, f_ = 0;
    std::map<uint32_t, double> seenMs_;   // when each prompt (object) was first shown
    double lostMs_ = -1;                  // the locked prompt off the screen since
};

} // namespace hotd::game
