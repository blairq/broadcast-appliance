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

#include "MediaRoute.hpp"
#include "RetornoLugar.hpp"
#include "Room.hpp"
#include "ipc/HealthBlock.hpp"
#include "ipc/MedidaTransporte.hpp"
#include "ipc/SharedAudioRing.hpp"
#include "platform/Ritmo.hpp"

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

} // namespace

int main() {
    testMedida();
    int segundos = 3;
    if (const char* e = std::getenv("BA_RETORNO_RITMO_S")) segundos = std::clamp(std::atoi(e), 1, 3600);
    testAlRitmo(segundos);
    sinSuVoz(true);
    sinSuVoz(false);
    salaPublica();
    std::printf("== test_retorno_ritmo: %s ==\n", g_fallos == 0 ? "OK" : "FALLOS");
    return g_fallos == 0 ? 0 : 1;
}
