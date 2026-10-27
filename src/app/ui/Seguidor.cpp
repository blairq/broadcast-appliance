// src/app/ui/Seguidor.cpp — ver Seguidor.hpp

#include "ui/Seguidor.hpp"

#include "ptz/ControlCamaras.hpp"
#include "ui/PersonasViewModel.hpp"
#include "video/VideoGrafos.hpp"

#include "Entrada.hpp"

#include <nlohmann/json.hpp>

#if BA_HAVE_ROSTROS
#include "rostros/Personas.hpp"
#include "rostros/Rostros.hpp"
#endif

#include <QCoreApplication>
#include <QDateTime>
#include <QPointer>
#include <QImage>

#include <algorithm>
#include <cmath>

namespace ba::app {

struct Seguidor::Estado {
    QString id; ///< persona reconocida (vacío: por posición o regla)
    QString nombre;
    QString objetivo; ///< lo que se le dijo a la tarjeta Seguimiento
    double x = 0.5, y = 0.5; ///< dónde se la vio por última vez (0..1 de la toma)
    qint64 vistaMs = 0;
    QString estado = "buscando";
    bool parada = true;
};

Seguidor::Seguidor(PersonasViewModel* personas, ptz::ControlCamaras* camaras, QObject* parent)
    : QObject(parent), personas_(personas), camaras_(camaras) {
    // Lo que dicen las tarjetas Seguimiento (en el bucle del motor): al hilo de la interfaz.
    QPointer<Seguidor> yo(this);
    escucha_ = motor::escucharElementos(
        [yo](const std::string& entrada, const std::string& nombre, const std::string& json, GstSample*) {
            if (nombre != "ba-seguir") return;
            QMetaObject::invokeMethod(
                qApp, [yo, e = QString::fromStdString(entrada), json]() { if (yo) yo->alSeguir(e, json); },
                Qt::QueuedConnection);
        });
}

Seguidor::~Seguidor() {
    motor::dejarDeEscuchar(escucha_);
    for (auto& [entrada, e] : seguidas_)
        if (camaras_ && !e->parada) camaras_->parar(entrada);
}

bool Seguidor::disponible() const {
#if BA_HAVE_ROSTROS
    return personas_ && personas_->disponible();
#else
    return false;
#endif
}

QVariantMap Seguidor::siguiendo() const {
    QVariantMap m;
    for (const auto& [entrada, e] : seguidas_)
        m[entrada] = QVariantMap{
            {"id", e->id}, {"nombre", e->nombre}, {"estado", e->estado}, {"x", e->x}, {"y", e->y}};
    return m;
}

#if BA_HAVE_ROSTROS
namespace {
/// Las caras vistas, en 0..1 del cuadro y con su nombre si se las reconoce.
QVariantList describir(PersonasViewModel* p, const std::vector<Reconocedor::Visto>& vistos,
                       const QImage& img) {
    QVariantList out;
    const double w = std::max(1, img.width()), h = std::max(1, img.height());
    for (const auto& v : vistos) {
        QVariantMap c{
            {"x", v.cara.x / w}, {"y", v.cara.y / h}, {"ancho", v.cara.ancho / w}, {"alto", v.cara.alto / h}};
        if (p != nullptr && !v.vector.empty()) {
            const auto m = p->libreta().buscar(v.vector);
            if (!m.id.empty() && m.parecido >= ba::rostros::kMismaPersona()) {
                c["id"] = QString::fromStdString(m.id);
                c["nombre"] = QString::fromStdString(m.nombre);
                c["parecido"] = m.parecido;
            }
        }
        out << c;
    }
    return out;
}
} // namespace
#endif

void Seguidor::analizar(const QString& entrada) {
#if BA_HAVE_ROSTROS
    if (!personas_) return;
    QPointer<Seguidor> yo(this);
    personas_->pedirAnalisis(
        "entrada:" + entrada, false,
        [yo, entrada](std::vector<Reconocedor::Visto> vistos, const QImage& img, const QString& error) {
            if (!yo) return;
            if (error.isEmpty()) {
                yo->caras_[entrada] = describir(yo->personas_, vistos, img);
                emit yo->cambio();
            }
            emit yo->analizado(entrada, error);
        });
#else
    emit analizado(entrada, tr("Este programa se armó sin reconocimiento de rostros"));
#endif
}

QString Seguidor::seguir(const QString& entrada, const QVariantMap& objetivo) {
#if BA_HAVE_ROSTROS
    if (!personas_ || !camaras_ || !grafos_) return tr("El seguimiento no está disponible");
    if (camaras_->modoActivo(entrada).isEmpty())
        return tr("«%1» no tiene PTZ: prendé el digital o configurá su motor").arg(entrada);
    QString obj;
    if (!objetivo.value("id").toString().isEmpty())
        obj = "persona:" + objetivo.value("id").toString();
    else if (objetivo.contains("cara"))
        obj = "id:" + QString::number(objetivo.value("cara").toInt());
    else if (!objetivo.value("regla").toString().isEmpty())
        obj = objetivo.value("regla").toString();
    else
        obj = QString::number(objetivo.value("x", 0.5).toDouble(), 'f', 4) + "," +
              QString::number(objetivo.value("y", 0.5).toDouble(), 'f', 4);
    auto& e = seguidas_[entrada];
    if (!e) e = std::make_unique<Estado>();
    e->id = objetivo.value("id").toString();
    e->nombre = objetivo.value("nombre").toString();
    e->objetivo = obj;
    e->x = objetivo.value("x", 0.5).toDouble();
    e->y = objetivo.value("y", 0.5).toDouble();
    e->vistaMs = QDateTime::currentMSecsSinceEpoch();
    e->estado = "buscando";
    // La tarjeta Rostros (que reconozca: Personas pone el nombre de cada cara) y
    // la de Seguimiento con su objetivo; el resto pasa en el pipeline.
    personas_->usarCamara(entrada, true);
    const std::string err = grafos_->configurarSeguir(entrada.toStdString(), obj.toStdString());
    if (!err.empty()) return QString::fromStdString(err);
    emit cambio();
    return {};
#else
    Q_UNUSED(entrada);
    Q_UNUSED(objetivo);
    return tr("Este programa se armó sin reconocimiento de rostros");
#endif
}

QString Seguidor::seguirPunto(const QString& entrada, double u, double v) {
    // De lo que se VE (después del PTZ digital) a la toma entera.
    double x = u, y = v;
    if (camaras_) {
        const QVariantMap p = camaras_->posicion(entrada);
        const double z = std::max(1.0, p.value("zoom", 1.0).toDouble());
        x = p.value("x", 0.5).toDouble() - 0.5 / z + u / z;
        y = p.value("y", 0.5).toDouble() - 0.5 / z + v / z;
    }
    return seguir(entrada, QVariantMap{{"x", std::clamp(x, 0.0, 1.0)}, {"y", std::clamp(y, 0.0, 1.0)}});
}

void Seguidor::dejar(const QString& entrada) {
    auto it = seguidas_.find(entrada);
    if (it == seguidas_.end()) return;
    const bool estabaMoviendo = !it->second->parada;
    seguidas_.erase(it);
    if (grafos_) grafos_->configurarSeguir(entrada.toStdString(), "");
    if (camaras_ && estabaMoviendo) camaras_->parar(entrada);
    if (personas_) personas_->usarCamara(entrada, false);
    emit cambio();
}

void Seguidor::alSeguir(const QString& entrada, const std::string& json) {
    auto it = seguidas_.find(entrada);
    if (it == seguidas_.end()) return;
    Estado& e = *it->second;
    const nlohmann::json j = nlohmann::json::parse(json, nullptr, false);
    if (!j.is_object()) return;
    e.estado = QString::fromStdString(j.value("estado", std::string("buscando")));
    if (j.contains("cara") && j["cara"].is_object()) {
        const auto& c = j["cara"];
        e.x = c.value("x", 0.0) + c.value("ancho", 0.0) / 2;
        e.y = c.value("y", 0.0) + c.value("alto", 0.0) / 2;
        e.vistaMs = QDateTime::currentMSecsSinceEpoch();
        if (c.contains("nombre")) e.nombre = QString::fromStdString(c.value("nombre", std::string()));
        // Una cámara motorizada no está en el pipeline: el pulso, desde acá.
        if (camaras_ && camaras_->modoActivo(entrada) != "digital") corregir(entrada, e, e.x, e.y, c.value("alto", 0.0));
    } else if (camaras_ && !e.parada && camaras_->modoActivo(entrada) != "digital") {
        camaras_->parar(entrada);
        e.parada = true;
    }
    emit cambio();
}

void Seguidor::corregir(const QString& entrada, Estado& e, double cx, double cy, double /*alto*/) {
    if (!camaras_) return;
    const double ex = cx - 0.5, ey = cy - 0.5;
    const double dist = std::max(std::abs(ex), std::abs(ey));
    // Zona muerta: centrada.
    if (dist < 0.07) {
        if (!e.parada) {
            camaras_->parar(entrada);
            e.parada = true;
        }
        e.estado = "centrada";
        return;
    }
    e.estado = "siguiendo";
    // Óptico: un pulso proporcional (tilt + es arriba; y crece hacia abajo).
    const auto vel = [](double err) {
        if (std::abs(err) < 0.04) return 0.0;
        const double v = std::clamp(std::abs(err) * 1.2, 0.12, 0.6);
        return err > 0 ? v : -v;
    };
    camaras_->mover(entrada, vel(ex), vel(-ey), 0);
    e.parada = false;
    const int ms = static_cast<int>(std::clamp(120.0 + dist * 500.0, 120.0, 320.0));
    QPointer<Seguidor> yo(this);
    QTimer::singleShot(ms, this, [yo, entrada]() {
        if (!yo) return;
        auto it = yo->seguidas_.find(entrada);
        if (it == yo->seguidas_.end()) return;
        if (yo->camaras_) yo->camaras_->parar(entrada);
        it->second->parada = true;
    });
}

} // namespace ba::app
