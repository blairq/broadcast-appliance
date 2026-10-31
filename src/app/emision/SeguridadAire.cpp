// src/app/emision/SeguridadAire.cpp — ver el .hpp

#include "emision/SeguridadAire.hpp"

#include "arranque/AppRuntime.hpp"
#include "i18n/I18n.hpp"

namespace ba::app::emision {

SeguridadAire::SeguridadAire(AppRuntime& rt) : rt_(rt) {}

void SeguridadAire::guardar() {
    // Es estado de aire: la sección de las salidas del documento de aire (F3-04). El
    // runtime la vuelve a poner con lo que dice el motor; la escribe el dueño del documento.
    // Y la foto del motor (lo que muestra la interfaz) se rearma.
    rt_.aireCambio();
}

std::string SeguridadAire::ftb(bool activar, int ms, const std::string& audio, const std::string& salida) {
    if (audio != "bajar" && audio != "mantener") return ba::i18n::traducir("emision", "audio: bajar o mantener");
    const TurnoMotor::Guarda turno(rt_.engine().turno()); // el negro y su lugar en el documento, juntos
    if (!rt_.engine().ftb(activar, ms, audio == "bajar", salida)) return rt_.engine().lastError();
    guardar();
    return {};
}

std::string SeguridadAire::placa(bool activar, const std::string& texto, const std::string& imagen, int ms,
                                 const std::string& salida) {
    const TurnoMotor::Guarda turno(rt_.engine().turno());
    if (!rt_.engine().placa(activar, texto, imagen, ms, salida)) return rt_.engine().lastError();
    guardar();
    return {};
}

nlohmann::json SeguridadAire::estado(const std::string& salida) const { return rt_.engine().estadoPlaca(salida); }

} // namespace ba::app::emision
