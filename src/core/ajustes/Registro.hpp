// ============================================================================
// src/core/ajustes/Registro.hpp — el registro central de los ajustes «avanzados»
//
// Una fila por clave de ba::ajustes (Ajustes.hpp): la sección, el rótulo y la
// explicación (marcados para traducir), el tipo, el rango, el valor de siempre,
// a qué archivo va (app.json, gateway.json o launcher.json, bajo "ajustes") y su
// ALCANCE, o sea qué hace falta para que rija:
//   live     en caliente: el código lo lee al usarlo. Los de la sala se le
//            mandan al gateway por su API (PUT /api/v1/config/ajustes): no se
//            reinicia la conferencia;
//   restart  al volver a abrir el programa;
//   gateway  reinicia la conferencia (el gateway lo lee una sola vez al arrancar);
//   motor    rearma el motor.
// La página de Configuración (SettingsModel) arma sus filas con esto; el gateway
// valida con esto lo que le llega. El código consulta el valor con el MISMO
// respaldo que esta tabla.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

namespace ba::ajustes {

/// A qué archivo va (y qué proceso lo usa).
enum class Destino { App, Sala, Launcher };

struct Definicion {
    const char* clave;
    const char* seccion;  ///< una clave: la traduce la página al mostrarla
    const char* etiqueta; ///< marcada con QT_TRANSLATE_NOOP("ajustes", …)
    const char* tipo;     ///< int, float, bool, text, choice
    std::vector<std::pair<const char*, const char*>> opciones; ///< para "choice": {valor, rótulo}
    double min = 0, max = 0;
    const char* sufijo;
    const char* ayuda;
    const char* alcance; ///< live | restart | gateway | motor (ver arriba)
    nlohmann::json defecto;
    Destino destino = Destino::App;
};

const std::vector<Definicion>& registro();
/// La fila de `clave` (nullptr si no está).
const Definicion* buscar(const std::string& clave);
/// "" si `valor` es del tipo de `clave` y está en su rango; si no, por qué (corto, en
/// castellano: lo arma quien lo muestra).
std::string validar(const std::string& clave, const nlohmann::json& valor);

} // namespace ba::ajustes
