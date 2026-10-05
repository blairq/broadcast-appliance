// test_room.cpp
//
// Modelo de la sala (Room) y almacen de archivos (ShareStore), sin sockets.
//
// Casos:
//   1. Lobby: entra en espera, posiciones FIFO, admitir asigna slots 1..N,
//      sala llena -> kFull, rechazar/expulsar liberan y avisan.
//   2. Lobby apagado: admision automatica, y al liberarse un slot entra el
//      siguiente de la fila.
//   3. Reconexion: la desconexion conserva el lugar `graceMs`; resume con la
//      clave correcta reengancha (y re-avisa "admitted"); clave mala -> kBadKey;
//      vencido el plazo, tick() lo purga y libera el slot.
//   4. Chat: desde el lobby solo lo ve el operador; desde adentro, la sala;
//      el operador puede escribirle a uno solo; texto saneado.
//   5. Sala cerrada: connect -> kLocked.
//   6. Pedidos validos/invalidos.
//   7. Los sinks se llaman SIN el lock tomado (un sink que consulta la sala no
//      se traba).
//   9. Modo oradores: admitir = a la fila (backstage, sin slot); al aire en
//      modo "uno" reemplaza, en "varios" usa el slot por defecto o el primero
//      libre; reordenar; tiempo propio; el reloj NUNCA saca a nadie en modo
//      manual (sigue en negativo); carrusel "chain" y "time" (con margen);
//      perfil editable; tablero sin claves.
//   8. ShareStore: deteccion por contenido (png, jpeg, mp4, webm, mp3, wav) y
//      rechazo de lo que no es medio (ejecutable, pdf, heic); guardado en
//      streaming, tope de tamaño, archivo corto.

#include "Room.hpp"
#include "ShareStore.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using ba::gateway::Room;
using ba::gateway::ShareStore;
using json = nlohmann::json;

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

/// Registro de todo lo que la sala emite, para inspeccionarlo.
struct Recorder {
    std::map<std::string, std::vector<json>> guest;
    std::vector<std::pair<std::string, json>> hosts;
    std::vector<std::string> dropped;
    std::vector<int> freed;

    void attach(Room& room) {
        Room::Sinks s;
        s.toGuest = [this](const std::string& id, const json& m) { guest[id].push_back(m); };
        s.toHosts = [this](const std::string& ev, const json& d) { hosts.emplace_back(ev, d); };
        s.dropGuest = [this](const std::string& id) { dropped.push_back(id); };
        s.slotFreed = [this](int slot, const std::string&) { freed.push_back(slot); };
        room.setSinks(s);
    }
    /// Ultimo mensaje de tipo `type` para `id` (null si no hubo).
    json last(const std::string& id, const std::string& type) const {
        const auto it = guest.find(id);
        if (it == guest.end()) return nullptr;
        for (auto m = it->second.rbegin(); m != it->second.rend(); ++m) {
            if (m->value("type", "") == type) return *m;
        }
        return nullptr;
    }
    int count(const std::string& id, const std::string& type) const {
        const auto it = guest.find(id);
        if (it == guest.end()) return 0;
        int n = 0;
        for (const auto& m : it->second)
            n += m.value("type", "") == type ? 1 : 0;
        return n;
    }
};

json findParticipant(const json& snap, const std::string& id) {
    for (const auto& p : snap["participants"]) {
        if (p["id"] == id) return p;
    }
    return nullptr;
}

void testLobby() {
    Room::Cfg cfg;
    cfg.maxGuests = 2;
    Room room(cfg);
    Recorder rec;
    rec.attach(room);

    const auto a = room.connect("Ana", "10.0.0.1", 1000);
    const auto b = room.connect("Beto", "10.0.0.2", 1001);
    const auto c = room.connect("  Caro\x01  ", "10.0.0.3", 1002);
    CHECK(a.err == Room::kOk && b.err == Room::kOk && c.err == Room::kOk, "tres entran al lobby");
    CHECK(!a.key.empty() && a.key != b.key, "cada uno recibe una clave propia");
    CHECK(rec.last(a.id, "hello")["key"] == a.key, "el hello lleva la clave");
    CHECK(rec.last(c.id, "lobby")["position"] == 3, "posicion FIFO en la fila");
    CHECK(findParticipant(room.snapshot(), c.id)["name"] == "Caro",
          "nombre saneado (sin controles ni espacios)");
    CHECK(findParticipant(room.snapshot(), a.id)["state"] == "waiting", "espera en el lobby");
    CHECK(room.slotOf(a.id) == 0, "sin slot hasta ser admitido");

    CHECK(room.admit(b.id, 2000) == Room::kOk, "admitir al segundo");
    CHECK(rec.last(b.id, "admitted")["slot"] == 1, "recibe el slot 1");
    CHECK(rec.last(c.id, "lobby")["position"] == 2, "la fila avanza");
    CHECK(room.admit(b.id, 2001) == Room::kBadState, "no se admite dos veces");
    CHECK(room.admit(a.id, 2002) == Room::kOk, "admitir al primero");
    CHECK(room.slotOf(a.id) == 2, "slot 2");
    CHECK(room.admit(c.id, 2003) == Room::kFull, "sala llena");
    CHECK(room.snapshot()["slotsFree"] == 0, "sin slots libres");

    CHECK(room.kick(b.id) == Room::kOk, "expulsar");
    CHECK(rec.last(b.id, "kicked") != nullptr, "el expulsado se entera");
    CHECK(!rec.dropped.empty() && rec.dropped.back() == b.id, "y se corta su conexion");
    CHECK(!rec.freed.empty() && rec.freed.back() == 1, "su slot se libera (cierra la sesion WebRTC)");
    CHECK(room.admit(c.id, 3000) == Room::kOk && room.slotOf(c.id) == 1, "el slot libre se reusa");

    const auto d = room.connect("Dani", "10.0.0.4", 3001);
    CHECK(room.reject(d.id) == Room::kOk, "rechazar a uno del lobby");
    CHECK(rec.last(d.id, "rejected") != nullptr, "el rechazado se entera");
    CHECK(findParticipant(room.snapshot(), d.id) == nullptr, "y no queda en la sala");
    CHECK(room.reject(a.id) == Room::kBadState, "no se rechaza a uno de adentro (se expulsa)");
    CHECK(room.admit("pnoexiste", 1) == Room::kNotFound, "id inexistente");

    bool sawRoom = false;
    for (const auto& [ev, data] : rec.hosts)
        sawRoom = sawRoom || (ev == "room" && data.contains("participants"));
    CHECK(sawRoom, "los operadores reciben el estado");
    CHECK(!room.snapshot()["participants"][0].contains("key"), "la clave NO sale en el snapshot");
}

void testAutoAdmit() {
    Room::Cfg cfg;
    cfg.maxGuests = 1;
    cfg.lobby = false;
    Room room(cfg);
    Recorder rec;
    rec.attach(room);
    const auto a = room.connect("A", "", 0);
    const auto b = room.connect("B", "", 0);
    CHECK(room.slotOf(a.id) == 1, "sin lobby entra directo");
    CHECK(room.slotOf(b.id) == 0, "si no hay lugar, espera");
    room.leave(a.id);
    CHECK(room.slotOf(b.id) == 1, "al liberarse, entra el siguiente");
    CHECK(rec.count(b.id, "admitted") == 1, "y se le avisa una vez");

    Room::Cfg cfg2;
    cfg2.maxGuests = 2;
    Room room2(cfg2);
    Recorder rec2;
    rec2.attach(room2);
    const auto x = room2.connect("X", "", 0);
    CHECK(room2.slotOf(x.id) == 0, "con lobby, espera");
    room2.setLobby(false, 10);
    CHECK(room2.slotOf(x.id) == 1, "apagar el lobby hace pasar a los que esperan");

    // Un desconectado de la fila no toma el slot: pasa al reengancharse.
    Room::Cfg cfg3;
    cfg3.maxGuests = 1;
    cfg3.lobby = false;
    Room room3(cfg3);
    Recorder rec3;
    rec3.attach(room3);
    const auto p1 = room3.connect("P1", "", 0);
    const auto p2 = room3.connect("P2", "", 0);
    const auto p3 = room3.connect("P3", "", 0);
    room3.disconnect(p2.id, 1);
    room3.leave(p1.id);
    CHECK(room3.slotOf(p2.id) == 0, "el desconectado no toma el slot");
    CHECK(room3.slotOf(p3.id) == 1, "lo toma el siguiente conectado");
    room3.leave(p3.id);
    CHECK(room3.slotOf(p2.id) == 0, "sigue sin entrar mientras este desconectado");
    CHECK(room3.resume(p2.id, p2.key, 2) == Room::kOk && room3.slotOf(p2.id) == 1, "entra al reengancharse");
}

void testReconnect() {
    Room::Cfg cfg;
    cfg.graceMs = 1000;
    Room room(cfg);
    Recorder rec;
    rec.attach(room);
    const auto a = room.connect("A", "", 0);
    room.admit(a.id, 10);
    room.disconnect(a.id, 100);
    CHECK(findParticipant(room.snapshot(), a.id)["connected"] == false, "desconectado pero en la sala");
    room.tick(600);
    CHECK(room.slotOf(a.id) == 1, "dentro del plazo conserva el slot");
    CHECK(room.resume(a.id, "clave-mala", 700) == Room::kBadKey, "clave incorrecta");
    CHECK(room.resume(a.id, a.key, 700) == Room::kOk, "reengancha con su clave");
    CHECK(rec.count(a.id, "admitted") == 2, "se le re-avisa que esta admitido");
    room.disconnect(a.id, 800);
    room.tick(1900);
    CHECK(findParticipant(room.snapshot(), a.id) == nullptr, "vencido el plazo se purga");
    CHECK(!rec.freed.empty() && rec.freed.back() == 1, "y libera el slot");
    CHECK(room.resume(a.id, a.key, 2000) == Room::kNotFound, "ya no puede volver con esa clave");
}

void testChat() {
    Room room(Room::Cfg{});
    Recorder rec;
    rec.attach(room);
    const auto in1 = room.connect("Adentro1", "", 0);
    const auto in2 = room.connect("Adentro2", "", 0);
    const auto wait = room.connect("Afuera", "", 0);
    room.admit(in1.id, 1);
    room.admit(in2.id, 1);

    CHECK(room.chatFromGuest(in1.id, "hola sala", 5) == Room::kOk, "chat desde adentro");
    CHECK(rec.last(in2.id, "chat")["msg"]["text"] == "hola sala", "lo ve el otro de adentro");
    CHECK(rec.last(wait.id, "chat") == nullptr, "no lo ve el del lobby");

    CHECK(room.chatFromGuest(wait.id, "me dejan pasar?", 6) == Room::kOk, "chat desde el lobby");
    CHECK(rec.last(in1.id, "chat")["msg"]["text"] == "hola sala", "los de adentro no lo ven");
    bool hostSaw = false;
    for (const auto& [ev, d] : rec.hosts)
        hostSaw = hostSaw || (ev == "chat" && d["text"] == "me dejan pasar?" && d["to"] == "host");
    CHECK(hostSaw, "el operador si");
    CHECK(rec.last(wait.id, "chat")["msg"]["text"] == "me dejan pasar?", "el autor recibe su copia");

    CHECK(room.chatFromHost(wait.id, "ya te hago pasar", 7) == Room::kOk, "operador a uno solo");
    CHECK(rec.last(wait.id, "chat")["msg"]["text"] == "ya te hago pasar", "le llega al destinatario");
    CHECK(rec.last(in1.id, "chat")["msg"]["text"] == "hola sala", "a nadie mas");

    CHECK(room.chatFromHost("all", "arrancamos", 8) == Room::kOk, "operador a todos");
    CHECK(rec.last(wait.id, "chat")["msg"]["text"] == "arrancamos", "el operador llega tambien al lobby");
    CHECK(room.chatFromHost("all", "   \x02 ", 9) == Room::kEmpty, "mensaje vacio tras sanear");
    CHECK(room.chatFromHost("pnadie", "x", 9) == Room::kNotFound, "destinatario inexistente");
    CHECK(Room::cleanText(std::string(3000, 'a')).size() == 1000, "texto recortado a 1000 bytes");
    CHECK(room.snapshot()["chat"].size() == 4, "historial en el snapshot");
}

void testLockAndRequests() {
    Room room(Room::Cfg{});
    Recorder rec;
    rec.attach(room);
    const auto a = room.connect("A", "", 0);
    room.setLocked(true);
    CHECK(room.connect("B", "", 0).err == Room::kLocked, "sala cerrada");
    CHECK(room.resume(a.id, a.key, 1) == Room::kOk, "pero los que ya estan pueden reengancharse");
    CHECK(room.request(a.id, "mute-mic") == Room::kOk, "pedido valido");
    CHECK(rec.last(a.id, "request")["what"] == "mute-mic", "llega el pedido");
    CHECK(room.request(a.id, "formatear-disco") == Room::kBadState, "pedido desconocido");
    room.guestMedia(a.id, json{{"hand", true}, {"screen", true}});
    CHECK(findParticipant(room.snapshot(), a.id)["hand"] == true, "mano levantada");
    room.request(a.id, "lower-hand");
    CHECK(findParticipant(room.snapshot(), a.id)["hand"] == false, "el operador la baja");
}

void testLobbyMedia() {
    Room::Cfg cfg;
    cfg.maxGuests = 1;
    cfg.lobbySlots = 2;
    Room room(cfg);
    std::vector<std::pair<std::string, ba::gateway::MediaRoute>> routes;
    Recorder rec;
    rec.attach(room);
    Room::Sinks s;
    s.toGuest = [&](const std::string& id, const json& m) { rec.guest[id].push_back(m); };
    s.route = [&](const std::string& id, const ba::gateway::MediaRoute& r) { routes.emplace_back(id, r); };
    room.setSinks(s);
    using K = ba::gateway::MediaRoute::Kind;

    const auto a = room.connect("A", "", 0);
    const auto b = room.connect("B", "", 0);
    const auto c = room.connect("C", "", 0);
    CHECK(room.routeOf(a.id).kind == K::Lobby && room.routeOf(a.id).index == 1, "A: preview 1 del lobby");
    CHECK(room.routeOf(b.id).kind == K::Lobby && room.routeOf(b.id).index == 2, "B: preview 2 del lobby");
    CHECK(room.routeOf(c.id).kind == K::None, "C: sin lugar de preview (espera sin publicar)");
    CHECK(!room.routeOf(a.id).talk, "nadie habla con el operador de entrada");

    CHECK(room.setTalk(a.id, true) == Room::kOk, "hablarle en privado a A");
    CHECK(room.routeOf(a.id).talk, "la ruta de A pasa a 'talk'");
    CHECK(rec.last(a.id, "talk")["on"] == true, "A se entera");
    CHECK(room.setTalk(c.id, true) == Room::kBadState, "no se habla con quien no tiene preview");

    routes.clear();
    CHECK(room.admit(a.id, 5) == Room::kOk, "admitir a A");
    CHECK(room.routeOf(a.id).kind == K::Program && room.routeOf(a.id).index == 1,
          "A pasa al programa (misma sesion)");
    CHECK(!room.routeOf(a.id).talk, "ya no esta en conversacion privada");
    CHECK(room.routeOf(c.id).kind == K::Lobby && room.routeOf(c.id).index == 1,
          "C hereda el preview que dejo A");
    bool sawA = false, sawC = false;
    for (auto& [id, r] : routes) {
        sawA = sawA || (id == a.id && r.kind == K::Program);
        sawC = sawC || (id == c.id && r.kind == K::Lobby);
    }
    CHECK(sawA && sawC, "las rutas nuevas se informan al servidor");
    // Adentro, «hablar» es IFB (T33): la voz del operador mezclada con su retorno.
    CHECK(room.setTalk(a.id, true) == Room::kOk, "adentro se le puede hablar (IFB)");
    CHECK(room.routeOf(a.id).kind == K::Program && room.routeOf(a.id).talk, "sigue en el programa, con talk");
    CHECK(room.setTalk(a.id, false) == Room::kOk && !room.routeOf(a.id).talk, "y se corta");

    routes.clear();
    room.kick(b.id);
    bool none = false;
    for (auto& [id, r] : routes)
        none = none || (id == b.id && r.kind == K::None);
    CHECK(none, "al irse, su ruta pasa a None (se cierra su sesion)");
    CHECK(findParticipant(room.snapshot(), c.id)["lobbySlot"] == 1,
          "el snapshot muestra el lugar de preview");
}

void testCupo() {
    // T33: el cupo cambia en caliente dentro de lo reservado; bajarlo no saca a nadie.
    Room::Cfg cfg;
    cfg.maxGuests = 1;
    cfg.reserva = 3;
    cfg.lobby = false;
    Room room(cfg);
    const auto a = room.connect("A", "", 0);
    const auto b = room.connect("B", "", 0);
    CHECK(room.slotOf(a.id) == 1 && room.slotOf(b.id) == 0, "con cupo 1, B espera");
    CHECK(room.setCupo(2) == Room::kOk, "subir el cupo a 2 sin reiniciar");
    CHECK(room.slotOf(b.id) == 2, "sin lobby, B entra al lugar nuevo");
    CHECK(room.snapshot()["maxGuests"] == 2 && room.snapshot()["reserva"] == 3, "el snapshot lo muestra");
    CHECK(room.setCupo(4) == Room::kBadState, "más que lo reservado, no");
    CHECK(room.setCupo(1) == Room::kFull, "bajar a 1 con el lugar 2 ocupado, no");
    room.leave(b.id);
    CHECK(room.setCupo(1) == Room::kOk, "con el lugar 2 libre, sí");
    CHECK(room.setCupo(0) == Room::kBadState, "cupo 0, no");
}

void testSpeakers() {
    Room::Cfg cfg;
    cfg.maxGuests = 3;
    cfg.backstageSlots = 2; // solo los 2 primeros de la fila mandan preview
    Room room(cfg);
    std::vector<std::pair<std::string, ba::gateway::MediaRoute>> routes;
    Recorder rec;
    Room::Sinks s;
    s.toGuest = [&](const std::string& id, const json& m) { rec.guest[id].push_back(m); };
    s.route = [&](const std::string& id, const ba::gateway::MediaRoute& r) { routes.emplace_back(id, r); };
    s.slotFreed = [&](int slot, const std::string&) { rec.freed.push_back(slot); };
    room.setSinks(s);
    using K = ba::gateway::MediaRoute::Kind;

    CHECK(room.setSpeakerConfig(json{{"enabled", true}, {"defaultSec", 60}, {"defaultSlot", 2}}, 0) ==
              Room::kOk,
          "activar el modo oradores");
    const auto a = room.connect("Ana", "", 0, {}, "Vecina del barrio", "+54 11 5555");
    const auto b = room.connect("Beto", "", 0);
    const auto c = room.connect("Caro", "", 0);
    const auto d = room.connect("Dani", "", 0);
    for (const auto* p : {&a, &b, &c, &d})
        CHECK(room.admit(p->id, 10) == Room::kOk, "admitir a la fila no necesita slot (4 > 3 slots)");
    CHECK(room.routeOf(a.id).kind == K::Backstage, "en la fila ve el programa (backstage)");
    CHECK(room.slotOf(a.id) == 0, "sin slot mientras espera turno");
    CHECK(rec.last(b.id, "speaker")["position"] == 2, "sabe su lugar en la fila");
    CHECK(rec.last(b.id, "speaker")["etaSec"] == 60, "y cuanto falta (el tiempo del de adelante)");
    json snap = room.snapshot();
    CHECK(snap["order"].size() == 4 && snap["order"][0] == a.id, "orden de llegada");
    CHECK(findParticipant(snap, a.id)["contact"] == "+54 11 5555", "contacto cargado al entrar");
    CHECK(findParticipant(snap, a.id)["allottedSec"] == 60, "tiempo general al entrar");

    const auto bs = [&](const std::string& id) {
        return findParticipant(room.snapshot(), id)["backstageSlot"];
    };
    CHECK(bs(a.id) == 1 && bs(b.id) == 2 && bs(c.id) == 0, "preview de la fila para los 2 primeros");
    CHECK(room.routeOf(a.id).kind == K::Backstage && room.routeOf(a.id).index == 1,
          "su ruta sube la camara al preview 1 (y sigue viendo el programa)");
    CHECK(room.routeOf(c.id).index == 0, "el tercero no sube nada");
    CHECK(room.moveSpeaker(d.id, 0) == Room::kOk, "reordenar: Dani primero");
    CHECK(room.snapshot()["order"][0] == d.id, "Dani encabeza la fila");
    CHECK(bs(d.id) == 2 && bs(a.id) == 1 && bs(b.id) == 0,
          "al adelantarse Dani toma el preview que suelta Beto (Ana conserva el suyo)");
    CHECK(room.setAllotted(d.id, 30) == Room::kOk, "tiempo propio de Dani");

    // Uno a la vez (modo por defecto), en el slot por defecto.
    CHECK(room.nextSpeaker(1000) == Room::kOk, "siguiente: Dani");
    CHECK(room.slotOf(d.id) == 2 && room.routeOf(d.id).kind == K::Program, "al aire en el slot por defecto");
    CHECK(bs(d.id) == 0 && bs(b.id) == 2, "al aire suelta el preview de la fila: lo toma el siguiente");
    CHECK(rec.last(d.id, "speaker")["state"] == "onair" && rec.last(d.id, "speaker")["allottedSec"] == 30,
          "el invitado recibe su reloj");
    CHECK(room.setOnAir(a.id, true, 2000) == Room::kOk, "Ana al aire a mano");
    CHECK(room.slotOf(d.id) == 0 && room.routeOf(d.id).kind == K::Backstage,
          "Dani sale (uno a la vez) y no se va");
    CHECK(findParticipant(room.snapshot(), d.id)["speaker"] == "done", "Dani ya hablo");
    CHECK(findParticipant(room.snapshot(), d.id)["usedMs"] == 1000, "y se registra lo que uso");

    // Manual: el reloj nunca saca a nadie.
    room.tick(2000 + 600 * 1000);
    CHECK(room.slotOf(a.id) == 2, "pasado el tiempo sigue al aire (cuenta negativa)");
    CHECK(room.setOnAir(a.id, false, 700000) == Room::kOk, "el operador la saca");
    CHECK(room.slotOf(a.id) == 0 && room.routeOf(a.id).kind == K::Backstage,
          "vuelve a backstage, no se la echa");
    CHECK(!rec.freed.empty() && rec.freed.back() == 2, "se libera el slot");
    CHECK(room.slotOf(b.id) == 0, "manual: nadie entra solo");

    // Cadena: al sacar a uno, entra el siguiente.
    room.setSpeakerConfig(json{{"carousel", "chain"}}, 700000);
    CHECK(room.nextSpeaker(700000) == Room::kOk && room.slotOf(b.id) == 2, "Beto al aire");
    room.setOnAir(b.id, false, 710000);
    CHECK(room.slotOf(c.id) == 2, "cadena: Caro entra al sacar a Beto");

    // Tiempo: pasado cero + margen, rota.
    room.setSpeakerConfig(json{{"carousel", "time"}, {"graceSec", 5}}, 710000);
    CHECK(room.requeue(d.id) == Room::kOk, "Dani vuelve a la fila");
    room.tick(710000 + 64 * 1000);
    CHECK(room.slotOf(c.id) == 2, "antes del margen sigue");
    room.tick(710000 + 65 * 1000);
    CHECK(room.slotOf(c.id) == 0 && room.slotOf(d.id) == 2, "tiempo + margen: rota a Dani");
    room.tick(800000);
    room.setSpeakerConfig(json{{"carousel", "manual"}}, 800000);

    // Varios a la vez.
    room.setSpeakerConfig(json{{"multi", true}}, 800000);
    room.requeue(a.id);
    CHECK(room.setOnAir(a.id, true, 800000) == Room::kOk, "Ana al aire junto a quien este");
    CHECK(room.slotOf(a.id) != 0 && room.slotOf(a.id) != room.slotOf(d.id), "otro slot");
    CHECK(room.setOnAir(b.id, true, 800000) == Room::kOk, "tercero al aire");
    CHECK(room.setOnAir(c.id, true, 800000) == Room::kFull, "sin slots libres");

    CHECK(room.setProfile(c.id, json{{"name", "Carolina"}, {"contact", "caro@ej.com"}}) == Room::kOk,
          "el admin edita el perfil");
    CHECK(findParticipant(room.snapshot(), c.id)["name"] == "Carolina", "nombre editado");
    const json board = room.boardSnapshot(0);
    CHECK(board["order"].size() == 4 && board.dump().find(a.key) == std::string::npos,
          "tablero: fila sin claves");
    Room::Participant pa;
    CHECK(room.byKey(a.key, pa) && pa.id == a.id, "buscar por clave");
    CHECK(!room.byKey("", pa), "clave vacia no encuentra nada");

    room.kick(a.id);
    CHECK(room.snapshot()["order"].size() == 3, "al irse sale de la fila");
}

void testSinksUnlocked() {
    Room room(Room::Cfg{});
    int reentrant = 0;
    Room::Sinks s;
    // Si la sala llamara a los sinks con su mutex tomado, esta consulta se
    // trabaria (mutex no recursivo) y el test no terminaria.
    s.toHosts = [&](const std::string&, const json&) {
        reentrant += static_cast<int>(room.snapshot()["participants"].size());
    };
    room.setSinks(s);
    room.connect("A", "", 0);
    CHECK(reentrant >= 1, "los sinks corren sin el lock (pueden consultar la sala)");
}

void testShares() {
    Room room(Room::Cfg{});
    Recorder rec;
    rec.attach(room);
    const auto a = room.connect("A", "", 0);
    room.admit(a.id, 1);
    Room::Participant who;
    CHECK(room.admittedByKey(a.key, who) && who.id == a.id, "la clave autoriza al admitido");
    CHECK(!room.admittedByKey("", who), "clave vacia no autoriza");
    Room::Share s;
    s.participantId = a.id;
    s.name = "foto.png";
    s.kind = "image";
    s.path = "/tmp/x.png";
    const std::string id = room.addShare(s, 5);
    CHECK(!id.empty(), "archivo registrado");
    CHECK(room.snapshot()["shares"][0]["participantName"] == "A", "con el nombre de quien lo compartio");
    CHECK(rec.last(a.id, "shared")["id"] == id, "el invitado recibe la confirmacion");
    std::string path;
    CHECK(room.removeShare(id, &path) && path == "/tmp/x.png", "se quita y devuelve la ruta");
    CHECK(!room.removeShare(id, nullptr), "no se quita dos veces");
}

std::vector<uint8_t> bytes(std::initializer_list<int> v) {
    std::vector<uint8_t> out;
    for (int x : v)
        out.push_back(static_cast<uint8_t>(x));
    return out;
}

std::vector<uint8_t> withText(size_t off, const char* text, size_t total = 64) {
    std::vector<uint8_t> v(total, 0);
    std::memcpy(v.data() + off, text, std::strlen(text));
    return v;
}

void testSniff() {
    ShareStore::Kind k;
    auto png = bytes({0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0});
    CHECK(ShareStore::sniff(png.data(), png.size(), k) && k.ext == ".png", "png");
    auto jpg = bytes({0xFF, 0xD8, 0xFF, 0xE0, 0, 0});
    CHECK(ShareStore::sniff(jpg.data(), jpg.size(), k) && k.kind == "image", "jpeg");
    auto mp4 = withText(4, "ftypisom");
    CHECK(ShareStore::sniff(mp4.data(), mp4.size(), k) && k.ext == ".mp4" && k.kind == "video", "mp4");
    auto m4a = withText(4, "ftypM4A ");
    CHECK(ShareStore::sniff(m4a.data(), m4a.size(), k) && k.kind == "audio", "m4a es audio");
    auto heic = withText(4, "ftypheic");
    CHECK(!ShareStore::sniff(heic.data(), heic.size(), k), "heic se rechaza (el motor no lo decodifica)");
    auto webm = bytes({0x1A, 0x45, 0xDF, 0xA3, 'x', 'w', 'e', 'b', 'm'});
    CHECK(ShareStore::sniff(webm.data(), webm.size(), k) && k.ext == ".webm", "webm");
    auto mp3 = withText(0, "ID3");
    CHECK(ShareStore::sniff(mp3.data(), mp3.size(), k) && k.ext == ".mp3", "mp3");
    auto wav = withText(0, "RIFF");
    std::memcpy(wav.data() + 8, "WAVE", 4);
    CHECK(ShareStore::sniff(wav.data(), wav.size(), k) && k.kind == "audio", "wav");
    auto exe = withText(0, "MZ");
    CHECK(!ShareStore::sniff(exe.data(), exe.size(), k), "un ejecutable se rechaza");
    auto pdf = withText(0, "%PDF-1.7");
    CHECK(!ShareStore::sniff(pdf.data(), pdf.size(), k), "pdf todavia no se soporta");
    auto elf = bytes({0x7F, 'E', 'L', 'F', 2, 1});
    CHECK(!ShareStore::sniff(elf.data(), elf.size(), k), "un ELF se rechaza");
}

void testStore() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "ba-test-shares";
    fs::remove_all(dir);
    ShareStore store(dir, 4096);

    // Fuente que entrega de a pedacitos (como un socket).
    std::vector<uint8_t> data(3000, 0xAB);
    const auto png = bytes({0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A});
    std::copy(png.begin(), png.end(), data.begin());
    size_t pos = 0;
    const auto reader = [&](uint8_t* buf, size_t n) {
        const size_t take = std::min<size_t>({n, 7, data.size() - pos});
        if (take == 0) return 0;
        std::memcpy(buf, data.data() + pos, take);
        pos += take;
        return static_cast<int>(take);
    };
    const auto r = store.save(data.size(), reader);
    CHECK(r.err == ShareStore::kOk, "guardado en streaming");
    CHECK(r.path.extension() == ".png", "extension segun el contenido");
    CHECK(fs::file_size(r.path) == data.size(), "tamaño exacto");
    {
        std::ifstream f(r.path, std::ios::binary);
        std::vector<uint8_t> back((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(back == data, "contenido identico");
    }

    pos = 0;
    CHECK(store.save(5000, reader).err == ShareStore::kTooLarge, "tope de tamaño");
    pos = 0;
    CHECK(store.save(data.size() + 100, reader).err == ShareStore::kShort,
          "archivo mas corto que lo anunciado");
    std::vector<uint8_t> exe(100, 0);
    exe[0] = 'M';
    exe[1] = 'Z';
    size_t p2 = 0;
    const auto exeReader = [&](uint8_t* buf, size_t n) {
        const size_t take = std::min(n, exe.size() - p2);
        std::memcpy(buf, exe.data() + p2, take);
        p2 += take;
        return static_cast<int>(take);
    };
    CHECK(store.save(exe.size(), exeReader).err == ShareStore::kUnsupported, "tipo no soportado");

    size_t files = 0;
    bool partials = false;
    for (const auto& e : fs::directory_iterator(dir)) {
        ++files;
        partials = partials || e.path().extension() == ".part";
    }
    CHECK(files == 1, "solo queda el archivo bueno");
    CHECK(!partials, "no quedan temporales a medias");
    store.clear();
    CHECK(fs::is_empty(dir), "clear() limpia el directorio");
    fs::remove_all(dir);
}

} // namespace

int main() {
    std::printf("== test_room ==\n");
    testLobby();
    testAutoAdmit();
    testReconnect();
    testChat();
    testLockAndRequests();
    testLobbyMedia();
    testCupo();
    testSpeakers();
    testSinksUnlocked();
    testShares();
    testSniff();
    testStore();
    if (g_failures == 0) {
        std::printf("test_room: OK\n");
        return 0;
    }
    std::printf("test_room: %d fallo(s)\n", g_failures);
    return 1;
}
