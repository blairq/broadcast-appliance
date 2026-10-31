// ============================================================================
// src/core/persist/ArchivoConfig.hpp — leer y guardar un JSON de Config/ con su esquema
//
// docs/arquitectura/04-plan-unificado.md, F3-06. TODOS los JSON de configuración de
// un evento (Config/*.json) se leen y se guardan por acá: un objeto por archivo.
//
//   - Leer uno que no está o está roto da {} (rige lo de fábrica). Uno que está sale
//     migrado al esquema actual (persist/Esquema.hpp) y con `"esquema": N`.
//   - Guardar lo marca con el esquema actual y lo ENCOLA en el escritor de la
//     persistencia (ColaEscritura.hpp): el disco lo toca su hilo, nunca el de la UI.
//     Leer ve lo encolado aunque todavía no haya llegado al disco.
//   - Uno que escribió una versión más nueva del programa (esquema mayor al de esta)
//     se lee como está y NO se pisa al guardar: se avisa en el registro y lo que se
//     quiso guardar queda solo en memoria (volver a la versión vieja no rompe la
//     configuración de la nueva).
//
// La versión de cada archivo y sus pasos de migración están en UNA tabla
// (ArchivoConfig.cpp): cambiar el formato de un archivo es subirle el número y
// agregar el paso ahí. El que no está en la tabla es el esquema 1, sin pasos.
//
// Sin Qt (ba_core): lo usan la app y el núcleo.
// ============================================================================
#pragma once

#include "persist/ColaEscritura.hpp"
#include "persist/Esquema.hpp"

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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

/// El objeto del archivo migrado al esquema actual, o {} si no está o no es un objeto.
/// `habia` (si se pide): si el archivo estaba, aunque esté roto (lo encolado cuenta).
nlohmann::json leerConfig(const std::filesystem::path& archivo, bool* habia = nullptr);
/// Lo marca con el esquema actual y lo encola para guardar (con la carpeta). false
/// solo si el archivo lo escribió una versión más nueva (no se pisa; ver arriba); si
/// el disco falla, el escritor lo reintenta y lo anota en el registro.
bool guardarConfig(const std::filesystem::path& archivo, nlohmann::json j, Pedido p = {});
/// Lo mismo pero ya, sin la cola: vuelve con el archivo en el disco. Para lo que tiene
/// que estar escrito antes de seguir aunque el proceso se caiga enseguida (la marca de
/// un driver que se va a probar). Solo para archivos que nadie guarda por la cola.
bool guardarConfigYa(const std::filesystem::path& archivo, nlohmann::json j);

} // namespace ba::persist
