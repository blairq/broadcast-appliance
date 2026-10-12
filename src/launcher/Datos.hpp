// src/launcher/Datos.hpp — dónde viven los datos del usuario, y la mudanza
//
// En Windows y Linux los datos (Config/, Logs/, Recordings/…) viven en la misma
// carpeta portable que el programa. En macOS van AL LADO de la .app
// («<X> Datos/», ver ba::platform::dataRootFor): escribir adentro de una .app
// rompe el sello de su firma, y macOS entonces la vuelve a verificar entera
// (varios GB) y deja de reconocer su identidad para los permisos de cámara y
// micrófono.
//
// La primera vez, lo que había adentro de la .app (de versiones anteriores) se
// MUDA a la carpeta nueva. Y lo de fábrica que el usuario puede ampliar
// (Graficos/, Extensiones/) se siembra: se copia lo que falta, sin pisar nada.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ba::launcher {

/// Deja lista la raíz de datos `datos` para el bundle `bundle` y devuelve la
/// que hay que usar: `datos`, o `bundle` si no se pudo crear (p. ej. la .app
/// corre desde un disco de solo lectura). En `avisos`, lo que hizo (para el log).
std::filesystem::path prepararDatos(const std::filesystem::path& bundle, const std::filesystem::path& datos,
                                    std::vector<std::string>& avisos);

/// Las configuraciones (ver platform/Paths.hpp): la primera vez MUDA (renombra, no
/// copia) lo que era de la configuración a `Configuraciones/Principal/` y deja
/// launcher.json en la carpeta de la máquina; siempre: que la activa exista y tenga
/// las semillas de fábrica (Graficos/, Extensiones/). Devuelve la carpeta de la activa.
std::filesystem::path prepararConfiguraciones(const std::filesystem::path& bundle, const std::filesystem::path& datos,
                                              std::vector<std::string>& avisos);

} // namespace ba::launcher
