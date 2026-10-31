// src/core/persist/ColaEscritura.cpp — ver el .hpp

#include "persist/ColaEscritura.hpp"

#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"

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

ColaEscritura::ColaEscritura() {
    // El registro nace antes que la cola: así muere después y el hilo lo puede usar
    // hasta el final (los static locales se destruyen en orden inverso).
    (void)ba::diag::registro();
    hilo_ = std::thread([this]() { correr(); });
}

ColaEscritura::~ColaEscritura() {
    vaciar();
    {
        std::lock_guard<std::mutex> l(mu_);
        parar_ = true;
    }
    hay_.notify_all();
    if (hilo_.joinable()) hilo_.join();
}

void ColaEscritura::encolar(Tipo t, const fs::path& ruta, std::string bytes, Pedido p) {
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
    encolar(Tipo::Archivo, ruta, std::move(bytes), std::move(p));
}

void ColaEscritura::escribirGeneracion(const fs::path& archivo, std::string cuerpo, Pedido p) {
    encolar(Tipo::Generacion, archivo, std::move(cuerpo), std::move(p));
}

void ColaEscritura::borrar(const fs::path& ruta, Prioridad prioridad) {
    Pedido p;
    p.prioridad = prioridad;
    encolar(Tipo::Borrar, ruta, {}, std::move(p));
}

bool ColaEscritura::leer(const fs::path& ruta, std::string& out) const {
    {
        std::lock_guard<std::mutex> l(mu_);
        const auto it = pendientes_.find(clave(ruta));
        if (it != pendientes_.end() && it->second.tipo != Tipo::Generacion) {
            if (it->second.tipo == Tipo::Borrar) return false;
            out = it->second.bytes;
            return true;
        }
    }
    return leerArchivo(ruta, out);
}

Lectura ColaEscritura::leerGeneraciones(const fs::path& archivo) const {
    {
        std::lock_guard<std::mutex> l(mu_);
        const auto it = pendientes_.find(clave(archivo));
        if (it != pendientes_.end()) {
            Lectura x;
            if (it->second.tipo == Tipo::Borrar) return x; // Nada
            x.estado = Lectura::Estado::Valida;
            x.cuerpo = it->second.bytes;
            return x;
        }
    }
    return DosGeneraciones(archivo).leer();
}

bool ColaEscritura::vaciar(std::chrono::milliseconds tope) {
    if (esHiloEscritor()) return false;
    std::unique_lock<std::mutex> l(mu_);
    return listo_.wait_for(l, tope, [this]() { return pendientes_.empty(); });
}

ColaEscritura::Cuentas ColaEscritura::cuentas() const {
    std::lock_guard<std::mutex> l(mu_);
    return cuentas_;
}

bool ColaEscritura::hacer(const Entrada& e, std::string& error) {
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
    }
    return false;
}

void ColaEscritura::correr() {
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
        if (ok) {
            ++cuentas_.escrituras;
            if (it != pendientes_.end() && it->second.version == e.version) {
                if (e.tipo == Tipo::Generacion && !e.igualar) {
                    // Lo mismo a la otra generación, si en kIgualar no llega nada nuevo.
                    Entrada& c = it->second;
                    c.igualar = true;
                    c.enCurso = false;
                    c.version = ++version_;
                    c.pedido = Pedido{e.pedido.durabilidad, e.pedido.prioridad, e.pedido.permisos, {}};
                    c.reintento = Reloj::now() + kIgualar;
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
