// src/app/ui/SalaViewModel.cpp — ver el .hpp

#include "ui/SalaViewModel.hpp"

#include "arranque/AppRuntime.hpp"
#include "control/BusEventos.hpp"
#include "net/AccesoRemoto.hpp"
#include "sala/ConfigSala.hpp"
#include "sala/CupoSala.hpp"
#include "sala/Direcciones.hpp"
#include "sala/PreguntasSala.hpp"
#include "sala/SeguridadSala.hpp"
#include "sala/VotacionSala.hpp"
#include "ui/ChatOverlayRenderer.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/RecursosViewModel.hpp"
#include "ui/SettingsModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QQmlEngine>

namespace ba::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

/// Sin padre y devuelto a QML: que no se lo quede su recolector. Es del área.
QObject* deCpp(QObject* o) {
    if (o != nullptr) QQmlEngine::setObjectOwnership(o, QQmlEngine::CppOwnership);
    return o;
}

} // namespace

SalaViewModel::SalaViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    AppRuntime& rt = p_.rt;
    ConferenceClient& cli = p_.conferencia.client();
    // El transporte A→B lo mide el gateway y viaja en la foto de la sala (los SLI, F4-01).
    p_.recursos.alTransporte([&cli]() {
        const QJsonValue t = cli.snapshot().value("uso").toObject().value("transporte");
        const nlohmann::json j = nlohmann::json::parse(QJsonDocument(t.toArray()).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
        return j.is_array() ? j : nlohmann::json::array();
    });
    direcciones_ = std::make_unique<sala::Direcciones>(cli);
    // El cupo es del modelo; acá solo se muestra lo que pasó.
    cupoSala_ = std::make_unique<sala::CupoSala>(rt, cli);
    cupoSala_->alAvisar([this](const std::string& texto, bool error) { emit notify(QString::fromStdString(texto), error ? "error" : "ok"); });
    cupoSala_->alCambiar([this](int n) { emit cupoCambio(n); });
    configSala_ = std::make_unique<sala::ConfigSala>(cli);
    seguridad_ = std::make_unique<sala::SeguridadSala>(rt.dataRoot(), cli, p_.enVivo);
    // El encargo al asistente vive afuera (necesita el MCP): la sala solo avisa.
    p_.conferencia.setIntakeSink([this](const QString& id, const QString& name, bool yaAdentro) {
        return assistantIntake(id, name, yaAdentro);
    });
    accesoRemoto_ = std::make_unique<AccesoRemoto>(rt, cli);
    accesoRemoto_->setDirecciones(direcciones_.get());
    accesoRemoto_->setEnVivo(p_.enVivo);
    // «Configurar todo con la API key»: los cambios de Configuración → Pinggy van por un
    // modelo aparte (como el MCP), así no se pisa lo que el operador tenga a medio editar.
    accesoRemoto_->setAplicarPinggy([&rt](const QVariantMap& cambios) -> QString {
        SettingsModel m(rt);
        m.revert();
        for (auto it = cambios.begin(); it != cambios.end(); ++it) {
            if (!m.value(it.key()).isValid()) return tr("esta versión no tiene %1").arg(it.key());
            m.setValue(it.key(), it.value());
        }
        const QString resumen = m.apply();
        if (m.fallo()) return resumen;
        return {}; // los formularios y las listas se enteran solos (avisos del runtime)
    });
    // La votación es del modelo (con el padrón del evento abierto); el público y el MCP la usan.
    votacion_ = std::make_unique<sala::VotacionSala>(cli, [this]() {
        EventoViewModel* ev = p_.evento ? p_.evento() : nullptr;
        return ev ? ev->votantes() : std::nullopt;
    });
    // Las preguntas, ídem; un mensaje del chat se busca en el chat que junta las plataformas.
    preguntas_ = std::make_unique<sala::PreguntasSala>(
        cli, [this](const std::string& clave) -> std::optional<sala::PreguntasSala::MensajeChat> {
            ChatOverlayRenderer* chat = p_.chat ? p_.chat() : nullptr;
            if (chat == nullptr) return std::nullopt;
            for (const auto& m : chat->collect())
                if (m.key.toStdString() == clave)
                    return sala::PreguntasSala::MensajeChat{m.text.toStdString(), m.author.toStdString(),
                                                            m.source == "room" ? std::string("sala") : m.source.toStdString()};
            return std::nullopt;
        });
}

SalaViewModel::~SalaViewModel() = default;

QObject* SalaViewModel::direccionesQml() const { return deCpp(direcciones_.get()); }
QObject* SalaViewModel::accesoRemotoQml() const { return deCpp(accesoRemoto_.get()); }

QString SalaViewModel::cupoUi(int n) { return QString::fromStdString(cupoSala_->cambiar(n)); }

QString SalaViewModel::aprobarInfoInvitado(const QString& participante, const QString& texto) {
    const QJsonObject sala = p_.conferencia.client().snapshot();
    for (const QJsonValue& v : sala.value("participants").toArray()) {
        const QJsonObject pa = v.toObject();
        if (pa.value("id").toString() == participante || pa.value("name").toString() == participante) {
            infoAprobada_[pa.value("id").toString()] = texto.isEmpty() ? pa.value("info").toString() : texto;
            return {};
        }
    }
    return tr("no hay un invitado «%1» en la conferencia").arg(participante);
}

QVariantList SalaViewModel::chatRecientesUi() const {
    QVariantList out;
    ChatOverlayRenderer* chat = p_.chat ? p_.chat() : nullptr;
    if (chat == nullptr) return out;
    const QStringList dest = chat->destacados();
    for (const auto& m : chat->collect())
        out << QVariantMap{{"key", m.key}, {"source", m.source}, {"author", m.author}, {"text", m.text},
                           {"destacado", dest.contains(m.key)}};
    return out;
}

bool SalaViewModel::destacarChatUi(const QString& key) {
    ChatOverlayRenderer* chat = p_.chat ? p_.chat() : nullptr;
    return chat != nullptr && chat->destacar(key);
}

void SalaViewModel::quitarDestacadoChatUi(const QString& key) {
    if (ChatOverlayRenderer* chat = p_.chat ? p_.chat() : nullptr) chat->quitarDestacado(key);
}

QString SalaViewModel::preguntaDesdeChatUi(const QString& key) {
    QPointer<SalaViewModel> yo(this);
    return qs(preguntas_->sumarDesdeChat(key.toStdString(), [yo](const std::string& err, const nlohmann::json&) {
        if (yo)
            emit yo->notify(err.empty() ? tr("Sumada a la cola de preguntas")
                                        : tr("No se pudo sumar la pregunta: %1").arg(QString::fromStdString(err)),
                            err.empty() ? "ok" : "error");
    }));
}

QString SalaViewModel::assistantIntake(const QString& id, const QString& name, bool yaAdentro) {
    if (!intakeSink_) return tr("El servidor MCP no está disponible.");
    return intakeSink_(id, name, yaAdentro);
}

void SalaViewModel::pedirDireccion(const QString& sub) {
    if (!accesoRemoto_->apiKeyGuardada()) return;
    const QString actual = accesoRemoto_->estado().value("pinggy").toMap().value("urlPublica").toString();
    if (actual.contains("//" + sub + ".")) return; // ya es esa
    if (p_.conAire && p_.conAire()) {
        emit notify(tr("El evento tiene su dirección (%1.a.pinggy.link) pero se está transmitiendo o grabando: "
                       "no se cambia ahora (reinicia la conferencia). Cambiala desde Guion → Página web cuando se pueda.")
                        .arg(sub),
                    "warning");
        return;
    }
    emit notify(tr("Pasando la dirección pública a %1.a.pinggy.link (la conferencia se reinicia)…").arg(sub), "info");
    accesoRemoto_->configurarConApiKey(sub);
}

void SalaViewModel::alEmpezarElVivo() {
    // T47: el PIN es uno por día (el mismo día, el mismo PIN).
    seguridad_->rotarPinSiCambioElDia([this](const std::string& error, bool cambio, const std::string& pin) {
        if (error.empty() && cambio) emit notify(tr("Día nuevo: la conferencia tiene PIN nuevo (%1)").arg(qs(pin)), "info");
    });
}

void SalaViewModel::completarVinculos(QJsonObject& ctx) const {
    // La sala: quién está al aire y quiénes esperan, en el orden en que los
    // ve el operador. `invitado1`, `invitado2`... son ESE orden, que es lo que
    // uno tiene delante cuando dice «el invitado 2».
    const QJsonObject sala = p_.conferencia.client().snapshot();
    QHash<QString, QJsonObject> porId;
    for (const QJsonValue& v : sala.value("participants").toArray()) porId.insert(v.toObject().value("id").toString(), v.toObject());

    // Lo que sale en un zócalo tiene que estar VALIDADO: si el invitado está en
    // el Cuerpo (por nombre), salen el nombre y el cargo de ahí; si no, el
    // `info` que él escribió al entrar solo sale si el operador lo aprobó
    // (invitado_info_aprobar). Sin eso, el cargo va vacío.
    const auto deParticipante = [this](const QJsonObject& pa) {
        const QString nombre = pa.value("name").toString();
        const nlohmann::json enCuerpo = p_.enElCuerpo ? p_.enElCuerpo(nombre.toStdString()) : nlohmann::json::object();
        QString cargo, bloque;
        bool validado = false;
        if (!enCuerpo.empty()) {
            cargo = QString::fromStdString(enCuerpo.value("cargo_oficial", std::string()));
            bloque = QString::fromStdString(enCuerpo.value("bloque", std::string()));
            validado = enCuerpo.contains("validado_por");
        } else if (const auto it = infoAprobada_.constFind(pa.value("id").toString()); it != infoAprobada_.constEnd()) {
            cargo = *it;
            validado = true;
        }
        return QJsonObject{{"nombre", enCuerpo.empty() ? nombre : QString::fromStdString(enCuerpo.value("nombre", nombre.toStdString()))},
                           {"cargo", cargo},
                           {"bloque", bloque},
                           {"validado", validado},
                           {"sugerencia", pa.value("info").toString()},
                           {"contacto", pa.value("contact").toString()}};
    };

    int n = 0;
    bool hayOrador = false;
    QJsonObject primeroEnCola;
    for (const QJsonValue& v : sala.value("order").toArray()) {
        const auto it = porId.constFind(v.toString());
        if (it == porId.constEnd()) continue;
        const QJsonObject pa = *it;
        ctx["invitado" + QString::number(++n)] = deParticipante(pa);
        const QString estado = pa.value("speaker").toString();
        if (estado == "onair" && !hayOrador) {
            ctx["orador"] = deParticipante(pa);
            hayOrador = true;
        } else if (estado == "queued" && primeroEnCola.isEmpty()) {
            primeroEnCola = deParticipante(pa);
        }
    }
    if (!primeroEnCola.isEmpty()) ctx["siguiente"] = primeroEnCola;
}

void SalaViewModel::alimentarEnlaces(EnlacesViewModel::Fuentes& f) {
    ConferenceViewModel* c = &p_.conferencia;
    f.enlaces = [c]() { return &c->enlaces(); };
    f.salaEnLinea = [c]() { return c->online(); };
    f.salaCerrada = [c]() { return c->locked(); };
    f.cerrarSala = [c](bool on) { c->setLocked(on); };
    f.tunel = [this]() { return accesoRemoto_->conexion(); };
    f.activarTunel = [this](bool on) -> QString {
        if (!on) return accesoRemoto_->desconectar();
        const QVariantList formas = accesoRemoto_->formasDeConectar();
        if (formas.isEmpty())
            return tr("No hay ninguna forma de conectar guardada: configurala en Configuración → Acceso remoto.");
        return accesoRemoto_->conectar(formas.first().toMap().value("id").toString());
    };
    f.direcciones = [this]() -> const sala::Direcciones* { return direcciones_.get(); };
    // Los que esperan en el lobby: se admiten en la misma fila de Servicios.
    EnlacesViewModel::Aprobaciones a;
    a.pendientes = [c]() {
        QVariantList out;
        const auto* l = qobject_cast<const JsonListModel*>(c->waiting());
        if (l == nullptr) return out;
        for (const QJsonValue& v : l->items()) {
            const QJsonObject pa = v.toObject();
            out << QVariantMap{{"id", pa.value("uid").toString()},
                               {"nombre", pa.value("name").toString()},
                               {"ip", QString()},
                               {"ms", pa.value("joinedMs").toDouble()},
                               {"detalle", pa.value("info").toString().isEmpty()
                                               ? tr("espera en el lobby")
                                               : tr("espera en el lobby · %1").arg(pa.value("info").toString())}};
        }
        return out;
    };
    a.resolver = [c](const QString& uid, bool aprobar, const QString&) -> QString {
        if (aprobar)
            c->admit(uid);
        else
            c->reject(uid);
        return {};
    };
    a.textoAprobar = tr("Admitir");
    a.unoEspera = tr("Una persona espera en el lobby: admitila acá abajo");
    a.variosEsperan = tr("%1 personas esperan en el lobby: admitilas acá abajo");
    f.aprobaciones["invitados"] = std::move(a);
}

void SalaViewModel::avisarA(EnlacesViewModel& e) {
    const auto avisar = [ep = &e]() { ep->avisarCambio(); };
    connect(&p_.conferencia, &ConferenceViewModel::onlineChanged, &e, avisar);
    // Cambiaron las direcciones (la red, el túnel, una puerta): los enlaces se vuelven a pedir.
    connect(direcciones_.get(), &sala::Direcciones::cambio, &e, &EnlacesViewModel::refrescar);
    connect(&p_.conferencia, &ConferenceViewModel::roomChanged, &e, avisar);
    connect(accesoRemoto_.get(), &AccesoRemoto::estadoChanged, &e, avisar);
}

void SalaViewModel::publicarParticipantes(control::BusEventos& bus) const {
    nlohmann::json gente = nlohmann::json::array();
    const QJsonObject snap = p_.conferencia.client().snapshot();
    for (const QJsonValue& v : snap.value("participants").toArray()) {
        const QJsonObject o = v.toObject();
        gente.push_back({{"id", o.value("id").toString().toStdString()},
                         {"nombre", o.value("name").toString().toStdString()},
                         {"estado", o.value("state").toString().toStdString()},
                         {"mic", o.value("mic").toBool()},
                         {"cam", o.value("cam").toBool()},
                         {"mano", o.value("hand").toBool()}});
    }
    bus.publicar("sala/participantes", gente);
}

} // namespace ba::app
