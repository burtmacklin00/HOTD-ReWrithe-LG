# Meta Quest 3 (standalone)

Worker "quest", 2026-11-03. Goal: the game in VR on a Meta Quest 3 with no PC: an Android APK (arm64-v8a) sideloaded
over USB. **Status: the APK builds and its contents are checked; nothing has run on a headset yet** (no headset was
connected while it was made). The Windows build is unchanged (the regression runs give the same numbers).

What it is: the same game and VR mode as `hotd --vr openxr` on a PC (docs/vr.md), built for Android:

- **Graphics:** OpenGL ES 3 (the Quest's Adreno, ES 3.2) through the renderer's existing ES layer (the one ANGLE uses:
  the shaders rewritten as `#version 300 es`, texture buffers as 2D textures, RGB uploads / reads as RGBA, no GPU timer
  queries; render/gl_backend.cpp). On Android it always runs, on the device's own EGL driver.
- **VR:** the OpenXR backend (src/vr/vr_openxr.cpp) with the Khronos OpenXR loader for Android: `xrInitializeLoaderKHR`
  (XR_KHR_loader_init_android: the Java VM and the activity), XR_KHR_android_create_instance, XR_KHR_opengl_es_enable
  (`XrGraphicsBindingOpenGLESAndroidKHR`: the game's EGL display, config and context). The loader finds Meta's runtime
  through the OpenXR runtime broker (the manifest's queries).
- **The menus in VR:** every flat screen (the files message, the setup page, the menu, the end screen) is shown on a
  big screen in front of you; the right controller is the mouse (its ray, the trigger clicks). In play the game is the
  VR game of docs/vr.md (the gun hand, the crosshair, the HUD on the arcade screen's plane).
- **Sound:** SDL3's Android audio (AAudio). **Settings, scores, the log:** the app's files folder (below).
- **No game data in the APK.** The player copies their own files to the headset (below). The APK holds only the game's
  own `data\` folder (fonts, menu pictures, translations, vr.ini), as every package does.

## Building the APK

Needs (all in `<main checkout>\private\deps`, never committed): `android-sdk` (platform-tools, platforms;android-34,
build-tools;35.0.1, ndk;27.3.13750724, cmake;3.31.6), `openxr_android` (the Khronos loader
`openxr_loader_for_android-1.1.65.aar` from Maven Central, Apache 2.0; the script unpacks it), the usual `dr_libs`,
`musashi`, `mame_scsp`, `stb`; a JDK 17+ on PATH (21 used); Python 3; SDL 3.2.10's source (the main checkout's
`build\_deps\sdl3-src`); a Windows build's `build\m68kgen` (the arcade sound's generated 68000 core: a cross build
cannot run m68kmake; without it the APK has no arcade sound board, the PC sound still works).

```
powershell -ExecutionPolicy Bypass -File tools\make_quest_apk.ps1            # build (CMake + NDK) and package
powershell -ExecutionPolicy Bypass -File tools\make_quest_apk.ps1 -NoBuild   # package the last build again
```

Result: `build-quest\HOTD-ReWrithe-quest-<version>-debug.apk` (about 15 MB), signed with a debug key that the script
makes once as `private\deps\android-debug.keystore` (keep it: an update must be signed with the same key to install over
the last one).

How (no Gradle, nothing downloaded at build time): CMake with the NDK's toolchain builds the game as `libmain.so` (SDL's
Android entry: `SDL_main`) and SDL as `libSDL3.so` (arm64-v8a, minSdk 32, the static libc++); `javac` + `d8` compile SDL's
own Java activity (the SDL source's `android-project`, the same version as the library) and ours
(`android/quest/java/.../HotdActivity.java`: SDL's activity, loading `libopenxr_loader.so` first); `aapt2` links the
manifest (`android/quest/AndroidManifest.xml`) with the game's `data\` as assets; `aapt` adds `classes.dex` and the three
libraries; `zipalign`, `apksigner`. The CMake changes for Android are all under `if(ANDROID)` (the game a shared library,
the developer tools left out, `--driver-mode=g++` for an NDK in a folder with spaces, a pre-generated `m68kops.c`).

Checked on the APK (aapt2 dump badging / xmltree, aapt list, apksigner verify): package `com.tawhid.hotdrewrithe`, label
"HOTD ReWrithe", no icon (Android's default), minSdk 32 / target 34, `lib/arm64-v8a/` libmain.so + libSDL3.so +
libopenxr_loader.so (native-code arm64-v8a only), the launcher intent with `com.oculus.intent.category.VR` and
`org.khronos.openxr.intent.category.IMMERSIVE_HMD`, `android.hardware.vr.headtracking` required, OpenGL ES 3.2, the
OpenXR permissions and broker queries, landscape, `assets/data_index.txt` + 751 data files.

## On the headset: Developer Mode (once)

Sideloading needs the headset in Developer Mode. **These steps are done by the headset's owner with their own Meta
account** (nobody else should sign in for them):

1. On developers.meta.com, sign in with the Meta account the headset uses and create (or join) an organisation (Meta
   asks for this once; it may ask to verify the account).
2. In the **Meta Horizon** app on the phone paired with the headset: Menu > Devices > the headset > Headset Settings >
   **Developer Mode** > on. Restart the headset.
3. Connect the headset to the PC with a USB-C cable (a data cable). In the headset, a box asks **"Allow USB
   debugging?"**: tick "Always allow from this computer", then Allow.

On the PC, adb is in `private\deps\android-sdk\platform-tools\` (or any Android SDK's platform-tools):

```
adb devices                      # the headset's serial and "device" (not "unauthorized": answer the box in the headset)
```

## Installing

```
adb install -r build-quest\HOTD-ReWrithe-quest-1.1.0-debug.apk
```

(`-r` replaces an older copy and keeps its files; to remove it: `adb uninstall com.tawhid.hotdrewrithe`, which also
deletes the copied game files.)

## Copying your game files

The game needs the player's own files, as on a PC: the arcade ROMs (the House of the Dead zip, Model 2 Revision A) and
the 1998 PC game's files. They go in the app's own folder on the headset:

```
/sdcard/Android/data/com.tawhid.hotdrewrithe/files/
    hotd.zip        the arcade ROMs (or a roms/ folder with the zip, or the loose ROM files, in it)
    pc/             the PC game's files: THOTD.EXE in pc/ (or one folder down), or a zip of them
    settings/       made by the game: launcher.ini, scores, hotd.log
    vr/vr.ini       made by the game on its first start (the VR settings)
```

Start the game once first (it makes the folder and shows where the files go), then from the PC:

```
adb push "C:\path\to\hotd.zip" /sdcard/Android/data/com.tawhid.hotdrewrithe/files/hotd.zip
adb push "C:\Games\HOTD" /sdcard/Android/data/com.tawhid.hotdrewrithe/files/pc
```

(the second line copies the whole installed PC game, its `Music` folder too if it has one; the folder becomes `pc/`, or
`pc/HOTD/` when `pc/` was there already: both are found.) The game looks again every 3 seconds while its message shows:
once both are there it checks them (the first check builds the ROM regions: a short wait) and the menu comes.

Other useful commands:

```
adb pull /sdcard/Android/data/com.tawhid.hotdrewrithe/files/settings/hotd.log          # the game's log
adb logcat -s SDL SDL/APP                                                             # the live log (Ctrl+C ends)
adb pull /sdcard/Android/data/com.tawhid.hotdrewrithe/files/vr/vr.ini                 # edit it, then:
adb push vr.ini /sdcard/Android/data/com.tawhid.hotdrewrithe/files/vr/vr.ini
```

## Starting it

In the headset: the App Library > the filter at the top right (All) > **Unknown Sources** > **HOTD ReWrithe**. It
starts in VR (no 2D window). Quit: the Meta button > Quit, or the game's own Quit.

## Controls (Touch controllers)

| where | right controller | left controller |
|---|---|---|
| menus, setup, end screen (the big screen) | point = the mouse, trigger = click, A = Enter, B = back (Esc) | X = Enter, Y = back, menu = back |
| in play | the gun (the game's own gun hand): trigger shoots, grip / A / B reload, point steeply down reloads | X = Start (join / continue), Y / grip reload, menu = pause |
| the pause | point at Continue / Exit (on the HUD's screen), trigger chooses | menu = pause / back to the game |

No keyboard or mouse is needed.

## vr.ini on the Quest (performance and comfort)

All of docs/vr.md's keys, and (data/vr/vr.ini lists them):

| key | default | what |
|---|---|---|
| `menu_panel` | auto (on on the Quest) | the flat screens on the big screen, the controller as the mouse |
| `menu_distance`, `menu_width` | 2.5, 3.2 | the big screen: metres ahead, metres wide |
| `resolution_scale` | 1.0 | the eyes' resolution x this (0.5 .. 1.5 of the headset's recommended size): **0.8 if it stutters** |
| `hud_lines` | 960 | the HUD panel's height in pixels (mipmapped every frame): 720 / 540 are cheaper |
| `arcade_sort` | auto (off on the Quest) | the flat screens' 3D (the menu's live background) with the arcade polygon sort; off: the depth buffer. On OpenGL ES the sort's keys are a float texture rewritten every frame (heavy); VR's eyes never sort |
| `srgb_swapchain` | on | off: an RGBA8 headset picture (if the colours look washed out or too dark) |

**Performance (72 Hz on the Quest 3), what looks heavy:** the world is drawn twice (once per eye) at the headset's
recommended size (about 1680 x 1760 per eye): `resolution_scale` is the main switch. The arcade sort (above) is off by
default. The HUD panel is redrawn and mipmapped every frame (`hud_lines`). Texture packs (data mods) make 2048 x 2048
atlases (up to 31 layers, RGBA): keep big packs off on the headset (Options > Mods). The headset's `xrWaitFrame` paces
the game (the frame limiter and the window's swap are off on Android while VR runs). The game logic is the arcade's 57.5 /
60 Hz either way; Smooth Motion blends the camera to the display's rate. The eye's depth cannot be read back on OpenGL
ES, so the gun's ray uses the fixed `aim_distance` for the 3D crosshair's distance (the aim itself is exact: it goes
through the game camera).

## First test on the headset (checklist)

1. `adb install` succeeds; the app is under Unknown Sources.
2. Start it: a VR view (not a flat window) with a dark room and the big screen: the message "needs your own game files"
   with the two adb lines. If nothing shows or it quits: `adb logcat -s SDL SDL/APP` and look for `openxr:` lines (the
   runtime, the session, the states 1 2 3 4 5) and `quest:` lines.
3. Push the files (above); within a few seconds the check runs and the menu shows on the big screen.
4. The right controller's red ray and dot follow your hand on the screen; the trigger clicks menu items; B goes back.
5. Start Arcade: the world around you, the gun hand on the right controller, the crosshair where it points, the HUD
   ahead; the trigger shoots (a short rumble), grip / A / B reload, X = Start, the left menu button pauses (point at
   Continue / Exit, trigger).
6. Colours as on the PC? Washed out: `srgb_swapchain = off` in vr.ini (and say which looked right). The log says
   `openxr: OpenGL ES sRGB write control there / missing` and which swapchain format it took.
7. Smooth at 72 Hz? If not: `resolution_scale = 0.8`, then `hud_lines = 720`; send hotd.log.
8. Sound plays (menu music, shots).
9. Take the headset off and on, open the Meta menu during play and close it: the game comes back; Quit from the Meta
   menu ends it.
10. If the gun sits wrong in the hand: `gun_offset` / `gun_rotate` (docs/vr.md).

## Untested / open (needs the headset)

Everything on the device: the app starting at all (SDL's activity in an immersive VR app; the window surface SDL makes
is never shown), the loader finding Meta's runtime, the session and its states, the ES binding (the EGL config looked up
from the context's EGL_CONFIG_ID), the swapchain format and sRGB, the frame rate, AAudio, the files check on the
headset's storage (files pushed with adb into the app's folder: normally readable by the app; if not, start the app
once before pushing), suspend / resume (SDL blocks its loop while the activity is paused), the controls' bindings
(the Touch profile of docs/vr.md), the haptics. The first start copies the game's data folder (751 files, 13 MB) from
the APK into the app's internal storage (a few seconds once per build).
