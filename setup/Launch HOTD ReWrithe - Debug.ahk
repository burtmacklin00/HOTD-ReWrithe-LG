#Requires AutoHotkey v1.1
; The House of the Dead: ReWrithe - Light Gun Edition: the DEBUG launcher (AutoHotkey 1.1).
; "Launch HOTD ReWrithe.ahk" (it must sit beside this file) with its logs on, all in the settings folder:
;   settings\launcher.log   the launcher's steps (start, the game's window, Esc, the game closing, Hook of the Reaper)
;   settings\outputs.log    every message the game sends to Hook of the Reaper, timed (recoil, ammo, life, damage, death)
;   settings\hotd.log       the game's own log (always written): guns, LightgunHID.ini, menus, bezel ...
; To report a problem: play until it happens, close the game, send the three files.
Debug := true
#Include %A_ScriptDir%\Launch HOTD ReWrithe.ahk
