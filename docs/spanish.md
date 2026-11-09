# The Spanish PC version: the Castilian Spanish dub ("doblaje en castellano", Spain)

The House of the Dead's PC port had one official dub: a Spanish CD (a 2003 re-release, "MVM 2003" in its files,
DirectX 8.1 in Spanish on it) whose voices, menus and texts are in Castilian Spanish (Spain's Spanish, castellano; not Latin American). It was never sold outside Spain. ReWrithe
reads it, from the player's own copy only, for **Options > Audio > Voice: Spanish (Spain)**.

Thanks to **3lc3z4r** (YouTube: Cez4rPlay) for the copy, its list of differences and the reference playthrough
["The House Of The Dead (PC) - Doblaje español ORIGINAL"](https://youtu.be/OVnjusRO1wk). No Spanish game file is part
of ReWrithe: the source only has file sizes and SHA-1 hashes (`src/game/pc_manifest_es.inc`).

## Using it

- **Next to a US / EU copy (recommended)**: the setup's **Extras** page (after Game files; also Options > System >
  Repair / Change Files) has the box "Spanish PC copy (Doblaje español)": drop or choose your installed Spanish copy
  (a folder or a zip of it) or the Spanish CD itself (its image: .cue / .bin, a .rar / .7z / .zip of it, or the disc's
  files): the CD keeps the game in Microsoft cabinets (`CAB\CAB1-8.CAB`, LZX, files continued across them), which the
  setup unpacks (Windows: the system's cabinet.dll; Linux / macOS: `cabextract`, else `bsdtar`, which cannot follow
  files continued across cabinets) into the installed layout (`util/cab_unpack.cpp`; v1.3.1: every file of an
  installed copy, byte for byte). It is checked like the PC game and must be the Spanish release (else "This is not
  the Spanish PC release"); a copy that cannot be used is simply not used ("Not a usable Spanish copy: it will not be
  used", Next stays allowed, its unpacked files removed); the box can stay empty. An install copies only what the Spanish features read, into
  `data\pc_spanish`: `SOUND\*.RBH` (151 files: the dub, GAMESNDS.RBH) and `ART\STAGE1\CREATURE\BIGGER.VMC` (152 files,
  57 MB); the uninstaller removes them. A portable copy keeps the folder (`spanishpc=` in `launcher.ini`). By hand:
  the copy in `data\pc_spanish` (or a folder in it), or `spanishpc=<folder>` / `--spanish-pc <folder>`.
- **As the PC game**: choose the installed Spanish copy as the PC game folder. The files check names it ("Spanish
  release (Doblaje español): 153 file(s) of the 2003 Spanish CD ..., accepted") and the game plays as with the US / EU
  copies, its own (Spanish) pictures in every language.
- **The language Español** (Options > System > Language) with a Spanish copy there: the Spanish voices (Voice: Auto)
  and, with the **PC models**, the Spanish CD's translated texture pages (below): the fonts, RELOAD -> CARGA,
  HOLD YOUR FIRE -> NO (a gun picture), PLEASE WAIT, the chapter cards ("Capítulo Primero / Tragedia"), the tutorial
  boards, the boss files and name cards ("Guerrero", PUNTO DÉBIL), CONTINUE / GAME OVER, the score / rescue signs,
  the name entry and ranking, the credits and the map. Every other page (the blood colours, the models, the worlds)
  stays the PC game's: every Blood Color works. Another language: the US / EU pictures as before.
- **The same with the arcade models** (spanish3): every arcade 2D picture whose PC match is one of those translated
  pictures is drawn as the Spanish PC picture, in the arcade picture's cells (the PC's 10 pixels a cell scaled to the
  arcade's 8) and between the arcade layers as the arcade one was (the film scratches stay over the chapter card):
  the chapter cards, the boss files and name cards (Guerrero, PUNTO DÉBIL), RELOAD -> CARGA EL ARMA, HOLD YOUR FIRE ->
  NO, ¿CONTINUAR?, RESCATE / VIDA EXTRA / PUNTOS, the 1J / 2J player marks, the attract's how-to-play boards, the
  Spanish credit titles (PROGRAMADORES ...), the game-over route map (PATIO, CALABOZOS ...), the RANKING / NAME ENTRY
  signs (matched by eye with tools/sprites_tool.py pairs, not seen in a run).
  40 arcade sprites; found when the PC sprites load (a PC picture on a translated page that differs from the US one by
  more than the re-saved colours; `HOTD_TRACE_ES=1` prints them and, each time they change, the ones on the screen).
  Kept English (no Spanish picture for them, or the PC match is another picture): PRESS START BUTTON and FREE PLAY
  (the PC has neither), the "shoot outside of the screen" reload hint (the PC never draws its 0x527), GAME OVER (the
  same on the Spanish CD), two staff roll cards (0x1DE / 0x1E2: their PC matches are the PC port's staff cards) and
  every sign in the 3D world (the arcade models' own textures).
- **Options > Audio > Voice** (`voice=`, `--voice auto|en|es`): **Auto** (0, the default: Spanish with Español, else
  English), **Spanish** (1, in every language), **English** (2). In every mode, Arcade mode with the arcade models and
  the arcade sound board too (the voice cues play the Spanish copy's samples instead of the board's English lines),
  PC Mode and the PC sound. Greyed, with a note, when no Spanish copy is found. With the Spanish copy as the only PC
  game and Voice: English, the arcade board's voices are English and the PC sound's stay Spanish (that copy has no
  English lines).
- **Notes in the menus**: Options > System > Language with Español: "The Spanish voices, signs and cards need your
  Spanish PC copy" (none found) or "With the arcade models PRESS START BUTTON, FREE PLAY, the reload hint, two staff
  roll cards and the signs in the 3D world stay in English; choose PC models for those." (arcade models); Gameplay > Blood Color with the Spanish CD as the only PC game: its red /
  green only; the Extras page's box says what it gives. All in the 16 language files.
- **Options > Gameplay > Boss Names: Codenames Only** (`bossnames=1`, `--boss-names codenames`; default Full; every
  language, arcade and PC models): the boss title cards show only the codename, "TYPE 27" / "TYPE 041" / "TYPE 6803" /
  "TYPE 0", moved to where the name began (with its own underline), not "Chariot" ...: as the Spanish CD, whose cards
  name the bosses only by its adapted names (Guerrero ...). The arcade's name card is one sprite (0xDF, 0xE0, 0xE1,
  0xE2; 0xE3 an unused Hermit / Type 61): its cells from the codename's on are drawn from its first cell; the PC card
  (the same layout, 1.25 times) the same in its 10-pixel cells. The Spanish CD's own cards (Español with the Spanish
  copy) stay as they are. The menus' boss names (Boss Mode) are unchanged. V: Chariot (arcade / PC models, English /
  German), Hangedman and Hermit (arcade, Boss Mode); the Magician's card not seen in a run.
- **Options > Audio > Reload Voice: Off** (`reloadvoice=0`, `--reload-voice off`; every language): no "Reload!" voice
  when an empty gun is fired, as the Spanish release. Arcade code 0xA01404 = PC cue 161 (GAMESNDS.RBH sample 36) is
  left out, the board's and the PC's; the later hint (0xA01405, cue 162 VOC06) and the RELOAD sign stay.

The CD holds a Macromedia Director installer (`INSTALAR.EXE`), the game in Microsoft CABs (`CAB\CAB1-8.CAB`), the three
55 MB VMC files loose (`ART\STAGE1\CREATURE\BIGGER.VMC`, `B_NO.VMC`, `B_SI.VMC`), filler files of 0x55 bytes /
random data under made-up names (`ATAC`, `RESORCE`, `SCENIC`, `TERMIN.LRT`: a copy protection's padding, not game
data) and the music as 11 CD audio tracks (2-12, as the US CD). Installed, the layout is the US one (`SOUND\`,
`SPRITE\`, `ART\STAGEn\...`, `CAM\`, `EVT\`).

## Every combination

Copies: **US** = a US / EU PC copy only; **US+ES** = the US / EU copy as the PC game and the Spanish copy on the Extras
page (or in data\pc_spanish); **ES** = the Spanish copy as the only PC game. "es" = the language Español. Rows with
the same outcome are merged. V = verified headless (render shots / sound traces, 2026-11-06), N = not verified.

| Copies | Language | Voice | Voices heard | Signs / cards / HUD words (PC models) | Same, arcade models | Menus | V |
|---|---|---|---|---|---|---|---|
| US | any | any (greyed: English) | English | English | English | the language's file | V (unchanged) |
| US+ES | es | Auto / Spanish | Spanish (board and PC sound) | Spanish (the Spanish CD's pages) | Spanish 2D pictures (the CD's, in the arcade's cells); the 3D signs, PRESS START / FREE PLAY English + the note | Spanish (es.txt) | V |
| US+ES | es | English | English | Spanish | as above + the note | Spanish | V (voices) |
| US+ES | other | Auto / English | English | English | English | that language | V |
| US+ES | other | Spanish | Spanish | English | English | that language | V (voices) |
| ES | any | Auto+es / Spanish | Spanish (board: its samples; PC sound: its own files) | Spanish (its own files, in every language) | es: Spanish 2D pictures (the list found with a US copy); another language: English | the language's file | V |
| ES | any | English (or Auto, not es) | board: English; PC sound: Spanish (no English lines in it) | Spanish | as above | the language's file | V |

The other settings, for every row:

- **Sound** (arcade board / PC sound): the Spanish voices on both (the board's voice codes play the Spanish samples).
- **Reload Voice** On / Off: On plays the empty gun's "Reload!" (0xA01404 = cue 161), Off leaves it out, the board's
  and the PC sound's, in every row. On, it is the English "Reload!" (the board's line, or the US GAMESNDS.RBH sample 36:
  the Spanish copy's silent sample 36 is not taken, not a dubbed line); only ES with the PC sound has none even with
  On (its own sample is the silent one). Test: `HOTD_NO_RELOAD=1 HOTD_EMPTY_PULL=1 HOTD_TRACE_SOUND=1` (the trace
  says "0xA01404 -> cue 161" and, with Off, "left out (Reload Voice: Off)"). V: 7 empty-gun pulls in 2500 frames,
  On 7 played / Off 7 left out, with the board and with the PC sound (US copy).
- **Blood Color**: US and US+ES: every choice (red, green, blue, purple, yellow, custom, Rainbow Blood) from the US / EU
  files, the Spanish text pages have no blood (V: red, purple with es + PC models). ES: red = B_SI.VMC, green = B_NO.VMC
  (= its BIGGER.VMC), blue / purple / yellow show red (the CD has no such sets; a note on Gameplay > Blood Color),
  custom / Rainbow tint from B_SI vs B_NO (V 2026-11-06: the logs name B_SI / B_NO / B_SI for red / green / blue,
  custom 2060FF and Rainbow Blood tint 1,122,180 texels from B_SI). The arcade models' blood is the arcade's, as before.
- **Modes**: Arcade, Chapter Select, Boss Mode, Survival (Chapter 1's courtyard), PC Mode: as the rows (their models
  setting; PC Mode's own menus are ReWrithe's, in the language's file). The Typing of the Dead 0: voices and signs as
  the rows, its typing words stay English (no Spanish word list; V: Capítulo Primero, 1J, the Spanish voices, the
  words and its PERFECT counter in English). Online co-op: the voices, the pictures and the language are each PC's own
  (its copies and settings); the game itself is the same on every PC (V, tools
etplay_test.ps1 on 127.0.0.1, 2
  players: a host with the Spanish copy on the Extras / Español / Voice Spanish and a joiner with none, 40,000 frames,
  40,000 hashes compared, no desync; a host with the Spanish CD as its only PC game and a US joiner, 20,000 frames, no
  desync: the game data hash is the same, the Spanish files only change pictures and sounds).
- **Language changed in the menu**: the voices at once; the texture pages from the next chapter's load (the menu's PC
  pictures from the next start); the arcade models' Spanish 2D pictures at once (they follow the
  Spanish pages' setting).

## The texture pages (US vs Spanish BIGGER.VMC)

Compared texel by texel (2026-11-06): 386 of the 421 pages differ, most only slightly (the Spanish file was saved
again: colours a level off, the colour key magenta -> transparent black). By what changed:

- **Text pictures (taken from the Spanish copy with es, 109 pages)**: 64 (small HUD words), 94-97 (the 2D fonts with
  accents), 99 (RELOAD -> CARGA EL ARMA), 100 (HOLD YOUR FIRE -> NO + gun, PLEASE WAIT -> POR FAVOR, ESPERA),
  139-158 (the chapter cards: Capítulo Primero / Segundo / Tercero / Final, Tragedia, Venganza, Verdad ...), 212-218 and
  332-336 (the tutorial boards: RECARGA ANTES DE QUEDARTE SIN BALAS ...), 223 / 226 / 230 / 231 / 234 / 236-238 (the
  boss files PUNTO DÉBIL, DESCONOCIDO, the boss name list Guerrero / Gárgola / Tarántula / Hechicero), 245 / 246 / 249 /
  250 (PUNTOS, CONTINUAR, GAME OVER / ¿CONTINUAR, INTRODUCE NOMBRE / PUESTOS), 273-330 (the credits: CRÉDITOS,
  PROGRAMADORES ...; the map: PATIO, CALABOZOS, TORRE, FÁBRICA ...; the boss name cards).
- **Blood colour only (not taken: the PC game's own)**: 7, 83-115, 193-203, 209-211, 264-272, 309-310, 331 and the
  blood texels of the world pages (red -> green: the Spanish BIGGER.VMC is its B_NO set).
- **Other (not taken)**: the world pages 512-595 (their blood and the colour key, no text found), the requantised
  model pages (1-6, 22-63, 169-192, 219-222 ...: the same pictures).
- **Arcade models** (spanish3): the arcade's 2D pictures (its texture ROM's cards and signs, one picture per card)
  whose PC match (src/assets/sprite_map_pc.inc) is a translated picture are drawn as that picture (above: The same
  with the arcade models); its 3D textures stay the arcade's.

## What differs from the US 1998 release

Compared file by file with the US install (C:\Games\HOTD, its unpatched `THOTD.EXE.orig` / `EVTALL.RBH.orig`):

| File(s) | US vs Spanish | What it is |
|---|---|---|
| `ART\STAGE1\CREATURE\CREAT1.RBH`, `CREAT2.RBH`, `ART\STAGE1-6\WORLD\WORLDn.RBH` | same | models, worlds |
| `SPRITE\SPR.RBH`, `CAM\ALLSTAGE\CAMALL.RBH`, `EVT\ALLSTAGE\EVTALL.RBH` | same | 2D sprites (the menus' pictures), cameras, events |
| `*.PAL`, `*.TBL`, `GAMEBIOS.DLL`, `SLATE32.DLL` | same | palettes, libraries |
| `THOTD.EXE` (8,855,552 bytes both) | differs | 128 runs of translated text (menus, PC Mode, the subtitles' lines, Boss Mode's names "Guerrero / Gárgola / Tarántula / Hechicero", "Fase" for Stage, the blood files `B_SI.VMC` / `B_NO.VMC` instead of `B_RED` ... `B_YELLO`, the D3D setup dialog) and **one code byte**: file 0x164349 (VA 0x573F49) `75` -> `EB`, a `test byte [eax+0x21],4; jnz` made a `jmp` |
| `SOUND\GAMESNDS.RBH` (644,836 both) | differs | the sample table is the same; the bank differs in two samples: **36 made silent** (the reload voice, cue 161) and **53 re-recorded** (cue 115) |
| `SOUND\O_*.RBH` (151 files, each 16 bytes longer) | differs | **the dub**: O_VOC02-88 (not VOC04) and O_GOO1_2, HEL1_2, HELP, HELP2, KILLM2, NONONO, NO_2, NO_F3, OHNO, OHNO_2, TELEPH, THAN_2 are new recordings fitted to the English lines' lengths; the other 85 (effects, screams, VOC04, VOC91-94) are the same sounds re-encoded (a few samples off) |
| `ART\STAGE1\CREATURE\BIGGER.VMC` (55,211,636 both) | differs | the textures: 12.3 MB in 403 runs differ (the blood colour, translated texture text: not mapped page by page yet); the Spanish `BIGGER.VMC` = its `B_NO.VMC` ("Blood: NO"), `B_SI.VMC` = "Blood: YES" |
| `texture.VMC` | 55 MB (US) / 2 bytes (ES) | unused |
| `B_RED / B_GREEN / B_BLUE / B_PURPL / B_YELLO.VMC` | US only | the US blood colours; the Spanish has only `B_SI` / `B_NO` ("Blood: YES / NO") |
| `CSTATS.CFG`, `SCORES.CFG`, `THOTD.CFG`, `DEVS.CFG`, `WAIT.BMP` | differs | default settings / scores, the loading picture |

ReWrithe reads `THOTD.EXE` only for its sound cue table (the same in both) and its icon, so the Spanish one works.

## Open questions (not done)

- **The code byte** at VA 0x573F49: which function it is in and what it changes (a renderer / device check, the boss
  names, or the Chapter 3 music loop 3lc3z4r mentions) needs the Ghidra project (docs/pcmode.md's RE notes).
- **Boss Names: Full / Codenames only**: the arcade's boss cards draw "THE CHARIOT" + "TYPE-27" from texture pages;
  on the Spanish CD the names are gone from the PC textures (BIGGER.VMC). An option needs the card's two parts
  located in the arcade texture ROM / the PC VMC pages (the arcade card is one picture) and the name part left out,
  plus the PC Boss Mode menu names (exe strings above).
- **Spanish pictures with the arcade models** (boss cards, chapter cards): the arcade models use the arcade ROM's
  textures; the Spanish ones are only in the PC's BIGGER.VMC (its page layout is the US one: same size).
- The music: the Spanish CD's audio tracks were not compared with the US CD's (the Chapter 3 music cut 3lc3z4r saw
  may be the code byte or the PC's CD-audio loop handling).

## es.txt: the Spanish CD's own terms (2026-11-06)

The 128 translated runs of the Spanish `THOTD.EXE` were paired with the US ones (same offsets: the string table 0x187A30-0x187DF0 = the menu labels in `Frontend::label` order, 0x188558-0x188800 the PC Mode / cheat screen, 0x1B57AE-0x1B587C the character names, 0x1DAE6E + 0x5E per event the cut-scene subtitles = `sub.<event>`) and with the texture pages above. Every es.txt line that translates the same English term or text now uses the CD's word; ReWrithe-only lines keep their wording with the official terms inside (Ajustes, Continuar, Combate, Gárgola, Científica ...). The CD's Castilian wording (tú / vosotros) is kept in the subtitles; its obvious typos are fixed (`dentendré`, `¡Espera` without `!`, `¡Gracias!.`, `¡¡Es terrible!`, a fifth dot). 131 lines changed, 4 added.

| English (key) | ours before | official after |
|---|---|---|
| Options | Opciones | Ajustes |
| High Scores | Récords | Puntuación |
| Are you sure? | ¿Seguro? | ¿Estás seguro? |
| Controller Config | Config. mando | Controles |
| Sound Effects | Efectos | Sonido |
| Auto Reload | Recarga auto | Recarga |
| How hard the game is: Very Easy .. Very Hard (the arcade's operator levels) | Dificultad del juego: Muy fácil .. Muy difícil (los niveles del operador del arcade) | Dificultad del juego: Muy fácil .. Expertos (los niveles del operador del arcade) |
| Continues in PC Mode (0-9, then Free Play: they never run out); arcade games are free play | Continúas en el Modo PC (0-9, luego Juego libre: nunca se acaban); el arcade es juego libre | Continuar en el Modo PC (0-9, luego Inmunidad: nunca se acaba); el arcade es juego libre |
| Back to Options (Escape) | Volver a Opciones (Escape) | Volver a Ajustes (Escape) |
| Start | Empezar | Jugar |
| Start Game | Empezar | Jugar |
| Two players on this PC from Chapter 1 (Player 2's input: Options > Controls) | Dos jugadores en este PC desde el Capítulo 1 (entrada del jugador 2: Opciones > Controles) | Dos jugadores en este PC desde el Capítulo 1 (entrada del jugador 2: Ajustes > Controles) |
| Four players on this PC from Chapter 1 (players 3 / 4: gamepads; Options > Controls) | Cuatro jugadores en este PC desde el Capítulo 1 (jugadores 3 / 4: mandos; Opciones > Controles) | Cuatro jugadores en este PC desde el Capítulo 1 (jugadores 3 / 4: mandos; Ajustes > Controles) |
| Key: Fire | Tecla: Disparar | Tecla: Disparo |
| Key: Reload | Tecla: Recargar | Tecla: Recarga |
| Pad: Fire | Mando: Disparar | Mando: Disparo |
| Pad: Reload | Mando: Recargar | Mando: Recarga |
| Reload | Recargar | Recarga |
| The Hanged Man | The Hanged Man | Gárgola |
| Start Chapter 2 (its boss: The Hanged Man) | Empieza el Capítulo 2 (su jefe: The Hanged Man) | Empieza el Capítulo 2 (su jefe: Gárgola) |
| The Hanged Man (Chapter 2's boss): beat it as fast as you can, the best times go in its table | The Hanged Man (jefe del Capítulo 2): vencelo lo más rápido posible, los mejores tiempos van a su tabla | Gárgola (jefe del Capítulo 2): vencelo lo más rápido posible, los mejores tiempos van a su tabla |
| All Bosses: the four bosses one after the other on one clock | Todos los jefes: los cuatro jefes uno tras otro con un solo reloj | Todos: los cuatro jefes uno tras otro con un solo reloj |
| Researcher (2) | Investigador (2) | Científico (2) |
| Researcher (3) | Investigador (3) | Científico (3) |
| Researcher (4) | Investigador (4) | Científico (4) |
| Researcher (woman) | Investigadora | Científica |
| Researcher (woman, 2) | Investigadora (2) | Científica (2) |
| Researcher (woman, 3) | Investigadora (3) | Científica (3) |
| Researcher (woman, 4) | Investigadora (4) | Científica (4) |
| Sophie, Dr. Curien and the researchers to rescue. Key: 2 | Sophie, el Dr. Curien y los investigadores a rescatar. Tecla: 2 | Sophie, el Dr. Curien y los científicos a rescatar. Tecla: 2 |
| DBR researcher held in the mansion; Rogan came for her. | Investigadora de la DBR retenida en la mansión; Rogan vino por ella. | Científica de la DBR retenida en la mansión; Rogan vino por ella. |
| Shooting a researcher costs a life | Disparar a un investigador cuesta una vida | Disparar a un científico cuesta una vida |
| Stage 2\|20000\|Researcher | Fase 2\|20000\|Investigador | Fase 2\|20000\|Científica |
| Stage 4\|60000\|G | Fase 4\|60000\|G | Fase 4\|60000\|Mister G |
| Stage 3\|40000\|G 2 | Fase 3\|40000\|G 2 | Fase 3\|40000\|Mister G 2 |
| Stage 1\|8000\|G | Fase 1\|8000\|G | Fase 1\|8000\|Mister G |
| Stage 1\|4000\|G 2 | Fase 1\|4000\|G 2 | Fase 1\|4000\|Mister G 2 |
| Your character when the host plays PC Mode (Auto: Rogan / G / Rogan 2 / G 2 by seat) | Tu personaje cuando el anfitrión juega al Modo PC (Auto: Rogan / G / Rogan 2 / G 2 según el puesto) | Tu personaje cuando el anfitrión juega al Modo PC (Auto: Rogan / Mister G / Rogan 2 / Mister G 2 según el puesto) |
| No Player | Sin jugador | Ninguno |
| Hit Size: | Tamaño de impacto: | Zona de impacto: |
| Reload Time: | Tiempo de recarga: | Recarga rápida: |
| Bullet Damage: | Daño de bala: | Potencia arma: |
| Chamber: | Cargador: | Balas: |
| Hit Points: | Vida: | Vidas: |
| You have a high score. | Tienes un récord. | Tienes un nuevo récord. |
| Congratulations! | ¡Felicidades! | ¡Enhorabuena! |
| The sound effects' volume in steps of 5%, after 100%: 0% (the Sound Effects slider on Options) | El volumen de los efectos en pasos de 5%, tras 100%: 0% (el control Efectos en Opciones) | El volumen de los efectos en pasos de 5%, tras 100%: 0% (el control Sonido en Ajustes) |
| The music's volume in steps of 5%, after 100%: 0% (the Music slider on Options) | El volumen de la música en pasos de 5%, tras 100%: 0% (el control Música en Opciones) | El volumen de la música en pasos de 5%, tras 100%: 0% (el control Música en Ajustes) |
| Free Play | Juego libre | Inmunidad |
| On: PC Mode's continues never run out (as the 1998 PC's Cheat screen). Arcade games are always free play | Sí: los continúas del Modo PC nunca se acaban (como la pantalla Cheat del PC de 1998). El arcade es siempre juego libre | Sí: Continuar del Modo PC nunca se acaba (como la pantalla Trucos del PC de 1998). El arcade es siempre juego libre |
| Arcade Mode | Modo arcade | Modo Arcade |
| Chariot Boss | Jefe Guerrero | Guerrero |
| Hangedman Boss | Jefe Gárgola | Gárgola |
| Hermit Boss | Jefe Tarántula | Tarántula |
| Magician Boss | Jefe Hechicero | Hechicero |
| All Bosses | Todos los jefes | Todos |
| PC Mode: the 1998 PC port's mode (characters; Continues). Boss Mode: the bosses only, against the clock | Modo PC: el modo del port de PC de 1998 (personajes; continúas). Modo Jefes: solo los jefes, contra reloj | Modo PC: el modo del port de PC de 1998 (personajes; Continuar). Combate: solo los jefes, contra reloj |
| Continues in PC Mode (0-9); arcade games are free play | Continúas en el Modo PC (0-9); el arcade es juego libre | Continuar en el Modo PC (0-9); el arcade es juego libre |
| GAME OVER | FIN DEL JUEGO | GAME OVER |
| Continues {n} | Continúas {n} | Continuar {n} |
| Press SPACE to skip Demo | Pulsa ESPACIO para saltar la demo | Pulsa ESPACIO para salir |
| The blood's colour on the model, its wounds and its blood. Key: G (Shift+G back). The Gallery's own: Options and Mods keep theirs. | El color de la sangre en el modelo, sus heridas y su sangre. Tecla: G (Shift+G atrás). Propio de la Galería: Opciones y Mods mantienen el suyo. | El color de la sangre en el modelo, sus heridas y su sangre. Tecla: G (Shift+G atrás). Propio de la Galería: Ajustes y Mods mantienen el suyo. |
| The Rainbow Blood mod's colours. Key: N. The Gallery's own: Options and Mods keep theirs. | Los colores del mod Sangre arcoíris. Tecla: N. Propio de la Galería: Opciones y Mods mantienen el suyo. | Los colores del mod Sangre arcoíris. Tecla: N. Propio de la Galería: Ajustes y Mods mantienen el suyo. |
| The blood burst of a new damage step, the drips from a cut part. Key: J. The Gallery's own: Options and Mods keep theirs. | El chorro de sangre de un nuevo paso de daño, el goteo de una parte cortada. Tecla: J. Propio de la Galería: Opciones y Mods mantienen el suyo. | El chorro de sangre de un nuevo paso de daño, el goteo de una parte cortada. Tecla: J. Propio de la Galería: Ajustes y Mods mantienen el suyo. |
| Spanish voices: yes (Options > Audio > Voice: Auto with Español, or Spanish). | Voces en español: sí (Opciones > Audio > Voces: Auto con Español, o Español). | Voces en español: sí (Ajustes > Audio > Voces: Auto con Español, o Español). |
| The Spanish voices need your Spanish PC copy (the setup's Extras page: Options > System > Repair / Change Files) | Las voces en español necesitan tu copia española de PC (la página Extras de la instalación: Opciones > Sistema > Reparar / cambiar archivos) | Las voces en español necesitan tu copia española de PC (la página Extras de la instalación: Ajustes > Sistema > Reparar / cambiar archivos) |
| Very Hard | (English: not in the file) | Expertos |
| Hard | (English: not in the file) | Difícil |
| Easy | (English: not in the file) | Fácil |
| Very Easy | (English: not in the file) | Muy fácil |

Cut-scene subtitles (the key's English in es.txt's comment):

| Key | ours before | official after |
|---|---|---|
| sub.1.1 | ¡¿Sophie?! | ¡Sophie! |
| sub.2.1 | ¡Corre! ¡Corre! | ¡Huye, huye! |
| sub.6.1 | Claro, ¿por qué no? | ¡Claro! ¿Por qué no? |
| sub.7.1 | ¡¡Esto es terrible!! | ¡Es terrible! |
| sub.8.1 | ¡¡Esa voz!! | Esa voz... |
| sub.9.1 | Sophie, ¿estás bien? | Sophie... ¿Te encuentras bien? |
| sub.10.1 | ¡Es un callejón sin salida! | Por aquí no se puede seguir. |
| sub.11.1 | ¡Ah! Está cerrada. | ¡Maldita sea! Está cerrada. |
| sub.12.1 | Esta debe ser la tarjeta de seguridad. | Esta debe ser la tarjeta de acceso. |
| sub.13.1 | Es demasiado tarde. | Demasiado tarde... |
| sub.14.1 | ¡¡Curien!! | ¡Curien! |
| sub.15.1 | ¡¡Tras él!! | ¡Sigámosle! |
| sub.16.1 | ¡Yo lo detendré! | ¡Le detendré! |
| sub.17.1 | ¡Prepárate, Curien! | Será mejor que te prepares, Curien. |
| sub.19.1 | ¡No dejaremos que te salgas con la tuya, Curien! | No permitiremos que te salgas con la tuya, Curien. |
| sub.20.1 | ¡Muy bien Curien, ahora es tu turno! | Es tu turno, Curien. |
| sub.21.1 | Tranquila, pagará por esto. | No te preocupes. Pagarán por ello. |
| sub.22.1 | ¡No tienes salida, Curien! | No tienes escapatoria, Curien. |
| sub.23.1 | Pero nunca, | Pero no podrás escapar |
| sub.24.1 | ¡jamás me vencerán! | a tu destino. |
| sub.25.1 | ¡Saluden a mi obra maestra! | ¡Saluda a mi obra maestra! |
| sub.26.1 | ¿Qué pasó? | ¿Qu..qué ha pasado? |
| sub.26.2 | ¿Por qué no siguen mis instrucciones? | ¿Por qué no me obedeces? |
| sub.29.1 | ¡Nadie sale vivo de aquí! | ¡No saldréis de aquí con vida! |
| sub.30.1 | ¡Hasta aquí llegaron! | ¡Nadie saldrá de aquí con vida! |
| sub.31.1 | ¡Acabaré con ustedes aquí! | ¡Aquí termina tu aventura! |
| sub.32.1 | ¡Acabaré con ustedes aquí! | ¡Aquí termina tu aventura! |
| sub.33.1 | ¿Quién eres? | ¿Quién eres tú... |
| sub.34.1 | Lo destruiré todo. | ¡Os destruiré a todos! |
| sub.37.1 | Aún hay otros dentro. Sálvenlos. | Aún hay gente dentro. ¡Sálvelos! |
| sub.42.1 | ¡¡Gracias!! Tomen esto. | ¡Gracias! Aquí tienes. |
| sub.43.1 | ¡¡Gracias!! Tomen esto. | ¡Gracias! Aquí tienes. |
| sub.44.1 | Desde esa sala pueden subir. | Por allí se puede subir. |
| sub.45.1 | Aún están arriba. | ¡Hay más gente arriba! |
| sub.46.1 | Usen esto. | ¡Usa esto para salir! |
| sub.50.1 | ¡Tenía tanto miedo! | Estaba tan asustada... |
| sub.51.1 | Gracias. | ¡Gracias! |
| sub.52.1 | Deben detener a Curien. | Hay que detener a Curien... |
| sub.52.2 | O pasará algo terrible. | o se apoderará del mundo. |
| sub.54.1 | Debo felicitarlos, | Te felicito... |
| sub.54.2 | No creí que llegaran tan lejos. | Nunca pensé que llegarías tan lejos. |
| sub.55.1 | ¡Pero hasta aquí llegaron! | ¡Ahora veamos... |
| sub.56.1 | ¡Veamos qué tan buenos son de verdad! | ...si puedes conmigo! |
| sub.57.1 | Debo admitir, | Admito... |
| sub.58.1 | que respeto de verdad su constancia. | ...que tu perseverancia es admirable. |
| sub.59.1 | que respeto de verdad su constancia. | ...que tu perseverancia es admirable. |
| sub.63.1 | Ya no tengo nada que perder. | He perdido a Sophie... |
| sub.64.1 | Pero debo seguir. | ...pero debo seguir adelante. |
| sub.66.1 | Misión cumplida. | Todo ha terminado. |
| sub.67.1 | Ahora, a la siguiente misión. | Hemos cumplido con nuestro deber. |
| sub.69.1 | Supongo que todo terminó. | Todo ha terminado... |
| sub.70.1 | Supongo que sí. | Eso espero... |
| sub.72.1 | Aún no han visto.... | Esto no debería... |
| sub.73.1 | ¡¡nada!! | haber ocurrido. |
| sub.77.1 | Nadie me da instrucciones. | Nadie me da órdenes. |
| sub.78.1 | Adiós, Sophie.... | Hasta siempre, Sophie... |
| sub.79.1 | ¡¡Sophie!! | ¡Sophie! |
| sub.82.1 | ¡¡Esto es terrible!! | ¡Es terrible! |
| sub.83.1 | ¡¡Esa voz!! | Esa voz... |
| sub.84.1 | ¡Es un callejón sin salida! | Por aquí no se puede seguir. |
| sub.85.1 | ¡Ah! Está cerrada. | ¡Maldita sea! ¡Está cerrada! |
| sub.86.1 | Esta debe ser la tarjeta de seguridad. | Esta debe ser la tarjeta de acceso. |
| sub.87.1 | Es demasiado tarde. | Demasiado tarde... |
| sub.88.1 | ¡¡Curien!! | ¡Curien! |
| sub.89.1 | ¡No tienes salida, Curien! | ¡No tienes escapatoria, Curien! |

Kept, with the reason:

- **Continue** (pause / continue screen) stays "Continuar": the CD's menu string reads "Continues" there (a slip); its
  CONTINUAR picture agrees with ours. **Exit** ("xxx" on the CD), **Green / Red / Blue ...** (the CD's "No / Sí": its
  blood is only on / off; ReWrithe has the colours), **Low / Medium / High / Short / Long / Small / Large** (the CD's
  "Min / Med / Alto", one slot overwritten: kept as ours, the keys are shared with other settings).
- **Free Play -> Inmunidad**: the CD's word for the PC's Cheat screen option (which is what ours is); a free-play
  arcade in lowercase help text stays "juego libre".
- **G -> Mister G**: the CD's PC Mode character names (both G slots); used in the PC Mode high-score table and the
  online seat list. Rogan stays Rogan.
- Subtitles: sub.49.1 ("Rogan, it's you!" in ours, "It's you!" on the PC) keeps "¡Rogan, eres tú!"; sub.74.1 / 75.1
  ("!!" / "!?") keep ours (the CD's "¡!" / "!?"); sub.77.1: the CD's line is cut ("para darme órdenes!"), so ours with
  its word: "Nadie me da órdenes."; sub.23.1 / 24.1: the CD puts its one line ("Pero no podrás escapar a tu destino.")
  in the first and leaves the second empty: split over both here. sub.4 / 27 / 28 are English on the CD: ours kept.
- New keys: Very Easy / Easy / Hard / Very Hard (the Options page's difficulty values: shown in English before).

- The music: the Spanish CD's audio tracks were not compared with the US CD's (the Chapter 3 music cut 3lc3z4r saw is
  not the code byte, below: the PC's CD-audio loop handling or the disc; ReWrithe plays its own music files, so it
  does not copy it).

## The code byte (VA 0x573F49)

The one changed instruction (spanish3, 2026-11-06; the PC decompile `FUN_00573eb0`, bytes checked in both EXEs) is in
the **Direct3D device enumeration callback** (its arguments: the device GUID, description, name, the hardware and the
software D3DDEVICEDESC, the list). A device with a hardware colour model is copied into the list's record (its
D3DDEVICEDESC at +0x14), then:

    test byte [eax+0x21], 4     ; D3DDEVICEDESC.dwDevCaps (record +0x20) bit 0x400: D3DDEVCAPS_DRAWPRIMTLVERTEX
    jnz  check_texture_sizes    ; US: without it the device is not listed (return 1, the count unchanged)
                                ; Spanish: jmp (always): the cap is not required
    check_texture_sizes:        ; dwMinTextureWidth / Height <= 256 and dwMaxTextureWidth / Height >= 256, then count++

So the 2003 re-release lists a hardware 3D card even when its driver does not report DrawPrimitive with transformed
and lit vertices (newer cards / DirectX 8.1 drivers through the old DirectX interfaces): a compatibility fix for the
1998 renderer's device list. It is not about the boss names, the reload voice or the music. ReWrithe has its own
OpenGL / ANGLE renderer and never enumerates Direct3D devices: **no option needed**, nothing to copy.
