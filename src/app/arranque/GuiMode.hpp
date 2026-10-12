// ============================================================================
// src/app/arranque/GuiMode.hpp — ba-app con interfaz grafica (Qt vendoreado)
// ============================================================================

#pragma once

namespace ba::app {

/// Arranca AppRuntime y la ventana del operador; vuelve al cerrarla.
///
/// Acepta las mismas opciones que el modo consola (ver OperateMode.hpp).
/// :returns: 0 en cierre ordenado; el codigo de AppRuntime::start si no
///           arranca.
int runGui(int argc, char** argv);

} // namespace ba::app
