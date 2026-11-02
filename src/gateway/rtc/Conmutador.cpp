// ============================================================================
// src/gateway/rtc/Conmutador.cpp — ver Conmutador.hpp
// ============================================================================
#include "rtc/Conmutador.hpp"

#include <deque>
#include <utility>

#include "diag/Frontera.hpp"

namespace ba::gateway::memoria {

namespace {

/// Lo que espera en el hilo a que termine el ámbito más externo.
struct Pendiente {
    Conmutador* c = nullptr;
    PuntoPtr p;
    std::vector<std::uint8_t> datos; // se reusa (la capacidad queda)
    std::uint32_t ip = 0;
    std::uint16_t puerto = 0;
    Via via = Via::Turn;
};

thread_local int t_profundidad = 0;
thread_local std::deque<Pendiente> t_pend; // deque: agregar no mueve los que ya están
thread_local std::size_t t_nPend = 0;

ba::diag::Frontera& fCruce() {
    static ba::diag::Frontera& f =
        ba::diag::frontera("memoria.cruce", 0, "entrega en memoria a otro hilo de red (cola SPSC, en lote)");
    return f;
}
ba::diag::Frontera& fTunel() {
    static ba::diag::Frontera& f =
        ba::diag::frontera("memoria.tunel", 18, "túnel (WireGuard) → red de WebRTC en memoria (en lote)");
    return f;
}

} // namespace

// --- la cola entre dos hilos: anillo de un productor y un consumidor, sin candado ---------
struct Conmutador::Cola {
    struct Lugar {
        PuntoPtr p;
        std::vector<std::uint8_t> datos;
        std::uint32_t ip = 0;
        std::uint16_t puerto = 0;
        Via via = Via::Turn;
        std::uint64_t ns = 0;
    };
    explicit Cola(std::size_t c) : lugares(c), cap(c) {}
    std::vector<Lugar> lugares;
    const std::size_t cap;
    alignas(64) std::atomic<std::size_t> cabeza{0}; // la escribe el productor
    alignas(64) std::atomic<std::size_t> cola{0};   // la escribe el consumidor

    bool poner(const PuntoPtr& p, const std::uint8_t* d, std::size_t n, std::uint32_t ip, std::uint16_t puerto, Via via) {
        const std::size_t h = cabeza.load(std::memory_order_relaxed);
        const std::size_t t = cola.load(std::memory_order_acquire);
        if (h - t >= cap) return false;
        Lugar& l = lugares[h % cap];
        l.p = p;
        l.datos.assign(d, d + n);
        l.ip = ip;
        l.puerto = puerto;
        l.via = via;
        l.ns = ba::diag::relojNs();
        cabeza.store(h + 1, std::memory_order_seq_cst);
        return true;
    }
    /// Saca todo lo que haya (en el hilo consumidor). `f(Lugar&)`.
    template <typename F> std::size_t vaciar(F&& f, std::uint64_t& masViejoNs, std::uint64_t& bytes) {
        std::size_t t = cola.load(std::memory_order_relaxed);
        const std::size_t h = cabeza.load(std::memory_order_seq_cst);
        std::size_t n = 0;
        for (; t != h; ++t, ++n) {
            Lugar& l = lugares[t % cap];
            if (masViejoNs == 0 || l.ns < masViejoNs) masViejoNs = l.ns;
            bytes += l.datos.size();
            f(l);
            l.p.reset();
        }
        cola.store(t, std::memory_order_release);
        return n;
    }
};

Conmutador::Conmutador(int hilos, int externos, Postear postear, std::size_t capacidad)
    : hilos_(hilos < 1 ? 1 : hilos), externos_(externos < 0 ? 0 : externos), postear_(std::move(postear)) {
    const int productores = hilos_ + externos_;
    colas_.resize(static_cast<std::size_t>(productores) * static_cast<std::size_t>(hilos_));
    for (int d = 0; d < productores; ++d)
        for (int k = 0; k < hilos_; ++k)
            if (d != k) colas_[static_cast<std::size_t>(d * hilos_ + k)] = std::make_unique<Cola>(capacidad < 16 ? 16 : capacidad);
    avisado_.reset(new std::atomic<bool>[static_cast<std::size_t>(hilos_)]);
    for (int k = 0; k < hilos_; ++k) avisado_[static_cast<std::size_t>(k)] = false;
    const std::size_t np = static_cast<std::size_t>(externos_ > 0 ? externos_ : 1) * static_cast<std::size_t>(hilos_);
    pendiente_.reset(new std::atomic<bool>[np]);
    for (std::size_t i = 0; i < np; ++i) pendiente_[i] = false;
}

Conmutador::~Conmutador() = default;

// --- registro -----------------------------------------------------------------------------

PuntoPtr Conmutador::abrir(std::uint32_t ip, std::uint16_t puerto, int hilo, Clase clase, Receptor* r) {
    auto p = std::make_shared<Punto>();
    p->ip = ip;
    p->puerto = puerto;
    p->hilo = hilo < 0 ? 0 : (hilo >= hilos_ ? hilos_ - 1 : hilo);
    p->clase = clase;
    p->receptor = r;
    {
        std::lock_guard<std::mutex> l(m_);
        // Un puerto reusado: el anterior ya se cerró (o se pisa: el último manda).
        auto& lugar = puntos_[clave(ip, puerto)];
        if (lugar) lugar->vivo.store(false, std::memory_order_release);
        lugar = p;
    }
    generacion_.fetch_add(1, std::memory_order_acq_rel);
    return p;
}

void Conmutador::cerrar(const PuntoPtr& p) {
    if (!p) return;
    p->vivo.store(false, std::memory_order_release);
    p->receptor = nullptr;
    std::lock_guard<std::mutex> l(m_);
    const auto it = puntos_.find(clave(p->ip, p->puerto));
    if (it != puntos_.end() && it->second == p) puntos_.erase(it);
}

PuntoPtr Conmutador::buscar(std::uint32_t ip, std::uint16_t puerto) const {
    std::lock_guard<std::mutex> l(m_);
    auto it = puntos_.find(clave(ip, puerto));
    if (it == puntos_.end()) it = puntos_.find(clave(0, puerto));
    return it == puntos_.end() ? nullptr : it->second;
}

PuntoPtr Conmutador::buscarPuerto(std::uint16_t puerto) const {
    std::lock_guard<std::mutex> l(m_);
    auto it = puntos_.find(clave(0x7F000001u, puerto)); // 127.0.0.1: donde atiende WebRTC para el túnel
    if (it == puntos_.end()) it = puntos_.find(clave(0, puerto));
    if (it != puntos_.end() && it->second->clase == Clase::WebrtcUdp) return it->second;
    return nullptr;
}

// --- mandar -------------------------------------------------------------------------------

bool Conmutador::mandar(int desde, const PuntoPtr& p, const std::uint8_t* datos, std::size_t n, std::uint32_t ipOrigen,
                        std::uint16_t puertoOrigen, Via via) {
    if (!p || !p->vivo.load(std::memory_order_acquire)) {
        descartes_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (p->hilo == desde) {
        if (t_profundidad == 0) {
            directos_.fetch_add(1, std::memory_order_relaxed);
            entregarAhora(p, datos, n, ipOrigen, puertoOrigen, via);
        } else {
            encolados_.fetch_add(1, std::memory_order_relaxed);
            encolarLocal(p, datos, n, ipOrigen, puertoOrigen, via);
        }
        return true;
    }
    if (desde < 0 || desde >= hilos_ + externos_) return false;
    Cola* q = colas_[static_cast<std::size_t>(desde * hilos_ + p->hilo)].get();
    if (q == nullptr || !q->poner(p, datos, n, ipOrigen, puertoOrigen, via)) {
        descartes_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    cruzados_.fetch_add(1, std::memory_order_relaxed);
    if (desde < hilos_) {
        despertar(p->hilo);
    } else {
        pendiente_[static_cast<std::size_t>((desde - hilos_) * hilos_ + p->hilo)].store(true, std::memory_order_relaxed);
    }
    return true;
}

void Conmutador::terminarTanda(int desde) {
    if (desde < hilos_ || desde >= hilos_ + externos_) return;
    for (int k = 0; k < hilos_; ++k) {
        auto& f = pendiente_[static_cast<std::size_t>((desde - hilos_) * hilos_ + k)];
        if (f.exchange(false, std::memory_order_relaxed)) despertar(k);
    }
}

void Conmutador::despertar(int destino) {
    if (avisado_[static_cast<std::size_t>(destino)].exchange(true, std::memory_order_seq_cst)) return;
    despertadas_.fetch_add(1, std::memory_order_relaxed);
    postear_(destino, [this, destino]() { vaciarHacia(destino); });
}

void Conmutador::vaciarHacia(int destino) {
    // Primero bajar el aviso: lo que llegue mientras se vacía vuelve a despertar.
    avisado_[static_cast<std::size_t>(destino)].store(false, std::memory_order_seq_cst);
    Ambito a(*this);
    const std::uint64_t t0 = ba::diag::relojNs();
    std::uint64_t viejo = 0, bytes = 0, viejoTunel = 0, bytesTunel = 0;
    std::size_t n = 0, nTunel = 0;
    for (int d = 0; d < hilos_ + externos_; ++d) {
        Cola* q = colas_[static_cast<std::size_t>(d * hilos_ + destino)].get();
        if (q == nullptr) continue;
        const bool externo = d >= hilos_;
        auto entregar = [](Cola::Lugar& l) {
            Punto* p = l.p.get();
            if (p == nullptr || !p->vivo.load(std::memory_order_acquire) || p->receptor == nullptr) return;
            ++t_profundidad;
            p->receptor->recibirEnMemoria(l.datos.data(), l.datos.size(), l.ip, l.puerto, l.via);
            --t_profundidad;
        };
        if (externo)
            nTunel += q->vaciar(entregar, viejoTunel, bytesTunel);
        else
            n += q->vaciar(entregar, viejo, bytes);
    }
    const std::uint64_t ahora = ba::diag::relojNs();
    if (n > 0) {
        fCruce().despertada();
        fCruce().lote(n, bytes, ahora - t0 + 1, viejo != 0 && ahora > viejo ? ahora - viejo : 0);
    }
    if (nTunel > 0) {
        fTunel().despertada();
        fTunel().lote(nTunel, bytesTunel, ahora - t0 + 1, viejoTunel != 0 && ahora > viejoTunel ? ahora - viejoTunel : 0);
    }
}

void Conmutador::entregarAhora(const PuntoPtr& p, const std::uint8_t* datos, std::size_t n, std::uint32_t ip,
                               std::uint16_t puerto, Via via) {
    Ambito a(*this);
    if (p->vivo.load(std::memory_order_acquire) && p->receptor != nullptr)
        p->receptor->recibirEnMemoria(datos, n, ip, puerto, via);
}

void Conmutador::encolarLocal(const PuntoPtr& p, const std::uint8_t* datos, std::size_t n, std::uint32_t ip,
                              std::uint16_t puerto, Via via) {
    if (t_nPend == t_pend.size()) t_pend.emplace_back();
    Pendiente& x = t_pend[t_nPend++];
    x.c = this;
    x.p = p;
    x.datos.assign(datos, datos + n);
    x.ip = ip;
    x.puerto = puerto;
    x.via = via;
}

void Conmutador::vaciarLocal() {
    // Se llama con t_profundidad == 0. Cada entrega corre con profundidad 1: lo que mande el
    // receptor al mismo hilo se agrega al final y sale en esta misma vuelta.
    for (std::size_t i = 0; i < t_nPend; ++i) {
        Pendiente& x = t_pend[i];
        Punto* p = x.p.get();
        if (p != nullptr && p->vivo.load(std::memory_order_acquire) && p->receptor != nullptr) {
            ++t_profundidad;
            p->receptor->recibirEnMemoria(x.datos.data(), x.datos.size(), x.ip, x.puerto, x.via);
            --t_profundidad;
        }
        t_pend[i].p.reset();
    }
    t_nPend = 0;
}

Conmutador::Ambito::Ambito(Conmutador& c) : c_(c) { ++t_profundidad; }

Conmutador::Ambito::~Ambito() {
    if (--t_profundidad == 0 && t_nPend > 0) c_.vaciarLocal();
}

nlohmann::json Conmutador::estado() const {
    std::size_t webrtc = 0, relays = 0;
    {
        std::lock_guard<std::mutex> l(m_);
        for (const auto& [k, p] : puntos_) (p->clase == Clase::RelayTurn ? relays : webrtc)++;
    }
    return {{"hilos", hilos_},
            {"socketsWebrtc", webrtc},
            {"relays", relays},
            {"directos", directos_.load(std::memory_order_relaxed)},
            {"encolados", encolados_.load(std::memory_order_relaxed)},
            {"cruzados", cruzados_.load(std::memory_order_relaxed)},
            {"despertadas", despertadas_.load(std::memory_order_relaxed)},
            {"descartes", descartes_.load(std::memory_order_relaxed)}};
}

} // namespace ba::gateway::memoria
