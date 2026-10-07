// src/app/agent/FlujosTexto.cpp — ver FlujosTexto.hpp

#include "agent/FlujosTexto.hpp"

#include "emision/FuentesApp.hpp"

#include <QDir>
#include <QFile>
#include <QTextStream>

#include <algorithm>
#include <fstream>

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
    QFile txt, srt, vtt;
    QDateTime inicio;
    int indice = 0;
};

FlujosTexto::FlujosTexto(std::filesystem::path dataRoot, QObject* parent)
    : QObject(parent), root_(std::move(dataRoot)) {
    std::ifstream f(root_ / "Config" / "correccion-subtitulos.json");
    const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_object()) {
        correccionActiva_ = j.value("activa", false);
        demoraMs_ = j.value("demora_ms", 4000);
    }
    reloj_ = new QTimer(this);
    connect(reloj_, &QTimer::timeout, this, &FlujosTexto::soltarVencidas);
    reloj_->start(200);
}

FlujosTexto::~FlujosTexto() = default;

QString FlujosTexto::carpeta() const {
    const std::filesystem::path ev = carpetaDelEvento_ ? carpetaDelEvento_() : std::filesystem::path();
    if (!ev.empty()) return QString::fromStdString((ev / "Transcripciones").string());
    return QString::fromStdString((root_ / "Recordings" / "Transcripciones").string());
}

void FlujosTexto::publicar(const QString& flujo, const QString& texto, const QString& origen,
                           double segundos) {
    const QString t = texto.trimmed();
    if (flujo.isEmpty() || t.isEmpty()) return;
    // Lo que transcribe la voz espera en la cola si hay corrector; las
    // traducciones salen de la frase ya corregida, así que no esperan de nuevo.
    if (correccionActiva_ && demoraMs_ > 0 && origen.startsWith("Voz a texto")) {
        pendientes_.push_back({++proxPendiente_, flujo, t, origen, segundos,
                               QDateTime::currentMSecsSinceEpoch() + demoraMs_});
        emit pendientesCambio();
        return;
    }
    entregar(flujo, t, origen, segundos);
}

void FlujosTexto::entregar(const QString& flujo, const QString& texto, const QString& origen, double segundos) {
    const QString t = texto.trimmed();
    if (t.isEmpty()) return;
    Frase f;
    f.seq = ++seq_;
    f.flujo = flujo;
    f.texto = t;
    f.origen = origen;
    f.segundos = segundos;
    f.cuando = QDateTime::currentDateTime();
    f.hora = f.cuando.toString("HH:mm:ss");
    frases_.push_back(f);
    while (frases_.size() > kTope)
        frases_.pop_front();

    const nlohmann::json conf = config_ ? config_(flujo) : nlohmann::json();
    const nlohmann::json salidas =
        conf.is_object() ? conf.value("salidas", nlohmann::json::object()) : nlohmann::json::object();
    if (salidas.value("archivo", false)) escribirArchivo(f);
    const std::string rotulo = salidas.value("rotulo", std::string());
    if (!rotulo.empty()) ponerRotulo(QString::fromStdString(rotulo), t);

    emit frase(flujo, t);
    emit fraseConOrigen(flujo, t, origen);
    emit cambio();
}

void FlujosTexto::soltarVencidas() {
    if (pendientes_.empty()) return;
    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    bool cambio = false;
    while (!pendientes_.empty() && pendientes_.front().venceMs <= ahora) {
        const Pendiente p = pendientes_.front();
        pendientes_.pop_front();
        entregar(p.flujo, p.texto, p.origen, p.segundos);
        cambio = true;
    }
    if (cambio) emit pendientesCambio();
}

void FlujosTexto::setCorreccion(bool activa, int demoraMs) {
    correccionActiva_ = activa;
    demoraMs_ = std::clamp(demoraMs, 0, 30000);
    // Apagarla suelta lo que esperaba (no se pierde nada).
    if (!activa)
        while (!pendientes_.empty()) {
            const Pendiente p = pendientes_.front();
            pendientes_.pop_front();
            entregar(p.flujo, p.texto, p.origen, p.segundos);
        }
    std::error_code ec;
    std::filesystem::create_directories(root_ / "Config", ec);
    std::ofstream o(root_ / "Config" / "correccion-subtitulos.json");
    o << nlohmann::json{{"activa", correccionActiva_}, {"demora_ms", demoraMs_}}.dump(1);
    emit pendientesCambio();
    emit cambio();
}

nlohmann::json FlujosTexto::pendientes() const {
    nlohmann::json out = nlohmann::json::array();
    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    for (const Pendiente& p : pendientes_)
        out.push_back({{"id", p.id},
                       {"flujo", p.flujo.toStdString()},
                       {"texto", p.texto.toStdString()},
                       {"origen", p.origen.toStdString()},
                       {"vence_ms", p.venceMs},
                       {"resta_ms", std::max<qint64>(0, p.venceMs - ahora)}});
    return out;
}

std::string FlujosTexto::corregir(std::uint64_t id, const QString& texto, bool descartar) {
    for (auto it = pendientes_.begin(); it != pendientes_.end(); ++it) {
        if (it->id != id) continue;
        const Pendiente p = *it;
        pendientes_.erase(it);
        if (!descartar) entregar(p.flujo, texto.trimmed().isEmpty() ? p.texto : texto, p.origen, p.segundos);
        emit pendientesCambio();
        return {};
    }
    return "esa frase ya salió (o no existe)";
}

QVariantList FlujosTexto::pendientesUi() const {
    QVariantList out;
    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    for (const Pendiente& p : pendientes_)
        out.append(QVariantMap{{"id", QVariant::fromValue<qulonglong>(p.id)},
                               {"flujo", p.flujo},
                               {"texto", p.texto},
                               {"resta", static_cast<int>(std::max<qint64>(0, p.venceMs - ahora) / 1000)}});
    return out;
}

QString FlujosTexto::corregirUi(qulonglong id, const QString& texto, bool descartar) {
    return QString::fromStdString(corregir(id, texto, descartar));
}

QStringList FlujosTexto::archivosDeSesion() const {
    QStringList out;
    for (const auto& [n, a] : archivos_)
        for (const QFile* f : {&a->txt, &a->srt, &a->vtt})
            if (!f->fileName().isEmpty()) out << f->fileName();
    return out;
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
        a->vtt.setFileName(base + ".vtt");
        a->txt.open(QIODevice::Append | QIODevice::Text);
        a->srt.open(QIODevice::Append | QIODevice::Text);
        // WebVTT: lo que entienden los reproductores web y YouTube.
        const bool nuevo = !QFile::exists(a->vtt.fileName());
        if (a->vtt.open(QIODevice::Append | QIODevice::Text) && nuevo) {
            QTextStream o(&a->vtt);
            o << "WEBVTT\n\n";
        }
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
        if (a->vtt.isOpen()) {
            QTextStream v(&a->vtt);
            v << tiempoSrt(ini).replace(',', '.') << " --> " << tiempoSrt(fin).replace(',', '.') << "\n" << f.texto << "\n\n";
            v.flush();
        }
    }
}

void FlujosTexto::ponerRotulo(const QString& rotulo, const QString& texto) {
    fuentes::cambiarAjustes(rotulo.toStdString(), nlohmann::json{{"text", texto.toStdString()}});
}

QVariantList FlujosTexto::flujos() const {
    std::map<QString, QVariantMap> por;
    if (lista_) {
        for (const auto& t : lista_()) {
            const QString n = QString::fromStdString(t.value("flujo", ""));
            const auto sal = t.value("salidas", nlohmann::json::object());
            por[n] = QVariantMap{
                {"nombre", n},
                {"origen", t.contains("origen")
                               ? QString::fromStdString(t.value("origen", ""))
                               : "Voz a texto en «" + QString::fromStdString(t.value("fuente", "")) + "»"},
                {"modo", QString::fromStdString(t.value("modo", ""))},
                {"archivo", sal.value("archivo", false)},
                {"rotulo", QString::fromStdString(sal.value("rotulo", std::string()))},
                {"cantidad", 0}};
        }
    }
    for (const Frase& f : frases_) {
        QVariantMap& m = por[f.flujo];
        if (!m.contains("nombre"))
            m = QVariantMap{{"nombre", f.flujo}, {"origen", f.origen}, {"cantidad", 0}};
        m["ultima"] = f.texto;
        m["hora"] = f.hora;
        m["cantidad"] = m["cantidad"].toInt() + 1;
    }
    QVariantList out;
    for (const auto& [n, m] : por)
        out << m;
    return out;
}

QVariantList FlujosTexto::ultimas(const QString& flujo, int n) const {
    QVariantList out;
    for (auto it = frases_.rbegin(); it != frases_.rend() && out.size() < n; ++it)
        if (flujo.isEmpty() || it->flujo == flujo)
            out.prepend(QVariantMap{
                {"flujo", it->flujo}, {"texto", it->texto}, {"hora", it->hora}, {"origen", it->origen}});
    return out;
}

std::vector<FlujosTexto::Frase> FlujosTexto::desde(std::uint64_t seq) const {
    std::vector<Frase> out;
    for (const Frase& f : frases_)
        if (f.seq > seq) out.push_back(f);
    return out;
}

} // namespace ba::app::agent
