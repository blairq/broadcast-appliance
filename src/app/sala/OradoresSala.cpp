// src/app/sala/OradoresSala.cpp — el modo oradores (ver el .hpp)

#include "sala/OradoresSala.hpp"

#include "sala/ParticipantesSala.hpp"

#include <algorithm>

namespace ba::app::sala {

OradoresSala::OradoresSala(ConferenceClient& sala) : sala_(sala) {}

std::string OradoresSala::configurar(const nlohmann::json& cambios, Listo listo) {
    if (!cambios.is_object()) return "configuración inválida";
    nlohmann::json b = nlohmann::json::object();
    for (const char* k : {"enabled", "defaultSec", "multi", "defaultSlot", "carousel", "graceSec"})
        if (cambios.contains(k)) b[k] = cambios[k];
    if (b.empty()) return "no hay nada para cambiar";
    if (b.contains("defaultSec") && (!b["defaultSec"].is_number() || b["defaultSec"].get<double>() <= 0))
        return "el tiempo general tiene que ser de más de cero segundos";
    if (b.contains("carousel")) {
        const std::string c = b["carousel"].is_string() ? b["carousel"].get<std::string>() : "";
        if (c != "manual" && c != "chain" && c != "time") return "carrusel: manual, chain o time";
    }
    pedir(sala_, "POST", "/api/v1/speakers", b, std::move(listo));
    return {};
}

void OradoresSala::siguiente(Listo listo) {
    pedir(sala_, "POST", "/api/v1/speakers/next", nlohmann::json::object(), std::move(listo));
}

void OradoresSala::alAire(const std::string& id, bool on, Listo listo) {
    pedir(sala_, "POST", rutaDe(id, "onair"), {{"on", on}}, std::move(listo));
}

void OradoresSala::mover(const std::string& id, int lugar, Listo listo) {
    pedir(sala_, "POST", rutaDe(id, "move"), {{"index", std::max(0, lugar)}}, std::move(listo));
}

std::string OradoresSala::tiempo(const std::string& id, int segundos, Listo listo) {
    if (segundos <= 0) return "el tiempo tiene que ser de más de cero segundos";
    pedir(sala_, "POST", rutaDe(id, "time"), {{"sec", segundos}}, std::move(listo));
    return {};
}

void OradoresSala::aLaFila(const std::string& id, Listo listo) {
    pedir(sala_, "POST", rutaDe(id, "requeue"), nlohmann::json::object(), std::move(listo));
}

void OradoresSala::presentador(const std::string& id, bool on, Listo listo) {
    pedir(sala_, "POST", rutaDe(id, "presenter"), {{"on", on}}, std::move(listo));
}

} // namespace ba::app::sala
