// ============================================================================
// src/app/tests/test_pantallas_destinos.cpp — las pantallas y lo que muestran (modelo)
//
// pantallas/Destinos sin red, sin hilos y sin Qt:
//   1. Lo guardado: la IP solo de las agregadas a mano, lo fijado, los carruseles; y de
//      vuelta (una descubierta sin dirección queda «buscando»).
//   2. La búsqueda: suma las nuevas, refresca la dirección y respeta el nombre propio.
//   3. Lo que contesta el aparato: libre, mostrando, ocupada con otra cosa, apagada.
//   4. Restaurar lo fijado: solo lo que no está mostrando, nunca lo ocupado, con espera
//      creciente; y un solo aviso de error por fijada que falla.
//   5. Los carruseles: pasos (lo en vivo no entra), mover, segundos con tope.
//   6. Las URLs: cada contenido con la base del televisor, codificadas; lo legible.
//   7. Los canales de escena: cuál soltar y cuál usar.
// ============================================================================
#include <cstdio>
#include <map>
#include <string>

#include "pantallas/Destinos.hpp"

using namespace ba::app::pantallas;
using nlohmann::json;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (false)

Encontrado tv(const std::string& id, const std::string& host, const std::string& nombre = "TV") {
    Encontrado e;
    e.id = id;
    e.host = host;
    e.nombre = nombre;
    e.modelo = "Chromecast";
    e.puerto = kPuertoCast;
    return e;
}

void guardado() {
    Destinos d;
    std::string id;
    CHECK(d.agregar("Hall", " 10.0.0.9 ", 0, &id).empty() && id == "manual:10.0.0.9", "a mano: su id es la dirección");
    CHECK(!d.agregar("Otra", "10.0.0.9", 0).empty(), "la misma dirección dos veces, no");
    CHECK(!d.agregar("", "  ", 0).empty(), "sin dirección, no");
    d.alBuscar({tv("Chromecast-abc", "192.168.1.20", "Living")}, 1000);
    CHECK(d.fijar("Chromecast-abc", "mensaje", {{"texto", "Hola"}, {"tamano", 3}}).empty(), "fijar un mensaje");
    const std::string car = d.carruselNuevo("  Hall  ", 1000);
    CHECK(d.carrusel(car) != nullptr && d.carrusel(car)->nombre == "Hall", "carrusel nuevo, con el nombre recortado");
    d.pasoAgregar(car, "reloj", {{"pie", "Sala 1"}}, 10);
    const json j = d.aJson();
    const json& p = j["pantallas"];
    CHECK(p.size() == 2, "las dos pantallas");
    CHECK(p[0].value("host", "") == "10.0.0.9", "la agregada a mano guarda su IP");
    CHECK(!p[1].contains("host"), "la descubierta NO guarda su IP (el DHCP la cambia)");
    CHECK(p[1]["fijadoDatos"].value("tamano", "") == "3", "lo fijado, como texto");
    CHECK(j["carruseles"].size() == 1 && j["carruseles"][0]["pasos"][0].value("segundos", 0) == 10, "el carrusel con su paso");
    Destinos otra;
    otra.deJson(j);
    CHECK(otra.lista().size() == 2 && otra.porId("Chromecast-abc")->estado == "buscando",
          "de vuelta: la descubierta espera la búsqueda que le dé la dirección de hoy");
    CHECK(otra.porId("Chromecast-abc")->fijadoTipo == "mensaje", "con lo fijado");
    CHECK(otra.carruseles().size() == 1, "y el carrusel");
}

void busqueda() {
    Destinos d;
    d.alBuscar({tv("a", "192.168.1.20", "Living")}, 1000);
    CHECK(d.renombrar("a", "  TV del hall ").empty(), "renombrar");
    d.alBuscar({tv("a", "192.168.1.33", "Living"), tv("b", "192.168.1.40", "Cocina")}, 2000);
    CHECK(d.porId("a")->host == "192.168.1.33", "la dirección se refresca siempre");
    CHECK(d.porId("a")->nombre == "TV del hall", "el nombre propio no se pisa");
    CHECK(d.porId("b") != nullptr && d.porId("b")->nombre == "Cocina", "la nueva se suma");
    CHECK(!d.porId("a")->fija, "renombrar no la vuelve «a mano»");
}

void consultas() {
    Destinos d;
    d.alBuscar({tv("a", "192.168.1.20")}, 1000);
    Estado r;
    r.ok = true;
    r.appId = kAppDashCast;
    d.alConsultar("a", r, 1100);
    CHECK(d.porId("a")->estado == "mostrando" && d.mostrando() == 1, "con DashCast: mostrando");
    r.appId = "CC1AD845";
    r.appNombre = "YouTube";
    d.alConsultar("a", r, 1200);
    CHECK(d.porId("a")->estado == "ocupada" && d.porId("a")->que == "YouTube", "otra aplicación: ocupada, y cuál");
    r.ok = false;
    r.error = "no contesta";
    d.alConsultar("a", r, 1300);
    CHECK(d.porId("a")->estado == "apagada" && d.porId("a")->error == "no contesta", "no contesta: apagada");
}

void restaurar() {
    Destinos d;
    d.alBuscar({tv("a", "192.168.1.20"), tv("b", "192.168.1.21")}, 1000);
    d.fijar("a", "reloj", json::object());
    auto r = d.paraRestaurar(2000);
    CHECK(r.size() == 1 && r[0].id == "a" && r[0].tipo == "reloj", "la que tiene algo fijado y no lo muestra");
    CHECK(d.paraRestaurar(2001).empty(), "no se la martilla: espera su próximo intento");
    CHECK(d.alMostrar("a", false, "apagada", "reloj", "", 2002), "el primer error de una fijada se avisa");
    d.paraRestaurar(2000 + 15000);
    CHECK(!d.alMostrar("a", false, "apagada", "reloj", "", 17001), "el segundo, no (sería una lluvia)");
    CHECK(!d.alMostrar("a", true, "", "reloj", "La hora", 50000) && d.porId("a")->estado == "mostrando", "salió");
    CHECK(d.paraRestaurar(10000000).empty(), "mostrando lo fijado: nada que hacer");
    Estado ocupada;
    ocupada.ok = true;
    ocupada.appId = "netflix";
    d.alConsultar("a", ocupada, 10000001);
    CHECK(d.paraRestaurar(20000000).empty(), "ocupada con otra cosa: no se le pisa sola");
}

void carruseles() {
    Destinos d;
    const std::string c = d.carruselNuevo("", 1700000000000LL);
    CHECK(c.rfind("car", 0) == 0 && d.carrusel(c)->nombre == "Carrusel", "nombre por omisión");
    CHECK(!d.pasoAgregar(c, "programa", json::object(), 10).empty(), "lo en vivo no entra en un carrusel");
    CHECK(!d.pasoAgregar(c, "nada", json::object(), 10).empty(), "un tipo que no existe, no");
    d.pasoAgregar(c, "mensaje", {{"texto", "uno"}}, 0);
    d.pasoAgregar(c, "mensaje", {{"texto", "dos"}}, 99999);
    CHECK(d.carrusel(c)->pasos[0].segundos == 15 && d.carrusel(c)->pasos[1].segundos == 3600, "segundos: por omisión y con tope");
    CHECK(d.pasoMover(c, 0, 1).empty() && d.carrusel(c)->pasos[0].datos.value("texto", "") == "dos", "mover");
    CHECK(!d.pasoMover(c, 0, 5).empty(), "mover fuera de la lista, no");
    CHECK(d.pasoSegundos(c, 0, 1).empty() && d.carrusel(c)->pasos[0].segundos == 3, "segundos: mínimo 3");
    CHECK(d.carruselTransicion(c, "corte").empty() && d.carrusel(c)->transicion == "corte", "transición");
    CHECK(d.carruselTransicion(c, "cualquiera").empty() && d.carrusel(c)->transicion == "fundido", "lo que no es corte, fundido");
    d.alBuscar({tv("a", "192.168.1.20"), tv("b", "192.168.1.21")}, 1000);
    CHECK(d.destinoDeCarrusel(c) == "a", "sin nadie mostrándolo: la primera");
    d.alMostrar("b", true, "", "carrusel:" + c, "", 1000);
    CHECK(d.destinoDeCarrusel(c) == "b", "la que lo está mostrando");
    CHECK(d.carruselBorrar(c).empty() && d.carruseles().empty(), "borrar");
}

void urls() {
    const std::string base = "http://192.168.1.5:8080";
    CHECK(url("mensaje", {{"texto", "Empezamos en 5'"}, {"pie", "Sala ñ"}}, base) ==
              base + "/pantalla/?tipo=mensaje&texto=Empezamos%20en%205%27&pie=Sala%20%C3%B1",
          "el mensaje, codificado");
    CHECK(url("mensaje", {{"texto", "x"}}, "").empty(), "sin dirección todavía: nada");
    CHECK(url("url", {{"url", "  http://x/  "}}, "") == "http://x/", "una página: tal cual");
    CHECK(url("prompter", json::object(), base) == base + "/prompter/", "el teleprompter");
    CHECK(url("votacion", {{"clavePublico", "k1"}, {"entrada", "https://a/b?c=d"}}, base) ==
              base + "/pantalla/votacion/?k=k1&entrada=https%3A%2F%2Fa%2Fb%3Fc%3Dd",
          "la votación: clave y entrada");
    CHECK(url("tally", {{"fuente", "Cam 1"}}, base) == base + "/pantalla/tally/?fuente=Cam%201", "el tally");
    CHECK(url("qr", {{"guardado", "x"}, {"texto", "hola"}, {"estilo", "c=1"}}, base) == base + "/pantalla/?tipo=qr&texto=hola&c=1",
          "el QR: lo guardado ya viene como texto y estilo");
    CHECK(!url("programa", json::object(), base).empty(), "lo en vivo: lo pide el gateway");
    CHECK(que("mensaje", {{"texto", "  áéíóúáéíóú  "}}, 5) == "Un mensaje — áéíóú…", "lo legible: título y primer dato, cortado por letras");
    CHECK(que("reloj", json::object(), 40) == "La hora", "sin datos: el título");
}

void canales() {
    std::map<int, std::string> escenas{{0, "Hall"}, {1, ""}, {2, "Recinto"}, {3, "Remotos"}};
    CanalesDelMotor c;
    c.cantidad = 4;
    c.escena = [&](int k) { return escenas[k]; };
    c.conDuenos = [](int k) { return k == 3; };
    const auto s = aSoltar(c, {2});
    CHECK(s.size() == 1 && s[0].first == 0 && s[0].second == "Hall", "se suelta la que nadie usa (no la de los remotos)");
    CHECK(canalPara(c, "Recinto") == 2, "la escena que ya está: la comparten");
    CHECK(canalPara(c, "Nueva") == 1, "una nueva: un canal libre");
    escenas[1] = "X";
    CHECK(canalPara(c, "Nueva") == -1, "sin libres: ninguno");
    Destinos d;
    d.alBuscar({tv("a", "1.1.1.1")}, 0);
    d.porId("a")->canalEscena = 2;
    d.porId("a")->trabajando = true;
    CHECK(d.canalesUsados() == std::set<int>{2}, "el canal de una pantalla que está mandando la escena se usa");
}

} // namespace

int main() {
    guardado();
    busqueda();
    consultas();
    restaurar();
    carruseles();
    urls();
    canales();
    if (g_fallos == 0) std::printf("test_pantallas_destinos: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
