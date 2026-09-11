// ============================================================================
// src/obs-module/obs/obs-module.h — API mínima de módulo de libobs (VENDOR,
// subconjunto fiel de obs-studio 31.0.0)
//
// Propósito:
//   Declara el subconjunto de la API C de libobs que usa `ba-sources` para
//   registrarse como plugin: `obs_module_register` / `obs_module_unregister`
//   (los entrypoints que OBS busca en la DLL) y el tipo `obs_log_t`. Es un
//   header propio del módulo para que el cross-build MinGW compile sin libobs
//   instalada: los símbolos se resuelven en el enlace de carga contra obs.dll.
//
// Modelo de threading:
//   `obs_module_register` se llama una vez desde el thread principal de libobs
//   al cargar el módulo; `obs_module_unregister` al descargarlo.
//
// Estrategia de errores:
//   Ninguna propia. `// VERIFY` marca la firma de `obs_module_register` que hay
//   que contrastar contra los headers reales de obs-studio 31.0.0.
// ============================================================================
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct obs_module;

// Callback de log de libobs: nivel + línea + args.
// VERIFY: typedef obs_log_t en obs-studio 31.0.0 (libobs/obs-module.h).
typedef int (*obs_log_t)(int level, const char *line, va_list args);

// Entrypoints del plugin. OBS los busca por nombre en la DLL.
// VERIFY: firma de obs_module_register en obs-studio 31.0.0
//   (struct obs_module*, obs_log_t, const char *local, const char *module).
void obs_module_register(struct obs_module *module, obs_log_t log_func, const char *local, const char *module_name);
// VERIFY: firma de obs_module_unregister en obs-studio 31.0.0.
void obs_module_unregister(void);
void obs_module_set_enabled(bool enabled);
void obs_module_config_path(const char *local, const char *module, const char *config_file);

#ifdef __cplusplus
}
#endif
