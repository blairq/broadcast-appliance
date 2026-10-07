// src/app/agent/VozPrompter.cpp — ver VozPrompter.hpp

#include "agent/VozPrompter.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>

#include <nlohmann/json.hpp>
#include "comun/HoraMonotonica.hpp"

#include <algorithm>
#include <cmath>

#include "agent/Voz.hpp"
#include "arranque/AppRuntime.hpp"
#include "audio/MonitorVoz.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#if defined(BA_HAVE_VOZ)
#include "audio/Habla.hpp"
#endif

namespace ba::app::agent {

namespace pv = prompter_voz;

namespace {
constexpr int kTickMs = 100;
constexpr int kConsultaMs = 250;       ///< cada cuánto se mira el estado del gateway
constexpr double kAnticipoMs = 6000;   ///< cuánto antes se sintetiza una frase (con 3 s llegaba tarde)
constexpr double kSaltoPalabras = 1.5; ///< más de esto entre lo previsto y lo informado: saltó

QString archivo(const std::filesystem::path& raiz) {
    return QString::fromStdString((raiz / "Config" / "voz-teleprompter.json").string());
}

double ahoraMs() { return static_cast<double>(QDateTime::currentMSecsSinceEpoch()); }
} // namespace

VozPrompter::VozPrompter(AppRuntime& rt, Voz& voz, ConferenceViewModel* sala, QObject* parent)
    : QObject(parent), rt_(rt), voz_(voz), sala_(sala),
      monitor_(std::make_unique<audio::MonitorVoz>("Voz del teleprompter", 62)),
      naturalPps_(std::make_shared<std::atomic<double>>(3.7)) { // 3,7 palabras/s: piper es_AR, medido
#if defined(BA_HAVE_VOZ)
    habla_ = std::make_unique<audio::Habla>();
#endif
    monitor_->preparar(); // ya en Audio y en la Matriz
    QFile f(archivo(rt_.dataRoot()));
    if (f.open(QIODevice::ReadOnly)) {
        const auto j = nlohmann::json::parse(f.readAll().toStdString(), nullptr, false);
        if (j.is_object()) activa_ = j.value("activa", false);
    }
    connect(&voz_, &Voz::cambio, this, [this]() { revisarVoz(); });
    reloj_.setInterval(kTickMs);
    connect(&reloj_, &QTimer::timeout, this, [this]() { tick(); });
    revisarVoz();
    if (activa_) reloj_.start();
    setEstado(activa_ ? "Esperando el teleprompter" : "Apagada");
}

VozPrompter::~VozPrompter() {
#if defined(BA_HAVE_VOZ)
    if (habla_) habla_->callar();
#endif
}

void VozPrompter::setActiva(bool on) {
    if (activa_ == on) return;
    activa_ = on;
    guardar();
    if (on) {
        siguiente_ = -1;
        reloj_.start();
        setEstado("Esperando el teleprompter");
    } else {
        reloj_.stop();
        reiniciar("apagada");
        setEstado("Apagada");
    }
    emit cambio();
}

void VozPrompter::guardar() const {
    const QString ruta = archivo(rt_.dataRoot());
    QDir().mkpath(QFileInfo(ruta).absolutePath());
    QFile f(ruta);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QByteArray::fromStdString(nlohmann::json{{"activa", activa_}}.dump(1)));
}

void VozPrompter::setEstado(const QString& e) {
    if (e == estado_) return;
    estado_ = e;
    emit cambio();
}

void VozPrompter::revisarVoz() {
#if defined(BA_HAVE_VOZ)
    // La misma voz que la del sistema (Configuración → Audio y voz).
    const QVariantMap v = voz_.vozParaOtros();
    const QString repo = v.value("repo").toString();
    if (repo.isEmpty() || repo == vozCargada_) return;
    vozCargada_ = repo;
    audio::Habla::Voz cfg;
    cfg.modelo = v.value("modelo").toString().toStdString();
    cfg.tokens = v.value("tokens").toString().toStdString();
    cfg.datos = v.value("datos").toString().toStdString();
    habla_->cargar(cfg, [](const std::string&) {});
#endif
}

void VozPrompter::reiniciar(const QString& porque) {
    Q_UNUSED(porque);
#if defined(BA_HAVE_VOZ)
    habla_->callar();
#endif
    monitor_->callar();
    siguiente_ = -1;
}

void VozPrompter::consultar() {
    if (sala_ == nullptr || consultando_) return;
    consultando_ = true;
    const double pedido = ahoraMs();
    sala_->client().get("/api/v1/prompter", [this, pedido](bool ok, const QJsonObject& r) {
        consultando_ = false;
        if (!ok) {
            hayEstado_ = false;
            setEstado("El teleprompter no contesta");
            return;
        }
        const double llegada = ahoraMs();
        const std::string texto = r.value("texto").toString().toStdString();
        pv::Estado e;
        e.posicion = r.value("posicion").toDouble();
        e.ppm = r.value("ppm").toInt();
        e.corriendo = r.value("corriendo").toBool();
        e.desdeMs = r.value("desdeMs").toDouble();
        // El reloj del gateway contra el nuestro (a mitad del viaje).
        const double offset = (pedido + llegada) / 2.0 - r.value("ahoraMs").toDouble();

        bool reiniciar_ = false;
        if (texto != texto_) {
            texto_ = texto;
            palabras_ = pv::palabras(texto_);
            frases_ = pv::frases(texto_, palabras_);
            reiniciar_ = true;
        }
        if (hayEstado_) {
            if (e.corriendo != est_.corriendo || e.ppm != est_.ppm) reiniciar_ = true;
            // ¿Saltó? Lo que informa contra lo que preveíamos.
            const double ms = llegada - offset;
            if (std::fabs(pv::posicionEn(e, ms) - pv::posicionEn(est_, ms)) > kSaltoPalabras)
                reiniciar_ = true;
        }
        est_ = e;
        offsetMs_ = offset;
        hayEstado_ = true;
        if (reiniciar_) reiniciar("cambio en el teleprompter");
    });
}

void VozPrompter::tick() {
    static int cuenta = 0;
    if (++cuenta * kTickMs >= kConsultaMs) {
        cuenta = 0;
        consultar();
    }
    if (!hayEstado_ || frases_.empty()) {
        if (hayEstado_) setEstado("No hay texto cargado en el teleprompter");
        return;
    }
#if defined(BA_HAVE_VOZ)
    if (!habla_->cargada()) {
        setEstado("Sin voz: elegí una en Configuración → Audio y voz");
        return;
    }
#endif
    if (!est_.corriendo) {
        setEstado("En pausa: sigue cuando el teleprompter siga");
        return;
    }
    const double ahoraG = ahoraMs() - offsetMs_; // reloj del gateway
    const double p = pv::posicionEn(est_, ahoraG);
    if (p >= static_cast<double>(palabras_.size())) {
        setEstado("Terminó el texto");
        return;
    }
    setEstado(QString("Leyendo a %1 palabras por minuto").arg(est_.ppm));

    // Ubicarse: la frase en la que está el teleprompter. Si ya la empezó, se
    // dice desde la palabra en la que va.
    if (siguiente_ < 0) {
        int k = 0;
        while (k + 1 < static_cast<int>(frases_.size()) && frases_[static_cast<std::size_t>(k)].hasta <= p)
            ++k;
        const auto& f = frases_[static_cast<std::size_t>(k)];
        if (p - f.desde > 0.6) {
            // Ya la empezó: lo que queda, si son al menos 3 palabras (un pedazo
            // de una o dos no se puede decir al ritmo del texto); si no, la próxima.
            const int desde = static_cast<int>(std::ceil(p));
            if (f.hasta - desde >= 3) agendar(desde, f.hasta, pv::llegaEn(est_, desde));
            siguiente_ = k + 1;
        } else {
            siguiente_ = k;
        }
    }
    // Las que vienen: sintetizarlas con tiempo y agendarlas.
    while (siguiente_ < static_cast<int>(frases_.size())) {
        const auto& f = frases_[static_cast<std::size_t>(siguiente_)];
        const double llega = pv::llegaEn(est_, f.desde);
        if (llega - ahoraG > kAnticipoMs) break;
        agendar(f.desde, f.hasta, llega);
        ++siguiente_;
    }
}

void VozPrompter::agendar(int desde, int hasta, double inicioMs) {
#if defined(BA_HAVE_VOZ)
    const std::string texto = pv::textoDe(texto_, palabras_, desde, hasta);
    if (texto.empty()) return;
    const int n = hasta - desde;
    const double v = pv::velocidadPara(n, est_.ppm, naturalPps_->load());
    // Del reloj del gateway al de libobs.
    const double faltaMs = inicioMs - (ahoraMs() - offsetMs_);
    const std::uint64_t inicioNs =
        ba::app::ahoraNs() + static_cast<std::uint64_t>(std::max(0.0, faltaMs) * 1'000'000.0);
    audio::MonitorVoz* m = monitor_.get();
    auto pps = naturalPps_;
    const double objetivoSeg = n * 60.0 / std::max(1, est_.ppm);
    const QString log = QString::fromStdString((rt_.dataRoot() / "Logs" / "voz-teleprompter.jsonl").string());
    const nlohmann::json base{{"desde", desde},
                              {"hasta", hasta},
                              {"ppm", est_.ppm},
                              {"velocidad", v},
                              {"objetivo_seg", objetivoSeg},
                              {"falta_ms_al_agendar", faltaMs}};
    habla_->decir(texto, 0, static_cast<float>(v),
                  [m, inicioNs, n, v, pps, log, base](const float* x, std::size_t k, std::uint32_t fr) {
                      // ¿Llegó a tiempo? Lo que falta para que tenga que sonar.
                      const double margenMs = (static_cast<double>(inicioNs) - ba::app::ahoraNs()) / 1e6;
                      m->reproducirEn(x, k, fr, inicioNs);
                      // Lo que tardó en decirla, llevado a velocidad 1: la voz natural.
                      const double seg = static_cast<double>(k) / fr;
                      if (seg > 0.3) {
                          const double medida = n / (seg * v);
                          pps->store(0.5 * pps->load() + 0.5 * std::clamp(medida, 1.5, 6.0));
                      }
                      // Bitácora de sincronía: duración real contra la del teleprompter.
                      nlohmann::json r = base;
                      r["ts"] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toStdString();
                      r["dura_seg"] = seg;
                      r["margen_ms"] = margenMs; // negativo: la síntesis llegó tarde
                      QFile f(log);
                      if (f.open(QIODevice::Append | QIODevice::WriteOnly))
                          f.write(QByteArray::fromStdString(r.dump() + "\n"));
                  });
#else
    Q_UNUSED(desde);
    Q_UNUSED(hasta);
    Q_UNUSED(inicioMs);
#endif
}

} // namespace ba::app::agent
