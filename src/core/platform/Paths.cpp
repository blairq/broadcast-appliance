// ============================================================================
// src/core/platform/Paths.cpp — Logica comun de rutas (sin #ifdef)
// ============================================================================

#include "platform/Paths.hpp"

#include <cstdlib>
#ifndef _WIN32
#include <stdlib.h>
#endif
#include <string>
#include <system_error>

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
    // <X>.app/Contents/MacOS/<exe>  -> root = Contents/Resources. El layout va
    // en Resources porque en Contents/ solo pueden ir las carpetas estandar de
    // una .app: cualquier otra cosa hace que codesign la rechace.
    if (norm.filename() == "MacOS" && norm.parent_path().filename() == "Contents") {
        return norm.parent_path() / "Resources";
    }
    // <root>/<exe> (launcher en la raiz del bundle)
    return norm;
}

std::filesystem::path bundleRoot() { return bundleRootFor(executableDir()); }

std::filesystem::path dataRootFor(const std::filesystem::path& bundle) {
    if (bundle.empty()) return bundle;
    const std::filesystem::path b = bundle.lexically_normal();
    const std::filesystem::path norm = b.has_filename() ? b : b.parent_path();
    // <X>.app/Contents/Resources -> <dir de la .app>/<X> Datos. Lo que se
    // escribe adentro de la .app rompe el sello de su firma: macOS la vuelve a
    // verificar entera (varios GB) y no reconoce su identidad para los
    // permisos de cámara y micrófono.
    if (norm.filename() == "Resources" && norm.parent_path().filename() == "Contents") {
        const std::filesystem::path app = norm.parent_path().parent_path();
        if (app.extension() == ".app") return app.parent_path() / (app.stem().string() + " Datos");
    }
    return norm;
}

std::filesystem::path dataRoot() {
    const char* env = std::getenv(kDataRootEnv);
    if (env != nullptr && *env != '\0') {
        // El launcher la escribe en UTF-8 (C++20: path desde char8_t).
        const std::string s(env);
        return std::filesystem::path(std::u8string(s.begin(), s.end()));
    }
    return dataRootFor(bundleRoot());
}

namespace {
std::filesystem::path makeDir(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    if (ec) return {};
    std::filesystem::permissions(p, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace,
                                 ec);
    return p; // sin permisos (FAT): igual sirve
}

void setEnv(const char* name, const std::filesystem::path& value) {
    const std::u8string u = value.u8string();
    const std::string s(u.begin(), u.end());
#ifdef _WIN32
    _putenv_s(name, s.c_str());
#else
    ::setenv(name, s.c_str(), 1);
#endif
}
} // namespace

std::filesystem::path stateDir() {
    const std::filesystem::path root = dataRoot();
    return root.empty() ? std::filesystem::path{} : makeDir(root / "Runtime");
}

std::filesystem::path cacheDir() {
    const std::filesystem::path root = dataRoot();
    return root.empty() ? std::filesystem::path{} : makeDir(root / "Cache");
}

std::filesystem::path credentialsDir() {
    const std::filesystem::path root = dataRoot();
    return root.empty() ? std::filesystem::path{} : makeDir(root / "Credenciales");
}

void redirectUserDirs() {
    const std::filesystem::path cache = cacheDir();
    if (cache.empty()) return;
    setEnv("BA_CACHE_DIR", cache);
    setEnv("XDG_CACHE_HOME", cache);                 // Qt, fontconfig, Mesa (Linux)
    setEnv("QML_DISK_CACHE_PATH", cache / "qml");    // QML compilado (todas las plataformas)
    setEnv("MESA_SHADER_CACHE_DIR", cache / "mesa"); // por si XDG no alcanza
    setEnv("__GL_SHADER_DISK_CACHE_PATH", cache / "nvidia");
}

std::string rutaGuardable(const std::filesystem::path& p) {
    if (p.empty()) return {};
    std::error_code ec;
    const auto raiz = std::filesystem::weakly_canonical(dataRoot(), ec);
    const auto abs = std::filesystem::weakly_canonical(p, ec);
    const auto rel = abs.lexically_relative(raiz);
    if (!ec && !rel.empty() && *rel.begin() != "..") {
        const std::u8string u = rel.generic_u8string();
        return std::string(u.begin(), u.end());
    }
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::filesystem::path rutaDeDatos(const std::string& guardada) {
    if (guardada.empty()) return {};
    const std::filesystem::path p(std::u8string(guardada.begin(), guardada.end()));
    if (p.is_relative()) return dataRoot() / p;
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) return p;
    // La carpeta de datos se movió: desde la primera carpeta conocida de los datos.
    static const char* const kCarpetas[] = {"Modelos", "Media", "Graficos", "Eventos", "Recordings", "Plugins",
                                            "Herramientas", "Miniaturas", "Fotos", "Shares"};
    std::filesystem::path resto;
    bool desde = false;
    for (const auto& parte : p) {
        if (!desde)
            for (const char* c : kCarpetas)
                if (parte == c) desde = true;
        if (desde) resto /= parte;
    }
    if (desde) {
        const auto nueva = dataRoot() / resto;
        if (std::filesystem::exists(nueva, ec)) return nueva;
    }
    return p;
}

} // namespace ba::platform
