// ============================================================================
// src/core/json/Json.hpp — leer y escribir JSON con funciones propias (no plantillas)
//
// Para el código nuevo: un texto (también un literal) se lee con ba::jsonDeTexto y no con
// `nlohmann::json::parse("…")` ni con `"…"_json` (los vigila tests/test_json_literales.py).
// Están compiladas una vez, en Json.cpp; quien las llama ve solo la declaración.
//
// Funciones libres en `ba` y no un espacio `ba::json`: cientos de archivos tienen
// `using json = nlohmann::json;` en el ámbito global y su código dentro de `ba::algo`;
// con un `ba::json` a la vista, `json` dejaría de nombrar al tipo ahí. Y no `leerJson`: ese
// es ba::persist::leerJson(ruta), que lee un ARCHIVO (con un `using namespace ba::persist`,
// leerJson("x.json") sería ambiguo).
// ============================================================================
#pragma once

#include "json/JsonFwd.hpp"

#include <string>
#include <string_view>

namespace ba {

/// El JSON de ese texto: lo mismo que `nlohmann::json::parse(texto)` (si no es JSON
/// válido, tira nlohmann::json::parse_error).
nlohmann::json jsonDeTexto(std::string_view texto);

/// El texto compacto de ese JSON: lo mismo que `j.dump()`.
std::string textoDeJson(const nlohmann::json& j);

} // namespace ba
