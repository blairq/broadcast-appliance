// ============================================================================
// src/motor/Sondas.hpp — cuánto agrega cada elemento de un pipeline, y en qué memoria va
//
// docs/planes/PLAN-TIEMPOS.md, nivel 2 (T3): sondas por elemento, A PEDIDO. Mientras
// dura un análisis, cada elemento del pipeline lleva una sonda de buffer en su pad
// de entrada y otra en el de salida; cada una anota la EDAD del cuadro (la hora del
// reloj del estudio menos el running time del cuadro). Lo que agrega un elemento es
// la edad a la salida menos la edad a la entrada: cuenta también lo que RETIENE (un
// filtro temporal que guarda cuadros) aunque lo procese rápido, y no depende de que
// el elemento conserve los PTS. Al terminar se sacan las sondas: fuera del análisis
// no cuesta nada.
//
// De cada pad se lee además en qué memoria va el video (las caps features:
// memory:GLMemory, memory:D3D12Memory, memory:CUDAMemory… o la del sistema): así se
// ve dónde la cadena baja de la GPU a la memoria y dónde vuelve a subir (cada cruce
// copia el cuadro entero).
// ============================================================================
#pragma once

#include <gst/gst.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

namespace ba::motor {

class Sondas {
   public:
    /// Pone las sondas en los elementos de `pipeline` (de primer nivel; un bin interno
    /// cuenta como un elemento). No toma posesión del pipeline: hay que terminar antes
    /// de que se destruya.
    explicit Sondas(GstElement* pipeline);
    ~Sondas(); ///< saca las sondas
    Sondas(const Sondas&) = delete;
    Sondas& operator=(const Sondas&) = delete;

    /// Saca las sondas y devuelve lo medido, de la fuente a la salida:
    /// {elementos: [{nombre, fabrica, medio, agrega_ms, agrega_p95_ms, edad_entrada_ms,
    ///               edad_salida_ms, cuadros, memoria_entrada, memoria_salida, caps_entrada,
    ///               caps_salida, cruce?: "baja"|"sube", nivel_cola_ms?,
    ///               puede_entrada/puede_salida: las memorias que aceptan sus pads (los
    ///               templates: lo que el elemento SABE usar, no lo que negoció),
    ///               declara_ms?: la latencia que le declara al pipeline (la consulta de
    ///               latencia: con eso compensa el estudio; si mide más, llega tarde)}],
    ///  cruces: [{elemento, que: "baja"|"sube", de, a}], segundos}
    nlohmann::json terminar();

    struct Estado;

   private:
    std::shared_ptr<Estado> st_;
};

/// Qué memoria dicen unas caps: "gl", "d3d12", "cuda", "va", "dmabuf", "sistema" o "" (sin caps).
std::string memoriaDe(GstCaps* caps);
/// ¿Esa memoria es de la GPU?
bool esGpu(const std::string& memoria);
/// Las memorias que aceptan los pads de una dirección de una fábrica (sus templates):
/// "gl", "d3d12"…, "sistema", o "cualquiera" (caps ANY). Vacío si no tiene pads así.
std::vector<std::string> memoriasPosibles(GstElementFactory* fabrica, GstPadDirection dir);

} // namespace ba::motor
