// ============================================================================
// src/app/escenas/Afv.hpp — el audio que sigue al video de UNA salida (modelo, sin Qt ni motor)
//
// Las fuentes marcadas suben con la escena que las tiene y bajan con la que no.
// Cada salida tiene el suyo (PLAN-SALIDAS): qué fuentes siguen al video y a qué
// nivel vuelve cada una cuando entra (el del operador en esa salida). Lo usa el
// conmutador de cada salida (Conmutador); acá solo la cuenta, para probarla sola.
// ============================================================================
#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::escenas {

struct Afv {
    std::set<std::string> fuentes;        ///< las que siguen al video
    std::map<std::string, float> niveles; ///< a qué vuelve cada una al entrar

    /// Un fundido de una fuente: de su volumen de ahora al que le toca.
    struct Fundido {
        std::string fuente;
        float desde = 0, hasta = 0;
    };
    /// Lo que hay que hacer al poner al aire una escena que tiene `enDestino`, con el
    /// volumen de ahora de cada fuente en esa salida. Una fuente que sale y estaba
    /// arriba recuerda su nivel (sin esto, al volver subía a 0 dB): `recordo` en true
    /// si cambió alguno (hay que guardarlo).
    std::vector<Fundido> alCortar(const std::vector<std::string>& enDestino,
                                  const std::function<float(const std::string&)>& volumen, bool* recordo);

    /// {"afv": [...], "afv_niveles": {...}} (lo de siempre en transiciones.json).
    nlohmann::json aJson() const;
    static Afv deJson(const nlohmann::json& j);
};

} // namespace ba::app::escenas
