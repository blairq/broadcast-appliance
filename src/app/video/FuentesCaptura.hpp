// ============================================================================
// src/app/video/FuentesCaptura.hpp — las pantallas y las aplicaciones que se pueden capturar
//
// Las fuentes «Captura de pantalla» (screen_capture) y «Captura de una
// aplicación» (window_capture): sus propiedades (la lista para elegir) y lo
// que se le manda al motor (ver src/motor/Capturas.hpp).
//
//   screen_capture   ajustes: monitor (el nombre del monitor de Qt; vacío = el
//                    principal), show_cursor
//   window_capture   ajustes: ventana («aplicación\ntítulo»), show_cursor. Se
//                    guarda la aplicación y el título: la ventana se vuelve a
//                    buscar cada vez (si cambia el título, la de esa aplicación).
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ba::app::video {

constexpr const char* kTipoPantalla = "screen_capture";
constexpr const char* kTipoVentana = "window_capture";

struct Pantalla {
    std::string nombre;   ///< el de Qt («\\.\DISPLAY2» en Windows): lo que se guarda
    std::string etiqueta; ///< «Pantalla 2: DELL U2720Q — 2560×1440 (principal)»
    int x = 0, y = 0, ancho = 0, alto = 0; ///< en puntos (coordenadas de Qt)
    int pxX = 0, pxY = 0, pxAncho = 0, pxAlto = 0; ///< en píxeles
    bool principal = false;
};

/// Los monitores conectados (vacío sin interfaz gráfica).
std::vector<Pantalla> pantallasDelEquipo();

/// Los ajustes del motor para una captura de pantalla (tipo «pantalla»).
nlohmann::json ajustesPantalla(const nlohmann::json& ajustes);
/// Los ajustes del motor para una captura de aplicación (tipo «ventana»).
nlohmann::json ajustesVentana(const nlohmann::json& ajustes);

/// Para el MCP y la lista de Propiedades: [{valor, etiqueta, app, titulo, ancho, alto}].
nlohmann::json ventanasParaElegir();

/// Registra las propiedades de los dos tipos (listas para elegir).
void registrarTiposCaptura();

} // namespace ba::app::video
