#Requires AutoHotkey v1.1
; The House of the Dead: ReWrithe - Light Gun Edition: LINK PLAY JOIN (AutoHotkey 1.1).
; "Launch HOTD ReWrithe.ahk" (it must sit beside this file; Esc, 1 and Hook of the Reaper as there) with the game going
; straight to Online Co-op > Join a Game:
;   - Same network (LAN): the host's game is found and joined by itself: nothing to type.
;   - Over the internet: copy the host's join code first (Ctrl+C), then start this: the code on the clipboard is used.
;   - Neither found: the Join page stays open (Paste Code / the network's list).
; Every PC needs the same build and game files. First time: Windows Firewall asks: allow Private networks.
LinkPlay := "join"
#Include %A_ScriptDir%\Launch HOTD ReWrithe.ahk
