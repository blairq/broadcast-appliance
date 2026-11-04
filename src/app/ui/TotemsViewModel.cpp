// ============================================================================
// src/app/ui/TotemsViewModel.cpp — ver TotemsViewModel.hpp
// ============================================================================
#include "ui/TotemsViewModel.hpp"
#include "i18n/I18n.hpp"

#include <QCoreApplication>

#include <QCryptographicHash>
#include <QBuffer>
#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <vector>

#include "agent/ChatEnVivo.hpp"
#include "agent/ModelosLocales.hpp"
#include "agent/Roles.hpp"
#include "agent/ServidorModeloLocal.hpp"
#include "agent/Voz.hpp"
#include "ajustes/Ajustes.hpp"
#include "arranque/AppRuntime.hpp"
#include "diag/Registro.hpp"
#include "documentos/Documento.hpp"
#include "documentos/Mrz.hpp"
#include "emision/FotoMotor.hpp"
#include "emision/OperatorFeed.hpp"
#include "emision/VentanillaMotor.hpp"
#include "emision/VozAlLobby.hpp"
#include "registro/AgenteTotem.hpp"
#include "registro/ConexionesTotem.hpp"
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

std::int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

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

/// El llama-server del programa como lo piden los tótems: la visión (el dorso) y el modelo que
/// entiende (el rol «registro») con el MISMO pedido, así comparten un solo servidor cargado.
/// `gguf` vacío = el del modelo local activo.
/// La configuración, de la foto del motor (es del plano de control).
agent::ServidorModeloLocal::Pedido pedidoLocal(agent::ModelosLocales* ml, AppRuntime& rt, const VentanillaMotor& ventanilla,
                                               const QString& gguf = {}) {
    agent::ServidorModeloLocal::Pedido p;
    p.ejecutable = agent::ServidorModeloLocal::ejecutable();
    p.gguf = !gguf.isEmpty() ? gguf : ml != nullptr ? ml->ggufParaChat() : QString();
    p.mmproj = ml != nullptr ? ml->mmprojPara(p.gguf) : QString();
    const auto cfg = ventanilla.foto().configuracion.app;
    const AppConfig porOmision;
    p.capasGpu = (cfg ? *cfg : porOmision).modeloCapasGpu;
    p.contexto = (cfg ? *cfg : porOmision).modeloContexto; // el mismo que el chat: comparten el servidor
    p.logs = QString::fromStdWString((rt.dataRoot() / "Logs").wstring());
    p.cargaS = ba::ajustes::entero("modelo.cargaS", 180);
    p.noReiniciar = true; // si el chat tiene cargado otro modelo, no se lo saca
    return p;
}

EventoViewModel* eventoDe(StudioViewModel& vm) { return static_cast<EventoViewModel*>(vm.evento()); }
PersonasViewModel* personasDe(StudioViewModel& vm) { return static_cast<PersonasViewModel*>(vm.personas()); }
ConferenceClient* clienteDe(StudioViewModel& vm) {
    auto* c = static_cast<ConferenceViewModel*>(vm.conference());
    return c != nullptr ? &c->client() : nullptr;
}

} // namespace

namespace {

/// El modelo que entiende (registro/AgenteTotem) como lo ve registro/ConexionesTotem.
class EntendedorTotem : public registro::Entendedor {
   public:
    explicit EntendedorTotem(std::unique_ptr<registro::AgenteTotem> a) : a_(std::move(a)) {}
    bool oido(const std::string& dicho) override { return a_->oido(dicho); }
    bool ocupado() const override { return a_->ocupado(); }
    bool caido() const override { return a_->caido(); }
    std::string motivoCaida() const override { return a_->motivoCaida(); }
    std::string nombre() const override { return a_->fuente().nombre; }
    void cancelar() override { a_->cancelar(); }

   private:
    std::unique_ptr<registro::AgenteTotem> a_;
};

} // namespace

/// Lo que registro/ConexionesTotem le pide al mundo: el gateway, la voz, la cámara, el
/// lector, el padrón y el modelo que entiende.
struct TotemsViewModel::Puertos : registro::ConexionesTotem::Puertos {
    TotemsViewModel& vm;
    explicit Puertos(TotemsViewModel& v) : vm(v) {}

    TotemsViewModel::Medios& medios(const std::string& conn) { return vm.medios_[conn]; }
    VozAlLobby* voz(int canal) const {
        return canal > 0 && canal < static_cast<int>(vm.voces_.size()) ? vm.voces_[static_cast<std::size_t>(canal)].get()
                                                                         : nullptr;
    }

    void vivoCambio() override { emit vm.vivoCambio(); }
    void conectadosCambio() override { emit vm.cambio(); }
    void registradosCambio() override { emit vm.registradosCambio(); }
    void pedirTic(bool si) override {
        if (si) vm.tic_.start();
        else vm.tic_.stop();
    }

    void enviar(const std::string& conn, const json& msg) override { vm.enviar(QString::fromStdString(conn), msg); }
    void alGateway(const std::string& ruta, const json& cuerpo) override {
        ConferenceClient* c = clienteDe(vm.vm_);
        if (c == nullptr || !c->online()) return;
        sala::pedir(*c, "POST", "/api/v1/registro/" + ruta, cuerpo, [](const std::string&, const json&) {});
    }

    void abrirAudio(const std::string& conn, int k) override {
        if (k < 1 || k >= static_cast<int>(vm.voces_.size())) return;
        vm.cargarVoz();
        if (!vm.voces_[static_cast<std::size_t>(k)])
            vm.voces_[static_cast<std::size_t>(k)] = std::make_unique<VozAlLobby>(ba::ipc::nombreVozRegistro(k));
        vm.voces_[static_cast<std::size_t>(k)]->abrir();
        medios(conn).canal = k;
        revisarOido(conn, k);
    }
    void cerrarAudio(const std::string& conn, int canal) override {
        auto it = vm.medios_.find(conn);
        if (it != vm.medios_.end()) {
            vm.cerrarMedios(conn, it->second);
            vm.medios_.erase(it);
        }
        if (VozAlLobby* v = voz(canal)) v->cerrar();
    }
    void revisarOido(const std::string& conn, int k) override {
        // El reconocedor carga en segundo plano al arrancar la app: un tótem que se conecta
        // antes (la app se reinició y la página volvió sola) se queda sin oído hasta que
        // esté; cada vuelta del tic vuelve a probar. El del tótem es el que se eligió en la
        // pestaña Agentes (Parakeet o Nemotron): con que esté bajado alcanza, se carga al abrirlo.
        TotemsViewModel::Medios& m = medios(conn);
        if (m.oido || vm.voz_ == nullptr || !vm.voz_->puedeOir("registro") || k < 1) return;
        const QString clave = QString::fromStdString("registro:" + conn);
        QPointer<TotemsViewModel> yo(&vm);
        m.oido = vm.voz_->abrirOido(
            clave,
            [yo, conn](const QString& t) {
                if (yo) yo->vivo_->alOido(conn, t.trimmed().toStdString(), ahoraMs());
            },
            [yo, conn]() {
                if (yo) yo->vivo_->interrumpido(conn, ahoraMs());
            });
        if (!m.oido) return;
        agent::Voz* voz = vm.voz_;
        const std::string cl = clave.toStdString();
        const auto callado = m.callado;
        // Con Nemotron y «interrumpir», el oído sigue oyendo mientras habla el asistente: la
        // página pide el micrófono con cancelación de eco y su voz baja por el mismo WebRTC.
        m.oye = voz->oyeMientrasHabla(clave);
        const auto oye = m.oye;
        m.toma = vm.rt_.operatorFeed().escucharRegistro(k, [voz, cl, callado, oye](const float* d, std::size_t n) {
            thread_local std::vector<float> mono;
            mono.resize(n);
            // Mientras habla el asistente, ceros: así el reconocedor cierra la frase
            // que tuviera abierta y no se transcribe la propia voz que vuelve.
            const bool cero = callado->load(std::memory_order_relaxed) && !oye->load(std::memory_order_relaxed);
            for (std::size_t i = 0; i < n; ++i) mono[i] = cero ? 0.0f : d[2 * i]; // un micrófono es mono
            voz->alimentarOido(cl, mono.data(), n, 48000);
        });
    }
    bool sintetizar(const std::string& conn, int k, const std::string& texto) override {
#if defined(BA_HAVE_VOZ)
        vm.cargarVoz(); // si cambió el idioma del evento, la voz de ese idioma (la frase que sigue)
        AtencionViewModel* a = vm.vm_.atencionVm();
        VozAlLobby* destino = voz(k);
        if (!vm.vozLista_ || !vm.habla_ || a == nullptr || destino == nullptr) return false;
        TotemsViewModel::Medios& m = medios(conn);
        m.mudo->store(false);
        m.callado->store(true);
        QPointer<TotemsViewModel> yo(&vm);
        vm.habla_->decir(
            // El hablante elegido es de la voz del atendedor: con la del idioma del evento, el primero.
            texto, vm.vozCargada_ == a->vozElegida() ? a->hablante() : 0, static_cast<float>(a->velocidad()),
            [destino, mudo = m.mudo](const float* d, std::size_t n, std::uint32_t f) {
                if (!mudo->load()) destino->encolar(d, n, f); // atendida en el medio: no sale
            },
            [yo, conn]() {
                QMetaObject::invokeMethod(yo.data(), [yo, conn]() {
                    if (yo) yo->vivo_->alTerminarFrase(conn);
                });
            });
        return true;
#else
        (void)conn;
        (void)k;
        (void)texto;
        return false;
#endif
    }
    void callar(const std::string& conn, int canal, bool sintesis) override {
        TotemsViewModel::Medios& m = medios(conn);
        m.mudo->store(true);
        if (VozAlLobby* v = voz(canal)) v->callar();
#if defined(BA_HAVE_VOZ)
        if (sintesis && vm.habla_) vm.habla_->callar();
#else
        (void)sintesis;
#endif
        m.callado->store(false);
    }
    void escuchar(const std::string& conn) override { medios(conn).callado->store(false); }
    bool oyeMientrasHabla(const std::string& conn) const override {
        const auto it = vm.medios_.find(conn);
        return it != vm.medios_.end() && it->second.oye && it->second.oye->load(std::memory_order_relaxed);
    }
    bool oyendoVoz(const std::string& conn) const override {
        return vm.voz_ != nullptr && vm.voz_->oyendoVoz(QString::fromStdString("registro:" + conn));
    }
    bool vozPendiente(int canal) const override {
        const VozAlLobby* v = voz(canal);
        return v != nullptr && v->pendiente() >= 0.02;
    }

    /// El último cuadro de la cámara del tótem si es nuevo (y anota su número).
    bool cuadro(int canal, std::uint64_t& ultimo, QImage& img) const {
        auto* frames = static_cast<FrameService*>(vm.vm_.frames());
        if (frames == nullptr) return false;
        std::uint64_t serie = 0;
        if (!frames->latest(QString("registro:%1").arg(canal), img, serie) || img.isNull() || serie == ultimo) return false;
        ultimo = serie;
        return true;
    }
    bool pedirCara(const std::string& conn, int canal) override {
        PersonasViewModel* pv = personasDe(vm.vm_);
        if (pv == nullptr || !pv->disponible()) return false;
        QImage img;
        if (!cuadro(canal, medios(conn).ultimoCuadro, img)) return false;
        // En JPEG: es lo que guarda la libreta como foto de la ficha si hay alta.
        QByteArray jpeg;
        QBuffer buf(&jpeg);
        buf.open(QIODevice::WriteOnly);
        if (!img.convertToFormat(QImage::Format_RGB888).save(&buf, "JPG", 88) || jpeg.isEmpty()) return false;
        QPointer<TotemsViewModel> yo(&vm);
        pv->identificarFoto(jpeg, [yo, conn, jpeg](const PersonasViewModel::Identificado& q, const QString& error) {
            if (!yo) return;
            if (!error.isEmpty()) return yo->vivo_->sinCara(conn);
            registro::ConexionesTotem::Cara c;
            c.personaId = q.personaId.toStdString();
            c.nombre = q.nombre.toStdString();
            c.hayCara = q.hayCara;
            c.sirve = q.sirve;
            c.sugerencia = q.sugerenciaEvento.toStdString();
            c.foto = jpeg.toStdString();
            yo->vivo_->alCara(conn, c, ahoraMs());
        });
        return true;
    }
    bool pedirDocumento(const std::string& conn, int canal, const std::vector<std::string>& aceptados) override {
        if (!vm.lector_) return false;
        QImage img;
        if (!cuadro(canal, medios(conn).ultimoCuadroDoc, img)) return false;
        QPointer<TotemsViewModel> yo(&vm);
        // No descartable: ya hay a lo sumo uno por conexión, y uno descartado no volvería y
        // dejaría la conexión esperando para siempre.
        vm.lector_->leer(img, aceptados, false, [yo, conn](LectorDocumentos::Resultado r) {
            if (yo) yo->vivo_->alLeerDocumento(conn, lectura(r), ahoraMs());
        });
        return true;
    }
    void traducirCodigo(const std::string& conn, const std::string& formato, const std::string& texto,
                        const std::vector<std::string>& aceptados) override {
        if (!vm.lector_) return vm.vivo_->alLeerDocumento(conn, {}, ahoraMs());
        documentos::Codigo cod;
        cod.formato = formato;
        cod.texto = texto;
        QPointer<TotemsViewModel> yo(&vm);
        vm.lector_->traducir({cod}, aceptados, [yo, conn](LectorDocumentos::Resultado r) {
            if (yo) yo->vivo_->alLeerDocumento(conn, lectura(r), ahoraMs());
        });
    }
    void pedirCuadroDePagina(const std::string& conn) override {
        ConferenceClient* cli = clienteDe(vm.vm_);
        if (cli == nullptr || !vm.lector_) {
            vm.vivo_->alCuadroDePagina(conn, false);
            return;
        }
        QPointer<TotemsViewModel> yo(&vm);
        cli->getRaw("/api/v1/registro/cuadro?conn=" + QString::fromUtf8(QUrl::toPercentEncoding(QString::fromStdString(conn))),
                    [yo, conn](bool ok, const QByteArray& jpeg) {
                        if (!yo) return;
                        const QImage img = ok ? QImage::fromData(jpeg, "JPG") : QImage();
                        const auto q = yo->vivo_->alCuadroDePagina(conn, !img.isNull());
                        if (q.dorso)
                            yo->lector_->leerDorso(img, q.aceptados, q.lecturas, [yo, conn](LectorDocumentos::Resultado r) {
                                if (yo) yo->vivo_->alLeerDorso(conn, lectura(r), ahoraMs());
                            });
                        if (q.frente)
                            yo->lector_->leer(img.convertToFormat(QImage::Format_RGB32), q.aceptados, false,
                                              [yo, conn](LectorDocumentos::Resultado r) {
                                                  if (yo) yo->vivo_->alLeerDocumento(conn, lectura(r), ahoraMs());
                                              });
                    });
    }
    void prepararDorso() override {
        if (vm.lector_) vm.lector_->prepararDorso();
    }
    void soltarDorso() override {
        if (vm.lector_) vm.lector_->soltarDorso();
    }
    bool dorsoDisponible() const override { return vm.dorsoDisponible(); }

    bool yaRegistrado(const std::string& personaId) const override {
        EventoViewModel* ev = eventoDe(vm.vm_);
        return ev != nullptr && ev->padronVm().registradoPorTotem(QString::fromStdString(personaId));
    }
    void guardarRegistro(const std::string& conn, const std::string& totem, const std::string& nombre,
                         const std::string& personaId, const json& datos, const std::string& foto) override {
        vm.guardarRegistro(conn, QString::fromStdString(totem), QString::fromStdString(nombre),
                           QString::fromStdString(personaId), datos, QByteArray::fromStdString(foto));
    }
    std::unique_ptr<registro::Entendedor> crearEntendedor(const std::string& conn, const std::string& totem,
                                                          registro::Conversacion& conv, bool avisar) override {
        return vm.crearEntendedor(conn, totem, conv, avisar);
    }
    void prepararModelo() override { vm.prepararModelo(); }
    void soltarModelo() override { vm.soltarModelo(); }

    /// Lo leído, como lo entiende el modelo.
    static registro::ConexionesTotem::Lectura lectura(const LectorDocumentos::Resultado& r) {
        registro::ConexionesTotem::Lectura l;
        l.leido = r.leido;
        l.datos = r.datos;
        l.hayCodigos = !r.codigos.empty();
        l.con = r.con;
        l.error = r.error.toStdString();
        l.dudosa = r.dudosa;
        l.noSeVe = r.noSeVe;
        l.hayDetalle = !r.detalle.empty();
        return l;
    }
};


TotemsViewModel::TotemsViewModel(AppRuntime& rt, StudioViewModel& vm, agent::Voz* voz, agent::ModelosLocales* modelos,
                                 QObject* parent)
    : QObject(parent), rt_(rt), vm_(vm), voz_(voz), modelos_(modelos), puertos_(std::make_unique<Puertos>(*this)),
      vivo_(std::make_unique<registro::ConexionesTotem>(*puertos_)) {
    connect(this, &TotemsViewModel::cambio, this, [this] { filasTotems_.poner(totems()); });
    connect(this, &TotemsViewModel::vivoCambio, this, [this] { filasConexiones_.poner(conexiones()); });
    connect(this, &TotemsViewModel::registradosCambio, this, [this] { filasRegistrados_.poner(registrados()); });
    if (EventoViewModel* ev = eventoDe(vm_)) {
        connect(&ev->guionVm(), &GuionViewModel::guionChanged, this, &TotemsViewModel::recargar);
        connect(&ev->padronVm(), &PadronViewModel::padronChanged, this, &TotemsViewModel::registradosCambio);
    }
    if (ConferenceClient* c = clienteDe(vm_)) {
        connect(c, &ConferenceClient::registroEvento, this, [this](const QJsonObject& o) {
            const json ev = json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
            if (ev.is_object()) vivo_->alEvento(ev, ahoraMs());
        });
        // El gateway volvió (o se reinició): no sabe de los tótems ni de nada vivo.
        c->registrarRepublicador("totems", this, [this]() { publicar(); });
        // Sin gateway no hay nadie conectado; al volver (el mismo o uno nuevo) se le
        // pregunta quién está.
        connect(c, &ConferenceClient::onlineChanged, this, [this](bool on) {
            if (on)
                reconocerConexiones();
            else
                vivo_->soltarTodas(ahoraMs());
            emit cambio();
        });
    }
    // Las urls cambian con la dirección de la red o el túnel.
    connect(&vm_.direcciones(), &sala::Direcciones::cambio, this, &TotemsViewModel::cambio);
    if (AtencionViewModel* a = vm_.atencionVm())
        connect(a, &AtencionViewModel::vozCambio, this, [this]() {
            // Otra voz del atendedor: se recarga la próxima vez que haga falta.
            if (vozLista_ && vm_.atencionVm() != nullptr && vm_.atencionVm()->vozElegida() != vozCargada_) vozLista_ = false;
            emit cambio();
        });
    // El reloj de las conversaciones: lo pide el modelo, solo con conexiones activas.
    tic_.setInterval(200);
    connect(&tic_, &QTimer::timeout, this, [this]() { vivo_->tic(ahoraMs()); });
    lector_ = std::make_unique<LectorDocumentos>(vm_.plugins(), rt_.dataRoot());
    // El dorso con el modelo local: el llama-server del programa con el proyector de
    // visión (--mmproj), en 127.0.0.1. La foto no sale de la máquina.
    {
        LectorDocumentos::VisionLocal v;
        agent::ModelosLocales* ml = modelos_;
        v.disponible = [ml](QString* falta) {
            if (ml == nullptr) return false;
            QString f;
            const QString gguf = ml->ggufParaChat({}, &f);
            if (gguf.isEmpty()) {
                if (falta != nullptr) *falta = f;
                return false;
            }
            QString repo;
            if (ml->mmprojPara(gguf, &repo).isEmpty()) {
                if (falta != nullptr)
                    *falta = repo.isEmpty() ? QCoreApplication::translate("totems", "el modelo local no tiene un proyector de visión en el catálogo")
                                            : QCoreApplication::translate("totems", "falta bajar la visión del modelo local («%1», modelo_local_bajar)").arg(repo);
                return false;
            }
            if (!QFileInfo::exists(agent::ServidorModeloLocal::ejecutable())) {
                if (falta != nullptr) *falta = QCoreApplication::translate("totems", "falta llama-server en el programa");
                return false;
            }
            return true;
        };
        AppRuntime* rt = &rt_;
        VentanillaMotor* ven = &vm_.ventanilla();
        v.pedir = [ml, rt, ven](QObject* quien, std::function<void(quint16, QString)> listo) {
            agent::ServidorModeloLocal::el().pedir(quien, pedidoLocal(ml, *rt, *ven), std::move(listo));
        };
        v.soltar = [](QObject* quien) { agent::ServidorModeloLocal::el().soltar(quien); };
        lector_->setVisionLocal(std::move(v));
        // Bajar o borrar la visión (o cambiar de modelo) cambia si se puede leer el dorso.
        if (ml != nullptr) {
            connect(ml, &agent::ModelosLocales::cambio, this, &TotemsViewModel::dorsoCambio);
            connect(ml, &agent::ModelosLocales::discoCambio, this, &TotemsViewModel::dorsoCambio);
        }
    }
    connect(lector_.get(), &LectorDocumentos::tiposCambio, this, &TotemsViewModel::tiposDocumentoCambio);
    conectarAtencion();
    recargar();
}

TotemsViewModel::~TotemsViewModel() {
    tic_.stop();
    for (auto& [conn, m] : medios_) {
        cerrarMedios(conn, m);
        if (m.canal > 0 && m.canal < static_cast<int>(voces_.size()) && voces_[static_cast<std::size_t>(m.canal)])
            voces_[static_cast<std::size_t>(m.canal)]->cerrar();
    }
#if defined(BA_HAVE_VOZ)
    if (habla_) habla_->callar();
    habla_.reset(); // antes que los canales: su hilo les escribe
#endif
    for (auto& v : voces_) v.reset();
}

void TotemsViewModel::cerrarMedios(const std::string& conn, Medios& m) {
    if (m.toma != 0) rt_.operatorFeed().dejarDeEscucharLobby(m.toma);
    m.toma = 0;
    if (m.oido && voz_ != nullptr) voz_->cerrarOido(QString::fromStdString("registro:" + conn));
    m.oido = false;
    m.callado->store(false);
}

// --- La configuración ----------------------------------------------------------------

ba::i18n::En TotemsViewModel::idiomaDelEvento() const {
    return ba::i18n::En(eventoIdioma_.empty() ? ba::i18n::idiomaDelOperador() : eventoIdioma_);
}

void TotemsViewModel::recargar() {
    EventoViewModel* ev = eventoDe(vm_);
    const std::filesystem::path carpeta = ev != nullptr ? ev->eventoAbierto().dir() : std::filesystem::path();
    const std::string nombre = ev != nullptr ? ev->eventoAbierto().evento().nombre : std::string();
    // El idioma del evento: en el que hablan y entienden los tótems ("" = el del operador).
    const std::string idioma = ev != nullptr ? ev->eventoAbierto().evento().idioma : std::string();
    if (carpeta == carpeta_) {
        // El mismo evento: quizá cambió de nombre (va en el saludo y el título) o de idioma (lo
        // que ya se conversa sigue en el suyo; lo nuevo, en el nuevo).
        if (nombre != eventoNombre_ || idioma != eventoIdioma_) {
            eventoNombre_ = nombre;
            eventoIdioma_ = idioma;
            vivo_->ponerEvento(&totems_, eventoNombre_, eventoIdioma_);
            vivo_->mandarPantallas();
            emit cambio();
        }
        return;
    }
    // Otro evento: lo que se conversaba era del anterior.
    vivo_->reiniciarTodas(ahoraMs());
    carpeta_ = carpeta;
    eventoNombre_ = nombre;
    eventoIdioma_ = idioma;
    secretos_.clear();
    bool ok = true;
    std::string err;
    totems_ = registro::cargarTotems(carpeta_, &ok, &err);
    vivo_->ponerEvento(&totems_, eventoNombre_, eventoIdioma_);
    if (!ok) emit notify(tr("No pude leer los tótems de registro: %1").arg(QString::fromStdString(err)), "error");
    publicar();
    // La voz se carga ya, no con la primera persona: si no, el primer saludo sale mudo.
    if (!totems_.lista().empty()) cargarVoz();
    emit cambio();
    emit registradosCambio();
}

std::string TotemsViewModel::guardarYPublicar() {
    std::string err;
    if (!registro::guardarTotems(carpeta_, totems_, &err))
        return tr("No se pudo guardar: %1").arg(QString::fromStdString(err)).toStdString();
    publicar();
    // Lo que muestran las pantallas de cada tótem (título, campos) cambia en el acto.
    vivo_->mandarPantallas();
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

void TotemsViewModel::renovar(const QString& id) {
    if (totems_.porId(id.toStdString()) == nullptr) return;
    secreto(id.toStdString(), true);
    publicar(); // el gateway olvida el viejo y corta a quien entró con él
    emit cambio();
    avisarError(url(id));
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
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "registro", 0, ba::i18n::llenar(ba::i18n::traducir("registro", "no se pudieron publicar los tótems: %1"), {err}));
    });
    publicarAtencion();
}

void TotemsViewModel::reconocerConexiones() {
    ConferenceClient* c = clienteDe(vm_);
    if (c == nullptr) return;
    QPointer<TotemsViewModel> yo(this);
    sala::pedir(*c, "GET", "/api/v1/registro/estado", {}, [yo](const std::string& err, const json& r) {
        if (yo && err.empty()) yo->vivo_->reconocer(r, ahoraMs());
    });
}

QVariantList TotemsViewModel::tiposDocumento() const {
    return lector_ ? aQv(lector_->tipos()).toList() : QVariantList{};
}

bool TotemsViewModel::lectorDisponible() const { return LectorDocumentos::disponible(); }

QString TotemsViewModel::dorso() const {
    if (!lector_) return {};
    QString falta;
    const QString con = lector_->dorsoCon(&falta);
    if (!con.isEmpty()) return tr("Si el código no sale, pide la parte de atrás y la lee con el %1.").arg(con);
    return tr("Sin lectura del dorso: %1 (o configurá un modelo propio).").arg(falta);
}

QVariantList TotemsViewModel::tipos() const {
    QVariantList out;
    for (registro::TipoCampo t : registro::todosLosTipos())
        out << QVariantMap{{"id", QString::fromLatin1(registro::nombreDeTipo(t))},
                           {"etiqueta", QString::fromStdString(registro::etiquetaDeTipo(t))}};
    return out;
}

json TotemsViewModel::totemJson(const QString& id) const {
    const registro::Totem* t = totems_.porId(id.toStdString());
    if (t == nullptr) return nullptr;
    json j = t->aJson();
    j["url"] = url(id).toStdString();
    j["urlPantalla"] = urlPantalla(id).toStdString();
    j["conectados"] = vivo_->conectados(id.toStdString());
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
    if (!hayEvento()) return tr("No hay un evento abierto: los tótems son de un evento (abrí o creá uno).").toStdString();
    std::vector<std::string> usados;
    for (const registro::Totem& t : totems_.lista()) usados.push_back(t.id);
    const std::string nombre = texto(j, "nombre").empty() ? tr("Registro").toStdString() : texto(j, "nombre");
    std::string nuevoId = texto(j, "id");
    if (nuevoId.empty()) nuevoId = registro::idDesdeNombre(nombre, usados);
    if (std::string e = totems_.agregar(registro::totemNuevo(nuevoId, nombre, eventoIdioma_)); !e.empty()) return e;
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
    if (!hayEvento()) return tr("No hay un evento abierto.").toStdString();
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

void TotemsViewModel::cambiar(const QString& id, const QVariantMap& cambios) {
    const json j = aJson(QVariant(cambios));
    avisarError(QString::fromStdString(cambiarJson(id, j.is_object() ? j : json::object())));
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

void TotemsViewModel::agregarCampo(const QString& id, const QString& tipo) {
    const registro::Totem* t = totems_.porId(id.toStdString());
    if (t == nullptr) return avisarError(tr("No hay ese tótem."));
    registro::Campo c;
    registro::tipoDeNombre(tipo.toStdString(), c.tipo);
    // Lo ve el público en la pantalla del tótem: en el idioma del evento.
    c.etiqueta = registro::etiquetaDeTipo(c.tipo, eventoIdioma_);
    // Una clave libre a partir del tipo («correo», «correo_2»…).
    std::vector<std::string> usadas;
    for (const registro::Campo& o : t->campos) usadas.push_back(o.clave);
    std::string base = registro::nombreDeTipo(c.tipo);
    c.clave = base;
    for (int n = 2; std::find(usadas.begin(), usadas.end(), c.clave) != usadas.end(); ++n) c.clave = base + "_" + std::to_string(n);
    if (c.tipo == registro::TipoCampo::Opcion) {
        const ba::i18n::En e = idiomaDelEvento();
        c.opciones = {ba::i18n::llenar(e.traducir("totem", "Opción %1"), {"1"}), ba::i18n::llenar(e.traducir("totem", "Opción %1"), {"2"})};
    }
    // De fábrica, el DNI; los demás se marcan en el editor (o por MCP).
    if (c.tipo == registro::TipoCampo::Documento) {
        c.documentos = {"dni-ar"};
        c.etiqueta = "DNI";
    }
    json cs = camposDe(*t);
    cs.push_back(c.aJson());
    avisarError(QString::fromStdString(cambiarJson(id, {{"campos", cs}})));
}

void TotemsViewModel::quitarCampo(const QString& id, int indice) {
    const registro::Totem* t = totems_.porId(id.toStdString());
    if (t == nullptr || indice < 0 || indice >= static_cast<int>(t->campos.size())) return avisarError(tr("No hay ese campo."));
    json cs = camposDe(*t);
    cs.erase(static_cast<std::size_t>(indice));
    avisarError(QString::fromStdString(cambiarJson(id, {{"campos", cs}})));
}

void TotemsViewModel::moverCampo(const QString& id, int desde, int hasta) {
    const registro::Totem* t = totems_.porId(id.toStdString());
    const int n = t != nullptr ? static_cast<int>(t->campos.size()) : 0;
    if (desde < 0 || desde >= n || hasta < 0 || hasta >= n) return avisarError(tr("No hay ese campo."));
    if (desde == hasta) return;
    json cs = camposDe(*t);
    json c = cs[static_cast<std::size_t>(desde)];
    cs.erase(static_cast<std::size_t>(desde));
    cs.insert(cs.begin() + hasta, c);
    avisarError(QString::fromStdString(cambiarJson(id, {{"campos", cs}})));
}

void TotemsViewModel::cambiarCampo(const QString& id, int indice, const QVariantMap& cambios) {
    const registro::Totem* t = totems_.porId(id.toStdString());
    if (t == nullptr || indice < 0 || indice >= static_cast<int>(t->campos.size())) return avisarError(tr("No hay ese campo."));
    json cs = camposDe(*t);
    const json c = aJson(QVariant(cambios));
    for (auto it = c.begin(); c.is_object() && it != c.end(); ++it) cs[static_cast<std::size_t>(indice)][it.key()] = it.value();
    avisarError(QString::fromStdString(cambiarJson(id, {{"campos", cs}})));
}

// --- Lo vivo ---------------------------------------------------------------------------

bool TotemsViewModel::enLinea() const {
    ConferenceClient* c = clienteDe(vm_);
    return c != nullptr && c->online();
}

QString TotemsViewModel::hablarFalta() const {
    if (voz_ == nullptr) return tr("Esta compilación no trae el motor de voz.");
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

// --- El audio -----------------------------------------------------------------------------

void TotemsViewModel::cargarVoz() {
#if defined(BA_HAVE_VOZ)
    AtencionViewModel* a = vm_.atencionVm();
    if (a == nullptr || modelos_ == nullptr) return;
    // El tótem le habla al público en el idioma del evento: la voz del atendedor si
    // habla esa lengua; si no, la del perfil del idioma (agent/VozIdioma).
    const QString repo = modelos_->vozPara(idiomaDelEvento().codigo(), a->vozElegida());
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
            if (!error.empty()) emit yo->notify(tr("La voz de los tótems no cargó: %1").arg(QString::fromStdString(error)), "warn");
        });
    });
#endif
}

void TotemsViewModel::cortar(const QString& conn) {
    ConferenceClient* c = clienteDe(vm_);
    if (c == nullptr) return;
    sala::pedir(*c, "POST", "/api/v1/registro/cortar", {{"conn", conn.toStdString()}}, [](const std::string&, const json&) {});
}

QVariantList TotemsViewModel::conexiones() const { return aQv(vivo_->filas()).toList(); }

json TotemsViewModel::estadoJson() const {
    return {{"conexiones", vivo_->filasParaAfuera()},
            {"atienden", vivo_->atiendenJson()},
            {"hablarFalta", hablarFalta().toStdString()},
            {"agente", agente().toStdString()},
            {"enLinea", enLinea()},
            {"conversando", vivo_->conversando()}};
}

void TotemsViewModel::guardarRegistro(const std::string& conn, const QString& totem, const QString& nombre,
                                      const QString& personaId, const json& datos, const QByteArray& foto) {
    EventoViewModel* ev = eventoDe(vm_);
    if (ev == nullptr) return;
    PersonasViewModel* pv = personasDe(vm_);
    const auto anotar = [this, ev, nombre, totem, datos](const QString& persona) {
        const QString id = ev->padronVm().registrarDesdeTotem(persona, nombre, totem, datos);
        if (id.isEmpty()) {
            emit notify(tr("Alguien se registró en un tótem pero no hay un evento abierto: no quedó anotado."), "warn");
            return;
        }
        ba::diag::registro().linea(ba::diag::Nivel::Info, "registro", 0,
                                   ba::i18n::llenar(ba::i18n::traducir("registro", "registrado por el tótem «%1»: «%2» (padrón %3)"), {totem.toStdString(), nombre.toStdString(), id.toStdString()}));
        emit registradosCambio();
    };
    // Una cara que no se conocía: se la da de alta con su foto (y el nombre, si
    // lo dijo), así la próxima vez la reconoce la puerta, el lobby o el tótem.
    if (personaId.isEmpty() && !foto.isEmpty() && pv != nullptr && pv->disponible()) {
        QPointer<TotemsViewModel> yo(this);
        pv->altaConFoto(foto, nombre, {}, [yo, anotar, conn](const QString& id, const QString&) {
            if (!yo) return;
            yo->vivo_->conocida(conn, id.toStdString());
            anotar(id);
        });
        return;
    }
    anotar(personaId);
}


QVariantList TotemsViewModel::registrados() const {
    QVariantList out;
    EventoViewModel* ev = eventoDe(vm_);
    if (ev == nullptr) return out;
    for (const QVariant& v : ev->padronVm().padron()) {
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

// ============================================================================
// La atención remota (docs/funciones/REGISTRO-TOTEM.md, «Atención remota»)
//
// Una persona del equipo, desde cualquier lado (la página /registro-atiende/ del
// gateway, con el link de este evento), ve todas las conversaciones, mira y escucha
// una por WebRTC (eso lo resuelve el gateway: ruta RegistroAtiende), la ATIENDE
// ella —el guion se pausa (registro/Conversacion::atender) y el asistente se calla—
// y completa o corrige los datos. Acá se decide: quién atiende qué (una a la vez),
// qué se hace con lo que pide y qué ven las páginas (el mismo estado que la pestaña
// Registro y totem_estado, más lo que hace falta para editar).
// ============================================================================

namespace {
const char* const kSecretoAtencion = "@atiende"; ///< no puede ser el id de un tótem ([a-z0-9-])
constexpr int kJuntarMs = 150;
} // namespace

void TotemsViewModel::conectarAtencion() {
    atencionTimer_.setSingleShot(true);
    atencionTimer_.setInterval(kJuntarMs);
    connect(&atencionTimer_, &QTimer::timeout, this, [this]() {
        if (vivo_->hayAtienden()) puertos_->alGateway("enviar", vivo_->paraAtencion());
    });
    // Cualquier cambio de lo vivo (lo oído, lo dicho, los campos, quién atiende) va a las
    // páginas de atención, juntado: en una charla cambia varias veces por segundo.
    connect(this, &TotemsViewModel::vivoCambio, this, [this]() {
        if (vivo_->hayAtienden() && !atencionTimer_.isActive()) atencionTimer_.start();
    });
}

QString TotemsViewModel::urlAtencion() const {
    if (!hayEvento()) return {};
    const Secreto s = secreto(kSecretoAtencion);
    // El token en el FRAGMENTO, como el de los tótems: no viaja en la petición.
    return QString::fromStdString(origen() + "/registro-atiende/#t=" + s.token);
}

void TotemsViewModel::renovarAtencion() {
    if (!hayEvento()) return;
    secreto(kSecretoAtencion, true);
    publicarAtencion(); // el gateway olvida el viejo y corta a quien entró con él
    emit cambio();
    avisarError(urlAtencion());
}

void TotemsViewModel::publicarAtencion() {
    ConferenceClient* c = clienteDe(vm_);
    if (c == nullptr || !c->online()) return;
    // Sin evento (o sin tótems) no hay nada que atender: sin credencial no entra nadie.
    json cred = json::object();
    if (hayEvento() && !totems_.lista().empty()) {
        const Secreto s = secreto(kSecretoAtencion);
        cred = {{"sal", s.sal}, {"hash_token", sha256(s.sal + s.token)}};
    }
    sala::pedir(*c, "POST", "/api/v1/registro/atencion", cred, [](const std::string& err, const json&) {
        if (!err.empty())
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "registro", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("registro", "no se pudo publicar el link de atención remota: %1"), {err}));
    });
}

QVariantList TotemsViewModel::atienden() const { return aQv(vivo_->atiendenJson()).toList(); }

json TotemsViewModel::atencionJson() const { return {{"url", urlAtencion().toStdString()}, {"atienden", vivo_->atiendenJson()}}; }

std::string TotemsViewModel::atenderConn(const QString& conn, const QString& quien, const QString& aconn) {
    return vivo_->atender(conn.toStdString(), quien.toStdString(), aconn.toStdString(), ahoraMs());
}

std::string TotemsViewModel::terminarConn(const QString& conn, const QString& como, const QString& aconn) {
    return vivo_->terminar(conn.toStdString(), como.toStdString(), aconn.toStdString(), ahoraMs());
}

std::string TotemsViewModel::ponerDatoConn(const QString& conn, const QString& clave, const QString& valor,
                                           const QString& aconn) {
    return vivo_->ponerDato(conn.toStdString(), clave.toStdString(), valor.toStdString(), aconn.toStdString(), ahoraMs());
}

void TotemsViewModel::atender(const QString& conn) { return avisarError(QString::fromStdString(atenderConn(conn, {}, {}))); }

void TotemsViewModel::terminarAtencion(const QString& conn, const QString& como) {
    avisarError(QString::fromStdString(terminarConn(conn, como, {})));
}

void TotemsViewModel::ponerDato(const QString& conn, const QString& clave, const QString& valor) {
    avisarError(QString::fromStdString(ponerDatoConn(conn, clave, valor, {})));
}


// ============================================================================
// El modelo que entiende (el rol «registro»; registro/AgenteRegistro, registro/AgenteTotem)
//
// Iniciativa mixta: el guion pregunta y confirma; el modelo entiende lo que la persona dice
// (varios datos de una, correcciones, preguntas sobre el evento con la ficha del tótem) y anota
// por herramientas que validan. Un AgenteTotem por conversación, con el servidor del rol: de
// fábrica el llama-server del programa (se carga cuando se conecta un tótem y se suelta cuando
// no queda ninguno), o el propio o uno de la nube si el organizador lo eligió en la pestaña
// Agentes. Sin modelo, o si no contesta a tiempo, el guion solo.
// ============================================================================

void TotemsViewModel::setRoles(agent::Roles* roles) {
    roles_ = roles;
    emit cambio();
}

namespace {
const agent::Rol* rolRegistro(const agent::Roles* roles) { return roles != nullptr ? roles->porId("registro") : nullptr; }
bool esLocal(const agent::Rol& r) { return r.proveedor.empty() || r.proveedor == "local"; }
} // namespace

bool TotemsViewModel::agenteAfuera() const {
    const agent::Rol* r = rolRegistro(roles_);
    return r != nullptr && r->activo && !esLocal(*r);
}

QString TotemsViewModel::agente() const {
    const agent::Rol* r = rolRegistro(roles_);
    if (r == nullptr) return tr("Guion solo (sin el rol «registro»).");
    if (!r->activo)
        return tr("Guion solo: el rol «%1» está apagado (pestaña Agentes).").arg(QString::fromStdString(r->titulo));
    if (esLocal(*r)) {
        if (puertoLocal_ != 0) return tr("Entiende con el modelo local: lo que dice la gente no sale de esta máquina.");
        if (pidiendoLocal_) return tr("Cargando el modelo local…");
        if (!faltaLocal_.isEmpty()) return tr("Guion solo: %1").arg(faltaLocal_);
        return tr("Con el modelo local (se carga cuando se conecta un tótem).");
    }
    agent::FuenteMirada f;
    std::string falta;
    if (!agent::ChatEnVivo::fuenteDeProveedor(r->proveedor, r->modelo, rt_.dataRoot(), f, &falta))
        return tr("Guion solo: %1.").arg(QString::fromStdString(falta));
    return tr("Entiende con %1: lo que dice la gente (sus datos) sale de esta máquina hacia ese servicio.")
        .arg(QString::fromStdString(f.nombre));
}

void TotemsViewModel::prepararModelo() {
    const agent::Rol* r = rolRegistro(roles_);
    if (r == nullptr || !r->activo || !esLocal(*r) || modelos_ == nullptr) return;
    // El servidor se reinició (otro modelo para el chat, se cayó): se pide de nuevo.
    if (puertoLocal_ != 0 && agent::ServidorModeloLocal::el().puerto() != puertoLocal_) puertoLocal_ = 0;
    if (pidiendoLocal_ || puertoLocal_ != 0) return;
    const bool alguno = std::any_of(totems_.lista().begin(), totems_.lista().end(),
                                    [](const registro::Totem& t) { return t.activo && t.conModelo; });
    if (!alguno) return;
    QString falta;
    const QString gguf = modelos_->ggufParaChat(QString::fromStdString(r->modelo), &falta);
    if (gguf.isEmpty() || !QFileInfo::exists(agent::ServidorModeloLocal::ejecutable())) {
        faltaLocal_ = gguf.isEmpty() ? (falta.isEmpty() ? tr("no hay un modelo local instalado") : falta)
                                     : tr("falta llama-server en el programa");
        emit cambio();
        return;
    }
    pidiendoLocal_ = true;
    faltaLocal_.clear();
    emit cambio();
    QPointer<TotemsViewModel> yo(this);
    agent::ServidorModeloLocal::el().pedir(this, pedidoLocal(modelos_, rt_, vm_.ventanilla(), gguf), [yo](quint16 puerto, QString error) {
        if (!yo) return;
        yo->pidiendoLocal_ = false;
        yo->puertoLocal_ = puerto;
        yo->faltaLocal_ = puerto != 0 ? QString() : error;
        if (puerto == 0)
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "registro", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("registro", "el modelo de los tótems no cargó (sigue el guion solo): %1"), {error.toStdString()}));
        emit yo->cambio();
    });
}

void TotemsViewModel::soltarModelo() {
    if (puertoLocal_ == 0 && !pidiendoLocal_) return;
    agent::ServidorModeloLocal::el().soltar(this); // sin nadie más que lo use, se apaga y libera la GPU
    puertoLocal_ = 0;
    pidiendoLocal_ = false;
    emit cambio();
}

bool TotemsViewModel::fuenteAgente(agent::FuenteMirada& f, std::string& prompt, std::string& falta) const {
    const agent::Rol* r = rolRegistro(roles_);
    if (r == nullptr || !r->activo) {
        falta = tr("el rol «registro» está apagado").toStdString();
        return false;
    }
    // El del rol, en el idioma del EVENTO (el modelo habla con el público): r->prompt es el del
    // operador. Uno propio (lo escribió el operador) va tal cual.
    if (!r->promptPropio.empty()) {
        prompt = r->promptPropio;
    } else {
        prompt = idiomaDelEvento().leer("prompts/roles/registro.md");
        if (prompt.empty()) prompt = r->prompt;
    }
    if (!esLocal(*r)) return agent::ChatEnVivo::fuenteDeProveedor(r->proveedor, r->modelo, rt_.dataRoot(), f, &falta);
    if (puertoLocal_ == 0) {
        falta = faltaLocal_.isEmpty() ? tr("el modelo local todavía está cargando").toStdString() : faltaLocal_.toStdString();
        return false;
    }
    f.url = "http://127.0.0.1:" + std::to_string(puertoLocal_) + "/v1";
    f.clave.clear();
    f.modelo = "local"; // el alias del llama-server del programa
    f.nombre = tr("modelo local").toStdString();
    return true;
}

std::unique_ptr<registro::Entendedor> TotemsViewModel::crearEntendedor(const std::string& conn, const std::string& totem,
                                                                       registro::Conversacion& conv, bool avisar) {
    prepararModelo(); // por si el servidor se reinició: la próxima conversación ya lo tiene
    agent::FuenteMirada f;
    std::string prompt, falta;
    if (!fuenteAgente(f, prompt, falta)) {
        if (avisar)
            ba::diag::registro().linea(ba::diag::Nivel::Info, "registro", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("registro", "tótem «%1» (%2): por ahora el guion solo (%3)"), {totem, conn, falta}));
        return nullptr;
    }
    auto a = std::make_unique<registro::AgenteTotem>(f, prompt, conv, registro::AgenteTotem::tiemposDeAjustes());
    QPointer<TotemsViewModel> yo(this);
    // Lo que va escribiendo: a la pantalla del tótem en el acto (y a quien mira la conversación).
    a->alTexto = [yo, conn](const std::string& acumulado) {
        if (yo) yo->vivo_->alTextoModelo(conn, acumulado, ahoraMs());
    };
    // Frase por frase a la voz, mientras sigue escribiendo.
    a->alFrase = [yo, conn](const std::string& frase) {
        if (yo) yo->vivo_->alFraseModelo(conn, frase, ahoraMs());
    };
    // Terminó el turno: lo que hace el guion (confirmar lo anotado, retomar la pregunta…).
    a->alListo = [yo, conn](const registro::Conversacion::Respuesta& r, bool) {
        if (yo) yo->vivo_->alListoModelo(conn, r, ahoraMs());
    };
    a->alCaer = [yo, conn](const std::string& motivo) {
        if (yo) yo->vivo_->alCaerModelo(conn, motivo);
    };
    // Al empezar la conversación (mientras saluda): el primer turno ya encuentra el sistema y
    // las herramientas leídos. Creado en un turno (el modelo recién cargó), no hay tiempo.
    if (avisar) a->precalentar();
    return std::make_unique<EntendedorTotem>(std::move(a));
}

} // namespace ba::app
