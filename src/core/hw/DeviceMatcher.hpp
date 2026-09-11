// DeviceMatcher.hpp
//
// Proposito:
//   ALGORITMO de matching con puntaje (spec 9.4) para re-identificar un
//   dispositivo observado contra la lista de dispositivos conocidos, de mayor
//   a menor puntaje:
//     1. ExactEndpoint  (100) — mismo endpointId.
//     2. Serial         (80)  — mismo serial no vacio.
//     3. VidPidLocation (60)  — mismo vid+pid y al menos un locationPath en
//                               comun.
//     4. UniqueVidPid   (40)  — mismo vid+pid y ese vid+pid aparece exactamente
//                               una vez entre los conocidos NO asignados.
//     5. AskOperator    (10)  — nada coincide: el caller muestra un banner no
//                               bloqueante.
//   Devuelve el UNICO match de mayor puntaje; ante empate en el puntaje maximo
//   (ambiguo) devuelve AskOperator. Es codigo pura y determinista, testeable en
//   Linux sin Windows.
//
// Modelo de threading:
//   La instancia es stateless (no tiene miembros mutables) y los metodos son
//   const: puede usarse en paralelo desde varios workers, y un mismo objeto
//   puede compartirse. Los argumentos se leen por const-ref; no se muta nada.
//
// Estrategia de errores:
//   No lanza. Devuelve siempre un MatchResult valido: si no hay ningun
//   candidato (known vacio o todos ya asignados) devuelve NoMatch; si hay
//   candidatos pero ninguno coincide devuelve AskOperator (el caller decide
//   mostrar el banner). Los indices fuera de rango en alreadyAssigned se
//   ignoran.

#pragma once

#include <cstdint>
#include <vector>

#include "hw/DeviceIdentity.hpp"

namespace ba::hw {

/// Tipo de match, de mas fuerte a mas debil (spec 9.4).
enum class MatchKind {
    ExactEndpoint,  // mismo endpointId
    Serial,         // mismo serial no vacio
    VidPidLocation, // mismo vid+pid y locationPath en comun
    UniqueVidPid,   // mismo vid+pid unico entre no asignados
    AskOperator,    // nada coincide (o ambiguo): preguntar al operador
    NoMatch,        // no hay candidatos
};

/// Resultado del matching: que tipo de match, contra que indice de `known` y
/// con que puntaje.
struct MatchResult {
    MatchKind kind = MatchKind::NoMatch;
    int index = -1; // indice en `known`, o -1 si no hay
    int score = 0;
};

/// Matcher de identidad de dispositivos con puntaje (spec 9.4). Stateless.
class DeviceMatcher {
   public:
    /// Puntajes fijos, de mayor a menor (spec 9.4). Se exponen para que los
    /// tests y el caller puedan razonar sobre ellos de forma estable.
    static constexpr int kScoreExactEndpoint = 100;
    static constexpr int kScoreSerial = 80;
    static constexpr int kScoreVidPidLocation = 60;
    static constexpr int kScoreUniqueVidPid = 40;
    static constexpr int kScoreAskOperator = 10;

    /// Busca el match de mayor puntaje de `observed` contra `known`,
    /// descartando los indices en `alreadyAssigned`.
    ///
    /// Reglas:
    ///  - Se evalua cada candidato no asignado con el mejor nivel que alcance
    ///    (endpoint > serial > vid+pid+location > vid+pid unico).
    ///  - Gana el puntaje mas alto. Si DOS o mas candidatos empatan en ese
    ///    puntaje maximo, el match es ambiguo y se devuelve AskOperator.
    ///  - Si ningun candidato coincide pero hay candidatos no asignados, se
    ///    devuelve AskOperator (banner no bloqueante).
    ///  - Si no hay candidatos (known vacio o todos asignados), NoMatch.
    ///
    /// :param observed: identidad del dispositivo que se observo al reconectar.
    /// :param known: lista de identidades conocidas (el hardware_map del host).
    /// :param alreadyAssigned: indices de `known` ya asignados a otra pista;
    ///                          se descartan como candidatos.
    /// :returns: MatchResult con kind/index/score del mejor match.
    MatchResult match(const DeviceIdentity& observed, const std::vector<DeviceIdentity>& known,
                      const std::vector<int>& alreadyAssigned) const;
};

} // namespace ba::hw
