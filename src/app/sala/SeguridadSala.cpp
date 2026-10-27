// src/app/sala/SeguridadSala.cpp — ver el .hpp

#include "sala/SeguridadSala.hpp"

#include "i18n/I18n.hpp"

#include "arranque/ArchivoConfig.hpp"
#include "sala/Pedido.hpp"

namespace ba::app::sala {

SeguridadSala::SeguridadSala(std::filesystem::path dataRoot, ConferenceClient& sala, EnVivo enVivo)
    : archivo_(std::move(dataRoot) / "Config" / "seguridad.json"), sala_(sala), enVivo_(std::move(enVivo)) {}

bool SeguridadSala::pinPorSesion() const { return config::leerObjeto(archivo_).value("pin_por_sesion", true); }

bool SeguridadSala::rotarTokenMcp() const { return config::leerObjeto(archivo_).value("rotar_token_mcp", true); }

std::string SeguridadSala::poner(const char* clave, bool si) {
    if (enVivo_ && enVivo_()) return ba::i18n::traducir("sala", "no se cambia en vivo");
    nlohmann::json j = config::leerObjeto(archivo_);
    j[clave] = si;
    if (!config::guardar(archivo_, j))
        return ba::i18n::llenar(ba::i18n::traducir("sala", "no se pudo guardar %1"), {archivo_.filename().string()});
    return {};
}

std::string SeguridadSala::setPinPorSesion(bool si) { return poner("pin_por_sesion", si); }

std::string SeguridadSala::setRotarTokenMcp(bool si) { return poner("rotar_token_mcp", si); }

nlohmann::json SeguridadSala::estado() const {
    const nlohmann::json j = config::leerObjeto(archivo_);
    return {{"rotar_token_mcp", j.value("rotar_token_mcp", true)}, {"pin_por_sesion", j.value("pin_por_sesion", true)}};
}

void SeguridadSala::rotarPinSiCambioElDia(ListoPin listo) {
    if (!pinPorSesion()) {
        if (listo) listo({}, false, {});
        return;
    }
    // Si es otro día lo decide el gateway (el PIN vigente no es el de hoy): si no, contesta cambio=false.
    pedir(sala_, "POST", "/api/v1/pin", {{"si_cambio_el_dia", true}},
          [listo](const std::string& error, const nlohmann::json& r) {
              if (!listo) return;
              if (!error.empty()) return listo(error, false, {});
              const bool cambio = r.value("cambio", true);
              listo({}, cambio, r.contains("pin") && r["pin"].is_string() ? r["pin"].get<std::string>() : std::string());
          });
}

} // namespace ba::app::sala
