// src/app/audio/LspDsp.cpp — ver el .hpp
#include "audio/LspDsp.hpp"

#include "platform/Paths.hpp"

#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

namespace ba::app::audio::lspdsp {

namespace {

#if defined(_WIN32)
constexpr const char* kNombre = "ba-lspdsp.dll";
#elif defined(__APPLE__)
constexpr const char* kNombre = "libba-lspdsp.dylib";
#else
constexpr const char* kNombre = "libba-lspdsp.so";
#endif

std::once_flag g_una;
Api g_api{};
bool g_ok = false;
std::string g_error;
std::string g_ruta;

void* abrir(const fs::path& p) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryW(p.wstring().c_str()));
#else
    return dlopen(p.string().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* simbolo(void* lib, const char* nombre) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), nombre));
#else
    return dlsym(lib, nombre);
#endif
}

void cargar() {
    std::vector<fs::path> candidatas;
    if (const char* env = std::getenv("BA_LSPDSP")) candidatas.emplace_back(env);
    candidatas.push_back(ba::platform::bundleRoot() / "bin" / "64bit" / kNombre);
    candidatas.push_back(ba::platform::executableDir() / kNombre);
#ifdef BA_DEV_LSPDSP
    candidatas.push_back(fs::path(BA_DEV_LSPDSP) / kNombre); // árbol de desarrollo
#endif
    void* lib = nullptr;
    for (const auto& p : candidatas) {
        std::error_code ec;
        if (!fs::exists(p, ec)) continue;
        lib = abrir(p);
        if (lib != nullptr) {
            g_ruta = p.string();
            break;
        }
    }
    if (lib == nullptr) {
        g_error = std::string("no está ") + kNombre + " (tools/deps/lspdsp.sh)";
        return;
    }
    bool falta = false;
    std::string cual;
    const auto res = [&](auto& destino, const char* nombre) {
        void* s = simbolo(lib, nombre);
        if (s == nullptr) {
            falta = true;
            if (cual.empty()) cual = nombre;
        }
        destino = reinterpret_cast<std::remove_reference_t<decltype(destino)>>(s);
    };
    res(g_api.version, "ba_lspdsp_version");
    res(g_api.versionLsp, "ba_lspdsp_version_lsp");
    res(g_api.medidoresCrear, "ba_medidores_crear");
    res(g_api.medidoresProcesar, "ba_medidores_procesar");
    res(g_api.medidoresLeer, "ba_medidores_leer");
    res(g_api.medidoresReiniciarIntegrado, "ba_medidores_reiniciar_integrado");
    res(g_api.medidoresLiberar, "ba_medidores_liberar");
    res(g_api.analizadorCrear, "ba_analizador_crear");
    res(g_api.analizadorConfigurar, "ba_analizador_configurar");
    res(g_api.analizadorProcesar, "ba_analizador_procesar");
    res(g_api.analizadorLeer, "ba_analizador_leer");
    res(g_api.analizadorLiberar, "ba_analizador_liberar");
    res(g_api.ruidoCrear, "ba_ruido_crear");
    res(g_api.ruidoGenerar, "ba_ruido_generar");
    res(g_api.ruidoLiberar, "ba_ruido_liberar");
    res(g_api.latenciaCrear, "ba_latencia_crear");
    res(g_api.latenciaArrancar, "ba_latencia_arrancar");
    res(g_api.latenciaProcesar, "ba_latencia_procesar");
    res(g_api.latenciaResultado, "ba_latencia_resultado");
    res(g_api.latenciaLiberar, "ba_latencia_liberar");
    if (falta) {
        g_error = g_ruta + " no tiene " + cual + " (¿de otra versión?)";
        return;
    }
    if (g_api.version() != BA_LSPDSP_VERSION) {
        g_error = g_ruta + " es de la versión " + std::to_string(g_api.version()) + " de la API (se esperaba " +
                  std::to_string(BA_LSPDSP_VERSION) + ")";
        return;
    }
    g_ok = true;
}

} // namespace

const Api* api() {
    std::call_once(g_una, cargar);
    return g_ok ? &g_api : nullptr;
}

std::string error() {
    std::call_once(g_una, cargar);
    return g_error;
}

std::string ruta() {
    std::call_once(g_una, cargar);
    return g_ruta;
}

} // namespace ba::app::audio::lspdsp
