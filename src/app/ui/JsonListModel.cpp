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

void JsonListModel::setItems(const QJsonArray& items) {
    bool sameIds = items.size() == items_.size();
    for (int i = 0; sameIds && i < items.size(); ++i) {
        sameIds = items.at(i).toObject().value("id") == items_.at(i).toObject().value("id");
    }
    if (sameIds) {
        if (items == items_) return;
        // Cada fila avisa solo los roles que le cambiaron: lo que no es un rol (la red de un
        // invitado, que cambia en cada foto) no despierta a nadie, y una fila quieta tampoco.
        const QJsonArray antes = std::exchange(items_, items);
        for (int i = 0; i < items_.size(); ++i) {
            const QJsonObject a = antes.at(i).toObject();
            const QJsonObject b = items_.at(i).toObject();
            QList<int> roles;
            for (int k = 0; k < keys_.size(); ++k)
                if (a.value(keys_.at(k)) != b.value(keys_.at(k))) roles << Qt::UserRole + 1 + k;
            if (!roles.isEmpty()) emit dataChanged(index(i), index(i), roles);
        }
        return;
    }
    // Cambiaron los ids (entró o se fue alguien, otro orden). Con ids únicos se quitan,
    // insertan y mueven SOLO esas filas: antes se rehacía el modelo entero y la vista
    // destruía y volvía a crear todos los delegados (con 60 invitados en la sala, 60
    // VideoItem por cada uno que entraba, en el hilo de la interfaz).
    if (!idsUnicos(items_) || !idsUnicos(items)) {
        beginResetModel();
        items_ = items;
        endResetModel();
        emit countChanged();
        return;
    }
    const int antes = items_.size();
    QSet<QString> quedan;
    for (const QJsonValue& v : items) quedan.insert(idDe(v));
    for (int i = items_.size() - 1; i >= 0; --i) {
        if (quedan.contains(idDe(items_.at(i)))) continue;
        beginRemoveRows({}, i, i);
        items_.removeAt(i);
        endRemoveRows();
    }
    for (int i = 0; i < items.size(); ++i) {
        const QString id = idDe(items.at(i));
        int j = -1;
        for (int k = i; k < items_.size(); ++k)
            if (idDe(items_.at(k)) == id) {
                j = k;
                break;
            }
        if (j < 0) {
            beginInsertRows({}, i, i);
            items_.insert(i, items.at(i));
            endInsertRows();
            continue;
        }
        if (j != i) {
            beginMoveRows({}, j, j, {}, i);
            const QJsonValue v = items_.takeAt(j);
            items_.insert(i, v);
            endMoveRows();
        }
        const QJsonObject a = items_.at(i).toObject();
        const QJsonObject b = items.at(i).toObject();
        items_.replace(i, b);
        QList<int> roles;
        for (int k = 0; k < keys_.size(); ++k)
            if (a.value(keys_.at(k)) != b.value(keys_.at(k))) roles << Qt::UserRole + 1 + k;
        if (!roles.isEmpty()) emit dataChanged(index(i), index(i), roles);
    }
    if (items_.size() != antes) emit countChanged();
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
