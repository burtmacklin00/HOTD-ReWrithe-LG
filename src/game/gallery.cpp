#include "game/gallery.h"
#include "game/gallery_cut.h"

#include "assets/creature_pose.h"
#include "assets/camera_paths.h"
#include "assets/sprites_pc.h"
#include "render/gl.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <sstream>

namespace hotd::game {

const char* galleryCategoryName(int c)
{
    static const char* const kNames[kGalCategories] = {"Players", "Humans", "Creatures", "Bosses", "Items", "Cut Content"};
    return c >= 0 && c < kGalCategories ? kNames[c] : "";
}

const std::vector<GalleryEntry>& galleryEntries()
{
    // Our own short lines (neutral descriptions, nothing quoted from either game or any other release). Names: the
    // official ones of docs/names.md (the Saturn manual / guidebook names, with the type codes); the developers' names
    // where none is known (said so). Chapters: where the game draws the type (HOTD_TRACE_TYPE_ROWS runs of Chapters 1-4,
    // the default route, 2 players, 2026-10-21) and the event scripts' spawn records (EVTALL.RBH). Hit points: the spawn
    // records' +0x22 (decompilation.md "Enemy spawn records": the standard enemies' life), the bosses' records / fights.
    static const std::vector<GalleryEntry> kEntries = {
        // ---- Players ----
        {"t2", "Thomas Rogan", kGalPlayers, 2, 0, "MO_p_ MO_tom", "Every chapter (player 1)", "",
         "AMS agent, player 1. Comes to the Curien mansion after a call for help."},
        {"t13", "G", kGalPlayers, 13, 0, "MO_gman MO_p_", "Every chapter (player 2)", "",
         "AMS agent, player 2, Rogan's partner on the case."},
        {"t14", "Rogan (second look)", kGalPlayers, 14, 1, "MO_p_ MO_tom", "Not in the story",
         "PC Mode character 3; one of the arcade's title-code looks",
         "Rogan's alternative skin (developer name BO_haride)."},
        {"t15", "G (second look)", kGalPlayers, 15, 0, "MO_gman MO_p_", "Not in the story",
         "PC Mode character 4; one of the arcade's title-code looks",
         "G's alternative skin (developer name BO_staje)."},
        // ---- Humans ----
        {"t34", "Sophie Richards", kGalHumans, 34, 0, "MO_s_", "Chapter 1 and the ending", "",
         "DBR researcher held in the mansion; Rogan came for her."},
        // galact (user 2026-10-28): the zombie Sophie (67, BO_zsophi) as her own entry; her own motion only (MO_s_zonbi).
        // galfix3: where the arcade draws her: the ending's event 6 (event list 6; event 3's branch by route_choice 2 =
        // kind122_ending_score: no continue, below the top rank card; 0 -> event 4, 1 -> event 5 with Sophie 34), in
        // texture area 2 (kind111_texture_area, ENTB2 118044) with colour row 13:92 (GGCOL 6 92): galleryLook. The PC
        // files have no zombie Sophie: with PC models she is Sophie's model
        {"t67", "Sophie (zombie)", kGalHumans, 67, 3, "MO_s_zonbi", "The ending",
         "Developer name BO_zsophi. PC models: Sophie's model",
         "Sophie after the creatures got to her: the ending's darkest outcome. The PC files have no zombie Sophie."},
        {"t60", "Dr. Roy Curien", kGalHumans, 60, 2, "MO_kyu", "Chapter 3", "",
         "The scientist whose research filled the mansion with creatures."},
        {"t17", "Researcher", kGalHumans, 17, 0, "MO_k_", "Chapters 1, 2", "Shooting a researcher costs a life",
         "DBR scientist, one of the staff to rescue from the creatures."},
        {"t19", "Researcher (2)", kGalHumans, 19, 1, "MO_k_", "Chapters 1, 2", "Shooting a researcher costs a life",
         "DBR scientist: the same body as the others, another head."},
        {"t20", "Researcher (3)", kGalHumans, 20, 1, "MO_k_", "Chapters 1, 2", "Shooting a researcher costs a life",
         "DBR scientist: the same body as the others, another head."},
        {"t21", "Researcher (4)", kGalHumans, 21, 0, "MO_k_", "Chapters 1-3", "Shooting a researcher costs a life",
         "DBR scientist: the same body as the others, another head."},
        {"t23", "Researcher (woman)", kGalHumans, 23, 1, "MO_k_", "Chapter 2", "Shooting a researcher costs a life",
         "DBR scientist, one of the staff to rescue from the creatures."},
        {"t25", "Researcher (woman, 2)", kGalHumans, 25, 1, "MO_k_", "Chapters 2, 3", "Shooting a researcher costs a life",
         "DBR scientist: the same body as the others, another head."},
        // types 23 / 25-27 are women's bodies: named as in the cut-scene viewer (scene_view.cpp actors)
        {"t26", "Researcher (woman, 3)", kGalHumans, 26, 0, "MO_k_", "Chapters 1, 2", "Shooting a researcher costs a life",
         "DBR scientist: the same body as the others, another head."},
        {"t27", "Researcher (woman, 4)", kGalHumans, 27, 1, "MO_k_", "Chapters 1-3", "Shooting a researcher costs a life",
         "DBR scientist: the same body as the others, another head."},
        // ---- Creatures ----
        {"t11", "Sam", kGalCreatures, 11, 0, "MO_z_walk MO_z_kamae MO_z_naguri MO_z_hit MO_z_dead", "Chapters 1-4",
         "Type A-1. Hit points 100 (a few have 120 or 160)", "The plain zombie: slow, straight at you."},
        {"t28", "Neil", kGalCreatures, 28, 0, "MO_z_walk MO_z_kamae MO_z_naguri MO_z_hit MO_z_dead", "Chapters 1-3",
         "Type A-2. Hit points 120 (a few have 100 or 140)", "A zombie with a quicker reach."},
        {"t29", "Cyril", kGalCreatures, 29, 0, "MO_z_ono MO_onozonbia", "Chapters 1-3", "Type A-3. Hit points 160",
         "Carries axes and throws them."},
        {"t24", "Kageo", kGalCreatures, 24, 0, "MO_z_kag", "Chapter 1 (variants in Chapter 3)",
         "Type B-1. Hit points 90 to 100", "A wrapped creature of the lower floors."},
        {"t57", "Bourbon", kGalCreatures, 57, 0, "MO_z_kag", "Chapter 1 (variants in Chapter 3)",
         "Type B-2. Hit points 150 (a few have 120)", "A heavier relative of Kageo."},
        {"t3", "Ebitan A", kGalCreatures, 3, 0, "MO_z_ebi MO_ebit", "Chapters 1-3", "Type B-3. Hit points 140",
         "A crawling creature that springs up close."},
        // bugs2 (GitHub #4): the official names are Ebitan A / B / C (B: the hanging one); Ebitan C is type 46
        // (BO_pdolob), drawn in Chapter 1's waterway (event 11: HOTD_TRACE_TYPE_ROWS, area 1)
        {"t4", "Ebitan B", kGalCreatures, 4, 1, "MO_ebitb", "Chapters 1, 2", "Type B-4",
         "Hangs from ceilings and drops down."},
        {"t46", "Ebitan C", kGalCreatures, 46, 0, "", "Chapter 1 (the waterway)", "Developer name BO_pdolob",
         "The third Ebitan, met only in the mansion's underground waterway."},
        {"t30", "Samson", kGalCreatures, 30, 1, "MO_z_chn", "Chapters 1, 2", "Type C-1. Hit points 250 (a few have 150)",
         "Swings a chainsaw."},
        {"t33", "Bentley", kGalCreatures, 33, 1, "MO_z_ben", "Chapter 2 (variants in Chapters 3, 4)", "Type C-2. Hit points 200",
         "A large zombie that hurls what it finds."},
        {"t84", "Burner", kGalCreatures, 84, 2, "MO_z_burn", "Chapter 3", "Type C-3. Hit points 220",
         "Attacks with a blowtorch."},
        {"t16", "Simon", kGalCreatures, 16, 0, "MO_z_saim", "Chapters 1, 2",
         "Type D-1. Hit points 180 to 200", "A zombie of the outer grounds."},
        {"t32", "Gilmore", kGalCreatures, 32, 1, "MO_z_han", "Chapter 2 (variants in Chapter 3)", "Type D-2. Hit points 220",
         "Carries a heavy hammer."},
        // (galfix: shown as his variant BO_harisb, type 47, the body the game draws: type 31's own models never appear
        // and their textures fit no texture area, the arcade look was a broken one)
        {"t31", "Harris", kGalCreatures, 47, 2, "MO_z_tekk MO_tekk", "Chapters 3, 4", "Type D-3",
         "Swings a ball and chain. This is the variant the game draws (BO_harisb)."},
        {"t53", "Robert", kGalCreatures, 53, 2, "MO_z_tetu", "Chapters 3, 4",
         "Type D-4. Hit points 210 (a few have 100 to 220)", "A zombie with an iron arm."},
        {"t18", "Rubin", kGalCreatures, 18, 1, "MO_dr_", "Chapter 2", "Type E-1", "A clawed creature, quick to close in."},
        {"t37", "Drake", kGalCreatures, 37, 1, "MO_dr_", "Chapter 2", "Type E-2", "Fast, throws knives."},
        {"t59", "Moody", kGalCreatures, 59, 2, "MO_mood", "Chapters 3, 4", "Type E-3", "Moves along walls and ceilings."},
        {"t0", "Kenfis", kGalCreatures, 0, 0, "MO_ddog MO_dog MO_inu", "Chapter 1", "Type F-1",
         "An undead dog; comes in packs."},
        {"t1", "Saruzou", kGalCreatures, 1, 0, "MO_saru MO_monkabe", "Chapter 1", "Type F-2",
         "An ape creature that climbs and leaps."},
        {"t7", "Bouere", kGalCreatures, 7, 0, "MO_frog", "Chapter 1", "Type F-3", "A giant frog."},
        {"t6", "Murrer", kGalCreatures, 6, 0, "MO_mola MO_molj MO_mol", "Chapters 1, 2", "Type G-1",
         "A segmented worm that burrows."},
        {"t48", "Taran", kGalCreatures, 48, 2, "MO_taraba", "Chapter 3", "Type G-3", "A spider-like creature."},
        {"t83", "Taalu", kGalCreatures, 83, 2, "", "Chapter 3", "Developer name (no official one known). Hit points 150",
         "A Chapter 3 creature; this is the variant the game draws (BO_taalub)."},
        {"t38", "Kage", kGalCreatures, 38, 1, "", "Chapter 2", "Developer name (no official one known)",
         "A monkey-like body. Not matched to an official name yet."},
        // bugs2 (#4): Parlor (E-4) = BO_kage2 ("kageta"): Chapters 3 / 4 and its own dagger-of-light models
        // (kagetabeem), as the official list describes him (Drake's moves, endless laser daggers, a bite)
        {"t68", "Parlor", kGalCreatures, 68, 2, "", "Chapters 3, 4", "Type E-4. Developer name BO_kage2",
         "Moves like Drake, but tougher; throws daggers of light without end and may bite."},
        {"t41", "Neopetit", kGalCreatures, 41, 2, "", "Chapters 1-3", "Developer name (no official one known)",
         "One of the small background creatures (three kinds)."},
        // bugs2 (#4): the other two background creatures (kind 96, BO_neopetit2 / 3), drawn in Chapter 1 (traced)
        {"t42", "Neopetit 2", kGalCreatures, 42, 0, "", "Chapters 1-3", "Developer name BO_neopetit2",
         "The second of the small background creatures."},
        {"t43", "Neopetit 3", kGalCreatures, 43, 0, "", "Chapters 1-3", "Developer name BO_neopetit3",
         "The third of the small background creatures."},
        // bugs2 (#4): two creatures that are no skeleton type: Devilon (F-4) = the bats (kinds 66-68, wing frames 0x54..0x67;
        // traced in Chapter 2, area 6), Name (G-2) = the slugs of Chapter 1's waterway (kind 141 crawls: models 0x971..,
        // kind 142 spits: 0x99E..; traced at Chapter 1 event 11, area 1)
        {"devilon", "Devilon", kGalCreatures, -1, 1, "", "Chapters 1, 2, 4", "Type F-4. Motion shows its wing frames",
         "Bats that fly at the player in groups and die when they hit; a swarm comes with the Hangedman.", "54 57 5A 5D 60 63",
         6, "5:66 7:81 9:28 11:61 13:12"},
        {"name", "Name", kGalCreatures, -1, 0, "", "Chapter 1 (the waterway)", "Type G-2. Motion shows its crawl and spit",
         "A slug that crawls along the walls and spits at the player or bites.", "971 97C 987 99E 9A8 9B2", 1,
         "5:39 7:81 9:50 11:6"},
        // ---- Bosses ----
        {"t35", "The Chariot", kGalBosses, 35, 0, "MO_b1_", "Chapters 1, 4", "Type 27. Hit points 480",
         "Chapter 1 boss: an armoured giant with a halberd. He comes back in Chapter 4 (480 hit points again)."},
        {"t36", "The Chariot (armour off)", kGalBosses, 36, 0, "MO_b1_", "Chapters 1, 4", "",
         "The Chariot once its armour has been shot away."},
        // (galfix: one entry; type 55, BO_devilonm, was a creature entry "Devilon": it is the Hangedman himself, a
        // smaller copy of his model carrying Sophie off in Chapter 1 (HOTD_TRACE_TYPE_MOTIONS: it plays his b2_base))
        {"t50", "The Hangedman", kGalBosses, 50, 1, "MO_b2_", "Chapters 1, 2, 4", "Type 041. Hit points 800",
         "Chapter 2 boss: a winged creature that fights over the mansion's roofs under the full moon. In Chapter 1 he carries Sophie off; he comes back in Chapter 4 with 560 hit points."},
        {"t64", "The Hermit", kGalBosses, 64, 2, "MO_b3_", "Chapter 3", "Type 6803. Hit points 160",
         "Chapter 3 boss: a giant spider in its web. At 100 hit points he drops off the wall."},
        {"t76", "The Magician", kGalBosses, 76, 3, "MO_boss4", "Chapter 4", "Type 0. Hit points 600",
         "Chapter 4 boss: the last creature of the mansion."},
        // ---- Items (props: the arcade's prop sub-type table 0xAC2C0, a model per damage step) ----
        {"frog", "Golden frog", kGalItems, 91, 1, "MO_frog", "Chapters 1-3, hidden in props", "Shooting it: 1000 points",
         "Hides in some props; when shot it leaps away."},
        {"life", "Life item", kGalItems, -1, 2, "", "Chapters 1-3, hidden in props", "Shooting it gives a life",
         "Dropped by some props when they break.", "312", 8, "5:39 7:81 9:28 11:6 13:60", false, 180},
        {"coin", "Gold coin", kGalItems, -1, 0, "", "Chapters 1-3, hidden in props", "Shooting it gives bonus points",
         "Spins in place until shot.", "3B5", 3, "5:119 7:81 9:28 11:37 19:70 21:100", false, 0, -60},
        {"axe", "Cyril's axe", kGalItems, -1, 0, "", "Chapters 1-3", "",
         "The axe Cyril holds and throws.", "5BB", 1, "7:81 9:28", false, 90},
        {"barrel", "Barrel", kGalItems, -1, 0, "", "Chapters 1, 2", "",
         "Breaks when shot; some props hide an item.", "688", 3, "5:119 7:81 9:28 11:37 19:70 21:100"},
        {"crate", "Wooden crate", kGalItems, -1, 0, "", "Chapters 1, 2", "", "A breakable crate.", "36E", 3,
         "5:119 7:81 9:28 11:37 19:70 21:100"},
        {"clock", "Grandfather clock", kGalItems, -1, 0, "", "Chapter 1", "Four damage steps (Motion shows them)",
         "Breaks a piece at a time when shot.", "D12 D13 D14 D15", 3, "5:119 7:81 9:28 11:37 19:70 21:100"},
        {"deer", "Deer head", kGalItems, -1, 1, "", "Chapter 2", "Two damage steps (Motion shows them)",
         "A mounted deer head on the wall.", "13A7 13A8", 6, "5:39 7:81 9:28 11:56", false, 180},
        {"tank", "Aquarium", kGalItems, -1, 1, "", "Chapter 2", "Four damage steps (Motion shows them)",
         "A fish tank that shatters.", "13AD 13AE 13AF 13B0", 6, "5:39 7:81 9:28 11:56"},
        // bugs2 (GitHub #4: "this fish is actually in the aquarium in Chapter 2"): back with the items; only its shine
        // (the unused environment mapping) is cut
        {"fish", "Human-faced fish", kGalItems, -1, 1, "", "Chapter 2 (in the aquarium)",
         "The only object reserved for the unused environment mapping (cut: it is drawn plainly)",
         "One of the fish with human faces swimming in the aquarium. The arcade has code to make it look wet and shiny, never used.",
         "1352", 6, "5:39 7:81 9:28 11:56"},
        // ---- Cut content (arcade only: the PC files have no meshes for them; docs/cut_content.md) ----
        // (clay: their textures are in area sheets the game never draws them with; gallery_cut.cpp has where (cutrestore),
        // U shows them untextured in one light colour of Chapter 1's palette; the leech's colours are a guess)
        {"t5", "MrG", kGalCut, 5, 0, "MO_mrg", "Never (cut)", "Developer name BO_mr_g. Four motions of its own. Untextured",
         "A cut creature: skeleton, models and motions (walk, shout, punch, fall), never spawned.", "", 0, "", true},
        {"t10", "Syndy", kGalCut, 10, 0, "MO_synd", "Never (cut)", "Developer name BO_syndy. One motion of its own. Untextured",
         "A cut character with its own demo motion; never spawned.", "", 0, "", true},
        {"t12", "Debu", kGalCut, 12, 0, "MO_z_ben MO_z_chn", "Never (cut)", "Developer name BO_debu. Untextured",
         "A cut Bentley-like zombie (it has Bentley's motion lists); never spawned.", "", 0, "", true},
        {"t39", "Lubin", kGalCut, 39, 0, "MO_dr_", "Never (cut)", "Developer name BO_lubin. No motions of its own. Untextured",
         "A cut creature with damage models; shown with Drake's motions (a guess).", "", 0, "", true},
        {"t52", "Disiprin", kGalCut, 52, 0, "MO_z_walk MO_z_kamae MO_z_naguri MO_z_hit MO_z_dead", "Never (cut)",
         "Developer name BO_disiprin. Untextured", "A cut zombie with the plain zombie's stance and walk lists; never spawned.",
         "", 0, "", true},
        {"t56", "Neozom", kGalCut, 56, 0, "MO_z_walk MO_z_kamae MO_z_naguri MO_z_hit MO_z_dead", "Never (cut)",
         "Developer name BO_neozom. Untextured", "A cut Sam variant: 16 of its 17 parts are Sam's, the chest its own.", "", 0,
         "", true},
        {"t66", "Baba", kGalCut, 66, 0, "MO_z_kamae MO_z_walk", "Never (cut)", "Developer name BO_pbaba. No motions of its own. Untextured",
         "A cut creature; models only. Shown with the plain zombie's stance and walks (a guess).", "", 0, "", true},
        {"t75", "Hiru", kGalCut, 75, 0, "MO_hiru", "Never (cut)", "Developer name BO_hiru_b. Six motions of its own",
         "A cut leech: a segmented body with its own motions; never spawned. Its colours are a guess."},
        {"head", "The Hermit's second head", kGalCut, -1, 2, "", "Never (cut)", "Model boss31_kao (0x1AC6), in no skeleton",
         "A second head for the Hermit next to the one the game uses; maybe a fly-eyed head (a guess).", "1AC6", 11,
         "5:53 9:81 11:11 17:98 19:83 21:23 23:17 29:23"},
    };
    return kEntries;
}

GalleryLook galleryLook(int type)
{
    // Where the game draws each type the longest (frames counted with HOTD_TRACE_TYPE_ROWS, GameView, over Chapters 1-4:
    // the default route, 2 players, the other branches by --route; arcade models): the texture area and the colour rows
    // the event scripts had swapped in (GGCOL / GGSET, table row 2 x row + 1 : row library entry). The arcade's creature
    // colours come from these rows (the same models are re-coloured per spawn group), so without them the Gallery drew
    // every creature in the area's own rows: green skin, wrong clothes (viewer fix, 2026-10-20). Per the entry's chapter
    // when the type is drawn there. Types 14 / 15 take Rogan's / G's (2 / 13), Harris (31) his variant 47's; the Murrer
    // (6) was never drawn by a body in those runs: Chapter 1's first area with the rows every creature there has.
    // gallery2 (2026-10-21, the same runs again): Burner 84, Taalu 83, Kage 38, Kageta 68, Neopetit 41, the researchers
    // 19 / 20 / 21 / 23 / 25 / 27, Curien's second model 78, the Hangedman's 69, the golden frog 91 (its chapter's
    // area). The cut types were never drawn: their area is the one whose textures fit them (looked at, gallery2).
    static const std::vector<std::pair<int, GalleryLook>> kLooks = {
        {2, {4, "7:81 9:1 11:45 15:48 19:117"}},     {13, {4, "7:81 9:1 11:45 15:48 19:117"}},
        {14, {7, "5:66 7:81 9:103 11:28 13:92 17:32 19:118 29:87"}},
        {15, {4, "7:81 9:1 11:45 15:48 19:117"}},    {34, {4, "7:81 9:1 11:45 15:48 19:117"}},
        {17, {1, "7:81 9:28"}},                      {26, {3, "5:119 7:81 9:28 11:37 19:70 21:100"}},
        {60, {11, "5:15 17:98 19:83 21:16 23:17"}},  {11, {1, "7:81 9:28"}},
        {28, {1, "7:81 9:28"}},                      {29, {1, "7:81 9:28"}},
        {24, {1, "5:66 7:81 9:50 11:6 13:110 17:94 19:106"}},
        {57, {1, "5:66 7:81 9:50 11:6 13:110 17:94 19:106"}},
        {3, {1, "5:66 7:81 9:28 11:61"}},            {4, {6, "5:39 7:81 9:28 11:61"}},
        {30, {7, "5:39 7:81 9:28 13:37 19:118"}},    {33, {5, "5:39 7:81 9:28"}},
        {16, {3, "5:119 7:81 9:28 11:37 19:70 21:100"}},
        {32, {5, "5:39 7:81 9:28 11:28 13:66 17:108 19:37"}},
        {31, {8, "7:81 9:28"}},                      {53, {8, "5:39 7:81 9:73 11:0 13:60"}},
        {47, {8, "7:81 9:28"}},   // galfix: Harris's entry draws type 47 (its own rows, traced: area 8, Chapter 3)
        {18, {7, "5:39 7:81 9:28 11:28 13:24 17:20 19:71 29:87"}},
        {37, {5, "5:39 7:81 9:28 11:28 13:55 15:80 17:67 19:59"}},
        {59, {8, "5:39 7:81 9:103 11:38 13:60"}},    {0, {1, "5:27 7:81 9:28"}},
        {1, {3, "5:119 7:81 9:28 11:37 19:70 21:100"}},
        {7, {1, "5:4 7:81 9:28 11:76"}},             {55, {1, "5:66 7:31 9:103 11:76"}},
        {6, {1, "7:81 9:28"}},                       {48, {8, "5:85 7:81 9:103 11:0 13:60"}},
        {35, {4, "7:81 9:1 11:45 15:48 19:117"}},    {36, {4, "7:81 9:1 11:45 15:48 19:117"}},
        {50, {7, "5:66 7:81 9:103 11:28 13:92 17:32 19:118 29:87"}},
        {64, {11, "5:53 9:81 11:11 17:98 19:83 21:23 23:17 29:23"}},
        {76, {12, "1:34 5:15 13:37 21:16 23:17"}},
        // gallery2
        {84, {9, "7:81 9:34 11:38"}},                {83, {9, "5:39 7:81 9:105 11:92 15:52"}},
        {38, {7, "5:39 7:81 9:28 11:28 13:60 19:59"}},
        {68, {9, "7:90 9:102 11:38"}},
        {42, {3, "5:119 7:81 9:28 11:37 19:70 21:100"}},   // bugs2: traced (Chapter 1, the sewer route's run)
        {43, {1, "5:4 7:81 9:28 11:76"}},
        {46, {1, "5:66 7:81 9:50 11:56"}},                 // bugs2: Ebitan C, Chapter 1 event 11
        {41, {8, "5:39 7:81 9:28 11:6 13:60"}},            // (Neopetit: was behind the comment above since bugs2)
        {19, {7, "5:39 7:81 9:28 13:37 19:118"}},    {20, {6, "5:39 7:81 9:28 11:56"}},
        {21, {1, "7:81 9:28"}},
        {23, {7, "5:39 7:81 9:28 11:28 13:24 17:20 19:71 29:87"}},
        {25, {5, "5:39 7:81 9:28 11:28 13:55 15:80 17:67 19:59"}},
        {27, {6, "5:65 7:81 9:28 11:61 13:60 17:67 19:36"}},
        {78, {12, "5:15 21:16 23:17"}},              {69, {10, "15:68 17:120 19:104"}},
        {91, {7, "5:39 7:81 9:28 11:61 13:24 17:20 19:71 29:87"}},
        {67, {2, "13:92"}},   // galfix3: the zombie Sophie as the ending's event 6 draws her (traced: area 2, rows 13:92)
    };
    if (const char* force = std::getenv("HOTD_GALLERY_LOOK")) {   // testing: "<type>:<area>[:<rows>]" (the cut types' areas)
        static std::string rows;
        int t = -1, a = 0;
        char buf[200] = {};
        if (std::sscanf(force, "%d:%d:%199[^\n]", &t, &a, buf) >= 2 && t == type) {
            rows = buf;
            return {a, rows.c_str()};
        }
    }
    if (const GalleryCutLook* c = galleryCutLook(type)) return {c->area, c->rows};   // the cut types (gallery_cut.cpp)
    for (const auto& [t, l] : kLooks)
        if (t == type) return l;
    return {};
}

namespace {
// the backgrounds: the dark default, the game's skies (the sky models the game draws around the camera in the outdoor
// areas: ArcadeSource's defaultSkies table, measured in MAME; 0x608 skyuv02a is the layer every sky has; Chapter 2's
// areas 6 / 7 look the same as its area 5 in the window, so one of them), plain colours
struct Backdrop {
    const char* name;
    int chapter, area;          // the sky's chapter stage and texture area (-1: a plain colour)
    uint32_t ids[2];
    float y;                    // the sky's height offset from the camera (the table's)
    float rgb[3];               // a plain colour's
    // gallery3: a room of the game's world around the model (its chapter stage's world models near `room`: the floor
    // point x, y, z the model stands on and the turn, radians, that puts the game camera's view there in front)
    bool isRoom = false;
    float room[4] = {};
};
// (rooms last: launcher.ini gallerybg= keeps its numbers)
Backdrop gRoomDebug;   // testing: HOTD_GALLERY_RAIL (Gallery::draw)
const Backdrop kBackdrops[] = {
    {"Dark", -1, 0, {0, 0}, 0, {0.03f, 0.025f, 0.03f}},
    {"Chapter 1 sky", 0, 1, {0x608, 0xA29}, -9, {}},
    {"Chapter 2 sky", 1, 5, {0x608, 0x1238}, -9, {}},
    {"Chapter 4 sky", 3, 12, {0x608, 0x1D20}, -9, {}},
    {"Grey", -1, 0, {0, 0}, 0, {0.32f, 0.32f, 0.34f}},
    {"Blue", -1, 0, {0, 0}, 0, {0.08f, 0.13f, 0.26f}},
    {"White", -1, 0, {0, 0}, 0, {0.85f, 0.85f, 0.85f}},
    // gallery3: rooms of the game's world (found with HOTD_GALLERY_RAIL: Chapter 1 rail 0 frame 200, the bridge to the
    // mansion; Chapter 2 rail 36 frame 150 in texture area 8, a corridor of the mansion)
    // galfix2 (user: "Bridge 1 ... is the entrance where the car comes in"; "Mansion corridor ... is broken"): the bridge
    // is Chapter 1's rail 6 (the bridge over the moat up to the mansion's red door), frame 0, its deck at -18 (the
    // floor search found the moat's bank under it); the corridor's textures are area 6's (area 8 is a Chapter 3 area:
    // its pictures on these models were the broken look), the red carpet and the paintings
    {"Bridge (Chapter 1)", 0, 1, {0, 0}, 0, {}, true, {-12.3f, -18.0f, -375.7f, 3.199f}},
    {"Mansion corridor (Chapter 2)", 1, 6, {0, 0}, 0, {}, true, {370.0f, -20.8f, -3.1f, 1.573f}},
};
constexpr int kBackdropCount = int(sizeof kBackdrops / sizeof kBackdrops[0]);
}   // namespace

// the chapter stage an entry is loaded from (testing: HOTD_GALLERY_STAGE=<0-3> loads every entry from that stage, to
// find the texture area of a type the game never draws, with HOTD_GALLERY_LOOK)
static int entryChapter(const GalleryEntry& e)
{
    static const char* force = std::getenv("HOTD_GALLERY_STAGE");
    return force ? std::clamp(std::atoi(force), 0, 3) : e.chapter;
}

int galleryBackgroundCount() { return kBackdropCount; }
const char* galleryBackgroundName(int b) { return kBackdrops[std::clamp(b, 0, kBackdropCount - 1)].name; }

// ---- gallery blood / gallery dismemberment: the game's tables (arcade program ROM; the PC's are the same data) ----
namespace {

constexpr uint32_t kPartRoles = 0x1184E0;            // 31 x 8 bytes a type: byte 3 the part class, byte 4 sever
constexpr uint32_t kPartReplacement = 0x11DFF0;      // 0x200 a type, 0x20 a class: the model of each damage step
constexpr uint32_t kPartDamageCallbacks = 0xC66A0;   // the per-type damage routine (part_damage_callback)
constexpr uint32_t kBlowOffStep = 0x135940;          // byte a type: the head's step that knocks it off (part_blow_off)
constexpr uint32_t kGgsetLists = 0xC5A40;            // GGSET n -> 4 lists (by blood colour) of 4 colour row entries
constexpr uint32_t kBloodTable = 0xAB110;            // the blood colour's RGB555 by blood_color (entry 1023)
constexpr uint32_t kRainbowTable = 0xAB120;          // palette_animation's 90-colour rainbow (the Chapter 4 screens)
constexpr uint32_t kBurstFirst = 7, kBurstLast = 0x23;     // blood_burst: one a frame
constexpr uint32_t kDripFirst = 0x46B, kDripLast = 0x4A3;  // blood_drip: one a frame, scale 0.4
constexpr uint32_t kKeepModel = 0x7B0;               // part_hit: a step model that keeps the part's model
constexpr uint32_t kNoModel = 1;                     // PN_spaceB: nothing drawn

uint32_t romU32(const std::vector<uint8_t>& m, uint32_t a)
{
    return a + 4 <= m.size() ? uint32_t(m[a]) | uint32_t(m[a + 1]) << 8 | uint32_t(m[a + 2]) << 16 | uint32_t(m[a + 3]) << 24 : 0;
}
uint16_t romU16(const std::vector<uint8_t>& m, uint32_t a) { return a + 2 <= m.size() ? uint16_t(m[a] | m[a + 1] << 8) : 0; }

// the body part of a part class (part_class_lost_bit 0x1359A0: classes 2 / 4 / 6 lose bit 1, 3 / 5 / 7 bit 2; the
// right / left shoulders are classes 2 / 3, the chest and hips 8 / 9, the legs 10-15); -1 none (the root, the hands'
// metal, the class-17 parts)
int partOfClass(int cls)
{
    if (cls == 1) return kPartHead;
    if (cls == 2 || cls == 4 || cls == 6) return kPartRightArm;
    if (cls == 3 || cls == 5 || cls == 7) return kPartLeftArm;
    if (cls == 8 || cls == 9) return kPartBody;
    if (cls >= 10 && cls <= 15) return kPartLegs;
    return -1;
}

// The per-type damage routines (port/damage_types.cpp, table 0xC66A0): at these step models of a part, its children at
// these joints (bit mask) are cut off; `first`: its first child (type 0's); `once`: only the first child at the joint
struct CutRule {
    uint32_t routine;
    std::vector<uint32_t> models;
    unsigned joints;
    bool first = false, once = false;
};
constexpr unsigned kJ2 = 1u << 2, kJ3 = 1u << 3, kJ6 = 1u << 6;
const std::vector<CutRule>& cutRules()
{
    static const std::vector<CutRule> kRules = {
        {0x68F20, {0x78F, 0x460, 0x5BA, 0x16EB, 0x139D, 0x1940}, kJ2 | kJ3 | kJ6},   // zom (part_damage_head_off)
        {0x69010, {kKeepModel}, kJ2 | kJ3 | kJ6},                                    // zonbiman
        {0x690C0, {0x13C6, 0x1135, 0x553, 0x125C, 0x12B7, 0x175A, 0x151E, 0x176D, 0x1955}, kJ2 | kJ3 | kJ6},   // debu
        {0x691E0, {0x1338, 0x1145, 0x152A, 0x17DA}, kJ2 | kJ3 | kJ6},                // gilmoa
        {0x692C0, {0x117B, 0xDF8, 0x1590, 0x183D}, kJ2 | kJ3 | kJ6},                 // haris
        {0x693A0, {0x1607, 0x965, 0x1A45}, kJ2 | kJ3 | kJ6},                         // mummy
        {0x69470, {0x15B0, 0x945, 0x19E7}, kJ2 | kJ3 | kJ6},                         // mummy2
        {0x68E20, {0x580, 0x17F, 0xAAA, 0x1482}, kJ2 | kJ3 | kJ6},                   // semu
        {0x68EC0, {0x732}, kJ3},                                                     // tetuman
        {0x68B50, {0xC8E, 0xCC1, 0xCAA}, kJ2, false, true},                          // mon: the head
        {0x68B50, {0xC8D, 0xCC0}, kJ3 | kJ6},                                        //      both arms
        {0x68B50, {0xCA8}, kJ6, false, true},
        {0x68B50, {0xCA9}, kJ3, false, true},
        {0x689E0, {0x881, 0x84F, 0x86C, 0xB8A, 0x88C, 0xB9F, 0x87B, 0x845, 0x863, 0xB7F, 0x886, 0xB99}, 0, true},   // dkiller
    };
    return kRules;
}
constexpr uint32_t kDkiller = 0x689E0;
// type 0's routine: its head's placeholder step (model 1) gives the head's parent this model, by type
uint32_t dkillerParentModel(int type)
{
    switch (type) {
    case 0x50: return 0x88C;
    case 0x4F: return 0x881;
    case 0x28: return 0xB8A;
    case 0: return 0x84F;
    case 8: return 0x86C;
    case 0x51: return 0xB9F;
    default: return 0;
    }
}

// every damage model of a type (the stage loads them with its parts)
std::vector<uint32_t> damageModelIds(const std::vector<uint8_t>& rom, int type)
{
    std::vector<uint32_t> ids;
    if (type < 0 || type >= 94) return ids;
    for (uint32_t c = 0; c < 16; ++c)
        for (uint32_t s = 0; s < 8; ++s)
            if (const uint32_t m = romU32(rom, kPartReplacement + uint32_t(type) * 0x200 + c * 0x20 + s * 4); m > kNoModel && m < 0x2000)
                ids.push_back(m);
    if (const uint32_t m = dkillerParentModel(type)) ids.push_back(m);
    return ids;
}

// GameView's custom Blood Color recolour of the blood rows (view.cpp tintBloodRows), for the Gallery's own colour: per
// brightness the row's grey kept, the colour above it the custom colour's
void tintRows(std::vector<uint8_t>& xlat, const std::set<int>& rows, int rgb)
{
    if (rgb < 0 || xlat.size() < 256 * 96) return;
    const float t[3] = {float(rgb >> 16 & 0xFF) / 255.0f, float(rgb >> 8 & 0xFF) / 255.0f, float(rgb & 0xFF) / 255.0f};
    const float top = std::max({t[0], t[1], t[2], 1e-3f});
    for (int row : rows) {
        if (row < 0 || row >= 32) continue;
        for (int i = 0; i < 256; ++i) {
            uint8_t* c[3] = {&xlat[size_t(row * 256 + i)], &xlat[size_t((32 + row) * 256 + i)], &xlat[size_t((64 + row) * 256 + i)]};
            const float lo = float(std::min({*c[0], *c[1], *c[2]})), hi = float(std::max({*c[0], *c[1], *c[2]}));
            for (int k = 0; k < 3; ++k) *c[k] = uint8_t(std::clamp((lo + (hi - lo) * t[k] / top) * top + 0.5f, 0.0f, 255.0f));
        }
    }
}

// the arcade has no yellow (blood table 0xAB110: red, green, blue, purple): the Gallery draws the arcade models' yellow
// with the custom recolour in this colour (the PC's B_YELLO.VMC pages on the PC models)
constexpr int kArcadeYellow = 0xF0D000;

} // namespace

const char* galleryBloodColorName(int c)
{
    static const char* const kNames[6] = {"Red", "Green", "Blue", "Purple", "Yellow", "Custom"};
    return kNames[std::clamp(c, 0, 5)];
}
const char* galleryRainbowSpeedName(float s)
{
    return s <= 0.0f ? "Still" : s < 0.15f ? "Slow" : s < 0.3f ? "Normal" : "Fast";
}
float galleryRainbowSpeedStep(float s, int dir)
{
    static const float kSpeeds[4] = {0.0f, 0.1f, 0.2f, 60.0f / 135.0f};   // (cosmods.cpp rainbowBloodConfigure's)
    int i = s <= 0.0f ? 0 : s < 0.15f ? 1 : s < 0.3f ? 2 : 3;
    return kSpeeds[((i + dir) % 4 + 4) % 4];
}
const char* galleryCreatureBloodName(int c)
{
    static const char* const kNames[3] = {"Own Colour Each", "Rainbow Cycle", "Off"};
    return kNames[std::clamp(c, 0, 2)];
}
const char* galleryPartName(int p)
{
    static const char* const kNames[kGalleryParts] = {"Head", "Right Arm", "Left Arm", "Body", "Legs"};
    return p >= 0 && p < kGalleryParts ? kNames[p] : "";
}

bool Gallery::open(const Paths& p, std::string& err)
{
    close();
    paths_ = p;
    if (p.regions.empty() || !anim_.load(p.regions, err)) {
        if (err.empty()) err = "the gallery needs the arcade files (the creatures' skeletons and motions)";
        return false;
    }
    arcade_ = std::make_unique<ArcadeSource>(ArcadeConfig{p.regions, p.dumps, p.texsets, true, false});
    if (!arcade_->available()) arcade_.reset();
    if (!p.pc.empty()) {
        pcBlood_ = bloodSet_ ? pcBloodWanted() : p.blood;   // (gallery blood: the Gallery's own colour)
        pc_ = std::make_unique<PcSource>(PcConfig{p.pc, pcBlood_});
        if (!pc_->available()) pc_.reset();
    }
    if (!arcade_ && !pc_) {
        err = "no model source";
        return false;
    }
    if (!renderer_.init(err)) return false;
    if (!backdropInit(err)) return false;
    opt_.clear[0] = 0.03f, opt_.clear[1] = 0.025f, opt_.clear[2] = 0.03f;
    if (!hasSource(source_)) source_ = arcade_ ? Source::Arcade : Source::PC;
    open_ = true;
    if (const char* dump = std::getenv("HOTD_GALLERY_DUMP"))   // testing: every entry's motion list (number, name)
        if (FILE* f = std::fopen(dump, "w")) {
            const int keep = index_;
            for (int i = 0; i < count(); ++i) {
                index_ = i;
                pick();
                const GalleryEntry& e = galleryEntries()[size_t(i)];
                std::fprintf(f, "%s type %d:", e.key, e.type);
                if (e.type >= 0)
                    for (int m : motions_) std::fprintf(f, " %d=%s", m, anim_.motionName(m).c_str());
                std::fprintf(f, "\n");
            }
            for (int m = 0; m < anim_.motionCount(); ++m)
                std::fprintf(f, "motion %d %s joints %d frames %d\n", m, anim_.motionName(m).c_str(), anim_.motionJoints(m),
                             anim_.motionFrames(m));
            const std::vector<std::string> names = arcadeObjectNames(anim_.mainData());
            for (int t = 0; t < anim_.typeCount(); ++t) {
                const Skeleton& sk = anim_.skeleton(t);
                for (size_t n = 0; n < sk.nodes.size(); ++n) {
                    const SkeletonNode& nd = sk.nodes[n];
                    std::fprintf(f, "node type %d %zu parent %d role %d class %d model %X %s\n", t, n, nd.parent, nd.joint,
                                 anim_.partClass(t, nd.joint), nd.model,
                                 nd.model < names.size() ? names[nd.model].c_str() : "");
                }
            }
            std::fclose(f);
            index_ = keep;
        }
    select(index_);
    return true;
}

void Gallery::close()
{
    if (!open_) return;
    for (auto& [k, s] : stages_)
        if (s) renderer_.release(*s);
    stages_.clear();
    failed_.clear();
    xlatRows_.clear();
    renderer_.shutdown();
    if (backProgram_) gl::DeleteProgram(backProgram_);
    if (backVao_) gl::DeleteVertexArrays(1, &backVao_);
    backProgram_ = backVao_ = 0;
    bloodEntry_ = 0;
    fx_.clear();
    arcade_.reset();
    pc_.reset();
    open_ = false;
}

const StageData* Gallery::stage(Source s, int chapter)
{
    const std::pair<int, int> key{int(s), chapter};
    if (auto it = stages_.find(key); it != stages_.end()) return it->second.get();
    if (failed_.count(key)) return nullptr;
    AssetSource* src = s == Source::Arcade ? static_cast<AssetSource*>(arcade_.get()) : static_cast<AssetSource*>(pc_.get());
    if (!src) return nullptr;
    // every entry of this chapter at once (one stage load per chapter and source)
    std::vector<uint32_t> ids;
    std::vector<ArcadeSource::SkinRequest> skins;
    for (const GalleryEntry& e : galleryEntries())
        if (entryChapter(e) == chapter && e.type >= 0 && e.type < anim_.typeCount()) {
            for (const SkeletonNode& n : anim_.skeleton(e.type).nodes)
                if (n.model) ids.push_back(n.model);
            // the models body_part_draw_hook draws in a part's place or with it (wings, veins, eyes, the gun, the coat)
            for (uint32_t id : partDrawExtraModels(e.type)) ids.push_back(id);
            // gallery dismemberment: its damage models (part_replacement; the PC files have the same IDs)
            for (uint32_t id : damageModelIds(anim_.maincpu(), e.type)) ids.push_back(id);
            // the generated skin (the bellies: skin_draw, arcade only; the PC draws its parts as they are)
            ArcadeSource::Skin sk;
            if (s == Source::Arcade && arcade_->skinInfo(e.type, sk, 0)) skins.push_back({kSkinKey + uint32_t(e.type), e.type, 0});
        } else if (entryChapter(e) == chapter && e.type < 0) {   // still models (items, props, the cut head)
            std::istringstream ms(e.models);
            for (std::string h; ms >> h;) ids.push_back(uint32_t(std::strtoul(h.c_str(), nullptr, 16)));
        }
    // pmfix2: the PC Mode extras without an entry of their own (Researcher (5) / (6), Dr. Curien (Dead): makePortrait)
    for (int t : {22, 49, 78})
        if (t < anim_.typeCount()) {
            for (const SkeletonNode& n : anim_.skeleton(t).nodes)
                if (n.model) ids.push_back(n.model);
            ArcadeSource::Skin sk;
            if (s == Source::Arcade && arcade_->skinInfo(t, sk, 0)) skins.push_back({kSkinKey + uint32_t(t), t, 0});
        }
    for (const Backdrop& b : kBackdrops)   // the skies of this chapter
        if (b.chapter == chapter)
            for (uint32_t id : b.ids) ids.push_back(id);
    // gallery blood: the blood burst's and the drip's frames (blood_burst 0x44EF0: 7..0x23; blood_drip 0x44E70: 0x46B..0x4A3)
    for (uint32_t id = kBurstFirst; id <= kBurstLast; ++id) ids.push_back(id);
    for (uint32_t id = kDripFirst; id <= kDripLast; ++id) ids.push_back(id);
    src->requestModels(chapter, ids);
    if (s == Source::Arcade) arcade_->requestSkins(chapter, skins);
    const uint64_t t0 = SDL_GetTicks();
    std::string err;
    std::unique_ptr<StageData> sd = src->loadStage(chapter, err);
    if (!sd) {
        SDL_Log("gallery: %s Chapter %d: %s", sourceName(s), chapter + 1, err.c_str());
        failed_.insert(key);
        return nullptr;
    }
    for (auto& [k, m] : sd->models) markCoplanarLayers(m);
    renderer_.upload(*sd);
    for (auto& [k, m] : sd->models)   // (the GPU has them; GameView frees its copies the same way)
        for (Mesh& me : m.meshes) me.releaseVertices();
    for (TextureSet& ts : sd->textureSets) {
        std::vector<std::vector<uint8_t>>().swap(ts.layers);
        std::vector<std::vector<uint8_t>>().swap(ts.layersBrown);
    }
    SDL_Log("gallery: %s Chapter %d loaded (%zu models, %llu ms)", sourceName(s), chapter + 1, sd->models.size(),
            (unsigned long long)(SDL_GetTicks() - t0));
    return (stages_[key] = std::move(sd)).get();
}

void Gallery::select(int i)
{
    const int n = count();
    index_ = ((i % n) + n) % n;
    lastInCategory_[size_t(category())] = index_;
    pick();
    damageInit();   // (gallery dismemberment: the type's tables, every part whole; not in pick: the portraits pick others)
    resetView();
    settle();   // a new entry: its first view at once
    if (reStage) {   // regallery: on the stage it fades in, coming a little from the right (0.25 s)
        fadeIn_ = 0;
        panX_ = wantPanX_ + 0.06f;
    }
}

bool Gallery::selectKey(const std::string& key)
{
    const auto& es = galleryEntries();
    for (size_t i = 0; i < es.size(); ++i)
        if (key == es[i].key) {
            if (open_) select(int(i));
            else index_ = int(i);
            return true;
        }
    return false;
}

void Gallery::step(int dir)
{
    const auto& es = galleryEntries();
    const int n = count(), c = category();
    for (int k = 1; k <= n; ++k) {
        const int i = ((index_ + dir * k) % n + n) % n;
        if (es[size_t(i)].category == c) {
            select(i);
            return;
        }
    }
}

int Gallery::indexInCategory() const
{
    int k = 0;
    for (int i = 0; i < index_; ++i) k += galleryEntries()[size_t(i)].category == category();
    return k;
}

int Gallery::countInCategory(int c) const
{
    int k = 0;
    for (const GalleryEntry& e : galleryEntries()) k += e.category == c;
    return k;
}

void Gallery::selectCategory(int c)
{
    if (c < 0 || c >= kGalCategories) return;
    const auto& es = galleryEntries();
    const int last = lastInCategory_[size_t(c)];
    // user 2026-10-26 ("always default the gallery to rogan in players"): Players always opens on Rogan (its first entry)
    if (c != kGalPlayers && last >= 0 && last < count() && es[size_t(last)].category == c) {
        select(last);
        return;
    }
    for (size_t i = 0; i < es.size(); ++i)
        if (es[i].category == c) {
            select(int(i));
            return;
        }
}

void Gallery::cycleBackground(int dir)
{
    background = ((background + dir) % kBackdropCount + kBackdropCount) % kBackdropCount;
}

void Gallery::setSource(Source s)
{
    if (hasSource(s)) source_ = s;
    framed_ = false;
}

// galfix (user 2026-10-26: "some creatures have animations that don't belong to them, like Chapter 3's small blue
// chrome creatures using the Magician's idle pose"): the motions the game plays on each creature type (HOTD_TRACE_TYPE_MOTIONS,
// GameView: Chapters 1-4 with 1 and 2 players, autofire and the bot, both sides of the branches; arcade motion numbers,
// those seen 3+ frames). An entry's list is its own named motions (GalleryEntry::motions) and these, no longer every
// motion with the skeleton's joint count (that gave Kageta the Magician's, Taalu the Chariot's, 235 to pick from)
static const std::vector<int>& seenMotions(int type)
{
    static const std::vector<std::pair<int, const char*>> kSeen = {
        {0, "101 102 106 107 112 113"}, {1, "410 414 416 417 418 419 431 434 436 437 440 443 445 448"},
        {2, "339 341 346 347 352 354 360 362 368 369 371 372 375 376 379 384 461 463 465 669 670 671 672"},
        {3, "483 497 500 505 509 559 563 568 569 570 571 594 595 596 621 627"},
        {4, "142 143 144 146 147 149 150 151 155 156 158 159 162 163 167 168"}, {7, "178 179 181 185"},
        {11, "472 483 497 502 504 505 516 560 561 563 564 565 597 598 615 616 621 624 626 627 628 629 641 643 644 645 647 648 649 658"},
        {13, "188 190 191 193 194 195 196 197 199 347 350 351 353 360 371 375 377 461 667 668"},
        {16, "473 483 487 497 498 502 504 505 519 528 533 534 535 537 539 540 560 561 562 563 564 565 566 567 592 608 612 617 618 619 621 644"},
        {17, "211 218 255 270 286"}, {18, "118 122 135 137 404 410 414 416 426 430 436 437 440 442 443 445 448 497"},
        {19, "211 255 271 272 286"}, {20, "217 220 222 235 236 255 276"},
        {21, "211 212 218 221 228 235 236 253 255 263 264 278 374 663"}, {23, "212 255 274 281 285"},
        {24, "502 504 521 560 565 602 649 650"}, {25, "232 241 242 243 253 255 262 281 285"},
        {26, "211 218 222 228 248 389"}, {27, "212 235 236 245 246 255 256 262 277 281 283 286"},
        {28, "472 473 474 475 477 483 485 497 499 500 502 504 505 512 518 519 560 561 563 564 565 567 592 597 598 602 603 608 612 615 621 624 628 629 641 643 644 649 650 658"},
        {29, "336 337 472 474 477 483 489 495 497 498 500 502 504 505 507 508 512 518 560 561 562 563 564 565 566 567 604 605 606 609 610 612 613 614 636 644 673"},
        {30, "471 497 502 510 523 524 540 542 543 544 545 549 550 551 552 553 554 556 557 561 563 592"},
        {32, "502 519 560 572 576 580 581 585 586"}, {33, "487 500 505 523 524 526 528 532 534 535 561"},
        {34, "220 255 386 388 390 391 392 394 396 399 401"}, {35, "1 4 5 13 14 16 17 18 19 20 662"},
        {36, "3 6 7 9 10 11 12 13 14 18 20 21"},
        {37, "98 99 118 120 128 129 131 132 133 135 137 404 410 416 426 436 437 440 443 445 448"},
        {38, "414 416 440 445 448"}, {41, "483 497 519 560 561 563 564 565 566 567 644 646 647 648"},
        {42, "483 563 564 566 567 644 646 648"}, {43, "483 563 564 567 644 646 648"},   // bugs2 (traced: Chapter 1)
        {46, "497 519 563 564"},
        {47, "497 498 502 504 505 560 562 563 564 630 631 632 633 634"}, {48, "454 455"},
        {50, "22 23 25 26 27 29 31 33 35"},
        {53, "471 473 474 483 504 519 560 561 563 564 565 592 597 617 618 619 620 621 637 638 639 649"},
        {57, "483 485 497 504 565 602 603 621"}, {59, "128 322 323 324 331 410 416 436 437 440 443 445 448"},
        {60, "294 298"}, {64, "39 41 44 45 46 47 48 50 51 53 54 55 56 57 58 59 60 61 62 63 64"},
        {68, "98 99 120 123 131 132 133 404 410 414 416 436 437 440 443 445 448"},
        {76, "66 76 83 84 85 86 87 90 92 93 94 95 96 97"},
        {83, "483 485 497 502 504 505 519 560 561 563 564 565 602 603 621 628 629 641"},
        {84, "505 541 542 544 545 549 550 551 552 554 556 557 561"}, {91, "178 179"},
    };
    static std::map<int, std::vector<int>> parsed;
    if (parsed.empty())
        for (const auto& [t, list] : kSeen) {
            std::istringstream ss(list);
            for (int m; ss >> m;) parsed[t].push_back(m);
        }
    static const std::vector<int> kNone;
    const auto it = parsed.find(type);
    return it == parsed.end() ? kNone : it->second;
}

void Gallery::pick()
{
    const GalleryEntry& e = entryNow();
    motions_.clear();
    motion_ = 0;
    frame_ = 1;
    framed_ = false;
    if (e.type < 0) {   // still models: the steps
        std::istringstream ms(e.models);
        for (std::string h; ms >> h;) motions_.push_back(int(std::strtoul(h.c_str(), nullptr, 16)));
        return;
    }
    const std::vector<int> all = compatibleMotions(anim_, e.type);
    std::istringstream ss(e.motions);
    std::string pre;
    while (ss >> pre)
        for (int m : all)
            if (anim_.motionName(m).rfind(pre, 0) == 0 && std::find(motions_.begin(), motions_.end(), m) == motions_.end())
                motions_.push_back(m);
    for (int m : seenMotions(e.type))   // galfix: and the ones the game plays on it
        if (std::find(all.begin(), all.end(), m) != all.end() && std::find(motions_.begin(), motions_.end(), m) == motions_.end())
            motions_.push_back(m);
    if (motions_.empty()) motions_ = all;   // (none known: every one that fits the skeleton)
    // idle, walk, the rest, attack, hit, death (each in motion-number order): a calm first look
    auto kindOrder = [](MotionKind k) {
        switch (k) {
        case MotionKind::Idle: return 0;
        case MotionKind::Walk: return 1;
        case MotionKind::Other: return 2;
        case MotionKind::Attack: return 3;
        case MotionKind::Hit: return 4;
        default: return 5;
        }
    };
    std::stable_sort(motions_.begin(), motions_.end(), [&](int a, int b) {
        return kindOrder(motionKind(anim_.motionName(a))) < kindOrder(motionKind(anim_.motionName(b)));
    });
}

void Gallery::nextMotion(int dir)
{
    if (motions_.empty()) return;
    const int n = int(motions_.size());
    motion_ = ((motion_ + dir) % n + n) % n;
    frame_ = 1;
    // still models keep their framing over the steps (the broken ones are smaller)
    if (entryNow().type >= 0) framed_ = false;
}

void Gallery::turn(float dyaw, float dpitch)
{
    wantYaw_ += dyaw;
    wantPitch_ = std::clamp(wantPitch_ + dpitch, -1.3f, 1.3f);
    if (dyaw != 0) autoTurn = false;
}

void Gallery::turnNow(float dyaw, float dpitch)
{
    turn(dyaw, dpitch);
    yaw_ = wantYaw_;
    pitch_ = wantPitch_;
}

void Gallery::zoom(float factor) { wantDist_ = std::clamp(wantDist_ / factor, 0.25f, 4.0f); }

void Gallery::resetView()
{
    // facing the camera (still models: their front, GalleryEntry::yaw / pitch); from where the view is now, the
    // shorter way round (the eased turn)
    const GalleryEntry& e = galleryEntries()[size_t(index_)];
    const float twoPi = 6.2831853f, front = 3.14159265f + e.yaw * 3.14159265f / 180.0f;
    wantYaw_ = front + twoPi * std::round((yaw_ - front) / twoPi);
    wantPitch_ = e.pitch != 0 ? e.pitch * 3.14159265f / 180.0f : -0.12f;
    wantDist_ = 1;
    wantPanX_ = wantPanY_ = 0;   // (gallery3: the slow turn stays as the player set it)
}

void Gallery::pan(float dx, float dy)
{
    wantPanX_ = std::clamp(wantPanX_ + dx, -1.5f, 1.5f);
    wantPanY_ = std::clamp(wantPanY_ + dy, -1.0f, 1.0f);
}

void Gallery::panPixels(float dx, float dy)
{
    pan(dx / float(std::max(1, lastH_)), dy / float(std::max(1, lastH_)));
    panX_ = wantPanX_;
    panY_ = wantPanY_;
}

// testing (finding the rooms of the room backgrounds): HOTD_GALLERY_RAIL=<chapter>,<path>,<frame>[,<area>] puts the
// model where the game camera of that rail frame (the PC's CAMALL.RBH) looks, on the floor under that point (the highest
// world box top under it), seen the way the game camera sees it; it prints the room's numbers for kBackdrops
bool Gallery::roomDebug()
{
    static const char* env = std::getenv("HOTD_GALLERY_RAIL");
    static int state = 0;   // 0 not tried, 1 made, 2 failed
    if (!env || state == 2) return false;
    if (state == 1) return true;
    state = 2;
    int ch = 1, path = 0, area = 0;
    float frame = 0, floorForce = 0;   // (galfix2: a 5th number, the floor's height, when the search finds the wrong one)
    const int got = std::sscanf(env, "%d,%d,%f,%d,%f", &ch, &path, &frame, &area, &floorForce);
    if (got < 3 || paths_.pc.empty()) return false;
    CameraPaths rails;
    std::string err;
    if (!rails.load(paths_.pc + "/CAM/ALLSTAGE/CAMALL.RBH", err)) {
        std::printf("GALLERY RAIL: %s\n", err.c_str());
        return false;
    }
    float eye[3], target[3];
    rails.evaluate(path, frame, eye, target);
    const StageData* rs = hasSource(Source::Arcade) ? stage(Source::Arcade, ch) : stage(source_, ch);
    if (!rs) return false;
    float floorY = -1e9f;
    for (const Placement& p : rs->placements) {
        if (rs->categoryOf(p.id) != Category::World) continue;
        const auto mit = rs->models.find(p.modelKey);
        if (mit == rs->models.end()) continue;
        const Model& md = mit->second;
        if (target[0] < md.boundsMin[0] + p.pos[0] || target[0] > md.boundsMax[0] + p.pos[0] ||
            target[2] < md.boundsMin[2] + p.pos[2] || target[2] > md.boundsMax[2] + p.pos[2])
            continue;
        const float top = md.boundsMax[1] + p.pos[1], bottom = md.boundsMin[1] + p.pos[1];
        if (top <= target[1] && top >= target[1] - 40 && top > floorY) floorY = top;
        else if (bottom <= target[1] && bottom >= target[1] - 40 && bottom > floorY && top > target[1])
            floorY = std::max(floorY, bottom);   // (a whole room in one model: its bottom)
    }
    if (floorY < -1e8f) floorY = target[1] - 15;
    if (got >= 5) floorY = floorForce;
    // the turn that puts the game camera's direction (eye -> target) where the Gallery's front view looks (yaw pi: -z)
    const float yawd = std::atan2(target[0] - eye[0], target[2] - eye[2]);
    gRoomDebug = Backdrop{"Room (test)", ch, area, {0, 0}, 0, {}, true, {target[0], floorY, target[2], 3.14159265f - yawd}};
    std::printf("GALLERY RAIL: chapter %d path %d frame %.0f: eye %.1f %.1f %.1f target %.1f %.1f %.1f floor %.1f yaw %.3f\n",
                ch, path, frame, eye[0], eye[1], eye[2], target[0], target[1], target[2], floorY, 3.14159265f - yawd);
    std::fflush(stdout);
    state = 1;
    return true;
}

std::string Gallery::lightLabel() const
{
    static const char* const kSide[8] = {"Front", "Front right", "Right", "Back right", "Back", "Back left", "Left", "Front left"};
    return light <= 0 ? "Game" : kSide[(light - 1) % 8];
}

// gallery3: the backdrop pass, one triangle strip over the window (the scissor keeps it in the rectangle). Mode 0: the
// plain background lit round the model (a soft spot a little above the middle, darker towards the edges) and the
// floor shadow (a soft ellipse under the feet); mode 1: the vignette, black with alpha towards the corners
namespace {
const char* kBackVs = R"(#version 330 core
uniform vec4 uPos;   // the quad in normalized device coordinates: x, y (bottom left), w, h
uniform vec4 uUv;    // mode 2: its texture rectangle u, v (top left), w, h
out vec2 vUV;
void main() {
    vec2 c = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    vUV = uUv.xy + vec2(c.x, 1.0 - c.y) * uUv.zw;
    gl_Position = vec4(uPos.xy + c * uPos.zw, 0.0, 1.0);
}
)";
const char* kBackFs = R"(#version 330 core
in vec2 vUV;
uniform vec4 uArea;     // the rectangle: x, y (from the bottom), w, h in window pixels
uniform vec3 uBase;     // the background colour
uniform vec4 uShadow;   // the shadow's centre (window pixels, from the bottom) and radii; rx 0: none
uniform int uMode;
uniform sampler2D uTex;
uniform vec4 uStage;    // regallery: the feet (window pixels from the bottom), the model's height in pixels, an alpha
uniform vec3 uPanels;   // regallery: the left column's edge, the file panel's edge (x), the controls' top (from the bottom)
out vec4 oColor;
// regallery: the stage: a soft light on the wall behind the model, a pool of it on the floor round the feet, the
// shadow, a little dither (no banding in the dark)
vec3 stage() {
    vec2 q = gl_FragCoord.xy;
    float mh = max(uStage.z, 1.0);
    vec2 w = vec2((q.x - uStage.x) / (mh * 0.95), (q.y - (uStage.y + mh * 0.6)) / (mh * 0.95));
    float wall = exp(-dot(w, w) * 1.3);
    vec3 col = uBase * 0.6 + vec3(0.062, 0.056, 0.054) * wall;
    vec2 f = (q - uStage.xy) / vec2(max(uShadow.z, 1.0) * 3.4, max(uShadow.w, 1.0) * 3.4);
    col += vec3(0.075, 0.068, 0.064) * exp(-dot(f, f) * 1.1);
    if (uShadow.z > 0.0) {
        vec2 s = (q - uShadow.xy) / uShadow.zw;
        col *= 1.0 - 0.8 * (1.0 - smoothstep(0.1, 1.0, length(s)));
    }
    col += (fract(sin(dot(q, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;
    return max(col, vec3(0.0));
}
void main() {
    vec2 p = (gl_FragCoord.xy - uArea.xy) / uArea.zw;   // 0..1 over the rectangle
    if (uMode == 2) {   // (the Remake layout's page, Gallery::drawSplit)
        oColor = texture(uTex, vUV);
        return;
    }
    if (uMode == 3) {   // regallery: the stage
        oColor = vec4(stage(), 1.0);
        return;
    }
    if (uMode == 4) {   // the stage over the floor reflection: it shows faintly under the feet, fading down
        float d = uStage.y - gl_FragCoord.y;
        float a = d <= 0.0 ? 1.0 : 1.0 - 0.3 * (1.0 - smoothstep(0.0, uStage.z * 0.45, d));
        oColor = vec4(stage(), a);
        return;
    }
    if (uMode == 5) {   // the stage over the model at an alpha (a new entry fading in)
        oColor = vec4(stage(), uStage.w);
        return;
    }
    if (uMode == 6) {   // the vignette and the page's panels' shade
        vec2 q = (p - 0.5) * 2.0;
        float a = smoothstep(0.6, 1.5, length(q)) * 0.6;
        float x = gl_FragCoord.x, k = uArea.w * 0.1;
        if (uPanels.x > 0.0) a = max(a, 0.38 * (1.0 - smoothstep(uPanels.x - k, uPanels.x + k * 1.6, x)));
        if (uPanels.y < uArea.x + uArea.z) a = max(a, 0.42 * smoothstep(uPanels.y - k * 1.6, uPanels.y + k, x));
        if (uPanels.z > 0.0) a = max(a, 0.55 * (1.0 - smoothstep(uPanels.z - k, uPanels.z + k * 0.5, gl_FragCoord.y)));
        oColor = vec4(0.0, 0.0, 0.0, a);
        return;
    }
    if (uMode == 1) {
        vec2 q = (p - 0.5) * 2.0;
        float v = smoothstep(0.55, 1.45, length(q));
        oColor = vec4(0.0, 0.0, 0.0, v * 0.65);
        return;
    }
    vec2 c = p - vec2(0.5, 0.56);
    c.x *= uArea.z / uArea.w;
    float spot = exp(-dot(c, c) * 3.2);
    vec3 col = uBase * (0.55 + 1.6 * spot) + vec3(0.050, 0.046, 0.044) * spot;
    if (uShadow.z > 0.0) {
        vec2 s = (gl_FragCoord.xy - uShadow.xy) / uShadow.zw;
        float d = length(s);
        col *= 1.0 - 0.75 * (1.0 - smoothstep(0.15, 1.0, d));
    }
    oColor = vec4(min(col, vec3(1.0)), 1.0);
}
)";
}   // namespace

bool Gallery::backdropInit(std::string& err)
{
    auto compile = [&](gl::GLenum type, const char* src) -> gl::GLuint {
        gl::GLuint s = gl::CreateShader(type);
        gl::ShaderSource(s, 1, &src, nullptr);
        gl::CompileShader(s);
        gl::shaderLog(s, "gallery");
        gl::GLint ok = 0;
        gl::GetShaderiv(s, gl::COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024];
            gl::GetShaderInfoLog(s, sizeof log, nullptr, log);
            err = std::string("gallery backdrop shader: ") + log;
            gl::DeleteShader(s);
            return 0;
        }
        return s;
    };
    const gl::GLuint vs = compile(gl::VERTEX_SHADER, kBackVs), fs = vs ? compile(gl::FRAGMENT_SHADER, kBackFs) : 0;
    if (!vs || !fs) {
        if (vs) gl::DeleteShader(vs);
        return false;
    }
    backProgram_ = gl::CreateProgram();
    gl::AttachShader(backProgram_, vs);
    gl::AttachShader(backProgram_, fs);
    gl::LinkProgram(backProgram_);
    gl::programLog(backProgram_, "gallery");
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);
    gl::GLint ok = 0;
    gl::GetProgramiv(backProgram_, gl::LINK_STATUS, &ok);
    if (!ok) {
        err = "gallery backdrop shader link failed";
        return false;
    }
    uArea_ = gl::GetUniformLocation(backProgram_, "uArea");
    uBase_ = gl::GetUniformLocation(backProgram_, "uBase");
    uShadow_ = gl::GetUniformLocation(backProgram_, "uShadow");
    uMode_ = gl::GetUniformLocation(backProgram_, "uMode");
    uStage_ = gl::GetUniformLocation(backProgram_, "uStage");
    uPanels_ = gl::GetUniformLocation(backProgram_, "uPanels");
    uPos_ = gl::GetUniformLocation(backProgram_, "uPos");
    uUv_ = gl::GetUniformLocation(backProgram_, "uUv");
    gl::UseProgram(backProgram_);
    gl::Uniform1i(gl::GetUniformLocation(backProgram_, "uTex"), 0);
    gl::GenVertexArrays(1, &backVao_);
    return true;
}

// ---- gallery3, the Remake layout ----
void Gallery::splitRect(float ww, float wh, float& scale, float x0[3], float& y0)
{
    scale = std::min(ww / 640.0f, wh / 480.0f);
    y0 = (wh - 480.0f * scale) / 2.0f;
    const float left = (ww - 640.0f * scale) / 2.0f;
    if (ww / wh <= 4.0f / 3.0f + 0.001f) {   // 4:3 or narrower: one picture
        x0[0] = left, x0[1] = left + 200.0f * scale, x0[2] = left + 440.0f * scale;
        return;
    }
    x0[0] = 0;
    x0[1] = ww / 2.0f - 120.0f * scale;
    x0[2] = ww - 200.0f * scale;
}

void Gallery::drawSplit(const std::vector<uint8_t>& rgba, int ww, int wh)
{
    if (!open_ || rgba.size() < size_t(640 * 480 * 4)) return;
    if (!splitTex_) gl::GenTextures(1, &splitTex_);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, splitTex_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, 640, 480, 0, gl::RGBA, gl::UNSIGNED_BYTE, rgba.data());
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    float s, x0[3], y0;
    splitRect(float(ww), float(wh), s, x0, y0);
    gl::Viewport(0, 0, ww, wh);
    gl::Disable(gl::DEPTH_TEST);
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::UseProgram(backProgram_);
    gl::Uniform1i(uMode_, 2);
    gl::BindVertexArray(backVao_);
    const float parts[4] = {0, 200, 440, 640};
    for (int i = 0; i < 3; ++i) {
        const float pw = (parts[i + 1] - parts[i]) * s;
        gl::Uniform4f(uPos_, -1.0f + 2.0f * x0[i] / float(ww), 1.0f - 2.0f * (y0 + 480.0f * s) / float(wh),
                      2.0f * pw / float(ww), 2.0f * 480.0f * s / float(wh));
        gl::Uniform4f(uUv_, parts[i] / 640.0f, 0, (parts[i + 1] - parts[i]) / 640.0f, 1);
        gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
    }
    gl::BindVertexArray(0);
    gl::Disable(gl::BLEND);
    gl::Enable(gl::DEPTH_TEST);
}

const std::vector<uint8_t>* Gallery::thumb(int entry) const
{
    const auto it = thumbs_.find({int(source_), entry});
    return it == thumbs_.end() || it->second.empty() ? nullptr : &it->second;
}

void Gallery::makeThumbs(int ww, int wh, int category, int most)
{
    if (!open_ || ww < 2 * kThumbW || wh < 2 * kThumbH) return;
    const auto& es = galleryEntries();
    for (int i = 0; i < int(es.size()) && most > 0; ++i) {
        if (es[size_t(i)].category != category || thumbs_.count({int(source_), i})) continue;
        --most;
        // the entry as the window shows it at first (its front, the first motion's first frame), the view kept
        const int keepIndex = index_, keepMotion = motion_, keepBg = background, keepLight = light;
        const std::vector<int> keepMotions = motions_;
        const float keepFrame = frame_, keepC[3] = {center_[0], center_[1], center_[2]}, keepR = radius_, keepH = halfH_;
        const float keepView[10] = {yaw_, pitch_, dist_, wantYaw_, wantPitch_, wantDist_, panX_, panY_, wantPanX_, wantPanY_};
        const bool keepFramed = framed_;
        const std::string keepStatus = status_;
        index_ = i;
        pick();
        yaw_ = 3.14159265f + es[size_t(i)].yaw * 3.14159265f / 180.0f;
        pitch_ = es[size_t(i)].pitch != 0 ? es[size_t(i)].pitch * 3.14159265f / 180.0f : -0.08f;
        // a portrait: the head and chest (items: the whole thing)
        const bool whole = es[size_t(i)].category == kGalItems;
        dist_ = whole ? 0.92f : 0.5f, panX_ = 0, panY_ = whole ? 0.0f : 0.32f;
        light = 0;
        const int w = 2 * kThumbW, h = 2 * kThumbH;
        thumbMode_ = true;
        draw(w, h, 0, 0, w, h);   // (the window's bottom-left corner as a w x h window: no skew)
        thumbMode_ = false;
        std::vector<uint8_t>& out = thumbs_[{int(source_), i}];
        if (status_.empty()) {
            std::vector<uint8_t> px(size_t(w) * h * 4);
            gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
            gl::ReadPixels(0, 0, w, h, gl::RGBA, gl::UNSIGNED_BYTE, px.data());
            out.assign(size_t(kThumbW) * kThumbH * 4, 0);
            const bool shadow = es[size_t(i)].category == kGalCut;   // (cut content: a silhouette)
            for (int y = 0; y < kThumbH; ++y)
                for (int x = 0; x < kThumbW; ++x) {
                    int sum[4] = {};
                    for (int k = 0; k < 4; ++k) {   // 2 x 2 down, rows from the bottom
                        const uint8_t* p = &px[(size_t(h - 1 - (2 * y + (k >> 1))) * w + size_t(2 * x + (k & 1))) * 4];
                        const bool key = p[0] > 240 && p[1] < 16 && p[2] > 240;
                        if (key) continue;
                        sum[0] += p[0], sum[1] += p[1], sum[2] += p[2], sum[3] += 255;
                    }
                    uint8_t* o = &out[(size_t(y) * kThumbW + size_t(x)) * 4];
                    const int n = sum[3] / 255;
                    if (!n) continue;
                    for (int c = 0; c < 3; ++c) o[c] = uint8_t(shadow ? (c == 2 ? 30 : 18) : sum[c] / n);
                    o[3] = uint8_t(sum[3] / 4);
                }
        }
        index_ = keepIndex, motion_ = keepMotion, background = keepBg, light = keepLight;
        motions_ = keepMotions;
        frame_ = keepFrame, std::copy_n(keepC, 3, center_), radius_ = keepR, halfH_ = keepH;
        yaw_ = keepView[0], pitch_ = keepView[1], dist_ = keepView[2], wantYaw_ = keepView[3], wantPitch_ = keepView[4];
        wantDist_ = keepView[5], panX_ = keepView[6], panY_ = keepView[7], wantPanX_ = keepView[8], wantPanY_ = keepView[9];
        framed_ = keepFramed;
        status_ = keepStatus;
    }
}

const GalleryEntry& Gallery::entryNow() const
{
    return portraitOn_ ? portraitEntry_ : galleryEntries()[size_t(index_)];
}

bool Gallery::makePortrait(int type, int entry, const char* motions, int w, int h, float yawDeg, float pitchDeg, float dist,
                           float panX, float panY, std::vector<uint8_t>& rgba)
{
    rgba.clear();
    const auto& es = galleryEntries();
    if (!open_ || entry < 0 || entry >= int(es.size()) || w <= 0 || h <= 0) return false;
    const int keepIndex = index_, keepMotion = motion_, keepBg = background, keepLight = light;
    const std::vector<int> keepMotions = motions_;
    const float keepFrame = frame_, keepC[3] = {center_[0], center_[1], center_[2]}, keepR = radius_, keepH = halfH_;
    const float keepView[10] = {yaw_, pitch_, dist_, wantYaw_, wantPitch_, wantDist_, panX_, panY_, wantPanX_, wantPanY_};
    const bool keepFramed = framed_;
    const std::string keepStatus = status_;
    index_ = entry;
    portraitEntry_ = es[size_t(entry)];
    portraitEntry_.type = type;
    if (motions) portraitEntry_.motions = motions;
    // curdead (user 2026-10-30: "why does Dr. Curien (Dead) look like that?"): the arcade look's texture area must be
    // in the stage it is drawn from. Dr. Curien (Dead) (78) has no entry: drawn as another's (Chapter 1's), whose stage
    // has no area 12 (the Magician's room, where the game draws his body), so his parts took the stage's first set: a
    // striped red face, a black and white coat. The first stage that has the area (Chapter 4's has every one), else as before
    if (source_ == Source::Arcade)
        if (const GalleryLook look = galleryLook(type); look.area > 0) {
            auto hasArea = [&](int ch) {
                const StageData* sd = stage(source_, ch);
                if (sd)
                    for (const TextureSet& ts : sd->textureSets)
                        if (ts.romArea == look.area) return true;
                return false;
            };
            if (!hasArea(portraitEntry_.chapter))
                for (int ch : {3, 2, 1, 0})
                    if (ch != portraitEntry_.chapter && hasArea(ch)) {
                        portraitEntry_.chapter = ch;
                        break;
                    }
        }
    portraitOn_ = true;
    pick();
    yaw_ = 3.14159265f + yawDeg * 3.14159265f / 180.0f;
    pitch_ = pitchDeg * 3.14159265f / 180.0f;
    dist_ = dist, panX_ = panX, panY_ = panY;
    light = 0;
    if (portraitHead.on && !motions_.empty())   // portraits2: the motion's frame
        frame_ = float(std::clamp(portraitHead.frame, 1, std::max(1, anim_.motionFrames(motions_.front()))));
    const int rw = 2 * w, rh = 2 * h;   // (drawn at twice the size, 2 x 2 down: smooth edges)
    // its own framebuffer (not the window's: a hidden or covered window's pixels cannot be read back, and the menu may
    // have another framebuffer bound when this runs)
    gl::GLuint fbo = 0, color = 0, depth = 0;
    gl::GenFramebuffers(1, &fbo);
    gl::BindFramebuffer(gl::FRAMEBUFFER, fbo);
    gl::GenTextures(1, &color);
    gl::BindTexture(gl::TEXTURE_2D, color);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, rw, rh, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, color, 0);
    gl::GenRenderbuffers(1, &depth);
    gl::BindRenderbuffer(gl::RENDERBUFFER, depth);
    gl::RenderbufferStorage(gl::RENDERBUFFER, gl::DEPTH_COMPONENT24, rw, rh);
    gl::FramebufferRenderbuffer(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::RENDERBUFFER, depth);
    const bool fboOk = gl::CheckFramebufferStatus(gl::FRAMEBUFFER) == gl::FRAMEBUFFER_COMPLETE;
    thumbMode_ = true;
    if (fboOk)
        draw(rw, rh, 0, 0, rw, rh);
    else
        status_ = "no framebuffer";
    thumbMode_ = false;
    const bool ok = status_.empty();
    if (!ok)
        std::fprintf(stderr, "gallery portrait of type %d: %s\n", type, status_.c_str());
    std::vector<uint8_t> px;
    if (ok) {
        px.resize(size_t(rw) * rh * 4);
        gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
        gl::ReadPixels(0, 0, rw, rh, gl::RGBA, gl::UNSIGNED_BYTE, px.data());
    }
    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    gl::DeleteFramebuffers(1, &fbo);
    gl::DeleteRenderbuffers(1, &depth);
    gl::DeleteTextures(1, &color);
    if (ok) {
        rgba.assign(size_t(w) * h * 4, 0);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int sum[4] = {};
                for (int k = 0; k < 4; ++k) {
                    const uint8_t* p = &px[(size_t(rh - 1 - (2 * y + (k >> 1))) * rw + size_t(2 * x + (k & 1))) * 4];
                    if (p[0] > 240 && p[1] < 16 && p[2] > 240) continue;   // (the key colour round the model)
                    sum[0] += p[0], sum[1] += p[1], sum[2] += p[2], sum[3] += 255;
                }
                const int n = sum[3] / 255;
                if (!n) continue;
                uint8_t* o = &rgba[(size_t(y) * w + size_t(x)) * 4];
                for (int c = 0; c < 3; ++c) o[c] = uint8_t(sum[c] / n);
                o[3] = uint8_t(sum[3] / 4);
            }
    }
    portraitOn_ = false;
    index_ = keepIndex, motion_ = keepMotion, background = keepBg, light = keepLight;
    motions_ = keepMotions;
    frame_ = keepFrame, std::copy_n(keepC, 3, center_), radius_ = keepR, halfH_ = keepH;
    yaw_ = keepView[0], pitch_ = keepView[1], dist_ = keepView[2], wantYaw_ = keepView[3], wantPitch_ = keepView[4];
    wantDist_ = keepView[5], panX_ = keepView[6], panY_ = keepView[7], wantPanX_ = keepView[8], wantPanY_ = keepView[9];
    framed_ = keepFramed;
    status_ = keepStatus;
    return ok;
}

// the bosses' intro cards (the type and the weak point): the arcade sprites the boss tasks place on the HUD layer
// (boss_chariot.cpp chariot_title_card, boss_hangedman.cpp, boss_hermit.cpp, boss_magician.cpp: sprite, cell x, cell y)
// drawn from the PC's copies (sprite_map_pc.inc) where the game puts them (a cell: 10 pixels of 640 x 480), cropped
namespace {
struct CardSprite { const char* key; int chapter; uint16_t sprite[2]; int cell[2][2]; };
const CardSprite kCards[] = {
    {"t35", 1, {0xDC, 0xDF}, {{33, 5}, {7, 5}}},
    {"t36", 1, {0xDC, 0xDF}, {{33, 5}, {7, 5}}},
    {"t50", 2, {0xDD, 0xE0}, {{27, 17}, {7, 5}}},
    {"t64", 3, {0xDE, 0xE1}, {{1, 6}, {33, 34}}},
    {"t76", 4, {0x1C9, 0xE2}, {{34, 6}, {7, 30}}},
};
#include "assets/sprite_map_pc.inc"
}   // namespace

const std::vector<uint8_t>* Gallery::card(int& w, int& h)
{
    const std::string k = key();
    const CardSprite* cs = nullptr;
    for (const CardSprite& c : kCards)
        if (k == c.key) cs = &c;
    if (!cs || paths_.pc.empty()) return nullptr;
    auto it = cards_.find(k);
    if (it == cards_.end()) {
        std::vector<uint8_t>& out = cards_[k];
        assets::PcSprites spr;
        std::string err;
        if (spr.load(paths_.pc, err, cs->chapter, paths_.blood)) {
            // galfix2 (user: "the weakpoint image for hangedman in gallery is a little too small"): each sprite cropped on
            // its own and the two stacked, the type line over the picture (the game's places are far apart on the
            // screen: the Hangedman's crop was mostly empty, so its picture came out tiny)
            struct Crop { int w = 0, h = 0; std::vector<uint8_t> px; };
            Crop part[2];
            for (int i = 0; i < 2; ++i) {
                const int id = kArcadeToPcSprite[cs->sprite[i]];
                if (!id) continue;
                std::vector<uint8_t> page(size_t(640) * 480 * 4, 0);
                spr.draw(page, id, cs->cell[i][0] * 10, cs->cell[i][1] * 10);
                int x0 = 640, y0 = 480, x1 = -1, y1 = -1;
                for (int y = 0; y < 480; ++y)
                    for (int x = 0; x < 640; ++x)
                        if (page[(size_t(y) * 640 + size_t(x)) * 4 + 3])
                            x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
                if (x1 < x0) continue;
                Crop& c = part[i];
                c.w = x1 - x0 + 1, c.h = y1 - y0 + 1;
                c.px.resize(size_t(c.w) * size_t(c.h) * 4);
                for (int y = 0; y < c.h; ++y)
                    std::copy_n(&page[(size_t(y0 + y) * 640 + size_t(x0)) * 4], size_t(c.w) * 4, &c.px[size_t(y) * size_t(c.w) * 4]);
            }
            const int gap = part[0].h && part[1].h ? 4 : 0;
            const int cw = std::max(part[0].w, part[1].w), ch = part[0].h + part[1].h + gap;
            if (cw > 0 && ch > 0) {
                out.assign(4 + size_t(cw) * size_t(ch) * 4, 0);
                out[0] = uint8_t(cw), out[1] = uint8_t(cw >> 8), out[2] = uint8_t(ch), out[3] = uint8_t(ch >> 8);
                int top = 0;
                for (int i : {1, 0}) {   // (sprite 1: the type line; 0: the picture)
                    const Crop& c = part[i];
                    for (int y = 0; y < c.h; ++y)
                        std::copy_n(&c.px[size_t(y) * size_t(c.w) * 4], size_t(c.w) * 4, &out[4 + (size_t(top + y) * size_t(cw)) * 4]);
                    if (c.h) top += c.h + gap;
                }
            }
        } else {
            SDL_Log("gallery: card sprites: %s", err.c_str());
        }
        it = cards_.find(k);
    }
    if (it->second.size() < 4) return nullptr;
    w = it->second[0] | (it->second[1] << 8);
    h = it->second[2] | (it->second[3] << 8);
    return &it->second;
}

void Gallery::backdropDraw(int mode, int ww, int wh, int x, int y, int w, int h, const float base[3], const float shadow[4])
{
    gl::Viewport(0, 0, ww, wh);
    gl::Disable(gl::DEPTH_TEST);
    if (mode == 1 || mode == 4 || mode == 5 || mode == 6) {
        gl::Enable(gl::BLEND);
        gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    } else {
        gl::Disable(gl::BLEND);
    }
    gl::UseProgram(backProgram_);
    gl::Uniform4f(uArea_, float(x), float(wh - (y + h)), float(w), float(h));
    gl::Uniform3f(uBase_, base[0], base[1], base[2]);
    gl::Uniform4f(uShadow_, shadow[0], shadow[1], shadow[2], shadow[3]);
    gl::Uniform1i(uMode_, mode);
    gl::Uniform4f(uStage_, stage_[0], stage_[1], stage_[2], stage_[3]);
    gl::Uniform3f(uPanels_, rePanels[0], rePanels[1] > 0 ? rePanels[1] : float(ww), rePanels[2] > 0 ? float(wh) - rePanels[2] : 0.0f);
    gl::Uniform4f(uPos_, -1, -1, 2, 2);
    gl::BindVertexArray(backVao_);
    gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
    gl::BindVertexArray(0);
    gl::Disable(gl::BLEND);
    gl::Enable(gl::DEPTH_TEST);
}

void Gallery::update(float dt)
{
    if (!open_) return;
    if (autoTurn) {
        yaw_ += dt * 0.5f;
        wantYaw_ += dt * 0.5f;
    }
    // smooth: the view eases to the wanted angles and distance (about 90 % of the way in a quarter second)
    const float k = 1.0f - std::exp(-dt * 10.0f);
    yaw_ += (wantYaw_ - yaw_) * k;
    pitch_ += (wantPitch_ - pitch_) * k;
    dist_ += (wantDist_ - dist_) * k;
    panX_ += (wantPanX_ - panX_) * k;
    panY_ += (wantPanY_ - panY_) * k;
    fadeIn_ = std::min(1.0f, fadeIn_ + dt / 0.28f);
    tick_ += dt * 60.0f;   // the game's frame counter for the hook models (wings, eyes, veins, the saw)
    if (!motions_.empty() && playing && entryNow().type >= 0) {
        const int frames = std::max(1, anim_.motionFrames(motions_[size_t(motion_)]));
        frame_ = std::fmod(frame_ - 1 + dt * 60.0f, float(frames)) + 1;
    }
}

void Gallery::draw(int ww, int wh, int x, int y, int w, int h, float lift, const int* frame)
{
    status_.clear();
    if (!open_ || w <= 0 || h <= 0) return;
    const int boxX = x, boxY = y, boxW = w, boxH = h;   // the whole rectangle (the backdrop); the model: above `lift`
    if (frame) x = frame[0], y = frame[1], w = std::max(1, frame[2]), h = std::max(1, frame[3]);
    h = std::max(1, int(float(h) * (1.0f - std::clamp(lift, 0.0f, 0.5f))));
    if (!thumbMode_) lastH_ = h;
    const GalleryEntry& e = entryNow();
    const int chapter = entryChapter(e);
    const StageData* sd = stage(source_, chapter);
    if (!sd) {
        status_ = std::string("The ") + sourceName(source_) + " files could not be read for this one";
        return;
    }
    const bool still = e.type < 0;   // items, props: models without a skeleton (motions_ = the steps' object IDs)
    if (motions_.empty() || (!still && e.type >= anim_.typeCount())) {
        status_ = "No skeleton for this one";
        return;
    }
    static const Skeleton kNoSkeleton;
    const Skeleton& sk = still ? kNoSkeleton : anim_.skeleton(e.type);
    const int m = motions_[size_t(motion_)];
    const bool arcade = sd->source == Source::Arcade;
    // the in-game look (galleryLook): the texture set of the area the creature is drawn in, its colour rows (still
    // models: the entry's own area and rows)
    const GalleryLook look = still ? GalleryLook{e.area, e.rows} : e.clay && clayCut ? GalleryLook{} : galleryLook(e.type);
    int areaSet = -1;
    for (size_t i = 0; i < sd->textureSets.size(); ++i)
        if (arcade && look.area > 0 && sd->textureSets[i].romArea == look.area) areaSet = int(i);
    bloodRowMask_ = 0;
    if (arcade && areaSet >= 0) {
        std::string& applied = xlatRows_[{chapter, areaSet}];
        // gallery blood: the look's blood rows in the blood colour (GGSET's lists), the custom colour (and the arcade's
        // yellow) recoloured over them as GameView does
        std::set<int> bloodRows;
        const bool recolour = bloodSet_;   // (the portraits too: the same translation, not rebuilt between them)
        const std::string rows = recolour ? lookRows(look.rows, bloodRows) : std::string(look.rows ? look.rows : "");
        // polish (user 2026-11-03: in the Gallery the bosses did not cycle, Harris's pale pink wound stripes stayed
        // pink): Rainbow Blood's per-pixel recolour of these rows (renderer uBloodRows), as GameView passes the game's
        // GGSET rows. Without it only the red-pixel fallback (woundTint) ran: the people / bosses (hue 4: no fallback)
        // kept their red, the pale shades of an enemy's blood rows missed the red test
        for (int row : bloodRows)
            if (row >= 0 && row < 32) bloodRowMask_ |= 1u << row;
        const int tint = !recolour || blood_.rainbow.on ? -1 : blood_.color == 5 ? blood_.rgb : blood_.color == 4 ? kArcadeYellow : -1;
        const std::string want = rows + (tint >= 0 ? "#" + std::to_string(tint) : std::string());
        if (applied != want) {   // color_row_set's rows into the set's colour translation, as GameView does
            std::vector<uint8_t> xlat;
            arcade_->colorXlat(look.area, parseColorRows(rows.c_str()), xlat);
            if (tint >= 0) tintRows(xlat, bloodRows, tint);
            renderer_.updateXlat(*sd, areaSet, xlat);
            applied = want;
        }
    }
    std::vector<uint32_t> models;
    std::vector<int> placedNode;   // placements_[i]'s skeleton node (-1: none)
    std::vector<uint32_t> partModel;   // gallery dismemberment: each node's model now
    std::vector<bool> partGone;        // (a cut part)
    auto posed = [&](int motion, int frame) {
        if (still)
            scratch_.clear();
        else
            anim_.pose(e.type, motion, std::clamp(frame, 1, std::max(1, anim_.motionFrames(motion))), scratch_);
        // portraits2: the head (class 1) turned round the neck, the shoulders (classes 3 / 2: left / right) dropped,
        // each with what hangs from it, in world axes (y up)
        const PortraitHead& ph = portraitHead;
        if (!still && portraitOn_ && ph.on) {
            auto turn = [&](int cls, float yaw, float pitch, float roll) {
                if (yaw == 0 && pitch == 0 && roll == 0) return;
                int top = -1;
                for (size_t n = 0; n < sk.nodes.size() && n < scratch_.size() && top < 0; ++n)
                    if (anim_.partClass(e.type, sk.nodes[n].joint) == cls) top = int(n);
                if (top < 0) return;
                const float k = 3.14159265f / 180.0f, cy = std::cos(yaw * k), sy = std::sin(yaw * k), cp = std::cos(pitch * k),
                            sp = std::sin(pitch * k), cr = std::cos(roll * k), sr = std::sin(roll * k);
                const float ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy}, rx[9] = {1, 0, 0, 0, cp, -sp, 0, sp, cp},
                            rz[9] = {cr, -sr, 0, sr, cr, 0, 0, 0, 1};
                Xform a, b, c;
                std::copy_n(ry, 9, a.rot), std::copy_n(rx, 9, b.rot), std::copy_n(rz, 9, c.rot);
                Xform t = a * b * c;
                const float* pv = scratch_[size_t(top)].pos;
                for (int i = 0; i < 3; ++i)
                    t.pos[i] = pv[i] - (t.rot[i * 3] * pv[0] + t.rot[i * 3 + 1] * pv[1] + t.rot[i * 3 + 2] * pv[2]);
                std::vector<bool> in(scratch_.size(), false);
                for (size_t n = size_t(top); n < sk.nodes.size() && n < scratch_.size(); ++n) {
                    in[n] = int(n) == top || (sk.nodes[n].parent >= 0 && in[size_t(sk.nodes[n].parent)]);
                    if (in[n]) scratch_[n] = t * scratch_[n];
                }
            };
            turn(1, ph.headYaw, ph.headPitch, ph.headRoll);
            turn(3, 0, ph.armDropX, ph.armDrop);
            turn(2, 0, -ph.armDropX, -ph.armDrop);
        }
        placements_.clear();
        placedNode.clear();
        placements_.reserve(4 * sk.nodes.size() + 2);
        auto place = [&](uint32_t id, const Xform& xf, int node = -1) {
            auto it = sd->models.find(id);
            if (it == sd->models.end() || it->second.triangleCount() == 0) return;
            Placement pl;
            pl.id = pl.modelKey = id;
            toPlacement(xf, pl);
            // in place: the root node's ground travel taken out (moving motions stay in the window)
            if (!scratch_.empty()) { pl.pos[0] -= scratch_[0].pos[0]; pl.pos[2] -= scratch_[0].pos[2]; }
            placements_.push_back(pl);
            placedNode.push_back(node);
        };
        if (still) {   // the step's model as it is
            place(uint32_t(motion), Xform{});
            return;
        }
        // gallery dismemberment: the parts' damage models, the cut parts left out
        damageModels(sk, partModel, partGone);
        for (size_t n = 0; n < sk.nodes.size() && n < scratch_.size(); ++n) {
            if (!sk.nodes[n].model || partGone[n] || partModel[n] <= kNoModel) continue;
            // what body_part_draw_hook draws for this part (another model in its place, extras with its matrix)
            PartDrawState ps;
            ps.type = e.type;
            ps.role = sk.nodes[n].joint;
            ps.cls = anim_.partClass(e.type, ps.role);
            ps.roleId = anim_.partClass(e.type, ps.role, 0);
            ps.motion = motion;
            ps.frame = frame - 1;
            ps.tick = int(tick_);
            ps.model = partModel[n];
            ps.pcModels = !arcade;
            partDrawModels(ps, models);
            for (uint32_t id : models) place(id, scratch_[n], int(n));
        }
        // relfix (the v1.2.0 checklist: the Gallery never showed Big Head): the Mods page's Big Head as in play
        // (cosmods.cpp bigHeadFrame): the topmost class-1 part and every part under it scaled about its joint. Not
        // the PC Mode portraits (their own head framing)
        if (!portraitOn_ && bigHeadTypeOn(e.type)) {
            const float sc = bigHeadSettings().scale;
            std::vector<int> headOf(sk.nodes.size(), -1);   // (parents come before their children)
            for (size_t n = 0; n < sk.nodes.size(); ++n) {
                const int p = sk.nodes[n].parent;
                headOf[n] = p >= 0 && size_t(p) < n && headOf[size_t(p)] >= 0 ? headOf[size_t(p)]
                            : anim_.partClass(e.type, sk.nodes[n].joint) == 1 ? int(n) : -1;
            }
            for (size_t i = 0; i < placements_.size(); ++i) {
                const int n = placedNode[i];
                const int h = n >= 0 && size_t(n) < headOf.size() ? headOf[size_t(n)] : -1;
                if (h < 0 || size_t(h) >= scratch_.size()) continue;
                float P[3] = {scratch_[size_t(h)].pos[0], scratch_[size_t(h)].pos[1], scratch_[size_t(h)].pos[2]};
                P[0] -= scratch_[0].pos[0], P[2] -= scratch_[0].pos[2];   // (as place(): the ground travel out)
                Placement& pl = placements_[i];
                for (float& v : pl.rot) v *= sc;
                for (int k = 0; k < 3; ++k) pl.pos[k] = P[k] + (pl.pos[k] - P[k]) * sc;
            }
        }
        // the generated skin (skin_draw: the belly between the anchor parts of classes 8 / 9), arcade only
        ArcadeSource::Skin skin;
        auto mit = arcade ? stages_.find({int(source_), chapter}) : stages_.end();
        if (mit != stages_.end() && mit->second && arcade_->skinInfo(e.type, skin, 0)) {
            const int a = anim_.nodeOfRole(e.type, skin.nodeA), b = anim_.nodeOfRole(e.type, skin.nodeB);
            auto model = mit->second->models.find(kSkinKey + uint32_t(e.type));
            if (a >= 0 && b >= 0 && size_t(std::max(a, b)) < scratch_.size() && model != mit->second->models.end()) {
                const Xform rel = inverse(scratch_[size_t(b)]) * scratch_[size_t(a)];
                std::vector<std::array<float, 3>> moved;
                for (const auto& c : skin.corners) {
                    std::array<float, 3> v;
                    for (int i = 0; i < 3; ++i)
                        v[i] = rel.rot[i * 3] * c[0] + rel.rot[i * 3 + 1] * c[1] + rel.rot[i * 3 + 2] * c[2] + rel.pos[i];
                    moved.push_back(v);
                }
                arcade_->rebuildSkin(chapter, skin, moved, model->second);
                renderer_.updateModel(*sd, model->second);
                place(model->first, scratch_[size_t(b)]);
            }
        }
    };
    if (!framed_) {   // the framing: the parts' boxes over the motion (again for each motion)
        float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
        // galfix (user 2026-10-26: "the Chariot is too zoomed out by default, the weapon counted as part of him"): a
        // held weapon is in the hand's own model (hyum_motiter: the Chariot's halberd; Cyril's axe, Samson's saw,
        // Burner's torch): a hand part (classes 6 / 7 / 16, a leaf) whose model is over 1.6 times its forearm's size
        // is left out of the framing (the body fills the view; the weapon may reach out of it)
        std::vector<bool> skip(sk.nodes.size(), false);
        auto extent = [&](uint32_t id) {
            const auto it = sd->models.find(id);
            if (it == sd->models.end()) return 0.0f;
            const Model& md = it->second;
            return std::max({md.boundsMax[0] - md.boundsMin[0], md.boundsMax[1] - md.boundsMin[1], md.boundsMax[2] - md.boundsMin[2]});
        };
        for (size_t n = 0; n < sk.nodes.size(); ++n) {
            const SkeletonNode& nd = sk.nodes[n];
            const int cls = anim_.partClass(e.type, nd.joint);
            if (!nd.model || nd.parent < 0 || !sk.nodes[size_t(nd.parent)].model || (cls != 6 && cls != 7 && cls != 16)) continue;
            bool leaf = true;
            for (const SkeletonNode& c : sk.nodes) leaf = leaf && c.parent != int(n);
            if (leaf && extent(nd.model) > 1.6f * extent(sk.nodes[size_t(nd.parent)].model)) skip[n] = true;
        }
        // a few frames over the whole motion (jumps, raised arms); still models: the first step (the whole one)
        const int frames = still ? 1 : std::max(1, anim_.motionFrames(m));
        noDamage_ = true;   // (gallery dismemberment: framed whole, the view stays as parts come and go)
        for (int fr = 1; fr <= frames; fr += std::max(1, frames / 8)) {
            posed(still ? motions_.front() : m, fr);
            for (size_t i = 0; i < placements_.size(); ++i) {
                const Placement& pl = placements_[i];
                if (placedNode[i] >= 0 && skip[size_t(placedNode[i])]) continue;
                const Model& md = sd->models.at(pl.modelKey);
                for (int c = 0; c < 8; ++c) {
                    const float v[3] = {(c & 1) ? md.boundsMax[0] : md.boundsMin[0], (c & 2) ? md.boundsMax[1] : md.boundsMin[1],
                                        (c & 4) ? md.boundsMax[2] : md.boundsMin[2]};
                    for (int k = 0; k < 3; ++k) {
                        const float p = pl.rot[k * 3] * v[0] + pl.rot[k * 3 + 1] * v[1] + pl.rot[k * 3 + 2] * v[2] + pl.pos[k];
                        lo[k] = std::min(lo[k], p);
                        hi[k] = std::max(hi[k], p);
                    }
                }
            }
        }
        if (!placements_.empty()) {
            for (int k = 0; k < 3; ++k) center_[k] = (lo[k] + hi[k]) / 2;
            halfH_ = std::max(0.5f, (hi[1] - lo[1]) / 2);
            radius_ = std::max(0.5f, 0.5f * std::hypot(hi[0] - lo[0], hi[2] - lo[2]));   // across, any way it turns
        }
        noDamage_ = false;
        framed_ = true;
    }
    posed(m, int(frame_));
    if (placements_.empty()) {
        status_ = std::string("Not in the ") + sourceName(source_) + " files";
        return;
    }
    std::vector<DrawItem> items;
    for (const Placement& pl : placements_) {
        DrawItem d;
        d.stage = sd;
        d.model = &sd->models.at(pl.modelKey);
        d.placement = &pl;
        d.variant = d.model->preferredVariant;
        if (d.variant < 0 && !d.model->validVariants.empty()) d.variant = d.model->validVariants.front();
        if (d.variant < 0 && arcade && !sd->textureSets.empty()) d.variant = 0;
        // GameView's pickVariant: the area's set for the models whose textures it holds
        const std::vector<int>& ok = d.model->validVariants;
        if (areaSet >= 0 && (ok.empty() || std::find(ok.begin(), ok.end(), areaSet) != ok.end())) d.variant = areaSet;
        // the creatures' light (object_draw: light record 1, the draw param creature_type_setup gives every body): straight
        // up in world space, as the game draws them (the world's own light points down)
        d.hasLight = true;
        d.light[1] = 1;
        if (light > 0) {   // gallery3: from the side instead, tilted 55 degrees, its way round fixed to the view
            const float a = yaw_ + float(light - 1) * 0.78539816f, s = 0.819f;
            d.light[0] = s * std::sin(a), d.light[1] = 0.574f, d.light[2] = s * std::cos(a);
        }
        // the cut types without textures: every polygon in one light entry of the stage's palette (Chapter 1's 700, an
        // ivory; the flash's flat drawing, DrawItem::flat)
        if (e.clay && arcade && (clayCut || !galleryCutLook(e.type))) d.flat = 700;   // (U: untextured, as first shown)
        items.push_back(d);
    }
    Camera cam;
    cam.fovY = 0.7f;
    cam.yaw = yaw_;
    cam.pitch = pitch_;
    float r[3], u[3], f[3];
    cam.basis(r, u, f);
    // the whole model fits the rectangle's height (and width, whichever way it turns) at zoom 1: the box's half height
    // over the vertical half angle, its half width across over the horizontal one, from its near side, a little margin
    const float aspect = float(w) / float(h);
    const float tanV = std::tan(cam.fovY / 2), tanH = tanV * aspect;
    const float fit = std::max(halfH_ / tanV, radius_ / tanH) * 1.06f + radius_;
    const float d = fit * dist_;
    // the camera sees the window; the rectangle's centre is the window's when it is centred (the menu's picture)
    for (int k = 0; k < 3; ++k) cam.pos[k] = center_[k] - f[k] * d;
    cam.fovY = 2.0f * std::atan(std::tan(cam.fovY / 2) * float(wh) / float(h));   // the rectangle's height = fovY
    // the rectangle's centre off the window's: the camera moves the other way (the model stays in the rectangle's middle)
    const float t = std::tan(cam.fovY / 2) * d;
    const float ox = (float(x) + float(w) / 2 - float(ww) / 2) / (float(wh) / 2);
    const float oy = (float(y) + float(h) / 2 - float(wh) / 2) / (float(wh) / 2);
    for (int k = 0; k < 3; ++k) cam.pos[k] += -r[k] * ox * t + u[k] * oy * t;
    // gallery3: the pan (in the rectangle's heights: t * 2 is the window's height at the model)
    const float ph = 2.0f * t * float(h) / float(wh);
    for (int k = 0; k < 3; ++k) cam.pos[k] += -r[k] * panX_ * ph + u[k] * panY_ * ph;
    // portraits2: the portrait's camera aimed at the head (its world box's middle), dist / pan in head heights; the light
    // from the camera's side
    if (portraitOn_ && portraitHead.on) {
        const PortraitHead& phd = portraitHead;
        float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
        for (size_t i = 0; i < placements_.size(); ++i) {
            const int n = i < placedNode.size() ? placedNode[i] : -1;
            if (n < 0 || anim_.partClass(e.type, sk.nodes[size_t(n)].joint) != 1) continue;
            const Placement& pl = placements_[i];
            const Model& md = sd->models.at(pl.modelKey);
            for (int c = 0; c < 8; ++c) {
                const float v[3] = {(c & 1) ? md.boundsMax[0] : md.boundsMin[0], (c & 2) ? md.boundsMax[1] : md.boundsMin[1],
                                    (c & 4) ? md.boundsMax[2] : md.boundsMin[2]};
                for (int k = 0; k < 3; ++k) {
                    const float p = pl.rot[k * 3] * v[0] + pl.rot[k * 3 + 1] * v[1] + pl.rot[k * 3 + 2] * v[2] + pl.pos[k];
                    lo[k] = std::min(lo[k], p), hi[k] = std::max(hi[k], p);
                }
            }
        }
        if (hi[1] > lo[1]) {
            const float hh = hi[1] - lo[1], unit = halfH_ * 0.29f;   // (a "head": 0.29 of the body's half height, the same for every body)
            if (std::getenv("HOTD_PM_PORTRAIT_LOG")) std::fprintf(stderr, "portrait type %d: head %.3f body half %.3f\n", e.type, hh, halfH_);
            cam.fovY = phd.fovDeg * 3.14159265f / 180.0f;
            // aimed at the neck (the head's joint: the same place on every body, whatever the hair; the root's ground
            // travel taken out as the placements), else the head's box
            float aim[3] = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
            for (size_t n = 0; n < sk.nodes.size() && n < scratch_.size(); ++n)
                if (anim_.partClass(e.type, sk.nodes[n].joint) == 1) {
                    for (int k = 0; k < 3; ++k) aim[k] = scratch_[n].pos[k] - (k != 1 ? scratch_[0].pos[k] : 0.0f);
                    break;
                }
            for (int k = 0; k < 3; ++k)
                cam.pos[k] = aim[k] + (r[k] * panX_ + u[k] * panY_) * unit - f[k] * dist_ * unit;
        }
        if (phd.lightDeg >= 0) {
            const float a = yaw_ + phd.lightDeg * 3.14159265f / 180.0f, s = std::sqrt(std::max(0.0f, 1 - phd.lightUp * phd.lightUp));
            for (DrawItem& di : items) di.light[0] = s * std::sin(a), di.light[1] = phd.lightUp, di.light[2] = s * std::cos(a);
        }
    }
    // gallery blood: Rainbow Blood's creature colour on the parts (as GameView: an enemy's wounds and blood texels)
    if (bloodSet_ && !still) {
        const bool enemy = e.category == kGalCreatures || e.category == kGalCut;
        const float own = 0.08f + 0.84f * float((uint32_t(e.type + 1) * 2654435761u) >> 16 & 0xFFFF) / 65536.0f;
        float hue = -1.0f;
        if (blood_.rainbow.on)   // (cosmods.h creatureBloodHue: 0..1 its own hue, 2 + cycling, 4 + its wounds as painted)
            hue = !enemy || blood_.rainbow.creature == 2 ? 4.0f : blood_.rainbow.creature == 1 ? 2.0f + own : own;
        else if (blood_.color == 5 && arcade && enemy)
            hue = 0.0f;   // (the custom colour on the wounds' red: renderer mode 3)
        for (DrawItem& di : items) di.bloodHue = hue;
    }
    // gallery blood: Blood Particles: a hit's blood burst on a part given a damage step (blood_burst: models 7..0x23 one
    // a frame, drawn in camera space at the part), and the blood dripping from the cut joints (part_bleed: a blood_drip
    // every 20 frames, models 0x46B..0x4A3 one a frame at scale 0.4; the game's bleeding ends after 100 frames, the
    // Gallery keeps it going). Drawn from the arcade's models when the shown source lacks them
    fxPlacements_.clear();
    if (bloodSet_ && !thumbMode_ && !portraitOn_ && !still) {
        auto nodePos = [&](int n, float out[3]) {
            for (int k = 0; k < 3; ++k) out[k] = scratch_[size_t(n)].pos[k] - (k != 1 && !scratch_.empty() ? scratch_[0].pos[k] : 0.0f);
        };
        if (blood_.particles) {
            std::vector<int> cuts;
            for (size_t i = 0; i < partGone.size() && i < scratch_.size(); ++i)
                if (partGone[i] && sk.nodes[i].parent >= 0 && !partGone[size_t(sk.nodes[i].parent)]) cuts.push_back(int(i));
            if (cuts.empty() || nextDrip_ > tick_ + 20.0f) nextDrip_ = tick_;
            if (!cuts.empty() && tick_ >= nextDrip_) {
                for (int c : cuts) {
                    Fx nf{1, tick_, {0, 0, 0}, c};
                    nodePos(c, nf.pos);
                    fx_.push_back(nf);
                }
                nextDrip_ = tick_ + 20.0f;
            }
        }
        fx_.erase(std::remove_if(fx_.begin(), fx_.end(), [&](const Fx& x) {
            const uint32_t last = x.kind == 0 ? kBurstLast - kBurstFirst : kDripLast - kDripFirst;
            return tick_ - x.start > float(last) + 1.0f || tick_ < x.start || (x.kind == 0 && size_t(x.node) >= scratch_.size());
        }), fx_.end());
        const StageData* arcadeSd = hasSource(Source::Arcade) ? stage(Source::Arcade, chapter) : nullptr;
        fxPlacements_.reserve(fx_.size());
        for (const Fx& fx : fx_) {
            const uint32_t id = (fx.kind == 0 ? kBurstFirst : kDripFirst) + uint32_t(tick_ - fx.start);
            const StageData* from = nullptr;
            for (const StageData* s : {sd, arcadeSd})
                if (!from && s) {
                    const auto it = s->models.find(id);
                    if (it != s->models.end() && it->second.triangleCount() > 0) from = s;
                }
            if (!from) continue;
            Placement pl;
            pl.id = pl.modelKey = id;
            if (fx.kind == 0) {   // the burst: the camera's axes (copro 0x21: drawn in camera space), at the part, a step nearer
                float p[3];
                nodePos(fx.node, p);
                for (int i = 0; i < 3; ++i) {
                    pl.pos[i] = p[i] - f[i] * 2.0f;
                    pl.rot[i * 3] = r[i], pl.rot[i * 3 + 1] = u[i], pl.rot[i * 3 + 2] = -f[i];
                }
            } else {             // the drip: scale 0.4 in world axes
                for (int i = 0; i < 3; ++i) pl.pos[i] = fx.pos[i];
                for (int i = 0; i < 9; ++i) pl.rot[i] = i % 4 == 0 ? 0.4f : 0.0f;
            }
            fxPlacements_.push_back(pl);
            DrawItem di;
            di.stage = from;
            di.model = &from->models.at(id);
            di.placement = &fxPlacements_.back();
            di.variant = di.model->preferredVariant;
            if (di.variant < 0 && !di.model->validVariants.empty()) di.variant = di.model->validVariants.front();
            if (di.variant < 0 && from->source == Source::Arcade && !from->textureSets.empty()) di.variant = 0;
            items.push_back(di);
        }
    }
    // gallery blood: the colours (the renderer's; GameView's in the game): Rainbow Blood, the custom colour on the
    // wounds, and the arcade's blood entry 1023 (the bursts, the drips, the gore) in the blood colour
    if (bloodSet_) {
        const std::vector<uint8_t>& rom = anim_.maincpu();
        const RainbowBloodSettings& rb = blood_.rainbow;
        const int custom = rb.on ? -1 : blood_.color == 5 ? blood_.rgb : blood_.color == 4 ? kArcadeYellow : -1;
        opt_.rainbow = rb.on ? (rb.together ? 2 : 1) : custom >= 0 && arcade ? 3 : 0;
        opt_.rainbowPhase = rb.on ? float(std::fmod(double(tick_) * rb.speed / 60.0, 1.0)) : 0.0f;
        opt_.rainbowGame = false;
        opt_.bloodRowMask = rb.on && arcade ? bloodRowMask_ : 0;   // (polish: the look's blood rows cycle per pixel)
        if (rb.on) {
            opt_.rainbowGame = rom.size() >= kRainbowTable + 180;
            for (int i = 0; i < 90 && opt_.rainbowGame; ++i) {
                const uint16_t v = romU16(rom, kRainbowTable + 2u * uint32_t(i));
                const int ch[3] = {v & 31, v >> 5 & 31, v >> 10 & 31};
                opt_.rainbowGame = ch[0] == 31 || ch[1] == 31 || ch[2] == 31;
                for (int c = 0; c < 3; ++c) opt_.rainbowTable[i][c] = float(ch[c]) / 31.0f;
            }
        }
        if (custom >= 0)
            for (int c = 0; c < 3; ++c) opt_.bloodRgb[c] = float(custom >> (16 - 8 * c) & 0xFF) / 255.0f;
        // entry 1023: blood_color's table colour (0x801F red ...; the rainbow: red, recoloured), the custom colour
        // as its RGB555 (R in the low bits)
        const int c = rb.on || blood_.color >= 4 ? 0 : blood_.color;
        uint16_t entry = uint16_t(romU16(rom, kBloodTable + 2u * uint32_t(c)) | 0x8000u);
        if (custom >= 0)
            entry = uint16_t(0x8000u | ((unsigned(custom) & 0xFF) >> 3) << 10 | ((unsigned(custom) >> 8 & 0xFF) >> 3) << 5 |
                             ((unsigned(custom) >> 16 & 0xFF) >> 3));
        if (entry != bloodEntry_) {
            bloodEntry_ = entry;
            std::vector<uint16_t> anim(1024, 0);
            anim[1023] = entry;
            renderer_.setPaletteAnimation(anim.data());
        }
    }
    // the background: a plain colour (the window's clear), or one of the game's skies around the camera (drawn first,
    // no depth writes, as the viewer and the game draw them), from the shown source when it has the sky model, else
    // the arcade's
    static const Backdrop kKey{"(portrait)", -1, 0, {0, 0}, 0, {1, 0, 1}};   // makeThumbs: the key colour round it
    const Backdrop& bg = thumbMode_ ? kKey : roomDebug() ? gRoomDebug : kBackdrops[std::clamp(background, 0, kBackdropCount - 1)];
    std::copy_n(bg.chapter < 0 ? bg.rgb : kBackdrops[0].rgb, 3, opt_.clear);
    // gallery3: the plain backgrounds drawn lit, with the floor shadow (the frame then keeps them: depth cleared only)
    gl::Enable(gl::SCISSOR_TEST);
    gl::Scissor(boxX, wh - (boxY + boxH), boxW, boxH);
    opt_.clearColor = bg.chapter >= 0 || thumbMode_;
    float shadow[4] = {0, 0, 0, 0};
    bool staged = false;   // regallery: the stage drawn (the fade-in redraws it over the model)
    if (bg.chapter < 0 && !thumbMode_) {
        // the floor under the middle of the box (y up), seen from the camera: its window point and the size of a
        // circle of the box's half width lying there (squashed by how steeply the camera looks down)
        const float fl[3] = {center_[0], center_[1] - halfH_, center_[2]};
        float v[3], xs = 0, ys = 0, zs = 0;
        for (int k = 0; k < 3; ++k) v[k] = fl[k] - cam.pos[k];
        for (int k = 0; k < 3; ++k) xs += v[k] * r[k], ys += v[k] * u[k], zs += v[k] * f[k];
        if (zs > 0.01f) {
            const float px = float(wh) / 2 / (zs * std::tan(cam.fovY / 2));   // window pixels per world unit there
            shadow[0] = float(ww) / 2 + xs * px;
            shadow[1] = float(wh) / 2 + ys * px;
            shadow[2] = radius_ * 0.9f * px;
            shadow[3] = shadow[2] * std::max(0.16f, std::fabs(f[1]));
            stage_[0] = shadow[0], stage_[1] = shadow[1], stage_[2] = 2.0f * halfH_ * px;
        }
        if (reStage && zs > 0.01f) {
            // regallery: the stage, then the model mirrored in the floor (y about the feet: the placements' y row and
            // position flipped; the renderer culls by the normals, which flip with them), the stage again over it but
            // for a faint band under the feet
            backdropDraw(3, ww, wh, boxX, boxY, boxW, boxH, bg.rgb, shadow);
            staged = true;
            const float floorY = center_[1] - halfH_;
            if (cam.pos[1] > floorY + 1.0f) {
                mirror_ = placements_;
                std::vector<DrawItem> refl(items.begin(), items.begin() + std::ptrdiff_t(std::min(items.size(), placements_.size())));   // (the model, not its blood)
                for (size_t i = 0; i < mirror_.size() && i < refl.size(); ++i) {
                    Placement& pl = mirror_[i];
                    pl.pos[1] = 2.0f * floorY - pl.pos[1];
                    for (int j = 0; j < 3; ++j) pl.rot[3 + j] = -pl.rot[3 + j];
                    refl[i].placement = &pl;
                }
                renderer_.draw(refl, cam, ww, wh, opt_);
                backdropDraw(4, ww, wh, boxX, boxY, boxW, boxH, bg.rgb, shadow);
            }
        } else
            backdropDraw(0, ww, wh, boxX, boxY, boxW, boxH, bg.rgb, shadow);
    }
    skyPlacements_.clear();
    roomPlacements_.clear();
    if (bg.isRoom) {
        // gallery3: a room of the game's world: the world models near the room's floor point, moved so that point is
        // under the model's feet and turned by the room's angle (the camera orbits inside the room; walls between it
        // and the model are seen from behind and culled, as single-sided polygons)
        const StageData* rs = nullptr;
        for (Source s : {source_, Source::Arcade})
            if (!rs && hasSource(s)) rs = stage(s, bg.chapter);
        if (rs) {
            int set = -1;
            for (size_t i = 0; i < rs->textureSets.size(); ++i)
                if (rs->source == Source::Arcade && rs->textureSets[i].romArea == bg.area) set = int(i);
            const float c = std::cos(bg.room[3]), s = std::sin(bg.room[3]);
            const float rotY[9] = {c, 0, s, 0, 1, 0, -s, 0, c};   // (yaw: +z toward +x)
            const float fl[3] = {center_[0], center_[1] - halfH_, center_[2]};
            const float reach = 260.0f;
            roomPlacements_.reserve(rs->placements.size());
            for (const Placement& p : rs->placements) {
                if (rs->categoryOf(p.id) != Category::World) continue;
                const auto mit = rs->models.find(p.modelKey);
                if (mit == rs->models.end() || mit->second.triangleCount() == 0) continue;
                const Model& md = mit->second;
                // the model's box (world placements: no turn) near the floor point (x / z)
                float dx = 0, dz = 0;
                const float lo0 = md.boundsMin[0] + p.pos[0], hi0 = md.boundsMax[0] + p.pos[0];
                const float lo2 = md.boundsMin[2] + p.pos[2], hi2 = md.boundsMax[2] + p.pos[2];
                dx = std::max({0.0f, lo0 - bg.room[0], bg.room[0] - hi0});
                dz = std::max({0.0f, lo2 - bg.room[2], bg.room[2] - hi2});
                if (dx * dx + dz * dz > reach * reach) continue;
                Placement q = p;
                float v[3];
                for (int k = 0; k < 3; ++k) v[k] = p.pos[k] - bg.room[k];
                for (int i = 0; i < 3; ++i) {
                    q.pos[i] = rotY[i * 3] * v[0] + rotY[i * 3 + 1] * v[1] + rotY[i * 3 + 2] * v[2] + fl[i];
                    for (int j = 0; j < 3; ++j)
                        q.rot[i * 3 + j] = rotY[i * 3] * p.rot[j] + rotY[i * 3 + 1] * p.rot[3 + j] + rotY[i * 3 + 2] * p.rot[6 + j];
                }
                roomPlacements_.push_back(q);
                DrawItem di;
                di.stage = rs;
                di.model = &md;
                di.placement = &roomPlacements_.back();
                di.variant = md.preferredVariant;
                if (di.variant < 0 && !md.validVariants.empty()) di.variant = md.validVariants.front();
                const std::vector<int>& ok = md.validVariants;
                if (set >= 0 && (ok.empty() || std::find(ok.begin(), ok.end(), set) != ok.end())) di.variant = set;
                if (di.variant < 0 && rs->source == Source::Arcade && !rs->textureSets.empty()) di.variant = 0;
                items.insert(items.begin(), di);
            }
        }
    } else if (bg.chapter >= 0) {
        std::vector<DrawItem> sky;
        skyPlacements_.reserve(2);
        for (uint32_t id : bg.ids) {
            const StageData* from = nullptr;
            for (Source s : {source_, Source::Arcade}) {
                const StageData* st = hasSource(s) ? stage(s, bg.chapter) : nullptr;
                if (!st) continue;
                const auto it = st->models.find(id);
                if (it != st->models.end() && it->second.triangleCount() > 0) {
                    from = st;
                    break;
                }
            }
            if (!from) continue;
            int set = -1;
            for (size_t i = 0; i < from->textureSets.size(); ++i)
                if (from->source == Source::Arcade && from->textureSets[i].romArea == bg.area) set = int(i);
            Placement pl;
            pl.id = pl.modelKey = id;
            pl.pos[0] = cam.pos[0], pl.pos[1] = cam.pos[1] + bg.y, pl.pos[2] = cam.pos[2];
            skyPlacements_.push_back(pl);
            DrawItem di;
            di.stage = from;
            di.model = &from->models.at(id);
            di.placement = &skyPlacements_.back();
            di.variant = from->source == Source::Arcade ? std::max(set, 0) : -1;
            di.background = true;
            sky.push_back(di);
        }
        items.insert(items.begin(), sky.begin(), sky.end());
    }
    renderer_.draw(items, cam, ww, wh, opt_);
    const float none[4] = {0, 0, 0, 0};
    if (staged && fadeIn_ < 1.0f) {   // regallery: a new entry: the stage over it, fading away
        stage_[3] = 1.0f - fadeIn_ * fadeIn_ * (3.0f - 2.0f * fadeIn_);
        backdropDraw(5, ww, wh, boxX, boxY, boxW, boxH, bg.rgb, shadow);
    }
    // the vignette over it all (regallery: and the page's panels' shade)
    if (!thumbMode_) backdropDraw(reStage ? 6 : 1, ww, wh, boxX, boxY, boxW, boxH, bg.rgb, none);
    gl::Disable(gl::SCISSOR_TEST);
}

std::string Gallery::name() const { return galleryEntries()[size_t(index_)].name; }
std::string Gallery::text() const { return galleryEntries()[size_t(index_)].text; }
std::string Gallery::group() const { return galleryCategoryName(category()); }
std::string Gallery::where() const { return galleryEntries()[size_t(index_)].where; }
std::string Gallery::facts() const
{
    const GalleryEntry& e = galleryEntries()[size_t(index_)];
    const GalleryCutLook* c = e.type >= 0 ? galleryCutLook(e.type) : nullptr;
    return c ? c->facts : e.facts;
}
std::string Gallery::sourceLabel() const { return source_ == Source::PC ? "PC" : "Arcade"; }

std::string Gallery::motionLabel() const
{
    const std::string of = std::to_string(motion_ + 1) + " / " + std::to_string(motions_.size());
    if (galleryEntries()[size_t(index_)].type < 0)   // still models: their damage steps
        return motions_.size() > 1 ? "Step " + of + (motion_ == 0 ? "  (whole)" : "  (damaged)") : "";
    if (motions_.empty()) return "-";
    // "3 / 12:  Walk  (z_walk_g)": the place in the list, the kind (a guess from the developers' name; none when it
    // says nothing), the developers' name
    std::string n = anim_.motionName(motions_[size_t(motion_)]);
    if (n.rfind("MO_", 0) == 0) n = n.substr(3);
    const MotionKind k = motionKind(n);
    return of + ":  " + (k == MotionKind::Other ? n : std::string(motionKindName(k)) + "  (" + n + ")");
}

// ---- gallery blood ----
int Gallery::pcBloodWanted() const
{
    if (blood_.rainbow.on)   // (as GameView: the custom path in white, its blood texels marked for the renderer's colours)
        return PcConfig::kBloodCustom | PcConfig::kBloodRainbow | 0xFFFFFF;
    if (blood_.color == 5) return PcConfig::kBloodCustom | (blood_.rgb & 0xFFFFFF);
    return std::clamp(blood_.color, 0, 4);
}

void Gallery::setBlood(const GalleryBlood& b)
{
    blood_ = b;
    blood_.color = std::clamp(blood_.color, 0, 5);
    blood_.rgb &= 0xFFFFFF;
    blood_.rainbow.creature = std::clamp(blood_.rainbow.creature, 0, 2);
    bloodSet_ = true;
    if (!blood_.particles) fx_.clear();
    if (!pc_ || pcBloodWanted() == pcBlood_) return;
    // the PC pages are tinted at the load: the PC source again with the new colour, its stages loaded again on use
    pcBlood_ = pcBloodWanted();
    for (auto it = stages_.begin(); it != stages_.end();) {
        if (it->first.first == int(Source::PC)) {
            if (it->second) renderer_.release(*it->second);
            it = stages_.erase(it);
        } else
            ++it;
    }
    for (auto it = failed_.begin(); it != failed_.end();) it = it->first == int(Source::PC) ? failed_.erase(it) : std::next(it);
    for (auto it = thumbs_.begin(); it != thumbs_.end();) it = it->first.first == int(Source::PC) ? thumbs_.erase(it) : std::next(it);
    pc_ = std::make_unique<PcSource>(PcConfig{paths_.pc, pcBlood_});
    if (!pc_->available()) pc_.reset();
    if (!pc_ && source_ == Source::PC && arcade_) source_ = Source::Arcade;
}

std::string Gallery::bloodLabel() const
{
    return blood_.rainbow.on ? "Rainbow" : galleryBloodColorName(blood_.color);   // (the custom colour: its #RRGGBB beside it)
}

// a look's colour rows ("row:entry ...", traced with red blood) in the blood colour: GGSET's lists give each blood row
// entry per colour (the red, green, blue and purple lists differ only there: entry 81 -> 57 / 44 / 122); bloodRows: the
// table rows that hold blood (the custom colour recolours them, as GameView does)
std::string Gallery::lookRows(const char* rows, std::set<int>& bloodRows) const
{
    bloodRows.clear();
    if (!rows || !*rows) return "";
    if (bloodRowEntries_.empty()) {   // (once: every GGSET list's entries by colour)
        const std::vector<uint8_t>& rom = anim_.maincpu();
        auto& map = const_cast<std::map<int, std::array<int, 4>>&>(bloodRowEntries_);
        for (uint32_t n = 0; n < 64; ++n) {
            const uint32_t lists = romU32(rom, kGgsetLists + 4 * n);
            if (lists == 0 || lists + 16 > rom.size()) break;
            for (uint32_t r = 0; r < 4; ++r) {
                std::array<int, 4> e{};
                bool ok = true;
                for (uint32_t c = 0; c < 4; ++c) {
                    const uint32_t l = romU32(rom, lists + 4 * c);
                    ok = ok && l + 8 <= rom.size();
                    e[c] = ok ? int16_t(romU16(rom, l + 2 * r)) : -1;
                }
                if (ok && e[0] >= 0 && (e[1] != e[0] || e[2] != e[0] || e[3] != e[0])) map[e[0]] = e;
            }
        }
        if (map.empty()) map[-1] = {};   // (no ROM lists: looked once)
    }
    // red for the rainbow (the renderer recolours the red) and the custom colours (recoloured from red, as the game's
    // logic keeps red for them); the arcade's yellow: the custom recolour
    const int c = blood_.rainbow.on || blood_.color >= 4 ? 0 : blood_.color;
    std::ostringstream out;
    for (const auto& [row, entry] : parseColorRows(rows)) {
        int e = entry;
        if (auto it = bloodRowEntries_.find(entry); it != bloodRowEntries_.end() && entry >= 0) {
            e = it->second[size_t(c)];
            bloodRows.insert(row);
        }
        out << row << ':' << e << ' ';
    }
    return out.str();
}

// ---- gallery dismemberment ----
void Gallery::damageInit()
{
    dmg_ = DamageData{};
    std::fill(std::begin(dmgStep_), std::end(dmgStep_), 0);
    fx_.clear();
    const GalleryEntry& e = entryNow();
    if (e.type < 0 || e.type >= anim_.typeCount() || e.type >= 94) return;
    const std::vector<uint8_t>& rom = anim_.maincpu();
    const Skeleton& sk = anim_.skeleton(e.type);
    const uint32_t t = uint32_t(e.type);
    dmg_.routine = romU32(rom, kPartDamageCallbacks + 4 * t);
    dmg_.blowOff = kBlowOffStep + t < rom.size() ? rom[kBlowOffStep + t] : 0;
    for (int c = 0; c < 16; ++c)
        for (int s = 0; s < 8; ++s) {
            const uint32_t m = romU32(rom, kPartReplacement + t * 0x200 + uint32_t(c) * 0x20 + uint32_t(s) * 4);
            if (m == 0) break;
            dmg_.chain[c][s] = m;
            dmg_.len[c] = s + 1;
        }
    // the parts this skeleton has, their steps; what the damage routines can cut off (joints 2 the head, 3 / 6 the
    // right / left arm: part_damage_head_off and the rest)
    unsigned cutJoints = 0;
    bool firstChild = false;
    for (const CutRule& r : cutRules())
        if (r.routine == dmg_.routine) cutJoints |= r.joints, firstChild = firstChild || r.first;
    for (const SkeletonNode& n : sk.nodes) {
        if (!n.model) continue;
        const int cls = anim_.partClass(e.type, n.joint), p = partOfClass(cls);
        if (p < 0 || cls < 0 || cls >= 16) continue;
        dmg_.steps[p] = std::max(dmg_.steps[p], dmg_.len[cls]);
        dmg_.any = dmg_.any || dmg_.len[cls] > 0;
    }
    if (!dmg_.any) return;
    // the head: knocked off at its blow-off step (part_hit -> part_blow_off), else cut by a routine
    if (dmg_.blowOff > 0 && dmg_.steps[kPartHead] > 0 && dmg_.blowOff <= dmg_.steps[kPartHead] + 1) {
        dmg_.steps[kPartHead] = dmg_.blowOff - 1;
        dmg_.canGo[kPartHead] = true;
    }
    if (cutJoints & kJ2 || (firstChild && dmg_.steps[kPartHead] > 0)) dmg_.canGo[kPartHead] = true;
    dmg_.canGo[kPartRightArm] = (cutJoints & kJ3) && dmg_.steps[kPartRightArm] >= 0;
    dmg_.canGo[kPartLeftArm] = (cutJoints & kJ6) && dmg_.steps[kPartLeftArm] >= 0;
    // (an arm without models of its own still goes: it needs a node)
    for (int p : {kPartRightArm, kPartLeftArm}) {
        bool has = false;
        for (const SkeletonNode& n : sk.nodes) has = has || (n.model && partOfClass(anim_.partClass(e.type, n.joint)) == p);
        dmg_.canGo[p] = dmg_.canGo[p] && has;
    }
    if (std::getenv("HOTD_GALLERY_DAMAGE_LOG")) {   // testing: the type's tables
        std::fprintf(stderr, "gallery damage: type %d routine %X blow-off %d steps", e.type, dmg_.routine, dmg_.blowOff);
        for (int p = 0; p < kGalleryParts; ++p) std::fprintf(stderr, " %s %d%s", galleryPartName(p), dmg_.steps[p], dmg_.canGo[p] ? "+off" : "");
        std::fprintf(stderr, "\n");
    }
}

int Gallery::damageSteps(int part) const { return part >= 0 && part < kGalleryParts ? dmg_.steps[part] : 0; }
bool Gallery::partCanGo(int part) const { return part >= 0 && part < kGalleryParts && dmg_.canGo[part]; }

void Gallery::stepDamage(int part, int dir)
{
    if (part < 0 || part >= kGalleryParts || !dmg_.any) return;
    const int n = damageStates(part) + 1;
    if (n <= 1) return;
    dmgStep_[part] = ((dmgStep_[part] + dir) % n + n) % n;
    // Blood Particles: a hit's blood burst on the part (part_hit -> blood_burst_spawn), not when it comes back whole
    if (blood_.particles && dmgStep_[part] != 0) {
        const GalleryEntry& e = entryNow();
        const Skeleton& sk = anim_.skeleton(e.type);
        for (size_t i = 0; i < sk.nodes.size(); ++i) {
            const SkeletonNode& nd = sk.nodes[i];
            if (!nd.model || partOfClass(anim_.partClass(e.type, nd.joint)) != part) continue;
            const bool top = nd.parent < 0 || partOfClass(anim_.partClass(e.type, sk.nodes[size_t(nd.parent)].joint)) != part;
            if (top) fx_.push_back({0, tick_, {0, 0, 0}, int(i)});
        }
    }
}

void Gallery::resetDamage()
{
    std::fill(std::begin(dmgStep_), std::end(dmgStep_), 0);
    fx_.clear();
}

std::string Gallery::damageLabel(int part) const
{
    if (!dmg_.any || damageStates(part) == 0) return "-";
    const int s = damage(part);
    if (s == 0) return "Whole";
    if (s > damageSteps(part)) return "Off";
    return "Damage " + std::to_string(s);
}

std::string Gallery::damageNote() const
{
    const GalleryEntry& e = entryNow();
    if (e.type < 0) return "A still model: M steps its damage";
    if (!dmg_.any) return "No damage models for this one";
    return "";
}

void Gallery::damageModels(const Skeleton& sk, std::vector<uint32_t>& model, std::vector<bool>& gone) const
{
    const size_t n = sk.nodes.size();
    model.resize(n);
    gone.assign(n, false);
    for (size_t i = 0; i < n; ++i) model[i] = sk.nodes[i].model;
    if (!dmg_.any || noDamage_ || thumbMode_ || portraitOn_) return;
    const int type = entryNow().type;
    std::vector<uint32_t> stepModel(n, 0);   // the step's own model (0x7B0 too: the routines test it)
    std::vector<int> cls(n, -1);
    for (size_t i = 0; i < n; ++i) {
        if (!sk.nodes[i].model) continue;
        cls[i] = anim_.partClass(type, sk.nodes[i].joint);
        const int p = partOfClass(cls[i]);
        if (p < 0 || cls[i] >= 16) continue;
        const int s = std::min(dmgStep_[p], dmg_.len[cls[i]]);
        for (int k = 0; k < s; ++k) {   // part_hit's steps in order (0x7B0: the part keeps its model)
            const uint32_t m = dmg_.chain[cls[i]][k];
            stepModel[i] = m;
            if (m != kKeepModel) model[i] = m;
        }
    }
    // the routines' cuts: the children at their joints (or the first child) of a part at one of their models
    auto children = [&](size_t i, auto&& f) {
        for (size_t c = i + 1; c < n; ++c)
            if (sk.nodes[c].parent == int(i)) f(c);
    };
    auto applyRules = [&](size_t i, uint32_t m) {
        for (const CutRule& r : cutRules()) {
            if (r.routine != dmg_.routine || std::find(r.models.begin(), r.models.end(), m) == r.models.end()) continue;
            bool firstDone = false;
            unsigned onceDone = 0;
            children(i, [&](size_t c) {
                const int j = sk.nodes[c].joint;
                if (r.first) {
                    if (!firstDone) gone[c] = true;
                    firstDone = true;
                } else if (j >= 0 && j < 32 && (r.joints >> j & 1) && !(r.once && (onceDone >> j & 1))) {
                    gone[c] = true;
                    onceDone |= 1u << j;
                }
            });
        }
    };
    for (size_t i = 0; i < n; ++i) {
        if (!stepModel[i]) continue;
        applyRules(i, stepModel[i]);
        // type 0's head placeholder: its parent takes the type's damage model (and that model's cut)
        if (dmg_.routine == kDkiller && stepModel[i] == kNoModel && sk.nodes[i].parent >= 0)
            if (const uint32_t pm = dkillerParentModel(type)) {
                model[size_t(sk.nodes[i].parent)] = pm;
                applyRules(size_t(sk.nodes[i].parent), pm);
            }
    }
    // a part "Off": the part from its top (its parent another part's): the head knocked off, an arm cut at the shoulder
    for (int p = 0; p < kGalleryParts; ++p) {
        if (!dmg_.canGo[p] || dmgStep_[p] <= dmg_.steps[p]) continue;
        for (size_t i = 0; i < n; ++i) {
            if (partOfClass(cls[i]) != p) continue;
            const int up = sk.nodes[i].parent;
            if (up < 0 || partOfClass(cls[size_t(up)]) != p) gone[i] = true;
        }
    }
    for (size_t i = 0; i < n; ++i)   // (what hangs from a cut part goes with it; the nodes come after their parents)
        if (sk.nodes[i].parent >= 0 && gone[size_t(sk.nodes[i].parent)]) gone[i] = true;
}

} // namespace hotd::game
