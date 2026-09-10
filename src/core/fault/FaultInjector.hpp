// FaultInjector.hpp
//
// Proposito:
//   Andamiaje generico de INYECCION DE FALLOS (fault-injection) para el nucleo
//   ba_core (spec §13, "Pruebas y criterios de aceptacion" / "Checklist de
//   inyeccion de fallas"). Es una ayuda de TEST/INTEGRACION: permite a un
//   componente bajo prueba forzar un modo de fallo concreto (disco lleno,
//   checksum que no coincide, crash de plugin, timeout) y verificar el camino
//   de recuperacion correspondiente, SIN depender de hardware ni de timing
//   real. No es una pieza de producto: vive en el camino de control/prueba,
//   NUNCA en el camino de audio en tiempo real (RT).
//
//   El mecanismo es un registro de "fallo armado" (fault armed). Un componente
//   bajo prueba consulta `armed()` para saber si debe comportarse como si el
//   fallo hubiera ocurrido, y llama a `fire()` para reportar que consumio el
//   fallo (devuelve true si lo consumio, false si no habia nada armado). Asi el
//   mismo FaultInjector sirve para todos los modos de fallo y para cualquier
//   componente, sin re-implantar inyeccion a mano en cada uno.
//
// Modelo de threading:
//   Thread-SAFE. El estado es un `std::atomic<FaultKind>`: arm/disarm/armed/
//   fire pueden llamarse desde distintos threads sin data race. `fire()` usa
//   compare-exchange para que, si dos threads compiten por consumir el mismo
//   fallo, solo UNO lo consuma (el otro recibe false). `instance()` es el
//   registro process-wide (singleton de Meyers, inicializacion thread-safe en
//   C++11+); tambien se puede usar una instancia propia por componente si se
//   prefiere aislar los tests.
//
// Estrategia de errores:
//   No lanza. arm()/disarm() no fallan (solo escriben el atomic). armed()
//   devuelve el valor actual. fire() devuelve bool (true = consumio un fallo,
//   false = no habia nada armado). writeWithFault() devuelve false solo ante un
//   fallo de E/S de disco (archivo inexistente o no escribible); en ese caso
//   `outPath` queda indeterminado.
//
// RT-safety:
//   Es una ayuda de TEST/INTEGRACION. `armed()`/`fire()` son atomicos baratos
//   (una carga/compare-exchange), pero el contrato es que se usen en el camino
//   de control o en tests, NO en la callback de audio RT: el steady RT path no
//   debe depender de esta pieza. writeWithFault() hace E/S de disco (lee y
//   escribe un archivo completo) y es explicitamente de control path.
#pragma once

#include <atomic>
#include <cstddef>
#include <string>

namespace ba::fault {

/// Modos de fallo que puede inyectar el andamiaje (spec §13, checklist de
/// inyeccion de fallas).
///
/// `None` significa "sin fallo armado" (estado por defecto). Cada valor
/// representa un modo de fallo concreto que un componente bajo prueba puede
/// detectar con `armed()` y consumir con `fire()`. El subyacente es `int`
/// para que `std::atomic<FaultKind>` sea trivialmente copiable y portable.
enum class FaultKind : int {
    None,             // sin fallo (estado por defecto / tras disarm o fire).
    DiskFull,         // el disco "no acepta" mas escritura (snapshot, logs...).
    ChecksumMismatch, // un archivo que se lee tiene bytes alterados (checksum
                      // que no coincide; ver writeWithFault()).
    PluginCrash,      // un plugin "cayo" (host de plugins debe recuperarse).
    Timeout,          // una operacion "se quedo" (el caller debe reintentar /
                      // degradar).
};

/// Andamiaje generico de inyeccion de fallos (spec §13).
///
/// Un registro de "fallo armado" thread-safe. El componente bajo prueba
/// consulta `armed()` para decidir si se comporta como si el fallo ocurriera,
/// y llama a `fire()` para reportar que lo consumio. Sirve tanto como registro
/// process-wide (via `instance()`) como instancia aislada por componente.
///
/// Es una ayuda de TEST/INTEGRACION, no del steady RT path (ver docstring de
/// modulo). No lanza.
class FaultInjector {
public:
    /// Constructor: arma `FaultKind::None` (sin fallo).
    FaultInjector() = default;

    /// Registro process-wide (singleton de Meyers).
    ///
    /// Devuelve la instancia compartida por todo el proceso, para que un
    /// componente bajo prueba y su test armen/consulten el MISMO registro sin
    /// pasar punteros. La inicializacion es thread-safe (C++11+). Si se
    /// prefiere aislar tests, se usa una instancia propia de FaultInjector en
    /// su lugar.
    ///
    /// :returns: referencia a la instancia process-wide.
    static FaultInjector& instance();

    /// Arma un modo de fallo concreto.
    ///
    /// Sustituye el estado actual por `kind`. Armar `FaultKind::None` es
    /// equivalente a `disarm()`. Es idempotente y thread-safe.
    ///
    /// :param kind: modo de fallo a armar (ver :type:`FaultKind`).
    void arm(FaultKind kind);

    /// Desarma cualquier fallo armado (vuelve a `FaultKind::None`).
    ///
    /// Thread-safe. No hace nada si ya no habia fallo.
    void disarm();

    /// Devuelve el modo de fallo actualmente armado.
    ///
    /// El componente bajo prueba lo usa para decidir si se comporta como si el
    /// fallo ocurriera. Con `FaultKind::None` no hay fallo. Thread-safe.
    ///
    /// :returns: el FaultKind armado (None si no hay).
    FaultKind armed() const;

    /// Consome el fallo armado, si lo hay.
    ///
    /// Si hay un fallo armado (distinto de None), lo "consume" (lo deja en
    /// None) y devuelve true: el caller sabe que el fallo fue suyo y que ya no
    /// lo va a ver en la proxima `armed()`. Si no habia fallo armado, devuelve
    /// false y no cambia nada. Thread-safe: si dos threads compiten, solo uno
    /// recibe true (compare-exchange).
    ///
    /// :returns: true si consumio un fallo; false si no habia nada armado.
    bool fire() const;

private:
    // `mutable` porque fire() (const) consume el fallo: es un efecto colateral
    // de tipo "contador", no un cambio del estado logico observable por
    // armed() mas alla de lo que fire() ya reporto.
    mutable std::atomic<FaultKind> armed_{FaultKind::None};
};

/// Demostracion autocontenida del modo `ChecksumMismatch` (spec §13).
///
/// Escribe una COPIA de `sourcePath` en `outPath`. Si `injector` tiene armado
/// `FaultKind::ChecksumMismatch`, la copia lleva EXACTAMENTE UN byte alterado
/// (XOR 0x01 sobre el primer byte), de modo que un caller que verifique el
/// checksum (p. ej. SnapshotStore/Restore) observe un mismatch. Si no hay ese
/// fallo armado (None u otro kind), la copia es EXACTA (byte a byte).
///
/// Es de CONTROL PATH: hace E/S de disco (lee el archivo completo y lo
/// reescribe). No consume el fallo: deja el ciclo arm->observar->consumir en
/// manos del caller (que decide cuando llamar a `fire()`). No lanza.
///
/// :param injector: registro a consultar para el modo de fallo (se usa
///                  `armed()`; no se muta).
/// :param sourcePath: ruta del archivo original a copiar (debe existir y ser
///                    legible).
/// :param outPath: ruta donde se escribe la copia (se crea/trunca).
/// :returns: true si se copio (con o sin byte alterado); false si `sourcePath`
///           no existe / no se puede leer o `outPath` no se puede escribir.
bool writeWithFault(const FaultInjector& injector,
                    const std::string& sourcePath,
                    const std::string& outPath);

} // namespace ba::fault
