// main.cpp
//
// Punto de entrada del launcher (Proceso 0, spec 3.1). Win32 puro, subsistema
// GUI (sin consola), CRT estatica (/MT). Deriva todas las rutas de
// GetModuleFileNameW porque la letra de unidad cambia entre equipos (spec 5.3).
//
// Modelo de threading:
//   El thread principal inicializa el log, construye el Supervisor y llama a
//   run(), que ejecuta el ciclo de vida completo (ver Supervisor.hpp).
//
// Estrategia de errores:
//   Si no se puede determinar la raiz o el supervisor falla, se loguea y se
//   devuelve un codigo de salida distinto de 0. En no-Windows es un stub que
//   loguea "solo Windows" y devuelve 0 (el target se valida aca por compilacion).

#include "Log.hpp"
#include "Supervisor.hpp"

#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {
std::wstring moduleDir() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return L".";
    }
    std::wstring p(buf, n);
    const size_t slash = p.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
}

// Convierte una ruta UTF-16 a UTF-8 para Log::init (que toma std::string).
std::string toUtf8(const std::wstring& p) {
    if (p.empty()) {
        return std::string();
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, p.c_str(), static_cast<int>(p.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string out;
    if (n > 0) {
        out.resize(static_cast<std::size_t>(n));
        WideCharToMultiByte(CP_UTF8, 0, p.c_str(), static_cast<int>(p.size()),
                            out.data(), n, nullptr, nullptr);
    }
    return out;
}
} // namespace

/// Punto de entrada GUI del launcher.
///
/// :param hInstance: handle de instancia.
/// :param hPrevInstance: reservado.
/// :param lpCmdLine: argumentos.
/// :param nCmdShow: estado de visualizacion.
/// :returns: codigo de salida del proceso.
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    const std::wstring root = moduleDir();
    ba::launcher::Log::init(toUtf8(root + L"\\Logs\\launcher.log"));

    ba::launcher::Supervisor::Config cfg;
    cfg.root = root;
    cfg.appExe = root + L"\\bin\\64bit\\ba-app.exe";
    cfg.gatewayExe = root + L"\\bin\\64bit\\ba-gateway.exe";
    cfg.logsDir = root + L"\\Logs";

    ba::launcher::Supervisor sup(std::move(cfg));
    return sup.run();
}

#if defined(_WIN32) && !defined(_MSC_VER)
// MinGW GUI CRT espera `WinMain` (narrow) como entry point. El launcher
// implementa `wWinMain` (wide); este trampoline lo puentea.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)lpCmdLine;
    return wWinMain(hInstance, hPrevInstance, nullptr, nCmdShow);
}
#endif
#else
// Stub no-Windows: el launcher es Win32 puro; aca solo se valida la compilacion.
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    ba::launcher::Log::init("./Logs/launcher.log");
    BA_LOG_INFO("launcher", 0, "launcher es solo para Windows; nada que hacer en esta plataforma");
    return 0;
}
#endif
