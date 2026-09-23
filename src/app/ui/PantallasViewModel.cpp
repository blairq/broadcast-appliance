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
        // El programa entra por el mismo camino que un invitado, así que el
        // enlace lo tiene que acuñar el gateway (lleva un permiso de ingreso):
        // por eso es el único contenido cuya URL no se arma acá.
        {"programa", "Lo que sale al aire",
         "El programa en vivo por WebRTC: unas décimas de retardo, no los segundos del reproductor de "
         "fábrica del televisor. La pantalla entra a la sala como una más: si el lobby está prendido, "
         "hay que hacerla pasar, y ocupa un lugar mientras dure.",
         {}, true, ""},
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
    QVariantMap ultimosDatos; ///< con qué se lo mandamos (para poder fijarlo)
    QString que;          ///< legible: "QR — https://…"
    QString error;
    double volumen = -1;
    bool silenciado = false;
    bool trabajando = false;
    std::int64_t vistaMs = 0;
    // --- Lo que tiene que mostrar SIEMPRE (se guarda) ---
    QString fijadoTipo;
    QVariantMap fijadoDatos;
    // Reintentos del restaurado: un televisor apagado no se martilla.
    int fallos = 0;
    std::int64_t proximoIntentoMs = 0;
    /// -1 no se sabe, 0 no sabe mostrar páginas, 1 sí.
    int puedeMostrar = -1;
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
    /// Cada tanto se revisa si cada pantalla está mostrando lo que le fijaron.
    QTimer mantenimiento;

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
            json x{{"id", d.id.toStdString()},
                   {"nombre", d.nombre.toStdString()},
                   {"modelo", d.modelo.toStdString()},
                   {"host", d.host.toStdString()},
                   {"puerto", d.puerto},
                   {"fija", d.fija}};
            if (!d.fijadoTipo.isEmpty()) {
                x["fijadoTipo"] = d.fijadoTipo.toStdString();
                json datos = json::object();
                for (auto it = d.fijadoDatos.begin(); it != d.fijadoDatos.end(); ++it)
                    datos[it.key().toStdString()] = it.value().toString().toStdString();
                x["fijadoDatos"] = datos;
            }
            o.push_back(std::move(x));
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
            d.fijadoTipo = QString::fromStdString(x.value("fijadoTipo", ""));
            // El json de `value()` es un TEMPORAL: si se itera directo, el
            // proxy de items() sobrevive al objeto y se lee memoria liberada.
            const json fijadoDatos = x.value("fijadoDatos", json::object());
            for (const auto& [k, v] : fijadoDatos.items())
                if (v.is_string()) d.fijadoDatos[QString::fromStdString(k)] = QString::fromStdString(v);
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
    // Lo que cada pantalla tiene que estar mostrando se revisa cada tanto: un
    // televisor que se reinició de madrugada vuelve solo a lo suyo.
    d_->mantenimiento.setInterval(15000);
    connect(&d_->mantenimiento, &QTimer::timeout, this, [this]() { restaurar(); });
    d_->mantenimiento.start();
    // Al arrancar se busca una vez: sin eso, una pantalla con IP nueva no se
    // encontraría hasta que alguien apretara el botón.
    QTimer::singleShot(4000, this, [this]() { buscar(); });

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
                               {"puedeMostrar", d.puedeMostrar},
                               {"fijadoTipo", d.fijadoTipo},
                               {"fijadoDatos", d.fijadoDatos},
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
    // El programa lleva un permiso de ingreso que acuña el gateway: la URL no
    // se puede armar acá. `mostrar` la pide y recién después manda.
    if (tipo == "programa") return "(la pide el gateway al mandar)";

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
                // Se le pregunta a cada una cómo está, escalonado: una lista
                // que dice «sin consultar» no sirve de nada, y abrir diez
                // canales TLS a la vez tampoco.
                int n = 0;
                for (const Destino& d : d_->destinos) {
                    const QString id = d.id;
                    QTimer::singleShot(300 * n++, this, [this, id]() { refrescar(id); });
                }
                // Un aparato que vuelve a aparecer (prendido de nuevo, o con
                // otra IP) retoma lo suyo enseguida, sin esperar la ronda. Va
                // después de las consultas: así ya se sabe si puede mostrar.
                for (Destino& d : d_->destinos) d.proximoIntentoMs = 0;
                QTimer::singleShot(300 * n + 1500, this, [this]() { restaurar(); });
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

QString PantallasViewModel::renombrar(const QString& id, const QString& nombre) {
    Destino* d = d_->porId(id);
    if (d == nullptr) return "No la encuentro.";
    const QString n = nombre.trimmed();
    if (n.isEmpty()) return "Ponele un nombre.";
    d->nombre = n;
    // Marcarla como puesta a mano es lo que hace que la próxima búsqueda no le
    // pise el nombre con el que anuncia el aparato.
    d->fija = true;
    d_->guardar();
    emit cambio();
    return {};
}

QString PantallasViewModel::fijar(const QString& id, const QString& tipo, const QVariantMap& datos) {
    Destino* d = d_->porId(id);
    if (d == nullptr) return "No la encuentro.";
    const Contenido* c = contenidoPorId(tipo);
    if (c == nullptr) return "No sé mostrar «" + tipo + "».";
    if (!c->disponible) return QString::fromUtf8(c->porque);
    d->fijadoTipo = tipo;
    d->fijadoDatos = datos;
    d->fallos = 0;
    d->proximoIntentoMs = 0;
    d_->guardar();
    emit cambio();
    // Y se lo manda ya: fijar algo sin verlo aparecer no da confianza.
    return mostrar(id, tipo, datos);
}

QString PantallasViewModel::fijarLoQueMuestra(const QString& id) {
    Destino* d = d_->porId(id);
    if (d == nullptr) return "No la encuentro.";
    if (d->contenido.isEmpty())
        return "Esta pantalla no está mostrando nada nuestro: mandale algo primero.";
    d->fijadoTipo = d->contenido;
    d->fijadoDatos = d->ultimosDatos;
    d->fallos = 0;
    d_->guardar();
    emit cambio();
    return {};
}

QString PantallasViewModel::desfijar(const QString& id) {
    Destino* d = d_->porId(id);
    if (d == nullptr) return "No la encuentro.";
    d->fijadoTipo.clear();
    d->fijadoDatos.clear();
    d_->guardar();
    emit cambio();
    return {};
}

// --- Restaurar lo fijado --------------------------------------------------------------

void PantallasViewModel::restaurar() {
    const std::int64_t ahora = QDateTime::currentMSecsSinceEpoch();
    for (Destino& d : d_->destinos) {
        if (d.fijadoTipo.isEmpty() || d.trabajando) continue;
        // Ya está mostrando lo que tiene que mostrar: nada que hacer.
        if (d.estado == "mostrando" && d.contenido == d.fijadoTipo) continue;
        // Ocupada con otra cosa (alguien puso Netflix): NO se le pisa sola.
        // Que lo decida una persona; para eso está el botón.
        if (d.estado == "ocupada") continue;
        if (ahora < d.proximoIntentoMs) continue;
        const QString tipo = d.fijadoTipo;
        const QVariantMap datos = d.fijadoDatos;
        const QString id = d.id;
        // La espera crece con los fallos: un televisor apagado se reintenta
        // cada vez menos, hasta una vez cada cinco minutos.
        d.proximoIntentoMs = ahora + std::min<std::int64_t>(15000LL * (1 + d.fallos), 300000LL);
        mostrar(id, tipo, datos);
    }
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
                d->puedeMostrar = r.puedeMostrar;
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
    // Si ya se preguntó y el aparato no sabe abrir páginas, se dice ANTES de
    // abrirle un canal y pisarle lo que esté pasando. (Un parlante sin
    // pantalla contesta el protocolo igual, pero no tiene dónde mostrar.)
    if (d->puedeMostrar == 0)
        return d->nombre + " no sabe mostrar páginas web: contesta el protocolo, pero no tiene dónde "
                           "mostrarlas (¿es un parlante?).";
    if (tipo == "programa") {
        if (d_->sala == nullptr) return "La sala no está corriendo: sin ella no hay programa que mandar.";
        d->trabajando = true;
        emit cambio();
        const QString nombre = d->nombre;
        d_->sala->client().get(
            "/api/v1/cast-link?target=local&nombre=" + QString::fromUtf8(QUrl::toPercentEncoding(nombre)),
            [this, id](bool ok, const QJsonObject& r) {
                Destino* d = d_->porId(id);
                if (d == nullptr) return;
                d->trabajando = false;
                if (!ok || !r.value("ok").toBool()) {
                    d->estado = "error";
                    const QString m = r.value("message").toString();
                    d->error = m.isEmpty() ? "No pude sacar el enlace del programa" : m;
                    emit notify(d->nombre + ": " + d->error, "error");
                    emit cambio();
                    return;
                }
                mandarUrl(id, r.value("url").toString(), "programa", "Lo que sale al aire");
            });
        return {};
    }
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

    d->ultimosDatos = datos;
    mandarUrl(id, url, tipo, que);
    return {};
}

/// Lo que hacen todos los contenidos al final: abrir el canal y mandar la URL.
void PantallasViewModel::mandarUrl(const QString& id, const QString& url, const QString& tipo,
                                   const QString& que) {
    Destino* d = d_->porId(id);
    if (d == nullptr) return;
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
                d->fallos = 0;
            } else {
                d->estado = "error";
                d->error = QString::fromStdString(r.error);
                ++d->fallos;
                // Un aviso por cada reintento del restaurado seria una lluvia
                // de avisos por un televisor apagado: solo el primero.
                if (d->fijadoTipo.isEmpty() || d->fallos == 1)
                    emit notify(d->nombre + ": " + d->error, "error");
            }
            emit cambio();
        });
    }).detach();
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
