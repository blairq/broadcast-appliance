// ============================================================================
// src/app/evento/LineaEdicion.hpp — leer y editar la línea de tiempo
//
// El paso L3 del plan (docs/planes/PLAN-LINEA-DE-TIEMPO.md, 3.7) más la fachada
// del guion v1 (L2). Todo puro: sin Qt, sin motor y sin reloj.
//
// LA FACHADA DEL GUION DE CUES. Desde la línea, lo que manda es la línea: el
// guion de cues (bloques → cues) que siguen viendo la página Guion, `guion_cue`
// y el asistente entrenado con eso es una LECTURA de la línea (`proyectar`): cada
// paso en fila con sus piezas, una pieza por cue. Editar un cue no edita la línea
// a ciegas: se migra de nuevo lo editado y se le devuelve todo lo que el v1 no sabe
// decir (`reconciliar`). La regla es simple: lo que no tocaste en el guion de cues
// queda EXACTAMENTE como estaba en la línea (un paso con sus cues iguales vuelve
// tal cual, con su hard start, su salida y sus piezas de PTZ); lo que tocaste se
// vuelve a migrar, conservando lo de la línea que no tiene que ver con lo tocado.
//
// EDITAR LA LÍNEA. `editar` aplica un lote de operaciones de una (todas o
// ninguna), con lo emitido protegido: en vivo solo se toca el futuro. Devuelve la
// línea nueva y qué cambió; el pronóstico resultante lo arma quien llama con el
// resolvedor.
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "evento/Evento.hpp"
#include "evento/Linea.hpp"
#include "evento/LineaProductor.hpp"

namespace ba::evento::linea {

// --- La fachada del guion de cues ------------------------------------------------------

/// De qué parte de la línea sale cada cue del guion.
struct RefCue {
    std::string paso;
    std::string pieza; ///< vacío: el cue es el paso mismo (una pausa o una espera)
};

struct Proyeccion {
    std::vector<ba::evento::Bloque> bloques;
    std::vector<std::vector<RefCue>> refs; ///< [bloque][cue], en paralelo con `bloques`

    /// Dónde está el cue de esa pieza; sin pieza, el primer cue del paso. {-1, -1} si no está.
    std::pair<int, int> cueDe(const std::string& paso, const std::string& pieza = {}) const;
    const RefCue* ref(int bloque, int cue) const;
};

/// La línea leída como guion de cues: un cue por pieza, en el orden de cada paso.
/// El paso que entra con una escena, un orador o un video empieza con ese cue (con
/// el id del paso); uno sin eso empieza con una pausa (si sigue solo) o una espera
/// (si tiene nota para el operador), salvo el «<bloque>-inicio» que crea la
/// migración. La duración de cada cue es lo que hay hasta el siguiente, y es
/// automático si el que sigue entra solo (el último: si el paso sigue solo).
Proyeccion proyectar(const Linea& l);

/// La línea después de editar su guion de cues (`v1`): lo que no cambió queda como
/// estaba en `vieja`, lo que cambió se migra de nuevo conservando lo propio de la
/// línea (salida, hard start, esperas, transición, vida y anclas puestas a mano,
/// piezas que el v1 no conoce). El estante, los disparadores, las plantillas y las
/// salidas son de la línea y no se tocan.
Linea reconciliar(const Linea& vieja, const std::vector<ba::evento::Bloque>& v1);

// --- Editar ---------------------------------------------------------------------------

struct Edicion {
    bool ok = false;
    std::string error; ///< por qué no (traducido)
    Linea linea;       ///< la nueva (si ok)
    nlohmann::json diff = nlohmann::json::array(); ///< [{que, tipo, id, antes?, despues?}]
};

/// Aplica `ops` (un array) de una: si una falla, no cambia nada. Operaciones:
///   {op:"bloque_agregar", bloque:{…}, despuesDe?}       sin despuesDe: al final; "" = al principio
///   {op:"paso_agregar", bloque, paso:{…}, despuesDe?}   ídem dentro del bloque
///   {op:"pieza_agregar", paso, pieza:{…}, despuesDe?}
///   {op:"pista_agregar", pista:{…}}   {op:"estante_agregar", item:{…}}
///   {op:"mover", id, bloque?|paso?, despuesDe?}         un bloque, un paso (a `bloque`) o una pieza (a `paso`)
///   {op:"quitar", id}
///   {op:"cambiar", id, campos:{…}}                       las claves de `campos` reemplazan las del objeto
/// `intocable(paso)`: true si ese paso ya salió o está al aire (en vivo solo se toca
/// el futuro). Vacío = todo se puede tocar.
Edicion editar(const Linea& l, const nlohmann::json& ops, const std::function<bool(const std::string&)>& intocable = {});

/// Qué cambió entre dos líneas, por id: [{que: agregado|quitado|cambiado|movido, tipo, id, antes?, despues?}].
nlohmann::json diferencias(const Linea& antes, const Linea& despues);

// --- Ver ------------------------------------------------------------------------------

/// Cómo escribir una hora (ms de época) para quien lee: la pone quien llama (con el
/// huso del evento). Vacía: los ms.
using Hora = std::function<std::string(int64_t)>;

/// El pronóstico compacto de una salida, para que el asistente no tenga que leer el
/// JSON entero: bloques → pasos (→ piezas con `nivel` "piezas"). `desde`/`hasta`
/// (ms, 0 = sin límite) dejan solo lo que se cruza con esa ventana.
nlohmann::json vista(const Linea& l, const Resolucion& r, const std::string& salida, int64_t desdeMs, int64_t hastaMs,
                     const std::string& nivel, int64_t ahoraMs, const Hora& hora = {});

/// La escena que pone un paso (la de su pieza de escena que no es manual), o "".
std::string escenaDePaso(const Linea& l, const Paso& p);

/// La pantalla de cámara (la Camera screen de Sofie): para cada cámara, los planos
/// que la usan, al aire y los que vienen, de TODAS las salidas (las cámaras se
/// comparten). `vistas` es un arreglo con la vista (nivel pasos) de cada salida; una
/// cámara sale por la escena del paso (`camarasDeEscena`) o por una pieza `ptz` del
/// paso (datos.camara). {camara: [{salida, paso, nombre, estado, frente, enSeg?, escena?, ptz?}]},
/// lo que está al aire primero y después por lo que falta.
nlohmann::json proximosPorCamara(const Linea& l, const nlohmann::json& vistas,
                                 const std::function<std::vector<std::string>(const std::string&)>& camarasDeEscena);

// El solucionador del back-timing (proponerAjuste) está en LineaProductor.hpp.

} // namespace ba::evento::linea
