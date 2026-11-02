#include "net/online_menu.h"

#include "game/bossmode.h"
#include "game/frontend.h"
#include "port/game.h"
#include "util/utf8.h"

#include <algorithm>
#include <cctype>

namespace hotd::net {

namespace {
const char* const kTitle = "Online Co-op";

std::string ipOnly(const Address& a)
{
    const std::string s = a.str();
    return s.substr(0, s.rfind(':'));
}
} // namespace

bool OnlineMenu::active(const game::Frontend& fe) const
{
    return fe.screen() == 100 && fe.extrasTitle == kTitle;
}

game::Frontend& OnlineMenu::textFrontend()
{
    static game::Frontend f;   // (the text launcher: never loaded or drawn; the pages' rows only)
    return f;
}

void OnlineMenu::openText(std::function<void()> back)
{
    textMode_ = true;
    back_ = std::move(back);
    if (password_.empty())   // (--code: the password the menu starts with)
        password_ = base.code.empty() || normaliseCode(base.code).empty() ? randomCode() : normaliseCode(base.code);
    show(textFrontend(), game_ ? page_ : Page::Main);
}

std::vector<OnlineMenu::TextRow> OnlineMenu::textRows() const
{
    std::vector<TextRow> v;
    for (const auto& r : rows_)
        v.push_back({r.name, r.value ? r.value() : std::string(), bool(r.click)});
    return v;
}

void OnlineMenu::clickText(int i)
{
    if (i >= 0 && size_t(i) < rows_.size() && rows_[size_t(i)].click) {
        const auto c = rows_[size_t(i)].click;   // (a copy: the click may rebuild the rows)
        c();
        build(textFrontend());
    }
}

void OnlineMenu::backText()
{
    if (exit_) { const auto e = exit_; e(); }
}

void OnlineMenu::open(game::Frontend& fe, std::function<void()> back)
{
    textMode_ = false;
    back_ = std::move(back);
    if (password_.empty())   // (--code: the password the menu starts with)
        password_ = base.code.empty() || normaliseCode(base.code).empty() ? randomCode() : normaliseCode(base.code);
    show(fe, game_ ? page_ : Page::Main);
}

void OnlineMenu::show(game::Frontend& fe, Page p)
{
    page_ = p;
    fe.extrasPage = 0;
    if (p == Page::Join && !listening_ && !previewing_) {   // the LAN list: listening on the lobby port (shared; tests: --lobby)
        Address bind{0, kLobbyPort};
        std::string e;
        if (!base.lobby.empty())
            parseAddress(base.lobby, bind, false, e);
        listening_ = lobby_.start(bind, e);
        listenAt_ = std::chrono::steady_clock::now();
        if (!listening_ && log) log("the LAN list is not available: " + e);
    }
    build(fe);
}

void OnlineMenu::startEdit(game::Frontend& fe, std::string& field, size_t max)
{
    edit_ = &field;
    editBackup_ = field;
    editMax_ = max;
    build(fe);
}

void OnlineMenu::typed(const std::string& text)
{
    if (!edit_)
        return;
    for (char c : text)
        if (c >= 32 && c < 127 && edit_->size() < editMax_)
            *edit_ += c;
}

void OnlineMenu::pick(int dir)
{
    // the letters a password / address / name uses, in an order a d-pad gets through quickly
    static const std::string kSet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.: ";
    if (!edit_)
        return;
    if (edit_->empty()) {
        *edit_ = "A";
        return;
    }
    char& c = edit_->back();
    size_t i = kSet.find(char(std::toupper(static_cast<unsigned char>(c))));
    if (i == std::string::npos) i = 0;
    i = (i + kSet.size() + size_t(dir > 0 ? 1 : kSet.size() - 1)) % kSet.size();
    c = kSet[i];
}

void OnlineMenu::pickNext()
{
    if (edit_ && edit_->size() < editMax_)
        *edit_ += edit_->empty() ? 'A' : edit_->back();   // (the same letter: a neighbour is a few presses away)
}

void OnlineMenu::backspace()
{
    if (edit_ && !edit_->empty())
        edit_->pop_back();
}

void OnlineMenu::endEdit(game::Frontend& fe, bool keep)
{
    if (!edit_)
        return;
    if (!keep)
        *edit_ = editBackup_;
    const bool joinNow = keep && (edit_ == &joinPassword_ || (edit_ == &joinText_ && !joinText_.empty())) && joinAfterEdit_;
    if (edit_ == &playerName) {
        playerName = sanitiseName(playerName);
        if (playerName.empty()) playerName = "Player";
        if (keep && saveSettings) saveSettings();
    }
    joinAfterEdit_ = false;
    edit_ = nullptr;
    if (joinNow)
        joinTyped(fe);
    else
        build(fe);
}

bool OnlineMenu::boxIsAddress() const
{
    // (a join code never has a dot or a colon; "HOTD-..." is the long share code: it holds the password)
    return joinText_.find_first_of(".:") != std::string::npos;
}

void OnlineMenu::joinTyped(game::Frontend& fe)
{
    std::string t = joinText_;
    while (!t.empty() && t.back() == ' ') t.pop_back();
    while (!t.empty() && t.front() == ' ') t.erase(t.begin());
    if (t.empty()) {
        status_ = "Type or paste the host's code first (Join Code).";
        build(fe);
        return;
    }
    Address a;
    std::string pw;
    const int r = parseJoinCode(t, a, pw);
    if (r > 0) {
        // the same network: the host's LAN address (the LAN game clicked, or the one announcing the code's address as
        // its LAN or internet address: a router seldom lets a PC reach its own internet address from inside)
        bool found = false;
        if (lanPick_.ip && lanPick_.port == a.port) {
            a = lanPick_;
            found = true;
        } else {
            for (const LobbyGame& g : lobby_.games())
                if (g.addr.port == a.port && (g.addr.ip == a.ip || (g.publicIp && g.publicIp == a.ip))) {
                    a = g.addr;
                    found = true;
                    break;
                }
        }
        // (an internet address and the list just opened, 2026-10-30: the Join page opened and the code pasted at once,
        // e.g. joining again after a restart: the host's LAN announcement not heard yet; wait for it a moment)
        const auto now = std::chrono::steady_clock::now();
        if (!found && !noDefer_ && listening_ && !isPrivateIp(a.ip) && now - listenAt_ < std::chrono::milliseconds(2500)) {
            pendingJoin_ = t;
            pendingUntil_ = listenAt_ + std::chrono::milliseconds(2500);
            status_ = "Looking for the host on your network...";
            build(fe);
            return;
        }
        noDefer_ = false;
        startJoin(fe, a.str(), pw);
        return;
    }
    std::string up;
    for (char c : t) up += char(std::toupper(static_cast<unsigned char>(c)));
    if (up.rfind("HOTD-", 0) == 0) {   // the long share code (a custom password): NetGame::open reads it
        startJoin(fe, t, "");
        return;
    }
    if (boxIsAddress()) {   // the manual way: address:port, then the password
        if (joinPassword_.empty()) {
            joinAfterEdit_ = true;
            startEdit(fe, joinPassword_, 16);
            return;
        }
        startJoin(fe, lanPick_.ip ? lanPick_.str() : t, joinPassword_);
        return;
    }
    status_ = r == -2 ? "A code never has 0, O, 1, I or L: check those letters with the host."
              : r == -1 ? "That code is too short: a code has 16 letters and digits (like ABCD-EFGH-JKMN-PQRS)."
              : r == -3 ? "That code is too long: a code has 16 letters and digits (like ABCD-EFGH-JKMN-PQRS)."
                        : "That code has a typo: check it with the host (or paste it).";
    build(fe);
}

bool OnlineMenu::pasteCode(game::Frontend& fe)
{
    if (page_ != Page::Join || edit_ || !paste)
        return false;
    std::string t;
    for (char c : paste())
        if (c >= 32 && c < 127 && t.size() < 48) t += c;
    while (!t.empty() && t.back() == ' ') t.pop_back();
    while (!t.empty() && t.front() == ' ') t.erase(t.begin());
    joinText_ = t;
    status_.clear();
    build(fe);
    return true;
}

void OnlineMenu::copyCode()
{
    if (!hasGame())
        return;
    if (game_ && copy)
        copy(game_->shareCode());
    copiedAt_ = std::chrono::steady_clock::now();
}

void OnlineMenu::cancel()
{
    if (game_) game_->close();
    game_.reset();
    ready_ = false;
    previewing_ = false;
}

std::unique_ptr<NetGame> OnlineMenu::take(Settings& s)
{
    s = settings_;
    if (game_ && game_->session().isHost()) {   // PC Mode online: the characters hostStart settled (the joiners' picks)
        const Settings& g = game_->gameSettings();
        s.pcMode = g.pcMode, s.pmContinues = g.pmContinues, s.pmFreePlay = g.pmFreePlay;
        std::copy(g.pmChar, g.pmChar + 4, s.pmChar);
        s.bossMode = g.bossMode;   // (Boss Mode online, protocol 18: the host's boss)
    }
    ready_ = false;
    tookHost_ = page_ == Page::Hosting;
    page_ = Page::Main;
    if (listening_) { lobby_.stop(); listening_ = false; }
    return std::move(game_);
}

void OnlineMenu::startFailed(game::Frontend& fe, const std::string& why)
{
    cancel();
    status_ = why.empty() ? "The online game could not start." : why;
    show(textMode_ ? textFrontend() : fe, tookHost_ ? Page::Host : Page::Joining);
}

void OnlineMenu::startHost(game::Frontend& fe)
{
    NetArgs a = base;
    a.join.clear();
    a.host = (base.bind.empty() ? std::string() : base.bind + ":") + std::to_string(port);   // (tests: --net-bind 127.0.0.1)
    a.code = password_;
    a.lanName = name;
    a.playerName = playerName;
    if (gameData) gameData(a);   // (protocol 19: a joiner with other game data is refused, with the reason)
    status_.clear();
    copiedAt_ = {};
    game_ = std::make_unique<NetGame>();
    game_->log = log;
    if (!game_->open(a, status_)) {
        game_.reset();
        build(fe);
        return;
    }
    show(fe, Page::Hosting);
}

void OnlineMenu::startJoin(game::Frontend& fe, const std::string& target, const std::string& password)
{
    NetArgs a = base;
    a.host.clear();
    a.join = target;
    a.code = password;
    a.playerName = playerName;
    if (gameData) gameData(a);   // (protocol 19: the host compares them with its own)
    status_ = "Connecting...";
    rosterLogged_ = false;
    game_ = std::make_unique<NetGame>();
    game_->log = log;
    std::string err;
    if (!game_->open(a, err)) {
        game_.reset();
        status_ = err;
    }
    show(fe, Page::Joining);
}

namespace {
// a long message over rows of their own: each row as many words as fit(row) allows (a word longer than a row is cut)
std::vector<std::string> wrapText(const std::string& text, const std::function<bool(const std::string&)>& fit)
{
    std::vector<std::string> out;
    std::string line;
    size_t i = 0;
    while (i < text.size()) {
        size_t end = text.find(' ', i);
        if (end == std::string::npos) end = text.size();
        std::string word = text.substr(i, end - i);
        i = end + 1;
        if (word.empty()) continue;
        if (!line.empty() && fit(line + " " + word)) { line += " " + word; continue; }
        if (!line.empty()) out.push_back(line);
        while (!fit(word) && word.size() > 1) {   // (an address or a code longer than a row)
            size_t n = word.size() - 1;
            while (n > 1 && !fit(word.substr(0, n))) --n;
            out.push_back(word.substr(0, n));
            word = word.substr(n);
        }
        line = word;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

const char* netInfoName(int v) { return v == 0 ? "Off" : v == 2 ? "Detailed" : "Small (Ping)"; }
// PC Mode's characters (THOTD.EXE's table 0x5CE348, docs/pcmode.md)
const char* charName(int c)
{
    static const char* const kNames[7] = {"No Player", "Rogan", "G", "Rogan 2", "G 2", "Sophie", "Researcher"};
    if (const auto* x = port::Game<port::A>::pm_extra(c))   // pmextra: the Extra Characters 7-19
        return x->name;
    return c >= 0 && c <= 6 ? kNames[c] : "";
}
// a "Your Character" row's value: 0 = the seat's default
std::string pickName(int c, int seatDefault)
{
    return c >= 1 && c <= kPmCharMax ? std::string(charName(c)) : seatDefault >= 1 ? std::string("Auto (") + charName(seatDefault) + ")" : std::string("Auto");
}
int nextPick(int c) { return c >= kPmCharMax ? 0 : c + 1; }   // Auto, Rogan .. Researcher, the extras
} // namespace

OnlineMenu::Shown OnlineMenu::shown() const
{
    if (!game_)
        return fake_;
    Shown g;
    g.host = game_->session().isHost();
    g.multi = game_->multi();
    g.peerIn = game_->peerIn();
    g.local = game_->localPlayer();
    g.seats = game_->gameSettings().seats;
    g.wrong = game_->session().wrongCodes();
    game_->roster(g.roster);
    if (g.host) {
        g.code = game_->shownCode();
        g.share = game_->shareCode();
        g.lan = game_->lanAddress().str();
        const PortMapper& u = game_->upnp();
        switch (u.state()) {
        case PortMapper::State::Working: g.internet = "Asking the router..."; break;
        case PortMapper::State::Mapped:
            g.internet = u.externalIp() ? "Open: " + ipOnly(Address{u.externalIp(), 0}) : std::string("Port open (UPnP)");
            break;
        default: g.internet = "Your network only";
        }
    }
    return g;
}

bool OnlineMenu::preview(game::Frontend& fe, const std::string& scene)
{
    cancel();
    previewing_ = false;
    status_.clear();
    fake_ = Shown{};
    fake_.code = "K7QXM2PD";
    password_ = "K7QXM2PD";
    fake_.share = "2WFU-MBKD-YH5B-72XV";
    fake_.lan = "192.168.1.20:47800";
    fake_.internet = "Open: 203.0.113.45";
    static const char* const kNames[4] = {"Thomas Rogan", "G", "Sophie Richards", "Harry1998ABCDEFG"};
    auto seat = [this](int s, int ping) { fake_.roster[s] = {true, uint16_t(ping), kNames[s]}; };
    const std::string noAnswer = "No answer from 203.0.113.45:47800. " + reachHint(47800);
    if (scene.size() > 5 && scene.compare(scene.size() - 5, 5, "-wide") == 0) {   // the widest names (16 W's)
        static const char* const kWide[4] = {"WWWWWWWWWWWWWWWW", "MMMMMMMMMMMMMMMM", "WWWWWWWWWWWWWWWW", "MMMMMMMMMMMMMMMM"};
        const std::string base = scene.substr(0, scene.size() - 5);
        if (!preview(fe, base)) return false;
        for (int s = 0; s < 4; ++s)
            if (fake_.roster[s].in) fake_.roster[s].name = kWide[s], fake_.roster[s].pingMs = s ? 9999 : 0;
        playerName = name = kWide[0];
        name += "WWWWWWWW";   // (24: the game name's limit)
        password_ = joinPassword_ = "WWWWWWWWWWWWWWWW";
        show(fe, page_);
        return true;
    }
    if (scene.size() > 5 && scene.compare(scene.size() - 5, 5, "-boss") == 0) {   // Boss Mode online: All Bosses
        if (!preview(fe, scene.substr(0, scene.size() - 5))) return false;
        fake_.roster[0].boss = 6;
        base.pcMode = false, base.bossMode = 6;   // (the Host page: its Boss Mode rows)
        show(fe, page_);
        return true;
    }
    if (scene.size() > 3 && scene.compare(scene.size() - 3, 3, "-pc") == 0) {   // PC Mode online: each seat's character
        if (!preview(fe, scene.substr(0, scene.size() - 3))) return false;
        static const int kChars[4] = {1, 5, 3, 6};   // Rogan, Sophie, Rogan 2, Researcher
        for (int s = 0; s < 4; ++s)
            fake_.roster[s].pcChar = kChars[s];
        base.pcMode = true;   // (the Host page: its PC Mode rows)
        show(fe, page_);
        return true;
    }
    if (scene == "join" || scene == "join-typing") {   // the LAN list (a long name, another version, a running game)
        previewing_ = true;
        LobbyGame a, b, c;
        a.addr = {0xC0A80114, 47800}, a.name = "Sophie's Curien Mansion!", a.players = 2, a.maxPlayers = 4, a.build = buildId();
        b.addr = {0xC0A80115, 47800}, b.name = "Old build", b.build = 1;
        c.addr = {0xC0A80116, 47800}, c.name = "G's game", c.players = 2, c.inGame = true, c.build = buildId();
        fakeLobby_ = {a, b, c};
        joinText_ = fake_.share;
        joinPassword_ = "K7QXM2PD";
        show(fe, Page::Join);
        if (scene == "join-typing") startEdit(fe, joinText_, 48);
        return true;
    }
    if (scene == "main") return show(fe, Page::Main), true;
    if (scene == "host") return show(fe, Page::Host), true;
    if (scene == "host-problem") {   // the port in use
        status_ = "UDP port 47800 is already in use (is another copy of the game hosting?)";
        return show(fe, Page::Host), true;
    }
    if (scene.rfind("hosting", 0) == 0) {
        previewing_ = true;
        seat(0, 0);
        if (scene == "hosting2") {
            seat(1, 38);
            fake_.peerIn = true;
        } else if (scene == "hosting2-empty") {
            fake_.internet = "Your network only";
        } else {   // hosting4-1 / hosting4-3: up to four PCs, one / three joined; hosting4-mix: the host P1 + P3, G P2 + P4
            fake_.multi = true;
            fake_.peerIn = true;
            seat(1, 38);
            if (scene == "hosting4-mix")
                fake_.roster[2] = {true, 0, kNames[0], 0}, fake_.roster[3] = {true, 38, kNames[1], 1};
            if (scene == "hosting4-3") {
                seat(2, 142), seat(3, 999);
                fake_.wrong = 3;
                status_ = "Sophie Richards left. Seat 3 is free again.";   // (NetGame::waitingLeft)
            }
        }
        return show(fe, Page::Hosting), true;
    }
    if (scene == "joining2" || scene == "joining4" || scene == "joining4-mix") {
        previewing_ = true;
        fake_.host = false;
        fake_.peerIn = true;
        fake_.multi = scene != "joining2";
        fake_.local = scene == "joining4" ? 3 : 1;
        seat(0, 0), seat(1, 61);
        if (scene == "joining4") seat(3, 87);
        if (scene == "joining4-mix")   // (this PC: P2 + P4; Sophie P3)
            seat(2, 90), fake_.roster[3] = {true, 61, kNames[1], 1};
        status_ = "Waiting for the host to start";
        return show(fe, Page::Joining), true;
    }
    static const std::pair<const char*, const char*> kFails[] = {
        {"fail-password", "Wrong code: the host refused it. Check the code with the host (or paste it)."},
        {"fail-full", "This game is full (no free seat, or it is running and not open for joining)."},
        {"fail-version", "The host has another version of HOTD ReWrithe: both PCs need the same build."},
        {"fail-locked", "The host stopped taking players after too many wrong codes: the host has to host again."},
        {"fail-hostleft", "The host left (or the connection dropped). Ask the host to host again, then join again."},
        {"fail-start", "The two PCs' games are different at the start: both need the same HOTD ReWrithe build and the same "
                       "game files (arcade ROMs / PC files)."},
        {"fail-address", "That is not a code or an address. Type or paste the host's code (like ABCD-EFGH-JKMN-PQRS), or its "
                         "address and port like 192.168.1.20:47800."},
        {"fail-noanswer", nullptr},
    };
    for (const auto& f : kFails)
        if (scene == f.first) {
            status_ = f.second ? f.second : noAnswer;
            return show(fe, Page::Joining), true;
        }
    if (scene == "host-locked") {
        status_ = "Too many wrong codes were tried: nobody else can join now. Stop hosting and host again (you get a new "
                  "code).";
        return show(fe, Page::Host), true;
    }
    return false;
}

std::string OnlineMenu::signatureOf(const std::vector<game::Frontend::Extra>& rows)
{
    std::string sig;
    for (const auto& r : rows)
        sig += r.name + "\x01" + (r.value ? r.value() : "") + (r.click ? "+" : "-") + "\x02";
    return sig;
}

std::vector<game::Frontend::Extra> OnlineMenu::makeRows(game::Frontend& fe)
{
    using X = game::Frontend::Extra;
    std::vector<X> rows;
    // a value that does not fit beside its label in the menu's font: its end only, after ".." (a field being typed shows
    // its newest letters)
    auto fit = [this, &fe](const std::string& label, std::string v) {
        if (textMode_ || fe.rowTextWidth("A") == 0) return v;
        const int room = game::Frontend::kExtrasWidth - fe.rowTextWidth(label) - 16;
        if (fe.rowTextWidth(v) <= room) return v;
        while (v.size() > 1 && fe.rowTextWidth(".." + v) > room) v = util::utf8Tail(v, v.size() - 1);   // (a letter: misc18)
        return ".." + v;
    };
    auto edited = [this, fit](const std::string& label, const std::string& field) {
        return [this, fit, label, &field]() {
            return fit(label, field + (edit_ == &field ? "_" : "") + (field.empty() && edit_ != &field ? "-" : ""));
        };
    };
    // a problem / note (status_): its own rows, left-aligned, as many as it needs (the menu's font: the row's width;
    // the text launcher: 90 characters)
    auto problem = [this, &rows, &fe](const std::string& text) {
        const bool px = !textMode_ && fe.rowTextWidth("A") > 0;
        // (lang: the message translated first, its rows not again; a CJK line breaks between any two characters)
        const bool tr = px && lang::on();
        const std::string t = tr ? lang::tr(text) : text;
        auto fits = [&](const std::string& s) {
            return px ? fe.rowTextWidthRaw(s) <= game::Frontend::kExtrasWidth : s.size() <= 90;
        };
        const std::vector<std::string> lines = tr && lang::active() ? lang::wrapBy(t, float(game::Frontend::kExtrasWidth), [&](const std::string& s) {
            return fits(s) ? 0.0f : 1e9f;
        }) : lang::noteLines(wrapText(t, fits));
        for (const std::string& line : lines) {
            rows.push_back(X{line, nullptr, nullptr});
            rows.back().raw = tr;
        }
    };
    // a seat of the roster: "Name (you), 32 ms" / "Name, host" / "Waiting..."
    // (a wide name is shortened to "Name.." so the text never runs into the row's label)
    auto seatText = [this, &fe](int s, const std::string& label, int s2 = -1) {
        if (!hasGame()) return std::string();
        const Shown g = shown();
        const Session::RosterSeat& r = g.roster[s];
        if (!r.in) return std::string("Waiting...");
        // a PC's second seat (up to four PCs, protocol 15): "Name (you), P2's PC" (the same PC as that seat)
        const int pc = r.with >= 0 ? r.with : s;
        const bool me = g.host ? pc == 0 : pc == g.local;
        std::string n = r.name.empty() ? "Player " + std::to_string(pc + 1) : r.name;
        // PC Mode (protocol 16): the seat's character ("Name (you), G, 38 ms"; two seats on one PC: "G + G 2")
        std::string ch = r.pcChar >= 0 ? std::string(", ") + charName(r.pcChar) : std::string();
        if (!ch.empty() && s2 >= 0 && g.roster[s2].pcChar >= 0)
            ch += std::string(" + ") + charName(g.roster[s2].pcChar);
        const std::string tail = r.with >= 0 ? std::string(me ? " (you)" : "") + ch + ", P" + std::to_string(r.with + 1) + "'s PC"
                                 : s == 0 ? std::string(g.host ? " (you)" : "") + ch + ", host"
                                          : std::string(me ? " (you)" : "") + ch + ", " + std::to_string(r.pingMs) + " ms";
        const int room = game::Frontend::kExtrasWidth - fe.rowTextWidth(label) - 16;
        if (!textMode_ && fe.rowTextWidth("A") > 0 && fe.rowTextWidth(n + tail) > room) {
            while (n.size() > 1 && fe.rowTextWidth(n + ".." + tail) > room) util::utf8PopBack(n);   // (misc18: a letter)
            n += "..";
        }
        return n + tail;
    };
    switch (page_) {
    case Page::Main:
        rows.push_back(X{"Host a Game", nullptr, [this, &fe]() { status_.clear(); show(fe, Page::Host); }});
        rows.back().help = "Start a game: you get a code to give your friends";
        rows.push_back(X{"Join a Game", nullptr, [this, &fe]() { lanPick_ = {}; status_.clear(); show(fe, Page::Join); }});
        rows.back().help = "Join a friend's game: type or paste the code they gave you";
        rows.push_back(X{"Your Name", edited("Your Name", playerName), [this, &fe]() { startEdit(fe, playerName, kMaxName); }});
        rows.back().help = "Your name in the waiting rooms (up to 16 letters)";
        rows.push_back(X{"In-game Info", [this]() { return std::string(netInfoName(netInfo)); }, [this]() {
                             netInfo = (netInfo + 1) % 3;
                             if (saveSettings) saveSettings();
                         }});
        rows.back().help = "In play: the ping at the bottom of the screen (F3 switches Small / Detailed / Off)";
        break;
    case Page::Host:
        rows.push_back(X{"Start Hosting", nullptr, [this, &fe]() {
                             if (base.code.empty() || normaliseCode(base.code).empty()) password_ = randomCode();   // (a new code each time)
                             startHost(fe);
                         }});
        rows.back().help = "Opens your game and shows the code your friends type to join";
        // 4P: up to four PCs (one seat each, the host relays), or two PCs with two seats each
        rows.push_back(X{"4P Online", [this]() { return std::string(base.pcs == 4 ? "Up to 4 PCs" : "2 PCs"); },
                         [this]() { base.pcs = base.pcs == 4 ? 2 : 4; }});
        rows.back().help = "Up to 4 PCs: one or two players each. 2 PCs: one friend (with 4 players: two on each PC)";
        if (base.pcs == 4) {   // the host's own seats: P1, or P1 + P3 (a second mouse / pad / gun on this PC)
            rows.push_back(X{"Your Seats", [this]() { return std::string(base.seats == 2 ? "2 (P1 + P3)" : "1 (P1)"); },
                             [this]() { base.seats = base.seats == 2 ? 1 : 2; }});
            rows.back().help = "2: two people play on this PC (Player 2 input plays P3)";
        }
        // PC Mode online (docs/pcmode.md "Online"): the host decides; each player picks a character on their own PC.
        // Boss Mode online (protocol 18): the host picks the boss (or All Bosses); every PC plays it
        rows.push_back(X{"Game Mode", [this]() { return std::string(base.bossMode ? "Boss Mode" : base.pcMode ? "PC Mode" : "Arcade"); },
                         [this]() {
                             if (base.bossMode)
                                 base.bossMode = 0;
                             else if (base.pcMode)
                                 base.pcMode = false, base.bossMode = 6;
                             else
                                 base.pcMode = true;
                             if (saveSettings) saveSettings();
                         }});
        rows.back().help = "PC Mode: the 1998 PC port's mode (characters; Continues). Boss Mode: the bosses only, against the clock";
        if (base.bossMode) {
            rows.push_back(X{"Boss", [this]() { return std::string(game::kBossNames[std::clamp(base.bossMode, 2, 6) - 2]); },
                             [this]() {
                                 base.bossMode = base.bossMode >= 6 ? 2 : base.bossMode + 1;
                                 if (saveSettings) saveSettings();   // (launcher.ini netbossmode=)
                             }});
            rows.back().help = "The boss everyone fights (All Bosses: the four in a row); no continues";
        }
        if (base.pcMode) {
            rows.push_back(X{"Your Character", [this]() { return pickName(base.pcChar[0], 1); },
                             [this]() { base.pcChar[0] = nextPick(base.pcChar[0]); if (saveSettings) saveSettings(); }});
            rows.back().help = "Your PC Mode character (friends pick their own on their PCs)";
            if ((base.pcs == 4 && base.seats == 2) || (base.pcs != 4 && hostSettings && hostSettings().seats == 4)) {
                rows.push_back(X{"P3 Character", [this]() { return pickName(base.pcChar[1], 3); },
                                 [this]() { base.pcChar[1] = nextPick(base.pcChar[1]); if (saveSettings) saveSettings(); }});
                rows.back().help = "The PC Mode character of this PC's second player (P3)";
            }
        }
        rows.push_back(X{"Game Name", edited("Game Name", name), [this, &fe]() { startEdit(fe, name, 24); }});
        rows.back().help = "The name players on your network see in their list";
        if (!status_.empty())
            problem(status_);
        break;
    case Page::Hosting: {
        // the join code: beside its label when it fits, else on a row of its own (never cut); a click copies it
        const std::string code = hasGame() ? shown().share : std::string();
        const int lw = fe.rowTextWidth("Your Code"), vw = fe.rowTextWidth(code);
        const bool oneRow = textMode_ || lw == 0 || lw + 16 + vw <= game::Frontend::kExtrasWidth;
        const std::string codeHelp = "Give this code to your friends. It is also the password: share it privately";
        rows.push_back(X{"Your Code", [this, oneRow]() { return oneRow && hasGame() ? shown().share : std::string(); },
                         [this]() { copyCode(); }});
        rows.back().help = codeHelp;
        if (!oneRow) {
            rows.push_back(X{code, nullptr, [this]() { copyCode(); }});
            rows.back().help = codeHelp;
        }
        rows.push_back(X{"Copy Code", [this]() { return std::string(copiedShown() ? "Copied!" : ""); }, [this]() { copyCode(); }});
        rows.back().help = "Copies the code: paste it in a chat with Ctrl+V";
        // PC Mode: this PC's character(s) (the joiners' show on their rows); an arcade game: no row (as before)
        if (hasGame() && shown().roster[0].pcChar >= 0) {
            rows.push_back(X{"PC Mode", [this]() {
                                 if (!hasGame()) return std::string();
                                 const Shown g = shown();
                                 std::string t = std::string("you: ") + charName(g.roster[0].pcChar);
                                 if (g.roster[2].in && g.roster[2].with == 0 && g.roster[2].pcChar >= 0)
                                     t += std::string(" + ") + charName(g.roster[2].pcChar);
                                 else if (!g.multi && hostSettings && hostSettings().seats == 4 && g.roster[2].pcChar >= 0)
                                     t += std::string(" + ") + charName(g.roster[2].pcChar);
                                 return t;
                             }, nullptr});
            rows.back().help = "PC Mode: every player plays the character they picked (shown beside their name)";
        }
        if (hasGame() && shown().multi) {   // up to four PCs: each seat's PC, its name and ping
            for (int s = 1; s <= 3; ++s) {
                const std::string label = "Player " + std::to_string(s + 1);
                rows.push_back(X{label, [seatText, s, label]() { return seatText(s, label); }, nullptr});
                rows.back().help = "Who has joined (and their ping: under 80 ms is great)";
            }
        } else {
            const bool four = hostSettings && hostSettings().seats == 4;
            const std::string label = four ? "Players 2 + 4" : "Player 2";
            rows.push_back(X{label, [seatText, label, four]() { return seatText(1, label, four ? 3 : -1); }, nullptr});
            rows.back().help = "Who has joined (and their ping: under 80 ms is great)";
        }
        const bool in = hasGame() && shown().peerIn;
        rows.push_back(X{"Start Game", nullptr,
                         in ? std::function<void()>([this]() {
                             if (game_ && hostSettings) { settings_ = hostSettings(); game_->hostStart(settings_); }
                         }) : std::function<void()>()});
        rows.back().help = in ? "Everyone who joined plays now" : "Waits until a friend has joined";
        // alone now; player 2 can join the running game later (the LAN list shows it as joinable) and presses start
        // (left out once someone is in: Start Game then; the rows stay on one page)
        if (!in) {
            rows.push_back(X{"Play Alone", nullptr,
                             hasGame() ? std::function<void()>([this]() {
                                 if (game_ && hostSettings) { settings_ = hostSettings(); settings_.players = 1; game_->hostStart(settings_); }
                             }) : std::function<void()>()});
            rows.back().help = "Start now: friends can still join your running game with the code";
        }
        rows.push_back(X{"Stop Hosting", nullptr, [this, &fe]() { cancel(); show(fe, Page::Host); }});
        rows.back().help = "Closes your game (the code stops working)";
        rows.push_back(X{"Internet", [this]() { return hasGame() ? shown().internet : std::string(); }, nullptr});
        if (hasGame()) {
            const Shown g = shown();
            rows.back().help = g.internet.rfind("Open", 0) == 0
                ? "Your router opened the port: the code works over the internet and on your network"
                : "Internet: forward UDP " + g.lan.substr(g.lan.rfind(':') + 1) +
                  " to this PC (" + g.lan.substr(0, g.lan.rfind(':')) + "); joiners type address + password " + g.code;
        }
        if (hasGame() && shown().wrong > 0)   // someone typed a wrong code (a typo, or a guesser)
            rows.push_back(X{"Wrong Codes", [this]() {
                                 return hasGame() ? std::to_string(shown().wrong) + " tried (20 = locked)" : std::string();
                             }, nullptr});
        if (!status_.empty())
            problem(status_);
        break;
    }
    case Page::Join: {
        rows.push_back(X{"Join Code", edited("Join Code", joinText_), [this, &fe]() {
                             lanPick_ = {};
                             joinAfterEdit_ = true;   // (Enter joins)
                             startEdit(fe, joinText_, 48);
                         }});
        rows.back().help = "Type the host's code (16 letters, dashes optional), then Enter";
        rows.push_back(X{"Paste Code", nullptr, [this, &fe]() { pasteCode(fe); }});
        rows.back().help = "Pastes a code you copied (Ctrl+V works too)";
        if (boxIsAddress()) {   // (the manual way: the host's address, then its password)
            rows.push_back(X{"Password", edited("Password", joinPassword_), [this, &fe]() { startEdit(fe, joinPassword_, 16); }});
            rows.back().help = "The host's password (on the host's Internet line)";
        }
        rows.push_back(X{"Join", nullptr, [this, &fe]() { joinTyped(fe); }});
        rows.back().help = "Connects to the host";
        rows.push_back(X{"Your Seats", [this]() { return std::to_string(base.seats); }, [this]() { base.seats = base.seats == 2 ? 1 : 2; }});
        rows.back().help = "2: two people play on this PC (a game of up to 4 PCs, when two seats are free)";
        // PC Mode online: this PC's character(s), used when the host's game is PC Mode (Auto: the seat's default)
        rows.push_back(X{"Your Character", [this]() { return pickName(base.pcChar[0], 0); },
                         [this]() { base.pcChar[0] = nextPick(base.pcChar[0]); if (saveSettings) saveSettings(); }});
        rows.back().help = "Your character when the host plays PC Mode (Auto: Rogan / G / Rogan 2 / G 2 by seat)";
        if (base.seats == 2) {
            rows.push_back(X{"2nd Character", [this]() { return pickName(base.pcChar[1], 0); },
                             [this]() { base.pcChar[1] = nextPick(base.pcChar[1]); if (saveSettings) saveSettings(); }});
            rows.back().help = "The PC Mode character of this PC's second player";
        }
        int listed = 0;
        for (const LobbyGame& g : previewing_ ? fakeLobby_ : lobby_.games()) {
            if (listed == 3) break;
            ++listed;
            const bool full = g.players >= g.maxPlayers;
            const bool ok = g.build == buildId() && !g.inGame;
            const Address addr = g.addr;
            const std::string info = g.build != buildId() ? "other version" : g.inGame ? (full ? "full" : "playing")
                                     : "Ch " + std::to_string(g.chapter + 1) + "  " + std::to_string(g.players) + "/" + std::to_string(g.maxPlayers);
            std::string shownName = util::utf8Head(g.name, 24);   // (the menu's font: shortened to "Name.." beside the info)
            if (!textMode_ && fe.rowTextWidth("A") > 0) {
                const int room = game::Frontend::kExtrasWidth - fe.rowTextWidth(info) - 16;
                if (fe.rowTextWidth(shownName) > room) {
                    while (shownName.size() > 1 && fe.rowTextWidth(shownName + "..") > room) util::utf8PopBack(shownName);
                    shownName += "..";
                }
            }
            // a game open on the LAN (the announcement holds its password, 2026-10-30): a click joins; else the code first
            const std::string lanCode = g.needsCode ? std::string() : g.code;
            rows.push_back(X{shownName, [info]() { return info; },
                             ok ? std::function<void()>([this, &fe, addr, lanCode]() {
                                 lanPick_ = addr;
                                 if (!lanCode.empty()) {
                                     startJoin(fe, addr.str(), lanCode);
                                     return;
                                 }
                                 joinAfterEdit_ = true;
                                 startEdit(fe, joinText_, 48);   // the code, then Enter joins this game
                             }) : std::function<void()>()});
            rows.back().help = g.build != buildId() ? "Another version of the game: both PCs need the same build"
                               : g.inGame         ? "Already playing, no free seat"
                               : lanCode.empty()  ? "On your network: click, type its code, Enter"
                                                  : "On your network: click to join (no code needed)";
        }
        if (!listed) {
            rows.push_back(X{"Your Network", [this]() { return std::string(listening_ ? "No games yet" : "Not available"); }, nullptr});
            rows.back().help = "Games hosted on your network show up here";
        }
        if (!status_.empty())
            problem(status_);
        break;
    }
    case Page::Joining:
        if (hasGame()) {
            rows.push_back(X{"Status", [this]() { return util::utf8Head(status_, 34); }, nullptr});
            // the waiting room: the host's roster (every seat's name and ping); this PC marked "(you)"
            if (shown().peerIn) {
                const Shown g = shown();
                const int seats = g.multi ? 4 : 2;
                const bool four = !g.multi && g.seats == 4;
                if (g.roster[0].pcChar >= 0) {   // (the host's PC Mode game; an arcade game: no row, as before)
                    rows.push_back(X{"Game Mode", []() { return std::string("PC Mode"); }, nullptr});
                    rows.back().help = "The host's choice: each player plays the character beside their name";
                } else if (g.roster[0].boss >= 2 && g.roster[0].boss <= 6) {   // (the host's Boss Mode game, protocol 18)
                    const int b = g.roster[0].boss;
                    rows.push_back(X{"Game Mode", [b]() { return "Boss Mode: " + std::string(game::kBossNames[b - 2]); }, nullptr});
                    rows.back().help = "The host's choice: the bosses only, against the clock; no continues";
                }
                for (int s = 0; s < seats; ++s) {
                    const std::string label = s == 0 ? std::string("Host") : four ? "Players " + std::to_string(s + 1) + " + " + std::to_string(s + 3)
                                                                                  : "Player " + std::to_string(s + 1);
                    const int s2 = four ? s + 2 : -1;
                    rows.push_back(X{label, [seatText, s, s2, label]() { return seatText(s, label, s2); }, nullptr});
                }
            }
        } else if (!status_.empty()) {
            problem(status_);
            rows.push_back(X{" ", nullptr, nullptr});   // (a gap before Back)
        }
        rows.push_back(X{hasGame() ? "Cancel" : "Back", nullptr, [this, &fe]() { cancel(); status_.clear(); show(fe, Page::Join); }});
        break;
    }
    if (edit_)   // typing: the keyboard, or a controller's letter picker
        rows.push_back(X{"Pad", [] { return std::string("Up Dn letter  Rt next  Lt del"); }, nullptr});
    return rows;
}

void OnlineMenu::build(game::Frontend& fe)
{
    using X = game::Frontend::Extra;
    std::vector<X> rows = makeRows(fe);
    auto exitFn = [this, &fe]() {
        if (edit_) { endEdit(fe, false); return; }
        if (page_ == Page::Main) {
            if (listening_) { lobby_.stop(); listening_ = false; }
            if (back_) back_();
            return;
        }
        cancel();
        status_.clear();
        show(fe, page_ == Page::Hosting ? Page::Host : page_ == Page::Joining ? Page::Join : Page::Main);
    };
    signature_ = signatureOf(rows);
    rows_ = rows;
    exit_ = exitFn;
    if (textMode_)   // the text launcher draws rows_ itself
        return;
    fe.extras = rows;
    fe.extrasTitle = kTitle;
    fe.extrasExit = exitFn;
    fe.screenExtras();
}

void OnlineMenu::script(game::Frontend& fe, const std::string& cmd)
{
    if (cmd == "edit-name") {   // (the letter picker's test: the name field empty, editing)
        show(fe, Page::Host);
        name.clear();
        startEdit(fe, name, 24);
    } else if (cmd == "pc-picks") {   // (launcher.ini netpcmode= / netpcchar=: Game Mode and the characters as clicked, saved)
        show(fe, Page::Host);
        base.pcMode = !base.pcMode;
        base.pcChar[0] = nextPick(nextPick(base.pcChar[0]));
        base.pcChar[1] = nextPick(base.pcChar[1]);
        if (saveSettings) saveSettings();
    } else if (cmd == "boss-picks") {   // (launcher.ini netbossmode=: Game Mode to Boss Mode, then the Boss row once, saved)
        show(fe, Page::Host);
        if (!base.bossMode) base.pcMode = false, base.bossMode = 6;
        base.bossMode = base.bossMode >= 6 ? 2 : base.bossMode + 1;
        if (saveSettings) saveSettings();
    } else if (cmd == "host-alone" || cmd == "host-alone-boss") {   // Play Alone at once (player 2 joins the running game)
        if (cmd == "host-alone-boss")   // (Boss Mode online: the Chariot)
            base.pcMode = false, base.bossMode = 2;
        show(fe, Page::Host);
        startHost(fe);
        if (game_ && hostSettings) {
            settings_ = hostSettings();
            settings_.players = 1;
            game_->hostStart(settings_);
        }
    } else if (cmd == "auto-host") {   // Light Gun Edition: --online-host
        show(fe, Page::Host);
        startHost(fe);
        if (game_) copyCode();
        scriptHost_ = game_ != nullptr;
    } else if (cmd.rfind("auto-join:", 0) == 0) {   // Light Gun Edition: --online-join [code]
        show(fe, Page::Join);
        scriptLanOpen_ = true;
        Address a;
        std::string pw, text = cmd.substr(10);
        autoJoinCode_ = !text.empty() && text.size() < 80 && parseShareCode(text, a, pw) ? text : std::string();
        autoJoinAt_ = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        if (autoJoinCode_.empty())
            status_ = "Looking for a game on your network... (over the internet: copy the host's code, then Paste Code)";
        build(fe);
    } else if (cmd == "host") {
        show(fe, Page::Host);
        startHost(fe);
        scriptHost_ = true;
    } else if (cmd.rfind("join:", 0) == 0) {
        // (menuhost: through the Join page's box as a player types it: a join code, a share code, or address|password)
        const size_t bar = cmd.find('|');
        show(fe, Page::Join);
        joinText_ = cmd.substr(5, bar == std::string::npos ? std::string::npos : bar - 5);
        joinPassword_ = bar == std::string::npos ? std::string() : cmd.substr(bar + 1);
        joinTyped(fe);
    } else if (cmd.rfind("join-lan:", 0) == 0) {   // (menuhost: the LAN list's first game clicked, its code typed, Enter)
        show(fe, Page::Join);
        scriptLan_ = cmd.substr(9);
    } else if (cmd == "join-lan-open") {   // (the LAN list's first game open on the LAN clicked: no code typed)
        show(fe, Page::Join);
        scriptLanOpen_ = true;
    }
}

void OnlineMenu::frame(game::Frontend& fe)
{
    if (listening_)
        lobby_.poll(0);
    if (!scriptLan_.empty() && page_ == Page::Join && !game_)   // (testing: join-lan:<code>, once the list shows a game)
        for (const LobbyGame& g : lobby_.games())
            if (g.build == buildId() && !g.inGame) {
                if (log) log("online test: the LAN list's \"" + g.name + "\" at " + g.addr.str() + " clicked");
                lanPick_ = g.addr;
                joinText_ = scriptLan_;
                scriptLan_.clear();
                joinTyped(fe);
                break;
            }
    if (scriptLanOpen_ && page_ == Page::Join && !game_)   // (testing: join-lan-open, as a click on the game's row)
        for (const LobbyGame& g : lobby_.games())
            if (g.build == buildId() && !g.inGame && !g.needsCode) {
                if (log) log("online test: the LAN list's \"" + g.name + "\" at " + g.addr.str() + " clicked (no code)");
                scriptLanOpen_ = false;
                lanPick_ = g.addr;
                startJoin(fe, g.addr.str(), g.code);
                break;
            }
    if (!autoJoinCode_.empty() && page_ == Page::Join && !game_ && std::chrono::steady_clock::now() >= autoJoinAt_) {
        scriptLanOpen_ = false;   // (Light Gun Edition, auto-join: no game open on this network: the code)
        joinText_ = autoJoinCode_;
        autoJoinCode_.clear();
        status_.clear();
        joinTyped(fe);
    }
    if (autoJoinAt_ != std::chrono::steady_clock::time_point{} && page_ != Page::Join && !game_) {
        scriptLanOpen_ = false;   // (auto-join: the player left the Join page: nothing more by itself)
        autoJoinCode_.clear();
        autoJoinAt_ = {};
    }
    if (!pendingJoin_.empty() && page_ == Page::Join && !game_) {   // a typed code waiting for the LAN list (joinTyped)
        bool heard = false;
        Address a;
        std::string pw;
        if (parseJoinCode(pendingJoin_, a, pw) > 0)
            for (const LobbyGame& g : lobby_.games())
                heard = heard || (g.addr.port == a.port && (g.addr.ip == a.ip || (g.publicIp && g.publicIp == a.ip)));
        if (heard || std::chrono::steady_clock::now() >= pendingUntil_) {
            joinText_ = pendingJoin_;
            pendingJoin_.clear();
            status_.clear();
            noDefer_ = true;
            joinTyped(fe);
        }
    } else if (!pendingJoin_.empty() && page_ != Page::Join) {
        pendingJoin_.clear();
    }
    if (scriptHost_ && game_ && game_->peerIn() && game_->session().pingSamples() >= 6 && !game_->session().released() && hostSettings) {
        settings_ = hostSettings();   // (testing: the Start button)
        game_->hostStart(settings_);
    }
    if (game_ && !ready_) {
        std::string err;
        const int r = game_->poll(settings_, err, 0);
        if (r > 0)
            ready_ = true;
        else if (r < 0) {
            status_ = err;
            // no answer, yet the host's LAN announcements arrive from that PC: its firewall drops the game's packets
            // (Windows asks on the first host; "Public" networks block it)
            const Address peer = game_->session().peer();
            if (!game_->session().isHost() && err.rfind("No answer", 0) == 0)
                for (const LobbyGame& g : lobby_.games())
                    if (g.addr.ip == peer.ip) {
                        status_ = "The host's PC is on your network but its game does not answer: on the host's PC allow "
                                  "hotd.exe in Windows Firewall (Private networks; Windows Security > Firewall > Allow an "
                                  "app), or set its network to Private. Then try again.";
                        break;
                    }
            if (log) log("online: " + status_);
            game_->close();
            game_.reset();
            if (page_ == Page::Hosting) page_ = Page::Host;
        } else if (page_ == Page::Joining) {
            status_ = game_->peerIn() ? "Waiting for the host to start" : "Connecting...";
            if (game_->peerIn() && game_->session().rosterReceived && !rosterLogged_ && log) {   // (once: the tests read it)
                rosterLogged_ = true;
                Session::RosterSeat r[4];
                game_->roster(r);
                std::string s = "online: in the host's waiting room:";
                for (int k = 0; k < (game_->multi() ? 4 : 2); ++k)
                    s += " [" + std::to_string(k + 1) + "] " +
                         (r[k].in ? (r[k].name.empty() ? std::string("?") : r[k].name) + " " + std::to_string(r[k].pingMs) + " ms"
                                  : std::string("free"));
                log(s);
            }
            if (game_->peerIn() && game_->silentSeconds() > 5.0) {   // the host stopped hosting / left
                status_ = "The host left (or the connection dropped). Ask the host to host again, then join again.";
                if (log) log("online: the host is gone");
                cancel();
            }
        } else if (page_ == Page::Hosting && game_->multi() && !game_->waitingLeft.empty()) {
            status_ = game_->waitingLeft;   // up to four PCs: a seat's PC left the waiting room (its seat is free again)
            game_->waitingLeft.clear();
        } else if (page_ == Page::Hosting && !game_->multi() && game_->peerIn() && game_->silentSeconds() > 5.0) {
            // player 2 left the waiting room: hosting again (the same password), open for the next one
            const std::string who = game_->session().peerName();
            if (log) log("player 2 left the waiting room; hosting again");
            cancel();
            startHost(fe);
            status_ = (who.empty() ? std::string("Player 2") : who) + " left the waiting room. Still hosting.";
        }
    }
    if (!textMode_ && !active(fe))
        return;
    // the values or the rows changed (players, names, pings, the LAN list, a problem): the page again
    if (signatureOf(makeRows(textMode_ ? textFrontend() : fe)) != signature_ || (page_ == Page::Hosting && !game_))
        build(fe);
}

} // namespace hotd::net
