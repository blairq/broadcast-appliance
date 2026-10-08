// ============================================================================
// src/app/arranque/RutasPortables.hpp — en la configuración, todas las rutas relativas
//
// Todo es portable: la carpeta de datos (y cada configuración) se puede mover o
// copiar a otra máquina y lo guardado tiene que seguir apuntando bien. Al GUARDAR,
// cada ruta absoluta que cae dentro de la configuración o de la máquina se escribe
// relativa («Media/logo.png», «Modelos/superres/x.onnx»; una URI file:// como
// «file:Media/video.mp4»); al LEER, lo relativo que empieza por una carpeta de los
// datos vuelve a ser la ruta (o la URI) de ahora. Lo que está afuera (otro disco,
// una URL) queda tal cual.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

namespace ba::app {

/// Una copia de `j` con las rutas de adentro de los datos relativas.
nlohmann::json rutasRelativas(const nlohmann::json& j);
/// Una copia de `j` con las rutas relativas a los datos resueltas (rutas y URIs de ahora).
nlohmann::json rutasAbsolutas(const nlohmann::json& j);

} // namespace ba::app
