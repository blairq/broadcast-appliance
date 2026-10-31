// ============================================================================
// src/motor/OrigenRed.hpp — una sola conexión por cámara de red
//
// Varias entradas pueden salir de la MISMA cámara (la original, una con
// superresolución, otra afilada…). Antes cada una abría su propia sesión RTSP:
// la cámara atiende pocas a la vez y las que sobraban quedaban «sin señal
// (reintentando)». Ahora hay un origen por URI que recibe y decodifica UNA vez y
// publica el video y el audio crudos (intersink «origen:<n>:video|audio»); cada
// entrada se cuelga con un intersrc y pone sus filtros. De yapa, cambiar un
// filtro de una entrada ya no reconecta la cámara.
//
// El origen reintenta solo si la cámara se cae, y se va unos segundos después de
// que lo suelta la última entrada (un rearmado no lo corta).
// ============================================================================
#pragma once

#include <gst/gst.h>

#include <string>

namespace ba::motor {

class Reloj;

namespace origen_red {

/// ¿Es una dirección de red que conviene compartir? (rtsp, rtmp, srt, http…; no archivos).
bool esDeRed(const std::string& uri);

/// Cómo se recibe una dirección (lo que guardó la entrada). Todo es parte de la
/// clave del origen: con otros valores, otra conexión.
struct Opciones {
    int latenciaMs = 200;       ///< el colchón de RTSP/SRT
    bool ntp = false;           ///< RTSP: la hora de captura del que manda
    std::string transporteRtsp; ///< "tcp", "udp" o "" (lo que negocie rtspsrc)
    std::string ca;             ///< raíces para verificar https/rtsps/rtmps ("" = las de GIO)
    int reconectarMs = 1000;    ///< primera espera para reconectar (después se duplica hasta 10 s)
};

/// Pide el origen de `uri` (lo crea si no existe) y devuelve el prefijo del
/// productor: «origen:<n>».
std::string usar(const Reloj& reloj, const std::string& uri, const Opciones& op);

/// Suelta el origen pedido con `usar` (por el prefijo).
void soltar(const std::string& prefijo);

/// Lo llama el bucle del motor: mensajes del bus, reintentos y los que quedaron sin usar.
void tic();

} // namespace origen_red
} // namespace ba::motor
