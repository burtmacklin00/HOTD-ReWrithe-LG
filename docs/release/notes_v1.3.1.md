# The House of the Dead: ReWrithe v1.3.1

> A fan-made rewrite, **not affiliated with, endorsed by or sponsored by SEGA**. **No game data is included in any
> download**: you need your own arcade ROMs (MAME's `hotd`, Revision A) and your own 1998 PC game.

> **AI disclaimer:** ReWrithe was made with AI assistance: AI tools helped reverse engineer the original game and combine its code to bring the arcade and PC versions together. No generative AI has been or ever will be used to create artistic assets. Everything you see and hear comes from the original games or from human-made work. Any new model, texture or sound effect will always be made by humans.

The Spanish PC version's dub, from your own Spanish copy, and a round of fixes from the GitHub issues. Coming from
v1.3.0: the game offers the update itself (Update / Not now); your settings and game files are kept.

## New

- **Spanish dub ("Doblaje español")** from your own copy of the 2003 Spanish PC CD. No Spanish file is included.
  - Add it on the setup's new **Extras** page (or Options > System > Repair / Change Files), next to your US / EU
    copy: the installed Spanish game, or the Spanish CD image itself (also in a .rar / .7z / .zip). It also works as
    the PC game on its own. A copy that can't be used is simply left out.
  - Options > Audio > **Voice**: Auto / Spanish / English, in every mode, with arcade or PC models and either sound
    source. Options > Audio > **Reload Voice**: On / Off.
  - With the language **Español**, the Spanish CD's translated pictures: chapter cards, boss cards, CARGA, CONTINUAR,
    the signs, credits and map. All with PC models, most with arcade models; the menu notes say what stays in
    English.
  - The Spanish translation uses the Spanish CD's own terms.
  - Thanks to **3lc3z4r** (YouTube: Cez4rPlay) for the copy, its list of differences and the reference playthrough
    ["The House Of The Dead (PC) - Doblaje español ORIGINAL"](https://youtu.be/OVnjusRO1wk). More in
    [docs/spanish.md](../spanish.md).
- **Boss Names**, Options > Gameplay: Full / Codenames Only.

## Fixed

- Every character uses only its own head and body in cut-scenes and endings (no more another character's head on a
  researcher's body).
- The floating skull and plates in Chapter 1 with PC models in Arcade Mode (#52).
- Slash and scratch marks follow Hit Marks (#56); black side bars in the attract's white cuts in 4:3 (#57); smooth
  cut-scene bars (#58).
- The white fade after the Magician's defeat with PC models (#55, in part).
- PC Mode: characters with more hit points than Max Lives keep them (#62, in part).
- Chapter Retry keeps the rescued researchers count for the last chapter's bonus room (#47).
- Online: a joiner playing a character 7-19 was refused as a wrong password.

## Known issues

- Still open from the issues: the bonus room with PC models (#51), the credits mask with PC models (#53), the
  Magician's red trail (#54), the clear sky with PC models (#55), the water under the bridge (#59).
- **The Typing of the Dead 0 is experimental / a proof of concept**, as in v1.3.0.
- **Windows Defender may flag `hotd.exe`** (a false positive for a new, unsigned program; the full source is public).
  If it was quarantined: Windows Security > Virus & threat protection > **Protection history**, open the entry for
  `hotd.exe`, **Actions > Restore** (or Allow on device). Check the zip against `SHA256SUMS.txt` first if unsure.
- Everything else as in [v1.3.0](notes_v1.3.0.md#known-issues).

## Downloads

| File | |
|---|---|
| hotd-rewrithe-v1.3.1-win64.zip | Windows 10 / 11 (64-bit) |
| hotd-rewrithe-v1.3.1-linux-x86_64.tar.gz | Linux, Steam Deck |
| hotd-rewrithe-v1.3.1-macos-universal.zip | macOS 11+ (Apple silicon and Intel) |
| SHA256SUMS.txt | the SHA-256 of each file |

**For the auto-updater:** the release must carry `hotd-rewrithe-v1.3.1-win64.zip` and `SHA256SUMS.txt` as assets; the
release's description is the changelog the game shows.

Report bugs on the GitHub Issues page with `settings\hotd.log` (never attach game files or ROMs).
