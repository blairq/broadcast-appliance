// test_mirada.cpp — dormir hasta el próximo cuadro esperado en vez de cada 4 ms (F2-14)
//
// docs/arquitectura/04-plan-unificado.md, F2-14 (02 C8, paso S). Con canales de verdad
// (memoria compartida, el mismo transporte que A y B):
//   1. La cuenta (ipc/Mirada.hpp): justo después del próximo esperado; atrasado, de a pasos
//      cortos que crecen hasta 4 ms; quieto, el paso quieto; sin ritmo, 4 ms; el período
//      medido sin las pausas.
//   2. El canal de video dice cuándo publicó y cada cuánto (el declarado o el medido), y
//      el tamaño real de un cuadro chico (V3: solo sus bytes).
//   3. Antes/después: un escritor a 30 cps (temblor de ±0,3 ms, como el motor; y de ±3 ms,
//      como un navegador) y, de a uno, un lector que mira cada 4 ms (antes), al ritmo del
//      escritor (33 ms: el batido del comentario de kMiradaMs) o con proximaMirada (ahora).
//      De cada uno: despertadas por segundo (cambios de contexto voluntarios del proceso
//      menos los del escritor solo), cuadros perdidos y la demora desde que se publicó
//      hasta que se leyó. Lo mismo con un anillo de audio (bloques de 10 ms).
//   4. El timbre (F3-13, ipc/Timbre.hpp): el mismo lector durmiendo hasta que el escritor
//      toca (esperarNuevo): menos despertadas que con Mirada y la demora de un salto en
//      decenas de µs (sin margen ni temblor); y en reposo (nadie publica), casi ninguna.
// BA_MIRADA_S=10 alarga cada fase (de fábrica 2 s).

#include "ipc/Mirada.hpp"
#include "ipc/SharedAudioRing.hpp"
#include "ipc/SpoutTransport.hpp"
#include "platform/Energia.hpp"
#include "platform/Ritmo.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define ba_getpid _getpid
#else
#include <sys/resource.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <libproc.h>
#endif
#define ba_getpid getpid
#endif

using namespace ba::ipc;
using Reloj = std::chrono::steady_clock;
using namespace std::chrono_literals;

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

std::uint64_t ns(Reloj::time_point t) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count());
}

void testCuenta() {
    const auto t0 = mirada::enReloj(1'000'000 * kMs); // un instante cualquiera
    const std::uint64_t ultimo = ns(t0);
    const std::uint64_t periodo = 33 * kMs;
    // Recién publicado: hasta el próximo + el margen.
    auto p = mirada::proxima(t0 + 5ms, ultimo, periodo, 15ms);
    CHECK(p == t0 + 33ms + mirada::kMargen, "duerme hasta el próximo esperado más el margen");
    // Justo antes del esperado: igual.
    p = mirada::proxima(t0 + 33ms, ultimo, periodo, 15ms);
    CHECK(p == t0 + 33ms + mirada::kMargen, "un poco antes: el mismo plazo (no bate con el propio ritmo)");
    // Atrasado 2 ms: un paso de 1 ms.
    p = mirada::proxima(t0 + 35ms, ultimo, periodo, 15ms);
    CHECK(p == t0 + 36ms, "recién atrasado: de a 1 ms");
    // Atrasado 20 ms: el paso crece hasta 4 ms (nunca más despertadas que antes).
    p = mirada::proxima(t0 + 53ms, ultimo, periodo, 15ms);
    CHECK(p == t0 + 57ms, "muy atrasado: de a 4 ms");
    // Quieto hace más de 1 s: el paso quieto.
    p = mirada::proxima(t0 + 1500ms, ultimo, periodo, 15ms);
    CHECK(p == t0 + 1515ms, "quieto: de a 15 ms");
    // Sin ritmo (nunca publicó o no se sabe el período): 4 ms como antes.
    CHECK(mirada::proxima(t0, 0, periodo, 15ms) == t0 + 4ms, "sin publicar: 4 ms");
    CHECK(mirada::proxima(t0, ultimo, 0, 15ms) == t0 + 4ms, "sin período: 4 ms");
    // El período medido: el primero tal cual, después la media móvil; una pausa no cuenta.
    std::uint64_t per = mirada::periodoMedido(0, 0, 100 * kMs);
    CHECK(per == 0, "con uno solo no hay período");
    per = mirada::periodoMedido(per, 100 * kMs, 133 * kMs);
    CHECK(per == 33 * kMs, "el primer intervalo");
    per = mirada::periodoMedido(per, 133 * kMs, 174 * kMs); // 41: 33 + 8/8
    CHECK(per == 34 * kMs, "media móvil (1/8 al nuevo)");
    CHECK(mirada::periodoMedido(per, 174 * kMs, 3000 * kMs) == per, "una pausa de más de 1 s no es un ritmo");
    // El temblor: un productor parejo no adelanta nada; uno que tiembla ±3 ms, unos ms (a lo
    // sumo un cuarto de período); leer uno de cada dos no se confunde con temblor.
    mirada::Temblor parejo, tiembla, saltea;
    for (std::uint64_t i = 1; i <= 64; ++i) {
        parejo.publicado(i * 33 * kMs, 33 * kMs);
        tiembla.publicado(i * 33 * kMs + (i % 2 == 0 ? 3 : -3) * static_cast<std::int64_t>(kMs), 33 * kMs);
        if (i % 2 == 0) saltea.publicado(i * 33 * kMs, 33 * kMs);
    }
    CHECK(parejo.adelanto(33 * kMs) == Reloj::duration::zero(), "parejo: en el momento");
    CHECK(tiembla.adelanto(33 * kMs) >= 6ms && tiembla.adelanto(33 * kMs) <= 33ms / 4 + 1ms, "tiembla: antes, con tope");
    CHECK(saltea.adelanto(33 * kMs) == Reloj::duration::zero(), "salteado: no es temblor");
    p = mirada::proxima(t0 + 5ms, ultimo, periodo, 15ms, 4ms);
    CHECK(p == t0 + 29ms + mirada::kMargen, "con adelanto: antes del esperado");
}

void testCanal() {
    const std::wstring nombre = L"BA.Test.Mirada.Canal." + std::to_wstring(ba_getpid());
    SpoutSender tx;
    CHECK(tx.publish(nombre.c_str(), SlotConfig{64, 36, 40 * kMs}), "publicar con el período dicho");
    SpoutReceiver rx;
    CHECK(rx.subscribe(nombre.c_str()), "suscribirse");
    CHECK(rx.periodoNs() == 40 * kMs, "el período que dijo el productor");
    std::vector<std::uint8_t> px(64 * 36 * 4, 0x11), dst(64 * 36 * 4, 0);
    const auto antes = Reloj::now();
    CHECK(tx.send(px.data(), px.size(), FrameMeta{1, 1}), "publicar uno");
    const auto plazo = rx.proximaMirada(15ms);
    CHECK(plazo >= antes + 40ms && plazo <= Reloj::now() + 40ms + mirada::kMargen, "la próxima mirada cuelga del envío");
    FrameMeta m{};
    CHECK(rx.receive(dst.data(), dst.size(), m) && m.ancho == 0 && dst[0] == 0x11, "un cuadro del tamaño del canal");
    // Un cuadro chico con su tamaño real: viajan solo sus bytes, y se sabe su tamaño.
    std::vector<std::uint8_t> chico(32 * 18 * 4, 0x22);
    CHECK(tx.send(chico.data(), chico.size(), FrameMeta{2, 2, 77, 32, 18}), "publicar uno chico");
    std::fill(dst.begin(), dst.end(), 0);
    CHECK(rx.receive(dst.data(), dst.size(), m), "leerlo");
    CHECK(m.ancho == 32 && m.alto == 18 && m.contenidoQpc == 77, "con su tamaño real y la hora de su contenido");
    CHECK(dst[32 * 18 * 4 - 1] == 0x22 && dst[32 * 18 * 4] == 0, "solo sus bytes");
    CHECK(!tx.send(chico.data(), chico.size(), FrameMeta{3, 3, 0, 65, 18}), "más ancho que el canal: no");
    CHECK(!tx.send(chico.data(), 16, FrameMeta{3, 3, 0, 32, 18}), "sin todos sus bytes: no");
    // Sin período dicho: lo mide.
    const std::wstring nombre2 = nombre + L".b";
    SpoutSender tx2;
    CHECK(tx2.publish(nombre2.c_str(), SlotConfig{16, 16}), "otro, sin período");
    SpoutReceiver rx2;
    CHECK(rx2.subscribe(nombre2.c_str()), "suscribirse al otro");
    std::vector<std::uint8_t> p2(16 * 16 * 4, 0);
    for (std::uint64_t i = 1; i <= 4; ++i) {
        tx2.send(p2.data(), p2.size(), FrameMeta{i, i});
        std::this_thread::sleep_for(20ms);
    }
    CHECK(rx2.periodoNs() >= 18 * kMs && rx2.periodoNs() <= 30 * kMs, "el período medido (~20 ms)");
}

/// Cómo se mira: cada 4 ms (antes), al ritmo del productor (el batido) o con proximaMirada.
enum class Modo { Cada4, AlRitmo, Mirada, Timbre };

struct Resultado {
    double despertadasS = 0; ///< cambios de contexto voluntarios por segundo del lector (despertadas de verdad)
    double vueltasS = 0;     ///< vueltas de su bucle por segundo
    std::uint64_t publicados = 0, leidos = 0;
    double demoraMediaMs = 0, demoraMaxMs = 0;
};

/// Las despertadas del proceso hasta ahora: en macOS las que cuenta el sistema (las «idle wake
/// ups» del Monitor de Actividad: temporizadores que lo sacan de dormir); en Linux, los cambios
/// de contexto voluntarios (cada despertada de un sleep es uno). En Windows no se mide (0).
long cambiosVoluntarios() {
#if defined(__APPLE__)
    rusage_info_v2 r{};
    if (proc_pid_rusage(getpid(), RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t*>(&r)) != 0) return 0;
    return static_cast<long>(r.ri_pkg_idle_wkups + r.ri_interrupt_wkups);
#elif defined(_WIN32)
    return 0;
#else
    rusage r{};
    getrusage(RUSAGE_SELF, &r);
    return r.ru_nvcsw;
#endif
}

double segundosDePrueba() {
    const char* v = std::getenv("BA_MIRADA_S");
    const double s = v != nullptr ? std::atof(v) : 2.0;
    return s > 0.5 ? s : 2.0;
}

/// Un escritor de video a 30 cps con temblor de ±`temblorUs` y un lector que mira según `modo`
/// (o ninguno: la base, para restar las despertadas del escritor).
Resultado faseVideo(int temblorUs, Modo modo, bool conLector) {
    static int n = 0;
    const std::wstring nombre = L"BA.Test.Mirada.Video." + std::to_wstring(ba_getpid()) + L"." + std::to_wstring(++n);
    SpoutSender tx;
    CHECK(tx.publish(nombre.c_str(), SlotConfig{64, 36}), "publicar");
    std::atomic<bool> fin{false};
    std::atomic<std::uint64_t> vueltas{0}, leidos{0}, demora{0}, demoraMax{0};
    std::thread lector;
    if (conLector)
        lector = std::thread([&] {
            SpoutReceiver rx;
            while (!rx.subscribe(nombre.c_str())) std::this_thread::sleep_for(1ms);
            std::vector<std::uint8_t> dst(64 * 36 * 4);
            ba::platform::Ritmo ritmo{modo == Modo::Cada4 ? Reloj::duration(4ms) : Reloj::duration(33ms)};
            while (!fin.load()) {
                vueltas.fetch_add(1);
                FrameMeta m{};
                const std::uint32_t visto = rx.timbre();
                if (rx.hasData() && rx.receive(dst.data(), dst.size(), m)) {
                    const std::uint64_t d = mirada::ahoraNs() - m.qpc;
                    leidos.fetch_add(1);
                    demora.fetch_add(d);
                    if (d > demoraMax.load()) demoraMax.store(d);
                }
                if (modo == Modo::Mirada)
                    ba::platform::dormirHasta(rx.proximaMirada(15ms));
                else if (modo == Modo::Timbre) {
                    if (!rx.hasData()) rx.esperarNuevo(visto, 15ms);
                } else
                    ritmo.esperar();
            }
        });
    std::this_thread::sleep_for(50ms);
    std::mt19937 azar(7);
    std::uniform_int_distribution<int> temblor(-temblorUs, temblorUs);
    std::vector<std::uint8_t> px(64 * 36 * 4, 0x40);
    const long csw0 = cambiosVoluntarios();
    const std::uint64_t vueltas0 = vueltas.load();
    const auto desde = Reloj::now();
    auto proximo = desde;
    std::uint64_t i = 0;
    while (Reloj::now() - desde < std::chrono::duration<double>(segundosDePrueba())) {
        proximo += std::chrono::microseconds(33'333);
        ba::platform::dormirHasta(proximo + std::chrono::microseconds(temblor(azar)));
        tx.send(px.data(), px.size(), FrameMeta{++i, mirada::ahoraNs()});
    }
    std::this_thread::sleep_for(40ms);
    const double seg = std::chrono::duration<double>(Reloj::now() - desde).count();
    Resultado r;
    r.despertadasS = static_cast<double>(cambiosVoluntarios() - csw0) / seg;
    r.vueltasS = static_cast<double>(vueltas.load() - vueltas0) / seg;
    fin.store(true);
    if (lector.joinable()) lector.join();
    r.publicados = i;
    r.leidos = leidos.load();
    r.demoraMediaMs = static_cast<double>(demora.load()) / std::max<std::uint64_t>(1, r.leidos) / 1e6;
    r.demoraMaxMs = static_cast<double>(demoraMax.load()) / 1e6;
    return r;
}

/// Lo mismo con un anillo de audio: bloques de 10 ms.
Resultado faseAudio(Modo modo, bool conLector) {
    static int n = 0;
    const std::wstring nombre = L"BA.Test.Mirada.Audio." + std::to_wstring(ba_getpid()) + L"." + std::to_wstring(++n);
    SharedAudioRing w;
    CHECK(SharedAudioRing::init(nombre.c_str(), 2, 16, static_cast<std::uint32_t>(ba_getpid()), 1, w).ok, "anillo");
    std::atomic<bool> fin{false};
    std::atomic<std::uint64_t> vueltas{0}, leidos{0}, demora{0}, demoraMax{0};
    std::thread lector;
    if (conLector)
        lector = std::thread([&] {
            SharedAudioRing r;
            CHECK(SharedAudioRing::openReader(nombre.c_str(), r).ok, "abrir para leer");
            std::vector<float> buf;
            ba::platform::Ritmo ritmo{4ms};
            while (!fin.load()) {
                vueltas.fetch_add(1);
                const std::uint32_t visto = r.timbre();
                leerPendientes(r, buf, [&](const PoppedBlock& b, std::size_t) {
                    const std::uint64_t d = mirada::ahoraNs() - b.qpc;
                    leidos.fetch_add(1);
                    demora.fetch_add(d);
                    if (d > demoraMax.load()) demoraMax.store(d);
                });
                if (modo == Modo::Mirada)
                    ba::platform::dormirHasta(r.proximaMirada(10ms));
                else if (modo == Modo::Timbre) {
                    if (r.pendientes() == 0) r.esperarNuevo(visto, 10ms);
                } else
                    ritmo.esperar();
            }
        });
    std::this_thread::sleep_for(30ms);
    std::vector<float> bloque(480 * 2, 0.1f);
    const long csw0 = cambiosVoluntarios();
    const std::uint64_t vueltas0 = vueltas.load();
    const auto desde = Reloj::now();
    auto proximo = desde;
    std::uint64_t i = 0;
    while (Reloj::now() - desde < std::chrono::duration<double>(segundosDePrueba())) {
        proximo += 10ms;
        ba::platform::dormirHasta(proximo);
        w.push(bloque.data(), mirada::ahoraNs());
        ++i;
    }
    std::this_thread::sleep_for(30ms);
    const double seg = std::chrono::duration<double>(Reloj::now() - desde).count();
    Resultado r;
    r.despertadasS = static_cast<double>(cambiosVoluntarios() - csw0) / seg;
    r.vueltasS = static_cast<double>(vueltas.load() - vueltas0) / seg;
    fin.store(true);
    if (lector.joinable()) lector.join();
    r.publicados = i;
    r.leidos = leidos.load();
    r.demoraMediaMs = static_cast<double>(demora.load()) / std::max<std::uint64_t>(1, r.leidos) / 1e6;
    r.demoraMaxMs = static_cast<double>(demoraMax.load()) / 1e6;
    if (conLector) CHECK(w.descartados() == 0, "sin descartes del escritor");
    return r;
}

void mostrar(const char* que, const Resultado& r, double base) {
    std::printf("  %-34s %6.1f despertadas/s (%6.1f vueltas/s), %3llu de %3llu leídos (%4.1f %% perdidos), demora media "
                "%5.2f ms, máx %5.2f ms\n",
                que, std::max(0.0, r.despertadasS - base), r.vueltasS, static_cast<unsigned long long>(r.leidos),
                static_cast<unsigned long long>(r.publicados),
                100.0 * (1.0 - static_cast<double>(r.leidos) / std::max<std::uint64_t>(1, r.publicados)), r.demoraMediaMs,
                r.demoraMaxMs);
}

void testAntesDespues() {
    // El escritor solo: sus propias despertadas, para restarlas.
    for (int temblorUs : {300, 3000}) {
        const double base = faseVideo(temblorUs, Modo::Cada4, false).despertadasS;
        std::printf("video a 30 cps, temblor ±%.1f ms (despertadas del lector: las del proceso menos las del escritor solo, %.1f/s)\n",
                    temblorUs / 1000.0, base);
        const Resultado antes = faseVideo(temblorUs, Modo::Cada4, true);
        const Resultado batido = faseVideo(temblorUs, Modo::AlRitmo, true);
        const Resultado ahora = faseVideo(temblorUs, Modo::Mirada, true);
        const Resultado timbre = faseVideo(temblorUs, Modo::Timbre, true);
        mostrar("cada 4 ms (antes)", antes, base);
        mostrar("cada 33 ms (al ritmo: bate)", batido, base);
        mostrar("proximaMirada (F2-14)", ahora, base);
        mostrar("timbre (F3-13)", timbre, base);
        if (Timbre::hayPrimitiva()) {
            CHECK(timbre.leidos + 1 >= timbre.publicados, "con el timbre no se pierden cuadros");
            CHECK(timbre.vueltasS < 1.3 * 30, "con el timbre, una vuelta por cuadro");
            CHECK(timbre.demoraMediaMs < ahora.demoraMediaMs, "con el timbre, menos demora por salto que con Mirada");
            CHECK(timbre.demoraMediaMs < 0.3, "con el timbre, la demora de un salto en décimas de ms");
        }
        CHECK(ahora.leidos + 1 >= ahora.publicados, "con proximaMirada no se pierden cuadros");
        // (Sin cuenta de despertadas del sistema —Windows—, las vueltas del bucle.)
        if (antes.despertadasS > 0)
            CHECK(ahora.despertadasS - base < 0.6 * (antes.despertadasS - base), "bastante menos despertadas que a 4 ms");
        else
            CHECK(ahora.vueltasS < 0.6 * antes.vueltasS, "bastante menos vueltas que a 4 ms");
        CHECK(ahora.demoraMediaMs <= antes.demoraMediaMs + 1.0, "la demora media no crece");
    }
    const double base = faseAudio(Modo::Cada4, false).despertadasS;
    std::printf("audio, bloques de 10 ms (escritor solo: %.1f despertadas/s)\n", base);
    const Resultado antes = faseAudio(Modo::Cada4, true);
    const Resultado ahora = faseAudio(Modo::Mirada, true);
    const Resultado timbre = faseAudio(Modo::Timbre, true);
    mostrar("cada 4 ms (antes)", antes, base);
    mostrar("proximaMirada (F2-14)", ahora, base);
    mostrar("timbre (F3-13)", timbre, base);
    if (Timbre::hayPrimitiva()) {
        CHECK(timbre.leidos + 1 >= timbre.publicados, "con el timbre no se pierde audio");
        CHECK(timbre.vueltasS < 1.3 * 100, "con el timbre, una vuelta por bloque");
        CHECK(timbre.demoraMediaMs < 0.3, "con el timbre, la demora de un salto en décimas de ms");
    }
    CHECK(ahora.leidos + 1 >= ahora.publicados, "no se pierde audio");
    CHECK(antes.despertadasS > 0 ? ahora.despertadasS < antes.despertadasS : ahora.vueltasS < antes.vueltasS,
          "menos despertadas que a 4 ms");
    CHECK(ahora.demoraMediaMs <= antes.demoraMediaMs + 1.0, "la demora media no crece");
}

/// En reposo (el canal existe y nadie publica): con Mirada el lector mira de a `pasoQuieto`;
/// con el timbre, solo por el vigía (kVigia). Y despertar() (unlock de una fuente) lo saca
/// del sueño en seguida.
void testReposo() {
    if (!Timbre::hayPrimitiva()) {
        std::printf("reposo: sin primitiva de timbre en este sistema (se sigue con Mirada)\n");
        return;
    }
    const std::wstring nombre = L"BA.Test.Mirada.Reposo." + std::to_wstring(ba_getpid());
    SpoutSender tx;
    CHECK(tx.publish(nombre.c_str(), SlotConfig{16, 16, 33 * kMs}), "publicar (sin cuadros)");
    SpoutReceiver rx;
    CHECK(rx.subscribe(nombre.c_str()), "suscribirse");
    CHECK(rx.tocaTimbre(), "el productor toca el timbre");
    std::atomic<bool> fin{false};
    std::atomic<int> vueltasMirada{0}, vueltasTimbre{0};
    auto lector = [&](bool conTimbre, std::atomic<int>& vueltas) {
        while (!fin.load()) {
            vueltas.fetch_add(1);
            const std::uint32_t visto = rx.timbre();
            if (rx.hasData()) continue;
            if (conTimbre)
                rx.esperarNuevo(visto, 15ms);
            else
                ba::platform::dormirHasta(rx.proximaMirada(15ms));
        }
    };
    const double seg = segundosDePrueba();
    {
        std::thread t(lector, false, std::ref(vueltasMirada));
        std::this_thread::sleep_for(std::chrono::duration<double>(seg));
        fin.store(true);
        t.join();
    }
    fin.store(false);
    std::thread t(lector, true, std::ref(vueltasTimbre));
    std::this_thread::sleep_for(std::chrono::duration<double>(seg));
    fin.store(true);
    const auto antes = Reloj::now();
    rx.despertar();
    t.join();
    const double despertarMs = std::chrono::duration<double, std::milli>(Reloj::now() - antes).count();
    std::printf("reposo (nadie publica): Mirada %.1f vueltas/s, timbre %.1f vueltas/s; despertar() en %.2f ms\n",
                vueltasMirada.load() / seg, vueltasTimbre.load() / seg, despertarMs);
    CHECK(vueltasTimbre.load() / seg <= 2.0, "con el timbre, en reposo, casi ninguna vuelta");
    CHECK(despertarMs < 20.0, "despertar() saca al lector del sueño en seguida");
}

} // namespace

int main() {
    // Como el gateway y la app al arrancar: sin App Nap ni temporizadores agrupados (en macOS
    // una espera de 30 ms se estira varios ms si no; en Windows, el temporizador fino).
    ba::platform::sinAhorroDeEnergia();
    testCuenta();
    testCanal();
    testAntesDespues();
    testReposo();
    if (g_fallos == 0) std::printf("test_mirada: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
