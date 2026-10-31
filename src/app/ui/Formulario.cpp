#include "ui/Formulario.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace ba::app::formulario {

namespace {
QString tr(const char* s) { return QCoreApplication::translate("Formulario", s); }
QString etiqueta(const QVariantMap& c) {
    const QString l = c.value("label").toString();
    return l.isEmpty() ? c.value("name").toString() : l;
}
} // namespace

QVariantMap campo(const QString& name, const QString& label, const QString& kind, const QVariant& value,
                  const QString& help, const QVariantList& options) {
    QVariantMap c{{"name", name}, {"label", label.isEmpty() ? name : label}, {"kind", kind}, {"value", value}};
    if (!help.isEmpty()) c["help"] = help;
    if (!options.isEmpty()) c["options"] = options;
    return c;
}

QString kindDeTipo(const QString& tipo) {
    if (tipo == "clave") return QStringLiteral("clave");
    if (tipo == "numero") return QStringLiteral("number");
    if (tipo == "si_no") return QStringLiteral("bool");
    if (tipo == "opcion") return QStringLiteral("list");
    if (tipo == "json") return QStringLiteral("json");
    return QStringLiteral("text");
}

QVariantList opcionesDe(const QStringList& valores) {
    QVariantList out;
    for (const QString& v : valores) out << QVariantMap{{"value", v}, {"label", v}};
    return out;
}

QString convertir(const QVariantMap& c, const QVariant& crudo, QVariant* out) {
    const QString kind = c.value("kind").toString();
    const bool vacio = !crudo.isValid() || crudo.isNull() || (crudo.typeId() == QMetaType::QString && crudo.toString().trimmed().isEmpty());
    if (vacio && kind != "bool") {
        *out = QVariant();
        return c.value("obligatorio").toBool() ? tr("falta «%1»").arg(etiqueta(c)) : QString();
    }
    if (kind == "bool") {
        *out = crudo.toBool();
        return {};
    }
    if (kind == "int" || kind == "float" || kind == "number") {
        bool ok = false;
        const double d = crudo.toString().trimmed().replace(',', '.').toDouble(&ok);
        if (!ok) return tr("«%1» tiene que ser un número").arg(etiqueta(c));
        // Entero si se escribió entero (un puerto, una cantidad): así le llega al que lo lee.
        *out = kind == "int" || (d == static_cast<double>(static_cast<qint64>(d)) && !crudo.toString().contains('.'))
                   ? QVariant(static_cast<qint64>(d))
                   : QVariant(d);
        return {};
    }
    if (kind == "json") {
        if (crudo.typeId() != QMetaType::QString) {
            *out = crudo; // ya es un valor (no vino de un campo de texto)
            return {};
        }
        QJsonParseError e{};
        const QJsonDocument d = QJsonDocument::fromJson(QByteArray("[") + crudo.toString().toUtf8() + "]", &e);
        if (e.error != QJsonParseError::NoError || !d.isArray()) return tr("«%1»: el JSON no es válido").arg(etiqueta(c));
        *out = d.array().at(0).toVariant();
        return {};
    }
    if (kind == "list") {
        const QVariantList ops = c.value("options").toList();
        for (const QVariant& o : ops)
            if (o.toMap().value("value") == crudo || o.toMap().value("value").toString() == crudo.toString()) {
                *out = o.toMap().value("value");
                return {};
            }
        return tr("«%1»: «%2» no es una de las opciones").arg(etiqueta(c), crudo.toString());
    }
    *out = crudo.toString();
    return {};
}

QString convertirTodos(const QVariantList& campos, const QVariantMap& crudos, QVariantMap* out) {
    for (const QVariant& cv : campos) {
        const QVariantMap c = cv.toMap();
        const QString n = c.value("name").toString();
        if (n.isEmpty() || c.value("kind") == "header" || c.value("kind") == "info") continue;
        // Lo que no se tocó queda como estaba (una escena que ya no existe sigue elegida).
        if (!crudos.contains(n)) {
            if (c.value("obligatorio").toBool() && (!c.value("value").isValid() || c.value("value").toString().isEmpty()))
                return tr("falta «%1»").arg(etiqueta(c));
            if (c.value("value").isValid() && !c.value("value").isNull()) out->insert(n, c.value("value"));
            continue;
        }
        QVariant v;
        const QString e = convertir(c, crudos.value(n), &v);
        if (!e.isEmpty()) return e;
        if (v.isValid()) out->insert(n, v);
    }
    return {};
}

} // namespace ba::app::formulario
