// ============================================================================
// src/app/EngineSelfTest.hpp — Autotest ejecutable del motor de emision
//
// Proposito:
//   Ejercita `BroadcastEngine` de punta a punta contra el motor REAL y reporta
//   cada paso, para que "el motor funciona" sea algo que se comprueba corriendo
//   el binario y no una afirmacion. Se invoca con `ba-app --engine-selftest`.
//
//   Pasos: arranque del motor -> escena -> publicacion del retorno hacia el
//   Proceso B -> disponibilidad de codificadores y salidas -> grabacion breve a
//   disco -> verificacion de que el archivo quedo escrito.
//
// Sin GStreamer en el programa no hay nada que probar: devuelve 77 (el codigo
// de "saltado" de CTest) en vez de 1, y deja dicho por que.
// ============================================================================
#pragma once

namespace ba::app {

/// Corre el autotest del motor de emision.
///
/// :param argc: argumentos recibidos por main (se aceptan overrides).
/// :param argv: idem.
/// :returns: 0 si todo paso, 77 si se salto por falta de GStreamer,
///           1 si algo fallo de verdad.
int runEngineSelfTest(int argc, char** argv);

} // namespace ba::app
