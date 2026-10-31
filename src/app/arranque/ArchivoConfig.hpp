// ============================================================================
// src/app/arranque/ArchivoConfig.hpp — leer y guardar un JSON de configuración (modelo)
//
// Lo común de los ajustes chicos que se guardan en Config/ (o Runtime/): un
// objeto JSON por archivo. Leer uno que no está o está roto da {} (rige lo de
// fábrica); guardar crea la carpeta si hace falta.
//
// Guardar ENCOLA en el escritor de la persistencia (core/persist/ColaEscritura.hpp):
// el disco (temporal + fsync + rename) lo toca su hilo, nunca el de la UI. Leer ve
// lo encolado aunque todavía no haya llegado al disco.
// ============================================================================
#pragma once

#include <filesystem>

#include <nlohmann/json.hpp>

namespace ba::app::config {

/// El objeto del archivo, o {} si no está o no es un objeto.
nlohmann::json leerObjeto(const std::filesystem::path& archivo);
/// Lo encola para guardar (con la carpeta). Siempre true: si el disco falla, el
/// escritor lo reintenta y lo anota en el registro.
bool guardar(const std::filesystem::path& archivo, const nlohmann::json& j);

} // namespace ba::app::config
