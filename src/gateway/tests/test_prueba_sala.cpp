// test_prueba_sala.cpp
//
// La prueba de sala (PruebaSala.hpp) y lo que cambia en la sala (Room), sin sockets.
//
// Casos:
//   1. La credencial: corta o con plazo malo no abre; vale solo mientras está
//      abierta y sin vencer; al vencer no vale más; al cerrar tampoco.
//   2. Los tokens: un token anotado se canjea UNA vez; uno vencido no; cerrar
//      (o una credencial nueva) los olvida.
//   3. Cerrar devuelve los participantes de prueba (para echarlos) y deja todo
//      limpio; renovar conserva los participantes y el interruptor.
//   4. La entrada directa solo existe con la prueba activa y se apaga al cerrar.
//   5. La sala: con el lobby prendido, el de prueba pasa directo y queda marcado;
//      el real espera. Sin lugar, el de prueba espera (no saca a nadie).
//   6. La entrada directa: los que esperan pasan; al apagarla vuelve el lobby.
//   7. La limpieza: idsDePrueba da solo los de prueba; echarlos deja a los reales.

#include "PruebaSala.hpp"
#include "Room.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

using ba::gateway::PruebaSala;
using ba::gateway::Room;
using json = nlohmann::json;

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                     \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                 \
            ++g_failures;                                                                    \
        }                                                                                    \
    } while (0)

const std::string kCred = "0123456789abcdef0123456789abcdef";

void credencial() {
    PruebaSala p;
    CHECK(!p.abrir("corta", 1000, 60000), "una credencial corta no abre");
    CHECK(!p.abrir(kCred, 1000, 1000), "sin plazo no abre");
    CHECK(!p.abrir(kCred, 0, PruebaSala::kDuracionMaxMs + 1), "más larga que el tope no abre");
    CHECK(!p.credencialValida(kCred, 1000), "sin prueba no vale nada");
    CHECK(p.abrir(kCred, 1000, 61000), "abre");
    CHECK(p.activa(30000), "activa");
    CHECK(p.credencialValida(kCred, 30000), "vale mientras dura");
    CHECK(!p.credencialValida("otra-cosa-de-32-caracteres-xxxxx", 30000), "otra no vale");
    CHECK(!p.credencialValida("", 30000), "vacía no vale");
    CHECK(!p.credencialValida(kCred, 61000), "vencida no vale");
    CHECK(p.vencida(61000) && !p.activa(61000), "vencida y no activa");
    p.cerrar();
    CHECK(!p.vencida(61000) && !p.credencialValida(kCred, 30000), "cerrada: revocada");
}

void tokens() {
    PruebaSala p;
    p.anotarToken("t0", 0, 5000);
    CHECK(!p.tomarToken("t0", 100), "sin prueba no se anota nada");
    p.abrir(kCred, 0, 600000);
    p.anotarToken("t1", 0, 300000);
    p.anotarToken("t2", 0, 1000);
    CHECK(!p.tomarToken("nadie", 10), "un token que no salió de la prueba no es de prueba");
    CHECK(p.tomarToken("t1", 10), "el de la prueba se canjea");
    CHECK(!p.tomarToken("t1", 20), "una sola vez");
    CHECK(!p.tomarToken("t2", 2000), "vencido no");
    p.anotarToken("t3", 0, 300000);
    p.abrir("ffffffffffffffffffffffffffffffff", 10, 600000); // otra credencial
    CHECK(!p.tomarToken("t3", 20), "una credencial nueva olvida los tokens de la anterior");
    p.anotarToken("t4", 0, 300000);
    p.cerrar();
    CHECK(!p.tomarToken("t4", 20), "cerrar olvida los tokens");
}

void cerrarYRenovar() {
    PruebaSala p;
    p.abrir(kCred, 0, 600000);
    p.anotarParticipante("pa");
    p.anotarParticipante("pb");
    CHECK(p.setDirecta(true, 10), "la entrada directa se prende con la prueba activa");
    p.abrir(kCred, 100, 900000); // renovar
    CHECK(p.esDePrueba("pa") && p.directa(), "renovar conserva participantes e interruptor");
    CHECK(p.estado(100, true).value("credencial", "") == kCred, "el estado para la app trae la credencial");
    CHECK(!p.estado(100, false).contains("credencial"), "el que viaja a todos no");
    auto ids = p.cerrar();
    std::sort(ids.begin(), ids.end());
    CHECK(ids.size() == 2 && ids[0] == "pa" && ids[1] == "pb", "cerrar devuelve a los de prueba");
    CHECK(!p.directa() && p.participantes().empty() && !p.esDePrueba("pa"), "y queda limpio");
    CHECK(!p.setDirecta(true, 10), "sin prueba no hay entrada directa");
    CHECK(!p.directa(), "sigue apagada");
}

void sala() {
    Room::Cfg cfg;
    cfg.maxGuests = 2;
    cfg.lobby = true;
    Room room(cfg);
    room.setSinks({});
    const auto real = room.connect("Ana", "1.1.1.1", 1000);
    const auto p1 = room.connect("hinchada-1", "127.0.0.1", 1000, {}, {}, {}, true);
    Room::Participant a, b;
    CHECK(room.participant(real.id, a) && a.state == Room::State::Waiting && !a.prueba, "el real espera en el lobby");
    CHECK(room.participant(p1.id, b) && b.state == Room::State::Admitted && b.slot == 1 && b.prueba,
          "el de prueba pasa directo y queda marcado");
    const auto p2 = room.connect("hinchada-2", "127.0.0.1", 1000, {}, {}, {}, true);
    const auto p3 = room.connect("hinchada-3", "127.0.0.1", 1000, {}, {}, {}, true);
    Room::Participant c;
    CHECK(room.participant(p3.id, c) && c.state == Room::State::Waiting, "sin lugar, el de prueba espera");
    CHECK(room.participant(real.id, a) && a.state == Room::State::Waiting, "el real no pasó por la prueba");
    // El snapshot los distingue.
    int marcados = 0;
    const json snap = room.snapshot(); // no un temporal en el for (UB)
    for (const auto& p : snap["participants"])
        if (p.value("prueba", false)) ++marcados;
    CHECK(marcados == 3, "el snapshot marca a los tres de prueba");
    CHECK(room.idsDePrueba().size() == 3, "idsDePrueba: los tres");

    // Limpieza: echar a los de prueba deja al real; al liberarse lugares el real sigue
    // esperando (con lobby: lo admite el operador).
    for (const auto& id : room.idsDePrueba()) room.kick(id);
    CHECK(room.idsDePrueba().empty(), "no queda ninguno de prueba");
    CHECK(room.participant(real.id, a) && a.state == Room::State::Waiting, "el real sigue esperando");
    (void)p2;
}

void directa() {
    Room::Cfg cfg;
    cfg.maxGuests = 3;
    cfg.lobby = true;
    Room room(cfg);
    room.setSinks({});
    const auto real = room.connect("Beto", "1.1.1.1", 1000);
    room.setDirecta(true, 2000);
    Room::Participant a;
    CHECK(room.participant(real.id, a) && a.state == Room::State::Admitted, "con la entrada directa pasa el que esperaba");
    CHECK(room.snapshot().value("directa", false), "el snapshot lo dice");
    const auto otro = room.connect("Caro", "1.1.1.2", 3000);
    CHECK(room.participant(otro.id, a) && a.state == Room::State::Admitted, "y el que llega entra directo");
    room.setDirecta(false, 4000);
    const auto tarde = room.connect("Dani", "1.1.1.3", 5000);
    CHECK(room.participant(tarde.id, a) && a.state == Room::State::Waiting, "apagada, vuelve el lobby");
    CHECK(room.snapshot().value("lobby", false), "el lobby configurado no se tocó");
}

} // namespace

int main() {
    credencial();
    tokens();
    cerrarYRenovar();
    sala();
    directa();
    if (g_failures == 0) std::printf("test_prueba_sala: OK\n");
    return g_failures == 0 ? 0 : 1;
}
