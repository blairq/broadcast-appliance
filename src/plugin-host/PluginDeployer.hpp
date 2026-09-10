// ============================================================================
// src/plugin-host/PluginDeployer.hpp — Paso de DEPLOY de los bundles LSP
// (spec D6/§7.5, LSP como componente CORE)
//
// `ba::plugin::PluginDeployer` es el paso de despliegue que pobla
// `AudioPlugins/` con los bundles `.vst3` de LSP (versión fijada
// `LSP_VERSION`, ver versions.cmake) y genera el `manifest.json` que el host
// (`Vst3Host` / `scanDirectory`) consume para descubrirlos.
//
// Puro C++ (std::filesystem + nlohmann/json ordered_json), cross-platform y
// testeable en Linux: no depende del VST3 SDK ni de Windows.
//
// Modelo de hilos:
//   - `deploy()` es CONTROL PATH: se invoca una vez, fuera de la ruta de
//     audio (p. ej. al arrancar el Proceso A o desde `tools/deploy`). No hay
//     restricciones de tiempo real; hace IO de disco (copia recursiva +
//     escritura del manifest).
//
// Política de errores:
//   - `deploy()` NO lanza. Ante cualquier fallo de filesystem (fuente
//     inexistible, copia fallida, escritura del manifest fallida) devuelve un
//     `DeployResult` con el conteo de lo que sí pudo copiar y la ruta del
//     manifest; nunca deja el proceso caído. Un `sourceDir` inexistible o sin
//     bundles produce `copied == 0` y un `manifest.json` de array vacío.
//   - La copia es IDEMPOTENTE: re-ejecutar `deploy()` sobre el mismo
//     `installDir` borra y re-copia cada bundle y reescribe el manifest,
//     dejando el estado final idéntico.
// ============================================================================

#pragma once

#include <string>

namespace ba {
namespace plugin {

/// Resultado de un `PluginDeployer::deploy()`.
///
/// :copied: cantidad de bundles `.vst3` copiados desde `sourceDir` a
///          `installDir` (0 si no había ninguno o la fuente no existía).
/// :manifestPath: ruta del `manifest.json` escrito en `installDir`.
struct DeployResult {
    int copied = 0;
    std::string manifestPath;
};

/// Paso de despliegue de los bundles LSP: copia los `.vst3` y escribe el
/// `manifest.json` que el host consume.
///
/// Uso típico (control path, una vez, con el grafo de audio detenido):
///   auto res = PluginDeployer::deploy("lsp-src", "AudioPlugins");
///   // res.copied == N, res.manifestPath == "AudioPlugins/manifest.json"
///
/// `deploy()` es idempotente: re-ejecutarlo sobre el mismo `installDir`
/// produce el mismo estado final.
class PluginDeployer {
public:
    /// Copia cada bundle `.vst3` (un DIRECTORIO) directamente bajo
    /// `sourceDir` hacia `installDir` (recursivo, overwrite idempotente) y
    /// escribe `installDir/manifest.json` con los bundles reales hallados.
    ///
    /// Control path: hace IO de disco; no llamar desde la ruta de audio.
    /// No lanza: ante cualquier fallo de filesystem devuelve el conteo de lo
    /// copiado y la ruta del manifest (ver docstring de módulo).
    ///
    /// :param sourceDir: directorio origen con los bundles `.vst3` de LSP.
    /// :param installDir: directorio destino (se crea si no existe).
    /// :returns: `DeployResult` con `copied` (bundles copiados) y
    ///           `manifestPath` (ruta del `manifest.json` escrito).
    static DeployResult deploy(const std::string& sourceDir,
                               const std::string& installDir);
};

} // namespace plugin
} // namespace ba
