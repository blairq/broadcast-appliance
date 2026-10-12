// src/app/agent/RotuloTraduccionConfig.cpp — ver el .hpp

#include "agent/RotuloTraduccionConfig.hpp"

#include "arranque/ArchivoConfig.hpp"

#include <algorithm>

namespace ba::app::agent {

RotuloTraduccionConfig::RotuloTraduccionConfig(std::filesystem::path dataRoot)
    : archivo_(std::move(dataRoot) / "Config" / "rotulo-traduccion.json") {
    const nlohmann::json j = config::leerObjeto(archivo_);
    activo_ = j.value("activo", true);
    capa_ = std::clamp(j.value("capa", 4), 1, 4);
}

void RotuloTraduccionConfig::setActivo(bool activo) {
    activo_ = activo;
    config::guardar(archivo_, {{"activo", activo_}, {"capa", capa_}});
}

} // namespace ba::app::agent
