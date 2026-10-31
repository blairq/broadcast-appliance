// ============================================================================
// src/app/evento/LineaVista.hpp — la línea de tiempo, lista para dibujar (L4 y L5)
//
// El plan: docs/planes/PLAN-LINEA-DE-TIEMPO.md, 3.5 A (la Línea multipista), B (la
// escaleta compacta) y C (la conducción). Puro como el resto de la línea: sin Qt,
// sin motor y con el `ahora` inyectado (se prueba en test_linea_vista).
//
// UN SOLO CÁLCULO. Acá no se calcula ningún tiempo: todo sale de la Resolucion del
// resolvedor (front, end y back time, estados, hard starts, over/under, piezas con
// su hora, conflictos) y de lo emitido de la corrida. Esto solo lo ORDENA para la
// vista: filas de la escaleta por bloque, cajas y bloques de los carriles con su
// posición en la ventana (0 = el borde izquierdo, 1 = el derecho), las marcas de la
// regla en la hora del evento y el presupuesto de miniaturas. El QML dibuja.
//
// EL PRESUPUESTO DE MINIATURAS (3.5 A y la memoria «CPU ociosa»): cada vista viva
// cuesta GPU. Al aire se ve el programa de la salida y el próximo su PVW (las dos
// ya existen); las dos escenas que siguen, vistas vivas chicas; el resto, fotos
// cada tanto. Una escena repetida usa la miniatura de la primera vez que aparece.
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "evento/Linea.hpp"
#include "evento/LineaEdicion.hpp"

namespace ba::evento::linea {

// (La escena de un paso por salida: linea::escenaDe, en Linea.hpp: la misma regla que la corrida.)

/// De dónde sale la miniatura de una caja.
enum class FuenteMini {
    Ninguna, ///< el paso no tiene escena
    Programa, ///< el paso al aire: el programa de su salida (ya existe)
    Pvw,      ///< el próximo: el PVW de su salida (lo armó el lookahead)
    Viva,     ///< una de las que siguen: una vista viva chica
    Foto,     ///< el resto: una foto cada tanto, solo si se ve
};
const char* nombreDe(FuenteMini f);

/// Las fuentes de cada escena de la salida según el presupuesto: al aire, el
/// programa; el próximo, el PVW; las `vivas` escenas siguientes (distintas de las
/// que ya tienen), vistas vivas; las demás, foto. Una escena repetida comparte.
std::map<std::string, FuenteMini> presupuestoMiniaturas(const Linea& l, const SalidaResuelta& sr, int vivas = 2);
/// La fuente de la miniatura de un paso: la del presupuesto de su escena, salvo el
/// al aire (siempre el programa) y el próximo (su PVW).
FuenteMini fuenteDe(const PasoResuelto& pr, const std::string& escena, const std::map<std::string, FuenteMini>& presupuesto);

/// Lo que la vista necesita además de la línea y su resolución.
struct ContextoVista {
    int64_t ahoraMs = 0;
    std::string salida; ///< la que se mira (escaleta, conducción)
    /// Lo emitido (de la corrida): la duración real de cada paso.
    std::vector<Instancia> instancias;
    /// Las piezas ya disparadas del paso al aire de `salida` (los adlibs que ya entraron).
    std::set<std::string> disparadas;
    /// ¿Se puede editar ese paso? Vacío = todo. Si no, `porque` dice por qué no.
    std::function<bool(const std::string& paso, std::string& porque)> editable;
    /// Los ids (pasos, piezas, bloques) que tocan los cambios sin confirmar.
    std::set<std::string> pendientes;
    /// "HH:MM:SS" en el huso del evento. Vacía: los ms.
    Hora hora;
    /// La miniatura de una escena con su fuente: {fuente, clave, escena} (las claves de
    /// video las pone quien sabe de video). Vacía: solo {fuente, escena}.
    std::function<nlohmann::json(const std::string& escena, FuenteMini f)> miniatura;
};

/// La escaleta compacta de `ctx.salida` (3.5 B): [{id, nombre, presupuestoSeg,
/// overUnderSeg, desde, hasta, pasos:[{id, numero, nombre, bloque, estado, entra
/// (go|auto|hora|cuando|ancla), entraHora, entraDe, frente, fin, previstoSeg,
/// realSeg?, overSeg, back?, tarde, estimado, hold, escena, mini, chips:[{id, tipo,
/// texto, manual, disparada}], guion, nota, editable, porque, pendiente}]}].
nlohmann::json escaleta(const Linea& l, const Resolucion& r, const ContextoVista& ctx);

/// Lo que agrega la conducción (3.5 C) al estado de la salida (`Corrida::estadoJson`,
/// la entrada de `ctx.salida`): del paso al aire, su escena, su miniatura, su guion,
/// su nota y sus adlibs (las piezas manuales, con si ya se dispararon); del próximo,
/// su escena, su miniatura y cuánto falta; el bloque con su nombre y su presupuesto;
/// el nombre del paso del próximo hard start. Devuelve `estado` completado.
nlohmann::json conduccion(const Linea& l, const Resolucion& r, const ContextoVista& ctx, nlohmann::json estado);

/// El estante de adlibs (la grilla tipo cue cart): [{id, texto, tipo, color, tecla,
/// esPaso}]. Los primeros ocho sin tecla propia llevan F1…F8.
nlohmann::json estante(const Linea& l);
/// El ítem del estante que dispara la tecla (la suya o la F1…F8 que le tocó); "" si ninguno.
std::string itemDeTecla(const Linea& l, const std::string& tecla);

/// Una ventana de tiempo (ms de época).
struct Ventana {
    int64_t desdeMs = 0;
    int64_t hastaMs = 0;
    double x(int64_t ms) const;   ///< 0 = desde, 1 = hasta (puede salirse)
    int64_t ms(double x) const;   ///< al revés
    int64_t largo() const { return hastaMs - desdeMs; }
};

/// La Línea multipista en la ventana (3.5 A): {ahora (x), finPlaneado (x)?, regla:[…],
/// hardStarts:[{x, salida, paso, nombre, hora, gapSeg, nivel (sobra|justo|overlap),
/// politica}], salidas:[{salida, carriles:[{id, tipo, nombre, color, escena (bool),
/// items:[…]}]}], globales:[carriles]}. Las cajas del carril de escena son los PASOS
/// (con miniatura, entrada, prevista y real, el fantasma de lo previsto y el over);
/// los demás carriles llevan las PIEZAS resueltas en esa pista. Las manuales van como
/// botones de su paso. Solo lo que se cruza con la ventana.
nlohmann::json multipista(const Linea& l, const Resolucion& r, const ContextoVista& ctx, const Ventana& v,
                          int64_t medianocheMs);

/// Las marcas de la regla en la hora del evento: [{x, texto, mayor}]. El paso entre
/// marcas sale del largo de la ventana (de 1 s a 1 h), alineado a la medianoche del
/// evento (así caen en «:00», «:15», «:30»).
nlohmann::json regla(const Ventana& v, int64_t medianocheMs, const Hora& hora);

/// "+0:42", "-1:05:10": para el over y las cuentas.
std::string conSigno(int64_t seg);

} // namespace ba::evento::linea
