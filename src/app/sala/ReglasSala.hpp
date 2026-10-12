// ============================================================================
// src/app/sala/ReglasSala.hpp — las reglas de la sala que no hablan con nadie
//
// Lo que las operaciones de la sala (ControlSala, ParticipantesSala…) deciden
// sin mandar nada: qué quiere decir un código de error de la API, qué hay que
// decirle al operador después de una prueba técnica y cómo se mezcla un cambio
// de la configuración del proxy. Aparte y sin Qt para poder probarlas solas
// (tests/test_reglas_sala.cpp). La GUI y el MCP leen los MISMOS textos.
// ============================================================================
#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace ba::app::sala {

/// Un código de error de la API de la sala ("full", "not_found"…) en
/// castellano, en minúscula y sin punto final: quien lo muestra le pone lo que
/// va adelante. Lo que no es un código conocido vuelve tal cual (ya es un texto).
std::string explicarError(const std::string& codigo);

/// Lo que hay que decirle al operador según el resultado de la prueba técnica
/// de un invitado (POST /participants/<id>/prueba): eco, red y avisos.
std::string recomendacionPrueba(const nlohmann::json& resultado);

/// Mezcla `cambios` en la configuración del proxy `actual`. Solo las claves que
/// entiende el proxy (activo, dominio, puertoTls, puertoHttp, servicios, acme);
/// `acme` se mezcla campo por campo. Devuelve el error ("" = bien) y deja la
/// configuración nueva en `nueva` solo si está bien (prendido pide dominio).
std::string mezclarProxy(const nlohmann::json& actual, const nlohmann::json& cambios, nlohmann::json& nueva);

} // namespace ba::app::sala
