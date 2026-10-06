// ============================================================================
// src/app/audio/GstVoz.hpp — las voces sintetizadas como fuente de GStreamer (`bavoz`)
//
// «Voz del sistema» y la del teleprompter (MonitorVoz) entran por GStreamer:
// una entrada del motor con `bavoz voz=<nombre>`, así su rack corre en barack.
// MonitorVoz arma las frases
// (encoladas, pegadas, o para empezar a una hora: el teleprompter) y las deja
// en la COLA de su voz; el elemento las entrega con esa hora pasada al reloj
// del pipeline (los dos relojes leídos juntos), así una frase para dentro de
// 2 s sale dentro de 2 s.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ba::app::audio::gstvoz {

/// Registra `bavoz` (lo llama gstrack::registrarElemento, con GStreamer andando).
bool registrarElemento();
bool registrado();

/// El reloj de las horas que llegan (MonitorVoz pone ba::app::ahoraNs;
/// de fábrica, steady_clock, que en los tres sistemas es el mismo).
void ponerReloj(std::uint64_t (*reloj)());

/// Encola un tramo de `n` cuadros, mono o estéreo intercalado (`ts`: dónde
/// empieza, en el reloj de la app: ba::app::ahoraNs).
void encolar(const std::string& voz, const float* muestras, std::size_t n, std::uint32_t frecuencia, std::uint64_t ts,
             int canales = 1);
/// Tira lo encolado de la voz (callarla ya).
void vaciar(const std::string& voz);

} // namespace ba::app::audio::gstvoz
