// ============================================================================
// src/gateway/tests/test_colas_compartidas.cpp — las colas de libwebrtc sobre pocos hilos
//
// rtc/ColasCompartidas.hpp tiene que cumplir el contrato de webrtc::TaskQueueBase aunque
// muchas colas compartan pocos hilos:
//   1. FIFO por cola, con varias colas y varios productores a la vez.
//   2. Nunca dos tareas de la misma cola a la vez; Current()/IsCurrent() dentro de la tarea.
//   3. PostDelayedTask: no antes de tiempo, en orden de vencimiento, y con poca demora.
//   4. Delete desde afuera espera la tarea en curso; las pendientes (y las diferidas) se
//      destruyen sin correr. Delete desde una tarea de la misma cola.
//   5. Una decodificación larga no frena a las livianas (grupos aparte).
//   6. Pocos hilos (los de los grupos) y pocas despertadas (en lote).
//   7. Cierre ordenado de la fábrica.
// ============================================================================

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/units/time_delta.h"

#include "diag/Frontera.hpp"
#include "rtc/ColasCompartidas.hpp"

using namespace ba::gateway;
using Reloj = std::chrono::steady_clock;
using Cola = std::unique_ptr<webrtc::TaskQueueBase, webrtc::TaskQueueDeleter>;

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

double msDesde(Reloj::time_point t0) { return std::chrono::duration<double, std::milli>(Reloj::now() - t0).count(); }

/// Espera (con tope) a que `cond` sea cierta.
template <typename F> bool esperar(F cond, int topeMs = 5000) {
    const auto t0 = Reloj::now();
    while (!cond()) {
        if (msDesde(t0) > topeMs) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

/// Cuenta cuántas veces se destruyó sin correr.
struct Testigo {
    std::atomic<int>* destruidos;
    explicit Testigo(std::atomic<int>* d) : destruidos(d) {}
    Testigo(Testigo&& o) noexcept : destruidos(o.destruidos) { o.destruidos = nullptr; }
    Testigo(const Testigo&) = delete;
    ~Testigo() {
        if (destruidos != nullptr) destruidos->fetch_add(1);
    }
};

void fifoYExclusion(webrtc::TaskQueueFactory& f) {
    std::printf("-- 1/2. FIFO, exclusión e IsCurrent con 40 colas y 4 productores\n");
    constexpr int kColas = 40, kProductores = 4, kPorProductor = 500;
    std::vector<Cola> colas;
    for (int i = 0; i < kColas; ++i)
        colas.push_back(f.CreateTaskQueue(i % 2 == 0 ? "AudioEncoder" : "DecodingQueue", webrtc::TaskQueueFactory::Priority::NORMAL));
    struct Estado {
        std::atomic<int> dentro{0};
        std::atomic<int> hechas{0};
        std::vector<int> ultimo = std::vector<int>(kProductores, -1); // solo desde la cola
        bool desorden = false;
    };
    std::vector<Estado> est(kColas);
    std::atomic<int> solapes{0}, noActual{0}, otraActual{0};
    std::mutex hilosM;
    std::set<std::thread::id> hilos;
    std::vector<std::thread> prod;
    for (int p = 0; p < kProductores; ++p)
        prod.emplace_back([&, p]() {
            for (int k = 0; k < kPorProductor; ++k)
                for (int c = 0; c < kColas; ++c) {
                    webrtc::TaskQueueBase* q = colas[c].get();
                    webrtc::TaskQueueBase* otra = colas[(c + 1) % kColas].get();
                    q->PostTask([&, c, p, k, q, otra]() {
                        Estado& e = est[c];
                        if (e.dentro.fetch_add(1) != 0) solapes.fetch_add(1);
                        if (!q->IsCurrent() || webrtc::TaskQueueBase::Current() != q) noActual.fetch_add(1);
                        if (otra->IsCurrent()) otraActual.fetch_add(1);
                        if (e.ultimo[p] != k - 1) e.desorden = true;
                        e.ultimo[p] = k;
                        if (k % 97 == 0) std::this_thread::sleep_for(std::chrono::microseconds(200));
                        {
                            std::lock_guard<std::mutex> l(hilosM);
                            hilos.insert(std::this_thread::get_id());
                        }
                        e.dentro.fetch_sub(1);
                        e.hechas.fetch_add(1);
                    });
                }
        });
    for (auto& t : prod) t.join();
    const bool todas = esperar([&]() {
        for (auto& e : est)
            if (e.hechas.load() != kProductores * kPorProductor) return false;
        return true;
    });
    CHECK(todas, "corren todas las tareas");
    bool desorden = false;
    for (auto& e : est) desorden = desorden || e.desorden;
    CHECK(!desorden, "FIFO por cola (por productor)");
    CHECK(solapes.load() == 0, "nunca dos tareas de la misma cola a la vez");
    CHECK(noActual.load() == 0, "Current()/IsCurrent() dentro de la tarea");
    CHECK(otraActual.load() == 0, "otra cola no es la actual");
    CHECK(webrtc::TaskQueueBase::Current() == nullptr, "afuera no hay cola actual");
    const auto o = colas::resolver({});
    std::printf("   hilos usados: %zu (grupos: %d + %d)\n", hilos.size(), o.hilosPesadas, o.hilosLivianas);
    CHECK(static_cast<int>(hilos.size()) <= o.hilosPesadas + o.hilosLivianas, "40 colas en los hilos de los grupos, no uno por cola");
    colas.clear();
}

void anidadas(webrtc::TaskQueueFactory& f) {
    std::printf("-- 1b. Una tarea que publica en su misma cola y en otra\n");
    Cola a = f.CreateTaskQueue("AudioEncoder", webrtc::TaskQueueFactory::Priority::NORMAL);
    Cola b = f.CreateTaskQueue("EncoderQueue", webrtc::TaskQueueFactory::Priority::NORMAL);
    std::mutex m;
    std::vector<int> orden;
    std::atomic<bool> listo{false};
    a->PostTask([&]() {
        a->PostTask([&]() {
            std::lock_guard<std::mutex> l(m);
            orden.push_back(3);
            b->PostTask([&]() { listo = true; });
        });
        std::lock_guard<std::mutex> l(m);
        orden.push_back(1);
    });
    a->PostTask([&]() {
        std::lock_guard<std::mutex> l(m);
        orden.push_back(2);
    });
    CHECK(esperar([&]() { return listo.load(); }), "la cadena llega a la otra cola");
    CHECK((orden == std::vector<int>{1, 2, 3}), "lo publicado desde adentro va al final");
}

void retardos(webrtc::TaskQueueFactory& f) {
    std::printf("-- 3. PostDelayedTask\n");
    Cola q = f.CreateTaskQueue("EncoderQueue", webrtc::TaskQueueFactory::Priority::NORMAL);
    std::mutex m;
    std::vector<int> orden;
    std::vector<double> demora(4, -1);
    const auto t0 = Reloj::now();
    const int plazos[4] = {30, 10, 20, 5};
    for (int i = 0; i < 4; ++i) {
        auto tarea = [&, i]() {
            std::lock_guard<std::mutex> l(m);
            demora[i] = msDesde(t0) - plazos[i];
            orden.push_back(plazos[i]);
        };
        if (i == 3)
            q->PostDelayedHighPrecisionTask(tarea, webrtc::TimeDelta::Millis(plazos[i]));
        else
            q->PostDelayedTask(tarea, webrtc::TimeDelta::Millis(plazos[i]));
    }
    CHECK(esperar([&]() {
              std::lock_guard<std::mutex> l(m);
              return orden.size() == 4;
          }),
          "corren las cuatro diferidas");
    CHECK((orden == std::vector<int>{5, 10, 20, 30}), "en orden de vencimiento");
    double peor = 0, antes = 0;
    for (double d : demora) {
        peor = std::max(peor, d);
        antes = std::min(antes, d);
    }
    std::printf("   demora: %.2f / %.2f / %.2f / %.2f ms\n", demora[0], demora[1], demora[2], demora[3]);
    CHECK(antes > -0.5, "ninguna antes de tiempo");
    CHECK(peor < 15.0, "con menos de 15 ms de demora");

    // Muchas cortas en varias colas: la demora media (lo que importa al ritmo).
    std::vector<Cola> qs;
    for (int i = 0; i < 8; ++i) qs.push_back(f.CreateTaskQueue("AudioEncoder", webrtc::TaskQueueFactory::Priority::NORMAL));
    std::atomic<int> hechas{0};
    std::atomic<std::int64_t> sumaUs{0};
    for (int r = 0; r < 25; ++r)
        for (auto& c : qs) {
            const auto pedido = Reloj::now();
            const int ms = 1 + (r % 4);
            c->PostDelayedHighPrecisionTask(
                [&, pedido, ms]() {
                    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Reloj::now() - pedido).count() - ms * 1000;
                    sumaUs.fetch_add(us);
                    hechas.fetch_add(1);
                },
                webrtc::TimeDelta::Millis(ms));
        }
    CHECK(esperar([&]() { return hechas.load() == 200; }), "200 diferidas cortas");
    const double media = static_cast<double>(sumaUs.load()) / 200.0 / 1000.0;
    std::printf("   demora media de 200 diferidas de 1-4 ms: %.3f ms\n", media);
    CHECK(media < 3.0, "demora media de menos de 3 ms");
}

void borrar(webrtc::TaskQueueFactory& f) {
    std::printf("-- 4. Delete\n");
    {
        Cola q = f.CreateTaskQueue("DecodingQueue", webrtc::TaskQueueFactory::Priority::HIGH);
        std::atomic<bool> empezo{false}, termino{false};
        std::atomic<int> corridas{0}, destruidas{0};
        q->PostTask([&]() {
            empezo = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            termino = true;
        });
        for (int i = 0; i < 10; ++i) q->PostTask([&, t = Testigo(&destruidas)]() { corridas.fetch_add(1); });
        q->PostDelayedTask([&, t = Testigo(&destruidas)]() { corridas.fetch_add(1); }, webrtc::TimeDelta::Seconds(30));
        esperar([&]() { return empezo.load(); });
        q.reset(); // Delete desde afuera, con una tarea corriendo
        CHECK(termino.load(), "Delete vuelve después de la tarea en curso");
        CHECK(corridas.load() == 0, "las pendientes no corren");
        CHECK(destruidas.load() == 11, "las pendientes y la diferida se destruyen en Delete");
    }
    {
        std::atomic<int> corridas{0};
        std::atomic<bool> listo{false};
        Cola q = f.CreateTaskQueue("AudioEncoder", webrtc::TaskQueueFactory::Priority::NORMAL);
        webrtc::TaskQueueBase* crudo = q.release();
        crudo->PostTask([&, crudo]() {
            crudo->PostTask([&]() { corridas.fetch_add(1); });
            crudo->Delete(); // desde su propia tarea
            listo = true;
        });
        CHECK(esperar([&]() { return listo.load(); }), "Delete desde una tarea de la misma cola");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK(corridas.load() == 0, "y después no corre nada más");
    }
}

void pesadasNoFrenan() {
    std::printf("-- 5. Una decodificación larga no frena al audio\n");
    auto f = colas::crear({1, 1});
    Cola dec = f->CreateTaskQueue("DecodingQueue", webrtc::TaskQueueFactory::Priority::HIGH);
    Cola dec2 = f->CreateTaskQueue("DecodingQueue", webrtc::TaskQueueFactory::Priority::HIGH);
    Cola aud = f->CreateTaskQueue("AudioEncoder", webrtc::TaskQueueFactory::Priority::NORMAL);
    std::atomic<bool> decEmpezo{false};
    dec->PostTask([&]() {
        decEmpezo = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    });
    dec2->PostTask([&]() { std::this_thread::sleep_for(std::chrono::milliseconds(150)); });
    esperar([&]() { return decEmpezo.load(); });
    const auto t0 = Reloj::now();
    std::atomic<double> ms{-1};
    aud->PostTask([&]() { ms = msDesde(t0); });
    esperar([&]() { return ms.load() >= 0; });
    std::printf("   el audio corrió a los %.2f ms (con dos decodificaciones de 150 ms en un hilo)\n", ms.load());
    CHECK(ms.load() < 20.0, "el audio no espera a la decodificación");
    dec.reset();
    dec2.reset();
    aud.reset();
}

void enLote(webrtc::TaskQueueFactory& f) {
    std::printf("-- 6. Despertadas en lote\n");
    std::vector<Cola> qs;
    for (int i = 0; i < 40; ++i) qs.push_back(f.CreateTaskQueue("AudioEncoder", webrtc::TaskQueueFactory::Priority::NORMAL));
    // Tandas como las del audio del retorno: cada 10 ms, una tarea a cada una de las 40 colas.
    auto& fr = ba::diag::frontera("colas.rtc");
    auto& fc = ba::diag::frontera("cola.AudioEncoder");
    const auto r0 = fr.leer();
    const auto c0 = fc.leer();
    std::atomic<int> hechas{0};
    for (int vuelta = 0; vuelta < 20; ++vuelta) {
        for (auto& q : qs)
            for (int k = 0; k < 2; ++k) q->PostTask([&]() { hechas.fetch_add(1); });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(esperar([&]() { return hechas.load() == 1600; }), "1600 tareas");
    const auto r1 = fr.leer();
    const auto c1 = fc.leer();
    const auto tareas = c1.pases - c0.pases;
    const auto despertadas = r1.despertadas - r0.despertadas;
    const auto turnos = r1.pases - r0.pases;
    std::printf("   %llu tareas, %llu turnos, %llu despertadas\n", static_cast<unsigned long long>(tareas),
                static_cast<unsigned long long>(turnos), static_cast<unsigned long long>(despertadas));
    CHECK(tareas == 1600, "el contador de la cola ve todas las tareas");
    CHECK(despertadas * 10 < tareas, "menos de una despertada cada 10 tareas");
    CHECK(turnos < tareas, "varias tareas por turno");
}

} // namespace

int main() {
    std::printf("== test_colas_compartidas ==\n");
    std::printf("núcleos de un bloque de caché: %d\n", colas::nucleosDeUnBloque());
    CHECK(colas::esPesada("DecodingQueue") && !colas::esPesada("AudioEncoder"), "clasificación por nombre");
    const auto r = colas::resolver({});
    CHECK(r.hilosPesadas >= 2 && r.hilosPesadas <= 6 && r.hilosLivianas == 2, "automático: 2-6 pesadas y 2 livianas");
    {
        auto f = colas::crear({});
        fifoYExclusion(*f);
        anidadas(*f);
        retardos(*f);
        borrar(*f);
        enLote(*f);
        std::printf("-- 7. Cierre ordenado\n");
        const auto t0 = Reloj::now();
        f.reset();
        CHECK(msDesde(t0) < 1000.0, "la fábrica se cierra y suelta sus hilos");
    }
    pesadasNoFrenan();
    std::printf("%s (%d fallos)\n", g_fallos == 0 ? "PASA" : "FALLA", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
