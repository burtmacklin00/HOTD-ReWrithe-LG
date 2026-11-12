# Subtitles: edit the cut-scene captions

The captions of the cut-scenes and voice lines ("Sophie!?", "Others are still inside. Save them.") come from the
game itself: the arcade's text, or the 1998 PC's font with PC models. A language file (`data/lang/<code>.txt`,
[translating.md](translating.md)) can translate them, at the same time and place as the original.

A **subtitle file** goes further: for any caption you can change the text, when it starts and ends, its size and its
place on the screen, in any language. The idea comes from Tenome's
[Typing of the Dead EX](https://github.com/Tenome/Typing-of-the-Dead-EX) (MIT), whose players edit their own subtitle
files in any language, with timing, size and position (its `mod/README.md`). No code of it is used here.

## The options

Options > Gameplay:

| Row | Values | What it does |
|---|---|---|
| **Subtitles** | Off / Original / **Custom** (default) | Off: no captions. Original: the game's own (or your language file's translation), as before. Custom: your subtitle file's changes; where the file has no line for a caption, or there is no file, Custom is exactly Original. |
| **Subtitle Size** | 75 % ... 200 % (default 100 %) | 100 %: the game's own captions. Another size: the captions drawn in a clear font (data/fonts) at that size, with arcade and PC models alike. |

Both menu styles (Remake and 1998 PC) show the two rows. They are saved in `launcher.ini` (`subtitles=0|1|2`: Off /
Original / Custom, the words also read; `subtitlesize=<percent>`). For one run: `--subtitles off|original|custom`, `--subtitle-size <percent>`.

## Where the file goes

The file is named after the language's code: `en.txt` for English, `es.txt` for Spanish, ... (the code of
Options > System > Language: the name of its `data/lang` file). The game looks in this order and takes the first it finds:

1. an On mod's folder: `data/mods/<mod>/subtitles/<code>.txt` (or the settings folder's `mods/<mod>/subtitles/`): share
   your subtitles as a mod ([modding.md](modding.md));
2. the settings folder (next to `launcher.ini`): `subtitles/<code>.txt`;
3. the game's own: `data/subtitles/<code>.txt`.

The game ships `data/subtitles/en.txt`: every English caption exactly as the game shows it, so Custom looks the same
as Original until you edit a line. Copy it to the settings folder's `subtitles/` and edit the copy (an update then never
overwrites your work). The file is read again within a second when you save it: no restart.

## The format

UTF-8 text, one caption a line, `#` starts a comment:

```
<event>.<line> [start=<time>] [end=<time>] [size=<percent>] [pos=top|bottom|<y>] [hide] = <text>
```

| Part | Meaning |
|---|---|
| `<event>.<line>` | The caption's id (below). `<line>` is 1 or 2: a few voice lines show a second caption after the first. |
| `start=` / `end=` | Move the caption's start / end against the game's own: `+30` (frames, 60 a second) later, `-30` earlier, `250ms`, `0.5s`. A caption never starts before its voice line begins (except line 2, which can start during line 1), and a skipped cut-scene ends it. |
| `size=` | Percent of the normal caption size (`150`, `150%`). Subtitle Size multiplies it. |
| `pos=` | `top`, `bottom`, or the line's centre in percent of the screen's height (`pos=40`: 0 the top, 100 the bottom). Default: the game's place. |
| `hide` | The caption is not shown. |
| `= <text>` | The text. `\n` breaks the line. Leave it out (`1.1 size=150 =`) to keep the game's text (or your language file's). Long text wraps by itself. |

A line that changes nothing (the game's text, no options) shows the game's own caption, pixel for pixel.

Examples:

```
1.1 size=150 pos=top = Sophie!? Where are you?
3.1 start=-30 end=+60 = Wait!
5.1 pos=40 =
26.2 hide
37.1 = Others are still inside.\nSave them!
```

Unknown options and bad ids are skipped; the game prints them (`subs: line 7: unknown option: ...`) when it reads the
file.

## Finding a caption's id

The id is the voice line's number in the game's subtitle table and the line (1 or 2), the same number as
`sub.<event>.<line>` in the language files. `data/subtitles/en.txt` lists every English one with its text. To see the
id of a caption as it plays, start the game with `HOTD_TRACE_SUBS=1`:

```
set HOTD_TRACE_SUBS=1
hotd.exe
```

Each caption prints its id, its frame and its length when it appears and when it goes:

```
subs: frame 2394 show 1.1 (event 90 frames, line 2 at 0 left): Sophie!?
subs: frame 2484 clear 1.1 (shown 90 frames)
```

(`event 90 frames`: the voice line lasts 90 frames, 1.5 s; `line 2 at N left`: its second caption comes N frames
before the end, 0: there is none.) The file in use is printed too.

## Notes

- Draw-only: the captions never change the game (online co-op: each PC uses its own file and options).
- Clean View and Subtitles Off in the cut-scene viewer, and Trailer Mode, still hide every caption.
- How it works: `src/game/subtitles.h` (the file and the options), `Runtime::pcTextIds` (the captions the game shows,
  from `tile_text_draw` 0x9C450), `Hud::subPlan` / `Hud::drawSubtitles` (what is drawn).
