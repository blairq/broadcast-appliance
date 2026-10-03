// src/app/ui/Seguidor.cpp — ver Seguidor.hpp

#include "ui/Seguidor.hpp"

#include "ptz/ControlCamaras.hpp"
#include "ui/PersonasViewModel.hpp"

#if BA_HAVE_ROSTROS
#include "rostros/Personas.hpp"
#include "rostros/Rostros.hpp"
#endif

#include <QDateTime>
#include <QImage>

#include <algorithm>
#include <cmath>

namespace ba::app {

struct Seguidor::Estado {
    QString id;      ///< persona reconocida (vacío: por posición)
    QString nombre;
    double x = 0.5, y = 0.5; ///< dónde se la vio por última vez (0..1)
    qint64 vistaMs = 0;
    QString estado = "buscando";
    bool enVuelo = false;
    bool parada = true;
    QTimer timer;
};

Seguidor::Seguidor(PersonasViewModel* personas, ptz::ControlCamaras* camaras, QObject* parent)
    : QObject(parent), personas_(personas), camaras_(camaras) {}

Seguidor::~Seguidor() {
    for (auto& [entrada, e] : seguidas_) {
        e->timer.stop();
        if (camaras_) camaras_->parar(entrada);
    }
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
        m[entrada] = QVariantMap{{"id", e->id}, {"nombre", e->nombre}, {"estado", e->estado}, {"x", e->x}, {"y", e->y}};
    return m;
}

#if BA_HAVE_ROSTROS
namespace {
/// Las caras vistas, en 0..1 del cuadro y con su nombre si se las reconoce.
QVariantList describir(PersonasViewModel* p, const std::vector<Reconocedor::Visto>& vistos, const QImage& img) {
    QVariantList out;
    const double w = std::max(1, img.width()), h = std::max(1, img.height());
    for (const auto& v : vistos) {
        QVariantMap c{{"x", v.cara.x / w}, {"y", v.cara.y / h}, {"ancho", v.cara.ancho / w}, {"alto", v.cara.alto / h}};
        if (p != nullptr && !v.vector.empty()) {
            const auto m = p->libreta().buscar(v.vector);
            if (!m.id.empty() && m.parecido >= ba::rostros::kMismaPersona) {
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
    personas_->pedirAnalisis("entrada:" + entrada, false,
                             [yo, entrada](std::vector<Reconocedor::Visto> vistos, const QImage& img, const QString& error) {
                                 if (!yo) return;
                                 if (error.isEmpty()) {
                                     yo->caras_[entrada] = describir(yo->personas_, vistos, img);
                                     emit yo->cambio();
                                 }
                                 emit yo->analizado(entrada, error);
                             });
#else
    emit analizado(entrada, "Este programa se armó sin reconocimiento de rostros");
#endif
}

QString Seguidor::seguir(const QString& entrada, const QVariantMap& objetivo) {
#if BA_HAVE_ROSTROS
    if (!personas_ || !camaras_) return "El seguimiento no está disponible";
    if (camaras_->modoActivo(entrada).isEmpty()) return "«" + entrada + "» no tiene PTZ: prendé el digital o configurá su motor";
    auto& e = seguidas_[entrada];
    if (!e) e = std::make_unique<Estado>();
    e->id = objetivo.value("id").toString();
    e->nombre = objetivo.value("nombre").toString();
    e->x = objetivo.value("x", 0.5).toDouble();
    e->y = objetivo.value("y", 0.5).toDouble();
    e->vistaMs = QDateTime::currentMSecsSinceEpoch();
    e->estado = "buscando";
    e->parada = true;
    personas_->usarCamara(entrada, true);
    e->timer.setInterval(300);
    QObject::disconnect(&e->timer, nullptr, this, nullptr);
    connect(&e->timer, &QTimer::timeout, this, [this, entrada]() { vuelta(entrada); });
    e->timer.start();
    emit cambio();
    return {};
#else
    Q_UNUSED(entrada);
    Q_UNUSED(objetivo);
    return "Este programa se armó sin reconocimiento de rostros";
#endif
}

void Seguidor::dejar(const QString& entrada) {
    auto it = seguidas_.find(entrada);
    if (it == seguidas_.end()) return;
    it->second->timer.stop();
    seguidas_.erase(it);
    if (camaras_) camaras_->parar(entrada);
    if (personas_) personas_->usarCamara(entrada, false);
    emit cambio();
}

void Seguidor::vuelta(const QString& entrada) {
#if BA_HAVE_ROSTROS
    auto it = seguidas_.find(entrada);
    if (it == seguidas_.end() || !personas_) return;
    Estado& e = *it->second;
    if (e.enVuelo) return;
    e.enVuelo = true;
    QPointer<Seguidor> yo(this);
    personas_->pedirAnalisis("entrada:" + entrada, true,
                             [yo, entrada](std::vector<Reconocedor::Visto> vistos, const QImage& img, const QString& error) {
                                 if (!yo) return;
                                 auto it2 = yo->seguidas_.find(entrada);
                                 if (it2 == yo->seguidas_.end()) return;
                                 Estado& e = *it2->second;
                                 e.enVuelo = false;
                                 const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
                                 const QVariantList caras = error.isEmpty() ? describir(yo->personas_, vistos, img) : QVariantList{};
                                 if (error.isEmpty()) yo->caras_[entrada] = caras;
                                 // El objetivo: la misma persona si está reconocida; si no, la cara más cercana a donde estaba.
                                 int mejor = -1;
                                 double dmin = 1e9;
                                 for (int i = 0; i < caras.size(); ++i) {
                                     const QVariantMap c = caras[i].toMap();
                                     const double cx = c["x"].toDouble() + c["ancho"].toDouble() / 2, cy = c["y"].toDouble() + c["alto"].toDouble() / 2;
                                     if (!e.id.isEmpty()) {
                                         if (c.value("id").toString() == e.id) {
                                             mejor = i;
                                             break;
                                         }
                                         continue;
                                     }
                                     const double d = std::hypot(cx - e.x, cy - e.y);
                                     if (d < dmin) {
                                         dmin = d;
                                         mejor = i;
                                     }
                                 }
                                 // Sin nombre, una cara muy lejos de donde estaba es otra persona.
                                 if (e.id.isEmpty() && mejor >= 0 && dmin > 0.35 && ahora - e.vistaMs < 1500) mejor = -1;
                                 if (mejor < 0) {
                                     if (!e.parada) {
                                         yo->camaras_->parar(entrada);
                                         e.parada = true;
                                     }
                                     e.estado = ahora - e.vistaMs > 2000 ? "perdida: esperando a que vuelva" : "buscando";
                                     emit yo->cambio();
                                     return;
                                 }
                                 const QVariantMap c = caras[mejor].toMap();
                                 const double cx = c["x"].toDouble() + c["ancho"].toDouble() / 2, cy = c["y"].toDouble() + c["alto"].toDouble() / 2;
                                 e.x = cx;
                                 e.y = cy;
                                 e.vistaMs = ahora;
                                 if (e.nombre.isEmpty()) e.nombre = c.value("nombre").toString();
                                 yo->corregir(entrada, e, cx, cy, c["alto"].toDouble());
                                 emit yo->cambio();
                             });
#else
    Q_UNUSED(entrada);
#endif
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
    if (camaras_->modoActivo(entrada) == "digital") {
        // El recorte: un paso hacia la cara (lo que se ve es una ventana de 1/zoom).
        const QVariantMap p = camaras_->posicion(entrada);
        double z = p.value("zoom", 1.0).toDouble();
        if (z < 1.3) z = 1.8; // sin zoom, el recorte no tiene para dónde correrse
        const double nx = std::clamp(p.value("x", 0.5).toDouble() + ex / z * 0.7, 0.5 / z, 1.0 - 0.5 / z);
        const double ny = std::clamp(p.value("y", 0.5).toDouble() + ey / z * 0.7, 0.5 / z, 1.0 - 0.5 / z);
        camaras_->encuadre(entrada, nx, ny, z, 0.3);
        return;
    }
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
