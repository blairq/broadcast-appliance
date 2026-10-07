// ============================================================================
// src/app/emision/NdiSource.hpp — Entrada NDI® sin el SDK propietario, por GStreamer
//
// Recibe una señal NDI con libNoIDea (VideoLAN, LGPL, hecha por ingeniería
// inversa para interoperabilidad; ALFA) y la entrega como el elemento de
// GStreamer `bandisrc` (registrado por la app): pads «video» (SpeedHQ
// comprimido, que decodifica `avdec_speedhq` de GStreamer) y «audio». Una
// entrada NDI es un `ba_gst_fuente` con eso (InputSpecs::ajustesNdi), así sus
// tarjetas y su rack corren en GStreamer. Descubrimiento de fuentes en la red
// por mDNS (libmicrodns). Recibe NDI «completo» (SpeedHQ), no NDI|HX.
//
// NDI® es una marca de Vizrt NDI AB; este programa no está afiliado a Vizrt.
//
// Threading: cada `bandisrc` tiene su hilo de recepción (conecta, reconecta
// solo y empuja lo que llega a sus appsrc). El descubrimiento corre en su
// propio hilo de libNoIDea; la lista se consulta con un mutex.
// ============================================================================
#pragma once

#include <string>
#include <vector>

namespace ba::app::ndi {

/// El tipo «lógico» de una entrada NDI (para la interfaz y las plantillas; la
/// fuente de libobs real es ba_gst_fuente).
constexpr const char* kSourceId = "ba_ndi_input";

struct Found {
    std::string name; ///< como lo anuncia el emisor ("PC (Camara 1)")
    std::string host;
    int port = 0;
};

/// Registra `bandisrc` en GStreamer (con GStreamer andando; una vez).
bool registrarElemento();
/// Ya no hay una fuente de libobs propia (se usa bandisrc): no hace nada.
void registerSourceType();

/// Busca fuentes NDI en la red durante `ms` y devuelve lo encontrado
/// (también lo ya visto antes).
std::vector<Found> discover(int ms);

/// Estado de los receptores: {name, host, port, connected, width, height, fps}.
struct Status {
    std::string source, host;
    int port = 0;
    bool connected = false;
    int width = 0, height = 0;
    double fps = 0;
    std::string error;
};
std::vector<Status> statuses();

} // namespace ba::app::ndi
