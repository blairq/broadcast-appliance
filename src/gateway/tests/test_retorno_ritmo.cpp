// test_retorno_ritmo.cpp — el retorno de audio de B: al ritmo, medido y sin la voz propia
//
// docs/arquitectura/04-plan-unificado.md, F0-01 a F0-04. Con anillos de verdad (memoria
// compartida) y la misma lógica que el hilo del retorno de MediaPump (RetornoLugar.hpp):
//   1. Medida (F0-02): faltantes por saltos de sello, descartes del escritor, cuadros
//      salteados y la edad p50/p99.
//   2. Al ritmo (F0-01, F0-03): un escritor sintético a 100 bloques/s (programa y un
//      mix-minus, como el motor) y un lector que mira cada 2 ms con platform::Ritmo:
//      overrun = 0, ocupación ≤ 3 bloques, a lo sumo un hueco del lugar (el que arma el
//      colchón). Dura 3 s; BA_RETORNO_RITMO_S=60 es la prueba larga del plan. Imprime el
//      período real del escritor (p99): en Windows, sin el temporizador fino, ~15,6 ms.
//   3. Sin su voz (F0-04): el programa lleva música + la voz del invitado 1 y su mix-minus
//      solo la música, con huecos. Lo que recibe el invitado 1 no se correlaciona con su
//      voz, ni con huecos ni con el lugar cerrado (retorno apagado: silencio); la fila, que
//      oye el programa, sí (el control de que la prueba ve la voz).
//   4. La sala publica «retorno apagado» en el participante y avisa solo si cambia.
//   5. El IFB con «ducking» (F2-12, Ducking.hpp): con un programa y una voz sintéticos
//      (sílabas de 120 ms con huecos de 60 ms), la envolvente de la ganancia del programa:
//      baja a la profundidad con la constante de ataque (10 ms), no sube en los huecos
//      entre sílabas (sin bombeo) y, 150 ms después de la voz, vuelve con la liberación
//      (300 ms), sin saltos.
//   6. Las voces de B (F2-14, F3-13; LectorVoz.hpp: operador, atendedor, asistente del
//      registro): con un escritor a 100 bloques/s y callado (el anillo abierto, nada
//      escrito), el bucle de antes (cada 2 ms) contra el de ahora (dormido en el timbre):
//      despertadas del sistema y vueltas por segundo, bloques perdidos y demora. Sin
//      anillo, un intento cada medio segundo.

#include "Ducking.hpp"
#include "LectorVoz.hpp"
#include "MediaRoute.hpp"
#include "RetornoLugar.hpp"
#include "Room.hpp"
#include "ipc/HealthBlock.hpp"
#include "ipc/MedidaTransporte.hpp"
#include "ipc/SharedAudioRing.hpp"
#include "platform/Ritmo.hpp"
#include "tests/Despertadas.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define ba_getpid _getpid
#else
#include <unistd.h>
#define ba_getpid getpid
#endif

using namespace ba::ipc;
using ba::gateway::MediaRoute;
namespace retorno = ba::gateway::retorno;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

constexpr std::uint64_t kMs = 1'000'000ULL;
constexpr std::size_t kFloats = audio_ring::kBlockFrames * 2;

/// Un nombre de anillo propio de esta corrida (dos tests a la vez no se pisan).
std::wstring nombre(const char* base) {
    const std::string s = std::string("BA.Test.Retorno.") + base + "." + std::to_string(ba_getpid());
    return std::wstring(s.begin(), s.end());
}

// --- 1. La medida --------------------------------------------------------------------

void testMedida() {
    MedidaTransporte m("BA.Test", "audio", 10 * kMs);
    const std::uint64_t t0 = 1'000'000 * kMs;
    m.bloque(t0, t0 + 5 * kMs);
    m.bloque(t0 + 10 * kMs, t0 + 15 * kMs);
    m.bloque(t0 + 40 * kMs, t0 + 45 * kMs); // faltan dos (20 y 30)
    m.bloque(t0 + 3000 * kMs, t0 + 3005 * kMs); // otro escritor: no es un hueco
    m.descartesDelEscritor(5); // el punto de partida
    m.descartesDelEscritor(8);
    m.mirar(2);
    m.mirar(4);
    m.mirar(1);
    FotoTransporte f = m.cerrar(10);
    CHECK(f.faltantes == 2, "dos bloques faltantes por el salto de sellos");
    CHECK(f.descartados == 3, "los descartes del escritor desde que se mira");
    CHECK(f.ocupacionMax == 4, "la ocupación máxima de la ventana");
    CHECK(f.leidos == 4, "los bloques leídos");
    CHECK(f.edadP50Ms == 5 && f.edadP99Ms == 5, "la edad de los bloques");
    f = m.cerrar(10);
    CHECK(f.faltantes == 0 && f.descartados == 0 && f.faltantesTotal == 2 && f.descartadosTotal == 3,
          "la ventana nueva empieza en cero; los totales siguen");
    CHECK(f.edadP50Ms < 0, "sin nada leído no hay edad");
    CHECK(!f.anomalia(3, 100), "una ventana tranquila no es anomalía");

    MedidaTransporte v("BA.Test.Video", "video");
    for (std::uint64_t i = 1; i <= 100; ++i) v.cuadro(i, t0, t0 + i * kMs); // de 1 a 100 ms
    v.cuadro(105, t0, t0 + 1 * kMs); // 101..104 salteados
    f = v.cerrar(10);
    CHECK(f.descartados == 4, "cuatro cuadros salteados");
    CHECK(f.edadP50Ms >= 49 && f.edadP50Ms <= 51, "p50 de 1..100 ms");
    CHECK(f.edadP99Ms >= 98 && f.edadP99Ms <= 100, "p99 de 1..100 ms");
    CHECK(f.anomalia(UINT64_MAX, 1000), "cuadros salteados: anomalía");
}

// --- 2. Al ritmo -----------------------------------------------------------------------

void testAlRitmo(int segundos) {
    SharedAudioRing escPrograma, escLugar, programa, lugar;
    const uint32_t pid = static_cast<uint32_t>(ba_getpid());
    CHECK(SharedAudioRing::init(nombre("RitmoProg").c_str(), 2, 16, pid, 1, escPrograma).ok, "anillo del programa");
    CHECK(SharedAudioRing::init(nombre("RitmoLugar").c_str(), 2, 16, pid, 1, escLugar).ok, "anillo del lugar");
    CHECK(SharedAudioRing::openReader(nombre("RitmoProg").c_str(), programa).ok, "lector del programa");
    if (!SharedAudioRing::openReader(nombre("RitmoLugar").c_str(), lugar).ok || !programa.header()) {
        CHECK(false, "lector del lugar");
        return;
    }

    const int bloques = segundos * 100;
    std::atomic<bool> listo{false};
    std::vector<double> periodosMs;
    periodosMs.reserve(static_cast<std::size_t>(bloques));
    // El escritor: el motor con sync=true, un bloque cada 10 ms con plazo absoluto.
    std::thread escritor([&]() {
        std::vector<float> b(kFloats, 0.1f);
        ba::platform::Ritmo ritmo{std::chrono::milliseconds(10), std::chrono::milliseconds(100)};
        auto antes = std::chrono::steady_clock::now();
        // Sellados como los sella el motor: por la hora del pipeline, de a 10 ms exactos (el
        // momento en que se escribe varía; el sello no).
        const std::uint64_t q0 = HealthBlock::nowQpc();
        for (int k = 0; k < bloques; ++k) {
            const std::uint64_t qpc = q0 + static_cast<std::uint64_t>(k) * 10 * kMs;
            escPrograma.push(b.data(), qpc);
            escLugar.push(b.data(), qpc);
            ritmo.esperar();
            const auto ahora = std::chrono::steady_clock::now();
            periodosMs.push_back(std::chrono::duration<double, std::milli>(ahora - antes).count());
            antes = ahora;
        }
        listo = true;
    });

    // El lector: el hilo del retorno, mirando cada 2 ms.
    MedidaTransporte mProg("prog", "audio", 10 * kMs), mLugar("lugar", "audio", 10 * kMs);
    std::vector<float> buf, bufLugar, silencio(kFloats, 0.0f);
    ba::platform::Ritmo ritmo{std::chrono::milliseconds(2)};
    std::uint64_t leidos = 0;
    while (!listo.load() || programa.pendientes() > 0) {
        leerPendientes(programa, buf, mProg, [&](const PoppedBlock&, std::size_t n) {
            retorno::bloqueDelLugar(lugar, bufLugar, silencio.data(), n, mLugar, HealthBlock::nowQpc());
            ++leidos;
            // Repartir a las sesiones cuesta algo: medio milisegundo por bloque.
            const auto hasta = std::chrono::steady_clock::now() + std::chrono::microseconds(500);
            while (std::chrono::steady_clock::now() < hasta) {
            }
        });
        ritmo.esperar();
    }
    escritor.join();
    const FotoTransporte fp = mProg.cerrar(segundos), fl = mLugar.cerrar(segundos);
    std::sort(periodosMs.begin(), periodosMs.end());
    const double p99 = periodosMs.empty() ? -1 : periodosMs[periodosMs.size() * 99 / 100];
    double suma = 0;
    for (double p : periodosMs) suma += p;
    std::printf("al ritmo, %d s: %llu leidos, programa: %llu descartados, ocupacion max %llu, %llu faltantes, edad "
                "p99 %.0f ms; lugar: %llu huecos, ocupacion max %llu; periodo del escritor: medio %.2f ms, p99 %.2f ms\n",
                segundos, (unsigned long long)leidos, (unsigned long long)fp.descartadosTotal,
                (unsigned long long)fp.ocupacionMax, (unsigned long long)fp.faltantesTotal, fp.edadP99Ms,
                (unsigned long long)fl.faltantesTotal, (unsigned long long)fl.ocupacionMax,
                suma / std::max<std::size_t>(1, periodosMs.size()), p99);
    CHECK(fp.descartadosTotal == 0 && programa.descartados() == 0, "overrun = 0 en el programa");
    CHECK(fp.faltantesTotal == 0, "no falta ningún bloque del programa");
    CHECK(lugar.descartados() == 0, "overrun = 0 en el lugar");
    CHECK(leidos == static_cast<std::uint64_t>(bloques), "se leyeron todos los bloques del programa");
    CHECK(fp.ocupacionMax <= 3, "la ocupación del programa queda en el nivel de operación (≤ 3 bloques)");
    CHECK(fl.ocupacionMax <= retorno::kColchonLugar + 1, "el lugar no junta más que su colchón");
    // El lugar se escribe junto con el programa: a lo sumo el primer hueco (el que arma el
    // colchón) y alguno si el planificador separa las dos escrituras justo en la mirada.
    CHECK(fl.faltantesTotal <= static_cast<std::uint64_t>(1 + segundos / 10), "casi sin huecos del lugar");
}

// --- 3. Sin su voz -----------------------------------------------------------------------

/// Correlación normalizada entre `a` y `b` corrida `lag` muestras (a[i + lag] con b[i]).
double correlacion(const std::vector<float>& a, const std::vector<float>& b, std::size_t lag) {
    double ab = 0, aa = 0, bb = 0;
    for (std::size_t i = 0; i + lag < a.size() && i < b.size(); ++i) {
        ab += static_cast<double>(a[i + lag]) * b[i];
        aa += static_cast<double>(a[i + lag]) * a[i + lag];
        bb += static_cast<double>(b[i]) * b[i];
    }
    return aa > 0 && bb > 0 ? ab / std::sqrt(aa * bb) : 0.0;
}

/// La mayor correlación (en valor absoluto) con un corrimiento de 0 a 2 bloques.
double peorCorrelacion(const std::vector<float>& salida, const std::vector<float>& voz) {
    double peor = 0;
    for (std::size_t lag = 0; lag <= 2 * audio_ring::kBlockFrames; lag += 16)
        peor = std::max({peor, std::fabs(correlacion(salida, voz, lag)), std::fabs(correlacion(voz, salida, lag))});
    return peor;
}

/// `lugarAbierto`: con el mix-minus del lugar 1 (con huecos) o con el lugar cerrado.
void sinSuVoz(bool lugarAbierto) {
    const char* caso = lugarAbierto ? "con huecos" : "con el lugar cerrado";
    SharedAudioRing escPrograma, escLugar, programa, lugar;
    const uint32_t pid = static_cast<uint32_t>(ba_getpid());
    const std::string sufijo = lugarAbierto ? "A" : "C";
    CHECK(SharedAudioRing::init(nombre(("VozProg" + sufijo).c_str()).c_str(), 2, 16, pid, 1, escPrograma).ok,
          "anillo del programa");
    CHECK(SharedAudioRing::init(nombre(("VozLugar" + sufijo).c_str()).c_str(), 2, 16, pid, 1, escLugar).ok,
          "anillo del lugar");
    CHECK(SharedAudioRing::openReader(nombre(("VozProg" + sufijo).c_str()).c_str(), programa).ok, "lector del programa");
    if (!SharedAudioRing::openReader(nombre(("VozLugar" + sufijo).c_str()).c_str(), lugar).ok || !programa.header()) {
        CHECK(false, "lector del lugar");
        return;
    }

    const MediaRoute invitado = MediaRoute::program(1);
    MediaRoute fila;
    fila.kind = MediaRoute::Kind::Backstage;
    std::vector<float> vozMono, alInvitado, aLaFila; // canal izquierdo
    std::vector<float> bp(kFloats), bl(kFloats), buf, bufLugar, silencio(kFloats, 0.0f);
    std::vector<const float*> deLugar(3, nullptr);
    MedidaTransporte mLugar("lugar", "audio", 10 * kMs);
    std::uint32_t semilla = 12345;
    double fase = 0;
    int huecos = 0;
    const int kBloques = 300;
    for (int k = 0; k < kBloques; ++k) {
        // La voz: ruido seudoaleatorio; la música: un seno de 440 Hz.
        for (std::size_t i = 0; i < audio_ring::kBlockFrames; ++i) {
            semilla = semilla * 1664525u + 1013904223u;
            const float voz = 0.3f * (static_cast<float>(semilla >> 8) / 16777216.0f - 0.5f);
            const float musica = 0.1f * static_cast<float>(std::sin(fase));
            fase += 2.0 * 3.14159265358979 * 440.0 / audio_ring::kSampleRate;
            bp[2 * i] = bp[2 * i + 1] = musica + voz;
            bl[2 * i] = bl[2 * i + 1] = musica;
            vozMono.push_back(voz);
        }
        const std::uint64_t qpc = HealthBlock::nowQpc();
        escPrograma.push(bp.data(), qpc);
        if (k % 7 == 3) ++huecos; // el mix-minus de este bloque no llega
        else escLugar.push(bl.data(), qpc);
        leerPendientes(programa, buf, [&](const PoppedBlock& blk, std::size_t n) {
            deLugar[1] = lugarAbierto ? retorno::bloqueDelLugar(lugar, bufLugar, silencio.data(), n, mLugar,
                                                                HealthBlock::nowQpc())
                                      : nullptr;
            const float* inv = retorno::fuente(invitado, blk.samples, deLugar, silencio.data());
            const float* fil = retorno::fuente(fila, blk.samples, deLugar, silencio.data());
            for (std::size_t i = 0; i < n; i += 2) {
                alInvitado.push_back(inv[i]);
                aLaFila.push_back(fil[i]);
            }
        });
    }
    const double cInvitado = peorCorrelacion(alInvitado, vozMono);
    const double cFila = std::fabs(correlacion(aLaFila, vozMono, 0));
    std::printf("sin su voz, %s: correlacion con su voz %.4f (la fila: %.3f)\n", caso, cInvitado, cFila);
    CHECK(cFila > 0.5, "control: lo que oye la fila (el programa) tiene la voz");
    CHECK(cInvitado < 0.02, "el invitado 1 no recibe su propia voz");
    if (lugarAbierto) {
        const FotoTransporte f = mLugar.cerrar(3);
        CHECK(f.faltantesTotal == static_cast<std::uint64_t>(huecos), "cada hueco del mix-minus se cuenta");
        double energia = 0;
        for (float x : alInvitado) energia += static_cast<double>(x) * x;
        CHECK(energia > 0, "con su mix-minus, el invitado oye la música");
    } else {
        CHECK(std::all_of(alInvitado.begin(), alInvitado.end(), [](float x) { return x == 0.0f; }),
              "con el lugar cerrado: retorno apagado (silencio), nunca el programa");
    }
}

// --- 4. La sala lo publica ---------------------------------------------------------------

void salaPublica() {
    using ba::gateway::Room;
    Room::Cfg cfg;
    cfg.maxGuests = 2;
    cfg.lobby = true;
    Room sala(cfg);
    int avisos = 0;
    Room::Sinks s;
    s.toGuest = [](const std::string&, const nlohmann::json&) {};
    s.toHosts = [&](const std::string& ev, const nlohmann::json&) {
        if (ev == "room") ++avisos;
    };
    s.dropGuest = [](const std::string&) {};
    s.slotFreed = [](int, const std::string&) {};
    s.route = [](const std::string&, const MediaRoute&) {};
    sala.setSinks(s);
    const auto j = sala.connect("Ana", "10.0.0.2", 1000);
    CHECK(j.err == Room::kOk, "entra a la sala");
    CHECK(sala.admit(j.id, 2000) == Room::kOk, "la admiten");
    const int slot = sala.slotOf(j.id);
    const auto retornoDe = [&]() {
        const nlohmann::json foto = sala.snapshot(); // vivo durante el for (no un temporal)
        for (const auto& p : foto["participants"])
            if (p.value("id", "") == j.id) return p.value("retorno", std::string("-"));
        return std::string("?");
    };
    CHECK(retornoDe() == "ok", "con su mix-minus: retorno ok");
    const int antes = avisos;
    sala.setSinRetorno({slot});
    CHECK(retornoDe() == "apagado", "sin su mix-minus: retorno apagado en su participante");
    CHECK(avisos == antes + 1, "la sala avisa el cambio");
    sala.setSinRetorno({slot});
    CHECK(avisos == antes + 1, "sin cambios no avisa de nuevo");
    sala.setSinRetorno({});
    CHECK(retornoDe() == "ok", "vuelve a ok");
}

// --- 5. El IFB con ducking ---------------------------------------------------------

void ducking() {
    ba::gateway::Ducking d(48000);
    std::vector<float> g;
    CHECK(!d.procesar(nullptr, kFloats, g), "sin voz y en reposo, el programa pasa tal cual");
    // La voz: un seno de 300 Hz a −12 dBFS en sílabas de 120 ms con huecos de 60 ms, 1 s; después nada.
    std::vector<double> env; // la ganancia del programa, un valor por milisegundo
    std::vector<float> voz(kFloats);
    double fase = 0;
    int ms = 0;
    for (int bloque = 0; bloque < 300; ++bloque) { // 3 s de a 10 ms
        bool hay = false;
        for (std::size_t i = 0; i < kFloats / 2; ++i) {
            const int t = ms + static_cast<int>(i / 48);
            const bool habla = t < 1000 && (t % 180) < 120;
            const float x = habla ? 0.25f * static_cast<float>(std::sin(fase)) : 0.0f;
            fase += 2.0 * 3.14159265358979 * 300.0 / 48000.0;
            voz[2 * i] = voz[2 * i + 1] = x;
            hay = hay || habla;
        }
        // Como el retorno: la cola de la voz trae bloques mientras el operador habla (con
        // los huecos adentro); cuando deja de hablar, no llega nada.
        const bool activo = d.procesar(ms < 1000 ? voz.data() : nullptr, kFloats, g);
        for (int k = 0; k < 10; ++k) env.push_back(activo ? g[static_cast<std::size_t>(k * 48 + 47)] : 1.0);
        ms += 10;
        (void)hay;
    }
    const double prof = std::pow(10.0, -15.0 / 20.0); // la de fábrica
    const auto db = [](double x) { return 20.0 * std::log10(std::max(x, 1e-6)); };
    std::printf("ducking: 5 ms %.1f dB, 50 ms %.1f dB, hueco (120-180 ms) máx %.1f dB, fin+100 ms %.1f dB, fin+300 ms %.1f dB, "
                "fin+2 s %.2f dB\n",
                db(env[5]), db(env[50]), db(*std::max_element(env.begin() + 120, env.begin() + 180)), db(env[1000 + 100]),
                db(env[1000 + 300]), db(env.back()));
    CHECK(env[5] < 0.9 && env[5] > prof, "el ataque es una rampa (a los 5 ms va bajando, no saltó)");
    CHECK(std::fabs(db(env[50]) - db(prof)) < 1.0, "a los 50 ms ya está en la profundidad (−15 dB)");
    double maxHuecos = 0;
    for (int t = 200; t < 1000; ++t) maxHuecos = std::max(maxHuecos, env[static_cast<std::size_t>(t)]);
    CHECK(db(maxHuecos) - db(prof) < 1.0, "entre sílabas no sube (sin bombeo)");
    CHECK(env[1000 + 100] < 0.2, "al terminar la voz no salta: a los 100 ms sigue abajo (la sostiene)");
    CHECK(env[1000 + 300] > 0.35 && env[1000 + 300] < 0.8, "a los 300 ms va volviendo (la liberación)");
    CHECK(env.back() > 0.99, "a los 2 s volvió entero");
    double salto = 0;
    for (std::size_t t = 1; t < env.size(); ++t) salto = std::max(salto, std::fabs(env[t] - env[t - 1]));
    CHECK(salto < 0.15, "sin saltos de un milisegundo a otro");
    bool reposo = false;
    for (int b = 0; b < 300 && !reposo; ++b) reposo = !d.procesar(nullptr, kFloats, g);
    CHECK(reposo, "de vuelta en reposo, el programa pasa tal cual");
    // La profundidad es configurable.
    ba::gateway::Ducking hondo(48000);
    hondo.ponerProfundidadDb(-20.0);
    std::fill(voz.begin(), voz.end(), 0.25f);
    for (int b = 0; b < 20; ++b) hondo.procesar(voz.data(), kFloats, g);
    CHECK(std::fabs(db(hondo.ganancia()) + 20.0) < 0.5, "con −20 dB baja a −20 dB");
}

// --- 6. Las voces (F2-14, F3-13) ---------------------------------------------------------

/// Lo que se midió en una fase de las voces.
struct FaseVoz {
    double despertadasS = 0; ///< del proceso, menos las del escritor solo
    double vueltasS = 0;     ///< vueltas del bucle del lector
    std::uint64_t escritos = 0, leidos = 0;
    double demoraMediaMs = 0;
};

/// Un escritor como la app (un bloque cada 10 ms, `escribe` = false: el anillo abierto y nadie
/// habla) y, si `lector`, el bucle de una voz de B: el de antes (cada 2 ms con platform::Ritmo)
/// o el de ahora (voz::leer, el de MediaPump::bajarVoz, dormido en el timbre).
FaseVoz faseVoz(const char* base, bool escribe, int lector, double segundos) {
    enum { kNinguno = 0, kAntes = 1, kAhora = 2 };
    const std::wstring n = nombre(base);
    SharedAudioRing esc;
    CHECK(SharedAudioRing::init(n.c_str(), 2, 16, static_cast<uint32_t>(ba_getpid()), 1, esc).ok, "anillo de la voz");
    std::atomic<bool> fin{false};
    std::atomic<std::uint64_t> vueltas{0}, leidos{0}, demora{0};
    const auto alBloque = [&](const PoppedBlock& b, std::size_t) {
        leidos.fetch_add(1);
        demora.fetch_add(HealthBlock::nowQpc() - b.qpc);
    };
    std::thread t;
    if (lector == kAntes)
        t = std::thread([&] {
            SharedAudioRing r;
            CHECK(SharedAudioRing::openReader(n.c_str(), r).ok, "abrir la voz");
            std::vector<float> buf;
            ba::platform::Ritmo ritmo{std::chrono::milliseconds(2)};
            while (!fin.load()) {
                vueltas.fetch_add(1);
                leerPendientes(r, buf, alBloque);
                ritmo.esperar();
            }
        });
    else if (lector == kAhora)
        t = std::thread([&] {
            std::vector<float> buf;
            ba::gateway::voz::leer(
                n, [&] { return !fin.load(); }, [] {}, [&](SharedAudioRing& r) { leerPendientes(r, buf, alBloque); },
                [&] { vueltas.fetch_add(1); });
        });
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // que abra y se duerma
    std::vector<float> b(kFloats, 0.1f);
    const long d0 = ba::pruebas::despertadasDelProceso();
    const std::uint64_t v0 = vueltas.load();
    const auto desde = std::chrono::steady_clock::now();
    auto proximo = desde;
    std::uint64_t escritos = 0;
    while (std::chrono::steady_clock::now() - desde < std::chrono::duration<double>(segundos)) {
        proximo += std::chrono::milliseconds(10);
        ba::platform::dormirHasta(proximo);
        if (escribe) {
            esc.push(b.data(), HealthBlock::nowQpc());
            ++escritos;
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const double seg = std::chrono::duration<double>(std::chrono::steady_clock::now() - desde).count();
    FaseVoz f;
    f.despertadasS = static_cast<double>(ba::pruebas::despertadasDelProceso() - d0) / seg;
    f.vueltasS = static_cast<double>(vueltas.load() - v0) / seg;
    fin.store(true);
    if (t.joinable()) t.join();
    f.escritos = escritos;
    f.leidos = leidos.load();
    f.demoraMediaMs = static_cast<double>(demora.load()) / static_cast<double>(std::max<std::uint64_t>(1, f.leidos)) / 1e6;
    return f;
}

void voces() {
    const double seg = 2.0;
    const double base = faseVoz("VozBase", true, 0, seg).despertadasS; // el escritor solo
    const FaseVoz antes = faseVoz("VozAntes", true, 1, seg);
    const FaseVoz ahora = faseVoz("VozAhora", true, 2, seg);
    const double baseQuieta = faseVoz("VozBaseQuieta", false, 0, seg).despertadasS;
    const FaseVoz quietaAntes = faseVoz("VozQuietaAntes", false, 1, seg);
    const FaseVoz quietaAhora = faseVoz("VozQuietaAhora", false, 2, seg);
    const auto mostrar = [](const char* que, const FaseVoz& f, double b) {
        std::printf("  %-36s %6.1f despertadas/s, %6.1f vueltas/s, %llu de %llu leídos, demora media %.2f ms\n", que,
                    std::max(0.0, f.despertadasS - b), f.vueltasS, (unsigned long long)f.leidos,
                    (unsigned long long)f.escritos, f.demoraMediaMs);
    };
    std::printf("voces de B (un bloque cada 10 ms; despertadas del lector: las del proceso menos las del escritor solo, "
                "%.1f/s y %.1f/s quieto)\n",
                base, baseQuieta);
    mostrar("hablando, cada 2 ms (antes)", antes, base);
    mostrar("hablando, timbre (ahora)", ahora, base);
    mostrar("callada, cada 2 ms (antes)", quietaAntes, baseQuieta);
    mostrar("callada, timbre (ahora)", quietaAhora, baseQuieta);
    CHECK(ahora.leidos + 1 >= ahora.escritos, "con el timbre la voz no pierde bloques");
    CHECK(ahora.vueltasS < 0.5 * antes.vueltasS, "bastante menos vueltas que cada 2 ms");
    CHECK(ahora.demoraMediaMs <= antes.demoraMediaMs + 0.5, "la demora media no crece");
    if (Timbre::hayPrimitiva()) {
        CHECK(ahora.vueltasS < 1.3 * 100, "con el timbre, una vuelta por bloque");
        CHECK(quietaAhora.vueltasS <= 2.0, "callada, con el timbre, casi ninguna vuelta");
        if (antes.despertadasS > 0)
            CHECK(ahora.despertadasS - base < 0.5 * (antes.despertadasS - base), "bastante menos despertadas del sistema");
    }
    // Un anillo que no está (el canal no conversa): un intento cada kReintento, no cada 50 ms.
    std::atomic<int> vueltasSin{0};
    std::atomic<bool> fin{false};
    std::thread t([&] {
        ba::gateway::voz::leer(
            nombre("VozQueNoEsta"), [&] { vueltasSin.fetch_add(1); return !fin.load(); }, [] {},
            [](SharedAudioRing&) {}, [] {});
    });
    std::this_thread::sleep_for(std::chrono::duration<double>(seg));
    fin.store(true);
    const auto parar = std::chrono::steady_clock::now();
    t.join();
    const double pararMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - parar).count();
    std::printf("  %-36s %6.1f vueltas/s; para en %.0f ms\n", "sin anillo", vueltasSin.load() / seg, pararMs);
    CHECK(vueltasSin.load() / seg < 5.0, "sin anillo, un intento cada medio segundo (antes: de a 50 ms)");
    CHECK(pararMs < 600.0, "sin anillo, para en menos de un reintento");
}

} // namespace

int main() {
    testMedida();
    int segundos = 3;
    if (const char* e = std::getenv("BA_RETORNO_RITMO_S")) segundos = std::clamp(std::atoi(e), 1, 3600);
    testAlRitmo(segundos);
    sinSuVoz(true);
    sinSuVoz(false);
    salaPublica();
    ducking();
    voces();
    std::printf("== test_retorno_ritmo: %s ==\n", g_fallos == 0 ? "OK" : "FALLOS");
    return g_fallos == 0 ? 0 : 1;
}
