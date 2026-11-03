// SPDX-License-Identifier: GPL-2.0-or-later
// src/tablero-web/ObjetoReplica.cpp — ver el .hpp
#include "ObjetoReplica.hpp"

#include "ClienteReplica.hpp"

#include <QDateTime>
#include <QJSEngine>
#include <QJsonDocument>
#include <QQmlEngine>

#include <private/qqmlopenmetaobject_p.h>

/// El metaobjeto dinámico: avisa al objeto cuando la vista lee o escribe una propiedad.
class MetaReplica : public QQmlOpenMetaObject {
   public:
    MetaReplica(ObjetoReplica* o) : QQmlOpenMetaObject(o), o_(o) {}

   protected:
    void propertyRead(int i) override {
        o_->leida(i);
        QQmlOpenMetaObject::propertyRead(i);
    }
    void propertyWritten(int i) override {
        QQmlOpenMetaObject::propertyWritten(i);
        o_->escrita(i);
    }

   private:
    ObjetoReplica* o_;
};

namespace {
QVariant inicial(char tipo) {
    switch (tipo) {
    case 'o':
        return QVariant::fromValue<QObject*>(nullptr);
    case 'l':
        return QVariantList{};
    case 'm':
        return QVariantMap{};
    case 's':
        return QString();
    case 'b':
        return false;
    case 'n':
        return 0;
    default:
        return {};
    }
}
const QByteArray kRev = QByteArrayLiteral("replicaRev");
} // namespace

ObjetoReplica::ObjetoReplica(ClienteReplica* cliente, const QString& id, const QJsonObject& esquema, QObject* parent)
    : QAbstractListModel(parent), cliente_(cliente), id_(id) {
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    meta_ = new MetaReplica(this);
    // Los roles primero (si es un modelo).
    const QJsonValue roles = esquema.value(QStringLiteral("roles"));
    modelo_ = roles.isArray();
    for (const QJsonValue& r : roles.toArray()) roles_.push_back(r.toString().toUtf8());
    // Las propiedades, con el valor de su tipo hasta que lleguen las de verdad.
    for (const QJsonValue& p : esquema.value(QStringLiteral("props")).toArray()) {
        const QJsonArray a = p.toArray();
        const QByteArray nombre = a.at(0).toString().toUtf8();
        const QString t = a.at(1).toString();
        const char tipo = t.isEmpty() ? 'v' : t.at(0).toLatin1();
        tipos_.insert(nombre, tipo);
        meta_->setValue(nombre, inicial(tipo));
    }
    meta_->setValue(kRev, 0);
    for (const QJsonValue& m : esquema.value(QStringLiteral("metodos")).toArray()) {
        const QJsonArray a = m.toArray();
        const QString n = a.at(0).toString();
        // Los del modelo los tiene este objeto de por sí (rowCount, index…).
        if (tipos_.contains(n.toUtf8()) || n == QLatin1String("rowCount") || n == QLatin1String("data") ||
            n == QLatin1String("index"))
            continue;
        metodos_.push_back({n, a.at(1).toBool()});
        nombresMetodos_.insert(n.toUtf8());
    }
    armarFunciones();
}

ObjetoReplica::~ObjetoReplica() = default;

void ObjetoReplica::armarFunciones() {
    if (funcionesArmadas_ || cliente_->motor() == nullptr) return;
    QJSEngine* js = cliente_->motor();
    funcionesArmadas_ = true;
    // La función de cada método: llama replicaLlamar; la que devuelve algo lee replicaRev
    // antes, así el binding que la usó se entera cuando llega la respuesta.
    static const QString fabrica = QStringLiteral(
        "(function (o, n, v) {"
        "  return v ? function () { o.replicaRev; return o.replicaLlamar(n, Array.prototype.slice.call(arguments), true); }"
        "           : function () { return o.replicaLlamar(n, Array.prototype.slice.call(arguments), false); };"
        "})");
    QJSValue f = js->evaluate(fabrica);
    const QJSValue yo = js->newQObject(this);
    for (const auto& [nombre, conValor] : std::as_const(metodos_))
        meta_->setValue(nombre.toUtf8(), QVariant::fromValue(f.call({yo, QJSValue(nombre), QJSValue(conValor)})));
}

QVariant ObjetoReplica::valor(const QString& nombre) const { return meta_->value(nombre.toUtf8()); }

int ObjetoReplica::rowCount(const QModelIndex& padre) const {
    return padre.isValid() ? 0 : static_cast<int>(filas_.size());
}

QVariant ObjetoReplica::data(const QModelIndex& i, int rol) const {
    if (!i.isValid() || i.row() >= filas_.size()) return {};
    const int k = rol - (Qt::UserRole + 1);
    if (k < 0 || k >= filas_[i.row()].size()) return {};
    return filas_[i.row()].at(k);
}

QHash<int, QByteArray> ObjetoReplica::roleNames() const {
    QHash<int, QByteArray> r;
    for (int k = 0; k < roles_.size(); ++k) r.insert(Qt::UserRole + 1 + k, roles_.at(k));
    return r;
}

void ObjetoReplica::leida(int indice) {
    if (abierto_ || pedido_) return;
    // La primera vez que la vista lee algo de este objeto: que la app mande sus valores.
    // (replicaRev y las funciones no cuentan, pero da igual: abrir de más no rompe nada.)
    Q_UNUSED(indice);
    pedido_ = true;
    cliente_->abrir(id_);
}

void ObjetoReplica::escrita(int indice) {
    const QByteArray nombre = meta_->name(indice); // relativo a las propiedades dinámicas
    if (nombre == kRev || nombresMetodos_.contains(nombre) || !tipos_.contains(nombre)) return;
    cliente_->poner(id_, QString::fromUtf8(nombre), meta_->value(nombre));
}

void ObjetoReplica::ponerValores(const QVariantMap& v) {
    abierto_ = true;
    for (auto it = v.cbegin(); it != v.cend(); ++it) meta_->setValue(it.key().toUtf8(), it.value());
    pedirOtraVez();
}

void ObjetoReplica::ponerFilas(const QVector<QVariantList>& nuevas) {
    abierto_ = true;
    // Lo mínimo que cambió: lo igual al principio y al final queda; lo del medio, si tiene
    // el mismo largo, se avisa como cambio de datos (las tarjetas no se rehacen al moverlas).
    const int n0 = static_cast<int>(filas_.size()), n1 = static_cast<int>(nuevas.size());
    int p = 0;
    while (p < n0 && p < n1 && filas_[p] == nuevas[p]) ++p;
    int q = 0;
    while (q < n0 - p && q < n1 - p && filas_[n0 - 1 - q] == nuevas[n1 - 1 - q]) ++q;
    const int medio0 = n0 - p - q, medio1 = n1 - p - q;
    // Lo del medio que está en los dos se actualiza en su lugar (la fila sigue, con datos
    // nuevos); lo que sobra se saca y lo que falta se agrega al final del medio.
    const int comun = std::min(medio0, medio1);
    for (int r = p; r < p + comun; ++r) {
        if (filas_[r] == nuevas[r]) continue;
        filas_[r] = nuevas[r];
        emit dataChanged(index(r), index(r));
    }
    if (medio0 > comun) {
        beginRemoveRows({}, p + comun, p + medio0 - 1);
        filas_.remove(p + comun, medio0 - comun);
        endRemoveRows();
    } else if (medio1 > comun) {
        beginInsertRows({}, p + comun, p + medio1 - 1);
        for (int r = comun; r < medio1; ++r) filas_.insert(p + r, nuevas[p + r]);
        endInsertRows();
    }
    pedirOtraVez();
}

void ObjetoReplica::pedirOtraVez() {
    // Lo que devuelven las funciones depende de los datos: si cambiaron, se vuelve a
    // preguntar (cada clave, como mucho dos veces por segundo).
    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    for (auto it = llamadas_.cbegin(); it != llamadas_.cend(); ++it) {
        if (ahora - pedidoMs_.value(it.key()) < 500) continue;
        pedidoMs_[it.key()] = ahora;
        cliente_->llamar(id_, it.value().first, it.value().second);
    }
}

void ObjetoReplica::resultado(const QString& clave, const QVariant& v) {
    const auto it = cache_.constFind(clave);
    if (it != cache_.constEnd() && it.value() == v) return;
    cache_.insert(clave, v);
    meta_->setValue(kRev, ++rev_);
}

QVariant ObjetoReplica::replicaLlamar(const QString& nombre, const QVariantList& args, bool conValor) {
    if (!conValor) {
        cliente_->llamar(id_, nombre, args);
        return {};
    }
    const QString clave = cliente_->claveDe(nombre, args);
    if (!llamadas_.contains(clave)) {
        // Una función que cambia de argumentos todo el tiempo (con la hora) no llena la memoria.
        if (llamadas_.size() > 200) {
            llamadas_.clear();
            pedidoMs_.clear();
        }
        llamadas_.insert(clave, {nombre, args});
        pedidoMs_.insert(clave, QDateTime::currentMSecsSinceEpoch());
        cliente_->llamar(id_, nombre, args);
    }
    return cache_.value(clave);
}
