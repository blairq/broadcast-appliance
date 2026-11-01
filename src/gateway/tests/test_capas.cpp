// ============================================================================
// src/gateway/tests/test_capas.cpp — la cámara en dos capas (Capas.hpp)
//
//   1. SelectorCapas: pedir otra capa prende las dos; la vieja sigue saliendo hasta
//      que llega la nueva, y recién ahí se pausa (sin parpadeo). «Nada» pausa todo.
//      Una conexión nueva arranca con las dos prendidas.
//   2. capaNecesaria: al aire o armado, alta; en el programa sin estar al aire, baja
//      (si la app ya dijo qué está al aire; si no, alta como siempre); el remoto con
//      la palabra y el siguiente, alta; lobby y vista previa, baja; la fila sin vista
//      previa, nada; una grilla sube al menos a la capa de su lugar; un espectador web,
//      alta.
//   3. WebrtcSession (backend NULL): sin los mids de las dos capas no se le pide nada
//      al navegador (uno de antes); con ellos, pedir una capa llama a onCapas solo
//      cuando cambia lo que tiene que mandar.
// ============================================================================

#include <cstdio>
#include <string>
#include <vector>

#include "Capas.hpp"
#include "Webrtc.hpp"

using namespace ba::gateway;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

bool activas(const SelectorCapas& s, bool alta, bool baja) { return s.activas() == CapasActivas{alta, baja}; }

void selector() {
    SelectorCapas s;
    CHECK(activas(s, true, true), "conexión nueva: el navegador arranca con las dos");
    CHECK(s.pedir(Capa::Baja), "pedir la baja cambia lo que manda");
    CHECK(activas(s, false, true), "nada salía todavía: solo la baja");
    bool pasa = true;
    CHECK(!s.llego(Capa::Alta, pasa) && !pasa, "un cuadro de la alta (todavía no la pausó) no sale");
    CHECK(!s.llego(Capa::Baja, pasa) && pasa, "la baja sale");
    CHECK(s.sale() == Capa::Baja, "y es la que sale");

    // Pasa al aire: las dos prendidas hasta que llega la alta.
    CHECK(s.pedir(Capa::Alta), "pedir la alta");
    CHECK(activas(s, true, true), "mientras no llega, las dos");
    CHECK(!s.llego(Capa::Baja, pasa) && pasa, "la baja sigue saliendo: no hay negro");
    CHECK(s.llego(Capa::Alta, pasa) && pasa, "llega la alta: sale, y cambia lo pedido");
    CHECK(activas(s, true, false), "la baja se pausa");
    CHECK(s.sale() == Capa::Alta, "sale la alta");
    CHECK(!s.llego(Capa::Baja, pasa) && !pasa, "un cuadro rezagado de la baja ya no sale");

    // Pedir lo mismo no cambia nada.
    CHECK(!s.pedir(Capa::Alta), "pedir la misma: nada que decirle");

    // Volver a la baja y arrepentirse antes de que llegue.
    CHECK(s.pedir(Capa::Baja) && activas(s, true, true), "a la baja: las dos");
    CHECK(s.pedir(Capa::Alta) && activas(s, true, false), "de vuelta a la alta antes de que llegue: solo la alta");
    CHECK(!s.llego(Capa::Baja, pasa) && !pasa, "la baja que llega tarde no sale");

    // Nada: pausa todo; al volver, se espera la pedida.
    CHECK(s.pedir(Capa::Nada) && activas(s, false, false), "nadie lo mira: nada");
    CHECK(s.sale() == Capa::Nada, "y no sale nada");
    CHECK(s.pedir(Capa::Baja) && activas(s, false, true), "vuelve a una miniatura: solo la baja (no la alta de antes)");
    CHECK(!s.llego(Capa::Baja, pasa) && pasa, "y sale");

    // Reconexión: el navegador nuevo arranca con las dos.
    s.reiniciar();
    CHECK(activas(s, true, true) && s.sale() == Capa::Nada, "reiniciar: las dos, nada sale");
    CHECK(s.pedir(Capa::Baja) && activas(s, false, true), "y se le vuelve a decir");
}

void necesidad() {
    NecesidadCapa n;
    n.ruta = MediaRoute::program(2);
    CHECK(capaNecesaria(n) == Capa::Alta, "en el programa, sin que la app diga nada: alta (como siempre)");
    n.appDijo = true;
    CHECK(capaNecesaria(n) == Capa::Baja, "la app dijo y no está al aire: baja (la vista chica y el multiview)");
    n.siguiente = true;
    CHECK(capaNecesaria(n) == Capa::Alta, "armado en el preview: alta de antemano");
    n.siguiente = false;
    n.alAire = true;
    CHECK(capaNecesaria(n) == Capa::Alta, "al aire: alta");

    n = {};
    n.ruta = MediaRoute::remoto(1);
    n.appDijo = true;
    CHECK(capaNecesaria(n) == Capa::Alta, "el remoto con la palabra: alta");
    n.ruta = MediaRoute::remoto(0);
    CHECK(capaNecesaria(n) == Capa::Alta, "el siguiente de la fila: alta (precargada)");

    n = {};
    n.ruta = MediaRoute::lobby(1, false);
    CHECK(capaNecesaria(n) == Capa::Baja, "en el lobby: baja (el cuadrito del operador)");
    n.ruta = MediaRoute::backstage(3);
    CHECK(capaNecesaria(n) == Capa::Baja, "en la fila con vista previa: baja");
    n.ruta = MediaRoute::backstage(0);
    CHECK(capaNecesaria(n) == Capa::Nada, "en la fila sin vista previa ni grilla: nada");
    n.altoGrilla = 90;
    CHECK(capaNecesaria(n) == Capa::Baja, "en un cuadrito de una grilla: baja");
    n.altoGrilla = 400;
    CHECK(capaNecesaria(n) == Capa::Alta, "en un lugar grande de una grilla: alta");
    n.altoGrilla = 0;
    n.espectadores = 1;
    CHECK(capaNecesaria(n) == Capa::Alta, "la mira una vista web: alta (se le reenvía esa)");

    n = {};
    n.ruta = MediaRoute::program(1);
    n.appDijo = true;
    n.altoGrilla = 120;
    CHECK(capaNecesaria(n) == Capa::Baja, "en el programa, no al aire, en un cuadrito: baja");
    n.ruta = MediaRoute::atencion();
    n.altoGrilla = 0;
    CHECK(capaNecesaria(n) == Capa::Nada, "la atención hablada no manda cámara");
}

void sesion() {
    WebrtcHub hub(1, 640, 360, 48000, 2);
    WebrtcSession* s = hub.session(1);
    std::vector<std::pair<bool, bool>> pedidos;
    SessionCallbacks cb;
    cb.onCapas = [&](bool a, bool b) { pedidos.emplace_back(a, b); };
    CHECK(s->configure(cb, hub.rtcFactory()), "sesión configurada");
    s->pedirCapa(Capa::Baja);
    CHECK(pedidos.empty() && !s->conCapas(), "un navegador de antes (sin capas): no se le pide nada");

    s->setCapas("1", "2");
    CHECK(s->conCapas(), "con los dos mids, dos capas");
    s->pedirCapa(Capa::Baja);
    CHECK(pedidos.size() == 1 && pedidos.back() == std::make_pair(false, true), "se le pide solo la baja");
    s->pedirCapa(Capa::Baja);
    CHECK(pedidos.size() == 1, "lo mismo otra vez: no se repite");
    s->pedirCapa(Capa::Nada);
    CHECK(pedidos.size() == 2 && pedidos.back() == std::make_pair(false, false), "nada: las dos pausadas");
    CHECK(s->capaPedida() == Capa::Nada, "la pedida queda anotada");

    // Conexión nueva (configure): arranca con las dos y hay que volver a decírselo.
    CHECK(s->configure(cb, hub.rtcFactory()), "reconfigurada");
    CHECK(!s->conCapas(), "la conexión nueva declara sus mids de nuevo");
    s->setCapas("1", "2");
    s->pedirCapa(Capa::Nada);
    CHECK(pedidos.size() == 3 && pedidos.back() == std::make_pair(false, false), "a la nueva se le vuelve a pedir");
    s->setCapas("", "");
    CHECK(!s->conCapas(), "sin mids: una sola cámara");
}

} // namespace

int main() {
    selector();
    necesidad();
    sesion();
    if (g_fallos == 0) std::printf("test_capas: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
