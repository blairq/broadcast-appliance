// ============================================================================
// src/app/ui/AtencionViewModel.cpp — ver AtencionViewModel.hpp
// ============================================================================
#include "ui/AtencionViewModel.hpp"
#include "i18n/I18n.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

#include "agent/AgentViewModel.hpp"
#include "agent/Despachador.hpp"
#include "agent/ModelosLocales.hpp"
#include "agent/Roles.hpp"
#include "agent/Voz.hpp"
#include "arranque/AppRuntime.hpp"
#include "asamblea/CuerpoViewModel.hpp"
#include "asamblea/SesionRemota.hpp"
#include "emision/OperatorFeed.hpp"
#include "emision/VozAlLobby.hpp"
#include "ui/ConferenceClient.hpp"
#include "sala/ControlSala.hpp"
#include "sala/Pedido.hpp"
#include "sala/ParticipantesSala.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/PersonasViewModel.hpp"
#include "ui/StudioViewModel.hpp"
#if defined(BA_HAVE_VOZ)
#include "audio/Habla.hpp"
#endif

namespace ba::app {

namespace {

QString archivoAjustes(const std::filesystem::path& datos) {
    return QString::fromStdString((datos / "Config" / "atencion.json").string());
}

/// La clave del oído de alguien que se atiende hablando (agent::Voz).
QString claveOido(const QString& id) { return "atencion:" + id; }

/// Un .wav mono de 16 bits (la prueba de la voz del atendedor).
bool escribirWav(const QString& ruta, const std::vector<float>& m, std::uint32_t frecuencia) {
    QDir().mkpath(QFileInfo(ruta).absolutePath());
    QFile f(ruta);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const auto u32 = [&f](std::uint32_t v) {
        const char b[4] = {static_cast<char>(v), static_cast<char>(v >> 8), static_cast<char>(v >> 16),
                           static_cast<char>(v >> 24)};
        f.write(b, 4);
    };
    const auto u16 = [&f](std::uint16_t v) {
        const char b[2] = {static_cast<char>(v), static_cast<char>(v >> 8)};
        f.write(b, 2);
    };
    const auto datos = static_cast<std::uint32_t>(m.size() * 2);
    f.write("RIFF", 4);
    u32(36 + datos);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1); // PCM
    u16(1); // mono
    u32(frecuencia);
    u32(frecuencia * 2);
    u16(2);
    u16(16);
    f.write("data", 4);
    u32(datos);
    for (const float x : m) u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp(x, -1.0f, 1.0f) * 32767.0f)));
    return true;
}

ConferenceViewModel* conferencia(StudioViewModel& vm) { return static_cast<ConferenceViewModel*>(vm.conference()); }
PersonasViewModel* personas(StudioViewModel& vm) { return static_cast<PersonasViewModel*>(vm.personas()); }

} // namespace

AtencionViewModel::AtencionViewModel(AppRuntime& rt, StudioViewModel& vm, agent::Voz& voz,
                                     agent::ModelosLocales& modelos, QObject* parent)
    : QObject(parent), rt_(rt), vm_(vm), voz_(voz), modelos_(modelos), alLobby_(std::make_unique<VozAlLobby>()) {
    QFile f(archivoAjustes(rt_.dataRoot()));
    if (f.open(QIODevice::ReadOnly))
        aj_ = sala::AjustesAtencion::deJson(nlohmann::json::parse(f.readAll().toStdString(), nullptr, false));
#if defined(BA_HAVE_VOZ)
    habla_ = std::make_unique<audio::Habla>();
#endif
    connect(&modelos_, &agent::ModelosLocales::cambio, this, &AtencionViewModel::revisarVoz);
    // Que el reconocedor esté (o no) cambia si se puede atender hablando.
    connect(&voz_, &agent::Voz::cambio, this, &AtencionViewModel::cambio);
    // Quién atiende depende de lo que esté prendido: el rol y el de automatización.
    connect(&vm_.despachador(), &agent::Despachador::cambio, this, &AtencionViewModel::cambio);
    if (auto* ag = static_cast<agent::AgentViewModel*>(vm_.agenteAutomatizacion()))
        connect(ag, &agent::AgentViewModel::runningChanged, this, &AtencionViewModel::cambio);
    if (ConferenceViewModel* conf = conferencia(vm_)) {
        connect(conf, &ConferenceViewModel::roomChanged, this, &AtencionViewModel::alSala);
        // «Los atiende» también se cambia por MCP (room_set) o se apaga solo
        // (sin quién atienda): la vista y Personas se enteran desde acá.
        connect(conf, &ConferenceViewModel::autoIntakeChanged, this, [this]() {
            if (PersonasViewModel* pv = personas(vm_)) pv->setLobbyPorAtencion(salaAtiende());
            if (!salaAtiende()) {
                cerrarVoz();
                if (!turnos_.terminarTodos(sala::Ambito::Sala).empty()) emit turnosCambio();
            }
            emit cambio();
        });
        conf->control().setAlChatear(
            [this](const std::string& para, const std::string& texto) { alChatear(para, texto); });
        // La página del que espera en la sesión remota no pudo abrir el micrófono (no
        // dio permiso, no tiene) o la sala no tiene lugar: sigue sin la voz.
        connect(&conf->client(), &ConferenceClient::sesionEvento, this, [this](const QJsonObject& ev) {
            if (ev.value("tipo").toString() != "atiende_error") return;
            const QString id = ev.value("id").toString();
            if (id == vozId_ && vozAmbito_ == sala::Ambito::Asamblea) sinVoz(id, ev.value("error").toString());
        });
    }
    if (asamblea::SesionRemota* s = vm_.sesionRemota()) {
        connect(s, &asamblea::SesionRemota::cambio, this, &AtencionViewModel::alAsamblea);
        connect(s, &asamblea::SesionRemota::fotoLista, this, &AtencionViewModel::alFotoIntegrante);
    }
    if (PersonasViewModel* pv = personas(vm_)) pv->setLobbyPorAtencion(salaAtiende());
    revisarVoz();
}

AtencionViewModel::~AtencionViewModel() {
    if (ConferenceViewModel* conf = conferencia(vm_)) conf->control().setAlChatear({});
    if (vozToma_ != 0) rt_.operatorFeed().dejarDeEscucharLobby(vozToma_);
#if defined(BA_HAVE_VOZ)
    if (habla_) habla_->callar();
    habla_.reset(); // antes que el canal: su hilo le escribe
#endif
    alLobby_.reset();
}

void AtencionViewModel::guardar() const {
    const QString ruta = archivoAjustes(rt_.dataRoot());
    QDir().mkpath(QFileInfo(ruta).absolutePath());
    QFile f(ruta);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QByteArray::fromStdString(aj_.aJson().dump(1, ' ', false, nlohmann::json::error_handler_t::replace)));
}

// --- Los interruptores -------------------------------------------------------------

bool AtencionViewModel::salaAtiende() const {
    ConferenceViewModel* conf = conferencia(vm_);
    return conf != nullptr && conf->autoIntake();
}

void AtencionViewModel::setSalaAtiende(bool on) {
    ConferenceViewModel* conf = conferencia(vm_);
    if (conf == nullptr || on == salaAtiende()) return;
    if (on && atendedor() == sala::Atendedor::Nadie) {
        emit notify(QString::fromStdString(sala::porQueNadie(sala::Atendedor::Nadie)), "error");
        emit cambio(); // la vista vuelve el interruptor a como estaba
        return;
    }
    conf->setAutoIntake(on); // avisa autoIntakeChanged: el resto se hace ahí
}

void AtencionViewModel::setSalaHablando(bool on) {
    if (on == aj_.salaHablando) return;
    aj_.salaHablando = on;
    guardar();
    if (!on && !turnos_.soltarLaVoz(sala::Ambito::Sala).empty()) cerrarVoz();
    // Los que ya se están atendiendo sin voz: el primero que puede la toma (si se
    // apagó acá y la voz era de la sala, puede ser uno de la asamblea).
    pasarLaVoz();
    emit turnosCambio();
    emit cambio();
}

void AtencionViewModel::setAsambleaHablando(bool on) {
    if (on == aj_.asambleaHablando) return;
    aj_.asambleaHablando = on;
    guardar();
    if (!on && !turnos_.soltarLaVoz(sala::Ambito::Asamblea).empty()) cerrarVoz();
    pasarLaVoz();
    emit turnosCambio();
    emit cambio();
}

void AtencionViewModel::setAsambleaAtiende(bool on) {
    if (on == aj_.asambleaAtiende) return;
    if (on && atendedor() == sala::Atendedor::Nadie) {
        emit notify(QString::fromStdString(sala::porQueNadie(sala::Atendedor::Nadie)), "error");
        emit cambio();
        return;
    }
    aj_.asambleaAtiende = on;
    guardar();
    if (!on) {
        esperandoFoto_.clear();
        if (!vozId_.isEmpty() && vozAmbito_ == sala::Ambito::Asamblea) cerrarVoz();
        turnos_.terminarTodos(sala::Ambito::Asamblea);
        pasarLaVoz(); // la voz quedó libre: puede ser para uno de la sala
        emit turnosCambio();
    }
    emit cambio();
    if (on) alAsamblea(); // los que ya estaban esperando también
}

sala::Atendedor AtencionViewModel::atendedor() const {
    const agent::Rol* r = vm_.despachador().roles().porId("recepcion");
    const auto* ag = static_cast<agent::AgentViewModel*>(vm_.agenteAutomatizacion());
    return sala::quienAtiende(r != nullptr && r->activo, ag != nullptr && ag->running());
}

QString AtencionViewModel::quienAtiendeTexto() const {
    const sala::Atendedor a = atendedor();
    if (a == sala::Atendedor::Nadie) return QString::fromStdString(sala::porQueNadie(a));
    return tr("Atiende %1.").arg(QString::fromStdString(sala::nombreDe(a)));
}

QString AtencionViewModel::hablarFalta() const {
    if (!voz_.disponible()) return tr("Esta compilación no trae el motor de voz.");
    // El reconocedor que eligió la recepción (pestaña Agentes): con que esté bajado
    // alcanza, se carga al abrir el oído.
    if (!voz_.puedeOir("recepcion"))
        return tr("Falta el reconocedor de voz (voz a texto) que usa la recepción: bajalo en Configuración → "
                  "Modelos locales (o elegí otro en Agentes).");
    if (!vozLista_)
        return tr("Falta la voz del atendedor: elegila en Configuración → Voz del sistema → Voz del atendedor.");
    return {};
}

QVariantList AtencionViewModel::turnos() const {
    QVariantList out;
    for (const auto& t : turnos_.todos())
        out << QVariantMap{{"id", QString::fromStdString(t.id)},
                           {"nombre", QString::fromStdString(t.nombre)},
                           {"ambito", sala::claveDe(t.ambito)},
                           {"quien", sala::claveDe(t.quien)},
                           {"modo", sala::claveDe(t.modo)}};
    return out;
}

// --- La sala -------------------------------------------------------------------------

QString AtencionViewModel::empezarSala(const QString& id, const QString& nombre, sala::Atendedor quien) {
    // Hablando solo con lugar en el lobby: sin él no hay canal por donde suba su voz.
    const auto t = turnos_.empezar(id.toStdString(), nombre.toStdString(), sala::Ambito::Sala, quien,
                                   aj_.salaHablando && puedeHablar() && lugarDe(id) > 0 && vozId_.isEmpty());
    if (t.modo == sala::Modo::Voz && vozId_ != id) abrirVoz(id);
    emit turnosCambio();
    return QString::fromLatin1(sala::claveDe(t.modo));
}

QString AtencionViewModel::instruccion(const QString& modo) {
    if (modo != "voz") return {};
    return QStringLiteral(" ") +
           QCoreApplication::translate(
               "asistente",
               "ESTA PERSONA ESTÁ ATENDIDA HABLANDO: todo lo que le mandes con `chat_send` le llega también "
               "dicho en voz alta, y lo que ella dice te llega como mensajes suyos (con `voz: true`) por "
               "`chat_wait` o `invitados_chat_leer`. Escribí como se habla: frases cortas, de a una pregunta, sin "
               "listas, sin emojis ni símbolos, y los números como se dicen. Si algo de lo que te llega no se "
               "entiende (la voz a texto a veces se equivoca), pedile que lo repita. Los datos para la ficha "
               "(nombre, cargo) confirmáselos en voz alta antes de guardarlos.");
}

std::function<bool(const std::string&)> AtencionViewModel::puedeTomarLaVoz() const {
    return [this](const std::string& id) {
        const sala::Turnos::Turno* t = turnos_.turno(id);
        if (t == nullptr) return false;
        if (t->ambito == sala::Ambito::Sala)
            return aj_.salaHablando && salaAtiende() && lugarDe(QString::fromStdString(id)) > 0;
        return aj_.asambleaHablando && aj_.asambleaAtiende && esperaEnAsamblea(id);
    };
}

bool AtencionViewModel::esperaEnAsamblea(const std::string& id) const {
    asamblea::SesionRemota* s = vm_.sesionRemota();
    if (s == nullptr) return false;
    for (const auto& p : s->participantes())
        if (p.value("id", std::string()) == id) return p.value("estado", std::string()) == "esperando";
    return false;
}

void AtencionViewModel::pasarLaVoz() {
    if (!vozId_.isEmpty() || !puedeHablar()) return;
    if (const std::string id = turnos_.pasarLaVoz(puedeTomarLaVoz()); !id.empty()) {
        abrirVoz(QString::fromStdString(id));
        emit turnosCambio();
    }
}

void AtencionViewModel::sinVoz(const QString& id, const QString& error) {
    const auto* t = turnos_.turno(id.toStdString());
    const QString nombre = t != nullptr ? QString::fromStdString(t->nombre) : id;
    const bool enAsamblea = t != nullptr && t->ambito == sala::Ambito::Asamblea;
    const QString porQue = error.isEmpty() ? tr("No se pudo atender hablando a «%1».").arg(nombre)
                                           : tr("No se pudo atender hablando a «%1»: %2.").arg(nombre, error);
    emit notify(porQue + " " + (enAsamblea ? tr("Se lo sigue atendiendo mirándole la cara.") : tr("Sigue por chat.")),
                "warn");
    turnos_.sinVoz(id.toStdString());
    cerrarVoz();
    pasarLaVoz();
    emit turnosCambio();
}

int AtencionViewModel::lugarDe(const QString& id) const {
    ConferenceViewModel* conf = conferencia(vm_);
    if (conf == nullptr) return 0;
    for (const QJsonValue& v : conf->client().snapshot().value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        if (p.value("id").toString() == id) return p.value("lobbySlot").toInt();
    }
    return 0;
}

void AtencionViewModel::alSala() {
    ConferenceViewModel* conf = conferencia(vm_);
    if (conf == nullptr) return;
    std::set<std::string> esperando, adentro;
    for (const QJsonValue& v : conf->client().snapshot().value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        const QString estado = p.value("state").toString();
        if (estado == "waiting") esperando.insert(p.value("id").toString().toStdString());
        if (estado == "admitted") adentro.insert(p.value("id").toString().toStdString());
    }
    bool cambio = false;
    for (const auto& t : turnos_.actualizar(sala::Ambito::Sala, esperando, adentro)) {
        cambio = true;
        // Al admitirla (o al irse) se termina la atención: la voz se suelta y
        // la toma el siguiente que se esté atendiendo por chat.
        if (QString::fromStdString(t.id) == vozId_) cerrarVoz();
    }
    if (vozId_.isEmpty() && puedeHablar()) {
        if (const std::string id = turnos_.pasarLaVoz(puedeTomarLaVoz()); !id.empty()) {
            abrirVoz(QString::fromStdString(id));
            cambio = true;
        }
    }
    // Las posiciones del lobby se reciclan: al entrar uno, el siguiente toma
    // su lugar. Si a quien se escucha le cambió, se lo sigue al lugar nuevo.
    if (!vozId_.isEmpty() && vozAmbito_ == sala::Ambito::Sala) {
        const int lugar = lugarDe(vozId_);
        if (lugar != vozLugar_) {
            if (vozToma_ != 0) rt_.operatorFeed().dejarDeEscucharLobby(vozToma_);
            vozToma_ = 0;
            vozLugar_ = lugar;
            if (lugar > 0) {
                const std::string clave = claveOido(vozId_).toStdString();
                agent::Voz* voz = &voz_;
                vozToma_ = rt_.operatorFeed().escucharLobby(lugar, [voz, clave](const float* d, std::size_t n) {
                    thread_local std::vector<float> mono;
                    mono.resize(n);
                    for (std::size_t i = 0; i < n; ++i) mono[i] = d[2 * i]; // un micrófono es mono
                    voz->alimentarOido(clave, mono.data(), n, 48000);
                });
            }
        }
    }
    if (cambio) emit turnosCambio();
}

void AtencionViewModel::abrirVoz(const QString& id) {
    ConferenceViewModel* conf = conferencia(vm_);
    if (conf == nullptr) return;
    cerrarVoz();
    const auto* turno = turnos_.turno(id.toStdString());
    vozId_ = id;
    vozAmbito_ = turno != nullptr ? turno->ambito : sala::Ambito::Sala;
    const QString nombre = turno != nullptr ? QString::fromStdString(turno->nombre) : id;
    QPointer<AtencionViewModel> yo(this);
    // Si la sala no puede (se quedó sin lugar en el lobby, el integrante se
    // desconectó, la sala es vieja), se sigue sin la voz: la atención no se
    // corta por eso.
    const auto siFalla = [yo, id](const std::string& err, const nlohmann::json&) {
        if (!yo || err.empty() || yo->vozId_ != id) return;
        yo->sinVoz(id, QString::fromStdString(err));
    };
    const bool enAsamblea = vozAmbito_ == sala::Ambito::Asamblea;
    // Lo que se le oye entra a su hilo como un dicho suyo (el mismo oído en los dos lados).
    voz_.abrirOido(claveOido(id), [yo, id, nombre, enAsamblea](const QString& texto) {
        if (!yo) return;
        if (ConferenceViewModel* c = conferencia(yo->vm_))
            c->agregarDichoPorVoz(id, texto, enAsamblea ? nombre : QString());
    });
    alLobby_->abrir();
    if (enAsamblea) {
        // El gateway le pide a su página el micrófono; su voz sube a BA.Audio.Atencion
        // (un canal solo: de a uno) y le baja la del que atiende, solo a él.
        sala::pedir(conf->client(), "POST", "/api/v1/sesion/atiende", {{"id", id.toStdString()}, {"on", true}},
                    siFalla);
        const std::string clave = claveOido(id).toStdString();
        agent::Voz* voz = &voz_;
        vozToma_ = rt_.operatorFeed().escucharAtencionRemota([voz, clave](const float* d, std::size_t n) {
            thread_local std::vector<float> mono;
            mono.resize(n);
            for (std::size_t i = 0; i < n; ++i) mono[i] = d[2 * i]; // un micrófono es mono
            voz->alimentarOido(clave, mono.data(), n, 48000);
        });
        return;
    }
    // La sala sube su voz y le baja la del que atiende.
    conf->participantes().atender(id.toStdString(), true, siFalla);
    vozLugar_ = -1; // alSala lo engancha a su lugar (y lo sigue si cambia)
    alSala();
}

void AtencionViewModel::cerrarVoz() {
    if (vozId_.isEmpty()) return;
    const QString id = vozId_;
    vozId_.clear();
    if (vozToma_ != 0) rt_.operatorFeed().dejarDeEscucharLobby(vozToma_);
    vozToma_ = 0;
    vozLugar_ = 0;
    voz_.cerrarOido(claveOido(id));
#if defined(BA_HAVE_VOZ)
    if (habla_) habla_->callar();
#endif
    alLobby_->cerrar();
    ConferenceViewModel* conf = conferencia(vm_);
    if (conf == nullptr) return;
    if (vozAmbito_ == sala::Ambito::Asamblea) {
        // Su página cierra el micrófono. Al habilitarlo lo apaga esto (el borde no
        // sabe quién está habilitado); si se fue, el gateway ya la apagó y no importa.
        sala::pedir(conf->client(), "POST", "/api/v1/sesion/atiende", {{"id", id.toStdString()}, {"on", false}},
                    [](const std::string&, const nlohmann::json&) {});
        return;
    }
    // Si todavía espera, que la sala deje de subir su voz. Si ya entró, la sala
    // lo apagó sola y esto contesta que no está en el lobby: no importa.
    conf->participantes().atender(id.toStdString(), false);
}

void AtencionViewModel::alChatear(const std::string& para, const std::string& texto) {
    if (vozId_.isEmpty() || vozAmbito_ != sala::Ambito::Sala || QString::fromStdString(para) != vozId_) return;
    decir(QString::fromStdString(texto));
}

bool AtencionViewModel::atiendeHablandoEnAsamblea(const QString& id) const {
    return !id.isEmpty() && id == vozId_ && vozAmbito_ == sala::Ambito::Asamblea;
}

QString AtencionViewModel::decirAlIntegrante(const QString& id, const QString& texto) {
    if (!atiendeHablandoEnAsamblea(id))
        return tr("A «%1» no se lo está atendiendo hablando en la sesión remota (atencion_estado dice a quién).").arg(id);
    const QString t = texto.trimmed();
    if (t.isEmpty()) return tr("Falta qué decirle.");
    decir(t);
    const auto* turno = turnos_.turno(id.toStdString());
    const QString nombre = turno != nullptr ? QString::fromStdString(turno->nombre) : id;
    // Escrito en su página también: si no lo oyó bien (o no puede subir el
    // volumen), lo lee. Y en el hilo del operador, como en la sala.
    if (asamblea::SesionRemota* s = vm_.sesionRemota())
        s->mandar(id.toStdString(), {{"type", "atiende-dice"}, {"texto", t.toStdString()}});
    if (ConferenceViewModel* c = conferencia(vm_)) c->agregarDichoAlAtendido(id, nombre, t);
    return {};
}

void AtencionViewModel::decir(const QString& texto) {
#if defined(BA_HAVE_VOZ)
    const QString t = texto.trimmed();
    if (t.isEmpty() || !vozLista_) return;
    VozAlLobby* destino = alLobby_.get();
    habla_->decir(t.toStdString(), aj_.vozHablante, static_cast<float>(aj_.vozVelocidad),
                  [destino](const float* m, std::size_t n, std::uint32_t f) { destino->encolar(m, n, f); });
#else
    Q_UNUSED(texto);
#endif
}

// --- La asamblea ---------------------------------------------------------------------

void AtencionViewModel::alAsamblea() {
    asamblea::SesionRemota* s = vm_.sesionRemota();
    if (s == nullptr) return;
    std::set<std::string> esperando, adentro;
    std::map<std::string, std::string> nombres;
    for (const auto& p : s->participantes()) {
        const std::string id = p.value("id", std::string());
        const std::string estado = p.value("estado", std::string());
        if (estado == "esperando") esperando.insert(id);
        if (estado == "habilitado") adentro.insert(id);
        nombres[id] = p.value("nombre", std::string());
    }
    bool cambio = false;
    for (const auto& t : turnos_.actualizar(sala::Ambito::Asamblea, esperando, adentro)) {
        esperandoFoto_.remove(QString::fromStdString(t.id));
        // Al habilitarlo (o al irse) termina, como en la sala: la voz se suelta.
        if (QString::fromStdString(t.id) == vozId_) cerrarVoz();
        cambio = true;
    }
    if (aj_.asambleaAtiende) {
        for (const std::string& id : esperando) {
            if (turnos_.turno(id) != nullptr) continue; // ya se lo está atendiendo
            const auto t = turnos_.empezar(id, nombres[id], sala::Ambito::Asamblea, atendedor(),
                                           aj_.asambleaHablando && puedeHablar() && vozId_.isEmpty());
            if (t.modo == sala::Modo::Voz) abrirVoz(QString::fromStdString(id));
            cambio = true;
            const QString qid = QString::fromStdString(id);
            esperandoFoto_.insert(qid);
            s->pedirFoto(qid);
            // Sin foto en un rato (cámara tapada, celular viejo), igual se lo atiende.
            QTimer::singleShot(8000, this, [this, qid]() {
                if (esperandoFoto_.remove(qid)) encargarAsamblea(qid, QCoreApplication::translate("asistente", "No llegó la foto de su cámara."));
            });
        }
    }
    if (vozId_.isEmpty() && puedeHablar()) {
        if (const std::string id = turnos_.pasarLaVoz(puedeTomarLaVoz()); !id.empty()) {
            abrirVoz(QString::fromStdString(id));
            cambio = true;
        }
    }
    if (cambio) emit turnosCambio();
}

void AtencionViewModel::alFotoIntegrante(const QString& id) {
    if (!esperandoFoto_.remove(id)) return; // la pidió otro (la vista)
    asamblea::SesionRemota* s = vm_.sesionRemota();
    PersonasViewModel* pv = personas(vm_);
    if (s == nullptr) return;
    if (pv == nullptr || !pv->disponible()) return encargarAsamblea(id, QCoreApplication::translate("asistente", "Este equipo no reconoce caras."));
    const QByteArray jpeg = s->fotoJpeg(id);
    const QVariantMap integrante = vm_.cuerpo().buscar(id);
    const QString personaId = integrante.value("persona_id").toString();
    const QString nombre = integrante.value("nombre").toString();
    QPointer<AtencionViewModel> yo(this);
    if (personaId.isEmpty()) {
        // Sin ficha no hay con qué compararlo. Con el alta automática se le
        // arma una con su nombre (el de su credencial: lo dio quien le hizo el
        // acceso) y la cara de ahora, y se la vincula a su banca, así la
        // próxima vez sí se lo reconoce.
        if (pv->altaAutomatica() && !nombre.isEmpty())
            pv->altaConFoto(jpeg, nombre, integrante.value("cargo_oficial").toString(),
                            [yo, id, nombre](const QString& ficha, const QString& error) {
                                if (!yo || ficha.isEmpty()) {
                                    Q_UNUSED(error);
                                    return;
                                }
                                if (yo->vm_.cuerpo().guardar({{"id", id}, {"persona_id", ficha}}).isEmpty())
                                    emit yo->notify(tr("Anoté a «%1» en Personas con la cara de su cámara y lo vinculé a "
                                                       "su banca: la próxima vez se lo reconoce.")
                                                        .arg(nombre),
                                                    "info");
                            });
        return encargarAsamblea(id, QCoreApplication::translate("asistente", "No tiene ficha de rostro: no hay con qué compararlo."));
    }
    pv->compararFoto(jpeg, personaId, [yo, id, personaId, nombre](float parecido, bool sirve, const QString& error) {
        if (!yo) return;
        if (!error.isEmpty()) return yo->encargarAsamblea(id, QCoreApplication::translate("asistente", "No se le pudo mirar la cara: %1").arg(error));
        PersonasViewModel* p = personas(yo->vm_);
        const float umbral = p != nullptr ? p->umbralEntrarSolo() : 1.0f;
        const bool esEl = sirve && parecido >= umbral;
        auto* ev = static_cast<EventoViewModel*>(yo->vm_.evento());
        // Entra solo SI el padrón del evento lo dice: la misma lista que deja
        // pasar sola a la gente del lobby de la sala. Y se avisa siempre.
        if (esEl && ev != nullptr && ev->puedeEntrarSolo(personaId)) {
            if (asamblea::SesionRemota* s = yo->vm_.sesionRemota();
                s != nullptr && s->habilitar(id.toStdString(), true, "reconocimiento").empty()) {
                ev->acreditarPorPersona(personaId, "rostro");
                emit yo->notify(tr("«%1» quedó habilitado solo: la cámara lo reconoció y el padrón del evento lo deja "
                                   "entrar solo.")
                                    .arg(nombre),
                                "info");
                return;
            }
        }
        const QString pct = QString::number(static_cast<int>(std::lround(parecido * 100))) + " %";
        const QString hace = QString::number(static_cast<int>(std::lround(umbral * 100))) + " %";
        yo->encargarAsamblea(
            id, esEl ? QCoreApplication::translate(
                           "asistente", "La cámara lo reconoce (parecido %1), pero el padrón del evento no lo deja entrar solo.")
                           .arg(pct)
                : sirve ? QCoreApplication::translate("asistente", "La cara NO coincide con su ficha (parecido %1; para "
                                                                   "dejarlo entrar solo hace falta %2).")
                              .arg(pct, hace)
                        : QCoreApplication::translate("asistente", "No se le ve bien la cara (parecido %1).").arg(pct));
    });
}

void AtencionViewModel::encargarAsamblea(const QString& id, const QString& queSeVio) {
    const QVariantMap integrante = vm_.cuerpo().buscar(id);
    const QString nombre = integrante.value("nombre").toString();
    const QString banca = integrante.value("banca").toString();
    const QString texto =
        QCoreApplication::translate(
            "asistente",
            "«%1» (banca %2, id %3) se conectó a la sesión remota de la asamblea y espera que lo habiliten. Lo que vio "
            "la cámara: %4 Contáselo al operador en una línea con `operator_ask` (opciones «Habilitar» y «Todavía no») "
            "y, si contesta «Habilitar», llamá `sesion_habilitar {banca: \"%3\"}`. ")
            .arg(nombre, banca, id, queSeVio) +
        // Hablando, se le habla (y se lo oye) con las mismas herramientas del chat de la sala.
        (atiendeHablandoEnAsamblea(id)
             ? QCoreApplication::translate(
                   "asistente",
                   "Lo estás atendiendo HABLANDO: lo que le mandes con `chat_send {to: \"%1\"}` le llega dicho en voz "
                   "alta (y escrito en su pantalla), y lo que él dice te llega con `chat_wait {participant: \"%1\"}`. "
                   "Saludalo, decile que en un momento lo habilitan y, si hace falta, preguntale lo que el operador "
                   "quiera saber.")
                       .arg(id) +
                   instruccion("voz")
             : QCoreApplication::translate("asistente", "No le escribas: en la sesión remota el que espera no tiene chat.")) +
        QStringLiteral(" ") + QCoreApplication::translate("asistente", "Manejate sola.");
    switch (atendedor()) {
        case sala::Atendedor::Recepcion: {
            std::string err;
            vm_.despachador().encargar("recepcion", texto.toStdString(), "sesion:" + id.toStdString(), 0, &err);
            if (!err.empty()) emit notify(tr("La recepción no tomó a «%1»: %2").arg(nombre, QString::fromStdString(err)), "warn");
            return;
        }
        case sala::Atendedor::Automatizacion:
            static_cast<agent::AgentViewModel*>(vm_.agenteAutomatizacion())->send(texto);
            return;
        case sala::Atendedor::Nadie: break;
    }
    emit notify(tr("«%1» espera que lo habiliten en la sesión remota, y no hay quién lo atienda: %2")
                    .arg(nombre, QString::fromStdString(sala::porQueNadie(sala::Atendedor::Nadie))),
                "warn");
}

// --- La voz del atendedor ---------------------------------------------------------------

QString AtencionViewModel::vozElegida() const {
    // La elegida si está bajada; si no, la primera bajada (como la del operador).
    const QString r = QString::fromStdString(aj_.vozRepo);
    if (!r.isEmpty() && !modelos_.carpetaInstalada(r).empty()) return r;
    return modelos_.instaladoDeTipo("tts");
}

QVariantList AtencionViewModel::voces() const {
    QVariantList out;
    const QString elegida = vozElegida();
    for (const QVariant& v : modelos_.catalogo()) {
        QVariantMap m = v.toMap();
        if (m.value("tipo").toString() != "tts") continue;
        m["elegida"] = m.value("repo").toString() == elegida;
        out.push_back(m);
    }
    return out;
}

bool AtencionViewModel::rutasDeVoz(agent::ModelosLocales& modelos, const QString& repo, std::string& modelo,
                                   std::string& tokens, std::string& datos) {
    const std::filesystem::path d = repo.isEmpty() ? std::filesystem::path() : modelos.carpetaInstalada(repo);
    if (d.empty()) return false;
    QString archivo;
    for (const QVariant& v : modelos.catalogo())
        if (v.toMap().value("repo").toString() == repo) archivo = v.toMap().value("modelo").toString();
    modelo = (d / archivo.toStdString()).string();
    tokens = (d / "tokens.txt").string();
    datos = (d / "espeak-ng-data").string();
    return true;
}

void AtencionViewModel::revisarVoz() {
#if defined(BA_HAVE_VOZ)
    const QString repo = vozElegida();
    const std::filesystem::path d = repo.isEmpty() ? std::filesystem::path() : modelos_.carpetaInstalada(repo);
    if (d.empty()) {
        vozLista_ = false;
        vozCargada_.clear();
        emit cambio();
        return;
    }
    if (repo == vozCargada_) {
        emit cambio(); // el catálogo cambió (otra voz bajada): la lista se rearma
        return;
    }
    vozCargada_ = repo;
    vozLista_ = false;
    audio::Habla::Voz v;
    rutasDeVoz(modelos_, repo, v.modelo, v.tokens, v.datos);
    emit cambio();
    QPointer<AtencionViewModel> yo(this);
    habla_->cargar(v, [yo](const std::string& error) {
        QMetaObject::invokeMethod(qApp, [yo, error]() {
            if (!yo) return;
            yo->vozLista_ = yo->habla_->cargada();
            yo->hablantes_ = yo->habla_->hablantes();
            if (yo->aj_.vozHablante >= yo->hablantes_) yo->aj_.vozHablante = 0;
            if (!error.empty())
                emit yo->notify(tr("La voz del atendedor no cargó: %1").arg(QString::fromStdString(error)), "warn");
            emit yo->cambio();
        });
    });
#endif
}

void AtencionViewModel::elegirVoz(const QString& repo) {
    if (repo == vozElegida()) return;
    if (modelos_.carpetaInstalada(repo).empty()) {
        emit notify(tr("Primero hay que bajar esa voz (Configuración → Modelos locales)."), "warn");
        return;
    }
    aj_.vozRepo = repo.toStdString();
    aj_.vozHablante = 0;
    guardar();
    revisarVoz();
}

void AtencionViewModel::setHablante(int h) {
    if (h < 0 || h == aj_.vozHablante) return;
    aj_.vozHablante = h;
    guardar();
    emit cambio();
}

void AtencionViewModel::setVelocidad(double v) {
    v = std::clamp(v, 0.7, 1.5);
    if (v == aj_.vozVelocidad) return;
    aj_.vozVelocidad = v;
    guardar();
    emit cambio();
}

void AtencionViewModel::probarVoz(const QString& texto) { probar(texto, {}); }

void AtencionViewModel::probar(const QString& texto, std::function<void(const QVariantMap&)> fin) {
    const QString t = texto.trimmed().isEmpty()
                          ? tr("Hola, ¿qué tal? Soy la que atiende la conferencia. ¿Me decís tu nombre y apellido, "
                               "tal como querés que se lea en pantalla?")
                          : texto.trimmed();
#if defined(BA_HAVE_VOZ)
    if (!vozLista_) {
        prueba_ = {{"probando", false}, {"texto", t}, {"error", hablarFalta().isEmpty() ? tr("No hay voz cargada.") : hablarFalta()}};
        emit pruebaCambio();
        if (fin) fin(prueba_);
        return;
    }
    prueba_ = {{"probando", true}, {"texto", t}};
    emit pruebaCambio();
    // SE GENERA, NO SE REPRODUCE: probar una voz no puede sonar por los
    // parlantes del estudio (puede haber gente, o estar al aire). Se junta el
    // audio, se mide y se guarda en un .wav para escucharlo con auriculares.
    struct Toma {
        std::mutex lock;
        std::vector<float> audio;
        std::uint32_t frecuencia = 22050;
    };
    auto toma = std::make_shared<Toma>();
    QPointer<AtencionViewModel> yo(this);
    const QString ruta = QString::fromStdString((rt_.dataRoot() / "Logs" / "voz-atendedor-prueba.wav").string());
    habla_->decir(
        t.toStdString(), aj_.vozHablante, static_cast<float>(aj_.vozVelocidad),
        [toma](const float* m, std::size_t n, std::uint32_t f) {
            std::lock_guard<std::mutex> l(toma->lock);
            toma->frecuencia = f;
            toma->audio.insert(toma->audio.end(), m, m + n);
        },
        [yo, toma, t, ruta, fin]() {
            QMetaObject::invokeMethod(qApp, [yo, toma, t, ruta, fin]() {
                if (!yo) return;
                std::vector<float> a;
                std::uint32_t f = 22050;
                {
                    std::lock_guard<std::mutex> l(toma->lock);
                    a = toma->audio;
                    f = toma->frecuencia;
                }
                // La forma de onda: 120 columnas con el pico de cada tramo.
                QVariantList forma;
                const std::size_t columnas = 120;
                float pico = 0.0f;
                for (const float x : a) pico = std::max(pico, std::fabs(x));
                for (std::size_t c = 0; c < columnas && !a.empty(); ++c) {
                    const std::size_t i0 = a.size() * c / columnas, i1 = std::max(i0 + 1, a.size() * (c + 1) / columnas);
                    float m = 0.0f;
                    for (std::size_t i = i0; i < i1 && i < a.size(); ++i) m = std::max(m, std::fabs(a[i]));
                    forma << (pico > 0 ? m / pico : 0.0f);
                }
                const bool guardado = !a.empty() && escribirWav(ruta, a, f);
                yo->prueba_ = {{"probando", false},
                               {"texto", t},
                               {"segundos", f > 0 ? static_cast<double>(a.size()) / f : 0.0},
                               {"forma", forma},
                               {"archivo", guardado ? ruta : QString()},
                               {"error", a.empty() ? tr("La voz no generó audio.") : QString()}};
                emit yo->pruebaCambio();
                if (fin) fin(yo->prueba_);
            });
        });
#else
    prueba_ = {{"probando", false}, {"texto", t}, {"error", tr("Esta compilación no trae el motor de voz.")}};
    emit pruebaCambio();
    if (fin) fin(prueba_);
#endif
}

// --- MCP --------------------------------------------------------------------------------

nlohmann::json AtencionViewModel::estado() const {
    using json = nlohmann::json;
    json turnos = json::array();
    for (const auto& t : turnos_.todos())
        turnos.push_back({{"id", t.id},
                          {"nombre", t.nombre},
                          {"ambito", sala::claveDe(t.ambito)},
                          {"quien", sala::claveDe(t.quien)},
                          {"modo", sala::claveDe(t.modo)}});
    json lasVoces = json::array();
    for (const QVariant& v : this->voces())
        lasVoces.push_back({{"repo", v.toMap().value("repo").toString().toStdString()},
                         {"nombre", v.toMap().value("nombre").toString().toStdString()},
                         {"instalada", v.toMap().value("instalado").toBool()}});
    const PersonasViewModel* pv = personas(vm_);
    return {{"sala", {{"atiende", salaAtiende()}, {"hablando", aj_.salaHablando}}},
            {"asamblea", {{"atiende", aj_.asambleaAtiende}, {"hablando", aj_.asambleaHablando}}},
            {"quienAtiende", sala::claveDe(atendedor())},
            {"quienAtiendeTexto", quienAtiendeTexto().toStdString()},
            {"puedeHablar", puedeHablar()},
            {"hablarFalta", hablarFalta().toStdString()},
            {"conLaVoz", vozId_.toStdString()},
            {"conLaVozAmbito", vozId_.isEmpty() ? "" : sala::claveDe(vozAmbito_)},
            {"altaAutomatica", pv != nullptr && pv->altaAutomatica()},
            {"lobbyMirando", pv != nullptr && pv->lobbyMirando()},
            {"turnos", turnos},
            {"voz", {{"repo", vozElegida().toStdString()}, {"lista", vozLista_}, {"hablante", aj_.vozHablante},
                     {"hablantes", hablantes_}, {"velocidad", aj_.vozVelocidad}, {"voces", lasVoces}}},
            {"entranSolos", ba::i18n::traducir("asistente", "La lista de quién entra solo si se lo reconoce es la del padrón del evento: "
                                                           "padron_ver {solos: true}; se cambia con padron_cambiar {id, dejarPasarSolo}.")}};
}

QString AtencionViewModel::configurar(const nlohmann::json& c) {
    if (!c.is_object()) return tr("Se esperaba un objeto.");
    const auto booleano = [&c](const char* k, bool& out) {
        const auto it = c.find(k);
        if (it == c.end() || !it->is_boolean()) return false;
        out = it->get<bool>();
        return true;
    };
    bool b = false;
    if (booleano("salaAtiende", b)) {
        setSalaAtiende(b);
        if (salaAtiende() != b) return QString::fromStdString(sala::porQueNadie(atendedor()));
    }
    if (booleano("salaHablando", b)) setSalaHablando(b);
    if (booleano("asambleaAtiende", b)) {
        setAsambleaAtiende(b);
        if (aj_.asambleaAtiende != b) return QString::fromStdString(sala::porQueNadie(atendedor()));
    }
    if (booleano("asambleaHablando", b)) setAsambleaHablando(b);
    if (booleano("altaAutomatica", b)) {
        PersonasViewModel* pv = personas(vm_);
        if (pv == nullptr || !pv->disponible()) return tr("Este equipo no tiene reconocimiento de rostros.");
        pv->setAltaAutomatica(b);
    }
    if (const auto v = c.find("voz"); v != c.end() && v->is_object()) {
        if (const auto r = v->find("repo"); r != v->end() && r->is_string()) {
            const QString repo = QString::fromStdString(r->get<std::string>());
            if (modelos_.carpetaInstalada(repo).empty())
                return tr("La voz «%1» no está bajada (modelos_locales / Configuración → Modelos locales).").arg(repo);
            elegirVoz(repo);
        }
        if (const auto h = v->find("hablante"); h != v->end() && h->is_number_integer()) setHablante(h->get<int>());
        if (const auto s = v->find("velocidad"); s != v->end() && s->is_number()) setVelocidad(s->get<double>());
    }
    return {};
}

} // namespace ba::app
