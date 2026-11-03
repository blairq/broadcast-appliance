// test_trabajos.cpp — los trabajos de las entradas en pocos hilos compartidos (Trabajos.hpp)
//
// Sin GStreamer:
//   - cada fila corre en orden y de a uno (nunca dos trabajos de la misma fila a la vez);
//   - 200 filas (los lugares de una sala de 200) con trabajos que esperan un poco no crean
//     200 hilos: como mucho el tope, y a los pocos segundos sin trabajo quedan los de base;
//   - un trabajo que se traba (una cámara de red que no contesta) no frena a las otras filas;
//   - `esperar` espera lo encargado; desde un trabajo de la misma fila no se traba;
//   - una excepción en un trabajo no voltea al que sigue.

#include "Trabajos.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace ba::motor;

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
} // namespace

int main() {
    ponerHilosDeTrabajo(2, 16);

    // En orden y de a uno por fila.
    {
        FilaDeTrabajo f;
        std::vector<int> orden;
        std::mutex m;
        std::atomic<int> adentro{0}, maxAdentro{0};
        for (int i = 0; i < 50; ++i)
            f.encargar([&, i]() {
                const int a = ++adentro;
                if (a > maxAdentro) maxAdentro = a;
                esperar(1);
                {
                    std::lock_guard<std::mutex> l(m);
                    orden.push_back(i);
                }
                --adentro;
            });
        f.esperar();
        bool enOrden = orden.size() == 50;
        for (int i = 0; enOrden && i < 50; ++i) enOrden = orden[static_cast<size_t>(i)] == i;
        CHECK(enOrden, "los trabajos de una fila, en el orden en que se encargaron");
        CHECK(maxAdentro.load() == 1, "de a uno por fila");
        CHECK(f.pendientes() == 0, "esperar deja la fila vacía");
    }

    // 200 filas (una sala de 200 lugares): no 200 hilos.
    {
        std::vector<std::unique_ptr<FilaDeTrabajo>> filas;
        for (int i = 0; i < 200; ++i) filas.push_back(std::make_unique<FilaDeTrabajo>());
        std::atomic<int> hechos{0};
        const auto t0 = std::chrono::steady_clock::now();
        for (auto& f : filas)
            for (int k = 0; k < 3; ++k)
                f->encargar([&]() {
                    esperar(5); // armar una entrada: espera a GStreamer
                    ++hechos;
                });
        for (auto& f : filas) f->esperar();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const HilosDeTrabajo h = hilosDeTrabajo();
        std::printf("200 filas × 3 trabajos de 5 ms: %.0f ms, hilos vivos %d, máximo %d\n", ms, h.vivos, h.maximo);
        CHECK(hechos.load() == 600, "todos los trabajos corrieron");
        CHECK(h.maximo <= 16, "como mucho el tope de hilos (no uno por fila)");
        CHECK(h.maximo >= 2, "creció para no hacerlos de a dos");
        CHECK(ms < 600 * 5 / 2.0, "en paralelo: bastante menos que uno detrás del otro");
        // Sin trabajo, a los ~5 s, quedan los de base.
        esperar(7000);
        const HilosDeTrabajo quietos = hilosDeTrabajo();
        std::printf("sin trabajo: %d hilos vivos\n", quietos.vivos);
        CHECK(quietos.vivos <= 2, "sin trabajo vuelven a ser los de base (2)");
        CHECK(quietos.ocupados == 0 && quietos.filasEsperando == 0, "nadie ocupado ni esperando");
    }

    // Uno trabado no frena a los demás.
    {
        FilaDeTrabajo trabada, otra;
        std::atomic<bool> soltar{false}, hecho{false};
        trabada.encargar([&]() {
            while (!soltar) esperar(5);
        });
        // Más trabadas que los hilos de base: el que viene igual consigue hilo.
        FilaDeTrabajo t2, t3;
        t2.encargar([&]() {
            while (!soltar) esperar(5);
        });
        t3.encargar([&]() {
            while (!soltar) esperar(5);
        });
        otra.encargar([&]() { hecho = true; });
        for (int i = 0; i < 200 && !hecho; ++i) esperar(5);
        CHECK(hecho.load(), "con tres filas trabadas, otra fila corre igual");
        soltar = true;
        trabada.esperar();
        t2.esperar();
        t3.esperar();
    }

    // esperar desde un trabajo de la misma fila: no se traba. Una excepción no voltea al que sigue.
    {
        FilaDeTrabajo f;
        std::atomic<bool> despues{false}, siguio{false};
        f.encargar([&]() {
            f.esperar(); // desde adentro: vuelve enseguida
            despues = true;
        });
        f.encargar([]() { throw std::runtime_error("un trabajo que falla"); });
        f.encargar([&]() { siguio = true; });
        f.esperar();
        CHECK(despues.load(), "esperar desde un trabajo de la misma fila no se traba");
        CHECK(siguio.load(), "después de una excepción, el que sigue corre");
    }

    // Una fila que se destruye espera lo suyo.
    {
        std::atomic<int> n{0};
        {
            FilaDeTrabajo f;
            for (int i = 0; i < 10; ++i)
                f.encargar([&]() {
                    esperar(2);
                    ++n;
                });
        }
        CHECK(n.load() == 10, "al destruirse, la fila espera lo encargado");
    }

    if (fallos == 0) std::printf("test_trabajos: OK\n");
    return fallos == 0 ? 0 : 1;
}
