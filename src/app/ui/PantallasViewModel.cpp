// ============================================================================
// src/app/ui/PantallasViewModel.cpp — ver PantallasViewModel.hpp
// ============================================================================
#include "ui/PantallasViewModel.hpp"

#include "arranque/ArchivoConfig.hpp"
#include "ajustes/Ajustes.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QMetaObject>
#include <QNetworkInterface>
#include <QTimer>
#include <QUdpSocket>

#include "arranque/RuntimeMotor.hpp"
#include "i18n/I18nTexto.hpp"
#include "emision/BroadcastEngine.hpp"
#include "emision/VentanillaMotor.hpp"
#include "escenas/RefEscena.hpp"
#include "ipc/CanalesEscena.hpp"
#include "diag/Registro.hpp"
#include "pantallas/Buscar.hpp"
#include "pantallas/Cast.hpp"
#include "pantallas/Destinos.hpp"
#include "sala/ControlSala.hpp"
#include "sala/EnlacesSala.hpp"
#include "sala/PantallasSala.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "platform/Hilo.hpp"

namespace ba::app {
namespace {

using json = nlohmann::json;
namespace pt = pantallas;

/// Cuánto se espera a un aparato antes de darlo por perdido. Un televisor
/// dormido tarda en despertar; más de esto es que no está.
inline int kTimeoutMs() { return ba::ajustes::entero("pantallas.timeoutMs", 8000); }
/// Cuánto dura una búsqueda en la red.
inline int kBusquedaSeg() { return ba::ajustes::entero("pantallas.busquedaS", 5); }

std::int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }
QString qs(const std::string& s) { return QString::fromStdString(s); }

json aJson(const QVariantMap& m) {
    const json j = json::parse(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson(QJsonDocument::Compact).toStdString(),
                               nullptr, false);
    return j.is_object() ? j : json::object();
}

QVariantMap aMapa(const json& j) {
    if (!j.is_object()) return {};
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, json::error_handler_t::replace)))
        .object()
        .toVariantMap();
}

/// Por dónde los hilos de red (buscar, preguntar, mostrar…) le devuelven el resultado a la
/// vista. Cada hilo se lleva su copia; el destructor la cierra antes de que la vista muera:
/// lo que llega después se descarta. Sin esto, cerrar la app con una búsqueda en curso
/// llamaba a invokeMethod sobre la vista ya destruida (SIGSEGV al salir, y el lanzador la
/// volvía a abrir).
struct Puerta {
    std::mutex mu;
    QObject* vista = nullptr;
};
template <typename F>
void alHiloPrincipal(const std::shared_ptr<Puerta>& p, F&& f) {
    std::lock_guard<std::mutex> l(p->mu);
    if (p->vista != nullptr) QMetaObject::invokeMethod(p->vista, std::forward<F>(f), Qt::QueuedConnection);
}

/// Todo lo que pasa con una pantalla queda escrito: mandar algo a un televisor
/// es una acción que se ve en la sala, y cuando algo sale mal (el aparato no
/// contesta, la página no carga) el registro tiene que poder contarlo después.
void anotar(diag::Nivel n, const std::string& texto) { diag::registro().linea(n, "pantallas", 0, texto); }

/// La dirección de ESTA máquina con la que ese aparato nos ve.
///
/// SE BUSCA LA INTERFAZ QUE LO TIENE EN SU RED, y NO se le pregunta a la tabla
/// de rutas. Parece al revés de lo razonable, así que el porqué:
///
/// Una red superpuesta (VPN, ZeroTier, Tailscale) puede instalar una ruta para
/// un rango privado —192.168.1.0/24, por ejemplo— que TAPA la de la placa de
/// red local. Pasó tal cual, y se ve en `ip route`:
///
///     192.168.1.0/24 via 192.168.199.1 dev zt... proto static metric 5000
///
/// Con esa ruta puesta, un televisor que está en la MISMA WiFi que nosotros se
/// alcanza dando la vuelta por el túnel. Funciona para hablarle —el aparato
/// contesta— pero nos ve llegar desde una dirección del túnel, y entonces la
/// URL que le damos apunta a un lugar al que él no puede volver: se queda
/// «cargando» para siempre, sin decir nada.
///
/// Una interfaz cuya red CONTIENE la dirección del aparato está,
/// necesariamente, en el mismo segmento que él: lo alcanza directo y él nos
/// puede contestar. Eso no lo puede decir la tabla de rutas, que va a preferir
/// lo que le hayan metido.
QString ipEnLaRedDe(const QString& host) {
    const QHostAddress destino(host);
    if (destino.isNull()) return {};
    for (const QNetworkInterface& ifc : QNetworkInterface::allInterfaces()) {
        const auto f = ifc.flags();
        if (!f.testFlag(QNetworkInterface::IsUp) || !f.testFlag(QNetworkInterface::IsRunning)) continue;
        if (f.testFlag(QNetworkInterface::IsLoopBack)) continue;
        for (const QNetworkAddressEntry& e : ifc.addressEntries()) {
            if (e.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (e.prefixLength() <= 0) continue;
            if (destino.isInSubnet(e.ip(), e.prefixLength())) return e.ip().toString();
        }
    }
    return {};
}

/// La dirección nuestra para hablarle: la de su propia red si la hay; si no,
/// la que diga el sistema (un aparato que de verdad está del otro lado de un
/// túnel solo se alcanza así).
QString ipParaAlcanzar(const QStringList& mias, const QString& host) {
    const QString propia = ipEnLaRedDe(host);
    if (!propia.isEmpty()) return propia;
    if (!host.isEmpty()) {
        QUdpSocket s;
        s.connectToHost(QHostAddress(host), 9); // UDP: "conectar" solo elige la ruta
        if (s.state() == QAbstractSocket::ConnectedState || s.waitForConnected(300)) {
            const QHostAddress local = s.localAddress();
            if (!local.isNull() && !local.isLoopback()) return local.toString();
        }
    }
    return mias.isEmpty() ? QString() : mias.first();
}

/// Los canales de escena del motor, mirados EN EL DUEÑO DEL MOTOR.
pt::CanalesDelMotor canalesDe(BroadcastEngine& eng) {
    pt::CanalesDelMotor c;
    c.cantidad = ipc::kCanalesEscena;
    c.escena = [&eng](int k) { return eng.escenaDeCanal(k); };
    c.conDuenos = [&eng](int k) { return eng.canalEscenaConDuenos(k); };
    return c;
}
/// EN EL DUEÑO DEL MOTOR: suelta los canales de escena que ninguna pantalla usa (ni son de
/// las vistas de los remotos). Devuelve los soltados: {canal, escena}.
std::vector<std::pair<int, std::string>> soltarCanales(BroadcastEngine& eng, const std::set<int>& usados) {
    const auto soltados = pt::aSoltar(canalesDe(eng), usados);
    for (const auto& [k, e] : soltados) eng.escenaACanal(k, "");
    return soltados;
}
struct CanalPedido {
    int canal = -1;
    std::string error;
    std::vector<std::pair<int, std::string>> soltados;
};

} // namespace

struct PantallasViewModel::Impl {
    AppRuntime* rt = nullptr;
    VentanillaMotor* v = nullptr;
    /// revisarCanales: hay un pedido al motor en camino; y si mientras tanto hubo otro cambio.
    bool revisando = false, revisarOtraVez = false;
    ConferenceViewModel* sala = nullptr;
    std::filesystem::path archivo;
    /// Las pantallas, los carruseles y sus reglas (el modelo: pantallas/Destinos).
    pt::Destinos destinos;
    std::atomic<bool> buscando{false};
    std::shared_ptr<Puerta> puerta = std::make_shared<Puerta>();
    QString aviso;
    /// Las IP de esta máquina por las que la sala atiende y el puerto de su puerta HTTP
    /// (src/gateway/Direcciones: la red local, sin certificado), que se preguntan y se
    /// refrescan al buscar. 0 = la sala no tiene la puerta HTTP abierta.
    QStringList misIps;
    int puertoWeb = 0;
    /// El origen con certificado de verdad (un tunel), si lo hay. Ver `urlDe`:
    /// sin esto no se le puede mostrar una pagina NUESTRA a un Chromecast.
    QString origenSeguro;
    QVariantList superficies;
    /// Lo que la sala sirve a las pantallas (modelo); nulo sin sala.
    std::unique_ptr<sala::PantallasSala> salaPantallas;
    /// Cada tanto se revisa si cada pantalla está mostrando lo que le fijaron.
    QTimer mantenimiento;

    /// Las direcciones de esta máquina (y el origen seguro) según la sala.
    void pedirDirecciones(std::function<void()> despues) {
        if (sala == nullptr) return;
        sala->control().verRed([this, despues](const std::string& error, const nlohmann::json& r) {
            if (!error.empty()) return;
            misIps.clear();
            puertoWeb = 0;
            const nlohmann::json loc = r.value("direcciones", nlohmann::json::object()).value("local", nlohmann::json());
            if (loc.is_object()) {
                for (const auto& v : loc.value("ips", nlohmann::json::array()))
                    if (v.is_string()) misIps << QString::fromStdString(v.get<std::string>());
                const QUrl http(QString::fromStdString(loc.value("http", std::string())));
                puertoWeb = http.port(0);
            }
            const auto o = r.find("origenSeguro");
            origenSeguro = o != r.end() && o->is_string() ? QString::fromStdString(o->get<std::string>()) : QString();
            if (despues) despues();
        });
    }

    /// La base de las URLs que el televisor `idDestino` puede alcanzar: HTTP (nuestro HTTPS
    /// es autofirmado y un televisor no tiene dónde aceptarlo) y la dirección NUESTRA con la
    /// que ese aparato nos ve, que la dice el sistema y no se adivina (ver ipParaAlcanzar).
    /// "" si todavía no se sabe.
    std::string base(const std::string& idDestino) const {
        QString ip;
        if (const pt::Destino* d = idDestino.empty() ? nullptr : destinos.porId(idDestino); d != nullptr)
            ip = ipParaAlcanzar(misIps, qs(d->host));
        else if (!misIps.isEmpty())
            ip = misIps.first();
        if (ip.isEmpty() || puertoWeb <= 0) return {};
        return ("http://" + ip + ":" + QString::number(puertoWeb)).toStdString();
    }

    void guardar() const {
        if (!archivo.empty()) config::guardar(archivo, destinos.aJson());
    }
};

PantallasViewModel::PantallasViewModel(AppRuntime& rt, VentanillaMotor& ventanilla, ConferenceViewModel* sala, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>()) {
    d_->puerta->vista = this;
    d_->rt = &rt;
    d_->v = &ventanilla;
    d_->sala = sala;
    if (sala != nullptr) d_->salaPantallas = std::make_unique<sala::PantallasSala>(sala->client());
    d_->archivo = rt.dataRoot() / "Config" / "pantallas.json";
    // El formato viejo (un arreglo de pantallas a secas) lo trae el esquema 2.
    d_->destinos.deJson(config::leerObjeto(d_->archivo));
    if (!pt::sePuedeBuscar())
        d_->aviso = tr("Este programa se compiló sin búsqueda en la red: agregá el televisor a mano con su "
                       "dirección IP.");
    // Los carruseles viven en la MEMORIA del gateway: si se reinicia, se los
    // olvida y las pantallas que los estuvieran mostrando dirían «no encuentro
    // ese carrusel». Por eso se republican cada vez que vuelve (y al registrarse,
    // si ya estaba).
    if (sala != nullptr) {
        sala->client().registrarRepublicador("carruseles", this, [this]() {
            for (const pt::Carrusel& c : d_->destinos.carruseles()) publicarCarrusel(qs(c.id));
        });
    }

    // Lo que cada pantalla tiene que estar mostrando se revisa cada tanto: un
    // televisor que se reinició de madrugada vuelve solo a lo suyo.
    d_->mantenimiento.setInterval(std::max(2000, ba::ajustes::sAMs("pantallas.revisarS", 15)));
    connect(&d_->mantenimiento, &QTimer::timeout, this, [this]() { restaurar(); });
    // Un canal de escena vive mientras alguna pantalla lo muestre: si la que lo
    // usaba pasó a otra cosa, se apagó o se la tomó otra aplicación, se suelta
    // (es una composición entera andando de gusto).
    connect(this, &PantallasViewModel::cambio, this, [this]() { revisarCanales(); });
    d_->mantenimiento.start();
    // Al arrancar se busca una vez: sin eso, una pantalla con IP nueva no se
    // encontraría hasta que alguien apretara el botón.
    QTimer::singleShot(4000, this, [this]() { buscar(); });

    // Las direcciones de esta máquina, para armar URLs que el televisor pueda
    // alcanzar. Se pregunta una vez al arrancar y en cada búsqueda.
    d_->pedirDirecciones([this]() {
        emit direccionesCambio();
        emit cambio();
    });
}

PantallasViewModel::~PantallasViewModel() {
    std::lock_guard<std::mutex> l(d_->puerta->mu); // los hilos que sigan no llegan más acá
    d_->puerta->vista = nullptr;
}

// --- Lo que ve la interfaz -----------------------------------------------------------

QVariantList PantallasViewModel::pantallas() const {
    QVariantList out;
    const std::int64_t ahora = ahoraMs();
    for (const pt::Destino& d : d_->destinos.lista())
        out.append(QVariantMap{{"id", qs(d.id)},
                               {"nombre", qs(d.nombre)},
                               {"modelo", qs(d.modelo)},
                               {"host", qs(d.host)},
                               {"puerto", d.puerto},
                               {"fija", d.fija},
                               {"estado", qs(d.estado)},
                               {"contenido", qs(d.contenido)},
                               {"que", qs(d.que)},
                               {"error", qs(d.error)},
                               {"volumen", d.volumen},
                               {"silenciado", d.silenciado},
                               {"trabajando", d.trabajando},
                               {"puedeMostrar", d.puedeMostrar},
                               {"fijadoTipo", qs(d.fijadoTipo)},
                               {"fijadoDatos", aMapa(d.fijadoDatos)},
                               {"vistaHace", static_cast<int>(d.vistaMs > 0 ? (ahora - d.vistaMs) / 1000 : -1)}});
    return out;
}

QVariantList PantallasViewModel::contenidos() const {
    QVariantList out;
    for (const pt::Contenido& c : pt::contenidos()) {
        QVariantList campos;
        for (const pt::Campo& f : c.campos)
            campos.append(QVariantMap{{"clave", QString::fromUtf8(f.clave)},
                                      {"etiqueta", qs(pt::texto(f.etiqueta))},
                                      {"ejemplo", qs(pt::texto(f.ejemplo))},
                                      {"largo", f.largo},
                                      {"opciones", QString::fromUtf8(f.opciones)}});
        out.append(QVariantMap{{"id", QString::fromUtf8(c.id)},
                               {"titulo", qs(pt::texto(c.titulo))},
                               {"ayuda", qs(pt::texto(c.ayuda))},
                               {"campos", campos},
                               {"disponible", c.disponible},
                               {"porque", qs(pt::texto(c.porque))}});
    }
    return out;
}

bool PantallasViewModel::buscando() const { return d_->buscando.load(); }
QString PantallasViewModel::aviso() const { return d_->aviso; }
int PantallasViewModel::mostrando() const { return d_->destinos.mostrando(); }

// --- URLs ----------------------------------------------------------------------------

QString PantallasViewModel::urlDe(const QString& tipo, const QVariantMap& datos, const QString& idDestino) {
    const std::string t = tipo.toStdString();
    // `url` va tal cual y lo que va en vivo lo pide el gateway: no hace falta la dirección.
    const std::string base = t == "url" || pt::esEnVivo(t) ? std::string() : d_->base(idDestino.toStdString());
    return qs(pt::url(t, aJson(datos), base));
}

void UrlDePantalla::setPantallas(QObject* p) {
    auto* vm = qobject_cast<PantallasViewModel*>(p);
    if (vm == pantallas_) return;
    if (pantallas_) disconnect(pantallas_, nullptr, this, nullptr);
    pantallas_ = vm;
    if (vm != nullptr) {
        // Lo que sabe el modelo: las pantallas (su dirección) y las direcciones de esta máquina.
        connect(vm, &PantallasViewModel::cambio, this, &UrlDePantalla::armar);
        connect(vm, &PantallasViewModel::direccionesCambio, this, &UrlDePantalla::armar);
    }
    emit entradaCambio();
    armar();
}

void UrlDePantalla::setTipo(const QString& t) {
    if (t == tipo_) return;
    tipo_ = t;
    emit entradaCambio();
    armar();
}

void UrlDePantalla::setDatos(const QVariantMap& d) {
    if (d == datos_) return;
    datos_ = d;
    emit entradaCambio();
    armar();
}

void UrlDePantalla::setDestino(const QString& d) {
    if (d == destino_) return;
    destino_ = d;
    emit entradaCambio();
    armar();
}

void UrlDePantalla::armar() {
    const QString u = pantallas_ ? pantallas_->urlDe(tipo_, datos_, destino_) : QString();
    if (u == url_) return;
    url_ = u;
    emit urlCambio();
}

// --- Buscar --------------------------------------------------------------------------

void PantallasViewModel::buscar() {
    if (d_->buscando.exchange(true)) return; // ya hay una en curso
    emit cambio();
    d_->pedirDirecciones([this]() { emit direccionesCambio(); });
    std::thread([this, puerta = d_->puerta]() {
        ba::platform::nombrarHilo("tv-buscar");
        std::string err;
        auto encontrados = pt::buscar(kBusquedaSeg(), &err);
        alHiloPrincipal(puerta, [this, encontrados, err]() {
            d_->buscando = false;
            if (!err.empty()) {
                d_->aviso = QString::fromStdString(err);
            } else {
                d_->aviso.clear();
                d_->destinos.alBuscar(encontrados, ahoraMs());
                d_->guardar();
                anotar(diag::Nivel::Info,
                       ba::i18n::llenar(ba::i18n::traducir("pantallas", "busqueda en la red: %1 encontradas"), {std::to_string(encontrados.size())}));
                // Se le pregunta a cada una cómo está, escalonado: una lista
                // que dice «sin consultar» no sirve de nada, y abrir diez
                // canales TLS a la vez tampoco.
                int n = 0;
                for (const pt::Destino& d : d_->destinos.lista()) {
                    const QString id = qs(d.id);
                    QTimer::singleShot(300 * n++, this, [this, id]() { refrescar(id); });
                }
                // Un aparato que vuelve a aparecer retoma lo suyo enseguida. Va después de
                // las consultas: así ya se sabe si puede mostrar.
                QTimer::singleShot(300 * n + 1500, this, [this]() { restaurar(); });
                emit notify(encontrados.empty() ? tr("No encontré ningún televisor en la red")
                                                : tr("Encontré %n pantalla", "", static_cast<int>(encontrados.size())),
                            encontrados.empty() ? "warn" : "ok");
            }
            emit cambio();
        });
    }).detach();
}

void PantallasViewModel::agregar(const QString& nombre, const QString& host, int puerto) {
    std::string id;
    if (const std::string e = d_->destinos.agregar(nombre.toStdString(), host.toStdString(), puerto, &id); !e.empty())
        return avisarError(e);
    d_->guardar();
    emit cambio();
    refrescar(qs(id)); // que diga enseguida si contesta o no
}

void PantallasViewModel::olvidar(const QString& id) {
    if (const std::string e = d_->destinos.olvidar(id.toStdString()); !e.empty()) return avisarError(e);
    d_->guardar();
    emit cambio();
}

void PantallasViewModel::renombrar(const QString& id, const QString& nombre) {
    if (const std::string e = d_->destinos.renombrar(id.toStdString(), nombre.toStdString()); !e.empty()) return avisarError(e);
    d_->guardar();
    emit cambio();
}

void PantallasViewModel::fijar(const QString& id, const QString& tipo, const QVariantMap& datos) {
    if (const std::string e = d_->destinos.fijar(id.toStdString(), tipo.toStdString(), aJson(datos)); !e.empty())
        return avisarError(e);
    d_->guardar();
    emit cambio();
    // Y se lo manda ya: fijar algo sin verlo aparecer no da confianza.
    if (tipo.startsWith("carrusel:")) return mostrarCarrusel(id, tipo.mid(9));
    return mostrar(id, tipo, datos);
}

void PantallasViewModel::fijarLoQueMuestra(const QString& id) {
    if (const std::string e = d_->destinos.fijarLoQueMuestra(id.toStdString()); !e.empty()) return avisarError(e);
    d_->guardar();
    emit cambio();
}

void PantallasViewModel::desfijar(const QString& id) {
    if (const std::string e = d_->destinos.desfijar(id.toStdString()); !e.empty()) return avisarError(e);
    d_->guardar();
    emit cambio();
}

// --- Carruseles -----------------------------------------------------------------------

QVariantList PantallasViewModel::carruseles() const {
    QVariantList out;
    for (const pt::Carrusel& c : d_->destinos.carruseles()) {
        QVariantList pasos;
        int total = 0;
        for (const pt::Paso& p : c.pasos) {
            // Un resumen legible: qué es y su primer dato con algo.
            pasos.append(QVariantMap{
                {"tipo", qs(p.tipo)}, {"datos", aMapa(p.datos)}, {"segundos", p.segundos}, {"que", qs(pt::que(p.tipo, p.datos, 40))}});
            total += p.segundos;
        }
        out.append(QVariantMap{
            {"id", qs(c.id)}, {"nombre", qs(c.nombre)}, {"transicion", qs(c.transicion)}, {"pasos", pasos}, {"vueltaSeg", total}});
    }
    return out;
}

QString PantallasViewModel::carruselNuevo(const QString& nombre) {
    const std::string id = d_->destinos.carruselNuevo(nombre.toStdString(), ahoraMs());
    d_->guardar();
    emit carruselesCambio();
    return qs(id);
}

QString PantallasViewModel::carruselBorrar(const QString& id) {
    if (const std::string e = d_->destinos.carruselBorrar(id.toStdString()); !e.empty()) return qs(e);
    d_->guardar();
    // Se lo saca del gateway también: una pantalla que lo estuviera mostrando
    // va a decir que no existe, que es la verdad.
    if (d_->salaPantallas) d_->salaPantallas->borrarCarrusel(id.toStdString());
    emit carruselesCambio();
    return {};
}

QString PantallasViewModel::carruselRenombrar(const QString& id, const QString& nombre) {
    if (const std::string e = d_->destinos.carruselRenombrar(id.toStdString(), nombre.toStdString()); !e.empty()) return qs(e);
    d_->guardar();
    emit carruselesCambio();
    return {};
}

QString PantallasViewModel::carruselTransicion(const QString& id, const QString& transicion) {
    if (const std::string e = d_->destinos.carruselTransicion(id.toStdString(), transicion.toStdString()); !e.empty())
        return qs(e);
    d_->guardar();
    publicarCarrusel(id);
    emit carruselesCambio();
    return {};
}

bool PantallasViewModel::pasoAgregar(const QString& carrusel, const QString& tipo, const QVariantMap& datos, int segundos) {
    const std::string e = d_->destinos.pasoAgregar(carrusel.toStdString(), tipo.toStdString(), aJson(datos), segundos);
    if (!e.empty()) return sinError(e);
    d_->guardar();
    publicarCarrusel(carrusel);
    emit carruselesCambio();
    return true;
}

void PantallasViewModel::pasoQuitar(const QString& carrusel, int indice) {
    if (const std::string e = d_->destinos.pasoQuitar(carrusel.toStdString(), indice); !e.empty()) return avisarError(e);
    d_->guardar();
    publicarCarrusel(carrusel);
    emit carruselesCambio();
}

QString PantallasViewModel::pasoMover(const QString& carrusel, int indice, int aIndice) {
    if (const std::string e = d_->destinos.pasoMover(carrusel.toStdString(), indice, aIndice); !e.empty()) return qs(e);
    d_->guardar();
    publicarCarrusel(carrusel);
    emit carruselesCambio();
    return {};
}

QString PantallasViewModel::pasoSegundos(const QString& carrusel, int indice, int segundos) {
    if (const std::string e = d_->destinos.pasoSegundos(carrusel.toStdString(), indice, segundos); !e.empty()) return qs(e);
    d_->guardar();
    publicarCarrusel(carrusel);
    emit carruselesCambio();
    return {};
}

void PantallasViewModel::publicar(const QString& id, const QString& hostDestino) {
    const pt::Carrusel* c = d_->destinos.carrusel(id.toStdString());
    if (c == nullptr || !d_->salaPantallas) return;
    // Cada paso se resuelve a una URL alcanzable DESDE el televisor: por eso hace falta
    // saber a cuál va (elige la IP de su misma red).
    const std::string base = d_->base(hostDestino.toStdString());
    std::vector<sala::PasoCarrusel> pasos;
    for (const pt::Paso& p : c->pasos) {
        const std::string url = pt::url(p.tipo, p.datos, base);
        if (url.empty()) continue;
        const pt::Contenido* con = pt::contenido(p.tipo);
        pasos.push_back({url, p.segundos, con == nullptr ? p.tipo : pt::texto(con->titulo)});
    }
    if (pasos.empty()) return;
    anotar(diag::Nivel::Info, ba::i18n::llenar(ba::i18n::traducir("pantallas", "publicó el carrusel «%1» (%2 pasos)"), {c->nombre, std::to_string(pasos.size())}));
    d_->salaPantallas->publicarCarrusel(c->id, c->nombre, c->transicion, pasos);
}

void PantallasViewModel::publicarCarrusel(const QString& id) {
    // Sin pantalla concreta: la que lo esté mostrando o cualquiera. Al mandarlo a una
    // pantalla se vuelve a publicar con la suya.
    publicar(id, qs(d_->destinos.destinoDeCarrusel(id.toStdString())));
}

void PantallasViewModel::publicarTally(const QString& aire, const QString& armada, const QStringList& fuentesAire,
                                       const QStringList& fuentesArmadas, bool transmitiendo, bool grabando) {
    if (!d_->salaPantallas) return;
    const auto lista = [](const QStringList& l) {
        std::vector<std::string> v;
        for (const QString& s : l) v.push_back(s.toStdString());
        return v;
    };
    d_->salaPantallas->tally(aire.toStdString(), armada.toStdString(), lista(fuentesAire), lista(fuentesArmadas), transmitiendo,
                             grabando);
}

void PantallasViewModel::mostrarCarrusel(const QString& idPantalla, const QString& carrusel) {
    pt::Destino* d = d_->destinos.porId(idPantalla.toStdString());
    if (d == nullptr) return avisarError(tr("No encuentro esa pantalla."));
    const pt::Carrusel* c = d_->destinos.carrusel(carrusel.toStdString());
    if (c == nullptr) return avisarError(tr("No encuentro ese carrusel."));
    if (c->pasos.empty()) return avisarError(tr("Ese carrusel no tiene ningún paso todavía."));
    if (d->trabajando) return avisarError(tr("Esperá: está ocupada con lo anterior."));
    if (d->puedeMostrar == 0) return avisarError(tr("%1 no sabe mostrar páginas web.").arg(qs(d->nombre)));
    // Primero la lista (con las URLs que ESTE televisor puede alcanzar), y
    // después se lo manda: si fuera al revés, abriría una lista que no está.
    publicar(carrusel, idPantalla);
    const std::string base = d_->base(d->id);
    if (base.empty()) return avisarError(tr("Todavía no sé con qué dirección me ve el televisor: probá de nuevo en unos segundos."));
    const std::string url = base + "/pantalla/carrusel/?id=" + pt::codificar(carrusel.toStdString());
    anotar(diag::Nivel::Info, ba::i18n::llenar(ba::i18n::traducir("pantallas", "a «%1»: carrusel «%2» -> %3"), {d->nombre, c->nombre, url}));
    d->ultimosDatos = {{"carrusel", carrusel.toStdString()}};
    d->canalEscena = -1;
    revisarCanales();
    mandarUrl(idPantalla, qs(url), "carrusel:" + carrusel, tr("Carrusel — %1").arg(qs(c->nombre)));
}

// --- El canal de control ---------------------------------------------------------------

QVariantList PantallasViewModel::superficies() const { return d_->superficies; }

void PantallasViewModel::pedirSuperficies() { pedirSuperficies(nullptr); }

void PantallasViewModel::pedirSuperficies(std::function<void(QVariantList)> luego) {
    if (!d_->salaPantallas) {
        if (luego) luego({});
        return;
    }
    d_->salaPantallas->superficies([this, luego](const std::string& error, const nlohmann::json& r) {
        if (!error.empty()) {
            if (luego) luego({});
            return;
        }
        d_->superficies.clear();
        for (const auto& v : r.value("superficies", nlohmann::json::array()))
            if (v.is_object()) d_->superficies.append(aMapa(v));
        emit superficiesCambio();
        if (luego) luego(d_->superficies);
    });
}

void PantallasViewModel::empujar(const QString& a, const QString& tipo, const QString& url) {
    empujar(a, tipo, url, false, nullptr);
}

void PantallasViewModel::empujar(const QString& a, const QString& tipo, const QString& url, bool recargar,
                                 std::function<void(bool, int)> luego) {
    if (!d_->salaPantallas) {
        if (luego) luego(false, 0);
        return;
    }
    anotar(diag::Nivel::Info,
           ba::i18n::llenar(ba::i18n::traducir("pantallas", "empujo a %1: %2"), {a.isEmpty() ? ba::i18n::llenar(ba::i18n::traducir("pantallas", "todas(%1)"), {tipo.toStdString()}) : a.toStdString(),
                             recargar ? ba::i18n::traducir("pantallas", "recargar") : url.toStdString()}));
    d_->salaPantallas->mostrarEnSuperficies(
        a.toStdString(), tipo.toStdString(), url.toStdString(), recargar,
        [this, luego](const std::string& error, const nlohmann::json& r) {
            pedirSuperficies();
            const auto n = r.find("a");
            if (luego) luego(error.empty(), n != r.end() && n->is_number() ? n->get<int>() : 0);
        });
}

// --- Restaurar lo fijado --------------------------------------------------------------

void PantallasViewModel::restaurar() {
    for (const auto& r : d_->destinos.paraRestaurar(ahoraMs())) {
        if (r.tipo.rfind("carrusel:", 0) == 0)
            mostrarCarrusel(qs(r.id), qs(r.tipo.substr(9)));
        else
            mostrar(qs(r.id), qs(r.tipo), aMapa(r.datos));
    }
}

// --- Hablarle a un aparato ------------------------------------------------------------
//
// Se sale por la interfaz que lo tiene en su red, aunque la tabla de rutas prefiera un
// túnel (ver ipEnLaRedDe)… para la URL. La CONEXION la rutea el sistema, como siempre:
// atarla a una interfaz la rompía («el aparato rechazó la conexión segura»). Una es cómo
// le hablamos nosotros, la otra es cómo nos encuentra él.

void PantallasViewModel::refrescar(const QString& id) {
    pt::Destino* d = d_->destinos.porId(id.toStdString());
    if (d == nullptr || d->trabajando) return;
    d->trabajando = true;
    emit cambio();
    std::thread([this, puerta = d_->puerta, id = d->id, host = d->host, puerto = d->puerto]() {
        ba::platform::nombrarHilo("tv-consultar");
        const pt::Estado r = pt::consultar(host, puerto, kTimeoutMs());
        alHiloPrincipal(puerta, [this, id, r]() {
            d_->destinos.alConsultar(id, r, ahoraMs());
            emit cambio();
        });
    }).detach();
}

void PantallasViewModel::mostrar(const QString& id, const QString& tipo, const QVariantMap& datos) {
    pt::Destino* d = d_->destinos.porId(id.toStdString());
    if (d == nullptr) return avisarError(tr("No encuentro esa pantalla."));
    if (d->trabajando) return avisarError(tr("Esperá: está ocupada con lo anterior."));
    const pt::Contenido* c = pt::contenido(tipo.toStdString());
    if (c == nullptr) return avisarError(tr("No sé mostrar «%1».").arg(tipo));
    if (!c->disponible) return avisarError(pt::texto(c->porque));
    // Si ya se preguntó y el aparato no sabe abrir páginas, se dice ANTES de
    // abrirle un canal y pisarle lo que esté pasando. (Un parlante sin
    // pantalla contesta el protocolo igual, pero no tiene dónde mostrar.)
    if (d->puedeMostrar == 0)
        return avisarError(tr("%1 no sabe mostrar páginas web: contesta el protocolo, pero no tiene dónde "
                              "mostrarlas (¿es un parlante?).")
                               .arg(qs(d->nombre)));
    if (tipo != "escena") {
        d->canalEscena = -1;
        revisarCanales();
    }
    // La votacion necesita dos cosas que solo sabe el gateway: la clave con la
    // que se lee la urna y la direccion que va adentro del QR. Se las pide y
    // recien despues manda, porque una pantalla con el QR equivocado manda a
    // toda la sala a un lugar que no existe.
    if (tipo == "votacion" && datos.value("clavePublico").toString().isEmpty()) {
        if (d_->sala == nullptr) return avisarError(tr("El servidor no está corriendo: sin él no hay urna que mostrar."));
        d->trabajando = true;
        emit cambio();
        d_->sala->enlaces().publico(false, [this, id, datos](const sala::Enlace& e) mutable {
            pt::Destino* d = d_->destinos.porId(id.toStdString());
            if (d == nullptr) return;
            d->trabajando = false;
            if (!e.ok) {
                d->estado = "error";
                d->error = tr("No pude sacar el enlace del público").toStdString();
                emit notify(tr("%1: %2").arg(qs(d->nombre), qs(d->error)), "error");
                emit cambio();
                return;
            }
            datos["clavePublico"] = QString::fromStdString(e.clave);
            datos["entrada"] = QString::fromStdString(e.url);
            mostrar(id, "votacion", datos);
        });
        return;
    }
    if (tipo == "programa") {
        if (d_->sala == nullptr) return avisarError(tr("El servidor no está corriendo: sin él no hay programa que mandar."));
        d->trabajando = true;
        emit cambio();
        d_->sala->enlaces().pantalla(d->nombre, -1, [this, id](const sala::Enlace& e) {
            pt::Destino* d = d_->destinos.porId(id.toStdString());
            if (d == nullptr) return;
            d->trabajando = false;
            if (!e.ok) {
                d->estado = "error";
                const std::string m = e.respuesta.value("message", std::string());
                d->error = m.empty() ? tr("No pude sacar el enlace del programa").toStdString() : m;
                emit notify(tr("%1: %2").arg(qs(d->nombre), qs(d->error)), "error");
                emit cambio();
                return;
            }
            mandarUrl(id, QString::fromStdString(e.url), "programa", tr("Lo que sale al aire"));
        });
        return;
    }
    if (tipo == "escena") {
        // Por el id si lo tiene (lo guardado de una pantalla fijada): un renombre no la pierde.
        const std::string pedida = escenas::apunta(datos.value("escena_id").toString().toStdString(),
                                                   datos.value("escena").toString().trimmed().toStdString(),
                                                   escenas::TablaEscenas(d_->v->foto().idsDeEscenas()));
        return avisarError(mostrarEscena(id, QString::fromStdString(pedida)));
    }
    const QString url = urlDe(tipo, datos, id);
    if (url.isEmpty())
        return avisarError(tipo == "url" ? tr("Poné una dirección.")
                                         : tr("Todavía no sé con qué dirección me ve el televisor: probá de nuevo en unos "
                                              "segundos, o fijate que el servidor esté corriendo."));
    // Lo legible, para la lista: qué le mandamos, no la URL entera.
    const std::string que = pt::que(tipo.toStdString(), aJson(datos), 60);
    d->ultimosDatos = aJson(datos);
    anotar(diag::Nivel::Info, ba::i18n::llenar(ba::i18n::traducir("pantallas", "a «%1» (%2): %3 -> %4"), {d->nombre, d->host, que, url.toStdString()}));
    mandarUrl(id, url, tipo, qs(que));
}

QString PantallasViewModel::mostrarEscena(const QString& id, const QString& pedida) {
    pt::Destino* d = d_->destinos.porId(id.toStdString());
    if (d == nullptr) return tr("No encuentro esa pantalla.");
    if (pedida.isEmpty()) return tr("Elegí qué escena mostrar.");
    if (d_->sala == nullptr) return tr("El servidor no está corriendo: sin él no hay escena que mandar.");
    const FotoMotor::Escena* fe = d_->v->foto().escena(pedida.toStdString()); // por el nombre o por el id
    if (fe == nullptr) return tr("No hay ninguna escena que se llame «%1».").arg(pedida);
    const QString escena = QString::fromStdString(fe->nombre); // el de ahora (los canales se comparan así)
    // El canal lo elige el dueño del motor, que sabe qué tiene cada uno: el que ya tiene esa
    // escena o uno que nadie esté usando (pantallas::canalPara). Mientras tanto la pantalla
    // queda «trabajando» y no se sueltan canales.
    d->canalEscena = -1;
    d->pidiendoCanal = true;
    d->trabajando = true;
    const std::set<int> usados = d_->destinos.canalesUsados();
    emit cambio();
    d_->v->pedir(
        this, "pantalla_canal",
        [&rt = *d_->rt, usados, e = escena.toStdString()]() {
            BroadcastEngine& eng = rt.engine();
            CanalPedido r;
            r.soltados = soltarCanales(eng, usados);
            r.canal = pt::canalPara(canalesDe(eng), e);
            if (r.canal < 0)
                r.error = ba::i18n::llenar(ba::i18n::traducir("pantallas", "Ya hay %1 escenas distintas en las pantallas: pasá "
                                                                           "alguna a una escena que ya se esté mostrando, o pará una."),
                                           {std::to_string(ipc::kCanalesEscena)});
            else if (!eng.escenaACanal(r.canal, e))
                r.error = ba::i18n::llenar(ba::i18n::traducir("pantallas", "No pude preparar la escena: %1"), {eng.lastError()});
            if (!r.error.empty()) r.canal = -1;
            return r;
        },
        [this, id, escena](const CanalPedido& r) {
            for (const auto& [k, e] : r.soltados)
                anotar(diag::Nivel::Info, ba::i18n::llenar(ba::i18n::traducir("pantallas", "canal de escena %1 («%2») sin pantallas: se suelta"), {std::to_string(k), e}));
            pt::Destino* d = d_->destinos.porId(id.toStdString());
            if (d == nullptr) return;
            d->pidiendoCanal = false;
            if (!r.error.empty()) {
                d->trabajando = false;
                d->estado = "error";
                d->error = r.error;
                emit notify(tr("%1: %2").arg(qs(d->nombre), qs(d->error)), "error");
                emit cambio();
                return;
            }
            enviarEscena(id, escena, r.canal);
        });
    return {};
}

void PantallasViewModel::enviarEscena(const QString& id, const QString& escena, int canal) {
    pt::Destino* d = d_->destinos.porId(id.toStdString());
    if (d == nullptr) return;
    d->canalEscena = canal;
    // Con el id al lado: lo fijado vuelve aunque después la renombren.
    d->ultimosDatos = {{"escena", escena.toStdString()}};
    if (const FotoMotor::Escena* fe = d_->v->foto().escena(escena.toStdString())) d->ultimosDatos["escena_id"] = fe->id;
    d->trabajando = true;
    emit cambio();
    anotar(diag::Nivel::Info, ba::i18n::llenar(ba::i18n::traducir("pantallas", "a «%1»: escena «%2» (canal %3)"), {d->nombre, escena.toStdString(), std::to_string(canal)}));
    d_->sala->enlaces().pantalla(d->nombre, canal, [this, id, escena](const sala::Enlace& e) {
        pt::Destino* d = d_->destinos.porId(id.toStdString());
        if (d == nullptr) return;
        d->trabajando = false;
        if (!e.ok) {
            d->estado = "error";
            const std::string m = e.respuesta.value("message", std::string());
            d->error = m.empty() ? tr("No pude sacar el enlace de la escena").toStdString() : m;
            d->canalEscena = -1;
            revisarCanales();
            emit notify(tr("%1: %2").arg(qs(d->nombre), qs(d->error)), "error");
            emit cambio();
            return;
        }
        mandarUrl(id, QString::fromStdString(e.url), "escena", tr("Escena — %1").arg(escena));
    });
}

void PantallasViewModel::revisarCanales() {
    // Mientras se pide un canal, no se suelta ninguno: el pedido todavía no está en `usados`.
    if (d_->destinos.pidiendoCanal()) return;
    // Un pedido por vez; los cambios que llegan mientras tanto, en uno más al volver.
    if (d_->revisando) {
        d_->revisarOtraVez = true;
        return;
    }
    d_->revisando = true;
    d_->v->pedir(
        this, "pantalla_soltar",
        [&rt = *d_->rt, usados = d_->destinos.canalesUsados()]() { return soltarCanales(rt.engine(), usados); },
        [this](const std::vector<std::pair<int, std::string>>& soltados) {
            for (const auto& [k, e] : soltados)
                anotar(diag::Nivel::Info, ba::i18n::llenar(ba::i18n::traducir("pantallas", "canal de escena %1 («%2») sin pantallas: se suelta"), {std::to_string(k), e}));
            d_->revisando = false;
            if (d_->revisarOtraVez) {
                d_->revisarOtraVez = false;
                revisarCanales();
            }
        });
}

bool PantallasViewModel::muestraPrograma() const { return d_->destinos.muestraPrograma(); }

/// Lo que hacen todos los contenidos al final: abrir el canal y mandar la URL.
void PantallasViewModel::mandarUrl(const QString& id, const QString& url, const QString& tipo, const QString& que) {
    pt::Destino* d = d_->destinos.porId(id.toStdString());
    if (d == nullptr) return;
    d->trabajando = true;
    d->error.clear();
    emit cambio();
    std::thread([this, puerta = d_->puerta, id = d->id, host = d->host, puerto = d->puerto, url = url.toStdString(),
                 tipo = tipo.toStdString(), que = que.toStdString()]() {
        ba::platform::nombrarHilo("tv-mostrar");
        // `reload` en 0: la página no se recarga sola. Un QR o un mensaje no
        // cambian, y una recarga en medio de un evento se ve.
        const pt::Estado r = pt::mostrarUrl(host, puerto, url, 0, kTimeoutMs());
        alHiloPrincipal(puerta, [this, id, r, tipo, que]() {
            const bool avisar = d_->destinos.alMostrar(id, r.ok, r.error, tipo, que, ahoraMs());
            if (const pt::Destino* d = d_->destinos.porId(id)) {
                if (r.ok)
                    anotar(diag::Nivel::Info, ba::i18n::llenar(ba::i18n::traducir("pantallas", "«%1» acepto: %2"), {d->nombre, que}));
                else
                    anotar(diag::Nivel::Aviso, ba::i18n::llenar(ba::i18n::traducir("pantallas", "«%1» no pudo: %2 (intento %3)"), {d->nombre, r.error, std::to_string(d->fallos)}));
                if (avisar) emit notify(tr("%1: %2").arg(qs(d->nombre), qs(d->error)), "error");
            }
            emit cambio();
        });
    }).detach();
}

void PantallasViewModel::parar(const QString& id) {
    pt::Destino* d = d_->destinos.porId(id.toStdString());
    if (d == nullptr || d->trabajando) return;
    d->trabajando = true;
    emit cambio();
    std::thread([this, puerta = d_->puerta, id = d->id, host = d->host, puerto = d->puerto]() {
        ba::platform::nombrarHilo("tv-parar");
        const pt::Estado r = pt::parar(host, puerto, kTimeoutMs());
        alHiloPrincipal(puerta, [this, id, r]() {
            d_->destinos.alParar(id, r.ok, r.error, ahoraMs());
            emit cambio();
        });
    }).detach();
}

void PantallasViewModel::silenciar(const QString& id, bool si) {
    pt::Destino* d = d_->destinos.porId(id.toStdString());
    if (d == nullptr || d->trabajando) return;
    d->trabajando = true;
    std::thread([this, puerta = d_->puerta, id = d->id, host = d->host, puerto = d->puerto, si]() {
        ba::platform::nombrarHilo("tv-orden");
        const pt::Estado r = pt::volumen(host, puerto, -1, si ? 1 : 0, kTimeoutMs());
        alHiloPrincipal(puerta, [this, id, r]() {
            d_->destinos.alSilenciar(id, r.ok, r.volumen, r.silenciado);
            emit cambio();
        });
    }).detach();
}

} // namespace ba::app
