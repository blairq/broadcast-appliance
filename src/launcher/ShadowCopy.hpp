// ShadowCopy.hpp
//
// Proposito:
//   La copia sombra (spec 3.1.5, decision D3): copia bin/ y data/ a
//   %TEMP%\BroadcastAppliance\<hash>\ y ejecuta desde alli, para que un
//   movimiento del cable USB no mate los procesos con EXCEPTION_IN_PAGE_ERROR.
//   La implementacion esta en ShadowCopy.cpp.
//
// Modelo de threading:
//   prepare() se invoca desde el thread principal ANTES de lanzar hijos.
//
// Estrategia de errores:
//   Nunca lanza: ante cualquier fallo loguea y prepare() devuelve la raiz
//   original (se ejecuta desde el disco).

#pragma once

#include <string>

namespace ba::launcher {

/// La copia sombra de los binarios de la version.
class ShadowCopy {
   public:
    /// Prepara la copia sombra y devuelve la raiz desde donde ejecutar.
    ///
    /// :param root: raiz del disco de origen.
    /// :param versionHash: hash de version para el subdirectorio.
    /// :returns: raiz efectiva desde donde lanzar (la sombra, o `root` si no se pudo).
    static std::wstring prepare(const std::wstring& root, const std::wstring& versionHash);

    /// Clave de version del CONTENIDO de bin/ y data/: hash de
    /// (ruta relativa, tamaño, fecha de modificacion) de cada archivo. Cambia
    /// en cuanto se actualiza cualquier binario, asi que una copia sombra de
    /// otra version nunca se reutiliza por error. Cuesta un `stat` por archivo.
    static std::wstring versionKey(const std::wstring& root);

    /// Borra la copia sombra en el cierre ordenado.
    static void cleanup(const std::wstring& shadowRoot);
};

} // namespace ba::launcher
