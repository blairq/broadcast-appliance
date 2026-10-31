// src/app/ui/EmisionViewModel.cpp — ver el .hpp

#include "ui/EmisionViewModel.hpp"

#include "arranque/AppRuntime.hpp"
#include "audio/ObjetivoLoudness.hpp"
#include "control/BusEventos.hpp"
#include "emision/RetornosInvitados.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace ba::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

QVariantMap mapaDe(const nlohmann::json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        .object()
        .toVariantMap();
}

} // namespace

EmisionViewModel::EmisionViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    // Los retornos y el objetivo de loudness son del modelo: acá se muestran.
    retornos_ = std::make_unique<emision::RetornosInvitados>(p_.rt);
    objetivoLoudness_ = std::make_unique<audio::ObjetivoLoudness>(p_.rt.objetivoLoudness());
}

EmisionViewModel::~EmisionViewModel() = default;

void EmisionViewModel::toggleRecording() {
    const bool grabando = p_.grabando && p_.grabando();
    if (!grabando) {
        // Antes de grabar: cuánto alcanza el disco a este bitrate.
        const nlohmann::json d = p_.rt.discoGrabacion();
        const double horas = d.value("horas", -1.0);
        if (horas >= 0 && horas < AppRuntime::kDiscoMinimoH) {
            emit pedirConfirmarGrabacion(horas);
            return;
        }
        if (horas >= 0 && horas < 2) emit notify(tr("Ojo: el disco alcanza para ~%1 h de grabación").arg(horas, 0, 'f', 1), "warn");
    }
    if (grabando) {
        p_.rt.stopRecording();
    } else if (p_.rt.startRecording()) {
        emit notify(tr("Grabando en %1").arg(qs(p_.rt.carpetaGrabacion())), "ok");
    } else {
        emit notify(tr("No se pudo grabar: %1").arg(qs(p_.rt.lastError())), "error");
    }
    emit refrescar();
}

void EmisionViewModel::grabarIgual() {
    if (p_.grabando && p_.grabando()) return;
    if (p_.rt.startRecording())
        emit notify(tr("Grabando (con poco disco)"), "warn");
    else
        emit notify(tr("No se pudo grabar: %1").arg(qs(p_.rt.lastError())), "error");
    emit refrescar();
}

void EmisionViewModel::startStreaming(const QString& server, const QString& key) {
    BroadcastEngine& motor = p_.rt.engine();
    if (p_.rt.salirAlAireManual(server.toStdString(), key.toStdString())) {
        // Nunca un "Al aire" verde si no cambió nada.
        if (motor.ultimoInicio() == BroadcastEngine::Inicio::YaIgual)
            emit notify(tr("Ya estabas transmitiendo a ese destino: no cambió nada"), "info");
        else
            emit notify(tr("Al aire"), "ok");
    } else if (motor.ultimoInicio() == BroadcastEngine::Inicio::YaOtroDestino) {
        emit pedirCambioDestino(qs(motor.destinoActivo()), server, key);
    } else {
        emit notify(tr("No se pudo transmitir: %1").arg(qs(p_.rt.lastError())), "error");
    }
    emit refrescar();
}

void EmisionViewModel::cambiarDestinoStream(const QString& server, const QString& key) {
    if (p_.rt.salirAlAireManual(server.toStdString(), key.toStdString(), true))
        emit notify(tr("Transmitiendo al destino nuevo"), "ok");
    else
        emit notify(tr("No se pudo cambiar de destino: %1").arg(qs(p_.rt.lastError())), "error");
    emit refrescar();
}

void EmisionViewModel::stopStreaming() {
    p_.rt.stopStreaming();
    emit refrescar();
}

QVariantList EmisionViewModel::destinosUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(p_.rt.destinos().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).array().toVariantList();
}

QString EmisionViewModel::guardarDestinoUi(const QVariantMap& d) {
    const auto j = nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(d)).toJson().toStdString(), nullptr, false);
    const std::string e = p_.rt.guardarDestino(j);
    if (e.empty()) emit destinosChanged();
    return qs(e);
}

QString EmisionViewModel::borrarDestinoUi(const QString& id) {
    const std::string e = p_.rt.borrarDestino(id.toStdString());
    if (e.empty()) emit destinosChanged();
    return qs(e);
}

QString EmisionViewModel::iniciarDestinoUi(const QString& id) {
    const std::string e = p_.rt.iniciarDestino(id.toStdString());
    emit destinosChanged();
    return qs(e);
}

void EmisionViewModel::detenerDestinoUi(const QString& id) {
    p_.rt.detenerDestino(id.toStdString());
    emit destinosChanged();
}

QVariantMap EmisionViewModel::discoUi() const { return mapaDe(p_.rt.discoGrabacion()); }

QVariantMap EmisionViewModel::remuxUi() const { return mapaDe(p_.rt.engine().remuxEstado()); }

QVariantMap EmisionViewModel::apurosDiscoUi() const { return mapaDe(p_.rt.engine().discoEstado()); }

void EmisionViewModel::priorizarProgramaUi(bool si) { p_.rt.engine().priorizarPrograma(si); }

QString EmisionViewModel::modoDiscoUi(const QString& modo) { return qs(p_.rt.engine().setModoDisco(modo.toStdString())); }

QVariantList EmisionViewModel::isosUi() const {
    QVariantList out;
    // El tamaño, de lo que contó el motor: preguntarle al disco desde la
    // interfaz (cada segundo) la trababa con el disco trabado.
    for (const auto& g : p_.rt.engine().sourceRecordings())
        out.append(QVariantMap{{"fuente", qs(g.source)},
                               {"archivo", qs(g.path)},
                               {"mb", static_cast<double>(g.bytes) / 1e6},
                               {"audioPropio", g.audioPropio},
                               {"activa", g.activa},
                               {"pausada", g.pausada}});
    return out;
}

void EmisionViewModel::isoInvitadoUi(const QString& id) {
    const QString f = fuenteDeInvitado(id);
    if (f.isEmpty()) return emit notify(tr("Ese invitado no tiene una fuente en el programa todavía."), "warn");
    const QString e = isoUi(f, true);
    emit notify(e.isEmpty() ? tr("Grabando «%1» aparte").arg(f) : e, e.isEmpty() ? "ok" : "error");
}

QString EmisionViewModel::isoUi(const QString& fuente, bool grabar) {
    if (!grabar) {
        p_.rt.stopSourceRecording(fuente.toStdString());
        emit estadoCambio();
        return {};
    }
    const std::string e = p_.rt.grabarAparte(fuente.toStdString()); // las reglas son del modelo
    if (!e.empty()) return qs(e);
    emit estadoCambio();
    return {};
}

QString EmisionViewModel::fuenteDeInvitado(const QString& participante) const {
    const QJsonObject snap = p_.sala.client().snapshot();
    for (const QJsonValue& v : snap.value("participants").toArray()) {
        const QJsonObject pa = v.toObject();
        if (pa.value("id").toString() != participante && pa.value("name").toString() != participante) continue;
        const int slot = pa.value("slot").toInt();
        if (slot <= 0) return {};
        const QString f = "Invitado " + QString::number(slot);
        for (const std::string& n : p_.rt.engine().sourceNames())
            if (qs(n) == f) return f;
        return {};
    }
    return {};
}

QString EmisionViewModel::isoTodasUi(bool grabar) {
    if (!grabar) {
        p_.rt.stopAllSourceRecordings();
        emit estadoCambio();
        return {};
    }
    QStringList errores;
    BroadcastEngine& motor = p_.rt.engine();
    for (const std::string& f : motor.sceneSources(motor.currentScene())) {
        bool ya = false;
        for (const auto& g : motor.sourceRecordings()) ya = ya || g.source == f;
        if (ya) continue;
        const QString e = isoUi(qs(f), true);
        if (!e.isEmpty()) errores << qs(f) + ": " + e;
    }
    return errores.join("; ");
}

QVariantMap EmisionViewModel::limpiaUi() const { return mapaDe(p_.rt.engine().cleanFeedEstado()); }

QString EmisionViewModel::limpiaActivarUi(bool activar, const QString& destino) {
    if (!activar) {
        p_.rt.detenerSenalLimpia();
        return {};
    }
    return qs(p_.rt.iniciarSenalLimpia(destino.toStdString()));
}

QVariantMap EmisionViewModel::grabacionUi() const {
    nlohmann::json j = p_.rt.engine().grabacionEstado();
    j["replay"] = p_.rt.engine().replayEstado();
    return mapaDe(j);
}

QVariantMap EmisionViewModel::loudnessUi(const QString& salida) const {
    return mapaDe(p_.rt.engine().loudness(salida.toStdString()).json());
}

void EmisionViewModel::resetLoudnessUi(const QString& salida) { p_.rt.engine().loudness(salida.toStdString()).reiniciar(); }

QString EmisionViewModel::objetivoLoudnessUi(const QVariantMap& o, const QString& salida) {
    const auto j = nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(o)).toJson().toStdString(), nullptr, false);
    return qs(objetivoLoudness_->poner(j, salida.toStdString()));
}

QString EmisionViewModel::retornoInvitadoUi(int slot) const { return qs(retornos_->modo(slot)); }

QString EmisionViewModel::setRetornoInvitadoUi(int slot, const QString& modo) {
    return qs(retornos_->poner(slot, modo.toStdString()));
}

void EmisionViewModel::muestrearSalud(control::BusEventos& bus) {
    const nlohmann::json h = p_.rt.engine().saludSalidas();
    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    const std::uint64_t saltados = h.value("saltados_encoder", 0ULL);
    const int saltadosNuevos = static_cast<int>(saltados >= saltadosAntes_ ? saltados - saltadosAntes_ : 0);
    saltadosAntes_ = saltados;
    nlohmann::json actual = nlohmann::json::array();
    const auto uno = [&](const std::string& id, const nlohmann::json& o) {
        Salud& s = salud_[id];
        const bool activo = o.value("activo", false);
        const std::uint64_t bytes = o.value("bytes", 0ULL);
        const int perdidos = o.value("perdidos", 0);
        const int kbps = activo && bytes >= s.bytesAntes ? static_cast<int>((bytes - s.bytesAntes) * 8 / 1000) : 0;
        const int perdidosNuevos = perdidos >= s.perdidosAntes ? perdidos - s.perdidosAntes : 0;
        s.bytesAntes = bytes;
        s.perdidosAntes = perdidos;
        const bool rec = o.value("reconectando", false);
        if (rec && !s.reconectando) {
            ++s.reconexiones;
            s.reconectandoDesde = ahora;
        }
        if (!rec && s.reconectando && s.reconectandoDesde > 0) s.reconexionMs = ahora - s.reconectandoDesde;
        s.reconectando = rec;
        const nlohmann::json punto{{"ms", ahora}, {"kbps", kbps}, {"perdidos_red", perdidosNuevos},
                                   {"perdidos_enc", saltadosNuevos}, {"congestion", o.value("congestion", 0.0)}};
        if (activo) {
            s.serie.push_back(punto);
            while (s.serie.size() > 600) s.serie.pop_front();
        }
        nlohmann::json hist = nlohmann::json::array();
        const std::size_t desde = s.serie.size() > 60 ? s.serie.size() - 60 : 0;
        for (std::size_t i = desde; i < s.serie.size(); ++i) hist.push_back(s.serie[i].value("kbps", 0));
        actual.push_back({{"id", id},
                          {"activo", activo},
                          {"kbps", kbps},
                          {"perdidos_red", perdidos},
                          {"perdidos_enc", saltados},
                          {"congestion", std::round(o.value("congestion", 0.0) * 100) / 100},
                          {"reconectando", rec},
                          {"reconexiones", s.reconexiones},
                          {"ultima_reconexion_ms", s.reconexionMs},
                          {"kbps_historial", hist}});
    };
    uno("principal", h.value("principal", nlohmann::json::object()));
    const nlohmann::json extras = h.value("extras", nlohmann::json::object()); // .items() de un temporal queda colgado
    for (const auto& [id, o] : extras.items()) uno(id, o);
    if (actual != saludActual_) {
        saludActual_ = actual;
        emit saludChanged();
    }
    bus.publicar("salida/salud", saludActual_);
}

QVariantList EmisionViewModel::saludSalidas() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(nlohmann::json{{"v", saludActual_}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        .object()
        .value("v")
        .toArray()
        .toVariantList();
}

nlohmann::json EmisionViewModel::saludSerie(const std::string& destino, int minutos) const {
    auto it = salud_.find(destino);
    if (it == salud_.end()) return nlohmann::json::array();
    const std::size_t n = static_cast<std::size_t>(std::clamp(minutos, 1, 10) * 60);
    nlohmann::json out = nlohmann::json::array();
    const auto& serie = it->second.serie;
    for (std::size_t i = serie.size() > n ? serie.size() - n : 0; i < serie.size(); ++i) out.push_back(serie[i]);
    return out;
}

} // namespace ba::app
