// ============================================================================
// src/app/evento/LineaIntercambio.hpp — la escaleta para afuera y desde afuera (L10)
//
// El plan: docs/planes/PLAN-LINEA-DE-TIEMPO.md, paso L10. La escaleta se arma
// muchas veces en otro lado (una planilla, Rundown Studio, Cuez, la redacción con
// un sistema MOS) o se le pasa a otro (el productor que la quiere en una planilla).
// Dos formatos:
//
//   CSV: un renglón por paso, con encabezado. Las columnas (las claves no se
//     traducen; al leer también se aceptan los nombres en inglés de las planillas
//     comunes: segment, title, duration, script, notes…):
//       bloque, id, nombre, entra, hora, duracion, fin, continuar, escena, guion, nota
//     `entra`: go | tras | con | hora 19:30 [avisar|esperar|cortar] | cuando <condición>
//     | ancla <salida>/<paso>. `duracion`: m:ss, h:mm:ss o segundos. `fin`: vacío (la
//     duración) | abierto | medio | mientras <condición>. `continuar`: no | sigue | con.
//     `hora` sale del pronóstico (si se pasa) y al leer no se usa: la hora es de la corrida.
//     El separador se adivina (coma, punto y coma o tabulador); comillas a lo RFC 4180.
//
//   MOS (running order, 2.8): `roCreate` con un `story` por paso y un `item` por
//     pieza. Lo propio de la línea (bloque, duración, cómo entra, la pieza entera) va
//     en `mosExternalMetadata` (esquema urn:broadcast-appliance:linea), así lo que
//     sale de acá vuelve igual; de otro sistema se toma lo básico (slug, id, duración
//     del story si la trae). Al leer se aceptan roCreate, roStoryAppend,
//     roStoryInsert y roStoryReplace (todos traen `story`): alcanza para traer una
//     escaleta, no es un servidor MOS.
//
// LEER NO APLICA. Lo leído se vuelve un lote para linea_editar (los bloques al final,
// las pistas que falten, los ids que choquen renombrados: lo mismo que una guía,
// lineaDeGuia), así se prueba y lo confirma una persona.
//
// Puro: sin Qt ni motor; se prueba en test_linea_productor.
// ============================================================================
#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "evento/Linea.hpp"
#include "evento/LineaEdicion.hpp"

namespace ba::evento::linea {

/// La escaleta en CSV (con `r`, la columna `hora` del pronóstico de `salida`).
std::string escaletaCsv(const Linea& l, const Resolucion* r = nullptr, const std::string& salida = {}, const Hora& hora = {});
/// La línea en MOS (roCreate). `roId`/`roSlug`: el evento.
std::string escaletaMos(const Linea& l, const std::string& roId, const std::string& roSlug);

/// Lo leído, como línea suelta (bloques, pasos, piezas y las pistas que usan), o el error.
struct Leida {
    bool ok = false;
    std::string error;
    Linea linea;
    nlohmann::json avisos = nlohmann::json::array();
};
Leida leerCsv(const std::string& texto, const std::string& nombreBloque = {});
Leida leerMos(const std::string& xml);

/// La lectura como lote para sumar a `l` (ver lineaDeGuia): {ok, error?, ops, avisos, bloques}.
nlohmann::json importar(const Linea& l, const Leida& leida);

/// "1:30" → 90, "1:02:03" → 3723, "45" → 45. -1 si no se entiende.
int segundosDe(const std::string& texto);
/// 90 → "1:30"; 3723 → "1:02:03".
std::string textoDeSegundos(int seg);

} // namespace ba::evento::linea
