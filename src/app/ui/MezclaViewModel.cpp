// src/app/ui/MezclaViewModel.cpp — ver el .hpp

#include "ui/MezclaViewModel.hpp"

#include "arranque/AppRuntime.hpp"
#include "audio/AudioViewModel.hpp"
#include "audio/Calibracion.hpp"
#include "audio/Matriz.hpp"
#include "audio/Mezclador.hpp"
#include "audio/RackManager.hpp"
#include "emision/VentanillaMotor.hpp"
#include "i18n/I18n.hpp"
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

QVariantMap aMapa(const nlohmann::json& r) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(r.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        .object()
        .toVariantMap();
}

} // namespace

MezclaViewModel::MezclaViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    AppRuntime& rt = p_.rt;
    // Racks de audio (LSP) de cada fuente: se montan los guardados. Qué fuentes hay y de qué
    // tipo, de la foto del motor.
    racks_ = std::make_unique<audio::RackManager>(rt.dataRoot());
    racks_->setFoto([v = &p_.ventanilla]() { return v->fotoCompartida(); },
                    [v = &p_.ventanilla](std::function<void()> orden) { v->pedir("rack_en_fuente", std::move(orden)); });
    racks_->restore();
    audio_ = std::make_unique<audio::AudioViewModel>(*racks_);
    // Una entrada de audio nueva (o una que se va), la agregue quien la agregue (la
    // interfaz, el asistente, una receta): la lista del modelo de Audio se rehace sola.
    // Las tiras la notan con la foto (reload = modelReset).
    connect(&p_.tiras, &QAbstractItemModel::modelReset, audio_.get(), [this]() { audio_->refresh(); });
    // El ruteo (escucha, pistas) y el audio de cada fuente son del motor: llegan con la foto
    // (la parte Mezcla), cambien por el camino que sea; la matriz los anota y guarda (y se
    // los pone a una fuente que nace) y avisa a sus vistas. Lo que cambia, lo pide.
    matriz_ = std::make_unique<audio::Matriz>(rt.dataRoot(), &rt, &p_.ventanilla);
    connect(matriz_.get(), &audio::Matriz::cambio, this, &MezclaViewModel::pflChanged);
    connect(matriz_.get(), &audio::Matriz::aviso, this, [this](const QString& e) { emit notify(e, "error"); });
    connect(&p_.ventanilla, &VentanillaMotor::cambio, this, &MezclaViewModel::alCambiarFoto);
    // El Mezclador (la ventana propia): niveles de entradas y salidas, y su estado.
    mezclador_ = std::make_unique<audio::Mezclador>(rt, p_.ventanilla, matriz_.get());
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
    alCambiarFoto(FotoMotor::Mezcla);
}

MezclaViewModel::~MezclaViewModel() = default;

QObject* MezclaViewModel::audioQml() const { return deCpp(audio_.get()); }
QObject* MezclaViewModel::matrizQml() const { return deCpp(matriz_.get()); }
QObject* MezclaViewModel::mezcladorQml() const { return deCpp(mezclador_.get()); }

void MezclaViewModel::setSalida(const std::string& salida) { mezclador_->setSalida(salida); }

void MezclaViewModel::alCambiarFoto(unsigned partes) {
    if ((partes & FotoMotor::Mezcla) == 0) return;
    const FotoMotor::MezclaAudio& m = p_.ventanilla.foto().mezcla;
    // Las pistas que graba el programa (T29) quedan rotuladas en la Matriz.
    for (int i = 1; i < audio::Matriz::kPistas; ++i)
        matriz_->setUsoPista(i, (m.pistasGrabacion & (1 << i)) ? tr("grabación del programa").toStdString() : std::string());
    if (balisticaPedida_.empty()) emit balisticaChanged();
}

void MezclaViewModel::revisarFuentes() { audio_->revisarFuentes(); }

void MezclaViewModel::vigilarModulos() {
    for (const auto& m : racks_->tomarSaltados())
        emit notify(tr("Audio: «%1» se saltó por lento (pasa sin procesar). Rearmalo en Audio cuando esté bien.").arg(qs(m)),
                    "warn");
}

QString MezclaViewModel::balistica() const {
    return qs(!balisticaPedida_.empty() ? balisticaPedida_ : p_.ventanilla.foto().mezcla.balistica);
}

void MezclaViewModel::setBalistica(const QString& b) {
    const std::string v = b.toStdString();
    if ((v != "pico" && v != "ppm" && v != "vu") || v == balistica().toStdString()) return;
    // Se ve enseguida (pendiente) y se pide: la configuración es del dueño.
    balisticaPedida_ = v;
    emit balisticaChanged();
    p_.ventanilla.pedir(
        this, "mezcla_balistica",
        [&rt = p_.rt, v]() {
            rt.config().balistica = v;
            rt.save();
            rt.fotoSucia(FotoMotor::Mezcla);
        },
        [this]() {
            balisticaPedida_.clear();
            emit balisticaChanged();
        });
}

QString MezclaViewModel::pfl() const { return qs(p_.ventanilla.foto().mezcla.pfl); }

QString MezclaViewModel::pflModo() const { return qs(p_.ventanilla.foto().mezcla.pflModo); }

QString MezclaViewModel::salidaEscucha() const { return qs(p_.ventanilla.foto().mezcla.salidaEscucha); }

void MezclaViewModel::setSalidaEscucha(const QString& salida) {
    // El aviso llega por la matriz (pflChanged).
    if (const QString e = matriz_->ponerSalidaEscucha(salida); !e.isEmpty()) emit notify(e, "error");
}

QString MezclaViewModel::pflUi(const QString& fuente) {
    // Tocar la que está en PFL la apaga. Uno solo a la vez y sin tocar la escucha fija
    // (Matriz::ponerPfl); el aviso llega con la foto (pflChanged).
    return matriz_->ponerPfl(pfl() == fuente && pflModo() == "pfl" ? QString() : fuente);
}

QString MezclaViewModel::aflUi(const QString& fuente) {
    // Como pflUi, con el fader de la salida que muestra el Mezclador.
    const QString salida = qs(mezclador_->salida());
    return matriz_->ponerPfl(pfl() == fuente && pflModo() == "afl" ? QString() : fuente, QStringLiteral("afl"), salida);
}

int MezclaViewModel::retardoUi(const QString& fuente) const {
    const FotoMotor::CanalAudio* c = p_.ventanilla.foto().mezcla.canal(fuente.toStdString());
    return c != nullptr ? c->retardoMs : 0;
}

QString MezclaViewModel::setRetardoUi(const QString& fuente, int ms) {
    const std::string f = fuente.toStdString();
    if (p_.ventanilla.foto().tipoDe(f).empty()) return tr("No existe la fuente «%1».").arg(fuente);
    p_.ventanilla.pedir(
        this, "mezcla_retardo",
        [&rt = p_.rt, f, ms = std::clamp(ms, -5000, 5000)]() -> std::string {
            BroadcastEngine& e = rt.engine();
            if (!e.setSourceSyncOffset(f, ms)) return e.lastError();
            rt.save();
            return {};
        },
        [this](const std::string& e) {
            if (!e.empty()) emit notify(qs(e), "error");
        });
    return {};
}

QString MezclaViewModel::medirSincro(int segundos, std::function<void(const nlohmann::json&)> listo,
                                     std::function<void(const QString&)> fallo, const std::string& salida,
                                     const std::string& aplicarA) {
    if (p_.ventanilla.foto().mezcla.midiendoSincro) return tr("ya se está midiendo");
    // Empieza en el dueño; los segundos se cuentan acá (la interfaz trabada solo atrasa el
    // final: lo medido es lo que salió mientras tanto) y termina en el dueño.
    p_.ventanilla.pedir(
        this, "mezcla_sincro_empezar",
        [&rt = p_.rt, salida]() -> std::string {
            BroadcastEngine& e = rt.engine();
            if (e.midiendoSincro()) return ba::i18n::traducir("audio", "ya se está midiendo");
            if (!e.empezarMedicionSincro(salida)) return e.lastError();
            rt.fotoSucia(FotoMotor::Mezcla);
            return {};
        },
        [this, segundos, listo = std::move(listo), fallo = std::move(fallo), aplicarA](const std::string& e) mutable {
            if (!e.empty()) {
                if (fallo) fallo(qs(e));
                return;
            }
            QTimer::singleShot(std::clamp(segundos, 4, 30) * 1000, this,
                               [this, listo = std::move(listo), aplicarA]() mutable { terminarSincro(std::move(listo), aplicarA); });
        });
    return {};
}

void MezclaViewModel::terminarSincro(std::function<void(const nlohmann::json&)> listo, const std::string& aplicarA) {
    p_.ventanilla.pedir(
        this, "mezcla_sincro_terminar",
        [&rt = p_.rt, aplicarA]() {
            BroadcastEngine& e = rt.engine();
            nlohmann::json r = e.terminarMedicionSincro();
            rt.fotoSucia(FotoMotor::Mezcla);
            // Con una fuente y una medición confiable, se le suma lo medido a lo que tenía.
            if (!aplicarA.empty() && r.value("confiable", false)) {
                const int nuevo = std::clamp(e.sourceSyncOffset(aplicarA) + r.value("retardo_ms", 0), -5000, 5000);
                if (e.setSourceSyncOffset(aplicarA, nuevo)) {
                    rt.save();
                    r["aplicado"] = {{"fuente", aplicarA}, {"ms", nuevo}};
                } else {
                    r["aplicado_error"] = e.lastError();
                }
            } else if (!aplicarA.empty()) {
                r["aplicado"] = false;
            }
            return r;
        },
        [listo = std::move(listo)](const nlohmann::json& r) {
            if (listo) listo(r);
        });
}

QString MezclaViewModel::medirSincroUi(int segundos, const QString& salida) {
    return medirSincro(
        segundos, [this](const nlohmann::json& r) { emit medicionSincroLista(aMapa(r)); },
        [this](const QString& e) { emit medicionSincroLista(aMapa({{"ok", false}, {"problema", e.toStdString()}})); },
        salida.toStdString());
}

} // namespace ba::app
