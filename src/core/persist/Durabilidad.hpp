// ============================================================================
// src/core/persist/Durabilidad.hpp — cuánto aguanta lo escrito y quién lo lee
//
// Los dos enums que usan el escritor (Escritor.hpp), la cola (Pedido.hpp) y quien
// guarda por ArchivoConfig.hpp. Aparte para que nombrarlos no arrastre el escritor.
//
// Sin Qt (ba_core).
// ============================================================================
#pragma once

namespace ba::persist {

/// Cuánto se asegura que lo escrito sobreviva a un corte de luz.
enum class Durabilidad {
    Normal,  ///< ajustes: fsync del archivo y de la carpeta
    Critica, ///< el estado de aire: además vacía la caché del disco (F_FULLFSYNC en macOS)
};

/// Quién puede leer el archivo.
enum class Permisos {
    Comunes,  ///< los de siempre (0644 en POSIX)
    Privados, ///< solo el dueño (0600 en POSIX; en Windows rige la ACL del perfil)
};

} // namespace ba::persist
