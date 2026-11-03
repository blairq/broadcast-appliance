#include "ui/JsonListModel.hpp"

#include <QSet>

#include <utility>

namespace ba::app {

JsonListModel::JsonListModel(QStringList keys, QObject* parent)
    : QAbstractListModel(parent), keys_(std::move(keys)) {}

QVariant JsonListModel::data(const QModelIndex& index, int role) const {
    const int k = role - Qt::UserRole - 1;
    if (!index.isValid() || index.row() >= items_.size() || k < 0 || k >= keys_.size()) return {};
    return items_.at(index.row()).toObject().value(keys_.at(k)).toVariant();
}

QHash<int, QByteArray> JsonListModel::roleNames() const {
    QHash<int, QByteArray> r;
    for (int i = 0; i < keys_.size(); ++i)
        r.insert(Qt::UserRole + 1 + i, keys_.at(i).toUtf8());
    return r;
}

QVariantMap JsonListModel::fila(int i) const {
    if (i < 0 || i >= items_.size()) return {};
    const QJsonObject o = items_.at(i).toObject();
    QVariantMap out;
    for (const QString& k : keys_)
        out.insert(k == "id" ? QStringLiteral("uid") : k, o.value(k).toVariant());
    return out;
}

namespace {
/// Los roles (Qt::UserRole + 1 + k) cuyas claves difieren entre dos filas.
QList<int> rolesDistintos(const QJsonObject& a, const QJsonObject& b, const QStringList& claves) {
    QList<int> roles;
    for (int k = 0; k < claves.size(); ++k)
        if (a.value(claves.at(k)) != b.value(claves.at(k))) roles << Qt::UserRole + 1 + k;
    return roles;
}
} // namespace

PlanFilas JsonListModel::planear(const QJsonArray& antes, const QJsonArray& ahora, const QStringList& claves) {
    PlanFilas p;
    p.antes = static_cast<int>(antes.size());
    p.final = ahora;
    const auto cambiar = [&](int i, const QJsonObject& a, const QJsonObject& b) {
        if (a == b) return;
        // Cada fila avisa solo los roles que le cambiaron: lo que no es un rol no despierta a
        // nadie, pero se guarda (sin aviso).
        p.pasos.push_back({PlanFilas::Tipo::Cambiar, i, 0, idDe(b), b, rolesDistintos(a, b, claves)});
    };
    bool mismosIds = antes.size() == ahora.size();
    for (int i = 0; mismosIds && i < ahora.size(); ++i) mismosIds = idDe(antes.at(i)) == idDe(ahora.at(i));
    if (mismosIds) {
        for (int i = 0; i < ahora.size(); ++i) cambiar(i, antes.at(i).toObject(), ahora.at(i).toObject());
        return p;
    }
    // Cambiaron los ids (entró o se fue alguien, otro orden). Con ids únicos se quitan,
    // insertan y mueven SOLO esas filas: rehacer el modelo entero hacía que la vista
    // destruyera y volviera a crear todos los delegados (con 60 invitados en la sala, 60
    // VideoItem por cada uno que entraba, en el hilo de la interfaz).
    if (!idsUnicos(antes) || !idsUnicos(ahora)) {
        p.rehacer = true;
        return p;
    }
    QJsonArray filas = antes; // se simula sobre una copia (compartida hasta que se toca)
    QSet<QString> quedan;
    for (const QJsonValue& v : ahora) quedan.insert(idDe(v));
    for (int i = static_cast<int>(filas.size()) - 1; i >= 0; --i) {
        const QString id = idDe(filas.at(i));
        if (quedan.contains(id)) continue;
        p.pasos.push_back({PlanFilas::Tipo::Quitar, i, 0, id, {}, {}});
        filas.removeAt(i);
    }
    for (int i = 0; i < ahora.size(); ++i) {
        const QString id = idDe(ahora.at(i));
        int j = -1;
        for (int k = i; k < filas.size(); ++k)
            if (idDe(filas.at(k)) == id) {
                j = k;
                break;
            }
        if (j < 0) {
            p.pasos.push_back({PlanFilas::Tipo::Insertar, i, 0, id, ahora.at(i).toObject(), {}});
            filas.insert(i, ahora.at(i));
            continue;
        }
        if (j != i) {
            p.pasos.push_back({PlanFilas::Tipo::Mover, i, j, id, {}, {}});
            const QJsonValue v = filas.takeAt(j);
            filas.insert(i, v);
        }
        cambiar(i, filas.at(i).toObject(), ahora.at(i).toObject());
        filas.replace(i, ahora.at(i));
    }
    return p;
}

bool JsonListModel::aplicarPasos(const PlanFilas& plan) {
    if (plan.rehacer) {
        reiniciar(plan.final);
        return true;
    }
    const qsizetype n0 = items_.size();
    const auto fallar = [&]() {
        if (items_.size() != n0) emit countChanged();
        return false;
    };
    for (const PlanFilas::Paso& x : plan.pasos) {
        const int n = static_cast<int>(items_.size());
        switch (x.tipo) {
            case PlanFilas::Tipo::Quitar:
                if (x.i < 0 || x.i >= n || idDe(items_.at(x.i)) != x.id) return fallar();
                beginRemoveRows({}, x.i, x.i);
                items_.removeAt(x.i);
                endRemoveRows();
                break;
            case PlanFilas::Tipo::Insertar:
                if (x.i < 0 || x.i > n) return fallar();
                beginInsertRows({}, x.i, x.i);
                items_.insert(x.i, x.valor);
                endInsertRows();
                break;
            case PlanFilas::Tipo::Mover: {
                if (x.i < 0 || x.desde <= x.i || x.desde >= n || idDe(items_.at(x.desde)) != x.id) return fallar();
                beginMoveRows({}, x.desde, x.desde, {}, x.i);
                const QJsonValue v = items_.takeAt(x.desde);
                items_.insert(x.i, v);
                endMoveRows();
                break;
            }
            case PlanFilas::Tipo::Cambiar:
                if (x.i < 0 || x.i >= n || idDe(items_.at(x.i)) != x.id) return fallar();
                items_.replace(x.i, x.valor);
                if (!x.roles.isEmpty()) emit dataChanged(index(x.i), index(x.i), x.roles);
                break;
        }
    }
    if (items_.size() != n0) emit countChanged();
    return true;
}

void JsonListModel::reiniciar(const QJsonArray& items) {
    beginResetModel();
    items_ = items;
    endResetModel();
    emit countChanged();
}

void JsonListModel::setItems(const QJsonArray& items) {
    // El plan sobre la lista de ahora siempre calza; si algo no calzara, se rehace.
    if (!aplicarPasos(planear(items_, items, keys_))) reiniciar(items);
}

void JsonListModel::aplicar(const PlanFilas& plan) {
    if (plan.antes == items_.size() && aplicarPasos(plan)) return;
    setItems(plan.final);
}

void JsonListModel::ponerFila(int i, const QJsonObject& item) {
    if (i < 0 || i >= items_.size()) return;
    const QJsonObject antes = items_.at(i).toObject();
    if (antes == item) return;
    items_.replace(i, item);
    if (const QList<int> roles = rolesDistintos(antes, item, keys_); !roles.isEmpty()) emit dataChanged(index(i), index(i), roles);
}

QString JsonListModel::idDe(const QJsonValue& fila) {
    const QJsonValue id = fila.toObject().value("id");
    return id.isString() ? id.toString() : id.isDouble() ? QString::number(id.toDouble(), 'g', 17) : QString();
}

bool JsonListModel::idsUnicos(const QJsonArray& filas) {
    QSet<QString> vistos;
    for (const QJsonValue& v : filas) {
        const QString id = idDe(v);
        if (id.isEmpty() || vistos.contains(id)) return false;
        vistos.insert(id);
    }
    return true;
}

void JsonListModel::append(const QJsonObject& item) {
    beginInsertRows({}, items_.size(), items_.size());
    items_.append(item);
    endInsertRows();
    emit countChanged();
}

} // namespace ba::app
