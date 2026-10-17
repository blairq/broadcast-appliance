// src/core/platform/GstEntorno.hpp — GStreamer, solo con lo del bundle
//
// GStreamer busca plugins por todos lados (la carpeta del sistema, la del
// usuario, variables de entorno) y guarda un registro en el perfil. El
// appliance es portable: GStreamer viaja adentro del bundle
// (bin/64bit/GStreamer.framework en macOS; bin/64bit/gstreamer en Windows y
// Linux) y tiene que usar SOLO eso. Esta función fija las variables antes de
// `gst_init`: la ruta de los plugins, el escáner, el registro (en Cache/) y
// los módulos de GIO, y vacía las rutas de afuera.
//
// No enlaza con GStreamer (solo arma el entorno): la usan el módulo de video
// (ba-sources) y la app, cada una antes de su gst_init.
#pragma once

#include <filesystem>
#include <vector>

namespace ba::platform {

/// La raíz de GStreamer del bundle (la que tiene lib/gstreamer-1.0), o vacía si
/// no está. En desarrollo, BA_GST_RAIZ la puede indicar.
std::filesystem::path gstRaiz();

/// Fija el entorno de GStreamer para usar solo lo del bundle. Devuelve false si
/// no hay GStreamer en el bundle (entonces no hay que llamar a gst_init).
bool prepararGst();

// --- El registro de plugins ---------------------------------------------------
// La primera vez en una PC (o con el bundle en otra ruta: el disco cambió de
// letra, la carpeta se movió) gst_init rehace el registro: pasa cada plugin por
// el escáner (otro proceso), que lo carga y lo describe. Un plugin que se traba
// al mirar el hardware de esa PC cuesta 60 s (después GStreamer lo deja afuera).
// El registro se escribe recién al terminar: cortar el arranque a la mitad lo
// pierde todo y la vez siguiente empieza de cero (pasó el 2026-10-14).

/// Si el gst_init que viene va a rehacer el registro: no está o se hizo para
/// otra ruta de plugins.
bool registroPorRehacer();

/// Cuántos plugins va a mirar GStreamer si rehace el registro (los módulos de
/// la ruta de plugins): para contar «37 de 312» mientras revisa.
int contarPlugins();

/// Justo antes de gst_init. Si el registro se va a rehacer, prende los mensajes
/// de GStreamer que dicen qué plugin revisa (GST_PLUGIN_LOADING, nivel DEBUG) y
/// los deja en Logs/gst-plugins-<pid>.log (uno de la app y uno por escáner: ahí
/// queda en qué plugin se trabó). Devuelve registroPorRehacer().
bool empezarRegistro();

/// Mientras dura la revisión (entre empezarRegistro y registroTerminado): los
/// archivos de diagnóstico de los escáneres (uno por escáner: si uno se traba,
/// GStreamer lo mata y arranca otro). Cada uno anota «attempt to load plugin
/// "<archivo>"» al empezar a cargar un plugin: es lo que está revisando. Vacío
/// si el diagnóstico no está prendido.
std::vector<std::filesystem::path> archivosDeEscaneres();

/// Justo después de gst_init. Anota la ruta para la que quedó hecho el registro
/// y apaga lo que prendió empezarRegistro (las variables de entorno; el umbral de
/// GST_PLUGIN_LOADING lo baja el que llama: esto no enlaza con GStreamer).
void registroTerminado(bool ok);

} // namespace ba::platform
