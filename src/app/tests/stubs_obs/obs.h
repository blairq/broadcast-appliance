// obs.h de mentira para las pruebas que no necesitan libobs (FlujosTexto: el
// rótulo de texto es lo único que toca OBS, y en las pruebas no hay motor).
#pragma once
struct obs_source;
typedef struct obs_source obs_source_t;
struct obs_data;
typedef struct obs_data obs_data_t;
inline bool obs_initialized() { return false; }
inline obs_source_t* obs_get_source_by_name(const char*) { return nullptr; }
inline obs_data_t* obs_data_create() { return nullptr; }
inline void obs_data_set_string(obs_data_t*, const char*, const char*) {}
inline void obs_source_update(obs_source_t*, obs_data_t*) {}
inline void obs_data_release(obs_data_t*) {}
inline void obs_source_release(obs_source_t*) {}
