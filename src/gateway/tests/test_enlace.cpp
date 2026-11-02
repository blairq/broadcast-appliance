// test_enlace.cpp — el medidor del enlace (net/Enlace, net/EnlaceMedidor, net/EnlaceServicio)
//
//   lo puro: el protocolo ida y vuelta (y lo que no es nuestro o viene corto), el jitter
//   de RFC 3550 (con relojes desfasados), la tasa entregada, la pérdida, los percentiles,
//   la rampa, cuándo satura un escalón, el pedido que acepta el servicio y el instalador
//   del eco (lleva el script entero);
//   el medidor contra el eco DE VERDAD (tools/vps/ba-enlace-eco.py) por UDP local: hola,
//   reposo, rampa de subida y bajada con un tope chico, y una tasa fija; sin eco, el
//   error dice que falta instalarlo. Sin python3: SKIP de esa parte.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "net/Enlace.hpp"
#include "net/EnlaceMedidor.hpp"
#include "net/EnlaceServicio.hpp"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace ba::gateway::net::enlace;
using json = nlohmann::json;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                             \
    do {                                                        \
        if (!(c)) {                                             \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__); \
            ++g_fallas;                                         \
        } else {                                                \
            std::printf("ok: %s\n", m);                         \
        }                                                       \
    } while (0)

bool cerca(double a, double b, double tol) { return a > b - tol && a < b + tol; }

void protocolo() {
    Mensaje m;
    m.tipo = Tipo::Dato;
    m.prueba = 0xABCDEF01;
    m.seq = 77;
    m.tUs = 0x0102030405060708ULL;
    const std::string d = codificar(m, 1200);
    CHECK(d.size() == 1200, "un dato se rellena hasta el tamaño pedido");
    const auto r = decodificar(d);
    CHECK(r && r->tipo == Tipo::Dato && r->prueba == m.prueba && r->seq == 77 && r->tUs == m.tUs, "dato ida y vuelta");

    Mensaje inf;
    inf.tipo = Tipo::Informe;
    inf.prueba = 9;
    inf.recibidos = 1000;
    inf.enviados = 1010;
    inf.seqMax = 1009;
    inf.jitterUs = 1234;
    inf.desordenados = 3;
    inf.bytes = 1200000;
    inf.primeroUs = 5;
    inf.ultimoUs = 1000005;
    const auto ri = decodificar(codificar(inf));
    CHECK(ri && ri->recibidos == 1000 && ri->enviados == 1010 && ri->seqMax == 1009 && ri->jitterUs == 1234 &&
              ri->desordenados == 3 && ri->bytes == 1200000 && ri->primeroUs == 5 && ri->ultimoUs == 1000005,
          "informe ida y vuelta");

    Mensaje b;
    b.tipo = Tipo::BajadaPedido;
    b.prueba = 4;
    b.kbps = 20000;
    b.ms = 1500;
    b.tamano = 1200;
    const auto rb = decodificar(codificar(b));
    CHECK(rb && rb->kbps == 20000 && rb->ms == 1500 && rb->tamano == 1200, "pedido de bajada ida y vuelta");

    Mensaje h;
    h.tipo = Tipo::HolaOk;
    h.version = kVersion;
    h.kbps = 200000;
    h.ms = 30000;
    const auto rh = decodificar(codificar(h));
    CHECK(rh && rh->version == kVersion && rh->kbps == 200000 && rh->ms == 30000, "hola ida y vuelta");

    CHECK(!decodificar(std::string("XXXX\x01", 5)), "lo que no empieza con BAE1 no es nuestro");
    CHECK(!decodificar(codificar(inf).substr(0, 20)), "un informe cortado se descarta");
    CHECK(!decodificar(std::string("BAE1\x63", 5)), "un tipo desconocido se descarta");
}

void cuentas() {
    // Tránsito constante (con un desfasaje de reloj enorme): jitter 0.
    Receptor r;
    for (uint32_t i = 0; i < 100; ++i) r.llegada(i, 1000000000ULL + i * 1000, 5 + i * 1000, 1200);
    CHECK(r.recibidos() == 100 && r.jitterUs() < 0.001, "tránsito parejo: jitter 0 aunque los relojes no coincidan");
    // Tránsitos que alternan ±1 ms: el jitter converge a ~1 ms (RFC 3550).
    Receptor a;
    for (uint32_t i = 0; i < 400; ++i) a.llegada(i, i * 10000, i * 10000 + (i % 2 ? 1000 : 0), 1200);
    CHECK(cerca(a.jitterUs(), 1000, 50), "jitter de RFC 3550 con ±1 ms alternado ≈ 1 ms");
    Receptor d;
    d.llegada(0, 0, 0, 100);
    d.llegada(2, 0, 10, 100);
    d.llegada(1, 0, 20, 100);
    CHECK(d.desordenados() == 1 && d.seqMax() == 2, "cuenta los desordenados");

    // 1001 paquetes de 1200 en 1 s: 1000 intervalos × (1200+28) × 8 = 9824 kbps.
    CHECK(cerca(kbpsEntregados(1001ULL * 1200, 1001, 0, 1000000), 9824, 1), "tasa entregada con cabecera IP/UDP");
    CHECK(kbpsEntregados(1200, 1, 0, 1000) == 0, "con un solo paquete no hay tasa");
    CHECK(cerca(perdida(1000, 950), 0.05, 1e-9) && perdida(0, 0) == 0 && perdida(10, 12) == 0, "pérdida acotada a 0..1");
    CHECK(cerca(percentil({1, 2, 3, 4, 5}, 50), 3, 1e-9) && cerca(percentil({1, 2, 3, 4}, 50), 2.5, 1e-9) &&
              percentil({}, 50) < 0,
          "percentiles con interpolación");
    CHECK(cerca(variacionMedia({10, 12, 10, 12}), 2, 1e-9), "variación media de los RTT");

    const std::vector<int> r30 = rampa(30000);
    CHECK(r30 == std::vector<int>({2000, 5000, 10000, 20000, 30000}), "rampa hasta un tope de la lista");
    const std::vector<int> r25 = rampa(25000);
    CHECK(r25.back() == 25000 && r25.size() == 5, "rampa con un tope suelto: el tope es el último");
    CHECK(rampa(1000) == std::vector<int>({1000}), "un tope más chico que el primer escalón");

    Escalon bien{20000, 19800, 0.001, 2, 40};
    Escalon pierde{20000, 19000, 0.08, 2, 40};
    Escalon corto{40000, 30000, 0.01, 2, 40};
    Escalon cola{20000, 19900, 0.0, 2, 400};
    CHECK(!saturado(bien, 35), "entregó lo pedido sin pérdida: no saturó");
    CHECK(saturado(pierde, 35), "perder más del 5 % es saturar");
    CHECK(saturado(corto, 35), "entregar menos del 85 % es saturar");
    CHECK(saturado(cola, 35), "que la demora suba más de 300 ms es saturar");
    CHECK(cerca(capacidadKbps({bien, corto}), 30000, 1e-9), "la capacidad es lo más que entregó");
}

void servicio() {
    Pedido p;
    std::string e;
    CHECK(Servicio::leerPedido(json{{"topeSubidaKbps", 30000}, {"tasaKbps", 5000}, {"segundos", 5}}, p, e) &&
              p.topeSubidaKbps == 30000 && p.tasaKbps == 5000 && p.segundos == 5,
          "el pedido se lee");
    Pedido q;
    CHECK(!Servicio::leerPedido(json{{"segundos", 600}}, q, e), "más de 30 s por sentido no");
    CHECK(!Servicio::leerPedido(json{{"subida", false}, {"bajada", false}}, q, e), "sin ningún sentido no");
    CHECK(!Servicio::leerPedido(json{{"topeBajadaKbps", "mucho"}}, q, e), "un tope que no es número no");

    Servicio sin({}, "10.99.0.1:51821", {});
    CHECK(sin.estado().value("estado", "") == "sin_tunel", "sin túnel: el estado lo dice");
    CHECK(!sin.medir(json::object()).value("ok", true), "sin túnel no mide");

    const std::string inst = instaladorEco("10.99.0.1", "10.99.0.0/24", kPuerto);
    CHECK(inst.find("--direccion 10.99.0.1 --puerto 51821 --red 10.99.0.0/24") != std::string::npos,
          "el instalador arranca el eco en la IP del túnel");
    CHECK(inst.find("wg-quick@wg0") != std::string::npos && inst.find("systemctl enable ba-enlace-eco") != std::string::npos,
          "y lo deja como servicio después del túnel");
    const std::string py = scriptEco();
    CHECK(py.find("MAGIA = b\"BAE1\"") != std::string::npos && inst.find(py.substr(0, 200)) != std::string::npos,
          "el instalador lleva el script del eco de este build");
}

#ifndef _WIN32
uint16_t puertoLibre() {
    const int s = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    socklen_t l = sizeof(a);
    getsockname(s, reinterpret_cast<sockaddr*>(&a), &l);
    close(s);
    return ntohs(a.sin_port);
}

void contraElEco() {
    if (std::system("python3 -c \"import sys\" >/dev/null 2>&1") != 0) {
        std::printf("SKIP eco: falta python3\n");
        return;
    }
    const uint16_t puerto = puertoLibre();
    const pid_t pid = fork();
    if (pid == 0) {
        const std::string p = std::to_string(puerto);
        execlp("python3", "python3", BA_TEST_ECO_ENLACE, "--direccion", "127.0.0.1", "--puerto", p.c_str(), "--red",
               "127.0.0.0/8", "--durante-s", "60", static_cast<char*>(nullptr));
        _exit(127);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(700)); // que arranque Python

    std::atomic<bool> cancelar{false};
    int avisos = 0;
    {
        TransporteSocket t("127.0.0.1", puerto);
        CHECK(t.ok(), "socket UDP hacia el eco");
        Medidor m(t, [&](const json& j) { ++avisos; (void)j; }, cancelar);
        Pedido p;
        p.topeSubidaKbps = 10000;
        p.topeBajadaKbps = 10000;
        p.escalonMs = 600;
        p.reposoMs = 600;
        const json r = m.correr(p);
        std::printf("   %s\n", r.dump().substr(0, 400).c_str());
        CHECK(r.value("ok", false), "la medición contra el eco termina bien");
        CHECK(r["eco"].value("version", 0) == kVersion, "el eco dice su versión");
        CHECK(r["reposo"].value("rttMs", -1.0) >= 0 && r["reposo"].value("perdida", 1.0) < 0.2, "RTT en reposo por loopback");
        const json& s = r["subida"];
        const json& b = r["bajada"];
        CHECK(s.value("capacidadKbps", 0.0) > 5000 && s["escalones"].size() >= 3, "subida: la rampa llegó a varios escalones");
        CHECK(b.value("capacidadKbps", 0.0) > 5000 && b["escalones"].size() >= 3, "bajada: la rampa llegó a varios escalones");
        CHECK(s["escalones"][0].value("recibidos", 0) > 0 && b["escalones"][0].value("enviados", 0) > 0,
              "el eco contó la subida y dijo cuánto mandó de bajada");
        CHECK(avisos > 5, "fue avisando el avance");

        Pedido fija;
        fija.tasaKbps = 4000;
        fija.segundos = 2;
        fija.reposoMs = 300;
        fija.bajada = false;
        const json rf = m.correr(fija);
        CHECK(rf.value("ok", false) && rf["subida"]["escalones"].size() == 1 && rf["subida"].value("tasaFija", false) &&
                  cerca(rf["subida"]["escalones"][0].value("kbpsEntregados", 0.0), 4000, 800),
              "tasa fija: un escalón a la tasa pedida");

        // Cancelar en el medio corta enseguida.
        std::thread corta([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            cancelar = true;
        });
        const auto t0 = std::chrono::steady_clock::now();
        const json rc = m.correr(p);
        corta.join();
        const double seg = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(!rc.value("ok", true) && rc.value("cancelado", false) && seg < 3, "cancelar corta la medición");
    }
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);

    // Sin eco del otro lado: no se cuelga y dice qué falta.
    std::atomic<bool> no{false};
    TransporteSocket t2("127.0.0.1", puertoLibre());
    Medidor m2(t2, {}, no);
    const json r2 = m2.correr(Pedido{});
    CHECK(!r2.value("ok", true) && r2.value("sinEco", false), "sin eco: avisa que falta instalarlo");
}
#endif

} // namespace

int main() {
    std::printf("== test_enlace ==\n");
    protocolo();
    cuentas();
    servicio();
#ifndef _WIN32
    contraElEco();
#endif
    if (g_fallas == 0) {
        std::printf("test_enlace: OK\n");
        return 0;
    }
    std::printf("test_enlace: %d fallo(s)\n", g_fallas);
    return 1;
}
