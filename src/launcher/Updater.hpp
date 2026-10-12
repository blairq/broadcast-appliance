// ============================================================================
// src/launcher/Updater.hpp — Actualizar el sistema sin tocar los datos
//
// `BroadcastAppliance --update <carpeta de la version nueva> [--start]`
//
//   1. Espera a que la app este cerrada (toma el lock de instancia unica).
//   2. Valida la version nueva (launcher + ba-app; manifest.json si trae).
//   3. Respalda Credenciales/ y Config/ en Respaldos/<fecha>/ (quedan las 5
//      ultimas).
//   4. Reemplaza SOLO el programa: todo lo que trae la version nueva salvo las
//      carpetas de datos (Config, Credenciales, Logs, Recordings, Shares,
//      Media, Runtime, Cache, Respaldos, Agentes), que nunca se tocan. Primero copia todo a
//      una carpeta de preparacion y recien despues intercambia (renombres en
//      el mismo disco): si algo falla a mitad, se deshace y queda la version
//      anterior entera.
//   5. La version anterior queda en `.actualizacion-anterior/` para volver
//      atras: `BroadcastAppliance --update-rollback`.
//   6. Vacia Cache/ (QML compilado de la version vieja).
//
// Con --start, al terminar arranca el sistema normalmente.
// ============================================================================
#pragma once

#include <filesystem>
#include "ajustes/Ajustes.hpp"
#include <string>
#include <vector>

namespace ba::launcher {

struct UpdateOptions {
    std::filesystem::path root;   ///< raiz del sistema instalado
    /// La carpeta de DATOS (Config/, Credenciales/): en Windows y Linux es `root`; en macOS
    /// está al lado de la .app. Vacía = `root`. El respaldo se hace acá.
    std::filesystem::path datos;
    std::filesystem::path source; ///< carpeta de la version nueva (vacio en rollback)
    bool rollback = false;
    int waitLockMs = ba::ajustes::sAMs("launcher.esperaCierreS", 60); ///< cuanto esperar a que la app cierre
    std::string lockName = "launcher"; ///< lock de instancia unica del launcher
};

struct UpdateResult {
    bool ok = false;
    std::string message;               ///< explicacion para la persona
    std::vector<std::string> replaced; ///< elementos reemplazados
    std::filesystem::path backup;      ///< respaldo de datos creado
};

/// Carpetas de DATOS: una actualizacion nunca las toca.
bool isDataEntry(const std::string& name);

UpdateResult runUpdate(const UpdateOptions& opt);

} // namespace ba::launcher
