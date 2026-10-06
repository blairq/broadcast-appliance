// ============================================================================
// src/app/EngineSelfTest.cpp — Implementacion del autotest del motor
//
// Ver EngineSelfTest.hpp. Los elementos de GStreamer que hacen falta se
// preguntan al registro (motor::hayFabrica); el resto del reporte sale del
// propio BroadcastEngine.
// ============================================================================

#include "EngineSelfTest.hpp"

#include "Piezas.hpp"
#include "platform/Paths.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "BroadcastEngine.hpp"

namespace ba::app {

namespace motor = ba::motor;

namespace {

int g_failures = 0;

void ok(const char* paso, bool cond, const std::string& detalle = {}) {
    std::printf("  [%s] %s%s%s\n", cond ? " OK " : "FALLO", paso, detalle.empty() ? "" : " — ",
                detalle.c_str());
    if (!cond) {
        ++g_failures;
    }
}

/// Valor de un argumento --clave=valor, o `def` si no esta.
std::string argValue(int argc, char** argv, const char* key, const std::string& def) {
    const std::string prefix = std::string(key) + "=";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.rfind(prefix, 0) == 0) {
            return a.substr(prefix.size());
        }
    }
    return def;
}

/// Raiz del bundle portable (desde el ejecutable, no desde el cwd).
std::string bundleRoot() { return ba::platform::bundleRoot().string(); }

} // namespace

int runEngineSelfTest(int argc, char** argv) {
    std::printf("== autotest del motor de emision (GStreamer) ==\n");

    // Rutas: por defecto el layout portable relativo al cwd; se pueden
    // sobreescribir para correr desde el arbol de build.
    const std::string root = bundleRoot();
    EnginePaths paths;
    paths.configDir = argValue(argc, argv, "--config", root + "/Config/plugin_config");

    const std::string recDir = argValue(argc, argv, "--rec-dir", root + "/Recordings");
    std::error_code ec;
    std::filesystem::create_directories(recDir, ec);

    VideoConfig video;
    // 720p30 en el autotest: mas liviano y suficiente para comprobar la cadena.
    video.baseWidth = video.outputWidth = 1280;
    video.baseHeight = video.outputHeight = 720;
    video.fpsNum = 30;
    video.fpsDen = 1;

    AudioConfig audio;

    BroadcastEngine eng;

    // --- 1) Arranque del motor ----------------------------------------------
    const bool up = eng.startup(paths, video, audio);
    if (!up) {
        const std::string& err = eng.lastError();
        std::printf("  [ -- ] arranque del motor — %s\n", err.c_str());
        // Sin GStreamer en el bundle no hay nada que probar (build sin motor).
        if (err.find("GStreamer no") != std::string::npos) {
            std::printf("\nSKIP: este programa no tiene GStreamer.\n");
            return 77;
        }
        return 1;
    }
    ok("arranque del motor (reloj, mezcla, composición)", true);

    // --- 2) Elementos de GStreamer: codificadores y salidas -------------------
    ok("codificador de video x264 disponible", motor::hayFabrica("x264enc"));
    ok("codificador de audio AAC disponible", motor::hayFabrica("avenc_aac"));
    ok("salida RTMP disponible", motor::hayFabrica("flvmux") && motor::hayFabrica("rtmp2sink"));
    ok("salida SRT disponible", motor::hayFabrica("mpegtsmux") && motor::hayFabrica("srtsink"));
    ok("salida de grabacion disponible", motor::hayFabrica("splitmuxsink") && motor::hayFabrica("matroskamux"));

    // --- 3) Escena de programa ----------------------------------------------
    ok("escena de programa creada", eng.buildScene(), eng.lastError());

    // --- 4) Camino de datos hacia el Proceso B ------------------------------
    // Es el productor que faltaba: sin esto el invitado remoto no ve el
    // programa ni oye la sala.
    ok("publicacion del retorno (BA.Program + BA.Audio.ContentForReturn)", eng.publishReturnFeeds(),
       eng.lastError());

    // --- 5) Grabacion real a disco ------------------------------------------
    EncoderConfig enc;
    enc.videoBitrateKbps = 2500;
    enc.preferHardware = false; // x264 siempre esta; el autotest no depende de GPU
    RecordConfig rec;
    rec.directory = recDir;
    rec.container = "mkv";

    // Lo que ya habia en el directorio antes de grabar: el chequeo final mira
    // SOLO archivos nuevos. Antes tomaba el mas grande del directorio, y con
    // una grabacion de una corrida previa pasaba aunque esta no escribiera nada.
    std::set<std::string> before;
    for (const auto& e : std::filesystem::directory_iterator(recDir, ec)) {
        before.insert(e.path().filename().string());
    }

    const bool recStarted = eng.startRecording(rec, enc);
    ok("grabacion iniciada", recStarted, eng.lastError());

    if (recStarted) {
        // Se deja correr para que el encoder produzca frames de verdad.
        std::this_thread::sleep_for(std::chrono::seconds(4));
        const EngineStatus st = eng.status();
        std::printf("         fps=%.1f grabando=%s\n", st.fps, st.recording ? "si" : "no");
        ok("la grabacion sigue activa tras 4 s", st.recording);
        eng.stopRecording();
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // El archivo tiene que existir y tener contenido: es la prueba de que
        // la cadena escena -> codificador -> muxer -> disco funciono entera.
        std::uintmax_t biggest = 0;
        std::string biggestName;
        for (const auto& e : std::filesystem::directory_iterator(recDir, ec)) {
            if (!e.is_regular_file(ec) || before.count(e.path().filename().string()) != 0) {
                continue;
            }
            const auto sz = e.file_size(ec);
            if (!ec && sz > biggest) {
                biggest = sz;
                biggestName = e.path().filename().string();
            }
        }
        ok("el archivo grabado existe y tiene datos", biggest > 10 * 1024,
           biggestName + " (" + std::to_string(biggest) + " bytes)");
    }

    // --- 6) Grabacion de archivo (T29): encoder propio, pista extra, copia y replay
    {
        RecordConfig fa = rec;
        fa.calidad = "archivo";
        fa.calidadCrf = 20;
        fa.pistas = {2};
        fa.copiaDir = recDir + "/copia";
        const bool okFa = eng.startRecording(fa, enc);
        ok("grabacion de archivo iniciada (encoder propio + pista 2 + copia)", okFa, eng.lastError());
        const bool okReplay = eng.setReplay(5, recDir + "/repeticiones", enc);
        ok("repeticion de 5 s andando", okReplay, eng.lastError());
        if (okFa) {
            std::this_thread::sleep_for(std::chrono::seconds(6));
            const nlohmann::json g = eng.grabacionEstado();
            std::printf("         %s\n", g.dump().c_str());
            ok("la copia en otro disco esta activa", g.value("copia_activa", false));
            ok("graba las pistas 1 y 2", g["pistas"].size() == 2);
            if (okReplay) {
                eng.guardarReplay();
                for (int i = 0; i < 40 && eng.ultimoReplay().empty(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                ok("la repeticion se guardo", !eng.ultimoReplay().empty(), eng.ultimoReplay());
                eng.setReplay(0, "", enc);
            }
            eng.stopRecording();
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::printf("         archivo: %s\n         copia: %s\n", g.value("archivo", "").c_str(),
                        g.value("copia", "").c_str());
            ok("el archivo y la copia tienen datos",
               std::filesystem::file_size(g.value("archivo", ""), ec) > 10 * 1024 &&
                   std::filesystem::file_size(g.value("copia", ""), ec) > 10 * 1024);
        }
    }

    eng.shutdown();

    if (g_failures == 0) {
        std::printf("\nautotest del motor: OK\n");
        return 0;
    }
    std::printf("\nautotest del motor: %d fallo(s)\n", g_failures);
    return 1;
}

} // namespace ba::app
