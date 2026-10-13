// src/app/sala/PantallasSala.cpp — lo que la sala sirve a las pantallas (ver el .hpp)

#include "sala/PantallasSala.hpp"

namespace ba::app::sala {

PantallasSala::PantallasSala(ConferenceClient& sala) : sala_(sala) {}

void PantallasSala::publicarCarrusel(const std::string& id, const std::string& nombre, const std::string& transicion,
                                     const std::vector<PasoCarrusel>& pasos) {
    if (pasos.empty()) return;
    nlohmann::json lista = nlohmann::json::array();
    for (const PasoCarrusel& p : pasos)
        lista.push_back({{"url", p.url}, {"segundos", p.segundos}, {"titulo", p.titulo}});
    pedir(sala_, "POST", "/api/v1/carruseles",
          {{"id", id}, {"nombre", nombre}, {"transicion", transicion}, {"pasos", lista}}, {});
}

void PantallasSala::borrarCarrusel(const std::string& id) {
    pedir(sala_, "POST", "/api/v1/carruseles", {{"borrar", id}}, {});
}

void PantallasSala::tally(const std::string& aire, const std::string& armada,
                          const std::vector<std::string>& fuentesAire, const std::vector<std::string>& fuentesArmadas,
                          bool transmitiendo, bool grabando) {
    pedir(sala_, "POST", "/api/v1/tally",
          {{"aire", aire},
           {"armada", armada},
           {"fuentesAire", fuentesAire},
           {"fuentesArmadas", fuentesArmadas},
           {"transmitiendo", transmitiendo},
           {"grabando", grabando}},
          {});
}

void PantallasSala::superficies(Listo listo) { pedir(sala_, "GET", "/api/v1/superficies", {}, std::move(listo)); }

void PantallasSala::mostrarEnSuperficies(const std::string& a, const std::string& tipo, const std::string& url,
                                         bool recargar, Listo listo) {
    nlohmann::json o = nlohmann::json::object();
    if (!url.empty()) o["url"] = url;
    if (recargar) o["recargar"] = true;
    if (!a.empty()) o["a"] = a;
    if (!tipo.empty()) o["tipo"] = tipo;
    pedir(sala_, "POST", "/api/v1/superficies/mostrar", o, std::move(listo));
}

} // namespace ba::app::sala
