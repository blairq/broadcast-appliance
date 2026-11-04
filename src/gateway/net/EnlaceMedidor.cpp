// ============================================================================
// src/gateway/net/EnlaceMedidor.cpp — ver EnlaceMedidor.hpp
// ============================================================================

#include "net/EnlaceMedidor.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "i18n/I18nTexto.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using SockT = SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SockT = int;
#endif

namespace ba::gateway::net::enlace {

using json = nlohmann::json;
using ba::i18n::traducir;

int64_t ahoraUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// --- TransporteSocket ------------------------------------------------------------

TransporteSocket::TransporteSocket(const std::string& ipv4, uint16_t puerto) {
    const SockT s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) return;
#else
    if (s < 0) return;
#endif
    int buf = 4 * 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buf), sizeof(buf));
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buf), sizeof(buf));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(puerto);
    inet_pton(AF_INET, ipv4.c_str(), &a.sin_addr);
    if (connect(s, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0) {
#ifdef _WIN32
        closesocket(s);
#else
        ::close(s);
#endif
        return;
    }
#ifdef _WIN32
    u_long uno = 1;
    ioctlsocket(s, FIONBIO, &uno);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    sock_ = static_cast<long long>(s);
}

TransporteSocket::~TransporteSocket() {
    if (sock_ < 0) return;
#ifdef _WIN32
    closesocket(static_cast<SockT>(sock_));
#else
    ::close(static_cast<SockT>(sock_));
#endif
}

bool TransporteSocket::enviar(std::vector<std::string>&& lote) {
    if (sock_ < 0) return false;
    for (const std::string& d : lote)
        ::send(static_cast<SockT>(sock_), d.data(), static_cast<int>(d.size()), 0);
    return true;
}

std::vector<Datagrama> TransporteSocket::recibir(int esperaMs) {
    std::vector<Datagrama> out;
    if (sock_ < 0) return out;
    const SockT s = static_cast<SockT>(sock_);
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(s, &rd);
    timeval tv{esperaMs / 1000, static_cast<decltype(timeval::tv_usec)>((esperaMs % 1000) * 1000)};
    if (select(static_cast<int>(s) + 1, &rd, nullptr, nullptr, &tv) <= 0) return out;
    char buf[2048];
    for (;;) {
        const int n = static_cast<int>(::recv(s, buf, sizeof(buf), 0));
        if (n <= 0) break;
        out.push_back({std::string(buf, static_cast<size_t>(n)), ahoraUs()});
    }
    return out;
}

// --- Medidor ---------------------------------------------------------------------

namespace {
constexpr uint32_t kPruebaPing = 0; ///< los ping no son de ninguna prueba
constexpr int64_t kPingCargaUs = 100000;
constexpr int64_t kPingReposoUs = 50000;

json escalonJson(const Escalon& e) {
    return {{"kbpsPedidos", e.kbpsPedidos},
            {"kbpsEntregados", std::round(e.kbpsEntregados)},
            {"perdida", std::round(e.perdida * 10000) / 10000},
            {"jitterMs", std::round(e.jitterMs * 10) / 10},
            {"rttMs", e.rttMs < 0 ? -1.0 : std::round(e.rttMs * 10) / 10},
            {"enviados", e.enviados},
            {"recibidos", e.recibidos}};
}
} // namespace

Medidor::Medidor(Transporte& t, ProgresoFn progreso, const std::atomic<bool>& cancelar)
    : t_(t), progreso_(std::move(progreso)), cancelar_(cancelar) {
    // Números de prueba que no choquen con lo que el eco tenga de una corrida anterior.
    prueba_ = static_cast<uint32_t>(ahoraUs() & 0x0FFFFFFF) << 2;
    if (prueba_ == kPruebaPing) prueba_ = 4;
}

void Medidor::avisar(const std::string& fase, double avance, const json& extra) {
    if (!progreso_) return;
    // A la app, como mucho 5 por segundo (salvo un cambio de fase o de escalón).
    const int64_t ahora = ahoraUs();
    const std::string clave = fase + "/" + extra.dump();
    if (clave == ultimoAviso_ && ahora - ultimoAvisoUs_ < 200000) return;
    ultimoAviso_ = clave;
    ultimoAvisoUs_ = ahora;
    json j = parcial_;
    j["estado"] = "midiendo";
    j["fase"] = fase;
    j["avance"] = std::round(std::clamp(avanceBase_ + avance * avancePeso_, 0.0, 1.0) * 1000) / 1000;
    for (auto it = extra.begin(); it != extra.end(); ++it) j[it.key()] = it.value();
    progreso_(j);
}

void Medidor::ping(int64_t ahora) {
    Mensaje m;
    m.tipo = Tipo::Ping;
    m.prueba = kPruebaPing;
    m.seq = pingSeq_++;
    m.tUs = static_cast<uint64_t>(ahora);
    ++pingsMandados_;
    t_.enviar({codificar(m)});
}

void Medidor::atender(const std::vector<Datagrama>& v, std::vector<double>* rtts, Receptor* rx, uint32_t pruebaRx,
                      uint32_t pruebaInforme, std::optional<Mensaje>* informe, std::optional<Mensaje>* bajadaOk) {
    for (const Datagrama& d : v) {
        const std::optional<Mensaje> m = decodificar(d.datos);
        if (!m) continue;
        switch (m->tipo) {
        case Tipo::Pong:
            if (m->prueba == kPruebaPing) {
                ++pingsVueltos_;
                if (rtts != nullptr) rtts->push_back(static_cast<double>(d.llegadaUs - static_cast<int64_t>(m->tUs)) / 1000.0);
            }
            break;
        case Tipo::Dato:
            if (rx != nullptr && m->prueba == pruebaRx) rx->llegada(m->seq, m->tUs, d.llegadaUs, d.datos.size());
            break;
        case Tipo::Informe:
            if (informe != nullptr && m->prueba == pruebaInforme) *informe = m;
            break;
        case Tipo::BajadaOk:
            if (bajadaOk != nullptr && m->prueba == pruebaRx) *bajadaOk = m;
            break;
        default:
            break;
        }
    }
}

bool Medidor::hola(json& eco) {
    Mensaje h;
    h.tipo = Tipo::Hola;
    for (int intento = 0; intento < 4 && !cancelar_; ++intento) {
        t_.enviar({codificar(h)});
        const int64_t hasta = ahoraUs() + 700000;
        while (ahoraUs() < hasta && !cancelar_) {
            for (const Datagrama& d : t_.recibir(50)) {
                const std::optional<Mensaje> m = decodificar(d.datos);
                if (m && m->tipo == Tipo::HolaOk) {
                    eco = {{"version", m->version}, {"maxKbps", m->kbps}, {"maxMs", m->ms}};
                    return true;
                }
            }
        }
    }
    return false;
}

json Medidor::reposo(int ms) {
    pingsMandados_ = pingsVueltos_ = 0;
    std::vector<double> rtts;
    const int64_t t0 = ahoraUs();
    const int64_t fin = t0 + static_cast<int64_t>(ms) * 1000;
    int64_t ultimo = 0;
    for (int64_t ahora = t0; ahora < fin + 500000 && !cancelar_; ahora = ahoraUs()) {
        if (ahora < fin && ahora - ultimo >= kPingReposoUs) {
            ping(ahora);
            ultimo = ahora;
            avisar("reposo", static_cast<double>(ahora - t0) / (ms * 1000.0));
        }
        atender(t_.recibir(5), &rtts, nullptr, 0, 0, nullptr, nullptr);
    }
    return {{"rttMs", std::round(percentil(rtts, 50) * 10) / 10},
            {"rttP95Ms", std::round(percentil(rtts, 95) * 10) / 10},
            {"jitterMs", std::round(variacionMedia(rtts) * 10) / 10},
            {"perdida", std::round(perdida(pingsMandados_, pingsVueltos_) * 10000) / 10000},
            {"ping", pingsMandados_}};
}

std::optional<Mensaje> Medidor::pedirInforme(uint32_t prueba, std::vector<double>* rtts, Receptor* rx) {
    Mensaje q;
    q.tipo = Tipo::InformePedido;
    q.prueba = prueba;
    std::optional<Mensaje> inf;
    // Lo que está en vuelo (con la cola llena puede tardar): 300 ms antes de preguntar.
    const int64_t espera = ahoraUs() + 300000;
    while (ahoraUs() < espera) atender(t_.recibir(10), rtts, rx, prueba, prueba, &inf, nullptr);
    for (int intento = 0; intento < 6 && !inf; ++intento) {
        t_.enviar({codificar(q)});
        const int64_t hasta = ahoraUs() + 500000;
        while (ahoraUs() < hasta && !inf) atender(t_.recibir(10), rtts, rx, prueba, prueba, &inf, nullptr);
    }
    return inf;
}

Escalon Medidor::subir(int kbps, int ms, int tamano) {
    const uint32_t id = ++prueba_;
    Escalon e;
    e.kbpsPedidos = kbps;
    std::vector<double> rtts;
    const double pps = kbps * 1000.0 / ((tamano + kCabeceraIpUdp) * 8.0);
    const int64_t t0 = ahoraUs();
    const int64_t fin = t0 + static_cast<int64_t>(ms) * 1000;
    const int64_t desdeRtt = t0 + static_cast<int64_t>(ms) * 300; // el 30 % primero: se llena la cola
    bool rttLimpio = false;
    uint32_t seq = 0;
    int64_t ultimoPing = 0;
    for (int64_t ahora = t0; ahora < fin && !cancelar_; ahora = ahoraUs()) {
        const auto debidos = static_cast<uint32_t>(static_cast<double>(ahora - t0) * pps / 1e6) + 1;
        std::vector<std::string> lote;
        while (seq < debidos && lote.size() < 512) {
            Mensaje d;
            d.tipo = Tipo::Dato;
            d.prueba = id;
            d.seq = seq++;
            d.tUs = static_cast<uint64_t>(ahora);
            lote.push_back(codificar(d, static_cast<size_t>(tamano)));
        }
        if (!lote.empty()) t_.enviar(std::move(lote));
        if (ahora - ultimoPing >= kPingCargaUs) {
            ping(ahora);
            ultimoPing = ahora;
        }
        if (!rttLimpio && ahora >= desdeRtt) {
            rtts.clear();
            rttLimpio = true;
        }
        atender(t_.recibir(1), &rtts, nullptr, 0, 0, nullptr, nullptr);
    }
    e.enviados = seq;
    if (cancelar_) return e;
    const std::optional<Mensaje> inf = pedirInforme(id, &rtts, nullptr);
    e.rttMs = percentil(rtts, 50);
    if (!inf) {
        e.perdida = 1;
        return e;
    }
    e.recibidos = inf->recibidos;
    e.kbpsEntregados = kbpsEntregados(inf->bytes, inf->recibidos, static_cast<int64_t>(inf->primeroUs),
                                      static_cast<int64_t>(inf->ultimoUs));
    e.perdida = perdida(e.enviados, e.recibidos);
    e.jitterMs = inf->jitterUs / 1000.0;
    return e;
}

Escalon Medidor::bajar(int kbps, int ms, int tamano) {
    const uint32_t id = ++prueba_;
    Escalon e;
    e.kbpsPedidos = kbps;
    Receptor rx;
    std::vector<double> rtts;
    Mensaje q;
    q.tipo = Tipo::BajadaPedido;
    q.prueba = id;
    q.kbps = static_cast<uint32_t>(kbps);
    q.ms = static_cast<uint32_t>(ms);
    q.tamano = static_cast<uint16_t>(tamano);
    std::optional<Mensaje> ok;
    for (int intento = 0; intento < 5 && !ok && rx.recibidos() == 0 && !cancelar_; ++intento) {
        t_.enviar({codificar(q)});
        const int64_t hasta = ahoraUs() + 400000;
        while (ahoraUs() < hasta && !ok && rx.recibidos() == 0)
            atender(t_.recibir(10), &rtts, &rx, id, 0, nullptr, &ok);
    }
    if (!ok && rx.recibidos() == 0) {
        e.perdida = 1;
        return e;
    }
    if (ok) e.kbpsPedidos = static_cast<int>(ok->kbps);
    const int durMs = ok ? static_cast<int>(ok->ms) : ms;
    const int64_t t0 = ahoraUs();
    const int64_t fin = t0 + static_cast<int64_t>(durMs) * 1000 + 300000;
    const int64_t desdeRtt = t0 + static_cast<int64_t>(durMs) * 300;
    bool rttLimpio = false;
    int64_t ultimoPing = 0;
    for (int64_t ahora = t0; ahora < fin && !cancelar_; ahora = ahoraUs()) {
        if (ahora - ultimoPing >= kPingCargaUs) {
            ping(ahora);
            ultimoPing = ahora;
        }
        if (!rttLimpio && ahora >= desdeRtt) {
            rtts.clear();
            rttLimpio = true;
        }
        atender(t_.recibir(2), &rtts, &rx, id, 0, nullptr, nullptr);
    }
    if (cancelar_) {
        Mensaje parar;
        parar.tipo = Tipo::Parar;
        parar.prueba = id;
        t_.enviar({codificar(parar)});
        return e;
    }
    const std::optional<Mensaje> inf = pedirInforme(id, &rtts, &rx);
    e.rttMs = percentil(rtts, 50);
    e.recibidos = rx.recibidos();
    e.enviados = inf ? inf->enviados : rx.seqMax() + 1;
    e.kbpsEntregados = kbpsEntregados(rx.bytes(), rx.recibidos(), rx.primeroUs(), rx.ultimoUs());
    e.perdida = perdida(e.enviados, e.recibidos);
    e.jitterMs = rx.jitterUs() / 1000.0;
    return e;
}

json Medidor::sentido(bool subida, const Pedido& p, double rttReposo) {
    const char* fase = subida ? "subida" : "bajada";
    std::vector<int> tasas;
    int ms = p.escalonMs;
    if (p.tasaKbps > 0) {
        tasas = {p.tasaKbps};
        ms = p.segundos * 1000;
    } else {
        tasas = rampa(subida ? p.topeSubidaKbps : p.topeBajadaKbps);
    }
    std::vector<Escalon> hechos;
    json lista = json::array();
    bool satura = false;
    for (size_t i = 0; i < tasas.size() && !cancelar_; ++i) {
        avisar(fase, static_cast<double>(i) / tasas.size(), {{"kbpsEscalon", tasas[i]}});
        const Escalon e =
            subida ? subir(tasas[i], ms, p.tamano) : bajar(tasas[i], ms, p.tamano);
        if (cancelar_) break;
        hechos.push_back(e);
        lista.push_back(escalonJson(e));
        parcial_[fase] = {{"escalones", lista}, {"capacidadKbps", std::round(capacidadKbps(hechos))}};
        if (saturado(e, rttReposo)) {
            satura = true;
            break;
        }
    }
    if (hechos.empty()) return json::object();
    // Lo que se informa: la capacidad (lo más que entregó), la pérdida y el jitter a la
    // tasa más alta que NO saturó (la que aguanta) y el peor RTT con carga.
    const Escalon* sano = nullptr;
    for (const Escalon& e : hechos)
        if (!saturado(e, rttReposo)) sano = &e;
    const Escalon* ref = sano != nullptr ? sano : &hechos.front();
    double rttCarga = -1;
    for (const Escalon& e : hechos) rttCarga = std::max(rttCarga, e.rttMs);
    json r{{"capacidadKbps", std::round(capacidadKbps(hechos))},
           {"saturo", satura},
           {"kbpsSinPerdida", sano != nullptr ? std::round(sano->kbpsEntregados) : 0.0},
           {"perdida", std::round(ref->perdida * 10000) / 10000},
           {"jitterMs", std::round(ref->jitterMs * 10) / 10},
           {"rttCargaMs", rttCarga < 0 ? -1.0 : std::round(rttCarga * 10) / 10},
           {"tasaFija", p.tasaKbps > 0},
           {"escalones", lista}};
    parcial_[fase] = r;
    return r;
}

json Medidor::correr(const Pedido& pedido) {
    Pedido p = pedido;
    const int64_t t0 = ahoraUs();
    parcial_ = json::object();
    avanceBase_ = 0;
    avancePeso_ = 0.03;
    avisar("hola", 0);
    json eco;
    if (!hola(eco)) {
        if (cancelar_) return {{"ok", false}, {"cancelado", true}};
        return {{"ok", false},
                {"sinEco", true},
                {"error", traducir("gateway", "el servidor del túnel no contesta: falta instalar el medidor "
                                              "(ba-enlace-eco) en el VPS o el túnel no está conectado")}};
    }
    parcial_["eco"] = eco;
    if (eco.value("version", 0) < kVersion) parcial_["ecoViejo"] = true;
    // Los topes del eco mandan.
    const int maxKbps = eco.value("maxKbps", 0);
    const int maxMs = eco.value("maxMs", 0);
    if (maxKbps > 0) {
        p.topeSubidaKbps = std::min(p.topeSubidaKbps, maxKbps);
        p.topeBajadaKbps = std::min(p.topeBajadaKbps, maxKbps);
        if (p.tasaKbps > 0) p.tasaKbps = std::min(p.tasaKbps, maxKbps);
    }
    if (maxMs > 0) {
        p.escalonMs = std::min(p.escalonMs, maxMs);
        p.segundos = std::min(p.segundos, maxMs / 1000);
    }
    p.tamano = std::clamp(p.tamano, kTamanoMinDato, 1392); // MTU del túnel (1420) - IP/UDP

    avanceBase_ = 0.03;
    avancePeso_ = 0.12;
    const json rep = reposo(p.reposoMs);
    parcial_["reposo"] = rep;
    const double rttReposo = rep.value("rttMs", -1.0);
    const int sentidos = (p.subida ? 1 : 0) + (p.bajada ? 1 : 0);
    const double resto = 0.85 / std::max(1, sentidos);
    avanceBase_ = 0.15;
    avancePeso_ = resto;
    if (p.subida && !cancelar_) {
        sentido(true, p, rttReposo);
        avanceBase_ += resto;
    }
    if (p.bajada && !cancelar_) sentido(false, p, rttReposo);
    if (cancelar_) return {{"ok", false}, {"cancelado", true}};
    json r = parcial_;
    r["ok"] = true;
    r["segundos"] = std::round(static_cast<double>(ahoraUs() - t0) / 1e5) / 10;
    return r;
}

} // namespace ba::gateway::net::enlace
