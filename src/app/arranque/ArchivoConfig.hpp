// ============================================================================
// src/app/arranque/ArchivoConfig.hpp — leer y guardar un JSON de Config/ (modelo)
//
// Los nombres de siempre de la app para el lector/escritor único con esquema del
// núcleo (core/persist/ArchivoConfig.hpp): ahí está qué hace y la tabla de esquemas.
// ============================================================================
#pragma once

#include "persist/ArchivoConfig.hpp"

namespace ba::app::config {

/// El objeto del archivo migrado a su esquema, o {} si no está o no es un objeto.
inline nlohmann::json leerObjeto(const std::filesystem::path& archivo) { return ba::persist::leerConfig(archivo); }
/// Lo marca con su esquema y lo encola para guardar (con la carpeta). false solo si
/// lo escribió una versión más nueva (no se pisa).
inline bool guardar(const std::filesystem::path& archivo, const nlohmann::json& j, ba::persist::Pedido p = {}) {
    return ba::persist::guardarConfig(archivo, j, std::move(p));
}

} // namespace ba::app::config
