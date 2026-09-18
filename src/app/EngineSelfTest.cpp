// ============================================================================
// src/app/EngineSelfTest.cpp — Implementacion del autotest del motor
//
// Ver EngineSelfTest.hpp. Este archivo SI incluye <obs.h> (para enumerar
// codificadores y salidas disponibles); el resto del reporte sale del propio
// BroadcastEngine.
// ============================================================================

#include "EngineSelfTest.hpp"

#include <obs.h>
#include <util/bmem.h>
#include <util/platform.h>

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

/// Raiz del bundle portable: dos niveles arriba del ejecutable
/// (`<root>/bin/64bit/ba-app`). Las rutas por defecto se resuelven desde aca y
/// NO desde el directorio de trabajo: si no, el autotest solo funcionaba
/// lanzado con `cd release` y desde cualquier otro lado no cargaba ningun
/// modulo. Si no se puede resolver, cae al cwd.
std::string bundleRoot() {
    std::string root = ".";
    if (char* exeRel = os_get_executable_path_ptr("../..")) {
        if (char* abs = os_get_abs_path_ptr(exeRel)) {
            root = abs;
            bfree(abs);
        }
        bfree(exeRel);
    }
    return root;
}

/// Lista los ids disponibles de un enumerador de libobs.
std::vector<std::string> enumEncoders() {
    std::vector<std::string> out;
    const char* id = nullptr;
    for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i) {
        out.emplace_back(id);
    }
    return out;
}

std::vector<std::string> enumOutputs() {
    std::vector<std::string> out;
    const char* id = nullptr;
    for (size_t i = 0; obs_enum_output_types(i, &id); ++i) {
        out.emplace_back(id);
    }
    return out;
}

bool contains(const std::vector<std::string>& v, const std::string& needle) {
    for (const auto& s : v) {
        if (s == needle) {
            return true;
        }
    }
    return false;
}

} // namespace

int runEngineSelfTest(int argc, char** argv) {
    std::printf("== autotest del motor de emision (libobs %s) ==\n", obs_get_version_string());

    // Rutas: por defecto el layout portable relativo al cwd; se pueden
    // sobreescribir para correr desde el arbol de build.
    const std::string root = bundleRoot();
    EnginePaths paths;
    paths.moduleBinDir = argValue(argc, argv, "--modules", root + "/obs-plugins/64bit");
    paths.moduleDataDir = argValue(argc, argv, "--module-data", root + "/data/obs-plugins");
    paths.obsDataDir = argValue(argc, argv, "--obs-data", root + "/data/libobs");
    // Config de los plugins (rtmp-services baja services.json, etc.). Vacio
    // = el cwd: ensuciaba el directorio desde donde se lanzara el autotest.
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

    // --- 1) Arranque de libobs ---------------------------------------------
    const bool up = eng.startup(paths, video, audio);
    if (!up) {
        const std::string& err = eng.lastError();
        std::printf("  [ -- ] arranque de libobs — %s\n", err.c_str());
        // Sin contexto grafico no hay nada que probar, pero tampoco es un fallo
        // del motor: se reporta como SALTADO para no teñir de rojo una suite
        // que corre en una maquina sin servidor grafico.
        if (err.find("obs_reset_video") != std::string::npos ||
            err.find("sin sesion grafica") != std::string::npos) {
            std::printf("\nSKIP: esta sesion no tiene contexto grafico (GL/D3D).\n"
                        "      El pipeline de video de libobs no puede inicializarse aca;\n"
                        "      correr en una sesion con servidor grafico para ejercitarlo.\n");
            return 77;
        }
        return 1;
    }
    ok("arranque de libobs (video + audio + modulos)", true);

    // --- 2) Modulos cargados: codificadores y salidas ------------------------
    const auto encoders = enumEncoders();
    const auto outputs = enumOutputs();
    ok("codificador de video x264 disponible", contains(encoders, "obs_x264"));
    ok("codificador de audio AAC disponible", contains(encoders, "ffmpeg_aac"));
    ok("salida RTMP disponible", contains(outputs, "rtmp_output"));
    ok("salida de grabacion disponible", contains(outputs, "ffmpeg_muxer"));
    std::printf("         (%zu codificadores, %zu salidas)\n", encoders.size(), outputs.size());
    for (const auto& id : encoders) {
        std::printf("           - %s\n", id.c_str());
    }

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

    eng.shutdown();

    if (g_failures == 0) {
        std::printf("\nautotest del motor: OK\n");
        return 0;
    }
    std::printf("\nautotest del motor: %d fallo(s)\n", g_failures);
    return 1;
}

} // namespace ba::app
