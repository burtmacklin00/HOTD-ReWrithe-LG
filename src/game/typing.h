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
#include <set>
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
    int smallMin[5] = {1, 1, 1, 1, 2}, smallMax[5] = {1, 1, 1, 2, 2};
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
    float projectileSpeed[5] = {0.25f, 0.28f, 0.3f, 0.4f, 0.5f};
    int projMin[5] = {1, 1, 1, 1, 2}, projMax[5] = {1, 1, 1, 2, 2};   // projectile_letters (from small_list)
    int toughHp = 200;            // hpMax >= this: the tough list
    int twoPromptsHp = 400;       // hpMax >= this (and not near): two prompts in a row (Overkill)
    int longerFromChapter = 5;    // from this chapter (1-4) enemies take the next difficulty's lists (5: never)
    std::vector<std::string> smallNames = {"bat_", "molb", "frog", "dog_", "taran"};   // small_names: routine name parts of small creatures
    std::vector<std::string> itemNames = {"frog", "gold", "coin", "life", "item_", "medic"};   // item_names (scenery)
    std::vector<std::string> routeNames = {"_lock", "padlock", "route_spot", "shootable_sign"};   // route_names (scenery, totd2)
    // the attack window (TotD: an enemy at you waits on a timer before it hits): seconds after it arrives (took an
    // attack place / came close), plus per letter of its prompt; and the least time between two attacks
    float attackWait[5] = {2.2f, 1.9f, 1.6f, 1.3f, 1.1f};   // (totd5: +0.6 s: the wind-up now plays within it)
    float attackPerLetter[5] = {0.5f, 0.4f, 0.3f, 0.2f, 0.16f};
    float smallWait[5] = {1.6f, 1.4f, 1.0f, 0.75f, 0.6f};   // totd6: a small one's (bat, Murrer, frog) reading time
    float smallQueue[5] = {0.6f, 0.45f, 0.35f, 0.3f, 0.25f};   // totd8: the small ones' graces queue, this a key
    float attackGap[5] = {1.5f, 1.2f, 0.8f, 0.5f, 0.3f};
    // totd6: the graces queue (one word after another, as a typist types them): a creature's grace starts no sooner
    // than the latest one running ends, plus this (seconds; < 0: no queue, totd5's graces all from the same moment)
    float attackQueue[5] = {0.5f, 0.45f, 0.4f, 0.3f, 0.2f};
    // totd6: after a hit the creatures whose grace ran out wait this long again (TotD: the zombie steps back)
    float hitReset[5] = {3.0f, 2.5f, 2.0f, 1.5f, 1.0f};
    // totd3 (user 2026-11-03: "enemies feel awkward waiting for you; in TotD they keep the game's own pace"):
    // attack_hold 0: no hold at all (the arcade's timing; a 60 WPM typist lost every life by Chapter 1's second
    // scene: creatures appear at arm's length and strike within 0.9 s); 1: totd2's window (from the arrival, seconds
    // long, the creature stood waiting with a bar); 2 (default): a short grace from the prompt shown (attack_wait +
    // attack_per_letter x its letters: a reading time and the word), counted while it walks up, so most creatures
    // attack on arrival at their own pace and only one that appears at arm's length waits a moment; no bar. Bosses:
    // a hit lands after a sentence's window ran out (as 1). The whole game a little slower on easy (game_speed)
    int attackHold = 2;
    float gameSpeed[5] = {0.85f, 0.92f, 1.0f, 1.0f, 1.0f};
    // a boss (one of its parts with hit points) loses 1 / boss_sentences of its hit points per sentence typed, on or
    // off the screen (the shots after it give its own hit reaction; the last one's its own death)
    int bossSentences[5] = {5, 6, 7, 8, 9};
    // creatures that get no prompt and are shot by the typing layer by themselves (auto_names: routine name parts;
    // the frogs, the Ebitans hanging from the ceiling), a moment after they show (auto_delay seconds)
    std::vector<std::string> autoNames = {"ebitb", "hanging"};   // (totd5: the frogs typed again)
    float autoDelay = 0.4f;
    // totd5: a creature standing far off (not coming, no attack place, beyond far_depth) gets its word only after
    // nothing else was up for idle_seconds while enemies are still counted alive (the event may wait on it)
    float idleSeconds = 4.0f;
    // totd5: seconds of boss action (no sentence up) after each boss sentence typed
    float bossRest[5] = {6.0f, 5.5f, 5.0f, 4.5f, 4.0f};
    // totd6: the Magician's barrage shots with a word at once (the rest fly without one), per difficulty
    int barrageWords[5] = {2, 2, 3, 3, 4};
    // totd6: an iron ball's throw flies at this share of its speed from its first sight (it is thrown from close by)
    float ballSpeed[5] = {0.1f, 0.12f, 0.15f, 0.2f, 0.25f};
    // totd2 (user 2026-11-03: "the grabbed researchers can't be saved"): a creature that reached the researcher it
    // chases waits rescue_wait + attack_per_letter x its prompt's letters (from the prompt shown) before its bite
    float rescueWait[5] = {6.0f, 5.0f, 4.0f, 3.0f, 2.5f};
    int bossShots = 3, bossShotGap = 6, bossDamageX = 3;
    // totd8: a boss strike let go after a missed window: its hits land for up to this long (or until one did)
    float bossStrikeSeconds = 6.0f;
    // totd8: a free strike (in the rest after a sentence, at the boss's own pace, boss_strike_gap seconds apart at most)
    // is blocked by the first boss_block_letters letters of the sentence that comes with it (0: no free strikes)
    float bossStrikeGap[5] = {9.0f, 8.0f, 7.0f, 6.0f, 5.0f};
    int bossBlockLetters[5] = {1, 2, 2, 3, 3};
    float bossBlockWarn[5] = {1.5f, 1.2f, 0.8f, 0.6f, 0.4f};   // the sentence this long before the free strike begins
    // totd8: a creature's box centre this share down its body from the top of its head (the PC TotD: 0.53); 0: on
    // the object's centre (totd2-7)
    float boxAnchor = 0.53f;
    // totd8 (the PC TotD: its last bosses take about 16 sentences, the others about 9): the Magician's sentences x this
    float bossFinalX = 1.7f;
    bool fireballWords = true;   // totd8: the Magician's fireballs carry a word (the PC TotD), not a G / H key
    std::map<std::string, float> bossX = {{"chariot", 1.15f}};   // totd8: boss_x_<routine name part> = sentences x
    // a boss sentence's window: base + per letter (seconds; 0.4 s a letter = 30 WPM)
    float bossWindowSeconds[5] = {5.0f, 4.0f, 3.0f, 3.0f, 2.0f}, bossWindowPerLetter[5] = {0.6f, 0.5f, 0.4f, 0.3f, 0.25f};
    struct Grade { char g; float acc, wpm; };
    std::vector<Grade> grades = {{'S', 98, 70}, {'A', 95, 55}, {'B', 90, 40}, {'C', 80, 28}, {'D', 65, 15}};
    // totd2 (user 2026-11-03: "a per-kill rating like the real Typing of the Dead"): each kill's grade from its
    // accuracy (%) and its speed (WPM from the moment it could be typed: the prompt shown or the last one done, to the
    // last letter: the reaction counts), best first; anything below the last: F
    std::vector<Grade> killGrades = {{'S', 100, 50}, {'A', 100, 36}, {'B', 85, 27}, {'C', 70, 19}, {'D', 50, 11}};
    // totd4 (user 2026-11-04: "a typewriter key sound, a ding for a word, another sound for a wrong letter"): the
    // sounds, each data/typing/<key|miss|ding>.wav when there (a mod's or the user's file), else the PC game's own
    // sound: a PC cue number of THOTD.EXE's table (docs/typing.md "Sounds"). key_cue 28: the 1998 PC's name entry key
    // sound (screen 15's Enter / Space click, sample 13, also the reload click); miss_cue 253: a short low buzz
    // (sample 60); ding_cue 140: the coin / life item chime (O_LIFEUP), until the user's ding.wav. -1: none
    bool sounds = true;
    int keyCue = 28, missCue = 253, dingCue = 140;
    float keyVolume = 4.0f, missVolume = 4.0f;   // (totd5: up to 4, a gain over the peak; totd7: 4, the gun shot's level)
    // totd5 (user 2026-11-04: "I could barely hear the typing sounds and could not tell what they were"): each sound's
    // peak brought to sound_peak of full scale (a user's own file too), then its volume; the ding's own volume
    float soundPeak = 0.95f, dingVolume = 2.0f;
    // totd7 (user 2026-11-05: "every typed letter fires the gun again with its normal shot sound"): a letter whose shot
    // is not fired (a small one, a thrown thing, an item, a boss sentence, a tick's second letter) plays the arcade
    // gun's shot (shot_sound: player 1's sound code 0xA01208) by itself; 0 none
    uint32_t shotSound = 0xA01208;
    // totd7 (user 2026-11-05: "quick attacks use one key by screen side, G for the left half, H for the right"): thrown
    // things, the small quick creatures, crates / barrels / props and the rescue locks / keypads get no word but a key
    // (quick_left / quick_right); no word starts with either. quick_keys = 0: words for them as before
    bool quickKeys = true;
    char quickLeft = 'g', quickRight = 'h';
    // totd7 (user 2026-11-05: "in TotD 2 creatures packed together, like the sewer slugs, shared one word"): creatures
    // of the same routine within group_dx / group_dy screen units (the arcade's 496-wide screen) and group_dz of depth of
    // one with a word join it (up to group_max in all): its word kills them all; crates / barrels side by side share
    // one key likewise (prop_group_dx / dy / dz). group_max 1: no groups
    float groupDx = 34.0f, groupDy = 44.0f, groupDz = 10.0f;
    float propGroupDx = 70.0f, propGroupDy = 40.0f, propGroupDz = 16.0f;
    int groupMax = 4;
    // totd7 (user 2026-11-05: "a golden frog / bonus item from a broken crate is collected automatically"): pickups
    // (item_names, not the props holding them) are shot by the layer pickup_delay seconds after they show, no key
    float pickupDelay = 0.25f;
    // totd7 (user 2026-11-05: "maybe a pixel shadow on the borders, I would have to see it"): the boxes' one-pixel drop
    // shadow (box_shadow; on: the user picked it 2026-11-05)
    bool boxShadow = true;
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
    uint32_t items[3] = {};      // totd5: items / props with an item / locks: shown, typed, broken or taken (routine changed / gone)
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
        bool danger = false;     // totd4: at you / may strike now (a thrown thing; a boss sentence running out): yellow
        int hitAge = 1000;       // totd4: ticks since its last correct letter (the box's hit flash)
        int lockAge = 1000;      // totd5: ticks since it was locked (the lock-on burst)
        float z = 0;             // totd5: its depth (a far one's box sits above it, not over it)
        // totd6 (Smooth Motion): x / y are the aim point before the tick (the blended picture's alpha 0); dx / dy its
        // step over the tick (after()), so the HUD draws it at x + alpha dx with the picture's own blend
        float dx = 0, dy = 0;
        bool quick = false;      // totd7: a quick target: one key by screen side (text "g" / "h"), a key-cap box
        int group = 1;           // totd7: the creatures (props) its word takes down (a group: x / y its middle)
        float top = 0, bottom = 0;   // totd8: its body's top (the head) and bottom, arcade screen units (equal: not known)
    };
    std::vector<Label> labels;
    // totd2: the kills' grades shown for a moment where the enemy was (arcade screen units; boss: under its sentence)
    struct Popup { float x = 0, y = 0; char grade = 'F'; uint64_t frame = 0; bool boss = false; };
    std::vector<Popup> popups;
    uint64_t frame() const { return frame_; }
    uint32_t combo = 0;
    uint32_t lastMissFrame = 0;  // (the HUD: a red flash)
    uint64_t lastHitFrame = 0;   // totd4: the last correct letter's tick and its object (the box's hit flash)
    uint32_t lastHitObj = 0;
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
    // totd8: the boss strike hold (port::gBossHold): true = the boss's strike waits (no window missed yet)
    bool holdsBoss(uint32_t obj);
    // totd8 (user 2026-11-05: "the text boxes must not block the zombies too much"): a creature's body as the shot
    // test sees it: its parts' hit spheres projected (arcade screen units: x, y, radius); none: false
    struct Circle { float x, y, r; };
    static bool bodyCircles(Runtime& rt, uint32_t obj, std::vector<Circle>& out);
    // totd5: a projectile reaching the screen (Runtime::projectile_hit_player): true = it was typed, no hit
    bool holdsProjectile() const;
    // totd5 (the report): the hits on player 1 by the routine that made them; typed things that reached the screen
    // without a hit
    std::map<std::string, uint32_t> hitFrom;
    mutable uint32_t blocked_ = 0;

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
        // totd3
        int bossGoal = -1;          // a boss part's hit points after its sentences so far (-1: none set); 0: the next shot kills
        bool autoShot = false;      // a frog / a hanging Ebitan: no prompt, the layer shoots it (auto_names)
        uint64_t nextAuto = 0;      // its next shot
        // totd5
        uint64_t nextStrike = 0;    // a word done whose shot did not land: the next direct hit (no more shots fired)
        int strikes = 0;
        // totd7
        bool quick = false;         // one key by screen side (TypingConfig::quickKeys)
        uint32_t leader = 0;        // in a group: the target whose word it shares (0: none, its own)
        bool pickup = false;        // a pickup collected by the layer (pickup_delay)
        uint64_t offSince = 0;      // not shootable since (its word goes after kNoShootFrames)
        uint64_t hiddenUntil = 0;   // a box gone off the screen (its latch over) stays gone until this frame (no blink back)
    };
    const bool noLatch_ = std::getenv("HOTD_TOTD_NOLATCH") != nullptr;   // (testing: round 6's boxes, the flicker comparison)
    // totd7 (the flicker check): each box's last frame shown, its target's serial and text
    struct Shown { int32_t serial = 0; uint64_t last = 0; std::string text; uint64_t flipAt = 0; };
    std::map<uint32_t, Shown> shown_;
    // totd5 (user 2026-11-04: "a destroyed / typed projectile can never damage the player"): the things typed to their
    // end (projectiles, small creatures): obj -> serial, until frame; their hits are held (holdsHit, holdsProjectile)
    std::map<uint32_t, std::pair<int32_t, uint64_t>> neutral_;
    bool harmlessNow() const;
    bool graceHolds(const char* via) const;
    uint64_t idleSince_ = 0;           // no word up since this frame (0: one is up)
    uint64_t lastHit_ = 0;             // the last hit on player 1 (attack_gap between hits)
    uint64_t lockFrame_ = 0;           // the frame lock_ was taken (the lock-on burst)
    uint32_t lockWas_ = 0;
    uint64_t bossRestUntil_ = 0;       // totd5: the boss acts (no sentence) until this frame after a sentence typed
    void noteHit(const char* via) const;
    mutable std::string tickHitVia_;   // the routine whose hit got through this tick
    void strike(Runtime& rt, Target& t);   // totd5: the landed shot's hit given directly (no shot)
    // totd2: a typed small target / projectile followed for a while: did it really go (killed, knocked away)
    struct Follow { uint32_t obj; int32_t serial; uint32_t routine; int16_t state; uint64_t until; uint32_t hitsAt; int what; };
    std::vector<Follow> follow_;
    // totd3: the thrown things carrying a prompt, where they were before the tick (after it: only projectile_speed of
    // the step is kept, for any projectile routine: the Magician's fireballs, the knives, the spit)
    struct ProjPos { uint32_t obj; int32_t serial; float x, y, z, k; };   // (k: the share of the step kept)
    std::vector<ProjPos> projPos_;
    std::map<uint32_t, std::pair<float, float>> labelAt_;   // totd6: each box's aim point before the tick (Label::dx)
    std::map<uint32_t, std::pair<int32_t, uint64_t>> killed_;   // done with (obj -> serial, until frame): no new prompt
    uint32_t resume_ = 0;            // the word an interrupt (a projectile, a bat) broke into: locked again after it
    double freeSinceMs_ = 0;         // the last prompt done (a kill's grade counts from here at the earliest)
    int appearedAt_ = 0;             // hostages_appeared at the chapter's start
    int bandOf(Runtime& rt, Target& t);
    std::map<uint32_t, uint64_t> preyAsked_;   // captors without a prompt yet: the frame their hold was first asked
    bool isCaptor(Runtime& rt, uint32_t obj) const;
    int bossSentencesFor(Runtime& rt, uint32_t obj) const;   // totd8: boss_sentences, the last boss's x boss_final_x
    bool isBallThrow(Runtime& rt, uint32_t obj) const;   // totd6: an iron ball flying at the player
    bool peopleNow_ = false;
    std::map<uint32_t, std::pair<float, float>> preyHeldAt_;   // captors held at their researcher this tick: x, z                    // a researcher on the screen this tick (a rescue scene)
    std::map<uint32_t, uint32_t> people_;   // (HOTD_TOTD_TRACE) the people seen and their routines
    struct Shot { uint32_t obj; int32_t serial; bool hold; };   // hold: a letter's shot (cannot kill)
    TypingConfig cfg_;
    // totd4: the sounds' folders (the last data folder with data/typing/<name>.wav, else the game's data/typing)
    std::map<std::string, std::string> soundDir_;
    std::set<std::string> soundSaid_;
    void sound(Runtime& rt, const char* name);   // "key", "miss", "ding"
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
    uint64_t bossSeenAt_ = 0;        // totd8: the last frame a boss target was seen
    uint32_t bossStriker_ = 0;       // totd8: the boss whose strike was let go (its hits land until bossStrikeUntil_)
    uint64_t bossStrikeUntil_ = 0;
    uint64_t bossUnseenAsk_ = 0;     // totd8: the first strike ask while no boss was seen (held up to 4 s)
    uint64_t nextFreeStrike_ = 0, blockFrom_ = 0, blockShotFrom_ = 0, blockShotUntil_ = 0;   // totd8: free strikes / blocks
    uint32_t blockObj_ = 0;          // the boss whose free strike its sentence's first letters block
    uint64_t warnUntil_ = 0;         // totd8: a free strike announced (its sentence up), held until this frame
    uint64_t staggerAt_ = 0;         // totd8: a blocked strike's stagger shot at this frame
    bool strikeBlocked_ = false;
    mutable bool strikeLanded_ = false;   // (this tick: the striker's hit let through)
    uint64_t lastBossFinish_ = 0;    // the frame the last boss sentence was typed
    uint64_t bossDownUntil_ = 0;     // totd3: a boss part killed by its last sentence: no boss prompts until this frame
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
    // totd7: a group member's leader (null: none / gone); a finished word's members taken down with it
    Target* leaderOf(const Target& t);
    void finishMembers(Runtime& rt, Target& leader);
    void shotSound(Runtime& rt);   // totd7: the gun's shot sound for a letter whose shot is not fired at once
public:
    // totd7 (the reports): quick keys typed, G / H with nothing on that side (ignored), keys with nothing up (ignored),
    // groups made, members taken down with their group's word, pickups collected by the layer
    uint32_t quickTyped = 0, quickIgnored = 0, ignoredKeys = 0, groupsMade = 0, groupKills = 0, pickupsAuto = 0;
    // totd7: strikes whose hit was held (the animation hit, no damage): a creature's with its word still up, a boss's
    mutable uint32_t heldCreature = 0, heldBoss = 0, heldInFight = 0;
    // totd8: boss strikes let go (a window missed) and landed; frames a strike was held; asks not held (no boss seen 2 s)
    uint32_t bossStrikes = 0, bossStrikesLanded = 0, bossHeldFrames = 0, bossUnheld = 0;
    uint32_t bossFreeStrikes = 0, bossBlocks = 0;   // totd8: free strikes let go, blocked by their sentence's first letters
    // totd8 (the prompt check): thrown things seen on the screen, prompted within 20 frames, later, never (by 20 frames)
    uint32_t projSeen = 0, projPrompted = 0, projLate = 0, projNever = 0;
    struct ProjSeen { uint64_t first = 0; int32_t serial = -1; int state = 0; };
    std::map<uint32_t, ProjSeen> projSeen_;
    mutable uint32_t blockedHits = 0;               // the hits those blocks held
    // totd7 (the flicker check): a box gone and back within 60 frames for the same target (a blink), a quick key's
    // letter changing (G / H / G), a box back after a longer gap (a word taken back and given again)
    uint32_t blinks = 0, keyFlips = 0, longGaps = 0;
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
