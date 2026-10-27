// src/app/emision/SeguridadAire.cpp — ver el .hpp

#include "emision/SeguridadAire.hpp"

#include "arranque/AppRuntime.hpp"
#include "arranque/ArchivoConfig.hpp"
#include "i18n/I18n.hpp"
#include "platform/Paths.hpp"

namespace ba::app::emision {

namespace {
// Runtime/ es de la máquina (no de la configuración elegida).
std::filesystem::path archivoPlaca() { return ba::platform::dataRoot() / "Runtime" / "placa.json"; }
} // namespace

SeguridadAire::SeguridadAire(AppRuntime& rt) : rt_(rt) {}

void SeguridadAire::guardar() {
    config::guardar(archivoPlaca(), rt_.engine().estadoPlaca());
    if (cambio_) cambio_();
}

std::string SeguridadAire::ftb(bool activar, int ms, const std::string& audio) {
    if (audio != "bajar" && audio != "mantener") return ba::i18n::traducir("emision", "audio: bajar o mantener");
    if (!rt_.engine().ftb(activar, ms, audio == "bajar")) return rt_.engine().lastError();
    guardar();
    return {};
}

std::string SeguridadAire::placa(bool activar, const std::string& texto, const std::string& imagen, int ms) {
    if (!rt_.engine().placa(activar, texto, imagen, ms)) return rt_.engine().lastError();
    guardar();
    return {};
}

nlohmann::json SeguridadAire::estado() const { return rt_.engine().estadoPlaca(); }

bool SeguridadAire::restaurar() {
    const nlohmann::json j = config::leerObjeto(archivoPlaca());
    const bool placa = j.value("placa", false), negro = j.value("ftb", false);
    if (placa) rt_.engine().placa(true, j.value("placa_texto", std::string()), j.value("placa_imagen", std::string()), 0);
    if (negro) rt_.engine().ftb(true, 0, j.value("ftb_audio", false));
    if ((placa || negro) && cambio_) cambio_();
    return placa || negro;
}

} // namespace ba::app::emision
