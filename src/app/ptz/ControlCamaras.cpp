// src/app/ptz/ControlCamaras.cpp — ver ControlCamaras.hpp

#include "ptz/ControlCamaras.hpp"

#include "AppRuntime.hpp"
#include "BroadcastEngine.hpp"
#include "InputSpecs.hpp"
#include "net/Credentials.hpp"
#include "ptz/OnvifBusqueda.hpp"
#include "ptz/PtzBackend.hpp"
#include "ptz/PtzDigital.hpp"
#include "ptz/PtzOnvif.hpp"
#include "ptz/PtzPanasonic.hpp"
#include "ptz/PtzVisca.hpp"

#include <obs.h>

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <nlohmann/json.hpp>

namespace ba::app::ptz {

namespace {

/// Nombre del filtro de Mejorar imagen en cada fuente.
constexpr const char* kFiltroMejora = "BA · Mejorar imagen";

QString qs(const std::string& s) { return QString::fromStdString(s); }

} // namespace

struct ControlCamaras::Camara {
    std::string entrada;
    QString backend = QStringLiteral("ninguno"); ///< ninguno | visca | onvif | panasonic
    QJsonObject conf;                            ///< sin la clave
    bool digital = true;
    QString mejora = QStringLiteral("general"); ///< general | si | no
    QString modo = QStringLiteral("optico");    ///< con hardware: qué mueve el joystick
    QJsonObject posicion{{"x", 0.5}, {"y", 0.5}, {"zoom", 1.0}}; ///< la digital, para volver a ella
    std::unique_ptr<PtzDigital> dig;
    std::unique_ptr<PtzBackend> hw;
    bool filtroPuesto = false; ///< el PTZ digital ya está en la fuente (y repuesto)
};

ControlCamaras::ControlCamaras(AppRuntime& rt, std::filesystem::path dataRoot, QObject* parent)
    : QObject(parent), rt_(rt), archivo_(dataRoot / "Config" / "camaras.json"),
      credenciales_(QString::fromStdString((dataRoot / "Credenciales").string())),
      busqueda_(std::make_unique<OnvifBusqueda>()) {
    connect(busqueda_.get(), &OnvifBusqueda::cambio, this, &ControlCamaras::busquedaCambio);
    leer();
    revision_.setInterval(1500);
    connect(&revision_, &QTimer::timeout, this, &ControlCamaras::sincronizar);
    revision_.start();
    guardado_.setSingleShot(true);
    guardado_.setInterval(1500);
    connect(&guardado_, &QTimer::timeout, this, &ControlCamaras::guardar);
    QTimer::singleShot(0, this, &ControlCamaras::sincronizar);
}

ControlCamaras::~ControlCamaras() {
    if (guardado_.isActive()) guardar();
}

// ---------------------------------------------------------------------------
// Lo que se muestra
// ---------------------------------------------------------------------------

QVariantList ControlCamaras::backends() {
    const QVariantMap host{{"clave", "host"}, {"etiqueta", "Dirección (IP o nombre)"}, {"tipo", "text"}};
    const QVariantMap puerto{{"clave", "puerto"}, {"etiqueta", "Puerto (vacío = el de siempre)"}, {"tipo", "int"}};
    const QVariantMap usuario{{"clave", "usuario"}, {"etiqueta", "Usuario"}, {"tipo", "text"}};
    const QVariantMap clave{{"clave", "clave"}, {"etiqueta", "Clave"}, {"tipo", "password"}};
    return {
        QVariantMap{{"id", "ninguno"}, {"nombre", "Solo digital"}, {"ayuda", "La cámara no se mueve: recorte y zoom en la GPU."},
                    {"campos", QVariantList{}}},
        QVariantMap{{"id", "visca"},
                    {"nombre", "VISCA por IP"},
                    {"ayuda", "Sony, PTZOptics, BirdDog, Avonic, Marshall, AVer, Lumens y la mayoría de las chinas. "
                              "Sony usa UDP 52381; PTZOptics y similares, TCP 5678 o UDP 1259."},
                    {"campos", QVariantList{host,
                                            QVariantMap{{"clave", "transporte"},
                                                        {"etiqueta", "Transporte"},
                                                        {"tipo", "choice"},
                                                        {"opciones", QVariantList{
                                                                         QVariantMap{{"valor", "sony"}, {"texto", "UDP con encabezado Sony (52381)"}},
                                                                         QVariantMap{{"valor", "tcp"}, {"texto", "TCP crudo (5678)"}},
                                                                         QVariantMap{{"valor", "udp"}, {"texto", "UDP crudo (1259)"}}}}},
                                            puerto,
                                            QVariantMap{{"clave", "direccion"}, {"etiqueta", "Dirección VISCA (1-7)"}, {"tipo", "int"}}}}},
        QVariantMap{{"id", "onvif"},
                    {"nombre", "ONVIF"},
                    {"ayuda", "Cámaras IP en general (Profile S). «Buscar en la red» las encuentra solas."},
                    {"campos", QVariantList{host, puerto, usuario, clave,
                                            QVariantMap{{"clave", "perfil"}, {"etiqueta", "Perfil (vacío = el que tenga PTZ)"}, {"tipo", "text"}}}}},
        QVariantMap{{"id", "panasonic"},
                    {"nombre", "Panasonic AW/UE"},
                    {"ayuda", "AW-HE40/42/50/60/70/130, AW-UE70/100/150/160 (control por HTTP)."},
                    {"campos", QVariantList{host, puerto, usuario, clave}}},
    };
}

bool ControlCamaras::buscando() const { return busqueda_->buscando(); }
QVariantList ControlCamaras::encontradas() const { return busqueda_->encontradas(); }
bool ControlCamaras::mejoraGeneral() const { return rt_.config().mejora.activa; }

QVariantMap ControlCamaras::fila(const Camara& c) const {
    const QString e = qs(c.entrada);
    const bool general = rt_.config().mejora.activa;
    QJsonObject conf = c.conf;
    conf.remove("clave");
    QVariantList presetsDig;
    if (c.dig)
        for (const QString& k : c.dig->presets().keys()) presetsDig << k.toInt();
    return {{"entrada", e},
            {"tipoEntrada", tipoEntrada_.count(c.entrada) ? tipoEntrada_.at(c.entrada) : QString()},
            {"backend", c.backend},
            {"conf", conf.toVariantMap()},
            {"tieneClave", cred::exists(credenciales_, nombreClave(c.entrada).c_str())},
            {"digital", c.digital},
            {"mejora", c.mejora},
            {"mejoraActiva", c.mejora == "si" || (c.mejora == "general" && general)},
            {"modo", c.hw ? c.modo : QStringLiteral("digital")},
            {"capacidades", c.hw ? c.hw->capacidades().mapa() : QVariantMap{}},
            {"capacidadesDigital", c.digital && c.dig ? c.dig->capacidades().mapa() : QVariantMap{}},
            {"listo", c.hw ? c.hw->listo() : c.digital},
            {"estado", c.hw ? c.hw->estado() : (c.digital ? QStringLiteral("Digital (recorte en la GPU)")
                                                           : QStringLiteral("PTZ apagado"))},
            {"estadoDigital", c.dig ? c.dig->estado() : QString()},
            {"presetsDigitales", presetsDig}};
}

QVariantList ControlCamaras::camaras() const {
    QVariantList out;
    for (const auto& [nombre, tipo] : tipoEntrada_) {
        Q_UNUSED(tipo);
        auto it = camaras_.find(nombre);
        if (it != camaras_.end()) out << fila(*it->second);
    }
    return out;
}

QJsonObject ControlCamaras::estadoJson(const QString& entrada) const {
    QJsonArray lista;
    for (const QVariant& v : camaras()) {
        QVariantMap m = v.toMap();
        if (!entrada.isEmpty() && m["entrada"].toString() != entrada) continue;
        const QVariantMap pos = posicion(m["entrada"].toString());
        if (!pos.isEmpty()) m["posicionDigital"] = pos;
        lista << QJsonObject::fromVariantMap(m);
    }
    return QJsonObject{{"camaras", lista}, {"mejoraGeneral", mejoraGeneral()}};
}

// ---------------------------------------------------------------------------
// Entradas y filtros
// ---------------------------------------------------------------------------

void ControlCamaras::sincronizar() {
    BroadcastEngine& e = rt_.engine();
    std::map<std::string, QString> presentes;
    for (const std::string& n : e.sourceNames()) {
        const std::string type = e.sourceTypeId(n);
        const auto s = nlohmann::json::parse(e.sourceSettingsJson(n), nullptr, false);
        const bool hw = type == inputs::kVideoType;
        const bool ndi = type == "ba_ndi_input";
        if (!hw && !inputs::isNetworkInput(type, s)) continue;
        presentes[n] = hw ? QStringLiteral("hardware") : ndi ? QStringLiteral("ndi") : QStringLiteral("red");
    }
    bool cambiaron = presentes != tipoEntrada_;
    tipoEntrada_ = presentes;
    const bool general = rt_.config().mejora.activa;
    for (const auto& [nombre, tipo] : presentes) {
        Q_UNUSED(tipo);
        auto& c = camaras_[nombre];
        if (!c) {
            c = std::make_unique<Camara>();
            c->entrada = nombre;
            cambiaron = true;
        }
        if (!c->dig) {
            c->dig = std::make_unique<PtzDigital>(nombre);
            connect(c->dig.get(), &PtzBackend::estadoCambio, this, [this]() {
                guardarPronto();
                emit cambio();
            });
        }
        aplicarFiltros(*c, general);
    }
    if (cambiaron) emit cambio();
}

void ControlCamaras::aplicarFiltros(Camara& c, bool mejoraGeneral) {
    obs_source_t* src = obs_get_source_by_name(c.entrada.c_str());
    if (src == nullptr) return;
    const AppConfig& cfg = rt_.config();

    // 1) Mejorar imagen: lo más cerca de la fuente (se limpia antes de ampliar).
    const bool quiereMejora = c.mejora == "si" || (c.mejora == "general" && mejoraGeneral);
    obs_source_t* mej = obs_source_get_filter_by_name(src, kFiltroMejora);
    obs_data_t* ajustes = obs_data_create();
    obs_data_set_int(ajustes, "ruido", cfg.mejora.ruido);
    obs_data_set_int(ajustes, "color", cfg.mejora.color);
    obs_data_set_int(ajustes, "nitidez", cfg.mejora.nitidez);
    if (quiereMejora && mej == nullptr) {
        obs_source_t* f = obs_source_create_private("ba_mejora_imagen", kFiltroMejora, ajustes);
        if (f != nullptr) {
            obs_source_filter_add(src, f);
            // «Arriba» en la lista de libobs (el último del arreglo, cuyo
            // destino es la fuente) = el primero en aplicarse: se limpia antes
            // de que el PTZ digital amplíe. Un filtro nuevo entra «abajo»
            // (índice 0, el último en aplicarse).
            obs_source_filter_set_order(src, f, OBS_ORDER_MOVE_TOP);
            obs_source_release(f);
        }
    } else if (quiereMejora && mej != nullptr) {
        obs_data_t* actual = obs_source_get_settings(mej);
        if (obs_data_get_int(actual, "ruido") != cfg.mejora.ruido || obs_data_get_int(actual, "color") != cfg.mejora.color ||
            obs_data_get_int(actual, "nitidez") != cfg.mejora.nitidez)
            obs_source_update(mej, ajustes);
        obs_data_release(actual);
    } else if (!quiereMejora && mej != nullptr) {
        obs_source_filter_remove(src, mej);
    }
    obs_data_release(ajustes);
    if (mej != nullptr) obs_source_release(mej);

    // 2) PTZ digital.
    obs_source_t* ptz = obs_source_get_filter_by_name(src, kFiltroPtz);
    if (c.digital && ptz == nullptr) {
        obs_source_t* f = obs_source_create_private("ba_ptz_digital", kFiltroPtz, nullptr);
        if (f != nullptr) {
            obs_source_filter_add(src, f);
            obs_source_release(f);
            c.filtroPuesto = false;
        }
    } else if (!c.digital && ptz != nullptr) {
        obs_source_filter_remove(src, ptz);
        c.filtroPuesto = false;
    }
    if (ptz != nullptr) obs_source_release(ptz);
    obs_source_release(src);
    // Recién puesto: vuelve al encuadre que tenía.
    if (c.digital && !c.filtroPuesto && c.dig) {
        c.filtroPuesto = c.dig->ir(c.posicion.value("x").toDouble(0.5), c.posicion.value("y").toDouble(0.5),
                                   c.posicion.value("zoom").toDouble(1.0), 0.0);
    }
}

// ---------------------------------------------------------------------------
// Órdenes
// ---------------------------------------------------------------------------

ControlCamaras::Camara* ControlCamaras::camara(const QString& entrada, QString* error) {
    auto it = camaras_.find(entrada.toStdString());
    if (it == camaras_.end() || !tipoEntrada_.count(it->first)) {
        if (error) *error = QStringLiteral("No hay una entrada de video «%1»").arg(entrada);
        return nullptr;
    }
    return it->second.get();
}

PtzBackend* ControlCamaras::activo(Camara& c) const {
    if (c.hw && (c.modo == "optico" || !c.digital)) return c.hw.get();
    if (c.digital) return c.dig.get();
    return nullptr;
}

#define BA_PTZ_ACTIVO(entrada)                                                                                   \
    QString error;                                                                                               \
    Camara* c = camara(entrada, &error);                                                                         \
    if (c == nullptr) return error;                                                                              \
    PtzBackend* b = activo(*c);                                                                                  \
    if (b == nullptr) return QStringLiteral("«%1» no tiene PTZ: prendé el digital o elegí un backend").arg(entrada);

QString ControlCamaras::mover(const QString& entrada, double pan, double tilt, double zoom) {
    BA_PTZ_ACTIVO(entrada);
    b->mover(qBound(-1.0, pan, 1.0), qBound(-1.0, tilt, 1.0), qBound(-1.0, zoom, 1.0));
    return {};
}

QString ControlCamaras::parar(const QString& entrada) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    // Para todo lo que pueda estar moviéndose (por si cambió el modo en medio).
    if (c->hw) c->hw->parar();
    if (c->dig) c->dig->parar();
    guardarPronto();
    return {};
}

QString ControlCamaras::enfocar(const QString& entrada, double v) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    if (!c->hw || !c->hw->capacidades().foco) return QStringLiteral("«%1» no tiene foco controlable").arg(entrada);
    c->hw->enfocar(qBound(-1.0, v, 1.0));
    return {};
}

QString ControlCamaras::autoFoco(const QString& entrada, bool automatico) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    if (!c->hw || !c->hw->capacidades().autoFoco) return QStringLiteral("«%1» no deja cambiar el autofoco").arg(entrada);
    c->hw->autoFoco(automatico);
    return {};
}

QString ControlCamaras::irPreset(const QString& entrada, int n) {
    BA_PTZ_ACTIVO(entrada);
    if (n < 1 || n > b->capacidades().presets) return QStringLiteral("Preset %1 fuera de rango (1-%2)").arg(n).arg(b->capacidades().presets);
    b->irAPreset(n);
    emit cambio();
    return {};
}

QString ControlCamaras::guardarPreset(const QString& entrada, int n) {
    BA_PTZ_ACTIVO(entrada);
    if (n < 1 || n > b->capacidades().presets) return QStringLiteral("Preset %1 fuera de rango (1-%2)").arg(n).arg(b->capacidades().presets);
    b->guardarPreset(n);
    guardarPronto();
    emit cambio();
    return {};
}

QString ControlCamaras::home(const QString& entrada) {
    BA_PTZ_ACTIVO(entrada);
    b->home();
    guardarPronto();
    return {};
}

#undef BA_PTZ_ACTIVO

QString ControlCamaras::encuadre(const QString& entrada, double x, double y, double zoom, double segundos) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    if (!c->digital || !c->dig) return QStringLiteral("El PTZ digital está apagado en «%1»").arg(entrada);
    if (!c->dig->ir(qBound(0.0, x, 1.0), qBound(0.0, y, 1.0), qBound(1.0, zoom, 8.0), qBound(0.0, segundos, 10.0)))
        return c->dig->estado();
    guardarPronto();
    return {};
}

QVariantMap ControlCamaras::posicion(const QString& entrada) const {
    auto it = camaras_.find(entrada.toStdString());
    if (it == camaras_.end() || !it->second->digital || !it->second->dig) return {};
    return it->second->dig->posicion();
}

// ---------------------------------------------------------------------------
// Configuración
// ---------------------------------------------------------------------------

std::string ControlCamaras::nombreClave(const std::string& entrada) const {
    // El nombre de la entrada puede tener cualquier cosa: al archivo va su hash.
    return "ptz-" + QCryptographicHash::hash(QByteArray::fromStdString(entrada), QCryptographicHash::Sha1)
                        .toHex()
                        .left(16)
                        .toStdString();
}

void ControlCamaras::crearHardware(Camara& c) {
    c.hw.reset();
    if (c.backend == "visca") c.hw = std::make_unique<PtzVisca>();
    else if (c.backend == "onvif") c.hw = std::make_unique<PtzOnvif>();
    else if (c.backend == "panasonic") c.hw = std::make_unique<PtzPanasonic>();
    if (!c.hw) return;
    connect(c.hw.get(), &PtzBackend::estadoCambio, this, &ControlCamaras::cambio);
    QJsonObject conf = c.conf;
    QByteArray clave;
    if (cred::read(credenciales_, nombreClave(c.entrada).c_str(), clave)) conf["clave"] = QString::fromUtf8(clave);
    c.hw->configurar(conf);
}

QString ControlCamaras::configurar(const QString& entrada, const QString& backend, const QVariantMap& conf) {
    static const QStringList kValidos{"ninguno", "visca", "onvif", "panasonic"};
    if (!kValidos.contains(backend))
        return QStringLiteral("Backend «%1» desconocido (ninguno, visca, onvif, panasonic)").arg(backend);
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    if (backend != "ninguno" && conf.value("host").toString().trimmed().isEmpty())
        return QStringLiteral("Falta la dirección de la cámara");
    QJsonObject j = QJsonObject::fromVariantMap(conf);
    const QString clave = j.take("clave").toString();
    if (!clave.isEmpty()) cred::write(credenciales_, nombreClave(c->entrada).c_str(), clave.toUtf8());
    if (backend == "ninguno") cred::remove(credenciales_, nombreClave(c->entrada).c_str());
    // Los números vienen como texto desde un formulario.
    for (const char* k : {"puerto", "direccion"})
        if (j.value(k).isString()) j[k] = j.value(k).toString().toInt();
    c->backend = backend;
    c->conf = backend == "ninguno" ? QJsonObject{} : j;
    if (c->backend != "ninguno") c->modo = QStringLiteral("optico");
    crearHardware(*c);
    guardarPronto();
    emit cambio();
    return {};
}

QString ControlCamaras::setDigital(const QString& entrada, bool prendido) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    c->digital = prendido;
    if (!prendido) c->modo = QStringLiteral("optico");
    aplicarFiltros(*c, rt_.config().mejora.activa);
    guardarPronto();
    emit cambio();
    return {};
}

QString ControlCamaras::setMejora(const QString& entrada, const QString& modo) {
    if (modo != "general" && modo != "si" && modo != "no") return QStringLiteral("Modo «%1»: general, si o no").arg(modo);
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    c->mejora = modo;
    aplicarFiltros(*c, rt_.config().mejora.activa);
    guardarPronto();
    emit cambio();
    return {};
}

QString ControlCamaras::setModo(const QString& entrada, const QString& modo) {
    if (modo != "optico" && modo != "digital") return QStringLiteral("Modo «%1»: optico o digital").arg(modo);
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    if (modo == "optico" && !c->hw) return QStringLiteral("«%1» no tiene backend de hardware").arg(entrada);
    if (modo == "digital" && !c->digital) return QStringLiteral("El PTZ digital está apagado en «%1»").arg(entrada);
    // Lo que se estaba moviendo, se para.
    if (c->hw) c->hw->parar();
    if (c->dig) c->dig->parar();
    c->modo = modo;
    guardarPronto();
    emit cambio();
    return {};
}

void ControlCamaras::buscarOnvif() {
    busqueda_->buscar(3000);
    emit busquedaCambio();
}

// ---------------------------------------------------------------------------
// Disco
// ---------------------------------------------------------------------------

void ControlCamaras::leer() {
    QFile f(QString::fromStdString(archivo_.string()));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject raiz = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject todas = raiz.value("camaras").toObject();
    for (auto it = todas.begin(); it != todas.end(); ++it) {
        const QJsonObject j = it.value().toObject();
        auto c = std::make_unique<Camara>();
        c->entrada = it.key().toStdString();
        c->backend = j.value("backend").toString("ninguno");
        c->conf = j.value("conf").toObject();
        c->conf.remove("clave");
        c->digital = j.value("digital").toBool(true);
        c->mejora = j.value("mejora").toString("general");
        c->modo = j.value("modo").toString("optico");
        if (j.contains("posicion")) c->posicion = j.value("posicion").toObject();
        c->dig = std::make_unique<PtzDigital>(c->entrada);
        c->dig->setPresets(j.value("presets").toObject());
        connect(c->dig.get(), &PtzBackend::estadoCambio, this, [this]() {
            guardarPronto();
            emit cambio();
        });
        crearHardware(*c);
        camaras_[c->entrada] = std::move(c);
    }
}

void ControlCamaras::guardarPronto() { guardado_.start(); }

void ControlCamaras::guardar() {
    QJsonObject todas;
    for (auto& [nombre, c] : camaras_) {
        // La posición digital actual (para volver a ella al reabrir).
        if (c->dig && tipoEntrada_.count(nombre) && obs_initialized()) {
            const QVariantMap p = c->dig->posicion();
            if (!p.isEmpty())
                c->posicion = QJsonObject{{"x", p["x"].toDouble()}, {"y", p["y"].toDouble()}, {"zoom", p["zoom"].toDouble()}};
        }
        QJsonObject conf = c->conf;
        conf.remove("clave");
        todas[qs(nombre)] = QJsonObject{{"backend", c->backend},
                                        {"conf", conf},
                                        {"digital", c->digital},
                                        {"mejora", c->mejora},
                                        {"modo", c->modo},
                                        {"posicion", c->posicion},
                                        {"presets", c->dig ? c->dig->presets() : QJsonObject{}}};
    }
    std::error_code ec;
    std::filesystem::create_directories(archivo_.parent_path(), ec);
    QSaveFile f(QString::fromStdString(archivo_.string()));
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(QJsonObject{{"camaras", todas}}).toJson(QJsonDocument::Indented));
    f.commit();
}

} // namespace ba::app::ptz
