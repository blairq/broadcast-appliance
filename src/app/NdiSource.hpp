// ============================================================================
// src/app/NdiSource.hpp — Entrada NDI® sin el SDK propietario
//
// Fuente de libobs "ba_ndi_input" que recibe una señal NDI con libNoIDea
// (VideoLAN, LGPL, hecha por ingenieria inversa para interoperabilidad; ALFA)
// y decodifica el video SpeedHQ con el FFmpeg del bundle. Descubrimiento de
// fuentes en la red por mDNS (libmicrodns). Recibe NDI "completo" (SpeedHQ),
// no NDI|HX (H.264/H.265).
//
// NDI® es una marca de Vizrt NDI AB; este programa no esta afiliado a Vizrt.
//
// Threading: cada instancia tiene su thread de recepcion (conecta, reconecta
// sola, decodifica y entrega cuadros/audio a libobs). El descubrimiento corre
// en su propio thread de libNoIDea; la lista se consulta con un mutex.
// ============================================================================
#pragma once

#include <string>
#include <vector>

namespace ba::app::ndi {

constexpr const char* kSourceId = "ba_ndi_input";

struct Found {
    std::string name; ///< como lo anuncia el emisor ("PC (Camara 1)")
    std::string host;
    int port = 0;
};

/// Registra el tipo de fuente (despues de cargar los modulos de libobs).
void registerSourceType();

/// Busca fuentes NDI en la red durante `ms` y devuelve lo encontrado
/// (tambien lo ya visto antes).
std::vector<Found> discover(int ms);

/// Estado de las instancias: {name, host, port, connected, width, height, fps}.
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
