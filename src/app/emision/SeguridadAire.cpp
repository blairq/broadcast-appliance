// src/app/emision/SeguridadAire.cpp — ver el .hpp

#include "emision/SeguridadAire.hpp"

#include "arranque/AppRuntime.hpp"
#include "arranque/ArchivoConfig.hpp"
#include "i18n/I18n.hpp"
#include "persist/ColaEscritura.hpp"
#include "platform/Paths.hpp"

namespace ba::app::emision {

namespace {
// Runtime/ es de la máquina (no de la configuración elegida).
std::filesystem::path archivoPlaca() { return ba::platform::dataRoot() / "Runtime" / "placa.json"; }
} // namespace

SeguridadAire::SeguridadAire(AppRuntime& rt) : rt_(rt) {}

void SeguridadAire::guardar() {
    auto& e = rt_.engine();
    nlohmann::json j = e.estadoPlaca();
    // Las demás salidas, solo si tienen algo puesto (con una sola, el archivo de siempre).
    nlohmann::json otras = nlohmann::json::object();
    for (const std::string& id : e.salidaIds()) {
        if (e.idDeSalida("") == id) continue;
        const nlohmann::json s = e.estadoPlaca(id);
        if (s.value("ftb", false) || s.value("placa", false)) otras[id] = s;
    }
    if (!otras.empty()) j["salidas"] = otras;
    // Es estado de aire: al escritor con prioridad y bajado del todo al disco.
    ba::persist::cola().escribir(archivoPlaca(), j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace),
                                 {ba::persist::Durabilidad::Critica, ba::persist::Prioridad::Aire, ba::persist::Permisos::Comunes, {}});
    if (cambio_) cambio_();
}

std::string SeguridadAire::ftb(bool activar, int ms, const std::string& audio, const std::string& salida) {
    if (audio != "bajar" && audio != "mantener") return ba::i18n::traducir("emision", "audio: bajar o mantener");
    if (!rt_.engine().ftb(activar, ms, audio == "bajar", salida)) return rt_.engine().lastError();
    guardar();
    return {};
}

std::string SeguridadAire::placa(bool activar, const std::string& texto, const std::string& imagen, int ms,
                                 const std::string& salida) {
    if (!rt_.engine().placa(activar, texto, imagen, ms, salida)) return rt_.engine().lastError();
    guardar();
    return {};
}

nlohmann::json SeguridadAire::estado(const std::string& salida) const { return rt_.engine().estadoPlaca(salida); }

bool SeguridadAire::restaurar() {
    const nlohmann::json j = config::leerObjeto(archivoPlaca());
    // Lo de una salida ("" = la por omisión) como estaba; true si había algo.
    const auto volver = [this](const nlohmann::json& s, const std::string& salida) {
        const bool placa = s.value("placa", false), negro = s.value("ftb", false);
        if (placa)
            rt_.engine().placa(true, s.value("placa_texto", std::string()), s.value("placa_imagen", std::string()), 0,
                               salida);
        if (negro) rt_.engine().ftb(true, 0, s.value("ftb_audio", false), salida);
        return placa || negro;
    };
    bool algo = volver(j, {});
    const nlohmann::json otras = j.value("salidas", nlohmann::json::object());
    for (auto it = otras.begin(); it != otras.end(); ++it)
        if (it.value().is_object() && rt_.engine().existeSalida(it.key())) algo = volver(it.value(), it.key()) || algo;
    if (algo && cambio_) cambio_();
    return algo;
}

} // namespace ba::app::emision
