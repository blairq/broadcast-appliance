// src/app/ui/ChatHilo.cpp — ver ChatHilo.hpp

#include "ui/ChatHilo.hpp"

#include <QAbstractItemModel>

namespace ba::app {

void ChatHilo::setFuente(QObject* f) {
    auto* m = qobject_cast<QAbstractItemModel*>(f);
    if (m == sourceModel()) return;
    rolPara_ = rolDe_ = -1;
    if (m != nullptr) {
        const QHash<int, QByteArray> roles = m->roleNames();
        rolPara_ = roles.key("to", -1);
        rolDe_ = roles.key("fromId", -1);
    }
    setSourceModel(m);
    emit fuenteCambio();
}

void ChatHilo::setHilo(const QString& h) {
    if (h == hilo_) return;
    hilo_ = h;
    invalidateFilter();
    emit hiloCambio();
}

bool ChatHilo::esDelHilo(const QString& hilo, const QString& para, const QString& de) {
    const bool esDeLaSala = para == "all";
    // El hilo privado con alguien son las dos direcciones: lo que le mandamos y lo que
    // contestó. Si solo se mirara `to`, sus respuestas (dirigidas al operador) caerían afuera.
    if (hilo.isEmpty()) return esDeLaSala;
    return !esDeLaSala && (para == hilo || de == hilo);
}

bool ChatHilo::filterAcceptsRow(int fila, const QModelIndex& padre) const {
    const QAbstractItemModel* m = sourceModel();
    if (m == nullptr || rolPara_ < 0) return false;
    const QModelIndex i = m->index(fila, 0, padre);
    return esDelHilo(hilo_, m->data(i, rolPara_).toString(), rolDe_ < 0 ? QString() : m->data(i, rolDe_).toString());
}

} // namespace ba::app
