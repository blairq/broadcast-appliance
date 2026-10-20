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

#include "emision/BroadcastEngine.hpp"

#include "ModosVideo.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ba::app::video {

/// Las cámaras del equipo [{name, value = id}].
std::vector<BroadcastEngine::ListItem> camarasDelEquipo(BroadcastEngine& engine);

/// Un modo de un dispositivo de video: tamaño, cps, formato real (crudo, MJPEG, H.264,
/// H.265), quién lo decodifica y si es un crudo que fabrica el sistema (ver motor/ModosVideo).
using ModoCamara = ba::motor::ModoVideo;
/// Los modos que ofrece (sin repetir; de mayor a menor). Vacío si no se sabe.
std::vector<ModoCamara> modosDeCamara(const std::string& id);
/// El que se elige solo para el lienzo actual, con el porqué.
ModoCamara modoAutomatico(const std::string& id, std::string* porque = nullptr);
/// Todo lo que se sabe de los dispositivos de video (del monitor de GStreamer, como
/// gst-device-monitor): [{nombre, id, api, usable, clase, propiedades, caps (las crudas,
/// una por renglón), modos (ModoVideo::json), automatico {…, porque}}]. `id` vacío: todos.
nlohmann::json fichasDeVideo(const std::string& id = {});

/// El id de una cámara como lo daba libobs, pasado al de GStreamer.
std::string idCamaraDesdeLibobs(const std::string& id);

} // namespace ba::app::video
