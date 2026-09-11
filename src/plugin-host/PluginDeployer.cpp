// ============================================================================
// src/plugin-host/PluginDeployer.cpp — Paso de DEPLOY de los bundles LSP
// (spec D6/§7.5, LSP como componente CORE)
//
// Implementación de `ba::plugin::PluginDeployer::deploy()`:
//   1. Crea `installDir` si no existe.
//   2. Para cada bundle `.vst3` (directorio) directamente bajo `sourceDir`,
//      borra el destino anterior y lo copia recursivamente a `installDir`
//      (overwrite idempotente).
//   3. Escribe `installDir/manifest.json` como un array `ordered_json` de
//      `{name, bundlePath, dllPath}`, usando el escáner existente
//      `scanDirectory(installDir)` (misma definición de "bundle real" que el
//      host: directorio `.vst3` con subdirectorio `Contents/`).
//
// Puro C++ (std::filesystem + nlohmann/json), cross-platform, testeable en
// Linux. No depende del VST3 SDK.
//
// Política de errores: sin excepciones. Ante cualquier fallo de filesystem
// se avanza con el conteo de lo copiado y se escribe el manifest con lo que
// haya; nunca lanza.
// ============================================================================

#include "PluginDeployer.hpp"

#include "Vst3Host.hpp" // scanDirectory + BundleInfo (mismo módulo).

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ba {
namespace plugin {

namespace {

/// true si `p` es un DIRECTORIO cuyo nombre termina en `.vst3` (un bundle
/// candidato a copiar). No exige `Contents/`: eso lo decide después
/// `scanDirectory` al armar el manifest. No lanza.
bool isVst3BundleDir(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::is_directory(p, ec) || ec) {
        return false;
    }
    return p.filename().string().ends_with(".vst3");
}

} // namespace

DeployResult PluginDeployer::deploy(const std::string& sourceDir, const std::string& installDir) {
    DeployResult result;
    result.copied = 0;
    result.manifestPath = (std::filesystem::path(installDir) / "manifest.json").string();

    std::error_code ec;

    // 1) Crear el directorio de instalación si no existe (idempotente).
    std::filesystem::create_directories(installDir, ec);

    // 2) Copiar cada bundle `.vst3` (directorio) directamente bajo sourceDir.
    const std::filesystem::path srcRoot(sourceDir);
    const std::filesystem::path dstRoot(installDir);
    if (std::filesystem::is_directory(srcRoot, ec) && !ec) {
        for (const auto& entry : std::filesystem::directory_iterator(srcRoot, ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            const std::filesystem::path bundle = entry.path();
            if (!isVst3BundleDir(bundle)) {
                continue;
            }

            const std::filesystem::path dst = dstRoot / bundle.filename();
            // Idempotente: borrar el destino anterior (si existe) y copiar
            // de nuevo el bundle completo.
            std::error_code rec;
            std::filesystem::remove_all(dst, rec);
            std::filesystem::copy(bundle, dst,
                                  std::filesystem::copy_options::recursive |
                                      std::filesystem::copy_options::overwrite_existing,
                                  ec);
            if (ec) {
                std::fprintf(stderr, "[ba_plugin_host] deploy: fallo copiando %s\n", bundle.string().c_str());
                ec.clear();
                continue; // no contar este bundle.
            }
            ++result.copied;
        }
    } else if (ec) {
        std::fprintf(stderr, "[ba_plugin_host] deploy: no se pudo leer sourceDir=%s\n", sourceDir.c_str());
        ec.clear();
    }

    // 3) Escribir installDir/manifest.json con los bundles reales hallados.
    //    ordered_json => orden de inserción estable (determinista).
    const std::vector<BundleInfo> bundles = scanDirectory(installDir);
    nlohmann::ordered_json arr = nlohmann::ordered_json::array();
    for (const auto& b : bundles) {
        nlohmann::ordered_json obj;
        obj["name"] = b.name;
        obj["bundlePath"] = b.bundlePath;
        obj["dllPath"] = b.dllPath;
        arr.push_back(std::move(obj));
    }
    {
        std::ofstream out(result.manifestPath, std::ios::binary);
        if (out) {
            out << arr.dump(2) << "\n";
        } else {
            std::fprintf(stderr, "[ba_plugin_host] deploy: no se pudo escribir %s\n",
                         result.manifestPath.c_str());
        }
    }

    return result;
}

} // namespace plugin
} // namespace ba
