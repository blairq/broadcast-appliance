// src/app/ui/MezclaViewModel.cpp — ver el .hpp

#include "ui/MezclaViewModel.hpp"

#include "arranque/AppRuntime.hpp"
#include "audio/AudioViewModel.hpp"
#include "audio/Calibracion.hpp"
#include "audio/Matriz.hpp"
#include "audio/Mezclador.hpp"
#include "audio/RackManager.hpp"
#include "ui/ListModels.hpp" // MixerModel

#include <QAbstractItemModel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QTimer>

#include <algorithm>
#include <cstring>
#include <set>

namespace ba::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

/// Sin padre y devuelto a QML: que no se lo quede su recolector. Es del área.
QObject* deCpp(QObject* o) {
    if (o != nullptr) QQmlEngine::setObjectOwnership(o, QQmlEngine::CppOwnership);
    return o;
}

} // namespace

MezclaViewModel::MezclaViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    AppRuntime& rt = p_.rt;
    // Racks de audio (LSP) de cada fuente: se montan los guardados.
    racks_ = std::make_unique<audio::RackManager>(rt.dataRoot());
    racks_->restore();
    audio_ = std::make_unique<audio::AudioViewModel>(*racks_);
    // Una entrada de audio nueva (o una que se va), la agregue quien la agregue (la
    // interfaz, el asistente, una receta): la lista del modelo de Audio se rehace sola.
    // El mezclador de las tarjetas la nota en su vuelta de 100 ms (reload = modelReset).
    connect(&p_.tiras, &QAbstractItemModel::modelReset, audio_.get(), [this]() { audio_->refresh(); });
    matriz_ = std::make_unique<audio::Matriz>(rt.dataRoot());
    // El ruteo (escucha, pistas) es del motor: cuando cambia, por el camino que sea,
    // la matriz avisa a sus vistas y el PFL de la tarjeta y del Mezclador se entera.
    // El audio de cada fuente es del motor: cuando cambia, por el camino que sea, la
    // matriz lo anota y guarda (y se lo pone a una fuente que nace), y el mezclador
    // refresca esa tira. La matriz solo se redibuja si cambió el ruteo.
    rt.engine().alCambiarAudioFuente([m = matriz_.get(), mx = &p_.tiras](const std::string& f, const char* que) {
        m->avisarFuente(f, que);
        if (std::strcmp(que, "alta") != 0) mx->avisarCambio(f);
    });
    connect(matriz_.get(), &audio::Matriz::cambio, this, &MezclaViewModel::pflChanged);
    // El Mezclador (la ventana propia): niveles de entradas y salidas, y su estado.
    mezclador_ = std::make_unique<audio::Mezclador>(rt, matriz_.get());
    mezclador_->setSalida(p_.salida);
    mezclador_->setRacks(racks_.get());
    // Un barrido de calibración nunca con la emisión o la grabación en curso.
    mezclador_->calibracion()->setAlAire(p_.alAire);
    // Una entrada de placa se abre si la trae la tarjeta «Entrada» de un rack, o si su
    // propio rack tiene algo (un filtro, voz a texto): sin salida en la matriz igual
    // tiene que llegarles el audio.
    matriz_->setUsadasPorOtros([r = racks_.get()]() {
        std::set<std::string> s = r->fuentesDeEntradas();
        for (auto& f : r->fuentesConRack()) s.insert(f);
        return s;
    });
}

// El aviso del motor (alCambiarAudioFuente) lo saca la raíz al irse, antes de que se vaya
// nadie: también apunta a sus tiras.
MezclaViewModel::~MezclaViewModel() = default;

QObject* MezclaViewModel::audioQml() const { return deCpp(audio_.get()); }
QObject* MezclaViewModel::matrizQml() const { return deCpp(matriz_.get()); }
QObject* MezclaViewModel::mezcladorQml() const { return deCpp(mezclador_.get()); }

void MezclaViewModel::setSalida(const std::string& salida) { mezclador_->setSalida(salida); }

void MezclaViewModel::revisarPistas() {
    // Las pistas que graba el programa (T29) quedan rotuladas en la Matriz.
    const int mask = p_.rt.engine().pistasGrabacionMask();
    for (int i = 1; i < audio::Matriz::kPistas; ++i)
        matriz_->setUsoPista(i, (mask & (1 << i)) ? tr("grabación del programa").toStdString() : std::string());
}

void MezclaViewModel::revisarFuentes() { audio_->revisarFuentes(); }

void MezclaViewModel::vigilarModulos() {
    for (const auto& m : racks_->tomarSaltados())
        emit notify(tr("Audio: «%1» se saltó por lento (pasa sin procesar). Rearmalo en Audio cuando esté bien.").arg(qs(m)),
                    "warn");
}

QString MezclaViewModel::balistica() const { return qs(p_.rt.config().balistica); }

void MezclaViewModel::setBalistica(const QString& b) {
    const std::string v = b.toStdString();
    if ((v != "pico" && v != "ppm" && v != "vu") || v == p_.rt.config().balistica) return;
    p_.rt.config().balistica = v;
    p_.rt.save();
    emit balisticaChanged();
}

QString MezclaViewModel::pfl() const { return qs(p_.rt.engine().pfl()); }

QString MezclaViewModel::pflModo() const { return qs(p_.rt.engine().pflModo()); }

QString MezclaViewModel::salidaEscucha() const { return qs(p_.rt.engine().salidaEscucha()); }

void MezclaViewModel::setSalidaEscucha(const QString& salida) {
    // El aviso llega por la matriz (pflChanged).
    if (const QString e = matriz_->ponerSalidaEscucha(salida); !e.isEmpty()) emit notify(e, "error");
}

QString MezclaViewModel::pflUi(const QString& fuente) {
    // Tocar la que está en PFL la apaga. Uno solo a la vez y sin tocar la escucha fija
    // (Matriz::ponerPfl); el aviso llega por el motor (pflChanged).
    return matriz_->ponerPfl(pfl() == fuente && pflModo() == "pfl" ? QString() : fuente);
}

QString MezclaViewModel::aflUi(const QString& fuente) {
    // Como pflUi, con el fader de la salida que muestra el Mezclador.
    const QString salida = qs(mezclador_->salida());
    return matriz_->ponerPfl(pfl() == fuente && pflModo() == "afl" ? QString() : fuente, QStringLiteral("afl"), salida);
}

int MezclaViewModel::retardoUi(const QString& fuente) const { return p_.rt.engine().sourceSyncOffset(fuente.toStdString()); }

QString MezclaViewModel::setRetardoUi(const QString& fuente, int ms) {
    if (!p_.rt.engine().setSourceSyncOffset(fuente.toStdString(), std::clamp(ms, -5000, 5000)))
        return qs(p_.rt.engine().lastError());
    p_.rt.save();
    return {};
}

QString MezclaViewModel::medirSincro(int segundos, std::function<void(const nlohmann::json&)> listo) {
    BroadcastEngine& motor = p_.rt.engine();
    if (motor.midiendoSincro()) return tr("ya se está midiendo");
    if (!motor.empezarMedicionSincro()) return qs(motor.lastError());
    QTimer::singleShot(std::clamp(segundos, 4, 30) * 1000, this, [&motor, listo = std::move(listo)]() {
        listo(motor.terminarMedicionSincro());
    });
    return {};
}

QString MezclaViewModel::medirSincroUi(int segundos) {
    return medirSincro(segundos, [this](const nlohmann::json& r) {
        emit medicionSincroLista(QJsonDocument::fromJson(QByteArray::fromStdString(r.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap());
    });
}

} // namespace ba::app
