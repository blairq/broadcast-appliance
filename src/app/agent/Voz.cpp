// ============================================================================
// src/app/agent/Voz.cpp — ver Voz.hpp
// ============================================================================
#include "agent/Voz.hpp"

#include <QCoreApplication>
#include <QKeyEvent>

#include "agent/ModelosLocales.hpp"
#include "AppRuntime.hpp"
#if defined(BA_HAVE_VOZ)
#include "audio/Escucha.hpp"
#endif

namespace ba::app::agent {

namespace {
QString deRuta(const std::filesystem::path& p) { return QString::fromStdWString(p.wstring()); }
} // namespace

Voz::Voz(AppRuntime& rt, ModelosLocales& modelos, QObject* parent)
    : QObject(parent), rt_(rt), modelos_(modelos) {
#if defined(BA_HAVE_VOZ)
    escucha_ = std::make_unique<audio::Escucha>();
    connect(&modelos_, &ModelosLocales::cambio, this, &Voz::revisarModelos);
    qApp->installEventFilter(this);
    revisarModelos();
#else
    estado_ = "Esta compilación no trae el motor de voz (make deps-sherpa-onnx).";
#endif
}

Voz::~Voz() {
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
