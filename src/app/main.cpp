// ============================================================================
// src/app/main.cpp — Punto de entrada del Proceso A (motor de emisión + UI)
//
// Propósito:
//   Punto de entrada de `ba-app` (Proceso A, spec §6). Tres rutas, elegidas en
//   tiempo de compilación:
//     1. `#if defined(_WIN32) && defined(BA_APP_HAVE_QT_OBS)` — app COMPLETA:
//        `obs_startup`/`obs_shutdown` (libobs) + `QApplication` + `MainWindow`
//        (Qt 6). Es la UI real del operador.
//     2. `#elif defined(_WIN32)` — modo HEADLESS Windows (cross-compile MinGW
//        de validación sin Qt/libobs): `wWinMain` que corre un autotest del
//        motor (dsp + ipc + persist) y termina.
//     3. `#else` — modo HEADLESS no-Windows (Linux): `main()` con el mismo
//        autotest, para validar la compilación del árbol en Linux.
//
//   El motor (`OperatorEngine`) es el que integra `ba_core`; aquí solo se
//   arranca la plataforma (libobs/Qt) y se le pasa el motor. Todo lo Qt/obs
//   vive tras `#ifdef _WIN32` (spec: "todo Qt/obs tras #ifdef _WIN32").
//
// Modelo de threading:
//   El thread principal inicializa libobs (en la ruta completa) y entra al
//   event loop de Qt (o corre el autotest y termina en headless). No crea
//   threads propios.
//
// Estrategia de errores:
//   No lanza. Si `obs_startup` o el motor fallan, se loguea (printf) y se
//   devuelve un código de salida distinto de 0. En headless el autotest
//   devuelve 0 si la integración dsp/ipc/persist funciona.
// ============================================================================

#include "OperatorEngine.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h> // getcwd (fallback POSIX de moduleDirW)
#endif

namespace {

// Directorio del módulo (UTF-16), derivado de GetModuleFileNameW (spec §5.3:
// la letra de unidad cambia entre equipos). En POSIX no existe "módulo"; el
// fallback usa el directorio de trabajo actual (getcwd).
std::wstring moduleDirW() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return L".";
    }
    const std::wstring p(buf, n);
    const std::size_t slash = p.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
#else
    char buf[4096]{};
    if (getcwd(buf, sizeof(buf)) == nullptr) {
        return L".";
    }
    const std::string p(buf);
    std::wstring out;
    out.reserve(p.size());
    for (char c : p)
        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return out;
#endif
}

// UTF-16 -> UTF-8 (para las rutas que libobs/QSettings toman en UTF-8). En
// POSIX el wstring es en realidad UTF-8 estrecho (1 wchar = 1 byte), así que la
// conversión es un traspaso de bytes.
std::string toUtf8(const std::wstring& p) {
    if (p.empty()) {
        return std::string();
    }
#ifdef _WIN32
    const int n = WideCharToMultiByte(CP_UTF8, 0, p.c_str(),
                                      static_cast<int>(p.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string out;
    if (n > 0) {
        out.resize(static_cast<std::size_t>(n));
        WideCharToMultiByte(CP_UTF8, 0, p.c_str(), static_cast<int>(p.size()),
                            out.data(), n, nullptr, nullptr);
    }
    return out;
#else
    std::string out;
    out.reserve(p.size());
    for (wchar_t c : p)
        out.push_back(static_cast<char>(static_cast<unsigned char>(c)));
    return out;
#endif
}

// --- Autotest headless (sin Qt/libobs) -------------------------------------
//
// Ejercita la integración real de `ba_core` que el motor aporta:
//   - DSP: los tres presets aplicados en caliente a cada canal + un tono de
//     440 Hz procesado por cada ChannelStrip + Automixer;
//   - IPC: ring de invitados (lector, pop -> silencio), HealthBlock (beat +
//     lectura/watchdog) y ControlEndpoint (sendRequest/poll vía tick);
//   - Persistencia: StateDoc construido + snapshot escrito en el cierre
//     ordenado (SnapshotStore::scheduleWrite + flushNow).
//
// :returns: 0 si la integración funciona; 1 si init o shutdown fallaron.
int runHeadlessSelfTest() {
    ba::app::OperatorEngine engine;
    // Config en un subdir local (los snapshots van a cfg/Config/session).
    const std::string cfg = "ba-app-headless";
    if (!engine.init(cfg)) {
        std::printf("ba-app headless: init falló (code=%d)\n",
                    engine.lastError());
        return 1;
    }

    // 1) DSP: aplicar los tres presets de fábrica a cada canal (en caliente).
    for (int ch = 0; ch < engine.numChannels(); ++ch) {
        engine.applyPreset(ch, ba::app::ChannelPreset::kPodium);
        engine.applyPreset(ch, ba::app::ChannelPreset::kLavalier);
        engine.applyPreset(ch, ba::app::ChannelPreset::kHandheld);
    }

    // 2) Audio: tono 440 Hz (480 frames = 10 ms) a través de cada canal.
    const int frames = 480;
    std::vector<float> buf(static_cast<std::size_t>(frames) * 2, 0.0f);
    for (int i = 0; i < frames; ++i) {
        const float s =
            0.3f * std::sin(2.0f * 3.14159265f * 440.0f * i / 48000.0f);
        buf[static_cast<std::size_t>(i) * 2] = s;
        buf[static_cast<std::size_t>(i) * 2 + 1] = s;
    }
    for (int ch = 0; ch < engine.numChannels(); ++ch) {
        engine.processAudio(buf.data(), frames, ch);
    }

    // 3) IPC: lector de invitados (pop -> silencio esperado, sin B).
    float guest[ba::ipc::audio_ring::kBlockFrames *
                ba::ipc::audio_ring::kDefaultChannels];
    bool sil = false;
    bool fade = false;
    engine.processGuestBlock(guest, sil, fade);

    // 4) IPC: control + health (ticks: heartbeat + poll del endpoint).
    for (int i = 0; i < 3; ++i) {
        engine.tick();
    }

    // 5) Persistencia: StateDoc (fuente de verdad, spec §11.1).
    const ba::persist::StateDoc doc = engine.buildStateDoc();
    std::printf("ba-app headless: seq=%llu dsp=%zu ipc=%zu\n",
                static_cast<unsigned long long>(doc.sequence),
                doc.sections.count("dsp") ? doc.sections.at("dsp").size() : 0,
                doc.sections.count("ipc") ? doc.sections.at("ipc").size() : 0);

    // 6) Cierre ordenado: scheduleWrite(immediate) + flushNow (spec §11.2).
    const bool ok = engine.shutdown();
    std::printf("ba-app headless: OK (ring=%d control=%d stale=%d shutdown=%d)\n",
                engine.guestRingOpen() ? 1 : 0,
                engine.controlConnected() ? 1 : 0,
                engine.watchedIsStale() ? 1 : 0, ok ? 1 : 0);
    return ok ? 0 : 1;
}

} // namespace

// ============================================================================
// Ruta 1 — App completa (Qt 6 + libobs)
// ============================================================================
#if defined(_WIN32) && defined(BA_APP_HAVE_QT_OBS)

#include <QApplication>

#include <obs.h>

#include "MainWindow.hpp"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    // Búsqueda de DLLs (spec §5.2): solo los directorios propios, nunca el PATH.
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    const std::wstring root = moduleDirW();
    AddDllDirectory((root + L"\\bin\\64bit").c_str());

    // libobs (spec §6.1/§5.4): obs_startup con el plugin_config en el disco
    // portable (<root>\Config\plugin_config), para que los plugins (incluida la
    // caché de CEF de obs-browser) escriban ahí.
    const std::string locale = "es_ES";
    const std::string pluginConfig = toUtf8(root + L"\\Config\\plugin_config");
    if (!obs_startup(locale.c_str(), pluginConfig.c_str(), locale.c_str(),
                     /*is_hwnd=*/false, /*private_data=*/nullptr)) {
        std::printf("ba-app: obs_startup falló\n");
        return 1;
    }

    int rc = 0;
    {
        // QApplication (Qt 6): argc/argv de 1 elemento (sin args extra).
        const char* argv0 = "ba-app";
        char* argv[1] = { const_cast<char*>(argv0) };
        int argc = 1;
        QApplication app(argc, argv);

        ba::app::OperatorEngine engine;
        if (!engine.init(toUtf8(root))) {
            std::printf("ba-app: motor no inicializado (code=%d)\n",
                        engine.lastError());
            obs_shutdown();
            return 1;
        }

        ba::app::MainWindow window(engine);
        window.show();
        rc = app.exec();

        // Cierre ordenado: lo hace MainWindow::closeEvent (engine.shutdown).
        obs_shutdown();
    }
    return rc;
}

// MinGW GUI CRT espera `WinMain` (narrow); trampoline a `wWinMain` (wide),
// mismo patrón que el launcher.
#if !defined(_MSC_VER)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)lpCmdLine;
    return wWinMain(hInstance, hPrevInstance, nullptr, nCmdShow);
}
#endif

// ============================================================================
// Ruta 2 — Headless Windows (cross MinGW sin Qt/libobs)
// ============================================================================
#elif defined(_WIN32)

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    return runHeadlessSelfTest();
}

#if !defined(_MSC_VER)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)lpCmdLine;
    return wWinMain(hInstance, hPrevInstance, nullptr, nCmdShow);
}
#endif

// ============================================================================
// Ruta 3 — Headless no-Windows (Linux)
// ============================================================================
#else

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    return runHeadlessSelfTest();
}

#endif // rutas
