// src/app/ui/ScopeVista.cpp — ver ScopeVista.hpp

#include "ui/ScopeVista.hpp"

#include "ui/ImagenViewModel.hpp"
#include "video/Scopes.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QImage>
#include <QThreadPool>

#include <algorithm>
#include <cmath>

namespace ba::app {

ScopeVista::ScopeVista(QObject* parent) : QObject(parent) {
    // ~5 veces por segundo: un scope no necesita más, y se dibuja solo con un cuadro nuevo.
    reloj_.setInterval(200);
    connect(&reloj_, &QTimer::timeout, this, &ScopeVista::mirar);
}

QObject* ScopeVista::imagen() const { return imagen_.data(); }

void ScopeVista::setImagen(QObject* o) {
    auto* i = qobject_cast<ImagenViewModel*>(o);
    if (i == imagen_) return;
    imagen_ = i;
    emit configCambio();
    otraVez();
}

void ScopeVista::setFuente(const QString& f) {
    if (f == fuente_) return;
    fuente_ = f;
    emit configCambio();
    otraVez();
}

void ScopeVista::setTipo(const QString& t) {
    if (t == tipo_) return;
    tipo_ = t;
    emit configCambio();
    otraVez();
}

void ScopeVista::setAncho(int a) {
    if (a == ancho_) return;
    ancho_ = a;
    emit configCambio();
    otraVez();
}

void ScopeVista::setAlto(int a) {
    if (a == alto_) return;
    alto_ = a;
    emit configCambio();
    otraVez();
}

void ScopeVista::setActivo(bool a) {
    if (a == activo_) return;
    activo_ = a;
    emit configCambio();
    if (activo_) {
        reloj_.start();
        otraVez();
    } else {
        reloj_.stop();
    }
}

void ScopeVista::otraVez() {
    ++pedido_;
    hayUltima_ = false;
    if (activo_) mirar();
}

void ScopeVista::poner(QVariantMap d) {
    if (d == dato_) return;
    dato_ = std::move(d);
    emit datoCambio();
}

void ScopeVista::mirar() {
    if (!activo_ || !imagen_ || dibujando_) return;
    video::TipoScope tipo = video::TipoScope::Onda;
    if (!video::tipoScopeDeTexto(tipo_, tipo)) return poner({{"error", tr("tipo desconocido")}});
    QImage img;
    std::uint64_t serie = 0;
    QString error;
    if (!imagen_->cuadroScope(fuente_, img, serie, error))
        return poner(error.isEmpty() ? QVariantMap{{"esperando", true}} : QVariantMap{{"error", error}});
    if (hayUltima_ && serie == ultimaSerie_) return; // el mismo cuadro: el dibujo no cambia
    hayUltima_ = true;
    ultimaSerie_ = serie;
    // Dibujar y medir lleva su tiempo: en otro hilo, y de a uno.
    dibujando_ = true;
    QPointer<ScopeVista> yo(this);
    const int pedido = pedido_;
    const int w = std::clamp(ancho_, 200, 1600), h = std::clamp(alto_, 120, 1200);
    QThreadPool::globalInstance()->start([yo, pedido, img, tipo, w, h]() {
        const video::Legalidad l = video::medirLegalidad(img);
        const QImage s = video::dibujarScope(img, tipo, w, h);
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        s.save(&buf, "PNG");
        QVariantMap d{{"imagen", "data:image/png;base64," + QString::fromLatin1(png.toBase64())},
                      {"legalidad", QVariantMap{{"negros_aplastados_pct", std::round(l.lumaBajaPct * 10) / 10},
                                                {"blancos_quemados_pct", std::round(l.lumaAltaPct * 10) / 10},
                                                {"fuera_de_gama_pct", std::round(l.cromaPct * 10) / 10}}}};
        QMetaObject::invokeMethod(
            qApp,
            [yo, pedido, d = std::move(d)]() mutable {
                if (!yo) return;
                yo->dibujando_ = false;
                if (pedido == yo->pedido_) yo->poner(std::move(d));
                else yo->mirar(); // cambió lo pedido mientras dibujaba
            },
            Qt::QueuedConnection);
    });
}

} // namespace ba::app

#include "moc_ScopeVista.cpp"
