// src/app/sala/PreguntasSala.cpp — la cola de preguntas (ver el .hpp)

#include "sala/PreguntasSala.hpp"

#include <algorithm>
#include <array>

namespace ba::app::sala {

PreguntasSala::PreguntasSala(ConferenceClient& sala, BuscarEnChat buscarEnChat)
    : sala_(sala), buscarEnChat_(std::move(buscarEnChat)) {}

void PreguntasSala::ver(Listo listo) { pedir(sala_, "GET", "/api/v1/preguntas", {}, std::move(listo)); }

std::string PreguntasSala::moderar(const std::string& accion, const std::string& id, Listo listo) {
    static const std::array<const char*, 7> acciones{"elegir",    "des_elegir", "al_aire", "responder",
                                                     "descartar", "limpiar",    "vaciar"};
    if (std::none_of(acciones.begin(), acciones.end(), [&](const char* a) { return accion == a; }))
        return "accion: elegir, des_elegir, al_aire, responder, descartar, limpiar o vaciar";
    if (accion != "limpiar" && accion != "vaciar" && id.empty()) return "Falta el id de la pregunta.";
    pedir(sala_, "POST", "/api/v1/preguntas", {{"accion", accion}, {"id", id}}, std::move(listo));
    return {};
}

std::string PreguntasSala::sumar(const std::string& texto, const std::string& nombre, const std::string& deDonde,
                                 Listo listo) {
    std::string t = texto;
    t.erase(0, t.find_first_not_of(" \t\n"));
    t.erase(t.find_last_not_of(" \t\n") + 1);
    if (t.empty()) return "La pregunta está vacía.";
    pedir(sala_, "POST", "/api/v1/preguntas",
          {{"accion", "preguntar"}, {"texto", t}, {"nombre", nombre}, {"deDonde", deDonde.empty() ? "sala" : deDonde}},
          std::move(listo));
    return {};
}

std::string PreguntasSala::sumarDesdeChat(const std::string& clave, Listo listo) {
    const auto m = buscarEnChat_ ? buscarEnChat_(clave) : std::nullopt;
    if (!m) return "ese mensaje ya no está en el chat";
    return sumar(m->texto, m->autor, m->deDonde, std::move(listo));
}

} // namespace ba::app::sala
