// src/app/sala/OradoresSala.cpp — el modo oradores (ver el .hpp)

#include "sala/OradoresSala.hpp"

#include "i18n/I18nTexto.hpp"

#include "sala/ParticipantesSala.hpp"
#include "ui/ConferenceClient.hpp"

#include <algorithm>

namespace ba::app::sala {

OradoresSala::OradoresSala(ConferenceClient& sala) : sala_(sala) {
    // El modo oradores lo eligió el operador andando: una sala reiniciada no lo sabe.
    sala_.registrarRepublicador("oradores", &vida_, [this]() {
        if (!pedido_.empty()) pedir(sala_, "POST", "/api/v1/speakers", pedido_, {});
    });
}

std::string OradoresSala::configurar(const nlohmann::json& cambios, Listo listo) {
    if (!cambios.is_object()) return ba::i18n::traducir("sala", "configuración inválida");
    nlohmann::json b = nlohmann::json::object();
    for (const char* k : {"enabled", "defaultSec", "multi", "defaultSlot", "carousel", "graceSec"})
        if (cambios.contains(k)) b[k] = cambios[k];
    if (b.empty()) return ba::i18n::traducir("sala", "no hay nada para cambiar");
    if (b.contains("defaultSec") && (!b["defaultSec"].is_number() || b["defaultSec"].get<double>() <= 0))
        return ba::i18n::traducir("sala", "el tiempo general tiene que ser de más de cero segundos");
    if (b.contains("carousel")) {
        const std::string c = b["carousel"].is_string() ? b["carousel"].get<std::string>() : "";
        if (c != "manual" && c != "chain" && c != "time") return ba::i18n::traducir("sala", "carrusel: manual, chain o time");
    }
    pedido_.update(b);
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
    if (segundos <= 0) return ba::i18n::traducir("sala", "el tiempo tiene que ser de más de cero segundos");
    pedir(sala_, "POST", rutaDe(id, "time"), {{"sec", segundos}}, std::move(listo));
    return {};
}

std::string OradoresSala::tiempo(const std::string& id, const nlohmann::json& valor, Listo listo, int* segundos) {
    int s = 0;
    if (std::string e = leerTiempoJson(valor, tiempoActual(sala_.snapshot(), id), s); !e.empty()) return e;
    if (segundos != nullptr) *segundos = s;
    return tiempo(id, s, std::move(listo));
}

std::string OradoresSala::tiempoGeneral(const nlohmann::json& valor, Listo listo, int* segundos) {
    int s = 0;
    if (std::string e = leerTiempoJson(valor, tiempoActual(sala_.snapshot(), {}), s); !e.empty()) return e;
    if (segundos != nullptr) *segundos = s;
    return configurar({{"defaultSec", s}}, std::move(listo));
}

void OradoresSala::aLaFila(const std::string& id, Listo listo) {
    pedir(sala_, "POST", rutaDe(id, "requeue"), nlohmann::json::object(), std::move(listo));
}

void OradoresSala::presentador(const std::string& id, bool on, Listo listo) {
    pedir(sala_, "POST", rutaDe(id, "presenter"), {{"on", on}}, std::move(listo));
}

} // namespace ba::app::sala
