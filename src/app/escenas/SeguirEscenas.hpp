// ============================================================================
// src/app/escenas/SeguirEscenas.hpp — lo guardado por el nombre de una escena la sigue
//
// Los conmutadores guardan cosas por el NOMBRE de la escena (la armada de cada salida, las
// transiciones de entrada de Config/transiciones.json). Al renombrar una escena eso tiene
// que tomar el nombre nuevo, y lo de una escena borrada, irse; si no, la armada queda
// apuntando a una escena que no existe y la transición se pierde (se guardaba con el viejo).
// Una función pura: la usa Conmutadores::reconciliarEscenas (con los renombres que anota el
// runtime, AppRuntime::renombresDeEscena) y la prueba sin motor.
// ============================================================================
#pragma once

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::escenas {

struct Seguidas {
    bool transiciones = false;      ///< cambiaron las transiciones de entrada (hay que guardarlas)
    std::vector<std::size_t> armadas; ///< los índices de `armadas` que cambiaron
};

/// Aplica `renombres` (de, a; en orden) y saca lo de las escenas que no están en `hay`:
/// `transiciones` es {escena: {tipo, ms}}; `armadas`, la armada de cada salida ("" = ninguna).
inline Seguidas seguirEscenas(nlohmann::json& transiciones, std::vector<std::string>& armadas,
                              const std::vector<std::pair<std::string, std::string>>& renombres,
                              const std::vector<std::string>& hay) {
    Seguidas s;
    const auto marcar = [&s](std::size_t i) {
        if (std::find(s.armadas.begin(), s.armadas.end(), i) == s.armadas.end()) s.armadas.push_back(i);
    };
    for (const auto& [de, a] : renombres) {
        if (transiciones.is_object() && transiciones.contains(de)) {
            transiciones[a] = transiciones[de];
            transiciones.erase(de);
            s.transiciones = true;
        }
        for (std::size_t i = 0; i < armadas.size(); ++i)
            if (armadas[i] == de) {
                armadas[i] = a;
                marcar(i);
            }
    }
    const auto existe = [&hay](const std::string& n) { return std::find(hay.begin(), hay.end(), n) != hay.end(); };
    if (transiciones.is_object())
        for (auto it = transiciones.begin(); it != transiciones.end();)
            if (!existe(it.key())) {
                it = transiciones.erase(it);
                s.transiciones = true;
            } else {
                ++it;
            }
    for (std::size_t i = 0; i < armadas.size(); ++i)
        if (!armadas[i].empty() && !existe(armadas[i])) {
            armadas[i].clear();
            marcar(i);
        }
    return s;
}

} // namespace ba::app::escenas
