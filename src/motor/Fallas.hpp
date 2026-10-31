// ============================================================================
// src/motor/Fallas.hpp — romper a propósito una salida (banco de caos, F1-14)
//
// Lo que programó diag_falla (solo con --test) en ba::fault::FallasProgramadas se
// aplica acá, en el tic de cada salida (Salidas.cpp), sobre su pipeline:
//
//   codificador: el elemento «codificador» avisa un error de codificación
//                (GST_STREAM_ERROR_ENCODE, como un NVENC/VideoToolbox que se cae) y
//                deja de sacar cuadros;
//   sumidero:    el que escribe (splitmuxsink «mux» de la grabación, rtmp2sink/srtsink
//                «red» de la transmisión) avisa un error de escritura
//                (GST_RESOURCE_ERROR_WRITE, como un disco que se va o una conexión que
//                se corta) y no recibe más nada.
//
// El mensaje es el mismo que da la falla de verdad, por el bus del pipeline: lo
// atiende el camino de siempre (Transmision::tic reconecta; la grabación, lo que
// haga F1-05). Sin nada programado, una carga atómica y vuelve.
// ============================================================================
#pragma once

#include <gst/gst.h>

#include <string>

namespace ba::motor {

/// `salida`: grabacion | transmision (ver FallasProgramadas).
/// `codificacion`: el pipeline del codificador que lee esa salida (la Codificacion
/// compartida): una falla de «codificador» va ahí, no al pipeline de la salida.
void aplicarFallasProgramadas(GstElement* pipeline, const std::string& salida, GstElement* codificacion = nullptr);

} // namespace ba::motor
