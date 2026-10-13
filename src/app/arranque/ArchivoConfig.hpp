// ============================================================================
// src/app/arranque/ArchivoConfig.hpp — leer y guardar un JSON de configuración (modelo)
//
// Lo común de los ajustes chicos que se guardan en Config/ (o Runtime/): un
// objeto JSON por archivo. Leer uno que no está o está roto da {} (rige lo de
// fábrica); guardar crea la carpeta si hace falta.
// ============================================================================
#pragma once

#include <filesystem>

#include <nlohmann/json.hpp>

namespace ba::app::config {

/// El objeto del archivo, o {} si no está o no es un objeto.
nlohmann::json leerObjeto(const std::filesystem::path& archivo);
/// Lo guarda (con la carpeta). false si no se pudo escribir.
bool guardar(const std::filesystem::path& archivo, const nlohmann::json& j);

} // namespace ba::app::config
