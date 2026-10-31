// test_ring_estereo.cpp — el anillo estéreo sin candado de la Matriz y los racks (F3-08)
//
// docs/arquitectura/04-plan-unificado.md, F3-08 (03 P11). Sin Qt y sin sonido: señales
// sintéticas.
//   1. Un escritor y un lector en hilos distintos, a los apurones: lo que sale es lo que
//      entró, en orden, sin repetir ni mezclar (una rampa: cada muestra es su número).
//   2. Deriva sintética: el que empuja va a 48 kHz + 300 ppm y el que saca a 48 kHz
//      (relojes de dos placas), diez minutos simulados de a 10 ms: el colchón no crece
//      sin fin (el lector recorta lo viejo) y entre recortes no hay huecos.
//   3. Al revés (el que empuja más lento): se vacía, se nota el hueco y vuelve a juntar
//      colchón.
//   4. Lo agendado a futuro (pushEn) no se recorta aunque pase el tope.
//   5. Lleno (el lector no lee): lo que no entra se tira y se cuenta.

#include "audio/RingEstereo.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using namespace ba::app::audio::anillo;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        } else {                                                                                             \
            std::printf("ok: %s\n", msg);                                                                    \
        }                                                                                                    \
    } while (0)

void testDosHilos() {
    Ring ring;
    constexpr std::uint32_t kTotal = 48000 * 20; // 20 s de audio, lo más rápido posible
    std::atomic<bool> listo{false};
    std::thread escritor([&] {
        std::vector<float> a(kBloque), b(kBloque);
        std::uint32_t n = 0;
        while (n < kTotal) {
            // No pasar el tope: si no, el lector recortaría (es otra prueba).
            if (ring.disponibles() + kBloque > kTope) {
                std::this_thread::yield();
                continue;
            }
            for (std::uint32_t i = 0; i < kBloque; ++i) {
                a[i] = static_cast<float>(n + i);
                b[i] = -static_cast<float>(n + i);
            }
            ring.push(a.data(), b.data(), kBloque);
            n += kBloque;
        }
        listo = true;
    });
    std::vector<float> a(333), b(333); // otro tamaño de bloque que el del escritor
    std::uint32_t esperado = 0;
    bool enOrden = true, parejo = true;
    while (esperado < kTotal) {
        const std::uint32_t hay = ring.disponibles();
        if (hay < 333 && !listo.load()) {
            std::this_thread::yield();
            continue;
        }
        if (!ring.pop(a.data(), b.data(), 333)) continue;
        const std::uint32_t k = std::min<std::uint32_t>(333, kTotal - esperado);
        for (std::uint32_t i = 0; i < k; ++i) {
            if (a[i] != static_cast<float>(esperado + i)) enOrden = false;
            if (b[i] != -a[i]) parejo = false;
        }
        if (!enOrden) break;
        esperado += k;
    }
    escritor.join();
    CHECK(enOrden, "dos hilos: lo que sale es lo que entró, en orden");
    CHECK(parejo, "dos hilos: izquierda y derecha del mismo cuadro");
    CHECK(ring.recortes.load() == 0 && ring.desbordes.load() == 0, "dos hilos: sin recortes ni desbordes");
}

/// Diez minutos simulados de a 10 ms: el que empuja a 48 kHz·(1 + ppm), el que saca a 48 kHz.
struct Deriva {
    std::uint64_t recortes = 0, huecos = 0, maxDisponibles = 0, saltosRampa = 0; ///< huecos: veces que se quedó sin
};
Deriva simularDeriva(double ppm) {
    Ring ring;
    Deriva d;
    std::vector<float> a(2 * kBloque), b(2 * kBloque), l(kBloque), r(kBloque);
    double acumulado = 0;
    std::uint64_t escritos = 0;
    // Una rampa (1…1e6, vuelta a 1: los float son enteros exactos hasta 2^24); 0 = silencio.
    const auto valor = [](std::uint64_t k) { return static_cast<float>(1 + k % 1'000'000); };
    float ultimo = 0;
    bool sonando = false;
    for (int paso = 0; paso < 60'000; ++paso) {
        acumulado += kBloque * (1.0 + ppm * 1e-6);
        const auto n = static_cast<std::uint32_t>(acumulado);
        acumulado -= n;
        for (std::uint32_t i = 0; i < n; ++i) a[i] = b[i] = valor(escritos + i);
        ring.push(a.data(), b.data(), n);
        escritos += n;
        d.maxDisponibles = std::max<std::uint64_t>(d.maxDisponibles, ring.disponibles());
        ring.pop(l.data(), r.data(), kBloque);
        for (std::uint32_t i = 0; i < kBloque; ++i) {
            if (l[i] == 0.0f) { // silencio: si venía sonando, un hueco
                if (sonando) ++d.huecos;
                sonando = false;
                break;
            }
            // Cada muestra, la anterior + 1, salvo en un recorte (salto adelante).
            if (sonando && l[i] != ultimo + 1.0f && !(ultimo == 1'000'000.0f && l[i] == 1.0f)) ++d.saltosRampa;
            ultimo = l[i];
            sonando = true;
        }
    }
    d.recortes = ring.recortes.load();
    return d;
}

void testDeriva() {
    const Deriva rapido = simularDeriva(300);
    std::printf("deriva +300 ppm, 10 min: %llu recortes, %llu saltos, %llu huecos, colchón máx %llu cuadros (%.1f ms)\n",
                static_cast<unsigned long long>(rapido.recortes), static_cast<unsigned long long>(rapido.saltosRampa),
                static_cast<unsigned long long>(rapido.huecos), static_cast<unsigned long long>(rapido.maxDisponibles),
                rapido.maxDisponibles * 1000.0 / 48000.0);
    CHECK(rapido.maxDisponibles <= kTope + 2 * kBloque, "+300 ppm: el colchón no crece sin fin");
    CHECK(rapido.recortes > 0 && rapido.saltosRampa == rapido.recortes, "+300 ppm: solo salta donde recorta");
    CHECK(rapido.huecos == 0, "+300 ppm: sin huecos");
    // 300 ppm de 10 min son 8640 cuadros de más: cada recorte saca kTope - kColchon.
    CHECK(rapido.recortes <= 8640 / (kTope - kColchon) + 2, "+300 ppm: pocos recortes (uno cada varios minutos)");

    const Deriva lento = simularDeriva(-300);
    std::printf("deriva -300 ppm, 10 min: %llu recortes, %llu huecos, colchón máx %llu cuadros\n",
                static_cast<unsigned long long>(lento.recortes), static_cast<unsigned long long>(lento.huecos),
                static_cast<unsigned long long>(lento.maxDisponibles));
    CHECK(lento.recortes == 0, "-300 ppm: no recorta");
    // 8640 cuadros de menos; de margen, el colchón menos el bloque que saca (480): se vacía
    // unas 18 veces en 10 minutos, UN hueco cada vez (no un cuadro de menos por bloque).
    CHECK(lento.huecos > 0 && lento.huecos <= 8640 / (kColchon - kBloque) + 2, "-300 ppm: un hueco cada tanto, y vuelve");
}

void testAgendado() {
    Ring ring(kCapacidadAgendado);
    std::vector<float> frase(48000 * 3, 0.5f); // 3 s, más que el tope
    const std::uint64_t ahora = 1'000'000'000'000ULL;
    ring.pushEn(frase.data(), frase.data(), static_cast<std::uint32_t>(frase.size()), ahora + 1'000'000'000ULL, ahora, 48000);
    std::vector<float> a(48000), b(48000);
    ring.pop(a.data(), b.data(), 48000); // el segundo de silencio hasta su hora
    std::vector<float> c(frase.size()), d(frase.size());
    ring.pop(c.data(), d.data(), static_cast<std::uint32_t>(c.size()));
    bool entera = true;
    for (float x : c) entera = entera && x == 0.5f;
    CHECK(entera && ring.recortes.load() == 0, "lo agendado (3 s, más que el tope) no se recorta");
}

void testLleno() {
    Ring ring(4096);
    std::vector<float> a(kBloque, 0.1f);
    for (int i = 0; i < 20; ++i) ring.push(a.data(), a.data(), kBloque); // 9600 en 4096
    CHECK(ring.disponibles() == 4096, "lleno: queda la capacidad");
    CHECK(ring.desbordes.load() == 20 * kBloque - 4096, "lleno: lo que no entró se cuenta");
}

} // namespace

int main() {
    testDosHilos();
    testDeriva();
    testAgendado();
    testLleno();
    if (g_fallos == 0) std::printf("test_ring_estereo: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
