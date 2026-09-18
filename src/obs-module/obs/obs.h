// ============================================================================
// src/obs-module/obs/obs.h — API mínima de fuentes/filtros de libobs (VENDOR,
// subconjunto fiel de obs-studio 31.0.0)
//
// Propósito:
//   Declara el subconjunto de la API C de libobs que usa `ba-sources`:
//   `obs_source_info` (con los callbacks de filtro y de fuente async),
//   `audio_mix` (entrada de un filtro de audio), `frame_data` (salida de una
//   fuente async de audio), los enums de formato de audio y `obs_register_source`
//   / `obs_source_get_private_data`. Es un header propio del módulo para que el
//   cross-build MinGW compile sin libobs instalada: los símbolos `obs_*` se
//   resuelven en el enlace de carga contra obs.dll (layout §5.1).
//
// Modelo de threading:
//   Igual que en libobs: `filter_audio` y `audio_render` corren en el thread de
//   audio (RT-safe: sin asignación/locks/I/O); `create`/`destroy`/`update`/
//   `get_properties` corren en el thread de control. `obs_source_get_private_data`
//   devuelve el puntero que devolvió `create`.
//
// Estrategia de errores:
//   Ninguna propia. Los `// VERIFY` marcan las firmas/estructuras que hay que
//   contrastar contra los headers reales de obs-studio 31.0.0 en el deploy.
// ============================================================================
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "obs-properties.h"

#ifdef __cplusplus
extern "C" {
#endif

struct obs_source;
struct obs_module;
// Forward declaration para el typedef obs_filter_video_t (el struct completo
// vive en la API de video de libobs; ba-sources no registra video, pero el
// callback de filtro de video lo referencia).
struct video_mix;

// --- Formato de audio (subconjunto del enum real de libobs) ------------------
enum audio_format {
    AUDIO_FORMAT_UNKNOWN = 0,
    AUDIO_FORMAT_U8,
    AUDIO_FORMAT_S16,
    AUDIO_FORMAT_S32,
    AUDIO_FORMAT_F32,
    AUDIO_FORMAT_COUNT,
    AUDIO_FORMAT_DEFAULT = AUDIO_FORMAT_F32
};

// --- Formato de frame de video (subconjunto del enum real de libobs) --------
// Solo se necesita para el typedef obs_source_video_render_t; ba-sources no
// registra video, pero el callback de filtro de video lo referencia.
enum obs_frame_format {
    OBS_FORMAT_UNKNOWN = 0,
    OBS_FORMAT_RGB,
    OBS_FORMAT_BGR,
    OBS_FORMAT_YVYU,
    OBS_FORMAT_UYVY,
    OBS_FORMAT_NV12,
    OBS_FORMAT_I420,
    OBS_FORMAT_IYUV,
    OBS_FORMAT_COUNT
};

// --- Mezcla de audio: entrada de un filtro de audio --------------------------
// VERIFY: estructura audio_mix en obs-studio 31.0.0 (libobs/obs.h).
struct audio_mix {
    uint32_t planes;
    uint32_t format;
    uint32_t channels;
    uint32_t frame_count;
    uint32_t sample_rate;
    uint64_t timestamp;
    void **plane_ptr;
};

// --- Frame de audio: salida de una fuente async de audio ---------------------
// VERIFY: estructura frame_data en obs-studio 31.0.0 (libobs/obs.h).
struct frame_data {
    uint32_t channels;
    uint32_t format;
    uint32_t frames;
    uint32_t samples_per_sec;
    uint64_t timestamp;
    void *data[2];
};

// --- Callbacks de fuente/filtro (subconjunto de los typedefs reales) --------
typedef void (*obs_source_tick_t)(struct obs_source *source);
typedef void *(*obs_source_create_t)(struct obs_data *settings, struct obs_source *source);
typedef void (*obs_source_destroy_t)(struct obs_source *source);
typedef void (*obs_source_show_t)(struct obs_source *source);
typedef void (*obs_source_hide_t)(struct obs_source *source);
typedef void (*obs_source_rename_t)(struct obs_source *source, const char *new_name);
typedef void (*obs_source_update_t)(struct obs_source *source, obs_data *settings);
typedef int (*obs_source_video_render_t)(struct obs_source *source, enum obs_frame_format format, void *dst, uint32_t linesize, uint64_t ts);
typedef uint64_t (*obs_source_audio_render_t)(struct obs_source *source, struct frame_data *frame, uint64_t ts);
typedef void (*obs_source_get_bounds_t)(struct obs_source *source, uint32_t *bounds_x, uint32_t *bounds_y, uint32_t *bounds_w, uint32_t *bounds_h);
typedef void (*obs_source_get_size_t)(struct obs_source *source, uint32_t *cx, uint32_t *cy);
typedef const char *(*obs_source_get_name_t)(struct obs_source *source);
typedef void (*obs_source_get_settings_t)(struct obs_source *source, obs_data *out_settings);
typedef void (*obs_source_defaults_t)(obs_data *out_settings);
typedef void (*obs_source_update_settings_t)(struct obs_source *source, obs_data *new_settings);
typedef obs_properties *(*obs_source_get_properties_t)(struct obs_source *source);
typedef void (*obs_source_get_duration_t)(struct obs_source *source, uint64_t *duration);
typedef void (*obs_source_play_t)(struct obs_source *source);
typedef void (*obs_source_pause_t)(struct obs_source *source);
typedef void (*obs_source_stop_t)(struct obs_source *source);
typedef void (*obs_source_preupdate_t)(struct obs_source *source);
typedef void (*obs_source_postupdate_t)(struct obs_source *source);

// --- Callbacks de filtro (un filtro es una fuente con estos callbacks) -------
// VERIFY: typedefs obs_filter_audio_t / obs_filter_video_t en obs-studio 31.0.0.
typedef uint64_t (*obs_filter_audio_t)(struct obs_source *filter, const struct audio_mix *in, uint64_t timestamp);
typedef int (*obs_filter_video_t)(struct obs_source *filter, const struct video_mix *in, uint64_t timestamp);

// --- Info de fuente/filtro (subconjunto de los campos reales, en orden) -----
// VERIFY: orden y nombres de los campos de obs_source_info en obs-studio 31.0.0.
// El struct real tiene más campos (async, transiciones, hotkeys); aquí se
// declara el subconjunto que usa `ba-sources`. Los initializers usan
// designadores (C++20), por lo que el orden de los campos omitidos no importa.
struct obs_source_info {
    const char *id;
    const char *description;
    const char *unversioned_id;

    obs_source_tick_t tick;
    obs_source_create_t create;
    obs_source_destroy_t destroy;
    obs_source_show_t show;
    obs_source_hide_t hide;
    obs_source_rename_t rename;
    obs_source_update_t update;

    obs_source_video_render_t video_render;
    obs_source_audio_render_t audio_render;

    obs_source_get_bounds_t get_bounds;
    obs_source_get_size_t get_size;
    obs_source_get_name_t get_name;
    obs_source_get_settings_t get_settings;
    obs_source_defaults_t defaults;
    obs_source_update_settings_t update_settings;
    obs_source_get_properties_t get_properties;
    obs_source_get_duration_t get_duration;
    obs_source_play_t play;
    obs_source_pause_t pause;
    obs_source_stop_t stop;
    obs_source_preupdate_t preupdate;
    obs_source_postupdate_t postupdate;

    // Callbacks de filtro (NULL si no es filtro).
    obs_filter_audio_t filter_audio;
    obs_filter_video_t filter_video;
};

// --- API pública de fuentes --------------------------------------------------
// VERIFY: firma de obs_register_source en obs-studio 31.0.0.
void obs_register_source(const struct obs_source_info *info);
// VERIFY: firma de obs_source_get_private_data en obs-studio 31.0.0.
void *obs_source_get_private_data(struct obs_source *source);
const char *obs_source_get_id(struct obs_source *source);
const char *obs_source_get_name(struct obs_source *source);

#ifdef __cplusplus
}
#endif
