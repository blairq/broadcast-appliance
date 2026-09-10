// DeviceMatcher.cpp
//
// Implementacion del matching con puntaje (spec 9.4). Ver DeviceMatcher.hpp
// para el contrato. Codigo pura y determinista: sin estado, sin IO, sin
// dependencias de plataforma, por lo que corre igual en Windows y Linux.

#include "hw/DeviceMatcher.hpp"

#include <algorithm>
#include <unordered_set>

namespace ba::hw {

namespace {

/// Devuelve true si `idx` esta en `alreadyAssigned`. Los indices fuera de rango
/// de `known` se ignoran (no son candidatos de todos modos).
bool isAssigned(int idx, const std::vector<int>& alreadyAssigned) {
    return std::find(alreadyAssigned.begin(), alreadyAssigned.end(), idx) !=
           alreadyAssigned.end();
}

/// Devuelve true si `observed` y `k` comparten al menos un locationPath.
bool sharesLocationPath(const DeviceIdentity& observed,
                        const DeviceIdentity& k) {
    for (const auto& p : observed.locationPaths) {
        if (std::find(k.locationPaths.begin(), k.locationPaths.end(), p) !=
            k.locationPaths.end()) {
            return true;
        }
    }
    return false;
}

/// Nivel (puntaje) que alcanza `observed` contra UN candidato `k`, o 0 si no
/// coincide en ningun nivel. Los niveles se evaluan de mas fuerte a mas debil
/// y se devuelve el primero que aplique (spec 9.4, de mayor a menor).
int candidateScore(const DeviceIdentity& observed, const DeviceIdentity& k) {
    // 1. ExactEndpoint: mismo endpointId (y no vacio, para no emparejar dos
    //    vacios por defecto).
    if (!observed.endpointId.empty() && observed.endpointId == k.endpointId) {
        return DeviceMatcher::kScoreExactEndpoint;
    }
    // 2. Serial: ambos con serial no vacio e igual.
    if (!observed.serial.empty() && observed.serial == k.serial) {
        return DeviceMatcher::kScoreSerial;
    }
    // vid+pid solo tiene sentido si observed reporta un vid o pid real; un
    // 0/0 (dispositivo no-USB) no debe emparejar por vid+pid.
    const bool hasVidPid = (observed.vid != 0 || observed.pid != 0);
    const bool sameVidPid =
        hasVidPid && observed.vid == k.vid && observed.pid == k.pid;
    if (sameVidPid) {
        // 3. VidPidLocation: mismo vid+pid y al menos un locationPath en comun.
        if (sharesLocationPath(observed, k)) {
            return DeviceMatcher::kScoreVidPidLocation;
        }
        // 4. UniqueVidPid: se resuelve a nivel de coleccion (ver match), porque
        //    depende de cuantas veces aparece ese vid+pid entre los no asignados.
        //    Se marca con un valor especial que match() resuelve.
        return DeviceMatcher::kScoreUniqueVidPid;
    }
    return 0;
}

} // namespace

MatchResult DeviceMatcher::match(const DeviceIdentity& observed,
                                 const std::vector<DeviceIdentity>& known,
                                 const std::vector<int>& alreadyAssigned) const {
    // Cuantas veces aparece el vid+pid de observed entre los conocidos NO
    // asignados: es lo que decide si UniqueVidPid aplica (aparece 1 vez).
    const bool observedHasVidPid = (observed.vid != 0 || observed.pid != 0);
    int unassignedSameVidPid = 0;
    bool hasAnyUnassigned = false;
    for (size_t i = 0; i < known.size(); ++i) {
        if (isAssigned(static_cast<int>(i), alreadyAssigned)) {
            continue;
        }
        hasAnyUnassigned = true;
        if (observedHasVidPid && observed.vid == known[i].vid &&
            observed.pid == known[i].pid) {
            ++unassignedSameVidPid;
        }
    }

    // Sin candidatos no asignados: NoMatch (nada contra que emparejar).
    if (!hasAnyUnassigned) {
        return MatchResult{MatchKind::NoMatch, -1, 0};
    }

    // Pasa 1: puntaje de cada candidato no asignado.
    int bestScore = 0;
    int bestIndex = -1;
    int tiesAtBest = 0;
    MatchKind bestKind = MatchKind::NoMatch;

    for (size_t i = 0; i < known.size(); ++i) {
        if (isAssigned(static_cast<int>(i), alreadyAssigned)) {
            continue;
        }
        int s = candidateScore(observed, known[i]);
        // UniqueVidPid solo vale si el vid+pid es unico entre no asignados.
        if (s == DeviceMatcher::kScoreUniqueVidPid &&
            unassignedSameVidPid != 1) {
            s = 0;
        }
        if (s <= 0) {
            continue;
        }
        if (s > bestScore) {
            bestScore = s;
            bestIndex = static_cast<int>(i);
            bestKind = (s == DeviceMatcher::kScoreExactEndpoint)
                           ? MatchKind::ExactEndpoint
                       : (s == DeviceMatcher::kScoreSerial)
                             ? MatchKind::Serial
                         : (s == DeviceMatcher::kScoreVidPidLocation)
                               ? MatchKind::VidPidLocation
                           : MatchKind::UniqueVidPid;
            tiesAtBest = 1;
        } else if (s == bestScore) {
            // Empate en el puntaje maximo: ambiguo.
            ++tiesAtBest;
        }
    }

    // Nada coincidio pero hay candidatos: AskOperator (banner no bloqueante).
    if (bestScore <= 0) {
        return MatchResult{MatchKind::AskOperator, -1, kScoreAskOperator};
    }
    // Empate en el mejor puntaje: ambiguo -> AskOperator.
    if (tiesAtBest > 1) {
        return MatchResult{MatchKind::AskOperator, -1, kScoreAskOperator};
    }
    return MatchResult{bestKind, bestIndex, bestScore};
}

} // namespace ba::hw
