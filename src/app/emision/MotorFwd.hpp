// ============================================================================
// src/app/emision/MotorFwd.hpp — El motor de emisión declarado, sin definirlo
//
// Para los headers que nombran el motor o sus piezas en una firma, un puntero, una
// referencia o un `unique_ptr` (arranque/RuntimeConfig.hpp, los ViewModels) sin usarlos
// por dentro: incluir emision/BroadcastEngine.hpp cuesta ~0,4 s por TU y hace que
// tocarlo recompile a todos (Ola 3 del diseño físico, docs/local/PLAN-DISENO-FISICO.md).
//
// Acá también viven los tipos chicos que viajan en esas firmas y no se pueden declarar
// adelantados por ser anidados: lo que pasó con una orden de salida (ResultadoOrden,
// AlResultado) y los avisos del motor (AvisoMotor). BroadcastEngine los sigue nombrando
// como siempre (BroadcastEngine::ResultadoOrden, BroadcastEngine::AvisoMotor…).
// ============================================================================
#pragma once

#include <functional>
#include <string>

namespace ba::app {

class ApiMotor;
class BroadcastEngine;
class LatidoAire;
class NivelesMotor;
class OperatorFeed;
class TurnoMotor;
struct FotoMotor;

/// Lo que de verdad pasó con una orden de salida que corre en el hilo de salidas
/// (startRecording, startStreaming): que devuelvan true es «encolada», no «hecha». El
/// resultado llega después a quien pidió, por `AlResultado`, DESDE EL HILO DE SALIDAS
/// (quien lo recibe se lo encarga a su hilo). `que`: "grabar" o "transmitir".
struct ResultadoOrden {
    std::string salida, que;
    bool ok = true;
    std::string error;
    std::string archivo; ///< grabar: el que abrió (la foto de las salidas llega después)
};
using AlResultado = std::function<void(const ResultadoOrden&)>;

/// Lo que se le encargó al motor y falló después: las órdenes que pueden
/// tardar van en cola y vuelven enseguida (la interfaz no espera), así que
/// su error llega por acá, desde el hilo del motor.
/// Un aviso del motor de algo que pasó después de pedirlo: TIPADO (quien lo muestra
/// decide por el código y el nivel, nunca comparando el texto, que sale traducido).
struct AvisoMotor {
    enum class Nivel { Error, Bien };
    std::string codigo; ///< estable: "orden_fallida", "disco_apuros", "disco_bien"
    Nivel nivel = Nivel::Error;
    std::string texto; ///< para el operador, traducido
};

} // namespace ba::app
