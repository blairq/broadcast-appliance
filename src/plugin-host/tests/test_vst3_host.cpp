// ============================================================================
// src/plugin-host/tests/test_vst3_host.cpp — unit test de `scanDirectory`
//
// Verifica el escáner de bundles VST3 (puro C++, cross-platform) en LINUX:
//   1. Un directorio temporal con un bundle FAKE `Foo.vst3/Contents/
//      x86_64-win/Foo.dll` (un byte dummy) debe producir EXACTAMENTE un
//      bundle nombrado "Foo", con bundlePath correcto y dllPath no vacío.
//   2. Un directorio temporal vacío debe producir una lista vacía.
//
// Sin framework: main() devuelve 0 si pasa y 1 si falla (patrón src/core).
// ============================================================================

#include "Vst3Host.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

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
    auto dir = base / ("ba_plugin_host_test_" + tag);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

} // namespace

int main() {
    // --- Caso 1: bundle fake Foo.vst3 ---------------------------------------
    {
        const auto root = makeTempDir("bundle");
        const auto bundle = root / "Foo.vst3";
        const auto dllDir = bundle / "Contents" / "x86_64-win";
        std::error_code ec;
        std::filesystem::create_directories(dllDir, ec);
        {
            std::ofstream f(dllDir / "Foo.dll", std::ios::binary);
            f.put('X'); // byte dummy: el escáner no lee el contenido.
        }
        // Ruido que debe ser ignorado: archivo suelto y directorio sin
        // Contents/ (no es un bundle real).
        {
            std::ofstream f(root / "notabundle.txt", std::ios::binary);
            f.put('Y');
        }
        std::filesystem::create_directories(root / "Bar.vst3", ec);
        // (Bar.vst3 sin Contents/ => descartado.)

        const auto bundles = scanDirectory(root.string());

        check(bundles.size() == 1, "se encontró exactamente un bundle");
        if (bundles.size() == 1) {
            check(bundles[0].name == "Foo", "el bundle se llama \"Foo\"");
            check(bundles[0].bundlePath == bundle.string(), "bundlePath apunta al directorio del bundle");
            check(!bundles[0].dllPath.empty(), "dllPath no está vacío (Foo.dll resuelto)");
        }

        std::error_code ec2;
        std::filesystem::remove_all(root, ec2);
    }

    // --- Caso 2: directorio vacío => lista vacía ----------------------------
    {
        const auto root = makeTempDir("empty");
        const auto bundles = scanDirectory(root.string());
        check(bundles.empty(), "directorio vacío => lista vacía");

        std::error_code ec2;
        std::filesystem::remove_all(root, ec2);
    }

    // --- Caso 3: root inexistible => lista vacía (no lanza) -----------------
    {
        const auto bundles = scanDirectory("/no/existe/para/ba_plugin_host_test");
        check(bundles.empty(), "root inexistible => lista vacía");
    }

    if (failures == 0) {
        std::printf("test_vst3_host: PASS\n");
        return 0;
    }
    std::printf("test_vst3_host: FAIL (%d)\n", failures);
    return 1;
}
