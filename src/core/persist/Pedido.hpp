// ============================================================================
// src/core/persist/Pedido.hpp — cómo se escribe lo que se encola
//
// Lo que dice quien guarda (ArchivoConfig.hpp, ColaEscritura.hpp): con qué
// durabilidad, con qué prioridad y con qué permisos. Aparte para que pedir un
// guardado no arrastre la cola ni el escritor.
//
// Sin Qt (ba_core).
// ============================================================================
#pragma once

#include <functional>

#include "persist/Durabilidad.hpp"

namespace ba::persist {

enum class Prioridad {
    Normal, ///< ajustes
    Aire,   ///< el estado de aire: primero
};

/// Cómo se escribe lo encolado.
struct Pedido {
    Durabilidad durabilidad = Durabilidad::Normal;
    Prioridad prioridad = Prioridad::Normal;
    Permisos permisos = Permisos::Comunes;
    /// Corre en el hilo escritor justo antes de escribir (p. ej. guardar la versión
    /// anterior del archivo). Con la fusión, rige el del último pedido.
    std::function<void()> antes;
};

} // namespace ba::persist
