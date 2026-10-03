// src/app/agent/FlujosTexto.cpp — ver FlujosTexto.hpp

#include "agent/FlujosTexto.hpp"

#include <obs.h>

#include <QDir>
#include <QFile>
#include <QTextStream>

namespace ba::app::agent {

namespace {

constexpr std::size_t kTope = 2000;

/// 00:01:02,345 (formato SRT).
QString tiempoSrt(qint64 ms) {
    if (ms < 0) ms = 0;
    return QStringLiteral("%1:%2:%3,%4")
        .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg((ms / 60000) % 60, 2, 10, QLatin1Char('0'))
        .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

/// Un nombre de salida apto para archivo.
QString seguro(QString s) {
    static const QString prohibidos = QStringLiteral("/\\:*?\"<>|");
    for (QChar& c : s)
        if (prohibidos.contains(c) || c.unicode() < 32) c = '_';
    return s.trimmed().isEmpty() ? QStringLiteral("texto") : s.trimmed();
}

} // namespace

/// Los archivos abiertos de una salida (de esta sesión).
struct FlujosTexto::Archivo {
    QFile txt, srt;
    QDateTime inicio;
    int indice = 0;
};

FlujosTexto::FlujosTexto(std::filesystem::path dataRoot, QObject* parent)
    : QObject(parent), root_(std::move(dataRoot)) {}

FlujosTexto::~FlujosTexto() = default;

QString FlujosTexto::carpeta() const {
    return QString::fromStdString((root_ / "Recordings" / "Transcripciones").string());
}

void FlujosTexto::publicar(const QString& flujo, const QString& texto, const QString& origen, double segundos) {
    const QString t = texto.trimmed();
    if (flujo.isEmpty() || t.isEmpty()) return;
    Frase f;
    f.seq = ++seq_;
    f.flujo = flujo;
    f.texto = t;
    f.origen = origen;
    f.segundos = segundos;
    f.cuando = QDateTime::currentDateTime();
    f.hora = f.cuando.toString("HH:mm:ss");
    frases_.push_back(f);
    while (frases_.size() > kTope) frases_.pop_front();

    const nlohmann::json conf = config_ ? config_(flujo) : nlohmann::json();
    const nlohmann::json salidas = conf.is_object() ? conf.value("salidas", nlohmann::json::object()) : nlohmann::json::object();
    if (salidas.value("archivo", false)) escribirArchivo(f);
    const std::string rotulo = salidas.value("rotulo", std::string());
    if (!rotulo.empty()) ponerRotulo(QString::fromStdString(rotulo), t);

    emit frase(flujo, t);
    emit cambio();
}

void FlujosTexto::escribirArchivo(const Frase& f) {
    auto& a = archivos_[f.flujo];
    if (!a) {
        a = std::make_unique<Archivo>();
        a->inicio = f.cuando.addMSecs(-static_cast<qint64>(f.segundos * 1000));
        QDir().mkpath(carpeta());
        const QString base = carpeta() + "/" + seguro(f.flujo) + "-" + a->inicio.toString("yyyyMMdd-HHmmss");
        a->txt.setFileName(base + ".txt");
        a->srt.setFileName(base + ".srt");
        a->txt.open(QIODevice::Append | QIODevice::Text);
        a->srt.open(QIODevice::Append | QIODevice::Text);
    }
    if (a->txt.isOpen()) {
        QTextStream o(&a->txt);
        o << "[" << f.hora << "] " << f.texto << "\n";
        o.flush();
    }
    if (a->srt.isOpen()) {
        const qint64 fin = a->inicio.msecsTo(f.cuando);
        const qint64 ini = std::max<qint64>(0, fin - static_cast<qint64>(std::max(1.0, f.segundos) * 1000));
        QTextStream o(&a->srt);
        o << ++a->indice << "\n" << tiempoSrt(ini) << " --> " << tiempoSrt(fin) << "\n" << f.texto << "\n\n";
        o.flush();
    }
}

void FlujosTexto::ponerRotulo(const QString& rotulo, const QString& texto) {
    if (!obs_initialized()) return;
    obs_source_t* s = obs_get_source_by_name(rotulo.toUtf8().constData());
    if (s == nullptr) return;
    obs_data_t* d = obs_data_create();
    obs_data_set_string(d, "text", texto.toUtf8().constData());
    obs_source_update(s, d);
    obs_data_release(d);
    obs_source_release(s);
}

QVariantList FlujosTexto::flujos() const {
    std::map<QString, QVariantMap> por;
    if (lista_) {
        for (const auto& t : lista_()) {
            const QString n = QString::fromStdString(t.value("flujo", ""));
            const auto sal = t.value("salidas", nlohmann::json::object());
            por[n] = QVariantMap{{"nombre", n},
                                 {"origen", t.contains("origen") ? QString::fromStdString(t.value("origen", ""))
                                                                 : "Voz a texto en «" + QString::fromStdString(t.value("fuente", "")) + "»"},
                                 {"modo", QString::fromStdString(t.value("modo", ""))},
                                 {"archivo", sal.value("archivo", false)},
                                 {"rotulo", QString::fromStdString(sal.value("rotulo", std::string()))},
                                 {"cantidad", 0}};
        }
    }
    for (const Frase& f : frases_) {
        QVariantMap& m = por[f.flujo];
        if (!m.contains("nombre")) m = QVariantMap{{"nombre", f.flujo}, {"origen", f.origen}, {"cantidad", 0}};
        m["ultima"] = f.texto;
        m["hora"] = f.hora;
        m["cantidad"] = m["cantidad"].toInt() + 1;
    }
    QVariantList out;
    for (const auto& [n, m] : por) out << m;
    return out;
}

QVariantList FlujosTexto::ultimas(const QString& flujo, int n) const {
    QVariantList out;
    for (auto it = frases_.rbegin(); it != frases_.rend() && out.size() < n; ++it)
        if (flujo.isEmpty() || it->flujo == flujo)
            out.prepend(QVariantMap{{"flujo", it->flujo}, {"texto", it->texto}, {"hora", it->hora}, {"origen", it->origen}});
    return out;
}

std::vector<FlujosTexto::Frase> FlujosTexto::desde(std::uint64_t seq) const {
    std::vector<Frase> out;
    for (const Frase& f : frases_)
        if (f.seq > seq) out.push_back(f);
    return out;
}

} // namespace ba::app::agent
