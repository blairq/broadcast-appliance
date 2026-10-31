// test_pintor_aire.cpp — lo que sale al aire se pinta fuera del hilo de la interfaz (F3-03)
//
// PintorAire solo (vueltas, despertar, apagado) y los subtítulos con la «interfaz»
// trabada: el hilo principal duerme sin atender eventos y, mientras tanto, el cuadro
// se tiene que pintar con la frase y después borrarse a tiempo.
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "agent/FlujosTexto.hpp"
#include "agent/Subtitulos.hpp"
#include "emision/FuentesApp.hpp"
#include "emision/PintorAire.hpp"

namespace fuentes = ba::app::fuentes;
using namespace std::chrono_literals;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                                   \
    do {                                                              \
        if (!(c)) {                                                   \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__);       \
            ++g_fallas;                                               \
        } else {                                                      \
            std::printf("ok: %s\n", m);                               \
        }                                                             \
    } while (0)

/// Un cuadro que llegó al «motor»: si tenía algo pintado.
struct Cuadro {
    std::string nombre;
    bool pintado = false;
};
} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    {
        std::atomic<int> vueltas{0};
        {
            fuentes::PintorAire p("prueba", 50ms, [&]() { ++vueltas; });
            std::this_thread::sleep_for(280ms);
            CHECK(vueltas.load() >= 4 && vueltas.load() <= 8, "pinta a su ritmo sin bucle de eventos");
            const int antes = vueltas.load();
            p.despertar();
            std::this_thread::sleep_for(15ms);
            CHECK(vueltas.load() > antes, "despertar pinta ya");
        }
        const int alApagar = vueltas.load();
        std::this_thread::sleep_for(120ms);
        CHECK(vueltas.load() == alApagar, "apagado no pinta más");
    }

    {
        // El «motor»: guarda lo que le entregan (desde el hilo de los subtítulos).
        std::mutex m;
        std::vector<Cuadro> llegaron;
        const std::thread::id principal = std::this_thread::get_id();
        std::atomic<bool> desdeOtroHilo{true};
        fuentes::ponerSalida([&](const std::string& n, const std::uint8_t* d, int w, int h, int st) {
            if (std::this_thread::get_id() == principal) desdeOtroHilo = false;
            bool algo = false;
            for (int y = 0; y < h && !algo; ++y)
                for (int x = 0; x < w && !algo; ++x) algo = d[static_cast<std::size_t>(y) * st + x * 4 + 3] != 0;
            std::lock_guard<std::mutex> l(m);
            llegaron.push_back({n, algo});
        });

        QTemporaryDir dir;
        ba::app::agent::FlujosTexto flujos(dir.path().toStdString());
        ba::app::agent::Subtitulos subs(flujos);
        fuentes::alta("CC", "ba_subtitulos",
                      {{"flujo", "Sala"}, {"cps", 200.0}, {"minimo", 0.3}, {"borrar", 0.3}, {"ancho", 400}});
        emit flujos.frase("Sala", "hola, esto sale al aire");

        // La interfaz, trabada: ni un evento en 1,5 s.
        std::this_thread::sleep_for(1500ms);

        std::vector<Cuadro> visto;
        {
            std::lock_guard<std::mutex> l(m);
            visto = llegaron;
        }
        bool conTexto = false, despuesVacio = false;
        for (const Cuadro& c : visto) {
            if (c.nombre != "CC") continue;
            if (c.pintado) conTexto = true;
            else if (conTexto) despuesVacio = true;
        }
        CHECK(conTexto, "con la interfaz trabada, la frase sale al aire");
        CHECK(despuesVacio, "y se borra a tiempo, sin la interfaz");
        CHECK(desdeOtroHilo.load(), "los cuadros los entrega otro hilo, no el principal");
        fuentes::ponerSalida(nullptr);
        fuentes::bajaTodas();
    }

    if (g_fallas == 0)
        std::printf("TODO OK\n");
    else
        std::printf("%d FALLAS\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
