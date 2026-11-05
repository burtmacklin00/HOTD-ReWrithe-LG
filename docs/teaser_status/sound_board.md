# sound_board: the arcade Model 2 sound board as an option (branch claude/soundboard, 2026-10-17)

User decision: emulate HOTD's Model 2 sound board (68000 + SCSP YMF292)
from the player's arcade ROM set, fed by the game's own sound commands, as a setting
"Sound: auto (follows the models) / arcade / PC" (default auto; LaunchConfig, launcher line, PC menu More Options row,
`--sound auto|arcade|pc`). Permissive cores only (BSD-3-Clause / BSD / MIT / zlib), each license in NOTICE.md.

## Status

| Step | State |
|---|---|
| 1. Cores on disk, licenses, plan, protocol | done (this file): **no SCSP or 68000 core source is on disk** -> blocked (no downloads) |
| 2. The board's command path from the game | done on paper (below); the code path already exists (`Runtime::sound_cue` / `sound_send3`) |
| 3. Cores built in | **blocked**: needs the core sources (see "What is needed") |
| 4. The setting | not started (pointless without step 3; small: see "Plan") |
| 5. WAV comparison against MAME | not started (needs step 3) |

## Step 1: what is on disk (searched 2026-10-17)

- `private\mame\` holds only the Model 2 driver (model2.cpp / .h, model2_m.cpp, model2_v.cpp, model2rd.ipp; mamedev/mame
  b67e5bc, src/mame/sega; header `license:BSD-3-Clause`). Reference only: the board's wiring below is read from it.
- `private\mame0289\` is MAME 0.289's **binary** release (no src\).
- Whole-disk search (C:\, `dir /s /b` for scsp.*, scspdsp.*, m68000.*, m68kcpu.*; and fame / cyclone / musashi): **none**.
- The sound ROMs are in the player's set: `private\hotd.zip` holds epr-19720.31 (68000 program, 512 KB) and
  mpr-19721.32 / mpr-19722.34 (samples, 4 MB each). The portable package's `data\arcade\hotd.zip` is the same set.

Per the rules (no downloads; "if a needed core isn't on disk, stop and report"), the work stops after the plan.

## License notes (to check on each file's header once the sources are on disk)

- MAME `src/devices/sound/scsp.cpp`, `scsp.h`, `scspdsp.cpp`, `scspdsp.h`: as far as known these carry
  `license:BSD-3-Clause` (copyright-holders ElSemi, R. Belmont). **Not verified here** (files not on disk). They depend
  on MAME's device / sound-stream framework, so they need a thin shim (the stream callback, the address space for the
  512 KB sound RAM, the IRQ callback, `midi_in`), not a drop-in build.
- MAME `src/devices/cpu/m68000/*` (Olivier Galibert's generated core, `m68000.cpp`, `m68000mcu`, the generated
  `m68000s.cpp` from `m68000_gen.py`): believed BSD-3-Clause; **not verified**. Heavily tied to MAME's
  `cpu_device` / memory system; the shim is larger.
- Alternative 68000 cores with permissive licenses: Musashi (Karl Stenerud; MIT-style license in m68k.h / readme)
  is standalone C and the easiest to embed. Not on disk either.
- Do not use: anything GPL (e.g. FAME/C is GPL-ish in some forks; Cyclone is ARM only; older MAME files with
  `license:GPL-2.0+` or `LGPL`). Check every file's header, including the generated / included ones.
- NOTICE.md gets one entry per core (name, version / commit, license text) when one is added.

## Step 2: the command path (main CPU -> board)

Hardware (MAME model2.cpp, `model2_scsp` config, used by model2c / hotd):

- Main CPU (i960) port **0x01C80000-0x01C80003**: an i8251 / uPD71051 UART (`model2c_crx_mem`, umask 0x00FF:
  data at +0, status / mode / command at +2). Its TxD goes into the SCSP's **MIDI in** (`scsp_device::midi_in`); the
  SCSP's MIDI out comes back on the UART's RxD. Serial clock 500 kHz = 16 x 31.25 kHz (MIDI rate).
- UART TxRDY / RxRDY -> main-CPU interrupt line bit 10 (`sound_ready_w`, gated by the interrupt enable).
- Sound CPU: 68000 at 45.1584 MHz / 4 = 11.2896 MHz. Map: 0x000000-0x07FFFF sound RAM 512 KB (shared with the SCSP;
  at reset its first 16 bytes = the program ROM's, i.e. the reset vectors), 0x100000-0x100FFF SCSP registers,
  0x400000 bank control (unused by hotd: samples are 8 MB), 0x600000-0x67FFFF program ROM epr-19720 (word-swapped
  load), 0x800000-0x9FFFFF samples, 0xA00000-0xFFFFFF banked samples (hotd: bank4 = samples + 0x800000.., i.e.
  beyond the 8 MB set; never read in practice).
- SCSP at 45.1584 MHz / 2 = 22.5792 MHz (44.1 kHz output, stereo); IRQs to the 68000 via `scsp_irq`.

Game side (arcade program; our port):

- `sound_cue` 0x154D0: puts a 24-bit command into a 16-entry queue (RAM 0x512340..); outside modes 4 / 5 only when
  the attract-sound setting 0x51F9C1 is on. The queue is drained to the UART; `sound_send3` 0x15590 writes one
  command straight to the board; `sound_queue_reset` 0x15630 empties the queue.
- Each command goes out as **3 bytes, high byte first**: 0xA0 xx yy (sound codes: 0xA00002 = all off / reset,
  0xA010nn music, 0xA012xx-0xA014xx effects and voices; the full list = the ROM's sound-test table 0xA8CDC, 261
  words) or 0xBE xx yy (0xBE5000: sent with 0xA00002 at game over / chapter starts; a volume / mode command, meaning
  not decoded). Seen by `tools/mame_sound_probe.lua`, which taps the port the same way.
- In ReWrithe these calls already arrive at `Runtime::sound_cue` / `Runtime::sound_send3` (src/game/runtime.h ->
  `game::Audio::cue`). For the arcade board the plan is: `Audio::cue(code)` -> push the 3 bytes into the emulated
  UART -> SCSP `midi_in` at the 31.25 kHz byte rate (about 320 us per byte; at least one byte per sound-CPU timeslice
  is safe), no change in the game logic.

## Plan (for when the cores are available)

1. Put the sources in `private\deps\<core>` (user action; nothing from private\ committed) and configure with
   `-DHOTD_SCSP_DIR=...` / `-DHOTD_M68K_DIR=...` (CMake option `HOTD_ARCADE_SOUND`, off when the folders are missing,
   like HOTD_STB_DIR).
2. `src/audio/sound_board.{h,cpp}` (new): the 68000 (Musashi or MAME core via a shim) with the map above, the SCSP
   with its 512 KB RAM, a byte FIFO into `midi_in`, run per game frame: 11.2896 MHz / 60 = 188,160 68000 cycles and
   735 SCSP samples a frame, the SCSP's output into the existing mixer (the device or `--audio-wav`).
3. ROMs from the player's set (the zip reader ArcadeSource already uses): epr-19720.31 word-swapped into
   0x600000, mpr-19721 / mpr-19722 into 0x800000; first 16 bytes copied to sound RAM at reset.
4. The setting: `LaunchConfig::sound` (auto / arcade / pc), the launcher line, the PC menu's More Options row
   (frontend.cpp's extras page, like Players / Player 2 Input), `--sound auto|arcade|pc` in src/main.cpp; auto =
   arcade when the arcade models are in use and the board can run, else PC. PC = today's path unchanged.
5. Check: record `--audio-wav` with `--sound arcade` and MAME's own WAV (`-wavwrite`, `-sound none` is not usable
   for that; use `-video none -nothrottle` with -wavwrite, never aloud) over the same attract / chapter start; compare
   the command logs (mame_sound_probe.lua vs HOTD_TRACE_SOUND=1) frame by frame, then the onsets in the WAVs.

## What is needed from the user

The core sources on disk (no downloads by workers): e.g. MAME's `src/devices/sound/scsp*` and either
`src/devices/cpu/m68000/` (plus `emu` headers they need) or Musashi's `m68k*.c/h`, placed under `private\deps\`.

## Cores on disk (2026-10-17, the user's OK to download)

- `private\deps\mame_scsp\` (not committed): MAME tag `mame0289`, `src/devices/sound/scsp.cpp`, `scsp.h`, `scspdsp.cpp`,
  `scspdsp.h`; each file's header: `license:BSD-3-Clause` (ElSemi, R. Belmont). They use MAME's device framework
  (device_t, sound streams, address spaces): building them in means a thin stand-in for that framework or a port.
- `private\deps\musashi\` (not committed): Karl Stenerud's Musashi, commit `313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd`;
  m68k.h, m68k_in.c, m68kconf.h, m68kcpu.c, m68kcpu.h, m68kdasm.c, m68kmake.c: MIT-style notice (checked per file).
  **Excluded** (moved to `excluded\`, never built): `m68kfpu.c` (68020+ FPU, no license notice) and `m68kmmu.h` (68030
  PMMU, "Copyright Nicola Salmoria and the MAME Team ... licensing and usage restrictions": not BSD / MIT); `softfloat\`
  not downloaded. The 68000 needs none of them: `src/third_party/musashi_shims/` holds our stand-ins (F-line opcodes ->
  the line-1111 exception, no address translation, the two softfloat headers' type names only), on the include path
  after the Musashi folder (m68kcpu.c includes m68kfpu.c / m68kmmu.h by name).
- Checked: m68kmake generates m68kops.c / .h, the core builds with MSVC (Debug and Release), and a 68000 test program
  (moveq / addq / bra) runs as expected.
- Next: CMake option `HOTD_ARCADE_SOUND` (Musashi + the m68kmake step + the SCSP), NOTICE.md entries for both, the
  board (68000 memory map above, the SCSP's MIDI input fed with the game's 3-byte commands), the setting, the WAV
  comparison.

## Progress (branch claude/soundboard2, 2026-10-17 night)

- Built in (CMake `HOTD_ARCADE_SOUND`, on when `HOTD_MUSASHI_DIR` / `HOTD_SCSP_DIR` are found, else off: the default
  build is unchanged): Musashi + the m68kmake step (`hotd_m68kmake` is built with /Od: its MSVC /O2 build drops bit 14
  of every opcode mask and m68k_init then runs off the handler table), MAME's SCSP unmodified, our framework stand-in
  `src/audio/mame_shim` (emu.h / dirom.h / diserial.h), the board `src/audio/sound_board.*` (map above, 256 68000
  cycles per 44.1 kHz sample, one MIDI byte per 14 samples, the SCSP's IRQ lines -> Musashi's IPL).
- Game side: `Audio::startBoard` (the codes go to the board instead of the PC cues, its output is mixed in, held by the
  pause); the files check keeps `audiocpu.bin` / `samples.bin` in the regions cache (stamp v2: one rebuild); a
  developer's extracted regions (big-endian) work too (byte order told apart by the reset PC); else the zip is read.
- Setting: `LaunchConfig::sound` (launcher.ini `sound=`, 0 auto / 1 arcade / 2 PC), launcher line "Sound", PC menu
  More Options row "Sound", `--sound auto|arcade|pc`. **Auto = the board, in every mode (PC Mode too; user 2026-10-20: the arcade board is the
  default, the PC sound an option).** Without the arcade sound ROMs: the PC sound. The board's queue in work RAM is out
  of the net hash: a board PC and a PC-sound PC play online together (netplay_test 15810, hashes equal).
- Checked: headless `--frames 1500 --sound arcade --mute --audio-wav` gives non-silent output from 4 s on (per-second
  RMS 24-666 of 32767); the board takes the commands (`HOTD_TRACE_BOARD=1`: MIDI bytes in / read, IRQs).
- **Not done** (stopped at 98 % weekly usage): level check (the output looks quiet: MVOL gain x our sfx volume), the
  MAME WAV comparison, the Chapter 1 runs (arcade / `--models pc`, 16695) and the input test on this branch, the
  release-notes line. `HOTD_TRACE_BOARD_STEPS=N` (the first N new 68000 PCs) is a debugging aid left in.

## Arcade = MAME checks (2026-10-19, worker claude/audiomute)

**Route:** the attract loop with no input. MAME was run headless and silent: `-video none -sound none -nothrottle
-wavwrite`, `-str 270`, a copy of the US NVRAM base, and `tools/mame_sound_attract.lua`. That script logs every 3-byte
command written to the i8251 at 0x1C80000, with the emulated time and the game's frame counter 0x51F428. The 240 s run
took 67 s. With `-sound none`, `-wavwrite` does record in 0.289.

**Ours:** `--title --sound arcade --frames 14400 --audio-wav` with HOTD_TRACE_SOUND=1. Compared with
`tools/sound_cmd_compare.py`, aligned at the first 0xBE5000, which is mode 9's start on both sides (MAME frame 363,
our frame 1).

**1. Commands, first attract loop (5690 game frames):**
- 79 of MAME's 84 commands match by code. 64 are on the same game frame and 10 are one frame earlier on our side.
  The music cues are exact: 0xA0146C at +205, 0xA01000 at +791, 0xA01001 (the demo's track) at +4484 / +4485.
- **Missing in ours:** 0xA0134B and 0xA01322 at +1676 / +1677, in the attract's opening scene right after the 5th
  0xA01208. These are probably effects of an attract-scene routine we don't port yet.
- **Different in the demo (mode 3):** MAME has 0xA01225 at +4559 and 0xA01321 / 0xA01319 at +4702 / +4707. Ours has
  0xA01347 / 0xA01318 there instead. The demo's shots hit different targets (the demo's RNG / replay is not identical).
- **Every loop:** MAME's mode 1 (the SEGA logo: 0xA00002 + 0xA01356, 361 frames) is skipped in our attract
  (mode 1 -> 9 at once). So from the second loop on, ours runs 361 frames ahead, and 0xA01356 never sounds.

**2. Timing:**
- MAME's hotd screen is 57.524 Hz. The game's frame counter advances 57.13 times per emulated second (some lag
  frames). Our game ticks are exactly 60 Hz, and the board renders 735 samples per tick (MAME: ~772 per game frame).
- So game events land about 4.8 % earlier in real time than on the arcade, while the music plays at its true speed.
  Example: the demo's music cue comes 74.7 s after mode 9 in ours and 78.5 s in MAME.
- The PC port also runs at 60: this is a project decision, not changed here.
- **Update (rate.md, 2026-10-20):** the logic now ticks at the vblank rate 57.524 Hz by default (Game speed: Arcade);
  the demo music comes 77.95 s after mode 9; the rest of MAME's 78.5 s are its lag frames.

**WAV comparison** (0.1 s RMS envelopes, aligned at each command):

| Segment | Envelope correlation | Ours vs MAME |
|---|---|---|
| Attract music 0xA01000, 8 s | 0.88 (0.94 with ours 0.1 s earlier) | x5.55 louder |
| Demo music 0xA01001, 15 s | 0.79 (0.85 with ours 0.1-0.2 s earlier) | x5.62 louder |

- The level ratio is our deliberate x5.5 board gain. Without it the board's output level is MAME's.
- Onset after the command: the telephone 0xA0146C at 20.0 ms in both; the demo music at 167 ms (ours) vs 173 ms
  (MAME).

**3. Board timing:**
- 256 68000 cycles per 44.1 kHz sample = 11.2896 MHz: MAME's clock.
- One MIDI byte per 14 samples = 317 us. MAME's i8251 at 31.25 kbaud with 10-bit frames gives 320 us (14.11 samples).
  That is about 0.8 % faster per byte, at most ~1 ms on a 3-byte command.

**4. The mixer alters the board's output:**
- Gain x5.5 x sfx / 0.8 (user 2026-10-18: as loud as the PC sound).
- The shared soft limiter above 0.7 then acts on it: 63 of 8.8 M samples on the Chapter 1 autofire run (6000 frames).
- So "no limiter on the board's output in normal play" does not hold strictly, but it is rare. To meet it exactly,
  the board's gain would have to come down to about x5, or the board would need to bypass the limiter.

**Not checked:** a Chapter 1 play route against MAME. Our runs and MAME's (`tools/mame_dump.lua` auto-aim) take
different routes, so only the attract was compared. "Sound: auto" is unchanged.

### Follow-up fixes (2026-10-19, same worker)

- **The board bypasses the soft limiter** (cfdd787).
  - The board's output is now added after the PC sound's limiter, so it is MAME's mix times one linear gain.
  - Why a bypass and not a lower gain: x5.5 is the user's loudness match with the PC sound, and with it the board
    never reaches full scale on the long runs. Peaks: 0.955 on Chapter 1 into Chapter 2, 0.920 on Chapter 2
    (`--strict`).
  - The WAV writer now clamps instead of wrapping.
- **The board went mute in Chapter 2** (1b4b58d).
  - The SCSP raises its DMA-end interrupt with HOLD_LINE, which MAME clears when the CPU acknowledges it. Ours kept
    the line up, so the 68000's interrupts stopped and the output went silent. On Chapter 1 into Chapter 2 it was
    silent from the Chapter 2 music cue on; `--chapter 2` measured RMS 0.010 against the PC sound's 0.119.
  - Now Musashi's interrupt acknowledge callback ends a held line. Chapter 2 measures RMS 0.105.
- **Missing 0xA0134B / 0xA01322** (91c7580): Tom_Adv_00_0's `body_parts_mark_role` (0x1EA30 / 0x1E9B0) is ported,
  marked UNVERIFIED. The zombie Tom shoots in the prologue now reacts as in MAME.
- **The demo's different hits** (the commit after 91c7580): `rand_seed` 0x34A40 (rand_state = seed) was the port's
  empty default, and the demo's evt_SCNINIT seeds 0. With it, the demo's random voices match MAME's.
- **Attract mode 1 (the FBI screen, 0xA01356)** is left off by default. That was the user's decision (fd8fa1d: launcher
  "FBI screen", `--fbi-screen`; the PC release has no such screen).

**Attract compare after the fixes:**

| Run | Matched (MAME's 179) | Same frame | Up to 3 frames early | Missing / extra |
|---|---|---|---|---|
| `--fbi-screen`, both loops (13000 frames) | 179 | 152 | 27 | 0 / 0 |
| Default, first loop | 84 of 84 | 72 | 12 | 0 / 0 |

Without the FBI screen, every later loop runs 361 frames ahead of MAME.

## Arcade FM (GitHub #26, 2026-11-03, worker claude/audio)

The report: Chapter 2 / 3, the boss and the Magician songs' FM parts sound wrong on the arcade sound, as in MAME. Our
board is MAME's SCSP (mame0289) unchanged, so it sounds like MAME by design. Measured headless (`--board-wav`, one
song alone; the reference: the PC CD's tracks, which have the same pitch, ratio 1.000 by long-term spectrum):

- **The FM instrument.** The songs' FM voice is one slot (sample 0x028C0: a 64-sample 16-bit looped wave, LEA 0x3F)
  with MDL 0xD and MDXSL = MDYSL = 0, i.e. it modulates itself with its own output from the stack (feedback FM), TL
  0x44, AR 26 / D1R 5 / D2R 4 / RR 18 / DL 2, KRS 0, octave -3 (a bass around 110 Hz). No song uses MDL 1-4.
- **Ruled out:** MAME's FM stack is s16 and holds 2x the slot output, but it never overflows in any of the 15 songs
  (60 s each). The driver rewrites DL / RR (slot word 5) on sounding slots and MAME then drops the key-rate scaling of
  RR; recomputing it with the scaling (and on word 4) changes no song audibly (envelope correlation 1.0000). Reading
  the feedback one sample later / earlier, or a modulation depth x2 / x0.5, does not bring the songs measurably closer
  to the CD (the CD's own mix differs everywhere by ~7 dB per third-octave band, so it cannot settle the FM details).
- **Tempo:** every song runs 3.2-3.5 % faster than the CD's (time-scale fit of the onset envelopes: Chapter 1 1.0325,
  Chapter 2 1.0345, boss 1.0320, Chapter 3 1.0315, Magician 1.0325; the loops 102.41 s vs 105.93 s for Chapter 2). The
  driver's tempo timer is timer B (TACTL 3, value 0xC6: 57 x 8 samples in MAME). One more count (the hardware
  counting to the overflow, 256 - value) would be 1.75 %; the rest could be the 68000's interrupt latency (wait states
  on sound RAM, not emulated) before it reloads the timer. Not changed: the CD may not be a recording of the board.
- **Not settled without a hardware recording** (the issue's YouTube links were not fetched): the attack shape (MAME
  starts each attack at 0x17F of 0x3FF on a linear scale, the chip counts its attenuation down), the feedback's stack
  delay (MAME: MDXSL 0 = the slot's own output 2 samples before), the modulation scale (MAME assumes 1024 samples per
  cycle; this wave has 64) and the key-rate scaling formula (MAME: octave + 2 KRS + FNS bit 9).
