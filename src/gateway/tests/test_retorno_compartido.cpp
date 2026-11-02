// ============================================================================
// src/gateway/tests/test_retorno_compartido.cpp — el video de retorno una vez por capa
// (RetornoCompartido.hpp): la parte que no necesita libwebrtc.
//
//   1. Las capas: el ajuste se lee (ordenadas, sin repetidas, lo malo afuera, por omisión
//      si no queda nada); una fuente chica no se agranda; el tope del operador.
//   2. SelectorRetorno: arranca en la más alta que entra; baja si el objetivo queda corto un
//      rato (no por un pozo de un instante); sube de a una con margen y después de un rato;
//      el tope manda siempre.
//   3. AgregadorClaves: muchos pedidos juntos son UNA clave; entre dos claves pasa el mínimo;
//      el pedido que llega antes de tiempo sale cuando se cumple; una clave propia del
//      codificador atiende a los que esperaban.
//   4. FlujoRetorno y el tope en la sesión (backend NULL): emitir devuelve el mismo cuadro y
//      la sesión guarda el tope (una conexión nueva lo borra: lo vuelve a poner el servidor).
//
// Lo que necesita libwebrtc (codificar una vez y repartir a varias conexiones) lo prueba
// test_webrtc_loopback.
// ============================================================================

#include <cstdio>
#include <string>
#include <vector>

#include "RetornoCompartido.hpp"
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

void capas() {
    const auto c = leerCapasRetorno("360:600, 720:1500,abc,180:200,720:900,10:5");
    CHECK((c == std::vector<CapaRetorno>{{720, 1500}, {360, 600}, {180, 200}}), "ordenadas, sin repetidas ni basura");
    CHECK(leerCapasRetorno("").size() == 3, "vacío: las de por omisión");
    CHECK(leerCapasRetorno("1080:4000,720:1500,540:1000,360:600,180:200").size() == 4, "a lo sumo cuatro");

    const auto todas = leerCapasRetorno("720:1500,360:600,180:200");
    CHECK(capasPara(720, todas).size() == 3, "fuente 720p: las tres");
    CHECK((capasPara(360, todas) == std::vector<CapaRetorno>{{360, 600}, {180, 200}}), "fuente 360p: no se agranda");
    CHECK((capasPara(120, todas) == std::vector<CapaRetorno>{{120, 200}}), "fuente más chica que todas: una a su alto");

    CHECK(indiceTope(todas, 0) == 0, "sin tope: la más alta");
    CHECK(indiceTope(todas, 720) == 0, "tope 720");
    CHECK(indiceTope(todas, 360) == 1, "tope 360");
    CHECK(indiceTope(todas, 100) == 2, "tope más bajo que todas: la última");

    CHECK(topeDePedido("calidad-720p") == 720 && topeDePedido("calidad-360p") == 360, "pedidos de calidad");
    CHECK(topeDePedido("calidad-max") == 0, "calidad-max: sin tope");
    CHECK(topeDePedido("mute-mic") == -1, "otro pedido: no es de calidad");
}

void selector() {
    const std::vector<int> kbps{1500, 600, 200};
    {
        SelectorRetorno s;
        CHECK(s.elegir(kbps, 0, 0, 0) == 0, "sin objetivo todavía: la más alta permitida");
        s.reiniciar();
        CHECK(s.elegir(kbps, 300'000, 0, 0) == 2, "300 kbps: arranca en la de abajo");
        s.reiniciar();
        CHECK(s.elegir(kbps, 800'000, 0, 0) == 1, "800 kbps: la del medio");
        s.reiniciar();
        CHECK(s.elegir(kbps, 2'000'000, 1, 0) == 1, "con tope 360: aunque entre la alta");
    }
    {
        SelectorRetorno s; // umbrales: baja con < 0,8× por 500 ms; sube con > 1,15× por 2 s
        CHECK(s.elegir(kbps, 2'000'000, 0, 0) == 0, "arranca arriba");
        CHECK(s.elegir(kbps, 1'000'000, 0, 100) == 0, "un pozo: todavía no baja");
        CHECK(s.elegir(kbps, 2'000'000, 0, 300) == 0, "se recuperó: sigue");
        CHECK(s.elegir(kbps, 1'000'000, 0, 400) == 0, "otro pozo: cuenta de nuevo");
        CHECK(s.elegir(kbps, 1'000'000, 0, 800) == 0, "400 ms: todavía no");
        CHECK(s.elegir(kbps, 1'000'000, 0, 900) == 1, "500 ms corto: baja a la que entra");
        CHECK(s.elegir(kbps, 150'000, 0, 1000) == 1, "se cae: empieza a contar");
        CHECK(s.elegir(kbps, 150'000, 0, 1600) == 2, "y baja a la última");
        CHECK(s.elegir(kbps, 50'000, 0, 5000) == 2, "más abajo no hay");
        CHECK(s.elegir(kbps, 5'000'000, 0, 6000) == 2, "sobra: espera antes de subir");
        CHECK(s.elegir(kbps, 5'000'000, 0, 7900) == 2, "1,9 s: todavía no");
        CHECK(s.elegir(kbps, 5'000'000, 0, 8000) == 1, "2 s: sube UNA");
        CHECK(s.elegir(kbps, 5'000'000, 0, 8100) == 1, "y vuelve a esperar");
        CHECK(s.elegir(kbps, 5'000'000, 0, 10100) == 0, "otra");
        CHECK(s.elegir(kbps, 1'600'000, 0, 20000) == 0, "1,6 Mbps alcanza para 1,5");
        CHECK(s.elegir(kbps, 5'000'000, 2, 20001) == 2, "el operador topea a 180: ya");
        CHECK(s.elegir(kbps, 5'000'000, 0, 21000) == 2, "saca el tope: espera para subir");
        CHECK(s.elegir(kbps, 5'000'000, 0, 23000) == 1, "y sube de a una");
        CHECK(s.elegir(kbps, 690'000, 0, 30000) == 1, "con margen para 600 no sube a 1500");
        CHECK(s.elegir(kbps, 690'000, 0, 40000) == 1, "nunca");
    }
}

void claves() {
    AgregadorClaves a;
    CHECK(!a.tocaAhora(0, 500), "sin pedidos no hay clave");
    for (int i = 0; i < 10; ++i) a.pedir();
    CHECK(a.tocaAhora(0, 500), "diez pedidos: una clave");
    a.salioClave(0);
    CHECK(!a.tocaAhora(10, 500), "y una sola");
    a.pedir();
    CHECK(!a.tocaAhora(200, 500), "otro pedido enseguida: espera el mínimo");
    CHECK(a.pendiente(), "queda pendiente");
    CHECK(a.tocaAhora(500, 500), "cumplido el mínimo, sale");
    a.salioClave(500);
    a.pedir();
    a.salioClave(700); // el codificador sacó una por su cuenta (p. ej. al arrancar)
    CHECK(!a.pendiente() && !a.tocaAhora(1300, 500), "una clave propia atiende a los que esperaban");
    CHECK(a.pedidos() == 12 && a.atendidos() == 2, "se cuentan pedidos y atendidos");
}

void sesionNull() {
    const std::vector<uint8_t> bgra(64 * 36 * 4, 128);
    const CuadroSaliente c = CuadroSaliente::desdeBgra(bgra.data(), 64, 36);
    WebrtcHub hub(2, 640, 360, 48000, 2);
    const FlujoRetorno f = hub.flujoRetorno("BA.Program");
    CHECK(f.vacio(), "sin libwebrtc no hay flujo");
    const CuadroSaliente e = f.emitir(c);
    CHECK(!e.vacio() && e.ancho() == 64 && e.alto() == 36, "emitir sin flujo: el mismo cuadro");
    CHECK(hub.retornoCompartido().is_array() && hub.retornoCompartido().empty(), "estado vacío");

    WebrtcSession* s = hub.session(1);
    SessionCallbacks cb;
    CHECK(s->configure(cb, nullptr), "configura");
    s->setTopeRetorno(360);
    CHECK(s->topeRetorno() == 360, "guarda el tope");
    s->setTopeRetorno(-5);
    CHECK(s->topeRetorno() == 0, "negativo: sin tope");
    s->setTopeRetorno(720);
    CHECK(s->configure(cb, nullptr) && s->topeRetorno() == 0, "conexión nueva: sin tope (lo pone el servidor)");
}

} // namespace

int main() {
    capas();
    selector();
    claves();
    sesionNull();
    if (g_fallos == 0) std::printf("test_retorno_compartido: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
