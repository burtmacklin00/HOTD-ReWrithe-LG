// quest (2026-11-03, docs/quest.md): the Meta Quest build's activity: SDL's own (org.libsdl.app.SDLActivity, from the
// SDL3 source's android-project), with the Khronos OpenXR loader loaded before the game (libmain.so: SDL_main)
package com.tawhid.hotdrewrithe;

import android.content.Context;
import android.media.MediaScannerConnection;
import android.os.Bundle;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

public class HotdActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "openxr_loader", "main"};
    }

    // questperf (user 2026-11-10: "extremely clunky on Meta Quest"): the window's own picture (the menus and the flat
    // screens, shown on the big screen in VR; the panel itself never shows it) at 1920 x 1080, not the headset's
    // 4128 x 2208: under a quarter of the pixels to draw, copy and mipmap every menu frame
    @Override
    protected SDLSurface createSDLSurface(Context context) {
        SDLSurface s = super.createSDLSurface(context);
        s.getHolder().setFixedSize(1920, 1080);
        return s;
    }

    // vrship (user 2026-11-09): the game files' folder made findable from a PC without adb: the app's own files folder
    // (Android/data/com.tawhid.hotdrewrithe/files: readable by the game with no permission) gets its pc/ folder and a
    // note, both handed to the media scanner, so a PC's file browser (USB, "Allow access to data") and SideQuest's file
    // manager list them at once (Android shows files made on the device over USB only after a scan)
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try {
            File base = getExternalFilesDir(null);
            if (base == null) return;
            File pc = new File(base, "pc");
            pc.mkdirs();
            File note = new File(base, "PUT YOUR GAME FILES HERE.txt");
            if (!note.exists()) {
                String text = "The House of the Dead: ReWrithe (Meta Quest): your own game files go in this folder.\r\n"
                    + "\r\n"
                    + "  hotd.zip   the arcade ROM zip (MAME's \"hotd\" set), here beside this file\r\n"
                    + "  pc         the 1998 PC game: everything in your installed PC game's folder (THOTD.EXE\r\n"
                    + "             directly in pc or one folder down), or a .zip of it\r\n"
                    + "\r\n"
                    + "The game looks for them every 3 seconds while its \"needs your own game files\" page shows.\r\n"
                    + "settings/hotd.log (made by the game) is the log to send with a bug report.\r\n";
                FileOutputStream out = new FileOutputStream(note);
                out.write(text.getBytes(StandardCharsets.UTF_8));
                out.close();
            }
            MediaScannerConnection.scanFile(this, new String[] {note.getAbsolutePath(), pc.getAbsolutePath()}, null, null);
        } catch (Exception e) {
            // (only a help for finding the folder: the game's own page shows the path)
        }
    }
}
