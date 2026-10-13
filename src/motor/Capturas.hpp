// ============================================================================
// src/motor/Capturas.hpp — capturar una pantalla o la ventana de una aplicación
//
// PANTALLA: la elige la app (de la lista de Qt) y manda en los ajustes su
// nombre y su recuadro; acá se arma el origen de cada sistema:
//   Windows  d3d12screencapturesrc (o d3d11) con el HMONITOR de ese nombre
//            («\\.\DISPLAY2», el de Qt): el índice de DXGI no sigue el orden
//            de Qt.
//   Mac      avfvideosrc capture-screen con el índice (CGGetActiveDisplayList)
//            del monitor de ese recuadro.
//   Linux    ximagesrc con el recuadro (X11: todos los monitores son una sola
//            pantalla); en Wayland, pipewiresrc (elige el portal).
//
// VENTANA de una aplicación: se guarda la aplicación (el ejecutable) y el
// título, no el identificador de la ventana (cambia cada vez que se abre).
// Cada vez que se arma la captura se busca la ventana viva: la del mismo
// título, o si no la primera de esa aplicación. Si no está, la entrada espera
// a que se abra (Entrada lo vuelve a intentar cada 2 s).
//   Windows  d3d12screencapturesrc window-handle (Windows Graphics Capture)
//   Mac      bacapturaventana (ScreenCaptureKit, bacaptura)
//   Linux    ximagesrc xid (X11)
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ba::motor {

struct VentanaCapturable {
    std::uint64_t handle = 0; ///< HWND, CGWindowID o la ventana de X11 (vale mientras esté abierta)
    std::string app;          ///< el ejecutable o el nombre de la aplicación
    std::string titulo;
    int pid = 0;
    int ancho = 0, alto = 0;
};

/// Las ventanas que se pueden capturar: visibles, con título, de otras aplicaciones.
std::vector<VentanaCapturable> ventanasCapturables();

/// La ventana viva de `app` con `titulo`: el mismo título y la misma aplicación; si no,
/// la primera de esa aplicación; si no, una de otra aplicación con ese título.
bool buscarVentanaCapturable(const std::string& app, const std::string& titulo, VentanaCapturable& out);

/// ¿Este sistema puede capturar una ventana sola? (y por qué no, si no).
bool hayCapturaDeVentana(std::string* porque = nullptr);

/// El origen de GStreamer para capturar una pantalla. Ajustes: monitor (nombre de
/// Qt), x, y, ancho, alto (en puntos), px: [x, y, ancho, alto] en píxeles, indice
/// (lo viejo), cursor.
std::string origenPantalla(const nlohmann::json& a);

/// El origen para capturar la ventana `app` / `titulo` (vacío si no está abierta).
std::string origenVentana(const std::string& app, const std::string& titulo, bool cursor);

#if defined(__APPLE__)
/// macOS: el device-index de avfvideosrc para la cámara de ese uniqueID de AVFoundation
/// (el «avf.unique_id» del dispositivo de GStreamer); -1 si no está. Se cuenta como
/// avfvideosrc, en el momento: el índice que trae el dispositivo de GStreamer
/// (avfdeviceprovider) a veces sale en otro orden y abría OTRA cámara.
int indiceCamaraMac(const std::string& uniqueId);
#endif

} // namespace ba::motor
