// ============================================================================
// src/app/agent/Voz.cpp — ver Voz.hpp
// ============================================================================
#include "agent/Voz.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>

#include <algorithm>

#include <nlohmann/json.hpp>

#include "agent/ModelosLocales.hpp"
#include "AppRuntime.hpp"
#include "audio/MonitorVoz.hpp"
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
    if (!feed.hayMicrofono()) {
        emit notify("No hay micrófono del operador: elegilo en Sala (la voz del operador).", "warn");
        return;
    }
    callar(); // el operador habla: el asistente se calla
    escucha_->empezar();
    audio::Escucha* e = escucha_.get();
    feed.setDictado([e](const float* m, std::size_t n, std::uint32_t f) { e->alimentar(m, n, f); });
    feed.setPushToTalk(true); // desde acá el lobby ya no oye al operador
    grabando_ = true;
    setEstado("Escuchando… (soltá para mandar)");
#endif
}

void Voz::soltar() {
#if defined(BA_HAVE_VOZ)
    if (!grabando_) return;
    OperatorFeed& feed = rt_.operatorFeed();
    feed.setPushToTalk(false); // el lobby vuelve a oírlo
    feed.setDictado({});
    grabando_ = false;
    transcribiendo_ = true;
    setEstado("Transcribiendo…");
    escucha_->terminar([this](const audio::Escucha::Resultado& r) {
        QMetaObject::invokeMethod(this, [this, r]() {
            transcribiendo_ = false;
            if (!r.error.empty()) {
                setEstado("Lista · " + QString::fromStdString(r.error));
                emit notify("Voz: " + QString::fromStdString(r.error), "warn");
                return;
            }
            ultimo_ = QString::fromStdString(r.texto);
            setEstado(QString("Lista · %1 s de habla en %2 ms")
                          .arg(r.segundosDeAudio, 0, 'f', 1)
                          .arg(r.ms, 0, 'f', 0));
            emit dictado(ultimo_);
        });
    });
#endif
}

void Voz::cancelar() {
#if defined(BA_HAVE_VOZ)
    if (!grabando_) return;
    rt_.operatorFeed().setPushToTalk(false);
    rt_.operatorFeed().setDictado({});
    escucha_->cancelar();
    grabando_ = false;
    setEstado("Lista · cancelado");
#endif
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
