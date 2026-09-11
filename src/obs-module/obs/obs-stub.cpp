// ============================================================================
// src/obs-module/obs/obs-stub.cpp — definiciones stub de la API de libobs
//
// Propósito:
//   El cross-build MinGW compila `ba-sources` sin libobs instalada: los headers
//   mínimos de obs/ solo *declaran* los símbolos `obs_*` y el linker necesita
//   *definiciones*. Este archivo aporta esas definiciones no-op para que la DLL
//   enlance. En el deploy real (Windows con libobs y el feature-flag
//   `BA_SOURCES_HAVE_OBS` definido) este archivo se excluye y los símbolos se
//   resuelven contra obs.dll.
//
// Modelo de threading:
//   Sin estado: cada stub es un no-op idempotente que devuelve un valor
//   neutro (nullptr / 0 / false). No corren en ningún thread concreto en el
//   cross-build porque la DLL no se carga.
//
// Estrategia de errores:
//   Ninguna: son placeholders de enlace, no lógica. Solo se compilan cuando
//   `BA_SOURCES_HAVE_OBS` NO está definido (path real excluido).
// ============================================================================

#ifndef BA_SOURCES_HAVE_OBS

#include "obs/obs-module.h"
#include "obs/obs.h"
#include "obs/obs-properties.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- obs_properties / obs_property ------------------------------------------

/// Crea una superficie de propiedades (stub de enlace).
/// :returns: nullptr (no hay libobs en el cross-build).
obs_properties *obs_properties_create(void) {
    return nullptr;
}

/// Añade una propiedad bool (stub de enlace).
/// :returns: nullptr (no hay libobs en el cross-build).
obs_property *obs_properties_add_bool(obs_properties *props, const char *name,
                                      const char *description) {
    (void)props;
    (void)name;
    (void)description;
    return nullptr;
}

/// Añade una propiedad double (stub de enlace).
/// :returns: nullptr (no hay libobs en el cross-build).
obs_property *obs_properties_add_double(obs_properties *props, const char *name,
                                        const char *description) {
    (void)props;
    (void)name;
    (void)description;
    return nullptr;
}

/// Añade una propiedad de lista (stub de enlace).
/// :returns: nullptr (no hay libobs en el cross-build).
obs_property *obs_properties_add_list(obs_properties *props, const char *name,
                                      const char *description,
                                      enum obs_property_type type,
                                      const struct obs_property_list_item *items,
                                      size_t num_items, uint32_t flags) {
    (void)props;
    (void)name;
    (void)description;
    (void)type;
    (void)items;
    (void)num_items;
    (void)flags;
    return nullptr;
}

/// Fija el rango de una propiedad double (stub de enlace).
void obs_property_double_set_range(obs_property *property, double min, double max,
                                   double step) {
    (void)property;
    (void)min;
    (void)max;
    (void)step;
}

// --- obs_data ---------------------------------------------------------------

/// Lee una cadena de `obs_data` (stub de enlace).
/// :returns: nullptr (no hay libobs en el cross-build).
const char *obs_data_get_string(const obs_data *settings, const char *name) {
    (void)settings;
    (void)name;
    return nullptr;
}

/// Lee un double de `obs_data` (stub de enlace).
/// :returns: 0.0 (no hay libobs en el cross-build).
double obs_data_get_double(const obs_data *settings, const char *name) {
    (void)settings;
    (void)name;
    return 0.0;
}

/// Lee un bool de `obs_data` (stub de enlace).
/// :returns: false (no hay libobs en el cross-build).
bool obs_data_get_bool(const obs_data *settings, const char *name) {
    (void)settings;
    (void)name;
    return false;
}

// --- Fuentes ----------------------------------------------------------------

/// Registra una fuente/filtro (stub de enlace).
void obs_register_source(const struct obs_source_info *info) {
    (void)info;
}

/// Recupera el puntero privado de una fuente (stub de enlace).
/// :returns: nullptr (no hay libobs en el cross-build).
void *obs_source_get_private_data(struct obs_source *source) {
    (void)source;
    return nullptr;
}

#ifdef __cplusplus
}
#endif

#endif // BA_SOURCES_HAVE_OBS
