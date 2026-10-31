// ============================================================================
// src/app/evento/LineaProductor.hpp — el asistente productor de la línea (L9)
//
// El paso L9 del plan (docs/planes/PLAN-LINEA-DE-TIEMPO.md, 3.7). Todo puro:
// sin Qt, sin motor y sin reloj. Lo usan linea_ajustar, linea_desde_plantilla,
// el rol que vigila el over/under (ui/LineaProductor) y la conducción.
//
// LA REGLA DE ORO: nada de esto aplica. Devuelve lotes de operaciones para
// `editar` (LineaEdicion.hpp) que se prueban primero (probar:true) y que aplica
// una persona. El asistente PROPONE; la persona confirma.
//
// EL SOLUCIONADOR DEL BACK-TIMING («terminar a las 21:00»). Mira el pronóstico
// de una salida y, si no se llega, arma opciones como el «Arreglar» de Rundown
// Studio:
//   - recortar: los pasos que vienen, según lo que dicen sus etiquetas
//     («recortable» = hasta la mitad, «recortable:30» = hasta el 30 %), de la
//     prioridad más baja a la más alta («prioridad:1» se recorta primero,
//     «prioridad:9» último; sin etiqueta, 5), en proporción a lo que dura cada uno
//     dentro de la misma prioridad;
//   - absorber: que UN paso que viene se haga cargo de toda la diferencia;
//   - saltear: los «opcional» que vienen, hasta cubrir lo que falta;
//   - mover la hora: correr un hard start (el que queda pisado, o el que deja un
//     hueco);
//   - relleno: si sobra, un paso de relleno del largo del hueco.
// Lo mismo, para cada hard start que viene con overlap (no se llega) o con gap
// (sobra): las opciones se arman con los pasos que hay ANTES de ese hard start.
// Un paso con la etiqueta «fijo» no se recorta ni absorbe.
// ============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "evento/Evento.hpp"
#include "evento/Linea.hpp"

namespace ba::evento::linea {

// --- Etiquetas que entiende el productor ---------------------------------------------

/// Cuánto se le puede sacar a un paso, en % (0 = nada): «recortable» 50, «recortable:N» N (1 a 90).
int recortablePct(const Paso& p);
/// De 1 (se recorta primero) a 9 (último): «prioridad:N»; sin etiqueta, 5.
int prioridadDe(const Paso& p);
bool tieneEtiqueta(const Paso& p, const std::string& etiqueta);

// --- Ajustar --------------------------------------------------------------------------

/// El solucionador. `objetivoFinMs` <= 0: el fin planeado de la salida (si no hay,
/// no hay contra qué medir y solo mira los hard starts). Devuelve
///   {objetivoMs, overSeg, recortaSeg, faltaSeg, ops, saltear, texto,
///    opciones: [{id, tipo, texto, ganaSeg, ops}],
///    hardStarts: [{paso, nombre, gapSeg, opciones: [...]}]}
/// `ops` (arriba) es la opción «recortar»: lo que se propone primero. `tipo` de cada
/// opción: recortar | absorber | saltear | mover_hora | relleno | estirar. Nunca aplica.
nlohmann::json proponerAjuste(const Linea& l, const Resolucion& r, const std::string& salida, int64_t objetivoFinMs);

/// "19:30" + minutos → "HH:MM" (da la vuelta a la medianoche). "" si no se entiende.
std::string sumarMinutos(const std::string& hhmm, int minutos);

// --- Plantillas -----------------------------------------------------------------------

/// Las plantillas de fábrica (las del evento, `Linea::plantillas`, van primero):
/// «orador» (un paso abierto con su zócalo y la palabra), «punto» (un punto del
/// orden del día) y «votacion». Sus textos llevan {campo}.
std::vector<Plantilla> plantillasDeFabrica();

struct DesdePlantilla {
    std::string plantilla;  ///< el id (de la línea o de fábrica)
    nlohmann::json datos = nlohmann::json::array(); ///< una fila por repetición: {nombre, cargo, tiempoSeg…}
    std::string bloque;     ///< dónde van; vacío = un bloque nuevo al final
    std::string nombreBloque;
    std::string despuesDe;  ///< dentro del bloque (vacío = al final; "" con `alPrincipio`)
    bool alPrincipio = false;
    std::string salida;     ///< la de los pasos y las pistas (vacía = la primera)
    std::string prefijo;    ///< de los ids nuevos (vacío = el de la plantilla)
};

/// Arma los pasos: una copia de los pasos de la plantilla por cada fila de
/// `datos`, instanciada con evento/LineaPlantillas (instanciarPlantilla: cada
/// {campo} con el dato de la fila; el que la fila no trae, vacío). Una pieza a la
/// que le falta lo esencial (un dato que es SOLO «{campo}», como la escena o quién
/// habla) no va, y se avisa. `tiempoSeg` (o `duracionSeg`) de la fila es lo que
/// dura el paso. Las pistas se buscan por id o por tipo en la salida; las que
/// faltan se agregan. Ids nuevos sin chocar con los de la línea.
/// {ok, error?, ops, avisos, pasos:[ids]}.
nlohmann::json desdePlantilla(const Linea& l, const DesdePlantilla& pedido);

/// Un bloque nuevo que sale del orden del día (`plantilla` + origen
/// "asamblea/orden-del-dia"), expandido con evento/LineaPlantillas
/// (expandirOrdenDelDia: un paso por punto; si después cambia el orden del día, se
/// re-expande sin tocar lo emitido). La plantilla, de la línea o de fábrica.
/// {ok, error?, ops (pistas que falten + bloque_agregar), avisos, pasos:[ids], bloque}.
nlohmann::json bloqueDeOrdenDelDia(const Linea& l, const std::string& plantilla, const nlohmann::json& puntos,
                                   const std::string& nombreBloque, const std::string& salida = {});

/// La línea de una guía de escenario (el guion de su plantilla, assets/demo) como
/// lote para sumar a la línea `l`: bloques al final, pistas que falten, ids que
/// choquen renombrados. {ok, ops, avisos, bloques:[ids]}.
nlohmann::json lineaDeGuia(const Linea& l, const ba::evento::Evento& guia);
/// Lo mismo, con una guía que ya es una línea (una plantilla guardada con la v2).
nlohmann::json lineaDeGuia(const Linea& l, Linea guia);

// --- El rol que vigila el over/under ----------------------------------------------------

/// Cuándo vale la pena avisar: el over pasó el umbral y (es la primera vez, o
/// creció un escalón desde el último aviso, o pasó el silencio y sigue igual).
/// Si se recupera por debajo de la mitad del umbral, se olvida lo avisado.
struct Vigia {
    int umbralSeg = 60;
    int escalonSeg = 60;
    int64_t silencioMs = 5 * 60 * 1000;

    struct PorSalida {
        int avisadoSeg = 0; ///< el over del último aviso (0 = no se avisó)
        int64_t avisadoMs = 0;
    };
    std::map<std::string, PorSalida> salidas;

    /// ¿Hay que proponer ahora? Si sí, lo anota como avisado.
    bool debeProponer(const std::string& salida, int overSeg, int64_t ahoraMs);
};

} // namespace ba::evento::linea
