// src/app/ui/HistorialPruebasViewModel.cpp — el historial de pruebas (ver el .hpp)

#include "ui/HistorialPruebasViewModel.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>

#include <nlohmann/json.hpp>

#include "i18n/I18nTexto.hpp"
#include "persist/ColaEscritura.hpp"
#include "sala/HistorialPruebas.hpp"

namespace ba::app {

using json = nlohmann::json;
namespace hi = sala::historial;

namespace {
QVariant aVariant(const json& j) {
    const QByteArray b = QByteArray::fromStdString(json{{"x", j}}.dump(-1, ' ', false, json::error_handler_t::replace));
    return QJsonDocument::fromJson(b).object().value("x").toVariant();
}
/// En el pool y de vuelta al hilo de la interfaz.
template <typename F, typename G>
void enElFondo(QObject* quien, F trabajo, G listo) {
    QPointer<QObject> yo(quien);
    QThreadPool::globalInstance()->start([yo, trabajo = std::move(trabajo), listo = std::move(listo)]() mutable {
        json r = trabajo();
        QMetaObject::invokeMethod(
            QCoreApplication::instance(), [yo, r = std::move(r), listo = std::move(listo)]() mutable {
                if (yo) listo(r);
            },
            Qt::QueuedConnection);
    });
}
} // namespace

HistorialPruebasViewModel::HistorialPruebasViewModel(std::filesystem::path datos, QObject* parent)
    : QObject(parent), datos_(std::move(datos)) {
    recargar();
}

void HistorialPruebasViewModel::recargar() {
    cargando_ = true;
    const int ronda = ++ronda_;
    const auto datos = datos_;
    enElFondo(
        this, [datos]() { return hi::listar(datos); },
        [this, ronda](const json& r) {
            if (ronda != ronda_) return;
            cargando_ = false;
            lista_ = aVariant(r).toList();
            emit listaChanged();
        });
}

void HistorialPruebasViewModel::alGuardar() {
    // El informe lo escribe la cola: se espera a que llegue al disco.
    QPointer<HistorialPruebasViewModel> yo(this);
    QTimer::singleShot(1500, this, [yo]() {
        if (yo) yo->recargar();
    });
}

void HistorialPruebasViewModel::abrir(const QString& archivo) {
    const auto datos = datos_;
    const std::string a = archivo.toStdString();
    enElFondo(
        this, [datos, a]() { return hi::leer(datos, a); },
        [this, archivo](const json& r) {
            if (!r.is_object()) return;
            detalle_ = aVariant(r).toMap();
            detalle_["archivo"] = archivo;
            emit detalleChanged();
        });
}

void HistorialPruebasViewModel::cerrar() {
    if (detalle_.isEmpty() && comparacion_.isEmpty()) return;
    detalle_.clear();
    comparacion_.clear();
    emit detalleChanged();
    emit comparacionChanged();
}

void HistorialPruebasViewModel::comparar(const QString& a, const QString& b) {
    const auto datos = datos_;
    const std::string sa = a.toStdString(), sb = b.toStdString();
    enElFondo(
        this, [datos, sa, sb]() {
            const json ja = hi::leer(datos, sa), jb = hi::leer(datos, sb);
            if (!ja.is_object() || !jb.is_object()) return json();
            return hi::comparar(ja, jb);
        },
        [this](const json& r) {
            comparacion_ = r.is_array() ? aVariant(r).toList() : QVariantList{};
            emit comparacionChanged();
        });
}

QString HistorialPruebasViewModel::exportar(const QString& destino) {
    const QUrl u(destino);
    const QString ruta = u.isLocalFile() ? u.toLocalFile() : destino;
    if (ruta.isEmpty()) return QString::fromStdString(ba::i18n::traducir("pruebas", "falta el archivo"));
    std::string contenido;
    if (ruta.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) {
        const QByteArray b = QJsonDocument::fromVariant(QVariant(lista_)).toJson(QJsonDocument::Compact);
        contenido = hi::csv(json::parse(b.toStdString(), nullptr, false));
    } else {
        const QVariant v = detalle_.isEmpty() ? QVariant(lista_) : QVariant(detalle_);
        contenido = QJsonDocument::fromVariant(v).toJson(QJsonDocument::Indented).toStdString();
    }
    ba::persist::cola().escribir(std::filesystem::path(ruta.toStdString()), contenido);
    return {};
}

} // namespace ba::app

#include "moc_HistorialPruebasViewModel.cpp"
