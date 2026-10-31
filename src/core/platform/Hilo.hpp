// ============================================================================
// src/core/platform/Hilo.hpp — el nombre del hilo que llama, en los tres sistemas
//
// Un hilo sin nombre en `sample`, en Instruments, en un volcado o en Process
// Explorer es un número: no se sabe a quién atribuirle la CPU ni la caída. Todo
// hilo propio (std::thread, std::async, QThread) se nombra al empezar con esto.
// Los de GStreamer los nombra el motor (`<pipeline>/<elemento>`, Prioridad.cpp).
//
//   - macOS: pthread_setname_np (solo el hilo propio; hasta 63 bytes).
//   - Linux: pthread_setname_np; el kernel guarda 15 bytes: se corta ahí (sin
//     partir un carácter UTF-8).
//   - Windows: SetThreadDescription (Windows 10 1607 o más nuevo; se busca en
//     kernel32 al llamar, así un sistema más viejo sigue andando sin nombres).
//
// Nombres cortos y que digan qué hace: «motor», «salidas», «mcp-http», «tv/1».
// ============================================================================
#pragma once

#include <string>
#include <string_view>

namespace ba::platform {

/// Le pone `nombre` (UTF-8) al hilo que llama. Si el sistema no deja, no pasa nada.
void nombrarHilo(std::string_view nombre);

/// El nombre del hilo que llama ("" si no tiene o el sistema no lo dice).
std::string nombreDelHilo();

} // namespace ba::platform
