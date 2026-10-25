// ============================================================================
// src/app/tests/test_atencion.cpp — la atención a quien se conecta (sala/Atencion)
//
//   1. Quién atiende: recepción si está prendida; si no, automatización; nunca
//      el del evento (no es ni un parámetro).
//   2. Hablando, de a uno: el primero toma la voz, los demás van por chat, y
//      cuando el de la voz termina la toma el siguiente.
//   3. Cuándo termina: al admitirla (Admitido) o al irse (SeFue); los de la
//      asamblea no se mezclan con los de la sala.
//   4. En la asamblea nunca hay voz.
//   5. Apagar «Hablando» pasa a todos a chat; apagar «Los atiende» termina todo.
//   6. El nombre que cuenta para anotarlo solo en Personas.
//   7. Los ajustes: ida y vuelta por JSON, y lo que viene mal queda de fábrica.
// ============================================================================
#include <cstdio>
#include <set>
#include <string>

#include "sala/Atencion.hpp"

using namespace ba::app::sala;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (false)

void quienAtiendeEnCadaCaso() {
    CHECK(quienAtiende(true, true) == Atendedor::Recepcion, "con recepción prendida, atiende recepción");
    CHECK(quienAtiende(true, false) == Atendedor::Recepcion, "recepción no necesita a automatización");
    CHECK(quienAtiende(false, true) == Atendedor::Automatizacion, "sin recepción, automatización");
    CHECK(quienAtiende(false, false) == Atendedor::Nadie, "sin ninguno, nadie");
    CHECK(std::string(claveDe(Atendedor::Recepcion)) == "recepcion", "clave de recepción");
    CHECK(std::string(claveDe(Atendedor::Automatizacion)) == "automatizacion", "clave de automatización");
    CHECK(std::string(claveDe(Atendedor::Nadie)).empty(), "nadie no tiene clave");
    CHECK(!porQueNadie(Atendedor::Nadie).empty(), "si no atiende nadie, se dice por qué");
    CHECK(porQueNadie(Atendedor::Automatizacion).empty(), "si alguien atiende, no hay excusa");
}

void hablandoDeAUno() {
    Turnos t;
    const auto ana = t.empezar("a", "Ana", Ambito::Sala, Atendedor::Recepcion, true);
    CHECK(ana.modo == Modo::Voz, "la primera toma la voz");
    const auto beto = t.empezar("b", "Beto", Ambito::Sala, Atendedor::Recepcion, true);
    CHECK(beto.modo == Modo::Chat, "el segundo va por chat: hay un solo canal de voz");
    CHECK(t.conLaVoz() == "a", "Ana tiene la voz");
    const auto otraVez = t.empezar("a", "Ana", Ambito::Sala, Atendedor::Recepcion, true);
    CHECK(otraVez.modo == Modo::Voz && t.todos().size() == 2, "empezar de nuevo no duplica el turno");
    const auto sinHablar = t.empezar("c", "Ceci", Ambito::Sala, Atendedor::Recepcion, false);
    CHECK(sinHablar.modo == Modo::Chat, "sin «Hablando», chat");

    // Ana entra: la voz pasa a Beto (el primero que se atendía por chat).
    const auto fin = t.actualizar(Ambito::Sala, {"b", "c"}, {"a"});
    CHECK(fin.size() == 1 && fin[0].id == "a" && fin[0].fin == Fin::Admitido && fin[0].teniaLaVoz,
          "Ana termina admitida y tenía la voz");
    CHECK(t.conLaVoz().empty(), "la voz quedó libre");
    CHECK(t.pasarLaVoz() == "b", "la toma Beto, que llegó antes que Ceci");
    CHECK(t.turno("b")->modo == Modo::Voz, "Beto ahora por voz");
    CHECK(t.pasarLaVoz().empty(), "si ya la tiene alguien, no se pasa");
}

void cuandoTermina() {
    Turnos t;
    t.empezar("a", "Ana", Ambito::Sala, Atendedor::Automatizacion, false);
    t.empezar("i1", "Integrante", Ambito::Asamblea, Atendedor::Automatizacion, false);
    auto fin = t.actualizar(Ambito::Sala, {}, {});
    CHECK(fin.size() == 1 && fin[0].id == "a" && fin[0].fin == Fin::SeFue, "se fue sin entrar: SeFue");
    CHECK(t.turno("i1") != nullptr, "el de la asamblea no se toca al mirar la sala");
    fin = t.actualizar(Ambito::Asamblea, {"i1"}, {});
    CHECK(fin.empty(), "sigue esperando: sigue el turno");
    fin = t.actualizar(Ambito::Asamblea, {}, {"i1"});
    CHECK(fin.size() == 1 && fin[0].fin == Fin::Admitido, "habilitado: Admitido");
    CHECK(t.todos().empty(), "no queda nadie");
}

void enLaAsambleaNoHayVoz() {
    Turnos t;
    const auto i = t.empezar("i1", "Integrante", Ambito::Asamblea, Atendedor::Recepcion, true);
    CHECK(i.modo == Modo::Chat, "en la asamblea, aunque se pida hablando, no toma la voz");
    CHECK(t.conLaVoz().empty(), "la voz sigue libre para la sala");
    CHECK(t.pasarLaVoz().empty(), "y no se le pasa a uno de la asamblea");
    const auto ana = t.empezar("a", "Ana", Ambito::Sala, Atendedor::Recepcion, true);
    CHECK(ana.modo == Modo::Voz, "la sala sí la toma");
}

void apagar() {
    Turnos t;
    t.empezar("a", "Ana", Ambito::Sala, Atendedor::Recepcion, true);
    t.empezar("b", "Beto", Ambito::Sala, Atendedor::Recepcion, true);
    t.empezar("i1", "Integrante", Ambito::Asamblea, Atendedor::Recepcion, false);
    CHECK(t.soltarLaVoz() == "a", "apagar «Hablando» dice quién tenía la voz");
    CHECK(t.conLaVoz().empty() && t.turno("a")->modo == Modo::Chat, "y todos pasan a chat");
    const auto ids = t.terminarTodos(Ambito::Sala);
    CHECK(ids.size() == 2, "apagar «Los atiende» de la sala termina los dos de la sala");
    CHECK(t.todos().size() == 1 && t.turno("i1") != nullptr, "y deja al de la asamblea");
}

void elNombreQueCuenta() {
    CHECK(nombreDado("  Ana   María  Pérez ") == "Ana María Pérez", "espacios de más afuera");
    CHECK(nombreDado("Ñandú") == "Ñandú", "con letras no ASCII");
    CHECK(nombreDado("Beto") == "Beto", "un solo nombre alcanza");
    CHECK(nombreDado("Invitado 3").empty(), "el que pone el navegador no cuenta");
    CHECK(nombreDado("invitada").empty(), "en minúscula tampoco");
    CHECK(nombreDado("iPhone de Juan").empty(), "el nombre del celular no cuenta");
    CHECK(nombreDado("Juan123").empty(), "un nombre no lleva números");
    CHECK(nombreDado("J").empty(), "una letra es un apuro");
    CHECK(nombreDado("   ").empty(), "vacío no cuenta");
    CHECK(nombreDado("").empty(), "nada no cuenta");
}

void ajustesIdaYVuelta() {
    AjustesAtencion a;
    a.salaHablando = true;
    a.asambleaAtiende = true;
    a.vozRepo = "vits-piper-es_AR-daniela-high";
    a.vozHablante = 1;
    a.vozVelocidad = 1.2;
    const AjustesAtencion b = AjustesAtencion::deJson(a.aJson());
    CHECK(b.salaHablando && b.asambleaAtiende, "los interruptores vuelven");
    CHECK(b.vozRepo == a.vozRepo && b.vozHablante == 1 && b.vozVelocidad == 1.2, "la voz vuelve");

    const AjustesAtencion c = AjustesAtencion::deJson(
        nlohmann::json{{"salaHablando", "si"}, {"voz", {{"velocidad", 9.0}, {"hablante", -3}}}});
    CHECK(!c.salaHablando, "un texto donde va un sí/no queda de fábrica");
    CHECK(c.vozVelocidad == 1.5, "la velocidad se acota");
    CHECK(c.vozHablante == 0, "un hablante negativo es el primero");
    CHECK(!AjustesAtencion::deJson(nlohmann::json::array()).asambleaAtiende, "algo que no es un objeto: de fábrica");
}

} // namespace

int main() {
    quienAtiendeEnCadaCaso();
    hablandoDeAUno();
    cuandoTermina();
    enLaAsambleaNoHayVoz();
    apagar();
    elNombreQueCuenta();
    ajustesIdaYVuelta();
    if (g_fallos == 0) std::printf("test_atencion: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
