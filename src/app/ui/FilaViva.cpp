// src/app/ui/FilaViva.cpp — ver el .hpp
#include "ui/FilaViva.hpp"

namespace ba::app {

void FilaViva::setModelo(QObject* m) {
    auto* mod = qobject_cast<QAbstractItemModel*>(m);
    if (mod == modelo_) return;
    if (modelo_) disconnect(modelo_, nullptr, this, nullptr);
    modelo_ = mod;
    if (mod != nullptr) {
        connect(mod, &QAbstractItemModel::dataChanged, this, &FilaViva::alCambiarDatos);
        connect(mod, &QAbstractItemModel::modelReset, this, &FilaViva::leer);
        connect(mod, &QAbstractItemModel::layoutChanged, this, &FilaViva::leer);
        connect(mod, &QAbstractItemModel::rowsInserted, this, &FilaViva::leer);
        connect(mod, &QAbstractItemModel::rowsRemoved, this, &FilaViva::leer);
        connect(mod, &QAbstractItemModel::rowsMoved, this, &FilaViva::leer);
    }
    emit entradaCambio();
    leer();
}

void FilaViva::setFila(int f) {
    if (f == fila_) return;
    fila_ = f;
    emit entradaCambio();
    leer();
}

void FilaViva::alCambiarDatos(const QModelIndex& desde, const QModelIndex& hasta) {
    if (fila_ >= desde.row() && fila_ <= hasta.row()) leer();
}

void FilaViva::leer() {
    QVariantMap d;
    if (modelo_ && fila_ >= 0 && fila_ < modelo_->rowCount()) {
        const QModelIndex i = modelo_->index(fila_, 0);
        const QHash<int, QByteArray> roles = modelo_->roleNames();
        for (auto it = roles.begin(); it != roles.end(); ++it) d.insert(QString::fromUtf8(it.value()), modelo_->data(i, it.key()));
    }
    if (d == datos_) return;
    datos_ = d;
    emit datosCambio();
}

} // namespace ba::app

#include "moc_FilaViva.cpp"
