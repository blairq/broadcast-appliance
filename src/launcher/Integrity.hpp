// Integrity.hpp
//
// Proposito:
//   Verificacion de integridad del launcher (spec 3.1.2): comprueba los
//   archivos del disco contra manifest.json (SHA-256) con verificacion
//   paralela. Si un archivo esta corrupto o ausente, se reporta con un
//   mensaje claro en lugar de crashear.
//
// Modelo de threading:
//   verify() es el unico punto de entrada y se invoca desde el thread
//   principal ANTES de lanzar hijos. Internamente reparte los archivos entre
//   N workers (std::thread) que hashean en paralelo; cada worker usa su propia
//   instancia de Sha256 (no se comparten). Los resultados se agregan bajo una
//   mutex de resultado.
//
// Estrategia de errores:
//   No lanza. Un archivo ausente o un hash que no coincide produce una
//   entrada en el reporte (no una excepcion). verify() devuelve un Report con
//   la lista de fallos; el caller decide si es fatal (spec: "mensaje claro en
//   lugar de crashear").

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ba::launcher {

/// Resultado de la verificacion de integridad.
struct IntegrityReport {
    bool ok = true;                     // true si no hubo fallos.
    std::size_t checked = 0;            // archivos verificados.
    std::size_t missing = 0;            // archivos ausentes.
    std::size_t mismatch = 0;           // hashes que no coinciden.
    std::vector<std::wstring> failures; // archivos con problema (ausente o mismatch).
};

/// Verificador de integridad basado en manifest.json (SHA-256).
class Integrity {
   public:
    /// Carga el manifest y verifica los archivos en paralelo.
    ///
    /// :param root: raiz del disco (donde vive manifest.json y los binarios).
    /// :param workers: cantidad de workers de hashing (0 => auto por nucleos).
    /// :returns: Report con los fallos; ok=false si hay alguno.
    static IntegrityReport verify(const std::wstring& root, int workers = 0);

    /// Verifica un solo archivo contra un hash esperado (hex, 64 chars).
    ///
    /// :param path: ruta del archivo.
    /// :param expectedHex: hash SHA-256 esperado en hex.
    /// :returns: 0 si coincide; -1 si el archivo no existe; -2 si el hash
    ///           difiere.
    static int verifyOne(const std::wstring& path, const std::string& expectedHex);
};

} // namespace ba::launcher
