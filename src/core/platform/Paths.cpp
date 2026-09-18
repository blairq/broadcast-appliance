// ============================================================================
// src/core/platform/Paths.cpp — Logica comun de rutas (sin #ifdef)
// ============================================================================

#include "platform/Paths.hpp"

#include <cstdlib>
#include <string>

namespace ba::platform {

std::filesystem::path executableDir() {
    const std::filesystem::path exe = executablePath();
    return exe.empty() ? std::filesystem::path{} : exe.parent_path();
}

std::filesystem::path bundleRootFor(const std::filesystem::path& exeDir) {
    if (exeDir.empty()) {
        return exeDir;
    }
    // `lexically_normal` quita un separador final: sin eso `filename()` de
    // "/x/bin/64bit/" es vacio y no se reconoce el layout.
    const std::filesystem::path dir = exeDir.lexically_normal();
    const std::filesystem::path norm = dir.has_filename() ? dir : dir.parent_path();

    // <root>/bin/64bit/<exe>
    if (norm.filename() == "64bit" && norm.parent_path().filename() == "bin") {
        return norm.parent_path().parent_path();
    }
    // <X>.app/Contents/MacOS/<exe>  -> root = Contents
    if (norm.filename() == "MacOS" && norm.parent_path().filename() == "Contents") {
        return norm.parent_path();
    }
    // <root>/<exe> (launcher en la raiz del bundle)
    return norm;
}

std::filesystem::path bundleRoot() { return bundleRootFor(executableDir()); }

std::filesystem::path dataRoot() {
    const char* env = std::getenv(kDataRootEnv);
    if (env != nullptr && *env != '\0') {
        // El launcher la escribe en UTF-8 (C++20: path desde char8_t).
        const std::string s(env);
        return std::filesystem::path(std::u8string(s.begin(), s.end()));
    }
    return bundleRoot();
}

} // namespace ba::platform
