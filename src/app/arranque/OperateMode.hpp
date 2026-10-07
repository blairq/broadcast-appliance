// ============================================================================
// src/app/arranque/OperateMode.hpp — Modo de operacion de ba-app sin UI grafica
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
//   fuentes de libobs con sus settings tal cual los usa OBS, asi que cualquier
//   fuente que traiga el bundle (camara v4l2, captura, imagen, texto, media,
//   navegador...) se agrega sin recompilar.
//
//   La clave de transmision NUNCA va en el archivo: se lee de la variable de
//   entorno que indique `stream.keyEnv` (por defecto BA_STREAM_KEY).
// ============================================================================

#pragma once

namespace ba::app {

/// Ejecuta el modo de operacion.
///
/// Opciones:
///   --config=<archivo>      otro app.json
///   --encoder=<alias|id>    auto | vaapi | nvenc | qsv | amf | videotoolbox | x264 | id de libobs
///   --bitrate=<kbps>        bitrate de video
///   --resolution=<WxH>      lienzo y salida
///   --fps=<n>
///   --record / --no-record  grabacion de respaldo
///   --rec-dir=<dir>
///   --stream-server=<url>   habilita la transmision (clave por entorno)
///   --guests=<n>            invitados en escena (0..4)
///   --duration=<seg>        termina solo (pruebas)
///   --status-every=<seg>    cada cuanto imprime el estado (0 = nunca)
///   --safe-mode             x264 y solo las fuentes de invitados
///   --restore               (lo pasa el launcher tras un crash)
///
/// :returns: 0 en cierre ordenado; !=0 si el motor no pudo arrancar.
int runOperate(int argc, char** argv);

} // namespace ba::app
