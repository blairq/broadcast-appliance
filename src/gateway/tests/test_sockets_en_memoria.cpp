// test_sockets_en_memoria.cpp — la entrega en memoria entre el TURN, la red de WebRTC y el
// túnel (rtc/Conmutador.hpp; F1/F2 de docs/planes/PLAN-GATEWAY-FRAGMENTOS.md)
//
// Sin libwebrtc: los «hilos de red» son hilos con una cola de tareas (lo que haría
// rtc::Thread::PostTask) y los extremos, receptores de prueba.
//   1. Mismo hilo: se entrega ahí mismo, en orden; lo que un receptor manda mientras recibe
//      NO se anida: sale al terminar (nunca hay dos entregas en la pila).
//   2. Ida y vuelta (TURN ↔ WebRTC) en el mismo hilo: llega, contesta, vuelve.
//   3. Otro hilo: 20 000 paquetes de A a B llegan todos y en orden, con muchas menos
//      despertadas que paquetes (en lote: se despierta solo si no estaba avisado).
//   4. Un punto cerrado no recibe nada más (ni lo que estaba en camino).
//   5. El productor de afuera (el túnel): una despertada por hilo y por tanda.
//   6. Buscar: por ip:puerto, por 0.0.0.0:puerto y el puerto de 127.0.0.1 (túnel); la
//      generación sube con cada punto nuevo.
#include "rtc/Conmutador.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace mem = ba::gateway::memoria;

static int fallas = 0;
#define CHECK(c, msg)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FALLA (línea %d): %s\n", __LINE__, msg);                                                      \
            ++fallas;                                                                                                  \
        }                                                                                                              \
    } while (0)

namespace {

/// Un «hilo de red»: corre las tareas que le postean, en orden.
class Hilo {
   public:
    Hilo() : t_([this] { bucle(); }) {}
    ~Hilo() {
        {
            std::lock_guard<std::mutex> l(m_);
            fin_ = true;
        }
        cv_.notify_all();
        t_.join();
    }
    void postear(std::function<void()> f) {
        {
            std::lock_guard<std::mutex> l(m_);
            q_.push_back(std::move(f));
        }
        cv_.notify_one();
    }
    void esperando(const std::function<void()>& f) {
        std::mutex m;
        std::condition_variable cv;
        bool listo = false;
        postear([&] {
            f();
            std::lock_guard<std::mutex> l(m);
            listo = true;
            cv.notify_one();
        });
        std::unique_lock<std::mutex> l(m);
        cv.wait(l, [&] { return listo; });
    }
    /// Espera a que corra todo lo que había (y lo que eso posteó, un par de vueltas).
    void vaciar() {
        for (int i = 0; i < 4; ++i) esperando([] {});
    }

   private:
    void bucle() {
        for (;;) {
            std::function<void()> f;
            {
                std::unique_lock<std::mutex> l(m_);
                cv_.wait(l, [this] { return fin_ || !q_.empty(); });
                if (q_.empty()) return;
                f = std::move(q_.front());
                q_.pop_front();
            }
            f();
        }
    }
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> q_;
    bool fin_ = false;
    std::thread t_;
};

/// Un extremo de prueba: anota lo que recibe y si alguna vez se anidó.
struct Receptor : mem::Receptor {
    std::vector<std::vector<uint8_t>> recibidos;
    std::vector<uint32_t> ips;
    std::vector<uint16_t> puertos;
    std::vector<mem::Via> vias;
    int dentro = 0;
    bool anidado = false;
    std::function<void(const uint8_t*, size_t)> alRecibir;
    void recibirEnMemoria(const uint8_t* d, size_t n, uint32_t ip, uint16_t puerto, mem::Via via) override {
        if (dentro++ > 0) anidado = true;
        recibidos.emplace_back(d, d + n);
        ips.push_back(ip);
        puertos.push_back(puerto);
        vias.push_back(via);
        if (alRecibir) alRecibir(d, n);
        --dentro;
    }
};

std::vector<uint8_t> bytes(const char* s) { return std::vector<uint8_t>(s, s + std::strlen(s)); }

} // namespace

int main() {
    Hilo h0, h1;
    std::atomic<int> posteos{0};
    mem::Conmutador c(2, 1, [&](int hilo, std::function<void()> f) {
        ++posteos;
        (hilo == 0 ? h0 : h1).postear(std::move(f));
    });
    constexpr uint32_t kLan = 0xC0A8010A, kLocal = 0x7F000001;

    // 6. Registro y búsqueda.
    Receptor webrtc0, relay0, webrtc1, cualquiera;
    const uint64_t g0 = c.generacion();
    mem::PuntoPtr pW0 = c.abrir(kLan, 40000, 0, mem::Clase::WebrtcUdp, &webrtc0);
    mem::PuntoPtr pR0 = c.abrir(kLan, 50000, 0, mem::Clase::RelayTurn, &relay0);
    mem::PuntoPtr pW1 = c.abrir(kLocal, 40001, 1, mem::Clase::WebrtcUdp, &webrtc1);
    mem::PuntoPtr pAny = c.abrir(0, 40002, 1, mem::Clase::WebrtcUdp, &cualquiera);
    CHECK(c.generacion() == g0 + 4, "la generación sube con cada punto nuevo");
    CHECK(c.buscar(kLan, 40000) == pW0, "buscar por ip:puerto");
    CHECK(c.buscar(kLan, 40002) == pAny, "buscar cae en el enlazado a 0.0.0.0");
    CHECK(c.buscar(kLan, 40003) == nullptr, "lo que no está no aparece");
    CHECK(c.buscarPuerto(40001) == pW1 && c.buscarPuerto(40002) == pAny, "el túnel busca por puerto (127.0.0.1 o 0.0.0.0)");
    CHECK(c.buscarPuerto(50000) == nullptr, "el túnel no encuentra relays del TURN");

    // 1 y 2. Mismo hilo, ida y vuelta: el «TURN» manda a WebRTC dentro de un manejador
    // (Ámbito) y WebRTC contesta mientras recibe.
    webrtc0.alRecibir = [&](const uint8_t* d, size_t n) {
        std::vector<uint8_t> r(d, d + n);
        r.push_back('!');
        c.mandar(0, pR0, r.data(), r.size(), kLan, 40000);
    };
    relay0.alRecibir = [&](const uint8_t*, size_t) {
        // Mientras recibe el relay (el TURN), WebRTC no tiene que estar en la pila.
        CHECK(webrtc0.dentro == 0, "el relay recibe con WebRTC fuera de la pila");
    };
    h0.esperando([&] {
        mem::Conmutador::Ambito a(c);
        for (const char* m : {"uno", "dos", "tres"}) {
            const auto b = bytes(m);
            c.mandar(0, pW0, b.data(), b.size(), kLan, 50000);
        }
        CHECK(webrtc0.recibidos.empty(), "adentro de un ámbito no se entrega todavía");
    });
    CHECK(webrtc0.recibidos.size() == 3 && webrtc0.recibidos[0] == bytes("uno") && webrtc0.recibidos[2] == bytes("tres"),
          "mismo hilo: llega todo, en orden, al salir del ámbito");
    CHECK(webrtc0.ips[0] == kLan && webrtc0.puertos[0] == 50000, "llega desde la dirección del relay");
    CHECK(relay0.recibidos.size() == 3 && relay0.recibidos[1] == bytes("dos!"), "ida y vuelta: la respuesta vuelve al relay");
    CHECK(!webrtc0.anidado && !relay0.anidado, "nunca se anida una entrega adentro de otra");
    // Sin ámbito: derecho.
    h0.esperando([&] {
        const auto b = bytes("directo");
        c.mandar(0, pW0, b.data(), b.size(), kLan, 50000);
        CHECK(webrtc0.recibidos.size() == 4, "sin ámbito: se entrega ahí mismo");
    });
    webrtc0.alRecibir = nullptr;
    relay0.alRecibir = nullptr;

    // 3. Otro hilo, en lote y en orden.
    constexpr int kN = 20000;
    const int posteosAntes = posteos.load();
    h0.esperando([&] {
        mem::Conmutador::Ambito a(c);
        for (int i = 0; i < kN; ++i) {
            uint8_t b[8];
            std::memcpy(b, &i, sizeof(i));
            std::memset(b + 4, 0x55, 4);
            // Si la cola está llena, se espera un poco (la prueba quiere todos).
            while (!c.mandar(0, pW1, b, sizeof(b), kLan, 50000)) std::this_thread::yield();
        }
    });
    h1.vaciar();
    bool enOrden = webrtc1.recibidos.size() == size_t(kN);
    for (size_t i = 0; enOrden && i < webrtc1.recibidos.size(); ++i) {
        int v = -1;
        std::memcpy(&v, webrtc1.recibidos[i].data(), sizeof(v));
        enOrden = v == int(i);
    }
    const int despertadas = posteos.load() - posteosAntes;
    std::printf("a otro hilo: %zu de %d, %d despertadas\n", webrtc1.recibidos.size(), kN, despertadas);
    CHECK(enOrden, "a otro hilo: llegan todos y en orden");
    CHECK(despertadas > 0 && despertadas < kN / 4, "a otro hilo: en lote (muchas menos despertadas que paquetes)");
    CHECK(!webrtc1.anidado, "a otro hilo: sin anidar");

    // 4. Cerrado: lo que estaba en camino se descarta y lo nuevo no sale.
    {
        const size_t antes = webrtc1.recibidos.size();
        h0.esperando([&] {
            const auto b = bytes("en camino");
            c.mandar(0, pW1, b.data(), b.size(), kLan, 50000);
        });
        h1.esperando([&] { c.cerrar(pW1); }); // puede llegar antes o después de la entrega
        h1.vaciar();
        const size_t llegados = webrtc1.recibidos.size() - antes;
        h0.esperando([&] {
            const auto b = bytes("tarde");
            CHECK(!c.mandar(0, pW1, b.data(), b.size(), kLan, 50000), "a un punto cerrado no se manda");
        });
        h1.vaciar();
        CHECK(webrtc1.recibidos.size() - antes == llegados && llegados <= 1, "un punto cerrado no recibe más");
        CHECK(c.buscar(kLocal, 40001) == nullptr, "cerrado: ya no se encuentra");
    }

    // 5. El túnel (productor de afuera, índice 2): una despertada por hilo y por tanda.
    {
        const int antes = posteos.load();
        for (int tanda = 0; tanda < 10; ++tanda) {
            for (int i = 0; i < 50; ++i) {
                const auto b = bytes("wg");
                c.mandar(2, pAny, b.data(), b.size(), 0x0A630002, uint16_t(5000 + i), mem::Via::Tunel);
            }
            c.terminarTanda(2);
            h1.vaciar();
        }
        CHECK(cualquiera.recibidos.size() == 500, "túnel: llegan todos");
        CHECK(posteos.load() - antes == 10, "túnel: una despertada por tanda");
        CHECK(cualquiera.vias.back() == mem::Via::Tunel && cualquiera.ips.back() == 0x0A630002,
              "túnel: llega marcado y desde el invitado");
    }

    const auto e = c.estado();
    std::printf("estado: %s\n", e.dump().c_str());
    CHECK(e.value("cruzados", uint64_t(0)) >= uint64_t(kN), "estado: cuenta los cruzados");

    c.cerrar(pW0);
    c.cerrar(pR0);
    c.cerrar(pAny);
    std::printf(fallas ? "test_sockets_en_memoria: %d fallas\n" : "test_sockets_en_memoria: todo ok\n", fallas);
    return fallas ? 1 : 0;
}
