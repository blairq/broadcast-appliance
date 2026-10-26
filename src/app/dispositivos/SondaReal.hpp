// ============================================================================
// src/app/dispositivos/SondaReal.hpp — las fuentes de drivers de verdad
//
//   GStreamer (SondaGst.cpp): cada proveedor de dispositivos con rango para
//     que lo usen los monitores (WASAPI, Media Foundation, Core Audio,
//     AVFoundation, Pulse, V4L2, DeckLink, los de los plugins). Se listan desde
//     el registro de GStreamer sin cargar el plugin; probar uno es cargarlo y
//     pedirle los dispositivos.
//   ASIO (SondaAsio.cpp, solo Windows): los drivers de HKLM\SOFTWARE\ASIO, sin
//     cargarlos; probar uno es crearlo por COM, init() y preguntarle los canales.
//
// El proveedor de ASIO de GStreamer NO se lista: carga TODOS los drivers ASIO
// a la vez (eso era el problema) y queda con rango NONE (platform/GstEntorno);
// cada driver ASIO es una entrada propia.
// ============================================================================
#pragma once

#include "dispositivos/SondaDrivers.hpp"

namespace ba::app::dispositivos::sonda {

Fuente fuenteGStreamer();
Fuente fuenteAsio(); ///< fuera de Windows no lista nada

/// El modo sonda de ba-app (main.cpp): GStreamer, ASIO y la de prueba.
int principalDeLaApp(int argc, char** argv);

/// En ESTE proceso (la app): el proveedor `nombre` de GStreamer con `rango`
/// (GST_RANK_NONE = 0: ningún monitor de dispositivos lo carga). Si GStreamer
/// todavía no arrancó no hace nada (al arrancar lo toma de GstEntorno).
void ponerRangoDeProveedor(const std::string& nombre, int rango);

} // namespace ba::app::dispositivos::sonda
