// ============================================================================
// src/core/platform/Paths.hpp — Rutas del proceso y del bundle portable
//
// Proposito:
//   Resolver, igual en las tres plataformas, donde esta el ejecutable, la raiz
//   del bundle portable (layout §5.1) y el directorio de runtime del usuario
//   (sockets de control, lockfiles).
//
//   Es la primera pieza de `ba::platform`: la capa unica donde viven los
//   `#ifdef` de sistema. La implementacion se elige por archivo en CMake
//   (`Paths_win.cpp`, `Paths_linux.cpp`, `Paths_mac.cpp`); la logica comun
//   (resolver la raiz del bundle) esta en `Paths.cpp` y no depende del SO.
//
// Layout que se reconoce para la raiz del bundle:
//   - `<root>/bin/64bit/<exe>`          -> ba-app, ba-gateway (Windows/Linux)
//   - `<root>/<exe>`                    -> el launcher (vive en la raiz, §5.1)
//   - `<X>.app/Contents/MacOS/<exe>`    -> launcher en macOS; root = Contents
//   - `<X>.app/Contents/bin/64bit/<exe>`-> A y B en macOS; root = Contents
//
// Estrategia de errores:
//   No lanza. Si el SO no permite resolver el ejecutable se devuelve una ruta
//   vacia y los llamadores caen al directorio de trabajo.
// ============================================================================

#pragma once

#include <filesystem>

namespace ba::platform {

/// Ruta absoluta del ejecutable del proceso actual (vacia si no se pudo).
std::filesystem::path executablePath();

/// Directorio que contiene al ejecutable (vacio si no se pudo).
std::filesystem::path executableDir();

/// Raiz del bundle portable a partir de un directorio de ejecutable.
///
/// Funcion pura (no toca el disco): aplica las reglas de layout de arriba.
/// Separada de `bundleRoot()` para poder testearla en cualquier plataforma.
///
/// :param exeDir: directorio del ejecutable.
/// :returns: la raiz del bundle; `exeDir` si no coincide ningun layout.
std::filesystem::path bundleRootFor(const std::filesystem::path& exeDir);

/// Raiz del bundle portable del proceso actual (`bundleRootFor(executableDir())`).
///
/// Es de donde salen los BINARIOS (bin/, obs-plugins/, data/). Con copia
/// sombra (spec 3.1.5) apunta a la copia, no al disco.
std::filesystem::path bundleRoot();

/// Variable de entorno con la que el launcher le indica a A y B la raiz REAL.
inline constexpr const char* kDataRootEnv = "BA_DATA_ROOT";

/// Raiz de los DATOS del usuario (Config/, Logs/, Recordings/, Web/).
///
/// Cuando A y B corren desde la copia sombra, sus binarios estan en una cache
/// pero los datos viven en el disco real: el launcher lo informa por
/// `BA_DATA_ROOT`. Sin esa variable (proceso lanzado a mano) es la raiz del
/// bundle.
std::filesystem::path dataRoot();

/// Directorio de runtime privado del usuario para sockets y lockfiles.
///
/// Windows: `%LOCALAPPDATA%\BroadcastAppliance` (los pipes no lo usan, los
/// locks si). Linux: `$XDG_RUNTIME_DIR/broadcast-appliance`. macOS:
/// `$TMPDIR/broadcast-appliance` (TMPDIR es por usuario en macOS). Si la
/// variable no esta, `/tmp/broadcast-appliance-<uid>`. Se crea con permisos
/// 0700 en POSIX: el canal de control no debe ser accesible por otros usuarios.
///
/// :returns: el directorio (creado), o vacio si no se pudo crear.
std::filesystem::path runtimeDir();

} // namespace ba::platform
