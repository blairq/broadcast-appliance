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
//   - LATENCIA: un módulo con lookahead (limitador, compresor, EQ de fase
//     lineal) atrasa el audio. `barack` la suma a la consulta LATENCY (así el
//     mezclador espera lo justo), adelanta el sello de lo que sale en esa
//     latencia (el contenido queda con su hora real de captura y no se corre
//     contra su video ni contra los otros micrófonos) y, cuando cambia (otro
//     plan del rack, el anfitrión que llega o se va), lo avisa con un mensaje
//     de latencia para que la tubería la vuelva a calcular.
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
    /// Lo que atrasa el audio, en muestras a 48 kHz (la latencia del rack). Se
    /// lee desde el hilo de audio en cada bloque: tiene que ser una lectura atómica.
    virtual std::uint32_t latenciaMuestras() const { return 0; }
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
