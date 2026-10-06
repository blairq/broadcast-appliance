// ============================================================================
// src/app/video/Camaras.hpp — las cámaras y capturadoras del equipo, por GStreamer
//
// Las cámaras entran como un `ba_gst_fuente` con `camara` = su id estable (el
// mismo que usaba libobs, así las guardadas se migran sin preguntar):
//   macOS    el uniqueID de AVFoundation (ba_gst_fuente busca su índice al armar)
//   Windows  la ruta del dispositivo (mfvideosrc device-path; libobs la daba
//            como «Nombre:ruta»)
//   Linux    /dev/videoN (v4l2src device)
// La lista y los modos (tamaño y cuadros) salen del monitor de dispositivos de
// GStreamer; si todavía no arrancó, de libobs.
// ============================================================================
#pragma once

#include "BroadcastEngine.hpp"

#include <string>
#include <vector>

namespace ba::app::video {

/// Las cámaras del equipo [{name, value = id}].
std::vector<BroadcastEngine::ListItem> camarasDelEquipo(BroadcastEngine& engine);

struct ModoCamara {
    int ancho = 0, alto = 0, fps = 0;
    std::string valor() const; ///< «1920x1080@30» (el `camara_modo` de ba_gst_fuente)
    std::string etiqueta() const;
};
/// Los modos que ofrece (sin repetir; de mayor a menor). Vacío si no se sabe.
std::vector<ModoCamara> modosDeCamara(const std::string& id);

/// El id de una cámara como lo daba libobs, pasado al de GStreamer.
std::string idCamaraDesdeLibobs(const std::string& id);

} // namespace ba::app::video
