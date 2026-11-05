// quest (2026-11-03, docs/quest.md): the Meta Quest build's activity: SDL's own (org.libsdl.app.SDLActivity, from the
// SDL3 source's android-project), with the Khronos OpenXR loader loaded before the game (libmain.so: SDL_main)
package com.tawhid.hotdrewrithe;

import org.libsdl.app.SDLActivity;

public class HotdActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "openxr_loader", "main"};
    }
}
