// ============================================================================
// src/app/agent/VozIdioma.hpp — la voz de cada idioma, leída de su perfil
//
// Qué voz de síntesis habla un idioma, qué reconocedor lo entiende (y cómo se le
// dice el idioma) y a qué ritmo lee el prompter con esa voz: el campo "voz" del
// perfil (`assets/idiomas/<código>.json`; docs/desarrollo/I18N.md, «Perfil del
// idioma: voz»). Nada de esto está escrito en el código: sumar un idioma es
// completar su perfil.
//
// Sin Qt (lo usa agent/Reconocedores, que se prueba solo) y sin estado: cada
// llamada lee el perfil (ba::i18n lo tiene en memoria).
// ============================================================================
#pragma once

#include "i18n/I18n.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <string>
#include <vector>

namespace ba::app::agent::voz_idioma {

/// El campo "voz" del perfil de ese idioma o, si no lo tiene, el del primero de sus
/// respaldos que lo tenga. Un objeto vacío si ninguno (no se sabe nada de su voz).
inline nlohmann::json perfil(const std::string& idioma) {
    const ba::i18n::Perfil p = ba::i18n::perfil(idioma);
    if (p.codigo.empty()) return nlohmann::json::object();
    if (const auto it = p.datos.find("voz"); it != p.datos.end() && it->is_object()) return *it;
    for (const std::string& r : p.respaldo) {
        const ba::i18n::Perfil q = ba::i18n::perfil(r);
        if (const auto it = q.datos.find("voz"); it != q.datos.end() && it->is_object()) return *it;
    }
    return nlohmann::json::object();
}

/// La lengua de un código (lo de antes del guion, en minúscula): "pt-BR" → "pt". Es lo
/// que dice el catálogo de modelos en `idioma` (las voces) y en `de`/`a` (la traducción).
inline std::string lengua(const std::string& codigo) {
    std::string out;
    for (const char c : codigo) {
        if (c == '-' || c == '_') break;
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

/// Si el reconocedor `motor` ("parakeet", "nemotron") entiende ese idioma. Un idioma
/// cuyo perfil no dice nada de reconocimiento cuenta como que sí (no se avisa lo que
/// no se sabe).
inline bool reconoce(const std::string& motor, const std::string& idioma) {
    const nlohmann::json v = perfil(idioma);
    const auto r = v.find("reconocimiento");
    if (r == v.end() || !r->is_object()) return true;
    return r->contains(motor);
}

/// Si el perfil dice algo de los reconocedores de ese idioma.
inline bool sabeReconocimiento(const std::string& idioma) {
    const nlohmann::json v = perfil(idioma);
    const auto r = v.find("reconocimiento");
    return r != v.end() && r->is_object();
}

/// Cómo se le dice el idioma al reconocedor `motor` (el «prompt» de idioma de
/// Nemotron: "es-US"). "" si lo detecta solo ("auto") o si el perfil no lo dice.
inline std::string comoLoDice(const std::string& motor, const std::string& idioma) {
    const nlohmann::json v = perfil(idioma);
    const auto r = v.find("reconocimiento");
    if (r == v.end() || !r->is_object()) return {};
    const auto m = r->find(motor);
    if (m == r->end() || !m->is_string()) return {};
    const std::string s = m->get<std::string>();
    return s == "auto" ? std::string() : s;
}

/// Las voces de síntesis del idioma, en orden de preferencia (repos del catálogo).
inline std::vector<std::string> voces(const std::string& idioma) {
    std::vector<std::string> out;
    const nlohmann::json v = perfil(idioma);
    const auto s = v.find("sintesis");
    if (s == v.end() || !s->is_array()) return out;
    for (const auto& e : *s) {
        if (e.is_string()) out.push_back(e.get<std::string>());
        else if (e.is_object() && e.contains("repo") && e["repo"].is_string()) out.push_back(e["repo"].get<std::string>());
    }
    return out;
}

/// Palabras por segundo que dice la voz `repo` a velocidad 1 (lo que el prompter toma
/// para arrancar; después lo aprende al hablar). La de esa voz en el perfil de algún
/// idioma; si no, la del perfil de `idioma`; si tampoco, `respaldo`.
inline double palabrasPorSegundo(const std::string& repo, const std::string& idioma, double respaldo) {
    for (const ba::i18n::Perfil& p : ba::i18n::idiomas()) {
        const auto v = p.datos.find("voz");
        if (v == p.datos.end() || !v->is_object()) continue;
        const auto s = v->find("sintesis");
        if (s == v->end() || !s->is_array()) continue;
        for (const auto& e : *s)
            if (e.is_object() && e.value("repo", std::string()) == repo && e.contains("palabrasPorSegundo") &&
                e["palabrasPorSegundo"].is_number())
                return e["palabrasPorSegundo"].get<double>();
    }
    const nlohmann::json v = perfil(idioma);
    if (const auto it = v.find("palabrasPorSegundo"); it != v.end() && it->is_number()) return it->get<double>();
    return respaldo;
}

} // namespace ba::app::agent::voz_idioma
