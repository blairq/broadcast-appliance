// ============================================================================
// src/app/agent/Actualizacion.hpp — la ultima version publicada de un agente
//
// Los agentes de la pestaña Asistente vienen FIJADOS en agentes.json (version,
// URL y SHA-256 por plataforma). Esto arma una entrada con esa MISMA forma a
// partir de lo que publica la fuente oficial de cada uno, para poder
// actualizar sin esperar una version nueva del sistema:
//
//   claude    el bucket oficial: <raiz>/latest y <raiz>/<v>/manifest.json, que
//             trae el checksum SHA-256 de cada plataforma;
//   opencode  el ultimo release de GitHub: cada archivo trae su digest SHA-256;
//   gemini    el paquete de npm con su integridad SHA-512 (node queda el
//             fijado).
//
// Una plataforma cuya fuente no publica hash no entra: nunca se instala algo
// sin verificar. No instala nada: solo pregunta (bloquea en la red; va en un
// hilo aparte).
// ============================================================================
#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace ba::app::agent {

/// :param runtime: claude | opencode | gemini.
/// :param base: su entrada de agentes.json (de ahi salen la fuente, los
///        nombres de los archivos y el ejecutable de cada plataforma).
/// :param err: que fallo, si devuelve vacio.
/// :returns: la entrada nueva, o `null` si no se pudo.
nlohmann::json ultimaPublicada(const std::string& runtime, const nlohmann::json& base, std::string* err);

} // namespace ba::app::agent
