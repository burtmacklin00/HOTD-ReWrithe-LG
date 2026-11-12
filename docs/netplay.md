# Online co-op (netplay)

Two PCs, two players, peer to peer between friends: no server of ours, no account, no library downloads. On a home
network and over the internet the host gives its friends one short **join code** (`XXXX-XXXX-XXXX-XXXX`: its
address, port and password in 16 symbols). Deterministic lockstep with a short input delay, plus **rollback** (8
frames) so the other player's lag does not slow the game. Code: `src/net/`. Phase 1 2026-10-19 (lockstep, crypto),
phase 2 2026-10-19/20 (menu, LAN list, share code, UPnP, CPace, ping, adaptive delay, rollback); 2026-10-26 the join
code, P<n> PAUSED, host-only cut-scene skip, late sounds after a rollback (protocol 13); the host's 4P Boss Hit Points /
4P Enemy Toughness in the settings for every PC (protocol 14, misc16: `boss4p` / `enemy4p` bytes after `gameText`);
2026-10-27 one or two seats per PC among up to four PCs (protocol 15, "One or two seats per PC"), PC Mode online
(protocol 16), a joiner's own PC Mode character in a running game (protocol 17, "PC Mode online, part 2"), Boss Mode
online (protocol 18, "Boss Mode online"); 2026-11-09 Survival online (protocol 22, "Survival online").

## Playing online

In the game: **Options > More Options > Online Co-op**.

- **Host a Game**: Start Hosting opens the **waiting room**: **Your Code** (the join code; a click copies it),
  **Copy Code** ("Copied!" for 2 s, SDL_SetClipboardText), who joined (name, ping), the internet status (UPnP).
  A new random code each time hosting starts (`--code` on the command line keeps a fixed password: then the long
  `HOTD-...` share code is shown). **Start Game** once someone is in, or **Play Alone**: the game starts now and
  stays open; player 2 joins the running game later with the same code and presses start like inserting a coin.
  4P Online and Game Name (the LAN list) are on the page before Start Hosting. The host's settings decide the game
  for every PC (chapter, players, models, difficulty, lives, blood, the sound source); each PC keeps its own
  crosshair, aspect, volumes.
- **Rejoin**: if player 2's PC drops out (6 s of silence), the host goes back to the last frame both agreed on and
  waits ("Player 2 lost the connection: they can join again"); player 2 joins again (the same password, from the
  list or the share code) and is back in the same game where it stopped. It need not wait out the 6 s: a join from
  the dropped PC's address (its IP; the restarted game's new port is fine) while that PC has been silent for 1 s
  makes the host hand the seat(s) back at once (`Session::rejoinHello` / `rejoinAsked`; before 2026-10-27 such a
  join was told "This game is full"). A live PC never sends HELLO once in, so another PC behind the same router is
  still told "full". No protocol change.
- **A player left** (2026-10-30, worker "netfix"): a PC that closes its window or picks Exit mid-game sends BYE with
  the frame count `kByeLeaving` (0xFFFFFFFF; `NetGame::leave`), not the end-of-run BYE (frames + hash, `finish`).
  Before, the host took any BYE as the end and stopped its game, so a player who closed the window could never join
  again (the rejoin test killed the joiner, which sends nothing, and passed). Now the host treats it like a drop: two
  PCs: back to the last confirmed frame, a new session, open for a join (`beginRejoinWait`); up to four PCs: the seat
  renewed. Either way the game **waits** (`dropHold_` / `dropPcs_`): no frame runs, every PC keeps the link; the
  host's line says "P2 left: waiting for them to join again (same code). Enter / Start: go on without them"; the
  host's PINGs carry the seats left in the paused byte's bits 4-7, the other PCs show "P2 left: the host waits for
  them or goes on". The PC back (any join of the running game) ends the wait; **Enter / Start** on the host
  (`dropContinue`) goes on without them: two PCs = playing alone, still open for a join (as Play Alone); four PCs =
  the seat stays free (neutral / CPU teammates). The frames are the host's either way, so every PC stays in sync.
  `--net-drop wait|continue|N` (N s, then on): defaults: a window game and any two-PC game wait; a headless game of up
  to four PCs goes on at once (as before). A host leaving still ends the game for the others ("The host left the
  game"). The end-of-run BYE (`--frames`, `--stop-at`, PC Mode / Boss Mode's end) is unchanged. Testing:
  `HOTD_NET_QUIT_AT=F` closes the window (or leaves, headless) at frame F.
- **Join a Game**: one box, **Join Code**: type the code (any case, dashes optional) or **Paste Code** / Ctrl+V,
  Enter or **Join**. On the same network the code finds the host's LAN address from the LAN list (the code's address
  is the announcement's sender or its announced internet address), so it works there too even when the code holds the
  internet address. The LAN list is below (click a game, type its code). The manual way: an address `1.2.3.4:47800`
  in the box shows a Password row (the host's password: its Internet line's help). Typos are refused before anything
  is sent (too short / long, 0 O 1 I L, the check symbol). The status shows "Waiting for the host" until Start.
- **With a controller**: the menu works with the d-pad / stick (South = choose, East = back). In a text field (name,
  password, address) the d-pad picks letters: Up / Down change the last letter, Right adds one, Left deletes, South
  keeps, East cancels. The keyboard types as usual (Ctrl+V pastes); Paste Share Code reads the clipboard.
- **The text launcher** (first start, or no PC menu art) has the same pages as rows at its bottom ("ONLINE CO-OP").
- **Inputs**: each PC's player plays with that PC's **Player 1 input** (mouse, gamepad, light gun; set in the
  launcher / More Options); on the client that is the game's player 2 (its pad's rumble, pause and start follow it).
  The keyboard's player 2 keys (aim, fire, reload) count for this PC's player only with Player 2 input = keyboard;
  player 1's keys (Options > Controls > Keys & Buttons) always do. In the 4P game on two PCs player 2's keys play
  this PC's second seat. Keyboard aim moves the crosshair once per input frame and is sent like the mouse's aim.
- **CPU teammates** (Co-op > Human-CPU seats on this PC): in the 4P game on two PCs this PC's second seat (Player 3
  on the host, Player 4 on the friend's PC; either setting counts) can be CPU; with up to four PCs the host's CPU
  seats play every seat no PC sits in (a friend joining takes it over). See "Input determinism" below.
- In the game: the ping at the bottom, in the middle (green / yellow / red, "LAG" when it struggles); **F3** switches
  it to the network lines (ping, input delay, rollbacks, waits, sync) and off. If the other PC stops
  answering: "Connection to the other PC lost: waiting (Esc: quit)"; it goes on if the other comes back. Esc > Exit
  leaves: the host keeps that seat and waits ("A player left" above); the host leaving: "The host left the game".

Command line (testing, or a desktop shortcut):

```
hotd --host 47800 [--code K7QF-2MXD] [--lan-name "Rogan's game"]   host (all interfaces); prints the password and share code
hotd --join 192.168.1.20:47800 --code K7QF-2MXD                   join by address
hotd --join R3B4-ZQ7S-CQ9V-7QN5                                    join by join code (any case, dashes optional)
hotd --join HOTD-1G5804A1WRFY-K7QF2MXD                             join by the long share code (a custom password)
hotd --join lan[:<game name>] --code K7QF-2MXD                     join the first (named) game in the LAN list
```

Options: `--net-name NAME` (the waiting rooms), `--pc-mode` on the host (an online PC Mode game), `--net-pc-char
a[,b]` (this PC's PC Mode characters 1-6, 0 = the seat's default; "PC Mode online" below), `--net-delay auto|N` (default auto), `--net-rollback N` (0-12, default 8; 0 = lockstep only), `--upnp
on|off|dry-run`, `--no-lan`; testing: `--net-sim loss,ms`, `--net-timeout S`, `--net-hash-every N`, `--net-fuzz N`,
`--lobby addr:port` (the LAN list on the loopback), `--net-bind addr` / `--net-port N` (the menu's host),
`HOTD_ONLINE_TEST=host|host-alone|host-alone-boss|join:<code or address>|<password>|join-lan:<code>|edit-name|launcher-alone|pc-picks|boss-picks` (the menu / launcher flows without clicks; `tools/netplay_menu_test.ps1` runs the menu pairs), `--net-alone` / `--net-solo-fps N` (a headless host alone), `HOTD_NET_DESYNC_AT=F`,
`HOTD_ROLLBACK_PROBE=N`. Headless runs (`--frames`) end with `NET: result=... frames=... hash=... peer_hash=...`.

## How to test on your PCs

Both PCs need the same build of ReWrithe and the same game files (ROMs; PC files when PC models are chosen): the start
compares both games' state and says so if they differ.

**Same Wi-Fi / LAN** (the easy one):
1. PC 1: Online Co-op > Host a Game > Start Hosting. Windows Firewall asks once whether hotd.exe may use the network:
   allow **Private networks** (home); Public is not needed.
2. PC 2: Online Co-op > Join a Game (PC 2's firewall asks once too: allow Private): type PC 1's code into Join Code
   (or Copy Code on PC 1, Paste Code on PC 2), Enter.
3. PC 1: Start Game when Player 2 shows a name. F3 shows the ping (a LAN: 1-5 ms, input delay 2).

**Over the internet**:
1. The host's waiting room "Internet" line: "Open: <address>" = UPnP opened the port, the code holds the internet
   address; "Your network only" = the router has no UPnP (or it is off): switch UPnP on there, or forward UDP port
   47800 to the host PC and have the friend type the host's internet address:47800 and the password (the Internet
   row's help line shows both the LAN address and the password).
2. Send the code (and only to your friend: it holds the password). The friend: Join Code (type or Paste Code) > Enter.
3. F3: a ping of 20-80 ms plays like a LAN thanks to rollback; above ~150 ms the other player's moves arrive late
   (their crosshair jumps), above ~250 ms the game waits now and then.
4. Not working: both PCs behind carrier-grade NAT / strict NAT (mobile hotspots) cannot connect directly; there is no
   relay (by design, no server).

## Architecture

- **Deterministic lockstep with input delay.** The game logic only sees per-frame inputs (`Runtime::gun[p]` and the
  start buttons). Each PC samples its local player's input at frame `f` and schedules it for frame `f + D`. D (the
  input delay) comes from the waiting room's ping when Start is pressed: one way + 12 ms in frames, 2..8 (LAN 2,
  internet 3-5); with rollback at most 2. Frames 0..D-1 are neutral on both sides.
- **Rollback** (`src/net/rollback.*`, `NetGame::reconcile`): a PC does not wait for the other player's input; it runs
  on a prediction (the last real input: the aim stays, no trigger / start), saving a snapshot before each such frame.
  When the real input arrives and differs, it restores that frame's snapshot and runs every frame since again (no
  log; the sounds: see "Rollback and sounds"), then goes on. Up to 8 frames (`--net-rollback`) ahead of the last
  confirmed frame; beyond that it waits as the lockstep does. A snapshot (`RuntimeState`) is the written memory
  (work RAM 0x500000, 0x700000, the 2D RAMs 0x1000000 / 0x1080000: ~1.7 MB, HOTD_ROLLBACK_PROBE showed nothing else
  changes), the geometry coprocessor (registers, B-RAM with the stored matrices) and the runtime's own between-frame
  fields (`Runtime`'s friend `RuntimeState`: the chapter-clear state, the 2D layer's PC cells, the palette animation,
  the teaser watchdog's counters, ...). A game over / the chapter's end are acted on only once every frame is
  confirmed (`settle`). New runtime fields that keep game state between frames must be added to `RuntimeState`
  (a miss shows as a desync in the rollback tests).
- **Input packets** carry frame-tagged inputs for every frame the peer has not acknowledged yet, so a lost packet is
  repaired by the next one. Each input is 9 bytes: flags (on screen, trigger, start) and the aim x / y as IEEE floats
  (checked finite and in range on receipt).
- **Ping**: PING / PONG every 250 ms (the waiting room's ping, the input delay, F3). PING's ninth byte (protocol 13):
  the seats paused (below).
- Four players: two PCs with two seats each (docs/coop4.md section 11: an input holds the PC's two seats), or up to four
  PCs with one or two seats each (the host relays, star: "Four PCs" and "One or two seats per PC" below).

## Pause, cut-scene skip, rollback and sounds (protocol 13, 2026-10-26)

- **Pause**: a PC's pause (Esc / P) stops its frames, so every other PC waits too. The paused PC keeps pumping the
  session (`NetGame::idle`: PINGs, inputs; before, a pause over 6 s looked like a dropped player to the host). Every
  PING carries a byte of paused seats (bit s = seat s); a change is sent at once. The host of up to four PCs ORs its
  seats' bits and passes the result to every seat. A waiting PC's status shows "P1 PAUSED" (the other PCs' seats)
  instead of "Waiting for the other player...". Headless: `HOTD_PAUSE_AT=F:N` on one side pauses it N x 16 ms and the
  other prints "the other PC shows: P1 PAUSED".
- **Only the host skips cut-scenes**: a trigger while the shots are off in play (game mode 4, `shot_enable` 0, not the
  name entry) is the skip request (`skip_request`, player_shoot). `NetGame::applyInputs` drops the other PCs' triggers
  there (2 PCs: the client's seats 1 / 3; up to four PCs: seats 1-3), from the game's state, so alike on every PC (and
  in a joiner's replay). A joiner who pulls the trigger there sees "Only the host can skip" for 2 s. Side effect: a
  joiner cannot reload during a cut-scene (the arcade's reload there is the same trigger); the gun reloads as usual
  once the shooting starts.
- **Rollback and sounds** (user 2026-10-26: "the host couldn't hear P2's gunfire; later it worked"): the frames
  re-run after a wrong prediction ran silent, and a remote shot is always mispredicted (the prediction never repeats a
  trigger). The PC that runs ahead (often the host: it starts first) predicts the other's input most of the time, so
  it lost most of the other player's shot sounds; the other PC got the host's inputs in time and heard everything.
  Now `Runtime::cueLog` records each frame's sound codes (the PC sound's cue, the board's queue drain, sound_send3);
  after a re-run, the codes the re-run sent that the first run did not are played at once (a few frames late, never
  lost); a predicted sound that did not happen has played already. `NET: ... late_sounds=N` counts them (2 players,
  40,000 frames on the loopback: ~170 on each PC). The sound source / volumes / focus: checked, nothing per player
  (the client uses the host's sound source; the focus-loss pause is off online).

## Transport and session flow

UDP, our own code on Winsock (`src/net/socket.*`, BSD sockets behind the same class). IPv4. Protocol 15 (`kProtocol`, session.h; 10: Monocypher on every system, "Security"; 11: the settings carry the host's Auto Reload, used for every seat; 12: Game Text; 13: PING's paused seats, the LAN announcement's internet address; 14: 4P Boss Hit Points / Enemy Toughness; 15: one or two seats per PC among up to four PCs).

1. Client -> host `HELLO` (nonce, CPace public key; resent every 250 ms; padded: no amplification).
2. Host -> client `HELLO_ACK` (nonce, public key, the client's nonce echoed).
3. Client -> host `CONFIRM` (encrypted: only the right password makes it authentic; the player's name). Wrong: `REFUSE` (wrong code),
   counted; after 20 the host takes no more joins (`REFUSE` locked).
4. Host -> client `WELCOME`: the waiting room (PING / PONG, `ROSTER` from now on).
5. The host's Start: `SETTINGS` (the game, the input delay; every field range-checked).
6. Both start the game and send `READY` with a hash of the started state (another build / other ROMs or files: an
   error before play). Then `INPUT` packets, `BYE` with the final frame and state hash.

## LAN lobby

`src/net/lobby.*`. The host broadcasts an announcement once a second to UDP port 47777 (255.255.255.255): magic,
protocol, the game's UDP port, players, chapter, needs-a-password, playing, the build id, the host's internet address
from UPnP (0 unknown; protocol 13: a joiner on the same LAN whose code holds that address connects to the LAN address
instead, routers seldom loop back), the name (32 printable ASCII characters). Join listens on 47777 (shared) and lists what it hears for 3 s;
the address is always the sender's, never one written in the packet. Everything is range-checked (200,000 random
packets in hotd_nettest). Tests use `--lobby 127.0.0.1:<port>` (the loopback: no firewall prompt).

**Open on the LAN** (2026-10-30, the default): needs-a-password 0 and, after the name, the password (a length byte, 4-32
letters / digits): a click in the LAN list joins with it, nothing typed (`--join lan` takes it too). A broadcast stays
on the local network (routers never forward it), so joins over the internet still need the code; anyone on the same
network can join (on a café's Wi-Fi, host with `--lan-code`: the old way, the password never announced, the code typed
after the click). The decoder refuses a password of other characters, an open game without one, and a closed game
with one. The announcement also goes to every adapter's own subnet broadcast (`localInterfaces`: GetAdaptersAddresses
/ getifaddrs, re-read every 10 s): Windows sends 255.255.255.255 out of one adapter only, and a VPN / Hyper-V /
VirtualBox adapter can be that one. The Join page: a typed join code whose address is an internet one and not heard
in the list yet waits up to 2.5 s after the page opened for the list (pasting the code at once after a restart sent
the joiner to the internet address, which routers seldom loop back); "No answer" from an address the list hears gets
the Windows Firewall hint (the host's game is alive on the network, its firewall drops the game's port).

## Join code (2026-10-26)

`lobby.cpp` makeJoinCode / parseJoinCode: one code, `XXXX-XXXX-XXXX-XXXX`, 16 symbols of a 31-symbol alphabet
(`ABCDEFGHJKMNPQRSTUVWXYZ23456789`: no 0 O 1 I L), any case, dashes / spaces optional. It is the number
`((ip x 4 + port - 47800) x 2^40 + secret) x 37 + check` written in base 31 (31^16 = 2^79.27 > 2^74 x 37): the host's
address (the internet address when UPnP gave it, else the LAN address), the port (47800-47803) and the password, and a
check symbol (mod 37 of a hash of the rest). The password is `randomCode()`: 40 random bits as 8 Crockford symbols, the
same string as before for CPace. A typo is refused before anything is sent (hotd_nettest: 12 of 480 single-symbol
typos and 1 of 15 neighbour swaps pass the check; those give another address or password: "no answer" or one wrong-code
try). The joiner on the same LAN: the code's address matched against the LAN list (the sender, or the announced
internet address) gives the host's LAN address.

**Security trade-off**: the secret part is 40 bits (~1.1 x 10^12). CPace is a PAKE: a recorded handshake does not let
anyone test passwords offline, so the only attack is guessing online, and the host locks after 20 wrong tries: 20 /
2^40 = 1.8 x 10^-11 per hosting. More bits would make the code longer (each 5 bits is one more symbol); fewer would
still be safe against online guessing but 40 keeps the margin of the old 8-character password. The address part is not
secret (anyone who sees the code can find the host), as before with the share code.

The old long share code `HOTD-XXXXXXXXXXXX-PASSWORD` (the address and port in Crockford base32 with a check byte, then
the password) stays for what the join code cannot hold: a custom password (`--code`, not 8 Crockford symbols) or a
port outside 47800-47803. `makeShareCode` gives the join code when it fits, else the long form; `parseShareCode` (and
`--join`) take both.

## Hole punching (2026-11-10, worker "natpunch"; no protocol change)

Online without port forwarding, still with no server of ours (`src/net/natpunch.*`, written from RFC 5389; no STUN /
ICE library's code). Three parts, all on the session's own UDP socket (Options::raw: every datagram the socket's owner
receives goes to NatTraversal::onPacket first; Session::sendPlain sends outside the protocol), so the NAT mapping
learned is the one the other PCs use:

1. **STUN** (RFC 5389 Binding Request / success response, XOR-MAPPED-ADDRESS, MAPPED-ADDRESS as the fallback; IPv4).
   The servers (`NetArgs::stun`: `--stun a,b,...`, launcher.ini `stunservers=`, default `net::kDefaultStun` =
   stun.l.google.com:19302, stun.cloudflare.com:3478, stun1.l.google.com:19302; `off` / `netstun=0` / the Online
   page's **Address Lookup** Off = no STUN, as before) are resolved on a thread (up to four), each asked up to 6 times
   500 ms apart; done after two answers (or one answer + 1 s, or 3.5 s). Two servers seeing two different mapped
   ports = a symmetric NAT (`NatTraversal::symmetric`): the waiting rooms warn "Your network may block direct
   connections". Who asks: a host bound to all interfaces; a joiner whose host address is on the internet (not
   `isPrivateIp`). A loopback test without `--stun-mock` asks nothing.
2. **The join code carries it**: `NetGame::shareCode` uses the STUN-mapped address and port when UPnP gave no
   external address (the 16-symbol join code when the mapped port is 47800-47803, which most home NATs keep; else the
   long `HOTD-...` share code, which holds any port). No new host code: old builds read the code as before. The LAN
   announcement's `publicIp` is the STUN address too, so a joiner on the same network still finds the LAN address.
3. **Punching**: punch packets are 16 bytes, `"HOTP"`, type (1 hello, 2 answer), a 32-bit id, zero padding; no
   secret (CPace authenticates the handshake that follows); every hello is answered (same size: no amplification;
   at most 64 answers a second). A joiner (to an internet host) sends its normal HELLO **and** punch hellos to the
   host at once (every 150 ms for 10 s): with a full-cone NAT, UPnP or a forwarded port that alone connects. With no
   answer after 3 s its page shows a **reply code** (`makeReplyCode`: a 2-bit kind, the joiner's STUN address, its LAN
   address in 10/8, 172.16/12 or 192.168/16 (24 bits + the class) and LAN port = 92 bits + an 8-bit BLAKE2b check =
   20 Crockford symbols, `XXXXX-XXXXX-XXXXX-XXXXX`; a single-symbol typo passes ~1 in 256). The host pastes it in its
   waiting room (`NetGame::addReply`): it punches toward both addresses every 150 ms for 10 s; the host's packets open
   its own NAT for the joiner's, the joiner's HELLOs (every 250 ms) get through, and the normal handshake runs. A punch
   heard back from another host address than the one tried (`NatTraversal::reached`) re-aims the joiner's HELLO there
   (`Session::retarget`, before the host's first answer only). A joiner that punches waits up to 90 s (time for the
   reply code to travel), then: "Couldn't connect: your networks block direct connections (strict / carrier-grade
   NAT). Try UPnP / port forwarding, or let the other player host."

Up to four PCs: the host's hub socket does the STUN and the punches (a star: every joiner punches to the host).
**One paste for the squad** (natpunch2, 2026-11-10): `findReplyCodes` finds every valid reply code in a block of text
(a group chat's copy: a run of 20 letters / digits, or neighbouring runs joined by `-` / spaces / tabs that make 20;
any case; only codes that pass the check, each once). Paste Reply Code / Ctrl+V hands the whole clipboard (up to
64 KB) to `NetGame::addReply`, which punches to every code not used yet in this hosting ("3 reply codes found:
punching..."; a repeat ignored quietly); pasting again adds the new ones. `OnlineMenu::focusGained` (the window's
SDL_EVENT_WINDOW_FOCUS_GAINED on the host's waiting room, not in automated runs) looks at the clipboard once and acts
only on valid new reply codes. The Reply Code rows stay while a seat is free. Tested: hotd_unit
`net.reply_codes_in_a_chat_paste` (a chat with names, times, 3 codes in three spellings, a repeat, a typo'd one, old
join codes, an address); three instances (the host with `--net-pcs 4 --net-wait 2` + 2 joiners, all behind
`--net-sim-nat`, one combined chat file read by `--reply-from`, which is re-read twice a second): every PC ended with
the same hash (the CTest harness runs pairs only, so this one is a script). **No relay**: two symmetric / carrier-grade NATs cannot be punched without a
relay server, and we host nothing (by the user's choice: no servers). Privacy: the logs say that STUN answered (and
how many servers), never the address; the codes contain it, and the UI says to share them only with friends. Every
`[net]` log line goes through `redactLog` (socket.h): a public IPv4 address becomes `public#XXXX` (a hash salted per
run: two players stay apart in one log), its port kept; LAN / loopback addresses stay readable; a join code holding an
internet address is not logged. `HOTD_NET_LOG_IPS=1` logs everything (developers).

Tests (no internet): hotd_unit `net` (`stun_messages`: the RFC 5769 section 2.2 sample response; `reply_code`: round
trips, the LAN classes, typos; `mock_stun_and_punch`: `StunMock` servers on 127.0.0.1, one answering 7 ports off (a
symmetric NAT detected), two sockets behind `--net-sim-nat`-style restricted NATs: the first one-way punch dropped, both
connect once both punch). CTest `module.online_punch`: two game instances, the host's mock STUN (`--stun-mock`), both
behind `--net-sim-nat`; the joiner reads the host's code (`--code-out` / `--join @FILE`), its direct try is dropped,
it writes its reply code (`--reply-out`), the host reads it (`--reply-from`), punches, and both play 3000 frames with
the same hashes. `module.online_punch_direct`: no simulated NAT: the direct try connects. Only two real homes can
test real routers (see "Verified / not verified").

## UPnP

`src/net/upnp.*`: Windows' own UPnP (the NATUPnP COM API: IUPnPNAT, IStaticPortMappingCollection). On for a host
bound to all interfaces: a background thread asks the router for UDP <port> -> this PC's LAN address ("HOTD ReWrithe
online co-op"), reads the internet address, and removes the mapping when hosting stops. A router without UPnP: the
waiting room says which port to forward. `--upnp dry-run` (the tests) sends nothing and maps nothing; `--upnp off`.

## Joining a running game / rejoining (protocol 4)

The game is deterministic, so a joiner needs no snapshot: it replays. The host keeps every frame's two inputs and its
restarts (a game over starts the chapter again) since the game's start. A host playing alone (Play Alone,
`--net-alone`) or waiting after player 2 was lost keeps its session open (a lost peer: a new session on the same port,
the host's own pending inputs carried over, the game back at the last confirmed frame). When a joiner's password is
right, the host **holds the game at frame F** ("Player 2 is joining...") and sends `SETTINGS` (the game's start
settings + `joinFrame` F + the log's size) and the **log** (`LOG` chunks of 1 KB, acknowledged with `LOG_ACK`; a run
length encoding: a 40-minute game is a few hundred KB at most, capped at 8 MB). The joiner starts the game from the
start settings and replays the log with sound and log lines off (~40,000 frames a second: Chapter 1 in under half a
second), its restarts included; both send `READY` with the state hash at F (another build / other files: refused, the
host goes on alone), then the lockstep (and rollback) continues from F; the joiner's first input-delay frames are
neutral on both. Player 2 then presses start (a mid-game join: like inserting a coin) or, on a rejoin, is simply back.
Not replayed: the music of the joiner's chapter until its next music cue (the replay is silent).

## Desync detection

Each PC hashes the work RAM after every confirmed frame (the sound command queue 0x512340-0x5123A0 left out: it differs
with the sound source and only feeds the sound) and sends the low 32 bits of its last 16 hashes in its input packets.
The first frame that differs is reported (`NET: desync at frame F`; the window stops with a message).

## Security

In short (for the README): everything between the two PCs is encrypted and authenticated with keys only the two
players with the password can make; a stranger who knows the address cannot join, read or change anything; a host
exposes one UDP port only while hosting (and the LAN announcement, which holds no secrets); stopping hosting (Stop
Hosting, leaving the game, closing hotd) closes the port and removes the UPnP mapping.

**Protocol 10 (worker "linux2", 2026-10-20): one crypto on every system, so Windows, Linux (Steam Deck) and macOS
play together.** The crypto is **Monocypher 4.0.3** (`src/third_party/monocypher/`, two files, CC0 / BSD-2-Clause,
audited by Cure53 in 2020), behind `src/net/crypto.h` / `crypto.cpp`. Only its documented high-level functions are used:
`crypto_x25519`, `crypto_elligator_map`, `crypto_aead_lock` / `crypto_aead_unlock` (XChaCha20-Poly1305),
`crypto_blake2b` / `_keyed` / the incremental BLAKE2b, `crypto_verify32`, `crypto_wipe`. Random bytes come from the
OS, not the library: `BCryptGenRandom` (Windows), `getrandom` (Linux), `arc4random_buf` (macOS), `getentropy` (other
BSDs). The old Windows-only CNG code (P-256, AES-GCM, our own hash-to-curve `p256.cpp`) is gone. A build of protocol 9
or older and a build of 10 refuse each other with "another version" (the HELLO keeps its 120-byte size and version
field for that).

- **CPace** (a PAKE; the CFRG draft's X25519 variant, with Monocypher's Elligator 2 as the map): the generator
  G = Elligator2(BLAKE2b-256(lv("CPace255") || lv(password) || lv("HOTD ReWrithe netplay v10"))) (lv = 2-byte length
  + bytes, as the draft's lv_cat). Each side picks a random 32-byte scalar s and sends Y = X25519(s, G) (X25519's
  clamping clears the cofactor). The shared secret K = X25519(s_a, Y_b) = X25519(s_b, Y_a) exists only for two sides
  with the same password. K all zero (a low-order point sent by an attacker) is refused, as the draft and Monocypher's
  manual ask (`crypto_verify32` against zero). An eavesdropper learns nothing to test passwords against offline; a fake
  host / client gets one guess per handshake (the host refuses everything after 20 wrong ones). The client makes one
  key pair per join attempt; the host a **fresh one for every new HELLO** (CPace's keys are one per session).
- **Keys**: BLAKE2b-512("HOTD ReWrithe netplay v10 keys" || K || transcript), transcript = the channel label, the
  client's nonce (16 random bytes) and Y, the host's nonce and Y (all fixed sizes). The first 32 bytes are the
  client -> host key, the last 32 the host -> client key (the derivation of Monocypher's `crypto_x25519` manual:
  hash the shared secret with both public keys). K, the scalars and the keys are wiped (`crypto_wipe`) as soon as
  they are not needed; the AEAD objects wipe their key when destroyed.
- **Packets** after the handshake: XChaCha20-Poly1305 (`crypto_aead_lock`), the 13-byte header (magic, type, 64-bit
  sequence number) as associated data, the 24-byte nonce = direction (4) || sequence number (8) || zeros (each
  direction also has its own key, so a nonce never repeats under one key). `crypto_aead_unlock` checks the tag before
  decrypting anything; a packet that fails is dropped and counted.
- **Authentication before use**: the host takes a joiner only when its CONFIRM opens with the derived key (version,
  "HOTD", the name); nothing in a sealed packet is looked at before it authenticates.
- Replay protection: the highest sequence number and a 64-packet window; duplicates and old packets are dropped
  after authentication (a cheap duplicate check first).
- Rate limits: at most 16 new handshakes a second per host (each costs two X25519); 20 wrong passwords lock the host.
- Never trust packet contents: size-checked before parsing (max 1200 bytes), every count / frame / float range-checked,
  malformed and unauthenticated packets dropped silently, nothing received is executed or used as an index without a
  bound; strangers' packets during a game are ignored. The LAN list's names are sanitised to printable ASCII.
- **Tests** (`hotd_nettest`, on every system): known answers from Monocypher's own test vectors (its release's
  `tests/vectors.h`, generated by its authors from the official vectors and libsodium; a selection in
  `src/net/crypto_kat.inc`): X25519 x 11 (with RFC 7748's), Elligator 2 map x 16, BLAKE2b x 12 (keyed / unkeyed,
  32 / 64-byte, long inputs), XChaCha20-Poly1305 x 10 (sealed through our `Aead`: cipher text and tag equal, opened
  back, every 7th byte flipped and the associated data changed: refused); CPace: the same password agrees, another
  password / channel / transcript does not, low-order keys (0, 1) refused, 200 codes; then the handshake, the wrong
  password, the lock after 20, the HELLO flood, the parsers' fuzz and 20,000 fuzz packets into a running game.
  Every test case takes a free port the OS picks (no fixed ports): two copies of the test, or a game hosting on the
  same PC, can run at the same time (fixed ports 47811-47830 used to fail now and then with "UDP port ... is already
  in use" on a busy PC).
- **Cross-play checked** (2026-10-20): a Linux (WSL) host and a Windows joiner, `--frames 40000 --autofire --mute
  --lives-kept 1 --players 2` over WSL's virtual network: both `result=ok`, 40,000 hashes compared, the same final
  hash as two Windows PCs (3ddee74c4007301f).
- Limits: short passwords (4-7 characters, allowed for LAN play and warned about) give an attacker on the internet more
  online guesses' worth of chance than the random 8 characters (40 bits, 20 tries); the join code holds the password
  (send it only to your friend). The map to the curve runs in constant time now (Elligator 2, no try-and-increment).
- Windows Firewall: the first host (and the first LAN list) asks whether hotd.exe may use the network. Allow **Private
  networks**. Tests bind to 127.0.0.1 only and never ask. Linux: no prompt (most desktop distributions and SteamOS
  have no inbound firewall on by default; with `ufw` / `firewalld` on, allow the UDP port); UPnP is Windows-only so far.

## Float determinism (the build)

Both PCs must run the same build. MSVC x64 with `/fp:precise` (stated in `CMakeLists.txt` for the game logic), no
`/fp:fast`, no `/arch:AVX2`: scalar SSE2, no FMA instructions, no contraction; GCC / Clang get `-ffp-contract=off
-fno-fast-math`. The game logic's math is the original's arithmetic plus `sqrt` (correctly rounded everywhere); the
CRT's `sin` / `atan2` ... are only used by the view. The UCRT's FMA3 paths are switched off in netplay
(`_set_FMA3_enable(0)`).

## Input determinism, CPU teammates (worker "inpolish", 2026-10-20)

The rule: during play nothing reaches the game logic except through `NetGame::applyInputs` (each frame's lockstep
inputs). Everything a PC decides by itself (mouse, keys, pads, the clock, its own settings) goes into its `Input`
in the sample, once per input frame, and every PC runs the frame with the same inputs. The audit:

- **Keyboard aim** was only applied in local games; online the aim keys did nothing. Now the sample moves this PC's
  guns by the held keys first (player 1's keys always, player 2's with Player 2 input = keyboard; in the 4P game on two
  PCs player 2's keys move the second seat), then converts the guns to the sent aim: deterministic like the mouse.
  Player 2's start key in the 4P game on two PCs is the second seat's start (as a second pad's).
- Fixed with it: with up to four PCs (one seat each) the sample read the second-seat slot first and cleared player 2's
  keyboard fire / reload before this PC's own seat read them (Player 2 input = keyboard fired nothing).
- **CPU teammates** were seeded from the clock (`SDL_GetTicksNS`) and off online. Now seeded from the seats and levels
  (`Bot::teamSeed`): local runs with CPU seats repeat exactly (two 20000-frame 4P runs with three CPUs: the same output).
  Online they **go through the lockstep**: the PC that owns a CPU seat asks the bot once per input frame
  (`Bot::seatInput`: the gun and the start button returned, the game untouched; `player_join` only through the input's
  start bit) and sends the result as that seat's input. The bot's own state (its generator, its targets) lives on that
  PC only and is never re-asked by rollback (re-simulated frames re-use the inputs sent), so no snapshot of it is
  needed and other PCs never run it. Seats: the 4P game on two PCs: this PC's second seat (its `Input` second fields);
  up to four PCs: the host fills the free seats (`NetGame::hostSeats`, in its own frame; `relay()` keeps a seat's
  value while no PC's input counts for it, the host's prediction uses it too). A joiner replays the log, CPU inputs
  included. The HUD marks a CPU seat only while it is free.
- **Name entry from the keyboard** (`Runtime::name_entry_key`) went straight into this PC's game: off online (the
  letters are shot, as in the arcade).
- **Boss Mode** chosen before going online could start one PC's game in Boss Mode (the start check refused it): never
  online now.
- Checked, fine: the pads' / light guns' start buttons and the mouse's middle button are this PC's start bit; the pause
  only stops this PC's ticks (the others wait); Auto reload is decided in the sample; PC Mode, the cheats, `--start-at`,
  `--lives-out` are local only; the game's own random numbers are in the work RAM (`rand_state`); the sound board's
  command queue (the only state the sound changes) is left out of the hash and read by nothing else. Testing-only
  environment variables (`HOTD_KEEP_LIVES`, `--name-keys`) must be the same on both PCs.
- Test: `HOTD_NET_KEYAIM_TEST=F:E` (a window online game): player 2's first Aim Left key held for 60 frames from F
  (injected key events), the window's game ends at E; both PCs print player 2's gun at F and F + 90.

## Verified / not verified (2026-10-20)

All on one PC over 127.0.0.1 (headless, or hidden windows: `HOTD_WINDOW_DISPLAY=0`), each instance its own
`HOTD_PREF_DIR` (`tools/netplay_test.ps1`):

| Test | Result |
|---|---|
| `hotd_nettest`: crypto, CPace (200 codes as generators, same code = same key, other code = other key), LAN announce / list, 200,000 random announcements, share code (round trip, a typo refused), settings decoder on 1,000,000 random blobs, right / wrong password, 3000 frames at 20 % loss, 20000 fuzz packets | all passed |
| lockstep, 1 player, `--frames 40000 --autofire --lives-kept 1` (phase 1) | chapter 1 ended at 16185 on both, hashes equal |
| rollback 8, 2 players, no loss (`-HostArgs "--players 2"`) | chapter 1 ended at 15810 on both, chapter 2 at 35268, final hash 3ddee74c4007301f on both (2026-10-20, main 4bd3041: unchanged since 5004868) |
| the same without `--players 2` (the script's default: a 1-player game, player 2's PC only watches) | chapter 1 ended at 16140 on both, chapter 2 at 36710, final hash 24f26acba90d219d on both (also unchanged since 5004868; not a regression, a different game) |
| rollback 8, 2 players, `--net-sim 10,30` both ways (10 % loss, 62 ms round trip) | 614 rollbacks, 4742 frames re-simulated, chapter 1 ended at 15342 on both (as the lockstep run), hashes equal |
| `HOTD_ROLLBACK_PROBE=37`, 2 players, 40000 frames (chapters 1-2) | 4337 snapshot / restore / re-run probes, all the same |
| adaptive delay | loopback: 2 frames; 62 ms round trip: 3 frames (2 with rollback) |
| LAN list: `--join "lan:Kitchen PC"` | found on the (loopback) LAN, joined, hashes equal |
| share code join (`--join HOTD-...`) | joined, hashes equal |
| join code join (2026-10-26: the host's printed code, typed lower case without dashes) | joined, hashes equal |
| pause (2026-10-26: `HOTD_PAUSE_AT=3000:300` on the host, then on the client) | the other prints "P1 PAUSED" / "P2 PAUSED", hashes equal |
| the menu flow (two hidden windows, `HOTD_ONLINE_TEST`): host from Online Co-op, a wrong password refused, a share-code join, Start, 1500 frames with rollback | hashes equal (26 rollbacks on the host) |
| UPnP `--upnp dry-run` | "would map UDP 47924 to 192.168.0.176:47924; nothing was sent" |
| a byte changed on one PC at frame 5000 (with rollback) | both report "desync at frame 5000" |
| wrong password, stranger datagrams, `--net-fuzz 50000`, a client killed mid-game | refused / dropped / the host reports the lost connection (phase 1) |
| join a running game (`tools/netplay_join_test.ps1 -Mode join`): the host alone at 1000 frames/s, a client after 4 s, P2 presses start at 9000 | joined at frame ~3900 (log ~2.4 KB, caught up in ~80 ms), "player 2 joins" at 9000, chapter 1 ended at 16380 on both, final hashes equal |
| rejoin (`-Mode rejoin`): 2 players, the client killed after 7 s, a new one 9 s later | host back to the last confirmed frame (3746), the new client caught up in 111 ms, chapter 1 ended at 15810 on both (as without the drop), final hashes equal |
| quick rejoin (`-Mode rejoin -RejoinMs 2000`, 2026-10-27): the new client 2 s after the kill | "player 2's PC is joining again ... its seat back at once", back in at frame 3657, chapter 1 ended at 16936 on both, final hashes equal (was: "This game is full") |
| quick rejoin, up to four PCs (`netplay4_test.ps1 -KillMs 8000 -RejoinMs 2000`; and `-Joiners 2 -C1Args "--net-seats 2"`) | c1b gets seat 2 back (the two-seat PC: seats 2 and 4), SYNC PASS on 4 / 3 PCs |
| `hotd_nettest`: a PC back 1.5 s after it dropped (another port) | not refused, the host's `rejoinAsked`; a live PC's game still "full" |
| leave (`netplay_join_test.ps1 -Mode leave`, 2026-10-30): the client leaves cleanly at frame 3000 (`HOTD_NET_QUIT_AT`), a new one 9 s later | "player 2 left the game (closed or quit)", back in at frame 3002, chapter 1 ended at 15631 on both, hashes ca14e40c69459616 (was: the host stopped: "The other player left") |
| continue (`-Mode continue`: host `--net-drop 3`) | the host waits 3 s, goes on alone from frame 3002 to 20000 |
| menu flows (`netplay_menu_test.ps1 -Mode rejoin`): lan-open (one click, no code), rejoin-close (the window closed at 1200, a new menu joiner with the code), rejoin-kill (killed, back by one LAN click) | PASS, all 29c07e646e8dae3b at 3000 frames |
| 4 PCs (`netplay4_test.ps1 -QuitAt 3000 -RejoinMs 4000 -HostArgs "--net-drop wait"`; `-KillMs 6000 -RejoinMs 2000`; `-QuitAt 3000 -HostArgs "--net-drop 3"`) | the game waits, P3 / P4 show "P2 left: the host waits for them or goes on"; SYNC PASS 604ed7f9d8b2db42 on 4 PCs (both rejoins); going on: SYNC PASS on 3 PCs |
| the menu: Play Alone, a window joiner 6 s later (`HOTD_ONLINE_TEST=host-alone`) | joined at frame 878 (caught up in 17 ms), 2400 frames, hashes equal |
| the text launcher: Host a Game > Start Hosting > Play Alone (`launcher-alone`), a headless joiner | joined at frame 390, 2400 frames, hashes equal |
| the controller's letter picker through the event queue (`edit-name`: Up Up Right Down Right Up Left Enter) | the name "BA" |
| single player unchanged | `--frames 40000 --autofire --mute --lives-kept 1` (and `--models pc`): chapter 1 ended at 16695; `--input-test`: 865 checks, 0 failed |
| keyboard aim online (`HOTD_NET_KEYAIM_TEST=600:1500`, a hidden window joiner with `--p2-input keyboard`, a headless host, `--net-sim 10,30` both ways) | player 2's gun x 245.4 -> -82.5 on both PCs, 1500 frames, hashes equal (2026-10-20) |
| CPU seats, 4P on two PCs (`--players 4`, host `--seat-bot 3:2`, joiner `--seat-bot 4:3`) | chapter 1 ended at 14676 on both (14894 in another run: the bots decide on this PC's live state, so a run is not the same twice online, only equal on both PCs), hashes equal; with `--net-sim 10,30`: 3316 rollbacks, 24226 frames re-simulated, hashes equal |
| CPU seat, up to four PCs (`netplay4_test.ps1 -HostArgs "--seat-bot 4:2"`, the third joiner 5 s late) | the CPU joins seat 4 at frame 36, the late PC takes it over at frame ~14000 (the log with the CPU's inputs replayed), all four hashes equal; without the late joiner (3 PCs) the CPU plays seat 4 to the end, hashes equal |
| CPU teammates locally (`--players 4 --seat-bot 2:2 --seat-bot 3:1 --seat-bot 4:3`, 20000 frames, twice) | the same output both times (was: seeded from the clock) |

Needs two real PCs (the user's 4 PCs): the Windows Firewall prompt and the LAN broadcast across the real network
(Wi-Fi <-> cable), UPnP against a real router (mapping, the internet address, removal), a real internet connection
through NAT, CPUs of different makes (the determinism safeguards), people playing with mice (rollback's feel: the other
crosshair's corrections), text entry in the menu (typing, Ctrl+V, the clipboard's share code), a real controller in the
menu (the letter picker was tested with key events; the pads' own d-pad -> key mapping is the pads code's), a rejoin
after a real network drop (Wi-Fi off and on). Four seats on two PCs: docs/coop4.md section 11 (protocol 7); up to four
PCs with one or two seats each: below (protocols 8, 15; tested on the loopback only). Not done: IPv6, the joiner's
chapter music before its next cue.

## Four PCs (protocol 8, 2026-10-20)

`--net-pcs 4` (the online menu's Host page: "4P Online: Up to 4 PCs") makes the host take up to three joiners, one
seat each (P2, P3, P4 in join order). The host's session is a hub that owns the UDP socket and hands each packet to its
seat's session (each its own CPace handshake and keys, the same password); a new HELLO goes to the first free seat,
none free: busy. Each joiner sends its one seat's input; the host relays frame f to every PC once its own and every
joined seat's input of f are in (a free seat: neutral), as full four-seat frames (36 bytes a frame). Rollback predicts
the relayed frame; the state hashes go between the host and each PC (all equal the host's). WELCOME and the settings
carry the joiner's seat; ROSTER (every 500 ms) lists each seat's PC and its ping for the waiting room. A free seat (never
joined, or its PC silent 6 s / gone) can join the running game: the host waits until every frame run is confirmed,
sends the log, and the seat's inputs count from the next frame not yet relayed. The LAN list shows n / 4 and lists the
game as joinable while a seat is free. `--net-wait N`: the command line's host starts once N PCs joined.
Two-PC games (2P, 2 x 2 seats) are unchanged on the wire apart from the version and the two new settings bytes.
Test: `tools/netplay4_test.ps1` (`-LateMs N`: the third joiner N ms later, i.e. mid-game).

## One or two seats per PC (protocol 15, 2026-10-27, worker "net4pc")

Up to four PCs, each with one or two players: 1+1+1+1, 2+1+1 (the two-seat PC host or joiner), 2+2, 1+1 (+ free
seats / CPU teammates). Host page: "4P Online: Up to 4 PCs", **Your Seats 2** (`--net-seats 2`): the host plays P1 +
P3, its Player 2 input plays P3. Join page: **Your Seats 2** (`--net-seats 2`): the joiner asks for two seats.

- **Wire** (two-player and two-PC games byte-for-byte as before, apart from the version): a joiner wanting two seats
  adds one byte (2) to its CONFIRM (a two-PC host ignores it). The host (`NetGame::takePair`) gives it its own seat (the
  first free one, as before) and the seat across (P2 + P4, P3 + P1 ...) when free, else any free seat, else one seat
  only (the waiting room asks again while it lasts; "asked for two seats; only one was free" at Start). That second
  seat's own session is **reserved** (the hub sends no new joiner there). The settings' seat byte carries the second
  seat in its high nibble (0 = none, as before; the decoder refuses it in a two-PC game or equal to the first). The
  two-seat joiner's INPUT is 18 bytes a frame (its second seat in the second fields, as the two-PC 4-seat game); the
  host's seat session reads it so (`setPcSeats(2)`). The relay puts both seats into the four-seat frame; the relayed
  frames, the log, the hashes and rollback are as before (every PC runs the same four seats).
- **ROSTER**: a PC's second seat has the seat of its first + 1 in the in-byte's high nibble (`RosterSeat::with`); the
  waiting rooms show it as "Rogan (you), P1's PC". Previews: `HOTD_FE_ONLINE=hosting4-mix` / `joining4-mix`.
- **Pause**: a two-seat PC's pause marks both its seats (only up to four PCs; two-PC games as before).
- **Dropping out**: a silent (6 s) or leaving PC frees its seat and its second seat (`renewSeat` / `dropPair`); the
  others play on (free seats: neutral, or the host's CPU teammates) once the host goes on (2026-10-30: the game first
  waits for the host's choice in a window game; "A player left"). It joins the running game again with the same
  code (the log replay) and gets the first free seat and its pair again; joining again within the 6 s frees its
  seats at once (a full game's only free seats: its own). The seats in at the start: the first seats
  with a PC up to the first free one (`hostStart`); a later PC's seat (e.g. the host's P3 with nobody on P2) presses
  start like a mid-game join.
- Also fixed: the window drew the other PCs' crosshairs from `1 - localPlayer` (a joiner on seat 3 / 4 of up to four
  PCs read outside the guns); now every seat that is not this PC's comes from the frame's inputs.

Tests (127.0.0.1, headless, each PC its own HOTD_PREF_DIR, `--frames 40000 --autofire --mute --lives-kept 1`, rollback 8;
`tools/netplay4_test.ps1`: `-Joiners N`, `-C1Args "--net-seats 2"`, `-KillMs` / `-RejoinMs`, the last line SYNC PASS / FAIL):

| Test | Result |
|---|---|
| 4 PCs x 1 seat | chapter 1 ended at 17455, final hash c23c9c7988f8218a on all four (the same as before the change) |
| 2+1+1 (host `--net-seats 2`), 1+2+1 (c1 `--net-seats 2`: seats 2 + 4), 2+2 (both) | all PCs result=ok, the same final hash c23c9c7988f8218a as 4 x 1 (every seat plays the same) |
| 2-PC 2-seat (`netplay_test.ps1 --players 4`) / 2-player online | d84f917c2904342f / 04075633af26e8b9 on both PCs: the same as the build before |
| 4 x 1 at `--net-sim 10,30` on every PC; 2+1+1 the same | ~1700 rollbacks / ~13,000 frames re-simulated per PC, all hashes c23c9c7988f8218a |
| a joiner mid-game (`-LateMs 5000`, host `--net-solo-fps 3000`) | P4 joined at frame 10964 (log 21.6 KB), all four result=ok, hashes equal |
| one PC killed and rejoined (`-KillMs 4000 -RejoinMs 8000`): 4 x 1; and 1+2+1 with the two-seat PC killed | seat 2 freed at frame 6873, back at 12149, 4 PCs hashes equal; the two-seat PC freed both seats at 6083, got seats 2 + 4 back at 10184, hashes equal |
| one PC leaving for good (`-KillMs 4000`) | freed at frame 7478; the other three play to 40000, hashes equal |
| `hotd_nettest` (new: the second seat in the settings and the roster, refusals; a hub with a two-seat joiner: CONFIRM's wish, the reserved seat, "full" for a fourth PC, 600 relayed frames at 10 % loss with both seats whole, hashes on both links) | all passed |

A rejoin is refused ("full") while the host still counts the dropped PC as in (6 s of silence); a moment later it works.

## PC Mode online (protocol 16, 2026-10-27, worker "onlinepm")

PC Mode (docs/pcmode.md) is offered online: two PCs, two PCs with two seats each, and up to four PCs.

- **The host decides**: Host page **Game Mode: Arcade / PC Mode** (`--pc-mode` on the command line), **Your
  Character** (and **P3 Character** when the host plays two seats). Each joiner picks its own character on its PC (Join
  page **Your Character**, **2nd Character** with Your Seats 2; `--net-pc-char a[,b]`); Auto (0) = the seat's default
  Rogan / G / Rogan 2 / G 2. The host's waiting room shows each seat's character as the joiners send it; **Start** fixes
  them (`NetGame::seatChar` / `hostStart`).
- **Wire**: CONFIRM is now version, "HOTD", the name, the seats wanted (1 / 2: always there, before an optional
  byte), the joiner's two characters (0-6). The settings gain 7 bytes after enemy4p: pcMode, pmChar[4] (1-6 each in
  PC Mode, all 0 in an arcade game; anything else refused), pmContinues (0-9), pmFreePlay. ROSTER gains a character
  byte per seat (0xFF: an arcade game), 80 bytes. An arcade game's settings, inputs, log and hashes are the same as
  before apart from the version (and those zero bytes).
- **The game**: every PC starts with the settings' PC Mode, characters, Continues and Free-Play (`enterGame`: not the
  PC's own Options), the host's Auto Reload (`pm_auto_reload`). Game Speed: Auto resolves on the host (PC Mode: 60 Hz,
  the arcade's 57.52 Hz otherwise) and both PCs tick at that rate; a host's explicit Arcade / 60 Hz is kept.
- **Desync check**: `NetGame::gameHash` (and the start's `bootHash`) adds PC Mode's between-frame runtime fields (the
  ones rollback's `RuntimeState::Pm` keeps) when PC Mode is on; arcade games hash the work RAM alone, as before.
- **Joining a running PC Mode game** (and rejoining): the joiner replays the host's log from the start settings; since
  part 2 (below) its own pick applies from its first start.
- Boss Mode: online since protocol 18 ("Boss Mode online" below). Chapter Select: the host's chapter already travels
  in the settings (both modes).

Tests (127.0.0.1, headless, each PC its own HOTD_PREF_DIR, `--frames 40000 --autofire --mute --lives-kept 1`, rollback 8):

| Test | Result |
|---|---|
| 2 players PC Mode (host `--pc-mode --net-pc-char 5`, joiner `--net-pc-char 6`: Sophie / Researcher) | chapter 1 ended at 16314, 40000 frames, 541 rollbacks, hash 1fea119a7f1c83f0 on both |
| the same with `--net-sim 10,30` both ways (Rogan 2 / G) | 1479 rollbacks, 10,956 frames re-simulated, hash b20a1e6203e299f1 on both |
| mid-game join (host alone `--net-solo-fps 1000`, a joiner after 4 s, start at 9000; 20000 frames) | joined at frame 3896 (log 2.3 KB, caught up in 88 ms), hashes equal |
| rejoin (2 players, `--net-sim 0,15`, the joiner killed after 7 s, a new one 9 s later; 20000 frames) | back in at frame 3735, hashes equal |
| 4 PCs PC Mode (`--net-pcs 4 --net-wait 3`, picks 4 / 5 / 6 / Auto) | characters 4, 5, 6, 4 on every PC, hash 6be9bcfe5da4c6ad on all four |
| arcade 2 players / 4 PCs (unchanged?) | 04075633af26e8b9 / c23c9c7988f8218a: the same as before |
| `hotd_nettest` (new: the joiner's characters in CONFIRM, the PC Mode settings and roster round trips, a pick-less joiner = 0, every out-of-range byte refused) | all passed (its stack is now 8 MB: main holds many Sessions) |
| the pages (`HOTD_FE_ONLINE=<scene>-pc`: host-pc, hosting2-pc, hosting4-mix-pc, joining2-pc, joining4-pc) | the rows fit (one page) |

### PC Mode online, part 2 (protocol 17, 2026-10-27, worker "onlinepm2")

- **A joiner's own character in a running game.** An input's flags gain bits 4-6 (`Input::kChar`): with a start, the
  seat's PC Mode character (1-6; 0 = none: an arcade game, Auto; 7 and bit 7 refused like any unknown flag:
  `Input::flagsOk`). `NetGame::picked` puts this PC's picks (`--net-pc-char a[,b]`, the pages' characters) into its
  own seats' starts in the sample, PC Mode games only; `applyInputs` gives a seat that is **waiting** (player state 9:
  never joined, or its game over) that character before the start runs (PlStartInit / the 4P seats' join read it). A
  continue (state 4) keeps the character. It is a frame input, so the log replay, rollback's re-runs and every PC do
  the same at the same frame; the seats in from the start already have their picks from the settings (the bits change
  nothing there). Arcade games send no bits: inputs, logs and hashes as before.
- **The end: name entry and the table on each PC.** A PC Mode game over or the game's end (the same confirmed frame on
  every PC: `settle` first) ends the online session there (`NetGame::finish`: the final hashes compared, as at Esc >
  Exit), then each PC files the rows of **its own seats** (`pcModeFinish(rt, pmOwnSeats())`: the PC's player and its
  second seat; CPU seats and the other PCs' players not) into **its own** PC Mode table (pcmode_scores.txt in its
  settings folder): the menus' name entry (screen 15) for each row that goes in, then the table. The entry is after
  the session, so nothing of it is lockstep. A command-line online game (no menus) files them as ANONYMOUS and ends;
  headless runs use HOTD_PM_NAME. Arcade online games restart the chapter on a game over, as before.
- **Continues**: the host's Continues / Free-Play (protocol 16's settings bytes) are the one credit pool of the shared
  game (`pm_credits`, in the hash and the rollback snapshot), so every PC sees the same credits: a continue or a
  mid-game join takes one (the PC's rule: a join needs a credit, or Free-Play), a CONTINUE? countdown restarts at 9 when
  the pool changes, a player out of lives with no credits left is out at once. All from the game's state and the frame
  inputs: alike on every PC. Each PC's own Continues / Free-Play are not used (nor changed) by an online game.
- **launcher.ini**: the Host page's Game Mode (`netpcmode=0/1`) and this PC's characters (`netpcchar=a,b`: first /
  second seat, 0 Auto; the Host and Join pages share them) are kept; `--net-pc-char` wins. Test:
  `HOTD_ONLINE_TEST=pc-picks` (prints the loaded values, toggles Game Mode and two picks, saves, prints them).
- Headless online PC Mode runs print "frame N: P<n>'s PC Mode character is now c", "PC Mode credits n" and each seat's
  "out of lives (CONTINUE?)" / "in play" / "game over" (diagnostics: a print may come a few frames later on one PC
  after a rollback; the hashes are what is compared).

Tests (127.0.0.1, headless, each PC its own HOTD_PREF_DIR, muted):

| Test | Result |
|---|---|
| mid-game join with a pick (`netplay_join_test.ps1 -HostArgs "--pc-mode --net-pc-char 5" -ClientArgs "--net-pc-char 6"`: host alone, joiner after 4 s, start at 9000) | joined at frame 3901, "P2's PC Mode character is now 6" at 9001 on both, 20000 frames, hash a864bbad188aa1f5 on both |
| 4 PCs PC Mode, c3 joins 5 s late with `--net-pc-char 6 --start-at 4:20000` (`netplay4_test.ps1 -LateMs 5000 -C3Args ...`) | P4 plays the Researcher (6) from its start at 20000 on all four, credits 5 -> 4 (the join's credit), SYNC PASS, hash 7c4f37d13834ff77 |
| 2 players PC Mode to the game over (no lives kept; Rogan 2 / Researcher; HOTD_PM_NAME HOSTA / JOINB; each settings folder a table of low scores) | game over at 12075 on both, hashes c16f074661b15086 equal at the end; host's table: HOSTA 2360 Rogan 2 first, joiner's: JOINB 5600 Researcher first (each PC only its own player) |
| the same as two hidden windows (command line, no firing) | both end the session at 3174 (result=ok, hashes equal), exit 0 |
| continues: host Continues 2, joiner's own 9 (ignored); P2 continues at 1700, P1 at 2400 | credits 2 -> 1 -> 0 on both, P2 out at once when the last credit went, game over at 3044, hashes equal; Continues 0 + the host's `--pc-free-play`: both continue, credits stay 0, hashes equal |
| launcher.ini (`HOTD_ONLINE_TEST=pc-picks`, twice) | loaded 0 / 0,0, saved 1 / 2,1; then loaded 1 / 2,1, saved 0 / 4,2 |
| 2 players PC Mode 40000 frames (Sophie / Researcher), arcade 2 players / 4 PCs | 1fea119a7f1c83f0 (as before part 2); 04075633af26e8b9 / c23c9c7988f8218a (unchanged) |
| `hotd_nettest` (new: the character bits accepted / refused; the lockstep test's inputs carry characters 0-6) | all passed |

## Boss Mode online (protocol 18, 2026-10-27, worker "bossnet")

Boss Mode (docs/teaser_status/bossmode.md) is offered online: two PCs, 4P on two PCs, up to four PCs.

- **The host decides**: Host page **Game Mode** Arcade -> PC Mode -> **Boss Mode**, then **Boss** (Chariot, Hangedman,
  Hermit, Magician, All Bosses); `--boss-mode <boss>` with `--host` on the command line. The joiner's waiting room shows
  "Game Mode: Boss Mode: <boss>" (ROSTER's new last byte). Game Speed Auto: the arcade's rate (as offline Boss Mode).
- **Wire**: the settings gain a `bossMode` byte after pmFreePlay (0, or 2-6; 1 / 7+ / with pcMode refused); ROSTER is
  81 bytes (the host's Boss Mode last). Arcade and PC Mode games: inputs, logs and hashes as before (only the version).
- **Lockstep-safe**: `BossMode::tick` (the clock, the win, All Bosses' next boss with its new chapter_start, the end)
  runs at the end of `Runtime::frame` after the walk-up, i.e. inside the game step that rollback re-runs and a joiner
  replays (it used to run in main's loop after the frame, outside rollback). The clock's step is `BossMode::tickMs`:
  a local game sets it before each frame (the tick's real time with Sonic Mode, as before: offline times unchanged),
  online it is fixed at the start to 1000 / the game's tick rate (the clock counts game ticks, alike on every PC).
- **Snapshot / hash**: `RuntimeState` keeps the BossMode struct (mode, boss, phase, result, clock, its step, the
  per-frame flags) and the logic's boss_mode / boss_next / boss_events / bossNoContinue / startAtSection0;
  `gameHash` / `bootHash` add them (`hashBoss`) and PC Mode's maths fields (a boss game runs pc_path) when Boss Mode is
  on. `HOTD_BOOT_HASH_PARTS=1` (developer) prints the parts of a start state's hash (which part a "came out different"
  join differs in).
- **The end**: a boss game's end (result 1 or 7: `phase == Done`) is acted on only once settled (rollback may take it
  back); then the session ends (`finish`: the final hashes compared) and each PC files the shared time in its **own**
  bosstimes.cfg with its own name entry (the menus; a command-line window: ANONYMOUS; headless: HOTD_BOSS_NAME), as
  online PC Mode. No start joins in a boss game (THOTD.EXE reads no start in state 4); a dropped PC can rejoin.
- Tests: `HOTD_ONLINE_TEST=host-alone-boss` (the menu host plays the Chariot alone; a joiner can join the running game).

Tests (127.0.0.1, headless, each PC its own HOTD_PREF_DIR, `--frames 40000 --autofire --mute --lives-kept 1`, rollback 8,
after merging main 1b61ad63):

| Test | Result |
|---|---|
| 2 players, each boss (`--players 2 --boss-mode chariot` ...) | Chariot 00:21:93, Hangedman 00:55:83, Hermit 00:21:43, Magician 02:05:46; each result=ok, hashes equal (20-120 rollbacks each) |
| 2 players All Bosses | 03:57:03, 16319 frames, hash 8bf592848abc3ba0 on both (~300 rollbacks) |
| the same with `--net-sim 10,30` both ways | ~1150 rollbacks, hash 8bf592848abc3ba0 (the same game) |
| rejoin in All Bosses (`netplay_join_test.ps1 -Mode rejoin -HostArgs "--boss-mode all"`) | back in at frame 3322, hash 8bf592848abc3ba0 on both |
| mid-game join (host alone, `-Mode join -HostArgs "--boss-mode all"`) | joined at 3935, hashes equal |
| 4 PCs All Bosses (`netplay4_test.ps1 -HostArgs "--boss-mode all"`) | SYNC PASS, all four 68abe2789db28d08 |
| 2 players All Bosses without --lives-kept | game over in the Hermit fight on both at the same frame (result=gameover 02:42:03), hashes equal |
| menu host (hidden window, `HOTD_ONLINE_TEST=host-alone-boss`), headless joiner 15 s later | joined at 738, the Chariot's game over on both, the session ended, hashes equal |
| arcade 2P / 4 PCs / 2P PC Mode vs main's own build (1b61ad63) | 060c037658d73c70 / d39d8480dcd11d00 / 3ceccf47f72e5133: the same |
| offline Boss Mode vs main's build (each boss, All, 4P All) | the same times and frames (Chariot 00:25:53 at 2725); soak `-Only boss`: 8 runs, 0 failed |
| `hotd_nettest` (new: Boss Mode settings / roster bytes, refusals, over the wire) | all passed |

## Survival online (protocol 22, 2026-11-09, worker "survnet")

Survival Mode (docs/survival.md) is offered online: two PCs, 4P on two PCs, up to four PCs (2-4 players). The Typing of
the Dead 0 stays single player (never online); Horde is not online.

- **The host decides**: Host page **Game Mode** Arcade -> PC Mode -> Boss Mode -> **Survival**, and in a dev build
  **Survival Arena** (a public build: Chapter 1's courtyard only, `survivalArenaCount()`; `OnlineMenu::survivalArenas`);
  `--survival <arena>` with `--host`. The joiner's waiting room shows "Game Mode: Survival: <arena>" (ROSTER's last
  byte 0x10 + arena). `HOTD_ONLINE_TEST=host-alone-survival`: the menu host plays the courtyard alone.
- **Wire**: the settings gain 10 bytes after bossMode: `survival` (0, or the arena + 1; refused with PC Mode / Boss
  Mode), `survivalSeed` (4), `survivalCap` (1-64), `survivalCut`, `survivalSmgWave`, `survivalWeaponsWave`,
  `survivalTest` (bit 0 the test buyer, bit 1 the weapon cycle). `netApply` puts them into the joiner's Survival options
  (saved and put back after the game, `NetSaved`). An input **without its start** carries the Survival extras in the
  flags' bits 3-7 (where a start carries its character): `Input::kHeld` (bit 3: the trigger held, the automatic
  weapons) and `kWeapon` (bits 4-7: 0 none, 1 previous, 2 next, 3-8 that weapon, 9 the light gun's auto best, 10 / 11
  auto best + previous / next); `flagsOk` takes codes 0-11. Only a Survival game sends or reads them
  (`NetGame::applyInputs` sets `Runtime::gun[s].held`, `weapons.request[s]`, `weapons.autoBest[s]`); the prediction
  keeps the held bit (an SMG held is not mispredicted every frame). Every other game's inputs, settings meaning and
  hashes are as before (only the version).
- **Local writes removed**: the wheel, the number keys and a pad's shoulders wrote `rt.weapons.request` straight from
  the window's events (this PC only: a desync online); online they go into `gNetWeapon` and the next input.
- **Snapshot / hash**: `RuntimeState` copies `Runtime::survival` and `Runtime::weapons` whole (and the lead-in's music
  code); `gameHash` / `bootHash` add `SurvivalMode::digest()` and `Weapons::digest()` (the logic fields, not the texts:
  the banner is translated per PC; not the music code: only kept with sound, a joiner's catch-up replays silently).
- **The end**: the game's end (phase Over) is acted on only once settled; then the session ends (`finish`) and each PC
  files the points in its own survival_scores.txt. `--survival-test N` online: the same rule.
- Testing: headless `--autofire` in Survival plays autofire()'s Survival rule through the input (`netAutofire`: the
  signs shot, the reload by hand, the SMG held); `HOTD_NET_SURVIVAL_SWITCH=N` switches weapon every N frames through
  the input.

Tests: see docs/survival.md "Online co-op" (2 PCs, 4P on two PCs, 4 PCs, with `--net-sim`, mid-game join, rejoin; 0
desyncs; a control run without the snapshot desyncs at frame 124). Story online unchanged: 2 players 40000 frames
8543b41a4be0f433 with this build and with main's. `hotd_nettest`: the Survival settings / roster / input extras, the
refusals, over the wire: all passed.

## Same game data (protocol 19, 2026-10-27, worker "netdata")

Since pmbody, PC Mode and Boss Mode read two things from the 1998 PC files when the PC game folder is set:
CAM\ALLSTAGE\CAMALL.RBH's camera paths and THOTD.EXE's motion table (0x5EFBD0, 0x2000 bytes); without them they use
the ROM's camera keys. Two PCs where one has the PC files and the other not would play different games. Before, this
showed only once the game had started ("the games are different at the start", the READY boot hash) with a generic
message; a different copy of the PC files that left the start state alone would have desynced later.

- **The join carries the data's digest.** Each PC computes two 64-bit digests (main.cpp `pmData` / `romDigest`):
  `pcData` over exactly the bytes the logic reads (the camera words + the motion table; 0 = no PC data), `romData`
  over the arcade regions the logic reads (maincpu, main_data, copro_data; 0 = not known). The joiner sends both at
  the end of its sealed CONFIRM (+16 bytes; protocol 19). The menu fills them before Host / Join
  (`OnlineMenu::gameData`), the command line before `connect`. Each PC logs `game data: ROMs ..., PC files ...`.
- **The host refuses a mismatch** with REFUSE 5-8 and logs `refused <addr> (<name>): <reason>`; the joiner shows:
  5 "The host's arcade ROMs are different from this PC's ..." (any game); in a PC Mode / Boss Mode game only:
  6 "The host uses the 1998 PC files for PC Mode / Boss Mode; this PC doesn't have them ...", 7 "This PC uses the 1998
  PC files ...; the host doesn't have them ...", 8 "The host's 1998 PC files are different from this PC's (another
  THOTD.EXE or CAMALL.RBH) ...". An arcade game does not compare the PC data (it reads none). The host remembers the
  refused address and answers its repeated CONFIRMs again (a lost REFUSE), two-PC host and four-PC hub alike; the
  seat stays free. Headless: `NET: result=refused (...)`.
- **Why refuse rather than fall back to the common subset** (both PCs on the ROM's keys when one lacks the PC files):
  the fallback would silently make the host's PC Mode differ from its own offline PC Mode (and from the 1998 PC the
  mode is proven against), and a joiner arriving in a running game cannot change the data the host is already
  playing with. A refusal at the join is deterministic, costs nothing in play, and tells the player what to fix.
- Host page: **Boss** (and Game Mode Boss Mode) is saved now too: launcher.ini `netbossmode=` (0, 2-6), like
  `netpcmode=`; `HOTD_ONLINE_TEST=boss-picks` prints it loaded / saved.

Tests (127.0.0.1, headless, `--frames 40000 --autofire --mute --lives-kept 1`; "none" = `--pc none`, "other" = a copy of
the PC files with one byte of CAMALL.RBH changed):

| Test | Result |
|---|---|
| PC Mode 2P, both `--pc C:\Games\HOTD` | result=ok, hash df92cbe6c2ea56c3 on both (= main's own build) |
| PC Mode 2P, both the developer default PC files | df92cbe6c2ea56c3 on both |
| PC Mode, host PC files, joiner none (main's build: "the start states differ") | refused at once: "The host uses the 1998 PC files ...; this PC doesn't have them" |
| PC Mode, host none, joiner PC files | refused: "This PC uses the 1998 PC files ...; the host doesn't have them" |
| PC Mode, host PC files, joiner other | refused: "The host's 1998 PC files are different ..." |
| Boss Mode All Bosses, host PC files, joiner none | refused (as PC Mode) |
| arcade 2P, host PC files, joiner none | plays, hashes equal (e8e66d3de3dae427 at 3000 frames, as main) |
| arcade 2P / 4 PCs / All Bosses 2P | 060c037658d73c70 / d39d8480dcd11d00 (SYNC PASS) / 3abe704ef3fbd320 (16034 frames; main's own build since pmbody: the same) |
| `hotd_nettest` (new: same data in, an arcade game ignores the PC data, unknown ROMs not compared, each refusal's sentence, 70 % loss, the four-PC hub) | all passed |

## The menu's host and joiner, tested in pairs (2026-10-27, worker "menuhost")

netdata found that a menu host in a hidden window (`HOTD_ONLINE_TEST=host` / `host-alone`) logged "hosting on" but
never answered a headless joiner, on main's own build too.

- **Cause: the test, not the network.** The hook fires on the first frame the main menu shows with the start-up
  splash over, which is the very frame the splash ends; the same loop pass then starts the boot attract (menuboot:
  splash -> attract). The attract is the game screen; the menu loop, and with it `OnlineMenu::frame` (the only place
  a menu session is polled), no longer ran, and a hidden window never gets the key that ends the attract. Port, bind
  address, join code, CPace secret, IPv4 and the protocol were all fine (with `--no-title` the same pair played).
  `host-alone(-boss)` worked because Play Alone goes into the game at once. A player cannot hit it: the attract
  starts only at the splash's end or after 30 s idle on the main menu, never on the Online Co-op pages, and leaving
  those pages stops hosting / joining.
- **Fix (also a guard for players):** `OnlineMenu::busy()` (hosting, joining, or listening for the LAN list); while
  busy the splash's attract and the idle / Extras attract are not started, so a menu session is always polled.
- **Hooks:** `join:` now goes through the Join page's box (`joinTyped`, as a player types it: a join code, a share
  code, or an address with the password), not straight to `startJoin`; `join-lan:<code>` waits for the LAN list's
  first joinable game, clicks it and types the code (the LAN address used, as for a player).
- **`tools/netplay_menu_test.ps1`** (`-Mode all|menu-cli|cli-menu|menu-menu|code|lan`): hidden windows
  (`HOTD_WINDOW_DISPLAY=99`), the boot kept (so the guard is tested), 3000 frames (`--stop-at` / `--frames`), PASS when
  both print `NET: result=ok` with the same hash; exit code = failed modes. code / lan host on 47803 (a short join code
  needs 47800-47803), the others on 47970; all bound to 127.0.0.1 (no firewall prompt).

| Pair (127.0.0.1, 3000 frames, 1 player, rollback 8) | Result |
|---|---|
| main's build, menu host (`host`, boot on) + headless joiner | the host's window went to the attract; joiner "no answer from the host in 20 s" |
| the same with `--no-title` (main's build) | joined, hashes equal: the network side was fine |
| menu-cli: menu host + `--join 127.0.0.1:47970 --code` | PASS fb20c042644c548c |
| cli-menu: `--host` + menu joiner (address + password in the box) | PASS fb20c042644c548c |
| menu-menu: two windows (address + password) | PASS fb20c042644c548c |
| code: menu host (random password) + menu joiner typing the join code lower case, no dashes | PASS fb20c042644c548c |
| lan: menu host + menu joiner clicking the LAN list's game (`--lobby`), the join code typed | PASS fb20c042644c548c |

Not covered here: a host bound to all interfaces (a real host; on one PC it would raise the Windows Firewall prompt),
the long share code typed without its dashes (not accepted; only the 16-letter code's dashes are optional).

## Waiting rooms, messages, the in-game info, the security review (protocol 9, 2026-10-20)

Plain step-by-step for players: **docs/online_howto.md**.

- **Names and the joiner's roster.** Each player has a name (Online Co-op > **Your Name**; launcher.ini `netname=`;
  default the Windows user name; `--net-name` on the command line), printable ASCII, at most 16. The joiner sends it
  inside its encrypted CONFIRM; the host sends ROSTER (every 500 ms: each seat's in / ping / name, 76 bytes) to every
  joiner, two-PC games included, so the joiner's waiting room lists the host and every seat with names and pings
  ("Rogan, host", "Ginger (you), 17 ms", "Waiting..."). Bug fixed on the way: the two-PC host's WELCOME had an empty
  payload, which the packet layer drops (every sealed packet needs at least one byte), so a two-PC joiner only left
  "Connecting..." when the host pressed Start. WELCOME is now always one byte (0 = a two-PC game, 1-3 = the seat).
- **Messages instead of silence.** The joiner's errors are full sentences on the page (wrapped over several rows):
  wrong password; game full; another version (the host's REFUSE, or a HELLO_ACK of another protocol); the host
  locked after 20 wrong passwords; **no answer** in `--net-timeout` seconds (default 20) with the hint "allow hotd.exe in
  Windows Firewall / forward UDP <port>"; a handshake that started but never finished; the host gone from the waiting
  room; the games differing at the start (another build / other game files); the address or password typed wrong. The
  host's page shows "Wrong Passwords: n tried" and "<name> left the waiting room". In play: "Connection to the other PC
  lost for n s", after 20 s "The other PC seems gone ... Esc: quit". A start that fails comes back to the Online page
  with the reason (it was only in the log before).
- **In-game info** (`NetGame::link`, drawn in main.cpp): Online Co-op > **In-game Info** Small / Detailed / Off
  (launcher.ini `netinfo=`; **F3** in play cycles it and keeps it). Small = the ping at the bottom's middle, green
  under 80 ms, yellow under 150, red above, and "LAG" for 3 s when rollback re-ran 30+ frames in a second or the game
  waited twice in a second. Detailed = four short lines there (ping, delay / frame, rollbacks, waits / sync). Both sit
  between players 1 / 2's HUD corners (players 3 / 4 have the top corners, the boss bar the top, the cut-scenes'
  captions the bottom's sides) and scale with the window (2x at 480-720 lines, 3x at 1080 ...). Nothing while a host
  plays alone. F12 / --shot-at screenshots include them (and the pad / net messages).
- **A seat leaving a four-PC waiting room** (before Start): a joined PC silent for 5 s (`NetGame::kWaitingLeftS`:
  Cancel, closed, its line dropped) gets a new seat session (`renewSeat`): the seat is free for the next joiner, the
  host's page says "<name> left. Seat n is free again.", every joiner's roster shows it free within half a second (a
  command-line joiner logs "the waiting room's seats: ..." on each change). Tests: hotd_nettest ("four PCs: a seat
  left before Start ..."), `tools\netplay4_test.ps1 -LeaveMs 1500` (a joiner killed in the waiting room; the next one
  gets its seat; prints PASS / FAIL).
- **The pages' text** (checked 2026-10-20 at 4:3 and 16:9): the PC menu font is wide (a row is 448 of 640 units), so
  messages and notes get rows of their own (wrapped by the font's widths, left-aligned; the text launcher: 90
  letters), long names / game names / typed addresses are shortened with ".." to fit beside their labels, the share
  code reads "Click to copy" (the code itself in the help line) and Play Alone hides once someone is in (the 4-PC
  waiting room stays on one page). Pictures without a network: `hotd --frontend-shot out.png --frontend-screen 120`
  with `HOTD_FE_ONLINE=<scene>` (`OnlineMenu::preview`: main, host, host-problem, host-locked, hosting2,
  hosting2-empty, hosting4-1, hosting4-3, join, join-typing, joining2, joining4, fail-password / -full / -version /
  -noanswer / -hostleft / -locked / -start / -address; "-wide" after a scene: 16-letter W / M names; "-pc": PC Mode, each seat's character); it also prints
  the rows as the text launcher shows them ("TEXT").
- **Security review** (all received data is untrusted, the password holder included):
  - Fixed: the wrong-password limit (20) could be bypassed: a host of up to four PCs reset it with every new seat
    session (`renewSeat`), a two-PC host with every new session (`renewSession`), and a host playing alone never
    enforced it (the handshake's error was ignored, HELLOs were still answered). The count now lives on the socket's
    owner, survives new sessions, and a locked host answers every new HELLO with REFUSE "locked".
  - Fixed: each new HELLO costs the host a key exchange; a flood is now limited to 16 new handshakes a second (a real
    joiner's HELLO repeats every 250 ms and gets in after the flood second; hotd_nettest: 400 forged HELLOs).
  - Fixed: a joiner could be made to allocate ~2 GB by a log header claiming 50 million frames (`frames.reserve`); the
    log and the settings' join frame are now capped at 4 million frames (~19 h) and nothing is reserved beyond 65536.
  - Fixed: a datagram over 2048 bytes (Windows: WSAEMSGSIZE) ended the receive loop of that pump; it is now counted as
    malformed and the loop goes on.
  - Checked, fine: every sealed packet is size-checked (max 1200), authenticated before use (AES-GCM, the header as
    associated data), replay-checked (64-packet window per direction, separate keys and nonce prefixes per direction:
    no reflection); INPUT checks player, ack, count x stride against the size, flags and floats of every seat before
    using any; LOG checks the offset / chunk length; SETTINGS / ROSTER / the input log decode into a temporary and
    check every field; REFUSE (unauthenticated) is ignored once connected and is 6 bytes (no amplification; HELLO is
    padded to 120). Known limit: before the handshake a spoofed REFUSE from the host's address can make a join fail
    (anyone on the path can drop packets anyway).
  - hotd_nettest adds: names / roster round trip and 300,000 mutated rosters; the input log on 300,000 mutated logs
    and a 4-million-frame header; no answer / full / version / locked messages; the HELLO flood; oversized
    datagrams; host-side fuzz into the joiner (20,000 packets each way).
