// ============================================================================
// src/app/placeholder.cpp — PLACEHOLDER de `ba-app` (Proceso A)  [SKELETON]
//
// ⚠️  Este archivo existe SOLO para que el target `ba-app` configure y compile
//     antes de que el agente del Proceso A aporte la lógica real (spec §6:
//     frontend Qt 6 sobre libobs, escenas, encoding, salida RTMPS, UI del
//     operador y modo ingeniería).
//
// El Proceso A es un ejecutable GUI (sin consola en Windows). Aquí solo hay un
// `main` mínimo que termina de inmediato; el agente de A lo reemplazará por el
// arranque de QApplication + obs_startup.
//
// Compila en Windows/MSVC y en Linux; todo lo Windows-only va detrás de
// #ifdef _WIN32. No implementa lógica de negocio.
// ============================================================================

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/// \brief Punto de entrada del Proceso A (placeholder).
///
/// Termina de inmediato con código 0. El agente del Proceso A reemplazará esto
/// por la inicialización de Qt 6 y libobs (spec §6.1, §6.6).
///
/// :param argc: cantidad de argumentos de línea de comandos.
/// :param argv: argumentos de línea de comandos.
/// :returns: código de salida del proceso.
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    return 0;
}
