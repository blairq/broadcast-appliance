// ============================================================================
// src/core/json/Json.cpp — ver Json.hpp
// ============================================================================
#include "json/Json.hpp"

#include <nlohmann/json.hpp>

namespace ba {

nlohmann::json jsonDeTexto(std::string_view texto) { return nlohmann::json::parse(texto); }

std::string textoDeJson(const nlohmann::json& j) { return j.dump(); }

} // namespace ba
