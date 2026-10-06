// src/app/ptz/ControlCamaras.cpp — ver ControlCamaras.hpp

#include "ptz/ControlCamaras.hpp"

#include "AppRuntime.hpp"
#include "BroadcastEngine.hpp"
#include "InputSpecs.hpp"
#include "net/Credentials.hpp"
#include "ptz/OnvifBusqueda.hpp"
#include "ptz/Posiciones.hpp"
#include "ptz/PtzBackend.hpp"
#include "ptz/PtzDigital.hpp"
#include "ptz/PtzOnvif.hpp"
#include "ptz/PtzPanasonic.hpp"
#include "ptz/PtzPlugin.hpp"
#include "ptz/PtzVisca.hpp"

#include "FuentesApp.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QUrl>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <cmath>

namespace ba::app::ptz {

namespace {

/// Orden de los filtros extra en la cadena de la entrada: se limpia antes de ampliar.
constexpr int kOrdenMejora = 10;
constexpr int kOrdenPtz = 20;

QString qs(const std::string& s) { return QString::fromStdString(s); }

} // namespace

struct ControlCamaras::Camara {
    std::string entrada;
    QString backend = QStringLiteral("ninguno"); ///< ninguno | visca | onvif | panasonic
    QJsonObject conf;                            ///< sin la clave
    bool digital = true;
    QString mejora = QStringLiteral("general");                  ///< general | si | no
    QString modo = QStringLiteral("optico");                     ///< con hardware: qué mueve el joystick
    QJsonObject posicion{{"x", 0.5}, {"y", 0.5}, {"zoom", 1.0}}; ///< la digital, para volver a ella
    std::unique_ptr<PtzDigital> dig;
    std::unique_ptr<PtzBackend> hw;
    bool filtroPuesto = false; ///< el PTZ digital ya está en la fuente (y repuesto)
    Posiciones posiciones;     ///< las posiciones con nombre
};

ControlCamaras::ControlCamaras(AppRuntime& rt, std::filesystem::path dataRoot, QObject* parent)
    : QObject(parent), rt_(rt), archivo_(dataRoot / "Config" / "camaras.json"),
      credenciales_(QString::fromStdString((dataRoot / "Credenciales").string())),
      busqueda_(std::make_unique<OnvifBusqueda>()) {
    connect(busqueda_.get(), &OnvifBusqueda::cambio, this, &ControlCamaras::busquedaCambio);
    leer();
    {
        std::ifstream f(dataRoot / "Config" / "ptz-interlock.json");
        const auto j = nlohmann::json::parse(f, nullptr, false);
        if (j.is_object()) {
            interlockActivo_ = j.value("activo", true);
            seguirSuavePermitido_ = j.value("seguir_suave_permitido", true);
        }
    }
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
    const QVariantMap puerto{
        {"clave", "puerto"}, {"etiqueta", "Puerto (vacío = el de siempre)"}, {"tipo", "int"}};
    const QVariantMap usuario{{"clave", "usuario"}, {"etiqueta", "Usuario"}, {"tipo", "text"}};
    const QVariantMap clave{{"clave", "clave"}, {"etiqueta", "Clave"}, {"tipo", "password"}};
    return {
        QVariantMap{{"id", "ninguno"},
                    {"nombre", "Solo digital"},
                    {"ayuda", "La cámara no se mueve: recorte y zoom en la GPU."},
                    {"campos", QVariantList{}}},
        QVariantMap{
            {"id", "visca"},
            {"nombre", "VISCA por IP"},
            {"ayuda", "Sony, PTZOptics, BirdDog, Avonic, Marshall, AVer, Lumens y la mayoría de las chinas. "
                      "Sony usa UDP 52381; PTZOptics y similares, TCP 5678 o UDP 1259."},
            {"campos",
             QVariantList{
                 host,
                 QVariantMap{{"clave", "transporte"},
                             {"etiqueta", "Transporte"},
                             {"tipo", "choice"},
                             {"opciones",
                              QVariantList{QVariantMap{{"valor", "sony"},
                                                       {"texto", "UDP con encabezado Sony (52381)"}},
                                           QVariantMap{{"valor", "tcp"}, {"texto", "TCP crudo (5678)"}},
                                           QVariantMap{{"valor", "udp"}, {"texto", "UDP crudo (1259)"}}}}},
                 puerto,
                 QVariantMap{
                     {"clave", "direccion"}, {"etiqueta", "Dirección VISCA (1-7)"}, {"tipo", "int"}}}}},
        QVariantMap{{"id", "onvif"},
                    {"nombre", "ONVIF"},
                    {"ayuda", "Cámaras IP en general (Profile S). «Buscar en la red» las encuentra solas."},
                    {"campos", QVariantList{host, puerto, usuario, clave,
                                            QVariantMap{{"clave", "perfil"},
                                                        {"etiqueta", "Perfil (vacío = el que tenga PTZ)"},
                                                        {"tipo", "text"}}}}},
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
        for (const QString& k : c.dig->presets().keys())
            presetsDig << k.toInt();
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
            {"motor", c.hw ? c.hw->motor() : QVariantMap{}},
            // El resumen (sin las respuestas crudas: la lista se rearma seguido y la mira la interfaz).
            {"funciones",
             [&c]() {
                 QVariantMap f = c.hw ? c.hw->funciones() : QVariantMap{};
                 f.remove("crudo");
                 return f;
             }()},
            {"estado", c.hw ? c.hw->estado()
                            : (c.digital ? QStringLiteral("Digital (recorte en la GPU)")
                                         : QStringLiteral("PTZ apagado"))},
            {"estadoDigital", c.dig ? c.dig->estado() : QString()},
            {"presetsDigitales", presetsDig},
            {"posiciones", posiciones(qs(c.entrada))}};
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
    const AppConfig& cfg = rt_.config();
    // 1) Mejorar imagen: lo más cerca de la fuente (se limpia antes de ampliar).
    const bool quiereMejora = c.mejora == "si" || (c.mejora == "general" && mejoraGeneral);
    nlohmann::json mejora = nlohmann::json::array();
    if (quiereMejora)
        mejora.push_back({{"id", "mejora"},
                          {"elemento", "bamejora"},
                          {"propiedades",
                           {{"ruido", std::to_string(cfg.mejora.ruido)},
                            {"color", std::to_string(cfg.mejora.color)},
                            {"nitidez", std::to_string(cfg.mejora.nitidez)}}}});
    fuentes::ponerFiltrosExtra(c.entrada, kOrdenMejora, mejora);
    // 2) PTZ digital.
    nlohmann::json ptz = nlohmann::json::array();
    if (c.digital) ptz.push_back({{"id", kFiltroPtz}, {"elemento", "baptz"}});
    fuentes::ponerFiltrosExtra(c.entrada, kOrdenPtz, ptz);
    if (!c.digital) c.filtroPuesto = false;
    // Recién puesto (la cadena se rearma): vuelve al encuadre que tenía.
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

#define BA_PTZ_ACTIVO(entrada)                                                                               \
    QString error;                                                                                           \
    Camara* c = camara(entrada, &error);                                                                     \
    if (c == nullptr) return error;                                                                          \
    PtzBackend* b = activo(*c);                                                                              \
    if (b == nullptr)                                                                                        \
        return QStringLiteral("«%1» no tiene PTZ: prendé el digital o elegí un backend").arg(entrada);

void ControlCamaras::pulsar(const QString& entrada, double pan, double tilt, double zoom) {
    Camara* c = camara(entrada, nullptr);
    PtzBackend* b = c != nullptr ? activo(*c) : nullptr;
    if (b == nullptr) return;
    if (pan == 0 && tilt == 0 && zoom == 0)
        b->parar();
    else
        b->mover(pan, tilt, zoom);
}

QString ControlCamaras::mover(const QString& entrada, double pan, double tilt, double zoom) {
    BA_PTZ_ACTIVO(entrada);
    ++pulsos_[entrada.toStdString()]; // lo manual manda: corta un apuntar a medias
    b->mover(qBound(-1.0, pan, 1.0), qBound(-1.0, tilt, 1.0), qBound(-1.0, zoom, 1.0));
    return {};
}

QString ControlCamaras::parar(const QString& entrada) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    ++pulsos_[entrada.toStdString()];
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
    if (!c->hw || !c->hw->capacidades().foco)
        return QStringLiteral("«%1» no tiene foco controlable").arg(entrada);
    c->hw->enfocar(qBound(-1.0, v, 1.0));
    return {};
}

QString ControlCamaras::autoFoco(const QString& entrada, bool automatico) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    if (!c->hw || !c->hw->capacidades().autoFoco)
        return QStringLiteral("«%1» no deja cambiar el autofoco").arg(entrada);
    c->hw->autoFoco(automatico);
    return {};
}

QString ControlCamaras::irPreset(const QString& entrada, int n) {
    BA_PTZ_ACTIVO(entrada);
    if (n < 1 || n > b->capacidades().presets)
        return QStringLiteral("Preset %1 fuera de rango (1-%2)").arg(n).arg(b->capacidades().presets);
    b->irAPreset(n);
    emit cambio();
    return {};
}

QString ControlCamaras::guardarPreset(const QString& entrada, int n) {
    BA_PTZ_ACTIVO(entrada);
    if (n < 1 || n > b->capacidades().presets)
        return QStringLiteral("Preset %1 fuera de rango (1-%2)").arg(n).arg(b->capacidades().presets);
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

QString ControlCamaras::encuadre(const QString& entrada, double x, double y, double zoom, double segundos) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    if (!c->digital || !c->dig) return QStringLiteral("El PTZ digital está apagado en «%1»").arg(entrada);
    if (!c->dig->ir(qBound(0.0, x, 1.0), qBound(0.0, y, 1.0), qBound(1.0, zoom, 8.0),
                    qBound(0.0, segundos, 10.0)))
        return c->dig->estado();
    guardarPronto();
    return {};
}

QString ControlCamaras::apuntar(const QString& entrada, double u, double v, double ancho, double alto) {
    BA_PTZ_ACTIVO(entrada);
    u = qBound(0.0, u, 1.0);
    v = qBound(0.0, v, 1.0);
    // Cuánto hay que acercar para que el cuadro llene la imagen (el lado que
    // más ocupa manda: así entra entero).
    const double lado = std::max(ancho, alto);
    const double acercar = lado > 0.01 ? 1.0 / lado : 1.0;

    if (b == c->dig.get()) {
        const QVariantMap p = c->dig->posicion();
        const double z = p.value("zoom", 1.0).toDouble();
        const double nz = qBound(1.0, z * acercar, 8.0);
        // Lo que se ve es una ventana de 1/z centrada en (x, y) del cuadro entero.
        double x = p.value("x", 0.5).toDouble() + (u - 0.5) / z;
        double y = p.value("y", 0.5).toDouble() + (v - 0.5) / z;
        // Que la ventana nueva no se salga del cuadro.
        x = qBound(0.5 / nz, x, 1.0 - 0.5 / nz);
        y = qBound(0.5 / nz, y, 1.0 - 0.5 / nz);
        if (!c->dig->ir(x, y, nz, 0.5)) return c->dig->estado();
        guardarPronto();
        return {};
    }

    // Motorizada que sabe apuntar sola (Axis): exacto, con su campo de visión real.
    if (b->apuntar(u, v, ancho, alto)) {
        ++pulsos_[entrada.toStdString()];
        return {};
    }
    // Motorizada: un pulso de pan/tilt proporcional a lo que falta hasta el
    // centro y, si hubo cuadro, uno de zoom. Las constantes son las de una
    // cámara de sala típica a velocidad media; con el zoom muy cerrado se
    // pasa (cada grado es más pantalla) y conviene repetir el clic.
    constexpr double kVelocidad = 0.5;
    constexpr double kSegundosPorPantalla = 1.6; ///< cruzar la imagen entera a kVelocidad
    constexpr double kSegundosPorDoble = 0.9;    ///< duplicar el zoom a kVelocidad
    const double dx = u - 0.5, dy = v - 0.5;
    const double mayor = std::max(std::abs(dx), std::abs(dy));
    const int pulso = ++pulsos_[entrada.toStdString()];
    QPointer<ControlCamaras> yo(this);
    auto zoom = [yo, entrada, pulso, acercar, kVelocidad, kSegundosPorDoble]() {
        if (!yo || yo->pulsos_[entrada.toStdString()] != pulso || acercar <= 1.01) return;
        yo->pulsar(entrada, 0, 0, kVelocidad);
        QTimer::singleShot(static_cast<int>(std::log2(acercar) * kSegundosPorDoble * 1000), yo,
                           [yo, entrada, pulso]() {
                               if (yo && yo->pulsos_[entrada.toStdString()] == pulso)
                                   yo->pulsar(entrada, 0, 0, 0);
                           });
    };
    if (mayor < 0.02) {
        zoom();
        return {};
    }
    // Las dos velocidades en la misma proporción que la distancia: llega en
    // diagonal y en un solo pulso. Tilt + es arriba; v crece hacia abajo.
    b->mover(dx / mayor * kVelocidad, -dy / mayor * kVelocidad, 0);
    QTimer::singleShot(static_cast<int>(mayor * kSegundosPorPantalla * 1000), this,
                       [yo, entrada, pulso, zoom]() {
                           if (!yo || yo->pulsos_[entrada.toStdString()] != pulso) return;
                           yo->pulsar(entrada, 0, 0, 0);
                           QTimer::singleShot(150, yo, zoom);
                       });
    return {};
}

#undef BA_PTZ_ACTIVO

QVariantMap ControlCamaras::posicion(const QString& entrada) const {
    auto it = camaras_.find(entrada.toStdString());
    if (it == camaras_.end() || !it->second->digital || !it->second->dig) return {};
    return it->second->dig->posicion();
}

// ---------------------------------------------------------------------------
// Posiciones con nombre
// ---------------------------------------------------------------------------

QVariantList ControlCamaras::posiciones(const QString& entrada) const {
    auto it = camaras_.find(entrada.toStdString());
    if (it == camaras_.end()) return {};
    QVariantList out = it->second->posiciones.json().toVariantList();
    for (QVariant& v : out) {
        QVariantMap m = v.toMap();
        const QString f = m.value("miniatura").toString();
        if (f.isEmpty()) continue;
        const QString ruta = QString::fromStdWString((carpetaMiniaturas() / f.toStdWString()).wstring());
        if (!QFile::exists(ruta)) continue;
        m["miniatura_ruta"] = ruta;
        // La fecha en la URL: si se regraba, QML no muestra la foto vieja del caché.
        m["miniatura_url"] = QUrl::fromLocalFile(ruta).toString() + "?v=" + m.value("modificada").toString();
        v = m;
    }
    return out;
}

std::filesystem::path ControlCamaras::carpetaMiniaturas() const {
    return archivo_.parent_path().parent_path() / "Miniaturas" / "ptz";
}

QString ControlCamaras::capturarMiniatura(const QString& entrada, const QString& idONombre) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    Posicion* p = c->posiciones.buscar(idONombre);
    if (p == nullptr) return QStringLiteral("No hay una posición «%1» en «%2»").arg(idONombre, entrada);
    std::vector<std::uint8_t> rgba;
    int w = 0, h = 0;
    if (!rt_.engine().captureSource(c->entrada, 320, rgba, w, h) || w <= 0 || h <= 0)
        return QStringLiteral("La entrada «%1» no da imagen ahora: la posición queda sin foto").arg(entrada);
    const QImage img = QImage(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888).convertToFormat(QImage::Format_RGB888);
    std::error_code ec;
    std::filesystem::create_directories(carpetaMiniaturas(), ec);
    const QString nombre = QString::fromStdString(nombreClave(c->entrada)) + "-" + p->id + ".jpg";
    const QString ruta = QString::fromStdWString((carpetaMiniaturas() / nombre.toStdWString()).wstring());
    if (!img.save(ruta, "JPG", 82)) return QStringLiteral("No se pudo guardar la foto en %1").arg(ruta);
    p->miniatura = nombre;
    p->modificada = Posiciones::ahora();
    guardarPronto();
    emit cambio();
    return {};
}

void ControlCamaras::miniaturaPronto(const QString& entrada, const QString& id) {
    // El recorte digital se mueve en ~0,3 s; la cabeza motorizada tarda más,
    // pero al registrar ya está quieta (se registra donde está).
    QTimer::singleShot(600, this, [this, entrada, id]() { capturarMiniatura(entrada, id); });
}

QString ControlCamaras::registrar(Camara& c, Posicion& p, QString& aviso) {
    // La definición de la entrada ahora.
    int w = 0, h = 0;
    rt_.engine().sourceSize(c.entrada, w, h);
    p.ancho = w;
    p.alto = h;
    // El recorte digital.
    p.digital = false;
    if (c.digital && c.dig) {
        const QVariantMap d = c.dig->posicion();
        if (!d.isEmpty()) {
            p.digital = true;
            p.x = d["x"].toDouble();
            p.y = d["y"].toDouble();
            p.zoom = d["zoom"].toDouble();
        }
    }
    // La cabeza: se graba en una memoria de la cámara.
    QStringList faltas;
    if (c.hw) {
        if (!c.hw->listo()) {
            faltas << QStringLiteral("la cámara no contesta: no se grabó en ella");
        } else if (c.hw->memorias() <= 0) {
            faltas << QStringLiteral("la cámara no tiene memorias");
        } else if (c.hw->usaTokens()) {
            p.backend = c.backend;
            p.memoria = 0;
            const QString entrada = qs(c.entrada), id = p.id;
            // ONVIF: la cámara contesta el token después.
            c.hw->grabarPosicion(0, p.nombre, [this, entrada, id](bool ok, QString token) {
                auto it = camaras_.find(entrada.toStdString());
                if (it == camaras_.end()) return;
                if (Posicion* q = it->second->posiciones.buscar(id)) {
                    q->token = ok ? token : QString();
                    if (!ok) q->backend.clear();
                    guardarPronto();
                    emit cambio();
                }
            });
        } else {
            const int m = p.memoria > 0 ? p.memoria : c.posiciones.memoriaLibre(c.hw->memorias());
            if (m <= 0) {
                faltas << QStringLiteral("no quedan memorias libres en la cámara");
            } else {
                p.backend = c.backend;
                p.memoria = m;
                c.hw->grabarPosicion(m, p.nombre, [](bool, QString) {});
            }
        }
    }
    aviso = faltas.join("; ");
    if (!p.digital && !p.tieneCamara() && !(c.hw && c.hw->usaTokens() && c.hw->listo()))
        return QStringLiteral("No hay nada que registrar: prendé el PTZ digital o configurá la cámara (%1)")
            .arg(aviso);
    return {};
}

QVariantMap ControlCamaras::crearPosicion(const QString& entrada, const QString& nombre,
                                          const QString& comentario, double velocidad, double segundos) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return {{"ok", false}, {"error", error}};
    error = c->posiciones.validarNombre(nombre);
    if (!error.isEmpty()) return {{"ok", false}, {"error", error}};
    Posicion nueva;
    nueva.nombre = nombre.trimmed();
    nueva.comentario = comentario.trimmed();
    nueva.velocidad = qBound(0.0, velocidad, 1.0);
    nueva.segundos = segundos < 0 ? -1 : qMin(segundos, 10.0);
    Posicion& p = c->posiciones.agregar(nueva);
    QString aviso;
    error = registrar(*c, p, aviso);
    const QString id = p.id;
    if (!error.isEmpty()) {
        c->posiciones.borrar(id);
        return {{"ok", false}, {"error", error}};
    }
    guardarPronto();
    emit cambio();
    miniaturaPronto(entrada, id);
    return {{"ok", true}, {"id", id}, {"aviso", aviso}};
}

QVariantMap ControlCamaras::editarPosicion(const QString& entrada, const QString& idONombre,
                                           const QVariantMap& cambios) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return {{"ok", false}, {"error", error}};
    Posicion* p = c->posiciones.buscar(idONombre);
    if (p == nullptr)
        return {{"ok", false},
                {"error", QStringLiteral("No hay una posición «%1» en «%2»").arg(idONombre, entrada)}};
    if (cambios.contains("nombre") && cambios["nombre"].toString().trimmed() != p->nombre) {
        error = c->posiciones.validarNombre(cambios["nombre"].toString(), p->id);
        if (!error.isEmpty()) return {{"ok", false}, {"error", error}};
        p->nombre = cambios["nombre"].toString().trimmed();
    }
    if (cambios.contains("comentario")) p->comentario = cambios["comentario"].toString().trimmed();
    if (cambios.contains("velocidad")) p->velocidad = qBound(0.0, cambios["velocidad"].toDouble(), 1.0);
    if (cambios.contains("segundos"))
        p->segundos = cambios["segundos"].toDouble() < 0 ? -1 : qMin(cambios["segundos"].toDouble(), 10.0);
    QString aviso;
    if (cambios.value("regrabar").toBool()) {
        // Con ONVIF se borra la memoria vieja y se graba una nueva.
        if (c->hw && c->hw->usaTokens() && !p->token.isEmpty()) {
            c->hw->borrarPosicion(0, p->token);
            p->token.clear();
        }
        Posicion copia = *p;
        error = registrar(*c, copia, aviso);
        if (!error.isEmpty()) return {{"ok", false}, {"error", error}};
        // registrar() pudo dejar pendiente el token: se copia sin pisarlo.
        Posicion* q = c->posiciones.buscar(copia.id);
        const QString token = q->token;
        *q = copia;
        if (q->token.isEmpty()) q->token = token;
        p = q;
    }
    p->modificada = Posiciones::ahora();
    guardarPronto();
    emit cambio();
    if (cambios.value("regrabar").toBool() || cambios.value("miniatura").toBool()) miniaturaPronto(entrada, p->id);
    return {{"ok", true}, {"aviso", aviso}};
}

QString ControlCamaras::borrarPosicion(const QString& entrada, const QString& idONombre) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    const Posicion* p = c->posiciones.buscar(idONombre);
    if (p == nullptr) return QStringLiteral("No hay una posición «%1» en «%2»").arg(idONombre, entrada);
    // Libera la memoria en la cámara (si es la misma cámara que la grabó).
    if (c->hw && p->tieneCamara() && p->backend == c->backend) c->hw->borrarPosicion(p->memoria, p->token);
    if (!p->miniatura.isEmpty()) {
        std::error_code ec;
        std::filesystem::remove(carpetaMiniaturas() / p->miniatura.toStdWString(), ec);
    }
    c->posiciones.borrar(p->id);
    guardarPronto();
    emit cambio();
    return {};
}

QVariantMap ControlCamaras::irAPosicion(const QString& entrada, const QString& idONombre, double segundos) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return {{"ok", false}, {"error", error}};
    const Posicion* p = c->posiciones.buscar(idONombre);
    if (p == nullptr) {
        QStringList nombres;
        for (const Posicion& x : c->posiciones.lista())
            nombres << x.nombre;
        return {{"ok", false},
                {"error", QStringLiteral("No hay una posición «%1» en «%2» (hay: %3)")
                              .arg(idONombre, entrada,
                                   nombres.isEmpty() ? QStringLiteral("ninguna") : nombres.join(", "))}};
    }
    QStringList avisos;
    bool algo = false;
    if (p->tieneCamara()) {
        if (!c->hw || c->backend != p->backend) {
            avisos << QStringLiteral(
                          "se grabó con el backend %1 y la cámara ahora tiene %2: la cabeza no se mueve")
                          .arg(p->backend, c->backend);
        } else {
            if (!c->hw->listo()) avisos << QStringLiteral("la cámara no contesta");
            c->hw->irAPosicionCon(p->memoria, p->token, p->velocidad);
            algo = true;
        }
    }
    if (p->digital) {
        if (!c->digital || !c->dig) {
            avisos << QStringLiteral("el PTZ digital está apagado: el recorte no se aplica");
        } else if (c->dig->ir(p->x, p->y, p->zoom,
                              qBound(0.0, segundos >= 0 ? segundos : (p->segundos >= 0 ? p->segundos : 1.0), 10.0))) {
            algo = true;
        }
    }
    int w = 0, h = 0;
    rt_.engine().sourceSize(c->entrada, w, h);
    if (p->ancho > 0 && w > 0 && (p->ancho != w || p->alto != h))
        avisos << QStringLiteral("se registró en %1 y la entrada ahora es %2x%3 (el recorte es proporcional: "
                                 "cae en el mismo lugar)")
                      .arg(p->definicion())
                      .arg(w)
                      .arg(h);
    if (!algo)
        return {{"ok", false},
                {"error", QStringLiteral("No se pudo ir a «%1»: %2").arg(p->nombre, avisos.join("; "))}};
    guardarPronto();
    emit cambio();
    return {{"ok", true}, {"nombre", p->nombre}, {"aviso", avisos.join("; ")}};
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
    if (c.backend == "visca")
        c.hw = std::make_unique<PtzVisca>();
    else if (c.backend == "onvif")
        c.hw = std::make_unique<PtzOnvif>();
    else if (c.backend == "panasonic")
        c.hw = std::make_unique<PtzPanasonic>();
    else if (c.backend == "plugin")
        c.hw = std::make_unique<PtzPlugin>();
    if (!c.hw) return;
    connect(c.hw.get(), &PtzBackend::estadoCambio, this, &ControlCamaras::cambio);
    QJsonObject conf = c.conf;
    QByteArray clave;
    if (cred::read(credenciales_, nombreClave(c.entrada).c_str(), clave))
        conf["clave"] = QString::fromUtf8(clave);
    c.hw->configurar(conf);
}

QString ControlCamaras::configurar(const QString& entrada, const QString& backend, const QVariantMap& conf) {
    static const QStringList kValidos{"ninguno", "visca", "onvif", "panasonic", "plugin"};
    if (!kValidos.contains(backend))
        return QStringLiteral("Backend «%1» desconocido (ninguno, visca, onvif, panasonic, plugin)")
            .arg(backend);
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    if (backend == "plugin" && conf.value("plugin").toString().isEmpty())
        return QStringLiteral("Falta el plugin");
    if (backend != "ninguno" && backend != "plugin" && conf.value("host").toString().trimmed().isEmpty())
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

QVariantMap ControlCamaras::funcionesDe(const QString& entrada) {
    Camara* c = camara(entrada, nullptr);
    return c != nullptr && c->hw ? c->hw->funciones() : QVariantMap{};
}

QString ControlCamaras::modoActivo(const QString& entrada) {
    Camara* c = camara(entrada, nullptr);
    PtzBackend* b = c != nullptr ? activo(*c) : nullptr;
    if (b == nullptr) return {};
    return b == c->dig.get() ? QStringLiteral("digital") : QStringLiteral("optico");
}

void ControlCamaras::refrescarPlugins() {
    for (auto& [nombre, c] : camaras_)
        if (c && c->backend == "plugin" && c->hw) static_cast<PtzPlugin*>(c->hw.get())->refrescar();
    emit cambio();
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
    if (modo != "general" && modo != "si" && modo != "no")
        return QStringLiteral("Modo «%1»: general, si o no").arg(modo);
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
    if (modo == "digital" && !c->digital)
        return QStringLiteral("El PTZ digital está apagado en «%1»").arg(entrada);
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
        c->posiciones.cargar(j.value("posiciones").toArray());
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
        if (c->dig && tipoEntrada_.count(nombre)) {
            const QVariantMap p = c->dig->posicion();
            if (!p.isEmpty())
                c->posicion = QJsonObject{
                    {"x", p["x"].toDouble()}, {"y", p["y"].toDouble()}, {"zoom", p["zoom"].toDouble()}};
        }
        QJsonObject conf = c->conf;
        conf.remove("clave");
        todas[qs(nombre)] = QJsonObject{{"backend", c->backend},
                                        {"conf", conf},
                                        {"digital", c->digital},
                                        {"mejora", c->mejora},
                                        {"modo", c->modo},
                                        {"posicion", c->posicion},
                                        {"presets", c->dig ? c->dig->presets() : QJsonObject{}},
                                        {"posiciones", c->posiciones.json()}};
    }
    std::error_code ec;
    std::filesystem::create_directories(archivo_.parent_path(), ec);
    QSaveFile f(QString::fromStdString(archivo_.string()));
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(QJsonObject{{"camaras", todas}}).toJson(QJsonDocument::Indented));
    f.commit();
}

bool ControlCamaras::alAire(const QString& entrada) const {
    auto ss = rt_.engine().sceneSources(rt_.engine().currentScene());
    // Lo que muestra una capa DSK (el recuadro del intérprete de LSA) también está al aire.
    for (const std::string& f : rt_.engine().fuentesDsk()) ss.push_back(f);
    return std::find(ss.begin(), ss.end(), entrada.toStdString()) != ss.end();
}

QVariantMap ControlCamaras::interlock() const {
    return {{"activo", interlockActivo_}, {"seguir_suave_permitido", seguirSuavePermitido_}};
}

void ControlCamaras::setInterlock(const QVariantMap& c) {
    if (c.contains("activo")) interlockActivo_ = c.value("activo").toBool();
    if (c.contains("seguir_suave_permitido")) seguirSuavePermitido_ = c.value("seguir_suave_permitido").toBool();
    std::ofstream o(rt_.dataRoot() / "Config" / "ptz-interlock.json");
    o << nlohmann::json{{"activo", interlockActivo_}, {"seguir_suave_permitido", seguirSuavePermitido_}}.dump(1);
}

} // namespace ba::app::ptz
