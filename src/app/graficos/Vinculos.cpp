// ============================================================================
// src/app/graficos/Vinculos.cpp — ver Vinculos.hpp
// ============================================================================
#include "graficos/Vinculos.hpp"
#include "arranque/ArchivoConfigQt.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <QTimer>

#include "graficos/Emisor.hpp"
#include "i18n/I18n.hpp"

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
    // Sale al aire: en el idioma del evento. Se resuelve cada segundo, así que un
    // cambio de idioma del evento se ve en la vuelta siguiente.
    if (v.isBool())
        return QString::fromStdString(v.toBool() ? ba::i18n::traducirEvento("graficos", "sí")
                                                 : ba::i18n::traducirEvento("graficos", "no"));
    return {};
}

/// Los orígenes que salen del TIEMPO (no del contexto de quien nos crea): la hora y el
/// cronómetro del orador. Un vínculo «vivo» que los usa tiene su reloj en el hilo del
/// dibujo (Emisor::seguirReloj).
const QStringList kCamposDelTiempo{"hora", "hora_s", "orador.cronometro", "orador.restante"};

/// Los campos del tiempo, a esta hora. Salen de acá y de ningún otro lado: los usa la
/// interfaz al revisar y el reloj de los campos vivos, en otro hilo, con la foto.
/// Quietos: lo mismo siempre, para saber si cambió algo que NO es el tiempo.
void ponerTiempo(QJsonObject& ctx, bool quietos = false) {
    const QDateTime ahora = QDateTime::currentDateTime();
    // Campos vivos (T43): cambian cada segundo; en un vínculo «vivo» el gráfico se
    // actualiza sin volver a entrar.
    ctx["hora"] = quietos ? QString() : ahora.time().toString("HH:mm");
    ctx["hora_s"] = quietos ? QString() : ahora.time().toString("HH:mm:ss");
    // El cronómetro del orador que está al aire: desde cuándo habla y lo que tiene
    // asignado (lo pone la asamblea). Lo que le queda; pasado, «+m:ss».
    QJsonObject o = ctx.value("orador").toObject();
    if (!o.contains("desde_ms")) return;
    const auto mmss = [](qint64 s) { return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0')); };
    const qint64 usado = std::max<qint64>(0, (ahora.toMSecsSinceEpoch() - static_cast<qint64>(o.value("desde_ms").toDouble())) / 1000);
    const qint64 asignado = static_cast<qint64>(o.value("asignado_s").toDouble());
    o["cronometro"] = quietos ? QString() : mmss(usado);
    o["restante"] = quietos || asignado <= 0 ? QString()
                    : usado > asignado        ? "+" + mmss(usado - asignado)
                                              : mmss(asignado - usado);
    ctx["orador"] = o;
}

/// Los valores del vínculo `v` contra `ctx`. Pura (sin nada de la interfaz): la usa
/// también el reloj de los campos vivos, en el hilo del dibujo. En `faltan`, los
/// orígenes que no existen.
QJsonObject resolverCon(const QJsonObject& v, const QJsonObject& ctx, QStringList& faltan) {
    // Una por hilo: se resuelve en la interfaz y en el del dibujo.
    static thread_local const QRegularExpression re(R"(\{([A-Za-z_][A-Za-z0-9_.]*)\})");
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
            if (origen.isUndefined() || origen.isNull())
                faltan << c.captured(1);
            else
                texto += aTexto(origen);
            desde = c.capturedEnd();
        }
        texto += plantilla.mid(desde);
        out[it.key()] = texto;
    }
    return out;
}

/// El vínculo usa el tiempo en algún campo.
bool usaTiempo(const QJsonObject& v) {
    const QJsonObject valores = v.value("valores").toObject();
    for (auto it = valores.begin(); it != valores.end(); ++it)
        for (const QString& c : kCamposDelTiempo)
            if (it.value().toString().contains("{" + c + "}")) return true;
    return false;
}

/// Aplana un objeto anidado a `a.b: valor`, para poder listar los orígenes con
/// su valor actual sin tener que describir a mano la forma del contexto.
void aplanar(const QJsonObject& o, const QString& prefijo, QJsonObject& out) {
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QString clave = prefijo.isEmpty() ? it.key() : prefijo + "." + it.key();
        if (it.value().isObject())
            aplanar(it.value().toObject(), clave, out);
        else
            out[clave] = it.value();
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
        // Ruta vacia = los vinculos no se leen ni se escriben (los apago el
        // diario de arranque). No se pisa el archivo: hay que poder mirarlo.
        if (archivo.empty()) return;
        config::guardarQ(archivo, QJsonObject{{"vinculos", vs}});
    }

    void leer() {
        if (archivo.empty()) return;
        vs = config::leerQObjeto(archivo).value("vinculos").toArray();
    }

    /// El contexto de quien nos crea, con el tiempo de ahora.
    QJsonObject contextoAhora() const {
        QJsonObject ctx = contexto ? contexto() : QJsonObject{};
        ponerTiempo(ctx);
        return ctx;
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
    if (d_->contexto) aplanar(d_->contextoAhora(), {}, out);
    return out;
}

QJsonObject Vinculos::resolver(const QJsonObject& v, QString* error) const {
    QStringList faltan;
    const QJsonObject out = resolverCon(v, d_->contextoAhora(), faltan);
    if (error != nullptr)
        *error = faltan.isEmpty() ? QString()
                                  : tr("no sé de dónde sacar %1 (mirá los orígenes que hay, con rotulo_vinculos)")
                                        .arg(faltan.join(", "));
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
    if (fuente.isEmpty()) return tr("el vínculo no dice en qué fuente de escena se ve");
    if (grafico.isEmpty()) return tr("el vínculo no dice qué gráfico poner");
    if (d_->em.ficha(grafico).isEmpty()) return tr("no existe el gráfico «%1»").arg(grafico);
    if (!o.value("valores").isObject() || o.value("valores").toObject().isEmpty())
        return tr("el vínculo no dice de dónde sale ningún campo");

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
    if (i >= 0)
        d_->vs[i] = o;
    else
        d_->vs.append(o);
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

        const QJsonObject ctx = d_->contextoAhora();
        QStringList faltan;
        const QJsonObject valores = resolverCon(o, ctx, faltan);
        // Un vínculo que no resuelve NO dispara y NO grita cada segundo: se
        // queda quieto. El problema se ve en la lista.
        if (!faltan.isEmpty()) continue;

        // Un vínculo «vivo» con el tiempo (la hora, el cronómetro): eso lo sigue el reloj
        // del hilo del dibujo (ver abajo). Acá cuenta solo si cambió lo demás (el orador,
        // el bloque), o si
        // ese reloj ya no anda (otro gráfico tomó la fuente, lo ocultaron).
        const bool conReloj = o.value("vivo").toBool(false) && usaTiempo(o);
        QJsonObject quieto = ctx;
        if (conReloj) ponerTiempo(quieto, true);
        const QString firma = QString::fromUtf8(
            QJsonDocument(conReloj ? resolverCon(o, quieto, faltan) : valores).toJson(QJsonDocument::Compact));

        const quint64 fuente = d_->em.fuentePorNombre(o.value("fuente").toString());
        if (d_->ultimo.value(id) == firma && (!conReloj || fuente == 0 || d_->em.siguiendoReloj(fuente))) continue;
        if (fuente == 0) continue; // esa fuente no está en ninguna escena ahora

        // Campos vivos (T43): un vínculo «vivo» con su gráfico ya al aire en esa fuente
        // solo se actualiza (el cuadro estable, sin volver a entrar).
        const QString grafico = o.value("grafico").toString();
        const bool vivo = o.value("vivo").toBool(false) && d_->ultimo.contains(id) &&
                          d_->em.ultimoGrafico(fuente) == grafico;
        const QString e = vivo ? d_->em.actualizar(grafico, valores, fuente)
                               : d_->em.mostrar(grafico, valores, fuente, 0, o.value("repetir").toBool(false));
        // El tiempo, desde ahora, en el hilo del dibujo: con una FOTO del contexto (lo de la
        // interfaz no se lee desde allá) y la hora de cada vuelta.
        if (conReloj && e.isEmpty())
            d_->em.seguirReloj(grafico, fuente, valores, [o, foto = ctx]() {
                QJsonObject c = foto;
                ponerTiempo(c);
                QStringList f;
                const QJsonObject v = resolverCon(o, c, f);
                return f.isEmpty() ? v : QJsonObject{};
            });
        // La firma se recuerda igual si falló: reintentar cada segundo una
        // cosa que no anda llena el registro y no arregla nada.
        d_->ultimo[id] = firma;
        emit actuo(id, e.isEmpty() ? valores.begin().value().toString() : e, !e.isEmpty());
    }
}

QString Vinculos::disparar(const QString& id) {
    const int i = d_->indiceDe(id);
    if (i < 0) return tr("no existe ese vínculo");
    const QJsonObject o = d_->vs[i].toObject();
    QString err;
    const QJsonObject valores = resolver(o, &err);
    if (!err.isEmpty()) return err;
    const quint64 fuente = d_->em.fuentePorNombre(o.value("fuente").toString());
    if (fuente == 0)
        return tr("no hay ninguna fuente de gráfico llamada «%1» en las escenas").arg(o.value("fuente").toString());
    const QString e =
        d_->em.mostrar(o.value("grafico").toString(), valores, fuente, 0, o.value("repetir").toBool(false));
    if (e.isEmpty())
        d_->ultimo[id] = QString::fromUtf8(QJsonDocument(valores).toJson(QJsonDocument::Compact));
    return e;
}

} // namespace ba::app::graficos
