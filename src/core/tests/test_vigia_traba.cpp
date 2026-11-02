// ============================================================================
// src/core/tests/test_vigia_traba.cpp — la interfaz trabada deja sus pilas (diag/VigiaTraba)
//
// El hilo principal hace de interfaz: late cada 20 ms y después se duerme 3 s
// (la traba). El vigía, desde su hilo, tiene que:
//   1. Sacar las pilas de TODOS los hilos MIENTRAS sigue trabada (antes de que
//      vuelva): Logs/dumps/A-<fecha>-traba-<ms>.pila (.dmp en Windows) y su .json.
//   2. Decirlo en el registro con la ruta; al volver, otra línea con lo que duró
//      (y el .json lo anota: duracion_ms).
//   3. En las pilas, el hilo de la interfaz marcado («interfaz <id>») con su pila
//      y los demás con su nombre; el resumen de la interfaz en el .json.
//   4. Como mucho una cada `cadaS`: una segunda traba enseguida no captura; con
//      cadaS = 0, sí.
// Además, sin hilos: diag::resumirPila clasifica (esperando al render de Qt,
// código propio, bucle anidado) y VigiaBucle ya no dice «corría: ?» cuando al
// volver de una traba se despachan muchos eventos atrasados antes que el turno.
// ============================================================================
#include "diag/Caidas.hpp"
#include "diag/VigiaBucle.hpp"
#include "diag/VigiaTraba.hpp"
#include "platform/Hilo.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FALLO [%s:%d] %s\n", __FILE__, __LINE__, msg);   \
            ++g_fallos;                                                   \
        } else {                                                          \
            std::printf("ok: %s\n", msg);                                 \
        }                                                                 \
    } while (0)

namespace fs = std::filesystem;
using reloj = std::chrono::steady_clock;

std::atomic<std::int64_t> g_latido{0}; ///< ms del último latido de la «interfaz»

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(reloj::now().time_since_epoch()).count();
}

/// La interfaz viva `ms`: late cada 20 ms.
void latir(int ms) {
    const auto fin = reloj::now() + std::chrono::milliseconds(ms);
    while (reloj::now() < fin) {
        g_latido = ahoraMs();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    g_latido = ahoraMs();
}

std::string leer(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

std::vector<fs::path> capturas(const fs::path& dir) {
    std::vector<fs::path> v;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string ext = e.path().extension().string();
        if ((ext == ".pila" || ext == ".dmp") && e.path().filename().string().find("-traba-") != std::string::npos)
            v.push_back(e.path());
    }
    std::sort(v.begin(), v.end());
    return v;
}

bool esperar(const std::function<bool()>& f, int ms) {
    const auto fin = reloj::now() + std::chrono::milliseconds(ms);
    while (reloj::now() < fin) {
        if (f()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return f();
}

void resumirSinHilos() {
    using ba::diag::resumirPila;
    // El hilo de la interfaz esperando al de render (lo de arriba del bucle de eventos).
    const std::string render = "BA-PILAS 1\n"
                               "== hilo 7 interfaz\n"
                               "#00 0x1 libsystem_kernel.dylib+0x10 __psynch_cvwait+0x8\n"
                               "#01 0x2 libsystem_pthread.dylib+0x20 _pthread_cond_wait+0x4\n"
                               "#02 0x3 QtCore+0x30 _ZN14QWaitCondition4waitEP6QMutex14QDeadlineTimer+0x10\n"
                               "#03 0x4 QtQuick+0x40 _ZN21QSGThreadedRenderLoop12polishAndSyncEPNS_6WindowEb+0x99\n"
                               "#04 0x5 QtQuick+0x50 _ZN12QQuickWindow5eventEP6QEvent+0x1\n"
                               "#05 0x6 QtCore+0x60 _ZN10QEventLoop4execE6QFlagsINS_17ProcessEventsFlagEE+0x1\n"
                               "#06 0x7 ba-app+0x70 main+0x1\n"
                               "== hilo 9 QSGRenderThread\n"
                               "#00 0x8 AMDRadeonX6000MTLDriver+0x10 ?\n"
                               "FIN\n";
    const std::string r = resumirPila(render, 7);
    std::printf("   resumen: %s\n", r.c_str());
    CHECK(r.rfind("esperando al hilo de render de Qt", 0) == 0, "resumirPila: la interfaz esperando al hilo de render");
    CHECK(ba::diag::VigiaTraba::hiloPorNombre(render, "QSGRenderThread") == 9, "hiloPorNombre: el de render");
    CHECK(resumirPila(render, 42).empty(), "un hilo que no está: vacío");
    // Código propio arriba del bucle (main, abajo, no cuenta).
    const std::string propio = "== hilo 7 interfaz\n"
                               "#00 0x1 libsystem_kernel.dylib+0x10 read+0x8\n"
                               "#01 0x2 ba-app+0x20 _ZN2ba3app11Conferencia6cargarEv+0x4\n"
                               "#02 0x3 QtCore+0x30 _ZN7QObject5eventEP6QEvent+0x10\n"
                               "#03 0x4 QtCore+0x40 _ZN10QEventLoop4execE6QFlagsINS_17ProcessEventsFlagEE+0x1\n"
                               "#04 0x5 ba-app+0x50 main+0x1\n";
    const std::string p = resumirPila(propio, 7);
    std::printf("   resumen: %s\n", p.c_str());
    CHECK(p.rfind("en código propio: ba::app::Conferencia::cargar", 0) == 0, "resumirPila: código propio (no main)");
    // Dos bucles: un diálogo (o un exec anidado).
    const std::string modal = "== hilo 7 interfaz\n"
                              "#00 0x1 QtCore+0x30 _ZN10QEventLoop4execE6QFlagsINS_17ProcessEventsFlagEE+0x1\n"
                              "#01 0x2 ba-app+0x20 _ZN2ba3app6Dialog4abrirEv+0x4\n"
                              "#02 0x4 QtCore+0x40 _ZN10QEventLoop4execE6QFlagsINS_17ProcessEventsFlagEE+0x1\n";
    const std::string m = resumirPila(modal, 7);
    std::printf("   resumen: %s\n", m.c_str());
    CHECK(m.rfind("en el bucle de eventos", 0) == 0 || m.find("anidado") != std::string::npos,
          "resumirPila: un bucle anidado no se confunde con código propio");
}

void culpableSinPisar() {
    // Al volver de una traba, Windows despacha los eventos encolados antes que el
    // temporizador: llenaban el anillo de 64 y el aviso decía «corría: ?».
    std::int64_t ahora = 1'000'000;
    std::vector<std::string> avisos;
    ba::diag::VigiaBucle v([&ahora]() { return ahora; }, 50, 50);
    v.alAvisar([&avisos](const std::string& t) { avisos.push_back(t); });
    v.tic();
    ahora += 10'000;
    v.anotar("ElLento", 43);
    ahora += 3'000'000; // la traba
    CHECK(v.enCurso() == "ElLento, evento 43", "enCurso: lo que corre (desde otro hilo)");
    for (int i = 0; i < 200; ++i) {
        v.anotar("Encolado", 1);
        ahora += 10;
    }
    v.tic();
    CHECK(avisos.size() == 1 && avisos[0].find("ElLento") != std::string::npos,
          "el aviso nombra al que trabó, aunque después se despacharon 200 encolados");
    if (!avisos.empty()) std::printf("   %s\n", avisos[0].c_str());
    // Las marcas del scene graph: entre «pidió» y «sincronizado», la interfaz espera al render.
    avisos.clear();
    ahora += 50'000;
    v.tic();
    ahora += 5'000;
    v.anotar(ba::diag::VigiaBucle::kQsgPidio);
    ahora += 2'000'000;
    v.anotar(ba::diag::VigiaBucle::kQsgSincronizando); // desde el hilo de render, ya tarde
    v.anotar(ba::diag::VigiaBucle::kQsgSincronizado);
    v.tic();
    CHECK(avisos.size() == 1 && avisos[0].find("espera al hilo de render") != std::string::npos,
          "una traba esperando al hilo de render lo dice");
    // Un mensaje nativo, con su número.
    avisos.clear();
    ahora += 50'000;
    v.tic();
    ahora += 5'000;
    v.anotar(ba::diag::VigiaBucle::kNativo, 0x0F);
    ahora += 1'000'000;
    v.tic();
    CHECK(avisos.size() == 1 && avisos[0].find("mensaje 0x000F") != std::string::npos, "un mensaje nativo, con su número");
}

} // namespace

int main() {
    ba::platform::nombrarHilo("interfaz");
    resumirSinHilos();
    culpableSinPisar();

    const fs::path dir = fs::temp_directory_path() / ("ba-test-vigia-traba-" + std::to_string(ahoraMs()));
    fs::create_directories(dir);

    // Un hilo con nombre, esperando: tiene que aparecer en las pilas.
    std::atomic<bool> fin{false};
    std::thread ayudante([&fin]() {
        ba::platform::nombrarHilo("ayudante-test");
        while (!fin) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });

    std::mutex mu;
    std::vector<std::string> lineas;
    std::atomic<int> cadaS{60};
    ba::diag::VigiaTraba vigia;
    ba::diag::VigiaTraba::Opciones o;
    o.dir = dir;
    o.edadMs = []() { return ahoraMs() - g_latido.load(); };
    o.umbralMs = []() { return 1000; };
    o.cadaS = [&cadaS]() { return cadaS.load(); };
    o.corria = []() { return std::string("QQuickWindowQmlImpl, evento 77"); };
    o.avisar = [&](const std::string& t) {
        std::lock_guard<std::mutex> l(mu);
        lineas.push_back(t);
        std::printf("   registro: %s\n", t.c_str());
    };
    o.sondeoMs = 50;
    const std::uint64_t yo = ba::diag::idDelHilo();
    latir(200);
    vigia.iniciar(std::move(o), yo);
    latir(300);

    // --- 1. La traba: 3 s quieta ---------------------------------------------------------
    const auto t0 = reloj::now();
    std::this_thread::sleep_for(std::chrono::seconds(3));
    const auto enTraba = capturas(dir); // lo que hay ANTES de volver a latir
    latir(400);
    std::printf("   (la traba duró %lld ms)\n",
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(reloj::now() - t0).count()));
    CHECK(enTraba.size() == 1, "las pilas se sacaron MIENTRAS seguía trabada");
    const auto todas = capturas(dir);
    CHECK(todas.size() == 1, "una sola captura por traba");
    if (!todas.empty()) {
        const fs::path c = todas[0];
        std::printf("   %s\n", c.filename().string().c_str());
        CHECK(c.filename().string().rfind("A-", 0) == 0, "A-<fecha>-traba-<ms>");
        fs::path js = c;
        js.replace_extension(".json");
        CHECK(esperar([&js]() { return leer(js).find("duracion_ms") != std::string::npos; }, 1000),
              "al volver, el .json anota lo que duró");
        const nlohmann::json j = nlohmann::json::parse(leer(js), nullptr, false);
        CHECK(j.value("tipo", "") == "traba" && j.value("hilo_interfaz", std::uint64_t{0}) == yo, "el .json: tipo traba y el hilo");
        CHECK(j.value("corria", "") == "QQuickWindowQmlImpl, evento 77", "el .json: lo que corría");
        CHECK(j.value("duracion_ms", 0) >= 2800, "duracion_ms: unos 3000");
#if !defined(_WIN32)
        const std::string texto = leer(c);
        CHECK(texto.rfind("BA-PILAS 1", 0) == 0 && texto.find("\nFIN\n") != std::string::npos, "el .pila completo");
        CHECK(texto.find("motivo traba-interfaz") != std::string::npos, "el motivo");
        CHECK(texto.find("interfaz " + std::to_string(yo) + "\n") != std::string::npos, "el hilo de la interfaz marcado");
        CHECK(texto.find("== hilo " + std::to_string(yo) + " interfaz") != std::string::npos, "su pila, con su nombre");
        CHECK(texto.find(" ayudante-test\n") != std::string::npos, "y los demás hilos con su nombre");
        CHECK(texto.find(" vigia-traba\n") == std::string::npos, "el vigía no se cuenta a sí mismo");
        const std::string r = ba::diag::resumirPila(texto, yo);
        std::printf("   la interfaz: %s\n", r.c_str());
        CHECK(!r.empty() && j.value("resumen_interfaz", "") == r, "el resumen de la interfaz, en el .json");
#else
        CHECK(fs::file_size(c) > 0, "el .dmp");
        CHECK(j.contains("hilos") && j["hilos"].size() >= 3, "el .json trae los hilos con su nombre");
#endif
    }
    {
        std::lock_guard<std::mutex> l(mu);
        CHECK(lineas.size() == 2, "dos líneas: la captura y la vuelta");
        if (lineas.size() == 2 && !todas.empty()) {
            CHECK(lineas[0].find(todas[0].filename().string()) != std::string::npos, "la primera dice la ruta");
            CHECK(lineas[1].find("duró") != std::string::npos, "la segunda, lo que duró");
        }
    }

    // --- 2. Otra enseguida: no captura (cadaS) ------------------------------------------------
    std::this_thread::sleep_for(std::chrono::milliseconds(1600));
    latir(300);
    CHECK(capturas(dir).size() == 1, "otra traba antes de cadaS: sin captura");

    // --- 3. Con cadaS = 0: sí ------------------------------------------------------------------
    cadaS = 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(1600));
    latir(300);
    CHECK(capturas(dir).size() == 2, "con cadaS = 0, la siguiente captura");
    CHECK(vigia.estado().value("capturas", 0) == 2, "estado(): dos capturas");

    vigia.detener();
    fin = true;
    ayudante.join();
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::printf(g_fallos ? "\n%d fallos\n" : "\ntodo ok\n", g_fallos);
    return g_fallos ? 1 : 0;
}
