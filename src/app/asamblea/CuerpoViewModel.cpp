// SPDX-License-Identifier: GPL-2.0-or-later
#include "asamblea/CuerpoViewModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace ba::app::asamblea {

namespace {
nlohmann::json deMapa(const QVariantMap& m) {
    return nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson().toStdString(), nullptr, false);
}
} // namespace

CuerpoViewModel::CuerpoViewModel(std::filesystem::path datos, QObject* parent)
    : QObject(parent), datos_(std::move(datos)), c_(std::make_unique<Cuerpo>(datos_ / "Config" / "cuerpo.json")) {}

QString CuerpoViewModel::nombre() const { return QString::fromStdString(c_->nombre()); }

QVariantList CuerpoViewModel::integrantes() const {
    QVariantList out = QJsonDocument::fromJson(QByteArray::fromStdString(c_->integrantes().dump())).array().toVariantList();
    for (QVariant& v : out) {
        QVariantMap m = v.toMap();
        const QString ruta = rutaFoto(m.value("id").toString());
        if (!ruta.isEmpty())
            m["foto_url"] = QUrl::fromLocalFile(ruta).toString() + "?v=" +
                            QString::number(QFileInfo(ruta).lastModified().toMSecsSinceEpoch());
        v = m;
    }
    return out;
}

QString CuerpoViewModel::rutaFoto(const QString& idOBanca) const {
    const nlohmann::json i = c_->buscar(idOBanca.toStdString());
    const QString f = QString::fromStdString(i.value("foto", std::string()));
    if (f.isEmpty()) return {};
    const QString ruta = QDir::isAbsolutePath(f) ? f : QString::fromStdWString((datos_ / f.toStdWString()).wstring());
    return QFileInfo::exists(ruta) ? ruta : QString();
}

QString CuerpoViewModel::guardarFoto(const QString& id, const QImage& img) {
    if (img.isNull()) return "no hay imagen";
    // Cuadrada: el centro, un poco hacia arriba (en una foto de busto la cara está arriba del centro).
    const int lado = std::min(img.width(), img.height());
    const int x = (img.width() - lado) / 2;
    const int y = std::max(0, std::min(img.height() - lado, (img.height() - lado) / 3));
    const QImage cuadrada = img.copy(x, y, lado, lado).scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                                .convertToFormat(QImage::Format_RGB888);
    const QString rel = "Fotos/cuerpo/" + id + ".jpg";
    const QString ruta = QString::fromStdWString((datos_ / rel.toStdWString()).wstring());
    QDir().mkpath(QFileInfo(ruta).absolutePath());
    if (!cuadrada.save(ruta, "JPG", 88)) return "no se pudo guardar la foto en " + ruta;
    std::string err;
    nlohmann::json c{{"id", id.toStdString()}, {"foto", rel.toStdString()}};
    if (c_->guardar(c, &err).empty()) return QString::fromStdString(err);
    emit cambio();
    return {};
}

QString CuerpoViewModel::subirFoto(const QString& id, const QString& archivo) {
    if (c_->buscar(id.toStdString()).empty()) return "no hay un integrante con ese id";
    const QString ruta = archivo.startsWith("file:") ? QUrl(archivo).toLocalFile() : archivo;
    QImage img(ruta);
    if (img.isNull()) return "no se pudo leer la imagen «" + ruta + "»";
    return guardarFoto(id, img);
}

void CuerpoViewModel::capturarFoto(const QString& id) {
    const nlohmann::json i = c_->buscar(id.toStdString());
    if (i.empty()) return emit fotoLista(id, false, "no hay un integrante con ese id");
    const QString camara = QString::fromStdString(i.value("preset_ptz", nlohmann::json::object()).value("camara", std::string()));
    if (camara.isEmpty() || !g_.capturar)
        return emit fotoLista(id, false, "su banca no tiene cámara (preset PTZ en el Cuerpo): subí la foto a mano");
    // Primero la cámara a su banca; si no se puede mover (está al aire), se captura como está.
    QString aviso;
    const QString banca = QString::fromStdString(i.value("banca", std::string()));
    if (g_.apuntar && !banca.isEmpty()) aviso = g_.apuntar(banca);
    QPointer<CuerpoViewModel> yo(this);
    QTimer::singleShot(aviso.isEmpty() ? 2500 : 200, this, [yo, id, camara, aviso]() {
        if (!yo) return;
        yo->g_.capturar(camara, [yo, id, camara, aviso](QImage img) {
            if (!yo) return;
            if (img.isNull()) return emit yo->fotoLista(id, false, "la cámara «" + camara + "» no da imagen ahora");
            const QString e = yo->guardarFoto(id, img);
            if (!e.isEmpty()) return emit yo->fotoLista(id, false, e);
            emit yo->fotoLista(id, true, aviso.isEmpty() ? "foto capturada" : "capturada sin mover la cámara: " + aviso);
        });
    });
}

bool CuerpoViewModel::borrarFoto(const QString& id) {
    std::string err;
    if (c_->guardar({{"id", id.toStdString()}, {"foto", ""}}, &err).empty()) return false;
    emit cambio();
    return true;
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
    const auto ruta = datos_ / "Exportado" / "planilla-integrantes.csv";
    return Cuerpo::escribirPlantilla(ruta) ? QString::fromStdWString(ruta.wstring()) : QString();
}

QVariantMap CuerpoViewModel::recinto() const {
    nlohmann::json r = c_->recinto();
    // La foto de cada uno (la cara de su banca cuando no está conectado por la banca virtual).
    for (auto& b : r["bancas"])
        if (b.contains("integrante")) {
            const QString f = rutaFoto(QString::fromStdString(b["integrante"].value("id", std::string())));
            b["integrante"]["foto_url"] = f.isEmpty() ? std::string() : QUrl::fromLocalFile(f).toString().toStdString();
        }
    return QJsonDocument::fromJson(QByteArray::fromStdString(r.dump())).object().toVariantMap();
}

QString CuerpoViewModel::editarRecinto(const QVariantMap& cambios) {
    const std::string e = c_->setRecinto(deMapa(cambios));
    if (e.empty()) emit cambio();
    return QString::fromStdString(e);
}

QVariantMap CuerpoViewModel::buscar(const QString& clave) const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(c_->buscar(clave.toStdString()).dump())).object().toVariantMap();
}

} // namespace ba::app::asamblea
