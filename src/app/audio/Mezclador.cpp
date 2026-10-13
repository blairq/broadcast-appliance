// src/app/audio/Mezclador.cpp — ver el .hpp
#include "audio/Mezclador.hpp"

#include "arranque/AppRuntime.hpp"
#include "audio/LoudnessPrograma.hpp"
#include "audio/Matriz.hpp"
#include "audio/RackManager.hpp"
#include "emision/BroadcastEngine.hpp"
#include "Motor.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace ba::app::audio {

namespace {
constexpr int kCuadroMs = 33;       // 30 Hz
constexpr qint64 kRetencionMs = 2000;

float aDb(float lineal) { return lineal > 1e-5f ? 20.0f * std::log10(lineal) : -100.0f; }

QVariantMap aMapa(const nlohmann::json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        .object()
        .toVariantMap();
}
} // namespace

Mezclador::Mezclador(AppRuntime& rt, Matriz* matriz, QObject* parent) : QObject(parent), rt_(rt), matriz_(matriz) {
    timer_.setInterval(kCuadroMs);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &Mezclador::tic);
    // Una columna nueva (un bus, una placa que se enchufa), una pista renombrada…
    if (matriz_ != nullptr) connect(matriz_, &Matriz::cambio, this, &Mezclador::rehacerListas);
    rehacerListas();
}

void Mezclador::usar(QObject* vista, bool si) {
    if (vista == nullptr) return;
    const bool antes = activo();
    if (si && !usuarios_.contains(vista)) {
        usuarios_.insert(vista);
        // Una vista que se destruye sin avisar no deja el temporizador andando.
        connect(vista, &QObject::destroyed, this, [this](QObject* o) {
            const bool a = activo();
            usuarios_.remove(o);
            if (!activo()) timer_.stop();
            if (a != activo()) emit activoCambio();
        });
    } else if (!si) {
        usuarios_.remove(vista);
    }
    if (activo() && !timer_.isActive()) {
        rehacerListas();
        timer_.start();
    } else if (!activo()) {
        timer_.stop();
    }
    if (antes != activo()) emit activoCambio();
}

void Mezclador::rehacerListas() {
    QStringList entradas;
    for (const auto& n : rt_.engine().audioSources()) entradas << QString::fromStdString(n);
    QVariantList salidas;
    std::vector<std::string> ids;
    if (matriz_ != nullptr)
        for (const QVariant& v : matriz_->columnas()) {
            const QVariantMap c = v.toMap();
            // Las entradas virtuales son filas (fuentes), no destinos con vúmetro propio.
            if (c.value("tipo").toString() == "entrada") continue;
            salidas << QVariantMap{{"id", c.value("id")}, {"nombre", c.value("nombre")}, {"tipo", c.value("tipo")}};
            ids.push_back(c.value("id").toString().toStdString());
        }
    if (entradas == entradas_ && salidas == salidas_) return;
    entradas_ = entradas;
    salidas_ = salidas;
    idSalidas_ = std::move(ids);
    emit listasCambio();
}

void Mezclador::medirYa() {
    rehacerListas();
    tic();
}

void Mezclador::tic() {
    // Las fuentes cambian poco: se mira cada tanto (no hace falta un aviso aparte).
    static int vueltas = 0;
    if (++vueltas % 30 == 0) rehacerListas();

    std::unordered_map<std::string, const motor::Motor::NivelAudio*> porId;
    std::shared_ptr<const std::vector<motor::Motor::NivelAudio>> niveles;
    if (motor::Motor* m = rt_.engine().motor()) niveles = m->niveles();
    if (niveles)
        for (const auto& n : *niveles) porId[(n.bus ? "bus:" : "") + n.id] = &n;

    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    const auto retener = [ahora](Retencion& r, int c, float pico) {
        if (pico >= r.db[c] || ahora - r.ms[c] > kRetencionMs) {
            r.db[c] = pico;
            r.ms[c] = ahora;
        }
        return r.db[c];
    };

    QVariantList ne;
    ne.reserve(entradas_.size() * 6);
    for (const QString& nombre : entradas_) {
        float p[2] = {-100, -100}, r[2] = {-100, -100};
        if (const auto it = porId.find(nombre.toStdString()); it != porId.end())
            for (int c = 0; c < 2; ++c) {
                p[c] = it->second->pico[c];
                r[c] = it->second->rms[c];
            }
        Retencion& ret = retE_[nombre];
        ne << p[0] << p[1] << r[0] << r[1] << retener(ret, 0, p[0]) << retener(ret, 1, p[1]);
    }

    QVariantList ns;
    ns.reserve(static_cast<qsizetype>(idSalidas_.size() * 6));
    for (const std::string& id : idSalidas_) {
        float p[2] = {-100, -100}, r[2] = {-100, -100};
        // Las pistas las mide el motor (la 1 es el bus Programa); los buses virtuales y
        // las salidas de la placa, la matriz (pico solo: el RMS se aproxima con él).
        const std::string bus = id == "pista1" ? "bus:programa" : id.rfind("pista", 0) == 0 ? "bus:" + id : "";
        if (!bus.empty()) {
            if (const auto it = porId.find(bus); it != porId.end())
                for (int c = 0; c < 2; ++c) {
                    p[c] = it->second->pico[c];
                    r[c] = it->second->rms[c];
                }
        } else if (matriz_ != nullptr) {
            const float db = static_cast<float>(matriz_->picoDbDe(id));
            p[0] = p[1] = db;
            r[0] = r[1] = db > -100 ? db - 3.0f : -100.0f;
        }
        Retencion& ret = retS_[QString::fromStdString(id)];
        ns << p[0] << p[1] << r[0] << r[1] << retener(ret, 0, p[0]) << retener(ret, 1, p[1]);
    }
    nivelesE_ = std::move(ne);
    nivelesS_ = std::move(ns);
    loudness_ = aMapa(rt_.engine().loudness().json());
    emit nivelesCambio();
}

QVariantMap Mezclador::tira(const QString& nombre) const {
    const std::string n = nombre.toStdString();
    auto& eng = rt_.engine();
    const float v = eng.sourceVolume(n);
    return {{"nombre", nombre},
            {"volumenDb", v > 1e-5f ? 20.0 * std::log10(v) : -100.0},
            {"mute", eng.sourceMuted(n)},
            {"pfl", eng.sourceMonitoring(n)},
            {"envios", matriz_ != nullptr ? matriz_->enviosDe(nombre) : QStringList{}}};
}

QVariantMap Mezclador::envios() const {
    QVariantMap out;
    if (matriz_ == nullptr) return out;
    for (const QString& n : entradas_) out[n] = matriz_->enviosDe(n);
    return out;
}

QVariantList Mezclador::analisisLsp() const {
    QVariantList out;
    for (const auto& a : RackManager::analisisLsp())
        out << QVariantMap{{"id", QString::fromStdString(a.value("id", std::string()))},
                           {"nombre", QString::fromStdString(a.value("nombre", std::string()))},
                           {"para", QString::fromStdString(a.value("para", std::string()))}};
    return out;
}

QString Mezclador::abrirAnalisisLsp(const QString& entrada, const QString& cual) {
    if (racks_ == nullptr) return "el audio de los racks no está disponible";
    if (!entradas_.contains(entrada)) return "no hay una entrada con audio «" + entrada + "»";
    return QString::fromStdString(racks_->abrirAnalisisLsp(entrada.toStdString(), cual.toStdString()));
}

nlohmann::json Mezclador::estado() const {
    using nlohmann::json;
    json entradas = json::array();
    for (qsizetype i = 0; i < entradas_.size(); ++i) {
        const QVariantMap t = tira(entradas_[i]);
        json e{{"nombre", entradas_[i].toStdString()},
               {"volumen_db", std::round(t.value("volumenDb").toDouble() * 10) / 10},
               {"mute", t.value("mute").toBool()},
               {"pfl", t.value("pfl").toBool()}};
        json env = json::array();
        for (const QString& s : t.value("envios").toStringList()) env.push_back(s.toStdString());
        e["envios"] = env;
        if (nivelesE_.size() >= (i + 1) * 6) {
            const auto d = [&](qsizetype k) { return std::round(nivelesE_[i * 6 + k].toDouble() * 10) / 10; };
            e["pico_db"] = {d(0), d(1)};
            e["rms_db"] = {d(2), d(3)};
            e["retencion_db"] = {d(4), d(5)};
        }
        entradas.push_back(e);
    }
    json salidas = json::array();
    for (qsizetype i = 0; i < salidas_.size(); ++i) {
        const QVariantMap c = salidas_[i].toMap();
        json s{{"id", c.value("id").toString().toStdString()},
               {"nombre", c.value("nombre").toString().toStdString()},
               {"tipo", c.value("tipo").toString().toStdString()}};
        if (nivelesS_.size() >= (i + 1) * 6) {
            const auto d = [&](qsizetype k) { return std::round(nivelesS_[i * 6 + k].toDouble() * 10) / 10; };
            s["pico_db"] = {d(0), d(1)};
            s["rms_db"] = {d(2), d(3)};
        }
        salidas.push_back(s);
    }
    return {{"entradas", entradas}, {"salidas", salidas},
            {"loudness", json::parse(QJsonDocument::fromVariant(loudness_).toJson(QJsonDocument::Compact).toStdString(), nullptr, false)}};
}

} // namespace ba::app::audio
