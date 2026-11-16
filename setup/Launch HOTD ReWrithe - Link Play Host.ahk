#Requires AutoHotkey v1.1
; The House of the Dead: ReWrithe - Light Gun Edition: LINK PLAY HOST (AutoHotkey 1.1).
; "Launch HOTD ReWrithe.ahk" (it must sit beside this file; Esc, 1 and Hook of the Reaper as there) with the game going
; Online Co-op's Host page: pick Players, 2 (1 per PC: you Player 1, your friend Player 2) or 4 (2 per PC: you
; Players 1 + 2, your friend's PC Players 3 + 4), and the game mode, then Start Hosting.
;   - Same network (LAN): your friends run "Launch HOTD ReWrithe - Link Play Join.ahk": nothing to type.
;   - Over the internet: your join code is copied to the clipboard: paste it to your friends (Discord, a text ...).
;   - The game starts by itself once your friend is in.
; Every PC needs the same build and game files. First time: Windows Firewall asks: allow Private networks.
LinkPlay := "host"
#Include %A_ScriptDir%\Launch HOTD ReWrithe.ahk
