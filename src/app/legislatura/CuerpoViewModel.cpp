// SPDX-License-Identifier: GPL-2.0-or-later
#include "legislatura/CuerpoViewModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace ba::app::legislatura {

namespace {
nlohmann::json deMapa(const QVariantMap& m) {
    return nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson().toStdString(), nullptr, false);
}
} // namespace

CuerpoViewModel::CuerpoViewModel(std::filesystem::path datos, QObject* parent)
    : QObject(parent), datos_(std::move(datos)), c_(std::make_unique<Cuerpo>(datos_ / "Config" / "cuerpo.json")) {}

QString CuerpoViewModel::nombre() const { return QString::fromStdString(c_->nombre()); }

QVariantList CuerpoViewModel::integrantes() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(c_->integrantes().dump())).array().toVariantList();
}

void CuerpoViewModel::setNombre(const QString& n) {
    c_->setNombre(n.trimmed().toStdString());
    emit cambio();
}

QString CuerpoViewModel::guardar(const QVariantMap& campos) {
    nlohmann::json j = deMapa(campos);
    // La cámara y el preset vienen sueltos de la tabla.
    if (j.contains("camara")) {
        j["preset_ptz"] = {{"camara", j.value("camara", std::string())}, {"posicion", j.value("posicion", std::string())}};
        j.erase("camara");
        j.erase("posicion");
    }
    if (j.contains("microfono") && j["microfono"].is_string())
        j["microfono"] = {{"fuente", j["microfono"].get<std::string>()}};
    std::string err;
    if (c_->guardar(j, &err).empty()) return QString::fromStdString(err);
    emit cambio();
    return {};
}

bool CuerpoViewModel::borrar(const QString& id) {
    const bool ok = c_->borrar(id.toStdString());
    if (ok) emit cambio();
    return ok;
}

bool CuerpoViewModel::validar(const QString& id) {
    const bool ok = c_->validar(id.toStdString(), "operador");
    if (ok) emit cambio();
    return ok;
}

QString CuerpoViewModel::importar(const QString& archivo, bool reemplazar) {
    const QString ruta = archivo.startsWith("file:") ? QUrl(archivo).toLocalFile() : archivo;
    const nlohmann::json r = c_->importarCsv(ruta.toStdString(), reemplazar);
    emit cambio();
    QString t = QString("Importados %1, actualizados %2").arg(r.value("importados", 0)).arg(r.value("actualizados", 0));
    if (!r["errores"].empty()) t += QString(", %1 con error (%2)").arg(r["errores"].size()).arg(QString::fromStdString(r["errores"][0].get<std::string>()));
    return t;
}

QString CuerpoViewModel::plantilla() {
    const auto ruta = datos_ / "Exportado" / "planilla-legisladores.csv";
    return Cuerpo::escribirPlantilla(ruta) ? QString::fromStdWString(ruta.wstring()) : QString();
}

QVariantMap CuerpoViewModel::recinto() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(c_->recinto().dump())).object().toVariantMap();
}

QString CuerpoViewModel::editarRecinto(const QVariantMap& cambios) {
    const std::string e = c_->setRecinto(deMapa(cambios));
    if (e.empty()) emit cambio();
    return QString::fromStdString(e);
}

QVariantMap CuerpoViewModel::buscar(const QString& clave) const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(c_->buscar(clave.toStdString()).dump())).object().toVariantMap();
}

} // namespace ba::app::legislatura
