// ============================================================================
// src/app/arranque/ModoConsola.hpp — Modo de operacion de ba-app sin UI grafica
//
// Proposito:
//   El Proceso A de verdad cuando no hay UI Qt (Linux/macOS hoy, o un
//   servidor sin pantalla): arranca el motor de emision, arma la escena desde
//   la configuracion, publica el retorno hacia B, graba y/o transmite, late en
//   BA.Health para el watchdog del launcher y atiende el canal de control.
//   Corre hasta que el launcher pide el cierre, llega SIGINT/SIGTERM o vence
//   `--duration`.
//
// Configuracion (flexible por diseño):
//   `Config/app.json` en la raiz del bundle; si no existe se escribe uno con
//   valores por defecto para que el operador lo edite. Todo se puede pisar
//   por linea de comandos (ver `runOperate`). La escena es una lista de
//   entradas del motor (tipos de motor/Entrada, con sus ajustes); los tipos de
//   antes se traducen al cargar (inputs::alMotor).
//
//   La clave de transmision NUNCA va en el archivo: se lee de la variable de
//   entorno que indique `stream.keyEnv` (por defecto BA_STREAM_KEY).
//
// Hilos: el bucle de runOperate es el dueño del motor (arranque/RuntimeConfig.hpp); las
// señales (SIGINT/SIGTERM) solo prenden la marca de cierre (AppRuntime::requestStop).
// ============================================================================

#pragma once

#include <filesystem>
#include <string>

namespace ba::app {

struct AppConfig;

/// Ejecuta el modo de operacion.
///
/// Opciones:
///   --config=<archivo>      otro app.json
///   --encoder=<alias|id>    auto | vaapi | nvenc | qsv | amf | videotoolbox | x264 | fábrica de GStreamer
///   --bitrate=<kbps>        bitrate de video
///   --resolution=<WxH>      lienzo y salida
///   --fps=<n>
///   --record / --no-record  grabacion de respaldo
///   --rec-dir=<dir>
///   --stream-server=<url>   habilita la transmision (clave por entorno)
///   --guests=<n>            invitados en escena (0..4)
///   --duration=<seg>        termina solo (pruebas)
///   --status-every=<seg>    cada cuanto imprime el estado (0 = nunca)
///   --safe-level=<1..3>     modo seguro escalonado (F1-10, fault/ModoSeguro.hpp): 1 y 2
///                           apagan piezas y restauran las escenas; 3 = --safe-mode
///   --safe-off=<a,b>        piezas apagadas por la causa de la última caída
///   --safe-mode             x264 y solo las fuentes de invitados (= --safe-level=3)
///   --restore               (lo pasa el launcher tras un crash)
///
/// :returns: 0 en cierre ordenado; !=0 si el motor no pudo arrancar.
int runOperate(int argc, char** argv);

/// El valor de `--clave=valor` en la línea de comandos (`def` si no está).
std::string argValue(int argc, char** argv, const char* key, const std::string& def);
/// Lo que pisa la línea de comandos sobre la configuración leída (las opciones de arriba;
/// también el modo seguro escalonado y el modo prueba). Lo usa AppRuntime::configure.
void applyCli(AppConfig& c, int argc, char** argv, const std::filesystem::path& root);

} // namespace ba::app
