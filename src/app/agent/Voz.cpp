// ============================================================================
// src/app/agent/Voz.cpp — ver Voz.hpp
// ============================================================================
#include "agent/Voz.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QTime>

#include <algorithm>

#include <nlohmann/json.hpp>

#include "agent/ModelosLocales.hpp"
#include "AppRuntime.hpp"
#include "audio/MonitorVoz.hpp"
#if defined(BA_HAVE_CLAP)
#include "audio/RackManager.hpp"
#endif
#if defined(BA_HAVE_VOZ)
#include "audio/Escucha.hpp"
#include "audio/Habla.hpp"
#endif

namespace ba::app::agent {

namespace {
QString deRuta(const std::filesystem::path& p) { return QString::fromStdWString(p.wstring()); }
} // namespace

Voz::Voz(AppRuntime& rt, ModelosLocales& modelos, QObject* parent)
    : QObject(parent), rt_(rt), modelos_(modelos) {
#if defined(BA_HAVE_VOZ)
    escucha_ = std::make_unique<audio::Escucha>();
    habla_ = std::make_unique<audio::Habla>();
    monitor_ = std::make_unique<audio::MonitorVoz>();
    leerAjustes();
    // Las frases de la transcripción continua (llegan en el hilo de Escucha).
    escucha_->setFrase([this](const std::string& fuente, const audio::Escucha::Resultado& r) {
        QMetaObject::invokeMethod(this, [this, fuente, r]() {
            const QString f = QString::fromStdString(fuente), t = QString::fromStdString(r.texto);
            transcripcion_.push_back(QVariantMap{{"fuente", f},
                                                 {"texto", t},
                                                 {"hora", QTime::currentTime().toString("HH:mm:ss")},
                                                 {"segundos", r.segundosDeAudio}});
            while (transcripcion_.size() > 200)
                transcripcion_.removeFirst();
            emit transcripcionCambio();
            emit frase(f, t);
        });
    });
    connect(&modelos_, &ModelosLocales::cambio, this, &Voz::revisarModelos);
    connect(&modelos_, &ModelosLocales::cambio, this, &Voz::revisarVoz);
    qApp->installEventFilter(this);
    revisarModelos();
    revisarVoz();
#else
    estado_ = "Esta compilación no trae el motor de voz (make deps-sherpa-onnx).";
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
    const QString voz = modelos_.instaladoDeTipo("voz");
    const QString vad = modelos_.instaladoDeTipo("vad");
    if (voz.isEmpty() || vad.isEmpty()) {
        lista_ = false;
        cargados_.clear();
        setEstado(voz.isEmpty() ? "Falta bajar el reconocedor de voz (Configuración → Modelos locales)."
                                : "Falta bajar el detector de voz (Silero VAD).");
        return;
    }
    if (cargados_ == voz + "|" + vad) return;
    cargados_ = voz + "|" + vad;
    lista_ = false;
    setEstado("Cargando el reconocedor de voz…");
    const std::filesystem::path d = modelos_.carpetaInstalada(voz);
    const std::filesystem::path v = modelos_.carpetaInstalada(vad);
    audio::Escucha::Modelos m;
    m.encoder = (d / "encoder.int8.onnx").string();
    m.decoder = (d / "decoder.int8.onnx").string();
    m.joiner = (d / "joiner.int8.onnx").string();
    m.tokens = (d / "tokens.txt").string();
    m.vad = (v / "silero_vad.onnx").string();
    escucha_->cargar(m, [this](const std::string& error) {
        QMetaObject::invokeMethod(this, [this, error]() {
            lista_ = escucha_->cargado();
            setEstado(!lista_         ? "No cargó el reconocedor: " + QString::fromStdString(error)
                      : error.empty() ? "Lista: mantené " + tecla() + " (o el micrófono) y hablá."
                                      : QString::fromStdString(error));
        });
    });
#endif
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
        emit notify("No hay micrófono del operador: elegilo en Configuración → Audio y voz.", "warn");
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
    setEstado("Escuchando… (soltá para mandar)");
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
        setEstado("Lista · no llegó audio (¿el micrófono está muteado o sin señal?)");
        emit notify("Voz: no llegó audio del micrófono.", "warn");
        return;
    }
    transcribiendo_ = true;
    setEstado("Transcribiendo…");
    escucha_->transcribir(std::move(elegida.audio), elegida.frecuencia,
                          [this, de](const audio::Escucha::Resultado& r) {
                              QMetaObject::invokeMethod(this, [this, r, de]() {
                                  transcribiendo_ = false;
                                  if (!r.error.empty()) {
                                      setEstado("Lista · " + QString::fromStdString(r.error));
                                      emit notify("Voz: " + QString::fromStdString(r.error), "warn");
                                      return;
                                  }
                                  ultimo_ = QString::fromStdString(r.texto);
                                  setEstado(QString("Lista · %1 s de habla (%2) en %3 ms")
                                                .arg(r.segundosDeAudio, 0, 'f', 1)
                                                .arg(QString::fromStdString(de))
                                                .arg(r.ms, 0, 'f', 0));
                                  emit dictado(ultimo_);
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
    setEstado("Lista · cancelado");
#endif
}

void Voz::alAudio(const std::string& fuente, int modo, const float* m, std::uint32_t n, std::uint32_t f) {
#if defined(BA_HAVE_VOZ)
    if (modo == 2) {
        escucha_->alimentarContinuo(fuente, m, n, f);
        return;
    }
    if (modo != 1 || !tomando_) return;
    std::lock_guard<std::mutex> l(tomasLock_);
    Toma& t = tomas_[fuente];
    if (t.audio.size() > static_cast<std::size_t>(audio::Escucha::kMaxSegundos * f)) return;
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
    for (const auto& f : racks_->fuentesConTexto())
        if (f.value("modo", std::string()) == "continuo") ahora.insert(f.value("fuente", std::string()));
    for (const auto& c : continuas_)
        if (!ahora.contains(c)) escucha_->cerrarContinuo(c);
    for (const auto& c : ahora)
        if (!continuas_.contains(c)) escucha_->abrirContinuo(c);
    continuas_ = std::move(ahora);
#endif
}

QString Voz::microfono() const { return QString::fromStdString(rt_.config().operatorAudio); }

QString Voz::setMicrofono(const QString& fuente) {
    if (!rt_.setOperatorFeed(rt_.config().operatorVideo, fuente.toStdString()))
        return "No existe la fuente «" + fuente + "».";
    emit cambio();
    return {};
}

QVariantList Voz::fuentesTexto() const {
    QVariantList out;
#if defined(BA_HAVE_CLAP)
    if (racks_ != nullptr)
        for (const auto& f : racks_->fuentesConTexto())
            out.push_back(QVariantMap{{"fuente", QString::fromStdString(f.value("fuente", std::string()))},
                                      {"modo", QString::fromStdString(f.value("modo", std::string()))}});
#endif
    return out;
}

QString Voz::setTexto(const QString& fuente, const QString& modo) {
#if defined(BA_HAVE_CLAP)
    if (racks_ == nullptr) return "Esta compilación no tiene racks de audio.";
    if (modo.isEmpty()) {
        audio::AudioRack* r = racks_->rack(fuente.toStdString(), false);
        if (r == nullptr) return {};
        std::string e;
        r->remove("voz-a-texto", &e);
        racks_->markDirty();
        emit racks_->changed();
        return {};
    }
    if (modo != "dictado" && modo != "continuo") return "El modo es «dictado» o «continuo».";
    return QString::fromStdString(racks_->setTexto(fuente.toStdString(), modo.toStdString()));
#else
    Q_UNUSED(fuente);
    Q_UNUSED(modo);
    return "Esta compilación no tiene racks de audio.";
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
QString archivoAjustes(const std::filesystem::path& raiz) { return deRuta(raiz / "Modelos" / "voz.json"); }
} // namespace

void Voz::leerAjustes() {
    QFile f(archivoAjustes(rt_.dataRoot()));
    if (!f.open(QIODevice::ReadOnly)) return;
    const nlohmann::json j = nlohmann::json::parse(f.readAll().toStdString(), nullptr, false);
    if (!j.is_object()) return;
    vozElegida_ = QString::fromStdString(j.value("voz", std::string()));
    hablante_ = j.value("hablante", 0);
    velocidad_ = std::clamp(j.value("velocidad", 1.0), 0.7, 1.5);
}

void Voz::guardarAjustes() const {
    const nlohmann::json j{
        {"voz", vozElegida_.toStdString()}, {"hablante", hablante_}, {"velocidad", velocidad_}};
    const QString ruta = archivoAjustes(rt_.dataRoot());
    QDir().mkpath(QFileInfo(ruta).absolutePath());
    QFile f(ruta);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QByteArray::fromStdString(j.dump(1)));
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
    habla_->cargar(v, [this](const std::string& error) {
        QMetaObject::invokeMethod(this, [this, error]() {
            vozLista_ = habla_->cargada();
            hablantes_ = habla_->hablantes();
            if (hablante_ >= hablantes_) hablante_ = 0;
            if (!error.empty()) emit notify("La voz no cargó: " + QString::fromStdString(error), "warn");
            emit cambio();
        });
    });
#endif
}

void Voz::elegirVoz(const QString& repo) {
    if (repo == vozElegida_) return;
    if (modelos_.carpetaInstalada(repo).empty()) {
        emit notify("Primero hay que bajar esa voz.", "warn");
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
        emit notify("No hay una voz cargada: bajá una en Configuración → Modelos locales.", "warn");
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
    decir("Hola, soy la voz del asistente. Esto solo lo escuchás vos, por los auriculares.");
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
