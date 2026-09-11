// ============================================================================
// src/obs-module/obs/obs-properties.h — API mínima de propiedades/datos de
// libobs (VENDOR, subconjunto fiel de obs-studio 31.0.0)
//
// Propósito:
//   Declara el subconjunto de la API C de libobs que usa `ba-sources`
//   (obs_properties / obs_property / obs_data y sus funciones de getters y
//   setters de lista). Es un header propio del módulo para que el cross-build
//   MinGW compile sin necesidad de tener libobs instalada en la máquina: los
//   símbolos `obs_*` se resuelven en el enlace de carga contra obs.dll
//   (layout §5.1). No es la librería completa de libobs: solo lo que se usa.
//
// Modelo de threading:
//   Igual que en libobs: `obs_properties_create`/`obs_properties_destroy` y
//   los setters de `obs_property` se usan en el thread de control (UI); los
//   getters de `obs_data` (`obs_data_get_*`) se usan al leer `settings` en
//   `update` (thread de control). No hay estado compartido entre threads.
//
// Estrategia de errores:
//   Ninguna propia: las funciones devuelven lo que define libobs (puntero o
//   bool de éxito). Los `// VERIFY` marcan las firmas que hay que contrastar
//   contra los headers reales de obs-studio 31.0.0 en el deploy.
// ============================================================================
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct obs_properties;
struct obs_property;
struct obs_data;
struct obs_data_tree;

// --- Tipos de propiedad (subconjunto del enum real de libobs) ---------------
enum obs_property_type {
    OBS_PROPERTY_INVALID = 0,
    OBS_PROPERTY_BOOL,
    OBS_PROPERTY_DEFAULT, // int
    OBS_PROPERTY_FLOAT,
    OBS_PROPERTY_INT,
    OBS_PROPERTY_COLOR,
    OBS_PROPERTY_EDITABLE_INT,
    OBS_PROPERTY_EDITABLE_FLOAT,
    OBS_PROPERTY_TEXT,
    OBS_PROPERTY_PATH,
    OBS_PROPERTY_LIST,
    OBS_PROPERTY_MODULE,
    OBS_PROPERTY_SCRIPTED,
    OBS_PROPERTY_COUNT
};

// --- Tipos de lista (subconjunto del enum real de libobs) --------------------
enum obs_property_list_type {
    OBS_PROPERTY_LIST_DEFAULT = 0,
    OBS_PROPERTY_LIST_EDITABLE,
    OBS_PROPERTY_LIST_EXCLUSIVE,
    OBS_PROPERTY_LIST_EXCLUSIVE_STR,
    OBS_PROPERTY_LIST_EXCLUSIVE_INT,
    OBS_PROPERTY_LIST_EXCLUSIVE_INTL,
    OBS_PROPERTY_LIST_EDITABLE_STR,
    OBS_PROPERTY_LIST_EDITABLE_INT,
    OBS_PROPERTY_LIST_EDITABLE_INTL
};

// --- Item de lista (nombre interno + nombre para mostrar) --------------------
struct obs_property_list_item {
    const char *name;
    const char *display_name;
};

// --- obs_properties ----------------------------------------------------------
// VERIFY: firma de obs_properties_create en obs-studio 31.0.0.
obs_properties *obs_properties_create(void);
// VERIFY: firma de obs_properties_destroy en obs-studio 31.0.0.
void obs_properties_destroy(obs_properties *props);

obs_property *obs_properties_add_bool(obs_properties *props, const char *name, const char *description);
obs_property *obs_properties_add_int(obs_properties *props, const char *name, const char *description);
obs_property *obs_properties_add_float(obs_properties *props, const char *name, const char *description);
obs_property *obs_properties_add_double(obs_properties *props, const char *name, const char *description);
obs_property *obs_properties_add_color(obs_properties *props, const char *name, const char *description);
obs_property *obs_properties_add_text(obs_properties *props, const char *name, const char *description, uint32_t type);
obs_property *obs_properties_add_path(obs_properties *props, const char *name, const char *description, uint32_t type, const char *filter, const char *last_path);
// VERIFY: firma de obs_properties_add_list en obs-studio 31.0.0.
obs_property *obs_properties_add_list(obs_properties *props, const char *name, const char *description, enum obs_property_type type, const struct obs_property_list_item *items, size_t num_items, uint32_t flags);
obs_property *obs_properties_add_list_string(obs_properties *props, const char *name, const char *description, enum obs_property_list_type type, const char * const *items, size_t num_items, uint32_t flags);
obs_property *obs_properties_add_list_int(obs_properties *props, const char *name, const char *description, enum obs_property_list_type type, const struct obs_property_list_item *items, size_t num_items, uint32_t flags);

// --- obs_property (setters que se usan) --------------------------------------
void obs_property_set_visible(obs_property *property, bool visible);
void obs_property_set_enabled(obs_property *property, bool enabled);
void obs_property_set_description(obs_property *property, const char *description);
void obs_property_set_display_name(obs_property *property, const char *display_name);
// VERIFY: firma de obs_property_double_set_range en obs-studio 31.0.0.
void obs_property_double_set_range(obs_property *property, double min, double max, double step);
void obs_property_int_set_range(obs_property *property, long long min, long long max, long long step);
void obs_property_float_set_range(obs_property *property, double min, double max, double step);
void obs_property_int_set_format(obs_property *property, const char *format);
void obs_property_float_set_format(obs_property *property, const char *format);
void obs_property_double_set_format(obs_property *property, const char *format);
void obs_property_text_set_info(obs_property *property, const char *info, uint32_t type);
void obs_property_list_add_item(obs_property *property, const char *name, const char *display_name, uint64_t flag);
void obs_property_list_insert_item(obs_property *property, size_t index, const char *name, const char *display_name, uint64_t flag);
void obs_property_list_remove_item(obs_property *property, size_t index);
void obs_property_list_clear_items(obs_property *property);

// --- obs_data (getters/setters que se usan) ----------------------------------
// VERIFY: firma de obs_data_get_string en obs-studio 31.0.0.
const char *obs_data_get_string(const obs_data *settings, const char *name);
double obs_data_get_double(const obs_data *settings, const char *name);
long long obs_data_get_int(const obs_data *settings, const char *name);
bool obs_data_get_bool(const obs_data *settings, const char *name);
uint64_t obs_data_get_uint(const obs_data *settings, const char *name);
void obs_data_set_bool(obs_data *settings, const char *name, bool val);
void obs_data_set_double(obs_data *settings, const char *name, double val);
void obs_data_set_int(obs_data *settings, const char *name, long long val);
void obs_data_set_string(obs_data *settings, const char *name, const char *val);
void obs_data_set_uint(obs_data *settings, const char *name, uint64_t val);

#ifdef __cplusplus
}
#endif
