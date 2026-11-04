// test_lazo_invitados.cpp — cuánto tarda la voz de un invitado, con muchos, hasta la mezcla
//
// Con el GStreamer del bundle y nuestros plugins (sin ellos: SKIP), nada por los parlantes.
// Hace de gateway: N anillos BA.Audio.Guest.<n> escritos por UN hilo cada 10 ms (como el
// PullAdm del gateway, que tira de todos los NetEq en un hilo), sellados con la hora de
// escritura. Las N entradas del motor (`baanillo canal=…`, estudio=false: como las arma la
// app) van por el lector común de los anillos (baanillocomun) a los mezcladores
// (baentrega/batoma): el programa (BA.Audio.ContentForReturn) y los retornos de cada
// invitado (baretornos, mix-minus).
//
// El invitado 1 da un golpe cada 2 s; los demás hablan bajito (un seno de −46 dBFS: cada
// uno entrega bloques, como un invitado con el micrófono abierto). Se mide el LAZO con el
// reloj de pared: cuándo se leyó el golpe en un anillo de salida menos cuándo lo escribió
// el «gateway» (no el sello del bloque: si una hora se corre, el sello la esconde).
//   - en el retorno del invitado 2 (bus de oído): lo que espera el bus (audio.oidoMs,
//     20 ms) + el bloque + lo que tarda el hilo común: decenas de ms, nunca cientos;
//   - en el programa: lo de arriba + la latencia del estudio (la del bus del programa);
//   - y ninguna entrada se va a «su propia demora»: la edad de su audio es la de la red
//     del gateway, no la del motor;
//   - en régimen no se pierde nada: el retorno sale entero (un bloque cada 10 ms) y las
//     batomas del programa no tiran ningún bloque (tarde ni adelantado).
//
// BA_LAZO_INVITADOS=40 para la prueba grande (de fábrica 12: ctest). BA_LAZO_CALLADOS=k: los
// últimos k no mandan audio (para medir el mezclador cuando vence su espera; con eso, los
// controles de «ningún invitado se va a su propia demora» pueden no cumplirse).

#include "LectorAnillo.hpp"
#include "Motor.hpp"
#include "tests/Puntualidad.hpp"
#include "ipc/CanalesSalida.hpp"
#include "ipc/HealthBlock.hpp"
#include "ipc/SharedAudioRing.hpp"

#include <gst/gst.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <numbers>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

using namespace ba::motor;
using json = nlohmann::json;
namespace ar = ba::ipc::audio_ring;

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__); \
            ++fallos;                                                        \
        }                                                                    \
    } while (0)

void esperar(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

constexpr std::uint64_t kCadaGolpeNs = 2'000'000'000ULL;

/// El «gateway»: escribe los N anillos cada 10 ms y anota cuándo escribió cada golpe.
struct Gateway {
    std::atomic<bool> fin{false};
    std::mutex mu;
    std::vector<std::uint64_t> golpes; ///< bajo mu: la hora (QPC) en que se escribió cada golpe
    std::thread hilo;

    void andar(const std::vector<std::string>& canales) {
        std::vector<std::unique_ptr<ba::ipc::SharedAudioRing>> rings;
        for (std::size_t i = 0; i < canales.size(); ++i) {
            auto r = std::make_unique<ba::ipc::SharedAudioRing>();
            const std::wstring w(canales[i].begin(), canales[i].end());
            if (!ba::ipc::SharedAudioRing::init(w.c_str(), 2, ar::kDefaultCapacityBlocks, 1000 + static_cast<std::uint32_t>(i), 1, *r).ok)
                std::printf("no se pudo crear %s\n", canales[i].c_str());
            rings.push_back(std::move(r));
        }
        std::vector<float> b(ar::kBlockFrames * 2);
        std::vector<double> fase(canales.size(), 0.0);
        auto t = std::chrono::steady_clock::now();
        std::uint64_t proximoGolpe = 0;
        // BA_LAZO_CALLADOS=k: los últimos k invitados no mandan nada (su audio no llega):
        // el mezclador saca cada bloque al vencer su espera, sin ellos.
        const std::size_t callados =
            std::getenv("BA_LAZO_CALLADOS") != nullptr ? std::min<std::size_t>(rings.size() - 1, std::strtoul(std::getenv("BA_LAZO_CALLADOS"), nullptr, 10)) : 0;
        while (!fin) {
            for (std::size_t i = 0; i < rings.size() - callados; ++i) {
                bool golpe = false;
                for (std::uint32_t k = 0; k < ar::kBlockFrames; ++k) {
                    float v = 0.005f * static_cast<float>(std::sin(fase[i])); // −46 dBFS: habla bajito
                    fase[i] += 2 * std::numbers::pi * (300.0 + 10.0 * static_cast<double>(i)) / ar::kSampleRate;
                    b[2 * k] = b[2 * k + 1] = v;
                }
                const std::uint64_t ahora = ba::ipc::HealthBlock::nowQpc();
                if (i == 0) {
                    if (proximoGolpe == 0) proximoGolpe = ahora + kCadaGolpeNs;
                    if (ahora >= proximoGolpe) {
                        // Un golpe de 5 ms al principio del bloque.
                        for (std::uint32_t k = 0; k < 240; ++k) b[2 * k] = b[2 * k + 1] = 0.6f;
                        golpe = true;
                        proximoGolpe += kCadaGolpeNs;
                    }
                }
                rings[i]->push(b.data(), ahora);
                if (golpe) {
                    std::lock_guard<std::mutex> l(mu);
                    golpes.push_back(ahora);
                }
            }
            t += std::chrono::milliseconds(10);
            std::this_thread::sleep_until(t);
        }
    }
};

/// Lee `canal` durante `ms`: la hora de lectura de cada golpe (QPC). En `bloques`, cuántos
/// bloques leyó (sin contar los que había al abrirlo).
std::vector<std::uint64_t> golpesLeidos(const std::string& canal, int ms, std::size_t& bloques) {
    std::vector<std::uint64_t> out;
    bloques = 0;
    ba::ipc::SharedAudioRing r;
    const std::wstring w(canal.begin(), canal.end());
    const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!ba::ipc::SharedAudioRing::openReader(w.c_str(), r).ok) {
        if (std::chrono::steady_clock::now() > fin) return out;
        esperar(20);
    }
    std::vector<float> buf(r.blockFloats());
    ba::ipc::PoppedBlock b;
    while (r.pendientes() > 0) r.pop(buf.data(), b);
    std::uint64_t callado = 0;
    while (std::chrono::steady_clock::now() < fin) {
        while (r.pendientes() > 0 && r.pop(buf.data(), b)) {
            ++bloques;
            const std::uint64_t leido = ba::ipc::HealthBlock::nowQpc();
            for (std::size_t i = 0; i + 1 < buf.size(); i += 2) {
                if (std::max(std::fabs(buf[i]), std::fabs(buf[i + 1])) >= 0.3f) {
                    if (callado >= 48 * 300 && !b.isSilence) out.push_back(leido);
                    callado = 0;
                } else {
                    ++callado;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return out;
}

/// Cada golpe leído con el último escrito antes (los golpes van cada 2 s: sin confusión).
std::vector<double> emparejar(Gateway& g, const std::vector<std::uint64_t>& leidos) {
    std::vector<std::uint64_t> escritos;
    {
        std::lock_guard<std::mutex> l(g.mu);
        escritos = g.golpes;
    }
    std::vector<double> out;
    for (std::uint64_t r : leidos) {
        std::uint64_t mejor = 0;
        for (std::uint64_t w : escritos)
            if (w <= r) mejor = w;
        if (mejor != 0 && r - mejor < kCadaGolpeNs) out.push_back(static_cast<double>(r - mejor) / 1e6);
    }
    return out;
}

struct Lazo {
    double p50 = -1, max = -1;
    std::size_t n = 0;
    std::size_t bloques = 0; ///< los leídos del anillo de salida
};
Lazo medir(Gateway& g, const char* que, const std::string& canal, int ms) {
    Lazo r;
    std::vector<double> v = emparejar(g, golpesLeidos(canal, ms, r.bloques));
    r.n = v.size();
    if (!v.empty()) {
        std::sort(v.begin(), v.end());
        r.p50 = v[v.size() / 2];
        r.max = v.back();
    }
    std::printf("%-26s %zu golpes, lazo p50 %.1f ms, máx %.1f ms; %zu bloques en %d ms\n", que, r.n, r.p50, r.max, r.bloques, ms);
    return r;
}
} // namespace

int main() {
    ba::pruebas::SondaPuntualidad sonda; // ¿la máquina deja correr a tiempo? (tests/Puntualidad.hpp)
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE);
#endif
#if defined(BA_TEST_GST_PLUGINS)
    g_setenv("BA_GST_PLUGINS_EXTRA", BA_TEST_GST_PLUGINS, FALSE);
#endif
    std::string err;
    if (!iniciarGStreamer(&err)) {
        std::printf("SKIP: sin GStreamer (%s)\n", err.c_str());
        return 77;
    }
    for (const char* e : {"audiomixer", "baanillosink", "baanillocomun", "baentrega", "batoma", "baretornos", "balimitador"})
        if (!hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return 77;
        }
    const int n = std::getenv("BA_LAZO_INVITADOS") != nullptr ? std::max(2, std::atoi(std::getenv("BA_LAZO_INVITADOS"))) : 12;
    const std::string pre = "BA.Audio.LazoInv" + std::to_string(getpid()) + ".";
    std::vector<std::string> canales;
    for (int i = 1; i <= n; ++i) canales.push_back(pre + "Guest." + std::to_string(i));
    Gateway g;
    g.hilo = std::thread([&]() { g.andar(canales); });

    Motor::Config cfg;
    cfg.video = false;
    cfg.prefijoAnillos = pre;
    cfg.sinkEscucha = "fakesink sync=true"; // nada por los parlantes
    Motor motor(cfg);
    CHECK(motor.iniciar().empty(), "el motor arranca");
    motor.reloj().saltearArranque();
    std::string fallo;
    motor.alFallar([&fallo](const std::string&, const std::string& e) { fallo = e; });
    for (int i = 1; i <= n; ++i) {
        const std::string id = "invitado" + std::to_string(i);
        CHECK(motor.agregarEntrada(id, {{"pipeline", ""}, {"pipeline_audio", "baanillo canal=" + canales[static_cast<std::size_t>(i - 1)]},
                                        {"audio", true}, {"estudio", false}},
                                   {{"global", true}})
                  .empty(),
              "un invitado");
        CHECK(motor.retorno(i, "mix_minus", id).empty(), "su retorno");
    }
    motor.esperarCola();
    CHECK(fallo.empty(), "ninguna orden falló");
    std::printf("%d invitados; esperando que midan su edad (~10 s)…\n", n);
    esperar(14000);

    // Lo que tiran las batomas de cada bus de las salidas, en régimen (antes y después de medir).
    // tarde: el mezclador ya había pasado; adelantados: más que lo que espera la cola.
    std::uint64_t tarde = 0, adelantados = 0;
    const auto tirados = [&motor, &tarde, &adelantados]() {
        std::uint64_t n = 0;
        tarde = adelantados = 0;
        const json st = motor.estado();
        const json salidas = st.value("salidas", json::object());
        for (const auto& [sid, sal] : salidas.items()) {
            const json prog = sal.value("programa", json());
            if (!prog.is_object()) continue;
            const json entradas = prog.value("entradas", json::object());
            for (const auto& [id, e] : entradas.items()) {
                tarde += e.value("tarde", std::uint64_t{0});
                adelantados += e.value("adelantados", std::uint64_t{0});
            }
            n = tarde + adelantados;
        }
        return n;
    };
    const std::uint64_t tiradosAntes = tirados();
    const std::uint64_t tardeAntes = tarde, adelantadosAntes = adelantados;
    const Lazo ret = medir(g, "retorno del invitado 2", pre + ba::ipc::sufijoRetorno("", 2), 9000);
    const Lazo prog = medir(g, "programa (ContentForReturn)", pre + "ContentForReturn", 9000);

    const std::uint64_t tiradosEnRegimen = tirados() - tiradosAntes;
    std::printf("bloques tirados por las batomas del programa en régimen: %llu (tarde %llu, adelantados %llu)\n",
                static_cast<unsigned long long>(tiradosEnRegimen), static_cast<unsigned long long>(tarde - tardeAntes),
                static_cast<unsigned long long>(adelantados - adelantadosAntes));
    const json st = motor.estado();
    std::printf("estudio: latencia %.0f ms\n", st["estudio"].value("latencia_ms", 0.0));
    double edadMax = 0, propioMax = 0;
    int conPropia = 0;
    for (int i = 1; i <= n; ++i) {
        const json e = st["entradas"].value("invitado" + std::to_string(i), json::object());
        const json ea = e.value("edad_audio", json());
        const double p50 = ea.is_object() ? ea.value("p50_ms", -1.0) : -1.0, p95 = ea.is_object() ? ea.value("p95_ms", -1.0) : -1.0;
        const double propio = e.value("retraso_propio_ms", 0.0);
        if (i <= 3 || i == n || propio > 0)
            std::printf("  invitado%-3d edad p50 %6.1f p95 %6.1f ms, demora propia %.0f ms, %llu bloques\n", i, p50, p95, propio,
                        static_cast<unsigned long long>(e.value("bloques_audio", 0)));
        edadMax = std::max(edadMax, p95 + propio);
        propioMax = std::max(propioMax, propio);
        if (propio > 0) ++conPropia;
    }
    std::printf("edad p95 máx %.1f ms; con demora propia: %d de %d\n", edadMax, conPropia, n);

    // Lo que pide el plan: decenas de ms en el oído (20 de espera + el bloque + el hilo) y,
    // en el programa, eso más la latencia del estudio. Con aire para una máquina cargada.
    const double tope = std::getenv("BA_LAZO_TOPE_MS") != nullptr ? std::atof(std::getenv("BA_LAZO_TOPE_MS")) : 80.0;
    CHECK(ret.n >= 3, "los golpes del invitado 1 llegan al retorno del 2");
    CHECK(ret.p50 > 0 && ret.p50 <= tope && ret.max <= tope + 40.0, "el retorno llega en decenas de ms");
    CHECK(prog.n >= 3, "los golpes llegan al programa");
    const double latEstudio = st["estudio"].value("latencia_ms", 0.0);
    CHECK(prog.p50 > 0 && prog.p50 <= tope + latEstudio + 40.0, "el programa: lo del oído más la latencia del estudio");
    CHECK(edadMax < 60.0, "la edad del audio de cada invitado es la del gateway (decenas de ms)");
    CHECK(conPropia == 0, "ningún invitado se va a su propia demora");
    // 9 s de retorno: 900 bloques (con aire para lo que había al abrirlo y el borde).
    CHECK(ret.bloques >= 880, "el retorno sale entero en régimen (un bloque cada 10 ms)");
    CHECK(tiradosEnRegimen == 0, "en régimen las batomas del programa no tiran nada");

    motor.detener();
    g.fin = true;
    g.hilo.join();
    std::printf("== test_lazo_invitados (%d): %s ==\n", n, fallos == 0 ? "OK" : "FALLOS");
    return ba::pruebas::resultadoTiempoReal("test_lazo_invitados", fallos, &sonda);
}
