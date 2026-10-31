// ============================================================================
// src/app/evento/LineaPlantillas.hpp — pasos armados desde una plantilla (L7)
//
// El plan: docs/planes/PLAN-LINEA-DE-TIEMPO.md (3.2 plantillas y 3.6). Una
// plantilla es un paso-modelo ("punto del orden del día", "entrevista") con
// marcas {clave} en sus textos y en los datos de sus piezas. Instanciarla es
// copiarla con ids propios y las marcas llenas.
//
// EL ORDEN DEL DÍA. Un bloque con `plantilla` y `origen: "asamblea/orden-del-dia"`
// se EXPANDE en un paso por punto (o los que tenga la plantilla, por punto): la
// escena del debate, el zócalo con {titulo}, la pieza `asamblea: tratar {n}`.
// Si el orden del día cambia, se vuelve a expandir SIN TOCAR lo emitido (como la
// reimportación de Ignite): lo que ya salió queda como salió y donde estaba; lo
// que no salió se rehace con los puntos nuevos; lo que alguien agregó a mano al
// bloque (sin la etiqueta "orden-del-dia") queda después.
//
// Puro: sin Qt; los puntos entran como el JSON de asamblea::OrdenDelDia::puntos().
// ============================================================================
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "evento/Linea.hpp"

namespace ba::evento::linea {

/// El origen de los bloques que se arman con el orden del día.
inline constexpr const char* kOrigenOrdenDelDia = "asamblea/orden-del-dia";
/// La etiqueta de los pasos que arma la expansión (los demás del bloque son a mano).
inline constexpr const char* kEtiquetaOrdenDelDia = "orden-del-dia";

/// Los pasos de `t` con las marcas {clave} llenas con `valores` (en el nombre, el
/// guion, la nota, los textos de las piezas y cualquier texto de sus datos) y con
/// ids nuevos: "<prefijo>-<id del paso>" y "<prefijo>-<id de la pieza>". Un dato que
/// es SOLO una marca de algo numérico ("{n}") queda como número.
std::vector<Paso> instanciarPlantilla(const Plantilla& t, const std::string& prefijo,
                                      const std::map<std::string, std::string>& valores);

/// Vuelve a armar los bloques que salen del orden del día con `puntos`
/// ([{n, expediente, titulo, estado, …}]; los «retirado» no van). `emitidos`: los
/// pasos que ya salieron o están al aire, que no se tocan. true si cambió algo.
bool expandirOrdenDelDia(Linea& l, const nlohmann::json& puntos, const std::set<std::string>& emitidos);

} // namespace ba::evento::linea
