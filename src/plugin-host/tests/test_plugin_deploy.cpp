// ============================================================================
// src/plugin-host/tests/test_plugin_deploy.cpp — unit test de
// `PluginDeployer::deploy` (paso de deploy de los bundles LSP)
//
// Verifica el deploy en LINUX (puro C++, sin SDK):
//   1. Un directorio temporal de origen con un bundle FAKE
//      `Bar.vst3/Contents/x86_64-win/Bar.dll` (un byte dummy) debe:
//        - copiar EXACTAMENTE un bundle (`copied == 1`),
//        - crear `installDir/manifest.json` cuyo texto contenga `"Bar"`,
//        - dejar en `installDir` un bundle real que `scanDirectory` resuelva
//          como EXACTAMENTE uno nombrado "Bar".
//   2. Idempotencia: re-ejecutar deploy() sobre el mismo installDir deja
//      `copied == 1` y el manifest sigue conteniendo "Bar".
//
// Sin framework: main() devuelve 0 si pasa y 1 si falla (patrón src/core).
// ============================================================================

#include "PluginDeployer.hpp"
#include "Vst3Host.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace ba::plugin;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (cond) {
        std::printf("  ok: %s\n", what);
    } else {
        std::printf("  FAIL: %s\n", what);
        ++failures;
    }
}

std::filesystem::path makeTempDir(const std::string& tag) {
    auto base = std::filesystem::temp_directory_path();
    auto dir = base / ("ba_plugin_deploy_test_" + tag);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

/// Crea un bundle FAKE `Bar.vst3/Contents/x86_64-win/Bar.dll` (byte dummy)
/// dentro de `root`.
void makeFakeBundle(const std::filesystem::path& root) {
    const auto bundle = root / "Bar.vst3";
    const auto dllDir = bundle / "Contents" / "x86_64-win";
    std::error_code ec;
    std::filesystem::create_directories(dllDir, ec);
    std::ofstream f(dllDir / "Bar.dll", std::ios::binary);
    f.put('X'); // byte dummy: el escáner no lee el contenido.
}

std::string readText(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    const auto source = makeTempDir("src");
    const auto install = makeTempDir("install");
    makeFakeBundle(source);

    // --- Caso 1: deploy de un bundle fake -----------------------------------
    {
        const DeployResult res = PluginDeployer::deploy(source.string(), install.string());

        check(res.copied == 1, "se copió exactamente un bundle");

        const std::filesystem::path manifest = install / "manifest.json";
        check(std::filesystem::exists(manifest), "manifest.json existe en installDir");

        const std::string text = readText(manifest);
        check(text.find("\"Bar\"") != std::string::npos, "el manifest contiene \"Bar\"");

        const auto bundles = scanDirectory(install.string());
        check(bundles.size() == 1, "scanDirectory(install) encuentra exactamente un bundle");
        if (bundles.size() == 1) {
            check(bundles[0].name == "Bar", "el bundle se llama \"Bar\"");
            check(bundles[0].bundlePath == (install / "Bar.vst3").string(),
                  "bundlePath apunta al bundle copiado");
        }

        // El resultado debe reportar la ruta del manifest escrito.
        check(res.manifestPath == manifest.string(), "DeployResult.manifestPath apunta al manifest.json");
    }

    // --- Caso 2: idempotencia (re-deploy sobre el mismo installDir) ---------
    {
        const DeployResult res2 = PluginDeployer::deploy(source.string(), install.string());
        check(res2.copied == 1, "re-deploy: se copió exactamente un bundle");
        const std::string text = readText(install / "manifest.json");
        check(text.find("\"Bar\"") != std::string::npos, "re-deploy: el manifest sigue conteniendo \"Bar\"");
        const auto bundles = scanDirectory(install.string());
        check(bundles.size() == 1, "re-deploy: sigue habiendo exactamente un bundle");
    }

    // Limpieza.
    std::error_code ec;
    std::filesystem::remove_all(source, ec);
    std::filesystem::remove_all(install, ec);

    if (failures == 0) {
        std::printf("test_plugin_deploy: PASS\n");
        return 0;
    }
    std::printf("test_plugin_deploy: FAIL (%d)\n", failures);
    return 1;
}
