// ============================================================================
// src/app/graficos/Vinculos.cpp — ver Vinculos.hpp
// ============================================================================
#include "graficos/Vinculos.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QHash>
#include <QRegularExpression>
#include <QTimer>

#include "graficos/Emisor.hpp"

namespace ba::app::graficos {
namespace {

/// Cada cuánto se mira si algo cambió. Un segundo alcanza: nadie nota que un
/// zócalo entre un segundo después de que cambió el orador, y mirar más
/// seguido sería gastar por nada.
constexpr int kRevisarMs = 1000;

/// Busca `a.b.c` adentro de un objeto. Devuelve un valor nulo si no está, que
/// es distinto de un texto vacío: uno significa «ese origen no existe» y el
/// otro «existe y está vacío».
QJsonValue porCamino(const QJsonObject& raiz, const QString& camino) {
    QJsonValue v = raiz;
    for (const QString& paso : camino.split('.')) {
        if (!v.isObject()) return {};
        const QJsonObject o = v.toObject();
        if (!o.contains(paso)) return {};
        v = o.value(paso);
    }
    return v;
}

QString aTexto(const QJsonValue& v) {
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(v.toDouble());
    if (v.isBool()) return v.toBool() ? "sí" : "no";
    return {};
}

/// Aplana un objeto anidado a `a.b: valor`, para poder listar los orígenes con
/// su valor actual sin tener que describir a mano la forma del contexto.
void aplanar(const QJsonObject& o, const QString& prefijo, QJsonObject& out) {
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QString clave = prefijo.isEmpty() ? it.key() : prefijo + "." + it.key();
        if (it.value().isObject()) aplanar(it.value().toObject(), clave, out);
        else out[clave] = it.value();
    }
}

} // namespace

struct Vinculos::Impl {
    std::filesystem::path archivo;
    Emisor& em;
    std::function<QJsonObject()> contexto;
    QJsonArray vs;
    QTimer reloj;
    /// Lo último que disparó cada vínculo. Es lo que hace que no se redispare
    /// una animación que ya está al aire diciendo exactamente lo mismo: en
    /// pantalla, eso se vería como un parpadeo cada segundo.
    QHash<QString, QString> ultimo;

    explicit Impl(std::filesystem::path a, Emisor& e) : archivo(std::move(a)), em(e) {}

    void guardar() const {
        QDir().mkpath(QString::fromStdString(archivo.parent_path().string()));
        QFile f(QString::fromStdString(archivo.string()) + ".nuevo");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
        f.write(QJsonDocument(QJsonObject{{"vinculos", vs}}).toJson());
        f.close();
        QFile::remove(QString::fromStdString(archivo.string()));
        QFile::rename(QString::fromStdString(archivo.string()) + ".nuevo",
                      QString::fromStdString(archivo.string()));
    }

    void leer() {
        QFile f(QString::fromStdString(archivo.string()));
        if (!f.open(QIODevice::ReadOnly)) return;
        vs = QJsonDocument::fromJson(f.readAll()).object().value("vinculos").toArray();
    }

    int indiceDe(const QString& id) const {
        for (int i = 0; i < vs.size(); ++i)
            if (vs[i].toObject().value("id").toString() == id) return i;
        return -1;
    }
};

Vinculos::Vinculos(std::filesystem::path archivo, Emisor& emisor, QObject* padre)
    : QObject(padre), d_(std::make_unique<Impl>(std::move(archivo), emisor)) {
    d_->leer();
    d_->reloj.setInterval(kRevisarMs);
    connect(&d_->reloj, &QTimer::timeout, this, &Vinculos::revisar);
    d_->reloj.start();
}

Vinculos::~Vinculos() = default;

void Vinculos::setContexto(std::function<QJsonObject()> fn) { d_->contexto = std::move(fn); }

QJsonObject Vinculos::origenes() const {
    QJsonObject out;
    if (d_->contexto) aplanar(d_->contexto(), {}, out);
    return out;
}

QJsonObject Vinculos::resolver(const QJsonObject& v, QString* error) const {
    static const QRegularExpression re(R"(\{([A-Za-z_][A-Za-z0-9_.]*)\})");
    const QJsonObject ctx = d_->contexto ? d_->contexto() : QJsonObject{};
    QStringList faltan;

    QJsonObject out;
    const QJsonObject valores = v.value("valores").toObject();
    for (auto it = valores.begin(); it != valores.end(); ++it) {
        const QString plantilla = it.value().toVariant().toString();
        QString texto;
        qsizetype desde = 0;
        auto m = re.globalMatch(plantilla);
        while (m.hasNext()) {
            const auto c = m.next();
            texto += plantilla.mid(desde, c.capturedStart() - desde);
            const QJsonValue origen = porCamino(ctx, c.captured(1));
            // Un origen que no existe NO se deja vacío: casi siempre es un
            // nombre mal escrito, y un zócalo sin nombre al aire es peor que
            // un zócalo que no sale.
            if (origen.isUndefined() || origen.isNull()) faltan << c.captured(1);
            else texto += aTexto(origen);
            desde = c.capturedEnd();
        }
        texto += plantilla.mid(desde);
        out[it.key()] = texto;
    }
    if (error != nullptr)
        *error = faltan.isEmpty()
                     ? QString()
                     : "no sé de dónde sacar " + faltan.join(", ") +
                           " (mirá los orígenes que hay, con rotulo_vinculos)";
    return out;
}

QJsonArray Vinculos::lista() const {
    QJsonArray out;
    for (const QJsonValue& v : d_->vs) {
        QJsonObject o = v.toObject();
        QString err;
        o["resuelve"] = resolver(o, &err);
        o["problema"] = err;
        o["fuenteExiste"] = d_->em.fuentePorNombre(o.value("fuente").toString()) != 0;
        out.append(o);
    }
    return out;
}

QString Vinculos::poner(const QJsonObject& v, QString* idOut) {
    QJsonObject o = v;
    const QString fuente = o.value("fuente").toString();
    const QString grafico = o.value("grafico").toString();
    if (fuente.isEmpty()) return "el vínculo no dice en qué fuente de escena se ve";
    if (grafico.isEmpty()) return "el vínculo no dice qué gráfico poner";
    if (d_->em.ficha(grafico).isEmpty()) return "no existe el gráfico «" + grafico + "»";
    if (!o.value("valores").isObject() || o.value("valores").toObject().isEmpty())
        return "el vínculo no dice de dónde sale ningún campo";

    // Se avisa ACÁ si un origen no existe, con el vínculo todavía sin guardar:
    // descubrirlo en vivo, cuando cambie el orador, es tarde.
    QString err;
    resolver(o, &err);
    if (!err.isEmpty()) return err;

    if (!o.contains("dispara")) o["dispara"] = true;
    if (!o.contains("activo")) o["activo"] = true;
    if (o.value("id").toString().isEmpty())
        o["id"] = "v" + QString::number(QDateTime::currentMSecsSinceEpoch(), 36);

    const int i = d_->indiceDe(o.value("id").toString());
    if (i >= 0) d_->vs[i] = o;
    else d_->vs.append(o);
    d_->guardar();
    if (idOut != nullptr) *idOut = o.value("id").toString();
    emit cambio();
    return {};
}

bool Vinculos::quitar(const QString& id) {
    const int i = d_->indiceDe(id);
    if (i < 0) return false;
    d_->vs.removeAt(i);
    d_->ultimo.remove(id);
    d_->guardar();
    emit cambio();
    return true;
}

bool Vinculos::activar(const QString& id, bool activo) {
    const int i = d_->indiceDe(id);
    if (i < 0) return false;
    QJsonObject o = d_->vs[i].toObject();
    o["activo"] = activo;
    d_->vs[i] = o;
    // Al apagarlo se olvida lo último: si se vuelve a prender, sale al aire
    // con lo que haya ahora aunque sea lo mismo de antes.
    if (!activo) d_->ultimo.remove(id);
    d_->guardar();
    emit cambio();
    return true;
}

void Vinculos::revisar() {
    if (!d_->contexto || d_->vs.isEmpty()) return;
    for (const QJsonValue& v : d_->vs) {
        const QJsonObject o = v.toObject();
        if (!o.value("activo").toBool(true) || !o.value("dispara").toBool(true)) continue;
        const QString id = o.value("id").toString();

        QString err;
        const QJsonObject valores = resolver(o, &err);
        // Un vínculo que no resuelve NO dispara y NO grita cada segundo: se
        // queda quieto. El problema se ve en la lista.
        if (!err.isEmpty()) continue;

        const QString firma =
            QString::fromUtf8(QJsonDocument(valores).toJson(QJsonDocument::Compact));
        if (d_->ultimo.value(id) == firma) continue;

        const quint64 fuente = d_->em.fuentePorNombre(o.value("fuente").toString());
        if (fuente == 0) continue; // esa fuente no está en ninguna escena ahora

        const QString e = d_->em.mostrar(o.value("grafico").toString(), valores, fuente, 0,
                                         o.value("repetir").toBool(false));
        // La firma se recuerda igual si falló: reintentar cada segundo una
        // cosa que no anda llena el registro y no arregla nada.
        d_->ultimo[id] = firma;
        emit actuo(id, e.isEmpty() ? valores.begin().value().toString() : e, !e.isEmpty());
    }
}

QString Vinculos::disparar(const QString& id) {
    const int i = d_->indiceDe(id);
    if (i < 0) return "no existe ese vínculo";
    const QJsonObject o = d_->vs[i].toObject();
    QString err;
    const QJsonObject valores = resolver(o, &err);
    if (!err.isEmpty()) return err;
    const quint64 fuente = d_->em.fuentePorNombre(o.value("fuente").toString());
    if (fuente == 0)
        return "no hay ninguna fuente de gráfico llamada «" + o.value("fuente").toString() +
               "» en las escenas";
    const QString e = d_->em.mostrar(o.value("grafico").toString(), valores, fuente, 0,
                                     o.value("repetir").toBool(false));
    if (e.isEmpty())
        d_->ultimo[id] = QString::fromUtf8(QJsonDocument(valores).toJson(QJsonDocument::Compact));
    return e;
}

} // namespace ba::app::graficos
