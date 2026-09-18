// ============================================================================
// src/app/EngineSelfTest.hpp — Autotest ejecutable del motor de emision
//
// Proposito:
//   Ejercita `BroadcastEngine` de punta a punta contra la libobs REAL y reporta
//   cada paso, para que "el motor funciona" sea algo que se comprueba corriendo
//   el binario y no una afirmacion. Se invoca con `ba-app --engine-selftest`.
//
//   Pasos: arranque de libobs -> escena -> publicacion del retorno hacia el
//   Proceso B -> disponibilidad de codificadores y salidas -> grabacion breve a
//   disco -> verificacion de que el archivo quedo escrito.
//
// Sobre el contexto grafico:
//   `obs_reset_video()` necesita un contexto GL/D3D. En una sesion sin
//   servidor grafico (una consola remota, un runner de CI) no hay forma de
//   obtenerlo y el arranque falla con un motivo claro. Eso NO es un fallo del
//   motor, asi que el autotest lo distingue: devuelve 77 (el codigo de
//   "saltado" de CTest) en vez de 1, y deja dicho por que.
// ============================================================================
#pragma once

namespace ba::app {

/// Corre el autotest del motor de emision.
///
/// :param argc: argumentos recibidos por main (se aceptan overrides).
/// :param argv: idem.
/// :returns: 0 si todo paso, 77 si se salto por falta de contexto grafico,
///           1 si algo fallo de verdad.
int runEngineSelfTest(int argc, char** argv);

} // namespace ba::app
