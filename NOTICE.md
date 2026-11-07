# Notice

This is an unofficial fan project. It is not affiliated with, endorsed by or sponsored by SEGA. "The House of the
Dead" and related names are trademarks of their owners and are used here only to describe what the project works
with.

- **No game data is included or distributed.** The program reads the player's own, legally obtained copies of the
  arcade ROM set and the installed 1998 PC game, and never changes them.
- The MIT license in [LICENSE](LICENSE) covers the code, tools and documentation written for this project. It
  grants no rights to the original games, their code, data, artwork, audio or names.
- Parts of `src/port/` and the names / types in `tools/ghidra/` describe how the original programs work (routine by
  routine, from reverse engineering for interoperability). Anyone redistributing or building on them is responsible
  for doing so lawfully where they live.
- The Model 2 hardware knowledge (ROM layout, coprocessor, rendering) builds on MAME's model2 driver (BSD-3-Clause,
  mamedev.org); thanks to the MAME team.
- Third-party code keeps its own license: SDL3 (zlib license, fetched at build time), stb_vorbis (public domain /
  MIT, fetched or supplied at build time), dr_mp3 (public domain / MIT-0, github.com/mackron/dr_libs, supplied at
  build time: MP3 music), Monocypher 4.0.3 (online co-op's encryption, included in `src/third_party/monocypher/`:
  Loup Vaillant and the Monocypher contributors, monocypher.org; dual-licensed CC0-1.0 or BSD-2-Clause, see its
  LICENCE.md there).
- The optional arcade sound board (CMake option HOTD_ARCADE_SOUND, src/audio/sound_board.*) builds in two cores
  supplied at build time (not in this repository): MAME's SCSP / YMF292 (`scsp.cpp`, `scsp.h`, `scspdsp.cpp`,
  `scspdsp.h`, MAME tag mame0289; BSD-3-Clause, copyright-holders ElSemi, R. Belmont; mamedev.org) and Karl
  Stenerud's Musashi 68000 core (commit 313ebf1; MIT-style license, see its m68k.h / readme.txt; Musashi's
  m68kfpu.c / m68kmmu.h are not used: `src/third_party/musashi_shims` holds this project's own stand-ins). The small stand-in for
  MAME's device framework in `src/audio/mame_shim` is this project's own code (MIT).
- `data/ui/rewrithe_wordmark.png` (the Remake menu's "ReWrithe" wordmark) is this project's own art, made with the Special Elite
  font by Astigmatic (Apache License 2.0); the font itself is not included.
- `data/fonts/NotoSansKR-Bold.otf` and `data/fonts/NotoSansJP-Bold.otf` (the Korean / Japanese menu fonts of the
  translations, docs/translating.md) are Noto Sans CJK by Google and Adobe (Sans2.004, from
  github.com/notofonts/noto-cjk releases `17_NotoSansKR.zip` / `16_NotoSansJP.zip`), unchanged, under the SIL Open
  Font License 1.1: `data/fonts/OFL.txt`. SHA-256: KR `5a6ceb287ed2fc6cfc6213144ebea68cbd94b20fc9eb873d8486493bf02d9bda`,
  JP `1b0edfb500b73a4fa8a4fcaae1bbbd403994e08e73e3e0da37e70d3853f42c5f` (no checksums are published for them; these
  are of the files as downloaded 2026-10-26). The font reader / rasterizer (`src/render/ttf.*`) is this project's own code.
- `data/fonts/NotoSansArabic-Bold.ttf` and `data/fonts/NotoSansHebrew-Bold.ttf` (the Arabic / Hebrew menu fonts,
  docs/translating.md "Right-to-left languages") are Noto Sans Arabic v2.013 and Noto Sans Hebrew v3.001, Copyright
  2022 (Arabic) / 2024 (Hebrew) The Noto Project Authors (github.com/notofonts/arabic and github.com/notofonts/hebrew releases
  `NotoSansArabic-v2.013.zip` / `NotoSansHebrew-v3.001.zip`, the `unhinted/ttf` Bold files), unchanged, under the SIL
  Open Font License 1.1: `data/fonts/OFL.txt`. SHA-256: Arabic
  `f4cb79f842a07d1bcf597879574358fb7010f73761396d1d0f2445dfedb4d2ca`, Hebrew
  `dfdb3056de1f4542b888c77a1a8a750548a802e271479f56e52152423b64dde8` (of the files as downloaded 2026-10-26). The
  bidirectional algorithm and the Arabic shaping (`src/render/bidi.*`) are this project's own code.
- `data/fonts/NotoSansSC-Bold.otf` (the Simplified Chinese menu font) is Noto Sans CJK SC by Google and Adobe (Version
  2.004, from github.com/notofonts/noto-cjk), Copyright 2014-2021 Adobe, and `data/fonts/NotoSans-Bold.ttf` (the
  Latin / Greek / Cyrillic / Vietnamese menu font: Russian, Turkish, Polish, Vietnamese) is Noto Sans v2.015, Copyright 2022 The Noto Project Authors
  (github.com/notofonts/latin-greek-cyrillic); both unchanged, under the SIL Open Font License 1.1:
  `data/fonts/OFL.txt`. SHA-256: SC `c6cb5a93abaa9edc8ee7463b7ebb7f42d618d40e6ed2f7a5371c97b0b64767c0`, Noto Sans
  `1df075a380fc7cb898acf64c1f7b3b4dd780de3caa860178bf929de35817a913` (of the files as downloaded 2026-11-03).
- `data/ui/prompts/xelu/` (the default button prompts: every controller's and the keyboard's / mouse's icons) are from
  **Xelu's Free Controller & Key Prompts** by Nicolae "Xelu" Berbece (Those Awesome Guys), updated by Paul Paun,
  thoseawesomeguys.com/prompts; public domain under Creative Commons Zero (CC0 1.0): `data/ui/prompts/LICENSE-Xelu.txt`
  (the pack's readme). Only the icons the game uses are included, unchanged.
- `data/ui/prompts/kenney/` (Options > Controls > Button Style "Kenney", and the families Xelu's pack lacks: Switch 2,
  GameCube, paddles, Joy-Con SL / SR) are from **Input Prompts** 1.5A by Kenney (kenney.nl); Creative Commons Zero
  (CC0 1.0): `data/ui/prompts/LICENSE-Kenney.txt`. Only the icons the game uses (Default 64 px and Double 128 px),
  unchanged. Button Style "Simple" draws its own caps and shapes (`src/game/frontend_remake.cpp`, this project's code).
- `data/typing/ding.wav` (The Typing of the Dead 0's bell) is made from **"Typewriter, bell #2"** by Joseph SARDIN,
  BigSoundBank.com (sound 2845); public domain under Creative Commons Zero (CC0 1.0). Mixed half clean, half 8-bit
  22 kHz for this project.
- The Typing of the Dead 0's design reference (no footage is included or used in the game): the playthrough video "The
  Typing of the Dead [PC] by SEGA ($6,132,600) [HD] [1080p60]" by the YouTube channel **Saturn Memories**
  (youtube.com/@SaturnMemories, https://www.youtube.com/watch?v=SDK88HHndK4).
- The Spanish dub (Options > Audio > Voice: Spanish, `docs/spanish.md`): researched on the Spanish PC version
  ("Doblaje español") provided by **3lc3z4r** (YouTube channel **Cez4rPlay**), with his list of its differences and his
  reference playthrough "The House Of The Dead (PC) - Doblaje español ORIGINAL" (https://youtu.be/OVnjusRO1wk).
  No Spanish game file is included: the voices are read from the player's own Spanish copy; only file sizes and SHA-1
  hashes are in the source (`src/game/pc_manifest_es.inc`).
- The Windows package also carries Microsoft's Visual C++ runtime DLLs (`msvcp140*.dll`, `vcruntime140*.dll`), the
  redistributable files of Visual Studio 2022, shipped as Microsoft allows for programs built with it.
- Third-party code that is NOT compiled into the program: the Model 2 knowledge above (MAME), the developer tools in
  `tools/` (they run MAME / Ghidra on the developer's own files; neither is included).

## In the release package

| Part | Where | License | Its text |
|---|---|---|---|
| SDL3 3.2.10 | `bin/SDL3.dll` (Linux / macOS: `libSDL3`) | zlib | below |
| stb_vorbis 1.22 (Sean Barrett) | compiled in (OGG music) | public domain (or MIT) | not required (public domain chosen) |
| dr_mp3 (David Reid, commit 51e61d3) | compiled in (MP3 music) | public domain (or MIT-0) | not required (public domain chosen) |
| Monocypher 4.0.3 | compiled in (online co-op) | CC0-1.0 (or BSD-2-Clause) | `src/third_party/monocypher/LICENCE.md` (CC0 chosen) |
| Musashi (Karl Stenerud, commit 313ebf1) | compiled in (arcade sound board) | MIT | below |
| MAME SCSP / SCSP DSP (mame0289) | compiled in (arcade sound board) | BSD-3-Clause | below |
| Noto Sans KR / JP / SC / Arabic / Hebrew, Noto Sans | `data/fonts/` | SIL OFL 1.1 | `data/fonts/OFL.txt` |
| Xelu's Free Controller & Key Prompts | `data/ui/prompts/xelu/` | CC0 1.0 | `data/ui/prompts/LICENSE-Xelu.txt` |
| Kenney Input Prompts 1.5A | `data/ui/prompts/kenney/` | CC0 1.0 | `data/ui/prompts/LICENSE-Kenney.txt` |
| "Typewriter, bell #2" (Joseph SARDIN, BigSoundBank.com, sound 2845) | `data/typing/ding.wav` | CC0 1.0 | not required |
| Visual C++ runtime (Windows only) | `bin/` | Microsoft Visual Studio redistributable | Visual Studio's license terms |
| ANGLE (The ANGLE Project Authors, chromium/7258; Windows only) | `bin/libEGL.dll`, `bin/libGLESv2.dll` (the DirectX renderer; built by vcpkg's `angle` port from Google's sources) | BSD-3-Clause | below |
| xxHash (Yann Collet; inside ANGLE) | `bin/libGLESv2.dll` | BSD-2-Clause | below |
| zlib 1.3.2 (Jean-loup Gailly, Mark Adler; ANGLE's) | `bin/z.dll` | zlib | below |
| OpenXR SDK loader 1.1.65 (The Khronos Group Inc.; Windows only) | `bin/openxr_loader.dll` (VR headsets: `--vr openxr`, docs/vr.md; built against the SDK's prebuilt loader, not in this repository) | Apache-2.0 | `licenses/OpenXR-LICENSE.txt` in the package (`docs/release/LICENSE-OpenXR-Apache-2.0.txt` here) |
| OpenXR loader for Android 1.1.65 (The Khronos Group Inc.; the Meta Quest APK only) | `lib/arm64-v8a/libopenxr_loader.so` in the APK (docs/quest.md; from `openxr_loader_for_android-1.1.65.aar`, not in this repository) | Apache-2.0 | `assets/licenses/OpenXR-LICENSE.txt` in the APK |
| SDL3's Android Java activity (the Meta Quest APK only) | `classes.dex` in the APK (SDL's `android-project` sources, compiled) | zlib | `assets/licenses/SDL-LICENSE.txt` in the APK |

### SDL3 (zlib license)

```
Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

### Musashi (MIT license)

```
Copyright © 1998-2001 Karl Stenerud

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

### MAME SCSP (BSD-3-Clause)

```
Copyright (c) ElSemi, R. Belmont (scsp.cpp, scsp.h, scspdsp.cpp, scspdsp.h; MAME, mamedev.org)
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### ANGLE (BSD-3-Clause)

```
Copyright 2018 The ANGLE Project Authors.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

    Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following
    disclaimer in the documentation and/or other materials provided
    with the distribution.

    Neither the name of TransGaming Inc., Google Inc., 3DLabs Inc.
    Ltd., nor the names of their contributors may be used to endorse
    or promote products derived from this software without specific
    prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

### xxHash (BSD-2-Clause; compiled into ANGLE)

```
xxHash Library
Copyright (c) 2012-2014, Yann Collet
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### zlib (zlib license; z.dll beside ANGLE)

```
 (C) 1995-2026 Jean-loup Gailly and Mark Adler

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

Jean-loup Gailly        Mark Adler
jloup@gzip.org          madler@alumni.caltech.edu
```
