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
//   4. De la baja a la HD (regla del usuario): conexión nueva con la alta pedida, la baja
//      sale mientras tanto; llega la alta: arranca el fundido, la baja sigue prendida (sus
//      cuadros son «anterior») y recién al terminar se pausa. Pedir lo mismo en pleno
//      fundido no lo corta. La capa de la grilla (con histéresis) manda sobre el umbral por
//      alto; la precarga pide la alta.
//   5. F3, quién se decodifica: fuera del aire, sin grilla, sin vista web y con la tarjeta
//      de la interfaz sin ver («nadie» o «de vez en cuando»), nada; cualquiera de esas cosas
//      lo vuelve a pedir. Reanudar pide un cuadro clave si el primero no lo es.
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
    n.capaGrilla = Capa::Baja;
    CHECK(capaNecesaria(n) == Capa::Baja, "en un cuadrito de una grilla: baja (lo dice la app)");
    n.capaGrilla = Capa::Alta;
    CHECK(capaNecesaria(n) == Capa::Alta, "en un lugar grande de una grilla: alta (lo dice la app)");
    n.capaGrilla = Capa::Nada;
    n.espectadores = 1;
    CHECK(capaNecesaria(n) == Capa::Alta, "la mira una vista web: alta (se le reenvía esa)");

    n = {};
    n.ruta = MediaRoute::program(1);
    n.appDijo = true;
    n.capaGrilla = Capa::Baja;
    CHECK(capaNecesaria(n) == Capa::Baja, "en el programa (en el cupo), no al aire, en un cuadrito: baja");
    n.ruta = MediaRoute::atencion();
    n.capaGrilla = Capa::Nada;
    CHECK(capaNecesaria(n) == Capa::Nada, "la atención hablada no manda cámara");
}

/// F3: no se decodifica a quien nadie ve.
void quienSeDecodifica() {
    NecesidadCapa n;
    n.ruta = MediaRoute::program(5);
    n.appDijo = true;
    n.vista = Mirada::Todos;
    CHECK(capaNecesaria(n) == Capa::Baja, "fuera del aire con su tarjeta a la vista: baja");
    n.vista = Mirada::DeVezEnCuando;
    CHECK(capaNecesaria(n) == Capa::Nada, "la tarjeta no se ve (una vez por segundo): nada, queda la última imagen");
    n.vista = Mirada::Nadie;
    CHECK(capaNecesaria(n) == Capa::Nada, "nadie lo lee: nada (no viaja ni se decodifica)");

    // Lo que sí lo hace ver, aunque la tarjeta no se vea.
    n.capaGrilla = Capa::Baja;
    CHECK(capaNecesaria(n) == Capa::Baja, "en una celda de una grilla a la vista: baja");
    n.capaGrilla = Capa::Nada; // quedó en el «+N»
    CHECK(capaNecesaria(n) == Capa::Nada, "en el «+N» de la grilla: nada");
    // Una escena fuera del aire que la interfaz MUESTRA (monitor, multiview, distribución de
    // pantallas): no puede quedar congelado.
    n.enVista = true;
    CHECK(capaNecesaria(n) == Capa::Baja, "en una escena a la vista (multiview): baja, no pausado");
    n.enVista = false;
    n.alAire = true;
    CHECK(capaNecesaria(n) == Capa::Alta, "al aire: alta, se vea o no su tarjeta");
    n.alAire = false;
    n.siguiente = true;
    CHECK(capaNecesaria(n) == Capa::Alta, "armado en el preview: alta");
    n.siguiente = false;
    n.precarga = true;
    CHECK(capaNecesaria(n) == Capa::Alta, "precargado por una grilla: alta");
    n.precarga = false;
    n.espectadores = 2;
    CHECK(capaNecesaria(n) == Capa::Alta, "lo mira una vista web: alta");
    n.espectadores = 0;

    // Sin que la app haya dicho qué está al aire, no se pausa a nadie (como siempre).
    n.appDijo = false;
    CHECK(capaNecesaria(n) == Capa::Alta, "la app no dijo nada: alta, aunque nadie mire su tarjeta");

    // Fuera del programa la tarjeta no cuenta: el lobby sigue con su cuadrito.
    n = {};
    n.ruta = MediaRoute::lobby(1, false);
    n.appDijo = true;
    n.vista = Mirada::Nadie;
    CHECK(capaNecesaria(n) == Capa::Baja, "en el lobby: baja (la tarjeta del programa no tiene que ver)");

    // Reanudar: después de la pausa, «nada» → «baja» prende solo la baja.
    SelectorCapas s;
    s.pedir(Capa::Baja);
    bool pasa = false;
    s.llego(Capa::Baja, pasa);
    CHECK(s.pedir(Capa::Nada) && activas(s, false, false), "se deja de ver: se pausan las dos");
    CHECK(s.pedir(Capa::Baja) && activas(s, false, true), "vuelve a verse: se reanuda la baja");
}

/// La capa media (360p): la alta topada; un solo transceptor prendido.
void capaMedia() {
    CHECK(transceptorDe(Capa::Media) == Capa::Alta && transceptorDe(Capa::Baja) == Capa::Baja, "la media va por la alta");
    CHECK(std::string(nombreCapa(Capa::Media)) == "media", "se llama media");
    NecesidadCapa n;
    n.ruta = MediaRoute::program(3);
    n.appDijo = true;
    n.vista = Mirada::Nadie;
    n.capaGrilla = Capa::Media;
    CHECK(capaNecesaria(n) == Capa::Media, "una celda grande fuera del tope de la alta: media");
    n.alAire = true;
    CHECK(capaNecesaria(n) == Capa::Alta, "al aire gana la alta");

    SelectorCapas s;
    CHECK(s.pedir(Capa::Baja) && activas(s, false, true), "arranca en la baja (solo la baja)");
    bool pasa = false;
    s.llego(Capa::Baja, pasa);
    CHECK(s.pedir(Capa::Media) && activas(s, true, true), "a la media: se prende el transceptor de la alta hasta que llegue");
    CHECK(s.pedida() == Capa::Alta, "pedida: el transceptor de la alta");
    CHECK(s.llego(Capa::Alta, pasa) && pasa && activas(s, true, false), "llega: solo ese transceptor (la baja se pausa)");
    CHECK(!s.pedir(Capa::Alta), "de la media a la alta: mismo transceptor, no se le pide nada nuevo (solo cambia el tope)");
}

/// Reanudar pide un cuadro clave si el primero que llega después del hueco no lo es.
void reanudarPideClave() {
    VigiaReanudar v(400);
    CHECK(!v.llego(1000, true), "el primero, clave: no hace falta pedir");
    CHECK(!v.llego(1066, false), "cuadros P seguidos: nada");
    CHECK(!v.llego(1133, false), "y otro");
    // Hueco (la capa estuvo pausada) y el navegador arranca con un cuadro clave: bien.
    CHECK(!v.llego(5000, true), "reanuda con un cuadro clave: no se pide");
    CHECK(!v.llego(5066, false), "sigue normal");
    // Hueco y llega un P: se pide, y se sigue pidiendo hasta que llegue la clave.
    CHECK(v.llego(9000, false), "reanuda sin clave: se pide una");
    CHECK(v.esperandoClave(), "queda esperando la clave");
    CHECK(v.llego(9066, false), "mientras no llega, se sigue pidiendo (el Feed lo limita)");
    CHECK(!v.llego(9133, true), "llegó la clave: listo");
    CHECK(!v.esperandoClave() && !v.llego(9200, false), "y los P de después no piden nada");
    CHECK(v.pedidos() == 2, "dos pedidos en total");
    // La primera vez que llega algo, si no es clave, también se pide.
    VigiaReanudar w(400);
    CHECK(w.llego(100, false), "arranca con un P: se pide la clave");
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

void fundido() {
    SelectorCapas s;
    s.setFundidoMs(300);
    bool pasa = false, anterior = false, empieza = false;
    // Conexión nueva: se pide la alta (se le dio la palabra). La baja sigue prendida.
    CHECK(!s.pedir(Capa::Alta) && activas(s, true, true), "se pide la alta: la baja también, hasta la HD");
    CHECK(!s.llego(Capa::Baja, pasa, anterior, empieza, 1000) && pasa && !anterior, "llega la baja primero: sale (nunca negro)");
    CHECK(s.sale() == Capa::Baja, "sale la baja");
    CHECK(!s.llego(Capa::Baja, pasa, anterior, empieza, 1040) && pasa, "y sigue saliendo");
    // Llega la HD.
    s.llego(Capa::Alta, pasa, anterior, empieza, 1100);
    CHECK(pasa && empieza && s.sale() == Capa::Alta, "llega la alta: sale y arranca el fundido");
    CHECK(activas(s, true, true) && s.fundiendo(), "la baja sigue prendida durante el fundido");
    CHECK(!s.llego(Capa::Baja, pasa, anterior, empieza, 1200) && !pasa && anterior, "un cuadro de la baja: es el anterior (refresca lo que se va)");
    CHECK(!s.llego(Capa::Alta, pasa, anterior, empieza, 1250) && pasa && !empieza, "la alta sigue (sin volver a empezar)");
    CHECK(!s.pedir(Capa::Alta) && s.fundiendo(), "pedir lo mismo en pleno fundido no lo corta");
    CHECK(s.llego(Capa::Alta, pasa, anterior, empieza, 1400) && pasa, "terminó el fundido: cambia lo pedido");
    CHECK(activas(s, true, false) && !s.fundiendo(), "recién ahora se pausa la baja");
    CHECK(!s.llego(Capa::Baja, pasa, anterior, empieza, 1450) && !pasa && !anterior, "un rezagado de la baja: a ningún lado");

    // Bajar (de la alta a la baja) no funde: el lugar se achica.
    CHECK(s.pedir(Capa::Baja) && activas(s, true, true), "a la baja: las dos");
    s.llego(Capa::Baja, pasa, anterior, empieza, 2000);
    CHECK(pasa && !empieza && activas(s, false, true), "llega la baja: sale y la alta se pausa de una");

    // Sin fundido (0): como antes.
    SelectorCapas t;
    CHECK(!t.pedir(Capa::Alta), "sin fundido: pedir la alta");
    t.llego(Capa::Baja, pasa, anterior, empieza, 0);
    CHECK(pasa, "la baja sale mientras tanto igual");
    CHECK(t.llego(Capa::Alta, pasa, anterior, empieza, 10) && pasa && !empieza && activas(t, true, false),
          "sin fundido, la baja se pausa apenas llega la alta");

    // Después de «nada» no se prende la baja de más.
    SelectorCapas u;
    u.setFundidoMs(300);
    u.pedir(Capa::Nada);
    CHECK(u.pedir(Capa::Alta) && activas(u, true, false), "de nada a la alta: solo la alta (como antes)");

    // Lo que pide la grilla.
    NecesidadCapa n;
    n.ruta = MediaRoute::backstage(0);
    n.capaGrilla = Capa::Baja;
    CHECK(capaNecesaria(n) == Capa::Baja, "la grilla dijo baja (miniatura, o más de «alta_hasta» personas): manda la grilla");
    n.capaGrilla = Capa::Alta;
    CHECK(capaNecesaria(n) == Capa::Alta, "la grilla dijo alta (el que pasa a grande)");
    n = {};
    n.ruta = MediaRoute::backstage(0);
    n.precarga = true;
    CHECK(capaNecesaria(n) == Capa::Alta, "precargado (el siguiente orador): alta");
}

} // namespace

int main() {
    selector();
    necesidad();
    quienSeDecodifica();
    capaMedia();
    reanudarPideClave();
    sesion();
    fundido();
    if (g_fallos == 0) std::printf("test_capas: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
