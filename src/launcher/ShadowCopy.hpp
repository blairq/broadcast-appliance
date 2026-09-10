// ShadowCopy.hpp
//
// Proposito:
//   INTERFAZ de la copia sombra (spec 3.1.5, decision D3): copia bin/,
//   obs-plugins/ y data/ a %TEMP%\BroadcastAppliance\<hash>\ y ejecuta desde
//   alli, para que un movimiento del cable USB no mate los procesos con
//   EXCEPTION_IN_PAGE_ERROR. Esta tarea (Fase 1, parte 1) SOLO declara la
//   interfaz; la implementacion queda para una fase posterior.
//
// Modelo de threading:
//   prepare() se invoca desde el thread principal ANTES de lanzar hijos.
//
// Estrategia de errores:
//   prepare() devuelve bool y loguea. En esta fase es un no-op documentado que
//   devuelve true (la copia no se realiza; se ejecuta desde la raiz).

#pragma once

#include <string>

namespace ba::launcher {

/// Interfaz de la copia sombra (stub en Fase 1).
class ShadowCopy {
public:
    /// Prepara la copia sombra y devuelve la raiz desde donde ejecutar.
    ///
    /// :param root: raiz del disco de origen.
    /// :param versionHash: hash de version para el subdirectorio.
    /// :returns: raiz efectiva desde donde lanzar (en esta fase, `root`).
    static std::wstring prepare(const std::wstring& root, const std::wstring& versionHash);

    /// Borra la copia sombra en el cierre ordenado. No-op en esta fase.
    static void cleanup(const std::wstring& shadowRoot);
};

} // namespace ba::launcher
