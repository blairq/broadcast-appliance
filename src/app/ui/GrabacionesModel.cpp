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

#include "arranque/AppRuntime.hpp"
#include "emision/FotoMotor.hpp"
#include "emision/VentanillaMotor.hpp"

namespace ba::app {

GrabacionesModel::GrabacionesModel(AppRuntime& rt, VentanillaMotor& ventanilla, QObject* parent)
    : JsonListModel(QStringList{"uid", "name", "size", "modified", "path"}, parent), rt_(rt), ventanilla_(ventanilla) {
    grababa_ = ventanilla_.foto().estado.graba;
    // Empezó o terminó una grabación: hay un archivo nuevo (o ya cerrado, con su tamaño).
    connect(&ventanilla_, &VentanillaMotor::cambio, this, [this](unsigned partes) {
        if ((partes & FotoMotor::Estado) == 0) return;
        const bool graba = ventanilla_.foto().estado.graba;
        if (graba == grababa_) return;
        grababa_ = graba;
        refrescar();
    });
    refrescar();
}

QString GrabacionesModel::carpeta() const { return QString::fromStdString(rt_.carpetaGrabacion()); }

void GrabacionesModel::refrescar() {
    const unsigned esta = ++lecturas_;
    // Las carpetas son de la copia de los ajustes de aire (se leen desde la interfaz); los
    // archivos, aparte.
    const std::vector<std::string> carpetas = rt_.carpetasDeGrabaciones();
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
