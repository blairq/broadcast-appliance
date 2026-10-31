// src/app/ui/AsistenteViewModel.cpp — ver el .hpp

#include "ui/AsistenteViewModel.hpp"

#include "agent/AgentViewModel.hpp"
#include "agent/AlertEngine.hpp"
#include "agent/Despachador.hpp"
#include "agent/EstadoIA.hpp"
#include "agent/FlujosTexto.hpp"
#include "agent/ModelosLocales.hpp"
#include "agent/Roles.hpp"
#include "agent/RotuloTraduccionConfig.hpp"
#include "agent/ServidorModeloLocal.hpp"
#include "agent/Subtitulos.hpp"
#include "agent/SubtitulosCC.hpp"
#include "agent/Traductor.hpp"
#include "agent/Vocabulario.hpp"
#include "agent/VocesRack.hpp"
#include "agent/Voz.hpp"
#include "agent/VozPrompter.hpp"
#include "ajustes/Ajustes.hpp"
#include "arranque/AppRuntime.hpp"
#include "asamblea/Cuerpo.hpp"
#include "control/Dsk.hpp"
#include "emision/VentanillaMotor.hpp"
#include "fault/ModoSeguro.hpp"
#include "ui/AlarmasViewModel.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/ManualesViewModel.hpp"
#include "ui/OperatorMessages.hpp"
#include "ui/PuestoCorreccion.hpp"
#include "ui/RielConfig.hpp"

#if defined(BA_HAVE_CLAP)
#include "audio/RackManager.hpp"
#endif

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QStringList>

namespace ba::app {

namespace {

/// Sin padre y devuelto a QML: que no se lo quede su recolector. Es del área.
QObject* deCpp(QObject* o) {
    if (o != nullptr) QQmlEngine::setObjectOwnership(o, QQmlEngine::CppOwnership);
    return o;
}

QVariant vocabQv(const nlohmann::json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(nlohmann::json{{"v", j}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        .object()
        .value("v")
        .toVariant();
}

} // namespace

AsistenteViewModel::AsistenteViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    vocabulario_ = std::make_unique<agent::Vocabulario>(p_.rt.dataRoot() / "Config" / "vocabulario.json");
    rotulo_ = std::make_unique<agent::RotuloTraduccionConfig>(p_.rt.dataRoot());
    armarAsistentes();
    armarMensajes();
    armarTextos();
}

AsistenteViewModel::~AsistenteViewModel() = default;

void AsistenteViewModel::armarAsistentes() {
    agent::AlertEngine& vigia = p_.alarmas.motor();
    agent_ = std::make_unique<agent::AgentViewModel>(p_.rt, p_.ventanilla, vigia, "evento");
    // El de automatización: el que recibe las alertas (siempre levantado).
    agenteAuto_ = std::make_unique<agent::AgentViewModel>(p_.rt, p_.ventanilla, vigia, "automatizacion");
    connect(agent_.get(), &agent::AgentViewModel::notify, this, &AsistenteViewModel::notify);
    connect(agenteAuto_.get(), &agent::AgentViewModel::notify, this, &AsistenteViewModel::notify);
    // Reiniciada con una conversación para retomar (--reanudar-asistente=<id>|<proveedor>|<modelo>,
    // ver el reinicio de Configuraciones): el asistente arranca con ella y se muestra.
    for (const QString& a : QCoreApplication::arguments())
        if (a.startsWith("--reanudar-asistente=")) {
            const QStringList pa = a.mid(21).split('|');
            QString evento;
            for (const QString& b : QCoreApplication::arguments())
                if (b.startsWith("--asistente-evento=")) evento = b.mid(19);
            const QString seguir =
                evento.isEmpty() ? QString()
                                 : QCoreApplication::translate(
                                       "asistente",
                                       "La app se reinició y ya está abierto el evento «%1». Si estabas en medio "
                                       "de algo, seguí desde donde quedaste; si no, esperá lo que te pida el operador.")
                                       .arg(evento);
            agent_->reanudarAlArrancar(pa.value(0), pa.value(1), pa.mid(2).join('|'), seguir);
        }
    // Las reglas las cambia el motor (interfaz, MCP, plugins): la lista del
    // asistente se entera por él, sin que cada llamador se acuerde de avisar.
    connect(&vigia, &agent::AlertEngine::reglasCambio, agent_.get(), &agent::AgentViewModel::alertsChanged);
    connect(&vigia, &agent::AlertEngine::reglasCambio, agenteAuto_.get(), &agent::AgentViewModel::alertsChanged);
    // Los roles automaticos: un disparo que un rol activo reclama va a un
    // agente de un tiro y NO al chat del asistente interactivo.
    roles_ = std::make_unique<agent::Roles>(p_.rt.dataRoot() / "Config" / "roles.json");
    despachador_ = std::make_unique<agent::Despachador>(*roles_, agent_->motores());
    agent_->setCorridasEnCurso([d = despachador_.get()]() { return d->enCurso(); });
    agenteAuto_->setCorridasEnCurso([d = despachador_.get()]() { return d->enCurso(); });
    // Los manuales de usuario (Configuración → Sistema → Manuales): los arma el asistente de evento.
    manuales_ = std::make_unique<ManualesViewModel>(ManualesViewModel::Partes{
        .enVivo = p_.enVivo,
        .encargar = [this](const QString& t) { if (agent_) agent_->encargar(t); },
        .faltaAsistente = [this]() { return agent_ ? agent_->faltaParaEncargo() : QString("El asistente no está disponible."); },
        .paginas = []() { return RielConfig::idsDePaginas(); }});
    connect(despachador_.get(), &agent::Despachador::notify, this, &AsistenteViewModel::notify);
    // Las alertas las recibe el de automatización: él se las ofrece primero a los roles
    // (una sola vez por alerta; el del evento ya no las mira).
    agenteAuto_->setReclamado([this](const nlohmann::json& ev) { return despachador_->evento(ev); });
    // El modelo local: sus archivos en Modelos/ (portable), el motor junto a ba-app.
    modelos_ = std::make_unique<agent::ModelosLocales>(p_.rt.dataRoot(), QCoreApplication::applicationDirPath());
    connect(modelos_.get(), &agent::ModelosLocales::notify, this, &AsistenteViewModel::notify);
    QTimer::singleShot(0, modelos_.get(), [m = modelos_.get()]() { m->arrancarSiCorresponde(); });
    voz_ = std::make_unique<agent::Voz>(p_.rt, p_.ventanilla, *modelos_);
    modelos_->setProbadorTc([v = voz_.get()](const QString& frase, agent::ModelosLocales::Respuesta r) {
        v->probarTc(frase, std::move(r));
    });
    connect(voz_.get(), &agent::Voz::notify, this, &AsistenteViewModel::notify);
    vozPrompter_ = std::make_unique<agent::VozPrompter>(p_.rt, *voz_, &p_.sala);
    // El modelo local como motor de agentes y del asistente (proveedor «local»).
    agent::Voz* v = voz_.get();
    agent_->setLocal([v](const QString& quien, const QString& texto, bool disparador) { v->atenderDesde(texto, quien, disparador); });
    // El chat del modelo local (pi + llama-server): qué .gguf y cómo bajar el que falta.
    for (agent::AgentViewModel* a : {agent_.get(), agenteAuto_.get()})
        a->setModeloChat(
            [m = modelos_.get()](const QString& pedido, QString* falta, QString* repo) { return m->ggufParaChat(pedido, falta, repo); },
            [m = modelos_.get()](const QString& repo) { m->descargar(repo); });
    // El llama-server carga la visión del modelo (--mmproj) si está bajada: la usan
    // el chat y el tótem (el dorso de un documento). Quién va con qué, ModelosLocales.
    agent::ServidorModeloLocal::el().setMmprojPara([m = modelos_.get()](const QString& gguf) { return m->mmprojPara(gguf); });
    agenteAuto_->setLocal([v](const QString& quien, const QString& texto, bool disparador) { v->atenderDesde(texto, quien, disparador); });
    v->setAsistenteGrande([a = agent_.get()]() { return a->asistenteGrande(); });
    v->setAsistenteLocal([a = agent_.get()]() { return a->asistenteLocal(); });
    // El micrófono del operador se cambia en Configuración: Sala lo muestra.
    connect(voz_.get(), &agent::Voz::cambio, &p_.sala, &ConferenceViewModel::operatorChanged);
}

void AsistenteViewModel::armarMensajes() {
    // Barra de mensajes del asistente al operador (y respuestas a invitados).
    messages_ = std::make_unique<OperatorMessages>();
    if (ba::fault::delProceso().activo()) {
        // El launcher arrancó en modo seguro tras varias caídas (F1-10): que el
        // operador sepa qué se apagó (nivel 1-2: las escenas están) o por qué
        // no ve sus escenas (nivel 3).
        messages_->post({{"kind", "message"},
                         {"priority", "critical"},
                         {"title", tr("Modo seguro").toStdString()},
                         {"text", ba::fault::delProceso().aviso()}});
    }
    messages_->setChatSender([this](const QString& to, const QString& text) {
        p_.sala.sendChat(to, text);
        return QString();
    });
    messages_->setCredentialSink([this](const QString& platform, const QVariantMap& values) {
        return p_.guardarCredencial ? p_.guardarCredencial(platform, values) : QString();
    });
    messages_->setLateAnswerSink([this](const nlohmann::json& item) {
        const std::string kind = item.value("kind", "");
        const std::string st = item.value("status", "");
        const QString text = QString::fromStdString(item.value("text", ""));
        const QString answer = QString::fromStdString(item.value("answer", ""));
        // Sin el nombre (lo eligio el invitado): solo su id.
        const QString who = QCoreApplication::translate("asistente", "el invitado %1")
                                .arg(QString::fromStdString(item.value("participantId", "?")));
        // Las marcas «[OPERADOR]» y «[RESPUESTA DEL OPERADOR]» no se traducen: la ayuda
        // del MCP y las descripciones de las herramientas le dicen al asistente que las busque.
        const QString op = QStringLiteral("[OPERADOR] ");
        QString note;
        if (kind == "question")
            note = st == "answered" ? QStringLiteral("[RESPUESTA DEL OPERADOR] ") +
                                          QCoreApplication::translate("asistente", "a tu pregunta \"%1\": %2").arg(text, answer)
                                    : op + QCoreApplication::translate("asistente", "descarto tu pregunta \"%1\".").arg(text);
        else if (kind == "reply")
            note = st == "sent" ? op + QCoreApplication::translate("asistente", "le mando a %1: \"%2\"").arg(who, answer)
                                : op + QCoreApplication::translate("asistente", "ignoro el mensaje de %1.").arg(who);
        else if (kind == "credential") {
            const QString pl = QString::fromStdString(item.value("platform", ""));
            if (pl.startsWith("camaras:"))
                note = op + (st == "saved"
                                 ? QCoreApplication::translate(
                                       "asistente",
                                       "guardo usuario y clave de las camaras %1 (las que dejo sin clave, no). "
                                       "Siguiente paso: camara_ficha de cada una.")
                                       .arg(pl.mid(8))
                                 : QCoreApplication::translate("asistente", "cerro el formulario de las camaras %1 sin guardar.")
                                       .arg(pl.mid(8)));
            else if (pl.startsWith("camara:"))
                note = op + (st == "saved"
                                 ? QCoreApplication::translate(
                                       "asistente",
                                       "guardo usuario y clave de la camara %1. Siguiente paso: camara_ficha {\"ip\": \"%1\"}.")
                                       .arg(pl.mid(7))
                                 : QCoreApplication::translate("asistente", "cerro el formulario de la camara %1 sin guardar.")
                                       .arg(pl.mid(7)));
            else
                note = op + (st == "saved"
                                 ? QCoreApplication::translate("asistente", "guardo la credencial de %1. Siguiente paso: %1_login.").arg(pl)
                                 : QCoreApplication::translate("asistente", "cerro el formulario de credenciales de %1 sin guardar.")
                                       .arg(pl));
        }
        // Al asistente que lo pidió (el de automatización atiende el lobby y los avisos).
        if (!note.isEmpty()) (item.value("asistente", "") == "automatizacion" ? agenteAuto_ : agent_)->deliverNote(note);
    });
}

void AsistenteViewModel::armarTextos() {
    // Las salidas de texto (antes que la Voz las use).
    flujos_ = std::make_unique<agent::FlujosTexto>(p_.rt.dataRoot());
    if (EventoViewModel* ev = p_.evento) flujos_->setCarpetaDelEvento([ev]() { return ev->carpetaEvento(); });
    // El puesto web del corrector de subtítulos (/correccion/, T44).
    puestoCorreccion_ = std::make_unique<PuestoCorreccion>(p_.sala.client(), *flujos_, this);
    // Los cuadros «Subtítulos» de las escenas muestran una salida de texto.
    subtitulos_ = std::make_unique<agent::Subtitulos>(*flujos_);
    // Subtítulos ocultos (CC) en la transmisión: 608 o la API de YouTube.
    // El 608 se le pide al motor y no se espera: a cuántas salidas llegó es lo que contesta
    // hoy el motor (M7: todavía no los mete, ninguna).
    cc_ = std::make_unique<agent::SubtitulosCC>(*flujos_, p_.rt.dataRoot(), [this](const QString& t, double s) {
        p_.ventanilla.pedir("cc_608", [&rt = p_.rt, t = t.toStdString(), s]() { rt.engine().enviarCaption(t, s); });
        return 0;
    });
    // «Traducción automática» rotulada mientras sale una traducción (pantalla o CC).
    rotuloApagar_.setSingleShot(true);
    connect(&rotuloApagar_, &QTimer::timeout, this, [this]() {
        if (control::Dsk* dsk = p_.dsk ? p_.dsk() : nullptr) dsk->mostrar(rotulo_->capa(), false, true);
    });
    connect(flujos_.get(), &agent::FlujosTexto::fraseConOrigen, this,
            [this](const QString& flujo, const QString&, const QString& origen) { rotularTraduccion(flujo, origen); });
    voz_->setFlujos(flujos_.get());
    // Las tarjetas «Texto a voz»: dicen lo que llega por una salida de texto.
    voces_ = std::make_unique<agent::VocesRack>(*modelos_);
    voces_->setFlujos(flujos_.get());
    voces_->setVozSistema([v = voz_.get()]() { return v != nullptr ? v->vozElegida() : QString(); });
    // La traducción en vivo: de una salida de texto a otra, en otro idioma.
    traductor_ = std::make_unique<agent::Traductor>(p_.rt.dataRoot(), *modelos_, *flujos_);
    // Lo que no anda de la IA, junto (la pastilla «IA» y ia_estado).
    estadoIA_ = std::make_unique<agent::EstadoIA>(agent_.get(), modelos_.get(), voz_.get(), vozPrompter_.get(), traductor_.get());
}

void AsistenteViewModel::conRacks(audio::RackManager& racks) {
#if defined(BA_HAVE_CLAP)
    auto* r = &racks;
    voz_->setRacks(r); // la voz sale de los nodos «Voz a texto»
    // Las salidas de texto: las de las tarjetas «Voz a texto» y las traducidas.
    flujos_->setConfig([r, t = traductor_.get()](const QString& flujo) {
        nlohmann::json c = r->textoDe(flujo.toStdString());
        if (c.is_null() && t != nullptr) c = t->salidaDe(flujo.toStdString());
        return c;
    });
    flujos_->setLista([r, t = traductor_.get()]() {
        nlohmann::json l = r->textos();
        if (t != nullptr)
            for (const auto& s : t->salidas()) l.push_back(s);
        return l;
    });
    voces_->setRacks(r);
    connect(r, &audio::RackManager::changed, voces_.get(), &agent::VocesRack::cambio);
    connect(r, &audio::RackManager::changed, flujos_.get(), &agent::FlujosTexto::cambio);
    // Cada traducción es una tarjeta «Traducir» de un rack.
    traductor_->setTarjetas({[r]() { return r->traducciones(); },
                             [r](const nlohmann::json& t, std::string* id) {
                                 std::string e;
                                 *id = r->addTraduccion(t, &e);
                                 return e;
                             },
                             [r](const std::string& id, const nlohmann::json& c) { return r->setTraduccionOpciones(id, c); },
                             [r](const std::string& id) { return r->quitarTraduccion(id); }});
    connect(r, &audio::RackManager::changed, traductor_.get(), &agent::Traductor::sincronizar);
    connect(r, &audio::RackManager::editado, traductor_.get(), &agent::Traductor::sincronizar);
#else
    (void)racks;
#endif
}

QObject* AsistenteViewModel::agenteQml() const { return deCpp(agent_.get()); }
QObject* AsistenteViewModel::agenteAutomatizacionQml() const { return deCpp(agenteAuto_.get()); }
QObject* AsistenteViewModel::despachadorQml() const { return deCpp(despachador_.get()); }
QObject* AsistenteViewModel::modelosQml() const { return deCpp(modelos_.get()); }
QObject* AsistenteViewModel::vozQml() const { return deCpp(voz_.get()); }
QObject* AsistenteViewModel::vozPrompterQml() const { return deCpp(vozPrompter_.get()); }
QObject* AsistenteViewModel::flujosQml() const { return deCpp(flujos_.get()); }
QObject* AsistenteViewModel::puestoCorreccionQml() const { return deCpp(puestoCorreccion_.get()); }
QObject* AsistenteViewModel::ccQml() const { return deCpp(cc_.get()); }
QObject* AsistenteViewModel::vocesQml() const { return deCpp(voces_.get()); }
QObject* AsistenteViewModel::traductorQml() const { return deCpp(traductor_.get()); }
QObject* AsistenteViewModel::estadoIAQml() const { return deCpp(estadoIA_.get()); }
QObject* AsistenteViewModel::mensajesQml() const { return deCpp(messages_.get()); }
QObject* AsistenteViewModel::manualesQml() const { return deCpp(manuales_.get()); }

QVariantMap AsistenteViewModel::vocabularioUi() const {
    QVariantMap m = vocabQv(vocabulario_->todo()).toMap();
    m["reemplazos"] = vocabQv(vocabulario_->ultimosReemplazos());
    return m;
}

QString AsistenteViewModel::vocabularioAgregarUi(const QString& termino, const QString& variantes, const QString& grupo) {
    std::vector<std::string> v;
    for (const QString& x : variantes.split(',', Qt::SkipEmptyParts))
        if (!x.trimmed().isEmpty()) v.push_back(x.trimmed().toStdString());
    return QString::fromStdString(vocabulario_->agregar(termino.trimmed().toStdString(), v, grupo.toStdString()));
}

bool AsistenteViewModel::vocabularioQuitarUi(const QString& termino) { return vocabulario_->quitar(termino.toStdString()); }

void AsistenteViewModel::vocabularioGrupoUi(const QString& grupo, bool activo) {
    vocabulario_->activarGrupo(grupo.toStdString(), activo);
}

QVariantMap AsistenteViewModel::vocabularioProbarUi(const QString& texto) {
    nlohmann::json r = nlohmann::json::array();
    const std::string t = vocabulario_->corregir(texto.toStdString(), &r);
    return {{"texto", QString::fromStdString(t)}, {"reemplazos", vocabQv(r)}};
}

int AsistenteViewModel::vocabularioDelCuerpoUi() {
    const asamblea::Cuerpo* cuerpo = p_.cuerpo ? p_.cuerpo() : nullptr;
    const int n = cuerpo != nullptr ? vocabulario_->cargarCuerpo(*cuerpo) : 0;
    emit notify(tr("Nombres del cuerpo: %1").arg(n), "info");
    return n;
}

void AsistenteViewModel::rotularTraduccion(const QString& flujo, const QString& origen) {
    control::Dsk* dskp = p_.dsk ? p_.dsk() : nullptr;
    if (!rotulo_->activo() || dskp == nullptr || !origen.startsWith("Traducción")) return;
    // ¿Se está mostrando? En un cuadro «Subtítulos» o en los subtítulos ocultos.
    const bool enPantalla = subtitulos_ && subtitulos_->cuadrosDe(flujo) > 0;
    const QVariantMap cc = cc_ ? cc_->estado() : QVariantMap{};
    const bool enCc = cc.value("activo").toBool() && cc.value("flujo").toString() == flujo;
    if (!enPantalla && !enCc) return;
    // El rótulo se le pide al motor; la capa se arma cuando contesta que está.
    const std::string nombre = "Traducción automática (rótulo)";
    p_.ventanilla.pedir(
        this, "rotulo_traduccion", [&rt = p_.rt, nombre]() { return rt.engine().textoGlobal(nombre, "Traducción automática"); },
        [this, nombre](bool esta) {
            if (control::Dsk* d = p_.dsk ? p_.dsk() : nullptr; esta && d != nullptr) mostrarRotulo(*d, nombre);
        });
}

void AsistenteViewModel::mostrarRotulo(control::Dsk& dsk, const std::string& nombre) {
    const int capa = rotulo_->capa();
    // La capa se arma una vez con el rótulo arriba a la izquierda.
    const nlohmann::json capas = dsk.capas();
    bool armada = false;
    for (const auto& c : capas)
        if (c.value("capa", 0) == capa) {
            const auto cont = c.value("contenido", nlohmann::json::array());
            armada = cont.size() == 1 && cont[0] == nombre;
        }
    if (!armada)
        dsk.definir(capa, {{"nombre", "Traducción automática"},
                           {"contenido", nlohmann::json::array({nombre})},
                           {"recuadro", {{"posicion", "arriba_izquierda"}, {"tamano", 0.22}, {"margen", 0.03}}},
                           {"fijo", false},
                           {"transicion_ms", 300}});
    for (const auto& c : dsk.capas())
        if (c.value("capa", 0) == capa && !c.value("visible", false)) dsk.mostrar(capa, true, false);
    rotuloApagar_.start(ba::ajustes::sAMs("traductor.rotuloApagarS", 15)); // se va después de la última frase traducida
}

bool AsistenteViewModel::rotuloTraduccionActivo() const { return rotulo_->activo(); }

void AsistenteViewModel::setRotuloTraduccion(bool activo) {
    rotulo_->setActivo(activo);
    if (control::Dsk* dsk = p_.dsk ? p_.dsk() : nullptr; !activo && dsk != nullptr) dsk->mostrar(rotulo_->capa(), false, true);
}

nlohmann::json AsistenteViewModel::rotuloTraduccionEstado() const {
    return {{"activo", rotulo_->activo()}, {"capa", rotulo_->capa()}, {"arriba", rotuloApagar_.isActive()}};
}

} // namespace ba::app
