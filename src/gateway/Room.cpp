// ============================================================================
// src/gateway/Room.cpp — implementacion de Room (ver el header)
// ============================================================================

#include "Room.hpp"

#include <algorithm>

#include "security/SecretsStore.hpp" // randomSecretHex

namespace ba::gateway {

using json = nlohmann::json;

namespace {

/// `prefix` + `hexChars` caracteres hex aleatorios (randomSecretHex entrega
/// como minimo 16 bytes; se recorta).
std::string randomId(const char* prefix, size_t hexChars) {
    std::string hex;
    if (!ba::security::randomSecretHex(16, hex)) return {};
    return std::string(prefix) + hex.substr(0, hexChars);
}

const char* stateName(Room::State s) { return s == Room::State::Admitted ? "admitted" : "waiting"; }

/// Recorta a `max` bytes sin partir un caracter UTF-8.
std::string utf8Truncate(const std::string& s, size_t max) {
    if (s.size() <= max) return s;
    size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80)
        --n;
    return s.substr(0, n);
}

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

Room::Room(Cfg cfg) : cfg_(std::move(cfg)) {
    if (cfg_.maxGuests < 1) cfg_.maxGuests = 1;
}

void Room::setSinks(Sinks sinks) {
    std::lock_guard<std::mutex> lock(m_);
    sinks_ = std::move(sinks);
}

void Room::setHostInfo(const std::string& pin, const std::string& guestUrl) {
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        pin_ = pin;
        guestUrl_ = guestUrl;
        out.roomChanged = true;
    }
    flush(out);
}

std::string Room::cleanName(const std::string& raw) {
    std::string s;
    for (char c : raw) {
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F) s.push_back(c);
    }
    s = utf8Truncate(trim(s), 40);
    return s.empty() ? std::string("Invitado") : s;
}

std::string Room::cleanText(const std::string& raw) {
    std::string s;
    for (char c : raw) {
        if (c == '\n' || (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F)) s.push_back(c);
    }
    return utf8Truncate(trim(s), 1000);
}

// --- Invitado ---------------------------------------------------------------

Room::Joined Room::connect(const std::string& name, const std::string& ip, int64_t nowMs,
                           const std::string& presetId) {
    Joined res;
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        lastNowMs_ = nowMs;
        if (locked_) {
            res.err = kLocked;
            return res;
        }
        const auto waiting = std::count_if(people_.begin(), people_.end(),
                                           [](const Participant& p) { return p.state == State::Waiting; });
        if (waiting >= cfg_.maxWaiting) {
            res.err = kLobbyFull;
            return res;
        }
        Participant p;
        p.id = presetId.empty() ? randomId("p", 12) : presetId;
        if (findLocked(p.id) != nullptr) {
            res.err = kBadState;
            return res;
        }
        p.key = randomId("", 32);
        if (p.id.empty() || p.key.empty()) {
            res.err = kBadState; // sin entropia: no se emiten claves predecibles
            return res;
        }
        p.name = cleanName(name);
        p.ip = ip;
        p.connected = true;
        p.joinedMs = nowMs;
        p.lastSeenMs = nowMs;
        people_.push_back(p);
        res.id = p.id;
        res.key = p.key;

        out.guest.emplace_back(p.id, json{{"type", "hello"},
                                          {"id", p.id},
                                          {"key", p.key},
                                          {"name", p.name},
                                          {"room", cfg_.name},
                                          {"lobby", cfg_.lobby}});
        if (!cfg_.lobby) {
            autoAdmitLocked(nowMs, out);
        }
        assignLobbySlotsLocked(out);
        lobbyPositionsLocked(out);
        out.roomChanged = true;
    }
    flush(out);
    return res;
}

int Room::resume(const std::string& id, const std::string& key, int64_t nowMs) {
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        lastNowMs_ = nowMs;
        Participant* p = findLocked(id);
        if (p == nullptr) return kNotFound;
        if (key.empty() || p->key != key) return kBadKey;
        p->connected = true;
        p->lastSeenMs = nowMs;
        out.guest.emplace_back(p->id, json{{"type", "hello"},
                                           {"id", p->id},
                                           {"key", p->key},
                                           {"name", p->name},
                                           {"room", cfg_.name},
                                           {"lobby", cfg_.lobby},
                                           {"resumed", true}});
        if (p->state == State::Admitted) {
            out.guest.emplace_back(p->id, json{{"type", "admitted"}, {"slot", p->slot}});
        } else if (!cfg_.lobby) {
            autoAdmitLocked(nowMs, out);
        }
        if (p->state == State::Waiting && p->lobbySlot > 0) {
            out.guest.emplace_back(p->id, json{{"type", "talk"}, {"on", p->talk}});
        }
        assignLobbySlotsLocked(out);
        lobbyPositionsLocked(out);
        out.roomChanged = true;
    }
    flush(out);
    return kOk;
}

void Room::disconnect(const std::string& id, int64_t nowMs) {
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        lastNowMs_ = nowMs;
        Participant* p = findLocked(id);
        if (p == nullptr) return;
        p->connected = false;
        p->mediaUp = false;
        p->lastSeenMs = nowMs;
        out.roomChanged = true;
    }
    flush(out);
}

void Room::leave(const std::string& id) {
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        removeLocked(id, out);
    }
    flush(out);
}

void Room::guestMedia(const std::string& id, const json& media) {
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        Participant* p = findLocked(id);
        if (p == nullptr || !media.is_object()) return;
        const auto flag = [&](const char* k, bool& v) {
            if (media.contains(k) && media[k].is_boolean() && media[k].get<bool>() != v) {
                v = media[k].get<bool>();
                out.roomChanged = true;
            }
        };
        flag("mic", p->mic);
        flag("cam", p->cam);
        flag("screen", p->screen);
        flag("hand", p->hand);
    }
    flush(out);
}

void Room::setMediaUp(const std::string& id, bool up) {
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        Participant* p = findLocked(id);
        if (p == nullptr || p->mediaUp == up) return;
        p->mediaUp = up;
        out.roomChanged = true;
    }
    flush(out);
}

int Room::chatFromGuest(const std::string& id, const std::string& text, int64_t nowMs) {
    const std::string t = cleanText(text);
    if (t.empty()) return kEmpty;
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        lastNowMs_ = nowMs;
        const Participant* p = findLocked(id);
        if (p == nullptr) return kNotFound;
        ChatMsg c;
        c.atMs = nowMs;
        c.fromId = p->id;
        c.fromName = p->name;
        // Desde el lobby solo lo lee el operador; desde adentro, toda la sala.
        c.to = p->state == State::Admitted ? "all" : "host";
        c.text = t;
        pushChatLocked(std::move(c), out);
    }
    flush(out);
    return kOk;
}

// --- Operador ---------------------------------------------------------------

int Room::admit(const std::string& id, int64_t nowMs) {
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        lastNowMs_ = nowMs;
        Participant* p = findLocked(id);
        if (p == nullptr) return kNotFound;
        if (p->state != State::Waiting) return kBadState;
        const int slot = freeSlotLocked();
        if (slot == 0) return kFull;
        admitLocked(*p, slot, nowMs, out);
        lobbyPositionsLocked(out);
    }
    flush(out);
    return kOk;
}

int Room::reject(const std::string& id) {
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        Participant* p = findLocked(id);
        if (p == nullptr) return kNotFound;
        if (p->state != State::Waiting) return kBadState;
        out.guest.emplace_back(id, json{{"type", "rejected"}});
        out.drop.push_back(id);
        removeLocked(id, out);
    }
    flush(out);
    return kOk;
}

int Room::kick(const std::string& id) {
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        if (findLocked(id) == nullptr) return kNotFound;
        out.guest.emplace_back(id, json{{"type", "kicked"}});
        out.drop.push_back(id);
        removeLocked(id, out);
    }
    flush(out);
    return kOk;
}

int Room::setTalk(const std::string& id, bool on) {
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        Participant* p = findLocked(id);
        if (p == nullptr) return kNotFound;
        if (p->state != State::Waiting || p->lobbySlot == 0) return kBadState;
        if (p->talk == on) return kOk;
        p->talk = on;
        out.routes.emplace_back(p->id, routeFor(*p));
        out.guest.emplace_back(p->id, json{{"type", "talk"}, {"on", on}});
        out.roomChanged = true;
    }
    flush(out);
    return kOk;
}

int Room::request(const std::string& id, const std::string& what) {
    static const char* kAllowed[] = {"mute-mic", "mute-cam", "stop-screen", "lower-hand"};
    if (std::none_of(std::begin(kAllowed), std::end(kAllowed), [&](const char* a) { return what == a; })) {
        return kBadState;
    }
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        Participant* p = findLocked(id);
        if (p == nullptr) return kNotFound;
        out.guest.emplace_back(id, json{{"type", "request"}, {"what", what}});
        if (what == "lower-hand" && p->hand) {
            p->hand = false;
            out.roomChanged = true;
        }
    }
    flush(out);
    return kOk;
}

int Room::chatFromHost(const std::string& to, const std::string& text, int64_t nowMs) {
    const std::string t = cleanText(text);
    if (t.empty()) return kEmpty;
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        lastNowMs_ = nowMs;
        const std::string dest = to.empty() ? std::string("all") : to;
        if (dest != "all" && findLocked(dest) == nullptr) return kNotFound;
        ChatMsg c;
        c.atMs = nowMs;
        c.fromId = "host";
        c.fromName = "Operador";
        c.to = dest;
        c.text = t;
        pushChatLocked(std::move(c), out);
    }
    flush(out);
    return kOk;
}

void Room::setLobby(bool on, int64_t nowMs) {
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        lastNowMs_ = nowMs;
        if (cfg_.lobby == on) return;
        cfg_.lobby = on;
        if (!on) {
            autoAdmitLocked(nowMs, out);
            lobbyPositionsLocked(out);
        }
        out.roomChanged = true;
    }
    flush(out);
}

void Room::setLocked(bool on) {
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        if (locked_ == on) return;
        locked_ = on;
        out.roomChanged = true;
    }
    flush(out);
}

// --- Archivos ---------------------------------------------------------------

std::string Room::addShare(Share share, int64_t nowMs) {
    Out out;
    std::string id;
    {
        std::lock_guard<std::mutex> lock(m_);
        id = randomId("s", 12);
        if (id.empty()) return {};
        share.id = id;
        share.atMs = nowMs;
        if (const Participant* p = findLocked(share.participantId)) share.participantName = p->name;
        shares_.push_back(share);
        out.guest.emplace_back(
            share.participantId,
            json{{"type", "shared"}, {"id", id}, {"name", share.name}, {"kind", share.kind}});
        out.roomChanged = true;
    }
    flush(out);
    return id;
}

bool Room::removeShare(const std::string& id, std::string* pathOut) {
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        const auto it =
            std::find_if(shares_.begin(), shares_.end(), [&](const Share& s) { return s.id == id; });
        if (it == shares_.end()) return false;
        if (pathOut != nullptr) *pathOut = it->path;
        shares_.erase(it);
        out.roomChanged = true;
    }
    flush(out);
    return true;
}

bool Room::share(const std::string& id, Share& out) const {
    std::lock_guard<std::mutex> lock(m_);
    for (const Share& s : shares_) {
        if (s.id == id) {
            out = s;
            return true;
        }
    }
    return false;
}

std::vector<Room::Share> Room::sharesOf(const std::string& participantId) const {
    std::lock_guard<std::mutex> lock(m_);
    std::vector<Share> res;
    for (const Share& s : shares_) {
        if (s.participantId == participantId) res.push_back(s);
    }
    return res;
}

// --- Consultas --------------------------------------------------------------

int Room::slotOf(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_);
    const Participant* p = findLocked(id);
    return (p != nullptr && p->state == State::Admitted) ? p->slot : 0;
}

MediaRoute Room::routeOf(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_);
    const Participant* p = findLocked(id);
    return p != nullptr ? routeFor(*p) : MediaRoute{};
}

bool Room::admittedByKey(const std::string& key, Participant& out) const {
    if (key.empty()) return false;
    std::lock_guard<std::mutex> lock(m_);
    for (const Participant& p : people_) {
        if (p.key == key && p.state == State::Admitted) {
            out = p;
            return true;
        }
    }
    return false;
}

bool Room::participant(const std::string& id, Participant& out) const {
    std::lock_guard<std::mutex> lock(m_);
    const Participant* p = findLocked(id);
    if (p == nullptr) return false;
    out = *p;
    return true;
}

json Room::snapshot() const {
    std::lock_guard<std::mutex> lock(m_);
    return snapshotLocked();
}

void Room::tick(int64_t nowMs) {
    Out out;
    {
        std::lock_guard<std::mutex> lock(m_);
        lastNowMs_ = nowMs;
        std::vector<std::string> gone;
        for (const Participant& p : people_) {
            if (!p.connected && nowMs - p.lastSeenMs >= cfg_.graceMs) gone.push_back(p.id);
        }
        for (const std::string& id : gone)
            removeLocked(id, out);
    }
    flush(out);
}

// --- Internos ---------------------------------------------------------------

Room::Participant* Room::findLocked(const std::string& id) {
    for (Participant& p : people_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

const Room::Participant* Room::findLocked(const std::string& id) const {
    for (const Participant& p : people_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

int Room::freeSlotLocked() const {
    for (int s = 1; s <= cfg_.maxGuests; ++s) {
        const bool used = std::any_of(people_.begin(), people_.end(), [s](const Participant& p) {
            return p.state == State::Admitted && p.slot == s;
        });
        if (!used) return s;
    }
    return 0;
}

void Room::admitLocked(Participant& p, int slot, int64_t nowMs, Out& out) {
    p.state = State::Admitted;
    p.slot = slot;
    p.admittedMs = nowMs;
    // Deja su lugar de preview del lobby: lo toma el siguiente que espera.
    p.lobbySlot = 0;
    p.talk = false;
    out.guest.emplace_back(p.id, json{{"type", "admitted"}, {"slot", slot}});
    out.routes.emplace_back(p.id, routeFor(p));
    assignLobbySlotsLocked(out);
    out.roomChanged = true;
}

void Room::removeLocked(const std::string& id, Out& out) {
    const auto it =
        std::find_if(people_.begin(), people_.end(), [&](const Participant& p) { return p.id == id; });
    if (it == people_.end()) return;
    if (it->state == State::Admitted && it->slot > 0) out.freed.emplace_back(it->slot, it->id);
    out.routes.emplace_back(it->id, MediaRoute{});
    people_.erase(it);
    out.roomChanged = true;
    // Sin lobby, el lugar que se libera lo toma el siguiente de la fila.
    if (!cfg_.lobby) autoAdmitLocked(lastNowMs_, out);
    assignLobbySlotsLocked(out);
    lobbyPositionsLocked(out);
}

void Room::autoAdmitLocked(int64_t nowMs, Out& out) {
    for (Participant& p : people_) {
        // Un desconectado no pasa: tomaria un slot que nadie usa. Entra
        // cuando se reengancha (resume) si todavia hay lugar.
        if (p.state != State::Waiting || !p.connected) continue;
        const int slot = freeSlotLocked();
        if (slot == 0) break;
        admitLocked(p, slot, nowMs, out);
    }
}

MediaRoute Room::routeFor(const Participant& p) {
    if (p.state == State::Admitted && p.slot > 0) return MediaRoute::program(p.slot);
    if (p.state == State::Waiting && p.lobbySlot > 0) return MediaRoute::lobby(p.lobbySlot, p.talk);
    return {};
}

void Room::assignLobbySlotsLocked(Out& out) {
    for (Participant& p : people_) {
        if (p.state != State::Waiting || p.lobbySlot != 0) continue;
        int free = 0;
        for (int k = 1; k <= cfg_.lobbySlots && free == 0; ++k) {
            const bool used = std::any_of(people_.begin(), people_.end(), [k](const Participant& o) {
                return o.state == State::Waiting && o.lobbySlot == k;
            });
            if (!used) free = k;
        }
        if (free == 0) break; // todos los lugares de preview ocupados
        p.lobbySlot = free;
        out.routes.emplace_back(p.id, routeFor(p));
        out.roomChanged = true;
    }
}

void Room::lobbyPositionsLocked(Out& out) {
    int pos = 0;
    for (const Participant& p : people_) {
        if (p.state != State::Waiting) continue;
        ++pos;
        out.guest.emplace_back(p.id, json{{"type", "lobby"}, {"position", pos}});
    }
}

void Room::pushChatLocked(ChatMsg msg, Out& out) {
    msg.seq = ++chatSeq_;
    chat_.push_back(msg);
    while (chat_.size() > cfg_.chatKeep)
        chat_.pop_front();
    const json j = toJson(msg);
    out.hosts.emplace_back("chat", j);
    const json g = json{{"type", "chat"}, {"msg", j}};
    if (msg.to == "all") {
        // A toda la sala: los admitidos. Los del lobby solo reciben lo que el
        // operador les manda (a todos o a ellos), no la charla de adentro.
        for (const Participant& p : people_) {
            if (p.state == State::Admitted || msg.fromId == "host") out.guest.emplace_back(p.id, g);
        }
    } else if (msg.to != "host") {
        out.guest.emplace_back(msg.to, g);
    }
    // Copia al propio autor invitado cuando no le llega por "all".
    if (msg.fromId != "host" && msg.to != "all") out.guest.emplace_back(msg.fromId, g);
}

json Room::toJson(const Participant& p) {
    return json{{"id", p.id},
                {"name", p.name},
                {"state", stateName(p.state)},
                {"slot", p.slot},
                {"lobbySlot", p.lobbySlot},
                {"talk", p.talk},
                {"connected", p.connected},
                {"mediaUp", p.mediaUp},
                {"mic", p.mic},
                {"cam", p.cam},
                {"screen", p.screen},
                {"hand", p.hand},
                {"joinedMs", p.joinedMs},
                {"admittedMs", p.admittedMs}};
}

json Room::toJson(const Share& s) {
    return json{{"id", s.id},
                {"participantId", s.participantId},
                {"participantName", s.participantName},
                {"name", s.name},
                {"kind", s.kind},
                {"mime", s.mime},
                {"path", s.path},
                {"bytes", s.bytes},
                {"atMs", s.atMs}};
}

json Room::toJson(const ChatMsg& c) {
    return json{{"seq", c.seq},           {"atMs", c.atMs}, {"fromId", c.fromId},
                {"fromName", c.fromName}, {"to", c.to},     {"text", c.text}};
}

json Room::snapshotLocked() const {
    json people = json::array();
    int admitted = 0;
    for (const Participant& p : people_) {
        people.push_back(toJson(p));
        if (p.state == State::Admitted) ++admitted;
    }
    json shares = json::array();
    for (const Share& s : shares_)
        shares.push_back(toJson(s));
    json chat = json::array();
    for (const ChatMsg& c : chat_)
        chat.push_back(toJson(c));
    return json{{"room", cfg_.name},
                {"lobby", cfg_.lobby},
                {"locked", locked_},
                {"maxGuests", cfg_.maxGuests},
                {"slotsFree", cfg_.maxGuests - admitted},
                {"pin", pin_},
                {"guestUrl", guestUrl_},
                {"participants", people},
                {"shares", shares},
                {"chat", chat}};
}

void Room::flush(Out& out) {
    Sinks sinks;
    json snap;
    {
        std::lock_guard<std::mutex> lock(m_);
        sinks = sinks_;
        if (out.roomChanged) snap = snapshotLocked();
    }
    if (sinks.toGuest) {
        for (auto& [id, msg] : out.guest)
            sinks.toGuest(id, msg);
    }
    if (sinks.route) {
        for (auto& [id, r] : out.routes)
            sinks.route(id, r);
    }
    if (sinks.slotFreed) {
        for (auto& [slot, id] : out.freed)
            sinks.slotFreed(slot, id);
    }
    if (sinks.dropGuest) {
        for (auto& id : out.drop)
            sinks.dropGuest(id);
    }
    if (sinks.toHosts) {
        for (auto& [ev, data] : out.hosts)
            sinks.toHosts(ev, data);
        if (out.roomChanged) sinks.toHosts("room", snap);
    }
}

} // namespace ba::gateway
