// ============================================================================
// test_direcciones.cpp — el catálogo de direcciones (Direcciones.hpp)
//
// Las combinaciones que daban enlaces sin sentido (PLAN-DIRECCIONES.md):
//   1. Sala solo en esta máquina: nunca 127.0.0.1; los servicios de la sala por
//      la puerta (IP de la red); los invitados sin local, con el motivo.
//   2. Con túnel: el local sigue siendo la IP de la red; el túnel va APARTE.
//   3. Una IP elegida: esa IP, no otra de la máquina.
//   4. «Dirección para el QR» = localhost/127.x: se ignora.
//   5. Sin IP: null con motivo, nada de 127.0.0.1.
//   6. La botonera nunca por el túnel; el control web solo si está habilitado.
//   7. origenDe: el origen sin ruta, o el error con el motivo.
// ============================================================================
#include "Direcciones.hpp"

#include <cstdio>
#include <string>

using json = nlohmann::json;
namespace d = ba::gateway::direcciones;

static int g_fallas = 0;
#define CHECK(c, m)                                                       \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", m, #c, __LINE__); \
            ++g_fallas;                                                   \
        }                                                                 \
    } while (0)

static json servicio(const json& cat, const std::string& id) {
    for (const auto& s : cat["servicios"])
        if (s["id"] == id) return s;
    return json();
}

static bool sinLoopback(const json& cat) {
    const std::string t = cat.dump();
    return t.find("127.0.0.1") == std::string::npos && t.find("localhost") == std::string::npos;
}

int main() {
    // --- 1) De fábrica: sala solo en esta máquina, sin túnel ---------------------
    d::Entradas e;
    e.ips = {"192.168.1.42", "10.8.0.2"};
    e.bind = "127.0.0.1";
    e.puertaHttps = 8792;
    e.puertaHttp = 8793;
    {
        const json c = d::armar(e);
        CHECK(sinLoopback(c), "nunca 127.0.0.1 ni localhost");
        CHECK(c["local"]["ip"] == "192.168.1.42", "la IP de la red, la mejor primero");
        CHECK(servicio(c, "botonera")["local"] == "https://192.168.1.42:8792/botonera/", "botonera por la puerta");
        CHECK(servicio(c, "presidencia")["local"] == "https://192.168.1.42:8792/presidencia/", "presidencia por la puerta");
        CHECK(servicio(c, "pantalla")["local"] == "http://192.168.1.42:8793/pantalla/", "televisores por HTTP");
        CHECK(servicio(c, "invitados")["local"].is_null() &&
                  !servicio(c, "invitados")["motivo_local"].get<std::string>().empty(),
              "invitados sin local, con el motivo");
        CHECK(c["remoto"].is_null() && servicio(c, "sesion")["remoto"].is_null(), "sin túnel, nada de afuera");
    }
    // --- 2) Con túnel ----------------------------------------------------------------
    e.remoto = "https://clase.a.pinggy.link/";
    e.via = "pinggy";
    {
        const json c = d::armar(e);
        CHECK(servicio(c, "presidencia")["local"] == "https://192.168.1.42:8792/presidencia/",
              "con túnel, el local sigue siendo la red");
        CHECK(servicio(c, "presidencia")["remoto"] == "https://clase.a.pinggy.link/presidencia/", "el túnel, aparte");
        CHECK(servicio(c, "registro")["local"] == "https://192.168.1.42:8792/registro/" &&
                  servicio(c, "registro")["remoto"] == "https://clase.a.pinggy.link/registro/",
              "el registro por tótem: la misma puerta que la sesión, y por el túnel");
        CHECK(servicio(c, "invitados")["remoto"] == "https://clase.a.pinggy.link/invitados/", "invitados por el túnel");
        CHECK(servicio(c, "botonera")["remoto"].is_null(), "la botonera nunca por el túnel");
        CHECK(servicio(c, "control")["remoto"].is_null(), "el control web desde afuera, apagado");
        CHECK(servicio(c, "pantalla")["remoto"].is_null(), "los televisores, solo red");
        e.controlRemoto = true;
        CHECK(d::armar(e)["servicios"].dump().find("pinggy.link/control/") != std::string::npos,
              "el control web desde afuera, habilitado");
        e.controlRemoto = false;
    }
    // --- 3) Sala abierta a la red, y una IP elegida ----------------------------------------
    e.bind = "0.0.0.0";
    e.invitadosHttps = 8789;
    CHECK(servicio(d::armar(e), "invitados")["local"] == "https://192.168.1.42:8789/invitados/",
          "sala abierta: invitados por la red");
    e.bind = "10.8.0.2";
    {
        const json c = d::armar(e);
        CHECK(c["local"]["ip"] == "10.8.0.2", "una IP elegida: esa, no otra");
        CHECK(servicio(c, "invitados")["local"] == "https://10.8.0.2:8789/invitados/", "invitados en la IP elegida");
    }
    e.invitadosHttps = 0;
    e.invitadosHttp = 8787;
    e.bind = "0.0.0.0";
    {
        const json s = servicio(d::armar(e), "invitados");
        CHECK(s["local"] == "http://192.168.1.42:8787/invitados/" &&
                  s["motivo_local"].get<std::string>().find("HTTPS") != std::string::npos,
              "sin HTTPS: http con el aviso de la cámara");
    }
    // --- 4) «Dirección para el QR» -------------------------------------------------
    e.urlHost = "localhost";
    CHECK(d::armar(e)["local"]["ip"] == "192.168.1.42", "localhost como dirección del QR se ignora");
    e.urlHost = "127.0.1.1";
    CHECK(sinLoopback(d::armar(e)), "127.x como dirección del QR se ignora");
    e.urlHost = "sala.local";
    CHECK(servicio(d::armar(e), "botonera")["local"] == "https://sala.local:8792/botonera/", "un nombre elegido vale");
    e.urlHost.clear();
    // --- 5) Sin IP de red ----------------------------------------------------------------
    {
        d::Entradas s = e;
        s.ips = {"127.0.0.1"};
        s.bind = "127.0.0.1";
        const json c = d::armar(s);
        CHECK(c["local"].is_null() && !c["motivo_local"].get<std::string>().empty(), "sin red: null con motivo");
        CHECK(sinLoopback(c), "sin red: tampoco 127.0.0.1");
    }
    // --- 6) La puerta cerrada -----------------------------------------------------
    {
        d::Entradas s = e;
        s.puertaHttps = 0;
        s.puertaHttp = 0;
        const json b = servicio(d::armar(s), "botonera");
        CHECK(b["local"].is_null() && !b["motivo_local"].get<std::string>().empty(), "sin puerta: el motivo");
    }
    // --- 7) origenDe ------------------------------------------------------------------
    {
        e.bind = "127.0.0.1";
        e.invitadosHttp = 0;
        const json c = d::armar(e);
        CHECK(d::origenDe(c, "tablero", "local").value("origen", "") == "https://192.168.1.42:8792",
              "origen local, sin ruta");
        CHECK(d::origenDe(c, "sesion", "remote").value("origen", "") == "https://clase.a.pinggy.link",
              "origen de afuera");
        const json r = d::origenDe(c, "invitados", "local");
        CHECK(r.value("error", "") == "local" && !r.value("message", "").empty(), "sin local: error con el motivo");
        CHECK(d::origenDe(c, "botonera", "remote").contains("error"), "botonera de afuera: error");
        CHECK(d::origenDe(c, "nada", "local").contains("error"), "servicio desconocido");
    }

    std::printf("test_direcciones: %s\n", g_fallas == 0 ? "OK" : "FALLAS");
    return g_fallas == 0 ? 0 : 1;
}
