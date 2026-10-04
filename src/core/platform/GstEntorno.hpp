// src/core/platform/GstEntorno.hpp — GStreamer, solo con lo del bundle
//
// GStreamer busca plugins por todos lados (la carpeta del sistema, la del
// usuario, variables de entorno) y guarda un registro en el perfil. El
// appliance es portable: GStreamer viaja adentro del bundle
// (bin/64bit/GStreamer.framework en macOS; bin/64bit/gstreamer en Windows y
// Linux) y tiene que usar SOLO eso. Esta función fija las variables antes de
// `gst_init`: la ruta de los plugins, el escáner, el registro (en Cache/) y
// los módulos de GIO, y vacía las rutas de afuera.
//
// No enlaza con GStreamer (solo arma el entorno): la usan el módulo de video
// (ba-sources) y la app, cada una antes de su gst_init.
#pragma once

#include <filesystem>

namespace ba::platform {

/// La raíz de GStreamer del bundle (la que tiene lib/gstreamer-1.0), o vacía si
/// no está. En desarrollo, BA_GST_RAIZ la puede indicar.
std::filesystem::path gstRaiz();

/// Fija el entorno de GStreamer para usar solo lo del bundle. Devuelve false si
/// no hay GStreamer en el bundle (entonces no hay que llamar a gst_init).
bool prepararGst();

} // namespace ba::platform
