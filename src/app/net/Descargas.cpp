// ============================================================================
// src/app/net/Descargas.cpp — ver Descargas.hpp
// ============================================================================
#include "net/Descargas.hpp"
#include "ajustes/Ajustes.hpp"

#include "net/HttpsClient.hpp"

#include <QDateTime>
#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <fstream>

namespace ba::app::net {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
Descargas* g_la = nullptr;
inline int kObreros() { return std::clamp(ba::ajustes::entero("descargas.simultaneas", 2), 1, 8); } // se lee al arrancar
inline std::size_t kHistorial() { return static_cast<std::size_t>(ba::ajustes::entero("descargas.historial", 100)); }

std::int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

const char* nombreEstado(Descargas::Estado e) {
    switch (e) {
        case Descargas::Estado::EnCola: return "en_cola";
        case Descargas::Estado::Bajando: return "bajando";
        case Descargas::Estado::Listo: return "listo";
        case Descargas::Estado::Error: return "error";
        case Descargas::Estado::Cancelado: return "cancelado";
    }
    return "?";
}
} // namespace

const char* Descargas::texto(Resultado r) {
    switch (r) {
        case Resultado::Encolada: return "encolada";
        case Resultado::YaEnCola: return "ya estaba en la cola";
        case Resultado::Bajando: return "ya se está bajando";
        case Resultado::YaEsta: return "ya está bajado";
        case Resultado::Invalida: return "pedido inválido";
    }
    return "?";
}

Descargas& Descargas::la() {
    // Por si alguien la pide antes de iniciar() (tests): sin historial.
    if (g_la == nullptr) iniciar({});
    return *g_la;
}

void Descargas::iniciar(const fs::path& raizDatos) {
    if (g_la != nullptr) return;
    g_la = new Descargas(raizDatos);
}

Descargas::Descargas(fs::path raiz) : raiz_(std::move(raiz)) {
    leerHistorial();
    for (int i = 0; i < kObreros(); ++i) obreros_.emplace_back([this] { correr(); });
}

Descargas::~Descargas() {
    {
        std::lock_guard<std::mutex> l(mu_);
        fin_ = true;
        for (auto& i : items_)
            if (i->estado == Estado::Bajando || i->estado == Estado::EnCola) i->cancelado->store(true);
    }
    hay_.notify_all();
    for (auto& t : obreros_)
        if (t.joinable()) t.join();
    if (buscador_.joinable()) buscador_.join();
}

Descargas::Resultado Descargas::pedir(Pedido p) {
    if (p.id.empty() || !p.trabajo) return Resultado::Invalida;
    {
        std::lock_guard<std::mutex> l(mu_);
        for (const auto& i : items_)
            if (i->pedido.id == p.id) {
                if (i->estado == Estado::EnCola) return Resultado::YaEnCola;
                if (i->estado == Estado::Bajando) return Resultado::Bajando;
            }
    }
    if (p.yaEsta && p.yaEsta()) return Resultado::YaEsta;
    auto it = std::make_shared<Item>();
    it->pedido = std::move(p);
    it->desde = ahoraMs();
    it->texto = "En la cola";
    {
        std::lock_guard<std::mutex> l(mu_);
        // Lo terminado con el mismo id sale del historial (queda el nuevo).
        items_.erase(std::remove_if(items_.begin(), items_.end(),
                                    [&](const std::shared_ptr<Item>& x) {
                                        return x->pedido.id == it->pedido.id &&
                                               x->estado != Estado::EnCola && x->estado != Estado::Bajando;
                                    }),
                     items_.end());
        items_.push_back(it);
    }
    hay_.notify_all();
    avisar();
    return Resultado::Encolada;
}

bool Descargas::ocupado(const std::string& id) const {
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& i : items_)
        if (i->pedido.id == id && (i->estado == Estado::EnCola || i->estado == Estado::Bajando)) return true;
    return false;
}

bool Descargas::avanceDe(const std::string& id, double* fraccion, std::string* texto) const {
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& i : items_)
        if (i->pedido.id == id && (i->estado == Estado::EnCola || i->estado == Estado::Bajando)) {
            if (fraccion != nullptr) *fraccion = i->progreso;
            if (texto != nullptr) *texto = i->texto;
            return true;
        }
    return false;
}

std::shared_ptr<Descargas::Item> Descargas::tomar() {
    // El primero en cola, si hay un obrero libre (de a dos: cada pedido tiene su
    // carpeta aparte, así que dos del mismo tipo no se pisan).
    // Lo que espera a otro (despuesDe) sigue en la cola hasta que ése termine.
    const auto pendiente = [this](const std::string& id) {
        return std::any_of(items_.begin(), items_.end(), [&](const std::shared_ptr<Item>& x) {
            return x->pedido.id == id && (x->estado == Estado::EnCola || x->estado == Estado::Bajando);
        });
    };
    for (auto& i : items_)
        if (i->estado == Estado::EnCola &&
            std::none_of(i->pedido.despuesDe.begin(), i->pedido.despuesDe.end(), pendiente))
            return i;
    return nullptr;
}

void Descargas::correr() {
    for (;;) {
        std::shared_ptr<Item> it;
        {
            std::unique_lock<std::mutex> l(mu_);
            hay_.wait(l, [&] { return fin_ || (it = tomar()) != nullptr; });
            if (fin_) return;
            it->estado = Estado::Bajando;
            it->texto = "Empezando…";
        }
        avisar();
        Contexto ctx;
        ctx.d_ = this;
        ctx.id_ = it->pedido.id;
        ctx.cancelado_ = it->cancelado;
        std::string err;
        try {
            err = it->pedido.trabajo(ctx);
        } catch (const std::exception& e) {
            err = std::string("falló: ") + e.what();
        }
        const bool cancelado = it->cancelado->load();
        {
            std::lock_guard<std::mutex> l(mu_);
            it->hasta = ahoraMs();
            it->estado = cancelado ? Estado::Cancelado : err.empty() ? Estado::Listo : Estado::Error;
            it->error = cancelado ? "cancelada" : err;
            it->texto = cancelado ? "Cancelada" : err.empty() ? "Listo" : err;
            if (err.empty() && !cancelado) it->progreso = 1.0;
            it->reintentable = !err.empty() || cancelado;
        }
        hay_.notify_all(); // puede destrabar otro del mismo tipo
        guardarHistorial();
        avisar();
        // alTerminar y la señal, en el hilo de la interfaz.
        const bool ok = err.empty() && !cancelado;
        const std::string error = cancelado ? "cancelada" : err;
        QMetaObject::invokeMethod(this, [this, it, ok, error]() {
            if (it->pedido.alTerminar) it->pedido.alTerminar(ok, error);
            emit terminada(QString::fromStdString(it->pedido.id), QString::fromStdString(it->pedido.titulo), ok,
                           QString::fromStdString(error));
        });
    }
}

void Descargas::Contexto::avance(double fraccion, const std::string& texto) {
    {
        std::lock_guard<std::mutex> l(d_->mu_);
        for (auto& i : d_->items_)
            if (i->pedido.id == id_ && i->estado == Estado::Bajando) {
                i->progreso = std::clamp(fraccion, 0.0, 1.0);
                i->texto = texto;
            }
    }
    d_->avisar();
}

std::string Descargas::Contexto::bajar(const std::string& url, const fs::path& destino, std::uint64_t base,
                                       std::uint64_t total, const std::string& nombre) {
    std::error_code ec;
    fs::create_directories(destino.parent_path(), ec);
    const fs::path parte = destino.string() + ".parte";
    const std::uint64_t ya = fs::exists(parte, ec) ? fs::file_size(parte, ec) : 0;
    HttpRequest req;
    req.url = url;
    req.responseFile = parte.string();
    req.resumeFrom = ya;
    req.timeoutMs = ba::ajustes::sAMs("descargas.timeoutS", 60);
    req.cancel = cancelado_.get();
    const auto t0 = std::chrono::steady_clock::now();
    const std::string quien = nombre.empty() ? destino.filename().string() : nombre;
    req.downloadProgress = [this, base, total, ya, t0, quien](std::uint64_t got, std::uint64_t tot) {
        const double seg = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const double kbps = seg > 0.5 ? static_cast<double>(got > ya ? got - ya : 0) / 1024.0 / seg : 0;
        const std::uint64_t todo = total > 0 ? total : tot;
        const std::uint64_t hecho = base + got;
        {
            std::lock_guard<std::mutex> l(d_->mu_);
            for (auto& i : d_->items_)
                if (i->pedido.id == id_ && i->estado == Estado::Bajando) {
                    i->bytes = hecho;
                    i->total = todo;
                    i->kbps = kbps;
                    i->progreso = todo > 0 ? static_cast<double>(hecho) / static_cast<double>(todo) : 0;
                    char t[160];
                    std::snprintf(t, sizeof t, "Bajando %s… %.0f / %.0f MB", quien.c_str(),
                                  static_cast<double>(hecho) / 1048576.0, static_cast<double>(todo) / 1048576.0);
                    i->texto = t;
                }
        }
        d_->avisar();
    };
    const HttpResponse res = HttpsClient::download(req);
    if (cancelado()) return "cancelada";
    // 416: la parte ya estaba completa (o el servidor no la reconoce): de cero.
    if (res.status == 416) {
        fs::remove(parte, ec);
        req.resumeFrom = 0;
        const HttpResponse otra = HttpsClient::download(req);
        if (!otra.error.empty() || otra.status / 100 != 2)
            return "no se pudo bajar " + quien + ": " + (otra.error.empty() ? "HTTP " + std::to_string(otra.status) : otra.error);
    } else if (!res.error.empty() || res.status / 100 != 2) {
        // La parte queda: el próximo intento sigue desde ahí.
        return "no se pudo bajar " + quien + ": " + (res.error.empty() ? "HTTP " + std::to_string(res.status) : res.error);
    }
    fs::rename(parte, destino, ec);
    if (ec) return "no se pudo dejar " + destino.string() + ": " + ec.message();
    return {};
}

std::string Descargas::Contexto::enLaInterfaz(
    std::function<void(std::function<void(const std::string&)>)> arrancar, std::function<void()> cortar) {
    struct Espera {
        std::mutex m;
        std::condition_variable c;
        bool fin = false;
        std::string error;
    };
    auto e = std::make_shared<Espera>();
    auto listo = [e](const std::string& err) {
        std::lock_guard<std::mutex> l(e->m);
        if (e->fin) return;
        e->fin = true;
        e->error = err;
        e->c.notify_all();
    };
    QMetaObject::invokeMethod(d_, [arrancar, listo]() { arrancar(listo); });
    bool cortado = false;
    std::unique_lock<std::mutex> l(e->m);
    while (!e->fin) {
        e->c.wait_for(l, std::chrono::milliseconds(200));
        if (!e->fin && cancelado() && !cortado) {
            cortado = true;
            if (cortar) QMetaObject::invokeMethod(d_, cortar);
        }
    }
    return e->error;
}

void Descargas::cancelar(const QString& id) {
    bool sacada = false;
    {
        std::lock_guard<std::mutex> l(mu_);
        for (auto& i : items_)
            if (i->pedido.id == id.toStdString()) {
                if (i->estado == Estado::Bajando) i->cancelado->store(true);
                if (i->estado == Estado::EnCola) {
                    i->estado = Estado::Cancelado;
                    i->texto = "Cancelada";
                    i->error = "cancelada";
                    i->hasta = ahoraMs();
                    i->reintentable = true;
                    sacada = true;
                }
            }
    }
    if (sacada) guardarEnElFondo();
    avisar();
}

bool Descargas::reintentar(const QString& id) {
    Pedido p;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = std::find_if(items_.begin(), items_.end(), [&](const std::shared_ptr<Item>& x) {
            return x->pedido.id == id.toStdString() && x->reintentable && x->pedido.trabajo;
        });
        if (it == items_.end()) return false;
        p = (*it)->pedido;
    }
    return pedir(std::move(p)) == Resultado::Encolada;
}

void Descargas::limpiarTerminadas() {
    {
        std::lock_guard<std::mutex> l(mu_);
        items_.erase(std::remove_if(items_.begin(), items_.end(),
                                    [](const std::shared_ptr<Item>& x) {
                                        return x->estado != Estado::EnCola && x->estado != Estado::Bajando;
                                    }),
                     items_.end());
    }
    guardarEnElFondo();
    avisar();
}

void Descargas::avisar() {
    // Desde cualquier hilo; a la interfaz, a lo sumo cuatro veces por segundo.
    if (pendienteAviso_.exchange(true)) return;
    QMetaObject::invokeMethod(this, [this]() {
        QTimer::singleShot(250, this, [this]() {
            pendienteAviso_ = false;
            emit cambio();
        });
    });
}

QVariantMap Descargas::aMapa(const Item& i) const {
    return QVariantMap{{"id", QString::fromStdString(i.pedido.id)},
                       {"titulo", QString::fromStdString(i.pedido.titulo)},
                       {"tipo", QString::fromStdString(i.pedido.tipo)},
                       {"version", QString::fromStdString(i.pedido.version)},
                       {"estado", nombreEstado(i.estado)},
                       {"progreso", i.progreso},
                       {"texto", QString::fromStdString(i.texto)},
                       {"bytes", static_cast<double>(i.bytes)},
                       {"total", static_cast<double>(i.total)},
                       {"kbps", i.kbps},
                       {"error", QString::fromStdString(i.error)},
                       {"desde", static_cast<double>(i.desde)},
                       {"hasta", static_cast<double>(i.hasta)},
                       {"reintentable", i.reintentable && static_cast<bool>(i.pedido.trabajo)}};
}

QVariantList Descargas::lista() const {
    std::lock_guard<std::mutex> l(mu_);
    QVariantList out;
    // Primero lo que anda y lo que espera; después el historial, lo último arriba.
    for (const auto& i : items_)
        if (i->estado == Estado::Bajando) out << aMapa(*i);
    for (const auto& i : items_)
        if (i->estado == Estado::EnCola) out << aMapa(*i);
    for (auto it = items_.rbegin(); it != items_.rend(); ++it)
        if ((*it)->estado != Estado::Bajando && (*it)->estado != Estado::EnCola) out << aMapa(**it);
    return out;
}

int Descargas::activas() const {
    std::lock_guard<std::mutex> l(mu_);
    return static_cast<int>(std::count_if(items_.begin(), items_.end(), [](const std::shared_ptr<Item>& x) {
        return x->estado == Estado::Bajando || x->estado == Estado::EnCola;
    }));
}

json Descargas::estado() const {
    json lista = json::array();
    for (const QVariant& v : this->lista()) {
        const QVariantMap m = v.toMap();
        json j = json::object();
        for (auto it = m.begin(); it != m.end(); ++it) {
            const QVariant& x = it.value();
            if (x.typeId() == QMetaType::Bool) j[it.key().toStdString()] = x.toBool();
            else if (x.typeId() == QMetaType::Double) j[it.key().toStdString()] = x.toDouble();
            else j[it.key().toStdString()] = x.toString().toStdString();
        }
        lista.push_back(j);
    }
    json act = json::array();
    {
        std::lock_guard<std::mutex> l(mu_);
        for (const auto& a : actualizaciones_)
            act.push_back({{"id", a.pedido.id}, {"titulo", a.pedido.titulo}, {"tipo", a.pedido.tipo},
                           {"version", a.pedido.version}, {"actual", a.actual}, {"detalle", a.detalle}});
    }
    return {{"descargas", lista}, {"actualizaciones", act}, {"buscando", buscando_.load()}};
}

// --- Actualizaciones ----------------------------------------------------------------------

void Descargas::registrarBuscador(const std::string& tipo, Buscador b) {
    std::lock_guard<std::mutex> l(mu_);
    buscadores_[tipo] = std::move(b);
}

void Descargas::buscarActualizaciones() {
    if (buscando_.exchange(true)) return;
    emit actualizacionesCambio();
    if (buscador_.joinable()) buscador_.join();
    std::map<std::string, Buscador> bs;
    {
        std::lock_guard<std::mutex> l(mu_);
        bs = buscadores_;
    }
    buscador_ = std::thread([this, bs]() {
        std::vector<Actualizacion> todas;
        for (const auto& [tipo, b] : bs) {
            try {
                for (auto& a : b()) todas.push_back(std::move(a));
            } catch (...) {
            }
        }
        QMetaObject::invokeMethod(this, [this, todas]() {
            {
                std::lock_guard<std::mutex> l(mu_);
                actualizaciones_ = todas;
            }
            buscando_ = false;
            emit actualizacionesCambio();
        });
    });
}

bool Descargas::actualizar(const QString& id) {
    Pedido p;
    std::function<void()> aplicar;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = std::find_if(actualizaciones_.begin(), actualizaciones_.end(),
                               [&](const Actualizacion& a) { return a.pedido.id == id.toStdString(); });
        if (it == actualizaciones_.end()) return false;
        p = it->pedido;
        p.yaEsta = nullptr; // es justamente para reemplazar lo que hay
        aplicar = it->aplicar;
        actualizaciones_.erase(it);
    }
    emit actualizacionesCambio();
    if (aplicar) {
        aplicar();
        return true;
    }
    return pedir(std::move(p)) != Resultado::Invalida;
}

QVariantList Descargas::actualizaciones() const {
    std::lock_guard<std::mutex> l(mu_);
    QVariantList out;
    for (const auto& a : actualizaciones_)
        out << QVariantMap{{"id", QString::fromStdString(a.pedido.id)},
                           {"titulo", QString::fromStdString(a.pedido.titulo)},
                           {"tipo", QString::fromStdString(a.pedido.tipo)},
                           {"version", QString::fromStdString(a.pedido.version)},
                           {"actual", QString::fromStdString(a.actual)},
                           {"detalle", QString::fromStdString(a.detalle)}};
    return out;
}

// --- Historial ------------------------------------------------------------------------------

void Descargas::guardarEnElFondo() {
    // Desde la interfaz: el archivo se escribe en otro hilo (un disco trabado no
    // la cuelga).
    std::thread([this]() { guardarHistorial(); }).detach();
}

void Descargas::guardarHistorial() {
    if (raiz_.empty()) return;
    std::lock_guard<std::mutex> lh(muHistorial_);
    json h = json::array();
    {
        std::lock_guard<std::mutex> l(mu_);
        for (const auto& i : items_) {
            if (i->estado == Estado::EnCola || i->estado == Estado::Bajando) continue;
            h.push_back({{"id", i->pedido.id}, {"titulo", i->pedido.titulo}, {"tipo", i->pedido.tipo},
                         {"version", i->pedido.version}, {"estado", nombreEstado(i->estado)}, {"error", i->error},
                         {"bytes", i->bytes}, {"desde", i->desde}, {"hasta", i->hasta}});
        }
    }
    while (h.size() > kHistorial()) h.erase(h.begin());
    std::error_code ec;
    fs::create_directories(raiz_ / "Runtime", ec);
    std::ofstream o(raiz_ / "Runtime" / "descargas.json", std::ios::trunc);
    o << h.dump(1, ' ', false, json::error_handler_t::replace);
}

void Descargas::leerHistorial() {
    if (raiz_.empty()) return;
    std::ifstream f(raiz_ / "Runtime" / "descargas.json");
    const json h = json::parse(f, nullptr, false);
    if (!h.is_array()) return;
    for (const json& j : h) {
        auto it = std::make_shared<Item>();
        it->pedido.id = j.value("id", std::string());
        it->pedido.titulo = j.value("titulo", std::string());
        it->pedido.tipo = j.value("tipo", std::string());
        it->pedido.version = j.value("version", std::string());
        const std::string e = j.value("estado", std::string());
        it->estado = e == "listo" ? Estado::Listo : e == "cancelado" ? Estado::Cancelado : Estado::Error;
        it->error = j.value("error", std::string());
        it->texto = it->estado == Estado::Listo ? "Listo" : it->error;
        it->bytes = j.value("bytes", std::uint64_t{0});
        it->total = it->bytes;
        it->progreso = it->estado == Estado::Listo ? 1.0 : 0.0;
        it->desde = j.value("desde", std::int64_t{0});
        it->hasta = j.value("hasta", std::int64_t{0});
        // Del historial no se puede reintentar (no hay trabajo): se pide de nuevo desde su lugar.
        it->reintentable = false;
        if (!it->pedido.id.empty()) items_.push_back(it);
    }
}

} // namespace ba::app::net
