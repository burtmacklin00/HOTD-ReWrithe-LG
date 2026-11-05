# Creature names: developer romaji and official English names (2026-10-14)

The developers' names come from the ROM's creature-type table (`BO_...`, maincpu 0xC8A60, 94 types, the same table in
THOTD.EXE; `tools/m2_names.py`) and the part names of each type's skeleton (main_data, see "Part names" below). The
official English names and type codes come from the fan wiki's creature list (The Wiki of the Dead, "List of creatures in
The House of the Dead", which takes them from the Saturn manual and the official guidebooks; read 2026-10-14). Creatures
the wiki lists only by file name (kage, pdolob, neopetit 1-3, taalu) and the cut creatures (baba, syndy, MrG, disiprin,
lubin, hiru, neozom, debu; [cut_content.md](cut_content.md)) keep those names.

How the match was made: part-name prefixes (e.g. type 28's parts are `neil_*`), the stance / walk motion lists per type
(maincpu 0xC6B60, decompilation.md "Enemy behaviours"), the chapters a type is spawned in (event-script enemy records of
kinds 0 / 96 / 98 / 99 / 100 / 103, where the record ID's low byte is the creature type; EVTALL.RBH lists 0-3 =
Chapters 1-4), and the behaviour code. **Confidence:** "sure" = the developer name is the official name, or code and
data say so; "probable" = chapter + behaviour + parts fit, not checked against footage.

## Creature types (BO_ table)

| Type | Developer name | Part names | Official name (type) | Confidence / notes |
|---:|---|---|---|---|
| 0 | BO_dkiller_kihon | dog_* | **Kenfis** (F-1) | sure: the dogs (kind 97, motions MO_ddog*), Chapter 1 only |
| 1 | BO_mon_kihon | mon_* | **Saruzou** (F-2) | sure: monkey body, motions MO_saru* / MO_monkabe* (kind 70), Chapter 1 |
| 2 | BO_tom | hara*, armer* | **Thomas Rogan** (AMS agent, player 1) | sure: developer routines Tom_Adv_00_0, ZombieWalk2Tom; cut scenes |
| 3 | BO_ebita | ebita_* | **Ebitan A** (B-3) | sure; Chapters 1-3 |
| 4 | BO_ebitb | ebitb_* | **Ebitan B** (B-4) | sure (hangs upside down: MO_ebitb_sakasa*) |
| 5 | BO_mr_g | mrg_* | "MrG" (cut) | sure; see cut_content.md |
| 6 | BO_molb | mol_mobody* (segmented) | **Murrer** (G-1) | probable: segmented worm body, groups, lunge (MO_molj, MO_molkou), kinds 34-39, Chapters 1-2 |
| 7 | BO_frog_ct | frog_* | **Bouere** (F-3) | probable: frog (kind 69, MO_frog_ljp / sjp jumps), Chapter 1 |
| 8 | BO_dkiller_black | dogff_* | Kenfis (F-1), dark variant | sure (same skeleton as type 0) |
| 9 | BO_mon_black | monff_* | Saruzou (F-2), dark variant | sure |
| 10 | BO_syndy | syn_*, bagbag* | "syndy" / Sindy (cut) | sure |
| 11 | BO_zom | zonbichana_* | **Sam** (A-1) | sure: plain zombie stance / walk lists; Chapters 1-4 |
| 12 | BO_debu | zonbisan_* | "debu" (cut Bentley variant) | sure (Bentley's motion list z_ben_*) |
| 13 | BO_gman_kihon | gt_hhara*, gtarmer* | **G** (AMS agent, player 2) | probable: same human skeleton as Thomas, in the cut scenes with him |
| 14 | BO_haride | harihara* | **Rogan, alternative skin** ("Rogan 2") | sure: PC Mode character 3 (THOTD.EXE 0x5CE348 +0x10) and the arcade title-code pairs 0xCA690 (docs/coop4.md); 20-joint human skeleton; one Chapter 2 prop record |
| 15 | BO_staje | stahara* | **G, alternative skin** ("G 2") | sure: PC Mode character 4 and the title-code pairs 0xCA690 (docs/coop4.md); 20-joint human skeleton |
| 16 | BO_zonbiman | zonbimana_* | **Simon** (D-1) | probable: stance MO_z_saim_kamae ("Saimon"), Chapters 1-2; the bridge zombie |
| 17 | BO_kenkyu | kenhara* | DBR researcher (scientist) | sure ("kenkyuu" = research); also types 19-22, 49 |
| 18 | BO_semu | semusimana_* | **Rubin** (E-1) | probable: small hunchback ("semushi"), 16 joints; kind 70 pairs it with BO_dreik (Drake) |
| 19-22 | BO_kenkyu_c / _h / _m / _s | kenhara* | DBR researchers | sure (hostage scenes, kind 100) |
| 23, 25-27 | BO_lady_o / _l / _m / _n | onnhara* | DBR researchers (women, "onna") | sure |
| 24 | BO_mummy | miiraa_* | **Kageo** (B-1) | probable: B-type, Chapter 1 waterway (with type 57), variants 61 / 86 in Chapter 3 |
| 28 | BO_neil | neil_* | **Neil** (A-2) | sure |
| 29 | BO_siriru | siriru_* | **Cyril** (A-3) | sure (axe motions MO_z_ono_*) |
| 30 | BO_samson | samson_* | **Samson** (C-1) | sure (chainsaw MO_z_chn_*) |
| 31 | BO_haris | haris_* | **Harris** (D-3) | sure (ball and chain MO_z_tekk_*); variants 47, 71, 77 (the game draws 47, BO_harisb; type 31 itself was never drawn in the Chapter 1-4 trace runs, 2026-10-26) |
| 32 | BO_gilmoa | gilmoa_* | **Gilmore** (D-2) | sure (hammer MO_z_han_*); variants 58, 63, 74 |
| 33 | BO_bentry | bentry_* | **Bentley** (C-2) | sure (MO_z_ben_*); variants 45, 51, 65, 72, 89 |
| 34 | BO_sophi | sof* | **Sophie Richards** | sure |
| 35 | BO_hyum | hyum_* | **The Chariot** (boss, Type 27), armoured | sure (kind 37) |
| 36 | BO_hyumhada | hyumhada_* | The Chariot, armour off | sure ("hada" = bare skin) |
| 37 | BO_dreik | dreik_* | **Drake** (E-2) | sure (knife throws MO_dr_knif_*) |
| 38 | BO_kage | kage_a* | "kage" | probable: monkey-like body, Chapter 2 (wiki: moves like Saruzou) |
| 39 | BO_lubin | lubin_* | "lubin" (cut) | sure |
| 40 | BO_dkillerb | dogb_* | Kenfis (F-1), variant | sure (dog skeleton) |
| 41-43 | BO_neopetit / 2 / 3 | petit*_* | "neopetit" 1-3 (background creatures) | sure (kind 96) |
| 44 | BO_pkenb | pkenb_a* | not identified | small animal body ("p" + "ken" = dog?), kind 96 like the neopetit |
| 45 | BO_bentryb | bentryb_* | Bentley (C-2), variant | sure |
| 46 | BO_pdolob | pdolob_a* | **Ebitan C** ("pdolob") | sure (GitHub #4, 2026-11-03: the fan list's third Ebitan, the waterway only); drawn at Chapter 1 event 11, area 1 |
| 47 | BO_harisb | harisb_* | Harris (D-3), variant | sure |
| 48 | BO_taraba_b | taraba_body*, legs fr* / fl* | **Taran** (G-3) | probable: spider body with 16 leg parts, Chapter 3 (kind 153); viewer category table called it Hermit until 2026-10-15 |
| 49 | BO_kenkyu_j | kenhara* | DBR researcher | sure |
| 50 | BO_devilon | devilon_* | **The Hangedman** (boss, Type 0) | probable: full-size winged humanoid; the small version is type 55 |
| 51 | BO_bentryc | bentryc_* | Bentley (C-2), variant | sure |
| 52 | BO_disiprin | dis_* | "disiprin" (cut) | sure |
| 53 | BO_tetuman | tetuman_* | **Robert** (D-4) | probable: "tetsu" = iron (metal arm), Chapters 3-4, stance MO_z_tetu_dash |
| 54 | BO_zonbi_b_2 | haris_* (21 joints) | Harris (D-3), variant? | same parts as type 31 with 3 more joints |
| 55 | BO_devilonm | devilon*_min | The Hangedman, Chapter 1 copy (was matched to Devilon, F-4) | sure (galfix, 2026-10-26): the game draws it in Chapter 1 carrying Sophie off and plays the Hangedman's b2_base on it (HOTD_TRACE_TYPE_MOTIONS); "min": a lighter copy of his parts. The Gallery files it under the Hangedman |
| 56 | BO_neozom | sam_mune, zonbichana_* | "neozom" (cut Sam variant) | sure (Sam's motion list; 16 of its 17 parts are Sam's own `zonbichana_*` models, only the chest `sam_mune` is its own) |
| 57 | BO_mummy2 | kageo2_* | **Bourbon** (B-2) | probable: "kageo 2" = Kageo's successor; Chapter 1; variants 62, 73 |
| 58 | BO_gilmoab | gilmoab_* | Gilmore (D-2), variant | sure |
| 59 | BO_moody | moody_* | **Moody** (E-3) | sure; Chapters 3-4 (wall / ceiling motions MO_moodkabe*, moodchu*) |
| 60 | BO_kyurian | kyu* | **Dr. Roy Curien** | sure ("Kyurian"; motions MO_kyuri_*), kind 24 |
| 61 | BO_mummyb | miiraab_* | Kageo (B-1), variant | probable |
| 62 | BO_mummy2b | kageo2b_* | Bourbon (B-2), variant | probable |
| 63 | BO_gilmoac | gilmoac_* | Gilmore (D-2), variant | sure |
| 64 | BO_boss3 | boss3_* | **The Hermit** (boss, Type 0011) | sure (kind 40) |
| 65 | BO_bentryd | bentryd_* | Bentley (C-2), variant | sure |
| 66 | BO_pbaba | pbaba_a* | "baba" (cut) | sure |
| 67 | BO_zsophi | zsof* | creature Sophie (an ending) | sure: event list 6 event 6, the branch of event 3 for route_choice 2 (kind122_ending_score: no continue, below the top rank card; 0 -> event 4 no Sophie, 1 -> event 5 Sophie 34); texture area 2 (kind111), colour row 13:92 |
| 68 | BO_kage2 | kageta_a* | **Parlor** (E-4) | probable (GitHub #4, 2026-11-03): Chapters 3-4, Drake's moves, endless laser daggers = its own beam models `kagetabeem`; variant 87 |
| 69 | BO_devilonx | devilonx_* | The Hangedman, variant | probable (Chapter 4 rematch?) |
| 70 | BO_moodyb | moodyb_* | Moody (E-3) variant, or **Parlor** (E-4)? | open: Parlor is not settled |
| 71 | BO_harisc | harisc_* | Harris (D-3), variant | sure |
| 72 | BO_bentrye | bentrye_* | Bentley (C-2), variant | sure |
| 73 | BO_mummy2c | kageo2c_* | Bourbon (B-2), variant | probable |
| 74 | BO_gilmoad | gilmoad_* | Gilmore (D-2), variant | sure |
| 75 | BO_hiru_b | hiru_body* (segmented) | "hiru" (cut leech) | sure |
| 76 | BO_boss4 | boss4_* | **The Magician** (boss, Type 1) | sure (kind 20) |
| 77 | BO_harisd | harisd_* | Harris (D-3), variant | sure |
| 78 | BO_kyurianb | kyub* | Dr. Curien, second model | sure (kind 25; Chapter 4 / ending scenes) |
| 79-81 | BO_dkiller_jp / _bl_jp / BO_dkillerb_jp | dog / dogff / dogb | Kenfis (F-1), "jp" versions | sure (same skeletons as 0 / 8 / 40; what "jp" selects is open, maybe the region setting) |
| 82 | BO_taalu | taalu_a* | "taalu" | sure; Chapter 3 |
| 83 | BO_taalub | taalub_a* | "taalu", variant | sure |
| 84 | BO_burner | burn_* | **Burner** (C-3) | sure (blowtorch MO_z_burn_koug) |
| 85 | BO_burnerb | burn_* | Burner (C-3), variant | sure |
| 86 | BO_mummyc | miiraac_* | Kageo (B-1), variant | probable |
| 87 | BO_kage2b | kagetab_a* | not identified (variant of 68) | |
| 88 | BO_moodyc | moodyc_* | Moody (E-3) variant, or Parlor (E-4)? | open |
| 89 | BO_bentryf | bentryf_* | Bentley (C-2), variant | sure |
| 90 | BO_tarabab | tarabab_* | Taran (G-3), variant | probable |
| 91 | BO_gfrog | gfrog_* | frog, not identified | same skeleton as Bouere; maybe **Name** (G-2)? open |
| 92 | BO_moodybb | moodyb_* | variant of 70 | |
| 93 | BO_moodycc | moodyc_* | variant of 88 | |

Matched 2026-11-03 (GitHub #4): **Devilon** (F-4) = the bats (kinds 66-68, wing frames 0x54..0x67, no skeleton type); **Name** (G-2) = the slugs of Chapter 1's waterway (kinds 141 / 142, models `nam_CT*` 0x971.., `namkou_CT*` 0x99E.., `nam_ieki_CT` the spit; "namekuji"); Parlor = type 68 above. Before that, not matched: **Parlor** (E-4; candidates: types 70 / 88 / 92 / 93, or the kageta types 68 / 87 with their beam
models) and **Name** (G-2; candidate type 91 gfrog). Checking them needs a look at the chapters where they appear (MAME or
the viewer at the spawn).

## Object kinds (event-script record kinds, kind table 0xAFDE0) with creature names

| Kind | Name in names.tsv | Official name |
|---:|---|---|
| 16 | sophie | Sophie Richards (type 34) |
| 17 | kind17_kenkyu_researcher_collapse | DBR researcher |
| 18 | boss_hangedman | The Hangedman |
| 20 | boss_magician | The Magician |
| 24 | kind24_kyurian_curien (was rescue_human60) | Dr. Curien (type 60) |
| 25 | kind25_kyurianb_curien (was rescue_human78) | Dr. Curien (type 78) |
| 34-36, 39 | kind34_molb_murrer, kind35_molb_murrer, kind36_molb_murrer_attack, kind39_molb_murrer_jump | Murrer (G-1) |
| 37 | boss_chariot | The Chariot |
| 40 | boss_hermit | The Hermit |
| 41 | kind41_fcno_debug (was kind41_fcno) | not a creature: the developers' debug display ("FCNO:%2d" / "HIT", a counter 0-60 stepped by two buttons, draws model 0x635 `sphere`); no event record uses kind 41 |
| 64 | kind64_kenkyu_researchers | DBR researchers |
| 69 | kind69_frog_bouere | Bouere (F-3) |
| 70 | kind70_saru_saruzou (was kind70_saru_monkey) | Saruzou (F-2); also handles Drake / Rubin (BO_dreik, BO_semu) |
| 97 | kind97_ddog_kenfis (was kind97_dog) | Kenfis (F-1); "ddog" is the prefix of its motions (MO_ddogg*) |
| 100, 103 | hostage, hostage_b | DBR researchers (rescue scenes) |

The object-kind table slots without a reading (`obj_kind000`..`obj_kind191`) are now structural names (generated by
tools/name_routines.py, counted half), no longer hand names.

## Part names: the ID -> name table (fixed 2026-10-14)

The part / model names are reached through a **pointer table at main_data 0xEE4EB0** (u32 per object ID, CPU address
0x02xxxxxx of a "PN_..." string; 7477 IDs). The strings themselves (main_data 0xE4B180..) are not one per ID: IDs without a
name have no string, so reading them "one per ID in order" (as the viewer, arcade_props.py and older docs did) drifts by
up to 6+ IDs past 0x800. Examples: 0x9E4 is `niwa01_03b` (not "ono"), 0x1AAF is `room4_02a` (not "tarabab_fldb"),
0xFD9 is `hyum_sakir` (the Chariot's right foot), 0x1C0F `boss4_hrs17`. Names quoted in older notes (formats.md §22-27,
TODO) before this date may be off by a few IDs. **Fixed 2026-10-15** in the viewer (`arcadeObjectNames` in
src/assets/arcade_loader.cpp, used by `ArcadeSource::loadNames` and `CreatureCategories::build`) and `tools/arcade_props.py`.
The viewer's type table now uses the official names above: types 48 / 90 are Taran (an enemy, no longer "Hermit"), type 55
is the small Devilon (an enemy; its `devilonm_*` / `devilon_*_min` parts are no longer bosses). Before / after sheets:
`private\screenshots\names_fix\`.
