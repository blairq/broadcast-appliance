// src/app/ui/MultiviewViewModel.cpp — ver el .hpp

#include "ui/MultiviewViewModel.hpp"
#include "arranque/ArchivoConfigQt.hpp"

#include "pantallas/Monitores.hpp"
#include "pantallas/Ventanas.hpp"

#include <QAbstractItemModel>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRect>

#include <algorithm>
#include <system_error>
#include <vector>

namespace ba::app {

namespace {

QVariantMap deFabrica() {
    return {{"fuentes", QStringList{"program", "armada"}},
            {"distribucion", "control"},
            {"separacion", 8},
            {"fondo", "#202024"},
            {"borde", 1},
            {"colorBorde", "#5a5a64"},
            {"grosorTally", 2},
            {"nombres", true},
            {"tamNombres", 18},
            {"tally", true},
            {"vumetros", true}};
}

QString idNuevo() { return QString::number(QDateTime::currentMSecsSinceEpoch(), 36); }

/// Una ventana de 16:9 centrada en el monitor `g` (o escalonada).
QRect geometriaEn(QRect g, int escalon) {
    if (!g.isValid()) g = QRect(0, 0, 1920, 1080);
    const int w = std::min(1280, g.width() - 80), h = w * 9 / 16;
    return QRect(g.x() + (g.width() - w) / 2 + escalon * 40, g.y() + (g.height() - h) / 2 + escalon * 40, w, h);
}

QRect rectDe(const QVariantMap& m) {
    return QRect(m.value("x").toInt(), m.value("y").toInt(), m.value("ancho").toInt(), m.value("alto").toInt());
}

} // namespace

MultiviewViewModel::MultiviewViewModel(std::filesystem::path config, QObject* parent)
    : QObject(parent), config_(std::move(config)) {
    leer();
}

void MultiviewViewModel::setVentanas(VentanasModel* ventanas, MonitoresModel* monitores) {
    ventanas_ = ventanas;
    monitores_ = monitores;
    if (ventanas_ == nullptr) return;
    connect(ventanas_, &VentanasModel::cambio, this, &MultiviewViewModel::ventanasCambio);
    connect(ventanas_, &QAbstractItemModel::dataChanged, this, &MultiviewViewModel::ventanasCambio);
}

void MultiviewViewModel::leer() {
    const QVariantMap raiz = config::leerQObjeto(config_ / "multiview.json").toVariantMap();
    QVariantList vistas = raiz.value("vistas").toList();
    if (vistas.isEmpty()) {
        QVariantMap v = deFabrica();
        for (auto it = raiz.begin(); it != raiz.end(); ++it)
            if (it.key() != "vistas" && it.key() != "actual" && it.key() != ba::persist::kClaveEsquema)
                v[it.key()] = it.value(); // formato viejo
        v["id"] = "1";
        v["nombre"] = tr("Multiview %1").arg(1);
        vistas << v;
    }
    // Lo que falte en una vista (de una versión vieja), de fábrica.
    for (QVariant& x : vistas) {
        QVariantMap v = x.toMap();
        const QVariantMap fab = deFabrica();
        for (auto it = fab.begin(); it != fab.end(); ++it)
            if (!v.contains(it.key())) v[it.key()] = it.value();
        x = v;
    }
    vistas_ = vistas;
    actual_ = raiz.value("actual").toString();
    if (indice(actual_) < 0) actual_ = vistas.first().toMap().value("id").toString();
}

int MultiviewViewModel::indice(const QString& id) const {
    for (int i = 0; i < vistas_.size(); ++i)
        if (vistas_[i].toMap().value("id").toString() == id) return i;
    return -1;
}

void MultiviewViewModel::guardar() {
    config::guardarQ(config_ / "multiview.json", QJsonObject::fromVariantMap({{"vistas", vistas_}, {"actual", actual_}}));
    emit cambio();
}

QVariantMap MultiviewViewModel::de(const QString& id) const {
    const int i = indice(id.isEmpty() ? actual_ : id);
    return i >= 0 ? vistas_[i].toMap() : QVariantMap{};
}

void MultiviewViewModel::cambiar(const QVariantMap& cambios) { cambiarDe(actual_, cambios); }

void MultiviewViewModel::cambiarDe(const QString& id, const QVariantMap& cambios) {
    const int i = indice(id);
    if (i < 0) return;
    QVariantMap v = vistas_[i].toMap();
    for (auto it = cambios.begin(); it != cambios.end(); ++it)
        if (it.key() != "id") v[it.key()] = it.value();
    vistas_[i] = v;
    guardar();
}

void MultiviewViewModel::setActual(const QString& id) {
    if (indice(id) < 0 || id == actual_) return;
    actual_ = id;
    guardar();
}

QString MultiviewViewModel::nueva(const QString& nombre, bool copiarActual) {
    QVariantMap v = copiarActual ? elegida() : deFabrica();
    v["id"] = idNuevo();
    v["nombre"] = nombre.trimmed().isEmpty() ? tr("Multiview %1").arg(vistas_.size() + 1) : nombre.trimmed();
    vistas_ << v;
    actual_ = v["id"].toString();
    guardar();
    return actual_;
}

void MultiviewViewModel::renombrar(const QString& id, const QString& nombre) {
    if (nombre.trimmed().isEmpty()) return;
    cambiarDe(id, {{"nombre", nombre.trimmed()}});
}

bool MultiviewViewModel::borrar(const QString& id) {
    const int i = indice(id);
    if (i < 0 || vistas_.size() <= 1) return false; // siempre queda una
    vistas_.removeAt(i);
    if (actual_ == id) actual_ = vistas_[std::max(0, i - 1)].toMap().value("id").toString();
    guardar();
    return true;
}

void MultiviewViewModel::restablecer() {
    const QVariantMap v = elegida();
    QVariantMap fab = deFabrica();
    fab["nombre"] = v.value("nombre");
    cambiar(fab);
}

QStringList MultiviewViewModel::conVentana() const {
    QStringList out;
    if (ventanas_ == nullptr) return out;
    for (const auto& w : ventanas_->ventanas())
        for (const QString& c : w.cosas)
            if (c.startsWith("multiview:") && !out.contains(c.mid(10))) out << c.mid(10);
    return out;
}

void MultiviewViewModel::abrirEnVentana(const QString& id, const QString& monitor, bool pantallaCompleta) {
    if (ventanas_ == nullptr || monitores_ == nullptr || de(id).isEmpty()) return;
    // La ventana de esa multivista se cierra y se abre donde se pide (así «Llevar
    // la ventana» la manda al monitor elegido aunque ya estuviera abierta).
    ventanas_->cerrarCosa("multiview:" + id);
    // `monitor`: id, número de orden, nombre o nombre del sistema (Monitores::resolver).
    const QString mid = monitor.isEmpty() ? QString() : monitores_->resolver(monitor);
    QVariantMap donde, principal;
    for (const QVariant& v : monitores_->conectadosEnOrden()) {
        const QVariantMap m = v.toMap();
        if (m.value("id").toString() == mid) donde = m;
        if (m.value("principal").toBool()) principal = m;
    }
    const bool hay = !donde.isEmpty();
    ventanas_->abrirEn({"multiview:" + id}, {}, hay ? mid : principal.value("id").toString(),
                       geometriaEn(rectDe(hay ? donde : principal), 0), pantallaCompleta && hay);
}

int MultiviewViewModel::abrirTodas() {
    if (ventanas_ == nullptr || monitores_ == nullptr) return 0;
    cerrarVentanas();
    QVariantMap principal;
    QVariantList otros;
    for (const QVariant& v : monitores_->conectadosEnOrden()) {
        if (v.toMap().value("principal").toBool())
            principal = v.toMap();
        else
            otros << v;
    }
    int escalon = 0;
    for (int i = 0; i < vistas_.size(); ++i) {
        const QString id = vistas_[i].toMap().value("id").toString();
        if (i < otros.size()) {
            const QVariantMap m = otros[i].toMap(); // una por monitor, completa, en el orden físico
            ventanas_->abrirEn({"multiview:" + id}, {}, m.value("id").toString(), geometriaEn(rectDe(m), 0), true);
        } else {
            ventanas_->abrirEn({"multiview:" + id}, {}, principal.value("id").toString(),
                               geometriaEn(rectDe(principal), escalon++), false);
        }
    }
    return vistas_.size();
}

int MultiviewViewModel::cerrarVentanas() {
    if (ventanas_ == nullptr) return 0;
    std::vector<int> cerrar;
    for (const auto& w : ventanas_->ventanas())
        if (w.cosas.size() == 1 && w.cosas.first().startsWith("multiview:")) cerrar.push_back(w.id);
    for (int c : cerrar) ventanas_->cerrar(c);
    return static_cast<int>(cerrar.size());
}

} // namespace ba::app
