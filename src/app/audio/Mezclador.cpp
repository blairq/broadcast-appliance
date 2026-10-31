// src/app/audio/Mezclador.cpp — ver el .hpp
#include "audio/Mezclador.hpp"

#include "arranque/AppRuntime.hpp"
#include "audio/Calibracion.hpp"
#include "audio/LoudnessPrograma.hpp"
#include "audio/Matriz.hpp"
#include "audio/RackManager.hpp"
#include "emision/NivelesMotor.hpp"
#include "emision/VentanillaMotor.hpp"

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

Mezclador::Mezclador(AppRuntime& rt, VentanillaMotor& ventanilla, Matriz* matriz, QObject* parent)
    : QObject(parent), rt_(rt), v_(ventanilla), matriz_(matriz) {
    timer_.setInterval(kCuadroMs);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &Mezclador::tic);
    // Una columna nueva (un bus, una placa que se enchufa), una pista renombrada…
    if (matriz_ != nullptr) connect(matriz_, &Matriz::cambio, this, &Mezclador::rehacerListas);
    // Una entrada con audio que llega o se va: con la foto del motor.
    connect(&v_, &VentanillaMotor::cambio, this, [this](unsigned partes) {
        if ((partes & FotoMotor::Mezcla) != 0) rehacerListas();
    });
    rehacerListas();
    calibracion_ = std::make_unique<Calibracion>(rt.dataRoot(), rt.modoPrueba());
}

Mezclador::~Mezclador() = default;

QObject* Mezclador::calibracionQml() const { return calibracion_.get(); }

void Mezclador::setPestana(const QString& p) {
    const QString v = p == "calibracion" ? p : QString("consola");
    if (v == pestana_) return;
    pestana_ = v;
    emit pestanaCambio();
}

void Mezclador::setRacks(RackManager* r) {
    racks_ = r;
    calibracion_->setRacks(r);
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
    for (const auto& c : v_.foto().mezcla.canales) entradas << QString::fromStdString(c.nombre);
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
    // (Las entradas que llegan o se van las trae la foto: rehacerListas.)
    static int vueltas = 0;
    ++vueltas;

    std::unordered_map<std::string, const motor::NivelAudio*> porId;
    const std::shared_ptr<const NivelesMotor::Niveles> niveles = rt_.niveles().niveles();
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
        // Las pistas las mide el motor (la 1 es el bus Programa), las de la salida que se
        // muestra (las demás llevan su id: bus:programa:<id>, bus:pista2:<id>); los buses
        // virtuales y las salidas de la placa, la matriz (pico solo: el RMS se aproxima con él).
        const std::string suya = salida_.empty() ? std::string() : ":" + salida_;
        const std::string bus = id == "pista1" ? "bus:programa" + suya : id.rfind("pista", 0) == 0 ? "bus:" + id + suya : "";
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
    // El loudness cambia despacio (el momentáneo integra 400 ms): 5 veces por segundo
    // alcanza, y se ahorra la vuelta por JSON en cada cuadro.
    if (vueltas % 6 == 0 || loudness_.isEmpty()) {
        // De la foto (la parte Medidas: el loudness de cada salida, por su id).
        const FotoMotor& f = v_.foto();
        const std::string id = f.mezcla.salida(salida_);
        loudness_ = f.loudness.contains(id) ? aMapa(f.loudness[id]) : QVariantMap{};
    }
    emit nivelesCambio();
}

QVariantMap Mezclador::tira(const QString& nombre) const {
    const std::string n = nombre.toStdString();
    const FotoMotor::MezclaAudio& m = v_.foto().mezcla;
    const float v = m.volumen(n, salida_);
    const FotoMotor::CanalAudio* c = m.canal(n);
    return {{"nombre", nombre},
            {"volumenDb", v > 1e-5f ? 20.0 * std::log10(v) : -100.0},
            {"mute", m.mudo(n, salida_)},
            // El solo de la escucha (F2-03): antes del fader (PFL) o con el de esta salida (AFL).
            {"pfl", m.pfl == n && m.pflModo == "pfl"},
            {"afl", m.pfl == n && m.pflModo == "afl"},
            // El retardo respecto de su video (sincro labios, audio_sincro): positivo atrasa.
            {"retardoMs", c != nullptr ? c->retardoMs : 0},
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
    if (racks_ == nullptr) return tr("el audio de los racks no está disponible");
    if (!entradas_.contains(entrada)) return tr("no hay una entrada con audio «%1»").arg(entrada);
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
               {"pfl", t.value("pfl").toBool()},
               {"afl", t.value("afl").toBool()},
               {"retardo_ms", t.value("retardoMs").toInt()}};
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
