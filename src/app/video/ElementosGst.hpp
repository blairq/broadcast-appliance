// ============================================================================
// src/app/video/ElementosGst.hpp — ¿están los elementos de GStreamer que hacen falta?
//
// Un plugin que falta en una plataforma (un bundle armado a medias, otra versión
// de GStreamer) no tiene que aparecer como silencio o negro sin explicación: al
// arrancar se revisa la lista de imprescindibles y lo que falta va al registro,
// a la interfaz (Configuración → Sistema y datos) y al MCP (`gst_elementos`).
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

namespace ba::app::video {

/// [{elemento, para, esta, imprescindible}] (GStreamer tiene que estar iniciado).
nlohmann::json revisarElementosGst();
/// Revisa en otro hilo (inicia GStreamer si hace falta) y anota lo que falta en el registro.
void revisarElementosGstAlArrancar();

} // namespace ba::app::video
