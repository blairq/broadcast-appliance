// src/core/persist/ColaEscritura.cpp — ver el .hpp

#include "persist/ColaEscritura.hpp"

#include "diag/Registro.hpp"
#include "i18n/I18nTexto.hpp"
#include "persist/Escritor.hpp"
#include "persist/Generaciones.hpp"

#include <algorithm>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>

namespace ba::persist {

namespace fs = std::filesystem;
using Reloj = std::chrono::steady_clock;

namespace {
std::string clave(const fs::path& ruta) { return ruta.lexically_normal().string(); }
constexpr auto kReintento = std::chrono::milliseconds(500);
/// Una generación escrita y nada nuevo en este tiempo: lo mismo va a la otra. Sin esto
/// la otra se queda con el estado de antes del último cambio, que puede ser de hace
/// horas (se escribe solo al cambiar): un corte a mitad de la escritura siguiente
/// volvería a eso. Así pierde, a lo sumo, lo de este último momento.
constexpr auto kIgualar = std::chrono::seconds(1);
} // namespace

struct ColaEscritura::Impl {
    enum class Tipo { Archivo, Generacion, Borrar, Agregar };
    struct Entrada {
        Tipo tipo = Tipo::Archivo;
        fs::path ruta;
        std::string bytes; ///< Agregar: lo que falta agregar (lo que llega se suma al final)
        Pedido pedido;
        std::uint64_t orden = 0;   ///< cuándo se encoló por primera vez (FIFO entre iguales)
        std::uint64_t version = 0; ///< sube con cada pedido nuevo para la ruta
        std::uint64_t tomada = 0;  ///< la versión que el hilo está escribiendo
        bool enCurso = false;      ///< el hilo la está escribiendo
        bool avisado = false;      ///< su error ya fue al registro
        bool igualar = false;      ///< la copia a la otra generación (ver kIgualar)
        Reloj::time_point reintento{};
    };
    void encolar(Tipo t, const fs::path& ruta, std::string bytes, Pedido p);
    void correr();
    bool hacer(const Entrada& e, std::string& error);

    mutable std::mutex mu_;
    std::condition_variable hay_;   ///< hay algo para el hilo (o hay que parar)
    std::condition_variable listo_; ///< el hilo terminó algo (para vaciar)
    std::map<std::string, Entrada> pendientes_;
    std::uint64_t orden_ = 0;
    std::uint64_t version_ = 0;
    bool parar_ = false;
    int apurando_ = 0; ///< cuántos esperan en vaciar(): la igualación de generaciones, sin demora
    Cuentas cuentas_;
    std::map<std::string, DosGeneraciones> generaciones_; ///< del hilo escritor (secuencia y turno)
    std::thread hilo_;
};

ColaEscritura::ColaEscritura() : d_(std::make_unique<Impl>()) {
    // El registro nace antes que la cola: así muere después y el hilo lo puede usar
    // hasta el final (los static locales se destruyen en orden inverso).
    (void)ba::diag::registro();
    d_->hilo_ = std::thread([d = d_.get()]() { d->correr(); });
}

ColaEscritura::~ColaEscritura() {
    vaciar();
    {
        std::lock_guard<std::mutex> l(d_->mu_);
        d_->parar_ = true;
    }
    d_->hay_.notify_all();
    if (d_->hilo_.joinable()) d_->hilo_.join();
}

bool ColaEscritura::esHiloEscritor() const { return std::this_thread::get_id() == d_->hilo_.get_id(); }

void ColaEscritura::Impl::encolar(Tipo t, const fs::path& ruta, std::string bytes, Pedido p) {
    {
        std::lock_guard<std::mutex> l(mu_);
        ++cuentas_.pedidos;
        Entrada& e = pendientes_[clave(ruta)];
        if (e.version == 0) {
            e.orden = ++orden_;
        } else if (e.version != e.tomada) {
            ++cuentas_.fusionados; // lo anterior todavía no se escribió: gana esto
        }
        e.tipo = t;
        e.ruta = ruta;
        e.bytes = std::move(bytes);
        // Si lo anterior era de aire, sigue adelante aunque esto no lo sea.
        if (e.version != 0 && e.pedido.prioridad == Prioridad::Aire) p.prioridad = Prioridad::Aire;
        e.pedido = std::move(p);
        e.version = ++version_;
        e.reintento = {};
        e.igualar = false;
    }
    hay_.notify_one();
}

void ColaEscritura::escribir(const fs::path& ruta, std::string bytes, Pedido p) {
    d_->encolar(Impl::Tipo::Archivo, ruta, std::move(bytes), std::move(p));
}

void ColaEscritura::escribirGeneracion(const fs::path& archivo, std::string cuerpo, Pedido p) {
    d_->encolar(Impl::Tipo::Generacion, archivo, std::move(cuerpo), std::move(p));
}

void ColaEscritura::agregar(const fs::path& ruta, std::string bytes, Pedido p) {
    {
        std::lock_guard<std::mutex> l(d_->mu_);
        ++d_->cuentas_.pedidos;
        // Aparte de lo que reemplaza el archivo entero (otra clave): un diario solo se suma.
        Impl::Entrada& e = d_->pendientes_[clave(ruta) + "\n+"];
        if (e.version == 0) {
            e.orden = ++d_->orden_;
            e.tipo = Impl::Tipo::Agregar;
            e.ruta = ruta;
            e.pedido = std::move(p);
        }
        // Al final de lo que falta (aunque el hilo esté agregando lo anterior: al terminar
        // saca solo lo que escribió).
        e.bytes += bytes;
        e.version = ++d_->version_;
        e.reintento = {};
    }
    d_->hay_.notify_one();
}

bool ColaEscritura::existe(const fs::path& ruta) const {
    {
        std::lock_guard<std::mutex> l(d_->mu_);
        const auto it = d_->pendientes_.find(clave(ruta));
        if (it != d_->pendientes_.end()) return it->second.tipo != Impl::Tipo::Borrar;
        if (d_->pendientes_.count(clave(ruta) + "\n+") != 0) return true;
    }
    std::error_code ec;
    return fs::exists(ruta, ec);
}

void ColaEscritura::borrar(const fs::path& ruta, Prioridad prioridad) {
    Pedido p;
    p.prioridad = prioridad;
    d_->encolar(Impl::Tipo::Borrar, ruta, {}, std::move(p));
}

bool ColaEscritura::leer(const fs::path& ruta, std::string& out) const {
    std::string falta; // lo encolado para agregar al final (un diario)
    {
        std::lock_guard<std::mutex> l(d_->mu_);
        if (const auto a = d_->pendientes_.find(clave(ruta) + "\n+"); a != d_->pendientes_.end()) falta = a->second.bytes;
        const auto it = d_->pendientes_.find(clave(ruta));
        if (it != d_->pendientes_.end() && it->second.tipo != Impl::Tipo::Generacion) {
            if (it->second.tipo == Impl::Tipo::Borrar) return false;
            out = it->second.bytes;
            return true;
        }
    }
    if (!leerArchivo(ruta, out) && falta.empty()) return false;
    out += falta;
    return true;
}

Lectura ColaEscritura::leerGeneraciones(const fs::path& archivo) const {
    {
        std::lock_guard<std::mutex> l(d_->mu_);
        const auto it = d_->pendientes_.find(clave(archivo));
        if (it != d_->pendientes_.end()) {
            Lectura x;
            if (it->second.tipo == Impl::Tipo::Borrar) return x; // Nada
            x.estado = Lectura::Estado::Valida;
            x.cuerpo = it->second.bytes;
            return x;
        }
    }
    return DosGeneraciones(archivo).leer();
}

bool ColaEscritura::vaciar(std::chrono::milliseconds tope) {
    if (esHiloEscritor()) return false;
    std::unique_lock<std::mutex> l(d_->mu_);
    // Quien espera no espera la copia a la otra generación: kIgualar solo junta cambios
    // seguidos, y el que vacía (el cierre) ya no va a mandar más. Sin esto el cierre de la
    // app esperaba siempre más de 1 s (el documento de aire se escribe al cerrar) y el
    // vigía de la interfaz lo daba por trabado (volcados «traba» del 2026-11-03 al salir).
    ++d_->apurando_;
    const auto ahora = Reloj::now();
    for (auto& [k, e] : d_->pendientes_)
        if (e.igualar && !e.enCurso && e.reintento > ahora) e.reintento = ahora;
    d_->hay_.notify_all();
    const bool ok = d_->listo_.wait_for(l, tope, [this]() { return d_->pendientes_.empty(); });
    --d_->apurando_;
    return ok;
}

ColaEscritura::Cuentas ColaEscritura::cuentas() const {
    std::lock_guard<std::mutex> l(d_->mu_);
    return d_->cuentas_;
}

bool ColaEscritura::Impl::hacer(const Entrada& e, std::string& error) {
    if (e.pedido.antes) e.pedido.antes();
    switch (e.tipo) {
        case Tipo::Archivo:
            return persist::escribir(e.ruta, e.bytes, e.pedido.durabilidad, e.pedido.permisos, &error);
        case Tipo::Generacion: {
            auto it = generaciones_.try_emplace(clave(e.ruta), e.ruta).first;
            return it->second.escribir(e.bytes, e.pedido.durabilidad, &error);
        }
        case Tipo::Borrar:
            generaciones_.erase(clave(e.ruta));
            // Las generaciones (si las hay) y el archivo de siempre.
            return DosGeneraciones(e.ruta).borrar();
        case Tipo::Agregar:
            return persist::agregar(e.ruta, e.bytes, e.pedido.durabilidad, &error);
    }
    return false;
}

void ColaEscritura::Impl::correr() {
    std::unique_lock<std::mutex> l(mu_);
    for (;;) {
        // El destructor ya esperó lo que pudo (vaciar): lo que quede es de un disco
        // que no responde y no se va a escribir.
        if (parar_) return;
        // La próxima: la de aire más vieja; si no hay, la más vieja. Las que fallaron
        // esperan su reintento.
        const auto ahora = Reloj::now();
        auto elegida = pendientes_.end();
        Reloj::time_point proxima = Reloj::time_point::max();
        for (auto it = pendientes_.begin(); it != pendientes_.end(); ++it) {
            const Entrada& e = it->second;
            if (e.enCurso) continue;
            if (e.reintento > ahora) {
                proxima = std::min(proxima, e.reintento);
                continue;
            }
            if (elegida == pendientes_.end()) {
                elegida = it;
                continue;
            }
            const Entrada& m = elegida->second;
            const bool aire = e.pedido.prioridad == Prioridad::Aire, aireM = m.pedido.prioridad == Prioridad::Aire;
            if ((aire && !aireM) || (aire == aireM && e.orden < m.orden)) elegida = it;
        }
        if (elegida == pendientes_.end()) {
            if (proxima == Reloj::time_point::max())
                hay_.wait(l);
            else
                hay_.wait_until(l, proxima);
            continue;
        }
        const std::string k = elegida->first;
        elegida->second.enCurso = true;
        elegida->second.tomada = elegida->second.version;
        const Entrada e = elegida->second; // copia: se escribe sin el candado
        l.unlock();
        std::string error;
        const bool ok = hacer(e, error);
        l.lock();
        auto it = pendientes_.find(k);
        if (ok && e.tipo == Tipo::Agregar && it != pendientes_.end()) {
            // Se agregó lo que había al tomarla; lo que llegó mientras tanto sigue.
            ++cuentas_.escrituras;
            it->second.bytes.erase(0, e.bytes.size());
            if (it->second.bytes.empty())
                pendientes_.erase(it);
            else
                it->second.enCurso = false;
        } else if (ok) {
            ++cuentas_.escrituras;
            if (it != pendientes_.end() && it->second.version == e.version) {
                if (e.tipo == Tipo::Generacion && !e.igualar) {
                    // Lo mismo a la otra generación, si en kIgualar no llega nada nuevo.
                    Entrada& c = it->second;
                    c.igualar = true;
                    c.enCurso = false;
                    c.version = ++version_;
                    c.pedido = Pedido{e.pedido.durabilidad, e.pedido.prioridad, e.pedido.permisos, {}};
                    c.reintento = apurando_ > 0 ? Reloj::now() : Reloj::now() + kIgualar; // ver vaciar
                } else {
                    pendientes_.erase(it);
                }
            }
            else if (it != pendientes_.end())
                it->second.enCurso = false; // llegó uno más nuevo mientras tanto
        } else {
            ++cuentas_.errores;
            if (it != pendientes_.end()) {
                it->second.enCurso = false;
                if (it->second.version == e.version) it->second.reintento = Reloj::now() + kReintento;
                if (!it->second.avisado) {
                    it->second.avisado = true;
                    l.unlock();
                    ba::diag::registro().linea(
                        ba::diag::Nivel::Error, "persistencia", 0,
                        ba::i18n::llenar(ba::i18n::traducir("persistencia", "no se pudo guardar %1 (se reintenta): %2"),
                                         {e.ruta.string(), error}));
                    l.lock();
                }
            }
        }
        listo_.notify_all();
    }
}

ColaEscritura& cola() {
    static ColaEscritura c;
    return c;
}

} // namespace ba::persist
