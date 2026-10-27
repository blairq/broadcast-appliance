// src/app/sala/ReglasSala.cpp — ver el .hpp

#include "sala/ReglasSala.hpp"

#include "i18n/I18n.hpp"

#include <cstdio>
#include <map>

namespace ba::app::sala {

std::string explicarError(const std::string& codigo) {
    // Se arma en cada llamada: así sale en el idioma de ahora.
    const std::map<std::string, std::string> kTexto{
        {"full", ba::i18n::traducir("sala", "la conferencia está llena: sacá a alguien primero")},
        {"not_found", ba::i18n::traducir("sala", "ya no está en la conferencia")},
        {"state", ba::i18n::traducir("sala", "no aplica en su estado actual")},
        {"empty", ba::i18n::traducir("sala", "no hay nadie más en la fila")},
        {"offline", ba::i18n::traducir("sala", "la conferencia (gateway) no responde")},
        {"", ba::i18n::traducir("sala", "la conferencia (gateway) no responde")}};
    const auto it = kTexto.find(codigo);
    return it != kTexto.end() ? it->second : codigo;
}

std::string recomendacionPrueba(const nlohmann::json& r) {
    if (!r.is_object()) return ba::i18n::traducir("sala", "La conferencia no pudo hacer la prueba.");
    if (r.contains("error") && r["error"].is_string()) return r["error"].get<std::string>();
    std::string out;
    const auto sumar = [&out](const std::string& s) { out += (out.empty() ? "" : " ") + s; };
    if (r.value("eco", false)) {
        char db[32];
        std::snprintf(db, sizeof db, "%.1f", r.value("eco_db", 0.0));
        sumar(ba::i18n::llenar(
            ba::i18n::traducir("sala", "Se escucha el retorno por su micrófono (%1 dB): que use auriculares o baje el parlante."),
            {db}));
    }
    const std::string sem = r.value("red", nlohmann::json::object()).value("semaforo", std::string());
    if (sem == "rojo")
        sumar(ba::i18n::traducir("sala", "La red no alcanza: que se acerque al wifi o use cable (o bajarle la calidad)."));
    else if (sem == "ambar")
        sumar(ba::i18n::traducir("sala", "La red anda justa: puede haber cortes."));
    if (r.contains("aviso") && r["aviso"].is_string()) sumar(r["aviso"].get<std::string>());
    return out.empty() ? ba::i18n::traducir("sala", "Todo bien: sin eco y la red alcanza.") : out;
}

std::string mezclarProxy(const nlohmann::json& actual, const nlohmann::json& cambios, nlohmann::json& nueva) {
    if (!cambios.is_object()) return ba::i18n::traducir("sala", "cambios inválidos");
    nlohmann::json p = actual.is_object() ? actual : nlohmann::json::object();
    for (const char* k : {"activo", "dominio", "puertoTls", "puertoHttp", "servicios"})
        if (cambios.contains(k)) p[k] = cambios[k];
    if (cambios.contains("acme") && cambios["acme"].is_object()) {
        if (!p.contains("acme") || !p["acme"].is_object()) p["acme"] = nlohmann::json::object();
        for (const auto& [k, v] : cambios["acme"].items()) p["acme"][k] = v;
    }
    // Prendido sin dominio no hay nombres que servir: se rechaza antes de
    // guardar (y de reiniciar la sala para nada).
    const bool activo = p.value("activo", false);
    const std::string dominio = p.contains("dominio") && p["dominio"].is_string() ? p["dominio"].get<std::string>() : "";
    if (activo && dominio.empty()) return ba::i18n::traducir("sala", "falta el dominio");
    nueva = std::move(p);
    return {};
}

} // namespace ba::app::sala
