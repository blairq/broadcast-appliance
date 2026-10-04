// ============================================================================
// src/obs-module/ba_module.cpp — Punto de entrada del modulo `ba-sources`
//
// Proposito:
//   El ABI que libobs espera de un modulo. `OBS_DECLARE_MODULE()` genera los
//   simbolos de infraestructura y `obs_module_load()` es el que el host busca
//   por nombre al cargar la biblioteca.
//
//   Existe porque el modulo anterior exportaba `obs_module_register`, una
//   funcion que NO forma parte de libobs. El host nunca la busca, asi que el
//   modulo no se cargaba en ninguna plataforma: las fuentes y el filtro que
//   registraba no llegaban a existir por mas correctos que fueran.
//
// Modelo de threading:
//   `obs_module_load` corre en el hilo que carga los modulos, antes de que haya
//   ninguna fuente viva. Solo registra descriptores; no arranca nada.
//
// Estrategia de errores:
//   Devuelve true siempre que el registro se complete. Un registro fallido lo
//   reporta libobs por su cuenta en el log.
// ============================================================================

#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("ba-sources", "en-US")

extern "C" {

/// Definida en ba_ipc_sources.cpp: registra `ba_guest_audio` y `ba_guest_video`.
void ba_register_ipc_sources(void);

/// Definida en ba_dsp_sources.cpp: registra `ba_channel_strip` (filtro) y
/// `ba_channel_preset` (fuente de referencia audible).
void ba_register_dsp_sources(void);

/// Definida en ba_mejora_imagen.cpp: registra los filtros de video
/// `ba_mejora_imagen` y `ba_ptz_digital`.
void ba_register_video_filters(void);

/// Definida en ba_gst_fuente.cpp: `ba_gst_fuente` (un pipeline de GStreamer
/// como entrada; sin GStreamer no registra nada).
void ba_register_gst_fuente(void);

/// Definida en ba_subtitulos.cpp: la fuente `ba_subtitulos` (subtítulos de una
/// salida de texto de la app: transcripción o traducción).
void ba_register_subtitulos(void);

/// Punto de entrada del modulo. Lo invoca libobs al cargar la biblioteca.
///
/// :returns: true si el modulo quedo registrado.
MODULE_EXPORT bool obs_module_load(void) {
    ba_register_ipc_sources();
    ba_register_dsp_sources();
    ba_register_video_filters();
    ba_register_gst_fuente();
    ba_register_subtitulos();
    blog(LOG_INFO, "[ba-sources] modulo cargado: fuentes IPC de invitado + DSP de canal + Mejorar imagen + "
                   "PTZ digital + grafo de video");
    return true;
}

/// Descarga del modulo: libobs limpia los registros por su cuenta.
MODULE_EXPORT void obs_module_unload(void) {}

/// Descripcion que muestra el host en la lista de modulos.
MODULE_EXPORT const char* obs_module_description(void) {
    return "Broadcast Appliance — fuentes de invitado remoto sobre IPC";
}

} // extern "C"
