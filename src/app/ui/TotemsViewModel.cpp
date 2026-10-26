// ============================================================================
// src/app/ui/TotemsViewModel.cpp — ver TotemsViewModel.hpp
// ============================================================================
#include "ui/TotemsViewModel.hpp"

#include <QCryptographicHash>
#include <QBuffer>
#include <QDateTime>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <vector>

#include "agent/Voz.hpp"
#include "arranque/AppRuntime.hpp"
#include "diag/Registro.hpp"
#include "documentos/Documento.hpp"
#include "emision/OperatorFeed.hpp"
#include "emision/VozAlLobby.hpp"
#include "registro/Conversacion.hpp"
#include "sala/Direcciones.hpp"
#include "sala/Pedido.hpp"
#include "security/CredentialStore.hpp"
#include "ui/AtencionViewModel.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/FrameService.hpp"
#include "ui/LectorDocumentos.hpp"
#include "ui/PersonasViewModel.hpp"
#include "ui/StudioViewModel.hpp"
#if defined(BA_HAVE_VOZ)
#include "audio/Habla.hpp"
#endif

namespace ba::app {

using nlohmann::json;

namespace {

// Los tiempos de la conversación. Se dicen en voz alta frente a una pantalla:
// cortos para que no parezca colgado, largos para no apurar a nadie.
constexpr qint64 kSilencioMs = 12000;      ///< sin decir nada: «¿seguís ahí?»
constexpr qint64 kMargenVozMs = 350;       ///< después de hablar, antes de volver a escuchar
constexpr qint64 kEntreFotosMs = 600;      ///< a lo sumo una foto al reconocimiento por conexión
constexpr qint64 kEntreFotosFrenteMs = 250; ///< en el paso de la foto, más seguido: se busca el momento
constexpr qint64 kFotoMs = 7000;           ///< cuánto se espera una toma de frente antes de pedirla de nuevo
constexpr qint64 kArranqueSinCaraMs = 2500; ///< con canal y sin cara a la vista: arranca igual
constexpr qint64 kListoMs = 8000;          ///< la pantalla de gracias (la página vuelve sola)
constexpr qint64 kEntreDocumentosMs = 300; ///< mostrando el documento: un cuadro al lector cada tanto
constexpr qint64 kDocumentoMs = 15000;     ///< cuánto se espera leerlo antes de decirle qué corregir

qint64 ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

std::string hexAleatorio(int bytes) {
    static const char* h = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < bytes; ++i) {
        const quint32 b = QRandomGenerator::system()->generate() & 0xFF;
        s += h[b >> 4];
        s += h[b & 15];
    }
    return s;
}

std::string sha256(const std::string& s) {
    return QCryptographicHash::hash(QByteArray::fromStdString(s), QCryptographicHash::Sha256).toHex().toStdString();
}

json aJson(const QVariant& v) {
    const QJsonDocument d = QJsonDocument::fromVariant(QVariantList{v});
    const json j = json::parse(d.toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
    return j.is_array() && !j.empty() ? j[0] : json();
}

QVariant aQv(const json& j) {
    return QJsonDocument::fromJson(
               QByteArray::fromStdString(json{{"v", j}}.dump(-1, ' ', false, json::error_handler_t::replace)))
        .object()
        .value("v")
        .toVariant();
}

std::string texto(const json& j, const char* k) {
    return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string();
}

/// Lo que sale por MCP (puede ir a un modelo en la nube) sin los datos de un
/// documento: los objetos de un campo `documento` quedan en su resumen
/// (documentos::resumenParaAfuera) y los números largos de lo dicho, tapados.
json datosParaAfuera(const json& datos) {
    if (!datos.is_object()) return datos;
    json out = datos;
    for (auto it = out.begin(); it != out.end(); ++it)
        if (it.value().is_object() && it.value().contains("numero"))
            it.value() = documentos::resumenParaAfuera(it.value());
    return out;
}

EventoViewModel* eventoDe(StudioViewModel& vm) { return static_cast<EventoViewModel*>(vm.evento()); }
PersonasViewModel* personasDe(StudioViewModel& vm) { return static_cast<PersonasViewModel*>(vm.personas()); }
ConferenceClient* clienteDe(StudioViewModel& vm) {
    auto* c = static_cast<ConferenceViewModel*>(vm.conference());
    return c != nullptr ? &c->client() : nullptr;
}

} // namespace

/// Una conexión de la página (un tótem puede tener varias: la TV y cada
/// celular que escaneó su QR).
struct TotemsViewModel::Conexion {
    QString conn, totem, ip, lugar;
    qint64 desdeMs = 0;
    int canal = 0;
    bool audio = false;
    std::unique_ptr<registro::Conversacion> conv;
    // El audio.
    int toma = 0;
    bool oido = false;
    /// Mientras habla el asistente: al oído le llegan ceros. Lo lee el hilo de
    /// GStreamer, por eso atómico y compartido (sobrevive a la conexión).
    std::shared_ptr<std::atomic<bool>> callado = std::make_shared<std::atomic<bool>>(false);
    bool hablando = false;
    bool sintetizando = false;
    qint64 finHablaMs = 0;
    qint64 ultimaVozMs = 0;
    // La cara.
    bool identificando = false;
    qint64 ultimaFotoMs = 0;
    std::uint64_t ultimoCuadro = 0; ///< el número del último cuadro mirado (uno nuevo o nada)
    QString ultimaCara; ///< "" sin cara, "?" desconocida, o el id de la ficha
    QString ultimoNombre;
    int vecesCara = 0;
    QByteArray mejorFoto; ///< la última foto con una cara que sirve (para el alta)
    // El paso de la foto de frente (Conversacion::Estado::Foto).
    bool buscandoFoto = false;
    qint64 fotoDesdeMs = 0;  ///< desde cuándo se busca (0 = todavía lo está pidiendo)
    QString fotoSugerencia;  ///< qué corregir, según la última toma que no sirvió
    // El paso del documento (Conversacion::Estado::Documento).
    bool buscandoDocumento = false;
    qint64 docDesdeMs = 0;          ///< desde cuándo se busca (0 = todavía lo está pidiendo)
    qint64 ultimoDocMs = 0;
    std::uint64_t ultimoCuadroDoc = 0;
    bool leyendoDocumento = false;  ///< hay un cuadro en el lector
    QString docSugerencia;          ///< qué corregir, según lo último que se vio
    /// Después de terminar, la próxima arranca recién cuando esa cara se va (o
    /// aparece otra conocida): si no, la misma persona empezaría de nuevo.
    bool esperaVacio = false;
    QString ultimaPersona;
    qint64 canalDesdeMs = 0;
    bool primeraDelCanal = false;
    qint64 terminoMs = 0;
    // Para la vista.
    std::string pedidoEnviado;
    /// El `listo` se manda cuando termina de decir la despedida: el celular
    /// suelta el canal al recibirlo, y con él se cortaría la voz a mitad.
    std::string listoPendiente;
    bool hayListo = false;
    QString oidoTexto, diceTexto, estado = "esperando";
};

TotemsViewModel::TotemsViewModel(AppRuntime& rt, StudioViewModel& vm, agent::Voz* voz, agent::ModelosLocales* modelos,
                                 QObject* parent)
    : QObject(parent), rt_(rt), vm_(vm), voz_(voz), modelos_(modelos) {
    if (EventoViewModel* ev = eventoDe(vm_)) {
        connect(ev, &EventoViewModel::guionChanged, this, &TotemsViewModel::recargar);
        connect(ev, &EventoViewModel::padronChanged, this, &TotemsViewModel::registradosCambio);
    }
    if (ConferenceClient* c = clienteDe(vm_)) {
        connect(c, &ConferenceClient::registroEvento, this, [this](const QJsonObject& o) {
            const json ev = json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
            if (ev.is_object()) alEvento(ev);
        });
        // El gateway volvió (o se reinició): no sabe de los tótems ni de nada vivo.
        connect(c, &ConferenceClient::onlineChanged, this, [this](bool on) {
            if (on) {
                publicar();
                reconocerConexiones();
            } else {
                std::vector<QString> todas;
                for (const auto& [k, c] : conns_) todas.push_back(k);
                for (const QString& k : todas) soltar(k);
            }
            emit cambio();
        });
    }
    // Las urls cambian con la dirección de la red o el túnel.
    connect(&vm_.direcciones(), &sala::Direcciones::cambio, this, &TotemsViewModel::cambio);
    if (AtencionViewModel* a = vm_.atencionVm())
        connect(a, &AtencionViewModel::cambio, this, [this]() {
            // Otra voz del atendedor: se recarga la próxima vez que haga falta.
            if (vozLista_ && vm_.atencionVm() != nullptr && vm_.atencionVm()->vozElegida() != vozCargada_) vozLista_ = false;
            emit cambio();
        });
    tic_.setInterval(200);
    connect(&tic_, &QTimer::timeout, this, &TotemsViewModel::tic);
    lector_ = std::make_unique<LectorDocumentos>(vm_.plugins());
    connect(lector_.get(), &LectorDocumentos::tiposCambio, this, &TotemsViewModel::tiposDocumentoCambio);
    recargar();
}

TotemsViewModel::~TotemsViewModel() {
    tic_.stop();
    for (auto& [k, c] : conns_) cerrarAudio(*c);
#if defined(BA_HAVE_VOZ)
    if (habla_) habla_->callar();
    habla_.reset(); // antes que los canales: su hilo les escribe
#endif
    for (auto& v : voces_) v.reset();
}

// --- La configuración ----------------------------------------------------------------

void TotemsViewModel::recargar() {
    EventoViewModel* ev = eventoDe(vm_);
    const std::filesystem::path carpeta = ev != nullptr ? ev->carpetaAbierta() : std::filesystem::path();
    const std::string nombre = ev != nullptr ? ev->guionAbierto().nombre : std::string();
    if (carpeta == carpeta_) {
        // El mismo evento: quizá cambió de nombre (va en el saludo y el título).
        if (nombre != eventoNombre_) {
            eventoNombre_ = nombre;
            for (auto& [k, c] : conns_) mandarPantalla(*c);
            emit cambio();
        }
        return;
    }
    // Otro evento: lo que se conversaba era del anterior.
    std::vector<QString> todas;
    for (const auto& [k, c] : conns_) todas.push_back(k);
    for (const QString& k : todas) {
        enviar(k, {{"type", "reiniciar"}});
        soltar(k);
    }
    carpeta_ = carpeta;
    eventoNombre_ = nombre;
    secretos_.clear();
    bool ok = true;
    std::string err;
    totems_ = registro::cargarTotems(carpeta_, &ok, &err);
    if (!ok) emit notify("No pude leer los tótems de registro: " + QString::fromStdString(err), "error");
    publicar();
    // La voz se carga ya, no con la primera persona: si no, el primer saludo sale mudo.
    if (!totems_.lista().empty()) cargarVoz();
    emit cambio();
    emit registradosCambio();
}

std::string TotemsViewModel::guardarYPublicar() {
    std::string err;
    if (!registro::guardarTotems(carpeta_, totems_, &err)) return "No se pudo guardar: " + err;
    publicar();
    // Lo que muestran las pantallas de cada tótem (título, campos) cambia en el acto.
    for (auto& [k, c] : conns_) mandarPantalla(*c);
    emit cambio();
    return {};
}

std::string TotemsViewModel::nombreSecreto(const std::string& id) const {
    // Por evento (cada carpeta tiene sus tótems y sus ids) y con un nombre válido
    // para el almacén ([a-z0-9._-]), como SesionRemota::nombreSecreto.
    return "registro-" + sha256(carpeta_.string() + "|" + id).substr(0, 32);
}

TotemsViewModel::Secreto TotemsViewModel::secreto(const std::string& id, bool renovar) const {
    if (!renovar)
        if (auto it = secretos_.find(id); it != secretos_.end()) return it->second;
    const auto almacen = ba::security::CredentialStore::bundle();
    Secreto s;
    std::string guardado;
    if (!renovar && almacen.read(nombreSecreto(id), guardado)) {
        const json g = json::parse(guardado, nullptr, false);
        if (g.is_object() && texto(g, "token").size() == 64 && !texto(g, "sal").empty()) {
            s.token = texto(g, "token");
            s.sal = texto(g, "sal");
        }
    }
    if (s.token.empty()) {
        // 256 bits: la url ES la credencial (no hay PIN: se para cualquiera frente a la TV).
        s.token = hexAleatorio(32);
        s.sal = hexAleatorio(16);
        almacen.write(nombreSecreto(id), json{{"token", s.token}, {"sal", s.sal}}.dump());
    }
    secretos_[id] = s;
    return s;
}

std::string TotemsViewModel::origen() const {
    // El mismo origen que la sesión remota (HTTPS: getUserMedia lo exige); el
    // gateway lo publica también como servicio «registro».
    const sala::Direcciones& d = vm_.direcciones();
    std::string o = d.origen("registro");
    if (o.empty()) o = d.origen("sesion");
    return o;
}

QString TotemsViewModel::url(const QString& id) const {
    const registro::Totem* t = totems_.porId(id.toStdString());
    if (t == nullptr) return {};
    const Secreto s = secreto(t->id);
    // El token va en el FRAGMENTO: el navegador no lo manda al servidor en la
    // url, ni queda en los registros de nadie en el camino.
    return QString::fromStdString(origen() + "/registro/#r=" + t->id + "&t=" + s.token);
}

QString TotemsViewModel::urlPantalla(const QString& id) const {
    const registro::Totem* t = totems_.porId(id.toStdString());
    if (t == nullptr) return {};
    const Secreto s = secreto(t->id);
    return QString::fromStdString(origen() + "/registro/?pantalla=1#r=" + t->id + "&t=" + s.token);
}

QString TotemsViewModel::renovar(const QString& id) {
    if (totems_.porId(id.toStdString()) == nullptr) return {};
    secreto(id.toStdString(), true);
    publicar(); // el gateway olvida el viejo y corta a quien entró con él
    emit cambio();
    return url(id);
}

void TotemsViewModel::publicar() {
    ConferenceClient* c = clienteDe(vm_);
    if (c == nullptr || !c->online()) return;
    json lista = json::array();
    for (const registro::Totem& t : totems_.lista()) {
        if (!t.activo) continue;
        const Secreto s = secreto(t.id);
        // El mismo esquema que la sesión remota: sha256 hex de (sal + token).
        lista.push_back({{"id", t.id}, {"nombre", t.nombre}, {"sal", s.sal}, {"hash_token", sha256(s.sal + s.token)}});
    }
    sala::pedir(*c, "POST", "/api/v1/registro/totems", {{"totems", lista}}, [](const std::string& err, const json&) {
        if (!err.empty())
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "registro", 0, "no se pudieron publicar los tótems: " + err);
    });
}

void TotemsViewModel::reconocerConexiones() {
    ConferenceClient* c = clienteDe(vm_);
    if (c == nullptr) return;
    QPointer<TotemsViewModel> yo(this);
    sala::pedir(*c, "GET", "/api/v1/registro/estado", {}, [yo](const std::string& err, const json& r) {
        if (!yo || !err.empty()) return;
        for (const json& x : r.value("conexiones", json::array())) {
            const QString conn = QString::fromStdString(texto(x, "conn"));
            if (conn.isEmpty()) continue;
            Conexion& k = yo->conexion(conn, QString::fromStdString(texto(x, "id")));
            k.ip = QString::fromStdString(texto(x, "ip"));
            k.lugar = QString::fromStdString(texto(x, "lugar"));
            if (x.contains("desdeMs") && x["desdeMs"].is_number()) k.desdeMs = x["desdeMs"].get<qint64>();
            k.audio = x.value("audio", false);
            const int canal = x.value("canal", 0);
            if (canal > 0 && k.canal != canal) {
                k.canal = canal;
                k.canalDesdeMs = ahoraMs();
                k.primeraDelCanal = true;
                yo->abrirAudio(k);
            }
        }
        emit yo->vivoCambio();
    });
}

QVariantList TotemsViewModel::tiposDocumento() const {
    return lector_ ? aQv(lector_->tipos()).toList() : QVariantList{};
}

bool TotemsViewModel::lectorDisponible() const { return LectorDocumentos::disponible(); }

QVariantList TotemsViewModel::tipos() const {
    QVariantList out;
    for (registro::TipoCampo t : registro::todosLosTipos())
        out << QVariantMap{{"id", QString::fromLatin1(registro::nombreDeTipo(t))},
                           {"etiqueta", QString::fromUtf8(registro::etiquetaDeTipo(t))}};
    return out;
}

json TotemsViewModel::totemJson(const QString& id) const {
    const registro::Totem* t = totems_.porId(id.toStdString());
    if (t == nullptr) return nullptr;
    json j = t->aJson();
    j["url"] = url(id).toStdString();
    j["urlPantalla"] = urlPantalla(id).toStdString();
    int n = 0;
    for (const auto& [k, c] : conns_)
        if (c->totem == id) ++n;
    j["conectados"] = n;
    return j;
}

json TotemsViewModel::listarJson() const {
    json ts = json::array();
    for (const registro::Totem& t : totems_.lista()) ts.push_back(totemJson(QString::fromStdString(t.id)));
    return {{"evento", eventoNombre_}, {"hayEvento", hayEvento()}, {"totems", ts}};
}

QVariantList TotemsViewModel::totems() const {
    QVariantList out;
    for (const registro::Totem& t : totems_.lista()) out << aQv(totemJson(QString::fromStdString(t.id)));
    return out;
}

QVariantList TotemsViewModel::paraElegir() const {
    QVariantList out;
    for (const registro::Totem& t : totems_.lista()) {
        if (!t.activo) continue;
        const QString id = QString::fromStdString(t.id);
        out << QVariantMap{{"id", id}, {"nombre", QString::fromStdString(t.nombre)}, {"url", url(id)},
                           {"urlPantalla", urlPantalla(id)}};
    }
    return out;
}

std::string TotemsViewModel::crearJson(const json& j, std::string& id) {
    if (!hayEvento()) return "No hay un evento abierto: los tótems son de un evento (abrí o creá uno).";
    std::vector<std::string> usados;
    for (const registro::Totem& t : totems_.lista()) usados.push_back(t.id);
    const std::string nombre = texto(j, "nombre").empty() ? std::string("Registro") : texto(j, "nombre");
    std::string nuevoId = texto(j, "id");
    if (nuevoId.empty()) nuevoId = registro::idDesdeNombre(nombre, usados);
    if (std::string e = totems_.agregar(registro::totemNuevo(nuevoId, nombre)); !e.empty()) return e;
    json resto = j.is_object() ? j : json::object();
    resto.erase("id");
    if (!resto.empty())
        if (std::string e = totems_.cambiar(nuevoId, resto); !e.empty()) {
            totems_.quitar(nuevoId);
            return e;
        }
    if (std::string e = guardarYPublicar(); !e.empty()) return e;
    id = nuevoId;
    return {};
}

std::string TotemsViewModel::cambiarJson(const QString& id, const json& cambios) {
    if (!hayEvento()) return "No hay un evento abierto.";
    if (std::string e = totems_.cambiar(id.toStdString(), cambios); !e.empty()) return e;
    return guardarYPublicar();
}

QString TotemsViewModel::crear(const QString& nombre) {
    std::string id;
    const std::string e = crearJson({{"nombre", nombre.trimmed().toStdString()}}, id);
    if (!e.empty()) {
        emit notify(QString::fromStdString(e), "error");
        return {};
    }
    return QString::fromStdString(id);
}

QString TotemsViewModel::cambiar(const QString& id, const QVariantMap& cambios) {
    const json j = aJson(QVariant(cambios));
    return QString::fromStdString(cambiarJson(id, j.is_object() ? j : json::object()));
}

bool TotemsViewModel::borrar(const QString& id) {
    if (!totems_.quitar(id.toStdString())) return false;
    ba::security::CredentialStore::bundle().remove(nombreSecreto(id.toStdString()));
    secretos_.erase(id.toStdString());
    guardarYPublicar(); // el gateway corta a los que estaban conectados a ese
    return true;
}

namespace {
/// Los campos del tótem como JSON, para tocar uno y mandarlos enteros.
json camposDe(const registro::Totem& t) { return t.aJson()["campos"]; }
} // namespace

QString TotemsViewModel::agregarCampo(const QString& id, const QString& tipo) {
    const registro::Totem* t = totems_.porId(id.toStdString());
    if (t == nullptr) return "No hay ese tótem.";
    registro::Campo c;
    registro::tipoDeNombre(tipo.toStdString(), c.tipo);
    c.etiqueta = registro::etiquetaDeTipo(c.tipo);
    // Una clave libre a partir del tipo («correo», «correo_2»…).
    std::vector<std::string> usadas;
    for (const registro::Campo& o : t->campos) usadas.push_back(o.clave);
    std::string base = registro::nombreDeTipo(c.tipo);
    c.clave = base;
    for (int n = 2; std::find(usadas.begin(), usadas.end(), c.clave) != usadas.end(); ++n) c.clave = base + "_" + std::to_string(n);
    if (c.tipo == registro::TipoCampo::Opcion) c.opciones = {"Opción 1", "Opción 2"};
    // De fábrica, el DNI; los demás se marcan en el editor (o por MCP).
    if (c.tipo == registro::TipoCampo::Documento) {
        c.documentos = {"dni-ar"};
        c.etiqueta = "DNI";
    }
    json cs = camposDe(*t);
    cs.push_back(c.aJson());
    return QString::fromStdString(cambiarJson(id, {{"campos", cs}}));
}

QString TotemsViewModel::quitarCampo(const QString& id, int indice) {
    const registro::Totem* t = totems_.porId(id.toStdString());
    if (t == nullptr || indice < 0 || indice >= static_cast<int>(t->campos.size())) return "No hay ese campo.";
    json cs = camposDe(*t);
    cs.erase(static_cast<std::size_t>(indice));
    return QString::fromStdString(cambiarJson(id, {{"campos", cs}}));
}

QString TotemsViewModel::moverCampo(const QString& id, int desde, int hasta) {
    const registro::Totem* t = totems_.porId(id.toStdString());
    const int n = t != nullptr ? static_cast<int>(t->campos.size()) : 0;
    if (desde < 0 || desde >= n || hasta < 0 || hasta >= n) return "No hay ese campo.";
    if (desde == hasta) return {};
    json cs = camposDe(*t);
    json c = cs[static_cast<std::size_t>(desde)];
    cs.erase(static_cast<std::size_t>(desde));
    cs.insert(cs.begin() + hasta, c);
    return QString::fromStdString(cambiarJson(id, {{"campos", cs}}));
}

QString TotemsViewModel::cambiarCampo(const QString& id, int indice, const QVariantMap& cambios) {
    const registro::Totem* t = totems_.porId(id.toStdString());
    if (t == nullptr || indice < 0 || indice >= static_cast<int>(t->campos.size())) return "No hay ese campo.";
    json cs = camposDe(*t);
    const json c = aJson(QVariant(cambios));
    for (auto it = c.begin(); c.is_object() && it != c.end(); ++it) cs[static_cast<std::size_t>(indice)][it.key()] = it.value();
    return QString::fromStdString(cambiarJson(id, {{"campos", cs}}));
}

// --- Lo vivo ---------------------------------------------------------------------------

bool TotemsViewModel::enLinea() const {
    ConferenceClient* c = clienteDe(vm_);
    return c != nullptr && c->online();
}

QString TotemsViewModel::hablarFalta() const {
    if (voz_ == nullptr) return "Esta compilación no trae el motor de voz.";
    // Lo mismo que hace falta para atender hablando: el reconocedor y la voz del atendedor.
    if (AtencionViewModel* a = vm_.atencionVm()) return a->hablarFalta();
    return {};
}

void TotemsViewModel::enviar(const QString& a, const json& msg, bool porTotem) {
    ConferenceClient* c = clienteDe(vm_);
    if (c == nullptr || !c->online()) return;
    json cuerpo{{"msg", msg}};
    cuerpo[porTotem ? "id" : "conn"] = a.toStdString();
    sala::pedir(*c, "POST", "/api/v1/registro/enviar", cuerpo, [](const std::string&, const json&) {});
}

void TotemsViewModel::mandarPantalla(Conexion& c) {
    const registro::Totem* t = totems_.porId(c.totem.toStdString());
    if (t == nullptr) return;
    // Una conversación de mentira solo para armar los textos y la lista de
    // campos vacía con la MISMA regla que la de verdad.
    const registro::Conversacion vacia(*t, eventoNombre_);
    enviar(c.conn, {{"type", "pantalla"}, {"titulo", vacia.titulo()}, {"subtitulo", vacia.subtitulo()}});
    if (c.conv == nullptr || !c.conv->enCurso()) {
        enviar(c.conn, {{"type", "campos"}, {"lista", vacia.camposParaMostrar()}});
        enviar(c.conn, {{"type", "estado"}, {"estado", "esperando"}});
    }
}

TotemsViewModel::Conexion& TotemsViewModel::conexion(const QString& conn, const QString& totem) {
    auto& p = conns_[conn];
    if (!p) {
        p = std::make_unique<Conexion>();
        p->conn = conn;
        p->desdeMs = ahoraMs();
    }
    if (!totem.isEmpty()) p->totem = totem;
    if (!tic_.isActive()) tic_.start();
    return *p;
}

void TotemsViewModel::soltar(const QString& conn) {
    auto it = conns_.find(conn);
    if (it == conns_.end()) return;
    cerrarAudio(*it->second);
    conns_.erase(it);
    if (conns_.empty()) tic_.stop();
    emit vivoCambio();
}

void TotemsViewModel::alEvento(const json& ev) {
    const std::string tipo = texto(ev, "tipo");
    const QString conn = QString::fromStdString(texto(ev, "conn"));
    const QString totem = QString::fromStdString(texto(ev, "id"));
    if (tipo == "intento") {
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "registro", 0,
                                   "tótem «" + texto(ev, "id") + "»: intento fallido desde " + texto(ev, "ip"));
        return;
    }
    if (conn.isEmpty()) return;
    if (tipo == "conexion") {
        if (!ev.value("conectado", false)) {
            soltar(conn);
            return;
        }
        Conexion& c = conexion(conn, totem);
        c.ip = QString::fromStdString(texto(ev, "ip"));
        c.lugar = QString::fromStdString(texto(ev, "lugar"));
        mandarPantalla(c);
        emit vivoCambio();
        emit cambio(); // `conectados` de cada tótem
        return;
    }
    auto it = conns_.find(conn);
    Conexion& c = it != conns_.end() ? *it->second : conexion(conn, totem);
    if (tipo == "canal") {
        const int k = ev.value("canal", 0);
        if (k == c.canal) return;
        cerrarAudio(c);
        // Sin canal no hay voz: lo que se conversaba se pierde (no se guarda a medias).
        if (k == 0 && c.conv != nullptr && c.conv->enCurso()) c.conv.reset();
        c.canal = k;
        if (k > 0) {
            c.canalDesdeMs = ahoraMs();
            c.primeraDelCanal = true;
            c.esperaVacio = false;
            abrirAudio(c);
        }
    } else if (tipo == "audio") {
        c.audio = ev.value("listo", false);
    } else if (tipo == "toque") {
        alToque(c, ev);
    }
    emit vivoCambio();
}

// --- El audio -----------------------------------------------------------------------------

void TotemsViewModel::cargarVoz() {
#if defined(BA_HAVE_VOZ)
    AtencionViewModel* a = vm_.atencionVm();
    if (a == nullptr || modelos_ == nullptr) return;
    const QString repo = a->vozElegida();
    if (repo == vozCargada_ && (vozLista_ || cargandoVoz_)) return;
    audio::Habla::Voz v;
    if (!AtencionViewModel::rutasDeVoz(*modelos_, repo, v.modelo, v.tokens, v.datos)) return;
    if (!habla_) habla_ = std::make_unique<audio::Habla>();
    vozCargada_ = repo;
    cargandoVoz_ = true;
    QPointer<TotemsViewModel> yo(this);
    habla_->cargar(v, [yo](const std::string& error) {
        QMetaObject::invokeMethod(yo.data(), [yo, error]() {
            if (!yo) return;
            yo->cargandoVoz_ = false;
            yo->vozLista_ = yo->habla_ && yo->habla_->cargada();
            if (!error.empty()) emit yo->notify("La voz de los tótems no cargó: " + QString::fromStdString(error), "warn");
        });
    });
#endif
}

void TotemsViewModel::abrirAudio(Conexion& c) {
    const int k = c.canal;
    if (k < 1 || k >= static_cast<int>(voces_.size())) return;
    cargarVoz();
    if (!voces_[static_cast<std::size_t>(k)])
        voces_[static_cast<std::size_t>(k)] = std::make_unique<VozAlLobby>(L"BA.Audio.RegistroVoz." + std::to_wstring(k));
    voces_[static_cast<std::size_t>(k)]->abrir();
    abrirOido(c);
}

void TotemsViewModel::abrirOido(Conexion& c) {
    // El reconocedor carga en segundo plano al arrancar la app: un tótem que se conecta
    // antes (la app se reinició y la página volvió sola) se queda sin oído hasta que
    // esté. tic() vuelve a probar.
    const int k = c.canal;
    if (c.oido || voz_ == nullptr || !voz_->lista() || k < 1) return;
    const QString conn = c.conn;
    const QString clave = "registro:" + conn;
    QPointer<TotemsViewModel> yo(this);
    c.oido = voz_->abrirOido(clave, [yo, conn](const QString& t) {
        if (yo) yo->alOido(conn, t);
    });
    if (!c.oido) return;
    agent::Voz* voz = voz_;
    const std::string cl = clave.toStdString();
    const auto callado = c.callado;
    c.toma = rt_.operatorFeed().escucharRegistro(k, [voz, cl, callado](const float* d, std::size_t n) {
        thread_local std::vector<float> mono;
        mono.resize(n);
        // Mientras habla el asistente, ceros: así el reconocedor cierra la frase
        // que tuviera abierta y no se transcribe la propia voz que vuelve.
        const bool cero = callado->load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < n; ++i) mono[i] = cero ? 0.0f : d[2 * i]; // un micrófono es mono
        voz->alimentarOido(cl, mono.data(), n, 48000);
    });
}

void TotemsViewModel::cerrarAudio(Conexion& c) {
    if (c.toma != 0) rt_.operatorFeed().dejarDeEscucharLobby(c.toma);
    c.toma = 0;
    if (c.oido && voz_ != nullptr) voz_->cerrarOido("registro:" + c.conn);
    c.oido = false;
    if (c.canal > 0 && c.canal < static_cast<int>(voces_.size()) && voces_[static_cast<std::size_t>(c.canal)])
        voces_[static_cast<std::size_t>(c.canal)]->cerrar();
    c.hablando = false;
    c.callado->store(false);
}

// --- La cara ------------------------------------------------------------------------------

void TotemsViewModel::mirarCara(Conexion& c) {
    if (c.identificando || c.canal == 0) return;
    if (ahoraMs() - c.ultimaFotoMs < kEntreFotosMs) return;
    // Conversando, solo hace falta si todavía no hay una foto que sirva (para
    // el alta) o no se sabe quién es; en el paso de la foto, siempre (y más seguido).
    if (c.buscandoFoto) {
        if (c.fotoDesdeMs == 0 || ahoraMs() - c.ultimaFotoMs < kEntreFotosFrenteMs) return;
    } else if (c.conv != nullptr && c.conv->enCurso() && !c.mejorFoto.isEmpty()) {
        return;
    }
    PersonasViewModel* pv = personasDe(vm_);
    auto* frames = static_cast<FrameService*>(vm_.frames());
    if (pv == nullptr || !pv->disponible() || frames == nullptr) return;
    // La cámara del tótem llega por WebRTC al canal BA.Registro.<k>, como la del
    // lobby: el mismo cuadro que ve el operador (o un monitor) es el que se reconoce.
    QImage img;
    std::uint64_t serie = 0;
    if (!frames->latest(QString("registro:%1").arg(c.canal), img, serie) || img.isNull() || serie == c.ultimoCuadro)
        return;
    c.ultimoCuadro = serie;
    c.ultimaFotoMs = ahoraMs();
    // En JPEG: es lo que guarda la libreta como foto de la ficha si hay alta.
    QByteArray jpeg;
    QBuffer buf(&jpeg);
    buf.open(QIODevice::WriteOnly);
    if (!img.convertToFormat(QImage::Format_RGB888).save(&buf, "JPG", 88) || jpeg.isEmpty()) return;
    c.identificando = true;
    const QString conn = c.conn;
    QPointer<TotemsViewModel> yo(this);
    pv->identificarFoto(jpeg, [yo, conn, jpeg](const PersonasViewModel::Identificado& q, const QString& error) {
        if (!yo) return;
        const auto it = yo->conns_.find(conn);
        if (it == yo->conns_.end()) return;
        it->second->identificando = false;
        if (!error.isEmpty()) return;
        yo->alCara(conn, jpeg, q.personaId, q.nombre, q.hayCara, q.sirve, q.sugerencia);
    });
}

void TotemsViewModel::alCara(const QString& conn, const QByteArray& jpeg, const QString& personaId, const QString& nombre,
                             bool hayCara, bool sirve, const QString& sugerencia) {
    auto it = conns_.find(conn);
    if (it == conns_.end()) return;
    Conexion& c = *it->second;
    // El paso de la foto: la primera toma de frente que sirve es la de su ficha.
    if (c.buscandoFoto && c.fotoDesdeMs > 0 && c.conv != nullptr) {
        if (hayCara && sirve) {
            c.mejorFoto = jpeg;
            if (!personaId.isEmpty()) c.conv->conocerCara({personaId.toStdString(), nombre.toStdString(), false});
            const auto r = c.conv->fotoLista();
            aplicar(c, r.decir, r.campos, r.pedir, r.guardar, r.terminar, r.foto);
        } else {
            c.fotoSugerencia = hayCara ? sugerencia : QString("No te veo la cara.");
        }
        return;
    }
    if (c.conv != nullptr && c.conv->enCurso()) {
        if (hayCara && sirve && c.mejorFoto.isEmpty()) c.mejorFoto = jpeg;
        if (!personaId.isEmpty())
            c.conv->conocerCara({personaId.toStdString(), nombre.toStdString(), false});
        return;
    }
    const QString clave = !hayCara ? QString() : personaId.isEmpty() ? QString("?") : personaId;
    if (c.esperaVacio) {
        // Recién terminó: la próxima arranca cuando esa cara se va o aparece otra persona conocida.
        const bool otra = !clave.isEmpty() && clave != "?" && clave != c.ultimaPersona;
        if (clave.isEmpty() || otra) {
            c.esperaVacio = false;
            c.mejorFoto.clear();
            c.vecesCara = 0;
        }
        if (clave.isEmpty() || !otra) return;
    }
    if (clave.isEmpty()) {
        c.vecesCara = 0;
        c.ultimaCara.clear();
        return;
    }
    if (clave == c.ultimaCara) {
        ++c.vecesCara;
    } else {
        c.ultimaCara = clave;
        c.ultimoNombre = nombre;
        c.vecesCara = 1;
        c.mejorFoto.clear();
    }
    if (sirve) c.mejorFoto = jpeg;
    // Dos fotos seguidas de la misma cara: hay alguien parado ahí, no pasando.
    if (c.vecesCara >= 2) empezar(c, personaId, nombre);
}

// --- La conversación --------------------------------------------------------------------------

void TotemsViewModel::empezar(Conexion& c, const QString& personaId, const QString& nombre) {
    const registro::Totem* t = totems_.porId(c.totem.toStdString());
    if (t == nullptr || !t->activo) return;
    if (c.conv != nullptr && c.conv->enCurso()) return;
    EventoViewModel* ev = eventoDe(vm_);
    registro::Conversacion::Cara cara;
    cara.personaId = personaId.toStdString();
    cara.nombre = nombre.toStdString();
    cara.yaRegistrado = !personaId.isEmpty() && ev != nullptr && ev->registradoPorTotem(personaId);
    c.conv = std::make_unique<registro::Conversacion>(*t, eventoNombre_);
    c.primeraDelCanal = false;
    c.esperaVacio = false;
    c.pedidoEnviado.clear();
    c.oidoTexto.clear();
    c.ultimaVozMs = ahoraMs();
    const auto r = c.conv->empezar(cara);
    ba::diag::registro().linea(ba::diag::Nivel::Info, "registro", 0,
                               "tótem «" + t->id + "» (" + c.conn.toStdString() + "): empieza" +
                                   (cara.personaId.empty() ? std::string() : " con " + cara.personaId));
    aplicar(c, r.decir, r.campos, r.pedir, r.guardar, r.terminar, r.foto);
}

void TotemsViewModel::alOido(const QString& conn, const QString& t) {
    auto it = conns_.find(conn);
    if (it == conns_.end()) return;
    Conexion& c = *it->second;
    // Lo que el oído cerró justo cuando el asistente empezaba a hablar (o el eco
    // de su propia voz) no cuenta.
    if (c.hablando || t.trimmed().isEmpty()) return;
    c.oidoTexto = t.trimmed();
    c.ultimaVozMs = ahoraMs();
    enviar(conn, {{"type", "oido"}, {"texto", c.oidoTexto.toStdString()}});
    if (c.conv != nullptr && c.conv->enCurso()) {
        if (!c.buscandoFoto) enviar(conn, {{"type", "estado"}, {"estado", "pensando"}});
        const auto r = c.conv->oido(c.oidoTexto.toStdString());
        aplicar(c, r.decir, r.campos, r.pedir, r.guardar, r.terminar, r.foto);
    }
    emit vivoCambio();
}

void TotemsViewModel::alToque(Conexion& c, const json& ev) {
    const std::string accion = texto(ev, "accion");
    if (accion == "empezar") {
        c.esperaVacio = false;
        const bool conocida = !c.ultimaCara.isEmpty() && c.ultimaCara != "?";
        empezar(c, conocida ? c.ultimaCara : QString(), conocida ? c.ultimoNombre : QString());
    } else if (c.conv != nullptr && accion == "cancelar") {
        const auto r = c.conv->cancelar();
        aplicar(c, r.decir, r.campos, r.pedir, r.guardar, r.terminar, r.foto);
    } else if (c.conv != nullptr && accion == "escrito") {
        c.ultimaVozMs = ahoraMs();
        const auto r = c.conv->escrito(texto(ev, "clave"), texto(ev, "valor"));
        aplicar(c, r.decir, r.campos, r.pedir, r.guardar, r.terminar, r.foto);
    }
}

void TotemsViewModel::aplicar(Conexion& c, const std::string& decirTxt, bool campos, const std::string& pedir,
                              bool guardar, bool terminar, bool foto) {
    if (c.conv == nullptr) return;
    // El documento se busca mientras la conversación esté en ese paso (lo dice ella,
    // no una bandera aparte). Cada vez que lo pide de nuevo, el tiempo cuenta de
    // nuevo desde que termina de decirlo (tic); la charla oída mientras tanto, no.
    if (c.conv->estado() == registro::Conversacion::Estado::Documento) {
        if (!c.buscandoDocumento || !decirTxt.empty()) {
            c.buscandoDocumento = true;
            c.docDesdeMs = 0;
            c.docSugerencia.clear();
        }
    } else {
        c.buscandoDocumento = false;
    }
    // La foto de frente: se busca recién cuando terminó de pedirla (tic), no mientras habla.
    // Una respuesta vacía (algo que se oyó mientras mira a la cámara) no la interrumpe.
    if (foto) {
        c.buscandoFoto = true;
        c.fotoDesdeMs = 0;
        c.fotoSugerencia.clear();
    } else if (terminar || !decirTxt.empty()) {
        c.buscandoFoto = false;
    }
    if (campos) enviar(c.conn, {{"type", "campos"}, {"lista", c.conv->camposParaMostrar()}});
    if (!pedir.empty() && pedir != c.pedidoEnviado) {
        json p = c.conv->pedido();
        if (p.is_object()) {
            p["type"] = "pedir";
            enviar(c.conn, p);
        }
        c.pedidoEnviado = pedir;
    }
    if (!decirTxt.empty()) decir(c, decirTxt);
    if (guardar) guardarRegistro(c);
    if (terminar) {
        if (c.hablando) {
            c.listoPendiente = decirTxt;
            c.hayListo = true;
        } else {
            enviar(c.conn, {{"type", "listo"}, {"texto", decirTxt}});
        }
        c.estado = "listo";
        c.esperaVacio = true;
        c.ultimaPersona = QString::fromStdString(c.conv->cara().personaId);
        if (c.ultimaPersona.isEmpty()) c.ultimaPersona = c.ultimaCara;
        c.terminoMs = ahoraMs();
        c.pedidoEnviado.clear();
    } else if (decirTxt.empty() && !c.buscandoFoto && !c.buscandoDocumento) {
        c.estado = "escuchando";
        enviar(c.conn, {{"type", "estado"}, {"estado", "escuchando"}});
    }
    emit vivoCambio();
}

void TotemsViewModel::decir(Conexion& c, const std::string& t) {
    c.diceTexto = QString::fromStdString(t);
    enviar(c.conn, {{"type", "dice"}, {"texto", t}});
#if defined(BA_HAVE_VOZ)
    const int k = c.canal;
    AtencionViewModel* a = vm_.atencionVm();
    if (vozLista_ && habla_ && a != nullptr && k > 0 && k < static_cast<int>(voces_.size()) &&
        voces_[static_cast<std::size_t>(k)]) {
        VozAlLobby* destino = voces_[static_cast<std::size_t>(k)].get();
        c.hablando = true;
        c.sintetizando = true;
        c.finHablaMs = 0;
        c.callado->store(true);
        if (c.estado != "listo") {
            c.estado = "hablando";
            enviar(c.conn, {{"type", "estado"}, {"estado", "hablando"}});
        }
        const QString conn = c.conn;
        QPointer<TotemsViewModel> yo(this);
        habla_->decir(
            t, a->hablante(), static_cast<float>(a->velocidad()),
            [destino](const float* m, std::size_t n, std::uint32_t f) { destino->encolar(m, n, f); },
            [yo, conn]() {
                QMetaObject::invokeMethod(yo.data(), [yo, conn]() {
                    if (!yo) return;
                    if (auto it = yo->conns_.find(conn); it != yo->conns_.end()) it->second->sintetizando = false;
                });
            });
        return;
    }
#endif
    // Sin voz: la página lo muestra escrito y se sigue escuchando.
    if (c.estado != "listo") {
        c.estado = "escuchando";
        enviar(c.conn, {{"type", "estado"}, {"estado", "escuchando"}});
    }
}

void TotemsViewModel::guardarRegistro(Conexion& c) {
    EventoViewModel* ev = eventoDe(vm_);
    if (ev == nullptr || c.conv == nullptr) return;
    const json datos = c.conv->datos();
    const QString nombre = QString::fromStdString(c.conv->nombre());
    const QString totem = c.totem;
    const QString personaId = QString::fromStdString(c.conv->cara().personaId);
    PersonasViewModel* pv = personasDe(vm_);
    const auto anotar = [this, ev, nombre, totem, datos](const QString& persona) {
        const QString id = ev->registrarDesdeTotem(persona, nombre, totem, datos);
        if (id.isEmpty()) {
            emit notify("Alguien se registró en un tótem pero no hay un evento abierto: no quedó anotado.", "warn");
            return;
        }
        ba::diag::registro().linea(ba::diag::Nivel::Info, "registro", 0,
                                   "registrado por el tótem «" + totem.toStdString() + "»: «" + nombre.toStdString() +
                                       "» (padrón " + id.toStdString() + ")");
        emit registradosCambio();
    };
    // Una cara que no se conocía: se la da de alta con su foto (y el nombre, si
    // lo dijo), así la próxima vez la reconoce la puerta, el lobby o el tótem.
    if (personaId.isEmpty() && !c.mejorFoto.isEmpty() && pv != nullptr && pv->disponible()) {
        QPointer<TotemsViewModel> yo(this);
        const QString conn = c.conn;
        pv->altaConFoto(c.mejorFoto, nombre, {}, [yo, anotar, conn](const QString& id, const QString&) {
            if (!yo) return;
            // Desde ahora esa cara es conocida: la que acaba de terminar, no «otra persona»
            // frente al tótem (si no, el kiosco la saludaría de nuevo enseguida).
            if (auto it = yo->conns_.find(conn); it != yo->conns_.end() && !id.isEmpty()) it->second->ultimaPersona = id;
            anotar(id);
        });
        return;
    }
    anotar(personaId);
}

void TotemsViewModel::tic() {
    const qint64 ahora = ahoraMs();
    for (auto& [k, cp] : conns_) {
        Conexion& c = *cp;
        if (c.canal > 0 && !c.oido) abrirOido(c);
        mirarCara(c);
        mirarDocumento(c);
        // Terminó de hablar: lo sintetizado ya salió por el canal.
        if (c.hablando) {
            const VozAlLobby* v = c.canal > 0 && c.canal < static_cast<int>(voces_.size())
                                      ? voces_[static_cast<std::size_t>(c.canal)].get()
                                      : nullptr;
            const bool salio = !c.sintetizando && (v == nullptr || v->pendiente() < 0.02);
            if (salio && c.finHablaMs == 0) c.finHablaMs = ahora;
            if (salio && ahora - c.finHablaMs >= kMargenVozMs) {
                c.hablando = false;
                c.callado->store(false);
                c.ultimaVozMs = ahora;
                if (c.hayListo) {
                    c.hayListo = false;
                    c.terminoMs = ahora;
                    enviar(c.conn, {{"type", "listo"}, {"texto", c.listoPendiente}});
                }
                if (c.conv != nullptr && c.conv->enCurso()) {
                    // Pidió la foto: desde ahora se buscan tomas de frente (y se cuenta el tiempo).
                    if (c.buscandoFoto) c.fotoDesdeMs = ahora;
                    if (c.buscandoDocumento) c.docDesdeMs = ahora; // pidió el documento: desde ahora se busca
                    c.estado = c.buscandoFoto ? "foto" : c.buscandoDocumento ? "documento" : "escuchando";
                    enviar(c.conn, {{"type", "estado"}, {"estado", c.estado.toStdString()}});
                    emit vivoCambio();
                }
            }
            continue;
        }
        if (c.buscandoFoto && c.conv != nullptr) {
            if (c.fotoDesdeMs == 0) c.fotoDesdeMs = ahora; // sin voz: no hubo nada que esperar
            if (ahora - c.fotoDesdeMs > kFotoMs) {
                const auto r = c.conv->fotoFallo(c.fotoSugerencia.toStdString());
                aplicar(c, r.decir, r.campos, r.pedir, r.guardar, r.terminar, r.foto);
            }
            continue;
        }
        if (c.buscandoDocumento && c.conv != nullptr) {
            if (c.docDesdeMs == 0) c.docDesdeMs = ahora; // sin voz: no hubo nada que esperar
            if (ahora - c.docDesdeMs > kDocumentoMs) {
                const auto r = c.conv->documentoFallo(c.docSugerencia.toStdString());
                aplicar(c, r.decir, r.campos, r.pedir, r.guardar, r.terminar, r.foto);
            }
            continue;
        }
        if (c.conv != nullptr && c.conv->enCurso()) {
            if (ahora - c.ultimaVozMs > kSilencioMs) {
                c.ultimaVozMs = ahora;
                const auto r = c.conv->silencio();
                aplicar(c, r.decir, r.campos, r.pedir, r.guardar, r.terminar, r.foto);
            }
            continue;
        }
        // La pantalla de gracias ya volvió sola a esperar.
        if (c.estado == "listo" && ahora - c.terminoMs > kListoMs) {
            c.estado = "esperando";
            emit vivoCambio();
        }
        // Tomó canal y no aparece una cara: el canal ES el arranque (el que abre
        // la página en el celular ya decidió registrarse). Solo la primera vez
        // por canal: en un kiosco el canal queda y lo que arranca es una cara nueva.
        if (c.canal > 0 && c.primeraDelCanal && !c.identificando && ahora - c.canalDesdeMs > kArranqueSinCaraMs) {
            const bool conocida = !c.ultimaCara.isEmpty() && c.ultimaCara != "?";
            empezar(c, conocida ? c.ultimaCara : QString(), conocida ? c.ultimoNombre : QString());
        }
    }
}

void TotemsViewModel::mirarDocumento(Conexion& c) {
    if (!c.buscandoDocumento || c.docDesdeMs == 0 || c.leyendoDocumento || c.canal == 0 || !lector_ ||
        c.conv == nullptr)
        return;
    if (ahoraMs() - c.ultimoDocMs < kEntreDocumentosMs) return;
    auto* frames = static_cast<FrameService*>(vm_.frames());
    if (frames == nullptr) return;
    // El mismo cuadro de la cámara del tótem que se reconoce (y que ve el operador).
    QImage img;
    std::uint64_t serie = 0;
    if (!frames->latest(QString("registro:%1").arg(c.canal), img, serie) || img.isNull() || serie == c.ultimoCuadroDoc)
        return;
    c.ultimoCuadroDoc = serie;
    c.ultimoDocMs = ahoraMs();
    c.leyendoDocumento = true;
    const QString conn = c.conn;
    QPointer<TotemsViewModel> yo(this);
    // No descartable: ya hay a lo sumo uno por conexión (leyendoDocumento), y uno
    // descartado no volvería y dejaría la conexión esperando para siempre.
    lector_->leer(img, c.conv->documentosAceptados(), false, [yo, conn](LectorDocumentos::Resultado r) {
        if (!yo) return;
        const auto it = yo->conns_.find(conn);
        if (it == yo->conns_.end()) return;
        Conexion& k = *it->second;
        k.leyendoDocumento = false;
        if (!k.buscandoDocumento || k.conv == nullptr) return;
        if (r.leido) {
            // En el registro, a lo sumo el tipo y los últimos tres dígitos.
            const registro::Totem* t = yo->totems_.porId(k.totem.toStdString());
            ba::diag::registro().linea(ba::diag::Nivel::Info, "registro", 0,
                                       "tótem «" + (t != nullptr ? t->id : k.totem.toStdString()) + "» (" +
                                           conn.toStdString() + "): documento leído (" +
                                           documentos::paraLog(documentos::Datos::deJson(r.datos)) + ")");
            const auto x = k.conv->documentoLeido(r.datos);
            yo->aplicar(k, x.decir, x.campos, x.pedir, x.guardar, x.terminar, x.foto);
            return;
        }
        // Qué corregir, si pasa el tiempo: se vio un código pero no es de un documento aceptado.
        if (!r.codigos.empty())
            k.docSugerencia = "Ese código no es de un documento que pueda leer. Mostrame el lado del código de barras.";
    });
}

void TotemsViewModel::cortar(const QString& conn) {
    ConferenceClient* c = clienteDe(vm_);
    if (c == nullptr) return;
    sala::pedir(*c, "POST", "/api/v1/registro/cortar", {{"conn", conn.toStdString()}}, [](const std::string&, const json&) {});
}

QVariantMap TotemsViewModel::filaDe(const Conexion& c) const {
    const registro::Totem* t = totems_.porId(c.totem.toStdString());
    return QVariantMap{{"conn", c.conn},
                       {"id", c.totem},
                       {"totem", t != nullptr ? QString::fromStdString(t->nombre) : c.totem},
                       {"canal", c.canal},
                       {"audio", c.audio},
                       {"ip", c.ip},
                       {"lugar", c.lugar},
                       {"desdeMs", c.desdeMs},
                       {"estado", c.estado},
                       {"paso", c.conv != nullptr ? QString::fromStdString(c.conv->paso()) : QString("esperando")},
                       {"quien", c.conv != nullptr ? QString::fromStdString(c.conv->nombre()) : QString()},
                       {"oido", c.oidoTexto},
                       {"dice", c.diceTexto},
                       {"campos", c.conv != nullptr ? aQv(c.conv->camposParaMostrar()) : QVariant(QVariantList{})}};
}

QVariantList TotemsViewModel::conexiones() const {
    QVariantList out;
    for (const auto& [k, c] : conns_) out << filaDe(*c);
    return out;
}

json TotemsViewModel::estadoJson() const {
    json cs = json::array();
    for (const auto& [k, c] : conns_) {
        json f = aJson(QVariant(filaDe(*c)));
        // Por MCP, sin los datos del documento (ver datosParaAfuera): lo dicho y oído
        // con los números largos tapados, y el campo `documento` en su resumen.
        f["dice"] = documentos::taparNumeros(f.value("dice", std::string()));
        f["oido"] = documentos::taparNumeros(f.value("oido", std::string()));
        if (c->conv != nullptr) {
            const json d = c->conv->datos();
            for (json& campo : f["campos"]) {
                const std::string clave = campo.value("clave", std::string());
                if (!d.contains(clave) || !d[clave].is_object()) continue;
                const json r = documentos::resumenParaAfuera(d[clave]);
                const std::string nombre = r.value("nombre", std::string()), numero = r.value("numero", std::string());
                campo["valor"] = nombre.empty() ? numero : numero.empty() ? nombre : nombre + " · " + numero;
            }
        }
        cs.push_back(std::move(f));
    }
    return {{"conexiones", cs},
            {"hablarFalta", hablarFalta().toStdString()},
            {"enLinea", enLinea()},
            {"conversando", std::count_if(conns_.begin(), conns_.end(),
                                          [](const auto& p) { return p.second->conv && p.second->conv->enCurso(); })}};
}

QVariantList TotemsViewModel::registrados() const {
    QVariantList out;
    EventoViewModel* ev = eventoDe(vm_);
    if (ev == nullptr) return out;
    for (const QVariant& v : ev->padron()) {
        QVariantMap m = v.toMap();
        const QString reg = m.value("registro").toString();
        if (reg.isEmpty() && m.value("porDonde").toString() != "registro") continue;
        const registro::Totem* t = totems_.porId(reg.toStdString());
        m["totem"] = t != nullptr ? QString::fromStdString(t->nombre) : reg;
        // Cómo se lee cada dato (un documento es un objeto: «Juana Prueba · DNI
        // 11.222.333»), con la MISMA regla que la pantalla del tótem.
        QVariantMap texto;
        const json datos = aJson(m.value("datos"));
        for (auto it = datos.begin(); datos.is_object() && it != datos.end(); ++it) {
            if (!it.value().is_object()) continue;
            registro::Campo c;
            c.tipo = registro::TipoCampo::Documento;
            texto[QString::fromStdString(it.key())] = QString::fromStdString(registro::paraMostrar(c, it.value()));
        }
        m["datosTexto"] = texto;
        out << m;
    }
    // Lo último arriba: es lo que se mira mientras la gente llega.
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value("llegoMs").toLongLong() > b.toMap().value("llegoMs").toLongLong();
    });
    return out;
}

json TotemsViewModel::registradosJson() const {
    json out = json::array();
    for (const QVariant& v : registrados()) {
        const QVariantMap m = v.toMap();
        out.push_back({{"id", m.value("id").toString().toStdString()},
                       {"nombre", m.value("nombre").toString().toStdString()},
                       {"personaId", m.value("personaId").toString().toStdString()},
                       {"registro", m.value("registro").toString().toStdString()},
                       {"totem", m.value("totem").toString().toStdString()},
                       {"llegoMs", m.value("llegoMs").toLongLong()},
                       {"datos", datosParaAfuera(aJson(m.value("datos")))}});
    }
    return out;
}

} // namespace ba::app
