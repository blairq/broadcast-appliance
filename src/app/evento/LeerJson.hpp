// ============================================================================
// src/app/evento/LeerJson.hpp — leer un campo de un JSON sin tirar excepciones
//
// Un evento.json lo edita gente (y el asistente): un campo con el tipo cambiado no
// puede tirar abajo la carga. Lo que no se entiende queda con su valor por defecto.
//
// Lo usan el guion v1 (Evento.cpp) y la línea v2 (Linea.cpp y lo que sale de ahí):
// antes cada uno tenía su copia en un namespace anónimo, y no leían igual (uno
// aceptaba 2.5 como número y el otro no).
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::evento {

inline std::string leerTexto(const nlohmann::json& j, const char* k, const std::string& def = {}) {
    if (!j.is_object()) return def;
    const auto it = j.find(k);
    return it != j.end() && it->is_string() ? it->get<std::string>() : def;
}

/// Entero o con coma (se trunca); otra cosa, `def`.
inline int64_t leerNumero(const nlohmann::json& j, const char* k, int64_t def = 0) {
    if (!j.is_object()) return def;
    const auto it = j.find(k);
    if (it == j.end()) return def;
    if (it->is_number_integer()) return it->get<int64_t>();
    if (it->is_number_float()) return static_cast<int64_t>(it->get<double>());
    return def;
}

inline bool leerBool(const nlohmann::json& j, const char* k, bool def = false) {
    if (!j.is_object()) return def;
    const auto it = j.find(k);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : def;
}

} // namespace ba::evento
