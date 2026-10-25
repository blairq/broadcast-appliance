// ============================================================================
// src/app/ui/AtencionViewModel.cpp — ver AtencionViewModel.hpp
// ============================================================================
#include "ui/AtencionViewModel.hpp"

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
    if (!on) {
        turnos_.soltarLaVoz();
        cerrarVoz();
    } else if (puedeHablar()) {
        // Los que ya se están atendiendo por chat: el primero pasa a voz.
        if (const std::string id = turnos_.pasarLaVoz(); !id.empty()) abrirVoz(QString::fromStdString(id));
    }
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
        turnos_.terminarTodos(sala::Ambito::Asamblea);
        emit turnosCambio();
    }
    emit cambio();
    if (on) alAsamblea(); // los que ya estaban esperando también
}

QString AtencionViewModel::asambleaHablandoMotivo() const {
    return "Todavía no: el que espera en la sesión remota manda solo la foto de su cámara (su micrófono se abre "
           "recién cuando tiene la palabra), así que no hay por dónde escucharlo. Se lo atiende mirándole la cara.";
}

sala::Atendedor AtencionViewModel::atendedor() const {
    const agent::Rol* r = vm_.despachador().roles().porId("recepcion");
    const auto* ag = static_cast<agent::AgentViewModel*>(vm_.agenteAutomatizacion());
    return sala::quienAtiende(r != nullptr && r->activo, ag != nullptr && ag->running());
}

QString AtencionViewModel::quienAtiendeTexto() const {
    const sala::Atendedor a = atendedor();
    if (a == sala::Atendedor::Nadie) return QString::fromStdString(sala::porQueNadie(a));
    return "Atiende " + QString::fromStdString(sala::nombreDe(a)) + ".";
}

QString AtencionViewModel::hablarFalta() const {
    if (!voz_.disponible()) return "Esta compilación no trae el motor de voz.";
    if (!voz_.lista())
        return "Falta el reconocedor de voz (voz a texto): bajalo en Configuración → Modelos locales.";
    if (!vozLista_)
        return "Falta la voz del atendedor: elegila en Configuración → Voz del sistema → Voz del atendedor.";
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
    const auto t = turnos_.empezar(id.toStdString(), nombre.toStdString(), sala::Ambito::Sala, quien,
                                   aj_.salaHablando && puedeHablar());
    if (t.modo == sala::Modo::Voz && vozId_ != id) abrirVoz(id);
    emit turnosCambio();
    return QString::fromLatin1(sala::claveDe(t.modo));
}

QString AtencionViewModel::instruccion(const QString& modo) {
    if (modo != "voz") return {};
    return " ESTA PERSONA ESTÁ ATENDIDA HABLANDO: todo lo que le mandes con `chat_send` le llega también "
           "dicho en voz alta, y lo que ella dice te llega como mensajes suyos (con `voz: true`) por "
           "`chat_wait` o `invitados_chat_leer`. Escribí como se habla: frases cortas, de a una pregunta, sin "
           "listas, sin emojis ni símbolos, y los números como se dicen. Si algo de lo que te llega no se "
           "entiende (la voz a texto a veces se equivoca), pedile que lo repita. Los datos para la ficha "
           "(nombre, cargo) confirmáselos en voz alta antes de guardarlos.";
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
    if (vozId_.isEmpty() && aj_.salaHablando && salaAtiende() && puedeHablar()) {
        if (const std::string id = turnos_.pasarLaVoz(); !id.empty()) {
            abrirVoz(QString::fromStdString(id));
            cambio = true;
        }
    }
    // Las posiciones del lobby se reciclan: al entrar uno, el siguiente toma
    // su lugar. Si a quien se escucha le cambió, se lo sigue al lugar nuevo.
    if (!vozId_.isEmpty()) {
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
    vozId_ = id;
    QPointer<AtencionViewModel> yo(this);
    // La sala sube su voz y le baja la del que atiende. Si no puede (se quedó
    // sin lugar en el lobby, la sala es vieja), se sigue por chat: la
    // atención no se corta por la voz.
    conf->participantes().atender(id.toStdString(), true, [yo, id](const std::string& err, const nlohmann::json&) {
        if (!yo || err.empty() || yo->vozId_ != id) return;
        const auto* t = yo->turnos_.turno(id.toStdString());
        const QString nombre = t != nullptr ? QString::fromStdString(t->nombre) : id;
        emit yo->notify("No se pudo atender hablando a «" + nombre + "»: " + QString::fromStdString(err) +
                            ". Sigue por chat.",
                        "warn");
        yo->turnos_.soltarLaVoz();
        yo->cerrarVoz();
        emit yo->turnosCambio();
    });
    voz_.abrirOido(claveOido(id), [yo, id](const QString& texto) {
        if (!yo) return;
        if (ConferenceViewModel* c = conferencia(yo->vm_)) c->agregarDichoPorVoz(id, texto);
    });
    vozLugar_ = -1; // alSala lo engancha a su lugar (y lo sigue si cambia)
    alLobby_->abrir();
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
    // Si todavía espera, que la sala deje de subir su voz. Si ya entró, la sala
    // lo apagó sola y esto contesta que no está en el lobby: no importa.
    if (ConferenceViewModel* conf = conferencia(vm_)) conf->participantes().atender(id.toStdString(), false);
}

void AtencionViewModel::alChatear(const std::string& para, const std::string& texto) {
    if (vozId_.isEmpty() || QString::fromStdString(para) != vozId_) return;
    decir(QString::fromStdString(texto));
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
        cambio = true;
    }
    if (aj_.asambleaAtiende) {
        for (const std::string& id : esperando) {
            if (turnos_.turno(id) != nullptr) continue; // ya se lo está atendiendo
            turnos_.empezar(id, nombres[id], sala::Ambito::Asamblea, atendedor(), false);
            cambio = true;
            const QString qid = QString::fromStdString(id);
            esperandoFoto_.insert(qid);
            s->pedirFoto(qid);
            // Sin foto en un rato (cámara tapada, celular viejo), igual se lo atiende.
            QTimer::singleShot(8000, this, [this, qid]() {
                if (esperandoFoto_.remove(qid)) encargarAsamblea(qid, "No llegó la foto de su cámara.");
            });
        }
    }
    if (cambio) emit turnosCambio();
}

void AtencionViewModel::alFotoIntegrante(const QString& id) {
    if (!esperandoFoto_.remove(id)) return; // la pidió otro (la vista)
    asamblea::SesionRemota* s = vm_.sesionRemota();
    PersonasViewModel* pv = personas(vm_);
    if (s == nullptr) return;
    if (pv == nullptr || !pv->disponible()) return encargarAsamblea(id, "Este equipo no reconoce caras.");
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
                                    emit yo->notify("Anoté a «" + nombre +
                                                        "» en Personas con la cara de su cámara y lo vinculé a su "
                                                        "banca: la próxima vez se lo reconoce.",
                                                    "info");
                            });
        return encargarAsamblea(id, "No tiene ficha de rostro: no hay con qué compararlo.");
    }
    pv->compararFoto(jpeg, personaId, [yo, id, personaId, nombre](float parecido, bool sirve, const QString& error) {
        if (!yo) return;
        if (!error.isEmpty()) return yo->encargarAsamblea(id, "No se le pudo mirar la cara: " + error);
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
                emit yo->notify("«" + nombre +
                                    "» quedó habilitado solo: la cámara lo reconoció y el padrón del evento lo deja "
                                    "entrar solo.",
                                "info");
                return;
            }
        }
        const QString pct = QString::number(static_cast<int>(std::lround(parecido * 100))) + " %";
        yo->encargarAsamblea(
            id, esEl ? "La cámara lo reconoce (parecido " + pct + "), pero el padrón del evento no lo deja entrar solo."
                : sirve ? "La cara NO coincide con su ficha (parecido " + pct + "; para dejarlo entrar solo hace falta " +
                              QString::number(static_cast<int>(std::lround(umbral * 100))) + " %)."
                        : "No se le ve bien la cara (parecido " + pct + ").");
    });
}

void AtencionViewModel::encargarAsamblea(const QString& id, const QString& queSeVio) {
    const QVariantMap integrante = vm_.cuerpo().buscar(id);
    const QString nombre = integrante.value("nombre").toString();
    const QString banca = integrante.value("banca").toString();
    const QString texto =
        "«" + nombre + "» (banca " + banca + ", id " + id +
        ") se conectó a la sesión remota de la asamblea y espera que lo habiliten. Lo que vio la cámara: " + queSeVio +
        " Contáselo al operador en una línea con `operator_ask` (opciones «Habilitar» y «Todavía no») y, si "
        "contesta «Habilitar», llamá `sesion_habilitar {banca: \"" + id +
        "\"}`. No le escribas: en la sesión remota el que espera no tiene chat. Manejate sola.";
    switch (atendedor()) {
        case sala::Atendedor::Recepcion: {
            std::string err;
            vm_.despachador().encargar("recepcion", texto.toStdString(), "sesion:" + id.toStdString(), 0, &err);
            if (!err.empty()) emit notify("La recepción no tomó a «" + nombre + "»: " + QString::fromStdString(err), "warn");
            return;
        }
        case sala::Atendedor::Automatizacion:
            static_cast<agent::AgentViewModel*>(vm_.agenteAutomatizacion())->send(texto);
            return;
        case sala::Atendedor::Nadie: break;
    }
    emit notify("«" + nombre + "» espera que lo habiliten en la sesión remota, y no hay quién lo atienda: " +
                    QString::fromStdString(sala::porQueNadie(sala::Atendedor::Nadie)),
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
    QString modelo;
    for (const QVariant& v : modelos_.catalogo())
        if (v.toMap().value("repo").toString() == repo) modelo = v.toMap().value("modelo").toString();
    audio::Habla::Voz v;
    v.modelo = (d / modelo.toStdString()).string();
    v.tokens = (d / "tokens.txt").string();
    v.datos = (d / "espeak-ng-data").string();
    emit cambio();
    QPointer<AtencionViewModel> yo(this);
    habla_->cargar(v, [yo](const std::string& error) {
        QMetaObject::invokeMethod(qApp, [yo, error]() {
            if (!yo) return;
            yo->vozLista_ = yo->habla_->cargada();
            yo->hablantes_ = yo->habla_->hablantes();
            if (yo->aj_.vozHablante >= yo->hablantes_) yo->aj_.vozHablante = 0;
            if (!error.empty())
                emit yo->notify("La voz del atendedor no cargó: " + QString::fromStdString(error), "warn");
            emit yo->cambio();
        });
    });
#endif
}

void AtencionViewModel::elegirVoz(const QString& repo) {
    if (repo == vozElegida()) return;
    if (modelos_.carpetaInstalada(repo).empty()) {
        emit notify("Primero hay que bajar esa voz (Configuración → Modelos locales).", "warn");
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
                          ? QString("Hola, ¿qué tal? Soy la que atiende la sala. ¿Me decís tu nombre y apellido, "
                                    "tal como querés que se lea en pantalla?")
                          : texto.trimmed();
#if defined(BA_HAVE_VOZ)
    if (!vozLista_) {
        prueba_ = {{"probando", false}, {"texto", t}, {"error", hablarFalta().isEmpty() ? "No hay voz cargada." : hablarFalta()}};
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
                               {"error", a.empty() ? QString("La voz no generó audio.") : QString()}};
                emit yo->pruebaCambio();
                if (fin) fin(yo->prueba_);
            });
        });
#else
    prueba_ = {{"probando", false}, {"texto", t}, {"error", "Esta compilación no trae el motor de voz."}};
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
            {"asamblea", {{"atiende", aj_.asambleaAtiende}, {"hablando", false},
                          {"porQueNoHablando", asambleaHablandoMotivo().toStdString()}}},
            {"quienAtiende", sala::claveDe(atendedor())},
            {"quienAtiendeTexto", quienAtiendeTexto().toStdString()},
            {"puedeHablar", puedeHablar()},
            {"hablarFalta", hablarFalta().toStdString()},
            {"conLaVoz", vozId_.toStdString()},
            {"altaAutomatica", pv != nullptr && pv->altaAutomatica()},
            {"lobbyMirando", pv != nullptr && pv->lobbyMirando()},
            {"turnos", turnos},
            {"voz", {{"repo", vozElegida().toStdString()}, {"lista", vozLista_}, {"hablante", aj_.vozHablante},
                     {"hablantes", hablantes_}, {"velocidad", aj_.vozVelocidad}, {"voces", lasVoces}}},
            {"entranSolos", "La lista de quién entra solo si se lo reconoce es la del padrón del evento: "
                            "padron_ver {solos: true}; se cambia con padron_cambiar {id, dejarPasarSolo}."}};
}

QString AtencionViewModel::configurar(const nlohmann::json& c) {
    if (!c.is_object()) return "Se esperaba un objeto.";
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
    if (booleano("altaAutomatica", b)) {
        PersonasViewModel* pv = personas(vm_);
        if (pv == nullptr || !pv->disponible()) return "Este equipo no tiene reconocimiento de rostros.";
        pv->setAltaAutomatica(b);
    }
    if (const auto v = c.find("voz"); v != c.end() && v->is_object()) {
        if (const auto r = v->find("repo"); r != v->end() && r->is_string()) {
            const QString repo = QString::fromStdString(r->get<std::string>());
            if (modelos_.carpetaInstalada(repo).empty())
                return "La voz «" + repo + "» no está bajada (modelos_locales / Configuración → Modelos locales).";
            elegirVoz(repo);
        }
        if (const auto h = v->find("hablante"); h != v->end() && h->is_number_integer()) setHablante(h->get<int>());
        if (const auto s = v->find("velocidad"); s != v->end() && s->is_number()) setVelocidad(s->get<double>());
    }
    return {};
}

} // namespace ba::app
