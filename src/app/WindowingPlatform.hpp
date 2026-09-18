// ============================================================================
// src/app/WindowingPlatform.hpp — Plataforma de ventanas de libobs (Linux)
//
// Proposito:
//   En Linux libobs necesita saber, ANTES de obs_startup, si corre sobre
//   Wayland o X11 y recibir la conexion al display. Por defecto asume X11 y
//   abre su propio display: en una sesion Wayland funcionaba de rebote via
//   XWayland, y sin sesion grafica obs_startup fallaba sin explicacion.
//
//   Vive en su propio archivo porque <X11/Xlib.h> define macros (`Status`,
//   `None`, `Bool`...) que chocan con el resto del codigo.
//
// Eleccion:
//   - BA_OBS_PLATFORM=x11|wayland la fuerza.
//   - Si no: Wayland nativo si hay WAYLAND_DISPLAY; si no, X11 con DISPLAY.
//   - Sin ninguna: error "sin sesion grafica" (no hay modo sin pantalla en
//     libobs: el pipeline de video necesita GL).
//   En Windows y macOS no hay nada que elegir: devuelve true sin hacer nada.
// ============================================================================

#pragma once

#include <string>

namespace ba::app {

/// Elige y fija la plataforma de ventanas de libobs.
///
/// :param display: recibe la conexion abierta (wl_display* o Display*), que
///                 hay que liberar con `releaseWindowingPlatform` DESPUES de
///                 obs_shutdown.
/// :param wayland: recibe true si se eligio Wayland.
/// :param err: motivo si no hay plataforma utilizable.
/// :returns: true si libobs puede arrancar.
bool selectWindowingPlatform(void*& display, bool& wayland, std::string& err);

/// Cierra la conexion abierta por `selectWindowingPlatform`. Acepta nullptr.
void releaseWindowingPlatform(void* display, bool wayland);

} // namespace ba::app
