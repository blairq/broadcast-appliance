// src/app/agent/ArgumentosLocales.hpp — los argumentos de una herramienta, con el modelo local
//
// El modelo local elige la herramienta (elegir) pero no sus datos: qué fuente,
// cuánto, qué escena. Esto arma el pedido para que los escriba (generar) y
// valida lo que contesta antes de que nada salga al aire:
//
//   - el prompt: la herramienta, su esquema, el estado del aparato (escenas,
//     fuentes, audio con su volumen, participantes, gráficos) y el pedido,
//     con las reglas en datos/voz-argumentos.txt;
//   - la respuesta: el primer objeto JSON;
//   - la validación: solo claves del esquema; los nombres, a uno que exista
//     del tipo que maneja la herramienta (sin mayúsculas ni acentos, o el
//     único que lo contiene, o uno muy parecido); los cambios relativos
//     ("+3", "-6": el modelo no hace cuentas) sobre el valor actual; los
//     valores de un enum y los requeridos.
//
// Lo que no se puede validar queda en `bloqueos` y no se ejecuta.
// Medido con tools/voz-banco.py (docs/voz-modelo-local.md).

#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ba::app::agent::argumentos {

using json = nlohmann::json;

/// La parte que no cambia (las reglas): datos/voz-argumentos.txt.
std::string sistemaPorDefecto();

/// La parte del estado que usa la herramienta (las escenas para las de
/// escenas, el audio para las de audio…): un prompt más corto contesta antes.
json estadoPara(const std::string& herramienta, const json& estado);

/// El texto para `generar` (sin prefijo): plantilla de chat de Qwen, sin
/// razonamiento, y la respuesta empezada con «{». Usa estadoPara().
std::string prompt(const std::string& sistema, const std::string& herramienta, const json& ficha,
                   const json& estado, const std::string& frase);

/// El primer objeto JSON de lo que escribió el modelo (al que se le antepone
/// la «{» del prompt). Tolera claves sin comillas y el objeto envuelto en el
/// nombre de la herramienta (`{"audio_analyze": {...}}`, `{capture_program}`).
/// Nulo si no hay uno válido.
json leer(const std::string& generado, const std::string& herramienta = {});

struct Resultado {
    json args = json::object();
    std::vector<std::string> notas;    ///< lo que se corrigió (nombre completado, relativo…)
    std::vector<std::string> bloqueos; ///< lo que impide ejecutar (no existe, falta requerido…)
};

/// Valida `args` contra el esquema de `ficha` (la de tools/list) y el estado.
Resultado validar(const std::string& herramienta, const json& ficha, const json& args, const json& estado);

/// Una línea para mostrar: «name: Música · volumeDb: -16».
std::string resumen(const json& args);

// --- Formato tc (modelo v2: tool calling nativo de Qwen3.5) --------------------
//
//   <tool_call>
//   <function=audio_set>
//   <parameter=name>
//   Música
//   </parameter>
//   <parameter=volumeDb>
//   -4
//   </parameter>
//   </function>
//   </tool_call>
//
// Valores (en XML no hay comillas): nombres copiados del estado; cambio con
// signo (-4 / +3); valor exacto con «=» adelante (=-10); true / false; enums
// tal cual. Se lee con la misma expresión que lib_tc.leer.
struct LlamadaTc {
    bool hay = false;                                            ///< había un <tool_call>
    std::string funcion;                                         ///< el nombre (o "derivar", "falta_dato")
    std::vector<std::pair<std::string, std::string>> parametros; ///< tal cual (texto)
};
LlamadaTc leerTc(const std::string& generado);
/// Los parámetros, convertidos según el esquema y las convenciones de la v2:
/// "=X" exacto; "+N"/"-N" cambio (queda como texto con signo para que
/// `validar` lo sume al valor actual); un número sin signo, tal cual;
/// booleanos; listas y objetos (JSON). Después va por `validar`.
json argsDesdeTc(const LlamadaTc& llamada, const json& ficha);

} // namespace ba::app::agent::argumentos
