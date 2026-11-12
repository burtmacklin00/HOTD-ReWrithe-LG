// Online co-op in the game's own front end (docs/netplay.md "Playing online"): pages built on the PC front end's
// extras page (game/frontend.h; the Remake style draws the same rows): Host a Game (the waiting room: the join code,
// Copy Code, who joined, Start) and Join a Game (one box for the code, Paste Code, Join; the LAN list). The window's
// loop feeds it each menu frame and takes the connected game when it is ready to play.
#pragma once

#include "game/frontend.h"
#include "net/net_game.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hotd::net {

class OnlineMenu {
public:
    std::function<Settings()> hostSettings;              // the host's game (the launcher's settings)
    std::function<void(const std::string&)> copy;        // to the clipboard
    std::function<std::string()> paste;                  // from the clipboard
    std::function<void(const std::string&)> log;
    std::function<void(NetArgs&)> gameData;              // this PC's game data digests (protocol 19), before Host / Join
    NetArgs base;                                       // the command line's netplay options (tests: --lobby, --upnp)
    std::string name = "HOTD game";                      // the host's game name (the LAN list)
    std::string playerName = "Player";                   // this player's name (the waiting rooms; launcher.ini netname=)
    int netInfo = 1;                                     // the in-game connection info: 0 off, 1 small (ping), 2 detailed (launcher.ini netinfo=)
    std::function<void()> saveSettings;                  // playerName / netInfo / Game Mode / the characters changed
    uint16_t port = 47800;                               // the host's UDP port
    // Survival Mode online (protocol 22): the arenas this build offers (main's survivalArenaCount(): a public build
    // Chapter 1's courtyard only; 0: no Survival in this build), the Host page's Game Mode then has Survival
    int survivalArenas = 0;
    // hole punching: launcher.ini stunservers= (empty: kDefaultStun); base.stun "off" = the Online page's Address
    // Lookup off (launcher.ini netstun=0)
    std::string stunServers;

    void open(game::Frontend& fe, std::function<void()> back);   // the Online page
    bool active(const game::Frontend& fe) const;
    void frame(game::Frontend& fe);                      // the network, the LAN list, the page's values
    // the text launcher (no PC menu art): the same pages as rows it draws itself
    struct TextRow { std::string label, value; bool clickable; };
    void openText(std::function<void()> back);
    bool textMode() const { return textMode_; }
    std::vector<TextRow> textRows() const;
    void clickText(int row);
    void backText();                                     // Esc
    void frameText() { frame(textFrontend()); }
    static game::Frontend& textFrontend();
    bool editing() const { return edit_ != nullptr; }
    void typed(const std::string& text);                 // while editing: text, or the keys below
    void backspace();
    // a controller's letter picker: the last character one up / down the letters (an empty field: 'A'); a new one
    void pick(int dir);
    void pickNext();
    void endEdit(game::Frontend& fe, bool keep);         // Enter (keep) / Escape
    // natpunch2: the game's window got the focus back: on the host's waiting room, valid new reply codes on the
    // clipboard are punched to (nothing else read or kept; never in the background)
    void focusGained(game::Frontend& fe);
    bool pasteCode(game::Frontend& fe);                  // Ctrl+V on the Join page (not typing): the code into the box
    // ready to play: the connected session; the client's settings are the host's
    bool ready() const { return ready_; }
    // hosting, joining or listening for the LAN list: the network in use (the menu must keep calling frame(), no attract)
    bool busy() const { return game_ != nullptr || listening_; }
    std::unique_ptr<NetGame> take(Settings& s);
    void cancel();                                       // hosting / joining stopped
    // the taken game could not start (the PCs' games differ, the other left): back to the page, the reason shown
    void startFailed(game::Frontend& fe, const std::string& why);
    // testing (HOTD_ONLINE_TEST): "host" (Start once player 2 is in), "join:<join code, share code or address>|<password>"
    // (typed in the Join box), "join-lan:<code>" (the LAN list's first game clicked, the code typed), "join-lan-open"
    // (the LAN list's first game open on the LAN clicked: no code). Not testing (Light Gun Edition, --online-host /
    // --online-join): "auto-host" (Start Hosting, the code copied to the clipboard, the game starts once a friend is in;
    // more can join the running game), "auto-join:<code or empty>" (a game on this network, else the code)
    void script(game::Frontend& fe, const std::string& cmd);
    // testing (--frontend-shot with --frontend-screen 120, HOTD_FE_ONLINE=<scene>): a page with made-up players and
    // messages, no network (main, host, hosting2, hosting4-1, hosting4-3, joining2, joining4, fail-password, fail-full,
    // fail-version, fail-noanswer, fail-hostleft, fail-locked, fail-start, fail-address, host-problem, host-locked; any
    // scene + "-pc": PC Mode, each seat's character); false: none
    bool preview(game::Frontend& fe, const std::string& scene);

private:
    enum class Page { Main, Host, Hosting, Join, Joining };
    Page page_ = Page::Main;
    std::function<void()> back_;
    std::unique_ptr<NetGame> game_;
    LobbyListener lobby_;
    bool listening_ = false;
    std::string password_, joinText_, joinPassword_, status_;
    std::chrono::steady_clock::time_point copiedAt_{};   // Copy Code: "Copied!" for 2 s after a click
    Address lanPick_;                                    // a LAN game clicked: the code joins it there (its LAN address)
    bool copiedShown() const { return copiedAt_ != std::chrono::steady_clock::time_point{} && std::chrono::steady_clock::now() - copiedAt_ < std::chrono::seconds(2); }
    void copyCode();
    // hole punching (docs/netplay.md "Hole punching"): the host's waiting room takes a friend's reply code (typed /
    // pasted: its punches start); a joiner's page shows its own reply code once the direct try had no answer
    std::string replyText_;
    void useReply(game::Frontend& fe, const std::string& text);
    void copyReply();
    void joinTyped(game::Frontend& fe);                 // Join / Enter: the box's code (or address + password)
    bool boxIsAddress() const;                           // the Join box holds an address (the Password row shows)
    std::string* edit_ = nullptr;
    std::string editBackup_;
    size_t editMax_ = 32;
    bool ready_ = false, joinAfterEdit_ = false, scriptHost_ = false, tookHost_ = false, rosterLogged_ = false;
    std::string scriptLan_;                              // testing (join-lan:<code>): the code typed once the LAN list shows a game
    bool scriptLanOpen_ = false;                         // testing (join-lan-open): the LAN list's first game clicked, no code
    // Light Gun Edition (auto-join, --online-join): a game open on this network is joined at once; else after 3 s the code
    // (from the command line or the clipboard), if it is one; else the page waits for a game on this network
    std::string autoJoinCode_;
    std::chrono::steady_clock::time_point autoJoinAt_{};
    // a typed join code whose address is not on this network's list yet (the list hears each host once a second): the
    // join waits up to 2.5 s after the Join page opened for the LAN list, so the same network uses the LAN address
    // (a code with the internet address seldom works from inside the same router)
    std::string pendingJoin_;
    std::chrono::steady_clock::time_point listenAt_{}, pendingUntil_{};
    bool noDefer_ = false;
    Settings settings_;
    std::string signature_;
    bool textMode_ = false;
    std::vector<game::Frontend::Extra> rows_;            // the page's rows (both modes)
    std::function<void()> exit_;
    // what the pages show of the game: the connected game's, or preview()'s made-up one
    struct Shown {
        bool host = true, multi = false, peerIn = false;
        int local = 0, seats = 2, wrong = 0;
        Session::RosterSeat roster[4];
        std::string code, share, lan, internet;
        bool punch = false, symmetric = false;   // hole punching: STUN on (the Reply Code rows), a strict NAT seen
        std::string reply, natNote;              // a joiner's reply code; the note (NetGame::natNote)
    };
    bool previewing_ = false;
    Shown fake_;
    std::vector<LobbyGame> fakeLobby_;                   // preview(): the LAN list
    bool hasGame() const { return game_ || previewing_; }
    Shown shown() const;
    void show(game::Frontend& fe, Page p);
    void build(game::Frontend& fe);
    std::vector<game::Frontend::Extra> makeRows(game::Frontend& fe);   // the page's rows now (build shows them)
    static std::string signatureOf(const std::vector<game::Frontend::Extra>& rows);
    void startEdit(game::Frontend& fe, std::string& field, size_t max);
    void startHost(game::Frontend& fe);
    void startJoin(game::Frontend& fe, const std::string& target, const std::string& password);
};

} // namespace hotd::net
