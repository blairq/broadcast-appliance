// ============================================================================
// src/app/main.cpp — Punto de entrada de ba-app (Proceso A)
//
// Un solo despacho para todas las plataformas (solo el ENTRY POINT difiere:
// wWinMain en Windows, main en el resto):
//
//   --headless-selftest  autotest de OperatorEngine (sin libobs)
//   --engine-selftest    autotest del motor de emision (libobs)
//   --no-ui              modo consola aunque haya pantalla
//   (default)            interfaz grafica si hay Qt y pantalla; si no, consola
//
// "Hay pantalla": Windows y macOS siempre; Linux si hay WAYLAND_DISPLAY o
// DISPLAY (por SSH no hay y se cae a la consola, que avisa si el motor no
// puede arrancar sin contexto grafico).
// ============================================================================

#include "diag/DiarioDeArranque.hpp"
#include "emision/OperatorEngine.hpp"
#include "platform/Herramientas.hpp"
#include "platform/Paths.hpp"
#include "arranque/Registrar.hpp"
#include "security/CredentialStore.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// shellapi.h necesita windows.h antes (bloque aparte: clang-format no lo reordena).
#include <shellapi.h>
#else
#include <sys/resource.h>
#endif

#if BA_HAVE_MOTOR
#include "emision/EngineSelfTest.hpp"
#include "arranque/OperateMode.hpp"
#endif
#if BA_APP_HAVE_GUI
#include "arranque/GuiMode.hpp"
#endif

namespace {

int runHeadlessSelfTest(bool restore = false) {
    ba::app::OperatorEngine engine;
    // Config en un subdir local (los snapshots van a cfg/Config/session).
    const std::string cfg = "ba-app-headless";
    if (!engine.init(cfg, restore)) {
        std::printf("ba-app headless: init falló (code=%d)\n", engine.lastError());
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
        const float s = 0.3f * std::sin(2.0f * 3.14159265f * 440.0f * i / 48000.0f);
        buf[static_cast<std::size_t>(i) * 2] = s;
        buf[static_cast<std::size_t>(i) * 2 + 1] = s;
    }
    for (int ch = 0; ch < engine.numChannels(); ++ch) {
        engine.processAudio(buf.data(), frames, ch);
    }

    // 3) IPC: lector de invitados (pop -> silencio esperado, sin B).
    float guest[ba::ipc::audio_ring::kBlockFrames * ba::ipc::audio_ring::kDefaultChannels];
    bool sil = false;
    bool fade = false;
    engine.processGuestBlock(guest, sil, fade);

    // 4) IPC: control + health (ticks: heartbeat + poll del endpoint).
    for (int i = 0; i < 3; ++i) {
        engine.tick();
    }

    // 5) Persistencia: StateDoc (fuente de verdad, spec §11.1).
    const ba::persist::StateDoc doc = engine.buildStateDoc();
    std::printf("ba-app headless: seq=%llu dsp=%zu ipc=%zu\n", static_cast<unsigned long long>(doc.sequence),
                doc.sections.count("dsp") ? doc.sections.at("dsp").size() : 0,
                doc.sections.count("ipc") ? doc.sections.at("ipc").size() : 0);

    // 6) Cierre ordenado: scheduleWrite(immediate) + flushNow (spec §11.2).
    const bool ok = engine.shutdown();
    std::printf("ba-app headless: OK (ring=%d control=%d stale=%d shutdown=%d)\n",
                engine.guestRingOpen() ? 1 : 0, engine.controlConnected() ? 1 : 0,
                engine.watchedIsStale() ? 1 : 0, ok ? 1 : 0);
    return ok ? 0 : 1;
}

bool hasFlag(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0) {
            return true;
        }
    }
    return false;
}

[[maybe_unused]] bool hasDisplay() {
#if defined(_WIN32) || defined(__APPLE__)
    return true;
#else
    const char* wl = std::getenv("WAYLAND_DISPLAY");
    const char* x = std::getenv("DISPLAY");
    return (wl != nullptr && *wl != '\0') || (x != nullptr && *x != '\0');
#endif
}

int dispatch(int argc, char** argv) {
#ifndef _WIN32
    // El tope de archivos abiertos (lo sube el launcher; esto, si se lanza sola):
    // cada pipeline del motor usa dos, y el de 256 de una app del Finder no alcanza.
    if (struct rlimit rl{}; getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur < 8192) {
        rl.rlim_cur = rl.rlim_max == RLIM_INFINITY ? 8192 : std::min<rlim_t>(8192, rl.rlim_max);
        setrlimit(RLIMIT_NOFILE, &rl);
    }
#endif
    // El launcher manda stdout y stderr a Logs/app.log. Hacia un archivo el CRT
    // pasa stdout a buffer completo y en un crash se pierde justo lo ultimo que
    // se dijo: se vuelca por linea, y se fija aca, antes del primer printf de
    // CUALQUIER modo (setvbuf solo vale antes de usar el stream; los modos no
    // lo repiten). stderr ya es sin buffer en todas las plataformas.
    //
    // El CRT de Microsoft trata _IOLBF como _IOFBF (buffer completo): en
    // Windows va sin buffer. El tamaño: 0 es valido en glibc ("elegi vos"),
    // pero el CRT de Microsoft exige 2 <= size <= INT_MAX: con 0 dispara su
    // invalid parameter handler y el proceso muere con 0xC0000409 (el ucrtbase
    // de Wine no valida y el CI no lo veia). Con _IONBF lo ignora, pero un
    // valor valido no cuesta nada.
#if defined(_WIN32)
    std::setvbuf(stdout, nullptr, _IONBF, BUFSIZ);
#else
    std::setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);
#endif
    // Portable: caches de Qt/fontconfig/shaders al bundle (antes de Qt y libobs).
    ba::platform::redirectUserDirs();
    // ffmpeg, ffprobe, yt-dlp, deno y gst-launch del bundle, primeros en el
    // PATH: los heredan los plugins, la terminal y el asistente (nadie baja
    // nada). Datos/Herramientas, si existe, va antes (un yt-dlp más nuevo).
    ba::platform::prepararHerramientas(ba::platform::dataRoot() / "Herramientas");
    // El registro, ANTES que nada: lo primero que puede fallar es el arranque,
    // y si el registro se abre despues, eso no queda escrito en ningun lado.
    ba::app::registrarTodo(ba::platform::dataRoot());
    // El diario de arranque: anota que se esta cargando para que, si algo se
    // lleva puesto el arranque, la proxima vez se saltee en vez de volver a
    // morir en el mismo lugar. Ver diag/DiarioDeArranque.hpp.
    ba::diag::abrirDiario(ba::platform::dataRoot() / "Runtime" / "arranque.json");
    // Credenciales en claro de versiones anteriores -> Credenciales/ cifradas.
    ba::security::CredentialStore::bundle().importLegacy(ba::platform::dataRoot());
    // --restore (spec §3.1.8): el launcher lo pasa al relanzar A tras un crash.
    const bool restore = hasFlag(argc, argv, "--restore");
    if (hasFlag(argc, argv, "--headless-selftest")) {
        return runHeadlessSelfTest(restore);
    }
#if BA_HAVE_MOTOR
    if (hasFlag(argc, argv, "--engine-selftest")) {
        return ba::app::runEngineSelfTest(argc, argv);
    }
#if BA_APP_HAVE_GUI
    if (!hasFlag(argc, argv, "--no-ui") && hasDisplay()) {
        return ba::app::runGui(argc, argv);
    }
#endif
    return ba::app::runOperate(argc, argv);
#else
    if (hasFlag(argc, argv, "--engine-selftest")) {
        std::printf("ba-app: compilado sin libobs; no hay motor de emision que probar\n");
        return 77; // 77 = saltado, igual que en la suite de CTest
    }
    return runHeadlessSelfTest(restore);
#endif
}

} // namespace

#if defined(_WIN32)

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> store;
    for (int i = 0; argvW != nullptr && i < argc; ++i) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<std::size_t>(n > 0 ? n - 1 : 0), '\0');
        WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, s.data(), n, nullptr, nullptr);
        store.push_back(s);
    }
    LocalFree(argvW);
    std::vector<char*> argv;
    for (auto& s : store) {
        argv.push_back(s.data());
    }
    argv.push_back(nullptr);
    return dispatch(static_cast<int>(store.size()), argv.data());
}

#if !defined(_MSC_VER)
// MinGW GUI CRT espera `WinMain` (narrow); se puentea a `wWinMain`.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR, int nCmdShow) {
    return wWinMain(hInstance, hPrevInstance, nullptr, nCmdShow);
}
#endif

#else

int main(int argc, char** argv) { return dispatch(argc, argv); }

#endif
