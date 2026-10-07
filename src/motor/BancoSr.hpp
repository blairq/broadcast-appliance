// ============================================================================
// src/motor/BancoSr.hpp — medir un método de superresolución en esta máquina
//
// El banco de pruebas de superresolución (la app arma la lista de métodos con
// lo que hay instalado: ver app/video/BancoSuperRes). Esto mide UNO, a un
// tamaño de entrada, con un pipeline sin pantalla:
//
//     videotestsrc is-live (a los cuadros del lienzo) ! <tamaño> ! <elemento> ! fakesink
//
// Qué se mide, según el elemento:
//   - los que trabajan en otro hilo (las redes: ONNX, CoreML, Apple) entregan el
//     cuadro anterior si la red no llega, así que el tiempo de la cadena no dice
//     nada (las sondas darían un cuadro justo): se lee lo que el propio elemento
//     midió de su red, como diferencia de `cuadros-red`/`ms-suma` entre el
//     principio y el fin de la ventana (el promedio exacto de esos cuadros, sin
//     arrastrar el primero, que abre la red). Si la red no procesó ninguno, no
//     corre;
//   - los sincrónicos (FSR, los shaders GLSL) no lo publican: se mide el tiempo
//     de cada cuadro entre la entrada y la salida del elemento (sondas en sus
//     pads, emparejadas por PTS) y se toma la mediana.
//
// La primera vez una red se abre (o se compila para la placa: TensorRT, ncnn):
// eso no cuenta. Se espera hasta `esperaMax` a que salga el primer cuadro y
// después se mide `segundos`.
// ============================================================================
#pragma once

#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace ba::motor {

struct CandidatoSr {
    std::string elemento = "basuperres";
    std::vector<std::pair<std::string, std::string>> propiedades; ///< como en gst-launch
    /// FSR y los shaders: el cuadro sale cuando está (se mide con sondas). Las
    /// redes no: se mide lo que el elemento dice que tardó su red.
    bool sincronico = false;
};

struct MedidaSr {
    double ms = 0;          ///< por cuadro (0 = no llegó a procesar ninguno)
    int cuadros = 0;        ///< los que salieron del elemento mientras se medía
    std::string como;       ///< "elemento" (lo que midió él) o "sondas"
    std::string acelerador; ///< si el elemento lo dice (neural-engine, gpu, dml…)
    std::string error;      ///< si no se pudo armar o el elemento dio error
    double abrirSeg = 0;    ///< lo que tardó en salir el primer cuadro (abrir/compilar la red)
};

/// Mide `c` con entradas de `ancho`×`alto` a `fpsN/fpsD`. Bloquea (correr en un
/// hilo aparte); `cancelar` lo corta.
MedidaSr medirSuperRes(const CandidatoSr& c, int ancho, int alto, int fpsN, int fpsD, double segundos = 4.0,
                       double esperaMax = 60.0, const std::atomic<bool>* cancelar = nullptr);

} // namespace ba::motor
