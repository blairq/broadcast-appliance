// RecoveryUi.hpp
//
// Proposito:
//   UI de recuperacion minima del launcher (Win32 puro, sin Qt, /MT; spec
//   3.1.11). Es la unica ventana visible del Proceso 0: cuando el Supervisor
//   detecta que un hijo (A o B) ha caido o colgado, muestra el incidente (que
//   proceso, codigo de salida, estado de integridad del disco) y deja al
//   operador elegir una accion: reintentar, forzar reinicio o salir.
//
// Modelo de threading:
//   present() se invoca desde el thread principal (el del Supervisor) y es
//   BLOQUEANTE: registra la clase de ventana, crea la ventana y sus controles
//   y corre su propio bucle de mensajes hasta que el usuario elige una accion
//   (boton) o cierra la ventana (X). No hay threads adicionales; el bucle es
//   el unico consumidor de mensajes de la ventana.
//
// Estrategia de errores:
//   Nunca lanza. Si no se puede registrar la clase de ventana, crear la
//   ventana o crear los controles, se loguea el GetLastError() y se devuelve
//   Dismissed (el Supervisor lo interpreta como "continuar con el flujo
//   normal"). El bucle solo termina por PostQuitMessage (boton o WM_DESTROY).

#pragma once

#include <cstdint>
#include <string>

#include "Integrity.hpp"

namespace ba::launcher {

/// Decision del operador en la UI de recuperacion.
enum class RecoveryAction {
    Retry,        // Reintentar: relanzar el hijo caido con el backoff normal.
    ForceRestart, // Forzar reinicio: terminar todos los hijos y relanzar desde 0.
    Exit,         // Salir: cierre ordenado del launcher.
    Dismissed     // Se cerro la ventana (X) sin elegir; el Supervisor continua.
};

/// Descripcion del incidente que la UI debe presentar. El Supervisor aporta los
/// hechos (que hijo caigo, codigos de salida, integridad); la UI solo los
/// presenta, sin logica de negocio.
struct Incident {
    std::wstring title;                // Titulo de la ventana.
    bool childADown = false;           // A caido o colgado.
    bool childBDown = false;           // B caido o colgado.
    bool appCodeValid = false;         // appExitCode es un codigo real de salida.
    std::uint32_t appExitCode = 0;     // Codigo de salida de A (si appCodeValid).
    bool gwCodeValid = false;          // gatewayExitCode es un codigo real de salida.
    std::uint32_t gatewayExitCode = 0; // Codigo de salida de B (si gwCodeValid).
    IntegrityReport integrity;         // Estado de integridad (si showIntegrity).
    bool showIntegrity = false;        // Si procede mostrar la linea de integridad.
};

/// UI de recuperacion minima del launcher (spec 3.1.11).
class RecoveryUi {
public:
    /// Muestra la ventana de recuperacion (bloqueante) y devuelve la decision
    /// del operador.
    ///
    /// :param inc: incidente a presentar (hijos caidos, codigos, integridad).
    /// :returns: la accion elegida; Dismissed si se cerro la ventana (X) o no
    ///           se pudo mostrar la UI (el Supervisor la trata como
    ///           "continuar con el flujo normal").
    static RecoveryAction present(const Incident& inc);
};

} // namespace ba::launcher
