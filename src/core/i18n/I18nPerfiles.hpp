// ============================================================================
// src/core/i18n/I18nPerfiles.hpp — el perfil de cada idioma
//
// `Perfil` (lo que cambia entre idiomas, en datos) y `Cobertura`, y lo que los devuelve:
// los idiomas que hay, el perfil de un código, el del operador (`actual`). Lo usan las
// pantallas y herramientas que muestran o eligen idiomas; para elegir el del proceso o
// traducir en otro alcanza con I18nIdioma.hpp, y para traducir con I18nTexto.hpp. Trae
// las dos (un archivo incluye siempre una sola parte). El cuadro completo, en I18n.cpp.
// ============================================================================
#pragma once

#include "i18n/I18nIdioma.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ba::i18n {

/// El perfil de un idioma: lo que cambia entre idiomas, en datos.
struct Perfil {
    std::string codigo;        ///< BCP 47: "es", "en", "pt-BR"
    std::string nombre;        ///< en ese idioma: "Español", "English", "Português (Brasil)"
    std::string locale;        ///< el de los formatos (números, fechas) si el sistema no dice otro: "es_AR"
    bool derechaAIzquierda = false;
    std::string plural = "uno-otro"; ///< la regla de plurales (ver formaPlural)
    std::vector<std::string> respaldo; ///< a qué idiomas recurrir antes que al original
    bool fuente = false;       ///< el idioma en que está escrito el código (no tiene catálogo)
    bool pseudo = false;       ///< el de pruebas: transforma el original (ver pseudo)
    std::string tipografia;    ///< la familia para esta escritura ("" = la de siempre)
    nlohmann::json datos;      ///< el perfil entero (voz, tótem… los leen sus módulos)
};

/// La cobertura de un idioma: cuántos textos del catálogo están traducidos.
struct Cobertura {
    int total = 0;
    int traducidos = 0;
    /// 0..1; el original y el pseudo-idioma están completos por definición.
    double fraccion() const { return total <= 0 ? 1.0 : static_cast<double>(traducidos) / total; }
};

/// Todos los idiomas que hay (el original primero; el resto por código).
std::vector<Perfil> idiomas();
/// El perfil de ese código, o uno vacío (codigo == "") si no existe.
Perfil perfil(const std::string& codigo);
Cobertura cobertura(const std::string& codigo);

/// El que se está usando (el original si nunca se llamó a usar). Para el código solo,
/// idiomaDelOperador() (I18nTexto.hpp).
const Perfil& actual();

} // namespace ba::i18n
