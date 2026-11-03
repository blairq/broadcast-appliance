// ============================================================================
// src/gateway/rtc/LoteTrabajo.cpp — ver LoteTrabajo.hpp
//
// Candados: solo el de cada Destino (m_), por un instante (marcar pendiente, ver si duerme).
// Nunca se llama a nada de afuera con él tomado. Lo del Remitente es de un solo hilo.
// ============================================================================
#include "rtc/LoteTrabajo.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "ajustes/Ajustes.hpp"
#include "diag/Frontera.hpp"

namespace ba::gateway::lote {

namespace {
thread_local Remitente* t_remitente = nullptr;
thread_local bool t_urgente = false;
thread_local const Destino* t_dueno = nullptr;
} // namespace

const char* nombre(Modo m) {
    switch (m) {
    case Modo::Separados:
        return "separados";
    case Modo::Juntos:
        return "juntos";
    case Modo::Lote:
        break;
    }
    return "lote";
}

Modo modoDeTexto(std::string_view s) {
    if (s == "separados") return Modo::Separados;
    if (s == "juntos") return Modo::Juntos;
    return Modo::Lote;
}

Modo modoDeLosAjustes() { return modoDeTexto(ba::ajustes::texto("gateway.redYTrabajo", "lote")); }

std::int64_t loteNsDeLosAjustes() {
    return std::int64_t(std::clamp(ba::ajustes::entero("gateway.loteTrabajoUs", 500), 0, 5000)) * 1000;
}

// ---------------------------------------------------------------------------- Destino

Destino::Destino(ba::diag::Frontera* frontera) : frontera_(frontera) {}

void Destino::soyElDueno() { t_dueno = this; }
bool Destino::esElDueno() const { return t_dueno == this; }

bool Destino::durmiendo() const {
    std::lock_guard<std::mutex> l(m_);
    return durmiendo_;
}

void Destino::esperar(std::int64_t maxNs) {
    const std::uint64_t t0 = ba::diag::relojNs();
    if (ultimoDespertarNs_ != 0) despiertoNs_.fetch_add(t0 - ultimoDespertarNs_, std::memory_order_relaxed);
    {
        std::unique_lock<std::mutex> l(m_);
        if (!pendiente_ && maxNs != 0) {
            durmiendo_ = true;
            if (maxNs < 0)
                cv_.wait(l, [this] { return pendiente_; });
            else
                cv_.wait_for(l, std::chrono::nanoseconds(maxNs), [this] { return pendiente_; });
            durmiendo_ = false; // si lo despertaron ya estaba en false; si venció el plazo, no
        }
        pendiente_ = false;
    }
    const std::uint64_t t1 = ba::diag::relojNs();
    dormidoNs_.fetch_add(t1 - t0, std::memory_order_relaxed);
    ultimoDespertarNs_ = t1;
}

bool Destino::despertar() {
    bool dormia = false;
    {
        std::lock_guard<std::mutex> l(m_);
        pendiente_ = true;
        if (durmiendo_) {
            // En false acá (no al volver de la espera): un segundo despertar antes de que el
            // dueño corra no cuenta otra despertada ni vuelve a notificar.
            durmiendo_ = false;
            dormia = true;
        }
    }
    if (dormia) {
        cv_.notify_one();
        despertadas_.fetch_add(1, std::memory_order_relaxed);
    }
    return dormia;
}

void Destino::avisar() {
    // El dueño avisándose a sí mismo: está corriendo y mira su cola (y sus diferidas) antes
    // de volver a esperar.
    if (t_dueno == this) return;
    avisos_.fetch_add(1, std::memory_order_relaxed);
    Remitente* r = t_remitente;
    if (r == nullptr) {
        despertar(); // otro hilo cualquiera: como siempre
        return;
    }
    deRed_.fetch_add(1, std::memory_order_relaxed);
    if (t_urgente || !r->enLote()) {
        if (t_urgente) urgentes_.fetch_add(1, std::memory_order_relaxed);
        const bool d = despertar();
        if (frontera_ != nullptr) {
            frontera_->pase(0);
            if (d) frontera_->despertada();
        }
        return;
    }
    anotados_.fetch_add(1, std::memory_order_relaxed);
    r->anotar(*this);
}

void Destino::contarAnotados(std::uint64_t n, std::uint64_t esperaNs, bool desperto) {
    if (frontera_ == nullptr) return;
    frontera_->lote(n, 0, 0, esperaNs + 1);
    if (desperto) frontera_->despertada();
}

Destino::Cuentas Destino::cuentas() const {
    Cuentas c;
    c.avisos = avisos_.load(std::memory_order_relaxed);
    c.deRed = deRed_.load(std::memory_order_relaxed);
    c.anotados = anotados_.load(std::memory_order_relaxed);
    c.urgentes = urgentes_.load(std::memory_order_relaxed);
    c.despertadas = despertadas_.load(std::memory_order_relaxed);
    c.dormidoNs = dormidoNs_.load(std::memory_order_relaxed);
    c.despiertoNs = despiertoNs_.load(std::memory_order_relaxed);
    return c;
}

nlohmann::json Destino::estado() const {
    const Cuentas c = cuentas();
    const double total = double(c.dormidoNs + c.despiertoNs);
    return {{"avisos", c.avisos},
            {"deRed", c.deRed},
            {"anotados", c.anotados},
            {"urgentes", c.urgentes},
            {"despertadas", c.despertadas},
            {"despierto_pct", total > 0 ? std::round(1000.0 * double(c.despiertoNs) / total) / 10.0 : 0.0}};
}

// ---------------------------------------------------------------------------- Remitente

Remitente::Remitente(bool enLote, std::int64_t loteNs) : enLote_(enLote), loteNs_(std::max<std::int64_t>(0, loteNs)) {
    anotados_.reserve(8);
}

Remitente::~Remitente() = default;

void Remitente::instalar(void (*alPrimero)(void*), void* ctx) {
    alPrimero_ = alPrimero;
    ctx_ = ctx;
    t_remitente = this;
}

void Remitente::desinstalar() {
    soltar();
    if (t_remitente == this) t_remitente = nullptr;
}

Remitente* Remitente::delHilo() { return t_remitente; }

void Remitente::anotar(Destino& d) {
    const std::uint64_t ahora = ba::diag::relojNs();
    bool ya = false;
    for (Anotado& a : anotados_)
        if (a.d.get() == &d) {
            ++a.n;
            ya = true;
            break;
        }
    if (!ya) {
        const bool primero = anotados_.empty();
        if (primero) primeraNs_ = ahora;
        anotados_.push_back({d.shared_from_this(), 1, ahora});
        if (primero) {
            if (alPrimero_ != nullptr) alPrimero_(ctx_);
            return;
        }
    }
    // Un hilo de red ocupado mucho rato sin terminar la tarea: que no espere de más.
    if (loteNs_ > 0 && std::int64_t(ahora - primeraNs_) >= loteNs_) soltar();
}

void Remitente::soltar() {
    if (anotados_.empty()) return;
    const std::uint64_t ahora = ba::diag::relojNs();
    for (Anotado& a : anotados_) {
        const bool d = a.d->despertar();
        a.d->contarAnotados(a.n, ahora - a.desdeNs, d);
    }
    anotados_.clear(); // suelta los shared_ptr; la capacidad queda
}

void Remitente::soltarSiVencio() {
    if (anotados_.empty()) return;
    if (loteNs_ == 0 || std::int64_t(ba::diag::relojNs() - primeraNs_) >= loteNs_) soltar();
}

Remitente::Urgente::Urgente() : antes_(t_urgente) { t_urgente = true; }
Remitente::Urgente::~Urgente() { t_urgente = antes_; }
bool Remitente::urgente() { return t_urgente; }

} // namespace ba::gateway::lote
