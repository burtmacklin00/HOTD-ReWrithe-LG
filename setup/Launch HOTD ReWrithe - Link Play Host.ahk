#Requires AutoHotkey v1.1
; The House of the Dead: ReWrithe - Light Gun Edition: LINK PLAY HOST (AutoHotkey 1.1).
; "Launch HOTD ReWrithe.ahk" (it must sit beside this file; Esc, 1 and Hook of the Reaper as there) with the game going
; straight to Online Co-op > Start Hosting, with the Host page's last settings (Arcade / PC Mode / Boss Mode, players).
;   - Same network (LAN): your friends run "Launch HOTD ReWrithe - Link Play Join.ahk": nothing to type.
;   - Over the internet: your join code is copied to the clipboard: paste it to your friends (Discord, a text ...).
;   - The game starts by itself once a friend is in; more can join the running game with start.
; Every PC needs the same build and game files. First time: Windows Firewall asks: allow Private networks.
LinkPlay := "host"
#Include %A_ScriptDir%\Launch HOTD ReWrithe.ahk
