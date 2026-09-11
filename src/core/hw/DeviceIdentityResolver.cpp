// DeviceIdentityResolver.cpp
//
// Implementacion del walk del arbol de devnodes (spec 9.2). Modela el
// `CM_Get_Parent` de cfgmgr32 sobre una tabla abstracta de nodos para que el
// algoritmo sea testeable en Linux. Ver DeviceIdentityResolver.hpp.

#include "hw/DeviceIdentityResolver.hpp"

#include <unordered_set>

namespace ba::hw {

namespace {

/// Busca el nodo con `id` en `nodes`; devuelve el indice o -1 si no existe.
int findNodeIndex(const std::string& id, const std::vector<DevNode>& nodes) {
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

bool DeviceIdentityResolver::resolveUsbAncestor(const std::string& startNodeId,
                                                const std::vector<DevNode>& nodes, DevNode& outUsb) const {
    // Tabla vacia o id inicial desconocido: fallo.
    if (nodes.empty()) {
        return false;
    }
    int cur = findNodeIndex(startNodeId, nodes);
    if (cur < 0) {
        return false;
    }

    // Sube por el arbol. `topmost` guarda el nodo mas alto alcanzado hasta el
    // momento (por si no hay USB). `visited` protege contra ciclos.
    DevNode topmost = nodes[cur];
    std::unordered_set<std::string> visited;
    visited.insert(nodes[cur].id);

    while (true) {
        // Si el nodo actual es USB, ese es el dispositivo fisico: listo.
        if (nodes[cur].isUsb) {
            outUsb = nodes[cur];
            return true;
        }
        // Sin padre (raiz) o padre desconocido: se llego al tope.
        const std::string& parent = nodes[cur].parent;
        if (parent.empty()) {
            outUsb = topmost;
            return true;
        }
        int next = findNodeIndex(parent, nodes);
        if (next < 0) {
            // El padre no esta en la tabla: el nodo actual es el tope.
            outUsb = topmost;
            return true;
        }
        // Ciclo: el padre ya se visito. Se detiene en el ultimo visitado.
        if (visited.count(nodes[next].id) != 0) {
            outUsb = topmost;
            return true;
        }
        visited.insert(nodes[next].id);
        topmost = nodes[next];
        cur = next;
    }
}

} // namespace ba::hw
