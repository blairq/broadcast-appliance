// ============================================================================
// src/gateway/tests/test_webrtc_lote.cpp — los hilos de red y de trabajo de libwebrtc con la
// frontera #4 en lote (rtc/HilosLote.hpp), con rtc::Thread de verdad
//
//   1. Orden y tandas: lo que un hilo de red le manda al de trabajo llega entero y en orden;
//      con «lote», a lo sumo una despertada por tarea del de red; con «separados», una por
//      paquete (el de trabajo se duerme entre uno y otro).
//   2. BlockingCall del de red al de trabajo con una tanda anotada: vuelve en el acto.
//   3. PostDelayedTask en el de trabajo con una tanda anotada: a tiempo.
//   4. E/S adentro de la espera: lo que llega por un socket UDP del de red y se manda al de
//      trabajo no queda esperando (nadie más le manda nada al de red).
//   5. «juntos»: PostTask a sí mismo sin despertar (cuenta en rtc.red.propias), en orden; E/S
//      que se manda a sí mismo; BlockingCall desde afuera.
//   6. Cierre: Stop de todos, rápido.
// ============================================================================
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

#include "api/units/time_delta.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/third_party/sigslot/sigslot.h"
#include "rtc_base/thread.h"
#if defined(WEBRTC_WIN)
#include "rtc_base/win32_socket_init.h"
#endif

#include "diag/Frontera.hpp"
#include "rtc/HilosLote.hpp"
#include "rtc/LoteTrabajo.hpp"

using namespace ba::gateway;
using Reloj = std::chrono::steady_clock;

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

template <typename F> bool esperar(F cond, int topeMs = 5000) {
    const auto t0 = Reloj::now();
    while (!cond()) {
        if (msDesde(t0) > topeMs) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

void girar(int us) {
    const auto t0 = Reloj::now();
    while (std::chrono::duration_cast<std::chrono::microseconds>(Reloj::now() - t0).count() < us) {
    }
}

struct Par {
    std::unique_ptr<rtcred::HiloRed> red;
    std::shared_ptr<lote::Destino> d;
    std::unique_ptr<rtcred::HiloTrabajo> trabajo;
};

Par crear(lote::Modo modo, std::int64_t loteNs, const char* frontera) {
    Par p;
    p.red = std::make_unique<rtcred::HiloRed>(modo, loteNs);
    p.red->SetName("test-red", nullptr);
    p.red->Start();
    p.d = std::make_shared<lote::Destino>(&ba::diag::frontera(frontera, 4, "prueba"));
    p.trabajo = std::make_unique<rtcred::HiloTrabajo>(p.d);
    p.trabajo->SetName("test-trabajo", nullptr);
    p.trabajo->Start();
    return p;
}

/// `tareas` tareas en el de red, cada una manda `porTarea` al de trabajo con `pausaUs` entre
/// uno y otro. Devuelve las despertadas del de trabajo que causó el de red.
std::uint64_t mandar(Par& p, int tareas, int porTarea, int pausaUs, bool* enOrden) {
    std::mutex m;
    std::vector<int> vistos;
    const int total = tareas * porTarea;
    const auto antes = p.d->cuentas();
    for (int t = 0; t < tareas; ++t) {
        p.red->PostTask([&, t] {
            for (int i = 0; i < porTarea; ++i) {
                const int n = t * porTarea + i;
                p.trabajo->PostTask([&, n] {
                    std::lock_guard<std::mutex> l(m);
                    vistos.push_back(n);
                });
                if (pausaUs > 0) girar(pausaUs);
            }
        });
        // Que el de trabajo llegue a dormirse entre tarea y tarea.
        esperar([&] {
            std::lock_guard<std::mutex> l(m);
            return int(vistos.size()) == (t + 1) * porTarea;
        });
        esperar([&] { return p.d->durmiendo(); }, 200);
    }
    std::lock_guard<std::mutex> l(m);
    bool ok = int(vistos.size()) == total;
    for (int i = 0; ok && i < total; ++i) ok = vistos[size_t(i)] == i;
    *enOrden = ok;
    return p.d->cuentas().despertadas - antes.despertadas;
}

void ordenYTandas() {
    std::printf("-- 1. Orden y tandas\n");
    {
        Par p = crear(lote::Modo::Lote, 5'000'000, "test.lote.red-trabajo");
        bool enOrden = false;
        const auto desp = mandar(p, 50, 20, 50, &enOrden);
        std::printf("   lote: 1000 pases, %llu despertadas\n", (unsigned long long)desp);
        CHECK(enOrden, "lote: llegan todas y en orden");
        CHECK(desp <= 60, "lote: a lo sumo una despertada por tarea del de red (50, con margen)");
        CHECK(p.d->cuentas().anotados >= 1000, "lote: se anotaron");
        p.trabajo->Stop();
        p.red->Stop();
    }
    {
        Par p = crear(lote::Modo::Separados, 0, "test.separados.red-trabajo");
        bool enOrden = false;
        const auto desp = mandar(p, 50, 20, 200, &enOrden);
        std::printf("   separados: 1000 pases, %llu despertadas\n", (unsigned long long)desp);
        CHECK(enOrden, "separados: llegan todas y en orden");
        CHECK(desp >= 300, "separados: casi una despertada por pase (como libwebrtc)");
        CHECK(p.d->cuentas().anotados == 0, "separados: nada anotado");
        p.trabajo->Stop();
        p.red->Stop();
    }
}

void blockingCall() {
    std::printf("-- 2. BlockingCall del de red con una tanda anotada\n");
    Par p = crear(lote::Modo::Lote, 1'000'000'000, "test.bc.red-trabajo"); // lote de 1 s: sin el urgente, se notaría
    esperar([&] { return p.d->durmiendo(); });
    std::atomic<bool> antes{false};
    double ms = -1;
    bool antesCorrio = false;
    p.red->BlockingCall([&] {
        p.trabajo->PostTask([&] { antes = true; });
        const auto t0 = Reloj::now();
        const int v = p.trabajo->BlockingCall([] { return 7; });
        ms = msDesde(t0);
        antesCorrio = antes.load() && v == 7;
    });
    std::printf("   BlockingCall: %.2f ms\n", ms);
    CHECK(ms >= 0 && ms < 50.0, "vuelve en el acto (no espera el lote)");
    CHECK(antesCorrio, "lo anotado antes corrió antes (FIFO)");
    p.trabajo->Stop();
    p.red->Stop();
}

void diferidas() {
    std::printf("-- 3. Diferidas con una tanda anotada\n");
    Par p = crear(lote::Modo::Lote, 1'000'000'000, "test.dif.red-trabajo");
    std::atomic<bool> hecha{false};
    Reloj::time_point cuando{};
    const auto t0 = Reloj::now();
    p.trabajo->PostDelayedTask(
        [&] {
            cuando = Reloj::now();
            hecha = true;
        },
        webrtc::TimeDelta::Millis(20));
    // Una tanda anotada (el de red queda ocupado: no se duerme ni suelta).
    p.red->PostTask([&] {
        p.trabajo->PostTask([] {});
        girar(60'000);
    });
    CHECK(esperar([&] { return hecha.load(); }, 2000), "la diferida corre");
    const double ms = std::chrono::duration<double, std::milli>(cuando - t0).count();
    std::printf("   diferida de 20 ms: %.1f ms\n", ms);
    CHECK(ms >= 19.0 && ms < 40.0, "a tiempo");
    p.trabajo->Stop();
    p.red->Stop();
}

/// Lee todo lo que haya en el socket y le manda una tarea a `destino` por datagrama.
class Receptor : public sigslot::has_slots<> {
   public:
    Receptor(rtc::Thread* destino, std::atomic<int>* recibidos) : destino_(destino), recibidos_(recibidos) {}
    void leer(rtc::Socket* s) {
        char buf[2048];
        rtc::SocketAddress de;
        while (s->RecvFrom(buf, sizeof buf, &de, nullptr) > 0) {
            std::atomic<int>* r = recibidos_;
            destino_->PostTask([r] { r->fetch_add(1); });
        }
    }

   private:
    rtc::Thread* destino_;
    std::atomic<int>* recibidos_;
};

/// Dos sockets UDP en el socket server del de red: uno manda `n` datagramas al otro, cuyo
/// receptor manda cada uno a `destino`.
bool porSockets(rtcred::HiloRed& red, rtc::Thread* destino, int n, const char* que) {
    std::atomic<int> recibidos{0};
    std::unique_ptr<rtc::Socket> rx, tx;
    std::unique_ptr<Receptor> receptor;
    red.BlockingCall([&] {
        rx.reset(red.socketserver()->CreateSocket(AF_INET, SOCK_DGRAM));
        tx.reset(red.socketserver()->CreateSocket(AF_INET, SOCK_DGRAM));
        rx->Bind(rtc::SocketAddress("127.0.0.1", 0));
        tx->Bind(rtc::SocketAddress("127.0.0.1", 0));
        receptor = std::make_unique<Receptor>(destino, &recibidos);
        rx->SignalReadEvent.connect(receptor.get(), &Receptor::leer);
    });
    const rtc::SocketAddress a = red.BlockingCall([&] { return rx->GetLocalAddress(); });
    // De a uno, con pausas: el de red vuelve a dormirse entre datagramas y lo que manda al
    // destino sale de ADENTRO de su espera de E/S.
    bool ok = true;
    for (int i = 0; i < n && ok; ++i) {
        red.PostTask([&] { tx->SendTo("x", 1, a); });
        ok = esperar([&] { return recibidos.load() == i + 1; }, 500);
    }
    std::printf("   %s: %d de %d\n", que, recibidos.load(), n);
    red.BlockingCall([&] {
        rx.reset();
        tx.reset();
        receptor.reset();
    });
    return ok;
}

void entradaSalida() {
    std::printf("-- 4. E/S adentro de la espera\n");
    Par p = crear(lote::Modo::Lote, 1'000'000'000, "test.es.red-trabajo");
    CHECK(porSockets(*p.red, p.trabajo.get(), 30, "lote, del socket al de trabajo"),
          "lo que se manda desde la vuelta de E/S no queda esperando");
    p.trabajo->Stop();
    p.red->Stop();
}

void juntos() {
    std::printf("-- 5. Juntos\n");
    auto red = std::make_unique<rtcred::HiloRed>(lote::Modo::Juntos, 0);
    red->SetName("test-juntos", nullptr);
    red->Start();
    ba::diag::Frontera& propias = ba::diag::frontera("rtc.red.propias", 4);
    const auto antes = propias.leer();
    std::mutex m;
    std::vector<int> vistos;
    red->PostTask([&] {
        for (int i = 0; i < 200; ++i)
            red->PostTask([&, i] {
                std::lock_guard<std::mutex> l(m);
                vistos.push_back(i);
            });
    });
    CHECK(esperar([&] {
              std::lock_guard<std::mutex> l(m);
              return vistos.size() == 200;
          }),
          "las 200 a sí mismo corren");
    {
        std::lock_guard<std::mutex> l(m);
        bool ok = true;
        for (int i = 0; i < 200; ++i) ok = ok && vistos[size_t(i)] == i;
        CHECK(ok, "en orden");
    }
    CHECK(propias.leer().pases - antes.pases >= 200, "sin despertarse de verdad (rtc.red.propias)");
    const auto t0 = Reloj::now();
    CHECK(red->BlockingCall([] { return 3; }) == 3 && msDesde(t0) < 50.0, "BlockingCall desde afuera, en el acto");
    CHECK(porSockets(*red, red.get(), 30, "juntos, del socket a sí mismo"),
          "lo que se manda a sí mismo desde la vuelta de E/S corre");
    red->Stop();
}

void cierre() {
    std::printf("-- 6. Cierre\n");
    Par p = crear(lote::Modo::Lote, 1'000'000'000, "test.cierre.red-trabajo");
    std::atomic<int> n{0};
    p.red->PostTask([&] {
        for (int i = 0; i < 10; ++i) p.trabajo->PostTask([&] { n.fetch_add(1); });
    });
    CHECK(esperar([&] { return n.load() == 10; }, 2000), "el de red suelta antes de dormirse");
    const auto t0 = Reloj::now();
    p.trabajo->Stop();
    p.red->Stop();
    CHECK(msDesde(t0) < 1000.0, "Stop de los dos, rápido");
}

} // namespace

int main() {
#if defined(WEBRTC_WIN)
    rtc::WinsockInitializer winsock;
#endif
    ordenYTandas();
    blockingCall();
    diferidas();
    entradaSalida();
    juntos();
    cierre();
    std::printf("%s (%d fallos)\n", g_fallos == 0 ? "PASA" : "FALLA", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
