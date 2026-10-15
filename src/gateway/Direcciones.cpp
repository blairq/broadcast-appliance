// ============================================================================
// src/gateway/Direcciones.cpp — ver Direcciones.hpp
// ============================================================================
#include "Direcciones.hpp"

#include <algorithm>

namespace ba::gateway::direcciones {

using json = nlohmann::json;

namespace {

bool esDeEstaMaquina(const std::string& h) {
    return h.empty() || h == "localhost" || h.rfind("127.", 0) == 0 || h == "0.0.0.0" || h == "::1";
}

enum class Puerta { Sala, SalaHttp, Invitados };

struct Servicio {
    const char* id;
    const char* nombre;
    const char* ruta;
    Puerta puerta;
    bool afuera;             ///< se atiende por el túnel
    const char* sinAfuera;   ///< por qué no (si !afuera)
};

// Los servicios y por dónde entran. Las claves (#k=, #room=, tokens) las agrega
// cada endpoint sobre el origen: acá solo el camino.
const std::vector<Servicio>& servicios() {
    static const std::vector<Servicio> s{
        {"invitados", "Invitados (sala)", "/invitados/", Puerta::Invitados, true, ""},
        {"sesion", "Integrantes (sesión remota)", "/sesion/", Puerta::Sala, true, ""},
        {"presidencia", "Consola de la presidencia", "/presidencia/", Puerta::Sala, true, ""},
        {"botonera", "Botonera (tablet)", "/botonera/", Puerta::Sala, false,
         "La botonera anda solo en la red local (nunca por el túnel)."},
        {"control", "Control web (dashboard)", "/control/", Puerta::Sala, true, ""},
        {"prompter", "Teleprompter", "/prompter/", Puerta::Sala, true, ""},
        {"rotulos", "Rótulos", "/rotulos/", Puerta::Sala, true, ""},
        {"correccion", "Corrección de subtítulos", "/correccion/", Puerta::Sala, true, ""},
        {"tablero", "Tablero de oradores", "/board/", Puerta::Sala, true, ""},
        {"publico", "Página del público", "/publico/", Puerta::Sala, true, ""},
        {"pantalla", "Televisores de la sala", "/pantalla/", Puerta::SalaHttp, false,
         "Los televisores van por la red local."},
        {"companion", "API de Companion", "/", Puerta::SalaHttp, false, "Companion va por la red local."},
    };
    return s;
}

} // namespace

std::string hostLocal(const Entradas& e) {
    if (!esDeEstaMaquina(e.urlHost)) return e.urlHost;
    if (!esDeEstaMaquina(e.bind)) return e.bind;
    for (const auto& ip : e.ips)
        if (!esDeEstaMaquina(ip)) return ip;
    return {};
}

json armar(const Entradas& e) {
    const std::string host = hostLocal(e);
    json out = json::object();

    // --- La red local --------------------------------------------------------
    std::string httpsSala, httpSala, motivoSala;
    if (host.empty()) {
        motivoSala = "No encontré ninguna dirección de red en esta máquina (¿está conectada la red?). Se puede "
                     "escribir en Configuración → Red de la sala → «Dirección para el QR».";
    } else {
        if (e.puertaHttps > 0) httpsSala = "https://" + host + ":" + std::to_string(e.puertaHttps);
        if (e.puertaHttp > 0) httpSala = "http://" + host + ":" + std::to_string(e.puertaHttp);
        if (httpsSala.empty() && httpSala.empty())
            motivoSala = "La puerta de la sala no está abierta (ver el registro del gateway).";
    }
    if (!host.empty()) {
        json ips = json::array();
        for (const auto& ip : e.ips)
            if (!esDeEstaMaquina(ip)) ips.push_back(ip);
        out["local"] = {{"ip", host}, {"ips", ips}, {"https", httpsSala}, {"http", httpSala}};
    } else {
        out["local"] = nullptr;
    }
    out["motivo_local"] = motivoSala;

    // --- Afuera --------------------------------------------------------------
    std::string remoto = e.remoto;
    while (!remoto.empty() && remoto.back() == '/') remoto.pop_back();
    if (!remoto.empty())
        out["remoto"] = {{"url", remoto}, {"via", e.via}, {"estado", e.estadoRemoto.empty() ? "listo" : e.estadoRemoto}};
    else
        out["remoto"] = nullptr;
    const std::string motivoRemoto =
        remoto.empty() ? (e.motivoRemoto.empty() ? std::string("No hay salida a internet (Configuración → Acceso remoto).")
                                                 : e.motivoRemoto)
                       : std::string();
    out["motivo_remoto"] = motivoRemoto;

    // --- Cada servicio ----------------------------------------------------------
    json lista = json::array();
    for (const Servicio& s : servicios()) {
        json f{{"id", s.id}, {"nombre", s.nombre}, {"local", nullptr}, {"remoto", nullptr},
               {"motivo_local", ""}, {"motivo_remoto", ""}};
        // Local
        if (s.puerta == Puerta::Invitados) {
            if (host.empty()) {
                f["motivo_local"] = motivoSala;
            } else if (e.invitadosHttps > 0) {
                f["local"] = "https://" + host + ":" + std::to_string(e.invitadosHttps) + s.ruta;
            } else if (e.invitadosHttp > 0) {
                f["local"] = "http://" + host + ":" + std::to_string(e.invitadosHttp) + s.ruta;
                f["motivo_local"] = "Sin HTTPS el navegador no da cámara ni micrófono por la red local.";
            } else {
                f["motivo_local"] = "La sala de invitados escucha solo en esta máquina (Configuración → Red de la "
                                    "sala → Acceso): por la red local no entran; desde afuera, por el túnel.";
            }
        } else {
            const std::string& base = s.puerta == Puerta::SalaHttp ? httpSala : httpsSala;
            if (!base.empty())
                f["local"] = base + s.ruta;
            else
                f["motivo_local"] = motivoSala.empty() ? std::string("La puerta de la sala no está abierta.") : motivoSala;
        }
        // Afuera
        const bool afuera = s.afuera && (std::string(s.id) != "control" || e.controlRemoto);
        if (!afuera) {
            f["motivo_remoto"] = std::string(s.id) == "control"
                                     ? "El control web desde afuera está apagado (se habilita con PIN)."
                                     : std::string(s.sinAfuera);
        } else if (!remoto.empty()) {
            f["remoto"] = remoto + s.ruta;
        } else {
            f["motivo_remoto"] = motivoRemoto;
        }
        lista.push_back(std::move(f));
    }
    out["servicios"] = std::move(lista);
    return out;
}

json origenDe(const json& catalogo, const std::string& servicio, const std::string& donde) {
    const bool remoto = donde == "remote" || donde == "remoto";
    for (const auto& s : catalogo.value("servicios", json::array())) {
        if (s.value("id", std::string()) != servicio) continue;
        const json& u = s[remoto ? "remoto" : "local"];
        if (!u.is_string()) {
            return {{"error", remoto ? "remote" : "local"},
                    {"message", s.value(remoto ? "motivo_remoto" : "motivo_local", std::string())}};
        }
        std::string origen = u.get<std::string>();
        if (const size_t sch = origen.find("://"); sch != std::string::npos) {
            const size_t fin = origen.find_first_of("/?#", sch + 3);
            if (fin != std::string::npos) origen.resize(fin);
        }
        json r{{"origen", origen}, {"url", u}};
        if (!s.value(remoto ? "motivo_remoto" : "motivo_local", std::string()).empty())
            r["warning"] = s.value(remoto ? "motivo_remoto" : "motivo_local", std::string());
        return r;
    }
    return {{"error", donde}, {"message", "No conozco el servicio «" + servicio + "»."}};
}

} // namespace ba::gateway::direcciones
