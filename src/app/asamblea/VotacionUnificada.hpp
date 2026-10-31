// ============================================================================
// src/app/asamblea/VotacionUnificada.hpp — la votación en curso, resuelta (regla pura)
//
// Hay dos formas de votar a la vez (asamblea/Votaciones.hpp): la central de conferencia
// y los celulares. Quien solo la DIBUJA (las consolas de presidencia) recibe esto ya
// resuelto, con las reglas de la asamblea:
//
//   - manda la central si está llevando una (su estado no es «sin_preparar»);
//   - los totales cuentan UN voto por integrante, vale el último (como en el acta):
//     con la nominal de la central se cruza con los celulares (`combinar`, que es
//     SesionRemota::resultadoCombinado); sin ella no hay con qué cruzar y se suman
//     (y `origen` lo dice);
//   - todas las opciones van en los totales, aunque estén en cero.
// ============================================================================
#pragma once

#include <functional>

#include <nlohmann/json.hpp>

namespace ba::app::asamblea {

/// `central`: la votación de la central ({estado, tema, menu, totales, nominal?}; {} sin
/// central). `celulares`: SesionRemota::votacion(). `opcionesDeMenu`: las opciones de un
/// menú de la central. Devuelve {estado, tema, opciones, totales, hasta_ms, origen}.
nlohmann::json votacionUnificada(const nlohmann::json& central, const nlohmann::json& celulares,
                                 const std::function<nlohmann::json(const nlohmann::json& nominal)>& combinar,
                                 const std::function<nlohmann::json(const std::string& menu)>& opcionesDeMenu);

} // namespace ba::app::asamblea
