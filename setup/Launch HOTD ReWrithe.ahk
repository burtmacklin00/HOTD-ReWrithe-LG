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
#NoEnv
#SingleInstance Force
SendMode Input
SetWorkingDir %A_ScriptDir%

; ---- settings ----
IgnorePads := false   ; true: every gamepad / XInput controller is ignored (light guns, mouse and keyboard only)
CloseHotr  := true    ; true: Hook of the Reaper is closed when the game closes

Game     := "hotd.exe"
GamePath := A_ScriptDir "\bin\hotd.exe"
quitting := false

if !FileExist(GamePath) {
    MsgBox, 16, HOTD ReWrithe, The game was not found:`n%GamePath%`n`nKeep this launcher in the game's folder.
    ExitApp
}

; 4x3 / 16x9 from the command line
mode := A_Args.Length() ? A_Args[1] : ""
if (mode = "4x3" || mode = "16x9") {
    preset := A_ScriptDir "\2 - Display - " mode ".exe"
    if FileExist(preset)
        RunWait, "%preset%", %A_ScriptDir%
}

if IgnorePads {
    ; SDL's own setting: ignore every pad but one that doesn't exist (not 0x0000/0x0000, which XInput pads often report);
    ; its RawInput pad driver skips that list, so it is switched off. Light guns are read as mice: not affected.
    EnvSet, SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, 0x0001/0x0001
    EnvSet, SDL_JOYSTICK_RAWINPUT, 0
}

Run, "%GamePath%", %A_ScriptDir%\bin,, gamePid
WinWait, ahk_pid %gamePid%,, 30
if !ErrorLevel
    WinActivate, ahk_pid %gamePid%
SetTimer, GameGone, 1000
return

; the game closed by itself (Quit in its menu): close what goes with it
GameGone:
Process, Exist, %Game%
if !ErrorLevel
    Gosub, Finish
return

#If WinActive("ahk_exe " Game)
$Esc::                                ; Esc: close the game completely (the game never sees it)
QuitAll:
if quitting
    return
quitting := true
SetTimer, GameGone, Off
WinClose, ahk_exe %Game%              ; a normal close: the game saves its settings and ends
Loop, 80 {
    Process, Exist, %Game%
    if !(running := ErrorLevel)
        break
    Sleep, 100
}
if running                            ; still running after 8 s: force it closed
    Run, taskkill /im "%Game%" /F,, Hide
Gosub, Finish
return
#If

Finish:
SetTimer, GameGone, Off
if CloseHotr
    CloseHOTR()
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
}
