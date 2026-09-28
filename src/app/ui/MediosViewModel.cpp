// ============================================================================
// src/app/ui/MediosViewModel.cpp — ver MediosViewModel.hpp
// ============================================================================
#include "ui/MediosViewModel.hpp"

#include <QDir>
#include <QUrl>

#include "AppRuntime.hpp"

namespace ba::app {

MediosViewModel::MediosViewModel(AppRuntime& rt, QObject* parent) : QObject(parent), rt_(rt) {
    refrescar();
}

MediosViewModel::~MediosViewModel() = default;

QString MediosViewModel::carpetaDeMedios() const {
    return QDir(QString::fromStdWString(rt_.dataRoot().wstring())).absoluteFilePath("Media");
}

void MediosViewModel::refrescar() {
    todos_ = medios::listar(QString::fromStdWString(rt_.dataRoot().wstring()));
    recalcular();
}

void MediosViewModel::recalcular() {
    visibles_ = medios::filtrar(todos_, consulta_);
    if (!tipo_.isEmpty()) {
        QVector<medios::Archivo> out;
        for (const medios::Archivo& a : visibles_)
            if (QString::fromUtf8(medios::nombreDeTipo(a.tipo)) == tipo_) out.push_back(a);
        visibles_ = out;
    }
    emit cambio();
}

QVariantList MediosViewModel::archivos() const {
    QVariantList out;
    out.reserve(visibles_.size());
    for (const medios::Archivo& a : visibles_)
        out.append(QVariantMap{
            {"ruta", a.ruta},
            {"relativa", a.relativa},
            {"nombre", a.nombre},
            {"carpeta", a.carpeta},
            {"tipo", QString::fromUtf8(medios::nombreDeTipo(a.tipo))},
            {"bytes", a.bytes},
            {"modificadoMs", a.modificadoMs},
            // La vista previa se dibuja desde el disco: es una URL de archivo,
            // no una copia en memoria. Con doscientas imagenes, copiarlas seria
            // llenar la memoria con lo que el disco ya tiene.
            {"url", QUrl::fromLocalFile(a.ruta).toString()},
            {"desarmable", medios::sePuedeDesarmar(a.nombre)}});
    return out;
}

QString MediosViewModel::consulta() const { return consulta_; }

void MediosViewModel::setConsulta(const QString& q) {
    if (q == consulta_) return;
    consulta_ = q;
    // Sin volver al disco: la lista ya esta en memoria y filtrar es comparar
    // texto. Es lo que hace que escribir rapido no congele nada.
    recalcular();
}

QString MediosViewModel::tipo() const { return tipo_; }

void MediosViewModel::setTipo(const QString& t) {
    if (t == tipo_) return;
    tipo_ = t;
    recalcular();
}

QVariantMap MediosViewModel::cuenta() const {
    QVariantMap c{{"imagen", 0}, {"video", 0}, {"audio", 0}, {"documento", 0}};
    for (const medios::Archivo& a : todos_) {
        const QString k = QString::fromUtf8(medios::nombreDeTipo(a.tipo));
        c[k] = c.value(k).toInt() + 1;
    }
    return c;
}

int MediosViewModel::total() const { return todos_.size(); }
int MediosViewModel::mostrados() const { return visibles_.size(); }

bool MediosViewModel::sePuedeDesarmar(const QString& ruta) const {
    return medios::sePuedeDesarmar(ruta);
}

} // namespace ba::app
