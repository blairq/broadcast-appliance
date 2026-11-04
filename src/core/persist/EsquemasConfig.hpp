// ============================================================================
// src/core/persist/EsquemasConfig.hpp — la tabla de esquemas de los JSON de Config/
//
// La versión de cada archivo de Config/ y sus pasos de migración (la tabla vive en
// ArchivoConfig.cpp). Detalle de la persistencia: quien lee y guarda usa
// ArchivoConfig.hpp; esto lo incluyen ArchivoConfig.cpp y los tests de la tabla.
//
// Sin Qt (ba_core).
// ============================================================================
#pragma once

#include "persist/Esquema.hpp"

#include <string>
#include <utility>
#include <vector>

namespace ba::persist {

/// La versión de un archivo de Config/ y cómo se llega a ella desde la 1.
struct EsquemaConfig {
    EsquemaConfig() = default;
    EsquemaConfig(std::string n, int a = 1, std::vector<PasoMigracion> p = {})
        : nombre(std::move(n)), actual(a), pasos(std::move(p)) {}

    std::string nombre;               ///< el nombre del archivo («audio-matriz.json»)
    int actual = 1;                   ///< el esquema que escribe esta versión
    std::vector<PasoMigracion> pasos; ///< pasos.size() == actual - 1
};

/// La tabla: todos los archivos de Config/ que conoce el programa.
const std::vector<EsquemaConfig>& esquemasConfig();
/// El de `nombre` (el nombre del archivo, sin carpeta); si no está en la tabla, el 1.
const EsquemaConfig& esquemaConfig(const std::string& nombre);

} // namespace ba::persist
