# Playing online: step by step

Up to four PCs play together directly (peer to peer). There is no server and no account: one PC **hosts** and gets a
short **code**; the others **join** by typing that code. Everything between the PCs is encrypted; without the code
nobody can join. (Technical details: docs/netplay.md in the source code.)

## Play without port forwarding

You usually need **no port forwarding** and no router settings. The code works the same way as before, it just
connects in more homes now:

1. Host: **Host a Game** -> **Start Hosting**. The **Internet** line says "Getting your address..." for a second,
   then **Hole punching** (or "Open: ..." when your router opened the port by itself with UPnP). Copy **Your Code** and
   send it to your friend.
2. Friend: **Join a Game** -> paste the code -> **Join**. Most of the time this is all: you are in the waiting room.
3. Only if the friend's page says "**No direct answer yet**": it shows a **Reply Code** (20 letters and digits). The
   friend clicks **Copy Reply Code** and sends it back to the host. The host pastes it in the waiting room
   (**Paste Reply Code**, or Ctrl+V), and both games now "knock" on each other's router for 10 seconds. The friend
   shows up in the list a moment later ("Waiting for your friend..." meanwhile).
   **A whole squad (up to 4 PCs): one paste.** Everyone who gets "No direct answer yet" posts their reply code in
   your group chat. The host selects the chat messages (names, times and other text can come along), copies them and
   pastes once: the game finds every reply code in it ("3 reply codes found: punching...") and each friend shows up as
   they connect. A code with a typo is skipped; a code already used is ignored. Someone late? Copy and paste again:
   only the new codes are added. When you come back to the game's window after copying, it also notices reply codes
   on the clipboard by itself (only then, and it ignores everything that is not a reply code).
4. Still nothing ("**Couldn't connect: your networks block direct connections**"): one of you is behind a strict or
   carrier-grade NAT (mobile hotspots, some internet providers). Try UPnP or port forwarding (section 5), or let the
   other player host. If the host's page says "Your network may block direct connections", swapping who hosts often
   helps.

What this does: your PC asks a public **STUN** server (by default Google's or Cloudflare's) "what is my internet
address?", and puts the answer in the code. That question is the only contact with anything outside your PCs: there
is no server of ours, no account, no lobby, and no relay (we run nothing; your game's traffic never goes through
anyone else). Don't want it? Online Co-op -> **Address Lookup** -> **Off**: the game then works as before (UPnP or
port forwarding only). The servers are in `launcher.ini` (`stunservers=`, comma separated, tried in that order;
`netstun=0` = Off).

Your code and the reply code contain your internet address: **share them only with friends**. The game never writes
your internet address (or anyone else's) to `hotd.log`: public addresses show as `public#XXXX`, so the log is safe to
attach to a bug report.

There is **no relay**: when both networks block direct connections, the game cannot help (a relay would need a server
running somewhere). Use UPnP / port forwarding, a VPN that puts both PCs on one network (e.g. a "LAN party" VPN), or
let the other player host.

## The short version

1. Host: main menu -> **Co-op** -> **Online Co-op** -> **Host a Game** -> **Start Hosting**. You see **Your Code**,
   e.g. `K7QX-M2PD-H4TA-9BCE`. Click **Copy Code** and send it to your friends (privately).
2. Friends: **Online Co-op** -> **Join a Game** -> type the code in **Join Code** (or **Paste Code** / Ctrl+V) ->
   **Enter** (or **Join**).
3. Host: when everyone shows up in the list, **Start Game**.

The code is 16 letters and digits. Small letters are fine, the dashes are optional, and it never contains 0, O, 1, I
or L (so they cannot be mixed up). A typo is almost always caught at once ("That code has a typo").

## 1. Before you start (every PC)

1. Copy the **whole** portable folder to every PC. All PCs need the **same version** of the game and the same game
   files (if one PC is updated, update all of them). Otherwise you get "another version" or "the games are different
   at the start".
   **PC Mode and Boss Mode** online also need the **1998 PC game** on every PC (the PC game chosen in the
   setup window), the same copy of it: these modes use its camera paths and animation table. Without it the host turns the
   friend away with a message saying which PC is missing them. Arcade games do not need the PC files.
2. Start the game once on every PC and check it plays.
3. Pick your name: Online Co-op -> **Your Name** (up to 16 letters). It shows in the waiting rooms.

## 2. Windows Firewall (the most common problem)

The first time you **host** (and the first time you open **Join a Game**), Windows asks whether hotd.exe may use the
network. Tick **Private networks** and click **Allow**. On a laptop whose Wi-Fi is set to "Public", tick **Public
networks** too.

If you clicked Cancel, or joining says "No answer":

1. Start menu -> type **Allow an app through Windows Firewall** -> open it.
2. **Change settings** -> find **hotd** in the list (none there: **Allow another app...** -> Browse -> pick
   `hotd.exe` in the game's folder -> Add).
3. Tick **Private** (and **Public** if your network is Public) -> **OK**.

Do this on the **host** PC for sure; on the joining PCs too if their LAN list stays empty. When the joining PC sees
the host's game in the LAN list but joining says "**The host's PC is on your network but its game does not answer**",
it is the host PC's firewall: do the steps above there. Swapped which PC hosts and it works? Same reason: the PC that
cannot host has the firewall blocking the game.

## 3. Host a game (one PC)

1. Online Co-op -> **Host a Game**.
2. More than two players? **4P Online**: "2 PCs" (one friend; with 4 players set, two players on each PC) or
   **"Up to 4 PCs"** (one or two players on each PC). With Up to 4 PCs, **Your Seats**: 1 (you are P1) or 2 (P1 + P3:
   a second person on your PC plays with the Player 2 input). Friends choose their own seats on the Join page.
   Optional: **Game Name** (what players on your network see in their list).
   **PC Mode** (the 1998 PC port's mode with characters): **Game Mode -> PC Mode**, then **Your Character**. Your
   friends pick their own character on their PCs (Join page: **Your Character**). Your Continues and Free-Play
   (Options) count for everyone (one pool of continues shared by all players); the game runs at PC Mode's 60 Hz on
   every PC. The game remembers Game Mode and your character for next time. When the game is over (or you reach the
   end), the online game ends and each PC's players type their names for the high scores on their own PC; each PC
   keeps its own PC Mode table.
   **Boss Mode** (the bosses only, against the clock): **Game Mode -> Boss Mode** (click Game Mode until it says so),
   then **Boss**: Chariot, Hangedman, Hermit, Magician or All Bosses (the four in a row). Everyone fights that boss
   together, no continues; nobody can join in with start during a fight. At the end each PC types its name for the
   boss times table on its own PC. The game remembers Boss Mode and the boss for next time.
3. **Start Hosting**. The page shows:
   - **Your Code**: the code to give your friends (a new one each time you host). Click it or **Copy Code**: the label
     says "Copied!" for 2 seconds, then paste it in a chat with Ctrl+V. The code is also the password: send it only to
     the people you play with.
   - **Player 2** (or Players 2-4): who has joined, with their name and ping.
   - **Internet**: "Open: 1.2.3.4" means friends on the internet can join with the code. "Your network only" means
     your router did not open the port (see section 5).
4. When everyone is in: **Start Game**. (**Play Alone** starts at once; friends can still join the running game with
   the same code.)

## 4. Join a game (the other PCs)

1. Online Co-op -> **Join a Game**.
2. **Join Code**: type the host's code, or **Paste Code** (Ctrl+V works too). Press **Enter** or click **Join**.
3. You are in the waiting room: it lists the host and every player. Wait for the host to press Start.

Two people on your PC? Set **Your Seats** to 2 before you join (a game of up to 4 PCs): you get two seats (e.g. P2 +
P4) when two are free, else one; the second person plays with the Player 2 input. The waiting room shows both, e.g. "Rogan (you),
P1's PC".

PC Mode: pick **Your Character** on the Join page before you join (Auto = Rogan, G, Rogan 2 or G 2 by your seat). The
waiting room then says "Game Mode: PC Mode" and shows each player's character next to their name. Joining a PC Mode
game that is already running: you play your own character once you press start (joining takes one of the shared
continues, as on the 1998 PC; with none left you cannot join until the host's game has Free-Play).

Boss Mode: the waiting room says "Game Mode: Boss Mode: <the boss>"; nothing to pick on your side.

**On the same Wi-Fi / router you need no code**: the host's game is listed on the Join page, under the buttons;
click it and you are in. (Anyone on your network can join that way. Over the internet the code is still needed.) The
code works on the same network too: it finds the host by itself.

## 5. Over the internet: the router

The host's router has to let the game in on **UDP port 47800** (only UDP, no TCP).

- Most routers do it by themselves (UPnP): the host's **Internet** line says "Open: ...". Nothing to do.
- "**Your network only**": switch on UPnP in the router's settings and host again, or forward the port by hand:

**Port forwarding by hand** (the host's router; menus differ by brand):

1. Open the router's page (often `192.168.1.1` or `192.168.0.1` in a browser; the login is usually on a sticker).
2. Find **Port Forwarding** (also "Virtual Server", "NAT", "Port mapping").
3. Add: protocol **UDP**, external port **47800**, internal port **47800**, device = the host PC (its address is in
   the help line of the **Internet** row, e.g. `192.168.1.20`). Save.
4. With a hand-made rule the code holds the host's home address, not its internet address: friends type the host's
   internet address and port into **Join Code** instead (e.g. `203.0.113.45:47800`), then the password shown in the
   **Internet** help line.

Port 47777 is only for the LAN list: never forward it. When hosting stops, the game closes its port (and removes the
UPnP rule).

Port forwarding is only needed when the code does not connect by itself (see "Play without port forwarding" at the top).
Hardest case: both PCs behind a mobile hotspot or carrier-grade NAT. Then let someone else host, or play on the same network.

## 6. In the game

- Bottom of the screen: the **ping** (green: under 80 ms, great; yellow: under 150 ms, fine; red: high). "**LAG**"
  shows when the connection struggles. **F3** switches it to a few detailed lines, then off.
- **Pause** (Esc / P): the game stops for everyone; the other PCs show "**P1 PAUSED**" (the number of the player who
  paused) until that player picks Continue.
- **Cut-scenes**: only the host can skip them. Another player's trigger does nothing there (a small note says "Only the
  host can skip").
- Short drops are fine: the game waits and goes on. A PC that drops out can join again with the same code (Join a
  Game, the same code, or one click on the game in the LAN list): it gets its seat(s) back at once, both seats of a
  two-seat PC, in the same game (Boss Mode: the same fight). This works after closing the game window too: start the
  game again, Online Co-op -> Join a Game.
- **A player leaves** (closes the game, Esc -> Exit, or the connection drops): the game stops for everyone. The host
  sees "P2 left: waiting for them to join again (same code). Enter / Start: go on without them". Wait, and the
  player joins again into the same game; or press **Enter** (or Start on the pad) to go on without them (their seat
  stays free: they can still join later). The other players see "P2 left: the host waits for them or goes on".
- Esc -> Exit leaves. When the **host** leaves, the game ends for everyone.

## 7. Messages and what to do

| Message | What to do |
| --- | --- |
| That code has a typo / is too short / too long | Check the code with the host (or paste it instead of typing). |
| A code never has 0, O, 1, I or L | You typed one of those: it is another letter (e.g. Q, D, J, T or 7). |
| Wrong code | The host refused it: check the code (the host gets a new code each time it hosts). |
| This game is full | All seats taken, or the game runs without a free seat. Host "Up to 4 PCs" for more players. |
| Another version of HOTD ReWrithe | Copy the same game folder to every PC. |
| The host uses the 1998 PC files for PC Mode / Boss Mode; this PC doesn't have them | Choose the 1998 PC game in the setup window on this PC (Options -> System -> Files Check), or ask the host for an Arcade game. |
| This PC uses the 1998 PC files ...; the host doesn't have them | The host chooses its PC game in the setup window (or hosts an Arcade game). |
| The host's 1998 PC files are different from this PC's | Both PCs need the same copy of the PC game (the same THOTD.EXE): copy the host's PC game folder. |
| The host's arcade ROMs are different from this PC's | Both PCs need the same arcade ROM set (MAME's hotd). |
| No answer from ... | The host is not hosting, the host's **firewall** blocks hotd.exe (section 2), or over the internet the port is not open (section 5). |
| The host answered but the connection did not finish | Packets are lost on the way; try again (another Wi-Fi spot, cable). |
| The host stopped taking players after too many wrong codes | Someone tried 20 wrong codes. The host stops hosting and hosts again (a new code). |
| The host left | The host stopped hosting or lost its connection. Host again, then join again. |
| The games are different at the start | Same game folder and game files on every PC. |
| Connection lost for n s | Waiting for the other PC; it continues by itself when it comes back. After 20 s: "seems gone", Esc quits. |
| UDP port 47800 is already in use | Another copy of the game is hosting on this PC: close it. |

## 8. Safety

- Everything is encrypted; a stranger who knows your address cannot join, watch or change anything without the code.
- The code's secret part is 40 random bits (about a trillion possibilities). Nobody can test codes offline, and after 20
  wrong codes the host refuses everyone until it hosts again, so guessing has no real chance.
- The code contains the password: send it only to the people you play with. Each new hosting makes a new code.
- The game opens its port only while you host, and closes it (and the UPnP rule) when you stop.
- **Your internet address is shared.** The share code holds the host's IP address, and the PCs in a game talk to each
  other directly (no server in between), so everyone you play with sees your IP address and you see theirs, as with
  most games without a server. Play only with people you trust with it.
- No accounts, no servers of ours, nothing is sent anywhere except to the PCs in your game; the game collects no data.
