// DeviceIdentityResolver.hpp
//
// Proposito:
//   ALGORITMO de resolucion de identidad fisica (spec 9.2). WASAPI no expone el
//   Device Instance Path del dispositivo USB, asi que hay que subir por el
//   arbol de devnodes hasta llegar al nodo USB. En Windows eso es
//   `CM_Locate_DevNodeW` + `CM_Get_Parent` (cfgmgr32); aqui se modela sobre un
//   arbol abstracto de devnodes (tabla de nodos con id + padre) para que el
//   ALGORITMO sea testeable en Linux sin cfgmgr32.
//
// Modelo de threading:
//   Stateless y const: la instancia no guarda nada y el metodo es const, por
//   lo que puede usarse en paralelo. La tabla de nodos se lee por const-ref y
//   no se muta.
//
// Estrategia de errores:
//   No lanza. Devuelve bool: true si se encontro un ancestro (USB o el nodo
//   tope) y `outUsb` queda con ese nodo; false si la tabla esta vacia o el id
//   inicial no existe en la tabla (nodo desconocido). `outUsb` se deja sin
//   modificar en fallo.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ba::hw {

/// Un devnode del arbol de dispositivos: id + id del padre ("" = raiz) +
/// flag de si es nodo USB + vid/pid/serial opcionales. Modela el minimo que
/// necesita el walk de spec 9.2 para ser testeable sin cfgmgr32.
struct DevNode {
    std::string id;     // id de instancia del devnode
    std::string parent; // id del devnode padre; "" = raiz (sin padre)
    bool isUsb = false; // true si este nodo es el dispositivo USB fisico
    std::uint32_t vid = 0;
    std::uint32_t pid = 0;
    std::string serial;
};

/// Resuelve el ancestro USB de un endpoint subiendo por el arbol de devnodes
/// (spec 9.2). Stateless.
class DeviceIdentityResolver {
   public:
    /// Dado el id del devnode del endpoint y la tabla de nodos, sube por el
    /// arbol (equivalente a `CM_Get_Parent`) hasta llegar a un nodo USB;
    /// devuelve ese nodo (el dispositivo USB fisico).
    ///
    /// Comportamiento:
    ///  - Si el id inicial no existe en la tabla, o la tabla esta vacia,
    ///    devuelve false y no modifica `outUsb`.
    ///  - Sube por `parent` hasta encontrar un nodo con `isUsb == true` y lo
    ///    devuelve en `outUsb` (true).
    ///  - Si no hay ningun ancestro USB, devuelve el nodo MAS ALTO alcanzable
    ///    (el tope del arbol, el de parent vacio o sin padre conocido) en
    ///    `outUsb` (true).
    ///  - Proteccion contra ciclos: si se visita un nodo ya visto, se detiene
    ///    y devuelve el ultimo nodo visitado.
    ///
    /// :param startNodeId: id del devnode del endpoint (punto de partida).
    /// :param nodes: tabla de devnodes (arbol).
    /// :param outUsb: recibe el nodo USB (o el tope) en exito.
    /// :returns: true si se resuelvo un nodo; false si no hay tabla o el id no
    ///           existe.
    bool resolveUsbAncestor(const std::string& startNodeId, const std::vector<DevNode>& nodes,
                            DevNode& outUsb) const;
};

} // namespace ba::hw
