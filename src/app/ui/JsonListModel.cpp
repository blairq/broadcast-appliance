#include "ui/JsonListModel.hpp"

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
    beginResetModel();
    items_ = items;
    endResetModel();
    emit countChanged();
}

void JsonListModel::append(const QJsonObject& item) {
    beginInsertRows({}, items_.size(), items_.size());
    items_.append(item);
    endInsertRows();
    emit countChanged();
}

} // namespace ba::app
