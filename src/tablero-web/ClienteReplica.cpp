// SPDX-License-Identifier: GPL-2.0-or-later
// src/tablero-web/ClienteReplica.cpp — ver el .hpp
#include "ClienteReplica.hpp"

#include "ObjetoReplica.hpp"

#include <QDateTime>
#include <QJSValue>
#include <QJsonDocument>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

ClienteReplica::ClienteReplica(QObject* parent) : QObject(parent) {
    reloj_.setSingleShot(true);
    reloj_.setInterval(kLoteMs);
    connect(&reloj_, &QTimer::timeout, this, &ClienteReplica::vaciar);
    relojVideo_.setSingleShot(true);
    relojVideo_.setInterval(100);
    connect(&relojVideo_, &QTimer::timeout, this, &ClienteReplica::mandarVideo);
}

ClienteReplica::~ClienteReplica() = default;

void ClienteReplica::setMotor(QJSEngine* motor) {
    motor_ = motor;
    for (ObjetoReplica* o : std::as_const(objs_)) o->armarFunciones();
}

ObjetoReplica* ClienteReplica::raiz() const { return objs_.value(raiz_); }

void ClienteReplica::hola() {
    QJsonObject m{{QStringLiteral("type"), QStringLiteral("replica")}, {QStringLiteral("op"), QStringLiteral("hola")}};
    if (enviar) enviar(QJsonDocument(m).toJson(QJsonDocument::Compact));
}

void ClienteReplica::recibirTexto(const QByteArray& texto) {
    const QJsonDocument d = QJsonDocument::fromJson(texto);
    if (d.isObject()) recibir(d.object());
}

void ClienteReplica::recibir(const QJsonObject& m) {
    if (m.value(QStringLiteral("op")).toString() == QLatin1String("lote")) {
        const bool antes = listo();
        for (const QJsonValue& x : m.value(QStringLiteral("m")).toArray()) procesar(x.toObject());
        // La raíz con sus valores (vienen en el mismo lote): ya se puede cargar la vista.
        if (!antes && raiz() != nullptr && raiz()->abierto()) {
            listo_ = true;
            emit listoChanged();
        }
        return;
    }
    procesar(m);
}

ObjetoReplica* ClienteReplica::armar(const QJsonObject& esquema) {
    const QString id = esquema.value(QStringLiteral("o")).toString();
    if (id.isEmpty()) return nullptr;
    if (ObjetoReplica* o = objs_.value(id)) return o;
    auto* o = new ObjetoReplica(this, id, esquema, this);
    objs_.insert(id, o);
    return o;
}

void ClienteReplica::procesar(const QJsonObject& m) {
    const QString op = m.value(QStringLiteral("op")).toString();
    const QString id = m.value(QStringLiteral("o")).toString();
    if (op == QLatin1String("esquema")) {
        armar(m);
    } else if (op == QLatin1String("raiz")) {
        raiz_ = id;
        escribe_ = m.value(QStringLiteral("escribe")).toBool();
    } else if (op == QLatin1String("valores")) {
        if (ObjetoReplica* o = objs_.value(id)) o->ponerValores(aVariant(m.value(QStringLiteral("v"))).toMap());
    } else if (op == QLatin1String("filas")) {
        ObjetoReplica* o = objs_.value(id);
        if (o == nullptr) return;
        QVector<QVariantList> filas;
        for (const QJsonValue& f : m.value(QStringLiteral("filas")).toArray()) filas.push_back(aVariant(f).toList());
        o->ponerFilas(filas);
    } else if (op == QLatin1String("resultado")) {
        enviados_.remove(m.value(QStringLiteral("id")).toInt()); // ya se hizo (o no): no hay qué confirmar
        const auto it = pendientes_.find(m.value(QStringLiteral("id")).toInt());
        if (it == pendientes_.end()) return;
        const auto [objeto, clave] = it.value();
        pendientes_.erase(it);
        if (!m.value(QStringLiteral("ok")).toBool()) {
            const QString e = m.value(QStringLiteral("error")).toString();
            qWarning("tablero web: %s: %s", qPrintable(clave), qPrintable(e));
            // Un botón que no se hizo lo dice (lo que devuelve un valor, no: se pide seguido).
            if (clave.isEmpty()) emit aviso(e, QStringLiteral("error"));
            return;
        }
        if (ObjetoReplica* o = objs_.value(objeto); o && !clave.isEmpty())
            o->resultado(clave, aVariant(m.value(QStringLiteral("v"))));
    } else if (op == QLatin1String("error")) {
        error_ = m.value(QStringLiteral("error")).toString();
        emit errorChanged();
    } else if (op == QLatin1String("confirmar")) {
        emit confirmar(m.value(QStringLiteral("id")).toInt(), m.value(QStringLiteral("texto")).toString());
    } else if (op == QLatin1String("aviso")) {
        emit aviso(m.value(QStringLiteral("texto")).toString(), m.value(QStringLiteral("nivel")).toString());
    } else if (op == QLatin1String("webrtc")) {
        emit enlaceWebrtc(m.value(QStringLiteral("url")).toString(), m.value(QStringLiteral("error")).toString());
    } else if (op == QLatin1String("cuadro")) {
        const QByteArray jpg = QByteArray::fromBase64(m.value(QStringLiteral("jpg")).toString().toLatin1());
        QImage img;
        if (!img.loadFromData(jpg, "JPG")) return;
        emit cuadro(m.value(QStringLiteral("clave")).toString(), img,
                    static_cast<qint64>(m.value(QStringLiteral("t")).toDouble()),
                    static_cast<quint64>(m.value(QStringLiteral("s")).toDouble()));
    }
}

QVariant ClienteReplica::aVariant(const QJsonValue& v) const {
    switch (v.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        return {};
    case QJsonValue::Bool:
        return v.toBool();
    case QJsonValue::Double: {
        const double d = v.toDouble();
        if (d == static_cast<double>(static_cast<int>(d))) return static_cast<int>(d);
        return d;
    }
    case QJsonValue::String:
        return v.toString();
    case QJsonValue::Array: {
        QVariantList l;
        for (const QJsonValue& x : v.toArray()) l.push_back(aVariant(x));
        return l;
    }
    case QJsonValue::Object: {
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("$o"))) return QVariant::fromValue<QObject*>(objs_.value(o.value(QStringLiteral("$o")).toString()));
        if (o.contains(QStringLiteral("$t"))) {
            const QString t = o.value(QStringLiteral("$t")).toString();
            const auto n = [&](const char* k) { return o.value(QLatin1String(k)).toDouble(); };
            if (t == QLatin1String("tam")) return QSizeF(n("w"), n("h"));
            if (t == QLatin1String("punto")) return QPointF(n("x"), n("y"));
            if (t == QLatin1String("rect")) return QRectF(n("x"), n("y"), n("w"), n("h"));
            if (t == QLatin1String("fecha")) return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(n("ms")));
        }
        QVariantMap m;
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) m.insert(it.key(), aVariant(it.value()));
        return m;
    }
    }
    return {};
}

QJsonValue ClienteReplica::aJson(const QVariant& vOriginal) const {
    QVariant v = vOriginal;
    if (v.metaType() == QMetaType::fromType<QJSValue>()) v = v.value<QJSValue>().toVariant();
    if (v.metaType().flags() & QMetaType::PointerToQObject) {
        auto* o = qobject_cast<ObjetoReplica*>(v.value<QObject*>());
        return o ? QJsonValue(QJsonObject{{QStringLiteral("$o"), o->id()}}) : QJsonValue();
    }
    switch (v.metaType().id()) {
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        QJsonArray a;
        for (const QVariant& x : v.toList()) a.push_back(aJson(x));
        return a;
    }
    case QMetaType::QVariantMap: {
        QJsonObject o;
        const QVariantMap m = v.toMap();
        for (auto it = m.cbegin(); it != m.cend(); ++it) o.insert(it.key(), aJson(it.value()));
        return o;
    }
    default:
        return QJsonValue::fromVariant(v);
    }
}

void ClienteReplica::encolar(const QJsonObject& m) {
    cola_.push_back(m);
    if (!reloj_.isActive()) reloj_.start();
}

void ClienteReplica::vaciar() {
    reloj_.stop();
    if (cola_.isEmpty()) return;
    QJsonObject lote{{QStringLiteral("type"), QStringLiteral("replica")},
                     {QStringLiteral("op"), QStringLiteral("lote")},
                     {QStringLiteral("m"), cola_}};
    cola_ = QJsonArray();
    if (enviar) enviar(QJsonDocument(lote).toJson(QJsonDocument::Compact));
}

void ClienteReplica::abrir(const QString& id) {
    encolar({{QStringLiteral("op"), QStringLiteral("abrir")}, {QStringLiteral("o"), id}});
}

QString ClienteReplica::claveDe(const QString& metodo, const QVariantList& args) const {
    return metodo + QLatin1Char('(') + QString::fromUtf8(QJsonDocument(aJson(args).toArray()).toJson(QJsonDocument::Compact)) +
           QLatin1Char(')');
}

int ClienteReplica::llamar(const QString& id, const QString& metodo, const QVariantList& args, bool conValor) {
    const int n = siguienteId_++;
    pendientes_.insert(n, {id, conValor ? claveDe(metodo, args) : QString()});
    // Las que no vuelven nunca (se cortó) no se acumulan.
    if (pendientes_.size() > 2000) pendientes_.clear();
    const QJsonObject m{{QStringLiteral("op"), QStringLiteral("llamar")},
                        {QStringLiteral("id"), n},
                        {QStringLiteral("o"), id},
                        {QStringLiteral("m"), metodo},
                        {QStringLiteral("a"), aJson(args)}};
    if (!conValor) {
        if (enviados_.size() > 200) enviados_.clear();
        enviados_.insert(n, m);
    }
    encolar(m);
    return n;
}

void ClienteReplica::poner(const QString& id, const QString& prop, const QVariant& v) {
    const int n = siguienteId_++;
    const QJsonObject m{{QStringLiteral("op"), QStringLiteral("poner")},
                        {QStringLiteral("id"), n},
                        {QStringLiteral("o"), id},
                        {QStringLiteral("p"), prop},
                        {QStringLiteral("v"), aJson(v)}};
    if (enviados_.size() > 200) enviados_.clear();
    enviados_.insert(n, m);
    encolar(m);
}

void ClienteReplica::confirmarLlamada(int id) {
    QJsonObject m = enviados_.take(id);
    if (m.isEmpty()) return;
    m.insert(QStringLiteral("confirmado"), true);
    if (m.value(QStringLiteral("op")).toString() == QLatin1String("llamar"))
        pendientes_.insert(id, {m.value(QStringLiteral("o")).toString(), QString()});
    encolar(m);
}

void ClienteReplica::pedirVideo(QObject* quien, const QString& clave, int ancho, int fps) {
    PedidoVideo p{clave, ancho, fps};
    const auto it = video_.constFind(quien);
    if (it != video_.constEnd() && it->clave == clave && it->ancho == ancho && it->fps == fps) return;
    video_.insert(quien, p);
    if (!relojVideo_.isActive()) relojVideo_.start();
}

void ClienteReplica::soltarVideo(QObject* quien) {
    if (video_.remove(quien) && !relojVideo_.isActive()) relojVideo_.start();
}

void ClienteReplica::pedirWebrtc() { encolar({{QStringLiteral("op"), QStringLiteral("webrtc")}}); }

void ClienteReplica::mandarVideo() {
    // Por clave, el ancho más grande y los cuadros por segundo más altos.
    QHash<QString, QPair<int, int>> porClave;
    for (const PedidoVideo& p : std::as_const(video_)) {
        if (p.clave.isEmpty()) continue;
        auto& x = porClave[p.clave];
        x.first = std::max(x.first, p.ancho);
        x.second = std::max(x.second, p.fps);
    }
    QJsonArray claves;
    QStringList ordenadas = porClave.keys();
    ordenadas.sort();
    for (const QString& k : ordenadas)
        claves.push_back(QJsonObject{{QStringLiteral("clave"), k},
                                     {QStringLiteral("ancho"), porClave[k].first},
                                     {QStringLiteral("fps"), porClave[k].second}});
    const QString texto = QString::fromUtf8(QJsonDocument(claves).toJson(QJsonDocument::Compact));
    if (texto == ultimoVideo_) return;
    ultimoVideo_ = texto;
    encolar({{QStringLiteral("op"), QStringLiteral("video")}, {QStringLiteral("claves"), claves}});
}
