#Requires AutoHotkey v1.1
; The House of the Dead: ReWrithe - Light Gun Edition: the launcher (AutoHotkey 1.1).
;
;   Starts the game (bin\hotd.exe) straight away: no splash, no pop-ups (front-ends such as LaunchBox are never
;   interrupted). The game itself draws the 4:3 bezel (4x3\bezel.png), reads LightgunHID.ini and sends the outputs
;   for Hook of the Reaper.
;   Esc       closes the game completely (cleanly: it saves its settings), then this launcher.
;   1         opens the game's pause menu (built into the game, as P; nothing to do here).
;   When the game closes (Esc, or Quit in its menu), Hook of the Reaper is asked to close too (a clean close only;
;   it is never started here).
;
; Optional argument: 4x3 or 16x9 (runs "2 - Display - 4x3.exe" / "2 - Display - 16x9.exe" first), e.g.
;   "Launch HOTD ReWrithe.ahk" 4x3
;
; Your settings: "Launch HOTD ReWrithe.ini" beside this launcher (updates never replace it), section [Launcher]:
;   IgnorePads=1   every gamepad / XInput controller is ignored (light guns, mouse and keyboard only); default 0
;   CloseHotr=0    leave Hook of the Reaper running when the game closes; default 1
;   Debug=1        the logs (as "Launch HOTD ReWrithe - Debug.ahk"); default 0
;
; Other launchers build on this one (they set their own values, then #Include it), so a change here reaches them too:
;   "Launch HOTD ReWrithe - Debug.ahk"   Debug := true
;   a front-end's own launcher           e.g. its gun set-up first, IgnorePads := true, EndCommand := "<a program>"
; Values they can set before the #Include: IgnorePads, CloseHotr, Debug (true / false), EndCommand (run at the end).
#NoEnv
#SingleInstance Force
SendMode Input
SetWorkingDir %A_ScriptDir%

; ---- settings: a launcher that includes this one wins, then Launch HOTD ReWrithe.ini, then the defaults ----
IniFile := A_ScriptDir "\Launch HOTD ReWrithe.ini"
if (IgnorePads = "") {
    IniRead, IgnorePads, %IniFile%, Launcher, IgnorePads, 0
    IgnorePads := (Trim(IgnorePads) = "1")
}
if (CloseHotr = "") {
    IniRead, CloseHotr, %IniFile%, Launcher, CloseHotr, 1
    CloseHotr := (Trim(CloseHotr) != "0")
}
if (Debug = "") {
    IniRead, Debug, %IniFile%, Launcher, Debug, 0
    Debug := (Trim(Debug) = "1")
}

Game     := "hotd.exe"
GamePath := A_ScriptDir "\bin\hotd.exe"
quitting := false

; the logs (Debug): this launcher's steps in settings\launcher.log, every output message in settings\outputs.log
; (the game writes it), beside the game's own settings\hotd.log
LogFile := A_ScriptDir "\settings\launcher.log"
if Debug {
    FileCreateDir, %A_ScriptDir%\settings
    FileDelete, %LogFile%
    EnvSet, HOTD_OUTPUTS_LOG, 1
}
Log("launcher started (" A_ScriptName ", AutoHotkey " A_AhkVersion ", " A_OSVersion ")")

if !FileExist(GamePath) {
    Log("the game was not found: " GamePath)
    MsgBox, 16, HOTD ReWrithe, The game was not found:`n%GamePath%`n`nKeep this launcher in the game's folder.
    ExitApp
}

; 4x3 / 16x9 from the command line
mode := A_Args.Length() ? A_Args[1] : ""
if (mode = "4x3" || mode = "16x9") {
    preset := A_ScriptDir "\2 - Display - " mode ".exe"
    if FileExist(preset) {
        RunWait, "%preset%", %A_ScriptDir%
        Log("display mode " mode ": ran " preset " (exit code " ErrorLevel ")")
    } else
        Log("display mode " mode ": " preset " not found")
}

if IgnorePads {
    ; SDL's own setting: ignore every pad but one that doesn't exist (not 0x0000/0x0000, which XInput pads often report);
    ; its RawInput pad driver skips that list, so it is switched off. Light guns are read as mice: not affected.
    EnvSet, SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, 0x0001/0x0001
    EnvSet, SDL_JOYSTICK_RAWINPUT, 0
    Log("gamepads ignored (IgnorePads)")
}
if Debug {
    IniRead, g1, %A_ScriptDir%\LightgunHID.ini, Lightguns, P1_Lightgun_HID, %A_Space%
    IniRead, g2, %A_ScriptDir%\LightgunHID.ini, Lightguns, P2_Lightgun_HID, %A_Space%
    IniRead, g3, %A_ScriptDir%\LightgunHID.ini, Lightguns, P3_Lightgun_HID, %A_Space%
    IniRead, g4, %A_ScriptDir%\LightgunHID.ini, Lightguns, P4_Lightgun_HID, %A_Space%
    Log("LightgunHID.ini: P1 [" Trim(g1) "] P2 [" Trim(g2) "] P3 [" Trim(g3) "] P4 [" Trim(g4) "]" (FileExist(A_ScriptDir "\LightgunHID.ini") ? "" : " (no file: the game asks for the guns)"))
    Process, Exist, HookOfTheReaper.exe
    Log("Hook of the Reaper " (ErrorLevel ? "is running (pid " ErrorLevel ")" : "is not running"))
}

Run, "%GamePath%", %A_ScriptDir%\bin,, gamePid
Log("game started (pid " gamePid ")")
WinWait, ahk_pid %gamePid%,, 30
if !ErrorLevel {
    WinActivate, ahk_pid %gamePid%
    WinGetPos, gx, gy, gw, gh, ahk_pid %gamePid%
    Log("game window " gw "x" gh " at " gx "," gy)
} else
    Log("no game window after 30 s")
SetTimer, GameGone, 1000
return

; the game closed by itself (Quit in its menu): close what goes with it
GameGone:
Process, Exist, %Game%
if !ErrorLevel {
    Log("the game closed by itself (Quit in its menu, or a crash: see hotd.log / crash-*.txt)")
    Gosub, Finish
}
return

#If WinActive("ahk_exe " Game)
$Esc::                                ; Esc: close the game completely (the game never sees it)
QuitAll:
if quitting
    return
quitting := true
Log("Esc: closing the game")
SetTimer, GameGone, Off
WinClose, ahk_exe %Game%              ; a normal close: the game saves its settings and ends
Loop, 80 {
    waited := A_Index * 100
    Process, Exist, %Game%
    if !(running := ErrorLevel)
        break
    Sleep, 100
}
if running {                          ; still running after 8 s: force it closed
    Log("the game was still running after 8 s: forced closed")
    Run, taskkill /im "%Game%" /F,, Hide
} else
    Log("the game closed (" waited " ms)")
Gosub, Finish
return
#If

Finish:
SetTimer, GameGone, Off
if CloseHotr
    CloseHOTR()
if (EndCommand != "") {               ; a front-end's launcher: its own last step
    Log("end command: " EndCommand)
    Run, %EndCommand%
}
Log("launcher done")
ExitApp
return

; Hook of the Reaper: asked to close (as its own close button does), then given a few seconds; never killed
CloseHOTR() {
    DetectHiddenWindows, On           ; (it may be in the tray)
    Process, Exist, HookOfTheReaper.exe
    if !ErrorLevel
        return
    WinClose, ahk_exe HookOfTheReaper.exe
    Process, WaitClose, HookOfTheReaper.exe, 5
    Log("Hook of the Reaper " (ErrorLevel ? "did not close within 5 s (left running)" : "closed"))
}

Log(text) {
    global LogFile, Debug
    if !Debug
        return
    FormatTime, t,, HH:mm:ss
    FileAppend, % t "." SubStr("00" Mod(A_TickCount, 1000), -2) "  " text "`n", %LogFile%
}
