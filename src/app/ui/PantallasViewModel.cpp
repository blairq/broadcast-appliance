// ============================================================================
// src/app/ui/PantallasViewModel.cpp — ver PantallasViewModel.hpp
// ============================================================================
#include "ui/PantallasViewModel.hpp"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <thread>
#include <vector>

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QUrl>

#include "AppRuntime.hpp"
#include "pantallas/Buscar.hpp"
#include "pantallas/Cast.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"

namespace ba::app {
namespace {

using json = nlohmann::json;
namespace pt = pantallas;

/// Cuánto se espera a un aparato antes de darlo por perdido. Un televisor
/// dormido tarda en despertar; más de esto es que no está.
constexpr int kTimeoutMs = 8000;
/// Cuánto dura una búsqueda en la red.
constexpr int kBusquedaSeg = 5;

/// El catálogo de lo que se puede proyectar. Está acá y no en el QML porque
/// también lo necesita el MCP: una sola lista, no dos que se desincronizan.
struct Campo {
    const char* clave;
    const char* etiqueta;
    const char* ejemplo;
    bool largo; ///< si va en un cuadro de varias líneas
};
struct Contenido {
    const char* id;
    const char* titulo;
    const char* ayuda;
    std::vector<Campo> campos;
    bool disponible;
    const char* porque; ///< si no está disponible
};

const std::vector<Contenido>& catalogo() {
    static const std::vector<Contenido> c = {
        {"mensaje", "Un mensaje", "Texto grande, para que se lea desde el fondo de la sala.",
         {{"texto", "Texto", "Empezamos en 5 minutos", true},
          {"pie", "Abajo, más chico", "Sala principal", false}},
         true, ""},
        {"qr", "Un código QR", "El QR ocupa la pantalla entera: se escanea desde lejos.",
         {{"texto", "Qué codifica", "https://…", false},
          {"pie", "Abajo, más chico", "Escaneá para entrar a la sala", false}},
         true, ""},
        {"reloj", "La hora", "La hora, grande. Para el fondo del escenario.",
         {{"pie", "Abajo, más chico", "", false}},
         true, ""},
        {"url", "Una página web", "Cualquier dirección. Tiene que ser alcanzable DESDE el televisor.",
         {{"url", "Dirección", "http://…", false}},
         true, ""},
        {"prompter", "El teleprompter", "La vista del orador: el discurso que esté cargado.", {}, true, ""},
        // Declarado a propósito aunque no ande: que el operador vea que está
        // previsto es mejor que preguntarse si se puede.
        {"programa", "Lo que sale al aire", "El programa en vivo, en la pantalla.", {}, false,
         "Todavía no: para que el video llegue con poco retardo hace falta una página receptora con "
         "WebRTC que no está escrita (ver docs/PANTALLAS.md)."},
    };
    return c;
}

const Contenido* contenidoPorId(const QString& id) {
    for (const Contenido& c : catalogo())
        if (id == QString::fromUtf8(c.id)) return &c;
    return nullptr;
}

/// Un destino, tal como lo guardamos.
struct Destino {
    QString id, nombre, modelo, host;
    int puerto = pt::kPuertoCast;
    bool fija = false; ///< agregada a mano: no se borra al buscar de nuevo
    // --- lo que sabemos de cómo está (no se guarda) ---
    QString estado = "?"; ///< ? | libre | mostrando | apagada | error | ocupada
    QString contenido;    ///< el tipo que le mandamos
    QString que;          ///< legible: "QR — https://…"
    QString error;
    double volumen = -1;
    bool silenciado = false;
    bool trabajando = false;
    std::int64_t vistaMs = 0;
};

/// La dirección de ESTA máquina que está en la misma red que `host`, para que
/// la URL que le pasamos al televisor la pueda alcanzar de verdad.
QString ipParaAlcanzar(const QStringList& mias, const QString& host) {
    if (mias.isEmpty()) return {};
    const auto partes = [](const QString& ip) { return ip.split('.'); };
    const QStringList d = partes(host);
    QString mejor = mias.first();
    int mejorComun = -1;
    for (const QString& m : mias) {
        const QStringList p = partes(m);
        int comun = 0;
        const int tope = static_cast<int>(std::min<qsizetype>({p.size(), d.size(), 4}));
        for (int i = 0; i < tope; ++i) {
            if (p[i] != d[i]) break;
            ++comun;
        }
        if (comun > mejorComun) {
            mejorComun = comun;
            mejor = m;
        }
    }
    return mejor;
}

} // namespace

struct PantallasViewModel::Impl {
    AppRuntime* rt = nullptr;
    ConferenceViewModel* sala = nullptr;
    std::filesystem::path archivo;
    std::vector<Destino> destinos;
    std::atomic<bool> buscando{false};
    QString aviso;
    /// Las IP de esta máquina y el puerto HTTP del gateway, que se preguntan
    /// una vez y se refrescan al buscar.
    QStringList misIps;
    int puertoWeb = 8787;

    Destino* porId(const QString& id) {
        for (Destino& d : destinos)
            if (d.id == id) return &d;
        return nullptr;
    }

    void guardar() const {
        if (archivo.empty()) return;
        QDir().mkpath(QString::fromStdString(archivo.parent_path().string()));
        json o = json::array();
        for (const Destino& d : destinos) {
            // Las descubiertas también se guardan: así la página no arranca
            // vacía mientras se busca, y el operador ve lo que había ayer.
            o.push_back(json{{"id", d.id.toStdString()},
                             {"nombre", d.nombre.toStdString()},
                             {"modelo", d.modelo.toStdString()},
                             {"host", d.host.toStdString()},
                             {"puerto", d.puerto},
                             {"fija", d.fija}});
        }
        std::ofstream f(archivo, std::ios::binary | std::ios::trunc);
        f << o.dump(2) << "\n";
    }

    void leer() {
        if (archivo.empty()) return;
        std::ifstream f(archivo, std::ios::binary);
        if (!f) return;
        const json o = json::parse(f, nullptr, false);
        if (!o.is_array()) return;
        for (const json& x : o) {
            if (!x.is_object()) continue;
            Destino d;
            d.id = QString::fromStdString(x.value("id", ""));
            d.nombre = QString::fromStdString(x.value("nombre", ""));
            d.modelo = QString::fromStdString(x.value("modelo", ""));
            d.host = QString::fromStdString(x.value("host", ""));
            d.puerto = x.value("puerto", pt::kPuertoCast);
            d.fija = x.value("fija", false);
            if (d.id.isEmpty() || d.host.isEmpty()) continue;
            destinos.push_back(std::move(d));
        }
    }
};

PantallasViewModel::PantallasViewModel(AppRuntime& rt, ConferenceViewModel* sala, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>()) {
    d_->rt = &rt;
    d_->sala = sala;
    d_->archivo = rt.dataRoot() / "Config" / "pantallas.json";
    d_->leer();
    if (!pt::sePuedeBuscar())
        d_->aviso = "Este programa se compiló sin búsqueda en la red: agregá el televisor a mano con su "
                    "dirección IP.";
    // Las direcciones de esta máquina, para armar URLs que el televisor pueda
    // alcanzar. Se pregunta una vez al arrancar y en cada búsqueda.
    if (sala != nullptr) {
        sala->client().get("/api/v1/network", [this](bool ok, const QJsonObject& r) {
            if (!ok) return;
            d_->misIps.clear();
            for (const QJsonValue& v : r.value("lan").toArray()) d_->misIps << v.toString();
            emit cambio();
        });
    }
}

PantallasViewModel::~PantallasViewModel() = default;

// --- Lo que ve la interfaz -----------------------------------------------------------

QVariantList PantallasViewModel::pantallas() const {
    QVariantList out;
    const std::int64_t ahora = QDateTime::currentMSecsSinceEpoch();
    for (const Destino& d : d_->destinos)
        out.append(QVariantMap{{"id", d.id},
                               {"nombre", d.nombre},
                               {"modelo", d.modelo},
                               {"host", d.host},
                               {"puerto", d.puerto},
                               {"fija", d.fija},
                               {"estado", d.estado},
                               {"contenido", d.contenido},
                               {"que", d.que},
                               {"error", d.error},
                               {"volumen", d.volumen},
                               {"silenciado", d.silenciado},
                               {"trabajando", d.trabajando},
                               {"vistaHace", static_cast<int>(d.vistaMs > 0 ? (ahora - d.vistaMs) / 1000 : -1)}});
    return out;
}

QVariantList PantallasViewModel::contenidos() const {
    QVariantList out;
    for (const Contenido& c : catalogo()) {
        QVariantList campos;
        for (const Campo& f : c.campos)
            campos.append(QVariantMap{{"clave", QString::fromUtf8(f.clave)},
                                      {"etiqueta", QString::fromUtf8(f.etiqueta)},
                                      {"ejemplo", QString::fromUtf8(f.ejemplo)},
                                      {"largo", f.largo}});
        out.append(QVariantMap{{"id", QString::fromUtf8(c.id)},
                               {"titulo", QString::fromUtf8(c.titulo)},
                               {"ayuda", QString::fromUtf8(c.ayuda)},
                               {"campos", campos},
                               {"disponible", c.disponible},
                               {"porque", QString::fromUtf8(c.porque)}});
    }
    return out;
}

bool PantallasViewModel::buscando() const { return d_->buscando.load(); }
QString PantallasViewModel::aviso() const { return d_->aviso; }

int PantallasViewModel::mostrando() const {
    int n = 0;
    for (const Destino& d : d_->destinos)
        if (d.estado == "mostrando") ++n;
    return n;
}

// --- URLs ----------------------------------------------------------------------------

QString PantallasViewModel::urlDe(const QString& tipo, const QVariantMap& datos, const QString& idDestino) {
    const Contenido* c = contenidoPorId(tipo);
    if (c == nullptr || !c->disponible) return {};
    if (tipo == "url") return datos.value("url").toString().trimmed();

    // La base tiene que ser alcanzable DESDE el televisor: HTTP (el HTTPS es
    // autofirmado y un televisor no tiene dónde aceptarlo) y la IP de esta
    // máquina que esté en su misma red.
    QString ip;
    if (const Destino* d = idDestino.isEmpty() ? nullptr : d_->porId(idDestino); d != nullptr)
        ip = ipParaAlcanzar(d_->misIps, d->host);
    else if (!d_->misIps.isEmpty())
        ip = d_->misIps.first();
    if (ip.isEmpty()) return {};
    const QString base = "http://" + ip + ":" + QString::number(d_->puertoWeb);

    if (tipo == "prompter") return base + "/prompter/";
    QString url = base + "/pantalla/?tipo=" + tipo;
    for (const Campo& f : c->campos) {
        const QString v = datos.value(QString::fromUtf8(f.clave)).toString();
        if (v.isEmpty()) continue;
        url += "&" + QString::fromUtf8(f.clave) + "=" + QString::fromUtf8(QUrl::toPercentEncoding(v));
    }
    return url;
}

// --- Buscar --------------------------------------------------------------------------

void PantallasViewModel::buscar() {
    if (d_->buscando.exchange(true)) return; // ya hay una en curso
    emit cambio();
    if (d_->sala != nullptr) {
        d_->sala->client().get("/api/v1/network", [this](bool ok, const QJsonObject& r) {
            if (!ok) return;
            d_->misIps.clear();
            for (const QJsonValue& v : r.value("lan").toArray()) d_->misIps << v.toString();
        });
    }
    std::thread([this]() {
        std::string err;
        auto encontrados = pt::buscar(kBusquedaSeg, &err);
        QMetaObject::invokeMethod(this, [this, encontrados, err]() {
            d_->buscando = false;
            if (!err.empty()) {
                d_->aviso = QString::fromStdString(err);
            } else {
                d_->aviso.clear();
                for (const auto& e : encontrados) {
                    const QString id = QString::fromStdString(e.id);
                    Destino* d = d_->porId(id);
                    if (d == nullptr) {
                        d_->destinos.push_back(Destino{});
                        d = &d_->destinos.back();
                        d->id = id;
                    }
                    // El nombre y la dirección se refrescan siempre (el DHCP
                    // cambia las IP); lo que el operador puso a mano, no.
                    if (!d->fija) {
                        d->nombre = QString::fromStdString(e.nombre);
                        d->modelo = QString::fromStdString(e.modelo);
                    }
                    d->host = QString::fromStdString(e.host);
                    d->puerto = e.puerto;
                    d->vistaMs = QDateTime::currentMSecsSinceEpoch();
                }
                d_->guardar();
                emit notify(encontrados.empty()
                                ? "No encontré ningún televisor en la red"
                                : "Encontré " + QString::number(encontrados.size()) +
                                      (encontrados.size() == 1 ? " pantalla" : " pantallas"),
                            encontrados.empty() ? "warn" : "ok");
            }
            emit cambio();
        });
    }).detach();
}

QString PantallasViewModel::agregar(const QString& nombre, const QString& host, int puerto) {
    const QString h = host.trimmed();
    if (h.isEmpty()) return "Poné la dirección IP del televisor.";
    const QString n = nombre.trimmed().isEmpty() ? h : nombre.trimmed();
    const QString id = "manual:" + h;
    if (d_->porId(id) != nullptr) return "Ya está en la lista.";
    Destino d;
    d.id = id;
    d.nombre = n;
    d.host = h;
    d.puerto = puerto > 0 ? puerto : pt::kPuertoCast;
    d.fija = true;
    d_->destinos.push_back(std::move(d));
    d_->guardar();
    emit cambio();
    refrescar(id); // que diga enseguida si contesta o no
    return {};
}

QString PantallasViewModel::olvidar(const QString& id) {
    const auto it = std::find_if(d_->destinos.begin(), d_->destinos.end(),
                                 [&](const Destino& d) { return d.id == id; });
    if (it == d_->destinos.end()) return "No la encuentro.";
    if (it->trabajando) return "Esperá a que termine lo que está haciendo.";
    d_->destinos.erase(it);
    d_->guardar();
    emit cambio();
    return {};
}

// --- Hablarle a un aparato ------------------------------------------------------------

void PantallasViewModel::refrescar(const QString& id) {
    Destino* d = d_->porId(id);
    if (d == nullptr || d->trabajando) return;
    d->trabajando = true;
    emit cambio();
    const QString host = d->host;
    const int puerto = d->puerto;
    std::thread([this, id, host, puerto]() {
        const pt::Estado r = pt::consultar(host.toStdString(), puerto, kTimeoutMs);
        QMetaObject::invokeMethod(this, [this, id, r]() {
            Destino* d = d_->porId(id);
            if (d == nullptr) return;
            d->trabajando = false;
            d->vistaMs = QDateTime::currentMSecsSinceEpoch();
            if (!r.ok) {
                d->estado = "apagada";
                d->error = QString::fromStdString(r.error);
                d->contenido.clear();
                d->que.clear();
            } else {
                d->error.clear();
                d->volumen = r.volumen;
                d->silenciado = r.silenciado;
                if (r.appId == pt::kAppDashCast) {
                    d->estado = "mostrando";
                } else if (r.appId.empty()) {
                    d->estado = "libre";
                    d->contenido.clear();
                    d->que.clear();
                } else {
                    // Hay otra cosa corriendo (Netflix, YouTube): se avisa en
                    // vez de pisarla sin preguntar.
                    d->estado = "ocupada";
                    d->contenido.clear();
                    d->que = QString::fromStdString(r.appNombre.empty() ? r.appId : r.appNombre);
                }
            }
            emit cambio();
        });
    }).detach();
}

QString PantallasViewModel::mostrar(const QString& id, const QString& tipo, const QVariantMap& datos) {
    Destino* d = d_->porId(id);
    if (d == nullptr) return "No encuentro esa pantalla.";
    if (d->trabajando) return "Esperá: está ocupada con lo anterior.";
    const Contenido* c = contenidoPorId(tipo);
    if (c == nullptr) return "No sé mostrar «" + tipo + "».";
    if (!c->disponible) return QString::fromUtf8(c->porque);
    const QString url = urlDe(tipo, datos, id);
    if (url.isEmpty())
        return tipo == "url" ? "Poné una dirección."
                             : "Todavía no sé con qué dirección me ve el televisor: probá de nuevo en unos "
                               "segundos, o fijate que la sala esté corriendo.";

    // Lo legible, para la lista: qué le mandamos, no la URL entera.
    QString que = QString::fromUtf8(c->titulo);
    for (const Campo& f : c->campos) {
        const QString v = datos.value(QString::fromUtf8(f.clave)).toString().trimmed();
        if (!v.isEmpty()) {
            que += " — " + (v.size() > 60 ? v.left(60) + "…" : v);
            break;
        }
    }

    d->trabajando = true;
    d->error.clear();
    emit cambio();
    const QString host = d->host;
    const int puerto = d->puerto;
    std::thread([this, id, host, puerto, url, tipo, que]() {
        // `reload` en 0: la página no se recarga sola. Un QR o un mensaje no
        // cambian, y una recarga en medio de un evento se ve.
        const pt::Estado r = pt::mostrarUrl(host.toStdString(), puerto, url.toStdString(), 0, kTimeoutMs);
        QMetaObject::invokeMethod(this, [this, id, r, tipo, que]() {
            Destino* d = d_->porId(id);
            if (d == nullptr) return;
            d->trabajando = false;
            d->vistaMs = QDateTime::currentMSecsSinceEpoch();
            if (r.ok) {
                d->estado = "mostrando";
                d->contenido = tipo;
                d->que = que;
                d->error.clear();
            } else {
                d->estado = "error";
                d->error = QString::fromStdString(r.error);
                emit notify(d->nombre + ": " + d->error, "error");
            }
            emit cambio();
        });
    }).detach();
    return {};
}

void PantallasViewModel::parar(const QString& id) {
    Destino* d = d_->porId(id);
    if (d == nullptr || d->trabajando) return;
    d->trabajando = true;
    emit cambio();
    const QString host = d->host;
    const int puerto = d->puerto;
    std::thread([this, id, host, puerto]() {
        const pt::Estado r = pt::parar(host.toStdString(), puerto, kTimeoutMs);
        QMetaObject::invokeMethod(this, [this, id, r]() {
            Destino* d = d_->porId(id);
            if (d == nullptr) return;
            d->trabajando = false;
            d->vistaMs = QDateTime::currentMSecsSinceEpoch();
            d->contenido.clear();
            d->que.clear();
            if (r.ok) {
                d->estado = "libre";
                d->error.clear();
            } else {
                d->estado = "error";
                d->error = QString::fromStdString(r.error);
            }
            emit cambio();
        });
    }).detach();
}

void PantallasViewModel::setVolumen(const QString& id, double nivel) {
    Destino* d = d_->porId(id);
    if (d == nullptr || d->trabajando) return;
    d->trabajando = true;
    const QString host = d->host;
    const int puerto = d->puerto;
    std::thread([this, id, host, puerto, nivel]() {
        const pt::Estado r = pt::volumen(host.toStdString(), puerto, nivel, -1, kTimeoutMs);
        QMetaObject::invokeMethod(this, [this, id, r]() {
            Destino* d = d_->porId(id);
            if (d == nullptr) return;
            d->trabajando = false;
            if (r.ok) {
                d->volumen = r.volumen;
                d->silenciado = r.silenciado;
            }
            emit cambio();
        });
    }).detach();
}

void PantallasViewModel::silenciar(const QString& id, bool si) {
    Destino* d = d_->porId(id);
    if (d == nullptr || d->trabajando) return;
    d->trabajando = true;
    const QString host = d->host;
    const int puerto = d->puerto;
    std::thread([this, id, host, puerto, si]() {
        const pt::Estado r = pt::volumen(host.toStdString(), puerto, -1, si ? 1 : 0, kTimeoutMs);
        QMetaObject::invokeMethod(this, [this, id, r]() {
            Destino* d = d_->porId(id);
            if (d == nullptr) return;
            d->trabajando = false;
            if (r.ok) {
                d->volumen = r.volumen;
                d->silenciado = r.silenciado;
            }
            emit cambio();
        });
    }).detach();
}

} // namespace ba::app
