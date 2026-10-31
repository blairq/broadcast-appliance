// src/app/ui/EmisionViewModel.cpp — ver el .hpp

#include "ui/EmisionViewModel.hpp"

#include "arranque/AppRuntime.hpp"
#include "audio/ObjetivoLoudness.hpp"
#include "control/BusEventos.hpp"
#include "emision/RetornosInvitados.hpp"
#include "emision/VentanillaMotor.hpp"
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

QVariantList listaDe(const nlohmann::json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(nlohmann::json{{"v", j}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        .object()
        .value("v")
        .toArray()
        .toVariantList();
}

/// Cómo salió pedir grabar: si hay que confirmar por el disco, con cuántas horas.
struct Grabar {
    bool confirmar = false, ok = false;
    double horas = -1;
    std::string carpeta, error;
};

/// Cómo salió salir al aire (BroadcastEngine::ultimoInicio).
struct Transmitir {
    bool ok = false;
    BroadcastEngine::Inicio inicio{};
    std::string activo, error;
};

} // namespace

EmisionViewModel::EmisionViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    // Los retornos y el objetivo de loudness son del modelo (los usa el dueño, por la
    // ventanilla y desde el MCP): acá se muestran de la foto.
    retornos_ = std::make_unique<emision::RetornosInvitados>(p_.rt);
    objetivoLoudness_ = std::make_unique<audio::ObjetivoLoudness>(p_.rt.objetivoLoudness());
    connect(&p_.ventanilla, &VentanillaMotor::cambio, this, &EmisionViewModel::alCambiar);
}

EmisionViewModel::~EmisionViewModel() = default;

void EmisionViewModel::alCambiar(unsigned partes) {
    if ((partes & FotoMotor::Emision) == 0) return;
    emit emisionCambio();
    // La LISTA de destinos (no su estado de cada vuelta): la fila de cada salida la muestra.
    std::uint64_t firma = 0;
    for (const auto& d : p_.ventanilla.foto().destinos)
        firma = firma * 31 + std::hash<std::string>{}(d.value("id", std::string()) + d.value("nombre", std::string()) +
                                                       d.value("salida", std::string()) + (d.value("respaldo", false) ? "r" : ""));
    if (firma != destinosAntes_) {
        destinosAntes_ = firma;
        emit destinosChanged();
    }
}

void EmisionViewModel::pedir(const char* que, std::function<std::string(AppRuntime&)> orden, std::function<void()> alVolver,
                             const QString& nivel) {
    p_.ventanilla.pedir(
        this, que, [&rt = p_.rt, orden = std::move(orden)]() { return orden(rt); },
        [this, nivel, alVolver = std::move(alVolver)](const std::string& e) {
            if (!e.empty()) emit notify(qs(e), nivel);
            else if (alVolver) alVolver();
            emit refrescar();
        });
}

void EmisionViewModel::toggleRecording() {
    const bool grabando = p_.grabando && p_.grabando();
    if (grabando) {
        pedir("grabar_parar", [](AppRuntime& rt) {
            rt.stopRecording();
            return std::string();
        });
        return;
    }
    // Antes de grabar: cuánto alcanza el disco a este bitrate (se mira en el dueño, que lee
    // el disco; con poco, vuelve a pedir que el operador confirme).
    p_.ventanilla.pedir(
        this, "grabar",
        [&rt = p_.rt]() {
            Grabar g;
            g.horas = rt.discoGrabacion().value("horas", -1.0);
            if (g.horas >= 0 && g.horas < AppRuntime::kDiscoMinimoH) {
                g.confirmar = true;
                return g;
            }
            g.ok = rt.startRecording();
            g.carpeta = rt.carpetaGrabacion();
            if (!g.ok) g.error = rt.lastError();
            return g;
        },
        [this](const Grabar& g) {
            if (g.confirmar) return emit pedirConfirmarGrabacion(g.horas);
            if (!g.ok) {
                emit notify(tr("No se pudo grabar: %1").arg(qs(g.error)), "error");
            } else {
                if (g.horas >= 0 && g.horas < 2) emit notify(tr("Ojo: el disco alcanza para ~%1 h de grabación").arg(g.horas, 0, 'f', 1), "warn");
                emit notify(tr("Grabando en %1").arg(qs(g.carpeta)), "ok");
            }
            emit refrescar();
        });
}

void EmisionViewModel::grabarIgual() {
    if (p_.grabando && p_.grabando()) return;
    p_.ventanilla.pedir(
        this, "grabar_igual",
        [&rt = p_.rt]() { return rt.startRecording() ? std::string() : rt.lastError().empty() ? std::string("?") : rt.lastError(); },
        [this](const std::string& e) {
            if (e.empty())
                emit notify(tr("Grabando (con poco disco)"), "warn");
            else
                emit notify(tr("No se pudo grabar: %1").arg(qs(e)), "error");
            emit refrescar();
        });
}

void EmisionViewModel::startStreaming(const QString& server, const QString& key) {
    p_.ventanilla.pedir(
        this, "transmitir",
        [&rt = p_.rt, s = server.toStdString(), k = key.toStdString()]() {
            Transmitir t;
            t.ok = rt.salirAlAireManual(s, k);
            t.inicio = rt.engine().ultimoInicio();
            t.activo = rt.engine().destinoActivo();
            t.error = rt.lastError();
            return t;
        },
        [this, server, key](const Transmitir& t) {
            if (t.ok) {
                // Nunca un "Al aire" verde si no cambió nada.
                if (t.inicio == BroadcastEngine::Inicio::YaIgual)
                    emit notify(tr("Ya estabas transmitiendo a ese destino: no cambió nada"), "info");
                else
                    emit notify(tr("Al aire"), "ok");
            } else if (t.inicio == BroadcastEngine::Inicio::YaOtroDestino) {
                emit pedirCambioDestino(qs(t.activo), server, key);
            } else {
                emit notify(tr("No se pudo transmitir: %1").arg(qs(t.error)), "error");
            }
            emit refrescar();
        });
}

void EmisionViewModel::cambiarDestinoStream(const QString& server, const QString& key) {
    p_.ventanilla.pedir(
        this, "transmitir_cambiar",
        [&rt = p_.rt, s = server.toStdString(), k = key.toStdString()]() {
            return rt.salirAlAireManual(s, k, true) ? std::string() : rt.lastError().empty() ? std::string("?") : rt.lastError();
        },
        [this](const std::string& e) {
            if (e.empty())
                emit notify(tr("Transmitiendo al destino nuevo"), "ok");
            else
                emit notify(tr("No se pudo cambiar de destino: %1").arg(qs(e)), "error");
            emit refrescar();
        });
}

void EmisionViewModel::stopStreaming() {
    pedir("transmitir_parar", [](AppRuntime& rt) {
        rt.stopStreaming();
        return std::string();
    });
}

QVariantList EmisionViewModel::destinosUi() const { return listaDe(p_.ventanilla.foto().destinos); }

QString EmisionViewModel::guardarDestinoUi(const QVariantMap& d) {
    const auto j = nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(d)).toJson().toStdString(), nullptr, false);
    p_.ventanilla.pedir(
        this, "destino_guardar", [&rt = p_.rt, j]() { return rt.guardarDestino(j); },
        [this](const std::string& e) { emit destinoGuardado(qs(e)); });
    return {};
}

QString EmisionViewModel::borrarDestinoUi(const QString& id) {
    pedir("destino_borrar", [i = id.toStdString()](AppRuntime& rt) { return rt.borrarDestino(i); });
    return {};
}

QString EmisionViewModel::iniciarDestinoUi(const QString& id) {
    pedir("destino_iniciar", [i = id.toStdString()](AppRuntime& rt) { return rt.iniciarDestino(i); });
    return {};
}

void EmisionViewModel::detenerDestinoUi(const QString& id) {
    pedir("destino_detener", [i = id.toStdString()](AppRuntime& rt) {
        rt.detenerDestino(i);
        return std::string();
    });
}

QVariantMap EmisionViewModel::discoUi() const { return mapaDe(disco_); }

void EmisionViewModel::pedirDisco() {
    p_.ventanilla.pedir(
        this, "disco", [&rt = p_.rt]() { return rt.discoGrabacion(); },
        [this](const nlohmann::json& d) {
            if (d == disco_) return;
            disco_ = d;
            emit discoCambio();
        });
}

QVariantMap EmisionViewModel::remuxUi() const { return mapaDe(p_.ventanilla.foto().remux); }

QVariantMap EmisionViewModel::apurosDiscoUi() const { return mapaDe(p_.ventanilla.foto().apurosDisco); }

void EmisionViewModel::priorizarProgramaUi(bool si) {
    p_.ventanilla.pedir("disco_priorizar", [&rt = p_.rt, si]() { rt.engine().priorizarPrograma(si); });
}

QString EmisionViewModel::modoDiscoUi(const QString& modo) {
    pedir("disco_modo", [m = modo.toStdString()](AppRuntime& rt) { return rt.engine().setModoDisco(m); });
    return {};
}

QVariantList EmisionViewModel::isosUi() const { return listaDe(p_.ventanilla.foto().isos); }

void EmisionViewModel::isoInvitadoUi(const QString& id) {
    const QString f = fuenteDeInvitado(id);
    if (f.isEmpty()) return emit notify(tr("Ese invitado no tiene una fuente en el programa todavía."), "warn");
    pedir("iso_invitado", [n = f.toStdString()](AppRuntime& rt) { return rt.grabarAparte(n); }, // las reglas son del modelo
          [this, f]() {
              emit notify(tr("Grabando «%1» aparte").arg(f), "ok");
              emit estadoCambio();
          });
}

QString EmisionViewModel::isoUi(const QString& fuente, bool grabar) {
    if (!grabar) {
        pedir("iso_parar", [n = fuente.toStdString()](AppRuntime& rt) {
            rt.stopSourceRecording(n);
            return std::string();
        }, [this]() { emit estadoCambio(); });
        return {};
    }
    pedir("iso", [n = fuente.toStdString()](AppRuntime& rt) { return rt.grabarAparte(n); }, // las reglas son del modelo
          [this]() { emit estadoCambio(); });
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
        return p_.ventanilla.foto().tipoDe(f.toStdString()).empty() ? QString() : f;
    }
    return {};
}

QString EmisionViewModel::isoTodasUi(bool grabar) {
    if (!grabar) {
        pedir("iso_parar_todas", [](AppRuntime& rt) {
            rt.stopAllSourceRecordings();
            return std::string();
        }, [this]() { emit estadoCambio(); });
        return {};
    }
    // Las de la escena al aire que todavía no se graban: se decide en el dueño (lo que se
    // graba ahora es suyo); los errores, juntos.
    pedir(
        "iso_todas",
        [](AppRuntime& rt) {
            BroadcastEngine& motor = rt.engine();
            std::string errores;
            for (const std::string& f : motor.sceneSources(motor.currentScene())) {
                bool ya = false;
                for (const auto& g : motor.sourceRecordings()) ya = ya || g.source == f;
                if (ya) continue;
                if (const std::string e = rt.grabarAparte(f); !e.empty()) errores += (errores.empty() ? "" : "; ") + f + ": " + e;
            }
            return errores;
        },
        [this]() { emit estadoCambio(); }, "warn");
    return {};
}

QVariantMap EmisionViewModel::limpiaUi() const { return mapaDe(p_.ventanilla.foto().limpia); }

QString EmisionViewModel::limpiaActivarUi(bool activar, const QString& destino) {
    pedir("limpia", [activar, d = destino.toStdString()](AppRuntime& rt) {
        if (!activar) {
            rt.detenerSenalLimpia();
            return std::string();
        }
        return rt.iniciarSenalLimpia(d);
    });
    return {};
}

QVariantMap EmisionViewModel::grabacionUi() const { return mapaDe(p_.ventanilla.foto().grabacion); }

QVariantMap EmisionViewModel::loudnessUi(const QString& salida) const {
    const FotoMotor& f = p_.ventanilla.foto();
    const std::string id = salida.isEmpty() ? std::string(BroadcastEngine::kSalidaPorOmision) : salida.toStdString();
    return mapaDe(f.loudness.value(id, nlohmann::json::object()));
}

void EmisionViewModel::resetLoudnessUi(const QString& salida) {
    p_.ventanilla.pedir("loudness_reiniciar", [&rt = p_.rt, s = salida.toStdString()]() { rt.engine().loudness(s).reiniciar(); });
}

QString EmisionViewModel::objetivoLoudnessUi(const QVariantMap& o, const QString& salida) {
    const auto j = nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(o)).toJson().toStdString(), nullptr, false);
    pedir("loudness_objetivo", [&ob = *objetivoLoudness_, j, s = salida.toStdString()](AppRuntime&) { return ob.poner(j, s); });
    return {};
}

QVariantMap EmisionViewModel::retornos() const { return mapaDe(p_.ventanilla.foto().retornos); }

QString EmisionViewModel::retornoInvitadoUi(int slot) const {
    return qs(p_.ventanilla.foto().retornos.value(std::to_string(slot), std::string("mix_minus")));
}

QString EmisionViewModel::setRetornoInvitadoUi(int slot, const QString& modo) {
    pedir("retorno_invitado", [&r = *retornos_, slot, m = modo.toStdString()](AppRuntime&) { return r.poner(slot, m); });
    return {};
}

void EmisionViewModel::muestrearSalud(control::BusEventos& bus) {
    const nlohmann::json& h = p_.ventanilla.foto().salud;
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

QVariantList EmisionViewModel::saludSalidas() const { return listaDe(saludActual_); }

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
