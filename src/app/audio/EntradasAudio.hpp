// ============================================================================
// src/app/audio/EntradasAudio.hpp — los micrófonos del equipo, por GStreamer
//
// Los micrófonos y líneas entran como un pipeline de GStreamer (`ba_gst_fuente`
// con `pipeline_audio`), no como fuentes de libobs: así los filtros de audio
// (el automixer, los plugins) corren en la capa de entrada y el día que se va
// libobs no se pierde nada.
//
//   macOS    osxaudiosrc unique-id="<UID de Core Audio>"   (el mismo id que usaba libobs)
//   Windows  wasapi2src device="<id del dispositivo>"     (el de libobs, {0.0.1…}, se convierte)
//   Linux    pulsesrc device="<nombre de PulseAudio>"     (el mismo id que usaba libobs)
//
// "default" es la entrada predeterminada del sistema (y la sigue si cambia).
// ============================================================================
#pragma once

#include "emision/BroadcastEngine.hpp"

#include <string>
#include <vector>

namespace ba::app::audio {

/// Las entradas de audio del equipo [{name, value}], la predeterminada primero.
/// Con GStreamer andando, de su monitor de dispositivos; si todavía no arrancó,
/// la lista de libobs (convertida al id de GStreamer).
std::vector<BroadcastEngine::ListItem> entradasDeAudio(BroadcastEngine& engine);

/// La parte de origen del pipeline para el dispositivo (para `pipeline_audio`).
std::string pipelineDeMicrofono(const std::string& dispositivo);

/// El id de un dispositivo como lo daba libobs, pasado al de GStreamer (en
/// Windows cambia; en Mac y Linux es el mismo).
std::string idDesdeLibobs(const std::string& id);

} // namespace ba::app::audio
