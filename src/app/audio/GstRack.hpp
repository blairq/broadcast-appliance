// ============================================================================
// src/app/audio/GstRack.hpp — el rack de audio como elemento de GStreamer (`barack`)
//
// Los racks (AudioRack: módulos LSP/CLAP, el grafo tipo JACK) de las entradas
// que ya son de GStreamer (micrófonos, red, pipelines: `ba_gst_fuente`) corren
// en la cadena de audio de la entrada, no como filtro de libobs. La app
// registra el elemento `barack fuente=<nombre>` en GStreamer (estático, dentro
// del proceso: ve los racks de RackManager) y `ba_gst_fuente` lo pone primero
// en la cadena del audio cuando su ajuste `rack` lo pide.
//
//   - RackManager PUBLICA el anfitrión de cada fuente (el que procesa: el rack,
//     su escucha, su grabación y su voz a texto) y lo RETIRA al sacar el rack.
//   - El elemento busca el anfitrión de su `fuente` (sin bloquear el hilo de
//     audio: si el registro está ocupado, prueba en el bloque siguiente) y le
//     pasa el audio en bloques planares de hasta 1024 muestras. Sin anfitrión
//     (la app todavía no restauró el rack), el audio pasa tal cual.
//   - 48 kHz estéreo F32 (lo que arma ba_gst_fuente delante).
// ============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace ba::app::audio::gstrack {

/// Lo que procesa el audio de una fuente (lo implementa el rack de RackManager).
struct Anfitrion {
    virtual ~Anfitrion() = default;
    /// Hilo de audio de GStreamer. `planos`: 2 canales de `frames` (≤ 1024) muestras.
    virtual void procesar(float* const* planos, int canales, std::uint32_t frames) = 0;
};

/// Registra `barack` en GStreamer (una vez; GStreamer tiene que estar iniciado).
bool registrarElemento();
/// Inicia GStreamer en otro hilo y registra `barack` (no bloquea).
void registrarEnSegundoPlano();
bool elementoRegistrado();

void publicar(const std::string& fuente, std::shared_ptr<Anfitrion> a);
void retirar(const std::string& fuente);
/// ¿Hay un `barack` procesando el audio de esa fuente ahora (en el último segundo)?
bool procesando(const std::string& fuente);

} // namespace ba::app::audio::gstrack
