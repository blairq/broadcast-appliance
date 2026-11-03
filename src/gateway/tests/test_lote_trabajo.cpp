// test_lote_trabajo.cpp — el despertar en lote de la frontera #4 (rtc/LoteTrabajo.hpp), sin
// libwebrtc: un bucle de juguete que hace lo mismo que rtc::Thread (encola, avisa, y antes de
// dormir mira su cola y el plazo de su próxima diferida) con un lote::Destino como espera.
//
//   1. Orden: miles de tareas de un remitente en lote llegan todas y en orden.
//   2. Una despertada por tanda: el dormido no se despierta al anotar, sí al soltar, y atiende
//      la tanda entera de una vez.
//   3. Sin lote (separados): una despertada por aviso, como libwebrtc.
//   4. Urgente (BlockingCall): despierta en el acto aunque el remitente tenga una tanda
//      anotada; la tanda sigue esperando.
//   5. Diferidas: el plazo de la próxima se respeta con una tanda anotada sin soltar.
//   6. El dueño avisándose a sí mismo: ni cuenta ni despierta.
//   7. El tope del lote: soltarSiVencio no suelta antes de loteNs; anotar suelta si venció.
//   8. Cierre: desinstalar suelta lo anotado; parar desde otro hilo despierta en el acto.
//   9. La frontera: pases, despertadas y espera.
#include "rtc/LoteTrabajo.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "diag/Frontera.hpp"

namespace lt = ba::gateway::lote;
using Reloj = std::chrono::steady_clock;

static int fallas = 0;
#define CHECK(c, msg)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FALLA (línea %d): %s\n", __LINE__, msg);                                                      \
            ++fallas;                                                                                                  \
        } else {                                                                                                       \
            std::printf("ok: %s\n", msg);                                                                              \
        }                                                                                                              \
    } while (0)

namespace {

double msDesde(Reloj::time_point t0) { return std::chrono::duration<double, std::milli>(Reloj::now() - t0).count(); }

template <typename F> bool esperarQue(F cond, int topeMs = 3000) {
    const auto t0 = Reloj::now();
    while (!cond()) {
        if (msDesde(t0) > topeMs) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return true;
}

/// Un hilo de trabajo de juguete: como rtc::Thread con EsperaTrabajo.
class Bucle {
   public:
    explicit Bucle(ba::diag::Frontera* f = nullptr) : d_(std::make_shared<lt::Destino>(f)) {
        hilo_ = std::thread([this] { correr(); });
        esperarQue([this] { return listo_.load(); });
    }
    ~Bucle() { parar(); }

    void post(std::function<void()> f) {
        {
            std::lock_guard<std::mutex> l(m_);
            cola_.push_back(std::move(f));
        }
        d_->avisar(); // como Thread::PostTaskImpl → WakeUpSocketServer
    }
    void postDiferida(int ms, std::function<void()> f) {
        {
            std::lock_guard<std::mutex> l(m_);
            diferidas_.push_back({Reloj::now() + std::chrono::milliseconds(ms), std::move(f)});
        }
        d_->avisar();
    }
    void parar() {
        if (!hilo_.joinable()) return;
        salir_ = true;
        d_->avisar();
        hilo_.join();
    }
    lt::Destino& destino() { return *d_; }
    int vueltas() const { return vueltas_.load(); }

   private:
    struct Diferida {
        Reloj::time_point cuando;
        std::function<void()> f;
    };
    void correr() {
        d_->soyElDueno();
        listo_ = true;
        while (!salir_) {
            std::function<void()> f;
            std::int64_t plazoNs = -1;
            {
                std::lock_guard<std::mutex> l(m_);
                const auto ahora = Reloj::now();
                for (auto it = diferidas_.begin(); it != diferidas_.end(); ++it)
                    if (it->cuando <= ahora) {
                        cola_.push_back(std::move(it->f));
                        diferidas_.erase(it);
                        break;
                    }
                if (!cola_.empty()) {
                    f = std::move(cola_.front());
                    cola_.pop_front();
                } else {
                    for (const Diferida& d : diferidas_) {
                        const std::int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(d.cuando - ahora).count();
                        if (plazoNs < 0 || ns < plazoNs) plazoNs = std::max<std::int64_t>(0, ns);
                    }
                }
            }
            if (f) {
                f();
                continue;
            }
            vueltas_.fetch_add(1);
            d_->esperar(plazoNs);
        }
    }

    std::shared_ptr<lt::Destino> d_;
    std::mutex m_;
    std::deque<std::function<void()>> cola_;
    std::vector<Diferida> diferidas_;
    std::atomic<bool> salir_{false}, listo_{false};
    std::atomic<int> vueltas_{0};
    std::thread hilo_;
};

/// Corre `f` en un hilo con un remitente instalado.
void comoRemitente(bool enLote, std::int64_t loteNs, const std::function<void(lt::Remitente&)>& f) {
    std::thread t([&] {
        lt::Remitente r(enLote, loteNs);
        r.instalar();
        f(r);
        r.desinstalar();
    });
    t.join();
}

void orden() {
    std::printf("-- 1. Orden\n");
    Bucle b;
    std::vector<int> vistos;
    std::mutex vm;
    constexpr int kN = 5000;
    comoRemitente(true, 500000, [&](lt::Remitente& r) {
        for (int i = 0; i < kN; ++i) {
            b.post([&, i] {
                std::lock_guard<std::mutex> l(vm);
                vistos.push_back(i);
            });
            if (i % 37 == 36) r.soltarSiVencio(); // fin de una «tarea» del de red
        }
    });
    CHECK(esperarQue([&] {
              std::lock_guard<std::mutex> l(vm);
              return int(vistos.size()) == kN;
          }),
          "llegan todas");
    bool enOrden = true;
    for (int i = 0; i < int(vistos.size()); ++i) enOrden = enOrden && vistos[size_t(i)] == i;
    CHECK(enOrden, "en el orden en que se mandaron");
}

void unaPorTanda() {
    std::printf("-- 2. Una despertada por tanda\n");
    Bucle b;
    std::atomic<int> hechas{0};
    CHECK(esperarQue([&] { return b.destino().durmiendo(); }), "el de trabajo duerme");
    const auto antes = b.destino().cuentas();
    comoRemitente(true, 50'000'000, [&](lt::Remitente& r) {
        for (int i = 0; i < 20; ++i) b.post([&] { hechas.fetch_add(1); });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK(hechas.load() == 0 && b.destino().durmiendo(), "anotar no lo despierta");
        CHECK(r.hayAnotados(), "la tanda está anotada");
        r.soltar();
    });
    CHECK(esperarQue([&] { return hechas.load() == 20; }), "al soltar atiende la tanda entera");
    const auto despues = b.destino().cuentas();
    CHECK(despues.despertadas - antes.despertadas == 1, "una sola despertada para 20 tareas");
    CHECK(despues.anotados - antes.anotados == 20, "20 anotados");
}

void separados() {
    std::printf("-- 3. Sin lote: una por aviso\n");
    Bucle b;
    std::atomic<int> hechas{0};
    CHECK(esperarQue([&] { return b.destino().durmiendo(); }), "el de trabajo duerme");
    const auto antes = b.destino().cuentas();
    comoRemitente(false, 0, [&](lt::Remitente&) {
        for (int i = 0; i < 10; ++i) {
            b.post([&] { hechas.fetch_add(1); });
            esperarQue([&] { return hechas.load() == i + 1 && b.destino().durmiendo(); });
        }
    });
    const auto despues = b.destino().cuentas();
    CHECK(hechas.load() == 10, "atiende las 10");
    CHECK(despues.despertadas - antes.despertadas == 10, "10 despertadas (como libwebrtc)");
    CHECK(despues.anotados == antes.anotados, "nada anotado");
}

void urgente() {
    std::printf("-- 4. Urgente (BlockingCall)\n");
    Bucle b;
    std::atomic<int> tanda{0};
    std::atomic<bool> urgenteHecha{false};
    CHECK(esperarQue([&] { return b.destino().durmiendo(); }), "el de trabajo duerme");
    comoRemitente(true, 50'000'000, [&](lt::Remitente& r) {
        b.post([&] { tanda.fetch_add(1); });
        // Lo que hace HiloTrabajo::BlockingCallImpl: el aviso es urgente.
        {
            lt::Remitente::Urgente u;
            b.post([&] { urgenteHecha = true; });
        }
        const auto t0 = Reloj::now();
        CHECK(esperarQue([&] { return urgenteHecha.load(); }, 1000), "la urgente corre sin esperar la tanda");
        CHECK(msDesde(t0) < 50.0, "en el acto");
        // La anotada ya estaba en la cola: corrió antes que la urgente (FIFO).
        CHECK(tanda.load() == 1, "lo de antes en la cola corre primero");
        CHECK(!lt::Remitente::urgente(), "Urgente se deshace al salir");
        r.soltar();
    });
}

void diferidas() {
    std::printf("-- 5. Diferidas con una tanda anotada\n");
    Bucle b;
    std::atomic<bool> hecha{false};
    Reloj::time_point cuando{};
    const auto t0 = Reloj::now();
    b.postDiferida(30, [&] {
        cuando = Reloj::now();
        hecha = true;
    });
    comoRemitente(true, 500'000'000, [&](lt::Remitente& r) {
        b.post([] {});
        CHECK(esperarQue([&] { return hecha.load(); }, 2000), "la diferida corre sin soltar la tanda");
        r.soltar();
    });
    const double ms = std::chrono::duration<double, std::milli>(cuando - t0).count();
    std::printf("   diferida de 30 ms: %.1f ms\n", ms);
    CHECK(ms >= 29.0 && ms < 30.0 + 20.0, "a tiempo (no antes, y con la precisión de siempre)");
}

void dueno() {
    std::printf("-- 6. El dueño a sí mismo\n");
    Bucle b;
    std::atomic<bool> hecha{false};
    std::atomic<std::uint64_t> avisosAdentro{0};
    b.post([&] {
        const auto a = b.destino().cuentas().avisos;
        b.post([&] { hecha = true; }); // el dueño se manda algo
        avisosAdentro = b.destino().cuentas().avisos - a;
    });
    CHECK(esperarQue([&] { return hecha.load(); }), "la tarea que se mandó a sí mismo corre");
    CHECK(avisosAdentro.load() == 0, "el aviso del dueño no cuenta ni despierta");
    CHECK(b.destino().esElDueno() == false, "desde otro hilo no es el dueño");
}

void tope() {
    std::printf("-- 7. El tope del lote\n");
    Bucle b;
    std::atomic<int> hechas{0};
    CHECK(esperarQue([&] { return b.destino().durmiendo(); }), "el de trabajo duerme");
    comoRemitente(true, 20'000'000, [&](lt::Remitente& r) { // 20 ms
        b.post([&] { hechas.fetch_add(1); });
        r.soltarSiVencio();
        CHECK(r.hayAnotados(), "antes del tope no suelta al terminar la tarea");
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        r.soltarSiVencio();
        CHECK(!r.hayAnotados(), "pasado el tope suelta");
        CHECK(esperarQue([&] { return hechas.load() == 1; }), "y corre");
        // anotar también suelta si la primera ya esperó el tope (una tarea larga del de red)
        b.post([&] { hechas.fetch_add(1); });
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        b.post([&] { hechas.fetch_add(1); });
        CHECK(!r.hayAnotados(), "anotar suelta si venció");
    });
    CHECK(esperarQue([&] { return hechas.load() == 3; }), "corren todas");
    comoRemitente(true, 0, [&](lt::Remitente& r) {
        b.post([&] { hechas.fetch_add(1); });
        r.soltarSiVencio();
        CHECK(!r.hayAnotados(), "lote 0: suelta al terminar cada tarea");
    });
    CHECK(esperarQue([&] { return hechas.load() == 4; }), "y corre");
}

void cierre() {
    std::printf("-- 8. Cierre\n");
    {
        Bucle b;
        std::atomic<int> hechas{0};
        CHECK(esperarQue([&] { return b.destino().durmiendo(); }), "el de trabajo duerme");
        std::thread t([&] {
            lt::Remitente r(true, 1'000'000'000);
            r.instalar();
            for (int i = 0; i < 5; ++i) b.post([&] { hechas.fetch_add(1); });
            r.desinstalar(); // el hilo de red termina: suelta
            CHECK(lt::Remitente::delHilo() == nullptr, "desinstalado");
        });
        t.join();
        CHECK(esperarQue([&] { return hechas.load() == 5; }), "desinstalar suelta lo anotado");
        const auto t0 = Reloj::now();
        b.parar();
        CHECK(msDesde(t0) < 100.0, "parar desde otro hilo despierta en el acto");
    }
    {
        // El destino puede irse antes que el remitente suelte: el remitente lo tiene por
        // shared_ptr.
        auto b = std::make_unique<Bucle>();
        lt::Remitente r(true, 1'000'000'000);
        std::thread t([&] {
            r.instalar();
            b->post([] {});
        });
        t.join();
        b.reset();
        std::thread t2([&] { r.soltar(); }); // despierta a un destino sin hilo: no pasa nada
        t2.join();
        CHECK(true, "soltar con el destino ya parado no rompe");
    }
}

void frontera() {
    std::printf("-- 9. La frontera\n");
    ba::diag::Frontera& f = ba::diag::frontera("test.red-trabajo", 4, "prueba");
    const auto antes = f.leer();
    Bucle b(&f);
    std::atomic<int> hechas{0};
    CHECK(esperarQue([&] { return b.destino().durmiendo(); }), "el de trabajo duerme");
    comoRemitente(true, 50'000'000, [&](lt::Remitente& r) {
        for (int tanda = 0; tanda < 10; ++tanda) {
            for (int i = 0; i < 8; ++i) b.post([&] { hechas.fetch_add(1); });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            r.soltar();
            esperarQue([&] { return hechas.load() == (tanda + 1) * 8 && b.destino().durmiendo(); });
        }
    });
    const auto despues = f.leer();
    const auto pases = despues.pases - antes.pases;
    const auto desp = despues.despertadas - antes.despertadas;
    std::printf("   pases %llu, despertadas %llu, espera media %.0f µs\n", (unsigned long long)pases,
                (unsigned long long)desp,
                despues.esperaN > antes.esperaN
                    ? double(despues.esperaNs - antes.esperaNs) / double(despues.esperaN - antes.esperaN) / 1000.0
                    : 0.0);
    CHECK(pases == 80, "80 pases");
    CHECK(desp == 10, "10 despertadas (8 pases por despertada)");
    CHECK(despues.esperaN - antes.esperaN == 10 && despues.esperaNs - antes.esperaNs >= 10ull * 1'000'000ull,
          "la espera de cada tanda (≥ 1 ms acá) se mide");
}

void fronteraDelRemitente() {
    std::printf("-- 10. El remitente con su frontera (el reloj del audio)\n");
    ba::diag::Frontera& deTrabajo = ba::diag::frontera("test.destino", 4, "prueba");
    ba::diag::Frontera& delReloj = ba::diag::frontera("test.reloj.trabajo", 10, "prueba");
    const auto a0 = deTrabajo.leer(), b0 = delReloj.leer();
    Bucle b(&deTrabajo);
    std::atomic<int> hechas{0};
    CHECK(esperarQue([&] { return b.destino().durmiendo(); }), "el de trabajo duerme");
    std::thread t([&] {
        lt::Remitente r(true, 2'000'000, &delReloj);
        r.instalar();
        for (int i = 0; i < 40; ++i) b.post([&] { hechas.fetch_add(1); }); // 40 invitados en una vuelta
        r.soltar();                                                          // fin de la vuelta
        r.desinstalar();
    });
    t.join();
    CHECK(esperarQue([&] { return hechas.load() == 40; }), "corren las 40");
    const auto a1 = deTrabajo.leer(), b1 = delReloj.leer();
    CHECK(b1.pases - b0.pases == 40 && b1.despertadas - b0.despertadas == 1, "40 pases y 1 despertada en la del reloj");
    CHECK(a1.pases == a0.pases && a1.despertadas == a0.despertadas, "nada en la del destino (la #4 es solo de la red)");
}

void modos() {
    std::printf("-- 0. Modos\n");
    CHECK(lt::modoDeTexto("lote") == lt::Modo::Lote, "lote");
    CHECK(lt::modoDeTexto("juntos") == lt::Modo::Juntos, "juntos");
    CHECK(lt::modoDeTexto("separados") == lt::Modo::Separados, "separados");
    CHECK(lt::modoDeTexto("cualquiera") == lt::Modo::Lote, "lo desconocido: lote");
    CHECK(std::string(lt::nombre(lt::Modo::Juntos)) == "juntos", "nombre");
}

} // namespace

int main() {
    modos();
    orden();
    unaPorTanda();
    separados();
    urgente();
    diferidas();
    dueno();
    tope();
    cierre();
    frontera();
    fronteraDelRemitente();
    std::printf("%s (%d fallas)\n", fallas == 0 ? "PASA" : "FALLA", fallas);
    return fallas == 0 ? 0 : 1;
}
