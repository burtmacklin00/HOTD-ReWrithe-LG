# Automated checks (worker "checks1", 2026-11-05)

Headless, muted test tooling on top of the existing runs (`docs/soak.md`: the crash / hang matrix, `tools\soak.ps1`).
Every command below runs from the checkout's root with `build\Release\hotd.exe`; `<main>` is the folder with
`private\` (the game data), e.g. `C:\Games\HOTD M2 RECOMPILE\M2 Source`. Every script gives each run its own
`HOTD_PREF_DIR` and kills a run past its timeout by its own PID (never by name: other runs share the PC).

## 1. The input test, robust (`--input-test`, `tools\input_test.ps1`)

What went wrong: the test takes ~6 minutes on an idle PC (~40 game frames a second in its hidden window; the Remake
boot's attract alone is 2 x 6056 frames, all logged as "frame 172") and longer on a busy PC. Callers ran it with a
300 s timeout: the run was killed mid-walk and the results file ended without its summary ("stalled at frame 172",
"ended after frame 1012"). Nothing inside the test hung; there was simply no sign of life and no clean end.

Now:
- the results file gets a `progress <s> s: frame N, stage S, step wait W, attract frames A, game frame F, C checks so far`
  line every 30 s (alive, and where it is);
- past `HOTD_INPUT_TEST_LIMIT` seconds (default 1800, 0 = none) the test writes
  `FAIL frame N: the input test timed out after <s> s (HOTD_INPUT_TEST_LIMIT): stage ...` and the summary, and ends;
- `tools\input_test.ps1` runs it with a wall-clock timeout (default 2400 s) and, when the game could not write the
  summary (a crash, killed from outside), appends `FAIL input-test: <why> (last line: ...)` and a summary line itself.
  The file's last line is therefore always `N checks, M failed`.

```
powershell -ExecutionPolicy Bypass -File tools\input_test.ps1 -Root "<main>" -Results <dir>\input.txt [-Timeout 2400] [-Limit 1800]
```
Exit code 0 = all checks passed. Give a caller timeout of at least 15 minutes (`-Timeout` covers it).

## 2. Text fit (`--text-fit`, `--lang-check`, `tools\text_fit.ps1`)

- Menus: `--lang-check <code>` already draws every menu screen of both menu styles (Classic, Remake) and lists
  `OVERFLOW` (wider than its box or the screen) / `BROKEN` (cut through a letter); `HOTD_LANG_SIZE=WxH` sets the size.
- In-mode HUD (new): `--text-fit` notes every HUD text drawn past its picture's edge (an Overlay panel against the
  window; Survival's score-font lines, `Hud::scoreText`, against the 2D canvas) and prints at the run's end
  `TEXTFIT <px> px (WxH) <text>` per text and `text-fit: N texts past the screen's edge, M pictures (lang xx)`.
  Ready pictures (TotD word boxes, sights) are listed as `TEXTFIT-PICTURE` and not counted.
- The sweep: every language in `data\lang` x 640x480 and 1920x1080 x (menus, Survival, TotD 0):
```
powershell -ExecutionPolicy Bypass -File tools\text_fit.ps1 -Root "<main>" [-Langs en,de,ko] [-Sizes 640x480,1920x1080]
    [-Modes menus,survival,totd] [-Frames 3000] [-Jobs 4] [-Out build\textfit]
```
  Output: `build\textfit\summary.md` (a table language x size, then every problem line), one log per run.
- One run by hand: `hotd --arcade <zip> --pc C:\Games\HOTD --render --mute --size 640x480 --lang de --text-fit --frames 3000 --survival courtyard --autofire --lives-kept 1`

## 3. HUD stays out of the action (`--hud-cover <pct>`)

`--render` runs: every 30th frame in play (game mode 4, shots on; `HOTD_HUD_COVER_EVERY=N`), the picture is read before
and after the HUD / 2D layer (sights left out) and the share of the play area's middle 70 % (each way) that the 2D
layer changed is measured. Frames above `<pct>` print `HUDCOVER frame F: X %`; the end prints
`hud-cover: N frames sampled, mean X %, max Y % at frame F, K above T %` and saves the worst frame with the HUD's pixels
in magenta as `<shot-dir>\hudcover_worst_f<F>.png`.
```
hotd --arcade <zip> --pc C:\Games\HOTD --render --mute --size 1280x720 --frames 12000 --autofire --lives-kept 1 --hud-cover 8 --shot-dir <dir> [--survival courtyard | --totd-test]
```

## 4. Long soak (`tools\soak_long.ps1`)

Survival to round 16 (`--survival-test 16`), Typing of the Dead 0 chapters 1-4 (Normal), the
arcade game Chapter 1 to the ending in one run; arcade and PC models. The game prints `PROGRESS` every
`HOTD_PROGRESS_EVERY` frames (a multiple of 60: mode, event script place, score, Survival round / kills,
memory) and `STUCK` once when none of those changed for `HOTD_STUCK_FRAMES` frames; the script samples the working set
every 5 s (`<run>.mem.csv`). A run fails on a crash (non-zero exit), the timeout, `bad_access`, `STUCK`, memory growth
(last quarter's mean more than `-GrowMb` above the second quarter's) or its end line missing.
```
powershell -ExecutionPolicy Bypass -File tools\soak_long.ps1 -Root "<main>" [-Only <regex>] [-Jobs 4] [-Models arcade,pc]
    [-Timeout 3600] [-Stuck 7200] [-GrowMb 150] [-Render] [-Out build\soak_long]
```
Output: `build\soak_long\summary.md`.

## 5. Typing of the Dead 0: the real-event path (worker totd7, 2026-11-05)

**Every TotD 0 acceptance test runs through the real-event path from now on**: the game in its real window (hidden,
never focused: `--totd-play` is an automated window run), driven only by SDL events pushed into the window's own event
queue, as a keyboard sends them: Shift's own key-down / key-up round a capital, each key's key-down and key-up with its
key code AND scancode and the keyboard id, the text event between (`totdRealKey`, main.cpp). The typist decides from the
boxes the HUD draws (`Typing::labels`); nothing calls the typing layer directly. Headless `--totd-test` stays a quick
self-test, not an acceptance test (its typist's keys never meet the window's loop, the key bindings, the pause or the
hold-to-skip).

Why (docs/typing.md "Round 7"): the totd2-6 typist sent no scancodes, never typed with nothing up, held Space from a
scene's first frame and never lost all lives: the user's keyboard paused the game with P mid-scene, broke the PERFECT
streak in cut-scenes, and met routes / continues the tests never did.

```
hotd --arcade <main>\private\hotd.zip --pc C:\Games\HOTD --no-launcher --windowed --mute --chapter N --difficulty 2
     --totd-play WPM,ACCURACY,SEED [--route 1:3=0,1:4=0] [--event E] [--lives-out F]
```
with `HOTD_PREF_DIR=<own folder>` and, as a person plays:
- `HOTD_TOTD_STRAY=1`: a letter about every 0.4 s whenever nothing is up (cut-scenes, empty screens, between waves);
- `HOTD_TOTD_SKIP=1` holds Space a second into each cut-scene (again every 3.5 s), `=2` holds the letter K instead;
- `HOTD_TOTD_CONTINUE=1`: at the continue countdown the 1 key; `--lives-out F` empties the lives at frame F;
- `HOTD_TOTD_IDLE=a,b`: the typist's hands off from frame a to b;
- `HOTD_TOTD_REC=<dir>` (`HOTD_TOTD_REC_EVERY=n`): the window's pictures for clips;
- `HOTD_TOTD_FLICKER=1`: every box that blinks printed (`TOTD FLICKER ...`).
- (totd8) `HOTD_TOTD_FULL=1`: on past the chapter's end and the game over to the ending and the name entry; `HOTD_TOTD_COVER=1`:
  `TOTD BOX COVER near / mid / far` (the body and head each box hides); `HOTD_TOTD_REC_FROM=F`: the recording from frame F;
  `HOTD_TOTD_BOSS7=1`: round 7's boss rule (strikes not held); `HOTD_TOTD_RAWWAV=1`: the typing WAVs as before (not
  converted for the board); `HOTD_SCSP_KEYS=1`: every arcade sound slot keyed on (format, rate).
- (totd8, the package bug) `HOTD_TOTD_LETTHROW=1`: the typist leaves throwers 6 s (they throw) and crates / locks 1.5 s;
  every report has `TOTD PROMPT CHECK` (each thrown thing on the screen prompted within 20 frames; a miss fails
  `TOTD TEST`). Run acceptance tests from a package copy too (`binhotd.exe` in a copy of the portable folder): the
  routine names (toolsghidra
ames.tsv, which TotD 0 and the CPU teammates classify objects by) were missing there
  until they were compiled in (2026-11-05): no G / H key anywhere in the package.

The report (stdout) adds `TOTD QUICK` (G / H typed, ignored, keys with nothing up, stray keys, groups, pickups, misses,
best streak), `TOTD FLICKER` (boxes gone and back within 60 frames, quick keys changing letter: both fail the run) and
`TOTD HELD STRIKES` (a creature's strike that hit nothing while its word was up). Chapter 1's keypad cell (event 6):
`--route 1:3=0,1:4=0`. Comparisons: `HOTD_TOTD_HOLD6=1` (round 6's attack rule), `HOTD_TOTD_NOLATCH=1` (round 6's
boxes), `HOTD_TOTD_NOSOUND=1` / `HOTD_TOTD_NOSHOT=1` (the typing sounds / the letters' gun sound off, with
`--audio-wav` for loudness), `HOTD_TOTD_BOXES=<dir>` (close-ups of the boxes at 640x480's and 1080p's scale).

## Results

First runs (2026-11-05, this PC, 16 threads, other workers' runs going at the same time):

| Check | Result |
|---|---|
| Input test under load (3 headless `--render` chapter runs in a loop beside it; later the render soak, 5 at once) | 2613 checks, 0 failed, 386 s and 385 s; 12 progress lines each |
| Input test, `-Limit 70` | `FAIL frame 172: the input test timed out after 70 s ... stage 7, step wait 2203` + `567 checks, 1 failed` |
| Input test, `-Timeout 25 -Limit 0` (killed from outside) | the wrapper's `FAIL input-test: killed after 25 s (-Timeout 25) (last line: ...)` + `497 checks, 1 failed` |
| Text fit: 16 languages x 2 sizes x 4 | HUD (Survival / TotD 0): no text past the edge in any run (e.g. Survival in Russian: 38725 texts drawn, 0 out). Menus: see below |
| HUD cover, 8 runs at 1280x720, 8 % | arcade 0.2-0.3 % mean, max 7.1 % (arcade models) / 9.0 % (PC: the screen blood); Survival mean 2.9 %, the shop between rounds 10 %; TotD max 9.4-10.2 % (word boxes stacked in a column) |
| Long soak, headless | ok (the unreleased modes' runs: their own notes) |
| Long soak, `-Render` | TotD Chapters 1 / 2 with PC models: crash at exit (below) |

Defects found:
- **The test bot and the iron ball** (fix5, 2026-11-05): a run started at Chapter 3 can stall at the lab's first
  creature wait (event script 0xF4094) when the bot keeps shooting the iron-ball swinger's unkillable ball
  (`iron_ball_follow` 0x47C70) instead of the swinger: the bot's fault, not the game's. `HOTD_AIM_SKIP=47C70` makes
  the bot leave the ball alone.
- **Crash at exit while the next chapter's world is being prepared** (TotD `--render --models pc`, the frames ran out
  just after a chapter clear): access violation in a worker thread, `ViewPrefetch::start` -> `GameView::prepare` ->
  `PcSource::loadStage` -> `readVmcPages` -> `pcVmcPageShift` (its function-static `known` map / mutex are destroyed by
  the exit while the worker still uses them). Likely the same when the window is closed right after a chapter clear
  with PC models. A fix: wait for (or detach from) `gViewPrefetch`'s job before `main` returns.
  **Fixed (fix5, 2026-11-05)**: `main`'s first local (`ExitJoin`) runs `ViewPrefetch::finish` on every return, before
  the statics go. TotD `--render --models pc` Chapters 1 / 2: exit 3 before, exit 0 in 5 of 5 runs each after.
- **Menus**: the Classic Language page's row "Português (Brasil) (Brazilian Portuguese)" runs 42-97 px past its box in
  ar, he, ja, ko, pl, tr, vi, zh (its native name drawn in the menu font, wider than the column; fix5 2026-11-05:
  `pt.txt`'s `lang.english` is now "Portuguese", the row "Português (Brasil) (Portuguese)"); the PC Mode score
  table's character names overflow in English too (5 names, the PC's font) and further in the translations (de
  "Forscher", es "Investigador", ja "ローガン 2" 48 px, ...): right-aligned, they grow left past their column but in
  the Japanese picture nothing collides, so these read as box-measurement notes, not visible faults. The menus give the same result at 640x480 and 1920x1080
  (the menu is laid out on its 640 x 480 grid).
- The menus' untranslated key counts (`MISSING`): 104-210 keys per language (de 208, id 210, it 205, fr 194).

Notes: headless runs are fast (a TotD chapter in 3 s, Survival round 16 in 8 s), so the plain soak measures memory
from the game's own PROGRESS lines; with `-Render` a chapter change loads a new world and moves the working set by
100-160 MB either way, so a growth figure there means "look at `<run>.mem.csv`", not a leak by itself.
