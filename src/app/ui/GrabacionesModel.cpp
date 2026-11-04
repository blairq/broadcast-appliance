// ============================================================================
// src/app/ui/GrabacionesModel.cpp — ver GrabacionesModel.hpp
// ============================================================================

#include "ui/GrabacionesModel.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <string>
#include <vector>

#include "arranque/RuntimeAire.hpp"
#include "emision/FotoMotor.hpp"
#include "emision/VentanillaMotor.hpp"

namespace ba::app {

GrabacionesModel::GrabacionesModel(VentanillaMotor& ventanilla, QObject* parent)
    : JsonListModel(QStringList{"uid", "name", "size", "modified", "path"}, parent), ventanilla_(ventanilla) {
    grababa_ = ventanilla_.foto().estado.graba;
    carpeta_ = ventanilla_.foto().sala.carpetaGrabacion;
    // Empezó o terminó una grabación: hay un archivo nuevo (o ya cerrado, con su tamaño). Se
    // abrió otro evento: otra carpeta.
    ventanilla_.escuchar(this, [this](unsigned partes) {
        const FotoMotor& f = ventanilla_.foto();
        bool releer = false;
        if ((partes & FotoMotor::Estado) != 0 && f.estado.graba != grababa_) {
            grababa_ = f.estado.graba;
            releer = true;
        }
        if ((partes & FotoMotor::Sala) != 0 && f.sala.carpetaGrabacion != carpeta_) {
            carpeta_ = f.sala.carpetaGrabacion;
            releer = true;
        }
        if (releer) refrescar();
    });
    refrescar();
}

void GrabacionesModel::refrescar() {
    const unsigned esta = ++lecturas_;
    // Dónde: la de ahora (la del evento, de la foto) y la general de la configuración (lo de
    // antes de los eventos). Los archivos, aparte.
    std::vector<std::string> carpetas{carpeta_};
    if (const auto& app = ventanilla_.foto().configuracion.app; app && app->rec.directory != carpeta_)
        carpetas.push_back(app->rec.directory);
    QPointer<GrabacionesModel> yo(this);
    QThreadPool::globalInstance()->start([yo, esta, carpetas]() {
        // Las del evento en curso y las de Recordings/, lo más nuevo primero.
        QFileInfoList files;
        for (const std::string& c : carpetas)
            files += QDir(QString::fromStdString(c))
                         .entryInfoList({"*.mkv", "*.mp4", "*.mov", "*.flv", "*.webm"}, QDir::Files, QDir::Time);
        std::sort(files.begin(), files.end(),
                  [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() > b.lastModified(); });
        QJsonArray out;
        for (const QFileInfo& f : files)
            out.append(QJsonObject{{"id", f.absoluteFilePath()},
                                   {"uid", f.absoluteFilePath()},
                                   {"name", f.fileName()},
                                   {"size", static_cast<double>(f.size())},
                                   {"modified", f.lastModified().toString("dd/MM/yyyy HH:mm")},
                                   {"path", f.absoluteFilePath()}});
        QMetaObject::invokeMethod(
            qApp,
            [yo, esta, out]() {
                if (yo && esta == yo->lecturas_) yo->setItems(out); // si no, ya se pidió otra
            },
            Qt::QueuedConnection);
    });
}

} // namespace ba::app

#include "moc_GrabacionesModel.cpp"
