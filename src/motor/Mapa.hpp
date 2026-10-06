// ============================================================================
// src/motor/Mapa.hpp — el mapa vivo de todos los pipelines del motor
//
// Para encontrar dónde el video (o el audio) pasa de la GPU a la CPU: cada
// pipeline que arma el motor se anota acá (lo hace Reloj::adoptar, por donde
// pasan todos) y `mapa()` recorre cada elemento —también los de adentro de los
// bins, como el decodificador que eligió uridecodebin3— y cada enlace con las
// caps NEGOCIADAS de verdad:
//   - la memoria de cada enlace: la caps feature (memory:GLMemory,
//     memory:D3D11Memory, memory:D3D12Memory, memory:VAMemory,
//     memory:CUDAMemory, memory:VKMemory…) o «sistema» si no tiene (CPU);
//   - las TRANSICIONES: un elemento que recibe memoria de GPU y entrega de
//     sistema (una bajada a la CPU) o al revés (una subida);
//   - el TRABAJO EN CPU: los elementos que procesan video crudo en memoria de
//     sistema (convertir, escalar, componer, filtrar), y los decodificadores y
//     codificadores que no son de hardware (GStreamer marca los de hardware con
//     «Hardware» en su clase).
// Cada pipeline se nombra por lo que lee y escribe por `inter` (los
// productores de sus intersrc/intersink), que es como el motor los conecta.
// ============================================================================
#pragma once

#include <gst/gst.h>

#include <nlohmann/json.hpp>

namespace ba::motor {

/// Anota un pipeline (se borra solo cuando se destruye).
void registrarPipeline(GstElement* pipeline);
/// El mapa de todos los pipelines vivos: {pipelines: [...], resumen: {...}}.
nlohmann::json mapa();

} // namespace ba::motor
