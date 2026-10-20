// ============================================================================
// src/motor/ModosVideo.hpp — qué modos da un dispositivo de video y cuál conviene
//
// docs/planes/PLAN-RESOLUCION.md, fase 1. Cualquier dispositivo de video que
// muestre GstDeviceMonitor (USB, capturadora, la cámara de la notebook) anuncia
// sus modos en sus caps: crudos en algún formato de píxel, MJPEG, H.264, H.265.
// Antes se usaban solo los crudos; una cámara USB 2 da 1080p a 30 solo en MJPEG,
// y en Windows Media Foundation lo disfrazaba: lo decodificaba en la CPU por dentro
// y lo ofrecía como NV12 (se ve en las caps: a ese tamaño los otros crudos llegan
// a 5 cps y el NV12 a 30).
//
// Acá: los modos con su formato real, el decodificador que usaría cada uno (el de
// mayor rango del registro que acepte ese formato: D3D12, VA, VideoToolbox,
// NVIDIA, Intel… o el de CPU; nada armado a mano por marca) y la elección por
// costo para lo que se pide (hoy el lienzo; en la fase 2, lo que se ve).
// ============================================================================
#pragma once

#include <gst/gst.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ba::motor {

struct ModoVideo {
    int ancho = 0, alto = 0;
    double fps = 0;          ///< los más altos que da (en el elegido: los que se le piden)
    std::vector<double> cps; ///< los que da (de menor a mayor; con `rango`, los extremos)
    bool rango = false;      ///< da cualquier cps entre cps.front() y cps.back()
    std::string formato;     ///< "crudo", "mjpeg", "h264", "h265"
    std::string pixel;       ///< crudo: NV12, YUY2, BGR… (los que dé, separados por «/»)
    std::string decodificador; ///< comprimido: la fábrica que lo decodifica ("" = no hay: no se puede usar)
    bool gpu = false;        ///< lo decodifica la GPU (o el hardware de video)
    bool convertido = false; ///< crudo que fabrica el sistema decodificando otro (en la CPU, sin decirlo)

    /// «1920x1080@30» (crudo) o «1920x1080@30:mjpeg»: el `camara_modo` guardado.
    std::string valor() const;
    /// En castellano, con lo que cuesta: «1920×1080 · 30 cps · MJPEG (decodifica la CPU: jpegdec)».
    std::string etiqueta() const;
    nlohmann::json json() const;
    bool usable() const { return formato == "crudo" || !decodificador.empty(); }
};

/// Los modos de unas caps de dispositivo (sin repetir, de mayor a menor). Solo memoria
/// del sistema (la de la placa repite los mismos modos).
std::vector<ModoVideo> modosDe(GstCaps* caps);

/// El decodificador de mayor rango para unas caps comprimidas ("image/jpeg",
/// "video/x-h264"…): {fábrica, si es de hardware}. Fábrica vacía si no hay.
std::pair<std::string, bool> decodificadorPara(const std::string& caps);

/// Lo que se le pide al dispositivo.
struct PedidoVideo {
    int ancho = 0, alto = 0;
    double fps = 30;
};
/// El modo que conviene: que dé los cps pedidos, con la proporción pedida, que cubra el
/// tamaño con el menor sobrante (o el más grande si ninguno cubre) y, a igualdad, el más
/// barato (crudo de verdad < comprimido en la GPU < en la CPU < crudo convertido por el
/// sistema < H.264/H.265, que agregan la latencia del GOP). `porque` (si viene) lo explica.
/// Un modo sin tamaño (ancho 0) si no hay ninguno usable.
ModoVideo elegirModo(const std::vector<ModoVideo>& modos, const PedidoVideo& p, std::string* porque = nullptr);

/// Un `camara_modo` guardado («1920x1080@30», «1920x1080@30:mjpeg») contra los modos que
/// hay: el que corresponde (sin formato: el más barato de ese tamaño). Ancho 0 si no está.
ModoVideo modoGuardado(const std::vector<ModoVideo>& modos, const std::string& valor);

/// Lo que va después de la fuente para ese modo: « ! image/jpeg,width=…,framerate=… !
/// jpegparse ! jpegdec» (o las caps crudas). Vacío si el modo no tiene tamaño.
std::string cadenaDeModo(const ModoVideo& m);

} // namespace ba::motor
