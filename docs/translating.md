# Translating the game (language files)

Anyone can translate ReWrithe's own texts (the menus, the help lines, the Gallery, the Jukebox, the online pages, the
first-run page, the notes over the game) and the cut-scene subtitles with one plain text file. No programming, no
rebuild.

## Quick start

1. Get the English list: `data\lang\en.txt` (it comes with the game; `hotd --export-lang en` writes it again from your
   build, every key the game has).
2. Copy it to `data\lang\<code>.txt`, for example `ko.txt` (Korean), `ja.txt`, `de.txt`, `pt-br.txt`. Save it as
   **UTF-8**.
3. Change the right side of each line:

   ```
   Options = 옵션
   ```

   Leave out (or put `#` before) a line you have not translated: it shows in English.
4. Set the language's name and, for a script the game's own fonts lack (Korean, Japanese, Chinese, Cyrillic, Greek ...),
   a font:

   ```
   lang.name = 한국어
   lang.font = NotoSansKR-Bold.otf
   ```

5. In the game: Options > System > **Language**. Change the file, press **F5** in the menu: it reloads at once.
6. Check it: `hotd --lang-check ko` draws every menu screen in your language and lists the texts that do not fit
   (`OVERFLOW`) and the keys not translated yet (`MISSING`). Add `--lang-shots <folder>` to save every screen as a
   picture (both menu styles).

## The file

- One entry a line: `English text = your text`. The left side must stay exactly as in en.txt (it is the key).
- `#` at the start of a line: a comment. Empty lines are fine.
- `\n` in a text: a new line. `\\` a backslash.
- **Placeholders** `{name}` keep their braces and name; move them where your language needs them:

  ```
  P{n} PAUSED = {n}P 일시 정지
  Sonic Mode: {mode} = 소닉 모드: {mode}
  ```

  `{n}` (also `{n1}`, `{n2}` ...) stands for a number only; any other name stands for any text. A piece that is
  itself a key (`Off` in `Sonic Mode: Off`) is translated too. You may add your own patterns for texts the game
  builds from parts.
- Some keys appear with an example value (`Chapter 2, scene 12: ...`, sample names): they come from the menu's preview
  pages; translate them only if you like.

## Settings at the top

| Key | Meaning |
|---|---|
| `lang.name` | The language's own name, shown in Options > Language |
| `lang.english` | Its name in English, shown after it on the Language page: `한국어 (Korean)`. Left out: known by the file's code (ko, ja, ar, he, zh, fr, de ...), else only `lang.name` |
| `lang.font` | A TTF / OTF / TTC file in `data\fonts` (or next to the .txt). A comma list: the next fonts are fallbacks for characters the first lacks. Empty: the game's own bitmap fonts (they have the Latin-1 letters: é, ñ, ü, ß ...) |
| `lang.font_scale` | The font's size against the game's letters (1 = the same capital height; CJK looks right near 0.8) |
| `lang.upper` | 1: the Remake menu style writes Latin letters as small capitals (as in English); 0: as written |
| `lang.rtl` | 1: the language reads right to left (Arabic, Hebrew, Persian, Urdu ...); 0: left to right. Left out: right to left when `lang.name` (else most of the texts) is in a right-to-left script |
| `lang.sample` | 1: a sample file (only a few lines) |

Texts left in English (ASCII) keep the game's own bitmap fonts, so a half-done translation looks like the game.

**Fonts.** The game ships Noto Sans KR, Noto Sans JP, Noto Sans SC (Simplified Chinese), Noto Sans Arabic, Noto Sans
Hebrew and Noto Sans (Latin / Greek / Cyrillic) (Bold; SIL Open Font License, `data\fonts\OFL.txt`). For another
script, put a free font in `data\fonts` and name it in `lang.font` (check its licence allows redistribution if you
share your pack). The game reads TrueType and OpenType (CFF) fonts itself (no hinting). A character the language's
font lacks (Latin letters and digits in an Arabic or Hebrew line) is taken from the other fonts in `data\fonts`.

**Size.** `lang.font_scale` 1 sizes the font's capitals to the height of the game's letter cells, which is taller
than the letters drawn in them: a Latin / Cyrillic font matches the game's capitals near 0.65 (`ru.txt`; `tr.txt`, `pl.txt`, `vi.txt` 0.72), CJK near 0.8.

**Capitals.** The Remake style's small capitals (`lang.upper = 1`) only change the Latin letters a-z: a Cyrillic,
Greek or a Latin-Extended language (Turkish, Polish, Vietnamese) uses `lang.upper = 0` (its letters as written; else `Enter/A` turns into `ENTER/A` beside lower-case
words).

## Right-to-left languages (Arabic, Hebrew ...)

Write the texts as you would type them (in reading order); the game puts each line in display order itself:

- **Bidi:** every line with an Arabic / Hebrew letter goes through the Unicode Bidirectional Algorithm (the parts UI
  texts need: right-to-left and left-to-right words, numbers with their signs (`25%`, `1/2`), spaces and punctuation,
  bracket pairs, mirrored brackets). Latin words and digits inside a line (`Enter/A`, `Esc/B`, `P{n}`) stay readable.
  With `lang.rtl = 1` a line reads from the right even when it starts with a Latin word.
- **Arabic shaping:** the letters take their joined forms (isolated / initial / medial / final) and lam + alef become
  one letter (لا), through the Unicode Arabic Presentation Forms, which Noto Sans Arabic has (Arabic, plus the common
  Persian / Urdu letters: پ چ ژ ک گ ی ...). Hebrew needs no shaping. Vowel marks (harakat) are kept but not placed
  (they may sit next to the letter rather than on it): leave them out of menu texts.
- **Layout:** the settings rows are mirrored (the label at the right, the value / switch / mark at the left, `<` for a
  page that opens), the tabs run from the right (Q / E and Left / Right follow them), the Jukebox's lists too (and
  its rows: right-aligned, the numbers at the left, the playing mark `<`, in both menu styles), the Remake style's
  key prompts sit at the bottom left (caps first, then the words), the in-game notes are right-aligned. Centred texts
  (the main menu, titles, help lines) stay centred. The PC's own Options page keeps its places (its boxes are the
  PC's; a translated value too wide for the small 1P / 2P boxes, such as Arabic / Hebrew Off, is drawn smaller to
  fit), and the game screens (HUD, pictures with words) are as they are.
- `data\lang\ar.txt` and `he.txt` hold every key (machine-translated 2026-10-27, see "Shipped languages").

**Not supported yet:** scripts that need more shaping than presentation forms: Thai (marks above / below), the Indic
scripts (Hindi / Devanagari, Bengali, Tamil ...: conjuncts, reordered vowels), Khmer, Myanmar, Sinhala. Their letters
are drawn one after another as they come, which is not correct writing. Arabic ligatures other than lam-alef and
mark positioning are not done either.

## Shipped languages (2026-10-27; Indonesian and the new keys 2026-11-03)

`ko.txt`, `ja.txt`, `ar.txt`, `he.txt` and `id.txt` (Indonesian, the game's own fonts: no `lang.font`) hold every key
of en.txt (menus, help lines, the name entry, the Players
page, Firing Range, Mods, the Gallery and Actors, the online pages, the first-run page, the in-game notes, the end
screen, all 85 subtitles). They are **machine translations (AI)**, marked so in each file's header: a native speaker
should check them. Names on the sample tables and the arcade's own pictures (RELOAD, CONTINUE?, the chapter cards,
the name entry's letters) stay as they are. Some labels are short on purpose (the classic PC menu's boxes are narrow).
`--lang-check` for each: no text too wide that English does not also have (the 5 English sample names on the PC
Mode scores page are wider than their column in every language).

`zh.txt` (Simplified Chinese, Noto Sans SC) and `ru.txt` (Russian, Noto Sans; short wordings and abbreviations where
the boxes are narrow) followed on 2026-11-03: every key, machine-translated (AI) too; `--lang-check` for each: 3 texts
too wide, the PC Mode scores' sample names that English has too. The keys left "not translated" are those that are
the same in every language (fps values, pad buttons, key caps, sample names, codes).

`tr.txt` (Turkish), `pl.txt` (Polish) and `vi.txt` (Vietnamese) (2026-11-03, machine-translated too) use Noto Sans
Bold (`NotoSans-Bold.ttf`: Latin Extended, Vietnamese, Cyrillic) at `lang.font_scale = 0.72` (its capitals the height
of the game's), with `lang.upper = 0`: the Remake style's capitals are ASCII only (Turkish i would become I, not İ;
ş ğ ı ł ą ę ś ż and the Vietnamese letters would stay small), and Noto's small letters are about the height of the
English small capitals. Their texts with a letter past ASCII are drawn in Noto Sans; the ASCII ones keep the game's
bitmap fonts, as for every language. `--lang-check`: only the texts English has too (the sample names on the PC Mode
scores page; the 2-21 cut-scene title in Turkish).

`fr.txt` (French), `it.txt` (Italian) and `de.txt` (German) (2026-11-03) also hold every key, machine-translated (AI)
the same way, in the game's own bitmap fonts (Latin-1: no `lang.font`; French writes "oe" for the joined letter, which
Latin-1 lacks). Labels are shortened to fit (German especially); `--lang-check` for each: no text too wide that English
does not also have.

17 more (langs2, 2026-11-10), every key, machine-translated (AI) the same way: in the game's own fonts (Latin-1, no
`lang.font`) `nl.txt` (Dutch), `sv.txt` (Swedish), `da.txt` (Danish), `nb.txt` (Norwegian Bokmal), `fi.txt` (Finnish),
`pt-pt.txt` (European Portuguese), `ms.txt` (Malay), `fil.txt` (Filipino); in Noto Sans with `lang.upper = 0`
`cs.txt` (Czech), `ro.txt` (Romanian), `hu.txt` (Hungarian), `uk.txt` (Ukrainian), `bg.txt` (Bulgarian), `el.txt`
(Greek); `th.txt` (Thai, `NotoSansThai-Bold.ttf`), `zh-tw.txt` (Traditional Chinese, `NotoSansTC-Bold.otf`) and
`fa.txt` (Persian, right-to-left, `NotoSansArabic-Bold.ttf`). en.txt also holds the 28 texts the `--export-lang` tour
does not reach (the PC menu's Very Easy / Very Hard, key caps, the model viewer's names ...: main.cpp's extra list), so
every language translates them.

In play, the PC font's words (the PC models' **Continues n**, **Press SPACE to skip Demo**, the PC pause's
**PAUSED / Continue / Exit**) are drawn as text in the language when one is chosen (the PC font only has the
English letters); English keeps the PC's pictures.

A text with ` = ` in it (`Enter = START, Esc = QUIT`) is written ` \= ` on the key side: the reader splits a line at
its first ` = ` (before 2026-10-27 such keys could not be translated, and `Enter` got a wrong text).

Testing `--lang-check` from a checkout: `--root` decides which `data\lang` is read; give the worktree's own root (and
`--arcade` / `--pc` for the game files) or the main checkout's files are checked.

## Cut-scene subtitles

The game's subtitle lines are keyed `sub.<event>.<line>`. en.txt lists them all at the end, each with the English line
in a comment above:

```
# Others are still inside. Save them.
sub.37.1 = 아직 안에 사람들이 남아 있어. 구해 줘.
```

A translated subtitle is drawn in your font (white with a dark edge, low in the picture) instead of the game's own
line, in both model sources (arcade / PC). Untranslated lines stay the game's own. (Options > Gameplay > Game Text
Japanese switches the game to the arcade's own Japanese subtitles and voice-message pictures instead.) To also change
a subtitle's timing, size or place, write a subtitle file with the same ids: [subtitles.md](subtitles.md).

## Pictures with words

Words drawn as pictures (RELOAD, HOLD YOUR FIRE, CONTINUE?, the chapter cards, the ranking) are not text. A language
may bring texture packs of its own: put them in `data\mods\lang\<code>\<pack>\` (the same layout as any pack,
docs/mods.md). They are on, above every other pack, while that language is chosen. Note: texture packs cover the 3D
models' textures today; the 2D sprites (most of the words above) are not covered by packs yet (docs/mods.md), so those
stay English for now.

## Sharing a translation

A translation is the .txt file (plus its font and packs, if any). Put them in a zip with the same folders
(`data\lang\`, `data\fonts\`, `data\mods\lang\<code>\`): players unzip it into the game's folder. When the game gets new
texts, `hotd --export-lang <code>` writes `data\lang\<code>.all.txt`: every key, your translations filled in and the new
ones as `#` lines to do.

## For developers

- The lookup sits where our texts are drawn (Frontend::drawText / drawSmall / rText, Overlay::setText, the launcher's
  first-run page): the code keeps its English strings; `game/lang.h` explains the rules. English never goes through it
  (the same pixels as before).
- A text cut into lines (help lines, descriptions, messages) is translated first, then cut by the language's widths
  (`lang::wrapBy`, `lang::wrapColumns`, Frontend::wrapTr); a Hangul / kana / CJK line breaks between any two characters.
- `--export-lang` records every text the menu screens draw (both styles, every Gallery entry, the online pages'
  previews, every widget's help, value and dropdown), plus the in-game notes listed in main.cpp's `langTool`, plus the
  subtitle table read from the arcade ROM. A new text shows up by itself; a text the code builds from numbers
  wants a pattern (`lang::addPattern` in langTool) so the export lists it once.
- Right to left: `render/bidi.h` (`visual`: one line shaped and in display order) runs inside `TtfText::width` /
  `draw` for any line with a right-to-left letter (other lines are untouched, so English and the other languages draw
  the same pixels as before); wrapping works on the reading order, each line is reordered after. `lang::rtl()` mirrors
  the layouts (Frontend `Widget::rtlRow`, `drawRRow`, `tabStrip`, `rTabsRtl_`).
