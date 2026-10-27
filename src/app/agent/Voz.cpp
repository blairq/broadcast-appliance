// ============================================================================
// src/app/agent/Voz.cpp — ver Voz.hpp
// ============================================================================
#include "agent/Voz.hpp"
#include "platform/Paths.hpp"

#include "agent/FlujosTexto.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPointer>
#include <QRegularExpression>
#include <QTime>
#include <QTimer>

#include <algorithm>
#include <chrono>

#include <nlohmann/json.hpp>

#include "agent/ArgumentosLocales.hpp"
#include "agent/ModelosLocales.hpp"
#include "i18n/I18n.hpp"
#include "arranque/AppRuntime.hpp"
#include "audio/MonitorVoz.hpp"
#if defined(BA_HAVE_CLAP)
#include "audio/RackManager.hpp"
#endif
#if defined(BA_HAVE_VOZ)
#include "ajustes/Ajustes.hpp"
#include "audio/Escucha.hpp"
#include "audio/EscuchaNemotron.hpp"
#include "audio/Habla.hpp"
#endif

namespace ba::app::agent {

namespace {
QString deRuta(const std::filesystem::path& p) { return QString::fromStdWString(p.wstring()); }
std::int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }
/// El tipo de modelo (catálogo) de cada motor.
QString tipoModelo(MotorOido m) { return m == MotorOido::Nemotron ? "voz_streaming" : "voz"; }
} // namespace

Voz::Voz(AppRuntime& rt, ModelosLocales& modelos, QObject* parent)
    : QObject(parent), rt_(rt), modelos_(modelos) {
#if defined(BA_HAVE_VOZ)
    escucha_ = std::make_unique<audio::Escucha>();
    streaming_ = std::make_unique<audio::EscuchaStreaming>();
    habla_ = std::make_unique<audio::Habla>();
    monitor_ = std::make_unique<audio::MonitorVoz>();
    monitor_->preparar(); // «Voz del sistema», ya en Audio y en la Matriz
    leerAjustes();
    escucha_->setCortes({cortePausa_, corteMaximo_, corteUmbral_});
    streaming_->setCortes({cortePausa_, corteMaximo_, corteUmbral_});
    // Lo que oye cada transcripción continua, para el indicador de la interfaz
    // (encontrar el punto justo de los cortes hablando).
    oidoT_ = new QTimer(this);
    oidoT_->setInterval(200);
    connect(oidoT_, &QTimer::timeout, this, [this]() {
        QVariantList l;
        for (const MotorOido m : Reconocedores::motores())
            for (const auto& [k, o] : motorOido(m)->oido())
                l << QVariantMap{{"flujo", QString::fromStdString(k)},
                                 {"hablando", o.hablando},
                                 {"segundos", o.segundos},
                                 {"parcial", QString::fromStdString(o.parcial)},
                                 {"motor", QString::fromStdString(Reconocedores::nombre(m))}};
        if (l != oido_) {
            oido_ = l;
            emit oidoCambio();
        }
    });
    oidoT_->start();
    // Las frases de la transcripción continua (llegan en el hilo de Escucha).
    // La clave es el nombre de la SALIDA DE TEXTO (la tarjeta), no la fuente.
    const auto aFrase = [this](const std::string& flujo, const audio::Reconocedor::Resultado& r) {
        alFrase(flujo, r.texto, r.segundosDeAudio);
    };
    escucha_->setFrase(aFrase);
    streaming_->setFrase(aFrase);
    // La persona empezó a hablar: si su consumidor puede interrumpir (el tótem
    // con Nemotron), se avisa a quien abrió el oído para que corte al asistente.
    const auto aVoz = [this](const std::string& clave) {
        QMetaObject::invokeMethod(this, [this, clave]() {
            const auto it = alHablar_.find(QString::fromStdString(clave));
            if (it == alHablar_.end() || !it->second) return;
            if (!reconocedores_->interrumpir(Reconocedores::consumidorDeClave(clave))) return;
            it->second();
        });
    };
    escucha_->setEmpiezaVoz(aVoz);
    streaming_->setEmpiezaVoz(aVoz);
    {
        audio::EscuchaStreaming::Margenes mg;
        mg.antes = ajustes::numero("escucha.margenAntesS", mg.antes);
        mg.despues = ajustes::numero("escucha.margenDespuesS", mg.despues);
        streaming_->setMargenes(mg);
    }
    // Cada motor se carga cuando alguien lo usa y se suelta cuando ya nadie (con un margen).
    reconocedores_ = std::make_unique<Reconocedores>(rt_.dataRoot() / "Config" / "reconocedores.json");
    reconocedores_->setMargenMs(static_cast<std::int64_t>(ajustes::numero("escucha.descargarTrasS", 120.0) * 1000));
    reconocedores_->setAcciones(
        {[this](MotorOido m) { cargarMotor(m); }, [this](MotorOido m) { descargarMotor(m); }});
    for (const MotorOido m : Reconocedores::motores()) cargados_[m] = claveModelos(m);
    reconT_ = new QTimer(this);
    reconT_->setInterval(1000);
    connect(reconT_, &QTimer::timeout, this, [this]() {
        if (reconocedores_->tic(ahoraMs())) actualizarReconocedores();
    });
    reconT_->start();
    // El dictado (F9) está siempre a mano: su reconocedor queda cargado, como siempre.
    reconocedores_->usar("dictado", "dictado", ahoraMs());
    actualizarReconocedores();
    connect(&modelos_, &ModelosLocales::cambio, this, &Voz::revisarModelos);
    connect(&modelos_, &ModelosLocales::cambio, this, &Voz::revisarVoz);
    qApp->installEventFilter(this);
    revisarModelos();
    revisarVoz();
#else
    estado_ = tr("Esta compilación no trae el motor de voz (make deps-sherpa-onnx).");
#endif
}

Voz::~Voz() {
#if defined(BA_HAVE_VOZ) && defined(BA_HAVE_CLAP)
    if (racks_ != nullptr) audio::RackManager::setTextoSink({}); // el audio ya no llama acá
#endif
#if defined(BA_HAVE_VOZ)
    if (habla_) habla_->callar();
    habla_.reset(); // antes que el monitor: su hilo le escribe
#endif
    rt_.operatorFeed().setPushToTalk(false);
    rt_.operatorFeed().setDictado({});
    if (qApp != nullptr) qApp->removeEventFilter(this);
}

bool Voz::disponible() const {
#if defined(BA_HAVE_VOZ)
    return true;
#else
    return false;
#endif
}

void Voz::setEstado(const QString& e) {
    estado_ = e;
    emit cambio();
}

void Voz::revisarModelos() {
#if defined(BA_HAVE_VOZ)
    // Se bajó o se borró un modelo: el motor que lo usa se vuelve a cargar (si
    // alguien lo está usando) o se suelta.
    for (const MotorOido m : Reconocedores::motores()) {
        const QString clave = claveModelos(m);
        if (cargados_[m] == clave) continue;
        cargados_[m] = clave;
        reconocedores_->recargar(m);
    }
    actualizarReconocedores();
#endif
}

// --- Los reconocedores (agent/Reconocedores) ---------------------------------------------

audio::Reconocedor* Voz::motorOido(MotorOido m) const {
#if defined(BA_HAVE_VOZ)
    if (m == MotorOido::Nemotron) return streaming_.get();
    return escucha_.get();
#else
    Q_UNUSED(m);
    return nullptr;
#endif
}

QString Voz::claveModelos(MotorOido m) {
    const QString voz = modelos_.instaladoDeTipo(tipoModelo(m));
    const QString vad = modelos_.instaladoDeTipo("vad");
    const QString titulo = QString::fromStdString(Reconocedores::titulo(m));
    faltan_[m] = voz.isEmpty()   ? tr("Falta bajar el reconocedor de voz %1 (Configuración → Modelos locales).").arg(titulo)
            : vad.isEmpty() ? tr("Falta bajar el detector de voz (Silero VAD).")
                            : QString();
    if (voz.isEmpty() || vad.isEmpty()) return {};
    return voz + "|" + vad;
}

bool Voz::puedeOir(const QString& consumidor) const {
#if defined(BA_HAVE_VOZ)
    const auto it = cargados_.find(reconocedores_->motorDe(consumidor.toStdString()));
    return it != cargados_.end() && !it->second.isEmpty();
#else
    Q_UNUSED(consumidor);
    return false;
#endif
}

void Voz::cargarMotor(MotorOido m) {
#if defined(BA_HAVE_VOZ)
    const QString voz = modelos_.instaladoDeTipo(tipoModelo(m));
    const QString vad = modelos_.instaladoDeTipo("vad");
    if (voz.isEmpty() || vad.isEmpty()) {
        reconocedores_->informarCarga(m, false,
                                      (voz.isEmpty() ? tr("falta bajar %1 (Configuración → Modelos locales)")
                                                           .arg(QString::fromStdString(Reconocedores::titulo(m)))
                                                     : tr("falta bajar el detector de voz (Silero VAD)"))
                                          .toStdString());
        return;
    }
    const std::filesystem::path d = modelos_.carpetaInstalada(voz);
    const std::filesystem::path v = modelos_.carpetaInstalada(vad);
    QPointer<Voz> yo(this);
    // Llega en el hilo del motor.
    const auto listo = [yo, m](const std::string& error) {
        QMetaObject::invokeMethod(qApp, [yo, m, error]() {
            if (!yo) return;
            // Parakeet avisa «sin VAD» con error pero cargado: cuenta como cargado.
            yo->reconocedores_->informarCarga(m, yo->motorOido(m)->cargado(), error);
            yo->actualizarReconocedores();
        });
    };
    if (m == MotorOido::Nemotron) {
        audio::ModelosNemotron n;
        n.encoder = (d / "encoder.int8.onnx").string();
        n.decoder = (d / "decoder.int8.onnx").string();
        n.joiner = (d / "joiner.int8.onnx").string();
        n.tokens = (d / "tokens.txt").string();
        n.vad = (v / "silero_vad.onnx").string();
        // El idioma (el «prompt» del modelo): castellano de fábrica; "auto" lo detecta solo.
        n.idioma = ajustes::texto("escucha.nemotronIdioma", "es-US");
        if (n.idioma == "auto") n.idioma.clear();
        n.hilos = ajustes::entero("escucha.nemotronHilos", 2);
        // El fin de turno del modelo, un poco más largo que la pausa del VAD:
        // quien cierra una frase corta es el margen; el modelo, la que sigue sin parar.
        n.pausaTurno = std::max(0.5, cortePausa_ + 0.3);
        // El largo máximo lo maneja EscuchaStreaming (corta en una pausa); la
        // regla del modelo corta en seco: queda de red, bien arriba.
        n.maximoTurno = std::max(20.0, corteMaximo_ * 3);
        audio::cargarNemotron(*streaming_, n, listo);
    } else {
        audio::Escucha::Modelos e;
        e.encoder = (d / "encoder.int8.onnx").string();
        e.decoder = (d / "decoder.int8.onnx").string();
        e.joiner = (d / "joiner.int8.onnx").string();
        e.tokens = (d / "tokens.txt").string();
        e.vad = (v / "silero_vad.onnx").string();
        escucha_->cargar(e, listo);
    }
#else
    Q_UNUSED(m);
#endif
}

void Voz::descargarMotor(MotorOido m) {
    if (audio::Reconocedor* r = motorOido(m)) r->descargar();
}

void Voz::abrirCorriente(const std::string& clave, const std::string& consumidor) {
#if defined(BA_HAVE_VOZ)
    const MotorOido m = reconocedores_->usar(clave, consumidor, ahoraMs());
    audio::Reconocedor* r = motorOido(m);
    r->abrirContinuo(clave);
    {
        std::lock_guard<std::mutex> l(rutasLock_);
        rutas_[clave] = r;
    }
    if (const auto b = oyeMientras_.find(QString::fromStdString(clave)); b != oyeMientras_.end())
        b->second->store(reconocedores_->interrumpir(consumidor));
    actualizarReconocedores();
#else
    Q_UNUSED(clave);
    Q_UNUSED(consumidor);
#endif
}

void Voz::cerrarCorriente(const std::string& clave) {
    audio::Reconocedor* r = nullptr;
    {
        std::lock_guard<std::mutex> l(rutasLock_);
        if (const auto it = rutas_.find(clave); it != rutas_.end()) {
            r = it->second;
            rutas_.erase(it);
        }
    }
    if (r != nullptr) r->cerrarContinuo(clave);
    if (reconocedores_) reconocedores_->soltar(clave, ahoraMs());
    actualizarReconocedores();
}

void Voz::moverCorriente(const std::string& clave) {
#if defined(BA_HAVE_VOZ)
    const auto antes = reconocedores_->motorDeUso(clave);
    if (!antes) return;
    // Las salidas de texto del rack son de la transcripción aunque se llamen como otra cosa.
    const std::string consumidor =
        continuas_.contains(clave) ? std::string("transcripcion") : Reconocedores::consumidorDeClave(clave);
    const MotorOido m = reconocedores_->usar(clave, consumidor, ahoraMs());
    if (const auto b = oyeMientras_.find(QString::fromStdString(clave)); b != oyeMientras_.end())
        b->second->store(reconocedores_->interrumpir(consumidor));
    if (*antes == m || clave == "dictado") return; // el dictado no tiene corriente: solo cuenta
    motorOido(*antes)->cerrarContinuo(clave);
    audio::Reconocedor* r = motorOido(m);
    r->abrirContinuo(clave);
    std::lock_guard<std::mutex> l(rutasLock_);
    rutas_[clave] = r;
#else
    Q_UNUSED(clave);
#endif
}

QString Voz::elegirReconocedor(const QString& consumidor, const QString& motor) {
#if defined(BA_HAVE_VOZ)
    const std::string c = consumidor.toStdString();
    if (const std::string e = reconocedores_->elegir(c, motor.toStdString()); !e.empty())
        return QString::fromStdString(e);
    // Lo que estaba abierto pasa al motor nuevo (el viejo se suelta con su margen).
    for (const std::string& clave : reconocedores_->usosDe(c)) moverCorriente(clave);
    actualizarReconocedores();
    return {};
#else
    Q_UNUSED(consumidor);
    Q_UNUSED(motor);
    return tr("Esta compilación no trae el motor de voz.");
#endif
}

QString Voz::setInterrumpir(const QString& consumidor, bool on) {
#if defined(BA_HAVE_VOZ)
    const std::string c = consumidor.toStdString();
    if (const std::string e = reconocedores_->setInterrumpir(c, on); !e.empty()) return QString::fromStdString(e);
    for (const std::string& clave : reconocedores_->usosDe(c))
        if (const auto b = oyeMientras_.find(QString::fromStdString(clave)); b != oyeMientras_.end())
            b->second->store(reconocedores_->interrumpir(c));
    actualizarReconocedores();
    return {};
#else
    Q_UNUSED(consumidor);
    Q_UNUSED(on);
    return tr("Esta compilación no trae el motor de voz.");
#endif
}

std::shared_ptr<std::atomic<bool>> Voz::oyeMientrasHabla(const QString& clave) {
    auto& b = oyeMientras_[clave];
    if (!b) b = std::make_shared<std::atomic<bool>>(false);
#if defined(BA_HAVE_VOZ)
    b->store(reconocedores_->interrumpir(Reconocedores::consumidorDeClave(clave.toStdString())));
#endif
    return b;
}

bool Voz::oyendoVoz(const QString& clave) const {
#if defined(BA_HAVE_VOZ)
    const std::string k = clave.toStdString();
    std::lock_guard<std::mutex> l(rutasLock_);
    const auto r = rutas_.find(k);
    if (r == rutas_.end()) return false;
    const auto oido = r->second->oido();
    const auto o = oido.find(k);
    return o != oido.end() && (o->second.hablando || !o->second.parcial.empty());
#else
    Q_UNUSED(clave);
    return false;
#endif
}

nlohmann::json Voz::reconocedoresJson() const {
#if defined(BA_HAVE_VOZ)
    nlohmann::json j = reconocedores_->estado();
    for (auto& m : j["motores"]) {
        const auto motor = Reconocedores::deNombre(m.value("id", std::string()));
        m["instalado"] = motor && cargados_.count(*motor) > 0 && !cargados_.at(*motor).isEmpty();
    }
    j["idioma_nemotron"] = ajustes::texto("escucha.nemotronIdioma", "es-US");
    if (streaming_) {
        const auto l = streaming_->lote();
        j["lote_nemotron"] = {{"ultimo", l.ultimo},
                              {"maximo", l.maximo},
                              {"pasos", l.pasos},
                              {"segundos_al_modelo", l.segundosAlModelo},
                              {"segundos_oidos", l.segundosOidos}};
    }
    return j;
#else
    return nlohmann::json::object();
#endif
}

void Voz::actualizarReconocedores() {
#if defined(BA_HAVE_VOZ)
    nlohmann::json j = reconocedoresJson();
    j.erase("lote_nemotron"); // cambia a cada rato: va por MCP, no a la vista
    const QVariantMap e = QJsonDocument::fromJson(QByteArray::fromStdString(j.dump())).object().toVariantMap();
    if (e != reconocedoresEstado_) {
        reconocedoresEstado_ = e;
        emit reconocedoresCambio();
        emit cambio(); // EstadoIA mira `cambio` (la alerta de los dos a la vez)
    }
    // El dictado: listo cuando su motor está cargado.
    const MotorOido m = reconocedores_->motorDeUso("dictado").value_or(reconocedores_->motorDe("dictado"));
    const bool lista = reconocedores_->cargado(m) && motorOido(m)->cargado();
    const QString titulo = QString::fromStdString(Reconocedores::titulo(m));
    QString estado;
    if (lista)
        estado = tr("Lista: mantené %1 (o el micrófono) y hablá.").arg(tecla());
    else if (reconocedores_->cargando(m))
        estado = tr("Cargando el reconocedor de voz (%1)…").arg(titulo);
    else if (const auto f = faltan_.find(m); f != faltan_.end() && !f->second.isEmpty())
        estado = f->second;
    else if (!reconocedores_->error(m).empty())
        estado = tr("No cargó el reconocedor: %1").arg(QString::fromStdString(reconocedores_->error(m)));
    // El estado del dictado lo pisan también sus resultados («Lista · 3 s de
    // habla…»): acá solo se cambia cuando cambia si está lista o por qué no.
    if (lista != lista_ || (!lista && !estado.isEmpty() && estado != estado_)) {
        lista_ = lista;
        setEstado(estado);
    }
#endif
}

void Voz::alFrase(const std::string& flujo, const std::string& texto, double segundos) {
    QMetaObject::invokeMethod(this, [this, flujo, texto, segundos]() {
        const QString fl = QString::fromStdString(flujo), t = QString::fromStdString(texto);
        // Un oído de afuera (la atención hablada, el tótem): la frase es de quien lo abrió.
        if (const auto o = oidos_.find(fl); o != oidos_.end()) {
            if (o->second && !t.trimmed().isEmpty()) o->second(t);
            return;
        }
        QString fuente = fl;
#if defined(BA_HAVE_CLAP)
        if (racks_ != nullptr) {
            const auto d = racks_->textoDe(flujo);
            if (d.is_object()) fuente = QString::fromStdString(d.value("fuente", flujo));
        }
#endif
        transcripcion_.push_back(QVariantMap{{"fuente", fuente},
                                             {"flujo", fl},
                                             {"texto", t},
                                             {"hora", QTime::currentTime().toString("HH:mm:ss")},
                                             {"segundos", segundos}});
        while (transcripcion_.size() > 200)
            transcripcion_.removeFirst();
        emit transcripcionCambio();
        emit frase(fuente, t);
        if (flujos_ != nullptr) flujos_->publicar(fl, t, "Voz a texto en «" + fuente + "»", segundos);
    });
}

void Voz::apretar() {
#if defined(BA_HAVE_VOZ)
    if (grabando_) return;
    if (!lista_) {
        emit notify(estado_, "warn");
        return;
    }
    OperatorFeed& feed = rt_.operatorFeed();
    // ¿Hay fuentes con voz a texto en modo dictado? Si no, se le pone solo al
    // micrófono del operador (así funciona sin pasos extra).
    bool porRack = false;
    if (racks_ != nullptr) {
        for (const auto& f : racks_->fuentesConTexto())
            if (f.value("modo", std::string()) == "dictado") porRack = true;
        const QString mic = microfono();
        if (!porRack && !mic.isEmpty() && racks_->setTexto(mic.toStdString(), "dictado").empty())
            porRack = true;
    }
    if (!porRack && !feed.hayMicrofono()) {
        emit notify(tr("No hay micrófono del operador: elegilo en Configuración → Voz del sistema."), "warn");
        return;
    }
    callar(); // el operador habla: el asistente se calla
    {
        std::lock_guard<std::mutex> l(tomasLock_);
        tomas_.clear();
    }
    // Sin racks: el micrófono del operador, directo de OperatorFeed.
    if (!porRack)
        feed.setDictado([this](const float* m, std::size_t n, std::uint32_t f) {
            alAudio("micrófono del operador", 1, m, static_cast<std::uint32_t>(n), f);
        });
    tomando_ = true;
    feed.setPushToTalk(true); // desde acá el lobby ya no oye al operador
    grabando_ = true;
    setEstado(tr("Escuchando… (soltá para mandar)"));
    prepararTc(); // mientras habla, el modelo v2 ya lee el estado
#endif
}

void Voz::soltar() {
#if defined(BA_HAVE_VOZ)
    if (!grabando_) return;
    tomando_ = false;
    OperatorFeed& feed = rt_.operatorFeed();
    feed.setPushToTalk(false); // el lobby vuelve a oírlo
    feed.setDictado({});
    grabando_ = false;
    // La fuente donde más se habló (si hay dos micrófonos con dictado, el
    // otro solo trajo ruido de fondo).
    Toma elegida;
    std::string de;
    {
        std::lock_guard<std::mutex> l(tomasLock_);
        for (auto& [fuente, t] : tomas_)
            if (t.energia > elegida.energia) {
                elegida = std::move(t);
                de = fuente;
            }
        tomas_.clear();
    }
    if (elegida.audio.empty()) {
        setEstado(tr("Lista · no llegó audio (¿el micrófono está muteado o sin señal?)"));
        emit notify(tr("Voz: no llegó audio del micrófono."), "warn");
        return;
    }
    transcribiendo_ = true;
    setEstado(tr("Transcribiendo…"));
    const MotorOido motorDictado =
        reconocedores_->motorDeUso("dictado").value_or(reconocedores_->motorDe("dictado"));
    motorOido(motorDictado)->transcribir(
        std::move(elegida.audio), elegida.frecuencia, [this, de](const audio::Reconocedor::Resultado& r) {
            QMetaObject::invokeMethod(this, [this, r, de]() {
                transcribiendo_ = false;
                if (!r.error.empty()) {
                    setEstado(tr("Lista · %1").arg(QString::fromStdString(r.error)));
                    emit notify(tr("Voz: %1").arg(QString::fromStdString(r.error)), "warn");
                    return;
                }
                ultimo_ = QString::fromStdString(r.texto);
                setEstado(tr("Lista · %1 s de habla (%2) en %3 ms")
                              .arg(r.segundosDeAudio, 0, 'f', 1)
                              .arg(QString::fromStdString(de))
                              .arg(r.ms, 0, 'f', 0));
                atenderDe(ultimo_, "dictado",
                          nlohmann::json{
                              {"fuente", de}, {"segundos", r.segundosDeAudio}, {"ms_transcripcion", r.ms}});
            });
        });
#endif
}

void Voz::cancelar() {
#if defined(BA_HAVE_VOZ)
    if (!grabando_) return;
    tomando_ = false;
    rt_.operatorFeed().setPushToTalk(false);
    rt_.operatorFeed().setDictado({});
    {
        std::lock_guard<std::mutex> l(tomasLock_);
        tomas_.clear();
    }
    grabando_ = false;
    setEstado(tr("Lista · cancelado"));
#endif
}

void Voz::alAudio(const std::string& fuente, int modo, const float* m, std::uint32_t n, std::uint32_t f) {
#if defined(BA_HAVE_VOZ)
    if (modo == 2) {
        std::lock_guard<std::mutex> l(rutasLock_);
        if (const auto it = rutas_.find(fuente); it != rutas_.end()) it->second->alimentarContinuo(fuente, m, n, f);
        return;
    }
    if (modo != 1 || !tomando_) return;
    std::lock_guard<std::mutex> l(tomasLock_);
    Toma& t = tomas_[fuente];
    if (t.audio.size() > static_cast<std::size_t>(audio::Escucha::kMaxSegundosAhora() * f)) return;
    t.frecuencia = f;
    t.audio.insert(t.audio.end(), m, m + n);
    for (std::uint32_t i = 0; i < n; ++i)
        t.energia += static_cast<double>(m[i]) * m[i];
#else
    Q_UNUSED(fuente);
    Q_UNUSED(modo);
    Q_UNUSED(m);
    Q_UNUSED(n);
    Q_UNUSED(f);
#endif
}

// --- Oídos de afuera de los racks ---------------------------------------------------

bool Voz::abrirOido(const QString& clave, std::function<void(const QString&)> alTexto,
                    std::function<void()> alHablar) {
#if defined(BA_HAVE_VOZ)
    // Basta con que el modelo de su motor esté bajado: si no está cargado, se
    // carga ahora (lo que llegue mientras tanto no se transcribe).
    const std::string consumidor = Reconocedores::consumidorDeClave(clave.toStdString());
    if (!puedeOir(QString::fromStdString(consumidor))) return false;
    const bool nueva = oidos_.find(clave) == oidos_.end();
    oidos_[clave] = std::move(alTexto);
    alHablar_[clave] = std::move(alHablar);
    if (nueva) abrirCorriente(clave.toStdString(), consumidor);
    return true;
#else
    Q_UNUSED(clave);
    Q_UNUSED(alTexto);
    Q_UNUSED(alHablar);
    return false;
#endif
}

void Voz::cerrarOido(const QString& clave) {
#if defined(BA_HAVE_VOZ)
    alHablar_.erase(clave);
    oyeMientras_.erase(clave);
    if (oidos_.erase(clave) > 0) cerrarCorriente(clave.toStdString());
#else
    Q_UNUSED(clave);
#endif
}

void Voz::alimentarOido(const std::string& clave, const float* m, std::size_t n, std::uint32_t f) {
#if defined(BA_HAVE_VOZ)
    std::lock_guard<std::mutex> l(rutasLock_);
    if (const auto it = rutas_.find(clave); it != rutas_.end()) // sin corriente abierta no hace nada
        it->second->alimentarContinuo(clave, m, n, f);
#else
    Q_UNUSED(clave);
    Q_UNUSED(m);
    Q_UNUSED(n);
    Q_UNUSED(f);
#endif
}

// --- Micrófono y transcripción ------------------------------------------------------

void Voz::setRacks(audio::RackManager* racks) {
#if defined(BA_HAVE_VOZ) && defined(BA_HAVE_CLAP)
    racks_ = racks;
    if (racks_ == nullptr) return;
    audio::RackManager::setTextoSink([this](const std::string& fuente, int modo, const float* m,
                                            std::uint32_t n,
                                            std::uint32_t f) { alAudio(fuente, modo, m, n, f); });
    connect(racks_, &audio::RackManager::changed, this, [this]() {
        revisarCorrientes();
        emit cambio();
    });
    revisarCorrientes();
#else
    Q_UNUSED(racks);
#endif
}

void Voz::revisarCorrientes() {
#if defined(BA_HAVE_VOZ) && defined(BA_HAVE_CLAP)
    if (racks_ == nullptr) return;
    std::set<std::string> ahora;
    for (const auto& f : racks_->textos())
        if (f.value("modo", std::string()) == "continuo") ahora.insert(f.value("flujo", std::string()));
    for (const auto& c : continuas_)
        if (!ahora.contains(c)) cerrarCorriente(c);
    for (const auto& c : ahora)
        if (!continuas_.contains(c)) abrirCorriente(c, "transcripcion");
    continuas_ = std::move(ahora);
#endif
}

QString Voz::microfono() const { return QString::fromStdString(rt_.config().operatorAudio); }

bool Voz::dictadoConMicrofono() const {
    if (!microfono().isEmpty() || rt_.operatorFeed().hayMicrofono()) return true;
    if (racks_ != nullptr)
        for (const auto& f : racks_->fuentesConTexto())
            if (f.value("modo", std::string()) == "dictado") return true;
    return false;
}

QString Voz::setMicrofono(const QString& fuente) {
    if (!rt_.setOperatorFeed(rt_.config().operatorVideo, fuente.toStdString()))
        return tr("No existe la fuente «%1».").arg(fuente);
    emit cambio();
    return {};
}

QVariantList Voz::fuentesTexto() const {
    QVariantList out;
#if defined(BA_HAVE_CLAP)
    if (racks_ != nullptr)
        for (const auto& f : racks_->fuentesConTexto())
            out.push_back(QVariantMap{{"fuente", QString::fromStdString(f.value("fuente", std::string()))},
                                      {"modo", QString::fromStdString(f.value("modo", std::string()))},
                                      {"flujo", QString::fromStdString(f.value("flujo", std::string()))}});
#endif
    return out;
}

QString Voz::setTexto(const QString& fuente, const QString& modo) {
#if defined(BA_HAVE_CLAP)
    if (racks_ == nullptr) return tr("Esta compilación no tiene racks de audio.");
    if (modo.isEmpty()) {
        racks_->quitarTextos(fuente.toStdString());
        return {};
    }
    if (modo != "dictado" && modo != "continuo") return tr("El modo es «dictado» o «continuo».");
    return QString::fromStdString(racks_->setTexto(fuente.toStdString(), modo.toStdString()));
#else
    Q_UNUSED(fuente);
    Q_UNUSED(modo);
    return tr("Esta compilación no tiene racks de audio.");
#endif
}

QVariantList Voz::transcripcionDe(const QString& fuente, int n) const {
    QVariantList out;
    for (auto it = transcripcion_.crbegin(); it != transcripcion_.crend() && out.size() < n; ++it)
        if (fuente.isEmpty() || it->toMap().value("fuente").toString() == fuente) out.prepend(*it);
    return out;
}

void Voz::borrarTranscripcion() {
    transcripcion_.clear();
    emit transcripcionCambio();
}

// --- Contestar hablando ----------------------------------------------------------

namespace {
QString archivoAjustes(const std::filesystem::path& raiz) { return deRuta(ba::platform::dataRoot() / "Modelos" / "voz.json"); }
} // namespace

// --- Lo dictado, por el modelo local ------------------------------------------------------

void Voz::setHerramientas(Ficha ficha, Ejecutor ejecutor, Contexto contexto) {
    ficha_ = std::move(ficha);
    ejecutor_ = std::move(ejecutor);
    contexto_ = std::move(contexto);
    sistemaArgs_ = argumentos::sistemaPorDefecto();
}

void Voz::setUsarModeloLocal(bool on) {
    if (usarModeloLocal_ == on) return;
    usarModeloLocal_ = on;
    guardarAjustes();
    emit cambio();
}

QVariantList Voz::automaticas() const {
    QVariantList out;
    for (const QString& h : automaticas_) {
        const FichaHerramienta f = ficha_ ? ficha_(h) : FichaHerramienta{};
        out << QVariantMap{{"herramienta", h}, {"titulo", f.titulo.isEmpty() ? h : f.titulo}};
    }
    return out;
}

void Voz::setAutomatica(const QString& herramienta, bool on) {
    const QString h = herramienta.trimmed();
    if (h.isEmpty() || automaticas_.contains(h) == on) return;
    if (on)
        automaticas_ << h;
    else
        automaticas_.removeAll(h);
    automaticas_.sort();
    guardarAjustes();
    emit cambio();
}

void Voz::atenderDesde(const QString& texto, const QString& quien, bool disparador) {
    if (!disparador) {
        atenderDe(texto, "asistente:" + quien, nlohmann::json::object());
        return;
    }
    // El encargo de un disparador viene como «[HH:mm:ss] aviso (disparador x,
    // tipo y)», una por línea si se juntaron: se dice y se atiende el aviso.
    static const QRegularExpression hora("^\\[\\d\\d:\\d\\d:\\d\\d\\]\\s*");
    static const QRegularExpression cola("\\s*\\(disparador [^)]*\\)\\s*$");
    for (QString linea : texto.split('\n', Qt::SkipEmptyParts)) {
        linea.remove(hora);
        linea.remove(cola);
        linea = linea.trimmed();
        if (linea.isEmpty()) continue;
        const int id = ultimoId_ + 1; // el que va a tomar atenderDe
        soloProponer_.insert(id);
        responder(id, linea);
        atenderDe(linea, "disparador:" + quien, nlohmann::json{{"disparador", true}});
    }
}

void Voz::atender(const QString& frase) { atenderDe(frase, "manual", nlohmann::json::object()); }
void Voz::dictar(const QString& frase) { atenderDe(frase, "dictado", nlohmann::json{{"simulado", true}}); }

void Voz::atenderDe(const QString& frase, const QString& origen, const nlohmann::json& meta) {
    const QString f = frase.trimmed();
    const int id = ++ultimoId_;
    nlohmann::json rec = meta;
    rec["origen"] = origen.toStdString();
    rec["texto"] = f.toStdString();
    bitacora(id, "reconocido", rec);
    if (f.isEmpty()) return;
    // Mirando al asistente (su página o su ventana): lo dictado es para él, tal cual,
    // como cuando el modelo local deriva. Si no está andando, sigue el camino de siempre.
    const bool asistenteAndando = (asistenteGrande_ && asistenteGrande_()) || (asistenteLocal_ && asistenteLocal_());
    if (origen == "dictado" && vistaAsistente_ && asistenteAndando) {
        bitacora(id, "al_asistente", {{"motivo", "mirando al asistente"}, {"texto", f.toStdString()}});
        emit dictado(f);
        return;
    }
    // Sin modelo local (o apagado a propósito): al asistente, como siempre.
    if (!usarModeloLocal_ || !modelos_.listo() || !ficha_) {
        bitacora(id, "al_asistente",
                 {{"motivo", !usarModeloLocal_ ? "modelo local apagado en Configuración"
                             : !ficha_         ? "sin herramientas MCP"
                                       : "modelo local no listo (" + modelos_.estado().toStdString() + ")"},
                  {"texto", f.toStdString()}});
        // Vino del asistente «Modelo local» (o es él quien lo recibiría):
        // devolvérselo sería un bucle.
        if (origen.startsWith("asistente:") || (asistenteLocal_ && asistenteLocal_())) {
            responder(id, !modelos_.listo() ? tr("El modelo local no está listo todavía.")
                                            : tr("El modelo local está apagado para lo dictado."));
            return;
        }
        emit dictado(f);
        return;
    }
    // Modelo v2 (tool calling nativo): una sola vuelta, con el estado.
    if (modelos_.formato() == "tc") {
        atenderTc(id, f, origen);
        return;
    }
    bitacora(id, "modelo_pedido",
             {{"op", "elegir"}, {"frase", f.toStdString()}, {"modelo", modelos_.activo().toStdString()}});
    const auto t0 = std::chrono::steady_clock::now();
    modelos_.elegir(f, [this, f, id, t0, origen](const nlohmann::json& j) {
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        QMetaObject::invokeMethod(this, [this, f, j, id, ms, origen]() {
            bitacora(id, "modelo_respuesta", {{"respuesta", j}, {"ms_ida_y_vuelta", ms}});
            const QString h = QString::fromStdString(j.value("herramienta", std::string()));
            const FichaHerramienta ficha = h.isEmpty() ? FichaHerramienta{} : ficha_(h);
            // `derivar`, un error o algo que no existe: es del asistente.
            if (j.contains("error") || h.isEmpty() || h == "derivar" || !ficha.existe) {
                derivarA(id, f, origen,
                         j.contains("error") ? "error del modelo"
                         : h == "derivar"    ? "derivar"
                                             : "herramienta inexistente: " + h.toStdString());
                return;
            }
            if (ficha.requiereDatos)
                completar(id, f, h, ficha);
            else
                decidir(id, f, h, ficha, nlohmann::json::object(), {}, {});
        });
    });
}

QString Voz::estadoTc(const nlohmann::json& estado) {
    // JSON compacto, con los acentos de verdad (como se entrenó). El MISMO
    // texto en preparar y en llamar: si difiere en un byte, no se aprovecha.
    return estado.is_object() && !estado.empty() ? QString::fromStdString(estado.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)) : QString();
}

void Voz::prepararTc(std::function<void()> listo) {
    if (!usarModeloLocal_ || !modelos_.listo() || modelos_.formato() != "tc" || !contexto_) {
        if (listo) listo();
        return;
    }
    contexto_([this, listo](const nlohmann::json& estado) {
        const QString e = estadoTc(estado);
        if (e.isEmpty()) {
            if (listo) listo();
            return;
        }
        modelos_.preparar(e, [this, listo](const nlohmann::json& j) {
            QMetaObject::invokeMethod(this, [this, j, listo]() {
                bitacora(0, "preparado", j);
                if (listo) listo();
            });
        });
    });
}

void Voz::probarTc(const QString& frase, std::function<void(const nlohmann::json&)> r) {
    const auto conEstado = [this, frase, r](const nlohmann::json& estado) {
        modelos_.llamar(estadoTc(estado), frase, [this, r](const nlohmann::json& j) {
            QMetaObject::invokeMethod(this, [r, j]() { r(j); });
        });
    };
    if (contexto_)
        contexto_(conEstado);
    else
        conEstado(nlohmann::json::object());
}

void Voz::derivarA(int id, const QString& f, const QString& origen, const std::string& motivo) {
    // Un asistente grande andando, y el pedido no vino de él: es suyo.
    const bool delAsistente = origen.startsWith("asistente:");
    if (!delAsistente && asistenteGrande_ && asistenteGrande_()) {
        bitacora(id, "al_asistente", {{"motivo", motivo}, {"texto", f.toStdString()}});
        emit dictado(f);
        return;
    }
    // Si no, NO se le devuelve: con el asistente «Modelo local», dictado()
    // volvía a entrar acá y derivaba de nuevo, sin fin. Tampoco se lo pasa a
    // una charla del mismo modelo sin herramientas: medido, a «escribile a
    // Juan…» contesta «le escribo a Juan» sin haber hecho nada.
    bitacora(id, "sin_asistente", {{"motivo", motivo}, {"texto", f.toStdString()}});
    responder(id, tr("Eso es para el asistente grande, y no está andando."));
    // Queda escrito en la barra, para no perderlo (salvo que la barra se lo
    // vaya a mandar al mismo modelo local).
    if (!delAsistente && !(asistenteLocal_ && asistenteLocal_())) emit dictado(f);
}

void Voz::atenderTc(int id, const QString& f, const QString& origen) {
    // El estado que se entrenó: escenas, fuentes, audio (el micrófono del
    // operador primero), participantes, gráficos y carruseles. Sin contexto
    // (no debería pasar) va sin estado: el modelo lo toma como charla.
    const auto conEstado = [this, id, f, origen](const nlohmann::json& estado) {
        const QString estadoJson = estadoTc(estado);
        bitacora(id, "modelo_pedido",
                 {{"op", "llamar"},
                  {"frase", f.toStdString()},
                  {"modelo", modelos_.activo().toStdString()},
                  {"estado", estadoJson.toStdString()}});
        const auto t0 = std::chrono::steady_clock::now();
        modelos_.llamar(estadoJson, f, [this, id, f, estado, t0, origen](const nlohmann::json& j) {
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            QMetaObject::invokeMethod(this, [this, id, f, estado, j, ms, origen]() {
                bitacora(id, "modelo_respuesta",
                         {{"op", "llamar"}, {"respuesta", j}, {"ms_ida_y_vuelta", ms}});
                if (j.contains("error")) {
                    derivarA(id, f, origen, "error del modelo: " + j.value("error", std::string()));
                    return;
                }
                const std::string crudo = j.value("crudo", j.value("texto", std::string()));
                const argumentos::LlamadaTc l = argumentos::leerTc(crudo);
                // Charla: el modelo contesta en texto.
                if (!l.hay) {
                    const QString texto = QString::fromStdString(j.value("texto", std::string())).trimmed();
                    bitacora(id, "charla", {{"texto", texto.toStdString()}});
                    if (texto.isEmpty()) {
                        derivarA(id, f, origen, "charla vacía");
                        return;
                    }
                    emit notify("🗣 " + texto, "ok");
                    responder(id, texto);
                    return;
                }
                const QString h = QString::fromStdString(l.funcion);
                if (h == "derivar") {
                    derivarA(id, f, origen, "derivar");
                    return;
                }
                if (h == "falta_dato") {
                    QString pregunta;
                    for (const auto& [k, v] : l.parametros)
                        if (k == "pregunta") pregunta = QString::fromStdString(v).trimmed();
                    if (pregunta.isEmpty()) pregunta = tr("Me falta un dato: ¿cuál?");
                    bitacora(id, "falta_dato", {{"pregunta", pregunta.toStdString()}});
                    emit notify("❓ " + pregunta, "ok");
                    responder(id, pregunta);
                    return;
                }
                const FichaHerramienta ficha = ficha_ ? ficha_(h) : FichaHerramienta{};
                if (!ficha.existe) {
                    derivarA(id, f, origen, "herramienta inexistente: " + h.toStdString());
                    return;
                }
                const nlohmann::json crudos = argumentos::argsDesdeTc(l, ficha.ficha);
                const auto r = argumentos::validar(h.toStdString(), ficha.ficha, crudos, estado);
                QStringList notas, bloqueos;
                for (const auto& n : r.notas)
                    notas << QString::fromStdString(n);
                for (const auto& b : r.bloqueos)
                    bloqueos << QString::fromStdString(b);
                bitacora(id, "argumentos",
                         {{"crudos", crudos},
                          {"args", r.args},
                          {"notas", notas.join(" | ").toStdString()},
                          {"bloqueos", bloqueos.join(" | ").toStdString()}});
                decidir(id, f, h, ficha, r.args, notas, bloqueos);
            });
        });
    };
    if (contexto_)
        contexto_(conEstado);
    else
        conEstado(nlohmann::json::object());
}

void Voz::completar(int id, const QString& frase, const QString& h, const FichaHerramienta& ficha) {
    if (!contexto_) {
        decidir(id, frase, h, ficha, nlohmann::json::object(), {},
                {tr("no hay estado del aparato para completar los datos")});
        return;
    }
    contexto_([this, id, frase, h, ficha](const nlohmann::json& estado) {
        const std::string texto =
            argumentos::prompt(sistemaArgs_, h.toStdString(), ficha.ficha, estado, frase.toStdString());
        bitacora(id, "modelo_pedido",
                 {{"op", "generar"}, {"herramienta", h.toStdString()}, {"prompt", texto}});
        const auto t0 = std::chrono::steady_clock::now();
        // Más lugar si la herramienta lleva listas u objetos (scene_define arma una escena entera).
        int maxTokens = 96;
        for (const auto& [clave, prop] : ficha.ficha.value("inputSchema", nlohmann::json::object())
                                             .value("properties", nlohmann::json::object())
                                             .items())
            if (argumentos::tipoDe(prop) == "array" || argumentos::tipoDe(prop) == "object")
                maxTokens = 256;
        modelos_.generar(
            QString::fromStdString(texto), false, maxTokens, {},
            [this, id, frase, h, ficha, estado, t0](const nlohmann::json& j) {
                const double ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                QMetaObject::invokeMethod(this, [this, id, frase, h, ficha, estado, j, ms]() {
                    const std::string crudo = j.value("texto", std::string());
                    const nlohmann::json leidos = argumentos::leer(crudo, h.toStdString());
                    QStringList notas, bloqueos;
                    nlohmann::json args = nlohmann::json::object();
                    if (j.contains("error"))
                        bloqueos << tr("el modelo no contestó: %1").arg(QString::fromStdString(j.value("error", std::string())));
                    else if (!leidos.is_object())
                        bloqueos << tr("el modelo no escribió un JSON");
                    else {
                        const auto r = argumentos::validar(h.toStdString(), ficha.ficha, leidos, estado);
                        args = r.args;
                        for (const auto& n : r.notas)
                            notas << QString::fromStdString(n);
                        for (const auto& b : r.bloqueos)
                            bloqueos << QString::fromStdString(b);
                    }
                    bitacora(id, "modelo_respuesta",
                             {{"op", "generar"},
                              {"crudo", "{" + crudo},
                              {"ms_ida_y_vuelta", ms},
                              {"args", args},
                              {"notas", notas.join(" | ").toStdString()},
                              {"bloqueos", bloqueos.join(" | ").toStdString()}});
                    decidir(id, frase, h, ficha, args, notas, bloqueos);
                });
            });
    });
}

void Voz::decidir(int id, const QString& frase, const QString& h, const FichaHerramienta& ficha,
                  const nlohmann::json& args, const QStringList& notas, const QStringList& bloqueos) {
    const QString titulo = ficha.titulo.isEmpty() ? h : ficha.titulo;
    // Automática y sin dudas: se hace. Con dudas (un nombre que no existe, un
    // dato que falta) se propone igual, para que el operador lo vea.
    // Una pregunta (solo lee) no cambia nada: se contesta sin preguntar.
    // Un disparador: solo lo que lee se hace solo; las acciones, propuestas.
    const bool automatica = automaticas_.contains(h) && !soloProponer_.contains(id);
    if ((automatica || ficha.lectura) && bloqueos.isEmpty()) {
        bitacora(id, "automatica", {{"herramienta", h.toStdString()}, {"args", args}});
        hacer(id, frase, h, titulo, args);
        return;
    }
    propuesta_ = QVariantMap{{"id", id},
                             {"frase", frase},
                             {"herramienta", h},
                             {"titulo", titulo},
                             {"args", QString::fromStdString(args.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))},
                             {"resumen", QString::fromStdString(argumentos::resumen(args))},
                             {"notas", notas.join(" · ")},
                             {"bloqueos", bloqueos.join(" · ")},
                             {"puedeHacer", bloqueos.isEmpty()}};
    if (!bloqueos.isEmpty()) responder(id, tr("No me quedó claro: %1").arg(bloqueos.first()));
    bitacora(
        id, "propuesta",
        {{"herramienta", h.toStdString()}, {"args", args}, {"bloqueos", bloqueos.join(" | ").toStdString()}});
    emit propuestaCambio();
}

void Voz::hacer(int id, const QString& frase, const QString& herramienta, const QString& titulo,
                const nlohmann::json& args) {
    if (!ejecutor_) return;
    const bool lectura = ficha_ && ficha_(herramienta).lectura;
    bitacora(id, "ejecutar", {{"herramienta", herramienta.toStdString()}, {"argumentos", args}});
    ejecutor_(
        herramienta, args, [this, id, frase, titulo, herramienta, lectura](bool ok, const QString& texto) {
            bitacora(id, "resultado",
                     {{"herramienta", herramienta.toStdString()},
                      {"ok", ok},
                      {"texto", texto.left(2000).toStdString()}});
            const QString corto = texto.size() > 160 ? texto.left(157) + "..." : texto;
            const QString aviso = corto.isEmpty()
                                      ? (ok ? tr("Hecho: %1") : tr("No se pudo: %1")).arg(titulo)
                                      : (ok ? tr("Hecho: %1 — %2") : tr("No se pudo: %1 — %2")).arg(titulo, corto);
            emit notify(aviso, ok ? "ok" : "warn");
            if (!ok)
                responder(id, tr("No pude. %1").arg(texto.left(140)));
            else if (lectura)
                contestarLectura(id, frase, herramienta, texto);
            else
                responder(id, tr("Listo."));
        });
}

void Voz::responder(int id, const QString& texto) {
    const QString t = texto.trimmed();
    bitacora(id, "respuesta_voz", {{"texto", t.toStdString()}, {"dicha", contestarConVoz_ && vozLista_}});
    if (contestarConVoz_ && vozLista_ && !t.isEmpty()) decir(t);
}

void Voz::contestarLectura(int id, const QString& frase, const QString& herramienta, const QString& datos) {
    // El mismo modelo local, sin prefijo: una oración para decir en voz alta.
    // En el idioma del operador: el sistema y el pedido (las marcas <|im_start|>… son del modelo).
    const std::string texto =
        "<|im_start|>system\n" +
        ba::i18n::traducir("asistente", "Sos la voz del aparato de transmisión en vivo. Contestale al operador lo que "
                                        "preguntó en UNA oración corta en castellano rioplatense, para decir en voz alta: "
                                        "sin listas, sin símbolos, sin markdown. Usá solo los datos que te paso.") +
        "<|im_end|>\n<|im_start|>user\n" +
        ba::i18n::llenar(ba::i18n::traducir("asistente", "Pregunta: «%1»\nDatos (de %2): %3"),
                         {frase.toStdString(), herramienta.toStdString(), datos.left(2500).toStdString()}) +
        "<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n";
    bitacora(id, "modelo_pedido", {{"op", "generar"}, {"para", "contestar"}, {"prompt", texto}});
    const auto t0 = std::chrono::steady_clock::now();
    modelos_.generar(QString::fromStdString(texto), false, 60, {}, [this, id, t0](const nlohmann::json& j) {
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        QMetaObject::invokeMethod(this, [this, id, j, ms]() {
            const QString r = QString::fromStdString(j.value("texto", std::string())).trimmed();
            bitacora(id, "modelo_respuesta",
                     {{"op", "generar"},
                      {"para", "contestar"},
                      {"texto", r.toStdString()},
                      {"ms_ida_y_vuelta", ms}});
            responder(id, r.isEmpty() ? tr("Listo.") : r);
        });
    });
}

#if defined(BA_HAVE_CLAP)
QString Voz::auriculares() const {
    return racks_ != nullptr ? QString::fromStdString(racks_->auriculares().value("id", std::string()))
                             : QString();
}

QVariantList Voz::salidasAudio() const {
    QVariantList out;
    if (racks_ == nullptr) return out;
    for (const auto& g : racks_->outputDevices()) {
        QVariantList salidas;
        for (const auto& o : g.value("salidas", nlohmann::json::array()))
            salidas << QVariantMap{{"id", QString::fromStdString(o.value("id", std::string()))},
                                   {"nombre", QString::fromStdString(o.value("nombre", std::string()))}};
        out << QVariantMap{{"dispositivo", QString::fromStdString(g.value("dispositivo", std::string()))},
                           {"salidas", salidas}};
    }
    return out;
}

QString Voz::setAuriculares(const QString& id) {
    if (racks_ == nullptr) return tr("el audio no está disponible");
    const std::string e = racks_->setAuriculares(id.toStdString());
    emit cambio();
    if (e.empty() && contestarConVoz_ && vozLista_) decir(tr("Te escucho por acá."));
    return QString::fromStdString(e);
}

#else
QString Voz::auriculares() const { return {}; }
QVariantList Voz::salidasAudio() const { return {}; }
QString Voz::setAuriculares(const QString&) { return tr("el audio no está disponible"); }
#endif

void Voz::setContestarConVoz(bool on) {
    if (contestarConVoz_ == on) return;
    contestarConVoz_ = on;
    guardarAjustes();
    emit cambio();
}

void Voz::confirmar(bool noVolverAPreguntar) {
    if (propuesta_.isEmpty()) return;
    const QVariantMap p = propuesta_;
    propuesta_.clear();
    emit propuestaCambio();
    const QString h = p.value("herramienta").toString();
    const int id = p.value("id").toInt();
    bitacora(id, "decision", {{"decision", noVolverAPreguntar ? "hacer_y_no_volver_a_preguntar" : "hacer"}});
    if (!p.value("puedeHacer").toBool()) {
        emit notify(tr("No se puede hacer así: %1").arg(p.value("bloqueos").toString()), "warn");
        return;
    }
    if (noVolverAPreguntar) setAutomatica(h, true);
    hacer(id, p.value("frase").toString(), h, p.value("titulo").toString(),
          nlohmann::json::parse(p.value("args").toString().toStdString(), nullptr, false));
}

void Voz::descartar() {
    if (propuesta_.isEmpty()) return;
    bitacora(propuesta_.value("id").toInt(), "decision", {{"decision", "no"}});
    propuesta_.clear();
    emit propuestaCambio();
}

void Voz::alAsistente() {
    if (propuesta_.isEmpty()) return;
    bitacora(propuesta_.value("id").toInt(), "decision", {{"decision", "al_asistente"}});
    const QString frase = propuesta_.value("frase").toString();
    propuesta_.clear();
    emit propuestaCambio();
    emit dictado(frase);
}

void Voz::bitacora(int id, const std::string& evento, nlohmann::json datos) const {
    const QString dir = QString::fromStdString((ba::platform::dataRoot() / "Logs").string());
    QDir().mkpath(dir);
    const QString ruta = dir + "/voz-modelo.jsonl";
    // Un archivo por vez, de hasta 20 MB; el anterior queda como .1.
    if (QFileInfo(ruta).size() > 20 * 1024 * 1024) {
        QFile::remove(ruta + ".1");
        QFile::rename(ruta, ruta + ".1");
    }
    datos["ts"] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toStdString();
    // El id se reinicia en cada apertura: la sesión (la hora de arranque) los separa.
    static const std::string sesion = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss").toStdString();
    datos["sesion"] = sesion;
    datos["id"] = id;
    datos["evento"] = evento;
    QFile f(ruta);
    if (f.open(QIODevice::Append | QIODevice::WriteOnly))
        f.write(QByteArray::fromStdString(
            datos.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n"));
}

void Voz::leerAjustes() {
    QFile f(archivoAjustes(rt_.dataRoot()));
    if (!f.open(QIODevice::ReadOnly)) return;
    const nlohmann::json j = nlohmann::json::parse(f.readAll().toStdString(), nullptr, false);
    if (!j.is_object()) return;
    vozElegida_ = QString::fromStdString(j.value("voz", std::string()));
    hablante_ = j.value("hablante", 0);
    velocidad_ = std::clamp(j.value("velocidad", 1.0), 0.7, 1.5);
    usarModeloLocal_ = j.value("modeloLocal", true);
    contestarConVoz_ = j.value("contestarConVoz", true);
    cortePausa_ = j.value("corte_pausa", 0.35);
    corteMaximo_ = j.value("corte_maximo", 6.0);
    corteUmbral_ = j.value("corte_umbral", 0.5);
    automaticas_.clear();
    for (const auto& h : j.value("automaticas", nlohmann::json::array()))
        if (h.is_string()) automaticas_ << QString::fromStdString(h.get<std::string>());
}

void Voz::guardarAjustes() const {
    nlohmann::json autos = nlohmann::json::array();
    for (const QString& h : automaticas_)
        autos.push_back(h.toStdString());
    const nlohmann::json j{{"voz", vozElegida_.toStdString()},
                           {"hablante", hablante_},
                           {"velocidad", velocidad_},
                           {"modeloLocal", usarModeloLocal_},
                           {"contestarConVoz", contestarConVoz_},
                           {"corte_pausa", cortePausa_},
                           {"corte_maximo", corteMaximo_},
                           {"corte_umbral", corteUmbral_},
                           {"automaticas", autos}};
    const QString ruta = archivoAjustes(rt_.dataRoot());
    QDir().mkpath(QFileInfo(ruta).absolutePath());
    QFile f(ruta);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QByteArray::fromStdString(j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace)));
}

QVariantMap Voz::cortes() const {
    return {{"pausa", cortePausa_}, {"maximo", corteMaximo_}, {"umbral", corteUmbral_}};
}

QString Voz::setCortes(double pausa, double maximo, double umbral) {
    if (pausa < 0.1 || pausa > 3) return tr("La pausa va de 0,1 a 3 s.");
    if (maximo < 2 || maximo > 30) return tr("El largo máximo va de 2 a 30 s.");
    if (umbral < 0.1 || umbral > 0.95) return tr("El umbral va de 0,1 a 0,95.");
    cortePausa_ = pausa;
    corteMaximo_ = maximo;
    corteUmbral_ = umbral;
#if defined(BA_HAVE_VOZ)
    escucha_->setCortes({pausa, maximo, umbral});
    streaming_->setCortes({pausa, maximo, umbral});
#endif
    guardarAjustes();
    emit cambio();
    return {};
}

QVariantList Voz::voces() const {
    QVariantList out;
    for (const QVariant& v : modelos_.catalogo()) {
        QVariantMap m = v.toMap();
        if (m.value("tipo").toString() != "tts") continue;
        m["elegida"] = m.value("repo").toString() == vozElegida_;
        out.push_back(m);
    }
    return out;
}

void Voz::revisarVoz() {
#if defined(BA_HAVE_VOZ)
    // Sin elegir (o borrada): la primera bajada.
    if (vozElegida_.isEmpty() || modelos_.carpetaInstalada(vozElegida_).empty()) {
        const QString primera = modelos_.instaladoDeTipo("tts");
        if (primera != vozElegida_) {
            vozElegida_ = primera;
            hablante_ = 0;
            guardarAjustes();
        }
    }
    const std::filesystem::path d =
        vozElegida_.isEmpty() ? std::filesystem::path() : modelos_.carpetaInstalada(vozElegida_);
    if (d.empty()) {
        vozLista_ = false;
        vozCargada_.clear();
        emit cambio();
        return;
    }
    if (vozCargada_ == vozElegida_) {
        emit cambio();
        return;
    }
    vozCargada_ = vozElegida_;
    vozLista_ = false;
    QString modelo;
    for (const QVariant& v : modelos_.catalogo())
        if (v.toMap().value("repo").toString() == vozElegida_) modelo = v.toMap().value("modelo").toString();
    audio::Habla::Voz v;
    v.modelo = (d / modelo.toStdString()).string();
    v.tokens = (d / "tokens.txt").string();
    v.datos = (d / "espeak-ng-data").string();
    emit cambio();
    habla_->cargar(v, [this, v](const std::string& error) {
        QMetaObject::invokeMethod(this, [this, error, v]() {
            vozLista_ = habla_->cargada();
            vozParaOtros_ = vozLista_ ? QVariantMap{{"modelo", QString::fromStdString(v.modelo)},
                                                    {"tokens", QString::fromStdString(v.tokens)},
                                                    {"datos", QString::fromStdString(v.datos)},
                                                    {"repo", vozCargada_}}
                                      : QVariantMap{};
            hablantes_ = habla_->hablantes();
            if (hablante_ >= hablantes_) hablante_ = 0;
            if (!error.empty()) emit notify(tr("La voz no cargó: %1").arg(QString::fromStdString(error)), "warn");
            emit cambio();
        });
    });
#endif
}

void Voz::elegirVoz(const QString& repo) {
    if (repo == vozElegida_) return;
    if (modelos_.carpetaInstalada(repo).empty()) {
        emit notify(tr("Primero hay que bajar esa voz."), "warn");
        return;
    }
    callar();
    vozElegida_ = repo;
    hablante_ = 0;
    guardarAjustes();
    revisarVoz();
}

void Voz::setHablante(int h) {
    if (h == hablante_ || h < 0) return;
    hablante_ = h;
    guardarAjustes();
    emit cambio();
}

void Voz::setVelocidad(double v) {
    v = std::clamp(v, 0.7, 1.5);
    if (v == velocidad_) return;
    velocidad_ = v;
    guardarAjustes();
    emit cambio();
}

void Voz::decir(const QString& texto) {
#if defined(BA_HAVE_VOZ)
    const QString t = texto.trimmed();
    if (t.isEmpty()) return;
    if (!vozLista_) {
        emit notify(tr("No hay una voz cargada: bajá una en Configuración → Modelos locales."), "warn");
        return;
    }
    audio::MonitorVoz* m = monitor_.get();
    ++pendientes_;
    hablando_ = true;
    emit cambio();
    habla_->decir(
        t.toStdString(), hablante_, static_cast<float>(velocidad_),
        [m](const float* x, std::size_t n, std::uint32_t f) { m->reproducir(x, n, f); },
        [this]() {
            QMetaObject::invokeMethod(this, [this]() {
                if (pendientes_ > 0) --pendientes_;
                hablando_ = pendientes_ > 0;
                emit cambio();
            });
        });
#else
    Q_UNUSED(texto);
#endif
}

void Voz::probarVoz() {
    decir(tr("Hola, soy la voz del asistente. Esto solo lo escuchás vos, por los auriculares."));
}

void Voz::callar() {
#if defined(BA_HAVE_VOZ)
    if (habla_) habla_->callar();
    if (monitor_) monitor_->callar();
    pendientes_ = 0;
    if (hablando_) {
        hablando_ = false;
        emit cambio();
    }
#endif
}

bool Voz::eventFilter(QObject* obj, QEvent* ev) {
    const auto t = ev->type();
    if (t == QEvent::KeyPress || t == QEvent::KeyRelease) {
        auto* k = static_cast<QKeyEvent*>(ev);
        if (k->key() == Qt::Key_F9) {
            // El filtro ve el mismo evento por cada objeto de la cadena: solo
            // cuenta el que va a la ventana, y nunca las repeticiones.
            if (!k->isAutoRepeat() && obj->isWindowType()) {
                if (t == QEvent::KeyPress)
                    apretar();
                else
                    soltar();
            }
            return true;
        }
    }
    // Si la app pierde el foco con la tecla apretada, no queda grabando (ni
    // el lobby callado).
    if (t == QEvent::ApplicationDeactivate && grabando_) soltar();
    return QObject::eventFilter(obj, ev);
}

} // namespace ba::app::agent
